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

; --- the ring sizes are RUNTIME now (SPEC.md 72.13) -------------------------
; These are the DEFAULTS the driver starts at and the floor it falls back to;
; [sk_rxcap]/[sk_txcap] and their shifts in ethstate.inc are what the code
; reads. Every size is a power of two so the walk stays a shift and a compare.
SK_RXCAP0   equ 1024            ; the floor: what a machine that can spare
SK_TXCAP0   equ 512             ; nothing else still gets
SK_RXSH0    equ 10
SK_TXSH0    equ 9

SK_RXMAX    equ 8192            ; **AND THE CEILING IS THE SEGMENT.** Four
SK_TXMAX    equ 1024            ; sockets at 8192+1024 is 36,864 bytes; the
SK_RXMAXSH  equ 13              ; next step up is 69,632 and the offsets into
SK_TXMAXSH  equ 10              ; the claim are SIXTEEN BITS

SK_RXMID    equ 4096            ; the middle rung, and the one the arithmetic
SK_TXMID    equ 512             ; says is the sweet spot: ~29 KB/s predicted
SK_RXMIDSH  equ 12              ; against a ~155 KB/s per-byte ceiling, so it
SK_TXMIDSH  equ 9               ; is fast without crowding the CPU

; What the two MEASURED rungs cost, in KB. Derived from the sizes above and
; from NET_SOCKS rather than written down beside them, because a number
; written twice is a number that disagrees with itself later. The floor has no
; entry here on purpose: it is not measured against anything (see sk_claim).
SK_KBMAX    equ (NET_SOCKS * (SK_RXMAX + SK_TXMAX) + 1023) / 1024
SK_KBMID    equ (NET_SOCKS * (SK_RXMID + SK_TXMID) + 1023) / 1024
SK_SHARESH  equ 2               ; ...and a rung the user did not ASK for may
                                ; take at most a 1/(1<<2) - a QUARTER - of the
                                ; free heap. It is a SHIFT and not a divisor
                                ; because the test is one `shr` on an 8088. A
                                ; card is not the reason the machine has
                                ; memory, and 37KB out of a 128KB machine's
                                ; heap is a package that then will not load -
                                ; a failure nothing connects to the network
                                ; card, because the card works perfectly

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

TCP_MSS     equ SK_TXMAX        ; we never send more than one buffer's worth,
                                ; so this is the honest number to advertise -
                                ; and it is under the 1460 an Ethernet path
                                ; allows, so nothing here can provoke a
                                ; fragment (inet.inc drops those).
                                ;
                                ; **THE MAXIMUM AND NOT THE CURRENT ONE**, now
                                ; that the buffer is sized at attach: the MSS
                                ; is announced in the SYN, once, and a peer
                                ; that has been told 1024 may send 1024 for
                                ; the life of the connection. Advertising the
                                ; live [sk_txcap] would be advertising a
                                ; number that was chosen before the ring was,
                                ; and it bounds what WE send, which tcp_out
                                ; already clamps to the real one
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
    mov cx, [sk_rxsh]
    shl di, cl
    add di, [sk_base]
    pop cx                          ; **AN OFFSET IN [sk_seg], not in DS.** The
    pop ax                          ; rings moved out of the driver's bss into
    ret                             ; a claim (SPEC.md 72.13), so the RX area
                                    ; starts at 0 and every caller supplies the
sk_txbase:                          ; segment itself
    push ax
    push cx
    mov al, [bx+SKO_IDX]
    xor ah, ah
    mov di, ax
    mov cx, [sk_txsh]
    shl di, cl
    add di, [sk_txoff]              ; ...and TX sits above every RX ring
    add di, [sk_base]
    pop cx
    pop ax
    ret

