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

    OS88_DRIVER 'Hard Drive', DRVC_DISK, hd_entry, hd_svc_end - hd_services






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

%include "hddabi.inc"
%include "hdcom.inc"
%include "cfg.inc"
%include "hdtool.inc"
%include "mount.inc"

; =============================================================================
; The entry proc: attach, detach, and the service table they publish
; =============================================================================

; -----------------------------------------------------------------------------
; hd_entry - attach / ready / detach (SPEC.md 51.2)
; in:  AL = DRVV_*; DS = CS = ours, ES = KERNEL_SEG
; out: attach: CF = 0 and SI = the service table, or CF = 1 = no hardware
;      ready, detach: nothing
;
; ATTACH AND READY ARE IN hd_mbr's BYTES (SPEC.md 52.13.6): each is sent once,
; to an image read off the disk a moment before, and both have finished
; before anything reads a partition table into those bytes. This dispatch and
; the refusal stay out here, because DRVV_DETACH comes at any time.
; -----------------------------------------------------------------------------
hd_entry:
    cmp al, DRVV_DETACH
    je hd_detach
    cmp al, DRVV_READY
    je .ready
    cmp al, DRVV_ATTACH         ; DRVV_TIER means nothing here: this driver
    jne .refuse                 ; has one tier and its refusal changes
    jmp hd_attach               ; nothing, which is the contract (51.2)
.ready:
    jmp hd_ready
.refuse:
    mov al, DRVE_HW             ; the reason, explicitly: attach's refusal may
    stc                         ; carry a DRVE_* now (SPEC.md 51.2), and this
    ret                         ; path is reached with AL = the verb

; -----------------------------------------------------------------------------
; hd_detach - give everything back (SPEC.md 51.2). Cannot fail.
; in:  nothing
; out: nothing
;
; Volumes first, then the tool's windows - and the windows LAST because a
; destroy repaints, and a repaint of the desktop walks the volume table. The
; kernel frees this image the moment we return.
; -----------------------------------------------------------------------------
hd_detach:
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
    call hd_unmount_row         ; a no-op on a row that is not live
    add bx, HDV_SIZE
    loop .vol
    call hd_tool_drop           ; HDT_SHUT and then the free, in that order. A
                                ; window of the TOOL's left on screen is a
                                ; paint through freed memory twice over - the
                                ; kernel releases this image the moment we
                                ; return, and we release the tool's before that
    pop cx
    pop bx
    ret

                                ; hd_geom_ok and hd_dev_mb MOVED to
                                ; hdcom.inc - both are pure functions
                                ; of a device row and the PAGE needs
                                ; them, so they belong in the file
                                ; both images compile (SPEC.md 52.11)

; =============================================================================
; The block transport (SPEC.md 51.8) - what the kernel calls for every sector
; =============================================================================

; -----------------------------------------------------------------------------
; hd_blk - DSV_BLK
; in:  AL = 0 read / 1 write, AH = our volume handle, DI:SI = the
;      VOLUME-RELATIVE LBA (DI the high word, SPEC.md 18.7.5), CX = sectors,
;      DX:BX = the buffer
; out: CF = 0 done; CF = 1 and AL = an int 13h status byte
; clobbers: AX (the output), flags
;
; The whole of what "partitions" means to os8088 is the addition below: the
; kernel hands a volume-relative LBA and this adds the partition's 32-bit
; base. The LBA was 16 bits until SPEC.md 18.7.5, and a kernel that predates
; it leaves DI = the low word - which this must never meet, the two shipping
; together. Everything above it - the FAT, the directory, the write path -
; is the floppy's code, unchanged.
;
; CX, SI and ES are never written here and hd_xfer keeps everything but AX,
; so only the four this routine spends are saved.
; -----------------------------------------------------------------------------
hd_blk:
    cmp al, 2                   ; SPEC.md 51.8: sub-function 2 is GEOM, which
    je hd_geom                  ; wants none of the transfer frame below
    push bx
    push dx
    push di
    push bp
    mov bp, di                  ; the LBA's high word (SPEC.md 18.7.5), before
                                ; DI becomes the volume row below
    mov [hd_bseg], dx
    mov [hd_bofs], bx
    mov [hd_bcnt], cx
    mov [hd_bop], al

    mov al, ah                  ; AH = our handle = the hd_vols row index
    call hd_vol_row             ; BX = the volume row
    jc .bad
    mov di, bx

    mov ax, si                  ; the transfer's END, volume-relative, against
    mov dx, bp                  ; the partition's length: past 32MB the kernel
    add ax, cx                  ; was told 'unknown' and its rule 13 bounds
    adc dx, 0                   ; nothing, so a BPB that overstates its size
    jc .bad                     ; would reach the NEXT partition - this is the
    cmp dx, [di+HDV_LEN+2]      ; one party that knows where it ends
    ja .bad                     ; (SPEC.md 18.7.5)
    jb .inlen
    cmp ax, [di+HDV_LEN]
    ja .bad
