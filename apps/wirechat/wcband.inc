; =============================================================================
; os8088 - apps/wirechat/wcband.inc
;
; THE BAND, AND WHAT IS ON THE GLASS (SPEC.md 113.5.1, 113.5.2).
;
; Every row WireChat draws is a BAND: a composition of cells (wc_sbuf, built by
; the wc_sb* helpers in wcview.inc) and a META block (wc_meta) that says how
; to draw it - a STYLE, up to WC_NSPAN colour SPANS, up to two 8x8 GLYPHS and
; one BOLD run. wc_bandc draws the pair as one band: for each span the two
; rows of air in its ground and one opaque OSAPI_FONT_RUN, then the glyphs,
; then the overstrike - so a band repaints every pixel it owns exactly once
; (PERFORMANCE.md rules 1 and 2), as wcview.inc's wc_band always did.
;
; THE SLOTS. A band drawn with a SLOT (BX) is drawn THROUGH it: the slot
; holds the meta and the cells as they are on the glass, so a band whose
; meta and cells are what the slot holds draws NOTHING, and one whose meta
; matches and whose cells do not draws ONE font_run of the run between the
; first cell that differs and the last (split at the spans it crosses, the
; overstrike and the glyphs inside it put back) - MIDIRack's field slots
; (SPEC.md 105.9.4), for every row of the window. A slot whose meta moved is
; drawn whole. A W_PAINT owes every slot (wc_owe): the kernel white-filled
; the content before calling it, so nothing the slots say is true any more.
;
; THE THEMES (SPEC.md 113.5.2). Every colour is a ROLE (R_*): a theme is one
; row of wc_themes, copied into wc_pens by wc_face at each full paint, and a
; style, a span, a glyph or a button asks wc_pens for its role. A theme change
; is a full paint, which owes every slot, so no slot compares across two.
; Monochrome is black and white alone: the same picture on a VGA as on a
; Hercules or a CGA, where every other colour rounds (SPEC.md 39.4).
; =============================================================================

wc_themes:      ; R_BG FG DIM ACC  HI HIFG  OK WARN BAD BADFG  MENTG MENTF  NICK x4
    db CBLACK, CWHITE, CWHITE, CWHITE,  CWHITE, CBLACK                ; mono
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
%if $ - wc_themes != TH_N * WC_NPEN
  %error "wc_themes is TH_N rows of WC_NPEN roles"
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

; --- wc_mbold - the overstrike: CL cells from cell CH. Preserves all --------
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
    xor cx, cx                          ; drawn alone (wc_bglyphs)?
.mc:
    cmp cx, WCX_G0 + 1
    je .mn
    cmp cx, WCX_G1 + 1
    je .mn
    mov al, [si]
    cmp al, [di]
    jne .full
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
    cmp dx, [wc_blast]
    jbe .out
.draw:
    mov bl, al
    jmp wc_bglyph
.out:
    ret

; --- wc_bfull - the air, every span's run, the glyphs, the overstrike -------
; The air is one pair of rows per run of spans that share a ground: the colour
; face's message header is three spans on one white, and three pairs of rows
; would be four calls it does not need.
wc_bfull:
    mov bx, wc_meta + WCX_SP
    mov al, [wc_meta+WCX_NSP]
    mov [wc_bn], al
    mov word [wc_bair], 0xFFFF
.ap:
    call wc_spse                        ; SI, DI = [start, end)
    cmp si, di
    jae .anext
    call wc_spcol                       ; AH = the ground
    cmp word [wc_bair], 0xFFFF
    je .astart
    cmp ah, [wc_bgnd]
    je .acont
    push si                             ; another ground: the run so far
    push ax
    mov di, [wc_bend]
    mov si, [wc_bair]
    mov al, [wc_bgnd]
    call wc_bairl
    pop ax
    pop si
.astart:
    mov [wc_bair], si
    mov [wc_bgnd], ah
.acont:
    call wc_spse
    mov [wc_bend], di
.anext:
    add bx, 3
    dec byte [wc_bn]
    jnz .ap
    cmp word [wc_bair], 0xFFFF
    je .runs
    mov si, [wc_bair]
    mov di, [wc_bend]
    mov al, [wc_bgnd]
    call wc_bairl
