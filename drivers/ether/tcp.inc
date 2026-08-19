; =============================================================================
; tcp.inc - TCP, stop-and-wait (SPEC.md 72.4)
;
; Four connections, one unacknowledged segment each. That is the whole
; congestion and flow-control story and it is a DELIBERATE floor rather than a
; first draft to be improved on:
;
;   * The wire is 10 Mbit and the machine is 4.77 MHz. A 5150 cannot fill a
;     window - PERFORMANCE.md prices a byte through this stack at far more
;     than a byte's time on the cable - so the thing a sliding window buys
;     (keeping the pipe full) is bought by nothing here.
;   * A retransmit queue is the part of TCP that is hard to get right, and
;     every bug in it is a bug that only appears under loss, which is the one
;     condition no harness in this tree can produce on demand.
;   * One segment in flight means the retransmit is always "send the front of
;     the buffer again", which is nine instructions and cannot desynchronise
;     from what the peer acknowledged.
;
; What it costs is throughput on a long path: one segment per round trip, so
; a 512-byte segment over a 30 ms link is ~17 KB/s. On a LAN it is faster than
; the machine, which is the case this exists for.
;
; --- SEQUENCE NUMBERS ARE 32-BIT AND MODULAR --------------------------------
; There are no 32-bit registers here, so a sequence number is a word pair and
; every comparison is a SUBTRACTION whose SIGN is the answer - `js`, never
; `jb` (SPEC.md 45.15's trap, in the place it was invented for). A sequence
; space that wraps at 4 GB wraps in practice on a link this stack could
; saturate for eleven hours, and the sign test costs the same as the wrong one.
;
; --- WHAT IS NOT HERE, AND WHY ----------------------------------------------
; No options, so no window scaling, no SACK and no timestamps: an MSS option
; is offered on our SYN and every option on theirs is skipped by the data
; offset. No reassembly of out-of-order segments - an unexpected sequence
; number is answered with a duplicate ACK and dropped, which is what makes the
; peer retransmit, and is correct rather than merely simple. No delayed ACK:
; a segment is acknowledged as it is taken, because the alternative is a timer
; whose only purpose is to save a frame on a machine that is not frame-bound.
; =============================================================================

; --- the TCP header ----------------------------------------------------------
TCO_SP      equ 0
TCO_DP      equ 2
TCO_SEQ     equ 4
TCO_ACK     equ 8
TCO_OFF     equ 12
TCO_FLG     equ 13
TCO_WIN     equ 14
TCO_CK      equ 16
TCO_URG     equ 18
TC_HLEN     equ 20

TF_FIN      equ 0x01
TF_SYN      equ 0x02
TF_RST      equ 0x04
TF_PSH      equ 0x08
TF_ACK      equ 0x10

; --- a connection's state ----------------------------------------------------
TS_CLOSED   equ 0
TS_LISTEN   equ 1
TS_SYNSENT  equ 2
TS_SYNRCVD  equ 3
TS_ESTAB    equ 4
TS_FINW1    equ 5
TS_FINW2    equ 6
TS_CLOSING  equ 7
TS_TIMEWT   equ 8
TS_LASTACK  equ 9
TS_CLOSEW   equ 10

; --- the socket record -------------------------------------------------------
SKO_ST      equ 0               ; NSK_*: what the PACKAGE is told
SKO_TS      equ 1               ; TS_*:  what TCP is doing
SKO_LP      equ 2               ; local port, host order
SKO_RP      equ 4               ; remote port, host order
SKO_RIP     equ 6               ; the peer, four bytes in WIRE order
SKO_SUNA    equ 10              ; the oldest byte we have not had acked
SKO_SNXT    equ 14              ; ...and one past the last we have sent
SKO_RNXT    equ 18              ; the next byte we expect
SKO_RXH     equ 22              ; the receive ring: write index...
SKO_RXT     equ 24              ; ...read index...
SKO_RXN     equ 26              ; ...and how much is in it
SKO_TXN     equ 28              ; bytes waiting to go
SKO_TXS     equ 30              ; of which this many are in flight, at the front
SKO_RTO     equ 32              ; the retransmit deadline, in ticks
SKO_TMO     equ 34              ; the overall one: connect, and TIME-WAIT
SKO_TRY     equ 36              ; retransmits so far
SKO_FL      equ 37              ; SKF_*
SKO_IDX     equ 38              ; our own slot number, so a buffer base is a
                                ; shift rather than a division
SKO_HOST    equ 39              ; NET_HOSTMAX bytes: the name, until DNS
SK_SZ       equ 128             ; A POWER OF TWO ON PURPOSE - `sk_ptr` is a
                                ; shift, and an 8086 `mul` writes DX, which is
                                ; live at three of the six call sites
%if SKO_HOST + NET_HOSTMAX > SK_SZ
  %error "ether: the socket record does not fit SK_SZ"
%endif

SK_RXCAP    equ 1024            ; and their shifts, below
SK_TXCAP    equ 512
SK_RXSH     equ 10
SK_TXSH     equ 9

SKF_FIN     equ 0x01            ; the application has closed: send a FIN once
                                ; everything it wrote has been acknowledged
SKF_FINS    equ 0x02            ; ...and it has gone (and costs a sequence
                                ; number, which is the half that is easy to
                                ; forget and shows up as a connection that
                                ; will not finish closing)
SKF_ORPH    equ 0x04            ; NETV_CLOSE has been called: the slot is the
                                ; driver's now and goes free when TCP is done
SKF_RES     equ 0x08            ; SKO_HOST needs a DNS lookup first
SKF_SYNA    equ 0x10            ; our SYN is the thing in flight
SKF_ACPT    equ 0x20            ; a listener made this and NETV_ACCEPT has not
                                ; been told about it yet. It is what stops one
                                ; connection being handed out twice, and what
                                ; frees a slot whose peer went away before
                                ; anybody asked for it

TCP_MSS     equ SK_TXCAP        ; we never send more than one buffer's worth,
                                ; so this is the honest number to advertise -
                                ; and it is under the 1460 an Ethernet path
                                ; allows, so nothing here can provoke a
                                ; fragment (inet.inc drops those)
TCP_RTO0    equ 9               ; ticks: half a second, doubling
TCP_RTMAX   equ 7               ; ...seven doublings is about a minute
TCP_CONNTMO equ 550             ; ticks: 30 seconds to get connected
TCP_TWTMO   equ 36              ; ticks in TIME-WAIT. The standard is minutes;
                                ; four sockets and a machine that reboots by
                                ; being switched off make that a way to run out
                                ; of handles, and two seconds is what a LAN's
                                ; stray segment actually needs

; -----------------------------------------------------------------------------
; sk_ptr - AL = slot 0..NET_SOCKS-1 -> BX = the record
; clobbers: BX, flags
; -----------------------------------------------------------------------------
sk_ptr:
    push cx
    mov bl, al
    xor bh, bh
    mov cl, 7
    shl bx, cl
    add bx, sk_tab
    pop cx
    ret

; -----------------------------------------------------------------------------
; sk_rxbase / sk_txbase - BX = a socket -> DI = its buffer
; -----------------------------------------------------------------------------
sk_rxbase:
    push ax
    push cx
    mov al, [bx+SKO_IDX]
    xor ah, ah
    mov di, ax
    mov cl, SK_RXSH
    shl di, cl
    add di, sk_rxb
    pop cx
    pop ax
    ret

sk_txbase:
    push ax
    push cx
    mov al, [bx+SKO_IDX]
    xor ah, ah
    mov di, ax
    mov cl, SK_TXSH
    shl di, cl
    add di, sk_txb
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sk_alloc - a free slot
; out: CF=0 and BX = a zeroed record, AL = its 1-based HANDLE; CF=1 = none
; -----------------------------------------------------------------------------
sk_alloc:
    push cx
    push di
    xor al, al
    mov cx, NET_SOCKS
.l:
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE
    je  .got
    inc al
    loop .l
    stc
    jmp short .out
.got:
    push ax
    mov di, bx
    mov cx, SK_SZ
    call mem_zero
    pop ax
    mov [bx+SKO_IDX], al
    inc al                      ; handles are 1-based: 0 is "no handle" in
    clc                         ; NETV_ACCEPT's answer
.out:
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; sk_h2p - AL = a handle -> BX = the record
; out: CF=1 = not a handle of theirs (and AX = NETE_HANDLE)
; -----------------------------------------------------------------------------
sk_h2p:
    or  al, al
    jz  .no
    cmp al, NET_SOCKS
    ja  .no
    dec al
    call sk_ptr
    inc al
    cmp byte [bx+SKO_ST], NSK_FREE
    je  .no
    clc
    ret
.no:
    mov ax, NETE_HANDLE
    stc
    ret

; =============================================================================
; 32-BIT SEQUENCE ARITHMETIC
; =============================================================================

; --- DI += AX (unsigned 16-bit), the pair at DS:DI ---------------------------
q_add:
    add [di], ax
    adc word [di+2], 0
    ret

; --- DX:AX = the pair at DS:SI minus the pair at DS:DI -----------------------
; The SIGN of the result is the comparison (see the header).
q_sub:
    mov ax, [si]
    mov dx, [si+2]
    sub ax, [di]
    sbb dx, [di+2]
    ret

; --- DX:AX <- the network-order dword at DS:SI -------------------------------
be32_get:
    mov ah, [si]
    mov al, [si+1]
    mov dx, ax
    mov ah, [si+2]
    mov al, [si+3]
    ret

; --- the network-order dword at DS:DI <- DX:AX -------------------------------
be32_put:
    mov [di], dh
    mov [di+1], dl
    mov [di+2], ah
    mov [di+3], al
    ret

; --- the pair at DS:DI <- DX:AX / DX:AX <- the pair at DS:SI -----------------
q_put:
    mov [di], ax
    mov [di+2], dx
    ret

q_get:
    mov ax, [si]
    mov dx, [si+2]
    ret

; =============================================================================
; THE RECEIVE RING
; =============================================================================

; -----------------------------------------------------------------------------
; sk_rxfree - BX = a socket -> AX = bytes it could still take
; -----------------------------------------------------------------------------
sk_rxfree:
    mov ax, SK_RXCAP
    sub ax, [bx+SKO_RXN]
    ret

; -----------------------------------------------------------------------------
; sk_rxput - CX bytes at DS:SI into socket BX's ring
; out: CX = how many were taken (the rest are dropped and NOT acknowledged)
; -----------------------------------------------------------------------------
sk_rxput:
    push ax
    push dx
    push si
    push di
    call sk_rxfree
    cmp cx, ax
    jbe .fits
    mov cx, ax
.fits:
    jcxz .out
    add [bx+SKO_RXN], cx
    call sk_rxbase              ; DI = the ring
    mov dx, cx
.l:
    mov ax, [bx+SKO_RXH]
    push di
    add di, ax
    mov al, [si]
    mov [di], al
    pop di
    inc si
    mov ax, [bx+SKO_RXH]
    inc ax
    cmp ax, SK_RXCAP
    jb  .wrap
    xor ax, ax
.wrap:
    mov [bx+SKO_RXH], ax
    loop .l
    mov cx, dx
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sk_rxget - up to CX bytes out of socket BX's ring, into the CALLER's segment
; in:  DI = the caller's offset, CX = its capacity
; out: CX = how many were delivered
; -----------------------------------------------------------------------------
sk_rxget:
    push ax
    push dx
    push si
    push di
    push es
    mov ax, [bx+SKO_RXN]
    cmp cx, ax
    jbe .fits
    mov cx, ax
.fits:
    jcxz .out
    sub [bx+SKO_RXN], cx
    mov dx, cx
    push di
    call sk_rxbase
    mov si, di                  ; SI = the ring
    pop di
    mov es, [eth_useg]
.l:
    push si
    add si, [bx+SKO_RXT]
    mov al, [si]
    pop si
    mov [es:di], al
    inc di
    mov ax, [bx+SKO_RXT]
    inc ax
    cmp ax, SK_RXCAP
    jb  .wrap
    xor ax, ax
.wrap:
    mov [bx+SKO_RXT], ax
    loop .l
    mov cx, dx
.out:
    pop es
    pop di
    pop si
    pop dx
    pop ax
    ret

; =============================================================================
; SENDING
; =============================================================================

; -----------------------------------------------------------------------------
; tcp_out - one segment for socket BX
; in:  AL = the flags, CX = data bytes from the FRONT of its send buffer
; out: CF=1 = nothing went (no ARP yet, or the card refused)
;
; The sequence number is always SKO_SUNA, because the front of the buffer is
; the only thing that can be in flight (see the header). That is what makes a
; retransmit and a first transmission the same call.
; -----------------------------------------------------------------------------
tcp_out:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [eth_oflg], al
    mov [eth_dlen], cx
    mov si, bx
    add si, SKO_RIP
    mov al, IPP_TCP
    call ip_start
    jc  .out

    mov di, eth_txb + IP_PAY
    mov ax, [bx+SKO_LP]
    call be_put
    add di, 2
    mov ax, [bx+SKO_RP]
    call be_put
    mov si, bx
    add si, SKO_SUNA
    call q_get
    mov di, eth_txb + IP_PAY + TCO_SEQ
    call be32_put
    mov si, bx
    add si, SKO_RNXT
    call q_get
    mov di, eth_txb + IP_PAY + TCO_ACK
    call be32_put
    mov byte [eth_txb+IP_PAY+TCO_OFF], 5 << 4
    mov al, [eth_oflg]
    mov [eth_txb+IP_PAY+TCO_FLG], al
    call sk_rxfree                      ; the window IS the free ring space,
    mov di, eth_txb + IP_PAY + TCO_WIN  ; computed here and nowhere else, so it
    call be_put                         ; cannot drift from the buffer
    mov word [eth_txb+IP_PAY+TCO_CK], 0
    mov word [eth_txb+IP_PAY+TCO_URG], 0

    mov cx, [eth_dlen]                  ; the data, from the front
    jcxz .nodata
    call sk_txbase
    mov si, di
    mov di, eth_txb + IP_PAY + TC_HLEN
    call mem_copy
.nodata:
    mov cx, [eth_dlen]
    add cx, TC_HLEN
    push cx
    call ip_pseudo                      ; BX = the pseudo-header sum. **BX is
    mov si, eth_txb + IP_PAY            ; the socket everywhere else in this
    call ip_ck_part                     ; file and it is not here** - which is
    mov ax, bx                          ; why nothing between these four lines
    not ax                              ; may touch the record
    pop cx
    mov [eth_txb+IP_PAY+TCO_CK], ax
    call ip_finish
    inc word [eth_nseg]
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
; tcp_ack - a bare acknowledgement for socket BX
; -----------------------------------------------------------------------------
tcp_ack:
    push ax
    push cx
    mov al, TF_ACK
    xor cx, cx
    call tcp_out
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_rst - refuse a segment that belongs to no connection
;
; Built entirely out of the frame in eth_rxb, because by definition there is no
; record to build it from. A machine that answers a stray segment with silence
; makes the peer retransmit it for a minute; one that answers with RST ends it
; in one round trip, and the case this is really for is our own TIME-WAIT
; having expired under a peer that had one more thing to say.
; -----------------------------------------------------------------------------
tcp_rst:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    test byte [eth_rxb+IP_PAY+TCO_FLG], TF_RST
    jnz .out                            ; never answer a reset with a reset
    mov si, eth_rxb + IP_OFF + IPO_SRC
    mov al, IPP_TCP
    call ip_start
    jc  .out
    mov si, eth_rxb + IP_PAY + TCO_DP   ; our port was their destination
    call be_get
    mov di, eth_txb + IP_PAY + TCO_SP
    call be_put
    mov si, eth_rxb + IP_PAY + TCO_SP
    call be_get
    mov di, eth_txb + IP_PAY + TCO_DP
    call be_put
    mov si, eth_rxb + IP_PAY + TCO_ACK  ; their ack becomes our sequence, and
    call be32_get                       ; their sequence plus everything it
    mov di, eth_txb + IP_PAY + TCO_SEQ  ; carried becomes our ack - which is
    call be32_put                       ; what makes the peer believe it
    mov si, eth_rxb + IP_PAY + TCO_SEQ
    call be32_get
    mov di, eth_ph2
    call q_put
    mov ax, [eth_seglen]
    mov di, eth_ph2
    call q_add
    test byte [eth_rxb+IP_PAY+TCO_FLG], TF_SYN
    jz  .nosyn
    mov ax, 1
    mov di, eth_ph2
    call q_add                          ; a SYN counts as a byte, and a RST
.nosyn:                                 ; that does not say so is ignored
    mov si, eth_ph2
    call q_get
    mov di, eth_txb + IP_PAY + TCO_ACK
    call be32_put
    mov byte [eth_txb+IP_PAY+TCO_OFF], 5 << 4
    mov byte [eth_txb+IP_PAY+TCO_FLG], TF_RST | TF_ACK
    mov word [eth_txb+IP_PAY+TCO_WIN], 0
    mov word [eth_txb+IP_PAY+TCO_CK], 0
    mov word [eth_txb+IP_PAY+TCO_URG], 0
    mov cx, TC_HLEN
    call ip_pseudo
    mov si, eth_txb + IP_PAY
    mov cx, TC_HLEN
    call ip_ck_part
    mov ax, bx
    not ax
    mov [eth_txb+IP_PAY+TCO_CK], ax
    mov cx, TC_HLEN
    call ip_finish
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_kick - send whatever socket BX has to send, if anything can go now
;
; ONE PLACE decides what goes on the wire for a connection, and every caller -
; the verbs, the timers, the receive path - comes through it. That is what
; keeps "is there a segment in flight" a question with one answer.
; -----------------------------------------------------------------------------
tcp_kick:
    push ax
    push cx
    push dx
    push di
    cmp word [bx+SKO_TXS], 0
    jne .out                            ; something is already in flight
    cmp byte [bx+SKO_TS], TS_ESTAB
    je  .data
    cmp byte [bx+SKO_TS], TS_CLOSEW
    je  .data
    jmp short .out
.data:
    mov cx, [bx+SKO_TXN]
    jcxz .fin
    cmp cx, TCP_MSS
    jbe .send
    mov cx, TCP_MSS
.send:
    mov al, TF_ACK | TF_PSH
    call tcp_out
    jc  .out                            ; ARP pending: the timer will retry
    mov [bx+SKO_TXS], cx
    mov di, bx
    add di, SKO_SNXT
    mov ax, cx
    call q_add
    call tcp_arm
    jmp short .out
.fin:
    test byte [bx+SKO_FL], SKF_FIN      ; nothing left to send: is a close owed?
    jz  .out
    test byte [bx+SKO_FL], SKF_FINS
    jnz .out
    mov al, TF_ACK | TF_FIN
    xor cx, cx
    call tcp_out
    jc  .out
    or  byte [bx+SKO_FL], SKF_FINS
    mov di, bx
    add di, SKO_SNXT
    mov ax, 1
    call q_add
    call tcp_arm
    cmp byte [bx+SKO_TS], TS_CLOSEW
    je  .last
    mov byte [bx+SKO_TS], TS_FINW1
    jmp short .out
.last:
    mov byte [bx+SKO_TS], TS_LASTACK
.out:
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_arm / tcp_disarm - the retransmit timer for socket BX
; -----------------------------------------------------------------------------
tcp_arm:
    push ax
    push cx
    call OSAPI_GET_TICKS
    mov cl, [bx+SKO_TRY]
    cmp cl, TCP_RTMAX
    jbe .sh
    mov cl, TCP_RTMAX
.sh:
    push ax
    mov ax, TCP_RTO0
    shl ax, cl
    mov cx, ax
    pop ax
    add ax, cx
    mov [bx+SKO_RTO], ax
    pop cx
    pop ax
    ret

tcp_disarm:
    mov word [bx+SKO_RTO], 0
    mov byte [bx+SKO_TRY], 0
    ret

; -----------------------------------------------------------------------------
; tcp_abort - the connection is over and the application is told
; in:  BX = the socket, AL = the NSK_* to leave it in
; -----------------------------------------------------------------------------
tcp_abort:
    mov byte [bx+SKO_TS], TS_CLOSED
    mov word [bx+SKO_RTO], 0
    mov word [bx+SKO_TXN], 0
    mov word [bx+SKO_TXS], 0
    test byte [bx+SKO_FL], SKF_ORPH
    jnz .free
    test byte [bx+SKO_FL], SKF_ACPT     ; nobody ever asked for this one, so
    jnz .free                           ; there is nobody to tell
    mov [bx+SKO_ST], al
    ret
.free:
    mov byte [bx+SKO_ST], NSK_FREE
    ret

; =============================================================================
; CONNECTING
; =============================================================================

; -----------------------------------------------------------------------------
; tcp_lport - a local port nobody else here is using
; out: AX
; -----------------------------------------------------------------------------
tcp_lport:
    push bx
    push cx
    push dx
.pick:
    inc word [eth_lport]
    mov ax, [eth_lport]
    cmp ax, 61000
    jb  .range
    mov ax, 49152
    mov [eth_lport], ax
.range:
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    push ax
    mov al, dl
    call sk_ptr
    pop ax
    cmp byte [bx+SKO_ST], NSK_FREE
    je  .next
    cmp ax, [bx+SKO_LP]
    je  .pick
.next:
    inc dl
    loop .l
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tcp_syn - socket BX has an address and a port: start connecting
; -----------------------------------------------------------------------------
tcp_syn:
    push ax
    push cx
    push dx
    push si
    push di
    call OSAPI_GET_TICKS                ; the initial sequence number. Not a
    mov dx, ax                          ; security measure on a machine with no
    call OSAPI_RAND                     ; clock worth the name - it is here so
    mov di, bx                          ; that a connection reopened on the same
    add di, SKO_SUNA                    ; port pair does not inherit the old
    call q_put                          ; one's numbers
    mov si, bx
    add si, SKO_SUNA
    call q_get
    mov di, bx
    add di, SKO_SNXT
    call q_put
    mov ax, 1
    call q_add
    mov byte [bx+SKO_TS], TS_SYNSENT
    mov byte [bx+SKO_ST], NSK_CONNECT
    or  byte [bx+SKO_FL], SKF_SYNA
    mov byte [bx+SKO_TRY], 0
    call OSAPI_GET_TICKS
    add ax, TCP_CONNTMO
    mov [bx+SKO_TMO], ax
    mov al, TF_SYN
    xor cx, cx
    call tcp_out                        ; a CF here is ARP: the timer retries
    call tcp_arm
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; THE RECEIVE PATH
; =============================================================================

; -----------------------------------------------------------------------------
; tcp_match - which socket does the frame in eth_rxb belong to?
; out: CF=0 and BX; CF=1 = none
; -----------------------------------------------------------------------------
tcp_match:
    push ax
    push cx
    push dx
    push si
    push di
    mov si, eth_rxb + IP_PAY + TCO_SP
    call be_get
    mov [eth_rport], ax
    mov si, eth_rxb + IP_PAY + TCO_DP
    call be_get
    mov [eth_lportr], ax
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE
    je  .next
    cmp byte [bx+SKO_TS], TS_LISTEN
    je  .next
    cmp byte [bx+SKO_TS], TS_CLOSED     ; a socket still waiting on DNS has a
    je  .next                           ; port pair and no connection
    mov ax, [eth_lportr]
    cmp ax, [bx+SKO_LP]
    jne .next
    mov ax, [eth_rport]
    cmp ax, [bx+SKO_RP]
    jne .next
    mov si, eth_rxb + IP_OFF + IPO_SRC
    mov di, bx
    add di, SKO_RIP
    call ip_eq
    je  .hit
.next:
    inc dl
    loop .l
    stc
    jmp short .out
.hit:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_listener - a LISTEN socket on the frame's destination port
; out: CF=0 and BX; CF=1 = none
; -----------------------------------------------------------------------------
tcp_listener:
    push ax
    push cx
    push dx
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_TS], TS_LISTEN
    jne .next
    mov ax, [eth_lportr]
    cmp ax, [bx+SKO_LP]
    je  .hit
