; =============================================================================
; os8088 - desk.inc (SPEC.md 26)
;
; Desktop drive icons: detect the floppy drives at boot (int 11h), paint a
; disk icon + an "A:" label per volume on the desktop, track selection, and
; open the Disk window on double-click via files_open_drive.
;
; Layout, per zone ordinal i (SPEC.md 26.1's wrap decides which one a volume
; gets). The icon is 32 rows on VGA and Hercules and 14 on the CGA, where the
; pixels are 2.4:1 tall and a 32-row icon is a stretched column rather than a
; diskette (SPEC.md 26.4); [desk_zstep] is 60 and 34 to match. Neither the
; icon's height nor SPEC.md 26.5's gap is STORED: [desk_zh1] is the sum, minus
; one, and it is the only form anything ever wanted (desk_rowcalc_x builds it
; per arm and the label band is derived back off it).
;   hit zone   x zx..zx+31, y (32+step*i) .. +[desk_zh1], where
;              zx = [vid_desk_zx] = screen width - 56 (584 at 640 wide)
;   icon       [desk_pdisk] / [desk_phdd] at (zx, top), 32 px wide
;   label      SPEC.md 26.5's gap of desktop under the icon, then 8px text,
;              black on a white rect that HUGS the caption - measured with
;              font_width and clamped to the zone plus its 2px overhang,
;              which is what desk_zone_rect erases
;
; desk_paint runs under the caller's gfx lock (from wm_paint_all, right
; after the desktop fill, before any window). desk_click runs with NO lock
; held and takes gfx_lock/gfx_unlock itself around the zones it redraws.
; A deselected zone is repainted with gfx_fill_gray first - its dither is
; pixel-parity based, so it aligns with the surrounding desktop.
; =============================================================================

; The drive column hangs off the RIGHT edge of the screen, so its left
; column is derived at boot rather than pinned: [vid_desk_zx] = width - 56,
; which is 584 on both 640-wide adapters - exactly where it has always been -
; and 664 on a 720-wide Hercules, where a fixed 584 would leave the icons
; stranded 88px shy of the edge. Everything below derives from that ONE word,
; including the redraw-overlap test, which used to open-code 582 and 634.
DESK_ZW     equ 32              ; hit zone width (zx .. zx+31) - the icon's
                                ; own width now. It was 48, because the label
                                ; was 'Disk A' and six 8px glyphs are exactly
                                ; 48; SPEC.md 26.4 made it 'A:' and the zone
                                ; is the picture again
DESK_ZOVER  equ 2               ; label overhang each side
DESK_ZY0    equ 32              ; first zone top
DESK_LBLH   equ 12              ; the label band: 2px gap + 8px text + 2px gap
DESK_ZGAP   equ 2               ; desktop between the icon's cell and the
                                ; label's white rect (SPEC.md 26.5). It was 0,
                                ; which read as a band under the icon while
                                ; the caption filled the zone and reads as
                                ; PART OF THE DISKETTE now that it hugs: the
                                ; floppy's last row is a solid black edge, so
                                ; the white rect touched it
DESK_ZGAP_S equ 1               ; ...and on the CGA, whose pixels are 2.4:1
                                ; tall - 26.4's argument about the icon
DESK_ZSTEP  equ 60              ; zone pitch with the 32-row icon...
DESK_ZSTEP_S equ 34             ; ...and with SPEC.md 26.4's 14-row one
DESK_COLW   equ 44              ; column-to-column pitch, leftwards: the zone
                                ; plus its overhang plus one clear glyph cell,
                                ; which is what stops two columns' captions
                                ; reading as one run of text
DESK_DBLT   equ 9               ; double-click window, ticks

; The column wrap (SPEC.md 26.1). Zones run DOWN a column and then start a
; NEW COLUMN TO THE LEFT, because on CGA they do not all fit: the strip is
; 640x200 there, the dock's top row is [vid_h]-24 = 176, and zone 2 would run
; 152..195 - straight through it. [desk_rows] is how many fit above the dock
; and is computed at boot from the live geometry, never from a constant, so
; VGA (7) and Hercules (4) keep every volume in one column and CGA (2) puts
; the hard disk beside the floppies instead of under them.

; -----------------------------------------------------------------------------
; desk_init - count floppy drives from the int 11h equipment word: if bit 0
;             is set, drives = ((AX >> 6) & 3) + 1, else 0. Rows 0 and 1 get
;             a zone flag; anything past them gets a volume ROW of its own
;             (SPEC.md 18.98). Initializes all module state (.bss is not
;             auto-zeroed).
; ins:  none
; outs: none (all registers preserved)

section .ovlw                  ; the boot overlay (SPEC.md 2.5): counting the
                                ; volumes and laying their zones out happens
                                ; once, before the first paint. desk_ord and
                                ; desk_zone_label below are called by the
                                ; RUNTIME zone painters and stay resident
; -----------------------------------------------------------------------------
desk_init:
    push ax
    push bx
    push cx
    push dx
    int 0x11                    ; AX = BIOS equipment word
    test al, 0x01               ; bit 0: any floppy drives at all?
    jz .none
    mov cl, 6
    shr ax, cl
    and al, 3                   ; 0..3 = 1..4 drives - AL, so AH keeps the
    inc ax                      ; equipment word's high byte (nothing reads it)
    mov dh, al                  ; ...and no clamp: it was 2, because the two
                                ; on the 5.25" adapter's external connector
                                ; had no volume row to be given (SPEC.md
                                ; 18.98). The COUNT is all the machine can
                                ; tell us and all it needs to: drive selects
                                ; are ordered, so 3 means 0,1,2 and there is
                                ; no drive 3 without a drive 2 to answer for
                                ;
                                ; DH and not AH: dsk_flop_add below ANSWERS in
                                ; AX, so a count kept there survives exactly
                                ; one call - which registered drive 2, lost
                                ; the count to 0, and left drive 3 with no row
    jmp .flags
.none:
    OVLMARK 51                  ; 51: SPEC.md 57.5's equipment word says this
                                ; machine has NO FLOPPY DRIVES, so no volume
                                ; gets a zone and the desktop has no icons.
                                ; On an XT that claim is the SW1 DIP switches
                                ; read back through the 8255 - a statement
                                ; about what somebody SET, and on a machine
                                ; with a hard-disk controller's option ROM in
                                ; it, about what that ROM left behind
    xor dh, dh
.flags:
    mov [fdd_dbg_eqp], dh       ; what int 11h CLAIMED, banked before anything
                                ; contests it (SPEC.md 57.5)
