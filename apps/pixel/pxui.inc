; =============================================================================
; os8088 - apps/pixel/pxui.inc
;
; PiXEL's WINDOW (SPEC.md 106.1 - 106.4, 106.15, 106.16): the layout, worked
; out from the live content box every time it is wanted, one painter per
; region - toolbar, tool column, canvas, panel column, filmstrip, status bar -
; the CACHES a command draws through, and the colour face's button painter.
;
; THE LAYOUT IS A FUNCTION OF THE WINDOW, NOT A COPY OF IT. px_layout reads
; OSAPI_WM_CONTENT, OSAPI_WM_GEOM and OSAPI_WM_DISPLAY and recomputes every
; rect from them; the paint proc calls it, and so does every press and every
; incremental draw. A window that MOVES does not call its paint proc (SPEC.md
; 11.96.12), so a rect banked on an earlier pass would describe somewhere the
; window used to be - and every cache below keeps its places RELATIVE to the
; content's corner for the same reason.
;
; EVERY PIXEL ONCE (CLAUDE.md performance rule 2). No region fills its ground
; and then draws on it. A text band is ONE opaque run padded to the band's
; width (px_tband), the ground beside a control is filled in strips that
; stop at the control's edge, and a button draws its own interior
; (os88ui_bdraw1, or px_bpaint on the colour face).
;
; A COMMAND DRAWS WHAT IT CHANGED (SPEC.md 106.15). A region painter draws its
; region whole and RECORDS what it drew - each button's flags and picture,
; each line's cells, the graph's and the thumbnail's keys, each filmstrip
; card's name - and px_usync walks the same window comparing the state with
; the record, drawing only what moved. W_PAINT and a command that moves a
; region (a panel shown, the filmstrip hidden, a card taken down) still draw
; regions whole; everything else goes through the records.
;
; Every routine here preserves every register, takes no lock and arms no
; clip: it is called from inside W_PAINT, where the kernel holds both, or
; through px_regdraw, which arms the clip for a callback.
; =============================================================================

; --- metrics (SPEC.md 106.1) ---------------------------------------------------
PX_TCW      equ 32                  ; tool column: 5 + a 22px button + 4, rule:
                                    ; 32, so the canvas starts on the byte grid
PX_TCL      equ 5                   ; ...the margin left of its buttons
PX_BTNW     equ 22                  ; an icon button: 16 + a 2px ring + frame
PX_ONEW     equ 30                  ; ...and the "1:1" text button, 3 glyphs
PX_MINCW    equ 160                 ; narrowest canvas the panel column leaves
PX_MINMID   equ 40                  ; shortest canvas the filmstrip leaves
PX_FULLH    equ 240                 ; content height below which the window is
                                    ; COMPACT: one panel at a time, no captions
PX_CAPW     equ 77 * 8              ; content width the captioned toolbar needs
PX_NAVW     equ 36                  ; the Navigator's three buttons, wide
PX_SBW      equ 14                  ; a status / filmstrip arrow button
PX_GROW     equ 24                  ; the grow box's corner (SPEC.md 11.1.1):
                                    ; its 13 columns and air enough that the
                                    ; status bar's arrows do not read as part
                                    ; of it
PX_ARH      equ 16                  ; a filmstrip arrow's rows
PX_INFON    equ 9                   ; Image Info's lines
PX_IPICK    equ 8                   ; ...the last the Eyedropper's (106.24)
PX_HISTN    equ 5                   ; Histogram's lines: the drop-down, then
                                    ; Mean, Std Dev, Min, Max
PX_DROPH    equ 12                  ; the drop-down's closed box
PX_CARDW    equ 104                 ; a filmstrip card's cell: 13 glyphs, so
                                    ; NAME.EXT fits under its box
PX_BOXW     equ 80                  ; ...and the card's box: a 4:3 picture
                                    ; at the pixel's own aspect (px_k_thumb)
PX_MAXCARD  equ 8                   ; cards a strip can hold (720 / 104 = 6)
; THE COLOUR FACE (SPEC.md 106.16): a 4bpp display, the full tier, and a
; content box this big - a VGA's is 640 x 418, an EGA's 640 x ~280
PX_COLW     equ 560
PX_COLH     equ 260
PX_PGAP     equ 4                   ; chrome round and between its panes
PX_TBCH     equ 2 + PXA_TBH + 2 + 1 ; its toolbar: air, a face, air, the rule
PX_SEPW     equ 8                   ; a toolbar separator's cell

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
    ; --- the face: colour on a 4bpp display, the full tier, a box this big
    ; (SPEC.md 106.16); below that a colour display gets the plain face,
    ; which is a complete face
    xor al, al
    cmp byte [px_bpp], 1
    jbe .nocol
    cmp byte [px_tier], 0
    jne .nocol
    cmp word [px_w], PX_COLW
    jb .nocol
    cmp word [px_h], PX_COLH
    jb .nocol
    inc al
.nocol:
    mov [px_col], al
    cmp al, [px_colwas]
    je .face
    mov [px_colwas], al
    call px_flags                   ; the buttons are ours on it (OS88UI_OWN)
.face:
    mov ax, 20                      ; a button: 16 rows + 4, or 8 + 4 on a CGA
    mov bx, 14                      ; the status bar
    cmp byte [px_half], 0
    je .m1
    mov ax, 12
    mov bx, 11
.m1:
    mov [px_btnh], ax
    mov [px_sth], bx
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
    mov ax, [px_k_hist + bx]
    mov [px_hsth], ax
    mov ax, [px_k_pw + bx]
    cmp byte [px_col], 0            ; the colour face's panes lose a column
    je .pw                          ; each side to their gaps and frames:
    add ax, 16                      ; the lines keep their 22 cells
.pw:
    mov [px_pw], ax

    ; --- the toolbar: captions on the full layout, if the width allows;
    ; the colour face's captions are inside its buttons ------------------
    mov byte [px_caps], 0
    cmp byte [px_col], 0
    je .plain
    mov ax, PX_TBCH
    jmp short .tb1
.plain:
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
    mov ax, [px_thh]                ; its rule, a card's box, the name's
    add ax, [px_lp]                 ; line and the air (no title strip:
    add ax, 1 + 6                   ; SPEC.md 106.15)
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
    and ax, 0xFFF8                  ; on the byte grid: the canvas is then a
.nopn:                              ; whole number of cells wide
    mov [px_pnx1], ax               ; the panel column's rule (or xr + 1)
    dec ax
    mov [px_cvx2], ax
    mov ax, [px_tcx2]
    inc ax
    mov [px_cvx1], ax

    call px_lay_panels
    call px_lay_buttons
    call px_lay_bodies
    call px_fslay
    call px_status_fit
    call px_filmlabel               ; the View menu follows the layout
    call px_vcalc                   ; the picture's view on this canvas
    call px_navplace                ; ...and the thumbnail's in its well
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
    mov [px_rkind], dl              ; (the pixel's aspect wants EGA's own)
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
    mov [px_c_strip], ax            ; highlight, highlight text
    mov ax, [si+4]
    mov [px_c_canvas], ax           ; canvas, canvas text
    mov ax, [si+6]
    mov [px_c_rule], ax             ; rule, chrome text
    mov al, [si+8]
    mov [px_c_card], al             ; a filmstrip card's inside
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- px_lay_panels - which panels are in the column, where, and how tall ------
; THERE ARE NO TITLE STRIPS (SPEC.md 106.15): a panel is its body, the
; Navigator's picture, the Histogram's graph and its Luminosity drop-down and
; Image Info's labelled lines saying what each is. On the plain face two
; panels are parted by one rule row; on the colour face each is a PANE, a
; grey frame round its body, with PX_PGAP rows of chrome round and between.
; The FULL layout stacks Navigator, Histogram and Image Info. A panel the
; column cannot hold is DROPPED from the bottom up - Info, then Histogram,
; then Navigator (SPEC.md 106.3) - and only Info may be cut short (a picture
; well or a column of buttons cut in half is neither); on the full tier Info,
; open and last, takes the rows left under it. The COMPACT layout shows
; [px_pcur] alone.
; out: px_pvis (bit n = panel n is in the column), px_pyw[n] its BODY's first
;      row, px_pbhw[n] its body's rows (0 = not laid out). Preserves all
px_lay_panels:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
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
    mov ax, [px_hsth]
    mov [px_pbody+2], ax
    mov ax, [px_lp]
    mov cx, PX_INFON
    mul cx
    add ax, 6
    mov [px_pbody+4], ax
    mov di, 1                       ; DI = a panel's frame rows, each side
    mov dx, [px_midy1]              ; DX = the next free row
    mov cx, [px_midy2]              ; CX = the column's last usable row
    cmp byte [px_col], 0
    jne .c
    xor di, di
    jmp short .c1
.c:
    add dx, PX_PGAP
    sub cx, PX_PGAP
.c1:
    mov [px_plast], cx
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
    cmp byte [px_pvis], 0           ; not the first: the rule, or the gap
    je .first
    inc dx
    cmp byte [px_col], 0
    je .first
    add dx, PX_PGAP - 1
.first:
    add dx, di                      ; past the pane's top frame row
    mov bx, si
    shl bx, 1
    mov ax, [px_pbody + bx]
    mov cx, [px_plast]
    sub cx, dx
    inc cx
    sub cx, di                      ; CX = the rows left, less the bottom row
    cmp ax, cx
    jle .fits
    cmp si, 2                       ; only Info may be cut short
    jb .undo
    mov ax, cx
.fits:
    mov cx, [px_lp]                 ; fewer rows than one line and its pad is
    add cx, 4                       ; no body at all
    cmp ax, cx
    jl .undo
    mov [px_pyw + bx], dx
    mov [px_pbhw + bx], ax
    mov cl, [px_bit + si]
    or [px_pvis], cl
    add dx, ax
    add dx, di                      ; past its bottom frame row
    dec dx                          ; (DX = its last row: the next panel's
    jmp short .skip1                ; separator comes after it)
.undo:
    sub dx, di                      ; this one is dropped: its separator and
    cmp byte [px_pvis], 0           ; frame row are not spent
    je .skip
    dec dx
    cmp byte [px_col], 0
    je .skip
    sub dx, PX_PGAP - 1
    jmp short .skip