.next:
    inc dl
    loop .l
    stc
    jmp short .out
.hit:
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_in - a TCP segment is in eth_rxb, CX bytes of IP payload
; -----------------------------------------------------------------------------
tcp_in:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp cx, TC_HLEN
    jb  .out
    mov [eth_tlen], cx

    mov al, [eth_rxb+IP_PAY+TCO_OFF]    ; the data offset, in 32-bit words
    mov cl, 4
    shr al, cl
    xor ah, ah
    shl ax, 1
    shl ax, 1
    cmp ax, TC_HLEN
    jb  .out
    cmp ax, [eth_tlen]
    ja  .out
    mov [eth_thl], ax
    mov cx, [eth_tlen]
    sub cx, ax
    mov [eth_seglen], cx                ; the DATA length, options removed

    call tcp_match
    jnc .have
    call tcp_listener
    jc  .rst
    test byte [eth_rxb+IP_PAY+TCO_FLG], TF_SYN
    jz  .rst
    call tcp_accept_in
    jmp short .out
.rst:
    call tcp_rst
    jmp short .out
.have:
    call tcp_seg
.out:
    pop di
    pop si
    pop dx                      ; **DX**, and it was missing: the `ret` below
    pop cx                      ; then took the saved DX as its return address
    pop bx                      ; and the machine ran off into this driver's
    pop ax                      ; own data with everything else still working
    ret

