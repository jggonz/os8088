; The rider simulation (SPEC.md 102.3).  One STEP is one 60.1 Hz tick of the game;
; the frame loop (game.inc) runs floor(1.1003 x sub-ticks) of them a frame.  This
; file is tools/exbsim.py's `Sim` written in 8086: the same order of operations, the
; same integer arithmetic, so tests/excitebike_ref.py can compare the two step for
; step (every record of xb_ttrace equals Sim.rec()).  When one changes, the other
; does, in the same commit, and const.inc's XP_ block is the numbers they share.
;
; The state is the `xm_` variables (excitebike.asm).  Units: positions and speeds
; are 8.8 fixed point in PIXELS; heights (xm_A, xm_gh, xm_th) likewise, above the
; lane's ground; xm_temp is the engine temperature x 256.
;
; PRICES (8088, clocks, per step; measured by tests/excitebike_perf.py --lap from
; the labels xm_s0 / xm_s1, which cost the guest nothing): the common ground step
; is ~1.9k - the lane, the class lookup, the heat, the pitch timer, the position and
; the column compare, and a speed update on every fourth step.  A column crossing
; adds ~0.9k (once every 2.3 steps at turbo), a script keyframe ~0.3k, and the
; air ~0.5k.  Nothing multiplies or divides in a step.

; -----------------------------------------------------------------------------
; xm_reset - a fresh rider on the current course (xb_cid / xb_ncols / xb_track_load)
; -----------------------------------------------------------------------------
xm_reset:
    SAVE
    push ds
    pop es
    mov di, xm_state
    mov cx, XM_STATE_SZ
    xor al, al
    cld
    rep stosb
    mov ax, XP_TEMP_MIN
    mov [xm_temp], ax
    xor ax, ax                      ; the game clock starts with the rider
    mov [xb_cs], ax
    mov [xb_ct10], ax
    dec ax
    mov [xb_cslast], ax
    mov byte [xm_lane], 26
    mov byte [xm_lap], 1
    mov byte [xm_cls], EXBC_PLAIN
    mov word [xm_col], XP_COL_AHEAD
    mov word [xm_lastcol], XP_COL_AHEAD
    mov word [xm_tbase], EXB_RUNWAY
    ; the course header: laps and trigger lists.  The second pass round the courses
    ; (xb_flag) rides the harder set on BOTH laps: lap 1 takes lap 2's fields
    call xb_header
    xor si, si
    cmp byte [xb_flag], 0
    je .f0
    mov si, 2
.f0:
    mov ax, [bx + 4 + si]           ; lap 1's length
    add ax, EXB_RUNWAY
    mov [xm_lap2col], ax
    mov ax, [bx + 12 + si]
    mov [xm_trigp], ax              ; lap 1's triggers
    mov [xm_trig1], ax
    mov ax, [bx + 14]
    mov [xm_trig2], ax
    ; the far end of the world: the bike's 24-pixel box must stay on the screen,
    ; so its pixel position stops at 8 x ncols - 112 (x = 296)
    mov ax, [xb_ncols]
    mov cl, 3
    shl ax, cl
    sub ax, 112
    mov [xm_posmax + 1], ax         ; (x << 8)
    mov byte [xm_posmax], 0
    mov byte [xm_posmax + 3], 0
    RESTORE
    ret

; BX = the header of the course [xb_track] (exbtracks.inc's layout; course 6 is the
; custom one xb_custom_load built in xb_chdr)
xb_header:
    xor ah, ah
    mov al, [xb_track]
    mov bx, ax
    shl bx, 1
    cmp al, EXB_NTRACKS
    jae .custom
    mov bx, [exb_tracks + bx]
    ret
.custom:
    mov bx, xb_chdr
    ret

; -----------------------------------------------------------------------------
; xm_ps - AL = the slope pitch of the terrain (sc < 4: sc, else 3 - sc), signed
; -----------------------------------------------------------------------------
xm_ps:
    mov al, [xm_sc]
    cmp al, 4
    jb .out
    neg al
    add al, 3
