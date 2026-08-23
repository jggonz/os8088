; =============================================================================
; os8088 - blank.inc
;
; The idle screen saver (SPEC.md 64, 79). After the configured idle period
; with no keyboard and no mouse, the machine either DRAWS - a spinning cube, a
; starfield, geometric shapes, fish - or, if it cannot, darks every monitor it
; has. The next input brings the desktop back, and that input is swallowed
; rather than acted on.
;
; It is for the MONITOR and not for the machine. The tubes this OS runs in
; front of - a 5151, a 5153, whatever is on the second card - burn a static
; image into the phosphor if it is left lit, and a desktop is the worst
; possible thing to leave there: a menu bar, a dock strip and a drive column
; that never move, at full brightness, for as long as the machine is on.
; Nothing about it is a power feature; there is no DPMS on hardware this old
; and none is attempted.
;
; TWO ANSWERS, AND THE SECOND IS THE FALLBACK FOR THE FIRST (SPEC.md 79.1):
;
;   the ANIMATION   SAVER.DRV, an overlay read off the system volume when the
;                   idle period runs out and freed when it ends (SPEC.md
;                   79.2). It is kern_big's alone, it is not in the kernel
;                   image, and every way it can fail - no system disk, no
;                   heap, a missing file, no mode ticked - lands on the
;                   blanker below rather than on a message.
;
;   the BLANKER     what this file always did. vid_blank_kind (SPEC.md
;                   39.18.1) gates the card's video output and leaves the CRTC
;                   running, so both syncs keep coming out, the monitor never
;                   re-acquires, and memory is untouched. The screen comes
;                   back INSTANTLY and exactly as it was, with nothing to
;                   repaint and no mode set; a background task may keep
;                   drawing into a blanked card with no more than a compare
;                   anywhere in the drawing path - which is why there is not
;                   one gate in gfx_*; and the blank cannot be undone by
;                   accident, because nothing in the kernel writes those mode
;                   registers except vid_setmode and the pair above.
;
; The animation gives up all three of those and pays for them: it draws, so
; the desktop underneath it is gone and has to be repainted on the way out
; (ss_stop_x), and the cursor has to come off the glass for the duration or
; its save-under would bank a starfield. THAT is what SPEC.md 64.5 said was
; not worth having, and what SPEC.md 79 reverses and why.
; =============================================================================

; --- the idle period ---------------------------------------------------------
;
; [ticks] is the BIOS counter at 18.2065 Hz, so a minute is 1,092.4 of them and
; five minutes is 5,461.95. The rate stays 18.2065 Hz whether or not SPEC.md
; 53.2.1's sub-tick is armed: that divides IRQ0 three ways and only every third
; entry bumps [ticks].
;
; BLK_IDLE IS THE DEFAULT AND NO LONGER THE ANSWER. [ss_idle] is what blk_pass
; compares against, because SPEC.md 79.4 made the period a setting; this is
; what a machine with no SYSTEM.CFG starts at, and the two agree by
; construction because the constant is written once and multiplied here.
SS_TPM      equ 1092            ; ticks in a minute, rounded down
SS_MINS     equ 5               ; ...and the default, in minutes
BLK_IDLE    equ SS_MINS * SS_TPM

; --- the settings (SPEC.md 79.4) ---------------------------------------------
; Which modes may be picked, how often one is swapped for another, and how
; long the machine has to be left alone first. THE OVERLAY IS HANDED A POINTER
; TO THIS BLOCK and reads and writes it through ES (SPEC.md 79.7), so the four
; bytes are contiguous and their order is an interface rather than a layout.
SS_M_CUBE   equ 1               ; the bits of [ss_modes]
SS_M_STARS  equ 2
SS_M_SHAPES equ 4
SS_M_FISH   equ 8
SS_M_ALL    equ 15              ; ...all four, and it is a LITERAL rather than
                                ; the four OR-ed together because
                                ; tests/unit/t_mirror.py compares the first
                                ; token after `equ` across the two files that
                                ; spell these out - an expression here and a
                                ; number in the overlay would read as a
                                ; divergence, and hiding the check to keep the
                                ; prettier spelling is the wrong way round
SS_NMODE    equ 4               ; ...and how many bits that is

SS_P_MODES  equ 0               ; the block, as the overlay indexes it
SS_P_SECS   equ 1
SS_P_MINS   equ 2
SS_P_POST   equ 3
SS_P_IDLE   equ 4               ; ...and the DERIVED period, in ticks, which
                                ; the settings window's Test button writes a 1
                                ; into to bring a session on NOW (SPEC.md
                                ; 79.7). It is derived and not stored, so
                                ; overwriting it is not a setting change - the
                                ; next post re-derives it from [ss_mins], and
                                ; ending the test IS that post