; -----------------------------------------------------------------------------
; tcp_accept_in - a SYN arrived for the listener in BX
;
; The LISTENER never becomes the connection: it stays listening and a fresh
; slot takes the peer, which is NETV_ACCEPT's whole contract (netpkg.inc) and
; what lets a server answer a second request while it is serving the first.
; -----------------------------------------------------------------------------
tcp_accept_in:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [bx+SKO_LP]                 ; the port to keep answering on
    call sk_alloc
    jc  .out                            ; no room: drop it, and the peer's own
                                        ; SYN retransmission is the retry
    mov [bx+SKO_LP], dx
    mov ax, [eth_rport]
    mov [bx+SKO_RP], ax
    mov si, eth_rxb + IP_OFF + IPO_SRC
    mov di, bx
    add di, SKO_RIP
    mov cx, 4
    call mem_copy
    mov si, eth_rxb + IP_PAY + TCO_SEQ  ; their sequence, plus one for the SYN
    call be32_get
    mov di, bx
    add di, SKO_RNXT
    call q_put
    mov ax, 1
    call q_add
    call OSAPI_RAND                     ; ...and ours
    mov dx, ax
    call OSAPI_RAND
    mov di, bx
    add di, SKO_SUNA
    call q_put
    mov si, bx
    add si, SKO_SUNA
    call q_get
    mov di, bx
    add di, SKO_SNXT
    call q_put
    mov ax, 1
    call q_add
    mov byte [bx+SKO_TS], TS_SYNRCVD
    mov byte [bx+SKO_ST], NSK_CONNECT
    or  byte [bx+SKO_FL], SKF_SYNA | SKF_ACPT
    mov byte [bx+SKO_TRY], 0
    call OSAPI_GET_TICKS                ; the connect deadline, which sk_alloc
    add ax, TCP_CONNTMO                 ; has just ZEROED - and a deadline of
    mov [bx+SKO_TMO], ax                ; zero is one tcp_timers' .ctmo has
                                        ; already passed, so the slot is
                                        ; aborted on the very first pump
    mov al, TF_SYN | TF_ACK
    xor cx, cx
    call tcp_out
    call tcp_arm
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_seg - the segment in eth_rxb belongs to socket BX
; -----------------------------------------------------------------------------
tcp_seg:
    push ax
    push cx
    push dx
    push si
    push di
    mov al, [eth_rxb+IP_PAY+TCO_FLG]
    mov [eth_flg], al

    test al, TF_RST
    jz  .nrst
    cmp byte [bx+SKO_TS], TS_SYNSENT    ; a reset while connecting is the peer
    je  .refused                        ; saying nothing is listening, which is
    mov al, NSK_CLOSED                  ; a different thing to tell the caller
    call tcp_abort                      ; than a connection that broke
    jmp .out
