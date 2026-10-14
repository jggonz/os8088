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
; but OSAPI_GET_TICKS and OSAPI_SND_TONE (itsnd.inc), neither of which
; touches ES, so ES survives every routine.
; =============================================================================

; -----------------------------------------------------------------------------
; it_flow - the state machine, once a wake. ES = the claim.
; -----------------------------------------------------------------------------
it_flow:
    mov al, [it_state]
    cmp byte [it_tpause], 0         ; THE HARNESS IS READING A ROUND (HOLD):
    je .run                         ; nothing of the game moves - but a SWEEP
    cmp al, ST_SWEEP                ; and READY run on, being the picture's and
    jb .out                         ; not the game's, so a held game stops at
.run:                               ; its round's first iteration, drawn
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
    cmp al, ST_READY
    je .ready
.out:                               ; ST_WAIT: Space (it_commands) moves on;
    ret                             ; ST_SWEEP: the presenters (itsweep.inc)
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
    mov byte [it_snd], 0            ; (silent, as its comment has always said:
    call it_snd_label               ; a harness game is the rows', not a
    call it_game_start              ; listener's - isotronsound uses the keys)
    ret
.play:
    call it_play
    ret
.crash:
    ; THE CRASH (SPEC.md 108.13.2): 0.8 s; the winner's score changes 0.27 s
    ; into it (the footage's 0.25), then WAIT says who won - or, the third
    ; loss in a row, GAME OVER does
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [it_tend]               ; AX = now - the end: -15 .. 0 and on
    cmp ax, IT_HUDT - IT_CRASHT
    jl .out
    xor cl, cl
    xchg cl, [it_hudp]              ; the score, once
    or [it_hudd], cl
    or ax, ax
    js .out
    mov byte [it_msgd], 1
    cmp byte [it_lossrow], 3
    jae .gover
    mov byte [it_state], ST_WAIT
    ; THE NEXT ARENA IS BUILT NOW, while the game waits for Space - the
    ; tower's height is known from the round's end - so the sweep Space
    ; starts presents from its first wake (SPEC.md 108.13.3); windowed, by
    ; the worker after its lock hold (it_bld_post)
    mov al, IT_BQ_WAIT
    call it_bld_post
    ret
.gover:
    mov byte [it_state], ST_OVER
    mov byte [it_qcont], 0          ; (a Space from CRASH: not GAME OVER's)
    add dx, IT_OVERT
    mov [it_tend], dx
    call it_snd_quiet               ; THE CRASH'S LAST ENTRY STILL HOLDS THE
    mov si, it_ph_over              ; CHANNEL at 60h (its off row was due this
    call it_snd_post                ; very wake, and the post would replace
    ret                             ; it unsent): our own off frees it, or the
                                    ; phrase's first 523 Hz at 50h is refused
                                    ; (MEASURED: CF = 1, owner 60h). Then its
                                    ; phrase, under the line (itsnd.inc)
.ready:
    ; READY (108.13.2): 0.5 s with the arena drawn and nothing moving, then
    ; the round - its pace from the first wake that owes no present (it_play).
    ; The About card up (nothing is presented under it) holds READY: its
    ; 0.5 s start again from the card's going - and so do its beeps, the
    ; card being silent (it_snd_frame): posted every wake under it, the
    ; phrase waits unbegun and starts on the first wake after
    call OSAPI_GET_TICKS
    cmp byte [it_abon], 0
    je .rd
    add ax, IT_READYT
    mov [it_tend], ax
    mov si, it_ph_ready
    call it_snd_post
    ret
.rd:
    sub ax, [it_tend]
    js .out
    IT_MARK it_rdy1
    mov byte [it_state], ST_PLAY
    mov byte [it_vsync], 1
    mov byte [it_gskip], 0
    mov byte [it_gquiet], 0
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
; it_to_title - back to the title (End Game, or a game over that has shown).
; Preserves every register.
; -----------------------------------------------------------------------------
it_to_title:
    call it_snd_quiet               ; Esc, or GAME OVER done: silence first
    mov byte [it_state], ST_TITLE
    mov byte [it_paused], 0
    mov byte [it_hudp], 0           ; (a score still owed is the title's no more)
    mov byte [it_swpre], 0          ; (nor an arena built for a round, nor the
    mov byte [it_qround], 0         ; round one was being built for)
    mov byte [it_full], 1           ; ...presented whole, its overlays with it
    push ax
    mov al, IT_BQ_TITLE             ; the title's picture (itcanvas.inc)
    call it_bld_post
    pop ax
    ret

; =============================================================================
; THE CANVAS BUILDS, OUTSIDE THE LOCK (SPEC.md 108.13.2)
;
; A build - WAIT's next arena (0.32 - 0.45 s on the 5150), the title (0.8 s
; with its lettering), a game's first arena - touches the canvas and the
; grid and nothing on the glass, so in a window it is NOT done in the wake
; that asks for it, inside OSAPI_GFX_LOCK with the pointer off and every
; other task's drawing blocked: it_bld_post leaves IT_BQ_* in it_bldq, that
; wake presents nothing (it_wake), and the worker builds it after
; OSAPI_GFX_UNLOCK (it_bld_run), its next wake presenting the result. While
; one is owed or being built the canvas is not a picture: a W_PAINT makes
; its damage black and owes a whole present (itwin.inc it_paint_rect), and
; the bracket's entry builds or waits for it first (it_bld_sync). In a
; bracket - the lock held throughout, the worker frozen, no pointer - a
; build is done where it is asked for.
; =============================================================================

; it_bld_post - AL = IT_BQ_*: the build, now in a bracket, else owed to the
; worker. Preserves every register.
it_bld_post:
    cmp byte [it_fsm], 0
    jne it_bld_do
    mov [it_bldq], al
    ret

; it_bld_do - build AL = IT_BQ_* (ES set here: the claim). WAIT's arena or a
; game's first one (it_sw_build), the round it is for then started by the
; next wake's it_commands (it_qround); or the title's picture. Preserves
; every register.
it_bld_do:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [it_seg]
    cmp al, IT_BQ_TITLE
    je .title
    cmp al, IT_BQ_ROUND
    jne .wait
    mov byte [it_qround], 1
.wait:
    call it_sw_build
    mov byte [it_swpre], 1
    jmp short .x
.title:
    call it_title_build
    call it_box_clear               ; (its spans' boxes: a whole present owed)
.x:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; it_bld_run - the worker, after OSAPI_GFX_UNLOCK: the build owed, if one is,
; claimed (bit 7) in one critical section - the UI task's it_bld_sync claims
; the same byte - then built and cleared. Clobbers AX.
it_bld_run:
    pushf
    cli
    mov al, [it_bldq]
    or al, al
    jz .none
    test al, IT_BQ_BUSY
    jnz .none
    or byte [it_bldq], IT_BQ_BUSY
    popf
    IT_MARK it_bld0
    call it_bld_do
    IT_MARK it_bld1
    mov byte [it_bldq], 0
    ret
.none:
    popf
    ret

; it_bld_sync - the bracket's entry (the UI task, the lock held): a build
; owed and not started is built here, one being built is waited for - the
; worker's build takes no lock, so it finishes while we yield. out: CF = 1
; it did not finish in IT_BLDWAIT ticks (the bracket is not entered).
; Preserves every register.
IT_BLDWAIT  equ 54                  ; 3 s: the longest build is 0.8 s
it_bld_sync:
    push ax
    push dx
    pushf
    cli
    mov al, [it_bldq]
    or al, al
    jz .free
    test al, IT_BQ_BUSY
    jnz .busy
    mov byte [it_bldq], 0
    popf
    call it_bld_do
    jmp short .ok
.free:
    popf
.ok:
    pop dx
    pop ax
    clc
    ret
.busy:
    popf
    call OSAPI_GET_TICKS
    mov dx, ax
.w:
    call OSAPI_TASK_YIELD
    cmp byte [it_bldq], 0
    je .ok
    call OSAPI_GET_TICKS
    sub ax, dx
    cmp ax, IT_BLDWAIT
    jb .w
    pop dx
    pop ax
    stc
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
    cmp byte [it_vsync], 0          ; A ROUND STARTED: nothing iterates while
    je .tick                        ; its whole present is still owed - or
    cmp byte [it_full], 0           ; still going out in bands, whichever task
    jne .resync                     ; started the round - and V = now in the
    mov byte [it_vsync], 0          ; first wake after it is on the glass, so
    call it_clock_sync              ; nothing the picture cost is owed
    jmp short .gov
.tick:
    cmp byte [it_tstall], 0         ; THE HARNESS'S FORCED STALL (isotronpace,
    je .ck                          ; SPEC.md 108.14.3): the clock is read
    call it_stall                   ; after a real wait of that many ticks
.ck:
    call it_clock                   ; AX = u since the last wake (<= 1,536;
    add ax, [it_debt]               ; a full-screen frame's sub-ticks too)
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
    call it_clock_sync              ; paused: V = now on the way out of it
    ret
.ended:
    call it_round_end
    ret

; -----------------------------------------------------------------------------
; it_stall - the harness's forced stall (ittest.inc it_tstall): wait, really,
; it_tstall ticks of the BIOS clock - the lock held, the frame not returned -
; then clear it. What follows is what any stall leaves: a clock that has run
; on. Preserves all but AX.
; -----------------------------------------------------------------------------
it_stall:
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
.w:
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp al, [it_tstall]
    jb .w
    mov byte [it_tstall], 0
    inc word [it_nstall]
    pop bx
    ret

; -----------------------------------------------------------------------------
; it_clock - the governor's clock (SPEC.md 108.4.2, 108.12.4): AX = the time,
; in u, since the last call or it_clock_sync - whole ticks (at most two: a
; stall owes no more) times 768, and in a full-screen bracket also the
; sub-ticks of FSXF_FASTTICK at 256 each, so `now` = ticks * 768 + phase *
; 256 and a tick's u are the same 768 whichever way they arrive. Wrap-safe.
; Preserves all but AX.
; -----------------------------------------------------------------------------
it_clock:
    push bx
    push cx
    push dx
    call OSAPI_GET_TICKS
    mov bx, ax                      ; BX = the tick now
    sub ax, [it_lastt]
    mov [it_lastt], bx
    cmp ax, 2
    jbe .t
    mov ax, 2
.t:
    mov cx, U_TICK
    mul cx                          ; AX = ticks * 768
    cmp byte [it_fsm], 0
    je .out
    xor cl, cl                      ; the phase now: the sub-tick count of
    cmp bx, [it_fstk]               ; this tick (0 when the tick moved since
    jne .ph                         ; the frame clock last looked)
    mov cl, [it_fsph]
.ph:
    mov ch, cl
    sub cl, [it_lastph]             ; -2 .. 2: a moved tick's 768 less the
    mov [it_lastph], ch             ; phase it had already paid
    add ah, cl                      ; AX += CL * 256 (the sum is >= 0)
.out:
    pop dx
    pop cx
    pop bx
    ret

; it_clock_sync - V = now: the debt nothing, the clock's last reading the
; reading now (a toggle, a round's start, a resume). Preserves everything.
it_clock_sync:
    push ax
    call OSAPI_GET_TICKS
    mov [it_lastt], ax
    mov byte [it_lastph], 0
    cmp byte [it_fsm], 0
    je .z
    cmp ax, [it_fstk]
    jne .z
    mov al, [it_fsph]
    mov [it_lastph], al
.z:
    mov word [it_debt], 0
    pop ax
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
    call it_snd_quiet               ; (a game started over a crash's sweep)
    mov byte [it_h], 1
    mov byte [it_prev], WIN_NONE
    mov byte [it_lossrow], 0
    mov word [it_sp], 0
    mov word [it_sc], 0
    mov word [it_round], 0
    mov byte [it_paused], 0
    mov byte [it_hudp], 0
    or byte [it_hudd], 3            ; the HUD's two lines appear (a swept
                                    ; start letters them; NOFLOW's whole
                                    ; present composes them)
    ; A GAME STARTED OVER AN ARENA ALREADY BUILT for the last game's next
    ; round (WAIT), or over a sweep half out: the canvas is not this game's
    ; and the glass is not the picture a sweep's region assumes (it_swoo is
    ; the canvas's last height, not the glass's) - so the arena is built
    ; again and goes out as a whole present, the sweep ending at once
    cmp byte [it_swpre], 0
    jne .stale
    cmp byte [it_state], ST_SWEEP
    jne .go
.stale:
    mov byte [it_swpre], 0
    mov byte [it_full], 1
.go:
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
    test byte [it_tflags], IT_TF_NOFLOW
    jnz .nf
    ; --- THE SWEEP'S START (SPEC.md 108.13.3): the arena - built when WAIT
    ; began (it_swpre), or not yet: a game's first round has no WAIT before
    ; it, so in a window its build is owed to the worker (it_bld_post) and
    ; the round waits for it in WAIT, unpresented, its next wake starting it
    ; (it_qround); in a bracket it is built here. Then the clock it is paced
    ; on, from the arena built
    mov byte [it_qround], 0
    cmp byte [it_swpre], 0
    jne .pre
    cmp byte [it_fsm], 0
    jne .inl
    mov byte [it_state], ST_WAIT
    mov al, IT_BQ_ROUND
    call it_bld_post
    jmp .rs
.inl:
    call it_sw_build
.pre:
    IT_MARK it_sw0
    call OSAPI_GET_TICKS
    mov [it_swt0], ax
    mov byte [it_swpre], 0          ; (the build emptied the grid)
    ; THE TOWER RISES (SPEC.md 108.14.1): a raise - not a reset to 1, nor a
    ; game's first round - has its chime while the new level sweeps in
    cmp word [it_round], 0
    je .built
    mov al, [it_h]
    cmp al, [it_hplayed]
    jbe .built
    push si
    mov si, it_ph_tower
    call it_snd_post
    pop si
    jmp short .built
.nf:
    call it_grid_reset
    mov al, [it_h]
    call it_rebuild                 ; THE ARENA: the tower of h, no walls
.built:
    mov byte [it_maxspan], 0        ; (itcanvas.inc) - the start cells' spans
    mov byte [it_maxprobe], 0       ; below are the round's first
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
    call it_treset                  ; ...nor a turn key's tracking (108.14.2)
    mov byte [it_sndpc], IT_SND_CELLS ; the wall tick's count, from the start
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
    mov dh, al
    call it_span                    ; its wall's first column
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
    mov [it_hend], bx               ; the head loop's end (it_iterate)
    mov al, [it_h]                  ; every head alive: it_live and the cost
    mov [it_live], al               ; base A + B * h, the one MUL of a round
    xor ah, ah
    mul word [it_B]
    add ax, [it_A]
    mov [it_costb], ax
    mov word [it_openp], it_heads + H_SIZE ; h >= 2: the E head (index 1)
    cmp byte [it_h], 1                     ; opens
    jne .one
    mov word [it_openp], it_heads   ; h = 1: the one head, E or W by the
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
    mov dh, al
    call it_span
    call it_box_clear               ; the whole picture is owed instead
    mov byte [it_paused], 0
    test byte [it_tflags], IT_TF_NOFLOW
    jnz .whole
    call it_sw_start                ; SWEPT onto the glass (itsweep.inc)
    jmp short .rs
.whole:
    mov byte [it_full], 1           ; the harness's NOFLOW: whole, at once
    mov byte [it_state], ST_PLAY
    ; --- the pace starts now: V = now, and it is the LAST thing done ------------
    ; (SPEC.md 108.4.2: no burst at a round's start. The canvas build is
    ; behind V here, and the round's WHOLE PRESENT - which goes out in bands
    ; from this wake on - is behind it too: while it_vsync is set, it_play
    ; iterates nothing and takes V = now in every wake until it_full is 0.)
    call it_clock_sync
    mov byte [it_vsync], 1
    mov byte [it_gskip], 0          ; ...and the presentation governor starts
    mov byte [it_gquiet], 0         ; a round clear (108.11.6), as the pace does
.rs:                                ; (a swept start does both at READY's end)
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
    jc .pdead
    dec byte [it_sndpc]             ; THE WALL TICK every 4th player cell, a
    jnz .pl                         ; flag the frame's sound takes once at
    mov byte [it_sndpc], IT_SND_CELLS ; most (itsnd.inc): ~40 cycles a cell
    mov byte [it_sndtk], 1
    jmp short .pl
.pdead:
    mov byte [it_winner], WIN_COMP  ; the player crashed: the computer wins NOW
    jmp short .cost
    ; --- each live computer head, in index order -----------------------------------
.comp:
    mov bx, it_heads                ; (by record, no index: it_ai, it_move
.hl:                                ; and it_span keep no CX for it)
    cmp bx, [it_hend]
    jae .hd
    cmp byte [bx + H_ALIVE], 0
    je .hn
    IT_MARK it_hs0                  ; A COMPUTER HEAD-STEP, logic and canvas:
    mov ax, [it_rng]                ; ONE byte a head-move, whatever it decides
    mov dx, 25173                   ; (it_rand, inline: its call, push and
    mul dx                          ; pop were 100 of 273 cycles here)
    add ax, 13849
    mov [it_rng], ax
    mov al, ah
    call it_ai                      ; itai.inc: BX head, AL r
    IT_MARK it_ai1                  ; (the decision's end: SPEC.md 108.10.3)
    mov al, C_COMP
    call it_move
    IT_MARK it_hs1                  ; (the 3,000-cycle line, SPEC.md 108.10.3)
    jnc .hn
    dec byte [it_live]              ; a head died: the next iteration's
    mov ax, [it_B]                  ; cost loses its B
    sub [it_costb], ax
.hn:
    add bx, H_SIZE
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
; out: CF = 1 it crashed (and is dead). Preserves BX; clobbers AX, CX, DX,
; SI, DI, BP.
;
; ONE HEAD-STEP, the unit the per-step budget is priced in: one grid
; compare, one store, the u/d bookkeeping and the checksum. it_place is
; INLINED here (its call, push and pop were ~70 cycles of every step) and
; one 4-byte row of it_mv per heading gives the delta, du and dd off a
; single index - ~467 cycles MEASURED on MartyPC's 5150 (wave 1), the plan's
; 150 being under the 8088's fetch floor for this many instructions. Wave 2
; adds the cell's picture, it_span (itcanvas.inc). it_step0..it_step1
; brackets the two together for the cycle rows.
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
    mov dh, al                      ; (its value, for the span)
    mov ax, [it_ck]
    rol ax, 1
    xor ax, si
    mov [it_ck], ax                 ; ...and ck = rol(ck, 1) XOR idx
    mov [bx + H_IDX], si
    mov ax, [bx + H_U]              ; AL = u, AH = d: one load and one store
    add al, [it_mv + 2 + di]        ; + du
    add ah, [it_mv + 3 + di]        ; + dd
    mov [bx + H_U], ax
    inc byte [bx + H_LEG]
    inc word [it_nsteps]
    IT_MARK it_span0                ; (the move's end, the picture's start)
    call it_span_ud                 ; ...and its wall: one column (108.10)
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
    test byte [it_tflags], IT_TF_HOLD
    jz .flow
    mov byte [it_tpause], 1         ; the harness reads the round, then clears
.flow:
    test byte [it_tflags], IT_TF_NOFLOW
    jz .shown
    or [it_hudd], al                ; the winner's HUD line: 1 left, 2 right
    mov byte [it_msgd], 1           ; - at once, the result with it
    mov byte [it_state], ST_NEXT    ; no crash pause, no wait, no sweep
    ret
.shown:
    mov [it_hudp], al               ; ...the score 0.27 s into CRASH (it_flow)
    push si
    mov si, it_ph_crash             ; ...and the crash's falling sweep, CRASH's
    call it_snd_post                ; whole 0.8 s (itsnd.inc)
    pop si
    call OSAPI_GET_TICKS
    add ax, IT_CRASHT
    mov [it_tend], ax
    mov byte [it_state], ST_CRASH   ; and GAME OVER only after it
    cmp byte [it_lossrow], 3
    jb .out
    inc word [it_gover]
.out:
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
