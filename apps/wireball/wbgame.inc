; Local simulation and the small-kernel text court. Pixel drawing is in wbvideo.inc.
wb_newgame:
    push ax
    mov byte [wb_left], 32
    mov byte [wb_right], 32
    mov byte [wb_scorel], 0
    mov byte [wb_scorer], 0
    mov byte [wb_flags], 0
    mov byte [wb_vx], 8
    mov byte [wb_vy], 2
    mov byte [wb_lspin], 0
    mov byte [wb_rspin], 0
    mov byte [wb_spinage], 4
    mov word [wb_crossings], 0
    call wb_serve
    mov al, WB_FX_LINK
    call wb_sound_play
    pop ax
    ret
wb_serve:
    push ax
    mov ax, [wb_width]
    shr ax, 1
    mov [wb_ballx], ax
    mov word [wb_bally], 10*4
    mov byte [wb_count], 36
    mov byte [wb_returns], 0
    mov byte [wb_fracx], 0
    mov byte [wb_fracy], 0
    mov al, WB_SPEED_MIN
    cmp byte [wb_vx], 0
    jge .direction
    neg al
.direction:
    mov [wb_vx], al
    call OSAPI_RAND
    and ax, 3
    mov bx, ax
    mov al, [wb_serveangles+bx]
    mov [wb_vy], al
    pop ax
    ret

; Drain keyboard BIOS records so a key typed in FSX cannot launch another app
; on return. Movement and command edges are sampled from make/break state.
wb_keys:
    mov ah, 1
    int 0x16
    jz .out
    xor ah, ah
    int 0x16
    cmp al, 27
    jne .command
    mov byte [wb_quit], 1
    ret
.command:
    or al, 0x20
    cmp al, 'm'
    je .mute
    cmp al, 'p'
    je .pause
    cmp al, 'n'
    jne .out
    test byte [wb_latch], 2
    jnz .out
    or byte [wb_latch], 2
    mov al, 2
    call wb_command_request
    ret
.mute:
    test byte [wb_latch], 4
    jnz .out
    or byte [wb_latch], 4
    call wb_sound_toggle
    ret
.pause:
    test byte [wb_latch], 1
    jnz .out
    or byte [wb_latch], 1
    mov al, 1
    call wb_command_request
.out:
    ret

wb_tick:
    mov byte [wv_legacytick], 1
    call wb_sound_tick
    mov al, 0x32                 ; release re-arms the M toggle
    call OSAPI_KEY_DOWN
    jc .mutedown
    and byte [wb_latch], 0xfb
.mutedown:
    inc word [wb_frames]
    mov al, KSC_ESC
    call OSAPI_KEY_DOWN
    jnc .commands
    mov byte [wb_quit], 1
.commands:
    mov al, 0x19                 ; P: only the rising edge
    call OSAPI_KEY_DOWN
    jc .pause
    and byte [wb_latch], 0xfe
    jmp .newkey
.pause:
    test byte [wb_latch], 1
    jnz .newkey
    or byte [wb_latch], 1
    mov al, 1
    call wb_command_request
.newkey:
    mov al, 0x31                 ; N
    call OSAPI_KEY_DOWN
    jc .new
    and byte [wb_latch], 0xfd
    jmp .timeout
.new:
    test byte [wb_latch], 2
    jnz .timeout
    or byte [wb_latch], 2
    mov al, 2
    call wb_command_request
.timeout:
    cmp byte [wb_practice], 0
    jne .active
    cmp byte [wb_bound], 0
    je .out
    mov ax, [wb_last]
    sub ax, [wb_seen]
    cmp ax, WB_TIMEOUT
    jb .active
    call wb_lost
    ret
.active:
    cmp byte [wb_phase], 2
    jne .out
    cmp byte [wb_spinage], 4
    jae .stalespin
    inc byte [wb_spinage]
    jmp .timer
.stalespin:
    mov byte [wb_rspin], 0
