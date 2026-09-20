; =============================================================================
; os8088 - HDD.DRV / mount.inc
;
; MOUNTING (SPEC.md 52.4), and it is what is LEFT of the old page.inc: the
; Control Panel page moved to the tool image (cppage.inc) and this did not,
; because the volume table is the resident's and a boot automount happens with
; no page on screen at all.
;
; It answers in [hd_msgc], an HDM_* code, for the reason hddabi.inc gives: the
; strings are in whichever image letters them and that is no longer this one.
; =============================================================================

; =============================================================================
; Mounting (SPEC.md 52.4)
; =============================================================================

; -----------------------------------------------------------------------------
; hd_mount - mount the selected device's first FAT partition
; in:  nothing; the gfx lock is HELD (a page click)
; out: nothing; [hd_msg] says what happened
; clobbers: flags
;
; It mounts the FIRST FAT partition and stops; the loop that takes all four is
; hd_mount_all's. Neither claims anything: SPEC.md 22.6 retired the listing
; donation, so a mounted volume costs this driver a hd_vols row and no heap.
; -----------------------------------------------------------------------------
hd_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [hd_nmnt], 0
    mov byte [hd_msgc], HDM_MNOTAB
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load
    jc .out                     ; no readable table: nothing to mount

    mov byte [hd_msgc], HDM_MNOFAT
    mov byte [hd_pslot], 0
.slot:
    mov al, [hd_pslot]
    call hd_part_ent
    call hd_part_isfat
    jc .next                    ; free, or a type that is not a FAT volume:
                                ; SKIPPED, which is the whole of "and if it
                                ; is not FAT, do not" (SPEC.md 52.4)
    cmp word [si+HP_SECS+2], 0
    jne .big                    ; over 65,535 sectors: not a volume this
                                ; kernel can carry (SPEC.md 18.7)
    call hd_already
    jnc .have                   ; this one is already mounted - by us...
    mov al, [hd_sel]            ; ...or by the KERNEL, which adopted the
    call hd_kvol                ; partition it booted from before we were
    jnc .have                   ; loaded at all (SPEC.md 52.10.3). Mounting it
                                ; again is a second icon on the same sectors.
                                ; It is hdcom.inc's and takes the DEVICE as an
                                ; argument, because the installer asks the
                                ; same question of a device of its own
                                ; (SPEC.md 52.10.4.1)
    call hd_mount_one
    jmp short .next
.have:
    mov byte [hd_msgc], HDM_MHAVE   ; and SAY so: the caption was still
                                    ; 'No FAT partition on it' from the top of
                                    ; the loop, which about a partition that is
                                    ; mounted and on the desktop is the one
                                    ; answer that is not true
    jmp short .next
.big:
    mov byte [hd_msgc], HDM_MBIG    ; the tool calls this slot Unmountable and
                                    ; so does the page, rather than reporting
                                    ; 'no FAT partition' about a partition that
                                    ; plainly is one (SPEC.md 52.2.2)
