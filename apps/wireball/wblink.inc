; WireBall's polled UART transport. All port access is inside the FSX bracket.
; A UART byte at 2400/8N1 is 4.17ms; the hot loop never sleeps or calls disk/video BIOS.
wb_identity:
    call OSAPI_RAND
    mov [wb_identity_value], ax
    ; A running hardware counter breaks ties even between identically booted peers.
    mov al, 0
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xor ax, [wb_last]
    or ax, 1
    mov [wb_identity_value+2], ax
    ret

wb_openports:
    push es
    pushf
    cli
    in al, 0x21
    mov [wb_pic], al
    or al, 0x18                ; mask BOTH: mouse IRQs scan both UARTs
    out 0x21, al
    popf
    mov ax, 0x40
    mov es, ax
    mov si, wb_ports
    mov dx, [es:0]
    cmp dx, 0x3f8
    je .first
    xor dx, dx
.first:
    call wb_openone
    mov si, wb_ports+WP_SIZE
    mov dx, [es:2]
    cmp dx, 0x2f8
    je .second
    xor dx, dx
.second:
    call wb_openone
    pop es
    ret
wb_openone:
    mov word [si+WP_BASE], 0
    mov byte [si+WP_TLEN], 0
    mov byte [si+WP_TPOS], 0
    mov byte [si+WP_RLEN], 0
    mov byte [si+WP_ESC], 0
    or dx, dx
    jz .out
    ; Every 8250 has a divisor latch; the original has no scratch register.
    ; Read back two patterns, then restore DLL/LCR even on refusal (§107.1).
    add dx, 3
    in al, dx
    mov [si+WP_LCR], al
    or al, 0x80
    out dx, al
    sub dx, 3
    in al, dx
    mov ah, al
    mov al, 0x55
    out dx, al
    jmp short $+2
    in al, dx
    mov cl, al
    mov al, 0xaa
    out dx, al
    jmp short $+2
    in al, dx
    mov ch, al
    mov al, ah
    out dx, al
    add dx, 3
    mov al, [si+WP_LCR]
    out dx, al
    sub dx, 3
    cmp cl, 0x55
    jne .out
    cmp ch, 0xaa
    jne .out
    mov [si+WP_BASE], dx
    add dx, 3
    in al, dx
    mov [si+WP_LCR], al
    and al, 0x7f
    out dx, al
    sub dx, 2
    in al, dx
    mov [si+WP_IER], al
    xor al, al
    out dx, al
    add dx, 2
    mov al, 0x80
    out dx, al
    sub dx, 3
    in al, dx
    mov [si+WP_DLL], al
    inc dx
    in al, dx
    mov [si+WP_DLM], al
    xor al, al
    out dx, al
    dec dx
    mov al, 48                 ; 115200 / 2400
    out dx, al
    add dx, 3
    mov al, 3                  ; 8N1, DLAB clear
    out dx, al
    inc dx
    in al, dx
    mov [si+WP_MCR], al
    or al, 3                   ; DTR/RTS asserted; no CTS/DSR gating
    and al, 0xe7               ; OUT2 and diagnostic loopback disabled
    out dx, al
    inc dx
    in al, dx
    test al, 1
    jz .out
    sub dx, 5
    in al, dx                  ; discard the mouse's old byte
.out:
    ret

wb_closeports:
    pushf
    cli
    mov si, wb_ports
    call wb_closeone
    mov si, wb_ports+WP_SIZE
    call wb_closeone
    in al, 0x21
    and al, 0xe7
    mov ah, [wb_pic]
    and ah, 0x18
    or al, ah
    out 0x21, al
    popf
    ret
wb_closeone:
    mov dx, [si+WP_BASE]
    or dx, dx
    jz .out
    add dx, 3
    mov al, 0x80
    out dx, al
    sub dx, 3
    mov al, [si+WP_DLL]
    out dx, al
    inc dx
    mov al, [si+WP_DLM]
    out dx, al
    add dx, 2
    mov al, [si+WP_LCR]
    and al, 0x7f
    out dx, al
    inc dx
    mov al, [si+WP_MCR]
    out dx, al
    sub dx, 3
    mov al, [si+WP_IER]
    out dx, al
    add dx, 2
    mov al, [si+WP_LCR]
    out dx, al
.out:
    ret

wb_service:
    mov si, wb_ports
    call wb_serviceone
    mov si, wb_ports+WP_SIZE
    call wb_serviceone
    ret
wb_serviceone:
    mov dx, [si+WP_BASE]
    or dx, dx
    jz .out
    add dx, 5
    in al, dx
    mov ah, al
    test al, 1
    jz .tx
    sub dx, 5
    in al, dx
    test ah, 0x1e              ; overrun/parity/framing/break invalidate partial frame
    jnz .error
    push dx
    call wb_receive
    pop dx
    jmp .txbase
