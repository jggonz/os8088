; =============================================================================
; os8088 - apps/tank/tkgame.inc
;
; THE SESSION (SPEC.md 85.6): the fsx bracket, the polled input model a
; bracket has instead of events, the simulation, and the panel.
; =============================================================================

; -----------------------------------------------------------------------------
; tk_cmd_play - take the machine (SPEC.md 53.1)
; in:  gfx lock held (menu / key / click context).  preserves all registers
; -----------------------------------------------------------------------------
tk_cmd_play:
    push ax
    push bx
    push cx
    cmp byte [tk_want], TKB_NONE
    je .out                         ; the same predicate the menu greyed on
    mov ax, tk_fsx_main
    mov bx, [tk_win]
    xor cx, cx                      ; no worker to keep: this loop IS the game
    call OSAPI_FSX_RUN              ; the kernel repaints the whole desktop on
.out:                               ; the way out (SPEC.md 53.6), and that
                                    ; repaint is our own W_PAINT - so the new
                                    ; score is on the panel already
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_fsx_main - the exclusive main (SPEC.md 53.1)
; in:  SI = our window, ES = KERNEL_SEG, DS = CS = ours, gfx lock held for the
;      whole session, every other task frozen
; out: near ret; the kernel restores the desktop
; -----------------------------------------------------------------------------
tk_fsx_main:
    push ds
    pop es
    mov di, tk_fsi
    mov al, [tk_fsxm]
    call OSAPI_FSX_MODE
    jc .out                         ; refused: the desktop is untouched and
                                    ; the bracket ends without drawing
    mov al, [tk_want]
    mov [tk_back], al
    call tk_bindraster
    call tk_r_setup
    jc .out
    call tk_devrows
    call tk_ridgesetup
    call tk_glyphs
    call tk_clearall
    mov byte [tk_par], 0
    mov byte [tk_quit], 0
    mov byte [tk_over], 0
    cmp byte [tk_lives], 0          ; A FINISHED GAME STARTS A NEW ONE (SPEC.md
    jne .keep                       ; 85.6). Only 'N' ever wrote tk_lives back,
    call tk_newgame                 ; so Play after a GAME OVER ran with an
.keep:                              ; empty bar: the first hit found 0 and the
                                    ; session ended before the first shell
                                    ; landed, with the finished game's score
                                    ; still on the panel. AFTER tk_r_setup's
                                    ; success, so a refused mode does not clear
                                    ; the score the attract panel is showing
    call tk_startround
    call OSAPI_GET_TICKS
    mov [tk_last], ax
.loop:
    call tk_input
    cmp byte [tk_quit], 0
    jne .out
    call tk_steps                   ; CX = ticks since the last frame
    jcxz .skip
    mov [tk_lstep], cl              ; ...and the lattice the assist gives back
                                    ; half of (SPEC.md 85.6.5.6): the player
                                    ; can only command a heading every
                                    ; TK_TURN * this.
                                    ;
                                    ; AND IT IS WRITTEN AFTER tk_input ON
                                    ; PURPOSE. tk_fire runs up there, so a shot
                                    ; is corrected with the SAME tk_lstep that
                                    ; tk_lockon closed the sight on at the end
                                    ; of the last frame. Written before it, a
                                    ; frame that owed two ticks instead of
                                    ; three would shrink the cap between the
                                    ; sight closing and the trigger, and the
                                    ; sight would have lied (85.6.5.7)
    cmp byte [tk_pause], 0
    jne .skip
    cmp byte [tk_over], 0           ; the world is finished with; what moves
    jne .skip                       ; now is the lettering
.sim:
    push cx
    call tk_pmove                   ; the player moves ON THE TICK, like
    call tk_update                  ; everything else - see tk_input
    pop cx
    loop .sim
.skip:
    cmp byte [tk_over], 2           ; A SETTLED GAME OVER IS A STILL PICTURE.
    jne .draw                       ; The world is not stepped, the lettering
    cmp byte [tk_still], 0          ; has landed and the radar has stopped, so
    jne .wait                       ; every frame from here is the last one
.draw:                              ; again - and PERFORMANCE.md's first rule
    cmp byte [tk_pquick], 0         ; is that nothing repaints more than it
    je .full                        ; changed. A letter typed into the initials
    mov byte [tk_pquick], 0         ; changes THREE LINES OF TEXT, so that is
    call tk_repoint                 ; all it redraws (SPEC.md 85.9.1)
    mov byte [tk_still], 1
    jmp .loop
.full:
    call tk_render                  ; ...and this is what makes 85.8.1's halo
    cmp byte [tk_over], 2           ; affordable: it is paid once when the
    jne .loop                       ; lettering settles, not once a frame
    mov byte [tk_still], 1
    jmp .loop
.wait:
    mov al, FSXW_TICK
    call OSAPI_FSX_WAIT
    jmp .loop
.out:
    ret

; -----------------------------------------------------------------------------
; tk_steps - how many SIMULATION steps this frame owes, and the wait if none
; out: CX = 1..TK_MAXSTEP.  clobbers AX, BX
;
; THE SIMULATION IS ON THE TICK AND THE DRAWING IS NOT, which is the only way
; to have both halves of what this game wants. A fixed step per FRAME makes a
; 386 play three times as fast as the XT this is built for; pacing every frame
; to the tick makes the XT WAIT - measured at 18.5% of its own frame, because
; a frame that overruns a tick by a fifth then rounds up to the next whole one.
; So: step the world once per elapsed tick, draw as often as the machine can,
; and wait only when there is nothing yet to step.
;
; The cap is what keeps a stall from teleporting anybody: a floppy access or a
; long first frame can leave several ticks owing, and a shell that advances
; four times in one frame can pass clean through a tank it should have hit.
; TK_MAXSTEP is declared in tank.asm beside TK_TURN, because the two multiply
; to the coarsest heading lattice the player is ever on and SPEC.md 85.6.5.8
; derives the aim box from it.
; -----------------------------------------------------------------------------
tk_steps:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [tk_last]
    jnz .have
    mov al, FSXW_TICK               ; nothing owing: the world cannot move
    call OSAPI_FSX_WAIT             ; yet, so do not burn the machine drawing
    call OSAPI_GET_TICKS            ; a frame identical to the last one
    mov bx, ax
    sub ax, [tk_last]
.have:
    mov [tk_last], bx
    cmp ax, TK_MAXSTEP
    jbe .ok
    mov ax, TK_MAXSTEP
.ok:
    mov cx, ax
    ret

; -----------------------------------------------------------------------------
; tk_bindraster - point the two indirect calls at this bracket's backend
; -----------------------------------------------------------------------------
tk_bindraster:
    mov si, tk_lp_modex
    mov di, tk_ep_modex
    mov word [tk_hrunproc], tk_hrun_modex
    mov word [tk_glyphproc], tk_glyph_modex
    mov byte [tk_kshift], 2
    cmp byte [tk_back], TKB_MODEX
    je .copy
    mov si, tk_lp_cga
    mov di, tk_ep_cga
    mov word [tk_hrunproc], tk_hrun_cga
    mov word [tk_glyphproc], tk_glyph_cga
    cmp byte [tk_back], TKB_CGA
    je .copy
    mov si, tk_lp_herc
    mov di, tk_ep_herc
    mov word [tk_hrunproc], tk_hrun_herc
    mov word [tk_glyphproc], tk_glyph_herc
    mov byte [tk_kshift], 3
.copy:                              ; the walk trio and the erase trio, six
    mov ax, [si]                    ; words, contiguous in bss so tk_gohalo
    mov [tk_lsh], ax                ; can swap them as one block
    mov ax, [si+2]
    mov [tk_lst], ax
    mov ax, [si+4]
    mov [tk_lvt], ax
    mov ax, [di]
    mov [tk_esh], ax
    mov ax, [di+2]
    mov [tk_est], ax
    mov ax, [di+4]
    mov [tk_evt], ax
    ret

; -----------------------------------------------------------------------------
; tk_clearall - both pages, or the whole shadow, once at entry
;
; The mode set clears what the card is SHOWING; page 1 of a Mode X flip and a
; freshly claimed shadow are neither of them that, and a frame drawn over
; either one starts with somebody else's bytes under it.
; -----------------------------------------------------------------------------
tk_clearall:
    push ds
    pop es
    mov ax, 0x00FF                  ; both span sets empty, so the first two
    cld                             ; frames clear nothing and draw everything
    mov di, tk_spans0
    mov cx, TK_MAXROW
    rep stosw
    mov di, tk_spans1
    mov cx, TK_MAXROW
    rep stosw
    mov di, tk_tmc                  ; every template key stale, so the first
    mov cx, TK_NTM * 2              ; frame draws the whole panel (85.3.5)
    mov ax, 0xFFFF
    rep stosw
    mov byte [tk_rtm_on], 0         ; ...and no ridge in the template (85.3.8)
    mov es, [tk_tseg]
    cmp byte [tk_back], TKB_MODEX
    jne .flat
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    xor di, di
    mov cx, 240 * 80 * 2 / 2        ; both pages, in words
    xor ax, ax
    rep stosw
    ret
.flat:
    xor di, di
    mov cx, TK_SHSEG / 2
    xor ax, ax
    rep stosw
    cmp byte [tk_tmpl], 0
    je .fr
    mov es, [tk_tmseg]              ; ...and the template beside it
    xor di, di
    mov cx, TK_SHSEG / 2
    rep stosw
.fr:
    ret

; -----------------------------------------------------------------------------
; tk_input - a bracket dispatches no events, so this is the whole input model
;            (SPEC.md 53.1)
;
; Two readers, because they answer two different questions. int 16h answers
; "what was TYPED" and is right for fire, pause and leaving; OSAPI_KEY_DOWN
; answers "what is HELD" and is the only thing that can drive a tank -
; SPEC.md 9.7 is about exactly this, and the typematic delay is what an app
; without it stutters on.
; -----------------------------------------------------------------------------
tk_input:
    push ax
    push bx
    push cx
    push dx
.keys:
    mov ah, 1
    int 0x16
    jz .held
    xor ah, ah
    int 0x16
    mov byte [tk_still], 0          ; whatever it turns out to mean, a key can
                                    ; change the picture
    mov byte [tk_pquick], 0
    call tk_ient_key                ; while a name is being typed EVERY key is
    jnc .free                       ; the prompt's, including the ones that
    cmp byte [tk_ient], 0           ; would otherwise leave the bracket - and
    je .keys                        ; while it is STILL up afterwards, nothing
    mov byte [tk_pquick], 1         ; but those three lines can have changed
    jmp short .keys
.free:
    cmp al, 27                      ; Esc
    je .quit
    cmp al, 'f'
    je .quit
    cmp al, 'F'
    je .quit
    cmp al, ' '
    je .fire
    cmp al, 'p'
    je .pause
    cmp al, 'P'
    je .pause
    cmp al, 'n'
    je .new
    cmp al, 'N'
    jne .keys
.new:
    mov byte [tk_over], 0
    call tk_newgame
    call tk_startround
    jmp short .keys
.pause:
    xor byte [tk_pause], 1
    jmp short .keys
.fire:
    call tk_fire
    jmp short .keys
.quit:
    mov byte [tk_quit], 1
    jmp .done


; THE HELD KEYS ARE LATCHED HERE AND SPENT ON THE TICK (SPEC.md 85.6.4).
; They used to be applied right here, once per FRAME, while the world stepped
; once per TICK - so the player turned at the machine's frame rate and the
; enemy turned at the clock's. On a 386 that is about 1.6 frames a tick and
; the tank swings round nicely; on the 4.77 MHz 8088 this game is FOR it is
; about a third of a frame a tick, so the same tank took five times as long to
; come about and the player could not make their turns and shots. Reported
; from a 5150 as exactly that. Everything below now writes the pair that
; tk_pmove spends, once per simulation step, so the two halves of a duel move
; on the same clock on every machine.
.held:
    mov byte [tk_kturn], 0
    mov word [tk_kdrv], 0
    cmp byte [tk_over], 0           ; nothing to drive once it is over
    jne .done
    cmp byte [tk_pause], 0
    jne .done
    cmp byte [tk_dead], 0           ; no driving while the screen is cracked
    jne .done
    xor bl, bl                      ; BL = the turn a step
    mov al, KSC_LEFT
    call OSAPI_KEY_DOWN
    jnc .a1
    sub bl, TK_TURN
.a1:
    mov al, TK_KA
    call OSAPI_KEY_DOWN
    jnc .a2
    sub bl, TK_TURN
.a2:
    mov al, KSC_RIGHT
    call OSAPI_KEY_DOWN
    jnc .a3
    add bl, TK_TURN
.a3:
    mov al, TK_KD
    call OSAPI_KEY_DOWN
    jnc .a4
    add bl, TK_TURN
.a4:
    mov [tk_kturn], bl

    xor bx, bx                      ; BX = the distance a step, signed
    mov al, KSC_UP
    call OSAPI_KEY_DOWN
    jnc .b1
    mov bx, TK_SPEED
.b1:
    mov al, TK_KW
    call OSAPI_KEY_DOWN
    jnc .b2
    mov bx, TK_SPEED
.b2:
    mov al, KSC_DOWN
    call OSAPI_KEY_DOWN
    jnc .b3
    mov bx, -TK_RSPEED
.b3:
    mov al, TK_KS
    call OSAPI_KEY_DOWN
    jnc .b4
    mov bx, -TK_RSPEED
