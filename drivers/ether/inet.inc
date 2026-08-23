; =============================================================================
; inet.inc - Ethernet framing, ARP, IP, ICMP and UDP (SPEC.md 72.3)
;
; Everything below the sockets. One receive path, one send path, and a frame
; buffer at each end that is written in place rather than assembled out of
; pieces - on a 4.77MHz 8088 a copy is the expensive part of a packet and the
; only way to have none is to build where the card will read.
;
; --- BIG-ENDIAN IS THE WIRE'S AND LITTLE-ENDIAN IS OURS ----------------------
; Every multi-byte field in every header here is network order, which is the
; opposite of the machine's. There are no `mov ax, [bx+IPO_LEN]` reads in this
; file for that reason: a word is fetched a byte at a time into AH then AL, or
; stored the same way, and the two helpers that do it are be_get/be_put. A
; plain word access to a header field assembles, runs, and is wrong by a
; factor of 256 - which for a length field means a packet that is either
; enormous or empty and for a port means a socket nobody can find.
;
; **The one deliberate exception is the CHECKSUM**, which is computed with
; ordinary word loads. The one's complement sum of a byte-swapped stream is
; the byte swap of the sum, so storing the swapped result into a header field
; with a swapped store puts the right bytes on the wire - and it saves 1,500
; byte swaps per frame on a machine that has none to spare. IP addresses are
; the same: they are never arithmetic here, only copied and compared, so they
; stay four bytes in wire order from end to end.
;
; --- ARP IS A CACHE AND A REFUSAL, NEVER A WAIT ------------------------------
; A send whose destination is not in the cache issues a request and answers
; CF=1. Nothing waits for a reply: TCP retransmits on its own timer and every
; other sender here is a retry loop already. That is what keeps the whole
; stack callable from a worker task with no lock held (SPEC.md 20.6 rule 7).
; =============================================================================

; --- the Ethernet header -----------------------------------------------------
EH_DST      equ 0
EH_SRC      equ 6
EH_TYPE     equ 12
EH_SIZE     equ 14
ETY_IP      equ 0x0800
ETY_ARP     equ 0x0806

; --- ARP ---------------------------------------------------------------------
AR_HTYPE    equ 0
AR_PTYPE    equ 2
AR_HLEN     equ 4
AR_PLEN     equ 5
AR_OPER     equ 6
AR_SHA      equ 8
AR_SPA      equ 14
AR_THA      equ 18
AR_TPA      equ 24
AR_SIZE     equ 28
ARP_REQ     equ 1
ARP_REPLY   equ 2

; --- IP ----------------------------------------------------------------------
IPO_VIHL    equ 0
IPO_TOS     equ 1
IPO_LEN     equ 2
IPO_ID      equ 4
IPO_FRAG    equ 6
IPO_TTL     equ 8
IPO_PROTO   equ 9
IPO_CK      equ 10
IPO_SRC     equ 12
IPO_DST     equ 16
IP_HLEN     equ 20
IPP_ICMP    equ 1
IPP_TCP     equ 6
IPP_UDP     equ 17
IP_OFF      equ EH_SIZE                 ; where an IP header starts in a frame
IP_PAY      equ EH_SIZE + IP_HLEN       ; ...and its payload

; --- ICMP --------------------------------------------------------------------
ICO_TYPE    equ 0
ICO_CODE    equ 1
ICO_CK      equ 2
ICMP_ECHO   equ 8
ICMP_REPLY  equ 0

; --- UDP ---------------------------------------------------------------------
UDO_SP      equ 0
UDO_DP      equ 2
UDO_LEN     equ 4
UDO_CK      equ 6
UD_HLEN     equ 8

; -----------------------------------------------------------------------------
; be_get / be_put - one network-order word
; be_get: in SI = where; out AX. be_put: in AX = the value, DI = where.
; clobbers: nothing else, not even flags for be_put
; -----------------------------------------------------------------------------
be_get:
    mov ah, [si]
    mov al, [si+1]
    ret

be_put:
    mov [di], ah
    mov [di+1], al
    ret

