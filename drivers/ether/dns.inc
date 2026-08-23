; =============================================================================
; dns.inc - the address of the machine, and the address of a name (SPEC.md 72.5)
;
; DHCP and DNS in one file because they are the same shape - a UDP exchange
; with a retry timer and no state above it - and because between them they are
; the whole of "what does a package have to be told before it can connect".
; The answer this driver gives is NOTHING: NETV_OPEN takes a name, and every
; number it needs to turn that into a connection comes off the wire.
;
; --- DHCP IS THE DEFAULT AND NOT THE ONLY WAY IN ----------------------------
; A static address is set in the Control Panel's Ethernet Setup window and
; remembered in ETHER.CFG (SPEC.md 72.7) - a file of the driver's own, because
; SPEC.md 51.9's settings blob is ONE 34 bytes for the whole machine and the
; hard-disk driver has it. `make ETHIP=...` still bakes one in for a machine
; being prepared before it is switched on.
;
; **NOTHING HERE PUMPS ITSELF.** dhcp_timer only runs from inside eth_pump,
; which only runs from inside a socket verb (SPEC.md 72.2.1), so the two
; places that BEGIN an exchange with no package polling - DRVV_READY, and a
; user pressing Renew or choosing Automatic - each spin the pump themselves
; through eth_dhcp_wait. Without it a switch to Automatic sent one DISCOVER
; and sat on `none` until some application happened to open a socket.
;
; --- RESOLUTION HAPPENS INSIDE NSK_CONNECT ----------------------------------
; netpkg.inc says a name is passed to NETV_OPEN and the lookup is part of
; connecting, which is what keeps NETV_RESOLVE reserved and unimplemented. So
; a socket handed a name sits in NSK_CONNECT with SKF_RES set, dns_timer picks
; it up, and tcp_syn runs when the answer arrives. **One lookup at a time**:
; a second socket waits, because two would need two source ports and a second
; timer to save a round trip nothing here is waiting on.
; =============================================================================

DHCP_CLIPORT equ 68
DHCP_SRVPORT equ 67
DNS_PORT     equ 53

; --- BOOTP's fixed part ------------------------------------------------------
BP_OP       equ 0
BP_HTYPE    equ 1
BP_HLEN     equ 2
BP_HOPS     equ 3
BP_XID      equ 4
BP_SECS     equ 8
BP_FLAGS    equ 10
BP_CIADDR   equ 12
BP_YIADDR   equ 16
BP_SIADDR   equ 20
BP_GIADDR   equ 24
BP_CHADDR   equ 28
BP_SNAME    equ 44
BP_FILE     equ 108
BP_MAGIC    equ 236
BP_OPTS     equ 240

DH_IDLE     equ 0
DH_DISCOVER equ 1
DH_REQUEST  equ 2
DH_BOUND    equ 3
DH_FAIL     equ 4

DHCP_TMO    equ 27              ; ticks between attempts - 1.5 s
DHCP_TRIES  equ 4

DNS_TMO     equ 36              ; 2 s
DNS_TRIES   equ 3

; -----------------------------------------------------------------------------
; ip_parse - is DS:SI a dotted quad? If so put it at DS:DI
; out: CF=0 = yes and the four bytes are stored; CF=1 = it is a name
; -----------------------------------------------------------------------------
ip_parse:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, 4                   ; four parts...
.part:
    xor bx, bx                  ; ...each 1..3 digits, 0..255
    xor ch, ch
.digit:
    mov al, [si]
    cmp al, '0'
    jb  .end
    cmp al, '9'
    ja  .end
    inc si
    inc ch
    cmp ch, 3
    ja  .no
    push dx
    mov dx, bx
    shl bx, 1
    shl bx, 1
    add bx, dx
    shl bx, 1                   ; bx = bx * 10, without touching AX
    pop dx
    sub al, '0'
    xor ah, ah
    add bx, ax
    cmp bx, 255
    ja  .no
    jmp short .digit
.end:
    or  ch, ch
    jz  .no                     ; no digits at all
    mov [di], bl
    inc di
    dec cl
    jz  .last
    cmp byte [si], '.'
    jne .no
    inc si
    jmp short .part