.out:
    ret

; -----------------------------------------------------------------------------
; xm_getcls - AL = the class of the tile in the bike's lane row of its column
; (exb_ccls: six bytes a dictionary column).  Clobbers BX.
; -----------------------------------------------------------------------------
xm_getcls:
    mov bx, [xm_col]
    mov al, [xb_cid + bx]
    xor ah, ah
    mov bx, ax
    shl ax, 1
    add ax, bx
    shl ax, 1                       ; cid x 6
    mov bl, [xm_lane]
    xor bh, bh
    mov bl, [xm_lrow + bx]          ; the collision row of the lane (xor bh above)
    add bx, ax
    mov al, [exb_ccls + bx]
    ret

; xm_crash - the bike goes down (the same in the air and on the ground)
xm_crash:
    mov byte [xm_mode], XM_CRASH
    mov word [xm_mtimer], XP_CRASH_STEPS
    mov ax, [xm_gh]
    mov [xm_A], ax
    xor ax, ax
    mov [xm_vy], ax
    mov [xm_vg], ax
    mov [xm_pitch], al
    mov [xm_wcount], al
    mov [xm_sqt], al
    or byte [xm_ev], XEV_CRASH
    ret

; CL = a slope code 1..3 -> DX = speed x (1/4, 1/2, 3/4).  Clobbers nothing else.
xm_rate:
    mov dx, [xm_speed]
    cmp cl, 1
    jne .n1
    shr dx, 1
    shr dx, 1
    ret
.n1:
    cmp cl, 2
    jne .n3
    shr dx, 1
    ret
.n3:
    push ax
    mov ax, dx
    shr ax, 1
    shr dx, 1
    shr dx, 1
    add dx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; xm_step - one step, the input in [xm_inp].  Clobbers everything (the caller saves).
; -----------------------------------------------------------------------------
xm_step:
xm_s0:
    mov bx, [xm_stepn]
    mov [xm_n], bl
    inc bx
    mov [xm_stepn], bx
    cmp byte [xm_fin], 0
    je .live
    mov byte [xm_inp], 0            ; a finished rider takes no orders
    jmp .noclk
.live:
    ; the game clock: 1.664 hundredths a step (thousandths counted)
    add word [xb_ct10], XP_CLOCK16
.clk:
    cmp word [xb_ct10], 1000
    jb .noclk
    sub word [xb_ct10], 1000
    cmp word [xb_cs], 59999
    jae .clk
    inc word [xb_cs]
    jmp .clk
.noclk:
    ; ---- lanes: on the ground only -------------------------------------------
    mov al, [xm_mode]
    cmp al, XM_RIDE
    je .lane
    cmp al, XM_STALL
    jne .nolane
.lane:
    mov ah, [xm_inp]
    test ah, INP_U
    jz .notu
    mov al, [xm_lane]
    cmp al, XP_LANE_MIN
    jbe .u1
    dec al
    mov [xm_lane], al
.u1:
    mov byte [xm_ldir], 0ffh
    jmp .nolane
.notu:
    test ah, INP_D
    jz .notd
    mov al, [xm_lane]
    cmp al, XP_LANE_MAX
    jae .d1
    inc al
    mov [xm_lane], al
.d1:
    mov byte [xm_ldir], 1
    jmp .nolane
.notd:
    mov ah, [xm_ldir]
    or ah, ah
    jz .nolane
    mov al, [xm_lane]
    cmp al, 14
    je .stop
    cmp al, 26
    je .stop
    cmp al, 38
    je .stop
    cmp al, 50
    je .stop
    add al, ah
    mov [xm_lane], al
    cmp al, XP_LANE_MIN
    jbe .stop
    cmp al, XP_LANE_MAX
    jb .nolane
.stop:
    mov byte [xm_ldir], 0
