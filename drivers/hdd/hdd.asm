; =============================================================================
; os8088 - HDD.DRV
;
; The loadable hard-disk driver (SPEC.md 51.8/52): MFM through an XT
; controller card's own ROM, and CHS-only IDE. Everything the user can see is
; here - the Control Panel's Hard Drive page, the disk tool that partitions
; and formats, and the Mount button - because the kernel's half of this is four
; API slots and a volume table, and nothing above them belongs in a kernel
; that boots machines with no hard disk at all.
;
; WHAT THE KERNEL KEEPS AND WHY. A volume is a small index with 16-bit
; VOLUME-RELATIVE LBAs (SPEC.md 18.7), so the FAT layer, the directory
; walker and the write path are the floppy's, unchanged, and a partition is
; capped at 65,535 sectors - 31.99MB, which is exactly the DOS 3.3 limit
; these machines ran. This driver holds the 32-bit partition base and adds
; it, which is the whole of what "partitions" means to os8088.
;
; THE TRANSPORT LADDER (SPEC.md 52.1), in probe order:
;
;   int 13h, drive 80h/81h   AH=08h answers and LBA 0 reads. This is the ST-11M
;                            and every other XT/AT card with a ROM, and IDE on
;                            any machine whose BIOS knows the drive. It is
;                            FIRST on purpose: the machine whose BIOS already
;                            knows a drive is the machine whose partition
;                            table, geometry and drive-parameter block were
;                            all written to agree with that BIOS.
;   IDE task file 1F0h/170h  for a drive the BIOS does not know. 16-BIT BUS
;                            ONLY - an 8088's `in ax, dx` is two byte reads at
;                            the same address and the drive's high byte is
;                            gone, which is what XT-IDE adapters latch - so
;                            this rung is gated on OSAPI_CPU_INFO >= CPU_286.
;                            An 8088 with a hard disk has a controller with a
;                            ROM in it, and rung 0 is that ROM.
;
; GEOMETRY the driver could not determine is the USER's to type in: the page
; carries an editable C/H/S triple and recomputes the size from it live. That
; is not an exotic path - it is a 286 with an early IDE drive and a CMOS type
; table that predates it.
;
; Prefix hd_. Sub-modules: part.inc (the partition table and the allocator),
; fmt.inc (the FAT formatter), tool.inc (the one window both of those are
; reached through), page.inc (the Control Panel page). All near procs with
; near rets, like any package (SPEC.md 51.1).
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Hard Drive', DRVC_DISK, hd_entry

HD_MAXDEV    equ 4              ; devices the probe will report
HD_MAXVOL    equ 4              ; volumes mountable at once: ONE PER FAT
                                ; PARTITION, because four primaries is the
                                ; whole of what a partition table holds and
                                ; each is its own drive letter and its own
                                ; desktop icon (SPEC.md 52.4). It matches the
                                ; kernel's own spare rows (DVOL_MAX - 2)

; --- one device row, stride HDD_SIZE ----------------------------------------
HDD_KIND     equ 0              ; db: HDK_*
HDD_UNIT     equ 1              ; db: int 13h drive (80h/81h), or 0/1 on the
                                ; IDE channel in HDD_BASE
HDD_FLAGS    equ 2              ; db: bit 0 = geometry known
HDD_PAD      equ 3
HDD_CYL      equ 4              ; dw: cylinders
HDD_HEADS    equ 6              ; dw: heads      } 1..255
HDD_SPT      equ 8              ; dw: sectors    } 1..63
HDD_BASE     equ 10             ; dw: the IDE task file's base port
HDD_SIZE     equ 12