.last:
    cmp byte [si], 0            ; it must end here, or it is a name that
    jne .no                     ; happens to start like an address
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; DHCP
; =============================================================================

; -----------------------------------------------------------------------------
; dhcp_frame - the fixed part of a BOOTP request, in eth_txb's UDP payload
; out: DI = where the options go
; -----------------------------------------------------------------------------
dhcp_frame:
    push ax
    push cx
    push si
    mov di, eth_txb + IP_PAY + UD_HLEN
    mov cx, BP_OPTS
    call mem_zero
    mov di, eth_txb + IP_PAY + UD_HLEN
    mov byte [di+BP_OP], 1              ; BOOTREQUEST
    mov byte [di+BP_HTYPE], 1           ; Ethernet
    mov byte [di+BP_HLEN], 6
    mov si, dhcp_xid
    push di
    add di, BP_XID
    mov cx, 4
    call mem_copy
    pop di
    mov byte [di+BP_FLAGS], 0x80        ; BROADCAST, big-endian: we have no
    mov byte [di+BP_FLAGS+1], 0         ; address yet, so a unicast reply is one
                                        ; we could not receive
    mov si, eth_mac
    push di
    add di, BP_CHADDR
    mov cx, 6
    call mem_copy
    pop di
    mov byte [di+BP_MAGIC], 99          ; the magic cookie, 63 82 53 63
    mov byte [di+BP_MAGIC+1], 130
    mov byte [di+BP_MAGIC+2], 83
    mov byte [di+BP_MAGIC+3], 99
    add di, BP_OPTS
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dhcp_params - the parameter request list and the end marker at DS:DI
; out: DI advanced
; -----------------------------------------------------------------------------
dhcp_params:
    mov byte [di], 55                   ; what we want to be told
    mov byte [di+1], 3
    mov byte [di+2], 1                  ; the subnet mask
    mov byte [di+3], 3                  ; the router
    mov byte [di+4], 6                  ; the name servers
    add di, 5
    mov byte [di], 255                  ; END
    inc di
    ret

; -----------------------------------------------------------------------------
; dhcp_bcast - send the frame at eth_txb's UDP payload, DI bytes past its start
; -----------------------------------------------------------------------------
dhcp_bcast:                     ; STKDEPTH-NOSAVE: ax bx
    push cx                     ; AX and BX are given back by udp_send, the
    push dx                     ; only thing below here; checked by
    push si                     ; `stkdepth.py --check` (SPEC.md 72.16.4)
    push bp
    mov cx, di
    sub cx, eth_txb + IP_PAY + UD_HLEN
    cmp cx, BP_OPTS + 4                 ; a BOOTP frame under 300 bytes is
    jae .len                            ; legal and some servers drop it, so
    mov cx, BP_OPTS + 4                 ; the zeroed tail is the pad
.len:
    mov si, ip_bcast
    mov dx, DHCP_SRVPORT
    mov bp, DHCP_CLIPORT
    call udp_send
    pop bp
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; dhcp_start - begin, or begin again
; -----------------------------------------------------------------------------
; **dhcp_begin STARTS AND dhcp_start RETRIES**, and the difference is one
; byte: the retry counter. It was one routine, so every retry reset the count
; it was being measured against and DISCOVER went out for ever - which looked
; like the deadline arithmetic being wrong and was not.
dhcp_begin:
    mov byte [dhcp_try], 0
    ; ...and fall through
