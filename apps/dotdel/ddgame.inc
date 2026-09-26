; =============================================================================
; os8088 - apps/dotdel/ddgame.inc
;
; THE GAME (SPEC.md 93.7 - 93.9): movement on the grid, the four personalities,
; the modes, the fruit, and the state machine that holds them together.
;
; --- MOVEMENT IS EXACT, AND THAT IS NOT AN OPTIMISATION ----------------------
;
; An actor's position is the sprite box's top-left in 1/16 px, and a DECISION -
; a turn, a wall, eating something, arriving anywhere - happens only when both
; coordinates are exact multiples of the tile. If a step would carry an actor
; PAST a tile origin it is cut at the origin, the decision is taken, and the
; remainder is spent afterwards (dd_advance). So no speed has to divide the
; tile size, an actor can never tunnel through a wall on a long step, and a
; frame the machine dropped costs nothing but time.
;
; That is what makes SPEC.md 93.6's catch-up safe: the logic runs once per tick
; of elapsed time and a step of four ticks is four steps, not one big one.
;
; --- THE EYES (SPEC.md 93.8.3) -----------------------------------------------
;
; A ghost's eyes follow the way it is GOING, which is one byte and free. Two
; things move them off that:
;
;   * a GLANCE - every so often, for a few ticks, at a direction that is not
;     the one it is travelling. It costs a byte of timer and it is the single
;     cheapest thing in this game that makes it look alive;
;   * a LOCK - when the ghost is chasing AND has line of sight to Smiles down
;     its own corridor, the eyes go WIDE and point straight at him. Line of
;     sight is a walk along one row or one column, which is at most 27 tile
;     reads and only for a ghost that shares a row or a column with him.
;
; The wide pair is a different image rather than a different draw: the sprite
; set already carries eight eye states a skirt phase (ddspr.inc), so this
; costs one index and no cycles at all in the frame.
; =============================================================================

; (dx, dy) for DIR_R, DIR_D, DIR_L, DIR_U - and a FIFTH entry for DIR_NONE,
; which is 4 and which several callers can hold. It costs three bytes and it
; makes every one of these tables TOTAL over the direction values that exist:
; a DIR_NONE that reached one used to read the next table along, so dd_rev
; answered DIR_U and dd_dy answered "two rows down". Nothing was corrupted -
; every reader validates the tile it lands on - but "no direction" reversing
; into a real one is the sort of thing that comes back as a ghost turning
; where it should have stood still.
dd_dx: db 1, 0, -1, 0, 0
dd_dy: db 0, 1, 0, -1, 0
; the reverse of each
dd_rev: db DIR_L, DIR_U, DIR_R, DIR_D, DIR_NONE
; the order the ghosts try their turns in - up, left, down, right - which is
; the arcade's own tie-break and is the whole of why a ghost prefers to climb
dd_tryo: db DIR_U, DIR_L, DIR_D, DIR_R

; the scatter corners, in tiles: Blinky, Pinky, Inky, Clyde
dd_scatc: db 25, 2, 27, 0
dd_scatr: db 0, 0, 30, 30

; where each ghost waits, and how many dots have to go before it is let out
dd_ghc:   db DD_HOMEC, DD_HOMEC, DD_HOMEC-2, DD_HOMEC+2
dd_ghr:   db DD_HOMER, DD_HOUSER, DD_HOUSER, DD_HOUSER
dd_ghdot: db 0, 0, 30, 60

; scatter / chase, in ticks at 18.2 Hz: 7s, 20s, 7s, 20s, 5s, 20s, 5s, forever
dd_modet: dw 127, 364, 127, 364, 91, 364, 91, 0
DD_NMODE  equ 8

; how long a pellet frightens them, by level - and it runs out entirely
dd_frit:  dw 145, 127, 109, 91, 73, 91, 55, 55, 36, 91, 55, 36, 36, 18, 0
DD_NFRIT  equ 15

; what a fruit is worth, by level band
dd_frv:   dw 100, 300, 500, 700, 1000, 2000, 3000, 5000
DD_NFRV   equ 8

; --- the death, one entry a tick (SPEC.md 93.5.16) -----------------------------
; DD_DIET entries, indexed by how many ticks have gone: a beat with the mouth
; SHUT and facing up, then it opens through the two Smiles already has and the
; three past them, then the burst, then he is gone. 0FFh is that last state and
; means dd_alive rather than an image - a blank sprite would be a band of black
; laid over the board, and what is wanted is the board.
;
; A TABLE AND NOT ARITHMETIC, because the frames are not evenly held: the beat
; is three ticks and the collapse two each, and the hold is the half of this
; that had to be looked at rather than derived.
DD_DGONE  equ 0FFh
dd_dietab:
    db SP_PAC + DIR_U * 3 + 0, SP_PAC + DIR_U * 3 + 0, SP_PAC + DIR_U * 3 + 0
    db SP_PAC + DIR_U * 3 + 1, SP_PAC + DIR_U * 3 + 1
    db SP_PAC + DIR_U * 3 + 2, SP_PAC + DIR_U * 3 + 2
    db SP_DIE + 0, SP_DIE + 0
    db SP_DIE + 1, SP_DIE + 1
    db SP_DIE + 2, SP_DIE + 2
    db SP_DIE + 3, SP_DIE + 3
    db DD_DGONE, DD_DGONE, DD_DGONE

DD_TILET  equ 4                 ; ticks a tile takes at 100% speed
DD_PCTPAC equ 100               ; Smiles
DD_PCTGH  equ 88                ; a roaming ghost
DD_PCTFRI equ 60                ; ...frightened
DD_PCTEYE equ 190               ; ...and eyes, which go home fast
DD_PCTTUN equ 55                ; anybody in the tunnel
; ...and the same five as an INDEX, because a percentage is a thing to divide
; by and an index is a thing to look up (SPEC.md 93.7.5). dd_relayout resolves
; each one against both axes once; dd_axis_step reads the answer.
SPD_PAC   equ 0
SPD_GH    equ 1
SPD_FRI   equ 2
SPD_EYE   equ 3
SPD_TUN   equ 4
SPD_N     equ 5

DD_GLANCE equ 5                 ; ticks a glance lasts
DD_PENWAIT equ 55               ; ...and ticks a ghost that got home as eyes
                                ; spends wandering the pen before it comes back
                                ; out (3 seconds)
DD_RELT   equ 73                ; ...and ticks of nothing eaten before the pen
                                ; lets one out anyway (4 seconds)
DD_PILLT  equ 5                 ; ticks a pellet spends lit, and dark

; =============================================================================
; STARTING THINGS
; =============================================================================

dd_new_game:
    cmp byte [dd_ok], 0             ; NO LAYOUT, NO GAME (SPEC.md 93.3.4.5):
    jne .go                         ; Enter, a click and Game > New all reach
    ret                             ; here with the refusal still on the glass
.go:
    ; THE WORKER STEPS NOTHING UNTIL THE BOARD IS THE GAME'S. This runs on the
    ; UI task - a key, a click, Game > New - and dd_level_begin clears
    ; [dd_demo] first and then walks the board, which YIELDS to a frame in
    ; flight (dd_board_render, SPEC.md 93.5.17). The worker then took a step
    ; in DDS_ATTRACT on the demo's board with the demo's protection gone:
    ; caught, dd_die, DDS_DIE, a life lost before READY was ever shown - 3
    ; runs of 11 of tests/dotdel.py, read as "ate -108 dots" because the
    ; count it took was the demo's. dd_step returns at once while this is set,
    ; exactly as it does behind the About card.
    mov byte [dd_newg], 1
    mov word [dd_score], 0
    mov word [dd_score+2], 0
    mov byte [dd_lives], DD_LIVES0
    mov byte [dd_level], 1
    mov byte [dd_extra], 0
    mov byte [dd_demo], 0
    call dd_level_begin
    mov byte [dd_newg], 0
    ret

; dd_level_begin - load the board and put everybody back
dd_level_begin:
    mov byte [dd_demo], 0
    mov byte [dd_rmin], 0           ; the WHOLE board: the slice is the
    mov word [dd_clipy0], 0         ; attract screen's, and it is the only
    call dd_level_load              ; thing that ever moves either word
    call dd_layout                  ; put dd_bdy back where a board goes
    call dd_board_render
    call dd_dotcnt_reset            ; a NEW BOARD zeroes it; a lost life does not
    mov byte [dd_fruits], 0
    mov word [dd_frtim], 0
    mov word [dd_fruit], 0
    call dd_actors_home
    mov byte [dd_state], DDS_READY
    mov word [dd_tim], DD_READYT
    mov byte [dd_full], 1
    mov byte [dd_paused], 0
    ret

