; =============================================================================
; os8088 - HDD.DRV / page.inc
;
; The Control Panel's Hard Drive page (SPEC.md 52.4), and the small helpers
; the rest of the driver shares.
;
; The page exists exactly while this driver is attached, which is not arranged
; anywhere: it falls out of the kernel clearing the DISK class's publication
; slot at detach (SPEC.md 31.9/51.2.1).
;
; The pane is 221 x 132 px - 27 characters by 14 rows - and the panel is not
; resizable, so this page is a list, an editor and two buttons, and anything
; that needs more room is a window of its own. Format opens the disk tool
; (tool.inc), which is partitioning and formatting in one window because they
; are one operation to the user.
;
;   Hard Drive
;    IDE0  980x 10x 17   81M      <- one row per probed device, the selected
;    BIOS  615x  4x 17   20M         one drawn as a black bar
;   C[ 980] H[ 10] S[ 17]  - +    <- the geometry, editable when the probe
;   [Format] [Mount]                 could not determine it
;   Mounted as C:                 <- and the caption is where the truth goes
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
; OSAPI_GFX_PEN (slot 0x0310), so a disabled button carries [gfx_dis] as well
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
HDP_B0X      equ 2              ; 'Format'
HDP_B1X      equ 78             ; 'Mount' / 'Unmount'
HDP_B2X      equ 154            ; 'Install' (SPEC.md 52.10.4)
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

HDP_BW2      equ 64             ; 154 + 64 = 218, inside the 221px pane - the
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
    mov bp, dx                  ; BP = pane top for the rest of the routine

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hd_s_page
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_HY
    call OSAPI_FONT_STR

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
    mov al, CWHITE
    call OSAPI_SET_COLOR
    jmp short .text
.plain:
    mov al, CBLACK
    call OSAPI_SET_COLOR
.text:
    mov si, hd_line
    mov cx, di
    add cx, HDP_LX
    call OSAPI_FONT_STR
    pop cx
    inc cl
    jmp short .row
.rows_done:
    cmp byte [hd_ndev], 0
    jne .geom
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hd_s_nodev
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_R0Y
    call OSAPI_FONT_STR

    ; --- the C/H/S editor ----------------------------------------------------
.geom:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hd_s_c
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_EY + 2
    call OSAPI_FONT_STR
    mov si, hd_s_h
    mov cx, di
    add cx, HDP_F1X - 10
    call OSAPI_FONT_STR
    mov si, hd_s_s
    mov cx, di
    add cx, HDP_F2X - 10
    call OSAPI_FONT_STR

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
    mov al, [hd_fldi]
    cmp al, [hd_field]
    jne .fvalue
    mov ax, di                  ; the selected field: a second frame just
    add ax, [hd_fldx]           ; inside the first, which is how the
    inc ax                      ; Date/Time page says "this one"
    mov cx, ax
    add cx, HDP_FW - 3
    mov bx, bp
    add bx, HDP_EY + 1
    mov dx, bx
    add dx, HDP_FH - 3
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
    call OSAPI_FONT_STR
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

    ; --- the two buttons ------------------------------------------------------
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
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, [hd_msg]
    mov cx, di
    add cx, HDP_LX
    mov dx, bp
    add dx, HDP_CAPY
    call OSAPI_FONT_STR

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
    call os88ui_btn             ; button on a release, and only the fill covers
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
    call os88ui_btn
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
; in:  nothing
; out: CF = 1 no, CF = 0 and BX = the hd_vols row
; clobbers: BX (the output), flags
; -----------------------------------------------------------------------------
hd_mounted:
    push ax
    push cx
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    je .yes
.next:
    add bx, HDV_SIZE
    loop .scan
    xor bx, bx
    pop cx
    pop ax
    stc
    ret
.yes:
    pop cx
    pop ax
    clc
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
    mov word [hd_msg], hd_s_typed
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
    mov word [hd_msg], hd_s_pick
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
    mov cx, [hd_clx]
    xor bl, bl                  ; BL = which button
    cmp cx, HDP_B0X
    jl .done
    cmp cx, HDP_B0X + HDP_BW0
    jl .hit
    mov bl, 1
    cmp cx, HDP_B1X
    jl .done
    cmp cx, HDP_B1X + HDP_BW1
    jl .hit
    mov bl, 2
    cmp cx, HDP_B2X
    jl .done
    cmp cx, HDP_B2X + HDP_BW2
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
    mov word [hd_msg], hd_s_nogeom
    cmp byte [hd_ndev], 0
    jne .repaint
    mov word [hd_msg], hd_s_nodev
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

; =============================================================================
; Mounting (SPEC.md 52.4)
; =============================================================================

