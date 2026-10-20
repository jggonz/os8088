; =============================================================================
; os8088 - RAMDISK.DRV / rdfsv.inc
;
; The directory and the FSV_* verbs (SPEC.md 62.9.1). This is the harness's
; own code with one thing changed underneath it: a file is a CHAIN of extents
; rather than one, so every read and write goes through rd_io and every size
; change through rd_chain_fit. Not one verb's contract moved.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1). Every string
; instruction in this file therefore loads its own segments and puts them
; back; a bare `rep stosw` here writes into the kernel's own image.
;
; **AND THEN ES IS THE DIRECTORY** (SPEC.md 62.9.13.1). The rows are not in
; this image: they are the first RD_DIRB bytes of [rd_dtab]'s claim, which
; Mount takes and Unmount gives back, so a loaded driver with nothing mounted
; does not carry them. A ROW IS AN OFFSET INTO THAT CLAIM and every access is
; [es:...]. Each verb makes ES the directory exactly once - rd_row for a
; handle, rd_vlook for a name, the scan itself for FSV_LIST and FSV_ENUM -
; and a NAME verb first stages the kernel's string into rd_nbuf, because a
; compare needs both sides and the 8086 has one spare segment register. DS
; stays ours throughout, so not one variable access in this file moved.
;
; Holding the segment across a verb is safe because nothing a verb calls
; claims memory (SPEC.md 66.3 rule 2); drv_fs_call's drv_stamped puts ES back
; for the kernel on the way out.
; =============================================================================

; =============================================================================
; DIRECTORY HELPERS
; =============================================================================

; rd_dir_clear - every row free. A STORE MUST EXIST: [rd_dtab] is 0
; otherwise and this would write the interrupt vectors.
; out: ES = the directory, which every caller wants next
;
; ZEROED WHOLE FIRST: the directory is a heap claim now (SPEC.md 62.9.13.1),
; not the image's own zeroed rd_dir, and a claim is not zeroed - and
; rd_meta_build copies every row's 14 name bytes into a Preserve, so the
; heap's bytes after each name's NUL went into the .RAM file
rd_dir_clear:
    push ax
    push cx
    push di
    mov es, [rd_dtab]
    xor di, di
    xor ax, ax
    mov cx, RD_DIRB / 2
    cld
    rep stosw
    mov di, RDE_TYPE
    mov cx, RD_MAXENT
.blank:
    mov byte [es:di], RD_FREE
    add di, RDE_SZ
    loop .blank
    mov byte [rd_cwd], 0
    pop di
    pop cx
    pop ax
    ret

; rd_row - AL = a handle -> CF=0 and ES:SI = its row; CF=1 = no such handle
; A free row answers CF=1, so a handle held across a delete cannot resolve.
; ES = the directory on BOTH answers: this is where a handle verb gets it.
rd_row:
    push ax
    mov es, [rd_dtab]
    or al, al
    jz .no
    cmp al, RD_MAXENT
    ja .no
    dec al
    mov ah, RDE_SZ              ; AX = index * RDE_SZ: a byte multiply, the
    mul ah                      ; product being at most 2,280 - and that IS
    xchg ax, si                 ; the row's offset in the claim
    cmp byte [es:si+RDE_TYPE], RD_FREE
    je .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; rd_hand - SI = a row -> AX = its handle (index + 1, so AH = 0); CF = 0
rd_hand:
    push dx
    mov ax, si                  ; the row's offset, the directory being at 0
    mov dl, RDE_SZ              ; a byte divide: the offset is under 2,304,
    div dl                      ; so the index fits AL, and SI is a row's
    inc ax                      ; first byte, so the remainder in AH is 0
    pop dx
    clc                         ; CF = 0 for FSV_STAT, which answers with this
    ret

; rd_free_row - in ES = the directory
;               out CF=0 and DI = a free row; CF=1 = the directory is full
rd_free_row:
    push cx
    xor di, di
    mov cx, RD_MAXENT
.scan:
    cmp byte [es:di+RDE_TYPE], RD_FREE
    je .got
    add di, RDE_SZ
    loop .scan
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

