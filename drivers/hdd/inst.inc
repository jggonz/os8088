; =============================================================================
; os8088 - HDD.DRV / inst.inc
;
; Install (SPEC.md 52.10.4): make a partition into a system volume the machine
; can boot from, by putting both floppies onto it and marking it bootable.
;
;   Install to  Size  State
;   Slot 1      31M   C: FAT16            <- mounted: its letter, then what
;   Slot 2      20M   Linux                  is on it (SPEC.md 52.10.4.2)
;   Slot 3      1M    FAT12, Too Small    <- the reason, when it is a no
;   Slot 4      -     Empty, No Room
;   [Install] [Close]      <- the first one says 'Copy Apps', then 'Restart'
;   Insert the APPS disk in A:, then Install
;
; **UNLESS IT KEEPS THE TARGET'S FILES** - the default over a volume, the
; Erase box clear (SPEC.md 52.10.15): then nothing is formatted, the kernel
; goes in the first free run long enough for it, and the VBR carries its
; offset. hd_inst_keep is that path; everything below is the other one.
;
; **IT FORMATS THE TARGET, AND THAT IS NOT A CONVENIENCE.** boot/boothd.asm
; reads KERNEL.SYS as a flat run from the start of the data area rather than
; walking its cluster chain - which is what makes it fit in 512 bytes
; (SPEC.md 19.3/52.10.2) - so the kernel has to be the FIRST thing on the
; volume and contiguous from cluster 2. Formatting guarantees a free cluster
; 2; writing the kernel before anything else puts it there.
;
; **THE COPY IS IN STAGES, DRIVEN BY REPEAT CLICKS**, because the machine this
; is for has ONE floppy drive (docs/FIELD-MACHINES.md) and a driver has no
; modal dialog to ask in - tool.inc's Delete uses the same idiom, the caption
; asking and the second click answering:
;
;   idle     -> Install arms:  'Erases it: Install again to confirm'
;   armed    -> format, copy the SYSTEM disk (the one in A: now), write the
;               VBR and then the MBR - the machine boots from here on
;               (SPEC.md 52.10.10):  'Insert the APPS disk, then Install'
;   sysdone  -> copy that disk:      'Done - remove the floppy, Restart'
;   done     -> the SAME button says 'Restart' now, and reboots the machine
;               (SPEC.md 52.10.6.1). It was a third button of its own, greyed
;               through every stage but this one
;
; Taking the system disk out mid-install is safe: the kernel is in RAM and the
; only thing that reaches for A: afterwards is this copy. A Control Panel
; close would try to write SYSTEM.CFG and be refused, because SPEC.md 51.5.1
; looks for KERNEL.SYS before writing and the apps disk has none.
;
; **THE MBR IS LAST AND IT IS THE COMMIT.** Every earlier step leaves a disk
; that is merely not bootable yet - the same discipline the formatter follows
; in writing its boot sector last (SPEC.md 52.3). An install interrupted
; anywhere leaves a machine that still boots from its floppy.
;
; A driver has no instance, so it has no folder of its own and uses the
; machine's (SPEC.md 19.2.1 - inst_vol_enter says so in its own comment):
; every file call here acts on the global current volume, and OSAPI_FILE_GOTO
; is how it moves. The volume is banked and restored around each stage,
; SPEC.md 51.5.2's rule, learned when a Control Panel click silently moved
; the user to A:.
; =============================================================================

HIW_W        equ 356
HIW_H        equ 150
HIW_LX       equ 8              ; a MULTIPLE OF 8, with OSAPI_WM_SNAP on the
                                ; window, so every OSAPI_FONT_RUN in here takes
                                ; SPEC.md 6.1's single-store path on the two
                                ; mono adapters. It was 4, which is off that
                                ; path by four pixels
HIW_HY       equ 6
HIW_R0Y      equ 24
HIW_ROWH     equ 12
HIW_CKY      equ 78             ; the Erase box (SPEC.md 52.10.15), between
HIW_CKH      equ 12             ; the last row and the buttons
HIW_CKW      equ 12 + 6 + 25*8  ; box, gap, 'Erase the partition first'
HIW_BY       equ 96
HIW_BH       equ 16
HIW_CAPY     equ 120
; HDBSTEP - tag which step of hd_icopy_one is running (SPEC.md 52.10.9).
; DEFINED IN BOTH BUILDS and empty in the shipped one, because the uses are
; in ordinary code: a macro that exists only under a knob makes every build
; without that knob fail to assemble, which is exactly what it did.
%ifdef INSTBENCH
  %macro HDBSTEP 1
    mov byte [hd_bfstep], %1
  %endmacro
%else
  %macro HDBSTEP 1
  %endmacro
%endif

HIW_B0X      equ 8              ; the ACTION button: 'Install', 'Copy Apps' or
HIW_B1X      equ 104            ; 'Restart', one control saying what the click
HIW_BW0      equ 88             ; does NOW (SPEC.md 52.10.6.1). Its widest
HIW_BW1      equ 56             ; caption is nine characters, 72px, and the
                                ; button was 72px: it would have touched the
                                ; frame at both ends, so it is 88 and Close
                                ; moved right by 16. These drive the HIT TEST
                                ; as well as the drawing (hd_ibhit reads the
                                ; same pairs), so there is one place to change
                                ; and not two
HIW_CELLS    equ 42             ; characters across the text band: HIW_W - 12
                                ; less HIW_LX, in 8px cells. 42 and not 35
                                ; since the State column says what a slot IS
                                ; (SPEC.md 52.10.4.2): its longest row is 24
                                ; cells from HIW_STATEC. Every run in this
                                ; window is padded to it, which is what makes
                                ; the run OPAQUE over the whole band and so
                                ; what removes the blank-then-letter flash

HIW_SIZEC    equ 12             ; the Size and State columns, in cells -
HIW_STATEC   equ 18             ; under hd_s_ihead's two headings
%if HIW_STATEC + 24 > HIW_CELLS
  %error "the State column's longest row does not fit HIW_CELLS"
%endif

; What a slot is as an install TARGET, which is not tool.inc's HTS_* question.
; That one asks what is there; this asks whether an install would work, and
; the two differ: a slot with a perfectly good volume on it is a fine target
; (it gets reformatted), and a slot too small is not, however healthy it is.
HIS_OK       equ 0
HIS_SMALL    equ 1
HIS_ALIEN    equ 2
HIS_NONE     equ 3
HIS_LIVE     equ 4              ; the volume THIS MACHINE BOOTED FROM, which is
                                ; a perfectly good partition and the one target
                                ; an install must never take (SPEC.md 52.10.4.1)

; Both floppies are 720KB between them, and a system volume wants room to work
; in afterwards. Below this an install would fit and leave nothing over, so it
; is refused with a reason rather than allowed to produce a full disk.
HIW_MINSEC   equ 4096           ; 2MB in sectors

; How far hd_iapps_scan walks the volume table looking for the apps disk. It
; is a BOUND and not a shared constant: the SDK publishes no volume count, and
; an index past the kernel's own DVOL_MAX is refused by OSAPI_VOL_KIND with no
; I/O at all - so this is deliberately generous, because too small silently
; misses a drive (SPEC.md 18.98 letters a floppy as far out as E:) and too
; large costs a compare.
HIA_VMAX     equ 16

; How deep the copy walks, counting the root as level 0 (SPEC.md 52.10.13).
; It is the size of hd_icopy_tree's explicit level stack and therefore a real
; bound: a folder deeper than this is REFUSED BY NAME rather than skipped,
; because a walk that silently stops copying is the defect this depth exists
; to fix. The deepest folder any disk in this tree carries is RUNCPM/A/0 on
; the combined apps floppy (SPEC.md 74.6) at three levels below the root, and
; six is that with room for a user's own disk.
HIW_DEPTH    equ 6

HII_IDLE     equ 0
HII_ARMED    equ 1
HII_SYSDONE  equ 2
HII_DONE     equ 3

HIW_CHUNKKB  equ 32             ; the copy buffer, KB. A cluster multiple on
                                ; every geometry here (SPEC.md 18.4.4's rule),
                                ; and small enough that a 128KB machine can
                                ; still find it
; ...and what it ASKS for first, so KERNEL.SYS - the biggest file the install
; carries - goes down in one write. That is a SPEED choice since SPEC.md
; 18.4.4 grew OSAPI_FILE_APPEND_SYS; before it, a machine that could not fund
; this was a machine that could not install os8088. `make INSTCHUNK=1` builds
; it at the fallback size, which is how the chunked path gets exercised on a
; machine with plenty of heap - and the chunked path is the ONE that carries
; a system file across three writes, so it is not a path to leave untested.
%ifndef HIW_KMAXKB
HIW_KMAXKB   equ 96
%endif

hd_itpl:    dw 70, 70, HIW_W, HIW_H, hd_s_ititle, hd_iw_paint, 0, hd_iw_click

; -----------------------------------------------------------------------------
; hd_iw_open - put the installer up (module internal)
; in:  the gfx lock is HELD (a Control Panel click)
; -----------------------------------------------------------------------------
hd_iw_open:
    push ax
    push bx
    push si
    mov byte [hd_iw_st], HII_IDLE
    mov word [hd_imsg], hd_s_ipick
    mov byte [hd_iautod], 0xFF  ; a fresh run asks the machine again
    mov al, [hd_sel]
    mov [hd_idev], al           ; BOUND here, tool.inc's hd_tdev reason: the
                                ; Control Panel underneath stays clickable, and
                                ; hd_istate/hd_isecs below describe THIS device
    call hd_iw_scan             ; a fresh table every time, tool.inc's reason:
                                ; the disk can be swapped between two clicks
    call hd_idefwipe            ; ...and the Erase box follows the selection:
                                ; clear over a volume, which is then KEPT,
                                ; set and greyed over anything else. The
                                ; volume we are RUNNING FROM is a volume too,
                                ; so it is offered - to keep, never to erase
                                ; (SPEC.md 52.10.15, which is where 52.10.4.1's
                                ; 'Boot from a floppy to replace it' went)
.win:
    cmp word [hd_iwin], 0
    jne .show
    mov si, hd_itpl
    call OSAPI_WM_CREATE
    jc .out
    mov [hd_iwin], bx
    mov ax, hd_iw_up            ; SPEC.md 13.7/13.8.1: not template words, so
    call OSAPI_WM_ONMOUSEUP     ; a window installs them itself after create
    mov ax, hd_iw_drag
    call OSAPI_WM_ONDRAG
    mov byte [hd_idown], 0
    mov al, 1                   ; WF_SNAP (SPEC.md 11.94): the content origin
    call OSAPI_WM_SNAP          ; stays on a multiple of 8, so every run in
                                ; here takes SPEC.md 6.1's single-store path
                                ; on the two mono adapters - and on VGA every
                                ; glyph of the fallback comes out aligned,
                                ; which is worth more (SPEC.md 11.94). It
                                ; costs 8px drag steps, which for a dialog
                                ; nobody drags twice is not a cost at all
.show:
    mov bx, [hd_iwin]
    call OSAPI_WM_SHOW
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_shut - take it down. DETACH's too: the kernel frees this image the
; moment detach returns, so a window of ours left on screen is a paint through
; freed memory. The unowned species (SPEC.md 38.1), so hiding IS closing.
; -----------------------------------------------------------------------------
hd_iw_shut:
    push bx
    mov bx, [hd_iwin]
    or bx, bx
    jz .out
    call OSAPI_WM_HIDE
.out:
    pop bx
    ret

; hd_iw_repaint - GONE, and worth a line about why. It erased the content and
; ran the whole W_PAINT for any change at all, which is the blank-then-redraw
; this window used to flash with: a click that only changed the caption threw
; away the header, four rows and two buttons to put back the same pixels.
; SPEC.md 52.10.6's three emitters replace it, and nothing calls a full
; repaint of this window any more except W_PAINT itself.

; -----------------------------------------------------------------------------
; hd_iw_scan - what each slot IS, and what it would be as a target
; out: per slot, hd_istate = HIS_* (the verdict: may Install take it) and the
;      State column's facts - hd_ilet, hd_ibody, hd_irsn, hd_itype, hd_isecs
;
; TWO QUESTIONS, and the column used to answer only the second (SPEC.md
; 52.10.4.2). `Ready` said an install would work and nothing about what the
; install would be working ON - which of four slots is the user's C:, which
; is a FAT volume nobody has mounted, which is somebody else's Linux. The
; column says what is THERE now, the letter first when it is on the desktop,
; and the reason after a comma only when the verdict is no.
; -----------------------------------------------------------------------------
hd_iw_scan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_idev]
    call hd_dev_row
    call hd_part_load           ; the table, or a blank one if unreadable
    xor cl, cl
.slot:
    mov al, cl
    call hd_part_ent            ; SI -> the entry
    call hd_iw_slot
    inc cl
    cmp cl, 4
    jb .slot

    ; The selection follows what is usable, so the first click lands on a
    ; target that would work rather than on whatever slot 0 happens to be.
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    je .out
    xor cl, cl
.pick:
    mov bl, cl
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    jne .pnext
    mov [hd_isel], cl
    jmp short .out
