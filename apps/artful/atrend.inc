; =============================================================================
; os8088 - apps/artful/atrend.inc
;
; ArtfulType's renderer (SPEC.md 46.4). One visual line is one picture: the
; line parser (at_parse) turns a slice into per-character visibility, style
; and x-position arrays; the composer (at_compose) builds the line as 1bpp
; rows in at_strip1, styling glyphs itself from the ROM 8x8 font - bold is
; an overstrike, italic a two-step row shear, links an underline, strike a
; centre rule, headings a 2x/3x bit-doubled scale-up; the expander
; (at_expand) widens 1bpp to packed 4bpp through a 256-entry table (white
; background, or light-gray for code spans, chosen per 8px column); and the
; whole line reaches the screen as ONE OSAPI_GFX_BLIT4. The keystroke path
; repaints one line; scrolling is OSAPI_GFX_SCROLL plus the revealed lines;
; only mode/zoom switches and W_PAINT redraw everything.
;
; The colours survive every adapter (SPEC.md 39.4): black text on white,
; CLGRAY code cells (a dither on 1bpp), XOR selection and caret.
;
; The caret is an XOR vertical bar with strict on/off bookkeeping
; (at_caret_on/off): anything that may touch its pixels turns it off first
; and back on after, always under the gfx lock - which is also what makes
; the blink worker's toggling safe.
; =============================================================================

; style bits in at_sty[]
AT_ST_B  equ 1                      ; bold (overstrike)
AT_ST_I  equ 2                      ; italic (shear)
AT_ST_C  equ 4                      ; code (gray cell background)
AT_ST_L  equ 8                      ; link (underline)
AT_ST_S  equ 16                     ; strikethrough

; -----------------------------------------------------------------------------
; at_geom_init - latch the live screen geometry (SPEC.md 39: ask, never assume)
; out: at_vw/at_vh/at_vbpp/at_tx0/at_ty0/at_ty1/at_tw/at_sbx; preserves all
; -----------------------------------------------------------------------------
at_geom_init:
    push ax
    push bx
    push cx
    push dx
    push si                         ; this proc PRESERVES ALL and does not
    mov bx, [at_win]                ; frame SI, where the cell answers the
    call OSAPI_WM_DISPLAY           ; band's top
    pop si                          ; AX=w, BX=h, CX=dock row, DL=adapter,
                                    ; DH=bpp - **FOR THE CARD THIS WINDOW IS
                                    ; ON** (SPEC.md 39.16.4). It matters here
                                    ; because the second caller is at_fs_enter:
                                    ; SPEC.md 11.2 sizes a fullscreen window to
                                    ; the display it is ON, so a Writer entered
                                    ; on the second card was laying its text
                                    ; column out for the primary's width
    mov [at_vw], ax
    mov [at_vh], bx
    mov [at_vbpp], dh
    sub ax, 128                     ; MARGIN_H = 64 each side - ArtfulType's
    and ax, 0FFF8h                  ; own margins; byte-aligned for the blits
    mov [at_tw], ax
    mov word [at_tx0], 64
    mov ax, 64
    add ax, [at_tw]
    add ax, 24                      ; the scroll bar centres in the right
    mov [at_sbx], ax                ; margin: text right + (64-16)/2
    mov ax, 44                      ; MBAR_H 20 + a 24px top margin
    mov cx, bx
    sub cx, 20                      ; 20px bottom margin
    cmp bx, 300                     ; CGA's 200 rows cannot afford either
    jae .tall
    mov ax, 30
    mov cx, bx
    sub cx, 4
.tall:
    mov [at_ty0], ax
    mov [at_ty1], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_cline_span - cache the caret's logical line as a visual-line range
; out: [at_cll0]/[at_cll1] = first/last visual line of the caret's logical
;      line (the range that renders RAW in Writer mode); preserves all
; -----------------------------------------------------------------------------
at_cline_span:
    push ax
    push bx
    mov ax, [at_caret]
    call at_line_of
    call at_lhome
    mov [at_cll0], bx
.fwd:
    inc bx
    cmp bx, [at_nlines]
    jae .end
    test byte [bx+at_lattr], 4
    jnz .fwd
