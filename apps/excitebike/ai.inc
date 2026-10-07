; The opponents (SPEC.md 102.4): up to three riders in Selection B, and the rider
; the attract demo drives.  An opponent is NOT a second simulation: it is the SAME
; rider (`xm_step`, sim.inc, every rule of 102.3) run on a state block of its own.
; The live `xm_` block always holds the player between frames; for each opponent
; the block is swapped in (one `rep movsw` of XM_STATE_SZ bytes, ~600 clocks), the
; frame's steps are run on it with an input byte synthesised by `xa_input`, and it
; is swapped out again.  One rule of physics, so an opponent cannot cheat, ride
; through a hurdle or take a ramp the player would not, and the guest == model
; test of the player (tests/excitebike_ref.py) is untouched: with no opponents
; (`xb_nai` = 0, the default) none of this code runs.
;
; PRICES (8088, clocks): a swap in and out ~1.2k a frame an opponent; a step 3.0k
; (the sim) + ~0.3k (`xa_input`: the throttle and lane logic) + 0.35k (the lane
; look-ahead, 4 lanes x 10 columns x ~70, once every 12 steps); the record 0.4k; the
; collision pass 0.15k a pair.  Three opponents at 1.1 steps a frame: ~17k a frame.
;
; THE RIDER'S BRAIN is exbsim.bot_input, the reference rider that finishes every
; course at turbo, with three changes that make it a competitor rather than a
; lap-timer: SKILL (1..3) gates the turbo (skill 3 as the reference, 2 three steps
; in four, 1 one in four), a RUBBER BAND (an opponent more than XA_RUBBER pixels
; ahead of the player throttles down, one more than XA_CATCHUP behind takes the
; turbo whatever its skill) keeps the field together, and a look-ahead lane choice
; made every 12 steps instead of every step.

; offsets of the fields the collision pass touches, from the start of a state block
XMO_POS         equ xm_pos - xm_state
XMO_SPEED       equ xm_speed - xm_state
XMO_A           equ xm_A - xm_state
XMO_VY          equ xm_vy - xm_state
XMO_GH          equ xm_gh - xm_state
XMO_VG          equ xm_vg - xm_state
XMO_MTIMER      equ xm_mtimer - xm_state
XMO_LANE        equ xm_lane - xm_state
XMO_MODE        equ xm_mode - xm_state
XMO_PITCH       equ xm_pitch - xm_state
XMO_WCOUNT      equ xm_wcount - xm_state
XMO_SQT         equ xm_sqt - xm_state
XMO_FIN         equ xm_fin - xm_state
XMO_EV          equ xm_ev - xm_state

; a record of xa_recs (16 bytes): +0 present (1 an opponent, 2 the attract rider),
; +1 skill, +2 the engine is being cooled, +3 the lane it steers for, +4 steps to the
; next lane look-ahead, +5 respawn cool-down, +6 respawns so far, +7 rider colour
XA_REC_SZ       equ 16
XA_LOOK         equ 10                  ; columns the lane look-ahead reads
XA_LANE_EVERY   equ 12                  ; steps between two look-aheads

; -----------------------------------------------------------------------------
; xa_init - fresh opponents on the start line, then a fresh player.  The live block
; ends up the player's, as after xm_reset.  With no opponents this is xm_reset.
; -----------------------------------------------------------------------------
xa_init:
    SAVE
    push ds
    pop es
    cld
    mov word [xa_sp], xa_st
    mov word [xa_rp], xa_recs
    mov byte [xa_i], 0
    ; the records: all absent, then the ones in play
    mov di, xa_recs
    mov cx, 4 * XA_REC_SZ / 2
    xor ax, ax
    rep stosw
    mov di, xb_bikes + XB_REC_AI * XB_BIKE_SIZE
    mov cx, XB_NAI_MAX * XB_BIKE_SIZE / 2
    rep stosw