; =============================================================================
; THE THREE RING COPIES (SPEC.md 72.13)
;
; mem_copy and usr_read move DS->DS and ES->DS, and the ring is in NEITHER
; segment now. Rather than juggle segment registers at four call sites - which
; is how a wrong ES gets written once and read back wrong for ever - the three
; shapes the driver actually needs are named here, and every one of them
; leaves DS and ES as it found them.
;
; **THEY ARE `rep movsb` NOW** (SPEC.md 72.16), and the note that used to stand
; here said the opposite: that a byte loop was right because movsb wants ES for
; the destination and one of these has the CALLER's segment in it already. That
; was not a reason, it was an inconvenience - each of them SETS both segments
; itself, which is one `mov` and the direction movsb moves in. The field
; profile priced the loop it excused at 83 clocks a byte against movsb's 17.
;
; The three shapes differ only in where the two segments come from:
;   ring_in    DS = the caller's ([eth_useg]),  ES = the claim
;   ring_out   DS = the claim,                  ES = ours
;   ring_move  DS = ES = the claim
; =============================================================================

; --- ring_in - the CALLER's buffer at SI -> the ring at DI, CX bytes --------
; **IT SETS ES ITSELF**, exactly as the usr_read it replaces did. That was
; not decoration there and it is not here: the caller of eth_v_send has been
; through the package door, and the segment to read from is the one banked in
; [eth_useg] rather than whatever ES happens to hold. Assuming the caller had
; loaded it cost one timed-out transfer to find.
ring_in_i:
    push ax
    push dx
    push cx
    push si
    push di
    push es
    push ds
    jcxz .out
    cmp word [sk_seg], 0        ; both reads BEFORE DS moves - neither word is
    je  .out                    ; reachable once it has
    mov ax, [sk_seg]
    mov dx, [eth_useg]
    mov es, ax                  ; **ES:DI = the ring, DS:SI = the caller's** -
    mov ds, dx                  ; the two the OTHER way round from what this
    cld                         ; used to do, because that is the direction
    rep movsb                   ; `movsb` moves in (72.16)
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop dx
    pop ax
    ret

; --- ring_out - the ring at SI -> DS:DI (a driver buffer), CX bytes ---------
ring_out_i:
    push ax
    push cx
    push si
    push di
    push es
    push ds                     ; DS moves below, so it is banked HERE and not
    jcxz .out                   ; on the arm that moves it - an unbalanced
    cmp word [sk_seg], 0        ; stack on the jcxz path is a `ret` to the
    je  .out                    ; wrong place, not a wrong answer
    mov ax, [sk_seg]
    push ds
    pop es                      ; ES:DI = ours...
    mov ds, ax                  ; ...and DS:SI the ring, which is the direction
    cld                         ; `movsb` moves in (72.16)
    rep movsb
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- ring_move - within the ring, SI -> DI, CX bytes ------------------------
; Forward, and every caller has dest < src, so an overlapping move is safe -
; the same guarantee the mem_copy it replaces relied on.
ring_move:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    jcxz .out
    cmp word [sk_seg], 0
    je  .out
    mov ax, [sk_seg]
    mov es, ax                  ; BOTH segments are the ring: `movsb` reads
    mov ds, ax                  ; through DS and writes through ES, and here
    cld                         ; they are the same block (72.16)
    rep movsb
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

%ifndef ETH_NOEMIT
; THE CLAIM IS THE OS's ONLY. The DOS partner (SPEC.md 62.11.1) shares every
; line of the ring code above but has no OSAPI_MEM_CLAIM to call - it is a
; .COM with megabytes and fixed bss - so it points [sk_seg]/[sk_base] at its
; own buffers in net_bss_clear and never comes here.
; =============================================================================
; sk_claim - take the rings, as big as the machine will give (SPEC.md 72.13)
; out: CF=0 and [sk_seg] set; CF=1 = not one rung was affordable
;
; A LADDER AND NOT A DEMAND. The driver used to carry 6KB of bss for these
; whatever the machine was, and could not have been given more; now it asks
; for the best rung and steps down until the heap says yes. The rungs are the
; three the arithmetic picks out:
;
;   8192 RX - measured against a ~155 KB/s per-byte ceiling, this is where
;             the window stops being the constraint and the CPU starts
;   4096 RX - the sweet spot: ~29 KB/s predicted, room to spare
;   1024 RX - the floor, and what the driver did before any of this
;
; **THE FLOOR STILL WORKS**, which is what makes the ladder honest: a machine
; that can spare 6KB gets exactly the stack it had, not a refusal to load.
; Only a machine that cannot spare even that is told no - and a NIC with no
; buffers cannot serve a socket, so that refusal is the true one.
;
; The user's choice in [sk_want] is tried FIRST and is not a floor: asking for
; 8192 on a machine that cannot hold it gets 4096 rather than nothing, because
; a slower network is a better answer than no network.
;
; **THE AUTO LADDER MEASURES THE HEAP; THE USER'S CHOICE DOES NOT.** Left to
; itself the ladder takes at most a QUARTER of the free heap (SK_SHARESH), so a
; 128KB machine gets the floor and a 640KB one gets the ceiling without anybody
; being asked - which is what "a sane default based on total RAM" means here.
; An explicit [sk_want] skips that test on purpose: a user who has opened the
; Control Panel and picked 8K has said something about their machine that the
; arithmetic does not know, and the claim itself is still the thing that can
; refuse. Only if the asked-for rung will not fit does the ladder run.
; =============================================================================
sk_claim:
    push ax
    push bx
    push cx
    push dx
    cmp word [sk_seg], 0
    jne .have                       ; a second attach must not claim twice
    mov bx, [sk_want]               ; the user's rung, if there is one
    or bx, bx
    jz .auto
    call sk_try
    jnc .done