.next:
    inc byte [hd_pslot]
    cmp byte [hd_pslot], 4
    jb .slot

    cmp byte [hd_nmnt], 0
    je .out                     ; the caption is whichever refusal got here
    call hd_mount_says          ; 'Mounted n volume(s)'
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount_one - mount the partition [hd_pslot] names (module internal)
; in:  SI = its entry, [hd_sel] = the device; the gfx lock is HELD
; out: [hd_nmnt] incremented on success; [hd_msg] set on a failure
; clobbers: flags
;
; NO LISTING CLAIM (SPEC.md 22.6). This used to claim 6KB and hand it over
; with the volume, so that the partition listed more entries than the kernel's
; own floor gave a floppy; the floor is the same cap for every volume now, so
; the claim funded nothing and DX goes over as 0.
;
; A partition whose TYPE says FAT and whose CONTENT does not is dropped here:
; osapi_vol_mount answers CF=1, the row goes back, and the loop carries on to
; the next slot. That is the second half of "and if it is not FAT, do not".
; -----------------------------------------------------------------------------
hd_mount_one:
    push ax
    push bx
    push cx
    push dx
    push si
    call hd_vol_new             ; BX = a free hd_vols row
    jc .full
    mov al, [hd_sel]
    mov [bx+HDV_DEV], al
    mov al, [hd_pslot]
    mov [bx+HDV_PART], al
    mov ax, [si+HP_LBA]
    mov [bx+HDV_BASE], ax
    mov ax, [si+HP_LBA+2]
    mov [bx+HDV_BASE+2], ax
    mov ax, [si+HP_SECS]
    mov [bx+HDV_SECS], ax

    mov al, [bx+HDV_VOLH]       ; our own handle IS the row index
    mov cx, [bx+HDV_SECS]
    xor dx, dx                  ; no donated listing claim: the kernel's own
                                ; buffers, which is every volume now (22.6)
    xor si, si                  ; no label: the KERNEL derives 'HDD C'..'HDD F'
    call OSAPI_VOL_ADD          ; from the volume index, which is the only
    jc .novol                   ; place that knows it (SPEC.md 26.1)
    mov [bx+HDV_VOL], al
    mov byte [bx+HDV_USED], 1   ; AL is STILL the volume index - the reload
    call OSAPI_VOL_MOUNT        ; that was here existed because the movable
                                ; declaration passed its proc through AX
    jc .badvol
    inc byte [hd_nmnt]
    jmp short .out
.badvol:
    call hd_unmount_row         ; typed FAT, is not FAT: give it all back and
    jmp short .out              ; leave the loop to try the next slot
.novol:
    mov byte [bx+HDV_USED], 0
.full:
    mov byte [hd_msgc], HDM_MFULL
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hd_lst_reloc lived here - the owner's half of a DONATED listing claim's move
; (SPEC.md 66.5.10.2), a by-value walk of hd_vols patching HDV_LSEG. SPEC.md
; 22.6 retired the donation, so this driver holds no claim on the kernel's
; behalf and declares nothing movable.

; -----------------------------------------------------------------------------
; hd_already - is [hd_sel]'s partition [hd_pslot] mounted already?
; in:  nothing
; out: CF = 0 yes, CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
hd_already:
    push ax
    push bx
    push cx
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, [hd_pslot]
    je .yes
.next:
    add bx, HDV_SIZE
    loop .scan
    pop cx
    pop bx
    pop ax
    stc
    ret
.yes:
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_mount_says - say 'Mounted n volume(s)' (module internal)
; in:  [hd_nmnt]
; out: [hd_msgc] = HDM_MOUNTED
; clobbers: flags
;
; IT BUILT THE STRING and now it sets a code. The count is already in
; [hd_nmnt] and the renderer has it, so what crossed as 32 bytes of built
; caption crosses as one byte and a number the page was reading anyway -
; which is the whole of hddabi.inc's rule about a caption being a pointer
; only in the image that letters it.
; -----------------------------------------------------------------------------
hd_mount_says:
    mov byte [hd_msgc], HDM_MOUNTED
    ret

; -----------------------------------------------------------------------------
; hd_vol_new - the first free hd_vols row (module internal)
; in:  nothing
; out: CF = 0 and BX = the row, with HDV_VOLH set to its own index;
;      CF = 1 = all of them are in use
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
hd_vol_new:
    push ax
    push cx
    mov bx, hd_vols
    xor al, al
    mov cx, HD_MAXVOL
.scan:
    cmp byte [bx+HDV_USED], 0
    je .free
    add bx, HDV_SIZE
    inc al
    loop .scan
    xor bx, bx
    pop cx
    pop ax
    stc
    ret