.runs:
    mov bx, wc_meta + WCX_SP
    mov al, [wc_meta+WCX_NSP]
    mov [wc_bn], al
.sp:
    call wc_spse
    cmp si, di
    jae .next
    call wc_spcol                       ; AL = ink, AH = ground
    call wc_brun
.next:
    add bx, 3
    dec byte [wc_bn]
    jnz .sp
    xor si, si                          ; the glyphs and the overstrike: the
    mov di, 0x7FFF                      ; whole band
    jmp wc_bdeco

; --- wc_bpart - the run [wc_bfirst, wc_blast] again, span by span -----------
wc_bpart:
    mov si, [wc_bfirst]
    mov di, [wc_blast]
    inc di                              ; [SI, DI)
    mov cl, [wc_meta+WCX_BL]            ; an overstrike the run touches is
    or cl, cl                           ; erased by it: take all of it in
    jz .runs
    xor ch, ch
    xor ax, ax
    mov al, [wc_meta+WCX_BC]
    add cx, ax                          ; [AX, CX): the bold cells
    cmp ax, di
    jae .runs
    cmp cx, si
    jbe .runs
    cmp ax, si
    jae .lo
    mov si, ax
.lo:
    cmp cx, di
    jbe .runs
    mov di, cx
.runs:
    mov [wc_bfirst], si
    mov [wc_blast], di
    mov bx, wc_meta + WCX_SP
    mov al, [wc_meta+WCX_NSP]
    mov [wc_bn], al
.sp:
    mov cl, [bx]
    xor ch, ch
    mov si, cx                          ; the span's [start, end)...
    call wc_spend
    xor ch, ch
    mov di, cx
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
    call wc_spcol
    call wc_brun
.next:
    add bx, 3
    dec byte [wc_bn]
    jnz .sp
    mov si, [wc_bfirst]
    mov di, [wc_blast]
    ; fall through: the decoration inside [SI, DI)

; --- wc_bdeco - the glyphs whose cell is in [SI, DI), and the overstrike when
; it starts there
wc_bdeco:
    mov cl, [wc_meta+WCX_G0]
    mov bl, [wc_meta+WCX_G0+1]
    call .g
    mov cl, [wc_meta+WCX_G1]
    mov bl, [wc_meta+WCX_G1+1]
    call .g
    mov cl, [wc_meta+WCX_BL]
    or cl, cl
    jz .out
    xor ax, ax
    mov al, [wc_meta+WCX_BC]
    cmp ax, si
    jb .out
    cmp ax, di
    jae .out
    jmp wc_bbold
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

; --- wc_bairl - the two rows of air over cells [SI, DI) in colour AL - the
; lower one in the band's UNDERLINE when it has one (a section title's rule,
; SPEC.md 113.5). Preserves BX, SI, DI.
wc_bairl:
    push bx
    call OSAPI_SET_COLOR
    mov ax, si
    mov bx, di
    mov cl, 3
    shl ax, cl
    shl bx, cl
    add ax, [wc_bx]
    add bx, [wc_bx]
    dec bx
    mov dx, [wc_by]
    call OSAPI_GFX_HLINE
    push ax
    mov al, [wc_meta+WCX_UL]
    cmp al, 0xFF
    je .ul
    call OSAPI_SET_COLOR
.ul:
    pop ax
    add dx, WC_LH - 1
    call OSAPI_GFX_HLINE
    pop bx
    ret

; --- wc_brun - cells [SI, DI) in AL = ink, AH = ground: one font_run --------
; Preserves every register.
wc_brun:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, [wc_sbuf+di]                ; cut the composition at DI
    mov byte [wc_sbuf+di], 0
    push bx
    push ax
    mov al, CBLACK                      ; a button drawn just before may have
    call OSAPI_SET_COLOR                ; left the pen DISABLED (timer.asm)
    pop ax
    mov cx, si
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [wc_bx]
    mov dx, [wc_by]
    inc dx                              ; the glyph row
    add si, wc_sbuf
    call OSAPI_FONT_RUN
    pop bx
    mov [wc_sbuf+di], bl
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_bbold - the overstrike: the bold cells again, transparent, at x+1 ----
; SPEC.md 6.6.2 case 6 (tests/textsites.txt). Preserves SI and DI.
wc_bbold:
    push si
    push di
    xor si, si
    mov cx, si
    mov cl, [wc_meta+WCX_BC]
    mov si, cx
    add cl, [wc_meta+WCX_BL]
    cmp cl, [wc_meta+WCX_LEN]
    jbe .end
    mov cl, [wc_meta+WCX_LEN]