%ifndef NO_FDDPROBE
    ; ...and on a 5150 that claim is the SW1 DIP switches read back through the
    ; 8255, which is a statement about what somebody SET. Ask the drive
    ; (SPEC.md 18.97). Only when the word claims the second drive - a machine
    ; claiming one is uncontested and pays this compare - and only ever about
    ; unit 1, because unit 0 is where the kernel just came from.
    ;
    ; **THE COUNT IS IN DH AND THIS BLOCK USED TO READ AH.** 18.98 moved it,
    ; because dsk_flop_add below answers in AX; the two changes met at the
    ; merge and git took both without a conflict. What that produced assembles
    ; and boots: the mask was `and ax, 3` then, so AH was 0, the registry
    ; published 0, the `cmp` never let the probe run at all, and the retire
    ; path wrote a count nothing read - a probe silently switched off and a
    ; freed row that could still be given a zone. Read DH, and both features
    ; are themselves again. The mask is `and al, 3` now, which is a byte
    ; shorter AND fails louder: AH carries the equipment word's serial/parallel
    ; counts, so anyone who reads it again gets nonsense rather than a quiet 0.
    cmp dh, 2
    jb .probed
    push ax
    mov al, 1                   ; UNIT 1 - and this line is 18.98's, because
                                ; the probe took no argument when it was
                                ; written and this call site set nothing. Made
                                ; to read AL, it inherited whatever was there:
                                ; the drive COUNT, so a two-drive machine
                                ; probed unit 2 and a four-drive one asked
                                ; about unit 4, whose DOR select is `4 & 3` -
                                ; unit 0, the drive the kernel just booted off
    call dsk_fdd_probe          ; preserves BX, CX, DX and ES, so the count
    mov cl, al                  ; is safe across it; the verdict comes out of
    pop ax                      ; AX's way because the probe answers in AL
    or cl, cl
    jnz .probed                 ; present, or not PROVEN absent - either way it
                                ; keeps its zone (18.97: the two errors are not
                                ; symmetric). Absent: the row goes - BUT ONLY
                                ; IF THE CLAIM WAS A DEFAULT (SPEC.md 18.97.2).
                                ;
                                ; 18.97 contests unit 1 because on a 5150 the
                                ; equipment word is the SW1 DIP SWITCHES: two
                                ; drives is the factory position, it is wrong
                                ; on most 5150s, and nobody ever looked. That
                                ; is a default worth disproving. An AT-class
                                ; machine takes the same count out of CMOS
                                ; SETUP, where it is a deliberate assertion by
                                ; whoever configured the machine - which is
                                ; 18.98.1's own reasoning for TRUSTING a claim
                                ; of three or four drives, and it applies here
                                ; with the same force.
                                ;
                                ; It has to, because the discriminator is known
                                ; to be wrong on real drives: 18.98.1's IBM
                                ; 4865 is present, powered, mounting and
                                ; reading under int 13h at the very moment it
                                ; answers TRK0 clear before AND after a
                                ; recalibrate - the exact signature that
                                ; retires a drive here. A probe that cannot
                                ; distinguish "no drive" from "a drive that
                                ; will not report TRK0" may only be let near a
                                ; claim that was nobody's decision.
    cmp byte [cpu_tier], CPU_8086   ; tier 0 = 8086/8088 = DIP switches. Read
    jne .keepb                      ; through DS: cpu_tier is .text with a real
                                    ; initialiser and cpu_detect ran at kmain
                                    ; long before desk_init (SPEC.md 41.1)
    call dsk_fdd_retire         ; **AND THE COUNT DOES NOT.** This was
                                ; `mov dh, 1` - correct while two was the most
                                ; a machine could claim, and wrong the moment
                                ; 18.98 made units 2 and 3 reachable: it
                                ; truncates the loop below, so a machine
                                ; claiming three or four drives with no unit 1
                                ; never asks about the external pair at all.
                                ; That is not a corner - it is one internal
                                ; drive plus a 4865 on the 37-pin connector,
                                ; which is the field machine with the thing
                                ; 18.98 was written for plugged into it.
                                ; .zloop below reads the ROW instead.
    jmp short .probed
.keepb:
    mov byte [fdd_dbg_u+FDU_VRD], 1 ; and the block does not get to say the row
                                    ; went when it stayed (SPEC.md 57.5): the
                                    ; probe wrote its own finding into VRD on
                                    ; the way out, and the DECISION is made
                                    ; here. `probe stop` still carries what the
                                    ; probe concluded, which is the row
                                    ; sysbench already tells the reader to read
                                    ; FIRST - so a field report says "the probe
                                    ; called it absent and the row was kept"
                                    ; in the two bytes it already had
.probed:
%endif
    ; A floppy the machine does not have gets no zone - but its VOLUME ROW
    ; stays live, because [disk_drive] may still name it and a mount of an
    ; absent drive is an ordinary failed mount (SPEC.md 18.3). That is still
    ; right for a drive nothing DISPROVED - a claim of one drive is no evidence
    ; about unit 1, and retiring a row on no evidence is what 18.97 forbids.
    xor cl, cl
.zloop:
    mov bl, cl
    call KERNEL_SEG:dsk_vol_slot      ; FAR: overlay -> resident. It clobbers
    xor al, al                  ; "BX (the output), flags", so AL is free
    cmp byte [bx+DV_KIND], DVK_FREE
    je .zskip                   ; the probe retired it - do not hand it back.
                                ; This is what lets the count above stay the
                                ; CLAIM: the zone is a fact about the ROW now,
                                ; and only the row is taken away
    cmp cl, dh
    jae .zskip
    inc ax
.zskip:
    mov [bx+DV_FLAGS], al       ; the value is BUILT and stored once - the
                                ; same byte at the same point in the loop
    inc cl
    cmp cl, 2
    jb .zloop

    ; --- and the EXTERNAL pair, which needs a ROW and not just a flag -------
    ; Units 2 and 3 have no row in dsk_vtab to turn on: rows 0 and 1 are the
    ; internal drives and everything above them is free for whoever asks first
    ; (SPEC.md 18.98). HERE, and not beside dsk_boot_from in kmain, precisely
    ; so that a hard-disk boot has already taken row 2 - which is what leaves
    ; the boot partition at C: and puts the externals after it.
    mov cl, 2
.eloop:
    cmp cl, dh
    jae .edone                  ; the machine does not claim this many drives,
                                ; so it gets no row and therefore no zone -
                                ; there is nothing here to hide afterwards
%ifndef NO_FDDPROBE
    ; ...and they are ASKED but not JUDGED (SPEC.md 18.98.1). The probe runs
    ; for its published state - 57.5's ST3/ST0 for this unit is the only way
    ; anybody can see what an external drive answered - and the ROW is made
    ; whatever it says.
    ;
    ; **The switches are contested for unit 1 and TRUSTED for units 2 and 3**,
    ; and the asymmetry is about what the setting MEANS. Two drives is the
    ; factory position on essentially every 5150 and is wrong on most of them,
    ; so unit 1's claim is a default worth disproving. Nobody sets SW1 to
    ; claim three or four by accident: that is a deliberate assertion about
    ; hardware somebody went and wired to the 37-pin connector, and 18.96.2's
    ; rule applies - trust the assertion. The costs are not symmetric either:
    ; a phantom C: is one wasted click, a hidden real drive is the drive the
    ; operator just installed.
    ;
    ; It is not hypothetical. The field 5150's 4865 answers ST3 = 22 with
    ; TRK0 clear before AND after a recalibrate - ST0 = 72, the 765's own
    ; Equipment Check - while DOS reads the same drive perfectly. Whatever
    ; that is, the probe cannot see past it, and 18.97's "every failure keeps
    ; the drive" is the rule that should have governed.
    mov al, cl
    call dsk_fdd_probe          ; preserves BX, CX, DX and ES: CL and the
