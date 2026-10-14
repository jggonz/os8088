; =============================================================================
; apps/isotron/itrules.inc - the rules engine (SPEC.md 108.2 - 108.5)
;
; The grid, a round, an iteration, a move, the RNG, scores, losses in a row,
; the tower and the pace governor. tools/isotronsim.py is the same rules in
; Python and is NORMATIVE: Round.iterate, Game.play_round and the Rng there
; are what every routine below is held to, round by round, by
; tests/isotron.py.
;
; Register contract for the whole file: DS = the package, ES = the claim
; (it_seg) - the grid lives at IT_GRID in it. Nothing here calls the kernel
; but OSAPI_GET_TICKS, so ES survives every routine.
; =============================================================================

; -----------------------------------------------------------------------------
; it_flow - the state machine, once a wake. ES = the claim.
; -----------------------------------------------------------------------------
it_flow:
    cmp byte [it_tpause], 0
    jne .out                        ; the harness is reading a finished round
    mov al, [it_state]
    cmp al, ST_TITLE
    je .title
    cmp al, ST_PLAY
    je .play
    cmp al, ST_CRASH
    je .crash
    cmp al, ST_OVER
    je .over
    cmp al, ST_NEXT
    je .next
.out:                               ; ST_WAIT: Space (it_commands) moves on
    ret
.title:
    test byte [it_tflags], IT_TF_NOFLOW
    jz .out
    cmp byte [it_tgo], 0
    je .out
    mov byte [it_tgo], 0            ; the harness's own game: its seed, its
    mov al, [it_tdiff]              ; tier, silent
    dec al
    cmp al, 3
    jb .tok
    xor al, al                      ; a tier the table does not have: Rookie
.tok:
    inc al
    mov ah, 1
    call it_game_start
    ret
.play:
    call it_play
    ret
.crash:
    call OSAPI_GET_TICKS
    sub ax, [it_tend]
    js .out
    mov byte [it_state], ST_WAIT
    mov byte [it_msgd], 1
    ret
.over:
    call OSAPI_GET_TICKS
    sub ax, [it_tend]
    js .out
    call it_to_title
    ret
.next:
    cmp byte [it_lossrow], 3
    jb .nextr
    inc word [it_gover]             ; the harness counts finished games
    call it_to_title
    ret
.nextr:
    call it_round_start
    ret

; -----------------------------------------------------------------------------
; it_to_title - back to the title (End Game, or a game over that has shown)
; -----------------------------------------------------------------------------
it_to_title:
    mov byte [it_state], ST_TITLE
    mov byte [it_paused], 0
    mov byte [it_msgd], 1
    mov byte [it_hudd], 1
    ret

; -----------------------------------------------------------------------------
; it_play - ST_PLAY: run the governor, and end the round if it ended
;
; THE GOVERNOR (SPEC.md 108.4.2). `it_debt` is now - V in u (1/768 tick):
; each wake adds the ticks that have passed, capped so a stall (a floppy, a
; long paint) owes at most one tick past the current one and never bursts the
; heads across the arena; then iterations run while the debt is not negative,
; each one paying its cost. A FAST harness game runs IT_FASTN iterations and
; pays nothing.
; -----------------------------------------------------------------------------
it_play:
    cmp byte [it_paused], 0
    jne .resync
    test byte [it_tflags], IT_TF_FAST
    jnz .fast
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [it_lastt]              ; ticks since the last wake, wrap-safe
    mov [it_lastt], dx
    cmp ax, 2
    jbe .add
    mov ax, 2                       ; ...and no more than two of them
.add:
    mov dx, U_TICK
    mul dx                          ; AX = ticks * 768 (<= 1,536)
    add ax, [it_debt]
    cmp ax, DEBT_CAP
    jle .cap
    mov ax, DEBT_CAP
.cap:
    mov [it_debt], ax
.gov:
    cmp word [it_debt], 0
    jl .out
    call it_gather
    call it_iterate
    jnc .gov
    jmp short .ended
.fast:
    mov cx, IT_FASTN
.fl:
    push cx
    call it_gather
    call it_iterate
    pop cx
    jc .ended
    loop .fl
.out:
    ret
.resync:
    call OSAPI_GET_TICKS            ; paused: V = now on the way out of it
    mov [it_lastt], ax
    mov word [it_debt], 0
    ret