.ai:
    mov al, [xa_i]
    cmp al, [xb_nai]
    jae .player
    call xm_reset
    xor bx, bx
    mov bl, [xa_i]
    mov al, [xa_lanes + bx]
    mov [xm_lane], al
    mov al, bl
    shl al, 1
    mov [xm_stepn], al              ; the wheels of the three do not turn in step
    mov si, [xa_rp]
    mov byte [si], 1
    mov al, [xa_skills + bx]
    mov [si + 1], al
    mov byte [si + 3], 0
    mov al, [xa_lanes + bx]
    mov [si + 3], al
    mov byte [si + 4], 0
    mov byte [si + 5], 0
    mov byte [si + 6], 0
    mov al, [xa_colours + bx]
    mov [si + 7], al
    mov al, [xa_lanes + bx]
    mov [si + 9], al
    mov si, xm_state
    mov di, [xa_sp]
    mov cx, XM_STATE_SZ / 2
    rep movsw
    add word [xa_sp], XM_STATE_SZ
    add word [xa_rp], XA_REC_SZ
    inc byte [xa_i]
    jmp .ai
.player:
    ; the attract rider's own record (index 3): the player driven by the same brain
    mov byte [xa_recs + 3 * XA_REC_SZ], 0
    cmp byte [xb_attract], 0
    je .go
    mov byte [xa_recs + 3 * XA_REC_SZ], 2
    mov byte [xa_recs + 3 * XA_REC_SZ + 1], 3
    mov byte [xa_recs + 3 * XA_REC_SZ + 9], 26
.go:
    call xm_reset
    RESTORE
    ret

xa_lanes:   db 14, 38, 50
xa_skills:  db 3, 2, 1
xa_colours: db 13, 12, 11               ; palette slots: blue, orange, yellow

; -----------------------------------------------------------------------------
; xa_frame - the opponents' steps for a frame in which the player made [xb_ksteps]
; -----------------------------------------------------------------------------
xa_frame:
    SAVE
    push ds
    pop es
    cld
    mov si, xm_state
    mov di, xa_ps
    mov cx, XM_STATE_SZ / 2
    rep movsw                       ; the player's block aside
    mov ax, [xm_pos + 1]
    mov [xa_ppx], ax
    mov al, [xm_fin]
    mov [xa_pfin], al
    ; the player's screen column as xm_bikes places it: BIKE_X + max(0, px - 8 x Smax)
    mov bx, [xb_Smax]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    sub ax, bx
    jg .ex
    xor ax, ax
.ex:
    add ax, XP_BIKE_X
    mov [xa_pscr], ax
    mov word [xa_sp], xa_st
    mov word [xa_rp], xa_recs
    mov byte [xa_i], 0
.ai:
    call xb_clk_sample              ; an opponent's steps are ~10-30k clocks
    mov al, [xa_i]
    cmp al, [xb_nai]
    jae .done
    mov si, [xa_rp]
    cmp byte [si], 0
    je .next
    mov [xa_cur], si
    mov si, [xa_sp]
    mov di, xm_state
    mov cx, XM_STATE_SZ / 2
    rep movsw                       ; this opponent is the live rider
    mov si, [xa_cur]
    cmp byte [si + 5], 0
    je .nocd
    dec byte [si + 5]
.nocd:
    mov cx, [xb_ksteps]
    jcxz .rec
.st:
    push cx
    call xa_input
    mov [xm_inp], al
    call xm_step
    pop cx
    loop .st
    call xa_respawn
.rec:
    call xa_record
    push ds
    pop es
    mov si, xm_state
    mov di, [xa_sp]
    mov cx, XM_STATE_SZ / 2
    rep movsw                       ; ...and is put away
.next:
    add word [xa_sp], XM_STATE_SZ
    add word [xa_rp], XA_REC_SZ
    inc byte [xa_i]
    jmp .ai
.done:
    push ds
    pop es
    mov si, xa_ps
    mov di, xm_state
    mov cx, XM_STATE_SZ / 2
    rep movsw                       ; the player's block back
    call xa_collide
    RESTORE
    ret

; -----------------------------------------------------------------------------
; xa_input - the input byte of the live rider (record [xa_cur])
; -----------------------------------------------------------------------------
xa_input:
    mov si, [xa_cur]
    test byte [xm_stepn], 3
    jnz .cached
    mov ax, [xm_pos + 1]
    sub ax, [xa_ppx]                ; how far ahead of the player this rider is
    cmp byte [si], 2
    jne .have
    xor ax, ax                      ; the attract rider IS the player: no band