.skip1:
    inc dx
.skip:
    inc si
    jmp short .next
.out:
    cmp byte [px_tier], 0           ; the full tier's column reads as ONE
    jne .ret                        ; stack: Image Info, when it is the last
    cmp word [px_pbhw + 4], 0       ; and open, takes the rows left under it
    je .ret
    mov ax, [px_plast]
    sub ax, [px_pyw + 4]
    inc ax
    cmp byte [px_col], 0
    je .i
    dec ax                          ; (its frame's bottom row)
.i:
    mov [px_pbhw + 4], ax
.ret:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_pbodyx - out AX..CX = the panels' bodies' columns (every panel has the
; column's width). Preserves the rest
px_pbodyx:
    mov ax, [px_pnx1]
    mov cx, [px_xr]
    inc ax                          ; plain: right of the column's rule
    cmp byte [px_col], 0
    je .r
    add ax, PX_PGAP                 ; colour: inside a pane's frame
    sub cx, PX_PGAP + 1
.r:
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
    mov ax, px_lab_full             ; the labels: our painter on the colour
    cmp byte [px_col], 0            ; face, the half-height pictures on a CGA
    je .lh                          ; (pxicons.inc), else the pictures
    mov ax, px_lab_col
    jmp short .lab
.lh:
    cmp byte [px_half], 0
    je .lab
    mov ax, px_lab_half
.lab:
    mov [px_btns + OS88UI_BT_LABELS], ax

    cmp byte [px_col], 0
    je .ptb
    call px_lay_tbc
    jmp short .tools
.ptb:
    call px_lay_tb

    ; --- the tool column ---------------------------------------------
.tools:
    mov ax, [px_cx0]
    add ax, PX_TCL
    mov cx, ax
    add cx, PX_BTNW - 1
    mov bx, [px_midy1]
    add bx, 3
    mov di, 3                       ; DI = the rows between two buttons
    cmp byte [px_col], 0
    je .tc
    mov ax, [px_cx0]                ; the colour face: a face is the column's
    add ax, PXA_TLX0                ; width, PXA_TLH rows, edge to edge
    mov cx, [px_cx0]
    add cx, PXA_TLX1
    mov bx, [px_midy1]
    add bx, 2 + PXA_TLY0
    mov di, PXA_TLH - (PXA_TLY1 - PXA_TLY0 + 1)
.tc:
    mov si, PX_B_T0
.tl:
    mov dx, bx
    add dx, [px_btnh]
    dec dx
    cmp byte [px_col], 0
    je .th
    mov dx, bx
    add dx, PXA_TLY1 - PXA_TLY0
.th:
    cmp dx, [px_midy2]
    jg .tdone                       ; this one and the rest do not fit
    call px_setrect
    mov bx, dx
    add bx, di
    inc bx
    inc si
    cmp si, PX_B_T0 + PX_NTOOL
    jb .tl
.tdone:

    ; --- the Navigator's three, when its body is on show ----------------
    test byte [px_pvis], 1
    jz .nonav
    cmp word [px_pbhw], 0
    je .nonav
    call px_pbodyx
    sub cx, 3                       ; CX = their right edge
    mov ax, cx
    sub ax, PX_NAVW - 1
    mov di, 12                      ; a text button: a glyph + 4
    cmp byte [px_half], 0
    je .nb
    mov di, 10
.nb:
    cmp byte [px_col], 0
    je .nb1
    mov di, 14                      ; ...a bevelled one: + its bevel
.nb1:
    mov dx, di
    mov bx, di
    add dx, bx
    add dx, bx
    add dx, 6                       ; DX = three buttons and two 3px gaps
    mov bx, [px_pbhw]
    sub bx, dx
    sar bx, 1
    add bx, [px_pyw]                ; BX = the first one's top
    mov si, PX_B_NZIN
.nl:
    mov dx, bx
    add dx, di
    dec dx
    call px_setrect
    mov bx, dx
    add bx, 4
    inc si
    cmp si, PX_B_NZIN + 3
    jb .nl
.nonav:

    ; --- the filmstrip's two arrows ---------------------------------------
    cmp byte [px_fson], 0
    je .nofs
    mov bx, [px_fsy1]               ; the arrows: PX_ARH rows (half that on a
    inc bx                          ; CGA), centred in the body under the rule
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
    cmp byte [px_pcon], 0           ; A PARAMETER CARD: its buttons, and
    je .nocard                      ; none under it (the EDIT part's,
    push cx                         ; SPEC.md 106.24)
    mov cl, EV_ULAY
    call px_ecall
    pop cx
.nocard:
    pop di
    pop si
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

; --- px_lay_bodies - what is inside each panel on show, from the layout -------
; The Navigator's well and the Histogram's well, column and drop-down. Here
; and not in the painters, because a press is tested against them and a
; press comes to a window that may have moved since it was painted.
; Preserves all
px_lay_bodies:
    push ax
    push bx
    push cx
    push dx
    push si
    test byte [px_pvis], 1
    jz .h
    mov cx, [px_pbhw]
    jcxz .h
    call px_pbodyx                  ; --- the Navigator --------------------
    mov [px_nbx1], ax
    mov si, PX_B_NZIN
    push ax
    call px_getrect                 ; AX = the buttons' left
    mov cx, ax
    pop ax
    sub cx, 6                       ; the well's frame: 4 in, 6 short of the
    add ax, 4                       ; buttons, 3 rows in at either end
    mov bx, [px_pyw]
    add bx, 3
    mov dx, [px_pyw]
    add dx, [px_pbhw]
    sub dx, 4
    mov [px_nwf], ax
    mov [px_nwf + 2], bx
    mov [px_nwf + 4], cx
    mov [px_nwf + 6], dx
    inc ax                          ; ...and its interior, which a press pans
    inc bx
    dec cx
    dec dx
    mov [px_nwell], ax
    mov [px_nwell + 2], bx
    mov [px_nwell + 4], cx
    mov [px_nwell + 6], dx
.h:
    test byte [px_pvis], 2
    jz .out
    mov cx, [px_pbhw + 2]
    jcxz .out
    call px_pbodyx                  ; --- the Histogram --------------------
    mov dx, cx
    sub dx, PX_HSTW * 8 + 2         ; the column: whole cells at the right
    mov [px_hcol], dx
    add ax, 4                       ; the well's frame
    mov [px_hwf], ax
    mov ax, [px_pyw + 2]
    add ax, 3
    mov [px_hwf + 2], ax
    mov ax, dx
    sub ax, 4
    mov [px_hwf + 4], ax
    mov ax, [px_pyw + 2]
    add ax, [px_pbhw + 2]
    sub ax, 4
    mov [px_hwf + 6], ax
    mov ax, dx                      ; the drop-down (OS88UI_DROP, 13.14)
    mov bx, [px_pyw + 2]
    add bx, 3
    sub cx, 2
    mov dx, bx
    add dx, PX_DROPH - 1
    mov [px_hdrop + OS88UI_DR_RECT], ax
    mov [px_hdrop + OS88UI_DR_RECT + 2], bx
    mov [px_hdrop + OS88UI_DR_RECT + 4], cx
    mov [px_hdrop + OS88UI_DR_RECT + 6], dx
    mov ax, [px_win]
    mov [px_hdrop + OS88UI_DR_WIN], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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

; --- px_lay_tbc - the colour face's toolbar (SPEC.md 106.16) ------------------
; A face is PXA_TBH rows and pxa_w[button] columns - a multiple of 8, so the
; card's planar blit takes it whole - and the button is the face less its
; PXA_GAP columns of chrome at the right: faces laid edge to edge are buttons
; with a gap between, and need no ground between them. A separator is a
; PX_SEPW cell. The items start a cell in, on the grid; one that would run
; past the right edge is hidden, and so is every one after it.
px_lay_tbc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [px_cx0]
    add di, 8                       ; DI = the next item's x
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
    mov ax, PX_SEPW                 ; AX = the item's width
    mov dl, [si + PX_TBD_BTN]
    cmp dl, 0xFF
    je .w
    push bx
    mov bl, dl
    xor bh, bh
    mov al, [pxa_w + bx]
    xor ah, ah
    pop bx
.w:
    mov cx, di
    add cx, ax
    dec cx                          ; CX = its last column
    cmp cx, [px_xr]
    jg .done
    mov byte [px_tbon + bx], 1
    push bx
    shl bx, 1
    mov [px_tbx + bx], di
    pop bx
    cmp dl, 0xFF
    je .adv
    push ax
    push bx
    push si
    sub cx, PXA_GAP                 ; the button: the face less its gap
    mov ax, di
    mov bx, [px_cy0]
    add bx, 2
    push dx
    mov dx, bx
    add dx, PXA_TBH - 1
    pop si
    and si, 0x00FF                  ; SI = the button
    call px_setrect
    pop si
    pop bx
    pop ax
.adv:
    add di, ax
    add si, PX_TBDSZ
    inc bx
    jmp short .it
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

; px_btn - draw button SI (an index) if it is laid out, and RECORD what was
; drawn - its flags and its picture - for px_bsync (SPEC.md 106.15).
; Preserves all
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
    mov bx, si
    shl bx, 1
    mov ax, [px_bflags + bx]
    mov cx, [px_lab_full + bx]      ; (Open's picture is Stop's while a
    shl bx, 1                       ; picture decodes: the key is the full
    mov [px_bkey + bx], ax          ; face's picture whatever the face)
    mov [px_bkey + bx + 2], cx
    inc byte [px_udrew]
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
; longer than the band is cut at its last cell. With [px_slot] naming a
; field SLOT the band is drawn through it (px_slotchk): only the cells that
; differ from what the slot says is on the glass. Preserves all
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
    cmp word [px_slot], 0           ; THROUGH A SLOT (SPEC.md 106.15): the
    je .whole                       ; cells that moved, or the band whole
    call px_slotchk                 ; when the slot does not describe it
    jnc .out
.whole:
    inc byte [px_udrew]
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

; px_wellframe - a well's frame alone, AX/BX/CX/DX: its interior is the
; caller's (a picture, a graph). Preserves all
px_wellframe:
    push ax
    mov al, [px_c_rule]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    ret

; =============================================================================
; THE REGIONS. Each draws its own rect whole and nothing outside it, so the
; paint proc can skip any region the damage rect misses (SPEC.md 106.4).
; =============================================================================

; --- the toolbar ---------------------------------------------------------------
px_draw_tb:
    cmp byte [px_col], 0
    je .plain
    jmp px_draw_tbc
.plain:
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
    mov word [px_slot], px_capslot  ; (Open's caption is Stop's while a
    call px_tband                   ; picture decodes)
    mov word [px_slot], 0
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
    push dx                         ; (the mul below - wave-1 NIT 1)
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
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the tool column -----------------------------------------------------------

; --- the colour face's toolbar (SPEC.md 106.16) ---------------------------------
; Two rows of air, the faces edge to edge on the grid - each button paints
; its whole face, its gap included (px_bpaint) - with a separator's cell
; between two groups, two rows of air and the rule. Every pixel once.
px_draw_tbc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [px_cx0]                ; the air above
    mov bx, [px_cy0]
    mov cx, [px_xr]
    mov dx, bx
    inc dx
    PXFILL [px_c_chrome]
    mov bx, [px_cy0]                ; BX..DX = the faces' rows
    add bx, 2
    mov dx, bx
    add dx, PXA_TBH - 1
    mov bp, [px_cx0]                ; BP = the next column not yet drawn
    xor di, di                      ; DI = the item
.it:
    cmp di, PX_TBN
    jae .tail
    cmp byte [px_tbon + di], 0
    je .tail
    mov si, di
    shl si, 1
    mov ax, [px_tbx + si]           ; the ground up to it (the margin)
    mov cx, ax
    dec cx
    push ax
    mov ax, bp
    PXFILL [px_c_chrome]
    pop ax
    mov si, di
    shl si, 1
    shl si, 1
    shl si, 1                       ; PX_TBDSZ = 8
    mov cl, [px_tbdef + si + PX_TBD_BTN]
    cmp cl, 0xFF
    jne .button
    mov cx, ax                      ; a separator: ground, the rule, ground
    inc cx
    PXFILL [px_c_chrome]
    add ax, 2
    call px_vrule
    inc ax
    mov cx, ax
    add cx, PX_SEPW - 4
    PXFILL [px_c_chrome]
    add cx, 1
    mov bp, cx
    jmp short .next
.button:
    xor ch, ch
    mov si, cx                      ; SI = the button, which paints its face
    call px_btn
    mov si, cx
    mov cl, [pxa_w + si]
    xor ch, ch
    add ax, cx
    mov bp, ax
.next:
    inc di
    jmp short .it
.tail:
    mov ax, bp                      ; ground to the right edge
    mov cx, [px_xr]
    PXFILL [px_c_chrome]
    mov ax, [px_cx0]                ; the air below, and the rule
    mov bx, dx
    inc bx
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

; --- the tool column -----------------------------------------------------------
px_draw_tools:
    cmp byte [px_col], 0
    je .plain
    jmp px_draw_toolsc
.plain:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov ax, [px_cx0]                ; the columns left of the buttons
    mov bx, [px_midy1]
    mov cx, ax
    add cx, PX_TCL - 1
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
    mov ax, [px_cx0]                ; ...and right of them
    add ax, PX_TCL + PX_BTNW
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
    add ax, PX_TCL
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

; the colour face's tool column (SPEC.md 106.16): two rows of air, the faces
; edge to edge - each the column's whole width, its rule included - and the
; ground and the rule under the last
px_draw_toolsc:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov ax, [px_cx0]                ; the air above
    mov bx, [px_midy1]
    mov cx, [px_tcx2]
    dec cx
    mov dx, bx
    inc dx
    PXFILL [px_c_chrome]
    mov ax, [px_tcx2]
    call px_vrule
    mov bp, [px_midy1]              ; BP = the next row not yet drawn
    add bp, 2
    mov si, PX_B_T0
.tl:
    call px_getrect
    cmp ax, cx
    jg .tail
    call px_btn                     ; its face, PXA_TLH rows
    add bp, PXA_TLH
    inc si
    cmp si, PX_B_T0 + PX_NTOOL
    jb .tl
.tail:
    mov ax, [px_cx0]
    mov bx, bp
    mov cx, [px_tcx2]
    dec cx
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
    cmp bx, dx
    jg .out
    mov ax, [px_tcx2]
    call px_vrule
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the panel column ----------------------------------------------------------
; NO TITLE STRIPS (SPEC.md 106.15). The plain face: the column's rule, and
; the bodies stacked with a rule row between two. The colour face: chrome
; round PANES - a grey frame round each body - PX_PGAP rows apart.
px_draw_panels:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [px_pnon], 0
    jne .on
    jmp .out
.on:
    call px_pbodyx                  ; AX..CX = the bodies' columns
    mov [px_rx1], ax
    mov [px_rx2], cx
    cmp byte [px_col], 0
    jne .cm
    mov ax, [px_pnx1]               ; plain: the column's rule
    mov bx, [px_midy1]
    mov dx, [px_midy2]
    call px_vrule
    jmp short .go
.cm:
    mov ax, [px_pnx1]               ; colour: the chrome either side of the
    mov bx, [px_midy1]              ; panes
    mov cx, [px_rx1]
    sub cx, 2
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
    mov ax, [px_rx2]
    add ax, 2
    mov cx, [px_xr]
    PXFILL [px_c_chrome]
.go:
    mov bp, [px_midy1]              ; BP = the next row not yet drawn
    xor di, di                      ; DI = the panel
.p:
    mov al, [px_bit + di]
    test [px_pvis], al
    jnz .p1
    jmp .pn
.p1:
    mov si, di
    shl si, 1
    mov bx, [px_pyw + si]           ; its body's first row
    mov cx, [px_pbhw + si]
    or cx, cx
    jnz .p2
    jmp .pn
.p2:
    mov dx, bx
    add dx, cx
    dec dx                          ; DX = its last
    push bx
    push dx
    cmp byte [px_col], 0
    jne .pane
    dec bx                          ; plain: the rows above it, the last of
    mov dx, bx                      ; them a rule when a panel is above
    mov ax, [px_rx1]
    mov cx, [px_rx2]
    cmp dx, bp
    jl .body
    dec dx
    push bx
    mov bx, bp
    PXFILL [px_c_chrome]
    pop bx
    cmp bx, [px_midy1]
    je .body
    mov dx, bx
    mov bx, cx
    call px_hrule
    jmp short .body
.pane:
    mov ax, [px_rx1]                ; colour: the chrome above its frame...
    dec ax
    mov cx, [px_rx2]
    inc cx
    mov dx, bx
    sub dx, 2
    push bx
    mov bx, bp
    PXFILL [px_c_chrome]
    pop bx
    pop dx                          ; ...and its frame
    push dx
    dec bx
    inc dx
    push ax
    mov al, [px_c_rule]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
.body:
    pop dx
    pop bx
    mov ax, [px_rx1]
    mov cx, [px_rx2]
    call [px_pbodyp + si]           ; AX..CX x, BX..DX y
    mov bp, dx
    inc bp
    cmp byte [px_col], 0
    je .pn
    inc bp                          ; (past the frame's bottom row)
.pn:
    inc di
    cmp di, 3
    jb .p
    mov ax, [px_rx1]                ; the column's ground under the last one
    mov cx, [px_rx2]
    cmp byte [px_col], 0
    je .t
    dec ax
    inc cx
.t:
    mov bx, bp
    mov dx, [px_midy2]
    PXFILL [px_c_chrome]
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the three bodies: AX..CX x, BX..DX y. Each preserves all -----------------
; Each draws its ground round what px_lay_bodies placed in it, and what can
; change (the picture, the graph, the drop-down, the lines) through its record.

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
    mov dx, [px_nwf + 2]            ; the air over the well's rows...
    dec dx
    PXFILL [px_c_body]
    mov bx, [px_nwf + 6]            ; ...and under them
    inc bx
    mov dx, [px_ry2]
    PXFILL [px_c_body]
    mov ax, [px_rx1]                ; the margin left of the well
    mov bx, [px_nwf + 2]
    mov cx, [px_nwf]
    dec cx
    mov dx, [px_nwf + 6]
    PXFILL [px_c_body]
    mov ax, [px_nwf]                ; the well: its frame, and the picture in
    mov cx, [px_nwf + 4]            ; it (px_navsync draws and records it)
    call px_wellframe
    mov word [px_kn], 0xFFFF
    call px_navsync
    mov si, PX_B_NZIN
    call px_getrect
    mov [px_bx1], ax                ; the button column's left
    mov ax, [px_nwf + 4]            ; the gap between it and the buttons
    inc ax
    mov cx, [px_bx1]
    dec cx
    mov bx, [px_nwf + 2]
    mov dx, [px_nwf + 6]
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
    mov dx, [px_nwf + 6]
    PXFILL [px_c_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_navsync - the Navigator's picture against its record [px_kn]: a
; different picture, depth or well is drawn (px_navthumb, which draws the
; frame fresh over it); the same one only has its frame follow the view.
; Layout done, clip armed. Preserves all
px_navsync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [px_pvis], 1
    jz .out
    cmp word [px_pbhw], 0
    je .out
    call px_nkey                  ; px_knew = what the well should show
    mov si, px_knew
    mov di, px_kn
    mov cx, 4
.c:
    mov ax, [si]
    cmp ax, [di]
    jne .draw
    add si, 2
    add di, 2
    loop .c
    call px_navframe_sync           ; the same picture: the frame alone
    jmp short .out
.draw:
    mov si, px_knew                 ; record, then draw
    mov di, px_kn
    mov cx, 4
.r:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .r
    mov ax, [px_nwell]
    mov bx, [px_nwell + 2]
    mov cx, [px_nwell + 4]
    mov dx, [px_nwell + 6]
    call px_navthumb
    inc byte [px_udrew]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_nkey - px_knew = the Navigator well's key: the picture shown (its
; serial, or 0 while there is none or it decodes), the depth, the well's
; size. Preserves all
px_nkey:
    push ax
    xor ax, ax
    call px_haspic
    jc .k
    cmp byte [px_cur + PXR_HAVE], 0
    je .k
    cmp byte [px_busy], 0           ; its tables being built (a commit's, or
    jne .k                          ; full screen's way out, 106.23): the
    mov al, [px_cur + PXR_SERIAL]   ; well's grey, and not recorded as drawn
    inc ah
.k:
    mov [px_knew], ax
    mov al, [px_bpp]
    mov ah, [px_col]
    mov [px_knew + 2], ax
    mov ax, [px_nwell + 4]
    sub ax, [px_nwell]
    mov [px_knew + 4], ax
    mov ax, [px_nwell + 6]
    sub ax, [px_nwell + 2]
    mov [px_knew + 6], ax
    pop ax
    ret

; Histogram: a graph well, and beside it the channel's drop-down and four
; statistics (SPEC.md 106.12)
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
    mov dx, [px_hwf + 2]
    dec dx
    PXFILL [px_c_body]              ; air on top
    mov bx, [px_hwf + 6]
    inc bx
    mov dx, [px_ry2]
    PXFILL [px_c_body]              ; ...and at the foot
    mov ax, [px_rx1]                ; the margin left of the well
    mov bx, [px_hwf + 2]
    mov cx, [px_hwf]
    dec cx
    mov dx, [px_hwf + 6]
    PXFILL [px_c_body]
    mov ax, [px_hwf]
    mov cx, [px_hwf + 4]
    call px_wellframe               ; the well: its frame, the graph in it
    mov ax, [px_hwf + 4]            ; the gap between it and the column
    inc ax
    mov cx, [px_hcol]
    dec cx
    PXFILL [px_c_body]
    mov ax, [px_hdrop + OS88UI_DR_RECT + 4]
    inc ax                          ; the columns right of the drop-down
    mov bx, [px_hdrop + OS88UI_DR_RECT + 2]
    mov cx, [px_rx2]
    mov dx, [px_hdrop + OS88UI_DR_RECT + 6]
    PXFILL [px_c_body]
    mov ax, [px_hcol]               ; the row of air under the drop-down
    mov bx, dx
    inc bx
    mov dx, bx
    PXFILL [px_c_body]
    mov word [px_kg], 0xFFFF        ; the graph, the drop-down and the lines
    mov word [px_kd], 0xFFFF        ; through their records - owed, so all
    call px_histsync                ; of them now
    mov bp, [px_hdrop + OS88UI_DR_RECT + 6]
    add bp, 2                       ; BP = the first statistic's row
    xor di, di
.l:
    cmp di, PX_HISTN - 1            ; the air under each line
    jae .ld
    mov dx, bp
    add dx, 7
    cmp dx, [px_hwf + 6]
    jg .ld
    mov ax, [px_hcol]
    mov bx, bp
    add bx, 8
    mov cx, [px_rx2]
    mov dx, bp
    add dx, [px_lp]
    dec dx
    cmp dx, [px_hwf + 6]
    jle .air
    mov dx, [px_hwf + 6]
.air:
    PXFILL [px_c_body]
    add bp, [px_lp]
    inc di
    jmp short .l
.ld:
    mov ax, [px_hcol]
    mov cx, [px_rx2]
    mov bx, bp
    mov dx, [px_hwf + 6]
    PXFILL [px_c_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_histsync - the Histogram's graph, drop-down and statistics against
; their records: each drawn when what it shows has moved. Layout done, clip
; armed. Preserves all
px_histsync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [px_pvis], 2
    jnz .on
    jmp .out
.on:
    cmp word [px_pbhw + 2], 0
    jne .on1
    jmp .out
.on1:
    ; --- the graph: counted or not, the channel, the picture, the depth,
    ; the well -------------------------------------------------------------
    mov al, [px_hok]
    mov ah, [px_hchan]
    mov [px_knew], ax
    mov al, [px_cur + PXR_SERIAL]
    mov ah, [px_col]
    mov [px_knew + 2], ax
    mov ax, [px_hwf + 4]
    sub ax, [px_hwf]
    mov [px_knew + 4], ax
    mov ax, [px_hwf + 6]
    sub ax, [px_hwf + 2]
    mov [px_knew + 6], ax
    mov si, px_knew
    mov di, px_kg
    mov cx, 4
.c:
    mov ax, [si]
    cmp ax, [di]
    jne .g
    add si, 2
    add di, 2
    loop .c
    jmp short .drop
.g:
    mov si, px_knew
    mov di, px_kg
    mov cx, 4
.r:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .r
    mov ax, [px_hwf]
    mov bx, [px_hwf + 2]
    mov cx, [px_hwf + 4]
    mov dx, [px_hwf + 6]
    inc ax
    inc bx
    dec cx
    dec dx
    call px_hgraph
    inc byte [px_udrew]
.drop:
    ; --- the drop-down: greyed while nothing is counted, its channel -----
    mov al, [px_hok]
    mov ah, [px_hchan]
    cmp ax, [px_kd]
    je .lines
    mov [px_kd], ax
    mov bx, px_hdrop
    xor di, di
    or al, al                       ; nothing to show: greyed, and its press
    jnz .live                       ; refused (13.14.5)
    mov di, OS88UI_DIS
.live:
    call os88ui_drop
    inc byte [px_udrew]
.lines:
    ; --- the four statistics, each through its slot ------------------------
    mov dx, [px_hdrop + OS88UI_DR_RECT + 6]
    add dx, 2                       ; DX = the first one's row
    xor di, di
.l:
    cmp di, PX_HISTN - 1
    jae .out
    mov ax, dx
    add ax, 7
    cmp ax, [px_hwf + 6]
    jg .out
    call px_statline                ; px_cline = statistic DI
    mov ax, di
    add ax, PX_SL_HIST
    call px_slotof
    mov [px_slot], ax
    call px_pbodyx                  ; CX = the body's last column
    mov ax, [px_hcol]
    mov si, px_cline
    push bx
    push di
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    xor di, di
    call px_tband
    pop di
    pop bx
    mov word [px_slot], 0
    add dx, [px_lp]
    inc di
    jmp short .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Image Info: its lines, each one run across the body through its slot
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
    cmp di, PX_INFON                ; the air under each line
    jae .ld
    mov dx, bp
    add dx, 7
    cmp dx, [px_ry2]
    jg .ld
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
    call px_infosync                ; the lines, owed: all of them
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_infosync - Image Info's lines through their slots: each draws the cells
; that moved. Layout done, clip armed. Preserves all
px_infosync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [px_pvis], 4
    jz .out
    mov cx, [px_pbhw + 4]
    jcxz .out
    mov bx, [px_pyw + 4]
    add cx, bx
    dec cx
    mov [px_ry2], cx                ; the body's last row
    mov dx, bx
    add dx, 3                       ; DX = the first line's row
    xor di, di
.l:
    cmp di, PX_INFON
    jae .out
    mov ax, dx
    add ax, 7
    cmp ax, [px_ry2]
    jg .out
    call px_infoline                ; px_cline = line DI
    mov ax, di
    add ax, PX_SL_INFO
    call px_slotof
    mov [px_slot], ax
    call px_pbodyx                  ; AX..CX = the body's columns
    mov si, px_cline
    push bx
    push di
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    xor di, di
    call px_tband
    pop di
    pop bx
    mov word [px_slot], 0
    add dx, [px_lp]
    inc di
    jmp short .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the canvas ----------------------------------------------------------------
; With a picture (decoded, or decoding): the part of it the damage rect
; meets, rendered (pxview.inc's px_rcanvas, SPEC.md 106.11). Without one: the
; empty state.
px_draw_canvas:
    call px_haspic
    jc px_draw_empty
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_cvx1]               ; the canvas, cut to the damage
    cmp ax, [px_dx1]
    jge .a
    mov ax, [px_dx1]
.a:
    mov bx, [px_midy1]
    cmp bx, [px_dy1]
    jge .b
    mov bx, [px_dy1]
.b:
    mov cx, [px_cvx2]
    cmp cx, [px_dx2]
    jle .c
    mov cx, [px_dx2]
.c:
    mov dx, [px_midy2]
    cmp dx, [px_dy2]
    jle .d
    mov dx, [px_dy2]
.d:
    cmp ax, cx
    jg .out
    cmp bx, dx
    jg .out
    cmp byte [px_busy], 0           ; the tables match this display, or are
    jne .r                          ; made now (106.11)
    call px_tabsync
.r:
    call px_rcanvas
    cmp ax, [px_cvx1]               ; the painted spans are what a render of
    jne .out                        ; the WHOLE canvas leaves; a part of it
    cmp bx, [px_midy1]              ; leaves them as they were, so a decode's
    jne .out                        ; rows outside the damage are still owed
    cmp cx, [px_cvx2]
    jne .out
    cmp dx, [px_midy2]
    jne .out
    call px_rowsall
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the canvas: the empty state (SPEC.md 106.3) -------------------------------
; A picture, "Open a picture  (Ctrl+O)", and - once a file has been picked -
; what it is. Centred as one block, every row of it drawn once.
px_draw_empty:
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
    mov dx, bx                      ; the first line: how to open one - or,
    mov si, px_s_empty              ; once a file was refused, why it was
    cmp byte [px_fhave], 0
    je .l1
    cmp byte [px_cvmsg], 0
    je .l1
    mov si, px_cvmsg
.l1:
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


; px_statline - the Histogram's statistic DI (Mean, Std Dev, Min, Max) as
; one line into px_cline: its label padded to 8, then its value or "-".
; Preserves all
px_statline:
    push ax
    push bx
    push si
    push di
    push dx
    mov bx, di
    shl bx, 1
    mov si, [px_hlabs + bx]
    mov ax, [px_hvals + bx]         ; the value's word
    mov di, px_cline
    call px_strcpy
    cmp byte [px_hok], 0
    je .dash
    mov bx, ax
    mov ax, [bx]
    xor dx, dx
    call px_u32n
    jmp short .out
.dash:
    mov si, px_s_dash
    call px_strcat
.out:
    pop dx
    pop di
    pop si
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
; A rule over it - there is no title strip (SPEC.md 106.15): the folder's
; count is the status bar's "n of N" - then the two arrows and the cards.
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
    mov ax, [px_cx0]                ; the rule
    mov bx, [px_xr]
    mov dx, [px_fsy1]
    call px_hrule
    mov ax, [px_fsy1]               ; the body: under the rule, over the bar
    inc ax
    mov [px_ry1], ax
    mov ax, [px_sty1]
    dec ax
    mov [px_ry2], ax
    mov si, PX_B_FSL                ; the left arrow, and its margin round it
    call px_getrect
    mov [px_bx1], cx
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_cx0]
    mov bx, [px_ry1]
    mov dx, [px_ry2]
    call px_aroundbtn
    pop dx
    pop cx
    pop bx
    pop ax
    call px_btn
    mov si, PX_B_FSR                ; the right one
    call px_getrect
    mov [px_bx2], ax
    push ax
    push bx
    push cx
    push dx
    mov cx, [px_xr]
    mov bx, [px_ry1]
    mov dx, [px_ry2]
    call px_aroundbtn
    pop dx
    pop cx
    pop bx
    pop ax
    call px_btn
    call px_fscards                 ; the cards between them
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_aroundbtn - the ground round button SI's rect inside the column AX..CX,
; rows BX..DX (the band), in the filmstrip's ground. Preserves all
px_aroundbtn:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [px_rx1], ax
    mov [px_rx2], cx
    mov [px_tyy], dx
    push si
    call px_getrect                 ; AX..CX, BX..DX = the button
    pop si
    mov di, [px_tyy]
    push ax
    push cx
    push dx
    mov ax, [px_rx1]                ; above it
    mov cx, [px_rx2]
    mov dx, bx
    dec dx
    mov bx, [px_ry1]
    PXFILL [px_c_chrome]
    pop dx
    pop cx
    pop ax
    push ax
    push bx
    push cx
    mov ax, [px_rx1]                ; below it
    mov cx, [px_rx2]
    mov bx, dx
    inc bx
    mov dx, di
    PXFILL [px_c_chrome]
    pop cx
    pop bx
    pop ax
    push cx
    mov cx, ax                      ; left of it
    dec cx
    mov ax, [px_rx1]
    PXFILL [px_c_chrome]
    pop cx
    mov ax, cx                      ; right of it
    inc ax
    mov cx, [px_rx2]
    PXFILL [px_c_chrome]
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_fscards - THE FILMSTRIP'S CARDS (SPEC.md 106.12) in columns AX..CX of
; the body [px_ry1]..[px_ry2]: the folder's pictures around the open one, a
; framed card each with its format's name, the file's name under it, the
; open one's card in the highlight's colours. With no folder yet the cards
; are blank and one line says how to fill them. The thumbnails are wave 5's.
; The cells are px_fslay's; each card is drawn whole and recorded (px_ckey),
; so px_fsync redraws the cards whose name or highlight moved and no other.
px_fscards:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [px_fcx1]               ; the ground left of the cells...
    mov bx, [px_fry1]
    mov cx, [px_fcl]
    dec cx
    mov dx, [px_fry2]
    PXFILL [px_c_chrome]
    mov ax, [px_fcn]                ; ...and right of them
    mov cx, PX_CARDW
    mul cx
    add ax, [px_fcl]
    mov cx, [px_fcx2]
    mov bx, [px_fry1]
    mov dx, [px_fry2]
    PXFILL [px_c_chrome]
    mov ax, [px_nsum]               ; the cards' records: these names
    mov [px_kfser], ax
    xor bp, bp                      ; BP = the cell
.cell:
    cmp bp, [px_fcn]
    jb .c1
    jmp .msg
.c1:
    call px_card
    inc bp
    jmp short .cell
.msg:
    call px_fsmsg
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_card - card BP: its cell, its box, the format in it, the name under it.
; Every pixel of the cell once. Preserves all
px_card:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call px_ckey                    ; ...and what it shows, recorded
    mov bx, bp
    shl bx, 1
    mov [px_kcard + bx], ax
    mov [px_fckey], ax              ; (its thumbnail bit: SPEC.md 106.21)
    inc byte [px_udrew]
    mov ax, bp
    mov cx, PX_CARDW
    mul cx
    add ax, [px_fcl]
    mov [px_fcx], ax                ; the cell's left
    add ax, (PX_CARDW - PX_BOXW) / 2
    mov [px_fcb], ax                ; the box's left
    ; which name, and is it the open picture?
    mov byte [px_fccur], 0
    mov word [px_fcname], px_s_empty0
    mov ax, bp
    add ax, [px_fcs]
    cmp ax, [px_nnames]
    jb .some
    cmp word [px_nnames], 0         ; past a folder's last picture: no card,
    je .named                       ; just the ground (blank cards are for the
    mov ax, [px_fcx]                ; folder not yet known)
    mov bx, [px_fry1]
    mov cx, ax
    add cx, PX_CARDW - 1
    mov dx, [px_fry2]
    PXFILL [px_c_chrome]
    jmp .done
.some:
    cmp ax, [px_ncur]
    jne .nc
    mov byte [px_fccur], 1
.nc:
    mov cx, PX_NREC
    mul cx
    add ax, px_names
    mov [px_fcname], ax
.named:
    ; --- above the name: ground, the box, ground --------------------------
    mov ax, [px_fcx]                ; left of the box
    mov bx, [px_fry1]
    mov cx, [px_fcb]
    dec cx
    mov dx, [px_fcny]
    dec dx
    PXFILL [px_c_chrome]
    mov ax, [px_fcb]                ; right of it
    add ax, PX_BOXW
    mov cx, [px_fcx]
    add cx, PX_CARDW - 1
    PXFILL [px_c_chrome]
    mov ax, [px_fcb]                ; over it
    mov cx, ax
    add cx, PX_BOXW - 1
    mov dx, [px_fct]
    dec dx
    PXFILL [px_c_chrome]
    mov bx, [px_fct]                ; under it, to the name
    add bx, [px_thh]
    mov dx, [px_fcny]
    dec dx
    PXFILL [px_c_chrome]
    mov bx, [px_fct]                ; the box
    mov dx, bx
    add dx, [px_thh]
    dec dx
    call px_wellframe
    inc ax
    inc bx
    dec cx
    dec dx
    mov [px_fcix], ax
    mov [px_fciy2], dx
    mov al, [px_c_card]             ; its inside: the open one's in the
    mov ah, [px_c_text]             ; strip's colours
    cmp byte [px_fccur], 0
    je .col
    mov al, [px_c_strip]
    mov ah, [px_c_stript]
.col:
    mov [px_fcpair], ax
    mov si, [px_fcname]
    cmp byte [si], 0
    jne .th
    mov ax, [px_fcix]               ; a blank card: one fill
    PXFILL [px_fcpair]
    jmp short .name
.th:
    test word [px_fckey], 0x2000    ; ITS THUMBNAIL, when one is made and the
    jz .fmt                         ; display can draw it (SPEC.md 106.21)
    mov ax, bp
    add ax, [px_fcs]
    call px_thcard
    jmp short .name
.fmt:
    push si                         ; the format's name, by the extension
    call px_extfmt                  ; AL = its PXF_*
    call px_fmtname                 ; SI = the name
    mov [px_fcfmt], si
    pop si
    mov ax, [px_fct]                ; the text's row: the box's middle
    mov dx, [px_thh]
    sub dx, 8
    sar dx, 1
    add ax, dx
    mov [px_fcmid], ax
    mov ax, [px_fcix]               ; above the text
    mov dx, [px_fcmid]
    dec dx
    PXFILL [px_fcpair]
    mov bx, [px_fcmid]              ; below it
    add bx, 8
    mov dx, [px_fciy2]
    PXFILL [px_fcpair]
    mov ax, [px_fcix]               ; the text: one run, centred
    mov dx, [px_fcmid]
    mov si, [px_fcfmt]
    mov bl, [px_fcpair + 1]
    mov bh, [px_fcpair]
    mov di, 1
    call px_tband
.name:
    ; --- the name's line, and the ground under it ---------------------------
    mov ax, [px_fcx]
    mov cx, ax
    add cx, PX_CARDW - 1
    mov dx, [px_fcny]
    mov si, [px_fcname]
    mov bl, [px_c_text]
    mov bh, [px_c_chrome]
    cmp byte [px_fccur], 0          ; the open picture's name in the
    je .nl                          ; highlight's colours: its thumbnail
    mov bl, [px_c_stript]           ; leaves little of the box's to say so
    mov bh, [px_c_strip]
.nl:
    mov di, 1
    call px_tband
    mov bx, [px_fcny]
    add bx, 8
    mov dx, [px_fry2]
    PXFILL [px_c_chrome]
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; px_fslay - the filmstrip's cells, from the layout: between the arrows, on
; the grid, at most PX_MAXCARD; which of the folder's names they show (the
; open one in the middle where it can be); the box's and the name's rows.
; Preserves all
px_fslay:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [px_fcn], 0
    cmp byte [px_fson], 0
    jne .on
    jmp .out
.on:
    mov ax, [px_fsy1]               ; the body: under the rule, over the bar
    inc ax
    mov [px_fry1], ax
    mov ax, [px_sty1]
    dec ax
    mov [px_fry2], ax
    mov si, PX_B_FSR
    call px_getrect
    dec ax
    mov [px_fcx2], ax
    mov si, PX_B_FSL
    call px_getrect
    mov ax, cx
    inc ax
    mov [px_fcx1], ax
    add ax, 4 + 7                   ; the first cell, on the grid
    and ax, 0xFFF8
    mov [px_fcl], ax
    mov dx, [px_fcx2]               ; cells that fit
    sub dx, 4
    sub dx, ax
    inc dx
    xor bx, bx
    or dx, dx
    jle .nc
    mov ax, dx
    xor dx, dx
    mov cx, PX_CARDW
    div cx
    mov bx, ax
    cmp bx, PX_MAXCARD
    jbe .nc
    mov bx, PX_MAXCARD
.nc:
    mov [px_fcn], bx
    mov ax, [px_ncur]               ; the window of names: centred on the open
    mov bx, [px_fcn]                ; one, turned by the pagers (106.21)
    shr bx, 1
    sub ax, bx
    mov dx, ax                      ; (DX = the centred start)
    add ax, [px_fsoff]
    jns .s1
    xor ax, ax
.s1:
    mov bx, [px_nnames]
    sub bx, [px_fcn]
    jns .s2
    xor bx, bx
.s2:
    cmp ax, bx
    jle .s3
    mov ax, bx
.s3:
    mov [px_fcs], ax
    sub ax, dx                      ; the turn as it stands, clamped: a pager
    mov [px_fsoff], ax              ; pressed past the end does not bank it
    call px_pgflags
    mov ax, [px_fry1]               ; the rows: the card's box, then the
    add ax, 2                       ; name's line
    mov [px_fct], ax
    add ax, [px_thh]
    add ax, 2
    mov [px_fcny], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_ckey - AX = what card BP shows: its name's place in the folder (+ 4000h
; for the open picture's), 0FFFEh a blank card (no folder yet), 0FFFDh no
; card (past the folder's last picture). Preserves the rest
px_ckey:
    mov ax, bp
    add ax, [px_fcs]
    cmp ax, [px_nnames]
    jb .some
    mov ax, 0xFFFE
    cmp word [px_nnames], 0
    je .r
    dec ax
.r:
    ret
.some:
    call px_thkey                   ; + 2000h: its thumbnail is drawn (106.21)
    push ax
    and ax, 0x1FFF
    cmp ax, [px_ncur]
    pop ax
    jne .r
    or ax, 0x4000
    ret

; px_fsync - each card against its record: a card whose name or highlight
; moved is drawn, and no other. A new folder owes them all. Layout done,
; clip armed. Preserves all
px_fsync:
    push ax
    push bx
    push bp
    cmp byte [px_fson], 0
    je .out
    mov ax, [px_nsum]               ; another folder's names
    cmp ax, [px_kfser]
    je .same
    mov [px_kfser], ax
    xor bx, bx
.owe:
    mov word [px_kcard + bx], 0xFFFF
    add bx, 2
    cmp bx, PX_MAXCARD * 2
    jb .owe
.same:
    xor bp, bp
    mov byte [px_fsmany], 0
.c:
    cmp bp, [px_fcn]
    jae .msg
    call px_ckey
    mov bx, bp
    shl bx, 1
    cmp ax, [px_kcard + bx]
    je .n
    call px_card
    inc byte [px_fsmany]
.n:
    inc bp
    jmp short .c
.msg:
    cmp byte [px_fsmany], 0         ; the cards moved under the blank names'
    je .out                         ; line: it goes back over them
    call px_fsmsg
.out:
    pop bp
    pop bx
    pop ax
    ret

; px_fsmsg - with no folder known, one line over the blank cards' names
; saying how to fill them. Preserves all
px_fsmsg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [px_nnames], 0
    jne .out
    cmp word [px_fcn], 0
    je .out
    mov ax, [px_fcn]
    mov bx, PX_CARDW
    mul bx
    add ax, [px_fcl]
    dec ax
    mov cx, ax                      ; CX = the last cell's last column
    mov ax, [px_fcl]
    mov dx, [px_fcny]
    mov si, px_fsline
    mov bl, [px_c_text]
    mov bh, [px_c_chrome]
    mov di, 1
    call px_tband
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the status bar --------------------------------------------------------------
; A rule, the fields - each ONE run on the 8-pixel grid, parted by a
; separator's cell with its rule inside (wave-1 review MIN-6: a 1-pixel rule
; between two runs put every field after the first off the grid) - and the
; two arrows. A field whose value changes is redrawn alone, through its slot:
; the cells that moved (SPEC.md 106.3, 106.15).
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
    mov bx, [px_tyy0]               ; BX..DX = the text's rows
    mov dx, bx
    add dx, 7
    mov ax, [px_cx0]                ; a cell of margin
    mov cx, ax
    add cx, 7
    PXFILL [px_c_chrome]
    xor si, si                      ; SI = the field
    xor di, di                      ; DI = fields drawn so far
.f:
    mov al, [px_bit + si]
    test [px_skeep], al
    jz .n
    or di, di                       ; a separator's cell in front of every
    jz .run                         ; field but the first
    push si
    shl si, 1
    mov ax, [px_sfxw + si]
    pop si
    sub ax, PX_SEPW
    mov cx, ax
    add cx, 2
    PXFILL [px_c_chrome]
    add ax, 3
    call px_vrule
    inc ax
    mov cx, ax
    add cx, 3
    PXFILL [px_c_chrome]
.run:
    mov bx, si
    call px_sfrun                   ; owed: the whole field
    mov bx, [px_tyy0]
    inc di
.n:
    inc si
    cmp si, PX_NSF
    jb .f
    call px_swidth                  ; the text rows past the last field
    add ax, [px_cx0]
    mov cx, [px_sbx]
    sub cx, 3
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
; Eight fields, each a fixed width in glyphs, a cell of margin before the
; first and a separator's cell between two. The bar holds as many as fit:
; px_status_fit drops them in px_sdrop's order until the rest do, so the
; narrow screens keep what matters most, and banks each kept field's x.

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
    mov ax, [px_cx0]                ; each kept field's x, on the grid
    add ax, 8
    xor bx, bx
.x:
    mov cl, [px_bit + bx]
    test [px_skeep], cl
    jz .xn
    mov si, bx
    shl si, 1
    mov [px_sfxw + si], ax
    mov cl, [px_sfw + bx]
    xor ch, ch
    inc cx                          ; its cells and the separator's
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

; px_swidth - out AX = the pixels the kept fields need from the content's
; left: the margin's cell, 8 x width each, a separator's cell between two
px_swidth:
    push bx
    push cx
    mov ax, 8 - 8                   ; (the first field has no separator)
    xor bx, bx
.w:
    mov cl, [px_bit + bx]
    test [px_skeep], cl
    jz .n
    mov cl, [px_sfw + bx]
    xor ch, ch
    inc cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add ax, cx
.n:
    inc bx
    cmp bx, PX_NSF
    jb .w
    pop cx
    pop bx
    ret

; px_sfrun - field BX, its value padded to its width, at its banked x on the
; text row, through its slot: the cells that moved. Clip armed. Preserves all
px_sfrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, px_cline
    call px_sfcopy
    mov byte [di], 0
    mov ax, bx
    add ax, PX_SL_STAT
    call px_slotof
    mov [px_slot], ax
    mov si, bx
    shl si, 1
    mov ax, [px_sfxw + si]
    mov cl, [px_sfw + bx]
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx
    mov dx, [px_tyy0]
    mov si, px_cline
    mov bl, [px_c_text]
    mov bh, [px_c_chrome]
    xor di, di
    call px_tband
    mov word [px_slot], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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

; =============================================================================
; THE RECORDS A COMMAND DRAWS THROUGH (SPEC.md 106.15)
; =============================================================================
; One record per kind of thing on the glass:
;
;   a button        its flags and its picture as drawn       px_bkey
;   a line of text  its cells as drawn (a SLOT): the status  px_slots
;                   fields, Image Info's lines, the
;                   Histogram's statistics, the caption row
;   the graph       counted, channel, picture, face, well    px_kg
;   the drop-down   counted, channel                         px_kd
;   the thumbnail   picture, depth, face, well               px_kn
;   a card          the name's place and the highlight       px_kcard
;
; A region painter draws its region whole and records; px_owe forgets a
; region's records first, so a painter under W_PAINT draws all of it
; whatever they said. px_usync walks the window comparing.

PX_SLOTSZ   equ 8 + 24              ; a slot: x, y, cells, room, ink + ground,
PX_SLOTN    equ 24                  ; ...and the cells themselves
PX_SL_STAT  equ 0                   ; the slots: the status fields...
PX_SL_INFO  equ PX_NSF              ; ...Image Info's lines...
PX_SL_HIST  equ PX_SL_INFO + PX_INFON   ; ...the Histogram's statistics
PX_NSLOT    equ PX_SL_HIST + PX_HISTN - 1
SL_X        equ 0                   ; word: the run's first cell, from the
SL_Y        equ 2                   ; word: content's corner (a move keeps it)
SL_N        equ 4                   ; byte: its cells; 0 = owed
SL_ROOM     equ 5                   ; byte: the cells the slot can hold
SL_PAIR     equ 6                   ; word: ink, ground
SL_CELLS    equ 8

; px_slotof - AX = a slot's number: AX = its address. Preserves the rest
px_slotof:
    push dx
    mov dx, PX_SLOTSZ
    mul dx
    add ax, px_slots
    pop dx
    ret

; px_slotchk - px_tband's line (px_line, [px_tbn] cells at [px_tbcx],
; [px_tby], in [px_tbink]) against the slot [px_slot]. CF = 1: the slot does
; not describe this band - owed, moved, another width or another colour - so
; it is recorded now and the caller draws the band whole. CF = 0: the band
; is on the glass but for the cells that differ, and those are drawn now as
; ONE run - none at all when nothing moved. Preserves all
px_slotchk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [px_slot]
    mov cx, [px_tbn]
    cmp cl, [bx + SL_ROOM]
    jbe .room
    mov byte [bx + SL_N], 0         ; too long for it: drawn whole, unrecorded
    stc
    jmp .out
.room:
    mov ax, [px_tbcx]
    sub ax, [px_cx0]
    mov dx, [px_tby]
    sub dx, [px_cy0]
    cmp cl, [bx + SL_N]
    jne .rec
    cmp ax, [bx + SL_X]
    jne .rec
    cmp dx, [bx + SL_Y]
    jne .rec
    mov ax, [px_tbink]
    cmp ax, [bx + SL_PAIR]
    jne .rec
    xor si, si                      ; the first cell that differs...
.f:
    cmp si, cx
    jae .same
    mov al, [bx + SL_CELLS + si]
    cmp al, [px_line + si]
    jne .found
    inc si
    jmp short .f
.same:
    clc
    jmp short .out
.found:
    mov di, cx                      ; ...and the last
.l:
    dec di
    mov al, [bx + SL_CELLS + di]
    cmp al, [px_line + di]
    je .l
    push si                         ; the run between them, recorded and
    mov dx, di                      ; drawn
    sub dx, si
    inc dx                          ; DX = its cells
    xor di, di
.cp:
    mov al, [px_line + si]
    mov [bx + SL_CELLS + si], al
    mov [px_cline + di], al
    inc si
    inc di
    dec dx
    jnz .cp
    mov byte [px_cline + di], 0
    pop si
    mov cl, 3
    shl si, cl
    mov cx, [px_tbcx]
    add cx, si
    mov dx, [px_tby]
    mov si, px_cline
    mov ax, [px_tbink]
    call OSAPI_FONT_RUN
    inc byte [px_udrew]
    clc
    jmp short .out
.rec:
    mov ax, [px_tbcx]               ; the slot is this band now
    sub ax, [px_cx0]
    mov [bx + SL_X], ax
    mov [bx + SL_Y], dx
    mov [bx + SL_N], cl
    mov ax, [px_tbink]
    mov [bx + SL_PAIR], ax
    xor si, si
.rc:
    mov al, [px_line + si]
    mov [bx + SL_CELLS + si], al
    inc si
    loop .rc
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_slotsinit - every slot owed, and its room set. Once, at launch
px_slotsinit:
    push ax
    push bx
    push cx
    mov bx, px_slots
    mov cx, PX_NSLOT
.s:
    mov byte [bx + SL_N], 0
    mov byte [bx + SL_ROOM], PX_SLOTN
    add bx, PX_SLOTSZ
    loop .s
    mov byte [px_capslot + SL_N], 0 ; the caption row's: a whole line
    mov byte [px_capslot + SL_ROOM], PX_LINEMAX
    pop cx
    pop bx
    pop ax
    ret

; px_oweslots - AX = the first slot, CX = how many: owed. Preserves all
px_oweslots:
    push ax
    push bx
    push cx
    call px_slotof
    mov bx, ax
.o:
    mov byte [bx + SL_N], 0
    add bx, PX_SLOTSZ
    loop .o
    pop cx
    pop bx
    pop ax
    ret

; px_owebtns - AX = the first button, CX = how many: their records owed.
; Preserves all
px_owebtns:
    push bx
    push cx
    mov bx, ax
    shl bx, 1
    shl bx, 1
.o:
    mov word [px_bkey + bx], 0xFFFF
    add bx, 4
    loop .o
    pop cx
    pop bx
    ret

; px_owe - AL = a mask of PX_R_* regions about to be drawn whole: their
; records owed, so the painter draws everything in them. Preserves all
px_owe:
    push ax
    push cx
    push bx
    mov bl, al
    test bl, PX_R_TB
    jz .t
    mov ax, PX_B_OPEN
    mov cx, PX_B_T0
    call px_owebtns
    mov byte [px_capslot + SL_N], 0
.t:
    test bl, PX_R_TOOLS
    jz .p
    mov ax, PX_B_T0
    mov cx, PX_NTOOL
    call px_owebtns
.p:
    test bl, PX_R_PANELS
    jz .f
    mov ax, PX_B_NZIN
    mov cx, 3
    call px_owebtns
    mov ax, PX_SL_INFO
    mov cx, PX_INFON + PX_HISTN - 1
    call px_oweslots
    mov word [px_kg], 0xFFFF        ; (no key is: a counted byte is 0 or 1)
    mov word [px_kn], 0xFFFF
    mov word [px_kd], 0xFFFF
.f:
    test bl, PX_R_FS
    jz .s
    mov ax, PX_B_FSL
    mov cx, 2
    call px_owebtns
    not word [px_kfser]             ; (px_fscards records them all)
.s:
    test bl, PX_R_STATUS
    jz .out
    mov ax, PX_B_SPREV
    mov cx, 2
    call px_owebtns
    mov ax, PX_SL_STAT
    mov cx, PX_NSF
    call px_oweslots
.out:
    pop bx
    pop cx
    pop ax
    ret

; px_bsync - every button laid out against its record: one whose flags or
; picture moved is drawn, and no other. Clip armed. Preserves all
px_bsync:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
.b:
    call px_getrect
    cmp ax, cx
    jg .n                           ; not laid out
    mov bx, si
    shl bx, 1
    mov ax, [px_bflags + bx]
    mov cx, [px_lab_full + bx]
    shl bx, 1
    cmp ax, [px_bkey + bx]
    jne .d
    cmp cx, [px_bkey + bx + 2]
    je .n
.d:
    call px_btn                     ; (which records it)
.n:
    inc si
    cmp si, PX_NB
    jb .b
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_usync - THE WALK (SPEC.md 106.15): the window against its records -
; buttons, the caption row, the status fields, the Navigator's picture and
; frame, the Histogram's graph, drop-down and statistics, Image Info's
; lines, the filmstrip's cards - each drawing what moved. Layout done, clip
; armed, the lock held. Preserves all
px_usync:
    push ax
    push bx
    call px_bsync
    cmp byte [px_caps], 0           ; the caption row: Open's caption is
    je .st                          ; Stop's while a picture decodes
    cmp byte [px_col], 0
    jne .st
    call px_capsync
.st:
    xor bx, bx                      ; the status fields
.f:
    mov al, [px_bit + bx]
    test [px_skeep], al
    jz .fn
    call px_sfrun
.fn:
    inc bx
    cmp bx, PX_NSF
    jb .f
    mov byte [px_sdirty], 0
    cmp byte [px_pnon], 0
    je .fs
    call px_navsync
    call px_histsync
    call px_infosync
.fs:
    call px_fsync
    pop bx
    pop ax
    ret

; px_capsync - the plain face's caption row through its slot. Preserves all
px_capsync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call px_capline
    mov bx, [px_cy0]
    add bx, 2
    add bx, [px_btnh]
    inc bx                          ; the caption row: under the buttons and
    mov dx, bx                      ; a row of air
    mov si, px_cline
    mov ax, [px_cx0]
    mov cx, [px_xr]
    mov bl, [px_c_text]
    mov bh, [px_c_chrome]
    xor di, di
    mov word [px_slot], px_capslot
    call px_tband
    mov word [px_slot], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE COLOUR FACE'S BUTTONS (SPEC.md 106.16, 13.8.10)
; =============================================================================
; px_bpaint is every button's label on the colour face (OS88UI_OWN): AX =
; the index, BX = its rect (screen), DI = its flags with the pressed look
; resolved. The toolbar's and the tool column's are PIXEL.GFX's pictures,
; ONE OSAPI_GFX_BLITP each - the toolbar's an icon over its caption, inside
; the button; every other button, and those when the file is not here or
; the card refuses the block (a straddle, an origin off the grid), is a
; bevelled grey button drawn here. Every register preserved.
px_bpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [px_pax], ax
    mov [px_pbx], bx
    mov [px_pdi], di
    cmp ax, PX_B_T0 + PX_NTOOL
    jae .bevel
    cmp word [pxa_seg], 0
    je .bevel
    cmp ax, PX_B_T0
    jae .tool
    mov si, ax                      ; --- a toolbar face -------------------
    cmp ax, PX_B_OPEN
    jne .t1
    cmp byte [px_busy], 1           ; Open is Stop while a picture decodes
    jne .t1
    mov si, PXA_STOP
.t1:
    mov cl, [pxa_w + si]
    xor ch, ch                      ; CX = its width
    shl si, 1
    mov si, [pxa_off + si]          ; SI = its first state
    mov ax, cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov bp, ax                      ; BP = a row's bytes
    mov dx, PXA_TBH
    mul dx                          ; AX = a plane's bytes
    mov di, ax
    shl ax, 1
    shl ax, 1                       ; AX = a face's bytes
    test word [px_pdi], OS88UI_DIS
    jz .nd
    add si, ax
.nd0:
    add si, ax
    jmp short .tst
.nd:
    test word [px_pdi], OS88UI_DOWN
    jnz .nd0
.tst:
    mov bx, [px_pbx]
    mov ax, [bx]
    mov bx, [bx + 2]
    mov dx, PXA_TBH
    jmp short .blit
.tool:
    sub ax, PX_B_T0                 ; --- a tool face ----------------------
    shl ax, 1
    test word [px_pdi], OS88UI_DOWN
    jz .tu
    inc ax
.tu:
    mov cx, PXA_TLFACE
    mul cx
    add ax, PXA_TLOFF
    mov si, ax
    mov bx, [px_pbx]
    mov ax, [bx]
    sub ax, PXA_TLX0
    mov bx, [bx + 2]
    sub bx, PXA_TLY0
    mov cx, PXA_TLW
    mov dx, PXA_TLH
    mov di, PXA_TLH * PXA_TLW / 8
    mov bp, PXA_TLW / 8
.blit:
    or di, 0x4000                   ; the plane step, walking a clip
    mov es, [pxa_seg]
    call OSAPI_GFX_BLITP
    jnc .out
.bevel:
    call px_bevel
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bevel - the code-drawn button: px_bpaint's [px_pax] / [px_pbx] /
; [px_pdi]. A black frame, a light top-left and a grey bottom-right - or,
; pressed, one grey line in at the top and the left and the contents a pixel
; down and right - and inside on light grey its picture, its caption, or the
; toolbar's both, one over the other, ringed by the grey: every pixel once.
; A toolbar or tool button also draws the rest of its face's cell - the gap,
; the margins, the tool column's rule - which is what the picture would have
; covered. Preserves all
px_bevel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [px_pbx]
    mov ax, [bx]
    mov [px_bv], ax
    mov ax, [bx + 2]
    mov [px_bv + 2], ax
    mov ax, [bx + 4]
    mov [px_bv + 4], ax
    mov ax, [bx + 6]
    mov [px_bv + 6], ax
    call px_bvcell                  ; a face's cell round the button
    ; --- what goes inside: a picture (SI), a caption (DI), or both ----------
    mov bx, [px_pax]
    shl bx, 1
    mov si, [px_lab_full + bx]      ; the plain face's label
    xor di, di
    test word [px_bkind + bx], OS88UI_IMG
    jnz .pic
    mov di, si                      ; a text label: the caption alone
    xor si, si
    jmp short .cap
.pic:
    cmp word [px_pax], PX_B_T0      ; the toolbar's: a caption under it
    jae .cap
    mov ax, [px_pax]
    call px_tbcaption                  ; DI = its caption
.cap:
    or di, di                       ; a caption wider than the button is
    jz .c1                          ; left out: the picture says it
    push si
    mov si, di
    call OSAPI_FONT_WIDTH           ; AX = its width
    pop si
    mov cx, [px_bv + 4]
    sub cx, [px_bv]
    sub cx, 4
    cmp ax, cx
    jbe .c1
    or si, si
    jz .c1                          ; (a caption alone is kept, and cut)
    xor di, di
.c1:
    mov [px_bvpic], si
    mov [px_bvcap], di
    mov al, CBLACK                  ; the frame
    call OSAPI_SET_COLOR
    mov ax, [px_bv]
    mov bx, [px_bv + 2]
    mov cx, [px_bv + 4]
    mov dx, [px_bv + 6]
    call OSAPI_GFX_FRAME
    inc ax                          ; the face inside it
    inc bx
    dec cx
    dec dx
    mov [px_bf], ax
    mov [px_bf + 2], bx
    mov [px_bf + 4], cx
    mov [px_bf + 6], dx
    xor cx, cx                      ; CX = the contents' shift
    test word [px_pdi], OS88UI_DOWN
    jz .up
    mov al, CDGRAY                  ; PRESSED: one grey line top and left
    mov ah, CDGRAY
    call px_bvtl
    inc cx
    jmp short .face
.up:
    mov al, CWHITE                  ; UP: light top-left...
    mov ah, CWHITE
    call px_bvtl
    mov al, CDGRAY                  ; ...grey bottom-right
    call px_bvbr
.face:
    call px_bvin                    ; the inside, CX = the shift
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_tbcaption - AX = a toolbar button: DI = its caption (Open's is Stop's while
; a picture decodes). Preserves the rest
px_tbcaption:
    push bx
    push cx
    mov di, px_s_one                ; (1:1 has no picture: its label is it)
    xor bx, bx
.f:
    cmp bx, PX_TBN * PX_TBDSZ
    jae .out
    cmp [px_tbdef + bx + PX_TBD_BTN], al
    je .got
    add bx, PX_TBDSZ
    jmp short .f
.got:
    mov di, [px_tbdef + bx + PX_TBD_CAP]
.out:
    pop cx
    pop bx
    ret

; px_bvcell - a toolbar or tool button's face cell round its rect [px_bv]:
; the toolbar's gap at its right; the tool column's margins and its rule.
; Preserves all
px_bvcell:
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_pax]
    cmp ax, PX_B_T0 + PX_NTOOL
    jae .out
    cmp ax, PX_B_T0
    jae .tool
    mov ax, [px_bv + 4]             ; the toolbar: PXA_GAP columns at the
    inc ax                          ; right
    mov bx, [px_bv + 2]
    mov cx, ax
    add cx, PXA_GAP - 1
    mov dx, [px_bv + 6]
    PXFILL [px_c_chrome]
    jmp short .out
.tool:
    mov ax, [px_bv]                 ; the tool column: the face's rows above
    sub ax, PXA_TLX0                ; and below the button, its columns left
    mov bx, [px_bv + 2]             ; and right, and the rule
    sub bx, PXA_TLY0
    mov cx, ax
    add cx, PXA_TLW - 2
    mov dx, [px_bv + 2]
    dec dx
    PXFILL [px_c_chrome]
    mov bx, [px_bv + 6]
    inc bx
    mov dx, [px_bv + 2]
    sub dx, PXA_TLY0
    add dx, PXA_TLH - 1
    PXFILL [px_c_chrome]
    mov bx, [px_bv + 2]
    mov dx, [px_bv + 6]
    push cx
    mov cx, [px_bv]
    dec cx
    PXFILL [px_c_chrome]
    pop cx
    mov ax, [px_bv + 4]
    inc ax
    PXFILL [px_c_chrome]
    mov ax, cx
    inc ax
    mov bx, [px_bv + 2]
    sub bx, PXA_TLY0
    mov dx, bx
    add dx, PXA_TLH - 1
    call px_vrule
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bvtl - the face's top row in AL and its left column in AH, taken out of
; [px_bf]
px_bvtl:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov [px_pc], al
    mov ax, [px_bf]
    mov bx, [px_bf + 2]
    mov cx, [px_bf + 4]
    mov dx, bx
    call px_fillc
    pop ax
    mov [px_pc], ah
    mov ax, [px_bf]
    mov bx, [px_bf + 2]
    inc bx
    mov cx, ax
    mov dx, [px_bf + 6]
    call px_fillc
    inc word [px_bf]
    inc word [px_bf + 2]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bvbr - the face's bottom row and right column in AL, taken out of it
px_bvbr:
    push ax
    push bx
    push cx
    push dx
    mov [px_pc], al
    mov ax, [px_bf]
    mov bx, [px_bf + 6]
    mov cx, [px_bf + 4]
    mov dx, bx
    call px_fillc
    mov ax, cx
    mov bx, [px_bf + 2]
    mov dx, [px_bf + 6]
    dec dx
    call px_fillc
    dec word [px_bf + 4]
    dec word [px_bf + 6]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bvin - the face's inside, [px_bf], on light grey: the picture
; [px_bvpic] (16 x 16) over the caption [px_bvcap] (8 rows) with a row
; between, or either alone, the block centred and moved CX down and right;
; the grey round each, in bands, so every pixel is written once. Preserves
; all
px_bvin:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, cx                      ; BP = the shift
    xor dx, dx                      ; DX = the block's rows
    cmp word [px_bvpic], 0
    je .h1
    add dx, 16
.h1:
    cmp word [px_bvcap], 0
    je .h2
    or dx, dx
    jz .h3
    inc dx
.h3:
    add dx, 8
.h2:
    mov ax, [px_bf + 6]
    sub ax, [px_bf + 2]
    inc ax
    sub ax, dx
    sar ax, 1
    add ax, [px_bf + 2]
    add ax, bp
    mov [px_bvy], ax                ; the block's top
    mov byte [px_pc], CLGRAY
    mov ax, [px_bf]                 ; the grey above it
    mov bx, [px_bf + 2]
    mov cx, [px_bf + 4]
    mov dx, [px_bvy]
    dec dx
    call px_fillc
    mov si, [px_bvpic]
    or si, si
    jz .cap
    mov dx, 16                      ; --- the picture's band -------------
    mov ax, 16
    call px_bvband                  ; AX = its left, the grey either side
    mov cx, ax
    mov dx, [px_bvy]
    mov al, CLGRAY
    mov ah, CBLACK
    test word [px_pdi], OS88UI_DIS
    jz .ink
    mov ah, CDGRAY
.ink:
    call px_icon
    add word [px_bvy], 16
    cmp word [px_bvcap], 0
    je .below
    mov ax, [px_bf]                 ; the row between
    mov bx, [px_bvy]
    mov cx, [px_bf + 4]
    mov dx, bx
    call px_fillc
    inc word [px_bvy]
.cap:
    mov si, [px_bvcap]
    or si, si
    jz .below
    push si                         ; --- the caption's band --------------
    call OSAPI_FONT_WIDTH           ; AX = its width
    pop si
    cmp ax, 0
    je .below
    mov dx, 8
    call px_bvband
    mov cx, ax
    mov dx, [px_bvy]
    mov ax, CBLACK | (CLGRAY << 8)
    test word [px_pdi], OS88UI_DIS
    jz .cink
    mov al, CDGRAY
.cink:
    call OSAPI_FONT_RUN
    add word [px_bvy], 8
.below:
    mov ax, [px_bf]                 ; the grey below it
    mov bx, [px_bvy]
    mov cx, [px_bf + 4]
    mov dx, [px_bf + 6]
    call px_fillc
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_bvband - a band of DX rows at [px_bvy] holding something AX wide,
; centred across [px_bf] and moved BP right: the grey either side of it
; filled. out AX = its left. Preserves the rest
px_bvband:
    push bx
    push cx
    push dx
    push si
    mov si, ax
    mov ax, [px_bf + 4]
    sub ax, [px_bf]
    inc ax
    sub ax, si
    sar ax, 1
    add ax, [px_bf]
    add ax, bp
    push ax                         ; its left
    mov bx, [px_bvy]
    add dx, bx
    dec dx
    mov cx, ax
    dec cx
    mov ax, [px_bf]
    call px_fillc                   ; the grey left of it...
    pop ax
    push ax
    add ax, si
    mov cx, [px_bf + 4]
    call px_fillc                   ; ...and right of it
    pop ax
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; PIXEL.GFX - the colour face's pictures (SPEC.md 106.16)
; =============================================================================
; tools/pixart.py draws them into a file that ships BESIDE the package, read
; at entry only on a colour primary - a Hercules or a CGA machine never
; spends a byte or a revolution on it - into a claim of its own, and freed
; with the window. It is a sidecar and not a part for MIDIRack's reason
; (SPEC.md 105.9.5), and one more: PiXEL's master is sized from the heap
; (SPEC.md 106.8), so a machine that cannot spare the pictures' 27 KB beside
; a picture is left them: under PXA_MINFREE free after the claim, the claim
; goes back and every button is the code-drawn bevel - a complete face.

; px_artload - the pictures, if the primary is colour, the file is here, the
; heap can spare them and they are the length the numbers say. Preserves all
px_artload:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call OSAPI_VIDEO                ; DH = the primary's bits per pixel
    cmp dh, 1
    jbe .out
    mov ax, (PXA_SIZE + 1023) / 1024
    call OSAPI_MEM_CLAIM            ; DX = the segment
    jc .out
    push dx
    call OSAPI_MEM_AVAIL            ; BX = the KB still free beside it
    pop si
    cmp bx, PXA_MINFREE
    jb .free
    mov es, si
    xor bx, bx
    mov cx, ((PXA_SIZE + 1023) / 1024) * 1024
    xor dx, dx
    push si
    mov si, pxa_name
    call OSAPI_FILE_READ            ; DX:AX = its size (unwrapped, 20.14)
    pop si
    jc .free
    or dx, dx
    jnz .free
    cmp ax, PXA_SIZE
    jne .free
    mov [pxa_seg], si
    jmp short .out
.free:
    mov dx, si
    call OSAPI_MEM_FREE
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the window's ONE button record (SPEC.md 106.3): every control in it, in
; the PX_B_* order. One, because os88ui_btnclick takes the first record whose
; window matches - a second record for the same window would never be found
    OS88UI_BTNREC px_btns, px_brects, px_lab_full, px_bflags, PX_NB