.nolane:
    call xm_getcls
    mov [xm_cls], al
    ; ---- engine heat ------------------------------------------------------------
    mov ax, [xm_temp]
    mov bl, [xm_inp]
    cmp byte [xm_mode], XM_STALL
    jne .heat
    cmp ax, XP_TEMP_MIN
    jbe .tdone
    sub ax, XP_COOL
    cmp ax, XP_TEMP_MIN
    jae .tdone
    mov ax, XP_TEMP_MIN
    jmp .tdone
.heat:
    test bl, INP_B
    jz .notb
    cmp byte [xm_mode], XM_AIR
    ja .notb                        ; a crashed bike gets no turbo
    add ax, XP_HEAT_B
    jmp .mud
.notb:
    test bl, INP_A
    jz .cool
    cmp ax, XP_TEMP_A
    jb .up
    je .mud
    sub ax, XP_COOL
    jmp .mud
.up:
    add ax, XP_HEAT_A
    jmp .mud
.cool:
    cmp ax, XP_TEMP_MIN
    jbe .mud
    sub ax, XP_COOL
    cmp ax, XP_TEMP_MIN
    jae .mud
    mov ax, XP_TEMP_MIN
.mud:
    cmp byte [xm_mode], XM_RIDE
    jne .cap
    cmp byte [xm_cls], EXBC_MUD
    jne .cap
    add ax, XP_HEAT_MUD
.cap:
    cmp ax, XP_TEMP_MAX
    jb .tdone
    mov ax, XP_TEMP_MAX
.tdone:
    mov [xm_temp], ax
    cmp byte [xm_mode], XM_RIDE
    jne .nooh
    cmp ax, XP_TEMP_MAX
    jb .nooh
    mov byte [xm_mode], XM_STALL
    mov word [xm_mtimer], XP_STALL_STEPS
    mov word [xm_vg], 0             ; a stalled bike must not take off at stall end
    or byte [xm_ev], XEV_OVERHEAT
.nooh:
    ; ---- speed, every fourth step -------------------------------------------------
    test byte [xm_n], 3
    jnz .nospeed
    mov al, [xm_mode]
    cmp al, XM_RIDE
    jne .s1
    cmp byte [xm_fin], 0
    je .ride
    jmp .stalldec
.s1:
    cmp al, XM_CRASH
    je .crashdec
    cmp al, XM_STALL
    je .stalldec
    cmp byte [xm_fin], 0
    jne .stalldec
    mov ah, [xm_inp]                ; in the air: Right holds the speed, Left sheds it
    xor dx, dx
    test ah, INP_R
    jnz .dec
    mov dx, XP_DEC_AIR
    test ah, INP_L
    jz .dec
    mov dx, XP_DEC_AIR_L
    jmp .dec
.ride:
    mov ah, [xm_inp]
    mov cx, XP_ACC_A
    xor bx, bx
    test ah, INP_B
    jz .nob
    mov bx, XP_SPD_B
    mov cx, XP_ACC_B
    jmp .capd
.nob:
    test ah, INP_A
    jz .capd
    mov bx, XP_SPD_A
.capd:
    mov dx, XP_DEC
    mov al, [xm_cls]
    cmp al, EXBC_MUD
    jne .nm
    cmp bx, XP_SPD_MUD
    jbe .mm
    mov bx, XP_SPD_MUD
.mm:
    mov dx, XP_DEC_MUD
    jmp .go
.nm:
    cmp al, EXBC_ROUGH
    jne .go
    cmp bx, XP_SPD_ROUGH
    jbe .rr
    mov bx, XP_SPD_ROUGH
.rr:
    mov dx, XP_DEC_ROUGH
.go:
    mov ax, [xm_speed]
    cmp ax, bx
    jae .notbelow
    add ax, cx
    cmp ax, bx
    jbe .st
    mov ax, bx
    jmp .st
.notbelow:
    je .nospeed
    sub ax, dx
    jb .tocap
    cmp ax, bx
    jae .st
.tocap:
    mov ax, bx
.st:
    mov [xm_speed], ax
    jmp .nospeed
.crashdec:
    mov dx, XP_DEC_CRASH
    jmp .dec
.stalldec:
    mov dx, XP_DEC_STALL