.refused:
    mov al, NSK_FAILED
    call tcp_abort
    mov byte [eth_lasterr], NETE_REFUSED
    jmp .out
.nrst:
    cmp byte [bx+SKO_TS], TS_SYNSENT
    je  .synsent

    ; --- is it the segment we are expecting? --------------------------------
    ; Anything else is dropped and answered with a duplicate ACK, which is what
    ; asks the peer to send the piece we are missing (see the header).
    mov si, eth_rxb + IP_PAY + TCO_SEQ
    call be32_get
    mov di, eth_ph2
    call q_put
    mov si, eth_ph2
    mov di, bx
    add di, SKO_RNXT
    call q_sub                          ; DX:AX = theirs - ours
    or  ax, dx
    jz  .inorder
    call tcp_ack
    jmp .out
.inorder:
    mov byte [eth_adv], 0               ; **DID WE CONSUME ANYTHING?** A stack
                                        ; that answers every segment with an
                                        ; ACK answers the peer's own ACKs, and
                                        ; on a stop-and-wait connection that is
                                        ; a frame per frame for nothing
    call tcp_ackin                      ; what have they acknowledged?

    cmp byte [bx+SKO_TS], TS_SYNRCVD    ; **AND A PASSIVE OPEN IS NOW UP.** The
    jne .est                            ; peer's final ACK is the only thing
    test byte [bx+SKO_FL], SKF_SYNA     ; that promotes a slot tcp_accept_in
    jnz .est                            ; made, and tcp_ackin has just cleared
    mov byte [bx+SKO_TS], TS_ESTAB      ; SKF_SYNA if it covered our SYN. SKF_
    mov byte [bx+SKO_ST], NSK_UP        ; ACPT stays on: eth_v_accept is what
    call tcp_disarm                     ; hands the handle out
