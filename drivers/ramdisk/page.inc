; =============================================================================
; os8088 - RAMPAGE.DRV / page.inc
;
; The Control Panel's Ram Disk page (SPEC.md 62.9.11).
;
; The pane is 221 x 132 px - 27 characters by 14 rows - and the panel is not
; resizable, so this page is two setting rows, five buttons and three lines of
; caption, and anything that needed more room would be a window of its own.
;
;   Ram Disk
;   Size [  64]K  [-] [+]
;   Store: (o) Conv  ( ) Xms
;   [Mount]  [Preserve]  [Load]
;   [Preserve As]  [Clear]
;   Mounted as D: 56K free of 64K
;   Boot image: BACKUP.RAM
;
; **EVERY STRING HERE FITS ITS BOX, and that is a rule and not a habit.** The
; caption column is 27 characters and font_str clips to the SCREEN and not to
; the pane (SPEC.md 5), so a longer one does not stop at the edge - it paints
; over the panel's border and out the other side.
;
; GREYING FOLLOWS SPEC.md 47: one predicate per control, shared by the
; greying, the click refusal and the explanation; a FACT and never a guess;
; and the whole control, not just its caption. The pen comes from
; OSAPI_GFX_PEN, so a disabled button carries [gfx_dis] as well as CDGRAY and
; its LABEL dithers on the two 1bpp adapters instead of coming out solid black
; next to a dotted frame.
;
; NOTHING HERE WRITES A DISK ON A CLICK except Preserve and Load, which are
; not settings: they are a file the user asked for at the moment they asked
; for it. Every SETTING is staged and the panel's close writes it (SPEC.md
; 31.8), which is the resident's rd_cfg_mark and never this image's business.
; =============================================================================

RP_PANEW    equ 221             ; the pane, and the whole of what may be drawn
RP_PANEH    equ 132

RP_HY       equ 4               ; the heading's text row
RP_SY       equ 18              ; the Size row
RP_LY       equ 34              ; ...and the Store row
RP_LX       equ 2

RP_SBX      equ 42              ; the size box - SIX characters wide, because
RP_SBW      equ 60              ; RD_MAXKB is 16,384 and a typed one needs its
RP_STX      equ RP_SBX + 6      ; five digits AND the caret cell beside them
RP_SW       equ 6               ; (SPEC.md 62.9.11.2)
RP_SKX      equ 106             ; the 'K'
RP_MX       equ 122             ; '-' and '+'
RP_PX       equ 146
RP_MW       equ 18
RP_FH       equ 12

RP_RX0      equ 56              ; the two Store radios
RP_RX1      equ 130
RP_RTX0     equ 72              ; ...and their labels
RP_RTX1     equ 146
RP_RW0      equ 48              ; how wide each control is, GLYPH AND LABEL:
RP_RW1      equ 40              ; a radio's label is part of it (47 rule 2)

RP_B0Y      equ 52              ; the first button row
RP_B1Y      equ 72              ; ...and the second
RP_BH       equ 16

RP_CAP0     equ 96              ; the three caption rows
RP_CAP1     equ 108
RP_CAP2     equ 120

; --- the controls, as {x1,y1,x2,y2} inclusive, PANE-RELATIVE -----------------
; ONE PLACE FOR EACH RECT, which is SPEC.md 22's fm_hit discipline: the drawn
; control and the clickable control cannot drift, because they are the same
; four words read by the paint and by the hit test.
    align 4, db 0               ; RP_BLBL/RP_ACTS below divide a rect's id
rp_r_sbox:   dw RP_SBX, RP_SY-3, RP_SBX+RP_SBW-1, RP_SY-3+RP_FH-1
rp_r_minus:  dw RP_MX, RP_SY-2, RP_MX+RP_MW-1, RP_SY-2+RP_FH-1
rp_r_plus:   dw RP_PX, RP_SY-2, RP_PX+RP_MW-1, RP_SY-2+RP_FH-1
rp_r_mount:  dw RP_LX, RP_B0Y,  RP_LX+63,      RP_B0Y+RP_BH-1
rp_r_pres:   dw 72,    RP_B0Y,  72+71,         RP_B0Y+RP_BH-1
rp_r_load:   dw 152,   RP_B0Y,  152+63,        RP_B0Y+RP_BH-1
rp_r_presa:  dw RP_LX, RP_B1Y,  RP_LX+95,      RP_B1Y+RP_BH-1
rp_r_clear:  dw 106,   RP_B1Y,  106+63,        RP_B1Y+RP_BH-1
; ...and the two Store radios' HIT bands, straight after so os88ui_bfind walks
; all nine as one array. A radio's band is the GLYPH AND ITS LABEL, because a
; radio's label is part of the control (SPEC.md 47 rule 2) - and a 12px target
; on a 1bpp screen is a target most mice miss. They are not drawn from, a
; radio being a glyph and a run rather than a rect.
rp_r_rad0:   dw RP_RX0, RP_LY-3, RP_RX0+RP_RW0, RP_LY-3+OS88UI_GW
rp_r_rad1:   dw RP_RX1, RP_LY-3, RP_RX1+RP_RW1, RP_LY-3+OS88UI_GW

; ...and what each BUTTON says and asks, in rp_r_minus's own order, so one
; table drives the full paint, the per-control repaint and nothing else. The
; rects stay the id space (a rect pointer is what the hit test answers and
; what every painter takes), and this is indexed off it by arithmetic rather
; than by a second numbering that could drift: (rect - rp_r_minus) / 8 * 4.
;
; rp_live is ALWAYS LIVE, which is a claim and not an omission - Mount and
; Load are both SPEC.md 47 rule 3's case, where the only honest test is doing
; the thing. It is a predicate like the others so that every row is CALLED.
rp_blbl:                        ; indexed rect / 2: see RP_BLBL below
    dw rp_s_minus, rp_size_ok
    dw rp_s_plus,  rp_size_ok
    dw rp_s_mount, rp_live
    dw rp_s_pres,  rp_pres_ok
    dw rp_s_load,  rp_live
    dw rp_s_presa, rp_presa_ok
    dw rp_s_clear, rp_clear_ok
RP_NBTN     equ 7

; ...and what each one DOES on the release, in the same order again and indexed
; (rect - rp_r_minus) / 4. rp_act asks the predicate above before it gets here,
; so the greying, the refusal and the action cannot be three different tests.
rp_acts:
    dw rp_a_minus, rp_a_plus, rp_a_mount, rp_a_pres, rp_a_load, rp_a_presa
    dw rp_a_clear