; --- what the kernel asks the overlay for (SPEC.md 79.3) ---------------------
; AL on the far call into its entry proc, drv_call's own contract. MIRRORED IN
; drivers/saver/saver.asm and gated by tests/unit/t_mirror.py, which is the
; only thing in this tree that can notice the two drifting apart.
;
; **THE OVERLAY PRESERVES EVERY REGISTER BUT AX**, which is why the glue below
; pushes almost nothing: seven pops on its side cost the floppy and nothing
; here, and `pop` writes no flag, so the CF a verb answers in survives them.
;
; **BX IS NOT AN ARGUMENT TO ANY VERB.** ss_call spends it on the row, so a
; verb that took its argument there would make the dispatcher the pointer's
; owner - drv_svc_call's recorded bug (SPEC.md 51.2), avoided by choosing the
; register rather than by remembering. SSV_CFG takes SI for that reason.
SSV_START   equ 0               ; a session begins. In: SI = the settings
                                ; block's offset in KERNEL_SEG, GFX LOCK HELD.
                                ; Out: CF = 1 = it cannot, so blank the signal
SSV_FRAME   equ 1               ; one frame IF one is due; it owns the clock
                                ; AND takes the lock itself, having refused
                                ; most of the calls before it gets that far
SSV_STOP    equ 2               ; the session is over. Cannot fail; no lock
SSV_CFG     equ 3               ; open the settings window. In: SI = the
                                ; settings block, GFX LOCK HELD - it is a
                                ; Control Panel click (SPEC.md 31.1)
SSV_BUSY    equ 4               ; may your image be freed? Out: CF = 1 = no,
                                ; the settings window is still on screen

; -----------------------------------------------------------------------------
; State. In .text with real initialisers rather than in .bss, and here that is
; not the usual caution - .bss does arrive zeroed (kernel.asm: the image's gap
; to OVL_START is emitted as zeros and the boot sector's one contiguous read
; lands them), and it costs the same image bytes either way, because the rung
; KIMG_PARA rounds covers .text and .bss together.
;
; It is here because [blk_t0] = 0 is not scratch, it is the ANSWER: the idle
; period starts when the machine does, so a desktop nobody ever touches saves
; itself five minutes after it is drawn. Written down as an initialiser that
; reads as a decision, rather than as a zero somebody has to go and confirm.
; -----------------------------------------------------------------------------
blk_t0      dw 0                ; [ticks] when the current idle period began
blk_on      db 0                ; is the desktop off the glass right now? TRUE
                                ; FOR BOTH ANSWERS - a gated signal and a
                                ; running animation are the same thing to
                                ; blk_wake, which is what keeps SPEC.md 64.2's
                                ; whole input rule one test
blk_act     db 0                ; mou_isr saw a packet; the UI task spends it

; -----------------------------------------------------------------------------
; The settings (SPEC.md 79.4), and they are in EVERY BUILD.
;
; kern_small has no animation - SAVER.DRV is inside %ifdef KERN_BIG here and
; drv_load_at, its only loader, is inside one in driver.inc - but it reads and
; writes these three bytes all the same, exactly as it carries [wm_animon] for
; a zoom it does not have (SPEC.md 76.11). A floppy is carried between
; machines and between kernels, and a setting silently dropped by the build
; that cannot use it comes back as a DIFFERENT setting on the build that can.
;
; [ss_mins] is the one that does work on both: zero disables the whole feature,
; animation and blanker alike, and everything else is the idle period.
; -----------------------------------------------------------------------------
ss_pub:
ss_modes    db SS_M_ALL         ; which modes the picker may choose from
ss_secs     db 30               ; seconds one mode runs before another is
                                ; picked - the whole point of a screen saver
                                ; being that nothing sits still
ss_mins     db SS_MINS          ; minutes of idle before it starts; 0 = never
%ifdef KERN_BIG
ss_post     db 0                ; the settings window changed something and
                                ; the UI task owes it work (SPEC.md 79.7)
%endif

ss_idle     dw BLK_IDLE         ; [ss_mins] in TICKS, which is the unit
                                ; blk_pass compares in. Derived, by
                                ; ss_mins2idle, so the multiply is paid when a
                                ; setting changes and not on every UI pass
