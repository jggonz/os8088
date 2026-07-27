; =============================================================================
; loader.inc - package loader (SPEC.md 21)
;
; Validates an os88fs directory entry, allocates a region from the package
; pool (0xA000..0xEFFF), reads the .o88 v2 file into it, applies the
; relocation table (SPEC.md 20.2: class 0 entries += base-0xA000, class 1
; entries -= it), zeroes the package bss, near-calls its entry, and
; registers the new instance in the instance table (SPEC.md 29). There is
; NO eviction: any number of package instances can be resident, and a load
; that cannot fit fails with "Out of memory" while running instances stay
; untouched.
;
; The pool allocator IS the instance table (SPEC.md 29.2 rule 7): a package
; record's [I_SPTR, I_SPTR+I_SIZE) is occupied iff I_STATE != 0, so the
; store that frees a record frees its region. Only the UI task allocates
; (loader_run runs on it exclusively), so allocation never races itself.
;
; loader_run is called only from the UI task (ui.inc step 3) with the gfx
; lock NOT held - it takes and releases the lock itself around wm_destroy /
; wm_show, and the package entry runs with the lock free (SPEC.md 20.2).
; ld_pending is written by files.inc and consumed by ui.inc; loader_run
; itself only receives the already-decoded directory index in AX.
;
; ld_run_body is structured as clusters with local error stubs (mov al /
; ret) because 8086 conditional jumps are short-range only.
; =============================================================================

; --- status codes stored in [ld_status] (SPEC.md 21) -------------------------
LD_OK       equ 0               ; loaded and shown
LD_EDISK    equ 1               ; disk_read failed
LD_EBAD     equ 2               ; not a valid package (entry or header)
LD_EBIG     equ 3               ; too large (file, image or image+bss)
LD_EABORT   equ 4               ; package entry returned CF=1
LD_ENOMEM   equ 5               ; instance table or pool exhausted

; --- os88fs directory entry field offsets (SPEC.md 19) ------------------------
LD_DE_TYPE  equ 16              ; word: 1 = application package
LD_DE_LBA   equ 18              ; word: start LBA of file data
LD_DE_SIZE  equ 20              ; word: size in bytes (image + reloc table)
LD_DE_SHIFT equ 5               ; entry stride 32 = 1 << 5

; --- .o88 v2 header field offsets (SPEC.md 20.2) ------------------------------
LD_H_MAGIC  equ 0               ; word: 0x384F ('O','8')
LD_H_VER    equ 2               ; byte: format version = 2
LD_H_FLAGS  equ 3               ; byte: bit 0 = embedded 16x16 icon
LD_H_LINK   equ 4               ; word: link base, must equal APP_LOAD_OFF
LD_H_ENTRY  equ 6               ; word: entry offset, image-relative
LD_H_IMG    equ 8               ; word: image size (reloc table excluded)
LD_H_BSS    equ 10              ; word: bytes to zero after the image
LD_H_RCNT   equ 12              ; word: relocation count
LD_HDR_SIZE equ 0x20

APP_POOL_END equ APP_LOAD_OFF + APP_MAX_SIZE    ; 0xF000, exclusive

; -----------------------------------------------------------------------------
; loader_init - reset loader state
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
loader_init:
    mov word [ld_pending], 0
    mov byte [ld_status], LD_OK
    ret

; -----------------------------------------------------------------------------
; ld_alloc - claim a pool region, first-fit lowest base (SPEC.md 21)
; in:  AX = bytes needed (a 512-multiple)
; out: CF = 0, BX = region base; CF = 1 no hole fits
; clobbers: nothing else. UI task only (occupancy is derived from the
;           instance table, which only this task grows).
;
; start = APP_LOAD_OFF; while any in-use package record overlaps
; [start, start+AX), start = that record's end, rescan from the top (each
; bump lands on a record end, so the loop terminates); fail when
; start + AX > APP_POOL_END.
; -----------------------------------------------------------------------------
ld_alloc:
    push cx
    push dx
    push si
    push di
    mov bx, APP_LOAD_OFF        ; BX = candidate base
