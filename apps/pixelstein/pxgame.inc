; =============================================================================
; os8088 - apps/pixelstein/pxgame.inc
;
; THE SESSION (SPEC.md 97.8): the clock, the two readers of the keyboard, the
; player's step with per-axis collision, the level's expansion into the two
; map layouts and its tables, the detail selector and one key handler for
; both worlds. WAVE 3: the doors that slide (97.2.4, 97.8), the keys, the
; pickups, the Use and Fire keys, the hitscan, the player's health and the
; DIE wash, the world's tick (px_sim_tick: the doors, the guards of
; pxact.inc, the weapon). WAVE 4: the seven states and the one the timedemo
; runs in (97.13) - ATTRACT, READY, PLAY, DIE, LEVELDONE, OVER, ENTER - the
; floor's ratios, the sound, the mouse, and the timedemo itself.
; =============================================================================

; -----------------------------------------------------------------------------
; px_steps - how many SIMULATION steps this frame owes, and the wait if none
;            (apps/tank/tkgame.inc's tk_steps, verbatim in shape)
; out: CX = 0..PX_MAXSTEP.  clobbers AX, BX
; -----------------------------------------------------------------------------
px_steps:
    call OSAPI_GET_TICKS
    cmp byte [px_state], PXST_DEMO  ; THE TIMEDEMO IS UNTHROTTLED (97.13): one
    jne .clock                      ; scripted step a frame and no wait, so
    mov [px_last], ax               ; the frames are the machine's own
    mov cx, 1
    ret
.clock:
    mov bx, ax
    sub ax, [px_last]
    jnz .have
    mov al, FSXW_TICK               ; nothing owing: the world cannot move
    call OSAPI_FSX_WAIT             ; yet, so do not burn the machine drawing
    call OSAPI_GET_TICKS            ; a frame identical to the last one
    mov bx, ax
    sub ax, [px_last]
.have:
    mov [px_last], bx
    cmp ax, PX_MAXSTEP
    jbe .ok
    mov ax, PX_MAXSTEP
.ok:
    mov cx, ax
    ret

; -----------------------------------------------------------------------------
; px_input - the bracket's typed keys: int 16h to empty, through
;            px_key_common (a bracket dispatches no events, SPEC.md 53.1)
; clobbers AX
; -----------------------------------------------------------------------------
px_input:
.keys:
    mov ah, 1
    int 0x16
    jz .out
    xor ah, ah
    int 0x16
    call px_key_common
    cmp byte [px_fsxq], 0
    je .keys
    mov byte [px_quit], 1
.out:
    ret

; -----------------------------------------------------------------------------
; px_key_common - one handler, two worlds: AL = ascii, AH = scan (W_ONKEY's
;                 two registers, and int 16h's)
; -----------------------------------------------------------------------------
px_key_common:
    ; --- THE STATES' OWN KEYS FIRST (97.13) ---------------------------------
    mov bl, [px_state]
    cmp bl, PXST_ENTER              ; EVERY KEY IS THE PROMPT'S (85.9's rule):
    jne .nenter                     ; Esc or F would otherwise leave the
    cmp byte [px_cardhold], 0       ; bracket and lose the row - once the
    jne .held                       ; card has stood its hold (below)
    jmp px_hs_key
.held:
    ret
.nenter:
    cmp bl, PXST_DEMO
    jne .ndemo
    cmp ah, KSC_ESC                 ; the timedemo: Esc still leaves the
    je .common                      ; bracket (its exit ends the run), any
    mov byte [px_demoab], 1         ; other key ends the run and shows the
    ret                             ; attract page
.ndemo:
    cmp bl, PXST_ATTRACT
    jne .nattr
    cmp byte [px_codep], 3          ; taking a code: every key is the code's
    ja .attr
    jmp px_code_key
.attr:
    cmp byte [px_cardhold], 0       ; A CARD STANDS PX_CARDHOLD TICKS BEFORE
    jne .common                     ; A KEY MOVES IT ON: the Space that threw
    cmp al, ' '                     ; the switch or ended OVER is still held,
    je .play                        ; and its typematic repeat skipped the
                                    ; next card whole (97.13)
    cmp ah, KSC_ENTER
    je .play
    cmp al, 't'
    je .tdemo
    cmp al, 'T'
    je .tdemo
    cmp al, 'c'
    je .code
    cmp al, 'C'
    jne .common
.code:
    jmp px_code_begin
.play:
    xor al, al                      ; a new game on the first floor, loaded
    jmp px_new_game                 ; between frames
.tdemo:
    mov byte [px_demoreq], 1        ; THE TIMEDEMO IS A BRACKET'S (97.13): it
    mov byte [px_demodone], 0       ; measures the fullscreen promise, so a
    cmp byte [px_inbr], 0           ; window's T takes the machine first and
    jne .tout                       ; the bracket's entry starts the run
    call px_go_fsx
.tout:
    ret
.nattr:
    cmp al, ' '
    je .adv
    cmp ah, KSC_ENTER
    jne .common
.adv:
    cmp byte [px_cardhold], 0       ; (the hold, as above)
    jne .common
    cmp bl, PXST_DONE               ; Space or Enter moves a card on: the
    jne .nd                         ; next floor...
    jmp px_done_next
.nd:
    cmp bl, PXST_OVER               ; ...the table or the attract page...
    jne .nr
    jmp px_over_end
.nr:
    cmp bl, PXST_READY              ; ...PLAY, without waiting
    jne .common
    mov byte [px_stimer], 1
    ret
.common:
    cmp ah, KSC_ESC
    je .esc
    cmp al, 'f'
    je .full
    cmp al, 'F'
    je .full
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp al, 'v'
    je .size
    cmp al, 'V'
    je .size
    cmp al, 9                       ; Tab: the map (97.6, wave 6)
    je .map
    cmp al, '1'                     ; 1, 2, 3: the knife, the pistol, the gun
    jb .notw                        ; (97.8; the first cut defined the scans
    cmp al, '3'                     ; and read none of them - review, wave 3)
    ja .notw
    sub al, '1'
    mov [px_weapon], al
    mov byte [px_dirty], 1          ; the frame changed: the draw rule erases
    ret                             ; the old and draws the new (97.6)
.notw:
    or al, al                       ; an extended key: AL = 0, AH = the scan
    jnz .out                        ; (the arrows reach int 16h this way)
    cmp ah, KSC_LEFT
    je .tapl
    cmp ah, KSC_RIGHT
    jne .out
    mov byte [px_tap], 1            ; a tap the level read may have missed
    ret                             ; (SPEC.md 88.7.5.2's latch)
.tapl:
    mov byte [px_tap], -1
.out:
    ret
.map:
    cmp byte [px_state], PXST_PLAY  ; the map is PLAY's...
    jne .out
    cmp byte [px_mapon], 0
    jne .mapoff
    cmp byte [px_pause], 0          ; ...and not over a pause the player took
    jne .out
    mov byte [px_mapon], 1
    mov byte [px_pause], 1          ; THE WORLD STOPS UNDER IT: one picture,
    call px_map_owe                 ; drawn once (twice on Mode X)
    mov byte [px_hmsg], PXM_MAP     ; ...and the bar says how to leave it
    mov byte [px_hmsgt], 0
    mov byte [px_hudd], 1
    ret
.mapoff:
    jmp px_map_off
.pause:
    cmp byte [px_mapon], 0          ; P over the map takes it down, as Tab
    jne .mapoff                     ; does
    cmp byte [px_state], PXST_PLAY  ; a pause is PLAY's: a card is still
    jne .out                        ; already
    xor byte [px_pause], 1
    mov byte [px_lined], 1
    jmp px_pause_msg                ; ...and the bar says so (97.13)
.size:
    ; THE V KEY cycles the Size row (SPEC.md 97.3): 48, 56, 64, 72, 80 and
    ; round, applied between frames like a menu pick. Saved from the UI task
    ; only - in the bracket the pick is banked and written when the bracket
    ; is left (px_go_fsx)
    mov al, [px_sizeix]
    inc al
    cmp al, PX_NSIZES
    jb .szw
    xor al, al
.szw:
    cmp byte [px_inbr], 0
    jne .sz
    cmp al, PX_NSIZEW               ; windowed: 48, 56, 64 and round - 72
    jb .sz                          ; and 80 are the bracket's, and their
    xor al, al                      ; items say so (97.3)
.sz:
    mov [px_sizeix], al
    mov byte [px_pend], 1
    mov byte [px_cfgdirty], 1
    cmp byte [px_inbr], 0
    jne .szmsg
    call px_set_save
    ret
.szmsg:
    mov byte [px_hmsgw], PXM_SIZE   ; the bracket's bar names the Size once
    ret                             ; px_apply has put it in force (97.13)
.full:
    cmp byte [px_inbr], 0
    jne .leave                      ; F leaves as well as enters (SPEC.md
    call px_go_fsx                  ; 11.2.1); entering is only ever reached
    ret                             ; from W_ONKEY or the menu, the UI task
.esc:
    cmp byte [px_inbr], 0
    jne .leave
    cmp byte [px_state], PXST_ATTRACT   ; WINDOWED, ESC ABANDONS THE GAME to
    je .out                         ; the attract page (97.13) - in a bracket
    jmp px_to_attract               ; it leaves the bracket and the game goes on
.leave:
    mov byte [px_fsxq], 1
    ret

; -----------------------------------------------------------------------------
; px_keys_read - the held keys, latched into what a step spends (SPEC.md
;                97.8, apps/tank's latch-and-spend): OSAPI_KEY_DOWN once a
;                frame when a step is owed
; clobbers AX
; -----------------------------------------------------------------------------
px_keys_read:
    push bx
    mov byte [px_kturn], 0
    mov byte [px_kfwd], 0
    mov byte [px_kstr], 0
    cmp byte [px_inbr], 0
    jne .read
    cmp byte [px_hasfoc], 0
    je .out                         ; somebody else has the keyboard
.read:
    xor bl, bl                      ; BL = the turn a step
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .l1
    sub bl, PX_TURN
.l1:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .l2
    add bl, PX_TURN
.l2:
    mov al, [px_tap]                ; a tap: spent once if the key reads up
    or al, al                       ; now - a tap the hold has since covered
    jz .l3                          ; is the hold's
    mov byte [px_tap], 0
    or bl, bl
    jnz .l3
    mov bl, PX_TURN
    cmp al, 0
    jg .l3
    neg bl
.l3:
    cmp byte [px_mouse], 0          ; THE MOUSE STEERS (97.13; off by default):
    je .nomouse                     ; the turn is the pointer's offset from
    call px_mouse_turn              ; the centre, clamped - there is no
.nomouse:                           ; relative pointer here (97.11)
    mov [px_kturn], bl
    mov bh, PX_SPEED
    mov al, PX_KLSH
    call OSAPI_KEY_DOWN
    jc .run
    mov al, PX_KRSH
    call OSAPI_KEY_DOWN
    jnc .walk
.run:
    mov bh, PX_RUN
.walk:
    xor bl, bl                      ; BL = forward, signed
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jc .fwd
    mov al, PX_KW
    call OSAPI_KEY_DOWN
    jnc .f1
.fwd:
    mov bl, bh
.f1:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jc .back
    mov al, PX_KS
    call OSAPI_KEY_DOWN
    jnc .f2
.back:
    mov bl, bh
    neg bl
.f2:
    mov [px_kfwd], bl
    xor bl, bl                      ; BL = strafe, signed, at the walk
    mov al, PX_KA
    call OSAPI_KEY_DOWN
    jnc .s1
    mov bl, -PX_SPEED
.s1:
    mov al, PX_KD
    call OSAPI_KEY_DOWN
    jnc .s2
    mov bl, PX_SPEED
.s2:
    mov [px_kstr], bl
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; px_kbdrain - eat the steering keys out of the BIOS buffer (apps/dotdel's
;              dd_kbdrain, SPEC.md 93.7.2.1): windowed, a held key fills it
;              and 9.8 then drops the newest arrival - the P or F just typed
; preserves every register
; -----------------------------------------------------------------------------
px_kbdrain:
    push ax
    push es
    mov ax, 0x0040
    mov es, ax
.again:
    mov ax, [es:0x1A]
    cmp ax, [es:0x1C]
    je .out                         ; empty: the common case
    pushf
    cli
    mov ah, 0x01
    int 0x16
    jz .pop
    cmp ah, KSC_LEFT
    je .eat
    cmp ah, KSC_RIGHT
    je .eat
    cmp ah, KSC_UP
    je .eat
    cmp ah, KSC_DOWN
    je .eat
    cmp ah, PX_KA
    je .eat
    cmp ah, PX_KD
    je .eat
    cmp ah, PX_KW
    je .eat
    cmp ah, PX_KS
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

; -----------------------------------------------------------------------------
; px_sincos - [px_hcos], [px_hsin] of the heading, Q14 (pxtab.inc's fold)
; clobbers AX, BX
; -----------------------------------------------------------------------------
px_sincos:
    mov ax, [px_head]
    call px_sin_of
    mov [px_hsin], ax
    mov ax, [px_head]
    add ax, PX_QUARTER
    call px_sin_of
    mov [px_hcos], ax
    ret

; px_sin_of - AX = sin(AX), a 12-bit angle, in Q14.  clobbers BX
px_sin_of:
    and ax, PX_ANG - 1
    mov bx, ax
    and bx, 1023
    shl bx, 1
    test ah, 0x04                   ; bit 10: the second or fourth quarter
    jz .q02
    neg bx
    add bx, 2048                    ; sin[1024 - i]
.q02:
    mov bx, [px_sin + bx]
    test ah, 0x08                   ; bit 11: the second half of the turn
    jz .pos
    neg bx
.pos:
    mov ax, bx
    ret

; -----------------------------------------------------------------------------
; px_step - one simulation step: spend what px_keys_read latched (97.8)
; clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
px_step:
    call px_timers                  ; the bar's message and the grin (97.13)
    mov al, [px_state]
    cmp al, PXST_PLAY
    jne .card
    call px_step_move
    call px_pmark                   ; the player's cell mark, and what it
                                    ; picked up (wave 3)
    call px_sim_tick                ; ...and the world's tick
    jmp px_hud_poll                 ; ...and does the bar owe a look (97.13)
.card:
    cmp al, PXST_DEMO
    jne .notdemo
    jmp px_demo_step                ; the timedemo's scripted step
.notdemo:
    call px_sim_tick                ; the DIE wash's clock, READY's, OVER's
    jmp px_hud_poll

; px_step_move - the player's own step
px_step_move:
    mov al, [px_kturn]
    or al, al
    jz .noturn
    cbw
    add [px_head], ax
    and word [px_head], PX_ANG - 1
    call px_sincos
    mov byte [px_dirty], 1
.noturn:
    mov al, [px_kfwd]
    or al, [px_kstr]
    jz .out
    ; the step vector: fwd * (cos, sin) + str * (-sin, cos), Q8.8
    mov al, [px_kfwd]
    cbw
    mov bx, [px_hcos]
    PX_MUL14
    mov si, ax                      ; dx so far
    mov al, [px_kfwd]
    cbw
    mov bx, [px_hsin]
    PX_MUL14
    mov cx, ax                      ; dy so far
    mov al, [px_kstr]
    cbw
    mov bx, [px_hsin]
    PX_MUL14
    sub si, ax                      ; - str * sin
    mov al, [px_kstr]
    cbw
    mov bx, [px_hcos]
    PX_MUL14
    add cx, ax                      ; + str * cos
    ; x first, against the four corner cells at the OLD y (SPEC.md 97.8)
    mov ax, [px_px]
    add ax, si
    mov dx, [px_py]
    push cx                         ; px_solid4 clobbers CX - the y step -
    push ax                         ; and the first cut walked south at 0
    call px_solid4                  ; and north at a tile a tick for it
    pop ax
    pop cx                          ; (pop moves no flag: ZF is the test's)
    jnz .xblk
    cmp ax, [px_px]
    je .xblk
    mov [px_px], ax
    mov byte [px_dirty], 1
.xblk:
    mov ax, [px_px]
    mov dx, [px_py]
    add dx, cx
    push dx
    call px_solid4
    pop dx
    jnz .out
    cmp dx, [px_py]
    je .out
    mov [px_py], dx
    mov byte [px_dirty], 1
.out:
    ret

; px_cell - AX = x, DX = y (Q8.8) -> BX = the plain map's cell index.
;           Clobbers AX. (tests/pxsbench's pb_sim_cell)
px_cell:
    mov bx, dx
    xor bl, bl
    shr bx, 1
    shr bx, 1                       ; y tile << 6
    mov al, ah
    xor ah, ah
    or bx, ax
    ret

; px_solid2 - the cells (x, y - r) and (x, y + r): ZF clear if either blocks.
;             Clobbers AX, BX, CX; DX preserved
px_solid2:
    push dx
    push ax
    sub dx, PX_RADIUS
    call px_cell
    call px_pblk
    pop ax
    pop dx
    jnz .out
    push dx
    push ax
    add dx, PX_RADIUS
    call px_cell
    call px_pblk
    pop ax
    pop dx
.out:
    ret

; px_pblk - does cell BX block the PLAYER? ZF clear if so: a wall, a blocking
;           static, an actor, or a door that has not slid past
;           PX_DOORPASS (97.8) - or, past that, a live actor in the doorway.
;           A DOOR cell is asked FIRST: its high nibble is a material, never
;           a mark (a secret door in wall material 1 is 0x1A, and the first
;           cut's mark test read its PXC_BLOCK and blocked the player for
;           good, open or shut; review, wave 3). Clobbers AX, BX, CX
px_pblk:
    mov cl, [px_map + bx]
    test cl, PXC_DOOR
    jnz .door
    test cl, PXC_SOLID | PXC_BLOCK | PXC_ACTOR
    ret                             ; (the test's ZF)
.door:
    mov ax, bx
    push ax                         ; the cell
    call px_door_of
    jc .shutp
    mov bx, ax
    cmp word [bx + PXD_POS], PX_DOORPASS
    jb .shutp
    pop ax
    push di
    mov di, 0xFFFF                  ; the player asking: a guard in the
    call px_body_in                 ; doorway blocks, its own cell does not
    pop di
    ret
.shutp:
    pop ax
    or cl, cl                       ; (the DOOR bit: ZF clear)
    ret

; px_solid4 - AX = x, DX = y: the four corners of the radius. ZF clear if
;             any blocks. Clobbers AX, BX, CX
px_solid4:
    push ax
    sub ax, PX_RADIUS
    call px_solid2
    pop ax
    jnz .out
    add ax, PX_RADIUS
    call px_solid2
.out:
    ret

; -----------------------------------------------------------------------------
; px_level_load - level AL of the stream into the two map layouts, the eye,
;                 the doors, the actors and the statics (SPEC.md 97.7, 97.2)
; in:  AL = the level, [px_hand + PXH_LEV] = the stream's segment.
; out: CF=1 refused
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_level_load:
    cmp al, PXL_NLEV
    jae .bad
    mov [px_floor], al
    mov ax, [px_hand + PXH_LEV]
    or ax, ax
    jz .bad
    mov es, ax
    ; THE DIRECTORY (pxlev.inc): the record's offset and length, both held
    ; to the part's own length (PXH_LEVLEN) before a byte of it is read
    mov bl, [px_floor]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov si, [px_levdir + bx]        ; the record
    mov dx, [px_levdir + bx + 2]    ; ...and its length
    mov ax, si
    add ax, dx
    jc .bad
    cmp ax, [px_hand + PXH_LEVLEN]
    ja .bad
    cmp dx, PXL_HDR
    jb .bad
    cmp word [es:si], 'PX'          ; 'PXL', 1
    jne .bad
    cmp word [es:si + 2], 'L' | (1 << 8)
    jne .bad
    mov ax, [es:si + PXL_SPAWNX]
    mov [px_px], ax
    mov ax, [es:si + PXL_SPAWNY]
    mov [px_py], ax                 ; ...both checked against the map below
    mov ax, [es:si + PXL_SPAWNA]
    and ax, PX_ANG - 1
    mov [px_head], ax
    mov al, [es:si + PXL_NDOORS]    ; the three counts, each under its table
    cmp al, PX_MAXDOORS
    ja .bad
    mov [px_ndoors], al
    mov al, [es:si + PXL_NACTORS]
    cmp al, PX_MAXACT
    ja .bad
    mov [px_nact], al
    mov al, [es:si + PXL_NSTATIC]
    cmp al, PX_MAXSTAT
    ja .bad
    mov [px_nstat], al
    ; the run-length map, row-major, into px_map. EVERY BYTE READ OFF THE
    ; FLOPPY IS HOSTILE (CLAUDE.md; 97.7): this is the one place a file byte
    ; becomes a length, so a zero run, a run past the map's end and an odd
    ; pair length each refuse the stream BEFORE a write - the first cut
    ; checked the total after the writes, and a count of 0 had by then
    ; stored 65,536 bytes from px_map through the rest of the carve.
    ; THE SOURCE IS BOUNDED TOO: PXL_MAPLEN is a file word, and the record's
    ; length (the directory's, held to the part's above) is what bounds
    ; every length READ OUT OF the stream - the pairs, then the three
    ; tables behind them
    mov ax, dx
    sub ax, PXL_HDR                 ; AX = the bytes the record can hold
    mov dx, [es:si + PXL_MAPLEN]    ; bytes of pairs...
    cmp dx, ax
    ja .bad                         ; ...not more than the record holds
    sub ax, dx                      ; ...and past them the three tables
    push ax
    mov al, [px_ndoors]
    mov ah, PXL_DOORSZ
    mul ah
    mov cx, ax
    mov al, [px_nact]
    mov ah, PXL_ACTSZ
    mul ah
    add cx, ax
    mov al, [px_nstat]
    mov ah, PXL_STATSZ
    mul ah
    add cx, ax
    pop ax
    cmp cx, ax
    ja .bad
    add si, PXL_HDR
    mov di, px_map
.rle:
    cmp dx, 2
    jb .expanded                    ; 0: done; 1: an odd length, refused below
    mov cl, [es:si]                 ; the count...
    mov al, [es:si + 1]             ; ...and the cell
    inc si
    inc si
    dec dx
    dec dx
    xor ch, ch
    or cl, cl
    jz .bad                         ; a zero run is not a run
    mov bx, px_map + 4096
    sub bx, di                      ; BX = the cells left
    cmp bx, cx
    jb .badj                        ; ...and this run would overrun them
.fill:
    mov [di], al
    inc di
    loop .fill
    jmp .rle
.expanded:
    or dx, dx
    jnz .bad                        ; an odd pair length
    cmp di, px_map + 4096
    jne .bad                        ; a stream that is not a whole map
    ; THE PERIMETER IS SOLID AND HOLDS NO DOOR, AND THE SPAWN IS INSIDE IT
    ; (97.7): the walkers of 97.2 have no crossing counter - they stop on a
    ; SOLID|DOOR bit and nothing else - so one border cell with its bit
    ; clear (a rotted floppy, an edited part 1) walks them out of px_mapT
    ; and px_map for ever, marking spotvis at wrapping offsets through the
    ; column arrays, px_hand and the code. tools/pxslevel.py refuses the
    ; same level on the host; this is the machine refusing the FILE. 256
    ; byte tests once at load, against a walk that cannot be bounded
    ; cheaply inside the body
    push si
    mov si, px_map                  ; the top row
    mov cx, 64
    call px_lev_wall
    jc .badpop
    mov si, px_map + 63 * 64        ; the bottom row
    mov cx, 64
    call px_lev_wall
    jc .badpop
    mov si, px_map + 64             ; the two side columns, rows 1..62
    mov cx, 62
.sides:
    mov al, [si]
    and al, PXC_SOLID | PXC_DOOR
    cmp al, PXC_SOLID
    jne .badpop
    mov al, [si + 63]
    and al, PXC_SOLID | PXC_DOOR
    cmp al, PXC_SOLID
    jne .badpop
    add si, 64
    loop .sides
    pop si
    mov ax, [px_px]                 ; the spawn: tiles 1..62 on both axes
    mov dx, [px_py]                 ; (a tile past 63 would fold px_cell's
    cmp ah, 1                       ; `or` into the y field, and one under
    jb .bad                         ; the radius would index px_map - 64),
    cmp ah, 62                      ; and on an open cell
    ja .bad
    cmp dh, 1
    jb .bad
    cmp dh, 62
    ja .bad
    call px_cell
    test byte [px_map + bx], PXC_SOLID | PXC_DOOR
    jnz .bad
    mov [px_pcell], bx
    ; ...and transposed, mapT[x*64 + y] = map[y*64 + x] (97.2) - BEFORE the
    ; marks below, which are the plain map's alone
    push si
    cld
    mov si, px_map
    xor bx, bx                      ; BX = y
.ty:
    mov di, px_mapT
    add di, bx                      ; mapT + y, then + x*64 a cell
    mov cx, 64
.tx:
    lodsb
    mov [di], al
    add di, 64
    loop .tx
    inc bx
    cmp bx, 64
    jb .ty
    pop si
    ; --- THE DOORS (97.7): cell, flags, lock - the cell a DOOR cell of the
    ;     map, in ascending order (px_door_of's row table wants it), the
    ;     lock 0..2; the slab shut
    mov di, px_doors
    mov cl, [px_ndoors]
    xor ch, ch
    mov dx, 0xFFFF                  ; the last cell, for the order
    jcxz .doors_done_j
    jmp short .door
.doors_done_j:
    jmp .doors_done
.door:
    mov ax, [es:si]
    cmp ax, 4096
    jae .bad
    mov bx, ax
    test byte [px_map + bx], PXC_DOOR
    jz .bad
    cmp dx, 0xFFFF
    je .d1
    cmp ax, dx
    jbe .bad                        ; not ascending
.d1:
    mov dx, ax
    mov [di + PXD_CELL], ax
    mov al, [px_map + bx]
    and al, 15
    mov [di + PXD_FLAGS], al        ; the map's own flags, not the file's
    mov al, [es:si + 3]
    cmp al, 2
    ja .bad
    mov [di + PXD_LOCK], al
    mov word [di + PXD_POS], 0
    mov byte [di + PXD_STATE], PXDS_SHUT
    mov byte [di + PXD_TIMER], 0
    add si, PXL_DOORSZ
    add di, PXD_SIZE
    loop .door
.doors_done:
    ; the row table: px_drow[y] = the first door in row y or after - the
    ; walk past the doors of the rows UNDER y comes first and the store
    ; after it (the first cut stored, then walked, so every entry lagged a
    ; row and px_door_of found no door from its own cell: Space in front of
    ; a door did nothing, and the cast, the movers and the line of sight
    ; read every door as shut)
    xor bx, bx                      ; BX = y
    xor cx, cx                      ; CL = the door index
.dwalk:
    cmp cl, [px_ndoors]
    jae .drow
    mov al, cl
    mov ah, PXD_SIZE
    mul ah
    mov di, ax
    mov ax, [px_doors + di + PXD_CELL]
    push cx
    mov cl, 6
    shr ax, cl
    pop cx
    cmp ax, bx                      ; its row under y? then it is behind
    jae .drow
    inc cl
    jmp short .dwalk
.drow:
    mov [px_drow + bx], cl
    inc bx
    cmp bx, 65
    jb .dwalk
    ; --- THE ACTORS: cell, kind, facing -----------------------------------------
    mov di, px_act
    mov cl, [px_nact]
    xor ch, ch
    jcxz .act_done_j
    jmp short .actor
.act_done_j:
    jmp .act_done
.actor:
    mov ax, [es:si]
    cmp ax, 4096
    jae .bad
    mov bx, ax
    test byte [px_map + bx], PXC_SOLID | PXC_DOOR
    jnz .bad
    mov [di + PXAC_CELL], ax
    or byte [px_map + bx], PXC_ACTOR
    mov ah, al                      ; the tile's centre
    and ah, 63
    mov al, 128
    mov [di + PXAC_X], ax
    mov ax, bx
    push cx
    mov cl, 6
    shr ax, cl
    pop cx
    mov ah, al
    mov al, 128
    mov [di + PXAC_Y], ax
    mov al, [es:si + 2]
    mov ah, al
    and al, 0x7F
    cmp al, 1
    ja .bad
    mov [di + PXAC_KIND], al
    mov byte [di + PXAC_STATE], PXAS_STAND
    mov byte [di + PXAC_FLAGS], 0
    test ah, PXL_PATROL
    jz .stands
    mov byte [di + PXAC_STATE], PXAS_PATROL
    mov byte [di + PXAC_FLAGS], PXAF_PATROL
.stands:
    mov ax, [es:si + 3]
    and ax, PX_ANG - 1
    mov [di + PXAC_ANG], ax
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    mov [di + PXAC_DIR], al
    mov byte [di + PXAC_TIMER], 0
    mov byte [di + PXAC_HP], PX_GUARDHP
    cmp byte [di + PXAC_KIND], PXK_DOG
    jne .guardhp
    mov byte [di + PXAC_HP], PX_DOGHP   ; the dog: one hit (wave 6)
.guardhp:
    mov byte [di + PXAC_FRAME], 0
    add si, PXL_ACTSZ
    add di, PXAC_SIZE
    dec cx
    jz .act_done
    jmp .actor
.act_done:
    ; --- THE STATICS: cell, kind, blocking ----------------------------------------
    mov di, px_stat
    mov cl, [px_nstat]
    xor ch, ch
    jcxz .stat_done_j
    jmp short .static
.stat_done_j:
    jmp .stat_done
.static:
    mov ax, [es:si]
    cmp ax, 4096
    jae .bad
    mov bx, ax
    test byte [px_map + bx], PXC_SOLID | PXC_DOOR
    jnz .bad
    mov [di + PXT_CELL], ax
    mov al, [es:si + 2]
    cmp al, PXK_DECO0 + 6
    jae .bad
    mov [di + PXT_KIND], al
    mov al, [es:si + 3]
    and al, 1
    mov [di + PXT_FLAGS], al
    jz .free
    or byte [px_map + bx], PXC_BLOCK
.free:
    PX_TIDX                         ; its cell in the transposed layout, once
    mov bx, di                      ; (the gather's probe reads it; 97.6)
    sub bx, px_stat
    shr bx, 1
    mov [px_statT + bx], ax
    add si, PXL_STATSZ
    add di, PXT_SIZE
    loop .static
.stat_done:
    mov bx, [px_pcell]              ; the player's own mark
    or byte [px_map + bx], PXC_PLAYER
    mov byte [px_aim], 0xFF
    call px_sincos
    clc
    ret
.badpop:
    pop si
.badj:
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; px_level_next - the floor [px_levnext] between frames (px_frame_begin):
;                 the switch's next floor, or the DIE restart of this one
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_level_next:
    mov al, [px_levnext]
    mov byte [px_levnext], 0xFF
    mov ah, [px_floor]              ; the floor in force, for a refusal
    push ax
    call px_level_load
    pop ax
    jnc .loaded
    ; REFUSED (a rotten floppy: every floor in the stream passed the host's
    ; rules). px_level_load stores the counts and expands the map BEFORE it
    ; can refuse a record or the perimeter, so the floor that was in force
    ; is loaded AGAIN over the half-written one - it loaded once, the
    ; stream in memory is what it was, so it loads whole - and the restart
    ; below starts it afresh. The first cut's comment said a refusal
    ; "leaves the last one", which it did not (review, wave 3)
    mov al, ah
    call px_level_load
    jc .out                         ; (cannot: see above)
.loaded:
    call px_map_off                 ; a floor loads with no map up (review,
    call px_spot_clear              ; wave 6: a stale map across a restart)
    mov byte [px_keys], 0
    mov byte [px_fade], 0
    mov byte [px_wframe], 0
    mov byte [px_wtimer], 0
    cmp byte [px_newgame], 0        ; A NEW GAME (97.13): the score, the
    je .notnew                      ; lives and the player from scratch
    mov byte [px_newgame], 0
    mov byte [px_restart], 0
    mov word [px_score], 0
    mov byte [px_lives], PX_LIVES0
    mov byte [px_victory], 0
    jmp short .player
.notnew:
    cmp byte [px_restart], 0        ; the switch's next floor keeps what the
    je .fresh                       ; player carried; a death does not
    mov byte [px_restart], 0
    cmp byte [px_lives], 0          ; (a death with no life left is OVER's,
    je .player                      ; never here - px_sim_tick; guarded)
    dec byte [px_lives]
.player:
    mov byte [px_health], PX_HEALTH0
    mov byte [px_ammo], PX_AMMO0
    mov byte [px_weapon], PXW_PISTOL
.fresh:
    call px_floor_stats             ; the ratios' totals and the clock
    mov byte [px_cardhold], PX_CARDHOLD
    mov byte [px_state], PXST_READY ; "FLOOR n" before every floor (97.13)
    mov byte [px_stimer], PX_READYT
    mov byte [px_cardl], 0
    call px_card_owe
    call px_force_all
    mov byte [px_lined], 1
.out:
    ret

; -----------------------------------------------------------------------------
; px_door_of - AX = a cell -> AX = its door's record, or CF = 1 (97.7's row
;              table: the doors of the cell's row, a few compares)
; preserves every other register
; -----------------------------------------------------------------------------
px_door_of:
    push bx
    push cx
    push si
    mov bx, ax
    mov cl, 6
    shr bx, cl                      ; the row
    mov cl, [px_drow + bx]          ; its doors: [cl, ch)
    mov ch, [px_drow + bx + 1]
    cmp cl, ch
    jae .none
    push ax
    mov al, cl
    mov ah, PXD_SIZE
    mul ah
    mov si, ax
    add si, px_doors
    pop ax
.d:
    cmp [si + PXD_CELL], ax
    je .found
    add si, PXD_SIZE
    inc cl
    cmp cl, ch
    jb .d
.none:
    pop si
    pop cx
    pop bx
    stc
    ret
.found:
    mov ax, si
    pop si
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; px_pmark - the player's cell mark follows the eye (pxact.inc's movers test
;            it), and a cell entered is searched for a pickup
; clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
px_pmark:
    mov ax, [px_px]
    mov dx, [px_py]
    call px_cell
    cmp bx, [px_pcell]
    je .out
    mov si, [px_pcell]
    test byte [px_map + si], PXC_DOOR   ; NEVER ON A DOOR CELL (97.8): its
    jnz .left                       ; high nibble is the material, and the
    and byte [px_map + si], ~PXC_PLAYER ; first cut's `and` turned a steel
.left:                              ; door walked through into material 8
    mov [px_pcell], bx              ; for good (review, wave 3)
    test byte [px_map + bx], PXC_DOOR
    jnz .marked
    or byte [px_map + bx], PXC_PLAYER
.marked:
    ; a pickup here? (97.8): the statics with a pickup kind
    mov cl, [px_nstat]
    or cl, cl
    jz .out
    mov si, px_stat
.st:
    cmp byte [si + PXT_KIND], PXK_DECO0
    jae .stn
    cmp [si + PXT_CELL], bx
    jne .stn
    call px_pickup
.stn:
    add si, PXT_SIZE
    dec cl
    jnz .st
.out:
    ret

; px_pickup - the static at SI, a pickup on the player's cell, is taken
;             (the 1992 engine's table): ammo 8 to 99, a medkit 25 and food
;             10 to 100, the two keys, treasure 100 and the chalice 500,
;             the extra life a life, full health and 25 rounds. Full
;             health or ammo leaves the one that would give it lying
; clobbers AX
px_pickup:
    mov al, [si + PXT_KIND]
    cmp al, PXK_AMMO
    je .ammo
    cmp al, PXK_MEDKIT
    je .med
    cmp al, PXK_FOOD
    je .food
    cmp al, PXK_GOLDKEY
    je .gold
    cmp al, PXK_SILVKEY
    je .silver
    cmp al, PXK_TREAS
    je .treas
    cmp al, PXK_CHALICE
    je .chal
    ; the extra life
    inc byte [px_lives]
    mov byte [px_health], PX_HEALTH0
    mov al, 25
    call px_give_ammo
    jmp short .grin
.ammo:
    cmp byte [px_ammo], PX_AMMOMAX
    jae .out
    mov al, 8
    call px_give_ammo
    jmp short .taken
.med:
    cmp byte [px_health], PX_HEALTH0
    jae .out
    mov al, 25
    call px_heal
    jmp short .taken
.food:
    cmp byte [px_health], PX_HEALTH0
    jae .out
    mov al, 10
    call px_heal
    jmp short .taken
.gold:
    or byte [px_keys], 1
    jmp short .taken
.silver:
    or byte [px_keys], 2
    jmp short .taken
.treas:
    add word [px_score], 100
    jmp short .loot
.chal:
    add word [px_score], 500
.loot:
    inc byte [px_ctreas]            ; the LEVELDONE card's loot ratio
.grin:
    mov byte [px_grin], PX_GRINT    ; ...and the face grins (97.13)
.taken:
    mov byte [si + PXT_KIND], 0xFF
    mov byte [px_dirty], 1
    mov al, PXSFX_PICK
    jmp px_sfx
.out:
    ret

px_give_ammo:
    add al, [px_ammo]
    cmp al, PX_AMMOMAX
    jbe .ok
    mov al, PX_AMMOMAX
.ok:
    mov [px_ammo], al
    ret

px_heal:
    add al, [px_health]
    cmp al, PX_HEALTH0
    jbe .ok
    mov al, PX_HEALTH0
.ok:
    mov [px_health], al
    ret

; -----------------------------------------------------------------------------
; px_sim_tick - the world, once a simulation step after the player's (97.8):
;               the DIE wash's clock; then, unless the gates froze it, the
;               doors, the guards, the Use key, the weapon
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_sim_tick:
    inc word [px_dtick]
    mov al, [px_state]
    cmp al, PXST_PLAY
    je .play
    cmp al, PXST_DYING
    je .dying
    cmp al, PXST_READY
    je .ready
    cmp al, PXST_OVER
    jne .out
    dec byte [px_stimer]            ; OVER stands PX_OVERT ticks, or until a
    jnz .out                        ; key (px_key_common)
    jmp px_over_end
.ready:
    dec byte [px_stimer]            ; READY stands PX_READYT ticks, or until
    jnz .out                        ; Space
    jmp px_play_begin
.dying:
    dec byte [px_fade]
    jnz .out
    cmp byte [px_lives], 0          ; the wash is over: no life left is the
    jne .again                      ; end of the game (97.13)...
    jmp px_game_over
.again:
    mov al, [px_floor]              ; ...else this floor again, a life fewer
    mov [px_levnext], al
    mov byte [px_restart], 1
.out:
    ret
.play:
    cmp byte [px_simoff], 0
    jne .out
    call px_doors_tick
    call px_act_tick
    call px_keys_tick
    ret

; -----------------------------------------------------------------------------
; px_doors_tick - every door a tick (97.8): opening slides PX_DOORSTEP a tick
;                 to 256, open holds PX_DOORHOLD ticks and closes unless a
;                 body is in it, closing slides back and reopens if a body
;                 steps in
; clobbers AX, BX, CX, SI
; -----------------------------------------------------------------------------
px_doors_tick:
    mov cl, [px_ndoors]
    or cl, cl
    jz .out
    mov si, px_doors
.d:
    mov al, [si + PXD_STATE]
    cmp al, PXDS_OPENING
    je .opening
    cmp al, PXDS_OPEN
    je .open
    cmp al, PXDS_CLOSING
    je .closing
.next:
    add si, PXD_SIZE
    dec cl
    jnz .d
.out:
    ret
.opening:
    mov ax, [si + PXD_POS]
    add ax, PX_DOORSTEP
    cmp ax, 256
    jb .slid
    mov ax, 256
    mov byte [si + PXD_STATE], PXDS_OPEN
    mov byte [si + PXD_TIMER], 0
.slid:
    mov [si + PXD_POS], ax
    call px_door_dirty
    jmp short .next
.open:
    inc byte [si + PXD_TIMER]
    cmp byte [si + PXD_TIMER], PX_DOORHOLD
    jb .next
    mov byte [si + PXD_TIMER], 0    ; (a body in it: asked again later)
    call px_door_close
    jmp short .next
.closing:
    call px_door_body
    jnc .slide
    mov byte [si + PXD_STATE], PXDS_OPENING   ; something stepped in
    jmp short .next
.slide:
    mov ax, [si + PXD_POS]
    sub ax, PX_DOORSTEP
    ja .slid2
    xor ax, ax
    mov byte [si + PXD_STATE], PXDS_SHUT
.slid2:
    mov [si + PXD_POS], ax
    call px_door_dirty
    jmp short .next

; px_door_dirty - the door at SI moved: a frame is owed only if a ray of the
;                 last cast crossed its cell (px_spot_seen) - a ray that
;                 reaches a door enters its cell to test the slab, so a
;                 door no ray reached changed no pixel (review r1: every
;                 sliding door set px_dirty, and a still eye then paid a
;                 cast and a gather a tick for a door behind a wall).
;                 preserves every register
px_door_dirty:
    push bx
    mov bx, [si + PXD_CELL]
    call px_spot_seen
    pop bx
    jnc .out
    mov byte [px_dirty], 1
.out:
    ret

; px_door_close - the door at SI starts closing, unless a body is in it
;                 (px_door_body). clobbers AX, BX
px_door_close:
    call px_door_body
    jc .out
    mov byte [si + PXD_STATE], PXDS_CLOSING
.out:
    ret

; px_door_body - is a body in the door at SI? CF = 1 yes: the player's cell,
;                or any actor's - alive or dead - whose centre is in it
;                (97.8: a body keeps a door open). clobbers AX, BX
px_door_body:
    push cx
    push si
    mov ax, [si + PXD_CELL]
    cmp ax, [px_pcell]
    je .yes
    mov cl, [px_nact]
    or cl, cl
    jz .no
    mov si, px_act
.a:
    cmp byte [si + PXAC_STATE], PXAS_NONE
    je .an
    cmp [si + PXAC_CELL], ax
    je .yes
.an:
    add si, PXAC_SIZE
    dec cl
    jnz .a
.no:
    pop si
    pop cx
    clc
    ret
.yes:
    pop si
    pop cx
    stc
    ret

; px_door_use - the player operates the door at BX (97.8): a lock it has
;               no key for refuses; shut or closing opens; open or opening
;               closes (unless a body is in it). clobbers AX, SI
px_door_use:
    mov al, [bx + PXD_LOCK]
    or al, al
    jz .free
    mov ah, [px_keys]               ; lock 1 wants bit 0, lock 2 bit 1
    cmp al, 1
    jne .k2
    test ah, 1
    jz .out
    jmp short .free
.k2:
    test ah, 2
    jz .out
.free:
    mov al, [bx + PXD_STATE]
    cmp al, PXDS_SHUT
    je .open
    cmp al, PXDS_CLOSING
    je .open
    mov si, bx
    call px_door_close
    ret
.open:
    mov byte [bx + PXD_STATE], PXDS_OPENING
    mov al, PXSFX_DOOR
    call px_sfx
    mov al, [bx + PXD_FLAGS]        ; A SECRET DOOR'S FIRST OPENING counts
    and al, PXC_SPECIAL | PXD_FOUND ; toward the card's secrets ratio
    cmp al, PXC_SPECIAL             ; (97.13): SPECIAL on a door cell, not
    jne .out                        ; yet found
    or byte [bx + PXD_FLAGS], PXD_FOUND
    inc byte [px_csec]
.out:
    ret

; -----------------------------------------------------------------------------
; px_keys_tick - the Use key and the Fire key, once a step (97.8): Space
;                operates the door or the switch in the cell ahead, on its
;                press; Ctrl fires - one shot a press for the knife and the
;                pistol, held for the gun - and the weapon's frames run
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_keys_tick:
    cmp byte [px_inbr], 0
    jne .keys
    cmp byte [px_hasfoc], 0
    je .anim                        ; somebody else has the keyboard
.keys:
    mov al, KSC_SPACE
    call OSAPI_KEY_DOWN
    jnc .nouse
    cmp byte [px_usek], 0
    jne .used
    mov byte [px_usek], 1
    call px_use
    jmp short .used
.nouse:
    mov byte [px_usek], 0
.used:
    mov al, PX_KCTRL
    call OSAPI_KEY_DOWN
    jnc .nofire
    cmp byte [px_wframe], 0
    jne .fired                      ; still in the last shot
    cmp byte [px_firek], 0
    je .shoot                       ; a fresh press: always
    mov al, [px_weapon]             ; held: the gun alone repeats
    cmp al, PXW_MGUN
    jne .fired
    cmp byte [px_ammo], 0
    je .fired
.shoot:
    call px_fire
.fired:
    mov byte [px_firek], 1
    jmp short .anim
.nofire:
    mov byte [px_firek], 0
.anim:
    cmp byte [px_wtimer], 0
    je .out
    dec byte [px_wtimer]
    jnz .out
    mov al, [px_wframe]
    cmp al, 1
    jne .rest
    mov byte [px_wframe], 2         ; the recoil
    mov byte [px_wtimer], 2
    mov byte [px_dirty], 1
    ret
.rest:
    mov byte [px_wframe], 0         ; ready again
    mov byte [px_dirty], 1
.out:
    ret

; px_fire - one shot with the weapon in hand (the knife when the ammo is
;           out): the frames start, a round is spent, and the hitscan lands
;           on the actor under the crosshair (97.6's px_aim), if the line
;           to it is clear, by the 1992 engine's table
px_fire:
    mov byte [px_wframe], 1
    mov byte [px_wtimer], 3
    mov byte [px_dirty], 1
    mov al, [px_weapon]
    cmp byte [px_ammo], 0
    jne .have
    xor al, al                      ; no rounds: the knife
.have:
    or al, al
    jz .knife
    dec byte [px_ammo]              ; (the bar's two ammo cells follow:
    push ax                         ; px_hud_poll, 97.13)
    mov al, PXSFX_SHOT
    call px_sfx
    pop ax
    cmp al, PXW_MGUN
    jne .aimed
    mov byte [px_wtimer], 2
.aimed:
    call px_target
    jc .out
    call px_act_deltas              ; the line from it to the eye
    call px_act_los
    jnc .out
    mov al, [px_adist]
    call OSAPI_RAND
    mov bl, [px_adist]
    cmp bl, 2
    jb .near
    cmp bl, 4
    jb .mid
    push ax
    mov ah, 12
    xor ah, ah
    mov cl, 12                      ; (rnd / 12) < dist: missed
    div cl
    cmp al, bl
    pop ax
    jb .out
.mid:
    xor ah, ah
    mov cl, 6
    div cl                          ; rnd / 6
    jmp short .hit
.near:
    shr al, 1                       ; rnd / 4
    shr al, 1
.hit:
    call px_act_hurt
    ret
.knife:
    mov al, PXSFX_KNIFE
    call px_sfx
    call px_target
    jc .out
    cmp word [px_aimh], PX_HEIGHTK / PX_MELEE   ; within 1.5 tiles: at
    jb .out                         ; least this tall
    call OSAPI_RAND
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1                       ; rnd >> 4
    call px_act_hurt
.out:
    ret

; px_target - SI = the actor under the crosshair, alive, or CF = 1
px_target:
    mov al, [px_aim]
    cmp al, 0xFF
    je .none
    cmp al, [px_nact]
    jae .none
    mov ah, PXAC_SIZE
    mul ah
    mov si, ax
    add si, px_act
    cmp byte [si + PXAC_STATE], PXAS_DIE
    jae .none
    cmp byte [si + PXAC_STATE], PXAS_NONE
    je .none
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; px_use - the cell the player faces (the heading's quarter): a door is
;          operated, the elevator switch thrown - its face becomes the used
;          one (material 13 in both layouts) and the next floor loads
;          between frames (97.7, 97.8)
; clobbers AX, BX, CX, SI
; -----------------------------------------------------------------------------
px_use:
    mov ax, [px_head]
    add ax, PX_ANG / 8              ; the quarter: E from -512 to 512...
    and ax, PX_ANG - 1
    mov cl, 10
    shr ax, cl                      ; 0 E, 1 S, 2 W, 3 N
    mov bx, ax
    shl bx, 1
    mov ax, [px_use_step + bx]
    add ax, [px_pcell]
    mov bx, ax
    mov al, [px_map + bx]
    test al, PXC_DOOR
    jz .nodoor
    mov ax, bx
    call px_door_of
    jc .out
    mov bx, ax
    call px_door_use
    mov byte [px_dirty], 1
    ret
.nodoor:
    test al, PXC_SPECIAL
    jz .out
    test al, PXC_SOLID
    jz .out
    mov ah, al
    mov cl, 4
    shr ah, cl
    cmp ah, PXM_SWITCH
    jne .out
    mov byte [px_map + bx], (PXM_USED << 4) | PXC_SOLID
    mov ax, bx
    call px_cellP2T
    mov bx, ax
    mov byte [px_mapT + bx], (PXM_USED << 4) | PXC_SOLID
    add word [px_score], 1000       ; the floor ends: its card, and the next
    mov byte [px_dirty], 1          ; floor on Space (px_done_next, 97.13)
    jmp px_level_done
.out:
    ret

px_use_step: dw 1, 64, -1, -64      ; E S W N: the cell ahead

; px_cellP2T - AX = a plain cell (y*64 + x) -> AX = its mapT index (x*64 + y)
;              - the same swap as px_cellT2P, which is its own inverse
px_cellP2T:
    jmp px_cellT2P

; -----------------------------------------------------------------------------
; px_player_hurt - AL damage to the player (97.8): none under the gates'
;                  px_god; at 0 the DIE wash starts and the floor restarts
;                  when it ends
; clobbers AX
; -----------------------------------------------------------------------------
px_player_hurt:
    cmp byte [px_god], 0
    jne .out
    cmp byte [px_state], PXST_PLAY
    jne .out
    mov ah, [px_health]
    sub ah, al
    jbe .dead
    mov [px_health], ah
    mov al, PXSFX_HIT
    jmp px_sfx
.out:
    ret
.dead:
    mov byte [px_health], 0
    mov byte [px_grin], 0           ; (no grin carried into the next life)
    mov byte [px_state], PXST_DYING
    mov byte [px_cardd], 0
    mov byte [px_fade], PX_FADE
    mov byte [px_dirty], 1
    mov al, PXSFX_DIE
    jmp px_sfx

; px_lev_wall - CX cells from SI are every one SOLID and none a DOOR: CF=1
;               if not. clobbers AL, CX, SI
px_lev_wall:
.c:
    mov al, [si]
    and al, PXC_SOLID | PXC_DOOR
    cmp al, PXC_SOLID
    jne .no
    inc si
    loop .c
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; The detail selector (SPEC.md 97.8): the ladder's positions in wave 1, Auto
; by tier and by the measured frame, a pinned rung that never moves
; -----------------------------------------------------------------------------
PX_APOS_N   equ 4
px_apos_tab:                        ; (rung, low res) per position, top first
    db PXR_TEX, 0                   ; 0 Textured Full
    db PXR_TEX, 1                   ; 1 Textured Low res - the 8086's default
    db PXR_FLAT, 0                  ; 2 Flat Full - an 8086 steps past it
    db PXR_FLAT, 1                  ; 3 Flat Low res - the floor, and where
                                    ; an 8086's WINDOW starts (97.8)
; THE 8086's STEP-UP LINES, by the position a step up LANDS on (SPEC.md
; 97.8; decided at wave 6's close): the budget over
; the largest ratio measured between that rung and the one under it, full
; repaint and finished frame, on the 5150's CGA and Hercules brackets and
; the Hercules window (97.15) - so a frame under the line lands under the
; budget on the rung above. Half the budget (a 286's line, PX_BUDGET50) is
; under every frame an 8086 draws at any position, which made a step down
; permanent for the session. px_ftime's unit, split into two word tables
px_auplo:                           ; ...the low words, by the landing position
    dw PX_AUPF & 0xFFFF             ; 0 Textured Full over Textured Low res
    dw PX_AUP1 & 0xFFFF             ; 1 Textured Low res over Flat Low res
    dw PX_AUPF & 0xFFFF             ; 2 Flat Full over Flat Low res
px_auphi:                           ; ...and the high words
    dw PX_AUPF >> 16
    dw PX_AUP1 >> 16
    dw PX_AUPF >> 16
; WIRE IS OFF THE LADDER (SPEC.md 97.8, the user's decision on the wave-1
; measurement): on the 5150 its turning draw is 76.4 ms against Flat Low
; res's 68.7 and its full repaint 106.4 against 85.5 (76.5 against 66.7
; and 87.0 after the Flat two-ends arm of 97.5, which Wire has no use
; for), and the stage split
; says WHERE - the cast and the present are identical between the two rungs
; (the same 32 rays, the same 80-row band: 31.1 and 21.2 ms on both), and
; Wire's COMPOSE is the dearer one - 24.6 ms against Flat's 18.4 turning,
; 51.4 against 31.7 on a full repaint (the --stages run on _cga_gla).
; THE PREMIUM IS A ROUTINE, NOT THE PICTURE: counted on the host with the
; gate's own model (tests/pxslib.py's _wire_runs/_wrun_rows) over the frame
; measured - scene A, 32 columns, one PX_TURN - Flat takes 53 ladder
; entries and stores 1,082 rows, Wire 77 entries and 309 rows plus 20
; px_pix stores, so Wire's DRAWING is the cheaper by ~2.5 ms (+24 entries
; at ~280 clk, -773 rows at 28.3). The measured +6.2 ms is therefore ~+8.7
; ms of bookkeeping - ~1,300 clk a column in px_wrun, px_ground and the
; eight-operand neighbour test, cs:-prefixed and branchy - every byte of
; which is removable without moving a pixel; the full repaint reads the same
; way (both rungs store 2,560 rows, Wire pays 192 entries to Flat's 96, 5.6
; ms of a 19.7 ms premium). A floor slower than the rung above it that can
; never climb back (76.4 > the 62.5 step-up line) is a trap TODAY, so the
; decision stands; it is CONTINGENT on px_wrun's cost and a cheaper one
; re-opens the floor. Wire stays a pinnable Detail item - a picture trade,
; edges only - and never Auto's.

; px_auto_init - the defaults, once at entry: Auto, Low res on an 8086
px_auto_init:
    mov byte [px_detail], PXD_AUTO
    mov byte [px_res], 0
    mov byte [px_sizeix], 2         ; Size 64 (px_sizes): 97.1's default,
                                    ; the user's "full width" direction -
                                    ; wave 3's review put it back after the
                                    ; first cut moved it to 48 on a scene
                                    ; the plan did not pin (97.1's fork)
    mov byte [px_rowsix], 0         ; Rows 80
    cmp byte [px_tier], CPU_8086
    jne .out
    mov byte [px_res], 1
.out:
    ret

; px_auto_start - the position a bracket ([px_inbr] = 1) or a window starts
;                 at on this tier, then the rung and resolution in force
;
; THE 8086 STARTS A BRACKET AT TEXTURED LOW RES (position 1, 97.1's
; default) AND A WINDOW AT FLAT LOW RES (position 3: PLAN 15's "one rung
; lower in a window" - and Flat Full, the position between, is DEARER than
; Textured Low res on an 8086, 164.4 against 113.9 ms, so it is no rung
; lower at all; wave 6's close) - SPEC.md 97.8. Wire is off the ladder
; (px_apos_tab above). A step down on an 8086 climbs back over the rung
; above's own line (px_auplo)
px_auto_start:
    push ax
    xor al, al                      ; 286 and up: the top
    cmp byte [px_tier], CPU_8086
    jne .pos
    mov al, 1                       ; the 8086: Textured Low res in a bracket
    cmp byte [px_inbr], 0           ; (97.1's default)...
    jne .pos
    mov al, 3                       ; ...and Flat Low res in a window
                                    ; (PLAN 15: one rung lower, the windowed
                                    ; tax being the OS's)
.pos:
    mov [px_apos], al
    mov [px_astart], al             ; ...and no step up passes it
    mov byte [px_amiss], 0
    mov byte [px_ahit], 0
    call OSAPI_GET_TICKS
    mov [px_ahold], ax              ; no hold-down owed at the start
    call px_apply
    pop ax
    ret

; px_apply - [px_rung] and [px_lowres] from the Detail row's two axes (the
;            rung and the resolution, px_detail / px_res), or from Auto's
;            position; a Textured rung's sets built if they are not (97.3,
;            px_tex_setup - so a window that never picks Textured never
;            pays the 0.9 s, and a bracket pays it behind its black); the
;            resolution's words follow, and - ONLY WHEN THE BAND MOVED
;            (px_lastx0) - the shadow is cleared and the glass outside a
;            bracket's band blacked; the text line is owed
; preserves every register (BP included: px_gen_one keeps a run's first row
; in it - the first cut pushed all but BP and said "clobbers nothing", and
; px_frame_end reaches here through px_auto_frame; review, wave 2)
px_apply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [px_detail]
    cmp al, PXD_AUTO
    jne .pinned
    mov bl, [px_apos]
    xor bh, bh
    shl bx, 1
    mov al, [px_apos_tab + bx]
    mov [px_rung], al
    mov al, [px_apos_tab + bx + 1]
    mov [px_lowres], al
    jmp short .words
.pinned:
    mov byte [px_rung], PXR_FLAT
    cmp al, PXD_WIRE
    jne .ntex
    mov byte [px_rung], PXR_WIRE
.ntex:
    cmp al, PXD_TEX
    jne .res
    mov byte [px_rung], PXR_TEX
.res:
    mov al, [px_res]
    mov [px_lowres], al
.words:
    cmp byte [px_rung], PXR_TEX
    jne .have
    call px_tex_setup               ; the sets for this backend, if not built
    jnc .have                       ; TEXTURED NEEDS ITS PARTS (97.9): a rung
    mov byte [px_rung], PXR_FLAT    ; the loader could not carve for - or the
                                    ; generator could not fit on this
                                    ; backend - lands on Flat, the item
                                    ; having said why
.have:
    call px_res_apply
    mov al, [px_x0]
    cmp al, [px_lastx0]
    je .band                        ; THE BAND STANDS: a rung or resolution
    mov [px_lastx0], al             ; change leaves nothing outside it, and
                                    ; the full repaint below rewrites every
                                    ; byte inside it in either resolution -
                                    ; the first cut cleared and blanked on
                                    ; EVERY pick, 43 ms an Auto step, charged
                                    ; to no frame (review, wave 2)
    call px_shadow_clear            ; THE SHADOW IS THE PICTURE (97.3): a
                                    ; narrower Size leaves the wider band's
                                    ; edges in it otherwise - never shown
                                    ; (the presents send Size bytes at x0)
                                    ; but not the frame either, and
                                    ; tests/pxssim.py diffs the whole 80 x
                                    ; 80. 3.2 K words, once a Size
    cmp byte [px_inbr], 0
    je .win
    call px_band_blank              ; the bracket: the glass outside the band
    jmp short .band                 ; (a narrower Size leaves the wider
                                    ; band's edges lit otherwise, pxrast.inc)
.win:
    mov byte [px_repaint], 1        ; windowed: the black around a narrower
                                    ; band is owed (pxwin.inc)
.band:
    call px_force_all
    mov byte [px_lined], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_auto_frame - one drawn frame took [px_ftime] (838ns units): eight over
;                 the budget step down, sixty-four under the step-up line
;                 step up - HALF the budget on a 286 or better, the rung
;                 above's own line on an 8086 (px_auplo), whose ladder is
;                 Textured Full, Textured Low res, Flat Low res with Flat
;                 Full skipped both ways - never above the tier's start,
;                 never inside the hold-down a step down began (SPEC.md 97.8)
;
; THE THRESHOLD ALONE IS NOT HYSTERESIS. The first cut stepped up at 60% of
; the budget, and the rung above measures 1.46-1.92x this one's draw (Mode
; X's work is the 1.92, cast and compose alone; the four with a present
; read 1.46-1.69), so the
; 8086's default climbed to Flat Full after 64 turning frames (68.7 ms <
; 75.0) and, on Hercules, flipped back and forth across a doorway. Half the
; budget clears every measured ratio up to 2.0 - and on an 8086 it is under
; every frame any position draws, so a step down there never came back
; (wave 6's close gave the 8086 the rung above's own line instead:
; px_auplo, 111.1 ms onto Textured Low res); the start position is the
; ceiling because Auto steps BACK up and was never given the rung above;
; and PX_AHOLD ticks must pass after a step down before any step up.
px_auto_frame:
    push ax
    push bx
    push dx
    cmp byte [px_detail], PXD_AUTO
    jne .out
    mov ax, [px_ftime]
    mov dx, [px_ftime + 2]
    cmp dx, PX_BUDGET >> 16
    jb .under
    ja .miss
    cmp ax, PX_BUDGET & 0xFFFF
    ja .miss
.under:
    mov byte [px_amiss], 0
    xor bx, bx
    mov bl, [px_apos]
    cmp bl, [px_astart]
    jbe .mid                        ; at the start, or above it: the ceiling
    dec bx                          ; the position above...
    cmp byte [px_tier], CPU_8086
    jne .l50                        ; (a 286 or better: half the budget,
                                    ; the rung above being 1.5-1.9x)
    cmp bl, 2                       ; ...but an 8086 climbs from Flat Low
    jne .ltab                       ; res past Flat Full to Textured Low
    cmp [px_astart], bl             ; res (Flat Full is DEARER than it,
    je .ltab                        ; 97.8), unless Flat Full is where a
    dec bx                          ; Resolution pick seated its ceiling
.ltab:
    shl bx, 1                       ; THE LINE IS THE RUNG ABOVE'S (97.8):
    cmp dx, [px_auphi + bx]         ; the budget over the largest ratio
    jb .hit                         ; measured between the two rungs
    ja .mid
    cmp ax, [px_auplo + bx]
    ja .mid
    jmp short .hit
.l50:
    shl bx, 1
    cmp dx, PX_BUDGET50 >> 16
    jb .hit
    ja .mid
    cmp ax, PX_BUDGET50 & 0xFFFF
    ja .mid
.hit:
    inc byte [px_ahit]
    cmp byte [px_ahit], 64
    jb .out
    mov byte [px_ahit], 0
    call OSAPI_GET_TICKS
    mov dx, ax
    sub ax, [px_ahold]
    js .out                         ; the hold-down has not passed: another
                                    ; sixty-four, then
    mov [px_ahold], dx              ; RETIRED: the deadline is now "now", so
                                    ; the signed difference cannot lap back
                                    ; under zero 32,768 ticks (30 min) after
                                    ; it passed and refuse every step up for
                                    ; the next 30 (CLAUDE.md's rule 3;
                                    ; px_worker's resync is the same idiom)
    shr bx, 1
    mov [px_apos], bl               ; A step UP, to the position judged
    jmp short .step
.mid:
    mov byte [px_ahit], 0
    jmp short .out
.miss:
    mov byte [px_ahit], 0
    inc byte [px_amiss]
    cmp byte [px_amiss], 8
    jb .out
    mov byte [px_amiss], 0
    mov al, [px_apos]
    cmp al, PX_APOS_N - 1
    jae .out
    inc ax                          ; a step DOWN...
    cmp byte [px_tier], CPU_8086
    jne .down
    cmp al, 2                       ; ...and an 8086 falls from Textured Low
    jne .down                       ; res straight to Flat Low res (97.8)
    inc ax
.down:
    mov [px_apos], al
    call OSAPI_GET_TICKS
    add ax, PX_AHOLD                ; ...and the tick a step up may follow
    mov [px_ahold], ax
.step:
    call px_apply
    call px_auto_say
.out:
    pop dx
    pop bx
    pop ax
    ret

; px_auto_say - a step is announced ONCE: the window's line says it
;               (px_apply owes the line), and in a bracket the bar's label
;               row says it, which is wave 4's half of 97.8's "announced
;               once" (97.13)
px_auto_say:
    cmp byte [px_inbr], 0
    je .out
    mov byte [px_hmsgw], PXM_DETAIL
.out:
    ret

; =============================================================================
; THE STATES (SPEC.md 97.13; wave 4). px_state is ONE byte both tasks read:
; the worker (or the bracket's loop) moves it on its clocks - READY's, the
; DIE wash's, OVER's - and the UI task moves it on a key (px_key_common),
; every transition writing the byte and owing a card, never loading a floor
; itself: a floor loads BETWEEN FRAMES (px_levnext, px_frame_begin), where
; nothing is composing the maps it rewrites
; =============================================================================
PX_READYT   equ 27                  ; ticks READY stands: 1.5 s
PX_CARDHOLD equ 9                   ; ...and every card before a key moves it
                                    ; on: half a second, longer than a
                                    ; typematic repeat's delay is short
PX_OVERT    equ 55                  ; ...and OVER: 3 s

; px_timers - once a step, in every state: the grin, the bar's message
; preserves every register but AX
px_timers:
    cmp byte [px_cardhold], 0       ; A CARD'S HOLD AGAINST A HELD KEY: its
    je .g                           ; ticks, and then until Space and Enter
    dec byte [px_cardhold]          ; both read UP - the key that threw the
    jnz .g                          ; switch or ended OVER is often still
    mov al, KSC_SPACE               ; down, and its typematic repeat skipped
    call OSAPI_KEY_DOWN             ; the LEVELDONE card and READY after it
    jc .still                       ; in one press (tests/pxsact.py's leg j;
    mov al, KSC_ENTER               ; a tick count alone was not enough)
    call OSAPI_KEY_DOWN
    jc .still
    cmp byte [px_inbr], 0           ; RELEASED - and in a bracket the BIOS
    je .g                           ; buffer is emptied here: a typematic
.drain:                             ; repeat typed just before the release is
    mov ah, 1                       ; still in it, and px_input would read it
    int 0x16                        ; on the next pass as a FRESH press and
    jz .g                           ; move the card on (tests/pxsstate.py saw
    xor ah, ah                      ; LEVELDONE skipped this way). A window's
    int 0x16                        ; keys are the kernel's events, dispatched
    jmp short .drain                ; as they arrive, and have no such lag
.still:
    mov byte [px_cardhold], 1       ; ...and asked again next step
.g:
    cmp byte [px_grin], 0
    je .m
    dec byte [px_grin]
.m:
    mov al, [px_hmsgw]              ; a message owed: posted now, after the
    or al, al                       ; pick it names is in force
    jz .t
    mov byte [px_hmsgw], 0
    mov [px_hmsg], al
    add byte [px_hmsgn], 8          ; a new post, even of the same message
                                    ; (8: the snapshot sums it with a PXM_*
                                    ; of 0..7, px_hud_snap)
    mov byte [px_hmsgt], PX_MSGT
    ret
.t:
    cmp byte [px_hmsgt], 0
    je .out
    dec byte [px_hmsgt]
    jnz .out
    mov byte [px_hmsg], PXM_NONE    ; the labels back
.out:
    ret

; px_pause_msg - the pause just changed: PAUSED on the bar's label row while
;                it stands, the labels back when it goes (both worlds)
px_pause_msg:
    mov byte [px_hmsgt], 0          ; sticky: no clock
    mov byte [px_hudd], 1           ; a paused game takes no step, so no poll
    cmp byte [px_pause], 0          ; would see it: owed here
    je .off
    mov byte [px_hmsg], PXM_PAUSED
    ret
.off:
    cmp byte [px_hmsg], PXM_PAUSED
    jne .out
    mov byte [px_hmsg], PXM_NONE
.out:
    ret

; px_new_game - AL = the floor: a game from scratch there, loaded between
;               frames (px_level_next's px_newgame arm). Any task
px_new_game:
    call px_map_off                 ; (a game from scratch: no map up)
    mov [px_levnext], al
    mov byte [px_newgame], 1
    mov byte [px_restart], 0
    mov byte [px_codep], 0xFF
    mov byte [px_demodone], 0
    mov byte [px_pause], 0
    ret

; px_play_begin - READY is over: PLAY, the view repainted whole, and a Space
;                 or Ctrl still held from the card is not a press
px_play_begin:
    mov byte [px_state], PXST_PLAY
    mov byte [px_cardd], 0          ; nothing of a card is owed in PLAY
    mov byte [px_usek], 1
    mov byte [px_firek], 1
    call px_force_all
    mov byte [px_lined], 1
    ret

; px_game_over - the last life's wash is over, or the last floor was left
px_game_over:
    call px_map_off
    mov byte [px_cardhold], PX_CARDHOLD
    mov byte [px_state], PXST_OVER
    mov byte [px_stimer], PX_OVERT
    mov byte [px_pause], 0
    mov byte [px_cardl], 0
    mov byte [px_lined], 1
    jmp px_card_owe

; px_over_end - OVER's clock ran out or a key moved it on: the initials if the
;               score earns a row (px_hs_qual), else the attract page
px_over_end:
    call px_hs_qual
    jnc px_to_attract
    mov byte [px_cardhold], PX_CARDHOLD
    mov byte [px_state], PXST_ENTER
    mov word [px_ini], '  '
    mov byte [px_ini + 2], ' '
    mov byte [px_inip], 0
    mov byte [px_cardl], 0
    jmp px_card_owe

; px_to_attract - the attract page (the end of a game, Esc in a window)
px_to_attract:
    call px_map_off                 ; (Esc in a window over the map)
    mov byte [px_cardhold], PX_CARDHOLD
    mov byte [px_state], PXST_ATTRACT
    mov byte [px_codep], 0xFF
    mov byte [px_pause], 0
    mov byte [px_hmsg], PXM_NONE
    mov byte [px_cardl], 0
    mov byte [px_lined], 1
    jmp px_card_owe

; px_level_done - the switch was thrown: the floor's card, its ratios and its
;                 time (px_ltime), until Space (px_done_next)
px_level_done:
    mov byte [px_cardhold], PX_CARDHOLD
    mov byte [px_state], PXST_DONE
    mov ax, [px_dtick]
    sub ax, [px_ltick0]
    mov [px_ltime], ax
    mov al, PXSFX_ELEV
    call px_sfx
    mov byte [px_cardl], 0
    mov byte [px_lined], 1
    jmp px_card_owe

; px_done_next - Space on LEVELDONE: the next floor, or after the last one the
;                end of the episode (OVER, "YOU ESCAPED")
px_done_next:
    mov al, [px_floor]
    inc al
    cmp al, PXL_NLEV
    jae .won
    mov [px_levnext], al
    mov byte [px_restart], 0
    ret
.won:
    mov byte [px_victory], 1
    jmp px_game_over

; px_floor_stats - a floor begins: its clock, and the totals the LEVELDONE
;                  card's ratios are of - the guards, the secret doors
;                  (SPECIAL on a door cell), the treasure and the chalices.
;                  clobbers AX, CX, SI
px_floor_stats:
    mov ax, [px_dtick]
    mov [px_ltick0], ax
    xor ax, ax
    mov [px_ckill], al
    mov [px_csec], al
    mov [px_ctreas], al
    mov al, [px_nact]
    mov [px_nkill], al
    xor ah, ah                      ; AH = the secret doors
    mov cl, [px_ndoors]
    mov si, px_doors
    or cl, cl
    jz .st
.d:
    test byte [si + PXD_FLAGS], PXC_SPECIAL
    jz .dn
    inc ah
.dn:
    add si, PXD_SIZE
    dec cl
    jnz .d
.st:
    mov [px_nsec], ah
    xor ah, ah                      ; AH = the treasure and the chalices
    mov cl, [px_nstat]
    mov si, px_stat
    or cl, cl
    jz .out
.s:
    mov al, [si + PXT_KIND]
    cmp al, PXK_TREAS
    je .t
    cmp al, PXK_CHALICE
    jne .sn
.t:
    inc ah
.sn:
    add si, PXT_SIZE
    dec cl
    jnz .s
.out:
    mov [px_ntreas], ah
    ret

; -----------------------------------------------------------------------------
; px_mouse_turn - the pointer's offset from the view's middle, a quarter of
;                 it and clamped to PX_TURN, added to BL (the step's turn;
;                 97.13). The middle is the band's in a window and the
;                 BRACKET'S in a bracket - OSAPI_FSX_SURF's rect, banked in
;                 px_fmid (SPEC.md 53.7.1: on a two-card desktop the
;                 screen OSAPI_VIDEO names is the primary, not the card
;                 the bracket is on); a dead zone of 8 dots each way.
;                 clobbers AX; BL in, BL out
; -----------------------------------------------------------------------------
px_mouse_turn:
    push cx
    push dx
    call OSAPI_MOUSE                ; CX = x
    mov ax, [px_fmid]               ; the bracket's own middle, off
    cmp byte [px_inbr], 0           ; OSAPI_FSX_SURF (px_fsx_main; wave 6 -
    jne .c                          ; OSAPI_VIDEO's width named the primary)
    mov al, [px_size]               ; the band is px_bx-anchored and Size x 8
    xor ah, ah                      ; dots wide (px_blit_win), so its middle
    shl ax, 1                       ; is px_bx + Size x 4 - not px_bx + 256,
    shl ax, 1                       ; which at Size 48 read a pointer on the
    add ax, [px_bx]                 ; middle as 64 dots right (review r2)
.c:
    sub cx, ax
    sar cx, 1
    sar cx, 1                       ; a quarter of the offset...
    cmp cx, 2
    jg .hi
    cmp cx, -2
    jge .none                       ; ...inside the dead zone: nothing
.hi:
    cmp cx, PX_TURN
    jle .lo
    mov cx, PX_TURN
.lo:
    cmp cx, -PX_TURN
    jge .add
    mov cx, -PX_TURN
.add:
    add bl, cl
    jno .none
    mov bl, 127                     ; the key and the mouse both turning
    js .none                        ; right overflowed: the most a byte turns
    mov bl, -127
.none:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; px_sfx - the sound effect AL (PXSFX_*) through OSAPI_SND_TONE, when the Sound
;          item is on (97.13): fire and forget, a duration so it silences
;          itself, worker-safe by the slot's own construction (os88api.inc).
;          preserves every register
; -----------------------------------------------------------------------------
PXSFX_SHOT  equ 0
PXSFX_KNIFE equ 1
PXSFX_DOOR  equ 2
PXSFX_PICK  equ 3
PXSFX_HIT   equ 4
PXSFX_DIE   equ 5
PXSFX_ELEV  equ 6
px_sfxtab:                          ; Hz, ticks
    dw 880, 1                       ; a shot: pistol or gun
    dw 330, 1                       ; the knife
    dw 196, 2                       ; a door
    dw 1319, 2                      ; a pickup
    dw 131, 2                       ; the player hit
    dw 98, PX_FADE                  ; the death, the length of the wash
    dw 523, 6                       ; the elevator
px_sfx:
    cmp byte [px_sound], 0
    je .out
    inc word [px_sfxn]              ; the gate's count, and the last one
    mov [px_sfxl], al               ; asked (tests/pxsstate.py leg c2)
    push ax
    push bx
    push cx
    push dx
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    mov ax, [px_sfxtab + bx]
    mov cx, [px_sfxtab + bx + 2]
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; =============================================================================
; THE TIMEDEMO (SPEC.md 97.13; PIXELSTEIN-PLAN 13.7): from the attract page,
; the two pinned scenes' input scripts replayed UNTHROTTLED - one scripted
; step a frame, no tick wait (px_steps), the world frozen - for a fixed
; number of frames, and frames / ticks / fps on the card, so a 5150 owner
; can report the About card's number without MartyPC. A bracket's (the T
; key in a window takes the machine first): it measures the fullscreen
; promise. Scene A is the first floor's spawn looking down the start hall;
; scene B is tools/pxssim.py's pose one tile south-east of the hall's door
; =============================================================================
PXD_BX      equ 14 * 256 + 128      ; scene B (tools/pxssim.py's scenes())
PXD_BY      equ 4 * 256 + 128
PXD_BH      equ 512
px_demo_script:                     ; steps, turn a step, walk a step, scene B
    db 16, 0, PX_SPEED, 0           ; A: down the start hall
    db 32, PX_TURN, 0, 0            ; A: half a turn to the right
    db 32, PX_TURN, 0, 1            ; B: through the doorway's turn
    db 0
PX_DEMOFR   equ 81                  ; ...the frames it draws: the 80 steps'
                                    ; and the start's own forced repaint,
                                    ; which the count includes (the clock
                                    ; starts before it is composed)

; px_demo_start - between frames (px_frame_begin): the first floor, scene A,
;                 the default Detail and Size pinned, the player as a new game
;                 has him, the clocks. clobbers everything but DS
;
; EVERY OWNER MEASURES THE SAME FRAMES (review, wave 4): the run is pinned
; to 97.1's default - Textured Low res, Size 64 - whatever Detail, Size or
; Auto position is in force, and a PINNED Detail is one px_auto_frame never
; steps (so no rung moves mid-run on a box that misses the budget); and the
; bar is a new game's, not whatever the last game left on it. What was in
; force comes back at the end or the abort (px_demo_restore)
px_demo_start:
    mov byte [px_demoreq], 0
    mov byte [px_demoab], 0
    xor al, al
    call px_level_load
    jc .no
    call px_spot_clear
    mov byte [px_keys], 0
    mov byte [px_state], PXST_DEMO
    mov byte [px_cardd], 0          ; no card owed in the demo
    mov al, [px_detail]             ; what is in force, kept...
    mov [px_demosv], al
    mov al, [px_res]
    mov [px_demosv + 1], al
    mov al, [px_sizeix]
    mov [px_demosv + 2], al
    mov byte [px_demosvd], 1
    mov byte [px_detail], PXD_TEX   ; ...and the default pinned (a machine
    mov byte [px_res], 1            ; with no Textured parts lands on Flat
    mov byte [px_sizeix], 2         ; in px_apply, and the card names it)
    call px_apply                   ; (the sets built before the clock)
    mov word [px_score], 0          ; the bar a new game's
    mov byte [px_lives], PX_LIVES0
    mov byte [px_health], PX_HEALTH0
    mov byte [px_ammo], PX_AMMO0
    mov byte [px_weapon], PXW_PISTOL
    mov byte [px_grin], 0
    mov word [px_demoi], px_demo_script
    mov byte [px_demon], 0
    mov ax, [px_frames]
    mov [px_demof0], ax
    call OSAPI_GET_TICKS
    mov [px_demot0], ax
    call px_force_all
    ret
.no:
    jmp px_to_attract

; px_demo_restore - the Detail and Size in force before the run, back and
;                   applied between frames (px_pend). Any exit of the run.
;                   clobbers nothing
px_demo_restore:
    cmp byte [px_demosvd], 0
    je .out
    mov byte [px_demosvd], 0
    push ax
    mov al, [px_rung]               ; what the run drew at, for the card
    mov [px_demorg], al
    mov al, [px_lowres]
    mov [px_demorg + 1], al
    mov al, [px_size]
    mov [px_demorg + 2], al
    mov al, [px_demosv]
    mov [px_detail], al
    mov al, [px_demosv + 1]
    mov [px_res], al
    mov al, [px_demosv + 2]
    mov [px_sizeix], al
    mov byte [px_pend], 1
    pop ax
.out:
    ret

; px_demo_step - one scripted step (px_step in DEMO): the next run's turn and
;                walk, scene B's pose where the script says, and a frame owed
;                whatever the step did. clobbers everything but DS
px_demo_step:
    cmp byte [px_demoab], 0
    jne .abort
    cmp byte [px_demon], 0
    jne .run
    mov si, [px_demoi]
    mov al, [si]
    or al, al
    jz .end
    mov [px_demon], al
    mov ax, [si + 1]                ; the run's turn and walk, kept for every
    mov [px_demokt], ax             ; step of it (px_kturn is rewritten by
    cmp byte [si + 3], 0            ; whatever else reads the keys)
    je .nopose
    mov word [px_px], PXD_BX
    mov word [px_py], PXD_BY
    mov word [px_head], PXD_BH
    call px_sincos
    call px_force_all
.nopose:
    add si, 4
    mov [px_demoi], si
.run:
    dec byte [px_demon]
    mov ax, [px_demokt]
    mov [px_kturn], al
    mov [px_kfwd], ah
    mov byte [px_kstr], 0
    call px_step_move
    mov byte [px_dirty], 1
    ret
.end:
    mov ax, [px_frames]
    sub ax, [px_demof0]
    mov [px_demof], ax
    call OSAPI_GET_TICKS
    sub ax, [px_demot0]
    mov [px_demot], ax
    mov cx, ax
    mov ax, [px_demof]              ; fps in tenths: frames x 182 / ticks,
    mov dx, 182                     ; ROUNDED - half the divisor added first,
    mul dx                          ; so the card reads what 97.15 quotes
    jcxz .zero                      ; (81 in 149 ticks is 9.90: truncated,
    push cx                         ; the card said 9.8; review, wave 6 r2)
    shr cx, 1
    add ax, cx
    adc dx, 0
    pop cx
    cmp dx, cx
    jae .zero                       ; (a quotient past a word: not a clock)
    div cx
    jmp short .fps
.zero:
    xor ax, ax
.fps:
    mov [px_demofps], ax
    mov byte [px_demodone], 1
    call px_demo_restore
    jmp px_to_attract
.abort:
    mov byte [px_demodone], 0
    call px_demo_restore
    jmp px_to_attract
