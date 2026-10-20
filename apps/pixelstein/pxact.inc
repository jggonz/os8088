; =============================================================================
; os8088 - apps/pixelstein/pxact.inc
;
; THE GUARDS (SPEC.md 97.8): stand / patrol / alert / chase / attack / pain /
; die / dead, one thinker a tick per actor, in the 1992 engine's shape read
; for technique. A standing or patrolling guard SEES the player when its own
; cell carries this frame's spotvis generation (a ray crossed it: the cheap
; gate), the player is not behind it, and a tile walk from it to the player
; meets no wall and no door shut past the line (px_act_los: two axis passes
; of at most PX_LOSMAX steps each, a slope divide an axis, a door's position
; read where the line crosses its slab - the CheckLine shape). It then
; REACTS after 1..4 ticks, chases four-way toward the player opening doors
; in its path, and when the line is clear ATTACKS with a chance that falls
; with distance, hitting by the 1992 engine's table (256 - 16 x tiles when
; the player can see it, 256 - 8 x tiles when not, 160 - ... when the
; player is running; the damage a random byte >> 2, 3 or 4 by distance).
; Damage to it is doubled while it has not seen the player; PX_GUARDHP is
; its life. A dead guard is a sprite that blocks nothing and keeps a door
; open.
;
; THE DOG (wave 6, PXK_DOG): the same thinker and the same states, with
; three differences - it runs at PX_DOGRUN (1.67x a guard, faster than the
; player walks), it has NO GUN: from a tile away with the line clear it
; LEAPS (ATTACK, PX_DOGLUNGE ticks on its bite frame) and bites for
; PX_DOGBITE in 256, a random byte >> 4, then PX_DOGBACK ticks before the
; next step; and ONE hit kills it (PX_DOGHP), 200 points. Four facing
; masters make its eight (px_dogfac), and it has no pain frame.
;
; Every actor's centre cell carries PXC_ACTOR in the plain map (pxgame.asm),
; cleared and set as it moves, so a mover's collision is the same cell test
; the player's is; the player's cell carries PXC_PLAYER for the same reason.
; =============================================================================

px_act_dirx: dw 1, 0, -1, 0         ; E S W N, Q8.8 per unit of speed
px_act_diry: dw 0, 1, 0, -1