.pnext:
    inc cl
    cmp cl, 4
    jb .pick
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_slot - one slot's verdict and State facts (module internal)
; in:  CL = the slot, SI = its entry, DI = the device row
; out: hd_istate/hd_ilet/hd_ibody/hd_irsn/hd_itype/hd_isecs for slot CL
; clobbers: flags
;
; The order is the order of the certainties, tool.inc's: the TABLE first
; (free, foreign, over the ceiling, laid out at another geometry), which costs
; no read, then the one sector a formatted volume has.
; -----------------------------------------------------------------------------
hd_iw_slot:
    push ax
    push bx
    push dx
    mov word [hd_iscur], 0
    mov word [hd_iscur+2], 0
    mov byte [hd_ilcur], 0
    mov byte [hd_ivcur], 0
    xor bx, bx                  ; BX = the reason, 0 = none to give
    mov al, HIS_ALIEN
    mov dx, hd_s_iunread        ; the read of the table FAILED: every slot
    cmp byte [hd_mbrok], HMB_BAD    ; below would read free, which is a
    je .put                         ; fabrication (tool.inc's hd_s_tnomb)
    call hd_part_used
    jnc .used
    push cx                     ; FREE: is there room to make one?
    mov al, cl
    call hd_slot_extent         ; -> [hd_fbase]/[hd_fsecs] (SPEC.md 52.2.1)
    pop cx
    mov al, HIS_NONE
    mov dx, hd_s_iempty
    mov bx, hd_s_inone
    jc .put                     ; no room on the disk for a new one
    xor bx, bx
    mov dx, hd_s_iunpart
    mov ax, [hd_fsecs]
    mov [hd_iscur], ax
    mov ax, [hd_fsecs+2]
    mov [hd_iscur+2], ax
    jmp short .sized
.used:
    mov ax, [si+HP_SECS]
    mov [hd_iscur], ax
    mov ax, [si+HP_SECS+2]
    mov [hd_iscur+2], ax
    call hd_part_isfat
    jnc .fat
    mov al, [si+HP_TYPE]        ; SOMEBODY ELSE'S, and its name is the reason
    call hd_ptype_name          ; DX = the name, 0 = 'Type nnh' at draw time
    mov al, HIS_ALIEN
    jmp short .put
.fat:
    mov dx, hd_s_f16name        ; the TYPE byte's name, as the Format window
    cmp byte [si+HP_TYPE], HPT_FAT12    ; reads it: what the table says
    jne .fname
    mov dx, hd_s_f12name
.fname:
    call hd_iw_letter           ; the letter, and CF = 0 = the KERNEL's own
    pushf                       ; ...which is decided below, after the facts
    mov al, HIS_ALIEN
    mov bx, hd_s_ibig
    call hd_part_big
    jc .popput                  ; past FAT16's ceiling (SPEC.md 18.7.5)
    mov bx, hd_s_tgeom
    call hd_tw_geomok           ; before the read, which a geometry fault
    jc .popput                  ; points at the wrong sector (SPEC.md 52.2.6)
    xor bx, bx
    call hd_fmt_isfat
    jc .unfmt
    mov byte [hd_ivcur], 1      ; a volume: an install may KEEP it (52.10.15)
    jmp short .fmted
.unfmt:
    mov dx, hd_s_traw           ; FAT-typed, nothing in it: Install formats
.fmted:                         ; either way, so this is a fine target
    popf
    jc .sized
    mov al, HIS_LIVE            ; ...UNLESS WE ARE RUNNING FROM IT (SPEC.md
    mov bx, hd_s_tlive          ; 52.10.4.1)
    jmp short .put
.popput:
    popf
    jmp short .put
.sized:
    mov al, HIS_OK
    cmp word [hd_iscur+2], 0
    jne .put
    cmp word [hd_iscur], HIW_MINSEC
    jae .put
    mov al, HIS_SMALL
    mov bx, hd_s_ismall
.put:
    push bx
    mov bl, cl
    xor bh, bh
    mov [bx+hd_istate], al
    mov al, [hd_ilcur]
    mov [bx+hd_ilet], al
    mov al, [hd_ivcur]
    mov [bx+hd_ivol], al
    mov al, [si+HP_TYPE]
    mov [bx+hd_itype], al
    shl bx, 1
    mov [bx+hd_ibody], dx
    pop word [bx+hd_irsn]
    shl bx, 1
    mov ax, [hd_iscur]
    mov [bx+hd_isecs], ax
    mov ax, [hd_iscur+2]
    mov [bx+hd_isecs+2], ax
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_letter - the drive letter a FAT slot is on the desktop as
; in:  CL = the slot, SI = its entry
; out: [hd_ilcur] = 'A'+volume, or 0 if nothing has it mounted;
;      CF = 0 = it is the volume the machine BOOTED from, CF = 1 = it is not
; clobbers: flags
;
; Two tables, because two owners: the kernel's adoption of the boot partition
; is asked through OSAPI_VOL_AT (hd_kvol), and a mount THIS driver made is
; asked across the seam (hd_vol_ask), hd_vols being the resident's.
; -----------------------------------------------------------------------------
hd_iw_letter:
    push ax
    mov al, [hd_idev]
    call hd_kvol                ; AL = the volume, CF = 0 = the kernel's
    jnc .kern
    mov al, [hd_idev]
    mov ah, cl
    call hd_vol_ask             ; AL = the volume, 0FFh = none
    cmp al, 0xFF
    je .none
    add al, 'A'
    mov [hd_ilcur], al
.none:
    pop ax
    stc
    ret
.kern:
    add al, 'A'
    mov [hd_ilcur], al
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_ptype_name - a FOREIGN partition type's name, for the State column
; in:  AL = the table's type byte
; out: DX = its name, or 0 if it is not one we know (drawn as 'Type nnh')
; clobbers: DX (the output), flags
;
; A partition we will not touch still deserves to be recognised: 'Unusable'
; over the user's Linux root says less than 'Linux' does, and says it about
; the one slot they most need to be sure of.
; -----------------------------------------------------------------------------
hd_ptype_name:
    push si
    mov si, hd_ptypes
.next:
    mov dx, [si+1]
    or dx, dx
    jz .out                     ; the terminator: 0 = not one we know
    cmp al, [si]
    je .out
    add si, 3
    jmp short .next
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; hd_iw_paint - W_PAINT.  in: BX = the window, ES = KERNEL_SEG (SPEC.md 20.3)
; -----------------------------------------------------------------------------
hd_iw_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si                  ; W_PAINT hands the window in SI, and
    call OSAPI_WM_CONTENT       ; OSAPI_WM_CONTENT wants it in BX
    mov di, ax                  ; AX = content left, DX = top - the KERNEL's,
    mov bp, dx                  ; never the template's: wm_fit may have clamped
                                ; it. AX and NOT CX, which is what this read
                                ; first and is a whole content drawn a window's
                                ; width to the right of where it belongs

    mov si, hd_s_ihead
    mov cx, di
    add cx, HIW_LX
    mov dx, bp
    add dx, HIW_HY
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the content's own ground
    call OSAPI_FONT_RUN             ; (SPEC.md 52.12)

    call hd_irows_emit
    call hd_ick_emit

    call hd_ibtns_emit
    call hd_imsg_emit

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_button - one framed button (module internal)
; in:  SI = label, AX = x, BX = width, CL = 0 the action button / 1 Close,
;      DI = pane left, BP = pane top
;
; The pen comes from OSAPI_GFX_PEN, so a disabled button carries [gfx_dis]
; as well as CDGRAY and its LABEL dithers on the two 1bpp adapters instead of
; coming out solid black inside a dotted frame (SPEC.md 47 rule 1). Close is
; never disabled: there is always a way out of a window.
;
; CL picks the PREDICATE, not the state: 0 = hd_iact_ok, 1 = always live. One
; predicate per control, shared by the greying, the click refusal and the
; explanation (SPEC.md 47 rule 2) - and ONE for the action button whichever of
; its three captions is on it (SPEC.md 52.10.6.1).
; -----------------------------------------------------------------------------

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; A Control Panel page draws its buttons one at a time, so the record is
; pointed at whichever rect the caller staged. The GESTURE here is the
; panel's own cp_ctl/cp_pgprobe probe (SPEC.md 13.8.4), which is already
; release-fired; what changed is that the DRAW goes through the control.
hdi_btlbl: dw 0
hdi_btflg: dw 0
    OS88UI_BTNREC hdi_btrec, 0, hdi_btlbl, hdi_btflg, 1

hd_iw_button:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [hd_ibx], ax
    mov [hd_ibw], bx            ; latched across the predicate call; SI is not
                                ; any more - os88ui_btn takes the label there,
                                ; so hd_ibl went with the drawing
    mov al, cl                  ; ...and PRESSED if it is the one being held.
    inc al                      ; ASKED FIRST, because CL is the predicate
    cmp al, [hd_idown]          ; selector on the way in and the DIS flag on
    mov dx, 0                   ; the way out - two meanings, one register
    jne .nodn
    mov dx, OS88UI_DOWN
.nodn:
    cmp cl, 1
    je .live
    call hd_iact_ok             ; the ONE predicate, shared by the greying,
    jmp short .pen              ; the refusal and the explanation (47 rule 2)
.live:
    clc
.pen:
    mov cx, 0                   ; CF -> the shared control's DIS flag. `mov`
    adc cx, 0                   ; does not touch flags, so the carry the
                                ; predicate answered in survives into the adc
    mov ax, di                  ; ...and the rect, in screen coordinates
    add ax, [hd_ibx]
    mov [hd_ibrect+0], ax
    add ax, [hd_ibw]
    dec ax
    mov [hd_ibrect+4], ax
    mov ax, bp
    add ax, HIW_BY
    mov [hd_ibrect+2], ax
    add ax, HIW_BH - 1
    mov [hd_ibrect+6], ax
    mov di, cx
    or di, dx                   ; DIS | DOWN, and DIS outranks it inside the
                                ; control, so a greyed button never says it
                                ; will fire (SPEC.md 13.8)
    mov bx, hd_ibrect           ; OS88UI_FILL because a RELEASE redraws this
    or di, OS88UI_FILL          ; button over a pressed one; on a full pass
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [hdi_btlbl], si          ; label and DI its flags, so the record takes
    mov [hdi_btflg], di          ; all three and the picture is identical
    mov [hdi_btrec+OS88UI_BT_RECTS], bx
    mov bx, hdi_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_ibrect:  dw 0, 0, 0, 0       ; the button being drawn, screen coordinates
hd_idown:   db 0                ; which button is DRAWN pressed, 0 = none
                                ; (SPEC.md 13.8.4). hd_isetdown is its one
                                ; writer and hd_iw_button its one reader

; -----------------------------------------------------------------------------
; hd_ibhit - which button is this CONTENT-relative point in?
; in:  CX = content-relative x, DX = content-relative y
; out: AL = 1..2 (the action button, Close), or 0; AH clobbered
;
; One description, read by the press, the drag and the release - and by the
; painter through the same HIW_B* constants. The ids are the predicate
; selector plus one, which is not a coincidence worth hiding: hd_iw_button's
; CL already names the control.
; -----------------------------------------------------------------------------
hd_ibhit:
    cmp dx, HIW_BY - 2
    jl .no
    cmp dx, HIW_BY + HIW_BH
    jge .no
    mov al, 1
    cmp cx, HIW_B0X
    jl .no
    cmp cx, HIW_B0X + HIW_BW0
    jl .out
    inc al
    cmp cx, HIW_B1X
    jl .no
    cmp cx, HIW_B1X + HIW_BW1
    jl .out
.no:
    xor al, al
.out:
    ret

; -----------------------------------------------------------------------------
; hd_ictlpaint - redraw ONE button, in whatever state [hd_idown] says
; in:  AL = 1..2; the lock is held
; out: nothing (all registers preserved)
;
; NOT hd_ibtns_draw: that white-fills the whole 16px band and re-emits both,
; which on every press and every slide is PERFORMANCE.md's double-draw flash -
; the defect the feature exists to avoid, introduced by the feature.
; -----------------------------------------------------------------------------
hd_ictlpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ax
    call hd_iorg                ; DI = content left, BP = content top
    pop ax
    jc .out
    dec al
    mov cl, al                  ; CL = the predicate selector, which IS the id
    or al, al                   ; minus one (see hd_ibhit)
    jnz .b1
    call hd_ib0_label           ; SI = what the action button says in THIS
    mov ax, HIW_B0X             ; stage - asked, never re-derived, because
    mov bx, HIW_BW0             ; hd_ibtns_emit asks the same routine
    jmp short .draw
.b1:
    mov si, hd_s_close
    mov ax, HIW_B1X
    mov bx, HIW_BW1
.draw:
    call hd_iw_button
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_isetdown - AL becomes the button drawn pressed (0 = none), and it DRAWS
; in:  AL = an id or 0; the lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_isetdown:
    push ax
    cmp al, [hd_idown]
    je .out
    push ax
    mov al, [hd_idown]          ; the one being let go, upright again
    or al, al
    jz .take
    mov byte [hd_idown], 0
    call hd_ictlpaint
.take:
    pop ax
    mov [hd_idown], al
    or al, al
    jz .out
    call hd_ictlpaint           ; ...and the one being taken, pressed
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_pt - a SCREEN point to a content-relative one (module internal)
; in:  CX = x, DX = y, SI = the window
; out: CX/DX content-relative; AX, BX, DI, BP clobbered
; -----------------------------------------------------------------------------
hd_iw_pt:
    push si
    mov bx, si
    mov si, dx
    call OSAPI_WM_CONTENT
    mov di, ax
    mov bp, dx
    sub cx, di
    mov dx, si
    sub dx, bp
    pop si
    ret

; -----------------------------------------------------------------------------
; hd_iw_up - W_ONMOUSEUP (SPEC.md 13.7): the three buttons fire HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = the window
; out: nothing
; -----------------------------------------------------------------------------
hd_iw_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .done
    push ax
    call hd_iw_pt
    pop bx                      ; BL = the armed control
    call hd_ibhit
    cmp al, bl
    je .fire
    xor al, al                  ; a different control, or none: CANCELLED
    call hd_isetdown
    jmp short .done
.fire:
    mov byte [hd_idown], 0      ; NOT hd_isetdown: every path below redraws the
                                ; band, hides the window or reboots the
                                ; machine, and drawing this button upright
                                ; first is PERFORMANCE.md's double-draw
    cmp al, 2
    je .close
    call hd_iact_ok             ; the action button's one predicate, whichever
    jc .refused                 ; caption is on it
    cmp byte [hd_iw_st], HII_DONE   ; ...and at the end that caption is
    je .restart                     ; 'Restart', so that is what the click does
                                    ; (SPEC.md 52.10.6.1). The STAGE decides
                                    ; and not a second id: there is one button
                                    ; here, and hd_ibhit cannot tell which verb
                                    ; it is wearing
    call hd_inst_step
    call hd_ick_draw            ; the box is greyed once anything is written
    call hd_ibtns_draw          ; the stage moved, so hd_iact_ok and the
    call hd_imsg_draw           ; caption may both have flipped - and the
    jmp short .done             ; message always did. The ROWS did not:
                                ; hd_iw_scan runs at open, and a stage changes
                                ; no slot's state (SPEC.md 52.10.6)
.close:
    call hd_iw_shut
    jmp short .done
.restart:
    mov al, 1                   ; NOT the flushing kind: the caption
                                ; this button sits under has just told the
                                ; user to take the floppy out, so the Control
                                ; Panel's write would mount an empty drive and
                                ; then say so (SPEC.md 20.10/52.10.6)
    call OSAPI_REBOOT           ; POSTED, not performed: we are holding the
    jmp short .done             ; gfx lock (SPEC.md 20.10)
.refused:
    ; A greyed control explains itself (SPEC.md 47 rule 5), so this says
    ; nothing new - it makes sure the reason is on screen.
    mov word [hd_imsg], hd_s_ibad
    call hd_imsg_draw           ; ONE line: nothing else about the window has
.done:                          ; changed, and a full repaint here flashed
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_drag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press still down
; -----------------------------------------------------------------------------
hd_iw_drag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .done
    push ax
    call hd_iw_pt
    pop bx
    call hd_ibhit
    cmp al, bl
    je .set
    xor al, al                  ; off it: nothing is down
.set:
    call hd_isetdown            ; ...which draws only if that CHANGED
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Drawing the face a piece at a time (SPEC.md 52.10.6)
;
; NOTHING HERE BLANKS AND THEN LETTERS. Every text element is one opaque
; OSAPI_FONT_RUN space-padded to HIW_CELLS, so a cell goes from what it was to
; what it will be in a single store (SPEC.md 6.1) and there is no frame in
; which the band is empty. The window carries OSAPI_WM_SNAP and HIW_LX is a
; multiple of 8, which is what puts those runs on the single-store path.
;
; The three emitters take the content origin in DI/BP - the painter already
; has it - and the three *_draw entry points fetch it for a caller that does
; not. A click spends only the one whose content actually changed: the message
; alone for an arm or a refusal, the rows for a selection, the buttons only
; when a greying predicate flipped.
;
; The button band is the ONE thing still erased before it is drawn, because a
; frame cannot be drawn opaquely: a dithered frame over a solid one leaves the
; solid one's pixels behind. It is a single 16px-high fill and it happens only
; on a state change, which is the "massively more expensive not to" case.
; =============================================================================

; -----------------------------------------------------------------------------
; hd_iorg - the installer window's content origin (module internal)
; out: CF=0 with DI = content left, BP = content top; CF=1 = no window, or it
;      is not visible, and the caller must draw nothing
; -----------------------------------------------------------------------------
hd_iorg:
    push ax
    push bx
    push cx
    push dx
    mov bx, [hd_iwin]
    or bx, bx
    jz .no
    call OSAPI_WM_GEOM          ; the visibility question; CX/DX are its answer
    jc .no
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top - the
    mov di, ax                  ; KERNEL's, never the template's (wm_fit)
    mov bp, dx
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

; -----------------------------------------------------------------------------
; hd_ipad - space-pad hd_line out to HIW_CELLS and re-terminate it
; out: nothing (all registers preserved)
;
; What makes a run OPAQUE over the whole band rather than over its own text:
; a space paints background on the fast path, so the padding IS the erase.
; -----------------------------------------------------------------------------
hd_ipad:
    push ax
    push cx
    push si
    mov si, hd_line
    xor cx, cx
.len:
    cmp cx, HIW_CELLS
    jae .cap
    cmp byte [si], 0
    je .pad
    inc si
    inc cx
    jmp short .len
.pad:
    mov byte [si], ' '
    inc si
    inc cx
    cmp cx, HIW_CELLS
    jb .pad
.cap:
    mov byte [si], 0
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_irun - hd_line as one opaque run (module internal)
; in:  DI/BP = content origin, DX = the row's y OFFSET inside the content,
;      AL = ink, AH = background
; -----------------------------------------------------------------------------
hd_irun:
    push cx
    push dx
    push si
    clc                         ; A LIVE PEN, every time. FONT_RUN takes its
    call OSAPI_GFX_PEN          ; ink and background in AX, but [gfx_dis] is a
                                ; separate piece of state (SPEC.md 47 rule 1)
                                ; and masks a glyph to a checkerboard whatever
                                ; the ink says - so a greyed BUTTON drawn just
                                ; before this left the caption and every row
                                ; dithered. Preserves every register
    call hd_ipad
    mov cx, di
    add cx, HIW_LX
    add dx, bp
    mov si, hd_line
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_irows_emit / hd_irows_draw - the four slot rows
; in (emit): DI/BP = content origin
;
; The selected row is the same run with ink and background swapped, which is
; how the black bar arrives with the text already on it: there is no fill and
; then a letter, so the row cannot be seen empty. The bar it replaced was a
; GFX_FILL as wide as the band followed by FONT_STR over it.
; -----------------------------------------------------------------------------
hd_irows_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    xor cl, cl
.row:
    push cx
    mov [hd_irow], cl
    call hd_iw_rowtext          ; -> hd_line
    mov al, [hd_irow]
    mov ah, HIW_ROWH
    mul ah
    add ax, HIW_R0Y
    mov dx, ax
    mov al, [hd_irow]
    cmp al, [hd_isel]
    mov ax, (CWHITE << 8) | CBLACK      ; AL = ink, AH = background
    jne .go
    mov ax, (CBLACK << 8) | CWHITE
.go:
    call hd_irun
    pop cx
    inc cl
    cmp cl, 4
    jb .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_irows_draw:
    push di
    push bp
    call hd_iorg
    jc .out
    call hd_irows_emit
.out:
    pop bp
    pop di
    ret

; -----------------------------------------------------------------------------
; hd_imsg_emit / hd_imsg_draw - the caption line, and NOTHING else
;
; The whole point of the pair: a click that only changes what the window SAYS
; used to erase the content and repaint every row, both buttons and the
; header, which flashes on every adapter including VGA.
; -----------------------------------------------------------------------------
hd_imsg_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [hd_imsg]
    mov bx, hd_line
.cp:
    mov al, [si]
    mov [bx], al
    inc si
    inc bx
    or al, al
    jnz .cp
    mov dx, HIW_CAPY
    mov ax, (CWHITE << 8) | CBLACK
    call hd_irun
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_imsg_draw:
    push di
    push bp
    call hd_iorg
    jc .out
    call hd_imsg_emit
.out:
    pop bp
    pop di
    ret

; -----------------------------------------------------------------------------
; hd_ib0_label - what the ACTION button says in the stage we are in
; out: SI = the string; every other register preserved, flags clobbered
;
; ONE control, three captions, each naming what the click actually does
; (SPEC.md 52.10.6.1): 'Install' while there is one to do, 'Copy Apps' once
; the volume boots and the click is adding software (SPEC.md 52.10.10), and
; 'Restart' once the apps are on it too. It is a routine rather than a table
; because the answer is a STAGE and not a row - and it is a routine rather
; than a branch because BOTH drawers need it, the full pass and the
; one-button repaint, and a second copy of it is a second opinion about what
; the button says.
; -----------------------------------------------------------------------------
hd_ib0_label:
    mov si, hd_s_iinst
    cmp byte [hd_iw_st], HII_SYSDONE
    je .copy
    cmp byte [hd_iw_st], HII_DONE
    je .rest
    ret
.copy:
    mov si, hd_s_icopy
    ret
.rest:
    mov si, hd_s_irest
    ret

; -----------------------------------------------------------------------------
; hd_ibtns_emit / hd_ibtns_draw - both buttons, greying and all
; -----------------------------------------------------------------------------
hd_ibtns_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    call hd_ib0_label           ; SI = 'Install' / 'Copy Apps' / 'Restart'
    mov ax, HIW_B0X
    mov bx, HIW_BW0
    xor cl, cl
    call hd_iw_button
    mov si, hd_s_close
    mov ax, HIW_B1X
    mov bx, HIW_BW1
    mov cl, 1
    call hd_iw_button
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_ibtns_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call hd_iorg
    jc .out
    mov al, CWHITE              ; the band, once: see the header - a frame is
    call OSAPI_SET_COLOR        ; the one thing that cannot be drawn opaquely
    mov ax, di
    add ax, HIW_LX
    mov bx, bp
    add bx, HIW_BY
    mov cx, di
    add cx, HIW_W - 12
    mov dx, bx
    add dx, HIW_BH - 1
    call OSAPI_GFX_FILL
    call hd_ibtns_emit
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE ERASE BOX (SPEC.md 52.10.15) - 'Erase the partition first'
;
; One check box and not a second button, because it is not a second verb: the
; one action button installs either way, and what the box changes is whether
; the volume's own files survive it. CLEAR is the default over a volume, which
; is then KEPT; over anything else - a free slot, a FAT entry with nothing in
; it - there is nothing to keep, so the box is SET AND GREYED (SPEC.md 47: a
; fact, not a choice). It is greyed too once the install has written anything.
; -----------------------------------------------------------------------------
hd_idefwipe:
    push bx
    mov bl, [hd_isel]
    xor bh, bh
    mov bl, [bx+hd_ivol]
    xor bl, 1                   ; a volume: keep it (0); nothing: erase (1)
    mov [hd_iwipe], bl
    pop bx
    ret

; hd_ick_ok - may the box be clicked? CF = 0 yes. The ONE predicate for the
; greying and the refusal (SPEC.md 47 rule 2)
hd_ick_ok:
    push bx
    cmp byte [hd_iw_st], HII_SYSDONE
    jae .no                     ; the install has written: nothing to choose
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_ivol], 0
    je .no                      ; nothing to keep
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; hd_ickmsg - the caption for the box's current state: the booted-from volume
; says why Install greyed when it is ticked, everything else the neutral line
hd_ickmsg:
    push bx
    mov word [hd_imsg], hd_s_ipick
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_LIVE
    jne .out
    cmp byte [hd_iwipe], 0
    je .out
    mov word [hd_imsg], hd_s_ilbwipe
.out:
    pop bx
    ret

; hd_ick_rect - the box's rect into its record, SCREEN coordinates
; in:  DI/BP = the content origin; out: nothing (all registers preserved)
hd_ick_rect:
    push ax
    mov ax, di
    add ax, HIW_LX
    mov [hd_ickrec+0], ax
    add ax, HIW_CKW - 1
    mov [hd_ickrec+4], ax
    mov ax, bp
    add ax, HIW_CKY
    mov [hd_ickrec+2], ax
    add ax, HIW_CKH - 1
    mov [hd_ickrec+6], ax
    pop ax
    ret

; hd_ick_emit / hd_ick_draw - the box, greyed or live. The label is an opaque
; run and the box is cleared by the glyph, so there is no ground to lay first
; (SPEC.md 13.15.2)
hd_ick_emit:
    push bx
    push di
    call hd_ick_rect
    call hd_ick_ok
    mov di, 0
    jnc .draw
    mov di, OS88UI_DIS
.draw:
    mov bx, hd_ickrec
    call os88ui_chk
    pop di
    pop bx
    ret

hd_ick_draw:
    push di
    push bp
    call hd_iorg
    jc .out
    call hd_ick_emit
.out:
    pop bp
    pop di
    ret

hd_ickrec:  dw 0, 0, 0, 0       ; OS88UI_CK_RECT, screen coordinates
            dw hd_s_ickl        ; OS88UI_CK_LABEL
hd_iwipe:   db 1                ; OS88UI_CK_ON: 1 = format first, 0 = KEEP the
            db 0                ; volume's files (SPEC.md 52.10.15)
hd_s_ickl:  db 'Erase the partition first', 0

; -----------------------------------------------------------------------------
; hd_iact_ok - may the ACTION button be clicked? (module internal)
; out: CF = 0 yes / 1 no.  SPEC.md 47 rule 2's single predicate for it.
;
; ONE predicate for one control, whichever of hd_ib0_label's three captions is
; on it. There were TWO - hd_rest_ok asked whether the stage had reached
; HII_DONE, and this routine's FIRST test was the same compare answering no
; because there is nothing left to install - and they collapse because they
; were always the same question asked from two ends (SPEC.md 52.10.6.1).
;
; At HII_DONE it answers YES, and that is a FACT rather than a guess (rule 3):
; the button says Restart there, and what there is to restart into is the
; install this dialog just did.
; -----------------------------------------------------------------------------
hd_iact_ok:
    push bx
    cmp byte [hd_iw_st], HII_DONE
    je .yes                     ; finished: the button is Restart now
    mov bl, [hd_isel]
    xor bh, bh
    cmp byte [bx+hd_istate], HIS_OK
    je .yes
    cmp byte [bx+hd_istate], HIS_LIVE   ; the volume we are running from may
    jne .no                             ; be KEPT and never erased (SPEC.md
    cmp byte [hd_iwipe], 0              ; 52.10.15): its kernel is in RAM,
    jne .no                             ; its files are replaced through the