.est:
    mov cx, [eth_seglen]                ; ...and the data
    jcxz .nodata
    mov si, eth_rxb + IP_PAY
    add si, [eth_thl]
    call sk_rxput                       ; CX = what fitted
    jcxz .acknow                        ; nothing did: the window is shut and
                                        ; the peer must come back - and NOTHING
                                        ; may be consumed here, their FIN least
                                        ; of all
    mov di, bx
    add di, SKO_RNXT
    mov ax, cx
    call q_add
    mov byte [eth_adv], 1
    cmp cx, [eth_seglen]
    jne .acknow                         ; short of what they sent: no FIN of
                                        ; theirs can be consumed yet
.nodata:
    test byte [eth_flg], TF_FIN
    jz  .acknow
    mov di, bx
    add di, SKO_RNXT
    mov ax, 1
    call q_add
    mov byte [eth_adv], 1
    cmp byte [bx+SKO_TS], TS_ESTAB
    je  .cw
    cmp byte [bx+SKO_TS], TS_FINW1
    je  .clsing
    cmp byte [bx+SKO_TS], TS_FINW2
    je  .tw
    jmp short .acknow
.cw:
    mov byte [bx+SKO_TS], TS_CLOSEW
    mov byte [bx+SKO_ST], NSK_CLOSING   ; **NOT an end of stream**: netpkg.inc
    jmp short .acknow                   ; says the drain to CX = 0 is