.end:
    mov di, cx
    cmp si, di
    jae .out
    mov bl, [wc_sbuf+di]
    mov byte [wc_sbuf+di], 0
    push bx
    push si
    mov cx, si
    call wc_spanat
    call wc_spcol                       ; the span's ink
    call OSAPI_SET_COLOR
    pop si
    mov cx, si
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [wc_bx]
    inc cx
    mov dx, [wc_by]
    inc dx
    add si, wc_sbuf
    call OSAPI_FONT_STR_XPARENT         ; the overstrike: it ADDS to the glyphs
    pop bx                              ; already down (tests/textsites.txt)
    mov [wc_sbuf+di], bl
.out:
    pop di
    pop si
    ret

; --- wc_bglyph - glyph BL at cell CL, over the span it sits in: the glyph's
; own ROLE for its ink (wc_glrole), the span's ground for its ground.
; Preserves SI and DI.
wc_bglyph:
    push si
    push di
    mov [wc_bgid], bl
    push cx
    call wc_spanat
    call wc_spcol                       ; AH = the span's ground
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
    mov al, [wc_glrole+bx]
    call wc_pen                         ; AL = the glyph's ink
    call wc_glyphat
    pop di
    pop si
    ret

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
; --- wc_owe - every slot owed, and the pane's top as drawn (a full paint:
; the content was filled in the ground). Preserves all.
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
; its slots with it, and owe the ones it vacated. Preserves all.
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
    mov cx, [wc_rows]
    cmp cx, WC_MAXB
    jbe .r
    mov cx, WC_MAXB
.r:
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
    mov cx, [wc_rows]
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
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wc_sdbtn - SEND: a solid block in the highlight, the caption a run as
; wide as the block (so it is the block's middle rows), the rows above and
; below it a fill each - every pixel once. Pressed: the text colour. Greyed:
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
    add dx, (WC_BOXH - 8) / 2 - 1
    call OSAPI_GFX_FILL
    add dx, 9                           ; below it
    mov bx, dx
    mov dx, [wc_bv+6]
    call OSAPI_GFX_FILL
    pop ax
    mov cx, [wc_bv]
    mov dx, [wc_bv+2]
    add dx, (WC_BOXH - 8) / 2
    mov si, wc_b_sendc
    call OSAPI_FONT_RUN
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
;           theme chosen can be drawn, and its kind and size are the header's
;           words. Every slot owed - the content is about to be filled - and
;           the menu's greying the display's. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_face:
    mov bx, [wc_win]
    call OSAPI_WM_DISPLAY               ; AX x BX, DL the kind, DH the depth
    push dx
    push bx
    push ax
    xor al, al
    cmp dh, 1
    jbe .m
    inc ax
.m:
    mov [wc_col], al
    ; --- the header's words: 'VGA 640x480 / 16 COLORS' ---------------------
    mov di, wc_adapt
    mov bx, wc_adapt + WC_ADAPTN - 2
    mov si, wc_s_vga
    cmp dl, VID_HERC
    jne .k1
    mov si, wc_s_herc
.k1:
    cmp dl, VID_CGA
    jne .k2
    mov si, wc_s_cga
.k2:
    cmp dl, VID_EGA
    jne .k3
    mov si, wc_s_ega
.k3:
    call wc_put
    pop ax
    xor dx, dx
    call wc_putd32
    mov al, 'x'
    call wc_putc
    pop ax
    xor dx, dx
    call wc_putd32
    pop dx
    mov si, wc_s_mono
    cmp dh, 1
    jbe .d
    mov si, wc_s_16c
.d:
    call wc_put
    mov byte [di], 0
    ; --- the theme, and its pens ------------------------------------------
    xor ax, ax
    cmp [wc_col], al
    je .t
    mov al, [wc_thm]
    cmp al, TH_N
    jb .t
    mov al, TH_DEFAULT
    mov [wc_thm], al
.t:
    mov [wc_eth], al
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