.end:
    dec bx
    mov [at_cll1], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_parse - analyse one visual line into the per-character arrays
; in:  BX = line index
; out: at_lbuf = the slice, [at_pcnt] chars, [at_pstart] logical start,
;      at_vis[] (0 draw / 1 hidden), at_sty[] (AT_ST_*), at_xmap[0..cnt] =
;      x px relative to the text origin (entry cnt = end-of-line caret x),
;      at_cellbg[] per 8px column (0 white / 1 gray), [at_pcw]/[at_prh] =
;      cell width / row height, [at_psc] = scale 1..3; preserves all regs
;
; RAW when Markdown mode, or when the line belongs to the caret's logical
; line in Writer mode (the live-preview rule, SPEC.md 46.2): every char
; visible, no styles. STYLED otherwise: span delimiters hidden and their
; text styled (at_span's exact toggle semantics, seeded from the line's
; attr nibble), the heading prefix hidden on the logical line's first
; visual line, links recognised within the line ([text](url): text
; underlined and verbatim, the rest hidden).
; -----------------------------------------------------------------------------
at_parse:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call at_slice                   ; at_lbuf, CX = len, AX = start
    mov [at_pcnt], cx
    mov [at_pstart], ax
    call at_lgeom                   ; AX = cellw, DX = rowh
    mov [at_pcw], ax
    mov [at_prh], dx
    mov cl, 3
    shr ax, cl
    mov [at_psc], ax                ; scale = cellw/8
    ; clear the column backgrounds
    push es
    push ds
    pop es
    cld
    mov di, at_cellbg
    mov cx, AT_CBGCAP/2
    xor ax, ax
    rep stosw
    pop es
    ; raw or styled?
    cmp byte [at_pforce], 0
    jne .styled                     ; at_cmd_none needs the hidden set as
    cmp byte [at_writer], 0         ; the parser defines it, in either mode
    je .raw
    cmp bx, [at_cll0]
    jb .styled
    cmp bx, [at_cll1]
    jbe .raw
    jmp short .styled
.raw:
    xor si, si                      ; SI = char index
    xor dx, dx                      ; DX = x
    mov cx, [at_pcnt]
.rloop:
    cmp si, cx
    jae .rdone
    mov byte [si+at_vis], 0
    mov byte [si+at_sty], 0
    mov di, si
    shl di, 1
    mov [di+at_xmap], dx
    add dx, [at_pcw]
    inc si
    jmp short .rloop
.rdone:
    mov di, si
    shl di, 1
    mov [di+at_xmap], dx
    jmp .out
.styled:
    mov al, [bx+at_lattr]
    mov ah, al
    and ah, 0F0h
    mov [at_pspan], ah              ; the span nibble at the line's start
    mov dx, ax
    and dx, 3                       ; DX = heading level
    xor bp, bp                      ; BP = hide-prefix countdown
    test al, 4
    jnz .nopfx                      ; continuations carry no prefix
    or dx, dx
    jz .nopfx
    mov bp, dx
    inc bp                          ; the '#'s and the space
.nopfx:
    ; heading base style: H1 and H3 bold, H2 plain - the weight hierarchy
    ; one bitmap size can still express
    xor al, al
    or dx, dx
    jz .base
    mov al, AT_ST_B
    cmp dx, 2
    jne .base
    xor al, al
.base:
    mov [at_pbase], al
    mov word [at_plt], 0FFFFh       ; link text end (index of ']')
    mov word [at_plh], 0FFFFh       ; link hide end (index of ')')
    mov word [at_scskip], 0FFFFh
    xor si, si                      ; SI = char index
    xor di, di                      ; DI = x
.sloop:
    cmp si, [at_pcnt]
    jae .sdone
    mov al, [si+at_lbuf]
    mov cl, [at_pbase]              ; CL = style, CH = hidden flag
    xor ch, ch
    ; heading prefix?
    or bp, bp
    jz .nopref
    dec bp
    inc ch
    jmp .place
.nopref:
    ; inside a recognised link?
    cmp word [at_plt], 0FFFFh
    je .fsm
    cmp si, [at_plt]
    jae .ltail
    or cl, AT_ST_L                  ; link text: verbatim + underline
    jmp .place