.auto:
    call OSAPI_MEM_AVAIL        ; **AX = the largest run, BX = the TOTAL free**
    mov ax, bx                  ; (SPEC.md 50.3), and it is the total this
    mov cl, SK_SHARESH          ; measures against: the largest run is what the
    shr ax, cl                  ; claim itself will test, and a card that fits
                                ; in the biggest hole on a machine with no
                                ; other memory left has still taken the machine
    mov bx, SK_RXMAX            ; AX = a quarter of the free heap, in KB
    cmp ax, SK_KBMAX
    jb  .mid
    call sk_try
    jnc .done
.mid:
    mov bx, SK_RXMID
    cmp ax, SK_KBMID
    jb  .floor
    call sk_try
    jnc .done
.floor:
    mov bx, SK_RXCAP0           ; THE FLOOR IS NOT MEASURED. 6KB is what the
    call sk_try                 ; driver's bss used to be, so a machine that
    jnc .done                   ; cannot spare it could not have loaded the
                                ; driver at all a version ago
    pop dx                          ; not one rung: [sk_seg] stays 0 and every
    pop cx                          ; socket verb refuses at sk_ring
    pop bx
    pop ax
    stc
    ret
.have:
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; --- sk_release - hand the rings back ---------------------------------------
sk_release:
    push ax
    push dx
    mov dx, [sk_seg]
    or dx, dx
    jz .out
    mov word [sk_seg], 0        ; FIRST: [sk_seg] is the one value every ring
                                ; reader refuses on, so a pre-emption between
                                ; here and the free meets a refusal instead
                                ; of a freed segment taking TCP payload. The
                                ; old order zeroed LAST so a failed free
                                ; could not orphan a live segment - but a
                                ; freed-and-still-named segment is the worse
                                ; side of that trade: the failure it leaves
                                ; is a leak nothing references, not a write
    call OSAPI_MEM_FREE
.out:
    pop dx
    pop ax
    ret

; --- sk_busy - CF=1 if any socket is in use ---------------------------------
; The rings ARE the sockets' data: half a connection's receive queue and its
; unacknowledged send bytes both live in the claim, so freeing it under a live
; socket loses a transfer in progress and leaves TCP acknowledging bytes it can
; no longer produce. Every path that RESIZES asks this first.
sk_busy:
    push ax
    push bx
    push cx
    xor al, al
    mov cx, NET_SOCKS
.l:
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE
    jne .yes
    inc al
    loop .l
    pop cx
    pop bx
    pop ax
    clc
    ret
.yes:
    pop cx
    pop bx
    pop ax
    stc
    ret

