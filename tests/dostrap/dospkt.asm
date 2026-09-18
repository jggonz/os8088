; DOSPKT.COM - a Crynwr packet driver client, for os8088's own (SPEC.md 96.23).
; OURS, MIT with the rest of the tree.
;
; WHY THIS EXISTS. mTCP is the validation target for wave 4 and mTCP is the
; CLIENT half - GPL, not in this repository, and a program whose verdict we
; cannot read (SPEC.md 96.18.3 makes that argument at length about Creative's
; TEST-SBC). This asks the same questions a client asks and prints the answers
; where a test can assert on them.
;
; It runs unchanged under a real DOS with a real packet driver, which is the
; whole of docs/DOS-DEBUGGING.md's method: two sides, one binary, diff.
;
; WHAT IT DOES, in the order a client does it:
;   1. walk 60h..80h for `PKT DRVR` at offset 3
;   2. driver_info          - version, class, type, functionality
;   3. access_type          - a handle for ARP (0806), receiver registered
;   4. get_address          - our own station address
;   5. send_pkt             - a broadcast ARP request for the gateway, which
;                             is a frame a real host must answer
;   6. spin on the tick     - the reply arrives through the UP-CALL, which is
;                             the half no other row here can reach
;   7. a WHOLE TCP CONNECTION by hand - SYN, the SYN|ACK, the ACK, an HTTP
;                             request and the bytes that come back. That is
;                             what SPEC.md 96.26.3's endpoint is for, and the
;                             three answers it prints are the only way to tell
;                             "the translation opened a socket" from "the
;                             client ever heard about it"
;   8. release_type
;
; Every line is `NAME value`, which is tests/dosirq.py's parser.
    org 0x100
    cpu 8086

VEC_LO      equ 0x60
VEC_HI      equ 0x80
ETY_ARP     equ 0x0806
ETY_IP      equ 0x0800
RXMAX       equ 1514

; the gateway QEMU's slirp always puts at 10.0.2.2, and the address its DHCP
; hands our own stack. Both are facts about the harness (SPEC.md 72.9) and the
; program prints what it used, so a different net is a readable failure.
GW_IP       db 10, 0, 2, 2
MY_IP       db 10, 0, 2, 15

start:
    mov sp, stack_top
    cld                             ; **THE FRAME IS BUILT WITH rep stosb AND
                                    ; rep movsb**, and nothing promises a DOS
                                    ; program DF=0 on entry. Without this the
                                    ; frame is assembled BACKWARDS out of the
                                    ; buffer and what goes on the wire is
                                    ; whatever the image had there
    mov ax, 0x0003                  ; **CLEAR THE SCREEN FIRST.** The bracket
    int 0x10                        ; puts the adapter in FSXM_TEXT80 and does
                                    ; not blank it, so what a program writes
                                    ; lands in the middle of the desktop's own
                                    ; framebuffer read as characters - which is
                                    ; unreadable, and looks exactly like a
                                    ; program that crashed

    call find_driver
    jnc .found
    mov dx, s_nodrv
    call puts
    jmp done
.found:
    mov dx, s_vec
    call puts
    mov al, [pktvec]
    call puthex8
    call crlf

    call driver_info
    call do_access
    jnc .acc
    mov dx, s_noacc
    call puts
    jmp done
.acc:
    call get_address
    call send_arp
    call wait_rx
    call do_tcp                     ; ...AND A CONNECTION (SPEC.md 96.26.3)
    call do_stats                   ; ...and what the driver counted of it
    call do_listen                  ; ...AND THE OTHER DIRECTION (96.26.8):
                                    ; be the server, if anything connects
    call release

done:
    mov dx, s_ready
    call puts
    mov ah, 0                       ; **IT HOLDS THE SCREEN**, the way
    int 0x16                        ; tests/dosirq's own probe does: the box
    mov ax, 0x4C00                  ; has no windowed text yet (wave 6), so a
    int 0x21                        ; program that exits takes its output with
                                    ; it and the only reader is a screenshot
                                    ; taken while the bracket is still up

; -----------------------------------------------------------------------------
; find_driver - the signature at offset 3 is the whole of how a client finds one
; -----------------------------------------------------------------------------
find_driver:
    push es
    mov bl, VEC_LO
.v:
    xor bh, bh
    mov ax, bx
    shl ax, 1
    shl ax, 1
    mov si, ax
    xor ax, ax
    mov es, ax
    mov ax, [es:si+2]
    mov di, [es:si]
    or ax, ax
    jz .next
    push ds
    mov ds, ax
    mov si, di
    add si, 3
    push cs
    pop es
    mov di, s_sig
    mov cx, 8
    repe cmpsb
    pop ds
    jcxz .got
.next:
    inc bl
    cmp bl, VEC_HI
    jbe .v
    pop es
    stc
    ret
.got:
    mov [pktvec], bl
    mov al, bl                      ; build the INT the calls go through: an
    mov [callvec_ds+1], al          ; 8086 has no `int reg`, so the opcode is
    pop es                          ; patched once and called from then on
    clc
    ret