; A rect's id halved is its row's offset in rp_blbl once the table is moved
; down by the first rect's half, and halved again its offset in rp_acts - so
; neither lookup subtracts anything. It needs the rects on even addresses,
; and four-byte for the second, which eight-byte rects off a 4-aligned
; first one are.
RP_BLBL     equ rp_blbl - (rp_r_minus - $$) / 2
RP_ACTS     equ rp_acts - (rp_r_minus - $$) / 4
%if (rp_r_minus - $$) % 4 != 0
  %error "RP_BLBL/RP_ACTS index by rect/2 and rect/4, so the rects must be 4-aligned"
%endif

rp_r_pane:   dw 0, 0, RP_PANEW-1, RP_PANEH-1    ; rp_repaint's white fill

; A radio's label sits a glyph and a gap right of its glyph, which is what
; lets rp_radio carry ONE x per radio rather than two
%if RDL_CONV != 0 || RDL_XMS != 1
  %error "rp_radio and the paint take RDL_CONV as zero and RDL_XMS as one"
%endif
%if RP_RTX0 != RP_RX0 + 16 || RP_RTX1 != RP_RX1 + 16
  %error "rp_radio derives a label's x from its glyph's"
%endif

rp_s_page:   db 'Ram Disk', 0
rp_s_size:   db 'Size', 0
rp_s_k:      db 'K', 0
rp_s_minus:  db '-', 0
rp_s_plus:   db '+', 0
rp_s_store:  db 'Store:', 0
rp_s_conv:   db 'Conv', 0
rp_s_xms:    db 'Xms', 0
rp_s_mount:  db 'Mount', 0
rp_s_unmnt:  db 'Unmount', 0
rp_s_pres:   db 'Preserve', 0
rp_s_presa:  db 'Preserve As', 0
rp_s_load:   db 'Load', 0
rp_s_clear:  db 'Clear', 0
rp_s_boot:   db 'Boot image: ', 0
rp_s_noboot: db 'No boot image', 0
rp_s_as:     db 'Mounted '        ; 'Mounted D: 224K of 224K' is 23 of the 27
rp_s_asvol:  db '?: ', 0          ; (the letter is written in before each use)
rp_s_free:   db 'K of ', 0       ; characters this pane holds, and the worst
                                 ; case is what has to fit: `Mounted as` and a
                                 ; spelt-out `free` made it 29, which font_str
                                 ; drew straight through the window's border
                                 ; and out the other side - the failure this
                                 ; file's own header names
rp_s_notmnt: db 'Not mounted', 0
rp_s_defnam: db 'RAMDISK.RAM', 0
rp_s_dotram equ rp_s_defnam + 7 ; '.RAM', its tail

; What went wrong, RDERR_MEM onwards, one after another and found by counting
; NULs (rp_paint): no table, because the strings ARE the order. RDERR_NONE has
; no row - it is the one verdict that draws nothing, so the caption line simply
; is not drawn - and RDS_ERR is READ-AND-CLEAR at the resident, so a verdict
; cannot outlive its cause and be reported again on the next unrelated paint
; (SPEC.md 59.5).
%if RDERR_NONE != 0 || RDERR_MEM != 1 || RDERR_VOL != 2 || RDERR_MNT != 3 || RDERR_FILE != 4 || RDERR_FMT != 5 || RDERR_BIG != 6 || RDERR_NOIMG != 7 || RDERR_NERR != 8
  %error "rp_e_mem.. are RDERR_MEM..RDERR_NOIMG in order, and RDERR_NONE is the 0 rp_size_fast ORs"
%endif
rp_e_mem:    db 'Not enough memory', 0
rp_e_vol:    db 'No free drive letter', 0
rp_e_mnt:    db 'The volume was refused', 0
rp_e_file:   db 'Disk error on the image', 0
rp_e_fmt:    db 'Not a Ram Disk image', 0
rp_e_big:    db 'That image is too big', 0
rp_e_noimg:  db 'Nothing preserved yet', 0

; =============================================================================
; PAINT
; =============================================================================