%endif                          ; count in DH both survive it. AL is IGNORED
    mov al, cl
    call KERNEL_SEG:dsk_flop_add        ; FAR: overlay -> resident
.eskip:
    inc cl
    cmp cl, 4
    jb .eloop
.edone:

    call KERNEL_SEG:ovw_desk_rowcalc  ; FAR: overlay -> resident. The row
                                ; count is geometry, and geometry can change
                                ; after boot now (SPEC.md 39.11.2), so the
                                ; arithmetic lives in .text where vid_switch
                                ; can reach it too

    xor ax, ax                  ; AX is popped two lines down, so it is free
    mov [desk_clkt], ax
    dec ax
    mov [desk_sel], al          ; 0xFF = no selection
    pop dx
    pop cx
    pop bx
    pop ax
    retf                        ; FAR, and the shim that used to own this
                                ; return is deleted (SPEC.md 2.6.1, 2.5.2):
                                ; kmain names this body directly through
                                ; OVLGATE1 and nothing near-calls it

; -----------------------------------------------------------------------------
; desk_ord - a volume's ORDINAL among the zones that are shown
; in:  AL = volume index
; out: CF = 0 and AH = the ordinal; CF = 1 = this volume has no zone
; clobbers: AH (the output), flags
;
; The zone list IS the volume table (SPEC.md 18.7/26), so there is no second
; array to keep in step - which is what makes a driver's C: appear by setting
; one flag and disappear by clearing it.

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own. Drive zones and the
                                ; desktop dither: drawn on a mount, a volume
                                ; going away and a desktop click, never in a
                                ; loop. This module has no data at all, so
                                ; there is no toggle in it (rule 1)

; -----------------------------------------------------------------------------
; desk_rowcalc - how many drive zones fit in one column (SPEC.md 26.1)
;
; in:       [vid_dock_y0]
; out:      [desk_rows]
; clobbers: flags
;
; 7 on VGA, 4 on Hercules, 2 on CGA - and at least one, always: a screen with
; no room for a single drive icon is not one this OS boots. It was inline in
; desk_init, which is boot-overlay code and therefore dead by the time the
; Control Panel can change the adapter under it (SPEC.md 39.11.2), so it is
; resident now and desk_init far-calls it like any other overlay->text step.
; -----------------------------------------------------------------------------
desk_rowcalc_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; --- which ICON, and therefore how tall a zone is (SPEC.md 26.4) --------
    ; The CGA's pixels are 2.4:1 tall, so the 32-row diskette renders as a
    ; stretched column there and costs 60 rows of pitch for it. The 14-row
    ; pair is square on that adapter AND doubles the column: 144 usable rows
    ; over a 34-row pitch is 4 zones where 60 was 2.
    ;
    ; [vid_kind] and not [vid_h], because the question is the PIXEL ASPECT
    ; and that is a property of the adapter's mode rather than of how many
    ; rows it has. It is re-asked here rather than at boot because this
    ; routine is what vid_switch re-runs (SPEC.md 39.11.2), so a machine
    ; moved from its CGA to its Hercules gets the tall pair back for free.
    ; The icon height and SPEC.md 26.5's gap are not stored: [desk_zh1] is
    ; the only thing anything ever read them for, so the sum is the arm's
    ; constant and the two words are gone. It is MINUS ONE because every
    ; reader wants it that way. The gap is inside the zone rather than beside
    ; it, so the erase, the damage growth and the XOR highlight - all of which
    ; derive from [desk_zh1] - cover it for free, and [desk_zstep] does not
    ; move: a zone still fits its pitch (45 of 60, 26 of 34).
    mov ax, 32 + DESK_ZGAP + DESK_LBLH - 1
    mov bx, DESK_ZSTEP
    mov si, ico_disk32
    mov di, ico_hdd32
    cmp byte [vid_kind], VID_CGA
    jne .set
    mov ax, 14 + DESK_ZGAP_S + DESK_LBLH - 1
    mov bx, DESK_ZSTEP_S
    mov si, ico_disk14
    mov di, ico_hdd14
.set:
    mov [desk_zh1], ax
    mov [desk_zstep], bx
    mov [desk_pdisk], si
    mov [desk_phdd], di

    mov ax, [vid_dock_y0]
    sub ax, DESK_ZY0
    xor dx, dx
    mov cx, bx                  ; BX still holds the pitch - nothing between
    div cx                      ; .set and here writes it
    or ax, ax
    jnz .rows
    inc ax                      ; AX is 0 by the test above, so the floor of
.rows:                          ; one row is an inc
    mov [desk_rows], ax
    jmp kretfc_di

