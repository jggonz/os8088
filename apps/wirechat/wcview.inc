; =============================================================================
; os8088 - apps/wirechat/wcview.inc
;
; THE LAYOUT AND EVERY PAINTER (SPEC.md 113.5).
;
; wc_geom is the ONE place the layout is computed, and every painter and every
; hit test calls it first - The Wire's wr_geom, for its reason: a window moves
; and resizes, and a painter and a hit test that hold two opinions about where
; a row is are a click that lands on the wrong room.
;
; EVERY ROW IS A BAND: WC_LH = 10 rows, a glyph row of 8 with one row of air
; above and below. wc_band letters the run with ONE opaque OSAPI_FONT_RUN,
; padded to the region's width, and fills the two air rows in the band's
; ground - so a band repaints every pixel it owns exactly once, and a row that
; was inverted (a selection, a room) and no longer is leaves nothing behind
; (PERFORMANCE.md rules 1 and 2). Bold is SPEC.md 6.6.2 case 6, an overstrike
; at x+1 (tests/textsites.txt).
;
; THE DIRTY BITS (WCD_*). A state change ORs the bits for the regions it
; changed; wc_flush paints exactly those, under a lock its caller holds.
; W_PAINT is the one painter that paints everything.
; =============================================================================

; -----------------------------------------------------------------------------
; wc_geom - where everything is, this pass (SPEC.md 113.5)
; out: CF = 1 not visible and nothing stored; else every wc_* geometry word,
;      the button rects, the field's rect and the scroll bar's block.
;      Preserves every register.
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
    ; --- the columns ---------------------------------------------------------
    mov cx, [wc_cw]
    xor ax, ax
    cmp cx, WC_WLEFT
    jb .nol
    mov ax, WC_LPW
.nol:
    mov [wc_lx2], ax
    xor ax, ax
    cmp cx, WC_WRIGHT
    jb .nor
    mov ax, cx
    sub ax, WC_RPW
    and ax, ~7
.nor:
    mov [wc_rx1], ax
    mov ax, [wc_lx2]                    ; the center: one past the left rule...
    or ax, ax
    jz .m1
    inc ax
.m1:
    mov [wc_mx1], ax
    mov ax, [wc_rx1]                    ; ...to one short of the right one
    or ax, ax
    jnz .m2
    mov ax, cx
.m2:
    dec ax
    mov [wc_mx2], ax
    ; --- the rows ------------------------------------------------------------
    mov byte [wc_topic], 0
    mov ax, WC_HDRH + 3                 ; the title's band
    mov [wc_ty], ax
    add ax, WC_LH + 4                   ; one band and its rule, and clear of
    cmp word [wc_ch], WC_HTOPIC         ; the Search button's last row - the
    jb .tp                              ; pane's first band drawn over it took
    mov byte [wc_topic], 1              ; the button's bottom edge away...
    add ax, WC_LH + WC_TOPICDY - 1      ; ...or two, the second clear of the
.tp:                                    ; Search button so it has the width
    mov [wc_py1], ax
    mov ax, [wc_ch]
    sub ax, WC_STH
    mov [wc_sy], ax
    sub ax, WC_INH
    mov [wc_iy], ax
    sub ax, WC_LH
    mov [wc_ny], ax
    sub ax, [wc_py1]                    ; the pane's bands
    xor dx, dx
    mov cx, WC_LH
    div cx
    cmp ax, 1
    jge .rw
    mov ax, 1
.rw:
    mov [wc_rows], ax
    mov byte [wc_compact], 0
    cmp ax, WC_COMPACT
    jae .cm
    mov byte [wc_compact], 1
.cm:
    mov ax, [wc_mx1]                    ; the text column: a little clear of
    add ax, 4 + 7                       ; the rule, then onto the byte grid
    and ax, ~7
    mov [wc_tx], ax
    mov cx, [wc_mx2]                    ; ...and its width: to the scroll bar
    sub cx, WC_SBW + 1
    sub cx, ax
    inc cx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp cx, WC_TIME + 8                 ; MINSIZE keeps it well above this;
    jge .cl                             ; the clamp is for the arithmetic
    mov cx, WC_TIME + 8
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
; wc_layout - the button rects, the field and the scroll bar, ABSOLUTE
; (wc_geom's tail). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_layout:
    mov di, wc_brects
    ; --- Set status... : the header's right end -------------------------------
    mov cx, [wc_cw]
    sub cx, 4
    mov ax, cx
    sub ax, 13 * 8 + 12 - 1
    mov bx, 1
    call wc_rect
    ; --- Search : the title's right end ----------------------------------------
    mov cx, [wc_mx2]
    sub cx, 4
    mov ax, cx
    sub ax, 6 * 8 + 12 - 1
    mov bx, [wc_ty]
    dec bx
    call wc_rect
    ; --- :) and Send : the input row's right end -------------------------------
    mov cx, [wc_mx2]
    sub cx, 4
    mov ax, cx
    sub ax, 4 * 8 + 12 - 1
    push ax
    mov bx, [wc_iy]
    inc bx
    inc bx
    sub ax, 2 * 8 + 12 + 4
    mov si, ax
    mov cx, si
    add cx, 2 * 8 + 12 - 1
    mov ax, si
    call wc_rect                        ; :)
    pop ax
    mov cx, ax
    add cx, 4 * 8 + 12 - 1
    mov bx, [wc_iy]                     ; wc_rect spent BX
    inc bx
    inc bx
    call wc_rect                        ; Send
    ; --- + Add friend : the left pane's foot, or nowhere -----------------------
    mov ax, 4
    mov cx, [wc_lx2]
    sub cx, 5
    mov bx, [wc_sy]
    sub bx, WC_BTNH + 3
    cmp word [wc_lx2], 0
    jne .fr
    mov ax, 1                           ; a rect with x1 > x2: no press can
    xor cx, cx                          ; land in it, and wc_paint draws no