; -----------------------------------------------------------------------------
; hd_mount - mount the selected device's first FAT partition
; in:  nothing; the gfx lock is HELD (a page click)
; out: nothing; [hd_msg] says what happened
; clobbers: flags
;
; The listing claim comes FIRST and is handed to the kernel with the volume,
; because a mounted volume with no listing buffer lists 32 entries instead of
; DSK_VENT and there is no second chance to say so (SPEC.md 22.6). A refused
; claim is not an error - the volume mounts with the kernel's own floor.
; -----------------------------------------------------------------------------
hd_mount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov byte [hd_nmnt], 0
    mov word [hd_msg], hd_s_mnotab
    mov al, [hd_sel]
    call hd_dev_row
    call hd_part_load
    jc .out                     ; no readable table: nothing to mount

    mov word [hd_msg], hd_s_mnofat
    mov byte [hd_pslot], 0
.slot:
    mov al, [hd_pslot]
    call hd_part_ent
    call hd_part_isfat
    jc .next                    ; free, or a type that is not a FAT volume:
                                ; SKIPPED, which is the whole of "and if it
                                ; is not FAT, do not" (SPEC.md 52.4)
    cmp word [si+HP_SECS+2], 0
    jne .big                    ; over 65,535 sectors: not a volume this
                                ; kernel can carry (SPEC.md 18.7)
    call hd_already
    jnc .have                   ; this one is already mounted - by us...
    mov al, [hd_sel]            ; ...or by the KERNEL, which adopted the
    call hd_kvol                ; partition it booted from before we were
    jnc .have                   ; loaded at all (SPEC.md 52.10.3). Mounting it
                                ; again is a second icon on the same sectors.
                                ; It is hdcom.inc's and takes the DEVICE as an
                                ; argument, because the installer asks the
                                ; same question of a device of its own
                                ; (SPEC.md 52.10.4.1)
    call hd_mount_one
    jmp short .next
.have:
    mov word [hd_msg], hd_s_mhave   ; and SAY so: the caption was still
                                    ; 'No FAT partition on it' from the top of
                                    ; the loop, which about a partition that is
                                    ; mounted and on the desktop is the one
                                    ; answer that is not true
    jmp short .next
.big:
    mov word [hd_msg], hd_s_mbig    ; the tool calls this slot Unmountable and
                                    ; so does the page, rather than reporting
                                    ; 'no FAT partition' about a partition that
                                    ; plainly is one (SPEC.md 52.2.2)
.next:
    inc byte [hd_pslot]
    cmp byte [hd_pslot], 4
    jb .slot

    cmp byte [hd_nmnt], 0
    je .out                     ; the caption is whichever refusal got here
    call hd_mount_says          ; 'Mounted n volume(s)'
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_mount_one - mount the partition [hd_pslot] names (module internal)
; in:  SI = its entry, [hd_sel] = the device; the gfx lock is HELD
; out: [hd_nmnt] incremented on success; [hd_msg] set on a failure
; clobbers: flags
;
; The listing claim comes FIRST and is handed to the kernel with the volume,
; because a mounted volume with no listing buffer lists 32 entries instead of
; DSK_VENT and there is no second chance to say so (SPEC.md 22.6). A refused
; claim is not an error - the volume mounts with the kernel's own floor.
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
    call hd_vol_new             ; BX = a free hd_vols row
    jc .full
    mov al, [hd_sel]
    mov [bx+HDV_DEV], al
    mov al, [hd_pslot]
    mov [bx+HDV_PART], al
    mov ax, [si+HP_LBA]
    mov [bx+HDV_BASE], ax
    mov ax, [si+HP_LBA+2]
    mov [bx+HDV_BASE+2], ax
    mov ax, [si+HP_SECS]
    mov [bx+HDV_SECS], ax

    mov ax, HDD_LISTKB          ; the listing claim (SPEC.md 22.6)
    call OSAPI_MEM_CLAIM
    jnc .claimed
    xor dx, dx                  ; refused: the kernel's 32-entry floor, which
.claimed:                       ; works and shows fewer files
    mov [bx+HDV_LSEG], dx

    mov al, [bx+HDV_VOLH]       ; our own handle IS the row index
    mov cx, [bx+HDV_SECS]
    xor si, si                  ; no label: the KERNEL derives 'HDD C'..'HDD F'
    call OSAPI_VOL_ADD          ; from the volume index, which is the only
    jc .novol                   ; place that knows it (SPEC.md 26.1)
    mov [bx+HDV_VOL], al
    mov byte [bx+HDV_USED], 1
    call OSAPI_VOL_MOUNT        ; AL is still the volume index
    jc .badvol
    inc byte [hd_nmnt]
    jmp short .out