; rd_nfiles - out AX = live rows, 0 with no store (and so no directory);
;             every other register preserved, ES included - RSV_STATE's ES is
;             the page's block
rd_nfiles:
    push bx
    push cx
    push es
    xor ax, ax
    cmp [rd_have], al
    je .out                     ; no store, no claim: nothing to read, and
                                ; reading [rd_dtab] = 0 would count the vectors
    mov es, [rd_dtab]
    mov bx, RDE_TYPE
    mov cx, RD_MAXENT
.scan:
    cmp byte [es:bx], RD_FREE
    je .next
    inc ax
.next:
    add bx, RDE_SZ
    loop .scan
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_settype - ES:SI = a row whose name is set -> its RDE_TYPE (internal)
;
; A PACKAGE IS DECIDED BY ITS EXTENSION, and a redirected volume has to decide
; it the same way disk_mount's own dsk_synth does. Without this a written or
; copied `.O88` lands as type 0 - a plain file - and the double-click goes to
; SPEC.md 54's association path instead of the loader. Nothing is associated
; with `.O88`, so it does NOTHING AND SAYS NOTHING: the file is listed, the
; right size, byte-perfect on the volume, and inert.
; -----------------------------------------------------------------------------
rd_settype:
    push ax
    push bx
    push cx
    lea bx, [si+RDE_NAME]
    mov cx, bx                  ; where the name starts...
.len:
    inc bx
    cmp byte [es:bx-1], 0
    jne .len                    ; ...and BX one past its NUL
    sub bx, 5                   ; where a `.O88` tail's dot would be
    xor ah, ah                  ; 0 = a plain file
    cmp bx, cx
    jb .set                     ; a name under four characters has no tail
    cmp word [es:bx+2], '88'
    jne .set
    cmp byte [es:bx], '.'
    jne .set
    mov al, [es:bx+1]
    call rd_upper
    cmp al, 'O'
    jne .set
    inc ah                      ; 1 = a package
.set:
    mov [es:si+RDE_TYPE], ah
    pop cx
    pop bx
    pop ax
    ret

; rd_upper - AL = ASCII -> upper case
rd_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; rd_scopy - DS:SI -> ES:DI, a NUL string of at most CX characters, and
;            NUL-terminated whatever it found (internal)
; out: CF = 0; every register preserved
;
; THE ONE NAME COPY. There were seven loops of this shape, one per pair of
; segments and per length, and they differed only in which segment each side
; was read through - which is what rd_scopy_in below supplies. ES is set by
; every caller here, never assumed: it is KERNEL_SEG on entry (SPEC.md 20.1).
rd_scopy:
    push ax
    push cx
    push si
    push di
    cld
.next:
    lodsb
    stosb
    or al, al
    jz .out
    loop .next
    xor al, al                  ; CX characters and no NUL among them: end it
    stosb
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; rd_scopy_in - ES:SI -> DS:DI, the same, for a name in a segment that is NOT
;               ours: the kernel's (FSV_*), or the page's (rd_svc_page)
; out: CF = 0; every register preserved
rd_scopy_in:
    call .swap                  ; swap DS and ES, so rd_scopy's DS:SI -> ES:DI
    call rd_scopy               ; is the caller's ES:SI -> DS:DI...
.swap:                          ; ...and fall in to swap them back, whose
    push ds                     ; `ret` is this routine's. The pushes and pops
    push es                     ; CROSS ON PURPOSE - it is an exchange, which
    pop ds                      ; tests/unit/t_asmrules.py's restore-order
    pop es                      ; check rightly reads as a defect anywhere it
    ret                         ; is a routine's own prologue and epilogue

; rd_ncmp - row ES:SI's name against the staged one at DS:DI, case-insensitive
; out: CF=0 equal; SI and DI preserved
;
; The row is in the directory's claim and the name in rd_nbuf, where rd_vlook
; staged it out of KERNEL_SEG - two segments, and ES and DS are both of them.
rd_ncmp:
    push ax
    push bx
    push di
    lea bx, [si+RDE_NAME]
