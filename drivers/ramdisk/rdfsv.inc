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
; =============================================================================

; =============================================================================
; DIRECTORY HELPERS
; =============================================================================

; rd_dir_clear - every row free
rd_dir_clear:
    push cx
    push di
    mov di, rd_dir
    mov cx, RD_MAXENT
.blank:
    mov byte [di+RDE_TYPE], RD_FREE
    add di, RDE_SZ
    loop .blank
    mov byte [rd_cwd], 0
    pop di
    pop cx
    ret

; rd_row - AL = a handle -> CF=0 and SI = its row; CF=1 = no such handle
; A free row answers CF=1, so a handle held across a delete cannot resolve.
rd_row:
    push ax
    or al, al
    jz .no
    cmp al, RD_MAXENT
    ja .no
    dec al
    xor ah, ah
    push cx
    push dx
    mov cx, RDE_SZ
    mul cx
    pop dx
    pop cx
    mov si, ax
    add si, rd_dir
    cmp byte [si+RDE_TYPE], RD_FREE
    je .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; rd_hand - SI = a row -> AL = its handle (index + 1)
rd_hand:
    push dx
    push cx
    mov ax, si
    sub ax, rd_dir
    xor dx, dx
    mov cx, RDE_SZ
    div cx
    inc ax
    pop cx
    pop dx
    ret

; rd_free_row - out CF=0 and DI = a free row; CF=1 = the directory is full
rd_free_row:
    push cx
    mov di, rd_dir
    mov cx, RD_MAXENT
.scan:
    cmp byte [di+RDE_TYPE], RD_FREE
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

; rd_nfiles - out AX = live rows
rd_nfiles:
    push bx
    push cx
    push dx
    xor dx, dx
    mov bx, rd_dir
    mov cx, RD_MAXENT
.scan:
    cmp byte [bx+RDE_TYPE], RD_FREE
    je .next
    inc dx
.next:
    add bx, RDE_SZ
    loop .scan
    mov ax, dx
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_settype - SI = a row whose name is set -> its RDE_TYPE (internal)
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
    mov bx, si
    add bx, RDE_NAME
    xor cx, cx
.len:
    cmp byte [bx], 0
    je .got
    inc bx
    inc cx
    jmp short .len
.got:
    cmp cx, 4
    jb .file
    sub bx, 4                   ; the last four characters
    cmp byte [bx], '.'
    jne .file
    mov al, [bx+1]
    call rd_upper
    cmp al, 'O'
    jne .file
    cmp byte [bx+2], '8'
    jne .file
    cmp byte [bx+3], '8'
    jne .file
    mov byte [si+RDE_TYPE], 1
    jmp short .out
.file:
    mov byte [si+RDE_TYPE], 0
.out:
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

; rd_ncopy - DS:SI -> DS:DI, a NUL name, at most 15 characters (internal)
rd_ncopy:
    push ax
    push cx
    mov cx, 15