.yes:                                   ; same API as anybody's
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_iw_rowtext - one row into hd_line (module internal)
; -----------------------------------------------------------------------------
hd_iw_rowtext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
%ifdef INSTBENCH
    cmp byte [hd_iw_st], HII_DONE   ; SPEC.md 52.10.9: the install is over, so
    jne .slots                      ; the slot list is answering a question
    call hd_bn_rowtext              ; nobody has any more
    jmp .rtout
.slots:
%endif
    mov di, hd_line
    mov si, hd_s_islot
    call hd_scat
    mov al, [hd_irow]
    xor ah, ah
    inc ax
    call hd_utoa
    mov al, HIW_SIZEC           ; the Size column, under its heading
    call hd_itab_to

    mov bl, [hd_irow]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [bx+hd_isecs]
    mov dx, [bx+hd_isecs+2]
    mov cx, ax
    or cx, dx
    jz .nosize
    call hd_iw_size
    jmp short .state
.nosize:
    mov si, hd_s_idash
    call hd_scat
.state:
    mov al, HIW_STATEC          ; ...and the State column, under its own
    call hd_itab_to
    mov bl, [hd_irow]
    xor bh, bh
    mov al, [bx+hd_ilet]        ; 'C: ' when it is on the desktop
    or al, al
    jz .body
    mov [di], al
    inc di
    mov si, hd_s_icolon
    call hd_scat
.body:
    mov al, [bx+hd_itype]
    shl bx, 1
    mov si, [bx+hd_ibody]
    or si, si
    jnz .named
    mov si, hd_s_itype          ; a foreign type with no name: 'Type 2Bh'
    call hd_scat
    call hd_hex2
    mov si, hd_s_ih
.named:
    call hd_scat
    mov si, [bx+hd_irsn]        ; ...and why not, when the answer is no
    or si, si
    jz .done
    push si
    mov si, hd_s_icomma
    call hd_scat
    pop si
    call hd_scat
.done:
    mov byte [di], 0
%ifdef INSTBENCH
.rtout:
%endif
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_itab_to - space-pad hd_line from DI out to column AL
; in:  DI = the write position in hd_line, AL = the column
; out: DI at that column (or where it was, if already past it: a wide field
;      prints wide, hd_utoa's own rule)
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_itab_to:
    push ax
    xor ah, ah
    add ax, hd_line
.pad:
    cmp di, ax
    jae .out
    mov byte [di], ' '
    inc di
    jmp short .pad
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_hex2 - AL as two hex digits at DI, DI advanced
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
hd_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .dig
    mov al, ah
    and al, 0x0F
    call .dig
    pop cx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '9' - 1
.put:
    mov [di], al
    inc di
    ret

%ifdef INSTBENCH
; =============================================================================
; SPEC.md 52.10.9 - what an install COSTS, per phase, in the only two units
; that survive an emulator: int 13h and sectors
;
; `make DISKCNT=1` builds this and the kernel's own instrument (SPEC.md 18.94)
; together, and neither exists in a shipped driver. It is here rather than in
; sysbench because an install cannot be driven from a package: it is the
; driver's own Control Panel page, it erases a partition, and the phases are
; internal to hd_inst_sys.
;
; THE TWO SIDES ARE COUNTED SEPARATELY AND THAT IS THE POINT. The kernel's
; counters stop at dsk_xfer's run loop and a DVK_DRV volume leaves before it
; (SPEC.md 52), so on an install every kernel counter is the FLOPPY - the
; hard disk is invisible to it. hd_bn_hit is the other half, counted where the
; command is issued, so `dev` is what the drive was asked to do and not what
; the kernel asked the driver for: rung 0 splits at the track and the DMA
; page, rung 1 at ATA-1's 255 (SPEC.md 52.1).
;
; Ticks are the 18.2 Hz BIOS counter, which is the machine's own clock and not
; the host's - the one timing an emulator cannot flatter (PERFORMANCE.md).
; =============================================================================
HDB_ROW  equ 10                 ; one row: floppy calls, floppy sectors,
HDB_FC   equ 0                  ; device calls, device sectors, ticks
HDB_FS   equ 2
HDB_DC   equ 4
HDB_DS   equ 6
HDB_TK   equ 8
HDB_NROW equ 4                  ; fmt, sys, apps, and their total

hd_bt:      times HDB_NROW * HDB_ROW db 0
hd_bt0:     times 5 dw 0        ; ...and the bank the current phase started at
hd_bblk:    dw 0                ; SPEC.md 18.94's block in KERNEL_SEG, or 0
hd_bfound:  db 0                ; ...looked for once, found or not
hd_bferr:   dw 0                ; the FERR_/status the failing step answered
hd_s_bfs:   db ' s', 0
hd_s_bfe:   db ' e', 0
hd_s_bfmt:  db 'fmt', 0
hd_s_bsys:  db 'sys', 0
hd_s_bapp:  db 'app', 0
hd_s_btot:  db 'TOT', 0
hd_btab:    dw hd_s_bfmt, hd_s_bsys, hd_s_bapp, hd_s_btot
hd_bnd_cal: dw 0                ; the DEVICE counters, fetched across the seam
hd_bnd_sec: dw 0
hd_bnd_rst: dw 0
hd_bfstep:  db 0                ; which step of hd_icopy_one was running
hd_bfail:   times 32 db 0       ; ...and the caption built from it
hd_bp:      dw 0                ; hd_bn_save's own cursor into hd_bbuf
hd_bvdrv:   db 0                ; ...and the volume it has to put back
hd_bvclus:  dw 0
hd_s_bfile: db 'INSTBNCH.TXT', 0
hd_s_bhdr:  db 'os8088 install, SPEC.md 52.10.9', 13, 10
            db '      fC   fS   dC   dS   tk', 13, 10, 0
            ; ...the ROW's own columns: 3 for the label, then five 5-wide
            ; fields. The window's legend is 5 spaces in and this is 3,
            ; because the caption is drawn at its own x and this is not
hd_s_bbuf:  db 'copy buffer KB', 0
hd_bbuf:    times 256 db 0
            ; the caption is with the other HIMSGs, where the macro exists

; hd_bn_fail - a failed copy says WHICH STEP and what it answered
; in:  AX = the FERR_/status the step returned, [hd_bfstep] = the step
; out: [hd_imsg] -> 'NAME s<step> e<ax>'
;
; SPEC.md 52.10.9. The shipped installer names the FILE, which is the one
; useful thing a user can act on. It is not enough to DEBUG with: nine call
; sites in hd_icopy_one answer CF=1 and the caption is the same for all of
; them, so a field failure says which file and leaves which half open. The
; steps are, in order: 1 stand in the source, 2 stat, 3 read whole, 4 stand
; in the destination, 5 write whole, 6 stand in the source (chunked), 7 read
; chunk, 8 stand in the destination (chunked), 9 write chunk.
hd_bn_fail:
    push ax
    push cx
    push si
    push di
    mov [hd_bferr], ax
    mov di, hd_bfail
    mov si, hd_iname
    call hd_scat
    mov si, hd_s_bfs
    call hd_scat
    mov al, [hd_bfstep]
    xor ah, ah
    call hd_utoa
    mov si, hd_s_bfe
    call hd_scat
    mov ax, [hd_bferr]
    call hd_utoa
    mov byte [di], 0
    mov word [hd_imsg], hd_bfail
    pop di
    pop si
    pop cx
    pop ax
    ret

; hd_bn_find - SPEC.md 57's registry, once. A kernel that publishes no 'DD'
; block leaves [hd_bblk] at 0 and every floppy column reads 0, which is what
; a reader that cannot find its block is supposed to do.
hd_bn_find:
    push ax
    push bx
    push si
    push es
    mov word [hd_bblk], 0
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [es:0x000E]
    or si, si
    jz .out
.scan:
    mov bx, [es:si]
    or bx, bx
    jz .out
    cmp bx, DBG_TAG_DISK
    je .hit
    add si, 4
    jmp short .scan
.hit:
    mov bx, [es:si+2]
    cmp word [es:bx], DBG_TAG_DISK  ; the block leads with its own tag, so a
    jne .out                        ; followed offset can be checked
    mov [hd_bblk], bx
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; hd_bn_dev - the DEVICE counters, across the seam (SPEC.md 52.10.9/52.11)
;
; They are incremented in the TRANSPORT image and read here, so this is an
; HSV_CTRS call rather than three memory reads. Preserves every register.
hd_bn_dev:
    push ax
    push si
    mov si, hd_req
    mov al, HSV_CTRS
    call hd_svc_call
    jc .none
    mov ax, [hd_req+HRQ_C0]
    mov [hd_bnd_cal], ax
    mov ax, [hd_req+HRQ_C1]
    mov [hd_bnd_sec], ax
    mov ax, [hd_req+HRQ_C2]
    mov [hd_bnd_rst], ax
    pop si
    pop ax
    ret
.none:                          ; a resident with no instrument in it: report
    mov word [hd_bnd_cal], 0    ; nothing rather than whatever was there
    mov word [hd_bnd_sec], 0
    mov word [hd_bnd_rst], 0
    pop si
    pop ax
    ret

; hd_bn_kern - out: AX = the kernel's int 13h count, DX = its sectors
hd_bn_kern:
    push bx
    push es
    mov bx, [hd_bblk]
    or bx, bx
    jz .none
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx+6]
    mov dx, [es:bx+4]
    jmp short .out
.none:
    xor ax, ax
    xor dx, dx
.out:
    pop es
    pop bx
    ret

; hd_bn_bank - a phase starts here. Preserves every register.
hd_bn_bank:
    push ax
    push dx
    cmp byte [hd_bfound], 0
    jne .have
    mov byte [hd_bfound], 1
    call hd_bn_find
.have:
    call hd_bn_dev              ; ...across the seam (SPEC.md 52.11)
    call hd_bn_kern
    mov [hd_bt0+HDB_FC], ax
    mov [hd_bt0+HDB_FS], dx
    mov ax, [hd_bnd_cal]
    mov [hd_bt0+HDB_DC], ax
    mov ax, [hd_bnd_sec]
    mov [hd_bt0+HDB_DS], ax
    call OSAPI_GET_TICKS
    mov [hd_bt0+HDB_TK], ax
    pop dx
    pop ax
    ret

; hd_bn_take - in: AL = the phase (0..2). Preserves every register.
hd_bn_take:
    push ax
    push bx
    push dx
    mov bl, al
    mov al, HDB_ROW
    mul bl                      ; AX = the row's offset
    add ax, hd_bt
    mov bx, ax
    call hd_bn_dev
    call hd_bn_kern
    sub ax, [hd_bt0+HDB_FC]
    mov [bx+HDB_FC], ax
    mov ax, dx
    sub ax, [hd_bt0+HDB_FS]
    mov [bx+HDB_FS], ax
    mov ax, [hd_bnd_cal]
    sub ax, [hd_bt0+HDB_DC]
    mov [bx+HDB_DC], ax
    mov ax, [hd_bnd_sec]
    sub ax, [hd_bt0+HDB_DS]
    mov [bx+HDB_DS], ax
    call OSAPI_GET_TICKS
    sub ax, [hd_bt0+HDB_TK]     ; modular: the tick counter wraps at midnight
    mov [bx+HDB_TK], ax
    pop dx
    pop bx
    pop ax
    ret

; hd_bn_total - the fourth row. Preserves every register.
hd_bn_total:
    push ax
    push bx
    push si
    xor si, si
.field:
    mov bx, hd_bt
    add bx, si
    mov ax, [bx]
    add ax, [bx+HDB_ROW]
    add ax, [bx+2*HDB_ROW]
    mov [bx+3*HDB_ROW], ax
    add si, 2
    cmp si, HDB_ROW
    jb .field
    pop si
    pop bx
    pop ax
    ret

; hd_bn_save - the table to INSTBNCH.TXT on the floppy (module internal)
;
; The rows on screen are the answer and this is how it leaves the machine. It
; goes to the FLOPPY and not to the volume just installed, because on the
; machine this is for there is ONE floppy drive and an ST-225
; (docs/FIELD-MACHINES.md): a file on C: has to be booted to be read, and a
; file on the disk already in the drive comes home in a pocket.
;
; Failure is SILENT and that is deliberate - a write-protected or full floppy
; must not turn a finished install into an error, and the four rows are still
; on screen either way.
hd_bn_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; where the user is, to put them back
    mov [hd_bvdrv], bl
    mov [hd_bvclus], dx
    xor dx, dx                      ; A:, root - the disk in the drive
    xor bl, bl
    call OSAPI_FILE_GOTO
    jc .out
    mov di, hd_bbuf
    mov si, hd_s_bhdr               ; a header, so a photograph of the file
    call hd_scat                    ; says what its columns are
    mov [hd_bp], di
    mov si, hd_s_bbuf               ; ...and WHAT BUFFER the copy got, because
    call hd_scat                    ; hd_ibuf_get's fallback is invisible from
    mov [hd_lineptr], di            ; the outside and changes how every big
    mov ax, [hd_ibufkb]             ; file is written (SPEC.md 52.10.11). A
    call hd_utoa4                   ; field report that does not say which of
    mov di, [hd_lineptr]            ; the two sizes was in play cannot settle
    mov word [di], 0x0A0D           ; an argument about the heap - and one has
    add di, 2                       ; already had to be settled by guessing
    mov [hd_bp], di                 ; ...our OWN cursor: hd_bn_rowtext builds
    mov byte [hd_irow], 0           ; into hd_line and resets [hd_lineptr]
.row:
    call hd_bn_rowtext              ; the same builder the screen uses, so the
    mov di, [hd_bp]                 ; file and the window cannot disagree
    mov si, hd_line
    call hd_scat
    mov word [di], 0x0A0D           ; CR LF: this is read on a DOS box
    add di, 2
    mov [hd_bp], di
    inc byte [hd_irow]
    cmp byte [hd_irow], HDB_NROW
    jb .row
    mov byte [hd_irow], 0           ; ...and leave it where the painter wants
    mov cx, [hd_bp]
    sub cx, hd_bbuf
    xor dx, dx
    push cs
    pop es
    mov bx, hd_bbuf
    mov si, hd_s_bfile
    call OSAPI_FILE_WRITE
.out:
    mov dx, [hd_bvclus]             ; back where the user was, whatever
    mov bl, [hd_bvdrv]              ; happened above (SPEC.md 51.5.2)
    call OSAPI_FILE_GOTO
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hd_bn_rowtext - one report row into hd_line, for [hd_irow] (module internal)
hd_bn_rowtext:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, hd_line
    mov [hd_lineptr], di
    mov bl, [hd_irow]
    xor bh, bh
    shl bx, 1
    mov si, [bx+hd_btab]
    call hd_scat
    mov al, [hd_irow]
    mov ah, HDB_ROW
    mul ah
    add ax, hd_bt
    mov bx, ax
    xor si, si
.field:
    mov di, [hd_lineptr]
    mov byte [di], ' '
    inc di
    mov ax, [bx+si]
    call hd_utoa4               ; wide rather than truncated, if it overflows
    add si, 2
    cmp si, HDB_ROW
    jb .field
    mov di, [hd_lineptr]
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; hd_iw_size - DX:AX sectors as '31M', '970K' or '12G' at DI, DI advanced
;
; KB as well as MB, because SPEC.md 52.2.1's alignment gaps are real
; partitions: a disk repartitioned by a modern tool leaves ~1,985 sectors at
; the front, and a size column reading '0M' looks like a broken tool rather
; than a small volume.
; -----------------------------------------------------------------------------
hd_iw_size:
    push ax
    push bx
    push cx
    push dx
    push si
    or dx, dx
    jnz .mb
    cmp ax, 2048
    jae .mb
    shr ax, 1                   ; sectors / 2 = KB
    call hd_utoa
    mov si, hd_s_ik
    jmp short .unit
.mb:
    mov cx, 11                  ; sectors / 2048 = MB, a 32-bit shift: a
.shift:                         ; partition is not bounded by a word of
    shr dx, 1                   ; sectors (SPEC.md 18.7.5)
    rcr ax, 1
    loop .shift
    mov si, hd_s_mb
    or dx, dx                   ; past 65,535MB, say it in GB
    jnz .gb
    cmp ax, 10000
    jb .num
.gb:
    mov cx, 10
.gshift:
    shr dx, 1
    rcr ax, 1
    loop .gshift
    mov si, hd_s_ig
.num:
    call hd_utoa
.unit:
    call hd_scat
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iw_click - W_ONCLICK.  in: BX = window, CX = x, DX = y, content-relative
; -----------------------------------------------------------------------------
hd_iw_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si                  ; W_ONCLICK's CX/DX are SCREEN coordinates and
    mov si, dx                  ; the window is in SI - tool.inc's conversion,
    call OSAPI_WM_CONTENT       ; and the reason this file had none was an
    mov di, ax                  ; assumption that they arrive content-relative.
    mov bp, dx                  ; They do not, and every y then compared past
    sub cx, di                  ; the button row, so nothing was ever hit
    mov dx, si
    sub dx, bp
    mov [hd_iclx], cx
    mov [hd_icly], dx

    cmp dx, HIW_BY - 2
    jl .notbtn
    ; SPEC.md 13.6/13.8.4: the action button writes a disk or reboots the
    ; machine and Close ends the window - neither has a safe prefix action,
    ; so the press only ARMS and DRAWS and hd_iw_up acts.
    call hd_ibhit
    or al, al
    jz .done
    xor ah, ah
    call os88ui_arm
    call hd_isetdown
    jmp short .done
.notbtn:
    cmp dx, HIW_CKY
    jl .rows
    cmp dx, HIW_CKY + HIW_CKH
    jge .done
    call hd_ick_ok              ; greyed: the box is a fact, not a choice
    jc .done
    call hd_ick_rect            ; screen coordinates, as the control wants
    add cx, di
    add dx, bp
    mov bx, hd_ickrec
    call os88ui_chkhit          ; toggled and its mark redrawn, or missed
    jc .done
    cmp byte [hd_iw_st], HII_ARMED  ; an armed Install was armed for the OTHER
    jne .ckmsg                      ; answer: disarm, tool.inc's rule
    mov byte [hd_iw_st], HII_IDLE
.ckmsg:
    call hd_ickmsg
    call hd_ibtns_draw          ; the greying may have flipped (the booted-from
    call hd_imsg_draw           ; volume may be kept, never erased)
    jmp short .done
.rows:
    mov ax, [hd_icly]
    sub ax, HIW_R0Y - 2
    js .done
    mov bl, HIW_ROWH
    div bl                      ; AL = the row
    cmp al, 4
    jae .done
    cmp al, [hd_isel]
    je .done
    mov [hd_isel], al
    mov byte [hd_iw_st], HII_IDLE   ; picking another row disarms, tool.inc's
    call hd_idefwipe                ; rule: anything but the same button again
    call hd_ickmsg
    call hd_ick_draw            ; ...and the box is this slot's default now
    call hd_irows_draw          ; the bar moved, the message changed, and the
    call hd_ibtns_draw          ; disarm may have re-greyed Install. The header
    call hd_imsg_draw           ; did not change and is not redrawn
    ; ...and NO `.refused` arm. It set hd_s_ibad for a click on a greyed
    ; control, and hd_ibhit answers 0 for one - so nothing ever reached it,
    ; which its own comment half conceded: "a greyed control explains itself
    ; (SPEC.md 47 rule 5), so this says nothing new". Rule 5 is still the
    ; answer; the arm was a second copy of it that could not run.
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_step - one click of the stage machine (module internal)
; -----------------------------------------------------------------------------
hd_inst_step:
    push ax
    cmp byte [hd_iw_st], HII_ARMED
    je .go
    cmp byte [hd_iw_st], HII_SYSDONE
    je .apps
    mov byte [hd_iw_st], HII_ARMED  ; ask. There is no dialog to ask in, and a
    mov word [hd_imsg], hd_s_iarm   ; second click on the same button is the
    cmp byte [hd_iwipe], 0          ; (...and what it asks says which install
    jne .out                        ; this is: SPEC.md 52.10.15)
    mov word [hd_imsg], hd_s_iarmk
    jmp short .out                  ; cheapest confirm that cannot be mistaken
                                    ; for the first (SPEC.md 52.2.3)
