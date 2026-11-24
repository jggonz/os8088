; =============================================================================
; os8088 - drivers/joy/joyui.inc - THE JOYSTICK PAGE (SPEC.md 137.6)
;
; The Control Panel page JOYSTICK.DRV publishes: a live test view of the
; sticks, the controller type, the dead zone, Invert Y, Swap A/B, a
; three-step calibration drawn inside the page (Windows 3.1's order: upper
; left, lower right, centre, each confirmed with a stick button), Reset, and
; a status line whose read time is MEASURED with the PIT around the sample.
; Assembled INTO the driver's image (joy.asm, `%define JOY_UI`), so it reads
; the driver's state as near data and calls its routines as near procs - the
; names it may use are joypkg.inc's second half (SPEC.md 137.11).
;
; ONE ENGINE, FOUR LAYOUTS (SPEC.md 137.6.1). The page is a list of WIDGETS -
; a 12-byte record each: a type, an argument, a pane-relative rect and the
; state the glass is SHOWING. A layout (compact or wide, normal or
; calibrating) is a table of widget descriptors whose coordinates are
; ANCHORED to numbers derived once from the pane's size (CX/BX on the paint,
; SPEC.md 31.9.4), so nothing here assumes 640 pixels. Then:
;
;   paint   lays the page's ground once and draws every widget
;   tick    takes one timed sample and redraws ONLY the widgets whose state
;           changed - the dot, a number, a lamp (CLAUDE.md, performance
;           rules 1-2): each widget answers its state as one word and the
;           engine compares it with the word the glass is showing
;   click   press / drag / release on the same control (SPEC.md 13.8.4),
;           which changes the driver's state; the same update pass draws it
;
; THE DOT NEVER LEAVES A HOLE. Moving it erases the old dot by drawing what
; was UNDER it - the crosshair and the dashed dead-zone square, composed into
; one 16-wide masked record (OSAPI_ICON_DRAW) whose mask leaves out the new
; dot's pixels - and then fills the new dot: each pixel is written once
; (SPEC.md 137.6.4).
;
; NOTHING HERE TOUCHES THE PORT except through the driver's own joy_take, and
; nothing here changes the sample/verb code (core's, SPEC.md 137.3).
; =============================================================================

JUI_DOT     equ 4               ; the stick dot, a square this many px
JUI_NW      equ 48              ; widgets, at most (the longest layout is 47:
                                ; the wide test view of a CH FlightStick Pro)
JUI_CALMIN  equ 16              ; a calibrated side narrower than this many
                                ; PIT counts was not moved: it keeps the
                                ; driver's own default (SPEC.md 137.6.5)
JUI_TXTMAX  equ 82              ; the longest text run: 637 px is 79 cells
JUI_WIDE    equ 300             ; a pane at least this wide is the wide face

; --- a widget (JW_*), 12 bytes --------------------------------------------------
JW_T        equ 0               ; db  WT_*
JW_A        equ 1               ; db  its argument (a stick, a source, a button)
JW_X        equ 2               ; dw  pane-relative x
JW_Y        equ 4               ; dw  ...and y
JW_W        equ 6               ; dw  width in px (a text field: its width,
                                ;     0 = exactly the string)
JW_H        equ 8               ; dw  height in px - or, for the three text
                                ;     types, the colours: ink | paper << 8
JW_S        equ 10              ; dw  WHAT THE GLASS IS SHOWING (the TEXT type:
                                ;     its string, which never changes)
JW_SZ       equ 12

; --- the widget types: the three text types FIRST (their JW_H is colours, so
; the layout does not resolve it as an anchored height) ------------------------
WT_TEXT     equ 0               ; a fixed string
WT_NUM      equ 1               ; a number from a source (JS_*), right-aligned
WT_DTXT     equ 2               ; a string from a source (JD_*)
WT_RECT     equ 3               ; a filled rect, the colour in JW_A
WT_GROUP    equ 4               ; a framed group with a caption strip (JW_S)
WT_BOX      equ 5               ; a stick's position box: A, B, or 2 = the raw
                                ; view while calibrating
WT_LAMP     equ 6               ; a button's lamp and its digit
WT_BAR      equ 7               ; throttle (0) or rudder (1), a vertical bar
WT_HAT      equ 8               ; the hat, a 3 x 3 lamp grid
WT_BTN      equ 9               ; a push button, JW_A = a JA_* action
WT_RADIO    equ 10              ; a controller type, JW_A = the JOYT_*
WT_CHECK    equ 11              ; Invert (even JW_A) or Swap (odd)
WT_STEP     equ 12              ; a calibration step's progress item, 1..3
WT_N        equ 13

; --- the actions a button names (JA_*) ------------------------------------------
JA_CAL      equ 0
JA_RESET    equ 1
JA_DZM      equ 2
JA_DZP      equ 3
JA_TYPE     equ 4               ; the compact face's type cycle
JA_CANCEL   equ 5
JA_BACK     equ 6

; --- the number sources (JS_*) --------------------------------------------------
JS_AX       equ 0               ; logical stick A X, B X... calibrated (0-3)
JS_RAW      equ 4               ; ...the raw count behind logical axis 0-3
JS_THR      equ 8               ; throttle, percent
JS_RUD      equ 9               ; rudder, signed
JS_DZ       equ 10              ; the dead zone, percent
JS_TIME     equ 11              ; the measured read time, wide format
JS_TIMEC    equ 12              ; ...compact format
JS_PRAW     equ 13              ; PORT axis 0-3's raw count (calibrating)
JS_PMIN     equ 17              ; ...the least seen while calibrating
JS_PMAX     equ 21              ; ...the most
JS_NONE     equ 0x8000          ; a source's "nothing to show"

; --- the string sources (JD_*) ---------------------------------------------------
JD_STATUS   equ 0
JD_TYPE     equ 1               ; the compact face's type name
JD_HAT      equ 2               ; the hat's direction, N NE .. or --
JD_BAN1     equ 3               ; the calibration banner's two lines
JD_BAN2     equ 4

; --- the anchors a descriptor's coordinates may name (SPEC.md 137.6.1) -----------
; AN(a, o) is anchor a plus a signed 12-bit offset; anchor 0 is zero, so a
; plain small number is itself
%define AN(a,o) ((((a) & 15) << 12) | ((o) & 0x0FFF))
JN_W        equ 1               ; the pane's width
JN_H        equ 2               ; ...its height
JN_MID      equ 3               ; W / 2
JN_SIDE     equ 4               ; the stick box's side
JN_BXA      equ 5               ; stick A's box x (calibrating: the table's x)
JN_SETW     equ 6               ; the settings group's width
JN_BBOT     equ 7               ; the row under the boxes (box top + side)
JN_LOW      equ 8               ; the lower groups' top
JN_SPL      equ 9               ; the controller / settings split
JN_BXB      equ 10              ; stick B's box x
JN_SBOT     equ 11              ; the stick groups' bottom
JN_C1       equ 12              ; stick B's group: throttle bar x (cal: W/3)
JN_C2       equ 13              ; ...rudder bar x (cal: 2W/3)
JN_C3       equ 14              ; ...the hat's x
JN_BMID     equ 15              ; the boxes' middle row
JUI_BTOP    equ 22              ; the wide stick boxes' top

; a descriptor is a widget record with its x, y, w (and h, past the text
; types) anchored
%macro JWD 6                    ; type, arg, x, y, w, h - a TEXT's h is its
    db %1, %2                   ; string, a GROUP's arg its caption (jui_gcap)
    dw %3, %4, %5, %6
%endmacro
JD_SZ       equ 10              ; a descriptor
%define CK(i,p) ((i) | ((p) << 8))  ; text colours: ink, paper
JC_TEXT     equ CK(CBLACK, CWHITE)
JC_BAN      equ CK(CWHITE, CBLUE)

