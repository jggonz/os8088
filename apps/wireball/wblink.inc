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
    mov al, [wv_fast]
    mov [wb_async], al
    pop es
    ret
wb_openone:
    mov byte [si+WP_QHEAD], 0
    mov byte [si+WP_QTAIL], 0
    mov byte [si+WP_QERR], 0
    mov word [si+WP_BASE], 0
    mov byte [si+WP_TLEN], 0
    mov byte [si+WP_TPOS], 0
    mov byte [si+WP_RLEN], 0
    mov byte [si+WP_ESC], 0
    or dx, dx
    jz .out
    ; Every 8250 has a divisor latch; the original has no scratch register.
    ; Read back two patterns, then restore DLL/LCR even on refusal (§109.1).
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
    mov byte [wb_async], 0
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
    cmp byte [wb_async], 0
    jne wb_queued
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
    mov byte [wb_left], 32
    mov byte [wb_right], 32
    mov byte [wb_command], 0
    mov byte [wb_ack], 0
    mov byte [wb_peerack], 0
    mov byte [wb_owner], 0
    mov word [wb_generation], 0
    mov byte [wb_pending], 0
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
    cmp byte [di+3], WB_STATE
    jne .input
    call wb_state_valid
    jc .out
    mov ax, [di+26]
    cmp byte [wb_phase], 2
    jne .initial
    cmp ax, [wb_generation]
    jne .grant
    mov al, [wb_owner]
    cmp al, [wb_side]
    jne .remote_state
    cmp al, [di+25]
    jne .out
    mov byte [wb_urgent], 1     ; repeat the acknowledgment, never rewind
    jmp .heartbeat
.remote_state:
    cmp al, [di+25]
    jne .out
    jmp .state
.grant:
    mov ax, [wb_generation]
    inc ax
    cmp ax, [di+26]
    jne .out
    mov al, [wb_owner]
    cmp al, [wb_side]
    je .out                    ; only the previous owner may grant the ball
    mov al, [wb_side]
    cmp al, [di+25]
    jne .out
    mov byte [wb_urgent], 1
    jmp .state
.initial:
    cmp byte [wb_side], 1
    jne .out
    cmp word [di+26], 0
    jne .initial_grant
    cmp byte [di+25], 0
    jne .out
    jmp .state
.initial_grant:
    cmp word [di+26], 1
    jne .out
    cmp byte [di+25], 1
    jne .out
    mov byte [wb_urgent], 1
.state:
    call wb_sound_remote
    push word [wb_left]         ; preserve the paddle displayed on this PC
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
    cmp byte [wb_side], 0
    jne .keep_right
    mov [wb_left], al
    jmp .metadata
.keep_right:
    mov [wb_right], ah
.metadata:
    mov al, [di+25]
    mov [wb_owner], al
    mov ax, [di+26]
    mov [wb_generation], ax
    mov al, [di+28]
    mov [wb_returns], al
    mov byte [wb_pending], 0
    mov byte [wb_fracx], 0
    mov byte [wb_fracy], 0
    mov al, [di+24]
    mov [wb_ack], al
    mov byte [wb_phase], 2
    mov al, [wb_owner]
    cmp al, [wb_side]
    jne .heartbeat
    mov al, [wb_command]
    cmp al, [wb_ack]
    je .heartbeat
    ; A request can be in flight when the old owner grants the ball. Finish
    ; it here and cancel its wire counter back to the grant's last ACK.
    ; Generation checks discard old INPUTs; rebasing prevents a later replay.
    mov al, [wb_cmdtype]
    call wb_applycommand
    mov al, [wb_ack]
    mov [wb_command], al
    mov byte [wb_urgent], 1
    jmp .heartbeat