%ifdef KERN_BIG
; ...AND IT IS PART OF THE PUBLISHED BLOCK, which is a fact about the ORDER of
; the declarations above and nothing else - so it is asserted rather than
; trusted. A byte inserted between [ss_post] and here would leave SAVER.DRV
; writing its Test into whatever landed at +4, which on a settings block is
; the difference between "start now" and a setting nobody typed.
SS_IDLE_AT  equ ss_idle - ss_pub
 %if SS_IDLE_AT != SS_P_IDLE
 %error "blank.inc: ss_idle is not at SS_P_IDLE - see SPEC.md 79.7"
 %endif
%endif

%ifdef KERN_BIG
blk_sv      db 0                ; a SAVER SESSION is running: the overlay owns
                                ; the glass, the video signal is NOT gated,
                                ; and the desktop is owed a repaint

; --- SAVER.DRV's row (SPEC.md 79.2) -----------------------------------------
; SIXTEEN BYTES SHAPED LIKE A drv_tab ROW AND NOT IN IT, which is xm_row's
; trick exactly (SPEC.md 41.12): drv_load_at takes any such record, so the
; mount, drv_find, the one MEM_K_DRV claim, the read, drv_check and the shared
; failure tail are the loader that already exists rather than a second copy of
; it - while the settings bitmap, the Drivers page and DRV_MAX never hear
; about us.
;
; .text and not .bss, because -f bin zeroes nothing. DRVR_CLASS is DRVC_OVL,
; which drv_check compares against the image's own header byte and
; drv_cls_idx refuses by range, so this can never take a publication slot even
; by mistake.
;
; DRVR_TITLE POINTS AT THE FILE NAME, and that is not laziness: the field may
; not be 0 in a record the shared loader walks, and there is no row, no
; caption and no Drivers page here, so nothing ever draws it. Six bytes of a
; rung with 136 in it (docs/KERNEL-MEMORY.md) buys a string nobody reads.
ss_fname:   db 'SAVER.DRV', 0

ss_row:
    dw PKG_DISP, 0              ; DRVR_DISP, DRVR_SEG: together the far
                                ; pointer drv_call reaches the image through
    dw ss_fname, ss_fname       ; DRVR_FILE, DRVR_TITLE
    dw 0, 0                     ; DRVR_KB, DRVR_ENT
    db DRVC_OVL                 ; DRVR_CLASS - not a class the kernel knows
    db 0                        ; DRVR_WANT - no settings file can reach it
    db DRVE_OK                  ; DRVR_ERR
    db 0                        ; DRVR_PAD
%endif

; -----------------------------------------------------------------------------
; blk_wake - user input has arrived (SPEC.md 64.2)
;
; in:       nothing; NO GFX LOCK HELD
; out:      CF = 1: the screen was not showing the desktop and THIS input is
;                   what brought it back, so the caller owes it to the user to
;                   throw that input away
;           CF = 0: ordinary input, deliver it
; clobbers: flags
;
; THE WAKE INPUT IS CONSUMED, and that is a safety rule rather than a
; convention borrowed from elsewhere. While the desktop is off the glass the
; user cannot see what is under the pointer or which window has the keyboard,
; so acting on the input that arrives out of that state is acting blind: a
; click lands on whatever happens to be beneath it, a keystroke goes to
; whichever window was frontmost five minutes ago. Restoring the display IS
; the whole of what a wake event means. Both halves of it are the caller's to
; spend, because they arrive by different routes - ui.inc drops the keystroke
; it has just fetched, and blk_pass drains the event ring.
;
; It is also what makes the feature safe to have no interaction with anything
; else in the kernel: an unblank can only be reached from ui_task, so no
; drawing path, no mode set and no fsx bracket can ever run against a screen
; that is dark without the user having lit it first.
; -----------------------------------------------------------------------------
blk_wake:
    push ax
    mov ax, [ticks]
    mov [blk_t0], ax            ; the idle period restarts from here, lit or
    cmp byte [blk_on], 0        ; dark - this is the only place it is set
    pop ax                      ; outside boot (pop does not touch flags)
    jne .dark
    clc                         ; already lit: an ordinary input
    ret
.dark:
    push ax                     ; ...and it can only have been a wake. AX is
    xor al, al                  ; banked because this routine's contract is
    call blk_set                ; flags and nothing else: ui.inc calls it
    pop ax                      ; mid-keystroke, between int 16h's peek and
    stc                         ; its fetch, and fsx_restore from inside its
    ret                         ; own saved frame