dhcp_start:
    push ax
    push cx
    push di
    call OSAPI_RAND                     ; a fresh transaction: a reply to the
    mov [dhcp_xid], ax                  ; PREVIOUS one must not be mistaken for
    call OSAPI_RAND                     ; an answer to this
    mov [dhcp_xid+2], ax
    call dhcp_frame
    mov byte [di], 53                   ; message type: DISCOVER
    mov byte [di+1], 1
    mov byte [di+2], 1
    add di, 3
    call dhcp_params
    call dhcp_bcast
    mov byte [dhcp_st], DH_DISCOVER
    call OSAPI_GET_TICKS
    add ax, DHCP_TMO
    mov [dhcp_tmo], ax
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dhcp_request - we have an offer: ask for it by name
; -----------------------------------------------------------------------------
dhcp_request:
    push ax
    push cx
    push si
    push di
    call dhcp_frame
    mov byte [di], 53
    mov byte [di+1], 1
    mov byte [di+2], 3                  ; REQUEST
    add di, 3
    mov byte [di], 50                   ; the address we were offered
    mov byte [di+1], 4
    add di, 2
    mov si, dhcp_off
    mov cx, 4
    call mem_copy
    add di, cx                          ; **mem_copy PRESERVES DI**, so the
                                        ; caller owns the advance. Without
                                        ; these two the next option is written
                                        ; OVER the address and the one after it
                                        ; over that: the server saw a REQUEST
                                        ; asking for `36 04 37 03` and answered
                                        ; NAK, for ever
    mov byte [di], 54                   ; ...and whose offer it was, because a
    mov byte [di+1], 4                  ; network with two servers offers twice
    add di, 2
    mov si, dhcp_srv
    mov cx, 4
    call mem_copy
    add di, cx
    call dhcp_params
    call dhcp_bcast
    mov byte [dhcp_st], DH_REQUEST
    call OSAPI_GET_TICKS
    add ax, DHCP_TMO
    mov [dhcp_tmo], ax
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dhcp_opt - find option AL in the reply in eth_rxb
; in:  AL = the code, CX = the UDP payload length
; out: CF=0, SI = its data, CL = its length; CF=1 = it is not there
;
; **BOUNDED BY THE FRAME AND NOT BY THE END MARKER**, because the end marker
; is in the packet and the packet came off a network. A walker that trusts it
; runs off the end of the buffer on a truncated or hostile reply.
; -----------------------------------------------------------------------------
dhcp_opt:
    push ax
    push bx
    push dx
    mov dl, al
    mov si, eth_rxb + IP_PAY + UD_HLEN
    mov bx, si
    add bx, cx                          ; BX = one past the payload
    add si, BP_OPTS
.l:
    cmp si, bx
    jae .no
    mov al, [si]
    cmp al, 255
    je  .no
    cmp al, 0                           ; PAD: one byte, no length
    je  .pad
    inc si
    cmp si, bx
    jae .no
    mov ah, [si]                        ; the length
    inc si
    cmp al, dl
    je  .hit
    mov al, ah                          ; not this one: step over its data
    xor ah, ah
    add si, ax
    jmp short .l
.pad:
    inc si
    jmp short .l
.hit:
    mov cl, ah
    xor ch, ch
    mov ax, si                          ; the data must fit the frame too
    add ax, cx
    cmp ax, bx
    ja  .no
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dhcp_in - a datagram arrived on port 68; CX = the UDP payload length
; -----------------------------------------------------------------------------
dhcp_in:
    push ax
    push bx
    push cx
    push si
    push di
    cmp cx, BP_OPTS + 1
    jb  .out
    mov si, eth_rxb + IP_PAY + UD_HLEN
    cmp byte [si+BP_OP], 2              ; BOOTREPLY
    jne .out
    mov di, dhcp_xid                    ; ...to OUR transaction
    push si
    add si, BP_XID
    mov bx, 4
.xid:
    mov al, [si]
    cmp al, [di]
    jne .xno
    inc si
    inc di
    dec bx
    jnz .xid
    pop si
    jmp short .xok
.xno:
    pop si
    jmp .out
.xok:
    cmp byte [si+BP_MAGIC], 99
    jne .out
    mov cx, [eth_udplen]
    mov al, 53
    call dhcp_opt
    jc  .out
    mov al, [si]
    cmp al, 2
    je  .offer
    cmp al, 5
    je  .ack
    cmp al, 6
    je  .nak
    jmp .out
.nak:
    mov byte [dhcp_st], DH_IDLE         ; start over rather than sit refused
    call dhcp_start
    jmp .out