.ended:
    call it_round_end
    ret

; -----------------------------------------------------------------------------
; it_gather - the coming iteration's keys: the bot's, the harness script's,
;             or what the UI task posted (itinput.inc). ES = the claim.
; -----------------------------------------------------------------------------
it_gather:
    test byte [it_tflags], IT_TF_BOT
    jz .script
    call it_bot                     ; itai.inc
    ret
.script:
    test byte [it_tflags], IT_TF_FAST
    jz .keys
    cmp byte [it_tinn], 0
    je .keys
    call it_script                  ; posts as the UI task would, then...
.keys:                              ; ...the keyboard's own consumer takes it
    mov al, [it_kboost]             ; the F10s posted so far...
    sub [it_kboost], al             ; ...taken in one RMW: a newer one stays
    mov [it_inboost], al
    xor al, al
    xchg al, [it_kf9]               ; F9, taken and cleared in one instruction
    mov [it_inf9], al
    ret

; -----------------------------------------------------------------------------
; it_script - the harness's (iteration, key) table at this iteration, POSTED
; into the UI task's mailbox exactly as it_key posts a keystroke (it_kboost,
; it_kf9, it_fifo_push), so it_gather's .keys consumer is what takes it. The
; count is clamped to the table's IT_TINMAX entries: a longer one would read
; the bss after it as script rows.
; -----------------------------------------------------------------------------
it_script:
    push ax
    push bx
    push cx
    xor bx, bx
    mov cl, [it_tinn]
    cmp cl, IT_TINMAX
    jbe .n
    mov cl, IT_TINMAX
.n:
    xor ch, ch
    jcxz .done
.lp:
    mov ax, [it_tinput + bx]
    cmp ax, [it_iter]
    jne .nx
    mov al, [it_tinput + bx + 2]
    cmp al, K_BOOST
    je .boost
    cmp al, K_NORMAL
    je .nf9
    call it_fifo_push               ; K_LEFT / K_RIGHT
    jmp short .nx
.boost:
    inc byte [it_kboost]
    jmp short .nx
.nf9:
    mov byte [it_kf9], 1
.nx:
    add bx, 3
    loop .lp
.done:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_game_start - a new game. in: AL = tier 1..3, AH = 1 for the harness's
;                 seed (it_tseed), 0 for the clock's. ES = the claim.
; -----------------------------------------------------------------------------
it_game_start:
    push ax
    push bx
    push si
    mov [it_tier], al
    dec al
    mov bl, 10
    mul bl
    mov si, ax
    add si, it_tiers                ; A, B, C, look, icpt, wander
    mov ax, [si]
    mov [it_A], ax
    mov ax, [si + 2]
    mov [it_B], ax
    mov ax, [si + 4]
    mov [it_C], ax
    mov al, [si + 6]
    mov [it_look], al
    mov al, [si + 7]
    mov [it_icpt], al
    mov al, [si + 8]
    mov [it_wander], al
    pop si
    pop bx
    pop ax
    mov dx, [it_tseed]
    or ah, ah
    jnz .seed
    push ax
    call OSAPI_GET_TICKS
    mov dx, ax
    pop ax
.seed:
    mov [it_rng], dx
    mov byte [it_h], 1
    mov byte [it_prev], WIN_NONE
    mov byte [it_lossrow], 0
    mov word [it_sp], 0
    mov word [it_sc], 0
    mov word [it_round], 0
    mov byte [it_paused], 0
    call it_round_start
    ret

; -----------------------------------------------------------------------------
; it_round_start - SPEC.md 108.3, a round's step 2: the grid, the player, h heads
; -----------------------------------------------------------------------------
it_round_start:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call it_grid_reset
    call it_dbg_ring                ; the debug view: the new grid is a ring
    mov word [it_ck], 0
    mov word [it_iter], 0
    mov word [it_nsteps], 0
    mov byte [it_winner], WIN_NONE
    mov byte [it_pspeed], P_NORMAL
    mov byte [it_pacc], 0
    mov al, [it_fhead]              ; empty the FIFO: the consumer's own byte
    mov [it_ftail], al
    mov byte [it_kboost], 0         ; keys pressed between rounds do not carry
    mov byte [it_kf9], 0
    mov al, [it_h]
    mov [it_hplayed], al
    ; --- the player: (108, 27) heading N --------------------------------------
    mov bx, IT_PLAYER
    mov ax, P_D0 * GW + P_U0
    mov [bx + H_IDX], ax
    mov byte [bx + H_U], P_U0
    mov byte [bx + H_D], P_D0
    mov byte [bx + H_HD], HD_N
    mov byte [bx + H_ALIVE], 1
    mov byte [bx + H_LEG], 0
    mov si, ax
    mov al, C_PLAYER | 1            ; entered N/S: the axis bit
    call it_place
    call it_dbg_cell
    ; --- the computer: h heads from (108, 90) ----------------------------------
    mov al, [it_h]
    mov [it_nheads], al
    xor cx, cx                      ; CL = the head
    mov bx, it_heads