.timer:
    cmp byte [wb_count], 0
    je .out
    test byte [wb_flags], 3
    jnz .out
    mov al, [wb_owner]
    cmp al, [wb_side]
    jne .out
    dec byte [wb_count]
    jnz .out
    mov al, WB_FX_SERVE
    call wb_sound_play
.out:
    ret

wb_controls:
    mov bx, wb_left
    cmp byte [wb_side], 0
    je .paddle
    mov bx, wb_right
.paddle:
    mov al, [bx]
    mov [wb_padold], al
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jc .up
    mov al, 0x11                 ; W
    call OSAPI_KEY_DOWN
    jnc .downkey
.up:
    cmp byte [bx], 0
    je .downkey
    dec byte [bx]
.downkey:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jc .down
    mov al, 0x1f                 ; S
    call OSAPI_KEY_DOWN
    jnc .ball
.down:
    cmp byte [bx], WB_PMAX
    jae .ball
    inc byte [bx]
.ball:
    mov al, [bx]
    sub al, [wb_padold]
    cmp byte [wb_side], 0
    jne .rightspin
    mov [wb_lspin], al
    ret
.rightspin:
    mov [wb_rspin], al
    ret
wb_frame:
    inc word [wb_subtick]
    cmp byte [wb_phase], 2
    jne .out
    call wb_controls
    test byte [wb_flags], 3
    jnz .out
    cmp byte [wb_practice], 0
    je .owner
    mov al, [wb_right]
    mov [wb_padold], al
    call wb_ai
    mov al, [wb_right]
    sub al, [wb_padold]
    mov [wb_rspin], al
.owner:
    cmp byte [wb_count], 0
    jne .out
    mov al, [wb_owner]
    cmp al, [wb_side]
    jne .out
    call wb_motion
    call wb_pass
    mov al, [wb_owner]
    cmp al, [wb_side]
    jne .out
    call wb_collide
.out:
    ret

wb_command_request:
    cmp byte [wb_phase], 2
    jne .out
    mov ah, [wb_side]
    cmp ah, [wb_owner]
    je wb_applycommand
    mov ah, [wb_command]
    cmp ah, [wb_ack]
    jne .out                    ; one outstanding request, repeated until ACK
    inc byte [wb_command]
    mov [wb_cmdtype], al
.out:
    ret
wb_applycommand:
    cmp al, 1
    jne .new
    xor byte [wb_flags], 1
    test byte [wb_flags], 1
    jnz .quiet
    mov al, WB_FX_SERVE
    call wb_sound_play
    ret
.quiet:
    call wb_sound_quiet
    ret
.new:
    cmp al, 2
    jne .out
    call wb_newgame
.out:
    ret

wb_ai:
    test word [wb_subtick], 1
    jnz .out
    mov ax, [wb_width]
    shr ax, 1
    cmp [wb_ballx], ax
    jb .out
    mov ax, [wb_bally]
    sub ax, 8
    jns .positive
    xor ax, ax
.positive:
    cmp ax, WB_PMAX
    jbe .target
    mov ax, WB_PMAX
.target:
    cmp al, [wb_right]
    je .out
    jb .up
    inc byte [wb_right]
    ret
.up:
    dec byte [wb_right]
.out:
    ret

wb_motion:
    mov ax, [wb_ballx]
    shl ax, 1
    shl ax, 1
    xor bx, bx
    mov bl, [wb_fracx]
    add ax, bx
    mov [wb_prevx], ax
    mov ax, [wb_bally]
    shl ax, 1
    shl ax, 1
    mov bl, [wb_fracy]
    add ax, bx
    mov [wb_prevy], ax
    mov al, [wb_vx]
    cbw
    mov [wb_stepvx], ax
    mov al, [wb_vy]
    cbw
    mov [wb_stepvy], ax
    mov dx, [wb_ballx]
    mov al, [wb_vx]
    cbw
    xor bx, bx
    mov bl, [wb_fracx]
    add ax, bx
    mov bl, al
    and bl, 3
    mov [wb_fracx], bl
    sar ax, 1
    sar ax, 1
    add [wb_ballx], ax
    ; Count seam transitions on the authoritative machine for the cable gate.
    cmp byte [wb_practice], 0
    jne .vertical
    cmp byte [wb_side], 0
    jne .vertical
    mov ax, [wb_ballx]
    cmp dx, 78*4
    jae .goingleft
    cmp ax, 78*4
    jb .vertical
    inc word [wb_crossings]
    jmp .vertical