.dec:
    mov ax, [xm_speed]
    sub ax, dx
    jae .st
    xor ax, ax
    jmp .st
.nospeed:
    ; ---- pitch ----------------------------------------------------------------------
    cmp byte [xm_mode], XM_AIR
    ja .nopitch
    cmp byte [xm_ptimer], 0
    jne .ptdec
    mov ah, [xm_inp]
    mov bl, [xm_pitch]
    cmp byte [xm_mode], XM_AIR
    je .pair
    mov byte [xm_ptimer], XP_PT_GROUND
    call xm_ps
    test ah, INP_L
    jz .noL
    add al, XP_PITCH_MAX            ; pitch < ps + 4 ?
    cmp bl, al
    jge .pdone
    inc bl
    jmp .pstore
.noL:
    cmp bl, al
    jg .pdown
    jl .pup
    jmp .pdone
.pdown:
    dec bl
    jmp .pstore
.pup:
    inc bl
    jmp .pstore
.pair:
    mov byte [xm_ptimer], XP_PT_AIR
    test ah, INP_L
    jz .noaL
    cmp bl, XP_PITCH_MAX
    jge .pdone
    inc bl
    jmp .pstore
.noaL:
    test ah, INP_R
    jz .pdone
    cmp bl, XP_PITCH_MIN
    jle .pdone
    dec bl
.pstore:
    mov [xm_pitch], bl
    jmp .pdone
.ptdec:
    dec byte [xm_ptimer]
.pdone:
    cmp byte [xm_mode], XM_RIDE
    jne .nopitch
    call xm_ps
    mov ah, [xm_pitch]
    sub ah, al
    cmp ah, XP_PITCH_MAX
    jl .nowh
    inc byte [xm_wcount]
    cmp byte [xm_wcount], XP_WHEELIE
    jb .nopitch
    call xm_crash
    jmp .nopitch
.nowh:
    mov byte [xm_wcount], 0
.nopitch:
    ; ---- the world moves ------------------------------------------------------------
    mov ax, [xm_speed]
    add [xm_pos], ax
    adc word [xm_pos + 2], 0
    mov ax, [xm_pos + 2]
    cmp ax, [xm_posmax + 2]
    jb .posok
    ja .clamp
    mov ax, [xm_pos]
    cmp ax, [xm_posmax]
    jbe .posok
.clamp:
    mov ax, [xm_posmax]
    mov [xm_pos], ax
    mov ax, [xm_posmax + 2]
    mov [xm_pos + 2], ax
    mov word [xm_speed], 0
.posok:
    mov ax, [xm_pos + 1]            ; pixels: (pos >> 8), a word at a byte offset
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add ax, XP_COL_AHEAD
    mov [xm_col], ax
    cmp ax, [xm_lastcol]
    je .nocol
    mov [xm_lastcol], ax
    call xm_column
.nocol:
    ; ---- vertical -------------------------------------------------------------------
    mov al, [xm_mode]
    cmp al, XM_RIDE
    je .ground
    cmp al, XM_AIR
    jne .notair
    call xm_air
    jmp .notair
.ground:
    call xm_ground
.notair:
    ; ---- timers -------------------------------------------------------------------
    mov al, [xm_mode]
    cmp al, XM_CRASH
    jne .t1
    dec word [xm_mtimer]
    jnz .fin
    mov byte [xm_mode], XM_RIDE
    xor ax, ax
    mov [xm_speed], ax
    mov [xm_pitch], al
    mov [xm_ptimer], al
    mov [xm_sqt], al
    jmp .fin
.t1:
    cmp al, XM_STALL
    jne .t2
    dec word [xm_mtimer]
    jnz .fin
    mov byte [xm_mode], XM_RIDE
    mov word [xm_temp], XP_TEMP_MIN
    jmp .fin
.t2:
    cmp al, XM_RIDE
    jne .fin
    cmp byte [xm_sqt], 0
    je .fin
    dec byte [xm_sqt]