; dd_actors_home - everybody on their start tile, facing their start way
dd_actors_home:
    mov word [dd_modeidx], 0
    mov ax, [dd_modet]
    mov [dd_modetim], ax
    mov word [dd_frcnt], 0
    mov word [dd_modech], 0
    mov byte [dd_eatn], 0
    mov word [dd_gpause], 0
    mov byte [dd_lastd], DIR_L
    ; Smiles
    xor si, si
    mov cl, DD_STARTC
    mov ch, DD_STARTR
    call dd_put_tile
    mov byte [dd_dir + 0], DIR_L
    mov byte [dd_want + 0], DIR_L
    mov byte [dd_alive + 0], 1
    ; ...and [dd_shown] is DELIBERATELY left alone. Putting everybody back is a
    ; jump of more than a tile, which SPEC.md 93.5.1 already calls a teleport
    ; and already draws as two bands - the tiles it was on, then where it is.
    ; Clearing dd_shown here instead made a lost life owe a WHOLE repaint, and
    ; on a VGA that is 677 ms against a frame's 55: leg E of tests/dotdel.py
    ; read anywhere from 66% to 98% depending on how many times an unsteered
    ; Smiles died inside its eight-second window.
    ; the four ghosts
    mov si, 1
.g:
    mov bx, si
    dec bx                          ; BX = the ghost's ordinal
    mov cl, [dd_ghc + bx]
    mov ch, [dd_ghr + bx]
    call dd_put_tile
    mov byte [dd_alive + si], 1
    mov byte [dd_dir + si], DIR_L
    mov byte [dd_want + si], DIR_L
    mov bx, si
    dec bx
    mov byte [dd_gs + bx], GS_HOUSE
    mov byte [dd_glance + bx], 0
    mov byte [dd_gbig + bx], 0
    or bx, bx
    jnz .penned
    mov byte [dd_gs + bx], GS_ROAM  ; the red one starts outside, as it always
    mov byte [dd_dir + si], DIR_L   ; has
.penned:
    mov byte [dd_ghunt + bx], 0
    mov byte [dd_gsee + bx], 0
    mov byte [dd_gwout + bx], 0
    mov byte [dd_gwait + bx], 0
    inc si
    cmp si, DD_NACT
    jb .g
    mov word [dd_relt], 0
    call dd_anims                   ; ...and give everybody an image and a PEN
    ret                             ; before the first frame draws them in
                                    ; colour 0, which is the background

; -----------------------------------------------------------------------------
; dd_actors_resnap - put every actor back on the origin of the tile it is on
;
; AN ACTOR'S POSITION IS IN UNITS OF THE TILE, so a relayout that changes the
; TILE leaves all five of them at coordinates that mean nothing. The field
; report was *"going fullscreen in the middle of play sets everything
; misaligned, Smiles doesn't draw in his lane, ghosts too"* - and it is not
; only the bracket: dragging the window between two displays of different
; shapes re-cuts the tile the same way (SPEC.md 93.4).
;
; The TILE each of them is on is still right - dd_ac/dd_ar are tile numbers -
; so re-deriving the position from it is the whole fix. What it costs is the
; fraction of a tile they had crossed, once, on a frame where the board is
; being rebuilt from scratch anyway.
; -----------------------------------------------------------------------------
dd_actors_resnap:
    push si
    xor si, si
.a:
    mov cl, [dd_ac + si]
    mov ch, [dd_ar + si]
    call dd_put_tile
    mov byte [dd_shown + si], 0     ; ...and nothing is on the glass where the
    inc si                          ; old coordinates said it was
    cmp si, DD_NACT
    jb .a
    pop si
    ret

; dd_dotcnt_reset - the per-BOARD dot tally the pen's release counts run off.
; It is reset when a board is laid and NOT when a life is lost: zeroing it on a
; death put ghosts 2 and 3 back behind the 30 and 60 dots they had already
; earned, so a player who kept dying was played against two ghosts for ever.
dd_dotcnt_reset:
    mov word [dd_eaten], 0
    mov word [dd_relt], 0
    ret

; -----------------------------------------------------------------------------
; dd_put_tile - actor SI onto the origin of tile CL, CH
;
; It sets FOUR things and not two: the position, and the TILE that position is
; in, in tiles and in sub-pixels. Those two are then carried along by
; dd_advance rather than divided out of the position every time somebody asks
; - which is what dd_tile_of used to do, twice per call, at ~160 cycles a
; 16-bit DIV on an 8088, from a dozen call sites a tick.
; -----------------------------------------------------------------------------
dd_put_tile:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    shl bx, 1
    mov [dd_ac + si], cl
    mov [dd_ar + si], ch
    mov al, cl
    xor ah, ah
    mul word [dd_twS]
    mov [dd_x + bx], ax
    mov [dd_acx + bx], ax
    mov al, ch
    xor ah, ah
    mul word [dd_thS]
    mov [dd_y + bx], ax
    mov [dd_ary + bx], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ONE LOGIC STEP - exactly one system tick of game time (SPEC.md 93.6)
; =============================================================================
dd_step:
    call dd_kbdrain                 ; ABOVE the early returns: a refused
    cmp byte [dd_ok], 0             ; layout is exactly where nothing else
    jne .laid                       ; would ever touch the buffer
    ret                             ; no layout yet, so no tiles, no speeds and
.laid:                              ; nothing to divide by
    cmp byte [dd_newg], 0           ; ...or while the UI task is swapping the
    jne .held                       ; demo's board for a new game's (dd_new_game)
    cmp byte [dd_abon], 0
    je .live
.held:
    ret                             ; SUSPENDED behind the About card, which is
.live:                              ; why nothing under it can change and the
    inc word [dd_anim]
    call dd_pill_tick
    call dd_wak_tick                ; the bite's second syllable (93.10.1)
    mov al, [dd_state]
    cmp al, DDS_ATTRACT
    je .attract
    cmp al, DDS_ENTER
    je .rout                        ; ...and NOTHING happens here (SPEC.md
                                    ; 93.12.4): the board is frozen under the
                                    ; panel, so the only thing this state does
                                    ; is take letters. It used to run the
                                    ; ATTRACT stepper - which is the other half
                                    ; of why the initials were typed on the
                                    ; attract screen - and once the demo flag
                                    ; stopped being set with it, that stepper
                                    ; ran dd_collide over a Smiles nothing was
                                    ; protecting: caught, dd_die, DDS_DIE, and
                                    ; the game went round DIE -> OVER -> ENTER
                                    ; -> DIE for ever
    cmp al, DDS_READY
    je .ready
    cmp al, DDS_PLAY
    je .play
    cmp al, DDS_DIE
    je .die
    cmp al, DDS_CLEAR
    je .clear
    cmp al, DDS_OVER
    je .over
    ret
.attract:
    call dd_attract_step
    ret
.ready:
    dec word [dd_tim]
    jnz .rout
    mov byte [dd_state], DDS_PLAY
    mov byte [dd_hudd], 1
.rout:
    ret
.play:
    cmp byte [dd_paused], 0
    jne .rout
    call dd_play_step
    ret
.die:
    call dd_die_anim                ; BEFORE the dec, so [dd_tim] runs DD_DIET
    dec word [dd_tim]               ; down to 1 and the table is indexed 0..n-1
    jnz .rout
    call dd_life_lost
    ret
.clear:
    dec word [dd_tim]
    jnz .rout
    inc byte [dd_level]
    call dd_level_begin
    ret
.over:
    dec word [dd_tim]
    jnz .rout
    call dd_hs_offer                ; a table place is typed, not awarded
    ret

; dd_pill_tick - the pellets' own clock, which runs in every state
dd_pill_tick:
    dec byte [dd_pilt]
    jns .out
    mov byte [dd_pilt], DD_PILLT
    xor byte [dd_pilon], 1
    mov byte [dd_pilrf], 1          ; ...and the glass owes them a refresh
.out:
    ret

; -----------------------------------------------------------------------------
; dd_play_step - one tick of an actual game
; -----------------------------------------------------------------------------
dd_play_step:
    cmp word [dd_gpause], 0
    je .go
    dec word [dd_gpause]            ; the beat after a ghost is eaten
    ret
.go:
    call dd_input
    call dd_modes
    xor si, si
    call dd_act_move                ; Smiles first: the ghosts aim at where he
                                    ; ends up, which is the arcade's own order,
                                    ; and dd_ontile eats what he crossed
    mov si, 1
.g:
    push si
    call dd_gh_think
    call dd_act_move
    pop si
    inc si
    cmp si, DD_NACT
    jb .g
    call dd_collide
    call dd_fruit_tick
    call dd_release_ck
    call dd_anims
    cmp word [dd_ndots], 0
    jne .out
    mov byte [dd_state], DDS_CLEAR
    mov word [dd_tim], DD_CLEART
.out:
    ret