; =============================================================================
; THE SEVEN CELLS (joy.asm's service table)
; =============================================================================
joyui_name: db 'Joystick', 0

; -----------------------------------------------------------------------------
; joyui_paint - DSV_CPPAINT: DI/DX = the pane's left/top, CX/BX = its size,
; SI = the panel's window; the gfx lock HELD. Registers are the kernel's to
; bank (cp_drv_ev), so nothing here keeps one
; -----------------------------------------------------------------------------
joyui_paint:
    call jui_bank
    call jui_sample
    call jui_layout
    jmp jui_full

; -----------------------------------------------------------------------------
; joyui_tick - DSV_CPTICK, ~9 Hz while the page shows (SPEC.md 31.9.4): the
; paint's registers. One timed sample, a calibration step if a stick button
; went down, and the widgets whose state moved
; -----------------------------------------------------------------------------
joyui_tick:
    call jui_bank               ; CF = 1: a size this layout was not made for
    jc jui_repaint              ; (the face changed under us): paint whole
    cmp word [jui_nw], 0
    je jui_repaint
    call jui_sample
    call jui_edge               ; CF = 1: it advanced and painted
    jc jui_ret
    jmp jui_update
jui_repaint:
    call jui_sample
    call jui_layout
    jmp jui_full

; -----------------------------------------------------------------------------
; joyui_click - DSV_CPCLICK, the PRESS: arm the control under it and draw it
; held, nothing else (SPEC.md 13.8.4). CX/BX = the pane-relative point
; -----------------------------------------------------------------------------
joyui_click:
    call jui_org
    call jui_hit                ; AX = the widget, 0 = none
    call os88ui_arm
    mov [jui_down], ax
    jmp jui_update

; joyui_drag - DSV_CPDRAG: the held look follows the pointer on and off it
joyui_drag:
    call jui_org
    call jui_hit
    xchg cx, ax                 ; CX = under the pointer
    call os88ui_armed           ; AX = what the press armed (peek)
    cmp ax, cx
    je .on
    xor ax, ax
.on:
    cmp ax, [jui_down]
    je jui_ret
    mov [jui_down], ax
    jmp jui_update

; joyui_up - DSV_CPUP, the RELEASE: on the control it pressed, act
joyui_up:
    call jui_org
    call jui_hit
    xchg cx, ax
    call os88ui_fire            ; AX = armed, and cleared
    mov word [jui_down], 0
    or ax, ax
    jz jui_update
    cmp ax, cx
    jne jui_update              ; slid off it: a cancel
    xchg si, ax
    call jui_act                ; CF = 1: it painted the whole page
    jnc jui_update
jui_ret:
    ret

; -----------------------------------------------------------------------------
; joyui_key - DSV_CPKEY: BL = ASCII, BH = scan. While calibrating, Enter or
; Space confirms a step (as a stick button does) and Esc cancels; every other
; key belongs to nobody. out CF = 0 taken
; -----------------------------------------------------------------------------
joyui_key:
    cmp byte [jui_step], 0
    je .no
    mov [jui_px], di
    mov [jui_py], dx
    cmp bl, 27
    je .esc
    cmp bl, 13
    je .adv
    cmp bl, ' '
    jne .no
.adv:
    call jui_adv
    clc
    ret
.esc:
    call jui_a_cancel
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; joyui_close - DSV_CPCLOSE (SPEC.md 31.9.1): a calibration left half done is
; dropped - nothing of it reached joy_cal - and the next open lays out afresh
; -----------------------------------------------------------------------------
joyui_close:
    mov byte [jui_step], 0
    mov word [jui_down], 0
    mov word [jui_nw], 0
    mov word [jui_w], 0
    ret

; =============================================================================
; THE ENGINE
; =============================================================================

; jui_bank - bank a paint's or tick's origin and size. out CF = 1 the size is
; not the one the layout was made for
jui_bank:
    mov [jui_px], di
    mov [jui_py], dx
    mov [jui_win], si
    cmp cx, [jui_w]
    jne .new
    cmp bx, [jui_h]
    je .same                    ; (CF = 0)
.new:
    mov [jui_w], cx
    mov [jui_h], bx
    stc
.same:
    ret

; jui_org - a click's origin, and its point in CX/DX (it arrives in CX/BX)
jui_org:
    mov [jui_px], di
    mov [jui_py], dx
    mov dx, bx
    ret

; jui_full - lay the whole pane's ground once, then draw every widget. A wide
; page owns every pixel of its area (SPEC.md 135.3.1)
jui_full:
    mov al, CWHITE
    call jui_ink
    xor ax, ax
    xor bx, bx
    mov cx, [jui_w]
    dec cx
    mov dx, [jui_h]
    dec dx
    call jui_fill
    mov bx, jui_wl
    mov al, 1
    jmp short jui_pass

; jui_update - every widget whose state moved, and only those
jui_update:
    mov bx, jui_wl
    xor al, al
                                ; ...and into jui_pass
; -----------------------------------------------------------------------------
; jui_pass - BX = the first widget, AL = 1 draw them all, 0 only the changed
; -----------------------------------------------------------------------------
jui_pass:
    mov [jui_force], al
.l:
    mov ax, [jui_nw]
    mov cx, JW_SZ
    mul cx
    add ax, jui_wl
    cmp bx, ax
    jae .r
    push bx
    mov si, bx
    call jui_one
    pop bx
    add bx, JW_SZ
    jmp short .l
.r:
    ret

; jui_one - SI = a widget: ask its state; draw it if forced or if it moved
jui_one:
    mov [jui_cur], si
    mov bl, [si+JW_T]
    xor bh, bh
    shl bx, 1
    push bx
    call [jui_tstate+bx]        ; AX = the state, SI kept
    pop bx
    mov dx, [si+JW_S]           ; DX = what the glass shows
    cmp byte [jui_force], 0
    jne .draw
    cmp ax, dx
    je .r
.draw:
    mov [si+JW_S], ax
    jmp [jui_tdraw+bx]          ; SI = widget, AX = new, DX = old
.r:
    ret

jui_tstate:
    dw jui_s_const, jui_s_num, jui_s_dtxt, jui_s_const, jui_s_const
    dw jui_s_box, jui_s_lamp, jui_s_bar, jui_s_hat, jui_s_btn
    dw jui_s_radio, jui_s_check, jui_s_step
jui_tdraw:
    dw jui_d_text, jui_d_num, jui_d_dtxt, jui_d_rect, jui_d_group
    dw jui_d_box, jui_d_lamp, jui_d_bar, jui_d_hat, jui_d_btn
    dw jui_d_radio, jui_d_check, jui_d_step
%if ($ - jui_tdraw) != 2 * WT_N || (jui_tdraw - jui_tstate) != 2 * WT_N
  %error "joyui: one state and one draw proc per WT_*"
%endif

; jui_inval - AL = a type: every widget of it draws whole on the next pass
jui_inval:
    mov bx, jui_wl
    mov cx, [jui_nw]
    jcxz .r
.l:
    cmp [bx+JW_T], al
    jne .n
    mov word [bx+JW_S], 0xFFFE
.n:
    add bx, JW_SZ
    loop .l
.r:
    ret

; -----------------------------------------------------------------------------
; jui_hit - CX/DX = a pane-relative point -> AX = the LIVE control under it
; (a button whose predicate passes, a radio, a check box), 0 = none
; -----------------------------------------------------------------------------
jui_hit:
    push si
    mov si, jui_wl
    mov bx, [jui_nw]
.l:
    or bx, bx
    jz .none
    mov al, [si+JW_T]
    cmp al, WT_BTN
    jb .n
    cmp al, WT_CHECK
    ja .n
    mov ax, [si+JW_X]
    cmp cx, ax
    jl .n
    add ax, [si+JW_W]
    cmp cx, ax
    jge .n
    mov ax, [si+JW_Y]
    cmp dx, ax
    jl .n
    add ax, [si+JW_H]
    cmp dx, ax
    jge .n
    cmp byte [si+JW_T], WT_BTN
    jne .yes
    push bx
    push cx
    push dx
    call jui_pred               ; CF = 1 greyed: not a target
    pop dx
    pop cx
    pop bx
    jnc .yes
.n:
    add si, JW_SZ
    dec bx
    jmp short .l
.none:
    xor si, si
.yes:
    xchg ax, si
    pop si
    ret

; jui_pred - SI = a button widget: CF = its action's predicate (1 greyed)
jui_pred:
    mov bl, [si+JW_A]
    xor bh, bh
    shl bx, 1
    jmp [jui_apred+bx]

; -----------------------------------------------------------------------------
; jui_act - SI = the control released on. out CF = 1 the page was painted
; whole (a mode change), CF = 0 the update pass draws what moved
; -----------------------------------------------------------------------------
jui_act:
    mov al, [si+JW_T]
    mov bl, [si+JW_A]
    cmp al, WT_RADIO
    jne .nr
    mov al, bl
    jmp jui_settype
.nr:
    cmp al, WT_CHECK
    jne .btn
    mov al, JOYF_INVY
    test bl, 1
    jz .fl
    mov al, JOYF_SWAP
.fl:
    xor [joy_blob+JOYB_FLAGS], al
    call joy_apply
    cmp al, JOYF_SWAP
    jne .nsw
    call joy_probe              ; the other side of the port: look again
.nsw:
    call jui_cfgw
    clc
    ret
.btn:
    call jui_pred
    jc .no                      ; greyed since the press: refused
    mov bl, [si+JW_A]
    xor bh, bh
    shl bx, 1
    jmp [jui_aact+bx]
.no:
    clc
    ret

; the buttons: label, predicate and action, in JA_* order
jui_albl:  dw jui_t_cal, jui_t_reset, jui_t_minus, jui_t_plus, jui_t_gt
           dw jui_t_cancel, jui_t_back
jui_apred: dw jui_p_cal, jui_p_reset, jui_p_dzm, jui_p_dzp, jui_p_live
           dw jui_p_live, jui_p_back
jui_aact:  dw jui_a_cal, jui_a_reset, jui_a_dzm, jui_a_dzp, jui_a_type
           dw jui_a_cancel, jui_a_back

; --- the predicates (SPEC.md 47: one test for the grey, the refusal and the
; act; a FACT, never a guess). CF = 1 greyed. Every register but AX kept ----
jui_p_cal:                      ; nothing answering: nothing to calibrate
    mov al, [joy_live]
    and al, [joy_tax]
    jnz jui_p_live
    stc
    ret
jui_p_reset:                    ; nothing calibrated: nothing to forget
    call jui_uncal              ; AL = calibrated axes of the type
    or al, al
    jnz jui_p_live
    stc
    ret
jui_p_dzm:
    cmp byte [joy_blob+JOYB_DZ], 1
    ret                         ; CF = 1 at 0
jui_p_dzp:
    cmp byte [joy_blob+JOYB_DZ], JOY_DZMAX
    cmc                         ; CF = 1 at the ceiling
    ret
jui_p_back:
    cmp byte [jui_step], 2
    ret                         ; CF = 1 on step 1
jui_p_live:
    clc
    ret

; jui_uncal - AL = the type's port axes whose blob centre is set (calibrated),
; AH = the type's axes. Nothing else
jui_uncal:
    push bx
    push cx
    mov ah, [joy_tax]
    xor al, al
    mov bx, JOYB_CAL + 3 * JOYC_SZ
    mov cl, 8
.l:
    cmp word [joy_blob+bx+JOYC_CEN], 0
    je .n
    or al, cl
.n:
    sub bx, JOYC_SZ
    shr cl, 1
    jnz .l
    and al, ah
    pop cx
    pop bx
    ret

; --- the actions --------------------------------------------------------------
jui_a_cal:                      ; enter the calibration (SPEC.md 137.6.5)
    mov byte [jui_step], 1
    mov bx, 6
.c:
    mov word [jui_cmin+bx], 0xFFFF
    mov word [jui_cmax+bx], 0
    mov word [jui_ccen+bx], JOY_NONE
    dec bx
    dec bx
    jns .c
    mov al, [joy_btn]
    mov [jui_bprev], al         ; a button already held is not a confirm
    jmp short jui_relay

jui_a_cancel:
    mov byte [jui_step], 0
jui_relay:
    call jui_layout
    call jui_full
    stc
    ret

jui_a_back:
    dec byte [jui_step]
    clc
    ret

jui_a_reset:
    call joy_reset              ; every centre forgotten; the next sample
    call jui_cfgw               ; re-centres (SPEC.md 137.3)
    jmp short jui_dzchg

jui_a_dzm:
    dec byte [joy_blob+JOYB_DZ]
    jmp short jui_dz
jui_a_dzp:
    inc byte [joy_blob+JOYB_DZ]
jui_dz:
    call joy_apply
    call jui_cfgw
jui_dzchg:
    mov al, WT_BOX              ; the dead-zone square moved: the boxes whole
    call jui_inval
    clc
    ret

jui_a_type:                     ; the compact face's cycle
    mov al, [joy_blob+JOYB_TYPE]
    inc ax
    cmp al, JOYT_MAX
    jbe jui_settype
    xor al, al
                                ; ...and into jui_settype
; -----------------------------------------------------------------------------
; jui_settype - AL = a JOYT_*: the driver takes it, looks at the port again,
; the blob is written, and the type's own widgets (stick B's group, the
; compact B line) are laid out and drawn in place. out CF = 0
; -----------------------------------------------------------------------------
jui_settype:
    cmp al, [joy_blob+JOYB_TYPE]
    je .same
    mov [joy_blob+JOYB_TYPE], al
    call joy_apply
    call joy_probe
    call jui_cfgw
    mov ax, [jui_ntail]
    mov [jui_nw], ax
    call jui_tail               ; the new type's widgets...
    mov al, CWHITE
    call jui_ink
    mov ax, [jui_trect]         ; ...over their own ground...
    mov bx, [jui_trect+2]
    mov cx, [jui_trect+4]
    mov dx, [jui_trect+6]
    call jui_fill
    mov ax, [jui_ntail]         ; ...drawn whole
    mov cx, JW_SZ
    mul cx
    add ax, jui_wl
    xchg bx, ax
    mov al, 1
    call jui_pass
.same:
    clc
    ret

; jui_cfgw - joy_blob to the kernel, as it stands: a SETTING (type, flags, dead
; zone, a reset). Not joy_save, which would also write the runtime centres an
; uncalibrated stick took from its first sample as if they were a calibration.
; SYSTEM.CFG gets it at the panel's close (SPEC.md 31.8)
jui_cfgw:
    push es
    push ds
    pop es
    mov si, joy_blob
    mov cx, JOYB_SZ
    mov al, 1
    call OSAPI_DRV_CFG
    pop es
    ret

; =============================================================================
; THE SAMPLE AND THE CALIBRATION
; =============================================================================

; -----------------------------------------------------------------------------
; jui_sample - joy_take, timed: the PIT is latched before and after with
; interrupts off throughout (joy_take holds them off itself), so the
; difference is the read's own cost in 838 ns units, exact under 55 ms. It is
; shown at most once a second (every ninth tick), in tenths of a ms. While
; calibrating, the captured range grows by this sample
; -----------------------------------------------------------------------------
jui_sample:
    pushf
    cli
    call joy_pit
    xchg bx, ax
    call joy_take
    call joy_pit
    popf
    sub bx, ax                  ; the channel counts DOWN
    inc byte [jui_tn]
    cmp byte [jui_tn], 9
    jae .pub
    cmp word [jui_tshow], 0
    jne .cal
.pub:
    mov byte [jui_tn], 0
    xchg ax, bx
    mov cx, 10
    mul cx
    add ax, 596                 ; (counts * 10 + 596) / 1193: tenths of a
    adc dx, 0                   ; ms, rounded (1,193.18 counts a ms)
    mov cx, 1193
    div cx
    or ax, ax
    jnz .s
    inc ax
.s:
    mov [jui_tshow], ax
.cal:
    cmp byte [jui_step], 0
    je .r
    mov bx, 6
.ax:
    mov ax, [joy_raw+bx]
    cmp ax, JOY_NONE
    je .n
    cmp ax, [jui_cmin+bx]
    jae .lo
    mov [jui_cmin+bx], ax
.lo:
    cmp ax, [jui_cmax+bx]
    jbe .n
    mov [jui_cmax+bx], ax
.n:
    dec bx
    dec bx
    jns .ax
.r:
    ret

; jui_edge - a stick button that went down since the last sample confirms the
; step (edge-detected here, SPEC.md 137.6.5). out CF = 1 it painted whole
jui_edge:
    mov al, [joy_btn]
    mov ah, [jui_bprev]
    mov [jui_bprev], al
    cmp byte [jui_step], 0
    je .no
    not ah
    and al, ah
    jz .no
    call jui_adv
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; jui_adv - confirm this step: on to the next, or at the centre, finish -
; the captured range and centres into joy_cal, joy_prep, joy_save - and back
; to the test view
; -----------------------------------------------------------------------------
jui_adv:
    mov al, [jui_step]
    cmp al, 3
    jae .fin
    inc byte [jui_step]
    jmp jui_update
