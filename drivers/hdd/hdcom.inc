; =============================================================================
; os8088 - HDD.DRV / hdcom.inc
;
; Code and scratch that BOTH images need, assembled into each of them
; (SPEC.md 52.11). Nothing in here crosses the segment boundary: each image
; gets its own copy of these routines and its own copy of the data they work
; on, and that is the point - a far call per decimal digit would be absurd,
; and a shared line buffer would be a second thing to keep in step.
;
; What is here is everything that is a pure function of its arguments or of
; the caller's OWN state:
;
;   the device table       hd_devs / hd_ndev / hd_sel and hd_dev_row. The
;                          resident PROBES it; the tool is handed a copy by
;                          HSV_SYNC. Once each has one, hd_dev_row and hd_chs
;                          are ordinary near code on both sides
;   the partition table    hd_mbr and its read half - the load, the blank and
;                          the three entry accessors. Each image reads LBA 0
;                          for itself when it needs it, which is cheaper than
;                          sharing a 512-byte buffer across a far call and
;                          cannot go stale
;   CHS                    hd_chs and its three answers
;   text                   hd_line, hd_scat, hd_utoa - and note that the LINE
;                          BUFFER is per-image, which is right: the page draws
;                          its rows with the resident's and the tool draws its
;                          own with the tool's, and they never see each other
;
; hd_part_load calls hd_raw, and hd_raw is the ONE routine in here that is
; different in the two images - real transport in the resident, a far-call
; thunk in the tool (hdsvc.inc). It has the same register contract in both,
; which is exactly what lets everything above it be one copy of one source.
; =============================================================================

; --- the device table, and the scratch the shared routines work in ----------
hd_ndev:     db 0
hd_sel:      db 0               ; the device row the page (or the tool) shows
hd_devs:     times HD_MAXDEV * HDD_SIZE db 0

hd_rowslot:  db 0               ; a row painter's loop index
hd_line:     times 64 db 0      ; one line of text under construction
hd_lineptr:  dw 0               ; ...and where hd_scat/hd_utoa left off

hd_lba:      dd 0               ; the LBA hd_chs converts, and the one hd_raw
                                ; is carrying