; --- callvec - `int <pktvec>`, written by find_driver ------------------------
;
; **IT PRESERVES DS, AND THAT IS NOT TIDINESS.** `driver_info` is DEFINED to
; return DS:SI pointing at the driver's name, so a client that calls it and
; keeps going runs with the DRIVER's segment as its own data segment from then
; on. Every symptom of that is somewhere else: the strings print as garbage,
; the frame handed to send_pkt is read out of the driver's image, and the
; ethertype access_type is given is two bytes of somebody else's code - so no
; arriving frame ever matches the handle and the receive path looks broken.
; `get_statistics` returns DS:SI the same way.
;
; mTCP saves DS around these calls. Ours did not, and it cost most of a
; session (SPEC.md 96.23.9).
callvec:
    push ds
    call callvec_ds
    pop ds
    ret

; --- callvec_ds - the same INT with DS LEFT AS THE DRIVER SET IT ------------
; The two calls that ANSWER in DS:SI - driver_info and get_statistics - cannot
; be read through callvec above, which is the point of it: the answer is a
; pointer into the driver's own segment and restoring DS throws the segment
; half away. A caller that wants the record has to be willing to run with a
; foreign DS for the length of the read, and to put it back itself.
callvec_ds:
    db 0xCD, 0x60
    ret                             ; `ret` and `pop` change no flag, so CF
                                    ; and DH reach the caller through both

; -----------------------------------------------------------------------------
driver_info:
    mov ah, 1
    mov al, 0xFF
    call callvec
    jc .no
    push cx
    push dx
    mov dx, s_ver
    call puts
    mov ax, bx
    call puthex16
    call crlf
    pop dx
    pop cx
    push cx
    mov dx, s_class
    call puts
    mov al, ch
    call puthex8
    call crlf
    pop cx
    ret
.no:
    mov dx, s_noinfo
    call puts
    ret

; -----------------------------------------------------------------------------
; do_access - a handle for ARP, with our receiver on it
; -----------------------------------------------------------------------------
do_access:
    mov ah, 2
    mov al, 1                       ; if_class: DIX Ethernet
    mov bx, 0xFFFF                  ; if_type: any
    mov dl, 0                       ; if_number
    mov si, arp_type
    mov cx, 2
    push cs
    pop es
    mov di, receiver
    call callvec
    jc .no
    mov [handle], ax
    push ax
    mov dx, s_hand
    call puts
    pop ax
    call puthex16
    call crlf
    clc
    ret
.no:
    mov [lasterr], dh
    stc
    ret

; -----------------------------------------------------------------------------
get_address:
    mov ah, 6
    mov bx, [handle]
    push cs
    pop es
    mov di, mymac
    mov cx, 6
    call callvec
    jc .no
    mov dx, s_mac
    call puts
    mov si, mymac
    mov cx, 6
.l:
    lodsb
    call puthex8
    loop .l
    call crlf
    ret
.no:
    mov dx, s_nomac
    call puts
    ret

; -----------------------------------------------------------------------------
; send_arp - a broadcast ARP request for the gateway
;
; A REAL FRAME A REAL HOST MUST ANSWER, which is what makes this row worth
; having: a send that is merely accepted proves the call and not the wire.
; -----------------------------------------------------------------------------
; **THE FRAME IS A TABLE, NOT A DRAWING.** It was built field by field with
; rep stosb/rep movsb, and that is eleven chances to be wrong about DF, about
; ES, and about byte order - for a frame whose every byte but the two MAC
; fields is a constant. A static template is checkable by eye against the
; RFC, and all this routine does is poke in the address the driver gave us.
send_arp:
    mov si, mymac                   ; source, twice: the Ethernet header's and
    mov di, txbuf + 6               ; the ARP sender's
    mov cx, 6
    call cpy
    mov si, mymac
    mov di, txbuf + 14 + 8
    mov cx, 6
    call cpy

    mov ah, 4
    mov si, txbuf
    mov cx, TXLEN
    call callvec
    jc .no
    mov dx, s_tx
    call puts
    mov al, 1
    call puthex8
    call crlf
    ret
.no:
    mov dx, s_notx
    call puts
    mov al, dh
    call puthex8
    call crlf
    ret

; -----------------------------------------------------------------------------
; wait_rx - spin on the BIOS tick while the up-call does the work
;
; **NOTHING HERE POLLS THE DRIVER.** That is the point: a Crynwr client does
; not, and if frames only arrived when we asked for them the box would be
; passing a test no real client would pass.
; -----------------------------------------------------------------------------
wait_rx:
    push es
    xor ax, ax
    mov es, ax
    mov bx, [es:0x46C]              ; the BDA tick, our only clock
    add bx, 55                      ; ~3 seconds at 18.2 Hz
.w:
    mov ax, [es:0x46C]
    cmp ax, bx
    jae .out
    cmp word [nrx], 0               ; ...but stop early once the reply is in,
    je .w                           ; so a passing run is not a slow one
    cmp word [narp], 0
    je .w
.out:
    pop es
    mov dx, s_rx
    call puts
    mov ax, [nrx]
    call puthex16
    call crlf
    mov dx, s_arp
    call puts
    mov ax, [narp]
    call puthex16
    call crlf
    mov dx, s_ety
    call puts
    mov ax, [firstety]
    call puthex16
    call crlf
    ret