.fr:                                    ; button for it
    call wc_rect
    ; --- the field: from the text column (after a prompt) to the :) button ----
    mov ax, [wc_tx]
    mov bl, [wc_mode]
    or bl, bl
    jz .np
    xor bh, bh
    shl bx, 1
    mov si, [wc_prompts+bx]
    push ax
    call wc_slen
    shl cx, 1
    shl cx, 1
    shl cx, 1
    pop ax
    add ax, cx
.np:
    add ax, [wc_ox]
    mov [wc_ln+LN_X1], ax
    mov ax, [wc_brects + 8 * WB_EMO + 0]
    sub ax, 5
    mov [wc_ln+LN_X2], ax
    mov ax, [wc_iy]
    add ax, [wc_oy]
    inc ax
    inc ax
    mov [wc_ln+LN_Y1], ax
    add ax, WC_FLDH - 1
    mov [wc_ln+LN_Y2], ax
    ; --- the scroll bar: the pane's right edge, as tall as its bands ----------
    mov ax, [wc_mx2]
    add ax, [wc_ox]
    mov [wc_sb+4], ax
    sub ax, WC_SBW - 1
    mov [wc_sb+0], ax
    mov ax, [wc_py1]
    add ax, [wc_oy]
    mov [wc_sb+2], ax
    mov cx, [wc_rows]
    mov dx, WC_LH
    xchg ax, cx
    mul dx
    add ax, cx
    dec ax
    mov [wc_sb+6], ax
    ret

; --- wc_rect - store the content-relative rect AX,BX..CX,(BX+WC_BTNH-1) at DI
; as an ABSOLUTE one, DI advanced. clobbers AX, BX, CX, DX
wc_rect:
    add ax, [wc_ox]
    mov [di], ax
    mov dx, bx
    add bx, [wc_oy]
    mov [di+2], bx
    add cx, [wc_ox]
    mov [di+4], cx
    add bx, WC_BTNH - 1
    mov [di+6], bx
    add di, 8
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

; =============================================================================
; THE BAND (see the header)
; =============================================================================
; wc_sb* compose into wc_sbuf; wc_band draws it.

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

; -----------------------------------------------------------------------------
; wc_band - draw wc_sbuf as one band in style AL, remembered nowhere (the
;           popups and the field's prompt, which are drawn whole or not at
;           all). wcband.inc's wc_bandc is the band; this is its plain call.
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

GL_PERSON   equ 0
GL_ONLINE   equ 1
GL_AWAY     equ 2
GL_BUSY     equ 3
GL_GONE     equ 4
GL_PLUG     equ 5
GL_CUT      equ 6
GL_LED      equ 7                       ; + LED_*: wcband.inc's lamps

; --- wc_stglyph - AL = a status (ST_*) -> BL = its glyph --------------------
wc_stglyph:
    mov bl, al
    add bl, GL_ONLINE
    ret
%if GL_AWAY - GL_ONLINE != ST_AWAY || GL_BUSY - GL_ONLINE != ST_BUSY || GL_GONE - GL_ONLINE != ST_GONE
  %error "wc_stglyph maps ST_* onto GL_* by an add"
%endif

; =============================================================================
; W_PAINT, and the flush
; =============================================================================

; -----------------------------------------------------------------------------
; wc_paint - W_PAINT: everything (the content is already white, the clip armed)
; in:  SI = window ptr. out: SI kept; the rest as W_PAINT may
; -----------------------------------------------------------------------------
wc_paint:
    push si
    call wc_hire                        ; the worker, once (SPEC.md 20.6)
    call wc_face                        ; mono or colour, and every slot owed
    call wc_geom
    jc .out
    call wc_rules
    mov word [wc_dirty], WCD_ALL & ~WCD_TAIL
    call wc_flush0                      ; every region, no popup, no card
    call wc_buttons
    cmp byte [wc_abon], 0
    je .pop
    mov bx, [wc_win]
    mov si, wc_ablines
    call os88ui_about_d                 ; the card LAST, over what it hides
    jmp short .out
.pop:
    call wc_popdraw
.out:
    pop si
    ret