; -----------------------------------------------------------------------------
; blk_pass - the UI task's per-pass service (SPEC.md 64.1, 79.5)
;
; in:       nothing; NO GFX LOCK HELD
; out:      nothing
; clobbers: flags
;
; THREE compares in the common case on kern_big and two on kern_small, which
; is every pass of a machine somebody is using: has the settings window posted
; anything, is there mouse activity to spend, and has the idle period run out.
; The third one used to be against a constant and is against [ss_idle] now,
; which costs nothing and is the whole of what SPEC.md 79.4 charges the idle
; path.
;
; IT SITS AHEAD OF evq_pop AND THAT ORDERING IS BINDING - it is the whole of
; the mouse half of SPEC.md 64.2. The press that woke the screen is still in
; the event ring when this runs, so draining it HERE is what stops it being
; dispatched a few instructions later as a click on a window the user could not
; see. Moved below evq_pop this still saves and still wakes, and the first
; click out of a dark screen goes wherever the pointer was left.
;
; The keyboard does not come through here: ui.inc has already fetched the
; keystroke by the time it knows there was one, so it calls blk_wake itself and
; drops the key on CF = 1.
;
; A RUNNING SESSION IS ALSO DRIVEN FROM HERE, one frame a pass, and the frame
; is the OVERLAY'S to refuse: it owns its own clock (SPEC.md 79.5), so this is
; a far call that usually returns having compared two words. Putting the pace
; there rather than here is what lets a mode run at its own rate without the
; kernel carrying a deadline for a feature that is not in it.
; -----------------------------------------------------------------------------
blk_pass:
%ifdef KERN_BIG
    cmp byte [ss_post], 0       ; the settings window changed something
    jne .post                   ; (SPEC.md 79.7)
%endif
    cmp byte [blk_act], 0       ; the mouse ISR saw a packet on a settled port
    jne .act
    cmp byte [blk_on], 0
%ifdef KERN_BIG
    jne .up                     ; already dark: there is no clock running, but
%else                           ; a session may owe a frame
    jne .out
%endif
    push ax
    cmp word [ss_idle], 0
    je .pop                     ; zero minutes: OFF, and that is a setting and
                                ; not a degenerate period (SPEC.md 79.4)
    mov ax, [ticks]
    sub ax, [blk_t0]            ; modular, and it cannot wrap on us: the test
    cmp ax, [ss_idle]           ; fires the moment it passes the period, and a
    jb .pop                     ; word holds an hour
    mov al, 1
    call blk_set
.pop:
    pop ax
.out:
    ret

.act:
    mov byte [blk_act], 0       ; consumed BEFORE it is acted on: a packet
    call blk_wake               ; landing inside this window simply sets it
    jnc .out                    ; again, and the worst a lost edge can cost is
                                ; one more pass. The press that woke the screen
                                ; is not a click (SPEC.md 64.2) - so the ring
                                ; is DRAINED, record by record, because a
                                ; package's wake is OWED, not dropped
                                ; (SPEC.md 74.1.1): the `call evq_init` that
                                ; stood here wiped EVT_WAKE records without
                                ; wm_wake_eaten, wm_wkq stayed at WKQ_RING
                                ; with no record behind it, and the window -
                                ; ftpd's, mid-transfer, first among them -
                                ; was deaf for the rest of its life
    push di
    mov di, fsx_evtmp           ; fsx_restore's drain scratch, borrowed: both
.dl:                            ; drains run on the UI task, so they can
    call evq_pop                ; never be live at once
    jc .dend
    call wm_wake_eaten
    jmp short .dl
.dend:
    pop di
    ret

%ifdef KERN_BIG
.up:
    call COLD_SEG:ssf_frame     ; a session gets one frame, if the overlay
    ret                         ; wants one; a GATED SIGNAL has nothing to do
                                ; and ssf_frame's own first compare is what
                                ; says so. That test belongs on the far side
                                ; and not here for the reason every other
                                ; decision in this file was taken on: it is
                                ; seven bytes of a .text rung against seven of
                                ; a .cold one, and what it costs the blanked
                                ; case is one far call per UI pass on a
                                ; machine nobody is looking at

.post:
    mov byte [ss_post], 0       ; consumed BEFORE it is spent, blk_act's rule:
    call COLD_SEG:ssf_post      ; a second post landing inside this window
    ret                         ; costs one more pass and nothing else
%endif

