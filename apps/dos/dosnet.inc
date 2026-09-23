; =============================================================================
; os8088 - apps/dos/dosnet.inc
;
; THE CABLE TRANSLATION (SPEC.md 96.26, docs/plans/DOS-CABLE-NET-PLAN.md)
;
; **A slirp in reverse.** The DOS program believes it is on an Ethernet: it
; ARPs, it builds IP headers, it runs its own TCP. Every frame it emits is
; terminated HERE and re-issued as a socket verb; every socket byte that
; arrives is wrapped in a header and handed back through §96.23.4's up-call.
;
; WHY, AND IT IS LATENCY RATHER THAN BANDWIDTH. The obvious design relays raw
; frames over the cable, and at PERFORMANCE.md Set 39's 3,741 B/s a frame is
; 0.405 s each way - so one round trip is 0.81 s, which sits on the edge of
; mTCP's own retransmit timeout. The first queue behind a data frame tips it
; into permanent retransmit. Translating sends only PAYLOAD and answers every
; acknowledgement locally in microseconds, so the client measures an RTT of
; about zero and its timers never fire.
;
; **IT DOES NOT KNOW WHICH WIRE IS UNDER IT.** Everything here goes through
; NETV_OPEN/STATUS/SEND/RECV/CLOSE, and both drivers answer those - the card
; and the cable. [cs:net_cls] is the whole difference, which is why this is
; developed and tested against ETHER.DRV under QEMU (DOSNETCARD=1) and works
; over NET.DRV unchanged.
;
; WHAT IT IS NOT: there is no ICMP (so no ping) and no UDP but the DNS port
; below, because the socket ABI has neither. Every TCP application works.
; =============================================================================

; --- THE TWO ADDRESSES, as IMAGE CONSTANTS with claim copies ----------------
; The ROUTER's, then the one we tell the CLIENT it has. Both are
; locally-administered unicast MACs: bit 1 of the first byte set, bit 0 clear
; (so not multicast), which is the range IEEE reserves for exactly this.
; Neither reaches a wire - the client is the only thing that ever reads them -
; but a client that saw a multicast bit here would refuse to ARP for us, and
; one that saw its OWN address as a frame's source would drop it as a loop,
; which is why they differ by a digit (SPEC.md 96.26.4).
;
; **THESE ARE THE `_c` COPIES AND THE CODE READS THE CLAIM'S.** Inside dn_*
; DS is the network claim (SPEC.md 96.26.6), so image data is out of reach
; without an override - and an override at each of seven call sites is more
; than the twelve bytes dn_init copies once.
dn_mac_c:
dn_gwmac_c: db 0x02, 'o', 's', '8', '8', 0x01
dn_ourmac_c: db 0x02, 'o', 's', '8', '8', 0x02
DN_MAC_C_N  equ 12

; --- ...and the environment variable that names the INBOUND ports ----------
; SPEC.md 96.26.8. Image data, read through CS like the MACs above and for the
; same reason. `OS88` and not `MTCP` or `DOS`: it is OUR variable in the
; user's own environment page, and a name a DOS program might itself define
; would be a collision nobody could debug.
dn_lsnvar:  db 'OS88LISTEN=', 0
DN_LSNVAR_N equ 11

; =============================================================================
; THE FOUR DOORS, AND DS IS THE CLAIM BEHIND EVERY ONE (SPEC.md 96.26.6)
; =============================================================================
; Everything under this line addresses its state and its frames DS-relative
; with no segment override, which was this file's premise when that state was
; in the package's bss - and it is still the premise now that the state is a
; heap claim, because ALL that changed is which segment DS holds. That is why
; moving 4,052 bytes out of a DOS program's arena cost fourteen `equ` lines
; and these four wrappers rather than a rewrite of seventy-eight frame
; references.
;
; `dos.asm` calls exactly four routines in here - dn_init, dn_shut, dn_tx and
; dn_ready - plus dn_copy, which takes whatever DS it is given and is used by
; dos.asm in its OWN segment, so it must stay unwrapped.
;
; **EACH ONE REFUSES ON A SEGMENT OF ZERO**, and that is not defensive
; padding: `dos_pkt_bufs` publishes no interface when the claim is refused,
; but dn_shut is called from the bracket's own teardown where nothing has
; consulted that - and `mov ds, 0` followed by this file's stores would write
; the interrupt vector table.
; **AND `[cs:net_cls]` IS THE ONE THING THAT STAYS OURS.** Which driver class
; answers the socket verbs is the package's own state, read at seven call
; sites in here, and CS is the package segment for a near-model package - so
; the override costs one prefix byte each and no copy. It is the tell for
; anything else that ever needs reaching out of here: an unprefixed reference
; to a package symbol from below this line reads the CLAIM at that offset and
; is silently wrong.
%macro DN_DOOR 1
dn_%1:
    push ds
    mov ds, [dos_pkt_bseg]          ; read with the OLD DS, which is ours
    mov ax, ds
    or ax, ax
    jz %%noclaim
    call dn_%1_x
    pop ds
    ret
%%noclaim:
    pop ds
    stc                             ; the two doors that answer report
    ret                             ; "nothing", which is what no claim means
%endmacro

DN_DOOR init
DN_DOOR shut
DN_DOOR tx
DN_DOOR ready

; -----------------------------------------------------------------------------
; dn_init_x - the translation is the route; forget everything
; out: nothing
; -----------------------------------------------------------------------------
dn_init_x:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, dn_mac_c                ; the two MACs, out of the IMAGE and into
    mov di, dn_gwmac                ; the claim - CS is ours and DS is the
    mov cx, DN_MAC_C_N              ; claim's, which is the one place in this
.m:                                 ; file an override is unavoidable
    mov al, [cs:si]
    mov [di], al
    inc si
    inc di
    loop .m
    pop di
    pop si
    mov bx, dn_flows
    mov cx, DN_NFLOW
.f:
    mov byte [bx+DN_F_USED], 0
    add bx, DN_F_SIZE
    loop .f
    mov bx, dn_names
    mov cx, DN_NNAME
.n:
    mov byte [bx+DN_N_USED], 0
    add bx, DN_N_SIZE
    loop .n
    mov word [dn_pend], 0
    mov byte [dn_lastip], 0
    mov byte [dn_rr], 0
    mov byte [dn_lrr], 0
    mov bx, dn_lsn                  ; ...and no listener until the environment
    mov cx, DN_NLSN                 ; says otherwise
.s:
    mov byte [bx+DN_L_USED], 0
    add bx, DN_L_SIZE
    loop .s
    mov word [dn_cip+0], 0
    mov word [dn_cip+2], 0
    call dn_lsn_init
    pop cx
    pop bx
    pop ax
    clc
    ret

; =============================================================================
; THE INBOUND DIRECTION (SPEC.md 96.26.8)
; =============================================================================
; **A LISTENING CLIENT SENDS NOTHING**, and that is the whole difficulty. A
; packet driver has no notion of a port, so when a DOS server binds one there
; is not a single frame on the wire to infer it from - unlike the outbound
; direction, where the client's own SYN carries everything we need.
;
; So the box has to be TOLD, and the place it is told is the one that already
; exists: the ENVIRONMENT PAGE (SPEC.md 96.20). `OS88LISTEN=80` in a row the
; user has typed anyway, beside the `MTCPCFG=` the program needs. No new
; dialog, no new file, and the DOS program sees it as a variable it ignores.
;
; A comma separates ports, so `OS88LISTEN=21,2048,2049` is three.
; -----------------------------------------------------------------------------
; dn_lsn_init - read the environment and take a listener per port named
;
; **[cs:dos_ebuf] AND NOT [dos_ebuf]**: DS is the network claim in here
; (SPEC.md 96.26.6) and the environment rows are the package's own bss, so
; this is the second place in this file that needs the override. An unprefixed
; read would scan whatever the claim holds at that offset.
; -----------------------------------------------------------------------------
dn_lsn_init:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, DOS_ENVN
    mov si, dos_ebuf
.row:
    push cx
    push si                         ; **THE ROW'S BASE HAS TO SURVIVE BOTH
    mov di, dn_lsnvar               ; CALLS**, and it is two calls rather than
    call dn_streqc                  ; one: dn_streqc walks SI to the first
    pop si                          ; difference and dn_lsn_ports walks it to
    push si                         ; the end of the port list. Restoring it
    jne .next                       ; only across the first left `.next`
    add si, DN_LSNVAR_N             ; adding DOS_ENVBUF to a cursor eleven-
    call dn_lsn_ports               ; plus bytes into the row that MATCHED, so
