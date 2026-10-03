; =============================================================================
; os8088 - apps/pixel/pxui.inc
;
; PiXEL's WINDOW (SPEC.md 106.1 - 106.4): the layout, worked out from the live
; content box every time it is wanted, and one painter per region - toolbar,
; tool column, canvas, panel column, filmstrip, status bar.
;
; THE LAYOUT IS A FUNCTION OF THE WINDOW, NOT A COPY OF IT. px_layout reads
; OSAPI_WM_CONTENT, OSAPI_WM_GEOM and OSAPI_WM_DISPLAY and recomputes every
; rect from them; the paint proc calls it, and so does every press and every
; incremental draw. A window that MOVES does not call its paint proc (SPEC.md
; 11.96.12), so a rect banked on an earlier pass would describe somewhere the
; window used to be.
;
; EVERY PIXEL ONCE (CLAUDE.md performance rule 2). No region fills its ground
; and then draws on it. A text band is ONE opaque run padded to the band's
; width (px_tband), the ground beside a control is filled in strips that
; stop at the control's edge, and a button draws its own interior
; (os88ui_bdraw1).
;
; Every routine here preserves every register, takes no lock and arms no
; clip: it is called from inside W_PAINT, where the kernel holds both, or
; through px_regdraw, which arms the clip for a callback.
; =============================================================================

; --- metrics (SPEC.md 106.1) ---------------------------------------------------
PX_TCW      equ 27                  ; tool column: 2 + a 22px button + 2, rule
PX_BTNW     equ 22                  ; an icon button: 16 + a 2px ring + frame
PX_ONEW     equ 30                  ; ...and the "1:1" text button, 3 glyphs
PX_MINCW    equ 160                 ; narrowest canvas the panel column leaves
PX_MINMID   equ 40                  ; shortest canvas the filmstrip leaves
PX_FULLH    equ 240                 ; content height below which the window is
                                    ; COMPACT: one panel at a time, no captions
PX_CAPW     equ 77 * 8              ; content width the captioned toolbar needs
PX_NAVW     equ 36                  ; the Navigator's three buttons, wide
PX_SBW      equ 14                  ; a status / filmstrip arrow button
PX_GROW     equ 16                  ; the grow box's corner (SPEC.md 11.1.1)
PX_ARH      equ 16                  ; a filmstrip arrow's rows
PX_INFON    equ 6                   ; Image Info's lines
PX_HISTN    equ 4                   ; Histogram's statistics lines