hd_cyl:      dw 0               ; hd_chs's answer
hd_head:     dw 0
hd_sec:      dw 0
hd_rawop:    db 0               ; hd_raw's direction: the LBA needs DX:AX and
                                ; the buffer ES:BX, so there is no register
                                ; left for it (SPEC.md 51.8's own reason)

; -----------------------------------------------------------------------------
; hd_dev_row - a device index's row
; in:  AL = index
; out: DI = the row
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_dev_row:
    push ax
    push dx
    mov ah, HDD_SIZE
    mul ah
    add ax, hd_devs
    mov di, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_dev_row_ck - the same row, refused when the index names no device
; in:  AL = index
; out: CF = 0 with DI = the row; CF = 1 = no such device (DI untouched)
; clobbers: DI (the output), flags
;
; The service handlers branch on CF after a row lookup, and the index they
; look up arrived over the tool seam (SPEC.md 52.11.5) - a mismatched tool
; image is exactly what the seam's validation exists for. hd_dev_row itself
; stays unchecked because hd_dev_new must reach the row AT [hd_ndev] to fill
; it before the count is raised.
; -----------------------------------------------------------------------------
hd_dev_row_ck:
    cmp al, [hd_ndev]
    jae .no
    call hd_dev_row
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hd_chs - a 32-bit LBA as CHS, for the geometry in DI (module internal)
; in:  DI = the device row, [hd_lba] = the LBA
; out: CF = 0 and [hd_cyl]/[hd_head]/[hd_sec]; CF = 1 = past the drive
; clobbers: flags
;
; ONE `div` pair and no 32/16 two-step, and that is provable rather than
; lucky: spt*heads is at most 63*255, the highest LBA CHS can name is
; 1024*255*63, and the quotient - the cylinder - is under 1024. DX before
; each divide is therefore far below the divisor.
; -----------------------------------------------------------------------------
hd_chs:
    push ax
    push bx
    push cx
    push dx
    mov ax, [di+HDD_HEADS]
    mul word [di+HDD_SPT]
    mov cx, ax                  ; CX = spt*heads (<= 16,065)
    test dx, dx
    jnz .bad
    or cx, cx
    jz .bad
    mov ax, [hd_lba]
    mov dx, [hd_lba+2]
    cmp dx, cx
    jae .bad                    ; the quotient would not fit a word
    div cx                      ; AX = cylinder, DX = the rest
    cmp ax, 1024
    jae .bad
    mov [hd_cyl], ax
    mov ax, dx
    xor dx, dx
    div word [di+HDD_SPT]       ; AX = head, DX = sector-1
    mov [hd_head], ax
    inc dx
    mov [hd_sec], dx
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_part_load - read the selected device's partition table into hd_mbr
; in:  DI = the device row
; out: CF = 0 and hd_mbr holds a table with a valid signature;
;      CF = 1 = no usable table, and hd_mbr holds a BLANK one instead.
;      [hd_mbrok] says WHICH, and callers that write must look:
;        HMB_BAD  (0) the read FAILED - what is on the disk is unknown
;        HMB_OK   (1) it came off the disk with a signature
;        HMB_NONE (2) it read fine and nobody has partitioned it
; clobbers: flags
;
; A disk with no signature is not an error here - it is a disk nobody has
; partitioned - so the failure path leaves a table the user can edit rather
; than nothing at all. What it must never do is leave the PREVIOUS device's
; table in the buffer, which is why the blank happens before the read.
;
; HMB_BAD AND HMB_NONE USED TO BE THE SAME ANSWER, and that was a way to lose
; a disk: a marginal LBA 0 on a drive with three real partitions read as "not
; partitioned", every slot showed free, and a free slot skipped the confirm -
; so one click wrote this fabricated table over the live one and formatted
; through the data behind it. A blank table is a fine thing to EDIT and never
; a fine thing to COMMIT when we could not read what it replaces.
; -----------------------------------------------------------------------------
hd_part_load:
    push ax
    push bx
    push cx
    push dx
    push es
    call hd_mbr_blank
    push ds
    pop es
    mov bx, hd_mbr
    xor ax, ax
    xor dx, dx
    mov cx, 1
    mov byte [hd_rawop], 0      ; read
    call hd_raw
    jc .bad                     ; the READ failed: leave HMB_BAD
    cmp word [hd_mbr+510], 0xAA55
    jne .nosig
    mov byte [hd_mbrok], HMB_OK
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.nosig:                         ; read fine, nobody has partitioned it - the
    call hd_mbr_blank           ; blank IS the right answer, and committing it
    mov byte [hd_mbrok], HMB_NONE   ; destroys nothing
    jmp short .out
.bad:
    call hd_mbr_blank           ; leaves HMB_BAD: this table is a fabrication
.out:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_mbr_blank - a table with nothing in it (module internal)
; in:  nothing
; out: hd_mbr = 446 zero bytes, four empty entries and the signature
; clobbers: flags
;
; THE BOOT CODE IS NOT PUT HERE, and that is what keeps 446 bytes of
; chain-loader out of the resident image. This runs on the READ path - it is
; what hd_part_load blanks the buffer with before it reads, and what it leaves
; behind when the read finds no signature - and nothing that reads a partition
; table ever looks at bytes 0..445. Only a COMMIT cares what is in front of
; the entries, so hd_part_write stamps the loader in on its way to the disk
; (partw.inc), gated on the same [hd_mbrok] that says whether those bytes are
; ours to write over. The behaviour is unchanged: what used to survive here
; from the blank now arrives there at the write.
; -----------------------------------------------------------------------------
hd_mbr_blank:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, hd_mbr
    mov cx, 256
    xor ax, ax
    rep stosw
    mov word [hd_mbr+510], 0xAA55
    mov byte [hd_mbrok], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_part_ent - one partition entry's address
; in:  AL = slot (0..3)
; out: SI = the entry
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
hd_part_ent:
    push ax
    push dx
    mov ah, HP_ESZ
    mul ah
    add ax, hd_mbr + HP_TBL
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_kvol - is this partition already a volume THE KERNEL carries?
; in:  AL = the device index, SI = the partition entry
; out: CF = 0 = yes, and AL = the volume index; CF = 1 = no
; clobbers: AL, flags
;
; **The kernel gets there first on an installed machine.** `dsk_boot_from`
; adopts the partition the BIOS booted from before any driver loads, because
; HDD.DRV is a file on that very volume (SPEC.md 52.10.3) - so C: is live,
; ours, and invisible to everything this driver keeps: `hd_vols` holds the
; rows THIS driver registered, and that adoption is by construction not one
; of them. `OSAPI_VOL_AT` is the half we cannot see, and it is the only
; unfenced slot of the OSAPI_VOL_* family precisely so THIS file can call it
; - hdcom.inc is assembled into both images, and the tool's segment is not
; the published disk driver's, so a fenced slot would refuse it.
;
; It is here rather than in either caller because the two ask the same
; question for opposite reasons and must not drift: `hd_mount` skips a
; partition that is already on the desktop (52.10.3.1), and `hd_iw_scan`
; refuses one as an install TARGET (52.10.4.1). One is a duplicate icon and
; the other is formatting the volume the machine is running from.
;
; **Only a BIOS row can be it.** The kernel's transport there is int 13h
; (rung 0), so a device reached through the task file is by definition not
; the drive the adoption named - and there is no int 13h number to offer for
; one anyway. An IDE row therefore answers "not the kernel's", which is right
; for the drive the BIOS does not know.
; -----------------------------------------------------------------------------
hd_kvol:
    push bx
    push cx
    push dx
    push di
    call hd_dev_row
    cmp byte [di+HDD_KIND], HDK_BIOS
    jne .no
    mov dl, [di+HDD_UNIT]       ; the int 13h drive, and the partition base as
    mov cx, [si+HP_LBA]         ; the kernel keeps it: 32 bits, high word in BX
    mov bx, [si+HP_LBA+2]
    call OSAPI_VOL_AT           ; preserves everything but AL
    jc .no
    pop di
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop di
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_part_used - is a slot in use?
; in:  SI = the entry
; out: CF = 0 in use (a known type and a non-zero length)
; clobbers: flags
; -----------------------------------------------------------------------------
hd_part_used:
    cmp byte [si+HP_TYPE], HPT_NONE
    je .no
    cmp word [si+HP_SECS], 0
    jne .yes
    cmp word [si+HP_SECS+2], 0
    jne .yes
.no:
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; hd_part_isfat - is this slot a FAT volume we could mount?
; in:  SI = the entry
; out: CF = 0 yes; CF = 1 = free, or a type that is not FAT
; clobbers: flags
;
; The TYPE byte, and the type byte only: whether the volume inside is really
; a FAT one is the mount's question, and `osapi_vol_mount` answering CF=1 is
; how a partition that lies gets dropped again (SPEC.md 52.4). Extended
; (05h/0Fh) and FAT32 (0Bh/0Ch) are deliberately not here - the first is a
; chain this driver does not walk and the second is a format this kernel
; does not read.
; -----------------------------------------------------------------------------
hd_part_isfat:
    push ax
    call hd_part_used
    jc .no
    mov al, [si+HP_TYPE]
    cmp al, HPT_FAT12
    je .yes
    cmp al, HPT_FAT16S
    je .yes
    cmp al, HPT_FAT16B
    je .yes
    cmp al, HPT_FAT16L
    je .yes
.no:
    pop ax
    stc
    ret
.yes:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_win_erase - white-fill one of our windows' CONTENT, and nothing else
; in:  BX = the window; the gfx lock is HELD
; out: nothing (all registers preserved)
;
; **The size comes from OSAPI_WM_GEOM, never from the template's own
; constants**, and that is not caution - it is a bug this had. A window's
; CONTENT is `W_W - 2` by `W_H - TITLE_H - 1`, and TITLE_H is EIGHTEEN; a
; repaint that open-coded `HPW_H - 14` filled six rows past the bottom of the
; content, straight through the window's border and its drop shadow and into
; whatever was behind it. The kernel's primitives clip to the SCREEN and not
; to a window (SPEC.md 5), so nothing downstream was ever going to catch it.
;
; The frame size is not the answer either, even with the right subtraction:
; wm_fit clamps a template that does not fit the live screen (SPEC.md 39.7),
; so the record - not the template - is the truth. Asking is the only thing
; that is right on all three adapters.
; -----------------------------------------------------------------------------
hd_win_erase:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_WM_GEOM          ; CX = content width, DX = content height
    jc .out                     ; not visible: nothing to erase
    mov si, cx
    mov di, dx
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov bx, dx
    mov cx, ax
    add cx, si
    dec cx                      ; x2 = left + width - 1
    mov dx, bx
    add dx, di
    dec dx                      ; y2 = top + height - 1
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop dx
    pop cx
    pop bx
    pop ax
    call OSAPI_GFX_FILL
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_scat - append the NUL string at SI to DI, leaving DI past it
; in:  SI = source, DI = destination
; out: DI advanced to the NUL (which is not written)
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_scat:
    push ax
    push si
.next:
    mov al, [si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    jmp short .next
.done:
    mov [hd_lineptr], di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_utoa / hd_utoa3 / hd_utoa4 - AX as decimal at DI, DI advanced
; in:  AX = the value, DI = the destination
; out: DI past the last digit
; clobbers: DI (the output), flags
;
; The padded forms right-align a column of numbers without a second pass, and
; a value too wide for its field simply prints wide - a geometry the user can
; see is worth more than a column that lines up.
; -----------------------------------------------------------------------------
hd_utoa:
    push cx
    xor cx, cx
    jmp short hd_utoa_body
hd_utoa3:
    push cx
    mov cx, 3
    jmp short hd_utoa_body
hd_utoa4:
    push cx
    mov cx, 4
hd_utoa_body:
    push ax
    push bx
    push dx
    push si
    mov si, cx                  ; SI = the width wanted. It CANNOT stay on
    mov bx, 10                  ; the stack: the digits go there next, and
    xor cx, cx                  ; the pop would take the last of them
.divide:
    xor dx, dx
    div bx
    push dx                     ; the digits, least significant first
    inc cx
    or ax, ax
    jnz .divide
    cmp si, cx
    jbe .emit
    sub si, cx                  ; pad with leading spaces
.pad:
    mov byte [di], ' '
    inc di
    dec si
    jnz .pad
.emit:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .emit
    mov [hd_lineptr], di
    pop si
    pop dx
    pop bx
    pop ax
    pop cx
    ret


; --- strings both images letter ---------------------------------------------
hd_s_mb:     db 'M', 0
hd_s_close:  db 'Close', 0
hd_s_two:    db '  ', 0