.fin:
    ; ---- the finish ------------------------------------------------------------------
    cmp byte [xm_fin], 0
    jne .done
    cmp byte [xm_lap], 2
    jne .done
    mov ax, [xb_ncols]
    sub ax, XP_FINISH_BACK
    cmp [xm_col], ax
    jb .done
    mov byte [xm_fin], 1
    or byte [xm_ev], XEV_FINISH
.done:
xm_s1:
    ret

; -----------------------------------------------------------------------------
; xm_column - the bike's column changed: lap, script triggers, hazards, keyframes
; -----------------------------------------------------------------------------
xm_column:
    cmp byte [xm_lap], 1
    jne .nolap
    mov ax, [xm_col]
    cmp ax, [xm_lap2col]
    jb .nolap
    mov byte [xm_lap], 2
    mov ax, [xm_trig2]
    mov [xm_trigp], ax
    mov ax, [xm_lap2col]
    mov [xm_tbase], ax              ; lap 2's triggers count from its first column
    or byte [xm_ev], XEV_LAP
.nolap:
    ; ---- script triggers: (column, id) pairs, lap relative ---------------------
.trig:
    mov si, [xm_trigp]
    mov ax, [si]
    cmp ax, 0ffffh
    je .notrig
    add ax, [xm_tbase]
    cmp ax, [xm_col]
    jne .notrig
    mov bx, [si + 2]
    shl bx, 1
    mov ax, [exb_scripts + bx - 2]
    mov [xm_scr], ax
    mov ax, [xm_col]
    mov [xm_sbase], ax
    mov byte [xm_sdefer], 0
    mov word [xm_lastfire], 0ffffh
    add word [xm_trigp], 4
    jmp .trig
.notrig:
    call xm_getcls
    mov [xm_cls], al
    ; ---- hazards ----------------------------------------------------------------------
    cmp byte [xm_mode], XM_AIR
    ja .script
    cmp byte [xm_fin], 0
    jne .script
    cmp al, EXBC_HURDLE_LO
    jne .h1
    cmp byte [xm_mode], XM_RIDE
    jne .script
    cmp word [xm_A], XP_HLO_CLEAR
    jge .script
    call xm_ps
    mov ah, [xm_pitch]
    sub ah, al
    cmp ah, 2
    jge .script
    mov ax, [xm_speed]
    shr ax, 1
    sub [xm_speed], ax
    or byte [xm_ev], XEV_BUMP
    jmp .script
.h1:
    cmp al, EXBC_HURDLE_HI
    jne .h2
    cmp word [xm_A], XP_HHI_CLEAR
    jge .script
    cmp byte [xm_mode], XM_RIDE
    jne .hicrash
    call xm_ps
    mov ah, [xm_pitch]
    sub ah, al
    cmp ah, 3
    jge .script
.hicrash:
    call xm_crash
    jmp .script
.h2:
    cmp al, EXBC_ARROW
    jne .h3
    cmp word [xm_temp], XP_TEMP_MIN
    jbe .script
    mov word [xm_temp], XP_TEMP_MIN
    jmp .script
.h3:
    cmp al, EXBC_KICKER
    jne .script
    cmp byte [xm_mode], XM_RIDE
    jne .script
    mov ax, [xm_speed]
    add ax, XP_KICK
    cmp ax, XP_SPD_MAX
    jbe .ks
    mov ax, XP_SPD_MAX
.ks:
    mov [xm_speed], ax
    ; ---- the running script: keyframes (x, code) at or before this column ----------
.script:
    mov si, [xm_scr]
    or si, si
    jz .out
    mov dx, [xm_col]
    sub dx, [xm_sbase]              ; the offset into the piece
.kf:
    mov al, [si]
    cmp al, 0ffh
    jne .k
    mov ax, [xm_lastfire]           ; the end: reset the ground unless a keyframe fired now
    cmp ax, [xm_col]
    je .save
    xor ax, ax
    mov [xm_th], ax
    mov [xm_sc], al
    mov [xm_scr], ax
    ret
