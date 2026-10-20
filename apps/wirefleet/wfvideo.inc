; Opaque tile rows, native clipping, no idle repaint. SPEC.md 114.2.
wf_geometry:
    cmp byte [wf_infs], 0
    jne wf_fs_geometry
    mov bx, [wf_win]
    call OSAPI_WM_CONTENT
    mov [wf_ox], ax
    mov [wf_oy], dx
    call OSAPI_WM_GEOM
    mov [wf_w], cx
    mov [wf_h], dx
    mov word [wf_cellh], 24
    mov word [wf_gridy], 40
    mov word [wf_roststep], 44
    cmp dx, 350
    jae .display
    mov word [wf_cellh], 16
    mov word [wf_roststep], 30
    cmp dx, 200
    jae .display
    mov word [wf_cellh], 8
    mov word [wf_gridy], 25
    mov word [wf_roststep], 16
.display:
    call OSAPI_WM_DISPLAY
    mov byte [wf_monochrome], 0
    mov word [wf_rowstride], 120
    mov word [wf_tstride], 12
    cmp dh, 1
    ja .map
    mov byte [wf_monochrome], 1
    mov word [wf_rowstride], 30
    mov word [wf_tstride], 3
.map:
    mov ax, [wf_cellh]
    mov cx, 10
    mul cx
    mov [wf_gridh], ax
    add ax, [wf_gridy]
    add ax, 4
    mov [wf_controlsy], ax
    xor di, di
    mov cx, [wf_cellh]
.row:
    mov ax, di
    shr ax, 1
    mov bx, 24
    mul bx
    div word [wf_cellh]
    mul word [wf_tstride]
    mov [wf_rowmap+di], ax
    inc di
    inc di
    loop .row
    ; Rects are absolute and rebuilt after every window movement.
    mov ax, [wf_ox]
    add ax, 16
    mov bx, [wf_oy]
    add bx, [wf_controlsy]
    mov di, wf_btnrects
    mov cx, 5
.rect:
    mov [di], ax
    mov [di+2], bx
    mov dx, ax
    add dx, 107
    mov [di+4], dx
    mov dx, bx
    add dx, 14
    mov [di+6], dx
    add ax, 114
    add di, 8
    loop .rect
    call wf_chat_geometry
    ret

; Relative opaque white margins. Grid pixels are never erased underneath.
wf_ground:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [wf_w]
    dec cx
    mov dx, [wf_gridy]
    dec dx
    call wf_fill
    xor ax, ax
    mov bx, [wf_gridy]
    add bx, [wf_gridh]
    mov dx, [wf_h]
    dec dx
    call wf_fill
    xor ax, ax
    mov bx, [wf_gridy]
    mov cx, 23
    mov dx, bx
    add dx, [wf_gridh]
    dec dx
    call wf_fill
    mov ax, 264
    mov cx, 325
    call wf_fill
    mov ax, 566
    mov cx, [wf_w]
    dec cx
    call wf_fill
    cmp byte [wf_ready], 0
    jne .out
    mov ax, 326
    mov cx, 565
    call wf_fill
.out:
    ret
wf_fill:
    push ax
    push bx
    push cx
    push dx
    add ax, [wf_ox]
    add bx, [wf_oy]
    add cx, [wf_ox]
    add dx, [wf_oy]
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wf_boards:
    mov byte [wf_drawboard], 0
.board:
    mov byte [wf_drawrow], 0
    mov word [wf_destx], 24
    cmp byte [wf_infs], 0
    je .which
    mov word [wf_destx], 20
.which:
    cmp byte [wf_drawboard], 0
    je .row
    mov word [wf_destx], 326
    cmp byte [wf_infs], 0
    je .row
    mov word [wf_destx], 176
.row:
    xor ax, ax
    mov al, [wf_drawrow]
    mul word [wf_cellh]
    add ax, [wf_gridy]
    mov [wf_desty], ax
    mov byte [wf_rowlimit], 10
    call wf_buildrow
    call wf_blitrow
    inc byte [wf_drawrow]
    cmp byte [wf_drawrow], 10
    jb .row
    cmp byte [wf_drawboard], 0
    jne .out
    cmp byte [wf_ready], 0
    je .out
    inc byte [wf_drawboard]
    jmp .board
