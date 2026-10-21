; Native fullscreen on VGA/Hercules, CGA four-color §53 bracket. SPEC 114.8.
wf_fullscreen:
    mov bx, [wf_win]
    call OSAPI_WM_DISPLAY
    cmp dh, 1
    ja .native
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_CGA320
    jz .native
    call wf_fontcopy
    mov ax, wf_fs_main
    mov bx, [wf_win]
    xor cx, cx
    call OSAPI_FSX_RUN
    mov si, wf_line
    call os88line_resync
    cmp byte [wf_fsfile], 1
    je wf_save
    cmp byte [wf_fsfile], 2
    je wf_load
    ret
.native:
    mov bx, [wf_win]            ; OSAPI_WM_DISPLAY answered the HEIGHT in BX,
    mov al, [wf_nativefull]     ; and FULLSCREEN takes BX as the window: F hung
    xor al, 1                   ; a VGA or Hercules desktop on a divide fault
    call OSAPI_FULLSCREEN
    jc .out
    mov [wf_nativefull], al
.out:
    ret

wf_fontcopy:
    push es
    call OSAPI_FONT_GLYPHS
    mov [wf_fontfirst], al
    mov [wf_fontlast], ah
    mov [wf_fontbase], si
    mov es, dx
    mov bx, 32
    mov di, wf_font
.char:
    mov cx, 8
    cmp bl, [wf_fontfirst]
    jb .blank
    cmp bl, [wf_fontlast]
    ja .blank
    mov ax, bx
    xor dx, dx
    mov dl, [wf_fontfirst]
    sub ax, dx
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, [wf_fontbase]
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy
    jmp .next
.blank:
    mov byte [di], 0
    inc di
    loop .blank
.next:
    inc bx
    cmp bx, 128
    jb .char
    pop es
    ret

wf_fs_main:
    mov byte [wf_fsfile], 0
    mov byte [wf_fsquit], 0
    mov byte [wf_fsmouse], 0
    mov word [wf_fslastx], 0xffff
    mov word [wf_fslasty], 0xffff
    OS88_ALTENTER_SEED
    push ds
    pop es
    mov di, wf_fsi
    mov al, FSXM_CGA320
    call OSAPI_FSX_MODE
    jc .out
    cmp word [wf_fsi+FSI_W], 320
    jne .out
    cmp word [wf_fsi+FSI_H], 200
    jne .out
    cmp byte [wf_fsi+FSI_BPP], 2
    jne .out
    cmp byte [wf_fsi+FSI_BANKS], 2
    jne .out
    mov byte [wf_infs], 1
    mov dx, 0x3d9
    mov al, 0x30               ; palette 1, bright; desktop restore owns exit
    out dx, al
    call wf_fs_geometry
    mov byte [wf_fsforce], 1
    mov byte [wf_dirty], 1
.loop:
    call wf_poll
    call os88alt_edge
    jc .leave
    mov ah, 1
    int 0x16
    jz .mouse
    xor ah, ah
    int 0x16
    call wf_fs_key
.mouse:
    call wf_fs_mouse
    cmp byte [wf_fsquit], 0
    jne .leave
    cmp byte [wf_dirty], 0
    je .wait
    mov byte [wf_dirty], 0
    call wf_fs_draw
.wait:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    jmp .loop
.leave:
    call wf_soundquiet
    mov byte [wf_infs], 0
.out:
    ret

wf_fs_geometry:
    mov word [wf_ox], 0
    mov word [wf_oy], 0
    mov word [wf_w], 320
    mov word [wf_h], 200
    mov word [wf_cellh], 10
    mov word [wf_gridy], 40
    mov word [wf_roststep], 16
    mov byte [wf_monochrome], 0
    mov word [wf_rowstride], 30
    mov word [wf_tstride], 3
    jmp wf_geometry.map

