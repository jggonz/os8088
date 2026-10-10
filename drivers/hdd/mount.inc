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
    call hd_part_big
    jc .big                     ; longer than any FAT16 volume: not one this
                                ; kernel can carry (SPEC.md 18.7.5)
    mov dl, [hd_sel]
    mov dh, [hd_pslot]
    mov bx, hd_vols
    call hd_vol_find
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
    mov byte [hd_msgc], HDM_MOUNTED ; 'Mounted n volume(s)': a CODE, and the
                                ; count is [hd_nmnt], which the page's renderer
                                ; reads anyway (hddabi.inc's rule about a
                                ; caption being a pointer only in the image
                                ; that letters it)
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
%ifdef HD_CYLPROBE
    call hd_cylprobe            ; may one call cross a head? (18.91.5)
%endif
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
    mov dx, [si+HP_SECS+2]
    mov [bx+HDV_LEN+2], dx      ; the whole length, for hd_blk's bound
    mov ax, [si+HP_SECS]
    mov [bx+HDV_LEN], ax
    or dx, dx
    jz .secs
    xor ax, ax                  ; past 32MB the length does not fit
.secs:                          ; OSAPI_VOL_ADD's CX: say 'unknown', which
    mov [bx+HDV_SECS], ax       ; the kernel's rule 13 accepts (SPEC.md 18.7.5)

    mov cx, ax                  ; = [bx+HDV_SECS]
    mov al, [bx+HDV_VOLH]       ; our own handle IS the row index
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

%ifdef HD_CYLPROBE
; -----------------------------------------------------------------------------
; hd_cylprobe - may ONE int 13h carry a read across a head on [hd_sel]'s
; device? (SPEC.md 18.91.5) - the kernel's dsk_hcyl_probe, for rung 0
; in:  SI = the partition entry being mounted, [hd_sel] = the device
; out: the device's HDF_CYLASK set once it has been asked, HDF_CYLOK too when
;      the answer is yes; every register preserved
;
; Asked of the DATA, because a ROM that does not walk the head answers a
; wrong read with CF = 0: the partition's boot sector and the sector before
; it - the last of the previous head - a sector a call (hd_xfer, which bounds
; at the track while the bit is clear) and then in ONE call, into a buffer
; whose second half was patterned first. Only a partition that starts a
; track on head 1 or above can be asked, so one that cannot leaves the
; device UNASKED and the next partition mounted gets the question. The
; buffer is a 2 KB DMA claim for the moment it takes. Rung 1 (the task
; file) needs none of this: the drive walks its own geometry.
; -----------------------------------------------------------------------------
hd_cylprobe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov al, [hd_sel]
    call hd_dev_row             ; DI = the device
    cmp byte [di+HDD_KIND], HDK_BIOS
    jne .out
    test byte [di+HDD_FLAGS], HDF_CYLASK
    jnz .out
    mov ax, [si+HP_LBA]         ; the sector before the boot sector...
    mov dx, [si+HP_LBA+2]
    sub ax, 1
    sbb dx, 0
    jc .out
    mov [hd_lba], ax
    mov [hd_lba+2], dx
    call hd_chs
    jc .out
    mov ax, [hd_sec]            ; ...must END a track...
    cmp ax, [di+HDD_SPT]
    jne .out
    mov ax, [hd_head]           ; ...that is not the cylinder's last
    inc ax
    cmp ax, [di+HDD_HEADS]
    jae .out
    or byte [di+HDD_FLAGS], HDF_CYLASK  ; asked, from here on, whatever it says
    push dx
    mov ax, 2
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA    ; DX = 2 KB, DMA-safe
    mov si, dx
    pop dx
    jc .out
    mov es, si
    xor bx, bx                  ; THE TRUTH: two sectors, a call a track
    mov byte [hd_rawop], 0
    mov cx, 2
    mov ax, [hd_lba]
    call hd_raw                 ; (it walks hd_lba on; DX:AX is the start)
    jc .free
    mov [hd_lba], ax            ; THE QUESTION: the same two in one call
    mov [hd_lba+2], dx
    call hd_chs
    push di
    mov di, 1024 + 512          ; its second sector PATTERNED first: a read
    mov ax, 0xE5A6              ; that stops short leaves this
    mov cx, 256
    cld
    rep stosw
    pop di
    mov bx, 1024
    mov ax, 0x0202
    call hd_chs_regs
    int 0x13
    jnc .cmp
    xor ah, ah                  ; refused: reset, and the answer is no
    int 0x13
    jmp short .free