; -----------------------------------------------------------------------------
; blk_set - the desktop off the glass, or back onto it (SPEC.md 64.3, 79.1)
;
; in:       AL = 1 to save, 0 to restore
; out:      [blk_on] follows
; clobbers: flags
;
; THE ANIMATION IS TRIED FIRST AND THE BLANKER IS WHAT HAPPENS WHEN IT CANNOT
; RUN (SPEC.md 79.1). Every refusal ss_start_x can answer - no mode ticked, no
; system disk, no heap, a missing or malformed SAVER.DRV - arrives here as
; CF = 1 and falls through to the walk below, which is what this file did
; before there was an overlay. Nothing is said to the user, because nothing
; here is the user's to fix and a toast on a machine nobody is sitting at is a
; message nobody reads (SPEC.md 79.8).
;
; THE ARROW COMES OFF THE GLASS FOR A SESSION AND NOT FOR A BLANK, and that
; asymmetry is the whole difference between the two answers. A gated signal
; leaves the framebuffer alone, so the cursor's save-under stays true and the
; ISR may keep moving it; a saver DRAWS, so the first frame over the arrow
; would be banked as background and put back on the desktop as a rectangle of
; starfield the moment the hand moved. cursor_hide/cursor_show are refcounted
; (SPEC.md 7.1), so the gfx_lock/gfx_unlock pairs inside every frame nest
; underneath this one and neither draws.
;
; EVERY CARD, not just [vid_kind]'s. On a two-monitor machine (SPEC.md 39.11) -
; which is the machine this project is calibrated against, a 5150 with a
; Hercules and a CGA in it - the desktop spans both and both tubes are lit, so
; blanking the primary alone would protect one monitor and leave the other
; showing a frozen desktop all night. The walk is vid_fsx_enter's, which darks
; every card but the bracket's for the same reason from the other end.
;
; No lock is taken and none is needed: this writes I/O ports and never touches
; a framebuffer, and vid_seq1_blank owns the one access that has to be atomic.
; -----------------------------------------------------------------------------
blk_set:
    mov [blk_on], al
%ifdef KERN_BIG
    call COLD_SEG:ssf_set       ; the ANIMATION first (SPEC.md 79.1). CF = 0
    jnc .done                   ; means the overlay has the glass, so the
%endif                          ; video signal must stay ON
    push ax
    push cx
    push di
%ifdef KERN_BIG
    mov cl, [vid_ndisp]
    xor ch, ch
.d:
    mov al, ch
    call vid_ctx_ptr            ; DI = that display's record; AX preserved
    mov al, [di+VID_CTX_KIND]
    call blk_one
    inc ch
    cmp ch, cl                  ; a do-while: [vid_ndisp] is never 0, and a
    jb .d                       ; kernel that made it 0 would still do one
%else
    mov al, [vid_kind]          ; kern_small is single-display by construction
    call blk_one                ; - vidsel.inc's whole multi-display half is
%endif                          ; inside one %ifdef KERN_BIG
    pop di
    pop cx
    pop ax
.done:
    ret

; in: AL = VID_* kind. [blk_on] has already been written, and says which way.
; Two near jmps and not a conditional one: an 8086 conditional branch is short
; only, and both bodies are a module away in vidsel.inc.
blk_one:
    cmp byte [blk_on], 0
    jne .dark
    jmp vid_unblank_kind
.dark:
    jmp vid_blank_kind

section .cold

; -----------------------------------------------------------------------------
; ss_mins2idle - [ss_mins] into [ss_idle] (SPEC.md 79.4)
; in:  nothing
; out: nothing (all registers preserved)
;
; IT EXISTS SO THAT blk_pass DOES NOT MULTIPLY. That routine runs on every UI
; pass - thousands of times a minute on a machine at idle - and this runs when
; a setting arrives, which is twice a session.
;
; THE MINUTES ARE CLAMPED, and the clamp is arithmetic rather than taste:
; 255 x 1092 is 278,460 and [ss_idle] is a word, so an unclamped 255 would
; store 16,316 ticks and the machine would save itself after FIFTEEN minutes
; when the user asked for four hours. PERFORMANCE.md rule 3's 16-bit lap,
; which fails structurally rather than proportionally, in the one place in
; this file that can multiply at all. SS_MAXMIN is what a word of ticks holds.
;
; Both builds: the period is a setting on kern_small too (see ss_pub).
; -----------------------------------------------------------------------------
SS_MAXMIN   equ 59              ; 59 x 1092 = 64,428, inside a word

ss_mins2idle:
    push ax
    push dx
    mov al, [ss_mins]
    cmp al, SS_MAXMIN
    jbe .ok
    mov al, SS_MAXMIN
