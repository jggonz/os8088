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
; hd_mount - mount every FAT volume on the selected device: the four primary
;            slots, then the logical drives of its extended partition
; in:  nothing; the gfx lock is HELD (a page click)
; out: nothing; [hd_msgc] (an HDM_*) says what happened
; clobbers: flags
;
; Neither half claims anything: SPEC.md 22.6 retired the listing donation, so
; a mounted volume costs this driver a hd_vols row and no heap.
;
; **The extended partition is walked AFTER the four primaries** (SPEC.md
; 52.4.1), because the walk reads each extended boot record into hd_mbr - the
; buffer the primary loop is still reading entries out of. Its slot is banked
; on the way past and the chain is hd_mount_chain's.
; -----------------------------------------------------------------------------
hd_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov byte [hd_nmnt], 0
    mov byte [hd_msgc], HDM_MNOTAB
    mov byte [hd_xslot], 0xFF   ; no extended partition seen yet
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load
    jc .out                     ; no readable table: nothing to mount

    mov byte [hd_msgc], HDM_MNOFAT
    mov byte [hd_pslot], 0
.slot:
    mov al, [hd_pslot]
    call hd_part_ent
    call hd_part_isext
    jc .prim
    cmp byte [hd_xslot], 0xFF   ; the FIRST extended entry and only it: DOS
    jne .next                   ; writes one per disk, and a second would be
    mov [hd_xslot], al          ; a second chain over the same sectors
    jmp short .next
.prim:
    call hd_mount_slot
.next:
    inc byte [hd_pslot]
    cmp byte [hd_pslot], 4
    jb .slot

    cmp byte [hd_xslot], 0xFF
    je .count
    call hd_mount_chain         ; the logical drives (SPEC.md 52.4.1)
.count:
    cmp byte [hd_nmnt], 0
    je .out                     ; the caption is whichever refusal got here
    mov byte [hd_msgc], HDM_MOUNTED ; 'Mounted n volume(s)': a CODE, and the
                                ; count is [hd_nmnt], which the page's renderer
                                ; reads anyway (hddabi.inc's rule about a
                                ; caption being a pointer only in the image
                                ; that letters it)
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount_slot - mount one entry if it is a FAT volume nobody has (internal)
; in:  SI = the entry, with its LBA ABSOLUTE on the device; [hd_pslot] = its
;      slot (0..3 primary, 4 and up a logical drive, SPEC.md 52.4.1); DI = the
;      device row; the gfx lock is HELD
; out: [hd_nmnt] / [hd_msgc] as hd_mount_one leaves them; every register kept
; -----------------------------------------------------------------------------
hd_mount_slot:
    push ax
    push bx
    push dx
    call hd_part_isfat
    jc .out                     ; free, or a type that is not a FAT volume:
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
    jc .mount                   ; loaded at all (SPEC.md 52.10.3). Mounting it
                                ; again is a second icon on the same sectors.
                                ; It is hdcom.inc's and takes the DEVICE as an
                                ; argument, because the installer asks the
                                ; same question of a device of its own
                                ; (SPEC.md 52.10.4.1)
    cmp byte [di+HDD_KIND], HDK_IDE ; ...unless rung 1 TOOK this drive from
    jne .have                   ; the BIOS (52.1.1) - the one IDE row hd_kvol
                                ; can say yes about: then the kernel's volume
                                ; is handed to us as it is, by hd_mount_one,
                                ; which asks hd_kvol again
.mount:
    call hd_mount_one
    jmp short .out
.have:
    mov byte [hd_msgc], HDM_MHAVE   ; and SAY so: the caption was still
                                    ; 'No FAT partition on it' from the top of
                                    ; the loop, which about a partition that is
                                    ; mounted and on the desktop is the one
                                    ; answer that is not true
    jmp short .out