; -----------------------------------------------------------------------------
; rp_paint - RDT_PAINT
; in:  DI = pane left, DX = pane top; the pane is white-filled and the gfx
;      lock is HELD (SPEC.md 31.9)
; out: nothing (clobbers everything - rampage.asm's REGISTERS)
; -----------------------------------------------------------------------------
rp_paint:
    call rp_bank
    jmp short rp_paint_at

; -----------------------------------------------------------------------------
; rp_repaint - the pane, white, then the page over it
; in:  [rp_px] / [rp_py]
; out: nothing (clobbers everything)
;
; A page draws itself UNCLIPPED and the kernel does not repaint after a click
; returns (SPEC.md 31.9), so this is the page's own job - and the page draws
; as if over the kernel's white pane, so the white comes first.
; -----------------------------------------------------------------------------
rp_repaint:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, rp_r_pane
    call rp_rect_abs
    call OSAPI_GFX_FILL
                                ; ...and falls into the paint

rp_paint_at:                    ; rp_repaint's way in, the origin banked
    call rp_state               ; the live state, on EVERY paint: the panel
                                ; stays clickable under a dialog's completion
                                ; and a mount can happen between two paints

    call rp_pen0
    mov si, rp_s_page
    mov cx, RP_LX
    mov dx, RP_HY
    call rp_str

    ; --- the size row --------------------------------------------------------
    ; ONE predicate, shared by the greying, the click refusal and the
    ; explanation (47 rule 2) - and one PEN for the row, the label, the box
    ; and the value being one control with nothing between them that moves it
    call rp_size_ok
    call rp_pen
    call rp_size_text           ; rp_line = the value, or what is being typed
    mov si, rp_s_size
    mov cx, RP_LX
    mov dx, RP_SY
    call rp_str
    mov si, rp_line
    mov cx, RP_STX
    call rp_str
    mov si, rp_s_k
    mov cx, RP_SKX
    call rp_str
    mov bx, rp_r_sbox           ; ...and the value box round it, out of the
    call rp_rect_abs            ; very rect the click tests. LAST is free:
    call OSAPI_GFX_FRAME        ; no pixel of the frame is a pixel of the run

    ; --- the store row -------------------------------------------------------
    call rp_loc_ok
    call rp_pen
    mov si, rp_s_store
    mov cx, RP_LX
    mov dx, RP_LY
    call rp_str
    xor bx, bx                  ; RDL_CONV
    call rp_radio
    mov bx, RDL_XMS
    call rp_radio

    ; --- the buttons, ALL SEVEN out of one table -----------------------------
    ; The two size cells are in it too, which is why they are no longer drawn
    ; up in the size row: `-` and `+` are buttons and want the pressed look
    ; exactly as the other five do, and a control drawn from a second place
    ; is a control whose down state has to be remembered twice.
    mov bx, rp_r_minus
.btns:
    push bx
    call rp_btn1
    pop bx
    add bx, 8
    cmp bx, rp_r_minus + 8 * RP_NBTN
    jb .btns

    ; --- the captions --------------------------------------------------------
    call rp_pen0
    mov di, rp_line             ; 'Mounted D: 56K of 64K', or 'Not mounted'
    mov si, rp_s_notmnt
    cmp byte [rp_st+RDS_MNT], 0
    je .mcap
    mov al, [rp_st+RDS_VOL]
    add al, 'A'
    mov [rp_s_asvol], al        ; the string's own letter cell
    mov si, rp_s_as
    call rp_app
    mov ax, [rp_st+RDS_FREEK]
    call rp_numapp
    mov si, rp_s_free
    call rp_app
    mov ax, [rp_st+RDS_KB]
    call rp_numapp
    mov si, rp_s_k
.mcap:
    mov dx, RP_CAP0
    call rp_cap

                                ; 'Boot image: BACKUP.RAM', or 'No boot image'
    mov si, rp_s_noboot
    cmp byte [rp_st+RDS_BOOT], 0
    je .bcap
    mov si, rp_s_boot
    call rp_app
    mov si, rp_st + RDS_NAME
.bcap:
    mov dx, RP_CAP1
    call rp_cap

    mov al, [rp_st+RDS_ERR]
    mov [rp_eshown], al         ; WHAT THE GLASS IS SHOWING, and the record is
                                ; kept here because this is the only routine
                                ; that puts it there - menu_bcell's discipline
                                ; (SPEC.md 12.9) at the scale of one line
    dec al                      ; RDERR_NONE draws nothing, and nor does a
    cmp al, RDERR_NERR - 1      ; number past the strings: both are out of
    jae .noerr                  ; range now, NONE having wrapped to 0xFF
    cbw
    xchg cx, ax                 ; CX = how many strings to step over
    mov si, rp_e_mem
    jcxz .err
.skip:
    lodsb
    or al, al
    jnz .skip
    loop .skip
.err:
    mov cx, RP_LX
    mov dx, RP_CAP2
    call rp_str
.noerr:
    jmp rp_kbmark               ; ...and the size the field was drawn with

; rp_cap - append SI at DI, end the line, and letter rp_line at (RP_LX, DX);
; out: DI = rp_line again
rp_cap:
    call rp_app
    mov [di], al                ; the NUL - rp_app's AL
    mov si, rp_line
    mov di, si                  ; ...and DI back at the start, for the next
    mov cx, RP_LX
    jmp short rp_str

; -----------------------------------------------------------------------------
; rp_radio - one Store radio and its label
; in:  BX = the RDL_* it selects
; out: nothing (clobbers everything)
; -----------------------------------------------------------------------------
rp_radio:
    mov al, OS88UI_GRADIO
    cmp bl, [rp_st+RDS_LOC]
    jne .off
    or al, OS88UI_GON
.off:
    lea dx, [bx+1]              ; ...and PRESSED if it is the one being held
    cmp dx, [rp_down]           ; (SPEC.md 13.8): a radio is 1-BASED in the
    jne .nodn                   ; one id space [rp_down] carries, so 0 can
    or al, OS88UI_GDOWN         ; mean "none" and a button's own id can be its
.nodn:                          ; RECT POINTER, which no radio can collide with
    mov di, RP_RX0
    mov si, rp_s_conv
    or bl, bl                   ; RDL_CONV
    je .which
    mov di, RP_RX1
    mov si, rp_s_xms
.which:
    call rp_loc_ok              ; ...and DISABLED out of CF, kept for the
    pushf                       ; label's pen too: os88ui_glyph leaves the pen
    sbb ah, ah                  ; LIVE (its contract). AH = 0 or 0xFF
    mov cx, di
    add cx, [rp_px]
    mov dx, RP_LY - 3
    add dx, [rp_py]
    call os88ui_glyph
    popf
    call rp_pen
    lea cx, [di+16]             ; the label: RP_RTXn = RP_RXn + 16
    mov dx, RP_LY
    jmp short rp_str

; =============================================================================
; THE PREDICATES (SPEC.md 47 rule 2)
;
; Each answers CF - 0 live, 1 disabled - touches nothing else, and is called
; by BOTH the paint and the click, so a control that looks unavailable cannot
; act and one that acts cannot look unavailable.
;
; RDS_MNT, RDS_XMS and RDS_BOOT are EXACTLY 0 or 1 - rd_sv_state stores them
; that way - so `cmp byte [x], 1` IS the test: CF = 1 exactly when x is 0,
; and a CMC makes that "exactly when x is 1".
; =============================================================================

rp_loc_ok:                      ; a store cannot be moved under a volume, and
    cmp byte [rp_st+RDS_XMS], 1 ; a machine with no store above 1MB has
    jc rp_pdone                 ; nowhere to move it to. A FACT the code can
                                ; test (47 rule 3)
rp_size_ok:                     ; ...nor resized under one
    cmp byte [rp_st+RDS_MNT], 1
    cmc
rp_pdone:
    ret

rp_pres_ok:                     ; Preserve needs somewhere already chosen to
    cmp byte [rp_st+RDS_BOOT], 1    ; write it AND something to write
    jc rp_pdone
rp_presa_ok:                    ; ...Preserve As only needs the volume
    cmp byte [rp_st+RDS_MNT], 1
    ret

rp_clear_ok:                    ; ...and Clear, an association to drop
    cmp byte [rp_st+RDS_BOOT], 1
    ret

; =============================================================================
; DRAWING HELPERS - every one of them takes PANE-RELATIVE coordinates
;
; The layout above reads pane-relative and every gfx primitive takes SCREEN
; coordinates (SPEC.md 5). ONE place does the addition, so the click test and
; the paint cannot disagree about which of the two a number is.
; =============================================================================

; rp_pen - OSAPI_GFX_PEN, and the INK a run must letter in, from one CF
; rp_pen0 - ...for a live control
; in:  CF = SPEC.md 47's argument (0 live, 1 disabled)
; out: the pen set and [rp_ink] banked; AL clobbered, CF kept
;
; A DRIVER CANNOT READ [gfx_color] BACK any more than a package can (SPEC.md
; 52.12): there is no OSAPI_GET_COLOR, so the ink has to be decided at the
; branch that decides the flag. Every one of this page's pen sites goes
; through here, so rp_str never has to ask. The FLAG stays the pen's, which is
; what leaves SPEC.md 6.1.12 to checkerboard a disabled caption for free.
rp_pen0:                        ; ...or LIVE, said in one call
    clc
rp_pen:
    mov al, CBLACK
    jnc .set
    mov al, CDGRAY
.set:
    mov [rp_ink], al
    call OSAPI_GFX_PEN
    ret

; rp_str  - SI = a string, CX/DX = a pane-relative pen, lettered in [rp_ink]
; rp_stra - ...in AL instead
; out: AX and CX clobbered; DX kept, so a row of runs sets its y once
rp_str:
    mov al, [rp_ink]
rp_stra:
    push dx
    add cx, [rp_px]
    add dx, [rp_py]
    mov ah, CWHITE              ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    pop dx
    ret

; rp_rect_abs - BX = a pane-relative rect -> AX/BX/CX/DX in SCREEN
; coordinates, which is what OSAPI_GFX_FRAME and _FILL take, and the same four
; words into rp_rect, which is what the button record names
rp_rect_abs:
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov bx, [bx+2]
    add ax, [rp_px]
    add cx, [rp_px]
    add bx, [rp_py]
    add dx, [rp_py]
    mov [rp_rect+0], ax
    mov [rp_rect+2], bx
    mov [rp_rect+4], cx
    mov [rp_rect+6], dx
    ret

; rp_bank - bank a verb's pane origin (DI, DX), and turn the y RDT_CLICK and
; its two siblings carry in BX into the DX every hit test takes
rp_bank:
    mov [rp_px], di
    mov [rp_py], dx
    mov dx, bx
    ret

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; A Control Panel page draws its buttons one at a time, so the record's rects
; are rp_rect, which rp_rect_abs has just filled with whichever button this
; is. The GESTURE here is the panel's own cp_ctl/cp_pgprobe probe (SPEC.md
; 13.8.4), which is already release-fired; what changed is that the DRAW goes
; through the control.
rdp_btlbl: dw 0
rdp_btflg: dw 0
    OS88UI_BTNREC rdp_btrec, rp_rect, rdp_btlbl, rdp_btflg, 1

