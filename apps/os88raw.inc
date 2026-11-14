; apps/os88raw.inc - a game's frames over the Ethernet card: ETHER.DRV's
; raw verbs (SPEC.md 72.22), written once. docs/plans/WIREARENA-LINK-PLAN.md
; section 12 is why it exists; WireArena is its first user, and it carries
; the same bodies apps/os88cable.inc carries on a serial cable.
;
; A FRAME: the destination's station address, ours, the includer's
; EtherType (RW_ETYPE), one byte giving the body's length, the body, and
; padding to Ethernet's 60-byte minimum. The length byte is because a
; padded frame does not say where its body ends.
;
; THE CLAIM IS EXCLUSIVE and stops ETHER.DRV's own pump - DHCP, TCP - for
; as long as it is held (72.22.1): take it when the user asks to link,
; give it back when they stop. Its verbs read no disk and may be made from
; inside a SPEC.md 53 bracket once the claim was taken before it (53.7).
; The NE2000 is POLLED: rw_poll is all the service the card gets.
;
; The includer provides:
;   RW_ETYPE   the EtherType, a word as written on the wire (88B5h ...)
;   rw_blk     RW_SIZE bytes of state (its bss)
;   rw_take    called with SI = a body, CL = its length, BX = its source's
;              station address (in rw_blk); may change any register

RW_MAC      equ 0               ; our station address (6)
RW_ON       equ 6               ; 1: the claim is held
RW_BCAST    equ 8               ; FF FF FF FF FF FF: everyone's (6)
RW_FR       equ 14              ; a frame, sent or received
RW_FRMAX    equ 64
RW_SIZE     equ RW_FR + RW_FRMAX

; rw_open - the card, claimed: ETHER.DRV attached if it was not (off the
; system disk, nothing recorded), then NETV_RAW. CF = 1 refused, AX = the
; driver layer's error (OSAPI_DRV_USE's DRVE_*) or NETE_BUSY. UI task.
; Keeps all but AX
rw_open:
    cmp byte [rw_blk+RW_ON], 0
    jne .ok
    push bx
    push di
    mov ax, (DRVU_UP << 8) | DRVC_NET
    call OSAPI_DRV_USE
    jc .out
    mov bh, DRVC_NET
    mov bl, NETV_RAW
    mov al, 1
    mov di, rw_blk+RW_MAC
    call OSAPI_DRV_CALL
    jc .out
    mov byte [rw_blk+RW_ON], 1
    mov ax, 0xFFFF
    mov [rw_blk+RW_BCAST], ax
    mov [rw_blk+RW_BCAST+2], ax
    mov [rw_blk+RW_BCAST+4], ax
    clc
.out:
    pop di
    pop bx
    ret
.ok:
    clc
    ret

; rw_close - the claim given back (ETHER.DRV's pump runs again). Keeps all
; but AX
rw_close:
    cmp byte [rw_blk+RW_ON], 0
    je .out
    push bx
    push di
    mov bh, DRVC_NET
    mov bl, NETV_RAW
    xor al, al
    xor di, di
    call OSAPI_DRV_CALL
    mov byte [rw_blk+RW_ON], 0
    pop di
    pop bx
.out:
    ret

; rw_send - SI = a body, CL = its length (at most RW_FRMAX - 15), DI = the
; station address it goes to (rw_blk+RW_BCAST: everyone). CF = 1: not sent.
; Changes AX, CX, DX, SI, DI
rw_send:
    cmp byte [rw_blk+RW_ON], 0
    je .no
    push es
    push ds
    pop es
    push si
    push cx
    mov si, di                  ; the destination
    mov di, rw_blk+RW_FR
    movsw
    movsw
    movsw
    mov si, rw_blk+RW_MAC       ; us
    movsw
    movsw
    movsw
    mov ax, RW_ETYPE
    xchg al, ah                 ; (big-endian on the wire)
    stosw
    pop cx
    pop si
    mov al, cl
    stosb
    xor ch, ch
    rep movsb
    mov cx, di                  ; padded to the minimum
    sub cx, rw_blk+RW_FR
    cmp cx, 60
    jae .len
    push cx
    neg cx
    add cx, 60
    xor al, al
    rep stosb
    pop cx
    mov cx, 60
.len:
    pop es
    push bx
    mov si, rw_blk+RW_FR
    xor dx, dx                  ; (our own segment)
    mov bh, DRVC_NET
    mov bl, NETV_RAWTX
    xor ax, ax
    call OSAPI_DRV_CALL
    pop bx
    jc .sent
    cmp ax, NETE_BUSY           ; (busy: not sent)
    jne .sent
    stc
.sent:
    ret
.no:
    stc
    ret

; rw_poll - CX = at most this many frames: each of ours drained off the
; card and handed to rw_take; others (ARP, a stray broadcast) dropped.
; Changes every register but DS, ES, BP and the stack
rw_poll:
    cmp byte [rw_blk+RW_ON], 0
    je .out
.f:
    push cx
    mov di, rw_blk+RW_FR
    mov cx, RW_FRMAX
    xor dx, dx
    mov bh, DRVC_NET
    mov bl, NETV_RAWRX
    xor ax, ax
    call OSAPI_DRV_CALL
    jc .empty
    cmp ax, NETE_BUSY           ; (CF = 0 AX = NETE_BUSY: the card busy -
    je .empty                   ; nothing this time, os88udp.inc's note)
    cmp cx, RW_FRMAX            ; (longer than any of ours: not ours)
    ja .n
    cmp cx, 15
    jb .n
    mov ax, [rw_blk+RW_FR+12]
    xchg al, ah
    cmp ax, RW_ETYPE
    jne .n
    mov al, [rw_blk+RW_FR+14]   ; the body's length, inside what came
    xor ah, ah
    add ax, 15
    cmp ax, cx
    ja .n
    mov si, rw_blk+RW_FR+15
    mov cl, [rw_blk+RW_FR+14]
    mov bx, rw_blk+RW_FR+6
    call rw_take
.n:
    pop cx
    loop .f
.out:
    ret
.empty:
    pop cx
    ret