.goingleft:
    cmp ax, 78*4
    jae .vertical
    inc word [wb_crossings]
.vertical:
    mov al, [wb_vy]
    cbw
    xor bx, bx
    mov bl, [wb_fracy]
    add ax, bx
    mov bl, al
    and bl, 3
    mov [wb_fracy], bl
    sar ax, 1
    sar ax, 1
    add ax, [wb_bally]
    jns .bottom
    neg ax
    call wb_yfraction
    neg byte [wb_vy]
    push ax
    mov al, WB_FX_WALL
    call wb_sound_play
    pop ax
.bottom:
    cmp ax, WB_YMAX
    jl .store
    jg .reflect
    cmp byte [wb_fracy], 0
    je .store
.reflect:
    neg ax
    add ax, WB_YMAX*2
    call wb_yfraction
    neg byte [wb_vy]
    push ax
    mov al, WB_FX_WALL
    call wb_sound_play
    pop ax
.store:
    mov [wb_bally], ax
    ret

; Reflect the fractional coordinate too: -(integer + fraction/4).
wb_yfraction:
    cmp byte [wb_fracy], 0
    je .out
    dec ax
    neg byte [wb_fracy]
    and byte [wb_fracy], 3
.out:
    ret
wb_collide:
    cmp byte [wb_vx], 0
    jg .right
    cmp byte [wb_practice], 0
    jne .leftlocal
    cmp byte [wb_side], 0
    jne .out
.leftlocal:
    cmp word [wb_ballx], 8
    jg .out
    jl .leftcontact
    cmp byte [wb_fracx], 0
    jne .out
.leftcontact:
    mov bl, [wb_left]
    mov cl, [wb_lspin]
    mov dx, 8
    call wb_contact
    jc .missleft
    call wb_finishcontact
    mov al, WB_FX_PADDLE
    call wb_sound_play
    ret
.missleft:
    inc byte [wb_scorer]
    mov byte [wb_vx], -8
    jmp .point
.right:
    cmp byte [wb_practice], 0
    jne .rightlocal
    cmp byte [wb_side], 1
    jne .out
.rightlocal:
    mov dx, [wb_width]
    sub dx, 12
    cmp [wb_ballx], dx
    jl .out
    mov bl, [wb_right]
    mov cl, [wb_rspin]
    call wb_contact
    jc .missright
    neg byte [wb_vx]
    call wb_finishcontact
    mov al, WB_FX_PADDLE
    call wb_sound_play
    ret
.missright:
    inc byte [wb_scorel]
    mov byte [wb_vx], 8
.point:
    mov al, WB_FX_POINT
    call wb_sound_play
    call wb_serve
    cmp byte [wb_scorel], 11
    jae .over
    cmp byte [wb_scorer], 11
    jb .out
.over:
    or byte [wb_flags], 2
    mov al, WB_FX_WIN
    call wb_sound_play
.out:
    ret
wb_hit:
    ; Ball and paddle centers are quarter rows. Include the ball's extent so
    ; grazing an end is a real contact, rather than a whole-cell miss.
    xor bh, bh
    add bx, 7
    shl bx, 1
    shl bx, 1
    mov ax, [wb_bally]
    shl ax, 1
    shl ax, 1
    xor dx, dx
    mov dl, [wb_fracy]
    add ax, dx
    sub ax, bx
    cmp ax, -36
    jl .miss
    cmp ax, 36
    jg .miss
    mov bx, ax
    mov al, cl
    cbw
    shl ax, 1                  ; paddle motion adds a modest directional impulse
    shl ax, 1
    shl ax, 1
    add ax, bx
    cmp ax, -36
    jge .upper
    mov ax, -36
.upper:
    cmp ax, 36
    jle .angle
    mov ax, 36