.fin:
    mov si, joy_raw             ; the centre: this sample, every axis
    mov di, jui_ccen
    mov cx, 4
    push es                     ; (ES is the kernel's in a callback: banked)
    push ds
    pop es
    cld
    rep movsw
    pop es
    call jui_calfin
    mov byte [jui_step], 0
    call jui_layout
    jmp jui_full

; jui_calfin - write the type's port axes' three points (SPEC.md 137.6.5)
jui_calfin:
    call joy_apply              ; joy_cal back to the blob's: runtime centres
                                ; of the axes NOT calibrated here are dropped
    mov si, joy_cal
    xor bx, bx                  ; BX = the port axis * 2
    mov dl, 1                   ; DL = its JOYA_* bit
.ax:
    test [joy_tax], dl
    jz .nx
    mov ax, [jui_ccen+bx]
    cmp ax, JOY_NONE
    je .nx
    or ax, ax
    jz .nx
    mov cx, [jui_cmin+bx]
    mov di, [jui_cmax+bx]
    call jui_isthr              ; the throttle has no rest position: its
    jne .cen                    ; centre is the middle of its travel
    cmp cx, di
    jae .cen
    mov ax, cx
    add ax, di
    rcr ax, 1
.cen:
    push dx
    mov dx, ax                  ; the low side: moved at least JUI_CALMIN,
    sub dx, JUI_CALMIN          ; or the driver's own default, 0
    jb .z
    cmp cx, dx
    jbe .lo
.z:
    xor cx, cx
.lo:
    mov dx, ax                  ; the high side, or twice the centre
    add dx, JUI_CALMIN
    cmp di, dx
    jae .hi
    mov di, ax
    shl di, 1
.hi:
    pop dx
    mov [si+JCAL_MIN], cx
    mov [si+JCAL_CEN], ax
    mov [si+JCAL_MAX], di
    call joy_prep
.nx:
    add si, JCAL_SZ
    inc bx
    inc bx
    shl dl, 1
    cmp dl, 16
    jb .ax
    jmp joy_save

; jui_isthr - BX = a port axis * 2: ZF = 1 it is the throttle (Y2, or Y1
; under Swap) of a flight type. AX kept
jui_isthr:
    push ax
    mov al, [joy_blob+JOYB_TYPE]
    cmp al, JOYT_FLIGHT
    je .fl
    cmp al, JOYT_CHFS
    jne .r                      ; (ZF = 0)
.fl:
    mov al, 6                   ; Y2...
    test byte [joy_blob+JOYB_FLAGS], JOYF_SWAP
    jz .c
    mov al, 2                   ; ...or Y1 swapped
.c:
    cmp bl, al
.r:
    pop ax
    ret

; =============================================================================
; LAYOUT (SPEC.md 137.6.1)
; =============================================================================

; -----------------------------------------------------------------------------
; jui_layout - the face (from the banked width), the colours (from the
; display the panel is on), the anchors, and the widget list
; -----------------------------------------------------------------------------
jui_layout:
    mov bx, [jui_win]           ; --- mono or colour (SPEC.md 39.4)
    or bx, bx
    jz .vid
    call OSAPI_WM_DISPLAY
    jmp short .dh
.vid:
    call OSAPI_VIDEO
.dh:
    xor al, al
    cmp dh, 4
    jae .col
    inc ax
.col:
    mov [jui_mono], al
    mov ax, (CBLUE << 8) | CDGRAY   ; the lines, the dot and the bars
    cmp byte [jui_mono], 0
    je .cs
    mov ax, (CBLACK << 8) | CBLACK
.cs:
    mov [jui_lcol], ax
    mov byte [jui_icon], 1
    mov byte [jui_icon+1], JUI_DOT
    mov word [jui_down], 0
    mov word [jui_nw], 0
    xor ax, ax
    mov [jui_anch], ax
    mov ax, [jui_w]
    mov [jui_anch+2*JN_W], ax
    shr ax, 1
    mov [jui_anch+2*JN_MID], ax
    mov ax, [jui_h]
    mov [jui_anch+2*JN_H], ax
    mov si, jui_l_cn            ; compact, normal
    mov cx, (jui_l_cn_e - jui_l_cn) / JD_SZ
    cmp word [jui_w], JUI_WIDE
    jb .compact
    cmp byte [jui_step], 0
    jne .wcal
    call jui_anwide
    mov si, jui_l_wn
    mov cx, (jui_l_wn_e - jui_l_wn) / JD_SZ
    jmp short .emit
.wcal:
    call jui_ancal
    mov si, jui_l_wc
    mov cx, (jui_l_wc_e - jui_l_wc) / JD_SZ
    jmp short .emit
.compact:
    cmp byte [jui_step], 0
    je .emit
    mov si, jui_l_cc
    mov cx, (jui_l_cc_e - jui_l_cc) / JD_SZ
.emit:
    call jui_emit
    mov ax, [jui_nw]
    mov [jui_ntail], ax
    ; ...and into jui_tail

; -----------------------------------------------------------------------------
; jui_tail - the widgets that depend on the controller type (the test view's
; stick B group, the compact B line) or on its axes (the calibration's table
; rows), appended from [jui_nw]; and the rect a type change clears
; -----------------------------------------------------------------------------
jui_tail:
    cmp byte [jui_step], 0
    jne jui_rows
    mov bl, [joy_blob+JOYB_TYPE]
    xor bh, bh
    shl bx, 1
    shl bx, 1                   ; 4 bytes a type: table, end
    cmp word [jui_w], JUI_WIDE
    jb .c
    mov si, [jui_tw_tab+bx]
    mov cx, [jui_tw_tab+bx+2]
    sub cx, si
    call jui_emitn
    mov si, jui_l_wt            ; stick B's lamps, on every type
    mov cx, (jui_l_wt_e - jui_l_wt) / JD_SZ
    call jui_emit
    mov ax, [jui_anch+2*JN_MID] ; the group's inside, below its caption:
    mov cx, ax                  ; x MID + 5 .. 2 * MID - 6 (an odd width
    shl cx, 1                   ; leaves its last column outside the group)
    sub cx, 6
    add ax, 5
    mov bx, 17
    mov dx, [jui_anch+2*JN_SBOT]
    sub dx, 2
    jmp short .rect
.c:
    mov si, [jui_tc_tab+bx]
    mov cx, [jui_tc_tab+bx+2]
    sub cx, si
    call jui_emitn
    mov ax, 58                  ; the compact B line
    mov bx, 36
    mov cx, 219
    mov dx, 43
.rect:
    mov [jui_trect], ax
    mov [jui_trect+2], bx
    mov [jui_trect+4], cx
    mov [jui_trect+6], dx
    ret

; jui_rows - the calibration table: one row per port axis the type uses, a
; label, the raw count now, the least and the most seen
jui_rows:
    mov bx, 80                  ; the wide table's first row, and its pitch
    mov dx, 12
    mov si, jui_r_w
    cmp word [jui_w], JUI_WIDE
    jae .go
    mov bx, 34
    mov dx, 10
    mov si, jui_r_c
.go:
    xor cx, cx                  ; CL = the port axis, CH = rows so far
.ax:
    mov al, 1
    shl al, cl
    test [joy_tax], al
    jz .nx
    push bx
    push cx
    push dx
    push si
    mov di, [jui_nw]
    push cx
    mov cx, 4
    call jui_emit
    pop cx
    mov ax, JW_SZ               ; DI = the first of the four just made
    mul di
    xchg di, ax
    add di, jui_wl
    pop si
    pop dx
    pop cx
    pop bx
    push dx
    mov al, ch                  ; y = the first row + rows * pitch
    mul dl
    add ax, bx
    mov [di+JW_Y], ax
    mov [di+JW_SZ+JW_Y], ax
    mov [di+2*JW_SZ+JW_Y], ax
    mov [di+3*JW_SZ+JW_Y], ax
    mov al, cl
    add [di+JW_SZ+JW_A], al     ; the three sources, by axis
    add [di+2*JW_SZ+JW_A], al
    add [di+3*JW_SZ+JW_A], al
    mov al, cl
    cbw
    shl ax, 1
    xchg si, ax
    mov si, [jui_axn+si]        ; the label, by axis
    mov [di+JW_S], si
    xchg si, ax
    pop dx
    inc ch
.nx:
    inc cl
    cmp cl, 4
    jb .ax
    ret

; jui_emitn - SI = descriptors, CX = their length in BYTES
jui_emitn:
    xchg ax, cx
    xor dx, dx
    mov cx, JD_SZ
    div cx
    xchg cx, ax
                                ; ...and into jui_emit
; -----------------------------------------------------------------------------
; jui_emit - SI = descriptors, CX = how many: resolve each one's anchors and
; append it at [jui_nw] (a full list drops the rest rather than overrun)
; -----------------------------------------------------------------------------
jui_emit:
    jcxz .r
    mov ax, [jui_nw]
    cmp ax, JUI_NW
    jae .r
    push cx
    mov cx, JW_SZ
    mul cx
    pop cx
    add ax, jui_wl
    xchg di, ax
.d:
    cmp word [jui_nw], JUI_NW
    jae .r
    push cx
    mov ax, [si]
    mov [di], ax                ; type and argument
    mov ax, [si+JW_X]
    call jui_res
    mov [di+JW_X], ax
    mov ax, [si+JW_Y]
    call jui_res
    mov [di+JW_Y], ax
    mov ax, [si+JW_W]
    call jui_res
    mov [di+JW_W], ax
    mov ax, [si+JW_H]
    xor bx, bx                  ; BX = the shown state: 0, or a TEXT's string
    mov cl, [si+JW_T]
    cmp cl, WT_RECT
    jae .res
    or cl, cl                   ; a text type: JW_H becomes its colours, and
    jnz .col                    ; a TEXT's descriptor h was its string
    xchg bx, ax
.col:
    mov ax, JC_TEXT
    cmp cl, WT_DTXT
    jne .h
    cmp byte [si+JW_A], JD_BAN1
    jb .h
    mov ax, JC_BAN
    jmp short .h
.res:
    call jui_res
.h:
    mov [di+JW_H], ax
    mov [di+JW_S], bx
    add si, JD_SZ
    add di, JW_SZ
    inc word [jui_nw]
    pop cx
    loop .d
.r:
    ret

; jui_res - AX = an anchored number -> its value. Nothing else
jui_res:
    push bx
    push cx
    mov bx, ax
    mov cl, 12
    shr bx, cl
    shl bx, 1
    and ax, 0x0FFF
    test ah, 0x08
    jz .p
    or ax, 0xF000
.p:
    add ax, [jui_anch+bx]
    pop cx
    pop bx
    ret

; jui_setan - AX = the value of anchor BX (an index)
jui_setan:
    shl bx, 1
    mov [jui_anch+bx], ax
    ret

; -----------------------------------------------------------------------------
; jui_anwide - the wide test view's anchors, from W and H (SPEC.md 137.6.2)
; -----------------------------------------------------------------------------
jui_anwide:
    mov ax, [jui_h]             ; the lower groups: 118 tall, over the
    sub ax, 156                 ; buttons row and the status line
    mov bx, JN_LOW
    call jui_setan
    sub ax, 8
    mov bx, JN_SBOT
    call jui_setan
    mov ax, [jui_w]             ; the split: three fifths
    mov cx, 3
    mul cx
    mov cx, 5
    div cx
    mov bx, JN_SPL
    call jui_setan
    neg ax
    add ax, [jui_w]
    sub ax, 8
    mov bx, JN_SETW
    call jui_setan
    mov cx, [jui_w]             ; a stick group's width: W/2 - 8
    shr cx, 1
    sub cx, 8
    mov ax, [jui_h]             ; the box: square, as tall as the group
    sub ax, 214                 ; leaves room for, as wide as it is
    mov dx, cx
    sub dx, 16
    cmp ax, dx
    jle .s1
    xchg ax, dx
.s1:
    call jui_side
    mov bx, JN_SIDE
    call jui_setan
    push ax
    add ax, JUI_BTOP
    mov bx, JN_BBOT
    call jui_setan
    pop ax
    push ax
    shr ax, 1
    add ax, JUI_BTOP
    mov bx, JN_BMID
    call jui_setan
    pop ax
    neg ax                      ; the boxes' x: centred in each group
    add ax, cx
    shr ax, 1
    add ax, 4
    mov bx, JN_BXA
    call jui_setan
    add ax, [jui_anch+2*JN_MID]
    mov bx, JN_BXB
    call jui_setan
    mov ax, cx                  ; stick B's columns: a sixth, a half and
    xor dx, dx                  ; five sixths of its group
    mov bx, 6
    div bx
    mov dx, [jui_anch+2*JN_MID]
    add dx, 4                   ; DX = the group's x
    push ax
    add ax, dx
    sub ax, 8
    mov bx, JN_C1
    call jui_setan
    mov ax, cx
    shr ax, 1
    add ax, dx
    sub ax, 8
    mov bx, JN_C2
    call jui_setan
    pop ax
    neg ax
    add ax, cx
    add ax, dx
    sub ax, 14
    mov bx, JN_C3
    jmp jui_setan

; jui_side - AX = a wanted side -> held to 48..240 and even (the dot's state
; packs a position in a byte each way)
jui_side:
    cmp ax, 48
    jge .lo
    mov ax, 48
.lo:
    cmp ax, 240
    jbe .hi
    mov ax, 240
.hi:
    and al, 0xFE
    ret

; jui_ancal - the wide calibration view's anchors
jui_ancal:
    mov ax, [jui_h]
    sub ax, 150
    cmp ax, 200
    jle .s
    mov ax, 200
.s:
    call jui_side
    mov bx, JN_SIDE
    call jui_setan
    add ax, 36                  ; the table: right of the box
    mov bx, JN_BXA
    call jui_setan
    mov ax, [jui_w]             ; the progress items: thirds
    xor dx, dx
    mov cx, 3
    div cx
    mov bx, JN_C1
    call jui_setan
    shl ax, 1
    mov bx, JN_C2
    jmp jui_setan

; =============================================================================
; THE STATES - SI = the widget, out AX; SI kept
; =============================================================================
jui_s_const:
    mov ax, [si+JW_S]
    ret

jui_s_btn:                      ; bit 0 greyed, bit 1 held
    call jui_pred
    sbb ax, ax
    and ax, 1
    jmp short jui_held
jui_s_radio:                    ; bit 0 the type, bit 1 held
    mov al, [joy_blob+JOYB_TYPE]
    cmp al, [si+JW_A]
    jmp short jui_on
jui_s_check:
    mov al, JOYF_INVY
    test byte [si+JW_A], 1
    jz .f
    mov al, JOYF_SWAP
.f:
    test [joy_blob+JOYB_FLAGS], al
    jz jui_off
    cmp al, al
jui_on:
    mov ax, 0                   ; (flags kept)
    jne jui_held
    inc ax
    jmp short jui_held
jui_off:
    xor ax, ax
jui_held:
    cmp si, [jui_down]
    jne .r
    or al, 2
.r:
    ret

jui_s_step:                     ; 0 to come, 1 this one, 2 done
    mov al, [jui_step]
    sub al, [si+JW_A]           ; >0 done, 0 current, <0 to come
    mov ah, 0
    jz .cur
    js .r0
    mov al, 2
    ret
.cur:
    inc ax
    ret
.r0:
    xor ax, ax
    ret

jui_s_lamp:                     ; bit 0 held, bit 1 not a button of the type
    mov cl, [si+JW_A]
jui_lampst:                     ; (the hat's cells: CL = a button)
    xor ax, ax
    cmp cl, [joy_tnb]
    jb .have
    mov al, 2
    ret
.have:
    mov bl, 1
    shl bl, cl
    test [joy_st+JOYS_BTN], bl
    jz .r
    inc ax
.r:
    ret

jui_s_hat:
    mov al, [joy_st+JOYS_HAT]
    mov ah, 0
    ret

jui_s_bar:                      ; the level in px, 0FFFFh not answering
    mov bl, JOYS_THR
    cmp byte [si+JW_A], 0
    je .t
    mov bl, JOYS_RUD
.t:
    xor bh, bh
    mov al, [joy_st+bx]
    or al, al
    jz .none
    dec ax
    xor ah, ah                  ; 0..254
    mov cx, [si+JW_H]
    dec cx
    dec cx
    mul cx
    mov cx, 254
    div cx
    ret
.none:
    mov ax, 0xFFFF
    ret

; -----------------------------------------------------------------------------
; jui_s_box - a box's dot: (x << 8) | y inside the box, or 0FFFFh when its
; stick is not answering. Stick A and B are the logical sticks, after Swap
; and Invert (joy_llive, joy_st); 2 is the calibration's raw view of the port
; axes behind stick A, scaled to 2048 counts
; -----------------------------------------------------------------------------
jui_s_box:
    mov cx, [si+JW_W]
    sub cx, JUI_DOT + 2         ; CX = the dot's travel, px
    mov al, [si+JW_A]
    cmp al, 2
    je .raw
    mov bl, 3
    mov di, joy_st + JOYS_X1
    or al, al
    jz .a
    mov bl, 12
    mov di, joy_st + JOYS_X2
.a:
    test [joy_llive], bl
    jz .none
    mov al, [di]
    call .pos
    push ax
    mov al, [di+1]
    call .pos
    pop dx
    mov ah, dl
    ret
.pos:                           ; AL = -127..127 -> AL = 1 + travel * (p+127)/254
    cbw
    add ax, 127
    mul cx
    mov bx, 254
    div bx
    inc ax
    ret
.raw:
    mov bx, 0                   ; stick A's port axes: X1, Y1 - or X2, Y2
    test byte [joy_blob+JOYB_FLAGS], JOYF_SWAP
    jz .rs
    mov bx, 4
.rs:
    mov ax, [joy_raw+bx]
    cmp ax, JOY_NONE
    je .none
    call .rpos
    push ax
    mov ax, [joy_raw+bx+2]
    cmp ax, JOY_NONE
    je .none2
    call .rpos
    pop dx
    mov ah, dl
    ret
.none2:
    pop ax
.rpos:                          ; AX = raw -> AL = 1 + travel * min(raw,2047)/2048
    cmp ax, 2047
    jbe .rf
    mov ax, 2047
.rf:
    mul cx
    push cx
    mov cx, 2048
    div cx
    pop cx
    inc ax
    ret
.none:
    mov ax, 0xFFFF
    ret

; -----------------------------------------------------------------------------
; jui_s_num - a number source (JS_*): AX = the value, JS_NONE for nothing
; -----------------------------------------------------------------------------
jui_s_num:
    mov al, [si+JW_A]
jui_src:
    cmp al, JS_RAW
    jae .nl
    mov cl, al                  ; --- a logical axis, calibrated
    mov bl, 1
    shl bl, cl
    test [joy_llive], bl
    jz .none
    xor bx, bx
    mov bl, al
    mov al, [joy_st+JOYS_X1+bx]
    cbw
    ret
.nl:
    cmp al, JS_THR
    jae .nraw
    sub al, JS_RAW              ; --- the raw count behind a logical axis
    test byte [joy_blob+JOYB_FLAGS], JOYF_SWAP
    jz .port
    xor al, 2
.port:
    xor bx, bx
    mov bl, al
    shl bx, 1
    mov ax, [joy_raw+bx]
    cmp ax, JOY_NONE
    je .none
    ret
.nraw:
    cmp al, JS_RUD
    ja .nrud
    mov bl, JOYS_THR
    jb .tr
    mov bl, JOYS_RUD
.tr:
    xor bh, bh
    mov bl, [joy_st+bx]
    or bl, bl
    jz .none
    cmp al, JS_RUD
    je .rud
    dec bx                      ; throttle 1..255 -> 0..100 %
    mov ax, 100
    mul bx
    mov bx, 254
    div bx
    ret
.rud:
    mov al, bl                  ; rudder 1..255 -> -127..127
    xor al, 0x80
    cbw
    ret
.nrud:
    cmp al, JS_DZ
    jne .ndz
    mov al, [joy_blob+JOYB_DZ]
    mov ah, 0
    ret
.ndz:
    cmp al, JS_TIMEC
    ja .ncal
    mov al, [joy_live]          ; --- the read time: shown only while
    and al, [joy_tax]           ; something answers
    jz .none
    mov ax, [jui_tshow]
    or ax, ax
    jz .none
    ret
.ncal:
    sub al, JS_PRAW             ; --- the calibration's port axes
    mov bl, al
    and bx, 3
    shl bx, 1
    cmp al, JS_PMIN - JS_PRAW
    jb .praw
    cmp al, JS_PMAX - JS_PRAW
    jae .pmax
    mov ax, [jui_cmin+bx]
    cmp ax, 0xFFFF
    je .none
    ret
.pmax:
    cmp word [jui_cmin+bx], 0xFFFF
    je .none
    mov ax, [jui_cmax+bx]
    ret
.praw:
    mov ax, [joy_raw+bx]
    cmp ax, JOY_NONE
    jne .r
.none:
    mov ax, JS_NONE
.r:
    ret

; the format each source is shown in
JF_SGN      equ 0               ; +34, -12, 0
JF_U        equ 1
JF_PCT      equ 2               ; 8%
JF_MS       equ 3               ; 1.5 ms a read
JF_MSC      equ 4               ; 1.5ms
jui_sfmt:
    db JF_SGN, JF_SGN, JF_SGN, JF_SGN, JF_U, JF_U, JF_U, JF_U
    db JF_PCT, JF_SGN, JF_PCT, JF_MS, JF_MSC
    times 12 db JF_U
%if $ - jui_sfmt != JS_PMAX + 4
  %error "joyui: one format per JS_*"
%endif

; -----------------------------------------------------------------------------
; jui_s_dtxt - a string source (JD_*): AX = the string, which is its state
; -----------------------------------------------------------------------------
jui_s_dtxt:
    mov al, [si+JW_A]
    xor bx, bx                  ; BX = 2 on the compact face: every string
    cmp word [jui_w], JUI_WIDE  ; source has a wide and a compact answer
    jae .w
    mov bl, 2
.w:
    cmp al, JD_TYPE
    jb jui_status
    je .type
    cmp al, JD_HAT
    je .hat
    mov ah, al                  ; --- the banner: step * 2 + line, by face
    mov al, [jui_step]
    dec ax
    and al, 3
    shl al, 1
    cmp ah, JD_BAN2
    jne .b1
    inc ax
.b1:
    shl al, 1
    shl al, 1                   ; 4 bytes an entry
    add bl, al
    mov ax, [jui_ban+bx]
    ret
.type:
    mov bl, [joy_blob+JOYB_TYPE]
    shl bx, 1
    mov ax, [jui_tshort+bx]
    ret
.hat:
    mov al, [joy_st+JOYS_HAT]
    call jui_hatcell
    mov bl, al
    shl bx, 1
    mov ax, [jui_hatn+bx]
    ret

; jui_hatcell - AL = a JOYD_* -> AL = its cell in the 3 x 3 grid, row-major
; (4 = centred). Nothing else
jui_hatcell:
    push cx
    mov ah, al
    mov cl, 1                   ; the column
    test ah, JOYD_LEFT
    jz .nl
    dec cx
.nl:
    test ah, JOYD_RIGHT
    jz .nr
    inc cx
.nr:
    mov al, 3                   ; ...and the row
    test ah, JOYD_UP
    jz .nu
    xor al, al
.nu:
    test ah, JOYD_DOWN
    jz .nd
    mov al, 6
.nd:
    add al, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; jui_status - the status line's words (SPEC.md 137.6.6): AX = the string, BX
; = 0 wide / 2 compact. Missing first, then not calibrated, then found
; -----------------------------------------------------------------------------
jui_status:
    mov al, [joy_live]
    and al, [joy_tax]
    mov si, jui_st_none
    jz .pick
    cmp al, [joy_tax]
    je .all
    mov ah, [joy_blob+JOYB_TYPE] ; --- an axis of the type is missing
    mov si, jui_st_nob
    cmp ah, JOYT_2X2B
    jne .nab
    test byte [joy_llive], 3
    jnz .pick
    mov si, jui_st_noa
    jmp short .pick
.nab:
    mov si, jui_st_nothr
    cmp ah, JOYT_FLIGHT
    je .pick
    cmp ah, JOYT_CHFS
    je .pick
    mov si, jui_st_nohat
    cmp ah, JOYT_TMFCS
    je .pick
.all:
    call jui_uncal              ; --- a type axis without a calibration
    cmp al, ah
    mov si, jui_st_uncal
    jne .pick
    mov al, [joy_blob+JOYB_TYPE]
    mov si, jui_st_two
    cmp al, JOYT_2X2B
    je .pick
    mov si, jui_st_fly
    cmp al, JOYT_FLIGHT
    je .pick
    cmp al, JOYT_CHFS
    je .pick
    mov si, jui_st_hat
    cmp al, JOYT_TMFCS
    je .pick
    mov si, jui_st_one
.pick:
    mov ax, [si+bx]
    mov si, [jui_cur]
    ret

; =============================================================================
; THE DRAWS - SI = the widget, AX = its new state, DX = the old one,
; [jui_force] = 1 for a whole paint. Every register free
; =============================================================================

jui_d_text:
    mov di, ax                  ; (a TEXT's state is its string)
    jmp jui_tw

jui_d_dtxt:
    mov di, ax
    jmp jui_tw

; -----------------------------------------------------------------------------
; jui_d_num - the value, formatted and RIGHT-aligned in its field: one opaque
; run of the field's width, nothing written twice
; -----------------------------------------------------------------------------
jui_d_num:
    mov bl, [si+JW_A]
    xor bh, bh
    mov bl, [jui_sfmt+bx]
    mov di, jui_num
    cmp ax, JS_NONE
    jne .val
    cmp bl, JF_MS
    jae .end                    ; a time not taken: an empty field
    mov word [di], '--'
    inc di
    inc di
    jmp short .end
.val:
    cmp bl, JF_SGN
    jne .uns
    or ax, ax
    jle .neg
    mov byte [di], '+'
    inc di
.neg:
    call dec_s16
    jmp short .end
.uns:
    cmp bl, JF_MS
    jae .ms
    call dec_u16
    cmp bl, JF_PCT
    jne .end
    mov byte [di], '%'
    inc di
    jmp short .end
.ms:
    xor dx, dx
    mov cx, 10
    div cx
    call dec_u16
    mov byte [di], '.'
    inc di
    xchg ax, dx
    add al, '0'
    mov [di], al
    inc di
    push si
    mov si, jui_t_ms
    cmp bl, JF_MS
    je .cp
    mov si, jui_t_msc
.cp:
    mov al, [si]
    inc si
    mov [di], al
    inc di
    or al, al
    jnz .cp
    dec di
    pop si
.end:
    mov byte [di], 0
    sub di, jui_num             ; DI = the number's length
    mov cx, [si+JW_W]           ; CX = the field's cells
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov bx, jui_txt
    sub cx, di
    jbe .copy                   ; (a number wider than its field is shown
.pad:                           ; whole rather than cut)
    mov byte [bx], ' '
    inc bx
    loop .pad
.copy:
    push si
    mov si, jui_num
.c:
    mov al, [si]
    inc si
    mov [bx], al
    inc bx
    or al, al
    jnz .c
    pop si
    mov cx, [si+JW_X]
    mov dx, [si+JW_Y]
    mov ax, [si+JW_H]
    push si
    mov si, jui_txt
    call jui_run
    pop si
    ret

jui_d_rect:
    mov al, [si+JW_A]
    call jui_mcol
    call jui_ink
    jmp jui_wfill

; -----------------------------------------------------------------------------
; jui_d_group - a group: its frame and its caption strip (SPEC.md 137.6.2)
; -----------------------------------------------------------------------------
jui_d_group:
    mov al, CBLACK
    call jui_ink
    mov ax, [si+JW_X]
    mov bx, [si+JW_Y]
    mov cx, ax
    add cx, [si+JW_W]
    dec cx
    mov dx, bx
    add dx, [si+JW_H]
    dec dx
    call jui_frame
    mov al, CLGRAY              ; the strip: two rows, the caption, two rows
    call jui_mcol
    call jui_ink
    mov ax, [si+JW_X]
    inc ax
    mov bx, [si+JW_Y]
    inc bx
    mov cx, ax
    add cx, [si+JW_W]
    sub cx, 3
    mov dx, bx
    inc dx
    push ax
    push cx
    call jui_fill
    pop cx
    pop ax
    mov bx, [si+JW_Y]
    add bx, 11
    mov dx, bx
    inc dx
    call jui_fill
    mov cx, [si+JW_X]
    inc cx
    mov dx, [si+JW_Y]
    add dx, 3
    mov bx, [si+JW_W]
    dec bx
    dec bx
    mov al, [si+JW_A]
    cbw
    shl ax, 1
    xchg di, ax
    mov di, [jui_gcap+di]
    mov ax, CK(CBLACK, CLGRAY)
    jmp jui_twx

; -----------------------------------------------------------------------------
; jui_d_box - a stick's box (SPEC.md 137.6.3, 137.6.4)
; Whole: the frame, the ground, and either the crosshair, the dashed dead
; zone and the dot - or, for a stick that is not answering, "Not connected"
; where there is room for it. A move: jui_dotmove
; -----------------------------------------------------------------------------
jui_d_box:
    push ax
    push dx
    call jui_boxgeo
    pop dx
    pop ax
    cmp byte [jui_force], 0
    jne .full
    cmp dx, 0xFFFE
    jae .full
    cmp ax, 0xFFFF
    je .full
    jmp jui_dotmove
.full:
    push ax
    mov al, CBLACK
    call jui_ink
    mov ax, [jui_bx0]
    mov bx, [jui_by0]
    mov cx, ax
    add cx, [jui_bs]
    dec cx
    mov dx, bx
    add dx, [jui_bs]
    dec dx
    push ax
    push bx
    push cx
    push dx
    call jui_frame
    mov al, CWHITE
    call jui_ink
    pop dx
    pop cx
    pop bx
    pop ax
    inc ax
    inc bx
    dec cx
    dec dx
    call jui_fill
    pop ax
    cmp ax, 0xFFFF
    je .nc
    push ax
    mov al, [jui_lcol]
    call jui_ink
    mov ax, 1                   ; the crosshair
    mov bx, [jui_bc]
    mov cx, [jui_bs]
    dec cx
    dec cx
    mov dx, bx
    call jui_bfill
    mov ax, [jui_bc]
    mov bx, 1
    mov cx, ax
    mov dx, [jui_bs]
    dec dx
    dec dx
    call jui_bfill
    mov cx, [jui_hz]            ; the dead zone, dashed
    jcxz .dot
    mov ax, [jui_bc]
    sub ax, cx                  ; AX = its low edge, CX = its high one
    add cx, [jui_bc]
    push ax
    push cx
    mov bx, ax                  ; the top...
    mov dx, ax
    call jui_dash
    pop cx
    pop ax
    push ax
    push cx
    mov bx, cx                  ; ...the bottom...
    mov dx, cx
    call jui_dash
    pop dx
    pop bx
    push bx
    push dx
    mov ax, bx                  ; ...the left...
    mov cx, bx
    call jui_dash
    pop dx
    pop bx
    mov ax, dx                  ; ...and the right
    mov cx, dx
    call jui_dash
.dot:
    pop ax
    jmp jui_dotat
.nc:
    cmp word [jui_bs], 112      ; "Not connected": 13 cells, where they fit
    jb .r
    mov cx, [jui_bs]
    sub cx, 104
    shr cx, 1
    add cx, [jui_bx0]
    mov dx, [jui_bc]
    sub dx, 4
    add dx, [jui_by0]
    mov si, jui_t_nc
    mov ax, JC_TEXT
    jmp jui_run
.r:
    ret

; jui_boxgeo - SI = a box: bank its origin, side, centre and dead zone
jui_boxgeo:
    mov ax, [si+JW_X]
    mov [jui_bx0], ax
    mov ax, [si+JW_Y]
    mov [jui_by0], ax
    mov ax, [si+JW_W]
    mov [jui_bs], ax
    shr ax, 1
    mov [jui_bc], ax
    xor cx, cx                  ; the dead zone's half side: dz % of the half
    cmp byte [si+JW_A], 2       ; (none on the raw view)
    je .z
    dec ax
    mov cl, [joy_blob+JOYB_DZ]
    mul cx
    mov cx, 100
    div cx
    xchg cx, ax
.z:
    mov [jui_hz], cx
    ret

; jui_dotat - AX = a dot's state: fill it
jui_dotat:
    push ax
    mov al, [jui_lcol+1]
    call jui_ink
    pop ax
    mov bl, al
    mov al, ah
    xor ah, ah
    xor bh, bh
    mov cx, ax
    add cx, JUI_DOT - 1
    mov dx, bx
    add dx, JUI_DOT - 1
                                ; ...and into jui_bfill
; jui_bfill - AX/BX/CX/DX box-relative -> filled in the current colour
jui_bfill:
    add ax, [jui_bx0]
    add cx, [jui_bx0]
    add bx, [jui_by0]
    add dx, [jui_by0]
    jmp jui_fill

; -----------------------------------------------------------------------------
; jui_dash - one edge of the dead-zone square, AX/BX..CX/DX box-relative (a
; row or a column): its lit runs filled. Lit is ((x + y) & 2) == 0, the one
; rule jui_lines composes by, so a dash a dot passed over comes back exactly
; -----------------------------------------------------------------------------
jui_dash:
    mov di, ax                  ; DI = where along it, SI = the end, BP = the
    mov si, cx                  ; fixed coordinate - horizontal when the y's
    mov bp, bx                  ; agree
    cmp bx, dx
    je .h
    mov di, bx
    mov si, dx
    mov bp, ax
.h:
    mov [jui_dh], bx
    sub [jui_dh], dx            ; 0 = horizontal
.l:
    cmp di, si
    jg .r
    mov ax, di
    add ax, bp
    test al, 2
    jz .run
    inc di
    jmp short .l
.run:
    mov cx, di                  ; a run: while lit
.e:
    inc di
    cmp di, si
    jg .draw
    mov ax, di
    add ax, bp
    test al, 2
    jz .e
.draw:
    push si
    push di
    push bp
    dec di                      ; CX..DI = the run along, BP across
    cmp word [jui_dh], 0
    je .hz
    mov ax, bp                  ; a column
    mov bx, cx
    mov cx, bp
    mov dx, di
    jmp short .f
.hz:
    mov ax, cx                  ; a row
    mov bx, bp
    mov cx, di
    mov dx, bp
.f:
    call jui_bfill
    pop bp
    pop di
    pop si
    jmp short .l
.r:
    ret

; -----------------------------------------------------------------------------
; jui_dotmove - the dot moved: DX = where it was, AX = where it is now (each
; (x << 8) | y inside the box). The old dot's cells are put back to what is
; under them - crosshair, dashed square or ground - by ONE masked record whose
; mask leaves out the new dot's pixels; then the new dot is filled. No pixel
; is written twice (SPEC.md 137.6.4)
; -----------------------------------------------------------------------------
jui_dotmove:
    mov [jui_new], ax
    mov [jui_old], dx
    xor cx, cx                  ; CX = the row
    mov di, jui_icon + 2        ; DI = its mask word
.row:
    mov al, [jui_old]           ; y = the old dot's top + the row
    xor ah, ah
    add ax, cx
    mov [jui_ty], ax
    mov bx, (0xFFFF << (16 - JUI_DOT)) & 0xFFFF ; the old dot's pixels...
    mov dl, [jui_new]
    xor dh, dh
    cmp ax, dx
    jb .keep
    add dx, JUI_DOT
    cmp ax, dx
    jae .keep
    push cx                     ; ...less the new dot's, on a row both have
    mov al, [jui_new+1]
    xor ah, ah
    mov dl, [jui_old+1]
    xor dh, dh
    sub ax, dx                  ; AX = how far right it went
    cmp ax, -(JUI_DOT - 1)
    jl .nx
    cmp ax, JUI_DOT - 1
    jg .nx
    mov dx, (0xFFFF << (16 - JUI_DOT)) & 0xFFFF
    mov cl, al
    or ax, ax
    jns .rt
    neg cl
    shl dx, cl
    jmp short .ex
.rt:
    shr dx, cl
.ex:
    not dx
    and bx, dx
.nx:
    pop cx
.keep:
    mov [di], bx
    mov al, [jui_old+1]
    xor ah, ah
    mov dx, [jui_ty]
    call jui_lines              ; AX = what is under this row
    and ax, bx
    mov [di+2*JUI_DOT], ax
    inc di
    inc di
    inc cx
    cmp cx, JUI_DOT
    jb .row
    mov al, CWHITE              ; the mask's ground, the lines over it
    mov ah, [jui_lcol]
    call OSAPI_ICON_PEN
    mov cl, [jui_old+1]
    xor ch, ch
    add cx, [jui_bx0]
    add cx, [jui_px]
    mov dl, [jui_old]
    xor dh, dh
    add dx, [jui_by0]
    add dx, [jui_py]
    mov si, jui_icon
    call OSAPI_ICON_DRAW
    mov ax, [jui_new]
    jmp jui_dotat

; -----------------------------------------------------------------------------
; jui_lines - what the box draws under a 16-pixel row: AX = its first x, DX =
; its y (box-relative) -> AX = the line pixels, bit 15 leftmost. The crosshair
; (solid) and the dead-zone square (lit where ((x + y) & 2) == 0, jui_dash's
; rule). Every other register kept
; -----------------------------------------------------------------------------
jui_lines:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax                  ; SI = x0
    xor bx, bx                  ; BX = the bits
    mov cx, [jui_bc]
    cmp dx, cx                  ; the crosshair's row...
    jne .nh
    dec bx
.nh:
    mov ax, cx                  ; ...and its column
    sub ax, si
    jb .nv
    cmp ax, 16
    jae .nv
    push cx
    mov cl, al
    mov ax, 0x8000
    shr ax, cl
    or bx, ax
    pop cx
.nv:
    mov ax, [jui_hz]
    or ax, ax
    jz .out
    mov di, cx
    add di, ax                  ; DI = the square's high edge
    sub cx, ax                  ; CX = its low one
    cmp dx, cx
    je .edge
    cmp dx, di
    je .edge
    jb .side
    jmp short .out
.side:
    cmp dx, cx
    jb .out
    mov ax, cx                  ; a row between: the two sides' pixels
    call .pt
    mov ax, di
    call .pt
    jmp short .out
.edge:                          ; the top or bottom row: a dashed run
    mov ax, cx
    sub ax, si                  ; a = lo - x0, at least 0
    jge .a
    xor ax, ax
.a:
    cmp ax, 16
    jge .out
    push ax
    mov ax, di
    sub ax, si                  ; b = hi - x0, at most 15
    jl .outp
    cmp ax, 15
    jle .b
    mov ax, 15
.b:
    mov cx, 15
    sub cx, ax
    mov ax, 0xFFFF
    shl ax, cl                  ; ...columns a..b
    pop cx
    push dx
    mov dx, 0xFFFF
    shr dx, cl
    and ax, dx
    pop dx
    mov cx, si                  ; ...lit: 0CCCCh turned by (x0 + y) & 3
    add cx, dx
    and cx, 3
    mov dx, 0xCCCC
    rol dx, cl
    and ax, dx
    or bx, ax
    jmp short .out
.outp:
    pop ax
.out:
    mov ax, bx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.pt:                            ; AX = a side's x: its pixel on this row
    push cx
    mov cx, ax
    add cx, dx
    test cl, 2
    jnz .pr
    sub ax, si
    jb .pr
    cmp ax, 16
    jae .pr
    mov cl, al
    mov ax, 0x8000
    shr ax, cl
    or bx, ax
.pr:
    pop cx
    ret

; -----------------------------------------------------------------------------
; jui_d_lamp - a button's lamp, and its digit when its greying changed
; -----------------------------------------------------------------------------
jui_d_lamp:
    mov bx, dx
    xor bx, ax                  ; BX = the bits that changed
    push bx
    push ax
    push si
    mov cx, [si+JW_X]
    mov dx, [si+JW_Y]
    call jui_lampat
    pop si
    pop ax
    pop bx
    cmp byte [jui_force], 0
    jne .dig
    test bl, 2
    jz .r
.dig:
    shr al, 1
    and al, 1
    mov [jui_tdis], al
    mov al, [si+JW_A]
    add al, '1'
    mov [jui_num], al
    mov byte [jui_num+1], 0
    mov cx, [si+JW_X]
    add cx, 10
    mov dx, [si+JW_Y]
    mov si, jui_num
    mov ax, JC_TEXT
    jmp jui_run
.r:
    ret

; jui_lampat - CX/DX = where (pane-relative), AL = 0 off, 1 lit, 2 greyed: one
; masked record, its disc and its ring in one pass. SI and AX-DX spent
jui_lampat:
    xor ah, ah
    and al, 3
    shl ax, 1
    mov bx, ax
    cmp byte [jui_mono], 0
    je .c
    add bx, 8
.c:
    push cx
    push dx
    mov ax, [jui_lpen+bx]
    call OSAPI_ICON_PEN
    pop dx
    pop cx
    add cx, [jui_px]
    add dx, [jui_py]
    mov si, jui_lamp
    call OSAPI_ICON_DRAW
    ret

jui_lpen:   dw CK(CLGRAY, CDGRAY), CK(CLGREEN, CGREEN), CK(CWHITE, CLGRAY)
            dw CK(CWHITE, CLGRAY)
            dw CK(CWHITE, CBLACK), CK(CBLACK, CBLACK), CK(CWHITE, CBLACK)
            dw CK(CWHITE, CBLACK)
jui_lamp:   db 1, 8             ; an 8 x 8 disc, and its ring
            dw 0x3C00, 0x7E00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0x7E00, 0x3C00
            dw 0x3C00, 0x4200, 0x8100, 0x8100, 0x8100, 0x8100, 0x4200, 0x3C00

; -----------------------------------------------------------------------------
; jui_d_hat - the hat: nine lamps whole, or the one that went out and the one
; that lit
; -----------------------------------------------------------------------------
jui_d_hat:
    mov [jui_new], ax
    cmp byte [jui_force], 0
    jne .all
    cmp dx, 0xFFFE
    jae .all
    mov al, dl
    call jui_hatcell
    xor bl, bl
    call .cell
    mov al, [jui_new]
    call jui_hatcell
    mov bl, 1
    jmp short .cell
.all:
    mov al, [jui_new]
    call jui_hatcell
    mov [jui_tmpb], al
    mov byte [jui_hc], 0
.l:
    mov al, [jui_hc]
    xor bl, bl
    cmp al, [jui_tmpb]
    jne .o
    inc bx
.o:
    call .cell
    inc byte [jui_hc]
    cmp byte [jui_hc], 9
    jb .l
    ret
.cell:                          ; AL = a cell, BL = its lamp's state
    push si
    xor ah, ah
    mov cl, 3
    div cl                      ; AL = the row, AH = the column
    mov dl, al
    mov al, 10
    mul ah
    xchg cx, ax
    add cx, [si+JW_X]
    mov al, 10
    mul dl
    xchg dx, ax
    add dx, [si+JW_Y]
    mov al, bl
    call jui_lampat
    pop si
    ret

; -----------------------------------------------------------------------------
; jui_d_bar - throttle or rudder: whole, or only the rows between the old
; level and the new one, in the colour they become
; -----------------------------------------------------------------------------
jui_d_bar:
    cmp ax, 0xFFFF
    jne .a
    xor ax, ax
.a:
    mov [jui_new], ax
    mov di, [si+JW_Y]
    add di, [si+JW_H]
    sub di, 2                   ; DI = the inside's bottom row
    cmp byte [jui_force], 0
    jne .full
    cmp dx, 0xFFFE
    je .full
    cmp dx, 0xFFFF
    jne .b
    xor dx, dx
.b:
    mov [jui_old], dx
    cmp ax, dx
    je .r
    ja .up
    mov al, CWHITE              ; it fell: white from the old top down
    call jui_ink
    mov bx, di
    sub bx, [jui_old]
    inc bx
    mov dx, di
    sub dx, [jui_new]
    jmp short .rows
.up:
    mov al, [jui_lcol+1]        ; it rose: ink from the new top down
    call jui_ink
    mov bx, di
    sub bx, [jui_new]
    inc bx
    mov dx, di
    sub dx, [jui_old]
.rows:
    mov ax, [si+JW_X]
    inc ax
    mov cx, ax
    add cx, [si+JW_W]
    sub cx, 3
    jmp jui_fill
.full:
    mov al, CBLACK
    call jui_ink
    mov ax, [si+JW_X]
    mov bx, [si+JW_Y]
    mov cx, ax
    add cx, [si+JW_W]
    dec cx
    mov dx, di
    inc dx
    call jui_frame
    mov dx, di                  ; the ground above the level...
    sub dx, [jui_new]
    mov bx, [si+JW_Y]
    inc bx
    cmp dx, bx
    jl .ink
    mov al, CWHITE
    call jui_ink
    mov ax, [si+JW_X]
    inc ax
    mov cx, ax
    add cx, [si+JW_W]
    sub cx, 3
    call jui_fill
.ink:
    cmp word [jui_new], 0       ; ...and the level
    je .r
    mov al, [jui_lcol+1]
    call jui_ink
    mov bx, di
    sub bx, [jui_new]
    inc bx
    mov dx, di
    jmp short .rows
.r:
    ret

; -----------------------------------------------------------------------------
; jui_d_btn - one push button through the standard control (SPEC.md
; 20.5.1.3): its label and greying from the action, held from jui_down
; -----------------------------------------------------------------------------
jui_d_btn:
    xor di, di
    test al, 1
    jz .l
    or di, OS88UI_DIS
.l:
    test al, 2
    jz .d
    or di, OS88UI_LATCH
.d:
    mov [jui_bflg], di
    mov bl, [si+JW_A]
    xor bh, bh
    shl bx, 1
    mov ax, [jui_albl+bx]
    mov [jui_blbl], ax
    mov ax, [si+JW_X]
    add ax, [jui_px]
    mov [jui_brect], ax
    add ax, [si+JW_W]
    dec ax
    mov [jui_brect+4], ax
    mov ax, [si+JW_Y]
    add ax, [jui_py]
    mov [jui_brect+2], ax
    add ax, [si+JW_H]
    dec ax
    mov [jui_brect+6], ax
    mov bx, jui_brec
    mov al, 1
    call os88ui_btn
    ret

; jui_d_radio / jui_d_check - a glyph, and its label on a whole paint only
jui_d_radio:
    mov bl, OS88UI_GRADIO
    mov di, jui_tfull
    jmp short jui_glyphw
jui_d_check:
    mov bl, OS88UI_GCHECK
    mov di, jui_chkl
jui_glyphw:
    push bx
    mov bl, [si+JW_A]
    xor bh, bh
    shl bx, 1
    mov di, [di+bx]             ; DI = the label
    pop bx
    test al, 1
    jz .n1
    or bl, OS88UI_GON
.n1:
    test al, 2
    jz .n2
    or bl, OS88UI_GDOWN
.n2:
    mov al, bl
    xor ah, ah
    mov cx, [si+JW_X]
    add cx, [jui_px]
    mov dx, [si+JW_Y]
    add dx, [jui_py]
    call os88ui_glyph
    cmp byte [jui_force], 0
    je .r
    mov cx, [si+JW_X]
    add cx, 16
    mov dx, [si+JW_Y]
    add dx, 2
    mov si, di
    mov ax, JC_TEXT
    jmp jui_run
.r:
    ret

; jui_d_step - a calibration step: its box (ticked once done) and its label,
; highlighted while it is the step, greyed while it is to come
jui_d_step:
    push ax
    mov bl, OS88UI_GCHECK
    cmp al, 2
    jne .g
    or bl, OS88UI_GON
.g:
    xor ah, ah
    or al, al
    jnz .lv
    inc ah                      ; to come: greyed
.lv:
    mov al, bl
    mov cx, [si+JW_X]
    add cx, [jui_px]
    mov dx, [si+JW_Y]
    add dx, [jui_py]
    call os88ui_glyph
    pop ax
    mov bx, JC_TEXT
    cmp al, 1
    jne .nc
    mov bx, JC_BAN
.nc:
    or al, al
    jnz .nd
    mov byte [jui_tdis], 1
.nd:
    xchg ax, bx                 ; AX = the colours
    mov bl, [si+JW_A]
    xor bh, bh
    dec bx
    shl bx, 1
    mov di, [jui_stepl+bx]
    mov cx, [si+JW_X]
    add cx, 16
    mov dx, [si+JW_Y]
    add dx, 2
    mov bx, [si+JW_W]
    sub bx, 16
    jmp short jui_twx

; =============================================================================
; DRAWING HELPERS - pane-relative in, the origin added in one place
; =============================================================================

; jui_tw - SI = a text widget, DI = its string
jui_tw:
    mov cx, [si+JW_X]
    mov dx, [si+JW_Y]
    mov bx, [si+JW_W]
    mov ax, [si+JW_H]
; -----------------------------------------------------------------------------
; jui_twx - one opaque run: CX/DX = where, BX = the field's width in px (0 =
; the string exactly), DI = the string, AX = ink | paper << 8. The string is
; padded with spaces to the field's whole cells, and a part cell left over is
; filled with the paper - text first, then only the tail (SPEC.md 31.9.3)
; -----------------------------------------------------------------------------
jui_twx:
    push ax
    push bx
    push cx
    push dx
    mov dx, bx
    shr dx, 1
    shr dx, 1
    shr dx, 1                   ; DX = the cells, 0 = exact
    mov bx, jui_txt
.c:
    cmp bx, jui_txt + JUI_TXTMAX - 1
    jae .e
    or dx, dx
    jz .x
    mov ax, bx
    sub ax, jui_txt
    cmp ax, dx
    jae .e
.x:
    mov al, [di]
    or al, al
    jz .nul
    inc di
    jmp short .put
.nul:
    or dx, dx
    jz .e
    mov al, ' '
.put:
    mov [bx], al
    inc bx
    jmp short .c
.e:
    mov byte [bx], 0
    pop dx
    pop cx
    pop bx
    pop ax
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, jui_txt
    call jui_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov di, bx
    and di, 7
    jz .r
    push ax
    mov al, ah
    call jui_mcol
    call jui_ink
    pop ax
    add cx, bx
    sub cx, di                  ; CX = past the last whole cell
    mov ax, cx
    add cx, di
    dec cx
    mov bx, dx
    add dx, 7
    jmp short jui_fill
.r:
    ret

; -----------------------------------------------------------------------------
; jui_run - SI = a NUL string at CX/DX (pane-relative), AX = ink | paper << 8,
; [jui_tdis] = greyed (SPEC.md 47: the pen's flag, which dithers the glyphs
; on a 1bpp adapter, and CDGRAY on a colour one). One OSAPI_FONT_RUN
; -----------------------------------------------------------------------------
jui_run:
    call jui_cmap
    cmp byte [jui_tdis], 0
    je .live
    stc
    call OSAPI_GFX_PEN
    cmp byte [jui_mono], 0
    jne .go
    mov al, CDGRAY
    jmp short .go
.live:
    clc
    call OSAPI_GFX_PEN
.go:
    add cx, [jui_px]
    add dx, [jui_py]
    call OSAPI_FONT_RUN
    cmp byte [jui_tdis], 0
    je .r
    mov byte [jui_tdis], 0
    clc
    call OSAPI_GFX_PEN          ; the pen back to live
.r:
    ret

; jui_cmap - AX = ink | paper << 8 -> the 1bpp pair: black on a white paper,
; white on any other (SPEC.md 39.4)
jui_cmap:
    cmp byte [jui_mono], 0
    je .r
    cmp ah, CWHITE
    mov ax, CK(CBLACK, CWHITE)
    je .r
    mov ax, CK(CWHITE, CBLACK)
.r:
    ret

; jui_mcol - AL = a fill colour -> the 1bpp one: white stays, the rest black
jui_mcol:
    cmp byte [jui_mono], 0
    je .r
    cmp al, CWHITE
    je .r
    mov al, CBLACK
.r:
    ret

jui_ink:
    call OSAPI_SET_COLOR
    ret

; jui_wfill - SI = a widget: its rect, filled in the current colour
jui_wfill:
    mov ax, [si+JW_X]
    mov bx, [si+JW_Y]
    mov cx, ax
    add cx, [si+JW_W]
    dec cx
    mov dx, bx
    add dx, [si+JW_H]
    dec dx
; jui_fill / jui_frame - AX/BX/CX/DX pane-relative, inclusive
jui_fill:
    call jui_abs
    call OSAPI_GFX_FILL
    ret
jui_frame:
    call jui_abs
    call OSAPI_GFX_FRAME
    ret
jui_abs:
    add ax, [jui_px]
    add cx, [jui_px]
    add bx, [jui_py]
    add dx, [jui_py]
    ret

; =============================================================================
; THE LAYOUTS (SPEC.md 137.6.1-137.6.3) - descriptors, anchored
; =============================================================================

; --- compact, the test view (222 x 132: 27 cells by 16 rows) -------------------
jui_l_cn:
    JWD WT_TEXT, 0, 2, 4, 0, joyui_name
    JWD WT_BOX, 0, 2, 14, 48, 0
    JWD WT_TEXT, 0, 58, 16, 0, jui_t_x
    JWD WT_NUM, JS_AX, 70, 16, 32, 0
    JWD WT_TEXT, 0, 106, 16, 0, jui_t_raw
    JWD WT_NUM, JS_RAW, 138, 16, 40, 0
    JWD WT_TEXT, 0, 58, 26, 0, jui_t_y
    JWD WT_NUM, JS_AX+1, 70, 26, 32, 0
    JWD WT_TEXT, 0, 106, 26, 0, jui_t_raw
    JWD WT_NUM, JS_RAW+1, 138, 26, 40, 0
    JWD WT_LAMP, 0, 58, 48, 0, 0
    JWD WT_LAMP, 1, 84, 48, 0, 0
    JWD WT_LAMP, 2, 110, 48, 0, 0
    JWD WT_LAMP, 3, 136, 48, 0, 0
    JWD WT_TEXT, 0, 2, 66, 0, jui_t_type
    JWD WT_DTXT, JD_TYPE, 50, 66, 144, 0
    JWD WT_BTN, JA_TYPE, 198, 64, 20, 13
    JWD WT_TEXT, 0, 2, 82, 0, jui_t_dz
    JWD WT_BTN, JA_DZM, 82, 80, 16, 12
    JWD WT_NUM, JS_DZ, 102, 82, 32, 0
    JWD WT_BTN, JA_DZP, 138, 80, 16, 12
    JWD WT_CHECK, 2, 2, 96, 80, 12
    JWD WT_CHECK, 3, 110, 96, 88, 12
    JWD WT_BTN, JA_CAL, 2, 109, 104, 14
    JWD WT_BTN, JA_RESET, 114, 109, 56, 14
    JWD WT_DTXT, JD_STATUS, 2, 124, 168, 0
    JWD WT_NUM, JS_TIMEC, 172, 124, 48, 0
jui_l_cn_e:

; --- compact, calibrating -------------------------------------------------------
jui_l_cc:
    JWD WT_DTXT, JD_BAN1, 2, 2, 216, 0
    JWD WT_DTXT, JD_BAN2, 2, 10, 216, 0
    JWD WT_BOX, 2, 2, 24, 48, 0
    JWD WT_TEXT, 0, 58, 24, 0, jui_t_hdrc
    JWD WT_BTN, JA_CANCEL, 2, 109, 64, 14
    JWD WT_BTN, JA_BACK, 74, 109, 56, 14
    JWD WT_DTXT, JD_STATUS, 2, 124, 168, 0
    JWD WT_NUM, JS_TIMEC, 172, 124, 48, 0
jui_l_cc_e:

; --- the compact B line, per type (jui_tc_tab) -----------------------------------
jui_c_2x2:
    JWD WT_TEXT, 0, 58, 36, 0, jui_t_b
    JWD WT_NUM, JS_AX+2, 70, 36, 32, 0
    JWD WT_NUM, JS_AX+3, 106, 36, 32, 0
jui_c_hatr:
    JWD WT_TEXT, 0, 58, 36, 0, jui_t_hat
    JWD WT_DTXT, JD_HAT, 90, 36, 16, 0
jui_c_chfs:
    JWD WT_DTXT, JD_HAT, 158, 36, 16, 0
jui_c_fly:
    JWD WT_TEXT, 0, 58, 36, 0, jui_t_t
    JWD WT_NUM, JS_THR, 70, 36, 32, 0
    JWD WT_TEXT, 0, 110, 36, 0, jui_t_r
    JWD WT_NUM, JS_RUD, 118, 36, 32, 0
jui_c_end:
jui_tc_tab:                     ; per JOYT_*: the first, and past the last
    dw jui_c_2x2, jui_c_2x2     ; 2B: none
    dw jui_c_2x2, jui_c_hatr    ; 2X2B
    dw jui_c_2x2, jui_c_2x2     ; 4B: none
    dw jui_c_fly, jui_c_end     ; FLIGHT
    dw jui_c_chfs, jui_c_end    ; CHFS: the hat, then the flight line
    dw jui_c_hatr, jui_c_chfs   ; TMFCS

; --- wide, the test view (SPEC.md 137.6.2) --------------------------------------
jui_l_wn:
    JWD WT_GROUP, 0, 4, 4, AN(JN_MID,-8), AN(JN_SBOT,-4)
    JWD WT_GROUP, 1, AN(JN_MID,4), 4, AN(JN_MID,-8), AN(JN_SBOT,-4)
    JWD WT_BOX, 0, AN(JN_BXA,0), JUI_BTOP, AN(JN_SIDE,0), 0
    JWD WT_TEXT, 0, 12, AN(JN_BBOT,6), 0, jui_t_x
    JWD WT_NUM, JS_AX, 24, AN(JN_BBOT,6), 32, 0
    JWD WT_TEXT, 0, 64, AN(JN_BBOT,6), 0, jui_t_raw
    JWD WT_NUM, JS_RAW, 96, AN(JN_BBOT,6), 40, 0
    JWD WT_TEXT, 0, 12, AN(JN_BBOT,16), 0, jui_t_y
    JWD WT_NUM, JS_AX+1, 24, AN(JN_BBOT,16), 32, 0
    JWD WT_TEXT, 0, 64, AN(JN_BBOT,16), 0, jui_t_raw
    JWD WT_NUM, JS_RAW+1, 96, AN(JN_BBOT,16), 40, 0
    JWD WT_LAMP, 0, AN(JN_MID,-58), AN(JN_BBOT,11), 0, 0
    JWD WT_LAMP, 1, AN(JN_MID,-32), AN(JN_BBOT,11), 0, 0
    JWD WT_GROUP, 2, 4, AN(JN_LOW,0), AN(JN_SPL,-8), 118
%assign jui_i 0
%rep JOYT_MAX + 1
    JWD WT_RADIO, jui_i, 12, AN(JN_LOW,18+16*jui_i), AN(JN_SPL,-24), 12
  %assign jui_i jui_i + 1
%endrep
    JWD WT_GROUP, 3, AN(JN_SPL,4), AN(JN_LOW,0), AN(JN_SETW,0), 118
    JWD WT_TEXT, 0, AN(JN_SPL,12), AN(JN_LOW,20), 0, jui_t_dz
    JWD WT_BTN, JA_DZM, AN(JN_SPL,92), AN(JN_LOW,17), 16, 13
    JWD WT_NUM, JS_DZ, AN(JN_SPL,110), AN(JN_LOW,20), 32, 0
    JWD WT_BTN, JA_DZP, AN(JN_SPL,144), AN(JN_LOW,17), 16, 13
    JWD WT_CHECK, 0, AN(JN_SPL,12), AN(JN_LOW,38), AN(JN_SETW,-16), 12
    JWD WT_CHECK, 1, AN(JN_SPL,12), AN(JN_LOW,56), AN(JN_SETW,-16), 12
    JWD WT_BTN, JA_CAL, AN(JN_W,-189), AN(JN_H,-34), 112, 16
    JWD WT_BTN, JA_RESET, AN(JN_W,-69), AN(JN_H,-34), 64, 16
    JWD WT_DTXT, JD_STATUS, 4, AN(JN_H,-10), AN(JN_W,-120), 0
    JWD WT_NUM, JS_TIME, AN(JN_W,-108), AN(JN_H,-10), 104, 0
jui_l_wn_e:

; --- the wide stick B group, per type (jui_tw_tab) ------------------------------
jui_w_2x2:
    JWD WT_BOX, 1, AN(JN_BXB,0), JUI_BTOP, AN(JN_SIDE,0), 0
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BBOT,6), 0, jui_t_x
    JWD WT_NUM, JS_AX+2, AN(JN_MID,24), AN(JN_BBOT,6), 32, 0
    JWD WT_TEXT, 0, AN(JN_MID,64), AN(JN_BBOT,6), 0, jui_t_raw
    JWD WT_NUM, JS_RAW+2, AN(JN_MID,96), AN(JN_BBOT,6), 40, 0
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BBOT,16), 0, jui_t_y
    JWD WT_NUM, JS_AX+3, AN(JN_MID,24), AN(JN_BBOT,16), 32, 0
    JWD WT_TEXT, 0, AN(JN_MID,64), AN(JN_BBOT,16), 0, jui_t_raw
    JWD WT_NUM, JS_RAW+3, AN(JN_MID,96), AN(JN_BBOT,16), 40, 0