; Dirty row's packed spans go directly to FSI's banked CGA framebuffer.
wf_fs_blitrow:
    cmp byte [wf_first], 255
    je .out
    push es
    mov ax, [wf_fsi+FSI_SEG]
    mov es, ax
    xor ax, ax
    mov al, [wf_first]
    mov bx, 3
    mul bx
    mov [wf_fsstart], ax
    mov bx, [wf_destx]
    shr bx, 1
    shr bx, 1
    add ax, bx
    mov [wf_fsxbyte], ax
    mov byte [wf_fsscan], 0
.scan:
    xor ax, ax
    mov al, [wf_fsscan]
    add ax, [wf_desty]
    mov di, 0
    test al, 1
    jz .bank
    mov di, [wf_fsi+FSI_BSTEP]
.bank:
    shr ax, 1
    mul word [wf_fsi+FSI_STRIDE]
    add di, ax
    add di, [wf_fsxbyte]
    xor ax, ax
    mov al, [wf_fsscan]
    mul word [wf_rowstride]
    mov si, wf_rowbuf
    add si, ax
    add si, [wf_fsstart]
    xor ax, ax
    mov al, [wf_last]
    sub al, [wf_first]
    inc al
    mov bx, 3
    mul bx
    mov cx, ax
    rep movsb
    inc byte [wf_fsscan]
    mov al, [wf_fsscan]
    xor ah, ah
    cmp ax, [wf_cellh]
    jb .scan
    pop es
.out:
    ret

; CX/DX pixel x/y; SI=NUL text. X is rounded to four pixels. White on black.
wf_fs_text:
    mov [wf_fstextx], cx
    mov [wf_fstexty], dx
    mov [wf_fstextptr], si
    push es
    mov ax, [wf_fsi+FSI_SEG]
    mov es, ax
.char:
    mov si, [wf_fstextptr]
    lodsb
    mov [wf_fstextptr], si
    or al, al
    jz .out
    cmp al, 32
    jb .space
    cmp al, 127
    jb .glyph
.space:
    mov al, 32
.glyph:
    sub al, 32
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, wf_font
    mov [wf_fsglyph], ax
    mov byte [wf_fstextscan], 0
.row:
    xor ax, ax
    mov al, [wf_fstextscan]
    add ax, [wf_fstexty]
    cmp ax, 200
    jae .out
    xor di, di
    test al, 1
    jz .bank
    mov di, [wf_fsi+FSI_BSTEP]
.bank:
    shr ax, 1
    mul word [wf_fsi+FSI_STRIDE]
    add di, ax
    mov ax, [wf_fstextx]
    cmp ax, 312
    ja .out
    shr ax, 1
    shr ax, 1
    add di, ax
    mov si, [wf_fsglyph]
    xor bx, bx
    mov bl, [wf_fstextscan]
    mov al, [si+bx]
    mov ah, al
    mov cl, 4
    shr al, cl
    mov bl, al
    mov al, [wf_bitpairs+bx]
    stosb
    mov al, ah
    and al, 15
    mov bl, al
    mov al, [wf_bitpairs+bx]
    stosb
    inc byte [wf_fstextscan]
    cmp byte [wf_fstextscan], 8
    jb .row
    add word [wf_fstextx], 8
    jmp .char
.out:
    pop es
    ret

wf_fs_line:
    push ds
    pop es
    mov di, wf_linebuf
    mov ax, '  '
    mov cx, 20
    rep stosw
    mov byte [di], 0
    mov di, wf_linebuf
    mov cx, 40
.copy:
    lodsb
    or al, al
    jz .draw
    stosb
    loop .copy
.draw:
    push dx
    mov bx, 0
.slot:
    cmp dx, [wf_fslines+bx]
    je .cached
    add bx, 2
    cmp bx, 12
    jb .slot
    jmp .render
.cached:
    mov ax, bx
    mov cx, 20
    mul cx
    mov di, wf_fscache
    add di, ax
    mov [wf_fscacheptr], di
    cmp byte [wf_fullpaint], 0
    jne .store
    mov si, wf_linebuf
    mov cx, 40
    repe cmpsb
    je .same