.angle:
    mov cx, ax                 ; signed impact offset
    or ax, ax
    jns .positive
    neg ax
.positive:
    mov bx, 10
    mul bx
    add ax, 18
    mov bx, 36
    xor dx, dx
    div bx
.index:
    mov bx, ax
    shl bx, 1
    cmp byte [wb_returns], 16
    jae .speed
    inc byte [wb_returns]
.speed:
    mov dl, [wb_returns]
    shr dl, 1
    add dl, WB_SPEED_MIN
    mov al, [wb_angles+bx]
    mul dl                     ; Q8 cos; AH is rounded quarter-column speed
    add ax, 128
    mov [wb_vx], ah
    mov al, [wb_angles+bx+1]
    mul dl                     ; Q8 sin/2 corrects the text-cell aspect ratio
    add ax, 128
    mov al, ah
    or cx, cx
    jns .vy
    neg al
.vy:
    mov [wb_vy], al
    clc
    ret
.miss:
    stc
    ret

; 0,6,...60 degrees: round(255*cos(a)), round(128*sin(a)). No runtime trig.
wb_angles:
    db 255,0, 254,13, 249,27, 243,40, 233,52, 221,64
    db 206,75, 190,86, 171,95, 150,104, 128,111
wb_serveangles: db -2,-1,1,2

; DX = contact plane, BL = local paddle, CL = its movement. Test at the
; exact plane crossing, including the quarter-unit fraction, not at the end
; of the microstep (which can already be above/below a grazing paddle).
wb_contact:
    mov [wb_contactpad], bl
    mov [wb_contactspin], cl
    shl dx, 1
    shl dx, 1
    mov [wb_plane], dx
    sub dx, [wb_prevx]
    mov [wb_gap], dx
    mov ax, dx
    imul word [wb_stepvy]
    idiv word [wb_stepvx]
    add ax, [wb_prevy]
    jns .bottom
    neg ax
.bottom:
    cmp ax, WB_YMAX*4
    jle .impact
    neg ax
    add ax, WB_YMAX*8
.impact:
    mov [wb_impact], ax
    mov dx, ax
    and dl, 3
    mov [wb_fracy], dl
    shr ax, 1
    shr ax, 1
    mov [wb_bally], ax
    mov bl, [wb_contactpad]
    mov cl, [wb_contactspin]
    jmp wb_hit

wb_finishcontact:
    mov ax, [wb_stepvx]
    sub ax, [wb_gap]
    mov [wb_remainder], ax
    mov al, [wb_vx]
    cbw
    imul word [wb_remainder]
    idiv word [wb_stepvx]
    add ax, [wb_plane]
    mov dx, ax
    and dl, 3
    mov [wb_fracx], dl
    shr ax, 1
    shr ax, 1
    mov [wb_ballx], ax
    mov al, [wb_vy]
    cbw
    imul word [wb_remainder]
    idiv word [wb_stepvx]
    add ax, [wb_impact]
    jns .bottom
    neg ax
    neg byte [wb_vy]
.bottom:
    cmp ax, WB_YMAX*4
    jle .store
    neg ax
    add ax, WB_YMAX*8
    neg byte [wb_vy]
.store:
    mov dx, ax
    and dl, 3
    mov [wb_fracy], dl
    shr ax, 1
    shr ax, 1
    mov [wb_bally], ax
    ret

; Each paddle is judged locally. Freeze and repeat a center-crossing grant
; until the receiving owner replies with the same generation.
wb_pass:
    cmp byte [wb_practice], 0
    jne .out
    cmp byte [wb_side], 0
    jne .leftwards
    cmp word [wb_ballx], 312
    jb .out
    mov byte [wb_owner], 1
    jmp .grant
.leftwards:
    cmp word [wb_ballx], 312
    jae .out
    mov byte [wb_owner], 0
.grant:
    inc word [wb_generation]
    mov byte [wb_pending], 1
    mov byte [wb_urgent], 1
.out:
    ret

wb_board:
    cmp byte [wv_graphics], 0
    jne wv_board
    push es
    mov ax, [wb_vseg]
    mov es, ax
    xor di, di
    mov ax, 0x0720
    mov cx, 2000