.out:
    ret

; AL = tile index, bit 7 = selection border. No private ship on enemy grid.
wf_tileid:
    xor bx, bx
    mov bl, [wf_drawrow]
    cmp byte [wf_drawboard], 2
    je .roster
    mov al, bl
    mov dl, 10
    mul dl
    add al, [wf_drawcol]
    xor ah, ah
    mov di, ax
    mov bx, ax
    cmp byte [wf_drawboard], 0
    jne .enemy
    mov al, [wf_board+bx]
    or al, al
    jz .select
    dec al
    xor ah, ah
    mov bx, ax
    mov dl, [wf_tilebases+bx]
    mov ax, di
    sub al, [wf_anchors+bx]
    cmp byte [wf_dirs+bx], 0
    je .segment
    push dx
    mov cl, 10
    div cl
    pop dx
    add dl, 17
.segment:
    add al, dl
    jmp .select
.enemy:
    mov al, [wf_outshots+bx]
.select:
    cmp byte [wf_drawboard], 0
    jne .cursor
    mov bx, di
    cmp byte [wf_inshots+bx], 1
    jne .hit
    mov al, 1
    jmp .cursor
.hit:
    cmp byte [wf_inshots+bx], 2
    jb .cursor
    add al, 34
.cursor:
    mov dl, [wf_drawboard]
    cmp dl, [wf_ready]
    jne .out
    mov dx, di
    cmp dl, [wf_target]
    jne .out
    or al, 0x80
.out:
    ret
.roster:
    mov al, [wf_tilebases+bx]
    add al, [wf_drawcol]
    ret

; Compose all cells, but draw only the changed contiguous range. A complete
; paint issues ten row blits per board rather than 100 cell blits.
wf_buildrow:
    push es
    push ds
    pop es
    mov byte [wf_first], 255
    mov byte [wf_last], 0
    mov byte [wf_drawcol], 0
.cell:
    call wf_tileid
    mov [wf_tile], al
    xor bx, bx
    mov bl, [wf_drawcol]
    mov [wf_rowtiles+bx], al
    xor bx, bx
    mov bl, [wf_drawrow]
    mov ax, bx
    mov dl, 10
    mul dl
    add al, [wf_drawcol]
    mov bx, ax
    cmp byte [wf_drawboard], 1
    jne .cache
    add bx, 100
.cache:
    mov al, [wf_tile]
    cmp byte [wf_drawboard], 2
    je .changed
    cmp byte [wf_fullpaint], 0
    jne .changed
    cmp al, [wf_cache+bx]
    je .copy
.changed:
    cmp byte [wf_drawboard], 2
    je .mark
    mov [wf_cache+bx], al
.mark:
    mov al, [wf_drawcol]
    cmp byte [wf_first], 255
    jne .last
    mov [wf_first], al
.last:
    mov [wf_last], al
.copy:
    inc byte [wf_drawcol]
    mov al, [wf_drawcol]
    cmp al, [wf_rowlimit]
    jb .cell
    mov al, [wf_first]
    cmp al, 255
    je .done
    mov [wf_drawcol], al
.compose:
    xor bx, bx
    mov bl, [wf_drawcol]
    mov al, [wf_rowtiles+bx]
    mov [wf_tile], al
    call wf_copytile
    inc byte [wf_drawcol]
    mov al, [wf_drawcol]
    cmp al, [wf_last]
    jbe .compose
.done:
    pop es
    ret

wf_copytile:
    xor ax, ax
    mov al, [wf_tile]
    and al, 0x7f
    mov bx, WF_TILE_COLOR
    mov si, wf_tiles_color
    cmp byte [wf_infs], 0
    je .native
    mov bx, WF_TILE_MONO
    mov si, wf_tiles_cga
    jmp .source
.native:
    cmp byte [wf_monochrome], 0
    je .source
    mov bx, WF_TILE_MONO
    mov si, wf_tiles_mono
.source:
    mul bx
    add ax, si
    mov [wf_source], ax
    xor ax, ax
    mov al, [wf_drawcol]
    mul word [wf_tstride]
    add ax, wf_rowbuf
    mov [wf_rowdest], ax
    xor dx, dx                  ; twice the destination scanline