.ltail:
    inc ch                          ; ']' .. ')': hidden
    cmp si, [at_plh]
    jne .place
    mov word [at_plt], 0FFFFh
    mov word [at_plh], 0FFFFh
    jmp .place
.fsm:
    cmp si, [at_scskip]
    je .hide                        ; second char of ** / ~~
    cmp al, '`'
    je .code
    test byte [at_pspan], AT_SP_C
    jnz .stylevis                   ; inside code: everything is literal
    cmp al, '*'
    je .star
    cmp al, '~'
    je .tilde
    cmp al, '['
    je .bracket
.stylevis:
    mov ah, [at_pspan]
    test ah, AT_SP_B
    jz .ni
    or cl, AT_ST_B
.ni:
    test ah, AT_SP_I
    jz .nc
    or cl, AT_ST_I
.nc:
    test ah, AT_SP_C
    jz .ns
    or cl, AT_ST_C
.ns:
    test ah, AT_SP_S
    jz .place
    or cl, AT_ST_S
    jmp short .place
.code:
    xor byte [at_pspan], AT_SP_C
    jmp short .hide
.star:
    call at_pkbuf                   ; AH = next slice byte or 0
    cmp ah, '*'
    je .bold2
    xor byte [at_pspan], AT_SP_I
    jmp short .hide
.bold2:
    xor byte [at_pspan], AT_SP_B
    jmp short .eat2
.tilde:
    call at_pkbuf
    cmp ah, '~'
    jne .stylevis                   ; a lone '~' is literal
    xor byte [at_pspan], AT_SP_S
.eat2:
    mov dx, si
    inc dx
    mov [at_scskip], dx
.hide:
    inc ch
    jmp short .place
.bracket:
    call at_linkscan                ; CF=0: at_plt/at_plh set
    jc .stylevis
    jmp short .hide                 ; the '[' itself is hidden
.place:
    mov [si+at_vis], ch
    mov [si+at_sty], cl
    mov dx, si
    shl dx, 1
    xchg bx, dx
    mov [bx+at_xmap], di
    xchg bx, dx
    or ch, ch
    jnz .next
    test cl, AT_ST_C
    jz .adv
    ; gray background columns for this cell
    push ax
    push bx
    push dx
    mov ax, di
    mov cl, 3
    shr ax, cl                      ; first 8px column
    mov dx, di
    add dx, [at_pcw]
    dec dx
    shr dx, cl                      ; last column
    mov bx, ax
.gcol:
    cmp bx, AT_CBGCAP
    jae .gdone
    mov byte [bx+at_cellbg], 1
    inc bx
    cmp bx, dx
    jbe .gcol
.gdone:
    pop dx
    pop bx
    pop ax
.adv:
    add di, [at_pcw]
.next:
    inc si
    jmp .sloop
.sdone:
    mov dx, si
    shl dx, 1
    xchg bx, dx
    mov [bx+at_xmap], di
    xchg bx, dx
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_pkbuf - AH = at_lbuf[SI+1], or 0 at the slice's end; preserves all else
at_pkbuf:
    push si
    inc si
    cmp si, [at_pcnt]
    jae .no
    mov ah, [si+at_lbuf]
    pop si
    ret
.no:
    xor ah, ah
    pop si
    ret

; -----------------------------------------------------------------------------
; at_linkscan - try to read [text](url) starting at the '[' at index SI
; out: CF=0 and at_plt = index of ']', at_plh = index of ')' - or CF=1
; The whole structure must sit inside this visual line (SPEC.md 46.4).
; -----------------------------------------------------------------------------
at_linkscan:
    push ax
    push di
    mov di, si
.rb:
    inc di
    cmp di, [at_pcnt]
    jae .no
    mov al, [di+at_lbuf]
    cmp al, ']'
    jne .rb
    mov ax, di                      ; AX = the ']'
    inc di
    cmp di, [at_pcnt]
    jae .no
    cmp byte [di+at_lbuf], '('
    jne .no
.rp:
    inc di
    cmp di, [at_pcnt]
    jae .no
    cmp byte [di+at_lbuf], ')'
    jne .rp
    mov [at_plt], ax
    mov [at_plh], di
    clc
    pop di
    pop ax
    ret