.cmp:
    push di
    push es
    pop ds
    xor si, si                  ; the two truth sectors must DIFFER, or the
    mov di, 512                 ; comparison proves nothing...
    mov cx, 256
    repe cmpsw
    mov al, 0                   ; (AL = the answer; `mov` keeps the flags)
    je .ans
    xor si, si                  ; ...and the one call must equal them
    mov di, 1024
    mov cx, 512
    repe cmpsw
    jne .ans
    inc ax
.ans:
    pop di
    pop ds                      ; ours again, from the frame
    push ds
    or al, al
    jz .free
    or byte [di+HDD_FLAGS], HDF_CYLOK
.free:
    mov dx, es
    call OSAPI_MEM_FREE
.out:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; hd_lst_reloc lived here - the owner's half of a DONATED listing claim's move
; (SPEC.md 66.5.10.2), a by-value walk of hd_vols patching HDV_LSEG. SPEC.md
; 22.6 retired the donation, so this driver holds no claim on the kernel's
; behalf and declares nothing movable.

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
; hd_vol_find - the next live hd_vols row on one device (and partition)
; in:  BX = the row to start AT (hd_vols for the first), DL = the device
;      index, DH = the partition slot or 0FFh for any slot
; out: CF = 0 and BX = the row; CF = 1 = none from BX on
; clobbers: BX (the output), flags
;
; ONE WALK, where there were five: hd_already, hd_vol_of, hd_dev_mounted,
; hd_unmount and hd_unmount_part each carried their own copy of this loop,
; and a scan that stops early in one of them is the kind of thing that stops
; being true quietly. The two that act on EVERY match take the row back, step
; past it and ask again.
; -----------------------------------------------------------------------------
hd_vol_find:
    cmp bx, hd_vols + HD_MAXVOL * HDV_SIZE
    jae .no
    cmp byte [bx+HDV_USED], 0
    je .next
    cmp [bx+HDV_DEV], dl
    jne .next
    cmp dh, 0xFF                ; any slot: equal, so CF = 0 on the way out
    je .yes
    cmp [bx+HDV_PART], dh       ; ...and so is this one when it matches
    je .yes
.next:
    add bx, HDV_SIZE
    jmp short hd_vol_find
.no:
    stc
.yes:
    ret

; -----------------------------------------------------------------------------
; hd_unmount - take the selected device's volumes away
; in:  nothing; the lock is HELD
; out: nothing
;
; EVERY volume this device has, not the first: Mount put up one icon per FAT
; partition and Unmount is the other half of that.
; -----------------------------------------------------------------------------
hd_unmount:
    push ax
    mov al, [hd_sel]
    mov ah, 0xFF                ; every slot
    call hd_unmount_part
    mov byte [hd_msgc], HDM_UNMOUNTED
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_unmount_part - drop every volume mounted on one partition (SPEC.md 52.12)
; in:  AL = the device index, AH = the partition slot, or 0FFh for every slot
;      (hd_unmount's)
; out: AL = 1 if anything was unmounted, 0 if not; AH = the LAST kernel volume
;      index dropped, 0FFh = none (SPEC.md 52.10.8 needs WHICH volume went and
;      not only that one did)
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
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    mov ax, 0xFF00              ; nothing yet
    mov bx, hd_vols
.scan:
    call hd_vol_find
    jc .out
    mov al, 1
    mov ah, [bx+HDV_VOL]        ; read while the row is still live
    call hd_unmount_row
    add bx, HDV_SIZE
    jmp short .scan
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_unmount_row - drop one volume (module internal)
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
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    mov bx, hd_vols
    call hd_vol_find
    mov al, 0xFF
    jc .out
    mov al, [bx+HDV_VOL]
.out:
    pop dx
    pop bx
    ret

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
; DX:BX (SPEC.md 51.8). It is the transfer's own direction byte: hd_bop IS
; hd_rawop in this image, so there is nothing to copy.
; -----------------------------------------------------------------------------
hd_raw:
    mov [hd_lba], ax
    mov [hd_lba+2], dx
    mov [hd_bofs], bx
    mov [hd_bcnt], cx
    mov [hd_bseg], es
    push ax
    call hd_xfer
    pop ax
    ret
