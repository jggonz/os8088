; =============================================================================
; os8088 - drivers/net/nwslv.inc
;
; THE SOCKET COMMANDS, SERVED (SPEC.md 62.11.1, docs/NET-PLAN.md 5)
;
; netsock.inc's NW_* seen from the far end of the cable. The master encodes
; them; this decodes, hands each to the SAME socket verb ETHER.DRV serves
; (ethsock.inc), and encodes the reply.
;
; **EVERY FRAME IS FIXED WHATEVER THE STATUS SAYS** (SPEC.md 62.10.1). A
; refused open still carries its handle byte and a refused recv still carries
; its length word, because the master reads a fixed number of bytes before it
; looks at the status - so a short reply on a failure would desync the wire
; and the NEXT command would be read out of this one's tail. That is the rule
; the whole protocol is built on and it is the easiest one to break here,
; where the failure paths are the ones nothing exercises.
;
; The status byte is 0 for OK and a NETE_* otherwise, which is netpkg.inc's
; own numbering - one fewer mapping to keep in step, and the same choice
; tests/lptlink/partner.py made when it played this role.
; =============================================================================

; --- nw_up - bring the stack up. CF=0 = there is a network -------------------
; out: CF=1 and SI = why not
nw_up:
    push ax
    push cx
    mov ax, ds
    mov [eth_useg], ax          ; **OURS, once.** The socket verbs reach a
                                ; caller's buffer through [eth_useg]
                                ; (ethusr.inc), and here the caller is us
    call dos_ticks
    mov [dos_seed], ax          ; ...a seed that is different every run
    call ne_probe
    jc  .nopkt
    call ne_init
    jc  .nopkt
    mov byte [eth_up], 1
    mov byte [eth_mode], ECM_DHCP
    call dhcp_begin
    mov cx, NW_DHCPW
.spin:
    call eth_pump
    call nw_ms
    cmp byte [dhcp_st], DH_BOUND
    je  .bound
    loop .spin
    mov si, s_nwnodhcp
    jmp short .fail
.bound:
    clc
    jmp short .out
.nopkt:
    mov si, s_nwnopkt
.fail:
    mov byte [eth_up], 0
    stc
.out:
    pop cx
    pop ax
    ret

; --- nw_ms - about a millisecond, so the DHCP spin is not a busy loop --------
; A packet driver receives on an INTERRUPT, so waiting is all this has to do.
nw_ms:
    push cx
    mov cx, 200
.l:
    loop .l
    pop cx
    ret

; --- nw_idle - pump the stack while the master is between commands ----------
; TCP has timers - a handshake to finish, a retransmit to make, a window to
; open - and none of them runs while this program sits in lp_rbyte_w. Every
; socket verb pumps, so a master that is polling drives the stack by itself;
; this is for the gaps between.
nw_idle:
    cmp byte [eth_up], 0
    je  .out
    call eth_pump
.out:
    ret

; =============================================================================
; THE COMMANDS. Each one: read the arguments, do it, encode the reply.
; =============================================================================

; --- NW_OPEN: port word, NET_HOSTMAX host bytes -> status, handle -----------
nw_s_open:
    call lp_rword
    jc  .io
    mov [nw_port], ax
    mov di, nw_host
    mov cx, NET_HOSTMAX
.rd:
    call lp_rbyte
    jc  .io
    mov [di], al
    inc di
    loop .rd
    mov byte [nw_host + NET_HOSTMAX - 1], 0     ; whatever arrived, ENDED: the
                                                ; name is about to be walked
    call nw_ready
    jc  .down
    mov si, nw_host
    mov cx, [nw_port]
    call eth_v_open
    jc  .no
    mov [nw_hnd], al
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
    mov byte [nw_hnd], 0
.reply:
    call nw_rep_h
.io:
    ret

; --- NW_LISTEN: port word -> status, handle ---------------------------------
nw_s_listen:
    call lp_rword
    jc  .io
    mov cx, ax
    call nw_ready
    jc  .down
    call eth_v_listen
    jc  .no
    mov [nw_hnd], al
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
    mov byte [nw_hnd], 0
.reply:
    call nw_rep_h
.io:
    ret

; --- NW_ACCEPT: handle byte -> status, handle -------------------------------
nw_s_accept:
    call lp_rbyte
    jc  .io
    call nw_ready
    jc  .down
    call eth_v_accept
    jc  .no
    mov [nw_hnd], al
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
    mov byte [nw_hnd], 0
.reply:
    call nw_rep_h
.io:
    ret