; -----------------------------------------------------------------------------
; px_act_frame - the sprite frame of the actor at SI for a candidate whose
;                column is px_scn's (97.6): the walk frames by the facing
;                the viewer sees - the direction from the actor to the eye
;                is the ray through its column plus a half turn, and the
;                index is ((that - the actor's facing + 256) >> 9) & 7:
;                0 the front, 2 its right side, 4 its back, 6 its left side
;                (the mirror of 2), 1, 3, 5, 7 the quarters - masters 0..4,
;                5..7 mirrors of 3..1; the attack, pain, die and dead frames
;                face nowhere. THE DOG (wave 6) takes its four masters
;                through px_dogfac, and its leap, fall and corpse
; out: px_scn's frame and flags. clobbers AX, BX, CX
; -----------------------------------------------------------------------------
px_act_frame:
    mov byte [px_scn + PXS_C_FL], 0
    mov al, [si + PXAC_STATE]
    cmp byte [si + PXAC_KIND], PXK_DOG
    je .dog
    cmp al, PXAS_ATTACK
    je .attack
    cmp al, PXAS_PAIN
    je .pain
    cmp al, PXAS_DIE
    je .die
    cmp al, PXAS_DEAD
    je .dead
    call .facing
    cmp al, 4
    jbe .master
    neg al
    add al, 8                       ; 8 - f: the master, mirrored
    mov byte [px_scn + PXS_C_FL], 1
.master:
    shl al, 1
    add al, cl
    add al, PXS_G_WALK0
    jmp short .set
.attack:
    mov al, PXS_G_SHOOT
    cmp byte [si + PXAC_FRAME], 1
    jne .set
    inc al                          ; the flash
    jmp short .set
.pain:
    mov al, PXS_G_PAIN
    jmp short .set
.die:
    mov al, [si + PXAC_FRAME]
    add al, PXS_G_DIE
    jmp short .set
.dead:
    mov al, PXS_G_DEAD
.set:
    mov [px_scn + PXS_C_FR], al
    ret
    ; --- THE DOG (wave 6): four masters, the leap, the fall ------------------
.dog:
    mov ah, PXS_D_BITE
    cmp al, PXAS_ATTACK
    jne .dg1
    cmp byte [si + PXAC_FRAME], 0
    je .dset                        ; the leap; the recovery stands
.dg1:
    mov ah, PXS_D_DIE
    cmp al, PXAS_DIE
    je .dset
    mov ah, PXS_D_DEAD
    cmp al, PXAS_DEAD
    je .dset
    call .facing                    ; AL = the facing, CL = the walk phase
    mov bl, al
    xor bh, bh
    mov al, [px_dogfac + bx]        ; the master, bit 7 mirrored
    or al, al
    jns .dm
    mov byte [px_scn + PXS_C_FL], 1
    and al, 3
.dm:
    shl al, 1
    add al, cl
    add al, PXS_D_WALK0
    jmp short .set
.dset:
    mov al, ah
    jmp short .set
    ; .facing - AL = the facing index 0..7 the viewer sees, CL = the walk
    ; phase (0 unless it patrols or chases). clobbers BX
.facing:
    mov bx, [px_scn + PXS_C_C]      ; the column, clamped to the fan
    or bx, bx
    jns .c0
    xor bx, bx
.c0:
    mov al, [px_cols]
    xor ah, ah
    dec ax
    cmp bx, ax
    jle .c1
    mov bx, ax
.c1:
    shl bx, 1
    add bx, [px_fanp]
    mov ax, [bx]
    add ax, [px_head]
    add ax, PX_ANG / 2              ; from the actor toward the eye
    sub ax, [si + PXAC_ANG]
    add ax, PX_ANG / 16
    mov cl, 9
    shr ax, cl
    and al, 7                       ; the facing index
    mov cl, [si + PXAC_FRAME]        ; the walk phase, while it walks
    shr cl, 1
    shr cl, 1
    and cl, 1
    mov ah, [si + PXAC_STATE]
    cmp ah, PXAS_PATROL
    je .fw
    cmp ah, PXAS_CHASE
    je .fw
    xor cl, cl
.fw:
    ret

; the dog's eight facings from its four masters (tools/pxsart.py's
; D_FACING): the master, bit 7 mirrored - its front, the three-quarter, its
; side, and the side again for the two quarters BEHIND it, its back, then
; the left half mirrored
px_dogfac:  db 0, 1, 2, 2, 3, 0x82, 0x82, 0x81

; -----------------------------------------------------------------------------
; px_act_tick - every actor's thinker, once a simulation step (97.8)
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_act_tick:
    mov cl, [px_nact]
    or cl, cl
    jz .out
    mov si, px_act
.a:
    push cx
    push si
    mov [px_acur], si
    call px_act_one
    pop si
    pop cx
    add si, PXAC_SIZE
    dec cl
    jnz .a
.out:
    ret

; px_act_one - the actor at SI, by state. clobbers everything but DS, SI
px_act_one:
    mov al, [si + PXAC_STATE]
    cmp al, PXAS_STAND
    je .stand
    cmp al, PXAS_PATROL
    je .patrol
    cmp al, PXAS_ALERT
    je .alert
    cmp al, PXAS_CHASE
    je .chase
    cmp al, PXAS_ATTACK
    je .attack
    cmp al, PXAS_PAIN
    je .pain
    cmp al, PXAS_DIE
    je .die
    ret                             ; NONE, DEAD: nothing
.stand:
    call px_act_sight
    jnc .out
    jmp short .seen
.patrol:
    call px_act_sight
    jc .seen
    mov byte [px_aspeed], PX_ACTWALK
    cmp byte [si + PXAC_KIND], PXK_DOG
    jne .pwalk
    mov byte [px_aspeed], PX_DOGWALK
.pwalk:
    mov al, [si + PXAC_DIR]
    call px_act_move
    jnc .walked
    inc al                          ; blocked: turn right, then left, then
    and al, 3                       ; back
    call px_act_move
    jnc .turned
    add al, 2
    and al, 3
    call px_act_move
    jnc .turned
    dec al                          ; dir + 3 - 1 = dir + 2: BACK (the first
    and al, 3                       ; cut's `inc` retried the blocked way, so
    call px_act_move                ; a patroller at a dead end stood for
    jc .out                         ; ever; review, wave 3)
.turned:
    call px_act_face
.walked:
    inc byte [si + PXAC_FRAME]
    call px_act_dirty
.out:
    ret
.seen:                              ; it saw the player: the reaction delay
    mov byte [si + PXAC_STATE], PXAS_ALERT
    call OSAPI_RAND
    and al, 3
    inc al
    mov [si + PXAC_TIMER], al
    call px_act_toward              ; ...and it turns to face the player
    call px_act_face
    call px_act_dirty
    ret
.alert:
    dec byte [si + PXAC_TIMER]
    jnz .out
    mov byte [si + PXAC_STATE], PXAS_CHASE
    or byte [si + PXAC_FLAGS], PXAF_ATTACK
    call px_act_dirty
    ret
.chase:
    call px_act_deltas
    cmp byte [si + PXAC_KIND], PXK_DOG
    je .dchase
    call px_act_los
    jnc .move
    ; a shot at the player: the chance falls with the distance (the 1992
    ; engine's (tics << 4) / dist at 70 Hz is 64 / dist a tick here)
    mov al, [px_adist]
    cmp al, 1
    jbe .sure
    xor ah, ah
    mov al, 64
    div byte [px_adist]             ; AL = 64 / dist, dist >= 2 here (the
    mov cl, al                      ; first cut divided AX = 0x0340 by AH =
    call OSAPI_RAND                 ; dist, a divide overflow on every chase
    cmp al, cl                      ; tick - and kept the chance in AH,
    jae .move                       ; which OSAPI_RAND answers into)
.sure:
    mov byte [si + PXAC_STATE], PXAS_ATTACK
    mov byte [si + PXAC_FRAME], 0
    mov byte [si + PXAC_TIMER], 5
    call px_act_toward
    call px_act_face
    call px_act_dirty
    ret
.dchase:                            ; THE DOG: no gun - a tile away with the
    cmp byte [px_adist], 1          ; line clear it LEAPS, else it runs
    ja .dmove
    call px_act_los
    jnc .dmove
    mov byte [si + PXAC_STATE], PXAS_ATTACK
    mov byte [si + PXAC_FRAME], 0
    mov byte [si + PXAC_TIMER], PX_DOGLUNGE
    call px_act_toward
    call px_act_face
    call px_act_dirty
    ret
.dmove:
    mov byte [px_aspeed], PX_DOGRUN
    jmp short .moveat
.move:
    mov byte [px_aspeed], PX_ACTRUN
.moveat:
    call px_act_toward              ; AL = the main axis's direction, AH
    push ax                         ; the other's (0xFF: none)
    call px_act_move
    pop ax
    jnc .went
    mov al, ah
    cmp al, 0xFF
    je .out
    call px_act_move
    jc .out
.went:
    call px_act_face
    inc byte [si + PXAC_FRAME]
    call px_act_dirty
    ret
.attack:
    dec byte [si + PXAC_TIMER]
    jnz .out
    mov al, [si + PXAC_FRAME]
    cmp byte [si + PXAC_KIND], PXK_DOG
    je .dbite
    or al, al
    jnz .ph1
    mov byte [si + PXAC_FRAME], 1    ; the shot
    mov byte [si + PXAC_TIMER], 3
    call px_act_shoot
    call px_act_dirty
    ret
.ph1:
    cmp al, 1
    jne .ph2
    mov byte [si + PXAC_FRAME], 2    ; the recovery
    mov byte [si + PXAC_TIMER], 5
    call px_act_dirty
    ret
.ph2:
    mov byte [si + PXAC_STATE], PXAS_CHASE
    mov byte [si + PXAC_FRAME], 0
    call px_act_dirty
    ret
.dbite:                             ; the dog: the leap lands (the bite),
    or al, al                       ; then it gathers itself
    jnz .ph2
    mov byte [si + PXAC_FRAME], 1
    mov byte [si + PXAC_TIMER], PX_DOGBACK
    call px_dog_bite
    call px_act_dirty
    ret
.pain:
    dec byte [si + PXAC_TIMER]
    jnz .out
    mov byte [si + PXAC_STATE], PXAS_CHASE
    call px_act_dirty
    ret
.die:
    dec byte [si + PXAC_TIMER]
    jnz .out
    inc byte [si + PXAC_FRAME]
    mov byte [si + PXAC_TIMER], 4
    cmp byte [si + PXAC_FRAME], 3
    jb .dying
    mov byte [si + PXAC_STATE], PXAS_DEAD
.dying:
    call px_act_dirty
    ret

; px_act_dirty - the actor at SI changed (a step, a frame, a state): a frame
;                is owed only if it can show - it was a candidate at the
;                last gather (PXAF_SEEN: the marks still hold that cast's
;                generation while the eye is still, so the record it left on
;                the glass is erased by the frame this owes), or its cell or
;                an open neighbour was crossed by that cast (px_spot_seen9:
;                it stepped into view). Review r1: every thinker set
;                px_dirty, and a patroller behind a wall cost a still eye a
;                cast and a gather every tick, ~36 ms of every 55.
;                preserves every register
px_act_dirty:
    test byte [si + PXAC_FLAGS], PXAF_SEEN
    jnz .yes
    push bx
    mov bx, [si + PXAC_CELL]
    call px_spot_seen9
    pop bx
    jnc .out
.yes:
    mov byte [px_dirty], 1
.out:
    ret

; -----------------------------------------------------------------------------
; px_act_sight - does the actor at SI see the player? CF = 1 yes: its cell
;                was crossed by a ray this frame, the player is not behind
;                it (its DIR's axis), and the line is clear
; clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
px_act_sight:
    mov bx, [si + PXAC_CELL]
    call px_spot_seen
    jnc .no
    call px_act_deltas
    mov al, [si + PXAC_DIR]
    cmp al, 4
    jae .los                        ; no direction: it looks everywhere
    ; behind it? E: the player west (dx < 0); S: north; W: east; N: south
    mov ah, [px_asdx]
    test al, 1
    jz .ew
    mov ah, [px_asdy]
.ew:
    test al, 2
    jz .pos
    neg ah
.pos:
    or ah, ah
    js .no
.los:
    jmp px_act_los
.no:
    clc
    ret

; px_act_deltas - the tile deltas from the actor at SI to the player:
;                 px_asdx/px_asdy signed, px_adx/px_ady their magnitudes,
;                 px_adist the larger. clobbers AX, BX
px_act_deltas:
    mov ax, [px_pcell]
    mov bx, [si + PXAC_CELL]
    mov ah, al                      ; the x tiles: the low six bits
    and ah, 63
    mov al, bl
    and al, 63
    sub ah, al                      ; (BX kept whole: the first cut built the
    mov [px_asdx], ah               ; actor's x in BH and then shifted BX for
    mov ax, [px_pcell]              ; its y, which read a tile of 91 for cell
    mov cl, 6                       ; 214 - so no guard beyond a corridor's
    shr ax, cl                      ; width ever had a line of sight)
    shr bx, cl
    sub al, bl
    mov [px_asdy], al
    mov ah, [px_asdx]
    or ah, ah
    jns .ax
    neg ah
.ax:
    mov [px_adx], ah
    or al, al
    jns .ay
    neg al
.ay:
    mov [px_ady], al
    cmp al, ah
    jae .d
    mov al, ah
.d:
    mov [px_adist], al
    ret

; px_act_toward - the four-way direction from the actor at SI toward the
;                 player (px_act_deltas done): AL = along the larger delta,
;                 AH = along the smaller (0xFF when it is 0). clobbers BX
px_act_toward:
    mov al, [px_asdx]
    mov bl, 0                       ; the x direction: E or W
    or al, al
    jns .xe
    mov bl, 2
.xe:
    mov al, [px_asdy]
    mov bh, 1                       ; the y direction: S or N
    or al, al
    jns .ys
    mov bh, 3
.ys:
    mov al, [px_ady]
    cmp [px_adx], al
    jb .ymain
    mov al, bl                      ; x is the main axis
    mov ah, bh
    cmp byte [px_ady], 0
    jne .out
    mov ah, 0xFF
    ret
.ymain:
    mov al, bh
    mov ah, bl
    cmp byte [px_adx], 0
    jne .out
    mov ah, 0xFF
.out:
    ret

; px_act_face - the actor at SI faces AL (0..3): its DIR and its ANG
px_act_face:
    push ax
    mov [si + PXAC_DIR], al
    xor ah, ah
    push cx
    mov cl, 10
    shl ax, cl                      ; dir * 1024
    pop cx
    mov [si + PXAC_ANG], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_act_move - the actor at SI a step of [px_aspeed] along direction AL:
;               its leading edge's two corner cells tested against walls,
;               doors (a shut one it opens if unlocked, and waits), statics,
;               other actors and the player. CF = 1 blocked. Its mark moves
;               with it
; preserves AX; clobbers BX, CX, DX, DI
; -----------------------------------------------------------------------------
px_act_move:
    push ax
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov cl, [px_aspeed]
    xor ch, ch
    mov ax, [px_act_diry + bx]      ; the step vector: the y step first,
    imul cx                         ; banked - `imul r16` writes DX:AX, and
    push ax                         ; the first cut computed x into DX and
    mov ax, [px_act_dirx + bx]      ; then multiplied y over it, so every
    imul cx                         ; mover's x step was 0 and no guard
    mov dx, ax                      ; DX = the x step        ever closed
    pop cx                          ; CX = the y step
    mov di, [si + PXAC_X]
    add di, dx                      ; DI = the new x
    mov ax, [si + PXAC_Y]
    add ax, cx
    push di
    push ax                         ; the new y
    ; the leading edge: the new centre plus the radius the way it goes
    mov ax, di
    or dx, dx
    jz .xl
    js .xm
    add ax, PX_ACTRAD
    jmp short .xl
.xm:
    sub ax, PX_ACTRAD
.xl:
    pop dx
    push dx
    or cx, cx
    jz .yl
    js .ym
    add dx, PX_ACTRAD
    jmp short .yl
.ym:
    sub dx, PX_ACTRAD
.yl:                                ; AX = the leading x, DX = the leading y
    mov bx, [si + PXAC_CELL]
    call px_act_unmark              ; its own mark aside for the test
    pop cx                          ; the new y
    pop di                          ; the new x
    pop bx                          ; the direction
    push bx
    push di
    push cx
    test bl, 1
    jnz .yaxis
    push dx                         ; x axis: (AX, DX - r) and (AX, DX + r)
    sub dx, PX_ACTRAD
    call px_act_cell
    pop dx
    jc .blocked
    add dx, PX_ACTRAD
    call px_act_cell
    jc .blocked
    jmp short .go
.yaxis:                             ; y axis: (AX - r, DX) and (AX + r, DX)
    push ax
    sub ax, PX_ACTRAD
    call px_act_cell
    pop ax
    jc .blocked
    add ax, PX_ACTRAD
    call px_act_cell
    jc .blocked
.go:
    pop cx
    pop di
    mov [si + PXAC_X], di
    mov [si + PXAC_Y], cx
    mov ax, di
    mov dx, cx
    call px_cell                    ; BX = the new cell
    mov [si + PXAC_CELL], bx
    call px_act_mark
    pop ax
    clc
    ret
.blocked:
    pop cx
    pop di
    mov bx, [si + PXAC_CELL]
    call px_act_mark
    pop ax
    stc
    ret

; px_act_mark / px_act_unmark - the PXC_ACTOR mark on cell BX set / cleared,
;              UNLESS THE CELL IS A DOOR (97.8): a door cell's high nibble is
;              its material, and the first cut's `or` and `and` there turned
;              a steel door (0xC2) into material 14 while a guard stood in
;              it and 0x82 once the player had walked through one - drawn
;              in the wrong texture from then on (review, wave 3). A body in
;              a doorway is asked of the actor table instead (px_body_in).
;              preserves every register
px_act_mark:
    test byte [px_map + bx], PXC_DOOR
    jnz .door
    or byte [px_map + bx], PXC_ACTOR
.door:
    ret
px_act_unmark:
    test byte [px_map + bx], PXC_DOOR
    jnz .door
    and byte [px_map + bx], ~PXC_ACTOR
.door:
    ret

; px_act_cell - the cell at AX, DX (Q8.8) blocks the mover at SI? CF = 1
;               yes. A DOOR cell is asked FIRST (97.8: its high nibble is a
;               material, never a mark - the first cut tested the marks
;               first and every steel door, 0xC2, read as PXC_PLAYER, so no
;               guard ever opened one; review, wave 3): a shut one it opens
;               (unlocked) and is blocked by until it slid past PX_DOORPASS,
;               and past that a body in the doorway - the player, or a
;               live actor other than itself - blocks it (px_body_in).
;               preserves AX, DX; clobbers BX, CX, DI
px_act_cell:
    push ax
    push dx
    call px_cell
    mov cl, [px_map + bx]
    test cl, PXC_DOOR
    jnz .door
    test cl, PXC_SOLID | PXC_BLOCK | PXC_ACTOR | PXC_PLAYER
    jnz .yes
.no:
    pop dx
    pop ax
    clc
    ret
.door:
    mov ax, bx
    push ax                         ; the cell
    call px_door_of
    jc .yesp
    mov bx, ax
    cmp word [bx + PXD_POS], PX_DOORPASS
    jae .body
    cmp byte [bx + PXD_LOCK], 0
    jne .yesp                       ; a locked door: it turns
    cmp byte [bx + PXD_STATE], PXDS_SHUT
    je .open
    cmp byte [bx + PXD_STATE], PXDS_CLOSING
    jne .yesp
.open:
    mov byte [bx + PXD_STATE], PXDS_OPENING
.yesp:
    pop ax
.yes:
    pop dx
    pop ax
    stc
    ret
.body:
    pop ax                          ; the cell
    mov di, si                      ; the mover does not block itself
    call px_body_in
    jnz .yes
    jmp short .no

; px_body_in - is a body in cell AX that blocks the asker DI - 0xFFFF the
;              player (its own cell then does not count), else an actor's
;              record (which does not count)? The player's cell, or a LIVE
;              actor's centre; a corpse blocks nothing (97.8). ZF clear if
;              so. clobbers BX, CX
px_body_in:
    push si
    cmp di, 0xFFFF
    je .acts
    cmp ax, [px_pcell]
    je .yes
.acts:
    mov cl, [px_nact]
    or cl, cl
    jz .no
    mov si, px_act
.a:
    cmp si, di
    je .an
    mov bl, [si + PXAC_STATE]
    cmp bl, PXAS_NONE
    je .an
    cmp bl, PXAS_DIE
    jae .an
    cmp [si + PXAC_CELL], ax
    je .yes
.an:
    add si, PXAC_SIZE
    dec cl
    jnz .a
.no:
    xor cl, cl                      ; ZF set: nobody
    pop si
    ret
.yes:
    mov cl, 1
    or cl, cl                       ; ZF clear
    pop si
    ret

; -----------------------------------------------------------------------------
; px_act_los - is the line from the actor at SI to the player clear? CF = 1
;              yes. The CheckLine shape (read for technique): an x pass over
;              the vertical grid lines between the two, the y at each by a
;              slope, then a y pass; a wall stops it, a door stops it where
;              the crossing lands on the slab (the fraction at or past the
;              door's position). Never past PX_LOSMAX tiles on either axis.
; clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
px_act_los:
    cmp byte [px_adx], PX_LOSMAX
    ja .no
    cmp byte [px_ady], PX_LOSMAX
    ja .no
    mov ax, [px_px]
    mov [px_lx2], ax
    mov ax, [px_py]
    mov [px_ly2], ax
    ; --- the x pass ---------------------------------------------------------
    cmp byte [px_adx], 0
    je .ypass
    mov ax, [si + PXAC_X]
    mov bx, ax
    and bx, 255                     ; partial: to the next x line
    cmp byte [px_asdx], 0
    js .xw
    neg bx
    add bx, 256
.xw:
    mov cx, [px_lx2]
    sub cx, ax                      ; CX = x2 - x1
    jns .xp
    neg cx
.xp:                                ; CX = |dx|, Q8.8, > 0
    mov ax, [px_ly2]
    sub ax, [si + PXAC_Y]            ; AX = y2 - y1
    push ax
    call px_los_slope               ; AX = (dy << 8) / |dx|, or 0 (below)
    mov [px_slos], ax
    pop ax
    imul bx                         ; dy x the partial, 32 bits...
    idiv cx                         ; .../ |dx|: the y at the first line,
    add ax, [si + PXAC_Y]            ; EXACT (the partial is at most |dx|, so
    mov [px_sfrac], ax              ; the quotient is at most |dy|: no
                                    ; overflow, no clamp - the first cut
                                    ; stepped the clamped slope and read a
                                    ; tile up to 128 away, past the map;
                                    ; review, wave 3)
    mov al, [si + PXAC_CELL]
    and al, 63                      ; the actor's x tile
    mov cl, [px_adx]                ; the lines to cross
    mov ch, [px_asdx]               ; ...the way
.xl:
    add al, 1                       ; the next line's cell: x stepped...
    cmp ch, 0
    jns .xr
    sub al, 2
.xr:
    mov bx, [px_sfrac]              ; ...y = the fraction's tile
    mov bl, bh
    xor bh, bh
    push cx
    mov cl, 6
    shl bx, cl                      ; y * 64
    pop cx
    xor ah, ah
    add bx, ax                      ; + x
    mov dx, [px_slos]
    add [px_sfrac], dx
    mov dl, [px_map + bx]
    test dl, PXC_SOLID
    jnz .no
    test dl, PXC_DOOR
    jz .xn
    push ax
    mov ax, bx
    call px_door_of                 ; the door's slab: the y at the line
    jc .nopop                       ; less half a step is where the line
    mov bx, ax                      ; crosses the middle of its cell
    mov ax, [px_sfrac]
    mov dx, [px_slos]
    sar dx, 1
    sub ax, dx
    xor ah, ah                      ; the fraction along the slab
    cmp ax, [bx + PXD_POS]
    pop ax
    jae .no                         ; on the door: stopped
.xn:
    dec cl
    jnz .xl
    ; --- the y pass ---------------------------------------------------------
.ypass:
    cmp byte [px_ady], 0
    je .yes
    mov ax, [si + PXAC_Y]
    mov bx, ax
    and bx, 255
    cmp byte [px_asdy], 0
    js .yw
    neg bx
    add bx, 256
.yw:
    mov cx, [px_ly2]
    sub cx, ax
    jns .yp
    neg cx
.yp:
    mov ax, [px_lx2]
    sub ax, [si + PXAC_X]
    push ax
    call px_los_slope
    mov [px_slos], ax
    pop ax
    imul bx
    idiv cx                         ; the x at the first line, exact
    add ax, [si + PXAC_X]
    mov [px_sfrac], ax
    mov ax, [si + PXAC_CELL]
    mov cl, 6
    shr ax, cl                      ; the actor's y tile
    mov cl, [px_ady]
    mov ch, [px_asdy]
.yl:
    add al, 1                       ; the next line's cell: y stepped...
    cmp ch, 0
    jns .yr
    sub al, 2
.yr:
    mov bx, [px_sfrac]              ; ...x = the fraction's tile
    mov bl, bh
    xor bh, bh
    push ax
    xor ah, ah
    push cx
    mov cl, 6
    shl ax, cl                      ; y * 64
    pop cx
    add bx, ax                      ; + x
    pop ax
    mov dx, [px_slos]
    add [px_sfrac], dx
    mov dl, [px_map + bx]
    test dl, PXC_SOLID
    jnz .no
    test dl, PXC_DOOR
    jz .yn
    push ax
    mov ax, bx
    call px_door_of
    jc .nopop
    mov bx, ax
    mov ax, [px_sfrac]
    mov dx, [px_slos]
    sar dx, 1
    sub ax, dx
    xor ah, ah
    cmp ax, [bx + PXD_POS]
    pop ax
    jae .no
.yn:
    dec cl
    jnz .yl
.yes:
    stc
    ret
.nopop:
    pop ax
.no:
    clc
    ret

; px_los_slope - AX = a delta (Q8.8, signed), CX = the other axis's |delta|
;                (> 0) -> AX = (delta << 8) / |other|, the step the walk
;                takes per line. UNDER PX_LOSDMIN the quotient could pass
;                32,767 - |delta| reaches (PX_LOSMAX + 1) x 256 - 1 = 6,399,
;                a tile's fraction each end, and 6,399 x 256 / 48 = 34,132
;                (review r1: the first cut's bound of 48 was derived from
;                6,144 and faulted the 8086's idiv, INT 0, which this
;                kernel does not catch) - and under 256 there is exactly ONE
;                line to cross (two tiles apart on that axis is 256 or
;                more), whose crossing the caller computes exactly, so the
;                slope is never stepped: it answers 0, and the door test's
;                half-step is then the crossing itself. clobbers DX
PX_LOSDMIN  equ 64
%if ((PX_LOSMAX + 1) * 256 - 1) * 256 / PX_LOSDMIN > 32767
%error "PX_LOSDMIN is under the divisor that keeps px_los_slope's quotient in a signed word"
%endif
%if PX_LOSDMIN > 256
%error "PX_LOSDMIN past 256: a slope of 0 would be stepped across a second line"
%endif
px_los_slope:
    cmp cx, PX_LOSDMIN
    jb .clamp
    cwd
    push cx
    mov cl, 8                       ; delta << 8, into DX:AX
    mov dh, dl
    mov dl, ah
    mov ah, al
    xor al, al
    pop cx
    idiv cx
    ret
.clamp:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; px_act_shoot - the actor at SI fires at the player (97.8): the line clear,
;                the chance by distance and whether the player can see it,
;                the damage by distance
; clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
px_act_shoot:
    call px_act_deltas
    call px_act_los
    jnc .out
    mov al, [px_adist]
    mov ah, 8                       ; x 8 unseen, x 16 seen
    test byte [si + PXAC_FLAGS], PXAF_SEEN
    jz .m
    mov ah, 16
.m:
    mul ah
    mov bx, 256
    mov cl, [px_kfwd]               ; a running player is harder to hit
    or cl, cl
    jns .r
    neg cl
.r:
    cmp cl, PX_RUN
    jb .c
    mov bx, 160
.c:
    sub bx, ax                      ; BX = the chance of 256
    jle .out
    call OSAPI_RAND
    xor ah, ah
    cmp ax, bx
    jae .out                        ; missed
    call OSAPI_RAND
    mov cl, 2
    cmp byte [px_adist], 2
    jb .dmg
    inc cl
    cmp byte [px_adist], 4
    jb .dmg
    inc cl
.dmg:
    shr al, cl
    call px_player_hurt
.out:
    ret

; -----------------------------------------------------------------------------
; px_dog_bite - the dog at SI's leap lands (wave 6): the player still within a
;               tile and the line clear, PX_DOGBITE in 256 bites for a random
;               byte >> 4 (the 1992 engine's T_Bite); a player who stepped
;               away is missed
; clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
px_dog_bite:
    call px_act_deltas
    cmp byte [px_adist], 1
    ja .out
    call px_act_los
    jnc .out
    call OSAPI_RAND
    cmp al, PX_DOGBITE
    jae .out
    call OSAPI_RAND
    mov cl, 4
    shr al, cl
    call px_player_hurt
.out:
    ret

; -----------------------------------------------------------------------------
; px_act_hurt - AL damage to the actor at SI (97.8): doubled while it has
;               not seen the player, then pain or death
; clobbers AX, BX
; -----------------------------------------------------------------------------
px_act_hurt:
    cmp byte [si + PXAC_STATE], PXAS_DIE
    jae .out
    cmp byte [si + PXAC_KIND], PXK_DOG
    je .dies                        ; the dog: any hit, even a 0 (a knife's
                                    ; rnd >> 4 can be), kills it (PX_DOGHP)
    test byte [si + PXAC_FLAGS], PXAF_ATTACK
    jnz .once
    shl al, 1
.once:
    mov ah, [si + PXAC_HP]
    sub ah, al
    jbe .dies
    mov [si + PXAC_HP], ah
    mov byte [si + PXAC_STATE], PXAS_PAIN
    mov byte [si + PXAC_TIMER], 4
    or byte [si + PXAC_FLAGS], PXAF_ATTACK
    call px_act_dirty
    ret
.dies:
    mov byte [si + PXAC_HP], 0
    mov byte [si + PXAC_STATE], PXAS_DIE
    mov byte [si + PXAC_FRAME], 0
    mov byte [si + PXAC_TIMER], 4
    mov bx, [si + PXAC_CELL]
    call px_act_unmark              ; a body blocks nothing
    mov ax, 100
    cmp byte [si + PXAC_KIND], PXK_DOG
    jne .pts
    add ax, ax                      ; the dog: 200
.pts:
    add [px_score], ax
    inc byte [px_ckill]             ; the LEVELDONE card's kills (97.13)
    call px_act_dirty
.out:
    ret