.ok:
    mov ah, 0
    mov dx, SS_TPM
    mul dx                      ; DX:AX, and DX is 0 by the clamp above
    mov [ss_idle], ax
    pop dx
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; ss_call - one verb into the overlay (module internal)
; in:  AL = SSV_*, every other register that verb's own argument
; out: whatever it returns, CF included
; clobbers: BX and BP are banked; DS and ES are drv_call's to restore
;
; BX IS NOT AN ARGUMENT TO ANY VERB, which is why it can be spent on the row
; here: SSV_CFG takes the settings block in SI for exactly that reason, and
; putting it in BX would have made the dispatcher the pointer's owner. That is
; drv_svc_call's recorded bug (SPEC.md 51.2), avoided by choosing the register
; rather than by remembering.
; -----------------------------------------------------------------------------
ss_call:
    push bx
    push bp
    mov bx, ss_row
    mov bp, [bx+DRVR_ENT]
    call drv_call               ; DS = the image, ES = KERNEL_SEG, and a row
    pop bp                      ; whose DRVR_SEG is 0 is a refusal rather than
    pop bx                      ; a far call. `pop` touches no flag
    ret

; -----------------------------------------------------------------------------
; ss_open - load the overlay and ask it for something (module internal)
; in:  AL = SSV_START or SSV_CFG
; out: CF = 0 it said yes; CF = 1 it could not be read, or it said no
; clobbers: AX, flags
;
; THE TWO WAYS IN ARE THE SAME FOUR STEPS - make sure the image is there, hand
; it the settings block, and give the image straight back if it takes the file
; and then refuses. They were written as two routines and were identical but
; for the verb, which is two things to keep in step for no reason at all.
;
; SI AND NOT BX carries the settings block, because ss_call spends BX on the
; row (SPEC.md 79.3).
; -----------------------------------------------------------------------------
ss_open:
    push si
    push bx
    mov bx, ss_row              ; drv_load_at answers CF = 0 immediately when
    call drv_load_at            ; DRVR_SEG is already live, so the second and
    pop bx                      ; every later call of a session is six compares
    jc .no
    mov si, ss_pub              ; the settings, in KERNEL_SEG - the overlay
    call ss_call                ; reads them through ES (SPEC.md 79.7)
    jc .free
    clc
    jmp short .out
.free:                          ; it took the file and then said no: the image
    call ss_reap_x              ; goes back rather than sitting in the heap
.no:                            ; until somebody asks again
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; ss_start_x - the idle period ran out (SPEC.md 79.1)
; in:  nothing; NO GFX LOCK HELD - this takes it
; out: CF = 0 the overlay has the screen; CF = 1 blank the signal instead
; clobbers: AX, flags
;
; NO MODE TICKED IS A REFUSAL AND NOT AN ERROR. Un-ticking all four is how the
; settings window says "just blank it" (SPEC.md 79.4), so it is tested first
; and costs a machine that wants the blanker exactly one compare and no disk
; access at all.
;
; **THE LOCK IS HELD ACROSS THE READ AND NOT ONLY ACROSS THE FIRST PAINT**, and
; that is not tidiness - it is SPEC.md 12.8.3. A file read arms the progress
; widget on the menu bar, and gfx_unlock is the ONE thing that takes it down
; again ("nothing anywhere has to call fpg_finish and nothing may"). Read
; outside a hold, the widget is armed and never disarmed: measured on the
; 5150-class machine, a 100x19 WHITE BLOCK sat in the menu bar for the whole
; session, which the saver then drew over and around. A static bright patch
; is exactly what this feature exists to prevent, so it is worth naming the
; cause: not the saver's drawing, but a disk read taken without the lock.
; -----------------------------------------------------------------------------
ss_start_x:
    cmp byte [ss_modes], 0
    stc                         ; the refusal, set BEFORE the branch so that
    je .out                     ; the two ways out share one `ret`. `stc`
                                ; writes no ZF, so the compare still reads
    call KERNEL_SEG:cw_gfx_lock
    mov al, SSV_START
    call ss_open
    pushf                       ; the verdict, banked across the unlock, whose
    call KERNEL_SEG:cw_gfx_unlock   ; contract is "clobbers: flags"
    popf
.out:
    ret