.store:
    mov di, [wf_fscacheptr]
    mov si, wf_linebuf
    mov cx, 40
    rep movsb
.render:
    pop dx
    xor cx, cx
    mov si, wf_linebuf
    jmp wf_fs_text
.same:
    pop dx
    ret

wf_fs_draw:
    mov al, FSXW_VSYNC
    call OSAPI_FSX_WAIT
    mov al, [wf_fsforce]
    mov [wf_fullpaint], al
    mov byte [wf_fsforce], 0
    call wf_boards
    cmp byte [wf_ready], 0
    jne .heads
    ; Deployment also shows empty enemy waters; the roster lives in the
    ; status line in this compact mode. No private peer data is drawn.
    mov byte [wf_drawboard], 1
    mov byte [wf_drawrow], 0
    mov word [wf_destx], 176
.sea:
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
    jb .sea
.heads:
    call wf_phase
    mov dx, 1
    call wf_fs_line
    mov si, wf_fslinkoff
    cmp byte [wf_conflict], 0
    je .connection
    mov si, wf_historyerror
    jmp .link
.connection:
    cmp byte [wf_connected], 0
    je .link
    mov si, wf_fslinkon
.link:
    mov dx, 11
    call wf_fs_line
    cmp byte [wf_fullpaint], 0
    je .controltext
    mov si, wf_yourfleet
    mov cx, 20
    mov dx, 22
    call wf_fs_text
    mov si, wf_enemywaters
    mov cx, 176
    mov dx, 22
    call wf_fs_text
    mov byte [wf_drawcol], 0
.columns:
    xor bx, bx
    mov bl, [wf_drawcol]
    mov al, [wf_axes+bx]
    mov [wf_digit], al
    mov byte [wf_digit+1], 0
    mov ax, bx
    mov cx, 12
    mul cx
    add ax, 24
    mov cx, ax
    mov dx, 31
    mov si, wf_digit
    call wf_fs_text
    xor ax, ax
    mov al, [wf_drawcol]
    mov cx, 12
    mul cx
    add ax, 180
    mov cx, ax
    mov dx, 31
    mov si, wf_digit
    call wf_fs_text
    inc byte [wf_drawcol]
    cmp byte [wf_drawcol], 10
    jb .columns
    mov byte [wf_drawrow], 0
.numbers:
    xor ax, ax
    mov al, [wf_drawrow]
    add al, '1'
    mov byte [wf_digit], ' '
    mov [wf_digit+1], al
    cmp al, ':'
    jne .number
    mov word [wf_digit], '10'
.number:
    mov byte [wf_digit+2], 0
    xor ax, ax
    mov al, [wf_drawrow]
    mul word [wf_cellh]
    add ax, [wf_gridy]
    inc ax
    mov dx, ax
    xor cx, cx
    mov si, wf_digit
    call wf_fs_text
    xor ax, ax
    mov al, [wf_drawrow]
    mul word [wf_cellh]
    add ax, [wf_gridy]
    inc ax
    mov dx, ax
    mov cx, 156
    mov si, wf_digit
    call wf_fs_text
    inc byte [wf_drawrow]
    cmp byte [wf_drawrow], 10
    jb .numbers
.controltext:
    mov si, wf_fsdeploy
    cmp byte [wf_ready], 0
    je .controls
    mov si, wf_fsbattle
.controls:
    mov dx, 146
    call wf_fs_line
    mov si, wf_chatline
    mov dx, 164
    call wf_fs_line
    mov si, wf_fsfooter
    cmp byte [wf_muted], 0
    je .footer
    mov si, wf_fsmuted
.footer:
    mov dx, 190
    call wf_fs_line
    cmp byte [wf_chatmode], 0
    je .prompt
    mov si, wf_input
    mov dx, 178
    jmp wf_fs_line
.prompt:
    mov si, wf_fslegend
    mov dx, 178
    jmp wf_fs_line