; --- sk_reclaim - take [sk_want]'s rung instead of the one we hold ----------
; out: CF=0 and [sk_rxcap] is whatever the ladder could get; CF=1 = nothing
;      was touched, because a socket is live (sk_busy)
;
; A FREE AND A CLAIM AND NOT A REGROW, deliberately: mem_regrow can move a
; block, and everything that reads a ring reads [sk_seg] fresh through sk_ring,
; so a move would be safe - but the sizes and the shifts change with the rung
; and every ring's CONTENTS would then be at the wrong offsets inside it.
; There is nothing to preserve, because sk_busy has just said there is no
; socket, so the cheap thing and the correct thing are the same one.
sk_reclaim:
    call sk_busy
    jc  .no
    call sk_release             ; and the ladder always ends at the floor,
    call sk_claim               ; which is the 6KB we have just handed back -
    ret                         ; so this cannot leave the driver with none
.no:
    stc
    ret

; --- sk_try - BX = an RX size: claim it, or CF=1 ----------------------------
; The TX size and both shifts follow the RX rung rather than being chosen
; separately - there is one knob, and a machine generous enough for a big
; receive window is not the one to economise on the send buffer.
sk_try:
    push ax
    push cx
    push dx
    mov cx, SK_TXCAP0               ; the rung's TX size and shifts
    mov ax, SK_RXSH0
    mov dx, SK_TXSH0
    cmp bx, SK_RXMAX
    jne .nmax
    mov cx, SK_TXMAX
    mov ax, SK_RXMAXSH
    mov dx, SK_TXMAXSH
    jmp short .got
.nmax:
    cmp bx, SK_RXMID
    jne .got
    mov cx, SK_TXMID
    mov ax, SK_RXMIDSH
    mov dx, SK_TXMIDSH
.got:
    mov [sk_rxsh], ax
    mov [sk_txsh], dx
    mov [sk_rxcap], bx
    mov [sk_txcap], cx
    ; --- KB wanted: NET_SOCKS * (rx + tx), rounded up ----------------------
    mov ax, bx
    add ax, cx
    mov cl, 2                       ; NET_SOCKS is 4 (netpkg.inc), and this is
    shl ax, cl                      ; the one place that assumes it
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; AX = KB
    call OSAPI_MEM_CLAIM            ; out DX = the base segment
    jc .no
    mov [sk_seg], dx
    mov ax, bx                      ; TX starts above all four RX rings
    mov cl, 2
    shl ax, cl
    mov [sk_txoff], ax
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

%endif  ; ETH_NOEMIT

; --- sk_ring - ES = the claim, or CF=1 if there is not one -------------------
; Every ring access goes through this rather than loading [sk_seg] by hand,
; because a zero there means the claim was refused and the ring does not
; exist - and a copy into segment 0 is the interrupt vector table.
sk_ring:
    push ax
    mov ax, [sk_seg]
    or ax, ax
    jz .no
    mov es, ax
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; sk_alloc - a free slot
; out: CF=0 and BX = a zeroed record, AL = its 1-based HANDLE; CF=1 = none
;
; **AND A TIME-WAIT SLOT IS FREE ENOUGH WHEN THERE IS NOTHING ELSE** (SPEC.md
; 72.14). That is not a corner: it is what an FTP server does on every single
; transfer, and it is the bug the field measured as a 6-7 second hole at the
; START of an upload, before one byte of disk work.
;
; Four slots and an FTP session accounts for all four - the port-21 listener,
; the control connection, the passive data listener and the data connection
; itself - so the moment a transfer's data socket closes it goes into
; TCP_TWTMO's two-second hold holding the fourth. The client's NEXT transfer
; starts inside those two seconds, every time: PASV takes a slot for the new
; listener, the client connects, and the SYN that arrives has nowhere to go.
; It is dropped, and what the user sees is the client's own retransmit backoff
; - one second, then two, then four - before anything happens at all.
;
; TIME-WAIT exists to absorb a stray retransmitted FIN from a peer that is
; already finished with. Under pressure that is worth exactly less than a
; connection somebody is waiting for, and reusing the slot is safe for the
; reason the state is cheap to give up: delivery matches the four-tuple, so a
; straggler for the dead connection does not match the live one unless the
; ports collide too.
;
; The FIRST one found is taken and not the oldest. With four slots there is
; one, rarely two, and a deadline compare to choose between them would cost
; more than it could ever save.
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
    xor al, al                  ; nothing free: the second walk, for a slot
    mov cx, NET_SOCKS           ; that is only being held out of politeness
