; =============================================================================
; os8088 - apps/skies/csflight.inc
;
; THE FLIGHT MODEL (SPEC.md 88.7). Deliberately simple, and stepped ONCE PER
; SYSTEM TICK the way TANK ATTACK steps its world (SPEC.md 85.6): the machine
; draws as fast as it can and the aeroplane flies at one speed on every
; machine. Speeds are 16.8 m/s, positions 16.8 metres in a dword, angles
; 65536 to the turn. Every constant is read out of [cs_plane]'s record.
; =============================================================================

; -----------------------------------------------------------------------------
; cs_runway_build - build the runway from [cs_airport] and put the aeroplane on it
; clobbers everything
; -----------------------------------------------------------------------------
cs_runway_build:
    mov si, [cs_airport]
    mov ax, [si + CSA_HDG]
    call cs_sin
    mov [cs_rwsin], ax
    mov ax, [si + CSA_HDG]
    call cs_cos
    mov [cs_rwcos], ax
    ; --- the four corners: +-hlen along, +-hwid across, turned by the heading
    ; along = (sin, cos), across = (cos, -sin)
    mov si, [cs_airport]
    mov ax, [si + CSA_HLEN]
    mov bx, [cs_rwsin]
    MUL14
    mov [cs_rwax], ax               ; along, x
    mov si, [cs_airport]
    mov ax, [si + CSA_HLEN]
    mov bx, [cs_rwcos]
    MUL14
    mov [cs_rwaz], ax               ; along, z
    mov si, [cs_airport]
    mov ax, [si + CSA_HWID]
    mov bx, [cs_rwcos]
    MUL14
    mov [cs_rwcx], ax               ; across, x
    mov si, [cs_airport]
    mov ax, [si + CSA_HWID]
    mov bx, [cs_rwsin]
    MUL14
    neg ax
    mov [cs_rwcz], ax               ; across, z
    mov di, cs_rwverts
    ; v0 = +along +across, v1 = -along +across, v2 = -along -across,
    ; v3 = +along -across; v4/v5 = the centreline's ends
    mov ax, [cs_rwax]
    add ax, [cs_rwcx]
    mov [di], ax
    mov ax, [cs_rwaz]
    add ax, [cs_rwcz]
    mov [di+2], ax
    mov ax, [cs_rwcx]
    sub ax, [cs_rwax]
    mov [di+4], ax
    mov ax, [cs_rwcz]
    sub ax, [cs_rwaz]
    mov [di+6], ax
    mov ax, [cs_rwax]
    add ax, [cs_rwcx]
    neg ax
    mov [di+8], ax
    mov ax, [cs_rwaz]
    add ax, [cs_rwcz]
    neg ax
    mov [di+10], ax
    mov ax, [cs_rwax]
    sub ax, [cs_rwcx]
    mov [di+12], ax
    mov ax, [cs_rwaz]
    sub ax, [cs_rwcz]
    mov [di+14], ax
    mov ax, [cs_rwax]
    mov [di+16], ax
    mov ax, [cs_rwaz]
    mov [di+18], ax
    neg ax
    mov [di+22], ax
    mov ax, [cs_rwax]
    neg ax
    mov [di+20], ax
    ; --- the model and the object, in bss ------------------------------------
    mov di, cs_rwmodel
    mov byte [di + CSM_TYPE], CSM_FLAT
    mov byte [di + CSM_NV], 6
    mov byte [di + CSM_NF], 1
    mov byte [di + CSM_NE], 4       ; the centreline is cs_rwline's (88.6.2)
    mov byte [di + CSM_EINK], CSI_MARK
    mov si, [cs_airport]
    mov ax, [si + CSA_HLEN]
    add ax, [si + CSA_HWID]
    mov [di + CSM_RAD], ax
    mov word [di + CSM_VERTS], cs_rwverts
    mov word [di + CSM_FACES], cs_f_rwy
    mov word [di + CSM_EDGES], cs_e_rwy
    mov ax, RW_DASHM                ; a stripe in Q15 of the centreline:
    mov dx, 16384                   ; RW_DASHM x 32768 / (2 hlen)
    mul dx
    div word [si + CSA_HLEN]
    mov [cs_rwdu], ax
    mov di, cs_rwobj
    mov word [di + CSO_MODEL], cs_rwmodel
    mov word [di + CSO_FAR], 0
    mov ax, [si + CSA_X]
    mov [di + CSO_X], ax
    mov ax, [si + CSA_Z]
    mov [di + CSO_Z], ax
    mov ax, [si + CSA_ELEV]
    mov [di + CSO_Y], ax
    mov word [di + CSO_RANGE], 16000
    mov word [di + CSO_LOD], 0
    mov ax, [si + CSA_NAME]
    mov [di + CSO_NAME], ax
    mov word [di + CSO_FLAGS], CSO_TERRAIN   ; the runway is the world's
    ret                             ; surface and not a building: Buildings
                                    ; None must not take away the strip you
                                    ; are standing on (SPEC.md 88.13.1)

; -----------------------------------------------------------------------------
; cs_reset - the aeroplane back at the airport's reset point, engine off
; clobbers everything
; -----------------------------------------------------------------------------
cs_reset:
    call cs_skipclr                 ; every object is a stranger again
    mov si, [cs_airport]
    mov ax, [si + CSA_SPAWN]
    mov bx, [cs_rwsin]
    MUL14
    add ax, [si + CSA_X]
    cwd
    mov cl, 8                       ; to 16.8
    call cs_shl32
    mov [cs_px], ax
    mov [cs_px+2], dx
    mov si, [cs_airport]
    mov ax, [si + CSA_SPAWN]
    mov bx, [cs_rwcos]
    MUL14
    add ax, [si + CSA_Z]
    cwd
    call cs_shl32
    mov [cs_pz], ax
    mov [cs_pz+2], dx
    mov si, [cs_airport]
    mov ax, [si + CSA_ELEV]
    cwd
    call cs_shl32
    mov [cs_py], ax
    mov [cs_py+2], dx
    mov ax, [si + CSA_HDG]
    mov [cs_hdg], ax
    xor ax, ax
    mov [cs_pitch], ax
    mov [cs_roll], ax
    mov [cs_rrate], ax              ; ...and the rates a lagging model carries
    mov [cs_prate], ax              ; (88.7.5), which are attitude too
    mov [cs_spd], ax
    mov [cs_vs], ax
    mov [cs_thr], ax
    mov [cs_thrust], ax
    mov [cs_thracc], ax
    mov [cs_crasht], ax
    mov byte [cs_state], CS_ST_GROUND
    mov byte [cs_stall], 0
    mov byte [cs_kbrake], 0         ; ...and the brake is OFF on a fresh
                                    ; runway, being a latch now (88.7.10.1)
    mov byte [cs_msg], CSG_TAKEOFF
    mov si, cs_g_take              ; ...WHICH NAMES THIS AEROPLANE'S ROTATE
    mov di, [cs_plane]              ; SPEED (88.7.9) and not the Cessna's: the
    mov ax, [di + CSP_VROT]         ; jet leaves the ground at 81 knots and
    call cs_gspeed                  ; the prompt used to say 55 at it
    mov byte [cs_inited], 1
    ; --- AN AMPHIBIAN STARTS ON THE WATER (SPEC.md 88.7.7) where the place
    ; has any: on the water strip's own threshold, pointing along it, with
    ; the hull in. Every one of the nine locations has a strip, so this is
    ; where the A5 begins wherever you take it.
    mov byte [cs_onwater], 0
    mov di, [cs_plane]
    test word [di + CSP_FLAGS], CSPF_AMPHIB
    jz .nowater
    mov si, [cs_airport]
    cmp word [si + CSA_WLEN], 0
    je .nowater
    mov ax, [si + CSA_WHDG]
    mov [cs_hdg], ax
    call cs_sin
    mov bx, [si + CSA_WLEN]         ; 60 m in from the threshold, the way
    sub bx, 60                      ; CSA_SPAWN puts it on a runway
    neg bx
    push bx
    MUL14
    add ax, [si + CSA_WX]
    cwd
    call cs_shl32
    mov [cs_px], ax
    mov [cs_px+2], dx
    mov si, [cs_airport]
    mov ax, [si + CSA_WHDG]
    call cs_cos
    pop bx
    MUL14
    add ax, [si + CSA_WZ]
    cwd
    call cs_shl32
    mov [cs_pz], ax
    mov [cs_pz+2], dx
    mov byte [cs_onwater], 1
    mov byte [cs_msg], CSG_TAKEOFF
    ret