.error:
    mov byte [si+WP_RLEN], 0xff
    mov byte [si+WP_ESC], 0
.txbase:
    add dx, 5
    in al, dx
    mov ah, al
.tx:
    test ah, 0x20
    jz .out
    mov bl, [si+WP_TPOS]
    cmp bl, [si+WP_TLEN]
    jae .out
    xor bh, bh
    mov di, si
    add di, bx
    mov al, [di+WP_TX]
    sub dx, 5
    out dx, al
    inc byte [si+WP_TPOS]
.out:
    ret

; AL = received wire byte, SI = the port. Delimiters bound a decoded frame.
wb_receive:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp al, 0x7e
    je .delimiter
    cmp byte [si+WP_RLEN], WB_FRAME
    jae .bad
    cmp byte [si+WP_ESC], 0
    jne .escaped
    cmp al, 0x7d
    jne .store
    mov byte [si+WP_ESC], 1
    jmp .out
.escaped:
    mov byte [si+WP_ESC], 0
    xor al, 0x20
.store:
    mov bl, [si+WP_RLEN]
    xor bh, bh
    mov di, si
    add di, bx
    mov [di+WP_RX], al
    inc byte [si+WP_RLEN]
    jmp .out
.bad:
    mov byte [si+WP_RLEN], 0xff
    jmp .out
.delimiter:
    cmp byte [si+WP_ESC], 0
    jne .reset
    cmp byte [si+WP_RLEN], WB_FRAME
    jne .reset
    call wb_accept
.reset:
    mov byte [si+WP_RLEN], 0
    mov byte [si+WP_ESC], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wb_accept:
    mov di, si
    add di, WP_RX
    xor al, al
    mov bx, di
    mov cx, WB_FRAME
.sum:
    xor al, [bx]
    inc bx
    loop .sum
    or al, al
    jnz .out
    cmp word [di], 0x4257
    jne .out
    cmp byte [di+2], WB_PROTOCOL
    jne .out
    cmp byte [di+3], WB_BYE
    ja .out
    ; An addressed packet must be ours, including during discovery.
    mov ax, [di+8]
    mov dx, [di+10]
    mov bx, ax
    or bx, dx
    jz .broadcast
    cmp ax, [wb_identity_value]
    jne .out
    cmp dx, [wb_identity_value+2]
    jne .out
    jmp .sender
.broadcast:
    cmp byte [di+3], WB_HELLO
    jne .out
.sender:
    mov ax, [di+4]
    mov dx, [di+6]
    mov bx, ax
    or bx, dx
    jz .out
    cmp byte [wb_bound], 0
    jne .bound
    cmp byte [di+3], WB_HELLO
    jne .out
    cmp dx, [wb_identity_value+2]
    jne .bind
    cmp ax, [wb_identity_value]
    jne .bind
    call wb_identity           ; equal HELLO identity: restart with fresh entropy
    jmp .out
.bind:
    mov [wb_peer], ax
    mov [wb_peer+2], dx
    mov [wb_port], si
    mov byte [wb_bound], 1
    mov byte [wb_phase], 1
    mov byte [wb_left], 8
    mov byte [wb_right], 8
    mov byte [wb_command], 0
    mov byte [wb_ack], 0
    mov byte [wb_side], 0
    cmp dx, [wb_identity_value+2]
    ja .bound
    jb .right
    cmp ax, [wb_identity_value]
    ja .bound
.right:
    mov byte [wb_side], 1
.bound:
    cmp si, [wb_port]
    jne .out
    cmp ax, [wb_peer]
    jne .out
    cmp dx, [wb_peer+2]
    jne .out
    cmp byte [di+3], WB_BYE
    je .lost
    cmp byte [di+3], WB_HELLO
    je .hello
    cmp byte [wb_side], 0
    je .input
    cmp byte [di+3], WB_STATE
    jne .out
    call wb_state_valid
    jc .out
    call wb_sound_remote
    push word [wb_right]        ; retain local input; a delayed echo must not move our paddle back
    mov bx, wb_left
    push di
    add di, 12
    mov cx, 12
.copy:
    mov al, [di]
    mov [bx], al
    inc di
    inc bx
    loop .copy
    pop di
    pop ax
    mov [wb_right], al
    mov al, [di+24]
    mov [wb_ack], al
    mov byte [wb_phase], 2
    jmp .heartbeat
.input:
    cmp byte [di+3], WB_INPUT
    jne .out
    cmp byte [di+13], WB_PMAX
    ja .out
    cmp byte [di+22], 2
    ja .out
    cmp byte [wb_phase], 2
    je .paddle
    call wb_newgame
    mov byte [wb_phase], 2
.paddle:
    mov al, [di+13]
    mov byte [wb_rspin], 0
    cmp al, [wb_right]
    je .still
    mov byte [wb_rspin], 1
    ja .still
    mov byte [wb_rspin], -1