.input:
    cmp byte [di+3], WB_INPUT
    jne .out
    cmp byte [di+12], WB_PMAX
    ja .out
    cmp byte [di+13], WB_PMAX
    ja .out
    cmp byte [di+22], 2
    ja .out
    mov ax, [di+26]
    cmp ax, [wb_generation]
    jne .out
    mov al, [di+25]
    cmp al, [wb_owner]
    jne .out
    cmp al, [wb_side]
    jne .out
    cmp byte [wb_phase], 2
    je .paddle
    call wb_newgame
    mov byte [wb_phase], 2
.paddle:
    cmp byte [wb_side], 0
    jne .leftpaddle
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
    jmp .command
.leftpaddle:
    mov al, [di+12]
    mov [wb_left], al
.command:
    mov al, [di+24]
    cmp al, [wb_peerack]
    je .heartbeat
    mov [wb_peerack], al
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
    cmp byte [di+25], 1
    ja .bad
    cmp byte [di+28], 16
    ja .bad
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
    cmp byte [wb_urgent], 0
    jne .send
    mov ax, [wb_last]
    sub ax, [wb_send]
    mov cx, 3
    cmp byte [wb_phase], 2
    je .rate
    mov cx, 18
.rate:
    cmp ax, cx
    jb .out
.send:
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
    mov byte [wb_urgent], 0
.out:
    ret

wb_packet_make:
    mov word [wb_packet], 0x4257
    mov byte [wb_packet+2], WB_PROTOCOL
    mov byte [wb_packet+3], WB_HELLO
    cmp byte [wb_phase], 2
    jne .ids
    mov byte [wb_packet+3], WB_STATE
    mov al, [wb_owner]
    cmp al, [wb_side]
    je .ids
    cmp byte [wb_pending], 0
    jne .ids
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
    mov al, [wb_peerack]
    cmp byte [wb_packet+3], WB_INPUT
    jne .ack
    mov al, [wb_cmdtype]
    mov [wb_packet+22], al
    mov al, [wb_command]
.ack:
    mov [wb_packet+24], al
    mov al, [wb_owner]
    mov [wb_packet+25], al
    mov ax, [wb_generation]
    mov [wb_packet+26], ax
    mov al, [wb_returns]
    mov [wb_packet+28], al
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
    mov [wb_packet+WB_FRAME-1], al
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
    pushf
    cli
    mov [si+WP_TLEN], al
    mov byte [si+WP_TPOS], 0
    popf
    ret

; A 582 Hz FSXF_RATE hook keeps the 8250 serviced during retrace waits.
; No kernel calls or frame decoding here; depth including helpers is 16 bytes.
wb_ratehook:
    add [wv_clock], ax
    cmp byte [wb_async], 0
    je .out
    mov si, wb_ports
    call wb_capture
    mov si, wb_ports+WP_SIZE
    call wb_capture
.out:
    ret
wb_capture:
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
    mov cl, [si+WP_QHEAD]
    mov ch, cl
    inc ch
    and ch, 63
    cmp ch, [si+WP_QTAIL]
    je .full
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov di, si
    add di, bx
    mov [di+WP_QUEUE], ax
    mov [si+WP_QHEAD], ch
    jmp .received
.full:
    mov byte [si+WP_QERR], 1
.received:
    add dx, 5
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
wb_queued:
    cmp byte [si+WP_QERR], 0
    je .start
    mov byte [si+WP_QERR], 0
    mov byte [si+WP_RLEN], 0xff
    mov byte [si+WP_ESC], 0
.start:
    mov bp, 16                  ; bounded decode work between motion frames
.byte:
    mov bl, [si+WP_QTAIL]
    cmp bl, [si+WP_QHEAD]
    je .out
    xor bh, bh
    shl bx, 1
    mov di, si
    add di, bx
    mov ax, [di+WP_QUEUE]
    inc byte [si+WP_QTAIL]
    and byte [si+WP_QTAIL], 63
    test ah, 0x1e
    jnz .error
    call wb_receive
    jmp .next
.error:
    mov byte [si+WP_RLEN], 0xff
    mov byte [si+WP_ESC], 0
.next:
    dec bp
    jnz .byte
.out:
    ret