.go:
    call hd_inst_sys
    jc .failed
    call hd_iapps_find          ; ...and do not ask for a disk that is already
    jc .swap                    ; in the machine (SPEC.md 52.10.5). Falling
    mov [hd_iautod], al         ; straight into .apps is what makes the whole
                                ; install one confirm on a two-drive machine
    jmp short .apps
.swap:
    mov byte [hd_iw_st], HII_SYSDONE
    mov word [hd_imsg], hd_s_iswap
    jmp short .out
.apps:
    call hd_inst_apps
    jc .failed
    call hd_iassoc              ; ...and the volume's OWN association cache,
                                ; built from what is actually on it (SPEC.md
                                ; 52.10.14). LAST, so it sees both disks'
                                ; packages, and SILENT: the MBR is already
                                ; committed and a cache that cannot be written
                                ; is SPEC.md 54.7's absent tier rather than a
                                ; failed install
    mov byte [hd_iw_st], HII_DONE
%ifdef INSTBENCH
    mov word [hd_imsg], hd_s_bdone  ; the rows below are the report now, so the
    call hd_irows_draw              ; caption is their legend (SPEC.md 52.10.9)
%else                               ; - and the rows have to be ASKED to
    mov word [hd_imsg], hd_s_idone  ; redraw: only the caption has a path of
%endif                              ; its own out of the stage machine
    jmp short .out
.failed:
    mov byte [hd_iw_st], HII_IDLE   ; [hd_imsg] already names what went wrong
.out:
    pop ax
    ret

; =============================================================================
; The copy itself
; =============================================================================

; -----------------------------------------------------------------------------
; hd_inst_sys - format the slot, then copy the disk that is in A: now
; out: CF=1 with [hd_imsg] naming what refused
; -----------------------------------------------------------------------------
hd_inst_sys:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [hd_mbrok], HMB_BAD    ; hd_mbr is a fabrication, not this disk's
    je .notable                     ; table: an Install that commits it wipes
                                    ; every partition we could not see
    mov byte [hd_iphase], 0         ; the SYSTEM phase: the system files are
                                    ; exactly what it is here for (52.10.12)
    call hd_ibuf_get            ; the buffer FIRST: a format we cannot follow
    jc .nomem                   ; with a copy is a wiped partition
                                ; ...and NO size gate on top of it, which is a
                                ; deliberate difference from the branch this
                                ; routine was last reviewed on. That gate read
                                ; `cmp word [hd_ibufkb], HIW_KMAXKB / jb
                                ; .small` and refused the install outright on a
                                ; buffer under HIW_KMAXKB, because KERNEL.SYS
                                ; was written whole and a system file could not
                                ; be appended to. Both halves of that reason
                                ; have expired: OSAPI_FILE_APPEND_SYS is the
                                ; door (SPEC.md 19.6.1), hd_icopy_one's species
                                ; test went with it, and the whole/chunked
                                ; choice below is now a plain 32-bit "does it
                                ; fit" - so a small buffer COPIES, slower, and
                                ; hd_ibuf_get's size is a speed choice. Putting
                                ; the gate back would refuse a machine short of
                                ; heap - the field 5150's class - about a file
                                ; the disk it booted from is carrying, which is
                                ; the bug the species test was removed to fix.
                                ; The wiped-partition failure it was written
                                ; against cannot arise here: the small-buffer
                                ; case succeeds instead of failing after the
                                ; format
    mov word [hd_imsg], hd_s_iwork
    call hd_imsg_draw           ; say so BEFORE the writes - a 32MB format is
                                ; hundreds of sectors and the user should not
                                ; watch a frozen window wonder. ONE LINE: the
                                ; rest of the window has not changed, and a
                                ; full repaint here was a flash right before
                                ; a long freeze (SPEC.md 52.10.6)
    call hd_ivol_bank           ; SPEC.md 51.5.2, whatever happens below
    call hd_iunmount_dst        ; ...and NOTHING may still be mounted on the
                                ; slot we are about to erase (SPEC.md 52.10.8)
    call OSAPI_BATCH_BEGIN      ; the interface is frozen from here to the
                                ; return, so the floppy in A: is the same
                                ; floppy (SPEC.md 18.9.3). Nothing closes it:
                                ; the unlock that ends this callback does, and
                                ; that is true of the failure paths too
    call hd_isrc_ck             ; ...and IS THERE A SYSTEM DISK TO COPY? Asked
    jc .nosys                   ; here, one line above the format, because
                                ; every answer below this point is given with
                                ; the destination already erased (52.10.8.1)
%ifdef INSTBENCH
    call hd_bn_bank             ; SPEC.md 52.10.9: the format on its own, which
%endif                          ; is the phase with no file in it at all
    xor ax, ax                  ; the kernel's start in the data area: 0,
    mov [hd_kofs], ax           ; cluster 2, unless a kept volume moves it
    mov [hd_kofs+2], ax
    cmp byte [hd_iwipe], 0      ; KEEP THE VOLUME'S FILES (SPEC.md 52.10.15):
    jne .wipe                   ; no format, and the kernel goes wherever
    call hd_inst_keep           ; there is a free run for it - which commits
    jc .failed                  ; the VBR the moment it lands
    jmp .tree
.wipe:
    call hd_inst_fmt
    jc .failed
%ifdef INSTBENCH
    xor al, al
    call hd_bn_take
    call hd_bn_bank
%endif
    call hd_inst_mount
    jc .failed
    call hd_ilvl_root           ; ...which stands at the ROOT of both volumes,
                                ; and hd_icopy_one stands where the level stack
                                ; says (SPEC.md 52.10.13)
    mov si, hd_s_kernel         ; KERNEL.SYS FIRST and alone, before the walk:
    call hd_icopy_one           ; SPEC.md 52.10.2 needs it at cluster 2
    jc .failed
.tree:
    call hd_icopy_tree
    jc .failed
    call hd_ivol_back
    cmp byte [hd_iwipe], 0      ; a kept volume's VBR went down with its
    je .mbr                     ; kernel, in hd_inst_keep
    call hd_inst_vbr            ; THE COMMIT LIVES HERE, at the end of the
    jc .nboot                   ; SYSTEM phase, because this is the point at