.scan:
    mov si, wf_rowmap
    add si, dx
    mov si, [si]
    add si, [wf_source]
    mov di, [wf_rowdest]
    mov cx, [wf_tstride]
    rep movsb
    test byte [wf_tile], 0x80
    jz .next
    mov ax, dx
    shr ax, 1
    mov bx, [wf_rowdest]
    cmp ax, 1
    je .horizontal
    mov cx, [wf_cellh]
    sub cx, 2
    cmp ax, cx
    je .horizontal
    cmp ax, 1
    jb .next
    cmp ax, cx
    ja .next
    cmp byte [wf_infs], 0
    jne .cgasides
    cmp byte [wf_monochrome], 0
    jne .monosides
    and byte [bx], 0xf0
    or byte [bx], 0x0e
    and byte [bx+11], 0x0f
    or byte [bx+11], 0xe0
    jmp .next
.monosides:
    or byte [bx], 0x40
    or byte [bx+2], 2
    jmp .next
.cgasides:
    or byte [bx], 0x30
    or byte [bx+2], 0x0c
    jmp .next
.horizontal:
    cmp byte [wf_infs], 0
    jne .cgaline
    cmp byte [wf_monochrome], 0
    jne .monoline
    mov di, bx
    inc di
    mov al, 0xee
    mov cx, 10
    rep stosb
    jmp .next
.monoline:
    or byte [bx], 0x7f
    mov byte [bx+1], 255
    or byte [bx+2], 0xfe
    jmp .next
.cgaline:
    or byte [bx], 0x3f
    mov byte [bx+1], 255
    or byte [bx+2], 0xfc
.next:
    mov ax, [wf_rowstride]
    add [wf_rowdest], ax
    inc dx
    inc dx
    mov ax, [wf_cellh]
    shl ax, 1
    cmp dx, ax
    jb .scan
    ret

wf_blitrow:
    cmp byte [wf_infs], 0
    jne wf_fs_blitrow
    cmp byte [wf_first], 255
    je .out
    push es
    push ds
    pop es
    xor ax, ax
    mov al, [wf_first]
    mul word [wf_tstride]
    mov si, wf_rowbuf
    add si, ax
    mov al, [wf_first]
    xor ah, ah
    mov cx, 24
    mul cx
    add ax, [wf_destx]
    add ax, [wf_ox]
    mov bx, [wf_desty]
    add bx, [wf_oy]
    mov cl, [wf_last]
    sub cl, [wf_first]
    inc cl
    xor ch, ch
    push ax
    mov ax, 24
    mul cx
    mov cx, ax
    pop ax
    mov dx, [wf_cellh]
    mov bp, [wf_rowstride]
    cmp byte [wf_monochrome], 0
    jne .mono
    call OSAPI_GFX_BLIT4
    jmp .done
.mono:
    push ax
    mov ax, CWHITE              ; white ink, black paper
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    call OSAPI_GFX_BLIT1
.done:
    pop es
.out:
    ret

; Text helper: relative CX/DX, SI=NUL text, AX=ink/paper. Opaque.
wf_text:
    clc
    call OSAPI_GFX_PEN
    push cx
    push dx
    add cx, [wf_ox]
    add dx, [wf_oy]
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    ret

wf_header:
    push ds
    pop es
    mov di, wf_bannerbuf
    mov al, ' '
    mov cx, 75
    rep stosb
    mov byte [di], 0
    call wf_phase
    mov di, wf_bannerbuf+1
    mov cx, 35
.phase:
    lodsb
    or al, al
    jz .caption
    stosb
    loop .phase
.caption:
    mov si, wf_tagline
    cmp byte [wf_ready], 0
    je .righttext
    mov si, wf_matchcaption
.righttext:
    mov di, wf_bannerbuf+39
    mov cx, 35
.captioncopy:
    lodsb
    or al, al
    jz .turn
    stosb
    loop .captioncopy
.turn:
    cmp byte [wf_ready], 0
    je .cache
    mov ax, [wf_turn]
    inc ax
    xor dx, dx
    mov bx, 100
    div bx
    add al, '0'
    mov [wf_bannerbuf+71], al
    mov ax, dx
    xor dx, dx
    mov bx, 10
    div bx
    add al, '0'
    add dl, '0'
    mov [wf_bannerbuf+72], al
    mov [wf_bannerbuf+73], dl