.next:                              ; every row after one that named ports was
    pop si                          ; scanned from the wrong place - which is
    pop cx                          ; the ordinary case, `MTCPCFG=` being the
    add si, DOS_ENVBUF              ; row a user types beside it
    loop .row
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dn_streqc - does [cs:SI] start with the NUL string at [cs:DI]? --------
; ZF=1 if it does. Both sides are the PACKAGE's, so both are CS-relative -
; which is why this is not dn_streqn.
dn_streqc:
    push ax
.l:
    mov al, [cs:di]
    or al, al
    jz .yes
    mov ah, [cs:si]
    cmp al, ah
    jne .no
    inc si
    inc di
    jmp short .l
.yes:
    pop ax
    cmp al, al                      ; ZF=1, whatever AL held
    ret
.no:
    pop ax
    or al, 0xFF                     ; ZF=0
    ret

; --- dn_lsn_ports - a comma list of decimal ports at [cs:SI], one listener --
;                    each, stopping at the NUL or when the table is full
dn_lsn_ports:
    push ax
    push bx
    push cx
    push dx
.one:
    xor ax, ax                      ; --- one decimal number ---
    xor cx, cx                      ; CX counts the digits, so a stray comma
.dig:                               ; cannot become port 0
    mov dl, [cs:si]
    cmp dl, '0'
    jb .end
    cmp dl, '9'
    ja .end
    sub dl, '0'
    mov bx, 10
    push dx                         ; **`mul` WRITES DX:AX AND THE DIGIT IS IN
    mul bx                          ; DL.** Third time in this file: dn_pump
    pop dx                          ; kept a counter in CX that NETV_STATUS
    add al, dl                      ; answers in, dn_seg kept flags in DL that
    adc ah, 0                       ; dn_put32's own DX:AX ate, and here the
                                    ; digit is destroyed one instruction before
                                    ; it is added. DX came back 0 for every
                                    ; port anyone would type, so `add al, dl`
                                    ; added NOTHING: five digits accumulated to
                                    ; zero, dn_lsn_ports' own `jz .done`
                                    ; refused to listen on port 0, and the
                                    ; whole inbound direction was silent with
                                    ; the environment row read correctly
    inc cx
    inc si
    jmp short .dig
.end:
    jcxz .done
    or ax, ax
    jz .done
    call dn_listen                  ; AX = the port, host order
.done:
    mov dl, [cs:si]
    cmp dl, ','
    jne .out
    inc si
    jmp short .one
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dn_listen - NETV_LISTEN on the port in AX, into a free listener row ----
dn_listen:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax                      ; the port, banked
    mov bx, dn_lsn
    mov cx, DN_NLSN
.f:
    cmp byte [bx+DN_L_USED], 0
    je .got
    add bx, DN_L_SIZE
    loop .f
    jmp short .out                  ; the table is full, which is not an error
.got:                               ; worth a refusal: the user named more
    mov cx, dx                      ; ports than DN_NLSN and the first four win
    push bx
    mov bh, [cs:net_cls]
    mov bl, NETV_LISTEN
    call OSAPI_DRV_CALL             ; in CX = the port, out AX = the handle
    pop bx
    jc .out                         ; the far side refused it - a port already
    or ax, ax                       ; bound on that machine is the usual
    jz .out                         ; reason, and there is nothing to say to
    mov [bx+DN_L_HAND], al          ; the DOS program about it
    mov [bx+DN_L_PORT], dx
    mov byte [bx+DN_L_USED], 1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_accept - one NETV_ACCEPT, round robin, and the SYN it owes the client
;
; **[dn_pend] IS CHECKED BEFORE THE ACCEPT AND NOT AFTER**, which is the whole
; ordering question here: an accepted socket we cannot announce is a connection
; the far side believes in and the client has never heard of, and there is
; nowhere to put it. So a staged frame means we do not ask this tick.
;
; out: nothing. [dn_pend] set if a connection arrived.
; -----------------------------------------------------------------------------
dn_accept:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [dn_pend], 0
    jne .out
    ; **AND WE MUST KNOW WHERE THE CLIENT IS.** Its address is banked from any
    ; IP frame it has sent (dn_tx), and a stack that has bound a port has
    ; usually ARPed and DHCPed long before anything connects to it - but not
    ; necessarily, and a SYN addressed to 0.0.0.0 is a frame the client drops
    ; while the far side holds a connection nobody answers. So we do not
    ; accept until we can address the answer.
    cmp word [dn_cip+0], 0
    jne .know
    cmp word [dn_cip+2], 0
    je .out
.know:
    mov al, [dn_lrr]                ; --- whose turn ---
    inc al
    cmp al, DN_NLSN
    jb .keep
    xor al, al
.keep:
    mov [dn_lrr], al
    mov ah, DN_L_SIZE
    mul ah
    add ax, dn_lsn
    mov si, ax                      ; SI = the listener row
    cmp byte [si+DN_L_USED], 0
    je .out
    mov al, [si+DN_L_HAND]
    mov bh, [cs:net_cls]
    mov bl, NETV_ACCEPT
    call OSAPI_DRV_CALL
    jc .out
    or ax, ax
    jz .out                         ; NOBODY YET, which netpkg.inc calls the
                                    ; ordinary answer and not an error
    mov dl, al                      ; the new handle, banked
    call dn_flow_new
    jc .noroom
    ; --- the flow, entirely of our own invention -------------------------
    mov byte [bx+DN_F_HAND], dl
    mov byte [bx+DN_F_ST], DN_S_SYNSENT
    mov ax, [si+DN_L_PORT]          ; the CLIENT's port is the one it is
    xchg al, ah                     ; listening on, in wire order for dn_flow
    mov [bx+DN_F_LPORT], ax
    mov ax, [dn_lrr]                ; ...and the far side's is ours to invent
    and ax, 0x00FF
    add ax, DN_LSN_PORT0
    xchg al, ah
    mov [bx+DN_F_RPORT], ax
    ; the SOURCE of what we send is a pool address, counted DOWN from the top
    ; so that it can never be one dn_dns handed out
    mov byte [bx+DN_F_RIP+0], 10
    mov byte [bx+DN_F_RIP+1], 88
    mov byte [bx+DN_F_RIP+2], 0
    mov al, 255
    sub al, [dn_lrr]
    mov [bx+DN_F_RIP+3], al
    push si                         ; ...and the destination is the client,
    push di                         ; which is the one thing here we had to be
    mov si, dn_cip                  ; TOLD rather than invent
    mov di, bx
    add di, DN_F_LIP
    mov cx, 4
    call dn_copy
    pop di
    pop si
    mov word [bx+DN_F_SND+0], 0     ; our ISN, and nothing expected yet: the
    mov word [bx+DN_F_SND+2], 0     ; client's SYN|ACK is what fills DN_F_RCV
    mov word [bx+DN_F_RCV+0], 0
    mov word [bx+DN_F_RCV+2], 0
    mov byte [bx+DN_F_USED], 1
    xor cx, cx                      ; --- and the SYN ---
    mov al, DN_SYN
    call dn_seg
    mov ax, 1                       ; a SYN consumes one sequence number
    call dn_adv
    jmp short .out
.noroom:
    mov al, dl                      ; no flow to put it in, so it is closed at
    mov bh, [cs:net_cls]            ; once rather than left half-accepted
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


; -----------------------------------------------------------------------------
; dn_shut_x - every flow closed, on the way out of the bracket
; -----------------------------------------------------------------------------
dn_shut_x:
    push ax
    push bx
    push cx
    mov bx, dn_flows
    mov cx, DN_NFLOW
.f:
    cmp byte [bx+DN_F_USED], 0
    je .next
    push bx
    push cx
    mov al, [bx+DN_F_HAND]
    mov bh, [cs:net_cls]
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    pop cx
    pop bx
    mov byte [bx+DN_F_USED], 0
.next:
    add bx, DN_F_SIZE
    loop .f
    ; --- AND THE LISTENERS, which outlive every flow through them ---------
    ; They are handles too, and NET_SOCKS is eight: a bracket that gave the
    ; flows back and kept four listeners would leave the far side with half
    ; its table gone for the rest of the session.
    mov bx, dn_lsn
    mov cx, DN_NLSN