.no:
    stc
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_compose - build the parsed line's pixels in at_strip1
; out: at_strip1 rows 0..[at_prh]-1 filled; preserves all registers
; -----------------------------------------------------------------------------
at_compose:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, at_strip1
    mov ax, [at_prh]
    mov cx, AT_S1ST/2
    mul cx
    mov cx, ax
    xor ax, ax
    rep stosw
    pop es
    xor si, si
.ch:
    cmp si, [at_pcnt]
    jae .done
    cmp byte [si+at_vis], 0
    jne .next
    mov al, [si+at_lbuf]
    mov dl, [si+at_sty]
    mov bx, si
    shl bx, 1
    mov di, [bx+at_xmap]            ; x px (a multiple of 8 by construction)
    call at_glyph
.next:
    inc si
    jmp short .ch
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_glyph - draw one styled, scaled glyph into at_strip1
; in:  AL = char (32..126; anything else renders as '?'), DL = AT_ST_*,
;      DI = x px within the strip (multiple of 8), [at_psc] = scale 1..3
; out: nothing; preserves all registers
; SS != DS: the strip cursor lives in BX (a DS default), never BP.
; -----------------------------------------------------------------------------
at_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp al, 32
    jb .huh
    cmp al, 126
    jbe .chok
.huh:
    mov al, '?'
.chok:
    mov [at_gsty], dl
    xor ah, ah
    sub ax, 32
    mov cl, 3
    shl ax, cl
    mov si, ax
    add si, at_fontbuf              ; SI -> the 8 glyph rows
    mov cl, 3
    shr di, cl                      ; DI = byte column in the strip
    mov bx, at_strip1
    add bx, di                      ; BX = strip write cursor
    xor bp, bp                      ; BP = source row 0..7
.row:
    mov al, [si]
    inc si
    test byte [at_gsty], AT_ST_B
    jz .nb
    mov ah, al
    shr ah, 1
    or al, ah                       ; overstrike bold
.nb:
    ; horizontal scale into at_grow[0..3]
    mov word [at_grow+2], 0
    mov dx, [at_psc]
    cmp dx, 2
    je .s2
    ja .s3
    mov [at_grow], al
    mov byte [at_grow+1], 0
    jmp short .sheared
.s2:
    push bx
    mov bl, al
    xor bh, bh
    mov cl, 4
    shr bx, cl
    mov ah, [bx+at_dbl]
    mov bl, al
    and bx, 15
    mov al, [bx+at_dbl]
    mov [at_grow], ah
    mov [at_grow+1], al
    pop bx
    jmp short .sheared
.s3:
    push bx
    mov bl, al
    xor bh, bh
    mov cl, 4
    shr bx, cl
    shl bx, 1
    mov dx, [bx+at_trp]             ; 12 bits for the high nibble
    mov bl, al
    xor bh, bh
    and bx, 15
    shl bx, 1
    mov bx, [bx+at_trp]             ; 12 bits for the low nibble
    ; bytes: [dx>>4] [(dx<<4)|(bx>>8)] [bx&ff]
    mov ax, dx
    mov cl, 4
    shr ax, cl
    mov [at_grow], al
    mov ax, dx
    shl ax, cl
    or al, bh
    mov [at_grow+1], al
    mov [at_grow+2], bl
    pop bx
.sheared:
    ; italic: the top half (source rows 0..3) shifts right by `scale` px
    test byte [at_gsty], AT_ST_I
    jz .noshear
    cmp bp, 4
    jae .noshear
    mov cx, [at_psc]
.shr:
    clc
    rcr byte [at_grow], 1
    rcr byte [at_grow+1], 1
    rcr byte [at_grow+2], 1
    rcr byte [at_grow+3], 1
    loop .shr
.noshear:
    ; OR the scaled row into `scale` consecutive strip rows
    mov dx, [at_psc]
