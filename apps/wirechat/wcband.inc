; =============================================================================
; os8088 - apps/wirechat/wcband.inc
;
; THE BAND, AND WHAT IS ON THE GLASS (SPEC.md 113.5.1, 113.5.2).
;
; Every row WireChat draws is a BAND: a composition of cells (wc_sbuf, built by
; the wc_sb* helpers in wcview.inc) and a META block (wc_meta) that says how
; to draw it - a STYLE, up to WC_NSPAN colour SPANS, up to two 8x8 GLYPHS and
; one BOLD run. wc_bandc draws the pair as one band: a run of WireChat's own
; face (THE FACE, below; SPEC.md 113.5.3) per colour pair - its air, its
; glyphs and its bold one composed blit - then the glyphs, so a band
; repaints every pixel it owns exactly once (PERFORMANCE.md rules 1 and 2).
;
; THE SLOTS. A band drawn with a SLOT (BX) is drawn THROUGH it: the slot
; holds the meta and the cells as they are on the glass, so a band whose
; meta and cells are what the slot holds draws NOTHING, and one whose meta
; matches and whose cells do not draws ONE run of the cells between the
; first that differs and the last (split at the colours it crosses, the
; glyphs inside it put back) - MIDIRack's field slots
; (SPEC.md 105.9.4), for every row of the window. A slot whose meta moved is
; drawn whole. A W_PAINT owes every slot (wc_owe): the kernel white-filled
; the content before calling it, so nothing the slots say is true any more.
;
; THE THEMES (SPEC.md 113.5.2). Every colour is a ROLE (R_*): a theme is one
; row of wc_themes, copied into wc_pens by wc_face at each full paint, and a
; style, a span, a glyph or a button asks wc_pens for its role. A theme change
; is a full paint, which owes every slot, so no slot compares across two.
; Wired and Mono-Dark are black and white alone: the same picture on a VGA as
; on a Hercules or a CGA, where every other colour rounds (SPEC.md 39.4). They
; lead the table, so `below TH_NMONO` is "one bit can draw it". On a colour
; display Wired is the LAST row, TH_WIREDC: the same black on white with its
; statuses, badges and nicks in colour (and its LEDs and emoticons, wc_tint).
; =============================================================================

wc_themes:      ; R_BG FG DIM ACC  HI HIFG  OK WARN BAD BADFG  MENTG MENTF  NICK x4
    db CWHITE, CBLACK, CBLACK, CBLACK,  CBLACK, CWHITE                ; wired
    db CBLACK, CBLACK, CBLACK, CWHITE,  CWHITE, CBLACK
    db CBLACK, CBLACK, CBLACK, CBLACK
    db CBLACK, CWHITE, CWHITE, CWHITE,  CWHITE, CBLACK                ; dark
    db CWHITE, CWHITE, CWHITE, CBLACK,  CBLACK, CWHITE
    db CWHITE, CWHITE, CWHITE, CWHITE
    db CBLUE, CWHITE, CLGRAY, CLCYAN,  CLCYAN, CBLUE                  ; blue
    db CLGREEN, CYELLOW, CLRED, CWHITE,  CMAGENTA, CWHITE
    db CYELLOW, CLCYAN, CLGREEN, CLMAGENTA
    db CBLACK, CLGREEN, CGREEN, CGREEN,  CLGREEN, CBLACK              ; green
    db CLGREEN, CGREEN, CWHITE, CBLACK,  CGREEN, CWHITE
    db CLGREEN, CWHITE, CLGREEN, CWHITE
    db CBLACK, CYELLOW, CBROWN, CBROWN,  CYELLOW, CBLACK              ; amber
    db CYELLOW, CBROWN, CLRED, CBLACK,  CBROWN, CWHITE
    db CYELLOW, CYELLOW, CYELLOW, CYELLOW
    db CWHITE, CBLACK, CBLACK, CBLACK,  CBLACK, CWHITE                ; wired,
    db CGREEN, CBROWN, CRED, CWHITE,  CWHITE, CBLACK                  ; colour
    db CBLUE, CMAGENTA, CGREEN, CRED
%if $ - wc_themes != (TH_N + 1) * WC_NPEN
  %error "wc_themes is TH_N rows of WC_NPEN roles, and Wired's colour row"
%endif

; the styles: (ink, ground) as ROLES
wc_stytab:
    db R_FG, R_BG                           ; WS_NORM
    db R_HIFG, R_HI                         ; WS_INV: a selection, the view
    db R_ACC, R_BG                          ; WS_ACC: titles, the prompt
    db R_MENTF, R_MENTG                     ; WS_MENT: a message naming us
    db R_ACC, R_BG                          ; WS_NOTE: the notice line
    db R_DIM, R_BG                          ; WS_DIM: a day divider
    db R_BAD, R_BG                          ; WS_ALERT: new messages
WS_NORM     equ 0
WS_INV      equ 1
WS_ACC      equ 2
WS_MENT     equ 3
WS_NOTE     equ 4
WS_DIM      equ 5
WS_ALERT    equ 6

; --- wc_pen - AL = the colour of role AL. Preserves the rest ----------------
wc_pen:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [wc_pens+bx]
    pop bx
    ret

; --- wc_penc - [gfx_color] = role AL. Preserves every register --------------
wc_penc:
    push ax
    call wc_pen
    call OSAPI_SET_COLOR
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_mreset - an empty meta of style AL: one span, from cell 0, in the
;             style's own colours; no glyph, no bold. Preserves every register
; -----------------------------------------------------------------------------
wc_mreset:
    push cx
    push di
    mov di, wc_meta
    mov cx, WC_METAN
.z:
    mov byte [di], 0
    inc di
    loop .z
    mov [wc_meta+WCX_STY], al
    mov byte [wc_meta+WCX_NSP], 1
    mov word [wc_meta+WCX_SP+1], 0xFFFF     ; span 0: the style's ink and ground
    mov byte [wc_meta+WCX_G0], 0xFF
    mov byte [wc_meta+WCX_G1], 0xFF
    mov byte [wc_meta+WCX_UL], 0xFF
    pop di
    pop cx
    ret

; --- wc_span - a span from cell CL: ink AL, ground AH (0xFF = the style's) -
; Spans go in left to right; cell 0 RE-COLOURS span 0. One past WC_NSPAN is
; dropped, which leaves its cells in the span before. Preserves all.
wc_span:
    push bx
    push dx
    mov bx, wc_meta + WCX_SP
    or cl, cl
    jz .set
    mov bl, [wc_meta+WCX_NSP]
    cmp bl, WC_NSPAN
    jae .out
    inc byte [wc_meta+WCX_NSP]
    xor bh, bh
    mov dx, bx
    shl bx, 1
    add bx, dx
    add bx, wc_meta + WCX_SP
.set:
    mov [bx], cl
    mov [bx+1], al
    mov [bx+2], ah
.out:
    pop dx
    pop bx
    ret

; --- wc_mglyph - glyph BL at cell CL: the first free of the two. Preserves all
wc_mglyph:
    cmp byte [wc_meta+WCX_G0], 0xFF
    jne .g1
    mov [wc_meta+WCX_G0], cl
    mov [wc_meta+WCX_G0+1], bl
    ret
.g1:
    mov [wc_meta+WCX_G1], cl
    mov [wc_meta+WCX_G1+1], bl
    ret

; --- wc_mbold - the bold: CL cells from cell CH. Preserves all -------------
wc_mbold:
    mov [wc_meta+WCX_BC], ch
    mov [wc_meta+WCX_BL], cl
    ret

; --- wc_basepair - AL = ink, AH = ground of the meta's style, in this theme -
wc_basepair:
    push bx
    mov bl, [wc_meta+WCX_STY]
    xor bh, bh
    shl bx, 1
    mov ax, [wc_stytab+bx]              ; the two roles...
    mov bl, ah
    mov ah, [wc_pens+bx]                ; ...and their colours
    mov bl, al
    mov al, [wc_pens+bx]
    pop bx
    ret

; --- wc_spcol - span BX's colours, resolved: AL = ink, AH = ground ---------
wc_spcol:
    push dx
    call wc_basepair
    mov dx, ax
    mov ax, [bx+1]
    cmp al, 0xFF
    jne .i
    mov al, dl
.i:
    cmp ah, 0xFF
    jne .g
    mov ah, dh
.g:
    pop dx
    ret

; --- wc_spanat - BX = the span cell CL is in. Preserves the rest -----------
wc_spanat:
    push ax
    push cx
    mov bx, wc_meta + WCX_SP
    mov ch, [wc_meta+WCX_NSP]
.s:
    dec ch
    jz .out
    cmp cl, [bx+3]
    jb .out
    add bx, 3
    jmp short .s
.out:
    pop cx
    pop ax
    ret

; --- wc_spend - CL = where span BX ends (one past): the next span's start,
; or the composition's end - and never past it. Preserves the rest but CH
wc_spend:
    push ax
    push dx
    mov ax, bx
    sub ax, wc_meta + WCX_SP
    mov dl, 3
    div dl                              ; AL = the span's index
    inc ax
    mov cl, [wc_meta+WCX_LEN]
    cmp al, [wc_meta+WCX_NSP]
    jae .out                            ; the last runs to the end
    cmp cl, [bx+3]
    jbe .out
    mov cl, [bx+3]
.out:
    pop dx
    pop ax
    ret

; =============================================================================
; wc_bandc - draw the composition as one band, through slot BX
; in:  CX = x, DX = the band's top (CONTENT-relative, CX a multiple of 8), BX
;      = the slot (0 = none: drawn whole, remembered nowhere); wc_sbuf, and
;      wc_meta from wc_mreset and its friends; the lock held, the clip armed
; out: every register preserved
; =============================================================================
wc_bandc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add cx, [wc_ox]
    add dx, [wc_oy]
    mov [wc_bx], cx
    mov [wc_by], dx
    mov si, wc_sbuf
    call wc_slen
    mov [wc_meta+WCX_LEN], cl
    cmp bx, wc_sln                      ; a narrow slot holds WC_NCAP cells: a
    jb .cap                             ; longer composition is drawn whole and
    cmp cl, WC_NCAP                     ; remembered nowhere
    jbe .cap
    xor bx, bx
.cap:
    mov [wc_bslot], bx
    or bx, bx
    jz .full
    cmp byte [bx+WCX_NSP], 0            ; owed
    je .full
    mov si, wc_meta                     ; the same meta - but for which
    mov di, bx                          ; glyphs, which are a cell each and
    xor cx, cx                          ; drawn alone (wc_bglyphs), and a
    mov [wc_binkd], cl                  ; span's INK, which is its run again?
.mc:
    cmp cx, WCX_G0 + 1
    je .mn
    cmp cx, WCX_G1 + 1
    je .mn
    mov al, [si]
    cmp al, [di]
    je .mn
    call wc_isink                       ; CF = 0: a span's ink byte
    jc .full
    mov byte [wc_binkd], 1
.mn:
    inc si
    inc di
    inc cx
    cmp cx, WC_METAN
    jb .mc
    ; --- the same meta: the cells that differ, first to last --------------
    xor cx, cx
    mov cl, [wc_meta+WCX_LEN]
    jcxz .done
    xor si, si
    mov dx, 0xFFFF
    mov bx, di                          ; BX = the slot's cells
.tc:
    mov al, [wc_sbuf+si]
    cmp al, [bx+si]
    je .tn
    mov [wc_blast], si
    cmp dx, 0xFFFF
    jne .tn
    mov dx, si
.tn:
    inc si
    loop .tc
    cmp byte [wc_binkd], 0              ; a span whose ink moved: all its
    je .tw                              ; cells are in the run
    call wc_inkrun
.tw:
    mov [wc_bfirst], dx
    cmp dx, 0xFFFF
    je .glyphs                          ; no cell moved
    call wc_bpart
.glyphs:
    call wc_bglyphs
    jmp short .meta
.full:
    call wc_bfull
.meta:
    mov di, [wc_bslot]
    or di, di
    jz .done
    mov si, wc_meta
    mov cx, WC_METAN
.mm:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .mm
.text:
    mov di, [wc_bslot]                  ; the cells, as they now are
    add di, WC_METAN
    mov si, wc_sbuf
    xor cx, cx
    mov cl, [wc_meta+WCX_LEN]
    jcxz .done
.tm:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .tm
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_isink - CF = 0 when meta byte CX is a live span's INK: a change
; there is that span's run drawn again, its ground and its air unchanged.
; Preserves all but flags
wc_isink:
    push ax
    push dx
    mov ax, cx
    sub ax, WCX_SP
    jb .no
    mov dl, 3
    div dl                              ; AL = the span, AH = the byte in it
    cmp ah, 1
    jne .no
    cmp al, [wc_meta+WCX_NSP]
    jae .no
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; --- wc_inkrun - widen the run [DX, [wc_blast]] (DX = 0xFFFF none) by every
; span whose ink is not the slot's. clobbers AX, BX, CX, SI, DI
wc_inkrun:
    mov bx, wc_meta + WCX_SP
    mov al, [wc_meta+WCX_NSP]
    mov [wc_bn], al
.s:
    mov di, bx
    sub di, wc_meta
    add di, [wc_bslot]
    mov al, [bx+1]
    cmp al, [di+1]
    je .n
    call wc_spse                        ; [SI, DI)
    cmp si, di
    jae .n
    dec di
    cmp dx, 0xFFFF
    je .new
    cmp si, dx
    jae .lo
    mov dx, si
.lo:
    cmp di, [wc_blast]
    jbe .n
    mov [wc_blast], di
    jmp short .n
.new:
    mov dx, si
    mov [wc_blast], di
.n:
    add bx, 3
    dec byte [wc_bn]
    jnz .s
    ret

; --- wc_bglyphs - the glyphs whose picture moved and that the run did not put
; back: one cell each, opaque (a glyph's mask is the whole cell)
wc_bglyphs:
    mov bx, [wc_bslot]
    mov dl, [bx+WCX_G0+1]
    mov cl, [wc_meta+WCX_G0]
    mov al, [wc_meta+WCX_G0+1]
    call .g
    mov bx, [wc_bslot]
    mov dl, [bx+WCX_G1+1]
    mov cl, [wc_meta+WCX_G1]
    mov al, [wc_meta+WCX_G1+1]
.g:
    cmp al, dl
    je .out
    cmp cl, 0xFF
    je .out
    cmp cl, [wc_meta+WCX_LEN]
    jae .out
    cmp word [wc_bfirst], 0xFFFF        ; inside the run that was drawn: put
    je .draw                            ; back by it already
    xor dx, dx
    mov dl, cl
    cmp dx, [wc_bfirst]
    jb .draw
    cmp dx, [wc_blast]                  ; one past the run (wc_bpart)
    jb .out
.draw:
    mov bl, al
    jmp wc_bglyph
.out:
    ret

; --- wc_bfull - every span's run, then the glyphs ----------------------------
; A run is the whole band's height (SPEC.md 113.5.3), its air and its bold
; inside it, so there is nothing else to draw: what used to be an air pair
; per ground, a run per span and an overstrike is a run per COLOUR PAIR now -
; wc_bspans puts adjacent spans of one ink and ground down as one.
wc_bfull:
    mov word [wc_bfirst], 0
    mov ax, 0x7FFF
    mov [wc_blast], ax
    call wc_bspans
    xor si, si                          ; the glyphs: the whole band
    mov di, 0x7FFF
    jmp wc_bdeco

; --- wc_bpart - the run [wc_bfirst, wc_blast] again, span by span -----------
; A bold cell is bold in the run that draws it, so a run cuts through the
; bold anywhere and nothing has to be widened to take it in (the overstrike
; this replaced spilled a pixel into the cell after it, and did). On the way
; out [wc_blast] is one PAST the run, which is what wc_bglyphs reads.
wc_bpart:
    inc word [wc_blast]                 ; [first, last + 1)
    call wc_bspans
    mov si, [wc_bfirst]
    mov di, [wc_blast]
    ; fall through: the glyphs inside [SI, DI)

; --- wc_bdeco - the glyphs whose cell is in [SI, DI) -------------------------
wc_bdeco:
    mov cl, [wc_meta+WCX_G0]
    mov bl, [wc_meta+WCX_G0+1]
    call .g
    mov cl, [wc_meta+WCX_G1]
    mov bl, [wc_meta+WCX_G1+1]
.g:
    cmp cl, 0xFF
    je .out
    cmp cl, [wc_meta+WCX_LEN]
    jae .out
    xor ax, ax
    mov al, cl
    cmp ax, si
    jb .out
    cmp ax, di
    jae .out
    jmp wc_bglyph
.out:
    ret

; --- wc_bspans - the cells [[wc_bfirst], [wc_blast]) of every span, cut to
; them, as runs: a span whose ink and ground are the run before it's, and
; which starts where that run ends, JOINS it (Wired's message line is a
; time, a nick and a text all black on white - one blit, not three).
; clobbers AX, BX, CX, SI, DI
wc_bspans:
    mov bx, wc_meta + WCX_SP
    mov al, [wc_meta+WCX_NSP]
    mov [wc_bn], al
    mov word [wc_bps], 0xFFFF           ; nothing pending
.sp:
    call wc_spse                        ; the span's [start, end)...
    cmp si, [wc_bfirst]                 ; ...cut to the run's
    jae .s1
    mov si, [wc_bfirst]
.s1:
    cmp di, [wc_blast]
    jbe .s2
    mov di, [wc_blast]
.s2:
    cmp si, di
    jae .next
    call wc_spcol                       ; AL = ink, AH = ground
    cmp word [wc_bps], 0xFFFF
    je .start
    cmp ax, [wc_bpc]
    jne .flush
    cmp si, [wc_bpe]
    je .join
.flush:
    push si
    push di
    push ax
    mov si, [wc_bps]
    mov di, [wc_bpe]
    mov ax, [wc_bpc]
    call wc_brun
    pop ax
    pop di
    pop si
.start:
    mov [wc_bps], si
    mov [wc_bpc], ax
.join:
    mov [wc_bpe], di
.next:
    add bx, 3
    dec byte [wc_bn]
    jnz .sp
    cmp word [wc_bps], 0xFFFF
    je .out
    mov si, [wc_bps]
    mov di, [wc_bpe]
    mov ax, [wc_bpc]
    jmp wc_brun
.out:
    ret

; --- wc_spse - span BX's cells: SI = its start, DI = one past its end.
; clobbers CX
wc_spse:
    mov cl, [bx]
    xor ch, ch
    mov si, cx
    call wc_spend
    xor ch, ch
    mov di, cx
    ret

; --- wc_brun - cells [SI, DI) in AL = ink, AH = ground: one wc_fput --------
; The band's bold cells are bold where they fall inside the run, and its
; UNDERLINE (a section title's rule, SPEC.md 113.5) is the run's last row.
; Preserves every register.
wc_brun:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, ax                          ; DL = ink, DH = ground
    xor ax, ax                          ; the bold cells, counted from SI
    mov al, [wc_meta+WCX_BC]
    sub ax, si
    mov [wc_fbc], ax
    xor cx, cx
    mov cl, [wc_meta+WCX_BL]
    add ax, cx
    mov [wc_fbe], ax
    mov al, [wc_meta+WCX_UL]
    mov [wc_ful], al
    mov cx, di
    sub cx, si                          ; CX = the cells
    mov ax, si
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [wc_bx]
    mov bx, [wc_by]
    add si, wc_sbuf
    call wc_fput
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE FACE (SPEC.md 113.5.3): WireChat's own type, 8 x 10 cells - a cell is a
; band - in apps/wirechat/wcface.t88, carried in the package as 95 glyphs of
; WC_FROWS rows (wc_font, wctxt.inc). A run of it is COMPOSED here, cell by
; cell into wc_fband at the band's fixed stride, and put down by ONE
; OSAPI_GFX_BLIT1 in the run's two colours (SPEC.md 5.4.2, 5.4.2.2): the
; ground, the air above and below and the glyphs are one pass, as font_run's
; were, and bold is ORed in at compose time rather than struck over.
; Every x is on the byte grid (WF_SNAP; a band's cells, the field's pen,
; Send's and Search's captions), so a cell is one byte of every band row and
; the compose shifts nothing.
; =============================================================================

; --- wc_flit - CF = 1 when colour AL is LIT on a display of one bit a pixel:
; light red, yellow and white (SPEC.md 39.4). Preserves every register
wc_flit:
    cmp al, CWHITE
    je .lit
    cmp al, CYELLOW
    je .lit
    cmp al, CLRED
    je .lit
    clc
    ret
.lit:
    stc
    ret

; -----------------------------------------------------------------------------
; wc_fput - CX characters at DS:SI, in WireChat's face, as one band
; in:  AX = x (screen, a multiple of 8), BX = y: the CELL's top, the row
;      above where an 8x8 glyph would stand; DL = ink, DH = ground;
;      [wc_fbc], [wc_fbe] = the bold characters [first, end), counted from
;      SI and SIGNED; [wc_ful] = the last row's colour, 0xFF = the ground's.
;      The lock held, the clip armed. CX above WC_FSTR draws WC_FSTR
; out: every register preserved
;
; THE POLARITY. A set bit is the blit's INK, and on a display of one bit the
; pen is not read at all: a set bit is LIT (SPEC.md 5.4.2.2). So the set
; bits are always whichever of the two colours a one-bit display lights - the
; glyph's on a dark ground, the GROUND's on a light one (Wired: black on
; white), the compose complementing every row for it - and the pen is turned
; round to match, so a colour display draws the same picture either way.
; -----------------------------------------------------------------------------
wc_fput:
    jcxz .none                          ; nothing to draw
    jmp short .go
.none:
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [wc_fx], ax
    mov [wc_fy], bx
    cmp cx, WC_FSTR
    jbe .n
    mov cx, WC_FSTR
.n:
    mov [wc_fcnt], cx
    xor bl, bl                          ; BL = the rows' complement
    mov al, dh
    call wc_flit
    jnc .pen                            ; a dark ground: the glyph is lit
    mov al, dl
    call wc_flit
    jc .pen                             ; both lit: no theme draws that
    not bl
    xchg dl, dh
.pen:
    mov [wc_fpen], dx
    mov dl, bl
    mov di, wc_fband
    xor bp, bp                          ; BP = the character's index
.c:
    lodsb
    sub al, WC_FFIRST
    cmp al, WC_FLAST - WC_FFIRST
    jbe .g
    xor al, al                          ; outside 32..126: a space
.g:
    xor ah, ah
    shl ax, 1
    mov bx, ax
    shl ax, 1
    shl ax, 1
    add bx, ax                          ; * WC_FROWS
    add bx, wc_font
    cmp bp, [wc_fbc]
    jl .plain
    cmp bp, [wc_fbe]
    jge .plain
    jmp .bold                           ; (the bodies are past a short jump)
.plain:
%assign WC_R 0
%rep WC_FROWS
    mov al, [bx+WC_R]
    xor al, dl
    mov [di+WC_R*WC_FSTR], al
  %assign WC_R WC_R+1
%endrep
    jmp .nx
.bold:
%assign WC_R 0
%rep WC_FROWS
    mov al, [bx+WC_R]                    ; the row, and itself a pixel right
    mov ah, al
    shr ah, 1
    or al, ah
    xor al, dl
    mov [di+WC_R*WC_FSTR], al
  %assign WC_R WC_R+1
%endrep
.nx:
    inc di
    inc bp
    dec cx
    jz .blitp
    jmp .c
.blitp:
    mov ax, [wc_fpen]                   ; AL = what a set bit becomes
    call OSAPI_GFX_BLIT1_PEN
    push ds
    pop es
    mov si, wc_fband
    mov bp, WC_FSTR
    mov ax, [wc_fx]
    mov bx, [wc_fy]
    mov cx, [wc_fcnt]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, WC_FROWS
    cmp byte [wc_ful], 0xFF
    je .blit
    dec dx                              ; the last row is the underline's
.blit:
    call OSAPI_GFX_BLIT1
    cmp byte [wc_ful], 0xFF
    je .out
    mov al, [wc_ful]                    ; ...one line in its colour
    call OSAPI_SET_COLOR
    mov ax, [wc_fx]
    mov bx, ax
    add bx, cx
    dec bx
    mov dx, [wc_fy]
    add dx, WC_FROWS - 1
    call OSAPI_GFX_HLINE
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
%if WC_FROWS != 10
  %error "wc_fput multiplies a character by WC_FROWS with two shifts and an add"
%endif

; --- wc_frun - OSAPI_FONT_RUN's contract, in WireChat's face: CX = x, DX =
; the top of the 8x8 cell font_run would draw, SI = a NUL string, AL = ink,
; AH = ground. The cell is drawn from the row above DX, WC_FROWS rows, the way
; a band draws it - so a caller that placed font_run's glyph places this one.
; It is os88line.inc's OS88LINE_RUN (SPEC.md 83.6), and Send's and Search's
; captions. Preserves every register
wc_frun:
    push ax
    push bx
    push cx
    push dx
    push di
    xor di, di
    mov [wc_fbc], di                    ; nothing bold...
    mov [wc_fbe], di
    mov byte [wc_ful], 0xFF             ; ...nothing underlined
    mov bx, dx
    dec bx
    mov dx, ax
    mov ax, cx
    mov di, si
.len:
    cmp byte [di], 0
    je .put
    inc di
    jmp short .len
.put:
    mov cx, di
    sub cx, si
    call wc_fput
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_bglyph - glyph BL at cell CL, over the span it sits in: the glyph's
; own ROLE for its ink (wc_glrole), the span's ground for its ground.
; In Wired a status or a lamp is an LED (SPEC.md 113.5.2): a colour
; picture on a colour display where the span's ground is the white it was
; drawn on, else the LED's shape in the span's own ink. Preserves SI and DI.
wc_bglyph:
    push si
    push di
    mov [wc_bgid], bl
    push cx
    call wc_spanat
    call wc_spcol                       ; AL = the span's ink, AH = its ground
    pop cx
    xor ch, ch
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [wc_bx]
    mov dx, [wc_by]
    inc dx
    mov bl, [wc_bgid]
    xor bh, bh
    cmp byte [wc_eth], TH_WIRED
    jne .glyph
    cmp bl, GL_ACT
    ja .glyph
    mov bl, [wc_ledmap+bx]              ; BL = LED_*
    cmp byte [wc_tint], 0
    je .shape
    cmp ah, CWHITE                      ; the picture's corners are white
    jne .shape
    call wc_led
    jnc .out
.shape:
    mov bl, [wc_ledgl+bx]
    jmp short .draw
.glyph:
    mov al, [wc_glrole+bx]
    call wc_pen                         ; AL = the glyph's ink
.draw:
    call wc_glyphat
.out:
    pop di
    pop si
    ret

; =============================================================================
; THE LEDs (SPEC.md 113.5.2): Wired's statuses and lamps on a colour
; display, 8x8 PLANAR pictures put down by ONE OSAPI_GFX_BLITP each (SPEC.md
; 5.4.3) - a rim, a body, a shade and a glint is four colours, which no icon
; pen lays down in one call. Their corners are Wired's white ground. A
; blit refused (a window across two displays) draws the LED's shape instead.
; =============================================================================
; --- wc_led - LED BL (LED_*) at CX, DX (screen). CF = 1 refused, nothing
; drawn. Preserves every register
wc_led:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor bh, bh
    shl bx, 1                           ; * 32: four planes of eight rows
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1
    lea si, [wc_leds+bx]
    push ds
    pop es
    mov ax, cx
    mov bx, dx
    mov cx, 8
    mov dx, 8
    mov di, 8 | 0x4000                  ; the plane step; walk an armed clip
    mov bp, 1                           ; the row stride
    call OSAPI_GFX_BLITP
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

LED_OFF     equ 0
LED_GREEN   equ 1
LED_AMBER   equ 2
LED_RED     equ 3
wc_ledmap:  db LED_GREEN, LED_AMBER, LED_RED, LED_OFF   ; the statuses
            db LED_GREEN, LED_AMBER, LED_RED, LED_OFF   ; the link
            db LED_AMBER                                ; activity
wc_ledgl:   db GL_LDOFF, GL_LDLIT, GL_LDHALF, GL_LDX    ; by LED_*: one bit
%if wc_ledgl - wc_ledmap != GL_ACT + 1 || GL_ONLINE != 0 || GL_LKOK != 4
  %error "wc_ledmap is GL_ONLINE..GL_ACT"
%endif

; WC_LED8 - eight rows of eight colour digits (hex, one a pixel), emitted
; PLANE-MAJOR as OSAPI_GFX_BLITP takes them: plane 0's eight rows, then
; plane 1's... so the picture in the source IS the picture on the glass
%macro WC_LED8 8
  %assign %%p 0
  %rep 4
    %rep 8
      %assign %%b 0
      %assign %%x 0
      %rep 8
        %substr %%ch %1 %%x+1
        %if %%ch >= 'a'
          %assign %%c %%ch - 'a' + 10
        %else
          %assign %%c %%ch - '0'
        %endif
        %assign %%b %%b | (((%%c >> %%p) & 1) << (7 - %%x))
        %assign %%x %%x + 1
      %endrep
      db %%b
      %rotate 1
    %endrep
    %assign %%p %%p + 1
  %endrep
%endmacro

; f the white ground and the glint, 8 the rim, 0 black, 7 grey; the body
; and its shade: light green/green, yellow/brown, light red/red
wc_leds:
    WC_LED8 'ff8888ff', 'f800008f', '80777708', '80777008', '80770008', '80700008', 'f800008f', 'ff8888ff'
    WC_LED8 'ff8888ff', 'f8aaaa8f', '8affaa28', '8afaaa28', '8aaaa228', '8aaa2228', 'f822228f', 'ff8888ff'
    WC_LED8 'ff8888ff', 'f8eeee8f', '8effee68', '8efeee68', '8eeee668', '8eee6668', 'f866668f', 'ff8888ff'
    WC_LED8 'ff8888ff', 'f8cccc8f', '8cffcc48', '8cfccc48', '8cccc448', '8ccc4448', 'f844448f', 'ff8888ff'
%if $ - wc_leds != 4 * 32
  %error "four LEDs of four planes of eight rows"
%endif

; --- wc_glyphat - glyph BL at CX, DX (SCREEN): ink AL over ground AH --------
; One opaque 8x8 icon: its mask is the whole cell. clobbers AX, SI
wc_glyphat:
    xchg al, ah                         ; AL = the mask's ground, AH = the
    call OSAPI_ICON_PEN                 ; data's ink
    call wc_glrec
    call OSAPI_ICON_DRAW
    ret

; --- wc_glrec - SI = glyph BL's icon record, built in wc_glbuf from its eight
; rows (wc_glyphs). The header and the mask are wc_face's, written once.
; Preserves the rest.
wc_glrec:
    push ax
    push bx
    push di
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    lea si, [wc_glyphs+bx]
    mov di, wc_glbuf + 2 + 16 + 1       ; the data's high bytes
    mov bx, 8
.r:
    lodsb
    mov [di], al
    inc di
    inc di
    dec bx
    jnz .r
    mov si, wc_glbuf
    pop di
    pop bx
    pop ax
    ret

; --- wc_glinit - wc_glbuf's header and mask: a 1x8 record, the whole cell ---
wc_glinit:
    mov word [wc_glbuf], 0x0801         ; db 1, 8
    mov di, wc_glbuf + 2
    mov cx, 8
.m:
    mov word [di], 0xFF00               ; the mask: every pixel
    mov word [di+16], 0                 ; ...and the data's low bytes
    inc di
    inc di
    loop .m
    ret

; =============================================================================
; THE SLOTS
; =============================================================================
; --- wc_owe - every slot owed, the pane's top and its layout as drawn, and
; its scroll column blank (a full paint: the content was filled in the
; ground). Preserves all.
wc_owe:
    push bx
    push cx
    mov bx, wc_slw
    mov cx, WC_NWIDE
.s:
    mov byte [bx+WCX_NSP], 0
    add bx, WC_SLOTW
    loop .s
    mov bx, wc_sln
    mov cx, WC_MAXB
.n:
    mov byte [bx+WCX_NSP], 0
    add bx, WC_SLOTNN
    loop .n
    mov word [wc_dtop], 0xFFFF
    xor cx, cx                          ; the pane's glass: no band known, and
    mov [wc_grows], cx                  ; all of it ground
    mov [wc_gused], cx
    mov bx, wc_sbg
    mov cx, WC_MAXB
.g:
    mov byte [bx], GL_BLANK
    inc bx
    loop .g
    pop cx
    pop bx
    ret

; --- wc_slot - BX = slot AX (a WCSL_* base plus a band), or 0 for none
; A band past WC_MAXB of its kind is no slot: drawn whole, remembered
; nowhere. Preserves the rest.
wc_slot:
    xor bx, bx
    cmp ax, WC_NSLOT
    jae .out
    push ax
    push dx
    cmp ax, WC_NWIDE
    jae .nar
    mov bx, WC_SLOTW
    mul bx
    add ax, wc_slw
    jmp short .got
.nar:
    sub ax, WC_NWIDE
    mov bx, WC_SLOTNN
    mul bx
    add ax, wc_sln
.got:
    mov bx, ax
    pop dx
    pop ax
.out:
    ret

; --- wc_pshift - the pane moved AX bands (signed: + up) on the glass: move
; its slots with it, and owe the ones it vacated. Every slot moves, laid out
; or not: a band past the last one laid matches nothing in wc_gby, so its
; slot is owed before it is drawn. Preserves all.
wc_pshift:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov cx, WC_MAXB
    or ax, ax
    js .down
    sub cx, ax                          ; the bands that stay: CX of them,
    jbe .owe                            ; from band AX to band 0
    push ax
    push cx
    mov dx, WC_SLOTW
    mul dx
    mov si, ax
    add si, wc_slw + WCSL_PANE * WC_SLOTW
    mov di, wc_slw + WCSL_PANE * WC_SLOTW
    mov ax, WC_SLOTW
    mul cx
    mov cx, ax
    cld
    rep movsb
    pop cx                              ; ...and the AX after them owed
    pop ax
    mov bx, cx
    add bx, WCSL_PANE
    mov cx, ax
    jmp short .tail
.down:
    neg ax
    sub cx, ax                          ; CX stay, from band 0 to band AX
    jbe .owe
    push ax
    push cx
    mov ax, cx
    dec ax
    mov dx, WC_SLOTW
    mul dx
    add ax, WC_SLOTW - 1
    mov si, ax
    add si, wc_slw + WCSL_PANE * WC_SLOTW
    pop cx
    pop dx                              ; DX = the shift
    push dx
    push cx
    mov ax, dx
    mov dx, WC_SLOTW
    mul dx
    mov di, si
    add di, ax
    mov ax, WC_SLOTW
    mul cx
    mov cx, ax
    std
    rep movsb
    cld
    pop cx
    pop ax
    mov bx, WCSL_PANE                   ; the first AX owed
    mov cx, ax
    jmp short .tail
.owe:
    mov bx, WCSL_PANE                   ; nothing stays: owe them all
    mov cx, WC_MAXB
.tail:
    jcxz .out
.o:
    mov ax, bx
    push bx
    call wc_slot
    or bx, bx
    jz .skip
    mov byte [bx+WCX_NSP], 0
.skip:
    pop bx
    inc bx
    loop .o
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; =============================================================================
; THE BUTTONS (SPEC.md 13.8.10, 113.5.2): every one is the package's, on every
; theme (OS88UI_OWN), so the library's press, drag-off and release reach the
; theme's picture through os88ui_btn. Each painter: AX = the button, BX = its
; rect (screen), DI = its resolved flags; the lock held, the clip armed.
; Every register preserved.
; =============================================================================
wc_btown:       dw wc_stbtn, wc_srbtn, wc_sdbtn

; --- wc_stbtn - STATUS: the header's right end, which IS the button. Pressed,
; its glyph, word and nick are in the highlight; the header band draws it
; through its slot, so a press costs what changed
wc_stbtn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor al, al
    test di, OS88UI_DOWN
    jz .up
    inc ax
.up:
    mov [wc_hdown], al
    call wc_geom
    jc .out
    call wc_phdr
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_srbtn - SEARCH: a box in the accent (pressed, in the text colour), a
; lens, a rule and 'Search...' dim. The box's inside is the ground the full
; paint laid, and nothing here draws over a pixel twice
wc_srbtn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wc_rcopy
    mov al, R_ACC
    test di, OS88UI_DOWN
    jz .f
    mov al, R_FG
.f:
    call wc_penc
    mov ax, [wc_bv]
    mov bx, [wc_bv+2]
    mov cx, [wc_bv+4]
    mov dx, [wc_bv+6]
    call OSAPI_GFX_FRAME
    mov al, R_ACC                       ; the rule between the lens and the
    call wc_penc                        ; words
    mov ax, [wc_bv]
    add ax, 15
    inc bx
    dec dx
    call OSAPI_GFX_VLINE
    mov al, R_BG
    call wc_pen
    mov ah, al
    mov al, R_ACC
    call wc_pen                         ; AL = the lens, AH = the ground
    mov cx, [wc_bv]
    add cx, 4
    mov dx, [wc_bv+2]
    add dx, 3
    mov bl, GL_LENS
    push dx
    call wc_glyphat
    pop dx
    mov al, R_BG
    call wc_pen
    mov ah, al
    mov al, R_DIM
    call wc_pen
    mov cx, [wc_bv]
    add cx, 20
    mov si, wc_b_search
    call wc_frun
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_sdbtn - SEND: a solid block in the highlight, the caption a run as
; wide as the block (so it is the block's middle WC_FROWS rows), the rows
; above and below it a fill each - every pixel once. Pressed: the text colour. Greyed:
; the ground, a dim caption and a dim frame
wc_sdbtn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wc_rcopy
    mov ax, R_HIFG | (R_HI << 8)        ; AL = the caption's role, AH = the
    test di, OS88UI_DIS                 ; block's
    jz .live
    mov ax, R_DIM | (R_BG << 8)
    jmp short .go
.live:
    test di, OS88UI_DOWN
    jz .go
    mov ax, R_BG | (R_FG << 8)
.go:
    push di
    call wc_pen
    xchg al, ah
    call wc_pen
    xchg al, ah                         ; AL = the caption, AH = the block
    push ax
    mov al, ah
    call OSAPI_SET_COLOR
    mov ax, [wc_bv]                     ; above the caption
    mov bx, [wc_bv+2]
    mov cx, [wc_bv+4]
    mov dx, bx
    add dx, (WC_BOXH - 8) / 2 - 2       ; to the row over the caption's cell
    call OSAPI_GFX_FILL
    add dx, WC_FROWS + 1                ; below it
    mov bx, dx
    mov dx, [wc_bv+6]
    call OSAPI_GFX_FILL
    pop ax
    mov cx, [wc_bv]
    mov dx, [wc_bv+2]
    add dx, (WC_BOXH - 8) / 2           ; font_run's glyph row: the cell is
    mov si, wc_b_sendc                  ; the row above it and WC_FROWS deep
    call wc_frun
    pop di
    test di, OS88UI_DIS
    jz .out
    mov al, R_DIM
    call wc_penc
    mov ax, [wc_bv]
    mov bx, [wc_bv+2]
    mov cx, [wc_bv+4]
    mov dx, [wc_bv+6]
    call OSAPI_GFX_FRAME
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_rcopy - the rect at BX into wc_bv. Preserves all ------------------
wc_rcopy:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov si, bx
    mov di, wc_bv
    movsw
    movsw
    movsw
    movsw
    pop es
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_face - what this paint draws (SPEC.md 113.5.2), asked of the display the
;           window is ON at each full paint: its depth decides whether the
;           theme chosen can be drawn. Every slot owed - the content is about
;           to be filled - and the menu's greying the display's.
;           clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_face:
    mov bx, [wc_win]
    call OSAPI_WM_DISPLAY               ; BX the height, DH the depth
    mov ax, 1                           ; the air under a message: about three
    cmp bx, 300                         ; square pixels' worth - one of CGA's
    jb .gap                             ; 2.4:1, two of Hercules' and EGA's,
    inc ax                              ; three of VGA's
    cmp bx, 400
    jb .gap
    inc ax
.gap:
    mov [wc_gap], ax
    xor al, al
    cmp dh, 1
    jbe .m
    inc ax
.m:
    mov [wc_col], al
    ; --- the theme, and its pens ------------------------------------------
    xor ax, ax
    mov [wc_tint], al
    mov al, [wc_thm]
    cmp al, TH_N
    jb .h
    mov al, TH_DEFAULT
    mov [wc_thm], al
.h:
    cmp al, TH_NMONO                    ; one bit draws the mono one chosen,
    jb .k                               ; and Wired for a colour one
    cmp ah, [wc_col]
    jne .t
    mov al, TH_WIRED
.k:
    cmp ah, [wc_col]
    je .t
    or al, al                           ; Wired on colour: its colour row
    jnz .t
    mov byte [wc_tint], 1
    mov [wc_eth], al
    mov al, TH_WIREDC
    jmp short .p
.t:
    mov [wc_eth], al
.p:
    mov cl, 4                           ; * WC_NPEN
    shl ax, cl
    add ax, wc_themes
    mov si, ax
    mov di, wc_pens
    mov cx, WC_NPEN
.c:
    lodsb
    mov [di], al
    inc di
    loop .c
    mov al, [wc_pens+R_FG]              ; the field: the text on the ground,
    mov ah, [wc_pens+R_BG]              ; its own frame put out of sight (the
    mov [os88line_pens], ax             ; input row's box is the frame)
    mov [os88line_pens+2], ah
    call wc_glinit
    call wc_owe
    jmp wc_menulabels
%if WC_NPEN != 16
  %error "wc_face multiplies a theme by 16 with a shift"
%endif