HDK_NONE     equ 0
HDK_BIOS     equ 1              ; int 13h (an ST-11M's ROM, or an AT BIOS)
HDK_IDE      equ 2              ; the task file, directly

; What [hd_mbrok] says about hd_mbr. HMB_BAD and HMB_NONE are NOT the same
; answer: one means the disk has no partitions, the other means we do not know
; what it has, and only the first makes writing a blank table safe.
HMB_BAD      equ 0              ; the read failed - hd_mbr is a fabrication
HMB_OK       equ 1              ; it came off the disk with a signature
HMB_NONE     equ 2              ; it read fine and is unpartitioned

; --- one mounted volume, stride HDV_SIZE ------------------------------------
HDV_USED     equ 0              ; db: 1 = live
HDV_DEV      equ 1              ; db: which device row
HDV_VOL      equ 2              ; db: the kernel's volume index
HDV_PART     equ 3              ; db: which partition slot (0..3)
HDV_BASE     equ 4              ; dd: its first LBA on the device
HDV_SECS     equ 8              ; dw: its length, <= 65,535 by construction
HDV_LSEG     equ 10             ; dw: the listing claim, 0 = none
HDV_VOLH     equ 12             ; db: OUR handle for it - the row's own index,
                                ; which is what comes back in DSV_BLK's AH
HDV_SIZE     equ 16

; --- IDE task file offsets from HDD_BASE ------------------------------------
IDE_DATA     equ 0
IDE_ERR      equ 1
IDE_SCNT     equ 2
IDE_SNUM     equ 3
IDE_CYLL     equ 4
IDE_CYLH     equ 5
IDE_DRVH     equ 6
IDE_STAT     equ 7              ; read: status; write: command
IDE_ST_BSY   equ 0x80
IDE_ST_DRDY  equ 0x40
IDE_ST_DRQ   equ 0x08
IDE_ST_DWF   equ 0x20             ; drive write fault: only a write can set it
IDE_ST_ERR   equ 0x01
IDE_C_READ   equ 0x20
IDE_C_WRITE  equ 0x30
IDE_C_IDENT  equ 0xEC
IDE_C_INITP  equ 0x91

%include "cfg.inc"
%include "part.inc"
%include "fmt.inc"
%include "tool.inc"
%include "inst.inc"
%include "page.inc"

; =============================================================================
; The entry proc: attach, detach, and the service table they publish
; =============================================================================

; -----------------------------------------------------------------------------
; hd_entry - attach / detach (SPEC.md 51.2)
; in:  AL = DRVV_*; DS = CS = ours, ES = KERNEL_SEG
; out: attach: CF = 0 and SI = the service table, or CF = 1 = no hardware
;      detach: nothing
;
; ATTACH IS ALL-OR-NOTHING and this one has an easy time of it: the probe
; writes no port that is not a read-back of a drive's own task file, claims
; no memory, and hooks no interrupt. Everything expensive - a listing claim,
; a mounted volume, a window - happens on a Control Panel click, and detach
; gives all of it back.
; -----------------------------------------------------------------------------
hd_entry:
    cmp al, DRVV_DETACH
    je hd_detach
    cmp al, DRVV_READY
    je hd_ready
    cmp al, DRVV_ATTACH
    jne .refuse                 ; DRVV_TIER means nothing here: this driver
                                ; has one tier and its refusal changes
                                ; nothing, which is the contract (SPEC.md
                                ; 51.2)
    call hd_state_init
    call hd_probe               ; fills hd_devs; CF = 1 = nothing answered
    jc .refuse
    mov si, hd_services
    clc
    ret
.refuse:
    mov al, DRVE_HW             ; the reason, explicitly: attach's refusal may
    stc                         ; carry a DRVE_* now (SPEC.md 51.2), and this
    ret                         ; path is reached with AL = the verb

; -----------------------------------------------------------------------------
; hd_ready - the kernel can take our calls now (SPEC.md 51.2.2/52.6)
; in:  nothing
; out: nothing
;
; The earliest point at which OSAPI_VOL_* will answer us: their fence is the
; publication slot and attach runs before it is armed. So this - and not
; attach - is where the settings file is read and where a drive that was
; mounted last session is mounted again.
; -----------------------------------------------------------------------------
hd_ready:
    call hd_cfg_load            ; geometry the user typed, and what was up
    call hd_cfg_automount
    clc
    ret

; -----------------------------------------------------------------------------
; hd_detach - give everything back (SPEC.md 51.2). Cannot fail.
; in:  nothing
; out: nothing
;
; Volumes first, then their listing claims, then the windows - and the
; windows LAST because a destroy repaints, and a repaint of the desktop walks
; the volume table. The kernel frees this image the moment we return.
; -----------------------------------------------------------------------------
hd_detach:
    push ax
    push bx
    push cx
    call hd_cfg_mark            ; a geometry typed and not yet acted on is
                                ; still worth keeping (SPEC.md 52.6). Staging
                                ; is all there is, and it is what this path
                                ; wanted anyway: the fence is still open (the
                                ; kernel releases the class AFTER this returns)
                                ; and the panel that unticked us is about to
                                ; close and write the file
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.vol:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_VOL]
    call OSAPI_VOL_DEL
    mov dx, [bx+HDV_LSEG]
    or dx, dx
    jz .noseg
    call OSAPI_MEM_FREE
.noseg:
    mov byte [bx+HDV_USED], 0
.next:
    add bx, HDV_SIZE
    loop .vol
    call hd_win_close_all       ; the partitioner and the formatter
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_state_init - zero what has to start zero (module internal)
; in:  nothing
; out: nothing (all registers preserved)
;
; A driver's data ships INSIDE its image, zero-filled on the floppy, so this
; is only needed for the things a re-attach must reset - and a re-attach is
; an ordinary Control Panel click.
; -----------------------------------------------------------------------------
hd_state_init:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    cld
    mov di, hd_devs
    mov cx, HD_MAXDEV * HDD_SIZE
    xor al, al
    rep stosb
    mov di, hd_vols
    mov cx, HD_MAXVOL * HDV_SIZE
    rep stosb
    mov byte [hd_ndev], 0
    mov byte [hd_sel], 0
    mov byte [hd_field], 0
    mov byte [hd_wantmnt], 0
    mov word [hd_twin], 0
    mov word [hd_msg], hd_s_pick
    pop es
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; The probe (SPEC.md 52.1)
; =============================================================================

; -----------------------------------------------------------------------------
; hd_probe - find every hard disk this machine will let us reach
; in:  nothing
; out: CF = 0 and [hd_ndev] > 0; CF = 1 = no hard disk at all
; clobbers: flags
;
; Rung 0 first, for every drive the BIOS admits to. Rung 1 then adds any IDE
; device the task file answers for and the BIOS did NOT already report - so a
; machine whose BIOS knows its disk gets one row for it, not two.
; -----------------------------------------------------------------------------
hd_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov byte [hd_ndev], 0

    ; --- rung 0: what the BIOS says -----------------------------------------
    ; int 13h AH=08h on 80h and 81h. DL comes back as the NUMBER of fixed
    ; disks the BIOS knows, which is the honest bound - and a card whose ROM
    ; hooked int 13h is exactly as authoritative here as an AT BIOS.
    mov byte [hd_pdl], 0x80     ; the drive number lives in MEMORY across