.b4:
    mov [tk_kdrv], bx
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_pmove - spend one step of what tk_input latched
; -----------------------------------------------------------------------------
tk_pmove:
    push ax
    push bx
    mov al, [tk_kturn]
    add [tk_pa], al
    mov bx, [tk_kdrv]
    or bx, bx
    jz .out
    call tk_drive
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_aimerr - how far off the sights the object in slot BX (a slot x 2) is
; in:  BX = slot x 2, [tk_csin]/[tk_ccos] set for the CURRENT heading
; out: CF=0 and AX = the error in QUARTER angle units, positive when the target
;      is to the right, so tk_pa + AX/4 points at it; CF=1 not shootable
; clobbers CX, DX, SI, DI.  preserves BX
;
; NO ARCTANGENT HERE EITHER, and this one does not need SPEC.md 85.6's cross
; product: tk_objcam_raw already has the pair the projection divides, and over
; the few units either side of the sights that anything asks about, THE ANGLE
; IS ITS TANGENT. ocx/ocz = tan(err) and one unit is tan(1u) = 402/16384 -
; tk_sintab's own first entry - so err = 40.756 * ocx/ocz. In quarters, because
; a quarter is a useful step and a whole unit's rounding would cost a third of
; the window at the shell's longest reach (SPEC.md 85.6.5.2).
; -----------------------------------------------------------------------------
tk_aimerr:
    call tk_objcam_raw
    mov cx, [tk_ocz]
    cmp cx, TK_NEARZ                ; behind us, or close enough to ram
    jl .no
    cmp cx, TK_FARZ
    jg .no
    mov ax, [tk_ocx]                ; ...and inside 45 degrees, which is what
    or ax, ax                       ; bounds the quotient at 163 and so the
    jns .abs                        ; idiv itself. Everything this is asked
    neg ax                          ; about is within a few units of the sights
.abs:
    cmp ax, cx
    jg .no
    mov ax, [tk_ocx]
    mov si, TK_ERRK
    imul si                         ; DX:AX, because ocx*163 leaves 16 bits
    idiv cx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tk_besttarget - the live tank nearest the sights, if one is inside AX
; in:  AX = the half-window, in quarter units, inclusive
; out: CF=0 and AX = that tank's error in quarter units, [tk_aimz] = its range;
;      CF=1 none
; clobbers AX, BX, CX, DX, SI, DI
;
; It takes the camera's own sine and cosine first, because LOCK asks this in
; the middle of a step and tk_camsetup last ran a frame and a heading ago.
; -----------------------------------------------------------------------------
tk_besttarget:
    call tk_camsetup
    mov [tk_aimw], ax
    mov word [tk_aimb], 0x7FFF
    mov bx, TK_NSTAT * 2            ; the scenery owns the first TK_NSTAT slots
    mov cx, TK_NOBJ - TK_NSTAT      ; and tk_findfree's rule holds here too
.o:
    push cx
    push bx
    mov si, bx
    shr si, 1
    cmp byte [tk_otype + si], OT_TANK
    jne .n
    call tk_aimerr
    jc .n
    mov dx, ax
    or ax, ax
    jns .p
    neg ax
.p:
    cmp ax, [tk_aimw]
    jg .n
    cmp ax, [tk_aimb]
    jge .n
    mov [tk_aimb], ax
    mov [tk_aimq], dx
    mov ax, [tk_ocz]                ; the RANGE decides the hit window, and
    mov [tk_aimz], ax               ; tk_ocz is the last object looked at
.n:
    pop bx
    add bx, 2
    pop cx
    loop .o
    cmp word [tk_aimb], 0x7FFF
    je .none
    mov ax, [tk_aimq]
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; tk_lockon - would a shot fired RIGHT NOW hit? (SPEC.md 85.6.5.7)
; out: [tk_locked] set, and a blip on the frame it ACQUIRES
; clobbers AX, BX, CX, DX, SI, DI
;
; THE SIGHT MAY NOT LIE, and that is the whole design of it. A reticle keyed to
; the BRACKETS would say "on target" for a tank five units off at 3,900 out,
; where tk_espoil's window is one and a half - so it would close on shots that
; miss, at exactly the range where the player cannot tell. It is keyed to the
; window instead: TK_HITQ/range is how far off a shot may be and still land,
; and tk_aimfix is how much of an error the gun is about to correct, so the
; sight closes on precisely the shots that hit.
;
; It follows that AIM OFF gets a reticle that closes rarely at long range and
; freely up close, which is not a lesser version of the feature - it is the
; defect being SHOWN. The brackets are still the outer gate, because a tank
; outside them is not one the player is aiming at.
; -----------------------------------------------------------------------------
tk_lockon:
    mov byte [tk_locked], 0
    cmp byte [tk_over], 0
    jne .edge
    cmp byte [tk_dead], 0           ; nothing is aimed through cracked glass
    jne .edge
    mov ax, TK_RETQ
    call tk_besttarget
    jc .edge
    mov cx, ax
    call tk_aimfix                  ; ...less whatever the gun is going to fix
    sub cx, ax                      ; CX = what the shell is still wrong by
    mov ax, cx
    or ax, ax
    jns .p
    neg ax
.p:
    mov bx, ax                      ; BX = |that|, in quarter units
    mov ax, TK_HITQ                 ; ...against the window at THIS range
    xor dx, dx
    div word [tk_aimz]
    mov cx, ax
.hys:
    cmp byte [tk_lockwas], 0        ; a target ON the boundary would otherwise
    je .cmp                         ; chatter the sight and its blip
    add cx, TK_LOCKHYS
.cmp:
    cmp bx, cx
    ja .edge
    mov byte [tk_locked], 1
.edge:
    mov al, [tk_locked]             ; the blip is an ACQUISITION, so it is the
    cmp al, [tk_lockwas]            ; EDGE that sounds and not the state
    je .out
    mov [tk_lockwas], al
    or al, al
    jz .out
    mov ax, TK_BLIPHZ
    mov cx, 1
    mov dl, TK_BLIPPRI              ; under the muzzle's, so a shot always wins
    call OSAPI_SND_TONE
.out:
    ret

; -----------------------------------------------------------------------------
; tk_aimfix - the correction the gun applies to an error of AX quarter units
; in:  AX = the signed error, from tk_besttarget
; out: AX = the signed correction, 0 for none.  clobbers nothing else
;
; ONE ROUTINE FOR THE GUN AND THE SIGHT (SPEC.md 85.6.5.6), which is what makes
; the closed reticle a promise rather than a decoration: tk_fire adds what this
; returns and tk_lockon subtracts it, so the sight closes on exactly the shots
; that land and the two cannot drift apart.
;
; THE BOX IS THE GATE, and outside it the player aims for themselves
; (SPEC.md 85.6.5.8). It is TK_BOXQ wide because that is half the coarsest
; lattice they can command, so the nearest heading to any bearing is inside the
; box AND inside the cap - the correction is exact there, and there is no
; bearing a sequence of presses cannot reach.
;
; THE CAP IS HALF THE LATTICE THIS FRAME PUTS THEM ON, and nothing more: the
; player can only command a heading every TK_TURN * tk_lstep units, so half of
; that is what the frame rate ROUNDED AWAY and anything further is aiming for
; them. Three units on a 5150 and one on a 386 - the assist is exactly the
; machine's own handicap - and at 1,000 units out, where tk_espoil's window is
; already 12 units wide, it changes no outcome at all.
; -----------------------------------------------------------------------------
tk_aimfix:
    push bx
    push cx
    mov cx, ax                      ; CX = the error, sign and all
    or ax, ax
    jns .p
    neg ax
.p:
    cmp ax, TK_BOXQ                 ; outside the closed sight's own box the
    ja .none                        ; gun does nothing at all
    mov al, [tk_lstep]
    mov ah, TK_TURN * 2             ; half the lattice, in quarter units:
    mul ah                          ; TK_TURN * lstep * 4 / 2. tank.asm asserts
    or ax, ax                       ; this cannot exceed TK_BOXQ, so nothing
    jz .none                        ; clamps it here
    mov bx, ax                      ; BX = the cap; clamp the error to it
    mov ax, cx
    cmp ax, bx
    jle .lo
    mov ax, bx
    jmp short .out
.lo:
    neg bx
    cmp ax, bx
    jge .out
    mov ax, bx
.out:
    pop cx
    pop bx
    ret
.none:
    xor ax, ax
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tk_qround - AX = an error in quarter units; out AL = it in whole ones, ROUNDED
; -----------------------------------------------------------------------------
tk_qround:
    or ax, ax
    jns .p
    neg ax
    add ax, 2
    sar ax, 1
    sar ax, 1
    neg ax
    ret
.p:
    add ax, 2
    sar ax, 1
    sar ax, 1
    ret

; -----------------------------------------------------------------------------
; tk_drive - move the player BX units along its heading, unless something is
;            already standing there
; -----------------------------------------------------------------------------
tk_drive:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx                      ; SI = the signed distance
    mov al, [tk_pa]
    call tk_sincos                  ; AX = sin, BX = cos
    mov cx, bx
    mov bx, si
    push cx
    MUL14                           ; dx = sin * dist >> 14
    mov [tk_ndx], ax
    pop ax                          ; cos
    mov bx, si
    MUL14
    mov [tk_ndz], ax
    mov ax, [tk_px]
    add ax, [tk_ndx]
    and ax, TK_WMASK
    mov [tk_nx], ax
    mov ax, [tk_pz]
    add ax, [tk_ndz]
    and ax, TK_WMASK
    mov [tk_nz], ax
    call tk_blocked
    jnc .move
    push word [tk_nx]               ; refused - but are we ALREADY inside
    push word [tk_nz]               ; something? Then every direction is
    mov ax, [tk_px]                 ; refused and this is the dead end of
    mov [tk_nx], ax                 ; SPEC.md 85.6.6, so the move is an escape
    mov ax, [tk_pz]                 ; and is allowed. TK_CLEARR means it cannot
    mov [tk_nz], ax                 ; happen; this means it cannot come back by
    call tk_blocked_scen            ; some other route, and it changes nothing
    pop word [tk_nz]                ; in a game where the player is outside
    pop word [tk_nx]                ; everything - which is every game.
    jnc .out                        ; SCENERY only: tk_blocked counts an enemy
                                    ; tank too, and one within 300 units - the
                                    ; ramming range - made the player's own
                                    ; position "blocked", so every step was
                                    ; allowed, into a cube included, and once
                                    ; inside it the position stayed blocked
                                    ; and the player drove through the piece
.move:
    mov ax, [tk_nx]
    mov [tk_px], ax
    mov ax, [tk_nz]
    mov [tk_pz], ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_blocked - would the player standing at [tk_nx]/[tk_nz] be inside a solid?
; out: CF=1 blocked.  clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
; tk_blocked_scen - the same question of the SCENERY alone: a tank is not a
;                   wall you can be inside of. CF as tk_blocked; the bound
;                   byte is restored on the way out with a mov, so CF survives
; -----------------------------------------------------------------------------
tk_bltop:   db OT_SHELL             ; the first type tk_blocked drives THROUGH:
                                    ; OT_SHELL (your own fire) by default,
                                    ; OT_TANK for the scenery-only question.
                                    ; Image data, never executed: a `ret`
                                    ; precedes it and a label follows
tk_blocked_scen:
    mov byte [tk_bltop], OT_TANK
    call tk_blocked
    mov byte [tk_bltop], OT_SHELL
    ret

tk_blocked:
    xor bx, bx
    mov cx, TK_NOBJ
.o:
    mov si, bx
    shr si, 1
    mov al, [tk_otype + si]
    or al, al
    jz .next
    cmp al, [tk_bltop]
    jae .next                       ; you may drive through your own fire
                                    ; (OT_SHELL and above), or - asked by
                                    ; tk_blocked_scen - through a tank
    mov ax, [tk_ox + bx]
    sub ax, [tk_nx]
    call tk_wrap
    or ax, ax
    jns .xa
    neg ax
.xa:
    cmp ax, 300
    jge .next
    mov dx, ax
    mov ax, [tk_oz + bx]
    sub ax, [tk_nz]
    call tk_wrap
    or ax, ax
    jns .za
    neg ax
.za:
    cmp ax, 300
    jge .next
    stc
    ret
.next:
    add bx, 2
    loop .o
    clc
    ret

; -----------------------------------------------------------------------------
; tk_update - one frame of simulation
; clobbers everything
; -----------------------------------------------------------------------------
tk_update:
    cmp byte [tk_dead], 0
    je .live
    dec byte [tk_dead]
    jnz .out                        ; the cracked screen holds the world still
    call tk_respawn
    jmp short .out
.live:
    cmp byte [tk_spawn], 0
    je .objs
    dec byte [tk_spawn]
    jnz .objs
    call tk_addtank
.objs:
    xor bx, bx
    mov cx, TK_NOBJ
.o:
    push cx
    mov [tk_sl2], bx
    shr bx, 1
    mov [tk_sl], bx
    mov si, bx
    mov al, [tk_otype + si]
    cmp al, OT_TANK
    je .tank
    cmp al, OT_SHELL
    jb .next
    call tk_shell
    jmp short .next
.tank:
    call tk_tankai
.next:
    mov bx, [tk_sl2]
    add bx, 2
    pop cx
    loop .o
.out:
    ret

; -----------------------------------------------------------------------------
; tk_ofwd - move object [tk_sl] forward BX units along its own heading
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
tk_ofwd:
    mov si, bx                      ; SI = the distance
    mov bl, [tk_sl]
    xor bh, bh
    mov al, [tk_oa + bx]
    call tk_sincos                  ; AX = sin, BX = cos
    mov cx, bx
    mov bx, si
    push cx
    MUL14
    mov di, [tk_sl2]
    add [tk_ox + di], ax
    and word [tk_ox + di], TK_WMASK
    pop ax
    mov bx, si
    MUL14
    mov di, [tk_sl2]
    add [tk_oz + di], ax
    and word [tk_oz + di], TK_WMASK
    ret

; -----------------------------------------------------------------------------
; tk_shell - one shell in flight: move it, age it, and see what it hit
; -----------------------------------------------------------------------------
tk_shell:
    mov bx, TK_SHVEL
    call tk_ofwd
    mov bl, [tk_sl]
    xor bh, bh
    dec byte [tk_otim + bx]
    jnz .alive
    mov byte [tk_otype + bx], OT_FREE
    ret