.s:
    cmp byte [bx+DN_L_USED], 0
    je .snext
    push bx
    push cx
    mov al, [bx+DN_L_HAND]
    mov bh, [cs:net_cls]
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    pop cx
    pop bx
    mov byte [bx+DN_L_USED], 0
.snext:
    add bx, DN_L_SIZE
    loop .s
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dn_tx - ONE frame from the client, CX bytes at DS:SI (our own segment)
;
; This is NETV_RAWTX's place in the routing (SPEC.md 96.26.1). It always
; reports success: a frame we do not understand is DROPPED, which is what a
; real wire does to a packet nothing answers, and a client that mistook a
; refusal for a dead card would stop trying.
; out: CF=0 always
; -----------------------------------------------------------------------------
dn_tx_x:
    push ax
    push bx
    push cx
    push si
    push di
    cmp cx, DN_EH_LEN
    jbe .drop
    mov ax, [si+DN_EH_TYP]          ; the ethertype, wire order
    xchg al, ah
    cmp ax, NET_ETY_ARP
    je .arp
    cmp ax, NET_ETY_IP
    jne .drop
    ; --- **WHOSE ADDRESS IS THE CLIENT'S** (SPEC.md 96.26.8) --------------
    ; Banked from every IP frame it sends, because an INBOUND connection has
    ; never seen one: NETV_ACCEPT hands us a socket and nothing else, so
    ; without this there is no destination to put in the SYN we owe the
    ; client. An outbound flow reads the same thing out of its own SYN and
    ; does not need it.
    push cx                         ; **CX IS THE FRAME LENGTH AND THE COPY
    push si                         ; TAKES IT AS AN ARGUMENT.** dn_copy
    push di                         ; preserves CX, so the first version of
    mov di, dn_cip                  ; this banked nothing - and then set it to
    add si, DN_IP_SRC               ; 4 itself, two instructions later. Every
    mov cx, 4                       ; IP frame the client sent was then
    call dn_copy                    ; dropped by the `cmp cx, DN_TCP` below
    pop di                          ; for being 4 bytes long: the ARP still
    pop si                          ; worked (it is answered above this),
    pop cx                          ; nothing else did, and get_statistics
    cmp cx, DN_TCP                  ; read `out 2, in 1` - a SYN sent and an
                                    ; ARP reply back, which is exactly what a
                                    ; far side that never heard of the
                                    ; connection looks like
    jb .drop
    cmp byte [si+DN_IP_VIHL], 0x45  ; v4, no options - the same refusal
    jne .drop                       ; eth_frame makes, for its reason
    mov al, [si+DN_IP_PROTO]
    cmp al, DN_IPP_TCP
    je .tcp
    cmp al, DN_IPP_UDP
    je .udp
.drop:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.arp:
    call dn_arp
    jmp short .drop
.udp:
    call dn_udp
    jmp short .drop
.tcp:
    call dn_tcp_in
    jmp short .drop

; -----------------------------------------------------------------------------
; dn_arp - answer it, whatever it asked for (SPEC.md 96.26.1)
; in:  SI = the frame, CX = its length
;
; **WE ARE THE WHOLE SEGMENT.** There is no wire to forward an ARP onto, so
; every request gets the same synthetic MAC and the client's routing table
; collapses to "everything is one hop away". That is exactly true here.
; -----------------------------------------------------------------------------
dn_arp:
    push ax
    push cx
    push si
    push di
    cmp cx, DN_ARP_END
    jb .out
    mov ax, [si+DN_ARP_OP]
    cmp ax, 0x0100                  ; oper 1, wire order: a REQUEST
    jne .out

    ; --- **NEVER ANSWER A DUPLICATE-ADDRESS PROBE** (SPEC.md 96.26.1.1) ----
    ; A stack coming up asks whether anything already holds the address it is
    ; about to use, and it asks with an ARP whose TARGET is its OWN address -
    ; either from that address (a gratuitous probe) or from 0.0.0.0. A reply
    ; means "taken", and a responder that answers EVERYTHING answers this too.
    ;
    ; mTCP then refuses to initialise at all, and what it prints is "Failed to
    ; initialize TCP/IP" - which names neither ARP nor an address and sends
    ; you looking at the config. It cost a session to find: every counter said
    ; the translation was idle because the client had given up before its
    ; first socket call.
    push cx
    push si
    push di
    mov di, si
    add di, DN_ARP_TPA
    add si, DN_ARP_SPA
    mov cx, 4
    call dn_streqn                  ; target == sender: a probe for its own
    pop di                          ; address
    pop si
    pop cx
    je .out
    push cx
    push si
    mov si, si
    add si, DN_ARP_SPA
    mov cx, 4
    call dn_iszero                  ; ...or from 0.0.0.0, which is the other
    pop si                          ; spelling of the same question
    pop cx
    je .out

    mov di, dn_frame                ; --- build the reply ---
    push si
    mov cx, 6                       ; destination: whoever asked
    add si, DN_ARP_SHA
    call dn_copy
    pop si
    mov di, dn_frame + DN_EH_SRC
    push si
    mov si, dn_gwmac                ; source: us, the router
    mov cx, 6
    call dn_copy
    pop si
    mov word [dn_frame+DN_EH_TYP], 0x0608   ; ARP, wire order
    mov word [dn_frame+DN_EH_LEN+0], 0x0100 ; htype 1
    mov word [dn_frame+DN_EH_LEN+2], 0x0008 ; ptype IPv4
    mov byte [dn_frame+DN_EH_LEN+4], 6
    mov byte [dn_frame+DN_EH_LEN+5], 4
    mov word [dn_frame+DN_ARP_OP], 0x0200   ; oper 2: a REPLY
    mov di, dn_frame + DN_ARP_SHA
    push si
    mov si, dn_gwmac                ; sender hardware: us
    mov cx, 6
    call dn_copy
    pop si
    push si                         ; sender protocol: WHATEVER WAS ASKED FOR,
    add si, DN_ARP_TPA              ; copied straight back out of the request
    mov di, dn_frame + DN_ARP_SPA
    mov cx, 4
    call dn_copy
    pop si
    push si                         ; target hardware and protocol: the asker
    add si, DN_ARP_SHA
    mov di, dn_frame + DN_ARP_THA
    mov cx, 6
    call dn_copy
    pop si
    push si
    add si, DN_ARP_SPA
    mov di, dn_frame + DN_ARP_TPA
    mov cx, 4
    call dn_copy
    pop si
    mov word [dn_pend], DN_ARP_END  ; ...and it is waiting for the next poll
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_ready - is there a frame for the client, and how long is it?
; out: CF=0 and CX = its length, IN dn_frame; CF=1 = nothing waiting
;
; **IT DOES NOT COPY, and that is why it is not shaped like NETV_RAWRX.** The
; card path's frame arrives in a heap claim and has to be moved into a buffer
; the caller names; ours was BUILT in dn_frame, in the one segment everything
; here shares, so a copy would be from our own bss to our own bss. The
; up-call's own copy to the client is the only one on this path.
;
; Two sources, and the order matters: a frame already staged - an ARP reply, a
; reset - goes first, because it is an answer the client is blocked on. Only
; then is a socket asked for data.
; -----------------------------------------------------------------------------
dn_ready_x:
    cmp word [dn_pend], 0
    jne .have
    call dn_pump                    ; nothing staged, so try the sockets
    cmp word [dn_pend], 0
    je .none
.have:
    mov cx, [dn_pend]
    mov word [dn_pend], 0
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; dn_copy - DS:SI -> DS:DI, CX bytes. One segment, so no override and no
;           string instruction whose DF we would have to trust.
; -----------------------------------------------------------------------------
dn_copy:
    push ax
    push cx
    push si
    push di
    jcxz .out
.l:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .l
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_cksum - the one's-complement sum of CX bytes at SI, folded to 16 bits
; out: AX = the value to STORE (already complemented)
;
; The carry is folded back on every word rather than collected and added at
; the end - the end-around carry is what makes that identical, and it removes
; the second fold that a collector needs and that is easy to forget.
; -----------------------------------------------------------------------------
dn_cksum:
    push bx
    push cx
    push dx
    push si
    xor bx, bx
    mov dl, cl                      ; bank the ODDNESS before CX becomes words
    and dl, 1
    shr cx, 1
    jz .tail