; -----------------------------------------------------------------------------
; rp_btn1 - one button, out of rp_blbl
; in:  BX = its rect (which is its id)
; out: nothing (clobbers everything)
;
; The label and the predicate come from the table rather than from the call
; site, which is what lets the full paint and the pressed-state redraw be the
; SAME drawing: a control repainted on a press cannot come back looking
; different from the one the pane was drawn with, because there is one body.
;
; OS88UI_FILL because a page is drawn over a pane the kernel white-filled once
; and then over ITSELF on every later paint - so without it a second draw's
; checkerboard caption lands on top of the first draw's solid one and a greyed
; label comes out pixel-identical to a live one, which reads as SPEC.md 47
; rule 1 being broken when the mechanism is fine.
; -----------------------------------------------------------------------------
rp_btn1:
    mov di, bx                  ; the row: /8 for the rect, *4 for the entry,
    shr di, 1                   ; which is ONE shift and not two - two is /4,
                                ; so `+` read its LABEL out of the predicate
                                ; column and then CALLED the string beside it
    mov si, [RP_BLBL+di]
    cmp di, (rp_r_mount - $$) / 2   ; Mount's label says what it will DO,
    jne .lbl                    ; which is the one thing a static table cannot
    cmp byte [rp_st+RDS_MNT], 0
    je .lbl
    mov si, rp_s_unmnt
.lbl:
    call [RP_BLBL+di+2]         ; the predicate answers in CF
    mov di, OS88UI_FILL
    jnc .live
    or di, OS88UI_DIS
.live:
    cmp bx, [rp_down]           ; ...and PRESSED if it is the one being held
    jne .nodn                   ; (SPEC.md 13.8). THE RECT POINTER IS THE ID:
    or di, OS88UI_DOWN          ; the hit test already answers in one and every
.nodn:                          ; painter already takes one, so there is no
                                ; second numbering to keep in step. DIS
                                ; outranks it inside the control, so a greyed
                                ; button never says it will fire
    mov [rdp_btlbl], si         ; THE ONE CONTROL (SPEC.md 20.5.1.3): the
    mov [rdp_btflg], di         ; record takes the label and the flags, its
    call rp_rect_abs            ; rect is rp_rect, and the picture is the
    mov bx, rdp_btrec           ; control's own
    mov al, 1
    call os88ui_btn             ; a CALL and not a tail jump, so that
    ret                         ; tests/btnsites.txt's grep can see it

; -----------------------------------------------------------------------------
; rp_ctlpaint - redraw ONE control, in whatever state it is now in
; in:  BX = a control id (a rect pointer, 1 or 2 for a radio, 0 = nothing)
; out: nothing (clobbers everything)
;
; **NEVER rp_repaint.** A press has to say so within the frame it arrives in,
; and blanking a 221x132 pane and rebuilding forty drawing calls of it to
; invert one button is PERFORMANCE.md Part 1's double-draw flash with the
; whole page as the flash - twice for every click, and four times if the
; pointer is dragged off the control and back.
; -----------------------------------------------------------------------------
rp_ctlpaint:
    cmp bx, 2                   ; 1..2 is a radio and anything else is a rect
    ja rp_btn1
    dec bx                      ; ...the RDL_* it selects
    jns rp_radio
    ret                         ; 0 = nothing

; -----------------------------------------------------------------------------
; rp_setdown - AX = the control that is now held (0 = none)
; out: nothing (clobbers everything)
;
; Draws ONLY IF THAT CHANGED, and draws the two controls that changed rather
; than the pane: the one going up and the one coming down. That is what makes
; the tracking edge (SPEC.md 13.8.1) affordable - a drag delivers this at the
; mouse's rate and the common answer is "the same one", which costs a compare.
; -----------------------------------------------------------------------------
rp_setdown:
    mov bx, [rp_down]           ; the one being let go
    cmp ax, bx
    je .out
    mov [rp_down], ax
    push ax
    call rp_ctlpaint
    pop bx                      ; ...and the one taking its place
    jmp short rp_ctlpaint