.k:
    xor ah, ah
    mov bl, [xm_sdefer]
    xor bh, bh
    add ax, bx
    cmp ax, dx
    ja .save
    mov bl, [si + 1]
    add si, 2
    mov ax, [xm_col]
    mov [xm_lastfire], ax
    mov al, bl
    and al, 63
    mov cl, 6
    shr bl, cl
    jz .ang
    dec bl
    jz .defer
    xor ah, ah                      ; H n: the target ground height, n x 2 pixels
    mov cl, 9
    shl ax, cl
    mov [xm_th], ax
    jmp .kf
.ang:
    mov [xm_sc], al
    jmp .kf
.defer:
    add [xm_sdefer], al
.save:
    mov [xm_scr], si
.out:
    ret

; -----------------------------------------------------------------------------
; xm_ground - riding: the height follows the terrain; a drop after a slope is a take-off
; -----------------------------------------------------------------------------
xm_ground:
    mov ax, [xm_gh]
    mov bx, [xm_th]
    cmp ax, bx
    jb .rise
    ja .fall
    jmp .set
.rise:
    mov cl, [xm_sc]
    cmp cl, 1
    jb .flat
    cmp cl, 3
    ja .flat
    call xm_rate
    mov [xm_vg], dx
    add ax, dx
    jmp .rcap
.flat:
    add ax, XP_RISE_FLAT
.rcap:
    cmp ax, bx
    jbe .set
    mov ax, bx
    jmp .set
.fall:
    mov dx, [xm_vg]
    or dx, dx
    jz .nofall
    mov cx, dx                      ; the ground drops away from a slope: the bike keeps its climb
    shr cx, 1
    add cx, dx
    mov [xm_vy], cx
    mov word [xm_vg], 0
    mov [xm_A], ax
    mov [xm_gh], bx
    mov byte [xm_mode], XM_AIR
    mov byte [xm_ptimer], XP_PT_AIR
    mov byte [xm_bounced], 0
    or byte [xm_ev], XEV_TAKEOFF
    ret
.nofall:
    mov cl, [xm_sc]
    cmp cl, 4
    jb .ff
    cmp cl, 6
    ja .ff
    sub cl, 3
    call xm_rate
    jmp .fd
.ff:
    mov dx, XP_FALL_FLAT
.fd:
    sub ax, dx
    jb .tth
    cmp ax, bx
    jae .set
.tth:
    mov ax, bx
.set:
    mov [xm_gh], ax
    mov [xm_A], ax
    ret

; -----------------------------------------------------------------------------
; xm_air - ballistic flight; a descent to the ground is a landing
; -----------------------------------------------------------------------------
xm_air:
    mov ax, [xm_th]
    mov [xm_gh], ax
    mov ax, [xm_A]
    mov bx, [xm_vy]
    add ax, bx
    mov cx, XP_GRAV
    test byte [xm_inp], INP_L
    jz .g
    mov cx, XP_GRAV_L
.g:
    sub bx, cx
    mov [xm_vy], bx
    cmp ax, XP_A_MAX
    jle .nomax
    mov ax, XP_A_MAX
    or bx, bx
    jle .nomax
    mov word [xm_vy], 0
    xor bx, bx
.nomax:
    mov [xm_A], ax
    or bx, bx
    jns .out
    cmp ax, [xm_gh]
    jg .out
    ; ---- the landing: how far the pitch is from the slope decides it -------------------
    call xm_ps
    mov ah, [xm_pitch]
    sub ah, al
    mov al, ah
    jns .mag
    neg al
.mag:
    mov dx, [xm_gh]
    mov [xm_A], dx
    cmp al, 3
    jb .c2
    call xm_crash                   ; three steps of pitch from the slope: down
    ret
.c2:
    cmp al, 2
    jne .ok
    cmp byte [xm_bounced], 0
    jne .ok
    mov ax, [xm_vy]                 ; two: one bounce, half the impact, an eighth of the speed
    neg ax
    shr ax, 1
    mov [xm_vy], ax
    mov byte [xm_bounced], 1
    mov ax, [xm_speed]
    mov cl, 3
    shr ax, cl
    sub [xm_speed], ax
    or byte [xm_ev], XEV_BOUNCE
    ret