.w:
    mov ah, [si]                    ; wire order: the first byte is the HIGH
    mov al, [si+1]                  ; half of the word
    add bx, ax
    adc bx, 0
    inc si
    inc si
    loop .w
.tail:
    or dl, dl                       ; an odd length pads with a zero LOW byte
    jz .done
    mov ah, [si]
    xor al, al
    add bx, ax
    adc bx, 0
.done:
    mov ax, bx
    not ax
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dn_get32 / dn_put32 - a 32-bit wire field, in DX:AX
;
; TCP's sequence numbers are the one place this file needs arithmetic wider
; than the machine. They are stored in the flow the same way they arrive - as
; DX:AX pairs in host order - and converted only at the header, because every
; comparison and every addition happens in the pair and only the wire cares
; which end is which.
; in:  get  SI = the field.  put  DI = the field, DX:AX = the value
; -----------------------------------------------------------------------------
dn_get32:
    mov ah, [si]                    ; big-endian: byte 0 is the most
    mov al, [si+1]                  ; significant
    mov dx, ax
    mov ah, [si+2]
    mov al, [si+3]
    ret

dn_put32:
    xchg dh, dl
    mov [di], dx
    xchg dh, dl
    xchg ah, al
    mov [di+2], ax
    xchg ah, al
    ret

; -----------------------------------------------------------------------------
; dn_flow - the flow this frame belongs to, by the client's port
; in:  SI = the frame
; out: CF=0 and BX = the flow; CF=1 = none
; -----------------------------------------------------------------------------
dn_flow:
    push ax
    push cx
    mov ax, [si+DN_T_SPORT]         ; wire order both sides, so no swap
    mov bx, dn_flows
    mov cx, DN_NFLOW
.l:
    cmp byte [bx+DN_F_USED], 0
    je .next
    cmp [bx+DN_F_LPORT], ax
    je .got
.next:
    add bx, DN_F_SIZE
    loop .l
    pop cx
    pop ax
    stc
    ret
.got:
    pop cx
    pop ax
    clc
    ret

; --- dn_flow_new - a free flow, or CF=1 -------------------------------------
dn_flow_new:
    push cx
    mov bx, dn_flows
    mov cx, DN_NFLOW
.l:
    cmp byte [bx+DN_F_USED], 0
    je .got
    add bx, DN_F_SIZE
    loop .l
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

; =============================================================================
; THE DNS HIJACK (SPEC.md 96.26.2)
; =============================================================================
; There is no UDP in the socket ABI, so a DNS query cannot be relayed - and
; that is the best simplification in this file rather than its hard edge,
; because NETV_OPEN takes a host NAME and the far side resolves it.
;
; So: remember the name, answer with a synthetic address out of 10.88.0.0/24,
; and when the client connects to that address, open the NAME. We never need a
; resolver, a cache or a UDP path of our own.
; -----------------------------------------------------------------------------
; dn_udp - a UDP datagram from the client. Port 53 is ours; the rest is
;          dropped, which is what an unrouted datagram deserves.
; in:  SI = the frame, CX = its length
; -----------------------------------------------------------------------------
dn_udp:
    push ax
    cmp cx, DN_UDP + 8 + 12
    jb .out
    mov ax, [si+DN_UDP+2]           ; the DESTINATION port, wire order
    cmp ax, 0x3500                  ; 53
    jne .out
    call dn_dns
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_dns - one query in, one A record out
; in:  SI = the frame
;
; It reads ONE question and answers it, which is all any resolver in a DOS
; program sends. The QNAME is copied out in its wire form - length-prefixed
; labels - and flattened to dots, because that is the shape NETV_OPEN wants.
; -----------------------------------------------------------------------------
dn_dns:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov di, si
    add di, DN_UDP + 8              ; the DNS header
    cmp word [di+4], 0x0100         ; exactly one question, wire order
    jne .out

    ; --- the QNAME, labels to dots ----------------------------------------
    mov bx, di
    add bx, 12                      ; the question starts after the header
    mov si, bx
    mov di, dn_qname
    xor cx, cx                      ; CX = how long the flattened name is
.lab:
    mov al, [si]
    or al, al
    jz .named                       ; the root label ends it
    test al, 0xC0                   ; a POINTER has no business in a query and
    jnz .out                        ; following one here would be a loop we
                                    ; have no budget to detect
    inc si
    xor ah, ah
    push ax
    add cx, ax
    inc cx                          ; ...and the dot
    cmp cx, DN_NAMEMAX - 1
    jae .outp                       ; a name longer than we can hold is a
                                    ; refusal, not a truncation
    pop ax
    mov cx, ax
.cp:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [di], '.'
    inc di
    jmp short .lab
.outp:
    pop ax
    jmp short .out
.named:
    cmp di, dn_qname                ; an empty name is not a question
    je .out
    dec di                          ; ...and the trailing dot comes off
    mov byte [di], 0

    ; --- remember it, and answer with the slot's own address ---------------
    call dn_name_put                ; AL = the pool index, CF=1 = full
    jc .out
    mov [dn_lastip], al
    call dn_dns_reply
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_name_put - remember dn_qname, and say which synthetic address it is
; out: CF=0 and AL = the last octet; CF=1 = no room
;
; A name already known keeps its address, so a client that resolves twice and
; connects once still connects to the right thing.
; -----------------------------------------------------------------------------
dn_name_put:
    push bx
    push cx
    push si
    push di
    mov bx, dn_names                ; --- pass 1: do we know it already? ---
    xor cl, cl
.known:
    cmp byte [bx+DN_N_USED], 0
    je .knext
    mov si, dn_qname
    mov di, bx
    add di, DN_N_NAME
    call dn_streq
    je .hit
.knext:
    add bx, DN_N_SIZE
    inc cl
    cmp cl, DN_NNAME
    jb .known
    mov bx, dn_names                ; --- pass 2: a free slot? ---
    xor cl, cl
.free:
    cmp byte [bx+DN_N_USED], 0
    je .take
    add bx, DN_N_SIZE
    inc cl
    cmp cl, DN_NNAME
    jb .free
    mov bx, dn_names                ; --- FULL: slot 0 goes ---
    xor cl, cl                      ; A name we cannot answer is a connection
                                    ; that fails, and the client has just said
                                    ; which name it cares about NOW. Four is
                                    ; more than any one program resolves at a
                                    ; time, so this is the pathological case
                                    ; and not the ordinary one
.take:
    mov byte [bx+DN_N_USED], 1
    mov si, dn_qname
    mov di, bx
    add di, DN_N_NAME
    push cx                         ; CL IS THE SLOT and dn_copy wants CX for
    mov cx, DN_NAMEMAX              ; the count - it preserves CX, so what
    call dn_copy                    ; comes back is the count and not the slot
    pop cx
.hit:
    mov al, cl
    add al, DN_POOL0
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; dn_ipstr - the four bytes at SI as a NUL-terminated dotted quad at DI
;
; Three digits at most and no leading zeros, because that is what a resolver's
; own parser expects - ip_parse in dns.inc reads decimal and stops at the dot.
; -----------------------------------------------------------------------------
dn_ipstr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, 4
.oct:
    mov al, [si]
    inc si
    xor ah, ah
    mov bl, 100
    div bl                          ; AL = hundreds, AH = the rest
    or al, al
    jz .tens
    add al, '0'
    mov [di], al
    inc di
    mov al, ah                      ; ...and once a digit is out, the next two
    xor ah, ah                      ; are emitted whatever they are
    mov bl, 10
    div bl
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    jmp short .ones
.tens:
    mov al, ah
    xor ah, ah
    mov bl, 10
    div bl
    or al, al
    jz .ones2
    add al, '0'
    mov [di], al
    inc di
    mov al, ah
    jmp short .ones
.ones2:
    mov al, ah
.ones:
    add al, '0'
    mov [di], al
    inc di
    dec cx
    jz .done
    mov byte [di], '.'
    inc di
    jmp short .oct
.done:
    mov byte [di], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dn_streqn - are the CX bytes at SI and DI the same? out ZF=1 = yes -----
dn_streqn:
    push ax
    push cx
    push si
    push di
    jcxz .same
.l:
    mov al, [si]
    cmp al, [di]
    jne .no
    inc si
    inc di
    loop .l
.same:
    pop di
    pop si
    pop cx
    pop ax
    cmp al, al                      ; ZF=1
    ret
