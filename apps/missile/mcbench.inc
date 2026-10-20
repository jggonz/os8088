; =============================================================================
; os8088 - apps/missile/mcbench.inc
;
; A DETERMINISTIC IN-GAME RUN, so a before/after can be of the SAME GAME.
;
; Built only under -DMC_BENCH (`make mcbench`); the shipped MISSILE.O88 is
; byte-identical without it, and tests/unit/t_appsmall.py's argument applies
; here too - an instrument that changes the product is not measuring it.
;
; WHY THIS EXISTS. PERFORMANCE.md Set 141.3 priced the walk conversion at
; -33.5% median on `mc_dsc_run` over LIVE PLAY - two trees playing two
; different games, medians over whatever the waves happened to do. That is
; enough to see a sign and not enough to trust a size: Set 141.2 had to
; measure blocks-per-call separately precisely because it varies. This runs
; ONE game, twice.
;
; WHAT MAKES IT DETERMINISTIC, and each of the three had to be true:
;
;   1. THE SEED. Every wave, target and satellite walks down one stream from a
;      single OSAPI_SRAND (mc_entry seeds it off the tick count). Here it is
;      MC_BSEED, so the two arms get the same wave.
;   2. NO CLOCK IN THE FRAME. mc_update and mc_render call OSAPI_GET_TICKS
;      NOWHERE - every timer in the game is a FRAME counter ([mc_hold],
;      [mc_ltick], [mc_et]) - so running frames back to back advances the game
;      exactly as running them one a tick would.
;   3. BACK TO BACK, and this is the one that matters most. mc_worker sleeps to
;      a DEADLINE: a faster frame does not make the game go faster, it makes
;      the worker sleep longer, and the whole win is invisible in wall time.
;      Worse, a frame that overruns a tick takes `.behind` and re-anchors the
;      deadline - which is clock-dependent, so a fast arm and a slow arm would
;      stop playing the same game at the first overrun. So the bench does not
;      go near the worker's loop.
;
; AND THE FOURTH THING, which is proof rather than design: mc_bsum walks the
; game's own physical state at the end and the row asserts the two arms agree.
; "Deterministic" is otherwise an assumption, and the one measurement it would
; be easiest to get silently wrong is the one where the two arms drifted.
;
; The scripted shots are the point of the exercise rather than decoration: the
; ABM trail is the longest walk in the game (base to crosshair, laid in one
; go, SPEC.md 48.14), so a run with no firing would measure the ICBM half and
; leave the player's out.
;
; ON A MACHINE WITH NO DEBUGGER it still answers. [mc_bticks] is the run in
; system ticks and mc_bshow puts it on the glass, so a 5150 reads the same
; number a photograph can carry (docs/FIELD-MACHINES.md).
;
; Prefix mc_b.
; =============================================================================

MC_BSEED    equ 0x5A17              ; ...and nothing about it is special
MC_BFRAMES  equ 400                 ; frames a run: past mc_newgame's 16-frame
                                    ; READY hold and well into wave 1
%ifndef MC_BFIRE                        ; -DMC_BFIRE=<n> picks the scenario:
MC_BFIRE    equ 7                       ; 7 is a BUSY game, near the batch's
%endif                                  ; limit, and a calmer one asks a
                                        ; different question - the conversion's
                                        ; win depends on how many walks a
                                        ; batch step is handed (PERFORMANCE.md
                                        ; Set 141.2), so one fire rate answers
                                        ; for one kind of play and not for the
                                        ; game

; -----------------------------------------------------------------------------
; mc_bench - THE RUN. Called from mc_worker, with the gfx lock FREE, which is
; where a played frame runs and the only place this one can.
;
; The key handler cannot call it: `gfx_lock` is not recursive (SPEC.md 7.3) and
; `ui_task` holds it around the whole event handler (UI-FREEZE-PLAN 1), so
; mc_render's own OSAPI_GFX_LOCK from there blocks the UI task against a lock
; it already owns and yields for ever. It costs 1,800 guest seconds of a
; machine that looks idle and says nothing.
;
; The worker does not reach OSAPI_TASK_ALIVE for the whole run, so a window
; closed mid-bench is noticed at the end of it rather than at once. That is
; acceptable in an instrument nobody ships and would not be in the game.
; preserves all registers
; -----------------------------------------------------------------------------
mc_bench:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, MC_BSEED
    call OSAPI_SRAND
    call mc_newgame                 ; a known board: six cities, three full
                                    ; bases, wave 1, M_READY for 16 frames
    mov word [mc_bfr], 0
    mov word [mc_bck], 0
    mov word [mc_bdrnf], 0
    mov word [mc_bdrnx], 0
    call OSAPI_GET_TICKS
    mov [mc_bt0], ax

    ; THE LOOP IS COUNTED IN MEMORY and the markers are the loop's own top and
    ; bottom, for two reasons. A `push cx` / `pop cx` inside the bracket would
    ; be charged to the frame - 27 cycles of tens of thousands, but charged to
    ; both arms is not the same as charged to neither - and a NASM local label
    ; between two global ones does not survive: `.frame` here would attach to
    ; mc_b_end and the `loop` would not assemble.