.retry:
    mov dx, bx
    add dx, ax                  ; DX = candidate end
    jc .full                    ; wrapped past 0xFFFF
    cmp dx, APP_POOL_END
    ja .full
    mov di, inst_tab            ; overlap scan over in-use package records
    mov cx, INST_MAX
.scan:
    cmp byte [di+I_STATE], 0
    je .next
    test byte [di+I_KIND], 0x80 ; packages only (builtins have I_SIZE 0)
    jz .next
    mov si, [di+I_SPTR]
    cmp si, dx                  ; record starts at/past our end: no overlap
    jae .next
    add si, [di+I_SIZE]         ; SI = record end (<= 0xF000: no wrap)
    cmp si, bx                  ; record ends at/before our start: ditto
    jbe .next
    mov bx, si                  ; overlap: restart just past this record
    jmp .retry
.next:
    add di, I_RECSZ
    loop .scan
    clc
    jmp .out
.full:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; ld_check_hdr - validate a .o88 v2 header (module-internal)
; in:  SI -> 32 readable header bytes; [ld_fsz] = the file's byte count
; out: CF = 0 and [ld_img]/[ld_bss]/[ld_ent]/[ld_rcnt] filled, or
;      CF = 1 and AL = LD_EBAD / LD_EBIG
; clobbers: AX, DX
; -----------------------------------------------------------------------------
ld_check_hdr:
    cmp word [si+LD_H_MAGIC], 0x384F        ; 'O','8'
    jne .bad
    cmp byte [si+LD_H_VER], 2               ; v1 files: "Bad package"
    jne .bad
    cmp word [si+LD_H_LINK], APP_LOAD_OFF
    jne .bad
    mov ax, [si+LD_H_IMG]
    cmp ax, LD_HDR_SIZE
    jb .bad                                 ; image smaller than the header
    cmp ax, APP_MAX_SIZE
    ja .toobig
    mov [ld_img], ax
    mov dx, [si+LD_H_BSS]
    cmp dx, APP_MAX_SIZE
    ja .toobig
    mov [ld_bss], dx
    add dx, ax                              ; img+bss <= 0xA000: no wrap
    cmp dx, APP_MAX_SIZE
    ja .toobig
    mov ax, [si+LD_H_ENTRY]                 ; image-relative entry
    cmp ax, LD_HDR_SIZE
    jb .bad
    cmp ax, [ld_img]
    jae .bad
    mov [ld_ent], ax
    mov ax, [si+LD_H_RCNT]
    cmp ax, APP_MAX_SIZE / 2                ; bound before the shift below
    ja .bad
    mov [ld_rcnt], ax
    shl ax, 1                               ; img + 2*count must equal the
    add ax, [ld_img]                        ; file size (truncation guard)
    cmp ax, [ld_fsz]
    jne .bad
    clc
    ret
.bad:
    mov al, LD_EBAD
    stc
    ret
.toobig:
    mov al, LD_EBIG
    stc
    ret

; -----------------------------------------------------------------------------
; loader_run - load, relocate and launch the package at a directory index
; in:  AX = directory index (0-based)
; out: nothing (all registers preserved); result in [ld_status]; on success
;      a new package instance is live in the instance table (SPEC.md 29)
; -----------------------------------------------------------------------------
loader_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    call ld_run_body            ; out: AL = LD_* status
    mov [ld_status], al
    call files_refresh          ; no lock held; it takes its own

    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ld_run_body - the load pipeline (module-internal; loader_run saves regs)
; in:  AX = directory index
; out: AL = LD_* status
;
; Steps (SPEC.md 21): validate the dir entry; peek the header via a
; 1-sector read into dsk_secbuf; size the allocation
; (roundup512(max(img+bss, fsize)) - sector granularity means the full-file
; read can never touch a neighbouring region); reserve an instance record +
; pool region; read the whole file; re-check the header (disk-swap guard);
; apply the reloc table; zero the bss (killing the table); call the entry;
; register + publish the instance and wm_show its window. Every failure
; path leaves running instances untouched - the reserved record was never
; published, so it (and with it the region) is simply still free.
; -----------------------------------------------------------------------------
ld_run_body:
    ; --- 1. validate the directory entry --------------------------------------
    cmp ax, [disk_nfiles]
    jae .bad1                   ; index out of range
    mov si, ax
    mov cl, LD_DE_SHIFT
    shl si, cl                  ; index * 32 (index <= 31: no overflow)
    add si, disk_dir            ; SI -> directory entry
    cmp word [si+LD_DE_TYPE], 1
    jne .bad1                   ; not an application package
    mov ax, [si+LD_DE_SIZE]
    or ax, ax
    jz .bad1                    ; empty file
    cmp ax, APP_MAX_SIZE
    ja .big1
    mov [ld_fsz], ax
    mov ax, [si+LD_DE_LBA]
    mov [ld_lba], ax
    jmp short .peek