.no:
    pop di
    pop si
    pop cx
    pop ax
    or al, 1                        ; ZF=0 - and AL is ours to spend, since
    ret                             ; every caller here wants only the flag

; --- dn_iszero - are the CX bytes at SI all zero? out ZF=1 = yes ------------
dn_iszero:
    push ax
    push cx
    push si
    jcxz .yes
.l:
    cmp byte [si], 0
    jne .no
    inc si
    loop .l
.yes:
    pop si
    pop cx
    pop ax
    cmp al, al
    ret
.no:
    pop si
    pop cx
    pop ax
    or al, 1
    ret

; --- dn_streq - is the NUL string at SI the same as the one at DI? ----------
; out: ZF=1 = equal
dn_streq:
    push si
    push di
    push ax
.l:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .l
.yes:
    pop ax
    pop di
    pop si
    cmp al, al                      ; ZF=1, touching nothing else
    ret
.no:
    pop ax
    pop di
    pop si
    or al, 1                        ; ZF=0
    ret

; -----------------------------------------------------------------------------
; dn_ip_out - wrap dn_frame's payload in Ethernet + IP and stage it
; in:  SI = the frame the client SENT (its addresses are reversed into ours),
;      AL = the IP protocol, CX = the payload's length past DN_TCP,
;      [dn_lastip] = the last octet of the source we claim to be
; out: [dn_pend] = the whole frame's length
; -----------------------------------------------------------------------------
dn_ip_out:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, al                      ; the protocol, banked
    mov bx, cx                      ; ...and the payload length

    mov di, dn_frame                ; --- Ethernet: back to whoever sent it ---
    push si
    add si, DN_EH_SRC
    mov cx, 6
    call dn_copy
    pop si
    mov di, dn_frame + DN_EH_SRC
    push si
    mov si, dn_gwmac
    mov cx, 6
    call dn_copy
    pop si
    mov word [dn_frame+DN_EH_TYP], 0x0008   ; IPv4, wire order

    mov byte [dn_frame+DN_IP_VIHL], 0x45    ; --- IP ---
    mov byte [dn_frame+DN_EH_LEN+1], 0      ; no DSCP
    mov ax, bx
    add ax, DN_IP_HLEN
    xchg al, ah
    mov [dn_frame+DN_IP_LEN], ax            ; total length, wire order
    mov word [dn_frame+DN_EH_LEN+4], 0      ; id: nothing here fragments, and
    mov word [dn_frame+DN_EH_LEN+6], 0      ; flags/offset with it
    mov byte [dn_frame+DN_IP_TTL], 64
    mov [dn_frame+DN_IP_PROTO], dl
    mov word [dn_frame+DN_IP_CK], 0         ; zero ACROSS the sum, always
    push si                                 ; source: the address the client
    add si, DN_IP_DST                       ; was TALKING to, straight back
    mov di, dn_frame + DN_IP_SRC
    mov cx, 4
    call dn_copy
    pop si
    push si                                 ; ...and destination: its own
    add si, DN_IP_SRC
    mov di, dn_frame + DN_IP_DST
    mov cx, 4
    call dn_copy
    pop si
    mov si, dn_frame + DN_EH_LEN
    mov cx, DN_IP_HLEN
    call dn_cksum
    xchg al, ah
    mov [dn_frame+DN_IP_CK], ax
    mov ax, bx
    add ax, DN_TCP
    mov [dn_pend], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_dns_reply - the question back, plus one A record pointing at us
; in:  SI = the query frame, [dn_lastip] = the address to answer with
;
; The answer is the QUESTION COPIED BACK with an answer appended, which is
; what every resolver accepts and what keeps this free of a name encoder: the
; labels we would have to emit are already on the wire in front of us.
; -----------------------------------------------------------------------------
dn_dns_reply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    add bx, DN_UDP + 8              ; the query's DNS header
    mov cx, [si+DN_UDP+4]           ; the UDP length, wire order
    xchg cl, ch
    sub cx, 8                       ; ...so this is the DNS message's length
    cmp cx, 12
    jb .out
    cmp cx, 400                     ; a query we would not fit an answer
    ja .out                         ; behind is one we decline to answer

    mov di, dn_frame + DN_UDP + 8   ; --- the DNS message, copied whole ---
    push cx
    mov si, bx
    call dn_copy
    pop cx
    mov di, dn_frame + DN_UDP + 8
    mov word [di+2], 0x8081         ; QR=1, RD=1, RA=1 - wire order of 0x8180
    mov word [di+6], 0x0100         ; ANCOUNT = 1
    mov word [di+8], 0              ; ...and no authority or additional
    mov word [di+10], 0

    add di, cx                      ; --- the answer, after the question ---
    mov word [di+0], 0x0CC0         ; NAME: a pointer to offset 12, which is
                                    ; where the question's own name sits
    mov word [di+2], 0x0100         ; TYPE A
    mov word [di+4], 0x0100         ; CLASS IN
    mov word [di+6], 0              ; TTL: 30 seconds, wire order. Short on
    mov word [di+8], 0x1E00         ; purpose - the mapping lives in four
                                    ; slots and a client that cached for an
                                    ; hour would outlive its own entry
    mov word [di+10], 0x0400        ; RDLENGTH 4
    mov byte [di+12], 10            ; ...and the address: 10.88.0.N
    mov byte [di+13], 88
    mov byte [di+14], 0
    mov al, [dn_lastip]
    mov [di+15], al
    add cx, 16                      ; the DNS message is that much longer

    mov ax, [si+DN_UDP+2]           ; --- UDP: the ports, swapped ---
    mov [dn_frame+DN_UDP+0], ax
    mov ax, [si+DN_UDP+0]
    mov [dn_frame+DN_UDP+2], ax
    mov ax, cx
    add ax, 8
    xchg al, ah
    mov [dn_frame+DN_UDP+4], ax     ; length, wire order
    mov word [dn_frame+DN_UDP+6], 0 ; **CHECKSUM ZERO IS LEGAL FOR UDP over
                                    ; IPv4 and means "not computed"** (RFC
                                    ; 768), which every resolver accepts and
                                    ; which saves a pseudo-header sum here
    add cx, 8                       ; the payload past DN_TCP
    mov al, DN_IPP_UDP
    call dn_ip_out
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE TCP ENDPOINT (SPEC.md 96.26.3)
; =============================================================================
; What is underneath is not a network: a NETV_SEND either takes bytes or does
; not, in order, locally. So this needs no congestion control, no retransmit
; queue and no reassembly - only the state machine, the sequence arithmetic
; and the checksum. DOS-CABLE-NET-PLAN 5 is the full list of what that removes.
;
; **ONE FRAME IS STAGED AT A TIME**, because dn_frame is one buffer, so every
; builder here checks [dn_pend] first and does nothing if something is already
; waiting. For a pure ACK that is free - they are cumulative, so the next one
; carries what the skipped one would have - and for a SYN|ACK or a FIN the
; STATE IS NOT ADVANCED until the frame is staged, so the next poll retries.
; -----------------------------------------------------------------------------
; dn_seg - build a segment for a flow, from the FLOW and nothing else
; in:  BX = the flow, AL = the TCP flags, CX = payload bytes ALREADY placed at
;      dn_frame + DN_T_PAY
; out: [dn_pend] set. Does NOT advance the sequence - the caller knows whether
;      its flags consume one (SYN and FIN do; data consumes its length)
; -----------------------------------------------------------------------------
dn_seg:
    push ax
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, cx                      ; the payload length, banked
    mov [dn_frame+DN_T_FLAGS], al   ; **THE FLAGS GO STRAIGHT INTO THE FRAME**
                                    ; and are not banked in a register, which
                                    ; is how this was wrong: they were held in
                                    ; DL, and dn_put32 takes DX:AX - so the
                                    ; sequence load below overwrote them and
                                    ; what went out was the low byte of the
                                    ; acknowledgement's high word. A SYN|ACK
                                    ; (0x12) left as SYN|FIN|PSH|URG (0x2B),
                                    ; with both checksums CORRECT over it, so
                                    ; the client dropped it in silence and sat
                                    ; waiting for a handshake that had already
                                    ; been answered

    mov si, dn_ourmac               ; --- Ethernet: to the client, from us ---
    mov di, dn_frame
    mov cx, 6
    call dn_copy
    mov si, dn_gwmac
    mov di, dn_frame + DN_EH_SRC
    mov cx, 6
    call dn_copy
    mov word [dn_frame+DN_EH_TYP], 0x0008   ; IPv4, wire order

    mov byte [dn_frame+DN_IP_VIHL], 0x45    ; --- IP ---
    mov byte [dn_frame+DN_EH_LEN+1], 0
    mov ax, bp
    add ax, DN_IP_HLEN + DN_T_HLEN
    xchg al, ah
    mov [dn_frame+DN_IP_LEN], ax
    mov word [dn_frame+DN_EH_LEN+4], 0      ; id, and flags/offset: nothing
    mov word [dn_frame+DN_EH_LEN+6], 0      ; here fragments
    mov byte [dn_frame+DN_IP_TTL], 64
    mov byte [dn_frame+DN_IP_PROTO], DN_IPP_TCP
    mov word [dn_frame+DN_IP_CK], 0         ; zero ACROSS the sum, always
    mov si, bx
    add si, DN_F_RIP                        ; source: what it connected TO
    mov di, dn_frame + DN_IP_SRC
    mov cx, 4
    call dn_copy
    mov si, bx
    add si, DN_F_LIP                        ; ...and destination: its own
    mov di, dn_frame + DN_IP_DST
    mov cx, 4
    call dn_copy
    mov si, dn_frame + DN_EH_LEN
    mov cx, DN_IP_HLEN
    call dn_cksum
    xchg al, ah
    mov [dn_frame+DN_IP_CK], ax

    mov ax, [bx+DN_F_RPORT]                 ; --- TCP ---
    mov [dn_frame+DN_T_SPORT], ax
    mov ax, [bx+DN_F_LPORT]
    mov [dn_frame+DN_T_DPORT], ax
    mov si, bx
    add si, DN_F_SND
    mov ax, [si]
    mov dx, [si+2]
    mov di, dn_frame + DN_T_SEQ
    call dn_put32
    mov si, bx
    add si, DN_F_RCV
    mov ax, [si]
    mov dx, [si+2]
    mov di, dn_frame + DN_T_ACK
    call dn_put32
    mov byte [dn_frame+DN_T_OFF], 0x50      ; five words, no options
    mov ax, DN_WIN                          ; a FIXED window, and it is our
    xchg al, ah                             ; staging buffer's size: the client
    mov [dn_frame+DN_T_WIN], ax             ; must never send more than we can
    mov word [dn_frame+DN_T_CK], 0          ; hold, because there is nowhere to
    mov word [dn_frame+DN_T_URG], 0         ; put an overflow and no reassembly
    mov cx, bp
    add cx, DN_T_HLEN
    call dn_tcp_ck
    mov cx, bp
    add cx, DN_T_HLEN
    mov ax, cx
    add ax, DN_TCP
    mov [dn_pend], ax
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; --- dn_adv - the flow's send sequence forward by AX --------------------------
dn_adv:
    push si
    push dx
    mov si, bx
    add si, DN_F_SND
    add [si], ax
    adc word [si+2], 0
    pop dx
    pop si
    ret