; -----------------------------------------------------------------------------
; do_tcp - open a connection, ask for a page, and say what came back
;
; **THE POINT IS THE ANSWER, NOT THE CONNECTION.** mTCP's own programs cannot
; say what they received - their output dies with the bracket (SPEC.md 96.11's
; wave 6) - so a handshake that fails inside one is unreadable. This runs the
; three-way handshake by hand and prints each answer where a test can read it:
;
;   SYNACK 0112   the flags of the segment that came back, with bit 8 set so
;                 that "nothing yet" and "flags of zero" are different answers
;   DATA 0060     payload bytes that arrived after the request
;   FIRST 4854    the first two of them - 'HT' of an HTTP response
;
; It registers for IP separately, because the ARP handle above will not match
; an 0800 frame.
;
; **THE GATEWAY'S MAC COMES OUT OF THE ARP REPLY** rather than a constant, so
; the same binary is right on either wire: the translation answers with its own
; synthetic router address (SPEC.md 96.26.2) and a real card's gateway answers
; with its real one.
; -----------------------------------------------------------------------------
do_tcp:
    mov ah, 2                       ; access_type for IP
    mov al, 1
    mov bx, 0xFFFF
    mov dl, 0
    mov si, ip_type
    mov cx, 2
    push cs
    pop es
    mov di, receiver
    call callvec
    jc .no
    mov [handle2], ax

    mov si, gwmac                   ; nothing answered the ARP, so there is
    mov cx, 6                       ; nobody to open a connection to and the
    call iszero                     ; failure to report is that one
    jne .havegw
    mov dx, s_nogw
    call puts
    ret
.havegw:
    mov si, gwmac                   ; --- the two MACs, which are the only
    mov di, tcpbuf + 0              ; bytes of the frame that are not either
    mov cx, 6                       ; a constant or computed
    call cpy
    mov si, mymac
    mov di, tcpbuf + 6
    mov cx, 6
    call cpy

    ; --- the SYN -----------------------------------------------------------
    mov word [seq_hi], 0            ; an ISN a capture can be read for
    mov word [seq_lo], 0x1000
    mov word [ack_hi], 0
    mov word [ack_lo], 0
    mov word [nrx], 0
    mov word [firstety], 0
    mov word [lastflags], 0
    mov word [rxdata], 0
    mov word [rxfirst], 0
    mov al, F_SYN
    xor cx, cx
    xor si, si
    call tcp_build
    call tcp_tx
    jc .no
    mov ax, 1                       ; a SYN consumes one sequence number
    call seq_adv

    mov cx, 90                      ; ~5 seconds of BIOS ticks
    mov si, lastflags
    call wait_word
    mov dx, s_syn
    call puts
    mov ax, [lastflags]
    call puthex16
    call crlf
    cmp ax, 0x100 | F_SYN | F_ACK
    jne .out                        ; no handshake, so no request to make

    ; --- the ACK that finishes the handshake -------------------------------
    call take_ack                   ; their sequence number + 1
    mov al, F_ACK
    xor cx, cx
    xor si, si
    call tcp_build
    call tcp_tx

    ; --- ...and the request ------------------------------------------------
    mov al, F_PSH | F_ACK
    mov cx, GETLEN
    mov si, s_get
    call tcp_build
    call tcp_tx
    mov ax, GETLEN
    call seq_adv

    mov cx, 145                     ; ~8 seconds: the far side has a
    mov si, rxdata                  ; connection of its own to make
    call wait_word
    mov dx, s_data
    call puts
    mov ax, [rxdata]
    call puthex16
    call crlf
    mov dx, s_first
    call puts
    mov ax, [rxfirst]
    call puthex16
    call crlf
    call put_log
    cmp word [rxdata], 0
    je .out
    mov ax, [dack_hi]               ; --- and acknowledge it, which is what
    mov [ack_hi], ax                ; the receiver banked the numbers for
    mov ax, [dack_lo]
    mov [ack_lo], ax
    mov al, F_ACK
    xor cx, cx
    xor si, si
    call tcp_build
    call tcp_tx
.out:
    ret
.no:
    mov al, dh                      ; **BANKED FIRST**: puts takes its string
    mov [lasterr], al               ; in DX, so reading dh after it prints the
    mov dx, s_nosyn                 ; high byte of the message's address
    call puts
    mov al, [lasterr]
    call puthex8
    call crlf
    ret

