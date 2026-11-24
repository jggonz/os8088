; =============================================================================
; os8088 - ZIP.DRV (SPEC.md 138)
;
; An Iomega Zip drive on the parallel port: the parallel Zip 100 (VPI0, Linux
; drivers/scsi/ppa.c) and the Zip Plus / Zip 250 (VPI2, Linux imm.c), in
; nibble mode on any port and in PS/2 byte mode on a bidirectional one. A
; DRVC_ZIP driver - the second BLOCK class (SPEC.md 138.1) - so the kernel's
; own FAT driver reads and writes the disk and this image serves SECTORS
; through DSV_BLK, never files.
;
;   ziptrans.inc  the transport contract (the lead's): zt_* and ZT*/ZTE_*
;   zipppa.inc    VPI0's _probe/_cmd/_reset          (SPEC.md 138.2.1)
;   zipimm.inc    VPI2's _probe/_cmd/_reset          (SPEC.md 138.2.2)
;   zipscsi.inc   the SCSI layer: one command, its retries, sense -> status,
;                 and the drive's state (SPEC.md 138.3)
;   zipvol.inc    DSV_BLK: the lazy medium, the fence, the chunks (138.4)
;   zipcp.inc     the Control Panel page (SPEC.md 138.6)
;   zippkg.inc    the package door, ZIPV_INFO and ZIPV_EJECT (SPEC.md 138.7)
;
; ATTACH FINDS THE DRIVE; READY REGISTERS THE ONE VOLUME, disk or none -
; CDROM.DRV's shape (SPEC.md 119), because the volume slots' fence is closed
; until attach has returned and the class is published (SPEC.md 51.2.2). The
; medium is learned on the first transfer, not at attach: a drive with no
; disk in it is a drive.
;
; Prefixes: zip_ (this file), zs_ (the SCSI layer), zv_ (the volume), zc_
; (the page), zp_ (the drive's state), zt_ (the transport state ziptrans.inc
; says the core declares). Every proc is near with a near ret (SPEC.md 51.1).
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1) - except the
; package door, where it is the CALLING PACKAGE's (SPEC.md 20.11).
; =============================================================================

%define OS88_DRV_HIGHOK          ; kern_mega may run this image above 1MB
                                ; (KERN-MEGA-PLAN item 82): no arithmetic on
                                ; our own CS or DS, nothing of ours reaches a
                                ; BIOS or a DMA engine - the wire is `in` and
                                ; `out` - and zv_bnorm leaves an LDT
                                ; selector's offset unfolded, as cd_bnorm does
%include "os88drv.inc"
%include "ziptrans.inc"

    OS88_DRIVER 'Zip Drive', DRVC_ZIP, zip_entry, zip_svc_end - zip_svc
zip_s_name  equ $$ + 16         ; DSV_CPNAME's 'Zip Drive' IS the header's
                                ; name field, NUL-padded to +31: the page
                                ; title and the driver's name are one string
                                ; (HDD.DRV's hd_s_page, driver size pass 1)

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2, 51.8). BLKCAP_SEL: DSV_BLK loads DX into ES
; for the transport as it is and walks only the offset under an LDT selector
; (zv_bnorm), so kern_mega hands us a selector rather than bouncing (51.8.1).
; -----------------------------------------------------------------------------
zip_svc:
    dw BLKCAP_SEL               ; DSV_CAPS
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw 0                        ; DSV_NAME    - read by nothing for this class
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw zv_blk                   ; DSV_BLK     - the sectors (zipvol.inc)
    dw zip_s_name               ; DSV_CPNAME  - the page (zipcp.inc)
    dw zc_paint                 ; DSV_CPPAINT
    dw zc_click                 ; DSV_CPCLICK - the press ARMS Eject...
    dw 0                        ; DSV_CPCLOSE - the page holds nothing
    dw 0                        ; DSV_FS      - a block class has no verbs
    dw 0                        ; DSV_CPKEY   - this page takes no keys
    dw zc_up                    ; DSV_CPUP    - ...the release FIRES it
    dw zc_drag                  ; DSV_CPDRAG  - and the pointer is followed
    dw zip_pkg                  ; DSV_PKGCALL - the door (zippkg.inc)