; --- dn_advr - ...and the receive one, which is what we acknowledge ----------
dn_advr:
    push si
    push dx
    mov si, bx
    add si, DN_F_RCV
    add [si], ax
    adc word [si+2], 0
    pop dx
    pop si
    ret

; --- dn_paylen - how many bytes of payload are in the client's segment ------
; in:  SI = the frame.  out: AX = the length (0 for a pure ACK)
dn_paylen:
    push bx
    push cx
    mov bx, si
    mov ah, [bx+DN_IP_LEN]          ; the IP total length, wire order - NOT the
    mov al, [bx+DN_IP_LEN+1]        ; frame's, because the card pads to 60
    sub ax, DN_IP_HLEN
    jb .none
    mov cl, [bx+DN_T_OFF]           ; ...and the TCP header's own length, which
    shr cl, 1                       ; is the high nibble in 32-bit words
    shr cl, 1
    shr cl, 1
    shr cl, 1
    and cl, 0x0F
    shl cl, 1
    shl cl, 1
    xor ch, ch
    sub ax, cx
    jb .none
    pop cx
    pop bx
    ret
.none:
    xor ax, ax
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dn_tcp_in - one TCP segment from the client
; in:  SI = the frame, CX = its length
; -----------------------------------------------------------------------------
dn_tcp_in:
    push ax
    push bx
    push cx
    push dx
    push di
    test byte [si+DN_T_FLAGS], DN_RST
    jnz .reset                      ; never answer a reset with a reset
    test byte [si+DN_T_FLAGS], DN_SYN
    jnz .syn
    call dn_flow
    jc .unknown                     ; a segment for a flow we do not have
    cmp byte [bx+DN_F_ST], DN_S_SYNSENT
    je .out                         ; an inbound flow that has not handshaken
                                    ; yet: anything but a SYN|ACK or an RST is
                                    ; not an answer to our SYN, and the client
                                    ; has not agreed to the connection
    cmp byte [bx+DN_F_ST], DN_S_UP
    jne .out                        ; still connecting, or closing: its own
                                    ; retransmit will find us in a moment
    test byte [si+DN_T_FLAGS], DN_FIN
    jnz .fin
    call dn_tcp_data
    jmp short .out
.syn:
    ; **A SYN WITH ACK IS OUR OWN ANSWER** (SPEC.md 96.26.8) and not a client
    ; opening a connection. The two are told apart by that one bit and by the
    ; flow's state, and both tests are needed: a plain SYN on a SYNSENT flow
    ; would be a simultaneous open, which nothing here does, and a SYN|ACK on
    ; a flow we opened OUTBOUND is a client that is confused.
    test byte [si+DN_T_FLAGS], DN_ACK
    jz .clientsyn
    call dn_flow
    jc .clientsyn
    cmp byte [bx+DN_F_ST], DN_S_SYNSENT
    jne .out
    call dn_synack_in
    jmp short .out
.clientsyn:
    call dn_tcp_open
    jmp short .out
.fin:
    call dn_tcp_fin
    jmp short .out
.reset:
    call dn_flow
    jc .out
    call dn_kill
    jmp short .out
; --- a segment for a flow we have not got -----------------------------------
; **A BARE ACK IS DROPPED AND NOT RESET**, which is not politeness: dn_kill
; frees a flow the moment its FIN goes out - there is no TIME_WAIT here and
; there is nowhere to keep one - so the client's own final ACK ALWAYS arrives
; after we have forgotten the connection. Answering it with a reset made every
; successful transfer end in an RST, which a client is entitled to report as
; an error on a transfer that in fact completed. Anything that asks for
; something - data, a FIN - still gets the reset, because that is a client
; retrying into silence and it should stop.
.unknown:
    test byte [si+DN_T_FLAGS], DN_FIN
    jnz .rst
    call dn_paylen                  ; ...which answers in AX
    or ax, ax
    jnz .rst
    jmp short .out
.rst:
    call dn_tcp_rst
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_tcp_open - a SYN: open the socket BY NAME (SPEC.md 96.26.2)
;
; The client is connecting to an address we invented in a DNS answer, so the
; last octet indexes straight back into the name table and NETV_OPEN gets the
; NAME. An address we never handed out is refused with RST: there is nothing
; to look up.
;
; NO SYN|ACK IS SENT HERE. NETV_OPEN is non-blocking and the far side has its
; own handshake to do, so the answer waits for NSK_UP in dn_pump. The client
; retransmits its SYN meanwhile, and dn_flow finding the flow is what makes
; that harmless.
; -----------------------------------------------------------------------------
dn_tcp_open:
    push si
    push di
    call dn_flow
    jnc .done                       ; a RETRANSMITTED SYN must not open a
                                    ; second socket
    call dn_flow_new
    jc .rst
    ; --- WHICH ADDRESS IS IT? (SPEC.md 96.26.5) ---------------------------
    ; One of ours out of the DNS pool, in which case the octet indexes the
    ; name; or a LITERAL the client was given directly, in which case there is
    ; nothing to look up and the dotted quad is itself the thing to open -
    ; NETV_OPEN takes either ("a dotted quad connects at once; a NAME waits",
    ; ethsock.inc). Without this arm a program handed an IP rather than a
    ; hostname could not connect at all, which is most test setups and plenty
    ; of real ones.
    push si
    add si, DN_IP_DST
    mov di, dn_qname
    call dn_ipstr                   ; the literal, as text, always - it costs
    pop si                          ; ~40 cycles and it is what the pool arm
                                    ; below overwrites if it applies
    mov di, dn_qname
    mov al, [si+DN_IP_DST+0]        ; is it 10.88.0.x at all?
    cmp al, 10
    jne .lit
    mov al, [si+DN_IP_DST+1]
    cmp al, 88
    jne .lit
    mov al, [si+DN_IP_DST+3]        ; ...then the pool octet indexes the names
    sub al, DN_POOL0
    jb .lit
    cmp al, DN_NNAME
    jae .lit
    mov ah, DN_N_SIZE
    mul ah
    add ax, dn_names
    mov di, ax
    cmp byte [di+DN_N_USED], 0
    je .rst                         ; ours to hand out, never handed out: a
    add di, DN_N_NAME               ; reset, because there IS nothing else it
    jmp short .have                 ; could have meant
