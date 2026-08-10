; =============================================================================
; loader.inc - package loader (SPEC.md 21)
;
; Validates a staged directory entry (SPEC.md 19), allocates a region from the package
; pool (0xB000..0xFDFF), reads the .o88 v2 file into it, applies the
; relocation table (SPEC.md 20.2: class 0 entries += base-APP_LOAD_OFF,
; class 1 entries -= it), zeroes the package bss, near-calls its entry, and
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
;
; **This module is `.cold` (SPEC.md 2.6).** Its code has a CS of its own, so
; a routine the kernel reaches from outside is a six-byte resident thunk in
; kernel.asm under the PUBLIC name, and the body here carries an `_x` suffix;
; calls that leave the cold set go out through `KERNEL_SEG:cw_*`. Two things
; are on the author rather than the build: CS is NOT KERNEL_SEG here, so
; nothing may take a kernel segment from it, and DATA stays in `.text` - the
; section toggles below are that, and a `db`/`dw` added on the wrong side of
; one is read at the wrong segment with no diagnostic at all.
; =============================================================================

; --- status codes stored in [ld_status] (SPEC.md 21) -------------------------
LD_OK       equ 0               ; loaded and shown
LD_EDISK    equ 1               ; disk_read failed
LD_EBAD     equ 2               ; not a valid package (entry or header)
LD_EBIG     equ 3               ; too large (file, image or image+bss)
LD_EABORT   equ 4               ; package entry returned CF=1
LD_ENOMEM   equ 5               ; instance table or pool exhausted

; --- staged directory entry field offsets (SPEC.md 19) ------------------------
LD_DE_TYPE  equ 16              ; word: 1 = application package
LD_DE_CLUS  equ 18              ; word: first cluster of the file's chain
LD_DE_SIZE  equ 20              ; word: size in bytes

; --- .o88 v3 header field offsets (SPEC.md 20.2) ------------------------------
LD_H_MAGIC  equ 0               ; word: 0x384F ('O','8')
LD_H_VER    equ 2               ; byte: format version = 3
LD_H_FLAGS  equ 3               ; byte: bit 0 = embedded 16x16 icon
LD_H_LINK   equ 4               ; word: link base, must be 0 (org 0 - a v3
                                ; package is position-independent because it
                                ; owns its segment, SPEC.md 20.1)
LD_H_ENTRY  equ 6               ; word: entry offset
LD_H_IMG    equ 8               ; word: image size
LD_H_BSS    equ 10              ; word: bytes to zero after the image
LD_H_DISP   equ PKG_DISP        ; 12: the three-byte dispatcher, IN the
                                ; header - `call bp / retf`. Every kernel to
                                ; package call goes through it (SPEC.md 20.1)
LD_HDR_SIZE equ 0x20

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here

; -----------------------------------------------------------------------------
; loader_init - reset loader state
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
loader_init_x:
    mov word [ld_base], 0
    mov word [ld_pending], 0
    mov word [ld_pwin], 0
    mov byte [ld_status], LD_OK
    ret