.offer:
    cmp byte [dhcp_st], DH_DISCOVER
    jne .out
    mov si, eth_rxb + IP_PAY + UD_HLEN + BP_YIADDR
    mov di, dhcp_off
    mov cx, 4
    call mem_copy
    mov cx, [eth_udplen]
    mov al, 54
    call dhcp_opt
    jc  .srvsi
    cmp cl, 4
    jne .srvsi
    mov di, dhcp_srv
    mov cx, 4
    call mem_copy
    jmp short .req
.srvsi:
    mov si, eth_rxb + IP_PAY + UD_HLEN + BP_SIADDR
    mov di, dhcp_srv
    mov cx, 4
    call mem_copy
.req:
    call dhcp_request
    jmp short .out
.ack:
    cmp byte [dhcp_st], DH_REQUEST
    jne .out
    mov si, eth_rxb + IP_PAY + UD_HLEN + BP_YIADDR
    mov di, eth_ip
    mov cx, 4
    call mem_copy
    mov cx, [eth_udplen]
    mov al, 1                           ; the subnet mask
    call dhcp_opt
    jc  .nomask
    cmp cl, 4
    jne .nomask
    mov di, eth_mask
    mov cx, 4
    call mem_copy
.nomask:
    mov cx, [eth_udplen]
    mov al, 3                           ; the router - the FIRST of however
    call dhcp_opt                       ; many it names
    jc  .nogw
    cmp cl, 4
    jb  .nogw
    mov di, eth_gw
    mov cx, 4
    call mem_copy
.nogw:
    mov cx, [eth_udplen]
    mov al, 6                           ; ...and the first name server
    call dhcp_opt
    jc  .nodns
    cmp cl, 4
    jb  .nodns
    mov di, eth_dns
    mov cx, 4
    call mem_copy
.nodns:
    mov byte [dhcp_st], DH_BOUND
    mov word [dhcp_tmo], 0
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dhcp_timer
; -----------------------------------------------------------------------------
dhcp_timer:
    push ax                     ; **THE FULL SET, and it is not this routine's
    push bx                     ; own need.** It clobbers only AX; the four
    push dx                     ; below are saved so that ETH_PUMP_I - which
    push si                     ; calls it - can stop saving them (SPEC.md
    push di                     ; 72.16.4). The saving is the same work either
                                ; way and this is where it is CHEAP: eth_pump_i
                                ; holds its set across a 108-byte subtree,
                                ; where these four sit twenty bytes into the
                                ; chain. Pushing a save DOWN to a shallow
                                ; callee is free depth
    cmp byte [dhcp_st], DH_DISCOVER
    je  .live
    cmp byte [dhcp_st], DH_REQUEST
    jne .out
.live:
    call OSAPI_GET_TICKS
    sub ax, [dhcp_tmo]
    js  .out
    inc byte [dhcp_try]
    mov al, [dhcp_try]
    cmp al, DHCP_TRIES
    jb  .again
    mov byte [dhcp_st], DH_FAIL         ; the page says so, and `make ETHIP=`
    jmp short .out                      ; is the answer on a LAN with no server
.again:
    cmp byte [dhcp_st], DH_REQUEST
    je  .rereq
    call dhcp_start
    jmp short .keep
.rereq:
    call dhcp_request
.keep:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; DNS
; =============================================================================

; -----------------------------------------------------------------------------
; dns_qname - the name at DS:SI into DNS label form at DS:DI
; out: DI advanced past the terminating zero; CF=1 = it will not encode
; -----------------------------------------------------------------------------
dns_qname:
    push ax
    push bx
    push cx
    push si
    push di
.label:
    mov bx, di                  ; the length byte goes here, once we know it
    inc di
    xor cl, cl
.ch:
    mov al, [si]
    or  al, al
    jz  .end
    cmp al, '.'
    je  .dot
    inc si
    mov [di], al
    inc di
    inc cl
    cmp cl, 63
    ja  .no
    jmp short .ch
.dot:
    inc si
    or  cl, cl
    jz  .no                     ; an empty label: `www..com`
    mov [bx], cl
    jmp short .label
