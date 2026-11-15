; =============================================================================
; os8088 - drivers/ether/ethsock.inc
;
; THE SOCKET VERBS, and they belong to NEITHER END in particular.
;
; NETV_OPEN..NETV_ACCEPT over tcp.inc, cut out of ether.asm whole so that the
; DOS side of the parallel cable can serve the same verbs with the same code
; (drivers/net/os88net.asm, SPEC.md 62.11.1). Two implementations of a socket
; API is exactly the shape docs/plans/completed/NET-PLAN.md warned about at the replayer -
; a bug fixed in one is not fixed in the other - and this file is the answer.
;
; It needs, from whichever program includes it: the state in ethstate.inc,
; tcp.inc, inet.inc, dns.inc, and a link layer answering nic_tx/nic_rx.
; NETV_IDENT and NETV_STATE stay with their HOSTS, because what identifies a
; driver and what counts as a link are the one thing the two ends genuinely
; disagree about.
; =============================================================================

; --- THE VERB'S OWN PUMP ------------------------------------------------------
; Every verb below starts by draining the ring, because the only thing that can
; advance the stack is a package asking the driver something.
;
; A driver-side WORKER doing it instead was built, measured on the 5150 and
; REMOVED - SPEC.md 72.19 is the record and docs/plans/completed/FTP-PERF.md 6.1 the numbers.

; --- NETV_OPEN ---------------------------------------------------------------
; in ES:SI = a NUL host, CX = the port; out AX = a handle
eth_v_open:
    push bx                     ; **SI IS HELD, NOT RE-POPPED.** The obvious
    push cx                     ; form - pop the pushed SI back when the name
    push dx                     ; is wanted - reads the wrong slot the moment
    push si                     ; anything is pushed after it, and there is
    push di                     ; something: DI. It was banked in a word
    call eth_pump               ; (eth_usi) until driver size pass 1; it is
    mov ax, [eth_ip]            ; simply never written: eth_pump, sk_alloc
    or  ax, [eth_ip+2]          ; and tcp_lport all keep it, and ip_zero's
    jz  .nolink                 ; question is asked in AX instead of SI
    mov dx, cx                  ; the port, across sk_alloc
    call sk_alloc
    jc  .full
    mov [eth_hnd], al           ; the handle, likewise
    mov [bx+SKO_RP], dx
    call tcp_lport
    mov [bx+SKO_LP], ax
    mov byte [bx+SKO_ST], NSK_CONNECT

    lea di, [bx+SKO_HOST]       ; the name, out of their segment and into the
                                ; record - bounded by the record and never by
    mov cx, NET_HOSTMAX - 1     ; the NUL, which came off a package
    call usr_read               ; SI = their offset, as they passed it
    mov byte [bx+SKO_HOST+NET_HOSTMAX-1], 0

    lea si, [bx+SKO_HOST]
    lea di, [bx+SKO_RIP]
    call ip_parse               ; a dotted quad connects at once; a NAME waits
    jnc .go                     ; for dns_timer to pick it up...
    call dns_cached             ; ...unless it is the name asked last time
    jnc .go                     ; (SPEC.md 72.5.1): then RIP is filled
    or  byte [bx+SKO_FL], SKF_RES
    call OSAPI_GET_TICKS
    add ax, TCP_CONNTMO
    mov [bx+SKO_TMO], ax
    jmp short .ok
.go:
    call tcp_syn
.ok:
    mov al, [eth_hnd]
    xor ah, ah                  ; (and CF=0: `xor` clears it)
    jmp short .pop
.nolink:
    mov ax, NETE_NOLINK
    jmp short .no
.full:
    mov ax, NETE_FULL
.no:
    stc
