; The frame loop, the pacing governor and the scripted scroll test (SPEC.md 102.1,
; plan 7 and 8).  Wave 2 drives the scroll engine from a TEST TRACK: the first
; course's compiled column ids scrolled at a scripted speed under one stationary
; bike, with a ticking HUD clock.  The variables the test harness pokes are
; documented in SPEC.md 102.6 (xb_tpause, xb_tframes, xb_tset/xb_tcol,
; xb_tspeed, xb_tburst, xb_bikes).
;
; THE CLOCK.  A frame's period is a whole number n of FASTTICK sub-ticks
; (54.62 Hz, 87,381 clocks each).  No API returns the sub-tick counter, so it
; is made here: the PIT's channel 0 counts DOWN from 21,845 once a sub-tick, and
; a sample that reads a LARGER count than the last one has seen exactly one
; reload.  That is exact as long as no two samples are a sub-tick apart, and
; every stretch of the frame BETWEEN SAMPLES is 60,000 clocks or less: a frame of four
; bikes and three opponents is 130-250k clocks, so the back ends and the opponents' loop
; call xb_clk_sample at their seams (per column, per bike, per footprint, per box): a
; sample missed is a sub-tick lost, and a lost sub-tick is a game that runs at half
; speed (wave 4 measured 30 steps a second with the seams out).  A sample is ~140 clocks.

; ---- back end dispatch --------------------------------------------------------
; xb_race_open builds what outlives a race (the sprite claim, the dictionaries);
; xb_race_begin what a race owns (the pages, the split, the HUD row); xb_backend_menu
; hands the card back to the menus.
xb_race_open:
    SAVE
    call xb_spr_load
    jc .fail
    call xa_tables                  ; the opponents' look-ahead tables
    cmp byte [xb_cga], 0
    jne .cga
    call xv_init
    jmp .ok
.cga:
    call xc_init
    jc .fail
.ok:
    RESTORE
    clc
    ret
.fail:
    RESTORE
    stc
    ret

xb_race_begin:
    cmp byte [xb_cga], 0
    jne .cga
    jmp xv_start
.cga:
    jmp xc_start

xb_backend_menu:
    cmp byte [xb_cga], 0
    jne .cga
    jmp xv_menu
.cga:
    jmp xc_menu

xb_race_close:
    cmp byte [xb_cga], 0
    je .out
    call xc_free
.out:
    ret

xb_render:
    SAVE
    cmp byte [xb_cga], 0
    jne .cga
    call xv_render
    jmp xb_presented
.cga:
    call xc_render
xb_presented:
    RESTORE
    ret

; ---- the sub-tick clock -------------------------------------------------------
xb_clk_sample:
    push ax
    pushf
    cli
    xor al, al
    out 43h, al                     ; latch channel 0
    in al, 40h
    mov ah, al
    in al, 40h
    xchg al, ah
    popf
    cmp ax, [cs:xb_pitl]            ; (CS-relative: the heavy routines call this with DS = the
    jbe .same                       ; card or a claim, at the seams between their pieces)
    inc word [cs:xb_subs]           ; the count went UP: one reload
.same:
    mov [cs:xb_pitl], ax
    pop ax
    ret

; AX = whole sub-ticks crossed, BX = the phase difference (signed, |BX| < XB_SUB)
; -> DX:AX = the span in PIT counts.  Clobbers BX, CX.
xb_span:
    or bx, bx
    jns .p
    dec ax
    add bx, XB_SUB
.p:
    mov cx, XB_SUB
    mul cx
    add ax, bx
    adc dx, 0
    ret

; ---- the race loop ------------------------------------------------------------
; xb_race runs one race (or the wave-2 scroll test, or the attract demo) and returns
; how it ended in [xb_result]: XR_ESC, XR_FINISH (the rider crossed the line and
; coasted XB_FIN_STEPS), XR_ALT (Alt+Enter: leave the bracket), XR_KEY (a key ended the
; demo), XR_DONE (the demo ran out).
xb_race:
    SAVE
    mov byte [xb_result], XR_ESC
    mov byte [xb_end], 0
    call xb_race_init
.frame:
    call os88alt_edge
    jnc .noalt
    mov byte [xb_result], XR_ALT
    jmp .out
