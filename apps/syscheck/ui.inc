; =============================================================================
; os8088 - apps/syscheck/ui.inc - layout and drawing (SPEC.md 118.4, 118.5)
;
; sc_layout derives every rect from the window as it is (OSAPI_WM_GEOM) at
; each paint; sc_drawall draws it all once; sc_drawowed draws what the scan
; changed since, and nothing else - the bar's new strip, the path and counter
; lines (at most every SC_UITICKS), a check row whose result moved, findings
; lines that arrived. Every run of text is ONE opaque OSAPI_FONT_RUN of a
; fixed width, so a redraw in place needs no erase (SPEC.md 6.1), and every
; text x is a multiple of 8 from a snapped content origin.
; =============================================================================

; --- what a draw owes (sc_dirty) ----------------------------------------------
SYD_BAR       equ 0x0001              ; the bar's fill
SYD_PATH      equ 0x0002              ; the path line
SYD_CNT       equ 0x0004              ; the counter line
SYD_HEAD      equ 0x0008              ; the progress card's heading
SYD_SYS       equ 0x0010              ; the This System card
SYD_BTN       equ 0x0020              ; the buttons (a label or a grey moved)
SYD_STAT      equ 0x0040              ; the status text beside them
SYD_FIND      equ 0x0080              ; the whole findings list
SYD_FADD      equ 0x0100              ; findings lines from [sc_dfind] on
SC_UITICKS  equ 4                   ; the path/counter/bar lines' rate

; --- the layout (sc_geo + SCG_*) -------------------------------------------------
SCG_CX      equ 0                   ; the content origin and size
SCG_CY      equ 2
SCG_CW      equ 4
SCG_CH      equ 6
SCG_LP      equ 8                   ; the row pitch
SCG_PX1     equ 10                  ; the progress card
SCG_PY1     equ 12
SCG_PX2     equ 14
SCG_PY2     equ 16
SCG_SX1     equ 18                  ; the This System card (0 = none)
SCG_TX      equ 20                  ; the progress card's text x
SCG_PCOLS   equ 22                  ; ...and its width in cells
SCG_YHEAD   equ 24                  ; its lines' y (0 = not drawn)
SCG_YTICK   equ 26
SCG_YPATH   equ 28
SCG_YCNT    equ 30
SCG_BX1     equ 32                  ; the bar's frame
SCG_BY1     equ 34
SCG_BX2     equ 36
SCG_BY2     equ 38
SCG_BW      equ 40                  ; ...its inner width
SCG_STX     equ 42                  ; the system card's text x, first y
SCG_STY     equ 44
SCG_SCOLS   equ 46
SCG_KX1     equ 48                  ; the Checks card
SCG_KY1     equ 50
SCG_KX2     equ 52
SCG_KY2     equ 54
SCG_KHY     equ 56                  ; its heading's y (0 = none)
SCG_KY0     equ 58                  ; the first row's y
SCG_KTX     equ 60                  ; the first column's x
SCG_KRPC    equ 62                  ; rows a column
SCG_KCW     equ 64                  ; a column's width in pixels
SCG_KCOLS   equ 66                  ; ...and in cells, the lamp's two included
SCG_FX1     equ 68                  ; the Findings card
SCG_FY1     equ 70
SCG_FX2     equ 72
SCG_FY2     equ 74
SCG_FHY     equ 76                  ; its heading's y (0 = none)
SCG_FY0     equ 78                  ; the first line's y
SCG_FTX     equ 80
SCG_FCOLS   equ 82                  ; a line's cells, the lamp's two included
SCG_BTY     equ 84                  ; the button row's top
SCG_STATX   equ 86                  ; the status text
SCG_STATC   equ 88
SCG_VY0     equ 90                  ; the chooser's first row (118.13)
SCG_VFIT    equ 92                  ; ...and the rows it has room for
SCG_SIZE    equ 94

SC_BTH      equ 15                  ; a button's height (Timer's, SPEC.md 111.1)

; -----------------------------------------------------------------------------
; sc_layout - every rect, from the window as it is now
; in:  BX = window ptr; out: nothing; clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
sc_layout:
    push bx
    push bp
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [sc_geo+SCG_CX], ax
    mov [sc_geo+SCG_CY], dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    mov [sc_geo+SCG_CW], cx
    mov [sc_geo+SCG_CH], dx
    push bx
    call OSAPI_WM_DISPLAY           ; DH = bits per pixel (keeps DI)
    mov [sc_bpp], dh
    pop bx
    mov ax, [sc_geo+SCG_CH]
    mov cx, 12                      ; the pitch: 12 on VGA's height, 10 on
    mov dl, 0                       ; Hercules', 9 and the short layout on
    cmp ax, 380                     ; CGA's (SPEC.md 118.4)
    jae .lp
    mov cx, 10
    cmp ax, 250
    jae .lp
    mov cx, 9
    inc dl
.lp:
    mov [sc_geo+SCG_LP], cx
    mov [sc_compact], dl
    mov bp, cx                      ; BP = the pitch, below
    ; --- the progress card ----------------------------------------------------
    mov ax, [sc_geo+SCG_CX]
    mov dx, [sc_geo+SCG_CY]
    mov si, ax
    add si, 4
    mov [sc_geo+SCG_PX1], si
    add ax, 16
    mov [sc_geo+SCG_TX], ax
    add dx, 4
    mov [sc_geo+SCG_PY1], dx
    mov ax, [sc_geo+SCG_CW]         ; the split: three fifths of the width,
    cmp byte [sc_compact], 0        ; on a cell
    jne .full
    mov cx, 3
    mul cx
    mov cx, 5
    div cx
    and ax, 0FFF8h
    jmp short .split
.full:
    xor ax, ax                      ; the short layout: one card, no system
.split:
    or ax, ax
    jz .nosys
    mov dx, [sc_geo+SCG_CX]
    add dx, ax
    mov [sc_geo+SCG_SX1], dx        ; the system card starts here
    sub dx, 4
    mov [sc_geo+SCG_PX2], dx
    add dx, 4 + 12
    mov [sc_geo+SCG_STX], dx
    mov cx, [sc_geo+SCG_CX]
    add cx, [sc_geo+SCG_CW]
    sub cx, dx
    sub cx, 8
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [sc_geo+SCG_SCOLS], cx
    jmp short .pcols