.ok:
    mov byte [xm_mode], XM_RIDE
    xor ax, ax
    mov [xm_vy], ax
    mov [xm_vg], ax
    mov byte [xm_sqt], XP_SQUASH
    mov byte [xm_ptimer], XP_PT_GROUND
    or byte [xm_ev], XEV_LAND
.out:
    ret

; -----------------------------------------------------------------------------
; xm_pose - AL = the pose (0..23) of the rider now.  Clobbers AX, CL.
; -----------------------------------------------------------------------------
xm_pose:
    mov al, [xm_mode]
    cmp al, XM_CRASH
    jne .p1
    mov ax, XP_CRASH_STEPS
    sub ax, [xm_mtimer]
    mov cl, 3
    shr ax, cl                      ; the eighth-second of the crash, 0..8
    cmp al, 3
    jae .c2
    add al, 19                      ; tumble_1..3
    ret
.c2:
    cmp al, 5
    jae .c3
    add al, 14                      ; slide_1..2
    ret
.c3:
    sub al, 5
    and al, 1
    add al, 22                      ; walk_1..2
    ret
.p1:
    cmp al, XM_STALL
    jne .p2
    xor al, al
    ret
.p2:
    cmp al, XM_AIR
    jne .ride
    mov al, [xm_pitch]
    cmp al, 2
    jl .a1
    mov al, 13                      ; nose up
    ret
.a1:
    cmp al, -2
    jg .a2
    mov al, 14                      ; nose down
    ret
.a2:
    mov ax, [xm_vy]
    or ax, ax
    jns .a3
    mov ax, [xm_A]
    sub ax, [xm_gh]
    cmp ax, 0a00h
    jge .a3
    mov al, 15                      ; falling, close to the ground: brace
    ret
.a3:
    mov al, 12
    ret
.ride:
    cmp byte [xm_sqt], 0
    je .r1
    mov al, 16                      ; land_squash
    ret
.r1:
    call xm_ps
    mov ah, [xm_pitch]
    sub ah, al                      ; AH = pitch relative to the slope, AL = the slope
    or ah, ah
    jle .r2
    mov al, ah
    inc al                          ; pitch_up_1..4 = 2..5
    ret
.r2:
    or al, al
    jle .r3
    add al, 8                       ; climb_1..3 = 9..11
    ret
.r3:
    jz .r4
    neg al
    add al, 5                       ; pitch_dn_1..3 = 6..8
    ret
.r4:
    mov al, [xm_stepn]
    shr al, 1
    shr al, 1
    and al, 1                       ; level_a / level_b: the wheels turn every 4 steps
    ret

; -----------------------------------------------------------------------------
; xm_bikes - the renderer's records (xb_bikes) from the state: the player, then the
; shadow when the bike is 24 pixels or more off the ground, then the dust puff
; -----------------------------------------------------------------------------
xm_bikes:
    push ds
    pop es
    mov di, xb_bikes
    mov cx, XB_REC_AI * XB_BIKE_SIZE / 2    ; (the opponents' records are xa_record's)
    xor ax, ax
    cld
    rep stosw
    ; x: the fixed screen column, plus the distance the window has stopped short of
    mov ax, [xm_pos + 1]
    mov bx, [xb_Smax]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    sub ax, bx
    jg .ex
    xor ax, ax