.bios:                          ; the probe: hd_bios_geom answers in AX, CX
    mov dl, [hd_pdl]            ; and DX, so there is no register free to
    call hd_bios_geom           ; hold it and the stack would have to be
    jc .bios_next               ; unwound on two paths
    call hd_dev_new             ; DI = a fresh row, CF = 1 = table full
    jc .ide
    mov byte [di+HDD_KIND], HDK_BIOS
    call hd_geom_store          ; AX/CX/DX -> the row, and the known flag
    mov al, [hd_pdl]
    mov [di+HDD_UNIT], al
.bios_next:
    inc byte [hd_pdl]
    cmp byte [hd_pdl], 0x82
    jb .bios

    ; --- rung 1: the IDE task file ------------------------------------------
    ; 16-bit bus only, and that is not caution: `in ax, dx` on an 8-bit bus is
    ; two byte reads at the same port and the drive's high byte is lost. An
    ; 8088 with a hard disk has a controller with a ROM, and that is rung 0.
.ide:
    call OSAPI_CPU_INFO         ; AL = CPU_*
    cmp al, CPU_286
    jb .done
    mov bx, 0x1F0
    call hd_ide_channel
    mov bx, 0x170
    call hd_ide_channel
.done:
    cmp byte [hd_ndev], 0
    je .none
    clc
    jmp short .out
.none:
    stc
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
; hd_bios_geom - int 13h AH=08h, decoded (module internal)
; in:  DL = 80h or 81h
; out: CF = 0 and CX = cylinders, DX = heads, AX = sectors/track;
;      CF = 1 = the BIOS does not know this drive
; clobbers: AX, CX, DX (the outputs), flags
;
; The returned values are MAXIMA and zero-based for the head count, which is
; the classic off-by-one in this call. A geometry with no sectors or no heads
; is a BIOS saying no in a way that does not set CF - some XT ROMs do exactly
; that for the second drive - so both are tested.
; -----------------------------------------------------------------------------
hd_bios_geom:
    push bx
    push si
    push di
    push es
    push dx
    mov ah, 0x08
    int 0x13
    jc .no
    ; CH = cyl low, CL bits 6-7 = cyl high, CL bits 0-5 = sectors,
    ; DH = max head
    mov al, cl
    and al, 0x3F                ; AL = sectors/track
    test al, al
    jz .no
    mov bl, al                  ; BL = sectors/track, banked
    mov al, cl
    and al, 0xC0                ; the cylinder's top two bits, in CL 6..7...
    mov cl, 6
    shr al, cl                  ; ...down to 0..3
    mov ah, al                  ; AH = cylinder bits 8..9
    mov al, ch                  ; AL = cylinder bits 0..7, so AX is the MAX
    mov cx, ax
    inc cx                      ; ...and the count is one more than the max
    mov al, dh
    xor ah, ah
    inc ax                      ; AX = heads
    mov dx, ax
    mov al, bl
    xor ah, ah                  ; AX = sectors/track
    test dx, dx
    jz .no
    pop bx                      ; the caller's DL, discarded: CX/DX/AX are
    pop es                      ; the outputs
    pop di
    pop si
    pop bx
    clc
    ret
.no:
    pop dx
    pop es
    pop di
    pop si
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ide_channel - probe both drives on one IDE channel (module internal)
; in:  BX = the task file's base port
; out: nothing (rows appended to hd_devs)
; clobbers: flags
; -----------------------------------------------------------------------------
hd_ide_channel:
    push ax
    push cx
    push dx
    push di
    xor cl, cl                  ; CL = drive 0 / 1
.drive:
    call hd_ide_ident           ; CF = 0 and hd_idbuf holds IDENTIFY's answer
    jc .next
    call hd_dev_new
    jc .out
    mov byte [di+HDD_KIND], HDK_IDE
    mov [di+HDD_UNIT], cl
    mov [di+HDD_BASE], bx
    mov ax, [hd_idbuf + 1*2]    ; word 1: cylinders
    mov cx, ax
    mov ax, [hd_idbuf + 3*2]    ; word 3: heads
    mov dx, ax
    mov ax, [hd_idbuf + 6*2]    ; word 6: sectors per track
    call hd_geom_store
    call hd_ide_setparams       ; tell the drive the geometry we will use -
                                ; without it every read lands somewhere else
    mov cl, [di+HDD_UNIT]
.next:
    inc cl
    cmp cl, 2
    jb .drive
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_dev_new - the next free device row (module internal)
; in:  nothing
; out: CF = 0 and DI = the row; CF = 1 = the table is full
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_dev_new:
    push ax
    mov al, [hd_ndev]
    cmp al, HD_MAXDEV
    jae .full
    call hd_dev_row
    inc byte [hd_ndev]
    pop ax
    clc
    ret
.full:
    pop ax
    stc
    ret

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
; hd_geom_store - put a probed geometry in a row (module internal)
; in:  DI = the row, CX = cylinders, DX = heads, AX = sectors/track
; out: nothing; HDD_FLAGS bit 0 set when all three are usable
; clobbers: flags
;
; A geometry is KNOWN only when every field is inside what CHS addressing can
; carry - 1..1024 cylinders, 1..255 heads, 1..63 sectors. Anything else is
; the user's to type in, which is an ordinary state and not a failure.
; -----------------------------------------------------------------------------
hd_geom_store:
    mov [di+HDD_CYL], cx
    mov [di+HDD_HEADS], dx
    mov [di+HDD_SPT], ax
    and byte [di+HDD_FLAGS], 0xFE
    call hd_geom_ok
    jc .out
    or byte [di+HDD_FLAGS], 1
.out:
    ret

