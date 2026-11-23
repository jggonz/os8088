; =============================================================================
; os8088 - cpw_thm.inc
;
; The wide Theme page (SPEC.md 135.11), assembled into CPWIDE.DRV after the
; chrome (cpwide.inc %includes it). Prefix `cpt_`. ES = the asset claim, DS =
; KERNEL_SEG, the gfx lock held, every coordinate CONTENT-relative (the kit's
; contract, SPEC.md 135.5). Internal procs clobber what they like but ES, DS.
;
; WHAT IS DRAWN. Four preview cards, the eight Color presets, an Effects group
; (window animations, SPEC.md 11.99) and a Screen Saver group. Cards, swatches
; and the saver's picture are ONE table-driven rect interpreter (cpt_elems):
; a row of five bytes is a colour and a rect whose coordinates are relative to
; an origin, bit 7 meaning "from the far edge". A colour byte is a field of
; the 10-byte theme row copied into the working area (0-9), or 80h + a colour.
;
; WHAT A CLICK DRAWS: nothing of the cards or the swatches. cp_thm_apply /
; cp_thm_pre ask cp_repost, ui_task repaints every window - this one through
; CWV_PAINT - so the selection frame moves with the world, once.
; =============================================================================

%ifdef KERN_BIG
section .modv

CPT_TOP     equ 52              ; the cards' top
CPT_L       equ CPW_PX + 8      ; the groups' and the first card's left

; --- the page's working bytes: the page block (CPW_W_PAGE, 80), this page's
; --- whole while it shows (SPEC.md 135.5.1); rebuilt on every paint ---------
CPT_W       equ CPW_W_PAGE + 0  ; dw card width
CPT_H       equ CPW_W_PAGE + 2  ; dw card height
CPT_TG      equ CPW_W_PAGE + 4  ; dw the presets group's top
CPT_TE      equ CPW_W_PAGE + 6  ; dw the Effects / Screen Saver groups' top
CPT_GW      equ CPW_W_PAGE + 8  ; dw the half groups' width
CPT_SX      equ CPW_W_PAGE + 10 ; dw the Screen Saver group's left
CPT_X0      equ CPW_W_PAGE + 12 ; dw the interpreter's origin x ...
CPT_Y0      equ CPW_W_PAGE + 14 ; dw ... y
CPT_R       equ CPW_W_PAGE + 16 ; dw the far edge: x of the last column ...
CPT_B       equ CPW_W_PAGE + 18 ; dw ... y of the last row (origin-relative)
CPT_PX      equ CPW_W_PAGE + 20 ; dw a press: x ...
CPT_PY      equ CPW_W_PAGE + 22 ; dw ... y
CPT_TX      equ CPW_W_PAGE + 24 ; dw a card label's left
CPT_TW      equ CPW_W_PAGE + 26 ; dw ... and its width
CPT_COL     equ CPW_W_PAGE + 28 ; db the colour byte being drawn
CPT_CI      equ CPW_W_PAGE + 29 ; db the card being drawn
CPT_ROW     equ CPW_W_PAGE + 30 ; 10 bytes: the theme row being drawn
%if CPT_ROW + 10 > CPW_W_PAGE + CPW_W_PAGEN
  %error "cpw_thm: the Theme page outgrew the page's working bytes"
%endif

CPT_ID_CUST equ CPW_ID0 + 1     ; the armed ids: Customize... and Settings...
CPT_ID_SAVE equ CPW_ID0 + 2

R_          equ 80h             ; "from the far edge", in a table's coordinate

; -----------------------------------------------------------------------------
; cpt_geo - the page's geometry from the content box (SPEC.md 135.11)
; -----------------------------------------------------------------------------
cpt_geo:
    mov ax, [es:CPW_W_CW]
    sub ax, CPT_L + 32          ; (CW - 161) / 4
    shr ax, 1
    shr ax, 1
    mov [es:CPT_W], ax
    mov bx, [es:CPW_W_CH]
    sub bx, CPW_H1 - TITLE_H - 1
    shr bx, 1
    add bx, 84
    mov [es:CPT_H], bx
    add bx, CPT_TOP + 8
    mov [es:CPT_TG], bx
    add bx, 68
    mov [es:CPT_TE], bx
    mov ax, [es:CPW_W_CW]
    sub ax, CPT_L + 16          ; (CW - 145) / 2
    shr ax, 1
    mov [es:CPT_GW], ax
    add ax, CPT_L + 8
    mov [es:CPT_SX], ax
    ret