.lit:
    mov di, dn_qname
.have:

    mov ax, [si+DN_T_SPORT]         ; --- the flow, before the open ---
    mov [bx+DN_F_LPORT], ax
    mov ax, [si+DN_T_DPORT]
    mov [bx+DN_F_RPORT], ax
    push si
    push di
    mov di, bx
    add di, DN_F_RIP
    add si, DN_IP_DST
    mov cx, 4
    call dn_copy
    pop di
    pop si
    push si
    push di
    mov di, bx
    add di, DN_F_LIP
    add si, DN_IP_SRC
    mov cx, 4
    call dn_copy
    pop di
    pop si
    push si                         ; their ISN + 1 is what we expect next
    add si, DN_T_SEQ
    call dn_get32
    pop si
    add ax, 1
    adc dx, 0
    push si
    mov si, bx
    add si, DN_F_RCV
    mov [si], ax
    mov [si+2], dx
    pop si
    push si                         ; OUR ISN is 0, deliberately: this is a
    mov si, bx                      ; local link with one peer, so the collide
    add si, DN_F_SND                ; case ISN randomisation exists for cannot
    xor ax, ax                      ; arise. Addressed through DN_F_SND rather
    mov [si], ax                    ; than as an offset from DN_F_RCV, which
    mov [si+2], ax                  ; was correct and would have broken in
    pop si                          ; silence the day the record changed

    push bx                         ; --- NETV_OPEN, with the NAME ---
    push si
    mov si, di                      ; DI is already the string: the name out
                                    ; of the table, or the literal in dn_qname
    mov cx, [bx+DN_F_RPORT]
    xchg cl, ch                     ; the verb wants a host-order port
    push ds
    pop es                          ; ES:SI is the CLAIM's - OSAPI_DRV_CALL is
    mov bh, [cs:net_cls]            ; an X stub and sets ES to the caller's DS
    mov bl, NETV_OPEN               ; anyway, so this only says so out loud
    call OSAPI_DRV_CALL
    pop si
    pop bx
    jc .rst
    mov [bx+DN_F_HAND], al
    mov byte [bx+DN_F_USED], 1
    mov byte [bx+DN_F_ST], DN_S_OPEN
.done:
    pop di
    pop si
    ret
.rst:
    mov byte [bx+DN_F_USED], 0
    pop di
    pop si
    call dn_tcp_rst
    ret

; -----------------------------------------------------------------------------
; dn_tcp_data - the client sent bytes: hand them to the socket and ACK
;
; IN ORDER ONLY. A segment whose sequence is not the one we expect is answered
; with a bare ACK naming what we DO expect, and dropped - the client then
; retransmits, which is a path it already has and which costs us no buffer.
; -----------------------------------------------------------------------------
dn_tcp_data:
    push ax
    push cx
    push dx
    push si
    push di
    call dn_paylen
    or ax, ax
    jz .ack                         ; a pure ACK: nothing to pass on
    mov cx, ax

    push cx                         ; is it the next one?
    push si
    add si, DN_T_SEQ
    call dn_get32
    pop si
    mov di, bx
    add di, DN_F_RCV
    cmp ax, [di]
    jne .oos
    cmp dx, [di+2]
    jne .oos
    pop cx

    push si                         ; --- NETV_SEND ---
    add si, DN_T_PAY
    push ds
    pop es
    mov al, [bx+DN_F_HAND]
    push bx
    mov bh, [cs:net_cls]
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    pop bx
    pop si
    jc .ack                         ; refused: ACK nothing and let it retry
    jcxz .ack                       ; took none - same
    mov ax, cx
    call dn_advr                    ; ...and only what was TAKEN is acknowledged
.ack:
    cmp word [dn_pend], 0
    jne .out                        ; something is already staged, and an ACK
                                    ; is cumulative: the next one carries this
    xor cx, cx
    mov al, DN_ACK
    call dn_seg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
.oos:
    pop cx
    jmp short .ack

; -----------------------------------------------------------------------------
; dn_tcp_fin - the client is done sending
; -----------------------------------------------------------------------------
dn_tcp_fin:
    push ax
    push cx
    mov ax, 1                       ; a FIN consumes one sequence number
    call dn_advr
    mov al, [bx+DN_F_HAND]
    push bx
    mov bh, [cs:net_cls]
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    pop bx
    cmp word [dn_pend], 0
    jne .st                         ; the state still moves: dn_pump owes the
                                    ; FIN and will send it when the buffer is
                                    ; free
    xor cx, cx
    mov al, DN_FIN | DN_ACK
    call dn_seg
    mov ax, 1
    call dn_adv
    mov byte [bx+DN_F_USED], 0      ; ...and it is gone. We do not wait for the
    pop cx                          ; client's ACK of our FIN: there is nothing
    pop ax                          ; left to retransmit and nothing to reuse
    ret                             ; the port for until it does
.st:
    mov byte [bx+DN_F_ST], DN_S_FIN
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_synack_in - the client has agreed to a connection WE opened (96.26.8)
; in:  BX = the flow (DN_S_SYNSENT), SI = the segment
;
; The mirror of dn_tcp_open's tail: bank its ISN + 1, acknowledge it, and the
; flow is up. From here nothing distinguishes the two directions - dn_tcp_data
; and dn_pump's `.up` arm carry bytes either way - which is the whole reason
; this wave is small.
; -----------------------------------------------------------------------------
dn_synack_in:
    push ax
    push cx
    push dx
    push si
    push di
    ; **THE STATE IS NOT ADVANCED UNTIL THE ACK IS STAGED**, which is the
    ; discipline this whole endpoint runs on (the note above dn_seg). Moving
    ; to DN_S_UP first and then losing the ACK to a busy [dn_pend] would leave
    ; the client retransmitting a SYN|ACK that `.syn` then drops for being on
    ; an UP flow - a connection up at both ends that never carries a byte. So
    ; a busy buffer means we do nothing and the client's own retransmit brings
    ; us back here.
    cmp word [dn_pend], 0
    jne .out
    add si, DN_T_SEQ                ; their ISN...
    call dn_get32                   ; -> DX:AX, and SI is untouched by it
    add ax, 1                       ; ...plus one, which is what a SYN consumes
    adc dx, 0
    mov di, bx
    add di, DN_F_RCV
    mov [di], ax
    mov [di+2], dx
    xor cx, cx                      ; the ACK that finishes the handshake...
    mov al, DN_ACK
    call dn_seg
    mov byte [bx+DN_F_ST], DN_S_UP  ; ...and only then is it up
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; --- dn_kill - the flow is over, with no ceremony ---------------------------
dn_kill:
    push ax
    push bx
    mov al, [bx+DN_F_HAND]
    push bx
    mov bh, [cs:net_cls]
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    pop bx
    mov byte [bx+DN_F_USED], 0
    pop bx
    pop ax
    ret