.next:
    mov al, [es:bx]
    call rd_upper
    mov ah, al
    mov al, [di]
    call rd_upper
    cmp al, ah
    jne .no
    or al, al
    jz .yes                     ; both reached the NUL together
    inc bx
    inc di
    jmp short .next
.yes:
    pop di
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rd_vlook - find the CALLER's name in the CURRENT folder
; in:  ES:SI = a NUL name - KERNEL_SEG, as every FSV_* name verb is handed it
; out: CF=0 and SI = the row; CF=1 = no such name. EITHER WAY ES = the
;      directory and DI = rd_nbuf, holding the name - which is what
;      rd_new_row copies into a fresh row
; clobbers: AX, CX, DI, ES, flags
;
; THE NAME IS STAGED FIRST, because the row it is compared against is in a
; segment that is neither ours nor the kernel's (SPEC.md 62.9.13.1) and the
; 8086 has one segment register to spare. 15 characters bounds the copy the
; way the row's own name field does; an 8.3 name is at most 12.
; -----------------------------------------------------------------------------
rd_vlook:
    mov di, rd_nbuf
    mov cx, 15
    call rd_scopy_in            ; ES:SI -> DS:DI
    mov es, [rd_dtab]
; rd_look - the same for a name ALREADY in DS:DI, with ES = the directory
rd_look:
    xor si, si
.row:
    cmp si, RD_DIRB
    jae .none
    cmp byte [es:si+RDE_TYPE], RD_FREE
    je .nextrow
    mov al, [es:si+RDE_PAR]
    cmp al, [rd_cwd]            ; this folder only: a name resolves where the
    jne .nextrow                ; volume is standing (SPEC.md 19.2)
    call rd_ncmp
    jnc .out
.nextrow:
    add si, RDE_SZ
    jmp short .row
.none:
    stc
.out:
    ret

; =============================================================================
; THE FSV_* VERBS
; =============================================================================

; -----------------------------------------------------------------------------
; FSV_CHDIR - stand in a folder, and say what is above it (SPEC.md 62.9.1)
; in:  AX = a handle out of an entry, 0 = the root
; out: CF=0 and DX = THE PARENT'S HANDLE; CF=1 = no such folder
;
; The parent is answered HERE rather than by a verb of its own because this is
; the only call that ever moves the current folder - the driver is being told
; where to go at the exact moment it can say what is above it, and the kernel
; has no directory sector to read a '..' out of.
; -----------------------------------------------------------------------------
rd_chdir:
    push si
    or ax, ax
    jz .root
    call rd_row
    jc .no
    cmp byte [es:si+RDE_TYPE], 2    ; folders only - nothing chdirs into a file
    jne .no
    mov [rd_cwd], al
    mov dl, [es:si+RDE_PAR]
    xor dh, dh
    pop si
    clc
    ret
.root:
    mov byte [rd_cwd], 0
    xor dx, dx                  ; the root's parent is never read: [dsk_cwd]=0
    pop si                      ; makes disk_mount synthesize no '..' at all
    clc
    ret
.no:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_LIST - append this folder's entries (SPEC.md 62.9.1)
;
; THE DRIVER APPENDS AND THE KERNEL COUNTS, and the kernel also SORTS (19.4)
; and synthesizes `..` (19.5) - two places deciding a listing's order is how a
; display index stops meaning what the hit-test thinks it means.
; -----------------------------------------------------------------------------
rd_list:
    push ax
    push bx
    push cx
    push si
    push di
    mov es, [rd_dtab]
    xor si, si
    mov cx, RD_MAXENT