; -----------------------------------------------------------------------------
; cpw_thm_paint - the whole page (cpw_page's, while [cp_sel] is CP_ITHM)
; -----------------------------------------------------------------------------
cpw_thm_paint:                  ; (cpw_page has cleared CPW_D_ON: the page
                                ; block is this page's now, SPEC.md 135.5.1)
    call cpw_sty                ; the ground, the page area to the status rule
    mov dx, ax
    dec dx
    dec dx
    mov byte [gfx_color], CWHITE
    mov ax, CPW_PX
    mov bx, CPW_BY
    mov cx, [es:CPW_W_CW]
    dec cx
    call cpw_fill
    call cpt_geo
    xor bx, bx                  ; --- the four cards
.card:
    push bx
    mov ax, [es:CPT_W]
    add ax, 8
    mul bx
    add ax, CPT_L
    mov [es:CPT_X0], ax
    mov al, bl
    call cpt_card
    pop bx
    inc bx
    cmp bx, 4
    jb .card
    mov ax, CPT_L               ; --- COLOR PRESETS
    mov bx, [es:CPT_TG]
    mov cx, [es:CPW_W_CW]
    sub cx, 9
    mov dx, bx
    add dx, 59
    mov si, cpt_s_pre
    call cpk_group
    xor si, si
.sw:
    push si                     ; swatch SI: its row into the scratch
    mov ax, si
    mov cl, THM_PRESET_ROWB
    mul cl
    add ax, cpt_pre
    mov si, ax
    push ds
    push cs
    pop ds
    mov di, CPT_ROW
    mov cx, THM_PRESET_ROWB
    cld
    rep movsb
    pop ds
    pop si
    mov ax, si                  ; origin = its 36x40 cell
    mov cx, 38
    mul cx
    add ax, CPT_L + 10
    mov [es:CPT_X0], ax
    mov ax, [es:CPT_TG]
    add ax, 16
    mov [es:CPT_Y0], ax
    mov word [es:CPT_R], 35
    mov word [es:CPT_B], 39
    push si
    mov bx, cpt_swbody
    cmp byte [thm_kind], THM_COLOR
    jb .nr                      ; Bright / Dark: no preset is live
    mov ax, si
    cmp al, [drv_cfg + CFG_THMP]
    jne .nr
    mov bx, cpt_swring          ; ...the live one wears the ring first
.nr:
    mov si, bx
    call cpt_elems
    pop si
    inc si
    cmp si, THM_PRESET_N
    jb .sw
    xor al, al
    call cpt_btn
    xor di, di
    call cpk_button             ; Customize...
%ifdef WM_ANIM
    mov ax, CPT_L               ; --- Effects
    mov bx, [es:CPT_TE]
    mov cx, [es:CPT_GW]
    add cx, CPT_L - 1
    mov dx, bx
    add dx, 55
    mov si, cpt_s_eff
    call cpk_group
    mov cx, CPT_L + 8
    mov dx, [es:CPT_TE]
    add dx, 18
    mov si, cpt_s_anim
    mov ax, (CWHITE << 8) | CBLACK
    xor bl, bl
    call cpw_run
    call cpt_tabs
%endif
    mov ax, [es:CPT_SX]         ; --- Screen Saver
    mov bx, [es:CPT_TE]
    mov cx, [es:CPW_W_CW]
    sub cx, 9
    mov dx, bx
    add dx, 55
    mov si, cpt_s_sav
    call cpk_group
    mov ax, [es:CPT_SX]
    add ax, 8
    mov [es:CPT_X0], ax
    mov ax, [es:CPT_TE]
    add ax, 18
    mov [es:CPT_Y0], ax
    mov word [es:CPT_R], 95
    mov word [es:CPT_B], 33
    mov si, cpt_sav
    call cpt_elems
    mov al, 1
    call cpt_btn
    xor di, di
    jmp cpk_button              ; Settings...

%ifdef WM_ANIM
; cpt_tabs - the On / Off strip, the live choice barred
cpt_tabs:
    mov cx, CPT_L + 8
    mov dx, [es:CPT_TE]
    add dx, 32
    mov si, cpt_s_tabs
    mov al, 1
    sub al, [wm_animon]
    jmp cpk_tabs
%endif

; -----------------------------------------------------------------------------
; cpt_card - one preview card
; in:  AL = 0..3 (a THM_*), [CPT_X0] = its left; its top is CPT_TOP
; The ring (blue 2px when it is the live theme, black 1px), the miniature, the
; name. The row is thm_tab's, or the live Color row for Color and Windows.
; -----------------------------------------------------------------------------
cpt_card:
    mov [es:CPT_CI], al
    mov word [es:CPT_Y0], CPT_TOP
    mov ax, [es:CPT_W]
    sub ax, 3
    mov [es:CPT_R], ax
    mov ax, [es:CPT_H]
    sub ax, 19
    mov [es:CPT_B], ax
    mov al, [es:CPT_CI]
    cmp al, THM_COLOR
    jbe .r
    mov al, THM_COLOR           ; a Windows card wears the Color row
.r:
    mov ah, THM_ROW_BYTES
    mul ah
    add ax, thm_tab
    mov si, ax
    mov di, CPT_ROW
    mov cx, THM_ROW_BYTES
    cld
    rep movsb                   ; DS = the kernel's, ES = the claim's
    mov byte [es:CPT_ROW + 9], CWHITE   ; byte 9 (the menu's XOR mask) is the
    cmp byte [es:CPT_CI], THM_WIN       ; window FACE here: white, grey on
    jne .face
    mov byte [es:CPT_ROW + 9], CLGRAY   ; the Windows card
.face:
    mov al, CBLACK              ; --- the ring
    mov bp, 1
    mov ah, [es:CPT_CI]
    cmp ah, [thm_kind]
    jne .ring
    mov al, CBLUE
    inc bp
.ring:
    mov [gfx_color], al
    mov si, [es:CPT_X0]
    mov di, [es:CPT_W]
    add di, si
    dec di
    mov ax, si
    mov bx, CPT_TOP
    mov cx, di
    mov dx, bp
    add dx, CPT_TOP - 1
    call cpw_fill               ; top
    mov bx, [es:CPT_H]
    add bx, CPT_TOP
    mov dx, bx
    dec dx
    sub bx, bp
    call cpw_fill               ; bottom
    mov bx, CPT_TOP
    mov cx, si
    add cx, bp
    dec cx
    call cpw_fill               ; left
    mov ax, di
    sub ax, bp
    inc ax
    mov cx, di
    call cpw_fill               ; right
    mov si, cpt_ct              ; --- the miniature
    call cpt_elems
    cmp byte [es:CPT_CI], THM_WIN
    jne .name
    call cpt_elems              ; ...and the Windows card's taskbar
.name:
    xor bh, bh                  ; --- its name, centred
    mov bl, [es:CPT_CI]
    mov dl, [cs:cpt_nlen + bx]
    xor dh, dh
    shl dx, 1
    shl dx, 1
    shl dx, 1
    mov [es:CPT_TW], dx
    mov ax, [es:CPT_W]
    sub ax, dx
    shr ax, 1
    add ax, [es:CPT_X0]
    mov [es:CPT_TX], ax
    mov dx, [es:CPT_H]
    add dx, CPT_TOP - 14        ; the text's top; the label is 16 rows
    shl bx, 1
    mov si, [cs:cpt_nm + bx]
    mov ax, (CWHITE << 8) | CBLACK
    mov bl, [es:CPT_CI]
    cmp bl, [thm_kind]
    jne .run
    push si                     ; the live one: a blue bar - the four spans
    push dx                     ; round the run, then the run on blue
    mov byte [gfx_color], CBLUE
    mov bx, dx                  ; DX = BX = the text's top
    mov ax, [es:CPT_X0]
    add ax, 2
    mov cx, [es:CPT_X0]
    add cx, [es:CPT_W]
    sub cx, 3
    sub bx, 4
    dec dx
    call cpw_fill               ; above
    pop dx
    push dx
    mov bx, dx
    add bx, 8
    add dx, 11
    call cpw_fill               ; below
    pop dx
    mov bx, dx
    add dx, 7
    mov cx, [es:CPT_TX]
    dec cx
    call cpw_fill               ; left of the text
    mov ax, [es:CPT_TX]
    add ax, [es:CPT_TW]
    mov cx, [es:CPT_X0]
    add cx, [es:CPT_W]
    sub cx, 3
    call cpw_fill               ; right of it
    sub dx, 7                   ; (DX = the text's top again)
    pop si
    mov ax, (CBLUE << 8) | CWHITE
.run:
    mov cx, [es:CPT_TX]
    xor bl, bl
    jmp cpw_run

; -----------------------------------------------------------------------------
; cpt_elems - the rect interpreter
; in:  SI = a table in the image (CS), [CPT_X0/Y0/R/B] the origin and the far
;      edge, [CPT_ROW] the theme row. Rows of five bytes: colour, x1, y1, x2,
;      y2; a colour byte of FFh ends the table
; out: SI = past the terminator
; -----------------------------------------------------------------------------
cpt_elems:
    mov al, [cs:si]
    inc si
    cmp al, 0xFF
    je .end
    mov [es:CPT_COL], al
    mov bx, [es:CPT_R]
    mov dx, [es:CPT_X0]
    mov al, [cs:si]
    inc si
    call cpt_dec
    push ax                     ; x1
    mov bx, [es:CPT_B]
    mov dx, [es:CPT_Y0]
    mov al, [cs:si]
    inc si
    call cpt_dec
    push ax                     ; y1
    mov bx, [es:CPT_R]
    mov dx, [es:CPT_X0]
    mov al, [cs:si]
    inc si
    call cpt_dec
    push ax                     ; x2
    mov bx, [es:CPT_B]
    mov dx, [es:CPT_Y0]
    mov al, [cs:si]
    inc si
    call cpt_dec
    mov dx, ax
    pop cx
    pop bx
    pop ax
    call cpt_rect
    jmp cpt_elems
.end:
    ret

; cpt_dec - AL = a coordinate byte, BX = the far edge, DX = the origin
;           -> AX = the content-relative coordinate
cpt_dec:
    xor ah, ah
    test al, R_
    jz .p
    and al, 7Fh
    neg ax
    add ax, bx
.p:
    add ax, dx
    ret

; cpt_rect - fill AX/BX/CX/DX in [CPT_COL]'s colour (a row field, FFh = the
;            50% dither desktop, or 80h + a colour)
cpt_rect:
    push bx
    mov bl, [es:CPT_COL]
    test bl, 80h
    jz .fld
    and bl, 0Fh
    jmp short .pen
.fld:
    xor bh, bh
    mov bl, [es:CPT_ROW + bx]
    cmp bl, THM_DITHER
    je .gray
.pen:
    mov [gfx_color], bl
    pop bx
    jmp cpw_fill
.gray:
    pop bx
    add ax, [es:CPW_W_OX]
    add cx, [es:CPW_W_OX]
    add bx, [es:CPW_W_OY]
    add dx, [es:CPW_W_OY]
    call KERNEL_SEG:cw_gfx_fill_gray
    ret

; -----------------------------------------------------------------------------
; cpt_btn - a button's rect and label. AL = 0 Customize..., 1 Settings...
; out: AX/BX/CX/DX = the rect, SI = the label (an image string)
; -----------------------------------------------------------------------------
cpt_btn:
    or al, al
    jnz .set
    mov cx, [es:CPW_W_CW]
    sub cx, 17
    mov ax, cx
    sub ax, 103
    mov bx, [es:CPT_TG]
    add bx, 27
    mov si, cpt_s_cust
    jmp short .y
.set:
    mov ax, [es:CPT_SX]
    add ax, 116
    mov cx, ax
    add cx, 95
    mov bx, [es:CPT_TE]
    add bx, 26
    mov si, cpt_s_set
.y:
    mov dx, bx
    add dx, 17
    ret

; cpt_hit - is the press ([CPT_PX/PY]) inside AX/BX/CX/DX? CF = 0 inside
cpt_hit:
    cmp [es:CPT_PX], ax
    jl .n
    cmp [es:CPT_PY], bx
    jl .n
    cmp [es:CPT_PX], cx
    jg .n
    cmp [es:CPT_PY], dx
    jg .n
    clc
    ret
.n:
    stc
    ret

; -----------------------------------------------------------------------------
; cpw_thm_click - a press on the page
; in:  AX/BX = the point, content-relative (cpw_click's); ES = the claim
; A card and a swatch act on the press and draw nothing (the repaint the theme
; change posts does); a button is armed, drawn pressed and fires on the release
; -----------------------------------------------------------------------------
cpw_thm_click:
    cmp word [es:CPW_W_CW], CPW_W1 - 2
    jb .r
    mov [es:CPT_PX], ax
    mov [es:CPT_PY], bx
    call cpt_geo
    mov al, 0                   ; --- the two buttons
.b:
    push ax
    call cpt_btn
    call cpt_hit
    jc .nb
    mov di, OS88UI_DOWN
    call cpk_button
    pop ax
    mov ah, 0
    add ax, CPT_ID_CUST
    CPWC os88ui_arm
    ret
.nb:
    pop ax
    inc al
    cmp al, 2
    jb .b
    mov ax, [es:CPT_PX]
    mov bx, [es:CPT_PY]
    sub ax, CPT_L
    jl .r
    mov cx, [es:CPT_H]          ; --- a card
    add cx, CPT_TOP
    cmp bx, CPT_TOP
    jl .pre
    cmp bx, cx
    jge .pre
    xor dx, dx
    mov cx, [es:CPT_W]
    add cx, 8
    div cx
    cmp ax, 4
    jae .r
    cmp dx, [es:CPT_W]
    jae .r                      ; the gap
    mov ah, CPG_THMSET
    jmp cpw_gate
.pre:
    mov bx, [es:CPT_PY]         ; --- a swatch
    mov cx, [es:CPT_TG]
    add cx, 16
    cmp bx, cx
    jl .eff
    add cx, 39
    cmp bx, cx
    jg .eff
    mov ax, [es:CPT_PX]
    sub ax, CPT_L + 10
    jl .r
    xor dx, dx
    mov cx, 38
    div cx
    cmp ax, THM_PRESET_N
    jae .r
    mov bx, ax                  ; BX = the preset
    cmp byte [thm_kind], THM_COLOR
    jb .inst
    cmp bl, [drv_cfg + CFG_THMP]
    je .r                       ; the live preset: nothing to change
.inst:
    mov al, THM_PRESET_ROWB     ; the row over the Color row, its number into
    mul bl                      ; 'TP' - what tv_install does (SPEC.md 135.11)
    add ax, cpt_pre
    mov si, ax
    push ds
    pop es
    push cs
    pop ds
    mov di, THM_CROW
    mov cx, THM_PRESET_ROWB
    cld
    rep movsb
    push es
    pop ds                      ; DS = the kernel's again
    mov [drv_cfg + CFG_THMP], bl
    mov es, [cs:cpw_dseg]       ; ES = the claim again
    mov ah, CPG_THMPRE
    jmp cpw_gate
%ifdef WM_ANIM
.eff:
    mov bx, [es:CPT_TE]         ; --- the animation strip
    add bx, 32
    cmp [es:CPT_PY], bx
    jl .r
    add bx, CPK_TABH - 1
    cmp [es:CPT_PY], bx
    jg .r
    mov cx, [es:CPT_PX]
    sub cx, CPT_L + 8
    mov si, cpt_s_tabs
    call cpk_tabhit             ; AL = the tab (0 On, 1 Off)
    jc .r
    xor al, 1                   ; ...as [wm_animon]
    cmp al, [wm_animon]
    je .r
    mov [wm_animon], al
    mov ah, CPG_WDSET           ; SYSTEM.CFG owed; nothing on screen to repaint
    call cpw_gate
    jmp cpt_tabs
%else
.eff:
%endif
.r:
    ret

; -----------------------------------------------------------------------------
; cpw_thm_up - the release of a wide control on this page
; in:  AX = what was armed (os88ui_fire's), CX/DX = the SCREEN point
; -----------------------------------------------------------------------------
cpw_thm_up:
    mov bx, ax
    sub bx, CPT_ID_CUST
    cmp bx, 1
    ja .r
    call cpw_org
    sub cx, [es:CPW_W_OX]
    sub dx, [es:CPW_W_OY]
    mov [es:CPT_PX], cx
    mov [es:CPT_PY], dx
    push bx                     ; (the index: cpt_geo spends BX)
    call cpt_geo
    pop bx
    push bx
    mov al, bl
    call cpt_btn
    push ax
    push bx
    push cx
    push dx
    xor di, di
    call cpk_button             ; the press drew it down: up again, always
    pop dx
    pop cx
    pop bx
    pop ax
    call cpt_hit
    pop bx
    jc .r                       ; slid off: nothing
    mov ah, CPG_THMCUST
    or bl, bl
    jz .go
    mov ah, CPG_SAVER
.go:
    jmp cpw_gate
.r:
    ret

; --- the tables ---------------------------------------------------------------
; a card, origin = its top left, far edge = (w - 3, h - 19): colour field 5 the
; desktop, 0 the menu bar, 1 the ink, 3 / 4 the title, 9 the window face
cpt_ct:
    db 5,       2, 2,  R_|0, R_|0       ; the desktop
    db 0,       2, 2,  R_|0, 10         ; the menu bar...
    db 1,       2, 11, R_|0, 11         ; ...its rule
    db 1,       8, 5,  24, 6            ; ...and two words
    db 1,       32, 5, 46, 6
    db 1,       12, 16, R_|12, R_|6     ; the window's frame
    db 9,       13, 17, R_|13, R_|7     ; ...its face
    db 3,       13, 17, R_|13, 25       ; ...its title bar
    db 4,       24, 20, R_|24, 21       ; ...and the title
    db 80h|CDGRAY, 20, 31, R_|20, 32    ; two lines of content
    db 80h|CDGRAY, 20, 37, R_|44, 38
    db 1,       R_|36, R_|16, R_|18, R_|10  ; a button
    db 0xFF
    db 80h|CLGRAY, 2, R_|5, R_|0, R_|0  ; Windows: the taskbar,
    db 80h|CWHITE, 2, R_|5, R_|0, R_|5  ; its highlight,
    db 80h|CDGRAY, 4, R_|3, 18, R_|1    ; and a Start button
    db 0xFF
; a preset swatch, origin = its 36x40 cell
cpt_swring:
    db 80h|CBLUE, 0, 0, R_|0, 2
    db 80h|CBLUE, 0, R_|2, R_|0, R_|0
    db 80h|CBLUE, 0, 0, 2, R_|0
    db 80h|CBLUE, R_|2, 0, R_|0, R_|0
cpt_swbody:
    db 80h|CBLACK, 4, 4, R_|4, R_|4
    db 3,       5, 5,  R_|5, 14         ; the title colour
    db 5,       5, 15, R_|5, R_|5       ; the desktop's
    db 0xFF
; the Screen Saver's picture, origin = its top left, 96x34
cpt_sav:
    db 80h|CBLACK, 0, 0, R_|0, R_|0
    db 80h|CWHITE, 10, 6, 11, 7
    db 80h|CWHITE, 30, 22, 31, 23
    db 80h|CWHITE, 54, 10, 55, 11
    db 80h|CWHITE, 74, 26, 75, 27
    db 80h|CWHITE, 86, 6, 87, 7
    db 80h|CWHITE, 40, 15, 43, 16
    db 80h|CWHITE, 41, 14, 42, 17
    db 80h|CLGRAY, 20, 26, 20, 26
    db 80h|CLGRAY, 44, 4, 44, 4
    db 80h|CLGRAY, 64, 18, 64, 18
    db 80h|CLGRAY, 90, 20, 90, 20
    db 0xFF

cpt_pre:    THM_PRESET_ROWS         ; the eight rows (drivers/thmpal/thmrows.inc)
cpt_nm:     dw cpt_s_n0, cpt_s_n1, cpt_s_n2, cpt_s_n3
cpt_nlen:   db 6, 4, 5, 7
cpt_s_n0:   db 'Bright', 0
cpt_s_n1:   db 'Dark', 0
cpt_s_n2:   db 'Color', 0
cpt_s_n3:   db 'Windows', 0
cpt_s_pre:  db 'Color Presets', 0
cpt_s_cust: db 'Customize...', 0
cpt_s_eff:  db 'Effects', 0
cpt_s_anim: db 'Window animations', 0
cpt_s_tabs: db 'On', 0, 'Off', 0, 0
cpt_s_sav:  db 'Screen Saver', 0
cpt_s_set:  db 'Settings...', 0
%endif
