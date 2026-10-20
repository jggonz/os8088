; WireFleet protocol v1 (SPEC.md 114.6). One acknowledged turn, no private
; fleet on the wire. Transport is the OS lease (115), never a private ISR.
WF_PACKET equ 64
WF_HELLO equ 0
WF_SYNC equ 1
WF_SHOT equ 2
WF_RESULT equ 3
WF_CHAT equ 4
; 2400 baud, Pong's rate (SPEC.md 114.3): a byte is 4.2 ms on the wire, so an
; XT's one-byte 8250 has four times the time 9600 gave it to be read - 86Box
; XTs lost ~9% of bytes at 9600 and no frame survived. A frame is 66 bytes,
; 275 ms; a heartbeat every nine ticks keeps the line half idle, and SHOT,
; RESULT and CHAT still go at once (urgent).
WF_DIVISOR equ 48
WF_BEAT equ 9

; Local timing entropy. The PIT is latched, never reprogrammed. Call anew
; when placing ships: a discovery identity has already been made public.
wf_entropy:
    call OSAPI_GET_TICKS
    mov bx, ax
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xor ax, bx
    ret

wf_link_new:
    call OSAPI_RAND
    mov [wf_identity], ax
    call wf_entropy
    or ax, 1
    mov [wf_identity+2], ax
    xor ax, ax
    mov [wf_peer], ax
    mov [wf_peer+2], ax
    mov [wf_match], ax
    mov [wf_match+2], ax
    mov [wf_turn], ax
    mov [wf_peerturn], ax
    mov [wf_chatrxseq], ax
    mov [wf_connected], al
    mov [wf_peerready], al
    mov [wf_conflict], al
    mov [wf_pending], al
    mov [wf_cachevalid], al
    mov [wf_chatpending], al
    mov [wf_finished], al
    mov [wf_enemysunk], al
    mov [wf_chatmode], al
    mov [wf_input], al
    mov [wf_line+LN_LEN], ax
    mov [wf_line+LN_CAR], ax
    mov [wf_line+LN_VIEW], ax
    mov [wf_line+LN_FOCUS], al
    mov [wf_chatcount], ax
    mov byte [wf_lastout], 0xff
    mov byte [wf_chatline], 0
    mov byte [wf_chatlog], 0
    mov byte [wf_chatlog+40], 0
    mov byte [wf_chatlog+80], 0
    mov [wf_rlen], al
    mov [wf_rescape], al
    mov [wf_txlen], ax
    mov [wf_txpos], ax
    mov word [wf_hash], 0xffff
    mov word [wf_chatseq], 1
    mov byte [wf_urgent], 1
    ret

; AL=COM number, from the Link menu: the search is PINNED to that port
; (114.9). Retains the match; switching a port is reconnecting it.
wf_connect:
    mov [wf_wantport], al
    mov byte [wf_linkmode], 2
    mov cl, al
    mov byte [wf_cands], cl     ; COM1 = bit 0 = 1, COM2 = bit 1 = 2
    call wf_disconnect
    mov al, [wf_wantport]
    call wf_lease
    jc .failed
    mov word [wf_message], wf_searching
    jmp wf_update
.failed:
    call wf_leaseerror
    jmp wf_update

; Link -> Automatic, and the window's first tick: search both ports.
wf_autostart:
    mov byte [wf_linkmode], 1
    mov byte [wf_cands], 3
    mov byte [wf_heard], 0
    call OSAPI_GET_TICKS
    mov [wf_retry], ax          ; due now
    ret

; AL=port. Opens, or MOVES a lease this instance already holds (115.1).
; CF=refused with AX=error and the lease closed; else the port is live.
wf_lease:
    mov [wf_curport], al
    cmp byte [wf_linkopen], 0
    jne .open
    push ax
    mov dx, cs
    xor ax, ax
    call OSAPI_MEM_MOVABLE      ; pin the region before publishing its ring
    pop ax
.open:
    mov bl, al
    mov bh, WF_DIVISOR
    xor ax, ax
    mov si, wf_serialbuf
    call OSAPI_SERIAL
    jc .refused
    mov byte [wf_linkopen], 1
    mov byte [wf_urgent], 1
    mov byte [wf_heard], 0
    mov byte [wf_rlen], 0
    mov byte [wf_rescape], 0
    mov word [wf_txlen], 0
    mov word [wf_txpos], 0
    call OSAPI_GET_TICKS
    mov [wf_seen], ax
    sub ax, WF_BEAT
    mov [wf_sendtime], ax
    call OSAPI_RAND             ; 40..71 ticks on this port before moving,
    and ax, 31                  ; drawn afresh at every move so two searches
    add ax, 40                  ; cannot stay in step on opposite ports
    mov [wf_dwell], ax
    call OSAPI_GET_TICKS
    add [wf_dwell], ax
    mov byte [wf_dirty], 1
    clc
    ret