.noalt:
    cmp byte [xb_end], 0
    je .keys
    mov al, [xb_end]
    dec al
    mov [xb_result], al
    jmp .out
.keys:
    mov ah, 1
    int 16h
    jz .nokey
    xor ah, ah
    int 16h
    call xb_racekey
    jc .out
    jmp .keys
.nokey:
    call xb_clk_sample
    mov ax, [xb_subs]
    mov [xb_f0], ax
    mov ax, XB_SUB
    sub ax, [xb_pitl]
    mov [xb_u0], ax                 ; the phase inside the sub-tick, where the work starts
    ; the exact length of the frame that just ended, in PIT counts (21,845 a sub-tick):
    ; from ITS start sample to THIS one, so the frames telescope and no time is lost
    ; between them - the steps of this frame are made from it, and game speed is right
    ; whatever the frame rate is
    mov ax, [xb_f0]
    sub ax, [xb_f0p]
    mov bx, [xb_u0]
    sub bx, [xb_u0p]
    call xb_span
    cmp dx, 20h                     ; a stall of over ~5 s: saturate, so the step count's
    jbe .spanok                     ; top byte cannot wrap past 255
    mov ax, 0FFFFh
    mov dx, 20h
.spanok:
    mov [xb_pcl], ax
    mov [xb_pch], dx
    mov ax, [xb_f0]
    mov [xb_f0p], ax
    mov ax, [xb_u0]
    mov [xb_u0p], ax
    mov word [xb_spinl], 0
    mov word [xb_spinh], 0
    call xb_frame_step
    call xb_clk_sample              ; how long the SIMULATION took: the governor must be able to
    mov ax, [xb_subs]               ; tell the part of the frame that scales with the steps from
    sub ax, [xb_f0]                 ; the part that does not (see xb_gov)
    mov bx, XB_SUB
    sub bx, [xb_pitl]
    sub bx, [xb_u0]
    call xb_span
    mov [xb_swl], ax
    mov [xb_swh], dx
    cmp byte [xb_tpause], 0
    jne .paused
    call xb_render
    inc word [xb_frames]
    cmp word [xb_tframes], 0
    je .chkdone
    dec word [xb_tframes]
    jnz .chkdone
    mov byte [xb_tpause], 1         ; the harness asked for exactly N frames
.chkdone:
    cmp byte [xb_tdone], 0          ; the harness's script ran out this frame: the frame that
    je .paused                      ; ran it out has been DRAWN (the final state is on the
    mov byte [xb_tdone], 0          ; glass), and now the game holds
    mov byte [xb_tpause], 1
.paused:
    call xb_gov
    call xu_race_frame              ; the sound, in the frame's idle time: after the governor has measured the