; -----------------------------------------------------------------------------
; mem_copy - CX bytes from DS:SI to DS:DI
;
; A LOOP AND NOT `rep movsb`, because movsb writes through ES and this driver
; is called with ES meaning three different things: KERNEL_SEG from a service
; call, the CALLER's segment from the package door (SPEC.md 20.11), and
; whatever the last kernel API left. Everything internal here addresses one
; segment and says so.
; -----------------------------------------------------------------------------
mem_copy:
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
; mem_zero - CX bytes at DS:DI
; -----------------------------------------------------------------------------
mem_zero:
    push ax
    push cx
    push di
    xor al, al
    jcxz .out
.l:
    mov [di], al
    inc di
    loop .l
.out:
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_ck_part - fold CX bytes at DS:SI into the running sum in BX
; out: BX
; clobbers: flags
;
; The end-around carry is what `adc` gives for free: neither `lodsw` nor
; `loop` touches the carry flag, so one `clc` before the loop and an `adc` per
; word is the whole one's complement sum. An odd trailing byte is the HIGH
; byte of the last word on the wire, which a little-endian load makes the low
; byte of the value - so `xor ah,ah / mov al,[si]` is exactly right, in the
; swapped arithmetic this file's header licenses.
; -----------------------------------------------------------------------------
ip_ck_part_i:
    push ax
    push cx
    push si
    shr cx, 1
    jnc .even
    push cx
    push si
    add si, cx                  ; si + 2*(cx) = the last byte
    add si, cx
    mov al, [si]
    xor ah, ah
    pop si
    pop cx
    add bx, ax
    adc bx, 0
.even:
    ; --- EIGHT WORDS AT A TIME (SPEC.md 72.16.6) ------------------------
    ; `lodsw` is 16 clocks plus the 8088's 4-clock word-access penalty, and
    ; `adc bx,ax` is 3 - so the body is 23 against `loop`'s SEVENTEEN. The
    ; bookkeeping was 42% of the tightest loop in the stack, and netbench
    ; priced the whole of it at 24,195 us/KB: the worst rate in its table by
    ; a factor of two, on the smallest byte count.
    ;
    ; **THE END-AROUND CARRY IS CF, SO NOTHING INSIDE THE SUM MAY TOUCH IT.**
    ; That is the entire shape of this routine and it is why the counting is
    ; ALL done before the `clc`: `and`, `shr` and `sub` clobber the carry,
    ; while `mov`, `xchg`, `jcxz` and `loop` - the only four used once the
    ; sum starts - do not. The first draft of this banked CF across the
    ; shifts with pushf/popf and banked the SHIFTED-OUT BIT instead, which
    ; is a checksum that is wrong about one packet in two and silent.
    push dx
    mov dx, cx
    and dx, 7                   ; DX = the leftover words...
    shr cx, 1
    shr cx, 1
    shr cx, 1                   ; ...and CX the whole eights
    xchg cx, dx                 ; CX = leftover, DX = eights
    clc                         ; THE SUM STARTS HERE
    jcxz .oct
.t:
    lodsw
    adc bx, ax
    loop .t
.oct:
    mov cx, dx
    jcxz .fold
.e:
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    lodsw
    adc bx, ax
    loop .e
.fold:
    adc bx, 0
    adc bx, 0
    pop dx
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_cksum - the complete checksum of CX bytes at DS:SI
; out: AX, ready to store with a plain word write (see the header)
; -----------------------------------------------------------------------------
ip_cksum:
    push bx
    xor bx, bx
    call ip_ck_part
    mov ax, bx
    not ax
    pop bx
    ret

; -----------------------------------------------------------------------------
; ip_eq - are the four bytes at DS:SI and DS:DI the same address?
; out: ZF=1 = yes. clobbers: flags
; -----------------------------------------------------------------------------
ip_eq:
    push ax
    push cx
    push si
    push di
    mov cx, 4
.l:
    mov al, [si]
    cmp al, [di]
    jne .no
    inc si
    inc di
    loop .l
    xor al, al                  ; ZF=1
    jmp short .out
.no:
    or  al, 1                   ; ZF=0, whatever AL was
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_zero - is the address at DS:SI all zeroes (i.e. do we have one)?
; out: ZF=1 = yes
; -----------------------------------------------------------------------------
ip_zero:
    push ax
    mov al, [si]
    or  al, [si+1]
    or  al, [si+2]
    or  al, [si+3]
    or  al, al
    pop ax
    ret