.vrep:
    mov al, [at_grow]
    or [bx], al
    mov al, [at_grow+1]
    or [bx+1], al
    mov al, [at_grow+2]
    or [bx+2], al
    mov al, [at_grow+3]
    or [bx+3], al
    add bx, AT_S1ST
    dec dx
    jnz .vrep
    inc bp
    cmp bp, 8
    jb .row
    ; underline (links) and strike rules, `scale` px thick
    test byte [at_gsty], AT_ST_L
    jz .nul
    mov ax, [at_psc]
    mov cl, 3
    shl ax, cl                      ; first row under the glyph box
    call at_ruleat
.nul:
    test byte [at_gsty], AT_ST_S
    jz .nst
    mov ax, [at_psc]
    shl ax, 1
    shl ax, 1
    dec ax                          ; 4*scale - 1: the centre
    call at_ruleat
.nst:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_ruleat - run a rule across one glyph cell
; in:  AX = first strip row, DI = byte column, [at_psc] = scale (thickness
;      in rows AND width in bytes)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_ruleat:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [at_psc]                ; SI = rows left
.r:
    cmp ax, [at_prh]
    jae .out
    push ax
    mov dx, AT_S1ST
    mul dx
    add ax, di
    mov bx, ax
    mov cx, [at_psc]
.b:
    mov byte [bx+at_strip1], 0FFh
    inc bx
    loop .b
    pop ax
    inc ax
    dec si
    jnz .r
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_expand - widen at_strip1 (1bpp) into at_strip4 (packed 4bpp)
; in:  [at_prh] rows; at_cellbg picks white or gray background per column
; out: at_strip4 filled for [at_tw] px; preserves all registers
; -----------------------------------------------------------------------------
at_expand:
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
    cld
    mov si, at_strip1
    mov di, at_strip4
    mov dx, [at_prh]
.row:
    push si
    push di
    mov ax, [at_xw]
    mov cl, 3
    shr ax, cl
    mov cx, ax                      ; CX = source bytes this row
    xor bp, bp                      ; BP = column index
.byte:
    lodsb
    mov bx, bp
    mov ah, [bx+at_cellbg]          ; latched BEFORE the index arithmetic
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    or ah, ah
    jnz .gray
    mov ax, [bx+at_x4w]
    stosw
    mov ax, [bx+at_x4w+2]
    stosw
    jmp short .nx
.gray:
    mov ax, [bx+at_x4g]
    stosw
    mov ax, [bx+at_x4g+2]
    stosw
.nx:
    inc bp
    loop .byte
    pop di
    pop si
    add si, AT_S1ST
    add di, AT_S4ST
    dec dx
    jnz .row
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
; at_draw_line - parse, compose and blit one visual line
; in:  BX = line index, AX = its screen y (from at_line_y)
; out: nothing; preserves all registers; gfx lock held by the caller
; The blit clips itself to the text region's bottom: the last visible line
; may be partial, and the strip simply sends fewer rows.
; -----------------------------------------------------------------------------
at_draw_line:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [at_dly], ax
    call at_parse
    call at_compose
    mov ax, [at_tw]
    mov [at_xw], ax
    call at_expand
    mov dx, [at_prh]
    mov ax, [at_dly]
    add ax, dx
    cmp ax, [at_ty1]
    jbe .rows
    mov dx, [at_ty1]
    sub dx, [at_dly]
    jg .rows
    jmp short .out                  ; fully below the region: nothing
.rows:
    push ds
    pop es
    push bx
    mov si, at_strip4
    mov bp, AT_S4ST
    mov ax, [at_tx0]
    mov bx, [at_dly]
    mov cx, [at_tw]
    call OSAPI_GFX_BLIT4
    pop bx
    call at_selxor
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

