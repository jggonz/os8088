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

; --- THE VERB'S OWN PUMP ------------------------------------------------------
; Every verb below starts by draining the ring, because the only thing that can
; advance the stack is a package asking the driver something.
;
; A driver-side WORKER doing it instead was built, measured on the 5150 and
; REMOVED - SPEC.md 72.19 is the record and docs/FTP-PERF.md 6.1 the numbers.

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
    xor ax, ax
    cmp [sk_seg], ax            ; NO RING = NO ROOM, and not "took them all":
    je  .cap                    ; ring_in refuses on the same word and returns
                                ; CX untouched, so crediting SKO_TXN here would
                                ; build the segment over the last frame's bytes
    mov ax, [sk_txcap]
    sub ax, [bx+SKO_TXN]
.cap:
    cmp cx, ax
    jbe .room
    mov cx, ax
.room:
    jcxz .none
    call sk_txbase
    add di, [bx+SKO_TXN]
    call ring_in                ; ...INTO THE CLAIM, not the driver's bss:
                                ; usr_read wrote through DS (SPEC.md 72.13)
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
    push ax                             ; waiting on an advertisement nothing
    mov ax, [sk_rxcap]
    shr ax, 1
    cmp dx, ax
    pop ax
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
    cmp byte [bx+SKO_TS], TS_LISTEN ; a LISTENER takes its un-accepted children
    jne .mine                       ; with it - see sk_reap_kids, and the FTP
    call sk_reap_kids               ; server it was found by (SPEC.md 77)
.mine:
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

; --- NETV_ABORT --------------------------------------------------------------
; AL = a handle: the connection is dropped and THE SLOT COMES BACK NOW
;
; eth_v_close hands an ESTABLISHED socket to TCP and keeps the slot until the
; FIN handshake finishes, which is right when the peer is still there and
; ruinous when it is not: the retransmit timer is TCP_RTMAX doublings, about
; a minute, and NET_SOCKS is four. The FTP server hit exactly that in the
; field - a client that timed out mid-upload left its data socket finishing a
; handshake with nobody, and the next session's LIST had no slot for
; NETV_ACCEPT to make a data connection from, so it sat at its 150 until that
; client timed out too (SPEC.md 77.25).
;
; The state teardown is eth_v_close's, minus the FIN: SKF_ORPH marks the slot
; as nobody's, sk_reap_kids takes a listener's pending queue with it, and
; tcp_abort does the rest and frees.
eth_v_abort:
    push ax
    push bx
    call eth_pump
    call sk_h2p
    jc  .no                             ; already gone: not an error, exactly
    cmp byte [bx+SKO_TS], TS_LISTEN     ; as eth_v_close is not
    jne .mine
    call sk_reap_kids
.mine:
    and byte [bx+SKO_FL], ~SKF_RES      ; a name still being looked up dies
    mov ah, [bx+SKO_IDX]                ; with the slot, and [dns_sk] lives
    cmp ah, [dns_sk]                    ; OUTSIDE the record, so sk_alloc's
    jne .nores                          ; mem_zero cannot reach it and the
    mov byte [dns_sk], 0xFF             ; reply would land on whoever the slot
    mov word [dns_lport], 0             ; went to next
.nores:
    or  byte [bx+SKO_FL], SKF_ORPH      ; ...which is what sends tcp_abort to
    mov al, NSK_FREE                    ; its .free arm
    call tcp_abort
.no:
    pop bx
    pop ax
    clc
    ret

; --- NETV_ADDR ---------------------------------------------------------------
; out ES:DI (the CALLER's) = four bytes of IPv4; CF=1 / NETE_NOLINK if none yet
;
; **SHARED RATHER THAN HOSTED, unlike NETV_IDENT and NETV_STATE.** This file's
; header keeps those two with their ends because what identifies a driver and
; what counts as a link are the one thing the cable and the card genuinely
; disagree about. An ADDRESS is not: both ends answer with the IP of the
; machine the stack is running on, and over the cable that machine is the DOS
; box - which is the true answer there, because an inbound connection arrives
; there and is forwarded (docs/NET-STACK-PLAN.md 1.5). One body, so the two
; transports cannot come to differ about it.
;
; **The zero test is not a formality.** `eth_ip` is four zeroes until DHCP
; binds or Setup is filled in, and 0.0.0.0 in an FTP 227 reply tells the
; client to connect to ITSELF - which fails somewhere it cannot explain. The
; refusal is what lets a server say "no address yet" instead.
eth_v_addr:
    push cx
    push si
    push di
    mov si, eth_ip
    call ip_zero
    jz  .none
    mov cx, 4
    call usr_write              ; SI = ours, DI = theirs, through [eth_useg] -
                                ; which is the caller's ES in the driver and
                                ; the program's own DS on the DOS side
                                ; (ethusr.inc)
    xor ax, ax
    pop di
    pop si
    pop cx
    clc
    ret
.none:
    mov ax, NETE_NOLINK
    pop di
    pop si
    pop cx
    stc
    ret

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