; =============================================================================
; ARP
; =============================================================================
ARP_N       equ 4               ; four entries: the gateway, the DNS server and
                                ; two more. A stack that only ever talks to a
                                ; router needs one, and a browser on a LAN that
                                ; talks to a local server needs two - past that
                                ; the round-robin victim below is as good as
                                ; anything and costs no code

; -----------------------------------------------------------------------------
; arp_find - the hardware address for the IP at DS:SI
; out: CF=0 and DI = a 6-byte MAC in the cache; CF=1 = not known
; -----------------------------------------------------------------------------
arp_find:
    push ax
    push bx
    push cx
    mov bx, 0
    mov cx, ARP_N
.e:
    mov al, [arp_used+bx]
    or  al, al
    jz  .next
    push si
    push di
    mov di, arp_ip
    push bx
    shl bx, 1
    shl bx, 1
    add di, bx
    pop bx
    call ip_eq
    pop di
    pop si
    je  .hit
.next:
    inc bx
    loop .e
    stc
    jmp short .out
.hit:
    mov di, bx                  ; DI = entry * 6 + arp_ha
    shl di, 1
    add di, bx
    shl di, 1
    add di, arp_ha
    clc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; arp_put - remember that the IP at DS:SI is at the MAC in DS:BX
; -----------------------------------------------------------------------------
arp_put:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call arp_find               ; already known? then just refresh it
    pop bx
    jnc .store
    xor di, di                  ; a free slot...
    mov cx, ARP_N
.free:
    cmp byte [arp_used+di], 0
    je  .got
    inc di
    loop .free
    mov dl, [arp_rr]            ; ...or the round-robin victim
    xor dh, dh
    mov di, dx
    inc dl
    cmp dl, ARP_N
    jb  .rr
    xor dl, dl
.rr:
    mov [arp_rr], dl
.got:
    mov byte [arp_used+di], 1
    mov dx, di                  ; DX = the entry number, kept across the copy
    shl di, 1
    shl di, 1
    add di, arp_ip
    push cx
    mov cx, 4
    call mem_copy               ; the address into the slot
    pop cx
    mov di, dx                  ; ...and now DI = the MAC slot, entry * 6
    shl di, 1
    add di, dx
    shl di, 1
    add di, arp_ha
.store:
    mov si, bx                  ; DI is the MAC slot either way
    mov cx, 6
    call mem_copy
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; arp_req - ask who has the IP at DS:SI
; -----------------------------------------------------------------------------
arp_req:
    push ax
    push cx
    push si
    push di
    push si
    mov di, eth_txb             ; broadcast
    mov cx, 6
    mov al, 0xFF