; -----------------------------------------------------------------------------
; at_selxor - XOR-invert this line's intersection with the selection
; in:  BX = line index (freshly at_parse'd: at_xmap valid), [at_dly] = y
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_selxor:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [at_sela]
    mov dx, [at_selb]
    cmp ax, dx
    je .out
    jb .ord
    xchg ax, dx
.ord:
    mov si, [at_pstart]
    cmp dx, si
    jbe .out
    mov cx, si
    add cx, [at_pcnt]
    cmp ax, cx
    ja .out                         ; (== cx keeps the EOL cell out below)
    cmp ax, si
    jae .lo
    mov ax, si
.lo:
    cmp dx, cx
    jbe .hi
    mov dx, cx
.hi:
    sub ax, si
    sub dx, si                      ; slice-relative [AX, DX)
    push bx
    mov bx, ax
    shl bx, 1
    mov ax, [bx+at_xmap]
    mov bx, dx
    shl bx, 1
    mov dx, [bx+at_xmap]
    pop bx
    cmp ax, dx
    je .out
    add ax, [at_tx0]
    add dx, [at_tx0]
    dec dx
    mov cx, dx                      ; CX = x2 inclusive
    push bx
    mov bx, [at_dly]
    mov dx, bx
    add dx, [at_prh]
    dec dx
    cmp dx, [at_ty1]
    jb .yok
    mov dx, [at_ty1]
    dec dx
.yok:
    call OSAPI_GFX_XOR_FILL
    pop bx
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_line_y - a visible line's screen y
; in:  BX = line index
; out: AX = y with CF=0, or CF=1 (not on screen); preserves all else
; -----------------------------------------------------------------------------
at_line_y:
    push bx
    push cx
    push dx
    mov cx, bx                      ; CX = target
    mov bx, [at_top]
    cmp cx, bx
    jb .off
    mov ax, [at_ty0]
.walk:
    cmp bx, cx
    jae .got
    push ax
    call at_lgeom                   ; DX = rowh
    pop ax
    add ax, dx
    inc bx
    cmp ax, [at_ty1]
    jae .off
    jmp short .walk
.got:
    cmp ax, [at_ty1]
    jae .off
    clc
    jmp short .out
.off:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_draw_text - repaint the whole text region (erase + every visible line)
; out: nothing; preserves all registers; lock held by the caller
; -----------------------------------------------------------------------------
at_draw_text:
    push ax
    push bx
    push cx
    push dx
    call at_caret_off
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_tx0]
    mov bx, [at_ty0]
    mov cx, ax
    add cx, [at_tw]
    dec cx
    mov dx, [at_ty1]
    dec dx
    call OSAPI_GFX_FILL
    mov bx, [at_top]
.line:
    cmp bx, [at_nlines]
    jae .done
    call at_line_y
    jc .done
    call at_draw_line
    inc bx
    jmp short .line
.done:
    call at_caret_on
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_draw_cline - repaint exactly the caret's line (the keystroke path)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_draw_cline:
    push ax
    push bx
    call at_caret_off
    mov ax, [at_caret]
    call at_line_of
    call at_line_y
    jc .done
    call at_draw_line
.done:
    call at_caret_on
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_caret_on / at_caret_off - the XOR caret, with bookkeeping
; It shows only when: fullscreen editor, no selection, no menu or modal up,
; blink phase lit, and its line is on screen. at_caret_off is uncondition-
; ally safe: it erases exactly what was drawn, or nothing.
; -----------------------------------------------------------------------------
at_caret_on:
    push ax
    push bx
    push cx
    push dx
    cmp byte [at_cshown], 0
    jne .out
    cmp byte [at_fs], 0
    je .out
    cmp byte [at_modal], 0
    jne .out
    cmp byte [at_menuon], 0
    jne .out
    cmp byte [at_cphase], 0
    jne .out                        ; blinked dark
    mov ax, [at_sela]
    cmp ax, [at_selb]
    jne .out
    mov ax, [at_caret]
    call at_line_of
    call at_line_y
    jc .out
    mov [at_ccy], ax
    call at_parse
    mov ax, [at_caret]
    sub ax, [at_pstart]
    push bx
    mov bx, ax
    shl bx, 1
    mov ax, [bx+at_xmap]
    pop bx
    add ax, [at_tx0]
    mov [at_ccx], ax
    mov ax, [at_psc]
    mov cl, 3
    shl ax, cl                      ; glyph-box height
    mov bx, [at_ccy]
    mov dx, bx
    add dx, ax
    dec dx
    cmp dx, [at_ty1]
    jb .yok
    mov dx, [at_ty1]
    dec dx
.yok:
    mov [at_ccy2], dx
    mov ax, [at_ccx]
    mov cx, ax
    call OSAPI_GFX_XOR_FILL
    mov byte [at_cshown], 1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

