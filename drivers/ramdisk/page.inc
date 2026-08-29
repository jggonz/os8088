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
rp_r_sbox:   dw RP_SBX, RP_SY-3, RP_SBX+RP_SBW-1, RP_SY-3+RP_FH-1
rp_r_minus:  dw RP_MX, RP_SY-2, RP_MX+RP_MW-1, RP_SY-2+RP_FH-1
rp_r_plus:   dw RP_PX, RP_SY-2, RP_PX+RP_MW-1, RP_SY-2+RP_FH-1
rp_r_mount:  dw RP_LX, RP_B0Y,  RP_LX+63,      RP_B0Y+RP_BH-1
rp_r_pres:   dw 72,    RP_B0Y,  72+71,         RP_B0Y+RP_BH-1
rp_r_load:   dw 152,   RP_B0Y,  152+63,        RP_B0Y+RP_BH-1
rp_r_presa:  dw RP_LX, RP_B1Y,  RP_LX+95,      RP_B1Y+RP_BH-1
rp_r_clear:  dw 106,   RP_B1Y,  106+63,        RP_B1Y+RP_BH-1

; ...and what each BUTTON says and asks, in rp_r_minus's own order, so one
; table drives the full paint, the per-control repaint and nothing else. The
; rects stay the id space (a rect pointer is what the hit test answers and
; what every painter takes), and this is indexed off it by arithmetic rather
; than by a second numbering that could drift: (rect - rp_r_minus) / 8 * 4.
;
; A predicate of 0 means ALWAYS LIVE, which is a claim and not an omission -
; Mount and Load are both SPEC.md 47 rule 3's case, where the only honest test
; is doing the thing.
rp_blbl:
    dw rp_s_minus, rp_size_ok
    dw rp_s_plus,  rp_size_ok
    dw rp_s_mount, 0
    dw rp_s_pres,  rp_pres_ok
    dw rp_s_load,  0
    dw rp_s_presa, rp_presa_ok
    dw rp_s_clear, rp_clear_ok
RP_NBTN     equ 7

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
rp_s_as:     db 'Mounted ', 0     ; 'Mounted D: 224K of 224K' is 23 of the 27
rp_s_free:   db 'K of ', 0       ; characters this pane holds, and the worst
                                 ; case is what has to fit: `Mounted as` and a
                                 ; spelt-out `free` made it 29, which font_str
                                 ; drew straight through the window's border
                                 ; and out the other side - the failure this
                                 ; file's own header names
rp_s_notmnt: db 'Not mounted', 0
rp_s_dotram: db '.RAM', 0
rp_s_defnam: db 'RAMDISK.RAM', 0

; What went wrong, indexed by RDERR_*. RDERR_NONE's row is the empty string,
; so the caption line simply is not drawn rather than being special-cased -
; and RDS_ERR is READ-AND-CLEAR at the resident, so a verdict cannot outlive
; its cause and be reported again on the next unrelated paint (SPEC.md 59.5).
rp_errtab:
    dw rp_e_none
    dw rp_e_mem
    dw rp_e_vol
    dw rp_e_mnt
    dw rp_e_file
    dw rp_e_fmt
    dw rp_e_big
    dw rp_e_noimg
rp_e_none:   db 0
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
; out: nothing
; -----------------------------------------------------------------------------
rp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [rp_px], di
    mov [rp_py], dx
    call rp_state               ; the live state, on EVERY paint: the panel
                                ; stays clickable under a dialog's completion
                                ; and a mount can happen between two paints

    clc
    call rp_pen
    mov si, rp_s_page
    mov cx, RP_LX
    mov dx, RP_HY
    call rp_str

    ; --- the size row --------------------------------------------------------
    call rp_size_ok             ; ONE predicate, shared by the greying, the
    call rp_pen                 ; click refusal and the explanation (47 rule 2)
    mov si, rp_s_size
    mov cx, RP_LX
    mov dx, RP_SY
    call rp_str

    call rp_size_ok
    call rp_pen
    mov ax, RP_SBX              ; the value box
    mov bx, RP_SY - 3
    mov cx, RP_SBX + RP_SBW - 1
    mov dx, RP_SY - 3 + RP_FH - 1
    call rp_frame

    call rp_size_text           ; rp_line = the value, or what is being typed
    call rp_size_ok
    call rp_pen
    mov si, rp_line
    mov cx, RP_STX
    mov dx, RP_SY
    call rp_str
    mov si, rp_s_k
    mov cx, RP_SKX
    call rp_str

    ; --- the store row -------------------------------------------------------
    call rp_loc_ok
    call rp_pen
    mov si, rp_s_store
    mov cx, RP_LX
    mov dx, RP_LY
    call rp_str

    mov al, RDL_CONV
    mov bx, RP_RX0
    mov si, rp_s_conv
    mov di, RP_RTX0
    call rp_radio
    mov al, RDL_XMS
    mov bx, RP_RX1
    mov si, rp_s_xms
    mov di, RP_RTX1
    call rp_radio

    ; --- the buttons, ALL SEVEN out of one table -----------------------------
    ; The two size cells are in it too, which is why they are no longer drawn
    ; up in the size row: `-` and `+` are buttons and want the pressed look
    ; exactly as the other five do, and a control drawn from a second place
    ; is a control whose down state has to be remembered twice.
    mov bx, rp_r_minus
    mov cx, RP_NBTN