.bad1:
    mov al, LD_EBAD
    ret
.big1:
    mov al, LD_EBIG
    ret

    ; --- 2. peek the header: first sector into dsk_secbuf ---------------------
    ; dsk_secbuf is disk.inc's mount scratch - safe to reuse here because
    ; both disk_mount and loader_run run only on the UI task.
.peek:
    push ds
    pop es
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read              ; in: AX=LBA, CX=1, ES:BX
    jc .disk1

    ; --- 3. validate the peeked header -----------------------------------------
    mov si, dsk_secbuf
    call ld_check_hdr           ; fills ld_img/ld_bss/ld_ent/ld_rcnt;
    jc .ret1                    ; AL = LD_EBAD / LD_EBIG on failure

    ; --- 4. size the allocation -------------------------------------------------
    mov ax, [ld_img]            ; need = roundup512(max(img+bss, fsize))
    add ax, [ld_bss]
    cmp ax, [ld_fsz]
    jae .sized
    mov ax, [ld_fsz]
.sized:
    add ax, 511                 ; <= 0x5000 + 511: no 16-bit overflow
    mov cl, 9
    shr ax, cl
    shl ax, cl
    cmp ax, APP_MAX_SIZE
    ja .big2
    mov [ld_need], ax

    ; --- 5. reserve an instance record + a pool region -------------------------
    call inst_alloc             ; DI = record (unpublished: still free on
    jc .nomem                   ; any later failure)
    mov bp, di                  ; BP = the record for the duration
    call ld_alloc               ; in: AX = need; out: BX = region base
    jc .nomem
    mov [ld_base], bx
    jmp short .read
.disk1:
    mov al, LD_EDISK
.ret1:
    ret
.big2:
    mov al, LD_EBIG
    ret
.nomem:
    mov al, LD_ENOMEM
    ret

    ; --- 6. read the whole file into the region --------------------------------
.read:
    mov ax, [ld_fsz]
    add ax, 511
    mov cl, 9
    shr ax, cl
    mov cx, ax                  ; CX = ceil(fsize/512) sectors
    mov ax, [ld_lba]
    mov bx, KERNEL_SEG          ; ES stays KERNEL_SEG through step 9 (the
    mov es, bx                  ; caller restores the real ES afterwards)
    mov bx, [ld_base]
    call disk_read              ; in: AX=LBA, CX=count, ES:BX -> dest
    jc .disk2

    mov si, [ld_base]           ; re-check against the in-region header -
    call ld_check_hdr           ; the disk could have been swapped between
    jc .ret2                    ; the peek and the read

    ; --- 7. apply the relocation table ------------------------------------------
    ; Table at base+img: [ld_rcnt] words. Low 15 bits = image offset of the
    ; word to patch (validated into [0x20, img-2]); bit 15 = class: 0 = an
    ; embedded package address (+= delta), 1 = a kernel-bound rel16
    ; displacement (-= delta) - SPEC.md 20.2.
    mov si, [ld_base]
    add si, [ld_img]            ; SI -> reloc table
    mov cx, [ld_rcnt]
    mov dx, [ld_base]
    sub dx, APP_LOAD_OFF        ; DX = delta (0 for a base-of-pool load)
    jcxz .reloc_done
    cld                         ; DF=0 for lodsw (SPEC.md 1)