; SI is a short status also used by the native banner.
wf_phase:
    mov si, wf_deployhorizontal
    cmp byte [wf_direction], 0
    je .deployment
    mov si, wf_deployvertical
.deployment:
    cmp byte [wf_ready], 0
    je .out
    mov si, wf_readytitle
    cmp byte [wf_finished], 1
    je .won
    cmp byte [wf_finished], 2
    je .lost
    mov si, wf_caplooking       ; the phase, not the link line (which
    cmp byte [wf_connected], 0  ; fullscreen prints under it, and a window
    je .out                     ; header cut to "F: deskto")
    mov si, wf_capwaiting
    cmp byte [wf_peerready], 0
    je .out
    mov si, wf_phasepending
    cmp byte [wf_pending], 0
    jne .out
    mov ax, [wf_turn]
    and al, 1
    cmp al, [wf_side]
    mov si, wf_phaseenemy
    jne .out
    mov si, wf_phaseyours
.out:
    ret
.won:
    mov si, wf_phasewon
    ret
.lost:
    mov si, wf_phaselost
    ret

wf_fs_key:
    cmp byte [wf_chatmode], 0
    jne .chat
    cmp al, 27
    je .leave
    cmp al, 0
    jne .ascii
    mov dl, [wf_target]
    cmp ah, 0x4b
    je .left
    cmp ah, 0x4d
    je .right
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    jne .out
    cmp dl, 90
    jae .out
    add dl, 10
    jmp .target
.left:
    or dl, dl
    jz .out
    dec dl
    jmp .target
.right:
    cmp dl, 99
    jae .out
    inc dl
    jmp .target
.up:
    cmp dl, 10
    jb .out
    sub dl, 10
.target:
    mov [wf_target], dl
    jmp .dirty
.ascii:
    cmp al, 13
    je .enter
    cmp al, '1'
    jb .letter
    cmp al, '5'
    ja .letter
    cmp byte [wf_ready], 0
    jne .out
    sub al, '1'
    mov [wf_selected], al
    jmp .dirty
.letter:
    or al, 0x20
    cmp al, 'f'
    je .leave
    cmp al, 's'
    je .save
    cmp al, 'o'
    je .load
    cmp al, 'm'
    je .mute
    cmp al, 't'
    je .beginchat
    cmp al, 'n'
    je .new
    cmp al, 'r'
    je .rotate
    cmp al, 'a'
    je .auto
    cmp al, 'c'
    je .clear
    cmp al, 'y'
    je .ready
.out:
    ret
.save:
    mov byte [wf_fsfile], 1
    jmp .leave
.load:
    mov byte [wf_fsfile], 2
.leave:
    mov byte [wf_fsquit], 1
    ret
.mute:
    xor byte [wf_muted], 1
    cmp byte [wf_muted], 0
    je .dirty
    call wf_soundquiet
    jmp .dirty
.beginchat:
    mov byte [wf_chatmode], 1
    jmp .dirty
.new:
    call wf_new
    mov byte [wf_fsforce], 1
    jmp .dirty
.rotate:
    cmp byte [wf_ready], 0
    jne .out
    xor byte [wf_direction], 1
    jmp .dirty
.auto:
    cmp byte [wf_ready], 0
    jne .out
    call wf_auto
    mov al, 1
    call wf_sfx
    jmp .dirty
.clear:
    cmp byte [wf_ready], 0
    jne .out
    call wf_clearfleet
    jmp .dirty
.ready:
    cmp byte [wf_placed], 5
    jne .out
    cmp byte [wf_ready], 0
    jne .out
    mov byte [wf_ready], 1
    mov byte [wf_urgent], 1
    mov byte [wf_fsforce], 1
    mov al, 2
    call wf_sfx
    jmp .dirty
.enter:
    cmp byte [wf_ready], 0
    jne .fire
    call wf_place
    jc .out
    call wf_nextship
    mov al, 1
    call wf_sfx
    jmp .dirty