; -----------------------------------------------------------------------------
; dd_input - the steering, polled (SPEC.md 93.7.2)
;
; OSAPI_KEY_DOWN asks whether a key IS DOWN, which is the question a maze game
; has: a key event arrives at the typematic rate, so a held arrow gives one
; event, a pause, and then a stream - and a turn that has to be taken within
; two tiles of a junction cannot be built on that.
;
; A direction that is not legal yet is REMEMBERED rather than dropped, so a
; turn asked for a few pixels early is taken at the junction. That is the whole
; of what "responsive" means here.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dd_kbdrain - eat the STEERING keys out of the BIOS buffer (SPEC.md 93.7.2.1)
; in:  nothing; out: nothing; preserves all registers
;
; dd_input reads the arrows and WASD as key STATE and consumes nothing, and
; dd_key_common ignores them - so windowed, nothing empties int 16h's buffer
; while a key is held, and SPEC.md 9.8 then drops the NEWEST arrival, which is
; the P or Esc the player just typed. cy_kbdrain's shape (apps/cyclone): peek,
; eat only the eight steering codes, STOP at anything else so a command keeps
; its place, peek and fetch inside one pushf/cli. The bracket's own loop drains
; to empty, so there this finds the buffer empty and costs one compare.
; -----------------------------------------------------------------------------
dd_kbdrain:
    push ax
    push es
    mov ax, 0x0040                  ; the BIOS data area, for int 16h's own
    mov es, ax                      ; buffer - head 001A, tail 001C
.again:
    mov ax, [es:0x1A]
    cmp ax, [es:0x1C]
    je .out                         ; empty: the common case, and no int at all
    pushf
    cli
    mov ah, 0x01                    ; peek: AH = scan, and the key stays put
    int 0x16
    jz .pop                         ; emptied under us after all
    cmp ah, KSC_LEFT
    je .eat
    cmp ah, KSC_RIGHT
    je .eat
    cmp ah, KSC_UP
    je .eat
    cmp ah, KSC_DOWN
    je .eat
    cmp ah, 0x1E                    ; A
    je .eat
    cmp ah, 0x20                    ; D
    je .eat
    cmp ah, 0x11                    ; W
    je .eat
    cmp ah, 0x1F                    ; S
    jne .pop                        ; anything else keeps its place
.eat:
    xor ah, ah
    int 0x16
    popf
    jmp short .again
.pop:
    popf
.out:
    pop es
    pop ax
    ret

dd_input:
    cmp byte [dd_demo], 0
    jne .ai
    cmp byte [dd_hasfoc], 0
    je .out                         ; somebody else has the keyboard
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jc .left
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jc .right
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jc .up
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jc .down
    mov al, 0x1E                    ; A
    call OSAPI_KEY_DOWN
    jc .left
    mov al, 0x20                    ; D
    call OSAPI_KEY_DOWN
    jc .right
    mov al, 0x11                    ; W
    call OSAPI_KEY_DOWN
    jc .up
    mov al, 0x1F                    ; S
    call OSAPI_KEY_DOWN
    jc .down
.out:
    ret
.left:
    mov byte [dd_want], DIR_L
    ret
.right:
    mov byte [dd_want], DIR_R
    ret
.up:
    mov byte [dd_want], DIR_U
    ret
.down:
    mov byte [dd_want], DIR_D
    ret
.ai:
    call dd_demo_think
    ret

; =============================================================================
; MOVEMENT
; =============================================================================

; -----------------------------------------------------------------------------
; dd_act_move - advance actor SI by one tick of its own speed
; in:  SI = actor. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_act_move:
    push si
    call dd_speed_of                ; AX = which of the five speeds this actor
    mov [dd_spdi], ax               ; is on right now (SPEC.md 93.7.5)
    ; the budget, in 1/16 px along whichever axis it is on
    call dd_axis_step               ; AX = 1/16 px this tick along dir
    mov [dd_rem], ax
.loop:
    cmp word [dd_rem], 0
    jle .done
    call dd_at_tile
    jnc .move
    call dd_ontile                  ; SPEC.md 93.7.3: the decision is taken
    call dd_decide                  ; HERE, standing on the tile, and not once
.move:                              ; a tick from outside
    mov al, [dd_dir + si]
    cmp al, DIR_NONE
    je .done
    call dd_to_edge                 ; AX = 1/16 px to the next tile origin
    cmp ax, [dd_rem]
    jbe .part
    mov ax, [dd_rem]
.part:
    call dd_advance                 ; ...and take it
    sub [dd_rem], ax
    jmp short .loop
.done:
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_ontile - actor SI has just arrived on a tile origin (SPEC.md 93.7.3)
;
; THIS IS WHERE A DECISION IS TAKEN, and it has to be, because an actor is only
; on a tile origin for an instant INSIDE dd_act_move. A tick's budget is a
; fixed 1/16-px count and a tile is 256 of them: at Smiles' 100% the two divide
; and he lands on every origin at the end of a tick, and at a ghost's 88% they
; do not - 56 into 256 comes out exactly once every 32 tiles. So a decision
; taken once a tick from outside the mover reached a penned ghost about twice a
; minute, which is why the ghosts appeared not to chase and why two of them
; paced the pen for ever: the only place they ever DID think was standing at a
; wall, where dd_decide had stopped them dead on an origin.
;
; What stays outside is what is about the TICK rather than the tile - sight,
; the eyes, the animation clocks.
; -----------------------------------------------------------------------------
dd_ontile:
    or si, si
    jz .pac
    call dd_gh_turn
    ret
.pac:
    call dd_eat_here                ; ...and every dot he crosses, on the tile
    ret                             ; he crosses it on

; dd_at_tile - CF = 1 when actor SI is exactly on a tile origin
dd_at_tile:
    push ax
    push bx
    mov bx, si
    shl bx, 1
    mov ax, [dd_x + bx]
    cmp ax, [dd_acx + bx]
    jne .no
    mov ax, [dd_y + bx]
    cmp ax, [dd_ary + bx]
    jne .no
    pop bx
    pop ax
    stc
    ret
.no:
    pop bx
    pop ax
    clc
    ret

; dd_tile_of - CL = actor SI's column, CH = its row (the tile its box sits in)
dd_tile_of:
    mov cl, [dd_ac + si]
    mov ch, [dd_ar + si]
    ret

; dd_to_edge - AX = 1/16 px from actor SI to the next tile origin along its
;              direction. A whole tile when it is standing on one.
dd_to_edge:
    push bx
    push cx
    push dx
    mov bx, si
    shl bx, 1
    mov al, [dd_dir + si]
    cmp al, DIR_R
    je .r
    cmp al, DIR_L
    je .l
    cmp al, DIR_D
    je .d
    ; up: back to this tile's own origin, or a whole tile when it is on one
    mov ax, [dd_y + bx]
    sub ax, [dd_ary + bx]
    jnz .out
    mov ax, [dd_thS]
    jmp short .out
.d:
    mov ax, [dd_ary + bx]
    add ax, [dd_thS]
    sub ax, [dd_y + bx]
    jmp short .out
.l:
    mov ax, [dd_x + bx]
    sub ax, [dd_acx + bx]
    jnz .out
    mov ax, [dd_twS]
    jmp short .out
.r:
    mov ax, [dd_acx + bx]
    add ax, [dd_twS]
    sub ax, [dd_x + bx]
.out:
    pop dx
    pop cx
    pop bx
    ret

; dd_advance - move actor SI by AX (1/16 px) along its direction, and wrap the
;              tunnel when it walks off the end of the world
; A step is never longer than one tile (dd_act_move cuts it at the edge), so
; the tile this carries along moves by at most one and the fix-up is a compare
; and an add rather than a divide.
dd_advance:
    push ax
    push bx
    push cx
    mov bx, si
    shl bx, 1
    mov cl, [dd_dir + si]
    cmp cl, DIR_R
    je .r
    cmp cl, DIR_L
    je .l
    cmp cl, DIR_D
    je .d
    ; --- up ----------------------------------------------------------------
    sub [dd_y + bx], ax
    mov ax, [dd_y + bx]
    cmp ax, [dd_ary + bx]
    jae .out
    mov ax, [dd_thS]
    sub [dd_ary + bx], ax
    dec byte [dd_ar + si]
    jmp .out
.d:
    add [dd_y + bx], ax
    mov ax, [dd_ary + bx]
    add ax, [dd_thS]
    cmp [dd_y + bx], ax
    jb .out
    mov [dd_ary + bx], ax
    inc byte [dd_ar + si]
    jmp .out
.l:
    sub [dd_x + bx], ax
    jc .lcross                      ; THE TUNNEL'S LEFT MOUTH (SPEC.md 93.7.4).
                                    ; A position is UNSIGNED, so at column 0 -
                                    ; where the tile's own origin is 0 - a step
                                    ; past it borrows and the compare below
                                    ; reads the result as a very large x rather
                                    ; than as a crossing. The tile was never
                                    ; decremented, .lwrap had nothing negative
                                    ; to wrap, and Smiles walked off the left of
                                    ; the world: never on a tile origin again,
                                    ; so never deciding anything again, and
                                    ; clipped off the glass. The BORROW is the
                                    ; crossing, and it is the only case the
                                    ; compare cannot see
    mov ax, [dd_x + bx]
    cmp ax, [dd_acx + bx]
    jae .lwrap