at_caret_off:
    push ax
    push bx
    push cx
    push dx
    cmp byte [at_cshown], 0
    je .out
    mov ax, [at_ccx]
    mov cx, ax
    mov bx, [at_ccy]
    mov dx, [at_ccy2]
    call OSAPI_GFX_XOR_FILL
    mov byte [at_cshown], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_scroll_to - change [at_top], moving pixels when it is cheap
; in:  AX = new top line (caller clamps to 0..at_maxtop)
; out: nothing; preserves all registers; lock held by the caller
; A move of up to 3 lines rides OSAPI_GFX_SCROLL; anything bigger, or a
; refused blit (a clip region cutting the rect), is a full repaint.
; -----------------------------------------------------------------------------
at_scroll_to:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, [at_top]
    je .out
    call at_caret_off
    mov si, [at_top]                ; SI = old top
    mov [at_top], ax
    mov bx, ax
    sub bx, si                      ; BX = move (signed)
    jns .down
    neg bx
    cmp bx, 3
    ja .full
    mov di, ax                      ; up: dy = -(heights of new..old-1)
    call at_sumn
    neg dx
    jmp short .blit
.down:
    cmp bx, 3
    ja .full
    mov di, si                      ; down: dy = heights of old..new-1
    call at_sumn
.blit:
    mov si, dx                      ; SI = signed dy
    mov ax, [at_tx0]
    mov bx, [at_ty0]
    mov cx, ax
    add cx, [at_tw]
    dec cx
    mov dx, [at_ty1]
    dec dx
    call OSAPI_GFX_SCROLL
    jc .full
    ; repaint the vacated band (plus the bottom line the region edge always
    ; cuts): scroll down = every line whose bottom crosses ty1 - dy;
    ; scroll up = every line above ty0 + |dy|
    or si, si
    js .upband
    mov di, [at_ty1]
    sub di, si
    mov bx, [at_top]
.bb:
    cmp bx, [at_nlines]
    jae .done
    call at_line_y
    jc .done
    push ax
    call at_lgeom                   ; DX = rowh
    pop ax
    push ax
    add ax, dx
    cmp ax, di
    pop ax
    jbe .bskip
    call at_draw_line
.bskip:
    inc bx
    jmp short .bb
.upband:
    mov di, [at_ty0]
    sub di, si                      ; band bottom = ty0 + |dy|
    mov bx, [at_top]
.ub:
    cmp bx, [at_nlines]
    jae .done
    call at_line_y
    jc .done
    cmp ax, di
    jae .done
    call at_draw_line
    inc bx
    jmp short .ub
.full:
    call at_draw_text
.done:
    call at_sbar
    call at_caret_on
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_sumn - DX = total height of BX lines starting at line DI; preserves else
at_sumn:
    push ax
    push bx
    push cx
    push si
    mov cx, bx
    mov si, di
    xor di, di
.s:
    jcxz .done
    mov bx, si
    call at_lgeom                   ; DX = rowh
    add di, dx
    inc si
    loop .s
.done:
    mov dx, di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_seecaret - scroll so the caret's line is fully visible
; out: nothing (the scroll repaints); preserves all registers
; -----------------------------------------------------------------------------
at_seecaret:
    push ax
    push bx
    push cx
    push dx
    push di
    mov ax, [at_caret]
    call at_line_of
    cmp bx, [at_top]
    jae .below
    mov ax, bx                      ; above the view: it becomes the top
    call at_scroll_to
    jmp short .out
.below:
    call at_line_y
    jc .push
    push ax
    call at_lgeom                   ; DX = rowh
    pop ax
    add ax, dx
    cmp ax, [at_ty1]
    jbe .out                        ; fully visible already
.push:
    ; new top = smallest t <= caret line with sum h(t..line) <= the view
    mov cx, bx                      ; CX = candidate top, walking backward
    push bx
    call at_lgeom
    pop bx
    mov di, dx                      ; DI = accumulated height
    mov ax, [at_ty1]
    sub ax, [at_ty0]                ; AX = room
.grow:
    jcxz .have
    push bx
    mov bx, cx
    dec bx
    call at_lgeom                   ; DX = rowh of the line above
    pop bx
    add di, dx
    cmp di, ax
    ja .have
    dec cx
    jmp short .grow