; -----------------------------------------------------------------------------
; desk_zflag - does volume BL have a desktop zone? (SPEC.md 26)
; in:  BL = volume index
; out: ZF = 1 no zone, ZF = 0 it has one; BX -> the volume row
; clobbers: BX (dsk_vol_slot_x's documented output), flags
;
; Four sites asked this question with the same two instructions.  `pop` and
; `ret` do not write flags, so ZF crosses the call unharmed - which is what
; makes a helper possible at all here.
; -----------------------------------------------------------------------------
desk_zflag:
    call dsk_vol_slot_x
    test byte [bx+DV_FLAGS], 1
    ret

; -----------------------------------------------------------------------------
desk_ord:
    push bx
    push cx
    xor ah, ah                  ; AH = the ordinal being counted
    xor cl, cl
.next:
    mov bl, cl
    call desk_zflag
    jz .skip
    cmp cl, al                  ; the compare that FINDS the volume compared
    je .out                     ; two equal bytes, so it left CF = 0 - which
    inc ah                      ; is exactly what a hit means. No clc, no
.skip:                          ; second exit
    inc cl
    cmp cl, DVOL_MAX            ; bottom-tested: DVOL_MAX is a compile-time
    jb .next                    ; constant >= 1, so the body always runs once
    stc                         ; ...and exhausted falls into the same pops
.out:                           ; one pair of pops, not two: `pop` does not
    pop cx                      ; write flags, so CF crosses them intact
    pop bx
    ret

; -----------------------------------------------------------------------------
; desk_zone_label - one volume's desktop caption
; in:  AL = volume index
; out: SI -> a NUL string in the kernel segment
; clobbers: SI (the output), flags
;
; The row's own inline label if it has one, else 'Disk X' derived from the
; index - which is what keeps a driver free to call its volume 'HDD C' while
; a floppy needs no label stored at all.
; -----------------------------------------------------------------------------
desk_zone_label:
    push ax
    push bx
    mov bl, al
    call dsk_vol_slot_x
    lea si, [bx+DV_LBL]
    cmp byte [si], 0
    jne .out                    ; the row named itself
    mov si, desk_lbl_gen        ; the DRIVE LETTER, built in place (SPEC.md
    add al, 'A'                 ; 26.4). It was 'Disk C' / 'HDD C', and the
    mov [si], al                ; prefix went for two reasons: six glyphs set
                                ; the column pitch, so two columns' captions
                                ; ran together with one space between them;
                                ; and the prefix said what the ICON already
                                ; says, since a driver-backed volume is drawn
                                ; with the hard disk. 'A:' rather than 'A'
                                ; because that is how every other surface in
                                ; this OS writes a drive
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_paint - draw every drive's icon + label; the selected zone (desk_sel)
;              gets an XOR highlight. Called by wm_paint_all after the
;              desktop fill; caller holds the gfx lock.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_paint_x:
    push ax
    mov al, (1 << DVOL_MAX) - 1 ; every zone - the push is FIRST, so the
    jmp short desk_pm_body      ; caller's AX is what reaches the stack

; -----------------------------------------------------------------------------
; desk_paint_mask - draw the drive zones named by a bitmask
; in:  AL = bit n set = draw zone n; caller holds the gfx lock
; out: nothing (all registers preserved)
;
; The same loop as desk_paint above, because "every zone" IS a mask - the
; bound that used to be `cmp cl,DVOL_MAX` is now the mask running out, and
; the full mask is spelled from DVOL_MAX so it cannot drift from it.
;
; desk_paint_mask gains desk_paint's desk_zflag test, which it did not have.
; It is redundant rather than new: its only caller passes [wm_dmg_zn], built
; by desk_dmg_zones_x from desk_zflag under the same held gfx lock, so a set
; bit already implies a set flag and the test can only ever agree.
; -----------------------------------------------------------------------------
desk_paint_mask_x:
    push ax
desk_pm_body:
    push bx
    push cx
    push dx
    mov dl, al                  ; DL = the mask for the duration - the one
    xor cl, cl                  ; register desk_zflag does not clobber
.next:
    shr dl, 1                   ; CF = "zone CL wants drawing"
    jnc .skip
    mov bl, cl
    call desk_zflag
    jz .skip
    mov al, cl
    call desk_draw_zone         ; AL = volume index; preserves every register
.skip:
    inc cl
    or dl, dl                   ; ...and the mask running out IS the bound
    jnz .next
    jmp kretfc_dx

; -----------------------------------------------------------------------------
; desk_zone_rect - one drive zone's DRAWN rect (not its hit zone)
; in:  AL = zone index
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; clobbers: the four outputs (AH included)
;
; The drawn rect is wider than the hit zone: the label's white box overhangs
; DESK_ZOVER px each side, and THIS ROUTINE is where that is expressed -
; [vid_desk_zx] is the hit zone's left edge and the only word published, with
; DESK_ZW/DESK_ZOVER doing the rest (SPEC.md 26/39.2). Two cells carrying the
; drawn edges were published beside it for a while and never read; the
; arithmetic here was always the answer. desk_zone_redraw comes through here
; for its own overlap test.
; -----------------------------------------------------------------------------
desk_zone_rect:
    call desk_zone_xy           ; AX = zone left, DX = zone top
desk_rect_ax:                   ; ...and the arithmetic ALONE, for a caller
                                ; that is already holding the origin. Costs
                                ; nothing: it is a second entry point, not a
                                ; routine. desk_zone_rect has no NASM local,
                                ; so this label re-parents none.
    mov bx, dx
    add dx, [desk_zh1]           ; y2 = top + 43
    mov cx, ax
    add cx, DESK_ZW + DESK_ZOVER - 1    ; x2, label overhang included
    sub ax, DESK_ZOVER                  ; x1, likewise
    ret

; -----------------------------------------------------------------------------
; desk_zone_xy - one volume's zone origin (SPEC.md 26.1)
; in:  AL = volume index
; out: AX = the HIT zone's left column, DX = its top row
; clobbers: AX, DX (the outputs), flags
;
; Zones fill a column downwards and then start a new one to the LEFT. The
; row count is [desk_rows], measured at boot against the live dock position -
; so this is where CGA's third volume stops landing on the dock.
; -----------------------------------------------------------------------------
desk_zone_xy:
    push bx
    push cx
    call desk_ord               ; AH = the ordinal, CF=1 = not shown
    mov al, ah                  ; harmless on the CF=1 arm - AX is overwritten
    pop cx                      ; there. Neither `mov` nor `pop` writes flags,
    pop bx                      ; so desk_ord's CF reaches the test below and
    jnc desk_ord_xy             ; ONE pair of pops serves both answers
    mov ax, [vid_desk_zx]       ; a hidden volume still needs a rect that is
    mov dx, -1000               ; nowhere: every caller draws or hit-tests it
    ret                         ; only after its own flag test, and this is
                                ; the belt to that pair of braces

; -----------------------------------------------------------------------------
; desk_ord_xy - where the Nth SHOWN zone sits (SPEC.md 26.1)
; in:  AL = an ordinal, 0..DVOL_MAX-1
; out: AX = the hit zone's left column, DX = its top row
; clobbers: AX, DX (the outputs), flags
;
; Split out of desk_zone_xy so the grid's extent (desk_zones_dmg) can be asked
; for by ordinal without a volume to hang it on - the geometry stays in one
; place, which is what the two callers of the old combined routine relied on.
; -----------------------------------------------------------------------------
desk_ord_xy:
    push bx
    push cx
    xor ah, ah
    xor dx, dx
    div word [desk_rows]        ; AX = column, DX = row within it
    mov cx, dx                  ; CX = the row - and it has to leave DX NOW,
    mov bx, DESK_COLW           ; because both multiplies below write there
    mul bx                      ; AX = column * 44 (7 * 44 = 308, so DX = 0)
    mov bx, [vid_desk_zx]
    sub bx, ax                  ; BX = the column's left, leftwards from the
                                ; drive column (SPEC.md 26.1) - and it is only
                                ; banked ONCE, because mul takes an r/m16
    mov ax, cx
    mul word [desk_zstep]       ; AX = row * pitch (6 * 60 = 360, DX = 0)
    add ax, DESK_ZY0
    mov dx, ax                  ; DX = the top row
    mov ax, bx                  ; AX = the left column
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; desk_zmark - a volume gained or lost its zone: post the repaint (SPEC.md 26.3)
; in:  the zone table already reflects an ADD, and does NOT yet reflect a
;      DELETE - either way the changing zone is still COUNTED
; out: nothing (all registers preserved)
;
; The two callers are dsk_vol_add and dsk_vol_del, and the asymmetry in when
; they call is the whole point. An add renumbers ordinals below the new count;
; a delete leaves stale pixels at what was the last ordinal. Counting with the
; changing zone included covers both, and taking the MAX covers a drain that
; coalesces several changes - unmounting a three-partition disk drops three
; volumes before ui_task looks. desk_zones_paint spends the mark and clears it.
; -----------------------------------------------------------------------------
desk_zmark_x:
    push ax
    call desk_zones_shown
    cmp al, [desk_zhw]
    jbe .post
    mov [desk_zhw], al
.post:
    mov al, 1                   ; AL is dead - the pop below settles it - and
    mov [desk_zdirty], al       ; a register-to-memory store is two bytes less
    mov [ui_post], al           ; ...and SAY SO once (SPEC.md 13.13)
    pop ax
    ret

; -----------------------------------------------------------------------------
; desk_zones_shown - how many volumes have a zone right now (module internal)
; out: AL = the count, 0..DVOL_MAX
; clobbers: AX (AL is the output, AH is scratch), flags
;
; AL, not AX: both callers in this file want a byte. desk_zmark compares and
; stores AL; desk_zones_dmg tests AL and does the one xor ah,ah its own two
; arms share. There is no third caller (grep, whole tree).
;
; desk_ord IS this walk - it counts the shown zones ahead of the volume it was
; asked about - so asking it for an index no row can have runs the count to the
; end and reports it in AH. One definition of "which zones are shown, and in
; what order", which is the property the damage rect has to agree with.
; -----------------------------------------------------------------------------
desk_zones_shown:
    mov al, 0xFF                ; matches no volume, so the walk never stops
    call desk_ord               ; early: CF = 1 and AH = every shown zone
    mov al, ah
    ret

; -----------------------------------------------------------------------------
; desk_zones_dmg - the rect the zones that MOVED could occupy (SPEC.md 26.3)
; out: AX/BX/CX/DX = x1/y1/x2/y2, inclusive, for wm_paint_dmg
; clobbers: the four outputs, flags
;
; NOT the whole grid, and that distinction is worth a paragraph because the
; grid is what this used to be. Sizing to DVOL_MAX made the answer independent
; of what is mounted, which is a real property - but the grid is six zones and
; a machine usually shows three, and the unused ones are not slack at the edge:
; columns fill downwards and wrap LEFT, so on a geometry with few rows the
; phantom zones are whole COLUMNS of desktop reaching back across the screen.
; On CGA (desk_rows = 2) three mounted volumes occupy x 526..633 and the grid
; claimed x 470..633 - 56 pixels of empty desktop that reached into the
; Control Panel window the Mount button lives in, so wm_paint_dmg's marking
; pass redrew that window WHOLE, W_PAINT and all, on every mount.
;
; What actually has to be covered is ordinals 0..n-1 where n is the largest
; number of zones the screen may currently be showing - [desk_zhw], the
; high-water mark desk_zmark keeps. An ADD renumbers only ordinals below the
; new count and a DELETE leaves stale pixels at the old last ordinal, so the
; mark is taken with the changing zone COUNTED in both cases: after the row
; goes live on an add, before its flag is cleared on a delete. That also
; survives coalescing, which "the current count plus one" does not - unmounting
; a three-partition disk drops three volumes into one drain.
;
; A mark of 0 means no change is pending, and the live count is the answer -
; which is what osapi_vol_paint needs, a driver being free to call it with
; nothing owed. There is no floor to apply when the mark IS set: an add stamps
; exactly the live count and a delete stamps one more, so a set mark is never
; below it. The count is floored at 1, so ordinal 0 is always a legal argument
; to desk_ord_xy.
;
; Ordinal 0 is the top of the rightmost column and ordinal n-1 the bottom of
; the leftmost, because columns fill downwards and then wrap LEFT - so the two
; corners are the two extremes. Only the LEFTMOST is asked for: ordinal 0 is
; not a general case of the wrap, it is the grid's ORIGIN, and desk_ord_xy
; starts from it - `div [desk_rows]` gives column 0 row 0, so its answer is
; [vid_desk_zx] and DESK_ZY0 on every adapter. Writing those two down is not
; the wrap arithmetic written twice; the wrap is still asked for exactly once.
; That pays for the whole call, both banks and all three push/pop pairs.
; -----------------------------------------------------------------------------
desk_zones_dmg:
    mov al, [desk_zhw]          ; the number of zones to cover. A set mark is
    or al, al                   ; never below the live count - an add stamps
    jnz .n                      ; exactly it and a delete stamps one more - so
    call desk_zones_shown       ; there is nothing to floor it against, and 0
    or al, al                   ; means no pending change at all: osapi_vol_paint
    jnz .n                      ; asking for the zones that exist
    mov al, 1                   ; ordinal 0 is always a legal argument
.n:
    xor ah, ah                  ; the one promotion both arms share
    mov bx, ax                  ; BX = the count, dead across desk_ord_xy
    dec ax                      ; the last ordinal in use
    xor dx, dx
    div word [desk_rows]        ; AX = the LEFTMOST column's index...
    mul word [desk_rows]        ; ...as the ordinal at the TOP of it
    mov cx, [desk_rows]         ; the deepest row any column reaches - NOT the
    cmp cx, bx                  ; last ordinal's row, which on a geometry
    jbe .deep                   ; whose final column is short (Hercules: four
    mov cx, bx                  ; rows, two columns, six volumes) sits above
.deep:                          ; the bottom of the first column and would cut
    dec cx                      ; the rect short of two whole zones
    add ax, cx                  ; AX = the bottom-left corner's ordinal, one
                                ; row past what a short last column uses -
                                ; slack, and in the safe direction
    call desk_ord_xy
    sub ax, DESK_ZOVER                  ; x1, label overhang included
    mov cx, [vid_desk_zx]               ; x2 - ordinal 0's left edge, which IS
    add cx, DESK_ZW + DESK_ZOVER - 1    ; the grid's right edge, plus the same
    mov bx, DESK_ZY0                    ; y1 - and ordinal 0's top row
    add dx, [desk_zh1]                  ; y2
    ret

; -----------------------------------------------------------------------------
; desk_dmg_zones - which drive zones does the damage rect touch?
; in:  [wm_dmg_*] (wm.inc); caller holds the gfx lock
; out: AL = bit n set = zone n is in the damage; the damage rect is GROWN to
;      cover every one of them. AH clobbered, everything else preserved.
;
; A zone is redrawn whole - gray fill, icon, label, highlight - so growing the
; rect to it is not slack: it is what makes wm_paint_dmg's marking pass notice
; a window sitting over that zone and redraw it too.
; -----------------------------------------------------------------------------
; THE ZONE MASK IS ONE BYTE, and DVOL_MAX is exactly at its limit. It is built
; here a bit at a time (`shl ax, cl` into DI, cl = 0..DVOL_MAX-1), returned in
; AL, and stored by wm_paint_dmg into [wm_dmg_zn] - a BYTE. It is spent by
; desk_paint_mask_x, which shifts it right one zone at a time and stops when
; it runs out. At 9 the ninth zone's bit is `shl ax,8` truncated by that byte,
; so that zone silently stops being repainted by every damage-driven path
; while desk_paint_x still draws it, and the symptom is a drive icon that
; survives one repaint and not the next. disk.inc guards DVOL_MAX against
; MEM_P_FATW_N the same way and for the same reason.
%if DVOL_MAX > 8
  %error "desk: the drive-zone mask is ONE BYTE - built a bit at a time here, returned in AL, and stored into [wm_dmg_zn]. Above 8 the top zones' bits do not fit it and those zones stop being repainted by every damage-driven path, silently"
%endif
desk_dmg_zones_x:
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si                  ; SI = the zone index, DI = the mask under
    xor di, di                  ; construction. NOT .bss: desk_zone_rect
                                ; writes all four GP registers, but nothing in
                                ; this loop's chain touches SI or DI -
                                ; desk_zflag -> dsk_vol_slot_x clobbers "BX
                                ; (the output), flags"; desk_zone_rect ->
                                ; desk_zone_xy -> desk_ord/desk_ord_xy push
                                ; and pop BX and CX only; wm_dmg_hit writes no
                                ; register at all and wm_dmg_add pushes SI to
                                ; keep the same promise
.zone:
    mov ax, si
    mov bl, al
    call desk_zflag             ; AL survives it
    jz .next
    call desk_zone_rect
    call KERNEL_SEG:cw_wm_dmg_hit             ; preserves the rect
    jnc .next
    call KERNEL_SEG:cw_wm_dmg_add
    mov cx, si
    mov ax, 1
    shl ax, cl                  ; 8086: a variable shift goes through CL
    or di, ax
.next:
    inc si
    cmp si, DVOL_MAX
    jb .zone
    mov ax, di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; -----------------------------------------------------------------------------
; desk_zones_paint - repaint the drive zones and what they disturb (26.2)
; in:  the gfx lock HELD
; out: nothing (all registers preserved)
;
; What a mount or an unmount actually owes the screen. It used to be
; wm_paint_all - the whole desktop dither, every visible window's frame and
; every W_PAINT - to make one icon appear at the right-hand edge.
;
; wm_paint_dmg (SPEC.md 11.91) is the right size of hammer: the dither
; clipped to the zones that moved, the zones themselves, the chrome, and only
; the windows that overlap. On a desktop with nothing near the right edge that
; is the zones and nothing else.
;
; The high-water mark is spent HERE and nowhere else: once these pixels are
; back, the screen shows exactly the zones that exist, so the next change
; starts counting again from what it can see.
; -----------------------------------------------------------------------------
desk_zones_paint_x:
    push ax
    push bx
    push cx
    push dx
    call desk_zones_dmg
    mov byte [desk_zhw], 0
    call KERNEL_SEG:cw_wm_paint_dmg
    jmp kretfc_dx               ; far-RETURNING, so both callers far-call it
                                ; and the resident thunk is not needed

; -----------------------------------------------------------------------------
; desk_click - desktop hit test, called by ui.inc when wm_hit found no
;              window under an EVT_MDOWN. NO lock held on entry; selection
;              flips redraw only the affected zones under an internally
;              acquired gfx_lock/gfx_unlock.
;
;              Zone hit: same zone as desk_sel and click-tick delta < 9
;              -> double-click: clear the selection (redraw under lock,
;              release), then files_open_drive AL=drive (locks internally):
;              it fronts a window already showing that drive's root, or
;              opens one, or at the cap moves the front one (SPEC.md 22.1).
;              Else select it, stamp desk_clkt. Miss: clear any selection.
; ins:  CX = x, DX = y (absolute screen px)
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
desk_click_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, cx                  ; DI/SI = the click, and CL = the volume:
    mov si, dx                  ; desk_zone_xy ANSWERS in AX and DX, so the
    xor cl, cl                  ; index cannot live in either
.zone:
    mov bl, cl
    call desk_zflag
    jz .zstep
    mov al, cl
    call desk_zone_xy           ; AX = zone left, DX = zone top
    cmp di, ax
    jb .zstep
    mov bx, ax
    add bx, DESK_ZW-1
    cmp di, bx
    ja .zstep
    cmp si, dx
    jb .zstep
    mov bx, dx
    add bx, [desk_zh1]
    cmp si, bx
    jbe .hit
.zstep:
    inc cl                      ; the bound is at the BOTTOM: DVOL_MAX is a
    cmp cl, DVOL_MAX            ; compile-time constant >= 1, so a do-while
    jb .zone                    ; cannot under-run - and .miss is then inside
    jmp short .miss             ; short range, where a top test needed NASM's
                                ; 5-byte `jcc +3 / jmp near` trampoline

.hit:
    mov al, cl                  ; AL = the volume index
    cmp al, [desk_sel]
    jne .select
    mov bx, [ui_click_t]        ; same zone: double-click? Compare click
    sub bx, [desk_clkt]         ; birth ticks (EV_C), wrap-safe - clicks
                                ; queued behind a slow mount must not
                                ; collapse into a double-click
    cmp bx, DESK_DBLT
    jae .select                 ; too slow - and .select's own first compare
                                ; finds "same zone" and returns, which IS the
                                ; restamp, with the tick already stamped
    ; --- double-click: deselect + redraw under our lock, then open ----------
    call KERNEL_SEG:cw_gfx_lock               ; the store is INSIDE the lock, here and in
    mov byte [desk_sel], 0xFF   ; both paths below: desk_zone_redraw's partial
    call desk_zone_redraw       ; case flips the highlight rather than redrawing
    call KERNEL_SEG:cw_gfx_unlock             ; it, so no other painter may see the new
                                ; selection and act on it in between
    call files_open_drive_x       ; AL = drive; must be called with NO lock
    jmp .out                    ; held - it locks internally (SPEC 22)

.select:                        ; new selection: redraw old + new zones -
    mov bx, [ui_click_t]        ; and, arriving from the too-slow arm above,
    mov [desk_clkt], bx         ; the restart of the timer on its own
    mov ah, [desk_sel]          ; AH = old zone (0xFF = none). READ above the
    cmp ah, al                  ; lock, which is safe because this routine is
    je .out                     ; desk_sel's only runtime writer (desk_init
                                ; writes it once at boot): equal means the
                                ; too-slow arm, and there is nothing to redraw
    call KERNEL_SEG:cw_gfx_lock
    mov [desk_sel], al          ; the STORE is still inside it (SPEC.md 26.3)
    cmp ah, 0xFF
    je .selnew
    xchg al, ah                 ; deselect redraw of the old zone
    call desk_zone_redraw
    xchg al, ah