.lcross:
    mov ax, [dd_twS]
    sub [dd_acx + bx], ax
    dec byte [dd_ac + si]
.lwrap:
    cmp byte [dd_ac + si], 0
    jge .out
    add byte [dd_ac + si], DD_COLS  ; the tunnel: off the left is column 27
    mov ax, [dd_mwS]
    add [dd_x + bx], ax
    add [dd_acx + bx], ax
    jmp short .out
.r:
    add [dd_x + bx], ax
    mov ax, [dd_acx + bx]
    add ax, [dd_twS]
    cmp [dd_x + bx], ax
    jb .out
    mov [dd_acx + bx], ax
    inc byte [dd_ac + si]
    cmp byte [dd_ac + si], DD_COLS
    jb .out
    sub byte [dd_ac + si], DD_COLS
    mov ax, [dd_mwS]
    sub [dd_x + bx], ax
    sub [dd_acx + bx], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_axis_step - AX = how far actor SI moves this tick, in 1/16 px, along the
;                axis it is on. [dd_spdi] is which of the five speeds it is on.
; -----------------------------------------------------------------------------
dd_axis_step:
    push bx
    push cx
    push dx
    ; A LOOKUP, NOT A MUL AND A DIV (SPEC.md 93.7.5). There are five speeds in
    ; this game and two axes, so the ten answers are resolved once in
    ; dd_relayout and this is an index into them - where it used to be a
    ; 16-bit multiply and a 16-bit divide per actor per tick, about 300 cycles
    ; five times a tick for one of ten constants.
    mov bx, [dd_spdi]
    shl bx, 1
    mov al, [dd_dir + si]
    cmp al, DIR_D
    je .vert
    cmp al, DIR_U
    je .vert
    mov ax, [dd_stpx + bx]
    jmp short .out
.vert:
    mov ax, [dd_stpy + bx]
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_speed_of - AX = actor SI's speed percentage right now
; -----------------------------------------------------------------------------
dd_speed_of:
    push bx
    push cx
    or si, si
    jz .pac
    mov bx, si
    dec bx
    mov al, [dd_gs + bx]
    cmp al, GS_EYES
    je .eyes
    cmp al, GS_FRIGHT
    je .fri
    mov ax, SPD_GH
    jmp short .tun
.fri:
    mov ax, SPD_FRI
    jmp short .tun
.eyes:
    mov ax, SPD_EYE
    jmp short .out
.pac:
    mov ax, SPD_PAC
.tun:
    ; the tunnel is slow for everybody but a pair of eyes
    push ax
    call dd_tile_of
    cmp ch, DD_TUNR
    jne .notun
    cmp cl, DD_TUNW
    jb .intun
    cmp cl, DD_COLS - 1 - DD_TUNW
    ja .intun
.notun:
    pop ax
    jmp short .out
.intun:
    pop ax
    mov ax, SPD_TUN
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_decide - actor SI is on a tile origin: take its turn, or stop
; -----------------------------------------------------------------------------
dd_decide:
    push ax
    push bx
    push cx
    push dx
    call dd_tile_of                 ; CL, CH = where it is
    mov [dd_dc], cx
    ; the wanted direction, if it is legal from here
    mov al, [dd_want + si]
    cmp al, DIR_NONE
    je .keep
    cmp al, [dd_dir + si]
    je .keep
    call dd_can_go
    jnc .keep
    mov al, [dd_want + si]
    mov [dd_dir + si], al
    jmp short .out
.keep:
    mov al, [dd_dir + si]
    cmp al, DIR_NONE
    je .out
    call dd_can_go
    jc .out
    mov byte [dd_dir + si], DIR_NONE
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_can_go - CF = 1 when actor SI may step from tile [dd_dc] in direction AL
;
; The ghost-house door is the one tile whose answer depends on who is asking:
; a ghost on its way out or its way home walks through it, and Smiles never
; does.
; -----------------------------------------------------------------------------
dd_can_go:
    push ax
    push bx
    push cx
    push dx
    mov bl, al
    xor bh, bh
    mov cx, [dd_dc]
    add cl, [dd_dx + bx]
    add ch, [dd_dy + bx]
    call dd_tile
    cmp al, TT_WALL
    je .no
    cmp al, TT_DOOR
    jne .yes
    or si, si
    jz .no                          ; Smiles may not
    mov bx, si
    dec bx
    mov al, [dd_gs + bx]
    cmp al, GS_OUT
    je .yes
    cmp al, GS_EYES
    je .yes
    cmp al, GS_HOUSE
    je .yes
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.yes:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; =============================================================================
; THE GHOSTS (SPEC.md 93.8)
; =============================================================================

; -----------------------------------------------------------------------------
; dd_gh_think - ghost SI's TICK: what it can see, and where its eyes are
; in:  SI = 1..4. Clobbers everything but SI. The TURN is dd_gh_turn, taken on
;      the tile itself.
; -----------------------------------------------------------------------------
dd_gh_think:
    push si
    mov bx, si
    dec bx
    mov [dd_gi], bx                 ; the ghost's ordinal, 0..3
    cmp byte [dd_gs + bx], GS_ROAM
    je .look
    mov byte [dd_gsee + bx], 0      ; only a ghost on the board hunts, and one
    mov byte [dd_ghunt + bx], 0     ; that stops hunting forgets the trail
    cmp byte [dd_gs + bx], GS_HOUSE
    jne .eyes
    cmp byte [dd_gwait + bx], 0     ; ...and one that came home as eyes is on a
    je .eyes                        ; clock of its own, not the pen's dot count
    dec byte [dd_gwait + bx]
    jnz .eyes
    mov byte [dd_gs + bx], GS_OUT
    jmp short .eyes
.look:
    call dd_gh_look
.eyes:
    call dd_gh_eyes
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_gh_turn - ghost SI's TILE: which way it leaves the one it is standing on
; in:  SI = 1..4, on an exact tile origin. Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_gh_turn:
    push si
    mov bx, si
    dec bx
    mov [dd_gi], bx
    mov al, [dd_gs + bx]
    cmp al, GS_HOUSE
    je .house
    cmp al, GS_OUT
    je .out_
    cmp al, GS_EYES
    je .eyes
    cmp al, GS_FRIGHT
    je .fright
    call dd_gh_roam
    jmp short .done
.house:
    call dd_gh_house
    jmp short .done
.out_:
    call dd_gh_leave
    jmp short .done
.eyes:
    call dd_gh_home
    jmp short .done
.fright:
    call dd_gh_flee
.done:
    pop si
    ret

; --- wandering the pen (SPEC.md 93.8.6) ---------------------------------------
;
; The pen's interior is a SIX BY THREE open box in all three layouts, and a
; penned ghost drifts round it: it keeps going while it can, turns at random
; about a quarter of the time it could have, and picks a new way when the box
; runs out. It used to bob one tile up and one tile down, which the walls did
; for it and which reads as a machine waiting rather than a ghost.
;
; dd_pen_ok, not dd_gh_legal, is what says where it may step - THE DOOR IS NOT
; A WAY OUT HERE. dd_can_go opens the door to any GS_HOUSE ghost (that is how
; dd_gh_leave walks through it), so a wander that consulted the ordinary rule
; would let one stroll out before the pen released it.
dd_gh_house:
    call dd_at_tile
    jnc .out
    ; A PAUSE ON THE TILE, and it is not only for the look of it (SPEC.md
    ; 93.5.8): an actor that did not move and whose image did not change is a
    ; band dd_actor_prep does not compose, and the skirt only flaps every
    ; fourth tick - so a ghost that hovers costs a quarter of one that drifts.
    ; Three of them wandering a 6x3 box was 4 points of a VGA's frame rate.
    call OSAPI_RAND
    test al, 1
    jz .hover
    mov al, [dd_dir + si]
    cmp al, DIR_NONE
    je .pick
    call dd_pen_ok
    jnc .pick                       ; the box ran out: it has to turn
    call OSAPI_RAND
    test al, 3
    jnz .out                        ; ...otherwise mostly carry straight on
.pick:
    call OSAPI_RAND                 ; a random start, then the first way that
    and ax, 3                       ; stays in the box and is not straight back
    mov [dd_gtry], ax
    mov cx, 4
.try:
    mov bx, [dd_gtry]
    and bx, 3
    mov al, bl
    push cx
    call dd_pen_ok
    pop cx
    jnc .next
    push cx
    push ax
    mov bl, [dd_dir + si]
    xor bh, bh
    cmp al, [dd_rev + bx]
    pop ax
    pop cx
    jne .take