; -----------------------------------------------------------------------------
; do_listen - BE THE SERVER: wait to be connected to, and answer (96.26.8)
;
; The box has to be TOLD which port to forward, because a listening client
; sends nothing a packet driver could be read for - `OS88LISTEN=<port>` on the
; environment page is how (SPEC.md 96.26.8). This program does not set that
; and does not need to: it answers whatever arrives, so the port is the
; harness's business and the assertion here is only *did a connection reach
; me and could I serve it*.
;
; Three answers, and the first is the one nothing else can give:
;   LSN 0001       an unasked SYN arrived - NETV_ACCEPT reached the client
;   LDATA 0012     ...and the request it then sent
;   LFIRST 4745    ...starting 'GE' of a GET
; -----------------------------------------------------------------------------
do_listen:
    ; **NOTHING IS CLEARED HERE, AND THAT IS THE POINT.** A `.COM` starts with
    ; the zeros its image carries and runs once per launch, so the three
    ; stores that used to stand here bought nothing - and they THREW AWAY the
    ; case this routine exists for. The box accepts the moment it knows where
    ; the client is (dn_accept needs [dn_cip], which any IP frame teaches), so
    ; a connection that was already waiting is accepted DURING the outbound
    ; leg above, and `receiver` banks its SYN before this routine is reached.
    ; Zeroing [isyn] here then waited ninety ticks for a SYN that had already
    ; arrived and been recorded.
    mov cx, 90                      ; ~5 seconds of BIOS ticks, which is
    mov si, isyn                    ; do_tcp's own figure. NOT more: on the
    call wait_word                  ; cable every one of these ticks is
                                    ; stepped by the partner four hundred
                                    ; cycles at a time, so a generous wait
                                    ; here is minutes of a test's wall clock
                                    ; - and the host end is already retrying
                                    ; its connect, so there is nothing to be
                                    ; patient about
    mov dx, s_lsn
    call puts
    mov ax, [isyn]
    call puthex16
    call crlf
    cmp word [isyn], 0
    je .out                         ; nobody connected, which on the card arm
                                    ; is the ordinary answer

    ; --- the roles swap, and every field of the frame with them -----------
    mov ax, [isyn_sport]            ; their port becomes the destination...
    mov [t_dport], ax
    mov ax, [isyn_dport]            ; ...and the one they asked for is ours.
    mov [t_sport], ax               ; ALL FOUR COME OUT OF THE BANK: `rxbuf`
    mov si, isyn_src                ; is whatever arrived LAST, and the SYN is
    mov di, t_dst                   ; long gone from it by here
    mov cx, 4
    call cpy
    mov si, isyn_dst                ; ...and the one they addressed is ours
    mov di, t_src
    mov cx, 4
    call cpy

    mov word [seq_hi], 0            ; our ISN for this direction
    mov word [seq_lo], 0x2000
    mov ax, [isyn_seq_h]            ; ...and their ISN + 1 is what we ack
    mov [ack_hi], ax
    mov ax, [isyn_seq_l]
    mov [ack_lo], ax
    add word [ack_lo], 1
    adc word [ack_hi], 0

    mov al, F_SYN | F_ACK           ; --- the SYN|ACK ---
    xor cx, cx
    xor si, si
    call tcp_build
    call tcp_tx
    mov ax, 1                       ; a SYN consumes one sequence number
    call seq_adv

    mov cx, 90                      ; --- and the request they then send ---
    mov si, ldata
    call wait_word
    mov dx, s_ldata
    call puts
    mov ax, [ldata]
    call puthex16
    call crlf
    mov dx, s_lfirst
    call puts
    mov ax, [lfirst]
    call puthex16
    call crlf
    cmp word [ldata], 0
    je .out

    mov ax, [ldata]                 ; acknowledge what arrived, then answer
    add [ack_lo], ax
    adc word [ack_hi], 0
    mov al, F_PSH | F_ACK
    mov cx, LREPLEN
    mov si, s_lrep
    call tcp_build
    call tcp_tx
    mov ax, LREPLEN
    call seq_adv
    call put_log
.out:
    ret

; -----------------------------------------------------------------------------
; tcp_build - one segment in tcpbuf, with both checksums COMPUTED
;
; in:  AL = the flags byte, CX = the payload length, SI = the payload (0 for
;      none); [seq_*] and [ack_*] = the numbers to put on the wire
; out: [txtcp] = the frame's length. Nothing is sent.
;
; **THE CHECKSUMS ARE COMPUTED AND NOT TEMPLATED**, which the first version's
; SYN was: a template only works for a frame that never changes, and a segment
; carrying a sequence number and a payload changes every time. A wrong
; checksum is indistinguishable from the box dropping the frame, which is
; precisely the wrong diagnosis to leave lying around.
; -----------------------------------------------------------------------------
tcp_build:
    push ax
    push bx
    push cx
    push si
    push di
    mov [t_flags], al
    mov [t_pay], cx
    or si, si
    jz .nopay
    mov di, tcpbuf + 54
    call cpy                        ; DS:SI -> DS:DI, CX bytes
