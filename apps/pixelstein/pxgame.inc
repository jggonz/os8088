; =============================================================================
; os8088 - apps/pixelstein/pxgame.inc
;
; THE SESSION (SPEC.md 96.8): the clock, the two readers of the keyboard, the
; player's step with per-axis collision, the level's expansion into the two
; map layouts, the detail selector and one key handler for both worlds.
; Wave 1 is PLAY from the first paint; the states, the doors that slide,
; the guards and the scores are later waves'.
; =============================================================================

; -----------------------------------------------------------------------------
; px_steps - how many SIMULATION steps this frame owes, and the wait if none
;            (apps/tank/tkgame.inc's tk_steps, verbatim in shape)
; out: CX = 0..PX_MAXSTEP.  clobbers AX, BX
; -----------------------------------------------------------------------------
px_steps:
    call OSAPI_GET_TICKS
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
.pause:
    xor byte [px_pause], 1
    mov byte [px_lined], 1
    ret
.full:
    cmp byte [px_inbr], 0
    jne .leave                      ; F leaves as well as enters (SPEC.md
    call px_go_fsx                  ; 11.2.1); entering is only ever reached
    ret                             ; from W_ONKEY or the menu, the UI task
.esc:
    cmp byte [px_inbr], 0
    je .out                         ; windowed, Esc is wave 4's (the attract)
.leave:
    mov byte [px_fsxq], 1
    ret

; -----------------------------------------------------------------------------
; px_keys_read - the held keys, latched into what a step spends (SPEC.md
;                96.8, apps/tank's latch-and-spend): OSAPI_KEY_DOWN once a
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
; px_step - one simulation step: spend what px_keys_read latched (96.8)
; clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
px_step:
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
    ; x first, against the four corner cells at the OLD y (SPEC.md 96.8)
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
    mov cl, [px_map + bx]
    pop ax
    add dx, PX_RADIUS * 2
    call px_cell
    or cl, [px_map + bx]
    pop dx
    test cl, PXC_SOLID | PXC_DOOR   ; a shut door blocks (the slide is wave 3)
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
; px_level_load - the level stream's first record into the two map layouts
;                 and the eye (SPEC.md 96.7, 96.2)
; in:  [px_hand + PXH_LEV] = the stream's segment.  out: CF=1 refused
; clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
px_level_load:
    mov ax, [px_hand + PXH_LEV]
    or ax, ax
    jz .bad
    mov es, ax
    cmp word [es:0], 'PX'           ; 'PXL', 1
    jne .bad
    cmp word [es:2], 'L' | (1 << 8)
    jne .bad
    mov ax, [es:PXL_SPAWNX]
    mov [px_px], ax
    mov ax, [es:PXL_SPAWNY]
    mov [px_py], ax                 ; ...both checked against the map below
    mov ax, [es:PXL_SPAWNA]
    and ax, PX_ANG - 1
    mov [px_head], ax
    ; the run-length map, row-major, into px_map. EVERY BYTE READ OFF THE
    ; FLOPPY IS HOSTILE (CLAUDE.md; 96.7): this is the one place a file byte
    ; becomes a length, so a zero run, a run past the map's end and an odd
    ; pair length each refuse the stream BEFORE a write - the first cut
    ; checked the total after the writes, and a count of 0 had by then
    ; stored 65,536 bytes from px_map through the rest of the carve.
    ; THE SOURCE IS BOUNDED TOO: PXL_MAPLEN is a file word, and the loader
    ; handed over the part's own length (PXH_LEVLEN, the row's OP_R_LEN), so
    ; a record shorter than its header or a pair length past the part's end
    ; is refused before SI walks into the next part of the carve - the
    ; second cut bounded the destination alone and read up to 8 KB past a
    ; 1 KB part
    mov ax, [px_hand + PXH_LEVLEN]
    cmp ax, PXL_HDR
    jb .bad
    sub ax, PXL_HDR                 ; AX = the bytes the record can hold
    mov si, PXL_HDR
    mov di, px_map
    mov dx, [es:PXL_MAPLEN]         ; bytes of pairs...
    cmp dx, ax
    ja .bad                         ; ...not more than the part holds
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
    jb .bad                         ; ...and this run would overrun them
.fill:
    mov [di], al
    inc di
    loop .fill
    jmp short .rle
.expanded:
    or dx, dx
    jnz .bad                        ; an odd pair length
    cmp di, px_map + 4096
    jne .bad                        ; a stream that is not a whole map
    ; THE PERIMETER IS SOLID AND HOLDS NO DOOR, AND THE SPAWN IS INSIDE IT
    ; (96.7): the walkers of 96.2 have no crossing counter - they stop on a
    ; SOLID|DOOR bit and nothing else - so one border cell with its bit
    ; clear (a rotted floppy, an edited part 1) walks them out of px_mapT
    ; and px_map for ever, marking spotvis at wrapping offsets through the
    ; column arrays, px_hand and the code. tools/pxslevel.py refuses the
    ; same level on the host; this is the machine refusing the FILE. 256
    ; byte tests once at load, against a walk that cannot be bounded
    ; cheaply inside the body
    mov si, px_map                  ; the top row
    mov cx, 64
    call px_lev_wall
    jc .bad
    mov si, px_map + 63 * 64        ; the bottom row
    mov cx, 64
    call px_lev_wall
    jc .bad
    mov si, px_map + 64             ; the two side columns, rows 1..62
    mov cx, 62
.sides:
    mov al, [si]
    and al, PXC_SOLID | PXC_DOOR
    cmp al, PXC_SOLID
    jne .bad
    mov al, [si + 63]
    and al, PXC_SOLID | PXC_DOOR
    cmp al, PXC_SOLID
    jne .bad
    add si, 64
    loop .sides
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
    ; ...and transposed, mapT[x*64 + y] = map[y*64 + x] (96.2)
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
    call px_sincos
    clc
    ret
.bad:
    stc
    ret

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
; The detail selector (SPEC.md 96.8): the ladder's positions in wave 1, Auto
; by tier and by the measured frame, a pinned rung that never moves
; -----------------------------------------------------------------------------
PX_APOS_N   equ 2
px_apos_tab:                        ; (rung, low res) per position, top first
    db PXR_FLAT, 0                  ; 0 Flat Full
    db PXR_FLAT, 1                  ; 1 Flat Low res - the floor
; WIRE IS OFF THE LADDER (SPEC.md 96.8, the user's decision on the wave-1
; measurement): on the 5150 its turning draw is 76.4 ms against Flat Low
; res's 68.7 and its full repaint 106.4 against 85.5 (76.5 against 66.7
; and 87.0 after the Flat two-ends arm of 96.5, which Wire has no use
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
    cmp byte [px_tier], CPU_8086
    jne .out
    mov byte [px_res], 1
.out:
    ret

; px_auto_start - the position a bracket ([px_inbr] = 1) or a window starts
;                 at on this tier, then the rung and resolution in force
;
; THE 8086 STARTS AT FLAT LOW RES IN BOTH WORLDS (SPEC.md 96.8), which in
; wave 1 is also the ladder's floor: PLAN 15's "one rung lower in a window"
; is a rule about Textured being the top, and there is nothing under Flat
; Low res for Auto to hand a window - Wire is off the ladder (px_apos_tab
; above: its turning draw reads 76.5 ms against 66.7 fullscreen, 83.0
; against 74.8 windowed on the Hercules desktop, and a full repaint 106.4
; against 87.0). Wave 2 prepends Textured and the window then starts one
; position under the bracket.
px_auto_start:
    push ax
    xor al, al                      ; 286 and up: the top
    cmp byte [px_tier], CPU_8086
    jne .pos
    mov al, 1                       ; the 8086: Flat Low res
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
;            position; the resolution's words follow, and the
;            text line is owed
px_apply:
    push ax
    push bx
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
    jne .res
    mov byte [px_rung], PXR_WIRE
.res:
    mov al, [px_res]
    mov [px_lowres], al
.words:
    call px_res_apply
    call px_force_all
    mov byte [px_lined], 1
    pop bx
    pop ax
    ret

; px_auto_frame - one drawn frame took [px_ftime] (838ns units): eight over
;                 the budget step down, sixty-four under HALF of it step up -
;                 never above the tier's start, never inside the hold-down a
;                 step down began (SPEC.md 96.8)
;
; THE THRESHOLD ALONE IS NOT HYSTERESIS. The first cut stepped up at 60% of
; the budget, and the rung above measures 1.46-1.92x this one's draw (Mode
; X's work is the 1.92, cast and compose alone; the four with a present
; read 1.46-1.69), so the
; 8086's default climbed to Flat Full after 64 turning frames (68.7 ms <
; 75.0) and, on Hercules, flipped back and forth across a doorway. Half the
; budget clears every measured ratio up to 2.0; the start position is the
; ceiling because Auto steps BACK up and was never given the rung above;
; and PX_AHOLD ticks must pass after a step down before any step up.
px_auto_frame:
    push ax
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
    mov al, [px_apos]
    cmp al, [px_astart]
    jbe .out                        ; at the start, or above it: the ceiling
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
    dec byte [px_apos]              ; A step UP
    call px_apply
    jmp short .out
.mid:
    mov byte [px_ahit], 0
    jmp short .out
.miss:
    mov byte [px_ahit], 0
    inc byte [px_amiss]
    cmp byte [px_amiss], 8
    jb .out
    mov byte [px_amiss], 0
    cmp byte [px_apos], PX_APOS_N - 1
    jae .out
    inc byte [px_apos]              ; a step DOWN...
    call OSAPI_GET_TICKS
    add ax, PX_AHOLD                ; ...and the tick a step up may follow
    mov [px_ahold], ax
    call px_apply
.out:
    pop dx
    pop ax
    ret