.badvol:
    call hd_unmount_row         ; typed FAT, is not FAT: give it all back and
    jmp short .out              ; leave the loop to try the next slot
.novol:
    mov dx, [bx+HDV_LSEG]
    or dx, dx
    jz .nofree
    call OSAPI_MEM_FREE
.nofree:
    mov byte [bx+HDV_USED], 0
.full:
    mov word [hd_msg], hd_s_mfull
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_already - is [hd_sel]'s partition [hd_pslot] mounted already?
; in:  nothing
; out: CF = 0 yes, CF = 1 no
; clobbers: flags
; -----------------------------------------------------------------------------
hd_already:
    push ax
    push bx
    push cx
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, [hd_pslot]
    je .yes
.next:
    add bx, HDV_SIZE
    loop .scan
    pop cx
    pop bx
    pop ax
    stc
    ret
.yes:
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_mount_says - 'Mounted n volume(s)', into the caption buffer (internal)
; in:  [hd_nmnt]
; out: [hd_msg] points at it
; clobbers: flags
;
; Its OWN buffer and not hd_line, which every device row rewrites on the way
; past: a caption outlives the paint that put it up.
; -----------------------------------------------------------------------------
hd_mount_says:
    push ax
    push si
    push di
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
    mov word [hd_msg], hd_cap
    pop di
    pop si
    pop ax
    ret

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
    pop cx
    pop ax
    stc
    ret
.free:
    mov [bx+HDV_VOLH], al
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_unmount - take the selected device's volume away
; in:  nothing; the lock is HELD
; out: nothing
; -----------------------------------------------------------------------------
hd_unmount:
    push ax
    push bx
    push cx
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, [hd_sel]
    jne .next
    call hd_unmount_row         ; EVERY volume this device has, not the first:
.next:                          ; Mount put up one icon per FAT partition and
    add bx, HDV_SIZE            ; Unmount is the other half of that
    loop .scan
    mov word [hd_msg], hd_s_unmounted
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_unmount_row - drop one volume and its claim (module internal)
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
    mov dx, [bx+HDV_LSEG]
    or dx, dx
    jz .noseg
    call OSAPI_MEM_FREE
.noseg:
    mov byte [bx+HDV_USED], 0
    mov word [bx+HDV_LSEG], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_vol_of - which kernel volume is mounted on one partition (SPEC.md 52.11)
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
    push cx
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, dl
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, dh
    jne .next
    mov al, [bx+HDV_VOL]
    jmp short .out
.next:
    add bx, HDV_SIZE
    loop .scan
    mov al, 0xFF
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_unmount_part - drop every volume mounted on one partition (SPEC.md 52.11)
; in:  AL = the device index, AH = the partition slot
; out: AL = 1 if anything was unmounted, 0 if not
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
    push cx
    push dx
    mov dx, ax                  ; DL = device, DH = slot
    xor al, al
    mov ah, 0xFF                ; ...and AH = the last volume index dropped,
    push ax                     ; 0xFF = none. The answer, built on the stack
    mov cx, HD_MAXVOL
    mov bx, hd_vols
.scan:
    cmp byte [bx+HDV_USED], 0
    je .next
    mov al, [bx+HDV_DEV]
    cmp al, dl
    jne .next
    mov al, [bx+HDV_PART]
    cmp al, dh
    jne .next
    pop ax                      ; the answer FIRST, because the pop would
    mov al, 1                   ; clobber AH, and the row is still live here:
    mov ah, [bx+HDV_VOL]        ; SPEC.md 52.10.8 needs WHICH volume went and
    push ax                     ; not only that one did
    call hd_unmount_row
.next:
    add bx, HDV_SIZE
    loop .scan
    pop ax
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_win_close_all - hide the tool window (detach's obligation)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hd_win_close_all:
    call hd_tool_drop           ; HDT_SHUT and then the free, in that order. A
    ret                         ; window of the TOOL's left on screen is a paint
                                ; through freed memory twice over - the kernel
                                ; releases this image the moment we return, and
                                ; we release the tool's before that

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
; DX:BX (SPEC.md 51.8).
; -----------------------------------------------------------------------------
hd_raw:
    mov [hd_lba], ax
    mov [hd_lba+2], dx
    mov [hd_bofs], bx
    mov [hd_bcnt], cx
    push ax
    mov ax, es
    mov [hd_bseg], ax
    pop ax
    push ax
    mov al, [hd_rawop]
    mov [hd_bop], al
    pop ax
hd_raw_op:
    push ax
    cmp byte [di+HDD_KIND], HDK_IDE
    je .ide
    call hd_bios_xfer
    jmp short .out
.ide:
    call hd_ide_xfer
.out:
    pop ax
    ret

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
