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
; THE FACES. [wc_col] is 1 on a colour display (wcview.inc's wc_paint asks
; OSAPI_WM_DISPLAY at each full paint) and selects the style table's second
; pair and the glyphs' colours. The MONO face never adds a span, so every
; band is the one span of its style - the call sequence wc_band made before
; this file existed, to the call. Its pictures are the contract.
; =============================================================================

; the styles: (ink, ground) on the mono face, then on the colour face. Every
; style but INV is black on white in mono, which is what keeps that face's
; pictures what they were
wc_stytab:
    db CBLACK, CWHITE,  CBLACK, CWHITE      ; WS_NORM
    db CWHITE, CBLACK,  CWHITE, CBLUE       ; WS_INV: a selection, the view
    db CBLACK, CWHITE,  CBLACK, CLGRAY      ; WS_STRIP: header, titles, bar
    db CBLACK, CWHITE,  CBLACK, CYELLOW     ; WS_MENT: a message naming us
    db CBLACK, CWHITE,  CBLUE, CWHITE       ; WS_NOTE: the notice line
    db CBLACK, CWHITE,  CDGRAY, CWHITE      ; WS_DIM: topic, a day divider
    db CBLACK, CWHITE,  CRED, CWHITE        ; WS_ALERT: new messages
WS_NORM     equ 0
WS_INV      equ 1
WS_STRIP    equ 2
WS_MENT     equ 3
WS_NOTE     equ 4
WS_DIM      equ 5
WS_ALERT    equ 6

; the glyphs' colour-face ink: on a light ground, then on a dark one (blue or
; black). The mono face takes the span's own ink, as wc_glyph always did
wc_glink:
    db CBLACK, CWHITE                       ; GL_PERSON
    db CGREEN, CLGREEN                      ; GL_ONLINE
    db CBROWN, CYELLOW                      ; GL_AWAY
    db CRED, CLRED                          ; GL_BUSY
    db CDGRAY, CLGRAY                       ; GL_GONE
    db CBLACK, CWHITE                       ; GL_PLUG
    db CBLACK, CWHITE                       ; GL_CUT

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

; --- wc_basepair - AL = ink, AH = ground of the meta's style, on this face --
wc_basepair:
    push bx
    mov bl, [wc_meta+WCX_STY]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    cmp byte [wc_col], 0
    je .m
    inc bx
    inc bx
.m:
    mov ax, [wc_stytab+bx]
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

; --- wc_bairl - the two rows of air over cells [SI, DI) in colour AL --------
; Preserves BX, SI, DI.
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

; --- wc_bglyph - glyph BL at cell CL, over the span it sits in --------------
; Preserves SI and DI.
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
    cmp bl, GL_LED
    jae .led
    cmp byte [wc_col], 0
    je .pen
    ; the colour face: the glyph's own ink, for a light ground or a dark one
    xor bh, bh
    shl bx, 1
    cmp ah, CBLUE
    je .dark
    cmp ah, CBLACK
    jne .light
.dark:
    inc bx
.light:
    mov al, [wc_glink+bx]
.pen:
    xchg al, ah                         ; AL = the mask's ground, AH = the
    call OSAPI_ICON_PEN                 ; data's ink
    mov bl, [wc_bgid]
    call wc_glrec
    call OSAPI_ICON_DRAW
    jmp short .out
.led:
    call wc_led
.out:
    pop di
    pop si
    ret

; --- wc_glrec - SI = glyph BL's record (wc_glyphs: 34 bytes each) ----------
wc_glrec:
    push bx
    xor bh, bh
    mov si, bx
    shl bx, 1
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    add si, bx
    add si, wc_glyphs
    pop bx
    ret