.clsing:                                ; what ends it
    mov byte [bx+SKO_TS], TS_CLOSING
    jmp short .acknow
.tw:
    call tcp_timewait
.acknow:
    cmp byte [eth_adv], 0
    jne .doack
    cmp word [eth_seglen], 0            ; a segment we could not take is still
    je  .nack                           ; a segment: say where we are
.doack:
    call tcp_ack
.nack:
    call tcp_kick
    jmp short .out

.synsent:
    test byte [eth_flg], TF_ACK
    jz  .out
    test byte [eth_flg], TF_SYN
    jz  .out
    call tcp_ackin
    test byte [bx+SKO_FL], SKF_SYNA     ; ...the ack has to have covered it
    jnz .out
    mov si, eth_rxb + IP_PAY + TCO_SEQ
    call be32_get
    mov di, bx
    add di, SKO_RNXT
    call q_put
    mov ax, 1
    call q_add
    mov byte [bx+SKO_TS], TS_ESTAB
    mov byte [bx+SKO_ST], NSK_UP
    call tcp_disarm
    call tcp_ack
    call tcp_kick
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_ackin - apply the acknowledgement in the segment to socket BX
;
; What is acknowledged is retired in ONE place, and it has three things in it
; because TCP counts three things in the same sequence space: a SYN, the data,
; and a FIN. Miss one and the connection hangs at exactly the point that thing
; was outstanding, which is why they are together.
; -----------------------------------------------------------------------------
tcp_ackin:
    push ax
    push cx
    push dx
    push si
    push di
    test byte [eth_flg], TF_ACK
    jz  .out
    mov si, eth_rxb + IP_PAY + TCO_ACK
    call be32_get
    mov di, eth_ph2
    call q_put
    mov si, eth_ph2                     ; ahead of what we SENT is no ack at
    mov di, bx                          ; all: SND.UNA may never pass SND.NXT,
    add di, SKO_SNXT                    ; and a peer that moved it there would
    call q_sub                          ; have every later segment sent from a
    or  dx, dx                          ; sequence the peer discards, with the
    js  .inwin                          ; retransmit timer just disarmed
    or  ax, ax
    jnz .out
    or  dx, dx
    jnz .out