.selnew:
    call desk_zone_redraw       ; selected redraw of the new zone
    call KERNEL_SEG:cw_gfx_unlock
    jmp .out

.miss:
    mov al, [desk_sel]          ; empty desktop click clears the selection
    cmp al, 0xFF
    je .out
    call KERNEL_SEG:cw_gfx_lock
    mov byte [desk_sel], 0xFF
    call desk_zone_redraw       ; AL = the old zone
    call KERNEL_SEG:cw_gfx_unlock

.out:
    jmp kretfc_di

; -----------------------------------------------------------------------------
; desk_zone_redraw - flip one zone's SELECTION outside wm_paint_all
; in:  AL = zone index, whose selection state has just changed; caller holds
;      the gfx lock
; out: nothing; preserves all registers
;
; The zone's drawn rect (desk_zone_rect: the gray refill spans the hit zone
; plus DESK_ZOVER either side, because the label overhangs it) may be
; overlapped by a visible window, and the gfx_* primitives clip to the
; SCREEN, so a bare desk_draw_zone would
; paint desktop pixels straight over that window. This used to answer that
; with a full wm_paint_all, which cost the whole screen twice for one click:
; picking a new icon redraws the old zone and the new one, and each of them
; repainted the world. wm_clip_rect (SPEC.md 11.3) is the region arithmetic
; that replaces it, and there are four answers (SPEC.md 26.3):
;
;   overflow    - more than 16 fragments; nothing about the region is known,
;                 so the old full repaint stays as the last resort
;   covered     - not one pixel of the zone is on screen: draw NOTHING
;   clear air   - the whole drawn rect is inside one fragment: the cheap
;                 unclipped single-zone redraw, exactly as before
;   partial     - draw only what actually changed, which is the highlight:
;                 gfx_xor_fill clips per fragment, so it lands on the visible
;                 part of the icon and nowhere else. Everything else in the
;                 zone - the gray, the icon, the label - is already on screen
;                 and identical either way, and redrawing it clipped would
;                 lose the icon anyway (ico_core clips whole-shape).
;
; That last case is why this is a FLIP and not a repaint, and why the caller
; must have changed [desk_sel] under the same lock hold: XOR is self-inverse,
; so it is exact only against pixels that still carry the previous state.
; -----------------------------------------------------------------------------
desk_zone_redraw:
    push ax
    push bx
    push cx
    push dx

    push ax                     ; keep the volume index for the draw call
    call desk_zone_rect         ; AX/BX = x1/y1, CX/DX = x2/y2 (inclusive)
    call KERNEL_SEG:wm_clip_rect              ; the zone less every visible window; the rect
                                ; survives in AX/BX/CX/DX for the test below
    jc .full                    ; too many fragments: repaint the world
    jz .none                    ; wholly covered: nothing of it is on screen
    call KERNEL_SEG:cw_wm_clip_test           ; is the WHOLE zone inside one fragment?
    pop ax                      ; BELOW the test, which reads the rect out of
                                ; AX/BX/CX/DX - and `pop` writes no flags, so
                                ; clip_test's CF crosses it and both arms
                                ; share the one restore
    jc .part
    call KERNEL_SEG:cw_wm_clip_clear          ; clear air, so the region has no work
    call desk_draw_zone         ; to do and the icon draws whole
    jmp short .done