; --- NW_STAT: handle byte -> status, state byte, ready word -----------------
nw_s_stat:
    call lp_rbyte
    jc  .io
    mov byte [nw_state], NSK_FREE
    mov word [nw_rdy], 0
    call nw_ready
    jc  .down
    call eth_v_status
    jc  .no
    mov [nw_state], ah
    mov [nw_rdy], cx
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
.reply:
    mov al, [nw_st]
    call lp_sbyte
    jc  .io
    mov al, [nw_state]
    call lp_sbyte
    jc  .io
    mov ax, [nw_rdy]
    call lp_sword
.io:
    ret

; --- NW_SEND: handle, len word, bytes -> status, taken word -----------------
nw_s_send:
    call lp_rbyte
    jc  .io
    mov [nw_hnd], al
    call lp_rword
    jc  .io
    mov [nw_ann], ax            ; what the master PROMISED...
    cmp ax, NET_SKMAX
    jbe .cap
    mov ax, NET_SKMAX           ; **THE BYTES STILL COME**, all of them - see
.cap:                           ; the header. A master that promised more than
    mov [nw_len], ax            ; this asked for is its own bug and the wire
                                ; is still read to the end, or the excess is
                                ; decoded as the NEXT command's letters
    mov cx, [nw_ann]
    mov di, nw_buf
    jcxz .have
.rd:
    call lp_rbyte
    jc  .io
    cmp di, nw_buf + NET_SKMAX
    jae .skip                   ; past the cap: read and DISCARD, so the frame
    mov [di], al                ; ends where the master thinks it does
    inc di
.skip:
    loop .rd
.have:
    mov word [nw_took], 0
    call nw_ready
    jc  .down
    mov al, [nw_hnd]
    mov si, nw_buf
    mov cx, [nw_len]
    call eth_v_send
    jc  .no
    mov [nw_took], cx
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
.reply:
    mov al, [nw_st]
    call lp_sbyte
    jc  .io
    mov ax, [nw_took]
    call lp_sword
.io:
    ret

; --- NW_RECV: handle, cap word -> status, len word, len bytes ---------------
nw_s_recv:
    call lp_rbyte
    jc  .io
    mov [nw_hnd], al
    call lp_rword
    jc  .io
    cmp ax, NET_SKMAX
    jbe .cap
    mov ax, NET_SKMAX
.cap:
    mov [nw_cap], ax
    mov word [nw_len], 0
    call nw_ready
    jc  .down
    mov al, [nw_hnd]
    mov di, nw_buf
    mov cx, [nw_cap]
    call eth_v_recv
    jc  .no
    mov [nw_len], cx
    mov byte [nw_st], 0
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.no:
    mov [nw_st], al
    mov word [nw_len], 0        ; ...and the LENGTH WORD still goes, saying 0
.reply:
    mov al, [nw_st]
    call lp_sbyte
    jc  .io
    mov ax, [nw_len]
    call lp_sword
    jc  .io
    mov cx, [nw_len]
    jcxz .io
    mov si, nw_buf
.wr:
    mov al, [si]
    inc si
    call lp_sbyte
    jc  .io
    loop .wr
.io:
    ret

; --- NW_CLOSE: handle byte -> status ----------------------------------------
; It always succeeds from the caller's point of view (netpkg.inc), so the only
; status this can carry is a link that is not up at all.
nw_s_close:
    call lp_rbyte
    jc  .io
    call nw_ready
    jc  .down
    call eth_v_close
    xor al, al
    jmp short .reply
.down:
    mov al, NETE_NOLINK
.reply:
    call lp_sbyte
.io:
    ret

; --- nw_ready - is there a stack at all? CF=1 = no --------------------------
nw_ready:
    cmp byte [eth_up], 0
    je  .no
    clc
    ret
.no:
    stc
    ret

; --- nw_rep_h - the two-byte reply three commands share ---------------------
nw_rep_h:
    push ax
    mov al, [nw_st]
    call lp_sbyte
    jc  .out
    mov al, [nw_hnd]
    call lp_sbyte
.out:
    pop ax
    ret

NW_DHCPW    equ 6000            ; nw_ms passes at DHCP, so this is seconds
                                ; rather than ticks - generous, because the
                                ; run is interactive and a lease that takes
                                ; five seconds is still a lease

nw_port:    dw 0
nw_hnd:     db 0
nw_st:      db 0
nw_state:   db 0
nw_rdy:     dw 0
nw_len:     dw 0
nw_ann:     dw 0                ; what NW_SEND's master ANNOUNCED, which may be
                                ; more than nw_len: the drain is by this
nw_cap:     dw 0
nw_took:    dw 0
nw_host:    times NET_HOSTMAX db 0
%ifndef ETH_NOEMIT
nw_buf:     times NET_SKMAX db 0
%endif
