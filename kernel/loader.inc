; =============================================================================
; loader.inc - package loader (SPEC.md 21)
;
; Validates a jopfs directory entry, evicts the resident program, reads the
; .jop image into the APP region (KERNEL_SEG:APP_LOAD_OFF), validates its
; header (SPEC.md 20.2), zeroes the package bss and near-calls its entry.
;
; loader_run is called only from the UI task (ui.inc step 3) with the gfx
; lock NOT held - it takes and releases the lock itself around wm_destroy /
; wm_show, and the package entry runs with the lock free (SPEC.md 20.2).
; ld_pending is written by files.inc and consumed by ui.inc; loader_run
; itself only receives the already-decoded directory index in AX.
; =============================================================================

; --- status codes stored in [ld_status] (SPEC.md 21) -------------------------
LD_OK       equ 0               ; loaded and shown
LD_EDISK    equ 1               ; disk_read failed
LD_EBAD     equ 2               ; not a valid package (entry or header)
LD_EBIG     equ 3               ; too large (file, image or image+bss)
LD_EABORT   equ 4               ; package entry returned CF=1

; --- jopfs directory entry field offsets (SPEC.md 19) ------------------------
LD_DE_TYPE  equ 16              ; word: 1 = application package
LD_DE_LBA   equ 18              ; word: start LBA of file data
LD_DE_SIZE  equ 20              ; word: size in bytes
LD_DE_SHIFT equ 5               ; entry stride 32 = 1 << 5

; --- .jop header field offsets, relative to APP_LOAD_OFF (SPEC.md 20.2) ------
LD_H_MAGIC  equ 0               ; word: 0x504A ('J','P')
LD_H_VER    equ 2               ; byte: format version = 1
LD_H_LOAD   equ 4               ; word: load offset, must equal APP_LOAD_OFF
LD_H_ENTRY  equ 6               ; word: entry offset (absolute)
LD_H_IMG    equ 8               ; word: image size, header included
LD_H_BSS    equ 10              ; word: bytes to zero after the image
LD_HDR_SIZE equ 0x20            ; entry must land at or past the header end

; -----------------------------------------------------------------------------
; loader_init - reset loader state
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
loader_init:
    mov word [ld_pending], 0
    mov word [ld_appwin], 0
    mov byte [ld_status], LD_OK
    ret

; -----------------------------------------------------------------------------
; loader_run - load, validate and launch the package at a directory index
; in:  AX = directory index (0-based)
; out: nothing (all registers preserved); result in [ld_status], resident
;      window ptr in [ld_appwin]
;
; Steps (SPEC.md 21):
;   1. validate the directory entry (index, type, size) BEFORE touching the
;      resident program - a bad entry must not cost the user their app
;   2. if a program is resident, gfx_lock + wm_destroy + gfx_unlock it
;   3. disk_read ceil(size/512) sectors to KERNEL_SEG:APP_LOAD_OFF
;   4. validate the header (magic/version/load/sizes/entry range)
;   5. zero the package bss after the image
;   6. near-call the entry (DS=ES=KERNEL_SEG, lock free); on success store
;      BX in ld_appwin and wm_show it under the lock
;   7. store the status and call files_refresh (no lock held)
; -----------------------------------------------------------------------------
loader_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- step 1: validate the directory entry --------------------------------
    cmp ax, [disk_nfiles]
    jae .bad                    ; index out of range
    mov si, ax
    mov cl, LD_DE_SHIFT
    shl si, cl                  ; index * 32 (index <= 31: no overflow)
    add si, disk_dir            ; SI -> directory entry
    cmp word [si+LD_DE_TYPE], 1
    jne .bad                    ; not an application package
    mov di, [si+LD_DE_SIZE]     ; DI = file size in bytes
    or di, di
    jz .bad                     ; empty file
    cmp di, APP_MAX_SIZE
    ja .toobig
    mov dx, [si+LD_DE_LBA]      ; DX = start LBA

    ; --- step 2: evict the resident program ----------------------------------
    ; Must happen before the read clobbers the region its procs live in.
    mov bx, [ld_appwin]
    or bx, bx
    jz .no_evict
    call gfx_lock
    call wm_destroy             ; in: BX = window ptr; repaints all
    call gfx_unlock
    mov word [ld_appwin], 0