mc_b_top:                           ; <-- the host brackets these two, and the
    call mc_bfire                   ;     span between them is one whole frame
    call mc_update
    call mc_render
    inc word [mc_bfr]
    ; ...AND WHAT THE DRAIN IS COSTING THE PICTURE. MC_DRNBUD trades a frame's
    ; peak against how long dead smoke lingers (SPEC.md 48.15 promises "gone in
    ; well under a second"), so a re-tune that only reports cycles is choosing
    ; on half the evidence. [mc_drnq] non-empty IS smoke still on the screen;
    ; counting the frames it is non-empty for makes the look side a number.
    ; Outside the bracket below would be wrong - it is inside, and it is nine
    ; instructions charged to BOTH arms equally.
    cmp word [mc_drnq], 0
    je .noq
    inc word [mc_bdrnf]
    mov ax, [mc_drnq]
    cmp ax, [mc_bdrnx]
    jbe .noq
    mov [mc_bdrnx], ax
.noq:
mc_b_end:
    ; STKBALANCE-OK: mc_b_top and mc_b_end are BRACKET MARKERS, not entry
    ; points - the host arms a breakpoint on each and the span between them is
    ; one frame. A source-reading walker cannot tell a marker from a routine,
    ; so it walks in here at depth 0, falls out of mc_bench's epilogue and
    ; reports a `ret` at -6. The six pops belong to mc_bench's own prologue
    ; six lines up, and nothing jumps or calls to either label.
    cmp word [mc_bfr], MC_BFRAMES
    jb mc_b_top

    call OSAPI_GET_TICKS
    sub ax, [mc_bt0]                ; wrap-safe by subtraction (SPEC.md 8)
    mov [mc_bticks], ax
    call mc_bsum
    call mc_bshow
    inc word [mc_bdone]             ; LAST, and it is what the harness waits
                                    ; on: [mc_bfr] and [mc_bck] are both reset
                                    ; at the top of a run, so reading either
                                    ; one to decide "has it finished" answers
                                    ; ZERO for a run that has just STARTED -
                                    ; which is indistinguishable from a bench
                                    ; that never ran, and was

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_bfire - the scripted shot, off the frame counter
; preserves all registers
;
; It writes exactly what mc_onclick writes ([mc_fire], [mc_fireb], [mc_firex],
; [mc_firey]) and nothing else, so the shot goes down mc_do_fire's own path -
; a bench that launched an ABM directly would skip the base choice, the
; ammunition check and the dry click, and stop being the game.
;
; The point is derived from the frame number against the CONTENT size, so the
; run is the same shape whatever the window is; 37 and 23 are coprime with
; nothing in particular and only have to spread the shots out.
; -----------------------------------------------------------------------------
mc_bfire:
    push ax
    push bx
    push cx
    push dx
    mov ax, [mc_bfr]
    xor dx, dx
    mov cx, MC_BFIRE
    div cx
    or dx, dx                       ; only every MC_BFIRE'th frame
    jnz .out

    mov ax, [mc_bfr]
    mov bx, 37
    mul bx
    xor dx, dx
    mov cx, [mc_cw]
    or cx, cx
    jz .out
    div cx
    mov [mc_firex], dx

    mov ax, [mc_bfr]
    mov bx, 23
    mul bx
    xor dx, dx
    mov cx, [mc_ch]
    shr cx, 1                       ; the top half: a target below the ground
    or cx, cx                       ; line is not a shot anyone takes
    jz .out
    div cx
    mov [mc_firey], dx

    mov byte [mc_fireb], 0FFh       ; the nearest base with missiles, which is
    mov word [mc_fire], 1           ; what a click without a number does
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_bsum - a checksum of the game's PHYSICAL STATE into [mc_bck]
; preserves all registers
;
; THE TABLE IS GAME LOGIC ONLY, and what is left out is the point: every walk
; block (mc_iwlk, mc_awlk), every drawn-to position (mc_ipx, mc_idrw, mc_iarm)
; and the batch itself are the IMPLEMENTATION, and the two arms represent a
; walk differently on purpose - SPEC.md 5.12.5's block is not the kernel's.
; Summing those would fail the agreement test for the one reason that is not a
; divergence. What is in the table is where every object IS and what it is
; doing, which is the game and must match to the byte.
;
; A rotate before each add, so a transposition cannot cancel.
; -----------------------------------------------------------------------------
mc_bsum:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = the running sum
    mov bx, mc_btab
.entry:
    mov si, [bx]
    or si, si
    jz .done
    mov cx, [bx + 2]
.byte:
    rol di, 1
    mov al, [si]
    xor ah, ah
    add di, ax
    inc si
    loop .byte
    add bx, 4
    jmp short .entry
.done:
    mov [mc_bck], di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mc_btab:
    dw mc_mode,    1
    dw mc_wave,    1
    dw mc_mult,    1
    dw mc_onscr,   1
    dw mc_hold,    2
    dw mc_lives,   2
    dw mc_scorelo, 2
    dw mc_scorehi, 2
    dw mc_icbleft, 2
    dw mc_smleft,  2
    dw mc_ltick,   2
    dw mc_calive,  MC_NCITY
    dw mc_balive,  MC_NBASE
    dw mc_bmis,    MC_NBASE
    dw mc_ia,      MC_MAXICBM
    dw mc_ix16,    MC_MAXICBM * 2
    dw mc_iy16,    MC_MAXICBM * 2
    dw mc_ivx,     MC_MAXICBM * 2
    dw mc_ivy,     MC_MAXICBM * 2
    dw mc_aa,      MC_MAXABM
    dw mc_ax16,    MC_MAXABM * 2
    dw mc_ay16,    MC_MAXABM * 2
    dw mc_ea,      MC_MAXEXP
    dw mc_ex,      MC_MAXEXP * 2
    dw mc_ey,      MC_MAXEXP * 2
    dw mc_et,      MC_MAXEXP
    dw 0

; -----------------------------------------------------------------------------
; mc_bshow - the result on the GLASS, for a machine with no debugger
; preserves all registers
;
; Ticks and the checksum, in the status line's own opaque run. The cycle
; figures are the host's; this is what a 5150 can be photographed saying.
;
; IT TAKES THE LOCK AND SETS THE CLIP, because mc_bench runs on the WORKER and
; the worker holds neither - mc_render takes both for itself every frame and
; this is a frame's worth of drawing outside it. Drawing without them from a
; worker is not a refusal you can see: it hangs, and the run never reaches
; [mc_bdone], which reads as a bench that never started.
mc_bshow:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, mc_bmsg + MC_BOTICK
    mov ax, [mc_bticks]
    call mc_bhex
    mov si, mc_bmsg + MC_BOSUM
    mov ax, [mc_bck]
    call mc_bhex

    call OSAPI_GFX_LOCK
    mov bx, [mc_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    mov si, mc_bmsg
    mov word [mc_runw], MC_BMSGW    ; mc_runc erases the field by hand on the
    mov al, CBLACK                  ; Mode X surface and wants its pixel width
    mov ah, CWHITE
    mov cx, 4
    mov dx, 4
    call mc_runc                    ; the OPAQUE run (SPEC.md 6.1): ground and
.unlock:                            ; glyph in one pass, as every field here
    call OSAPI_GFX_UNLOCK
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mc_bhex - AX as four hex digits at DS:SI; preserves all registers
;
; `rol ax, cl` four times returns AX to itself, and each turn brings the next
; nibble down from the top - so no shift count and no scratch copy. `shl ax, 4`
; would be the obvious spelling and is not an 8086 instruction (CLAUDE.md's
; first hard rule), which is why CL carries the 4 and the stack carries the
; loop counter round it.
mc_bhex:
    push ax
    push bx
    push cx
    push si
    mov cx, 4
.digit:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    mov bx, ax
    and bx, 0x000F
    push ax
    mov al, [mc_bhexd + bx]
    mov [si], al
    pop ax
    inc si
    loop .digit
    pop si
    pop cx
    pop bx
    pop ax
    ret

mc_bhexd:  db '0123456789ABCDEF'

; --- the bench's own state, INLINE rather than in the bss block --------------
; A package's image is loaded into its heap claim (SPEC.md 20.1), so inline
; data is writable RAM - mc_step3 above is the same thing. Kept here so that
; the whole instrument is ONE include and THREE `%ifdef` hooks, which is what
; makes it portable to an older tree for the other arm of the comparison.
mc_bdone:   dw 0                    ; runs COMPLETED - the only monotonic
                                    ; thing here, and so the only safe wait
mc_breq:    dw 0                    ; the key handler's request, read by the
                                    ; WORKER - see mc_worker's %ifdef
mc_bfr:     dw 0                    ; frames run so far
mc_bt0:     dw 0                    ; ticks at the start
mc_bticks:  dw 0                    ; ...and the run's duration in them
mc_bck:     dw 0                    ; the end-state checksum
mc_bdrnf:   dw 0                    ; frames with the drain queue NON-EMPTY -
                                    ; dead smoke still on the screen
mc_bdrnx:   dw 0                    ; ...and the deepest that queue ever got

; --- and the message LAST, with its two fields COUNTED ----------------------
; mc_bhex writes four bytes and the offsets were hand-counted wrong the first
; time - +6 and +18 against the right +8 and +15 - so the second field ran four
; bytes off the end of the string and into [mc_bdone], the word the harness
; waits on. Nothing looked wrong: the run finished, every figure it published
; was correct, and the completion counter read 14,147. Derived from labels
; here, so the string can be reworded without a second count.
mc_bmsg:   db 'BENCH t='
mc_btick:  db '0000'
           db ' s='
mc_bsumd:  db '0000', 0
MC_BOTICK  equ mc_btick - mc_bmsg
MC_BOSUM   equ mc_bsumd - mc_bmsg
MC_BMSGW   equ 19 * 8            ; ...and its width in pixels, for mc_runc