; -----------------------------------------------------------------------------
; hd_geom_ok - is a row's geometry usable? (module internal)
; in:  DI = the row
; out: CF = 0 usable
; clobbers: flags
; -----------------------------------------------------------------------------
hd_geom_ok:
    push ax
    mov ax, [di+HDD_CYL]
    test ax, ax
    jz .no
    cmp ax, 1024
    ja .no
    mov ax, [di+HDD_HEADS]
    test ax, ax
    jz .no
    cmp ax, 255
    ja .no
    cmp byte [di+HDD_KIND], HDK_IDE
    jne .spt
    cmp ax, 16                  ; the task file has four bits of head, so an
    ja .no                      ; IDE row carrying more is not a drive we can
.spt:                           ; address at all - refuse the GEOMETRY rather
                                ; than truncate it at the port (SPEC.md 52.1)
    mov ax, [di+HDD_SPT]
    test ax, ax
    jz .no
    cmp ax, 63
    ja .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_dev_mb - a device's size in whole MB
; in:  DI = the row
; out: AX = megabytes (0 if the geometry is unknown)
; clobbers: AX (the output), flags
;
; cyl * heads * spt sectors, and 2,048 sectors is a megabyte - so the answer
; is (cyl*heads*spt) >> 11, computed as a 32-bit product because 1024 x 16 x
; 63 is 1,032,192 and does not fit a word.
; -----------------------------------------------------------------------------
hd_dev_mb:
    push bx
    push cx
    push dx
    call hd_geom_ok
    jc .none
    mov ax, [di+HDD_HEADS]
    mul word [di+HDD_SPT]       ; DX:AX = heads*spt (<= 16,065)
    mul word [di+HDD_CYL]       ; ...times cylinders: a true 32-bit product
                                ; only because the first fits a word
    mov bx, ax                  ; DX:BX = total sectors
    mov cl, 11
.shift:                         ; >> 11, a bit at a time: the 8086 has no
    shr dx, 1                   ; 32-bit shift and this runs once a repaint
    rcr bx, 1
    dec cl
    jnz .shift
    mov ax, bx
    jmp short .out
.none:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The block transport (SPEC.md 51.8) - what the kernel calls for every sector
; =============================================================================

; -----------------------------------------------------------------------------
; hd_blk - DSV_BLK
; in:  AL = 0 read / 1 write, AH = our volume handle, SI = the VOLUME-RELATIVE
;      LBA, CX = sectors, DX:BX = the buffer
; out: CF = 0 done; CF = 1 and AL = an int 13h status byte
; clobbers: AX (the output), flags
;
; The whole of what "partitions" means to os8088 is the addition below: the
; kernel hands a 16-bit volume-relative LBA and this adds the partition's
; 32-bit base. Everything above it - the FAT, the directory, the write path -
; is the floppy's code, unchanged.
; -----------------------------------------------------------------------------
hd_blk:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov [hd_bseg], dx
    mov [hd_bofs], bx
    mov [hd_bcnt], cx
    mov [hd_bop], al

    mov al, ah                  ; AH = our handle = the hd_vols row index
    call hd_vol_row             ; BX = the volume row
    jc .bad
    mov di, bx

    mov ax, si                  ; the 32-bit LBA: base + volume-relative
    xor dx, dx
    add ax, [di+HDV_BASE]
    adc dx, [di+HDV_BASE+2]
    mov [hd_lba], ax
    mov [hd_lba+2], dx

    mov al, [di+HDV_DEV]
    call hd_dev_row             ; DI = the device row
    cmp byte [di+HDD_KIND], HDK_IDE
    je .ide
    call hd_bios_xfer
    jmp short .done
.ide:
    call hd_ide_xfer
.done:
    jc .fail
    xor al, al
    clc
    jmp short .out
.bad:
    mov al, 0x04                ; "sector not found": the honest answer for a
.fail:                          ; handle that names no volume
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_vol_row - a volume handle's row
; in:  AL = handle (0..HD_MAXVOL-1)
; out: CF = 0 and BX = the row (live); CF = 1 otherwise
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
hd_vol_row:
    push ax
    cmp al, HD_MAXVOL
    jae .bad
    mov ah, HDV_SIZE
    mul ah
    add ax, hd_vols
    mov bx, ax
    cmp byte [bx+HDV_USED], 0
    je .bad
    pop ax
    clc
    ret
.bad:
    xor bx, bx
    pop ax
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
; hd_bios_xfer - rung 0's transfer (module internal)
; in:  DI = the device row, [hd_lba], [hd_bcnt], [hd_bseg]:[hd_bofs], [hd_bop]
; out: CF = 0 done, else CF = 1 and AL = the int 13h status
; clobbers: AX (the output), flags
;
; ONE SECTOR PER CALL, exactly like disk.inc's floppy path and for the same
; two reasons: a track boundary never matters, and a transfer that starts
; 512-aligned can never straddle a 64KB DMA page. Three attempts with an
; AH=00h reset between them.
; -----------------------------------------------------------------------------
hd_bios_xfer:
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov si, [hd_bcnt]
.run:
    or si, si
    jz .ok
    call hd_chs
    jc .fail
    call hd_bios_run            ; AX = sectors this one int 13h may carry
    jc .fail                    ; ...or none of them can: an unaligned buffer
    mov [hd_run], ax            ; whose next sector would straddle a DMA page
    mov bp, 3
.attempt:
    mov es, [hd_bseg]           ; reloaded per attempt: the buffer walks a
    mov bx, [hd_bofs]           ; segment RUN, and a retry must re-aim
    mov ah, 0x02
    cmp byte [hd_bop], 0
    je .go
    mov ah, 0x03
