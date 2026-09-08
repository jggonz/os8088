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
; whole line reaches the screen as ONE OSAPI_GFX_BLIT4. On a 1bpp adapter
; at_codebg turns the code cell over on the way past, a grey one being
; unreadable under text there (SPEC.md 46.4.1). The keystroke path repaints
; one line; scrolling is OSAPI_GFX_SCROLL plus the revealed lines; only
; mode/zoom switches and W_PAINT redraw everything.
;
; The colours survive every adapter (SPEC.md 39.4): black text on white,
; CLGRAY code cells on a colour card and INVERSE ones on a 1bpp adapter
; (46.4.1 - a dither cannot carry a letter), XOR selection and caret.
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
%ifndef NOATSBAR
    mov byte [at_sbst], 0           ; the geometry [at_sbty] is expressed in
%endif                              ; has just moved (SPEC.md 46.4.4)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_parse - analyse one visual line into the per-character arrays
; in:  BX = line index
; out: at_lbuf = the slice, [at_pcnt] chars, [at_pstart] logical start,
;      at_vis[] (0 draw / 1 hidden), at_sty[] (AT_ST_*), at_xmap[0..cnt] =
;      x px relative to the text origin (entry cnt = end-of-line caret x),
;      at_cellbg[] per 8px column (0 white / 1 gray) with [at_pcb0]/[at_pcb1]
;      spanning the ones it set, [at_pcw]/[at_prh] = cell width / row height,
;      [at_psc] = scale 1..3; preserves all regs
;
; RAW in Markdown mode, STYLED in Writer mode - THE MODE DECIDES AND NOTHING
; ELSE (SPEC.md 46.2.1). The caret's own logical line used to render raw here
; as well; a document that is one paragraph then never styled at all, which is
; how this shipped. Styled: span delimiters hidden and their
; text styled (at_span's exact toggle semantics, seeded from the line's
; attr nibble), the heading prefix hidden on the logical line's first
; visual line, links recognised within the line ([text](url): text
; underlined and verbatim, the rest hidden).
;
; ...and then at_reveal shows back the one run the caret is standing at the
; end of (SPEC.md 46.2.2), which is what lets markdown be TYPED into a view
; that hides it. It is the last thing this proc does, because it rebuilds
; at_xmap and at_cellbg out of the at_vis it changed. A LINK is the one
; construct that answers to the caret rather than to the run (46.2.3), and it
; has to do that in the FSM above rather than in at_reveal: a click cannot land
; inside a hidden run, so nothing would ever put the caret at the end of one.
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
    mov word [at_pcb0], 0FFFFh      ; ...and an EMPTY code span, which is what
    mov word [at_pcb1], 0           ; a raw line keeps (at_codebg's gate)
    ; raw or styled? MODE ALONE DECIDES (SPEC.md 46.2.1) - Writer styles every
    ; line of the document including the one the caret is on, which is what
    ; the original's BuildHiddenView did and what the View menu's two names
    ; promise. The caret's paragraph used to render RAW here; see 46.2.1 for
    ; why that had to go and what it cost the caret to take it out.
    cmp byte [at_pforce], 0
    jne .styled                     ; at_cmd_none needs the hidden set as
    cmp byte [at_writer], 0         ; the parser defines it, in either mode
    je .raw
%ifdef NOATPLAIN
    jmp .styled
%else
    test byte [bx+at_lattr], 8      ; SPEC.md 46.4.8: at_scan already PROVED
    jz .styled                      ; this line free of the four characters
%endif                              ; that could style it, walking bytes it
                                    ; was reading anyway
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
%ifndef NOATCELL
    ; SPEC.md 46.4.9: .rloop wrote at_vis = 0 and at_sty = 0 for EVERY cell,
    ; and at scale 1 at_xmap[i] is i*8 - so cell i's byte column is i. That is
    ; exactly what at_compose needs to drop the per-cell call.
    mov byte [at_pplain], 1
    cmp word [at_psc], 1
    je .out
    mov byte [at_pplain], 0
%endif
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
    call at_linkcaret               ; CF=1: the caret is standing IN this link
    jnc .hide                       ; ...otherwise the '[' itself is hidden
    mov word [at_plt], 0FFFFh       ; a link the caret is in stays LITERAL, so
    mov word [at_plh], 0FFFFh       ; its URL can be clicked into and edited
    jmp .stylevis                   ; (SPEC.md 46.2.3)
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
    call at_codecols                ; this cell's grey background columns
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
    call at_reveal                  ; ...and the run the caret is typing
%ifndef NOATCELL
    mov byte [at_pplain], 0         ; a styled line has hidden cells and
%endif                              ; per-cell styles: no fast loop
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
; at_codecols - mark a code cell's 8px background columns from the pen x in DI
; out: at_cellbg[] set across the cell and [at_pcb0]/[at_pcb1] widened to the
;      span of every column set; preserves every register
; Factored out of at_parse's .place because at_reveal has to lay these again
; from scratch: they are a function of the pen, and the pen moves when a
; hidden run turns visible.
; -----------------------------------------------------------------------------
at_codecols:
    push ax
    push bx
    push cx
    push dx
    mov ax, di
    mov cl, 3
    shr ax, cl                      ; first 8px column
    mov dx, di
    add dx, [at_pcw]
    dec dx
    shr dx, cl                      ; last column
    mov bx, ax
    cmp bx, [at_pcb0]               ; ...and the SPAN of them, so at_codebg
    jae .g0                         ; walks a code run rather than the line
    mov [at_pcb0], bx
.g0:
.gcol:
    cmp bx, AT_CBGCAP
    jae .gdone
    mov byte [bx+at_cellbg], 1
    cmp bx, [at_pcb1]
    jbe .g1
    mov [at_pcb1], bx
.g1:
    inc bx
    cmp bx, dx
    jbe .gcol
.gdone:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_reveal - show the delimiter run the caret is standing at the end of
;             (SPEC.md 46.2.2)
; in:  at_vis[]/at_sty[]/at_xmap[] as the styled pass left them
; out: the run of HIDDEN characters ending exactly at the caret turned visible,
;      and at_xmap/at_cellbg/[at_pcb0]/[at_pcb1] rebuilt around it; preserves
;      every register. Nothing happens in Markdown mode, under at_pforce, or on
;      a line the caret is not standing on.
;
; THE ONE EXCEPTION TO 46.2.1, and it is what makes typing markdown by hand
; bearable in a view that hides it: `**` you have just typed is a `**` you can
; see, and it disappears at the keystroke that ends it.
;
;     *            ->  *              the run is the caret's, so it shows
;     **           ->  **
;     **H          ->  H (bold)       H is not a delimiter: the run is behind
;     **Hello      ->  Hello          the caret now, and hides
;     **Hello*     ->  Hello*         a new run, at the caret again
;     **Hello**    ->  Hello**
;     **Hello** _  ->  Hello_         the space ends it
;
; It is defined on the RUN and not on the span, which is why one rule covers
; every delimiter the parser has - `*`, `**`, backtick, `~~`, a heading's `#`
; prefix and a link's `](url)` tail - and why the tail of a link is typeable
; without a carve-out of its own: every character of a URL is hidden and each
; one you type is the run's new end.
;
; The rebuild is not optional and it is the whole cost: at_xmap and at_cellbg
; are functions of the PEN, the pen only advances on a visible cell, and the
; run just changed which cells those are. Only the caret's line ever pays it.
; -----------------------------------------------------------------------------
at_reveal:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [at_pforce], 0
    jne .out                        ; at_cmd_none wants the hidden set whole
    cmp byte [at_writer], 0
    je .out
    mov ax, [at_caret]
    sub ax, [at_pstart]
    jb .out                         ; the caret is not on this line...
    cmp ax, [at_pcnt]
    ja .out                         ; ...at either end
    mov si, ax                      ; SI = the caret's slice index
    mov cx, si
.back:
    or cx, cx
    jz .done
    mov bx, cx
    dec bx
    cmp byte [bx+at_vis], 0
    je .done                        ; a visible character ends the run
    mov byte [bx+at_vis], 0
    mov cx, bx
    jmp short .back
.done:
    cmp cx, si
    je .out                         ; nothing was hidden there
    push es                         ; --- the pen moved: lay the columns again
    push ds
    pop es
    cld
    mov di, at_cellbg
    mov cx, AT_CBGCAP/2
    xor ax, ax
    rep stosw
    pop es
    mov word [at_pcb0], 0FFFFh
    mov word [at_pcb1], 0
    xor si, si                      ; SI = index
    xor di, di                      ; DI = pen x
.rex:
    mov bx, si
    shl bx, 1
    mov [bx+at_xmap], di
    cmp si, [at_pcnt]
    jae .out
    cmp byte [si+at_vis], 0
    jne .rnext
    test byte [si+at_sty], AT_ST_C
    jz .radv
    call at_codecols
.radv:
    add di, [at_pcw]
.rnext:
    inc si
    jmp short .rex
.out:
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
; at_linkcaret - is the caret standing inside the link at slice index SI?
; in:  SI = the '[' index, [at_plh] = the ')' index (at_linkscan just set it)
; out: CF=1 when [at_caret] is anywhere in [ '[' .. one past ')' ]; preserves
;      every register
;
; SPEC.md 46.2.3: 46.2.2's run rule reveals the delimiters at the caret, which
; is enough to TYPE a link and not enough to EDIT one - a click cannot land
; inside a hidden run at all, because at_xy2log walks a zero-width cell past.
; So a link is treated as one construct: the caret anywhere in it, the visible
; text included, and the whole `[text](url)` draws literally.
;
; One past the ')' counts, which is where End and the last typed character of
; a URL both leave the caret; the character after THAT is outside, and the link
; collapses - the same shape as the space that ends a `**` run.
; -----------------------------------------------------------------------------
at_linkcaret:
    push ax
    push dx
    cmp byte [at_writer], 0
    je .no                          ; markdown draws the syntax anyway
    cmp byte [at_pforce], 0
    jne .no                         ; at_cmd_none wants the hidden set whole
    mov ax, [at_caret]
    sub ax, [at_pstart]
    jb .no                          ; the caret is not on this line
    cmp ax, si
    jb .no                          ; ...or is before the link
    mov dx, [at_plh]
    inc dx
    cmp ax, dx
    ja .no                          ; ...or past its close
    pop dx
    pop ax
    stc
    ret
.no:
    pop dx
    pop ax
    clc
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
    AT_PAPERAX                      ; SPEC.md 46.4.2: screen polarity, so the
    rep stosw                       ; ground is SET and at_glyph clears ink
    pop es
    xor si, si
%ifndef NOATCELL
    ; --- SPEC.md 46.4.9: a PLAIN scale-1 line, composed without a call ------
    ; at_glyph is 1,034 cycles a cell and its eight-row emitter is 264 of
    ; them; the rest is a call, seven register banks, a range check and a
    ; strip cursor recomputed from at_xmap. On a line at_parse took .rloop
    ; for at scale 1, NONE of that can differ: every cell is visible and
    ; unstyled, so there is no style to dispatch on and no hidden cell to
    ; skip, and cell i's byte column is i - so the cursor is an `inc`.
    cmp byte [at_pplain], 0
    je .ch
    mov cx, [at_pcnt]
    jcxz .done
    mov di, at_strip1               ; cell 0's byte column
.fastc:
    mov al, [si+at_lbuf]
    cmp al, ' '
    jne .fdraw
    cmp byte [at_blankok], 0        ; 46.4.6, and there is no rule to draw:
    jne .fnext                      ; a plain cell carries no link or strike
.fdraw:
    cmp al, 32
    jb .fhuh
    cmp al, 126
    jbe .fok
.fhuh:
    mov al, '?'
.fok:
    push cx
    xor ah, ah
    sub ax, 32
    mov cl, 3
    shl ax, cl
    mov bx, ax
    add bx, at_fontbuf              ; BX -> the eight glyph rows
    push di
    mov cx, 8
.frow:
    mov al, [bx]
    inc bx
%ifndef NOATBLIT1
    not al
%endif
    mov [di], al
    add di, AT_S1ST
    loop .frow
    pop di
    pop cx
.fnext:
    inc di
    inc si
    loop .fastc
    jmp short .done
%endif
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
    call at_codebg                  ; ...and the code span's own ground, which
                                    ; on a 1bpp adapter only this side can draw
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_codebg - the code span's ground on a 1bpp adapter (SPEC.md 46.4.1)
; out: on 1bpp, at_strip1's code columns INVERTED and at_cellbg cleared;
;      nothing at all on a colour adapter. Preserves every register.
;
; CLGRAY is the code cell's background everywhere else and it is the RIGHT
; colour: SPEC.md 39.4 reduces a middle grey to the kernel's 50% dither, which
; is what every other grey in the system wants. It is wrong HERE, and only
; here, because a code cell is the one grey in the UI with TEXT ON TOP OF IT -
; a black 8x8 glyph on a 50% checkerboard carries the same ink as its own
; ground, so the word inside the span stops being a word. Measured on a
; Hercules, `code` came back a smudge: the one style of the six that did not
; survive the adapter.
;
; A LIGHTER GROUND IS NOT THE ANSWER, and the glass is what settled it: a 25%
; stipple was built first and read barely better, because what destroys the
; letter is not the ground's density but a dot that TOUCHES a stroke - one
; beside the bowl of an `o` closes it - and knocking a one-pixel halo out
; under every glyph pixel bought legible-if-you-squint, for ~30 bytes and a
; dilation per column. Inverting the span is FOUR bytes and reads outright.
;
; So on 1bpp the code cell is the badge turned over: `not` makes the ground
; black and the glyph a hole in it, which is the same distinction the colour
; adapter draws and the only one this adapter can draw legibly. The rules
; at_glyph laid - a strike, a link's underline - invert with it, correctly.
;
; The expander is then told the truth for this adapter, which is that the line
; has no grey columns in it at all: at_cellbg is CLEARED, so at_expand widens
; every byte through at_x4w and its hot loop pays NOTHING for any of this. The
; app owning the code ground is the same division of labour the styling
; already uses - at_compose bolds, shears, underlines and rules its own glyphs
; rather than asking the kernel for a face it has not got.
;
; [at_pcb0]/[at_pcb1] are why this is not on the keystroke path: at_parse
; records the code span's first and last 8px column, and a line with no code
; in it - which is nearly every line - leaves 0FFFFh > 0 and is refused on the
; first compare. A line that HAS one walks the span, never the line.
;
; SS != DS, so BP is a COUNTER here and never an address (at_glyph's rule);
; the strip is addressed [bx+si], both DS defaults.
; -----------------------------------------------------------------------------
at_codebg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [at_vbpp], 1
    ja .out                         ; a colour adapter keeps CLGRAY, which is
                                    ; a light grey there and reads perfectly
    mov dx, [at_pcb1]
    cmp [at_pcb0], dx
    ja .out                         ; no code span on this line
    mov si, at_strip1               ; SI = this row's base in the strip
    xor bp, bp                      ; BP = row
.row:
    cmp bp, [at_prh]
    jae .clear
    mov bx, [at_pcb0]               ; BX = 8px column
.col:
    cmp byte [bx+at_cellbg], 0
    je .nxc
    not byte [bx+si]
.nxc:
    inc bx
    cmp bx, dx
    jbe .col
    add si, AT_S1ST
    inc bp
    jmp short .row
.clear:
    push es                         ; the ground is drawn: the expander has no
    push ds                         ; grey columns left to widen
    pop es
    cld
    mov di, at_cellbg
    mov cx, AT_CBGCAP/2
    xor ax, ax
    rep stosw
    pop es
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
%ifndef NOATBLANK
    ; SPEC.md 46.4.6: a SPACE inks nothing, and at_compose has already laid
    ; this cell's ground. AT_ST_L and AT_ST_S are the two styles that ink a
    ; blank cell, and at_ruleat draws them AFTER the row loop, so a cell
    ; carrying either is composed normally. Bold, both nibble doublers and the
    ; clc-seeded shear all preserve blankness.
    cmp al, ' '
    jne .notsp
    test dl, AT_ST_L | AT_ST_S
    jnz .notsp
    cmp byte [at_blankok], 0        ; ...and only if the FACE agrees
    je .notsp
    jmp .nst
.notsp:
%endif
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
%ifndef NOATFAST
    ; --- THE UNSTYLED SCALE-1 CELL (SPEC.md 46.4.3) ------------------------
    ; ...which is nearly every character of body text and ALL of Markdown
    ; mode. The general body below answers the same four questions eight
    ; times a cell - bold? scale? italic? - and folds four bytes in; here
    ; every answer is fixed and the row is one byte at one column.
    ;
    ; A plain STORE is correct rather than a fold because at_compose has
    ; already laid this cell's ground down, and at scale 1 no two visible
    ; cells share a byte column (at_xmap's x is a multiple of 8). The
    ; general path's [bx+1..3] writes are no-ops here anyway: at_grow+1..3
    ; are zero, which complemented is 0FFh, and `and 0FFh` changes nothing.
    or dl, dl                       ; the style, still live in DL
    jnz .row
    cmp word [at_psc], 1            ; a WORD compare: at_psc IS a word, and a
    jne .row                        ; low-byte one reads scale 257 as scale 1
    mov cx, 8
.fast:
    lodsb                           ; DF is clear: at_compose and at_bigtext
%ifndef NOATBLIT1
    not al                          ; both `cld` before they call in, which
%endif                              ; this makes a precondition rather than a
    mov [bx], al                    ; coincidence
    add bx, AT_S1ST
    loop .fast
    jmp .nst                        ; no rule to draw - AT_ST_L and AT_ST_S
%endif                              ; are in DL, and DL is zero on this path
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
%ifdef NOATROW
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
%ifndef NOATBLIT1
    not word [at_grow]
    not word [at_grow+2]
%endif
    ; fold the scaled row into `scale` consecutive strip rows
    mov dx, [at_psc]
.vrep:
    mov al, [at_grow]
    AT_MERGE [bx], al
    mov al, [at_grow+1]
    AT_MERGE [bx+1], al
    mov al, [at_grow+2]
    AT_MERGE [bx+2], al
    mov al, [at_grow+3]
    AT_MERGE [bx+3], al
    add bx, AT_S1ST
    dec dx
    jnz .vrep
%else
    ; --- SPEC.md 46.4.5: the row into REGISTERS, once ----------------------
    ; at_grow is four bss bytes, and the two stages below treated it as
    ; working storage rather than as a value: the shear read-modify-wrote all
    ; four PER SHIFT STEP and .vrep re-read all four ON EVERY REPEAT - eight
    ; reads a repeat at scale 2, to move one byte of glyph.
    ;
    ; THE BYTE ORDER IS INVERTED ON PURPOSE. at_grow[0] is the LEFTMOST 8 px
    ; and must sit in AH, because that is what makes `rcr ax,1 / rcr dx,1` a
    ; genuine 32-bit shift right across b0->b1->b2->b3: rcr ax,1 carries AH's
    ; bit 0 into AL's bit 7 and out of AL into DX. `mov ax, [at_grow]` would
    ; put b0 in AL and run the shear BACKWARDS - every italic mirrored, and
    ; nothing anywhere raises.
    mov ah, [at_grow]               ; b0, the leftmost 8 px
    mov al, [at_grow+1]
    mov dh, [at_grow+2]
    mov dl, [at_grow+3]
    test byte [at_gsty], AT_ST_I
    jz .noshear
    cmp bp, 4                       ; the top half only
    jae .noshear
    mov cx, [at_psc]
.shr:
    clc                             ; a clear bit shifted in is NO INK, which
    rcr ax, 1                       ; is why 46.4.2's complement is below and
    rcr dx, 1                       ; not above this
    loop .shr                       ; `loop` writes no flag, so CF survives it
.noshear:
%ifndef NOATBLIT1
    ; SPEC.md 46.4.2. The complement is HERE and can be nowhere above: the
    ; bold overstrike and the shear above both reason in INK. All four bytes,
    ; because .vrep writes all four at every scale.
    not ax
    not dx
%endif
    ; fold the scaled row into `scale` consecutive strip rows. The counter is
    ; CX now: DX is half the glyph row.
    mov cx, [at_psc]
.vrep:
    AT_MERGE [bx], ah
    AT_MERGE [bx+1], al
    AT_MERGE [bx+2], dh
    AT_MERGE [bx+3], dl
    add bx, AT_S1ST
    loop .vrep
%endif
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
    mov byte [bx+at_strip1], AT_RULE
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
%ifdef NOATBLIT1
    mov ax, [at_tw]                 ; the pre-46.4.2 order, kept verbatim so
    mov [at_xw], ax                 ; this arm assembles byte for byte
    call at_expand                  ; identical to the package before it
%endif
    ; THE ROW CLAMP STAYS ABOVE THE ARM AND MUST: OSAPI_GFX_BLIT1 refuses
    ; DX = 0 with CF = 1, and a refusal falls into the expander below, which
    ; would then draw a full-height line off the bottom of the text region.
    mov dx, [at_prh]
    mov ax, [at_dly]
    add ax, dx
    cmp ax, [at_ty1]
    jbe .rows
    mov dx, [at_ty1]
    sub dx, [at_dly]
    jg .rows
%ifdef NOATBLIT1
    jmp short .out                  ; fully below the region: nothing
%else
    jmp .out                        ; ...and the band arm below puts .out out
%endif                              ; of a short jump's reach
.rows:
%ifndef NOATBLIT1
    ; --- the band arm (SPEC.md 46.4.2) -------------------------------------
    ; at_compose has already built exactly what OSAPI_GFX_BLIT1 takes: 1bpp,
    ; row-major, bit 7 leftmost, SCREEN polarity, at at_tx0 = 64 with at_tw
    ; masked to a multiple of 8 (at_geom_init) and at_prh <= AT_SROWS = 30
    ; against the slot's 255. The DEFAULT pen carries it on every adapter, so
    ; there is no OSAPI_GFX_BLIT1_PEN call: on 1bpp the pen is not read, and
    ; on VGA the default pair short-circuits gfx_blit1's .pvga with no port
    ; write and takes the plain `rep movsw` emit rather than the complemented
    ; one. This is what deletes at_expand AND the kernel's 4bpp decode from
    ; the line: 64.3 -> 3.3 ms on Hercules, 135.4 -> 2.9 on VGA (PREDICTED;
    ; Paint's identical round trip MEASURED 22x, SPEC.md 42.23.4).
    cmp byte [at_vbpp], 1
    jbe .band                       ; 1bpp: at_codebg has already CLEARED
                                    ; at_cellbg, so no grey column survives
                                    ; and the band is the whole truth
    mov ax, [at_pcb1]               ; a colour adapter keeps CLGRAY, so only a
    cmp [at_pcb0], ax               ; line with NO code span is two colours -
    ja .band                        ; at_codebg's own compare, reused
    jmp short .wide
.band:
    push ds
    pop es
    push bx
    push dx
    mov si, at_strip1
    mov bp, AT_S1ST
    mov ax, [at_tx0]
    mov bx, [at_dly]
    mov cx, [at_tw]
    call OSAPI_GFX_BLIT1
    pop dx                          ; pop leaves the flags, so CF still speaks
    pop bx
    jnc .sel                        ; CF = 1 is a REFUSAL and nothing drawn -
                                    ; kern_small carries the slot and not the
                                    ; body (SPEC.md 5.4.2.5) - so fall into
                                    ; the expander, which is that build's only
                                    ; path and draws the identical picture
.wide:
    ; ...and on the band path the expansion happens HERE, below the clamp, so
    ; a line the clamp threw away never pays for it either.
    mov ax, [at_tw]
    mov [at_xw], ax
    call at_expand
%endif
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
.sel:
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
; at_filltail - white from row AX down to at_ty1-1, across the text width
; in:  AX = first row (< at_ty1); out: nothing; preserves all registers
; SPEC.md 46.4.10. The two whole-view repaints had this inline and used it to
; erase the ground they were about to draw on; it erases what the text does
; NOT cover now, which is why it takes the top row as an argument.
; -----------------------------------------------------------------------------
at_filltail:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_tx0]
    mov cx, ax
    add cx, [at_tw]
    dec cx
    mov dx, [at_ty1]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
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
%ifdef NOATTAIL
    mov ax, [at_ty0]                ; the pre-46.4.10 erase: the WHOLE region,
    call at_filltail                ; every pixel of it drawn over again below
%else
    mov ax, [at_ty0]
    mov [at_tby], ax                ; SPEC.md 46.4.10: the first row nothing
%endif                              ; has drawn on yet
    mov bx, [at_top]
.line:
    cmp bx, [at_nlines]
    jae .tail
    call at_line_y
    jc .tail
    call at_draw_line
%ifndef NOATTAIL
    mov ax, [at_dly]                ; ...and at_parse's own row height, which
    add ax, [at_prh]                ; is the one at_line_y would walk to
    mov [at_tby], ax
%endif
    inc bx
    jmp short .line
.tail:
%ifndef NOATTAIL
    mov ax, [at_tby]
    cmp ax, [at_ty1]
    jae .done                       ; the text reached the bottom by itself
    call at_filltail
%endif
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
%ifndef NOATCX
    ; SPEC.md 46.4.8: on a line at_scan has PROVED plain, at_xmap[i] is i*pcw
    ; by construction - so the caret's x is a multiply and none of at_parse is
    ; needed. Not the slice either: the proof came off the layout's own walk,
    ; which is what makes this worth more than proving it here would be.
    test byte [bx+at_lattr], 8
    jz .parse
    call at_lgeom                   ; AX = cell width, DX = row height
    mov [at_pcw], ax
    mov [at_prh], dx
    mov cl, 3
    shr ax, cl
    mov [at_psc], ax                ; the glyph box below wants the scale
    push bx
    shl bx, 1
    mov ax, [bx+at_lstart]
    pop bx
    mov [at_pstart], ax
    mov cx, [at_caret]
    sub cx, ax                      ; AX is still at_pstart
    mov ax, cx
    mul word [at_pcw]               ; DX is dead here and pushed at entry
    jmp short .havex
.parse:
%endif
    call at_parse
    mov ax, [at_caret]
    sub ax, [at_pstart]
    push bx
    mov bx, ax
    shl bx, 1
    mov ax, [bx+at_xmap]
    pop bx
.havex:
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
%ifdef NOATSBAR
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
    mov si, ax
%else
    ; --- SPEC.md 46.4.4: what is already on the glass ---------------------
    ; at_maxtop is walked ONCE here and handed to at_thumby_m below; it used
    ; to be walked again inside at_thumby.
    call at_maxtop
    mov si, ax                      ; SI = maxtop, live to the end
    or ax, ax
    jnz .want
    ; the document FITS, so there is no bar. Erase one only if one is there.
    cmp byte [at_sbst], 2
    je .out                         ; already known blank: ZERO far calls,
                                    ; which is what every Enter in a short
                                    ; document used to pay a full-height
                                    ; gutter fill for - the fill sat ABOVE
                                    ; the test that decides there is no bar
    call .blank
    mov byte [at_sbst], 2
    jmp .out
.want:
    cmp byte [at_sbst], 1
    jne .full                       ; POISONED or blank: draw the lot
    cmp si, [at_sbmax]
    jne .full                       ; the document's length moved
    mov cx, si
    call at_thumby_m
    cmp ax, [at_sbty]
    je .out                         ; NOTHING MOVED. On a long document this
                                    ; is most scrolls: the travel is
                                    ; ty1-ty0-47 px over maxtop lines, so a
                                    ; 1,000-line Hercules document moves the
                                    ; thumb 0.24 px a line
    ; --- only the thumb moved: grey its old span and draw it again --------
    ; The span is sbx+1..sbx+14 - the SHAFT's width, not the thumb's 16. The
    ; columns either side are the OUTER frame, black for the bar's whole
    ; height, and the thumb's own frame only ever redraws them the same
    ; colour. gfx_fill_gray's dither phase is a function of absolute y, so a
    ; partial re-grey is phase-correct by construction.
    push ax
    mov ax, [at_sbx]
    inc ax
    mov bx, [at_sbty]
    mov cx, [at_sbx]
    add cx, 14
    mov dx, bx
    add dx, 15
    call OSAPI_GFX_FILL_GRAY
    pop ax
    mov [at_sbty], ax
    call .thumb
    jmp .out
.full:
    ; BANK MAXTOP NOW, not at the thumb below: the two arrow loops between
    ; here and there use SI as their own 1..5 counter, so SI is ZERO by the
    ; time the thumb is drawn. Reading it there banked at_sbmax = 0 - which
    ; made every later call miss its own bank and take this path again - and
    ; handed at_thumby_m a zero divisor. tests/atblit.py caught it as a thumb
    ; frozen at the top of the shaft while the view was at the bottom.
    mov [at_sbmax], si
    call .blank                     ; the arrow boxes' ground
%endif
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
%ifdef NOATSBAR
    call at_thumby
    mov [at_sbty], ax
%else
    mov cx, [at_sbmax]              ; ...and read it back from the bank, for
    call at_thumby_m                ; the reason above
    mov [at_sbty], ax
    mov byte [at_sbst], 1
%endif
    call .thumb
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the gutter's ground, and the thumb at [at_sbty] - the two pieces both the
; full body and 46.4.4's incremental arm need
.blank:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_sbx]
    mov bx, [at_ty0]
    dec bx
    mov cx, ax
    add cx, 15
    mov dx, [at_ty1]
    call OSAPI_GFX_FILL
    ret
.thumb:
    mov bx, [at_sbty]
    mov dx, bx
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
    push cx                         ; the public form, which resolves maxtop
    call at_maxtop                  ; itself - at_sb_click's call site is
    mov cx, ax                      ; unchanged by SPEC.md 46.4.4
    call at_thumby_m
    pop cx
    ret

; -----------------------------------------------------------------------------
; at_thumby_m - the same, with maxtop ALREADY in CX (and nonzero)
; in:  CX = maxtop; out: AX = the thumb's y; preserves BX/CX/DX
; at_sbar walks at_maxtop once and hands it here; it used to be walked TWICE
; per bar, once there and once in at_thumby (SPEC.md 46.4.4).
; -----------------------------------------------------------------------------
at_thumby_m:
    push bx
    push dx
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
    pop bx
    ret