.part:
    call desk_zone_hilite       ; cut by a window: flip the visible part of
    jmp short .done             ; the highlight and touch nothing else
.full:
    pop ax
    call KERNEL_SEG:cw_wm_paint_all
    jmp short .done
.none:
    pop ax                      ; the list is already empty, i.e. disarmed
.done:
    jmp kretc_dx

; -----------------------------------------------------------------------------
; desk_zone_hilite - XOR one zone's hit rect: the selection highlight, and
;                    the ONLY thing that differs between a selected zone and
;                    an unselected one. Self-inverse, so it both applies and
;                    removes. Internal.
; ins:  AL = volume index; caller holds the gfx lock (a clip region may be
;       armed - gfx_xor_fill clips per fragment)
; outs: none; clobbers AX, BX, CX, DX - both call sites have them pushed and
;       dead, and this is the last thing either does
; -----------------------------------------------------------------------------
desk_zone_hilite:
    call desk_zone_rect         ; the DRAWN rect...
    add ax, DESK_ZOVER          ; ...less the label's overhang at each edge,
    sub cx, DESK_ZOVER          ; which is the HIT rect this one wants
    call KERNEL_SEG:cw_gfx_xor_fill  ; NOT a tail call any more, and it cannot
    ret                         ; be: a cw_ shim ends in `retf`, so jumping to
                                ; one pops this routine's NEAR return address
                                ; as CS:IP and lands in the weeds. The cost is
                                ; one `ret` - the frame still has nothing to
                                ; restore (SPEC.md 2.6)