; =============================================================================
; THE LEDs (SPEC.md 113.5.2): the status bar's two lamps on the colour face,
; 8x8 PLANAR pictures put down by ONE OSAPI_GFX_BLITP each (SPEC.md 5.4.3) - a
; rim, a body, a shade and a glint is four colours, which no icon pen can lay
; down in one call. Drawn on the strip's grey, which is their ground. A blit
; refused (a window across two displays, SPEC.md 5.4.3) puts down the status
; glyph in the lamp's colour instead: a lamp that is still a lamp.
; =============================================================================
; --- wc_led - lamp BL (GL_LED + LED_*) at CX, DX (screen) ---------------------
wc_led:
    push bp
    push es
    sub bl, GL_LED
    xor bh, bh
    mov [wc_bgid], bl
    mov si, bx                          ; * 32: four planes of eight rows
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    shl si, 1
    add si, wc_leds
    push ds
    pop es
    mov ax, cx
    mov bx, dx
    mov cx, 8
    mov dx, 8
    mov di, 8 | 0x4000                  ; the plane step; walk an armed clip
    mov bp, 1                           ; the row stride
    push ax
    push bx
    call OSAPI_GFX_BLITP
    pop dx
    pop cx
    jnc .out
    ; refused: the online glyph in the lamp's colour
    mov bl, [wc_bgid]
    xor bh, bh
    mov ah, [wc_ledink+bx]
    mov al, CLGRAY
    call OSAPI_ICON_PEN
    mov bl, GL_ONLINE
    call wc_glrec
    call OSAPI_ICON_DRAW
.out:
    pop es
    pop bp
    ret

LED_OFF     equ 0
LED_GREEN   equ 1
LED_AMBER   equ 2
LED_RED     equ 3
wc_ledink:  db CDGRAY, CLGREEN, CYELLOW, CLRED

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

; 7 the strip's grey, 8 the rim, 0 black, f white; the body and its shade
wc_leds:
    WC_LED8 '77888877', '78000087', '80777708', '80777008', '80770008', '80700008', '78000087', '77888877'
    WC_LED8 '77888877', '78aaaa87', '8affaa28', '8afaaa28', '8aaaa228', '8aaa2228', '78222287', '77888877'
    WC_LED8 '77888877', '78eeee87', '8effee68', '8efeee68', '8eeee668', '8eee6668', '78666687', '77888877'
    WC_LED8 '77888877', '78cccc87', '8cffcc48', '8cfccc48', '8cccc448', '8ccc4448', '78444487', '77888877'

; =============================================================================
; THE SLOTS
; =============================================================================
; --- wc_owe - every slot owed, and the scroll bar's numbers, and the pane's
; top as drawn (a W_PAINT: the content was white-filled). Preserves all.
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
    mov cx, 2 * WC_MAXB