.tw:
    call sk_ptr
    cmp byte [bx+SKO_TS], TS_TIMEWT
    je  .got
    inc al
    loop .tw
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
    mov ax, [sk_rxcap]
    sub ax, [bx+SKO_RXN]
    ret

; -----------------------------------------------------------------------------
; sk_rxput - CX bytes at DS:SI into socket BX's ring
; out: CX = how many were taken (the rest are dropped and NOT acknowledged)
; -----------------------------------------------------------------------------
sk_rxput_i:
    push ax
    push dx
    push si
    push di
    push bp
    push es
    call sk_rxfree
    cmp cx, ax
    jbe .fits
    mov cx, ax
.fits:
    jcxz .zero
    call sk_ring                ; **THE RING IS IN [sk_seg]** (SPEC.md 72.13),
    jc  .zero                   ; so the store needs the override - without it
    add [bx+SKO_RXN], cx        ; this writes the driver's own bss at the
    push cx                     ; ring's old offsets, which assembles
    call sk_rxbase              ; perfectly and corrupts whatever is there
    mov bp, di                  ; BP = the ring's base...
    mov dx, bp
    add dx, [sk_rxcap]          ; ...and DX one past its end
    add di, [bx+SKO_RXH]        ; DI = where the writer stands
    mov ax, [bx+SKO_RXH]        ; ...and the new head, computed ONCE. CX cannot
    add ax, cx                  ; exceed sk_rxfree, so head + cx is under two
    cmp ax, [sk_rxcap]          ; capacities and one subtract is the whole wrap
    jb  .nw
    sub ax, [sk_rxcap]
.nw:
    mov [bx+SKO_RXH], ax
    cld                         ; **TWO `rep movsb` RUNS, SPLIT AT THE WRAP**
    mov ax, dx                  ; (SPEC.md 72.16). ne_ring_read has always cut
    sub ax, di                  ; the card's ring this way; the socket ring's
    cmp cx, ax                  ; own copies were a hand loop until the
    jbe .one                    ; profiler priced them
    sub cx, ax                  ; CX = what is left after the end...
    push cx
    mov cx, ax                  ; ...and AX was the run up to it
    rep movsb
    pop cx
    mov di, bp                  ; round to the base for the remainder
    rep movsb
    jmp short .fin
.one:
    rep movsb
.fin:
    pop cx                      ; the answer, banked before the move spent CX
    jmp short .out
.zero:
    xor cx, cx                  ; no claim, or a full ring: the same answer,
.out:                           ; and every caller already handles it
    pop es
    pop bp
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
; **THE RECORD COMES OUT OF THE LOOP** (SPEC.md 72.13). This copied one byte
; at a time and read [bx+SKO_RXT] on every one of them, which was affordable
; while the ring and the record shared the driver's DS. They do not any more:
; the ring is in the claim and the destination is the CALLER's, so the loop
; needs both segment registers and the record can be reached from neither.
;
; So the tail is advanced ONCE, before DS moves, and the loop walks a plain
; pointer with the ring's end and base in registers. It is also four
; instructions a byte shorter than what it replaces, which is not the reason
; but is not nothing at 4.77MHz.
sk_rxget_i:
    push ax
    push dx
    push si
    push di
    push bp
    push es
    push ds
    mov ax, [bx+SKO_RXN]
    cmp cx, ax
    jbe .fits
    mov cx, ax
.fits:
    jcxz .none
    cmp word [sk_seg], 0
    je  .none                   ; no claim, so no ring: nothing to give
    sub [bx+SKO_RXN], cx
    push cx                     ; THE ANSWER, banked before CX is spent
    push di                     ; ...and the destination offset
    call sk_rxbase              ; DI = this socket's ring, in the claim
    mov bp, di                  ; BP = base
    mov si, bp
    add si, [bx+SKO_RXT]        ; SI = where the reader stands
    mov dx, bp
    add dx, [sk_rxcap]          ; DX = one past the end
    mov ax, [bx+SKO_RXT]        ; ...and the new tail, while the record is
    add ax, cx                  ; still addressable
    cmp ax, [sk_rxcap]
    jb  .nw
    sub ax, [sk_rxcap]