jui_w_2b:
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BMID,-8), 0, jui_t_nu1
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BMID,2), 0, jui_t_nu2
jui_w_4b:
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BMID,-8), 0, jui_t_fb1
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BMID,2), 0, jui_t_fb2
jui_w_hatr:
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BBOT,6), 0, jui_t_hatr
    JWD WT_NUM, JS_RAW+3, AN(JN_MID,116), AN(JN_BBOT,6), 40, 0
jui_w_hat:
    JWD WT_TEXT, 0, AN(JN_C3,2), JUI_BTOP, 0, jui_t_hat
    JWD WT_HAT, 0, AN(JN_C3,0), AN(JN_BMID,-14), 0, 0
    JWD WT_DTXT, JD_HAT, AN(JN_C3,6), AN(JN_BMID,18), 16, 0
jui_w_fly:
    JWD WT_TEXT, 0, AN(JN_C1,-24), JUI_BTOP, 0, jui_t_thr
    JWD WT_BAR, 0, AN(JN_C1,0), 34, 16, AN(JN_SIDE,-24)
    JWD WT_NUM, JS_THR, AN(JN_C1,-8), AN(JN_SIDE,13), 32, 0
    JWD WT_TEXT, 0, AN(JN_C2,-16), JUI_BTOP, 0, jui_t_rud
    JWD WT_BAR, 1, AN(JN_C2,0), 34, 16, AN(JN_SIDE,-24)
    JWD WT_NUM, JS_RUD, AN(JN_C2,-8), AN(JN_SIDE,13), 32, 0
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BBOT,6), 0, jui_t_thrr
    JWD WT_NUM, JS_RAW+3, AN(JN_MID,116), AN(JN_BBOT,6), 40, 0
    JWD WT_TEXT, 0, AN(JN_MID,12), AN(JN_BBOT,16), 0, jui_t_rudr
    JWD WT_NUM, JS_RAW+2, AN(JN_MID,116), AN(JN_BBOT,16), 40, 0