.have:
    mov [xa_d], ax
    mov bl, INP_A                   ; the throttle is always open
    mov ax, [xm_temp]
    cmp ax, XP_TEMP_MAX - 3 * 256
    jae .cool
    cmp ax, XP_TEMP_A + 256
    jb .heat
    cmp byte [si + 2], 0
    jne .cool
.heat:
    mov byte [si + 2], 0
    mov ax, [xa_d]
    cmp ax, XA_RUBBER
    jg .kind                        ; far ahead of the player: no turbo
    cmp ax, -XA_CATCHUP
    jl .turbo                       ; well behind: turbo whatever the skill
    mov al, [si + 1]
    cmp al, 3
    jae .turbo
    mov ah, [xm_stepn]
    and ah, 3
    cmp al, 2
    jne .s1
    or ah, ah
    jnz .turbo                      ; skill 2: three steps in four
    jmp .kind
.s1:
    or ah, ah
    jnz .kind                       ; skill 1: one in four
.turbo:
    or bl, INP_B
    jmp .kind
.cool:
    mov byte [si + 2], 1
.kind:
    mov [si + 8], bl
    jmp .mode
.cached:
    mov bl, [si + 8]
.mode:
    mov al, [xm_mode]
    cmp al, XM_AIR
    je .air
    cmp al, XM_RIDE
    jne .done
    ; ---- riding: the lane, looked ahead every 12 steps -----------------------------
    dec byte [si + 4]
    jns .steer
    mov byte [si + 4], XA_LANE_EVERY - 1
    push bx
    call xa_lane                    ; sets [si + 3]
    pop bx
    mov si, [xa_cur]
.steer:
    mov al, [xm_lane]
    cmp al, [si + 3]
    je .done
    jb .down
    or bl, INP_U
    jmp .done
.down:
    or bl, INP_D
    jmp .done
.air:
    mov al, [xm_pitch]              ; level the bike for the landing
    or al, al
    jg .nose
    jl .tail
    jmp .done
.nose:
    or bl, INP_R
    jmp .done
.tail:
    or bl, INP_L
.done:
    mov al, bl
    ret

; the lane centre with the least trouble in the next XA_LOOK columns -> [si + 3].  The
; reference rider's rule: a little stubbornness (the lane it is in is a point
; cheaper), a lane a lane's width away a point dearer.  The trouble of a column in
; a lane is a table (xa_tables) indexed by the column's id: 1 for rough, mud and a
; low hurdle, 2 for a high one, doubled here; all four lanes are summed in ONE pass
; over the columns (~165 clocks a column: the first version asked exb_ccls for each
; lane in turn, 300 a column a lane, and was 26k clocks a frame with three riders).
xa_lane:
    mov si, [xa_cur]
    mov al, [si + 9]
    mov [xa_homel], al
    mov si, [xm_col]
    xor dx, dx
    xor ax, ax
    mov bp, XA_LOOK
.k:
    inc si
    cmp si, [xb_ncols]
    jae .sums
    mov bl, [xb_cid + si]
    xor bh, bh
    add dl, [xa_hz0 + bx]
    add dh, [xa_hz1 + bx]
    add al, [xa_hz2 + bx]
    add ah, [xa_hz3 + bx]
    dec bp
    jnz .k
.sums:
    mov [xa_sum], dl
    mov [xa_sum + 1], dh
    mov [xa_sum + 2], al
    mov [xa_sum + 3], ah
    ; the lane it is in now: the nearest centre
    mov al, [xm_lane]
    xor bx, bx
    cmp al, 20
    jb .h
    inc bx
    cmp al, 32
    jb .h
    inc bx
    cmp al, 44
    jb .h
    inc bx
.h:
    mov dl, [xa_sum + bx]
    add dl, dl
    dec dl                          ; DL = the best cost so far: its own lane's, less one
    mov dh, [xa_centres + bx]       ; DH = the lane it steers for
    xor cx, cx