.nowater:
    ; --- and a SAILPLANE does not start on the runway (SPEC.md 88.7.6) -----
    ; CSP_LAUNCH is the height the tow releases at, in metres above the
    ; field; zero is every aeroplane with an engine. There is no winch here
    ; and no tug: a launch is a scripted sequence and this is a state, so
    ; what it does is put the aeroplane where a launch would have left it -
    ; over the threshold, at best-glide speed, wings level, already flying.
    mov di, [cs_plane]
    mov ax, [di + CSP_LAUNCH]
    or ax, ax
    jz .down
    add ax, [cs_py+1]               ; above the FIELD and not above the sea
    mov [cs_py+1], ax
    mov byte [cs_state], CS_ST_AIR
    mov byte [cs_msg], CSG_RELEASE
    mov byte [cs_msgt], CS_MSGAGE   ; ...which nothing else would clear (88.7.6.2)
    mov ax, [di + CSP_VSTALL]       ; 1.5 VSTALL, which is about what a tug
    mov bx, ax                      ; tows at and near enough best glide
    shr bx, 1
    shr bx, 1
    add ax, bx
    add ax, bx
    mov [cs_spd], ax
    mov si, cs_g_tow                ; ...and the prompt names the speed the
    call cs_gspeed                  ; tow LEFT IT AT (88.7.9)
.down:
    ret

; -----------------------------------------------------------------------------
; cs_gspeed - the prompt that names a speed, in the aeroplane's own numbers
;             (SPEC.md 88.7.9)
; in:  SI = the sentence's opening; AX = the speed, in cs_spd's units
; clobbers everything. cs_reset is the only caller and clobbers everything too
;
; The knots are cs_k_spd's own conversion, so the sentence and the airspeed
; indicator can never come to disagree about what 42 m/s is.
; -----------------------------------------------------------------------------
cs_gspeed:
    push ax                         ; TWO PANELS, TWO SENTENCES, BOTH COMPOSED
    push si                         ; HERE (SPEC.md 88.15.6.1): a cockpit's
    mov bx, [si + 2]                ; forty cells hold 'FULL THROTTLE, PULL
    mov [cs_gsuf], bx               ; BACK AT 54 KNOTS' and the one-line
                                    ; strip's fourteen hold 'ROTATE 54 KT'.
                                    ; BX AND NOT AX: AX is the speed, and it
                                    ; has to reach the first pass - loading
                                    ; the suffix through it read the SUFFIX'S
                                    ; ADDRESS as a speed and every cockpit
                                    ; prompt said 542 knots (tests/skiesfleet)
    mov si, [si]                    ; Composing BOTH now rather than the live
    mov di, cs_promptb              ; backend's one is what makes it free of
    call cs_gspeed1                 ; ordering: a Mode pick does not clear
    pop si                          ; cs_inited (it is not the aeroplane that
    pop ax                          ; changed), so a prompt composed for the
    mov bx, [si + 6]                ; backend that was running would still be
    mov [cs_gsuf], bx               ; up the next time the other one was
    mov si, [si + 4]                ; entered
    mov di, cs_promptc
cs_gspeed1:                         ; SI = the opening, DI = the buffer
    push ax
    call cs_strcpy                  ; the words, DI left where the NUL goes
    pop ax
    mov bx, 996
    mul bx
    mov ax, dx                      ; AX = knots
    xor cx, cx
    call cs_utoa                    ; ...which preserves DI, so walk it on
.fw:
    cmp byte [di], 0
    je .kt
    inc di
    jmp short .fw
.kt:
    mov si, [cs_gsuf]
    call cs_strcpy
    mov byte [di], 0                ; cs_strcpy copies UP TO the NUL, not it
    ret

; cs_shl32 - DX:AX <<= 8. Clobbers CX.
cs_shl32:
    mov cx, 8
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
    ret

; -----------------------------------------------------------------------------
; cs_step - one tick of the world (SPEC.md 88.7)
; clobbers everything
; -----------------------------------------------------------------------------
cs_step:
    cmp byte [cs_state], CS_ST_CRASH
    jne .live
    dec word [cs_crasht]            ; the picture stays for CS_CRASHT ticks,
    jnz .out                        ; then the runway again
    call cs_reset
.out:
    ret