.nw:
    mov [bx+SKO_RXT], ax
    mov ax, [sk_seg]            ; the ring's segment into a REGISTER first -
    pop di                      ; [sk_seg] is unreachable once DS moves
    mov es, [eth_useg]          ; ES:DI = the caller's buffer
    mov ds, ax                  ; DS:SI = the ring. NOTHING BELOW THIS LINE
                                ; may name a driver variable - and it does not
                                ; have to: DX, BP and the two pointers are all
                                ; registers, which is what lets `movsb` reach
                                ; across the two segments at all (72.16)
    cld
    mov ax, dx
    sub ax, si                  ; AX = bytes to the ring's end
    cmp cx, ax
    jbe .one
    sub cx, ax
    push cx
    mov cx, ax
    rep movsb
    pop cx
    mov si, bp                  ; ...round to the base
    rep movsb
    jmp short .fin
.one:
    rep movsb
.fin:
    pop cx                      ; the answer
    jmp short .out
.none:
    xor cx, cx
.out:
    pop ds
    pop es
    pop bp
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
    call ring_out               ; the ring is the CLAIM's, the frame is ours
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

; -----------------------------------------------------------------------------
; sk_reap_kids - a LISTENER is closing, so its un-accepted children go with it
; in:  BX = the listening socket
; out: nothing; every register preserved
;
; **CLOSING A LISTENER MUST DESTROY ITS PENDING QUEUE.** That is what every
; sockets implementation does and what this one did not, because until an FTP
; server (SPEC.md 77) there was nothing in the tree that listened at all - so
; the path had never been walked with a connection sitting on it.
;
; A connection the stack has completed but NETV_ACCEPT has not taken is marked
; SKF_ACPT and belongs to NOBODY: the package was never given a handle for it,
; so it cannot close it, and eth_v_accept will not hand it over once the peer
; has closed, because it matches on NSK_UP. So it sat in the table until the
; machine was turned off.
;
; **THE FOURTH HANDLE IS WHAT MAKES THAT FATAL RATHER THAN UNTIDY** (netpkg.inc
; keeps NET_SOCKS at 4 precisely for a server). Measured: an FTP server that
; refused ONE transfer - a read-only STOR - stranded the data connection the
; client had already dialled, and the NEXT transfer had no slot to accept
; into. The symptom is a server answering 227 and 150 perfectly and then never
; sending a byte, several commands after the one that actually broke, with
; every counter clean and no error anywhere.
;
; tcp_abort already does the right thing once it is reached: SKF_ACPT sends it
; straight to .free, on its own reasoning that "nobody ever asked for this
; one, so there is nobody to tell". The bug was that nothing ever reached it.
; -----------------------------------------------------------------------------
sk_reap_kids:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [bx+SKO_LP]         ; children carry the LISTENER's local port,
    xor dl, dl                  ; which is how eth_v_accept finds them too
    mov cx, NET_SOCKS
.l:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE
    je  .next
    test byte [bx+SKO_FL], SKF_ACPT
    jz  .next                   ; somebody owns this one
    cmp si, [bx+SKO_LP]
    jne .next
    mov al, NSK_FAILED          ; ...which nobody ever reads, SKF_ACPT taking
    call tcp_abort              ; tcp_abort to .free
.next:
    inc dl
    loop .l
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
tcp_in:                         ; STKDEPTH-NOSAVE: dx si di
    push ax                     ; DX, SI and DI are NOT saved here and that is
    push bx                     ; checked rather than asserted: this routine
    push cx                     ; touches none of the three itself, every
    cmp cx, TC_HLEN             ; callee below gives all three back, and
                                ; `tools/stkdepth.py --check` fails the build
                                ; the day one of them stops. Six bytes, held
                                ; across the deepest chain in the driver -
                                ; SPEC.md 1's blanket save is right everywhere
                                ; else and is what this trades away here
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
    pop cx                      ; ...and DX/SI/DI are not popped either. The
    pop bx                      ; pair has to move TOGETHER: DX's pop was once
    pop ax                      ; missing here on its own and the `ret` took
    ret                         ; the saved DX as its return address, with the
                                ; machine running off into this driver's data
                                ; and everything else still working

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
    call ring_move                      ; forward, and dest < src, so an
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