.next:
    inc word [dd_gtry]
    loop .try
    mov bl, [dd_dir + si]           ; nothing but a reversal, so take that -
    xor bh, bh                      ; which is a corner of a 6x3 box with the
    mov al, [dd_rev + bx]           ; ghost facing its own wall
    call dd_pen_ok
    jnc .out
.take:
    mov [dd_want + si], al          ; both, as dd_gh_leave does: a pen turn is
    mov [dd_dir + si], al           ; usually a reversal, and dd_dir must never
.out:                               ; be left pointing at the door
    ret
.hover:
    mov byte [dd_want + si], DIR_NONE   ; stand still for a tick. It is on the
    mov byte [dd_dir + si], DIR_NONE    ; tile, so it thinks again next tick
    ret

; dd_pen_ok - CF = 1 when ghost SI may step in direction AL and still be inside
;             the pen. Preserves every register (flags aside).
dd_pen_ok:
    push ax
    push bx
    push cx
    mov bl, al
    xor bh, bh
    call dd_tile_of
    add cl, [dd_dx + bx]
    add ch, [dd_dy + bx]
    cmp cl, DD_PENC0
    jb .no
    cmp cl, DD_PENC1
    ja .no
    cmp ch, DD_PENR0
    jb .no
    cmp ch, DD_PENR1
    ja .no
    call dd_tile
    cmp al, TT_WALL
    je .no
    pop cx
    pop bx
    pop ax
    stc
    ret
.no:
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dd_gh_leave - walking out of the pen, and it is SCRIPTED
;
; It used to be dd_gh_aim at the tile above the door, and that is the bug that
; left two ghosts bobbing in the pen for ever: dd_gh_legal will not let a ghost
; REVERSE, and a ghost let out while its bob was carrying it downwards can
; never turn round to face the door. It then took whichever sideways turn was
; nearest and paced the pen, animating, for the rest of the life.
;
; The pen is three tiles by six and its exit is one column, so there is nothing
; for a pathfinder to work out: slide to DD_HOMEC, then go up. Setting dd_dir
; as well as dd_want is what makes the turn a reversal the ghost is allowed.
; -----------------------------------------------------------------------------
dd_gh_leave:
    call dd_at_tile
    jnc .out
    call dd_tile_of
    cmp ch, DD_HOMER
    jbe .arrived                    ; through the door and out
    cmp cl, DD_HOMEC
    je .up
    mov al, DIR_R
    jb .go
    mov al, DIR_L
.go:
    mov [dd_want + si], al
    mov [dd_dir + si], al
    ret
.up:
    mov byte [dd_want + si], DIR_U
    mov byte [dd_dir + si], DIR_U
.out:
    ret
.arrived:
    mov bx, [dd_gi]
    mov byte [dd_gs + bx], GS_ROAM
    mov byte [dd_want + si], DIR_L
    ret

; --- eyes going home ----------------------------------------------------------
;
; ONE TABLE READ, and no search at all (SPEC.md 93.8.5). Every greedy version
; of this failed the same way - dd_gh_aim descends a distance it cannot see
; round corners, so the eyes took a side corridor and either circled the board
; or ping-ponged between two tiles for ever. dd_home_map walks the board once
; when it is decoded and leaves, in every reachable tile, the direction that
; steps one tile closer to the pen; here that is a byte to read.
;
; dd_dir is written as well as dd_want because the route may double back on
; the way the eyes were travelling, and dd_gh_legal will not let a ghost
; reverse. The route is a shortest path, so a step it names is always onto a
; tile that is not a wall.
dd_gh_home:
    call dd_at_tile
    jnc .out
    call dd_tile_of
    cmp cl, DD_HOMEC
    jne .route
    cmp ch, DD_HOUSER
    je .arrived
.route:
    call dd_tindex
    mov bx, ax
    mov al, [dd_hdir + bx]
    cmp al, 4
    jae .aim                        ; unmapped: a board with no pen, or a tile
    mov [dd_want + si], al          ; the walk could not reach
    mov [dd_dir + si], al
    ret
.aim:
    mov al, DD_HOMEC
    mov ah, DD_HOMER
    call dd_gh_aim
.out:
    ret
.arrived:
    mov bx, [dd_gi]                 ; back in the pen: it re-grows a body and
    mov byte [dd_gs + bx], GS_HOUSE ; wanders it for DD_PENWAIT ticks before it
    mov byte [dd_gwait + bx], DD_PENWAIT    ; comes back out (SPEC.md 93.8.6)
    mov byte [dd_want + si], DIR_L
    mov byte [dd_dir + si], DIR_L
    ret

; --- frightened: a real choice, taken at random -------------------------------
dd_gh_flee:
    call dd_at_tile
    jnc .out
    call OSAPI_RAND
    and ax, 3
    mov [dd_gtry], ax
    mov cx, 4
.try:
    mov bx, [dd_gtry]
    and bx, 3
    mov al, bl
    push cx
    call dd_gh_legal
    pop cx
    jc .take
    inc word [dd_gtry]
    loop .try
    ret
.take:
    mov bx, [dd_gtry]
    and bx, 3
    mov [dd_want + si], bl
.out:
    ret

; -----------------------------------------------------------------------------
; dd_gh_roam - a ghost that is out, whole and not frightened (SPEC.md 93.8.1)
;
; SIGHT starts a chase and MEMORY carries it. While a ghost has a clear line
; down its own row or column to Smiles it goes straight for him, through its
; own personality; and every tick it can see him it writes down the tile he is
; on. When he goes out of view it heads for that tile - the mouth of the lane
; he turned into - and if he is still not in view when it gets there it gives
; up and goes back to patrolling its own corner.
;
; That is deliberately not the arcade's model: the arcade ghost knows where
; Smiles is at every moment, which on a board this size reads as unfair and
; gives a player no way to break a chase. Here breaking one is a turn taken
; out of sight.
; -----------------------------------------------------------------------------
dd_gh_roam:
    call dd_at_tile
    jnc .out
    mov bx, [dd_gi]
    cmp byte [dd_ghunt + bx], 0
    je .wander
    cmp byte [dd_gsee + bx], 0
    jne .onhim
    ; on the trail but blind: the tile he was last seen on
    call dd_tile_of                 ; CL/CH = where this ghost is
    mov al, [dd_ghlc + bx]
    cmp al, cl
    jne .lane
    mov al, [dd_ghlr + bx]
    cmp al, ch
    jne .lane
    mov byte [dd_ghunt + bx], 0     ; got there, and he is not in sight of it
    jmp short .wander
.lane:
    mov al, [dd_ghlc + bx]
    mov ah, [dd_ghlr + bx]
    call dd_gh_aim
    ret
.onhim:
    ; STRAIGHT AT HIM, and that is the whole of it: dd_gh_sees walked the row
    ; or column between them and found no wall, so the direction it handed back
    ; is both legal and closing. Aiming with a target instead let the arcade's
    ; personalities send a ghost that could see Smiles four tiles PAST him, and
    ; dd_gh_aim's no-reverse rule then kept it going that way - which is the
    ; field report, "they move in the opposite direction I am going even though
    ; I am still in that lane". Writing dd_dir as well as dd_want is what makes
    ; the turn a reversal it is allowed: a ghost that has just caught sight of
    ; Smiles behind it turns round.
    mov al, [dd_gseed + bx]
    mov [dd_want + si], al
    mov [dd_dir + si], al
    ret
.wander:
    call dd_gh_scat
    call dd_gh_aim
.out:
    ret

; -----------------------------------------------------------------------------
; dd_gh_look - can ghost SI see Smiles this tick, and where has he got to?
; Banks [dd_gsee], [dd_gseed] and, while he is in view, the tile to remember.
; Clobbers everything but SI.
; -----------------------------------------------------------------------------
dd_gh_look:
    call dd_gh_sees                 ; AL = the direction, when it can
    mov bx, [dd_gi]
    jc .seen
    mov byte [dd_gsee + bx], 0
    ret
.seen:
    mov byte [dd_gsee + bx], 1
    mov [dd_gseed + bx], al
    mov byte [dd_ghunt + bx], 1
    push si
    xor si, si
    call dd_tile_of                 ; the lane he is in NOW is the one to make
    pop si                          ; for when he leaves it
    mov bx, [dd_gi]
    mov [dd_ghlc + bx], cl
    mov [dd_ghlr + bx], ch
    ret