; -----------------------------------------------------------------------------
; desk_draw_zone - repaint one drive zone from scratch: gray desktop fill
;                  (parity dither aligns with the surroundings), disk icon,
;                  label, and the XOR highlight when the zone is selected.
;                  Used by desk_paint and desk_click. Internal.
; ins:  AL = drive index (0/1); caller holds the gfx lock
; outs: none (all registers preserved; [gfx_color] restored)
; -----------------------------------------------------------------------------
desk_draw_zone:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ah, [gfx_color]         ; byte only - gfx_lock_flag lives next door.
    push ax                     ; The pen goes on the STACK, not in .bss, and
                                ; it is read HERE, above cw_thm_desk: under
                                ; OS88_THEME thm_desk writes [gfx_color]
                                ; itself (vga12.inc), so a read below it would
                                ; bank the wrong pen. AL is still the volume
    mov [desk_zdrv], al         ; index - a BYTE, bounded at DVOL_MAX-1 = 7
    call desk_zone_xy           ; AX = zone left, DX = zone top
    mov [desk_zx], ax
    mov [desk_zy], dx

    ; --- gray desktop under the zone + the label's 2px side overhang --------
    call desk_rect_ax           ; the SAME rect desk_zone_rect builds, written
                                ; out here in a different instruction order:
                                ; x1 = left-2, x2 = (left-2)+32+4-1 = left+33,
                                ; y1 = top, y2 = top+[desk_zh1]. AX and DX
                                ; still hold the origin - the two stores to
                                ; [desk_zx]/[desk_zy] above do not touch them
    call KERNEL_SEG:cw_thm_desk

    ; --- the icon, which IS the zone's width now (SPEC.md 26.4) --------------
    mov cx, [desk_zx]
    mov dx, [desk_zy]
    mov si, [desk_pdisk]
    mov bl, [desk_zdrv]         ; dsk_vol_fixed_x takes BL and answers in AX,
    call dsk_vol_fixed_x          ; the MEDIUM, not the transport (SPEC.md
    jc .icon                    ; 18.7.2). This was `cmp DV_KIND, DVK_DRV`,
    or al, al                   ; which is the TRANSPORT - so a hard disk
    jz .icon                    ; booted through the BIOS (DVK_BIOS, unit
    mov si, [desk_phdd]         ; 80h+) drew the FLOPPY icon, which is what a