.alive:
    call tk_shellwall               ; scenery stops a shell, whoever fired it
    jc .gone
    mov si, [tk_sl]
    cmp byte [tk_otype + si], OT_ESHELL
    je .theirs
    ; --- ours: does it meet a tank? ---------------------------------------
    mov bx, [tk_sl2]
    mov ax, [tk_ox + bx]
    mov [tk_hx], ax
    mov ax, [tk_oz + bx]
    mov [tk_hz], ax
    xor bx, bx
    mov cx, TK_NOBJ
.t:
    mov si, bx
    shr si, 1
    cmp byte [tk_otype + si], OT_TANK
    jne .tn
    call tk_near
    jnc .tn
    mov byte [tk_otype + si], OT_FREE
    mov si, [tk_sl]
    mov byte [tk_otype + si], OT_FREE
    add word [tk_score], 1000
    adc word [tk_score + 2], 0
    mov byte [tk_spawn], 45         ; the next one is on its way
    ret
.tn:
    add bx, 2
    loop .t
    ret
.gone:
    mov si, [tk_sl]
    mov byte [tk_otype + si], OT_FREE
    ret
.theirs:
    ; --- theirs: does it meet us? ------------------------------------------
    mov bx, [tk_sl2]
    mov ax, [tk_ox + bx]
    mov [tk_hx], ax
    mov ax, [tk_oz + bx]
    mov [tk_hz], ax
    mov ax, [tk_px]
    mov [tk_tx], ax
    mov ax, [tk_pz]
    mov [tk_tz], ax
    call tk_nearpt
    jnc .miss
    mov si, [tk_sl]
    mov byte [tk_otype + si], OT_FREE
    call tk_hitme
.miss:
    ret

; -----------------------------------------------------------------------------
; tk_shellwall - did the shell at [tk_sl] just fly into the scenery?
; out: CF=1 it did.  clobbers AX, BX, CX, DX, SI
;
; EVERY STEP, and not once at the muzzle, because a ray cast at the moment of
; firing cannot know where a TANK will be by the time the shell arrives - and
; the two tests then disagree about what a shot can reach. Twelve box tests a
; shell a step is about 0.6 ms a frame on the target machine, which is a
; twentieth of what the walk costs; the once-on-fire version buys nothing
; worth the second answer.
; -----------------------------------------------------------------------------
tk_shellwall:
    mov bx, [tk_sl2]
    mov ax, [tk_ox + bx]
    mov [tk_hx], ax
    mov ax, [tk_oz + bx]
    mov [tk_hz], ax
    xor bx, bx
    mov cx, TK_NSTAT
.o:
    mov si, bx
    shr si, 1
    mov al, [tk_otype + si]
    or al, al
    jz .n
    xor ah, ah
    mov si, ax
    mov al, [tk_rstop + si]         ; this shape's own reach
    xor ah, ah
    mov [tk_wallr], ax
    mov ax, [tk_ox + bx]
    mov [tk_tx], ax
    mov ax, [tk_oz + bx]
    mov [tk_tz], ax
    call tk_nearw
    jc .hit
.n:
    add bx, 2
    loop .o
    clc
    ret
.hit:
    stc
    ret

; tk_near - is object BX (a slot x2) within TK_HITR of [tk_hx]/[tk_hz]?
; out: CF=1 yes.  preserves BX and CX
tk_near:
    push ax
    push dx
    mov ax, [tk_ox + bx]
    mov [tk_tx], ax
    mov ax, [tk_oz + bx]
    mov [tk_tz], ax
    pop dx
    pop ax
    ; fall through

; tk_nearpt - [tk_hx]/[tk_hz] against [tk_tx]/[tk_tz], a box test
; out: CF=1 within.  preserves BX and CX
tk_nearpt:
    mov word [tk_wallr], TK_HITR
    ; fall through

; tk_nearw - the same box test at [tk_wallr] instead of TK_HITR
tk_nearw:
    push ax
    mov ax, [tk_tx]
    sub ax, [tk_hx]
    call tk_wrap
    or ax, ax
    jns .x
    neg ax
.x:
    cmp ax, [tk_wallr]
    jge .no
    mov ax, [tk_tz]
    sub ax, [tk_hz]
    call tk_wrap
    or ax, ax
    jns .z
    neg ax
.z:
    cmp ax, [tk_wallr]
    jge .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; tk_tankai - turn toward the player, close the distance, and shoot
;
; NO ARCTANGENT. The sign of the cross product of the tank's forward vector
; with the vector to the player says which way to turn, and the dot product
; says whether the player is in front at all; "aimed" is then |cross| small
; against the range. Four multiplies a tank a frame, and not one divide.
; -----------------------------------------------------------------------------
tk_tankai:
    mov bx, [tk_sl2]
    mov ax, [tk_px]
    sub ax, [tk_ox + bx]
    call tk_wrap
    mov [tk_adx], ax
    mov ax, [tk_pz]
    sub ax, [tk_oz + bx]
    call tk_wrap
    mov [tk_adz], ax

    mov bl, [tk_sl]
    xor bh, bh
    mov al, [tk_oa + bx]
    call tk_sincos
    mov [tk_osin], ax
    mov [tk_ocos], bx

    mov ax, [tk_osin]               ; cross = sin*dz - cos*dx
    mov bx, [tk_adz]
    MUL14
    mov cx, ax
    mov ax, [tk_ocos]
    mov bx, [tk_adx]
    MUL14
    neg ax
    add ax, cx
    mov [tk_across], ax

    mov ax, [tk_osin]               ; dot = sin*dx + cos*dz
    mov bx, [tk_adx]
    MUL14
    mov cx, ax
    mov ax, [tk_ocos]
    mov bx, [tk_adz]
    MUL14
    add ax, cx
    mov [tk_adot], ax

    mov bl, [tk_sl]                 ; A TANK STILL INSIDE ITS GRACE WANDERS
    xor bh, bh                      ; (SPEC.md 85.6.1). It tracks at a quarter
    inc byte [tk_otim + bx]         ; rate and closes at a third speed, so it
    mov byte [tk_slow], 0           ; arrives late and off the beam instead of
    cmp byte [tk_ocool + bx], TK_HUNT ; bearing down on the sights from the
    jbe .hunt                       ; first frame. tk_otim is free on a tank -
    mov byte [tk_slow], 1           ; only a shell uses it as a life
    test byte [tk_otim + bx], 3
    jnz .aim                        ; three steps in four it does not turn
.hunt:
    mov ax, [tk_across]             ; turn toward, one unit a step - slower
    or ax, ax                       ; than the player, which is what makes
    jz .aim                         ; flanking a tank the way to beat it
    jns .right
    inc byte [tk_oa + bx]
    jmp short .aim
.right:
    dec byte [tk_oa + bx]
.aim:
    ; THE SIGN IS DERIVED AND NOT GUESSED, because a cross product alone has
    ; TWO zeroes - facing the target and facing away from it - and only one of
    ; them is the one to settle on. d(cross)/da is the DOT product, so with
    ; the player in front (dot > 0) raising the heading raises the cross, and
    ; the way to drive a positive cross to zero is to LOWER it. The other
    ; convention is stable at dot < 0, which is a tank that turns its back and
    ; then can neither close nor fire: measured, at dot = -3570 for ever.
    ; close, but stop short: a tank that drives into you is not a duel
    mov ax, [tk_adot]
    cmp ax, 700
    jl .shoot
    mov bx, 10
    cmp byte [tk_slow], 0
    je .fast
    mov bx, 4
.fast:
    call tk_ofwd
.shoot:
    mov bl, [tk_sl]
    xor bh, bh
    cmp byte [tk_ocool + bx], 0
    je .ready
    dec byte [tk_ocool + bx]
    ret
.ready:
    cmp word [tk_adot], 0           ; behind us: nothing to shoot at
    jle .no
    mov ax, [tk_across]             ; aimed? |cross| under a sixteenth of the
    or ax, ax                       ; range is about three and a half degrees
    jns .p
    neg ax
.p:
    mov cx, [tk_adot]
    mov dx, cx
    mov cl, 4
    shr dx, cl
    cmp ax, dx
    jg .no
    call tk_efire
.no:
    ret

; -----------------------------------------------------------------------------
; tk_findfree - a free MOVER slot (the scenery owns the first TK_NSTAT)
; out: CF=0 and BX = slot x 2, SI = slot; CF=1 none.  clobbers AX, CX
; -----------------------------------------------------------------------------
tk_findfree:
    mov si, TK_NSTAT
    mov cx, TK_NOBJ - TK_NSTAT
.s:
    cmp byte [tk_otype + si], OT_FREE
    je .got
    inc si
    loop .s
    stc
    ret
.got:
    mov bx, si
    shl bx, 1
    clc
    ret

; -----------------------------------------------------------------------------
; tk_spawnshell - a shell leaving AL's muzzle
; in:  AL = the OT_*, [tk_sx]/[tk_sz] = where from, AH = the heading
; -----------------------------------------------------------------------------
tk_spawnshell:
    push ax
    call tk_findfree
    jc .out
    pop ax
    mov [tk_otype + si], al
    mov [tk_oa + si], ah
    mov byte [tk_otim + si], TK_SHLIFE
    push ax
    mov ax, [tk_sx]
    mov [tk_ox + bx], ax
    mov ax, [tk_sz]
    mov [tk_oz + bx], ax
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_fire - the player's gun. ONE SHELL AT A TIME, which is the arcade's rule
;           and is what makes a missed shot cost something.
; -----------------------------------------------------------------------------
tk_fire:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [tk_dead], 0
    jne .out
    cmp byte [tk_over], 0
    jne .out
    mov si, TK_NSTAT
    mov cx, TK_NOBJ - TK_NSTAT
.s:
    cmp byte [tk_otype + si], OT_SHELL
    je .out
    inc si
    loop .s
    mov al, [tk_pa]                 ; A tank inside the CLOSED sight's box is
    mov [tk_aimh], al               ; one the player has aimed at themselves
    mov ax, TK_RETQ                 ; (SPEC.md 85.6.5.8), and only there does
    call tk_besttarget              ; the shell leave at its bearing rather
                                    ; than at the hull's
    jc .have
    call tk_aimfix
    call tk_qround
    add [tk_aimh], al
.have:
    mov al, [tk_aimh]
    call tk_sincos                  ; AX = sin, BX = cos
    mov cx, bx
    mov bx, 250                     ; out of the barrel, not out of the eye
    push cx
    MUL14
    add ax, [tk_px]
    and ax, TK_WMASK
    mov [tk_sx], ax
    pop ax
    mov bx, 250
    MUL14
    add ax, [tk_pz]
    and ax, TK_WMASK
    mov [tk_sz], ax
    mov al, OT_SHELL
    mov ah, [tk_aimh]
    call tk_spawnshell
    mov ax, 900                     ; the muzzle report
    mov cx, 2
    mov dl, 0x40
    call OSAPI_SND_TONE
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_efire - the tank at [tk_sl] shoots
; -----------------------------------------------------------------------------
tk_efire:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [tk_sl2]
    mov ax, [tk_ox + bx]
    mov [tk_sx], ax
    mov ax, [tk_oz + bx]
    mov [tk_sz], ax
    call OSAPI_RAND                 ; A SPREAD, or the duel is not one: the
    and al, 7                       ; aim gate is about three and a half
    sub al, 4                       ; degrees, and at close range the shell's
    mov [tk_espread], al            ; own radius is wider than that - so a tank
                                    ; that fires when aimed cannot miss. Four
                                    ; units of heading either way is about its
                                    ; own width at 2,000 units out
    call tk_espoil                  ; ...unless the schedule says this one is
    cmp byte [tk_espoiled], 0       ; meant to go wide, and then the derived
    je .aimed                       ; miss REPLACES the spread rather than
    mov [tk_espread], al            ; adding to it: the two are the same
.aimed:                             ; quantity and a -4 spread against a +5
                                    ; spoil is a hit that was meant to miss
    mov si, [tk_sl]
    mov al, OT_ESHELL
    mov ah, [tk_oa + si]
    add ah, [tk_espread]
    call tk_spawnshell
    mov si, [tk_sl]
    mov byte [tk_ocool + si], TK_COOL
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_espoil - the deliberate miss (SPEC.md 85.6.3)
; out: AL = extra heading units, signed, 0 if this shot is meant to arrive
;      clobbers nothing else
;
; DERIVED FROM THE RANGE, because a fixed offset is not a miss. A heading error
; of k units puts the shell 2*pi*R*k/256 across the player's path, and the
; player is TK_HITR wide, so anything under TK_HITR*256/(2*pi*R) - about
; 6,100/R - still hits. At the 700 units a tank closes to that is nine units,
; and at 3,000 it is two: one figure for both either misses everything or
; misses nothing.
; -----------------------------------------------------------------------------
tk_espoil:
    push bx
    push cx
    push dx
    mov byte [tk_espoiled], 1
    mov bx, [tk_ntank]              ; the accuracy this far into the game
    cmp bx, TK_CURVE
    jbe .n
    mov bx, TK_CURVE
.n:
    mov cl, [tk_acccurve + bx]
    call OSAPI_RAND
    xor dx, dx
    mov bx, 100
    div bx                          ; DX = 0..99
    xor ch, ch
    cmp dx, cx
    jb .hit                         ; inside the accuracy: let it fly true
    mov bx, [tk_adot]               ; the range, near enough - the shell leaves
    cmp bx, 64                      ; along the tank's own heading
    jg .r
    mov bx, 64
.r:
    mov ax, 6100
    xor dx, dx
    div bx
    cmp ax, 40                      ; more than a sixth of a turn is not a
    jbe .k                          ; miss, it is a tank shooting sideways
    mov ax, 40