; -----------------------------------------------------------------------------
; ld_slot - the instance slot a reserved record sits in, which is the owner
;           word its REGION claim carries (SPEC.md 50.2)
; in:  DI = instance record ptr
; out: BX = slot index 0..INST_MAX-1
; clobbers: nothing else (flags)
;
; The region is owned by the SLOT and a package's data claims by its SEGMENT,
; deliberately: mem_free_rec already releases both, so teardown needs no new
; code, and the Task Manager's HEAP column - which sums a package's claims by
; segment - does not then count the region twice against the SIZE column that
; already reports it.
; -----------------------------------------------------------------------------
ld_slot:
    push ax
    push cx
    mov ax, di
    sub ax, inst_tab
    mov cl, 5                   ; I_RECSZ = 32
    shr ax, cl
    mov bx, ax
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ld_unreserve - give back a region, and anything the entry proc claimed
; in:  DI = the reserved (unpublished) instance record
; out: nothing (all registers preserved)
;
; The record is not published, so mem_free_rec cannot be used: it reads
; I_SPTR, which step 9 has not written yet. Both owner words are known here
; anyway - the slot for the region, [ld_base] for whatever the entry proc
; claimed before it failed.
; -----------------------------------------------------------------------------
ld_unreserve:
    push bx
    call ld_slot
    call KERNEL_SEG:cw_mem_free_owner
    mov bx, [ld_base]
    or bx, bx
    jz .out
    call KERNEL_SEG:cw_mem_free_owner
    mov word [ld_base], 0
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ld_check_hdr - validate a .o88 v3 header (module-internal)
; in:  SI -> 32 readable header bytes (kernel segment); [ld_fsz] = the file's
;      byte count
; out: CF = 0 and [ld_img]/[ld_bss]/[ld_ent] filled, or CF = 1 and
;      AL = LD_EBAD / LD_EBIG
; clobbers: AX, DX
;
; v3 (SPEC.md 20.2) has no relocation table: a package is linked at org 0 and
; loaded at a paragraph boundary in its own segment, so the file IS the
; image. The three dispatcher bytes are checked here too - they are how the
; kernel calls in, and a package whose header does not carry them would jump
; the kernel into its data on the first paint.
; -----------------------------------------------------------------------------
ld_check_hdr:
    cmp word [si+LD_H_MAGIC], 0x384F        ; 'O','8'
    jne .bad
    cmp byte [si+LD_H_VER], 3               ; v1/v2 files: "Bad package"
    jne .bad
    cmp word [si+LD_H_LINK], 0              ; org 0 or it is not v3
    jne .bad
    cmp word [si+LD_H_DISP], 0D5FFh         ; call bp
    jne .bad
    cmp byte [si+LD_H_DISP+2], 0CBh         ; retf
    jne .bad
    mov ax, [si+LD_H_IMG]
    cmp ax, LD_HDR_SIZE
    jb .bad                                 ; image smaller than the header
    cmp ax, APP_MAX_SIZE
    ja .toobig
    cmp ax, [ld_fsz]                        ; the image IS the file now
    jne .bad
    mov [ld_img], ax
    mov dx, [si+LD_H_BSS]
    cmp dx, APP_MAX_SIZE
    ja .toobig
    mov [ld_bss], dx
    add dx, ax                              ; img+bss <= 0x1E000: no wrap
    cmp dx, APP_MAX_SIZE
    ja .toobig
    mov ax, [si+LD_H_ENTRY]
    cmp ax, LD_HDR_SIZE
    jb .bad
    cmp ax, [ld_img]
    jae .bad
    mov [ld_ent], ax
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
; ld_icon - record where this package's embedded icon is (SPEC.md 20.2/25)
; in:  DI = the instance record, [ld_base] = the loaded region
; out: nothing (all registers preserved)
;
; A SENTINEL, not a copy. The icon body is 64 bytes at a fixed offset in the
; package's own header, and the region holding it lives exactly as long as
; the instance does - so a per-slot copy in kernel .bss was 768 bytes spent
; duplicating something that could not go away first. inst_icon_ptr stages
; one body back into the kernel segment when a tile is actually drawn, which
; is the dsk_get_icon idiom and costs nothing on any path that does not draw.
; -----------------------------------------------------------------------------
ld_icon:
    push ax
    push es
    mov word [di+I_ICON], 0
    mov es, [ld_base]
    test byte [es:LD_H_FLAGS], 1
    jz .out
    mov word [di+I_ICON], I_ICO_PKG ; the SENTINEL, and no copy: the body is
                                ; already in the region this instance owns,
                                ; at the fixed offset every package header
                                ; puts it at, and it lives exactly as long
                                ; (SPEC.md 25). inst_icon_ptr stages it when
                                ; something actually draws a tile
