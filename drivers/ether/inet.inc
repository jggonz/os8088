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
; opposite of the machine's. So no header field is USED as it is loaded: a
; word is loaded and turned round with `xchg ah, al` before it means anything,
; turned round again before it is stored, or compared in place against a
; constant BE16() has turned round at assembly time. (They were be_get and
; be_put, a byte at a time behind a call, until driver size pass 1: every
; caller was a `mov` away from the word itself.) A plain word access to a
; header field, used as it stands, assembles, runs, and is wrong by a factor
; of 256 - which for a length field means a packet that is either enormous or
; empty and for a port means a socket nobody can find.
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
; ...and the same two as a LITTLE-ENDIAN word load of the wire bytes sees them,
; so a field can be compared in place rather than fetched through be_get
%define BE16(x) ((((x) >> 8) & 0xFF) | (((x) & 0xFF) << 8))
ETY_IP_LE   equ BE16(ETY_IP)
ETY_ARP_LE  equ BE16(ETY_ARP)

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
; mem_copy - CX bytes from DS:SI to DS:DI
;
; `rep movsb` UNDER A BORROWED ES. movsb writes through ES and this driver is
; called with ES meaning three different things: KERNEL_SEG from a service
; call, the CALLER's segment from the package door (SPEC.md 20.11), and
; whatever the last kernel API left - so ES is set to DS for the one
; instruction and given back. It used to be a byte loop for that reason, at
; ~51 cycles a byte where the string move is 17, and five bytes longer.
;
; mem_cp4 is the commonest length as an ENTRY: it leaves CX = 4 on the way
; out exactly as `mov cx, 4 / call mem_copy` did, so every call site that
; reads CX afterwards reads the same thing. It costs the caller nothing - the
; same `mov` runs, in here rather than there - and saves three bytes a site.
; -----------------------------------------------------------------------------
mem_cp6:                        ; ...and six, a MAC, for the cold callers
    mov cx, 6                   ; only: the jump is ~15 cycles, which is why
    jmp short mem_copy          ; ip_start's two per frame keep the long form
mem_cp4:
    mov cx, 4
mem_copy:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; ep_* - THE SHARED EPILOGUE LADDER. A routine that saved some tail of
; AX BX CX DX SI DI BP, in that order, ends with `jmp ep_<the last one it
; pushed>` instead of its own run of pops and a `ret`: three bytes where it
; was up to eight. Neither `pop` nor `ret` writes a flag, so a CF answer set
; before the jump arrives intact.
;
; **COLD ROUTINES ONLY.** The jump is ~15 cycles on an 8088, so nothing on
; the per-frame or per-segment path takes it - the Control Panel page, the
; Setup window, DHCP, DNS, ARP and attach/detach do. A stack walker follows
; the jump (tools/stkbalance.py, tools/stkdepth.py), so the routine is still
; checked as balanced at its own `ret`.
; -----------------------------------------------------------------------------
ep_bp:
    pop bp
ep_di:
    pop di
ep_si:
    pop si
ep_dx:
    pop dx
ep_cx:
    pop cx
    pop bx                      ; (no ep_bx / ep_ax: nothing here saves so few,
    pop ax                      ; and a label nothing jumps to reads to a stack
    ret                         ; walker as an entry that returns unbalanced)

; -----------------------------------------------------------------------------
; mem_zero - CX bytes at DS:DI (the same borrowed ES)
; -----------------------------------------------------------------------------
mem_zero:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor al, al
    cld
    rep stosb
    pop es
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
    push ax                     ; TWO WORD COMPARES. An address is four bytes
    mov ax, [si]                ; and a byte loop spent ~200 cycles saying so
    cmp ax, [di]                ; on every frame eth_frame checks and every
    jne .out                    ; segment tcp_match walks
    mov ax, [si+2]
    cmp ax, [di+2]
.out:
    pop ax                      ; ...and `pop` writes no flags
    ret

; -----------------------------------------------------------------------------
; ip_zero - is the address at DS:SI all zeroes (i.e. do we have one)?
; out: ZF=1 = yes
; -----------------------------------------------------------------------------
ip_zero:
    push ax
    mov ax, [si]
    or  ax, [si+2]
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
%if ARP_N & (ARP_N - 1)
  %error "arp_put's round-robin wraps with an AND: ARP_N must be a power of two"
