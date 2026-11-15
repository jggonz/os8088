; apps/os88udp.inc - a game's datagrams on ETHER.DRV's UDP verbs (SPEC.md
; 72.24), written once: the network carrier docs/plans/completed/WIREARENA-NET-PLAN.md
; section 1 chose, for matches found by a broadcast and by an address. It
; is apps/os88raw.inc's twin: the same three calls, an address of six
; bytes (an IP address, then a port as a little-endian word) where that one
; has a station address.
;
; ud_open needs the stack to have an address (DHCP bound, or static): the
; caller asks again on its next tick while ud_open answers NETE_NOLINK,
; and falls back to raw frames if it never comes.
;
; The includer provides:
;   UD_PORT    the port, both ours and the one we send to
;   ud_blk     UD_SIZE bytes of state (its bss)
;   ud_take    called with SI = a datagram, CL = its length (at most
;              UD_MAX), BX = its source's six bytes (in ud_blk)

UD_IP       equ 0               ; our address (4), then UD_PORT (2)
UD_ON       equ 6               ; 1: the port is bound
UD_BCAST    equ 8               ; 255.255.255.255:UD_PORT (6)
UD_NRX      equ 14              ; (w) datagrams received; (w) polls made
UD_NPOLL    equ 16
UD_SRC      equ 18              ; the last datagram's source (6)
UD_BUF      equ 24              ; a datagram received
UD_MAX      equ 64
UD_SIZE     equ UD_BUF + UD_MAX

; ud_open - ETHER.DRV attached if it was not, our address asked, the port
; bound. CF = 1 refused, AX = the driver layer's error, or NETE_NOLINK: no
; address yet (ask again). UI task. Keeps all but AX
ud_open:
    cmp byte [ud_blk+UD_ON], 0
    jne .ok
    push bx
    push cx
    push di
    mov ax, (DRVU_UP << 8) | DRVC_NET
    call OSAPI_DRV_USE
    jc .out
    mov bh, DRVC_NET
    mov bl, NETV_ADDR
    mov di, ud_blk+UD_IP
    call OSAPI_DRV_CALL         ; CF = 1: no address yet
    jc .out
    mov bh, DRVC_NET
    mov bl, NETV_UDP
    mov al, 1
    mov cx, UD_PORT
    call OSAPI_DRV_CALL
    jc .out
    mov byte [ud_blk+UD_ON], 1
    mov word [ud_blk+UD_IP+4], UD_PORT
    mov ax, 0xFFFF
    mov [ud_blk+UD_BCAST], ax
    mov [ud_blk+UD_BCAST+2], ax
    mov word [ud_blk+UD_BCAST+4], UD_PORT
    clc
.out:
    pop di
    pop cx
    pop bx
    ret
.ok:
    clc
    ret

; ud_close - the port released. Keeps all but AX
ud_close:
    cmp byte [ud_blk+UD_ON], 0
    je .out
    push bx
    mov bh, DRVC_NET
    mov bl, NETV_UDP
    xor al, al
    call OSAPI_DRV_CALL
    mov byte [ud_blk+UD_ON], 0
    pop bx
.out:
    ret

; ud_send - SI = a datagram, CL = its length, DI = the six bytes it goes to
; (ud_blk+UD_BCAST: everyone on the network). CF = 1: not sent (no ARP
; answer yet, or the card busy - the next one will be). Changes AX, CX
;
; **CF = 0 WITH AX = NETE_BUSY IS NOT SUCCESS**: ETHER.DRV's dispatcher
; answers a call that finds the card busy (its own timers) so, and a
; caller that read CF alone took a busy for a datagram (WIREARENA-NET-PLAN
; 9.2). Both calls here clear AX first and test it.
ud_send:
    cmp byte [ud_blk+UD_ON], 0
    je .no
    push bx
    xor ch, ch
    mov bh, DRVC_NET
    mov bl, NETV_UDPTX
    xor ax, ax
    call OSAPI_DRV_CALL
    pop bx
    jc .out
    cmp ax, NETE_BUSY           ; (busy: not sent - CF = 1)
    jne .out
    stc
.out:
    ret
.no:
    stc
    ret

; ud_poll - CX = at most this many datagrams: each handed to ud_take with
; its source. Changes every register but DS, ES, BP and the stack
ud_poll:
    cmp byte [ud_blk+UD_ON], 0
    je .out
    inc word [ud_blk+UD_NPOLL]
.d:
    push cx
    mov di, ud_blk+UD_SRC       ; the source's six bytes, then the datagram
    mov cx, 6 + UD_MAX          ; (UD_BUF follows UD_SRC)
    mov bh, DRVC_NET
    mov bl, NETV_UDPRX
    xor ax, ax
    call OSAPI_DRV_CALL
    jc .empty
    cmp ax, NETE_BUSY           ; CF = 0 AX = NETE_BUSY: the card was busy
    je .empty                   ; (its own timers): nothing this time
    inc word [ud_blk+UD_NRX]
    cmp cx, UD_MAX              ; (longer than any of ours: not ours)
    ja .n
    mov si, ud_blk+UD_BUF
    mov bx, ud_blk+UD_SRC
    call ud_take
.n:
    pop cx
    loop .d
.out:
    ret
.empty:
    pop cx
    ret