.no_evict:

    ; --- step 3: read the file into the APP region ---------------------------
    mov ax, di
    add ax, 511                 ; DI <= 0x5000: no 16-bit overflow
    mov cl, 9
    shr ax, cl
    mov cx, ax                  ; CX = ceil(size / 512) sectors
    mov ax, dx                  ; AX = start LBA
    mov bx, KERNEL_SEG          ; ES stays KERNEL_SEG through steps 5-6;
    mov es, bx                  ; the caller's ES is restored at .done
    mov bx, APP_LOAD_OFF
    call disk_read              ; in: AX=LBA, CX=count, ES:BX -> dest
    jc .diskerr

    ; --- step 4: validate the header ------------------------------------------
    cmp word [APP_LOAD_OFF+LD_H_MAGIC], 0x504A      ; 'J','P'
    jne .bad
    cmp byte [APP_LOAD_OFF+LD_H_VER], 1
    jne .bad
    cmp word [APP_LOAD_OFF+LD_H_LOAD], APP_LOAD_OFF
    jne .bad
    mov di, [APP_LOAD_OFF+LD_H_IMG]                 ; DI = image size
    cmp di, APP_MAX_SIZE
    ja .toobig
    mov cx, [APP_LOAD_OFF+LD_H_BSS]                 ; CX = bss size
    cmp cx, APP_MAX_SIZE
    ja .toobig
    mov ax, di                  ; both <= 0x5000, so the sum <= 0xA000:
    add ax, cx                  ; safe from 16-bit overflow
    cmp ax, APP_MAX_SIZE
    ja .toobig
    mov ax, [APP_LOAD_OFF+LD_H_ENTRY]
    cmp ax, APP_LOAD_OFF+LD_HDR_SIZE
    jb .bad                     ; entry inside the header
    mov dx, APP_LOAD_OFF
    add dx, di                  ; DX = end of image (<= 0xF000, no overflow)
    cmp ax, dx
    jae .bad                    ; entry past the image

    ; --- step 5: zero the package bss -----------------------------------------
    mov bx, ax                  ; BX = entry offset (stosb clobbers AL)
    mov di, dx                  ; DI = APP_LOAD_OFF + image size
    xor al, al
    cld
    rep stosb                   ; ES=KERNEL_SEG; CX may be 0 (no-op)

    ; --- step 6: launch --------------------------------------------------------
    ; DS = ES = KERNEL_SEG, IF=1, gfx lock not held (SPEC.md 20.2). The entry
    ; creates its window(s) lock-free and returns BX = window ptr / CF set.
    call bx
    jc .abort
    mov [ld_appwin], bx
    call gfx_lock
    call wm_show                ; in: BX = window ptr; repaints all
    call gfx_unlock
    mov al, LD_OK
    jmp short .finish

.diskerr:
    mov al, LD_EDISK
    jmp short .finish
.bad:
    mov al, LD_EBAD
    jmp short .finish
.toobig:
    mov al, LD_EBIG
    jmp short .finish
.abort:
    ; SPEC.md 20.2: an aborting entry returns BX = its half-created window
    ; ptr (or 0). Destroy it, or three aborted loads would leak every free
    ; wm_wins slot for good. BX comes from a package that just declared
    ; itself broken, so trust nothing: only a properly aligned pointer
    ; into wm_wins reaches wm_destroy (which writes through it).
    test bx, bx
    jz .abort_clean
    mov ax, bx
    sub ax, wm_wins
    jc .abort_clean             ; below the window table
    cmp ax, MAX_WIN*WIN_SIZE
    jae .abort_clean            ; beyond it
    mov cl, WIN_SIZE
    div cl                      ; AX <= 143, so AH = offset mod WIN_SIZE
    test ah, ah
    jnz .abort_clean            ; not record-aligned
    call gfx_lock
    call wm_destroy             ; in: BX; clears the record + zord slot
    call gfx_unlock
.abort_clean:
    mov al, LD_EABORT

.finish:
    ; --- step 7: publish the status, refresh the Disk window -----------------
    mov [ld_status], al
    call files_refresh          ; no lock held; it takes its own

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

ld_pending: resw 1              ; 0 = none, else directory index + 1
                                ; (set by files.inc, consumed by ui.inc)
ld_appwin:  resw 1              ; window ptr of the resident program, 0 = none
ld_status:  resb 1              ; LD_* result of the last loader_run

section .text