.nosys:
    mov word [sc_geo+SCG_SX1], 0
    mov dx, [sc_geo+SCG_CX]
    add dx, [sc_geo+SCG_CW]
    sub dx, 5
    mov [sc_geo+SCG_PX2], dx
.pcols:
    mov ax, [sc_geo+SCG_PX2]        ; the progress text's cells
    sub ax, [sc_geo+SCG_TX]
    sub ax, 8
    mov cl, 3
    shr ax, cl
    mov [sc_geo+SCG_PCOLS], ax
    ; --- its lines -------------------------------------------------------------
    mov dx, [sc_geo+SCG_PY1]
    add dx, 5
    cmp byte [sc_compact], 0
    jne .cline
    mov [sc_geo+SCG_YHEAD], dx      ; heading, bar, ticks, -, path, counts
    mov [sc_geo+SCG_STY], dx        ; (the system card's first line too)
    add dx, bp
    call .bar
    add dx, bp
    mov [sc_geo+SCG_YTICK], dx
    add dx, bp
    add dx, bp
    mov [sc_geo+SCG_YPATH], dx
    add dx, bp
    mov [sc_geo+SCG_YCNT], dx
    add dx, bp
    add dx, bp
    sub dx, 4
    jmp short .pend
.cline:                             ; the short layout: bar, then one line
    mov word [sc_geo+SCG_YHEAD], 0
    mov word [sc_geo+SCG_YTICK], 0
    mov word [sc_geo+SCG_YCNT], 0
    call .bar
    add dx, bp
    mov [sc_geo+SCG_YPATH], dx
    add dx, bp
.pend:
    mov [sc_geo+SCG_PY2], dx
    ; --- the button row, from the bottom -------------------------------------
    mov ax, [sc_geo+SCG_CY]
    add ax, [sc_geo+SCG_CH]
    sub ax, SC_BTH + 4
    mov [sc_geo+SCG_BTY], ax
    mov ax, [sc_geo+SCG_CX]
    add ax, 8 + SC_BTROW + 16
    mov [sc_geo+SCG_STATX], ax
    mov cx, [sc_geo+SCG_CX]
    add cx, [sc_geo+SCG_CW]
    sub cx, ax
    sub cx, 8
    jns .stc
    xor cx, cx
.stc:
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [sc_geo+SCG_STATC], cx
    ; --- the Checks card ---------------------------------------------------------
    mov ax, [sc_geo+SCG_CX]
    add ax, 4
    mov [sc_geo+SCG_KX1], ax
    mov [sc_geo+SCG_FX1], ax
    add ax, 12
    mov [sc_geo+SCG_KTX], ax
    mov [sc_geo+SCG_FTX], ax
    mov ax, [sc_geo+SCG_CX]
    add ax, [sc_geo+SCG_CW]
    sub ax, 5
    mov [sc_geo+SCG_KX2], ax
    mov [sc_geo+SCG_FX2], ax
    mov dx, [sc_geo+SCG_PY2]
    add dx, 4
    cmp byte [sc_details], 0
    jne .nochk
    mov [sc_geo+SCG_KY1], dx
    add dx, 5
    mov cx, SC_NCHK                 ; one column of nine...
    mov ax, [sc_geo+SCG_KX2]
    sub ax, [sc_geo+SCG_KTX]
    sub ax, 4
    cmp byte [sc_compact], 0
    je .onecol
    mov word [sc_geo+SCG_KHY], 0    ; ...or, short, two of five and no heading
    mov cx, (SC_NCHK + 1) / 2
    shr ax, 1
    and ax, 0FFF8h
    jmp short .cols
.onecol:
    mov [sc_geo+SCG_KHY], dx
    add dx, bp
.cols:
    mov [sc_geo+SCG_KRPC], cx
    mov [sc_geo+SCG_KCW], ax
    shr ax, 1
    shr ax, 1
    shr ax, 1
    dec ax                          ; a cell of air at the column's end
    mov [sc_geo+SCG_KCOLS], ax
    mov [sc_geo+SCG_KY0], dx
    push dx
    mov ax, bp
    mul cx                          ; (DX:AX - the y banked round it)
    pop dx
    add dx, ax
    add dx, 1
    mov [sc_geo+SCG_KY2], dx
    add dx, 4
    jmp short .find
.nochk:
    mov word [sc_geo+SCG_KY1], 0    ; Details: no Checks card
.find:
    ; --- the Findings card: what is left -----------------------------------------
    mov [sc_geo+SCG_FY1], dx
    add dx, 5
    cmp byte [sc_compact], 0
    jne .nofh
    mov [sc_geo+SCG_FHY], dx
    add dx, bp
    jmp short .fy0
.nofh:
    mov word [sc_geo+SCG_FHY], 0
.fy0:
    mov [sc_geo+SCG_FY0], dx
    mov ax, [sc_geo+SCG_BTY]
    sub ax, 4
    mov [sc_geo+SCG_FY2], ax
    sub ax, dx                      ; the lines that fit
    sub ax, 2
    jns .fit
    xor ax, ax
.fit:
    xor dx, dx
    div bp
    mov [sc_sb+10], ax              ; fit
    ; ...its scroll bar, the standard one (SPEC.md 13.10), 14 wide
    mov ax, [sc_geo+SCG_FX2]
    sub ax, 2
    mov [sc_sb+4], ax               ; x2
    sub ax, 13
    mov [sc_sb+0], ax               ; x1
    sub ax, [sc_geo+SCG_FTX]
    sub ax, 8
    mov cl, 3
    shr ax, cl
    mov [sc_geo+SCG_FCOLS], ax
    mov ax, [sc_geo+SCG_FY1]
    add ax, 2
    mov [sc_sb+2], ax               ; y1
    mov ax, [sc_geo+SCG_FY2]
    sub ax, 2
    mov [sc_sb+6], ax               ; y2
    ; ...and the chooser, which takes the Checks and Findings room
    mov dx, [sc_geo+SCG_PY2]
    add dx, 4 + 5
    add dx, bp
    add dx, 3
    mov [sc_geo+SCG_VY0], dx
    mov ax, [sc_geo+SCG_BTY]
    sub ax, 6
    sub ax, dx
    jns .vf
    xor ax, ax
.vf:
    xor dx, dx
    div bp
    mov [sc_geo+SCG_VFIT], ax
    mov byte [sc_laid], 1
    pop bp
    pop bx
    ret
; .bar - the bar's frame on the line at DX (a local helper: keeps DX)
.bar:
    mov ax, [sc_geo+SCG_TX]
    mov [sc_geo+SCG_BX1], ax
    mov cx, [sc_geo+SCG_PCOLS]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    mov [sc_geo+SCG_BX2], cx
    sub cx, ax
    dec cx
    mov [sc_geo+SCG_BW], cx
    mov ax, dx
    dec ax
    mov [sc_geo+SCG_BY1], ax
    add ax, bp
    sub ax, 2
    mov [sc_geo+SCG_BY2], ax
    ret

; -----------------------------------------------------------------------------
; sc_drawall - everything, once (W_PAINT: the content is white, the clip armed)
; in:  BX = window ptr; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
sc_drawall:
    call sc_pen
    mov ax, [sc_geo+SCG_PX1]        ; the cards' frames
    mov bx, [sc_geo+SCG_PY1]
    mov cx, [sc_geo+SCG_PX2]
    mov dx, [sc_geo+SCG_PY2]
    call OSAPI_GFX_FRAME
    mov ax, [sc_geo+SCG_SX1]
    or ax, ax
    jz .nosys
    mov cx, [sc_geo+SCG_KX2]
    call OSAPI_GFX_FRAME
.nosys:
    cmp byte [sc_choose], 0
    jne .nofind                     ; the chooser draws its own card
    mov ax, [sc_geo+SCG_KX1]
    mov bx, [sc_geo+SCG_KY1]
    or bx, bx
    jz .nochk
    mov cx, [sc_geo+SCG_KX2]
    mov dx, [sc_geo+SCG_KY2]
    call OSAPI_GFX_FRAME
.nochk:
    mov ax, [sc_geo+SCG_FX1]
    mov bx, [sc_geo+SCG_FY1]
    mov cx, [sc_geo+SCG_FX2]
    mov dx, [sc_geo+SCG_FY2]
    cmp dx, bx
    jbe .nofind
    call OSAPI_GFX_FRAME
.nofind:
    mov ax, [sc_geo+SCG_BX1]        ; the bar's frame, then its whole fill
    mov bx, [sc_geo+SCG_BY1]
    mov cx, [sc_geo+SCG_BX2]
    mov dx, [sc_geo+SCG_BY2]
    call OSAPI_GFX_FRAME
    mov word [sc_barpx], 0
    call sc_drawbar
    call sc_drawticks
    call sc_drawheads
    call sc_drawsys
    call sc_drawpath
    call sc_drawcnt
    cmp byte [sc_choose], 0
    je .checks
    call sc_drawchoose
    jmp short .btns
.checks:
    mov ax, SC_NCHK - 1
.row:
    call sc_drawrow
    dec ax
    jns .row
    call sc_drawfind
.btns:
    call sc_buttons
    call sc_drawstat
    xor ax, ax
    mov [sc_dirty], ax
    mov [sc_drows], ax
    mov word [sc_dfind], 0FFFFh
    call OSAPI_GET_TICKS
    mov [sc_tui], ax
    ret

; -----------------------------------------------------------------------------
; sc_drawowed - what the scan changed since the last draw (a wake's burst)
; in:  BX = window ptr; gfx lock held; out: clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
sc_drawowed:
    cmp byte [sc_laid], 0
    je .out                         ; no paint yet: it will draw everything
    cmp byte [sc_btrec+OS88UI_BT_N], 0
    je .out                         ; the About card is over it all
    call OSAPI_WM_CLIP_SET
    jc .out                         ; not visible: the next paint draws it
    call sc_pen
    cmp byte [sc_choose], 0
    je .lists
    and word [sc_dirty], ~(SYD_FIND | SYD_FADD) ; the chooser covers both
    mov word [sc_drows], 0
    jmp .head
.lists:
    test word [sc_dirty], SYD_FIND
    jz .rows
    call sc_drawfind                ; the whole list (the selection moved)
    and word [sc_dirty], ~(SYD_FIND | SYD_FADD)
    mov word [sc_dfind], 0FFFFh
.rows:
    mov ax, SC_NCHK - 1
.row:
    mov cx, ax
    mov dx, 1
    shl dx, cl
    test [sc_drows], dx
    jz .nr
    call sc_drawrow
.nr:
    dec ax
    jns .row
    mov word [sc_drows], 0
    test word [sc_dirty], SYD_FADD
    jz .head
    call sc_drawfadd
    and word [sc_dirty], ~SYD_FADD
    mov word [sc_dfind], 0FFFFh
.head:
    test word [sc_dirty], SYD_HEAD
    jz .sys
    call sc_drawheads
.sys:
    test word [sc_dirty], SYD_SYS
    jz .btn
    call sc_drawsys
.btn:
    test word [sc_dirty], SYD_BTN
    jz .stat
    call sc_buttons
.stat:
    test word [sc_dirty], SYD_STAT
    jz .slow
    call sc_drawstat
.slow:
    and word [sc_dirty], ~(SYD_HEAD | SYD_SYS | SYD_BTN | SYD_STAT)
    ; --- the bar, path and counter lines: at most every SC_UITICKS, and at
    ; the end of a scan whatever the clock says
    call OSAPI_GET_TICKS
    cmp byte [sc_run], 0
    je .now
    mov dx, ax
    sub dx, [sc_tui]
    cmp dx, SC_UITICKS
    jb .out
.now:
    mov [sc_tui], ax
    test word [sc_dirty], SYD_BAR
    jz .path
    call sc_drawbar
.path:
    test word [sc_dirty], SYD_PATH
    jz .cnt
    call sc_drawpath
.cnt:
    test word [sc_dirty], SYD_CNT
    jz .clr
    call sc_drawcnt
.clr:
    and word [sc_dirty], ~(SYD_BAR | SYD_PATH | SYD_CNT)
.out:
    ret

; sc_pen - the black pen, and its disabled flag cleared (a greyed button can
;          leave it set, and font_run would then grey every glyph)
sc_pen:
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    ret


; -----------------------------------------------------------------------------
; sc_run_at - ONE opaque run of exactly BX cells: SI, padded with spaces
; in:  CX = x, DX = y, SI = the text (not sc_line), BX = cells, AL = ink,
;      AH = ground; out: nothing; every register kept
; -----------------------------------------------------------------------------
sc_run_at:
    push bx
    push cx
    push si
    push di
    or bx, bx
    jz .out
    cmp bx, 98
    jbe .n
    mov bx, 98
.n:
    push cx
    push ax
    mov di, sc_line
    mov cx, bx
.c:
    mov al, [si]
    or al, al
    jz .pad
    inc si
    mov [di], al
    inc di
    loop .c
    jmp short .end
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.end:
    mov byte [di], 0
    pop ax
    pop cx
    mov si, sc_line
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret

SC_INK      equ (CWHITE << 8) | CBLACK  ; black on white
SC_INV      equ (CBLACK << 8) | CWHITE  ; white on black: the selected row

; -----------------------------------------------------------------------------
; sc_drawbar - the bar's fill to [sc_prog], drawing only the strip that moved
;              (Audio Player's delta, SPEC.md 86): growth in the fill, a new
;              scan's shrink in white
; -----------------------------------------------------------------------------
sc_drawbar:
    push si
    mov ax, [sc_prog]
    mul word [sc_geo+SCG_BW]        ; DX:AX = prog * width
    mov cx, 1000
    div cx                          ; AX = the pixels it should cover
    mov cx, [sc_barpx]
    cmp ax, cx
    je .out
    mov [sc_barpx], ax
    mov bx, [sc_geo+SCG_BX1]
    inc bx                          ; BX = the bar's inner left
    mov si, ax                      ; SI = the new extent
    ja .grow
    ; --- shrink: white from the new extent to the old ---------------------
    xchg ax, cx                     ; AX = the old extent, CX = the new
    add ax, bx
    dec ax                          ; x2
    add cx, bx                      ; x1
    xchg ax, cx                     ; AX = x1, CX = x2
    mov bx, [sc_geo+SCG_BY1]
    inc bx
    mov dx, [sc_geo+SCG_BY2]
    dec dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    jmp short .pen
.grow:
    add cx, bx                      ; x1 = the old extent's end
    add ax, bx
    dec ax                          ; x2 = the new one's
    xchg ax, cx                     ; AX = x1, CX = x2
    mov bx, [sc_geo+SCG_BY1]
    inc bx
    mov dx, [sc_geo+SCG_BY2]
    dec dx
    cmp byte [sc_bpp], 1
    ja .colour
    mov si, sc_pat50                ; 1bpp: the 50% pattern, screen-anchored,
    call OSAPI_GFX_FILL_PAT         ; so this strip meets the last one
    jmp short .pen
.colour:
    push ax
    mov al, CBLUE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.pen:
    call sc_pen
.out:
    pop si
    ret

; sc_drawticks - "0%  25%  50%  75%  100%" under the bar (the tall layout)
sc_drawticks:
    mov dx, [sc_geo+SCG_YTICK]
    or dx, dx
    jz .out
    mov di, 0                       ; the label index, 0..4
.one:
    mov ax, [sc_geo+SCG_PCOLS]      ; its cell: cols * k / 4, less its length
    push dx                         ; less one at the far end
    mul di
    pop dx
    shr ax, 1
    shr ax, 1
    mov bx, di
    shl bx, 1
    mov si, [sc_ticks+bx]
    or di, di
    jz .at
    dec ax
    cmp di, 4
    jne .at
    dec ax
    dec ax
.at:
    mov cl, 3
    shl ax, cl
    add ax, [sc_geo+SCG_TX]
    mov cx, ax
    push di
    mov bx, 4
    mov ax, SC_INK
    call sc_run_at
    pop di
    inc di
    cmp di, 5
    jb .one
.out:
    ret

; sc_drawheads - the progress card's, Checks' and Findings' headings
sc_drawheads:
    mov dx, [sc_geo+SCG_YHEAD]
    or dx, dx
    jz .chk
    mov di, sc_txt
    mov si, sc_h_check              ; Checking / Checked / Stopped, or - with
    cmp byte [sc_run], 0            ; nothing checked yet - the chooser's
    jne .h1                         ; invitation
    mov si, sc_h_ready
    cmp byte [sc_ended], 0
    je .h2
    mov si, sc_h_checked
    cmp byte [sc_stopped], 0
    je .h1
    mov si, sc_h_stopped
.h1:
    call sc_scpy
    cmp byte [sc_vlet], 0
    je .h4                          ; the first step has not named it yet
    call sc_drive2                  ; "C:"
    mov si, sc_h_dash
    call sc_scpy
    mov si, sc_h_hd
    cmp byte [sc_fixed], 0
    jne .h3
    mov si, sc_h_fl
.h3:
    call sc_scpy
    cmp byte [sc_isboot], 0
    je .h4
    mov si, sc_v_boot               ; ", the boot volume"
    call sc_scpy
    jmp short .h4
.h2:
    call sc_scpy
.h4:
    mov si, sc_txt
    mov cx, [sc_geo+SCG_TX]
    mov bx, [sc_geo+SCG_PCOLS]
    mov ax, SC_INK
    call sc_run_at
.chk:
    mov dx, [sc_geo+SCG_KHY]
    or dx, dx
    jz .out
    cmp word [sc_geo+SCG_KY1], 0
    je .out
    mov cx, [sc_geo+SCG_KTX]
    mov si, sc_h_checks
    mov bx, 6
    mov ax, SC_INK
    call sc_run_at
.out:
    ret

; sc_drawsys - the This System card (the tall layout): what this package can
;              know, which is not the running kernel's build (118.1.1)
sc_drawsys:
    mov ax, [sc_geo+SCG_SX1]
    or ax, ax
    jz .out
    mov dx, [sc_geo+SCG_STY]
    mov cx, [sc_geo+SCG_STX]
    mov bx, [sc_geo+SCG_SCOLS]
    mov si, sc_h_sys
    mov ax, SC_INK
    call sc_run_at
    xor di, di                      ; the line, 0..5
.line:
    add dx, [sc_geo+SCG_LP]
    push di
    push dx
    push di
    mov bx, di
    shl bx, 1
    mov si, [sc_syslbl+bx]
    mov di, sc_txt
    call sc_scpy
.pad:
    cmp di, sc_txt + 14
    jae .val
    mov byte [di], ' '
    inc di
    jmp short .pad
.val:
    pop bx                          ; the line
    shl bx, 1
    call [sc_sysval+bx]             ; the value, at DI
    pop dx
    mov si, sc_txt
    mov cx, [sc_geo+SCG_STX]
    mov bx, [sc_geo+SCG_SCOLS]
    mov ax, SC_INK
    call sc_run_at
    pop di
    inc di
    cmp di, 6
    jb .line
.out:
    ret

sc_v_build:
    mov ax, BUILD_NUM
    jmp dec_u16
sc_v_fmt:
    mov ax, PKG_FMT
    jmp dec_u16
sc_v_drv:
    mov ax, DRV_VER
    jmp dec_u16
sc_v_vol:
    cmp byte [sc_vlet], 0
    je sc_v_dash
    call sc_drive2
    mov si, sc_s_fat12
    cmp byte [sc_fat16], 0
    je .f
    mov si, sc_s_fat16
.f:
    jmp sc_scpy
sc_v_kern:
    mov bl, [sc_kstate]
    xor bh, bh
    shl bx, 1
    mov si, [sc_kst+bx]
    jmp sc_scpy
sc_v_free:
    mov ax, [sc_freelo]
    mov dx, [sc_freehi]
    mov cx, ax
    or cx, dx
    jz sc_v_dash
    jmp dec_u32
sc_v_dash:
    mov byte [di], '-'
    inc di
    mov byte [di], 0
    ret

; sc_cnttext - the counter line into sc_txt
sc_cnttext:
    mov di, sc_txt
    mov byte [di], 0
    cmp byte [sc_vlet], 0
    je .out                         ; nothing has run yet
    cmp byte [sc_run], 0
    je .ended
    mov si, sc_c_files
    call sc_scpy
    mov ax, [sc_nfiles]
    call dec_u16
    mov si, sc_c_folders
    call sc_scpy
    mov ax, [sc_nfold]
    call dec_u16
    mov si, sc_c_pkgs
    call sc_scpy
    mov ax, [sc_npkg]
    jmp dec_u16
.ended:
    mov si, sc_c_done
    cmp byte [sc_stopped], 0
    je .e1
    mov si, sc_c_stopd
.e1:
    call sc_scpy
    mov ax, [sc_nfiles]
    call dec_u16
    mov si, sc_c_filesin
    call sc_scpy
    mov ax, [sc_nfold]
    call dec_u16
    mov si, sc_c_foldersp
    call sc_scpy
    cmp byte [sc_stopped], 0
    jne .out
    mov si, sc_c_comma
    call sc_scpy
    mov ax, [sc_secs]
    cmp ax, 60
    jb .s
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    call dec_u16
    mov si, sc_c_min
    call sc_scpy
    mov ax, dx
.s:
    call dec_u16
    mov si, sc_c_sec
    call sc_scpy
.out:
    ret

; sc_drawpath - the path being read (the counter line instead, once a short
;               layout's scan has ended)
sc_drawpath:
    mov dx, [sc_geo+SCG_YPATH]
    or dx, dx
    jz .out
    mov si, sc_cpath
    cmp byte [sc_run], 0
    jne .draw
    mov si, sc_s_none
    cmp byte [sc_compact], 0
    je .draw
    call sc_cnttext
    mov si, sc_txt
.draw:
    mov cx, [sc_geo+SCG_TX]
    mov bx, [sc_geo+SCG_PCOLS]
    mov ax, SC_INK
    call sc_run_at
.out:
    ret

; sc_drawcnt - the counter line (the tall layout)
sc_drawcnt:
    mov dx, [sc_geo+SCG_YCNT]
    or dx, dx
    jz .out
    push dx
    call sc_cnttext
    pop dx
    mov si, sc_txt
    mov cx, [sc_geo+SCG_TX]
    mov bx, [sc_geo+SCG_PCOLS]
    mov ax, SC_INK
    call sc_run_at
.out:
    ret

; -----------------------------------------------------------------------------
; sc_drawrow - one Checks row: its lamp, its name, a leader, its result
; in:  AX = the row; out: keeps AX; clobbers BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
sc_drawrow:
    cmp word [sc_geo+SCG_KY1], 0
    jne .go
    ret                             ; Details: there is no Checks card
.go:
    push ax
    mov si, ax                      ; SI = the row
    xor dx, dx
    div word [sc_geo+SCG_KRPC]      ; AX = its column, DX = its line
    mov di, dx
    mul word [sc_geo+SCG_KCW]
    add ax, [sc_geo+SCG_KTX]
    mov cx, ax                      ; CX = x
    mov ax, di
    mul word [sc_geo+SCG_LP]
    add ax, [sc_geo+SCG_KY0]
    mov dx, ax                      ; DX = y
    mov ax, si
    call sc_rowlamp                 ; AL = the lamp
    mov bx, si
    mov ah, 0
    cmp bl, [sc_sel]
    jne .lamp
    inc ah                          ; AH = 1: the selected row
.lamp:
    push ax
    call sc_lamp
    push cx
    push dx
    mov ax, si
    call sc_rowtext                 ; sc_txt, and BX = its cells
    pop dx
    pop cx
    add cx, 16                      ; the text after the lamp's two cells
    pop ax
    or ah, ah
    mov ax, SC_INK
    jz .t
    mov ax, SC_INV
.t:
    mov si, sc_txt
    call sc_run_at
    pop ax
    ret

; sc_rowtext - sc_txt = row AX's name, a dotted leader and its result,
;              exactly as wide as the row's text; out: BX = those cells
;              (clobbers AX, CX, DX, SI, DI)
sc_rowtext:
    push ax
    call sc_rowres                  ; sc_rsn = the result
    pop ax
    mov bx, ax
    shl bx, 1
    mov si, [sc_rnames+bx]
    mov di, sc_txt
    call sc_scpy
    cmp ax, SC_DRV                  ; a count where there is one to give
    jne .p
    mov ax, SC_NDRV
    jmp short .count
.p:
    cmp ax, SC_PKG
    jne .lead
    test word [sc_rdone], 1 << SC_PKG
    jz .lead
    mov ax, [sc_npkg]
.count:
    mov byte [di], ' '
    mov byte [di+1], '('
    inc di
    inc di
    call dec_u16
    mov byte [di], ')'
    inc di
    mov byte [di], 0
.lead:
    mov si, sc_rsn                  ; CX = the result's length
    xor cx, cx
.rl:
    cmp byte [si], 0
    je .rd
    inc si
    inc cx
    jmp short .rl
.rd:
    mov bx, [sc_geo+SCG_KCOLS]
    sub bx, 2                       ; BX = the cells
    cmp bx, cx
    jbe .out                        ; too narrow for a result: the name alone
    mov ax, bx
    sub ax, cx                      ; AX = the result's first cell
    mov si, ax
    dec si                          ; SI = the cell before it
    mov dx, di
    sub dx, sc_txt                  ; DX = the name's length
    cmp dx, si
    jbe .fill
    mov dx, si                      ; a name too long is cut
    mov di, sc_txt
    add di, dx
.fill:
    mov byte [di], ' '              ; a space, dots, a space, the result
    inc di
    inc dx
.dots:
    cmp dx, si
    jae .sp
    mov byte [di], '.'
    inc di
    inc dx
    jmp short .dots
.sp:
    cmp dx, ax
    jae .res
    mov byte [di], ' '
    inc di
.res:
    mov si, sc_rsn
    call sc_scpy
.out:
    ret

; sc_rowres - sc_rsn = row AX's result: "-", "checking", "OK", "2 broken",
;             "3 warnings", "1 broken, 2 warn" (clobbers BX, CX, DX, SI, DI)
sc_rowres:
    mov di, sc_rsn
    mov cx, ax
    mov dx, 1
    shl dx, cl
    test [sc_rdone], dx
    jnz .done
    mov si, sc_s_dash
    test [sc_rrun], dx
    jz .cp
    mov si, sc_s_checking
.cp:
    jmp sc_scpy
.done:
    mov bx, ax
    shl bx, 1
    mov cx, [sc_rbrk+bx]
    mov dx, [sc_rwrn+bx]
    mov si, sc_s_ok
    mov ax, cx
    or ax, dx
    jz .cp
    jcxz .warn
    mov ax, cx
    call dec_u16
    mov si, sc_s_broken
    call sc_scpy
    or dx, dx
    jz .ret
    mov si, sc_s_comma
    call sc_scpy
    mov ax, dx
    call dec_u16
    mov si, sc_s_warn
    jmp sc_scpy
.warn:
    mov ax, dx
    call dec_u16
    mov si, sc_s_warning
    call sc_scpy
    cmp dx, 1
    je .ret
    mov byte [di], 's'
    inc di
    mov byte [di], 0
.ret:
    ret

; sc_rowlamp - AL = row AX's lamp (LP_*); every other register kept
sc_rowlamp:
    push bx
    push cx
    push dx
    mov cx, ax
    mov dx, 1
    shl dx, cl
    mov bx, ax
    shl bx, 1
    mov al, LP_PEND
    test [sc_rrun], dx
    jz .d
    mov al, LP_RUN
.d:
    test [sc_rdone], dx
    jz .out
    mov al, LP_BROKEN
    cmp word [sc_rbrk+bx], 0
    jne .out
    mov al, LP_WARN
    cmp word [sc_rwrn+bx], 0
    jne .out
    mov al, LP_OK
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_lamp - one lamp, ONE OSAPI_GFX_BLIT1 (SPEC.md 118.5): a coloured square
;           on 4bpp; on 1bpp, where every colour but white is black, a SHAPE
; in:  AL = LP_*, AH = 1 on a selected (black) row, CX = x, DX = y
; out: nothing; every register kept
; -----------------------------------------------------------------------------
sc_lamp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov bl, al
    xor bh, bh                      ; BX = the state
    mov si, bx
    shl si, 1
    shl si, 1
    shl si, 1                       ; SI = its eight bytes' offset
    cmp byte [sc_bpp], 1
    ja .colour
    ; --- 1bpp: a lit bit is WHITE, so on a white row the shape is inverted --
    add si, sc_lmono
    mov di, sc_lband
    push cx
    mov cx, 8
.m:
    lodsb
    or ah, ah
    jnz .ms
    not al
.ms:
    stosb
    loop .m
    pop cx
    mov si, sc_lband
    jmp short .blit
.colour:
    add si, sc_lcolsh
    mov al, [sc_lcol+bx]            ; AL = the ink...
    or ah, ah
    mov ah, CWHITE                  ; ...AH = the row's ground
    jz .pen
    mov ah, CBLACK
.pen:
    call OSAPI_GFX_BLIT1_PEN
.blit:
    mov ax, cx                      ; AX = x
    mov bx, dx                      ; BX = y
    mov cx, 8
    mov dx, 8
    mov bp, 1
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the Findings card
; -----------------------------------------------------------------------------
; sc_fclamp - the scroll bar's total from the selected check, its pos clamped
sc_fclamp:
    push ax
    push cx
    call sc_selcount
    mov [sc_sb+8], cx
    sub cx, [sc_sb+10]              ; the furthest pos: total - fit
    jns .m
    xor cx, cx
.m:
    cmp [sc_sb+12], cx
    jbe .out
    mov [sc_sb+12], cx
.out:
    pop cx
    pop ax
    ret

; sc_drawfind - the heading, every visible line and the bar
sc_drawfind:
    mov dx, [sc_geo+SCG_FHY]
    or dx, dx
    jz .lines
    mov di, sc_txt
    mov si, sc_h_find
    call sc_scpy
    mov bl, [sc_sel]
    xor bh, bh
    shl bx, 1
    mov si, [sc_rnames+bx]
    call sc_scpy
    mov si, sc_txt
    mov cx, [sc_geo+SCG_FTX]
    mov bx, [sc_geo+SCG_FCOLS]
    mov ax, SC_INK
    call sc_run_at
.lines:
    call sc_fclamp
    xor ax, ax
.l:
    cmp ax, [sc_sb+10]
    jae .bar
    call sc_drawfline
    inc ax
    jmp short .l
.bar:
    cmp word [sc_sb+10], 0
    je .out
    mov bx, sc_sb
    call os88ui_sbupdw              ; the whole bar, and what it drew
.out:
    ret

; sc_drawfadd - the lines from [sc_dfind] on (new findings), and the bar the
;               cheapest correct way
sc_drawfadd:
    call sc_fclamp
    mov ax, [sc_dfind]
    sub ax, [sc_sb+12]
    js .bar                         ; above the view: only the bar moved
.l:
    cmp ax, [sc_sb+10]
    jae .bar
    call sc_drawfline
    inc ax
    jmp short .l
.bar:
    cmp word [sc_sb+10], 0
    je .out
    mov bx, sc_sb
    call os88ui_sbupd
.out:
    ret

; sc_drawfline - findings line AX of the view (keeps AX; clobbers the rest)
sc_drawfline:
    push ax
    mov bx, ax
    mov ax, [sc_geo+SCG_LP]
    mul bx
    add ax, [sc_geo+SCG_FY0]
    mov dx, ax                      ; DX = y
    mov ax, bx
    add ax, [sc_sb+12]
    call sc_fnth                    ; SI = the record, CF = none
    jc .empty
    mov al, [si+FR_SEV]             ; its lamp: a note is a tick, a warning
    mov ah, LP_OK                   ; a checkerboard, a breakage solid
    cmp al, SV_WARN
    jb .ls
    mov ah, LP_WARN
    je .ls
    mov ah, LP_BROKEN
.ls:
    mov al, ah
    xor ah, ah
    mov cx, [sc_geo+SCG_FTX]
    call sc_lamp
    push dx
    mov bx, [sc_geo+SCG_FCOLS]
    sub bx, 2                       ; BX = the text's cells
    mov ax, bx                      ; the path's column: two fifths of them,
    shl ax, 1                       ; 16..28
    xor dx, dx
    mov cx, 5
    div cx
    cmp ax, 16
    jae .a
    mov ax, 16
.a:
    cmp ax, 28
    jbe .b
    mov ax, 28
.b:
    push si
    lea si, [si+FR_PATH]
    mov di, sc_txt
    mov cx, ax
    call sc_scpyc                   ; the path, cut to its column
    mov cx, sc_txt
    add cx, ax                      ; CX = the column's end
.pad:
    cmp di, cx
    ja .rsn
    mov byte [di], ' '
    inc di
    jmp short .pad
.rsn:
    pop si
    lea si, [si+FR_RSN]
    call sc_scpy
    pop dx
    mov cx, [sc_geo+SCG_FTX]
    add cx, 16
    mov si, sc_txt
    mov ax, SC_INK
    call sc_run_at
    pop ax
    ret
.empty:
    mov si, sc_s_none               ; a blank line, the lamp's cells included
    or bx, bx
    jnz .blank
    mov ax, [sc_sb+8]
    or ax, ax
    jnz .blank
    mov al, [sc_sel]                ; the first line of an empty list says why
    xor ah, ah
    call sc_rowlamp
    mov si, sc_f_notyet
    cmp al, LP_PEND
    je .blank
    mov si, sc_f_checking
    cmp al, LP_RUN
    je .blank
    mov si, sc_f_nothing
.blank:
    mov cx, [sc_geo+SCG_FTX]
    mov bx, [sc_geo+SCG_FCOLS]
    mov ax, SC_INK
    call sc_run_at
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_buttons - the five buttons' rects and greying, then the buttons
;              (keeps SI)
; -----------------------------------------------------------------------------
sc_buttons:
    push si
    push bp
    mov ax, [sc_geo+SCG_CX]
    add ax, 8
    mov dx, [sc_geo+SCG_BTY]
    mov cx, dx
    add cx, SC_BTH - 1
    mov di, sc_rects
    mov si, sc_btw
    mov bx, sc_bflags
    xor bp, bp                      ; BP = the button
.r:
    mov [di+0], ax
    mov [di+2], dx
    add ax, [si]
    dec ax
    mov [di+4], ax
    mov [di+6], cx
    add ax, 9                       ; the next one, 8 pixels along
    push ax
    mov ax, bp
    call sc_bok                     ; CF = 1: greyed (SPEC.md 47)
    mov ax, OS88UI_FILL
    jnc .live
    or ax, OS88UI_DIS
.live:
    mov [bx], ax
    pop ax
    add di, 8
    inc si
    inc si
    inc bx
    inc bx
    inc bp
    cmp bp, SC_NB
    jb .r
    mov bx, sc_btrec
    mov al, 1                       ; index PLUS ONE (os88ui_btn)
.d:
    call os88ui_btn
    inc ax
    cmp al, SC_NB + 1
    jb .d
    call sc_pen
    pop bp
    pop si
    ret

; sc_totals - CX = every check's broken findings, DX = every warning
sc_totals:
    push bx
    xor bx, bx
    xor cx, cx
    xor dx, dx
.t:
    add cx, [sc_rbrk+bx]
    add dx, [sc_rwrn+bx]
    inc bx
    inc bx
    cmp bx, 2 * SC_NCHK
    jb .t
    pop bx
    ret

; sc_drawstat - the line beside the buttons
sc_drawstat:
    mov bx, [sc_geo+SCG_STATC]
    or bx, bx
    jz .out
    mov di, sc_txt
    mov byte [di], 0
    cmp byte [sc_run], 0
    je .ended
    mov si, sc_st_scan
    call sc_scpy
    jmp short .draw
.ended:
    cmp byte [sc_ended], 0
    je .draw
    call sc_totals
    mov ax, cx
    or ax, dx
    jnz .some
    mov si, sc_st_clear
    call sc_scpy
    jmp short .draw
.some:
    mov ax, cx
    call dec_u16
    mov si, sc_s_broken
    call sc_scpy
    mov si, sc_s_comma
    call sc_scpy
    mov ax, dx
    call dec_u16
    mov si, sc_s_warning
    call sc_scpy
    cmp dx, 1
    je .draw
    mov byte [di], 's'
    inc di
    mov byte [di], 0
.draw:
    mov si, sc_txt
    mov cx, [sc_geo+SCG_STATX]
    mov dx, [sc_geo+SCG_BTY]
    add dx, 4
    mov bx, [sc_geo+SCG_STATC]
    mov ax, SC_INK
    call sc_run_at
.out:
    ret

; -----------------------------------------------------------------------------
; the selection and the findings' scroll
; -----------------------------------------------------------------------------
; sc_hitrow - AL = the Checks row under CX,DX (screen), CF = 1 none; keeps
;             every other register
sc_hitrow:
    push bx
    push cx
    push dx
    cmp byte [sc_laid], 0
    je .no
    cmp word [sc_geo+SCG_KY1], 0
    je .no
    mov ax, dx
    sub ax, [sc_geo+SCG_KY0]
    jb .no
    xor dx, dx
    div word [sc_geo+SCG_LP]        ; AX = the line
    cmp ax, [sc_geo+SCG_KRPC]
    jae .no
    mov bx, ax
    mov ax, cx
    sub ax, [sc_geo+SCG_KTX]
    jb .no
    xor dx, dx
    div word [sc_geo+SCG_KCW]       ; AX = the column
    mul word [sc_geo+SCG_KRPC]
    add ax, bx
    cmp ax, SC_NCHK                 ; past the last row (or the last column)
    jae .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; sc_select - make row AL the selected one and show its findings
; in:  AL = the row, SI = window; gfx lock held; keeps SI
sc_select:
    cmp al, [sc_sel]
    je .ret
    push si
    mov ah, [sc_sel]
    mov [sc_sel], al
    mov word [sc_sb+12], 0
    push ax
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; a key or a click has no clip armed
    pop ax
    jc .out                         ; not visible: the next paint draws it
    call sc_pen
    push ax
    mov al, ah
    xor ah, ah
    call sc_drawrow                 ; the old row, unselected...
    pop ax
    xor ah, ah
    call sc_drawrow                 ; ...the new one, selected
    call sc_drawfind
.out:
    pop si
.ret:
    ret

; sc_fscroll - move the findings' view by AX lines (keeps SI)
sc_fscroll:
    push si
    mov cx, [sc_sb+12]
    add ax, cx
    jns .p
    xor ax, ax
.p:
    mov [sc_sb+12], ax
    call sc_fclamp
    cmp [sc_sb+12], cx
    je .out
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call sc_pen
    xor ax, ax
.l:
    cmp ax, [sc_sb+10]
    jae .bar
    call sc_drawfline
    inc ax
    jmp short .l
.bar:
    mov bx, sc_sb
    call os88ui_sbupd
.out:
    pop si
    ret

; --- the lamps (SPEC.md 118.5), eight rows each, in LP_* order -------------------
sc_lmono:                           ; 1bpp: SHAPE carries it (inverted on white)
    db 0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00   ; not run: hollow
    db 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40, 0x00   ; running: an arrow
    db 0x02, 0x06, 0x8C, 0xD8, 0x70, 0x20, 0x00, 0x00   ; OK: a tick
    db 0xAA, 0x54, 0xAA, 0x54, 0xAA, 0x54, 0xAA, 0x00   ; warning: checkers
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00   ; broken: solid
sc_lcolsh:                          ; 4bpp: COLOUR carries it
    db 0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00
    db 0x40, 0x60, 0x70, 0x78, 0x70, 0x60, 0x40, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
    db 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0x00
sc_lcol:    db CDGRAY, CBLUE, CGREEN, CBROWN, CRED
sc_pat50:   db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55

; --- the rows' names (SPEC.md 118.1) -------------------------------------------
sc_rnames:  dw sc_n0, sc_n1, sc_n2, sc_n3, sc_n4, sc_n5, sc_n6, sc_n7, sc_n8
sc_n0:      db 'Kernel and boot files', 0
sc_n1:      db 'Drivers', 0
sc_n2:      db 'Packages', 0
sc_n3:      db 'Install leftovers', 0
sc_n4:      db 'Retired programs', 0
sc_n5:      db 'Release files', 0
sc_n6:      db 'Settings', 0
sc_n7:      db 'File associations', 0
sc_n8:      db 'Hibernation image', 0

sc_ticks:   dw sc_t0s, sc_t25, sc_t50, sc_t75, sc_t100
sc_t0s:     db '0%', 0
sc_t25:     db '25%', 0
sc_t50:     db '50%', 0
sc_t75:     db '75%', 0
sc_t100:    db '100%', 0

sc_h_check:   db 'Checking ', 0
sc_h_checked: db 'Checked ', 0
sc_h_stopped: db 'Stopped ', 0
sc_h_dash:    db ' - ', 0
sc_h_ready:   db 'Ready to check a drive', 0
sc_h_hd:      db 'hard disk', 0
sc_h_fl:      db 'floppy', 0
sc_h_checks:  db 'Checks', 0
sc_h_find:    db 'Findings - ', 0
sc_h_sys:     db 'This System', 0

sc_syslbl:  dw sc_l0, sc_l1, sc_l2, sc_l3, sc_l4, sc_l5
sc_sysval:  dw sc_v_build, sc_v_fmt, sc_v_drv, sc_v_vol, sc_v_kern, sc_v_free
sc_l0:      db 'Release build', 0
sc_l1:      db 'Package fmt', 0
sc_l2:      db 'Driver ver', 0
sc_l3:      db 'Volume', 0
sc_l4:      db 'Kernel file', 0
sc_l5:      db 'Free KB', 0
sc_kst:     dw sc_s_dash, sc_k1, sc_k2, sc_k3
sc_k1:      db 'this release', 0
sc_k2:      db 'another build', 0
sc_k3:      db 'missing', 0
sc_s_fat12: db ' FAT12', 0
sc_s_fat16: db ' FAT16', 0

sc_c_files:    db 'Files ', 0
sc_c_folders:  db '  Folders ', 0
sc_c_pkgs:     db '  Packages ', 0
sc_c_done:     db 'Done - ', 0
sc_c_stopd:    db 'Stopped - ', 0
sc_c_filesin:  db ' files in ', 0
sc_c_foldersp: db ' folders', 0
sc_c_comma:    db ', ', 0
sc_c_min:      db ' min ', 0
sc_c_sec:      db ' s', 0

sc_s_none:     db 0
sc_s_dash:     db '-', 0
sc_s_checking: db 'checking', 0
sc_s_ok:       db 'OK', 0
sc_s_broken:   db ' broken', 0
sc_s_comma:    db ', ', 0
sc_s_warn:     db ' warn', 0
sc_s_warning:  db ' warning', 0
sc_st_scan:    db 'Scanning...', 0
sc_st_clear:   db 'No problems found', 0
sc_f_notyet:   db '  Not checked yet', 0
sc_f_checking: db '  Checking...', 0
sc_f_nothing:  db '  Nothing found', 0