.live:
    mov di, [cs_plane]
    call cs_msgage                  ; --- an ANNOUNCEMENT expires (88.7.6.2) --
    ; --- the throttle -------------------------------------------------------
    ; NOT ON A GLIDER (88.7.6.1). CSP_THRUST 0 is an aeroplane with no engine,
    ; and its throttle used to open to 100 on the W key like everyone else -
    ; which the ENGINE NOISE is made from, so the Bijave hummed. The panel
    ; hid it (the Bijave's THR item is (0,0)), the model ignored it, and the
    ; only thing that ever read it was the sound.
    cmp word [di + CSP_THRUST], 0
    je .throk
    mov al, [cs_kthr]
    or al, al
    jz .throk
    cbw
    shl ax, 1
    add ax, [cs_thr]
    jns .t0
    xor ax, ax
.t0:
    cmp ax, 100
    jle .t1
    mov ax, 100
.t1:
    mov [cs_thr], ax
.throk:
    ; --- the engine: thrust FOLLOWS the throttle at CSP_SPOOL (88.7.5) -----
    ; Every tick and not only on a key, because a lag has to run while
    ; nothing is being pressed - which is the whole of what a lag is. A
    ; propeller's SPOOL is 0, `sar by 0` is a no-op, and the gap closes
    ; whole: the two aeroplanes that shipped before this are unchanged to
    ; the unit.
    mov ax, [cs_thr]
    mul word [di + CSP_THRUST]      ; AX = the thrust the throttle asks for
    mov bx, 100
    div bx
    mov cl, 8                       ; ...IN 8.8, because a whole unit of
    shl ax, cl                      ; thrust is 19 on the jet and a 32nd of
    sub ax, [cs_thracc]             ; a gap that small is nothing: a lag has
    mov bx, ax                      ; to be finer than the quantity it lags
    mov cl, [di + CSP_SPOOL]
    sar ax, cl                      ; the fraction of the gap this tick owes
    jnz .spool
    mov ax, bx                      ; a gap too small to shift is closed
.spool:                             ; outright, or the engine idles for ever
    add [cs_thracc], ax
    mov ax, [cs_thracc]
    mov cl, 8
    shr ax, cl
    mov [cs_thrust], ax
    ; --- the speed: thrust, less drag, less gravity along the nose ---------
    mov ax, [cs_spd]
    mul ax                          ; DX:AX = v^2 (16.16)
    mov ax, dx                      ; AX = v^2 in whole m^2/s^2
    mul word [di + CSP_DRAGK]
    mov bx, dx                      ; BX = the drag, a tick
    mov ax, [cs_thrust]
    sub ax, bx
    mov cx, ax                      ; CX = thrust - drag
    cmp byte [cs_state], CS_ST_GROUND
    jne .air
    jmp .ground
.air:
    mov ax, [cs_pitch]
    push cx
    push di
    call cs_sin
    pop di
    pop cx
    mov bx, CS_GRAV
    MUL14
    sub cx, ax                      ; less g sin(pitch)
    mov ax, [cs_spd]
    add ax, cx
    jns .s0
    xor ax, ax
.s0:
    mov bx, [di + CSP_VMAX]
    mov cx, bx
    shr cx, 1
    shr cx, 1
    add bx, cx                      ; ...and a dive tops out at 1.25 VMAX
    cmp ax, bx
    jle .s1
    mov ax, bx
.s1:
    mov [cs_spd], ax
    ; --- attitude and the stall: THE PLANE'S OWN (88.7.2), through the near
    ;     proc its record names. DI is the plane, and it comes back ---------
    call [di + CSP_ATT]
    mov di, [cs_plane]
    cmp byte [cs_stall], 0          ; ...and the warning goes when it ends
    jne .warned
    cmp byte [cs_msg], CSG_STALL
    jne .warned
    mov byte [cs_msg], CSG_NONE
.warned:
    ; --- the turn: TURNK sin(roll) a tick, and the rudder ------------------
    mov ax, [cs_roll]
    push di
    call cs_sin
    pop di
    mov bx, [di + CSP_TURNK]
    MUL14
    add [cs_hdg], ax
    mov al, [cs_kyaw]
    cbw
    mov bx, CS_RUDDER
    imul bx
    add [cs_hdg], ax
    ; --- the motion: where the nose points, at the speed -------------------
    mov ax, [cs_pitch]
    push di
    call cs_cos
    pop di
    mov bx, [cs_spd]
    MUL14
    mov [cs_hs], ax                 ; the horizontal component
    mov ax, [cs_pitch]
    push di
    call cs_sin
    pop di
    mov bx, [cs_spd]
    MUL14                           ; ...and the vertical
    cmp byte [cs_stall], 0
    je .vsok
    mov bx, [di + CSP_VSTALL]
    sub bx, [cs_spd]                ; the deficit, and half of it is sink
    sar bx, 1
    sub ax, bx
.vsok:
    ; --- AND THE AIR ITSELF (SPEC.md 88.7.6.3). Into cs_vs and not into the
    ;     altitude, so the VARIOMETER shows it: cs_k_vs reads this word, and
    ;     an instrument that misses the one thing a sailplane is flown on
    ;     would be an odd instrument
    push ax
    push di
    call cs_lift
    mov [cs_airv], ax
    pop di
    pop ax
    add ax, [cs_airv]
    mov [cs_vs], ax
    ; --- crossing INTO it is what you HEAR: leaving is silent, arriving is
    ;     a swoop, which is the cue a variometer's audio actually gives
    mov bl, 0
    mov ax, [cs_airv]
    or ax, ax
    jz .sg
    mov bl, 1
    jns .sg
    mov bl, -1
.sg:
    cmp bl, [cs_airs]
    je .swok
    mov [cs_airs], bl
    or bl, bl
    jz .swok
    cmp byte [cs_sound], 0          ; ...and it is SCHEDULED, so with the sound
    je .swok                        ; off there is nothing to schedule - one
    mov [cs_swdir], bl              ; armed here and left standing is a swoop
    mov byte [cs_swt], CS_SWOOPT    ; that plays when the sound comes back on
.swok:
    call cs_move
    ; --- the ground, and what is standing on it ---------------------------
    ; The wheels are on it when the altitude is BELOW the field, or AT it
    ; (to the metre) and not climbing: a lift-off's first tick is centimetres
    ; up and the whole-metre compare alone put it straight back down.
    mov si, [cs_airport]
    mov ax, [si + CSA_ELEV]
    mov bx, [cs_py+1]               ; the altitude's whole metres
    cmp bx, ax
    jg .aloft
    jl .touch
    cmp word [cs_vs], 0
    jg .aloft
.touch:
    call cs_touch
    jmp short .done
.aloft:
    cmp bx, CS_CEIL
    jle .c1
    mov word [cs_py+1], CS_CEIL     ; the ceiling that keeps a word a word
    mov byte [cs_py], 0
.c1:
    call cs_collide
.done:
    call cs_fence
    ret

    ; ===== on the ground ====================================================
.ground:
    sub cx, [di + CSP_FRICT]        ; rolling friction, and the brakes
    cmp byte [cs_onwater], 0
    je .wheels
    ; A HULL IS A BRAKE UNLESS YOU ARE DRIVING IT (SPEC.md 88.7.7.2). Twice
    ; the rolling friction was the first answer and it is 0.85 m/s^2 on the
    ; A5: 28 seconds and 340 m to come off the step, which the field read as
    ; the water not slowing it at all. Water takes the WHEEL BRAKE'S figure
    ; as well, with no key - but only with the THROTTLE SHUT, because a hull
    ; that drags harder than the engine pushes is an amphibian that cannot
    ; take off, which is what the first build of this was.
    sub cx, [di + CSP_FRICT]
    cmp word [cs_thr], CS_HULLTHR
    jae .g0
    sub cx, [di + CSP_BRAKE]
    jmp short .g0
.wheels:
    cmp byte [cs_kbrake], 0
    je .g0
    sub cx, [di + CSP_BRAKE]
    ; ...AND THE BRAKE CLOSES THE THROTTLE (SPEC.md 88.7.10). The field read
    ; the brakes as not working, and the arithmetic says why: a landing roll
    ; is flown with the throttle where the take-off left it, W having been
    ; held and never let go, so the Cessna's 20 of brake was spending 16 of
    ; itself on its own engine and stopping at a fifth of its rate. A pilot
    ; closes the throttle before touching the brakes; the key that means
    ; STOP does it here.
    mov word [cs_thr], 0
.g0:
    mov ax, [cs_spd]
    add ax, cx
    jns .g1
    xor ax, ax
.g1:
    mov [cs_spd], ax
    mov word [cs_roll], 0
    mov byte [cs_stall], 0
    ; the nose: only above VROT, and only when pulled
    mov bx, [cs_pitch]
    cmp ax, [di + CSP_VROT]
    jb .nodown
    cmp byte [cs_kpitch], 0
    jle .nodown
    add bx, [di + CSP_PITCHR]
    cmp bx, 2730                    ; 15 degrees on the wheels
    jle .np
    mov bx, 2730
.np:
    mov [cs_pitch], bx
    cmp bx, CS_LIFTOFF
    jl .steer
    mov byte [cs_state], CS_ST_AIR  ; and it flies
    mov byte [cs_kbrake], 0         ; ...with the brake let go (88.7.10.1)
    mov byte [cs_msg], CSG_NONE
    jmp short .steer
.nodown:
    sub bx, [di + CSP_PITCHR]
    jns .nd1
    xor bx, bx
.nd1:
    mov [cs_pitch], bx
.steer:
    mov al, [cs_kyaw]               ; the nosewheel: hdg += v x STEERK >> 8
    cbw
    mov bx, [cs_spd]
    mov cl, 7                       ; whole m/s out of 16.7
    shr bx, cl
    shl bx, 1                       ; CS_STEERK = 2
    imul bx
    add [cs_hdg], ax
    mov ax, [cs_spd]
    mov [cs_hs], ax
    mov word [cs_vs], 0
    call cs_move
    mov si, [cs_airport]            ; the wheels stay on the ground
    mov ax, [si + CSA_ELEV]
    mov [cs_py+1], ax
    mov byte [cs_py], 0
    call cs_fence
    ret

; =============================================================================
; THE FLIGHT MODELS (SPEC.md 88.7.2): one proc per aeroplane, named by its
; CSP_ATT, called once a tick in the air with DI = the plane's record. Each
; owns the two attitude axes and the stall - everything else about a tick is
; shared, because thrust, drag, the turn and the motion are the same
; arithmetic whatever is flying.
;
; Out: [cs_stall] set or cleared, [cs_pitch] and [cs_roll] moved. Clobbers
; everything; the caller reloads DI.
; =============================================================================

; -----------------------------------------------------------------------------
; cs_att_trim - A TRAINER (the Cessna): both axes CLAMPED to the record's
;               limits, both returning toward level when the stick is
;               centred, and a stall that drops the nose no further than the
;               pitch limit. You cannot get this aeroplane onto its back.
; -----------------------------------------------------------------------------
cs_att_trim:
    mov al, [cs_kroll]
    cbw
    mov bx, [di + CSP_ROLLR]
    mov cx, [di + CSP_ROLLL]
    mov dx, [di + CSP_MAXROLL]
    mov si, cs_roll
    call cs_axis
    mov di, [cs_plane]
    mov al, [cs_kpitch]
    cbw
    mov bx, [di + CSP_PITCHR]
    mov cx, [di + CSP_PITCHT]
    mov dx, [di + CSP_MAXPITCH]
    mov si, cs_pitch
    call cs_axisp                   ; ...and the ELEVATOR is a body rate
                                    ; (SPEC.md 88.7.8), so it is resolved
                                    ; before the clamp sees it
    mov di, [cs_plane]
    mov byte [cs_stall], 0
    mov ax, [cs_spd]
    cmp ax, [di + CSP_VSTALL]
    jae .out
    mov byte [cs_stall], 1
    mov byte [cs_msg], CSG_STALL
    mov ax, [cs_pitch]
    sub ax, CS_STALLDROP
    mov bx, [di + CSP_MAXPITCH]
    neg bx
    cmp ax, bx
    jge .sd
    mov ax, bx
.sd:
    mov [cs_pitch], ax
.out:
    ret

; -----------------------------------------------------------------------------
; cs_att_free - AN AEROBATIC BIPLANE (the Pitts): NO LIMIT ON EITHER AXIS and
;               no return to level. Both angles are 16-bit with 65,536 to the
;               turn, so an unclamped `add` wraps at 360 degrees for nothing -
;               the aeroplane rolls right round and loops right over, and it
;               stays wherever the stick left it.
;
; NOTHING SPECIAL IS NEEDED AT THE VERTICAL, and that is worth saying because
; the usual Euler dodge - flip the heading and mirror the roll past 90
; degrees - is what you write when the nose vector is built by hand. Here it
; is not: cs_matrix reads sin and cos out of a full-circle table and cs_move
; takes the horizontal component as cos(pitch), which goes NEGATIVE past the
; vertical. That is the correct answer rather than a tolerated one - over the
; top of a loop the nose really does point up and BACK along the heading.
;
; THAT IS A CLAIM ABOUT WHERE THE AEROPLANE GOES, AND IT WAS READ AS ONE
; ABOUT THE STICK. Nothing above says how a key reaches the angles, and this
; proc used to add the elevator straight into [cs_pitch] - a rotation about
; the WORLD's wing axis rather than the aeroplane's. Roll upright after a
; loop and the two point opposite ways, so the stick came back inverted;
; cs_elev is the one line that fixes it (SPEC.md 88.7.8).
;
; The stall drops the nose toward the EARTH and not toward zero pitch, which
; is the same thing only while upright: inverted, the earth is the other way.
; cos(roll) says which, and there is no clamp on the result.
; -----------------------------------------------------------------------------
cs_att_free:
    mov al, [cs_kroll]              ; --- roll: rate x stick, and it stays ---
    cbw
    or ax, ax
    jz .pitch
    imul word [di + CSP_ROLLR]
    mov si, cs_roll                 ; ...eased onto the horizon if this is the
    call cs_ease                    ; last of an approach (88.7.3) - and here
    add [cs_roll], ax               ; INVERTED level is one of them
.pitch:
    mov di, [cs_plane]
    mov al, [cs_kpitch]
    cbw
    or ax, ax
    jz .stall
    imul word [di + CSP_PITCHR]
    call cs_elev                    ; a BODY rate into the world (88.7.8)
    mov si, cs_pitch
    call cs_ease
    add [cs_pitch], ax
.stall:
    mov di, [cs_plane]
    mov byte [cs_stall], 0
    mov ax, [cs_spd]
    cmp ax, [di + CSP_VSTALL]
    jae .out
    mov byte [cs_stall], 1
    mov byte [cs_msg], CSG_STALL
    mov ax, [cs_roll]               ; which way is down from here?
    call cs_cos
    or ax, ax
    mov ax, CS_STALLDROP
    jns .drop                       ; upright: the nose falls, pitch down
    neg ax                          ; on its back: the earth is the other way
.drop:
    sub [cs_pitch], ax
.out:
    ret

; -----------------------------------------------------------------------------
; cs_att_lag - A JET (the Fouga Magister): the stick sets a rate TARGET and
;              the aeroplane eases onto it (SPEC.md 88.7.5)
;
; The two aeroplanes before this are direct-drive - the stick IS the rate, in
; both directions, the same tick. That is right for a Pitts and wrong for two
; tonnes at 190 metres a second, and it is the single most recognisable
; difference between a light aerobat and a jet on a keyboard: this one keeps
; rolling after the key is let go, and takes a beat to start.
;
; IT COMPOSES WITH THE HORIZON CAPTURE (88.7.3) rather than fighting it. The
; step cs_ease shortens is the current rate whatever set it, so the jet still
; lands exactly on the horizon - and because the rate decays with the stick
; centred, it can COAST onto level instead of being flown onto it, which no
; other aeroplane here can do.
;
; No clamp on either axis: a Magister aerobats, and the Patrouille de France
; flew nine of them. The stall is cs_att_free's, jumped into rather than
; copied - the nose drops toward the EARTH and cos(roll) says which way.
; -----------------------------------------------------------------------------
CS_LAGSH equ 2                      ; a quarter of the gap a tick: 90% of the
                                    ; way there in eight, which is half a
                                    ; second of a real second

cs_att_lag:
    test byte [cs_hzhold], 1        ; already on the horizon this frame: the
    jnz .pitch                      ; rest of its ticks stand still (88.7.5.2)
    mov al, [cs_kroll]
    cbw
    imul word [di + CSP_ROLLR]      ; the rate the stick is asking for...
    mov si, cs_rrate
    call cs_lagax                   ; ...ZF when the aeroplane is not turning
    jz .pitch
    mov si, cs_roll
    call cs_ease
    jnc .radd
    or byte [cs_hzhold], 1          ; ARRIVED: the axis holds for the rest of
    cmp byte [cs_kroll], 0          ; this frame (SPEC.md 88.7.5.2)...
    jne .radd                       ; ...and the RATE goes with it only if the
    mov word [cs_rrate], 0          ; stick is CENTRED (88.7.5.2.1): a pilot
.radd:                              ; still holding it means to keep rolling,
    add [cs_roll], ax               ; and rebuilding from zero cost 8 frames
.pitch:
    mov di, [cs_plane]
    test byte [cs_hzhold], 2
    jnz .stall
    mov al, [cs_kpitch]
    cbw
    imul word [di + CSP_PITCHR]
    mov si, cs_prate
    call cs_lagax                   ; ...the aeroplane's own pitch rate, and
    jz .stall                       ; it is a BODY rate like anyone's (88.7.8)
    call cs_elev
    mov si, cs_pitch
    call cs_ease
    jnc .padd
    or byte [cs_hzhold], 2
    cmp byte [cs_kpitch], 0         ; ...the elevator the same (88.7.5.2.1)
    jne .padd
    mov word [cs_prate], 0
.padd:
    add [cs_pitch], ax
.stall:
    mov di, [cs_plane]
    jmp cs_att_free.stall

; cs_lagax - AX = the rate the stick asks for, SI = the aeroplane's current
; rate: close part of the gap. out AX = the new rate, ZF = it is zero.
; Clobbers CX. Preserves BX, DX, DI.
;
; THE TWO SIDES ARE NOT THE SAME LENGTH, and the first build had them equal
; at a quarter of the gap a tick, which made the aeroplane unflyable in a way
; the gate did not look at: a rate decaying by a quarter a tick has THREE
; TIMES ITS CURRENT VALUE still to travel, so releasing the stick at full
; rate carried it 19.6 degrees further. A tap - one tick of key - rolled 19.6
; degrees, and levelling out was impossible because centring the stick at the
; horizon coasted a fifth of a turn past it. Both of the owner's complaints
; are that one number.
;
; So building a rate is slow and losing one is fast, which is also what the
; air does: inertia is what makes a roll take a moment to start, and damping
; is what stops it the moment the ailerons are centred. Three quarters of the
; gap a tick going down leaves a tail of a third of the rate - 2.2 degrees
; from full - so a tap is a nudge and a release lands where it was pointed.
cs_lagax:
    mov cx, ax
    sub cx, [si]                    ; CX = the gap
    or ax, ax
    jnz .build                      ; the stick is asking for a rate...
    mov ax, cx                      ; ...or it is centred, and the rate DIES:
    sar ax, 1                       ; a quarter of the gap is what stays, so
    sar ax, 1                       ; three quarters of it goes this tick
    neg ax
    add ax, cx
    jmp short .step
.build:
    mov ax, cx
    sar ax, 1
    sar ax, 1                       ; CS_LAGSH: a quarter of the gap a tick
.step:
    jnz .add
    mov ax, cx                      ; a gap too small to shift is closed
.add:                               ; outright, or a rate sticks at -1 for ever
    add [si], ax
    mov ax, [si]
    or ax, ax
    ret

; -----------------------------------------------------------------------------
; cs_ease - THE HORIZON CAPTURES THE LAST THREE TICKS (SPEC.md 88.7.3)
; in:  AX = the step this tick, signed (the rate x the stick); SI = the angle
; out: AX = the step to take. Preserves BX, CX, DX, SI, DI.
;
; A key is a stick that is hard over or centred, so an axis moves in whole
; ROLLR/PITCHR quanta and can only STOP where the quanta fall - 10 degrees of
; roll on the Pitts, and cs_steps spends up to three ticks in a frame, so what
; the pilot feels is a 30-degree quantum with level flight not in it. Here the
; last three ticks of an approach are shortened so the angle LANDS on the
; horizon: one tick away take exactly the distance, two take half of what is
; left rounded up, three a third. Four or more is not an approach and the rate
; is untouched, and so is an axis moving AWAY.
;
; THE HORIZON IS A HALF TURN, not zero: wings level and INVERTED level are
; both places the Pitts may settle, and so is the top of a loop. The angles
; wrap at 65,536, so `AND 0x7FFF` against 0x4000 picks the nearer of the two
; bounding multiples in three instructions and needs no case for the wrap.
; -----------------------------------------------------------------------------
cs_ease:
    push bx
    mov bl, CS_HZR                  ; --- WHICH AXIS, and is it already there?
    cmp si, cs_roll
    je .axis
    mov bl, CS_HZP
.axis:
    mov [cs_hzbit], bl
    test [cs_hzhold], bl
    jz .pair
    xor ax, ax                      ; it LANDED earlier in this frame: it
    pop bx                          ; stays until the frame is DRAWN (88.7.3.1)
    clc
    ret
.pair:
    dec bl                          ; --- THE AXIS'S RAMP-OUT PAIR (88.7.3.3):
    shl bl, 1                       ; a cap and its increment, a word each,
    shl bl, 1                       ; at 0 for roll and 4 for pitch
    xor bh, bh
    mov [cs_hzox], bx
.go:
    or ax, ax
    jz .plain                       ; centred: there is no step to shorten
    push cx
    push dx
    mov cx, ax                      ; CX = the step, signed...
    mov dx, ax                      ; ...and its MAGNITUDE, which is this
    or dx, dx                       ; axis's FULL rate: the ramp out needs it
    jns .rate                       ; whichever way this call leaves
    neg dx
.rate:
    mov [cs_eass], dx
    mov ax, [si]
    and ax, 0x7FFF                  ; how far into this half turn we are
    cmp ax, 0x4000
    jb .below
    mov bx, 0x8000                  ; the multiple ABOVE is the near one
    sub bx, ax
    jmp short .dist
.below:
    mov bx, ax                      ; ...or the one BELOW
    neg bx
.dist:
    mov ax, bx                      ; BX = the signed distance to it
    xor ax, cx
    js .out                         ; moving away: not an approach at all
    mov ax, bx
    or ax, ax
    jns .dmag
    neg ax
.dmag:
    jz .out                         ; already on it: leave at the full rate
    mov [cs_easm], ax               ; the distance's MAGNITUDE, parked: the
.smag:                              ; step's sign is the one it wears back
    ; --- THE WINDOW IS A LADDER OF WHOLE FRAMES (SPEC.md 88.7.3.2), and the
    ; distance is spread over the FIRST rung that reaches it - so the landing
    ; is a frame's LAST tick and every frame of the approach travels alike.
    ; BX = the rung in ticks, AX = how far it reaches, DX = a counter.
    mov bl, [cs_tleft]              ; rung one is what is LEFT of this frame
    xor bh, bh
    mov dx, bx
    or dx, dx
    jz .out                         ; no ticks left at all: nothing to spread
    xor ax, ax
.reach:
    add ax, [cs_eass]
    jc .spread                      ; past a word: it certainly reaches
    dec dx
    jnz .reach
.fit:
    cmp [cs_easm], ax
    jbe .spread
    mov dl, [cs_tframe]             ; ...or one WHOLE FRAME further out, up
    xor dh, dh                      ; to CS_EASEF of them
    or dx, dx
    jz .out
    add bx, dx
    cmp bx, CS_EASEN
    ja .out                         ; further than the ladder reaches: a
.wide:                              ; plain full-rate tick
    add ax, [cs_eass]
    jc .spread
    dec dx
    jnz .wide
    jmp short .fit
.spread:
    mov ax, [cs_easm]               ; BX = the ticks to spread it over
    add ax, bx
    dec ax                          ; ...rounded UP, so the last one lands
    xor dx, dx
    div bx
    cmp ax, [cs_easm]
    jb .sign                        ; a plain eased tick
    mov ax, [cs_easm]               ; ...or this one REACHES the horizon
    mov dl, [cs_hzbit]              ; and the axis is HELD there for the rest
    or [cs_hzhold], dl              ; of the frame (88.7.3.1) - which by
                                    ; construction is no ticks at all
    ; --- AND THE WAY OUT MIRRORS THE WAY IN (SPEC.md 88.7.3.3): the cap is
    ; already the rate the approach came down to, so all that is armed here
    ; is how fast it climbs back - a shortfall over 1 << CS_EASEOS ticks
    mov bx, [cs_hzox]
    mov dx, [cs_eass]
    sub dx, [cs_hzo + bx]
    jbe .lsign                      ; came in at the full rate: nothing to do
%rep CS_EASEOS                      ; a shift by CL would spend the STEP'S
    shr dx, 1                       ; SIGN, which is the one thing this proc
%endrep                             ; still needs, so the shift is unrolled
    jnz .larm
    inc dx                          ; ...at least a unit, or it never ends
.larm:
    mov [cs_hzo + bx + 2], dx
.lsign:
    or cx, cx                       ; the distance wears the STEP's sign
    jns .lpos
    neg ax
.lpos:
    pop dx
    pop cx
    pop bx
    stc                             ; CF says the caller ARRIVED on it, which
    ret                             ; a lagging model acts on (88.7.5.2)
.sign:
    mov bx, [cs_hzox]               ; THE WAY OUT STARTS WHERE THE WAY IN
    mov [cs_hzo + bx], ax           ; ENDED (88.7.3.3): this eased step
    or cx, cx                       ; the magnitude wears the step's sign,
    jns .done                       ; which is the distance's - they agree,
    neg ax                          ; or we left at .out
    jmp short .done
.out:
    mov ax, cx                      ; --- A FULL-RATE STEP, capped while the
    mov bx, [cs_hzox]               ; axis is climbing back out (88.7.3.3)
    mov dx, [cs_hzo + bx + 2]
    or dx, dx
    jnz .ramp
    mov dx, [cs_eass]               ; not climbing: the way out would start
    mov [cs_hzo + bx], dx           ; at the full rate, so there is no ramp
    jmp short .done
.ramp:
    add [cs_hzo + bx], dx           ; the cap climbs toward the full rate...
    mov dx, [cs_eass]
    cmp [cs_hzo + bx], dx
    jb .rcap
    mov [cs_hzo + bx], dx           ; ...and stops there, the ramp over
    mov word [cs_hzo + bx + 2], 0
.rcap:
    mov dx, [cs_hzo + bx]
    or ax, ax
    jns .rpos
    neg ax
    cmp ax, dx
    jbe .rneg
    mov ax, dx
.rneg:
    neg ax
    jmp short .done
.rpos:
    cmp ax, dx
    jbe .done
    mov ax, dx
.done:
    pop dx
    pop cx
    pop bx
    clc
    ret
.plain:
    mov bx, [cs_hzox]               ; the stick is CENTRED: the climb is over
    mov word [cs_hzo + bx + 2], 0
    pop bx
    clc
    ret

; cs_axis - one control axis: AX = the key (-1/0/1), BX = the held rate,
; CX = the return rate, DX = the limit, SI = the angle word. Clobbers AX, BX.
; -----------------------------------------------------------------------------
; cs_elev - THE ELEVATOR IS A BODY RATE (SPEC.md 88.7.8)
; in:  AX = this tick's pitch rate about the aeroplane's OWN wing axis
; out: AX = the part of it that turns the world PITCH; the part that turns
;      the HEADING has been added to [cs_hdg] already
; clobbers AX, BX, CX, DX.  Preserves SI and DI
;
; The stick commands a rotation about the wing axis, and adding it straight
; into [cs_pitch] is a rotation about the WORLD's - the same thing only while
; the wings are level. Two terms convert it, and neither needs a constant:
;
;     world pitch += q cos(roll)          heading += q sin(roll)
;
; In a 90 degree bank cos is 0 and sin is 1, so back-pressure is all turn and
; no pitch - which is what a banked aeroplane does. Over the top of a loop
; the roll is 180 and cos is -1, so the same key moves the nose the other way
; round the circle, which is the right way UP: the controls come back the
; right way round with no case analysis at all.
;
; The exact conversion divides the heading term by cos(pitch) and adds
; q sin(roll) tan(pitch) to the roll. BOTH ARE DROPPED: they are singular at
; the vertical, which is somewhere this aeroplane goes, and what they buy -
; a turn that keeps its rate at high pitch, and apparent roll under
; back-pressure in a bank - is worth less than a model with no blow-up in it.
;
; BUT THE FIRST OF THEM HAS A SIGN IN IT (SPEC.md 88.7.8.1). Dropping the
; magnitude of 1/cos(pitch) costs a turn rate; dropping the SIGN costs the
; direction, because past the vertical cos(pitch) is negative - and this
; aeroplane loops. Reported from the field as "after acrobatics the elevator
; inverts relative to the banked turn: in a 90 degree left bank, holding up
; goes right". The sign is kept and the magnitude is still dropped, which is
; four instructions and no blow-up.
; -----------------------------------------------------------------------------
cs_elev:
    push si
    push di
    push ax                         ; the body rate, past cs_sin's clobbers
    mov ax, [cs_roll]
    call cs_sin
    mov bx, ax
    pop ax
    push ax
    MUL14                           ; q sin(roll): the HEADING, and this proc
    ; --- ...TIMES THE SIGN OF cos(pitch) (SPEC.md 88.7.8.1). The division
    ;     dropped below takes the sign with it, and past the vertical that
    ;     sign is NEGATIVE - so after a loop a banked pull turned the wrong
    ;     way. It needs no cosine: cos is negative exactly when the angle is
    ;     more than a quarter turn from zero, which is the top bit of the
    ;     angle plus a quarter turn.
    mov bx, [cs_pitch]
    add bx, 16384
    jns .hd
    neg ax
.hd:
    add [cs_hdg], ax                ; is the only place it is applied
    mov ax, [cs_roll]
    call cs_cos
    mov bx, ax
    pop ax
    MUL14                           ; q cos(roll): the world PITCH, back to
    pop di                          ; the caller to ease and integrate
    pop si
    ret

; cs_axisp - cs_axis for the PITCH axis, which is a BODY rate (88.7.8) and is
;            resolved into the world before the ease, the add or the clamp
;            sees it. Same registers in and out
cs_axisp:
    or ax, ax
    jnz .live
    jmp cs_axis.ret
.live:
    push dx                         ; DX is the limit, and both imul and
    imul bx                         ; cs_elev write it
    call cs_elev
    pop dx
    jmp cs_axis.eased

cs_axis:
    or ax, ax
    jz .ret
    push dx                         ; imul writes DX, and DX is the limit
    imul bx
    pop dx
.eased:
    call cs_ease                    ; ...and the horizon takes the last three
    add ax, [si]                    ; ticks of an approach (88.7.3)
    cmp ax, dx
    jle .lo
    mov ax, dx
.lo:
    neg dx
    cmp ax, dx
    jge .st
    mov ax, dx
.st:
    mov [si], ax
    ret
.ret:
    mov ax, [si]                    ; nothing held: toward zero by CX
    or ax, ax
    jz .done
    js .neg
    sub ax, cx
    jns .st
    xor ax, ax
    jmp short .st
.neg:
    add ax, cx
    js .st
    xor ax, ax
    jmp short .st
.done:
    ret

; cs_move - one tick of [cs_hs] along [cs_hdg] and [cs_vs] up. The per-tick
; displacement is v / 18.2, which is v x 7200 >> 16 with speed in 16.7 and
; the position in 16.8 (SPEC.md 88.7.4).
cs_move:
    mov ax, [cs_hs]
    mov bx, 7200
    imul bx
    mov ax, dx                      ; AX = metres a tick, 16.8
    mov [cs_ht], ax
    mov ax, [cs_hdg]
    call cs_sin
    mov bx, [cs_ht]
    MUL14
    cwd
    add [cs_px], ax
    adc [cs_px+2], dx
    mov ax, [cs_hdg]
    call cs_cos
    mov bx, [cs_ht]
    MUL14
    cwd
    add [cs_pz], ax
    adc [cs_pz+2], dx
    mov ax, [cs_vs]
    mov bx, 7200
    imul bx
    mov ax, dx
    cwd
    add [cs_py], ax
    adc [cs_py+2], dx
    ret

; cs_fence - the world is +-CS_EDGE metres, and the aeroplane stays in it
cs_fence:
    mov si, cs_px
    call .one
    mov si, cs_pz
    call .one
    ret
.one:
    mov ax, [si+1]
    cmp ax, CS_EDGE
    jg .hi
    cmp ax, -CS_EDGE
    jl .lo
    ret
.hi:
    mov word [si+1], CS_EDGE
    mov byte [si], 0
    mov byte [cs_msg], CSG_EDGE
    ret
.lo:
    mov word [si+1], -CS_EDGE
    mov byte [si], 0
    mov byte [cs_msg], CSG_EDGE
    ret

; =============================================================================
; THE AIR (SPEC.md 88.7.6.3) - a REPEATING TILE of lift and sink
;
; The first build was eight rects placed once, in metres from the location's
; centre, and the field flew for five minutes in two different worlds and
; never found one (88.7.6.4). Four rects in a tile that repeats every
; CS_AIRTILE metres is the same table doing a different job: the air is
; everywhere, a glider meets some inside a minute wherever it points, and
; the arithmetic is one AND per axis rather than a longer walk.
;
; The tile is anchored on the LOCATION'S OWN CENTRE, so one table serves all
; nine worlds and the pattern sits the same way round every runway.
;
; CS_LIFTCLR is a CALM BUBBLE round the field and no longer a constraint on
; where a rect may be put: the distance test comes first and the tile is free
; to be dense. That is what makes the two requirements independent - a
; guaranteed still circuit, and air you can find.
;
; The rate is cs_vs's own scale, m/s x 128, positive UP. A sailplane's still
; air sink is about 1 m/s, so +448 is a climb you can work and -320 is air
; worth leaving.
; =============================================================================
CSAIR_DX    equ 0                   ; within the tile, metres
CSAIR_DZ    equ 2
CSAIR_HW    equ 4                   ; ...and how far it reaches, half-extents
CSAIR_HD    equ 6
CSAIR_RATE  equ 8                   ; m/s x 128, + up
CSAIR_SIZE  equ 10
CS_NLIFT    equ 4
CS_AIRTILE  equ 4096                ; a POWER OF TWO: the wrap is an AND
CS_LIFTCLR  equ 1800                ; the calm bubble round the field

cs_lifts:
    dw   800,  900,  600, 500,  448     ; +3.5 m/s
    dw  2600,  700,  500, 450, -320     ; -2.5
    dw  1500, 2800,  650, 550,  512     ; +4.0, the strong one
    dw  3300, 2600,  500, 500, -256

; -----------------------------------------------------------------------------
; cs_lift - the air's vertical rate where the aeroplane is, in cs_vs's units
; out: AX = the rate, 0 in still air. Clobbers BX, CX, SI, DI
;
; AN AEROPLANE WITH AN ENGINE DOES NOT FEEL IT, and that is the design rather
; than a shortcut (88.7.6.3): a Magister crosses one of these in three seconds
; at a bump it has the thrust to ignore, where a sailplane at 22 m/s dwells in
; it for a minute and has nothing else to climb on. It is the same
; CSP_THRUST = 0 the throttle is gated on, so a sailplane is exactly the
; aeroplane the air flies.
; -----------------------------------------------------------------------------
cs_lift:
    xor ax, ax
    mov di, [cs_plane]
    cmp word [di + CSP_THRUST], 0
    jne .out
    mov si, [cs_airport]
    mov cx, [cs_px+1]
    sub cx, [si + CSA_X]
    mov di, [cs_pz+1]
    sub di, [si + CSA_Z]
    ; --- THE CALM BUBBLE FIRST (88.7.6.4): a launch is over the threshold and
    ;     a circuit is flown here, so the field's own air is still whatever
    ;     the tile says. Manhattan, which is what every other range in this
    ;     simulator is measured in
    mov ax, cx
    call cs_wabs
    mov bx, ax
    mov ax, di
    call cs_wabs
    add ax, bx
    cmp ax, CS_LIFTCLR
    jb .none
    and cx, CS_AIRTILE - 1          ; ...and the tile, which is why this is a
    and di, CS_AIRTILE - 1          ; power of two
    mov si, cs_lifts
    mov bx, CS_NLIFT
.l:
    mov ax, cx
    sub ax, [si + CSAIR_DX]
    call cs_wabs
    cmp ax, [si + CSAIR_HW]
    jg .n
    mov ax, di
    sub ax, [si + CSAIR_DZ]
    call cs_wabs
    cmp ax, [si + CSAIR_HD]
    jg .n
    mov ax, [si + CSAIR_RATE]
    ret
.n:
    add si, CSAIR_SIZE
    dec bx
    jnz .l
.none:
    xor ax, ax
.out:
    ret

; -----------------------------------------------------------------------------
; cs_msgage - an ANNOUNCEMENT goes by itself; a WARNING stays (SPEC.md 88.7.6.2)
;
; The field reported the glider's `TOW RELEASED - NOSE DOWN FOR 43 KNOTS`
; standing on the panel until something else happened, and the cause is that
; a sailplane STARTS IN THE AIR: `CSG_TAKEOFF` is cleared by the liftoff at
; cs_step's `.steer`, and an aeroplane launched already flying never reaches
; that line. So the prompt had no clearer at all.
;
; A countdown rather than a second condition, because the two are different
; kinds of message. A WARNING is true while a state is true and clears when
; the state does - the stall, the world's edge, a crash. An ANNOUNCEMENT is a
; thing that HAPPENED, and the reader has read it: the tow release, a landing,
; a splashdown. Those three set [cs_msgt] and it takes them away.
; -----------------------------------------------------------------------------
cs_msgage:
    cmp byte [cs_msgt], 0
    je .out
    dec byte [cs_msgt]
    jnz .out
    ; A TOAST (88.13.8) BORROWS THE STRIP and banks what it took, so an
    ; announcement that ages out UNDER one is retired where it is actually
    ; kept: clearing cs_msg here would cut the toast short, and leaving
    ; cs_toastwas alone would put the announcement back when the toast went.
    cmp byte [cs_msg], CSG_TOAST
    je .under
    mov byte [cs_msg], CSG_NONE
.out:
    ret
.under:
    mov byte [cs_toastwas], CSG_NONE
    ret

; -----------------------------------------------------------------------------
; cs_touch - the wheels met the ground: a landing, or not (SPEC.md 88.7)
; -----------------------------------------------------------------------------
cs_touch:
    ; --- THE WATER FIRST, when this aeroplane can use it (SPEC.md 88.7.7).
    ; A water strip is a runway made of water - the same four numbers, so
    ; the arithmetic below is the arithmetic above and an amphibian's
    ; landing is not a polygon test. Every other aeroplane skips this and
    ; ditching stays a crash, which is what ditching is.
    mov byte [cs_onwater], 0
    mov di, [cs_plane]
    test word [di + CSP_FLAGS], CSPF_AMPHIB
    jz .rwy
    mov si, [cs_airport]
    cmp word [si + CSA_WLEN], 0     ; 0 is a place that offers no water
    je .rwy                         ; landing at all (88.7.7)
    call cs_inwater                 ; ALL of it, not a strip on it (88.7.7.1)
    jnc .rwy
    mov byte [cs_onwater], 1        ; ...on the water, and the limits below
    jmp short .limits               ; are the same ones
.rwy:
    call cs_runway_xy               ; [cs_along]/[cs_across]
    mov si, [cs_airport]
    mov ax, [cs_along]
    or ax, ax
    jns .a1
    neg ax
.a1:
    cmp ax, [si + CSA_HLEN]
    jg .crash
    mov ax, [cs_across]
    or ax, ax
    jns .a2
    neg ax
.a2:
    cmp ax, [si + CSA_HWID]
    jg .crash
.limits:
    cmp word [cs_vs], CS_LANDVS
    jl .crash
    mov ax, [cs_roll]
    or ax, ax
    jns .a3
    neg ax
.a3:
    cmp ax, CS_LANDROLL
    jg .crash
    mov ax, [cs_pitch]
    cmp ax, CS_LANDPMIN
    jl .crash
    cmp ax, CS_LANDPMAX
    jg .crash
    ; --- down, and rolling --------------------------------------------------
    mov byte [cs_state], CS_ST_GROUND
    mov ax, [si + CSA_ELEV]
    mov [cs_py+1], ax
    mov byte [cs_py], 0
    mov word [cs_pitch], 0
    mov word [cs_roll], 0
    mov word [cs_vs], 0
    mov byte [cs_msg], CSG_LANDED
    mov byte [cs_msgt], CS_MSGAGE
    cmp byte [cs_onwater], 0
    je .counted
    mov byte [cs_msg], CSG_SPLASH   ; ...and the water says WHICH water, off
    mov ax, [cs_wname]              ; the object actually touched (88.7.7.1):
    or ax, ax                       ; four worlds carry more than one and all
    jnz .named                      ; four used to report the same name
    mov si, [cs_airport]
    mov ax, [si + CSA_WNAME]
.named:
    mov [cs_crashwhy], ax
.counted:
    inc word [cs_landings]
    ret
.crash:
    mov si, cs_n_ground
    jmp cs_crash

; =============================================================================
; cs_inwater - is the aeroplane over DRAWN water? (SPEC.md 88.7.7.1)
;
; out: CF=1 and [cs_wname] = the touched object's name; CF=0 and [cs_wname] 0.
;      Clobbers every register.
;
; A point-in-polygon test over the location's own CSI_RIVER faces, and what
; makes it affordable is that cs_touch FIRES ONCE, at the tick the wheels meet
; the ground - which cs_water_xy's own comment said before this was written.
; At most 20 water faces in any world here and every one a quad, behind a
; Manhattan reject on the object's CSM_RAD.
;
; DIVISION-FREE, and in the OBJECT's coordinates rather than the world's: the
; point is brought to the object, so every term is bounded by the model's own
; extent (measured: local vertices +-6,000, worst product 9.1e7) and one
; imul's DX:AX holds it with room to spare.
; =============================================================================
cs_inwater:
    mov word [cs_wname], 0
    mov si, [cs_airport]
    mov ax, [si + CSA_NOBJ]
    mov [cs_wnob], ax
    or ax, ax
    jz .no
    mov ax, [si + CSA_OBJS]
    mov [cs_wobj], ax
.obj:
    mov si, [cs_wobj]
    mov di, [si + CSO_MODEL]
    cmp byte [di + CSM_TYPE], CSM_FLAT
    jne .next                       ; water is a RIBBON, and a ribbon is FLAT
    mov ax, [cs_px+1]               ; --- the point in the object's own frame
    sub ax, [si + CSO_X]
    mov [cs_wpx], ax
    call cs_wabs
    mov cx, ax
    mov ax, [cs_pz+1]
    sub ax, [si + CSO_Z]
    mov [cs_wpz], ax
    call cs_wabs
    add ax, cx                      ; ...Manhattan-rejected on the model's own
    mov bx, [di + CSM_RAD]          ; radius, which is what keeps a touchdown
    mov dx, bx                      ; from walking every ribbon in the world
    shr dx, 1                       ; --- AND ONE AND A HALF OF IT, because
    add bx, dx                      ;     the sum is a MANHATTAN distance and
    cmp ax, bx                      ;     CSM_RAD is a EUCLIDEAN radius
    jg .next                        ;     (SPEC.md 88.5.11): |dx| + |dz| runs
                                    ;     to sqrt(2) x the radius at 45
                                    ;     degrees, so a bare compare rejects
                                    ;     a point that IS over the water -
                                    ;     the Seine's pieces lie diagonally
                                    ;     and their corners are exactly where
                                    ;     the two measures diverge most. 3/2
                                    ;     is over sqrt(2) and is a shift and
                                    ;     an add; the rejected set only grows,
                                    ;     and what it grows by is points this
                                    ;     was wrong about
    mov ax, [di + CSM_VERTS]
    mov [cs_wvt], ax
    mov cl, [di + CSM_NF]
    xor ch, ch
    jcxz .next
    mov [cs_wnf], cx
    mov si, [di + CSM_FACES]
.face:
    mov [cs_wfp], si
    cmp byte [si + 1], CSI_RIVER    ; the INK is what says water: there is no
    jne .nf                         ; flag on the object and never was
    call cs_wpoly
    jc .yes
.nf:
    mov si, [cs_wfp]                ; db count, ink, flags, then the indices
    mov al, [si]
    xor ah, ah
    add ax, 3
    add si, ax
    dec word [cs_wnf]
    jnz .face
.next:
    add word [cs_wobj], CSO_SIZE
    dec word [cs_wnob]
    jnz .obj
.no:
    clc
    ret
.yes:
    mov si, [cs_wobj]
    mov ax, [si + CSO_NAME]
    mov [cs_wname], ax
    stc
    ret

; cs_wabs - AX = |AX|
cs_wabs:
    or ax, ax
    jns .p
    neg ax
.p:
    ret

; cs_wpoly - SI -> a face record: CF=1 if (cs_wpx, cs_wpz) is inside it.
;            The crossing count, walked with the PREVIOUS vertex kept, so an
;            edge costs one index lookup rather than two
cs_wpoly:
    mov byte [cs_wodd], 0
    mov cl, [si]
    xor ch, ch
    mov [cs_wnv], cx
    add si, 3
    mov [cs_wface], si
    mov bx, cx                      ; ...starting from the LAST vertex, which
    dec bx                          ; is what closes the polygon
    call cs_wvert
    call cs_wkeep
    xor bx, bx
.e:
    mov [cs_wi], bx
    call cs_wvert
    call cs_wedge
    call cs_wkeep                   ; ...and this vertex becomes the last one
    mov bx, [cs_wi]
    inc bx
    cmp bx, [cs_wnv]
    jb .e
    shr byte [cs_wodd], 1           ; CF = an ODD number of crossings
    ret

; cs_wkeep - the vertex just read becomes the edge's first end
cs_wkeep:
    mov ax, [cs_wvx]
    mov [cs_wax], ax
    mov ax, [cs_wvz]
    mov [cs_waz], ax
    ret

; cs_wvert - BX = a position in the face's index list; out cs_wvx/cs_wvz
cs_wvert:
    push bx
    mov si, [cs_wface]
    add si, bx
    mov bl, [si]
    xor bh, bh
    shl bx, 1
    shl bx, 1                       ; four bytes a vertex: x then z
    add bx, [cs_wvt]
    mov si, bx
    mov ax, [si]
    mov [cs_wvx], ax
    mov ax, [si + 2]
    mov [cs_wvz], ax
    pop bx
    ret

; cs_wedge - one edge, (cs_wax, cs_waz) to (cs_wvx, cs_wvz), against the
;            point: toggle [cs_wodd] when the ray crosses it
cs_wedge:
    xor bx, bx                      ; BL = (az > pz), BH = (bz > pz)
    mov ax, [cs_waz]
    cmp ax, [cs_wpz]
    jle .a0
    mov bl, 1
.a0:
    mov ax, [cs_wvz]
    cmp ax, [cs_wpz]
    jle .b0
    mov bh, 1
.b0:
    cmp bl, bh
    je .out                         ; the edge does not straddle the row
    mov ax, [cs_wvx]                ; t = dx (pz - az) - dz (px - ax), and the
    sub ax, [cs_wax]                ; DIVISION the textbook form does is a
    mov bx, [cs_wpz]                ; comparison against dz's sign instead
    sub bx, [cs_waz]
    imul bx
    mov cx, ax
    mov si, dx                      ; SI:CX = dx (pz - az)
    mov ax, [cs_wvz]
    sub ax, [cs_waz]
    mov [cs_wdz], ax
    mov bx, [cs_wpx]
    sub bx, [cs_wax]
    imul bx
    sub cx, ax
    sbb si, dx                      ; SI:CX = t
    or si, si
    js .neg
    jnz .pos
    or cx, cx
    jz .out                         ; t = 0: ON the edge, and no toggle either
.pos:                               ; way is what makes it deterministic
    cmp word [cs_wdz], 0
    jg .tog
    ret
.neg:
    cmp word [cs_wdz], 0
    jl .tog
.out:
    ret
.tog:
    xor byte [cs_wodd], 1
    ret

; cs_runway_xy - the aeroplane in the runway's coordinates: along its heading
; and across it, whole metres
cs_runway_xy:
    mov si, [cs_airport]
    mov ax, [si + CSA_X]
    mov bx, [si + CSA_Z]
    mov cx, [cs_rwsin]              ; the runway's, already computed
    mov dx, [cs_rwcos]
    jmp short cs_local_xy

; cs_water_xy - and the WATER STRIP's (SPEC.md 88.7.7), whose sine and cosine
; are taken here rather than cached: this runs once, at a touchdown
cs_water_xy:
    mov si, [cs_airport]
    mov ax, [si + CSA_WHDG]
    call cs_sin
    push ax
    mov si, [cs_airport]
    mov ax, [si + CSA_WHDG]
    call cs_cos
    mov dx, ax
    pop cx
    mov si, [cs_airport]
    mov ax, [si + CSA_WX]
    mov bx, [si + CSA_WZ]

; cs_local_xy - AX/BX = a strip's centre, CX/DX = the sine and cosine of its
; heading: the aeroplane in ITS coordinates, into [cs_along]/[cs_across]
cs_local_xy:
    mov [cs_lsn], cx
    mov [cs_lcs], dx
    mov cx, ax
    mov ax, [cs_px+1]
    sub ax, cx
    mov [cs_ddx], ax
    mov ax, [cs_pz+1]
    sub ax, bx
    mov [cs_ddz], ax
    mov ax, [cs_ddx]
    mov bx, [cs_lsn]
    MUL14
    mov cx, ax
    mov ax, [cs_ddz]
    mov bx, [cs_lcs]
    MUL14
    add ax, cx
    mov [cs_along], ax
    mov ax, [cs_ddx]
    mov bx, [cs_lcs]
    MUL14
    mov cx, ax
    mov ax, [cs_ddz]
    mov bx, [cs_lsn]
    MUL14
    sub cx, ax
    mov [cs_across], cx
    ret

; -----------------------------------------------------------------------------
; cs_collide - the aeroplane against every CSO_COLLIDE box (SPEC.md 88.6)
; -----------------------------------------------------------------------------
cs_collide:
    mov si, [cs_airport]            ; the PICKED location's world (88.6.4):
    mov cx, [si + CSA_NOBJ]         ; a tower in Paris is not in the way of
    mov si, [si + CSA_OBJS]         ; an aeroplane over Manhattan
    jcxz .out
.o:
    mov ax, [si + CSO_FLAGS]
    test ax, CSO_COLLIDE
    jz .next
    test ax, CSO_DENSE              ; ...AND YOU CANNOT HIT WHAT THE RUNG DOES
    jz .solid                       ; NOT DRAW (88.13.1.4): a High tier is not
    cmp byte [cs_setbld], CSBL_HIGH ; in the world below High, and this walker
    jne .next                       ; reads the table and never the ladder, so
.solid:                             ; without it Manhattan's twelve kill a
                                    ; player at Moderate out of clear air
    mov ax, [cs_px+1]
    sub ax, [si + CSO_X]
    jns .x1
    neg ax
.x1:
    mov bx, [si + CSO_MODEL]
    mov di, [bx + CSM_VERTS]        ; level 0: wx, h, wz
    cmp ax, [di]
    jge .next
    mov ax, [cs_pz+1]
    sub ax, [si + CSO_Z]
    jns .z1
    neg ax
.z1:
    cmp ax, [di+4]
    jge .next
    mov al, [bx + CSM_NV]           ; the tallest level is the last
    xor ah, ah
    dec ax
    mov dx, ax
    shl ax, 1
    add ax, dx
    shl ax, 1                       ; x 6
    add di, ax
    mov ax, [di+2]
    add ax, [si + CSO_Y]
    cmp [cs_py+1], ax
    jge .next
    mov si, [si + CSO_NAME]
    jmp cs_crash
.next:
    add si, CSO_SIZE
    loop .o
.out:
    ret

; -----------------------------------------------------------------------------
; cs_crash - SI = what was hit. The picture freezes with the reason on the
;            panel for CS_CRASHT ticks, then cs_reset (SPEC.md 88.7)
; -----------------------------------------------------------------------------
cs_crash:
    mov [cs_crashwhy], si
    mov byte [cs_state], CS_ST_CRASH
    mov word [cs_crasht], CS_CRASHT
    mov byte [cs_msg], CSG_CRASH
    inc word [cs_crashes]
    cmp byte [cs_sound], 0
    je .quiet
    mov ax, 110                     ; a low blast, a second long
    mov cx, 18
    mov dl, 0x40
    call OSAPI_SND_TONE
    mov word [cs_tone], 0xFFFF      ; ...and the engine tone is re-issued
                                    ; after it, whatever it was
.quiet:
    ret

; -----------------------------------------------------------------------------
; cs_sound_step - the engine tone follows the throttle; a stall beeps
;                 (SPEC.md 88.8). Once a tick.
; -----------------------------------------------------------------------------
cs_sound_step:
    xor ax, ax                      ; AX = the tone wanted
    cmp byte [cs_sound], 0
    je .want
    cmp byte [cs_state], CS_ST_CRASH
    je .want
    ; --- THE AIR HAS THE FIRST WORD (SPEC.md 88.7.6.3): a rising swoop into
    ;     lift, a falling one into sink, CS_SWOOPT ticks of it. It is only
    ;     ever heard on an aeroplane with no engine, because only there does
    ;     cs_lift answer at all - and there the tone under it is silence
    cmp byte [cs_swt], 0
    je .noswoop
    dec byte [cs_swt]
    mov al, CS_SWOOPT
    sub al, [cs_swt]                ; 1..CS_SWOOPT: how far through it is
    cbw
    mov bx, CS_SWOOPD
    imul bx
    cmp byte [cs_swdir], 0
    jg .swup
    neg ax                          ; sink: down from the top
    add ax, CS_SWOOPHI
    jmp short .want
.swup:
    add ax, CS_SWOOPLO              ; lift: up from the bottom
    jmp short .want
.noswoop:
    mov ax, [cs_thr]
    or ax, ax
    jz .st
    add ax, 50
.st:
    cmp byte [cs_stall], 0
    je .want
    inc byte [cs_stallt]
    cmp byte [cs_stallt], 9
    jb .sb
    mov byte [cs_stallt], 0
.sb:
    cmp byte [cs_stallt], 4
    jae .want
    mov ax, 900                     ; the stall warning: four ticks in nine
.want:
    cmp ax, [cs_tone]
    je .out
    mov [cs_tone], ax
    xor cx, cx
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    ret