.go:
    mov al, [hd_run]
    call hd_chs_regs
    int 0x13
    jnc .next
    mov [hd_status], ah
    mov ah, 0x00                ; reset the controller and try again
    mov dl, [di+HDD_UNIT]
    int 0x13
    dec bp
    jnz .attempt
    mov al, [hd_status]
    jmp short .fail
.next:
    mov cx, [hd_run]            ; hd_buf_step is per sector, and the LBA it
.step:                          ; walks is what hd_chs reads next time round
    call hd_buf_step
    dec si
    loop .step
    jmp short .run
.ok:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.fail:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_bios_run - how many sectors one int 13h may carry from here (internal)
; in:  DI = the device row, SI = sectors still wanted, [hd_sec], [hd_bseg],
;      [hd_bofs]
; out: CF = 0 and AX = 1..127; CF = 1 = this buffer cannot be transferred at all
; clobbers: AX (the output), flags
;
; Rung 0 batches too, but it has two bounds the task file does not, and both
; are the BIOS's rather than the drive's:
;
;   - a CHS int 13h MUST NOT CROSS A TRACK. The drive would walk into the
;     next head itself; the BIOS call does not, and the classic answer is
;     status 04h or a silent short read. So the run stops at spt.
;   - the 8237 never carries into the page port, so a DMA transfer must end
;     inside the 64KB page it starts in - dskw_runmax's rule, applied here
;     because the kernel bounds only the runs IT issues and this rung splits
;     them again.
;
; One sector always fits both PROVIDED the buffer is 512-aligned, which is what
; guard 6 is for and what the `align 512` on hd_mbr/hd_sec0 buys. hd_raw stores
; ES:BX verbatim and hd_buf_step carries only at 16-bit overflow, so nothing
; here folds an offset into its segment the way dskw_norm does - the caller's
; alignment IS the guarantee. An unaligned buffer can leave zero sectors before
; the page boundary, and that is a refusal (CF=1), not a forced run of one.
; -----------------------------------------------------------------------------
hd_bios_run:
    push cx
    push dx
    mov ax, [di+HDD_SPT]        ; sectors left in this track, [hd_sec] being
    sub ax, [hd_sec]            ; 1-based
    inc ax
    cmp ax, si
    jbe .page
    mov ax, si
.page:
    mov dx, [hd_bseg]
    mov cl, 4
    shl dx, cl
    add dx, [hd_bofs]           ; DX = the offset within the 64KB DMA page
    neg dx
    jz .out                     ; a zero offset means the whole page: no cap
    mov cl, 9
    shr dx, cl                  ; ...as sectors
    cmp ax, dx
    jbe .out
    mov ax, dx
.out:
    or ax, ax                   ; A bound of zero cannot happen for a buffer
    jnz .cap                    ; that is 512-aligned, which every int 13h
                                ; target in this driver now is. It CAN happen
                                ; for one that is not, and forcing a run of 1
                                ; there issues exactly the straddling transfer
                                ; the cap exists to prevent - so this is a
                                ; refusal now, and a future misalignment fails
                                ; visibly instead of one heap layout in 64
    stc
    pop dx
    pop cx
    ret
.cap:
    cmp ax, 127
    jbe .done
    mov ax, 127
.done:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_chs_regs - [hd_cyl]/[hd_head]/[hd_sec] into int 13h's register shape
; in:  DI = the device row; AH/AL already hold the function and count
; out: CX, DH, DL loaded
; clobbers: CX, DX, flags
;
; CH = cylinder bits 0..7, CL bits 6..7 = cylinder bits 8..9, CL bits 0..5 =
; the 1-based sector. The two high bits go up six places one shift at a time
; because an 8086 has no shift by an immediate count.
; -----------------------------------------------------------------------------
hd_chs_regs:
    mov cx, [hd_cyl]
    mov ch, cl                  ; CH = cylinder low 8
    mov cl, [hd_cyl+1]          ; 0..3
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    shl cl, 1
    and cl, 0xC0
    or cl, [hd_sec]
    mov dh, [hd_head]
    mov dl, [di+HDD_UNIT]
    ret

; -----------------------------------------------------------------------------
; hd_buf_step - advance the buffer and the LBA by one sector (internal)
; in:  [hd_bofs]/[hd_bseg]/[hd_lba]
; out: nothing (all registers preserved)
;
; The offset carries into the SEGMENT rather than wrapping, which is the same
; arithmetic dskw_norm does inside the kernel (SPEC.md 18.4.1): 512 bytes is
; 32 paragraphs, so a wrapped offset means a whole 64KB has been crossed.
; -----------------------------------------------------------------------------
hd_buf_step:
    push ax
    add word [hd_bofs], 512
    jnc .noseg
    mov ax, [hd_bseg]
    add ax, 0x1000
    mov [hd_bseg], ax
.noseg:
    add word [hd_lba], 1
    jnc .out
    inc word [hd_lba+2]
.out:
    pop ax
    ret

; =============================================================================
; The IDE task file (SPEC.md 52.1) - CHS only, polled PIO, no interrupt
; =============================================================================

; -----------------------------------------------------------------------------
; hd_ide_wait - poll for BSY clear, bounded (module internal)
; in:  BX = the base port
; out: CF = 0 and AL = the status; CF = 1 = it never came ready
; clobbers: AX (the output), flags
;
; BOUNDED, always. The one way to hang a machine here is to wait forever for
; a bit that never changes on a machine where every read is 0FFh - the exact
; bug Linux shipped until v5.11 - so this counts out instead.
; -----------------------------------------------------------------------------
hd_ide_wait:
    push cx
    push dx
    mov dx, bx
    add dx, IDE_STAT
    xor cx, cx                  ; 65,536 reads: ~0.2s on a 286, and the only