.inwin:
    mov si, eth_ph2                     ; theirs - ours: zero or less is old
    mov di, bx
    add di, SKO_SUNA
    call q_sub
    or  dx, dx
    js  .out
    or  ax, ax
    jnz .new
    or  dx, dx
    jz  .out
.new:
    mov cx, ax                          ; how many sequence numbers retired.
    or  dx, dx                          ; A jump of more than 64KB cannot
    jz  .amt                            ; happen with one segment in flight,
    mov cx, 0xFFFF                      ; so this only guards a malicious peer
.amt:
    test byte [bx+SKO_FL], SKF_SYNA     ; the SYN, if it was still out
    jz  .nosyn
    and byte [bx+SKO_FL], ~SKF_SYNA
    dec cx
.nosyn:
    mov ax, [bx+SKO_TXS]                ; the data
    cmp cx, ax
    jae .alldata
    mov ax, cx
.alldata:
    or  ax, ax
    jz  .nodata
    sub cx, ax
    sub [bx+SKO_TXS], ax
    sub [bx+SKO_TXN], ax
    push cx                             ; ...and what is left slides down
    push ax
    call sk_txbase
    mov si, di
    add si, ax
    mov cx, [bx+SKO_TXN]
    call mem_copy                       ; forward, and dest < src, so an
    pop ax                              ; overlapping move is safe
    pop cx