.c:
    mov bx, cx
    mov al, [xa_sum + bx]
    add al, al
    mov bl, [xa_centres + bx]       ; BL = this centre's lane number
    mov ah, bl
    sub ah, [xm_lane]
    jns .abs
    neg ah
.abs:
    cmp ah, 24
    jb .dist
    inc al
    cmp ah, 48
    jb .dist
    inc al
.dist:
    cmp bl, [xa_homel]
    je .near
    inc al                          ; not the lane it likes
.near:
    cmp al, dl
    jge .no
    mov dl, al
    mov dh, bl
.no:
    inc cx
    cmp cx, 4
    jb .c
    mov si, [xa_cur]
    mov [si + 3], dh
    ret

xa_centres: db 14, 26, 38, 50

; the trouble tables of the four lane centres' collision rows (5, 3, 2, 0), one
; byte per dictionary column: 1 rough / mud / low hurdle, 2 high hurdle.  Once, at
; the start (xb_race_open): 4 x EXB_NCOLS lookups.
xa_tables:
    SAVE
    xor bx, bx                      ; the dictionary column
.col:
    mov ax, bx
    mov dx, ax
    shl ax, 1
    add ax, dx
    shl ax, 1
    mov si, ax                      ; SI = cid x 6
    xor cx, cx                      ; the lane index
.lane:
    mov di, cx
    mov dl, [xa_rows + di]          ; the lane's collision row
    xor dh, dh
    add dx, si
    mov di, dx
    mov al, [exb_ccls + di]         ; the class of the tile there
    xor ah, ah
    mov di, ax
    mov al, [xa_weights + di]       ; its trouble
    push ax
    mov ax, EXB_NCOLS
    mul cx
    add ax, bx
    mov di, ax                      ; lane x EXB_NCOLS + cid
    pop ax
    mov [xa_hz + di], al
    inc cx
    cmp cx, 4
    jb .lane
    inc bx
    cmp bx, EXB_NCOLS
    jb .col
    RESTORE
    ret

xa_rows: db 5, 3, 2, 0
xa_weights: db 0, 0, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0     ; by EXBC_*: none, plain, ROUGH, MUD, HLO, HHI, ...

; -----------------------------------------------------------------------------
; xa_respawn - the live opponent is a whole screen and more away from the player:
; put it back in the field, from the screen's edge.  Ahead of the player by XA_FAR
; pixels it reappears XA_BEHIND behind (off the left edge), behind by XA_LOST it
; reappears XA_AHEAD ahead (off the right edge).  Not while the player is finished,
; and not twice within XA_SPAWNCOOL steps.
; -----------------------------------------------------------------------------
xa_respawn:
    cmp byte [xa_pfin], 0
    jne .out
    cmp byte [xm_fin], 0            ; a finished opponent stays where it finished
    jne .out
    mov si, [xa_cur]
    cmp byte [si + 5], 0
    jne .out
    mov ax, [xm_pos + 1]
    sub ax, [xa_ppx]
    cmp ax, XA_FAR
    jg .behind
    cmp ax, -XA_LOST
    jge .out
    mov ax, [xa_ppx]
    add ax, [xa_ahead]
    jmp .place
.behind:
    mov ax, [xa_ppx]
    sub ax, XA_BEHIND
    jns .place
    xor ax, ax
.place:
    push ax
    call xa_rng
    and al, 3
    xor bx, bx
    mov bl, al
    mov al, [xa_centres + bx]
    mov [xm_lane], al
    pop ax
    call xa_place
    mov si, [xa_cur]
    inc byte [si + 6]
    mov byte [si + 5], XA_SPAWNCOOL
.out:
    ret

; AX = a pixel position: the live rider stands there, riding, at the player's speed
; (a fresh engine, no script running, nothing of the ground under it remembered).
; The column moves on to a plain one, and the trigger list to the first script the
; rider has not passed.
xa_place:
    mov bx, [xm_posmax + 1]
    cmp ax, bx
    jbe .lim
    mov ax, bx
.lim:
    mov cl, 3
    mov bx, ax
    shr bx, cl
    add bx, XP_COL_AHEAD             ; the column
    mov dx, 24