.nopay:
    ; --- **THE ENDS ARE VARIABLES AND NOT THE TEMPLATE'S** ----------------
    ; do_listen (SPEC.md 96.26.8) makes this program the SERVER, and then
    ; every one of these is the mirror of the client role: the ports swap, the
    ; addresses swap, and the peer is whoever connected rather than the
    ; gateway. Four words and eight bytes, set once per role.
    mov ax, [t_sport]
    mov [tcpbuf + 34], ax
    mov ax, [t_dport]
    mov [tcpbuf + 36], ax
    mov si, t_src
    mov di, tcpbuf + 26
    mov cx, 4
    call cpy
    mov si, t_dst
    mov di, tcpbuf + 30
    mov cx, 4
    call cpy
    mov si, t_src                   ; ...and the pseudo-header's copy of them
    mov di, pseudo + 0
    mov cx, 4
    call cpy
    mov si, t_dst
    mov di, pseudo + 4
    mov cx, 4
    call cpy
    ; --- the IP header -----------------------------------------------------
    mov ax, [t_pay]
    add ax, 40                      ; two 20-byte headers
    xchg al, ah
    mov [tcpbuf + 16], ax           ; total length, big-endian
    mov word [tcpbuf + 24], 0       ; the checksum field, zeroed to sum over
    xor bx, bx
    mov si, tcpbuf + 14
    mov cx, 20
    call ck_sum
    not bx
    xchg bl, bh
    mov [tcpbuf + 24], bx
    ; --- the TCP header ----------------------------------------------------
    mov ax, [seq_hi]
    xchg al, ah
    mov [tcpbuf + 38], ax
    mov ax, [seq_lo]
    xchg al, ah
    mov [tcpbuf + 40], ax
    mov ax, [ack_hi]
    xchg al, ah
    mov [tcpbuf + 42], ax
    mov ax, [ack_lo]
    xchg al, ah
    mov [tcpbuf + 44], ax
    mov al, [t_flags]
    mov [tcpbuf + 47], al
    mov word [tcpbuf + 50], 0
    ; ...whose checksum covers a PSEUDO-HEADER as well as the segment, which
    ; is the one part of TCP that is not in the segment it protects
    mov ax, [t_pay]
    add ax, 20
    xchg al, ah
    mov [pseudo + 10], ax
    xor bx, bx
    mov si, pseudo
    mov cx, 12
    call ck_sum
    mov si, tcpbuf + 34
    mov cx, [t_pay]
    add cx, 20
    call ck_sum
    not bx
    xchg bl, bh
    mov [tcpbuf + 50], bx
    mov ax, [t_pay]
    add ax, 54
    mov [txtcp], ax
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ck_sum - fold CX bytes at DS:SI into BX, as big-endian 16-bit words
;
; The carry is folded back in as it happens rather than at the end, which is
; what makes the order of the calls - and splitting one sum across two buffers,
; as the TCP checksum does - not matter.
; -----------------------------------------------------------------------------
ck_sum:
    push ax
    push cx
    push dx
    push si
    mov dx, cx
    and dx, 1
    shr cx, 1
    jcxz .tail
.l:
    mov ah, [si]
    mov al, [si+1]
    add bx, ax
    adc bx, 0
    inc si
    inc si
    loop .l
.tail:
    or dx, dx
    jz .out
    mov ah, [si]                    ; a lone last byte is the HIGH half of a
    xor al, al                      ; word the standard pads with zero
    add bx, ax
    adc bx, 0
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
tcp_tx:
    push ax
    push cx
    push si
    mov ah, 4
    mov si, tcpbuf
    mov cx, [txtcp]
    call callvec
    pop si
    pop cx
    pop ax
    ret                             ; CF and DH are callvec's - pop sets
                                    ; neither

; -----------------------------------------------------------------------------
; seq_adv - our sequence number, by AX
seq_adv:
    add [seq_lo], ax
    adc word [seq_hi], 0
    ret

; --- take_ack - the sequence number of the segment just received, plus one --
; A SYN consumes one sequence number, so this is what acknowledges it.
take_ack:
    push ax
    mov ax, [rxbuf + 34 + 4]        ; the wire is big-endian and we are not
    xchg al, ah
    mov [ack_hi], ax
    mov ax, [rxbuf + 34 + 6]
    xchg al, ah
    mov [ack_lo], ax
    add word [ack_lo], 1
    adc word [ack_hi], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; wait_word - spin on the BIOS tick until [SI] is non-zero, CX ticks at most
;
; **IT POLLS NOTHING.** The frame arrives through the driver's own up-call,
; called from the box's INT 08h chain (SPEC.md 96.23.4), so a Crynwr client
; has nothing to ask and this loop is the whole of its receive path.
; -----------------------------------------------------------------------------
wait_word:
    push ax
    push bx
    push es
    xor ax, ax
    mov es, ax
    mov bx, [es:0x46C]
    add bx, cx
.w:
    mov ax, [es:0x46C]
    cmp ax, bx
    jae .done
    cmp word [si], 0
    je .w
.done:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; put_log - `FLAGS 12 10 18 11`, the connection's shape on one line
put_log:
    push ax
    push bx
    push cx
    push dx
    mov dx, s_flags
    call puts
    mov bx, 0
    mov cx, [nflag]
    jcxz .done
.l:
    mov al, [flaglog+bx]
    call puthex8
    mov dx, s_sp
    call puts
    inc bx
    loop .l
.done:
    call crlf
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; iszero - CX bytes at DS:SI: ZF=1 if every one of them is
iszero:
    push ax
    push cx
    push si
    xor ax, ax
.l:
    or al, [si]
    inc si
    loop .l
    or al, al
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; do_stats - get_statistics' six dwords, printed as `STATS pin pout bin bout`
;
; The record lives in the DRIVER's segment, so this is the one call that has
; to go through callvec_ds - and the record is COPIED out before anything is
; printed, because every string below is a near label in ours.
; -----------------------------------------------------------------------------
do_stats:
    push ds
    push si
    push di
    mov ah, 24
    mov bx, [handle]
    call callvec_ds
    jc .no
    xor di, di                      ; **SI AND DI ARE BOTH WALKED**: [si+di]
.l:                                 ; is not a legal 8086 effective address -
    mov ax, [si]                    ; only BX or BP may be the base
    mov [cs:statbuf+di], ax         ; DS:SI is THEIRS and CS is ours, which is
    inc si                          ; what makes the override the whole trick
    inc si
    inc di
    inc di
    cmp di, 24
    jb .l
    pop di
    pop si
    pop ds
    mov dx, s_stats
    call puts
    xor bx, bx