.inlen:
    mov ax, si                  ; the 32-bit LBA: base + volume-relative, whose
    mov dx, bp                  ; HIGH word the kernel hands in DI (SPEC.md
    add ax, [di+HDV_BASE]       ; 18.7.5) - banked in BP at the top
    adc dx, [di+HDV_BASE+2]
    mov [hd_lba], ax
    mov [hd_lba+2], dx

    mov al, [di+HDV_DEV]
    call hd_dev_row             ; DI = the device row
    call hd_xfer                ; CF, and AL = 0 or the status
    jmp short .out
.bad:
    mov al, 0x04                ; "sector not found": the honest answer for a
    stc                         ; handle that names no volume, and for a
.out:                           ; sector past the partition's end
    pop bp
    pop di
    pop dx
    pop bx
    ret

%ifdef INSTBENCH
; -----------------------------------------------------------------------------
; hd_bn_hit - one command about to be issued, carrying [hd_run] sectors
; preserves every register and the flags
;
; Called from BOTH rungs, immediately before the command goes out, so it
; counts what the DRIVE was actually asked to do rather than what the kernel
; asked the driver for - which are different numbers by construction: rung 0
; splits at the track and the DMA page, rung 1 at ATA-1's 255 (SPEC.md 52.1).
; -----------------------------------------------------------------------------
hd_bn_hit:
    pushf
    push ax
    inc word [hd_bn_cal]
    mov ax, [hd_run]
    add [hd_bn_sec], ax
    pop ax
    popf
    ret
%endif

; -----------------------------------------------------------------------------
; hd_geom - DSV_GEOM (SPEC.md 51.8, 87.5)
; in:  AH = our volume handle
; out: CF = 0 and DL = the int 13h drive, CX:BX = the partition's first LBA,
;      SI = sectors per track, DI = heads; CF = 1 no such volume, or a device
;      on rung 1 (the task file), which the ROM cannot read for the caller
; clobbers: the outputs, flags
; -----------------------------------------------------------------------------
hd_geom:
    push ax
    mov al, ah
    call hd_vol_row             ; BX = the volume row
    jc .no
    mov si, bx
    mov al, [si+HDV_DEV]
    call hd_dev_row             ; DI = its device
    cmp byte [di+HDD_KIND], HDK_BIOS
    jne .no
    mov dl, [di+HDD_UNIT]
    mov bx, [si+HDV_BASE]
    mov cx, [si+HDV_BASE+2]
    mov ax, [di+HDD_SPT]
    mov di, [di+HDD_HEADS]
    mov si, ax
    pop ax
    clc
    ret
.no:
    pop ax
    stc
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
; hd_xfer - one transfer to a DEVICE, on whichever rung it is reached by
; in:  DI = the device row, [hd_lba], [hd_bcnt], [hd_bseg]:[hd_bofs], [hd_bop]
; out: CF = 0 and AL = 0 done; CF = 1 and AL = an int 13h status byte
; clobbers: AX (the output), flags
;
; hd_blk and hd_raw both come through here, and the two rungs share this
; frame and its one epilogue - which is also what makes them agree about BP:
; it is NOT in the clobber list, and a caller holding a pointer in BP across a
; mount must get it back rather than a device row.
;
; RUNG 0, int 13h. A track boundary never matters and a transfer that starts
; 512-aligned can never straddle a 64KB DMA page (hd_bios_run is the cap);
; three attempts with an AH=00h reset between them.
; -----------------------------------------------------------------------------
hd_xfer:
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push di
    mov si, [hd_bcnt]
    cmp byte [di+HDD_KIND], HDK_IDE
    je hd_xfer_ide
.run:
    or si, si
    jz hd_xfer_ok
    call hd_chs
    jc hd_xfer_fail4
    call hd_bios_run            ; AX = sectors this one int 13h may carry
    jc hd_xfer_fail4            ; ...or none of them can: an unaligned buffer
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
%ifdef INSTBENCH
    call hd_bn_hit              ; SPEC.md 52.10.9: the DEVICE side of an
%endif                          ; install, which nothing else counts
    int 0x13
    jnc .next
    push ax                     ; AH = the status, across the reset
    mov ah, 0x00                ; reset the controller and try again
    mov dl, [di+HDD_UNIT]
%ifdef INSTBENCH
    inc word [hd_bn_rst]
%endif
    int 0x13
    pop ax
    dec bp
    jnz .attempt
    mov al, ah
    jmp short hd_xfer_fail
.next:
    mov cx, [hd_run]            ; hd_buf_step is per sector, and the LBA it
.step:                          ; walks is what hd_chs reads next time round
    call hd_buf_step
    dec si
    loop .step
    jmp short .run

hd_xfer_ok:
    xor al, al                  ; and CF = 0
    jmp short hd_xfer_out
hd_xfer_fail4:
    mov al, 0x04                ; "sector not found": a CHS the geometry cannot
hd_xfer_fail:                   ; name, or a buffer no transfer can reach
    stc
hd_xfer_out:
    pop di
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_xfer_ide - rung 1's half of hd_xfer, reached by its jump and leaving by
;               its epilogue
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
; Measured on the reference copy (docs/plans/completed/HDD-PLAN.md part 13): 1,918 commands
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
hd_xfer_ide:
    mov bp, di                  ; BP = the device row: DI is the PIO loop's
    mov bx, [di+HDD_BASE]
