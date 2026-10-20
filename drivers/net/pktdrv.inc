; =============================================================================
; os8088 - drivers/net/pktdrv.inc
;
; A PACKET DRIVER WHERE THE NE2000 WAS (docs/plans/completed/NET-PLAN.md 5, SPEC.md 62.11.1)
;
; drivers/ether's TCP/IP stack talks to its card through exactly five
; routines - ne_probe, ne_init, ne_stop, ne_tx, ne_rx - and nothing else in
; inet.inc, tcp.inc or dns.inc knows an 8390 exists. This file is those five
; again over the FTP Software Packet Driver interface, so the SAME stack runs
; on the DOS end of the parallel cable, against whatever card mTCP is already
; using there.
;
; **THAT IS THE WHOLE REASON THE DOS SIDE CAN SERVE SOCKETS AT ALL.** The
; alternative was linking mTCP itself into a NASM .COM - a C++ library and an
; Open Watcom toolchain this tree deliberately does not have - or writing a
; second TCP, which is docs/plans/completed/NET-PLAN.md's own warning about two replayers
; wearing a different hat.
;
; --- WHAT IS DIFFERENT FROM ne2000.inc, and it is one thing -----------------
; **ne_probe does NOT fill eth_mac here; ne_init does.** The station address
; comes from get_address, which needs the handle access_type hands back, and
; access_type is the call that goes live - so a probe that learned the MAC
; would have to register a receiver and release it again to stay a probe.
; os88net.asm calls the two in that order and is this file's only host;
; ether.asm's DRVV_ATTACH order (probe, then init) would need the dance.
;
; --- THE RECEIVER IS ASYNCHRONOUS AND THAT IS WHY THERE IS A RING -----------
; A packet driver does not answer "have you a frame"; it CALLS YOU, in
; interrupt context, twice per frame: once for somewhere to put it and once
; to say it is there. Frames therefore arrive while inet.inc is still walking
; the last one, and ne_rx's contract is "take ONE frame" - so pk_recv stages
; into a small ring and ne_rx pops it. Single producer, single consumer, two
; cursors: the only word both sides touch is the slot's own length, and it
; goes non-zero exactly once per frame and back to zero exactly once.
; =============================================================================

; THE FOUR CARD CONSTANTS ethstate.inc STILL NAMES. They are ne2000.inc's and
; a packet-driver host does not include it, so they live here rather than
; being teased out of a file that is correct as it stands: NE_FRAME is the one
; that means anything to us (inet.inc sizes its buffers on it) and the ring
; pages are the card's own, kept only so the shared state assembles.
NE_FRAME    equ 1514
NE_TXPAGE   equ 0x40
NE_PSTART   equ NE_TXPAGE + 6   ; ne2000.inc's NE_TXPAGE + NE_TXPAGES
NE_PSTOP    equ 0x80

PKT_FRAME   equ 1514            ; one Ethernet frame, headers and all
PKT_NBUF    equ 4               ; ...and how many may be in flight behind the
                                ; stack. Four is ~6KB and covers a burst of a
                                ; window's worth of segments; a full ring
                                ; REFUSES, which is what a dropped frame is
                                ; supposed to look like to TCP

; -----------------------------------------------------------------------------
; ne_probe - find a packet driver, and check it is an Ethernet one
; out: CF=0 and [pk_int] set; CF=1 = there is none
;
; The signature is the eight bytes 'PKT DRVR' at offset 3 of the handler,
; which is the spec's own way of telling a packet driver from whatever else
; has taken a vector in 60h..80h.
; -----------------------------------------------------------------------------
ne_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [pk_int], 0
    mov dh, 0x60
.scan:
    mov al, dh
    mov ah, 0x35                ; DOS: get interrupt vector -> ES:BX
    int 0x21
    mov ax, es                  ; a vector nobody has taken is not a driver
    or  ax, bx
    jz  .next
    mov di, bx
    add di, 3
    mov si, pk_sig
    mov cx, 8
    cld
.cmp:
    lodsb
    cmp al, [es:di]
    jne .next
    inc di
    loop .cmp
    jmp short .found
.next:
    inc dh
    cmp dh, 0x81
    jb  .scan
    jmp short .none
.found:
    mov [pk_int], dh
    mov [pk_intop + 1], dh      ; ...PATCHED INTO THE `int` ITSELF: the vector
                                ; is not known until now and the 8086 has no
                                ; indirect software interrupt. Every DOS
                                ; packet-driver client does this
    mov ah, 1                   ; driver_info
    mov al, 0xFF                ; ...the value an older driver wants
    push ds                     ; **IT ANSWERS DS:SI = ITS NAME**, so DS is
    call pk_call                ; gone unless we keep it
    pop ds
    jc  .none
    cmp ch, 1                   ; class 1 = Ethernet (DIX and 802.3)
    jne .none
    clc
    jmp short .out
.none:
    mov byte [pk_int], 0
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_init - go live: register the receiver and learn the station address
; out: CF=0 and eth_mac filled; CF=1 = the driver refused
; -----------------------------------------------------------------------------
ne_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [pk_wr], 0         ; ...a fresh ring, because a second bring-up
    mov byte [pk_rd], 0         ; must not inherit the first one's frames
    mov cx, PKT_NBUF
    xor bx, bx