.have:
    mov ax, cx
    call at_scroll_to
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_sbar - draw (or blank) the scroll bar in the right margin
; out: nothing; preserves all registers; lock held by the caller
; Classic Macintosh anatomy: an arrow box at each end, a 50%-gray shaft, a
; 16px square white thumb - and hidden entirely while the document fits
; the view (gScrollBarVisible's rule, main.c).
; -----------------------------------------------------------------------------
at_sbar:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [at_fs], 0
    je .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_sbx]
    mov bx, [at_ty0]
    dec bx
    mov cx, ax
    add cx, 15
    mov dx, [at_ty1]
    call OSAPI_GFX_FILL
    call at_maxtop
    or ax, ax
    jz .out                         ; the document fits: no bar at all
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ; frame + the two arrow boxes
    mov ax, [at_sbx]
    mov bx, [at_ty0]
    dec bx
    mov cx, ax
    add cx, 15
    mov dx, [at_ty1]
    call OSAPI_GFX_FRAME
    mov dx, bx
    add dx, 15
    call OSAPI_GFX_FRAME
    mov bx, [at_ty1]
    sub bx, 15
    mov dx, [at_ty1]
    call OSAPI_GFX_FRAME
    ; up arrow (HLINE: AX = x1, BX = x2, DX = y)
    mov dx, [at_ty0]
    add dx, 4
    mov si, 1                       ; half-width 1..5
.ua:
    mov ax, [at_sbx]
    add ax, 7
    sub ax, si
    mov bx, [at_sbx]
    add bx, 7
    add bx, si
    call OSAPI_GFX_HLINE
    inc dx
    inc si
    cmp si, 6
    jb .ua
    ; down arrow
    mov dx, [at_ty1]
    sub dx, 12
    mov si, 5
.da:
    mov ax, [at_sbx]
    add ax, 7
    sub ax, si
    mov bx, [at_sbx]
    add bx, 7
    add bx, si
    call OSAPI_GFX_HLINE
    inc dx
    dec si
    jnz .da
    ; shaft
    mov ax, [at_sbx]
    inc ax
    mov bx, [at_ty0]
    add bx, 15
    mov cx, [at_sbx]
    add cx, 14
    mov dx, [at_ty1]
    sub dx, 16
    call OSAPI_GFX_FILL_GRAY
    ; thumb
    call at_thumby
    mov bx, ax
    mov dx, ax
    add dx, 15
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_sbx]
    mov cx, ax
    add cx, 15
    push bx
    push dx
    call OSAPI_GFX_FILL
    pop dx
    pop bx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [at_sbx]
    mov cx, ax
    add cx, 15
    call OSAPI_GFX_FRAME
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_maxtop - the highest at_top that still fills the view
; out: AX = it (0 = the document fits entirely); preserves all else
; -----------------------------------------------------------------------------
at_maxtop:
    push bx
    push cx
    push dx
    mov bx, [at_nlines]
    mov cx, [at_ty1]
    sub cx, [at_ty0]
.back:
    or bx, bx
    jz .zero
    dec bx
    call at_lgeom                   ; DX = rowh
    sub cx, dx
    jc .stop
    jmp short .back
.stop:
    inc bx
    mov ax, bx
    cmp ax, [at_nlines]
    jb .out
    mov ax, [at_nlines]
    dec ax
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_thumby - the thumb's top y for the current at_top
; out: AX = y; preserves all else
; -----------------------------------------------------------------------------
at_thumby:
    push bx
    push cx
    push dx
    call at_maxtop
    mov cx, ax                      ; CX = maxtop (nonzero at the call site)
    mov ax, [at_ty1]
    sub ax, [at_ty0]
    sub ax, 47                      ; travel = shaft height - 16 (thumb):
    jns .t                          ; (ty1-16) - (ty0+15) - 16
    xor ax, ax
.t:
    mov bx, [at_top]
    cmp bx, cx
    jbe .m
    mov bx, cx                      ; clamp a stale top
.m:
    mul bx                          ; DX:AX = travel * top
    div cx
    add ax, [at_ty0]
    add ax, 15
    pop dx
    pop cx
    pop bx
    ret