.p:
    mov ax, [statbuf+bx]            ; the LOW word of each dword: a transfer
    call puthex16                   ; this size cannot reach the high one, and
    mov dx, s_sp                    ; a test that needs it can read the record
    call puts                       ; out of guest memory
    add bx, 4
    cmp bx, 24
    jb .p
    call crlf
    ret
.no:
    pop di
    pop si
    pop ds
    ret

; -----------------------------------------------------------------------------
release:
    mov ah, 3
    mov bx, [handle]
    call callvec
    ret

; -----------------------------------------------------------------------------
; receiver - THE UP-CALL, called twice per frame by the driver
;
;   AX=0  give me somewhere to put CX bytes -> ES:DI, or 0:0 to refuse
;   AX=1  here it is, in DS:SI, CX bytes
;
; Far, and it may be entered at ANY time - from the driver's tick - so every
; reference here is through CS and nothing assumes DS.
; -----------------------------------------------------------------------------
receiver:
    or ax, ax
    jnz .have
    cmp cx, RXMAX                   ; a frame we have no room for is REFUSED,
    ja .refuse                      ; which is the contract rather than a
    cmp word [cs:rxbusy], 0         ; failure (SPEC.md 96.23.4.2)
    jne .refuse
    mov word [cs:rxbusy], 1
    mov [cs:rxlen], cx
    push cs
    pop es
    mov di, rxbuf
    retf
.refuse:
    xor ax, ax
    mov es, ax
    xor di, di
    retf
.have:
    push ax
    push bx
    push cx
    push si
    push ds
    push cs
    pop ds
    inc word [nrx]
    mov ax, [rxbuf+12]              ; the ethertype, as it sits on the wire
    xchg al, ah
    cmp word [firstety], 0
    jne .notfirst
    mov [firstety], ax
.notfirst:
    cmp ax, ETY_IP
    jne .notip
    cmp byte [rxbuf+14+9], 6        ; ...a TCP segment: bank its FLAGS, which
    jne .out                        ; is the first question do_tcp asks
    mov al, [rxbuf+34+13]
    xor ah, ah
    or ax, 0x100                    ; ...with a bit set so "nothing yet" and
    mov [lastflags], ax             ; "flags of zero" are different answers
    ; --- **AN INBOUND SYN IS SOMEBODY CONNECTING TO US** (SPEC.md 96.26.8) -
    ; A SYN with no ACK, arriving unasked: do_listen's whole signal. Banked
    ; here rather than acted on, because this is an up-call from the box's own
    ; INT 08h chain and building a reply inside one would put a send_pkt
    ; inside the driver's own receive path.
    test al, F_SYN
    jz .notsyn
    test al, F_ACK
    jnz .notsyn
    push ax                         ; **THE FLAGS ARE BANKED ACROSS THIS**:
                                    ; the log below writes AL, and every read
                                    ; here overwrites it - so without this the
                                    ; one entry that matters most goes into
                                    ; the log as the low byte of a port
    mov ax, [rxbuf+34+0]            ; its source port, wire order
    mov [isyn_sport], ax
    mov ax, [rxbuf+34+2]            ; **AND THE PORT IT ASKED FOR**, which is
    mov [isyn_dport], ax            ; the half do_listen used to read back out
                                    ; of `rxbuf` when it ran - true only while
                                    ; the SYN is still the LAST frame that
                                    ; arrived, and it is not: the box accepts
                                    ; as soon as it knows where we are, so the
                                    ; SYN lands during the outbound leg and
                                    ; `rxbuf` holds that connection's FIN by
                                    ; the time this matters. The reply then
                                    ; went out on the wrong source port and
                                    ; the box read it as a NEW outbound
                                    ; connection - `NETV_OPEN 10.88.0.255` in
                                    ; the far side's log, against its own
                                    ; invented pool address
    mov ax, [rxbuf+34+4]            ; ...and its ISN, big-endian to ours
    xchg al, ah
    mov [isyn_seq_h], ax
    mov ax, [rxbuf+34+6]
    xchg al, ah
    mov [isyn_seq_l], ax
    mov si, rxbuf + 14 + 12         ; ...and where it came from
    mov di, isyn_src
    mov cx, 4
    call cpy
    mov si, rxbuf + 14 + 16         ; ...and the address it was sent TO, for
    mov di, isyn_dst                ; isyn_dport's reason
    mov cx, 4
    call cpy
    inc word [isyn]
    pop ax                          ; the flags again
                                    ; ...and FALLS THROUGH to the flag log:
                                    ; the inbound SYN belongs in it, being the
                                    ; proof that NETV_ACCEPT reached the client
                                    ; at all
.notsyn:
    ; --- **EVERY SEGMENT'S FLAGS, IN ORDER** -------------------------------
    ; One word is the LAST answer, and a connection is a sequence: 12 02 10
    ; 18 11 is a handshake, a request, a reply and a close, and any one of
    ; them missing is a different defect. Eight is more than a transfer this
    ; small can produce, so a full log is itself a finding.
    mov si, [nflag]
    cmp si, FLAGLOG
    jae .nolog
    mov [flaglog+si], al
    inc word [nflag]