.bc:
    mov [di], al
    inc di
    loop .bc
    mov si, eth_mac
    mov cx, 6
    call mem_copy
    mov di, eth_txb + EH_TYPE
    mov ax, ETY_ARP
    call be_put

    mov di, eth_txb + EH_SIZE
    mov ax, 0x0001
    call be_put                 ; hardware type: Ethernet
    add di, 2
    mov ax, ETY_IP
    call be_put                 ; protocol type
    mov byte [eth_txb+EH_SIZE+AR_HLEN], 6
    mov byte [eth_txb+EH_SIZE+AR_PLEN], 4
    mov di, eth_txb + EH_SIZE + AR_OPER
    mov ax, ARP_REQ
    call be_put
    mov si, eth_mac
    mov di, eth_txb + EH_SIZE + AR_SHA
    mov cx, 6
    call mem_copy
    mov si, eth_ip
    mov di, eth_txb + EH_SIZE + AR_SPA
    mov cx, 4
    call mem_copy
    mov di, eth_txb + EH_SIZE + AR_THA
    mov cx, 6
    call mem_zero
    pop si
    mov di, eth_txb + EH_SIZE + AR_TPA
    mov cx, 4
    call mem_copy
    mov cx, EH_SIZE + AR_SIZE
    call ne_tx
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; arp_in - an ARP frame is in eth_rxb
;
; A REQUEST for us is answered; a REPLY is cached. And the SENDER of a request
; is cached too, which is not politeness: a machine that has just asked for us
; is a machine we are about to answer and then talk to, and caching it there
; saves the request we would otherwise send back at it.
; -----------------------------------------------------------------------------
arp_in:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, eth_rxb + EH_SIZE + AR_PTYPE
    call be_get
    cmp ax, ETY_IP
    jne .out
    cmp byte [eth_rxb+EH_SIZE+AR_HLEN], 6
    jne .out
    cmp byte [eth_rxb+EH_SIZE+AR_PLEN], 4
    jne .out

    mov si, eth_rxb + EH_SIZE + AR_SPA      ; cache the sender either way
    mov bx, eth_rxb + EH_SIZE + AR_SHA
    call arp_put

    mov si, eth_rxb + EH_SIZE + AR_OPER
    call be_get
    cmp ax, ARP_REQ
    jne .out
    mov si, eth_rxb + EH_SIZE + AR_TPA      ; ...is it us being asked about?
    mov di, eth_ip
    call ip_eq
    jne .out
    mov si, eth_ip
    call ip_zero
    jz  .out                                ; we have no address to claim yet

    ; --- the reply, built from the request -----------------------------------
    mov si, eth_rxb + EH_SRC
    mov di, eth_txb + EH_DST
    mov cx, 6
    call mem_copy
    mov si, eth_mac
    mov di, eth_txb + EH_SRC
    mov cx, 6
    call mem_copy
    mov di, eth_txb + EH_TYPE
    mov ax, ETY_ARP
    call be_put
    mov si, eth_rxb + EH_SIZE
    mov di, eth_txb + EH_SIZE
    mov cx, AR_SIZE
    call mem_copy
    mov di, eth_txb + EH_SIZE + AR_OPER
    mov ax, ARP_REPLY
    call be_put
    mov si, eth_rxb + EH_SIZE + AR_SHA      ; their address becomes the target
    mov di, eth_txb + EH_SIZE + AR_THA
    mov cx, 10                              ; SHA and SPA in one copy
    call mem_copy
    mov si, eth_mac
    mov di, eth_txb + EH_SIZE + AR_SHA
    mov cx, 6
    call mem_copy
    mov si, eth_ip
    mov di, eth_txb + EH_SIZE + AR_SPA
    mov cx, 4
    call mem_copy
    mov cx, EH_SIZE + AR_SIZE
    call ne_tx
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; IP
; =============================================================================

; -----------------------------------------------------------------------------
; ip_route - which machine do we hand a packet for DS:SI to?
; out: SI = the address to ARP for: the destination itself if it is on our own
;      subnet, the gateway if it is not
; -----------------------------------------------------------------------------
ip_route:
    push ax
    push bx
    push cx
    mov cx, 4
    xor bx, bx
.l:
    mov al, [si+bx]
    xor al, [eth_ip+bx]
    and al, [eth_mask+bx]
    jnz .gw
    inc bx
    loop .l
    jmp short .out
.gw:
    mov si, eth_gw
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_start - begin a frame for protocol AL to the address at DS:SI
; out: CF=0 and DI = eth_txb + IP_PAY, ready for the payload;
;      CF=1 = the hardware address is not known yet and a request has gone out
; clobbers: flags, DI
;
; **CF=1 IS NOT A FAILURE.** It is the ordinary first attempt at a new
; destination and the caller's answer is to try again on its next tick - which
; every caller in this driver already does, because a network is a thing that
; loses packets whether or not the ARP cache is warm.
; -----------------------------------------------------------------------------
ip_start:                       ; STKDEPTH-NOSAVE: bx
    push ax                     ; BX is not saved - see eth_pump_i
    push cx
    push si
    mov [eth_pr], al
    mov di, eth_dip                     ; bank the real destination: the route
    mov cx, 4                           ; below may hand us the gateway's
    call mem_copy
    mov si, eth_dip
    cmp byte [si], 0xFF                 ; A BROADCAST HAS NO ARP, and DHCP is
    jne .unicast                        ; sent before we have an address at all
    cmp byte [si+1], 0xFF               ; - so a stack that tried to resolve it
    jne .unicast                        ; could never acquire one
    cmp byte [si+2], 0xFF
    jne .unicast
    cmp byte [si+3], 0xFF
    jne .unicast
    mov di, eth_bcmac
    jmp short .have