.icon:                          ; machine booting off its own disk always saw
    call KERNEL_SEG:cw_icon_draw_ix   ; the four are the INDEXED kind
                                ; (SPEC.md 25.7) and this is their only site

    ; --- label: the white rect HUGS the caption (SPEC.md 26.4) ---------------
    ; It used to be the zone's full width whatever was in it, which was right
    ; while the caption was six glyphs and exactly filled it. 'A:' is two, and
    ; a 36px bar with 16px of text in it reads as a bar with a letter lost in
    ; the middle - so the width is measured and the rect follows it.
    ;
    ; CLAMPED to the zone plus its overhang, which is the bound desk_zone_rect
    ; and desk_dmg_zones repaint: nothing in the tree gives a volume a DV_LBL
    ; of its own today (the kernel names them, SPEC.md 52.4), but a driver
    ; that did would otherwise letter outside the rect that erases it.
    mov al, [desk_zdrv]
    call desk_zone_label        ; SI -> it, the row's or a derived 'A:'
    push si
    call KERNEL_SEG:font_width             ; AX = its pixel width
    mov bx, [desk_zx]           ; BX = the zone's left, held for BOTH clamps
    mov cx, DESK_ZW             ; and for the lea below
    sub cx, ax
    sar cx, 1                   ; SIGNED, and this is the load-bearing half:
    add cx, bx                  ; font_width is 8*length and DESK_ZW is 32, so
                                ; a caption of five glyphs or more makes
                                ; 32-w NEGATIVE. `shr` would turn zx-12 into
                                ; zx+32756, and the pen is NOT clamped - only
                                ; the white rect below is - so the caption
                                ; would letter outside the rect that erases it.
                                ; CX = the caption's left, centred in the zone
    push cx                     ; ...banked on the stack across cw_gfx_fill
    mov dx, cx                  ; DX = the second copy, for the left clamp
    add ax, cx
    dec ax                      ; AX = the caption's right
    lea cx, [bx+DESK_ZW-1]      ; BX is a legal 16-bit base and AX is not,
    cmp ax, cx                  ; which is why this trick is here and not in
    jbe .rr                     ; desk_rect_ax. DESK_ZOVER comes OUT of both
    mov ax, cx                  ; clamps: min(R+2, zx+33) == min(R, zx+31)+2
.rr:                            ; and max(l-2, zx-2) == max(l, zx)-2
    mov cx, ax
    add cx, DESK_ZOVER          ; CX = x2, one overhang out
    mov ax, dx
    cmp ax, bx
    jae .rl
    mov ax, bx
.rl:
    sub ax, DESK_ZOVER          ; AX = x1
    mov dx, [desk_zy]
    add dx, [desk_zh1]          ; y2 = [desk_zy] + [desk_zh1] exactly, and
    mov bx, dx                  ; zh1 == icoh + zgap + DESK_LBLH - 1 by
    sub bx, DESK_LBLH - 1       ; construction on BOTH desk_rowcalc_x arms and
                                ; at the .text resting value - so y1 comes off
                                ; it and [desk_icoh]/[desk_zgap] are not read
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill

    ; --- ...and the caption in it, black -------------------------------------
    ; OPAQUE (SPEC.md 26.6): the white rect above is DESK_LBLH = 12 rows and a
    ; glyph is 8, so the fill stays and the run buys the cells - 22.11.3's
    ; rule. The rect is still needed for the 2px of white above and below.
    mov byte [gfx_color], CBLACK
    pop cx                      ; the pen, then the label - the push order
    pop si                      ; was si first, so the pops run the other way
    sub dx, DESK_LBLH - 3       ; DX IS STILL y2: gfx_fill's header is
                                ; "clobbers: flags" (vga12.inc) and cw_gfx_fill
                                ; is `call gfx_fill / retf`, so y = y2-9, which
                                ; is y1+2 - the 2px of white above the text.
                                ; dock_paint rides this same contract
    mov ax, (CWHITE << 8) | CBLACK
    call KERNEL_SEG:cw_font_run

    ; --- selection highlight: XOR the whole hit zone --------------------------
    mov al, [desk_sel]
    cmp al, [desk_zdrv]         ; the fall-through IS AL == [desk_zdrv], so the
    jne .done                   ; reload the old code did here was dead;
    call desk_zone_hilite       ; desk_zone_hilite takes AL and its chain
.done:                          ; (desk_zone_rect -> desk_zone_xy -> desk_ord)
                                ; zeroes AH itself. One place owns the
                                ; highlight's geometry, so the full repaint
                                ; and the partial flip cannot disagree
    pop ax
    mov [gfx_color], ah
    jmp kretc_si

section .text                   ; DATA, so back to the kernel segment (SPEC.md
                                ; 2.6 rule 1). These labels carry NO COLON,
                                ; which is why a scan keyed on `name:` missed
                                ; them and moved the lot into .cold - where DS
                                ; still names KERNEL_SEG, so desk_pdisk read as
                                ; garbage and the comment below came true
desk_lbl_gen db "A:", 0         ; scratch for desk_zone_label's derived form:
                                ; a volume with no label of its own is named
                                ; from its index, in place

; --- what the ADAPTER decides (SPEC.md 26.4) ---------------------------------
; Set by desk_rowcalc, which is also what vid_switch re-runs, so these follow
; the machine being moved between its two cards. In .text with real
; initialisers and not .bss, for the reason every boot-reachable table here
; is: -f bin zeroes nothing, and a zero [desk_pdisk] draws the interrupt
; vector table as an icon.
desk_zh1    dw 32 + DESK_ZGAP + DESK_LBLH - 1   ; ...the zone's, MINUS ONE:
                                ; every reader wants the inclusive bottom
desk_zstep  dw DESK_ZSTEP       ; zone-to-zone pitch
desk_pdisk  dw ico_disk32       ; the diskette at that height...
desk_phdd   dw ico_hdd32        ; ...and the hard disk

section .cold                   ; ...and back to cold code for the far entries

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above.
; =============================================================================
; -----------------------------------------------------------------------------
section .bss
desk_rows    resw 1             ; zones that fit in one column above the dock
                                ; (SPEC.md 26.1) - 7 on VGA, 4 on Hercules,
                                ; 2 on CGA, which is why the wrap exists
desk_zdirty  resb 1             ; 1 = a volume was added or removed and the
                                ; drive zones owe a repaint (SPEC.md 26.3)
desk_zhw     resb 1             ; ...and the largest number of zones the screen
                                ; may be showing while that debt stands, which
                                ; is what sizes the damage rect. desk_zmark
                                ; raises it, desk_zones_paint spends it. 0 is
                                ; "no pending change", and .bss arrives zeroed
desk_sel     resb 1             ; selected VOLUME index, 0xFF = none
desk_clkt    resw 1             ; click birth tick (EV_C) of the last zone click
desk_zy      resw 1             ; desk_draw_zone: current zone top
desk_zx      resw 1             ; ...and its left column (SPEC.md 26.1)
desk_zdrv    resb 1             ; desk_draw_zone: current volume index - a
                                ; BYTE, because DVOL_MAX bounds it at 7 and
                                ; every reader wants AL or BL. The saved
                                ; [gfx_color] that used to sit here is on the
                                ; routine's own stack now
section .text