.k:
    add ax, 2                       ; ...and clear of the edge of the radius
    mov cx, ax
    call OSAPI_RAND
    and ax, 3
    add ax, cx
    mov cx, ax
    call OSAPI_RAND                 ; either side, evenly
    test al, 1
    jz .pos
    neg cx
.pos:
    mov ax, cx
    jmp short .out
.hit:
    xor ax, ax
    mov [tk_espoiled], al
.out:
    pop dx
    pop cx
    pop bx
    ret

; How much of a start the player gets, and how straight the enemy shoots, by
; the number of tanks that have come at them (SPEC.md 85.6.3). The AIM stops
; improving at the tenth tank; the TIME goes on tightening to the twentieth,
; where a game is at full difficulty and stays there. Both tables are indexed
; by min(tanks, TK_CURVE), so the last entry is the one a long game lives on.
TK_CURVE equ 20
tk_gracurve:                        ; SIGNED ticks against TK_GRACE. Two
    db 36, 32, 29, 25, 22           ; seconds of allowance on the first tank,
    db 18, 14, 11, 7, 4             ; down to none at the tenth - and then
    db 0                            ; THREE SECONDS BELOW IT by the twentieth,
    db -6, -11, -17, -22, -28       ; which is where a game stops getting
    db -33, -39, -44, -50, -55      ; harder
tk_acccurve:                        ; ...and the percentage of shots meant to
    db 50, 53, 56, 60, 63           ; arrive, which stops climbing at the tenth
    db 66, 70, 73, 76, 80
    db 80, 80, 80, 80, 80
    db 80, 80, 80, 80, 80, 80

; -----------------------------------------------------------------------------
; tk_hitme - a shell found the player
; -----------------------------------------------------------------------------
tk_hitme:
    push ax
    push bx
    push cx
    push dx
    mov byte [tk_dead], 22
    xor bx, bx                      ; every live tank starts its wait again:
    mov cx, TK_NOBJ                 ; they have been aiming right through the
.re:                                ; freeze, and a crack that clears into the
    cmp byte [tk_otype + bx], OT_TANK ; next shell is the same complaint in a
    jne .rn                         ; different place
    mov byte [tk_ocool + bx], TK_REGRACE
    mov byte [tk_otim + bx], 0
.rn:
    inc bx
    loop .re
    cmp byte [tk_lives], 0
    je .done
    dec byte [tk_lives]
.done:
    mov ax, 120
    mov cx, 8
    mov dl, 0x40
    call OSAPI_SND_TONE
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_respawn - the crack has faded
; -----------------------------------------------------------------------------
tk_respawn:
    cmp byte [tk_lives], 0
    jne .on
    call tk_gostart                 ; out of tanks
.on:
    ret

; -----------------------------------------------------------------------------
; tk_addtank - a fresh enemy, out of sight and coming
; -----------------------------------------------------------------------------
tk_addtank:
    push ax
    push bx
    push cx
    push dx
    push si
    call tk_findfree
    jc .out
    mov [tk_nsl], si
    mov [tk_nsl2], bx
    mov byte [tk_otype + si], OT_TANK
    mov al, TK_GRACE                ; ...plus the beginner's bonus, which the
    mov bx, [tk_ntank]              ; tenth tank has taken all the way off
    cmp bx, TK_CURVE
    jbe .ok
    mov bx, TK_CURVE
.ok:
    add al, [tk_gracurve + bx]
    mov si, [tk_nsl]
    mov [tk_ocool + si], al
    mov byte [tk_otim + si], 0
    inc word [tk_ntank]
    call OSAPI_RAND
    mov [tk_ra2], al                ; a bearing...
    call OSAPI_RAND
    and ax, 1023
    add ax, 3000                    ; ...and a range of 3000..4023, which is
    mov [tk_rrange], ax             ; outside the draw distance of nothing and
    mov al, [tk_ra2]                ; inside it by enough to be seen coming
    call tk_sincos
    mov [tk_osin], ax
    mov [tk_ocos], bx
    mov ax, [tk_osin]
    mov bx, [tk_rrange]
    MUL14
    add ax, [tk_px]
    and ax, TK_WMASK
    mov bx, [tk_nsl2]
    mov [tk_ox + bx], ax
    mov ax, [tk_ocos]
    mov bx, [tk_rrange]
    MUL14
    add ax, [tk_pz]
    and ax, TK_WMASK
    mov bx, [tk_nsl2]
    mov [tk_oz + bx], ax
    mov si, [tk_nsl]                ; facing back the way it came, which is
    mov al, [tk_ra2]                ; at us
    add al, 128
    mov [tk_oa + si], al
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_newgame / tk_startround / tk_hs_top
; -----------------------------------------------------------------------------
tk_newgame:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [tk_frames], 0
    mov word [tk_score], 0
    mov word [tk_score+2], 0
    mov byte [tk_lives], TK_LIVES
    mov word [tk_ntank], 0          ; the difficulty curve starts again with
    mov byte [tk_dead], 0           ; the game, not with the round (85.6.3)
    mov word [tk_px], 0
    mov word [tk_pz], 0
    mov byte [tk_pa], 0
    xor si, si                      ; the scenery, scattered over the torus
    mov cx, TK_NSTAT
.s:
    mov al, cl                      ; three shapes in rotation, so a player
    and al, 3                       ; always has all three to navigate by
    or al, al
    jnz .ty
    mov al, OT_BLOCK
.ty:
    mov [tk_otype + si], al
    mov bx, si
    shl bx, 1
    ; A JITTERED GRID, NOT A UNIFORM SCATTER (SPEC.md 85.6.6.3): slot s takes
    ; column s & 3 of four 2,048-unit columns and row s >> 2 of three
    ; 2,731-unit rows, and lands in the middle half of its cell. Neighbours
    ; are then never nearer than 1,025 units, and no view can hold the
    ; cluster a uniform draw put in one in six games.
    call OSAPI_RAND
    and ax, 1023
    add ax, 512
    mov dx, si                      ; DX = the column, i
    and dx, 3
    mov dh, dl                      ; i x 2048 = i << 11
    xor dl, dl
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add ax, dx
    mov [tk_ox + bx], ax
    call OSAPI_RAND
    mov dx, 1366                    ; 0..1365, with no divide: the high word
    mul dx                          ; of rand x 1366
    mov di, dx
    add di, 683                     ; DI = the jitter within the row
    mov ax, si                      ; j = s >> 2
    shr ax, 1
    shr ax, 1
    mov dx, 2731
    mul dx                          ; AX = j x 2731
    add ax, di
    mov [tk_oz + bx], ax
    mov ax, [tk_ox + bx]            ; ...AND NOT ON TOP OF THE PLAYER (SPEC.md
    sub ax, [tk_px]                 ; 85.6.6). A box test, like tk_blocked's
    call tk_wrap                    ; own, so clearing it on EITHER axis is
    or ax, ax                       ; enough - and x is the one shoved, because
    jns .qx                         ; the piece only has to leave the box, not
    neg ax                          ; go anywhere in particular
.qx:
    cmp ax, TK_CLEARR
    jge .qok
    mov ax, [tk_oz + bx]
    sub ax, [tk_pz]
    call tk_wrap
    or ax, ax
    jns .qz
    neg ax
.qz:
    cmp ax, TK_CLEARR
    jge .qok
    add word [tk_ox + bx], TK_CLEARR * 2    ; a FOLD and not a re-draw: it
    and word [tk_ox + bx], TK_WMASK         ; terminates, and a retry loop on
.qok:                                       ; OSAPI_RAND is a hang waiting for
                                            ; a bad one
    mov byte [tk_oa + si], 0
    inc si
    dec cx                          ; the body outgrew `loop`'s reach
    jz .placed
    jmp .s
.placed:
    mov cx, TK_NOBJ - TK_NSTAT      ; ...and no movers yet
.m:
    mov byte [tk_otype + si], OT_FREE
    inc si
    loop .m
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tk_startround:
    push ax
    mov byte [tk_dead], 0
    mov byte [tk_spawn], 0
    mov byte [tk_pause], 0          ; ...AND A ROUND IS NEVER PAUSED. It is
                                    ; here rather than at each entry because
                                    ; there are two: the bracket's own, and
                                    ; 'N' inside it - and 'N' did not clear it,
                                    ; so P then N drew a fresh world that
                                    ; nothing stepped and no key reached
    call tk_addtank
    pop ax
    ret