.unicast:
    call ip_route
    call arp_find
    jnc .have
    call arp_req
    pop si
    pop cx
    pop ax
    stc
    ret
.have:
    mov si, di                          ; DI = the MAC in the cache
    mov di, eth_txb + EH_DST
    mov cx, 6
    call mem_copy
    mov si, eth_mac
    mov di, eth_txb + EH_SRC
    mov cx, 6
    call mem_copy
    mov di, eth_txb + EH_TYPE
    mov ax, ETY_IP
    call be_put

    mov di, eth_txb + IP_OFF
    mov byte [di+IPO_VIHL], 0x45
    mov byte [di+IPO_TOS], 0
    mov byte [di+IPO_TTL], 64
    mov al, [eth_pr]
    mov [di+IPO_PROTO], al
    mov word [di+IPO_FRAG], 0
    mov word [di+IPO_CK], 0
    mov ax, [eth_ipid]
    inc word [eth_ipid]
    push di
    add di, IPO_ID
    call be_put
    pop di
    mov si, eth_ip
    mov di, eth_txb + IP_OFF + IPO_SRC
    mov cx, 4
    call mem_copy
    mov si, eth_dip
    mov di, eth_txb + IP_OFF + IPO_DST
    mov cx, 4
    call mem_copy
    mov di, eth_txb + IP_PAY
    pop si
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; ip_finish - CX bytes of payload are in place: length, checksum, send
; out: CF from ne_tx
; -----------------------------------------------------------------------------
ip_finish:
    push ax
    push cx
    push si
    push di
    mov ax, cx
    add ax, IP_HLEN
    mov di, eth_txb + IP_OFF + IPO_LEN
    call be_put
    mov word [eth_txb+IP_OFF+IPO_CK], 0
    mov si, eth_txb + IP_OFF
    push cx
    mov cx, IP_HLEN
    call ip_cksum
    pop cx
    mov [eth_txb+IP_OFF+IPO_CK], ax
    add cx, EH_SIZE + IP_HLEN
    call ne_tx
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_pseudo - the TCP/UDP pseudo-header sum for the frame ip_start just built
; in:  CX = the transport length (header + data)
; out: BX = a running sum with the pseudo-header in it
;
; It is source, destination, a zero, the protocol and the length - never
; transmitted, only counted, which is what makes a checksum notice a packet
; delivered to the wrong machine.
; -----------------------------------------------------------------------------
ip_pseudo:
    push ax
    push cx
    push si
    xor bx, bx
    mov si, eth_txb + IP_OFF + IPO_SRC
    push cx
    mov cx, 8                           ; source and destination together
    call ip_ck_part
    pop cx
    mov al, [eth_pr]                    ; then the zero byte, the protocol and
    mov byte [eth_ph], 0                ; the length - staged in OUR OWN bss
    mov [eth_ph+1], al                  ; and not on the stack, because SS is
    mov [eth_ph+2], ch                  ; the kernel's task slice and every
    mov [eth_ph+3], cl                  ; address in this file is a DS one
    mov si, eth_ph
    mov cx, 4
    call ip_ck_part
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; ICMP - echo reply and nothing else
;
; A machine that answers a ping is a machine somebody can prove is on the
; network without any of the software above this line working, which is why
; this is here and why it is the first end-to-end test in SPEC.md 72.
; =============================================================================
icmp_in:
    push ax
    push bx
    push cx
    push si
    push di
    cmp byte [eth_rxb+IP_PAY+ICO_TYPE], ICMP_ECHO
    jne .out
    cmp cx, 4                           ; CX = the IP payload length
    jb  .out
    cmp cx, NE_FRAME - IP_PAY
    ja  .out
    mov si, eth_rxb + IP_OFF + IPO_SRC
    mov al, IPP_ICMP
    push cx
    call ip_start
    pop cx
    jc  .out                            ; ARP is on its way; they will ping again
    mov si, eth_rxb + IP_PAY
    call mem_copy                       ; the whole message back, data and all
    mov di, eth_txb + IP_PAY
    mov byte [di+ICO_TYPE], ICMP_REPLY
    mov word [di+ICO_CK], 0
    mov si, di
    push cx
    call ip_cksum
    pop cx
    mov [eth_txb+IP_PAY+ICO_CK], ax
    call ip_finish
    inc word [eth_nping]
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; UDP - two ports, and both of them are ours
; =============================================================================
; in: CX = the IP payload length, the frame in eth_rxb
udp_in:
    push ax
    push cx
    push dx
    push si
    cmp cx, UD_HLEN
    jb  .out
    mov si, eth_rxb + IP_PAY + UDO_LEN  ; **THE UDP LENGTH, NOT THE IP ONE.**
    call be_get                         ; The two agree on a well-formed
    cmp ax, UD_HLEN                     ; datagram and one of them came off a
    jb  .out                            ; network, so the shorter is the one to
    cmp ax, cx                          ; walk options with (dhcp_opt)
    ja  .out
    sub ax, UD_HLEN
    mov [eth_udplen], ax
    mov dx, ax
    mov si, eth_rxb + IP_PAY + UDO_DP
    call be_get
    mov cx, dx
    cmp ax, DHCP_CLIPORT
    je  .dhcp
    cmp ax, [dns_lport]
    je  .dns
    jmp short .out