; -----------------------------------------------------------------------------
; ss_cfg_x - open the settings window (SPEC.md 79.7)
; in:  the gfx lock is HELD - this is a Control Panel click (SPEC.md 31.1), so
;      the overlay may draw and must not take the lock
; out: CF = 1 the overlay could not be read, and the CALLER owes the user a
;      reason (SPEC.md 47 rule 3); CF = 0 the window is up
; clobbers: AX, flags
;
; The one verb the user is standing in front of, and therefore the one whose
; refusal is said out loud. Everything else here is silent because nobody is
; watching; this is a button press that did nothing, which is the shape
; SPEC.md 2.8.4 refuses.
; -----------------------------------------------------------------------------
ss_cfg_x:
    mov al, SSV_CFG
    jmp short ss_open

; -----------------------------------------------------------------------------
; ss_stop_x - the user is back (SPEC.md 79.6)
; in:  nothing; NO GFX LOCK HELD
; out: nothing; clobbers AX and the flags
;
; THE DESKTOP IS OWED A FULL REPAINT AND THERE IS NO CHEAPER ANSWER. A session
; has drawn over the menu bar, the drive column, the dock and every window, so
; there is no damage rect to be clever with - wm_paint_all IS the damage
; (PERFORMANCE.md rule 1's one legitimate exemption, because the whole screen
; really did change). It is ~0.4s on the 5150 and it is paid once per wake, on
; the far side of a period measured in minutes.
;
; **AND wm_paint_all ALONE IS NOT ENOUGH**, which is what cw_blk_relit is for
; and is worth stating because it looks like it should be. That routine forces
; NEITHER the menu bar nor the dock: the dither it lays starts at MBAR_H and
; stops above the strip, so nothing in it has ever been able to touch either
; (SPEC.md 12.9, 30.3.2), and both early-out on "nothing has changed". A saver
; has drawn over both. Measured before the shim existed: the desktop came back
; with a BLACK menu bar and a BLACK dock and everything between them correct -
; which reads as a repaint that half worked rather than as two force flags
; nobody set. fsx_restore is the other caller that has to say the same thing.
;
; SSV_STOP FIRST, THEN THE REPAINT. The overlay may hold a clip region or a
; pen of its own, and the repaint has to start from a screen nothing else
; believes it owns.
; -----------------------------------------------------------------------------
ss_stop_x:
    mov al, SSV_STOP
    call ss_call
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_blk_relit
    call KERNEL_SEG:cw_gfx_unlock
    jmp short ss_reap_x         ; ...and the image goes back, if nothing else
                                ; has a use for it (SPEC.md 79.8)