.end:
    or  cl, cl
    jz  .no
    mov [bx], cl
    mov byte [di], 0
    inc di
    mov ax, di                  ; the caller's DI is what advances
    pop di
    mov di, ax
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dns_skip - step DS:SI past one name in the reply
; in:  SI = the name, BX = one past the message
; out: SI advanced; CF=1 = it ran off the end
;
; A COMPRESSION POINTER ENDS THE NAME, and it is never followed here: every
; name this parser skips is one it does not need to read, and following a
; pointer is how a hostile reply makes a parser loop forever.
; -----------------------------------------------------------------------------
dns_skip:
    push ax
.l:
    cmp si, bx
    jae .no
    mov al, [si]
    or  al, al
    jz  .zero
    test al, 0xC0
    jnz .ptr
    xor ah, ah
    inc si
    add si, ax
    jmp short .l
.ptr:
    add si, 2
    cmp si, bx
    ja  .no
    clc
    pop ax
    ret
.zero:
    inc si
    clc
    pop ax
    ret
.no:
    stc
    pop ax
    ret

; -----------------------------------------------------------------------------
; dns_send - ask for the name in socket [dns_sk]'s SKO_HOST
; -----------------------------------------------------------------------------
dns_send:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov al, [dns_sk]
    call sk_ptr
    mov di, eth_txb + IP_PAY + UD_HLEN
    mov ax, [dns_id]
    call be_put                         ; the transaction
    mov word [di+2], 0x0001             ; flags: RECURSION DESIRED, and it is
                                        ; 0x0100 on the wire - a plain word
                                        ; store of a big-endian constant is
                                        ; the one place this file swaps by hand
    mov byte [di+4], 0
    mov byte [di+5], 1                  ; one question
    mov word [di+6], 0
    mov word [di+8], 0
    mov word [di+10], 0
    add di, 12
    mov si, bx
    add si, SKO_HOST
    call dns_qname
    jc  .no
    mov byte [di], 0                    ; QTYPE = A
    mov byte [di+1], 1
    mov byte [di+2], 0                  ; QCLASS = IN
    mov byte [di+3], 1
    add di, 4
    mov cx, di
    sub cx, eth_txb + IP_PAY + UD_HLEN
    mov si, eth_dns
    mov dx, DNS_PORT
    mov bp, [dns_lport]
    call udp_send
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dns_in - a reply on our source port; CX = the UDP payload length
; -----------------------------------------------------------------------------
dns_in:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [dns_sk], 0xFF
    je  .out
    cmp cx, 12
    jb  .out
    mov si, eth_rxb + IP_OFF + IPO_SRC  ; from the SERVER we asked, and from
    mov di, eth_dns                     ; the port servers answer on: udp_in
    call ip_eq                          ; dispatched this on the destination
    jne .out                            ; port alone, and the transaction ID
    mov si, eth_rxb + IP_PAY + UDO_SP   ; below is 16 bits over a source port
    call be_get                         ; that walks upward from a known base
    cmp ax, DNS_PORT
    jne .out
    mov si, eth_rxb + IP_PAY + UD_HLEN
    mov bx, si
    add bx, cx                          ; BX = one past the message, and every
                                        ; step below is bounded by it
    call be_get
    cmp ax, [dns_id]
    jne .out
    test byte [si+2], 0x80              ; it must be a response
    jz  .out
    mov al, [si+3]
    and al, 0x0F
    jnz .fail                           ; NXDOMAIN and friends: say no now
    mov dl, [si+7]                      ; the answer count, low byte - a reply
    or  dl, dl                          ; with more than 255 answers is not one
    jz  .fail                           ; we need
    mov cx, 0
    mov cl, [si+5]                      ; the question count
    add si, 12
.q:
    or  cl, cl
    jz  .ans
    call dns_skip
    jc  .out
    add si, 4                           ; QTYPE and QCLASS
    cmp si, bx
    ja  .out
    dec cl
    jmp short .q