.out:
    ret

; -----------------------------------------------------------------------------
; rp_hit - which control is the pane-relative point in?
; in:  CX = x, DX = y
; out: AX = the id (a rect pointer, 1 or 2 for a radio, 0 = none); BX
;      clobbered, every other register preserved
;
; THE SIZE BOX IS NOT IN IT, and that is SPEC.md 13.6 rather than an oversight:
; taking the caret is a safe prefix action, so that one control keeps the
; PRESS - and a hit test that could arm it would be a second opinion about
; which of the two it is.
; -----------------------------------------------------------------------------
rp_hit:
    mov bx, rp_r_minus          ; the seven buttons and the two radios are
    mov ax, RP_NBTN + 2         ; contiguous and eight bytes each, so
    call os88ui_bfind           ; os88ui_bfind walks them as one array
    sub ax, RP_NBTN             ; ...the radios come out 1 and 2, their ids
    jg .out
    add ax, RP_NBTN - 1         ; a button's index, or -1 for a miss
    js .none
    shl ax, 1                   ; index -> the rect itself, which is the id
    shl ax, 1
    shl ax, 1
    add ax, bx
    ret
.none:
    xor ax, ax
.out:
    ret

; =============================================================================
; CLICK
; =============================================================================

; -----------------------------------------------------------------------------
; rp_onup - RDT_UP: the RELEASE, which is where this page acts
; in:  the same four registers RDT_CLICK takes
; out: nothing
;
; The gesture is press-and-release ON THE SAME CONTROL. Anything else - a
; slide off, a release on a different control - is a CANCEL and does nothing
; but put the pressed look back.
; -----------------------------------------------------------------------------
rp_onup:
    call rp_bank
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    call rp_onit
    jne .cancel
    call rp_state               ; the world, fresh - every predicate rp_act
                                ; asks reads it, and a mount may have happened
                                ; under the panel since the last paint
    mov word [rp_down], 0       ; NOT rp_setdown: rp_act's paths that change
                                ; anything repaint the pane and draw this
                                ; control upright with them, and drawing it
                                ; upright HERE first is the double-draw
    mov ax, si                  ; the id - rp_state has spent AX
    push si                     ; ...and kept across rp_act, whose dialog
    call rp_act                 ; buttons spend SI on the name it opens with
    pop bx
    jc rp_ctlpaint              ; it did not repaint, so this one control owes
    ret                         ; its own way back up
.cancel:
    xor ax, ax
    jmp rp_setdown

; -----------------------------------------------------------------------------
; rp_ondrag - RDT_DRAG: the pressed control follows the pointer (SPEC.md 13.8.1)
; in:  the same four registers again; arrives between the press and the release
; out: nothing
;
; PEEK, never take: the arm is the release's to spend, and a drag that
; consumed it would leave the release with nothing to fire.
; -----------------------------------------------------------------------------
rp_ondrag:
    call rp_bank
    call os88ui_armed
    call rp_onit
    je .set
    xor ax, ax                  ; off it: nothing is down
.set:
    jmp rp_setdown              ; ...which draws only if that CHANGED

; -----------------------------------------------------------------------------
; rp_onit - is the point still on the control the press armed?
; in:  AX = that control (0 = none), CX/DX = the pane-relative point
; out: ZF = 1 yes, with SI = the control; AX, BX clobbered.
;
; NOTHING ARMED IS NEVER "ON IT", and that costs both callers nothing: they
; take the off-it branch, which is rp_setdown(0) - and with nothing armed
; nothing is held either ([rp_down] only ever holds 0 or the armed control:
; the press sets both, the release clears both, a drag moves [rp_down]
; between the two), so rp_setdown's own compare draws nothing.
; -----------------------------------------------------------------------------
rp_onit:
    mov si, ax
    or ax, ax
    jnz .armed
    inc ax                      ; ZF = 0
    ret
.armed:
    call rp_hit
    cmp ax, si
    ret

; -----------------------------------------------------------------------------
; rp_click - RDT_CLICK: the PRESS, which arms and draws and does nothing else
; in:  DI = pane left, DX = pane top, CX = rel x, BX = rel y (lock HELD)
; out: nothing
;
; SPEC.md 13.8.4. Every control on this page but one wanted the release: Mount
; unmounts a live volume, Clear forgets the boot image, Preserve and Load put
; a floppy dialog up, and `-`/`+` resize a store - none of them has a safe
; prefix action (13.6), so a mis-aimed press did the thing with no way to
; think better of it. The exception is the SIZE BOX, where taking the caret is
; exactly a safe prefix action, so it still fires here.
;
; **rp_state is NOT called here.** RDS_ERR is read-and-clear at the resident
; (SPEC.md 59.5) and only a path that REPAINTS can put the verdict on the
; glass, so asking on the press would consume a verdict nothing was going to
; draw. The press greys and presses against the block the last PAINT read,
; which is also the block the pixels it is drawing over came from - so a
; pressed control cannot come back looking different for a reason the user
; never saw.
; -----------------------------------------------------------------------------
rp_click:
    call rp_bank                ; DX = the relative y; CX is already the x
    mov bx, rp_r_sbox
    call os88ui_bhit
    jnc .focus
    call rp_hit                 ; everything else ARMS (13.7)...
    cmp byte [rp_edit], 0       ; ...and ENDS THE EDIT, which it ends by
    je .arm                     ; CANCELLING: a half-typed number is not an
    mov word [rp_edit], 0       ; answer, and applying one because the user
    push ax                     ; pressed Mount is the kind of surprise a
    call rp_size_draw           ; caret is supposed to prevent.
    pop ax                      ;
                                ; **IT REDRAWS, and that is not tidiness.**
                                ; Ending the edit changes what the field is
                                ; supposed to say - from `512_` back to the
                                ; size that is really set - and this click
                                ; may go on to draw nothing at all: dead pane
                                ; space, or a `+` the clamp refuses
                                ; (rp_size_fast condition 1). Left undrawn,
                                ; that is a half-typed number stranded on the
                                ; glass for the rest of the session, in the
                                ; two places nobody clicks twice
.arm:
    call os88ui_arm
    jmp rp_setdown