%endif

; -----------------------------------------------------------------------------
; arp_find - the hardware address for the IP at DS:SI
; out: CF=0 and DI = a 6-byte MAC in the cache; CF=1 = not known
; -----------------------------------------------------------------------------
arp_find:
    push bx
    push cx
    push di                     ; the caller's, given back on a miss
    xor bx, bx                  ; BX = the entry, DI = its address: ip_eq
    mov cx, ARP_N               ; keeps SI and DI, so DI walks the table
    mov di, arp_ip              ; rather than being rebuilt from BX each turn
.e:                             ; (driver size pass 1 - ip_start's path)
    cmp byte [arp_used+bx], 0
    je  .next
    call ip_eq
    je  .hit
.next:
    add di, 4
    inc bx
    loop .e
    pop di
    stc
    jmp short .out
.hit:
    add sp, 2                   ; ...and on a hit the answer replaces it:
    shl bx, 1                   ; DI = arp_ip + 4 * entry, so + 2 * entry and
    lea di, [bx+di+(arp_ha-arp_ip)] ; the gap is arp_ha + 6 * entry
    clc
.out:
    pop cx
    pop bx
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
    call arp_find               ; already known? then just refresh it (BX kept)
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
    and dl, ARP_N - 1           ; (ARP_N is a power of two)
    mov [arp_rr], dl
.got:
    mov byte [arp_used+di], 1
    mov dx, di                  ; DX = the entry number, kept across the copy
    shl di, 1
    shl di, 1
    add di, arp_ip
    call mem_cp4                ; the address into the slot (DI kept)...
    shl dx, 1                   ; ...and DI = the MAC slot: arp_ip + 4 * entry,
    add di, dx                  ; + 2 * entry, + the gap is arp_ha + 6 * entry
    add di, arp_ha - arp_ip
.store:
    mov si, bx                  ; DI is the MAC slot either way
    call mem_cp6
    jmp ep_di

; -----------------------------------------------------------------------------
; arp_req - ask who has the IP at DS:SI
; -----------------------------------------------------------------------------
arp_req:
    push ax
    push cx
    push si
    push di
    push si
    mov si, eth_bcmac           ; broadcast
    mov di, eth_txb + EH_DST
    call mem_cp6
    mov di, eth_txb + EH_SIZE + AR_THA
    mov cx, 6                   ; the address we are asking for
    call mem_zero
    pop si
    mov di, eth_txb + EH_SIZE + AR_TPA
    call mem_cp4
    mov al, ARP_REQ
    call arp_send
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; arp_send - send the ARP frame in eth_txb as operation AL (ARP_REQ/ARP_REPLY)
; in:  the caller has written the three fields that differ - the Ethernet
;      destination, THA and TPA. Everything else is written here, and it is
;      the same for a request and a reply: our MAC twice, the type, the fixed
;      header, the operation and our address.
; clobbers: CX, SI, DI
; -----------------------------------------------------------------------------
arp_send:
    mov si, eth_mac
    mov di, eth_txb + EH_SRC
    call mem_cp6
    mov di, eth_txb + EH_SIZE + AR_SHA
    call mem_cp6                ; (mem_copy preserves SI)
    mov si, arp_hdr
    mov di, eth_txb + EH_TYPE
    mov cx, arp_hdr_end - arp_hdr
    call mem_copy
    mov [eth_txb+EH_SIZE+AR_OPER+1], al ; the operation's low byte; the high
                                        ; one is the template's zero
    mov si, eth_ip
    mov di, eth_txb + EH_SIZE + AR_SPA
    call mem_cp4
    mov cx, EH_SIZE + AR_SIZE
    jmp nic_tx

; the wire bytes from EH_TYPE to AR_OPER's high byte, big-endian as sent:
; ARP over Ethernet, hardware type 1 (Ethernet), protocol IP, six and four
arp_hdr:    db 0x08, 0x06, 0x00, 0x01, 0x08, 0x00, 6, 4, 0
arp_hdr_end:
%if ETY_ARP != 0x0806 || ETY_IP != 0x0800 || ARP_REQ > 255 || ARP_REPLY > 255
  %error "arp_hdr spells the ethertypes and the operation by hand"
%endif

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
    cmp word [eth_rxb+EH_SIZE+AR_PTYPE], ETY_IP_LE  ; the wire's 08 00
    jne .out
    cmp word [eth_rxb+EH_SIZE+AR_HLEN], 4 << 8 | 6  ; HLEN 6 and PLEN 4
    jne .out

    mov si, eth_rxb + EH_SIZE + AR_SPA      ; cache the sender either way
    mov bx, eth_rxb + EH_SIZE + AR_SHA
    call arp_put

    cmp word [eth_rxb+EH_SIZE+AR_OPER], ARP_REQ << 8 ; 00 01: a REQUEST
    jne .out
    mov si, eth_rxb + EH_SIZE + AR_TPA      ; ...is it us being asked about?
    mov di, eth_ip
    call ip_eq
    jne .out
    mov si, di                              ; (ip_eq keeps DI = eth_ip)
    call ip_zero
    jz  .out                                ; we have no address to claim yet

    ; --- the reply, built from the request -----------------------------------
    mov si, eth_rxb + EH_SRC
    mov di, eth_txb + EH_DST
    call mem_cp6
    mov si, eth_rxb + EH_SIZE + AR_SHA      ; their address becomes the target
    mov di, eth_txb + EH_SIZE + AR_THA
    mov cx, 10                              ; SHA and SPA in one copy
    call mem_copy
    mov al, ARP_REPLY
    call arp_send
.out:
    pop di
    pop si
    jmp ep_cx

; =============================================================================
; IP
; =============================================================================

; -----------------------------------------------------------------------------
; ip_route - which machine do we hand a packet for DS:SI to?
; out: SI = the address to ARP for: the destination itself if it is on our own
;      subnet, the gateway if it is not
; -----------------------------------------------------------------------------
ip_route:
    push ax                     ; a WORD at a time: an address is never
    mov ax, [si]                ; arithmetic here (see the header), and any
    xor ax, [eth_ip]            ; bit that differs under the mask is the same
    and ax, [eth_mask]          ; answer whichever byte it is in (driver size
    jnz .gw                     ; pass 1: the byte loop was 32 bytes and a
    mov ax, [si+2]              ; turn of `loop` per byte, on every send)
    xor ax, [eth_ip+2]
    and ax, [eth_mask+2]
    jz  .out
.gw:
    mov si, eth_gw
.out:
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
    mov [eth_pr], al                    ; AL is the protocol to the end: no
    mov di, eth_dip                     ; callee below writes it. Bank the real
    call mem_cp4                        ; destination - the route below may hand
                                        ; us the gateway's - and SI is still the
                                        ; caller's copy of it (mem_cp4 keeps SI)
    mov cx, [si]                        ; A BROADCAST HAS NO ARP, and DHCP is
    and cx, [si+2]                      ; sent before we have an address at all
    inc cx                              ; - so a stack that tried to resolve it
    jnz .unicast                        ; could never acquire one. All four
                                        ; bytes 0xFF is the two words ANDed
                                        ; being 0xFFFF, which `inc` makes zero
    mov di, eth_bcmac
    jmp short .have
.unicast:
    call ip_route
    call arp_find
    jnc .have
    call arp_req
    stc
    jmp short .pop
.have:
    mov si, di                          ; DI = the MAC in the cache
    mov di, eth_txb + EH_DST
    mov cx, 6
    call mem_copy                       ; (CX and DI kept: the source address
    mov si, eth_mac                     ; is the next six bytes)
    add di, cx
    call mem_copy
    mov word [eth_txb+EH_TYPE], ETY_IP_LE

    ; --- the IP header, a WORD a field-pair (driver size pass 1) -------------
    ; LEN is ip_finish's, and so is CK: it zeroes the field before it sums the
    ; header, and nothing between here and there reads it - the transport
    ; checksums cover the pseudo-header, never this one - so it is not zeroed
    ; here as well
    mov di, eth_txb + IP_OFF
    mov word [di+IPO_VIHL], 0x45        ; VIHL 45h, and TOS 0 in the high byte
    mov word [di+IPO_FRAG], 0
    mov ah, al                          ; TTL 64, and the protocol after it
    mov al, 64
    mov [di+IPO_TTL], ax
    mov ax, [eth_ipid]
    inc word [eth_ipid]
    xchg ah, al                         ; network order
    mov [di+IPO_ID], ax
%if IPO_TOS != IPO_VIHL + 1 || IPO_PROTO != IPO_TTL + 1
  %error "ip_start writes VIHL/TOS and TTL/PROTO as one word each"
%endif
    mov si, eth_ip
    mov di, eth_txb + IP_OFF + IPO_SRC
    call mem_cp4                        ; CX = 4 and DI kept: the destination
    mov si, eth_dip                     ; follows the source...
    add di, cx
    call mem_cp4
    add di, cx                          ; ...and the payload follows that
%if IPO_DST != IPO_SRC + 4 || IP_PAY != IP_OFF + IPO_DST + 4
  %error "ip_start steps DI from IPO_SRC to IPO_DST to IP_PAY by four"
%endif
    clc
.pop:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ip_finish - CX bytes of payload are in place: length, checksum, send
; out: CF from nic_tx
; -----------------------------------------------------------------------------
ip_finish:
    push ax
    push cx
    push si
    mov ax, cx
    add ax, IP_HLEN
    xchg ah, al                         ; network order
    mov [eth_txb+IP_OFF+IPO_LEN], ax
    mov word [eth_txb+IP_OFF+IPO_CK], 0
    mov si, eth_txb + IP_OFF
    push cx
    mov cx, IP_HLEN
    call ip_cksum
    pop cx
    mov [eth_txb+IP_OFF+IPO_CK], ax
    add cx, EH_SIZE + IP_HLEN
    call nic_tx                         ; (which keeps DI, as this does)
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
; **SUMMED IN REGISTERS** (driver size pass 1). It was two ip_ck_part calls,
; the second over four bytes staged in eth_ph - the zero, the protocol and the
; length. They are six words, so they are six `adc`s: the same words in the
; same order of a one's complement sum, and the end-around carry makes the
; result a function of their total alone (0 only if every word is 0, else
; the one value in 1..FFFFh congruent to it mod FFFFh), so one chain with one
; fold gives exactly what two chains with two folds did.
ip_pseudo:
    push ax
    push cx
    mov bx, [eth_txb+IP_OFF+IPO_SRC]    ; source and destination...
    add bx, [eth_txb+IP_OFF+IPO_SRC+2]
    adc bx, [eth_txb+IP_OFF+IPO_DST]
    adc bx, [eth_txb+IP_OFF+IPO_DST+2]
    mov ah, [eth_pr]                    ; ...the zero byte and the protocol, as
    mov al, 0                           ; the wire has them (`mov`, not `xor`:
    adc bx, ax                          ; CF is the sum's)...
    xchg cl, ch                         ; ...and the length, network order
    adc bx, cx
    adc bx, 0
    adc bx, 0
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
    cmp cx, NIC_FRAME - IP_PAY
    ja  .out
    mov si, eth_rxb + IP_OFF + IPO_SRC
    mov al, IPP_ICMP
    call ip_start                       ; (CX kept)
    jc  .out                            ; ARP is on its way; they will ping again
    mov si, eth_rxb + IP_PAY
    call mem_copy                       ; the whole message back, data and all -
    mov byte [di+ICO_TYPE], ICMP_REPLY  ; and DI is still eth_txb + IP_PAY
    mov word [di+ICO_CK], 0
    mov si, di
    call ip_cksum                       ; (CX kept)
    mov [di+ICO_CK], ax
    call ip_finish
    inc word [eth_nping]
.out:
    pop di
    pop si
    jmp ep_cx

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
    mov ax, [eth_rxb+IP_PAY+UDO_LEN]    ; **THE UDP LENGTH, NOT THE IP ONE.**
    xchg ah, al                         ; The two agree on a well-formed
    cmp ax, UD_HLEN                     ; datagram and one of them came off a
    jb  .out                            ; network, so the shorter is the one to
    cmp ax, cx                          ; walk options with (dhcp_opt)
    ja  .out
    sub ax, UD_HLEN
    mov [eth_udplen], ax
    mov dx, ax
    mov ax, [eth_rxb+IP_PAY+UDO_DP]
    xchg ah, al
    mov cx, dx
    cmp ax, DHCP_CLIPORT
    je  .dhcp
    cmp ax, [dns_lport]
    je  .dns
%ifndef ETH_NORAW
    or  ax, ax                  ; a PACKAGE's port (SPEC.md 72.23): queued
    jz  .out                    ; for NETV_UDPRX
    cmp ax, [eth_uport]
    jne .out
    call udp_queue
%endif
    jmp short .out
.dns:
    call dns_in
    jmp short .out
.dhcp:
    call dhcp_in
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
    call ip_start                       ; (CX kept)
    jc  .out
    mov ax, bp                          ; DI = eth_txb + IP_PAY: the header,
    xchg ah, al                         ; each word in network order
    mov [di+UDO_SP], ax                 ; source port
    mov ax, dx
    xchg ah, al
    mov [di+UDO_DP], ax                 ; destination
    add cx, UD_HLEN                     ; CX = the datagram from here on
    mov ax, cx
    xchg ah, al
    mov [di+UDO_LEN], ax                ; length
    mov word [di+UDO_CK], 0

    call ip_pseudo                      ; the checksum, over the pseudo-header
    mov si, di                          ; and the datagram
    call ip_ck_part
    mov ax, bx
    not ax
    or  ax, ax                          ; a UDP checksum of zero means "not
    jnz .ck                             ; computed", so the all-ones form is
    dec ax                              ; the one to send
.ck:
    mov [di+UDO_CK], ax
    call ip_finish
    clc
.out:
    pop di
    pop si
    jmp ep_cx

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
    mov ax, [eth_rxb+EH_TYPE]           ; compared as the wire has it
    cmp ax, ETY_ARP_LE
    je  .arp
    cmp ax, ETY_IP_LE
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
    mov ax, [eth_rxb+IP_OFF+IPO_FRAG]   ; a FRAGMENT is refused rather than
                                        ; reassembled: nothing this stack sends
    and ax, BE16(0x3FFF)                ; can provoke one, and a browser that
    jnz .drop                           ; met one would need a buffer it has not
    mov si, eth_rxb + IP_OFF + IPO_DST  ; got. Is it for us?
    mov di, eth_ip
    call ip_eq
    je  .mine
    mov ax, [si]                        ; ...or a broadcast, which is what a
    and ax, [si+2]                      ; DHCP offer arrives as before we have
    inc ax                              ; an address at all (SI is still the
    jnz .drop                           ; destination: ip_eq preserves it)
.mine:
    mov ax, [eth_rxb+IP_OFF+IPO_LEN]    ; the wire length, which is what the
    xchg ah, al                         ; payload length must come from: the
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
    jne .drop
    call tcp_in                         ; (TCP falls through: one taken branch
    jmp short .out                      ; fewer on the busiest path)
.icmp:
    call icmp_in
    jmp short .out
.udp:
    call udp_in
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
    push cx                     ; **ONLY CX AND BP.** CX is nic_rx's output and
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
    cmp byte [eth_raw], 0       ; **THE RAW CONSUMER IS THE STACK NOW**
    jne .out                    ; (SPEC.md 72.22). Not the drain and not the
                                ; timers: a DHCP renew mid-bracket would put a
                                ; frame on a wire somebody else believes they
                                ; own, and a retransmit would do it for a
                                ; connection nothing is left to read
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
; out: CF=1 = the ring was empty. CX is nic_rx's length and eth_frame's input,
;      so it flows between them and is the caller's to save.
;
; Split out of the loop above so that a caller can put the CARD MUTEX round one
; frame instead of round a whole drain (SPEC.md 72.19). A drain of eight frames
; is up to eighty milliseconds of byte-at-a-time DMA, and every package verb
; that lands in that window gets NETE_BUSY; one frame is ten.
; -----------------------------------------------------------------------------
eth_pump1:
    call nic_rx
    jc  .none                   ; (CF = 1 already: nic_rx's empty)
    inc word [eth_nrx]
    call eth_frame
    clc
.none:
    ret

; -----------------------------------------------------------------------------
; eth_ptimers - the retransmit, DNS and DHCP clocks. Cheap when idle; each one
;               is a compare against [ticks] on nearly every call.
; -----------------------------------------------------------------------------
eth_ptimers:
    call tcp_timers
    call dns_timer
    jmp dhcp_timer              ; (its `ret` is ours)