.free:
    mov [bx+HDV_VOLH], al
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_unmount - take the selected device's volume away
; in:  nothing; the lock is HELD
; out: nothing
; -----------------------------------------------------------------------------
hd_unmount:
    push ax
    push bx
    push cx
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    call hd_unmount_row         ; EVERY volume this device has, not the first:
.next:                          ; Mount put up one icon per FAT partition and
    add bx, HDV_SIZE            ; Unmount is the other half of that
    loop .scan
    mov byte [hd_msgc], HDM_UNMOUNTED
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_unmount_row - drop one volume and its claim (module internal)
; in:  BX = the hd_vols row
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_unmount_row:
    push ax
    push dx
    cmp byte [bx+HDV_USED], 0
    je .out
    mov al, [bx+HDV_VOL]
    call OSAPI_VOL_DEL
    mov byte [bx+HDV_USED], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_vol_of - which kernel volume is mounted on one partition (SPEC.md 52.12)
; in:  AL = the device index, AH = the partition slot
; out: AL = the kernel's volume index, or 0FFh if nothing is mounted there
; clobbers: AX (the output), flags
;
; hd_mount_one publishes its result by filling a row rather than by answering,
; so somebody has to go and look; the installer used to, and this is that walk
; moved to the side of the boundary the volume table is on.
; -----------------------------------------------------------------------------
hd_vol_of:
    push bx
    push cx
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, dl
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, dh
    jne .next
    mov al, [bx+HDV_VOL]
    jmp short .out
.next:
    add bx, HDV_SIZE
    loop .scan
    mov al, 0xFF
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_unmount_part - drop every volume mounted on one partition (SPEC.md 52.12)
; in:  AL = the device index, AH = the partition slot
; out: AL = 1 if anything was unmounted, 0 if not
; clobbers: AX (the output), flags
;
; The disk tool's, until the split moved the volume table out of its reach.
; It is the right place for it either way: hd_vols is the RESIDENT's - the
; kernel's volume indices are registered from here and given back from here -
; so the tool naming a partition and this doing the whole job is one crossing
; instead of a shared table.
;
; EVERY row and not the first, for hd_unmount's own reason: one partition is
; one volume today, and a scan that stops early is the kind of thing that
; stops being true quietly.
; -----------------------------------------------------------------------------
hd_unmount_part:
    push bx
    push cx
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    xor al, al
    mov ah, 0xFF                ; ...and AH = the last volume index dropped,
    push ax                     ; 0xFF = none. The answer, built on the stack
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, dl
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, dh
    jne .next
    pop ax                      ; the answer FIRST, because the pop would
    mov al, 1                   ; clobber AH, and the row is still live here:
    mov ah, [bx+HDV_VOL]        ; SPEC.md 52.10.8 needs WHICH volume went and
    push ax                     ; not only that one did
    call hd_unmount_row
.next:
    add bx, HDV_SIZE
    loop .scan
    pop ax
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_win_close_all - hide the tool window (detach's obligation)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_win_close_all:
    call hd_tool_drop           ; HDT_SHUT and then the free, in that order. A
    ret                         ; window of the TOOL's left on screen is a paint
                                ; through freed memory twice over - the kernel
                                ; releases this image the moment we return, and
                                ; we release the tool's before that

; =============================================================================
; Shared helpers
; =============================================================================

; -----------------------------------------------------------------------------
; hd_raw - one raw transfer to a DEVICE, outside any volume
; in:  [hd_rawop] = 0 read / 1 write, DX:AX = the LBA, ES:BX = the buffer,
;      CX = sectors, DI = the device row
; out: CF = 0 done; CF = 1 failed
; clobbers: flags
;
; The op is in MEMORY rather than a register because the LBA needs DX:AX and
; the buffer needs ES:BX - the same reason DSV_BLK takes its own buffer in
; DX:BX (SPEC.md 51.8).
; -----------------------------------------------------------------------------
hd_raw:
    mov [hd_lba], ax
    mov [hd_lba+2], dx
    mov [hd_bofs], bx
    mov [hd_bcnt], cx
    push ax
    mov ax, es
    mov [hd_bseg], ax
    pop ax
    push ax
    mov al, [hd_rawop]
    mov [hd_bop], al
    pop ax
hd_raw_op:
    push ax
    cmp byte [di+HDD_KIND], HDK_IDE
    je .ide
    call hd_bios_xfer
    jmp short .out
.ide:
    call hd_ide_xfer
.out:
    pop ax
    ret

