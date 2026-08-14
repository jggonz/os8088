; =============================================================================
; lplslv.inc - the SLAVE half of the LapLink transport
;
; Shared by tests/lptlink and OS88NET.COM, and deliberately NOT part of
; lplink.inc: os8088 is always the master (docs/NET-PLAN.md 1.3), so NET.DRV
; would carry this as dead code. What the two includers here have in common is
; that both are the far end of a cable and both have a console.
;
; REQUIRES, on top of lplink.inc's lpl_ticks:
;
;   lpl_kbhit   CF=1 and AL = the key if one is waiting (consumed), else CF=0.
;
; The window is EIGHT NIBBLES and not four bytes, which is the whole reason
; this is worth sharing: a byte-granular window cannot recover from being half
; a byte out of step, and a slave that joined in the middle of a transfer is
; the ordinary case when the master is sweeping ports. SPEC.md 9.5's mouse
; resync, one level down.
; =============================================================================

; -----------------------------------------------------------------------------
; slv_hunt - dwell on the current port waiting to be called
; out: CF=0 the magic arrived, CF=1 the dwell expired or a key was pressed
;      ([slv_abort] = 1 for the second)
;
; The window is EIGHT NIBBLES, not four bytes, and that is what makes a slave
; which joined in the middle of a transfer resynchronise: a byte-granular
; window cannot recover from being half a byte out of step. SPEC.md 9.5's
; mouse resync, one level down.
; -----------------------------------------------------------------------------
slv_hunt:
    push ax
    push cx
    mov word [win_lo], 0
    mov word [win_hi], 0
    call lpl_ticks
    add ax, SLAVE_DWELL
    mov [dl_hunt], ax
.loop:
    call lpl_kbhit
    jc  .key
    call lpl_ticks
    sub ax, [dl_hunt]           ; modular compare, so midnight is not a bug
    jns .expired
    call lp_rnib
    jc  .loop                   ; nothing yet - the dwell is what bounds this
    mov cl, 4
.sh:
    shl word [win_lo], 1
    rcl word [win_hi], 1
    dec cl
    jnz .sh
    or  byte [win_lo], al
    mov ax, [win_hi]
    cmp ax, MAGQ_HI
    jne .loop
    mov ax, [win_lo]
    cmp ax, MAGQ_LO
    jne .loop
    clc
    jmp short .out
.key:
    cmp al, 27                  ; ESC stops; any other key just moves on
    jne .expired
    mov byte [slv_abort], 1
.expired:
    stc
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; slv_reply - answer the magic, and say which protocol this is
; -----------------------------------------------------------------------------
slv_reply:
    push ax
    push cx
    push si
    mov si, magic_r
    mov cx, 4
.s:
    mov al, [si]
    inc si
    call lp_sbyte
    jc  .bad
    loop .s
    mov al, PROTO_VER
    call lp_sbyte
    jc  .bad
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret


; -----------------------------------------------------------------------------
; lp_rbyte_w - lp_rbyte, but waits FOR AS LONG AS IT TAKES for the first nibble
;
; The second nibble keeps the ordinary short bound, which is the point: a
; retry loop around a whole byte would resume in the middle of one and desync
; the phase. Only the gap BETWEEN bytes may be long.
;
; IT USED TO GIVE UP AFTER CMD_WAIT - 180 ticks, ~10 seconds - and that was
; wrong in a way only a real session shows. The gap between commands is the
; USER'S THINKING TIME and has no upper bound: os8088 mounts the volume at
; Connect, and the next command comes when somebody double-clicks the drive
; icon, which may be a minute later or an hour. The slave timed out, went back
; to hunting, and os8088 - which had no idea - sent its next read into a
; machine that was no longer listening for one. It reached the field as "the
; link comes up and reports 1440 sectors, then opening D: shows nothing and
; the Control Panel says disconnected": the mount at Connect worked, and
; everything after the first idle pause did not.
;
; So the server waits, and the two ways out are the ones a server should have:
; ESC here, and the master's own NC_BYE - which net_detach sends on unload and
; net_connect now sends BEFORE its hello, so a slave still sitting in this
; loop from a stale session is shaken loose and can be found again. Without
; that second half an unbounded wait would trade one bug for another: the
; magic is not a command, so this loop would discard it and the reconnect
; would never complete.
; -----------------------------------------------------------------------------
lp_rbyte_w:
    push bx
.w:
    call lp_rnib
    jnc .got
    call lpl_kbhit              ; ESC is the only deadline left
    jnc .w
    cmp al, 27                  ; ...and ONLY ESC: lpl_kbhit CONSUMES the key,
    jne .w                      ; so ending on any of them ends a live session
                                ; on a brush of the DOS keyboard - and the
                                ; swallowed key then defeats the main loop's
                                ; own ESC test, so nothing is even reported
    mov byte [slv_abort], 1     ; slv_hunt's flag: the sweep above stops too
    jmp short .bad
.got:
    mov bl, al
    call lp_rnib
    jc .bad
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    or  al, bl
    clc
    pop bx
    ret
.bad:
    stc
    pop bx
    ret

win_lo:     dw 0
win_hi:     dw 0
dl_hunt:    dw 0
slv_abort:  db 0