.focus:
    xor ax, ax                  ; the caret takes the PRESS, so nothing is armed
    call os88ui_arm             ; and nothing is held - said rather than
    call rp_setdown             ; assumed, because a stale arm here would fire
                                ; the previous control at the next release
    call rp_size_ok
    jc .out                     ; greyed: the volume is up, so there is nothing
    cmp byte [rp_edit], 0       ; to type into
    jne .out                    ; already editing: the click changes nothing
    mov word [rp_edit], 1       ; the caret, and nothing typed yet (rp_elen)
    jmp rp_size_draw            ; ONE run, exactly as a step costs one
.out:
    ret

; -----------------------------------------------------------------------------
; rp_act - do what the control the user released on does
; in:  AX = the control id, the state block FRESH
; out: CF = 0 the pane was repainted; CF = 1 it was not, so the caller owes
;      this one control a redraw. Clobbers everything.
;
; EVERY BRANCH THAT CHANGES ANYTHING ENDS IN A FULL PANE REPAINT, and that is
; affordable here in a way it would not be in a window: the pane is 221x132
; and a release is a gesture the user is already waiting on. What it buys is
; that the page has exactly ONE drawing path for its content, so a control's
; state on screen can never disagree with the resident's.
;
; The CF answer is what keeps that from also being the pressed look's problem:
; a refusal draws nothing, and a control that drew itself down has to be put
; back by somebody. A greyed control explains itself (47 rule 5), so a refused
; release says nothing new. A dialog that went UP answers CF = 0 like a
; repaint: it covers the pane, and whatever takes it down repaints it.
; -----------------------------------------------------------------------------
rp_act:
    cmp ax, 2                   ; 1..2 are the two Store radios
    ja .btn
    call rp_loc_ok
    jc rp_a_no
    xchg bx, ax
    dec bx                      ; BL = the RDL_* it selects
    mov al, RSV_LOC
    jmp short rp_a_svc
.btn:
    shr ax, 1
    xchg bx, ax                 ; BX = the row's offset off RP_BLBL...
    call [RP_BLBL+bx+2]         ; ...whose predicate goes first
    jc rp_a_no
    shr bx, 1                   ; ...and rp_acts is half its stride
    jmp [RP_ACTS+bx]

rp_a_minus:
    mov bx, [rp_st+RDS_KB]
    sub bx, RD_STEPKB
    jmp short rp_a_size
rp_a_plus:
    mov bx, [rp_st+RDS_KB]
    add bx, RD_STEPKB
rp_a_size:                      ; BX and not AX: AL is the VERB on every RSV_*
    mov al, RSV_SIZE            ; call, so an argument that needs a whole word
    call rp_svc                 ; rides elsewhere. The RESIDENT clamps it,
                                ; being the one that owns what a legal size is
    call rp_size_fast           ; ...and ONE call redraws the field, unless
    jc rp_a_rep                 ; something else on the pane moved with it
rp_a_no:
    stc                         ; the field is not the button, so either way
    ret                         ; the caller still owes the `-` or `+` its way
                                ; back up
rp_a_mount:                     ; Mount, or Unmount when a volume is up -
    mov al, RSV_MOUNT           ; RDS_MNT being 0 or 1
    add al, [rp_st+RDS_MNT]
    jmp short rp_a_svc
%if RSV_UNMOUNT != RSV_MOUNT + 1
  %error "rp_a_mount adds RDS_MNT to RSV_MOUNT"
%endif
rp_a_pres:
    mov al, RSV_PRESERVE
    xor si, si                  ; 0 = the remembered image, in its own folder
    jmp short rp_a_svc
rp_a_presa:
    call rp_a_up
    mov si, rp_st + RDS_NAME    ; the remembered name as the default, which is
    cmp byte [si], 0            ; what a Save As should open holding
    jne .ask
    mov si, rp_s_defnam
.ask:
    mov bl, FDLG_SAVE
    jmp short rp_a_dlg
rp_a_load:
    call rp_a_up
    xor si, si
    mov bl, FDLG_OPEN
rp_a_dlg:
    mov al, RSV_DLG
    call rp_svc                 ; the dialog is UP, or it was refused - and
    clc                         ; either way the button is already upright
    ret                         ; (rp_a_up), so this owes the caller NOTHING.
                                ; The dialog is modal and the answer arrives
                                ; at rp_dlgdone, which repaints; a Cancel
                                ; takes it down by PUTTING BACK WHAT WAS
                                ; UNDER IT - so what was under it has to be
                                ; the button UP. Drawn after the dialog it
                                ; would land OVER the window just put up, the
                                ; page being unclipped (SPEC.md 31.9); not
                                ; drawn at all, Esc put the PRESSED picture
                                ; back for the rest of the session. And a
                                ; refusal leaves the pane on the glass with
                                ; the button already up, so returning CF = 1
                                ; would draw it a second time.
                                ;
                                ; (It was `stc` here once, and the caller
                                ; redrew whatever SI held - which this path
                                ; had just pointed at a NAME, so Preserve As
                                ; "redrew" a rect read out of 'RAMDISK.RAM'
                                ; through a predicate read past the table,
                                ; and Load redrew control 0, which is
                                ; nothing. Neither put anything on the glass
                                ; by luck rather than by design.)

; rp_a_up - draw the released control UPRIGHT, before a dialog goes up over it
; in:  BX = its RP_ACTS index, which is its id / 4 ([rp_down] is already 0)
; out: nothing (clobbers everything)
rp_a_up:
    shl bx, 1
    shl bx, 1
    jmp rp_btn1

rp_a_clear:
    mov al, RSV_CLEAR
rp_a_svc:
    call rp_svc
rp_a_rep:
    call rp_repaint
    clc
    ret