.clear:
    mov [es:di], ax
    add di, 2
    loop .clear
    mov di, 2*160
    mov ax, 0x0bcd              ; CP437 double horizontal rule
    mov cx, 80
.top:
    mov [es:di], ax
    mov [es:di+21*160], ax
    add di, 2
    loop .top
    mov byte [wb_marked], 0xff
    mov word [wb_drawold], 0xffff
    mov word [wb_drawold+2], 0xffff
    mov word [wb_drawold+4], 0xffff
    mov di, wb_hudold
    mov cx, 80
.invalid:
    mov byte [di], 0xff
    inc di
    loop .invalid
    mov di, 24*160
    mov si, wb_footer
    mov ah, 7
.footer:
    lodsb                       ; DS is the package; ES is video, no STOS/MOVS
    or al, al
    jz .done
    mov [es:di], ax
    add di, 2
    jmp .footer
.done:
    pop es
    ret

; Erase just the previous sprites, then stamp this frame's three small objects.
wb_draw:
    cmp byte [wv_graphics], 0
    jne wv_draw
    push es
    mov ax, [wb_vseg]
    mov es, ax
    call wb_seam
    mov si, wb_drawold
    mov cx, 2
.erasep:
    mov di, [si]
    cmp di, 0xffff
    je .nextp
    mov ax, 0x0720
    mov [es:di], ax
    mov [es:di+160], ax
    mov [es:di+320], ax
    mov [es:di+480], ax
.nextp:
    add si, 2
    loop .erasep
    mov di, [wb_drawold+4]
    cmp di, 0xffff
    je .new
    mov word [es:di], 0x0720
.new:
    mov word [wb_drawold], 0xffff
    mov word [wb_drawold+2], 0xffff
    mov word [wb_drawold+4], 0xffff
    cmp byte [wb_phase], 2
    jne .hud
    cmp byte [wb_side], 0
    jne .rightp
    mov al, [wb_left]
    mov bx, 2
    call wb_paddlepos
    mov [wb_drawold], di
    call wb_paddledraw
    cmp byte [wb_practice], 0
    je .ball
.rightp:
    mov al, [wb_right]
    mov bx, 77
    call wb_paddlepos
    mov [wb_drawold+2], di
    call wb_paddledraw
.ball:
    mov bx, [wb_ballx]
    shr bx, 1
    shr bx, 1
    cmp byte [wb_side], 0
    je .view
    sub bx, 78
.view:
    cmp bx, 78
    jae .hud                    ; the ball is on the OTHER monitor
    inc bx
    mov ax, [wb_bally]
    shr ax, 1
    shr ax, 1
    call wb_paddlepos_word
    mov [wb_drawold+4], di
    mov word [es:di], 0x0ffe     ; white square, legible on monochrome too
.hud:
    pop es
    call wb_huddraw
    ret
wb_seam:
    mov al, [wb_phase]
    shl al, 1
    or al, [wb_side]
    cmp byte [wb_practice], 0
    je .tag
    or al, 8
.tag:
    cmp al, [wb_marked]
    je .out
    mov [wb_marked], al
    mov di, 3*160
    mov cx, WB_ROWS
.clear:
    mov word [es:di], 0x0720
    mov word [es:di+158], 0x0720
    add di, 160
    loop .clear
    cmp byte [wb_practice], 0
    jne .out
    cmp byte [wb_phase], 2
    jne .out
    mov di, 3*160+158
    mov ax, 0x0b3e              ; '>' points toward the other screen
    cmp byte [wb_side], 0
    je .draw
    mov di, 3*160
    mov al, '<'
.draw:
    mov cx, WB_ROWS/2
.row:
    mov [es:di], ax
    add di, 320
    loop .row
.out:
    ret

wb_paddlepos:
    shr al, 1
    shr al, 1
    xor ah, ah
wb_paddlepos_word:
    add ax, 3
    mov dx, 160
    mul dx
    shl bx, 1
    add ax, bx
    mov di, ax
    ret