.zr:
    mov word [pk_len + bx], 0
    add bx, 2
    loop .zr

    mov ah, 2                   ; access_type
    mov al, 1                   ; class 1: Ethernet
    mov bx, 0xFFFF              ; type: whatever this interface is
    mov dl, 0                   ; interface 0
    xor cx, cx                  ; typelen 0 = EVERY packet, which is what a
    xor si, si                  ; stack wants and what mTCP asks for too
    push ds
    pop es
    mov di, pk_recv             ; ES:DI = the receiver
    call pk_call
    jc  .fail
    mov [pk_handle], ax

    mov ah, 6                   ; get_address
    mov bx, [pk_handle]
    push ds
    pop es
    mov di, eth_mac
    mov cx, 6
    call pk_call
    jc  .fail
    clc
    jmp short .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_stop - hand the type back. It cannot fail and its caller cannot care.
; -----------------------------------------------------------------------------
ne_stop:
    push ax
    push bx
    cmp word [pk_handle], 0
    je  .out
    mov ah, 3                   ; release_type
    mov bx, [pk_handle]
    call pk_call
    mov word [pk_handle], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_tx - put CX bytes of eth_txb on the wire
; out: CF=1 = the driver would not take it
;
; A frame shorter than 60 bytes is padded and THE PAD IS ZEROED, which the
; card-side version could leave to the hardware: send_pkt sends exactly what
; it is given, so an unzeroed pad would put the tail of the previous frame on
; the wire behind an ARP request.
; -----------------------------------------------------------------------------
ne_tx:
    push ax
    push cx
    push di
    push es
    cmp cx, 60
    jae .send
    push cx
    push ds
    pop es
    mov di, eth_txb
    add di, cx
    mov ax, 60
    sub ax, cx
    mov cx, ax
    xor al, al
    cld
    rep stosb
    pop cx
    mov cx, 60
.send:
    mov ah, 4                   ; send_pkt: DS:SI = the frame, CX = its length
    push si
    mov si, eth_txb
    call pk_call
    pop si
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ne_rx - take ONE frame off the ring
; out: CF=0 and CX = its length in eth_rxb; CF=1 = the ring is empty
; -----------------------------------------------------------------------------
ne_rx:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    mov bl, [pk_rd]
    xor bh, bh
    shl bx, 1
    mov cx, [pk_len + bx]
    or  cx, cx
    jz  .empty
    mov al, [pk_rd]
    xor ah, ah
    mov dx, PKT_FRAME
    mul dx                      ; **MUL WRITES DX** - which is why DX is saved
    mov si, ax
    add si, pk_ring
    mov di, eth_rxb
    push cx                     ; ...CX is the OUTPUT and rep movsb spends it
    push ds
    pop es
    cld
    rep movsb
    pop cx
    mov word [pk_len + bx], 0   ; ...the slot is free again, and this store is
                                ; the ONE word pk_recv also reads
    mov al, [pk_rd]
    inc al
    cmp al, PKT_NBUF
    jb  .rok
    xor al, al
.rok:
    mov [pk_rd], al
    clc
    jmp short .out
.empty:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pk_recv - the packet driver's receiver upcall. FAR, and in INTERRUPT CONTEXT
;
; AX = 0: it wants somewhere to put CX bytes - answer ES:DI, or ES:DI = 0 to
;         refuse, which is the spec's way of dropping a frame
; AX = 1: it has copied them; DS:SI is the buffer we handed back
;
; DS IS NOT OURS ON ENTRY and there is no telling whose it is, so both arms
; reload it from CS - which is exact in a .COM, where CS = DS = SS.
; -----------------------------------------------------------------------------
pk_recv:
    or  ax, ax
    jnz .copied
    push cs                     ; --- AX = 0: give it a buffer ---
    pop ds
    cmp cx, PKT_FRAME
    ja  .refuse                 ; too big to be an Ethernet frame
    mov [pk_plen], cx
    mov bl, [pk_wr]
    xor bh, bh
    shl bx, 1
    cmp word [pk_len + bx], 0
    jne .refuse                 ; the ring is full: DROP IT, which is exactly
                                ; what a busy network does and what TCP is
                                ; built to survive
    mov al, [pk_wr]
    xor ah, ah
    mov bx, PKT_FRAME
    mul bx
    mov di, ax
    add di, pk_ring
    push cs
    pop es
    retf
.refuse:
    xor di, di
    mov es, di
    retf
.copied:
    push cs                     ; --- AX = 1: it is there ---
    pop ds
    mov bl, [pk_wr]
    xor bh, bh
    shl bx, 1
    mov ax, [pk_plen]
    mov [pk_len + bx], ax       ; ...LAST, so ne_rx can never see a slot that
                                ; is claimed and not yet filled
    mov al, [pk_wr]
    inc al
    cmp al, PKT_NBUF
    jb  .wok
    xor al, al
.wok:
    mov [pk_wr], al
    retf

; --- pk_call - the driver, through the vector ne_probe found ----------------
; The `int` below is data as much as it is code: ne_probe writes its operand.
pk_call:
pk_intop:
    db 0xCD, 0x00
    ret                         ; ...a near ret preserves the driver's CF

pk_sig:     db 'PKT DRVR'
pk_int:     db 0                ; the vector it answered on, 0 = none found
pk_handle:  dw 0                ; access_type's, 0 = not registered
pk_wr:      db 0                ; the ring: pk_recv writes here...
pk_rd:      db 0                ; ...and ne_rx reads here
pk_plen:    dw 0                ; the length banked between the two upcalls
pk_len:     times PKT_NBUF dw 0 ; 0 = the slot is free
%ifndef ETH_NOEMIT
pk_ring:    times PKT_NBUF * PKT_FRAME db 0
%endif