.cache:
    cmp byte [wf_fullpaint], 0
    jne .store
    mov si, wf_bannerbuf
    mov di, wf_headershown
    mov cx, 75
    repe cmpsb
    je .axes
.store:
    mov si, wf_bannerbuf
    mov di, wf_headershown
    mov cx, 75
    rep movsb
.title:
    mov si, wf_bannerbuf
    xor cx, cx
    mov dx, 2
    mov ax, (1 << 8) | CWHITE
    cmp byte [wf_monochrome], 0
    je .banner
    mov ax, CWHITE
.banner:
    call wf_text
.axes:
    mov ax, (CWHITE << 8) | CBLACK
    cmp byte [wf_fullpaint], 0
    je .out
    mov cx, 24
    mov dx, [wf_gridy]
    sub dx, 20
    cmp word [wf_cellh], 8
    je .columns
.labels:
    mov si, wf_yourfleet
    call wf_text
    mov cx, 326
    mov si, wf_yourships
    cmp byte [wf_ready], 0
    je .right
    mov si, wf_enemywaters
.right:
    call wf_text
    ; Column labels are one glyph each, centered over the 24px cell.
.columns:
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
    add cx, 32
    mov dx, [wf_gridy]
    sub dx, 9
    mov si, wf_digit
    mov ax, (CWHITE << 8) | CBLACK
    call wf_text
    cmp byte [wf_ready], 0
    je .nextcol
    add cx, 302
    call wf_text
.nextcol:
    inc byte [wf_drawcol]
    cmp byte [wf_drawcol], 10
    jb .col
    mov byte [wf_drawrow], 0
.row:
    xor ax, ax
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
    add dx, [wf_gridy]
    mov ax, [wf_cellh]
    sub ax, 7
    shr ax, 1
    add dx, ax
    mov cx, 4
    mov si, wf_digit
    mov ax, (CWHITE << 8) | CBLACK
    call wf_text
    cmp byte [wf_ready], 0
    je .nextrow
    add cx, 302
    call wf_text
.nextrow:
    inc byte [wf_drawrow]
    cmp byte [wf_drawrow], 10
    jb .row
.out:
    ret

wf_roster:
    cmp byte [wf_ready], 0
    jne .out
    mov byte [wf_rosterindex], 0
.ship:
    xor bx, bx
    mov bl, [wf_rosterindex]
    xor ax, ax
    cmp byte [wf_anchors+bx], WF_ABSENT
    je .selection
    or al, 1
.selection:
    cmp bl, [wf_selected]
    jne .signature
    or al, 2
    cmp byte [wf_direction], 0
    je .signature
    or al, 4
.signature:
    cmp byte [wf_fullpaint], 0
    jne .show
    cmp al, [wf_rostershown+bx]
    je .next
.show:
    mov [wf_rostershown+bx], al
    cmp byte [wf_fullpaint], 0
    je .name
    mov al, [wf_lengths+bx]
    mov [wf_rowlimit], al
    mov [wf_drawrow], bl
    mov byte [wf_drawboard], 2
    mov word [wf_destx], 308
    mov ax, bx
    mul word [wf_roststep]
    add ax, [wf_gridy]
    mov [wf_desty], ax
    call wf_buildrow
    call wf_blitrow
.name:
    xor bx, bx
    mov bl, [wf_rosterindex]
    mov ax, bx
    shl bx, 1
    mov si, [wf_shipnames+bx]
    mov cx, 440
    mul word [wf_roststep]
    add ax, [wf_gridy]
    mov dx, ax
    mov ax, (CWHITE << 8) | CBLACK
    mov bl, [wf_rosterindex]
    cmp bl, [wf_selected]
    jne .nametext
    mov ax, CWHITE
.nametext:
    call wf_text
    mov si, wf_emptylabel
    xor bx, bx
    mov bl, [wf_rosterindex]
    cmp byte [wf_anchors+bx], WF_ABSENT
    je .selected
    mov si, wf_placedlabel
.selected:
    cmp bl, [wf_selected]
    jne .status
    mov si, wf_selectedh
    cmp byte [wf_direction], 0
    je .status
    mov si, wf_selectedv