.head:
    cmp cl, [it_nheads]
    jae .placed
    mov word [bx + H_IDX], C_D0 * GW + C_U0
    mov byte [bx + H_U], C_U0
    mov byte [bx + H_D], C_D0
    mov byte [bx + H_ALIVE], 1
    mov byte [bx + H_LEG], 0
    mov byte [bx + H_TURNED], 0
    mov byte [bx + H_UPEND], 0
    mov byte [bx + H_USIDE], 0
    mov si, cx
    mov al, [it_heads4 + si]        ; W, E, S, N: the first h of them
    mov [bx + H_HD], al
    add bx, H_SIZE
    inc cl
    jmp short .head
.placed:
    mov al, [it_h]                  ; every head alive: it_live and the cost
    mov [it_live], al               ; base A + B * h, the one MUL of a round
    xor ah, ah
    mul word [it_B]
    add ax, [it_A]
    mov [it_costb], ax
    mov byte [it_opener], 1         ; h >= 2: the E head (index 1) opens
    cmp byte [it_h], 1
    jne .one
    mov byte [it_opener], 0         ; h = 1: the one head, E or W by the
    call it_rand                    ; round's first RNG byte (bit 0: 1 = W)
    mov ah, HD_E
    test al, 1
    jz .e
    mov ah, HD_W
.e:
    mov [it_heads + H_HD], ah