.ex:
    add ax, XP_BIKE_X
    and al, 0feh
    mov dx, ax                      ; DX = x
    xor bx, bx
    mov bl, [xm_lane]
    add bx, XP_FEET0 - 24
    mov cx, bx                      ; CX = the box top of a bike standing on the ground
    mov al, [xm_A + 1]              ; A >> 8 (A is never negative between steps)
    xor ah, ah
    sub bx, ax
    push cx
    call xm_pose                    ; (clobbers CL)
    pop cx
    mov [xb_bikes + 1], al
    mov byte [xb_bikes], 1
    mov [xb_bikes + 2], dx
    mov [xb_bikes + 4], bx
    mov byte [xb_bikes + 6], XP_RIDER_SLOT
    ; the CGA redraws every present record on every scrolled frame and a box costs it
    ; 27-29k clocks: the rider alone keeps it on the CRT frame (measured: the shadow and
    ; dust records doubled 1% and 2.6% of the frames), so it draws neither
    cmp byte [xb_cga], 0
    jne .out
    ; the shadow
    cmp byte [xm_mode], XM_AIR
    ja .noshadow
    mov ax, [xm_A]                  ; only 24 pixels or more above the ground: then the shadow's
    sub ax, [xm_gh]                 ; box and the rider's cannot overlap (the CGA composes a box
    cmp ax, XP_SHADOW_MIN           ; from the world alone)
    jl .noshadow
    mov bx, cx
    mov al, [xm_gh + 1]
    xor ah, ah
    sub bx, ax
    mov byte [xb_bikes + 8], 1
    mov byte [xb_bikes + 9], EXB_POSE_SHADOW
    mov [xb_bikes + 10], dx
    mov [xb_bikes + 12], bx
    mov byte [xb_bikes + 14], XP_RIDER_SLOT
.noshadow:
    ; the dust: behind the rear wheel while the bike slides and after a landing
    mov al, [xm_mode]
    cmp al, XM_CRASH
    jne .d1
    mov ax, XP_CRASH_STEPS
    sub ax, [xm_mtimer]
    cmp ax, 40
    jb .dust
    ret
.d1:
    cmp al, XM_RIDE
    jne .out
    cmp byte [xm_sqt], 0
    je .out
.dust:
    push dx
    push cx
    mov ax, [xm_stepn]
    shr ax, 1
    shr ax, 1
    xor dx, dx
    mov bx, 3
    div bx
    add dl, EXB_POSE_DUST
    mov [xb_bikes + 17], dl
    pop cx
    pop dx
    sub dx, 24                      ; a whole box behind the rider: the puff trails the rear wheel
    jns .dx
    xor dx, dx
.dx:
    mov byte [xb_bikes + 16], 1
    mov [xb_bikes + 18], dx
    mov [xb_bikes + 20], cx
    mov byte [xb_bikes + 22], XP_RIDER_SLOT
.out:
    ret

; -----------------------------------------------------------------------------
; xm_trace - the test harness's per-step record (tests/excitebike_ref.py):
; 16 bytes at xb_ttrace + 16 x [xb_tsi], the layout of Sim.rec()
; -----------------------------------------------------------------------------
xm_trace:
    mov bx, [xb_tsi]
    and bx, XB_TSTEPS - 1
    mov cl, 4
    shl bx, cl
    add bx, xb_ttrace
    mov ax, [xm_pos]
    mov [bx], ax
    mov ax, [xm_pos + 2]
    mov [bx + 2], ax
    mov ax, [xm_speed]
    mov [bx + 4], ax
    mov ax, [xm_A]
    mov [bx + 6], ax
    mov ax, [xm_vy]
    mov [bx + 8], ax
    mov ax, [xm_temp]
    mov [bx + 10], ax
    mov al, [xm_mode]
    mov [bx + 12], al
    mov al, [xm_pitch]
    mov [bx + 13], al
    mov al, [xm_lane]
    mov [bx + 14], al
    call xm_pose
    mov [bx + 15], al
    ret

; the collision row of a lane number (lane_row in tools/exbsim.py): 5 below 16, else 5 - (lane - 8) / 8
xm_lrow:
%assign i 0
%rep 64
%if i < 16
    db 5
%elif i < 56
    db 5 - ((i - 8) >> 3)
%else
    db 0
%endif
%assign i i + 1
%endrep
; gauge cells filled for a temperature of 8 + u units (u = 0..24): (u + 4) / 5
xm_gtab:
%assign i 0
%rep 25
    db (i + 4) / 5
%assign i i + 1
%endrep