.status:
    mov ax, (CWHITE << 8) | CBLACK
    cmp word [wf_cellh], 8
    je .next
    add dx, 12
    call wf_text
.next:
    inc byte [wf_rosterindex]
    cmp byte [wf_rosterindex], 5
    jb .ship
.out:
    ret

wf_controls:
    mov word [wf_btnlabels], wf_rotatelabel
    mov word [wf_btnlabels+2], wf_autolabel
    mov word [wf_btnlabels+4], wf_clearlabel
    mov word [wf_btnlabels+6], wf_readylabel
    mov word [wf_btnlabels+8], wf_soundlabel
    mov word [wf_btnflags+8], 0
    cmp byte [wf_muted], 0
    je .flags
    mov word [wf_btnlabels+8], wf_soundoff
.flags:
    mov di, wf_btnflags
    xor ax, ax
    cmp byte [wf_ready], 0
    je .editable
    mov ax, OS88UI_DIS
.editable:
    mov [di], ax
    mov [di+2], ax
    mov [di+4], ax
    cmp byte [wf_placed], 5
    je .ready
    mov ax, OS88UI_DIS
.ready:
    mov [di+6], ax
    cmp byte [wf_ready], 0
    je .draw
    mov word [wf_btnlabels], wf_firelabel
    mov word [wf_btnlabels+2], wf_chatlabel
    mov word [wf_btnlabels+4], wf_savelabel
    mov word [wf_btnlabels+6], wf_connectlabel
    mov word [wf_btnflags+2], 0
    mov word [wf_btnflags+4], 0
    mov word [wf_btnflags+6], 0
    call wf_canfire
    jc .draw
    mov word [wf_btnflags], 0
.draw:
    cmp byte [wf_chatmode], 0
    je .buttons
    mov word [wf_btnflags], OS88UI_DIS
    mov word [wf_btnflags+2], OS88UI_DIS
    mov word [wf_btnflags+4], OS88UI_DIS
    mov word [wf_btnflags+6], OS88UI_DIS
    mov word [wf_btnflags+8], OS88UI_DIS
    jmp .buttons
.buttons:
    mov bx, wf_buttons
    mov al, 1
.button:
    xor ah, ah
    mov di, ax
    dec di
    shl di, 1
    mov si, di
    shl si, 1
    mov dx, [wf_btnlabels+di]
    cmp byte [wf_fullpaint], 0
    jne .show
    cmp dx, [wf_buttonshown+si]
    jne .show
    mov dx, [wf_btnflags+di]
    cmp dx, [wf_buttonshown+si+2]
    je .next
.show:
    mov dx, [wf_btnlabels+di]
    mov [wf_buttonshown+si], dx
    mov dx, [wf_btnflags+di]
    mov [wf_buttonshown+si+2], dx
    call os88ui_btn
.next:
    inc al
    cmp al, 6
    jb .button
.out:
    ret

wf_footer:
    mov ax, [wf_message]
    cmp byte [wf_fullpaint], 0
    jne .draw
    cmp ax, [wf_footershown]
    jne .draw
    mov ax, [wf_chatcount]
    cmp ax, [wf_chatshown]
    jne .draw
    mov al, [wf_chatmode]
    cmp al, [wf_chatmodeshown]
    je .out
.draw:
    mov ax, [wf_message]
    mov [wf_footershown], ax
    mov ax, [wf_chatcount]
    mov [wf_chatshown], ax
    mov al, [wf_chatmode]
    mov [wf_chatmodeshown], al
    mov cx, 16
    mov dx, [wf_controlsy]
    add dx, 18
    mov ax, (CWHITE << 8) | CBLACK
    mov si, [wf_message]
    cmp word [wf_cellh], 8
    jne .status
    cmp byte [wf_chatmode], 0
    jne .chat
    cmp byte [wf_chatline], 0
    je .status
    cmp byte [wf_connected], 0
    je .status
    mov si, wf_chatline
.status:
    call wf_paddedline
.chat:
    jmp wf_chat_draw
.out:
    ret

    OS88UI_BTNREC wf_buttons,wf_btnrects,wf_btnlabels,wf_btnflags,5