jui_w_end:
jui_tw_tab:
    dw jui_w_2b, jui_w_4b       ; 2B
    dw jui_w_2x2, jui_w_2b      ; 2X2B
    dw jui_w_4b, jui_w_hatr     ; 4B
    dw jui_w_fly, jui_w_end     ; FLIGHT
    dw jui_w_hat, jui_w_end     ; CHFS: the hat, then the flight bars
    dw jui_w_hatr, jui_w_fly    ; TMFCS: the hat's raw count and the hat
jui_l_wt:                       ; ...and stick B's two lamps, on every type
    JWD WT_LAMP, 2, AN(JN_W,-62), AN(JN_BBOT,11), 0, 0
    JWD WT_LAMP, 3, AN(JN_W,-36), AN(JN_BBOT,11), 0, 0
jui_l_wt_e:

; --- wide, calibrating (SPEC.md 137.6.5) ----------------------------------------
jui_l_wc:
    JWD WT_RECT, CBLUE, 4, 4, AN(JN_W,-8), 2
    JWD WT_DTXT, JD_BAN1, 4, 6, AN(JN_W,-8), 0
    JWD WT_DTXT, JD_BAN2, 4, 14, AN(JN_W,-8), 0
    JWD WT_RECT, CBLUE, 4, 22, AN(JN_W,-8), 2
    JWD WT_GROUP, 0, 4, 30, AN(JN_W,-8), AN(JN_H,-114)
    JWD WT_BOX, 2, 12, 48, AN(JN_SIDE,0), 0
    JWD WT_TEXT, 0, AN(JN_BXA,0), 52, 0, jui_t_capt
    JWD WT_TEXT, 0, AN(JN_BXA,0), 66, 0, jui_t_hdrw
    JWD WT_GROUP, 4, 4, AN(JN_H,-76), AN(JN_W,-8), 36
    JWD WT_STEP, 1, 12, AN(JN_H,-58), 136, 12
    JWD WT_STEP, 2, AN(JN_C1,0), AN(JN_H,-58), 136, 12
    JWD WT_STEP, 3, AN(JN_C2,0), AN(JN_H,-58), 136, 12
    JWD WT_BTN, JA_CANCEL, AN(JN_W,-141), AN(JN_H,-34), 64, 16
    JWD WT_BTN, JA_BACK, AN(JN_W,-69), AN(JN_H,-34), 64, 16
    JWD WT_DTXT, JD_STATUS, 4, AN(JN_H,-10), AN(JN_W,-120), 0
    JWD WT_NUM, JS_TIME, AN(JN_W,-108), AN(JN_H,-10), 104, 0