.dhcp:
    call dhcp_in
    jmp short .out
.dns:
    call dns_in
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; udp_send - CX bytes at eth_txb+IP_PAY+UD_HLEN to [DS:SI]:DX from port BP
; in:  SI = destination IP, DX = destination port, BP = source port,
;      CX = payload length
; out: CF=1 = it did not go (no ARP yet)
;
; The payload must already be in place, because ip_start does not move it: the
; frame is built around it.
; -----------------------------------------------------------------------------
udp_send:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, IPP_UDP
    push cx
    call ip_start
    pop cx
    jc  .out
    mov di, eth_txb + IP_PAY
    mov ax, bp
    call be_put                         ; source port
    add di, 2
    mov ax, dx
    call be_put                         ; destination
    add di, 2
    mov ax, cx
    add ax, UD_HLEN
    call be_put                         ; length
    mov word [eth_txb+IP_PAY+UDO_CK], 0

    push cx                             ; the checksum, over the pseudo-header
    add cx, UD_HLEN                     ; and the datagram
    call ip_pseudo
    mov si, eth_txb + IP_PAY
    call ip_ck_part
    mov ax, bx
    not ax
    pop cx
    or  ax, ax                          ; a UDP checksum of zero means "not
    jnz .ck                             ; computed", so the all-ones form is
    mov ax, 0xFFFF                      ; the one to send
.ck:
    mov [eth_txb+IP_PAY+UDO_CK], ax
    add cx, UD_HLEN
    call ip_finish
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE RECEIVE PATH
; =============================================================================

; -----------------------------------------------------------------------------
; eth_frame - one frame is in eth_rxb, CX bytes of it
; -----------------------------------------------------------------------------
eth_frame_i:                    ; STKDEPTH-NOSAVE: bx
    push ax                     ; BX is not saved - see eth_pump_i above
    push cx
    push si
    push di
    cmp cx, EH_SIZE
    jbe .drop
    mov si, eth_rxb + EH_TYPE
    call be_get
    cmp ax, ETY_ARP
    je  .arp
    cmp ax, ETY_IP
    jne .drop

    ; --- IP ------------------------------------------------------------------
    cmp cx, EH_SIZE + IP_HLEN
    jb  .drop
    cmp byte [eth_rxb+IP_OFF+IPO_VIHL], 0x45
    jne .drop                           ; v4 with no options, or not ours to
                                        ; understand. Options are legal and
                                        ; vanishingly rare, and a stack that
                                        ; guessed at the header length would be
                                        ; reading the transport header at the
                                        ; wrong offset when it got it wrong
    mov si, eth_rxb + IP_OFF
    push cx
    mov cx, IP_HLEN
    call ip_cksum
    pop cx
    or  ax, ax                          ; a correct header sums to 0xFFFF, so
    jnz .drop                           ; its complement is zero
    mov si, eth_rxb + IP_OFF + IPO_FRAG ; a FRAGMENT is refused rather than
    call be_get                         ; reassembled: nothing this stack sends
    and ax, 0x3FFF                      ; can provoke one, and a browser that
    jnz .drop                           ; met one would need a buffer it has not
    mov si, eth_rxb + IP_OFF + IPO_DST  ; got. Is it for us?
    mov di, eth_ip
    call ip_eq
    je  .mine
    mov si, eth_rxb + IP_OFF + IPO_DST  ; ...or a broadcast, which is what a
    cmp byte [si], 0xFF                 ; DHCP offer arrives as before we have
    jne .drop                           ; an address at all
    cmp byte [si+1], 0xFF
    jne .drop
    cmp byte [si+2], 0xFF
    jne .drop
    cmp byte [si+3], 0xFF
    jne .drop