.nodata:
    test byte [bx+SKO_FL], SKF_FINS     ; and the FIN
    jz  .nofin
    or  cx, cx
    jz  .nofin
    call tcp_finacked
.nofin:
    mov si, eth_ph2                     ; SND_UNA moves to what they acked
    mov di, bx
    add di, SKO_SUNA
    mov cx, 4
    call mem_copy
    call tcp_disarm
    cmp word [bx+SKO_TXS], 0
    jne .out
    call tcp_kick
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_finacked - our FIN has been acknowledged
; -----------------------------------------------------------------------------
tcp_finacked:
    push ax
    and byte [bx+SKO_FL], ~SKF_FINS
    cmp byte [bx+SKO_TS], TS_FINW1
    je  .fw2
    cmp byte [bx+SKO_TS], TS_CLOSING
    je  .tw
    cmp byte [bx+SKO_TS], TS_LASTACK
    je  .done
    jmp short .out
.fw2:
    mov byte [bx+SKO_TS], TS_FINW2
    jmp short .out
.tw:
    call tcp_timewait
    jmp short .out
.done:
    mov al, NSK_CLOSED
    call tcp_abort
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_timewait - both FINs are done: hold the slot briefly (see TCP_TWTMO)
; -----------------------------------------------------------------------------
tcp_timewait:
    push ax
    mov byte [bx+SKO_TS], TS_TIMEWT
    call OSAPI_GET_TICKS
    add ax, TCP_TWTMO
    mov [bx+SKO_TMO], ax
    mov word [bx+SKO_RTO], 0
    pop ax
    ret

; =============================================================================
; THE TIMERS - the only thing here that runs without a packet
; =============================================================================
tcp_timers:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call OSAPI_GET_TICKS
    mov [eth_now], ax
    xor dl, dl
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_TS], TS_CLOSED
    je  .next
    cmp byte [bx+SKO_TS], TS_LISTEN
    je  .next

    cmp byte [bx+SKO_TS], TS_TIMEWT     ; the short hold, then the slot goes
    jne .conn
    mov ax, [eth_now]
    sub ax, [bx+SKO_TMO]
    js  .next
    mov al, NSK_CLOSED
    call tcp_abort
    jmp short .next
.conn:
    cmp byte [bx+SKO_TS], TS_SYNSENT    ; the overall connect deadline
    je  .ctmo
    cmp byte [bx+SKO_TS], TS_SYNRCVD
    jne .rtx
.ctmo:
    mov ax, [eth_now]
    sub ax, [bx+SKO_TMO]
    js  .rtx
    mov al, NSK_FAILED
    call tcp_abort
    jmp short .next
.rtx:
    cmp word [bx+SKO_RTO], 0
    je  .idle
    mov ax, [eth_now]
    sub ax, [bx+SKO_RTO]
    js  .next
    inc byte [bx+SKO_TRY]
    cmp byte [bx+SKO_TRY], TCP_RTMAX + 1
    jbe .again
    mov al, NSK_FAILED                  ; it stopped answering altogether
    call tcp_abort
    jmp short .next
.again:
    call tcp_retx
    jmp short .next
.idle:
    call tcp_kick                       ; nothing in flight and something to
.next:                                  ; send: usually the ARP that was not
    inc dl                              ; ready the first time
    loop .l
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tcp_retx - send the front of socket BX's window again
; -----------------------------------------------------------------------------
tcp_retx:
    push ax
    push cx
    test byte [bx+SKO_FL], SKF_SYNA
    jz  .data
    mov al, TF_SYN
    cmp byte [bx+SKO_TS], TS_SYNRCVD
    jne .syn
    mov al, TF_SYN | TF_ACK
.syn:
    xor cx, cx
    call tcp_out
    jmp short .arm
.data:
    mov cx, [bx+SKO_TXS]
    jcxz .fin
    mov al, TF_ACK | TF_PSH
    call tcp_out
    jmp short .arm
.fin:
    test byte [bx+SKO_FL], SKF_FINS
    jz  .arm
    mov al, TF_ACK | TF_FIN
    xor cx, cx
    call tcp_out
.arm:
    call tcp_arm
    inc word [eth_nretx]
    pop cx
    pop ax
    ret