jui_l_wc_e:

; the calibration table's row, four widgets: label, now, least, most (jui_rows
; sets the y, the axis and the label)
jui_r_w:
    JWD WT_TEXT, 0, AN(JN_BXA,0), 0, 0, 0
    JWD WT_NUM, JS_PRAW, AN(JN_BXA,32), 0, 40, 0
    JWD WT_NUM, JS_PMIN, AN(JN_BXA,80), 0, 40, 0
    JWD WT_NUM, JS_PMAX, AN(JN_BXA,128), 0, 40, 0
jui_r_c:
    JWD WT_TEXT, 0, 58, 0, 0, 0
    JWD WT_NUM, JS_PRAW, 74, 0, 40, 0
    JWD WT_NUM, JS_PMIN, 114, 0, 40, 0
    JWD WT_NUM, JS_PMAX, 154, 0, 40, 0

; =============================================================================
; WORDS. Every compact string fits its field: 27 cells is the pane
; =============================================================================
jui_t_x:     db 'X', 0
jui_t_y:     db 'Y', 0
jui_t_b:     db 'B', 0
jui_t_t:     db 'T', 0
jui_t_r:     db 'R', 0
jui_t_raw:   db 'raw', 0
jui_t_type:  db 'Type:', 0
jui_t_dz:    db 'Dead zone', 0
jui_t_hat:   db 'Hat', 0
jui_t_thr:   db 'Throttle', 0
jui_t_rud:   db 'Rudder', 0
jui_t_thrr:  db 'Throttle raw', 0
jui_t_rudr:  db 'Rudder raw', 0
jui_t_hatr:  db 'Hat raw', 0
jui_t_nu1:   db 'Not used by this', 0
jui_t_nu2:   db 'controller type', 0
jui_t_fb1:   db 'Buttons 3 and 4', 0
jui_t_fb2:   db 'belong to stick A', 0
jui_t_nc:    db 'Not connected', 0
jui_t_capt:  db 'Captured range', 0
jui_t_hdrw:  db 'Axis  Now   Min   Max', 0
jui_t_hdrc:  db '    Now  Min  Max', 0
jui_gcap:    dw jui_t_ga, jui_t_gb, jui_t_gc, jui_t_gs, jui_t_gp
jui_t_ga:    db ' STICK A', 0
jui_t_gb:    db ' STICK B', 0
jui_t_gc:    db ' CONTROLLER', 0
jui_t_gs:    db ' SETTINGS', 0
jui_t_gp:    db ' PROGRESS', 0
jui_t_cal:   db 'Calibrate...', 0
jui_t_reset: db 'Reset', 0
jui_t_minus: db '-', 0
jui_t_plus:  db '+', 0
jui_t_gt:    db '>', 0
jui_t_cancel: db 'Cancel', 0
jui_t_back:  db 'Back', 0
jui_t_ms:    db ' ms a read', 0
jui_t_msc:   db 'ms', 0
jui_axn:     dw jui_t_x1, jui_t_y1, jui_t_x2, jui_t_y2
jui_t_x1:    db 'X1', 0
jui_t_y1:    db 'Y1', 0
jui_t_x2:    db 'X2', 0
jui_t_y2:    db 'Y2', 0