; --- wc_rules - the header's, the panes' and the status bar's rules ---------
; On the colour face the header and the status bar are a grey STRIP first -
; a full paint's decoration, which its bands then letter over (SPEC.md
; 113.5.2) - and the rules are dark grey.
wc_rules:
    mov al, CBLACK
    cmp byte [wc_col], 0
    je .ink
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [wc_ox]
    mov bx, [wc_oy]
    mov cx, ax
    add cx, [wc_cw]
    dec cx
    mov dx, bx
    add dx, WC_HDRH - 1
    call OSAPI_GFX_FILL
    mov bx, [wc_oy]
    add bx, [wc_sy]
    inc bx
    mov dx, [wc_oy]
    add dx, [wc_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov ax, [wc_rx1]                    ; MEMBERS' strip runs back to its rule
    or ax, ax
    jz .nr
    add ax, [wc_ox]
    inc ax
    mov cx, ax
    add cx, 6
    mov bx, [wc_oy]
    add bx, WC_HDRH + 3
    mov dx, bx
    add dx, WC_LH - 1
    call OSAPI_GFX_FILL
.nr:
    mov al, CDGRAY
.ink:
    call OSAPI_SET_COLOR
    mov dx, WC_HDRH                     ; the header's
    call wc_hrule
    mov dx, [wc_sy]                     ; the status bar's
    call wc_hrule
    mov ax, [wc_lx2]
    or ax, ax
    jz .r
    call wc_vrule
.r:
    mov ax, [wc_rx1]
    or ax, ax
    jz .t
    call wc_vrule
.t:
    mov ax, [wc_mx1]                    ; under the title, across the center
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
    jmp wc_hruler

; --- wc_vrule - a rule down column AX, between the header and the bar -------
wc_vrule:
    add ax, [wc_ox]
    mov bx, WC_HDRH + 1
    add bx, [wc_oy]
    mov dx, [wc_sy]
    dec dx
    add dx, [wc_oy]
    call OSAPI_GFX_VLINE
    ret

; the slots are FAR: a tail `jmp` to one would return through a near frame,
; so this is the call-and-ret the rules share
wc_hruler:
    call OSAPI_GFX_HLINE
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
    pop ax
.1:
    test ax, WCD_LEFT
    jz .2
    push ax
    call wc_pleft
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

; --- wc_buttons - draw every button that is in the layout --------------------
wc_buttons:
    call wc_bflags_set
    mov al, 1
.b:
    cmp al, WB_FRIEND + 1
    jne .draw
    cmp word [wc_lx2], 0                ; no left pane: no Add friend
    je .next
.draw:
    call wc_btn
.next:
    inc ax
    cmp al, WB_N + 1
    jb .b
    ret

; --- wc_bflags_set - the flag words, from the ONE predicate (SPEC.md 47) ---
wc_bflags_set:
    push ax
    push bx
    xor ax, ax
    mov bx, wc_bflags
.f:
    mov dx, OS88UI_FILL
    cmp byte [wc_col], 0                ; the colour face paints its own
    je .face                            ; (SPEC.md 13.8.10)
    or dx, OS88UI_OWN
.face:
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
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_bok - would this button do anything? (SPEC.md 47 rule 2)
; in:  AL = the button. out: CF = 0 live, 1 refused; every register preserved
;
; Send wants something to send and a mode that sends; Add friend wants room
; on the list. The rest are always live: Search with nothing to search finds
; nothing and says so, which is an answer.
; -----------------------------------------------------------------------------
wc_bok:
    cmp al, WB_SEND
    je .send
    cmp al, WB_FRIEND
    je .friend
    clc
    ret
.send:
    cmp word [wc_ln+LN_LEN], 0
    je .no
    clc
    ret
.friend:
    cmp byte [wc_fn], WC_NFR
    jae .no
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; THE REGIONS
; =============================================================================

; -----------------------------------------------------------------------------
; wc_phdr - the header: who we are, and how we are (SPEC.md 113.5)
; -----------------------------------------------------------------------------
wc_phdr:
    call wc_sbclr
    call wc_rclr
    mov byte [wc_rinv], WS_STRIP
    mov al, ' '
    call wc_sbch
    call wc_sbch
    call wc_sbch
    mov si, wc_nick
    mov [wc_rbold], si                  ; bold, when there is a nick
    mov byte [wc_rbcol], 3
    cmp byte [si], 0
    jne .n
    mov si, wc_s_nonick
.n:
    call wc_sbstr
    mov cx, 16
    call wc_sbto
    mov al, ' '
    call wc_sbch
    call wc_sbch
    mov al, [wc_st]                     ; the status's glyph and colour
    cmp byte [wc_link], LK_LIVE
    je .g
    mov al, ST_GONE
.g:
    push ax
    call wc_stglyph
    mov [wc_rgl2], bl
    mov byte [wc_rgcol2], 16
    mov bl, al                          ; the word in the glyph's own ink
    xor bh, bh
    shl bx, 1
    mov al, [wc_glink+bx+2]
    mov ah, 0xFF
    mov cl, 18
    call wc_rspan
    pop ax
    call wc_stword                      ; SI = 'Online', 'Away', ...
    call wc_sbstr
    mov cx, [wc_brects + 8 * WB_STATUS] ; to the button, on the byte grid
    sub cx, [wc_ox]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    dec cx
    call wc_sbto
    mov byte [wc_rgl], GL_PERSON
    mov byte [wc_rgcol], 1
    xor cx, cx
    mov dx, 4
    mov ax, WCSL_HDR
    jmp wc_rowout

; --- wc_stword - SI = the word for our state: the status, or why we have none
wc_stword:
    mov si, wc_s_offline
    cmp byte [wc_link], LK_LIVE
    jne .out
    mov bl, [wc_st]
    xor bh, bh
    shl bx, 1
    mov si, [wc_stwords+bx]
.out:
    ret

; -----------------------------------------------------------------------------
; wc_lband - what band N of the left pane holds (SPEC.md 113.5)
; in:  CX = the band, from 0. out: AL = LB_*, AH = the item's index;
;      clobbers BX, DX
;
; THE PAINTER AND THE HIT TEST BOTH ASK THIS, so a click lands on what was
; drawn there. The order: ROOMS, its four rooms, a gap; MESSAGES, its
; conversations, a gap; FRIENDS ONLINE, the friends with a live session.
; -----------------------------------------------------------------------------
LB_NONE     equ 0
LB_TROOMS   equ 1
LB_ROOM     equ 2
LB_TMSGS    equ 3
LB_DM       equ 4
LB_TFRND    equ 5
LB_FRIEND   equ 6

wc_lband:
    mov ax, LB_TROOMS
    jcxz .out
    dec cx
    cmp cx, WC_NROOM
    jb .room
    sub cx, WC_NROOM
    jcxz .none                          ; the gap
    dec cx
    mov al, LB_TMSGS
    jcxz .out
    dec cx
    xor dx, dx
    mov dl, [wc_dn]
    cmp cx, dx
    jb .dm
    sub cx, dx
    jcxz .none
    dec cx
    mov al, LB_TFRND
    jcxz .out
    dec cx
    call wc_frnth                       ; CX = n -> AH = the friend, CF none
    jc .none
    mov al, LB_FRIEND
    ret
.room:
    mov ah, cl
    mov al, LB_ROOM
    ret
.dm:
    mov ah, cl
    mov al, LB_DM
    ret
.none:
    xor ax, ax
.out:
    ret

; --- wc_frnth - the CX'th friend who is online -> AH = its index, CF = 1 none
; clobbers BX, DX
wc_frnth:
    push cx
    push si
    xor dx, dx                          ; DL = the friend, DH = online so far
.f:
    cmp dl, [wc_fn]
    jae .no
    mov al, dl
    call wc_frrec                       ; SI = its nick
    call wc_rosfind                     ; CF = 0 online
    jc .n
    cmp dh, cl
    je .yes
    inc dh
.n:
    inc dl
    jmp short .f
.yes:
    mov ah, dl
    pop si
    pop cx
    clc
    ret
.no:
    pop si
    pop cx
    stc
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

; --- wc_onlinefr - how many friends are online -> AL. clobbers BX, CX, DX ---
wc_onlinefr:
    xor dx, dx
.f:
    cmp dl, [wc_fn]
    jae .out
    push si
    mov al, dl
    call wc_frrec
    call wc_rosfind
    pop si
    jc .n
    inc dh
.n:
    inc dl
    jmp short .f
.out:
    mov al, dh
    ret

; -----------------------------------------------------------------------------
; wc_pleft - the left pane, every band down to the Add friend button
; -----------------------------------------------------------------------------
wc_pleft:
    cmp word [wc_lx2], 0
    je .out
    xor cx, cx                          ; CX = the band
.band:
    mov dx, cx                          ; its top: WC_HDRH + 3 + band * 10
    mov ax, WC_LH
    push cx
    mul dx
    add ax, WC_HDRH + 3
    mov dx, ax
    add ax, WC_LH
    mov bx, [wc_brects + 8 * WB_FRIEND + 2]
    sub bx, [wc_oy]
    cmp word [wc_brects + 8 * WB_FRIEND + 0], 1
    jne .lim                            ; the button's top is the floor...
    mov bx, [wc_sy]                     ; ...or, with no button, the bar's
.lim:
    cmp ax, bx
    pop cx
    ja .out
    push cx
    push dx
    mov [wc_rband], cx
    call wc_lband                       ; AL = what, AH = which
    pop dx
    call wc_lrow
    pop cx
    inc cx
    jmp short .band
.out:
    ret

; --- wc_lrow - draw left band DX holding AL/AH (wc_lband's answer); the band's
; number in [wc_rband] names its slot
wc_lrow:
    push dx
    call wc_sbclr
    call wc_rclr
    mov byte [wc_rlead], 1              ; the colour face runs it to the rule
    cmp al, LB_TROOMS
    je .troom
    cmp al, LB_ROOM
    je .room
    cmp al, LB_TMSGS
    je .tmsg
    cmp al, LB_DM
    je .dm
    cmp al, LB_TFRND
    je .tfr
    cmp al, LB_FRIEND
    je .fr
    jmp .draw
.troom:
    mov si, wc_s_rooms
    jmp short .title
.tmsg:
    mov si, wc_s_msgs
.title:
    mov [wc_rbold], si
    mov byte [wc_rinv], WS_STRIP
    call wc_sbstr
    jmp .draw
.tfr:
    mov si, wc_s_frnds
    mov [wc_rbold], si
    mov byte [wc_rinv], WS_STRIP
    call wc_sbstr
    mov cx, WC_LCELLS - 2
    call wc_sbto
    call wc_onlinefr
    xor ah, ah
    call wc_sbnum
    jmp .draw
.room:
    mov al, ah                          ; '# name', the count right-aligned
    xor ah, ah
    push ax
    cmp al, [wc_view]
    jne .rnot
    mov byte [wc_rinv], WS_INV
.rnot:
    mov cl, 0                           ; the colour face's '#' is grey
    call wc_rdim
    mov si, wc_s_hash
    call wc_sbstr
    pop ax
    push ax
    call wc_roomname                    ; SI = its name
    call wc_sbstr
    mov cx, WC_LCELLS - 3
    call wc_sbto
    pop bx
    cmp byte [wc_ment+bx], 0            ; named there (SPEC.md 113.2.4): an @
    jne .rment                          ; ahead of the count
    shl bx, 1
    mov ax, [wc_cnt+bx]
    sub ax, [wc_seen+bx]
    jmp .count
.rment:
    shl bx, 1
    mov ax, [wc_cnt+bx]
    sub ax, [wc_seen+bx]
    cmp ax, 99
    jbe .rm1
    mov ax, 99
.rm1:
    dec di                              ; the badge takes the pad before it:
    mov byte [wc_rbadc], CMAGENTA       ; ' @3' or '@12'
    call wc_rbad0
    push ax
    cmp ax, 10
    jae .rm2
    mov al, ' '
    call wc_sbch
.rm2:
    mov al, '@'
    call wc_sbch
    pop ax
    or ax, ax
    jz .badge
    call wc_sbnum
    jmp short .badge
.dm:
    mov al, ah
    xor ah, ah
    mov cl, 4                           ; WC_DREC = 16
    shl ax, cl
    add ax, wc_dms
    mov si, ax
    push si
    call wc_isview                      ; ZF = 1: this is the view
    jne .dnot
    mov byte [wc_rinv], WS_INV
.dnot:
    call wc_pglyph                      ; the partner's glyph from the roster
    mov al, ' '                         ; (and a cell for it); then a space
    call wc_sbch
    call wc_sbstr
    mov cx, WC_LCELLS - 3
    call wc_sbto
    pop si
    mov al, [si+WC_DUNR]
    xor ah, ah
    jmp .count
.fr:
    mov al, ah
    call wc_frrec                       ; SI = the friend
    push si
    call wc_isview
    jne .fnot
    mov byte [wc_rinv], WS_INV
.fnot:
    call wc_pglyph
    mov al, ' '
    call wc_sbch
    pop si
    call wc_sbstr
    jmp short .draw
.count:
    or ax, ax                           ; 0 is shown as nothing
    jz .draw
    cmp ax, 99
    jbe .num
    mov ax, 99
.num:
    mov byte [wc_rbadc], CRED           ; unread: a red badge (SPEC.md 113.5.2)
    call wc_rbad0
    cmp ax, 10
    jae .two
    push ax
    mov al, ' '
    call wc_sbch
    pop ax
.two:
    call wc_sbnum
.badge:
    call wc_rbad1
.draw:
    mov cx, WC_LCELLS
    call wc_sbto
    pop dx
    mov ax, [wc_rband]
    add ax, WCSL_LEFT
    cmp word [wc_rband], WC_MAXB
    jb .slot
    mov ax, 0xFFFF
.slot:
    mov cx, 8
    jmp wc_rowout

; --- wc_rclr - a row with nothing to say about itself yet: the normal style,
; no glyph, no bold, no spans, at the left. Preserves all.
wc_rclr:
    mov byte [wc_rinv], WS_NORM
    mov byte [wc_rgl], 0xFF
    mov byte [wc_rgl2], 0xFF
    mov byte [wc_rgcol], 0
    mov word [wc_rbold], 0
    mov byte [wc_rbcol], 0
    mov byte [wc_rspn], 0
    mov byte [wc_rlead], 0
    ret

; --- wc_rspan - a span for the row: from cell CL, ink AL, ground AH (0xFF the
; style's). THE COLOUR FACE'S ONLY: the mono face has none, which is what
; keeps its pictures what they were. Preserves all.
wc_rspan:
    cmp byte [wc_col], 0
    je .out
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
.out:
    ret

; --- wc_rdim - cell CL grey (the colour face's '#' and '@'), the next one the
; style's again. Preserves all.
wc_rdim:
    push ax
    push cx
    mov al, CDGRAY
    cmp byte [wc_rinv], WS_INV          ; ...not on a selection's blue
    jne .g
    mov al, CLGRAY
.g:
    mov ah, 0xFF
    call wc_rspan
    inc cl
    mov ax, 0xFFFF
    call wc_rspan
    pop cx
    pop ax
    ret

; --- wc_rbad0 / wc_rbad1 - a badge from here (DI) / to here: white on
; [wc_rbadc]. Preserve all.
wc_rbad0:
    push ax
    push cx
    mov cx, di
    sub cx, wc_sbuf
    mov al, CWHITE
    mov ah, [wc_rbadc]
    call wc_rspan
    pop cx
    pop ax
    ret
wc_rbad1:
    push ax
    push cx
    mov cx, di
    sub cx, wc_sbuf
    mov ax, 0xFFFF
    call wc_rspan
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_rowout - draw the composed row at CX, DX through slot AX (0xFFFF none):
; the style [wc_rinv], glyph [wc_rgl] at cell [wc_rgcol] and [wc_rgl2] at
; [wc_rgcol2] (0xFF none), the bold [wc_rbold] (a string, 0 none) at cell
; [wc_rbcol], the spans wc_rspan collected - and on the colour face, a row
; with [wc_rlead] set moves a cell right and starts at x - 8, so its ground
; runs from the pane's edge to its rule
; -----------------------------------------------------------------------------
wc_rowout:
    push ax
    push cx
    mov al, [wc_rinv]
    call wc_mreset
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
    jz .lead
    call wc_slen
    mov ch, [wc_rbcol]
    call wc_mbold
.lead:
    pop cx
    cmp byte [wc_col], 0
    je .go
    cmp byte [wc_rlead], 0
    je .go
    call wc_lead
    sub cx, 8
.go:
    pop ax
    call wc_slot
    jmp wc_bandc

; --- wc_lead - the composition a cell right, a space in front, and every cell
; the meta names with it. Preserves all.
wc_lead:
    push ax
    push bx
    push si
    mov si, wc_sbuf + WC_SBUFN - 2
.m:
    mov al, [si-1]
    mov [si], al
    dec si
    cmp si, wc_sbuf
    ja .m
    mov byte [si], ' '
    mov byte [wc_sbuf+WC_SBUFN-1], 0
    mov bx, wc_meta + WCX_SP + 3        ; the spans after the first
    mov al, [wc_meta+WCX_NSP]
.s:
    dec al
    jz .g
    inc byte [bx]
    add bx, 3
    jmp short .s
.g:
    cmp byte [wc_meta+WCX_G0], 0xFF
    je .g1
    inc byte [wc_meta+WCX_G0]
.g1:
    cmp byte [wc_meta+WCX_G1], 0xFF
    je .b
    inc byte [wc_meta+WCX_G1]
.b:
    inc byte [wc_meta+WCX_BC]
    pop si
    pop bx
    pop ax
    ret

; --- wc_pglyph - SI = a nick: its status glyph into [wc_rgl], a space for it
; in the composition. Preserves SI.
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

; -----------------------------------------------------------------------------
; wc_ptitle - the center's title (and topic): '# room' or '@ nick'
; -----------------------------------------------------------------------------
wc_ptitle:
    call wc_sbclr
    call wc_rclr
    mov al, ' '
    call wc_sbch
    mov cl, 1                           ; the colour face's '#' or '@' is grey
    call wc_rdim
    mov si, wc_s_at
    cmp byte [wc_view], WV_DM
    je .t
    mov si, wc_s_hash
.t:
    call wc_sbstr
    mov ax, di
    sub ax, wc_sbuf                     ; the name's column, for the bold
    mov [wc_rbcol], al
    mov si, wc_vdm
    cmp byte [wc_view], WV_DM
    je .n
    mov al, [wc_view]
    xor ah, ah
    call wc_roomname
.n:
    mov [wc_rbold], si
    call wc_sbstr
    call wc_titlecells                  ; CX = cells up to the Search button
    call wc_sbto
    mov cx, [wc_tx]
    mov dx, [wc_ty]
    mov ax, WCSL_TITLE
    call wc_rowout
    cmp byte [wc_topic], 0
    je .out
    ; --- the topic ----------------------------------------------------------
    call wc_sbclr
    call wc_rclr
    mov byte [wc_rinv], WS_DIM
    mov al, ' '
    call wc_sbch
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
    mov cx, [wc_mx2]                    ; the whole center: the topic is below
    sub cx, [wc_tx]                     ; the Search button
    shr cx, 1
    shr cx, 1
    shr cx, 1
    call wc_sbto
    mov cx, [wc_tx]
    mov dx, [wc_ty]
    add dx, WC_LH + WC_TOPICDY
    mov ax, WCSL_TOPIC
    jmp wc_rowout
.out:
    ret

; --- wc_titlecells - CX = the cells from the text column to the Search button
wc_titlecells:
    mov cx, [wc_brects + 8 * WB_SEARCH]
    sub cx, [wc_ox]
    sub cx, [wc_tx]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    dec cx
    ret

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
; wc_pfield - the input row: the mode's prompt, then the field
; -----------------------------------------------------------------------------
wc_pfield:
    ; The row is cleared first: a prompt that came or went moved the field's
    ; left edge, and os88line_draw paints its frame and its text cells but
    ; not the strip between them - so the last prompt's first letter stayed
    ; inside the frame. This runs on a MODE change and a field set from
    ; behind; a keystroke is os88line_edit's, which needs none of it.
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wc_tx]
    add ax, [wc_ox]
    mov bx, [wc_ln+LN_Y1]
    mov cx, [wc_brects + 8 * WB_EMO + 0]
    dec cx
    mov dx, [wc_ln+LN_Y2]
    call OSAPI_GFX_FILL
    mov bl, [wc_mode]
    or bl, bl
    jz .f
    call wc_sbclr
    xor bh, bh
    shl bx, 1
    mov si, [wc_prompts+bx]
    call wc_sbstr
    mov cx, [wc_tx]
    mov dx, [wc_iy]
    add dx, 3
    mov al, WS_NOTE                     ; the prompt: blue on the colour face
    call wc_band
.f:
    mov si, wc_ln
    call os88line_draw
    call wc_bflags_set                  ; ...and Send, whose grey is the
    mov al, WB_SEND + 1                 ; field's emptiness (SPEC.md 47 rule
    jmp wc_btn                          ; 2): a field filled from behind - a
                                        ; refused message put back - is one

; -----------------------------------------------------------------------------
; wc_pright - MEMBERS: the room's sessions, or everyone's in a conversation
; -----------------------------------------------------------------------------
wc_pright:
    cmp word [wc_rx1], 0
    je .out
    call wc_members                     ; AL = how many
    call wc_sbclr
    push ax
    mov si, wc_s_members
    cmp byte [wc_view], WV_DM
    jne .t
    mov si, wc_s_online
.t:
    push si
    call wc_sbstr
    mov cx, WC_RCELLS - 3
    call wc_sbto
    pop si
    pop ax
    xor ah, ah
    push si
    call wc_sbnum
    mov cx, WC_RCELLS
    call wc_sbto
    pop si
    call wc_rclr
    mov [wc_rbold], si
    mov byte [wc_rinv], WS_STRIP
    mov cx, [wc_rx1]
    add cx, 8
    mov dx, WC_HDRH + 3
    mov ax, WCSL_RIGHT
    call wc_rowout
    ; --- the members, a band each, to the status bar ----------------------
    xor bx, bx                          ; BX = the roster record to look at
    mov word [wc_rowy], WC_HDRH + 3 + WC_LH
    mov word [wc_rband], 1
.m:
    mov ax, [wc_rowy]
    add ax, WC_LH
    cmp ax, [wc_sy]
    ja .out
    call wc_sbclr
    call wc_rclr
.seek:
    cmp bl, [wc_rn]
    jae .blank
    call wc_ismember                    ; CF = 0: roster record BL is one
    jnc .one
    inc bx
    jmp short .seek
.one:
    push bx
    mov ax, WC_RREC
    mul bx
    add ax, wc_ros
    mov si, ax
    mov al, [si+WC_RST]
    call wc_stglyph
    mov [wc_rgl], bl
    mov al, ' '                         ; the glyph's cell, then a space
    call wc_sbch
    call wc_sbch
    call wc_sbstr
    pop bx
    inc bx
.blank:
    mov cx, WC_RCELLS
    call wc_sbto
    push bx
    mov cx, [wc_rx1]
    add cx, 8
    mov dx, [wc_rowy]
    mov ax, [wc_rband]
    add ax, WCSL_RIGHT
    cmp word [wc_rband], WC_MAXB
    jb .slot
    mov ax, 0xFFFF
.slot:
    call wc_rowout
    pop bx
    add word [wc_rowy], WC_LH
    inc word [wc_rband]
    jmp short .m
.out:
    ret

; --- wc_ismember - CF = 0 when roster record BL belongs in MEMBERS ---------
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

; --- wc_members - AL = how many sessions MEMBERS lists. clobbers BX ---------
wc_members:
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
    ret

; -----------------------------------------------------------------------------
; wc_pstat - the status bar: the link, the room's head count, notifications
; -----------------------------------------------------------------------------
wc_pstat:
    call wc_sbclr
    call wc_rclr
    mov byte [wc_rinv], WS_STRIP
    mov al, ' '                         ; the glyph's cell and a space - and on
    call wc_sbch                        ; the colour face, two lamps' (SPEC.md
    call wc_sbch                        ; 113.5.2)
    call wc_sbch
    cmp byte [wc_col], 0
    je .word
    call wc_sbch
    call wc_sbch
.word:
    call wc_linkword                    ; SI = 'Connected', ...
    call wc_sbstr
    cmp byte [wc_link], LK_LIVE
    jne .right
    mov si, wc_s_bar
    call wc_sbstr
    call wc_members
    xor ah, ah
    call wc_sbnum
    mov si, wc_s_inroom
    cmp byte [wc_view], WV_DM
    jne .w
    mov si, wc_s_onl
.w:
    call wc_sbstr
.right:
    mov cx, [wc_cw]                     ; 'Notifications: On' at the right end,
    shr cx, 1                           ; clear of the grow box
    shr cx, 1
    shr cx, 1
    sub cx, WC_NOTIFYC
    call wc_sbto
    mov si, wc_s_notify
    call wc_sbstr
    mov cx, di                          ; On green, Off red
    sub cx, wc_sbuf
    mov ax, 0xFF00 | CGREEN
    mov si, wc_s_on
    cmp byte [wc_notify], 0
    jne .o
    mov al, CRED
    mov si, wc_s_off
.o:
    call wc_rspan
    call wc_sbstr
    mov cx, di
    sub cx, wc_sbuf
    mov ax, 0xFFFF
    call wc_rspan
    mov cx, [wc_cw]
    shr cx, 1
    shr cx, 1
    shr cx, 1
    sub cx, WC_GROWC
    call wc_sbto
    mov byte [wc_rgcol], 1
    cmp byte [wc_col], 0
    jne .lamps
    mov bl, GL_PLUG
    cmp byte [wc_link], LK_LIVE
    je .g
    mov bl, GL_CUT
.g:
    mov [wc_rgl], bl
    jmp short .out
.lamps:
    call wc_lamps                       ; BL = the link's, BH = activity's
    mov [wc_rgl], bl
    mov [wc_rgl2], bh
    mov byte [wc_rgcol2], 3
.out:
    xor cx, cx
    mov dx, [wc_sy]
    inc dx
    mov ax, WCSL_STAT
    jmp wc_rowout

; --- wc_lamps - the two lamps' glyphs: BL the link's, BH activity's ---------
; THE LINK: green with a session, amber while one is being made or the server
; asked us to rest, red with no card or after a failure, dark with no nick.
; ACTIVITY: amber while a request we MADE is on the wire (a join, a message,
; a flag - a held poll is waiting, not activity) and for a second after a
; reply that brought something (wc_ondone's [wc_act]); dark otherwise.
wc_lamps:
    mov al, [wc_link]
    mov bl, GL_LED + LED_GREEN
    cmp al, LK_LIVE
    je .act
    mov bl, GL_LED + LED_OFF
    cmp al, LK_NONICK
    je .act
    mov bl, GL_LED + LED_RED
    cmp al, LK_NODRV
    je .act
    cmp al, LK_OFF
    je .act
    mov bl, GL_LED + LED_AMBER
.act:
    mov bh, GL_LED + LED_AMBER
    cmp byte [wc_act], 0
    jne .out
    mov al, [wc_rq]
    cmp al, RQ_NONE
    je .dark
    cmp al, RQ_POLL
    jne .out
.dark:
    mov bh, GL_LED + LED_OFF
.out:
    ret

; --- wc_linkword - SI = the status bar's word for [wc_link] ------------------
wc_linkword:
    mov bl, [wc_link]
    xor bh, bh
    shl bx, 1
    mov si, [wc_linkwords+bx]
    cmp byte [wc_link], LK_OFF
    jne .out
    ; 'Offline - retrying in Ns'
    push di
    mov di, wc_rbuf
    push bx
    mov bx, wc_rbuf + 31
    call wc_put
    mov ax, [wc_wait]
    xor dx, dx
    call wc_putd32
    mov al, 's'
    call wc_putc
    mov byte [di], 0
    pop bx
    pop di
    mov si, wc_rbuf
.out:
    ret

; =============================================================================
; THE MESSAGE PANE
; =============================================================================
; A message is LINES: an optional divider, a header (time and nick) unless
; compact, then its text in SEGMENTS, word-wrapped. [WC_MLINES] caches the
; count for the current width; wc_relines makes every cache good and sums
; them into [wc_total].

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
    mov cx, [wc_cols]
    sub cx, WC_TIME
    jmp short .s
.done:
    mov ax, dx
    cmp byte [wc_compact], 0
    jne .c
    inc ax                              ; the header line
.c:
    cmp ax, 255
    jb .st
    mov al, 255
.st:
    mov [bx+WC_MLINES], al
.out:
    pop bx
    xor ah, ah
    ret

; --- wc_w0 - BX = a record: CX = its first segment's cells. clobbers AX ------
; Compact puts 'nick: ' in front of the first segment; the rest start at the
; text column either way.
wc_w0:
    mov cx, [wc_cols]
    sub cx, WC_TIME
    cmp byte [wc_compact], 0
    je .out
    push si
    lea si, [bx+WC_MNICK]
    push cx
    call wc_slen
    mov ax, cx
    pop cx
    pop si
    sub cx, ax
    sub cx, 2
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
; new-messages divider, its header and its text. Preserves BX; clobbers CX,
; DX, SI, DI
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

; --- wc_maxtop - AX = the largest top line that still fills the pane -------
wc_maxtop:
    mov ax, [wc_total]
    sub ax, [wc_rows]
    jge .out
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; wc_ppane - the whole pane, from [wc_top], and its scroll bar
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
    ; --- the lines on the glass, moved to where they now belong -------------
    ; [wc_dtop] is the top the glass shows. Fewer lines apart than the pane is
    ; tall, OSAPI_GFX_SCROLL moves them and the slots move with them, so the
    ; rows below draw only what the move uncovered (SPEC.md 113.5.1)
    mov ax, [wc_top]
    cmp word [wc_dtop], 0xFFFF
    je .rows
    cmp byte [wc_pop], PP_NONE          ; a popup over the pane would move with
    jne .rows                           ; it
    sub ax, [wc_dtop]
    jz .rows
    mov cx, ax
    jns .abs
    neg cx
.abs:
    cmp cx, [wc_rows]
    jae .rows
    push ax
    mov cx, WC_LH
    imul cx
    mov si, ax                          ; SI = rows, positive: up
    mov ax, [wc_tx]
    add ax, [wc_ox]
    mov cx, [wc_cols]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx                              ; x2 + 1 is a multiple of 8: tx is
    mov bx, [wc_py1]                    ; and so is a whole number of cells
    add bx, [wc_oy]
    push ax
    mov ax, [wc_rows]
    mov dx, WC_LH
    mul dx
    add ax, bx
    dec ax
    mov dx, ax
    pop ax
    call OSAPI_GFX_SCROLL
    pop ax
    jc .rows                            ; refused (a covered pane): the slots
    call wc_pshift                      ; stay, and the rows draw what differs
.rows:
    mov ax, [wc_top]
    mov [wc_dtop], ax
    xor cx, cx
.r:
    cmp cx, [wc_rows]
    jae .bar
    push cx
    call wc_prow
    pop cx
    inc cx
    jmp short .r
.bar:
    jmp wc_sbar

; -----------------------------------------------------------------------------
; wc_ptail - what was appended: at the bottom the pane follows the end, and
;            reading further up it stays where it is and only the bar says
;            there is more. wc_ppane moves what can be moved and draws only
;            what the slots say is new.
; -----------------------------------------------------------------------------
wc_ptail:
    call wc_maxtop                      ; the end, before the new lines
    cmp [wc_top], ax
    jb .read
    mov byte [wc_tobot], 1              ; at it: follow it
.read:
    jmp wc_ppane

; --- wc_sbar - the scroll bar from [wc_total], [wc_rows] and [wc_top]: the
; library's os88ui_sbupd (SPEC.md 13.10.5.6), so a move is the thumb's three
; calls or none, and only a total or a fit that moved draws the whole bar -
; which a W_PAINT owes ([wc_sbk], wcband.inc's wc_owe)
wc_sbar:
    mov ax, [wc_total]
    mov [wc_sb+8], ax
    mov ax, [wc_rows]
    mov [wc_sb+10], ax
    mov ax, [wc_top]
    mov [wc_sb+12], ax
    mov bx, wc_sb
    cmp byte [wc_sbk], 0
    je .whole
    jmp os88ui_sbupd
.whole:
    mov byte [wc_sbk], 1
    jmp os88ui_sbupdw

; -----------------------------------------------------------------------------
; wc_prow - draw pane row CX (0 = the top band), which shows line top + CX,
;           through its slot
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_prow:
    mov [wc_rband], cx
    mov ax, WC_LH
    mul cx
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
    push bx
    call wc_mrec                        ; BX = the record
    mov [wc_rrec], bx
    pop ax
    cmp al, [wc_sel]
    jne .ns
    mov byte [wc_rinv], WS_INV
    jmp short .nm
.ns:
    mov bx, [wc_rrec]                   ; a header that names us: yellow on
    test byte [bx+WC_MFLG], WCM_MENT    ; the colour face
    jz .nm
    mov byte [wc_rinv], WS_MENT
.nm:
    cmp byte [wc_compact], 0
    jne .compact
    jcxz .header
    dec cx                              ; CX = the segment
    mov al, ' '                         ; a text line: the time column blank
    push cx
    mov cx, WC_TIME
    call wc_sbto
    pop cx
    mov byte [wc_rinv], WS_NORM         ; only the header carries the selection
    jmp short .segment
.compact:
    jcxz .chead
    push cx
    mov cx, WC_TIME
    call wc_sbto
    pop cx
    mov byte [wc_rinv], WS_NORM
    jmp short .segment
.chead:
    call wc_phead                       ; 'hh:mm ' 'nick' then ': ' segment 0
    mov al, ':'
    call wc_sbch
    mov al, ' '
    call wc_sbch
    xor cx, cx
.segment:
    ; segment CX of the text, at the composition's end
    push di
    mov bx, [wc_rrec]
    lea si, [bx+WC_MTEXT]
    push cx
    call wc_w0
    pop dx                              ; DX = segments to skip
.skip:
    push dx
    call wc_seg
    pop dx
    or dx, dx
    jz .take
    dec dx
    mov si, di
    mov cx, [wc_cols]
    sub cx, WC_TIME
    jmp short .skip
.take:
    pop di
    jcxz .out
.copy:
    lodsb
    call wc_sbch
    loop .copy
    jmp short .out
.header:
    call wc_phead
    jmp short .out
.divider:
    mov byte [wc_rinv], WS_ALERT
    call wc_sbdivider
    jmp short .out
.dayline:
    mov byte [wc_rinv], WS_DIM
    call wc_sbdayline
.out:
    mov cx, [wc_cols]
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

; --- wc_phead - a header's 'hh:mm nick', the nick bold at the text column;
; on the colour face the time grey and the nick in its own colour, unless the
; header is the selection's blue
wc_phead:
    call wc_sbtime
    cmp byte [wc_rinv], WS_INV
    je .nick
    xor cl, cl
    mov ax, 0xFF00 | CDGRAY
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
    mov cx, di
    sub cx, wc_sbuf
    mov ax, 0xFFFF
    jmp wc_rspan

; --- wc_nickcol - AL = the colour face's ink for the nick at SI: one of six
; that read on white, by a sum of its letters - so a nick is always the same
; colour, on every machine (SPEC.md 113.5.2). Preserves the rest.
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
    mov al, bl
    xor ah, ah
    mov bl, 6
    div bl
    mov bl, ah
    xor bh, bh
    mov al, [wc_nickcols+bx]
    pop si
    pop bx
    ret

wc_nickcols:    db CBLUE, CGREEN, CRED, CMAGENTA, CBROWN, CCYAN

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
    push ax
    cmp ax, 10
    jae .m2
    mov al, '0'
    call wc_sbch
.m2:
    pop ax
    call wc_sbnum
    mov al, ' '
    jmp wc_sbch

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
;      its header unless compact, then its segments);
;      CF = 1 past the end. clobbers AX, DX, SI, DI
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
    ; above the :) button, its right edge on the button's
    mov ax, [wc_brects + 8 * WB_EMO + 4]
    sub ax, WC_POPW * 8 + 3
    mov bx, [wc_brects + 8 * WB_EMO + 2]
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
    ; below Set status..., its right edge on the button's
    mov ax, [wc_brects + 8 * WB_STATUS + 4]
    sub ax, WC_POPW * 8 + 3
    mov bx, [wc_brects + 8 * WB_STATUS + 6]
    inc bx
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
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wc_poprc+0]
    mov bx, [wc_poprc+2]
    mov cx, [wc_poprc+4]
    mov dx, [wc_poprc+6]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
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
    call wc_band
    pop cx
    pop bx
    inc bx
    jmp short .r
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