.mbr:
    call hd_inst_mbr            ; which the disk is a working os8088: KERNEL.SYS
    jc .nboot                   ; is at cluster 2 and every driver is written,
                                ; and the apps disk is ADDITIONS. It used to be
                                ; at the end of the APPS phase on the reasoning
                                ; that the MBR is the commit and a half-install
                                ; must not be offered as bootable - which is
                                ; right about a half-formatted disk and wrong
                                ; about this one. The field found it the
                                ; expensive way: an apps copy that stopped on
                                ; one file left a fully-populated partition
                                ; that had never been marked active, so the
                                ; machine said `No active partition` and a
                                ; complete system install was thrown away by a
                                ; failure in the extras (PERFORMANCE.md Part 9
                                ; Set 22's install, SPEC.md 52.10.10)
%ifdef INSTBENCH
    mov al, 1                   ; ...and the phase ends AT the commit now
    call hd_bn_take
%endif
    call hd_ibuf_put
    clc
    jmp short .out
.notable:
    mov word [hd_imsg], hd_s_inomb
    stc
    jmp short .out
.nomem:
    mov word [hd_imsg], hd_s_inomem
    stc
    jmp short .out
.nboot:                         ; the FILES are all there and the disk simply
    mov word [hd_imsg], hd_s_inboot ; will not boot - a different sentence to
    call hd_ibuf_put            ; the user and a different half to debug. The
    stc                         ; volume is already back (SPEC.md 52.10.10 put
    jmp short .out              ; the commit AFTER hd_ivol_back)
.nosys:                         ; NOTHING HAS BEEN ERASED on this path, which
    mov word [hd_imsg], hd_s_inosys ; is the only reason it is worth having
.failed:
    call hd_ivol_back
    call hd_ibuf_put
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_apps - copy the swapped disk, then commit (module internal)
; -----------------------------------------------------------------------------
hd_inst_apps:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [hd_iphase], 1     ; ...and this one adds SOFTWARE to a machine
                                ; that already boots, so nothing hidden and
                                ; system on the source is its business
                                ; (SPEC.md 52.10.12). It is set HERE and not
                                ; cleared at the end: hd_inst_sys sets its own
                                ; on the way in, which is the ordering that
                                ; survives a failed apps phase leaving the
                                ; stage machine at HII_IDLE for a fresh
                                ; Install click
    call hd_ibuf_get
    jc .nomem
    mov word [hd_imsg], hd_s_iwork
    call hd_imsg_draw
%ifdef INSTBENCH
    call hd_bn_bank             ; SPEC.md 52.10.9 - and this phase is the one
%endif                          ; the progress bar has always shown
    call hd_ivol_bank           ; ...which set hd_isrcdrv to the user's volume;
    call OSAPI_BATCH_BEGIN      ; a SECOND batch when the user was asked to swap
                                ; (the ask unlocked the interface and ended the
                                ; first), and a nested one when the apps disk
                                ; was found and this runs on from hd_inst_sys
    mov al, [hd_iautod]         ; an auto-detected apps disk overrides THAT one
    cmp al, 0xFF                ; and never hd_ivdrv, or the restore below puts
    je .look                    ; the user on a drive they were never on
    mov [hd_isrcdrv], al
    mov byte [hd_iautod], 0xFF  ; spent: a second install asks afresh
    jmp short .src
.look:
    ; NO PRE-SWAP ANSWER, SO ASK AGAIN NOW (SPEC.md 52.10.5.1). The banked
    ; volume is where the user was BROWSING when they started the install, and
    ; it was never a statement about where the apps disk is - it is stage 1
    ; that reads the disk the user was standing on, and even stage 1 has to
    ; correct for it (hd_isrc_ck). What has happened since the probe refused
    ; is exactly the thing that changes the answer: the user was asked to swap
    ; disks and did. So this asks the machine the same question stage 1's
    ; probe asks, and SKIPS NOTHING - the drive the probe ruled out held a
    ; system disk a moment ago and holds the apps disk now.
    mov al, 0xFF
    call hd_iapps_scan
    jc .src                     ; NOT FOUND: the banked volume stands, exactly
    mov [hd_isrcdrv], al        ; as it did before any of this existed. The
.src:                           ; three tests define OUR apps disk, and a
    mov al, [hd_isrcdrv]        ; ...UNLESS it is the destination, which a
    cmp al, [hd_idrv]           ; kept install to the booted-from volume
    jne .notdst                 ; makes possible (SPEC.md 52.10.15): the
    mov byte [hd_isrcdrv], 0    ; floppy in A: then
.notdst:
                                ; hand-made floppy carrying only GAMES fails
                                ; all three while being precisely what the user
                                ; meant to copy - so a miss may not become a
                                ; refusal. The scan can only IMPROVE the guess
    call hd_icopy_tree
    jc .failed
    call hd_ivol_back           ; NO commit here: the system phase already
                                ; made the disk bootable (SPEC.md 52.10.10),
                                ; so a failure in this phase costs the apps
                                ; and not the install
%ifdef INSTBENCH
    mov al, 2                   ; the apps phase ends at the COMMIT, so the
    call hd_bn_take             ; two boot records are inside it (SPEC.md
    call hd_bn_total            ; 52.10.9)
    call hd_bn_save             ; ...and off the machine, onto the floppy
%endif
    call hd_ibuf_put
    clc
    jmp short .out
.nomem:
    mov word [hd_imsg], hd_s_inomem
    stc
    jmp short .out
.failed:
    call hd_ivol_back
.failed2:
    call hd_ibuf_put
    stc
    jmp short .out
    ; ...and NO `.nboot` arm here. It said "the files are all there and the
    ; disk simply will not boot", which is a real distinction and no longer
    ; this phase's to make: SPEC.md 52.10.10 moved the boot commit into the
    ; SYSTEM phase, and the branch went with it - that phase has its own
    ; `.nboot` with two live `jc`s into it. This copy stayed behind with
    ; nothing able to reach it.
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ibuf_get / hd_ibuf_put - the copy buffer (module internal)
;
; A heap claim rather than bss, for SPEC.md 50's reason: it is HIW_KMAXKB at
; its largest and the driver's image would carry that on every machine, hard
; disk or not. Refusal is a normal path - the caller says so and stops.
;
; IT WALKS A LADDER, and that is SPEC.md 52.10.11's other half. It used to ask
; twice - 96KB, then 32 - so a machine that could spare 64 was copied at 32,
; three times the int 13h calls for no reason at all. Two sizes is a CLIFF and
; the heap is a continuum; the table is descending, the first that answers
; wins, and the size taken is recorded because it is the only way anyone
; outside can tell which rung a machine landed on (52.10.9's report prints it).
;
; THE SIZE GOES IN AX AND THE SEGMENT COMES BACK IN DX (SPEC.md 50.3). This
; asked in CX and read the answer out of AX, so the buffer segment was
; whatever AX happened to hold - 0x0047 on the machine that found it, which is
; BELOW KERNEL_SEG. Reading 76KB of KERNEL.SYS there writes over the interrupt
; vector table, the BIOS data area and the bottom of the kernel, which is why
; it presented first as a crash to CS=0000:IP=0000 and then as an endless loop
; through damaged kernel code. A claim can never be below the heap base, so
; the value alone was the tell.
; -----------------------------------------------------------------------------
hd_ibuf_get:
    push ax
    push cx
    push dx
    push si
    mov si, hd_ibufsz
.try:
    mov ax, [si]
    or ax, ax
    jz .no                      ; the ladder ran out: not even the smallest
    call OSAPI_MEM_CLAIM
    jnc .got
    add si, 2
    jmp short .try
.got:
    mov ax, [si]
    mov [hd_ibufkb], ax
.have:
    mov [hd_ibufseg], dx
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    mov word [hd_ibufseg], 0
    mov word [hd_ibufkb], 0
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; The ladder, descending, ending in a 0. Every rung is a whole number of KB
; and so a cluster multiple on every geometry this kernel mounts (SPEC.md
; 18.4.4's rule at both ends), and the top one is HIW_KMAXKB so that
; KERNEL.SYS goes down in one write where the heap allows it.
hd_ibufsz:  dw HIW_KMAXKB, 64, 48, HIW_CHUNKKB, 16, 8, 0

; -----------------------------------------------------------------------------
; hd_ibufbytes - the buffer's capacity in BYTES, 32-bit (module internal)
; out: DX:AX; every other register preserved
;
; A routine rather than a shift because HIW_KMAXKB * 1024 does not fit a word,
; and the 16-bit version of this arithmetic is what made the installer refuse
; KERNEL.SYS outright.
; -----------------------------------------------------------------------------
hd_ibufbytes:
    push cx
    mov ax, [hd_ibufkb]
    mov cx, 1024
    mul cx                      ; DX:AX = KB * 1024
    pop cx
    ret

hd_ibuf_put:
    push ax
    push dx
    mov dx, [hd_ibufseg]        ; DX here too, for the same reason
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [hd_ibufseg], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ivol_bank / hd_ivol_bankv / hd_ivol_back - the user's volume (SPEC.md
; 51.5.2)
; BOTH PRESERVE THE FLAGS: each caller's result is in CF and a failed restore
; must not read as a failed install.
;
; TWO ENTRIES BECAUSE THEY ARE TWO STATEMENTS. `hd_ivol_bank` says "remember
; where the user was AND read the files from there", which is what a phase
; wants on the way in; `hd_ivol_bankv` says only the first half, which is what
; a routine that is merely standing somewhere for a moment wants. hd_iksecs
; was the second kind through the first door, and that cost SPEC.md 52.10.5.1
; a whole round: it runs from hd_inst_vbr at the END of the system phase, so
; it quietly put [hd_isrcdrv] back to the user's volume - overwriting
; hd_isrc_ck's A: fallback - and the apps probe then skipped the wrong drive.
; Invisible until something read that byte after the copy, which is exactly
; what the probe now does.
; -----------------------------------------------------------------------------
hd_ivol_bank:
    pushf
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the directory, BL = the drive
    mov [hd_isrcdrv], bl        ; the copy reads from there too, unless the
                                ; caller overrides it after this returns
    jmp short hd_ivol_bank_v2
hd_ivol_bankv:
    pushf
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE
hd_ivol_bank_v2:
    mov [hd_ivcwd], dx
    mov [hd_ivdrv], bl
    pop dx
    pop bx
    pop ax
    popf
    ret

hd_ivol_back:
    pushf
    push ax
    push bx
    push dx
    mov dx, [hd_ivcwd]
    mov bl, [hd_ivdrv]
    call OSAPI_FILE_GOTO        ; a failed restore is SWALLOWED by the popf,
    pop dx                      ; deliberately - drv_vol_back's rule
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; hd_ifdd2 - has this machine a SECOND floppy drive? (module internal)
; out: CF = 0 = yes; CF = 1 = one drive, or none
; clobbers: flags
;
; int 11h is the only honest source, bits 7:6 being the count less one, and
; the kernel's volume table is no help AT THIS ONE QUESTION: rows 0 and 1 are
; live on every machine (SPEC.md 18.98's `.zloop` takes the ZONE away and
; keeps the row, because [disk_drive] may still name it and a mount of an
; absent drive is an ordinary failed mount), so B: answers VK_REMOVABLE on a
; one-drive PC exactly as it does on a two-drive one - where the BIOS aliases
; unit 1 onto the same physical drive, so a probe of B: reads the disk that is
; in A: and answers a question nobody asked.
;
; UNITS 2 AND 3 NEED NO TEST OF THEIR OWN: SPEC.md 18.98 only MAKES those rows
; when the machine claims the drives, so the row existing is already the
; answer and hd_iapps_cand's OSAPI_VOL_KIND is the whole of it.
; -----------------------------------------------------------------------------
hd_ifdd2:
    push ax
    push bx                     ; int 11h answers in AX and says nothing about
    push cx                     ; the rest; BX is the scan's cursor
    int 0x11                    ; the equipment word
    test al, 1                  ; ...any floppy drive at all
    jz .no
    mov cl, 6
    shr al, cl                  ; bits 7:6 = drives - 1
    or al, al
    jz .no
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_iapps_cand - is this volume worth asking about? (module internal)
; in:  BL = a volume index, BH = one to skip (0xFF = none)
; out: CF = 0 = ask it; CF = 1 = skip it
; clobbers: flags. No I/O of any kind: this is what keeps a scan of eight
;           volumes down to the two or three drives a machine really has.
;
; A REMOVABLE volume and nothing else (SPEC.md 18.7.2): the apps disk is a
; floppy, so a hard-disk partition - the destination included - and a
; redirected network volume (SPEC.md 62.9) are out by their KIND rather than
; by their index, which is what makes this right on a machine whose floppies
; are lettered A:, B:, D: and E: (SPEC.md 18.98).
; -----------------------------------------------------------------------------
hd_iapps_cand:
    push ax
    cmp bl, bh
    je .no                      ; the drive we already know the answer for
    cmp bl, 1
    jne .kind
    call hd_ifdd2               ; B: ON A ONE-DRIVE MACHINE IS A:
    jc .no
.kind:
    mov al, bl
    call OSAPI_VOL_KIND
    jc .no                      ; no such volume
    cmp al, VK_REMOVABLE
    jne .no                     ; a hard disk is not where the apps disk is
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_iapps_at - is the disk in THIS drive the apps disk? (module internal)
; in:  BL = a volume index
; out: CF = 0 = yes; CF = 1 = no
; clobbers: flags
;
; IT LEAVES THE VOLUME ON THE CANDIDATE, deliberately: hd_iapps_scan restores
; once at the end instead of once per drive, which is a whole remount saved
; for every candidate that answers no. Both gotos are QUIET (SPEC.md 19.2.2)
; for hd_isrc's reason - nothing here SHOWS a folder, OSAPI_FILE_FIND walking
; raw directory sectors - and the restore at the boundary is the full one,
; which is what pays the [dsk_lstale] debt they leave.
;
; The test for the disk itself is an APPS FOLDER in its root, which is what
; makes it the apps disk (SPEC.md 19.2) and what the system disk does not
; have. Nothing here is a guess: if the folder is not found - a renamed disk,
; a data disk, no disk at all - this refuses and the swap prompt happens
; exactly as it did before.
; -----------------------------------------------------------------------------
hd_iapps_at:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [hd_iafound], 0
    push bx
    xor dx, dx                  ; its ROOT
    call OSAPI_FILE_GOTO_Q
    pop bx
    jc .refuse                  ; no disk in it, or one we cannot read
    xor cx, cx
.scan:
    push es
    push cs
    pop es
    mov di, hd_ifind            ; NOT hd_ient: the tree walk owns that one and
    call OSAPI_FILE_FIND        ; this runs between two of its passes
    pop es
    jc .ended                   ; the end of the root, and no KERNEL.SYS in it
    mov si, hd_ifind            ; A SYSTEM DISK IS NOT THE APPS DISK, and this
    mov di, hd_s_kernel         ; is the test that says so - not the APPS
    call hd_iname_same          ; folder below, which the system disk is going
    jc .refuse                  ; to have too. Same marker drv_cfg_save uses to
                                ; find the system volume (SPEC.md 51.5.1), so
                                ; there is one answer to "which disk is that"
                                ; rather than two that can drift. A driver's
                                ; FILE_FIND sees hidden and system entries
                                ; (SPEC.md 19.7.1's fence), which is what makes
                                ; it able to ask at all
    cmp word [hd_ifind+14], OSAPI_FT_DIR    ; a folder ('..' is FT_UP)
    jne .scan
    mov si, hd_ifind
    mov di, hd_s_iapps
    call hd_iname_same
    jnc .scan
    mov ax, [hd_ifind+16]       ; its first cluster, for the descent below
    mov [hd_iappsc], ax
    mov byte [hd_iafound], 1    ; ...and keep WALKING rather than stopping
    jmp short .scan             ; here, because a KERNEL.SYS further down the
                                ; listing still disqualifies the disk - and it
                                ; is sorted by name (SPEC.md 19.4), so APPS
                                ; comes FIRST and an early exit would miss it
.ended:
    cmp byte [hd_iafound], 0
    je .refuse

    ; --- and it must actually HOLD SOMETHING OF OURS ------------------------
    ; "No KERNEL.SYS" only says the disk is not a system disk, and "a folder
    ; called APPS" is a name anybody's floppy may happen to carry. Neither is
    ; a statement that this is an os8088 apps disk, and the failure that pair
    ; allows is the expensive one: a stranger's floppy copied onto the volume
    ; the user just installed to. So the last test is POSITIVE - at least one
    ; *.O88 inside APPS - which is what the disk exists for and what the copy
    ; is about to move.
    push bx
    mov dx, [hd_iappsc]
    call OSAPI_FILE_GOTO_Q
    pop bx
    jc .refuse
    xor cx, cx
.pscan:
    push es
    push cs
    pop es
    mov di, hd_ifind
    call OSAPI_FILE_FIND
    pop es
    jc .refuse                  ; walked the whole folder and found no package
    cmp word [hd_ifind+14], OSAPI_FT_PKG    ; a PACKAGE, which is what type 1
    jne .pscan                  ; actually means and exactly what is wanted
                                ; here - hd_io88 below then confirms the name
    mov si, hd_ifind
    call hd_io88
    jnc .pscan
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax                      ; ...and a pop leaves CF alone
    ret

; -----------------------------------------------------------------------------
; hd_iapps_scan - WHICH drive has the apps disk in it? (SPEC.md 52.10.5)
; in:  AL = a volume to skip (0xFF = none)
; out: CF = 0 and AL = that volume; CF = 1 = it is not in the machine
; clobbers: AX, flags. The volume is put back on EVERY path, including the
;           ones that stood on a candidate to look at it - the caller has
;           already restored the user's volume by then (SPEC.md 51.5.2) and a
;           probe that moved it would be a navigation nobody asked for.
;
; LOWEST INDEX FIRST, so A: is asked before B: and before SPEC.md 18.98's
; external pair. That is the order the user was told to use - the prompt says
; to insert the apps disk, and the drive they take the system disk out of is
; the one they put it back into - and on a machine where it is wrong the disk
; is found one drive later at the cost of a mount.
; -----------------------------------------------------------------------------
hd_iapps_scan:
    push bx
    mov bh, al                  ; the one to skip, held in BH because BL walks
    xor bl, bl                  ; the volumes and both survive every call below
.next:
    call hd_iapps_cand
    jc .step
    call hd_iapps_at
    jnc .found
.step:
    inc bl
    cmp bl, HIA_VMAX
    jb .next
    call hd_ivol_back
    stc
    jmp short .out
.found:
    call hd_ivol_back           ; ...which preserves the flags AND AX, so the
    mov al, bl                  ; answer is taken after it either way
    clc
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_iapps_find - is the apps disk already in the machine? (SPEC.md 52.10.5)
; in:  the volume is wherever hd_inst_sys left it (the user's, restored)
; out: CF = 0 and AL = that drive; CF = 1 = ask the user to swap
; clobbers: AX, flags. The volume is put back on EVERY path.
;
; THE DRIVE IT SKIPS IS THE ONE STAGE 1 READ, and not the one the user is
; standing on. That disk is a system disk, which hd_iapps_at's KERNEL.SYS test
; would refuse anyway, so skipping it saves a mount rather than deciding
; anything - where skipping [hd_ivdrv] decided a great deal and decided it
; wrong: it is where the user was BROWSING, so a user who had opened a Disk
; window on the drive holding the apps disk was asked to swap in a disk that
; was already in the machine.
; -----------------------------------------------------------------------------
hd_iapps_find:
    mov al, [hd_isrcdrv]
    jmp short hd_iapps_scan

; -----------------------------------------------------------------------------
; hd_isrc / hd_idst - stand in the ROOT of one volume or the other (internal)
;
; A driver has no folder of its own and uses the machine's (SPEC.md 19.2.1),
; so these move the GLOBAL volume and every file call afterwards resolves
; there. Each is a REMOUNT, which is why the copy is structured to make as
; few of them as it can - one per file, not one per chunk.
; -----------------------------------------------------------------------------
; They and their level-stack twins hd_isrc_here / hd_idst_here (SPEC.md
; 52.10.13) ALL TAKE THE QUIET GOTO (SPEC.md 19.2.2), and the reason they may is
; that nothing in this file ever SHOWS a folder: it reads and writes by name,
; and OSAPI_FILE_FIND walks raw directory sectors (dsk_find) rather than the
; mount listing. So the directory scan, the sort and the icon harvest a full
; dsk_chdir pays for are all bought for nothing here - twice per file and
; twice per CHUNK. The write gate survives, which is the thing that could
; have made this fail confusingly: dsk_chdir_q sets [dsk_quiet] and calls the
; REAL dsk_chdir, so [dsk_mntok] is set exactly as before and only the
; listing publish is skipped.
;
; Measured over one whole install (make DISKCNT=1, SPEC.md 18.94, both disks,
; 22 files onto a pristine 31M partition), together with hd_icopy_one's
; dropped terminating pass: 128 mounts -> 76, 1,372 sectors -> 971, 638
; int 13h calls -> 354. Same 22 files at the same clusters, BEVERLY.MOD
; byte-identical, FAT1 == FAT2, no orphans and no cross-links.
;
; It was once tried and reverted, and that is worth keeping because the
; diagnosis was WRONG: the copy failed at the end, the quiet goto was blamed,
; and reverting it changed nothing at all. What actually broke was
; dskw_read_at refusing the very call that should have answered "finished".
;
; THE BOUNDARY KEEPS THE FULL GOTO, and that is the rule rather than an
; omission: hd_ivol_back hands the machine back to the user, and a quiet
; mount there would leave the global listing empty with [dsk_lstale] owed for
; whatever they do next. Quiet inside the loop, full at the boundary.
; hd_iapps_find's probe keeps it too - it runs once, so it buys nothing.
;
; There were four of these: hd_isrc_sub and hd_idst_sub stood in THE folder,
; singular, which is all a one-level walk ever had to name. The level stack
; replaced them with hd_isrc_here / hd_idst_here, which stand in whichever
; folder the walk is in - and cluster 0 is a root to OSAPI_FILE_GOTO, so those
; two subsume these as well.
hd_isrc:
    push ax
    push bx
    push dx
    mov bl, [hd_isrcdrv]        ; the source is where the user was: A: on a
    xor dx, dx                  ; one-floppy machine, at its ROOT
    call OSAPI_FILE_GOTO_Q      ; QUIET (SPEC.md 19.2.2)
    pop dx
    pop bx
    pop ax
    ret

hd_idst:
    push ax
    push bx
    push dx
    mov bl, [hd_idrv]
    xor dx, dx
    call OSAPI_FILE_GOTO_Q      ; QUIET (SPEC.md 19.2.2)
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_icopy_tree - the source volume's tree, folders and folders inside them
;
; ITERATIVE, over an explicit stack of at most HIW_DEPTH levels (SPEC.md
; 52.10.13). It was two hand-unrolled loops - the root's files, then one level
; of folders and THEIR files - and the thing that shape could not say is "a
; folder inside a folder". SYSTEM/APPDATA (SPEC.md 19.9) is exactly that, and
; it is EMPTY, so an install produced a machine on which every application's
; own state folder was missing: each one kept its settings in memory and said
; nothing, which is the tolerant behaviour SPEC.md 19.9 asks for and reads as
; "preferences do not stick on a hard-disk machine". SYSTEM/DOS/OS88NET.COM
; (SPEC.md 62) went the same way with no error anywhere.
;
; THE RECURSION THOSE TWO LOOPS WERE AVOIDING IS STILL AVOIDED, and that was
; the right instinct: this runs on the UI task's stack (SPEC.md 20.6) and open
; recursion is how you overrun one. A descent here pushes six bytes onto three
; arrays and a return pops them, so the stack depth is the same at level 5 as
; at level 0 and the bound is a number rather than a hope.
;
; Level 0 is the ROOT of both volumes: cluster 0 is what OSAPI_FILE_GOTO takes
; for a root, so the root is an ordinary level and nothing below here carries a
; special case for it.
; -----------------------------------------------------------------------------
hd_icopy_tree:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call hd_ilvl_root
.entry:
    call hd_isrc_here           ; back to THIS level's folder on the source:
    jc .fail                    ; the copy moved us
    call hd_ilvl_bx
    mov cx, [hd_iordv+bx]
    push es
    push cs
    pop es
    mov di, hd_ient
    call OSAPI_FILE_FIND        ; the walk is stateless and the ordinal is all
    pop es                      ; of its state (SPEC.md 19.7.1)
    jc .up                      ; FERR_NOENT: the end of THIS directory
    call hd_ilvl_bx
    mov [hd_iordv+bx], cx
    mov ax, [hd_ient+14]
    cmp ax, OSAPI_FT_UP
    je .entry                   ; the synthesized '..' is not ours to copy
    cmp ax, OSAPI_FT_DIR
    je .folder                  ; ...and anything BELOW FT_DIR is a file,
                                ; package or not. The sub-folder loop this
                                ; replaces asked `cmp ax, OSAPI_FT_PKG`, which
                                ; is type 1 - a PACKAGE - so one level down
                                ; only *.O88 was copied and MEDIA/BEVERLY.MOD
                                ; was silently left behind (SPEC.md 19.7.1)
    cmp byte [hd_ilvl], 0
    jne .file                   ; ...and NEITHER IS KERNEL.SYS IN THE ROOT,
    mov si, hd_ient             ; which hd_inst_sys has already written FIRST
    mov di, hd_s_kernel         ; so that it lands on cluster 2 (SPEC.md
    call hd_iname_same          ; 52.10.2). Copying it again is a REPLACE: it
    jc .entry                   ; allocates a fresh chain at the end of the
                                ; volume and frees the one at the front, so
                                ; the kernel ended up at cluster 40 with
                                ; MEDIA sitting on cluster 2 - a volume that
                                ; is perfectly good and cannot be booted,
                                ; because the boot sector reads a flat run
                                ; from the start of the data area rather than
                                ; walking a chain.
                                ;
                                ; IN THE ROOT, because the root is where the
                                ; one it wrote went. The test was implicitly
                                ; root-only when only the root loop ran it;
                                ; now that one loop walks every level it has
                                ; to say so, or a user's own KERNEL.SYS in a
                                ; folder of their own would be skipped for
                                ; sharing a name with the kernel
    mov si, hd_ient             ; ...AND NEITHER IS ASSOC.DAT (SPEC.md
    mov di, hd_s_assoc          ; 52.10.14). It is a per-VOLUME cache whose
    call hd_iname_same          ; app rows carry the CLUSTER of the folder
    jc .entry                   ; each program lives in (SPEC.md 54.7.1), so
                                ; the source's copy describes the FLOPPY's
                                ; layout and is not merely stale on the
                                ; destination, it is wrong in a way the
                                ; kernel cannot tell from right: `.HTM`
                                ; resolves to BROWSER and then to a cluster
                                ; that means nothing on C:. hd_iassoc builds
                                ; the volume its own at the end of the
                                ; install.
                                ;
                                ; ROOT-ONLY like the kernel above, and for a
                                ; reason of its own: asc_root_find looks in
                                ; the root and only there, so a user's own
                                ; ASSOC.DAT in a folder of theirs is an
                                ; ordinary file and is copied.
                                ;
                                ; THE APPS PHASE ALREADY REFUSED IT - SPEC.md
                                ; 52.10.12's species test skips everything
                                ; hidden + system, and its comment names this
                                ; exact hazard. The SYSTEM phase is the one
                                ; that carries it, because that phase's whole
                                ; job is the hidden + system files, so the
                                ; test that was written to keep the cache off
                                ; the destination ran only on the half that
                                ; was never going to bring it
.file:
    call hd_ikeepit             ; a KEPT volume keeps the user's settings
    jnc .entry                  ; (SPEC.md 52.10.15)
    mov si, hd_ient
    call hd_icopy_one
    jc .failm                   ; it named the FILE; do not talk over it
    jmp .entry
.folder:
    mov al, [hd_ilvl]           ; is there room to go down? The stack is a
    inc al                      ; real bound and this is where it binds
    cmp al, HIW_DEPTH
    jae .deep
    mov ax, [hd_ient+16]
    mov [hd_ichildc], ax        ; its cluster on the SOURCE, for the descent
    mov si, hd_ient             ; ...and bank its NAME, because hd_ient is
    mov di, hd_iname2           ; rewritten by the lookup below and by every
    mov cx, 13                  ; file underneath it
    call hd_iname_copy
    call hd_idst_here           ; stand in the PARENT on the destination...
    jc .fail
    mov si, hd_iname2
    call OSAPI_FILE_MKDIR       ; ...and make it there (SPEC.md 18.5.1)
    jc .mkchk
.mkok:
    call hd_idst_child          ; AX = where it landed. LOOKED UP rather than
    jc .fail                    ; remembered, because a re-install finds one
                                ; that already exists - and looked up once per
                                ; FOLDER now, where hd_idst_findsub paid for
                                ; the same scan once per FILE
    push ax                     ; is the folder we go down into one whose
    call hd_ilvl_bx             ; files are SETTINGS? Inherited from the
    shr bx, 1                   ; parent, or named APPDATA (SPEC.md 19.9)
    mov al, [hd_ikeepl+bx]
    push si
    push di
    mov si, hd_iname2
    mov di, hd_s_appdata
    call hd_iname_same
    pop di
    pop si
    adc al, 0
    mov [hd_ikeepl+bx+1], al
    pop ax
    inc byte [hd_ilvl]          ; ...and down
    push ax
    call hd_ilvl_bx
    pop ax
    mov [hd_idstc+bx], ax
    mov ax, [hd_ichildc]
    mov [hd_isrcc+bx], ax
    xor ax, ax
    mov [hd_iordv+bx], ax
    jmp .entry
.mkchk:
    cmp ax, FERR_EXIST          ; a re-install finds its own folders: that is
    je .mkok                    ; not a failure, it is the second time
    jmp short .fail
.up:
    cmp byte [hd_ilvl], 0       ; the end of a folder is a RETURN to its
    je .done                    ; parent, whose own ordinal is already one
    dec byte [hd_ilvl]          ; past the entry we came in through
    jmp .entry
.deep:
    mov word [hd_imsg], hd_s_ideep  ; SPEC.md 52.10.13: a folder we cannot
    jmp short .failm                ; walk is NAMED, not walked past
.done:
    clc
    jmp short .out
    ; A copy that stops names the FILE it stopped on (hd_icopy_one) or says
    ; nothing more than this.
    ;
    ; `.failm` IS THE HALF THAT WAS MISSING, and its absence is most of why
    ; SPEC.md 52.10.7 stayed open. Every path came here, so the generic
    ; sentence was written over hd_icopy_one's careful "NAME THE FILE" on the
    ; way out - the comment above claimed the file was named and the code
    ; guaranteed it never was. The user saw `Disk error` for a fault that had
    ; already identified itself as BEVERLY.MOD, and the session that went
    ; looking for it reached for abort-markers because the message it
    ; already had said nothing. A failure that arrives from a callee which
    ; sets [hd_imsg] keeps that message; only the paths that have nothing
    ; better to say fall into `.fail`.
.failm:
    stc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ilvl_root / hd_ilvl_bx - the walk's level stack (SPEC.md 52.10.13)
;
; hd_ilvl_root puts the walk at level 0, which is the ROOT of both volumes -
; and it is called by hd_inst_sys as well, because KERNEL.SYS is copied on its
; own before the walk starts and hd_icopy_one stands where the stack says.
;
; hd_ilvl_bx answers BX = the current level's index into the three arrays. It
; is a call rather than three inline instructions because every site that
; wants one of them wants it after something that clobbered BX.
; -----------------------------------------------------------------------------
hd_ilvl_root:
    push ax
    xor ax, ax
    mov [hd_ilvl], al
    mov [hd_ikeepl], al
    mov [hd_isrcc], ax
    mov [hd_idstc], ax
    mov [hd_iordv], ax
    pop ax
    ret

hd_ilvl_bx:
    mov bl, [hd_ilvl]
    xor bh, bh
    shl bx, 1
    ret

; -----------------------------------------------------------------------------
; hd_icopy_one - copy ONE file, source folder current on entry
; in:  SI -> its NUL name (hd_ient's, or a literal for KERNEL.SYS)
; out: CF=1 and [hd_imsg] set
;
; ONE SHAPE: a run of chunked takes, however small the file. The chunk is the
; BUFFER, which is a whole number of KB and therefore of clusters on every
; geometry this kernel mounts - the rule that has to hold at BOTH ends, since
; the destination's cluster size need not equal the source's.
;
; **IT WAS TWO SHAPES AND THAT IS WHAT BROKE IT** (SPEC.md 52.10.13). A file
; that fitted the buffer took one OSAPI_FILE_READ and one OSAPI_FILE_WRITE,
; and OSAPI_FILE_READ IS THE TRANSPARENT ONE (SPEC.md 20.14.3): a compressed
; source arrived EXPANDED and was installed as a plain file, hint and all
; gone. The chunked shape used OSAPI_FILE_READ_AT and was raw and correct all
; along - so the two shapes disagreed about the one thing they had to agree
; on, and which one a file got was decided by its size. `README.TXT` is 8,850
; bytes on the disk and 16,304 expanded; it took the whole-file shape, and an
; installed machine carried the bigger one.
;
; Deleting the shape rather than repairing it is the fix, because the defect
; was the SPLIT. For a file that fits, the loop below is the same two calls
; plus a compare - `hd_isrc_here` is already standing in the source, so its
; OSAPI_FILE_GOTO_Q is a compare and a return (SPEC.md 18.9.1).
;
; A SYSTEM file goes through the _SYS twin of each verb (SPEC.md 19.6.1) so it
; lands hidden and system exactly as it is on the floppy. That is also what
; removed this routine's only refusal: append used to meet the two bits the
; create had stamped, so a system file had to fit the buffer WHOLE, and the
; largest file the installer carries - KERNEL.SYS - is a system file.
; -----------------------------------------------------------------------------
hd_icopy_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, hd_iname            ; bank the name: every volume switch below
    mov cx, 13                  ; runs through code that rewrites hd_ient
    call hd_iname_copy
    HDBSTEP 1
    call hd_isrc_here           ; ...and STAND IN THE SOURCE before asking about
    jc .fail                    ; it. The tree walk arrives here already there,
                                ; but hd_inst_sys's KERNEL.SYS call arrives
                                ; straight off the mount with the DESTINATION
                                ; current - where the file it is looking for is
                                ; exactly what does not exist yet
    xor ax, ax
    mov [hd_ioff], ax
    mov [hd_ioff+2], ax
    mov [hd_isys], al

    mov si, hd_iname            ; how big is it, and is it a system file?
    HDBSTEP 2
    call hd_istat
    jc .fail

    ; --- and WHOSE file is it? (SPEC.md 52.10.12) ----------------------------
    ; The APPS phase never writes a system file. The machine it is adding to
    ; already boots, and everything hidden + system on it - KERNEL.SYS, every
    ; *.DRV, SYSTEM.CFG, ASSOC.DAT - was put there by the system phase and
    ; belongs to the volume rather than to the disk in the drive. Copying the
    ; source's over them is at best pointless and at worst a stale driver on
    ; top of a fresh one, or a floppy's association cache (54.7, whose rows
    ; carry SOURCE cluster numbers) on a hard disk.
    ;
    ; It is a SPECIES test and not a name list, so it needs no maintenance as
    ; files are added, and it is the same two bits every other boundary in
    ; this system reads (19.6). hd_icopy_tree's KERNEL.SYS test stays - that
    ; one is about the SYSTEM phase not copying, in its tree walk, the file it
    ; has already written first and alone (52.10.2).
    ;
    ; Skipping is CF = 0: a system file on the apps disk is not a failure, it
    ; is a file that is not the apps disk's business.
    cmp byte [hd_iphase], 0
    je .mine
    cmp byte [hd_isys], 0
    jne .notmine
.mine:

    ; --- the copy (SPEC.md 18.4.4, 52.10.13) ---------------------------------
    ; No species test HERE any more, and its absence is the fix rather than a
    ; simplification. It read `cmp byte [hd_isys], 0 / jne .toobig` - "a system
    ; file cannot be appended to" - which was true of the kernel it was written
    ; against and made the whole install rest on hd_ibuf_get winning its
    ; HIW_KMAXKB claim, because KERNEL.SYS is the biggest file here and a
    ; system one. A machine short of heap took the 32KB fallback and was then
    ; told `A file is too big for this machine` about a file the disk it booted
    ; from is carrying, and a kernel that grew past 96KB would have got the
    ; same answer on every machine. OSAPI_FILE_APPEND_SYS is the door
    ; (SPEC.md 19.6.1); hd_ibuf_get's size is a speed choice now.
.chunk:
    HDBSTEP 6
    call hd_isrc_here
    jc .fail
    HDBSTEP 7
    call hd_iread_chunk         ; AX = bytes taken, 0 = the file is finished
    jc .fail
    or ax, ax
    jz .ok
    mov [hd_itake], ax
    HDBSTEP 8
    call hd_idst_here
    jc .fail
    HDBSTEP 9
    call hd_iwrite_chunk
    jc .fail
    mov ax, [hd_itake]          ; the offset advances by what was TAKEN, and
    add [hd_ioff], ax           ; the next read starts exactly there
    adc word [hd_ioff+2], 0

    call hd_ichunk              ; ...and STOP on a SHORT take, instead of going
    cmp ax, [hd_itake]          ; round to be told so: the extra pass costs a
    ja .ok                      ; source remount and a directory walk to answer
    jmp .chunk                  ; 0 bytes (SPEC.md 52.10.7).
                                ;
                                ; THE SHORT TAKE AND NOT [hd_isize], which is
                                ; what the test used to be. OSAPI_FILE_READ_AT
                                ; is RAW and OSAPI_FILE_FIND's size is what the
                                ; file EXPANDS to (SPEC.md 20.14.3), so on a
                                ; compressed file the offset reached the end of
                                ; the data and never reached that number - the
                                ; loop went round once more for nothing. A take
                                ; shorter than one full chunk is the end of the
                                ; file whatever the file is, which is the only
                                ; terminator a RAW copier can use and the rule
                                ; the whole-file shape got wrong by not having
                                ; a loop at all
.notmine:                       ; SPEC.md 52.10.12: not the apps phase's
.ok:
    clc
    jmp short .out
.fail:
%ifdef INSTBENCH
    call hd_bn_fail                 ; ...and in a benchmark build, WHICH STEP
%else                               ; and what it answered (SPEC.md 52.10.9)
    mov word [hd_imsg], hd_iname    ; NAME THE FILE. A copy that stops has one
%endif                              ; useful thing to say and it is which file
    stc                             ; it stopped on - "Disk error" sent this
                                    ; session hunting the wrong half twice
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_istat - the named file's size and species (module internal)
; in:  SI = the name, source folder current
; out: CF=0 with [hd_isize] (32-bit) and [hd_isys]; CF=1 = it is not there
;
; By FIND rather than by a stat call, because there is no stat slot and
; because FIND is what already answers both questions: the 32-bit size (the
; listing's is clamped to a word) and the attribute that says whether this is
; a system file (SPEC.md 19.7.1).
; -----------------------------------------------------------------------------
hd_istat:
    push bx
    push cx
    push dx
    push si
    push di
    mov [hd_iwant], si
    xor cx, cx
.next:
    push es
    push cs
    pop es
    mov di, hd_ifind
    call OSAPI_FILE_FIND
    pop es
    jc .no
    push si
    push di
    mov si, [hd_iwant]
    mov di, hd_ifind
    call hd_iname_same
    pop di
    pop si
    jnc .next
    mov ax, [hd_ifind+18]
    mov [hd_isize], ax
    mov ax, [hd_ifind+20]
    mov [hd_isize+2], ax
    mov ax, [hd_ifind+16]
    mov [hd_iclus], ax          ; ...and where it starts (SPEC.md 52.10.15)
    mov al, [hd_ifind+13]
    and al, 0x06                ; hidden|system: SPEC.md 19.6's species
    mov [hd_isys], al
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_iread_chunk - one take at [hd_ioff] (module internal)
; out: CF=0 with AX = bytes delivered, 0 = finished
; -----------------------------------------------------------------------------
hd_iread_chunk:
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    call hd_ichunk              ; AX = the take, and it is CAPPED - see there
    mov cx, ax
    mov ax, [hd_ioff]
    mov dx, [hd_ioff+2]
    call OSAPI_FILE_READ_AT     ; out DX:AX = the bytes, 0 at the end
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_ichunk - how many bytes one take moves (module internal)
; out: AX; every other register preserved
;
; The buffer's capacity, CAPPED AT 32,768, and the cap is not tidiness:
; OSAPI_FILE_READ_AT takes its count in CX, so a 64KB buffer asks for 65,536,
; which truncates to **zero** - and a zero-length take is exactly what
; hd_icopy_one reads as "the file is finished", so the destination would be
; left holding whatever had been written so far and the copy would report
; success. A silently truncated KERNEL.SYS is the worst outcome this module
; has, and nothing downstream could catch it.
;
; This routine's predecessor was a `mov cx, ax` on the low half with a comment
; saying the chunk path never ran above 64KB - untrue since HIW_KMAXKB became
; 96 (which lands on 0x8000 and worked by luck), and fatal the moment
; hd_ibufsz gained a 64KB rung.
;
; 32,768 is a power of two and so a whole number of clusters on every geometry
; this kernel mounts, which is SPEC.md 18.4.4's precondition at both ends.
; -----------------------------------------------------------------------------
hd_ichunk:
    push dx
    call hd_ibufbytes           ; DX:AX = the buffer's capacity, 32-bit
    or dx, dx
    jnz .cap                    ; 64KB or more: the low half is a lie
    cmp ax, 32768
    jbe .out
.cap:
    mov ax, 32768
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; hd_iwrite_chunk - the first take CREATES, every later one APPENDS
;
; A SYSTEM file takes the _SYS twin of each verb (SPEC.md 19.6.1), so it is
; created hidden + system and then extended past those bits - which is the
; whole reason a big system file can be copied at all, and why this path has
; no size limit left in it (SPEC.md 18.4.4).
; -----------------------------------------------------------------------------
hd_iwrite_chunk:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [hd_ibufseg]
    xor bx, bx
    mov si, hd_iname
    mov cx, [hd_itake]
    mov ax, [hd_ioff]
    or ax, [hd_ioff+2]
    jnz .append
    xor dx, dx
    cmp byte [hd_isys], 0
    jne .cresys
    call OSAPI_FILE_WRITE       ; offset 0: create (or replace, on a re-run)
    jmp short .out
.cresys:
    call OSAPI_FILE_WRITE_SYS   ; ...hidden + system, as it is on the floppy
    jmp short .out
.append:
    cmp byte [hd_isys], 0
    jne .appsys
    call OSAPI_FILE_APPEND      ; every later chunk, SPEC.md 18.4.4
    jmp short .out
.appsys:
    call OSAPI_FILE_APPEND_SYS  ; ...past the two bits the create stamped
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_isrc_ck - is the source really the SYSTEM DISK? (SPEC.md 52.10.8.1)
; out: CF = 0 and [hd_isrcdrv] names a volume with KERNEL.SYS in its root;
;      CF = 1 = there is no system disk to install FROM
; clobbers: flags
;
; **It runs BEFORE the format, and that is the whole of what it is for.** The
; source is wherever the user was standing (§51.5.2's bank), which is right on
; the machine this was written for - one floppy, and the disk it booted from
; still in it - and is a guess everywhere else: a Disk window left open on B:,
; a RAM disk, a network volume, or the hard-disk partition §52.10.8.1 is about.
; Every one of those reaches `hd_icopy_one` with the destination ALREADY
; ERASED, which turns "I picked the wrong source" into a partition that is
; gone. Asked here it is a refusal that costs one quiet mount and a directory
; walk.
;
; The test is KERNEL.SYS in the root - the same marker `drv_cfg_save` uses to
; find the system volume (§51.5.1) and `hd_iapps_find` uses to rule one OUT,
; so there is one answer in this system to "which disk is that" rather than
; three that can drift.
;
; **The fallback is every FLOPPY, lowest first** (§52.10.15). It was A:
; alone, on the reasoning that the machine booted off a floppy - which stopped
; being true when a kept install made the booted-from volume a target: that
; machine has a blank or nothing in A: and the new system disk wherever the
; user put it. Falling back is silent because the user asked to install os8088
; and not to install whatever they were last looking at - the volume they were
; on is put back afterwards either way ([hd_ivdrv] is untouched here). And the
; DESTINATION is never the source: on the booted-from volume the bank names a
; disk with KERNEL.SYS on it, and it is the one being written.
; -----------------------------------------------------------------------------
hd_isrc_ck:
    push ax
    push si
    mov al, [hd_idev]           ; NEVER THE DESTINATION (SPEC.md 52.10.15): a
    call hd_dev_row             ; kept install may go to the volume the
    call hd_part_load           ; machine booted from, which carries a
    mov al, [hd_isel]           ; KERNEL.SYS and is where a user booted from it
    call hd_part_ent            ; is standing - and a copy of a volume onto
    mov al, [hd_idev]           ; itself replaces each file with the first
    call hd_kvol                ; chunk of itself
    jc .src
    cmp al, [hd_isrcdrv]
    jne .src
    mov byte [hd_isrcdrv], 0
.src:
    call hd_isrc_ck_one         ; where the bank left us...
    jnc .ok
    push bx                     ; ...and then EVERY FLOPPY, lowest first
    mov bh, [hd_isrcdrv]        ; (SPEC.md 52.10.15). It was A: alone, which
    xor bl, bl                  ; is right on the machine this was written
.next:                          ; for and wrong on the one a kept install
    call hd_iapps_cand          ; makes ordinary: booted from its hard disk,
    jc .step                    ; with the new system disk in whichever drive
    mov [hd_isrcdrv], bl        ; is not the one the BIOS wanted a blank in.
    call hd_isrc_ck_one         ; hd_iapps_cand is the apps probe's own filter
    jnc .found                  ; - removable, present, B: only on a two-drive
.step:                          ; machine - so the two searches cannot
    inc bl                      ; disagree about what a floppy is
    cmp bl, HIA_VMAX
    jb .next
    pop bx
.no:
    pop si
    pop ax
    stc
    ret
.found:
    pop bx
.ok:
    pop si
    pop ax
    clc
    ret

; hd_isrc_ck_one - KERNEL.SYS in [hd_isrcdrv]'s root? (module internal)
; The root, always, and hd_isrc rather than hd_isrc_here: the level stack
; belongs to the tree walk and nothing has set it yet, but saying so costs
; nothing and this must not follow it if it ever does.
hd_isrc_ck_one:
    push si
    call hd_isrc                ; the ROOT of the source volume
    jc .out
    mov si, hd_s_kernel
    call hd_istat               ; CF = 1 = not there, and it is not our disk
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; hd_isrc_here / hd_idst_here - the folder the walk is IN (module internal)
;
; The level stack says which one on each volume (SPEC.md 52.10.13), and
; cluster 0 is a root to OSAPI_FILE_GOTO - so a root file and a file five
; folders down take the same two routines and neither has a special case.
;
; They read [hd_ilvl] rather than a "am I in a folder" flag, which is what
; [hd_iinsub] was: one bit can distinguish the root from THE folder and cannot
; distinguish two folders, so every file below the first level went to the
; wrong place or nowhere. hd_inst_sys's lone KERNEL.SYS copy arrives here with
; the stack at level 0 because it calls hd_ilvl_root first.
; -----------------------------------------------------------------------------
hd_isrc_here:
    push ax
    push bx
    push dx
    call hd_ilvl_bx
    mov dx, [hd_isrcc+bx]
    mov bl, [hd_isrcdrv]
    call OSAPI_FILE_GOTO_Q      ; QUIET (SPEC.md 19.2.2)
    pop dx
    pop bx
    pop ax
    ret

hd_idst_here:
    push ax
    push bx
    push dx
    call hd_ilvl_bx
    mov dx, [hd_idstc+bx]
    mov bl, [hd_idrv]
    call OSAPI_FILE_GOTO_Q      ; QUIET (SPEC.md 19.2.2)
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_idst_child - the cluster of the folder named in hd_iname2 (internal)
; in:  the DESTINATION's parent folder is current
; out: CF=0 and AX = its first cluster; CF=1 = it is not there, and that is a
;      real failure - it was made a moment ago or found to exist already - or
;      what is there is a FILE of that name, which a kept volume can hold
;
; The destination folder's cluster is not known until it has been made, and a
; re-install finds one that already exists - so it is LOOKED UP by name rather
; than remembered from the mkdir, which would have to distinguish those cases.
;
; ONCE PER FOLDER, at the descent, where hd_idst_findsub ran this same scan for
; every FILE in the folder: the walk had nowhere to keep the answer and the
; level stack is that place.
; -----------------------------------------------------------------------------
hd_idst_child:
    push cx
    push si
    push di
    xor cx, cx
.next:
    push es
    push cs
    pop es
    mov di, hd_ifind
    call OSAPI_FILE_FIND
    pop es
    jc .out
    mov si, hd_iname2
    mov di, hd_ifind
    call hd_iname_same
    jnc .next
    cmp word [hd_ifind+14], OSAPI_FT_DIR    ; a FILE by that name answered
    je .dir                     ; MKDIR's FERR_EXIST too, and its cluster is
    stc                         ; its data: the walk stops rather than write
    jmp short .out              ; entries into it
.dir:
    mov ax, [hd_ifind+16]
    clc
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_iname_copy - CX bytes from SI to DI (module internal, NUL-safe)
; hd_iname_same - CF=1 when the NUL strings at SI and DI match
; -----------------------------------------------------------------------------
; hd_io88 - does the NUL name at SI end in '.O88'? (module internal)
; out: CF = 1 yes.  Preserves every register.
; -----------------------------------------------------------------------------
hd_io88:
    push ax
    push si
    push di
    mov di, si
.len:
    cmp byte [di], 0
    je .end
    inc di
    jmp short .len
.end:
    sub di, 4                   ; the last four characters, if there are four
    cmp di, si
    jb .no
    mov si, di
    mov di, hd_s_o88
    call hd_iname_same          ; CF is its answer, and it is ours
    jmp short .out
.no:
    clc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
hd_iname_copy:
    push ax
    push cx
    push si
    push di
.next:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .done
    loop .next
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

hd_iname_same:
    push ax
    push si
    push di
.next:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .next
.yes:
    pop di
    pop si
    pop ax
    stc
    ret
.no:
    pop di
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_inst_fmt - partition and format the selected slot (module internal)
; The formatter's own path (SPEC.md 52.3), reached exactly as tool.inc's
; Format reaches it: the entry first, then the volume, boot sector last.
; -----------------------------------------------------------------------------
hd_inst_fmt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_idev]
    call hd_dev_row
    mov al, [hd_isel]
    call hd_slot_extent
    jc .fail
    call hd_fmt_plan
    jc .fail
    mov al, [hd_isel]
    call hd_part_ent
    call hd_ent_fill            ; NOT active yet: the MBR is the commit and it
                                ; is written at the very end (SPEC.md 52.10.4)
    mov al, [hd_idev]
    call hd_dev_row
    call hd_part_chs
    call hd_part_write
    jc .fail
    mov al, [hd_idev]
    call hd_dev_row
    call hd_fmt_run
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ifmt
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_iunmount_dst - NOTHING ON THE DESTINATION DRIVE may be mounted while we
;                   install to it (SPEC.md 52.10.8, 52.10.8.1)
; out: nothing; every register preserved
;
; **This is what "Install stops on KERNEL.SYS" was.** Arriving at Install with
; the destination already mounted is the ORDINARY way to get here - the Hard
; Drive page's own Mount button is two buttons to the left of Install, and a
; Disk window on C: is how anybody would look at what is there before
; replacing it. The format then rewrote the FAT and the root of a volume the
; kernel still had mounted, with its own FAT window (§18.8.1), its banked BPB
; (§18.9.2) and - since §19.2.3 - cached directory sectors, all describing a
; disk that no longer existed. The copy that followed failed on the first
; file and named it, which is `hd_icopy_one` doing exactly what it should and
; reads like the file being at fault.
;
; The existing unmount ran INSIDE hd_inst_mount, after the format: it was
; written to catch the volume the install itself had just left behind, and it
; is too late for a volume the USER left. Both are wanted; this is the one
; that has to be first.
;
; **And the user may be standing on it.** §51.5.2's bank is taken a moment
; before this and names a drive that stops existing, so both halves of it are
; put back to A: when the volume that goes is the one they named -
; `[hd_isrcdrv]` especially, because that is where KERNEL.SYS is read FROM
; and a dead volume there fails the copy just as thoroughly.
;
; **IT IS THE WHOLE DRIVE AND NOT THE DESTINATION SLOT, and that is
; §52.10.8.1.** The slot-only version fixed the case it was written against -
; install to the partition you had mounted - and left the sibling untouched:
; MOUNT is per DEVICE (`hd_mount` walks all four slots), the volume that ends
; up CURRENT is the last one it mounted, and installing to any OTHER slot
; unmounted a partition nobody was standing on while `[hd_isrcdrv]` went on
; naming one that was still there. The copy then read the system files off a
; hard-disk partition - on the machine that reported it, a data volume with
; MOD files and no KERNEL.SYS in it at all. One click of Mount is the whole
; difference between `Done` and `KERNEL.SYS`, on the same disk and the same
; slot, which is what says the destination was never the question: what has
; to be true is that the install is not standing on the drive it is about to
; write.
; -----------------------------------------------------------------------------
hd_iunmount_dst:
    push ax
    push cx
    xor cl, cl                  ; EVERY slot, because one partition of this
.slot:                          ; drive being mounted is enough - it need not
    mov al, cl                  ; be the one being installed to
    mov ah, [hd_idev]
    call hd_tw_unmount_slot     ; -> [hd_tdrop], [hd_tdropv]
    cmp byte [hd_tdrop], 0
    je .next
    mov al, [hd_tdropv]
    cmp al, 0xFF
    je .next
    cmp al, [hd_ivdrv]
    jne .src
    mov byte [hd_ivdrv], 0      ; nowhere to go back to: the system disk
    mov word [hd_ivcwd], 0
.src:
    cmp al, [hd_isrcdrv]
    jne .next
    mov byte [hd_isrcdrv], 0    ; ...and nowhere to read the files from
.next:                          ; per SLOT, because [hd_tdropv] names the last
    inc cl                      ; volume THIS call dropped and the next call
    cmp cl, 4                   ; overwrites it
    jb .slot
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_mount - mount what we just made, and remember its volume (internal)
; out: [hd_idrv] = the kernel's volume index for the destination
; -----------------------------------------------------------------------------
hd_inst_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_isel]           ; belt: hd_inst_sys already dropped it before
    mov ah, [hd_idev]           ; the format, and this costs a scan of four
    call hd_tw_unmount_slot     ; rows. A stale volume on this slot names a
                                ; FAT the format has just replaced
    mov al, [hd_idev]
    call hd_dev_row
    call hd_part_load           ; the table as the format left it
    mov al, [hd_isel]
    mov [hd_pslot], al          ; hd_mount_one reads the SLOT from here and
    call hd_part_ent            ; the entry from SI - it answers no volume
    mov al, [hd_idev]           ; the BANKED device (bound at open), never
                                ; [hd_sel]: the Format window's own open
                                ; re-syncs the highlight, and both windows
                                ; can be up at once
    call hd_mount_one           ; ...and it ANSWERS with the kernel's volume
                                ; index, where the resident's version left a
                                ; row in hd_vols to be gone and found. That
                                ; walk was this file's only sight of the
                                ; volume table, and the table is the resident
                                ; driver's (SPEC.md 52.11)
    mov al, [hd_mvol]
    cmp al, 0xFF
    je .nomount
    mov [hd_idrv], al
    clc
    jmp short .out
.nomount:
    mov word [hd_imsg], hd_s_imnt
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; KEEPING THE VOLUME'S FILES (SPEC.md 52.10.15)
;
; An install that does not format. Everything the install disks carry is
; written over what is there - the kernel, every driver, every package and
; document - and everything else is left alone: the user's own files, their
; SYSTEM.CFG, and whatever an application keeps in an APPDATA folder.
;
; THE KERNEL IS THE HARD PART, because boot/boothd.asm reads it as a flat run
; rather than walking its chain (SPEC.md 52.10.2), so it must land CONTIGUOUS -
; and on a volume with files on it the clusters after the old kernel are
; somebody's. It does not have to land at cluster 2 any more: the sector adds
; BOOTHD_KOFS, the run's offset into the data area, which this writes. What
; has to be arranged is that the kernel's own allocator puts it in ONE run,
; and it does that with nothing but the file API:
;
;   1. hd_kplan reads the FAT and finds the FIRST run of free clusters long
;      enough for the new one, and counts the free clusters below that run -
;      the old kernel's among them when it is one run, because step 2 frees
;      exactly those. A refusal here has written nothing;
;   2. the old KERNEL.SYS is cut to nothing, so its clusters are free (and a
;      kernel that was in pieces is planned for again, from the FAT as it is
;      now);
;   3. a PAD file of exactly that many clusters is written after a fresh mount
;      - disk_mount resets the allocator's rover to cluster 2, and dskw_alloc
;      is next-fit from the rover, so the pad takes precisely the holes below
;      the run and nothing else;
;   4. the kernel is copied, and every cluster it is given is the next one in
;      the run - whether the rover survived a chunk or a remount put it back
;      at 2, the pad has taken everything under it;
;   5. the pad is deleted, the kernel's chain is CHECKED against the plan, and
;      the VBR is written at once with the run's offset: that is the commit.
;
; The window in which the partition does not boot is steps 2-5, and it is
; seconds: the old kernel's clusters were freed in step 2, and the pad or the
; new kernel may be written over them before the VBR points somewhere new. The
; wipe path's window is the whole system phase.
; =============================================================================

; -----------------------------------------------------------------------------
; hd_ikeepit - does a KEPT volume keep ITS copy of this file?
; in:  hd_ient = the source entry; the walk's level stack says where we are
; out: CF = 0 = keep the destination's, do not copy; CF = 1 = copy
; clobbers: flags
;
; SYSTEM.CFG in the root and anything inside an APPDATA are the user's
; SETTINGS, and the install disks carry defaults for both - so on a kept
; volume the destination's copy wins WHERE THERE IS ONE, and the default goes
; down only where there is not.
; -----------------------------------------------------------------------------
hd_ikeepit:
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [hd_iwipe], 0
    jne .copy                   ; a formatted volume has nothing to keep
    call hd_ilvl_bx
    shr bx, 1
    cmp byte [hd_ikeepl+bx], 0
    jne .ask                    ; inside an APPDATA: every file is a setting
    or bx, bx
    jnz .copy
    mov si, hd_ient
    mov di, hd_s_cfg
    call hd_iname_same
    jnc .copy
.ask:
    mov si, hd_ient             ; the name, banked: hd_istat rewrites hd_ifind
    mov di, hd_iname            ; and the destination walk is its own
    mov cx, 13
    call hd_iname_copy
    call hd_idst_here           ; is there one on the DESTINATION?
    jc .copy
    mov si, hd_iname
    call hd_istat
    jc .copy                    ; no: the default goes down
    clc                         ; yes: it is the user's and it stays
    jmp short .out
.copy:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_keep - the kept volume's system phase, up to and including its VBR
; out: CF=1 with [hd_imsg] set; CF=0 = the partition boots the new kernel
; -----------------------------------------------------------------------------
hd_inst_keep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_idev]           ; the partition's base, from the TABLE: the
    call hd_dev_row             ; VBR's HiddSec and every raw read below
    call hd_part_load
    mov al, [hd_isel]
    call hd_part_ent
    mov ax, [si+HP_LBA]
    mov [hd_fbase], ax
    mov ax, [si+HP_LBA+2]
    mov [hd_fbase+2], ax
    mov al, [hd_idev]           ; THE VOLUME THE MACHINE BOOTED FROM is already
    call hd_kvol                ; the kernel's and cannot be remounted by us -
    jc .drv                     ; nor needs to be: its files are replaced
    mov [hd_idrv], al           ; through the same API as anybody's
    mov byte [hd_kgeo], 0       ; ...and at the BPB's own geometry, which is
    jmp short .mounted          ; the one the running kernel reads it by
.drv:
    call hd_inst_mount          ; ...otherwise ours, as after a format
    jc .nomnt
    mov byte [hd_kgeo], 1       ; ...which HDD.DRV writes at the ROW's
.mounted:
    call hd_ilvl_root
    call hd_idst                ; the OLD kernel first: where it starts and
    jc .io                      ; how long it is, so that the plan can count
    mov word [hd_koc], 0        ; its clusters as the free ones the cut will
    mov si, hd_s_kernel         ; make them - nothing is written until the
    call hd_istat               ; plan has found room
    jc .noold
    mov ax, [hd_iclus]
    mov [hd_koc], ax
    mov ax, [hd_isize]
    mov [hd_kosz], ax
    mov ax, [hd_isize+2]
    mov [hd_kosz+2], ax
.noold:
    call hd_isrc                ; how big is the NEW one?
    jc .io
    mov si, hd_s_kernel
    call hd_istat
    jc .io
    call hd_kplan               ; 1. where it goes, and what is below it
    jc .out
    call hd_idst                ; 2. the old one lets go of its clusters. A
    jc .io                      ; zero-length replace through the _SYS verb,
    push es                     ; because it is hidden + system and nothing
    xor bx, bx                  ; else may touch it (SPEC.md 19.6)
    xor cx, cx
    xor dx, dx
    mov si, hd_s_kernel
    call OSAPI_FILE_WRITE_SYS
    pop es
    jc .io
    cmp word [hd_kon], 0        ; the plan counted the old kernel free only
    jne .planned                ; when it was ONE run (hd_kold): otherwise
    cmp word [hd_koc], 2        ; the cut freed clusters it counted as used,
    jb .planned                 ; and the pad is sized from the FAT as it is
    mov word [hd_koc], 0        ; now - which can only have more room
    call hd_kplan
    jc .out
.planned:
    cmp word [hd_kf], 0
    je .kern
    call hd_isrc                ; 3. the pad, after a FRESH mount: the rover
    call hd_idst                ; is back at cluster 2
    jc .io
    call hd_kpad
    jc .padio
.kern:
    mov si, hd_s_kernel         ; 4. the kernel
    call hd_icopy_one
    jc .out                     ; it named the file
    call hd_idst                ; 5. the pad goes, whether or not there was
    mov si, hd_s_kpad           ; one - FERR_NOENT is the answer then
    call OSAPI_FILE_DELETE
    call hd_kverify             ; ...the chain is what was planned...
    jc .out
    mov al, [hd_ivdrv]          ; hd_iksecs banks the volume it stands on,
    push ax                     ; which is the DESTINATION here: the user's
    push word [hd_ivcwd]        ; own goes back round it
    call hd_inst_vbr            ; ...and THE COMMIT
    pop word [hd_ivcwd]
    pop ax
    mov [hd_ivdrv], al
    jc .nboot
    clc
    jmp short .out
.nboot:
    mov word [hd_imsg], hd_s_inboot
    stc
    jmp short .out
.nomnt:
    mov word [hd_imsg], hd_s_ikbpb  ; nothing was formatted, whatever
    stc                             ; hd_inst_mount's own caption says
    jmp short .out
.padio:
    call hd_idst                ; a pad that stopped part-way is not left in
    jc .io                      ; the user's root
    mov si, hd_s_kpad
    call OSAPI_FILE_DELETE
.io:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_kplan - find the new kernel a run (module internal)
; in:  [hd_isize] = its size; [hd_fbase] = the partition's base; the volume
;      has just been committed (every write flushes its FAT, SPEC.md 18.4)
; out: CF=0 with [hd_kr] = the run's first cluster, [hd_kf] = free clusters
;      below it, [hd_kofs] = (run - 2) * spc; CF=1 with [hd_imsg] set
; clobbers: flags
;
; FIRST FIT, because the pad costs a write of every cluster it covers: the
; lowest run long enough has the fewest free holes under it.
; -----------------------------------------------------------------------------
hd_kplan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [hd_idev]
    call hd_dev_row
    mov byte [hd_rawop], 0      ; the volume's BPB
    mov ax, [hd_fbase]
    mov dx, [hd_fbase+2]
    mov bx, hd_sec0
    mov cx, 1
    push ds
    pop es
    call hd_raw
    jc .io
    cmp word [hd_sec0+HF_B_BPS], 512
    jne .bad
    cmp byte [hd_kgeo], 0       ; HDD.DRV writes the kernel at the ROW's
    je .geo                     ; geometry and boothd reads it back at the
    mov ax, [hd_sec0+HF_B_SPT]  ; BPB's, which a kept volume keeps: two
    cmp ax, [di+HDD_SPT]        ; geometries would put it at two places
    jne .geom                   ; (SPEC.md 52.10.15)
    mov ax, [hd_sec0+HF_B_HEADS]
    cmp ax, [di+HDD_HEADS]
    jne .geom
.geo:
    mov al, [hd_sec0+HF_B_SPC]
    xor ah, ah
    or ax, ax
    jz .bad
    mov [hd_kspc], ax
    mov ax, [hd_sec0+HF_B_RSVD]
    mov bx, ax                  ; BX = rsvd, and the FAT's absolute start
    add ax, [hd_fbase]
    mov [hd_kfat], ax
    mov ax, [hd_fbase+2]
    adc ax, 0
    mov [hd_kfat+2], ax
    mov al, [hd_sec0+HF_B_NFATS]
    xor ah, ah
    mul word [hd_sec0+HF_B_FATSZ]   ; DX:AX = both FATs
    or dx, dx
    jnz .bad
    add bx, ax
    jc .bad
    mov ax, [hd_sec0+HF_B_ROOTN]
    add ax, 15
    mov cl, 4
    shr ax, cl                  ; the root's sectors
    add bx, ax                  ; BX = the data area's first sector
    jc .bad
    mov ax, [hd_sec0+HF_B_TOT16]
    xor dx, dx
    or ax, ax
    jnz .tot
    mov ax, [hd_sec0+HF_B_TOT32]
    mov dx, [hd_sec0+HF_B_TOT32+2]
.tot:
    sub ax, bx
    sbb dx, 0
    jc .bad
    cmp dx, [hd_kspc]           ; the quotient must fit a word
    jae .bad
    div word [hd_kspc]          ; AX = clusters
    mov byte [hd_kf12], 1
    cmp ax, 4085
    jb .f12
    mov byte [hd_kf12], 0
.f12:
    inc ax                      ; the highest cluster number
    mov [hd_kmax], ax

    mov ax, [hd_isize]          ; clusters the kernel needs: its sectors,
    mov dx, [hd_isize+2]        ; then clusters, both rounded up
    call hd_kclus
    jc .bad
    mov [hd_kn], ax

    mov word [hd_kchunk], 0xFFFF
    call hd_kold                ; [hd_kon]: the old kernel, if it is one run
    jc .io
    xor si, si                  ; SI = free clusters before the run
    xor di, di                  ; DI = the run's length so far
    mov cx, 2                   ; CX = the cluster
.scan:
    cmp cx, [hd_kmax]
    ja .noroom
    mov ax, cx
    call hd_kfat_get
    jc .io
    or ax, ax
    jz .free
    mov ax, cx                  ; the OLD kernel's run is free too: the cut
    sub ax, [hd_koc]            ; frees exactly it, and nothing is cut until
    cmp ax, [hd_kon]            ; this has found room
    jae .used
.free:
    or di, di
    jnz .grow
    mov [hd_kr], cx             ; a run starts here
.grow:
    inc di
    cmp di, [hd_kn]
    je .found
    jmp short .next
.used:
    add si, di                  ; the run was too short: its clusters are
    xor di, di                  ; free ones BELOW whatever run we find
.next:
    inc cx
    jnz .scan
.noroom:
    mov word [hd_imsg], hd_s_ikroom
    stc
    jmp short .out
.found:
    mov [hd_kf], si
    mov ax, [hd_kr]             ; the offset the VBR adds: (run - 2) * spc
    sub ax, 2
    mul word [hd_kspc]
    mov [hd_kofs], ax
    mov [hd_kofs+2], dx
    clc
    jmp short .out
.bad:
    mov word [hd_imsg], hd_s_ikbpb
    stc
    jmp short .out
.geom:
    mov word [hd_imsg], hd_s_ikgeom
    stc
    jmp short .out
.io:
    mov word [hd_imsg], hd_s_ierr
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
; hd_kclus - the clusters a file of DX:AX bytes takes, rounded up (internal)
; in:  DX:AX = its size; [hd_kspc] from hd_kplan
; out: CF=0 and AX = the clusters, never 0; CF=1 = none, or past a word
; clobbers: AX (the output), DX, flags
; -----------------------------------------------------------------------------
hd_kclus:
    push cx
    add ax, 511                 ; its sectors...
    adc dx, 0
    jc .no
    mov cx, 9
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    add ax, [hd_kspc]           ; ...then its clusters, both rounded up
    adc dx, 0
    sub ax, 1
    sbb dx, 0
    cmp dx, [hd_kspc]           ; the quotient must fit a word
    jae .no
    div word [hd_kspc]
    or ax, ax
    jz .no
    clc
    pop cx
    ret
.no:
    stc
    pop cx
    ret

; -----------------------------------------------------------------------------
; hd_kold - may the plan count the OLD kernel's clusters as free? (internal)
; in:  [hd_koc]/[hd_kosz] = its first cluster and size (a cluster under 2 =
;      there is none); hd_kplan's [hd_kspc]/[hd_kmax]/[hd_kf12]
; out: CF=0 and [hd_kon] = its clusters when they are [hd_koc] onwards in ONE
;      run that ends the chain, else 0; CF=1 = a FAT read failed
; clobbers: flags
;
; Checked link by link, not trusted from the size: what frees them is the cut,
; which frees the CHAIN, so a cluster counted free here has to be one the
; chain really holds. A kernel in pieces counts as used, and hd_inst_keep
; plans again once the cut has freed them.
; -----------------------------------------------------------------------------
hd_kold:
    push ax
    push bx
    push cx
    push dx
    mov word [hd_kon], 0
    mov bx, [hd_koc]
    cmp bx, 2
    jb .ok
    mov ax, [hd_kosz]
    mov dx, [hd_kosz+2]
    call hd_kclus
    jc .ok
    mov dx, ax                  ; DX = the run it would be
    mov cx, ax
    add ax, bx                  ; ...which has to end inside the volume
    jc .ok
    dec ax
    cmp ax, [hd_kmax]
    ja .ok
.link:
    mov ax, bx
    call hd_kfat_get
    jc .out
    dec cx
    jz .last
    inc bx
    cmp ax, bx
    je .link
    jmp short .ok               ; in pieces
.last:
    cmp byte [hd_kf12], 0
    jne .eoc12
    cmp ax, 0xFFF8
    jb .ok
    jmp short .run
.eoc12:
    cmp ax, 0x0FF8
    jb .ok
.run:
    mov [hd_kon], dx
.ok:
    clc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_kfat_get - one FAT entry, read raw (module internal)
; in:  AX = the cluster; [hd_kfat]/[hd_kf12] from hd_kplan
; out: CF=0 and AX = the entry (12 or 16 bits); CF=1 = the read failed
; clobbers: AX (the output), flags
;
; Three sectors at a time into the copy buffer, cached: 1,536 bytes is exactly
; 1,024 FAT12 entries or 768 FAT16 ones, so a chunk never splits an entry and
; the scan reads each FAT sector once.
; -----------------------------------------------------------------------------
hd_kfat_get:
    push bx
    push cx
    push dx
    push di
    push es
    mov cx, 768                 ; entries per chunk
    cmp byte [hd_kf12], 0
    je .per
    mov cx, 1024
.per:
    xor dx, dx
    div cx                      ; AX = the chunk, DX = the entry in it
    push dx
    cmp ax, [hd_kchunk]
    je .have
    mov [hd_kchunk], ax
    mov cx, 3
    mul cx                      ; DX:AX = the chunk's first sector, FAT-relative
    add ax, [hd_kfat]
    adc dx, [hd_kfat+2]
    push ax
    mov al, [hd_idev]
    call hd_dev_row
    pop ax
    mov es, [hd_ibufseg]
    xor bx, bx
    mov byte [hd_rawop], 0
    call hd_raw
    jnc .have
    mov word [hd_kchunk], 0xFFFF
    pop dx
    stc
    jmp short .out
.have:
    pop bx                      ; BX = the entry's index in the chunk
    mov es, [hd_ibufseg]
    cmp byte [hd_kf12], 0
    jne .f12
    shl bx, 1
    mov ax, [es:bx]
    jmp short .ok
.f12:
    mov ax, bx
    shr ax, 1                   ; CF = the index is ODD
    pushf
    add bx, ax                  ; index * 3/2
    mov ax, [es:bx]
    popf
    jc .odd
    and ax, 0x0FFF
    jmp short .ok
.odd:
    mov cl, 4
    shr ax, cl
.ok:
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_kpad - [hd_kf] clusters of pad, as KPAD.TMP in the destination's root
; in:  the destination's root is current, JUST MOUNTED (the rover is at 2)
; out: CF per the file calls
;
; The bytes are whatever the buffer holds: nothing reads them, and the file is
; deleted the moment the kernel is down.
; -----------------------------------------------------------------------------
hd_kpad:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [hd_kf]             ; DI:SI = the bytes still to write
    mul word [hd_kspc]
    mov cx, 9
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
    mov si, ax
    mov di, dx
    mov es, [hd_ibufseg]
    xor bx, bx
    xor ax, ax                  ; AX = 0: the first write creates
.more:
    push ax
    call hd_ichunk              ; AX = a take, a cluster multiple
    mov cx, ax
    pop ax
    or di, di
    jnz .take
    cmp si, cx
    jae .take
    mov cx, si                  ; the last, short one
.take:
    push ax
    push si
    mov si, hd_s_kpad
    or ax, ax
    jnz .app
    xor dx, dx
    call OSAPI_FILE_WRITE
    jmp short .did
.app:
    call OSAPI_FILE_APPEND
.did:
    pop si
    pop ax
    jc .out
    mov ax, 1
    sub si, cx
    sbb di, 0
    mov dx, si
    or dx, di
    jnz .more
    clc
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
; hd_kverify - did the kernel land where hd_kplan said, in ONE run?
; out: CF=0 yes; CF=1 with [hd_imsg] set - and the VBR has NOT been written
;
; Asked of the DISK, because the whole placement rests on an allocator this
; driver does not own: the directory for where the chain starts, the FAT for
; whether each cluster is followed by the next.
; -----------------------------------------------------------------------------
hd_kverify:
    push ax
    push bx
    push cx
    push si
    call hd_idst
    jc .bad
    mov si, hd_s_kernel
    call hd_istat
    jc .bad
    mov ax, [hd_iclus]
    cmp ax, [hd_kr]
    jne .bad
    mov word [hd_kchunk], 0xFFFF    ; the FAT has been written since the scan
    mov bx, ax
    mov cx, [hd_kn]
.link:
    mov ax, bx
    call hd_kfat_get
    jc .bad
    dec cx
    jz .last
    inc bx
    cmp ax, bx
    je .link
    jmp short .bad
.last:
    cmp byte [hd_kf12], 0
    jne .eoc12
    cmp ax, 0xFFF8
    jb .bad
    jmp short .ok
.eoc12:
    cmp ax, 0x0FF8
    jb .bad
.ok:
    clc
    jmp short .out
.bad:
    mov word [hd_imsg], hd_s_iksplit
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_inst_vbr - the volume boot record (SPEC.md 52.10.2)
;
; boot/boothd.asm's 512 bytes with THIS volume's BPB written over the first
; 62, which is os88disk.py's split done in the driver: bytes 0..2 and 62..511
; are the sector's own code and are not ours to touch.
;
; The kernel's sector count goes into the word at BOOTHD_KSECS. That is the
; one patch site and it is pinned at a documented offset precisely so this
; code can find it without knowing anything else about the sector's layout.
; -----------------------------------------------------------------------------
hd_inst_vbr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    push ds
    pop es
    cld
    cmp byte [hd_iwipe], 0      ; A VOLUME WE KEPT KEEPS ITS OWN BPB (SPEC.md
    jne .fresh                  ; 52.10.15): nothing was formatted, so there
    mov al, [hd_idev]           ; is no plan to write one from - the one on
    call hd_dev_row             ; the disk describes the FAT the files are in
    mov byte [hd_rawop], 0
    mov ax, [hd_fbase]
    mov dx, [hd_fbase+2]
    mov bx, hd_sec0
    mov cx, 1
    call hd_raw
    jc .fail
    mov si, hd_bootvbr          ; ...and OUR code over everything but it: the
    mov di, hd_sec0             ; jump, and bytes 62..511
    mov cx, 3
    rep movsb
    mov si, hd_bootvbr + 62
    mov di, hd_sec0 + 62
    mov cx, 512 - 62
    rep movsb
    mov ax, [hd_fbase]              ; the base as the TABLE says it, whatever
    mov [hd_sec0+HF_B_HIDD], ax     ; wrote the volume: it is what the sector
    mov ax, [hd_fbase+2]            ; adds to every LBA
    mov [hd_sec0+HF_B_HIDD+2], ax
    mov byte [hd_sec0+HF_B_DRVNUM], 0x80
    jmp short .kpatch
.fresh:
    mov si, hd_bootvbr          ; our template -> the sector buffer
    mov di, hd_sec0
    mov cx, 512
    rep movsb

    ; --- the BPB, from the volume we just formatted -------------------------
    mov word [hd_sec0+HF_B_BPS], 512
    mov ax, [hd_fspc]
    mov [hd_sec0+HF_B_SPC], al
    mov word [hd_sec0+HF_B_RSVD], HF_RSVD
    mov byte [hd_sec0+HF_B_NFATS], HF_NFATS
    mov word [hd_sec0+HF_B_ROOTN], HF_ROOTENT
    call hd_fmt_tot             ; TotSec16, or TotSec32 past a word
    mov byte [hd_sec0+HF_B_MEDIA], HF_MEDIA
    mov ax, [hd_ffatsz]
    mov [hd_sec0+HF_B_FATSZ], ax
    mov al, [hd_idev]
    call hd_dev_row
    mov ax, [di+HDD_SPT]
    mov [hd_sec0+HF_B_SPT], ax
    mov ax, [di+HDD_HEADS]
    mov [hd_sec0+HF_B_HEADS], ax
    mov ax, [hd_fbase]              ; BPB_HiddSec: the partition base, and
    mov [hd_sec0+HF_B_HIDD], ax     ; what the sector adds to every LBA
    mov ax, [hd_fbase+2]
    mov [hd_sec0+HF_B_HIDD+2], ax
    mov byte [hd_sec0+HF_B_DRVNUM], 0x80
    mov byte [hd_sec0+HF_B_BOOTSIG], 0x29

    ; --- and the kernel's length and start, at the pinned patch sites --------
.kpatch:
    call hd_iksecs                  ; AX = sectors of KERNEL.SYS
    jc .fail
    mov [hd_sec0+BOOTHD_KSECS], ax
    mov ax, [hd_kofs]               ; 0 after a format: the kernel is at
    mov [hd_sec0+BOOTHD_KOFS], ax   ; cluster 2. A kept volume's is wherever
    mov ax, [hd_kofs+2]             ; hd_kplan found it room (52.10.15)
    mov [hd_sec0+BOOTHD_KOFS+2], ax

    mov al, [hd_idev]
    call hd_dev_row
    mov byte [hd_rawop], 1          ; write
    mov ax, [hd_fbase]              ; volume LBA 0, absolute
    mov dx, [hd_fbase+2]
    mov bx, hd_sec0
    mov cx, 1
    push ds
    pop es
    call hd_raw
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
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
; hd_iksecs - how many sectors KERNEL.SYS occupies on the DESTINATION
; out: CF=0 and AX = the count
;
; Asked of the destination rather than remembered from the copy, because that
; is the volume the boot sector will read it from and the question is about
; the file that is actually there - a re-install over a shorter kernel would
; otherwise patch the previous one's length.
; -----------------------------------------------------------------------------
hd_iksecs:
    push bx
    push cx
    push dx
    push si
    call hd_ivol_bankv          ; the VOLUME only: this is standing on the
                                ; destination to read one size, not setting up
                                ; a copy, and [hd_isrcdrv] is hd_isrc_ck's
                                ; answer by the time anything gets here
    call hd_idst
    jc .fail
    mov si, hd_s_kernel
    call hd_istat
    jc .fail
    mov ax, [hd_isize]
    mov dx, [hd_isize+2]
    add ax, 511                     ; ceil(size / 512), in 32 bits: a kernel
    adc dx, 0                       ; near 64KB would carry out of a word
    mov cl, 9
.shr:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .shr
    call hd_ivol_back
    clc
    jmp short .out
.fail:
    call hd_ivol_back
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_inst_mbr - the boot code and the active flag, LAST (SPEC.md 52.10.4)
;
; This is the commit. Everything before it leaves a disk that is merely not
; bootable yet; this one sector is what makes the machine start from it, so
; an install interrupted anywhere earlier still boots from its floppy.
;
; The table is PRESERVED - only bytes 0..445 and the four status bytes change
; - because the other three entries may be somebody else's partitions.
; -----------------------------------------------------------------------------
hd_inst_mbr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [hd_idev]
    call hd_dev_row
    call hd_part_load           ; re-read: the format wrote the table and this
    jc .fail                    ; must not put a stale copy back

    mov si, hd_bootstub         ; the chain-loader over the boot code only
    mov di, hd_mbr
    mov cx, HD_MBRCODE
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es

    xor cl, cl                  ; exactly one entry is active
.flag:
    mov al, cl
    call hd_part_ent
    mov byte [si+HP_STATUS], 0
    cmp cl, [hd_isel]
    jne .fnext
    mov byte [si+HP_STATUS], 0x80
.fnext:
    inc cl
    cmp cl, 4
    jb .flag

    mov al, [hd_idev]
    call hd_dev_row
    call hd_part_write
    jc .fail
    clc
    jmp short .out
.fail:
    mov word [hd_imsg], hd_s_ierr
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Strings and state
;
; EVERY ONE OF THESE FITS ITS BOX. The window is 300px wide, so a caption is
; at most 36 characters at HIW_LX - page.inc's rule, and for its reason:
; font_str clips to the SCREEN and not to the window (SPEC.md 5), so a long
; one paints over the border and out the other side.
; =============================================================================
hd_s_ititle:  db 'Install', 0
hd_s_ihead:   db 'Install to  Size  State', 0   ; HIW_SIZEC, HIW_STATEC
hd_s_iinst:   db 'Install', 0
hd_s_icopy:   db 'Copy Apps', 0
hd_s_irest:   db 'Restart', 0
hd_s_islot:   db 'Slot ', 0
hd_s_idash:   db '-', 0
hd_s_ik:      db 'K', 0
hd_s_kernel:  db 'KERNEL.SYS', 0
hd_s_cfg:     db 'SYSTEM.CFG', 0    ; ...and the two a KEPT volume keeps
hd_s_appdata: db 'APPDATA', 0       ; (SPEC.md 52.10.15)
hd_s_kpad:    db 'KPAD.TMP', 0      ; the allocator's pad, for one moment
hd_s_assoc:   db 'ASSOC.DAT', 0 ; ...the OTHER file the copy may not carry
                                ; (SPEC.md 52.10.14)
hd_s_iapps:   db 'APPS', 0      ; what makes a disk THE APPS DISK (52.10.5)
hd_s_o88:     db '.O88', 0      ; ...and what has to be INSIDE it

; THE STATE COLUMN (SPEC.md 52.10.4.2): [letter ': '] body [', ' reason].
; What is THERE, and why not only when the answer is no - so the longest row
; is `C: FAT16, Wrong Geometry`, 24 cells from HIW_STATEC, which is what
; HIW_CELLS is sized for. The FAT names and three of the reasons are the
; Format window's own strings, so two windows use one word for one fact.
hd_s_iunpart: db 'Unpartitioned', 0 ; a free slot the disk has room behind
hd_s_iempty:  db 'Empty', 0         ; ...and one it has none behind
hd_s_iunread: db 'Unreadable', 0    ; the table itself would not read
hd_s_ismall:  db 'Too Small', 0
hd_s_inone:   db 'No Room', 0
hd_s_ibig:    db 'Too Big', 0       ; FAT, past FAT16's ceiling (18.7.5)
hd_s_icolon:  db ': ', 0
hd_s_icomma:  db ', ', 0
hd_s_itype:   db 'Type ', 0
hd_s_ih:      db 'h', 0
hd_s_ig:      db 'G', 0

; The foreign types worth a NAME: type byte, then the string. Anything else
; reads 'Type nnh', which is still more than 'Unusable' said.
hd_ptypes:
    db 0x05
    dw hd_s_pext
    db 0x0F
    dw hd_s_pext
    db 0x85
    dw hd_s_pext
    db 0x07
    dw hd_s_pntfs
    db 0x0B
    dw hd_s_pfat32
    db 0x0C
    dw hd_s_pfat32
    db 0x82
    dw hd_s_pswap
    db 0x83
    dw hd_s_plinux
    db 0x8E
    dw hd_s_plvm
    db 0xA5
    dw hd_s_pbsd
    db 0xA6
    dw hd_s_pbsd
    db 0xA9
    dw hd_s_pbsd
    db 0x02
    dw hd_s_pxenix
    db 0x03
    dw hd_s_pxenix
    db 0x63
    dw hd_s_punix
    db 0x0A
    dw hd_s_pos2
    db 0xDB
    dw hd_s_pcpm
    db 0x64
    dw hd_s_pnw
    db 0x65
    dw hd_s_pnw
    db 0xEE
    dw hd_s_pgpt
    db 0xEF
    dw hd_s_pefi
    db 0x11
    dw hd_s_phid
    db 0x14
    dw hd_s_phid
    db 0x16
    dw hd_s_phid
    db 0x1E
    dw hd_s_phid
    db 0
    dw 0                        ; the terminator: its WORD is the test
hd_s_pext:    db 'Extended', 0
hd_s_pntfs:   db 'NTFS/HPFS', 0
hd_s_pfat32:  db 'FAT32', 0
hd_s_pswap:   db 'Linux Swap', 0
hd_s_plinux:  db 'Linux', 0
hd_s_plvm:    db 'Linux LVM', 0
hd_s_pbsd:    db 'BSD', 0
hd_s_pxenix:  db 'XENIX', 0
hd_s_punix:   db 'Unix', 0
hd_s_pos2:    db 'OS/2 Boot', 0
hd_s_pcpm:    db 'CP/M-86', 0
hd_s_pnw:     db 'NetWare', 0
hd_s_pgpt:    db 'GPT Disk', 0
hd_s_pefi:    db 'EFI System', 0
hd_s_phid:    db 'Hidden FAT', 0    ; a FAT type with the hide bit (10h) set

; Every line below is one CAPTION, and the macro is what stops one growing
; past the band: HIW_CELLS is the run's width and a longer string is silently
; TRUNCATED on screen, which is the kind of defect nobody sees until the one
; state that produces it happens on somebody else's machine.
%macro HIMSG 2
%1: db %2, 0
    %strlen %%n %2
    %if %%n > HIW_CELLS
        %error "installer caption is wider than HIW_CELLS"
    %endif
%endmacro

    HIMSG hd_s_ipick, 'Pick a partition, then Install'
    HIMSG hd_s_iarm, 'Erases it: Install again to confirm'
    HIMSG hd_s_iwork, 'Copying - this takes a few minutes'
    HIMSG hd_s_iswap, 'Insert the APPS disk, then Install'
    HIMSG hd_s_idone, 'Done - remove the floppy, Restart'
    HIMSG hd_s_ibad, 'That partition cannot be used'
    HIMSG hd_s_inomem, 'Not enough memory to copy'
    HIMSG hd_s_inomb, 'Cannot read the partition table'
    HIMSG hd_s_inosys, 'No system disk found - nothing done'
    HIMSG hd_s_ifmt, 'Could not format that partition'
    HIMSG hd_s_imnt, 'Formatted, but it would not mount'
    HIMSG hd_s_ierr, 'Disk error - the install stopped'
    HIMSG hd_s_inboot, 'Copied, but it will not boot'
    HIMSG hd_s_ideep, 'Folders nested too deep to copy'
    HIMSG hd_s_ikroom, 'No room for the kernel in one piece'
    HIMSG hd_s_ikbpb, 'That volume is not one we can keep'
    HIMSG hd_s_ikgeom, 'Formatted for another geometry'
    HIMSG hd_s_iksplit, 'The kernel did not land in one piece'
    HIMSG hd_s_iarmk, 'Keeps its files: Install again to confirm'
    HIMSG hd_s_ilbwipe, 'Boot from a floppy to erase it'
%ifdef INSTBENCH
    HIMSG hd_s_bdone, '     fC   fS   dC   dS   tk  done'
%endif

hd_iwin:      dw 0              ; .text with a real initialiser: -f bin zeroes
                                ; nothing, and a garbage window pointer here
                                ; is a paint through whatever it names
hd_iw_st:     db 0
hd_isel:      db 0
hd_imsg:      dw hd_s_ipick
hd_istate:    times 4 db 0      ; HIS_*: the verdict
hd_isecs:     times 4 dd 0      ; ...the extent, 32 bits: a PARTITION is not
                                ; bounded by a word of sectors
hd_ilet:      times 4 db 0      ; the State column (SPEC.md 52.10.4.2): the
hd_itype:     times 4 db 0      ; drive letter or 0, the table's type byte,
hd_ibody:     times 4 dw 0      ; what is there (0 = 'Type nnh'),
hd_irsn:      times 4 dw 0      ; and why not, 0 = no reason to give
hd_iscur:     dd 0              ; hd_iw_slot's working extent...
hd_ilcur:     db 0              ; ...and letter
hd_ivcur:     db 0              ; ...and whether it found a volume to keep
hd_irow:      db 0
hd_iclx:      dw 0
hd_icly:      dw 0
hd_ibx:       dw 0
hd_ibw:       dw 0
hd_ibufseg:   dw 0
hd_ibufkb:    dw 0
hd_idrv:      db 0              ; the destination's kernel volume index
hd_ivdrv:     db 0              ; ...and the user's, banked
hd_ivcwd:     dw 0
hd_isrcdrv:   db 0              ; the drive the COPY reads from, which is the
                                ; banked one on a one-drive machine and the
                                ; other floppy when hd_iapps_find has found the
                                ; apps disk already in it (SPEC.md 52.10.5).
                                ; Separate from hd_ivdrv because that one is
                                ; what hd_ivol_back puts the user on, and the
                                ; two must not be the same word
hd_iafound:   db 0              ; hd_iapps_find: an APPS folder has been seen
hd_iappsc:    dw 0              ; ...and its first cluster, for the descent
hd_iautod:    db 0xFF           ; the auto-detected apps drive, 0xFF = none.
                                ; .text with a real initialiser, hd_iwin's
                                ; reason: -f bin zeroes nothing and 0 here is
                                ; drive A:
hd_ilvl:      db 0              ; THE WALK'S LEVEL STACK (SPEC.md 52.10.13):
hd_isrcc:     times HIW_DEPTH dw 0    ; which folder each level is on the
hd_idstc:     times HIW_DEPTH dw 0    ; source and on the destination (0 = the
hd_iordv:     times HIW_DEPTH dw 0    ; root), and how far OSAPI_FILE_FIND has
                                ; walked it (SPEC.md 19.7.1). Six bytes a level
                                ; instead of a stack frame, which is what keeps
                                ; a deep tree off the UI task's stack.
                                ;
                                ; It replaces four words and a flag - one
                                ; ordinal for the root, one for "the" folder, a
                                ; cluster each side and a bit saying which of
                                ; the two the current file was in. That shape
                                ; could not name a folder inside a folder, so
                                ; SYSTEM/APPDATA was never made on an installed
                                ; volume (SPEC.md 19.9)
hd_ichildc:   dw 0              ; a folder's cluster on the SOURCE, held from
                                ; the entry that named it to the push that
                                ; takes it - hd_ient is rewritten in between
hd_iphase:    db 0              ; 0 = the SYSTEM phase, 1 = the APPS phase.
                                ; Only hd_icopy_one reads it, and only to
                                ; refuse a hidden+system source file in phase
                                ; 1 (SPEC.md 52.10.12). .text with a real
                                ; initialiser like every byte here: -f bin
                                ; zeroes nothing, and garbage would put the
                                ; system phase on the wrong side of that test
hd_ient:      times OSAPI_FIND_SZ db 0
hd_ifind:     times OSAPI_FIND_SZ db 0
hd_ikeepl:    times HIW_DEPTH db 0    ; per level: its files are SETTINGS
                                ; (an APPDATA, SPEC.md 19.9) and a kept volume
                                ; keeps the user's copy (SPEC.md 52.10.15)
hd_ivol:      times 4 db 0      ; per slot: a FAT volume is there to keep
hd_iclus:     dw 0              ; hd_istat's answer: the file's first cluster
hd_kofs:      dd 0              ; the kernel's start in the data area, sectors
hd_kfat:      dd 0              ; hd_kplan: the FAT's first sector, absolute
hd_kspc:      dw 0              ; ...sectors per cluster
hd_kmax:      dw 0              ; ...the highest cluster number
hd_kf12:      db 0              ; ...1 = FAT12
hd_kn:        dw 0              ; ...clusters the new kernel needs
hd_kr:        dw 0              ; ...the first cluster of the run it goes in
hd_kf:        dw 0              ; ...free clusters below that run: the pad
hd_koc:       dw 0              ; ...the OLD kernel's first cluster, 0 = none
hd_kosz:      dd 0              ; ...its size
hd_kon:       dw 0              ; ...its clusters when ONE run, else 0
hd_kgeo:      db 0              ; 1 = HDD.DRV writes it: the BPB's geometry
                                ; must be the row's (SPEC.md 52.10.15)
hd_kchunk:    dw 0xFFFF         ; hd_kfat_get's cached 3-sector chunk
hd_iname:     times 14 db 0
hd_iname2:    times 14 db 0
hd_iwant:     dw 0
hd_isize:     dd 0
hd_isys:      db 0
hd_ioff:      dd 0
hd_itake:     dw 0

; --- state this module owns (moved out of hdd.asm's data block) ---
hd_idev:     db 0               ; ...and the installer's, for the same reason