.n:
    mov byte [bx+WCX_NSP], 0
    add bx, WC_SLOTNN
    loop .n
    mov byte [wc_sbk], 0
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
; THE COLOUR FACE'S BUTTONS (SPEC.md 13.8.10, 113.5.2) - MIDIRack's bevel
; (apps/midirack/mrui.inc, mru_bevel): a black frame, a white top-left and a
; grey bottom-right, the caption black on light grey; pressed, one grey line
; in at the top and the left and the caption a pixel down and right; greyed,
; the caption grey. The caption is opaque on the grey with the grey RINGED
; round it in four fills, so no pixel is written twice. The record's labels
; are wc_btown on the colour face (wcview.inc's wc_face), every entry this
; proc; the captions stay wc_btlbl's.
; in:  AX = the button, BX = its rect (screen), DI = its resolved flags; the
;      lock held. out: every register preserved
; =============================================================================
wc_bevel:
    push ax
    push bx
    push cx
    push dx
    push si
    shl ax, 1
    mov si, ax
    mov si, [wc_btlbl+si]               ; SI = the caption
    mov ax, [bx]
    mov [wc_bv], ax
    mov ax, [bx+2]
    mov [wc_bv+2], ax
    mov ax, [bx+4]
    mov [wc_bv+4], ax
    mov ax, [bx+6]
    mov [wc_bv+6], ax
    xor al, al                          ; the frame
    call OSAPI_SET_COLOR
    mov ax, [wc_bv]
    mov bx, [wc_bv+2]
    mov cx, [wc_bv+4]
    mov dx, [wc_bv+6]
    call OSAPI_GFX_FRAME
    inc ax                              ; the face, before the bevel takes its
    inc bx                              ; lines out of it
    dec cx
    dec dx
    mov [wc_bf], ax
    mov [wc_bf+2], bx
    mov [wc_bf+4], cx
    mov [wc_bf+6], dx
    xor cx, cx                          ; CX = the caption's shift
    test di, OS88UI_DIS
    jnz .up
    test di, OS88UI_DOWN
    jz .up
    mov al, CDGRAY                      ; PRESSED: one grey line in at the top
    mov ah, CDGRAY                      ; and the left
    call wc_bvtl
    inc cx
    jmp short .face
.up:
    mov al, CWHITE                      ; UP: light top-left...
    mov ah, CWHITE
    call wc_bvtl
    mov al, CDGRAY                      ; ...grey bottom-right
    call wc_bvbr
.face:
    push cx
    push si
    call OSAPI_FONT_WIDTH               ; AX = the caption's width
    pop si
    pop cx
    mov [wc_btw], ax
    mov dx, [wc_bv+4]                   ; centred in the button, + the shift
    sub dx, [wc_bv]
    inc dx
    sub dx, ax
    sar dx, 1
    add dx, [wc_bv]
    add dx, cx
    cmp dx, [wc_bf]
    jge .x1
    mov dx, [wc_bf]
.x1:
    mov [wc_btx], dx
    mov dx, [wc_bv+6]
    sub dx, [wc_bv+2]
    inc dx
    sub dx, 8
    sar dx, 1
    add dx, [wc_bv+2]
    add dx, cx
    mov [wc_bty], dx
    mov byte [wc_ink], CLGRAY           ; the ring: above, below, left, right
    mov ax, [wc_bf]
    mov bx, [wc_bf+2]
    mov cx, [wc_bf+4]
    mov dx, [wc_bty]
    dec dx
    call wc_afill
    mov bx, [wc_bty]
    add bx, 8
    mov dx, [wc_bf+6]
    call wc_afill
    mov bx, [wc_bty]
    mov dx, bx
    add dx, 7
    mov cx, [wc_btx]
    dec cx
    call wc_afill
    mov ax, [wc_btx]
    add ax, [wc_btw]
    mov cx, [wc_bf+4]
    call wc_afill
    mov cx, [wc_btx]                    ; the caption
    mov dx, [wc_bty]
    mov ax, CBLACK | (CLGRAY << 8)
    test di, OS88UI_DIS
    jz .ink
    mov al, CDGRAY
.ink:
    call OSAPI_FONT_RUN
    xor al, al
    call OSAPI_SET_COLOR
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wc_bvtl - the face's top row in AL and its left column in AH, taken out of
; [wc_bf]
wc_bvtl:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov [wc_ink], al
    mov ax, [wc_bf]
    mov bx, [wc_bf+2]
    mov cx, [wc_bf+4]
    mov dx, bx
    call wc_afill
    pop ax
    mov [wc_ink], ah
    mov ax, [wc_bf]
    mov bx, [wc_bf+2]
    inc bx
    mov cx, ax
    mov dx, [wc_bf+6]
    call wc_afill
    inc word [wc_bf]
    inc word [wc_bf+2]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wc_bvbr - the face's bottom row and right column in AL, taken out of it
wc_bvbr:
    push ax
    push bx
    push cx
    push dx
    mov [wc_ink], al
    mov ax, [wc_bf]
    mov bx, [wc_bf+6]
    mov cx, [wc_bf+4]
    mov dx, bx
    call wc_afill
    mov ax, cx
    mov bx, [wc_bf+2]
    mov dx, [wc_bf+6]
    dec dx
    call wc_afill
    dec word [wc_bf+4]
    dec word [wc_bf+6]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; wc_afill - AX..DX (SCREEN) in [wc_ink], or nothing when it is empty
wc_afill:
    cmp cx, ax
    jl .r
    cmp dx, bx
    jl .r
    push ax
    mov al, [wc_ink]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.r:
    ret

; the colour face's labels array: every button this painter (OS88UI_OWN)
wc_btown:       dw wc_bevel, wc_bevel, wc_bevel, wc_bevel, wc_bevel

; -----------------------------------------------------------------------------
; wc_face - which face this paint draws (SPEC.md 113.5.2), asked of the
;           display the window is ON at each full paint: 4 bits a pixel is the
;           colour face, anything less the mono one. Every slot owed - the
;           kernel white-filled the content - and the buttons' labels the
;           face's. clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
wc_face:
    mov bx, [wc_win]
    call OSAPI_WM_DISPLAY               ; DH = bits per pixel
    xor al, al
    cmp dh, 1
    jbe .m
    inc ax
.m:
    mov [wc_col], al
    call wc_owe
    mov ax, wc_btlbl
    cmp byte [wc_col], 0
    je .l
    mov ax, wc_btown
.l:
    mov [wc_btrec + OS88UI_BT_LABELS], ax
    ret