.run:
    or si, si
    jz hd_xfer_ok
    mov di, bp                  ; DI = the row again, for hd_chs
    call hd_chs                 ; the CHS of the run's FIRST sector
    jc hd_xfer_fail4
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
%ifdef INSTBENCH
    call hd_bn_hit              ; a command, not a sector: this rung hands the
%endif                          ; whole run to the drive (SPEC.md 52.10.9)
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
    jz .run
    mov al, 0x0A                ; "bad sector detected" - the drive said so,
    jmp hd_xfer_fail            ; rather than our own generic 04h
.fail:
    jmp hd_xfer_fail4

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
;
; THE ANSWER IS THE LAST COMPARE, `cmp ax, 1`, which borrows for zero and for
; nothing else. It used to be a `clc` on the success path, and that `clc` was
; NOT OPTIONAL: its absence once made every transfer on this rung fail, because
; `cmp ax, 127` borrows for every run below 127 and neither `mov` nor `pop`
; touches the flags (SPEC.md 52.10.7.1). A flag left over from an earlier
; compare is not an answer - which is why the one that answers comes last.
; -----------------------------------------------------------------------------
hd_bios_run:
    push cx
    push dx
%ifndef NO_HDCYL
%ifdef HD_CYLPROBE
    test byte [di+HDD_FLAGS], HDF_CYLOK ; HDCYLPROBE=1: only when hd_cylprobe
    jz .trk                     ; saw the ROM carry one call across a head
%endif
    test byte [di+HDD_FLAGS], 2 ; ...never on a geometry that is not the
    jnz .trk                    ; ROM's (typed, or a saved record): the ROM
                                ; walks heads in ITS geometry, so a run counted
                                ; in ours reads the wrong sectors with CF = 0.
                                ; The track was self-consistent and stays
    cmp byte [hd_bop], 0        ; A READ runs to the CYLINDER's end (SPEC.md
    jne .trk                    ; 18.91.5) - (heads - head) x spt, less the
    mov ax, [di+HDD_HEADS]      ; sectors behind [hd_sec], which is 1-based.
    sub ax, [hd_head]           ; DX is pushed. Writes keep the track
    mul word [di+HDD_SPT]
    sub ax, [hd_sec]
    inc ax
    jmp short .cyl
.trk:
%endif
    mov ax, [di+HDD_SPT]        ; sectors left in this track, [hd_sec] being
    sub ax, [hd_sec]            ; 1-based
    inc ax
%ifndef NO_HDCYL
.cyl:
%endif
    cmp ax, si
    jbe .page
    mov ax, si
.page:
    mov dx, [hd_bseg]
    mov cl, 4
    shl dx, cl
    add dx, [hd_bofs]           ; DX = the offset within the 64KB DMA page
    neg dx
    jz .cap                     ; a zero offset means the whole page: no cap
    mov cl, 9
    shr dx, cl                  ; ...as sectors
    cmp ax, dx
    jbe .cap
    mov ax, dx
.cap:
    cmp ax, 127
    jbe .done
    mov ax, 127
.done:
    cmp ax, 1                   ; CF = 1 for a bound of ZERO, which cannot
    pop dx                      ; happen for a 512-aligned buffer - every int
    pop cx                      ; 13h target in this driver is one - and CAN
    ret                         ; for one that is not, where forcing a run of
                                ; 1 would issue exactly the straddling transfer
                                ; the cap exists to prevent

; -----------------------------------------------------------------------------
; hd_chs_regs - [hd_cyl]/[hd_head]/[hd_sec] into int 13h's register shape
; in:  DI = the device row; AH/AL already hold the function and count
; out: CX, DH, DL loaded
; clobbers: CX, DX, flags
;
; CH = cylinder bits 0..7, CL bits 6..7 = cylinder bits 8..9, CL bits 0..5 =
; the 1-based sector. hd_chs has refused any cylinder past 1,023, so the
; high byte is 0..3 and two ROTATES put it in bits 6..7 with zeros below -
; where six shifts and a mask used to, an 8086 having no shift by an
; immediate count.
; -----------------------------------------------------------------------------
hd_chs_regs:
    mov cx, [hd_cyl]
    xchg cl, ch                 ; CH = cylinder low 8, CL = bits 8..9
    ror cl, 1
    ror cl, 1                   ; ...into bits 6..7
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
    add word [hd_bofs], 512
    jnc .noseg
    add word [hd_bseg], 0x1000
.noseg:
    add word [hd_lba], 1
    adc word [hd_lba+2], 0
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
    mov al, 0xA0                ; the two bits ATA-1 pins high...
    test cl, 1
    jz .d0
    mov al, 0xB0                ; ...and the drive in bit 4
.d0:
    mov ah, ch
    and ah, 0x0F                ; ...and the head in bits 0..3. MASKED: head 16
    or al, ah                   ; would set bit 4 and address the OTHER drive on
    out dx, al                  ; the channel, and head 64 would set LBA mode.
                                ; hd_chs refuses such a geometry before we get
                                ; here - this is the last line, not the only one
    pop dx
    jmp short hd_ide_wait

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
; INITIALIZE DEVICE PARAMETERS is issued once, from hd_at_channel, with the
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

hd_s_page:   db 'Hard Drive', 0