.out:
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; loader_run - load, relocate and launch the package at a directory index
; in:  AX = directory index (0-based), into the listing of the file-manager
;      window named by [ld_pwin] (SPEC.md 22.1) - which may not be the one
;      currently mounted
; out: nothing (all registers preserved); result in [ld_status]; on success
;      a new package instance is live in the instance table (SPEC.md 29)
;
; The sync comes first and the index is banked across it, in that order and
; for a hard reason: disk_mount deliberately zeroes [ld_pending] (SPEC.md
; 18.3), so anything that reads the pending word AFTER a sync reads zero. The
; index arrives here in AX, already consumed by ui.inc, which is what makes
; the order safe - and [ld_pwin] is read before the sync too, so this stays
; true if the drain ever changes.
; -----------------------------------------------------------------------------
loader_run_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov di, [ld_pwin]           ; the window whose listing this index means
    or di, di
    jz .body                    ; no poster (a package's own request): the
                                ; globals are the only listing there is
    push ax
    call fmv_sync_x             ; ...make the globals BE that listing
    pop ax
.body:
    mov byte [ld_painted], 0
    call ld_run_body_x          ; out: AL = LD_* status
    mov [ld_status], al
    push bx                     ; ...and SAY it (SPEC.md 60). It used to be
    push cx                     ; stamped on the window that asked, because
    mov bx, fm_stattab          ; one global put 'Bad package' on every open
    mov cx, LD_NSTAT            ; Disk window at once - a question a toast
    call KERNEL_SEG:cw_toast_say ; cannot raise, since it belongs to no
    pop cx                      ; window at all and appears at the moment of
    pop bx                      ; the act (SPEC.md 22.9)
    cmp byte [ld_painted], 0
    jne .painted                ; the success path published the status and
                                ; drew the new window itself; all that is
                                ; left is the poster's own status line, which
                                ; still reads "Loading..."
    cmp al, LD_EABORT           ; ...and a FAILED load drew nothing AT ALL, so
    jne .painted                ; it owes exactly the same one line - 'Bad
                                ; package' where 'Loading...' is. This was an
                                ; unconditional files_refresh, which is
                                ; wm_paint_all: the desktop dither, the drive
                                ; zones, the chrome and every window on the
                                ; screen, to report four words. The one
                                ; exception is a package the loader REFUSED
                                ; after running its entry proc, which may have
                                ; put pixels anywhere before it said no
    call files_refresh_x
    jmp short .done
.painted:
    call files_poster_x
.done:

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
; -----------------------------------------------------------------------------
; ld_run_name - the load pipeline, resolved by NAME (SPEC.md 21.4)
; in:  SI -> a NUL-terminated 8.3 name, in the CURRENT directory
; out: AL = LD_* status
;
; ld_run_body's twin, and it exists so a caller does not need a LISTING. An
; index is only meaningful against disk_dir, and making disk_dir be a given
; folder is a FULL mount - the directory scan, the sort and one icon-harvest
; read per file (SPEC.md 18.9) - where dskw_stat walks the directory sectors
; and answers the same three questions step 1 asks. Every caller that had to
; mount only to turn a name it already had into an index can stop.
;
; It is also the SAFER resolution, which is worth saying because it looks
; like a pure optimisation. An index is resolved against a snapshot that can
; have shifted underneath it - a name sorting into place moves everything
; after it (SPEC.md 19.4) - and that is docs/FIELD-NOTES.md 4, "Bad package"
; about a file nobody clicked. A name cannot be shifted into somebody else's
; entry: it either exists in this directory now or it does not.
; -----------------------------------------------------------------------------
ld_run_name_x:
    call ld_res_name
    jc .fail
    mov ax, [ld_clus]           ; ...AND IN AX. Step 1 ends on the cluster it
                                ; just stored, so .peek's first instruction
                                ; takes it from the register - an undocumented
                                ; contract at a jump target, which is exactly
                                ; the kind of difference that assembles
                                ; cleanly and runs wrong: without this, AL was
                                ; still dskw_stat's ATTRIBUTE byte and
                                ; dsk_clus2lba was handed 0x20 as a cluster
    jmp ld_run_body_x.peek      ; joins the pipeline exactly where the index
.fail:                          ; resolver leaves it - [ld_fsz] and [ld_clus]
    ret                         ; are the rest of what step 1 produces

; -----------------------------------------------------------------------------
; ld_res_name - fill [ld_fsz]/[ld_clus] from a name (module internal)
; in:  SI -> a NUL-terminated 8.3 name
; out: CF=0; CF=1 with AL = LD_EBAD / LD_EBIG
; -----------------------------------------------------------------------------
ld_res_name:
    call dskw_stat_x            ; AL = raw attribute, BX = first cluster,
    jc .bad                     ; DX:CX = size. A near call - loader.inc and
                                ; diskw.inc are both .cold (SPEC.md 2.6)
    test al, 0x1E               ; hidden | system | volume | directory: the
    jnz .bad                    ; species disk_mount's own filter drops, so
                                ; this and "type 1" agree on what may be
                                ; loaded. READ-ONLY is deliberately allowed -
                                ; TASKMGR.O88 is read-only and is an app
                                ; (SPEC.md 19.6)
    or dx, dx
    jnz .big                    ; over 64KB before the cap is even consulted
    or cx, cx
    jz .bad                     ; empty file
    cmp cx, APP_MAX_SIZE
    ja .big
    mov [ld_fsz], cx
    cmp bx, 2                   ; the same range step 1 re-checks, and for the
    jb .bad                     ; same reason: it stands alone if the mount
    cmp bx, [dsk_maxclus]       ; code ever changes (SPEC.md 21 step 1)
    ja .bad
    mov [ld_clus], bx
    clc
    ret
.bad:
    mov al, LD_EBAD
    stc
    ret
.big:
    mov al, LD_EBIG
    stc
    ret

ld_run_body_x:
    ; --- 1. validate the directory entry --------------------------------------
    cmp ax, [disk_nfiles]
    jae .bad1                   ; index out of range
    call KERNEL_SEG:cw_dsk_get_dir ; SI -> the entry, staged out of LOW_SEG into
                                ; the kernel segment (SPEC.md 2.1)
    cmp word [si+LD_DE_TYPE], 1
    jne .bad1                   ; not an application package
    mov ax, [si+LD_DE_SIZE]
    or ax, ax
    jz .bad1                    ; empty file
    cmp ax, APP_MAX_SIZE
    ja .big1
    mov [ld_fsz], ax
    mov ax, [si+LD_DE_CLUS]     ; first cluster: re-check the range the
    cmp ax, 2                   ; mount already validated (SPEC.md 21 step
    jb .bad1                    ; 1 - this check stands alone if the mount
    cmp ax, [dsk_maxclus]       ; code ever changes)
    ja .bad1
    mov [ld_clus], ax
    jmp short .peek
.bad1:
    mov al, LD_EBAD
    ret
.big1:
    mov al, LD_EBIG
    ret

    ; --- 2. peek the header: first sector into dsk_secbuf ---------------------
    ; dsk_secbuf is disk.inc's mount scratch - safe to reuse here because
    ; both disk_mount and loader_run run only on the UI task. It lives in
    ; LOW_SEG, so the sector lands there and only the 32 header bytes are
    ; staged back into the kernel segment for ld_check_hdr to read.
.peek:
    call KERNEL_SEG:cw_dsk_clus2lba ; AX: cluster -> its first LBA; the first
    jc .bad1                    ; sector of the first cluster IS the file's
                                ; first 512 bytes, so peek semantics are
                                ; exact (SPEC.md 21 step 2)
    mov bx, LOW_SEG
    mov es, bx
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_read              ; in: AX=LBA, CX=1, ES:BX
    jc .disk1

    ; --- 3. validate the peeked header -----------------------------------------
    mov si, dsk_secbuf
    mov di, ld_hdr
    mov cx, LD_HDR_SIZE
    call KERNEL_SEG:cw_dsk_copy_in ; LOW_SEG:dsk_secbuf -> KERNEL_SEG:ld_hdr
    mov si, ld_hdr
    call ld_check_hdr           ; fills ld_img/ld_bss/ld_ent;
    jc .ret1                    ; AL = LD_EBAD / LD_EBIG on failure

    ; --- 4. size the allocation -------------------------------------------------
    mov ax, [ld_img]            ; need = roundup512(img + bss)
    add ax, [ld_bss]
    add ax, 511                 ; <= 0xF000 + 511: no 16-bit overflow
    mov cl, 9
    shr ax, cl
    shl ax, cl
    cmp ax, APP_MAX_SIZE
    ja .big2
    mov [ld_need], ax

    ; --- 5. reserve an instance record + a pool region -------------------------
    call KERNEL_SEG:cw_inst_alloc ; DI = record (unpublished: still free on
    jc .nomem                   ; any later failure)
    mov [ld_rec], di            ; banked in .bss, not in BP: BP carries the
                                ; entry offset into the dispatcher below,
                                ; and loader_run is UI-task-only and one
                                ; load at a time (SPEC.md 21)
    add ax, 1023                ; the region is a heap claim now (SPEC.md
    mov cl, 10                  ; 20.1/50.3), so it rounds to whole KB rather
    shr ax, cl                  ; than to the 512 bytes int 13h needs - which
                                ; mem_claim's own granularity then guarantees
    call ld_slot                ; BX = the owner word: this record's slot
    call KERNEL_SEG:cw_mem_claim_hi ; from the TOP down, away from the data claims
    jc .nomem                   ; growing up from the bottom (SPEC.md 50.3)
    mov [ld_base], dx
    mov cl, 10                  ; I_SIZE is bytes, and it is what the region
    shl ax, cl                  ; occupies now, not what the file needed
    mov [ld_need], ax
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
    mov dx, ax                  ; DX = ceil(fsize/512) sectors
    mov ax, [ld_clus]
    mov es, [ld_base]           ; the package's OWN segment, offset 0
    xor bx, bx
    call KERNEL_SEG:cw_dsk_read_chain ; in: AX=cluster, DX=sectors, ES:BX -> dest.
    jc .ret2                    ; on CF, AL is ALREADY the status: 1 =
                                ; LD_EDISK (disk error), 2 = LD_EBAD (chain
                                ; corrupt is bad data, not a disk error) -
                                ; SPEC.md 21 step 6

    mov si, ld_hdr              ; re-check against the in-region header - the
    mov di, si                  ; disk could have been swapped between the
    push ds                     ; peek and the read. It lives in the package
    push ds                     ; segment now, so it is staged like any other
    push es                     ; cross-segment read (SPEC.md 20.1)
    pop ds                      ; DS = the package's segment (the source)...
    pop es                      ; ...and ES = KERNEL_SEG, off the stack. NOT
                                ; out of CS: this module is .cold (SPEC.md
                                ; 2.6), so CS is COLD_SEG and the staged copy
                                ; would land in the middle of the cold code
    xor si, si
    push cx
    mov cx, LD_HDR_SIZE
    cld
    rep movsb
    pop cx
    pop ds
    push ds
    pop es                      ; ES = KERNEL again for the zero below
    mov si, ld_hdr
    call ld_check_hdr
    jc .ret2

    ; --- 7. zero the package bss ------------------------------------------------
    mov es, [ld_base]
    mov di, [ld_img]
    mov cx, [ld_bss]
    xor al, al
    cld
    rep stosb                   ; CX may be 0 (no-op)
    push ds
    pop es                      ; ES = KERNEL again (SPEC.md 1 rule 3)

    ; --- 8. launch ---------------------------------------------------------------
    ; The entry runs with CS = DS = its own segment, ES = KERNEL_SEG, IF = 1
    ; and the gfx lock NOT held (SPEC.md 20.2). It is reached the same way
    ; every other package callback is - through the dispatcher in its header
    ; (SPEC.md 20.1) - so an entry proc is an ordinary near proc ending in an
    ; ordinary `ret`, and returns BX = its window / CF set.
    mov ax, [ld_base]
    mov [ld_fp+2], ax           ; a module-local far pointer: loader_run is
    mov word [ld_fp], PKG_DISP  ; UI-task-only and one load at a time
    mov bp, [ld_ent]
    push ds
    push ds
    pop es                      ; ES = KERNEL_SEG, per the callback contract -
                                ; taken from DS while that still names the
                                ; kernel, because this module is .cold and CS
                                ; is COLD_SEG (SPEC.md 2.6)
    mov ds, ax                  ; DS = the package's own segment - so the far
    call far [es:ld_fp]         ; pointer is read through ES, the only
    pop ds                      ; register still naming KERNEL_SEG here
    jc .abort

    ; --- 9. register + publish the instance, show the window ---------------------
    jmp short .reg
.ret2:                          ; step 6 or 7 failed with a region already
    push ax                     ; claimed (AL is the status): give it back,
    mov di, [ld_rec]            ; or a failed load would hold heap for the
    call ld_unreserve           ; session. The record itself is unpublished,
    pop ax                      ; so it needs nothing
    ret

.reg:
    mov di, [ld_rec]
    mov byte [di+I_KIND], KIND_PKG
    mov byte [di+I_TASK], 0xFF  ; born task-less; a callback may claim one
                                ; worker later (SPEC.md 20.6). The entry
                                ; proc cannot: neither I_STATE nor
                                ; wm_owner is published until right here.
    mov ax, [ld_base]
    mov [di+I_SPTR], ax         ; the region's base SEGMENT (SPEC.md 20.1)
    mov ax, [ld_need]
    mov [di+I_SIZE], ax         ; allocated bytes: pool occupancy + RAM sum
    push es
    mov es, [ld_base]           ; the name field is in the package's image
    mov si, 16
    call KERNEL_SEG:cw_inst_set_name_x ; ES:SI -> the 16-byte NUL-padded name
    pop es
    call ld_icon                ; copy an embedded icon into kernel .bss
    call KERNEL_SEG:cw_inst_bind_win ; DI = record, BX = window (from the entry)
    mov byte [di+I_STATE], 1    ; publish - the LAST record write
    ; --- the status BEFORE the repaint, not after ---------------------------
    ; A Disk window's status line draws [ld_status] (SPEC.md 22), so the
    ; value has to be published before the pass that draws it. Setting it in
    ; loader_run afterwards is what forced a SECOND full-screen repaint on
    ; every successful launch, purely to correct one line of text.
    mov byte [ld_status], LD_OK
    mov byte [ld_painted], 1    ; ...and this pass IS the repaint
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_wm_show  ; in: BX = window ptr; repaints all
    call KERNEL_SEG:cw_gfx_unlock
    mov al, LD_OK
    ret

.abort:
    ; SPEC.md 20.2: an aborting entry may have created windows before it
    ; declared itself broken - the one it returns in BX, and any others.
    ; Destroy them ALL by the W_SEG creator stamp (wm_destroy_seg) rather
    ; than by trusting BX: a package that just declared itself broken gets
    ; nothing believed, and the sweep only ever touches records wm_create
    ; stamped itself. BEFORE ld_unreserve, so no window outlives the region
    ; its procs live in - a survivor's repaint would far-call a freed claim.
    ; The reserved instance record was never published, so it needs nothing.
    call KERNEL_SEG:cw_gfx_lock
    mov ax, [ld_base]
    call KERNEL_SEG:cw_wm_destroy_seg
    call KERNEL_SEG:cw_gfx_unlock
    mov di, [ld_rec]            ; the region, and anything the entry proc
    call ld_unreserve           ; claimed before it declared itself broken
    mov al, LD_EABORT
    ret

; -----------------------------------------------------------------------------
section .bss

ld_pending: resw 1              ; 0 = none, else directory index + 1
                                ; (set by files.inc, consumed by ui.inc)
ld_pwin:    resw 1              ; -> the state block of the file-manager
                                ; window that posted it, 0 = none. The index
                                ; above is meaningless anywhere else, and a
                                ; BACKGROUND window's listing is a real one it
                                ; is entitled to act on (SPEC.md 22.1)
ld_status:  resb 1              ; LD_* result of the last loader_run
ld_painted: resb 1              ; 1 = ld_run_body already did the full pass
                                ; (its wm_show), so loader_run must not
ld_clus:    resw 1              ; loader_run scratch: the file's first cluster
ld_fsz:     resw 1              ; file size, bytes (image + reloc table)
ld_img:     resw 1              ; header: image size
ld_bss:     resw 1              ; header: bss size
ld_ent:     resw 1              ; header: image-relative entry
ld_base:    resw 1              ; allocated region base
ld_need:    resw 1              ; allocated region size (512-multiple)
ld_hdr:     resb LD_HDR_SIZE    ; the peeked .o88 header, staged out of the
                                ; LOW_SEG sector buffer for ld_check_hdr
ld_rec:     resw 1              ; the reserved instance record
ld_fp:      resw 2              ; far pointer to the package's dispatcher

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
ldf_ld_run_body:        call ld_run_body_x
                    retf
ldf_ld_run_name:        call ld_run_name_x
                    retf
ldf_loader_init:        call loader_init_x
                    retf
ldf_loader_run:         call loader_run_x
                    retf

section .text