wb_paddledraw:
    mov ax, 0x0fdb
    mov [es:di], ax
    mov [es:di+160], ax
    mov [es:di+320], ax
    mov [es:di+480], ax
    ret

wb_huddraw:
    cmp byte [wb_practice], 0
    jne .make
    call wb_service
.make:
    call wb_hudmake
    cmp byte [wb_practice], 0
    jne .paint
    call wb_service
.paint:
    jmp wb_hudput
wb_hudmake:
    ; Build a fixed-width line in RAM. Only cells that changed reach VRAM.
    mov di, wb_hud
    mov cx, 80
.blank:
    mov byte [di], ' '
    inc di
    loop .blank
    mov di, wb_hud
    mov si, wb_brand
    call wb_hudstr
    mov di, wb_hud+12
    mov si, wb_search
    cmp word [wb_ports+WP_BASE], 0
    jne .ports
    cmp word [wb_ports+WP_SIZE+WP_BASE], 0
    jne .ports
    mov si, wb_noport
.ports:
    cmp byte [wb_practice], 0
    je .serial
    mov si, wb_practicetext
    jmp .status
.serial:
    cmp byte [wb_phase], 0
    je .status
    mov si, wb_handshake
    cmp byte [wb_phase], 2
    jne .status
    mov si, wb_lefttext
    cmp byte [wb_side], 0
    je .status
    mov si, wb_righttext
.status:
    call wb_hudstr
    cmp byte [wb_phase], 2
    jne .write
    cmp byte [wb_practice], 0
    jne .score
    mov byte [wb_hud+36], 'C'
    mov byte [wb_hud+37], 'O'
    mov byte [wb_hud+38], 'M'
    mov byte [wb_hud+39], '2'
    mov bx, [wb_port]
    cmp word [bx+WP_BASE], 0x3f8
    jne .score
    mov byte [wb_hud+39], '1'
.score:
    mov di, wb_hud+44
    mov al, [wb_scorel]
    call wb_hudnumber
    mov byte [di], ':'
    inc di
    mov al, [wb_scorer]
    call wb_hudnumber
    mov di, wb_hud+53
    mov si, wb_rally
    cmp byte [wb_count], 0
    je .pause
    mov si, wb_ready
.pause:
    test byte [wb_flags], 1
    jz .over
    mov si, wb_paused
.over:
    test byte [wb_flags], 2
    jz .word
    mov si, wb_winleft
    mov al, [wb_scorel]
    cmp al, [wb_scorer]
    ja .word
    mov si, wb_winright
.word:
    call wb_hudstr
.write:
    ret
wb_hudput:
    push es
    mov ax, [wb_vseg]
    mov es, ax
    xor bx, bx
    mov di, 160
    mov cx, 80
.cell:
    mov al, [wb_hud+bx]
    cmp al, [wb_hudold+bx]
    je .skip
    mov [wb_hudold+bx], al
    mov ah, 0x0f
    mov [es:di], ax
.skip:
    inc bx
    add di, 2
    loop .cell
    pop es
    ret
wb_hudstr:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp wb_hudstr
.out:
    ret
wb_hudnumber:
    xor ah, ah
    mov dl, 10
    div dl
    add ax, 0x3030
    mov [di], al
    mov [di+1], ah
    add di, 2
    ret

wb_brand: db 'WIREBALL',0
wb_search: db 'Searching COM1/COM2...',0
wb_noport: db 'No COM1/COM2 UART found.',0
wb_handshake: db 'Opponent found - linking...',0
wb_lefttext: db 'LEFT - serial linked',0
wb_righttext: db 'RIGHT - serial linked',0
wb_practicetext: db 'Practice - you are LEFT',0
wb_rally: db 'Rally!',0
wb_ready: db 'Get ready...',0
wb_paused: db 'Paused (P resumes)',0
wb_winleft: db 'LEFT wins! N: new match',0
wb_winright: db 'RIGHT wins! N: new match',0
wb_footer: db '  Up/Down or W/S: paddle  P: pause  N: new  M: sound  Esc: desktop',0