.refused:
    push ax
    mov byte [wf_linkopen], 0
    mov byte [wf_connected], 0
    OS88_REGION_MOVABLE
    pop ax
    stc
    ret

; AX=lease error -> the line that says what to do about it.
wf_leaseerror:
    cmp ax, 8
    jbe .known
    mov ax, 5
.known:
    shl ax, 1
    mov bx, ax
    mov ax, [wf_linkerrors+bx]
    mov [wf_message], ax
    mov byte [wf_dirty], 1
    ret

; One tick of the automatic search (114.9). Only mode 1 moves anything.
wf_autolink:
    cmp byte [wf_linkmode], 1
    jne .out
    cmp byte [wf_linkopen], 0
    jne .live
    mov ax, [wf_now]
    sub ax, [wf_retry]
    js .out                     ; a refusal is waited out
    mov al, [wf_cands]
    or al, al
    jz .none
    mov ah, [wf_wantport]       ; the chosen or saved port first, then COM2
    or ah, ah                   ; (COM1 is the mouse on most machines)
    jnz .first
    mov ah, 2
.first:
    test al, ah
    jnz .try
    xor ah, 3
.try:
    mov al, ah
    call wf_lease
    jnc .opened
    call wf_refusal
.out:
    ret
.opened:
    cmp word [wf_message], wf_loadedmessage
    je .out                     ; a loaded match keeps saying what it waits for
    mov word [wf_message], wf_searching
    ret
.none:
    mov byte [wf_linkmode], 0
    mov word [wf_message], wf_nofreeport
    mov byte [wf_dirty], 1
    ret
.live:
    cmp byte [wf_heard], 0
    jne .found
    cmp byte [wf_cands], 3      ; one candidate: nothing to move between
    jne .out
    mov ax, [wf_now]
    sub ax, [wf_dwell]
    js .out
    mov al, [wf_curport]
    xor al, 3
    call wf_lease               ; MOVE to the other port
    jnc .opened
    call wf_refusal             ; that one is gone; the next tick reopens
    ret                         ; the remaining one (the lease is closed)
.found:
    mov al, [wf_curport]        ; a WireFleet answered here: this is the
    mov [wf_wantport], al       ; port, for this search and for Save
    mov [wf_cands], al
    ret