jui_tfull:   dw jui_n0, jui_n1, jui_n2, jui_n3, jui_n4, jui_n5
jui_n0:      db '2-button joystick', 0
jui_n1:      db 'Two 2-button joysticks', 0
jui_n2:      db '4-button joystick', 0
jui_n3:      db 'Flight stick (throttle, rudder)', 0
jui_n4:      db 'CH FlightStick Pro (hat)', 0
jui_n5:      db 'ThrustMaster FCS (hat)', 0
jui_tshort:  dw jui_s0, jui_s1, jui_s2, jui_s3, jui_s4, jui_s5
jui_s0:      db '2-button stick', 0
jui_s1:      db 'Two 2-btn sticks', 0
jui_s2:      db '4-button stick', 0
jui_s3:      db 'Flight stick', 0
jui_s4:      db 'CH FlightStick Pro', 0
jui_s5:      db 'ThrustMaster FCS', 0
%if JOYT_MAX != 5
  %error "joyui: one name per JOYT_*"
%endif
jui_chkl:    dw jui_k0, jui_k1, jui_k2, jui_k3
jui_k0:      db 'Invert Y axis', 0
jui_k1:      db 'Swap sticks A and B', 0
jui_k2:      db 'Invert Y', 0
jui_k3:      db 'Swap A/B', 0
jui_stepl:   dw jui_p1, jui_p2, jui_p3
jui_p1:      db '1 Upper left', 0
jui_p2:      db '2 Lower right', 0
jui_p3:      db '3 Centre', 0
jui_hatn:    dw jui_h0, jui_h1, jui_h2, jui_h3, jui_h4, jui_h5, jui_h6
             dw jui_h7, jui_h8