.mine:
    mov si, eth_rxb + IP_OFF + IPO_LEN  ; the wire length, which is what the
    call be_get                         ; payload length must come from: the
    sub cx, EH_SIZE                     ; card's frame is padded to 60 bytes
    cmp ax, cx                          ; and every short packet is padded. The
    ja  .drop                           ; datagram's room is the frame WITHOUT
    cmp ax, IP_HLEN                     ; the Ethernet header, and a length
    jb  .drop                           ; below the header itself wraps the
    sub ax, IP_HLEN                     ; subtract into ~65,500 - which becomes
    mov cx, ax                          ; the payload length tcp_in is given
    mov al, [eth_rxb+IP_OFF+IPO_PROTO]
    cmp al, IPP_ICMP
    je  .icmp
    cmp al, IPP_UDP
    je  .udp
    cmp al, IPP_TCP
    je  .tcp
    jmp short .drop
.icmp:
    call icmp_in
    jmp short .out
.udp:
    call udp_in
    jmp short .out
.tcp:
    call tcp_in
    jmp short .out
.arp:
    call arp_in
    jmp short .out
.drop:
    inc word [eth_ndrop]
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_pump - drain the ring and run the timers
;
; **THE FRAME BUDGET IS THE POINT.** This is called from inside socket verbs a
; worker task issues once a tick, with the gfx lock NOT held - but the UI task
; is waiting on that worker's turn all the same, and a busy segment of network
; can hand us frames faster than a 4.77MHz machine can process them for as
; long as it likes. Eight frames is about 12KB, which is more than one tick of
; anything this stack talks to and is a bound the desktop can feel the end of.
; -----------------------------------------------------------------------------
ETH_BUDGET  equ 8

eth_pump_i:                     ; STKDEPTH-NOSAVE: ax bx dx si di
    push cx                     ; **ONLY CX AND BP.** CX is ne_rx's output and
    push bp                     ; is live across `call eth_frame`; BP is the
                                ; budget. The other five are given back by
                                ; every callee - dhcp_timer and dns_timer save
                                ; them for exactly this reason - and
                                ; `stkdepth.py --check` fails the build the day
                                ; one stops. TEN BYTES off the deepest chain in
                                ; the driver, and the routine still preserves
                                ; everything it always did (SPEC.md 72.16.4)
    cmp byte [eth_up], 0
    je  .out
    mov bp, ETH_BUDGET
.f:
    call eth_pump1
    jc  .timers
    dec bp
    jnz .f
.timers:
    call eth_ptimers
.out:
    pop bp
    pop cx
    ret

; -----------------------------------------------------------------------------
; eth_pump1 - ONE frame off the ring, if there is one
; out: CF=1 = the ring was empty. CX is ne_rx's length and eth_frame's input,
;      so it flows between them and is the caller's to save.
;
; Split out of the loop above so that a caller can put the CARD MUTEX round one
; frame instead of round a whole drain (SPEC.md 72.19). A drain of eight frames
; is up to eighty milliseconds of byte-at-a-time DMA, and every package verb
; that lands in that window gets NETE_BUSY; one frame is ten.
; -----------------------------------------------------------------------------
eth_pump1:
    call ne_rx
    jc  .none
    inc word [eth_nrx]
    call eth_frame
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; eth_ptimers - the retransmit, DNS and DHCP clocks. Cheap when idle; each one
;               is a compare against [ticks] on nearly every call.
; -----------------------------------------------------------------------------
eth_ptimers:
    call tcp_timers
    call dns_timer
    call dhcp_timer
    ret