; -----------------------------------------------------------------------------
; dd_gh_scat - AL/AH = ghost [dd_gi]'s own corner, which is what it patrols
;              when it is not tracking anybody
; -----------------------------------------------------------------------------
dd_gh_scat:
    push bx
    mov bx, [dd_gi]
    mov al, [dd_scatc + bx]
    mov ah, [dd_scatr + bx]
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_gh_aim - point ghost SI at tile (AL, AH): of the legal turns, take the one
;             whose next tile is nearest, and never reverse
;
; The try order is up, left, down, right, so an exact tie goes the way the
; arcade's does - which is what makes a ghost climb out of a corner rather
; than pace in it.
; -----------------------------------------------------------------------------
dd_gh_aim:
    push bx
    push cx
    push dx
    push di
    mov [dd_tgt], ax
    mov word [dd_best], 0FFFFh
    mov byte [dd_bestd], DIR_NONE
    xor di, di
.try:
    mov bx, di
    mov al, [dd_tryo + bx]
    push ax
    call dd_gh_legal
    pop ax
    jnc .next
    ; the tile that direction leads to
    push ax
    call dd_tile_of
    pop ax
    mov bl, al
    xor bh, bh
    add cl, [dd_dx + bx]
    add ch, [dd_dy + bx]
    ; ...and its distance, squared, from the target
    mov al, cl
    sub al, byte [dd_tgt]
    cbw
    mov bx, ax
    imul bx
    mov dx, ax
    mov al, ch
    sub al, byte [dd_tgt + 1]
    cbw
    mov bx, ax
    imul bx
    add ax, dx
    cmp ax, [dd_best]
    jae .next
    mov [dd_best], ax
    mov bx, di
    mov al, [dd_tryo + bx]
    mov [dd_bestd], al
.next:
    inc di
    cmp di, 4
    jb .try
    mov al, [dd_bestd]
    cmp al, DIR_NONE
    je .out
    mov [dd_want + si], al
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; dd_gh_legal - CF = 1 when ghost SI may turn to direction AL from where it is
;               (not a wall, and not straight back the way it came)
dd_gh_legal:
    push ax
    push bx
    push cx
    mov bl, [dd_dir + si]           ; a ghost may not reverse: the rule is what
    cmp bl, DIR_NONE                ; stops one pacing in front of the player
    je .norev
    xor bh, bh
    cmp al, [dd_rev + bx]
    je .no
.norev:
    push ax
    call dd_tile_of
    mov [dd_dc], cx
    pop ax
    call dd_can_go
    jc .yes
.no:
    pop cx
    pop bx
    pop ax
    clc
    ret
.yes:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dd_gh_eyes - where ghost SI is looking, and how wide (SPEC.md 93.8.3)
; -----------------------------------------------------------------------------
dd_gh_eyes:
    push bx
    mov bx, [dd_gi]
    mov byte [dd_gbig + bx], 0
    ; a glance, if one is running
    cmp byte [dd_glance + bx], 0
    je .noglance
    dec byte [dd_glance + bx]
    jmp short .out
.noglance:
    mov al, [dd_dir + si]
    cmp al, DIR_NONE
    je .keeplook                    ; STANDING STILL LOOKS WHERE IT LOOKED. It
    mov [dd_glook + bx], al         ; used to snap to DIR_L, which on a penned
.keeplook:                          ; ghost's hover tick (SPEC.md 93.5.8) is a
                                    ; twitch every other tick
    ; TRACKING is what widens them, and dd_gh_look has already decided it
    cmp byte [dd_gsee + bx], 0
    je .roll
    mov byte [dd_gbig + bx], 1
    mov al, [dd_gseed + bx]
    mov [dd_glook + bx], al
    jmp short .out
.roll:
    cmp byte [dd_ghunt + bx], 0
    jne .out                        ; on a trail: it is not looking around
    call OSAPI_RAND                 ; ...and every so often it looks away
    and ax, 63
    cmp ax, 1
    ja .out
    mov bx, [dd_gi]
    mov byte [dd_glance + bx], DD_GLANCE
    call OSAPI_RAND
    and al, 3
    mov bx, [dd_gi]
    mov [dd_glook + bx], al
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dd_gh_sees - CF = 1 and AL = the direction, when ghost SI shares an open row
;              or column with Smiles
; Clobbers AX/BX/CX/DX.
; -----------------------------------------------------------------------------
dd_gh_sees:
    push si
    push di
    call dd_tile_of
    mov [dd_gc0], cx
    xor si, si
    call dd_tile_of
    mov [dd_pc], cx
    pop di
    pop si
    mov al, [dd_gc0 + 1]
    cmp al, [dd_pc + 1]
    je .samerow
    mov al, [dd_gc0]
    cmp al, [dd_pc]
    je .samecol
    clc
    ret
.samerow:
    ; walk the row between the two columns; any wall breaks the line
    mov cl, [dd_gc0]
    mov ch, [dd_gc0 + 1]
    mov dl, DIR_R
    mov al, [dd_pc]
    cmp al, cl
    je .none
    ja .rwalk
    mov dl, DIR_L
.rwalk:
    mov dh, [dd_pc]
.rstep:
    cmp dl, DIR_R
    jne .rleft
    inc cl
    jmp short .rtest
.rleft:
    dec cl
.rtest:
    cmp cl, dh
    je .seen
    push dx
    call dd_iswall
    pop dx
    jc .none
    jmp short .rstep
.samecol:
    mov cl, [dd_gc0]
    mov ch, [dd_gc0 + 1]
    mov dl, DIR_D
    mov al, [dd_pc + 1]
    cmp al, ch
    je .none
    ja .cwalk
    mov dl, DIR_U
.cwalk:
    mov dh, [dd_pc + 1]
.cstep:
    cmp dl, DIR_D
    jne .cup
    inc ch
    jmp short .ctest
.cup:
    dec ch
.ctest:
    cmp ch, dh
    je .seen
    push dx
    call dd_iswall
    pop dx
    jc .none
    jmp short .cstep
.seen:
    mov al, dl
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; dd_modes - the scatter / chase clock, and the frightened one
; -----------------------------------------------------------------------------
dd_modes:
    cmp word [dd_frcnt], 0
    je .normal
    dec word [dd_frcnt]
    jnz .out                        ; the frightened clock STOPS the mode one,
    call dd_unfright                ; which is what the arcade does
    jmp short .out
.normal:
    cmp word [dd_modetim], 0
    je .out                         ; the last phase runs for ever
    dec word [dd_modetim]
    jnz .out
    inc word [dd_modeidx]
    mov bx, [dd_modeidx]
    cmp bx, DD_NMODE
    jb .load
    mov bx, DD_NMODE - 1
    mov [dd_modeidx], bx
.load:
    shl bx, 1
    mov ax, [dd_modet + bx]
    mov [dd_modetim], ax
    xor byte [dd_modech], 1         ; even phases scatter, odd ones chase
    call dd_gh_reverse              ; a mode change turns them all round
.out:
    ret

; dd_gh_reverse - every roaming ghost about-faces, which is the tell that the
;                 mode changed and is the arcade's own rule
dd_gh_reverse:
    push si
    mov si, 1
.g:
    mov bx, si
    dec bx
    mov al, [dd_gs + bx]
    cmp al, GS_ROAM
    je .turn
    cmp al, GS_FRIGHT
    jne .next
.turn:
    mov bl, [dd_dir + si]
    cmp bl, DIR_NONE
    je .next
    xor bh, bh
    mov al, [dd_rev + bx]
    mov [dd_want + si], al
.next:
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; dd_unfright - the pellet has worn off
dd_unfright:
    push si
    mov word [dd_frcnt], 0
    mov byte [dd_eatn], 0
    mov si, 1
.g:
    mov bx, si
    dec bx
    cmp byte [dd_gs + bx], GS_FRIGHT
    jne .next
    mov byte [dd_gs + bx], GS_ROAM
    cmp byte [dd_gwout + bx], 0
    je .next
    mov byte [dd_gs + bx], GS_OUT   ; it was still walking out when it turned,
.next:                              ; and GS_ROAM there cannot open the door
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; dd_fright_on - a pellet has just gone
dd_fright_on:
    push si
    mov al, [dd_level]
    dec al
    cmp al, DD_NFRIT
    jb .lev
    mov al, DD_NFRIT - 1
.lev:
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [dd_frit + bx]
    mov [dd_frcnt], ax
    or ax, ax
    jz .none                        ; late levels do not frighten at all
    mov byte [dd_eatn], 0
    mov si, 1
.g:
    mov bx, si
    dec bx
    mov byte [dd_gwout + bx], 0
    mov al, [dd_gs + bx]
    cmp al, GS_ROAM
    je .fright
    cmp al, GS_OUT                  ; a ghost still walking out of the pen is
    jne .next                       ; ON the board and turns blue with the
    mov byte [dd_gwout + bx], 1     ; rest: one that did not was a ghost that
.fright:                            ; killed Smiles while every other one ran
    mov byte [dd_gs + bx], GS_FRIGHT
.next:
    inc si
    cmp si, DD_NACT
    jb .g
    call dd_gh_reverse