.plain:
    cmp bx, [xb_ncols]
    jae .at
    cmp byte [xb_cid + bx], EXBCOL_PLAIN_C
    jbe .at
    inc bx
    dec dx
    jnz .plain
.at:
    mov ax, bx
    sub ax, XP_COL_AHEAD
    mov cl, 3
    shl ax, cl
    cmp ax, [xm_posmax + 1]
    jbe .px
    mov ax, [xm_posmax + 1]
.px:
    mov [xm_pos + 1], ax
    mov byte [xm_pos], 0
    mov byte [xm_pos + 3], 0
    mov [xm_col], bx
    mov [xm_lastcol], bx
    xor ax, ax
    mov [xm_A], ax
    mov [xm_gh], ax
    mov [xm_th], ax
    mov [xm_vy], ax
    mov [xm_vg], ax
    mov [xm_scr], ax
    mov [xm_mtimer], ax
    mov [xm_sc], al
    mov [xm_pitch], al
    mov [xm_ptimer], al
    mov [xm_wcount], al
    mov [xm_sqt], al
    mov [xm_bounced], al
    mov [xm_ldir], al
    mov [xm_fin], al
    mov [xm_sdefer], al
    mov byte [xm_mode], XM_RIDE
    mov word [xm_temp], XP_TEMP_MIN
    mov word [xm_lastfire], 0ffffh
    mov ax, [xa_ps + xm_speed - xm_state]
    cmp ax, 0x0200
    jae .sp
    mov ax, 0x0200                  ; a stopped player must not leave an opponent standing
.sp:
    mov [xm_speed], ax
    ; the lap, and the trigger list from the first script ahead of this column
    cmp bx, [xm_lap2col]
    jae .lap2
    mov byte [xm_lap], 1
    mov si, [xm_trig1]
    mov ax, EXB_RUNWAY
    jmp .tb
.lap2:
    mov byte [xm_lap], 2
    mov si, [xm_trig2]
    mov ax, [xm_lap2col]
.tb:
    mov [xm_tbase], ax
.scan:
    mov ax, [si]
    cmp ax, 0ffffh
    je .set
    add ax, [xm_tbase]
    cmp ax, bx
    ja .set                         ; a script at a later column: that is the next
    add si, 4                       ; (one AT this column would fire next step: skipped)
    jmp .scan
.set:
    mov [xm_trigp], si
    ret