.pop:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
.no:                            ; (CF = 1: sk_h2p's)
    pop dx
    pop bx
    ret

; --- NETV_SEND ---------------------------------------------------------------
; in AL = handle, ES:SI = bytes, CX = how many; out CX = taken
eth_v_send:
    push ax
    push bx
    push dx
    push si
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
    mov ax, [bx+SKO_TXC]        ; **THIS SOCKET'S RING** (SPEC.md 72.21), which
    sub ax, [bx+SKO_TXN]        ; is a lean one unless it asked for the bulk
.cap:
    cmp cx, ax
    jbe .room
    mov cx, ax
.room:
    jcxz .none
    push cx                     ; the whole count, whichever arm runs
    mov ax, [bx+SKO_TXN]        ; the byte after the last one queued, in a
    call sk_txpos               ; CIRCULAR ring (SPEC.md 72.4.3): DI = it,
    cmp cx, ax                  ; AX = the run to the ring's end
    jbe .tail
    xchg cx, ax                 ; the run to the end first...
    call ring_in
    add si, cx
    sub ax, cx
    mov cx, ax                  ; ...and the rest at the ring's start
    call sk_txbase
.tail:
    call ring_in                ; ...INTO THE CLAIM, not the driver's bss:
                                ; usr_read wrote through DS (SPEC.md 72.13)
    pop cx
    add [bx+SKO_TXN], cx
    call tcp_kick               ; (keeps CX - it banks its own)
.none:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
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
                                        ; waiting on an advertisement nothing
                                        ; else in this driver would ever send,
                                        ; and the transfer stops for good.
    ; -----------------------------------------------------------------------
    ; **ONCE PER DRAIN, ON THE CROSSING** (SPEC.md 72.4). This fired whenever
    ; the ring had been at or above half full, which is every pass of a loop
    ; that empties it - and fd_recv_stage drains a whole chunk in one
    ; (SPEC.md 77.30), so a full ring sent an update per NET_SKMAX taken.
    ; Measured on the wire that was 253 frames out of the server against 109
    ; data frames into it for a 153KB upload: 2.32 to 1, where one to one is
    ; the floor. A transmitted frame costs the target ~10 ms whatever it
    ; carries (72.16), so the redundant two thirds were ~1.4 s per 153KB.
    ;
    ; The crossing is the whole test: WAS at or above half, IS now below.
    ; One update is all the peer needs to start sending again, and every
    ; segment it then sends is acknowledged with the space that opened after
    ; it - so nothing is lost by not repeating ourselves.
    ; -----------------------------------------------------------------------
    mov ax, [bx+SKO_RXC]        ; ...and half of ITS ring, not the driver's
    shr ax, 1                   ; (AX is the handle the pops below restore,
    cmp dx, ax                  ; so it is free here) was it at or above half...
    jb  .out
    cmp [bx+SKO_RXN], ax                ; ...and is it below half now?
    jae .out
    call tcp_ack                        ; (which keeps CX, the answer)
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

; --- NETV_BULK ---------------------------------------------------------------
; AL = a handle: it is going to move a file, so give it the big ring if it is
; there (SPEC.md 72.21.1).
eth_v_bulk:
    push bx
    call sk_h2p
    jc  .no
    mov byte [bx+SKO_WANTB], 1          ; recorded either way - a LISTENER holds
    cmp byte [bx+SKO_TS], TS_LISTEN     ; no ring itself and this is inherited
    je  .ok                             ; by every child it accepts
    ; --- **ONLY LET GO OF A RING WHEN THE BULK ONE IS THERE TO TAKE** -------
    ; put-then-get would be a socket with no ring at all if the get failed,
    ; which is a connection that can never receive again. The pool's own byte
    ; is the test, and it is exact.
    cmp byte [sk_rused], 0
    jne .ok                             ; bulk is out - or WE hold it already
    cmp word [bx+SKO_RXN], 0            ; ...and a swap only moves a pointer
    jne .ok                             ; while there is nothing in the ring
    cmp word [bx+SKO_TXN], 0
    jne .ok
    call sk_ring_put
    call sk_ring_get
.ok:
    pop bx
    xor ax, ax
    clc
    ret
.no:                                    ; (CF = 1: sk_h2p's)
    pop bx
    ret

; --- NETV_SKTAB --------------------------------------------------------------
; in ES:DI = a buffer, CX = its capacity; out CX = bytes written, AL=NET_SOCKS
;
; **THE TABLE, NOT A HANDLE** (SPEC.md 72.20). Every other verb here answers
; about a socket the caller owns; the sockets that cause trouble are the ones
; nobody owns. Bounded by the caller's CX and never by NET_SOCKS, so a package
; built against a smaller table still reads whole records.
eth_v_sktab:
    push bx
    push di
    push bp
    push es
    call eth_pump
    mov bp, cx                          ; the capacity, banked
    mov es, [eth_useg]
    cld
    xor cx, cx                          ; ...and CX becomes the answer
    mov bx, sk_tab                      ; BX = the record, walked
.l:
    mov ax, cx
    add ax, SKI_SZ
    cmp ax, bp
    ja  .done                           ; a partial record is worse than none
    mov si, sk_ski                      ; ...and the record's fields in SKI
.f:                                     ; order, each a WORD to ES:DI: a byte
    lodsb                               ; field's spare half is overwritten by
    cbw                                 ; the field after it, so DI steps one
    push bx                             ; for those and two for the rest, and
    add bx, ax                          ; SKI_SZ in all
    mov ax, [bx]
    pop bx
    stosw
    cmp si, sk_ski + SKI_NB
    ja  .w
    dec di
.w:
    cmp si, sk_ski_end
    jb  .f
    add cx, SKI_SZ
    sub bx, -SK_SZ                      ; tcp.inc's SK_NEXT, which is not
    cmp bx, SK_END                      ; defined yet where this is included
    jb  .l
.done:
    mov al, NET_SOCKS
    pop es
    pop bp
    pop di
    pop bx
    clc
    ret

; the record's offsets in SKI order: the SKI_NB byte fields, then the words -
; ST, TS, FL and TRY, then LP, RP, RXN, TXN and RXC (WHICH RING, as its
; size - 72.21)
SKI_NB      equ 4
sk_ski:     db SKO_ST, SKO_TS, SKO_FL, SKO_TRY
            db SKO_LP, SKO_RP, SKO_RXN, SKO_TXN, SKO_RXC
sk_ski_end:
%if SKI_ST != 0 || SKI_TS != 1 || SKI_FL != 2 || SKI_TRY != 3 || SKI_LP != 4 || SKI_RP != 6 || SKI_RXN != 8 || SKI_TXN != 10 || SKI_RXC != 12 || SKI_SZ != 14
  %error "sk_ski writes the SKI record as four bytes and five words, in order"
%endif

; --- NETV_CLOSE --------------------------------------------------------------
; in AL = a handle. The handle is theirs no more, whatever the far side says.
eth_v_close:
    push ax
    push bx
    call sk_shut
    jc  .no
    or  byte [bx+SKO_FL], SKF_ORPH
    cmp byte [bx+SKO_TS], TS_ESTAB
    je  .fin
    cmp byte [bx+SKO_TS], TS_CLOSEW
    je  .fin
    mov byte [bx+SKO_TS], TS_CLOSED     ; nothing was ever connected, or it is
    mov byte [bx+SKO_ST], NSK_FREE      ; already over: the slot goes now
    call sk_ring_put                    ; ...and the ring with it (72.21)
    jmp short .no
.fin:
    or  byte [bx+SKO_FL], SKF_FIN
    mov byte [bx+SKO_ST], NSK_CLOSED    ; ...but TCP keeps the slot until the
    call tcp_kick                       ; handshake is done (tcp_abort)
.no:
    pop bx
    pop ax
    clc                                 ; a close of a handle that is already
    ret                                 ; gone is not an error

; --- sk_shut - what NETV_CLOSE and NETV_ABORT share -------------------------
; in:  AL = a handle. out: CF=1 = no such socket; else BX = its record, a
;      LISTENER's un-accepted children gone with it (sk_reap_kids, and the
;      FTP server it was found by - SPEC.md 77), and a NAME still being
;      looked up cancelled with the slot (dns_unres). An ESTABLISHED socket
;      is never mid-lookup, so for eth_v_close's FIN path that last is a no-op
; clobbers: AH
sk_shut:
    call eth_pump
    call sk_h2p
    jc  .out
    cmp byte [bx+SKO_TS], TS_LISTEN
    jne .mine
    call sk_reap_kids
.mine:
    call dns_unres
    clc
.out:
    ret

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
    call sk_shut
    jc  .no                             ; already gone: not an error, exactly
                                        ; as eth_v_close is not
    or  byte [bx+SKO_FL], SKF_ORPH      ; ...which is what sends tcp_abort to
    mov al, NSK_FREE                    ; its .free arm
    call tcp_abort
.no:
    jmp short eth_v_close.no            ; ...and eth_v_close's own tail

; --- NETV_ADDR ---------------------------------------------------------------
; out ES:DI (the CALLER's) = four bytes of IPv4; CF=1 / NETE_NOLINK if none yet
;
; **SHARED RATHER THAN HOSTED, unlike NETV_IDENT and NETV_STATE.** This file's
; header keeps those two with their ends because what identifies a driver and
; what counts as a link are the one thing the cable and the card genuinely
; disagree about. An ADDRESS is not: both ends answer with the IP of the
; machine the stack is running on, and over the cable that machine is the DOS
; machine - which is the true answer there, because an inbound connection arrives
; there and is forwarded (docs/plans/completed/NET-STACK-PLAN.md 1.5). One body, so the two
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
    xor ax, ax                  ; (and CF=0: `xor` clears it)
    jmp short .pop
.none:
    mov ax, NETE_NOLINK
    stc
.pop:
    pop di
    pop si
    pop cx
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
    xor ah, ah                  ; (and CF=0: `xor` clears it)
    jmp short .pop
.full:
    mov ax, NETE_FULL
    stc
.pop:
    pop cx
    pop bx
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
    xor ah, ah                  ; (and CF=0: `xor` clears it)
    jmp short .pop
.next:
    inc dl
    loop .l
    xor ax, ax                  ; nobody yet, which is the ORDINARY answer -
    pop si                      ; a listener polls for it every tick, so it
    pop dx                      ; keeps its own pops rather than a jump to
    pop cx                      ; the shared ones
    pop bx
    ret
.no:
    mov ax, NETE_HANDLE
    stc
.pop:
    pop si
    pop dx
    pop cx
    pop bx
    ret


; =============================================================================
; THE RAW VERBS - whole frames, for a consumer that IS a stack (SPEC.md 72.22)
; =============================================================================
; netpkg.inc carries the contract and the reasoning. These three are short
; because every hard part of them is already written: nic_tx and nic_rx are the
; link layer the socket half sits on, and usr_read/usr_write are the caller's
; segment. What is new is only WHO the frames belong to.
;
; **ETH_NORAW, BECAUSE THIS FILE HAS TWO HOSTS** (SPEC.md 72.22.5). ether.asm
; is one and drivers/net/os88net.asm is the other - the DOS end of the cable
; runs the SAME stack (SPEC.md 62.11.1), and it reaches these verbs through no
; table at all: eth_vtab lives in ether.asm. So on that host they are 215
; bytes of a shipped .COM that nothing can call. The gate is ETH_NOEMIT's
; shape and sits beside it in the same includer, for the same reason.
%ifndef ETH_NORAW

; --- NETV_RAW ----------------------------------------------------------------
; in AL = 1 claim / 0 release.
;
; **THE CLAIM STOPS eth_pump, WHICH IS WHY IT IS A CLAIM AND NOT A FLAG.**
; A second consumer would not corrupt anything - nic_rx is one frame at a time
; and the card mutex is above us - it would simply make frames vanish, one
; stack getting the ones the other was waiting for. That is the failure this
; project keeps writing down as the expensive kind: nothing reports an error
; and the symptom is a protocol that stalls.
eth_v_raw:
    or  al, al
    je  .release
    cmp byte [eth_raw], 0
    jne .held
    mov byte [eth_raw], 1
    push cx
    push si
    xor cx, cx                  ; the counters are per CLAIM, so a probe can
    mov [eth_nraw], cx          ; read them without knowing what ran before it
    mov [eth_nrawtx], cx
    or  di, di                  ; ...AND THE STATION ADDRESS, because a
    jz  .nomac                  ; consumer that is the stack now cannot build
    mov si, eth_mac             ; one frame without it (netpkg.inc)
    mov cx, 6
    call usr_write              ; SI ours, DI theirs
.nomac:
    pop si
    pop cx
    clc
    ret
.release:
    mov byte [eth_raw], 0       ; **A RELEASE ALWAYS SUCCEEDS** (netpkg.inc):
    clc                         ; DOS calls it on every exit path it
    ret                         ; has, including the ones that never claimed
.held:
    mov ax, NETE_BUSY
    stc
    ret

; --- NETV_RAWTX --------------------------------------------------------------
; in SI = the frame, CX = its length, DX = the segment it is in (0 = the
;    caller's own).
eth_v_rawtx:
    push bx
    push cx
    push si
    push di
    push word [eth_useg]            ; **DX NAMES THE SEGMENT** (SPEC.md
    call eth_rawseg                 ; 72.22.4): a consumer moving 1.5KB a frame
    cmp byte [eth_raw], 0           ; should not have to keep the buffer in its
    je  .noclaim                    ; own image, and OSAPI_DRV_CALL can only
                                    ; put the CALLER's segment in [eth_useg]
    cmp cx, EH_SIZE             ; shorter than an Ethernet header is not a
    jb  .bad                    ; short frame, it is a wrong one - nic_tx pads
    cmp cx, NIC_FRAME            ; to the 60-byte minimum itself
    ja  .bad                    ; ...and too long is REFUSED, never truncated:
                                ; a silently shortened frame is a checksum
                                ; failure at the far end and a bug hunt here
    mov di, eth_txb
    call usr_read               ; SI theirs, DI ours, CX bytes
    call nic_tx
    jc  .gone
    inc word [eth_nrawtx]       ; the raw count. [eth_ntx], the CARD's count
                                ; the Control Panel page shows, nic_tx has
                                ; already taken (ne_tx, rt_tx): a second `inc`
                                ; here counted every raw frame twice (kernel
                                ; size pass 18 - the owner's call, -4)
    clc
.pop:                           ; ONE TAIL: the frame's path runs the same
    pop word [eth_useg]         ; instructions it did, and the refusals jump
    pop di                      ; in with their CF set (driver size pass 1)
    pop si
    pop cx
    pop bx
    ret
.gone:
    mov ax, NETE_NOLINK         ; the card never finished the previous transmit
    jmp short .err
.bad:
    mov ax, NETE_ARG
    jmp short .err
.noclaim:
    mov ax, NETE_NOLINK         ; **NOT NETE_ARG**: a caller that never claimed
                                ; the wire is not making a malformed call, it
                                ; is making one there is no wire for - and the
                                ; frame it wanted to send would have gone out
                                ; under a MAC our own stack is still using
.err:
    stc
    jmp short .pop

; --- NETV_RAWRX --------------------------------------------------------------
; in DI = a buffer, CX = its capacity, DX = the segment it is in (0 = the
;    caller's own).
; out CF=0 with CX = the frame's TRUE length; CF=1 = nothing there.
eth_v_rawrx:
    push ax
    push bx
    push si
    push di
    push word [eth_useg]
    call eth_rawseg             ; DX, as above
    mov bx, cx                  ; **THE CAPACITY, BANKED BEFORE nic_rx**, which
                                ; returns the frame's length in the same
                                ; register the caller passed the capacity in
    cmp byte [eth_raw], 0
    je  .none                   ; no claim, no frames - and it reads as an
                                ; empty ring rather than an error, because a
                                ; poll loop is the only thing that calls this
    call nic_rx                  ; CF=1 = empty; CX = the length in eth_rxb
    jc  .none
    inc word [eth_nrx]
    inc word [eth_nraw]
    push cx                     ; **THE TRUE LENGTH IS THE ANSWER** and the
    cmp cx, bx                  ; COPY is what the capacity bounds. A caller
    jbe .fits                   ; told "1514" by a 64-byte buffer knows it lost
    mov cx, bx                  ; bytes; one told "64" would think that was the
.fits:                          ; frame and read a truncated header as a whole
    mov si, eth_rxb
    call usr_write              ; SI ours, DI theirs, CX bytes
    pop cx
    pop word [eth_useg]
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.none:
    pop word [eth_useg]
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; --- eth_rawseg - DX names the buffer's segment, 0 = the caller's own --------
; The two raw verbs both want it and both have banked [eth_useg] already.
eth_rawseg:
    or  dx, dx
    jz  .keep
    mov [eth_useg], dx
.keep:
    ret

; =============================================================================
; UDP FOR A PACKAGE (SPEC.md 72.24): one port, a queue of UQ_N datagrams of
; up to UQ_DATA bytes each, filled by udp_in as frames are pumped and
; drained by NETV_UDPRX. The stack already speaks UDP (DHCP, DNS): what is
; new is a package's port. A full queue drops the newest and counts it.
; =============================================================================
UQ_N        equ 8
UQ_DATA     equ 64
UQ_LEN      equ 0               ; a slot: the datagram's TRUE length, the
UQ_SRC      equ 2               ; source (IP, then port, little-endian), the
UQ_PAY      equ 8               ; payload (at most UQ_DATA of it)
UQ_SZ       equ UQ_PAY + UQ_DATA

; --- NETV_UDP: AL = 1 bind CX = a port, 0 release ----------------------------
eth_v_udp:
    or  al, al
    je  .release
    jcxz .bad
    cmp word [eth_uport], 0
    je  .take
    cmp cx, [eth_uport]         ; (the same port again: still ours)
    jne .busy
.take:
    mov [eth_uport], cx
    mov al, [eth_cslot]         ; (whose: NETV_GONE releases it)
    mov [eth_uslot], al
    mov word [eth_uqh], 0       ; the queue empty
    clc
    ret
.release:
    mov word [eth_uport], 0
    clc
    ret
.busy:
    mov ax, NETE_BUSY
    stc
    ret
.bad:
    mov ax, NETE_ARG
    stc
    ret

; --- NETV_UDPTX: SI = the payload, CX = its length, DI = 6 bytes: IP, port --
eth_v_udptx:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp word [eth_uport], 0
    je  .nolink
    cmp cx, 512
    ja  .bad
    push si
    push cx
    mov si, di                  ; the destination: 6 bytes, theirs
    mov di, eth_udst
    mov cx, 6
    call usr_read
    pop cx
    pop si
    mov di, eth_txb + IP_PAY + UD_HLEN
    call usr_read               ; the payload, in place (udp_send's rule)
    mov si, eth_udst
    mov dx, [eth_udst+4]
    mov bp, [eth_uport]
    call udp_send               ; CF=1: no ARP answer yet - again later
    jc  .nolink
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.bad:
    mov ax, NETE_ARG
    jmp short .err
.nolink:
    mov ax, NETE_NOLINK
.err:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; --- NETV_UDPRX: DI = a buffer, CX = its capacity ----------------------------
; The ring pumped first (this verb is all the service a polling game gives
; the card), then the oldest queued datagram out: its SOURCE's six bytes at
; DI (BX is the door's class and verb, so it cannot carry a pointer), its
; payload after them. CX = the payload's TRUE length
eth_v_udprx:
    push ax
    push si
    push di
    push bx                     ; **eth_pump SAVES ONLY CX AND BP**
    push dx                     ; (eth_pump_i's STKDEPTH-NOSAVE): without
    call eth_pump               ; these the caller's DI - where the datagram
    pop dx                      ; goes - was the pump's, and the copy landed
    pop bx                      ; at an offset in the package nobody chose
    pop di                      ; (WIREARENA-NET-PLAN 9.2)
    push di
    cmp word [eth_uqh], 0
    je  .none
    sub cx, 6                   ; (the room past the source)
    jae .room
    xor cx, cx
.room:
    mov ax, [eth_uq+UQ_LEN]
    push ax                     ; (the TRUE length, the answer)
    cmp ax, cx
    jae .cap
    mov cx, ax
.cap:
    cmp cx, UQ_DATA
    jbe .fit
    mov cx, UQ_DATA
.fit:
    push cx
    mov si, eth_uq + UQ_SRC
    mov cx, 6
    call usr_write              ; SI ours, DI theirs
    pop cx
    add di, 6
    mov si, eth_uq + UQ_PAY
    call usr_write
    push es                     ; the rest down a slot
    push ds
    pop es
    mov si, eth_uq + UQ_SZ
    mov di, eth_uq
    mov cx, (UQ_N - 1) * UQ_SZ
    cld
    rep movsb
    pop es
    dec word [eth_uqh]
    pop cx
    pop di
    pop si
    pop ax
    clc
    ret
.none:
    pop di
    pop si
    pop ax
    stc
    ret

; udp_queue - a datagram for the package's port (udp_in, CX = its payload's
; length, the frame in eth_rxb): into the queue's next slot, or dropped
udp_queue:
    push ax
    push cx
    push si
    push di
    push es
    mov ax, [eth_uqh]
    cmp ax, UQ_N
    jae .full
    mov di, UQ_SZ
    mul di
    add ax, eth_uq
    mov di, ax
    push ds
    pop es
    mov [di+UQ_LEN], cx
    add di, UQ_SRC              ; (past the length: the source, then the
    mov si, eth_rxb + IP_OFF + IPO_SRC ; payload)
    movsw
    movsw
    mov ax, [eth_rxb + IP_PAY + UDO_SP] ; the source port, network order
    xchg ah, al                 ; (be_get's load, written in place - it went in
    stosw                       ; driver size pass 1)
    mov si, eth_rxb + IP_PAY + UD_HLEN
    cmp cx, UQ_DATA
    jbe .c
    mov cx, UQ_DATA
.c:
    cld
    rep movsb
    inc word [eth_uqh]
    jmp short .out
.full:
    inc word [eth_uqdrop]
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

eth_uport:  dw 0                ; the package's port, 0 = none
eth_uslot:  db 0xFF             ; ...its instance
eth_uqh:    dw 0                ; datagrams queued
eth_uqdrop: dw 0                ; ...dropped, the queue full
eth_udst:   times 6 db 0        ; NETV_UDPTX's destination, ours
eth_uq:     times UQ_N * UQ_SZ db 0

%endif                          ; ETH_NORAW