.btns:
    call rp_btn1
    add bx, 8
    loop .btns

    ; --- the captions --------------------------------------------------------
    clc
    call rp_pen
    call rp_cap_mount
    mov si, rp_line
    mov cx, RP_LX
    mov dx, RP_CAP0
    call rp_str

    call rp_cap_boot
    mov si, rp_line
    mov cx, RP_LX
    mov dx, RP_CAP1
    call rp_str

    mov al, [rp_st+RDS_ERR]
    mov [rp_eshown], al         ; WHAT THE GLASS IS SHOWING, and the record is
                                ; kept here because this is the only routine
                                ; that puts it there - menu_bcell's discipline
                                ; (SPEC.md 12.9) at the scale of one line
    cmp al, RDERR_NERR
    jae .noerr
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [bx+rp_errtab]
    cmp byte [si], 0
    je .noerr
    mov cx, RP_LX
    mov dx, RP_CAP2
    call rp_str
.noerr:
    call rp_kbmark              ; ...and the size the field was drawn with
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_radio - one Store radio and its label
; in:  AL = the RDL_* it selects, BX = the glyph's pane x, DI = the label's,
;      SI = the label
; out: nothing
; -----------------------------------------------------------------------------
rp_radio:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = which one this is, across the tests
    mov al, OS88UI_GRADIO
    cmp ah, [rp_st+RDS_LOC]
    jne .off
    or al, OS88UI_GON
.off:
    mov dl, ah                  ; ...and PRESSED if it is the one being held
    inc dl                      ; (SPEC.md 13.8). AH is still the INDEX here
    xor dh, dh                  ; and is about to become the disabled flag, so
    cmp dx, [rp_down]           ; the test has to happen NOW - and a radio is
    jne .nodn                   ; 1-BASED in the one id space [rp_down] carries,
    or al, OS88UI_GDOWN         ; so 0 can mean "none" and a button's own id
.nodn:                          ; can be its RECT POINTER, which no radio can
                                ; collide with
    push ax                     ; ...and the DISABLED flag last, because CF is
    call rp_loc_ok              ; what carries it out of the predicate and
    pop ax                      ; every compare above would eat it (POP and MOV
    mov ah, 0                   ; leave the flags alone, which is what makes
    jnc .live                   ; this ordering work at all)
    mov ah, 1
.live:
    mov cx, bx
    add cx, [rp_px]
    mov dx, RP_LY - 3
    add dx, [rp_py]
    call os88ui_glyph
    call rp_loc_ok
    call rp_pen
    mov cx, di
    mov dx, RP_LY
    call rp_str
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE PREDICATES (SPEC.md 47 rule 2)
;
; Each answers CF - 0 live, 1 disabled - and each is called by BOTH the paint
; and the click, so a control that looks unavailable cannot act and one that
; acts cannot look unavailable.
; =============================================================================

rp_size_ok:                     ; the store cannot be resized under a volume
    cmp byte [rp_st+RDS_MNT], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

rp_loc_ok:                      ; ...nor moved, and a machine with no store
    cmp byte [rp_st+RDS_MNT], 0 ; above 1MB has nowhere to move it to. A FACT
    jne .no                     ; the code can test (47 rule 3)
    cmp byte [rp_st+RDS_XMS], 0
    je .no
    clc
    ret
.no:
    stc
    ret

rp_pres_ok:                     ; Preserve needs something to write AND
    cmp byte [rp_st+RDS_MNT], 0 ; somewhere already chosen to write it
    je .no
    cmp byte [rp_st+RDS_BOOT], 0
    je .no
    clc
    ret
.no:
    stc
    ret

rp_presa_ok:                    ; ...Preserve As only needs the volume
    cmp byte [rp_st+RDS_MNT], 0
    je .no
    clc
    ret
.no:
    stc
    ret

rp_clear_ok:                    ; ...and Clear, an association to drop
    cmp byte [rp_st+RDS_BOOT], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; DRAWING HELPERS - every one of them takes PANE-RELATIVE coordinates
