; WireFleet's colour face, SPEC.md 114.10. A 4bpp display with a content box
; of at least WF_CMINW x WF_CMINH gets it; everything else keeps 114.2's.
; Built from apps/os88flat.inc. Coordinates here are content-relative and
; wf_fill / wf_text add the origin; os88flat.inc wants them absolute.
WF_CMINW equ 560
WF_MGX1 equ 328                 ; 114.2's enemy board: ON the 8 grid (see wf_ground)
WF_CMINH equ 276
WF_CTOP equ 38                  ; the cards' top edge
WF_CGRIDY equ 63                ; the boards' first row
WF_CGX0 equ 40                  ; own board (and deployment's)
WF_CGX1 equ 336                 ; enemy board (on the 8 grid, for BLITP)
WF_CROWX equ 304                ; the "Your ships" rows, deployment
WF_CROWY equ 55

; Banked by wf_geometry at every full paint (and every update): the colour
; verdict and the layout it implies. Mono windows set the 114.2 constants.
wf_c_geometry:
    mov byte [wf_colour], 0
    mov word [wf_gx0], 24
    mov word [wf_gx1], WF_MGX1
    mov word [wf_rostx1], 310
    mov word [wf_rostx2], 580
    mov ax, [wf_gridy]
    mov [wf_rosty], ax
    cmp byte [wf_infs], 0
    jne .out
    cmp byte [wf_monochrome], 0
    jne .out
    cmp word [wf_w], WF_CMINW
    jb .out
    cmp word [wf_h], WF_CMINH
    jb .out
    mov byte [wf_colour], 1
    mov word [wf_gx0], WF_CGX0
    mov word [wf_gx1], WF_CGX1
    mov word [wf_gridy], WF_CGRIDY
    mov word [wf_rostx1], WF_CROWX
    mov ax, [wf_w]
    sub ax, 4
    mov [wf_rostx2], ax
    mov word [wf_rosty], WF_CROWY
    mov ax, [wf_cellh]
    add ax, 4
    mov [wf_roststep], ax
    mov ax, [wf_cellh]          ; the cards end three under the boards
    mov cx, 10
    mul cx
    mov [wf_gridh], ax
    add ax, WF_CGRIDY+3
    mov [wf_cardb], ax
    add ax, 19
    mov [wf_chatt], ax
    mov [wf_controlsy], ax      ; (114.2's name for "below the boards")
    mov ax, [wf_h]              ; lines of chat that fit above the entry
    sub ax, 31
    sub ax, [wf_chatt]
    sub ax, 3
    jns .lines
    xor ax, ax
.lines:
    mov cl, 10
    div cl
    cmp al, 3
    jbe .n
    mov al, 3
.n:
    mov [wf_chatn], al
.out:
    ret

; Button rectangles, absolute. Deployment: 1..3 Rotate/Auto/Clear in a row
; and 4 Ready as a bar on the ships card. Battle keeps only 1 Fire, in the
; legend row. 5 is Chat/Send beside the entry. Unused ones are parked.
wf_c_rects:
    push es
    push ds
    pop es
    mov di, wf_btnrects
    mov cx, 20
    mov ax, 0x7ff0              ; off the glass: never drawn, never hit
    rep stosw
    pop es
    mov di, wf_btnrects
    cmp byte [wf_ready], 0
    jne .battle
    mov ax, [wf_roststep]       ; under the ship rows and the placed count
    mov cx, 5
    mul cx
    add ax, WF_CROWY+14
    mov bx, ax
    mov dx, bx
    add dx, 17
    cmp word [wf_cellh], 16
    ja .row
    sub dx, 2
.row:
    mov ax, WF_CROWX+2
    mov si, 3
.three:
    mov [di], ax
    mov [di+2], bx
    add ax, 93
    mov [di+4], ax
    mov [di+6], dx
    add ax, 3
    add di, 8
    dec si
    jnz .three
    mov word [di], WF_CROWX+2   ; Ready: the width of the card
    mov ax, [wf_w]
    sub ax, 7
    mov [di+4], ax
    mov ax, dx
    add ax, 6
    mov [di+2], ax
    add ax, 25
    cmp word [wf_cellh], 16
    ja .ready
    sub ax, 4
.ready:
    mov [di+6], ax
    jmp short .send
.battle:
    mov ax, [wf_cardb]
    add ax, 2
    mov word [di], 182
    mov [di+2], ax
    mov word [di+4], 311
    add ax, 15
    mov [di+6], ax
.send:
    mov di, wf_btnrects+32
    mov ax, [wf_w]
    sub ax, 72
    mov [di], ax
    add ax, 64
    mov [di+4], ax
    mov ax, [wf_h]
    sub ax, 31
    mov [di+2], ax
    add ax, 14
    mov [di+6], ax
    mov word [wf_buttons+OS88UI_BT_RECTS], wf_btnrects
    mov di, wf_btnrects         ; relative -> absolute, but the parked
    mov cx, 5
.abs:
    cmp word [di], 0x7ff0
    je .next
    mov ax, [wf_ox]
    add [di], ax
    add [di+4], ax
    mov ax, [wf_oy]
    add [di+2], ax
    add [di+6], ax
.next:
    add di, 8
    loop .abs
    ret

; -----------------------------------------------------------------------------
; The full paint's ground and decoration. The boards are never under a fill:
; their cards are drawn round them (114.2: no erase before the board).
; -----------------------------------------------------------------------------
wf_c_ground:
    mov al, CBLUE               ; title bar
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [wf_w]
    dec cx
    mov dx, 21
    call wf_fill
    mov al, CLGRAY              ; the ground, in strips round everything
    call OSAPI_SET_COLOR
    mov bx, 22
    mov dx, 22
    call wf_fill
    mov bx, 36
    mov dx, WF_CTOP-1
    call wf_fill
    mov bx, [wf_cardb]
    inc bx
    mov dx, [wf_chatt]
    dec dx
    call wf_fill                ; the legend row
    mov bx, [wf_h]
    sub bx, 14
    mov dx, bx
    call wf_fill
    mov bx, 23                  ; side margins, top to the status bar
    mov dx, [wf_h]
    sub dx, 15
    mov cx, 1
    call wf_fill
    mov ax, [wf_w]
    sub ax, 2
    mov cx, [wf_w]
    dec cx
    call wf_fill
    mov ax, 298                 ; between the two cards
    mov cx, 301
    mov bx, WF_CTOP
    mov dx, [wf_cardb]
    call wf_fill
    mov al, CWHITE              ; notice strip and status bar grounds
    call OSAPI_SET_COLOR
    mov ax, 2
    mov bx, 23
    mov cx, [wf_w]
    sub cx, 3
    mov dx, 35
    call wf_fill
    xor ax, ax
    mov bx, [wf_h]
    sub bx, 12
    mov cx, [wf_w]
    dec cx
    mov dx, [wf_h]
    dec dx
    call wf_fill
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    mov bx, [wf_h]
    sub bx, 13
    mov dx, bx
    call wf_fill
    ; the own board's card, and the second card
    mov ax, 2
    mov cx, 297
    mov si, wf_yourwaters
    cmp byte [wf_ready], 0
    je .own
    mov si, wf_yourfleet
.own:
    mov dx, WF_CGX0
    call wf_c_boardcard
    cmp byte [wf_ready], 0
    je .ships
    mov ax, 302
    mov cx, [wf_w]
    sub cx, 3
    mov si, wf_enemywaters
    mov dx, WF_CGX1
    call wf_c_boardcard
    jmp short .chat
.ships:
    mov ax, 302
    mov bx, WF_CTOP
    mov cx, [wf_w]
    sub cx, 3
    mov dx, [wf_cardb]
    mov si, wf_yourships
    call wf_c_card
    call wf_c_pictures
.chat:
    mov ax, 2
    mov bx, [wf_chatt]
    mov cx, [wf_w]
    sub cx, 3
    mov dx, [wf_h]
    sub dx, 15
    xor si, si
    call wf_c_card
    call wf_c_legend
    ret

; AX..DX relative rect, SI title: flat_card at the window's origin.
wf_c_card:
    push ax
    push bx
    push cx
    push dx
    add ax, [wf_ox]
    add cx, [wf_ox]
    add bx, [wf_oy]
    add dx, [wf_oy]
    call flat_card
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A card round a board: AX/CX = card x1/x2, DX = the board's x, SI = title.
; Everything inside the rim but the board itself is white, then coordinates
; and the two closing grid lines the tiles do not carry.
wf_c_boardcard:
    mov [wf_cbx], dx
    mov [wf_cbx1], ax
    push ax
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    pop ax
    mov bx, WF_CTOP
    mov dx, [wf_cardb]
    push ax
    push cx
    add ax, [wf_ox]
    add cx, [wf_ox]
    add bx, [wf_oy]
    add dx, [wf_oy]
    call OSAPI_GFX_FRAME
    pop cx
    pop ax
    inc ax
    dec cx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    mov bx, WF_CTOP+1           ; above the board
    mov dx, WF_CGRIDY-1
    call wf_fill
    mov bx, [wf_cardb]          ; below it, past the closing line
    sub bx, 2
    mov dx, bx
    inc dx
    call wf_fill
    push cx                     ; left of it
    mov bx, WF_CGRIDY
    mov dx, [wf_cardb]
    sub dx, 2
    mov cx, [wf_cbx]
    dec cx
    call wf_fill
    pop cx
    mov ax, [wf_cbx]            ; right of it, past the closing line
    add ax, 241
    call wf_fill
    mov al, CCYAN               ; the right and bottom grid lines
    call OSAPI_SET_COLOR
    mov ax, [wf_cbx]
    add ax, 240
    mov cx, ax
    mov bx, WF_CGRIDY
    mov dx, [wf_cardb]
    sub dx, 3
    call wf_fill
    mov ax, [wf_cbx]
    mov bx, dx
    call wf_fill
    ; the title and the ruled line, as flat_card draws them
    mov cx, [wf_cbx1]
    add cx, 4
    mov dx, WF_CTOP+4
    mov ax, (CWHITE << 8) | CBLUE
    call wf_text
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [wf_cbx1]
    add ax, 4
    mov cx, ax
    add cx, 286
    mov bx, WF_CTOP+13
    mov dx, bx
    call wf_fill
    ; column letters and row numbers
    mov byte [wf_drawcol], 0
.col:
    xor bx, bx
    mov bl, [wf_drawcol]
    mov al, [wf_axes+bx]
    mov [wf_digit], al
    mov byte [wf_digit+1], 0
    mov ax, bx
    mov cx, 24
    mul cx
    mov cx, ax
    add cx, [wf_cbx]
    add cx, 8
    mov dx, WF_CGRIDY-9
    push si
    mov si, wf_digit
    mov ax, (CWHITE << 8) | CDGRAY
    call wf_text
    pop si
    inc byte [wf_drawcol]
    cmp byte [wf_drawcol], 10
    jb .col
    mov byte [wf_drawrow], 0
.row:
    mov al, [wf_drawrow]
    inc al
    mov byte [wf_digit], ' '
    add al, '0'
    mov [wf_digit+1], al
    cmp al, ':'
    jne .number
    mov word [wf_digit], '10'
.number:
    mov byte [wf_digit+2], 0
    mov al, [wf_drawrow]
    xor ah, ah
    mul word [wf_cellh]
    mov dx, ax
    add dx, WF_CGRIDY
    mov ax, [wf_cellh]
    sub ax, 7
    shr ax, 1
    add dx, ax
    mov cx, [wf_cbx]
    sub cx, 19
    push si
    mov si, wf_digit
    mov ax, (CWHITE << 8) | CDGRAY
    call wf_text
    pop si
    inc byte [wf_drawrow]
    cmp byte [wf_drawrow], 10
    jb .row
    ret

; Each ship's picture on the "Your ships" card: full paint only.
wf_c_pictures:
    mov ax, WF_CROWX+8
    mov bx, WF_CROWY+2
    mov cx, 120
    mov dx, [wf_roststep]
    shl dx, 1
    shl dx, 1
    add dx, [wf_roststep]
    call wf_planarprobe
    mov byte [wf_rosterindex], 0
.ship:
    xor bx, bx
    mov bl, [wf_rosterindex]
    mov al, [wf_lengths+bx]
    mov [wf_rowlimit], al
    mov [wf_drawrow], bl
    mov byte [wf_drawboard], 2
    mov word [wf_destx], WF_CROWX+8
    mov ax, bx
    mul word [wf_roststep]
    add ax, WF_CROWY+2
    mov [wf_desty], ax
    call wf_buildrow
    call wf_blitrow
    inc byte [wf_rosterindex]
    cmp byte [wf_rosterindex], 5
    jb .ship
    ret

wf_c_legend:
    mov cx, 6
    mov dx, [wf_cardb]
    add dx, 6
    cmp byte [wf_ready], 0
    je .hint
    mov si, wf_legend
.part:
    mov al, [si]                ; glyph, its ink, then NUL-ended words
    or al, al
    jz .out
    mov [wf_digit], al
    mov byte [wf_digit+1], 0
    mov al, [si+1]
    mov ah, CLGRAY
    add si, 2
    push si
    mov si, wf_digit
    call wf_text
    add cx, 8
    pop si
    push si
    mov ax, (CLGRAY << 8) | CBLACK
    call wf_text
    pop si
.skip:
    lodsb
    or al, al
    jz .part
    add cx, 8
    jmp .skip
.hint:
    mov si, wf_deployhint
    mov ax, (CLGRAY << 8) | CDGRAY
    call wf_text
.out:
    ret

; -----------------------------------------------------------------------------
; The incremental half: each field through its cache. W_PAINT forces all.
; -----------------------------------------------------------------------------
wf_c_title:
    call wf_c_caption           ; SI = caption, AL = its ink
    cmp byte [wf_fullpaint], 0
    jne .draw
    cmp si, [wf_capshown]
    jne .draw
    cmp al, [wf_capink]
    je .right
.draw:
    mov [wf_capshown], si
    mov [wf_capink], al
    mov cl, al
    mov ch, CBLUE
    mov ax, [wf_ox]
    add ax, 8
    mov bx, [wf_oy]
    add bx, 3
    mov di, wf_shipicon
    call flat_big
.right:
    ; "Turn 18" at the right, in battle: a field of ten cells.
    push ds
    pop es
    mov di, wf_turnbuf
    mov ax, '  '
    mov cx, 5
    rep stosw
    mov byte [di], 0
    cmp byte [wf_ready], 0
    je .turnshow
    mov di, wf_turnbuf
    mov si, wf_turnword
    mov cx, 5
    rep movsb
    mov ax, [wf_turn]
    inc ax
    call dec_u16                ; apps/os88dec.inc (20.5.2)
    mov byte [di], ' '          ; the field, not the string, ends it
.turnshow:
    cmp byte [wf_fullpaint], 0
    jne .turndraw
    mov si, wf_turnbuf
    mov di, wf_turnshown
    mov cx, 10
    repe cmpsb
    je .out
.turndraw:
    mov si, wf_turnbuf
    mov di, wf_turnshown
    mov cx, 10
    rep movsb
    mov si, wf_turnbuf
    mov cx, [wf_w]
    sub cx, 88
    mov dx, 7
    mov ax, (CBLUE << 8) | CWHITE
    call wf_text
.out:
    ret

; SI = the title bar's caption for the moment, AL = its ink.
wf_c_caption:
    mov si, wf_deploytitle
    mov al, CWHITE
    cmp byte [wf_ready], 0
    je .out
    mov si, wf_capvictory
    mov al, CYELLOW
    cmp byte [wf_finished], 1
    je .out
    mov si, wf_capdefeat
    mov al, CLRED
    cmp byte [wf_finished], 2
    je .out
    mov si, wf_cappaused
    cmp byte [wf_conflict], 0
    jne .out
    mov al, CLCYAN
    mov si, wf_caplooking
    cmp byte [wf_connected], 0
    je .out
    mov si, wf_capwaiting
    cmp byte [wf_peerready], 0
    je .out
    mov si, wf_capfiring
    cmp byte [wf_pending], 0
    jne .out
    mov ax, [wf_turn]
    and al, 1
    cmp al, [wf_side]
    mov si, wf_captheirs
    mov al, CLCYAN
    jne .out
    mov si, wf_capyours
    mov al, CYELLOW
.out:
    ret

; The notice strip: a lamp for the link and the newest line of news.
wf_c_notice:
    call wf_c_lampcolour
    cmp byte [wf_fullpaint], 0
    jne .lamp
    cmp al, [wf_lampshown]
    je .text
.lamp:
    mov [wf_lampshown], al
    mov cl, al
    mov ax, [wf_ox]
    add ax, 6
    mov bx, [wf_oy]
    add bx, 25
    call flat_lamp
.text:
    ; News is whichever changed last: a new message, or (with no room for
    ; a chat log) a new chat line.
    mov si, [wf_message]
    cmp si, [wf_msgseen]
    je .which
    mov [wf_msgseen], si
    mov ax, [wf_chatcount]
    mov [wf_chatnews], ax
.which:
    cmp byte [wf_chatn], 0
    jne .msg
    mov ax, [wf_chatcount]
    cmp ax, [wf_chatnews]
    je .msg
    mov si, wf_chatline
.msg:
    cmp byte [wf_fullpaint], 0
    jne .draw
    cmp si, [wf_footershown]
    jne .draw
    cmp si, wf_chatline
    jne .out
    mov ax, [wf_chatcount]
    cmp ax, [wf_chatshown]
    je .out
.draw:
    mov [wf_footershown], si
    mov ax, [wf_chatcount]
    mov [wf_chatshown], ax
    push ds
    pop es
    mov di, wf_linebuf          ; 71 cells: the strip less the lamp
    mov ax, '  '
    mov cx, 36
    rep stosw
    mov byte [di-1], 0
    mov di, wf_linebuf
    mov cx, 71
.copy:
    lodsb
    or al, al
    jz .show
    stosb
    loop .copy
.show:
    mov si, wf_linebuf
    mov cx, 20
    mov dx, 26
    mov ax, (CWHITE << 8) | CBLACK
    call wf_text
.out:
    ret

; AL = the lamp's colour: green linked, yellow looking, red stopped.
wf_c_lampcolour:
    mov al, CLRED
    cmp byte [wf_conflict], 0
    jne .out
    mov al, CLGREEN
    cmp byte [wf_connected], 0
    jne .out
    mov al, CYELLOW
    cmp byte [wf_linkopen], 0
    jne .out
    cmp byte [wf_linkmode], 1
    je .out
    mov al, CLRED
.out:
    ret

; The ship rows: name chip and check box, through each row's signature.
wf_c_roster:
    cmp byte [wf_ready], 0
    jne .placed
    mov byte [wf_rosterindex], 0
.ship:
    xor bx, bx
    mov bl, [wf_rosterindex]
    xor ax, ax
    cmp byte [wf_anchors+bx], WF_ABSENT
    je .sel
    or al, 1
.sel:
    cmp bl, [wf_selected]
    jne .sig
    or al, 2
.sig:
    cmp byte [wf_fullpaint], 0
    jne .show
    cmp al, [wf_rostershown+bx]
    je .next
.show:
    mov [wf_rostershown+bx], al
    mov [wf_rowsig], al
    mov ax, bx
    mul word [wf_roststep]
    add ax, WF_CROWY
    mov [wf_rowy], ax
    ; the row's frame: blue round the selected ship, white otherwise
    mov al, CWHITE
    test byte [wf_rowsig], 2
    jz .frame
    mov al, CBLUE
.frame:
    call OSAPI_SET_COLOR
    mov ax, WF_CROWX
    add ax, [wf_ox]
    mov bx, [wf_rowy]
    add bx, [wf_oy]
    mov cx, [wf_w]
    sub cx, 6
    add cx, [wf_ox]
    mov dx, bx
    add dx, [wf_roststep]
    sub dx, 2
    call OSAPI_GFX_FRAME
    ; the name, in a fixed field, highlighted when selected
    xor bx, bx
    mov bl, [wf_rosterindex]
    shl bx, 1
    mov si, [wf_shipnames+bx]
    push ds
    pop es
    mov di, wf_linebuf
    mov ax, '  '
    mov cx, 8
    rep stosw
    mov byte [di], 0
    mov di, wf_linebuf
    test byte [wf_rowsig], 2
    jz .unmarked
    mov byte [di], '>'
.unmarked:
    inc di
    inc di
.name:
    lodsb
    or al, al
    jz .named
    stosb
    jmp .name
.named:
    mov si, wf_linebuf
    mov cx, WF_CROWX+136
    mov dx, [wf_rowy]
    mov ax, [wf_cellh]
    sub ax, 4
    shr ax, 1
    add dx, ax
    mov ax, (CWHITE << 8) | CBLACK
    test byte [wf_rowsig], 2
    jz .ink
    mov ax, (CWHITE << 8) | CBLUE
.ink:
    call wf_text
    ; the check box, on the 8-pixel grid
    mov ax, [wf_w]
    sub ax, 24
    and ax, 0xfff8
    add ax, [wf_ox]
    mov bx, [wf_rowy]
    add bx, [wf_oy]
    mov cx, [wf_cellh]
    sub cx, 7
    shr cx, 1
    add bx, cx
    mov cl, [wf_rowsig]
    and cl, 1
    call flat_check
.next:
    inc byte [wf_rosterindex]
    cmp byte [wf_rosterindex], 5
    jb .ship
.placed:
    cmp byte [wf_ready], 0
    jne .out
    mov al, [wf_placed]         ; "n / 5 placed"
    cmp byte [wf_fullpaint], 0
    jne .count
    cmp al, [wf_placedshown]
    je .out
.count:
    mov [wf_placedshown], al
    add al, '0'
    mov [wf_placedtext], al
    mov si, wf_placedtext
    mov cx, WF_CROWX+100
    mov ax, [wf_roststep]
    mov dx, 5
    mul dx
    add ax, WF_CROWY+2
    mov dx, ax
    mov ax, (CWHITE << 8) | CDGRAY
    call wf_text
.out:
    ret

; The buttons: captions and styles for this phase, drawn through caches.
wf_c_controls:
    push es
    push ds
    pop es
    mov di, wf_ccap
    xor ax, ax
    mov cx, 5
    rep stosw
    mov di, wf_btnlabels        ; every label is the painter (13.8.10)
    mov ax, flat_btn
    mov cx, 5
    rep stosw
    mov si, wf_cstydeploy
    cmp byte [wf_ready], 0
    je .sty
    mov si, wf_cstybattle
.sty:
    mov di, wf_cstyle
    mov cx, 5
    rep movsb
    pop es
    mov word [wf_ccap+8], wf_chatlabel
    cmp byte [wf_chatmode], 0
    je .phase
    mov word [wf_ccap+8], wf_sendlabel
.phase:
    mov di, wf_btnflags
    cmp byte [wf_ready], 0
    jne .battle
    mov word [wf_ccap], wf_horizlabel
    cmp byte [wf_direction], 0
    je .rot
    mov word [wf_ccap], wf_vertlabel
.rot:
    mov word [wf_ccap+2], wf_autoplacelabel
    mov word [wf_ccap+4], wf_cclearlabel
    mov word [wf_ccap+6], wf_creadylabel
    mov ax, OS88UI_OWN
    mov [di], ax
    mov [di+2], ax
    mov [di+4], ax
    cmp byte [wf_placed], 5
    je .ready
    or ax, OS88UI_DIS
.ready:
    mov [di+6], ax
    jmp short .chat
.battle:
    call wf_c_firecap
    mov word [wf_ccap], wf_firecap
    mov ax, OS88UI_OWN
    mov [di+2], ax
    mov [di+4], ax
    mov [di+6], ax
    call wf_canfire             ; (it spends AX)
    mov ax, OS88UI_OWN
    jnc .fire
    or ax, OS88UI_DIS
.fire:
    mov di, wf_btnflags
    mov [di], ax
.chat:
    mov ax, OS88UI_OWN
    cmp byte [wf_connected], 0
    jne .live
    cmp byte [wf_chatmode], 0
    jne .live
    or ax, OS88UI_DIS
.live:
    mov [di+8], ax
    cmp byte [wf_chatmode], 0
    je .buttons
    mov word [di], OS88UI_OWN | OS88UI_DIS
    mov word [di+2], OS88UI_OWN | OS88UI_DIS
    mov word [di+4], OS88UI_OWN | OS88UI_DIS
    mov word [di+6], OS88UI_OWN | OS88UI_DIS
    mov word [di+8], OS88UI_OWN
.buttons:
    cmp byte [wf_abon], 0       ; the colour face keeps all five live; the
    jne .count                  ; About card withdraws them (wf_about)
    mov word [wf_buttons+OS88UI_BT_N], 5
.count:
    mov bx, wf_buttons
    mov al, 1
.button:
    xor ah, ah
    mov di, ax
    dec di
    shl di, 1
    mov si, di
    shl si, 1
    mov dx, [wf_ccap+di]
    mov cl, [wf_target]
    cmp byte [wf_fullpaint], 0
    jne .show
    cmp dx, [wf_buttonshown+si]
    jne .show
    mov dx, [wf_btnflags+di]
    cmp dx, [wf_buttonshown+si+2]
    jne .show
    cmp di, 0                   ; Fire's caption names the target
    jne .next
    cmp byte [wf_ready], 0
    je .next
    cmp cl, [wf_fireshown]
    je .next
.show:
    mov dx, [wf_ccap+di]
    mov [wf_buttonshown+si], dx
    mov dx, [wf_btnflags+di]
    mov [wf_buttonshown+si+2], dx
    or di, di
    jnz .draw
    mov [wf_fireshown], cl
.draw:
    call os88ui_btn
.next:
    inc al
    cmp al, 6
    jb .button
    ret

; "Fire at J10" from the target cell.
wf_c_firecap:
    push ds
    pop es
    mov di, wf_firecap
    mov si, wf_firelead
    mov cx, 8
    rep movsb
    xor ax, ax
    mov al, [wf_target]
    mov bl, 10
    div bl
    xchg al, ah                 ; AL = column, AH = row
    add al, 'A'
    stosb
    mov al, ah
    inc al
    cmp al, 10
    jb .one
    mov al, '1'
    stosb
    mov al, 0
.one:
    add al, '0'
    stosb
    xor al, al
    stosb
    ret

; Two lines of what just happened, beside Fire (battle only).
wf_c_events:
    cmp byte [wf_ready], 0
    je .out
    push ds
    pop es
    mov di, wf_evbuf
    mov ax, '  '
    mov cx, 35
    rep stosw
    mov di, wf_evbuf
    cmp byte [wf_cachevalid], 0
    je .mine
    mov si, wf_evtheir
    call wf_c_copy
    mov al, [wf_cachecell]
    call wf_c_cell
    mov bl, [wf_cachevalue]
    mov si, wf_evmiss
    cmp bl, 1
    je .theirs
    mov si, wf_evhit
    cmp bl, 2
    je .theirs
    mov si, wf_evsank
.theirs:
    call wf_c_copy
.mine:
    mov di, wf_evbuf+35
    mov al, [wf_lastout]
    cmp al, 100
    jae .cache
    mov si, wf_evmine
    call wf_c_copy
    mov al, [wf_lastout]
    call wf_c_cell
    xor bx, bx
    mov bl, [wf_lastout]
    mov bl, [wf_outshots+bx]
    mov si, wf_evmiss
    cmp bl, 1
    je .result
    mov si, wf_evhit
    cmp bl, 2
    je .result
    mov si, wf_evsunk
.result:
    call wf_c_copy
.cache:
    mov byte [wf_evbuf+34], 0
    mov byte [wf_evbuf+69], 0
    cmp byte [wf_fullpaint], 0
    jne .draw
    mov si, wf_evbuf
    mov di, wf_evshown
    mov cx, 70
    repe cmpsb
    je .out
.draw:
    mov si, wf_evbuf
    mov di, wf_evshown
    mov cx, 70
    rep movsb
    mov cx, 320
    mov dx, [wf_cardb]
    add dx, 2
    mov si, wf_evbuf
    mov ax, (CLGRAY << 8) | CBLACK
    call wf_text
    add dx, 9
    mov si, wf_evbuf+35
    call wf_text
.out:
    ret
; SI = NUL text -> [di], DI past it (no NUL).
wf_c_copy:
    lodsb
    or al, al
    jz .r
    stosb
    jmp wf_c_copy
.r:
    ret
; AL = cell 0..99 -> "J10" at [di].
wf_c_cell:
    xor ah, ah
    mov bl, 10
    div bl
    xchg al, ah
    add al, 'A'
    stosb
    mov al, ah
    inc al
    cmp al, 10
    jb .one
    mov al, '1'
    stosb
    xor al, al
.one:
    add al, '0'
    stosb
    ret

; The status bar: the port on the left, the opponent on the right.
wf_c_status:
    push ds
    pop es
    mov di, wf_statbuf
    mov ax, '  '
    mov cx, 37
    rep stosw
    mov byte [di], 0
    mov di, wf_statbuf
    mov si, wf_stnolink
    cmp byte [wf_linkopen], 0
    je .left
    mov si, wf_stcom
    call wf_c_copy
    mov al, [wf_curport]
    add al, '0'
    stosb
    mov si, wf_stbaud
.left:
    call wf_c_copy
    mov si, wf_stlooking
    cmp byte [wf_linkopen], 0
    jne .who
    mov si, wf_stoff
    jmp short .right
.who:
    cmp byte [wf_connected], 0
    je .right
    mov si, wf_stplacing
    cmp byte [wf_peerready], 0
    je .right
    mov si, wf_stready
.right:
    push si                     ; right-aligned: measure, then place
    xor cx, cx
.len:
    lodsb
    or al, al
    jz .measured
    inc cx
    jmp .len
.measured:
    pop si
    mov di, wf_statbuf+73
    sub di, cx
    call wf_c_copy
    cmp byte [wf_fullpaint], 0
    jne .draw
    mov si, wf_statbuf
    mov di, wf_statshown
    mov cx, 74
    repe cmpsb
    je .out
.draw:
    mov si, wf_statbuf
    mov di, wf_statshown
    mov cx, 74
    rep movsb
    mov si, wf_statbuf
    mov cx, 4
    mov dx, [wf_h]
    sub dx, 10
    mov ax, (CWHITE << 8) | CBLACK
    call wf_text
.out:
    ret

; The chat card's lines, newest last, and the entry.
wf_c_chat:
    mov al, [wf_chatn]
    or al, al
    jz .entry
    mov ax, [wf_chatcount]
    cmp byte [wf_fullpaint], 0
    jne .lines
    cmp ax, [wf_chatlogshown]
    je .entry
.lines:
    mov [wf_chatlogshown], ax
    mov al, 3
    sub al, [wf_chatn]
    mov ah, 40
    mul ah
    add ax, wf_chatlog
    mov si, ax
    xor bx, bx
    mov bl, [wf_chatn]
    mov dx, [wf_chatt]
    add dx, 3
.line:
    push si
    push bx
    mov cx, 8
    call wf_paddedline
    pop bx
    pop si
    add si, 40
    add dx, 10
    dec bx
    jnz .line
.entry:
    cmp byte [wf_fullpaint], 0
    je .out
    mov si, wf_line
    call os88line_draw
.out:
    ret

; The whole colour face, through the caches; wf_fullpaint forces it all.
wf_c_draw:
    call wf_c_rects
    cmp byte [wf_fullpaint], 0
    je .fields
    call wf_c_ground
.fields:
    call wf_boards
    call wf_c_title
    call wf_c_notice
    call wf_c_roster
    call wf_c_controls
    call wf_c_events
    call wf_c_status
    call wf_c_chat
    ret

wf_yourwaters: db 'YOUR WATERS',0
wf_cstydeploy: db CWHITE,CWHITE,CWHITE,CGREEN,CBLUE
wf_cstybattle: db CRED,CWHITE,CWHITE,CWHITE,CBLUE
wf_capvictory: db 'VICTORY!',0
wf_capdefeat: db 'DEFEAT',0
wf_cappaused: db 'MATCH PAUSED',0
wf_caplooking: db 'FINDING OPPONENT',0
wf_capwaiting: db 'OPPONENT PLACING',0
wf_capfiring: db 'FIRING...',0
wf_captheirs: db "OPPONENT'S TURN",0
wf_capyours: db 'YOUR TURN',0
wf_turnword: db 'Turn '
wf_horizlabel: db 'Horiz. [R]',0
wf_vertlabel: db 'Vert. [R]',0
wf_autoplacelabel: db 'Auto [A]',0
wf_cclearlabel: db 'Clear [C]',0
wf_creadylabel: db 'READY [Y]',0
wf_sendlabel: db 'Send',0
wf_firelead: db 'Fire at '
wf_placedtext: db '0 / 5 placed',0
wf_deployhint: db 'Click a square to place.  R rotates.  Right-click removes.  1-5 picks.',0
; legend: glyph, its ink, then its words
wf_legend: db 'X',CRED,' Hit  ',0, 'o',CBLUE,' Miss  ',0, '#',CBLACK,' Sunk',0, 0
wf_evtheir: db 'They fired ',0
wf_evmine: db 'You fired ',0
wf_evmiss: db ': miss.',0
wf_evhit: db ': HIT!',0
wf_evsank: db ': sunk yours!',0
wf_evsunk: db ': SUNK!',0
wf_stnolink: db 'No serial port open',0
wf_stcom: db 'COM',0
wf_stbaud: db '  2400 baud 8N1',0
wf_stlooking: db 'Looking for opponent',0
wf_stoff: db 'Link off - Link menu',0
wf_stplacing: db 'Opponent placing ships',0
wf_stready: db 'Opponent ready',0
; The title bar's ship: the package icon's own outline, 16 rows.
wf_shipicon:
    db 16
    dw 0,0,0x0180,0x03c0,0x03c0,0x07e0,0x1ff8,0x7ffe
    dw 0xffff,0xffff,0x7ffe,0x3ffc,0x7ffe,0xffff,0x6666,0