.big:
    mov byte [hd_msgc], HDM_MBIG    ; the tool calls this slot Unmountable and
                                    ; so does the page, rather than reporting
                                    ; 'no FAT partition' about a partition that
                                    ; plainly is one (SPEC.md 52.2.2)
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount_chain - walk the extended partition's chain and mount its logical
;                  drives (SPEC.md 52.4.1)
; in:  [hd_xslot] = the extended partition's slot, hd_mbr still holding the
;      table it is in; DI = the device row; the gfx lock is HELD
; out: [hd_nmnt] / [hd_msgc] as hd_mount_slot leaves them; hd_mbr holds the
;      last record read
; clobbers: AX, BX, CX, DX, SI, flags (DI kept); ES = DS on the way out
;
; Each extended boot record is a partition table of its own, read into hd_mbr:
; entry 0 is the logical drive, its LBA relative to THIS record; entry 1 is the
; link to the next record, its LBA relative to the EXTENDED PARTITION's start.
; Two different bases, and that is the DOS format, not a choice.
;
; **The chain comes off a disk, so it is hostile** (SPEC.md 18.2's rule), and
; three things bound it. A link must point FORWARD, past the record it is in,
; so no record is read twice and no drive mounted twice; a chain written out
; of disk order stops at its first backward link, a drive missing rather than
; a volume doubled. HD_MAXLOG links and no more, whatever they say. And a
; logical drive must END inside the extended partition, so it cannot alias the
; partitions outside it - a link that points outside reads a record whose
; drive cannot pass that test. The rest is the mount's: a drive whose start
; lands on its own record reads that record as a boot sector, which BPB rule
; 2 refuses. The walk also stops at a full volume table - every record past
; it is a read that can mount nothing.
; -----------------------------------------------------------------------------
hd_mount_chain:
    push di
    mov al, [hd_xslot]
    call hd_part_ent            ; SI = the entry, still in hd_mbr
    add si, HP_LBA              ; its base and length, the walk's frame: hd_mbr
    mov di, hd_xbase            ; is about to stop holding them
    push ds
    pop es
    movsw
    movsw
    movsw
    movsw
    pop di
    xor ax, ax
    cwd                         ; DX:AX = 0: the first record is the
    mov byte [hd_pslot], 4      ; partition's own first sector
.ebr:
    mov [hd_xcur], ax
    mov [hd_xcur+2], dx
    add ax, [hd_xbase]
    adc dx, [hd_xbase+2]
    mov bx, hd_mbr
    mov cx, 1
    mov [hd_rawop], ch          ; a read
    call hd_raw
    jc .end                     ; an unreadable record ends the chain
    cmp word [hd_mbr+510], 0xAA55
    jne .end
    mov si, hd_mbr + HP_TBL     ; entry 0: the logical drive
    call hd_part_used
    jc .link                    ; a record with no drive is legal: a link only
    mov ax, [si+HP_LBA]
    mov dx, [si+HP_LBA+2]
    add ax, [hd_xcur]           ; -> its start, relative to the partition
    adc dx, [hd_xcur+2]
    jc .link
    mov cx, ax
    mov bx, dx
    add ax, [si+HP_SECS]        ; -> its end
    adc dx, [si+HP_SECS+2]
    jc .link
    cmp [hd_xlen+2], dx         ; CF = 1 = past the partition's end
    jne .inside
    cmp [hd_xlen], ax
.inside:
    jc .link
    add cx, [hd_xbase]          ; absolute, in place: hd_mount_one reads the
    adc bx, [hd_xbase+2]        ; entry's LBA as the device's
    mov [si+HP_LBA], cx
    mov [si+HP_LBA+2], bx
    call hd_mount_slot
    cmp byte [hd_msgc], HDM_MFULL
    je .end
.link:
    mov si, hd_mbr + HP_TBL + HP_ESZ
    call hd_part_isext
    jc .end                     ; the last drive in the chain
    mov ax, [si+HP_LBA]
    mov dx, [si+HP_LBA+2]
    cmp dx, [hd_xcur+2]         ; THE CHAIN MUST MOVE FORWARD: a link at or
    jb .end                     ; behind the record it is in would read a
    ja .fwd                     ; record twice and mount its drive again,
    cmp ax, [hd_xcur]           ; under a new slot number
    jbe .end
.fwd:
    inc byte [hd_pslot]
    cmp byte [hd_pslot], 4 + HD_MAXLOG
    jb .ebr
.end:
    ret

; -----------------------------------------------------------------------------
; hd_part_isext - is this entry an extended partition? (SPEC.md 52.4.1)
; in:  SI = the entry
; out: CF = 0 yes (05h or 0Fh, with a non-zero length); CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
hd_part_isext:
    cmp byte [si+HP_TYPE], HPT_EXT
    je .used
    cmp byte [si+HP_TYPE], HPT_EXTL
    stc
    jne .out
.used:
    jmp hd_part_used            ; a typed entry with no length is free
.out:
    ret

; -----------------------------------------------------------------------------
; hd_mount_one - mount the partition [hd_pslot] names (module internal)
; in:  SI = its entry, [hd_sel] = the device; the gfx lock is HELD
; out: [hd_nmnt] incremented on success; [hd_msgc] set on a full table or a
;      refused add. CF = 1 there too, but a partition the mount rejects
;      leaves OSAPI_VOL_DEL's CF - hd_svc_mount's answer is hd_vol_of's
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
    cmp al, 4
    jb .parent                  ; a primary is its own parent...
    mov al, [hd_xslot]          ; ...a logical drive's is the extended slot
.parent:                        ; (SPEC.md 52.4.1)
    mov [bx+HDV_PARENT], al
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
    xor dx, dx                  ; DX = 0: a volume ADDED...
    mov al, [hd_sel]            ; ...unless it is THE KERNEL'S already: only
    call hd_kvol                ; on a drive rung 1 took (hd_mount skips it
    jc .add                     ; on any other), and then it is TAKEN -
    mov dl, al                  ; OSAPI_VOL_ADD with DL = the index hd_kvol
.add:                           ; answered, never 0 or 1 (SPEC.md 52.1.1)
    mov al, [bx+HDV_VOLH]       ; our own handle IS the row index
                                ; (no donated listing claim: the kernel's own
                                ; buffers, which is every volume now, 22.6)
    xor si, si                  ; no label: the KERNEL derives 'HDD C'..'HDD F'
    call OSAPI_VOL_ADD          ; from the volume index, which is the only
    jc .novol                   ; place that knows it (SPEC.md 26.1); a take
    mov [bx+HDV_VOL], al        ; reads neither CX nor SI
    mov byte [bx+HDV_USED], 1   ; AL is STILL the volume index - the reload
    or dx, dx                   ; that was here existed because the movable
    jnz .counted                ; declaration passed its proc through AX. A
    call OSAPI_VOL_MOUNT        ; TAKE is mounted already, and listed: it is
    jc .badvol                  ; not read again (DX survives the slot)
.counted:
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
    mov ax, 128                 ; 2 KB in paragraphs, all of it the
    mov cx, ax                  ; page-safe head
    mov bl, MEMP_CLAIM + MEMP_CONV
    call OSAPI_MEM_P            ; DX = 2 KB, DMA-safe
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
    stc
    jmp short .out
.free:                          ; (CF = 0 off the equal compare)
    mov [bx+HDV_VOLH], al
.out:
    pop cx
    pop ax
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
    cmp [bx+HDV_PARENT], dh     ; ...and a logical drive INSIDE the slot: the
    je .yes                     ; extended partition's volumes are its own,
                                ; so Delete and Format drop them with it
                                ; (SPEC.md 52.4.1)
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
; hd_vol_of - which kernel volume is mounted on one partition (SPEC.md 52.10.4.2)
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
    mov ax, es
    cmp ah, 0xF0                ; OUR OWN BUFFER, IN THE HMA (SPEC.md 2.10.8):
    jae .bounce                 ; hd_mbr is in this image, and hd_bios_run
    test al, 4                  ; ...or ANY kern_mega selector pair (KERN-
    jnz .bounce                 ; MEGA-MEMORY-PLAN MC7.4): above 1MB a
                                ; reflected int 13h is refused 09h, and below
                                ; it the image's paragraph is no longer what
                                ; it is named by. A real-mode image is 512-
                                ; aligned, so bit 2 is clear there
.go:
    call hd_xfer                ; refuses it there. Every other caller's
    pop ax                      ; buffer is in the heap
    ret
.bounce:                        ; ...so through a heap claim, which starts
    push bx                     ; 512-aligned, so a run splits only on a
    push cx                     ; sector boundary at a DMA page (hd_bios_run).
    push dx                     ; THE BUFFER IS THE CALLER'S ES:BX AND CX
    push si                     ; SECTORS OF IT - not ours, and not one: the
    push di                     ; tool's HSV_RAW hands its own image (hd_mbr,
    push es                     ; hd_sec0) and the installer's three-sector
    mov ax, 96                  ; FAT chunk (hd_kfat_get) through here, and on
    push bx                     ; kern_mega every one of those is a selector.
    mov bl, MEMP_CONV               ; A copy through DS:BX read HDD.DRV's own
    call OSAPI_MEM_P            ; image at the tool's offset - #GP 0 past its
    pop bx                      ; limit, the installer's Next on kern_mega
    jc .out                     ; (KERN-MEGA-MEMORY-PLAN MC7, as built). AX =
    mov [hd_bseg], dx           ; 96 PARAGRAPHS, the three sectors a raw
    xor ax, ax                  ; transfer is at most, BL = MEMP_CONV, and
                                ; CX - the sectors, 1..3 - is the HEAD: no
                                ; page boundary can split that few
                                ; paragraphs, and it marks the claim the
                                ; chip's. Out DX = its segment
    mov [hd_bofs], ax           ; ...at the claim's offset 0
    mov ch, cl                  ; CX = sectors x 256 words: a raw transfer is
    mov cl, al                  ; one to three sectors, well under 256
    push cx                     ; (kept for the way out)
    push di                     ; (the device row, which hd_xfer reads)
    push ds
    mov si, bx
    push es
    pop ds                      ; DS:SI = the caller's buffer
    mov es, dx
    xor di, di
    rep movsw                   ; in - which a read then overwrites (DF = 0,
    pop ds                      ; the system's contract, SPEC.md 1 rule 3)...
    pop di
    call hd_xfer
    pop cx
    pop es                      ; the caller's ES, read back off the stack
    push es                     ; and left there for .out (no flag moves)
    pushf
    mov di, bx
    push ds
    mov ds, dx
    xor si, si
    rep movsw                   ; ...and out: a write's own bytes, a read's
    pop ds                      ; answer (a failed one hd_part_load blanks)
    call OSAPI_MEM_FREE         ; DX
    popf
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