.one:
    mov bx, it_heads
    mov si, C_D0 * GW + C_U0
    mov al, [bx + H_HD]
    and al, 1
    xor al, 1                       ; the axis bit: 1 = N/S (heading bit 0 = 0)
    or al, C_COMP
    call it_place                   ; the shared start cell, once
    call it_dbg_cell
    mov byte [it_state], ST_PLAY
    mov byte [it_paused], 0
    mov byte [it_hudd], 1
    mov byte [it_msgd], 1
    ; --- the pace starts now: V = now, and it is the LAST thing done ------------
    ; (SPEC.md 108.4.2: no burst at a round's start. Whatever the round start
    ; cost - and wave 2's canvas build will sit right here - is behind V, so
    ; the governor's first wake owes only the ticks after it.)
    call OSAPI_GET_TICKS
    mov [it_lastt], ax
    mov word [it_debt], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_grid_reset - every cell 0, the outline ring C_EDGE. ES = the claim.
; ~11k word stores and ~420 byte stores: ~0.2M cycles, once a round.
; -----------------------------------------------------------------------------
it_grid_reset:
    push ax
    push cx
    push di
    cld
    mov di, IT_GRID
    mov cx, GSIZE / 2
    xor ax, ax
    rep stosw
%if GSIZE & 1
    stosb
%endif
    mov al, C_EDGE
    mov di, IT_GRID                 ; d = 0, every u
    mov cx, GW
    rep stosb
    mov di, IT_GRID + (GH - 1) * GW ; d = 100, every u
    mov cx, GW
    rep stosb
    mov di, IT_GRID + GW            ; u = 0 and u = 216 for d = 1..99
    mov cx, GH - 2
.side:
    mov [es:di], al
    mov [es:di + GW - 1], al
    add di, GW
    loop .side
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_place - mark grid cell SI with AL and fold it into the checksum:
;            ck = rol(ck, 1) XOR idx (SPEC.md 108.7.2)
; -----------------------------------------------------------------------------
it_place:
    push ax
    mov [es:si + IT_GRID], al
    mov ax, [it_ck]
    rol ax, 1
    xor ax, si
    mov [it_ck], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; it_rand - the LCG (SPEC.md 108.6): s = s*25173 + 13849, AL = s >> 8.
; One MUL (~120 cycles on an 8088). Preserves everything but AX.
; -----------------------------------------------------------------------------
it_rand:
    push dx
    mov ax, [it_rng]
    mov dx, 25173
    mul dx
    add ax, 13849
    mov [it_rng], ax
    mov al, ah
    pop dx
    ret

; -----------------------------------------------------------------------------
; it_iterate - ONE ITERATION (SPEC.md 108.3). ES = the claim; the keys are
; in it_inboost / it_inf9 and the FIFO. out: CF = 1 the round is over and
; [it_winner] says who won it. Clobbers AX..DI.
;
; The player first: its speed, then as many whole cells as its accumulator
; has, one FIFO turn popped before each. A player crash ends the round there:
; the computer heads do not move in that iteration. Then each live computer
; head in index order draws ONE RNG byte, decides and moves. The cost is
; charged last: A + B * (live heads at the start) + C * (player cells).
;
; NO MULTIPLY AND NO WALK OF THE HEADS: it_live is a running count and
; it_costb = A + B * it_live is kept with it (one MUL at the round's start,
; a subtraction of B when a head dies), so the iteration snapshots it into
; it_cost and adds C for each player cell as it moves.
; -----------------------------------------------------------------------------
it_iterate:
    ; --- A + B * (live heads at the start) ----------------------------------------
    mov ax, [it_costb]
    mov [it_cost], ax
    ; --- the speed -----------------------------------------------------------------
    cmp byte [it_inf9], 0
    je .nof9
    mov byte [it_pspeed], P_NORMAL
.nof9:
    mov al, [it_pspeed]
    add al, [it_inboost]
    cmp al, P_MAX
    jbe .sp
    mov al, P_MAX
.sp:
    cmp al, [it_pspeed]
    je .same
    mov byte [it_hudd], 1           ; the HUD shows the speed
.same:
    mov [it_pspeed], al
    add [it_pacc], al
    ; --- the player's whole cells -------------------------------------------------
.pl:
    cmp byte [it_pacc], 6
    jb .comp
    sub byte [it_pacc], 6
    call it_fifo_pop                ; CF = empty, else AL = K_LEFT / K_RIGHT
    jc .noturn
    mov bx, IT_PLAYER
    mov ah, [bx + H_HD]
    inc ah                          ; right = h + 1
    cmp al, K_LEFT
    jne .t
    add ah, 2                       ; left = h + 3
.t:
    and ah, 3
    mov [bx + H_HD], ah
    mov byte [bx + H_LEG], 0
.noturn:
    mov ax, [it_C]
    add [it_cost], ax               ; + C a player cell, crash or not
    mov bx, IT_PLAYER
    mov al, C_PLAYER
    call it_move
    jnc .pl
    mov byte [it_winner], WIN_COMP  ; the player crashed: the computer wins NOW
    jmp short .cost
    ; --- each live computer head, in index order -----------------------------------
.comp:
    xor cx, cx
    mov bx, it_heads
.hl:
    cmp cl, [it_nheads]
    jae .hd
    cmp byte [bx + H_ALIVE], 0
    je .hn
    call it_rand                    ; ONE byte a head-move, whatever it decides
    call it_ai                      ; itai.inc: BX head, CL index, AL r
    mov al, C_COMP
    call it_move
    jnc .hn
    dec byte [it_live]              ; a head died: the next iteration's
    mov ax, [it_B]                  ; cost loses its B
    sub [it_costb], ax
.hn:
    add bx, H_SIZE
    inc cl
    jmp short .hl
.hd:
    cmp byte [it_live], 0
    jne .cost
    mov byte [it_winner], WIN_PLAYER
    ; --- the virtual cost (SPEC.md 108.4.1) ------------------------------------------
.cost:
    mov ax, [it_cost]
    sub [it_debt], ax
    inc word [it_iter]
    cmp byte [it_winner], WIN_NONE
    je .go
    stc
    ret
.go:
    clc
    ret

; -----------------------------------------------------------------------------
; it_move - one cell for the head at BX, its owner bits in AL. ES = the claim.
; out: CF = 1 it crashed (and is dead). Preserves BX and CX; clobbers AX, DX,
; SI, DI.
;
; ONE HEAD-STEP, the unit the per-step budget is priced in: one grid
; compare, one store, the u/d bookkeeping and the checksum. it_place is
; INLINED here (its call, push and pop were ~70 cycles of every step) and
; one 4-byte row of it_mv per heading gives the delta, du and dd off a
; single index. MEASURED before this pass at ~525 cycles on MartyPC's 5150
; without the debug view's bit (it_dbg_cell, ~460 more, wave 1 only) - the
; plan's 150 is under the 8088's fetch floor for this many instructions and
; was re-priced in SPEC.md 108.6. it_step0..it_step1 brackets it for the
; cycle rows.
; -----------------------------------------------------------------------------
it_move:
    IT_MARK it_step0                ; (isotron.asm: a mark, never a label)
    mov dl, [bx + H_HD]
    xor dh, dh
    mov di, dx
    shl di, 1
    shl di, 1                       ; DI = the heading's it_mv row
    mov si, [bx + H_IDX]
    add si, [it_mv + di]
    cmp byte [es:si + IT_GRID], 0
    jne .crash
    and dl, 1                       ; the axis of the move that entered it
    or al, dl
    xor al, 1                       ; ...stored as 1 = N/S (heading bit 0 = 0)
    mov [es:si + IT_GRID], al       ; it_place, inline: the cell...
    mov ax, [it_ck]
    rol ax, 1
    xor ax, si
    mov [it_ck], ax                 ; ...and ck = rol(ck, 1) XOR idx
    mov [bx + H_IDX], si
    mov ax, [it_mv + 2 + di]        ; AL = du, AH = dd
    add [bx + H_U], al
    add [bx + H_D], ah
    inc byte [bx + H_LEG]
    inc word [it_nsteps]
    call it_dbg_cell
    IT_MARK it_step1
    clc
    ret
.crash:
    mov byte [bx + H_ALIVE], 0
    stc
    ret

; -----------------------------------------------------------------------------
; it_round_end - the winner scores; losses in a row; the tower (SPEC.md
; 108.3): a player win raises it ONLY if the previous round was a player win
; too, and 4 + 1 is 1. A loss never lowers it.
; -----------------------------------------------------------------------------
it_round_end:
    inc word [it_round]
    cmp byte [it_winner], WIN_PLAYER
    jne .comp
    inc word [it_sp]
    mov byte [it_lossrow], 0
    cmp byte [it_prev], WIN_PLAYER
    jne .prev
    inc byte [it_h]
    cmp byte [it_h], 5
    jb .prev
    mov byte [it_h], 1
    jmp short .prev
.comp:
    inc word [it_sc]
    inc byte [it_lossrow]
.prev:
    mov al, [it_winner]
    mov [it_prev], al
    mov byte [it_hudd], 1
    mov byte [it_msgd], 1
    test byte [it_tflags], IT_TF_HOLD
    jz .flow
    mov byte [it_tpause], 1         ; the harness reads the round, then clears
.flow:
    test byte [it_tflags], IT_TF_NOFLOW
    jz .shown
    mov byte [it_state], ST_NEXT    ; no crash pause, no wait, no sweep
    ret
.shown:
    call OSAPI_GET_TICKS
    cmp byte [it_lossrow], 3
    jb .crash
    add ax, IT_OVERT
    mov [it_tend], ax
    mov byte [it_state], ST_OVER
    inc word [it_gover]
    ret
.crash:
    add ax, IT_CRASHT
    mov [it_tend], ax
    mov byte [it_state], ST_CRASH
    ret

; -----------------------------------------------------------------------------
; tables
; -----------------------------------------------------------------------------
; it_mv: a row a heading (N, E, S, W): the idx delta, then du and dd
it_mv:      dw GW
            db 0, 1
            dw 1
            db 1, 0
            dw -GW
            db 0, -1
            dw -1
            db -1, 0
it_heads4:  db HD_W, HD_E, HD_S, HD_N
; the tiers (SPEC.md 108.4.1, 108.6): A, B, C in u; LOOK, ICPT, WANDER; pad
it_tiers:
    dw 36, 178, 62
    db 1, 64, 6, 0                  ; Rookie - the footage's pace and reading
    dw 27, 134, 47
    db 3, 96, 4, 0                  ; Veteran - x4/3, ours
    dw 18, 89, 31
    db 6, 128, 2, 0                 ; Lunatic - x2, ours