.poll:                          ; thing that matters is that it ENDS
    in al, dx
    test al, IDE_ST_BSY
    jz .ready
    loop .poll
    pop dx
    pop cx
    stc
    ret
.ready:
    pop dx
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; hd_ide_drq - wait for BSY clear AND DRQ set (module internal)
; in:  BX = the base port
; out: CF = 0 ready for a sector's worth of data; CF = 1 = timeout or error
; clobbers: AX, flags
; -----------------------------------------------------------------------------
hd_ide_drq:
    push cx
    push dx
    mov dx, bx
    add dx, IDE_STAT
    xor cx, cx
.poll:
    in al, dx
    test al, IDE_ST_BSY
    jnz .again
    test al, IDE_ST_ERR
    jnz .err
    test al, IDE_ST_DRQ
    jnz .ready
.again:
    loop .poll
.err:
    pop dx
    pop cx
    stc
    ret
.ready:
    pop dx
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; hd_ide_select - point the task file at one drive (module internal)
; in:  BX = base, CL = drive 0/1, CH = head (0..15)
; out: CF = 0 selected and ready
; clobbers: AX, flags
; -----------------------------------------------------------------------------
hd_ide_select:
    push dx
    mov dx, bx
    add dx, IDE_DRVH
    mov al, cl
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1                   ; drive into bit 4
    and al, 0x10
    or al, 0xA0                 ; the two bits ATA-1 pins high
    mov ah, ch
    and ah, 0x0F                ; ...and the head in bits 0..3. MASKED: head 16
    or al, ah                   ; would set bit 4 and address the OTHER drive on
    out dx, al                  ; the channel, and head 64 would set LBA mode.
                                ; hd_chs refuses such a geometry before we get
                                ; here - this is the last line, not the only one
    pop dx
    call hd_ide_wait
    ret

; -----------------------------------------------------------------------------
; hd_ide_ident - IDENTIFY DEVICE into hd_idbuf (module internal)
; in:  BX = base, CL = drive 0/1
; out: CF = 0 and hd_idbuf holds 256 words; CF = 1 = no such drive
; clobbers: AX, flags
;
; A drive that predates ATA-1 answers ABRT here, and that is not a failure -
; it is exactly the machine the page's manual geometry exists for. It is
; refused as a DEVICE, though: without IDENTIFY there is nothing to tell us a
; drive is even present, and inventing one would put a row on the page for a
; controller that is not there.
; -----------------------------------------------------------------------------
hd_ide_ident:
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    mov ch, 0
    call hd_ide_select
    jc .no
    cmp al, 0xFF                ; a floating bus reads as all ones
    je .no
    test al, IDE_ST_DRDY
    jz .no
    mov dx, bx
    add dx, IDE_STAT
    mov al, IDE_C_IDENT
    out dx, al
    call hd_ide_drq
    jc .no
    mov dx, bx
    add dx, IDE_DATA
    mov di, hd_idbuf
    mov cx, 256
    cld
.read:
    in ax, dx                   ; 16-bit, and the reason this rung is gated
    stosw                       ; on a 16-bit bus (SPEC.md 52.1)
    loop .read
    pop es
    pop di
    pop dx
    pop cx
    clc
    ret
.no:
    pop es
    pop di
    pop dx
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ide_setparams - INITIALIZE DEVICE PARAMETERS (module internal)
; in:  DI = the device row
; out: nothing
; clobbers: AX, flags
;
; NOT OPTIONAL. The geometry a drive is addressed with has to be the geometry
; it was told about, or every read lands somewhere else.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; hd_geom_push - re-teach an IDE drive a geometry that changed after the probe
; in:  DI = the device row
; clobbers: flags
;
; INITIALIZE DEVICE PARAMETERS is issued once, from hd_ide_channel, with the
; geometry IDENTIFY reported. Anything that overwrites HDD_HEADS/HDD_SPT
; afterwards - the Control Panel's fields, or a geometry restored out of
; SYSTEM.CFG before the first paint - leaves the drive translating with one
; geometry while hd_chs translates with another, and every later transfer then
; addresses a physical sector nobody asked for. So a write of the geometry is
; a write to the DRIVE too, and a drive that refuses the parameters loses its
; usable bit rather than being addressed with a translation it rejected.
;
; A cylinder-only edit is harmless - 91h carries heads-1 and SPT and nothing
; else - but re-issuing on one costs a command and keeps the rule simple.
; -----------------------------------------------------------------------------
hd_geom_push:
    cmp byte [di+HDD_KIND], HDK_IDE
    jne .out                    ; the BIOS rung translates for us
    test byte [di+HDD_FLAGS], 1
    jz .out                     ; the geometry was refused already
    push ax
    call hd_ide_setparams
    jc .bad
    test al, IDE_ST_ERR
    jnz .bad
    pop ax
.out:
    ret
.bad:
    and byte [di+HDD_FLAGS], 0xFE   ; unusable: better no disk than the wrong
    pop ax                          ; sectors on a real one
    ret