.reloc:
    lodsw                       ; AX = table entry
    mov di, ax
    and di, 0x7FFF              ; DI = image offset
    cmp di, LD_HDR_SIZE
    jb .bad2                    ; a fixup may never touch the header
    mov bx, [ld_img]
    sub bx, 2
    cmp di, bx
    ja .bad2                    ; or run past the image end
    add di, [ld_base]           ; DI -> the word to patch
    test ax, 0x8000
    jnz .relsub
    add [di], dx                ; class 0: embedded package address
    jmp short .relnext
.relsub:
    sub [di], dx                ; class 1: rel16 displacement into the kernel
.relnext:
    loop .reloc
    jmp short .reloc_done
.disk2:
    mov al, LD_EDISK
.ret2:
    ret
.bad2:
    mov al, LD_EBAD
    ret

    ; --- 8. zero the package bss (overwrites the dead reloc table) -------------
.reloc_done:
    mov di, [ld_base]
    add di, [ld_img]
    mov cx, [ld_bss]
    xor al, al
    cld
    rep stosb                   ; ES=KERNEL_SEG; CX may be 0 (no-op)

    ; --- 9. launch ---------------------------------------------------------------
    ; DS = ES = KERNEL_SEG, IF=1, gfx lock not held (SPEC.md 20.2). The entry
    ; creates its window(s) lock-free and returns BX = window ptr / CF set.
    mov bx, [ld_base]
    add bx, [ld_ent]
    call bx
    jc .abort

    ; --- 10. register + publish the instance, show the window ------------------
    mov di, bp
    mov byte [di+I_KIND], KIND_PKG
    mov byte [di+I_TASK], 0xFF  ; packages cannot spawn (SPEC.md 20.2)
    mov ax, [ld_base]
    mov [di+I_SPTR], ax
    mov ax, [ld_need]
    mov [di+I_SIZE], ax         ; allocated bytes: pool occupancy + RAM sum
    mov si, [ld_base]
    add si, 16                  ; header name field (NUL-pad-safe copy)
    call inst_set_name
    xor ax, ax
    mov si, [ld_base]
    test byte [si+LD_H_FLAGS], 1
    jz .noicon
    mov ax, si
    add ax, LD_HDR_SIZE         ; embedded icon body rides in the region
.noicon:
    mov [di+I_ICON], ax
    call inst_bind_win          ; DI = record, BX = window (from the entry)
    mov byte [di+I_STATE], 1    ; publish - the LAST record write
    call gfx_lock
    call wm_show                ; in: BX = window ptr; repaints all
    call gfx_unlock
    mov al, LD_OK
    ret

.abort:
    ; SPEC.md 20.2: an aborting entry returns BX = its half-created window
    ; ptr (or 0). Destroy it, or aborted loads would leak wm_wins slots for
    ; good. BX comes from a package that just declared itself broken, so
    ; trust nothing: only a properly aligned pointer into wm_wins reaches
    ; wm_destroy (which writes through it). The reserved instance record
    ; was never published, so it and the region are simply still free.
    test bx, bx
    jz .abort_clean
    mov ax, bx
    sub ax, wm_wins
    jc .abort_clean             ; below the window table
    cmp ax, MAX_WIN*WIN_SIZE
    jae .abort_clean            ; beyond it
    mov cl, WIN_SIZE
    div cl                      ; AL = idx; AH = offset mod WIN_SIZE
    test ah, ah
    jnz .abort_clean            ; not record-aligned
    call gfx_lock
    call wm_destroy             ; in: BX; clears the record + zord slot
    call gfx_unlock
.abort_clean:
    mov al, LD_EABORT
    ret

; -----------------------------------------------------------------------------
section .bss

ld_pending: resw 1              ; 0 = none, else directory index + 1
                                ; (set by files.inc, consumed by ui.inc)
ld_status:  resb 1              ; LD_* result of the last loader_run
ld_lba:     resw 1              ; loader_run scratch: file start LBA
ld_fsz:     resw 1              ; file size, bytes (image + reloc table)
ld_img:     resw 1              ; header: image size
ld_bss:     resw 1              ; header: bss size
ld_ent:     resw 1              ; header: image-relative entry
ld_rcnt:    resw 1              ; header: relocation count
ld_base:    resw 1              ; allocated region base
ld_need:    resw 1              ; allocated region size (512-multiple)

section .text