zip_svc_end:

zip_label:  db 'Zip', 0         ; the desktop zone's label (SPEC.md 138.4)

; =============================================================================
; ENTRY - one compare per verb it handles, everything else REFUSED (51.2.2)
; =============================================================================
zip_entry:
    cmp al, DRVV_ATTACH
    je zip_attach
    cmp al, DRVV_DETACH
    je zip_detach
    cmp al, DRVV_READY
    je zip_ready
    stc                         ; TIER, HWINFO, SLEEP, WAKE and any later
    ret                         ; verb: refused, nothing done (SPEC.md 51.2.2)

; -----------------------------------------------------------------------------
; DRVV_ATTACH - find the drive (SPEC.md 138.2, 138.5)
; in:  DS = ours, ES = KERNEL_SEG, BX = the row (PRESERVED: drv_attach reads
;      it on return, SPEC.md 51.2)
; out: CF = 0 and SI = the service table; CF = 1 and AL = a DRVE_*
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1): no vector, no claim, no volume. What
; a refusal leaves behind is every candidate port DISCONNECTED, which is the
; transports' contract for a failed probe (ziptrans.inc) and the bidirectional
; test's own restore below - a printer on the port prints as before.
;
; THE CANDIDATES (SPEC.md 138.5): the ZP pin's port alone, or nothing for Off,
; or - Auto - the BIOS's own LPT table at 0040:0008h, three words, a zero
; ending it. 0040h is an identity selector under kern_mega's protected mode
; (SPEC.md 39's list), so the read is the same on every kernel.
; -----------------------------------------------------------------------------
zip_attach:
    cmp word [zt_base], 0
    mov al, DRVE_TWICE          ; (a `mov`: the compare's ZF stands)
    jne .fail                   ; attached already: touch nothing (51.2.2)
    push bx
    push cx
    push dx
    push es
    call os88_pin               ; AL = the ZP pin (SPEC.md 51.9.2)
    cmp al, DRVPIN_OFF
    je .no
    mov cx, 1                   ; a pin: ONE candidate, (pin * 2) + 200h
    xor ah, ah
    shl ax, 1
    add ax, DRVPIN_BASE
    xchg ax, dx                 ; DX = the pinned port - or 200h for Auto,
    cmp dx, DRVPIN_BASE         ; which names no LPT port and is the test
    jne .one
    mov ax, 0x40                ; AUTO: the BIOS's LPT table
    mov es, ax
    mov bx, 8
    mov cl, 3
.scan:
    mov dx, [es:bx]
    or dx, dx
    jz .no                      ; a zero ends the table
    call zip_try                ; CF = 0: a drive, and it is ours
    jnc .yes
    inc bx
    inc bx
    loop .scan
    jmp short .no
.one:
    call zip_try
    jnc .yes
.no:
    mov word [zt_base], 0       ; DETACH and a second ATTACH read it as "none"
    pop es
    pop dx
    pop cx
    pop bx
    mov al, DRVE_HW
.fail:
    stc
    ret
.yes:
    pop es
    pop dx
    pop cx
    pop bx
    mov si, zip_svc             ; CF = 0 from zip_try
    ret

; -----------------------------------------------------------------------------
; zip_try - is the Iomega drive on THIS port? (attach's, internal)
; in:  DX = a candidate port base
; out: CF = 0 found: [zt_base], [zt_mode], [zt_id], [zt_vec], [zp_proto] and
;      the vendor checked; CF = 1 not here, the port disconnected
; clobbers: AX, DX, flags - attach's scan reloads DX from the table
;
; BIDIRECTIONAL IS ASKED ONCE, HERE (SPEC.md 138.2): DIRECTION set, 55h and
; then AAh written to the data latch and read back. An output-only port - every
; XT card - drives the lines and answers what was written; a port that has
; tri-stated them answers something else. The control register and the latch
; are put back as found before either transport runs.
;
; VPI0 FIRST, THEN VPI2 (SPEC.md 138.2). A transport that probes is copied into
; zt_vec, and the drive must then answer INQUIRY with IOMEGA: a parallel SCSI
; device that is not a Zip is not ours to format a volume table onto.
; -----------------------------------------------------------------------------
zip_try:
    push bx
    push cx
    push si
    mov [zt_base], dx
    mov bx, dx                  ; BX = the base; DX walks its registers
    in al, dx                   ; the latch as found...
    mov ch, al
    inc dx
    inc dx                      ; ZT_CTRL
    in al, dx
    mov cl, al                  ; ...and the control register
    or al, ZT_CTR_DIR
    out dx, al                  ; DIRECTION: tri-state the data lines
    mov dx, bx
    mov al, 0x55
    out dx, al
    in al, dx
    mov ah, al
    mov al, 0xAA
    out dx, al
    in al, dx
    cmp ax, 0x55AA              ; AH = the first read, AL = the second: both
    mov al, ZIPM_NIBBLE         ; what was written is a port that drives the
    je .mode                    ; lines whatever DIRECTION says
    mov al, ZIPM_PS2
.mode:
    mov [zt_mode], al
    xchg al, ch                 ; CH = the mode, for VPI2 below
    out dx, al                  ; the latch back...
    inc dx
    inc dx
    mov al, cl
    and al, ~ZT_CTR_DIR & 0xFF  ; ...and the control register, DIRECTION
    out dx, al                  ; clear before any transport runs

    mov si, zip_ppavec          ; VPI0 first...
    mov byte [zp_proto], ZIPP_VPI0
    call ppa_probe              ; (a transport keeps every register: CH
    jnc .found                  ; stands)
    mov si, zip_immvec          ; ...then VPI2, with the mode as the core
    mov byte [zp_proto], ZIPP_VPI2  ; proved it: a failed VPI0 probe may
    mov [zt_mode], ch           ; have lowered it on its way to failing
    call imm_probe
    jc .none
.found:
    mov ax, [si]                ; the one that probed, into zt_vec
    mov [zt_vec+ZTV_PROBE], ax
    mov ax, [si+2]
    mov [zt_vec+ZTV_CMD], ax
    mov ax, [si+4]
    mov [zt_vec+ZTV_RESET], ax
    call zs_ident               ; INQUIRY: IOMEGA, or not ours
    jnc .out
.none:
    mov byte [zp_proto], 0
    stc
.out:
    pop si
    pop cx
    pop bx
    ret

zip_ppavec: dw ppa_probe, ppa_cmd, ppa_reset
zip_immvec: dw imm_probe, imm_cmd, imm_reset

; -----------------------------------------------------------------------------
; DRVV_READY - the volume, registered now that the fence answers (51.2.2)
;
; ONE volume, whether or not a disk is in the drive (SPEC.md 138.4): an empty
; drive is a zone with nothing to mount, as an empty A: is. CX = 0 is
; "unknown length" (SPEC.md 18.7.5) - a Zip 100 is 196,608 sectors, past what
; the register holds - so zv_blk is the fence. A refusal to register is quiet:
; the driver stays loaded and its page and door still answer.
; -----------------------------------------------------------------------------
zip_ready:
    call zip_voladd
    clc
    ret

; zip_voladd - OSAPI_VOL_ADD our one volume, [zip_vol] = its index or FFh.
; preserves every register
zip_voladd:
    push ax
    push cx
    push dx
    push si
    xor al, al                  ; our own handle: there is one drive
    xor cx, cx                  ; unknown length (above)
    xor dx, dx                  ; a plain add, not a take (SPEC.md 52.1.1)
    mov si, zip_label
    call OSAPI_VOL_ADD
    jnc .ok
    mov al, 0xFF
.ok:
    mov [zip_vol], al
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
;
; The volume goes (OSAPI_VOL_DEL writes back first, SPEC.md 138.1); the port
; is already disconnected, every transport command ending that way
; (ziptrans.inc). Nothing else was taken. The disk stays in the drive:
; unticking a driver is not a request to eject.
; -----------------------------------------------------------------------------
zip_detach:
    call zip_voldel
    mov word [zt_base], 0
    clc
    ret

; zip_voldel - OSAPI_VOL_DEL our volume if we hold one. preserves every reg
zip_voldel:
    push ax
    mov al, 0xFF
    xchg al, [zip_vol]
    cmp al, 0xFF
    je .out
    call OSAPI_VOL_DEL
.out:
    pop ax
    ret

    OS88_PIN                    ; os88_pin: the ZP blob's byte (51.9.2)

%include "zipppa.inc"
%include "zipimm.inc"
%include "zipscsi.inc"
%include "zipvol.inc"
%include "zipcp.inc"
%include "zippkg.inc"

%define OS88UI_ARM              ; os88ui_arm/fire/armed: the press/release
%define OS88UI_NOGEST           ; the page drives its one button through the
                                ; panel's own press/drag/release cells, as
                                ; NET.DRV's does (drivers/net/netui.inc)
%define OS88UI_NOGLYPH          ; ...and draws no check box and no radio
%define OS88UI_NOBFIND          ; hit-tests its own rect (SPEC.md 20.5.1.3.4)
%include "os88ui.inc"           ; ...and the standard control (SPEC.md 20.5.1)

; =============================================================================
; STATE - the one cell whose load-time value is not 0, then OS88_STATE's zeros
; =============================================================================
zip_vol:    db 0xFF             ; the volume index we registered, FF = none

    OS88_STATE                  ; every cell from here is 0 at load
; --- the transport's (ziptrans.inc: the core DECLARES them) -----------------
zt_base:    dw 0                ; the port; 0 = not attached
zt_mode:    db 0                ; ZIPM_NIBBLE / ZIPM_PS2
zt_id:      db 0                ; the drive's SCSI ID
zt_vec:     dw 0, 0, 0          ; the probed transport's _probe, _cmd, _reset

; --- the drive (zipscsi.inc) -------------------------------------------------
zp_proto:   db 0                ; ZIPP_VPI0 / ZIPP_VPI2
zp_st:      db 0                ; ZIPS_*: what the last query found
zp_med:     db 0                ; ZM_*: does the volume know the medium?
zp_cap:     dw 0, 0             ; the medium's sectors, 0 = none
zp_base:    dw 0, 0             ; the volume: its first sector on the disk...
zp_len:     dw 0, 0             ; ...and its length (the fence)
zs_cdb:     times 10 db 0       ; one CDB, built in place
zs_buf:     times ZS_BUFSZ db 0 ; INQUIRY, SENSE, CAPACITY, MODE SENSE answers
zs_tick:    dw 0                ; zs_cmd's wait: the tick it last saw...
zs_left:    db 0                ; ...and the ticks still allowed
zs_dir:     db 0                ; the command's ZTD_*, kept across a retry

; --- the transfer (zipvol.inc) -----------------------------------------------
zv_op:      db 0                ; 0 read / 1 write
zv_lba:     dw 0, 0             ; the next command's absolute LBA
zv_bofs:    dw 0                ; the buffer, offset FIRST so `les` loads the
zv_bseg:    dw 0                ; pair

; --- the page (zipcp.inc) ----------------------------------------------------
zc_px:      dw 0                ; the pane's origin, banked by zc_ent
zc_py:      dw 0
zc_body:    dw 0                ; zc_ent's: the verb body it is running
zc_rc:      dw 0, 0, 0, 0       ; Eject's rect, built by zc_brect
zc_down:    db 0                ; 1 = Eject is drawn PRESSED (SPEC.md 13.8.4)
zc_buf:     times 24 db 0       ; a composed value: "VPI0 nibble", "96 MB"
zc_btlbl:   dw 0                ; the button record's label...
zc_btflg:   dw 0                ; ...and flags (zc_btn writes both)

%if zv_bseg - zv_bofs != 2
  %error "zip: les reads zv_bofs then zv_bseg"
%endif

    OS88_DRV_END

; THE BUDGET (the ZIP fan-out's, docs/plans/ZIP-PLAN.md): the image with its
; bss, both transports included. A claim is the granules of this (SPEC.md
; 51.1.2), so this is the RAM a ticked row costs
ZIP_IMG_MAX equ 6656
%if OS88_DRV_SIZE > ZIP_IMG_MAX
  %error "ZIP.DRV is past its 6,656-byte budget (image + bss)"
%endif