; tk_hs_top - the panel's HIGH is the table's first row, which is the only
; high score that survives the session
tk_hs_top:
    push ax
    mov ax, [tk_hs]
    mov [tk_hiscore], ax
    mov ax, [tk_hs+2]
    mov [tk_hiscore+2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_render - one whole frame
; -----------------------------------------------------------------------------
tk_render:
    inc word [tk_frames]            ; the only instrumentation in here, and
    call tk_camsetup                ; tests/tank.py is what reads it
    call tk_r_begin
    cmp byte [tk_tmpl], 0           ; the static panel, kept current in the
    je .nt                          ; template (SPEC.md 85.3.5) - AFTER the
    call tk_tmupdate                ; clear, which has just copied the old
.nt:                                ; template over last frame's runs
    call tk_ridge_tm                ; the ridge: template while the heading
                                    ; holds, dynamic while it turns (85.3.8)
    mov al, OT_CUBE
    call tk_drawtype
    mov al, OT_PYR
    call tk_drawtype
    mov al, OT_BLOCK
    call tk_drawtype
    call tk_drawmovers
    call tk_hud
    cmp byte [tk_over], 0
    je .glass
    call tk_gover
.glass:
    call tk_r_end
    ret

; -----------------------------------------------------------------------------
; tk_hud_layout - where the panel's parts sit, derived from the viewport
;
; Nothing here is a screen constant. The three viewports are 320x240, 320x200
; and 640x200, and a panel laid out in one of them is off the edge of another.
; Clobbers CX, SI, DI, BP (tk_tmlayout's); keeps AX, BX, DX.
; -----------------------------------------------------------------------------
tk_hud_layout:
    push ax
    push bx
    push dx
    mov ax, [tk_vw]                 ; the horizontal and vertical scales the
    mov bx, 8                       ; small fixed tables below are drawn at,
    mul bx                          ; in eighths
    mov bx, 320
    xor dx, dx
    div bx
    mov [tk_hsx], ax
    mov ax, [tk_vh]
    mov bx, 8
    mul bx
    mov bx, 200
    xor dx, dx
    div bx
    mov [tk_hsy], ax

    mov ax, [tk_vw]
    shr ax, 1
    mov [tk_rcx], ax                ; the radar, centred over the gunsight
    mov ax, [tk_vh]
    mov bx, 16
    mul bx
    mov bx, 100
    xor dx, dx
    div bx
    mov [tk_rcy], ax
    mov ax, [tk_vh]
    mov bx, 12
    mul bx
    mov bx, 100
    xor dx, dx
    div bx
    mov [tk_rr], ax

    mov ax, [tk_vw]                 ; the score, right of the radar and
    mov bx, 62                      ; 8-aligned so a glyph is whole bytes
    mul bx
    mov bx, 100
    xor dx, dx
    div bx
    and al, 0xF8
    mov [tk_txx], ax
    mov ax, [tk_vh]
    mov bx, 4
    mul bx
    mov bx, 100
    xor dx, dx
    div bx
    mov [tk_txy], ax
    call tk_rimpoints
    call tk_tmlayout                ; ...and the template's six rectangles
    pop dx
    pop bx
    pop ax
    ret

; tk_hscale - AX = a table coordinate; out AX = it, in this viewport's pixels
; BL = 0 horizontal, 1 vertical
tk_hscalex:
    cmp word [tk_hsx], 8            ; eighths, so 8 is 1:1 - which is what
    je .same                        ; both 320-wide viewports are, and they
    imul word [tk_hsx]              ; are the machines that need the cycles
    sar ax, 1
    sar ax, 1
    sar ax, 1
.same:
    ret
tk_hscaley:
    cmp word [tk_hsy], 8
    je .same
    imul word [tk_hsy]
    sar ax, 1
    sar ax, 1
    sar ax, 1
.same:
    ret

; -----------------------------------------------------------------------------
; tk_drawtab - a table of segments, scaled, about an origin
; in:  SI = the table (dx1,dy1,dx2,dy2 words, 0x7FFF ends it),
;      [tk_tox]/[tk_toy] = the origin
; -----------------------------------------------------------------------------
tk_drawtab:
.s:
    cmp word [si], 0x7FFF
    je .done
    mov ax, [si]
    call tk_hscalex
    add ax, [tk_tox]
    mov [tk_ex1], ax
    mov ax, [si+2]
    call tk_hscaley
    add ax, [tk_toy]
    mov [tk_ey1], ax
    mov ax, [si+4]
    call tk_hscalex
    add ax, [tk_tox]
    mov [tk_ex2], ax
    mov ax, [si+6]
    call tk_hscaley
    add ax, [tk_toy]
    mov [tk_ey2], ax
    push si
    mov ax, [tk_ex1]
    mov bx, [tk_ey1]
    mov cx, [tk_ex2]
    mov dx, [tk_ey2]
    cmp bx, dx
    jne .sl
    call tk_hseg
    jmp short .nx
.sl:
    call tk_seg
.nx:
    pop si
    add si, 8
    jmp short .s
.done:
    ret

; -----------------------------------------------------------------------------
; tk_hud - the panel, the gunsight, and the cracks
;
; TWO SHAPES (SPEC.md 85.3.5). On the shadow backends the STATIC panel lives in
; the TEMPLATE and tk_tmupdate has already kept it current, so a frame draws
; only what moves: the sweep and the blips. Mode X has no template and draws
; the whole panel every frame, which is what every frame did before.
; -----------------------------------------------------------------------------
tk_hud:
    cmp byte [tk_tmpl], 0
    je .full
    mov al, TKI_HUD
    call tk_setink
    jmp tk_radar_dyn
.full:
    call tk_lockon                  ; SPEC.md 85.6.5.7, and it must run before
    call tk_sightdraw               ; the sight is drawn: it picks the table
    call tk_rim
    call tk_radar_dyn
    mov al, TKI_HUD
    call tk_setink
    call tk_scores
    call tk_livesbar
    call tk_rangescan
    call tk_rangedraw
    cmp byte [tk_dead], 0
    je .done
    call tk_crackdraw               ; the glass, on the way out
.done:
    ret

; tk_sightdraw - the open sight, or the SAME EIGHT segments closed on a target
; (85.6.5.7): tk_drawtab draws the identical number of identical runs either
; way, so a lock costs the frame nothing
tk_sightdraw:
    mov al, TKI_MARK
    call tk_setink
    mov ax, [tk_vcx]
    mov [tk_tox], ax
    mov ax, [tk_vcy]
    mov [tk_toy], ax
    mov si, tk_t_sight
    cmp byte [tk_locked], 0
    je .s
    mov si, tk_t_lock
.s:
    jmp tk_drawtab

; tk_crackdraw - the cracked glass, about the point of aim
tk_crackdraw:
    mov al, TKI_MARK
    call tk_setink
    mov ax, [tk_vcx]
    mov [tk_tox], ax
    mov ax, [tk_vcy]
    mov [tk_toy], ax
    mov si, tk_t_crack
    jmp tk_drawtab

; -----------------------------------------------------------------------------
; tk_rim - the dial's rim: sixteen segments that never move, so on a shadow
;          backend they are drawn ONCE, into the template (85.3.5)
; tk_radar_dyn - the sweep and a blip per tank: what moves, every frame
;
; THE BLIP COSTS NOTHING TO PLACE. An enemy's position in CAMERA space is
; already its position on the radar - x across, z up the dial - so the radar
; is the same transform the renderer just did, at a different scale, and
; needs no bearing and no arctangent.
; -----------------------------------------------------------------------------
tk_rim:
    mov al, TKI_RADAR               ; the rim is the dial's furniture and the
    call tk_setink                  ; sweep is what moves on it
    xor bx, bx
    mov cx, 16
.rim:
    push cx
    push bx
    mov ax, [tk_rrx + bx]
    mov [tk_ex1], ax
    mov ax, [tk_rry + bx]
    mov [tk_ey1], ax
    add bx, 2
    and bx, 31
    mov ax, [tk_rrx + bx]
    mov cx, ax
    mov ax, [tk_rry + bx]
    mov dx, ax
    mov ax, [tk_ex1]
    mov bx, [tk_ey1]
    cmp bx, dx
    jne .sl
    call tk_hseg
    jmp short .rn
.sl:
    call tk_seg
.rn:
    pop bx
    add bx, 2
    pop cx
    loop .rim
    ret

tk_radar_dyn:
    mov al, TKI_HUD
    call tk_setink
    mov al, [tk_sweep]              ; the sweep: a narrow wedge, as the
    call tk_wedge                   ; arcade's is
    mov al, [tk_sweep]
    add al, 18
    call tk_wedge
    cmp byte [tk_over], 0           ; a dial that keeps sweeping under GAME
    jne .nosweep                    ; OVER says the game is still going on -
    add byte [tk_sweep], 5          ; and it is also the last moving thing on
.nosweep:                           ; that screen, so stopping it is what lets
                                    ; the frame be drawn once (85.8.1)

    xor bx, bx
    mov cx, TK_NOBJ
.b:
    push cx
    push bx
    mov si, bx
    shr si, 1
    cmp byte [tk_otype + si], OT_TANK
    jne .bn
    call tk_objcam_raw
    mov al, TKI_SHELL               ; a blip is a THREAT, and reads as one
    call tk_setink
    call tk_blip
    mov al, TKI_HUD
    call tk_setink
.bn:
    pop bx
    add bx, 2
    pop cx
    loop .b
    ret

; tk_wedge - one arm of the sweep, AL = its angle
tk_wedge:
    push bx
    push cx
    push dx
    call tk_sincos                  ; AX = sin, BX = cos
    mov cx, bx
    mov bx, [tk_rr]
    MUL14
    add ax, [tk_rcx]
    mov [tk_ex2], ax
    mov ax, cx
    mov bx, [tk_rr]
    MUL14
    neg ax
    add ax, [tk_rcy]
    mov dx, ax
    mov cx, [tk_ex2]
    mov ax, [tk_rcx]
    mov bx, [tk_rcy]
    cmp bx, dx
    jne .sl
    call tk_hseg
    jmp short .o
.sl:
    call tk_seg
.o:
    pop dx
    pop cx
    pop bx
    ret

; tk_blip - the object whose camera-space position is in [tk_ocx]/[tk_ocz]
tk_blip:
    push bx
    push cx
    push dx
    mov ax, [tk_ocx]
    imul word [tk_rr]
    mov bx, TK_RADR
    idiv bx
    mov cx, ax
    or ax, ax
    jns .xa
    neg ax
.xa:
    cmp ax, [tk_rr]
    jge .out
    add cx, [tk_rcx]
    mov [tk_tox], cx
    mov ax, [tk_ocz]
    imul word [tk_rr]
    mov bx, TK_RADR
    idiv bx
    neg ax
    mov cx, ax
    or ax, ax
    jns .ya
    neg ax
.ya:
    cmp ax, [tk_rr]
    jge .out
    add cx, [tk_rcy]
    mov [tk_toy], cx
    push si
    mov si, tk_t_blip
    call tk_drawtab
    pop si
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tk_scores / tk_livesbar / tk_range - the lettered half of the panel
; -----------------------------------------------------------------------------
tk_scores:
    mov cx, [tk_txx]
    mov dx, [tk_txy]
    mov si, tk_s_high
    call tk_text
    mov ax, [tk_hiscore]
    mov dx, [tk_hiscore+2]
    mov di, tk_numbuf
    call tk_utoa
    call tk_numpos
    mov si, tk_numbuf
    call tk_text

    mov cx, [tk_txx]
    mov dx, [tk_txy]
    add dx, 10
    mov si, tk_s_pl1
    call tk_text
    mov ax, [tk_score]
    mov dx, [tk_score+2]
    mov di, tk_numbuf
    call tk_utoa
    call tk_numpos
    add dx, 10
    mov si, tk_numbuf
    call tk_text
    ret

; tk_numpos - CX/DX = where a right-aligned tk_numbuf starts
tk_numpos:
    push ax
    push si
    mov si, tk_numbuf
    call tk_strlen                  ; CX = cells
    mov ax, 12
    sub ax, cx
    mov cl, 3
    shl ax, cl
    add ax, [tk_txx]
    and al, 0xF8
    mov cx, ax
    mov dx, [tk_txy]
    pop si
    pop ax
    ret

tk_livesbar:
    push bx
    mov bl, [tk_lives]
    or bl, bl
    jz .out
    mov ax, [tk_txx]
    add ax, 24
    mov [tk_tox], ax
    mov ax, [tk_txy]
    add ax, 26
    mov [tk_toy], ax
.l:
    push bx
    mov si, tk_t_life
    call tk_drawtab
    pop bx
    mov ax, [tk_tox]
    add ax, 18
    mov [tk_tox], ax
    dec bl
    jnz .l
.out:
    pop bx
    ret

; ENEMY IN RANGE: a tank in front of us and inside a shell's reach. It is the
; arcade's own cue and it is the difference between hunting and being hunted.
; The SCAN and the DRAW are two routines because the template keys on the
; answer (85.3.5): the text is lettered when the answer changes, not per frame.
tk_rangescan:
    mov byte [tk_inrange], 0
    xor bx, bx
    mov cx, TK_NOBJ
.o:
    push cx
    push bx
    mov si, bx
    shr si, 1
    cmp byte [tk_otype + si], OT_TANK
    jne .n
    call tk_objcam_raw
    mov ax, [tk_ocz]
    or ax, ax
    jle .n
    cmp ax, 3200
    jg .n
    mov dx, ax
    shr dx, 1
    mov ax, [tk_ocx]
    or ax, ax
    jns .a
    neg ax
.a:
    cmp ax, dx
    jg .n
    pop bx
    pop cx
    mov byte [tk_inrange], 1
    ret
.n:
    pop bx
    add bx, 2
    pop cx
    loop .o
    ret

tk_rangedraw:
    cmp byte [tk_inrange], 0
    je .o
    mov al, TKI_HUD
    call tk_setink
    mov cx, 8                       ; LEFT, where the 1983 port puts it: this
                                    ; is fourteen cells of text and the dial
                                    ; is 48 wide about the centre, so anything
                                    ; centred collides with it on EVERY
                                    ; viewport - it was drawn straight through
                                    ; the radar on all three
    mov dx, [tk_rcy]
    add dx, 4
    mov si, tk_s_range
    call tk_text
.o:
    ret

; =============================================================================
; THE HUD TEMPLATE (SPEC.md 85.3.5) - the two shadow backends only
;
; The panel used to be drawn every frame, and measured at 61-67 ms of every
; Hercules frame - two fifths of a typical one - for a picture that changes
; on a hit, a kill, a lock and never otherwise. So the static panel lives in a
; second 16,000-byte buffer, the TEMPLATE, right after the shadow in the same
; claim, and the per-row clear that used to store zeros over last frame's runs
; copies the template's bytes instead. The shadow is therefore always the
; template plus this frame's dynamic drawing, by induction, and the six
; static items cost the frame nothing until one of them CHANGES.
;
; An item is a KEY, a RECTANGLE and a DRAW routine. Once a frame every key is
; compared with its cache; on a change the rectangle is zeroed in the
; template, the item is drawn into the template through the ordinary walks
; (with the span marks sent to a set nobody reads), the rectangle is copied
; template -> shadow, and the rectangle is marked into this frame's spans so
; the blit carries it. Rectangles rather than the drawing's own spans because
; a life LOST leaves nothing to mark: the pixels that must reach the glass are
; the ones no longer drawn.
; =============================================================================

; tk_tmupdate - keep the six items current. Runs right after tk_r_begin, so
;               the clear has already copied the OLD template over last
;               frame's runs and the shadow holds nothing dynamic yet.
tk_tmupdate:
    call tk_lockon                  ; -> [tk_locked], the sight's key
    call tk_rangescan               ; -> [tk_inrange], the range line's key
    mov ax, 1                       ; the rim: keyed on a constant, so it is
    xor dx, dx                      ; drawn once a bracket
    xor bp, bp
    call tk_tmitem
    mov al, [tk_locked]
    xor ah, ah
    xor dx, dx
    mov bp, 1
    call tk_tmitem
    mov al, [tk_lives]
    xor ah, ah
    xor dx, dx
    mov bp, 2
    call tk_tmitem
    mov ax, [tk_score]              ; the low words: a kill is +1000, which
    mov dx, [tk_hiscore]            ; cannot leave a low word where it was
    mov bp, 3
    call tk_tmitem
    mov al, [tk_inrange]
    xor ah, ah
    xor dx, dx
    mov bp, 4
    call tk_tmitem
    mov al, [tk_dead]               ; cracked or not: the count is not the key
    or al, al
    jz .cr
    mov al, 1
.cr:
    xor ah, ah
    xor dx, dx
    mov bp, 5
    call tk_tmitem
    ret

; The items, in key order: what each draws into the template when asked. The
; seventh is the ridge (85.3.8), which keeps its own state and is here only so
; that an item whose rectangle it crosses can have it drawn again.
TK_NTM equ 7
tk_tmproc:
    dw tk_rim, tk_sightdraw, tk_tm_lives, tk_tm_scores, tk_rangedraw, tk_tm_crack
    dw tk_tm_ridge

tk_tm_lives:
    mov al, TKI_HUD
    call tk_setink
    jmp tk_livesbar
tk_tm_scores:
    mov al, TKI_HUD
    call tk_setink
    jmp tk_scores
tk_tm_crack:
    cmp byte [tk_dead], 0
    je .o
    jmp tk_crackdraw
.o:
    ret

; tk_tmitem - one item: redraw it if its key moved
; in:  DX:AX = the key, BP = the item (0..TK_NTM-1).  clobbers everything
tk_tmitem:
    mov di, bp
    shl di, 1
    shl di, 1
    add di, tk_tmc
    cmp ax, [di]
    jne .redo
    cmp dx, [di+2]
    je .same
.redo:
    mov [di], ax
    mov [di+2], dx
    call tk_tmrect
    push bp                         ; the movers count words in BP
    call tk_tmzero
    pop bp
    mov ax, [tk_tmseg]              ; the walks draw into the TEMPLATE...
    mov [tk_tseg], ax
    mov ax, [tk_spcur]              ; ...and mark a set nobody reads
    mov [tk_tmsave], ax
    mov word [tk_spcur], tk_spjunk
    ; EVERY ITEM WHOSE RECTANGLE MEETS THIS ONE IS DRAWN AGAIN, this one
    ; included: the zero above took their pixels inside it with it. The
    ; crack's rectangle holds the whole sight, which is how the sight was
    ; first seen to vanish at the start of every game - the crack item,
    ; drawing nothing, had just zeroed it.
    mov al, [ds:tk_tmovl + bp]      ; DS, SPELLED: a BP operand addresses SS
    mov [tk_tmbits], al             ; by default, and SS is the kernel's
    mov [tk_tmcur], si              ; the rectangle, for a redraw that can
    push bp                         ; limit itself to it (the ridge's). The
    call tk_tmdrawset               ; walks keep nothing, BP included, and
    pop bp                          ; BP is the item: without this the copy
    mov word [tk_tmcur], 0          ; below ran a garbage rectangle for ever
    mov ax, [tk_tmsave]
    mov [tk_spcur], ax
    mov ax, [tk_shseg]
    mov [tk_tseg], ax
    call tk_tmrect
    push bp
    call tk_tmcopy                  ; the rectangle, template -> shadow
    pop bp
    call tk_tmrect
    call tk_tmmark                  ; ...and into this frame's spans
.same:
    ret

; tk_tmrect - SI -> item BP's rectangle
tk_tmrect:
    mov si, bp
    shl si, 1
    shl si, 1
    shl si, 1
    add si, tk_tmr
    ret

; tk_tmoverlap - the intersection matrix, once a bracket: bit j of byte i is
;                set when rectangles i and j share a row AND a byte
tk_tmoverlap:
    xor bp, bp
.i:
    mov byte [ds:tk_tmovl + bp], 0  ; DS, SPELLED - see tk_tmitem
    xor bx, bx                      ; BX = j
    mov dl, 1                       ; DL = bit j
.j:
    mov si, bp
    shl si, 1
    shl si, 1
    shl si, 1
    add si, tk_tmr                  ; SI = rect i
    mov di, bx
    shl di, 1
    shl di, 1
    shl di, 1
    add di, tk_tmr                  ; DI = rect j
    mov ax, [si]                    ; rows: r0_i <= r1_j and r0_j <= r1_i
    cmp ax, [di+2]
    jg .no
    mov ax, [di]
    cmp ax, [si+2]
    jg .no
    mov ax, [si+4]                  ; bytes, the same way
    cmp ax, [di+6]
    jg .no
    mov ax, [di+4]
    cmp ax, [si+6]
    jg .no
    or [ds:tk_tmovl + bp], dl
.no:
    shl dl, 1
    inc bx
    cmp bx, TK_NTM
    jb .j
    inc bp
    cmp bp, TK_NTM
    jb .i
    ret

; tk_tmdrawset - draw every item whose bit is set in [tk_tmbits], into
;                whatever tk_tseg/tk_spcur point at. Bit j is tk_tmproc[j].
tk_tmdrawset:
    xor bx, bx
.j:
    shr byte [tk_tmbits], 1
    jnc .nj
    push bx
    call [tk_tmproc + bx]
    pop bx
.nj:
    add bx, 2
    cmp bx, TK_NTM * 2
    jb .j
    ret

; =============================================================================
; THE RIDGE IN THE TEMPLATE, WHILE THE HEADING HOLDS (SPEC.md 85.3.8)
;
; The ridge is at infinity and moves only when the camera turns, and it cost
; 26 ms of a 97 ms Hercules frame to draw every frame. It cannot be a plain
; item: its rectangle is the whole width by seventy rows, and zeroing, copying
; and blitting that on every turn would cost more than drawing it. So it keeps
; its OWN span set - what it actually lit in the template - and the rule is
; the heading: the frame after a turn stops, the ridge is drawn into the
; template over those runs; the frame a turn starts, it is taken out again
; and drawn into the shadow as every frame once did. A frame in the middle of
; a turn costs exactly what it did; a still one costs no ridge at all.
; =============================================================================
tk_ridge_full:                      ; the whole width, then tk_ridge
    xor ax, ax
    mov [tk_rx0], ax
    mov ax, [tk_vw]
    dec ax
    mov [tk_rx1], ax
    jmp tk_ridge

tk_ridge_tm:
    cmp byte [tk_tmpl], 0
    je tk_ridge_full                ; Mode X: every frame, as before
    mov al, [tk_pa]
    mov ah, [tk_rpa_last]
    mov [tk_rpa_last], al
    cmp al, ah
    jne .moving
    cmp byte [tk_rtm_on], 0         ; SETTLED: the same heading as last frame
    je .settle
    cmp al, [tk_rpa_tm]
    je .done                        ; ...and the template already has it
.settle:
    mov [tk_rpa_tm], al
    call tk_rdg_out                 ; the old ridge out; its runs in tk_spans2
    call tk_rdg_touch               ; -> [tk_tmbits]: the items on those runs
    push ds
    pop es
    mov di, tk_sprdg                ; the new ridge records its own runs
    mov cx, TK_MAXROW
    mov ax, 0x00FF
    cld
    rep stosw
    mov ax, [tk_tmseg]
    mov [tk_tseg], ax
    mov ax, [tk_spcur]
    mov [tk_tmsave], ax
    mov word [tk_spcur], tk_sprdg
    call tk_ridge_full
    mov word [tk_spcur], tk_spjunk
    call tk_tmdrawset               ; the items the zero took, drawn again
    mov ax, [tk_tmsave]
    mov [tk_spcur], ax
    mov ax, [tk_shseg]
    mov [tk_tseg], ax
    mov byte [tk_rtm_on], 1
    call tk_rdg_union               ; tk_spans2 = the old runs and the new
    jmp tk_rdg_apply                ; template -> shadow over them, marked
.moving:
    cmp byte [tk_rtm_on], 0
    je .dyn
    call tk_rdg_out                 ; MOVING: the template must hold no ridge
    call tk_rdg_touch
    mov ax, [tk_tmseg]
    mov [tk_tseg], ax
    mov ax, [tk_spcur]
    mov [tk_tmsave], ax
    mov word [tk_spcur], tk_spjunk
    call tk_tmdrawset
    mov ax, [tk_tmsave]
    mov [tk_spcur], ax
    mov ax, [tk_shseg]
    mov [tk_tseg], ax
    mov byte [tk_rtm_on], 0
    call tk_rdg_apply
.dyn:
    jmp tk_ridge_full               ; into the shadow, as every frame once did
.done:
    ret

; tk_tm_ridge - the ridge as an ITEM: drawn again when another item's zero
;               took some of it, and only across that item's rectangle
tk_tm_ridge:
    cmp byte [tk_rtm_on], 0
    je .o
    mov si, [tk_tmcur]
    or si, si
    jnz .part
    jmp tk_ridge_full
.part:
    mov cl, [tk_kshift]
    mov ax, [si+4]
    shl ax, cl
    mov [tk_rx0], ax
    mov ax, [si+6]
    inc ax
    shl ax, cl
    dec ax
    mov [tk_rx1], ax
    jmp tk_ridge
.o:
    ret

; EVERY PASS BELOW RUNS OVER THE RIDGE'S BAND ALONE - rectangle 6's rows, the
; tallest peak the table holds above the horizon - and not the viewport's 200.
; The first build ran them full height and a transition frame spent 30 ms in
; span-array loops before it drew a pixel.
;
; tk_rdg_band - SI -> tk_spans2 at the band's first row, DI -> tk_sprdg there,
;               BX -> tk_rowoff there, CX = the band's rows, DX = its first row
tk_rdg_band:
    mov dx, [tk_tmr + 48]
    mov cx, [tk_tmr + 50]
    sub cx, dx
    inc cx
    mov bx, dx
    shl bx, 1
    mov si, bx
    add si, tk_spans2
    mov di, bx
    add di, tk_sprdg
    add bx, tk_rowoff
    ret

; tk_rdg_out - the template's ridge, if it has one, taken out: its runs are
;              copied to tk_spans2 and zeroed in the template; else tk_spans2's
;              band is armed empty
tk_rdg_out:
    call tk_rdg_band
    push ds
    pop es
    cld
    cmp byte [tk_rtm_on], 0
    je .empty
    push cx
    push bx
    push di
    xchg si, di                     ; tk_sprdg -> tk_spans2, the band
    rep movsw
    pop si                          ; SI = tk_sprdg at the band
    pop bx
    pop bp                          ; BP = its rows
    jmp tk_tmzruns
.empty:
    mov di, si
    mov ax, 0x00FF
    rep stosw
    ret

; tk_tmzruns - zero the template over the runs of SI, BP rows, BX -> tk_rowoff
tk_tmzruns:
    mov es, [tk_tmseg]
    cld
.r:
    lodsw
    cmp al, ah
    ja .n
    and al, 0xFE
    or ah, 1
    sub ah, al
    shr ah, 1
    mov cl, ah
    xor ch, ch
    inc cx
    xor ah, ah
    mov di, [bx]
    add di, ax
    xor ax, ax
    rep stosw
.n:
    inc bx
    inc bx
    dec bp
    jnz .r
    ret

; tk_tmcpruns - template -> shadow over the runs of SI, BP rows, BX -> tk_rowoff
tk_tmcpruns:
    mov es, [tk_shseg]
    cld
.r:
    lodsw
    cmp al, ah
    ja .n
    and al, 0xFE
    or ah, 1
    sub ah, al
    shr ah, 1
    mov cl, ah
    xor ch, ch
    inc cx
    xor ah, ah
    mov di, [bx]
    add di, ax
    push si
    mov si, di
    push ds
    mov ds, [tk_tmseg]
    rep movsw
    pop ds
    pop si
.n:
    inc bx
    inc bx
    dec bp
    jnz .r
    ret

; tk_rdg_touch - [tk_tmbits] = the six panel items whose rectangle meets a
;                run of tk_spans2 inside the band: the ones a zero over it took
tk_rdg_touch:
    mov byte [tk_tmbits], 0
    xor bp, bp
    mov dl, 1
.i:
    mov si, bp
    shl si, 1
    shl si, 1
    shl si, 1
    add si, tk_tmr                  ; SI = the rectangle
    mov ax, [si]                    ; its rows, clipped to the band
    cmp ax, [tk_tmr + 48]
    jge .r0
    mov ax, [tk_tmr + 48]
.r0:
    mov cx, [si+2]
    cmp cx, [tk_tmr + 50]
    jle .r1
    mov cx, [tk_tmr + 50]
.r1:
    sub cx, ax
    jl .ni                          ; no row in common
    inc cx                          ; CX = rows
    mov bx, ax
    shl bx, 1
    add bx, tk_spans2               ; BX = the run of the first such row
    mov al, [si+4]                  ; AL = byte0, AH = byte1
    mov ah, [si+6]
.r:
    cmp [bx], ah                    ; lo > byte1 (or empty): no
    ja .nr
    push ax
    mov al, [bx+1]
    cmp al, [bx]
    jb .nx                          ; lo > hi: empty
    cmp al, [si+4]                  ; hi < byte0: no
    jb .nx
    pop ax
    or [tk_tmbits], dl
    jmp short .ni
.nx:
    pop ax
.nr:
    inc bx
    inc bx
    loop .r
.ni:
    shl dl, 1
    inc bp
    cmp bp, TK_NTM - 1              ; the six panel items, not the ridge
    jb .i
    ret

; tk_rdg_union - tk_spans2 = its runs widened by tk_sprdg's, over the band
tk_rdg_union:
    call tk_rdg_band
.r:
    mov ax, [di]
    cmp al, [si]
    jae .l
    mov [si], al
.l:
    cmp ah, [si+1]
    jbe .h
    mov [si+1], ah
.h:
    inc si
    inc si
    inc di
    inc di
    loop .r
    ret

; tk_rdg_apply - template -> shadow over tk_spans2's band runs, and those runs
;                into this frame's span set so the blit carries them
tk_rdg_apply:
    call tk_rdg_band
    push cx
    push dx
    push si
    mov bp, cx
    call tk_tmcpruns
    pop si                          ; SI = tk_spans2 at the band
    pop di                          ; DI = the band's first row...
    shl di, 1
    add di, [tk_spcur]              ; ...in this frame's set
    pop cx
.r:
    mov ax, [si]
    cmp al, [di]
    jae .l
    mov [di], al
.l:
    cmp ah, [di+1]
    jbe .h
    mov [di+1], ah
.h:
    inc si
    inc si
    inc di
    inc di
    loop .r
    ret

; A RECTANGLE is four words: row0, row1, byte0, byte1, all inclusive and all
; clamped to the viewport by tk_tmreg. The byte pair is rounded out to whole
; WORDS by the two movers, as the clear and the blit round theirs.
tk_tmzero:                          ; zero it in the template
    mov es, [tk_tmseg]
    mov dx, [si+4]
    and dl, 0xFE
    mov bp, [si+6]
    or bp, 1
    sub bp, dx
    shr bp, 1
    inc bp                          ; BP = words a row
    mov di, [si]
    shl di, 1                       ; DI = the row-offset index
    mov cx, [si+2]
    sub cx, [si]
    inc cx                          ; CX = rows
    jle .done                       ; ...and none is none, not 65,536
    cld
.r:
    push cx
    push di
    mov di, [tk_rowoff + di]
    add di, dx
    mov cx, bp
    xor ax, ax
    rep stosw
    pop di
    add di, 2
    pop cx
    loop .r
.done:
    ret

tk_tmcopy:                          ; template -> shadow, the same rectangle
    push ds
    mov es, [tk_shseg]
    mov dx, [si+4]
    and dl, 0xFE
    mov bp, [si+6]
    or bp, 1
    sub bp, dx
    shr bp, 1
    inc bp
    mov di, [si]
    shl di, 1
    mov cx, [si+2]
    sub cx, [si]
    inc cx
    jle .done
    mov ds, [tk_tmseg]
    cld
.r:
    push cx
    push di
    mov di, [cs:tk_rowoff + di]
    add di, dx
    mov si, di                      ; the same offset in both halves
    mov cx, bp
    rep movsw
    pop di
    add di, 2
    pop cx
    loop .r
.done:
    pop ds
    ret

tk_tmmark:                          ; the rectangle into [tk_spcur]
    mov bx, [si]
    shl bx, 1
    add bx, [tk_spcur]
    mov cx, [si+2]
    sub cx, [si]
    inc cx
    jle .done
    mov dx, [si+4]                  ; DL = byte0
    mov dh, [si+6]                  ; DH = byte1
.r:
    cmp [bx], dl
    jbe .l
    mov [bx], dl
.l:
    cmp [bx+1], dh
    jae .h
    mov [bx+1], dh
.h:
    add bx, 2
    loop .r
.done:
    ret

; tk_tmreg - store one rectangle, clamped to the viewport
; in: SI -> the rectangle, AX = row0, BX = row1, CX = x0, DX = x1 (pixels)
tk_tmreg:
    or ax, ax
    jns .a
    xor ax, ax
.a:
    cmp ax, [tk_vh]
    jl .b
    mov ax, [tk_vh]
    dec ax
.b:
    mov [si], ax
    or bx, bx
    jns .c
    xor bx, bx
.c:
    cmp bx, [tk_vh]
    jl .d
    mov bx, [tk_vh]
    dec bx
.d:
    mov [si+2], bx
    mov bx, dx                      ; BX = x1, AX = x0
    mov ax, cx
    mov cl, [tk_kshift]
    or ax, ax
    jns .e
    xor ax, ax
.e:
    cmp ax, [tk_vw]
    jl .f
    mov ax, [tk_vw]
    dec ax
.f:
    shr ax, cl
    mov [si+4], ax
    or bx, bx
    jns .g
    xor bx, bx
.g:
    cmp bx, [tk_vw]
    jl .h
    mov bx, [tk_vw]
    dec bx
.h:
    shr bx, cl
    mov [si+6], bx
    ret

; tk_tmlayout - the six rectangles, from the panel's own layout
tk_tmlayout:
    ; the rim: rcy +- rr, rcx +- rr, a pixel of slack
    mov si, tk_tmr
    mov ax, [tk_rcy]
    sub ax, [tk_rr]
    dec ax
    mov bx, [tk_rcy]
    add bx, [tk_rr]
    inc bx
    mov cx, [tk_rcx]
    sub cx, [tk_rr]
    dec cx
    mov dx, [tk_rcx]
    add dx, [tk_rr]
    inc dx
    call tk_tmreg
    ; the sight: tk_t_sight reaches +-34 vertically and +-46 across
    mov ax, 34
    call tk_hscaley
    mov bp, ax
    mov ax, 46
    call tk_hscalex
    mov di, ax
    mov ax, [tk_vcy]
    sub ax, bp
    dec ax
    mov bx, [tk_vcy]
    add bx, bp
    inc bx
    mov cx, [tk_vcx]
    sub cx, di
    dec cx
    mov dx, [tk_vcx]
    add dx, di
    inc dx
    mov si, tk_tmr + 8
    call tk_tmreg
    ; the lives: tk_livesbar's origins, and tk_t_life's +-6 by -4
    mov ax, 4
    call tk_hscaley
    mov bp, ax
    mov ax, 6
    call tk_hscalex
    mov di, ax
    mov ax, [tk_txy]
    add ax, 26
    sub ax, bp
    dec ax
    mov bx, [tk_txy]
    add bx, 27
    mov cx, [tk_txx]
    add cx, 24
    sub cx, di
    dec cx
    mov dx, [tk_txx]
    add dx, 24 + 18 * (TK_LIVES - 1)
    add dx, di
    inc dx
    mov si, tk_tmr + 16
    call tk_tmreg
    ; the scores: two lines of twelve cells at txx/txy
    mov ax, [tk_txy]
    mov bx, ax
    add bx, 18
    mov cx, [tk_txx]
    mov dx, cx
    add dx, 12 * 8 - 1
    mov si, tk_tmr + 24
    call tk_tmreg
    ; ENEMY IN RANGE: fourteen cells at x = 8, four rows under the dial's centre
    mov ax, [tk_rcy]
    add ax, 4
    mov bx, ax
    add bx, 8
    mov cx, 8
    mov dx, 8 + 14 * 8 - 1
    mov si, tk_tmr + 32
    call tk_tmreg
    ; the crack: tk_t_crack reaches -58..72 down and -150..132 across
    mov ax, 58
    call tk_hscaley
    mov bp, ax
    mov ax, 72
    call tk_hscaley
    mov bx, [tk_vcy]
    add bx, ax
    inc bx
    mov ax, [tk_vcy]
    sub ax, bp
    dec ax
    push ax
    push bx
    mov ax, 150
    call tk_hscalex
    mov bp, ax
    mov ax, 132
    call tk_hscalex
    mov dx, [tk_vcx]
    add dx, ax
    inc dx
    mov cx, [tk_vcx]
    sub cx, bp
    dec cx
    pop bx
    pop ax
    mov si, tk_tmr + 40
    call tk_tmreg
    ; the ridge's band (85.3.8): the tallest peak the table holds, scaled,
    ; above the horizon, the whole width - for the matrix only
    mov si, tk_ridgetab
    mov cx, 256
    xor al, al
.mx:
    cmp al, [si]
    jae .m1
    mov al, [si]
.m1:
    inc si
    loop .mx
    xor ah, ah
    mul word [tk_ridgek]
    mov al, ah                      ; AX = the tallest peak in rows
    mov ah, dl
    mov bx, [tk_vcy]
    inc bx
    mov dx, bx
    sub dx, ax
    sub dx, 2
    mov ax, dx                      ; AX = row0, BX = row1
    xor cx, cx
    mov dx, [tk_vw]
    dec dx
    mov si, tk_tmr + 48
    call tk_tmreg
    jmp tk_tmoverlap

; =============================================================================
; Text in a foreign mode (SPEC.md 85.7)
;
; No font_* slot is legal in here (SPEC.md 53.7), but OSAPI_FONT_GLYPHS is:
; it hands over the BITMAP rather than drawing it, which is exactly what an
; app that owns the raster needs. So the panel is lettered in the kernel's own
; 8x8 face on all three adapters, with no second typeface anywhere.
;
; Every run is forced to an 8-PIXEL BOUNDARY, which is what makes a glyph row
; a whole byte on Hercules, two whole bytes on CGA, and two byte-columns on
; Mode X - one store per row instead of eight tests.
; =============================================================================
tk_glyphs:
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL/AH = its range
    mov [tk_gtab], si
    mov [tk_gseg], dx
    mov [tk_gfirst], al
    mov [tk_glast], ah
    ret

; tk_text - SI = a NUL string, CX = x, DX = y (both viewport-relative)
tk_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    and cl, 0xF8
.ch:
    mov al, [si]
    or al, al
    jz .done
    inc si
    push si
    push cx                         ; every glyph backend wants CX for a
    push dx                         ; count or a shift count
    call tk_glyph
    pop dx
    pop cx
    pop si
    add cx, 8
    cmp cx, [tk_vw]
    jl .ch
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_glyph - AL = the character, CX = x, DX = y.  clobbers AX, BX, SI, DI, BP
tk_glyph:
    cmp al, [tk_gfirst]
    jb .out
    cmp al, [tk_glast]
    ja .out
    or dx, dx
    js .out
    mov bx, dx
    add bx, 8
    cmp bx, [tk_vh]
    jg .out
    or cx, cx
    js .out

    push cx                         ; --- stage the glyph into our segment ---
    push dx
    sub al, [tk_gfirst]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [tk_gtab]
    mov ax, [tk_gseg]
    push ds
    push ds
    pop es
    mov si, bx
    mov ds, ax
    mov di, tk_gbuf
    mov cx, 8
    cld
    rep movsb
    pop ds
    pop dx
    pop cx

    push cx                         ; --- the span rows and the address -----
    push dx
    mov bx, cx
    mov cl, 3
    cmp byte [tk_back], TKB_HERC
    je .k
    mov cl, 2
.k:
    shr bx, cl
    mov [tk_gcol], bx               ; the first byte of every row
    mov al, 1
    cmp byte [tk_back], TKB_HERC
    je .w
    mov al, 2
.w:
    add bl, al
    dec bl
    mov [tk_gcol2], bx              ; ...and the last
    pop dx
    pop cx

    push cx
    mov bx, dx                      ; mark eight rows of span
    shl bx, 1
    add bx, [tk_spcur]
    mov cx, 8
    mov ax, [tk_gcol]
.sp:
    cmp [bx], al
    jbe .s1
    mov [bx], al
.s1:
    push ax
    mov ax, [tk_gcol2]
    cmp [bx+1], al
    jae .s2
    mov [bx+1], al
.s2:
    pop ax
    add bx, 2
    loop .sp
    pop cx

    mov ax, dx                      ; DI = tbase + y*80 + the first byte
    push cx
    mov cl, 4
    shl ax, cl
    mov di, ax
    shl ax, 1
    shl ax, 1
    add di, ax
    pop cx
    add di, [tk_tbase]
    add di, [tk_gcol]
    mov es, [tk_tseg]
    call [tk_glyphproc]
.out:
    ret

; EVERY CELL IS OPAQUE, ground and glyph in one pass - which is SPEC.md 6.1's
; rule, met here for the same reason and with the same fix. These three used to
; OR, and a caption over the wireframe kept the wireframe: the score, ENEMY IN
; RANGE, the game-over prompts and the initials being typed all had the scene
; running through them and half vanished into it. A store costs no more than
; an OR - on Hercules it costs a read less - and tk_glyph has always marked the
; whole 8x8 cell dirty, so the span is already right for it.
tk_glyph_herc:
    mov si, tk_gbuf
    mov cx, 8
    cld
.r:
    lodsb
    mov [es:di], al
    add di, 80
    loop .r
    ret

tk_glyph_cga:
    mov si, tk_gbuf
    mov bp, 8
    cld
.r:
    lodsb
    mov dl, al
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov bx, ax
    add bx, [tk_nibp]
    mov al, [bx]                    ; the nibble tables put colour 0 under
    mov [es:di], al                 ; every clear bit, so a plain store IS
    mov al, dl                      ; ink on black
    and al, 0x0F
    xor ah, ah
    mov bx, ax
    add bx, [tk_nibp]
    mov al, [bx]
    mov [es:di+1], al
    add di, 80
    dec bp
    jnz .r
    ret

; Mode X letters PLANE BY PLANE, not row by row: a glyph's eight rows touch
; every plane, so doing it the other way is 32 map-mask writes instead of 4.
tk_glyph_modex:
    xor bl, bl
.pl:
    mov al, 1
    mov cl, bl
    shl al, cl
    call tk_mxmask
    push di
    mov si, tk_gbuf
    mov bp, 8
.r:
    mov al, [si]
    mov cl, 7                       ; a set bit becomes the ink and a clear one
    sub cl, bl                      ; becomes 0, both STORED: the cell is
    mov ah, al                      ; opaque, and branch-free with it
    shr ah, cl
    and ah, 1
    neg ah                          ; 1 -> 0FFh, 0 -> 0
    and ah, [tk_ink8]
    mov [es:di], ah
    mov cl, 3
    sub cl, bl
    mov ah, al
    shr ah, cl
    and ah, 1
    neg ah
    and ah, [tk_ink8]
    mov [es:di+1], ah
    inc si
    add di, 80
    dec bp
    jnz .r
    pop di
    inc bl
    cmp bl, 4
    jb .pl
    ret

; -----------------------------------------------------------------------------
; tk_utoa - DX:AX in decimal at ES... no: at DS:DI, NUL-terminated
; tk_strlen - SI = string; out CX = its length
; -----------------------------------------------------------------------------
tk_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in BP
    push ax
    push bx
    push dx
    push si
    push di
    push bp
    xor bp, bp
    mov bx, 10
.d:
    push ax
    mov ax, dx
    xor dx, dx
    div bx                          ; AX = the high quotient
    mov si, ax
    pop ax
    div bx                          ; AX = the low quotient, DX = the digit
    push dx
    inc bp
    mov dx, si
    mov si, ax
    or si, dx
    jnz .d
.p:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    dec bp
    jnz .p
    mov byte [di], 0
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

tk_strlen:
    push si
    xor cx, cx
.s:
    cmp byte [si], 0
    je .d
    inc cx
    inc si
    jmp short .s
.d:
    pop si
    ret

; -----------------------------------------------------------------------------
; tk_rimpoints - the radar's sixteen rim points, once a bracket
; -----------------------------------------------------------------------------
tk_rimpoints:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si
    mov byte [tk_ra], 0
.p:
    mov al, [tk_ra]                 ; NOT in DL: MUL14 clobbers DX
    call tk_sincos                  ; AX = sin, BX = cos
    mov cx, bx
    mov bx, [tk_rr]
    push cx
    MUL14
    add ax, [tk_rcx]
    mov [tk_rrx + si], ax
    pop ax
    mov bx, [tk_rr]
    MUL14
    neg ax
    add ax, [tk_rcy]
    mov [tk_rry + si], ax
    add si, 2
    add byte [tk_ra], 16
    cmp si, 32
    jb .p
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The fixed shapes the panel is drawn from: (dx1,dy1,dx2,dy2) about an origin,
; in 320x200 units, scaled to the live viewport by tk_drawtab. 0x7FFF ends one.
; =============================================================================
tk_t_sight:                         ; the arcade's gunsight: two stalks and a
    dw    0, -34,    0, -12         ; bracket either side of the point of aim
    dw    0,  12,    0,  34
    dw  -46, -10,  -34, -10
    dw  -34, -10,  -34,  10
    dw  -46,  10,  -34,  10
    dw   46, -10,   34, -10
    dw   34, -10,   34,  10
    dw   46,  10,   34,  10
    dw 0x7FFF

tk_t_lock:                          ; ...and CLOSED on a target (85.6.5.7).
    dw    0, -34,    0, -14         ; EIGHT SEGMENTS, the same eight, of the
    dw    0,  14,    0,  34         ; same lengths - so the sight snapping shut
    dw  -(TK_BOXPX+12), -10,  -TK_BOXPX, -10        ; is free: tk_drawtab draws
    dw  -TK_BOXPX, -10,  -TK_BOXPX,  10             ; the identical number of
    dw  -(TK_BOXPX+12),  10,  -TK_BOXPX,  10        ; identical-length runs and
    dw   TK_BOXPX+12, -10,   TK_BOXPX, -10          ; the frame a lock lands in
    dw   TK_BOXPX, -10,   TK_BOXPX,  10             ; costs what every other
    dw   TK_BOXPX+12,  10,   TK_BOXPX,  10          ; frame costs
    dw 0x7FFF                       ; THE INNER EDGE IS TK_BOXPX AND NOT A
                                    ; DRAWING CHOICE: it is the box the gun
                                    ; corrects inside, derived in tank.asm and
                                    ; asserted against TK_BOXQ there, so what
                                    ; the player is told to aim inside is the
                                    ; same number the gun tests

tk_t_crack:                         ; the glass, when they get you first
    dw    0,   0,  -40, -30
    dw  -40, -30,  -92, -18
    dw  -92, -18, -150, -58
    dw    0,   0,   36, -46
    dw   36, -46,   62, -12
    dw   62, -12,  132, -42
    dw    0,   0,  -26,  42
    dw  -26,  42,  -72,  72
    dw    0,   0,   30,  52
    dw   30,  52,   92,  44
    dw 0x7FFF

tk_t_blip:
    dw   -3,   0,    3,   0
    dw    0,  -3,    0,   3
    dw 0x7FFF

tk_t_life:                          ; a tank, four pixels tall
    dw   -6,   0,    6,   0
    dw   -6,   0,   -6,  -4
    dw    6,   0,    6,  -4
    dw   -3,  -4,    3,  -4
    dw 0x7FFF

tk_nibtabs:
%include "tknib.inc"

; =============================================================================
; GAME OVER (SPEC.md 85.8): the lines fly in and become the letters
;
; The whole sequence carries NO per-segment state. Forty segments, each with
; a direction and a distance derived from its own INDEX, all sharing one
; remaining-distance figure that falls with the clock: the frame is a table
; walk and three multiplies a segment. Storing forty flown positions would be
; 480 bytes of bss to save about three milliseconds a frame, and this game
; has the milliseconds and not the bytes.
;
; THE EASE IS THE POINT. A linear fly-in reads as a slide; squaring the
; remaining distance makes the pieces hurtle in and then settle onto the
; letters, which is the movement the arcade cabinets used and the reason ONE
; second is long enough to be worth watching. It was two, and the ease is why
; halving it costs nothing: the frames are the same frames, taken from the same
; curve, at whatever rate the machine can draw them.
; =============================================================================

; -----------------------------------------------------------------------------
; tk_gostart - the player is out of tanks
; -----------------------------------------------------------------------------
tk_gostart:
    push ax
    call tk_hs_submit               ; a row, or TK_NHS*4 for none
    call tk_hs_top
    cmp word [tk_hsidx], TK_NHS * 4
    jae .noname
    call tk_ient_begin
.noname:
    mov byte [tk_over], 1
    call OSAPI_GET_TICKS
    mov [tk_ovt], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_gover - one frame of the overlay
; clobbers everything
; -----------------------------------------------------------------------------
tk_gover:
    call OSAPI_GET_TICKS            ; how far in, as 0..256 of the way
    sub ax, [tk_ovt]
    cmp ax, TK_GOANIM
    jb .run
    mov byte [tk_over], 2           ; landed: from here it is a menu, and the
    mov ax, 256                     ; journey is FULLY elapsed - zero is the
    jmp short .q                    ; other end of this scale and parks every
                                    ; letter at its launch point instead
.run:
    mov bx, 15                      ; 256/18, near enough - the clamp below is
    mul bx                          ; what makes the last frame exact
    cmp ax, 256
    jbe .q
    mov ax, 256
.q:
    neg ax                          ; the distance still to travel, EASED: the
    add ax, 256                     ; square makes them hurtle in and settle
    mov bx, ax
    mul bx
    mov al, ah                      ; >> 8
    mov ah, dl
    mov [tk_goq], ax

    call tk_gohalo                  ; the margin, cleared, before any letter
    xor ax, ax                      ; is laid - see SPEC.md 85.8.1
    mov [tk_hox], ax
    mov [tk_hoy], ax
    mov al, TKI_MARK
    call tk_setink
    call tk_golets
    cmp byte [tk_over], 2           ; the prompts belong to the settled state,
    jne .out                        ; not to the flight
    call tk_goprompt
.out:
    ret

; -----------------------------------------------------------------------------
; tk_golets - the forty segments once, wherever [tk_hox]/[tk_hoy] put them
;
; Always through tk_seg and never tk_hseg, because the halo pass runs this same
; loop through the ERASE walk and there is no erasing horizontal run. A letter
; stroke is thirty pixels; the fast run was buying almost nothing here.
; -----------------------------------------------------------------------------
tk_golets:
    mov si, tk_goseg
    xor bp, bp
.seg:
    cmp byte [si], 0FFh
    je .out
    call tk_gooff                   ; BP's own direction and distance, scaled
    mov ax, [si]                    ; by what is left of the journey
    call tk_gopt
    mov [tk_ex1], ax
    mov [tk_ey1], bx
    mov ax, [si+2]
    call tk_gopt
    mov [tk_ex2], ax
    mov [tk_ey2], bx
    mov ax, [tk_ex1]
    mov bx, [tk_ey1]
    mov cx, [tk_ex2]
    mov dx, [tk_ey2]
    push si                         ; tk_seg keeps nothing (SPEC.md 85.3.4)
    push bp
    call tk_seg
    pop bp
    pop si
    add si, 4
    inc bp
    jmp short .seg
.out:
    ret

; -----------------------------------------------------------------------------
; tk_gohalo - clear a margin around every letter stroke (SPEC.md 85.8.1)
;
; The lettering used to be laid straight over whatever the scene had left
; there, and on a busy screen the horizon, a slab edge or the wreck ran
; through the letters. A halo is the fix that keeps the scene: the same forty
; segments are walked first with the ERASE plot at each of a small set of
; displacements, so a one-pixel margin either side of every stroke is taken
; back to black and the letters then sit in their own clearing.
;
; It is a separate PASS over all forty and not a per-segment step, because a
; halo drawn segment by segment would rub out the letters drawn before it.
; -----------------------------------------------------------------------------
tk_gohalo:
    push si
    cmp byte [tk_over], 2           ; the fly-in does not get one: the letters
    jne .out                        ; are moving, nothing overlaps them for
    mov al, [tk_gosh]               ; longer than a frame, and four extra
    or al, al                       ; passes over forty segments is most of the
    jz .out                         ; second the animation has to play in
    mov si, tk_halosh
    cmp al, 1
    je .go
    mov si, tk_haloout
.go:
    mov [tk_hset], si
    mov al, TKI_MASK                ; not a colour: every bit of one pixel
    call tk_setink
    mov ax, [tk_lsh]                ; the drawing trio out, the erase trio in
    mov [tk_lsave], ax
    mov ax, [tk_lst]
    mov [tk_lsave+2], ax
    mov ax, [tk_lvt]
    mov [tk_lsave+4], ax
    mov ax, [tk_esh]
    mov [tk_lsh], ax
    mov ax, [tk_est]
    mov [tk_lst], ax
    mov ax, [tk_evt]
    mov [tk_lvt], ax
.pass:
    mov si, [tk_hset]
    mov ax, [si]
    cmp ax, 0x7FFF
    je .done
    mov [tk_hox], ax
    mov ax, [si+2]
    mov [tk_hoy], ax
    add word [tk_hset], 4
    call tk_golets
    jmp short .pass
.done:
    mov ax, [tk_lsave]
    mov [tk_lsh], ax
    mov ax, [tk_lsave+2]
    mov [tk_lst], ax
    mov ax, [tk_lsave+4]
    mov [tk_lvt], ax
.out:
    pop si
    ret

; The displacements each mode walks. A shadow is one pass and falls to the
; lower right; an outline is four and surrounds the stroke.
tk_halosh:
    dw 2, 2
    dw 0x7FFF, 0
tk_haloout:
    dw -1, 0
    dw 1, 0
    dw 0, -1
    dw 0, 1
    dw 0x7FFF, 0

; -----------------------------------------------------------------------------
; tk_gooff - the fly-in offset for segment BP, into [tk_gdx]/[tk_gdy]
;
; Direction and distance both come from the INDEX, which is what makes forty
; segments arrive from forty places with nothing stored: 37 is coprime with
; 256 so the headings never repeat, and the distance walks a different cycle
; so two segments on the same bearing still start apart.
; -----------------------------------------------------------------------------
tk_gooff:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, bp
    mov bl, 37
    mul bl
    call tk_sincos                  ; AX = sin, BX = cos
    mov [tk_osin], ax
    mov [tk_ocos], bx
    mov ax, bp                      ; 200 + (53n mod 128) units away
    mov bl, 53
    mul bl
    and ax, 127
    add ax, 200
    mul word [tk_goq]               ; ...of which this much is left
    mov al, ah
    mov ah, dl
    mov bx, ax
    mov [tk_gok], bx
    mov ax, [tk_ocos]
    MUL14
    mov [tk_gdx], ax
    mov ax, [tk_osin]
    mov bx, [tk_gok]
    MUL14
    mov [tk_gdy], ax
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_gopt - AL = a grid x (0..71), AH = a grid y (0..10)
; out: AX = viewport x, BX = viewport y, with this segment's offset on it
; -----------------------------------------------------------------------------
tk_gopt:
    push cx
    push dx
    mov cl, ah
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    add bx, TK_GOX
    mov ax, bx
    call tk_hscalex
    add ax, [tk_gdx]
    add ax, [tk_hox]                ; the halo pass's own displacement, 0 for
    mov [tk_gtx], ax                ; the letters themselves
    mov al, cl
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, ax
    add bx, TK_GOY
    mov ax, bx
    call tk_hscaley
    add ax, [tk_gdy]
    add ax, [tk_hoy]
    mov bx, ax
    mov ax, [tk_gtx]
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; tk_repoint - the prompt block alone, onto the frame already standing
;
; SPEC.md 85.9.1. A typed initial changes three lines of text on a picture that
; is otherwise finished, and rendering the world again for it cost the better
; part of a second a letter on the machine this game is for.
;
; It works because nothing about the frame's bookkeeping has moved since
; tk_r_end: [tk_tbase] still addresses the surface that is on the glass - the
; shadow on CGA and Hercules, and on Mode X the page tk_r_end just flipped TO,
; which is why there is no flip here - and [tk_spcur] still names the span set
; that describes it, so the glyphs widen exactly the record the next full frame
; will erase from. Every glyph cell is opaque (SPEC.md 85.7) and the name line
; only ever GROWS while it is being typed, so nothing has to be cleared first.
; -----------------------------------------------------------------------------
tk_repoint:
    call tk_goprompt
    cmp byte [tk_back], TKB_MODEX   ; Mode X drew straight into the visible
    je .out                         ; page; the other two owe a blit
    call tk_blit
.out:
    ret

; -----------------------------------------------------------------------------
; tk_goprompt - the score, and the two keys that leave this screen
; -----------------------------------------------------------------------------
tk_goprompt:
    mov al, TKI_HUD
    call tk_setink
    mov ax, [tk_score]
    mov dx, [tk_score+2]
    mov di, tk_numbuf
    call tk_utoa
    mov si, tk_s_score              ; "SCORE" and the number, as one run
    mov di, tk_wbuf
    call tk_strcpy
    mov si, tk_numbuf
    call tk_strcpy
    mov si, tk_wbuf
    mov bx, 128                     ; BELOW the horizon on every viewport: at
    call tk_goline                  ; 118 it lands ON it in Mode X, whose 240
                                    ; rows scale this block down onto it
    cmp byte [tk_ient], 0
    je .keys
    mov al, TKI_MARK                ; the row was earned: take a name for it
    call tk_setink
    mov si, tk_s_namep
    mov di, tk_wbuf
    call tk_strcpy
    mov si, tk_ibuf
    call tk_strcpy
    mov si, tk_wbuf
    mov bx, 146
    call tk_goline
    mov si, tk_s_enter
    mov bx, 158
    call tk_goline
    ret
.keys:
    mov al, TKI_MARK
    call tk_setink
    ; THE TWO KEY LINES SHARE ONE LEFT EDGE, and that is the whole reason
    ; they are not centred like the score above them: centring each line
    ; separately puts the N and the F in different columns and the two
    ; actions in different columns again, so nothing lines up with anything.
    ; One left edge, taken from the LONGER line, and all four columns agree.
    mov si, tk_s_gnew
    call tk_strlen
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov ax, [tk_vw]
    sub ax, cx
    jns .lok
    xor ax, ax
.lok:
    shr ax, 1
    and al, 0F8h
    mov [tk_gokx], ax
    mov si, tk_s_gnew
    mov bx, 146
    call tk_gokey
    mov si, tk_s_gexit
    mov bx, 158
    call tk_gokey
    ret

; tk_gokey - SI = a string, BX = its row in 320x200 space; at [tk_gokx]
tk_gokey:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, bx
    call tk_hscaley
    mov dx, ax
    mov cx, [tk_gokx]
    call tk_text
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_goline - SI = a string, BX = its row in 320x200 space; centred
tk_goline:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [tk_gtx], bx                ; the row, kept off the STACK so the
    call tk_strlen                  ; prologue and epilogue stay a mirror pair
    shl cx, 1                       ; CX = cells...
    shl cx, 1
    shl cx, 1                       ; ...as pixels
    mov ax, [tk_vw]
    sub ax, cx
    jns .ok
    xor ax, ax
.ok:
    shr ax, 1
    and al, 0F8h
    mov cx, ax
    mov ax, [tk_gtx]
    call tk_hscaley
    mov dx, ax
    call tk_text
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_strcpy - DS:SI -> DS:DI, NUL included; DI ends ON the NUL
tk_strcpy:
    push ax
.c:
    mov al, [si]
    mov [di], al
    inc si
    or al, al
    jz .d
    inc di
    jmp short .c
.d:
    pop ax
    ret
