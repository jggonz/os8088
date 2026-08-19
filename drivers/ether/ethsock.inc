; =============================================================================
; os8088 - drivers/ether/ethsock.inc
;
; THE SOCKET VERBS, and they belong to NEITHER END in particular.
;
; NETV_OPEN..NETV_ACCEPT over tcp.inc, cut out of ether.asm whole so that the
; DOS side of the parallel cable can serve the same verbs with the same code
; (drivers/net/os88net.asm, SPEC.md 62.11.1). Two implementations of a socket
; API is exactly the shape docs/NET-PLAN.md warned about at the replayer -
; a bug fixed in one is not fixed in the other - and this file is the answer.
;
; It needs, from whichever program includes it: the state in ethstate.inc,
; tcp.inc, inet.inc, dns.inc, and a link layer answering ne_tx/ne_rx.
; NETV_IDENT and NETV_STATE stay with their HOSTS, because what identifies a
; driver and what counts as a link are the one thing the two ends genuinely
; disagree about.
; =============================================================================
; --- NETV_OPEN ---------------------------------------------------------------
; in ES:SI = a NUL host, CX = the port; out AX = a handle
eth_v_open:
    mov [eth_usi], si           ; **BANKED, NOT RE-POPPED.** The obvious form -
    push bx                     ; pop the pushed SI back when the name is
    push cx                     ; wanted - reads the wrong slot the moment
    push dx                     ; anything is pushed after it, and there is
    push si                     ; something: DI
    push di
    call eth_pump
    mov si, eth_ip
    call ip_zero
    jz  .nolink
    mov dx, cx                  ; the port, across sk_alloc
    call sk_alloc
    jc  .full
    mov [eth_hnd], al           ; the handle, likewise
    mov [bx+SKO_RP], dx
    call tcp_lport
    mov [bx+SKO_LP], ax
    mov byte [bx+SKO_ST], NSK_CONNECT

    mov di, bx                  ; the name, out of their segment and into the
    add di, SKO_HOST            ; record - bounded by the record and never by
    mov cx, NET_HOSTMAX - 1     ; the NUL, which came off a package
    mov si, [eth_usi]
    call usr_read
    mov byte [bx+SKO_HOST+NET_HOSTMAX-1], 0

    mov si, bx
    add si, SKO_HOST
    mov di, bx
    add di, SKO_RIP
    call ip_parse               ; a dotted quad connects at once; a NAME waits
    jnc .go                     ; for dns_timer to pick it up
    or  byte [bx+SKO_FL], SKF_RES
    call OSAPI_GET_TICKS
    add ax, TCP_CONNTMO
    mov [bx+SKO_TMO], ax
    jmp short .ok
.go:
    call tcp_syn
.ok:
    mov al, [eth_hnd]
    xor ah, ah
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.nolink:
    mov ax, NETE_NOLINK
    jmp short .no
.full:
    mov ax, NETE_FULL
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; --- NETV_STATUS -------------------------------------------------------------
; in AL = handle; out AH = NSK_*, CX = readable now, AL = the handle
eth_v_status:
    push bx
    push dx
    call eth_pump
    call sk_h2p
    jc  .no
    mov dl, al
    mov cx, [bx+SKO_RXN]
    mov ah, [bx+SKO_ST]
    mov al, dl
    pop dx
    pop bx
    clc
    ret
.no:
    pop dx
    pop bx
    stc
    ret

; --- NETV_SEND ---------------------------------------------------------------
; in AL = handle, ES:SI = bytes, CX = how many; out CX = taken
eth_v_send:
    push ax
    push bx
    push dx
    push di
    call eth_pump
    call sk_h2p
    jc  .no
    cmp byte [bx+SKO_ST], NSK_UP
    je  .ok
    cmp byte [bx+SKO_ST], NSK_CLOSING   ; the peer stopped talking, not
    je  .ok                             ; listening: a half-close is legal
    mov ax, NETE_HANDLE
    jmp short .no
.ok:
    mov ax, SK_TXCAP
    sub ax, [bx+SKO_TXN]
    cmp cx, ax
    jbe .room
    mov cx, ax