hd_ide_setparams:
    push bx
    push cx
    push dx
    mov bx, [di+HDD_BASE]
    mov cl, [di+HDD_UNIT]
    mov ch, [di+HDD_HEADS]
    dec ch                      ; the head field is a MAXIMUM
    and ch, 0x0F
    call hd_ide_select
    jc .out
    mov dx, bx
    add dx, IDE_SCNT
    mov al, [di+HDD_SPT]
    out dx, al
    mov dx, bx
    add dx, IDE_STAT
    mov al, IDE_C_INITP
    out dx, al
    call hd_ide_wait
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_ide_xfer - rung 1's transfer (module internal)
; in:  DI = the device row, [hd_lba], [hd_bcnt], [hd_bseg]:[hd_bofs], [hd_bop]
; out: CF = 0 done, else CF = 1 and AL = a status byte
; clobbers: AX (the output), flags
;
; ONE COMMAND PER RUN, and one DRQ handshake per sector inside it. ATA-1's
; sector-count register is what makes that legal: the drive walks
; sector/head/cylinder itself, so a run may cross tracks and cylinders freely
; and the only cap is the register's 255 (0 would mean 256, which nothing
; here asks for).
;
; It used to be one command per sector, on the reasoning that dsk_xfer looped
; int 13h anyway so a run bought nothing. That stopped being true when a
; driver-backed volume started handing the whole count to DSV_BLK in one call
; (SPEC.md 18.7): a command is a task-file write, a BSY poll and a DRQ poll,
; and paying that per 512 bytes is most of what a copy costs on a real drive.
; Measured on the reference copy (docs/HDD-PLAN.md part 13): 1,918 commands
; became 141.
;
; No DMA page bound here, unlike the BIOS rung: PIO moves every byte through
; the CPU, and hd_buf_step carries the offset into the segment.
;
; The PIO loop is `in ax, dx` / `stosw` because this tree is cpu 8086 and
; `rep insw` is a 186 instruction - a 286-and-up machine could emit its two
; opcode bytes by hand, and that is an optimisation for a day when someone
; has measured it.
; -----------------------------------------------------------------------------
hd_ide_xfer:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp                     ; BP is NOT in this routine's clobber list, and
                                ; hd_bios_xfer saves it - the two rungs have to
                                ; agree or a caller holding a pointer in BP
                                ; across a mount gets it back as a device row
    mov si, [hd_bcnt]
    mov bp, di                  ; BP = the device row: DI is the PIO loop's
    mov bx, [di+HDD_BASE]
.run:
    or si, si
    jz .ok
    push bp
    pop di                      ; DI = the row again, for hd_chs
    call hd_chs                 ; the CHS of the run's FIRST sector
    jc .fail
    cmp word [hd_head], 15      ; the task file has FOUR bits of head, and
    ja .fail                    ; hd_chs is shared with the BIOS rung, which
                                ; legitimately carries 255 of them in DH. An
                                ; IDE geometry that cannot be expressed is a
                                ; refusal here, not a silent truncation into
                                ; the drive-select bit (SPEC.md 52.1)
    mov cl, [di+HDD_UNIT]
    mov ch, [hd_head]
    call hd_ide_select
    jc .fail
    mov ax, si                  ; the whole remainder in one command, up to
    cmp ax, 255                 ; what the count register holds
    jbe .n
    mov ax, 255
.n:
    mov [hd_run], ax
    mov dx, bx
    add dx, IDE_SCNT
    out dx, al                  ; AL = the run: 1..255
    inc dx                      ; IDE_SNUM
    mov al, [hd_sec]
    out dx, al
    inc dx                      ; IDE_CYLL
    mov al, [hd_cyl]
    out dx, al
    inc dx                      ; IDE_CYLH
    mov al, [hd_cyl+1]
    out dx, al
    mov dx, bx
    add dx, IDE_STAT
    mov al, IDE_C_READ
    cmp byte [hd_bop], 0
    je .cmd
    mov al, IDE_C_WRITE
.cmd:
    out dx, al
.sector:                        ; one DRQ handshake per sector, inside the
    call hd_ide_drq             ; one command above
    jc .fail
    mov dx, bx                  ; IDE_DATA
    mov cx, 256
    mov es, [hd_bseg]
    mov di, [hd_bofs]
    cld
    cmp byte [hd_bop], 0
    jne .write
.read:
    in ax, dx                   ; 16-bit, and the reason this rung is gated
    stosw                       ; on a 16-bit bus (SPEC.md 52.1)
    loop .read
    jmp short .advance
.write:
    mov ax, [es:di]
    out dx, ax
    inc di
    inc di
    loop .write
.advance:
    call hd_buf_step
    dec si
    dec word [hd_run]
    jnz .sector
                                ; The command is finished. hd_ide_drq tests ERR
                                ; at the TOP of each sector, which catches a
                                ; READ (a failed read never asserts DRQ) and
                                ; catches NOTHING on a WRITE: the last sector's
                                ; data is already shifted out and its completion
                                ; status has never been looked at. Every write
                                ; in this driver is one sector, so without this
                                ; a write fault is reported to the kernel as
                                ; success - and the commit order in SPEC.md
                                ; 18.4.1 is only safe while a failed data write
                                ; comes back as a failure.
    call hd_ide_wait
    jc .fail
    test al, IDE_ST_ERR | IDE_ST_DWF
    jnz .failst
    jmp .run
.ok:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.failst:
    mov al, 0x0A                ; "bad sector detected" - the drive said so,
    jmp short .failout          ; rather than our own generic 04h
.fail:
    mov al, 0x04
.failout:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; The service table (SPEC.md 51.2)
; =============================================================================
hd_services:
    dw 0                        ; DSV_CAPS    - sound's
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw 0                        ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw hd_blk                   ; DSV_BLK
    dw hd_s_page                ; DSV_CPNAME  - and so the page exists
    dw hd_page_paint            ; DSV_CPPAINT
    dw hd_page_click            ; DSV_CPCLICK

hd_s_page:   db 'Hard Drive', 0