.nolog:
    ; --- ...AND ITS PAYLOAD, which is the question after it ----------------
    ; The length is the IP header's rather than the frame's: a short segment
    ; is PADDED to the wire's 60-byte minimum, so counting what arrived would
    ; read 6 bytes of payload out of a bare ACK.
    mov ax, [rxbuf+14+2]            ; IP total length, big-endian
    xchg al, ah
    sub ax, 20                      ; less the IP header...
    mov bl, [rxbuf+34+12]           ; ...and less the TCP one, whose length is
    mov cl, 4                       ; the top nibble of +12, in DWORDS
    shr bl, cl
    shl bl, 1
    shl bl, 1
    xor bh, bh
    sub ax, bx
    jbe .out                        ; a segment with no payload at all
    add [rxdata], ax
    mov [paylen], ax
    mov ax, [rxbuf+34+4]            ; its sequence number + its length is what
    xchg al, ah                     ; do_tcp owes it back as an ACK
    mov [dack_hi], ax
    mov ax, [rxbuf+34+6]
    xchg al, ah
    mov [dack_lo], ax
    mov ax, [paylen]
    add [dack_lo], ax
    adc word [dack_hi], 0
    cmp word [isyn], 0              ; on the SERVER flow the same payload is
    je .clientpay                   ; the REQUEST, and it is counted apart:
    mov dx, [rxbuf+34+2]            ; do_listen asserts on what it was sent.
    cmp dx, [isyn_dport]            ; **AND THE PORT DECIDES WHICH FLOW IT
    jne .clientpay                  ; IS**, not [isyn] alone: the SYN arrives
                                    ; during the outbound leg, so `isyn` is
                                    ; set while the CLIENT connection is still
                                    ; carrying data - and its 45-byte answer
                                    ; was landing in [ldata] as though the
                                    ; host had sent it. LDATA 45 LFIRST 4854
                                    ; ('HT' of our own reply) was the reading
    add [ldata], ax
    mov si, rxbuf + 34
    add si, bx
    mov ah, [si]
    mov al, [si+1]
    cmp word [lfirst], 0
    jne .out
    mov [lfirst], ax
    jmp short .out
.clientpay:
    cmp word [rxfirst], 0
    jne .out
    mov si, rxbuf + 34              ; the payload starts after a header whose
    add si, bx                      ; length we have just worked out
    mov ah, [si]                    ; ...and its first two bytes are 'HT' of
    mov al, [si+1]                  ; an HTTP response
    mov [rxfirst], ax
    jmp short .out
.notip:
    cmp ax, ETY_ARP
    jne .out
    mov al, [rxbuf+14+7]            ; ARP oper, low byte: 2 = a REPLY
    cmp al, 2
    jne .out
    inc word [narp]
    mov si, rxbuf + 22              ; ...and its sender hardware address is
    mov di, gwmac                   ; the gateway's, which do_tcp addresses
    mov cx, 6                       ; its frames to
    call cpy
.out:
    mov word [rxbusy], 0
    pop ds
    pop si
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; the console, in the two calls every DOS has
; -----------------------------------------------------------------------------
puts:
    push ax
    mov ah, 9
    int 0x21
    pop ax
    ret

crlf:
    push ax
    push dx
    mov dx, s_crlf
    mov ah, 9
    int 0x21
    pop dx
    pop ax
    ret

puthex16:
    push ax
    mov al, ah
    call puthex8
    pop ax
    call puthex8
    ret

puthex8:
    push ax
    push cx
    push dx
    mov cl, 4
    mov ch, al
    shr al, cl
    call .nyb
    mov al, ch
    and al, 0x0F
    call .nyb
    pop dx
    pop cx
    pop ax
    ret
.nyb:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    push dx
    mov dl, al
    mov ah, 2
    int 0x21
    pop dx
    ret

; -----------------------------------------------------------------------------
; --- cpy - DS:SI -> DS:DI, CX bytes, no string instruction and no ES -------
cpy:
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

ip_type:    db 0x08, 0x00           ; ...and IP, for do_tcp's own handle
arp_type:   db 0x08, 0x06           ; the ethertype access_type is given, in
                                    ; wire order - which is what the spec says
                                    ; and what makes a big-endian compare right

s_sig:      db 'PKT DRVR'
s_crlf:     db 13, 10, '$'
s_vec:      db 'PKTVEC $'
s_ver:      db 'VER $'
s_class:    db 'CLASS $'
s_hand:     db 'HANDLE $'
s_mac:      db 'MAC $'
s_tx:       db 'TX $'
s_rx:       db 'RX $'
s_arp:      db 'ARP $'
s_ety:      db 'ETY $'
s_nodrv:    db 'NODRV 1', 13, 10, '$'
s_noacc:    db 'NOACC 1', 13, 10, '$'
s_noinfo:   db 'NOINFO 1', 13, 10, '$'
s_nomac:    db 'NOMAC 1', 13, 10, '$'
s_notx:     db 'NOTX $'
s_syn:      db 'SYNACK $'
s_data:     db 'DATA $'
s_first:    db 'FIRST $'
s_nogw:     db 'NOGW 1', 13, 10, '$'
s_flags:    db 'FLAGS $'
s_stats:    db 'STATS $'
s_lsn:      db 'LSN $'
s_ldata:    db 'LDATA $'
s_lfirst:   db 'LFIRST $'
s_sp:       db ' $'
s_nosyn:    db 'NOSYN $'
s_ready:    db 'READY', 13, 10, '$'

; --- the request itself, which is not a `$`-terminated string -------------
s_get:      db 'GET / HTTP/1.0', 13, 10, 13, 10
GETLEN      equ $ - s_get