.ans:
    or  dl, dl
    jz  .fail
    call dns_skip
    jc  .out
    mov ax, si
    add ax, 10
    cmp ax, bx
    ja  .out
    mov al, [si]                        ; TYPE, big-endian
    or  al, al
    jnz .next
    cmp byte [si+1], 1                  ; ...A?
    jne .next
    cmp byte [si+2], 0                  ; CLASS IN?
    jne .next
    cmp byte [si+3], 1
    jne .next
    mov al, [si+8]                      ; RDLENGTH
    or  al, al
    jnz .next
    cmp byte [si+9], 4
    jne .next
    add si, 10
    mov ax, si
    add ax, 4
    cmp ax, bx
    ja  .out
    mov al, [dns_sk]                    ; ...and there it is
    call sk_ptr
    mov di, bx
    add di, SKO_RIP
    mov cx, 4
    call mem_copy
    and byte [bx+SKO_FL], ~SKF_RES
    call tcp_syn
    mov byte [dns_sk], 0xFF
    mov word [dns_lport], 0
    jmp short .out
.next:
    mov ah, [si+8]                      ; RDLENGTH, big-endian, into a step
    mov al, [si+9]
    mov cx, bx                          ; ...bounded by what is LEFT and not by
    sub cx, si                          ; where the step lands: the add is
    sub cx, 10                          ; modulo 65536, and wire data that
    cmp ax, cx                          ; wraps SI puts it BELOW the message,
    ja  .fail                           ; where every test of where it landed
    add si, 10                          ; passes and the walk is over this
    add si, ax                          ; driver's own image
    cmp si, bx
    jae .fail
    dec dl
    jmp .ans
.fail:
    call dns_fail
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dns_fail - the name did not resolve: the socket says so
; -----------------------------------------------------------------------------
dns_fail:
    push ax
    push bx
    cmp byte [dns_sk], 0xFF
    je  .out
    mov al, [dns_sk]
    call sk_ptr
    and byte [bx+SKO_FL], ~SKF_RES
    mov al, NSK_FAILED
    call tcp_abort
    mov byte [eth_lasterr], NETE_REFUSED
    mov byte [dns_sk], 0xFF
    mov word [dns_lport], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dns_timer - start a lookup, or give up on one
; -----------------------------------------------------------------------------
dns_timer:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                     ; DI is not this routine's need either - it is
                                ; saved so eth_pump_i can stop saving it, for
                                ; dhcp_timer's reason above (SPEC.md 72.16.4)
    cmp byte [dns_sk], 0xFF
    jne .live

    xor dl, dl                          ; nothing in flight: is anything owed?
    mov cx, NET_SOCKS
.scan:
    mov al, dl
    call sk_ptr
    cmp byte [bx+SKO_ST], NSK_FREE      ; a slot that has been FREED owes
    je  .nextscan                       ; nothing, whatever SKF_RES still says
    test byte [bx+SKO_FL], SKF_RES
    jnz .go
.nextscan:
    inc dl
    loop .scan
    jmp short .out
.go:
    mov si, eth_dns                     ; ...and can we even ask? A machine
    call ip_zero                        ; with no name server has to say no
    jz  .nodns                          ; rather than time out four times
    mov [dns_sk], dl
    call OSAPI_RAND
    mov [dns_id], ax
    call tcp_lport                      ; a source port out of the same pool
    mov [dns_lport], ax
    mov byte [dns_try], 0
    call dns_send
    jc  .bad
    call OSAPI_GET_TICKS
    add ax, DNS_TMO
    mov [dns_tmo], ax
    jmp short .out
.bad:
    call dns_fail                       ; the name will not even encode
    jmp short .out
.nodns:
    mov [dns_sk], dl
    call dns_fail
    jmp short .out
.live:
    call OSAPI_GET_TICKS
    sub ax, [dns_tmo]
    js  .out
    inc byte [dns_try]
    mov al, [dns_try]
    cmp al, DNS_TRIES
    jae .give
    call dns_send
    call OSAPI_GET_TICKS
    add ax, DNS_TMO
    mov [dns_tmo], ax
    jmp short .out
.give:
    call dns_fail
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