; AX=refusal during a search. 3/4: this port is not a candidate. 1/2:
; wait and retry. Anything else ends the search with its line.
wf_refusal:
    cmp ax, 3                   ; the mouse's or no UART: a candidate fewer,
    je .drop                    ; which a search expects - said only when
    cmp ax, 4                   ; none is left (.drop's end)
    je .drop
    push ax
    call wf_leaseerror
    pop ax
    mov bx, [wf_now]
    add bx, 18
    mov [wf_retry], bx
    cmp ax, 2
    jbe .out
    mov byte [wf_linkmode], 0
.out:
    ret
.drop:
    mov bx, [wf_now]
    mov [wf_retry], bx          ; the next candidate, at once
    mov al, [wf_curport]
    not al
    and [wf_cands], al
    mov ah, [wf_cands]
    or ah, ah
    jnz .out
    mov byte [wf_linkmode], 0
    mov word [wf_message], wf_nofreeport
    mov byte [wf_dirty], 1
    ret

; Link > Statistics: what the cable delivered, as a line of news.
wf_stats:
    push ds
    pop es
    mov di, wf_statline
    mov si, wf_stlabel
    call wf_c_copy
    mov ax, [wf_strx]
    call dec_u16
    mov si, wf_sterrlabel
    call wf_c_copy
    mov ax, [wf_sterr]
    call dec_u16
    mov si, wf_stbitslabel
    call wf_c_copy
    mov al, [wf_stbits]
    call wf_hexbyte
    mov si, wf_stframelabel
    call wf_c_copy
    mov ax, [wf_stframes]
    call dec_u16
    mov si, wf_strefusedlabel
    call wf_c_copy
    mov ax, [wf_badframes]
    call dec_u16
    mov si, wf_sthellolabel
    call wf_c_copy
    mov ax, [wf_sthello]
    call dec_u16
    mov byte [di], 0
    mov word [wf_message], wf_statline
    mov word [wf_footershown], 0   ; the same pointer, new text: redraw it
    mov byte [wf_dirty], 1
    ret
wf_hexbyte:
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call .digit
    pop ax
    and al, 15
.digit:
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 7
.put:
    stosb
    ret

wf_disconnect:
    cmp byte [wf_linkopen], 0
    je .out
    mov al, 1
    call OSAPI_SERIAL
    mov byte [wf_linkopen], 0
    mov byte [wf_connected], 0
    OS88_REGION_MOVABLE
    mov word [wf_message], wf_linkpaused
    mov byte [wf_dirty], 1
.out:
    ret

wf_tick:
    push si
    call wf_poll
    cmp byte [wf_dirty], 0
    je .timer
    mov byte [wf_dirty], 0
    cmp byte [wf_abon], 0
    jne .timer
    call wf_update
.timer:
    mov bx, [wf_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    pop si
    ret

wf_poll:
    call wf_soundtick
    call OSAPI_GET_TICKS
    mov [wf_now], ax
    call wf_autolink
    cmp byte [wf_linkopen], 0
    je .out
    mov byte [wf_rxwork], 128
.rx:
    mov al, 2
    call OSAPI_SERIAL
    jc .schedule
    inc word [wf_strx]          ; Link > Statistics
    or ah, ah
    jz .byte
    inc word [wf_sterr]
    or [wf_stbits], ah
    mov byte [wf_rlen], 255
    mov byte [wf_rescape], 0
.byte:
    call wf_receive
    dec byte [wf_rxwork]
    jnz .rx
.schedule:
    call OSAPI_GET_TICKS
    mov [wf_now], ax
    sub ax, [wf_seen]
    cmp ax, 55
    jb .tx
    cmp byte [wf_connected], 0
    je .tx
    mov byte [wf_connected], 0
    mov byte [wf_dirty], 1
    mov word [wf_message], wf_linkpaused
.tx:
    ; A partially queued frame always finishes before another is made.
    call wf_flush
    mov ax, [wf_txpos]
    cmp ax, [wf_txlen]
    jb .out
    mov ax, [wf_now]
    sub ax, [wf_sendtime]
    cmp ax, WF_BEAT
    jae .make
    cmp byte [wf_urgent], 0
    je .out
.make:
    mov byte [wf_urgent], 0
    mov ax, [wf_now]
    mov [wf_sendtime], ax
    call wf_packet_make
    cmp word [wf_peer], 0
    jne .bound
    cmp word [wf_peer+2], 0
    jne .bound
    mov byte [wf_frame+3], WF_HELLO
    jmp .encode
.bound:
    ; Interleave SYNC with outstanding commands: readiness, turn ACK and
    ; chat ACK cannot be starved by a lost command or its lost reply.
    inc byte [wf_sendkind]
    cmp byte [wf_connected], 0
    jne .commands
    test byte [wf_sendkind], 3
    jnz .commands
    mov byte [wf_frame+3], WF_HELLO
    jmp .encode
.commands:
    test byte [wf_sendkind], 1
    jz .encode
    cmp byte [wf_cachevalid], 0
    je .shot
    mov ax, [wf_cacheturn]
    cmp ax, [wf_peerturn]
    jne .shot
    inc ax
    cmp ax, [wf_turn]
    jne .shot
    mov byte [wf_frame+3], WF_RESULT
    mov ax, [wf_cacheturn]
    mov [wf_frame+16], ax
    mov si, wf_cachecell
    mov di, wf_frame+19
    mov cx, 5
    push ds
    pop es
    rep movsb
    jmp .encode
.shot:
    cmp byte [wf_pending], 0
    je .chat
    mov byte [wf_frame+3], WF_SHOT
    mov al, [wf_pendingcell]
    mov [wf_frame+19], al
    jmp .encode
.chat:
    cmp byte [wf_chatpending], 0
    je .encode
    mov byte [wf_frame+3], WF_CHAT
    mov ax, [wf_chatseq]
    mov [wf_frame+24], ax
    mov al, [wf_chatlength]
    mov [wf_frame+26], al
    xor ah, ah
    mov cx, ax
    mov si, wf_chattext
    mov di, wf_frame+27
    push ds
    pop es
    rep movsb
.encode:
    call wf_encode
    call wf_flush
.out:
    ret

wf_packet_make:
    push ds
    pop es
    mov di, wf_frame
    xor ax, ax
    mov cx, WF_PACKET/2
    rep stosw
    mov word [wf_frame], 'WF'
    mov byte [wf_frame+2], 1
    mov byte [wf_frame+3], WF_SYNC
    mov si, wf_identity
    mov di, wf_frame+4
    mov cx, 12                 ; adjacent identity, peer, match dwords
    rep movsb
    mov ax, [wf_turn]
    mov [wf_frame+16], ax
    mov al, [wf_ready]
    mov [wf_frame+18], al
    mov ax, [wf_chatrxseq]
    mov [wf_frame+24], ax
    mov ax, [wf_hash]
    mov [wf_frame+59], ax
    ret

wf_encode:
    mov ax, 0xffff
    mov si, wf_frame
    mov cx, 62
    call wf_crc
    mov [wf_frame+62], ax
    mov si, wf_frame
    mov di, wf_txbuf
    mov al, 0x7e
    stosb
    mov cx, WF_PACKET
.byte:
    lodsb
    cmp al, 0x7e
    je .escape
    cmp al, 0x7d
    jne .store
.escape:
    mov ah, al
    mov al, 0x7d
    stosb
    mov al, ah
    xor al, 0x20
.store:
    stosb
    loop .byte
    mov al, 0x7e
    stosb
    sub di, wf_txbuf
    mov [wf_txlen], di
    mov word [wf_txpos], 0
    ret

wf_flush:
    mov bx, [wf_txpos]
.byte:
    cmp bx, [wf_txlen]
    jae .out
    mov cl, [wf_txbuf+bx]
    mov al, 3
    call OSAPI_SERIAL
    jc .out
    inc bx
    mov [wf_txpos], bx
    jmp .byte
.out:
    ret

; A malformed frame stays poisoned until a delimiter. A delimiter after an
; incomplete escape cannot commit it. CRC is checked before any game action.
wf_receive:
    cmp al, 0x7e
    je .delimiter
    cmp byte [wf_rlen], WF_PACKET
    jae .bad
    cmp byte [wf_rescape], 0
    jne .escaped
    cmp al, 0x7d
    jne .store
    mov byte [wf_rescape], 1
    ret
.escaped:
    xor al, 0x20
    mov byte [wf_rescape], 0
.store:
    xor bx, bx
    mov bl, [wf_rlen]
    mov [wf_rxbuf+bx], al
    inc byte [wf_rlen]
    ret
.bad:
    mov byte [wf_rlen], 255
    mov byte [wf_rescape], 0
    ret
.delimiter:
    cmp byte [wf_rescape], 0
    jne .reset
    cmp byte [wf_rlen], WF_PACKET
    jne .reset
    inc word [wf_stframes]
    call wf_packet_take
.reset:
    mov byte [wf_rlen], 0
    mov byte [wf_rescape], 0
    ret

wf_packet_take:
    mov si, wf_rxbuf
    mov ax, 0xffff
    mov cx, 62
    call wf_crc
    cmp ax, [wf_rxbuf+62]
    jne .bad
    cmp word [wf_rxbuf], 'WF'
    jne .bad
    cmp byte [wf_rxbuf+2], 1
    jne .bad
    cmp byte [wf_rxbuf+3], WF_CHAT
    ja .bad
    cmp byte [wf_rxbuf+61], 0
    jne .bad
    cmp byte [wf_rxbuf+18], 1
    ja .bad
    cmp word [wf_rxbuf+16], 200
    ja .bad
    mov si, wf_rxbuf+4          ; a WireFleet that is not our own echo is
    mov di, wf_identity         ; on this port: the search can stop (114.9)
    call wf_equal4
    je .heardself
    mov byte [wf_heard], 1
.heardself:
    mov ax, [wf_rxbuf+8]
    or ax, [wf_rxbuf+10]
    jz .unaddressed
    mov si, wf_rxbuf+8
    mov di, wf_identity
    call wf_equal4
    jne .bad
    jmp .addressed
.unaddressed:
    cmp byte [wf_rxbuf+3], WF_HELLO
    jne .bad
.addressed:
    cmp byte [wf_rxbuf+3], WF_HELLO
    je wf_hello_take
    mov si, wf_rxbuf+4
    mov di, wf_peer
    call wf_equal4
    jne .bad
    mov si, wf_rxbuf+12
    mov di, wf_match
    call wf_equal4
    jne .bad
    cmp byte [wf_rxbuf+3], WF_SYNC
    je wf_sync_take
    cmp byte [wf_rxbuf+3], WF_SHOT
    je wf_shot_take
    cmp byte [wf_rxbuf+3], WF_RESULT
    je wf_result_take
    jmp wf_chat_take
.bad:
    inc word [wf_badframes]
    ret

wf_equal4:
    mov ax, [si]
    cmp ax, [di]
    jne .out
    mov ax, [si+2]
    cmp ax, [di+2]
.out:
    ret

wf_hello_take:
    inc word [wf_sthello]
    mov ax, [wf_rxbuf+4]
    or ax, [wf_rxbuf+6]
    jz wf_packet_take.bad
    mov si, wf_rxbuf+4
    mov di, wf_identity
    call wf_equal4
    jne .different
    cmp word [wf_turn], 0
    jne wf_packet_take.bad
    call wf_link_new
    ret
.different:
    mov ax, [wf_peer]
    or ax, [wf_peer+2]
    jz .bind
    mov si, wf_rxbuf+4
    mov di, wf_peer
    call wf_equal4
    jne .stranger
    mov byte [wf_urgent], 1
    ret
.stranger:
    ; 114.9: before a result is committed there is nothing to protect, so
    ; an opponent that relaunched or chose New fleet is simply found again.
    cmp byte [wf_finished], 0
    jne .rematch
    cmp word [wf_turn], 0
    jne wf_packet_take.bad
    cmp byte [wf_pending], 0
    jne wf_packet_take.bad
    cmp byte [wf_cachevalid], 0
    jne wf_packet_take.bad
    xor ax, ax
    mov [wf_peerready], al
    mov [wf_connected], al
    mov [wf_chatrxseq], ax
    mov word [wf_chatseq], 1
    jmp .bind
.rematch:
    cmp word [wf_message], wf_rematch
    je wf_packet_take.bad
    mov word [wf_message], wf_rematch
    mov byte [wf_dirty], 1
    jmp wf_packet_take.bad
.bind:
    push ds
    pop es
    mov si, wf_rxbuf+4
    mov di, wf_peer
    mov cx, 4
    rep movsb
    mov byte [wf_side], 0
    mov si, wf_identity
    mov ax, [wf_identity+2]
    cmp ax, [wf_peer+2]
    jb .host
    ja .guest
    mov ax, [wf_identity]
    cmp ax, [wf_peer]
    jb .host
.guest:
    mov byte [wf_side], 1
    mov si, wf_peer
.host:
    mov di, wf_match
    mov cx, 4
    rep movsb
    mov byte [wf_urgent], 1
    mov byte [wf_dirty], 1
    ret

wf_touch:
    call OSAPI_GET_TICKS
    mov [wf_seen], ax
    ret

wf_sync_take:
    mov ax, [wf_rxbuf+16]
    mov dx, ax
    sub dx, [wf_turn]
    cmp dx, 0xffff
    je .valid
    cmp dx, 1
    ja wf_history_conflict
    or dx, dx
    jnz .valid
    mov dx, [wf_rxbuf+59]
    cmp dx, [wf_hash]
    jne wf_history_conflict
.valid:
    cmp ax, [wf_peerturn]
    je .samepeerturn
    mov byte [wf_dirty], 1
.samepeerturn:
    mov [wf_peerturn], ax
    mov al, [wf_rxbuf+18]
    cmp al, [wf_peerready]
    je .ready
    mov [wf_peerready], al
    mov byte [wf_dirty], 1
    cmp byte [wf_connected], 0
    je .ready
    call wf_readyline
.ready:
    cmp byte [wf_conflict], 0
    jne .out
    cmp byte [wf_connected], 0
    jne .chatack
    mov byte [wf_connected], 1
    mov byte [wf_dirty], 1
    mov word [wf_message], wf_linkrestored
    cmp word [wf_turn], 0
    jne .cue
    call wf_readyline
.cue:
    mov al, 7
    call wf_sfx
.chatack:
    cmp byte [wf_chatpending], 0
    je .seen
    mov ax, [wf_rxbuf+24]
    cmp ax, [wf_chatseq]
    jne .seen
    mov byte [wf_chatpending], 0
    inc word [wf_chatseq]
    mov byte [wf_dirty], 1
.seen:
    call wf_touch
.out:
    ret

; Linked, before the first shot: say whose move it is (114.9).
wf_readyline:
    cmp word [wf_turn], 0
    jne .out
    cmp byte [wf_finished], 0
    jne .out
    mov ax, wf_foundplace
    cmp byte [wf_peerready], 0
    je .mine
    mov ax, wf_peerreadyline
.mine:
    cmp byte [wf_ready], 0
    je .set
    mov ax, wf_waitpeer
    cmp byte [wf_peerready], 0
    je .set
    mov ax, wf_battlefirst
    cmp byte [wf_side], 0
    je .set
    mov ax, wf_battlesecond
.set:
    mov [wf_message], ax
    mov byte [wf_dirty], 1
.out:
    ret

wf_history_conflict:
    mov byte [wf_conflict], 1
    mov byte [wf_connected], 0
    mov byte [wf_dirty], 1
    mov word [wf_message], wf_historyerror
    ret

wf_canfire:
    cmp byte [wf_connected], 1
    jne .bad
    cmp byte [wf_ready], 1
    jne .bad
    cmp byte [wf_peerready], 1
    jne .bad
    cmp byte [wf_pending], 0
    jne .bad
    cmp byte [wf_conflict], 0
    jne .bad
    cmp byte [wf_finished], 0
    jne .bad
    mov ax, [wf_turn]
    cmp ax, [wf_peerturn]
    jne .bad
    and al, 1
    cmp al, [wf_side]
    jne .bad
    xor bx, bx
    mov bl, [wf_target]
    cmp byte [wf_outshots+bx], 0
    jne .bad
    clc
    ret
.bad:
    stc
    ret
wf_fire:
    call wf_queue_shot
    jc .out
    jmp wf_update
.out:
    ret
wf_queue_shot:
    call wf_canfire
    jc .out
    mov al, [wf_target]
    mov [wf_pendingcell], al
    mov byte [wf_pending], 1
    mov byte [wf_urgent], 1
    mov word [wf_message], wf_shotpending
    mov al, 3
    call wf_sfx
    mov byte [wf_dirty], 1
    clc
.out:
    ret

wf_shot_take:
    cmp byte [wf_ready], 1
    jne wf_packet_take.bad
    cmp byte [wf_rxbuf+18], 1
    jne wf_packet_take.bad
    cmp byte [wf_conflict], 0
    jne wf_packet_take.bad
    cmp byte [wf_rxbuf+19], 100
    jae wf_packet_take.bad
    mov ax, [wf_rxbuf+16]
    cmp byte [wf_cachevalid], 0
    je .fresh
    cmp ax, [wf_cacheturn]
    jne .fresh
    mov al, [wf_rxbuf+19]
    cmp al, [wf_cachecell]
    jne wf_history_conflict
    mov byte [wf_urgent], 1
    jmp wf_touch
.fresh:
    cmp ax, [wf_turn]
    jne wf_packet_take.bad
    and al, 1
    cmp al, [wf_side]
    je wf_packet_take.bad        ; only the other captain may shoot us
    cmp byte [wf_finished], 0
    jne wf_packet_take.bad
    xor bx, bx
    mov bl, [wf_rxbuf+19]
    cmp byte [wf_inshots+bx], 0
    jne wf_packet_take.bad
    mov ax, [wf_rxbuf+59]
    cmp ax, [wf_hash]
    jne wf_history_conflict
    mov al, [wf_rxbuf+19]
    mov [wf_cachecell], al
    mov ax, [wf_turn]
    mov [wf_cacheturn], ax
    mov byte [wf_cachevalue], 1
    mov byte [wf_cacheship], 0
    mov byte [wf_cacheanchor], 0
    mov byte [wf_cachedir], 0
    mov byte [wf_inshots+bx], 1
    mov al, [wf_board+bx]
    or al, al
    jz .commit
    mov byte [wf_cachevalue], 2
    mov byte [wf_inshots+bx], 2
    mov dh, al
    mov bx, 0
.afloat:
    cmp [wf_board+bx], dh
    jne .next
    cmp byte [wf_inshots+bx], 2
    jb .commit
.next:
    inc bx
    cmp bx, 100
    jb .afloat
    mov byte [wf_cachevalue], 3
    mov [wf_cacheship], dh
    mov al, dh
    dec al
    xor ah, ah
    mov bx, ax
    mov al, [wf_anchors+bx]
    mov [wf_cacheanchor], al
    mov al, [wf_dirs+bx]
    mov [wf_cachedir], al
    xor bx, bx
.sunk:
    cmp [wf_board+bx], dh
    jne .nextsunk
    mov byte [wf_inshots+bx], 3
.nextsunk:
    inc bx
    cmp bx, 100
    jb .sunk
.commit:
    ; Canonical result fields go into the validated receive buffer only
    ; after judgment; no remote field chooses the local outcome.
    push ds
    pop es
    mov si, wf_cachecell
    mov di, wf_rxbuf+19
    mov cx, 5
    rep movsb
    call wf_history
    mov [wf_hash], ax
    inc word [wf_turn]
    mov byte [wf_cachevalid], 1
    mov byte [wf_dirty], 1
    mov byte [wf_urgent], 1
    mov al, [wf_cachevalue]
    add al, 3
    call wf_sfx
    call wf_checkend
    jmp wf_touch

wf_result_take:
    cmp byte [wf_pending], 1
    jne .out                    ; duplicate ACK cannot advance again
    cmp byte [wf_conflict], 0
    jne .out
    mov ax, [wf_rxbuf+16]
    cmp ax, [wf_turn]
    jne .out
    mov al, [wf_rxbuf+19]
    cmp al, [wf_pendingcell]
    jne wf_packet_take.bad
    mov al, [wf_rxbuf+20]
    cmp al, 1
    jb wf_packet_take.bad
    cmp al, 3
    ja wf_packet_take.bad
    cmp al, 3
    je .reveal
    mov al, [wf_rxbuf+21]
    or al, [wf_rxbuf+22]
    or al, [wf_rxbuf+23]
    jnz wf_packet_take.bad
    jmp .hash
.reveal:
    call wf_reveal_check
    jc wf_packet_take.bad
.hash:
    call wf_history
    cmp ax, [wf_rxbuf+59]
    jne wf_history_conflict
    mov [wf_hash], ax
    xor bx, bx
    mov bl, [wf_pendingcell]
    mov al, [wf_rxbuf+20]
    mov [wf_outshots+bx], al
    mov [wf_lastout], bl        ; 114.10's "You fired F7: HIT!"
    cmp al, 3
    jne .commit
    call wf_reveal_apply
.commit:
    inc word [wf_turn]
    mov byte [wf_pending], 0
    mov byte [wf_dirty], 1
    mov byte [wf_urgent], 1
    mov word [wf_message], wf_resultmessage
    mov al, [wf_rxbuf+20]
    add al, 3
    call wf_sfx
    call wf_checkend
    jmp wf_touch
.out:
    ret

; Validate a sunk reveal against all the hits we already know. Current
; target may be the final hit; every other square must be a previous hit.
wf_reveal_check:
    mov al, [wf_rxbuf+21]
    cmp al, 1
    jb .bad
    cmp al, 5
    ja .bad
    dec al
    xor ah, ah
    mov bx, ax
    mov cl, al
    mov al, 1
    shl al, cl
    test [wf_enemysunk], al
    jnz .bad
    mov cl, [wf_lengths+bx]
    xor ch, ch
    mov al, [wf_rxbuf+22]
    cmp al, 100
    jae .bad
    xor ah, ah
    mov dl, 10
    div dl
    mov dl, [wf_rxbuf+23]
    cmp dl, 1
    ja .bad
    or dl, dl
    jz .edge
    mov ah, al
.edge:
    add ah, cl
    cmp ah, 10
    ja .bad
    xor bx, bx
    mov bl, [wf_rxbuf+22]
    mov dx, 1
    cmp byte [wf_rxbuf+23], 0
    je .check
    mov dx, 10
.check:
    cmp bl, [wf_pendingcell]
    je .next
    cmp byte [wf_outshots+bx], 2
    jne .bad
.next:
    add bx, dx
    loop .check
    clc
    ret
.bad:
    stc
    ret
wf_reveal_apply:
    mov al, [wf_rxbuf+21]
    dec al
    xor ah, ah
    mov bx, ax
    mov cl, al
    mov al, 1
    shl al, cl
    or [wf_enemysunk], al
    mov cl, [wf_lengths+bx]
    xor ch, ch
    xor bx, bx
    mov bl, [wf_rxbuf+22]
    mov dx, 1
    cmp byte [wf_rxbuf+23], 0
    je .mark
    mov dx, 10
.mark:
    mov byte [wf_outshots+bx], 3
    add bx, dx
    loop .mark
    ret

wf_checkend:
    cmp byte [wf_finished], 0
    jne .out
    xor bx, bx
    xor cx, cx
.cell:
    cmp byte [wf_outshots+bx], 2
    jb .own
    inc cl
.own:
    cmp byte [wf_inshots+bx], 2
    jb .next
    inc ch
.next:
    inc bx
    cmp bx, 100
    jb .cell
    cmp cl, 17
    jne .lost
    mov byte [wf_finished], 1
    mov word [wf_message], wf_won
    mov al, 8
    call wf_sfx
.lost:
    cmp ch, 17
    jne .out
    mov byte [wf_finished], 2
    mov word [wf_message], wf_lost
    mov al, 9
    call wf_sfx
.out:
    ret

wf_chat_take:
    mov al, [wf_rxbuf+26]
    cmp al, 1
    jb wf_packet_take.bad
    cmp al, 32
    ja wf_packet_take.bad
    xor ah, ah
    mov cx, ax
    mov si, wf_rxbuf+27
.ascii:
    lodsb
    cmp al, 32
    jb wf_packet_take.bad
    cmp al, 126
    ja wf_packet_take.bad
    loop .ascii
    mov ax, [wf_rxbuf+24]
    cmp ax, [wf_chatrxseq]
    je .ack
    mov dx, [wf_chatrxseq]
    inc dx
    cmp ax, dx
    jne wf_packet_take.bad
    mov [wf_chatrxseq], ax
    push ds
    pop es
    mov si, wf_peerprefix
    mov di, wf_chatline
    mov cx, 6
    rep movsb
    mov si, wf_rxbuf+27
    xor cx, cx
    mov cl, [wf_rxbuf+26]
    rep movsb
    mov byte [di], 0
    call wf_chat_append
    mov byte [wf_dirty], 1
.ack:
    mov byte [wf_urgent], 1
    jmp wf_touch

wf_history:
    push ds
    pop es
    mov di, wf_canonical
    mov ax, [wf_rxbuf+16]
    stosw
    mov si, wf_rxbuf+19
    mov cx, 5
    rep movsb
    mov si, wf_canonical
    mov cx, 7
    mov ax, [wf_hash]
    jmp wf_crc

; CRC-16/CCITT-FALSE: AX seed, SI bytes, CX count. AX result.
wf_crc:
.byte:
    xor ah, [si]
    inc si
    mov dl, 8
.bit:
    shl ax, 1
    jnc .next
    xor ax, 0x1021
.next:
    dec dl
    jnz .bit
    loop .byte
    ret

wf_linkerrors: dw wf_searching,wf_linkbusy,wf_movemouse,wf_mouseport,wf_noport,wf_badbuffer,wf_searching,wf_searching,wf_needsserial
wf_searching: db 'Looking for your opponent on the null-modem cable...',0
wf_linkbusy: db 'Another program is using the serial port. Trying again...',0
wf_movemouse: db 'Move the mouse once so the OS can tell it from the cable.',0
wf_mouseport: db 'That COM port is the mouse. Connect using the other port.',0
wf_noport: db 'No supported BIOS-advertised UART on that port.',0
wf_badbuffer: db 'Serial lease refused this application buffer.',0
wf_needsserial: db 'Linking needs SERIAL.DRV: insert the system disk, Link > Automatic.',0
wf_nofreeport: db 'No free serial port for the cable. Link menu: choose a port.',0
wf_stlabel: db 'RX ',0
wf_sterrlabel: db ', err ',0
wf_stbitslabel: db ' (bits ',0
wf_stframelabel: db '), frames ',0
wf_strefusedlabel: db ', refused ',0
wf_sthellolabel: db ', hello ',0
wf_rematch: db 'Your opponent started a new match. Press N to join it.',0
wf_foundplace: db 'Opponent found! Place your fleet, then Ready [Y].',0
wf_peerreadyline: db 'Opponent is ready and waiting. Place your fleet, then Ready.',0
wf_waitpeer: db 'Fleet locked. Waiting for your opponent to finish placing.',0
wf_battlefirst: db 'Battle stations! You fire first: choose a square in enemy waters.',0
wf_battlesecond: db 'Battle stations! Your opponent fires first.',0
wf_linkpaused: db 'Link paused. Reconnect to resume; your fleet and turn are kept.',0
wf_linkrestored: db 'Link restored. Match resumed.',0
wf_historyerror: db 'Match histories differ. Resume refused; no state was replaced.',0
wf_shotpending: db 'Shot sent. Waiting for its result; do not fire again.',0
wf_resultmessage: db 'Shot resolved. One shot per turn, including after a hit.',0
wf_won: db 'VICTORY! You sank all five enemy ships. Both press N for a new match.',0
wf_lost: db 'Your fleet is sunk. Both press N for a new match.',0
wf_peerprefix: db 'Peer: '
wf_youprefix: db 'You:  '
