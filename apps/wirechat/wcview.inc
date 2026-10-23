; =============================================================================
; os8088 - apps/wirechat/wcview.inc
;
; THE LAYOUT AND EVERY PAINTER (SPEC.md 113.5): a BBS terminal - a header, a
; row of room tabs, the title and its search box, the message pane with its
; scroll column, the ONLINE / FRIENDS / DIRECT pane on the right, the input
; box with Send, and the status bar with its F-key legend.
;
; wc_geom is the ONE place the layout is computed, and every painter and every
; hit test calls it first - The Wire's wr_geom, for its reason: a window moves
; and resizes, and a painter and a hit test that hold two opinions about where
; a row is are a click that lands on the wrong room. What a painter COMPOSES
; and a press must find - where the tabs and the F-key legend fell - the
; painter records (wc_tabc, wc_fkc), so the hit test reads what is on the
; glass.
;
; EVERY ROW IS A BAND: WC_LH = 10 rows, one cell of WireChat's own 8 x 10
; face (SPEC.md 113.5.3). wcband.inc's wc_bandc letters it a colour pair at a
; time, each run composed - air, glyphs and bold - and put down by one
; OSAPI_GFX_BLIT1, padded to the region's width, so a band repaints every
; pixel it owns exactly once (PERFORMANCE.md rules 1 and 2).
;
; THE DIRTY BITS (WCD_*). A state change ORs the bits for the regions it
; changed; wc_flush paints exactly those, under a lock its caller holds.
; W_PAINT is the one painter that paints everything: the ground once, the
; rules, then every band (the window is OSAPI_WM_OWNBG, SPEC.md 113.5).
; =============================================================================

; -----------------------------------------------------------------------------
; wc_geom - where everything is, this pass (SPEC.md 113.5)
; out: CF = 1 not visible and nothing stored; else every wc_* geometry word,
;      the button rects and the field's rect. Preserves every register.
; -----------------------------------------------------------------------------
wc_geom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [wc_win]
    call OSAPI_WM_GEOM                  ; CX = content width, DX = height
    jc .out
    mov [wc_cw], cx
    mov [wc_ch], dx
    call OSAPI_WM_CONTENT               ; AX = left, DX = top
    mov [wc_ox], ax
    mov [wc_oy], dx
    ; --- the columns: the center, and the right pane past its rule ----------
    mov cx, [wc_cw]
    xor ax, ax
    cmp cx, WC_WRIGHT
    jb .nor
    mov ax, cx
    sub ax, WC_RPW
    and ax, ~7
.nor:
    mov [wc_rx1], ax
    mov word [wc_mx1], 0
    or ax, ax
    jnz .m2
    mov ax, cx
.m2:
    dec ax
    mov [wc_mx2], ax
    ; --- the rows, top down: the header's one or two bands, the tabs --------
    mov word [wc_hy2], 0
    mov ax, 1 + WC_LH                   ; the header's rule, under one band...
    cmp word [wc_ch], WC_HTALL
    jb .h1
    mov word [wc_hy2], 1 + WC_LH        ; ...or under two
    add ax, WC_LH
.h1:
    mov [wc_hr], ax
    inc ax
    inc ax
    mov [wc_tby], ax                    ; the tabs, and their rule under them
    add ax, WC_LH + 2
    mov [wc_mid], ax                    ; the first row under that rule
    add ax, 3
    mov [wc_ty], ax                     ; the title, beside the search box
    add ax, 15
    mov [wc_py1], ax                    ; the pane, under the title's rule
    ; --- ...and bottom up: the status bar, the input row, the notice -------
    mov ax, [wc_ch]
    sub ax, WC_STH
    mov [wc_sy], ax
    sub ax, WC_INH
    mov [wc_iy], ax
    sub ax, WC_LH + 1
    mov [wc_ny], ax
    sub ax, [wc_py1]                    ; the pane's height...
    jge .ph
    xor ax, ax
.ph:
    mov [wc_ph], ax
    xor dx, dx                          ; ...and its scroll column's cells,
    mov cx, WC_LH                       ; on a grid of their own: the bands'
    div cx                              ; are wc_play's, and move with the
    cmp ax, 1                           ; messages' gaps
    jge .rw
    mov ax, 1
.rw:
    cmp ax, WC_MAXB
    jbe .rw2
    mov ax, WC_MAXB
.rw2:
    mov [wc_srows], ax
    mov word [wc_tx], 8                 ; the text column, one cell in...
    mov cx, [wc_mx2]                    ; ...and its cells to the scroll
    sub cx, 8 + 4 - 1                   ; column, the last of them, four
    shr cx, 1                           ; pixels clear of the rule
    shr cx, 1
    shr cx, 1
    cmp cx, WC_TCOL + 8                 ; MINSIZE keeps it well above this;
    jge .cl                             ; the clamp is for the arithmetic
    mov cx, WC_TCOL + 8
.cl:
    cmp cx, WC_SBUFN - 1
    jb .cl2
    mov cx, WC_SBUFN - 2
.cl2:
    mov [wc_cols], cx
    call wc_layout
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_layout - the button rects and the field, ABSOLUTE (wc_geom's tail)
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_layout:
    mov di, wc_brects
    ; --- STATUS: the header's right end, from the status glyph --------------
    call wc_hstat                       ; CX = its first cell
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov cx, [wc_cw]
    and cx, ~7
    dec cx
    mov bx, 1
    mov dx, WC_LH
    call wc_rect
    ; --- SEARCH: the title's right end, its lens on the byte grid -----------
    mov ax, [wc_mx2]
    sub ax, 9
    shr ax, 1
    shr ax, 1
    shr ax, 1
    sub ax, WC_SRCHC                    ; AX = the lens's cell
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov cx, ax
    add cx, WC_SRCHC * 8 + 3
    sub ax, 4
    mov bx, [wc_mid]
    inc bx
    mov dx, bx
    add dx, 13
    call wc_rect
    ; --- SEND: the input box's right end, on the byte grid -----------------
    mov ax, [wc_cw]
    sub ax, 9 + WC_SENDC * 8 - 1
    and ax, ~7
    mov cx, ax
    add cx, WC_SENDC * 8 - 1
    mov bx, [wc_iy]
    add bx, WC_BOXY
    mov dx, bx
    add dx, WC_BOXH - 1
    call wc_rect
    ; --- the field: from the prompt's rule to Send ------------------------------
    call wc_pcells                      ; CX = the prompt's cells
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, 16 + 4 + 2                  ; the prompt, its rule, a pixel
    add cx, [wc_ox]
    mov [wc_ln+LN_X1], cx
    mov ax, [wc_brects + 8 * WB_SEND]
    sub ax, 4
    mov [wc_ln+LN_X2], ax
    mov ax, [wc_iy]
    add ax, [wc_oy]
    add ax, WC_BOXY + 1
    mov [wc_ln+LN_Y1], ax
    add ax, WC_FLDH - 1
    mov [wc_ln+LN_Y2], ax
    ret

; --- wc_rect - store the content-relative rect AX,BX..CX,DX at DI as an
; ABSOLUTE one, DI advanced. clobbers AX, BX, CX, DX
wc_rect:
    add ax, [wc_ox]
    mov [di], ax
    add bx, [wc_oy]
    mov [di+2], bx
    add cx, [wc_ox]
    mov [di+4], cx
    add dx, [wc_oy]
    mov [di+6], dx
    add di, 8
    ret

; --- wc_pcells - CX = the cells of the mode's prompt. Preserves the rest ----
wc_pcells:
    push bx
    push si
    mov bl, [wc_mode]
    xor bh, bh
    shl bx, 1
    mov si, [wc_prompts+bx]
    call wc_slen
    pop si
    pop bx
    ret

; --- wc_slen - CX = the length of the NUL string at SI. Preserves the rest. -
wc_slen:
    push si
    push ax
    xor cx, cx
.c:
    lodsb
    or al, al
    jz .out
    inc cx
    jmp short .c
.out:
    pop ax
    pop si
    ret

; --- wc_cells - CX = the content's whole cells. Preserves the rest ---------
wc_cells:
    mov cx, [wc_cw]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    ret

; =============================================================================
; THE BAND (see the header)
; =============================================================================
; wc_sb* compose into wc_sbuf; wc_bandc (wcband.inc) draws it.

; --- wc_sbclr - an empty composition; DI = wc_sbuf. Preserves the rest. ----
wc_sbclr:
    mov di, wc_sbuf
    mov byte [di], 0
    ret

; --- wc_sbstr - append the NUL string at SI (at most until the buffer ends) --
; in: DI = the composition's end. out: DI advanced. clobbers AL, SI
wc_sbstr:
.c:
    lodsb
    or al, al
    jz .out
    cmp di, wc_sbuf + WC_SBUFN - 1
    jae .c
    mov [di], al
    inc di
    jmp short .c
.out:
    mov byte [di], 0
    ret

; --- wc_sbch - append AL. in/out DI ---------------------------------------
wc_sbch:
    cmp di, wc_sbuf + WC_SBUFN - 1
    jae .out
    mov [di], al
    inc di
    mov byte [di], 0
.out:
    ret

; --- wc_sbto - pad the composition with spaces to column CX, or cut it there
; in: DI = its end. out: DI = wc_sbuf + CX, NUL there. clobbers AL
wc_sbto:
    push cx
    cmp cx, WC_SBUFN - 1
    jbe .ok
    mov cx, WC_SBUFN - 1
.ok:
    add cx, wc_sbuf
.c:
    cmp di, cx
    jae .cut
    mov byte [di], ' '
    inc di
    jmp short .c
.cut:
    mov di, cx
    mov byte [di], 0
    pop cx
    ret

; --- wc_sbnum - append AX in decimal. clobbers AX, DX ---------------------
wc_sbnum:
    push bx
    push cx
    push si
    push di
    xor dx, dx
    call wc_d32str
    pop di
    call wc_sbstr
    pop si
    pop cx
    pop bx
    ret

; --- wc_sb2 - append AL as two digits, '03'. clobbers AX -------------------
wc_sb2:
    xor ah, ah
    push bx
    mov bl, 10
    div bl
    pop bx
    add ax, '00'
    call wc_sbch
    mov al, ah
    jmp wc_sbch

; --- wc_sbat - CX = the composition's cell count. Preserves the rest -------
wc_sbat:
    mov cx, di
    sub cx, wc_sbuf
    ret

; -----------------------------------------------------------------------------
; wc_band - draw wc_sbuf as one band in style AL, remembered nowhere (the
;           popups, the prompt and the placeholder, which are drawn whole or
;           not at all)
; in:  CX = x, DX = the band's top (both CONTENT-relative), AL = WS_*; the lock
;      held, the clip armed
; out: every register preserved
; -----------------------------------------------------------------------------
wc_band:
    push bx
    call wc_mreset
    xor bx, bx
    call wc_bandc
    pop bx
    ret

; --- wc_stglyph - AL = a status (ST_*) -> BL = its glyph --------------------
wc_stglyph:
    mov bl, al
    ret
%if GL_ONLINE != ST_ONLINE || GL_AWAY != ST_AWAY || GL_BUSY != ST_BUSY || GL_GONE != ST_GONE
  %error "wc_stglyph maps ST_* onto GL_* as they are"
%endif

; =============================================================================
; W_PAINT, and the flush
; =============================================================================

; -----------------------------------------------------------------------------
; wc_paint - W_PAINT: everything (the window is OWNBG: nothing is drawn yet,
;            the clip armed)
; in:  SI = window ptr. out: SI kept; the rest as W_PAINT may
; -----------------------------------------------------------------------------
wc_paint:
    push si
    call wc_hire                        ; the worker, once (SPEC.md 20.6)
    call wc_face                        ; the theme, and every slot owed
    call wc_geom
    jc .out
    call wc_rules                       ; the ground, once, and the rules
    mov byte [wc_fresh], 1
    mov word [wc_dirty], WCD_ALL & ~WCD_TAIL
    call wc_flush0                      ; every region, no popup, no card
    call wc_buttons
    mov byte [wc_fresh], 0
    cmp byte [wc_abon], 0
    je .pop
    mov bx, [wc_win]
    call wc_cardlines
    call os88ui_about_d                 ; the card LAST, over what it hides
    jmp short .out
.pop:
    call wc_popdraw
.out:
    pop si
    ret

; --- wc_cardlines - SI = the card's lines: About's, or F1's help -----------
wc_cardlines:
    mov si, wc_ablines
    cmp byte [wc_abon], 2
    jne .out
    mov si, wc_hlplines
.out:
    ret

; --- wc_rules - the ground, once, then the rules and the input box ---------
; A full paint's decoration, which the bands then letter over.
wc_rules:
    mov al, R_BG
    call wc_penc
    mov ax, [wc_ox]
    mov bx, [wc_oy]
    mov cx, ax
    add cx, [wc_cw]
    dec cx
    mov dx, bx
    add dx, [wc_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov al, R_ACC
    call wc_penc
    mov dx, [wc_hr]                     ; the header's
    call wc_hrule
    mov dx, [wc_mid]                    ; the tabs'
    dec dx
    call wc_hrule
    mov dx, [wc_iy]                     ; the input row's
    call wc_hrule
    mov dx, [wc_sy]                     ; the status bar's
    call wc_hrule
    mov ax, [wc_rx1]                    ; the right pane's
    or ax, ax
    jz .t
    add ax, [wc_ox]
    mov bx, [wc_mid]
    add bx, [wc_oy]
    mov dx, [wc_iy]
    dec dx
    add dx, [wc_oy]
    call OSAPI_GFX_VLINE
.t:
    mov ax, [wc_ox]                     ; the input box, to Send
    add ax, 8
    mov bx, [wc_iy]
    add bx, [wc_oy]
    add bx, WC_BOXY
    mov cx, [wc_brects + 8 * WB_SEND]
    dec cx
    mov dx, bx
    add dx, WC_BOXH - 1
    call OSAPI_GFX_FRAME
    xor ax, ax                          ; under the title, across the center
    mov bx, [wc_mx2]
    mov dx, [wc_py1]
    sub dx, 2
    jmp short wc_hrule2

; --- wc_hrule - a rule across the content at row DX; wc_hrule2 AX..BX -----
wc_hrule:
    xor ax, ax
    mov bx, [wc_cw]
    dec bx
wc_hrule2:
    add ax, [wc_ox]
    add bx, [wc_ox]
    add dx, [wc_oy]
    call OSAPI_GFX_HLINE                ; the slots are FAR: a call and a ret
    ret

; -----------------------------------------------------------------------------
; wc_flush - paint the dirty regions
; in:  the lock HELD and the clip ARMED. out: [wc_dirty] = 0; clobbers all
;      but BP
;
; With the About card up nothing is drawn: the card is opaque over the
; center, and its dismissal repaints everything. A popup is drawn again over
; whatever a region painted under it.
; -----------------------------------------------------------------------------
wc_flush:
    cmp byte [wc_abon], 0
    jne .out
    call wc_geom
    jc .out
    call wc_flush0
    call wc_popdraw
.out:
    ret

wc_flush0:
    xor ax, ax
    xchg ax, [wc_dirty]                 ; the bits, and none from here
    test ax, WCD_HDR
    jz .1
    push ax
    call wc_phdr
    call wc_phdr2
    pop ax
.1:
    test ax, WCD_TABS
    jz .2
    push ax
    call wc_ptabs
    pop ax
.2:
    test ax, WCD_TITLE
    jz .3
    push ax
    call wc_ptitle
    pop ax
.3:
    test ax, WCD_PANE
    jz .4
    push ax
    call wc_ppane
    pop ax
    jmp short .5
.4:
    test ax, WCD_TAIL
    jz .5
    push ax
    call wc_ptail
    pop ax
.5:
    test ax, WCD_NOTE
    jz .6
    push ax
    call wc_pnote
    pop ax
.6:
    test ax, WCD_FIELD
    jz .7
    push ax
    call wc_pfield
    pop ax
.7:
    test ax, WCD_RIGHT
    jz .8
    push ax
    call wc_pright
    pop ax
.8:
    test ax, WCD_STAT
    jz .9
    call wc_pstat
.9:
    ret

; --- wc_buttons - draw every button ------------------------------------------
wc_buttons:
    call wc_bflags_set
    mov al, 1
.b:
    call wc_btn
    inc ax
    cmp al, WB_N + 1
    jb .b
    ret

; --- wc_bflags_set - the flag words, from the ONE predicate (SPEC.md 47) ---
; Every button is the package's on every theme (SPEC.md 113.5.2).
wc_bflags_set:
    push ax
    push bx
    push dx
    xor ax, ax
    mov bx, wc_bflags
.f:
    mov dx, OS88UI_FILL | OS88UI_OWN
    call wc_bok                         ; CF = 1 refused
    jnc .live
    or dx, OS88UI_DIS
.live:
    mov [bx], dx
    inc bx
    inc bx
    inc ax
    cmp al, WB_N
    jb .f
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_bok - would this button do anything? (SPEC.md 47 rule 2)
; in:  AL = the button. out: CF = 0 live, 1 refused; every register preserved
;
; Send wants something to send. The rest are always live: Search with nothing
; to search finds nothing and says so, which is an answer.
; -----------------------------------------------------------------------------
wc_bok:
    cmp al, WB_SEND
    jne .live
    cmp word [wc_ln+LN_LEN], 0
    je .no
.live:
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; THE HEADER
; =============================================================================

; --- wc_hstat - CX = the header's status part's first cell: its glyph, a
; space, the word in a field as wide as the widest (OFFLINE), two spaces,
; the nick, a space - at the band's right end. The field is fixed so a
; status change moves no span: its band is a run again, not a whole band
; (SPEC.md 113.5.1). Preserves the rest.
WC_HWORD    equ 7

wc_hstat:
    push ax
    push bx
    push si
    mov si, wc_nick
    cmp byte [si], 0
    jne .n
    mov si, wc_s_nonick
.n:
    call wc_slen
    mov ax, cx
    add ax, WC_HWORD + 5
    call wc_cells
    sub cx, ax
    jge .out
    xor cx, cx
.out:
    mov [wc_hrs], cx
    pop si
    pop bx
    pop ax
    ret

; --- wc_stword - SI = the header's word for our state: the status, or why we
; have none. clobbers BX
wc_stword:
    mov si, wc_s_offline
    cmp byte [wc_link], LK_LIVE
    jne .out
    mov bl, [wc_st]
    xor bh, bh
    shl bx, 1
    mov si, [wc_hwords+bx]
.out:
    ret

; --- wc_mystat - AL = the status our glyph shows: ours, or gone ------------
wc_mystat:
    mov al, [wc_st]
    cmp byte [wc_link], LK_LIVE
    je .out
    mov al, ST_GONE
.out:
    ret

; -----------------------------------------------------------------------------
; wc_phdr - the header's first band: 'WireChat', and at the right end our
;           glyph, the status word in its colour and the nick, which are the
;           STATUS button (wcband.inc's wc_stbtn)
; -----------------------------------------------------------------------------
wc_phdr:
    call wc_sbclr
    call wc_rclr
    call wc_hstat                       ; CX = the status's first cell
    push cx
    mov al, ' '
    call wc_sbch
    mov si, wc_s_brand
    mov [wc_rbold], si
    mov byte [wc_rbcol], 1
    call wc_sbstr
    pop cx
    call wc_sbto
    ; --- the status: its glyph, its word in its colour, the nick ------------
    push cx
    call wc_mystat
    call wc_stglyph
    mov [wc_rgl], bl
    mov [wc_rgcol], cl
    pop cx
    xor bh, bh
    mov al, [wc_glrole+bx]              ; the word in the glyph's own role
    call wc_pen
    mov ah, 0xFF
    cmp byte [wc_hdown], 0
    je .up
    mov al, R_HI                        ; pressed: the highlight
    call wc_pen
    mov ah, al
    mov al, R_HIFG
    call wc_pen
.up:
    call wc_rspan
    mov al, ' '
    call wc_sbch
    call wc_sbch
    call wc_stword
    call wc_sbstr
    mov cx, [wc_hrs]
    add cx, 2 + WC_HWORD + 2
    call wc_sbto
    cmp byte [wc_hdown], 0
    jne .nick
    call wc_sbat
    mov ax, 0xFFFF                      ; the nick in the text's colour
    call wc_rspan
.nick:
    mov si, wc_nick
    cmp byte [si], 0
    jne .n
    mov si, wc_s_nonick
.n:
    call wc_sbstr
    call wc_cells
    call wc_sbto
    xor cx, cx
    mov dx, 1
    mov ax, WCSL_HDR
    jmp wc_rowout

; -----------------------------------------------------------------------------
; wc_phdr2 - the header's second band, on a tall window: the machine's date
;            and time at the right end, 'Oct 20 2026  16:03' (OSAPI_CLK_GET)
; -----------------------------------------------------------------------------
wc_phdr2:
    cmp word [wc_hy2], 0
    je .out
    call wc_sbclr
    call wc_rclr
    call wc_cells
    sub cx, 19
    jge .c
    xor cx, cx
.c:
    call wc_sbto
    mov al, R_ACC
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    call OSAPI_CLK_GET                  ; CX year, DH month, DL day, BH:BL
    mov [wc_clkm], bl
    push bx
    push cx
    push dx
    mov bl, dh                          ; the month's three letters
    dec bl
    xor bh, bh
    shl bx, 1
    shl bx, 1
    lea si, [wc_s_months+bx]
    call wc_sbstr
    mov al, ' '
    call wc_sbch
    pop dx
    mov al, dl
    call wc_sb2
    mov al, ' '
    call wc_sbch
    pop ax
    call wc_sbnum
    mov al, ' '
    call wc_sbch
    call wc_sbch
    call wc_sbat
    mov ax, 0xFFFF                      ; the time in the text's colour
    call wc_rspan
    pop bx
    mov al, bh
    xor ah, ah
    xor dh, dh
    mov dl, bl
    call wc_sbhm
    call wc_cells
    call wc_sbto
    xor cx, cx
    mov dx, [wc_hy2]
    mov ax, WCSL_HDR2
    jmp wc_rowout
.out:
    ret

; =============================================================================
; THE TABS
; =============================================================================

; -----------------------------------------------------------------------------
; wc_ptabs - a tab a room: ' #name ', the view's in the highlight, an unread
;            count a badge after it ('@' ahead of it for a mention), '|'
;            between. Each tab's first cell goes in wc_tabc for the press.
; -----------------------------------------------------------------------------
wc_ptabs:
    call wc_sbclr
    call wc_rclr
    mov al, ' '
    call wc_sbch
    xor bx, bx                          ; BX = the room
.t:
    call wc_sbat
    mov [wc_tabc+bx], cl
    cmp bl, [wc_view]
    jne .ns
    mov al, R_HI                        ; the view's: the highlight
    call wc_pen
    mov ah, al
    mov al, R_HIFG
    call wc_pen
    call wc_rspan
.ns:
    mov al, ' '
    call wc_sbch
    mov al, '#'
    call wc_sbch
    push bx
    mov ax, bx
    call wc_roomname
    call wc_sbstr
    mov al, ' '
    call wc_sbch
    pop bx
    cmp bl, [wc_view]
    jne .badge
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
.badge:
    ; --- unread: a badge, '@' ahead of the count for a mention ------------
    push bx
    mov si, bx
    shl si, 1
    mov ax, [wc_cnt+si]
    sub ax, [wc_seen+si]
    cmp ax, 99
    jbe .c
    mov ax, 99
.c:
    cmp byte [wc_ment+bx], 0
    jne .bad
    or ax, ax
    jz .nob
.bad:
    push ax
    call wc_sbat
    mov al, R_BAD
    call wc_pen
    mov ah, al
    mov al, R_BADFG
    call wc_pen
    call wc_rspan
    mov al, ' '
    call wc_sbch
    pop ax
    cmp byte [wc_ment+bx], 0
    je .num
    push ax
    mov al, '@'
    call wc_sbch
    pop ax
    or ax, ax
    jz .bend
.num:
    call wc_sbnum
.bend:
    mov al, ' '
    call wc_sbch
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
    mov al, ' '
    call wc_sbch
.nob:
    pop bx
    inc bx
    cmp bx, WC_NROOM
    jae .end
    call wc_sbat                        ; '|' in the accent between
    mov al, R_ACC
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    mov al, '|'
    call wc_sbch
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
    jmp .t
.end:
    call wc_sbat
    mov [wc_tabc+WC_NROOM], cl
    call wc_cells
    call wc_sbto
    xor cx, cx
    mov dx, [wc_tby]
    mov ax, WCSL_TABS
    jmp wc_rowout

; --- wc_roomname - AX = a room -> SI = its name (or a placeholder) ---------
wc_roomname:
    push dx
    mov dx, WC_ROOMN + WC_TOPICN
    mul dx
    add ax, wc_rooms
    mov si, ax
    pop dx
    cmp byte [si], 0
    jne .out
    mov si, wc_s_dots                   ; not told yet (no L lines)
.out:
    ret

; =============================================================================
; THE CENTER
; =============================================================================

; -----------------------------------------------------------------------------
; wc_ptitle - '#room' bold in the accent and its topic dim, or '@nick' and
;             'Private conversation' - up to the search box
; -----------------------------------------------------------------------------
wc_ptitle:
    call wc_sbclr
    call wc_rclr
    xor cl, cl                          ; the name in the accent
    mov al, R_ACC
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    mov al, '@'
    cmp byte [wc_view], WV_DM
    je .t
    mov al, '#'
.t:
    call wc_sbch
    mov byte [wc_rbcol], 1
    mov si, wc_vdm
    cmp byte [wc_view], WV_DM
    je .n
    mov al, [wc_view]
    xor ah, ah
    call wc_roomname
.n:
    mov [wc_rbold], si
    call wc_sbstr
    mov al, ' '
    call wc_sbch
    call wc_sbch
    call wc_sbat                        ; ...and the topic dim
    mov al, R_DIM
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    mov si, wc_s_private
    cmp byte [wc_view], WV_DM
    je .tp
    mov al, [wc_view]                   ; the room's topic: straight from the
    xor ah, ah                          ; table, empty until the L lines come
    mov dx, WC_ROOMN + WC_TOPICN
    mul dx
    add ax, wc_rooms + WC_ROOMN
    mov si, ax
.tp:
    call wc_sbstr
    mov cx, [wc_brects + 8 * WB_SEARCH] ; to the search box
    sub cx, [wc_ox]
    sub cx, [wc_tx]
    sar cx, 1
    sar cx, 1
    sar cx, 1
    dec cx
    jge .w
    xor cx, cx
.w:
    call wc_sbto
    mov cx, [wc_tx]
    mov dx, [wc_ty]
    mov ax, WCSL_TITLE
    jmp wc_rowout

; -----------------------------------------------------------------------------
; wc_pnote - the notice line: what the server or the conductor last said
; -----------------------------------------------------------------------------
wc_pnote:
    call wc_sbclr
    call wc_rclr
    mov byte [wc_rinv], WS_NOTE
    mov si, wc_note
    call wc_sbstr
    mov cx, [wc_cols]
    call wc_sbto
    mov cx, [wc_tx]
    mov dx, [wc_ny]
    mov ax, WCSL_NOTE
    jmp wc_rowout

; -----------------------------------------------------------------------------
; wc_pfield - the input row: the box's inside, the mode's prompt and its rule,
;             the field, the placeholder, and Send
; -----------------------------------------------------------------------------
wc_pfield:
    ; A prompt that came or went moved the field's left edge, and os88line_
    ; draw paints its own rect and nothing left of it - so the box's inside
    ; is the ground again first, unless a full paint has just laid it. This
    ; runs on a MODE change and a field set from behind; a keystroke is
    ; os88line_edit's, which needs none of it.
    ; FIND is the box in the highlight (SPEC.md 113.6.1): its inside, the
    ; prompt, its rule and the field, so what Enter does is not a guess -
    ; which a full paint's ground is not, so that fill is never skipped.
    mov al, R_FG                        ; the field's pens: the text on the
    mov ah, R_BG                        ; ground, its frame put out of sight
    cmp byte [wc_mode], MD_FIND
    jne .pn
    mov ax, R_HIFG | (R_HI << 8)
.pn:
    push ax
    call wc_pen
    xchg al, ah
    call wc_pen
    xchg al, ah
    mov [os88line_pens], ax
    mov [os88line_pens+2], ah
    pop ax
    cmp byte [wc_mode], MD_FIND
    je .fill
    cmp byte [wc_fresh], 0
    jne .p
.fill:
    mov al, ah
    call wc_penc
    mov ax, [wc_ox]
    add ax, 9
    mov bx, [wc_ln+LN_Y1]
    mov cx, [wc_brects + 8 * WB_SEND]
    sub cx, 2
    mov dx, [wc_ln+LN_Y2]
    call OSAPI_GFX_FILL
.p:
    call wc_sbclr
    call wc_pcells
    mov bl, [wc_mode]
    xor bh, bh
    shl bx, 1
    mov si, [wc_prompts+bx]
    call wc_sbstr
    mov cx, 16
    mov dx, [wc_iy]
    add dx, WC_BOXY + 3
    mov al, WS_ACC                      ; the prompt in the accent
    mov ah, R_ACC                       ; ...and its rule
    cmp byte [wc_mode], MD_FIND
    jne .pr
    mov ax, WS_INV | (R_HIFG << 8)
.pr:
    push ax
    call wc_band
    pop ax
    mov al, ah
    call wc_penc
    mov ax, [wc_ln+LN_X1]
    sub ax, 2
    mov bx, [wc_ln+LN_Y1]
    mov dx, [wc_ln+LN_Y2]
    call OSAPI_GFX_VLINE
    mov si, wc_ln
    call os88line_draw
    call wc_holder
    call wc_bflags_set                  ; ...and Send, whose grey is the
    mov al, WB_SEND + 1                 ; field's emptiness (SPEC.md 47 rule
    jmp wc_btn                          ; 2)

; --- wc_holder - an empty field in message mode says where it sends:
; 'Message #lobby', dim, a cell in from the caret. Drawn over the field's own
; ground, which os88line_draw's strip fill takes back with the first key
wc_holder:
    cmp word [wc_ln+LN_LEN], 0
    jne .out
    cmp byte [wc_mode], MD_MSG
    jne .out
    call wc_sbclr
    mov si, wc_s_msgto
    call wc_sbstr
    mov si, wc_vdm
    mov al, '@'
    cmp byte [wc_view], WV_DM
    je .n
    mov al, [wc_view]
    xor ah, ah
    call wc_roomname
    mov al, '#'
.n:
    call wc_sbch
    call wc_sbstr
    push si
    mov si, wc_ln
    call os88line_pen                   ; AX = the text's first cell
    pop si
    add ax, 8
    mov cx, [wc_ln+LN_X2]
    sub cx, 2
    sub cx, ax
    jl .out
    shr cx, 1
    shr cx, 1
    shr cx, 1
    push ax
    call wc_sbto
    pop ax
    sub ax, [wc_ox]
    mov cx, ax
    mov dx, [wc_ln+LN_Y1]
    sub dx, [wc_oy]
    add dx, 2
    mov al, WS_DIM
    jmp wc_band
.out:
    ret

; =============================================================================
; THE RIGHT PANE: ONLINE, FRIENDS, DIRECT (SPEC.md 113.5)
; =============================================================================

; -----------------------------------------------------------------------------
; wc_rsect - what band N of the right pane holds
; in:  CX = the band, from 0. out: AL = RS_*, AH = the item's index;
;      clobbers BX, CX, DX
;
; THE PAINTER AND THE HIT TEST BOTH ASK THIS, so a press lands on what was
; drawn there. ONLINE and its members, a gap; FRIENDS and every friend, a
; gap; DIRECT and its conversations, or a line saying there are none.
; -----------------------------------------------------------------------------
RS_NONE     equ 0
RS_TONL     equ 1
RS_MEMBER   equ 2
RS_TFRND    equ 3
RS_FRIEND   equ 4
RS_TDM      equ 5
RS_DM       equ 6
RS_NODM     equ 7

wc_rsect:
    mov ax, RS_TONL
    jcxz .out
    dec cx
    push cx
    call wc_members                     ; AX = how many
    pop cx
    cmp cx, ax
    jb .mem
    sub cx, ax
    jcxz .none                          ; the gap
    dec cx
    mov ax, RS_TFRND
    jcxz .out
    dec cx
    xor dx, dx
    mov dl, [wc_fn]
    cmp cx, dx
    jb .fr
    sub cx, dx
    jcxz .none
    dec cx
    mov ax, RS_TDM
    jcxz .out
    dec cx
    mov dl, [wc_dn]
    or dl, dl
    jnz .dms
    mov al, RS_NODM
    jcxz .out
    jmp short .none
.dms:
    cmp cx, dx
    jae .none
    mov ah, cl
    mov al, RS_DM
    ret
.mem:
    mov ah, cl
    mov al, RS_MEMBER
    ret
.fr:
    mov ah, cl
    mov al, RS_FRIEND
    ret
.none:
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; wc_pright - the right pane, a band at a time down to the input row's rule
; -----------------------------------------------------------------------------
wc_pright:
    cmp word [wc_rx1], 0
    je .out
    mov word [wc_rband], 0
    mov ax, [wc_mid]
    add ax, 3
    mov [wc_rowy], ax
.b:
    mov ax, [wc_rowy]
    add ax, WC_LH
    cmp ax, [wc_iy]
    ja .out
    mov cx, [wc_rband]
    call wc_rsect
    call wc_rrow
    add word [wc_rowy], WC_LH
    inc word [wc_rband]
    jmp short .b
.out:
    ret

; --- wc_rrow - compose and draw right band [wc_rband] at [wc_rowy] holding
; AL/AH (wc_rsect's answer)
wc_rrow:
    call wc_sbclr
    call wc_rclr
    cmp al, RS_TONL
    je .tonl
    cmp al, RS_TFRND
    je .tfr
    cmp al, RS_TDM
    je .tdm
    cmp al, RS_MEMBER
    je .mem
    cmp al, RS_FRIEND
    je .fr
    cmp al, RS_DM
    je .dm
    cmp al, RS_NODM
    je .nodm
    jmp .draw
.tonl:
    call wc_members
    mov si, wc_s_online
    mov bl, GL_PEOPLE
    jmp short .title
.tfr:
    mov al, [wc_fn]
    mov si, wc_s_frnds
    mov bl, GL_PEOPLE
    jmp short .title
.tdm:
    mov al, [wc_dn]
    mov si, wc_s_direct
    mov bl, GL_MAIL
.title:
    ; 'ONLINE  03' bold in the accent, its glyph at the right end, the
    ; band's lower air in the accent: the section's rule
    push ax
    mov byte [wc_rinv], WS_ACC
    mov [wc_rbold], si
    mov [wc_rgl], bl
    mov byte [wc_rgcol], WC_RCELLS - 2
    mov al, R_ACC
    call wc_pen
    mov [wc_rul], al
    call wc_sbstr
    mov cx, 8
    call wc_sbto
    pop ax
    call wc_sb2
    jmp .draw
.mem:
    mov cl, ah                          ; the AH'th member
    xor ch, ch
    call wc_nthmember                   ; SI = the nick
    jmp short .who
.fr:
    mov al, ah
    call wc_frrec                       ; SI = the friend
.who:
    push si
    call wc_isview                      ; the conversation open: highlighted
    jne .nv
    mov byte [wc_rinv], WS_INV
.nv:
    call wc_pglyph
    mov al, ' '
    call wc_sbch
    pop si
    call wc_sbstr
    jmp short .draw
.dm:
    mov al, ah
    xor ah, ah
    mov cl, 4                           ; WC_DREC = 16
    shl ax, cl
    add ax, wc_dms
    mov si, ax
    push si
    call wc_isview
    jne .dnot
    mov byte [wc_rinv], WS_INV
.dnot:
    call wc_pglyph
    mov al, ' '
    call wc_sbch
    call wc_sbstr
    mov cx, WC_RCELLS - 4
    call wc_sbto
    pop si
    mov al, [si+WC_DUNR]                ; unread: a badge at the right end
    xor ah, ah
    or ax, ax
    jz .draw
    cmp ax, 99
    jbe .dn
    mov ax, 99
.dn:
    push ax
    call wc_sbat
    mov al, R_BAD
    call wc_pen
    mov ah, al
    mov al, R_BADFG
    call wc_pen
    call wc_rspan
    pop ax
    cmp ax, 10
    jae .d2
    push ax
    mov al, ' '
    call wc_sbch
    pop ax
.d2:
    call wc_sbnum
    mov al, ' '
    call wc_sbch
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
    jmp short .draw
.nodm:
    mov byte [wc_rinv], WS_DIM
    mov si, wc_s_nodm
    call wc_sbstr
.draw:
    mov cx, WC_RCELLS
    call wc_sbto
    mov cx, [wc_rx1]
    add cx, 8
    mov dx, [wc_rowy]
    mov ax, [wc_rband]
    add ax, WCSL_RIGHT
    cmp word [wc_rband], WC_MAXB
    jb .slot
    mov ax, 0xFFFF
.slot:
    jmp wc_rowout

; --- wc_nthmember - CX = n -> SI = the n'th session ONLINE lists, CF = 1
; none. clobbers AX, BX, CX
wc_nthmember:
    xor bx, bx
.c:
    cmp bl, [wc_rn]
    jae .no
    call wc_ismember
    jc .n
    jcxz .yes
    dec cx
.n:
    inc bx
    jmp short .c
.yes:
    mov ax, WC_RREC
    mul bx
    add ax, wc_ros
    mov si, ax
    clc
    ret
.no:
    stc
    ret

; --- wc_ismember - CF = 0 when roster record BL belongs in ONLINE ---------
; In a room view, the sessions in that room; in a conversation, everyone.
wc_ismember:
    cmp byte [wc_view], WV_DM
    je .yes
    push ax
    push bx
    xor bh, bh
    mov ax, WC_RREC
    mul bx
    mov bx, ax
    mov al, [wc_ros+bx+WC_RROOM]
    cmp al, [wc_view]
    pop bx
    pop ax
    je .yes
    stc
    ret
.yes:
    clc
    ret

; --- wc_members - AX = how many sessions ONLINE lists. clobbers BX ---------
wc_members:
    push dx
    xor ax, ax
    xor bx, bx
.c:
    cmp bl, [wc_rn]
    jae .out
    call wc_ismember
    jc .n
    inc ax
.n:
    inc bx
    jmp short .c
.out:
    pop dx
    ret

; --- wc_frrec - AL = a friend -> SI = its nick. Preserves the rest. ----------
wc_frrec:
    push ax
    push dx
    xor ah, ah
    mov dx, WC_NICKN
    mul dx
    add ax, wc_frs
    mov si, ax
    pop dx
    pop ax
    ret

; --- wc_rosfind - the nick at SI on the roster? CF = 0 and BX = its record;
; preserves the rest
wc_rosfind:
    push cx
    push di
    xor cx, cx
    mov cl, [wc_rn]
    mov bx, wc_ros
    jcxz .no
.c:
    mov di, bx
    call wc_streqci
    je .yes
    add bx, WC_RREC
    loop .c
.no:
    pop di
    pop cx
    stc
    ret
.yes:
    pop di
    pop cx
    clc
    ret

; --- wc_pglyph - SI = a nick: its status glyph into [wc_rgl] at the
; composition's cell, and a space for it. Preserves SI.
wc_pglyph:
    push bx
    push si
    mov al, ST_GONE
    call wc_rosfind
    jc .g
    mov al, [bx+WC_RST]
.g:
    call wc_stglyph
    mov [wc_rgl], bl
    push cx
    call wc_sbat
    mov [wc_rgcol], cl
    pop cx
    mov al, ' '
    call wc_sbch
    pop si
    pop bx
    ret

; --- wc_isview - ZF = 1 when the view is the conversation with the nick at SI
wc_isview:
    cmp byte [wc_view], WV_DM
    jne .out                            ; ZF = 0 from the compare
    push di
    mov di, wc_vdm
    call wc_streqci
    pop di
.out:
    ret

; =============================================================================
; THE ROW: what a painter has composed, drawn through its slot
; =============================================================================

; --- wc_rclr - a row with nothing to say about itself yet: the normal style,
; no glyph, no bold, no spans, no underline. Preserves all.
wc_rclr:
    mov byte [wc_rinv], WS_NORM
    mov byte [wc_rgl], 0xFF
    mov byte [wc_rgl2], 0xFF
    mov byte [wc_rgcol], 0
    mov word [wc_rbold], 0
    mov byte [wc_rbcol], 0
    mov byte [wc_rspn], 0
    mov byte [wc_rul], 0xFF
    ret

; --- wc_rspan - a span for the row: from cell CL, ink AL, ground AH (0xFF the
; style's). Preserves all.
wc_rspan:
    push bx
    push dx
    mov bl, [wc_rspn]
    cmp bl, WC_NSPAN - 1
    jae .full
    inc byte [wc_rspn]
    xor bh, bh
    mov dx, bx
    shl bx, 1
    add bx, dx
    mov [wc_rsps+bx], cl
    mov [wc_rsps+bx+1], al
    mov [wc_rsps+bx+2], ah
.full:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wc_rowout - draw the composed row at CX, DX through slot AX (0xFFFF none):
; the style [wc_rinv], glyph [wc_rgl] at cell [wc_rgcol] and [wc_rgl2] at
; [wc_rgcol2] (0xFF none), the bold [wc_rbold] (a string, 0 none) at cell
; [wc_rbcol], the underline [wc_rul] and the spans wc_rspan collected
; -----------------------------------------------------------------------------
wc_rowout:
    push ax
    push cx
    mov al, [wc_rinv]
    call wc_mreset
    mov al, [wc_rul]
    mov [wc_meta+WCX_UL], al
    mov bx, wc_rsps
    mov ch, [wc_rspn]
.sp:
    or ch, ch
    jz .gl
    mov cl, [bx]
    mov ax, [bx+1]
    call wc_span
    add bx, 3
    dec ch
    jmp short .sp
.gl:
    mov cl, [wc_rgcol]
    mov bl, [wc_rgl]
    cmp bl, 0xFF
    je .g2
    call wc_mglyph
.g2:
    mov cl, [wc_rgcol2]
    mov bl, [wc_rgl2]
    cmp bl, 0xFF
    je .b
    call wc_mglyph
.b:
    mov si, [wc_rbold]
    or si, si
    jz .go
    call wc_slen
    mov ch, [wc_rbcol]
    call wc_mbold
.go:
    pop cx
    pop ax
    call wc_slot
    jmp wc_bandc

; =============================================================================
; THE STATUS BAR
; =============================================================================

; -----------------------------------------------------------------------------
; wc_pstat - the F-key legend as far as there is room, the two lamps, the
;            link and the room's head count, 'Notifications: On' at the end
; -----------------------------------------------------------------------------
wc_pstat:
    call wc_linktext                    ; wc_rbuf: the link's words
    mov si, wc_rbuf
    call wc_slen
    mov ax, cx
    call wc_cells                       ; the lamps' cell: before the link's
    sub cx, WC_NOTIFYC + 3 + 3          ; words, the lamps and a space, and
    sub cx, ax                          ; ' | ' ahead of Notifications
    jge .l
    xor cx, cx
.l:
    mov [wc_lmc], cx
    call wc_sbclr
    call wc_rclr
    ; --- the legend: each item while it ends a cell clear of the lamps ----
    mov al, ' '
    call wc_sbch
    xor bx, bx                          ; BX = the item, times two
    mov byte [wc_fkn], 0
.k:
    mov si, [wc_fkeys+bx]
    call wc_slen
    mov ax, cx                          ; AX = its cells...
    or bx, bx
    jz .k0
    add ax, 3                           ; ...after ' | '
.k0:
    call wc_sbat
    add ax, cx
    inc ax
    cmp ax, [wc_lmc]
    ja .kend
    push bx                             ; where it starts, for the press
    shr bx, 1
    mov [wc_fkc+bx], cl
    pop bx
    mov al, R_ACC                       ; the separator and the key: accent
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    or bx, bx
    jz .k2
    push si
    mov si, wc_s_bar
    call wc_sbstr
    pop si
.k2:
    lodsb                               ; 'F1'
    call wc_sbch
    lodsb
    call wc_sbch
    call wc_sbat                        ; ...its word: the text's colour
    mov ax, 0xFFFF
    call wc_rspan
    call wc_sbstr
    inc byte [wc_fkn]
    inc bx
    inc bx
    cmp bx, 6
    jb .k
.kend:
    call wc_sbat
    mov bl, [wc_fkn]
    xor bh, bh
    mov [wc_fkc+bx], cl                 ; the legend's end
    ; --- the lamps and the link ---------------------------------------------
    mov cx, [wc_lmc]
    call wc_sbto
    mov [wc_rgcol], cl
    inc cx
    mov [wc_rgcol2], cl
    call wc_lamps
    mov [wc_rgl], bl
    mov [wc_rgl2], bh
    mov al, ' '
    call wc_sbch
    call wc_sbch
    call wc_sbch
    mov si, wc_rbuf
    call wc_sbstr
    ; --- ' | Notifications: On' --------------------------------------------
    call wc_cells
    sub cx, WC_NOTIFYC + 3
    call wc_sbto
    mov al, R_ACC
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    mov si, wc_s_bar
    call wc_sbstr
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
    mov si, wc_s_notify
    call wc_sbstr
    call wc_sbat                        ; On in OK, Off in BAD
    mov al, R_OK
    mov si, wc_s_on
    cmp byte [wc_notify], 0
    jne .o
    mov al, R_BAD
    mov si, wc_s_off
.o:
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    call wc_sbstr
    call wc_sbat
    mov ax, 0xFFFF
    call wc_rspan
    call wc_cells
    sub cx, WC_GROWC
    call wc_sbto
    xor cx, cx
    mov dx, [wc_sy]
    inc dx
    mov ax, WCSL_STAT
    jmp wc_rowout

; --- wc_linktext - wc_rbuf = the link's words: 'Connected: 4 in #lobby',
; 'Connected: 4 online' in a conversation, else the state's own words
wc_linktext:
    call wc_linkword                    ; SI = 'Connected', ...
    push di
    mov di, wc_rbuf
    mov bx, wc_rbuf + 31
    call wc_put
    cmp byte [wc_link], LK_LIVE
    jne .end
    mov si, wc_s_colon
    call wc_put
    push bx
    call wc_members
    pop bx
    xor dx, dx
    call wc_putd32
    mov si, wc_s_onl
    cmp byte [wc_view], WV_DM
    je .w
    mov si, wc_s_inrm
    call wc_put
    mov al, [wc_view]
    xor ah, ah
    push bx
    call wc_roomname
    pop bx
.w:
    call wc_put
.end:
    mov byte [di], 0
    pop di
    ret

; --- wc_lamps - the two lamps' glyphs: BL the link's, BH activity's ---------
; THE LINK: OK with a session, WARN while one is being made or the server
; asked us to rest, BAD with no card or after a failure, a ring with no nick.
; ACTIVITY: a diamond while a request we MADE is on the wire (a join, a
; message, a flag - a held poll is waiting, not activity) and for a second
; after a reply that brought something (wc_ondone's [wc_act]); none otherwise.
wc_lamps:
    mov al, [wc_link]
    mov bl, GL_LKOK
    cmp al, LK_LIVE
    je .act
    mov bl, GL_LKOFF
    cmp al, LK_NONICK
    je .act
    mov bl, GL_LKBAD
    cmp al, LK_NODRV
    je .act
    cmp al, LK_OFF
    je .act
    mov bl, GL_LKWARN
.act:
    mov bh, GL_ACT
    cmp byte [wc_act], 0
    jne .out
    mov al, [wc_rq]
    cmp al, RQ_NONE
    je .dark
    cmp al, RQ_POLL
    jne .out
.dark:
    mov bh, GL_BLANK
    cmp byte [wc_eth], TH_WIRED          ; Wired's is an LED that is out
    jne .out
    mov bh, GL_LKOFF
.out:
    ret

; --- wc_linkword - SI = the status bar's word for [wc_link] ------------------
; 'Offline - retrying in Ns' is composed in wc_rbuf + 32.
wc_linkword:
    mov bl, [wc_link]
    xor bh, bh
    shl bx, 1
    mov si, [wc_linkwords+bx]
    cmp byte [wc_link], LK_OFF
    jne .out
    push di
    mov di, wc_rbuf + 32
    push bx
    mov bx, wc_rbuf + 32 + 31
    call wc_put
    mov ax, [wc_wait]
    xor dx, dx
    call wc_putd32
    mov al, 's'
    call wc_putc
    mov byte [di], 0
    pop bx
    pop di
    mov si, wc_rbuf + 32
.out:
    ret

; =============================================================================
; THE MESSAGE PANE
; =============================================================================
; A message is LINES: an optional day divider, an optional new-messages
; divider, then its text in SEGMENTS, word-wrapped - the first behind the
; time and the nick, the rest under the text column. [WC_MLINES] caches the
; count for the current width; wc_relines makes every cache good and sums
; them into [wc_total]. The last cell of every band is the scroll column.

; -----------------------------------------------------------------------------
; wc_seg - the next segment of text: where it starts and how long it is
; in:  SI = the text from here, CX = the cells available
; out: SI = the segment, CX = its length (0 = the text is spent), DI = where
;      the next one starts. clobbers AX
;
; Greedy: the whole rest if it fits; else up to the last space that fits, the
; space itself dropped; else - one word longer than the line - cut at the
; line's end (SPEC.md 113.5).
; -----------------------------------------------------------------------------
wc_seg:
    push bx
    mov di, si
    xor bx, bx                          ; BX = the last space that fits, 0 none
.c:
    mov ax, di
    sub ax, si                          ; AX = characters so far
    cmp byte [di], 0
    je .all
    cmp ax, cx
    jae .full
    cmp byte [di], ' '
    jne .n
    mov bx, di
.n:
    inc di
    jmp short .c
.all:
    mov cx, ax                          ; the rest fits, up to the NUL
    pop bx
    ret
.full:
    cmp byte [di], ' '                  ; a line that ends exactly at a space:
    jne .back                           ; the break is right here
    lea di, [di+1]
    pop bx
    ret                                 ; (CX = the cells, all of them)
.back:
    or bx, bx                           ; ...else at the last space that fit,
    jz .cut                             ; unless it is the first character
    cmp bx, si
    je .cut
    mov cx, bx
    sub cx, si
    lea di, [bx+1]                      ; resume after the space
    pop bx
    ret
.cut:
    mov di, si                          ; one word longer than the line: cut
    add di, cx                          ; it where the line ends
    pop bx
    ret

; --- wc_mlines - message BX (an index): its line count, cached ------------------
; out: AX = the lines; clobbers CX, DX, SI, DI
wc_mlines:
    push bx
    call wc_mrec
    xor ax, ax
    mov al, [bx+WC_MLINES]
    or al, al
    jnz .out
    lea si, [bx+WC_MTEXT]
    call wc_w0                          ; CX = the first segment's cells
    xor dx, dx                          ; DX = the segments
.s:
    push dx
    call wc_seg
    pop dx
    inc dx
    mov si, di
    cmp byte [si], 0
    je .done
    call wc_wn
    jmp short .s
.done:
    mov ax, dx
    cmp ax, 255
    jb .st
    mov al, 255
.st:
    mov [bx+WC_MLINES], al
.out:
    pop bx
    xor ah, ah
    ret

; --- wc_tstart - BX = a record: CX = its first line's text column, past the
; nick. Preserves the rest
wc_tstart:
    push si
    lea si, [bx+WC_MNICK]
    call wc_slen
    pop si
    add cx, WC_TIME + 1
    cmp cx, WC_TCOL
    jae .out
    mov cx, WC_TCOL
.out:
    ret

; --- wc_w0 - BX = a record: CX = its first segment's cells. Preserves the
; rest. The text ends a cell short of the scroll column.
wc_w0:
    push ax
    call wc_tstart
    mov ax, [wc_cols]
    sub ax, 2
    sub ax, cx
    mov cx, ax
    pop ax
    cmp cx, 4
    jge .out
    mov cx, 4
.out:
    ret

; --- wc_wn - CX = the cells of every segment after the first ------------------
wc_wn:
    mov cx, [wc_cols]
    sub cx, 2 + WC_TCOL
    cmp cx, 4
    jge .out
    mov cx, 4
.out:
    ret

; --- wc_relines - every cache good, and [wc_total] their sum and the dividers'
; in: an invalidate first (wc_dirtylines) when the width or the style moved
; clobbers AX, BX, CX, DX, SI, DI
wc_relines:
    call wc_dayflags
    xor bx, bx
    xor dx, dx
.m:
    cmp bl, [wc_mn]
    jae .out
    push dx
    call wc_mtot
    pop dx
    add dx, ax
    inc bx
    jmp short .m
.out:
    mov [wc_total], dx
    ret

; --- wc_mtot - message BX (an index): ALL its lines - its day divider, the
; new-messages divider and its text. Preserves BX; clobbers CX, DX, SI, DI
wc_mtot:
    call wc_mlines
    cmp bl, [wc_divi]
    jne .d
    inc ax                              ; the divider is one line of its own
.d:
    push bx
    call wc_mrec
    test byte [bx+WC_MFLG], WCM_DAY
    pop bx
    jz .out
    inc ax                              ; ...and so is a new day
.out:
    ret

; --- wc_dayflags - WCM_DAY on each message whose LOCAL day is not the day of
; the one before it (SPEC.md 113.5): the first dated message has one, and a
; history from a server without dates has none. clobbers AX, BX, CX, DX
wc_dayflags:
    push di
    xor cx, cx                          ; CX = the day before
    xor di, di                          ; DI = the index
    mov bx, wc_msgs
.m:
    mov ax, di
    cmp al, [wc_mn]
    jae .out
    and byte [bx+WC_MFLG], ~WCM_DAY & 0xFF
    call wc_lday
    or ax, ax
    jz .n
    cmp ax, cx
    je .n
    or byte [bx+WC_MFLG], WCM_DAY
.n:
    mov cx, ax
    add bx, WC_MREC
    inc di
    jmp short .m
.out:
    pop di
    ret

; --- wc_dirtylines - forget every cached line count ---------------------------
wc_dirtylines:
    push bx
    push cx
    xor cx, cx
    mov cl, [wc_mn]
    mov bx, wc_msgs + WC_MLINES
    jcxz .out
.c:
    mov byte [bx], 0
    add bx, WC_MREC
    loop .c
.out:
    pop cx
    pop bx
    ret

; --- wc_maxtop - AX = the largest top line from which every line to the end
; is on the glass: wc_play's rule - a band whole, and a message's last line
; with its gap - counted back from the end. Preserves the rest
wc_maxtop:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, [wc_ph]                     ; DI = the rows still free
    xor si, si                          ; SI = the lines they hold, from the end
    xor bx, bx
    mov bl, [wc_mn]
.m:
    or bx, bx
    jz .done
    dec bx
    push si
    push di
    call wc_mtot                        ; AX = message BX's lines...
    pop di
    pop si
    mov cx, ax
    mov dx, WC_LH
    mul dx
    add ax, [wc_gap]                    ; ...and the rows they take
    cmp ax, di
    ja .part
    sub di, ax
    add si, cx
    jmp short .m
.part:
    mov ax, di                          ; the end of it that fits: its last
    sub ax, [wc_gap]                    ; line carries the gap
    jb .done
    xor dx, dx
    mov cx, WC_LH
    div cx
    add si, ax
.done:
    mov ax, [wc_total]
    sub ax, si
    jge .out
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- wc_sbcalc - the scroll column's thumb from [wc_total], wc_maxtop and
; [wc_top]: between the arrows, as tall as the lines the end shows are a share
; of them all, and as far down as the top is of the way to the end. Nothing
; when it all fits. clobbers AX, BX, CX, DX
wc_sbcalc:
    mov byte [wc_sbon], 0
    mov bx, [wc_srows]
    cmp bx, 3
    jb .out
    call wc_maxtop                      ; AX = the lines the top moves over
    or ax, ax
    jz .out
    mov cx, ax
    sub bx, 2                           ; BX = the track
    mov ax, [wc_total]
    sub ax, cx                          ; the lines the end shows
    mul bx
    div word [wc_total]                 ; AX = the thumb
    or ax, ax
    jnz .s1
    inc ax
.s1:
    cmp ax, bx
    jbe .s2
    mov ax, bx
.s2:
    mov [wc_sbn], ax
    sub bx, ax                          ; BX = the room it moves in
    mov ax, [wc_top]
    mul bx
    div cx
    inc ax
    mov [wc_sbt], ax
    mov byte [wc_sbon], 1
.out:
    ret

; --- wc_sbglyph - CX = a scroll column cell -> BL = its glyph ---------------
wc_sbglyph:
    mov bl, GL_BLANK
    cmp byte [wc_sbon], 0
    je .out
    mov bl, GL_SBUP
    jcxz .out
    mov bl, GL_SBDN
    push ax
    mov ax, [wc_srows]
    dec ax
    cmp cx, ax
    pop ax
    je .out
    mov bl, GL_SBTRK
    cmp cx, [wc_sbt]
    jb .out
    push ax
    mov ax, [wc_sbt]
    add ax, [wc_sbn]
    cmp cx, ax
    pop ax
    jae .out
    mov bl, GL_SBTHM
.out:
    ret

; -----------------------------------------------------------------------------
; wc_ppane - the whole pane, from [wc_top], with its scroll column
;
; THE BANDS ARE NOT A GRID (SPEC.md 113.5.1): a message's last line carries
; [wc_gap] rows of the pane's ground under it, so wc_play lays the bands out
; from the top each pass and wc_gby is the same table as the glass has it.
; A band whose place and height the glass already has is drawn through its
; slot as before; one that moved is owed, drawn whole, and its gap filled;
; and whatever the glass had past the last band is ground again.
; -----------------------------------------------------------------------------
wc_ppane:
    call wc_dirtylines                  ; the width may have moved since
    call wc_relines
    call wc_maxtop                      ; a pane that shrank or a store that
    cmp byte [wc_tobot], 0              ; emptied cannot leave the top past
    je .clamp                           ; the end - and a fresh history opens
    cmp byte [wc_mn], 0                 ; at its last line, once it has one
    je .clamp
    mov byte [wc_tobot], 0
    mov [wc_top], ax
.clamp:
    cmp [wc_top], ax
    jbe .t
    mov [wc_top], ax
.t:
    call wc_sbcalc
    call wc_play                        ; the bands, from the top
    ; --- the lines on the glass, moved to where they now belong -------------
    ; [wc_dtop] is the top the glass shows. While some of what it shows stays
    ; on it, OSAPI_GFX_SCROLL moves the [wc_gused] rows that are not ground,
    ; and the slots and the glass's table move with them (SPEC.md 113.5.1)
    mov ax, [wc_top]
    cmp word [wc_dtop], 0xFFFF
    je .rows
    cmp byte [wc_pop], PP_NONE          ; a popup over the pane would move with
    jne .rows                           ; it
    sub ax, [wc_dtop]                   ; AX = the lines, + up
    jz .rows
    jns .up
    neg ax
    cmp ax, [wc_rows]                   ; down: the lines that come in are the
    ja .rows                            ; top bands, as wc_play laid them
    mov si, ax
    shl si, 1
    mov si, [wc_pby+si]
    neg si
    neg ax
    jmp short .mv
.up:
    cmp ax, [wc_grows]                  ; up: the ones that go out are the
    ja .rows                            ; glass's top bands
    mov si, ax
    shl si, 1
    mov si, [wc_gby+si]                 ; SI = the rows, + up
.mv:
    mov cx, si
    or cx, cx
    jns .abs
    neg cx
.abs:
    cmp cx, [wc_gused]                  ; nothing would stay
    jae .rows
    push ax
    push si
    mov ax, [wc_tx]
    add ax, [wc_ox]
    mov cx, [wc_cols]                   ; the text's cells: the scroll column
    dec cx                              ; is on a grid of its own
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx                              ; x2 + 1 is a multiple of 8: tx is
    mov bx, [wc_py1]                    ; and so is a whole number of cells
    add bx, [wc_oy]
    mov dx, bx
    add dx, [wc_gused]
    dec dx
    call OSAPI_GFX_SCROLL
    pop si
    pop ax
    jc .rows                            ; refused: nothing moved, and the rows
    call wc_pshift                      ; draw what differs
    call wc_gshift
.rows:
    mov ax, [wc_top]
    mov [wc_dtop], ax
    xor cx, cx
.r:
    cmp cx, [wc_rows]
    jae .tail
    push cx
    call wc_gsame                       ; where the glass has it?
    jnc .same
    mov ax, cx                          ; no: owed, drawn whole, and its gap
    add ax, WCSL_PANE
    call wc_slot
    or bx, bx
    jz .whole
    mov byte [bx+WCX_NSP], 0
.whole:
    call wc_prow
    pop cx
    push cx
    call wc_pgap
    jmp short .nx
.same:
    call wc_prow
.nx:
    pop cx
    inc cx
    jmp short .r
.tail:
    mov si, [wc_rows]                   ; past the last band: ground, which
    shl si, 1                           ; the glass is beyond [wc_gused]
    mov bx, [wc_pby+si]
    mov dx, [wc_gused]
    mov [wc_gused], bx
    cmp bx, dx
    jae .copy
    cmp byte [wc_fresh], 0
    jne .copy
    dec dx
    call wc_pfill
.copy:
    mov cx, [wc_rows]                   ; the glass has this layout now
    mov [wc_grows], cx
    inc cx
    mov si, wc_pby
    mov di, wc_gby
.cp:
    lodsw
    mov [di], ax
    inc di
    inc di
    loop .cp
    jmp wc_psbc

; --- wc_play - the bands from [wc_top]: band N shows line top + N at
; wc_pby[N] (pane-relative), WC_LH rows - and, under a message's last line,
; [wc_gap] rows of the pane's ground, the air between two messages whoever
; sent them. A band is laid only whole, its gap with it. out: [wc_rows] and
; wc_pby[0..rows], the last its end. clobbers AX, BX, CX, DX, SI, DI
wc_play:
    xor ax, ax
    mov [wc_pby], ax
    mov [wc_rows], ax
    mov ax, [wc_top]
    call wc_lfind                       ; BX = the message, CX = the part
    jc .out
.m:
    push cx
    call wc_mtot                        ; AX = its lines
    pop cx
    mov dx, ax
.l:
    mov si, [wc_rows]
    cmp si, WC_MAXB
    jae .out
    shl si, 1
    mov ax, [wc_pby+si]
    add ax, WC_LH
    inc cx
    cmp cx, dx
    jb .fit                             ; not its last line
    add ax, [wc_gap]
.fit:
    cmp ax, [wc_ph]
    ja .out
    mov [wc_pby+si+2], ax
    inc word [wc_rows]
    cmp cx, dx
    jb .l
    xor cx, cx
    inc bx
    cmp bl, [wc_mn]
    jb .m
.out:
    ret

; --- wc_gsame - CF = 0 when band CX's place and height are the glass's
; (wc_gby). Preserves CX; clobbers AX, BX
wc_gsame:
    cmp cx, [wc_grows]
    jae .no
    mov bx, cx
    shl bx, 1
    mov ax, [wc_pby+bx]
    cmp ax, [wc_gby+bx]
    jne .no
    mov ax, [wc_pby+bx+2]
    cmp ax, [wc_gby+bx+2]
    jne .no
    clc
    ret
.no:
    stc
    ret

; --- wc_gshift - the glass moved AX bands and SI rows (signed, + up) inside
; the [wc_gused] rows OSAPI_GFX_SCROLL was given: its table moves with them,
; keeping the bands still whole inside those rows. Down, the first AX are the
; ones coming in and match nothing. clobbers AX, BX, CX, DX, DI
wc_gshift:
    mov cx, [wc_grows]
    or ax, ax
    js .down
    sub cx, ax                          ; CX = the bands that stay
    mov [wc_grows], cx
    mov bx, ax
    shl bx, 1                           ; from the first of them...
    xor di, di
    inc cx                              ; ...with their end
.u:
    mov ax, [wc_gby+bx]
    sub ax, si
    mov [wc_gby+di], ax
    inc bx
    inc bx
    inc di
    inc di
    loop .u
    ret
.down:
    neg ax
    mov dx, ax                          ; DX = the bands
    mov bx, cx                          ; BX = an entry, from the end
.d:
    mov di, bx
    add di, dx
    cmp di, WC_MAXB
    ja .dn                              ; past the table
    shl di, 1
    push bx
    shl bx, 1
    mov ax, [wc_gby+bx]
    pop bx
    sub ax, si
    mov [wc_gby+di], ax
.dn:
    dec bx
    jns .d
    add cx, dx                          ; CX = the most there can be now
    cmp cx, WC_MAXB
    jbe .w0
    mov cx, WC_MAXB
.w0:
    mov bx, dx                          ; the bands still whole, from AX on
.w:
    cmp bx, cx
    jae .wd
    mov di, bx
    shl di, 1
    mov ax, [wc_gby+di+2]
    cmp ax, [wc_gused]
    ja .wd
    inc bx
    jmp short .w
.wd:
    mov [wc_grows], bx
    mov cx, dx
    xor di, di
.f:
    mov word [wc_gby+di], 0xFFFF
    inc di
    inc di
    loop .f
    ret

; --- wc_pgap - band CX's gap, in the pane's ground, unless a full paint has
; just laid it. clobbers AX, BX, CX, DX
wc_pgap:
    cmp byte [wc_fresh], 0
    jne .out
    mov bx, cx
    shl bx, 1
    mov dx, [wc_pby+bx+2]
    mov bx, [wc_pby+bx]
    add bx, WC_LH                       ; BX = the gap's first row
    cmp bx, dx
    jae .out
    dec dx
    jmp short wc_pfill
.out:
    ret

; --- wc_pfill - the pane's ground across its text's cells, rows BX..DX
; (pane-relative, inclusive). clobbers AX, BX, CX, DX
wc_pfill:
    mov ax, [wc_py1]
    add ax, [wc_oy]
    add bx, ax
    add dx, ax
    mov al, R_BG
    call wc_penc
    mov ax, [wc_tx]
    add ax, [wc_ox]
    mov cx, [wc_cols]
    dec cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx
    call OSAPI_GFX_FILL
    ret

; --- wc_psbc - the scroll column: a glyph a cell on a grid of its own, WC_LH
; apart from the pane's top, each drawn only when it is not the one the glass
; has (wc_sbg). The rows round the glyphs are ground and nothing else draws
; there: the bands stop a cell short, and OSAPI_GFX_SCROLL is given the text.
; clobbers AX, BX, CX, DX, SI, DI
wc_psbc:
    xor di, di                          ; DI = the cell
.c:
    cmp di, [wc_srows]
    jae .out
    mov cx, di
    call wc_sbglyph                     ; BL = its glyph
    cmp bl, [wc_sbg+di]
    je .n
    mov [wc_sbg+di], bl
    mov al, R_BG
    call wc_pen
    mov ah, al                          ; AH = the ground
    xor bh, bh
    mov al, [wc_glrole+bx]
    call wc_pen                         ; AL = the glyph's ink
    push ax
    mov ax, WC_LH
    mul di
    mov dx, ax
    pop ax
    add dx, [wc_py1]
    add dx, [wc_oy]
    inc dx                              ; its eight rows inside the cell's ten
    mov cx, [wc_cols]
    dec cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, [wc_tx]
    add cx, [wc_ox]
    call wc_glyphat
.n:
    inc di
    jmp short .c
.out:
    ret

; -----------------------------------------------------------------------------
; wc_ptail - what was appended: at the bottom the pane follows the end, and
;            reading further up it stays where it is and only the scroll
;            column says there is more. wc_ppane moves what can be moved and
;            draws only what the slots say is new.
; -----------------------------------------------------------------------------
wc_ptail:
    call wc_maxtop                      ; the end, before the new lines
    cmp [wc_top], ax
    jb .read
    mov byte [wc_tobot], 1              ; at it: follow it
.read:
    jmp wc_ppane

; -----------------------------------------------------------------------------
; wc_prow - draw pane band CX (0 = the top one), which shows line top + CX,
;           at wc_pby[CX], through its slot - its text, a cell short of the
;           scroll column (wc_psbc's); its gap is wc_ppane's
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_prow:
    mov [wc_rband], cx
    mov bx, cx
    shl bx, 1
    mov ax, [wc_pby+bx]
    add ax, [wc_py1]
    mov [wc_rowy], ax                   ; the band's top, content-relative
    mov ax, [wc_top]
    add ax, cx                          ; AX = the line
    call wc_rclr
    call wc_sbclr
    call wc_lfind                       ; BX = the message, CX = the part, CF none
    jc .out
    ; --- a new day? -------------------------------------------------------
    push bx
    call wc_mrec
    test byte [bx+WC_MFLG], WCM_DAY
    mov [wc_rrec], bx
    pop bx
    jz .noday
    or cx, cx
    jnz .afterday
    jmp .dayline
.afterday:
    dec cx
.noday:
    ; --- the divider? -----------------------------------------------------
    cmp bl, [wc_divi]
    jne .nodiv
    or cx, cx                           ; part 0 of the message the divider
    jnz .after                          ; sits on is the divider (jcxz is
    jmp .divider                        ; short-only, and it is far)
.after:
    dec cx
.nodiv:
    ; CX = the segment. The first line carries the selection or the mention
    push bx
    call wc_mrec                        ; BX = the record
    mov [wc_rrec], bx
    pop ax
    jcxz .first
    push cx                             ; a later line: under the text column
    mov cx, WC_TCOL
    call wc_sbto
    pop cx
    jmp short .segment
.first:
    cmp al, [wc_sel]
    jne .ns
    mov byte [wc_rinv], WS_INV
    jmp short .head
.ns:
    mov bx, [wc_rrec]
    test byte [bx+WC_MFLG], WCM_MENT
    jz .head
    mov byte [wc_rinv], WS_MENT
.head:
    call wc_phead                       ; 'hh:mm nick', padded to its text
.segment:
    ; segment CX of the text, at the composition's end
    push di
    mov bx, [wc_rrec]
    lea si, [bx+WC_MTEXT]
    mov dx, cx                          ; DX = segments to skip
    call wc_w0
.skip:
    push dx
    call wc_seg
    pop dx
    or dx, dx
    jz .take
    dec dx
    mov si, di
    call wc_wn
    jmp short .skip
.take:
    pop di
    jcxz .out
    push di                             ; where the segment starts
.copy:
    lodsb
    call wc_sbch
    loop .copy
    pop si
    call wc_emocol
    jmp short .out
.divider:
    mov byte [wc_rinv], WS_ALERT
    call wc_sbdivider
    jmp short .out
.dayline:
    mov byte [wc_rinv], WS_DIM
    call wc_sbdayline
.out:
    mov cx, [wc_cols]                   ; to the scroll column, which is
    dec cx                              ; wc_psbc's
    call wc_sbto
    mov cx, [wc_tx]
    mov dx, [wc_rowy]
    mov ax, [wc_rband]
    add ax, WCSL_PANE
    cmp word [wc_rband], WC_MAXB
    jb .slot
    mov ax, 0xFFFF
.slot:
    jmp wc_rowout

; --- wc_phead - a first line's 'hh:mm nick', the nick bold, padded to where
; its text starts; the time dim and the nick in its own colour unless the
; line is the highlight's or the mention's
wc_phead:
    push cx
    call wc_sbtime
    cmp byte [wc_rinv], WS_NORM
    jne .nick
    xor cl, cl
    mov al, R_DIM
    call wc_pen
    mov ah, 0xFF
    call wc_rspan
    mov cl, WC_TIME
    mov si, [wc_rrec]
    add si, WC_MNICK
    call wc_nickcol                     ; AL = the nick's colour
    mov ah, 0xFF
    call wc_rspan
.nick:
    mov si, [wc_rrec]
    add si, WC_MNICK
    mov [wc_rbold], si
    mov byte [wc_rbcol], WC_TIME
    call wc_sbstr
    mov bx, [wc_rrec]
    call wc_tstart
    call wc_sbto
    cmp byte [wc_rinv], WS_NORM
    jne .out
    mov ax, 0xFFFF                      ; the text in the text's colour
    call wc_rspan
.out:
    pop cx
    ret

; --- wc_emocol - Wired on a colour display: each emoticon the picker offers in
; the segment [SI, DI) of wc_sbuf is a span in its own colour (wc_emoink),
; the text's after it - on any line but the highlight's, while the band has
; spans to spare (two, and the scroll column's). One inside a word is not one
; (':Pa', 'foo_O'). Preserves all.
wc_emocol:
    cmp byte [wc_tint], 0
    je .ret
    cmp byte [wc_rinv], WS_INV
    je .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, di                          ; DX = the segment's end
    mov di, si                          ; DI = the cell tried
.at:
    cmp di, dx
    jae .out
    cmp di, wc_sbuf
    je .try
    mov al, [di-1]
    call wc_nickch                      ; CF = 0: a word's character before
    jnc .next
.try:
    xor cx, cx                          ; CX = the emoticon
.e:
    mov bx, cx
    shl bx, 1
    mov si, [wc_emos+bx]
    xor bx, bx                          ; BX = its characters matched
.c:
    lodsb
    or al, al
    jz .end
    push ax
    lea ax, [bx+di]
    cmp ax, dx
    pop ax
    jae .miss
    cmp al, [bx+di]
    jne .miss
    inc bx
    jmp short .c
.end:
    lea si, [bx+di]                     ; ...and none after
    cmp si, dx
    jae .hit
    mov al, [si]
    call wc_nickch
    jnc .miss
.hit:
    cmp byte [wc_rspn], WC_NSPAN - 4
    ja .out
    push cx
    mov si, cx
    mov ax, di
    sub ax, wc_sbuf
    mov cl, al                          ; CL = its cell
    mov al, [wc_emoink+si]
    mov ah, 0xFF
    call wc_rspan
    add cl, bl
    mov ax, 0xFFFF                      ; the text's after it
    call wc_rspan
    pop cx
    add di, bx
    jmp short .at
.miss:
    inc cx
    cmp cx, PP_EMON
    jb .e
.next:
    inc di
    jmp short .at
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; --- wc_nickcol - AL = the theme's ink for the nick at SI: one of four, by a
; sum of its letters - so a nick is always the same colour, on every machine
; (SPEC.md 113.5.2). Preserves the rest.
wc_nickcol:
    push bx
    push si
    xor bx, bx
.c:
    lodsb
    or al, al
    jz .d
    call wc_lower
    add bl, al
    jmp short .c
.d:
    and bl, 3
    mov al, [wc_pens+R_NICK+bx]
    pop si
    pop bx
    ret

; --- wc_sbtime - 'h:mm ' for the record at [wc_rrec], local, right-aligned ---
wc_sbtime:
    mov bx, [wc_rrec]
    mov ax, [bx+WC_MMIN]
    add ax, [wc_tzo]
    add ax, 1440
    xor dx, dx
    mov cx, 1440
    div cx
    mov ax, dx                          ; the local minute of the day
    xor dx, dx
    mov cx, 60
    div cx                              ; AX = the hour, DX = the minute
    call wc_sbhm
    mov al, ' '
    jmp wc_sbch

; --- wc_sbhm - append hour AX, minute DX as 'h:mm', right-aligned in five
; cells, on the clock's 12- or 24-hour choice. clobbers AX, CX, DX
wc_sbhm:
    cmp byte [wc_h12], 0
    je .h
    push dx
    xor dx, dx
    mov cx, 12
    div cx
    mov ax, dx
    pop dx
    or ax, ax
    jnz .h
    mov ax, 12
.h:
    push dx
    cmp ax, 10
    jae .two
    push ax
    mov al, ' '
    call wc_sbch
    pop ax
.two:
    call wc_sbnum
    mov al, ':'
    call wc_sbch
    pop ax
    jmp wc_sb2

; --- wc_sbdayline - '---- Tue Oct 20 ----' across the pane, for [wc_rrec] ---
wc_sbdayline:
    mov al, '-'
    mov cx, 4
.l:
    call wc_sbch
    loop .l
    mov al, ' '
    call wc_sbch
    mov bx, [wc_rrec]
    call wc_lday
    call wc_sbday
    mov al, ' '
    call wc_sbch
    jmp short wc_sbdivider.r

; --- wc_sbdivider - '---- N new messages ----' across the pane --------------
wc_sbdivider:
    mov al, '-'
    mov cx, 4
.l:
    call wc_sbch
    loop .l
    mov al, ' '
    call wc_sbch
    mov al, [wc_divn]
    xor ah, ah
    call wc_sbnum
    mov si, wc_s_new1
    cmp byte [wc_divn], 1
    je .w
    mov si, wc_s_newn
.w:
    call wc_sbstr
.r:
    mov ax, di
    sub ax, wc_sbuf
    inc ax
    inc ax
    cmp ax, [wc_cols]
    jae .out
    mov al, '-'
    call wc_sbch
    jmp short .r
.out:
    ret

; -----------------------------------------------------------------------------
; wc_lfind - which message, and which of its lines, is line AX?
; out: CF = 0, BX = the message's index, CX = the part (0 = its day divider
;      when it has one, then its new-messages divider when it has that, then
;      its segments); CF = 1 past the end. clobbers AX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_lfind:
    xor bx, bx
.m:
    cmp bl, [wc_mn]
    jae .no
    push ax
    call wc_mtot                        ; AX = its lines (cached)
    mov cx, ax
    pop ax
    cmp ax, cx
    jb .yes
    sub ax, cx
    inc bx
    jmp short .m
.yes:
    mov cx, ax
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; THE POPUPS (SPEC.md 113.6.2): a list drawn over the content. A press inside
; picks; a press anywhere else - or the About card - dismisses.
; =============================================================================
WC_POPW     equ 12                      ; cells

; --- wc_popgeom - the open popup's rect into wc_poprc, and its row count -----
; out: CX = rows; clobbers AX, BX, DX
wc_popgeom:
    mov cx, 3
    cmp byte [wc_pop], PP_STATUS
    je .st
    mov cx, PP_EMON
    ; above Send, its right edge on Send's
    mov ax, [wc_brects + 8 * WB_SEND + 4]
    sub ax, WC_POPW * 8 + 3
    mov bx, [wc_brects + 8 * WB_SEND + 2]
    push cx
    mov dx, WC_LH
    xchg ax, cx
    mul dx
    xchg ax, cx
    sub bx, cx
    sub bx, 5
    pop cx
    jmp short .put
.st:
    ; below the header's status, its right edge on the status's
    mov ax, [wc_brects + 8 * WB_STATUS + 4]
    sub ax, WC_POPW * 8 + 3
    mov bx, [wc_brects + 8 * WB_STATUS + 6]
    add bx, 2
.put:
    and ax, ~7                          ; the rows on the byte grid
    mov [wc_poprc+0], ax
    mov [wc_poprc+2], bx
    add ax, WC_POPW * 8 + 3
    mov [wc_poprc+4], ax
    push cx
    mov ax, WC_LH
    mul cx
    add ax, bx
    add ax, 3
    mov [wc_poprc+6], ax
    pop cx
    ret

; --- wc_popdraw - the open popup, if one is ------------------------------------
; It is drawn OVER what is there, so its ground is filled first - the one
; place this file fills and then letters, and it is a popup: it appears once
; per click and is gone by the next.
wc_popdraw:
    cmp byte [wc_pop], PP_NONE
    je .out
    call wc_popgeom
    push cx
    mov al, R_BG
    call wc_penc
    mov ax, [wc_poprc+0]
    mov bx, [wc_poprc+2]
    mov cx, [wc_poprc+4]
    mov dx, [wc_poprc+6]
    call OSAPI_GFX_FILL
    mov al, R_ACC
    call wc_penc
    mov ax, [wc_poprc+0]
    mov cx, [wc_poprc+4]
    call OSAPI_GFX_FRAME
    pop cx
    xor bx, bx
.r:
    cmp bx, cx
    jae .out
    push bx
    push cx
    call wc_popitem                     ; SI = item BX's words
    push si
    call wc_sbclr
    pop si
    call wc_sbstr
    mov cx, WC_POPW - 1
    call wc_sbto
    pop cx
    pop bx
    push bx
    push cx
    call wc_poprow                      ; CX, DX = the band, content-relative
    xor al, al
    call wc_mreset
    call wc_emotint
    push bx
    xor bx, bx                          ; no slot: a popup is drawn whole
    call wc_bandc
    pop bx
    pop cx
    pop bx
    inc bx
    jmp short .r
.out:
    ret

; --- wc_emotint - Wired on a colour display: the picker's row BX in its
; emoticon's colour, the one wc_emocol gives it in a message. Preserves all
wc_emotint:
    cmp byte [wc_tint], 0
    je .out
    cmp byte [wc_pop], PP_EMO
    jne .out
    push ax
    push cx
    mov al, [wc_emoink+bx]
    mov ah, 0xFF
    xor cl, cl                          ; span 0, re-coloured
    call wc_span
    pop cx
    pop ax
.out:
    ret

; --- wc_popitem - BX = a row of the open popup -> SI = its words ------------
wc_popitem:
    push bx
    shl bx, 1
    mov si, [wc_stwords+bx]
    cmp byte [wc_pop], PP_STATUS
    je .out
    mov si, [wc_emos+bx]
.out:
    pop bx
    ret

; --- wc_poprow - BX = a row -> CX, DX = its band, content-relative ---------
; The rows sit one cell in from the frame (the rect is on the byte grid, so
; the run is too) and two rows down.
wc_poprow:
    push ax
    mov ax, WC_LH
    mul bx
    add ax, [wc_poprc+2]
    sub ax, [wc_oy]
    add ax, 2
    mov dx, ax
    mov cx, [wc_poprc+0]
    sub cx, [wc_ox]
    add cx, 8
    pop ax
    ret