; a 16-bit LFSR (the opponents' only source of chance): AL = its low byte
xa_rng:
    mov ax, [xb_rng]
    shr ax, 1
    jnc .n
    xor ax, 0b400h
.n:
    mov [xb_rng], ax
    ret

; -----------------------------------------------------------------------------
; xa_record - the live opponent's bike record: on the screen by its distance from
; the player, or absent when its 24-pixel box is not wholly there
; -----------------------------------------------------------------------------
xa_record:
    xor bx, bx
    mov bl, [xa_i]
    mov cl, 3
    shl bx, cl
    add bx, xb_bikes + XB_REC_AI * XB_BIKE_SIZE
    mov ax, [xm_pos + 1]
    sub ax, [xa_ppx]
    add ax, [xa_pscr]
    mov dx, ax
    add dx, [xb_xlo]
    cmp dx, [xb_xspan]
    jae .hide                       ; (a negative x is a huge unsigned one; the CGA engine shows the
    and ax, 0fffeh                  ; bikes that hang XB_EDGE pixels off either side, the VGA only whole ones)
    mov dx, ax
    push bx
    xor bx, bx
    mov bl, [xm_lane]
    add bx, XP_FEET0 - 24
    mov al, [xm_A + 1]
    xor ah, ah
    sub bx, ax                      ; BX = the box top
    call xm_pose                    ; AL = the pose (clobbers AX, CL)
    mov cx, bx
    pop bx
    mov byte [bx], 1
    mov [bx + 1], al
    mov [bx + 2], dx
    mov [bx + 4], cx
    mov si, [xa_cur]
    mov al, [si + 7]
    mov [bx + 6], al
    ret
.hide:
    mov byte [bx], 0
    ret

; -----------------------------------------------------------------------------
; xa_collide - riders that touch: within XA_HITX pixels along, XA_HITL lanes across
; and XA_HITH of height.  The rear rider loses a quarter of its speed and, when it
; closes faster than XA_HARD, crashes; the front one is pushed on a little.  Once a
; frame, on whatever the frames' steps left: bikes move at most 5 pixels a step, so
; a touch is seen within a frame or two.
; -----------------------------------------------------------------------------
xa_collide:
    cmp byte [xb_nai], 0
    je .out
    SAVE
    ; the blocks in play: the player's (the live one), then each opponent's
    mov word [xa_bases], xm_state
    xor bx, bx
    mov bl, [xb_nai]
    mov cx, bx
    mov di, xa_bases + 2
    mov ax, xa_st
.b:
    mov [di], ax
    add di, 2
    add ax, XM_STATE_SZ
    loop .b
    inc bx
    mov [xa_nb], bl
    xor bp, bp                      ; the first rider's index * 2
.i:
    mov ax, bp
    shr ax, 1
    inc ax
    cmp al, [xa_nb]
    jae .end
    mov di, bp
    add di, 2
.j:
    mov ax, di
    shr ax, 1
    cmp al, [xa_nb]
    jae .nexti
    mov si, [ds:xa_bases + bp]
    mov bx, [xa_bases + di]
    push bp
    push di
    call xa_pair                    ; SI, BX = the two blocks
    pop di
    pop bp
    add di, 2
    jmp .j
.nexti:
    add bp, 2
    jmp .i
.end:
    RESTORE
.out:
    ret

; SI, BX = two state blocks.  The cheap, usually-failing tests first (a pair is nearly
; always far apart along the track): ~110 clocks a pair that misses.
xa_pair:
    mov ax, [si + XMO_POS + 1]
    sub ax, [bx + XMO_POS + 1]      ; AX = si's px - bx's px
    mov dx, ax
    jns .a
    neg dx
.a:
    cmp dx, XA_HITX
    jae .no
    mov cl, [si + XMO_LANE]
    sub cl, [bx + XMO_LANE]
    jns .l
    neg cl
.l:
    cmp cl, XA_HITL
    jae .no
    mov dx, [si + XMO_A]
    sub dx, [bx + XMO_A]
    jns .h
    neg dx
.h:
    cmp dx, XA_HITH
    jae .no
    cmp byte [si + XMO_MODE], XM_CRASH
    je .no
    cmp byte [bx + XMO_MODE], XM_CRASH
    je .no
    cmp byte [si + XMO_FIN], 0
    jne .no
    cmp byte [bx + XMO_FIN], 0
    jne .no
    ; a touch.  the rear rider is the one with the smaller position (a tie: the first)
    or ax, ax
    jle .siRear
    xchg si, bx
.siRear:                            ; SI = the rear block, BX = the front one
    mov ax, [si + XMO_SPEED]
    mov dx, ax
    sub dx, [bx + XMO_SPEED]        ; the closing speed
    mov cx, ax
    shr cx, 1
    shr cx, 1
    sub ax, cx
    mov [si + XMO_SPEED], ax
    mov ax, [bx + XMO_SPEED]
    add ax, 16
    cmp ax, XP_SPD_MAX
    jbe .f
    mov ax, XP_SPD_MAX
.f:
    mov [bx + XMO_SPEED], ax
    cmp si, xm_state
    jne .evb
    or byte [xm_ev], XEV_BUMP
.evb:
    cmp bx, xm_state
    jne .hard
    or byte [xm_ev], XEV_BUMP
.hard:
    cmp dx, XA_HARD
    jle .no
    cmp byte [si + XMO_MODE], XM_RIDE
    jne .no
    ; the rear bike goes down
    mov byte [si + XMO_MODE], XM_CRASH
    mov word [si + XMO_MTIMER], XP_CRASH_STEPS
    mov ax, [si + XMO_GH]
    mov [si + XMO_A], ax
    xor ax, ax
    mov [si + XMO_VY], ax
    mov [si + XMO_VG], ax
    mov [si + XMO_PITCH], al
    mov [si + XMO_WCOUNT], al
    mov [si + XMO_SQT], al
    or byte [si + XMO_EV], XEV_CRASH
.no:
    ret