; --- px_layout - every rect, from the live window ------------------------------
; out: CF = 1 the window is not visible (nothing was computed); else CF = 0
; preserves every register
px_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [px_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = top
    mov [px_cx0], ax
    mov [px_cy0], dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    jnc .vis
    jmp .gone
.vis:
    mov [px_w], cx
    mov [px_h], dx
    add ax, cx
    dec ax
    mov [px_xr], ax                 ; the content's last column...
    mov ax, [px_cy0]
    add ax, dx
    dec ax
    mov [px_yb], ax                 ; ...and its last row
    call px_display                 ; depth, kind, palette, half-height icons

    ; --- the tier and the per-display metrics ---------------------------
    mov byte [px_tier], 0           ; 0 = FULL
    cmp word [px_h], PX_FULLH
    jae .full
    mov byte [px_tier], 1           ; 1 = COMPACT
.full:
    mov ax, 20                      ; a button: 16 rows + 4, or 8 + 4 on a CGA
    mov bx, 14                      ; the status bar
    mov cx, 12                      ; a panel strip
    cmp byte [px_half], 0
    je .m1
    mov ax, 12
    mov bx, 11
    mov cx, 10
.m1:
    mov [px_btnh], ax
    mov [px_sth], bx
    mov [px_sh], cx
    mov ax, 10                      ; the line pitch: 10 on a VGA, 9 where a
    cmp byte [px_kind], VID_VGA     ; row is a taller pixel
    je .m2
    mov ax, 9
.m2:
    mov [px_lp], ax
    mov bl, [px_kind]               ; the picture heights scale with the
    xor bh, bh                      ; pixel's aspect (SPEC.md 106.1)
    shl bx, 1
    mov ax, [px_k_thumb + bx]
    mov [px_thh], ax
    mov ax, [px_k_nav + bx]
    mov [px_navh], ax
    mov ax, [px_k_pw + bx]
    mov [px_pw], ax

    ; --- the toolbar: captions on the full layout, if the width allows ----
    mov byte [px_caps], 0
    cmp byte [px_tier], 0
    jne .nocap
    cmp word [px_w], PX_CAPW
    jb .nocap
    mov byte [px_caps], 1
.nocap:
    mov ax, [px_btnh]
    add ax, 4
    cmp byte [px_caps], 0
    je .tb1
    add ax, 9
.tb1:
    mov [px_tbh], ax
    add ax, [px_cy0]
    dec ax
    mov [px_tby2], ax

    ; --- the status bar and the filmstrip, from the bottom up -------------
    mov ax, [px_yb]
    sub ax, [px_sth]
    inc ax
    mov [px_sty1], ax
    add ax, 3                       ; the status text's row: 3 down a 14-row
    cmp byte [px_half], 0           ; bar, 2 down an 11-row one
    je .sty
    dec ax
.sty:
    mov [px_tyy0], ax
    call px_fs_wanted               ; AL = 1 the filmstrip is shown
    mov [px_fson], al
.fsagain:
    xor ax, ax
    cmp byte [px_fson], 0
    je .fs1
    mov ax, [px_sh]
    cmp byte [px_fscol], 0
    jne .fs1
    add ax, [px_thh]
    add ax, [px_lp]
    add ax, 6
.fs1:
    mov bx, [px_sty1]
    sub bx, ax
    mov [px_fsy1], bx               ; the filmstrip's first row, which is the
    dec bx                          ; status bar's when it is hidden
    mov [px_midy2], bx
    mov ax, [px_tby2]
    inc ax
    mov [px_midy1], ax
    sub bx, ax
    inc bx                          ; BX = the middle band's height
    cmp bx, PX_MINMID
    jge .midok
    cmp byte [px_fson], 0
    je .midok
    mov byte [px_fson], 0           ; the filmstrip goes first when the
    jmp short .fsagain              ; window is too short for both
.midok:

    ; --- the columns ----------------------------------------------------
    mov ax, [px_cx0]
    add ax, PX_TCW - 1
    mov [px_tcx2], ax
    mov byte [px_pnon], 0
    mov ax, [px_xr]
    inc ax                          ; no panel column: the canvas runs to the
    cmp byte [px_pnoff], 0          ; content's right edge
    jne .nopn
    mov cx, [px_w]
    sub cx, PX_TCW
    sub cx, [px_pw]
    cmp cx, PX_MINCW
    jl .nopn
    mov byte [px_pnon], 1
    sub ax, [px_pw]
.nopn:
    mov [px_pnx1], ax               ; the panel column's rule (or xr + 1)
    dec ax
    mov [px_cvx2], ax
    mov ax, [px_tcx2]
    inc ax
    mov [px_cvx1], ax

    call px_lay_panels
    call px_lay_buttons
    call px_status_fit
    call px_filmlabel               ; the View menu follows the layout
    clc
    jmp short .out
.gone:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_fs_wanted - out AL = 1 when the filmstrip is shown: the user's View >
; Filmstrip choice when one was made, else the tier's default - shown on the
; full layout, hidden on the compact one (SPEC.md 106.1). Preserves the rest
px_fs_wanted:
    mov al, [px_fsuser]             ; 0 = the tier's, 1 = on, 2 = off
    or al, al
    jz .tier
    cmp al, 1
    je .on
    xor al, al
    ret
.on:
    ret
.tier:
    mov al, [px_tier]
    xor al, 1
    ret

; px_display - the display the window is ON (SPEC.md 39.16.4), never
; OSAPI_VIDEO's primary: a window dragged onto a second card colours itself
; for that card. Sets px_bpp, px_kind, px_half and the palette. Preserves all
px_display:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [px_win]
    call OSAPI_WM_DISPLAY           ; DL = kind, DH = bits per pixel
    mov [px_bpp], dh
    mov byte [px_half], 0
    cmp dl, VID_CGA
    jne .nh
    mov byte [px_half], 1
.nh:
    cmp dl, VID_CGA                 ; VID_EGA (3) is a 640x350 screen, and
    jbe .k                          ; takes the Hercules row of every table:
    mov dl, VID_HERC                ; its pixel is nearly as tall
.k:
    mov [px_kind], dl
    ; THE PALETTE BY DEPTH, not by adapter: a grey is a dither on a 1bpp
    ; screen (SPEC.md 39.4), and text on a dither cannot be read
    mov si, px_pal4
    cmp dh, 1
    ja .p
    mov si, px_pal1
.p:
    mov ax, [si]
    mov [px_c_chrome], ax           ; chrome, body
    mov ax, [si+2]
    mov [px_c_strip], ax            ; strip, strip text
    mov ax, [si+4]
    mov [px_c_canvas], ax           ; canvas, canvas text
    mov ax, [si+6]
    mov [px_c_rule], ax             ; rule, chrome text
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- px_lay_panels - which panels are in the column, where, and how tall ------
; The FULL layout stacks Navigator, Histogram and Image Info. A panel the
; column cannot hold is DROPPED from the bottom up - Info, then Histogram,
; then Navigator (SPEC.md 106.3) - and the last one standing gives up body
; rows before it gives up its strip. The COMPACT layout shows [px_pcur] alone.
; out: px_pvis (bit n = panel n is in the column), px_pyw[n] its strip's top,
;      px_pbhw[n] its body's rows (0 = the strip alone). Preserves all
px_lay_panels:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [px_pvis], 0
    xor ax, ax
    mov [px_pbhw], ax
    mov [px_pbhw+2], ax
    mov [px_pbhw+4], ax
    cmp byte [px_pnon], 0
    jne .on
    jmp .out
.on:
    mov ax, [px_navh]               ; the bodies' heights on this display
    mov [px_pbody], ax
    mov ax, [px_lp]
    mov cx, PX_HISTN
    mul cx
    add ax, 6
    mov [px_pbody+2], ax
    mov ax, [px_lp]
    mov cx, PX_INFON
    mul cx
    add ax, 6
    mov [px_pbody+4], ax
    mov dx, [px_midy1]              ; DX = the next free row
    xor si, si                      ; SI = the panel, 0..2
.next:
    cmp si, 3
    jae .out
    cmp byte [px_tier], 0
    je .full1
    mov ax, si                      ; COMPACT: the current one only
    cmp al, [px_pcur]
    jne .skip
.full1:
    mov ax, dx
    add ax, [px_sh]
    dec ax
    cmp ax, [px_midy2]
    jg .out                         ; not even its strip: this one and every
                                    ; one after it are dropped
    mov bx, si
    shl bx, 1
    mov [px_pyw + bx], dx           ; its strip
    mov cl, [px_bit + si]
    or [px_pvis], cl
    add dx, [px_sh]
    test [px_pcol], cl              ; collapsed: the strip alone
    jnz .skip
    mov ax, [px_pbody + bx]
    mov cx, [px_midy2]
    sub cx, dx
    inc cx                          ; CX = the rows left
    cmp ax, cx
    jle .fits
    cmp si, 2                       ; only Info may be cut short: the other
    jb .skip                        ; two are a picture and a column of
    mov ax, cx                      ; buttons, and half of one is neither
.fits:
    mov cx, [px_lp]                 ; fewer rows than one line and its pad is
    add cx, 4                       ; no body at all
    cmp ax, cx
    jl .skip
    mov [px_pbhw + bx], ax
    add dx, ax
.skip:
    inc si
    jmp short .next
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- px_lay_buttons - every button's rect, and the hidden ones' empty rect ---
; A hidden button keeps the rect {1,1,0,0}, which os88ui_bhit can never match
; (it compares signed), so the press needs no visibility test of its own.
px_lay_buttons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, px_brects               ; everything hidden first, so a region
    mov cx, PX_NB                   ; not laid out this pass cannot leave a
.hide:                              ; stale rect for a press to find
    mov word [di], 1
    mov word [di+2], 1
    mov word [di+4], 0
    mov word [di+6], 0
    add di, 8
    loop .hide
    mov ax, px_lab_full             ; the labels: the half-height pictures on
    cmp byte [px_half], 0           ; a CGA (pxicons.inc)
    je .lab
    mov ax, px_lab_half
.lab:
    mov [px_btns + OS88UI_BT_LABELS], ax
    call px_boxlabels               ; '>' or '-'/'+': the tier is ours to know

    call px_lay_tb

    ; --- the tool column ---------------------------------------------
    mov ax, [px_cx0]
    add ax, 2
    mov cx, ax
    add cx, PX_BTNW - 1
    mov bx, [px_midy1]
    add bx, 3
    mov si, PX_B_T0
.tl:
    mov dx, bx
    add dx, [px_btnh]
    dec dx
    cmp dx, [px_midy2]
    jg .tdone                       ; this one and the rest do not fit
    call px_setrect
    mov bx, dx
    add bx, 4
    inc si
    cmp si, PX_B_T0 + PX_NTOOL
    jb .tl
.tdone:

    ; --- the Navigator's three, when its body is on show ----------------
    test byte [px_pvis], 1
    jz .nonav
    cmp word [px_pbhw], 0
    je .nonav
    mov cx, [px_xr]
    sub cx, 4                       ; CX = their right edge
    mov ax, cx
    sub ax, PX_NAVW - 1
    mov di, 12                      ; a text button: a glyph + 4
    cmp byte [px_half], 0
    je .nb
    mov di, 10
.nb:
    mov dx, di
    mov bx, di
    add dx, bx
    add dx, bx
    add dx, 4                       ; DX = three buttons and two 2px gaps
    mov bx, [px_pbhw]
    sub bx, dx
    sar bx, 1
    add bx, [px_pyw]
    add bx, [px_sh]                 ; BX = the first one's top
    mov si, PX_B_NZIN
.nl:
    mov dx, bx
    add dx, di
    dec dx
    call px_setrect
    mov bx, dx
    add bx, 3
    inc si
    cmp si, PX_B_NZIN + 3
    jb .nl
.nonav:

    ; --- each laid-out panel's box ---------------------------------------
    xor si, si
.pb:
    mov cl, [px_bit + si]
    test [px_pvis], cl
    jz .pbn
    push si
    mov bx, si
    shl bx, 1
    mov bx, [px_pyw + bx]           ; the strip's top
    add si, PX_B_PB0
    call px_boxrect
    pop si
.pbn:
    inc si
    cmp si, 3
    jb .pb

    ; --- the filmstrip's box and its two arrows ----------------------------
    cmp byte [px_fson], 0
    je .nofs
    mov bx, [px_fsy1]
    mov si, PX_B_FSBOX
    call px_boxrect
    cmp byte [px_fscol], 0
    jne .nofs
    mov bx, [px_fsy1]               ; the arrows: PX_ARH rows (half that on a
    add bx, [px_sh]                 ; CGA), centred in the body
    mov dx, [px_sty1]
    sub dx, bx                      ; DX = the body's rows
    mov cx, PX_ARH
    cmp byte [px_half], 0
    je .arh
    mov cx, PX_ARH / 2 + 2
.arh:
    sub dx, cx
    sar dx, 1
    add bx, dx                      ; BX = their top
    mov dx, bx
    add dx, cx
    dec dx
    mov ax, [px_cx0]
    add ax, 3
    mov cx, ax
    add cx, PX_SBW - 1
    mov si, PX_B_FSL
    call px_setrect
    mov cx, [px_xr]
    sub cx, 3
    mov ax, cx
    sub ax, PX_SBW - 1
    mov si, PX_B_FSR
    call px_setrect
.nofs:

    ; --- the status bar's two, short of the grow box's corner -----------
    mov cx, [px_xr]
    sub cx, PX_GROW
    mov ax, cx
    sub ax, PX_SBW - 1
    mov bx, [px_sty1]
    add bx, 2
    mov dx, bx
    add dx, 10
    cmp byte [px_half], 0
    je .sh
    dec bx                          ; an 11-row bar: rows 1..10
    dec dx
    dec dx
.sh:
    mov si, PX_B_SNEXT
    call px_setrect
    sub ax, PX_SBW
    sub cx, PX_SBW
    mov si, PX_B_SPREV
    call px_setrect
    mov [px_sbx], ax                ; the status text stops short of them
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_boxrect - SI = a box button, BX = its strip's top: the 10x10 box at the
; strip's right end. Preserves all
px_boxrect:
    push ax
    push bx
    push cx
    push dx
    mov cx, [px_xr]
    sub cx, 2
    mov ax, cx
    sub ax, 9
    cmp byte [px_half], 0
    jne .h
    inc bx                          ; a 12-row strip: the box sits a row in
.h:
    mov dx, bx
    add dx, 9
    call px_setrect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_setrect - SI = a button index, AX/BX/CX/DX its rect. Preserves all
px_setrect:
    push si
    shl si, 1
    shl si, 1
    shl si, 1
    add si, px_brects
    mov [si], ax
    mov [si+2], bx
    mov [si+4], cx
    mov [si+6], dx
    pop si
    ret

; px_getrect - SI = a button index: AX/BX/CX/DX its rect. Preserves SI
px_getrect:
    push si
    shl si, 1
    shl si, 1
    shl si, 1
    add si, px_brects
    mov ax, [si]
    mov bx, [si+2]
    mov cx, [si+4]
    mov dx, [si+6]
    pop si
    ret

; --- px_lay_tb - the toolbar's items, on the 8-pixel grid ----------------------
; Every item has a width in whole cells (px_tbdef): its caption is centred in
; the cell, its button centred over it, and a separator is one cell with its
; rule in the middle. On the grid the whole caption row is ONE opaque run
; (px_draw_tb) and an aligned one (SPEC.md 6.1). An item that would not end
; a cell short of the right edge is hidden, and so is every one after it.
px_lay_tb:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [px_cx0]
    add di, 8                       ; DI = the next cell's x: a cell of margin
    mov si, px_tbdef
    xor bx, bx                      ; BX = the item
.clr:
    mov byte [px_tbcap + bx], 0xFF
    mov byte [px_tbon + bx], 0
    inc bx
    cmp bx, PX_TBN
    jb .clr
    xor bx, bx
.it:
    cmp bx, PX_TBN
    jae .done
    mov al, [si + PX_TBD_CC]        ; the cell's glyphs with captions...
    cmp byte [px_caps], 0
    jne .cw
    mov al, [si + PX_TBD_NC]        ; ...or without
.cw:
    xor ah, ah
    mov cl, 3
    shl ax, cl                      ; AX = the cell in pixels
    mov dx, di
    add dx, ax
    add dx, 7
    cmp dx, [px_xr]
    jg .done
    mov byte [px_tbon + bx], 1
    push bx
    shl bx, 1
    mov [px_tbx + bx], di           ; the cell's left, for the painter
    pop bx
    mov dl, [si + PX_TBD_BTN]
    cmp dl, 0xFF
    je .adv                         ; a separator: its cell is all it has
    push ax
    push bx
    push si
    xor dh, dh
    push dx                         ; the button's index
    mov cl, [si + PX_TBD_W]         ; CX = the button's width
    xor ch, ch
    sub ax, cx
    shr ax, 1
    add ax, di                      ; AX = the button's left...
    add cx, ax
    dec cx                          ; ...CX its right
    mov bx, [px_cy0]
    add bx, 2
    mov dx, bx
    add dx, [px_btnh]
    dec dx
    pop si                          ; SI = the button
    call px_setrect
    pop si
    pop bx
    pop ax
    cmp byte [px_caps], 0           ; the caption's first glyph, counted in
    je .adv                         ; cells from the content's left edge
    push ax
    push cx
    mov al, [si + PX_TBD_CC]
    sub al, [si + PX_TBD_CL]
    shr al, 1
    mov cx, di
    sub cx, [px_cx0]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    add al, cl
    mov [px_tbcap + bx], al
    pop cx
    pop ax
.adv:
    add di, ax
    add si, PX_TBDSZ
    inc bx
    jmp .it
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; DRAWING HELPERS
; =============================================================================

; px_fillc - fill AX/BX/CX/DX (absolute, inclusive) in [px_pc]. An empty rect
; (x1 > x2 or y1 > y2) draws nothing, so a caller may hand over a strip that
; came out zero-wide. Preserves every register
px_fillc:
    cmp ax, cx
    jg .no
    cmp bx, dx
    jg .no
    push ax
    mov al, [px_pc]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.no:
    ret

; PXFILL colour - px_fillc in a colour: an immediate or a byte in memory
%macro PXFILL 1
    push ax
    mov al, %1
    mov [px_pc], al
    pop ax
    call px_fillc
%endmacro

; px_vrule - column AX, rows BX..DX, in the rule colour. Preserves all
px_vrule:
    push ax
    mov al, [px_c_rule]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_VLINE
    ret

; px_hrule - row DX, columns AX..BX, in the rule colour. Preserves all
px_hrule:
    push ax
    mov al, [px_c_rule]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_HLINE
    ret

; px_btn - draw button SI (an index) if it is laid out. Preserves all
px_btn:
    push ax
    push bx
    push cx
    push dx
    call px_getrect
    cmp ax, cx
    jg .no
    mov ax, si
    inc ax                          ; os88ui_btn takes the index PLUS ONE
    mov bx, px_btns
    call os88ui_btn
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_icon - picture SI at CX, DX: AL = its ground (the mask pass), AH = its
; ink. A picture the clip would cut is drawn as its ground instead: the
; sprite pass clips a shape WHOLE (SPEC.md 25.6), so a cut one would leave
; its cell holding whatever was there (os88ui_bdraw1's rule). Preserves all
px_icon:
    push ax
    push bx
    push cx
    push dx
    mov [px_icpair], ax
    mov [px_icx], cx
    mov [px_icy], dx
    mov ax, cx                      ; the picture's rect
    mov bx, dx
    add cx, 15
    mov dl, [si+1]                  ; the record's rows
    xor dh, dh
    add dx, bx
    dec dx
    call OSAPI_WM_CLIP_TEST         ; CF = 1: not all of it may be drawn
    jnc .whole
    push ax
    mov al, [px_icpair]             ; ...so its ground, over the same rect
    mov [px_pc], al
    pop ax
    call px_fillc
    jmp short .out
.whole:
    mov ax, [px_icpair]
    mov cx, [px_icx]
    mov dx, [px_icy]
    call OSAPI_ICON_PEN             ; AL = the mask pass, AH = the data pass
    call OSAPI_ICON_DRAW            ; CF = 1 is a malformed record: nothing
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_tband - ONE 8-row text band: x AX..CX at row DX, the string SI left
; (DI = 0) or centred (DI = 1), as one OPAQUE run aligned on the 8-pixel grid
; and padded with spaces to the band's last whole cell; the slivers either
; side of the grid are filled in the ground. BL = ink, BH = ground. A string
; longer than the band is cut at its last cell. Preserves all
px_tband:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [px_tbink], bx
    mov [px_tbx1], ax
    mov [px_tbx2], cx
    mov [px_tby], dx
    mov [px_pc], bh                 ; the ground, for both slivers
    add ax, 7
    and ax, 0xFFF8
    mov [px_tbcx], ax               ; the first aligned column
    sub cx, ax
    inc cx
    sar cx, 1
    sar cx, 1
    sar cx, 1                       ; CX = whole cells, signed
    jg .cells
    mov ax, [px_tbx1]               ; no room for one: the band is ground
    mov cx, [px_tbx2]
    mov bx, dx
    add dx, 7
    call px_fillc
    jmp .out
.cells:
    cmp cx, PX_LINEMAX
    jbe .cmax
    mov cx, PX_LINEMAX
.cmax:
    mov [px_tbn], cx
    xor bx, bx                      ; BX = the string's length, cut at CX
.len:
    cmp bx, cx
    jae .lend
    cmp byte [si + bx], 0
    je .lend
    inc bx
    jmp short .len
.lend:
    xor ax, ax                      ; AX = the leading spaces
    or di, di
    jz .lead
    mov ax, cx
    sub ax, bx
    shr ax, 1
.lead:
    mov di, px_line
    mov cx, ax
    jcxz .s1d
.s1:
    mov byte [di], ' '
    inc di
    loop .s1
.s1d:
    mov cx, bx
    jcxz .s2d
.s2:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .s2
.s2d:
    mov cx, px_line
    add cx, [px_tbn]
    sub cx, di                      ; CX = the trailing spaces
    jle .s3d
.s3:
    mov byte [di], ' '
    inc di
    loop .s3
.s3d:
    mov byte [di], 0
    mov ax, [px_tbx1]               ; the sliver left of the grid
    mov bx, [px_tby]
    mov cx, [px_tbcx]
    dec cx
    mov dx, bx
    add dx, 7
    call px_fillc
    mov cx, [px_tbcx]               ; the run
    mov dx, [px_tby]
    mov si, px_line
    mov ax, [px_tbink]              ; AL = ink, AH = ground
    call OSAPI_FONT_RUN
    mov ax, [px_tbn]                ; the sliver right of it
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [px_tbcx]
    mov bx, [px_tby]
    mov cx, [px_tbx2]
    mov dx, bx
    add dx, 7
    call px_fillc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_well - an empty picture well: the frame AX/BX/CX/DX and a grey interior,
; solid light grey on a colour screen and the 50% dither on a 1bpp one, where
; a grey would round to black (SPEC.md 39.4). Preserves all
px_well:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, [px_c_rule]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    inc ax
    inc bx
    dec cx
    dec dx
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    cmp byte [px_bpp], 1
    ja .col
    call OSAPI_GFX_FILL_GRAY
    jmp short .out
.col:
    PXFILL CLGRAY
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE REGIONS. Each draws its own rect whole and nothing outside it, so the
; paint proc can skip any region the damage rect misses (SPEC.md 106.4).
; =============================================================================

; --- the toolbar ---------------------------------------------------------------
px_draw_tb:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [px_cx0]                ; the two rows of air above the buttons
    mov bx, [px_cy0]
    mov cx, [px_xr]
    mov dx, bx
    inc dx
    PXFILL [px_c_chrome]
    mov bx, [px_cy0]                ; BX..DX = the button row
    add bx, 2
    mov dx, bx
    add dx, [px_btnh]
    dec dx
    mov bp, [px_cx0]                ; BP = the next column not yet drawn
    xor di, di                      ; DI = the item
.it:
    cmp di, PX_TBN
    jae .tail
    cmp byte [px_tbon + di], 0
    je .tail                        ; hidden: so is everything after it
    mov si, di
    mov cx, PX_TBDSZ
    mov ax, di
    push dx
    mul cx
    pop dx
    mov si, ax
    mov al, [px_tbdef + si + PX_TBD_BTN]
    cmp al, 0xFF
    jne .button
    mov si, di                      ; --- a separator: ground to its rule
    shl si, 1
    mov ax, [px_tbx + si]
    add ax, 3                       ; AX = the rule's column
    mov cx, ax
    dec cx
    push ax
    mov ax, bp
    PXFILL [px_c_chrome]
    pop ax
    call px_vrule
    inc ax
    mov bp, ax
    jmp short .next
.button:
    xor ah, ah
    mov si, ax                      ; SI = the button
    push bx
    push dx
    call px_getrect                 ; AX = its left, CX = its right
    mov cx, ax
    dec cx
    mov ax, bp
    pop dx
    pop bx
    PXFILL [px_c_chrome]
    call px_btn
    push bx
    push dx
    call px_getrect
    inc cx
    mov bp, cx
    pop dx
    pop bx
.next:
    inc di
    jmp short .it
.tail:
    mov ax, bp                      ; ground to the right edge
    mov cx, [px_xr]
    PXFILL [px_c_chrome]
    ; --- the caption row, ONE run (SPEC.md 106.3) -------------------------
    mov bx, dx
    inc bx                          ; BX = the first row under the buttons
    cmp byte [px_caps], 0
    je .nocap
    mov ax, [px_cx0]
    mov dx, bx
    PXFILL [px_c_chrome]            ; one row of air
    call px_capline                 ; px_cline = every caption at its cell
    inc bx
    mov dx, bx
    mov si, px_cline
    mov ax, [px_cx0]
    mov cx, [px_xr]
    push bx
    mov bl, [px_c_text]
    mov bh, [px_c_chrome]
    xor di, di
    call px_tband
    pop bx
    add bx, 8
.nocap:
    mov ax, [px_cx0]                ; the rows down to the rule
    mov cx, [px_xr]
    mov dx, [px_tby2]
    dec dx
    PXFILL [px_c_chrome]
    mov dx, [px_tby2]
    mov bx, cx
    call px_hrule
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_capline - compose the caption row into px_cline: spaces, with each laid
; out item's caption at its cell. Preserves all
px_capline:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, px_cline
    mov cx, PX_LINEMAX
    mov al, ' '
.sp:
    mov [di], al
    inc di
    loop .sp
    mov byte [di], 0
    xor bx, bx
.it:
    cmp bx, PX_TBN
    jae .out
    mov al, [px_tbcap + bx]
    cmp al, 0xFF
    je .nx
    xor ah, ah
    mov di, px_cline
    add di, ax
    push bx
    mov ax, bx
    mov cx, PX_TBDSZ
    mul cx
    mov bx, ax
    mov si, [px_tbdef + bx + PX_TBD_CAP]
    pop bx
.cp:
    mov al, [si]
    or al, al
    jz .nx
    cmp di, px_cline + PX_LINEMAX
    jae .nx
    mov [di], al
    inc si
    inc di
    jmp short .cp
.nx:
    inc bx
    jmp short .it
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- the tool column -----------------------------------------------------------
px_draw_tools:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov ax, [px_cx0]                ; the two columns left of the buttons
    mov bx, [px_midy1]
    mov cx, ax
    inc cx
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
    mov ax, [px_cx0]                ; ...and the two right of them
    add ax, 2 + PX_BTNW
    mov cx, [px_tcx2]
    dec cx
    PXFILL [px_c_chrome]
    mov ax, [px_tcx2]               ; the column's rule
    call px_vrule
    mov bp, [px_midy1]              ; BP = the next row not yet drawn
    mov si, PX_B_T0
.tl:
    call px_getrect
    cmp ax, cx
    jg .tail                        ; this one did not fit, nor the rest
    mov dx, bx                      ; the ground above this button
    dec dx
    mov bx, bp
    PXFILL [px_c_chrome]
    call px_btn
    call px_getrect
    inc dx
    mov bp, dx
    inc si
    cmp si, PX_B_T0 + PX_NTOOL
    jb .tl
.tail:
    mov ax, [px_cx0]                ; ...and under the last one
    add ax, 2
    mov cx, ax
    add cx, PX_BTNW - 1
    mov bx, bp
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the canvas: the empty state (SPEC.md 106.3) -------------------------------
; A picture, "Open a picture  (Ctrl+O)", and - once a file has been picked -
; what it is. Centred as one block, every row of it drawn once.
px_draw_canvas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [px_cvx1]
    cmp ax, [px_cvx2]
    jle .some
    jmp .out
.some:
    mov bp, 16                      ; BP = the picture's rows
    mov di, 6                       ; DI = the air under it
    cmp byte [px_half], 0
    je .h
    mov bp, 8
    mov di, 3
.h:
    mov ax, bp                      ; the block: picture, air, a line...
    add ax, di
    add ax, 8
    cmp byte [px_fhave], 0
    je .one
    add ax, [px_lp]                 ; ...and a second
.one:
    mov cx, [px_midy2]
    sub cx, [px_midy1]
    inc cx
    sub cx, ax
    sar cx, 1
    jns .top
    xor cx, cx
.top:
    mov bx, [px_midy1]
    add bx, cx                      ; BX = the block's top
    push bx
    mov ax, [px_cvx1]               ; the ground above it
    mov cx, [px_cvx2]
    mov dx, bx
    dec dx
    mov bx, [px_midy1]
    PXFILL [px_c_canvas]
    pop bx
    ; the picture's row: ground, picture, ground
    mov ax, [px_cvx2]
    sub ax, [px_cvx1]
    sub ax, 15
    sar ax, 1
    add ax, [px_cvx1]               ; AX = the picture's left
    push ax
    mov cx, ax
    dec cx
    mov ax, [px_cvx1]
    mov dx, bx
    add dx, bp
    dec dx
    PXFILL [px_c_canvas]
    pop ax
    push ax
    add ax, 16
    mov cx, [px_cvx2]
    PXFILL [px_c_canvas]
    pop cx                          ; CX = the picture's left
    mov dx, bx
    mov si, pxi_photo
    cmp byte [px_half], 0
    je .full
    mov si, px_hicons + PXI_PHOTO * PXH_SZ
.full:
    mov al, [px_c_canvas]
    mov ah, [px_c_cvtext]
    call px_icon
    add bx, bp
    mov ax, [px_cvx1]               ; the air under it
    mov cx, [px_cvx2]
    mov dx, bx
    add dx, di
    dec dx
    PXFILL [px_c_canvas]
    add bx, di
    mov dx, bx                      ; the first line
    mov si, px_s_empty
    push bx
    mov bl, [px_c_cvtext]
    mov bh, [px_c_canvas]
    mov di, 1
    call px_tband
    pop bx
    add bx, 8
    cmp byte [px_fhave], 0
    je .rest
    mov dx, bx                      ; the air between the lines
    add dx, [px_lp]
    sub dx, 9
    PXFILL [px_c_canvas]
    add bx, [px_lp]
    sub bx, 8
    mov dx, bx
    mov si, px_cvline
    push bx
    mov bl, [px_c_cvtext]
    mov bh, [px_c_canvas]
    call px_tband
    pop bx
    add bx, 8
.rest:
    mov dx, [px_midy2]              ; the ground below
    PXFILL [px_c_canvas]
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the panel column ----------------------------------------------------------
px_draw_panels:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [px_pnon], 0
    jne .on
    jmp .out
.on:
    mov ax, [px_pnx1]               ; the column's rule
    mov bx, [px_midy1]
    mov dx, [px_midy2]
    call px_vrule
    mov bx, [px_midy1]              ; BX = the next row not yet drawn
    xor di, di                      ; DI = the panel
.p:
    mov al, [px_bit + di]
    test [px_pvis], al
    jz .pn
    mov si, di
    shl si, 1
    mov dx, [px_pyw + si]           ; its strip
    mov ax, [px_pnx1]
    inc ax
    push si
    mov si, [px_ptitle + si]
    push di
    add di, PX_B_PB0
    call px_strip
    pop di
    pop si
    mov bx, dx
    add bx, [px_sh]                 ; BX = its body's top
    mov cx, [px_pbhw + si]
    jcxz .pn                        ; collapsed, or no room
    mov dx, bx
    add dx, cx
    dec dx                          ; DX = its body's last row
    mov ax, [px_pnx1]
    inc ax
    mov cx, [px_xr]
    call [px_pbodyp + si]           ; AX..CX x, BX..DX y
    mov bx, dx
    inc bx
.pn:
    inc di
    cmp di, 3
    jb .p
    mov ax, [px_pnx1]               ; the column's ground under the last one
    inc ax
    mov cx, [px_xr]
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_strip - a panel's title strip: AX = its left, DX = its top, SI = the
; title, DI = its box button; it runs to the content's right edge. The title
; is one run in the strip's colours; the box sits at the right end with the
; strip's colour round it. Preserves all
px_strip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax                      ; BP = the strip's left
    mov [px_sty], dx
    mov [px_sbox], di
    push si
    mov si, di
    call px_getrect                 ; the box: AX..CX, BX..DX
    pop si
    mov [px_bx1], ax
    mov [px_by1], bx
    mov [px_bx2], cx
    mov [px_by2], dx
    mov bx, [px_sty]                ; --- the title's part: x BP .. box - 3
    mov ax, 2                       ; the text's row: centred in the strip
    cmp byte [px_half], 0
    je .ty
    mov ax, 1
.ty:
    add ax, bx
    mov [px_tyy], ax
    mov dx, ax
    dec dx
    mov ax, bp
    mov cx, [px_bx1]
    sub cx, 3
    PXFILL [px_c_strip]             ; above the text
    push bx
    mov dx, [px_tyy]
    mov bl, [px_c_stript]
    mov bh, [px_c_strip]
    xor di, di
    call px_tband                   ; the title
    pop bx
    mov bx, [px_tyy]
    add bx, 8
    mov dx, [px_sty]
    add dx, [px_sh]
    dec dx
    PXFILL [px_c_strip]             ; below it
    ; --- the box's part: x box - 2 .. the right edge -----------------------
    mov ax, [px_bx1]
    sub ax, 2
    mov bx, [px_sty]
    mov cx, [px_xr]
    mov dx, [px_by1]
    dec dx
    PXFILL [px_c_strip]             ; above the box
    mov bx, [px_by2]
    inc bx
    mov dx, [px_sty]
    add dx, [px_sh]
    dec dx
    PXFILL [px_c_strip]             ; below it
    mov bx, [px_by1]
    mov dx, [px_by2]
    mov cx, [px_bx1]
    dec cx
    PXFILL [px_c_strip]             ; left of it
    mov ax, [px_bx2]
    inc ax
    mov cx, [px_xr]
    PXFILL [px_c_strip]             ; right of it
    mov si, [px_sbox]
    call px_btn                     ; ...and the box
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the three bodies: AX..CX x, BX..DX y. Each preserves all -----------------

; Navigator: a picture well and the three zoom buttons beside it
px_body_nav:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [px_rx1], ax
    mov [px_ry1], bx
    mov [px_rx2], cx
    mov [px_ry2], dx
    mov dx, bx                      ; three rows of air on top...
    add dx, 2
    PXFILL [px_c_body]
    mov bx, [px_ry2]                ; ...and at the foot
    mov dx, bx
    sub bx, 2
    PXFILL [px_c_body]
    mov si, PX_B_NZIN
    call px_getrect
    mov [px_bx1], ax                ; the button column's left
    mov ax, [px_rx1]                ; the margin left of the well
    mov bx, [px_ry1]
    add bx, 3
    mov cx, ax
    add cx, 3
    mov dx, [px_ry2]
    sub dx, 3
    PXFILL [px_c_body]
    mov ax, [px_rx1]                ; the well
    add ax, 4
    mov cx, [px_bx1]
    sub cx, 6
    call px_well
    mov ax, cx                      ; the gap between it and the buttons
    inc ax
    mov cx, [px_bx1]
    dec cx
    PXFILL [px_c_body]
    mov ax, [px_bx1]                ; the margin right of the buttons
    add ax, PX_NAVW
    mov cx, [px_rx2]
    PXFILL [px_c_body]
    mov bp, bx                      ; BP = the next row of the column
    mov si, PX_B_NZIN
.b:
    call px_getrect
    mov dx, bx
    dec dx
    mov bx, bp
    PXFILL [px_c_body]              ; the column above this one
    call px_btn
    call px_getrect
    inc dx
    mov bp, dx
    inc si
    cmp si, PX_B_NZIN + 3
    jb .b
    mov bx, bp
    mov dx, [px_ry2]
    sub dx, 3
    PXFILL [px_c_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Histogram: a graph well and four statistics beside it
px_body_hist:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [px_rx1], ax
    mov [px_ry1], bx
    mov [px_rx2], cx
    mov [px_ry2], dx
    mov dx, bx
    add dx, 2
    PXFILL [px_c_body]              ; air on top
    mov bx, [px_ry2]
    mov dx, bx
    sub bx, 2
    PXFILL [px_c_body]              ; ...and at the foot
    mov ax, [px_rx1]                ; the margin left of the well
    mov bx, [px_ry1]
    add bx, 3
    mov cx, ax
    add cx, 3
    mov dx, [px_ry2]
    sub dx, 3
    PXFILL [px_c_body]
    mov ax, [px_rx2]                ; the statistics take their width of
    sub ax, PX_HSTW * 8 + 2         ; whole cells at the right
    mov [px_bx1], ax
    mov cx, ax
    sub cx, 4
    mov ax, [px_rx1]
    add ax, 4
    call px_well
    mov bp, bx                      ; BP = the next row of the statistics
    xor di, di
.l:
    cmp di, PX_HISTN
    jae .ld
    mov dx, bp
    add dx, 7
    cmp dx, [px_ry2]
    jg .ld
    mov ax, cx                      ; the line, from the well's edge
    inc ax
    push cx
    mov cx, [px_rx2]
    mov dx, bp
    mov si, di
    shl si, 1
    mov si, [px_hlines + si]
    push bx
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    push di
    xor di, di
    call px_tband
    pop di
    pop bx
    mov bx, bp                      ; the air under it
    add bx, 8
    mov dx, bp
    add dx, [px_lp]
    dec dx
    cmp dx, [px_ry2]
    jle .air
    mov dx, [px_ry2]
.air:
    PXFILL [px_c_body]
    pop cx
    add bp, [px_lp]
    inc di
    jmp short .l
.ld:
    mov ax, cx
    inc ax
    mov cx, [px_rx2]
    mov bx, bp
    mov dx, [px_ry2]
    sub dx, 3
    PXFILL [px_c_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Image Info: its lines, each one run across the body
px_body_info:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [px_rx1], ax
    mov [px_ry1], bx
    mov [px_rx2], cx
    mov [px_ry2], dx
    mov dx, bx
    add dx, 2
    PXFILL [px_c_body]              ; air on top
    add bx, 3
    mov bp, bx                      ; BP = the next row
    xor di, di
.l:
    cmp di, PX_INFON
    jae .ld
    mov dx, bp
    add dx, 7
    cmp dx, [px_ry2]
    jg .ld
    call px_infoline                ; px_cline = line DI
    mov ax, [px_rx1]
    mov cx, [px_rx2]
    mov dx, bp
    mov si, px_cline
    push bx
    push di
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    xor di, di
    call px_tband
    pop di
    pop bx
    mov bx, bp
    add bx, 8
    mov dx, bp
    add dx, [px_lp]
    dec dx
    cmp dx, [px_ry2]
    jle .air
    mov dx, [px_ry2]
.air:
    PXFILL [px_c_body]
    add bp, [px_lp]
    inc di
    jmp short .l
.ld:
    mov ax, [px_rx1]
    mov cx, [px_rx2]
    mov bx, bp
    mov dx, [px_ry2]
    PXFILL [px_c_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_infoline - compose Image Info's line DI into px_cline: its label padded
; to PX_ILAB glyphs, then its value. Preserves all
px_infoline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, di                      ; the value: PX_IVSZ bytes a line
    mov cx, PX_IVSZ
    mul cx
    add ax, px_ival
    push ax
    mov bx, di
    shl bx, 1
    mov si, [px_ilabels + bx]
    mov di, px_cline
    mov cx, PX_ILAB
.lab:
    mov al, [si]
    or al, al
    jz .pad
    mov [di], al
    inc si
    inc di
    loop .lab
    jmp short .val
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.val:
    pop si
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the filmstrip -------------------------------------------------------------
px_draw_fs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [px_fson], 0
    jne .on
    jmp .out
.on:
    mov ax, [px_cx0]
    mov dx, [px_fsy1]
    mov si, px_fstitle
    mov di, PX_B_FSBOX
    call px_strip
    cmp byte [px_fscol], 0
    je .body
    jmp .out
.body:
    mov si, PX_B_FSL
    call px_getrect                 ; the left arrow's rows are the band's
    mov [px_ry1], bx
    mov [px_ry2], dx
    mov [px_bx1], cx                ; ...and its right edge
    mov ax, [px_cx0]                ; the air above the band
    mov bx, [px_fsy1]
    add bx, [px_sh]
    mov cx, [px_xr]
    mov dx, [px_ry1]
    dec dx
    PXFILL [px_c_body]
    mov bx, [px_ry2]                ; ...and below it
    inc bx
    mov dx, [px_sty1]
    dec dx
    PXFILL [px_c_body]
    mov bx, [px_ry1]                ; the margin left of the arrow
    mov dx, [px_ry2]
    mov cx, [px_cx0]
    add cx, 2
    PXFILL [px_c_body]
    call px_btn
    mov si, PX_B_FSR
    call px_btn
    call px_getrect
    mov [px_bx2], ax                ; the right arrow's left edge
    inc cx                          ; the margin right of it
    mov ax, cx
    mov cx, [px_xr]
    mov bx, [px_ry1]
    PXFILL [px_c_body]
    mov ax, [px_ry2]                ; the line, centred in the band
    sub ax, [px_ry1]
    sub ax, 7
    sar ax, 1
    add ax, [px_ry1]
    mov [px_tyy], ax
    mov ax, [px_bx1]
    inc ax
    mov cx, [px_bx2]
    dec cx
    mov bx, [px_ry1]
    mov dx, [px_tyy]
    dec dx
    PXFILL [px_c_body]
    mov bx, [px_tyy]
    add bx, 8
    mov dx, [px_ry2]
    PXFILL [px_c_body]
    mov dx, [px_tyy]
    mov si, px_fsline
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    mov di, 1
    call px_tband
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the status bar --------------------------------------------------------------
; A rule, the fields - each ONE run, a 1-pixel rule between two - and the two
; arrows. A field whose value changes is redrawn alone (px_sflush), as the
; one run it already was (SPEC.md 106.3).
px_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [px_cx0]                ; the rule
    mov bx, [px_xr]
    mov dx, [px_sty1]
    call px_hrule
    mov cx, [px_sbx]                ; the text's part: x0 .. arrows - 3
    sub cx, 3
    mov bx, [px_sty1]
    inc bx
    mov dx, [px_tyy0]               ; the rows above the text
    dec dx
    PXFILL [px_c_chrome]
    mov bx, [px_tyy0]               ; ...and below it
    add bx, 8
    mov dx, [px_yb]
    PXFILL [px_c_chrome]
    xor bx, bx                      ; the fields, left to right
    mov di, [px_cx0]                ; DI = the next column not yet drawn
.f:
    mov al, [px_bit + bx]
    test [px_skeep], al
    jz .n
    or bx, bx                       ; a rule in front of every field but
    jz .run                         ; the name, which is always first
    mov ax, di
    push bx
    mov bx, [px_tyy0]
    mov dx, bx
    add dx, 7
    call px_vrule
    pop bx
    inc di
.run:
    call px_sfrun                   ; at [px_sfxw + 2*BX], which is DI
    mov al, [px_sfw + bx]
    xor ah, ah
    add ax, 2
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add di, ax
.n:
    inc bx
    cmp bx, PX_NSF
    jb .f
    mov ax, di                      ; the text rows past the last field
    mov bx, [px_tyy0]
    mov cx, [px_sbx]
    sub cx, 3
    mov dx, bx
    add dx, 7
    PXFILL [px_c_chrome]
    ; --- the arrows' part: x arrows - 2 .. the right edge ------------------
    mov si, PX_B_SNEXT
    call px_getrect                 ; CX = their right, BX..DX their rows
    mov [px_bx2], cx
    mov [px_by1], bx
    mov [px_by2], dx
    mov ax, [px_sbx]                ; the 2px gap from the text
    sub ax, 2
    mov cx, ax
    inc cx
    mov bx, [px_sty1]
    inc bx
    mov dx, [px_yb]
    PXFILL [px_c_chrome]
    mov ax, [px_sbx]                ; above them
    mov cx, [px_bx2]
    mov dx, [px_by1]
    dec dx
    PXFILL [px_c_chrome]
    mov bx, [px_by2]                ; below them
    inc bx
    mov dx, [px_yb]
    PXFILL [px_c_chrome]
    mov ax, [px_bx2]                ; the grow box's corner, to the edge
    inc ax
    mov cx, [px_xr]
    mov bx, [px_sty1]
    inc bx
    PXFILL [px_c_chrome]
    mov si, PX_B_SPREV
    call px_btn
    mov si, PX_B_SNEXT
    call px_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the status fields (SPEC.md 106.3) -------------------------------------------
; Eight fields, each a fixed width in glyphs, drawn ` value ` with a 1-pixel
; rule between two. The bar holds as many as fit: px_status_fit drops them in
; px_sdrop's order until the rest do, so the narrow screens keep what matters
; most, and banks each kept field's x.

; px_status_fit - which fields fit the bar, and each one's x. Preserves all
px_status_fit:
    push ax
    push bx
    push cx
    push si
    mov ax, [px_sbx]                ; the pixels the fields may use
    sub ax, 3
    sub ax, [px_cx0]
    inc ax
    mov [px_scells], ax             ; (pixels, despite the name)
    mov byte [px_skeep], 0xFF       ; start with every field...
    xor si, si                      ; SI = the next field to drop
.again:
    call px_swidth                  ; AX = the pixels those fields need
    cmp ax, [px_scells]
    jle .fits
    cmp si, PX_NSF - 1
    jae .fits                       ; the name is never dropped
    mov bl, [px_sdrop + si]
    xor bh, bh
    mov al, [px_bit + bx]
    not al
    and [px_skeep], al              ; ...and drop them in order
    inc si
    jmp short .again
.fits:
    mov ax, [px_cx0]                ; each kept field's x
    xor bx, bx
.x:
    mov cl, [px_bit + bx]
    test [px_skeep], cl
    jz .xn
    or bx, bx
    jz .x0
    inc ax                          ; past the rule in front of it
.x0:
    mov si, bx
    shl si, 1
    mov [px_sfxw + si], ax
    mov cl, [px_sfw + bx]
    xor ch, ch
    add cx, 2
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
.xn:
    inc bx
    cmp bx, PX_NSF
    jb .x
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_swidth - out AX = the pixels the kept fields need: 8 x (width + 2)
; each, and a 1-pixel rule between two
px_swidth:
    push bx
    push cx
    mov ax, -1
    xor bx, bx
.w:
    mov cl, [px_bit + bx]
    test [px_skeep], cl
    jz .n
    mov cl, [px_sfw + bx]
    xor ch, ch
    add cx, 2
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
    inc ax
.n:
    inc bx
    cmp bx, PX_NSF
    jb .w
    pop cx
    pop bx
    ret

; px_sfrun - field BX as its one run, ` value ` padded to its width, at its
; banked x on the text row. Preserves all
px_sfrun:
    push ax
    push cx
    push dx
    push si
    push di
    mov di, px_cline
    mov byte [di], ' '
    inc di
    call px_sfcopy
    mov byte [di], ' '
    mov byte [di + 1], 0
    mov si, bx
    shl si, 1
    mov cx, [px_sfxw + si]
    mov dx, [px_tyy0]
    mov si, px_cline
    mov al, [px_c_text]
    mov ah, [px_c_chrome]
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; px_sfcopy - field BX's value into DI, padded with spaces to its width and
; not terminated. Preserves all but DI (advanced past the field)
px_sfcopy:
    push ax
    push cx
    push si
    mov cl, [px_sfw + bx]
    xor ch, ch
    mov si, bx
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1                       ; PX_SVSZ = 16
    add si, px_sv
.c:
    mov al, [si]
    or al, al
    jz .p
    mov [di], al
    inc si
    inc di
    loop .c
    jmp short .out
.p:
    mov byte [di], ' '
    inc di
    loop .p
.out:
    pop si
    pop cx
    pop ax
    ret

; --- the window's ONE button record (SPEC.md 106.3): every control in it, in
; the PX_B_* order. One, because os88ui_btnclick takes the first record whose
; window matches - a second record for the same window would never be found
    OS88UI_BTNREC px_btns, px_brects, px_lab_full, px_bflags, PX_NB