.none:
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_release_ck - let the penned ghosts out
;
; TWO clocks, because one is not enough: a ghost leaves when the board has lost
; its share of dots, and - the arcade's global timer - the longest-penned one
; leaves anyway once DD_RELT ticks have gone by with nothing eaten. Without the
; second a cautious player is played against one ghost, and with the tally
; reset on every death a player who kept dying never met the third at all.
; -----------------------------------------------------------------------------
dd_release_ck:
    push si
    inc word [dd_relt]
    cmp word [dd_relt], DD_RELT
    jb .bydot
    mov word [dd_relt], 0
    call dd_release_one
.bydot:
    mov si, 1
.g:
    mov bx, si
    dec bx
    cmp byte [dd_gs + bx], GS_HOUSE
    jne .next
    cmp byte [dd_gwait + bx], 0     ; a ghost serving its DD_PENWAIT is on its
    jne .next                       ; own clock and this one does not touch it
    mov al, [dd_ghdot + bx]
    xor ah, ah
    cmp [dd_eaten], ax
    jb .next
    mov byte [dd_gs + bx], GS_OUT
.next:
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; dd_release_one - the longest-penned ghost, out
dd_release_one:
    push si
    mov si, 1
.g:
    mov bx, si
    dec bx
    cmp byte [dd_gs + bx], GS_HOUSE
    jne .next
    cmp byte [dd_gwait + bx], 0     ; ...the longest-penned one that is not
    jne .next                       ; already on a clock
    mov byte [dd_gs + bx], GS_OUT
    pop si
    ret
.next:
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; =============================================================================
; EATING, COLLIDING, SCORING (SPEC.md 93.9)
; =============================================================================

; -----------------------------------------------------------------------------
; dd_eat_here - whatever Smiles is standing on
; -----------------------------------------------------------------------------
dd_eat_here:
    push si
    xor si, si
    call dd_at_tile
    jnc .out                        ; a dot is taken at the tile, not near it
    call dd_tile_of
    call dd_eat_tile                ; AL = what was there; nothing is drawn
    cmp al, TT_DOT
    je .dot
    cmp al, TT_PILL
    je .pill
    jmp short .out
.dot:
    inc word [dd_eaten]
    mov word [dd_relt], 0
    mov ax, DD_PTDOT
    call dd_score_add
    call dd_dot_snd                 ; ...a BITE and not a clink (93.10.1)
    call dd_fruit_ck
    jmp short .out
.pill:
    inc word [dd_eaten]
    mov word [dd_relt], 0
    mov ax, DD_PTPILL
    call dd_score_add
    call dd_fright_on
    mov ax, 220
    call dd_beep
    call dd_fruit_ck
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; dd_collide - Smiles against the four of them
;
; The test is the TILE and not the boxes: two actors crossing at speed can
; never be in the same tile without having been within half a tile of each
; other, and a box overlap would fire on a pass that the player reads as a
; miss.
; -----------------------------------------------------------------------------
dd_collide:
    push si
    xor si, si
    mov ax, [dd_x]                  ; Smiles' box, in 1/16 px
    mov [dd_pcx], ax
    mov ax, [dd_y]
    mov [dd_pcy], ax
    mov si, 1
.g:
    mov bx, si
    dec bx
    mov [dd_gi], bx
    mov al, [dd_gs + bx]
    cmp al, GS_EYES
    je .next                        ; a pair of eyes cannot be run into
    cmp al, GS_HOUSE
    je .next
    mov bx, si
    shl bx, 1
    mov ax, [dd_x + bx]
    sub ax, [dd_pcx]
    jns .dxp
    neg ax
.dxp:
    mov dx, [dd_twS]
    shr dx, 1
    cmp ax, dx
    jae .next
    mov ax, [dd_y + bx]
    sub ax, [dd_pcy]
    jns .dyp
    neg ax
.dyp:
    mov dx, [dd_thS]
    shr dx, 1
    cmp ax, dx
    jae .next
    mov bx, [dd_gi]
    cmp byte [dd_gs + bx], GS_FRIGHT
    je .eat
    cmp byte [dd_demo], 0
    jne .next                       ; the demo's Smiles is not killed: the
    call dd_die                     ; attract loop is a shop window, not a game
    pop si
    ret
.eat:
    mov byte [dd_gs + bx], GS_EYES  ; ...and dd_shown is LEFT ALONE: it has not
                                    ; moved, so its next band is its own box
                                    ; and the eyes land exactly over the ghost
    cmp byte [dd_demo], 0
    je .score
    mov byte [dd_gs + bx], GS_ROAM  ; ...except on the ATTRACT SLICE, where the
                                    ; ghost house is above dd_rmin and reads as
                                    ; WALL: a pair of eyes there can never get
                                    ; home and wanders the strip for ever
.score:
    call dd_ghost_score
    mov word [dd_gpause], DD_EATPAUSE
    mov ax, 1200
    call dd_beep
.next:
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; dd_ghost_score - 200, 400, 800, 1600 as they go down
dd_ghost_score:
    push ax
    push bx
    push cx
    mov cl, [dd_eatn]
    inc byte [dd_eatn]
    mov ax, 200
    or cl, cl
    jz .add
.sh:
    shl ax, 1
    dec cl
    jnz .sh
.add:
    call dd_score_add
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_score_add - AX points, and the extra life the total may have earned
; -----------------------------------------------------------------------------
dd_score_add:
    push ax
    push bx
    push dx
    add [dd_score], ax
    adc word [dd_score+2], 0
    mov byte [dd_hudd], 1
    cmp byte [dd_extra], 0
    jne .out
    mov ax, [dd_score+2]
    or ax, ax
    jnz .give
    mov ax, [dd_score]
    cmp ax, DD_EXTRA
    jb .out
.give:
    mov byte [dd_extra], 1
    inc byte [dd_lives]
    mov ax, 1600
    call dd_beep
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_die / dd_life_lost - Smiles is caught
; -----------------------------------------------------------------------------
dd_die:
    push ax
    push si
    mov byte [dd_state], DDS_DIE
    mov word [dd_tim], DD_DIET
    mov ax, 180
    call dd_beep
    ; THE GHOSTS LEAVE (SPEC.md 93.5.16). Each is wiped by its own band on the
    ; next frame - dd_actor_prep's .none arm is dd_wipe_old - so the collapse
    ; plays on a clean board. It is not decoration: four bodies standing on the
    ; tile Smiles is dissolving on is where the eye goes instead of on him.
    mov si, 1
.hide:
    mov byte [dd_alive + si], 0
    inc si
    cmp si, DD_NACT
    jb .hide
    call dd_die_anim                ; ...and frame 0 lands on the frame of the
    pop si                          ; catch rather than a tick after it
    pop ax
    ret

; -----------------------------------------------------------------------------
; dd_die_anim - the image Smiles wears this tick of the death (SPEC.md 93.5.16)
;
; It writes [dd_img + 0] and [dd_alive + 0] and nothing else, so the band
; machinery does the rest exactly as it does in play: the union of where he was
; and where he is, one gfx_blit1, and the board composed under him. Nothing
; MOVES during the death, so dd_actor_prep skips the ticks on which the table
; repeats and the collapse costs seven blits, not eighteen.
; Preserves every register.
; -----------------------------------------------------------------------------
dd_die_anim:
    push ax
    push bx
    mov ax, DD_DIET
    sub ax, [dd_tim]
    cmp ax, DD_DIET                 ; the table is the bound, and an unsigned
    jb .in                          ; borrow above lands well past it
    mov ax, DD_DIET - 1
.in:
    mov bx, ax
    mov al, [dd_dietab + bx]
    cmp al, DD_DGONE
    je .gone
    mov [dd_img + 0], al
    mov byte [dd_alive + 0], 1
    jmp short .out
.gone:
    mov byte [dd_alive + 0], 0
.out:
    pop bx
    pop ax
    ret

dd_life_lost:
    dec byte [dd_lives]
    jz .over
    js .over
    call dd_actors_home
    mov word [dd_fruit], 0
    mov byte [dd_state], DDS_READY
    mov word [dd_tim], DD_READYT
    ret                             ; ...and NOT a full repaint: the board did
.over:                              ; not change, only where five actors are
    mov byte [dd_lives], 0
    mov byte [dd_state], DDS_OVER
    mov word [dd_tim], DD_OVERT
    mov byte [dd_hudd], 1           ; ...AND NEITHER DOES GAME OVER. This set
    mov byte [dd_boxd], 1           ; dd_full and repainted the whole board -
    ret                             ; every wall, every dot - for a life count
                                    ; that had gone to zero and a panel that
                                    ; covers the middle of it. The line above
                                    ; already says why a lost life does not,
                                    ; and the last life is a lost life

; =============================================================================
; THE FRUIT (SPEC.md 93.9.2)
; =============================================================================