xb_flushed equ $                    ; work, before the wait (audio.inc; the mark is the harness's)
.wait:
    call xb_clk_sample
    mov ax, [xb_subs]
    sub ax, [xb_f0]
    cmp byte [xb_cga], 0            ; the CGA is paced by its retrace (xc_vr, 102.1), not by n:
    jne .go                         ; a frame ends when its card writes have, and n only reports
    cmp ax, [xb_n]                  ; what the work needs
    jae .go
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    ; FSXW_FRAME returns only after a NEW IRQ0, so at least one sub-tick boundary came
    ; since the sample before the call.  The PIT comparison cannot see it when the task
    ; resumed a little later in this sub-tick than in the last (the count is then SMALLER
    ; than the previous sample's), and two such samples one sub-tick apart are exactly
    ; what a wait of n >= 2 takes: the loop then waited a whole extra sub-tick - frames of
    ; 3.6 and 4.5 sub-ticks at n = 2 on the CGA (wave 3 measured it).  Count the boundary
    ; the wait itself proves.
    mov ax, [xb_subs]
    call xb_clk_sample
    cmp ax, [xb_subs]
    jne .wait
    inc word [xb_subs]
    jmp .wait
.go:
    cmp ax, XB_STEPS_MAX
    jbe .per
    mov ax, XB_STEPS_MAX
.per:
    mov [xb_period], ax             ; sub-ticks since the last frame started
    jmp .frame
.out:
    RESTORE
    ret

; AX = a typed key (AH scancode, AL ascii) -> CF=1: the race is over
xb_racekey:
    cmp al, 27
    je .esc
    cmp byte [xb_attract], 0
    jne .demokey                    ; any key ends the attract demo
    cmp al, 13
    je .pause
    or al, 20h
    cmp al, 'c'
    jne .notc
    call xb_palette_next
    clc
    ret
.notc:
    cmp al, 'm'
    jne .notm
    call xu_toggle                  ; M: sound on / off
    clc
    ret
.notm:
    cmp al, 'p'
    jne .none
    cmp byte [xb_noflow], 0
    jne .harness
.pause:
    cmp byte [xb_noflow], 0
    jne .none                       ; (Enter means nothing in the raw loop)
    xor byte [xb_paused], 1
    mov byte [xb_huddirty], 2
    call xu_pause                   ; the blip, and whatever was sounding stops (audio.inc)
    clc
    ret
.harness:
    xor byte [xb_tpause], 1         ; the raw loop's pause is the harness's
.none:
    clc
    ret
.demokey:
    mov byte [xb_result], XR_KEY
    stc
    ret
.esc:
    cmp byte [xb_attract], 0
    jne .demokey
    mov byte [xb_result], XR_ESC
    stc
    ret

xb_race_init:
    mov word [xb_subs], 0
    mov word [xb_pitl], 0
    call xb_clk_sample              ; primes the last count; a stray wrap is harmless
    mov word [xb_subs], 0
    mov word [xb_n], 1
    mov word [xb_period], 1
    mov byte [xb_quiet], 0
    mov word [xb_acc], 0
    mov word [xb_acc + 2], 0
    mov ax, [xb_subs]
    mov [xb_f0p], ax
    mov ax, XB_SUB
    sub ax, [xb_pitl]
    mov [xb_u0p], ax
    mov word [xb_sacc], 0
    mov word [xb_sacc + 2], 0
    mov word [xb_pcl], 0
    mov word [xb_pch], 0
    mov word [xb_pos], 0
    mov word [xb_pos + 2], 0
    mov word [xb_S], 0
    mov ax, [xb_ncols]
    sub ax, [xb_wcols]
    mov [xb_Smax], ax
    mov word [xb_tspeed], XB_TSPEED
    mov byte [xb_tpause], 0
    mov word [xb_tframes], 0
    mov byte [xb_tset], 0
    mov word [xb_tburst], 0
    mov word [xb_cs], 0
    mov word [xb_ct10], 0
    mov word [xb_frames], 0
    mov word [xb_cslast], 0ffffh
    ; the HUD string and what is drawn: nothing yet
    push ds
    pop es
    mov si, xb_hudtpl
    mov di, xb_hudstr
    mov cx, XB_HUDLEN
    cld
    rep movsb
    mov di, xb_hudcur
    mov cx, XB_HUDLEN
    mov al, 0ffh
    rep stosb
    mov byte [xb_huddirty], 2
    ; one stationary bike: pose 0, 88 pixels from the left, in the lanes
    mov di, xb_bikes
    mov cx, XB_MAXBIKES * XB_BIKE_SIZE
    xor al, al
    rep stosb
    mov byte [xb_bikes], 1
    mov byte [xb_bikes + 1], 0
    mov word [xb_bikes + 2], 88
    mov word [xb_bikes + 4], 100
    mov byte [xb_bikes + 6], 10
    mov byte [xb_simon], 1          ; the race; the wave-2 scroll test is `xb_simon` = 0
    mov word [xb_tsi], 0
    mov word [xb_tsn], 0
    mov word [xb_cd], 0
    mov word [xb_flash], 0
    mov word [xb_fint], 0
    mov word [xb_ksteps], 0
    mov word [xb_attn], 0
    mov byte [xb_paused], 0
    mov byte [xb_inp], 0
    cmp byte [xb_noflow], 0
    jne .raw
    mov word [xb_cd], XB_COUNTDOWN  ; READY 3, 2, 1 - the rider and the clock wait
.raw:
    cmp word [xb_rng], 0
    jne .seeded
    mov word [xb_rng], 0ace1h
.seeded:
    call xa_init                    ; the opponents (if any) and the rider, fresh
    ret

xb_hudtpl: db 'TIME 0:00.00 1 ]]]]]'

; ---- the governor (plan 8) ----------------------------------------------------
; work = how long the frame's work took, in sub-ticks, rounded up.  n is raised
; to it at once (an overrun is paid for immediately, so the next frame is not
; late as well) and lowered by one only after XB_QUIET consecutive frames whose
; work fits (n - 1) whole sub-ticks: even pacing beats a higher peak rate (PERFORMANCE.md
; Part 3.2), and a governor that dithers is uneven pacing.
xb_gov:
    cmp byte [xc_to], 0             ; a frame the CGA skipped (no retrace) measures nothing: it
    jne .skipped                    ; counts as quiet without being judged.  The catch-up frame
    jmp .nskip                      ; after it IS measured - a burst landing in it must raise n
.skipped:                           ; within the frame - but it may only raise n, never judge
    mov byte [xc_after], 1          ; the quiet count (the VGA never sets either flag)
.neutral:
    cmp word [xb_n], 1
    jbe .out
    cmp byte [xb_quiet], XB_QUIET
    jae .out
    inc byte [xb_quiet]
    ret
.nskip:
    cmp byte [xb_bigframe], 0       ; a full-window write (the first frames, a jump) is a
    je .measure                     ; one-off, not the steady state the governor paces
    mov byte [xb_bigframe], 0
    mov byte [xc_after], 0
    mov byte [xb_quiet], 0
    ret
.measure:
    call xb_clk_sample
    mov ax, [xb_subs]
    sub ax, [xb_f0]
    mov bx, XB_SUB
    sub bx, [xb_pitl]
    sub bx, [xb_u0]                 ; the phase difference, signed
    call xb_span                    ; DX:AX = the frame so far in PIT counts
    sub ax, [xb_spinl]              ; ...less the time spent waiting for the retrace
    sbb dx, [xb_spinh]
    jns .work
    xor ax, ax
    xor dx, dx
.work:
    mov cx, XB_SUB
    mov [xb_wl], ax
    mov [xb_wh], dx
    div cx
    or dx, dx
    jz .whole
    inc ax
.whole:
    mov [xb_work], ax
    cmp ax, [xb_n]
    jbe .fits
    cmp ax, 4
    jbe .raise
    mov ax, 4
.raise:
    mov [xb_n], ax
    mov byte [xc_after], 0
    mov byte [xb_quiet], 0
    ret
.fits:
    cmp byte [xc_after], 0          ; the catch-up frame fits: neutral, as it always was
    je .fits2
    mov byte [xc_after], 0
    jmp .neutral
.fits2:
    cmp word [xb_n], 1
    jbe .steady
    ; quiet = the work this frame would have taken ONE SUB-TICK SHORTER fits that shorter
    ; frame.  The work as measured is at THIS n's step count, and the simulation - the part
    ; that scales with the steps - is 60% of a Selection B frame: at n = 3 it makes 3.7 steps
    ; and 186k clocks of work, which is over two sub-ticks, so a governor that asked "does
    ; this fit n - 1" could never come down although a frame of n - 1 = 2's 2.5 steps is 150k
    ; and fits with room.  Prediction: work - simulation / n, and 6% over that; a wrong one
    ; costs a raise (immediate) and 64 more quiet frames, and the wave-2 governor test (a
    ; burst, no simulation) is unchanged.  (A 0.8 margin here once left a CGA whose steady
    ; work is 0.83-0.93 of a sub-tick unable ever to come down from 2.)
    mov ax, [xb_swl]
    mov dx, [xb_swh]
    mov cx, [xb_n]
    cmp dx, cx
    jae .loud
    div cx                          ; AX = simulation / n
    mov bx, ax
    mov ax, [xb_wl]
    mov dx, [xb_wh]
    sub ax, bx
    sbb dx, 0
    jb .loud
    mov bx, ax                      ; + 1/16
    mov cx, dx
    mov si, 4
.sh:
    shr cx, 1
    rcr bx, 1
    dec si
    jnz .sh
    add ax, bx
    adc dx, cx
    mov cx, XB_SUB
    cmp dx, cx
    jae .loud
    div cx
    or dx, dx
    jz .pw
    inc ax
.pw:
    mov cx, [xb_n]
    dec cx
    cmp ax, cx
    jbe .quiet
.loud:                              ; (the 64 frames are the hysteresis; a frame that really
    mov byte [xb_quiet], 0          ; needs n resets it)
    ret
.quiet:
    inc byte [xb_quiet]
    cmp byte [xb_quiet], XB_QUIET
    jb .steady
    dec word [xb_n]
.steady:
    cmp word [xb_n], 1
    jne .out
    mov byte [xb_quiet], 0
.out:
    ret

; ---- the frame's simulation ---------------------------------------------------------
xb_frame_step:
    cmp byte [xb_simon], 0
    je xb_tt_step
; xb_sim_frame - the steps of this frame, then the records the back end draws.
; Steps a frame = 1.10033 x the sub-ticks since the last frame started (a 16.16
; accumulator, at most XB_STEPS_MAX: a slow machine slows the game, it never
; spirals).  With the harness feeding a script (xb_tmode) each step takes the next
; script byte and leaves a trace record; the script's end (xb_tsn) pauses the game after
; the frame that reached it has been drawn (xb_tdone).
; Both are RINGS of XB_TSTEPS entries indexed by xb_tsi, so a harness that stays
; ahead of the game feeds an endless run (tests/excitebike_perf.py --lap).
xb_sim_frame:
    cmp byte [xb_treset], 0         ; the harness asks for a fresh rider, paused or not
    je .noreset
    mov byte [xb_treset], 0
    call xa_init
.noreset:
    cmp byte [xb_tpause], 0
    jne .out
    mov word [xb_ksteps], 0
    ; steps = the frame's PIT counts x 1.10033 / 21,845: a 32-bit accumulator in 2^-24 of a
    ; step, XB_STEPK (845) added a count, so the whole steps are its top byte
    mov cx, XB_STEPK
    mov ax, [xb_pcl]
    mul cx
    add [xb_sacc], ax
    adc [xb_sacc + 2], dx
    mov ax, [xb_pch]
    mul cx
    add [xb_sacc + 2], ax
    mov al, [xb_sacc + 3]
    mov byte [xb_sacc + 3], 0
    xor ah, ah
    cmp ax, XB_STEPS_MAX
    jbe .steps
    mov ax, XB_STEPS_MAX
.steps:
    mov [xb_steps], ax
    cmp byte [xb_paused], 0
    je .running
    mov word [xb_sacc], 0           ; a paused game makes no steps and owes none
    mov word [xb_sacc + 2], 0
    xor ax, ax
    mov [xb_steps], ax
.running:
    cmp byte [xb_attract], 0
    jne .noinput                    ; the demo's rider is driven by xa_input, not the keys
    call xb_input
.noinput:
    mov ax, [xb_steps]
    mov cx, ax
    jcxz .render
.step:
    push cx
    cmp byte [xb_tmode], 0
    je .live
    mov bx, [xb_tsi]
    cmp bx, [xb_tsn]
    jae .stop
    and bx, XB_TSTEPS - 1           ; the script and the trace are rings of XB_TSTEPS
    mov al, [xb_tscript + bx]
    mov [xm_inp], al
    call xm_step
    call xm_trace
    inc word [xb_tsi]
    inc word [xb_ksteps]
    mov ax, [xb_tsi]
    cmp ax, [xb_tsn]
    jb .next
.stop:
    mov byte [xb_tdone], 1          ; the script is spent: draw this frame, then hold
    pop cx
    jmp .render
.live:
    cmp word [xb_cd], 0
    je .go
    dec word [xb_cd]                ; READY: nobody moves and the clock does not run
    jnz .next
    mov word [xb_flash], XB_GO_STEPS
    mov byte [xb_flashk], 0
    jmp .next
.go:
    mov al, [xb_inp]
    cmp byte [xb_attract], 0
    je .human
    mov word [xa_cur], xa_recs + 3 * XA_REC_SZ
    call xa_input
.human:
    mov [xm_inp], al
    call xm_step
    inc word [xb_ksteps]
.next:
    pop cx
    loop .step
.render:
    cmp byte [xb_nai], 0
    je .noai
    call xa_frame                   ; the opponents make the same steps and get their records (ai.inc)
.noai:
    call xm_bikes
    call xb_hud_sim
    cmp byte [xb_noflow], 0
    jne .noflow
    call xb_flow_frame              ; the lap flash, the finish, the demo's end (flow.inc)
    call xb_banner                  ; ...and the banner plates' flash (video.inc; VGA only)
.noflow:
    mov ax, [xm_pos + 1]
    shr ax, 1
    shr ax, 1
    shr ax, 1                       ; the window's column: the bike's pixel / 8
    cmp ax, [xb_Smax]
    jbe .s
    mov ax, [xb_Smax]
.s:
    mov [xb_S], ax
    mov al, [xm_ev]                 ; this frame's events, for the sound: it runs after the governor (below)
    or [xu_ev], al
    mov byte [xm_ev], 0
.out:
    ret

; ---- the scroll test track (wave 2's stand-in for the simulation) --------------
xb_tt_step:
    mov cx, [xb_tburst]             ; the governor test's injected load
    jcxz .noburst
    mov word [xb_tburst], 0
.burst:
    push cx
    mov cx, 1024
.spin:
    loop .spin                      ; 1024 x 17 = 17.4k clocks
    call xb_clk_sample              ; a wrap must never go unseen
    pop cx
    loop .burst
.noburst:
    cmp byte [xb_tpause], 0
    jne .out
    cmp byte [xb_tset], 0
    je .step
    mov byte [xb_tset], 0
    mov ax, [xb_tcol]               ; a jump: position = column << 11 (8.8 of a pixel)
    cmp ax, [xb_Smax]
    jbe .jump
    mov ax, [xb_Smax]
.jump:
    mov dx, ax
    mov cl, 11
    shl ax, cl
    mov cl, 5
    shr dx, cl
    mov [xb_pos], ax
    mov [xb_pos + 2], dx
    jmp .col
.step:
    mov ax, [xb_period]             ; steps = 1.10033 x sub-ticks, by a 16.16 accumulator
    add [xb_acc + 2], ax
    mov cx, XB_STEP_LO
    mul cx
    add [xb_acc], ax
    adc word [xb_acc + 2], 0
    mov ax, [xb_acc + 2]
    mov word [xb_acc + 2], 0
    cmp ax, XB_STEPS_MAX
    jbe .steps
    mov ax, XB_STEPS_MAX            ; below ~7 fps the game slows, it never spirals
.steps:
    mov [xb_steps], ax
    mul word [xb_tspeed]
    add [xb_pos], ax
    adc [xb_pos + 2], dx
    mov ax, [xb_steps]              ; the game clock runs 1.664 cs a step (1664 thousandths:
    mov cx, XP_CLOCK16              ; 60.1 steps/s x 1.664 = 100 cs/s, real seconds)
    mul cx
    add [xb_ct10], ax
.cs:
    cmp word [xb_ct10], 1000
    jb .col
    sub word [xb_ct10], 1000
    cmp word [xb_cs], 59999         ; the clock stops at 9:59.99 rather than wrapping
    jae .cs
    inc word [xb_cs]
    jmp .cs
.col:
    mov ax, [xb_pos]                ; S = position >> 11
    mov cl, 11
    shr ax, cl
    mov dx, [xb_pos + 2]
    mov cl, 5
    shl dx, cl
    or ax, dx
    cmp ax, [xb_Smax]
    jbe .set
    mov ax, [xb_Smax]
    mov dx, ax                      ; hold at the end of the course
    mov cl, 11
    shl ax, cl
    mov cl, 5
    shr dx, cl
    mov [xb_pos], ax
    mov [xb_pos + 2], dx
    mov ax, [xb_Smax]
.set:
    mov [xb_S], ax
    call xb_hud_build
.out:
    ret

; "TIME m:ss.cc N #####" from the game clock in hundredths
xb_hud_build:
    mov ax, [xb_cs]
    cmp ax, [xb_cslast]
    je .out
    mov [xb_cslast], ax
    xor dx, dx
    mov cx, 6000
    div cx
    cmp al, 9
    jbe .min
    mov al, 9
.min:
    add al, '0'
    mov [xb_hudstr + 5], al
    mov ax, dx
    xor dx, dx
    mov cx, 100
    div cx
    aam
    add ax, 3030h
    mov [xb_hudstr + 7], ah
    mov [xb_hudstr + 8], al
    mov ax, dx
    aam
    add ax, 3030h
    mov [xb_hudstr + 10], ah
    mov [xb_hudstr + 11], al
    cmp byte [xb_huddirty], 2       ; a full paint is still owed
    je .out
    mov byte [xb_huddirty], 1
.out:
    ret