;
; The layout above reads pane-relative and every gfx primitive takes SCREEN
; coordinates (SPEC.md 5). ONE place does the addition, so the click test and
; the paint cannot disagree about which of the two a number is.
; =============================================================================

; rp_pen - OSAPI_GFX_PEN, and the INK a run must letter in, from one CF
; in:  CF = SPEC.md 47's argument (0 live, 1 disabled)
; out: the pen set and [rp_ink] banked; every register preserved
;
; A DRIVER CANNOT READ [gfx_color] BACK any more than a package can (SPEC.md
; 52.12): there is no OSAPI_GET_COLOR, so the ink has to be decided at the
; branch that decides the flag. Every one of this page's seven pen sites goes
; through here, so rp_str never has to ask. The FLAG stays the pen's, which is
; what leaves SPEC.md 6.1.12 to checkerboard a disabled caption for free.
rp_pen:
    push ax
    mov al, CBLACK
    jnc .set
    mov al, CDGRAY
.set:
    mov [rp_ink], al
    pop ax
    call OSAPI_GFX_PEN
    ret

; rp_str - SI = a string, CX/DX = a pane-relative pen
rp_str:
    push ax
    push cx
    push dx
    add cx, [rp_px]
    add dx, [rp_py]
    mov al, [rp_ink]            ; AL = ink, AH = the pane's own ground
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; rp_frame - AX/BX/CX/DX = a pane-relative rect
rp_frame:
    push ax
    push bx
    push cx
    push dx
    add ax, [rp_px]
    add cx, [rp_px]
    add bx, [rp_py]
    add dx, [rp_py]
    call OSAPI_GFX_FRAME
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rp_rect_abs - BX = a pane-relative rect -> rp_rect, in screen coordinates
rp_rect_abs:
    push ax
    mov ax, [bx+0]
    add ax, [rp_px]
    mov [rp_rect+0], ax
    mov ax, [bx+2]
    add ax, [rp_py]
    mov [rp_rect+2], ax
    mov ax, [bx+4]
    add ax, [rp_px]
    mov [rp_rect+4], ax
    mov ax, [bx+6]
    add ax, [rp_py]
    mov [rp_rect+6], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_btn - one button, greyed from the caller's CF
; in:  BX = its pane-relative rect, SI = its label, CF = 1 disabled
; out: nothing
;
; OS88UI_FILL is set because a page is drawn over a pane the kernel white-
; filled once and then over ITSELF on every later paint - so without it a
; second draw's checkerboard caption lands on top of the first draw's solid
; one and a greyed label comes out pixel-identical to a live one, which reads
; as SPEC.md 47 rule 1 being broken when the mechanism is fine.
; -----------------------------------------------------------------------------
rp_btn:
    push ax
    push bx
    push cx
    push dx
    push di
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
    call rp_rect_abs
    mov bx, rp_rect
    call os88ui_btn
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_btn1 - one button, out of rp_blbl
; in:  BX = its rect (which is its id)
; out: nothing (every register preserved, so a caller may loop on BX)
;
; The label and the predicate come from the table rather than from the call
; site, which is what lets the full paint and the pressed-state redraw be the
; SAME drawing: a control repainted on a press cannot come back looking
; different from the one the pane was drawn with, because there is one body.
; -----------------------------------------------------------------------------
rp_btn1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, bx
    sub ax, rp_r_minus          ; the row: /8 for the rect, *4 for the entry,
    shr ax, 1                   ; which is ONE shift and not two - two is /4,
    mov di, ax                  ; so `+` read its LABEL out of the predicate
                                ; column and then CALLED the string beside it
    mov si, [rp_blbl+di]
    mov ax, [rp_blbl+di+2]
    cmp bx, rp_r_mount          ; ...and Mount's label says what it will DO,
    jne .lbl                    ; which is the one thing a static table cannot
    cmp byte [rp_st+RDS_MNT], 0
    je .lbl
    mov si, rp_s_unmnt
.lbl:
    or ax, ax                   ; 0 = always live (47 rule 3)
    jz .live
    call ax                     ; ...otherwise the predicate answers in CF
    jmp short .go
.live:
    clc
.go:
    call rp_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_ctlpaint - redraw ONE control, in whatever state it is now in
; in:  BX = a control id (a rect pointer, 1 or 2 for a radio, 0 = nothing)
; out: nothing (every register preserved)
;
; **NEVER rp_repaint.** A press has to say so within the frame it arrives in,
; and blanking a 221x132 pane and rebuilding forty drawing calls of it to
; invert one button is PERFORMANCE.md Part 1's double-draw flash with the
; whole page as the flash - twice for every click, and four times if the
; pointer is dragged off the control and back.
; -----------------------------------------------------------------------------
rp_ctlpaint:
    or bx, bx
    jz .out
    push ax
    push si
    push di
    cmp bx, 2                   ; 1..2 is a radio and anything else is a rect
    ja .btn
    mov al, bl
    dec al
    mov si, rp_s_conv
    mov di, RP_RTX0
    mov bx, RP_RX0
    cmp al, RDL_CONV
    je .rad
    mov si, rp_s_xms
    mov di, RP_RTX1
    mov bx, RP_RX1
.rad:
    call rp_radio
    jmp short .done
.btn:
    call rp_btn1
.done:
    pop di
    pop si
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; rp_setdown - AX = the control that is now held (0 = none)
; out: nothing (every register preserved)
;
; Draws ONLY IF THAT CHANGED, and draws the two controls that changed rather
; than the pane: the one going up and the one coming down. That is what makes
; the tracking edge (SPEC.md 13.8.1) affordable - a drag delivers this at the
; mouse's rate and the common answer is "the same one", which costs a compare.
; -----------------------------------------------------------------------------
rp_setdown:
    push ax
    push bx
    cmp ax, [rp_down]
    je .out
    mov bx, [rp_down]           ; the one being let go
    mov [rp_down], ax
    call rp_ctlpaint
    mov bx, [rp_down]           ; ...and the one taking its place
    call rp_ctlpaint
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_hit - which control is the pane-relative point in?
; in:  CX = x, DX = y
; out: AX = the id (a rect pointer, 1 or 2 for a radio, 0 = none); every other
;      register preserved
;
; THE SIZE BOX IS NOT IN IT, and that is SPEC.md 13.6 rather than an oversight:
; taking the caret is a safe prefix action, so that one control keeps the
; PRESS - and a hit test that could arm it would be a second opinion about
; which of the two it is.
; -----------------------------------------------------------------------------
rp_hit:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, rp_r_minus          ; the seven are contiguous and eight bytes
    mov ax, RP_NBTN             ; each, so os88ui_bfind walks them as an array
    call os88ui_bfind
    or ax, ax
    jz .rad
    dec ax                      ; index -> the rect itself, which is the id
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, rp_r_minus
    jmp short .out
.rad:
    call rp_radio_hit
    jc .none
    xor ah, ah
    inc ax                      ; 1-based, so 0 stays "none"
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; CLICK
; =============================================================================

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
; **rp_state is NOT called here any more.** RDS_ERR is read-and-clear at the
; resident (SPEC.md 59.5) and only a path that REPAINTS can put the verdict on
; the glass, so asking on the press would consume a verdict nothing was going
; to draw. The press greys and presses against the block the last PAINT read,
; which is also the block the pixels it is drawing over came from - so a
; pressed control cannot come back looking different for a reason the user
; never saw.
; -----------------------------------------------------------------------------
rp_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [rp_px], di
    mov [rp_py], dx
    mov [rp_cx], cx
    mov [rp_cy], bx
    mov dx, bx                  ; DX arrived as the pane TOP and the hit tests
                                ; want the point: CX is already the relative x,
                                ; and the relative y is in BX
    mov bx, rp_r_sbox
    call os88ui_bhit
    jnc .focus
    call rp_edit_end            ; ANY OTHER CLICK ENDS THE EDIT, and it ends it
                                ; by CANCELLING: a half-typed number is not an
                                ; answer, and applying one because the user
                                ; pressed Mount is the kind of surprise a
                                ; caret is supposed to prevent
    call rp_hit                 ; ...and everything else ARMS (13.7)
    call os88ui_arm
    call rp_setdown             ; ...and says so, in one control's worth of
    jmp short .out              ; pixels

.focus:
    xor ax, ax                  ; the caret takes the PRESS, so nothing is armed
    call os88ui_arm             ; and nothing is held - said rather than
    call rp_setdown             ; assumed, because a stale arm here would fire
                                ; the previous control at the next release
    call rp_size_ok
    jc .out                     ; greyed: the volume is up, so there is nothing
    cmp byte [rp_edit], 0       ; to type into
    jne .out                    ; already editing: the click changes nothing
    mov byte [rp_edit], 1
    mov byte [rp_elen], 0
    call rp_size_draw           ; ONE run, exactly as a step costs one
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
; rp_onup - RDT_UP: the RELEASE, which is where this page acts
; in:  the same four registers RDT_CLICK takes
; out: nothing
;
; The gesture is press-and-release ON THE SAME CONTROL. Anything else - a
; slide off, a release on a different control - is a CANCEL and does nothing
; but put the pressed look back.
; -----------------------------------------------------------------------------
rp_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [rp_px], di
    mov [rp_py], dx
    mov [rp_cx], cx
    mov [rp_cy], bx
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov si, ax                  ; SI, because CX/DX are the point below and BX
                                ; is what rp_ctlpaint takes
    mov cx, [rp_cx]
    mov dx, [rp_cy]
    call rp_hit
    cmp ax, si
    jne .cancel
    call rp_state               ; the world, fresh - every predicate rp_act
                                ; asks reads it, and a mount may have happened
                                ; under the panel since the last paint
    mov word [rp_down], 0       ; NOT rp_setdown: rp_act's paths that change
                                ; anything repaint the pane and draw this
                                ; control upright with them, and drawing it
                                ; upright HERE first is the double-draw
    mov ax, si
    call rp_act
    jnc .out                    ; it repainted, so the pane already agrees
    mov bx, si                  ; ...it did not, so this one control owes its
    call rp_ctlpaint            ; own way back up
    jmp short .out
.cancel:
    xor ax, ax
    call rp_setdown
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
; rp_ondrag - RDT_DRAG: the pressed control follows the pointer (SPEC.md 13.8.1)
; in:  the same four registers again; arrives between the press and the release
; out: nothing
;
; PEEK, never take: the arm is the release's to spend, and a drag that
; consumed it would leave the release with nothing to fire.
; -----------------------------------------------------------------------------
rp_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [rp_px], di
    mov [rp_py], dx
    mov dx, bx                  ; ...the pane top out, the relative y in
    call os88ui_armed
    or ax, ax
    jz .out
    mov si, ax
    call rp_hit                 ; CX/DX = the point the pointer is at now
    cmp ax, si
    je .set
    xor ax, ax                  ; off it: nothing is down
.set:
    call rp_setdown             ; ...which draws only if that CHANGED
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
; rp_act - do what the control the user released on does
; in:  AX = the control id, the state block FRESH
; out: CF = 0 the pane was repainted; CF = 1 it was not, so the caller owes
;      this one control a redraw
;
; EVERY BRANCH THAT CHANGES ANYTHING ENDS IN A FULL PANE REPAINT, and that is
; affordable here in a way it would not be in a window: the pane is 221x132
; and a release is a gesture the user is already waiting on. What it buys is
; that the page has exactly ONE drawing path for its content, so a control's
; state on screen can never disagree with the resident's.
;
; The CF answer is what keeps that from also being the pressed look's problem:
; a refusal and a dialog draw nothing, and a control that drew itself down has
; to be put back by somebody.
; -----------------------------------------------------------------------------
rp_act:
    cmp ax, rp_r_minus
    je .minus
    cmp ax, rp_r_plus
    je .plus
    cmp ax, rp_r_mount
    je .mount
    cmp ax, rp_r_pres
    je .preserve
    cmp ax, rp_r_load
    je .load
    cmp ax, rp_r_presa
    je .presa
    cmp ax, rp_r_clear
    je .clear
    cmp ax, 2                   ; ...and 1..2 are the two Store radios
    ja .none
    dec ax
    jmp short .setloc
.none:
    stc
    ret

.minus:
    call rp_size_ok
    jc .no                      ; a greyed control explains itself (47 rule 5),
    mov ax, [rp_st+RDS_KB]      ; so a refused release says nothing new
    sub ax, RD_STEPKB
    jmp short .setsize
.plus:
    call rp_size_ok
    jc .no
    mov ax, [rp_st+RDS_KB]
    add ax, RD_STEPKB
.setsize:
    mov bx, ax                  ; BX and not AX: AL is the VERB on every RSV_*
    mov al, RSV_SIZE            ; call, so an argument that needs a whole word
    call rp_svc                 ; rides elsewhere. The RESIDENT clamps it,
                                ; being the one that owns what a legal size is
    call rp_size_fast           ; ...and ONE call redraws the field, unless
    jc .repaint                 ; something else on the pane moved with it
.no:
    stc                         ; the field is not the button, so either way
    ret                         ; the caller still owes the `-` or `+` its way
                                ; back up
.setloc:
    push ax
    call rp_loc_ok
    pop ax
    jc .no
    mov bl, al
    mov al, RSV_LOC
    call rp_svc
    jmp short .repaint
.mount:
    cmp byte [rp_st+RDS_MNT], 0
    jne .unmount
    mov al, RSV_MOUNT
    call rp_svc
    jmp short .repaint
.unmount:
    mov al, RSV_UNMOUNT
    call rp_svc
    jmp short .repaint
.preserve:
    call rp_pres_ok
    jc .no
    mov al, RSV_PRESERVE
    xor si, si                  ; 0 = the remembered image, in its own folder
    call rp_svc
    jmp short .repaint
.presa:
    call rp_presa_ok
    jc .no
    mov si, rp_st + RDS_NAME    ; the remembered name as the default, which is
    cmp byte [si], 0            ; what a Save As should open holding
    jne .askname
    mov si, rp_s_defnam
.askname:
    mov bl, FDLG_SAVE
    mov al, RSV_DLG
    call rp_svc
    stc
    ret                         ; the dialog is modal and the answer arrives
                                ; at rp_dlgdone: repainting under it would
                                ; draw the page over the window just put up -
                                ; but the BUTTON still has to come back up, or
                                ; it sits pressed for as long as the dialog is
                                ; on screen
.load:
    xor si, si
    mov bl, FDLG_OPEN
    mov al, RSV_DLG
    call rp_svc
    stc
    ret
.clear:
    call rp_clear_ok
    jc .no
    mov al, RSV_CLEAR
    call rp_svc
.repaint:
    call rp_repaint
    clc
    ret

; -----------------------------------------------------------------------------
; rp_size_fast - a size change costs the size FIELD, and nothing else
; in:  the resident has just taken (and CLAMPED) a new size
; out: CF = 0 the pane is right; CF = 1 = the caller owes a full repaint
; clobbers: flags
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
; Nothing else on the pane reads the size: rp_cap_mount quotes it only in the
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
    push ax
    push bx
    push cx
    push dx
    push si
    call rp_state               ; the CLAMPED size, not the one we asked for:
                                ; the resident owns what is legal
    cmp byte [rp_st+RDS_ERR], RDERR_NONE
    jne .slow                   ; (4)
    cmp byte [rp_eshown], RDERR_NONE
    jne .slow                   ; (3)
    call rp_size_ok
    jc .slow                    ; (2)
    mov ax, [rp_st+RDS_KB]
    cmp ax, [rp_kbshown]
    je .done                    ; (1) - and drawing nothing IS the answer
    call rp_size_draw
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.slow:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rp_size_draw - the value box's RP_SW characters, in one opaque run
; in:  [rp_st+RDS_KB]
; out: nothing; [rp_kbshown] follows the glass
;
; The field is a FIXED RP_SW characters and rp_num right-aligns into it, so the
; leading spaces are what erase a digit the number just lost - 224 to 8 comes
; back as `   8` and not as `   8` over a stale `4`. That is the whole reason
; the run's width is the field's and not the number's.
; -----------------------------------------------------------------------------
rp_size_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    call rp_size_text
    mov si, rp_line
    mov cx, RP_STX
    add cx, [rp_px]
    mov dx, RP_SY
    add dx, [rp_py]
    mov al, CBLACK              ; live by construction - every path here has
    mov ah, CWHITE              ; already asked rp_size_ok (rp_size_fast's
    call OSAPI_FONT_RUN         ; condition 2, and rp_click's .focus)
    call rp_kbmark
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_kbmark - record what the field now shows
; in:  [rp_edit], [rp_st+RDS_KB]
; out: nothing (AX clobbered)
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
    cmp byte [rp_edit], 0
    jne .put
    mov ax, [rp_st+RDS_KB]
.put:
    mov [rp_kbshown], ax
    ret

; -----------------------------------------------------------------------------
; rp_size_text - rp_line = what the size field should read
; in:  [rp_edit], [rp_ebuf], [rp_st+RDS_KB]
; out: rp_line, exactly RP_SW characters and a NUL
;
; TWO STATES, and the alignment says which: a VALUE is right-aligned, and
; something being TYPED is left-aligned with a caret after it. That is not a
; decoration - it is the only thing on a five-cell field that can distinguish
; "the machine is telling you 64" from "you have typed 6 so far", and without
; it a half-typed number reads as a smaller one that has already been applied.
;
; Padded to the full width either way, so the run that draws it erases what the
; last one left (SPEC.md 62.9.11.1).
; -----------------------------------------------------------------------------
rp_size_text:
    push ax
    push cx
    push si
    push di
    cmp byte [rp_edit], 0
    jne .typed
    mov ax, [rp_st+RDS_KB]
    mov cx, RP_SW
    call rp_num                 ; right-aligned in the whole field
    jmp short .out
.typed:
    mov si, rp_ebuf
    mov di, rp_line
    xor cx, cx
.copy:
    cmp cl, [rp_elen]
    jae .caret
    mov al, [si]
    mov [di], al
    inc si
    inc di
    inc cl
    jmp short .copy
.caret:
    mov byte [di], '_'          ; the caret is a character, because a field
    inc di                      ; this small has no room for one beside the
    inc cl                      ; text and an XOR block would have to be
.pad:                           ; erased by something that knows where it was
    cmp cl, RP_SW
    jae .end
    mov byte [di], ' '
    inc di
    inc cl
    jmp short .pad
.end:
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_key - RDT_KEY: a keystroke, while the size box has the caret
; in:  BL = ASCII, BH = the scan code; the gfx lock is HELD
; out: CF = 0 we took it; CF = 1 we did not, and the panel drops it
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
    push bx
    push cx
    push dx
    push si
    push di
    cmp bl, RP_K_ESC
    je .cancel
    cmp bl, RP_K_CR
    je .commit
    cmp bl, RP_K_TAB
    je .commit
    cmp bl, RP_K_BS
    je .back
    cmp bl, '0'
    jb .eat
    cmp bl, '9'
    ja .eat
    mov al, [rp_elen]           ; a digit: append, if there is room
    cmp al, RP_SW - 1           ; ...one short of the field, because the caret
    jae .eat                    ; occupies a cell too
    mov ah, 0
    mov si, ax
    mov [rp_ebuf+si], bl
    inc byte [rp_elen]
    call rp_size_draw
    jmp short .took
.back:
    cmp byte [rp_elen], 0
    je .eat
    dec byte [rp_elen]
    call rp_size_draw
    jmp short .took
.cancel:
    call rp_edit_end            ; ...which redraws the field itself
    jmp short .took
.commit:
    call rp_edit_value          ; AX = what was typed, 0 = nothing
    mov byte [rp_edit], 0
    or ax, ax
    jz .redraw                  ; an empty commit is a cancel, not a 0KB store
    mov bx, ax
    mov al, RSV_SIZE
    call rp_svc
    call rp_state               ; ...and take the CLAMPED answer back
.redraw:
    call rp_size_draw
.took:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.eat:
    pop di                      ; a key the field cannot use is still OURS - it
    pop si                      ; landed in a text box, and passing it back
    pop dx                      ; would only offer it to nobody
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; rp_edit_end - drop the caret without applying anything, and put the field back
; in:  nothing
; out: nothing (all registers preserved)
;
; **IT REDRAWS, and that is not tidiness.** Ending the edit changes what the
; field is supposed to say - from `512_` back to the size that is really set -
; and the branch that ended it may go on to draw nothing at all: a click on
; dead pane space, or a `+` the clamp refuses (rp_size_fast condition 1). Left
; to the caller, that is a half-typed number stranded on the glass for the rest
; of the session, and it strands it in the two places nobody clicks twice.
;
; A no-op when there was no caret, so rp_click may call it unconditionally.
; -----------------------------------------------------------------------------
rp_edit_end:
    cmp byte [rp_edit], 0
    je .out
    push ax
    mov byte [rp_edit], 0
    mov byte [rp_elen], 0
    call rp_size_draw
    pop ax
.out:
    ret

; rp_edit_value - AX = the digits typed, 0 = none
rp_edit_value:
    push bx
    push cx
    push si
    xor ax, ax
    xor si, si
    mov cl, [rp_elen]
    xor ch, ch
    jcxz .out
.digit:
    mov bx, 10
    mul bx                      ; AX * 10 - and DX is spent, which is why the
                                ; caller has banked it
    mov bl, [rp_ebuf+si]
    sub bl, '0'
    xor bh, bh
    add ax, bx
    inc si
    loop .digit
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rp_radio_hit - did the click land on one of the two Store radios?
; in:  CX/DX = the pane-relative click
; out: CF = 0 and AL = the RDL_* it selects; CF = 1 = neither
;
; The band is the GLYPH AND ITS LABEL, because a radio's label is part of the
; control (SPEC.md 47 rule 2) - and a 12px target on a 1bpp screen is a
; target most mice miss.
; -----------------------------------------------------------------------------
rp_radio_hit:
    cmp dx, RP_LY - 3
    jl .no
    cmp dx, RP_LY - 3 + OS88UI_GW
    jg .no
    cmp cx, RP_RX0
    jl .no
    cmp cx, RP_RX0 + RP_RW0
    jg .try1
    mov al, RDL_CONV
    clc
    ret
.try1:
    cmp cx, RP_RX1
    jl .no
    cmp cx, RP_RX1 + RP_RW1
    jg .no
    mov al, RDL_XMS
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; rp_repaint - the pane, white, then the page over it
; in:  [rp_px] / [rp_py]
; out: nothing
;
; A page draws itself UNCLIPPED and the kernel does not repaint after a click
; returns (SPEC.md 31.9), so this is the page's own job. The white fill comes
; first because font_str is transparent and every string above would otherwise
; land on top of the last one.
; -----------------------------------------------------------------------------
rp_repaint:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [rp_px]
    mov bx, [rp_py]
    mov cx, ax
    add cx, RP_PANEW - 1
    mov dx, bx
    add dx, RP_PANEH - 1
    call OSAPI_GFX_FILL
    mov di, [rp_px]
    mov dx, [rp_py]
    call rp_paint
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rp_dlgdone - RDT_DLGDONE: the user picked a name (SPEC.md 51.10)
; in:  AH = the mode it ran in, ES:DI = the chosen NUL name in KERNEL_SEG,
;      DX:CX = its size. The gfx lock is held and the dialog is already down
; out: nothing
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [rp_dlgmode], ah
    call rp_name_stage          ; ES:DI -> rp_name
    cmp byte [rp_dlgmode], FDLG_SAVE
    jne .load
    call rp_name_ext
    mov al, RSV_PRESERVE
    jmp short .go
.load:
    mov al, RSV_LOAD
.go:
    mov si, rp_name
    call rp_svc
    call rp_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rp_name_stage - ES:DI (KERNEL_SEG) -> rp_name, at most 12 characters
rp_name_stage:
    push ax
    push cx
    push si
    push di
    mov si, di
    mov di, rp_name
    mov cx, 12
.next:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; rp_name_ext - give rp_name a '.RAM' when it has no extension at all
rp_name_ext:
    push ax
    push cx
    push si
    push di
    mov si, rp_name
    xor cx, cx
.len:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '.'
    je .out                     ; the user named one: theirs wins
    inc si
    inc cx
    jmp short .len
.done:
    or cx, cx
    jz .out                     ; an empty name is the dialog's problem, and
                                ; the file slot's FERR_NAME is what says so
    cmp cx, 8
    jbe .add
    mov si, rp_name             ; a stem past 8 has no room for an extension
    add si, 8                   ; inside an 8.3 name
.add:
    mov di, si
    mov si, rp_s_dotram
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    jmp short .copy
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; CAPTIONS
; =============================================================================

; rp_cap_mount - 'Mounted D: 56K of 64K', or 'Not mounted'
rp_cap_mount:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, rp_line
    cmp byte [rp_st+RDS_MNT], 0
    je .not
    mov si, rp_s_as
    call rp_app
    mov al, [rp_st+RDS_VOL]
    add al, 'A'
    call rp_ch
    mov al, ':'
    call rp_ch
    mov al, ' '
    call rp_ch
    mov ax, [rp_st+RDS_FREEK]
    call rp_numapp
    mov si, rp_s_free
    call rp_app
    mov ax, [rp_st+RDS_KB]
    call rp_numapp
    mov al, 'K'
    call rp_ch
    jmp short .end
.not:
    mov si, rp_s_notmnt
    call rp_app
.end:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; rp_cap_boot - 'Boot image: BACKUP.RAM', or 'No boot image'
rp_cap_boot:
    push ax
    push si
    push di
    mov di, rp_line
    cmp byte [rp_st+RDS_BOOT], 0
    je .none
    mov si, rp_s_boot
    call rp_app
    mov si, rp_st + RDS_NAME
    call rp_app
    jmp short .end
.none:
    mov si, rp_s_noboot
    call rp_app
.end:
    mov byte [di], 0
    pop di
    pop si
    pop ax
    ret

; rp_ch - AL -> [DI], DI advances
rp_ch:
    mov [di], al
    inc di
    ret

; rp_app - DS:SI -> DS:DI without the NUL; DI advances, SI is spent
rp_app:
    push ax
.next:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc si
    inc di
    jmp short .next
.out:
    pop ax
    ret

; rp_num - AX = a value, CX = a field width -> rp_line, right-aligned and NUL
rp_num:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, rp_line
    add di, cx
    mov byte [di], 0
    dec di
    mov bx, 10
.digit:
    xor dx, dx
    div bx
    add dl, '0'
    mov [di], dl
    dec di
    dec cx
    or ax, ax
    jz .pad
    or cx, cx
    jnz .digit
    jmp short .out
.pad:
    or cx, cx
    jz .out
    mov byte [di], ' '
    dec di
    dec cx
    jmp short .pad
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rp_numapp - AX = a value, appended at DI with no padding; DI advances
rp_numapp:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, rp_tmp + 6
    mov byte [si], 0
    mov bx, 10
.digit:
    dec si
    xor dx, dx
    div bx
    add dl, '0'
    mov [si], dl
    or ax, ax
    jnz .digit
    call rp_app
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