; =============================================================================
; Data. A driver has no bss - its zeroed data ships inside the image
; (SPEC.md 51.1), which is what lets the kernel make exactly one claim, at
; the size the directory entry already reported, before a byte is read.
; =============================================================================
                                ; hd_field, hd_rowdev, hd_fldi, hd_fldx,
                                ; hd_clx and hd_cly all MOVED to cppage.inc
                                ; with the page that is the only thing that
                                ; ever reads them (SPEC.md 52.13)

hd_vols:     times HD_MAXVOL * HDV_SIZE db 0

hd_bseg:     dw 0               ; ...and its buffer, count and direction
hd_bofs:     dw 0
hd_bcnt:     dw 0
hd_bop       equ hd_rawop       ; the direction: hdcom.inc's own byte, which
                                ; hd_raw's callers set and hd_blk sets for
                                ; itself - so hd_raw has nothing to copy
hd_run:      dw 0               ; sectors in the transfer being issued

%ifdef INSTBENCH
; SPEC.md 52.10.9 - the DEVICE side of a transfer, which nothing else counts.
; The kernel's own instrument (SPEC.md 18.94) stops at dsk_xfer's run loop and
; a DVK_DRV volume leaves before it, so on an install to a hard disk every
; kernel counter reports the FLOPPY. These are free-running and the installer
; banks and subtracts them per phase, the way tests/sysbench does.
hd_bn_sec:   dw 0               ; sectors the drive was asked to move
hd_bn_cal:   dw 0               ; ...in how many commands
hd_bn_rst:   dw 0               ; ...and controller resets, which are retries
%endif

                                ; hd_idbuf is in hd_mbr's 512 bytes, with the
                                ; probe that fills it (SPEC.md 52.13.6)

hd_pslot:    db 0               ; the partition hd_mount is working on
hd_wantmnt:  db 0               ; bit n = mount device n at DRVV_READY: it
                                ; was mounted last session, or the probe
                                ; just found it (SPEC.md 52.6.1)