; -----------------------------------------------------------------------------
; ss_reap_x - give the image back if nothing is using it (SPEC.md 79.8)
; in:  nothing; NO GFX LOCK HELD
; out: nothing (all registers preserved)
;
; THE OVERLAY IS ASKED AND NEVER VOLUNTEERS, which is hd_tool_reap's rule one
; feature over (SPEC.md 52.11.7): a call in which the overlay said "I have
; finished" would be answered by freeing the image it has to return into.
; SSV_BUSY is a question, asked from the kernel's own stack, and CF = 1 means
; the settings window is still on screen and its W_PAINT still names this
; segment.
;
; Left loaded in that case it goes at the next reap, and there are TWO: the
; end of a session (ss_stop_x), and a post from the settings window itself -
; which it raises when it applies AND when it closes, so between them they
; cover every way the image stops being wanted. A user who leaves that window
; up holds ~7KB of heap until they close it, which is the same bargain the
; disk tool takes (SPEC.md 52.11.7).
;
; **IT IS LOCK-AGNOSTIC**, which SSV_BUSY's shape is what buys: that verb reads
; one word and answers, and the window it is answering about was destroyed by
; its own W_ONCLOSE with the lock the kernel already held (SPEC.md 79.7). So
; this may be called from inside a hold - ss_start_x's - as well as from
; outside one.
; -----------------------------------------------------------------------------
ss_reap_x:
    push ax
    push bx
    cmp word [ss_row+DRVR_SEG], 0
    je .out                     ; nothing loaded: nothing to ask and nothing
    cmp byte [blk_sv], 0        ; to free
    jne .out                    ; a session is running and IS the user
    mov al, SSV_BUSY
    call ss_call
    jc .out
    mov bx, ss_row
    call drv_release
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ss_post_x - spend what the settings window asked for (SPEC.md 79.7)
; in:  nothing
; out: nothing (all registers preserved)
;
; The window writes three bytes and a flag and returns; everything that has to
; happen BECAUSE they changed happens here, on the UI task, with nothing held.
; Three things do:
;
;   1. the period is re-derived, or a machine told "twenty minutes" would go
;      on saving itself after five;
;   2. SYSTEM.CFG is marked OWED and not written - SPEC.md 31.8's rule, and
;      the settings window is opened from the Control Panel, so the panel's
;      own close is what spends it, exactly as it spends every other page's;
;   3. the image is offered back, which is what closing the window reaches.
; -----------------------------------------------------------------------------
ss_post_x:
    push ax
    mov byte [ss_post], 0       ; CONSUMED BEFORE IT IS SPENT (blk_act's rule):
                                ; a post that lands inside this routine simply
                                ; sets it again, and the worst a lost edge can
                                ; cost is one more UI pass
    call ss_mins2idle
    mov byte [cp_wdirty], 1
    call ss_reap_x
    pop ax
    ret

; -----------------------------------------------------------------------------
; ss_set_x - blk_set's whole animation arm (SPEC.md 79.1, 79.6)
; in:  AL = 1 a session begins, 0 it ends
; out: CF = 0 the overlay has the glass and the video signal must stay ON;
;      CF = 1 blk_set's own walk should gate it
; clobbers: AX, flags
;
; IT IS ONE FAR CALL AND NOT TWO BECAUSE THE .text SIDE IS THE EXPENSIVE ONE.
; Everything below was written in blk_set to begin with - the two arms, the
; cursor pair and the [blk_sv] stores - and it cost the image rung 34 bytes
; for code that runs twice a session. Here it costs the cold rung, which had
; room, and blk_set is a far call and a branch (docs/KERNEL-MEMORY.md).
;
; THE ARROW COMES OFF THE GLASS FOR A SESSION AND NOT FOR A BLANK, and that
; asymmetry is the whole difference between the two answers. A gated signal
; leaves the framebuffer alone, so the cursor's save-under stays true and the
; ISR may keep moving it; a saver DRAWS, so the first frame over the arrow
; would be banked as background and put back on the desktop as a rectangle of
; starfield the moment the hand moved. cursor_hide/cursor_show are refcounted
; (SPEC.md 7.1), so the gfx_lock/gfx_unlock pairs inside every frame nest
; underneath this one and neither of them draws.
;
; [blk_sv] IS CLEARED BEFORE ss_stop_x AND SET AFTER ss_start_x, both times so
; that the flag is false for the whole of the repaint: wm_paint_all is a UI
; pass's worth of drawing and nothing inside it may still believe the overlay
; owns the screen.
; -----------------------------------------------------------------------------
ss_set_x:
    or al, al
    jz .lit
    call ss_start_x
    jc .no                      ; nothing loaded, so nothing to hide
    call KERNEL_SEG:cw_cursor_hide
    mov al, 1                   ; ...and a byte store through AL is three
    mov [blk_sv], al            ; bytes where an immediate one is six
    clc
    ret
.lit:
    cmp byte [blk_sv], 0
    je .blank                   ; the signal was gated: blk_set un-gates it
    mov [blk_sv], al            ; AL is still the 0 that brought us here
    call ss_stop_x
    call KERNEL_SEG:cw_cursor_show
    clc
    ret
.blank:
    call ss_mins2idle           ; the BLANKER's wake still re-derives the
                                ; period: Test with zero modes ticked parked
                                ; 1 in [ss_idle], ss_start_x refused so the
                                ; session never began, and nothing ever put
                                ; the derived period back - the screen
                                ; re-blanked every tick until Ok or Cancel
                                ; landed on a machine going dark under the
                                ; pointer. Idempotent on every ordinary
                                ; blanker wake, and it ends a blanker-Test
                                ; exactly the way sv_do_stop ends a saver's
.no:
    stc
    ret

; --- the far entries (SPEC.md 2.6) -------------------------------------------
; `retf` pops CS:IP and touches no flag, so the CF each body answers in comes
; back through it unchanged - mod.inc's modf_* pair for the same reason.
;
; ssf_frame HAS NO BODY OF ITS OWN. SSV_FRAME/STOP/BUSY preserve every
; register (SPEC.md 79.3), which is the overlay's obligation and costs the
; kernel nothing, so the verb IS the routine.
ssf_set:        push ax
                call ss_set_x
                pop ax          ; `pop` touches no flag: the CF survives it
                retf
ssf_frame:      cmp byte [blk_sv], 0    ; blk_pass's gate, on this side of the
                je .none                ; boundary (see .up there)
                push ax
                mov al, SSV_FRAME
                call ss_call
                pop ax
.none:
                retf
ssf_post:       call ss_post_x
                retf
ssf_cfg:        call ss_cfg_x
                retf
%endif

section .text