.still:
    mov byte [wb_spinage], 0
    mov [wb_right], al
    mov al, [di+24]
    cmp al, [wb_ack]
    je .heartbeat
    mov [wb_ack], al
    mov al, [di+22]
    call wb_applycommand
    jmp .heartbeat
.hello:
    cmp byte [wb_phase], 2
    je .heartbeat
    ; HELLO with echoed identity proves the remote peer received ours.
    mov ax, [di+8]
    or ax, [di+10]
    jz .heartbeat
    cmp byte [wb_side], 0
    jne .heartbeat
    call wb_newgame
    mov byte [wb_phase], 2
.heartbeat:
    call OSAPI_GET_TICKS
    mov [wb_seen], ax
    ret
.lost:
    call wb_lost
.out:
    ret

wb_state_valid:
    cmp byte [di+12], WB_PMAX
    ja .bad
    cmp byte [di+13], WB_PMAX
    ja .bad
    cmp word [di+14], 620
    ja .bad
    cmp word [di+16], WB_YMAX
    ja .bad
    mov al, [di+18]
    or al, al
    jz .bad
    jns .speed
    neg al
.speed:
    cmp al, WB_SPEED_MAX
    ja .bad
.vx:
    cmp byte [di+19], -8
    jl .bad
    cmp byte [di+19], 8
    jg .bad
    cmp byte [di+20], 11
    ja .bad
    cmp byte [di+21], 11
    ja .bad
    cmp byte [di+22], 3
    ja .bad
    cmp byte [di+23], 36
    ja .bad
    clc
    ret
.bad:
    stc
    ret

wb_lost:
    call wb_sound_quiet
    mov byte [wb_bound], 0
    mov byte [wb_phase], 0
    mov word [wb_peer], 0
    mov word [wb_peer+2], 0
    mov word [wb_port], 0
    mov byte [wb_command], 0
    mov byte [wb_ack], 0
    call OSAPI_GET_TICKS
    mov [wb_send], ax
    sub word [wb_send], 18
    ret

wb_schedule:
    mov ax, [wb_last]
    sub ax, [wb_send]
    mov cx, 3
    cmp byte [wb_phase], 2
    je .rate
    mov cx, 18
.rate:
    cmp ax, cx
    jb .out
    mov ax, [wb_last]
    mov [wb_send], ax
    mov si, wb_ports
    call wb_scheduleone
    mov si, wb_ports+WP_SIZE
    call wb_scheduleone
.out:
    ret
wb_scheduleone:
    cmp word [si+WP_BASE], 0
    je .out
    cmp byte [wb_bound], 0
    je .room
    cmp si, [wb_port]
    jne .out
.room:
    mov al, [si+WP_TPOS]
    cmp al, [si+WP_TLEN]
    jb .out                      ; never wait for a busy UART
    call wb_packet_make
    call wb_encode
.out:
    ret

wb_packet_make:
    mov word [wb_packet], 0x4257
    mov byte [wb_packet+2], WB_PROTOCOL
    mov byte [wb_packet+3], WB_HELLO
    cmp byte [wb_phase], 2
    jne .ids
    mov byte [wb_packet+3], WB_STATE
    cmp byte [wb_side], 0
    je .ids
    mov byte [wb_packet+3], WB_INPUT
.ids:
    mov ax, [wb_identity_value]
    mov [wb_packet+4], ax
    mov ax, [wb_identity_value+2]
    mov [wb_packet+6], ax
    mov ax, [wb_peer]
    mov [wb_packet+8], ax
    mov ax, [wb_peer+2]
    mov [wb_packet+10], ax
    mov bx, wb_left
    mov di, wb_packet+12
    mov cx, 12
.copy:
    mov al, [bx]
    mov [di], al
    inc bx
    inc di
    loop .copy
    mov al, [wb_ack]
    cmp byte [wb_side], 0
    je .ack
    mov al, [wb_cmdtype]
    mov [wb_packet+22], al
    mov al, [wb_command]
.ack:
    mov [wb_packet+24], al
    ret

wb_encode:
    mov di, si
    add di, WP_TX
    mov byte [di], 0x7e
    inc di
    mov bx, wb_packet
    xor al, al
    mov cx, WB_FRAME-1
.sum:
    xor al, [bx]
    inc bx
    loop .sum
    mov [wb_packet+25], al
    mov bx, wb_packet
    mov cx, WB_FRAME
.byte:
    mov al, [bx]
    inc bx
    cmp al, 0x7d
    je .escape
    cmp al, 0x7e
    jne .put
.escape:
    mov byte [di], 0x7d
    inc di
    xor al, 0x20
.put:
    mov [di], al
    inc di
    loop .byte
    mov byte [di], 0x7e
    inc di
    sub di, si
    sub di, WP_TX
    mov ax, di
    mov [si+WP_TLEN], al
    mov byte [si+WP_TPOS], 0
    ret