.next:
    mov al, [si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop cx
    pop ax
    ret

; rd_ncopy_es - ES:SI -> DS:DI, the same, for a name the KERNEL is handing us
rd_ncopy_es:
    push ax
    push cx
    push si
    push di
    mov cx, 15
.next:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; rd_ncmp - row SI's name against the caller's at ES:DI, case-insensitive
; out: CF=0 equal; SI and DI preserved
;
; The caller's string is in KERNEL_SEG and ours is in our own image, which is
; why ES is read explicitly: ES is KERNEL_SEG on entry to everything the
; kernel far-calls into a driver (SPEC.md 20.1).
rd_ncmp:
    push ax
    push bx
    push di
    mov bx, si
    add bx, RDE_NAME
.next:
    mov al, [bx]
    call rd_upper
    mov ah, al
    mov al, [es:di]
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

; rd_look - find ES:DI's name in the CURRENT folder
; out: CF=0 and SI = the row; CF=1 = no such name
rd_look:
    mov si, rd_dir
.row:
    cmp si, rd_dir + RD_MAXENT * RDE_SZ
    jae .none
    cmp byte [si+RDE_TYPE], RD_FREE
    je .nextrow
    mov al, [si+RDE_PAR]
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
    cmp byte [si+RDE_TYPE], 2   ; folders only - nothing chdirs into a file
    jne .no
    mov [rd_cwd], al
    mov dl, [si+RDE_PAR]
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
    mov si, rd_dir
    mov cx, RD_MAXENT
.next:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [si+RDE_PAR]
    cmp al, [rd_cwd]
    jne .skip
    call rd_stage               ; SI's row -> rd_ent
    push si
    mov si, rd_ent              ; ES is set by the X stub from our own DS, so
    call OSAPI_FS_ENT           ; there is nothing to load here
    pop si
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
    mov si, rd_dir
    mov di, RD_MAXENT
.row:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [si+RDE_PAR]
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
    push bx                     ; RD_STAGE SPENDS BX - its name copy walks the
    call rd_stage               ; row through it - and BX is the caller's
    pop bx                      ; buffer offset, which nothing else holds
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
; in:  SI = the row
; out: rd_ent filled; AX/BX/DI clobbered, CX PRESERVED
;
; CX is saved because the caller's loop counter is in it, and the first
; version of this did not save it: the zeroing loop below spent CX, rd_list's
; `loop` then ran ~65,000 times off the end of the table, and the mount filled
; the listing to its cap with whatever followed.
; -----------------------------------------------------------------------------
rd_stage:
    push cx
    mov di, rd_ent
    mov cx, DSK_DE_SIZE/2
.zero:
    mov word [di], 0            ; DS-relative, NOT `rep stosw`: ES is
    inc di                      ; KERNEL_SEG here (SPEC.md 20.1)
    inc di
    loop .zero

    mov bx, si                  ; @0: the display name, up to its own NUL
    add bx, RDE_NAME
    mov di, rd_ent
.name:
    mov al, [bx]
    mov [di], al
    or al, al
    jz .named
    inc bx
    inc di
    jmp short .name
.named:
    mov al, [si+RDE_TYPE]       ; @16: 0 file / 1 package / 2 folder. NEVER 3 -
    xor ah, ah                  ; the parent link is the kernel's to synthesize
    mov [rd_ent+16], ax
    call rd_hand                ; @18: OUR OPAQUE HANDLE
    xor ah, ah
    mov [rd_ent+18], ax
    mov ax, [si+RDE_SIZE]       ; @20: the size dword, which fm_measure sums
    mov [rd_ent+20], ax
    mov ax, [si+RDE_SIZE+2]
    mov [rd_ent+22], ax
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
    mov di, si                  ; ES:DI = the caller's name
    call rd_look
    jc .none
    mov cx, [si+RDE_SIZE]
    mov dx, [si+RDE_SIZE+2]
    xor bx, bx
    cmp byte [si+RDE_TYPE], 2
    jne .attr
    mov bl, 0x10                ; a directory, in the kernel's own vocabulary
.attr:
    call rd_hand
    xor ah, ah
    clc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

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
    jc .bad
    mov ax, [si+RDE_SIZE+2]     ; a 32-BIT capacity against a 32-bit size, and
    cmp ax, di                  ; the high words decide first
    ja .big
    jb .fits
    mov ax, [si+RDE_SIZE]
    cmp ax, cx
    ja .big
.fits:
    mov ax, [si+RDE_SIZE]
    mov [rd_n], ax
    mov ax, [si+RDE_SIZE+2]
    mov [rd_n+2], ax
    mov ax, [rd_n]
    or ax, [rd_n+2]
    jz .none                    ; an empty file: 0 bytes, and not an error
    mov ax, [si+RDE_EXT]
    mov [rd_io_ext], ax
    mov byte [rd_io_dir], 0     ; store -> caller
    mov word [rd_io_off], 0
    mov word [rd_io_off+2], 0
    mov ax, [rd_n]
    mov [rd_io_len], ax
    mov ax, [rd_n+2]
    mov [rd_io_len+2], ax
    mov [rd_io_seg], dx
    mov [rd_io_ofs], bx
    mov byte [rd_io_prog], 1
    call rd_io
    mov byte [rd_io_prog], 0
    cmp byte [rd_xerr], 0       ; the extended store refused a stage: the
    jne .ioerr                  ; buffer holds someone else's bytes and saying
.none:                          ; so is the only honest answer (62.9.10.2)
    mov ax, [rd_n]
    mov dx, [rd_n+2]
    clc
    ret
.ioerr:
    mov ax, FERR_IO
    xor dx, dx
    stc
    ret
.big:
    mov ax, FERR_BIG
    stc
    ret
.bad:
    mov ax, FERR_NOENT
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
    mov [rd_cap], cx
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    call rd_row
    jc .bad
    mov ax, [si+RDE_SIZE]       ; what is LEFT of the file, in 32 bits: an
    mov cx, [si+RDE_SIZE+2]     ; offset can now be past 64KB into a file that
    sub ax, [rd_off]            ; is itself megabytes
    sbb cx, [rd_off+2]
    jc .eof                     ; the offset is past the end: 0 bytes, and NOT
    mov dx, ax                  ; an error - it is how every walk finishes
    or dx, cx
    jz .eof
    or cx, cx
    jnz .clamp                  ; more than a word left, so the capacity wins
    cmp ax, [rd_cap]
    jbe .take
.clamp:
    mov ax, [rd_cap]            ; ...clamped to what was asked for
.take:
    mov [rd_n], ax
    mov ax, [si+RDE_EXT]
    mov [rd_io_ext], ax
    mov byte [rd_io_dir], 0
    mov ax, [rd_off]
    mov [rd_io_off], ax
    mov ax, [rd_off+2]
    mov [rd_io_off+2], ax
    mov ax, [rd_n]
    mov [rd_io_len], ax
    mov word [rd_io_len+2], 0
    mov ax, [rd_bufs]
    mov [rd_io_seg], ax
    mov ax, [rd_bufo]
    mov [rd_io_ofs], ax
    call rd_io
    cmp byte [rd_xerr], 0
    jne .ioerr                  ; (62.9.10.2)
    mov ax, [rd_n]
    xor dx, dx
    clc
    ret
.ioerr:
    mov ax, FERR_IO
    xor dx, dx
    stc
    ret
.eof:
    xor ax, ax
    xor dx, dx
    clc
    ret
.bad:
    mov ax, FERR_NOENT
    stc
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
    mov [rd_nameo], si          ; banked: rd_look and rd_free_row both spend
    mov [rd_n], cx              ; the registers this needs at the end
    mov [rd_n+2], di
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    cmp byte [rd_have], 0
    je .full
    mov ax, [rd_n]              ; DX / [rd_fitlo] = the count. It arrived as
    mov [rd_fitlo], ax          ; DI:CX and DX is the BUFFER's segment here,
    mov dx, [rd_n+2]            ; which is why it is reloaded from the bank
    call rd_fits                ; rather than passed on. A file is bounded by
    jc .big                     ; the volume now and by nothing else - the
                                ; 63.5KB ceiling went with the 16-bit offsets
                                ; (SPEC.md 62.9.10)
    mov di, si                  ; ES:DI = the name
    call rd_look
    jnc .have
    call rd_free_row            ; a new file: a row...
    jc .dirfull
    mov si, di                  ; SI = the row rd_free_row found
    mov word [si+RDE_EXT], RD_NOEXT
    mov word [si+RDE_SIZE], 0
    mov word [si+RDE_SIZE+2], 0
    mov byte [si+RDE_TYPE], 0
    mov al, [rd_cwd]
    mov [si+RDE_PAR], al
    push si
    add di, RDE_NAME            ; DI is still that row
    mov si, [rd_nameo]          ; ES:SI = the caller's name, banked above
    call rd_ncopy_es
    pop si
    call rd_settype             ; ...and only now can the extension be read
    push si
    mov cx, [rd_n]
    mov dx, [rd_n+2]
    call rd_chain_fit
    pop si
    jc .newfull                 ; the row was ours a moment ago and nothing
                                ; else can have taken it, so giving it back is
                                ; the whole rollback
    jmp short .store
.have:
    cmp byte [si+RDE_TYPE], 2
    jne .fit
    mov ax, FERR_PROT           ; a folder is not a file to be overwritten
    stc
    ret
.fit:
    push si
    mov cx, [rd_n]
    mov dx, [rd_n+2]
    call rd_chain_fit
    pop si
    jc .full
.store:
    mov cx, [rd_n]
    mov [si+RDE_SIZE], cx
    mov ax, [rd_n+2]
    mov [si+RDE_SIZE+2], ax
    or ax, cx
    jz .ok
    mov ax, [si+RDE_EXT]
    mov [rd_io_ext], ax
    mov byte [rd_io_dir], 1     ; caller -> store
    mov word [rd_io_off], 0
    mov word [rd_io_off+2], 0
    mov ax, [rd_n]
    mov [rd_io_len], ax
    mov ax, [rd_n+2]
    mov [rd_io_len+2], ax
    mov ax, [rd_bufs]
    mov [rd_io_seg], ax
    mov ax, [rd_bufo]
    mov [rd_io_ofs], ax
    call rd_io
    cmp byte [rd_xerr], 0
    jne .ioerr                  ; (62.9.10.2)
.ok:
    clc
    ret
.ioerr:
    mov ax, FERR_IO
    stc
    ret
.newfull:
    mov byte [si+RDE_TYPE], RD_FREE
    jmp short .full
.big:
    mov ax, FERR_BIG
    stc
    ret
.full:
    mov ax, FERR_FULL
    stc
    ret
.dirfull:
    mov ax, FERR_DIRFULL
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
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    je .prot
    mov ax, [si+RDE_SIZE]       ; where it goes, in 32 bits...
    mov dx, [si+RDE_SIZE+2]
    mov [rd_off], ax
    mov [rd_off+2], dx
    add ax, [rd_n]              ; ...and what the file becomes
    adc dx, 0
    mov [rd_end], ax
    mov [rd_end+2], dx
    mov [rd_fitlo], ax          ; will THAT fit the store? (DX is its high half)
    call rd_fits
    jc .big
    push si
    mov cx, [rd_end]
    mov dx, [rd_end+2]
    call rd_chain_fit
    pop si
    jc .full
    mov ax, [rd_end]
    mov [si+RDE_SIZE], ax
    mov ax, [rd_end+2]
    mov [si+RDE_SIZE+2], ax
    cmp word [rd_n], 0
    je .ok
    mov ax, [si+RDE_EXT]
    mov [rd_io_ext], ax
    mov byte [rd_io_dir], 1
    mov ax, [rd_off]
    mov [rd_io_off], ax
    mov ax, [rd_off+2]
    mov [rd_io_off+2], ax
    mov ax, [rd_n]
    mov [rd_io_len], ax
    mov word [rd_io_len+2], 0
    mov ax, [rd_bufs]
    mov [rd_io_seg], ax
    mov ax, [rd_bufo]
    mov [rd_io_ofs], ax
    call rd_io
    cmp byte [rd_xerr], 0
    jne .ioerr                  ; (62.9.10.2)
.ok:
    clc
    ret
.ioerr:
    mov ax, FERR_IO
    stc
    ret
.big:
    mov ax, FERR_BIG
    stc
    ret
.full:
    mov ax, FERR_FULL
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
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
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    je .prot
    mov ax, [si+RDE_EXT]
    call rd_chain_free
    mov byte [si+RDE_TYPE], RD_FREE
    clc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_RENAME - SI = the old name, DI = the new one, both in KERNEL_SEG
; -----------------------------------------------------------------------------
rd_rename:
    mov [rd_nameo], di          ; bank the NEW name: rd_look spends DI
    mov di, si
    call rd_look
    jc .none
    push si                     ; the row it found
    mov di, [rd_nameo]
    call rd_look                ; is the new name taken? (rd_look uses ES:DI)
    pop si
    jnc .exists
    push si
    mov di, si
    add di, RDE_NAME
    mov si, [rd_nameo]
    call rd_ncopy_es            ; ES:SI -> DS:DI
    pop si
    call rd_settype             ; a rename can change what a file IS - .TXT to
    clc                         ; .O88 makes it a package, exactly as it would
    ret                         ; on a FAT volume, where the mount re-reads it
.exists:
    mov ax, FERR_EXIST
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_MKDIR - SI = a NUL name in KERNEL_SEG
;
; A folder costs a ROW and no extent, so a directory-heavy volume runs out of
; rows long before it runs out of store - which is why FERR_DIRFULL and
; FERR_FULL are different answers here as they are on a FAT volume.
; -----------------------------------------------------------------------------
rd_mkdir:
    mov [rd_nameo], si
    mov di, si
    call rd_look
    jnc .exists
    call rd_free_row
    jc .dirfull
    mov si, di
    mov byte [si+RDE_TYPE], 2
    mov word [si+RDE_EXT], RD_NOEXT
    mov word [si+RDE_SIZE], 0
    mov word [si+RDE_SIZE+2], 0
    mov al, [rd_cwd]
    mov [si+RDE_PAR], al
    add di, RDE_NAME
    mov si, [rd_nameo]
    call rd_ncopy_es
    clc
    ret
.exists:
    mov ax, FERR_EXIST
    stc
    ret
.dirfull:
    mov ax, FERR_DIRFULL
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_RMDIR - SI = a NUL name in KERNEL_SEG; the folder must be EMPTY
;
; Emptiness is tested HERE because only the side holding the directory can
; answer it - which is the same reason the FAT path tests it inside dskw_rmdir
; rather than making files.inc walk the folder first.
; -----------------------------------------------------------------------------
rd_rmdir:
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    jne .prot                   ; a file is dskw_delete's, not ours
    call rd_hand                ; AL = this folder's handle...
    push si
    mov si, rd_dir              ; ...and nothing may still name it as a parent
    mov cx, RD_MAXENT
.scan:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .nextrow
    cmp [si+RDE_PAR], al
    je .notempty
.nextrow:
    add si, RDE_SZ
    loop .scan
    pop si
    mov byte [si+RDE_TYPE], RD_FREE
    clc
    ret
.notempty:
    pop si
    mov ax, FERR_PROT           ; the kernel's own word for "not empty" here
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
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
    push cx
    push dx
    mov ax, [rd_kb]             ; BX:AX = the store, in bytes - and BX rather
    mov bx, ax                  ; than DX, because DX is the count's own high
    mov cl, 10                  ; word and this compares the two
    shl ax, cl
    mov cl, 6
    shr bx, cl
    pop dx                      ; the count, back off the stack: the shifts
    push dx                     ; above spent CL and CX is its low half
    mov cx, [rd_fitlo]
    cmp bx, dx
    ja .fits
    jb .no
    cmp ax, cx
    jb .no
.fits:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
