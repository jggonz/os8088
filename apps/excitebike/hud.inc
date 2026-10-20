; The HUD's non-clock cells (SPEC.md 102.3).  The 20 cells are
;   0-11  "TIME m:ss.cc"     (xb_hud_build, game.inc: digits 5..11 change on the clock)
;   12    blank
;   13    the lap number, or the B of BURNING
;   14    blank                 ("BURNING" fills 13..19 while the engine is stalled)
;   15-19 the engine gauge: five cells, each a full block (`\`) or an empty one (`]`),
;         (u + 4) / 5 of them full for a temperature of 8 + u units
; The renderers compare each cell with what they drew (xb_hudcur) and draw the ones
; that differ, so a gauge that did not move costs the compare and nothing else.
;
; xb_hud_sim - the whole string for this state; xb_huddirty = 2 when cells 12..19
; changed (the digits alone set 1 in xb_hud_build).  ~600 clocks.
xb_hud_sim:
    push ds
    pop es
    cld
    call xb_hud_build
    mov di, xm_hudtmp
    cmp byte [xb_noflow], 0
    jne .game
    ; ---- the game's messages, in the cells 13..19: what the rider is told wins over the gauge
    mov si, xb_m_paused
    cmp byte [xb_paused], 0
    jne .msg
    mov si, xb_m_finish
    cmp byte [xm_fin], 0
    jne .msg
    cmp word [xb_cd], 0
    je .nocd
    mov ax, [xb_cd]                 ; READY 3 / 2 / 1: (cd - 1) / 60 + 1
    dec ax
    xor dx, dx
    mov cx, 60
    div cx
    add al, '1'
    mov [xb_m_ready + 6], al
    mov si, xb_m_ready
    jmp .msg
.nocd:
    cmp word [xb_flash], 0
    je .noflash
    mov si, xb_m_go
    cmp byte [xb_flashk], 0
    je .msg
    mov al, [xm_lap]
    add al, '0'
    mov [xb_m_lap + 4], al
    mov si, xb_m_lap
    jmp .msg
.noflash:
    cmp byte [xb_attract], 0
    je .game
    mov si, xb_m_demo
.msg:
    mov cx, 7
    rep movsb
    jmp .cmp
.game:
    cmp byte [xm_mode], XM_STALL
    jne .norm
    mov si, xb_burning
    mov cx, 7
    rep movsb
    jmp .cmp
.norm:
    mov al, [xm_lap]
    add al, '0'
    stosb
    mov al, ' '
    stosb
    mov al, [xm_temp + 1]           ; the temperature's whole units
    sub al, 8
    xor ah, ah
    mov bx, ax
    mov bl, [xm_gtab + bx]          ; cells full, 0..5
    mov cx, 5
    xor dl, dl
.g:
    mov al, ']'
    cmp dl, bl
    jae .e
    mov al, 92                      ; the full block
.e:
    stosb
    inc dl
    loop .g
.cmp:
    mov si, xm_hudtmp
    mov di, xb_hudstr + 13
    mov cx, 7
    repe cmpsb
    je .out
    mov si, xm_hudtmp
    mov di, xb_hudstr + 13
    mov cx, 7
    rep movsb
    mov byte [xb_huddirty], 2
.out:
    ret

xb_burning: db 'BURNING'
xb_m_paused: db 'PAUSED '
xb_m_finish: db 'FINISH!'
xb_m_ready: db 'READY 3'
xb_m_go: db '  GO!  '
xb_m_lap: db 'LAP 1  '
xb_m_demo: db '  DEMO '