; -----------------------------------------------------------------------------
; rp_size_fast - a size change costs the size FIELD, and nothing else
; in:  the resident has just taken (and CLAMPED) a new size
; out: CF = 0 the pane is right; CF = 1 = the caller owes a full repaint
; clobbers: everything
;
; **ONE `OSAPI_FONT_RUN` AGAINST A WHOLE-PANE REPAINT**, which on this page is
; a fill, five buttons, two radio glyphs and about two dozen strings - some
; forty drawing calls at PERFORMANCE.md Part 2's ~756us of fixed cost each, to
; move one digit. It is also what the press LOOKS like: a full repaint blanks
; the pane and rebuilds it, which is PERFORMANCE.md Part 1's double-draw flash
; with the whole page as the flash, twice a second while somebody holds the
; size down.
;
; The run is opaque - background and glyphs in one pass (SPEC.md 6.1) - so the
; field is never momentarily blank, and it cannot produce SPEC.md 11.3's
; granularity failure. It covers exactly the four cells inside the value box:
; the frame's own pixels are a row above, a row below and a column each side,
; so nothing redraws the box.
;
; FOUR CONDITIONS, and every one of them is a way the fast path would be a LIE
; about some other part of the pane:
;
;   1. the size ACTUALLY MOVED. `+` at the ceiling and `-` at the floor clamp
;      to where they were, and the right answer there is to draw nothing at
;      all - §22.2's "a click on the row already selected draws nothing".
;   2. the control is LIVE. rp_size_ok is the same predicate the paint greys
;      with, so a state this path cannot draw correctly is a state it declines.
;   3. no error line is ON THE GLASS. rp_state read-and-clears RDS_ERR at the
;      resident (SPEC.md 59.5), so a verdict drawn by the last paint has
;      ALREADY been consumed - and a partial redraw would leave it on screen
;      with nothing left that knows to take it away. [rp_eshown] is the record
;      of what was drawn, which is the only thing that can answer that.
;   4. no NEW verdict arrived, for the same reason in the other direction.
;
; 3 and 4 are one test: RDERR_NONE is 0, so the OR of the two is 0 exactly
; when neither is a verdict.
;
; Nothing else on the pane reads the size: the caption quotes it only in the
; mounted branch, and the size cannot be changed while the volume is up
; (condition 2 is that same predicate). That is what makes the field the
; whole of the damage.
; -----------------------------------------------------------------------------
rp_size_fast:
%ifdef RPSLOW                   ; `make RPSLOW=1` builds the whole-pane repaint
    stc                         ; back, which is the reference half of "the
    ret                         ; picture is the same, only the number of times
%endif                          ; it was drawn changed" - a claim a screenshot
                                ; of ONE build cannot check (the REDRAWFULL
                                ; precedent, SPEC.md 12.9)
    call rp_state               ; the CLAMPED size, not the one we asked for:
                                ; the resident owns what is legal
    mov al, [rp_st+RDS_ERR]
    or al, [rp_eshown]
    stc                         ; (STC leaves ZF alone)
    jnz .out                    ; (3) and (4)
    call rp_size_ok
    jc .out                     ; (2)
    mov ax, [rp_st+RDS_KB]
    cmp ax, [rp_kbshown]
    jne rp_size_draw            ; ...which answers CF = 0 itself (rp_kbmark)
.out:                           ; (1) - drawing nothing IS the answer, and the
    ret                         ; equal compare has cleared CF to say so

; -----------------------------------------------------------------------------
; rp_size_draw - the value box's RP_SW characters, in one opaque run
; in:  [rp_st+RDS_KB]
; out: nothing; [rp_kbshown] follows the glass; CF = 0 (clobbers everything)
;
; The field is a FIXED RP_SW characters and rp_size_text right-aligns into it,
; so the leading spaces are what erase a digit the number just lost - 224 to 8
; comes back as `   8` and not as `   8` over a stale `4`. That is the whole
; reason the run's width is the field's and not the number's.
;
; BLACK and not [rp_ink]: live by construction - every path here has already
; asked rp_size_ok (rp_size_fast's condition 2, and rp_click's .focus, which
; is the only way a caret and so a key arrives).
; -----------------------------------------------------------------------------
rp_size_draw:
    call rp_size_text
    mov si, rp_line
    mov cx, RP_STX
    mov dx, RP_SY
    mov al, CBLACK
    call rp_stra
                                ; ...and falls into the record of it

; -----------------------------------------------------------------------------
; rp_kbmark - record what the field now shows
; in:  [rp_edit], [rp_st+RDS_KB]
; out: nothing (AX clobbered), CF = 0 - rp_size_fast returns through here
;
; **A FIELD BEING TYPED INTO SHOWS NO VALUE AT ALL, so it may not claim one.**
; [rp_kbshown] is the record of the GLASS (SPEC.md 62.9.11.1) and rp_size_fast
; reads "the size did not move" out of it as a licence to draw NOTHING - so a
; typed field recorded as its underlying size is a step that gets clamped back
; to where it was and then leaves `512_` on screen for the rest of the session.
; RP_KBNONE is unreachable as a size (RD_MAXKB is 16,384), so it cannot collide
; with one.
; -----------------------------------------------------------------------------
RP_KBNONE   equ 0xFFFF

rp_kbmark:
    mov ax, RP_KBNONE
    cmp byte [rp_edit], 0       ; ...a compare with 0, so CF = 0 from here on
    jne .put
    mov ax, [rp_st+RDS_KB]
.put:
    mov [rp_kbshown], ax
    ret

; -----------------------------------------------------------------------------
; rp_size_text - rp_line = what the size field should read
; in:  [rp_edit], [rp_ebuf], [rp_elen], [rp_st+RDS_KB]
; out: rp_line, exactly RP_SW characters and a NUL (clobbers everything)
;
; TWO STATES, and the alignment says which: a VALUE is right-aligned, and
; something being TYPED is left-aligned with a caret after it. That is not a
; decoration - it is the only thing on a five-cell field that can distinguish
; "the machine is telling you 64" from "you have typed 6 so far", and without
; it a half-typed number reads as a smaller one that has already been applied.
;
; Padded to the full width either way - the spaces go down FIRST and the text
; over them - so the run that draws it erases what the last one left (SPEC.md
; 62.9.11.1).
; -----------------------------------------------------------------------------
rp_size_text:
    mov di, rp_line
    mov cx, RP_SW
    mov al, ' '
.pad:
    mov [di], al
    inc di
    loop .pad
    mov [di], cl                ; the NUL, CX having run out
    cmp byte [rp_edit], 0
    jne .typed
    mov ax, [rp_st+RDS_KB]      ; a value: right-aligned in the whole field,
    jmp rp_digits               ; built backwards from its end
.typed:
    mov si, rp_elen             ; the count, and the buffer straight after it
    lodsb
    mov cl, al
    mov di, rp_line
    jcxz .caret
.copy:
    lodsb
    mov [di], al
    inc di
    loop .copy
.caret:
    mov byte [di], '_'          ; the caret is a character, because a field
    ret                         ; this small has no room for one beside the
                                ; text and an XOR block would have to be
                                ; erased by something that knows where it was