hd_cfgbuf    equ hd_mbr         ; the blob, staged for OSAPI_DRV_CFG: its
                                ; first HDC_FBUF bytes, which are hd_attach's
                                ; by the time DRVV_READY reads the blob in
                                ; there and partition boot code - which the
                                ; resident never reads - ever after. Its
                                ; lifetime is one hd_cfg_mark or one
                                ; hd_ready, and no partition table is read
                                ; during either (SPEC.md 52.13.6)
                                ; hd_cap MOVED to page.inc, beside the strings
                                ; it is composed from: a built caption belongs
                                ; to the image that letters it, exactly as a
                                ; constant one does (hddabi.inc's HDM_*)

; =============================================================================
; hd_at_dup and hd_at_new are ATTACH-ONLY too, and they are out here for one
; reason: the run below that IS hd_mbr is longer than 512 bytes without them,
; and its excess would sit past the window as image anyway - while out here
; they fill padding the window's `align 512` would otherwise spend on zeros.
; The rule for the run below still binds them: nothing but hd_attach's own
; tree calls them.
; =============================================================================

; -----------------------------------------------------------------------------
; hd_at_dup - is the drive IDENTIFY just answered for one rung 0 already has?
; in:  hd_idbuf holds IDENTIFY's answer; the rung 0 rows are in hd_devs
; out: CF = 0 yes - the BIOS reported it and this unit gets no row of its own,
;      and that BIOS row is now SPENT; CF = 1 no
; clobbers: AX, DX, DI, flags (BX, the base port, is kept)
;
; The key is the GEOMETRY, because it is the only thing the two rungs both
; answer for: a BIOS row's unit is 80h/81h and an IDE row's is 0/1 on a base
; port the BIOS never mentions, so SPEC.md 52.6's kind+unit+base cannot pair
; them. Heads and sectors/track must be EQUAL - int 13h reports the drive's own
; pair for a drive it is not translating, and a drive it IS translating has a
; geometry that is not this one's at all - while the cylinder count only has to
; be no LARGER, because reserving the last cylinder or two for diagnostics is
; what a BIOS drive-type table does and SeaBIOS does it too (a 65-cylinder
; drive is 64 cylinders through AH=08h).
;
; **Each BIOS row is spent once**, in [hd_dupd]: two identical drives with only
; the first in the BIOS is the case a plain scan gets wrong, and it gets it
; wrong in the direction that LOSES a disk. What no key can tell apart is an
; MFM drive and an unknown IDE drive of exactly the same geometry - the second
; is then skipped rather than listed.
;
; Without any of this the machine whose BIOS knows its disk gets TWO rows for
; it, and SPEC.md 52.6.1's automount then mounts every partition on it twice -
; once through each transport, as C: and again as D:, on a desktop with two
; icons per volume and two FAT caches over the same sectors.
; -----------------------------------------------------------------------------
hd_at_dup:
    mov dx, 0x0100              ; DL = the row index, DH = its bit in [hd_dupd]
.scan:
    cmp dl, [hd_ndev]
    jae .no
    mov al, dl
    call hd_dev_row
    cmp byte [di+HDD_KIND], HDK_BIOS
    jne .next
    test [hd_dupd], dh          ; already paired with an earlier IDE unit?
    jnz .next
    mov ax, [hd_idbuf + 3*2]    ; word 3: heads
    cmp ax, [di+HDD_HEADS]
    jne .next
    mov ax, [hd_idbuf + 6*2]    ; word 6: sectors per track
    cmp ax, [di+HDD_SPT]
    jne .next
    mov ax, [hd_idbuf + 1*2]    ; word 1: cylinders - the BIOS may report
    cmp [di+HDD_CYL], ax        ; fewer, never more
    ja .next
    or [hd_dupd], dh            ; spent - and `or` leaves CF = 0
    ret
.next:
    inc dx
    shl dh, 1
    jmp short .scan
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hd_at_new - the next free device row
; in:  nothing
; out: CF = 0 and DI = the row; CF = 1 = the table is full
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_at_new:
    push ax
    mov al, [hd_ndev]
    cmp al, HD_MAXDEV
    cmc                         ; CF = 1 = full
    jc .out
    call hd_dev_row
    inc byte [hd_ndev]
    clc
.out:
    pop ax
    ret

%include "hdsec.inc"

; =============================================================================
; ATTACH-ONLY CODE, LAID IN hd_mbr's 512 BYTES (SPEC.md 52.13.6)
;
; Everything from here to hd_mbr_end runs ONCE, at DRVV_ATTACH, and the first
; hd_part_load blanks it into a partition table. That is safe for one reason,
; and it is the kernel's rather than ours: drv_load_row refuses a row whose
; DRVR_SEG is already set (`.already`), so an attach is only ever sent to an
; image dskw_read_x has just read off the disk - unticking the driver is an
; unload, the next tick reads a fresh image, and a hibernate round trip is
; hbm_detach/hbm_reload, a fresh image again. Nothing below may be CALLED
; from anywhere but hd_attach's own tree, and nothing above may name a label
; down here but hd_entry's one jump; the routines carry `hd_at_` in their
; names for that reason.
;
; It is the same 512 bytes hd_idbuf used to be (52.13.3), one step further:
; that union shared IDENTIFY's buffer with the partition table, and this
; shares the CODE that fills IDENTIFY's buffer - which is why hd_idbuf is
; fourteen bytes now, the three words the probe reads, and not 512.
; =============================================================================

; -----------------------------------------------------------------------------
; hd_attach - DRVV_ATTACH: find every hard disk this machine will let us reach
; in:  AL = DRVV_ATTACH; DS = CS = ours, ES = KERNEL_SEG
; out: CF = 0 and SI = the service table; CF = 1 and AL = DRVE_HW = no hard
;      disk at all
; clobbers: SI, AL (the outputs), flags
;
; Rung 0 first, for every drive the BIOS admits to. Rung 1 then adds any IDE
; device the task file answers for and the BIOS did NOT already report - so a
; machine whose BIOS knows its disk gets one row for it, not two (SPEC.md
; 52.1).
;
; NOTHING IS ZEROED FIRST. hd_state_init used to clear hd_devs, hd_vols and
; the counts for "a re-attach" - and there is no such thing: the image this
; runs in was read off the disk a moment ago (see above), so every one of
; them is the zero the source declares.
;
; ATTACH IS ALL-OR-NOTHING and this one has an easy time of it: the probe
; writes no port that is not a read-back of a drive's own task file, claims
; no memory, and hooks no interrupt.
; -----------------------------------------------------------------------------
hd_attach:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    ; --- rung 0: what the BIOS says -----------------------------------------
    ; int 13h AH=08h on 80h and 81h. DL comes back as the NUMBER of fixed
    ; disks the BIOS knows, which is the honest bound - and a card whose ROM
    ; hooked int 13h is exactly as authoritative here as an AT BIOS. An AT
    ; BIOS answers AH=08h for a drive it does not have with CF CLEAR and the
    ; geometry of CMOS drive type 1 - 305 x 4 x 17, a phantom 10MB `BIOS1`
    ; under the real disk; MR BIOS on a one-drive 286 does exactly that. So
    ; 81h is asked only when 80h answered and counted two; a BIOS that does
    ; not answer for 80h has no fixed disk to number from, and rung 1 finds
    ; an IDE one.
    mov dl, 0x80
    call hd_at_bios
    cmp byte [hd_pcnt], 2
    jb .ide
    mov dl, 0x81
    call hd_at_bios

    ; --- rung 1: the IDE task file ------------------------------------------
    ; 16-bit bus only, and that is not caution: `in ax, dx` on an 8-bit bus is
    ; two byte reads at the same port and the drive's high byte is lost. An
    ; 8088 with a hard disk has a controller with a ROM, and that is rung 0.
.ide:
    call OSAPI_CPU_INFO         ; AL = CPU_*
    cmp al, CPU_286
    jb .done
    mov bx, 0x1F0
    call hd_at_channel
    mov bx, 0x170
    call hd_at_channel
.done:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    mov si, hd_services
    cmp byte [hd_ndev], 1       ; CF = 1 = nothing answered
    jnc .out
    mov al, DRVE_HW             ; the reason, explicitly (SPEC.md 51.2)
.out:
    ret

; -----------------------------------------------------------------------------
; hd_at_bios - one rung 0 drive: int 13h AH=08h, decoded, and its row
; in:  DL = 80h or 81h
; out: a row appended to hd_devs if the BIOS knows the drive; [hd_pcnt] = the
;      BIOS's fixed-disk COUNT whenever it answered at all
; clobbers: AX, BX, CX, DX, DI, ES, flags - hd_attach saved them
;
; The returned values are MAXIMA and zero-based for the head count, which is
; the classic off-by-one in this call. A geometry with no sectors is a BIOS
; saying no in a way that does not set CF - some XT ROMs do exactly that for
; the second drive - and so is DH = 0FFh, whose count of 256 the row cannot
; carry. CH = cyl low, CL bits 6-7 = cyl high, CL bits 0-5 = sectors.
; -----------------------------------------------------------------------------
hd_at_bios:
    push dx
    mov ah, 0x08
    int 0x13
    pop bx                      ; BL = the drive
    jc .out
    mov [hd_pcnt], dl           ; the count hd_attach bounds 81h by
    mov al, cl
    and al, 0x3F                ; AL = sectors/track
    jz .out
    inc dh                      ; DH = heads; 0FFh + 1 = 256 is caught HERE
    jz .out
    rol cl, 1                   ; CL bits 6..7 -> bits 0..1
    rol cl, 1
    and cl, 3
    xchg cl, ch                 ; CX = the cylinder MAX...
    inc cx                      ; ...and the count is one more
    mov dl, dh
    xor dh, dh                  ; DX = heads
    xor ah, ah                  ; AX = sectors/track
    call hd_at_new              ; DI = a fresh row
    jc .out
    mov byte [di+HDD_KIND], HDK_BIOS
    mov [di+HDD_UNIT], bl
    jmp short hd_at_geom
.out:
    ret

; -----------------------------------------------------------------------------
; hd_at_geom - put a probed geometry in a fresh row
; in:  DI = the row, CX = cylinders, DX = heads, AX = sectors/track
; out: nothing; HDD_FLAGS bit 0 set when all three are usable
; clobbers: flags
;
; A geometry is KNOWN only when every field is inside what CHS addressing can
; carry - 1..1024 cylinders, 1..255 heads, 1..63 sectors. Anything else is
; the user's to type in, which is an ordinary state and not a failure. The
; row is fresh off hd_at_new, so its flags are already zero.
; -----------------------------------------------------------------------------
hd_at_geom:
    mov [di+HDD_CYL], cx
    mov [di+HDD_HEADS], dx
    mov [di+HDD_SPT], ax
    call hd_geom_ok
    jc .out
    or byte [di+HDD_FLAGS], 1
.out:
    ret

; -----------------------------------------------------------------------------
; hd_at_channel - probe both drives on one IDE channel
; in:  BX = the task file's base port
; out: rows appended to hd_devs
; clobbers: AX, CX, DX, DI, ES, flags - hd_attach saved them
;
; A drive rung 0 already reported gets NO row here - that is what SPEC.md 52.1
; means by "for a drive the BIOS does not know", and hd_at_dup is the test.
; -----------------------------------------------------------------------------
hd_at_channel:
    xor cx, cx                  ; CL = drive 0 / 1
.drive:
    push cx
    call hd_at_ident            ; CF = 0 and hd_idbuf holds IDENTIFY's answer
    jc .next
    call hd_at_dup              ; the BIOS's own row for this drive is the one
    jnc .next                   ; to keep (SPEC.md 52.1)
    call hd_at_new
    jc .full
    pop cx
    push cx
    mov byte [di+HDD_KIND], HDK_IDE
    mov [di+HDD_UNIT], cl
    mov [di+HDD_BASE], bx
    mov cx, [hd_idbuf + 1*2]    ; word 1: cylinders
    mov dx, [hd_idbuf + 3*2]    ; word 3: heads
    mov ax, [hd_idbuf + 6*2]    ; word 6: sectors per track
    call hd_at_geom
    call hd_ide_setparams       ; tell the drive the geometry we will use -
                                ; without it every read lands somewhere else
.next:
    pop cx
    inc cx
    cmp cl, 2
    jb .drive
    ret
.full:
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_at_ident - IDENTIFY DEVICE: the three words the probe reads, into hd_idbuf
; in:  BX = base, CL = drive 0/1
; out: CF = 0 and hd_idbuf holds words 0..6; CF = 1 = no such drive
; clobbers: AX, CX, DX, DI, ES, flags
;
; A drive that predates ATA-1 answers ABRT here, and that is not a failure -
; it is exactly the machine the page's manual geometry exists for. It is
; refused as a DEVICE, though: without IDENTIFY there is nothing to tell us a
; drive is even present, and inventing one would put a row on the page for a
; controller that is not there.
;
; ALL 256 WORDS ARE STILL READ - the drive holds DRQ until the sector is
; drained - and the 249 nobody looks at are read into AX and dropped, which
; is what lets hd_idbuf be 14 bytes.
; -----------------------------------------------------------------------------
hd_at_ident:
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
    mov dx, bx                  ; IDE_DATA
    push ds
    pop es
    mov di, hd_idbuf
    mov cx, 7
    cld
.read:
    in ax, dx                   ; 16-bit, and the reason this rung is gated
    stosw                       ; on a 16-bit bus (SPEC.md 52.1)
    loop .read
    mov cx, 256 - 7
.drain:
    in ax, dx
    loop .drain
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ready - DRVV_READY: the kernel can take our calls now (SPEC.md
;            51.2.2/52.6/52.6.1)
; in:  nothing
; out: CF = 0 (hd_cfg_automount's answer); every register preserved
;
; The earliest point at which OSAPI_VOL_* will answer us: their fence is the
; publication slot and attach runs before it is armed. So this - and not
; attach - is where the settings are read and where the drives are mounted:
; every one the probe found, plus every one the settings said was mounted
; last session (SPEC.md 52.6.1).
;
; IT IS IN hd_mbr's BYTES, LIKE ATTACH, AND IT LEAVES BY A JUMP. Everything
; up to the jump runs before anything reads a partition table; the mount
; that does is hd_cfg_automount's, which is out in the resident and RETURNS
; TO THE KERNEL, because the first hd_part_load it makes blanks the bytes
; this routine is made of. A `call` there would return into a partition
; table.
;
; Three things, in this order:
;
;   1. THE SYSTEM VOLUME, BANKED ONCE (SPEC.md 52.11). drv_load brackets the
;      whole load in drv_vol_bank .. drv_vol_back and sends DRVV_READY from
;      inside that bracket (SPEC.md 51.2.2), so here and only here the
;      current volume IS the one HDD.DRV was read from - which is where
;      HDDTOOL.DRV is too, on a floppy machine and an installed one alike.
;      **AND THAT SENTENCE NEEDED A KERNEL FIX TO BE TRUE** (SPEC.md 51.2.3):
;      OSAPI_FILE_HERE answers for the CALLING INSTANCE (SPEC.md 19.2.1), a
;      driver is not one, and ticking our row on the Drivers page is a
;      CONTROL PANEL click - so this banked wherever the PANEL had been
;      launched, and Format and Install went to B: for the tool with it
;      sitting in A:. drv_call clears the stamp now, so this reads the
;      machine, which is what it always meant to read.
;
;   2. THE SETTINGS BLOB (SPEC.md 52.6): geometry the user typed, and what
;      was mounted. A blob nobody has written reads back as zeroes and a zero
;      version is not HDC_VER, so a machine that has never saved gets the
;      defaults - the probe's own answers and nothing mounted - and never an
;      error. Each record is matched to a device by WHAT IT IS - kind, unit
;      and base port - never by where it sat in the table last time.
;
;   3. WHAT IS THERE (SPEC.md 52.6.1): every probed device with a usable
;      geometry is added to [hd_wantmnt] - ADDED, never replacing, so the
;      automount can only ever attempt MORE than the settings asked for.
;      hd_geom_ok is the page's own predicate (SPEC.md 47), so what is
;      attempted here is exactly what the page would have let the user click.
;
; [hd_wantmnt] starts at the zero the image declares: this runs once, on an
; image read a moment ago.
; -----------------------------------------------------------------------------
hd_ready:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call OSAPI_FILE_HERE        ; DX = the directory, BL = its drive
    mov [hd_tcwd], dx
    mov [hd_tvol], bl

    push ds
    pop es
    mov si, hd_cfgbuf
    mov cx, HDC_FBUF
    xor al, al                  ; get
    call OSAPI_DRV_CFG
    jc .found                   ; not published: impossible here, but the
                                ; answer is the defaults either way
    cmp cx, HDC_ENT0
    jb .found
    cmp byte [hd_cfgbuf], HDC_VER
    jne .found                  ; another version's shape: the defaults, and
                                ; the next save rewrites it in this one
    mov cl, [hd_cfgbuf+HDC_COUNT]
    xor ch, ch
    jcxz .found
    cmp cl, HD_MAXDEV
    ja .found
    mov si, hd_cfgbuf + HDC_ENT0
.rec:
    push cx
    mov bx, 0x0100              ; BL = the device index, BH = its bit
.scan:
    cmp bl, [hd_ndev]
    jae .nextrec
    mov al, bl
    call hd_dev_row
    mov ax, [si+HDC_E_KIND]     ; AL = kind, AH = unit - matched by WHAT IT
    cmp ax, [di+HDD_KIND]       ; IS, never by where it sat in the table last
    jne .next                   ; time (cfg.inc's header)
    mov al, [si+HDC_E_BASE]     ; the stored port, back up four bits
    xor ah, ah
    mov cl, 4
    shl ax, cl
    cmp ax, [di+HDD_BASE]
    jne .next
    ; --- the geometry, and ONLY when the probe could not answer -------------
    ; This used to restore it unconditionally, reasoning that "a saved record
    ; only exists when the user typed it in or mounted with it, and either way
    ; it is the one that worked". That is true on ONE machine and false the
    ; moment the file travels - and a BIOS drive's match key is
    ; kind=BIOS/unit=80h/base=0, which is the SAME KEY ON EVERY MACHINE THERE
    ; IS. So the geometry typed on one box was restored, silently and with
    ; full confidence, onto a different box's drive, and every LBA then
    ; resolved to a different physical sector (docs/FIELD-NOTES.md 33). The
    ; probe's answer is a fact about the drive in front of us and the saved
    ; one is a fact about wherever this file was written, so the probe wins
    ; whenever it has one. The MOUNT bit below is untouched: which drives to
    ; mount is a preference and travels fine; a geometry is not and does not.
    ;
    ; UNLESS THE RECORD SAYS THE USER TYPED IT. The +/- editor is live on
    ; every drive, probed or not, so "the probe answered" is not the same fact
    ; as "nobody typed one". HDC_F_TYPED is the fact itself, recorded by
    ; hd_cfg_mark off HDD_FLAGS bit 1. The field record that travelled between
    ; the 5150s carried flags=01 - mounted, not typed - so it still loses to
    ; the probe and docs/FIELD-NOTES.md 33 stays fixed.
    test byte [si+HDC_E_FLAGS], HDC_F_TYPED
    jnz .geom                   ; the user's own answer wins...
    test byte [di+HDD_FLAGS], 1 ; ...and where there is none, the probe's does
    jnz .nogeom
.geom:
    mov ax, [si+HDC_E_CYL]
    mov [di+HDD_CYL], ax
    mov al, [si+HDC_E_HEADS]
    xor ah, ah
    mov [di+HDD_HEADS], ax
    mov al, [si+HDC_E_SPT]
    mov [di+HDD_SPT], ax
    and byte [di+HDD_FLAGS], 0xFC
    call hd_geom_ok
    jc .nogeom
    or byte [di+HDD_FLAGS], 3   ; bit 0 usable, AND BIT 1 - "not the BIOS's",
                                ; which the save needs: it drops any record
                                ; that is "probed and not mounted", so a
                                ; restored geometry on an unmounted drive
                                ; would otherwise erase itself at the next
                                ; write
    call hd_geom_push           ; the drive has to be told too: this runs before
                                ; the first paint, so a restored geometry that
                                ; only we know about would automount and then
                                ; read the wrong sectors with no user present
.nogeom:
    test byte [si+HDC_E_FLAGS], HDC_F_MOUNT
    jz .nextrec
    or [hd_wantmnt], bh
    jmp short .nextrec
.next:
    inc bx
    shl bh, 1
    jmp short .scan
.nextrec:
    pop cx
    add si, HDC_ESZ
    loop .rec

.found:                         ; 3: what is THERE
    mov bx, 0x0100              ; BL = the device index, BH = its bit
.dev:
    cmp bl, [hd_ndev]
    jae .go
    mov al, bl
    call hd_dev_row             ; DI = the row, which is hd_geom_ok's argument
    call hd_geom_ok
    jc .skip
    or [hd_wantmnt], bh
.skip:
    inc bx
    shl bh, 1
    jmp short .dev
.go:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp hd_cfg_automount        ; NOT a call: see above

; =============================================================================
; The service table (SPEC.md 51.2) - read ONCE, by drv_publish, the moment
; hd_attach returns, and never again: the kernel keeps its own copy. So it is
; attach-only data and lives with the attach-only code.
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
    dw hd_s_page                ; DSV_CPNAME  - and so the page exists. The
                                ; STRING stays resident: the panel reads it
                                ; out of our segment whenever it lists pages
    dw hd_cp_paint              ; DSV_CPPAINT - a THUNK now (SPEC.md 52.13):
    dw hd_cp_click              ; DSV_CPCLICK   the page is in HDDTOOL.DRV
    dw hd_tool_reap             ; DSV_CPCLOSE - the panel has gone, so the disk
                                ; tool's 11KB goes with it (SPEC.md 52.11.7)
    dw 0                        ; DSV_FS      - a file redirector's, not ours
    dw hd_cp_key                ; DSV_CPKEY   - the arrows move the drive
                                ; highlight and the C/H/S field; the geometry
                                ; itself is still set with - and + (52.4)
    dw hd_cp_up                 ; DSV_CPUP    - the page acts on the RELEASE
    dw hd_cp_drag               ; DSV_CPDRAG  - ...and follows the pointer
                                ;               between the edges (13.8.4)
hd_svc_end:                     ; ...AND STOPS HERE, its length in the header
                                ; (OS88_DRIVER's fourth argument, +15). So
                                ; DSV_PKGCALL is published 0 without a cell
                                ; here saying so - no package reaches a raw
                                ; sector - and a cell added to DSV_* later is
                                ; 0 for this driver until it writes one

hd_pcnt:     db 0               ; how many fixed disks the BIOS said it has
hd_dupd:     db 0               ; bit n = rung 0's row n is the same drive as
                                ; an IDE unit rung 1 has already seen, so it
                                ; can pair with no other (hd_at_dup)
hd_idbuf:    times 7 dw 0       ; IDENTIFY's words 0..6 (hd_at_ident)
hd_mbr_end:
%if hd_attach != hd_mbr || hd_ready - hd_mbr < HDC_FBUF
  %error "hd_cfgbuf is hd_mbr's first bytes: only hd_attach may be under them"
%endif
%if hd_mbr_end - hd_mbr < 512
    times 512 - (hd_mbr_end - hd_mbr) db 0  ; the rest of the partition table
%endif

; **NO os88ui.inc.** It was here for the PAGE and the page is in HDDTOOL.DRV
; now (SPEC.md 52.13), which was already carrying its own copy for the two
; tool windows - so those bytes are DELETED rather than moved, and they are
; the largest single item this driver gave back. Nothing else in the resident
; draws a control: what is left of its drawing is one line of refusal text in
; hd_cp_paint, lettered with OSAPI_FONT_RUN.

    OS88_DRV_END