; dd_fruit_ck - two per board, at seventy dots and at a hundred and seventy
dd_fruit_ck:
    push ax
    mov ax, [dd_eaten]
    cmp ax, 70
    je .maybe
    cmp ax, 170
    jne .out
.maybe:
    cmp byte [dd_fruits], 2
    jae .out
    inc byte [dd_fruits]
    mov word [dd_fruit], 1
    mov word [dd_frtim], DD_FRUITT
    mov byte [dd_frdirty], 1
.out:
    pop ax
    ret

; dd_fruit_tick - it waits, and it is taken by standing on it
dd_fruit_tick:
    push si
    cmp word [dd_fruit], 0
    je .out
    dec word [dd_frtim]
    jnz .live
    mov word [dd_fruit], 0          ; nobody came
    mov byte [dd_frdirty], 1
    jmp short .out
.live:
    xor si, si
    call dd_tile_of
    cmp cl, DD_FRUITC
    jne .out
    cmp ch, DD_FRUITR
    jne .out
    mov word [dd_fruit], 0
    mov byte [dd_frdirty], 1
    call dd_fruit_val
    call dd_score_add
    mov ax, 900
    call dd_beep
.out:
    pop si
    ret

; dd_fruit_val - AX = what this level's fruit is worth
dd_fruit_val:
    push bx
    mov al, [dd_level]
    dec al
    shr al, 1                       ; a new fruit every two levels
    cmp al, DD_NFRV
    jb .ok
    mov al, DD_NFRV - 1
.ok:
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [dd_frv + bx]
    pop bx
    ret

; =============================================================================
; ANIMATION - which image each actor wears this frame
; =============================================================================
dd_anims:
    push si
    ; --- Smiles: dir * 3 + phase, and the phase runs 0 1 2 1 so the mouth
    ; opens and closes rather than snapping shut ---------------------------
    xor si, si
    mov al, [dd_dir]
    cmp al, DIR_NONE
    jne .facing
    mov al, [dd_lastd]
    jmp short .phase
.facing:
    mov [dd_lastd], al
.phase:
    mov cl, 3
    mul cl                          ; AX = direction * 3
    mov bl, al
    mov ax, [dd_anim]
    and ax, 3
    cmp ax, 3
    jne .ph
    mov ax, 1
.ph:
    add al, bl
    add al, SP_PAC
    mov [dd_img + 0], al
    mov al, [dd_ink]
    mov [dd_inkof + 0], al
    ; --- the ghosts --------------------------------------------------------
    mov si, 1
.g:
    mov bx, si
    dec bx
    mov [dd_gi], bx
    mov al, [dd_gs + bx]
    cmp al, GS_EYES
    je .geyes
    cmp al, GS_FRIGHT
    je .gfri
    ; body: skirt phase from the shared clock, eyes from the look state
    mov ax, [dd_anim]
    and ax, 4                       ; a slower flap than the mouth
    jz .skirt0
    mov al, 8
    jmp short .skirt
.skirt0:
    xor al, al
.skirt:
    mov bx, [dd_gi]
    mov ah, [dd_gbig + bx]
    shl ah, 1
    shl ah, 1
    add al, ah
    add al, [dd_glook + bx]
    add al, SP_GHOST
    mov [dd_img + si], al
    mov bx, si
    mov al, [dd_ink + bx]
    mov [dd_inkof + si], al
    jmp short .gnext
.gfri:
    mov ax, [dd_anim]
    and ax, 4
    jz .f0
    mov al, 1
    jmp short .f1
.f0:
    xor al, al
.f1:
    add al, SP_FRIGHT
    mov [dd_img + si], al
    mov al, DD_INKFRI
    cmp word [dd_frcnt], 36         ; the last two seconds flash white
    ja .fset
    mov bx, [dd_anim]
    and bx, 2
    jz .fset
    ; THE FLASH IS A PEN ON COLOUR AND AN IMAGE ON ONE PLANE (SPEC.md 93.6.4).
    ; DD_INKFRI and CWHITE are two pens, and dd_pen does not set a pen at all
    ; below 2 bpp - so on Hercules and CGA both halves of the flash drew the
    ; same lit pixels and the warning never appeared. The dithered twin is the
    ; same body with one pixel in two, which is the only "other colour" an
    ; adapter with one plane has.
    cmp byte [dd_bpp], 1
    jbe .fdim
    mov al, CWHITE
    jmp short .fset
.fdim:
    add byte [dd_img + si], SP_NFRIB
.fset:
    mov [dd_inkof + si], al
    jmp short .gnext
.geyes:
    mov bx, [dd_gi]
    mov al, [dd_glook + bx]
    add al, SP_EYES
    mov [dd_img + si], al
    mov byte [dd_inkof + si], DD_INKEYE
.gnext:
    inc si
    cmp si, DD_NACT
    jb .g
    pop si
    ret

; =============================================================================
; SOUND - three notes and a toggle (SPEC.md 93.10)
; =============================================================================
; dd_tone - AX Hz for CX ticks, or nothing at all when the sound is off.
; Never at the cost of a frame: OSAPI_SND_TONE is fire and forget, so nothing
; here waits for anything.
dd_tone:
    cmp byte [dd_snd], 0
    je .out
    push ax
    push cx
    push dx
    mov dl, 0x40
    call OSAPI_SND_TONE
.done:                              ; ...a label so the cost can be bracketed
    pop dx
    pop cx
    pop ax
.out:
    ret

; dd_beep - one note for a couple of ticks: a pill, a ghost, a fruit, a life,
; a death. It also CANCELS a bite in progress (below): every one of those
; outranks the second syllable of a dot.
dd_beep:
    push cx
    mov byte [dd_wakt], 0
    mov cx, 2
    call dd_tone
    pop cx
    ret

; -----------------------------------------------------------------------------
; THE DOT'S WARBLE (SPEC.md 93.10.1)
;
; One note a dot was a CLINK - 660 Hz, four and a half times a second, for a
; whole board of two hundred and forty - and the field's word for it was
; grating. The arcade does not play a note at all: it plays a SWEEP, and the
; sweep alternates direction bite to bite, which is where "waka waka" comes
; from.
;
; A square wave on tick granularity cannot sweep, but it can SLUR. Two tones
; one tick apart is a two-syllable bite, and turning the pair over on the next
; dot gives the up-down-up-down the ear hears as chewing rather than as an
; alarm. Both tones are also well below the old one, which is the other half of
; "less grating": a square wave is all odd harmonics, so 660 Hz put its third
; and fifth at 1,980 and 3,300 - the middle of where an ear is most sensitive -
; and 349 puts them at 1,047 and 1,745. That half is the SPEAKER's: a loaded
; driver publishing DSV_TONE takes the tier (SPEC.md 34.2), so on an OPL2 the
; timbre is the patch's and only the pitch and the warble carry over.
;
; THE INTERVAL IS BEING CHOSEN BY LISTENING and it is NOT SETTLED. Six
; candidates were synthesised at the exact tones and tick lengths this code
; asks for - the speaker is a square wave, so the preview is faithful - and two
; survived: this fourth, and the whole tone D4/E4 (294/330), which is heard as
; one sound wavering rather than as two notes. They are being A/B'd on real
; hardware, on the speaker and on an AdLib, and the answer is two `equ`s.
;
; It is NOT a per-tick sound engine. One byte counts the syllable down and one
; word holds what to play when it lands, so a tick that is not the second
; syllable of a bite costs a compare against zero.
; -----------------------------------------------------------------------------
DD_WAKLO  equ 262                   ; C4 and F4, a fourth. D4/E4 - a whole tone -
DD_WAKHI  equ 349                   ; is the other candidate and is one edit
DD_WAKT   equ 1                     ; ticks a syllable. A bite at full speed is
                                    ; DD_TILET = 4, so the pair is a crisp
                                    ; wa-ka and then a gap, which is the
                                    ; arcade's rhythm and not a drone

; dd_dot_snd - a dot is eaten. Plays the first syllable and arms the second.
dd_dot_snd:
    push ax
    push cx
    xor byte [dd_wakph], 1          ; ...and the PAIR turns over each dot
    mov ax, DD_WAKLO
    mov cx, DD_WAKHI
    cmp byte [dd_wakph], 0
    je .set
    xchg ax, cx
.set:
    mov [dd_wak2], cx
    mov byte [dd_wakt], DD_WAKT
    mov cx, 1                       ; ONE tick, so a bite that is never
    call dd_tone                    ; followed up stops rather than drones
    pop cx
    pop ax
    ret

; dd_wak_tick - the second syllable, one tick on. Called every tick in every
; state, beside dd_pill_tick.
dd_wak_tick:
    cmp byte [dd_wakt], 0
    je .out
    dec byte [dd_wakt]
    jnz .out
    push ax
    push cx
    mov ax, [dd_wak2]
    mov cx, 1
    call dd_tone
    pop cx
    pop ax
.out:
    ret