.room:
    jcxz .none
    call sk_txbase
    add di, [bx+SKO_TXN]
    call usr_read
    add [bx+SKO_TXN], cx
    push cx
    call tcp_kick
    pop cx
.none:
    pop di
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop dx
    pop bx
    add sp, 2                   ; AX is the error and must survive the pops
    stc
    ret

; --- NETV_RECV ---------------------------------------------------------------
; in AL = handle, ES:DI = a buffer, CX = its capacity; out CX = delivered
eth_v_recv:
    push ax
    push bx
    push dx
    push si
    call eth_pump
    call sk_h2p
    jc  .no
    mov dx, [bx+SKO_RXN]
    call sk_rxget
    or  cx, cx
    jz  .out
    cmp byte [bx+SKO_ST], NSK_UP        ; **THE WINDOW JUST OPENED.** Without
    jne .out                            ; this a peer that filled the ring is
    cmp dx, SK_RXCAP / 2                ; waiting on an advertisement nothing
    jb  .out                            ; else in this driver would ever send,
    push cx                             ; and the transfer stops for good
    call tcp_ack
    pop cx
.out:
    pop si
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop dx
    pop bx
    add sp, 2
    stc
    ret

; --- NETV_CLOSE --------------------------------------------------------------
; in AL = a handle. The handle is theirs no more, whatever the far side says.
eth_v_close:
    push ax
    push bx
    call eth_pump
    call sk_h2p
    jc  .no
    or  byte [bx+SKO_FL], SKF_ORPH
    cmp byte [bx+SKO_TS], TS_ESTAB
    je  .fin
    cmp byte [bx+SKO_TS], TS_CLOSEW
    je  .fin
    and byte [bx+SKO_FL], ~SKF_RES      ; a NAME still being looked up is
    mov ah, [bx+SKO_IDX]                ; cancelled WITH the slot. [dns_sk]
    cmp ah, [dns_sk]                    ; lives outside the record, so
    jne .nores                          ; sk_alloc's mem_zero cannot reach it
    mov byte [dns_sk], 0xFF             ; and the reply would land on whoever
    mov word [dns_lport], 0             ; the slot went to next - dns_in writes
.nores:                                 ; SKO_RIP and calls tcp_syn on it
    mov byte [bx+SKO_TS], TS_CLOSED     ; nothing was ever connected, or it is
    mov byte [bx+SKO_ST], NSK_FREE      ; already over: the slot goes now
    jmp short .out
.fin:
    or  byte [bx+SKO_FL], SKF_FIN
    mov byte [bx+SKO_ST], NSK_CLOSED    ; ...but TCP keeps the slot until the
    call tcp_kick                       ; handshake is done (tcp_abort)
.out:
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    clc                                 ; a close of a handle that is already
    ret                                 ; gone is not an error

; --- NETV_LISTEN -------------------------------------------------------------
; in CX = a port; out AX = a listening handle
eth_v_listen:
    push bx
    push cx
    call eth_pump
    call sk_alloc
    jc  .full
    mov [bx+SKO_LP], cx
    mov byte [bx+SKO_TS], TS_LISTEN
    mov byte [bx+SKO_ST], NSK_LISTEN
    xor ah, ah
    pop cx
    pop bx
    clc
    ret
.full:
    mov ax, NETE_FULL
    pop cx
    pop bx
    stc
    ret

; --- NETV_ACCEPT -------------------------------------------------------------
; in AL = a listening handle; out AX = a NEW handle, or 0 with CF=0
eth_v_accept:
    push bx
    push cx
    push dx
    push si
    call eth_pump
    call sk_h2p
    jc  .no
    cmp byte [bx+SKO_TS], TS_LISTEN
    jne .no
    mov si, [bx+SKO_LP]         ; the port the connection must have arrived on
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    test byte [bx+SKO_FL], SKF_ACPT
    jz  .next
    cmp si, [bx+SKO_LP]
    jne .next
    cmp byte [bx+SKO_ST], NSK_UP
    jne .next
    and byte [bx+SKO_FL], ~SKF_ACPT
    mov al, dl
    inc al
    xor ah, ah
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.next:
    inc dl
    loop .l
    xor ax, ax                  ; nobody yet, which is the ORDINARY answer
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    mov ax, NETE_HANDLE
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