; =============================================================================
; Data. A driver has no bss - its zeroed data ships inside the image
; (SPEC.md 51.1), which is what lets the kernel make exactly one claim, at
; the size the directory entry already reported, before a byte is read.
; =============================================================================
hd_ndev:     db 0
hd_sel:      db 0               ; the device row the page is showing
hd_field:    db 0               ; the C/H/S field the page has selected
hd_msg:      dw 0               ; -> the page's caption, always something

hd_devs:     times HD_MAXDEV * HDD_SIZE db 0
hd_vols:     times HD_MAXVOL * HDV_SIZE db 0

hd_lba:      dd 0               ; the transfer's 32-bit LBA
hd_bseg:     dw 0               ; ...and its buffer, count and direction
hd_bofs:     dw 0
hd_bcnt:     dw 0
hd_bop:      db 0
hd_run:      dw 0               ; sectors in the transfer being issued
hd_status:   db 0
hd_pdl:      db 0               ; hd_probe's int 13h drive number
hd_cyl:      dw 0               ; hd_chs's answer
hd_head:     dw 0
hd_sec:      dw 0

hd_idbuf:    times 512 db 0     ; IDENTIFY's 256 words (PIO, never DMA)
hd_rawop:    db 0               ; hd_raw's direction: the LBA needs DX:AX and
                                ; the buffer ES:BX, so there is no register
                                ; left for it (SPEC.md 51.8's own reason)

; ALIGNED, and the alignment is guard 6's, not tidiness. Both of these are
; int 13h transfer targets on the HDK_BIOS rung, and int 13h moves one sector
; per call but does NOT stop that sector straddling a 64KB physical boundary -
; only starting 512-aligned does, and the DMA controller answers a straddle
; with error 09h. hd_raw stores ES:BX verbatim with no dskw_norm-style fold,
; and a driver's image is an ordinary heap claim whose base is 1KB-aligned,
; so the residue these offsets carry is the whole of the protection.
    align 512
hd_mbr:      times 512 db 0     ; the partition table being edited
    align 512
hd_sec0:     times 512 db 0     ; the formatter's sector under construction
hd_mbrok:    db 0               ; HMB_*: whether hd_mbr is the DISK's table
hd_line:     times 64 db 0      ; one line of text under construction
hd_lineptr:  dw 0               ; ...and where hd_scat/hd_utoa left off
hd_rowslot:  db 0               ; the page's and the partitioner's loop index
hd_rowdev:   db 0
hd_pslot:    db 0               ; the partition hd_mount is working on
hd_wantmnt:  db 0               ; bit n = device n was mounted last session
hd_selsave:  db 0               ; hd_cfg_automount's saved selection
hd_cwd:      dw 0               ; the volume+directory the automount borrowed...
hd_cdrv:     db 0               ; ...and must put back
hd_cfgi:     db 0               ; hd_cfg_build's loop index and record count
hd_cfgn:     db 0
hd_cfgbuf:   times HDC_FBUF db 0     ; the blob, staged for OSAPI_DRV_CFG
hd_nmnt:     db 0               ; ...and how many it has mounted this click
hd_cap:      times 32 db 0      ; the page's caption, when it is BUILT rather
                                ; than pointed at a literal. Its own buffer:
                                ; hd_line is rewritten by every device row on
                                ; the way past, and a caption outlives the
                                ; paint that put it up
hd_fldi:     db 0               ; the C/H/S editor's loop index
hd_fldx:     dw 0
hd_clx:      dw 0               ; the click being dispatched
hd_cly:      dw 0
hd_twin:     dw 0               ; the disk tool's window, 0 = never opened
hd_idev:     db 0               ; ...and the installer's, for the same reason
hd_tdev:     db 0               ; the DEVICE the tool is bound to, banked when
                                ; it opened. hd_mbr and hd_tstate describe THIS
                                ; device; [hd_sel] is the Control Panel's
                                ; highlight and the panel stays clickable
                                ; underneath, so a write that reads [hd_sel]
                                ; can commit one disk's table to another's
                                ; LBA 0. Everything destructive reads this
hd_tsel:     db 0               ; ...the slot it has selected,
hd_tarm:     db 0               ; the ARMED ACTION when a destructive button is
                                ; one click from happening, 0 when none is:
                                ; slot+1 in the low nibble and HTA_DEL for
                                ; which button, so Format and Delete cannot arm
                                ; each other (52.2.3)
hd_tdrop:    db 0               ; a Delete unmounted a volume, so the caption
                                ; says so and the mounted set is re-staged
hd_tmsg:     dw 0               ; ...and its caption
hd_tstate:   times 4 db 0       ; HTS_* per slot, worked out when the tool
                                ; opens and after every format
hd_ask:      times 40 db 0      ; the confirm question, built into a buffer of
                                ; its own: hd_line belongs to the row painter
hd_xslot:    db 0               ; hd_slot_extent's scan: the slot being placed,
hd_xstart:   dd 0               ; the candidate start,
hd_xlimit:   dd 0               ; how far it may run,
hd_xend:     dd 0               ; one entry's end,
hd_xbest:    dd 0               ; and the largest hole the walk has seen
hd_xblen:    dd 0               ; (52.2.1)
hd_fbase:    dd 0               ; the extent a format will write: its first LBA
hd_fsecs:    dw 0               ; ...and its length
hd_fspc:     dw 0               ; the plan: sectors per cluster,
hd_ffatsz:   dw 0               ; FAT sectors,
hd_fclus:    dw 0               ; clusters,
hd_fdata:    dw 0               ; the first data sector,
hd_ftype:    db 0               ; and 0 = FAT12 / 1 = FAT16

    OS88_DRV_END