.next:
    cmp byte [es:si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [es:si+RDE_PAR]
    cmp al, [rd_cwd]
    jne .skip
    call rd_stage               ; SI's row -> rd_ent
    push si
    mov si, rd_ent              ; ES is set by the X stub from our own DS, so
    call OSAPI_FS_ENT           ; there is nothing to load here...
    pop si
    mov es, [rd_dtab]           ; ...and the directory is re-read after it,
                                ; SPEC.md 66.3 rule 2 kept rather than argued
                                ; (`mov` leaves the call's CF alone)
    jc .full                    ; the listing is full: SPEC.md 19's cap, and
.skip:                          ; stopping cleanly is what the kernel's own
    add si, RDE_SZ              ; directory scan does
    loop .next
.full:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_ENUM - the Nth entry of a folder, by ordinal (SPEC.md 62.9.7)
; in:  AL = the folder's handle (0 = the root), CX = the ordinal,
;      DX:BX = a 32-byte buffer
; out: CF=0 and the buffer holds a staged SPEC.md 19.1 entry;
;      CF=1 with AX = 0 = past the end (not an error)
;
; THE ORDINAL HAS TO BE STABLE, and that is a property of walking the ROW
; ARRAY BY INDEX rather than of anything clever: rows are laid down in
; creation order and never compacted, so the same (handle, ordinal) names the
; same row for as long as the FOLDER is unmodified.
; -----------------------------------------------------------------------------
rd_enum:
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the folder, so AL is free to load rows
    mov es, [rd_dtab]
    xor si, si
    mov di, RD_MAXENT
.row:
    cmp byte [es:si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [es:si+RDE_PAR]
    cmp al, ah
    jne .skip                   ; somebody else's child
    jcxz .found                 ; ...this is the one asked for
    dec cx
.skip:
    add si, RDE_SZ
    dec di
    jnz .row
    xor ax, ax                  ; past the end, and NOT an error: it is how
    stc                         ; every walk of every folder finishes
    jmp short .out
.found:
    call rd_stage               ; (it keeps BX, the caller's buffer offset,
                                ; which nothing else holds)
    push es                     ; ...and out to that buffer, which is DX:BX
    mov es, dx                  ; because ES is KERNEL_SEG on entry to
    mov si, rd_ent              ; anything the kernel far-calls (SPEC.md 51.8)
    mov di, bx
    mov cx, DSK_DE_SIZE
    cld
    rep movsb
    pop es
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_stage - one row -> the 32-byte staged SPEC.md 19.1 entry (internal)
; in:  ES:SI = the row
; out: rd_ent filled; AX/DI clobbered, every other register PRESERVED
;
; CX is saved because the caller's loop counter is in it, and the first
; version of this did not save it: the zeroing loop below spent CX, rd_list's
; `loop` then ran ~65,000 times off the end of the table, and the mount filled
; the listing to its cap with whatever followed.
; -----------------------------------------------------------------------------
rd_stage:
    push cx
    push si
    push es                     ; the directory...
    push ds
    pop es                      ; ...and ours for the zeroing, then back
    mov di, rd_ent
    mov cx, DSK_DE_SIZE/2
    xor ax, ax
    cld
    rep stosw                   ; zeroed first, every field this does not set
    pop es

    push si                     ; @0: the display name, NUL and all
    add si, RDE_NAME
    mov di, rd_ent
    mov cl, 15
    call rd_scopy_in            ; ES:SI (the row) -> DS:DI
    pop si
    mov al, [es:si+RDE_TYPE]    ; @16: 0 file / 1 package / 2 folder. NEVER 3 -
    mov [rd_ent+16], ax         ; the parent link is the kernel's to synthesize
    call rd_hand                ; @18: OUR OPAQUE HANDLE (AH = 0: it is <= 96)
    mov [rd_ent+18], ax
    mov ax, [es:si+RDE_SIZE]    ; @20: the size dword, which fm_measure sums
    mov [rd_ent+20], ax
    mov ax, [es:si+RDE_SIZE+2]
    mov [rd_ent+22], ax
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - a name in the current folder -> a handle (SPEC.md 62.9.1)
; in:  SI -> a NUL 8.3 name, in KERNEL_SEG
; out: CF=0, AX = the handle, DX:CX = the size, BL = a FAT-style attribute
;      byte; CF=1 and AX = FERR_*
;
; The ONLY verb that takes a name, and every read composes on it. BL is a FAT
; attribute byte and not the 19.1 type word, so dskw_rbody's existing
; `test 0x18` refuses a folder with FERR_PROT.
; -----------------------------------------------------------------------------
rd_stat:
    call rd_vlook               ; ES:SI = the caller's name
    jc rd_e_noent
    mov cx, [es:si+RDE_SIZE]
    mov dx, [es:si+RDE_SIZE+2]
    xor bx, bx
    cmp byte [es:si+RDE_TYPE], 2
    jne .attr
    mov bl, 0x10                ; a directory, in the kernel's own vocabulary
.attr:
    jmp rd_hand                 ; AX = the handle (AH is 0: it is <= 96), and
                                ; CF = 0

; -----------------------------------------------------------------------------
; FSV_READ - the whole file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, DI:CX = its 32-bit capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = FERR_*
;
; THE CAPACITY IS CHECKED AGAIN HERE and that is not redundant: dskw_rbody
; checks it before it calls, but dsk_read_chain - the loader's path - reaches
; this cell without passing that check at all, and a driver that trusts its
; caller's arithmetic writes past the end of somebody else's claim.
;
; It reports its progress as it goes (SPEC.md 12.8.1): the kernel arms the
; activity widget from FSV_STAT's size and is then one far call deep in here,
; blind, until this returns. rd_io does it per extent - 1KB to 8KB a report,
; whichever granule the store picked - which is a plausible frame, and the only
; place this driver pretends to be slow.
; -----------------------------------------------------------------------------
rd_read:
    call rd_row
    jc rd_e_noent
    mov ax, [es:si+RDE_SIZE+2]  ; a 32-BIT capacity against a 32-bit size, and
    cmp ax, di                  ; the high words decide first
    ja rd_e_big
    jb .fits
    mov ax, [es:si+RDE_SIZE]
    cmp ax, cx
    ja rd_e_big
.fits:
    mov [rd_bufs], dx
    mov [rd_bufo], bx
    xor ax, ax                  ; from the top...
    mov [rd_off], ax
    mov [rd_off+2], ax
    mov ax, [es:si+RDE_SIZE]    ; ...and all of it
    mov [rd_n], ax
    mov dx, [es:si+RDE_SIZE+2]
    mov [rd_n+2], dx
    or dx, ax
    jz .out                     ; an empty file: 0 bytes, and not an error
    mov ax, 0x0100              ; store -> caller, reporting each piece
    call rd_io
    jc rd_e_io                  ; the extended store refused a stage: the
                                ; buffer holds someone else's bytes and saying
                                ; so is the only honest answer (62.9.10.2)
.out:
    mov ax, [rd_n]
    mov dx, [rd_n+2]            ; CF = 0 from the `or` or from rd_io
    ret

; -----------------------------------------------------------------------------
; THE FSV_* REFUSALS, spelled once: AX = the FERR_* and CF = 1. They sit in
; the middle of the verbs so every one of them is a short jump away.
; -----------------------------------------------------------------------------
rd_e_io:
    xor dx, dx                  ; FSV_READ / READAT answer a count in DX:AX,
    mov ax, FERR_IO             ; and a refusal's is nothing
    jmp short rd_e
rd_e_noent:
    mov ax, FERR_NOENT
    jmp short rd_e
rd_e_big:
    mov ax, FERR_BIG
rd_e:
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_READAT - a chunk of a file, by handle and byte offset (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, CX = its capacity, DI:SI = the offset
; out: CF=0 and DX:AX = the bytes delivered, 0 = at or past the end
;
; No alignment precondition at all, where the FAT path needs whole clusters at
; both ends: there are no clusters here, so dskw_read_at's two refusals simply
; do not arise on this volume.
; -----------------------------------------------------------------------------
rd_readat:
    mov [rd_off], si
    mov [rd_off+2], di
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    call rd_row
    jc rd_e_noent
    mov ax, [es:si+RDE_SIZE]    ; what is LEFT of the file, in 32 bits: an
    mov dx, [es:si+RDE_SIZE+2]  ; offset can now be past 64KB into a file that
    sub ax, [rd_off]            ; is itself megabytes
    sbb dx, [rd_off+2]
    jc .eof                     ; the offset is past the end: 0 bytes, and NOT
    or dx, dx                   ; an error - it is how every walk finishes
    jnz .clamp                  ; more than a word left, so the capacity wins
    or ax, ax
    jz .eof
    cmp ax, cx
    jbe .take
.clamp:
    mov ax, cx                  ; ...clamped to what was asked for
.take:
    mov [rd_n], ax
    mov word [rd_n+2], 0
    xor ax, ax                  ; store -> caller, no reports
    call rd_io
    jc rd_e_io                  ; (62.9.10.2)
    mov ax, [rd_n]
    jmp short .out
.eof:
    xor ax, ax
.out:
    xor dx, dx                  ; ...which is also what leaves CF = 0
    ret

; -----------------------------------------------------------------------------
; FSV_WRITE - create or replace a file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG, DX:BX = the bytes, DI:CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; THE CHAIN IS RESIZED BEFORE ANY BYTE MOVES, and rd_chain_fit is
; all-or-nothing (SPEC.md 62.9.10) - so a replace that will not fit leaves the
; file it was replacing exactly as it was, and there is no window in which a
; name exists with a half-built chain behind it. A FAT volume has to commit an
; order (SPEC.md 18.4 rule 1) precisely because it cannot do that.
; -----------------------------------------------------------------------------
rd_write:
    mov [rd_n], cx              ; banked: rd_vlook and rd_free_row both spend
    mov [rd_n+2], di            ; the registers this needs at the end
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    xor ax, ax                  ; a write starts at the top of the file
    mov [rd_off], ax
    mov [rd_off+2], ax
    cmp byte [rd_have], 0
    je rd_e2_full
    mov [rd_fitlo], cx          ; DI:CX = the count. A file is bounded by the
    mov dx, di                  ; volume now and by nothing else - the 63.5KB
    call rd_fits                ; ceiling went with the 16-bit offsets
    jc rd_e2_big                 ; (SPEC.md 62.9.10)
    call rd_vlook               ; ES:SI = the name
    jnc .have
    call rd_new_row             ; a new file: a row...
    jc rd_e2_dirfull
    call rd_settype             ; ...and only now can the extension be read
    call .fit
    jnc .store                  ; the row was ours a moment ago and nothing
    mov byte [es:si+RDE_TYPE], RD_FREE  ; else can have taken it, so giving
    jmp rd_e2_full               ; it back is the whole rollback
.have:
    cmp byte [es:si+RDE_TYPE], 2
    je rd_e2_prot                ; a folder is not a file to be overwritten
    call .fit
    jc rd_e2_full
.store:
    mov cx, [rd_n]
    mov [es:si+RDE_SIZE], cx
    mov ax, [rd_n+2]
    mov [es:si+RDE_SIZE+2], ax
    or ax, cx
    jz .ok                      ; CF = 0
    mov ax, 0x0001              ; caller -> store, no reports
    call rd_io
    jc rd_e2_io                 ; (62.9.10.2)
.ok:
    ret
.fit:                           ; SI's chain, made [rd_n] long
    mov cx, [rd_n]
    mov dx, [rd_n+2]
    jmp rd_chain_fit

; ...and AGAIN for FSV_WRITE and FSV_APPEND, and a third time for the four
; name verbs below: an 8086 has no near conditional jump, and a refusal that is
; not a short jump away is assembled as a jump AROUND a near one - five bytes a
; site, against the two of a short `jc`.
rd_e2_io:
    xor dx, dx
    mov ax, FERR_IO
    jmp short rd_e2
rd_e2_noent:
    mov ax, FERR_NOENT
    jmp short rd_e2
rd_e2_big:
    mov ax, FERR_BIG
    jmp short rd_e2
rd_e2_full:
    mov ax, FERR_FULL
    jmp short rd_e2
rd_e2_dirfull:
    mov ax, FERR_DIRFULL
    jmp short rd_e2
rd_e2_prot:
    mov ax, FERR_PROT
rd_e2:
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_APPEND - add CX bytes to the end of an existing file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG, DX:BX = the bytes, CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; This is what the copy engine streams through (SPEC.md 22.5), so it is the
; one write verb whose COST matters: it extends the chain by whatever the new
; length needs and writes only at the end.
; -----------------------------------------------------------------------------
rd_append:
    mov [rd_n], cx
    mov word [rd_n+2], 0        ; FSV_APPEND's count is a WORD by contract -
                                ; the copy engine chunks - and everything below
                                ; it is 32-bit, so the high half is stated once
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    call rd_vlook               ; ES:SI = the name
    jc rd_e2_noent
    cmp byte [es:si+RDE_TYPE], 2
    je rd_e2_prot
    mov ax, [es:si+RDE_SIZE]    ; where it goes, in 32 bits...
    mov dx, [es:si+RDE_SIZE+2]
    mov [rd_off], ax
    mov [rd_off+2], dx
    add ax, [rd_n]              ; ...and what the file becomes, DX:AX
    adc dx, 0
    mov [rd_fitlo], ax          ; will THAT fit the store? (DX is its high half)
    call rd_fits
    jc rd_e2_big
    mov cx, ax                  ; DX:CX = the new length, which rd_chain_fit
    call rd_chain_fit           ; takes and leaves alone
    jc rd_e2_full
    mov [es:si+RDE_SIZE], cx
    mov [es:si+RDE_SIZE+2], dx
    cmp word [rd_n], 0
    je .ok                      ; CF = 0
    mov ax, 0x0001              ; caller -> store, no reports
    call rd_io
    jc rd_e2_io                  ; (62.9.10.2)
.ok:
    ret

; -----------------------------------------------------------------------------
; FSV_DELETE - remove a file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG
;
; A FOLDER IS REFUSED HERE and removed by FSV_RMDIR, which is the kernel's own
; split (dskw_delete against dskw_rmdir) and not a choice this driver gets to
; make: files.inc calls one or the other from the entry's TYPE.
; -----------------------------------------------------------------------------
rd_delete:
    call rd_vlook               ; ES:SI = the name
    jc rd_e2_noent
    cmp byte [es:si+RDE_TYPE], 2
    je rd_e2_prot
    mov ax, [es:si+RDE_EXT]
    call rd_chain_free
    mov byte [es:si+RDE_TYPE], RD_FREE
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_RENAME - SI = the old name, DI = the new one, both in KERNEL_SEG
; -----------------------------------------------------------------------------
rd_rename:
    push di                     ; the NEW name, banked: rd_vlook spends DI...
    push es                     ; ...and the KERNEL_SEG it is in, ES being
    call rd_vlook               ; the directory after this one
    pop es
    pop di
    jc rd_e3_noent              ; (`pop` leaves rd_vlook's CF alone)
    push si                     ; the row it found
    mov si, di
    call rd_vlook               ; is the new name taken? It is staged in
    pop si                      ; rd_nbuf either way...
    jnc rd_e3_exist
    push si
    lea di, [si+RDE_NAME]
    mov si, rd_nbuf             ; ...so it goes into the row from there
    mov cx, 15
    call rd_scopy               ; DS:SI -> ES:DI
    pop si
    call rd_settype             ; a rename can change what a file IS - .TXT to
    clc                         ; .O88 makes it a package, exactly as it would
    ret                         ; on a FAT volume, where the mount re-reads it

; -----------------------------------------------------------------------------
; FSV_MKDIR - SI = a NUL name in KERNEL_SEG
;
; A folder costs a ROW and no extent, so a directory-heavy volume runs out of
; rows long before it runs out of store - which is why FERR_DIRFULL and
; FERR_FULL are different answers here as they are on a FAT volume.
; -----------------------------------------------------------------------------
rd_mkdir:
    call rd_vlook               ; ES:SI = the name, staged for rd_new_row
    jnc rd_e3_exist
    call rd_new_row
    jc rd_e3_dirfull
    mov byte [es:si+RDE_TYPE], 2    ; (a `mov`: CF = 0 stays the answer)
    ret

; -----------------------------------------------------------------------------
; FSV_RMDIR - SI = a NUL name in KERNEL_SEG; the folder must be EMPTY
;
; Emptiness is tested HERE because only the side holding the directory can
; answer it - which is the same reason the FAT path tests it inside dskw_rmdir
; rather than making files.inc walk the folder first.
; -----------------------------------------------------------------------------
rd_rmdir:
    call rd_vlook               ; ES:SI = the name
    jc rd_e3_noent
    cmp byte [es:si+RDE_TYPE], 2
    jne rd_e3_prot               ; a file is dskw_delete's, not ours
    call rd_hand                ; AL = this folder's handle...
    xor di, di                  ; ...and nothing may still name it as a parent
    mov cx, RD_MAXENT
.scan:
    cmp byte [es:di+RDE_TYPE], RD_FREE
    je .nextrow
    cmp [es:di+RDE_PAR], al
    je rd_e3_prot                ; the kernel's own word for "not empty" here
.nextrow:
    add di, RDE_SZ
    loop .scan
    mov byte [es:si+RDE_TYPE], RD_FREE
    clc
    ret

rd_e3_noent:                    ; (the third copy - see rd_e2 above)
    mov ax, FERR_NOENT
    jmp short rd_e3
rd_e3_dirfull:
    mov ax, FERR_DIRFULL
    jmp short rd_e3
rd_e3_exist:
    mov ax, FERR_EXIST
    jmp short rd_e3
rd_e3_prot:
    mov ax, FERR_PROT
rd_e3:
    stc
    ret

; -----------------------------------------------------------------------------
; rd_new_row - a fresh row in the current folder, named rd_nbuf (rd_vlook
;              staged it), ES = the directory
; out: CF=0 and SI = the row, no extent and no bytes - its TYPE is still the
;      caller's to set; CF=1 = the directory is full
; clobbers: AX, CX, DI, flags
; -----------------------------------------------------------------------------
rd_new_row:
    call rd_free_row            ; DI = a free row
    jc .out
    mov si, di
    mov word [es:si+RDE_EXT], RD_NOEXT
    xor ax, ax
    mov [es:si+RDE_SIZE], ax
    mov [es:si+RDE_SIZE+2], ax
    mov al, [rd_cwd]
    mov [es:si+RDE_PAR], al
    push si
    add di, RDE_NAME
    mov si, rd_nbuf             ; the caller's name, as rd_vlook staged it
    mov cx, 15
    call rd_scopy               ; DS:SI -> ES:DI ...CF = 0
    pop si
.out:
    ret

; -----------------------------------------------------------------------------
; FSV_DFREE - free space (SPEC.md 62.9.1)
; out: CF=0, DX:AX = free BYTES, BX = the granule
;
; EXACT, because the store is fixed extents: count the free ones and multiply.
; A bump allocator would have had to either lie here or grow a compactor, and
; this is the whole reason the arena is shaped the way it is - chained extents
; changed what a FILE can be and left that property untouched.
; -----------------------------------------------------------------------------
rd_dfree:
    push cx
    call rd_free_ext
    mul word [rd_extb]          ; DX:AX = free extents * the LIVE extent size,
                                ; which scales with the store (SPEC.md 62.9.10)
                                ; - 2,048 x 8KB genuinely needs both halves
    mov bx, [rd_extb]
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; rd_fits - would DX:[rd_fitlo] bytes fit this store at all? (internal)
; in:  DX = the count's high word, [rd_fitlo] = its low one
; out: CF = 0 yes; CF = 1 = bigger than the whole volume
; clobbers: flags
;
; The ONLY size ceiling a file has now. It was RD_FILEMAX, a constant, because
; every offset inside a file was a word; with the walk 32-bit the honest bound
; is the store, and a file that will not fit is refused with FERR_BIG before a
; byte moves - which is the refusal the one-extent store already made, in the
; same words.
; -----------------------------------------------------------------------------
rd_fits:
    push ax
    push bx
    push dx
    mov bx, dx                  ; BX:[rd_fitlo] = the count, because DX is
    call rd_kb_bytes            ; about to be the store's own high word
    sub ax, [rd_fitlo]          ; the store LESS the count, which borrows
    sbb dx, bx                  ; exactly when the count is the larger
    pop dx
    pop bx
    pop ax
    ret