; dn_tcp_rst - "there is nothing listening here"
; in:  SI = the segment that provoked it
;
; The ACK number is the one field that matters: a client checks it before it
; believes an RST, which is what stops an off-path reset. Ours is in-path by
; construction, so this is simply the sequence after whatever it sent.
; -----------------------------------------------------------------------------
dn_tcp_rst:
    push ax
    push cx
    push dx
    push si
    push di
    mov ax, [si+DN_T_DPORT]         ; the ports, swapped
    mov [dn_frame+DN_T_SPORT], ax
    mov ax, [si+DN_T_SPORT]
    mov [dn_frame+DN_T_DPORT], ax
    mov word [dn_frame+DN_T_SEQ], 0 ; our sequence: nothing has been sent
    mov word [dn_frame+DN_T_SEQ+2], 0
    push si
    add si, DN_T_SEQ                ; ...and the ack is theirs plus one, which
    call dn_get32                   ; is what a SYN consumes
    add ax, 1
    adc dx, 0
    pop si
    mov di, dn_frame + DN_T_ACK
    call dn_put32
    mov byte [dn_frame+DN_T_OFF], 0x50      ; five 32-bit words, no options
    mov byte [dn_frame+DN_T_FLAGS], DN_RST | DN_ACK
    mov word [dn_frame+DN_T_WIN], 0
    mov word [dn_frame+DN_T_CK], 0
    mov word [dn_frame+DN_T_URG], 0
    mov cx, DN_T_HLEN
    call dn_tcp_ck
    mov cx, DN_T_HLEN
    mov al, DN_IPP_TCP
    call dn_ip_out
    pop di
    pop si
    pop cx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_tcp_ck - the TCP checksum, over the PSEUDO-HEADER and the segment
; in:  SI = the client's frame (for the addresses), CX = the TCP length
;
; The pseudo-header is source, destination, a zero, the protocol and the TCP
; length - and it is NOT on the wire, so it is assembled in a scratch block
; and summed with the segment. Two sums folded together is the same answer as
; one sum over a contiguous buffer, which is what lets the block stay separate
; from the frame.
; -----------------------------------------------------------------------------
dn_tcp_ck:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, cx                      ; the TCP length, banked

    mov di, dn_pseudo               ; source: the address we are SENDING from,
    mov si, dn_frame + DN_IP_SRC    ; which dn_ip_out has already written
    mov cx, 4
    call dn_copy
    mov di, dn_pseudo + 4
    mov si, dn_frame + DN_IP_DST
    mov cx, 4
    call dn_copy
    mov byte [dn_pseudo+8], 0
    mov byte [dn_pseudo+9], DN_IPP_TCP
    mov ax, bx
    xchg al, ah
    mov [dn_pseudo+10], ax

    mov si, dn_pseudo               ; --- sum one: the pseudo-header ---
    mov cx, 12
    call dn_cksum
    not ax                          ; dn_cksum complements, and a PARTIAL sum
    mov dx, ax                      ; must not be - so undo it and redo the
                                    ; complement once at the end
    mov si, dn_frame + DN_TCP       ; --- sum two: the segment ---
    mov cx, bx
    call dn_cksum
    not ax
    add ax, dx                      ; fold them, end-around
    adc ax, 0
    not ax
    xchg al, ah
    mov [dn_frame+DN_T_CK], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dn_pump - ask the sockets what has happened, and turn it into a frame
;
; **ONE FLOW PER CALL AT MOST**, because dn_frame is one buffer. The scan is
; round-robin from [dn_rr] rather than from zero, or a busy first flow would
; starve every other one - which on a two-connection program like FTP is the
; difference between a transfer and a hang.
;
; Every arm here checks nothing about [dn_pend]: dn_ready only calls this when
; it is zero, and that is the whole of the interlock.
; -----------------------------------------------------------------------------
dn_pump:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    ; **THE COUNTER IS BP AND NOT CL**, because CX is an OUTPUT of every verb
    ; here - NETV_STATUS answers the readable byte count in it. With the
    ; counter in CL, `dec cl` ran on whatever the driver had just returned:
    ; a 0 wrapped it to 255, so one pump made hundreds of far calls, a poll
    ; made ten pumps of them, and the tick handler took LONGER THAN A TICK.
    ; The machine then spent all its time in its own timer and the DOS
    ; program never ran again - which looked exactly like a hang in the
    ; program, and eth_ncall wrapping its 16 bits was the tell.
    ; OSAPI_DRV_CALL publishes "BP, DS and ES come back yours".
    call dn_accept                  ; **THE INBOUND PASS FIRST** (SPEC.md
                                    ; 96.26.8), because it is the one arm that
                                    ; can create a flow: doing it last would
                                    ; mean a connection accepted this tick
                                    ; waits a whole one before anything looks
                                    ; at it. It refuses itself when a frame is
                                    ; already staged, so it costs a listening
                                    ; box one compare and a box with no
                                    ; listener at all one more
    cmp word [dn_pend], 0
    jne .out                        ; ...and if it staged the SYN, that IS
                                    ; this pump's frame
    mov bp, DN_NFLOW
.l:
    mov al, [dn_rr]
    inc al
    cmp al, DN_NFLOW
    jb .keep
    xor al, al
.keep:
    mov [dn_rr], al
    mov ah, DN_F_SIZE
    mul ah
    add ax, dn_flows
    mov bx, ax
    cmp byte [bx+DN_F_USED], 0
    je .next
    cmp byte [bx+DN_F_ST], DN_S_OPEN
    je .connecting
    cmp byte [bx+DN_F_ST], DN_S_UP
    je .up
    cmp byte [bx+DN_F_ST], DN_S_FIN
    je .owedfin
                                    ; DN_S_SYNSENT falls through: what it is
                                    ; waiting for is the CLIENT, and nothing
                                    ; the sockets can say moves it. The one
                                    ; thing worth doing here would be a
                                    ; retransmit, and there is no timer to
                                    ; hang one on (96.26.8)
.next:
    dec bp
    jnz .l
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- still connecting: has it come up, or failed? --------------------------
.connecting:
    call dn_stat                    ; AH = an NSK_*, CX = readable
    jc .next
    cmp ah, NSK_UP
    je .estab
    cmp ah, NSK_FAILED
    je .failed
    cmp ah, NSK_CLOSING             ; up and closed before we looked, which a
    je .failed                      ; refused connection can arrive as
    jmp short .next
.estab:
    xor cx, cx                      ; **THE SYN|ACK, AT LAST.** The client has
    mov al, DN_SYN | DN_ACK         ; been retransmitting its SYN while the far
    call dn_seg                     ; side handshook, and this is the first
    mov ax, 1                       ; thing it has heard back
    call dn_adv                     ; a SYN consumes one sequence number
    mov byte [bx+DN_F_ST], DN_S_UP
    jmp .out
.failed:
    push bx                         ; a connection that never came up is an
    xor cx, cx                      ; RST the client understands - its own
    mov al, DN_RST | DN_ACK         ; error path then says so
    call dn_seg
    pop bx
    call dn_kill
    jmp short .out

; --- established: is there anything to deliver, or is it over? -------------
.up:
    call dn_stat
    jc .next
    or cx, cx
    jz .maybeover
    cmp cx, DN_WIN                  ; never take more than one segment's worth,
    jbe .take                       ; whatever is readable: the client's window
    mov cx, DN_WIN                  ; is what we advertised and we may not
.take:                              ; exceed it
    push cx
    mov di, dn_frame + DN_T_PAY
    push ds
    pop es
    mov al, [bx+DN_F_HAND]
    push bx
    mov bh, [cs:net_cls]
    mov bl, NETV_RECV               ; ES:DI is the CLAIM's, CX its capacity
    call OSAPI_DRV_CALL
    pop bx
    pop ax                          ; (the capacity we asked for, discarded)
    jc .next
    jcxz .maybeover                 ; 0 IS NOT END OF STREAM (netpkg.inc) - it
                                    ; is "nothing yet", and reading it as an
                                    ; end truncates every transfer that
                                    ; arrives in more than one piece
    push cx
    mov al, DN_ACK | DN_PSH
    call dn_seg
    pop ax
    call dn_adv                     ; our sequence by what we just sent
    jmp .out
.maybeover:
    cmp ah, NSK_CLOSING             ; readable is 0 AND the peer has closed:
    jne .next                       ; THAT is end of stream
    xor cx, cx
    mov al, DN_FIN | DN_ACK
    call dn_seg
    mov ax, 1
    call dn_adv
    call dn_kill
    jmp .out

; --- we owe a FIN that dn_tcp_fin could not stage ---------------------------
.owedfin:
    xor cx, cx
    mov al, DN_FIN | DN_ACK
    call dn_seg
    mov ax, 1
    call dn_adv
    call dn_kill
    jmp .out

; -----------------------------------------------------------------------------
; dn_stat - NETV_STATUS for the flow in BX
; out: CF=0 with AH = an NSK_* and CX = bytes readable now; CF=1 = refused
; -----------------------------------------------------------------------------
dn_stat:
    push bx
    mov al, [bx+DN_F_HAND]
    mov bh, [cs:net_cls]
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    pop bx
    ret