; -----------------------------------------------------------------------------
; rp_key - RDT_KEY: a keystroke, while the size box has the caret
; in:  BL = ASCII, BH = the scan code; the gfx lock is HELD
; out: CF = 0 we took it; CF = 1 we did not, and the panel drops it. AX is
;      kept here, rd_page_key banking every other register
;
; **NOTHING IS TAKEN UNLESS THE FIELD HAS FOCUS**, which is what keeps this
; page from swallowing keys that belong to nobody: a Control Panel is not a
; text window, and a page that claimed every keystroke would make the panel
; feel like one.
;
; Enter and Tab COMMIT, Escape CANCELS, Backspace deletes and a digit appends.
; The commit hands the typed number to the resident, which CLAMPS it - so a
; typed 9999 on a 640KB machine comes back as what that machine will fund,
; and the field shows what was actually taken rather than what was asked for.
; That is the same clamp `+` goes through, and it is the reason the commit
; repaints from the state instead of from the buffer.
;
; **RDT_KEY CARRIES NO PANE ORIGIN** - the panel's W_ONKEY has a key and
; nothing else - so every draw from here uses the [rp_px]/[rp_py] the last
; PAINT banked. That is sound rather than lucky: a key can only arrive after
; the page has been painted (the caret is put up by a click on a painted box),
; and the panel is not resizable, so the pane cannot move under a caret.
; -----------------------------------------------------------------------------
RP_K_BS     equ 8
RP_K_TAB    equ 9
RP_K_CR     equ 13
RP_K_ESC    equ 27

rp_key:
    cmp byte [rp_edit], 0
    je .no                      ; no caret: not ours
    push ax
    xchg ax, bx                 ; AL = the key
    mov si, rp_elen             ; SI = the count, the buffer one byte on
    cmp al, RP_K_ESC
    je .cancel
    cmp al, RP_K_CR
    je .commit
    cmp al, RP_K_TAB
    je .commit
    cmp al, RP_K_BS
    je .back
    cmp al, '0'                 ; a key the field cannot use is still OURS -
    jb .took                    ; it landed in a text box, and passing it back
    cmp al, '9'                 ; would only offer it to nobody
    ja .took
    mov bl, [si]                ; a digit: append, if there is room
    cmp bl, RP_SW - 1           ; ...one short of the field, because the caret
    jae .took                   ; occupies a cell too
    mov bh, 0
    mov [bx+si+1], al           ; rp_ebuf follows rp_elen
    inc byte [si]
    jmp short .draw
.back:
    cmp byte [si], 0
    je .took
    dec byte [si]
    jmp short .draw
.cancel:
    mov byte [si], 0            ; nothing typed, and so a commit of nothing,
.commit:                        ; which is exactly a cancel
    mov byte [rp_edit], 0
    xor ax, ax                  ; AX = the digits typed
    mov cl, [si]
    mov ch, 0
    jcxz .draw
    inc si                      ; rp_ebuf
    mov bx, 10
.digit:
    mul bx                      ; AX * 10, DX spent (rd_page_key banks it)
    xchg dx, ax
    lodsb                       ; a digit, and only digits are ever stored
    sub al, '0'
    cbw
    add ax, dx
    loop .digit
    or ax, ax
    jz .draw                    ; an empty commit is a cancel, not a 0KB store
    xchg bx, ax
    mov al, RSV_SIZE
    call rp_svc
    call rp_state               ; ...and take the CLAMPED answer back
.draw:
    call rp_size_draw
.took:
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; rp_dlgdone - RDT_DLGDONE: the user picked a name (SPEC.md 51.10)
; in:  AH = the mode it ran in, ES:DI = the chosen NUL name in KERNEL_SEG,
;      DX:CX = its size. The gfx lock is held and the dialog is already down
; out: nothing; CX, DX and DI kept - rd_dlg_land banks only AX, BX, SI and BP
;
; The name is staged into OUR segment first, because everything below hands it
; to the resident as a pointer in this image and ES stops being KERNEL_SEG the
; moment rd_svc_page runs.
;
; A SAVE NAME GETS `.RAM` PUT ON IT if the user typed no extension, which is
; the one piece of policy in this file: an image with no extension is a file
; nobody can tell from a document.
; -----------------------------------------------------------------------------
rp_dlgdone:
    push cx
    push dx
    push di
    mov si, rp_name             ; ES:DI -> rp_name, at most 12 characters,
    mov cx, 12                  ; noting on the way whether there is a '.'
    xor bx, bx
.stage:
    mov al, [es:di]
    mov [si], al
    or al, al
    jz .staged
    cmp al, '.'
    jne .nodot
    inc bx
.nodot:
    inc di
    inc si
    loop .stage
    mov [si], cl                ; ...and the NUL after a full twelve
.staged:                        ; SI = the NUL
    mov al, RSV_LOAD            ; AH is still the mode
    cmp ah, FDLG_SAVE
    jne .go
    or bx, bx
    jnz .save                   ; the user named an extension: theirs wins
    cmp cl, 12                  ; CX = 12 less the length
    je .save                    ; an empty name is the dialog's problem, and
                                ; the file slot's FERR_NAME is what says so
    mov di, si
    cmp cl, 12 - 8
    jae .add
    mov di, rp_name + 8         ; a stem past 8 has no room for an extension
.add:                           ; inside an 8.3 name
    mov si, rp_s_dotram
    call rp_app
    mov [di], al                ; ...and its NUL, rp_app's AL
.save:
    mov al, RSV_PRESERVE
.go:
    mov si, rp_name
    call rp_svc
    call rp_repaint
    pop di
    pop dx
    pop cx
    ret

; =============================================================================
; CAPTIONS
; =============================================================================

; rp_app - DS:SI -> DS:DI without the NUL; DI advances, SI is spent, AL = 0
rp_app:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short rp_app
.out:
    ret

; rp_numapp - AX = a value, appended at DI with no padding; DI advances
; (clobbers AX, BX, DX, SI)
rp_numapp:
    push di
    mov di, rp_tmp + 6
    call rp_digits
    mov si, di
    pop di
    jmp short rp_app

; rp_digits - AX = a value, written in decimal BACKWARDS so that it ends just
; before DI; out DI = its first digit (clobbers AX, BX, DX)
rp_digits:
    mov bx, 10
.digit:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    or ax, ax
    jnz .digit
    ret