.fire:
    call wf_queue_shot
    jmp .dirty
.chat:
    cmp al, 27
    je .endchat
    cmp al, 13
    je .send
    mov bx, [wf_line+LN_LEN]
    cmp al, 8
    je .backspace
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out
    cmp bx, 32
    jae .out
    mov [wf_input+bx], al
    inc bx
    jmp .edited
.backspace:
    or bx, bx
    jz .out
    dec bx
.edited:
    mov byte [wf_input+bx], 0
    mov [wf_line+LN_LEN], bx
    jmp .dirty
.send:
    call wf_chat_submit
    jc .out
.endchat:
    mov byte [wf_chatmode], 0
.dirty:
    mov byte [wf_dirty], 1
    ret

wf_fs_mouse:
    cmp byte [wf_chatmode], 0
    jne .out
    call OSAPI_MOUSE
    mov ah, [wf_fsmouse]
    mov [wf_fsmouse], al
    and al, 1
    test ah, 1
    jnz .held
    mov [wf_fspress], al
    jmp .point
.held:
    mov byte [wf_fspress], 0
.point:
    cmp cx, [wf_fslastx]
    jne .moved
    cmp dx, [wf_fslasty]
    jne .moved
    cmp byte [wf_fspress], 0
    je .out
.moved:
    mov [wf_fslastx], cx
    mov [wf_fslasty], dx
    shr cx, 1                 ; desktop CGA mouse X is in 640-pixel units
    cmp byte [wf_ready], 0
    je .own
    cmp dx, 146
    jb .enemy
    cmp dx, 155
    ja .out
    cmp cx, 176
    jb .out
    cmp byte [wf_fspress], 0
    je .out
    call wf_queue_shot
    ret
.enemy:
    sub cx, 176
    jmp .grid
.own:
    sub cx, 20
.grid:
    jc .out
    cmp cx, 120
    jae .out
    sub dx, 40
    jc .out
    cmp dx, 100
    jae .out
    mov ax, dx
    mov dl, 10
    div dl
    mov dl, 10
    mul dl
    mov bx, ax
    mov ax, cx
    mov dl, 12
    div dl
    xor ah, ah
    add ax, bx
    cmp al, [wf_target]
    je .press
    mov [wf_target], al
    mov byte [wf_dirty], 1
.press:
    cmp byte [wf_fspress], 0
    je .out
    cmp byte [wf_ready], 0
    jne .out
    call wf_place
    jc .out
    call wf_nextship
    mov al, 1
    call wf_sfx
    mov byte [wf_dirty], 1
.out:
    ret

wf_bitpairs: db 0x00,0x03,0x0c,0x0f,0x30,0x33,0x3c,0x3f,0xc0,0xc3,0xcc,0xcf,0xf0,0xf3,0xfc,0xff
wf_fsdeploy: db 'R rotate A auto Y ready T chat',0
wf_fsbattle: db 'Enter fire T chat   [ Fire: Enter ]',0
wf_fsfooter: db 'F:desktop M:sound S:save O:load',0
wf_fsmuted: db 'F:desktop M:muted S:save O:load',0
wf_fslegend: db '. miss  X hit  # sunk   Arrows:target',0
wf_fslinkoff: db 'WireFleet | Link paused - F: desktop',0
wf_fslinkon: db 'WireFleet | 2400 baud 8N1 | linked',0
wf_phaseyours: db 'YOUR TURN',0
wf_phaseenemy: db 'ENEMY TURN',0
wf_phasepending: db 'SHOT SENT - WAITING FOR RESULT',0
wf_phasewon: db 'VICTORY - ENEMY FLEET SUNK',0
wf_phaselost: db 'DEFEAT - YOUR FLEET SUNK',0
wf_deployvertical: db 'DEPLOY YOUR FLEET - VERTICAL',0
wf_deployhorizontal: db 'DEPLOY YOUR FLEET - HORIZONTAL',0
wf_fslines: dw 1,11,146,164,178,190