; ...and what do_listen answers WITH, as the server half (SPEC.md 96.26.8)
s_lrep:     db 'HTTP/1.0 200 OK', 13, 10, 13, 10, 'dos', 13, 10
LREPLEN     equ $ - s_lrep

F_FIN       equ 0x01                ; the flags, by name
F_SYN       equ 0x02
F_RST       equ 0x04
F_PSH       equ 0x08
F_ACK       equ 0x10

pktvec:     db 0
lasterr:    db 0
handle:     dw 0
handle2:    dw 0
lastflags:  dw 0
nrx:        dw 0
narp:       dw 0
firstety:   dw 0
rxbusy:     dw 0
rxlen:      dw 0
rxdata:     dw 0                    ; payload bytes that have arrived
rxfirst:    dw 0                    ; ...and the first two of them
paylen:     dw 0
seq_hi:     dw 0                    ; **TWO WORDS, NOT A DWORD** - this is an
seq_lo:     dw 0                    ; 8086, so a 32-bit sequence number is
ack_hi:     dw 0                    ; carried as its halves and written to the
ack_lo:     dw 0                    ; wire big-endian a word at a time
dack_hi:    dw 0                    ; what the last DATA segment owes back
dack_lo:    dw 0
nflag:      dw 0
FLAGLOG     equ 8
flaglog:    times FLAGLOG db 0
statbuf:    times 24 db 0           ; get_statistics' record, copied out of
                                    ; the driver's own segment
txtcp:      dw 0
t_flags:    db 0
t_pay:      dw 0
t_sport:    dw 0x3412               ; OUR port, wire order - 4660 as a client
t_dport:    dw 0xA31F               ; ...and theirs, 8099
t_src:      db 10, 0, 2, 15         ; our address...
t_dst:      db 10, 0, 2, 2          ; ...and the peer's
isyn:       dw 0                    ; --- what an INBOUND SYN brought ---
isyn_sport: dw 0                    ; its source port, wire order
isyn_seq_h: dw 0                    ; ...and its ISN
isyn_seq_l: dw 0
isyn_dport: dw 0                    ; ...the port it asked for
isyn_src:   times 4 db 0            ; ...and where it came from
isyn_dst:   times 4 db 0            ; ...and the address it was sent to
ldata:      dw 0                    ; payload bytes it sent us
lfirst:     dw 0
mymac:      times 6 db 0
gwmac:      times 6 db 0            ; out of the ARP reply, never a constant
; --- the TCP frame, BUILT rather than templated ------------------------------
; To 10.0.2.2:8099, from 10.0.2.15:4660. Every field a segment changes - the
; length, both checksums, the sequence and acknowledgement numbers, the flags
; and the payload - is written by tcp_build; what is left here is the constant
; part, so that the layout is readable in one place and the code is offsets
; into it.
tcpbuf:
            times 6 db 0                        ; +0  to the gateway: poked in
            times 6 db 0                        ; +6  from us: poked in
            db 0x08, 0x00                       ; +12 IPv4
            db 0x45, 0x00                       ; +14 v4, five words, no TOS
            db 0x00, 0x28                       ; +16 total length: computed
            db 0x00, 0x00, 0x00, 0x00           ; +18 id, flags
            db 64, 6                            ; +22 ttl, TCP
            db 0x00, 0x00                       ; +24 header checksum: computed
            db 10, 0, 2, 15                     ; +26 from 10.0.2.15
            db 10, 0, 2, 2                      ; +30 to   10.0.2.2
            db 0x12, 0x34                       ; +34 sport 4660
            db 0x1F, 0xA3                       ; +36 dport 8099
            db 0x00, 0x00, 0x00, 0x00           ; +38 seq: written per segment
            db 0x00, 0x00, 0x00, 0x00           ; +42 ack: written per segment
            db 0x50, 0x00                       ; +46 five words; flags poked in
            db 0x04, 0x00                       ; +48 window 1024
            db 0x00, 0x00                       ; +50 checksum: computed
            db 0x00, 0x00                       ; +52 urgent
            times 128 db 0                      ; +54 ...and room for a payload

; --- TCP's pseudo-header, which is the one part of the checksum that is not --
; in the segment it protects. The length word is written per segment.
pseudo:     db 10, 0, 2, 15
            db 10, 0, 2, 2
            db 0, 6
            db 0, 0

; --- the ARP request, as a template (every byte but the MACs is a constant) --
txbuf:
            db 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF    ; +0  destination: broadcast
            times 6 db 0                        ; +6  source: poked in
            db 0x08, 0x06                       ; +12 ethertype ARP
            db 0x00, 0x01                       ; +14 htype: Ethernet
            db 0x08, 0x00                       ; +16 ptype: IPv4
            db 6                                ; +18 hlen
            db 4                                ; +19 plen
            db 0x00, 0x01                       ; +20 oper: REQUEST
            times 6 db 0                        ; +22 sender hardware: poked in
            db 10, 0, 2, 15                     ; +28 sender protocol
            times 6 db 0                        ; +32 target hardware: unknown
            db 10, 0, 2, 2                      ; +38 target protocol: the
TXLEN       equ 42                              ;     gateway QEMU always has
            times 64 - TXLEN db 0
rxbuf:      times RXMAX db 0
            times 256 db 0
stack_top:
