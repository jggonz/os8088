; =============================================================================
; os8088 - HDDTOOL.DRV / cppage.inc
;
; THE CONTROL PANEL'S `Drives` PAGE, and it is in the TOOL's image rather than
; the driver's (SPEC.md 52.13). What the resident keeps is the NAME - cp_list
; letters it and the kernel stages it at attach - five thunks, and the ability
; to bring this image in.
;
; **THE TRIGGER IS A PAINT, NOT A CLICK**, which is rdpage.inc's difference
; from hdtool.inc stated the other way round: the hard disk used to keep its
; page resident and load a second image when the user pressed Format, so the
; load happened on a button. The page IS that second image now, so the load
; happens at DSV_CPPAINT, the first time the page is selected - and the two
; windows that used to be the only reason to load are already here when it is.
;
; It cost the resident 2,893 bytes: 1,910 of page and the whole of os88ui.inc,
; which this image was already carrying for the tool windows. The page is
; therefore the cheapest thing in the driver to move and was the largest thing
; left in it.
;
; WHAT CROSSES, and each of them because it must:
;   - the device table, [hd_ndev] and [hd_sel] come over on hd_sync, which the
;     tool has always done before it ACTS - the panel stays clickable under a
;     tool window and a geometry can be retyped between one open and the next;
;   - the caption is an HDM_* CODE and never a pointer (hddabi.inc), because
;     font_str reads through DS;
;   - a selection, a typed geometry, a mount and an unmount go BACK as verbs,
;     because hd_vols and the real device table are the resident's and stay
;     the resident's.
; =============================================================================

;
; **EVERY STRING HERE FITS ITS BOX, and that is a rule and not a habit.** The
; caption column is 27 characters (221px of pane, text at x=2) and a row is
; 26; font_str clips to the SCREEN and not to the pane (SPEC.md 5), so a
; longer one does not stop at the edge - it paints over the panel's border and
; out the other side. Two captions did.
;
; GREYING FOLLOWS SPEC.md 47: one predicate per control, shared by the
; greying, the click refusal and the explanation; a fact and never a guess;
; and the whole control, not just its caption. The pen comes from
; OSAPI_GFX_PEN (slot 0x0260), so a disabled button carries [gfx_dis] as well
; as CDGRAY and its LABEL dithers on the two 1bpp adapters instead of coming
; out solid black next to a dotted frame.
; =============================================================================

HDP_HY       equ 4              ; the heading's text row
HDP_R0Y      equ 17             ; the first device row
HDP_ROWH     equ 11
HDP_LX       equ 2
HDP_EY       equ 63             ; the C/H/S editor's row
HDP_FH       equ 11
HDP_F0X      equ 10             ; the three field boxes
HDP_F1X      equ 66
HDP_F2X      equ 114
HDP_FW       equ 38
HDP_MX       equ 158            ; '-' and '+'
HDP_PX       equ 186
HDP_BTW      equ 24
HDP_BY       equ 82             ; the button row
HDP_BH       equ 16
; The button row reads Format, Install, Mount, evenly spaced (SPEC.md 52.4):
; 64 + 8 + 64 + 8 + 72 from x=2 ends at 218, inside the 221px pane, with the
; two that open a WINDOW together and the one that acts on the spot at the end.
; The INDICES did not move with the positions - 0 Format, 1 Mount, 2 Install
; is still what hd_btn_ok is asked and what hd_page_button draws - so B1 is to
; the right of B2 and the hit test walks the row 0, 2, 1.
HDP_B0X      equ 2              ; 'Format'
HDP_B1X      equ 146            ; 'Mount' / 'Unmount' - LAST on the row
HDP_B2X      equ 74             ; 'Install' (SPEC.md 52.10.4) - in the middle
HDP_BW0      equ 64
HDP_BW1      equ 72

; --- control ids (SPEC.md 13.8.4) -------------------------------------------
; 1-based so 0 can mean "nothing is down", and per PAGE, exactly as ctrl.inc's
; are: changing the page needs a click in the item list and the button is
; already down, so the selection cannot move between the press and the release.
HDP_CFORMAT  equ 1
HDP_CMOUNT   equ 2
HDP_CINSTALL equ 3
HDP_CMINUS   equ 4
HDP_CPLUS    equ 5

HDP_BW2      equ 64             ; Install's; the row's right edge is Mount's,
                                ; 146 + 72 = 218, inside the 221px pane - the
                                ; same arithmetic the caption column obeys
HDP_CAPY     equ 104

; -----------------------------------------------------------------------------
; hd_page_paint - DSV_CPPAINT
; in:  DI = pane left, DX = pane top; the pane is white-filled and the gfx
;      lock is HELD (SPEC.md 31.9)
; out: nothing
; clobbers: whatever it likes - it is a callback
; -----------------------------------------------------------------------------
hd_page_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call hd_sync                ; **THE FIRST THING, AND IT IS NOT A NICETY.**
                                ; hd_devs, [hd_ndev], [hd_sel], [hd_msgc] and
                                ; [hd_mntmask] are all THIS image's copies and
                                ; are zero until the resident fills them - so
                                ; a page that painted before syncing drew
                                ; `No hard disk found` on a machine with one,
                                ; with every button greyed by hd_btn_ok's
                                ; first compare. It is also the RIGHT place
                                ; rather than a load-time fill: the panel is
                                ; repainted after every click, and the tool
                                ; windows can change the table under it
                                ; (SPEC.md 52.13)
    mov bp, dx                  ; BP = pane top for the rest of the routine

    mov si, hd_s_page               ; NO PEN: font_run takes the pair in AX and
    mov cx, di                      ; never reads [gfx_color] (SPEC.md 52.12)
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_HY
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN

    ; --- one row per probed device -------------------------------------------
    xor cl, cl
.row:
    cmp cl, [hd_ndev]
    jae .rows_done
    push cx
    mov [hd_rowslot], cl
    mov al, cl
    call hd_dev_line            ; hd_line = 'IDE0  980x 10x 17   81M'
    mov al, [hd_rowslot]
    mov ah, HDP_ROWH
    mul ah
    add ax, HDP_R0Y
    mov dx, bp
    add dx, ax                  ; DX = the row's text y
    mov al, [hd_rowslot]
    cmp al, [hd_sel]
    jne .plain
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    add ax, HDP_LX - 1
    pop dx
    push dx
    mov bx, dx
    dec bx
    mov cx, di
    add cx, 214
    mov dx, bx
    add dx, 9
    call OSAPI_GFX_FILL
    pop dx
    mov word [hd_pair], (CBLACK << 8) | CWHITE  ; white on the bar just laid,
                                    ; and the PAIR is the whole of it - the fill
                                    ; above owns the pen (SPEC.md 52.12)
    jmp short .text
.plain:
    mov word [hd_pair], (CWHITE << 8) | CBLACK  ; ...or black on the pane
.text:
    mov si, hd_line
    mov cx, di
    add cx, HDP_LX
    mov ax, [hd_pair]           ; OPAQUE (SPEC.md 52.12)
    call OSAPI_FONT_RUN
    pop cx
    inc cl
    jmp short .row
.rows_done:
    cmp byte [hd_ndev], 0
    jne .geom
    mov si, hd_s_nodev
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_R0Y
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN

    ; --- the C/H/S editor ----------------------------------------------------
.geom:
    mov si, hd_s_c
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_EY + 2
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN
    mov si, hd_s_h
    mov cx, di
    add cx, HDP_F1X - 10
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN
    mov si, hd_s_s
    mov cx, di
    add cx, HDP_F2X - 10
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN

    mov byte [hd_fldi], 0
.field:
    mov al, [hd_fldi]
    call hd_page_fieldx         ; AX = the box's pane-relative x
    mov [hd_fldx], ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di
    add ax, [hd_fldx]
    mov cx, ax
    add cx, HDP_FW - 1
    mov bx, bp
    add bx, HDP_EY
    mov dx, bx
    add dx, HDP_FH - 1
    call OSAPI_GFX_FRAME
.fvalue:
    mov al, [hd_fldi]
    call hd_page_fieldv         ; AX = the value
    push di
    mov di, hd_line
    call hd_utoa
    mov byte [di], 0
    pop di
    mov si, hd_line
    call OSAPI_FONT_WIDTH       ; right-aligned in the box, so a 3-digit and
    mov cx, di                  ; a 4-digit value read as the same field
    add cx, [hd_fldx]
    add cx, HDP_FW - 4
    sub cx, ax
    mov dx, bp
    add dx, HDP_EY + 2
    mov si, hd_line
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN

    ; --- the SELECTED field's inner frame, ON TOP OF the value -------------
    ; It used to be drawn before it, and with a transparent value that was
    ; invisible: the two overlap on exactly one row. The box is HDP_FH = 11
    ; rows at HDP_EY, the inner frame HDP_EY+1 .. HDP_EY+8, and the value's
    ; eight cell rows are HDP_EY+2 .. HDP_EY+9 - so the frame's BOTTOM edge
    ; and the value's LAST row are the same row. A transparent glyph left it
    ; alone; an opaque run paints it, and the selected field lost the bottom
    ; of its ring. Chrome wins, so it is drawn last (SPEC.md 52.12)
    mov al, [hd_fldi]
    cmp al, [hd_field]
    jne .fnext
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, di                  ; a second frame just inside the first, which
    add ax, [hd_fldx]           ; is how the Date/Time page says "this one"
    inc ax
    mov cx, ax
    add cx, HDP_FW - 3
    mov bx, bp
    add bx, HDP_EY + 1
    mov dx, bx
    add dx, HDP_FH - 3
    call OSAPI_GFX_FRAME
.fnext:
    inc byte [hd_fldi]
    cmp byte [hd_fldi], 3
    jb .field

    ; --- the +/- pair ---------------------------------------------------------
    mov si, hd_s_minus
    mov ax, HDP_MX
    mov cl, HDP_CMINUS
    call hd_page_pm
    mov si, hd_s_plus
    mov ax, HDP_PX
    mov cl, HDP_CPLUS
    call hd_page_pm

    ; --- the three buttons, drawn by INDEX and placed by their own x ---------
    mov si, hd_s_format
    mov ax, HDP_B0X
    mov bx, HDP_BW0
    mov cl, 0
    call hd_page_button
    mov si, hd_s_mount
    call hd_mounted
    jc .mlabel
    mov si, hd_s_unmount
.mlabel:
    mov ax, HDP_B1X
    mov bx, HDP_BW1
    mov cl, 1
    call hd_page_button
    mov si, hd_s_install
    mov ax, HDP_B2X
    mov bx, HDP_BW2
    mov cl, 2
    call hd_page_button

    ; --- and the caption, which is where the truth goes ----------------------
    call hd_msg_str             ; SI = the string for [hd_msgc], resolved
    mov cx, di                  ; against THIS image's own (hddabi.inc)
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_CAPY
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: cp_page's pane (SPEC.md 52.12)
    call OSAPI_FONT_RUN

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_page_button - one framed button, greyed when its predicate says no
; in:  AX = x, BX = width (pane-relative), SI = label, CL = which button,
;      DI = pane left, BP = pane top
; out: nothing (all registers preserved)
;
; ONE PREDICATE, shared with the click and the caption (SPEC.md 47): the pen
; comes from hd_btn_ok and so does the refusal, so a greyed button can never
; be one that would have worked.
; -----------------------------------------------------------------------------

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; A Control Panel page draws its buttons one at a time, so the record is
; pointed at whichever rect the caller staged. The GESTURE here is the
; panel's own cp_ctl/cp_pgprobe probe (SPEC.md 13.8.4), which is already
; release-fired; what changed is that the DRAW goes through the control.
hdp_btlbl: dw 0
hdp_btflg: dw 0
    OS88UI_BTNREC hdp_btrec, 0, hdp_btlbl, hdp_btflg, 1

hd_page_button:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    push bx
    push si
    mov al, cl                  ; ...and PRESSED if it is the one being held.
    inc al                      ; ASKED FIRST, because CL is the button index
    call hd_isdown              ; on the way in and the DIS flag on the way
    mov dx, 0                   ; out - two meanings, one register, four
    jne .nodn                   ; instructions apart
    mov dx, OS88UI_DOWN
.nodn:
    mov al, cl
    call hd_btn_ok              ; CF = 1 = this control is disabled, which is
    mov cx, 0                   ; the shape the shared control's flag takes:
    adc cx, 0                   ; OS88UI_DIS is 1 and `mov` does not touch CF,
                                ; so the flag IS the carry, with no test in
                                ; between. It sets CDGRAY and [gfx_dis]
                                ; together and colours the FRAME as well as
                                ; the label - greying half a control is worse
                                ; than not greying it, and on mono the flag is
                                ; what stops the label being the half left
    pop si
    pop bx
    pop ax
    add ax, di                  ; ...and the rect, in screen coordinates
    mov [hd_pbrect+0], ax
    add ax, bx
    dec ax
    mov [hd_pbrect+4], ax
    mov ax, bp
    add ax, HDP_BY
    mov [hd_pbrect+2], ax
    add ax, HDP_BH - 1
    mov [hd_pbrect+6], ax
    mov di, cx
    or di, dx                   ; DIS | DOWN - and DIS outranks it inside the
                                ; control, so a greyed button never says it
                                ; will fire (SPEC.md 13.8)
    mov bx, hd_pbrect           ; OS88UI_FILL: the ground behind this is the
    or di, OS88UI_FILL          ; pane's white on a full paint and a PRESSED
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [hdp_btlbl], si          ; label and DI its flags, so the record takes
    mov [hdp_btflg], di          ; all three and the picture is identical
    mov [hdp_btrec+OS88UI_BT_RECTS], bx
    mov bx, hdp_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
                                ; the control leaves the pen live by itself,
                                ; so the clc/GFX_PEN that used to close this
                                ; routine is gone
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hd_pbrect:  dw 0, 0, 0, 0       ; the button being drawn, screen coordinates

; --- SPEC.md 13.8.4: this page acts on the RELEASE ---------------------------
; THE LADDER IS THE HIT TEST, which is ctrl.inc's own answer (SPEC.md 13.8.3)
; and for its reason: splitting hd_page_click into a hit test and an action
; would be a SECOND description of five controls' geometry that then has to
; agree with two painters. So the ladder is RUN to identify, and [hd_pgpr]
; (hd_pgpr - `hd_probe' is the HARDWARE probe) says which of three a pass is:
;
;   0  act - the release, once it knows the pointer is still on the control
;   1  identify only - the drag, and the release's own hit test
;   2  the PRESS: a row or a field still SELECTS (a safe prefix action keeps
;      the press, SPEC.md 13.6) while a button only reports its id
;
; That third mode is the whole reason hd_ctl takes an id of 0: it is what a
; site that is not an armable control passes, and it is the only thing that
; can tell the two species apart in one ladder.
hd_pgpr:    db 0
hd_ctlid:   db 0                ; ...and what the pass resolved to
hd_down:    db 0                ; which control is DRAWN pressed, 0 = none

; -----------------------------------------------------------------------------
; hd_ctl - a ladder site reports itself
; in:  AL = this site's id, or 0 for a site that is not an armable control
; out: CF = 1: return without acting. CF = 0: act. Every register preserved.
; -----------------------------------------------------------------------------
hd_ctl:
    cmp byte [hd_pgpr], 0
    je .act
    or al, al
    jz .row
    mov [hd_ctlid], al
    stc
    ret
.row:
    cmp byte [hd_pgpr], 2      ; the press: rows and fields still select
    je .act
    stc
    ret
.act:
    clc
    ret

; -----------------------------------------------------------------------------
; hd_isdown - is control AL the one drawn pressed?
; out: ZF = 1 if it is; every register preserved
; -----------------------------------------------------------------------------
hd_isdown:
    cmp al, [hd_down]
    ret

; -----------------------------------------------------------------------------
; hd_page_probe - run the ladder to IDENTIFY
; in:  DI = pane left, DX = pane top, CX/BX = pane-relative point, AL = the
;      mode wanted (1 = identify only, 2 = the press)
; out: AL = the control the point landed on, 0 = none
; -----------------------------------------------------------------------------
hd_page_probe:
    mov [hd_pgpr], al
    mov byte [hd_ctlid], 0
    call hd_page_ladder
    mov byte [hd_pgpr], 0      ; cleared on EVERY path out, not by the three
    mov al, [hd_ctlid]          ; callers: a page left probing would take no
    ret                         ; clicks at all for the rest of the session

; -----------------------------------------------------------------------------
; hd_page_click - DSV_CPCLICK: the PRESS
; in:  DI = pane left, DX = pane top, CX/BX = pane-relative point
; out: nothing
;
; A row or a field still SELECTS here - a safe prefix action keeps the press
; (SPEC.md 13.6) - and a BUTTON only arms and draws itself down. That split is
; what mode 2 is for, and it is the whole difference between this page and a
; window: a window can afford two ladders, a page ABI hands over one entry.
; -----------------------------------------------------------------------------
hd_page_click:
    push ax
    push bp
    mov al, 2
    call hd_page_probe
    or al, al
    jz .out
    mov bp, dx                  ; the painters take the pane top in BP
    xor ah, ah
    call os88ui_arm
    call hd_setdown             ; ...and the pressed look
.out:
    pop bp
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ctlpaint - redraw ONE control, in whatever state [hd_down] says
; in:  AL = a control id, DI = pane left, BP = pane top; the lock is held
; out: nothing (all registers preserved)
;
; NOT hd_page_repaint: that erases the whole pane, so driving the pressed look
; through it would white 221x131 and letter it again on every press and every
; slide across a control - PERFORMANCE.md's double-draw flash, introduced by
; the feature whose whole argument is that it does not flash.
; -----------------------------------------------------------------------------
hd_ctlpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp al, HDP_CMINUS
    jb .btn
    mov cl, al                  ; the +/- pair; CL is the id hd_page_pm reads
    mov si, hd_s_minus
    mov ax, HDP_MX
    cmp cl, HDP_CMINUS
    je .pm
    mov si, hd_s_plus
    mov ax, HDP_PX
.pm:
    call hd_page_pm
    jmp short .out
.btn:
    dec al                      ; the three framed buttons, 0-based
    mov cl, al
    mov si, hd_s_format
    mov ax, HDP_B0X
    mov bx, HDP_BW0
    or cl, cl
    jz .draw
    cmp cl, 2
    je .inst
    mov si, hd_s_mount          ; Mount says Unmount when this device already
    call hd_mounted             ; has a volume (hd_page_paint's own branch)
    jc .m
    mov si, hd_s_unmount
.m:
    mov ax, HDP_B1X
    mov bx, HDP_BW1
    jmp short .draw
.inst:
    mov si, hd_s_install
    mov ax, HDP_B2X
    mov bx, HDP_BW2
.draw:
    call hd_page_button
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_setdown - AL becomes the control drawn pressed (0 = none), and it DRAWS
; in:  AL = an id or 0; DI = pane left, BP = pane top; the lock is held
; out: nothing (all registers preserved)
;
; Draws only what CHANGED - the control being let go and the one being taken -
; so a pointer moving inside the control it pressed costs one compare and no
; pixels (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
hd_setdown:
    push ax
    cmp al, [hd_down]
    je .out
    push ax
    mov al, [hd_down]           ; ...the one being let go, upright again
    or al, al
    jz .take
    mov byte [hd_down], 0
    call hd_ctlpaint
.take:
    pop ax
    mov [hd_down], al
    or al, al
    jz .out
    call hd_ctlpaint            ; ...and the one being taken, pressed
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_page_up - DSV_CPUP (SPEC.md 13.8.4). Same arguments as DSV_CPCLICK.
; -----------------------------------------------------------------------------
hd_page_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov si, ax                  ; SI = the armed control. NOT BX and NOT the
                                ; stack: BX is the ladder's own pane-relative
                                ; Y, so banking the id there hands the second
                                ; probe below a click at y = 2
    mov bp, dx                  ; BP = pane top, the painters' convention
    mov al, 1                   ; identify only
    call hd_page_probe
    xor ah, ah
    cmp ax, si
    je .fire
    xor al, al                  ; a different control, or none: CANCELLED
    call hd_setdown
    jmp short .out
.fire:
    mov byte [hd_down], 0       ; NOT hd_setdown: every path below repaints the
                                ; pane, which draws this control upright with
                                ; it - and drawing it upright here first is
                                ; PERFORMANCE.md's double-draw
    xor al, al                  ; ...and now for real
    call hd_page_probe
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
; hd_page_drag - DSV_CPDRAG (SPEC.md 13.8.1/13.8.4)
; -----------------------------------------------------------------------------
hd_page_drag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov si, ax                  ; SI, for hd_page_up's reason: BX is the
    mov bp, dx                  ; ladder's pane-relative y
    mov al, 1
    call hd_page_probe          ; AL = what the pointer is over now
    xor ah, ah
    cmp ax, si
    je .set
    xor al, al                  ; off it: nothing is down
.set:
    call hd_setdown             ; ...which draws only if that CHANGED
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
; hd_page_pm - one of the two adjust buttons (module internal)
; in:  AX = x, SI = its label, DI/BP = the pane origin
;
; The Control Panel's Date/Time page has the same pair and draws it with the
; same shared control (SPEC.md 31.2), which is the whole reason this one is
; converted as well: two +/- pairs one page apart in the same window should
; not be two different controls.
;
; ONE PIXEL: the glyph's y was the literal +3 in a 13-row box and the shared
; control centres at (13-8)/2 = 2, so it sits one row higher. Horizontally it
; already WAS centred - HDP_BTW/2 - 4 is (24-8)/2 - and never looked it.
; -----------------------------------------------------------------------------
hd_page_pm:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add ax, di
    mov [hd_pbrect+0], ax
    add ax, HDP_BTW - 1
    mov [hd_pbrect+4], ax
    mov ax, bp
    add ax, HDP_EY - 1
    mov [hd_pbrect+2], ax
    add ax, HDP_FH + 1
    mov [hd_pbrect+6], ax
    mov di, OS88UI_FILL         ; always live, never the default - and it FILLS
                                ; for the reason hd_page_button does: a
                                ; release redraws it over a pressed one
    mov al, cl                  ; CL = the id, set by the caller (nothing here
    call hd_isdown              ; overwrites CX, unlike hd_page_button)
    jne .nodn
    or di, OS88UI_DOWN
.nodn:
    mov bx, hd_pbrect
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [hdp_btlbl], si          ; label and DI its flags, so the record takes
    mov [hdp_btflg], di          ; all three and the picture is identical
    mov [hdp_btrec+OS88UI_BT_RECTS], bx
    mov bx, hdp_btrec
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

; -----------------------------------------------------------------------------
; hd_btn_ok - may this button be clicked? (module internal)
; in:  AL = 0 Format / 1 Mount
; out: CF = 0 yes, CF = 1 = disabled
; clobbers: flags
;
; A FACT and never a guess (SPEC.md 47 rule 4): every test here is something
; already known - is there a device, is its geometry usable, is it already
; mounted - and none of them is "try it and see".
; -----------------------------------------------------------------------------
hd_btn_ok:
    push bx
    push di
    cmp byte [hd_ndev], 0
    je .no
    push ax
    mov al, [hd_sel]
    call hd_dev_row
    call hd_geom_ok
    pop ax
    jc .no                      ; no usable geometry: nothing may be written
    cmp al, 1
    jne .yes                    ; Format needs nothing more
    pop di
    pop bx
    clc
    ret                         ; Mount is live either way - it becomes
                                ; Unmount when this device already has a
                                ; volume, and that is a different ACTION and
                                ; not a disabled one
.yes:
    pop di
    pop bx
    clc
    ret
.no:
    pop di
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_mounted - does the selected device already have a mounted volume?
; in:  [hd_sel], [hd_mntmask]
; out: CF = 1 no, CF = 0 yes
; clobbers: flags
;
; IT WALKED hd_vols AND IT CANNOT: that table is the resident's, the kernel
; registered those volume indices out of it, and this image never sees it
; (SPEC.md 52.13). HSV_STATE answers the one question the page ever asked it
; as a bit per device, so the walk happens where the table is. It also stops
; ANSWERING a row - all three callers only ever read CF, which is why a mask
; is enough rather than a compromise.
; -----------------------------------------------------------------------------
hd_mounted:
    push ax
    push cx
    mov cl, [hd_sel]
    mov al, 1
    shl al, cl                  ; HD_MAXDEV is 4, so the mask is a byte
    test al, [hd_mntmask]
    jz .no
    pop cx
    pop ax
    clc
    ret
.no:
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_page_fieldx / hd_page_fieldv - the C/H/S editor's three fields
; in:  AL = field (0 = cylinders, 1 = heads, 2 = sectors)
; out: hd_page_fieldx: AX = the box's pane-relative x
;      hd_page_fieldv: AX = the selected device's value for it
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hd_page_fieldx:
    cmp al, 1
    je .f1
    ja .f2
    mov ax, HDP_F0X
    ret
.f1:
    mov ax, HDP_F1X
    ret
.f2:
    mov ax, HDP_F2X
    ret

hd_page_fieldv:
    push bx
    push di
    mov bl, al
    mov al, [hd_sel]
    call hd_dev_row
    xor ax, ax
    cmp byte [hd_ndev], 0
    je .out
    cmp bl, 1
    je .v1
    ja .v2
    mov ax, [di+HDD_CYL]
    jmp short .out
.v1:
    mov ax, [di+HDD_HEADS]
    jmp short .out
.v2:
    mov ax, [di+HDD_SPT]
.out:
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_page_adjust - move the selected field by AL (+1 / -1)
; in:  AL = the step, signed
; out: nothing
; clobbers: flags
;
; The bounds are CHS addressing's own: 1..1024 cylinders, 1..255 heads, 1..63
; sectors. A value outside them is not a geometry, so the field simply stops.
; -----------------------------------------------------------------------------
hd_page_adjust:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [hd_ndev], 0
    je .out
    mov cl, al                  ; CL = the step
    mov al, [hd_sel]
    call hd_dev_row
    mov bl, [hd_field]
    mov al, bl
    call hd_page_fieldv         ; AX = the value now
    mov dx, 1024                ; DX = its ceiling
    cmp bl, 1
    jb .have
    mov dx, 255
    je .heads
    mov dx, 63
    jmp short .have
.heads:
    cmp byte [di+HDD_KIND], HDK_IDE
    jne .have
    mov dx, 16                  ; the task file has four bits of head: the
.have:                          ; field STOPS there rather than accepting a
                                ; number hd_geom_ok would then refuse
    cmp cl, 1
    jne .down
    cmp ax, dx
    jae .out
    inc ax
    jmp short .store
.down:
    cmp ax, 2
    jb .out
    dec ax
.store:
    cmp bl, 1
    je .s1
    ja .s2
    mov [di+HDD_CYL], ax
    jmp short .flag
.s1:
    mov [di+HDD_HEADS], ax
    jmp short .flag
.s2:
    mov [di+HDD_SPT], ax
.flag:
    and byte [di+HDD_FLAGS], 0xFE   ; typed in by hand is still KNOWN, as
    or byte [di+HDD_FLAGS], 2       ; long as it is usable - which is the
    call hd_geom_ok                 ; whole point of the editor. Bit 1 says
    jc .mark                        ; the USER set it, which is what makes it
    or byte [di+HDD_FLAGS], 1       ; worth a record in the blob (SPEC.md 52.6)
    mov byte [hd_msgc], HDM_TYPED
    call hd_geom_push               ; ...and the DRIVE is told, or it keeps
                                    ; translating with the one the probe left
.mark:
    call hd_cfg_mark                ; staged on every click and written on
                                    ; none: the panel's teardown spends it
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_dev_line - one device row's line, into hd_line (module internal)
; in:  AL = the device index
; out: hd_line holds a NUL string
; clobbers: flags
; -----------------------------------------------------------------------------
hd_dev_line:
    push ax
    push bx
    push si
    push di
    mov [hd_rowdev], al
    call hd_dev_row
    mov bx, di                  ; BX = the device row for the whole routine;
    mov di, hd_line             ; DI is the string cursor
    mov si, hd_s_bios
    cmp byte [bx+HDD_KIND], HDK_IDE
    jne .kind
    mov si, hd_s_ide
.kind:
    call hd_scat
    mov al, [bx+HDD_UNIT]       ; the DRIVE, not the row: an int 13h unit is
    and al, 0x7F                ; 80h/81h and an IDE one is 0/1, and neither
    xor ah, ah                  ; is the order we happened to probe them in
    call hd_utoa
    mov si, hd_s_two
    call hd_scat
    push di                     ; hd_geom_ok wants the row in DI
    mov di, bx
    call hd_geom_ok
    pop di
    jc .unknown
    mov ax, [bx+HDD_CYL]
    call hd_utoa4
    mov si, hd_s_x
    call hd_scat
    mov ax, [bx+HDD_HEADS]
    call hd_utoa3
    mov si, hd_s_x
    call hd_scat
    mov ax, [bx+HDD_SPT]
    call hd_utoa3
    mov si, hd_s_two
    call hd_scat
    push di
    mov di, bx
    call hd_dev_mb              ; AX = megabytes
    pop di
    call hd_utoa4
    mov si, hd_s_mb
    call hd_scat
    jmp short .done
.unknown:
    mov si, hd_s_unk
    call hd_scat
.done:
    mov byte [di], 0
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_page_ladder - the page's one coordinate ladder (module internal)
; in:  DI = pane left, DX = pane top, CX = pane-relative x, BX = relative y
;      (the gfx lock is HELD); [hd_pgpr] says whether this pass ACTS
; out: nothing
;
; THE LADDER IS THE HIT TEST (SPEC.md 13.8.4), so the press, the drag and the
; release all run this one description rather than three that have to agree.
; -----------------------------------------------------------------------------
hd_page_ladder:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, dx
    mov dx, bx                  ; DX = the relative y, for the tests below
    mov [hd_clx], cx
    mov [hd_cly], dx

    cmp dx, HDP_R0Y - 2
    jl .done
    cmp dx, HDP_EY - 2
    jl .devrow
    cmp dx, HDP_BY - 2
    jl .editor
    cmp dx, HDP_BY + HDP_BH
    jl .buttons
    jmp .done

.devrow:
    xor al, al                  ; NOT an armable control: a row SELECTS, which
    call hd_ctl                 ; is a safe prefix action and keeps the press
    jc .done                    ; (SPEC.md 13.6) - but it must not happen a
                                ; second time on the release's own hit test
    mov ax, dx
    sub ax, HDP_R0Y - 2
    mov bl, HDP_ROWH
    div bl
    cmp al, [hd_ndev]
    jae .done
    mov [hd_sel], al
    mov byte [hd_msgc], HDM_PICK
    jmp .repaint

.editor:
    mov cx, [hd_clx]
    cmp cx, HDP_MX
    jl .pickfield
    cmp cx, HDP_MX + HDP_BTW
    jl .minus
    cmp cx, HDP_PX
    jl .done
    cmp cx, HDP_PX + HDP_BTW
    jge .done
    mov al, HDP_CPLUS
    call hd_ctl
    jc .done
    mov al, 1
    call hd_page_adjust
    jmp .repaint
.minus:
    mov al, HDP_CMINUS
    call hd_ctl
    jc .done
    mov al, 0
    call hd_page_adjust
    jmp .repaint
.pickfield:
    xor al, al                  ; a field SELECTS too - .devrow's reasoning
    call hd_ctl
    jc .done
    xor bl, bl
.fscan:
    mov al, bl
    call hd_page_fieldx
    cmp cx, ax
    jl .fnext
    add ax, HDP_FW
    cmp cx, ax
    jge .fnext
    mov [hd_field], bl
    jmp .repaint
.fnext:
    inc bl
    cmp bl, 3
    jb .fscan
    jmp .done

.buttons:
    ; LEFT TO RIGHT, which is 0, 2, 1: the row reads Format, Install, Mount and
    ; the indices did not move with it (SPEC.md 52.4). The ladder walks the
    ; row in x order because each rung's `jl .done` means "in the gap before
    ; this button", and a rung out of order turns a gap into another button's
    ; interior - the click would land on whichever index was tested first.
    mov cx, [hd_clx]
    xor bl, bl                  ; BL = which button
    cmp cx, HDP_B0X
    jl .done
    cmp cx, HDP_B0X + HDP_BW0
    jl .hit
    mov bl, 2
    cmp cx, HDP_B2X
    jl .done
    cmp cx, HDP_B2X + HDP_BW2
    jl .hit
    mov bl, 1
    cmp cx, HDP_B1X
    jl .done
    cmp cx, HDP_B1X + HDP_BW1
    jge .done
.hit:
    mov al, bl                  ; ids are 1-based (0 means "nothing is down"),
    inc al                      ; and BL is the 0-based index hd_btn_ok wants
    call hd_ctl
    jc .done
    mov al, bl
    call hd_btn_ok
    jc .refused
    cmp bl, 0
    je .dofmt
    cmp bl, 2
    je .doinst
    call hd_mounted
    jc .domount
    call hd_unmount
    jmp short .saved            ; the SET of mounted drives is what the next
.domount:                       ; boot reads - STAGED, like every other
    call hd_mount               ; setting in the machine, and on the disk when
.saved:                         ; the panel closes (SPEC.md 31.8/52.6)
    call hd_cfg_mark
    jmp .repaint
                                ; Opening the tool stages nothing: the geometry
                                ; is all it reads, and the editor staged that on
                                ; the click that changed it. A Delete INSIDE it
                                ; can unmount, and stages there (SPEC.md 52.2.4)
                                ; Both tools repaint the PANE FIRST and open the
                                ; window SECOND. A Control Panel page draws
                                ; itself unclipped (SPEC.md 31.9) and the kernel
                                ; does not repaint after a click returns, so a
                                ; pane repaint AFTER wm_show paints straight
                                ; over the window it has just put up - which is
                                ; what the Install dialog arrived doing.
                                ; ...and a REFUSED open is the exception to
                                ; that ordering, because there is no window to
                                ; paint over: the caption it just set would
                                ; otherwise sit undrawn until something else
                                ; repainted the page (hdtool.inc)
.dofmt:
    call hd_page_repaint
    mov al, HDT_FORMAT          ; the tool is a second image now, read off
    call hd_tool_open           ; the system volume on this click (52.11)
    jc .repaint
    jmp short .done
.doinst:                        ; Install stages nothing either, for the tool's
    call hd_page_repaint        ; reason: what it changes is a DISK, and about
    mov al, HDT_INSTALL         ; the machine's settings nothing at all
    call hd_tool_open
    jc .repaint
    jmp short .done
.refused:
    ; A greyed control explains itself (SPEC.md 47 rule 5), so the refusal
    ; says nothing new - it just makes sure the reason is on screen.
    mov byte [hd_msgc], HDM_NOGEOM
    cmp byte [hd_ndev], 0
    jne .repaint
    mov byte [hd_msgc], HDM_NODEV
.repaint:
    call hd_page_repaint
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
; hd_page_repaint - erase the pane and draw it again (module internal)
; in:  DI = pane left, BP = pane top; the lock is held
; out: nothing (all registers preserved)
;
; The pane and nothing else: a page draws itself and the kernel does not
; repaint after a click returns (SPEC.md 31.9).
; -----------------------------------------------------------------------------
hd_page_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, bp
    mov cx, di
    add cx, 221                 ; the pane: 221 x 132, and it GREW when a third
    mov dx, bp                  ; driver published a page (ctrl.inc's CP_CH).
    add dx, 131                 ; A short clear here leaves a stale strip at the
    call OSAPI_GFX_FILL         ; bottom of a click-repaint, and nothing says so
    mov dx, bp
    call hd_page_paint
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_msg_str - [hd_msgc] as a string in THIS image (module internal)
; in:  [hd_msgc], and [hd_nmnt] for the one that is built
; out: SI -> a NUL string in the rendering image's own segment
; clobbers: SI (the output), flags
;
; HDM_MOUNTED is composed rather than looked up, which is why the table has a
; 0 in that slot and not a pointer: a caption carrying a NUMBER cannot be a
; constant, and building it HERE is what lets the resident forget it entirely
; (hddabi.inc). Its buffer is its own and not hd_line, which every device row
; rewrites on the way past - a caption outlives the paint that put it up.
;
; An out-of-range code answers HDM_PICK rather than reading past the table:
; the byte crosses an image boundary, so it is INPUT and is treated as such.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; hd_tool_open - open one of the two windows (module internal)
; in:  AL = HDT_FORMAT or HDT_INSTALL
; out: CF = 0 a window is up; CF = 1 nothing opened and [hd_msgc] says why
; clobbers: flags
;
; THE RESIDENT'S NAME AND THE RESIDENT'S CONTRACT, so hd_page_click's two call
; sites did not move - and over here it is not a load at all. The resident's
; version brought this image in and called a verb; the page is IN this image,
; so the windows are a near call away and the only thing that can fail is the
; window itself. That is the whole of what moving the page bought the two
; buttons: the file is already here by the time either can be pressed.
; -----------------------------------------------------------------------------
hd_tool_open:
    push ax
    call hd_sync                ; the geometry as the panel last left it -
    cmp al, HDT_FORMAT          ; hddtool.asm's own verbs do this first too
    jne .inst
    call hd_tw_open
    cmp word [hd_twin], 0
    je .no
    jmp short .yes
.inst:
    call hd_iw_open
    cmp word [hd_iwin], 0
    je .no
.yes:
    pop ax
    clc
    ret
.no:
    mov al, HDM_NOTOOL
    call hd_say
    pop ax
    stc
    ret

hd_s_page:   db 'Hard Drive', 0 ; THE PAGE'S HEADING, and the resident keeps
                                ; its own copy for DSV_CPNAME - cp_list
                                ; letters that one through the kernel's own
                                ; staging (SPEC.md 31.9), so the two are the
                                ; same eleven bytes in two images on purpose

; --- the page's OWN state, which came over with it (SPEC.md 52.13) ---------
; Every one of these was in the resident's bss and had exactly one reader:
; this file. hd_ndev, hd_sel, hd_rowslot and hd_pair are NOT here - they are
; hdcom.inc's and so exist in both images already, which is what makes
; hd_sync a copy rather than an ABI.
hd_field:    db 0               ; the C/H/S field the page has selected
hd_rowdev:   db 0               ; the device a row painter is on
hd_fldi:     db 0               ; the C/H/S editor's loop index
hd_fldx:     dw 0
hd_clx:      dw 0               ; the click being dispatched
hd_cly:      dw 0

hd_msg_str:
    push ax
    push bx
    push di
    mov al, [hd_msgc]
    cmp al, HDM_N
    jb .known
    xor al, al                  ; HDM_PICK: a code we do not have a string for
.known:
    cmp al, HDM_MOUNTED
    je .built
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [hd_msg_tab+bx]
    jmp short .out
.built:
    mov di, hd_cap
    mov si, hd_s_mounted
    call hd_scat
    mov al, [hd_nmnt]
    xor ah, ah
    call hd_utoa
    mov si, hd_s_vols
    cmp byte [hd_nmnt], 1
    jne .plural
    mov si, hd_s_vol1
.plural:
    call hd_scat
    mov byte [di], 0
    mov si, hd_cap
.out:
    pop di
    pop bx
    pop ax
    ret

hd_msg_tab:
    dw hd_s_pick                ; HDM_PICK
    dw hd_s_nosys               ; HDM_NOSYS
    dw hd_s_notool              ; HDM_NOTOOL
    dw hd_s_typed               ; HDM_TYPED
    dw hd_s_nogeom              ; HDM_NOGEOM
    dw hd_s_nodev               ; HDM_NODEV
    dw hd_s_mnotab              ; HDM_MNOTAB
    dw hd_s_mnofat              ; HDM_MNOFAT
    dw hd_s_mhave               ; HDM_MHAVE
    dw hd_s_mbig                ; HDM_MBIG
    dw hd_s_mfull               ; HDM_MFULL
    dw hd_s_unmounted           ; HDM_UNMOUNTED
    dw 0                        ; HDM_MOUNTED is BUILT, never looked up
%if ($ - hd_msg_tab) / 2 != HDM_N
  %error "hd_msg_tab has drifted from hddabi.inc's HDM_* - one word per code, in order"
%endif

hd_cap:      times 32 db 0      ; ...and HDM_MOUNTED's buffer, which belongs
                                ; with the strings it is composed from

hd_s_install: db 'Install', 0
hd_s_bios:    db 'BIOS', 0
hd_s_ide:     db 'IDE', 0
hd_s_x:       db 'x', 0
hd_s_unk:     db ' geometry unknown', 0
hd_s_c:       db 'C', 0
hd_s_h:       db 'H', 0
hd_s_s:       db 'S', 0
hd_s_minus:   db '-', 0
hd_s_plus:    db '+', 0
hd_s_format:  db 'Format', 0
hd_s_mount:   db 'Mount', 0
hd_s_unmount: db 'Unmount', 0
hd_s_pick:    db 'Pick a drive, then a button', 0
hd_s_nodev:   db 'No hard disk found', 0
hd_s_nogeom:  db 'Geometry unknown: type it', 0
hd_s_typed:   db 'Geometry set by hand', 0
hd_s_mounted: db 'Mounted ', 0
hd_s_vol1:    db ' volume', 0
hd_s_vols:    db ' volumes', 0
hd_s_mnofat:  db 'No FAT partition on it', 0
hd_s_mhave:   db 'Already mounted', 0
hd_s_unmounted: db 'Unmounted', 0
hd_s_mnotab:  db 'No partition table yet', 0
hd_s_mfull:   db 'No free volume', 0
hd_s_mbig:    db 'Partition is over 32MB', 0
hd_s_nosys:   db 'Need the system disk', 0
hd_s_notool:  db 'Tool would not open', 0