jui_h0:      db 'NW', 0
jui_h1:      db 'N', 0
jui_h2:      db 'NE', 0
jui_h3:      db 'W', 0
jui_h4:      db '--', 0
jui_h5:      db 'E', 0
jui_h6:      db 'SW', 0
jui_h7:      db 'S', 0
jui_h8:      db 'SE', 0

; the banner, per step and line: wide, compact
jui_ban:     dw jui_b1a, jui_b1c, jui_bba, jui_bbc
             dw jui_b2a, jui_b2c, jui_bba, jui_bbc
             dw jui_b3a, jui_b3c, jui_b3b, jui_b3d
jui_b1a:     db ' Step 1 of 3: Move the stick to the UPPER LEFT', 0
jui_b2a:     db ' Step 2 of 3: Move the stick to the LOWER RIGHT', 0
jui_bba:     db ' corner and press a stick button (or Enter)', 0
jui_b3a:     db ' Step 3 of 3: Let the stick go to the CENTRE', 0
jui_b3b:     db ' and press a stick button (or Enter)', 0
jui_b1c:     db ' Step 1 of 3: UPPER LEFT', 0
jui_b2c:     db ' Step 2 of 3: LOWER RIGHT', 0
jui_bbc:     db ' corner, then a button', 0
jui_b3c:     db ' Step 3 of 3: CENTRE', 0
jui_b3d:     db ' let go, then a button', 0

; the status line (SPEC.md 137.6.6): wide, compact
jui_st_none:  dw jui_q0, jui_q0c
jui_st_nob:   dw jui_q1, jui_q1c
jui_st_noa:   dw jui_q2, jui_q2c
jui_st_nothr: dw jui_q3, jui_q3c
jui_st_nohat: dw jui_q4, jui_q4c
jui_st_uncal: dw jui_q5, jui_q5c
jui_st_two:   dw jui_q6, jui_q6
jui_st_fly:   dw jui_q7, jui_q7c
jui_st_hat:   dw jui_q8, jui_q8
jui_st_one:   dw jui_q9, jui_q9
jui_q0:      db 'No joystick connected - looking once a second', 0
jui_q0c:     db 'No joystick connected', 0
jui_q1:      db 'Stick A found, stick B not connected', 0
jui_q1c:     db 'Stick B not connected', 0
jui_q2:      db 'Stick B found, stick A not connected', 0
jui_q2c:     db 'Stick A not connected', 0
jui_q3:      db 'Stick A found, no throttle or rudder', 0
jui_q3c:     db 'No throttle or rudder', 0
jui_q4:      db 'Stick A found, no hat', 0
jui_q4c:     db 'No hat', 0
jui_q5:      db 'Not calibrated - press Calibrate...', 0
jui_q5c:     db 'Not calibrated', 0
jui_q6:      db 'Sticks A and B found', 0
jui_q7:      db 'Stick, throttle and rudder found', 0
jui_q7c:     db 'Stick+throttle found', 0
jui_q8:      db 'Stick A and hat found', 0
jui_q9:      db 'Stick A found', 0

; --- the one button record (SPEC.md 20.5.1.3): the page draws its buttons one
; at a time, so the record's rect, label and flags are staged per button
    OS88UI_BTNREC jui_brec, jui_brect, jui_blbl, jui_bflg, 1

; --- the shared controls and the decimal text ----------------------------------
%define OS88UI_ARM              ; arm / fire / armed: the press and release
%define OS88UI_NOGEST           ; ...driven here (joyui_click/_up/_drag)
%define OS88UI_NOBFIND          ; ...and the hit test is the widget list's
%include "os88ui.inc"
%define DEC_S16
%include "os88dec.inc"

; =============================================================================
; THE PAGE'S STATE - zero at load, expanded below joy.asm's OS88_STATE
; =============================================================================
%macro JOYUI_STATE 0
jui_px:     dw 0                ; the pane's origin, banked per call
jui_py:     dw 0
jui_w:      dw 0                ; ...its size, from the paint (0 = lay out)
jui_h:      dw 0
jui_win:    dw 0                ; the panel's window
jui_nw:     dw 0                ; widgets in the list
jui_ntail:  dw 0                ; ...the first of the type's own
jui_down:   dw 0                ; the control held (a widget), 0 none
jui_cur:    dw 0                ; the widget being asked
jui_tshow:  dw 0                ; the read time shown, tenths of a ms
jui_trect:  times 4 dw 0        ; the type's own widgets' ground
jui_anch:   times 16 dw 0       ; the anchors (JN_*)
jui_cmin:   times 4 dw 0        ; calibrating: per port axis, the least seen
jui_cmax:   times 4 dw 0        ; ...the most
jui_ccen:   times 4 dw 0        ; ...and the centre confirmed
jui_bx0:    dw 0                ; the box being drawn: x, y, side, centre,
jui_by0:    dw 0                ; the dead zone's half side
jui_bs:     dw 0
jui_bc:     dw 0
jui_hz:     dw 0
jui_dh:     dw 0                ; jui_dash: 0 a row
jui_new:    dw 0
jui_old:    dw 0
jui_ty:     dw 0
jui_lcol:   db 0, 0             ; the lines' colour, then the dot's and bars'
jui_brect:  times 4 dw 0        ; the staged button
jui_blbl:   dw 0
jui_bflg:   dw 0
jui_icon:   times 2 + 4 * JUI_DOT db 0  ; the dot's erase record
jui_num:    times 16 db 0       ; a number, formatted
jui_txt:    times JUI_TXTMAX db 0   ; a run, padded
jui_force:  db 0
jui_mono:   db 0                ; 1 = a 1bpp display
jui_tdis:   db 0                ; the next run is greyed
jui_step:   db 0                ; calibrating: 1..3, 0 not
jui_bprev:  db 0                ; the port's buttons at the last sample
jui_tn:     db 0                ; samples since the time was shown
jui_hc:     db 0
jui_tmpb:   db 0
jui_wl:     times JUI_NW * JW_SZ db 0   ; THE WIDGETS
%endmacro
