; =============================================================================
; os8088 - sched.inc
;
; Round-robin scheduler off the PIT, in either of two modes (SPEC.md 8/8.2):
; pre-emptive (the boot default - every tick on which sch_lock is clear
; switches) or cooperative (the tick declines to switch, so the running task
; keeps the CPU until it reaches the switch path itself through task_yield,
; task_sleep or task_exit - with a watchdog that forces a switch anyway once
; the task has held the CPU for SCH_WD_TICKS ticks, so a task that never
; yields cannot hard-hang the machine). The mode is the single byte sch_coop,
; flipped at run time by sched_mode_set; both modes share the task table, the
; per-task stacks and the saved frame below, so the flip adds and removes no
; state and needs no rendezvous.
;
; Up to MAX_TASKS tasks; task 0 is the boot thread (the UI task) and runs on
; the task-0 stack (SS:STK0_TOP) — it owns no slice of sch_stacks. Slots
; 1..MAX_TASKS-1 are dynamic (task_spawn / task_exit), each with an
; SCH_STACK-byte stack in .lowbss.
;
; All tasks run CS = DS = KERNEL_SEG and SS = LOW_SEG (SPEC.md 1/2.1): the
; stacks live in low memory, outside the kernel's 64KB window, and a context
; switch is still nothing more than swapping SP because every task shares the
; one SS. SS is therefore NOT saved in the frame below and never changes
; after kmain sets it. The consequence for the rest of the kernel is that
; BP-relative operands address SS, not DS: code that uses BP as a pointer
; into a kernel structure must carry an explicit `ds:` override.
;
; Saved-frame layout (top of a suspended task's stack, low address first):
;       [SP+ 0] DS      pushed last by the save path
;       [SP+ 2] ES
;       [SP+ 4] BP
;       [SP+ 6] DI
;       [SP+ 8] SI
;       [SP+10] BX
;       [SP+12] DX
;       [SP+14] CX
;       [SP+16] AX      pushed first by the save path
;       [SP+18] IP      \
;       [SP+20] CS       > hardware int 08h frame (task_yield fakes it)
;       [SP+22] FLAGS   /
; The timer ISR, task_yield and task_spawn all build/consume exactly this
; frame, so every suspended task resumes through the one sch_resume path.
; =============================================================================

; --- task table (SPEC.md 8) --------------------------------------------------
%ifdef KERN_SMALL
MAX_TASKS   equ 7               ; SIX worker slices and the UI task
                                ; (docs/KERN-SMALL-CUT-PLAN.md D1). The SDK's
                                ; copy STAYS AT 14, on SPEC.md 51.0's rule for
                                ; MEM_P_FATW_N: apps/os88api.inc publishes this
                                ; to packages and taskmgr sizes buffers from it
                                ; (SS_TSTATE is MAX_TASKS bytes), so a package
                                ; built at 14 reading a 7-slot snapshot
                                ; OVER-allocates, which is safe, where the
                                ; reverse would overflow. One .o88 runs on both
                                ; kernels (SPEC.md 24), so the SDK carries the
                                ; larger of the two.
%else
MAX_TASKS   equ 14              ; mirrored in apps/os88api.inc, which sizes
                                ; every package's snapshot buffer from it
                                ; (SPEC.md 20.9) - change them together and
                                ; rebuild every .o88
%endif
T_STATE     equ 0               ; byte: 0 free, 1 ready, 2 sleeping
T_SP        equ 2               ; word: saved SP (frame above)
T_WAKE      equ 4               ; word: [ticks] value to wake at
T_INST      equ 6               ; byte: owning instance index (SPEC.md 29),
                                ; 0xFF = none (the UI task)
T_FLAGS     equ 7               ; byte (was padding): bit 0 = TF_SERVICE -
                                ; set only by the driver-worker cell, read
                                ; only by the fsx freeze (SPEC.md 53.2)
T_SIZE      equ 8               ; record stride

TF_SERVICE  equ 1               ; survives the fullscreen-exclusive freeze:
                                ; a loaded driver's refill/drain worker is
                                ; plumbing, not app work, and parking it
                                ; would starve a Sound Blaster stream mid-
                                ; game (SPEC.md 53.2)

SCH_CHSTK   equ 128             ; the ROM int 08h chain's private stack
                                ; (SPEC.md 8.5). It holds the chain itself -
                                ; 56 bytes on the worst BIOS measured, SeaBIOS,
                                ; including the pushf and the far call - plus
                                ; whatever lands in the window the ROM stis
                                ; open: an int 09h, a mouse IRQ's six-byte gate
                                ; frame before it swaps away (SPEC.md 9.10),
                                ; the floppy's completion. ~78 worst case, so
                                ; 1.6x. A second IRQ0 does NOT nest here and
                                ; SPEC.md 8.5.1 says why
%if SCH_CHSTK % 2
  %error "SCH_CHSTK must be EVEN: sch_chstack precedes sch_stacks in .lowbss and an odd size would move it off the word boundary its canary and `rep stosw` fill both need"
%endif

SCH_STACK   equ 384             ; per-task stack bytes. It was 1,536 when the
                                ; stacks were the only thing in .lowbss, then
                                ; 512. A 0xCC fill probe under the heaviest
                                ; load the machine can carry - Timer, two
                                ; Bounces, the Fractal's drawing worker and
                                ; Arkanoid's game loop all live at once, with
                                ; the tick and mouse ISRs landing on top of
                                ; them - left its deepest mark 142 bytes down,
                                ; and Tracker's streaming worker feeding a
                                ; Sound Blaster through its own IRQ reached
                                ; the same 142. 256 is 1.8x that, which is
                                ; thinner than this project usually runs, so
                                ; SCH_MAGIC below makes an overrun a CAUGHT
                                ; failure instead of a silent one.
                                ;
                                ; **IT IS A CONSTANT AND A REBUILD.** It used
                                ; to have to be 256 exactly, because
                                ; sch_switch turned the slot index into the
                                ; slice's base with a byte swap; that path is
                                ; still there for 256 and there is a general
                                ; one beside it for any multiple of 128, so
                                ; changing this and MAX_TASKS is the whole of
                                ; the change (docs/KERNEL-MEMORY.md, "Task
                                ; stacks"). apps/taskmgr's bss map used to be
                                ; hand-chained literals and was the other half
                                ; of why nobody could; it derives now
%if (SCH_STACK % 128) != 0
  %error "SCH_STACK must be a multiple of 128 - it is the largest class and every class is one"
%endif

; --- what the KERNEL's own spawners ask for (SPEC.md 8.7) --------------------
; A package says so in its header; these three have no header to say it in, so
; they say it here, each against a measurement rather than a guess.
SCH_IDLE_STK    equ 128         ; the idle task. Its own footprint is at most 4
                                ; bytes - `pushf` then `cli`, then task_yield's
                                ; own `pushf`/`cli` - so what it needs IS the
                                ; interrupt floor, measured at 32 on QEMU and
                                ; 40-64 on real iron (docs/STACK-SLOTS-PLAN.md
                                ; 7.1). 128 is 2x the worst of those
SCH_BUILTIN_STK equ 128         ; a built-in kind's task. Timer and Bounce are
                                ; the two that take one, and six Bounces at
                                ; once measured +10..24 over the floor
SCH_DRV_STK     equ 192         ; a driver's worker. The only one in the tree
                                ; is the sound driver's stream task, whose
                                ; deepest static chain is 28 bytes
                                ; (tools/stkdepth.py on sbl_refill_task) - so
                                ; 64 + 28 = 92 and this is 2.1x. It is one
                                ; number for all drivers because one driver
                                ; calls OSAPI_DRV_TASK in the whole tree (the
                                ; sound driver's refill and drain spawners);
                                ; a driver that needs more is a line here with
                                ; a reason beside it, not a new mechanism

SCH_MAGIC   equ 0x5A57          ; the word at the BOTTOM of every slice. A
                                ; stack that reaches it has overrun, and the
                                ; next push corrupts the slice below - which
                                ; is another task's, so the symptom would be
                                ; arbitrary and nowhere near the cause
SCH_FRAME   equ 24              ; saved-frame size: 9 regs + IP/CS/FLAGS

; -----------------------------------------------------------------------------
; SCH_PARTITION - THE SLICE LAYOUT, WRITTEN ONCE (SPEC.md 8.7)
;
; One SCH_SLOT line per background slot, in slot order, giving that slot's
; stack in BYTES. Everything else about the layout is DERIVED from this list:
; sch_stkbase's addresses, sch_stksize's sizes, the total sch_stacks reserves,
; and the count that must agree with MAX_TASKS. Edit here and nowhere else.
;
; It is a FIXED PARTITION and not an allocator (docs/STACK-SLOTS-PLAN.md 6.2):
; the slices exist from boot, a spawn takes the smallest free one that is big
; enough, and running out is OSAPI_TASK_SPAWN's existing CF=1 - which SPEC.md
; 20.6 already requires every package to degrade on. No compaction, no
; fragmentation, no new failure mode.
;
; The list is uniform TODAY. What made it a list is that the sizes are about to
; stop being uniform, and every reader had to stop assuming otherwise first.
; -----------------------------------------------------------------------------
%ifdef KERN_SMALL
; --- kern_small's SIX (docs/KERN-SMALL-CUT-PLAN.md D1) -----------------------
; 1,280 bytes against kern_big's 2,816. The COUNT is the owner's; what is
; chosen here is WHICH SIX, off the same survey (docs/STACK-SLOTS-PLAN.md 12.1)
; read against what SPEC.md 24.5 actually puts on the small disks:
;
;   the 384 class  ftpd, the Browser and Frotz - NONE of which ships on
;                  kern_small. ONE IS KEPT ANYWAY, and not for them: the C SDK
;                  declares CC_STACK = SCH_STACK (SPEC.md 20.6 rule 7),
;                  tests/unit/t_mirror.py guards the pair, and SCH_STACK is the
;                  LARGEST class by construction. Take the class away and
;                  either a C package's worker can never be spawned or the
;                  mirror breaks.
;   the 256 class  Missile Command ships; Modplug, Word and Telnet do not
;   the 192 class  the modal class, and the one with real competition here:
;                  Artful, the Task Manager, the Fractal, Cyclone, Notepad,
;                  Arkanoid and Tamegram are all on the small apps disk
;   the 128 class  the idle task holds one for the life of the machine, so the
;                  second is the only sleeper slot a program can have - and the
;                  sound driver's stream task, one of its four consumers, went
;                  with SPEC.md 51.0
;
; FIVE USABLE, the idle task owning the sixth. A 43KB heap holds about two
; packages, so the binding resource is not this table.
%macro SCH_PARTITION 0
    SCH_SLOT 128                ; the idle task's, for the life of the machine
    SCH_SLOT 128                ; ...and the one sleeper slot left over
    SCH_SLOT 192                ; the modal class (STACK-SLOTS-PLAN 12.1)
    SCH_SLOT 192
    SCH_SLOT 256                ; Missile Command
    SCH_SLOT SCH_STACK          ; ...and the top class, here for CC_STACK's
%endmacro                       ; sake rather than for a program's
%else
%macro SCH_PARTITION 0
    SCH_SLOT 128                ; the sleepers. ONE of these is the idle task's
    SCH_SLOT 128                ; for as long as the machine is up, so two are
    SCH_SLOT 128                ; really available: a Timer, a Bounce, the
                                ; sound driver's stream task. NOT a C package
                                ; whose worker "only sleeps": CWORD's and
                                ; RUNCPM's close the window from that worker,
                                ; and wm_destroy repaints what it uncovered
                                ; on the CALLER's stack - they ask for 256
    SCH_SLOT 192                ; THE MODAL CLASS - eleven of the twenty
    SCH_SLOT 192                ; workers in the tree land here
    SCH_SLOT 192                ; (docs/STACK-SLOTS-PLAN.md 12.1): WIREFRAME,
    SCH_SLOT 192                ; Artful, the Task Manager, the Fractal, WEAVE,
    SCH_SLOT 192                ; Cyclone, Notepad, Arkanoid, Tracker
    SCH_SLOT 192
    SCH_SLOT 256                ; Modplug, Missile Command, Word, Telnet,
    SCH_SLOT 256                ; Tamegram, Tank Attack, CWORD, RUNCPM
    SCH_SLOT SCH_STACK          ; ftpd, the Browser and Frotz - and TWO is the
    SCH_SLOT SCH_STACK          ; judgement in this list. The pair that plausibly
                                ; runs at once is ftpd and the Browser, on a
                                ; machine serving files while somebody reads;
                                ; a third costs 192 bytes and, because the low
                                ; window has 162 left, 512 of anybody's RAM.
                                ; It is one line here if the field wants it
%endmacro
%endif

; --- and the three things counted off it, before anything can use them -------
%assign SCH_STK_TOTAL 0
%assign SCH_STK_N     0
%assign SCH_STK_MAX   0
%macro SCH_SLOT 1
    %if (%1) < SCH_STK_MAX
      %error "SCH_PARTITION must ascend: first fit is SMALLEST fit (SPEC.md 8.7.1) only while the slots are listed small to large"
    %endif
    %assign SCH_STK_TOTAL SCH_STK_TOTAL + (%1)
    %assign SCH_STK_N     SCH_STK_N + 1
    %if (%1) > SCH_STK_MAX
        %assign SCH_STK_MAX (%1)
    %endif
    %if ((%1) % 2) != 0
        %error "every slice must be EVEN: sch_switch's canary is a word written at the slice base and task_spawn fills with `rep stosw`, so an odd size puts every slice above it on an odd address and the canary straddles"
    %endif
    %if (%1) < SCH_FRAME + 2
        %error "a slice smaller than SCH_FRAME + the canary word cannot hold a task's own parked frame, never mind an interrupt"
    %endif
%endmacro
SCH_PARTITION
%unmacro SCH_SLOT 1

%if SCH_STK_N != (MAX_TASKS - 1)
  %error "SCH_PARTITION must have exactly MAX_TASKS-1 SCH_SLOT lines: slot 0 is the UI task and owns no slice of sch_stacks"
%endif
%if SCH_STK_MAX != SCH_STACK
  %error "SCH_STACK must be the LARGEST class in SCH_PARTITION - it is what a package that declares nothing is given, and what every bound in the SDK is written against"
%endif
%if SCH_STK_TOTAL > 65535
  %error "the task slices no longer fit one segment"
%endif

SCH_WD_TICKS equ 18             ; cooperative mode: the watchdog forces a
                                ; switch once the running task has held the
                                ; CPU this many ticks without reaching
                                ; sch_switch (~1s at 18.2065 Hz, SPEC.md 8.2)

; -----------------------------------------------------------------------------
; sched_init - set up the task table and hook int 08h
;
; Task 0 (the caller) becomes current and ready; its T_SP is filled in by the
; first switch away from it. Saves the BIOS vector for chaining and unhook.
;
; in:       nothing
; out:      nothing
; clobbers: flags
;
; IT IS IN THE BOOT OVERLAY (SPEC.md 2.5, docs/LAST-DROP-BYTES.md row 3), which
; is 166 bytes of `.text` that stop existing when kmain gives the blob back.
; kmain:4170 is its ONE caller and there is no second entry: nothing
; re-initialises the scheduler, the reboot path is sched_unhook - which RESTORES
; the old vector and is a different routine - and sch_fast_on/sch_fast_off
; re-programme the PIT without coming near this body. Zero outbound calls, so
; the move needs no shim in either direction.
;
; The vector store below is what makes it safe to be here and is worth reading
; before anything is added: `mov word [es:0x22], KERNEL_SEG` names the segment
; as a CONSTANT. In `.ovl` CS is the BLOB's, and the blob is about to become
; somebody's heap claim, so `mov [es:0x22], cs` would install a vector into
; freed memory and the machine would die later at an address with no connection
; to this file. tools/os88ovlchk.py rule 2b refuses that spelling outright now.
; -----------------------------------------------------------------------------
section .ovlw
sched_init:
    push ax
    push bx
    push cx
    push es

    mov bx, sch_tasks           ; mark every slot free
    mov cx, MAX_TASKS
.clear:
    mov byte [bx+T_STATE], 0
    add bx, T_SIZE
    loop .clear

    mov byte [sch_cur], 0
    mov byte [sch_lock], 0
    mov word [ticks], 0
    mov byte [sch_fast], 0      ; no sub-tick until a bracket asks (53.8)
    mov byte [sch_coop], 0      ; pre-emptive until the Control Panel says
    mov byte [sch_hold], SCH_WD_TICKS
                                ; otherwise (SPEC.md 8.2). All three are
    mov byte [sch_idleslot], 0xFF ; NO idle task yet, and this is exactly why
                                ; it is 0xFF rather than 0 - nothing clears
                                ; .bss, and a zero here names slot 0, the UI
                                ; task, which sch_switch's scan would then
                                ; skip for ever
    mov word [sch_wd_hits], 0   ; zeroed HERE by hand, like the four bytes
                                ; above it: nothing clears .bss at boot, so
                                ; every one of these has to be stored rather
                                ; than assumed. (This comment used to explain
                                ; sch_coop, which is set three lines up - and
                                ; a store whose comment justifies a DIFFERENT
                                ; store is a store somebody deletes)
    push es                     ; TASK 0 HAS A CANARY NOW, and it is checked on
    push ax                     ; every switch like everybody else's - see
    mov ax, LOW_SEG             ; sch_stkbase, whose slot-0 entry is this word.
    mov es, ax                  ; Its stack grows DOWN onto the top of .lowbss
    mov word [es:STK0_BOT], SCH_MAGIC ; - the other tasks' slices, the glyph
    pop ax                      ; table, the disk buffers - so an overrun there
    pop es                      ; used to be SILENT, which is what the KFZ-only
                                ; version of this seed was written to complain
                                ; about and could only report on a build nobody
                                ; runs. Unconditional now: the seed is what
                                ; makes the table uniform, and the table is
                                ; what makes the check free
    mov byte [sch_tasks+T_STATE], 1     ; task 0 = this thread, ready
    mov byte [sch_tasks+T_INST], 0xFF   ; the UI task owns no instance
    mov byte [sch_tasks+T_FLAGS], 0
    mov byte [fsx_task], 0xFF   ; fullscreen exclusive off (SPEC.md 53.2);
    mov byte [fsx_worker], 0xFF ; .bss arrives zeroed but 0 is task 0, so
                                ; the OFF value must be stored explicitly

    ; PIT ch0 -> mode 2 (rate generator), divisor 0 = 65536: the IRQ rate
    ; stays 18.2065 Hz (BIOS timekeeping counts IRQs, unaffected), but the
    ; counter now decrements by 1 per input clock - the BIOS default mode 3
    ; steps by 2 and wraps twice per period, useless for elapsed reads
    ; (SPEC.md 8.1). Reprogram BEFORE hooking int 08h: the BIOS tick
    ; handler touches no PIT ports, so IF=1 is safe here.
    mov al, 0x34
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    mov bx, sch_cycles          ; zero the per-task cycle counters
    mov cx, MAX_TASKS * 2
.zcyc:
    mov word [bx], 0
    inc bx
    inc bx
    loop .zcyc

    mov al, 0x00                ; seed sch_pit_last with a coherent stamp:
    out 0x43, al                ; high word = [ticks] = 0, low word = the
    in al, 0x40                 ; current phase (65536 - latched count)
    mov cl, al
    in al, 0x40
    mov ch, al
    neg cx
    mov [sch_pit_last], cx
    mov word [sch_pit_last+2], 0

    xor ax, ax                  ; vector table segment
    mov es, ax
    pushf
    cli                         ; no half-written vector, ever
    mov ax, [es:0x20]           ; int 08h lives at 0000:0020
    mov [sch_old08], ax
    mov ax, [es:0x22]
    mov [sch_old08+2], ax
    mov word [es:0x20], sch_isr
    mov word [es:0x22], KERNEL_SEG
    popf

    pop es
    pop cx
    pop bx
    pop ax
    retf                        ; FAR, and it has to be: everything in the blob
                                ; is reached through `call far [spl_fp]`, so an
                                ; entry that kept its near `ret` would pop IP
                                ; and leave CS on the stack. SPEC.md 2.6.1's
                                ; rule, one section along - the body owns the
                                ; far return and there is no thunk in between
section .text

; -----------------------------------------------------------------------------
; sch_fast_on / sch_fast_off - the fsx sub-tick (SPEC.md 53.2.1)
;
; in: AL = sub-ticks per tick, 2..4 (anything else is refused)
;
; IRQ0 is made to arrive N times a system tick so the round-robin quantum -
; the thing that decides how long the drawing side waits when the audio worker
; becomes ready - drops from 55 ms to 18. [ticks] does NOT change rate: the
; ISR divides, and the Nth entry does everything a tick has always done, chain
; and all. So task_sleep, every timeout, the double-click window, snd_tick's
; duties and the BIOS clock are all untouched by construction rather than by
; adjustment, and only the SWITCHING gets finer.
;
; It is safe here and would not be system-wide because the bracket's task set
; is small and known (SPEC.md 53.2): the exclusive task, its kept worker and
; TF_SERVICE drivers. Nothing else can be mid-anything.
;
; The one thing that genuinely cannot survive is sch_account, whose elapsed
; arithmetic reads the PIT counter against its own 65536 period; it pauses
; while armed and is re-seeded here on the way out.
; -----------------------------------------------------------------------------
; sch_pit_now - channel 0's clock, LATCHED AND NEVER WRITTEN (SPEC.md 34.1)
; in:  nothing
; out: DX:AX = [ticks]:phase, the phase counting UP from 0 at the tick
;      (65536 - the latched count)
; clobbers: AX, DX, flags
;
; The latch, both reads AND the [ticks] read sit inside ONE pushf/cli...popf
; window, which is SPEC.md 34.1's BINDING atomicity rule: the 8254 ignores a
; second latch command while a latched value is unread, so an interleave
; hands one reader a stale byte - and a phase paired with a tick from outside
; the window can straddle a reload.
;
; sched.inc owns the PIT (SPEC.md 8.1), and this triple stood in FOUR places
; before size pass 2: here in sch_fast_off's re-seed, in wm.inc's wm_an_pit
; (which this replaces outright - it WAS this routine, in the wrong module),
; and in snd.inc's PCM deadline prime.  Two copies are deliberately NOT
; converted:
;   * sched_init's seed at the top of this file is `.ovlw`, the boot overlay,
;     and a near call from there to `.text` would assemble cleanly and run
;     wrong;
;   * sch_account's is on the IRQ0 path and does more (the IRR coherence
;     test), so it is neither this body nor a place to add a call.
;
; It does NOT re-derive sch_account's IRQ-pending coherence test and does not
; need to: every caller compares a deadline with `js`, so a DX one behind at
; the instant of a reload costs at most one extra pass and never a wrong
; answer.  This clock is a pace, not an accounting.
; -----------------------------------------------------------------------------
sch_pit_now:
    push cx
    pushf
    cli
    mov al, 0x00                ; latch channel 0's count
    out 0x43, al
    in al, 0x40
    mov cl, al
    in al, 0x40
    mov ch, al
    mov dx, [ticks]
    popf
    neg cx                      ; phase = 65536 - count, counting up from the
    mov ax, cx                  ; tick
    pop cx
    ret

; -----------------------------------------------------------------------------
sch_fast_on:
    push ax
    push bx
    cmp al, 2
    jb .out
    cmp al, 4
    ja .out
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [sch_fdivs + bx - 4] ; BX = 65536/N, tabled: no division in an arm.
                                ; AL stays N - the index arithmetic is BX's
                                ; alone, because both are wanted below
    pushf
    cli
    mov [sch_fcnt], al          ; divide from the first sub-tick, and set the
    mov [sch_fast], al          ; flag INSIDE the same window as the reprogram
    push ax
    mov al, 0x34                ; ch0, lo/hi, mode 2 - the mode is unchanged,
    out 0x43, al                ; only the divisor (SPEC.md 8.1)
    mov ax, bx
    out 0x40, al
    mov al, ah
    out 0x40, al
    pop ax
    popf
.out:
    pop bx
    pop ax
    ret

sch_fast_off:
    push ax
    push cx
    cmp byte [sch_fast], 0
    je .out
    pushf
    cli
    mov al, 0x34                ; the BIOS-compatible divisor 0 = 65536 back
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al
    call sch_pit_now            ; ...and re-seed the accounting stamp, or the
    mov [sch_pit_last], ax      ; first full tick after this charges a whole
    mov [sch_pit_last+2], dx    ; bracket's worth of garbage to somebody.
                                ; Its own pushf/cli nests inside this one and
                                ; its popf restores IF=0, which is what we are
                                ; already in
    mov byte [sch_fast], 0
    popf
.out:
    pop cx
    pop ax
    ret

sch_fdivs:  dw 32768, 21845, 16384      ; N = 2, 3, 4

; -----------------------------------------------------------------------------
; sched_unhook - restore the BIOS int 08h vector, then unhook mouse + sound
;
; Used before reboot so the ROM owns the machine again.
;
; in:       nothing
; out:      nothing
; clobbers: flags
; -----------------------------------------------------------------------------
sched_unhook:
    push ax
    push es

    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [sch_old08]
    mov [es:0x20], ax
    mov ax, [sch_old08+2]
    mov [es:0x22], ax
    popf

    call mouse_unhook           ; restores int 0Ch, masks IRQ4
    call snd_unhook             ; 61h bits 0-1 back to their boot state,
                                ; ch2 quiescent (SPEC.md 34.7)

    ; PIT ch0 back to the BIOS default (mode 3, square wave, divisor
    ; 65536). The vector is already restored so sch_isr can no longer read
    ; the PIT mid-sequence, but keep the three OUTs atomic anyway - an
    ; interleaved counter read between the two count-byte writes is
    ; undefined on a real 8253 (SPEC.md 8.1).
    pushf
    cli
    mov al, 0x36
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al
    popf

    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; sch_wake_ui - make the UI task ready (PROBE, docs/SCHED-IDLE-PLAN.md 6.3)
;
; One byte store, atomic on an 8086, so an ISR may do it with nothing held.
; Task 0 is the UI task and its record is at offset 0 of sch_tasks, so this is
; a single instruction with no index arithmetic. Setting READY on a task that
; is already READY is a no-op.
; -----------------------------------------------------------------------------
sch_wake_ui:
    mov word [sch_uiwake], 0x0101
                                ; ...and SAY SO, for the lost-wakeup guard at
                                ; ui_task's .idle (SPEC.md 8.1.2.3) AND for
                                ; the idle task's own halt guard (SPEC.md
                                ; 8.1.2), spent at sch_idle_body's .rearm.
                                ; The T_STATE store below is not enough on its
                                ; own: it carries no information while the
                                ; task is RUNNING, which is exactly when the
                                ; race happens.
                                ;
                                ; ONE word store, not two byte stores: the two
                                ; bytes are adjacent (the %if at the foot of
                                ; this file makes that a build-time fact) and
                                ; are always set together to the same value.
                                ; 6 bytes against 10, so the 8088's fetch
                                ; floor falls 43.4 -> 26.0 clocks - and an
                                ; 8086 takes an interrupt only at an
                                ; instruction boundary, so a single word store
                                ; cannot be torn where two byte stores can,
                                ; which is the right direction for a
                                ; lost-wakeup guard. NOTHING ABOUT THE WAKEUP
                                ; LOGIC CHANGES: same bytes, same values, same
                                ; two readers, each spending its own
    mov byte [sch_tasks+T_STATE], 1
    ret

; -----------------------------------------------------------------------------
; sch_idle_body / sch_idle_start - the idle task (SPEC.md 8.1.2)
;
; It is what sch_switch picks where the old code resumed the outgoing task, and
; UIBLOCK is what makes that load-bearing: a ui_task that can sleep makes the
; old fallback wrong, because resuming it would resume a sleeper. `make
; NOUIBLOCK=1` is the build where it is inert again - ui_task never blocks
; there, so the scan always finds it and the fallback is never taken.
;
; hlt rather than a spin, and the machine has to be able to take an interrupt
; or it never comes back: sti is inside the loop on both arms, not before it,
; so a stray cli anywhere cannot strand the machine for good, and a machine
; whose IRQ0 was masked at boot spins instead (the fail-safe, SPEC.md 8.1.2.1).
;
; THE HALT IS GUARDED, the same shape as ui_task's .idle (SPEC.md 8.1.2.3):
; sch_wake_ui sets [sch_idlewake] in the same breath as T_STATE, and the test,
; the halt and the clear are ONE IF=0 window with sti's shadow covering the
; hlt. Without it a wake landing between the test and the hlt is halted
; straight through and nothing looks again until the tick. The byte is spent
; HERE rather than by ui_task, so the fsx freeze - where ui_task is READY but
; ineligible (SPEC.md 53.2) - costs one extra yield per wake instead of a spin
; for the whole exclusive bracket.
; -----------------------------------------------------------------------------
sch_idle_body:
.loop:
    cmp byte [sch_idlespin], 0  ; THE FAIL-SAFE (SPEC.md 8.1.2.1): a machine
    jne .spin                   ; whose IRQ0 was masked at boot never leaves a
                                ; hlt, so it does not take one. One compare on
                                ; a path that is by definition doing nothing
    pushf
    cli                         ; the guard: nothing may be posted between the
    cmp byte [sch_idlewake], 0  ; test and the halt...
    jne .rearm                  ; a wake landed since the last pass: go round
    sti                         ; ...and sti's shadow covers the hlt, so an
    hlt                         ; interrupt taken here ENDS it rather than
.rearm:                         ; preceding it
    mov byte [sch_idlewake], 0  ; spent BEFORE the yield, so a wake posted
    popf                        ; after this is one the next pass sees
.spin:
    sti                         ; ...inside the loop: see above
    call task_yield             ; ...and THIS is what makes a wake immediate
    jmp short .loop             ; (SPEC.md 8.1.2.2). Only IRQ0 falls into
                                ; sch_switch; every other ISR - the mouse's
                                ; IRQ4 above all - iret's straight back here.
                                ; Without the yield a task that an ISR just
                                ; made ready waits for the next TICK, so the
                                ; wake byte on its own buys nothing and input
                                ; still lands up to 55 ms late. Costs one
                                ; switch per interrupt taken
                                ; while idle, which is what being idle is for

; in:  nothing.  out: CF=1 if no slot was free (the machine keeps the old
;      resume-the-outgoing fallback and nothing else changes)
;
; IN THE BOOT OVERLAY (docs/LAST-DROP-BYTES.md row 16). One caller, kmain:4182,
; and the idle task it spawns outlives it by construction - a task's code is
; sch_idle_body in `.text`, which is what BX = 0 above names. Taking the address
; of a `.text` label from `.ovl` is correct and taking the address of an `.ovl`
; one would not be: `.text` has vstart=0 and the consumer supplies KERNEL_SEG.
;
; **kmain does not read the CF this answers with**, which is what lets the site
; use the flag-banking gate rather than OVLCALLC. If a caller ever does, the
; call has to change with it.
; -----------------------------------------------------------------------------
section .ovlw
sch_idle_start:
    push bx
    push cx
    push dx
    in al, 0x21                 ; PIC1's mask, ONCE (SPEC.md 8.1.2.1): bit 0 is
    and al, 1                   ; IRQ0. Masked means the tick never arrives, so
    mov [sch_idlespin], al      ; a hlt here would never end - spin instead,
                                ; which is exactly what the machine did before
                                ; this task existed. A driver that masks IRQ0
                                ; LATER is out of this guard's reach and is a
                                ; driver bug; this is the boot-time case, and
                                ; it is the one docs/FIELD-NOTES.md has seen
    mov ax, sch_idle_body
    xor bx, bx                  ; KERNEL_SEG
    mov cx, SCH_IDLE_STK        ; the smallest class: its own footprint is 4
                                ; bytes and the rest is the floor (SPEC.md 8.7)
    mov dx, 0x00FF              ; DL -> T_INST = 0xFF, "owns no instance"
    call KERNEL_SEG:cw_task_spawn ; FAR: `.ovl` -> resident, through the shim
    jc .out                       ; driver.inc already needed
    mov [sch_idleslot], al      ; the ONE identifier for the idle task: the
                                ; scan skips this index and the fallback picks
                                ; it, and both need the index anyway
    clc
.out:
    pop dx
    pop cx
    pop bx
    retf                        ; FAR - see sched_init above. `retf` pops IP:CS
                                ; and leaves the flags alone, so the CF this
                                ; answers with survives it
section .text

; -----------------------------------------------------------------------------
; task_spawn - create a task that iret-launches into a near entry point
;
; Builds a complete saved frame at the top of the slot's stack: DX = the
; spawn argument, other GP regs zero, DS = ES = KERNEL_SEG, CS = KERNEL_SEG,
; IP = entry, FLAGS with IF=1. The slot's sch_cycles counter is zeroed
; before the publish (the ISR only charges the RUNNING slot, so a
; pre-publish write cannot race - SPEC.md 8.1). The scan starts at slot 1:
; slot 0 is the UI task, never free, and runs on the boot stack.
;
; CX IS THE STACK THE TASK ASKS FOR, IN BYTES (SPEC.md 8.7), and the scan takes
; the first free slot at least that big. SCH_PARTITION is written smallest
; first, so first fit IS smallest fit and a big worker cannot be given the last
; small slice a Timer would have fitted in. **CX = 0 means the largest class**,
; which is what a package that declares nothing gets - so a caller that has no
; opinion says nothing and is safe, rather than silently asking for the
; smallest.
; The entry routine must never ret - it terminates only via task_exit
; (usually through inst_task_die, SPEC.md 29) or loops forever.
;
; ATOMIC, and it has to be (SPEC.md 8): the whole of "find a slot with
; T_STATE = 0" ... "publish T_STATE = 1" runs under IF=0. A byte store being
; atomic only makes the record safe to READ mid-build - it does nothing
; about two spawners picking the SAME free slot, and since SPEC.md 20.6
; every task, not just the UI task, can reach this routine (a package's
; W_PAINT may spawn its worker, and W_PAINT is dispatched by whichever task
; drives the repaint). Pre-empted between the scan and the publish, the
; second spawner would overwrite the first's T_SP/T_INST and both callers
; would be handed the same slot number - one task lost, one instance record
; leaked for the session. The window is ~30 stores with no I/O, shorter than
; task_exit's.
;
; The frame is built through ES, not DS: sch_stacks is in LOW_SEG (.lowbss),
; which DS does not address. The task record itself is ordinary kernel .bss
; and stays DS-relative.
;
; in:       AX = entry point (an offset in the code segment below)
;           BX = the code segment the task runs in, or 0 for KERNEL_SEG.
;                A package's worker runs with CS = DS = its own segment and
;                ES = KERNEL_SEG (SPEC.md 20.1/20.6), which is exactly what
;                the frame below sets up - the scheduler itself needs no
;                change, because DS, ES and CS are already part of the saved
;                frame and sch_resume pops all three.
;           CX = the stack the task wants, in bytes; 0 = the largest class
;                (SPEC.md 8.7.1). Preserved.
;           DX = argument word - lands in the new task's DX; DL is also
;                stored to T_INST (instance index, 0xFF = none)
; out:      CF = 1 if no free slot (task not created)
;           CF = 0, AL = slot index (1..MAX_TASKS-1)
; clobbers: AX (AL is an output), flags
; -----------------------------------------------------------------------------
task_spawn:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp                     ; BP is the new task's SEGMENT below - a
                                ; VALUE, never an address, so no [bp+..] is
                                ; introduced and SS != DS is untouched
    pushf                       ; the caller's IF, banked across the window
    cli
    or bx, bx                   ; BX = 0 means the kernel's own segment
    jnz .haveseg
    mov bx, KERNEL_SEG
.haveseg:
    mov bp, bx                  ; banked: BX is the scan pointer below. A
                                ; register rather than a .bss word: task_spawn
                                ; uses BP nowhere else and makes no outbound
                                ; call between here and the frame build, so
                                ; the two segment stores can read it directly

    or cx, cx                   ; CX = the stack asked for, in bytes; 0 is
    jnz .haveask                ; "no opinion", and no opinion gets the LARGEST
    mov cx, SCH_STACK           ; class. Zero must not fall through as a
.haveask:                       ; request for the smallest free slice
    mov bx, sch_tasks + T_SIZE  ; scan for a free slot BIG ENOUGH, from slot 1
    mov si, 2                   ; SI = slot * 2, the index both tables take
.find:
    cmp byte [bx+T_STATE], 0
    jne .next
    cmp cx, [si+sch_stksize]    ; free - but does it fit? SCH_PARTITION is
    jbe .found                  ; written smallest first, so first fit is
.next:                          ; SMALLEST fit and one instruction
    add bx, T_SIZE
    inc si
    inc si
    cmp si, MAX_TASKS * 2
    jb .find
    xor ax, ax                  ; nothing free that is big enough: AL = 0,
    jmp .done                   ; which is never a valid slot (slot 0 is the
                                ; UI task) - .done turns it back into CF = 1
                                ; after the popf, which is the refusal
                                ; SPEC.md 20.6 already makes every package
                                ; degrade on

.found:
    mov cx, si                  ; the scan left SI = slot * 2, which is the
    shr cx, 1                   ; index BOTH tables take - so the slice costs
    mov di, ax                  ; two loads and no arithmetic. CX becomes the
    mov ax, [si+sch_stksize]    ; slot index again (AL returns it and the cycle
    mov si, [si+sch_stkbase]    ; counter below wants it); the entry is parked
    xchg si, di                 ; in DI and comes back as SI, where the rest of
                                ; this routine expects it. DI = the slice BASE,
                                ; where the canary goes, and AX = its SIZE
    push cx                     ; ...all three banked across the fill, which
    push di                     ; wants CX for its own count and walks DI
    push ax
    push ss                     ; ES = LOW_SEG (SPEC.md 2.1), which is what SS names
    pop es
    ; --- THE 0xCC FILL, and it is NOT a debug build's (SPEC.md 8.3) --------
    ; Every byte of the slice, so the deepest one anything ever touched can be
    ; read back out of LOW_SEG. The canary and the frame are written OVER this,
    ; below and above it, so nothing here has to know where either ends up.
    ;
    ; **UNCONDITIONAL, and that is the point.** It was `%ifdef KFZTRACE`, which
    ; meant the only kernel whose margin could be measured was one nobody runs -
    ; and the margin is the thing this project has just spent a week being
    ; wrong about (docs/FIELD-NOTES.md 29.6). It costs ONE `rep stosw` of the
    ; slice per SPAWN - a few hundred microseconds, once, against a launch that
    ; has already been through int 13h - and it turns tests/stackprobe from a
    ; probe of its own slice into one that can read EVERY task's high water on
    ; the shipping kernel, on the real machine, while the thing under suspicion
    ; is running.
    ;
    ; THE FILL IS DONE FIRST, before the top is worked out, and that ordering
    ; is what keeps this register-clean now that the size is per-slot: the
    ; count IS the size, so taking it while AX still holds it costs one `mov`
    ; and no second table read.
    mov cx, ax
    shr cx, 1                   ; ...as WORDS. SCH_SLOT's own %error is what
                                ; keeps every size even, which this and the
                                ; word canary below both need
    mov ax, 0xCCCC
    cld
    rep stosw                   ; ES:DI = the slice base, upwards - words,
                                ; 12.5 clocks a byte against stosb's 17, and
                                ; this whole fill runs inside the spawn's cli
    pop ax                      ; the size...
    pop di                      ; ...the base...
    pop cx                      ; ...and the slot index, which is NOT dead: the
                                ; cycle counter and the return value both want
                                ; it
    mov word [es:di], SCH_MAGIC
    add ax, di                  ; base + size = the slice TOP...
    sub ax, SCH_FRAME           ; ...and the fake frame is carved from it
    mov di, ax
    mov [bx+T_SP], di

                                ; the frame lives in LOW_SEG, so it is built
                                ; through ES - DS cannot reach it. ES IS
                                ; ALREADY LOW_SEG, loaded for the 0xCC fill
                                ; above, and nothing between writes it. CX IS
                                ; NOT DEAD - it is the slot index, and it is
                                ; still needed for the cycle counter and the
                                ; return value below
                                ; ...and DI is dead past the block: the frame
                                ; address is already parked in T_SP above, so
                                ; stosw may walk it. DF is 0 from the fill's
                                ; own cld and nothing between touches it
    mov ax, bp
    stosw                       ; DS  = the task's own segment
    mov ax, KERNEL_SEG
    stosw                       ; ES  = the kernel, always (SPEC.md 20.1)
    xor ax, ax
    stosw                       ; BP
    stosw                       ; DI
    stosw                       ; SI
    stosw                       ; BX
    xchg ax, dx                 ; one byte each way, and DX is wanted below
    stosw                       ; DX  = the spawn argument
    xchg ax, dx
    stosw                       ; CX
    stosw                       ; AX
    mov ax, si
    stosw                       ; IP  = entry
    mov ax, bp
    stosw                       ; CS  = the task's own segment
    mov ax, 0x0202
    stosw                       ; FLAGS: IF=1, reserved bit 1 set
    mov [bx+T_INST], dl         ; owning instance (0xFF = none)
    mov byte [bx+T_FLAGS], 0    ; TF_SERVICE is the driver cell's to set
                                ; (SPEC.md 53.2), never inherited

    mov ax, cx                  ; zero the slot's cycle counter before the
    shl ax, 1                   ; publish, so a reused slot never inherits
    shl ax, 1                   ; a dead task's total (SPEC.md 8.1)
    mov di, ax
    xor ax, ax                  ; through AX: mov [m],ax is 4 bytes where
    mov [di+sch_cycles], ax     ; mov word [m],0 is 6, and AX is free here
    mov [di+sch_cycles+2], ax   ; (it held the FLAGS word, dead, and is
                                ; reloaded from CX two lines below).
                                ; [di+disp] addresses DS, not SS - DI is not
                                ; BP - which is the addressing this already
                                ; used

    mov byte [bx+T_STATE], 1    ; publish - MUST be the last record write
    mov ax, cx                  ; AL = slot index (1..MAX_TASKS-1)

.done:
    popf                        ; IF back - and the caller's CF with it, so
    cmp al, 1                   ; the answer is re-derived from AL here:
                                ; AL = 0 -> CF = 1 (table full), else CF = 0.
                                ; The pops below touch no flags.
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; task_yield - give up the rest of this time slice
;
; Fakes the hardware int 08h frame (FLAGS with the caller's IF, CS, IP) with
; pushf / cli / push cs / near call, then saves registers exactly like the
; ISR and drops into the common switch. The eventual iret lands on the ret
; below with all registers and the caller's IF restored.
;
; in:       nothing
; out:      nothing (returns later, after other tasks have run)
; clobbers: flags only (arithmetic flags; IF preserved via the frame)
; -----------------------------------------------------------------------------
task_yield:
%ifdef KERN_BIG
    ; --- the VMware absolute pointer's service point (SPEC.md 9.11.3) --------
    ; vmmouse is POLLED, not interrupt-driven, so every spin loop that waits on
    ; the mouse - a drag, a grow, a menu track, the file manager's icon drag -
    ; has to pump it or the button-release it is waiting for never arrives (a
    ; freeze). They ALL call task_yield, so draining it here services every one,
    ; present and future, near and far (the cold segment's cw_task_yield lands
    ; here too).
    ;
    ; **WRITTEN OUT RATHER THAN VMM_POLL, and the difference is DS.** The macro
    ; is for ui_task and menu_drop, which already hold the kernel's; this site
    ; does not - task_yield is an OSAPI slot, so the caller may be a package
    ; with its own DS. The GATE is still the macro's cs-relative compare, and
    ; the segment load moves INSIDE the branch: a machine with no backdoor then
    ; pays 9 bytes and ~39 clocks (the 8088's fetch floor over the encoding,
    ; PERFORMANCE.md Part 2) instead of 16 and 91 - 8.2 us against 19.1, or
    ; 1.2% of a 693 us switch instead of 2.8%. That is the whole price the
    ; target machine pays for a branch it can never take.
    ;
    ; Before the cli/frame build: this is still the caller's task context with
    ; IF as it left it, exactly as ui_task's own VMM_POLL - and vmm_poll takes
    ; its own cli, and its own STACK, around the apply.
    cmp byte [cs:vmm_on], 0
    jz .noyvmm
    push ds
    push ax
    mov ax, KERNEL_SEG
    mov ds, ax
    call vmm_poll
    pop ax
    pop ds
.noyvmm:
%endif
    ; STKBALANCE-OK: fabricates the int 08h frame (pushf / push cs /
    ; call .save), so the ret below is reached by the scheduler's iret and not
    ; by .save returning - the depth at that ret is the frame, by design.
    pushf                       ; FLAGS slot - carries the caller's IF
    cli                         ; frame build + switch must be atomic
    push cs                     ; CS slot
    call .save                  ; IP slot; iret resumes at the ret below
    ret

.save:
    push ax
    push cx
    push dx
    push bx
    push si
    push di
    push bp
    push es
    push ds
    push cs                     ; caller's DS is on the stack; we need ours
    pop ds
                                ; NO sch_account HERE (SPEC.md 8.1.1). It used
                                ; to charge the slice unconditionally, and on a
                                ; machine with one runnable task that is 500
                                ; cycles to bill a task and then go on running
                                ; the same task - 1,048 times a second, 10.9%
                                ; of an idle 8088. sch_switch charges at .pick
                                ; instead, and only when the pick is a real
                                ; change; the sch_resume branch below is a
                                ; no-change by construction
    cmp byte [sch_lock], 0      ; scheduler locked: resume self immediately
    jne sch_resume
    jmp sch_switch

; -----------------------------------------------------------------------------
; task_sleep - block this task for AX timer ticks (18 ~= 1 second)
;
; T_WAKE is written before T_STATE (SPEC.md 8) so the ISR's wake scan never
; sees a sleeper with a stale deadline. Wrap-safe: the ISR compares with the
; subtraction idiom, never magnitude.
;
; in:       AX = ticks to sleep
; out:      nothing (returns when the deadline has passed)
; clobbers: flags
; -----------------------------------------------------------------------------
; sch_currec - BX = the RUNNING task's record offset ([sch_cur] * T_SIZE)
; in:  nothing
; out: BX = the offset, so every caller's `[bx+sch_tasks+T_*]` is unchanged
; clobbers: BX ONLY - not CX, which is why the multiply is three open shifts
;      rather than the `mov cl,3 / shl bx,cl` the five sites spelled it with.
;      instance.inc's inst_ctx_inst pushes BX and nothing else, and that is
;      the site that decides the contract
;
; The idiom stood in six places and was spelled three different ways.  Five
; are now calls; THE SIXTH IS NOT AND MUST NOT BE - sch_switch's copy is on
; the switch path, where a near call and ret (~43 clocks) is 0.05% of an idle
; machine but ~1.4% at the 1,541 switches a second docs/SCHED-IDLE-PLAN.md
; 6.1.2 measures with an app open.  That is why this saves what it saves.
;
; PLACEMENT: outside the sch_isr -> sch_switch -> sch_resume fall-through run.
; -----------------------------------------------------------------------------
sch_currec:
    mov bl, [sch_cur]
    xor bh, bh
    shl bx, 1                   ; T_SIZE = 8, as three open shifts: CX belongs
    shl bx, 1                   ; to the callers here
    shl bx, 1
    ret

; -----------------------------------------------------------------------------
task_sleep:
    push ax
    push bx
                                ; NO push cx: sch_currec clobbers BX only, and
                                ; nothing else in this body names CX
    call sch_currec             ; BX = current task record
    add ax, [ticks]
    mov [bx+sch_tasks+T_WAKE], ax       ; deadline first...
    mov byte [bx+sch_tasks+T_STATE], 2  ; ...then state (both stores atomic)
    call task_yield             ; switch away; we resume once state -> 1

    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; task_exit - terminate the CURRENT task (self-exit only; NEVER RETURNS)
;
; in:  BX = ptr to a release byte zeroed atomically with the task's death
;      (an instance record's I_STATE, SPEC.md 29), or 0 for none
;
; Runs entirely under IF=0 (the cli below has no matching popf - control
; never returns): charges the final slice, zeroes the release byte, frees
; the own task record, and jumps into sch_switch, which parks a
; now-meaningless SP into the freed record (harmless) and resumes the next
; ready task. The freed slot is skipped by the round-robin scan (T_STATE=0);
; the fallback cannot pick a dead task because it picks [sch_idleslot], a task
; that is always READY, owns no instance and never exits - and sch_idle_start
; spawns it at boot, before any task can be created let alone exit. The
; resume-the-outgoing-task arm survives only for the window before that call,
; where nothing can have exited either. Because the release byte and T_STATE
; are both cleared inside one IF=0 window, an instance slot and its task free
; at the same instant - "instance free but task table still holding the
; zombie" is unobservable (SPEC.md 8/29).
;
; PLACEMENT: outside the sch_isr -> sch_switch -> sch_resume fall-through
; run - this routine reaches sch_switch by explicit jmp only.
; -----------------------------------------------------------------------------
task_exit:
    cli                         ; IF=0 to the end - we never return
    mov di, bx                  ; DI survives sch_account's clobbers
    call sch_account            ; charge the final slice to the dying slot
    test di, di
    jz .norel
    mov byte [di], 0            ; release the instance record (SPEC.md 29)
.norel:
    call sch_currec             ; free our own task record
    mov byte [bx+sch_tasks+T_STATE], 0
    jmp sch_switch              ; explicit jump - placement-safe

; -----------------------------------------------------------------------------
; sch_account - charge the PIT cycles since the last measure point to the
;               task that just ran (module-internal, SPEC.md 8.1)
;
; PLACEMENT: this routine must sit OUTSIDE the sch_isr -> sch_switch
; fall-through path (sch_isr does not jump to sch_switch - it falls into
; it). Inserting any routine between them sends every unlocked tick through
; that routine's ret with no return address on the stack. (task_exit above
; enters sch_switch by explicit jmp - that is the sanctioned pattern.)
;
; Builds the 32-bit timestamp now = [ticks]*65536 + phase, where phase =
; 65536 - latched count (0 at reload, monotone within a period), subtracts
; the previous stamp and adds the difference into sch_cycles[sch_cur].
; The timestamp is exact for intervals of any length - a full-tick slice
; with no yield spans exactly one counter reload, which a bare mod-65536
; count delta would alias to ~0.
;
; Pending-IRQ disambiguation: during an IF=0 window the counter may reload
; with int 08h not yet delivered, leaving [ticks] one behind the phase.
; After latching, read the PIC IRR; if IRQ0 is pending AND the phase is
; small (< 0x8000), the reload preceded the latch - use ticks+1. A large
; phase with the bit set means the reload landed in the us between latch
; and IRR read - keep ticks.
;
; in:       IF=0, DS=KERNEL_SEG, [sch_cur] = the task being charged. Both
;           call sites (sch_isr, task_yield's save path) hold a complete
;           saved frame, so clobbering is safe.
; out:      SI = 4 * [sch_cur], the byte offset of that task's slot inside
;           sch_cycles - ON BOTH ARMS, including the sch_fast early-out,
;           which charges nothing but still answers the question. task_cycles
;           and task_debit used to re-derive this from the same [sch_cur]
;           under the same cli; now they do not.
; clobbers: AX, BX, CX, DX, SI - on BOTH arms now; the early-out used to
;           clobber nothing. Every call site already tolerated the list:
;           sch_isr holds a full frame, sch_switch's .pick pushes DX and BX,
;           task_exit banks the record pointer in DI first, task_cycles and
;           task_debit push BX/CX/SI.
;
; WHAT THE MOVE COSTS, said out loud. On the charging arm it is exactly
; neutral - the same five instructions, relocated from .cohere, which every
; path through that arm reaches anyway. On the sch_fast EARLY-OUT arm it is
; 12 bytes of new straight-line code: 23 clocks of execution under a 52.1
; fetch floor, so 52.1 clocks per entry, and that arm is taken only while an
; fsx bracket is armed (SPEC.md 53.2.1). sch_isr calls this at .full, the
; TICK, and not on sub-ticks, so the ISR's share of that is 18.2/s = 948
; clocks a second = 0.020% of a 4.77 MHz 8088; each additional REAL task
; change inside the bracket adds 52.1 more (100/s would be 0.11%), and the
; freeze leaves at most the exclusive task, its kept worker and a driver's
; service worker eligible. Outside a bracket sch_fast is 0, this arm is not
; taken, and the change costs nothing at all. The alternative - a second copy
; of the index block on the early-out arm - is 12 of the 24 bytes back for
; that 0.02%, and the bytes win.
; -----------------------------------------------------------------------------
sch_account:
    mov bl, [sch_cur]           ; SI = &sch_cycles[sch_cur] - an OUTPUT, so it
    xor bh, bh                  ; is computed before the early-out below and
    shl bx, 1                   ; not after the charge. In BX, because CX
    shl bx, 1                   ; carries the latched phase further down; and
    mov si, bx                  ; hoisting it above the latch cannot change
                                ; the reading, because nothing before the
                                ; `out 0x43` affects it
    cmp byte [sch_fast], 0      ; the fsx sub-tick (SPEC.md 53.2.1) divides the
    jne .idle                   ; very period this arithmetic is built on, so
                                ; accounting PAUSES for the bracket - here
                                ; rather than at the call sites, because
                                ; task_yield and task_cycles reach it too and
                                ; a half-gated stamp is worse than none
    mov al, 0x00                ; latch channel 0's count
    out 0x43, al
    in al, 0x40                 ; count lo
    mov cl, al
    in al, 0x40                 ; count hi
    mov ch, al
    neg cx                      ; CX = phase = 65536 - count
    mov dx, [ticks]             ; DX = timestamp high word candidate
    mov al, 0x0A                ; OCW3: next read of 0x20 returns the IRR
    out 0x20, al
    in al, 0x20
    test al, 1                  ; IRQ0 pending?
    jz .cohere
    or cx, cx                   ; phase >= 0x8000: reload came after the
    js .cohere                  ; latch - keep ticks
    inc dx                      ; reload before the latch: tick not yet in
.cohere:
    mov ax, cx                  ; elapsed = now - last, 32-bit
    sub ax, [sch_pit_last]
    mov bx, dx
    sbb bx, [sch_pit_last+2]
    mov [sch_pit_last], cx
    mov [sch_pit_last+2], dx
    add [si+sch_cycles], ax     ; sch_cycles[sch_cur] += elapsed, at the SI
    adc [si+sch_cycles+2], bx
.idle:
    ret

; -----------------------------------------------------------------------------
; task_cycles - flush the slice so far onto the running task and read its total
;
; The stamp half of the charge-a-callback pair (SPEC.md 8.1). Both readings
; come from the same sch_cycles slot, which only grows while that task is
; actually on the CPU, so the difference measures real CPU time and not the
; ticks the task spent pre-empted away.
;
; PLACEMENT: outside the sch_isr -> sch_switch -> sch_resume fall-through run,
; for the reason spelled out on sch_account.
;
; in:       nothing (any task; IF either way, DS = KERNEL_SEG)
; out:      DX:AX = sch_cycles[sch_cur] with the pending slice folded in
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
task_cycles:
    push bx
    push cx
    push si
    pushf
    cli                         ; sch_account's contract: IF=0
    call sch_account            ; ...which hands back SI = the slot's offset
    mov ax, [si+sch_cycles]
    mov dx, [si+sch_cycles+2]
    popf
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; task_debit - close a task_cycles stamp: return the cycles elapsed since it
;              and REMOVE them from the running task's counter
;
; Move, not copy (SPEC.md 8.1): the caller adds the amount to some other
; counter (an instance's I_CYC, SPEC.md 29), so the two together still
; partition one total. Nesting works for free - an inner debit lowers the
; outer pair's end reading by exactly the inner amount, so the outer stretch
; is credited only with what it did itself.
;
; A negative difference is forced to 0 and nothing is subtracted: sch_account
; can hand back a slightly backwards timestamp when its pending-IRQ
; disambiguation guesses the wrong side of a counter reload, and an unclamped
; borrow would wrap the caller's 32-bit counter to just under 2^32 (seen in
; practice as a package's tally jumping backwards by ~0.6 tick).
;
; in:       DX:AX = stamp from task_cycles, taken on THIS task
; out:      DX:AX = elapsed cycles (0 if the counter regressed), already
;           subtracted from sch_cycles[sch_cur]
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
task_debit:
    push bx
    push cx
    push si
    push di
    push bp
    mov bp, ax                  ; BP:DI = the start stamp - sch_account
    mov di, dx                  ; clobbers AX/BX/CX/DX/SI, but not BP/DI
    pushf
    cli
    call sch_account            ; ...which hands back SI = the slot's offset
    mov ax, [si+sch_cycles]
    mov dx, [si+sch_cycles+2]
    sub ax, bp                  ; DX:AX = now - start
    sbb dx, di
    test dx, dx                 ; counter regressed: report nothing and leave
    js .none                    ; the task's tally alone
    sub [si+sch_cycles], ax     ; ...and off the task's tally it comes
    sbb [si+sch_cycles+2], dx
    popf
    jmp .out
.none:
    xor ax, ax
    xor dx, dx
    popf
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sched_mode_set - select the scheduling mode (SPEC.md 8.2)
;
; Normalises the flag to 0/1 and hands the incoming mode a full watchdog
; budget by clearing the hold counter. Both stores are single bytes made
; under IF=0, so no tick can observe a half-changed mode; the worst case is
; that the tick racing the store schedules with the old mode - one tick's
; decision, self-correcting on the next.
;
; Callable from any task at any time: the two modes share the task table, the
; per-task stacks and the 24-byte saved frame above, so a mode change adds no
; state, removes no state and invalidates no saved frame - no task has to be
; parked, drained or notified. That is why the Control Panel may call this
; from inside a window callback while holding the gfx lock (SPEC.md 31).
;
; sch_lock is deliberately NOT reused as the "don't switch" flag even though
; it looks like one: task_yield tests it too and self-resumes when it is set
; (see task_yield above), so raising sch_lock would not make multitasking
; cooperative - it would stop multitasking altogether and freeze every Timer
; and Bounce instance forever, their task_sleep never handing the CPU on.
; Cooperative mode must leave the VOLUNTARY path fully working and suppress
; only the INVOLUNTARY one, hence a separate byte tested after sch_lock.
;
; PLACEMENT: outside the sch_isr -> sch_switch -> sch_resume fall-through
; run, for the reason spelled out on sch_account.
;
; in:       AL = 0 pre-emptive, any non-zero = cooperative
; out:      nothing
; clobbers: flags (every register preserved, AL included)
; -----------------------------------------------------------------------------
sched_mode_set:
    push ax
    pushf
    cli                         ; no tick between the two stores
    or al, al
    jz .store
    mov al, 1                   ; normalise: any non-zero means cooperative
.store:
    mov [sch_coop], al
    mov byte [sch_hold], SCH_WD_TICKS
                                ; the incoming mode starts with a full budget
    popf
    pop ax
    retf

; -----------------------------------------------------------------------------
; sched_mode_get - read the current scheduling mode (SPEC.md 8.2)
;
; The only sanctioned reader of sch_coop: the Control Panel's radio glyphs,
; the About box's third line and the Task Manager's SCHED field all come
; through here. A one-byte read needs no critical section - the store side is
; a single atomic byte write (sched_mode_set).
;
; PLACEMENT: outside the sch_isr -> sch_switch -> sch_resume fall-through
; run, for the reason spelled out on sch_account.
;
; in:       nothing
; out:      AL = 0 pre-emptive, 1 cooperative
; clobbers: AL (it is the output); flags untouched
; -----------------------------------------------------------------------------
sched_mode_get:                 ; FAR-ENTERED (SPEC.md 2.6.1): no near caller
    mov al, [sch_coop]          ; of its own, so the shim that used to `call`
    retf                        ; this and `retf` is gone

; -----------------------------------------------------------------------------
; sch_isr - int 08h handler (PIT, ~18.2 Hz)
;
; Chains to the BIOS tick FIRST (it maintains its own count and sends the
; EOI - we must not), then bumps [ticks], wakes due sleepers, and switches
; round-robin if the tail's two tests allow it: sch_lock must be clear, and
; in cooperative mode only the watchdog switches (SPEC.md 8.2). Runs with
; IF=0 throughout; calls no BIOS service beyond the chain; never sti.
;
; Decision order is binding: accounting and the wake scan run BEFORE both
; tests, so cycle accounting and task_sleep deadlines keep working every tick
; in both modes; and sch_lock is tested BEFORE the mode, so a floppy read
; (disk.inc holds sch_lock across int 13h) accumulates no hold ticks and can
; never trip the watchdog.
;
; in:       hardware interrupt (any task, any DS/ES)
; out:      via iret, possibly into a different task
; -----------------------------------------------------------------------------
%ifdef KFZTRACE
KHB_STK     equ 20              ; x = 160: where sch_stkdie says the canary
                                ; died, well clear of the heartbeat
KHB_STUCK   equ 546             ; ticks (30s) with NO ui_task pass before the
                                ; watchdog prints. It was 5 seconds against the
                                ; old held-mutex trigger, and 5 seconds of NO
                                ; PASS is something a healthy machine really
                                ; does: ModPlug's opening W_PAINT is one pass
                                ; and takes tens of seconds on the machine this
                                ; is for, so a short threshold would have the
                                ; instrument firing in the middle of the very
                                ; draw the bug lives in - MANUFACTURING the
                                ; failure it exists to observe. The reported
                                ; freeze is permanent (180s and counting), so
                                ; nothing is lost by waiting
KHB_IPOFF   equ 34              ; this block's 8 pushed words + sch_isr's 9,
                                ; so [sp+34] is the IP the CPU pushed
KHB_X       equ 0               ; the LEFT EDGE: fifteen values are 75 bytes and
                                ; anything further right runs into the clock,
                                ; whose glyphs cannot be told from cells
khb_beat:   db 0                ; the heartbeat's own counter, in .text so the
                                ; ISR can reach it through CS before DS is ours
khb_kfz:    db 0                ; how far ui_task's `.keys` branch got with the
                                ; last keystroke (the KFZ macro in kernel.asm).
                                ; The package-side instrument came back reading
                                ; ZERO, so W_ONKEY never reaches ModPlug and the
                                ; stall is somewhere between int 16h and
                                ; wm_pkgcall - which is nine marks wide
khb_chain:  db 0                ; ...and how many times the BIOS int 08h chain
                                ; CAME BACK: beat - chain is 0 or 1, and which
                                ; one it is says whether the machine stopped
                                ; inside the interrupt or outside it
khb_sph:    db 0                ; SP high / low at the last tick, and whether
khb_spl:    db 0                ; task 0's floor word is still there - staged
khb_bad:    db 0                ; so the emit loop can treat them as table rows
khb_csh:    db 0                ; CS high: 00 kernel, 0D cold, else a package
khb_iph:    db 0                ; the interrupted IP: which routine the machine
khb_ipl:    db 0                ; is standing in when it stops
khb_imr:    db 0                ; --- WHY THE MACHINE STOPS, three readings ---
khb_pisr:   db 0                ; The field reports a HARD freeze and a strip
khb_flg:    db 0                ; that stops moving, and IRQ0 is the only thing
                                ; painting the strip - so whatever ends the
                                ; machine ends IRQ0, and there are exactly four
                                ; ways for that to happen. The PIC's MASK
                                ; register says whether IRQ0 was masked off; its
                                ; IN-SERVICE register says whether an EOI went
                                ; missing (bit 0 is always set here - this ISR
                                ; is the one in service - so it is any OTHER bit
                                ; that means something); and the high byte of
                                ; the interrupted FLAGS carries IF in bit 1,
                                ; which says whether the code we interrupted was
                                ; running with interrupts off. The fourth, a
                                ; reprogrammed PIT, is what a beat that stops
                                ; while these three read normal would leave.
khb_deep:   db 0                ; --- THE DEEPEST A BACKGROUND TASK HAS BEEN,
khb_dcur:   db 0                ; and WHERE. tools/stkwater.py's 0xCC fill says
khb_dcsh:   db 0                ; how deep the slice went; it cannot say what
khb_diph:   db 0                ; was running, and the two answers turned out
khb_dipl:   db 0                ; to disagree: cutting 32 bytes off the
                                ; driver's STATIC worst chain moved the
                                ; measured high water by six, because the
                                ; runtime path is not the static one (an ARP
                                ; cache that hits, a DHCP timer that does
                                ; nothing). This is one byte of depth and the
                                ; interrupted CS:IP at the moment it was
                                ; reached, sampled every tick - so the next cut
                                ; is aimed at the chain the machine is actually
                                ; in. Depth, not SP, because the answer has to
                                ; compare across slots - and it is derived from
                                ; sch_stacks + slot * SCH_STACK, never from
                                ; SP's low bits: at 384 a slice is 128-aligned
                                ; and the `neg al` that read 256-aligned ones
                                ; produced garbage (the switch path below says
                                ; the same thing at more length)
khb_wide:   db 0x00, 0x0F, 0xF0, 0xFF   ; two bits -> four pixels each
khb_stuck:  dw 0                ; consecutive ticks with NO ui_task pass
khb_uipass: dw 0                ; bumped once per ui_task loop
khb_uilast: dw 0                ; ...and what it read at the last tick
khb_msg:    db 'AT 00:0000 OWN 00 CUR 00 SP 0000 R 0000 0000 0000 M 00 V 00 F 00 BK 0000', 0
KHB_M_CS    equ 3                ; 'AT cc:iiii OWN oo CUR cc SP ssss' - every
KHB_M_IPH   equ 6                ; one of these counted off the literal above,
KHB_M_IPL   equ 8                ; which is what the first version did not do:
KHB_M_OWN   equ 15               ; the fields landed on each other and on the
KHB_M_CUR   equ 22               ; NUL, so the report ran on into the hex digit
KHB_M_SPH   equ 28               ; table and printed it
KHB_M_SPL   equ 30
KHB_M_RET   equ 35
KHB_M_IMR   equ 52               ; ...and the four that say WHY it stopped:
KHB_M_ISR   equ 57               ; mask, in-service, the interrupted FLAGS, and
KHB_M_FLG   equ 62               ; the entry/chain pair
KHB_M_BEAT  equ 68
KHB_M_CHAIN equ 70
khb_hexd:   db '0123456789ABCDEF'

; khb_hex - AL as two hex digits at DS:SI (KFZ only; clobbers AX, BX)
khb_hex:
    push bx
    push ax
    mov bl, al
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    and al, 0x0F
    xor bh, bh
    push bx
    mov bl, al
    mov al, [khb_hexd+bx]
    pop bx
    mov [si], al
    mov al, bl
    and al, 0x0F
    push bx
    mov bl, al
    mov al, [khb_hexd+bx]
    pop bx
    mov [si+1], al
    pop ax
    pop bx
    ret
khb_srcs:   dw khb_beat, khb_chain, khb_kfz, sch_cur, sch_lock, gfx_lock_flag
            dw gfx_lock_own, khb_sph, khb_spl, khb_bad, khb_csh, khb_iph
            dw khb_ipl, khb_imr, khb_pisr, 0
khb_srcend:                     ; FIFTEEN cells is 75 of a CGA row's 80 bytes.
                                ; khb_flg is deliberately NOT here: IRQ0 is
                                ; delivered only with IF set, so the one bit
                                ; anybody would read it for is a CONSTANT in
                                ; this context. It stays in the text report,
                                ; where the width costs nothing, for DF and OF
KHB_CELLS   equ ((khb_srcend - khb_srcs) / 2) - 1   ; derived off the table, so
KHB_ROWB    equ ((KHB_CELLS * 5) + 1) & ~1          ; a cell added above cannot
                                ; leave the row behind it: five bytes a cell,
                                ; rounded UP to a word because the blit moves
                                ; words, and the odd byte it rounds through is
                                ; the trailing gap nobody reads
%if KHB_ROWB > 80               ; a CGA row is 80 bytes, a Hercules row 90, and
  %error "the heartbeat strip no longer fits a CGA row"
%endif
khb_row:    times KHB_ROWB db 0xFF   ; THE STRIP, COMPOSED HERE AND NOT ON THE
                                ; SCREEN. Four bytes per value are decoded once
                                ; into this row and the row is then blitted into
                                ; each of the four banks - 60 nibble decodes and
                                ; four rep movsw, against 240 decodes written
                                ; straight at the framebuffer. THAT COST THE
                                ; MACHINE ITS MOUSE: the old loop was ~10ms per
                                ; call on a 4.77MHz 8088 and it is called TWICE
                                ; a tick with IF=0, while a 1200-baud mouse byte
                                ; lands in the 8250's ONE-byte buffer every
                                ; 7.5ms - so every packet lost bytes, every run
                                ; broke on the resync rule (SPEC.md 9.5), and
                                ; the field reported no mouse at all. The blank
                                ; byte between values is never written, so the
                                ; 0xFF it is born with is what shows: WHITE, and
                                ; guaranteed white rather than whatever the menu
                                ; bar had there

; -----------------------------------------------------------------------------
; khb_paint - the strip (KFZ only). Preserves every register and the flags.
;
; CALLED TWICE PER TICK, and that is the whole of what makes khb_beat and
; khb_chain mean anything. Painting once at entry cannot say which side of the
; BIOS chain a machine died on - the entry paint of the tick that died and the
; entry paint of a healthy tick are the same picture, beat one ahead of chain
; in both. Painted again after the chain returns, the LAST picture on a stopped
; screen answers it: equal counters mean the interrupt ran to the end and
; whatever stopped the machine is out in task code, one apart means it never
; came back from the BIOS.
;
; Two bits per framebuffer byte, four pixels per bit, high bit first, a set bit
; BLACK on the white menu bar, four banks tall (SPEC.md 39.3). At one pixel per
; bit a photographed cell is unreadable: the field's screenshots arrive at about
; 2x, so a bit was two screen pixels and every reading was a guess.
; -----------------------------------------------------------------------------
khb_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    pushf
    mov bx, [vid_rseg]          ; NOT BEFORE THE ADAPTER IS PICKED: this ISR
    or bx, bx                   ; runs from the first tick, which is during the
    jz .out                     ; splash, and [vid_rseg] is 0 until vid_setmode
                                ; publishes it - so an unguarded paint lands at
                                ; segment 0, which is the INTERRUPT VECTOR TABLE
    mov es, bx
    cld                         ; the blit below is a string op in an ISR

    ; --- decode the fifteen values ONCE, into khb_row (SPEC.md 9.6.5) --------
    ; Sixty nibbles, not two hundred and forty. DS is KERNEL_SEG here, so the
    ; row is plain [di]; the framebuffer is not touched until the blit.
    mov si, khb_srcs
    mov di, khb_row
    mov cl, 2                   ; the rotate count, set ONCE - the old loop
                                ; spent a push/pop pair per shift for it
.cell:
    mov bx, [cs:si]
    or bx, bx
    jz .blit                    ; 0 ends the table
    inc si
    inc si
    mov ah, [cs:bx]             ; the value, rotated two bits at a time...
    xor bh, bh                  ; ...and BH is 0 from here on: BL is the only
    mov ch, 4                   ; half khb_wide is ever indexed by
.nib:
    rol ah, cl                  ; the next two bits, high pair first, into 1:0 -
                                ; four of these put AH back as it started
    mov bl, ah
    and bl, 3
    mov al, [cs:khb_wide+bx]
    not al                      ; a set bit is black on the white bar
    mov [di], al
    inc di
    dec ch
    jnz .nib
    inc di                      ; one blank byte between values, left at the
    jmp short .cell             ; 0xFF khb_row is born with
.blit:
    xor dx, dx
.bank:
    mov si, khb_row
    mov di, dx
    add di, KHB_X
    mov cx, KHB_ROWB / 2
    rep movsw                   ; DS:SI = the row, ES:DI = the bank
    add dx, 0x2000
    cmp dx, 0x8000
    jb .bank
.out:
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

sch_isr:
    push ax                     ; frame layout documented at top of file
    push cx
    push dx
    push bx
    push si
    push di
    push bp
    push es
    push ds
    push cs
    pop ds

%ifdef KFZTRACE
    ; --- the heartbeat (SPEC.md 7/8, KFZ only) -------------------------------
    ; Painted from IRQ0, which is the whole point: on the machine this exists
    ; for the timer is the last thing still running, so it is the only context
    ; able to report. Eight bytes of kernel state, left to right:
    ;
    ;   beat  sch_cur  sch_lock  gfx_lock_flag  gfx_lock_own  SPhi  SPlo  stk0
    ;
    ; EACH IS 32 PIXELS WIDE - two bits per framebuffer byte, four pixels per
    ; bit, high bit first - because at one pixel per bit a photographed cell
    ; is unreadable: the field screenshots arrive at about 2x, so a bit was
    ; two screen pixels and every reading was a guess.
    ;
    ; A set bit is BLACK on the white menu bar. Four banks tall (SPEC.md 39.3).
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    pushf
    ; --- AND WHERE THE MACHINE ACTUALLY IS ----------------------------------
    ; The interrupted IP, straight off the frame the CPU pushed. On a machine
    ; that has stopped this is the one reading that NAMES the code rather than
    ; describing the symptom: nasm -l turns it back into a routine. The offset
    ; is this block's 8 words plus sch_isr's 9, both fixed.
    mov bx, sp
    mov ax, [ss:bx+KHB_IPOFF]
    mov [cs:khb_ipl], al
    mov [cs:khb_iph], ah
    mov ax, [ss:bx+KHB_IPOFF+2] ; ...and the HIGH BYTE of CS with it, which is
    mov [cs:khb_csh], ah        ; all that is needed to say WHICH segment: 00
                                ; kernel, 0D the cold segment, anything else a
                                ; package - an IP alone is ambiguous across
                                ; three address spaces
    mov ax, [ss:bx+KHB_IPOFF+4] ; ...and the FLAGS above it: IF is bit 9, so
    mov [cs:khb_flg], ah        ; bit 1 of the byte kept here
    in al, 0x21                 ; the PIC's mask: is IRQ0 (bit 0) still let in?
    mov [cs:khb_imr], al
    mov al, 0x0B                ; OCW3 read-select: bit 3 distinguishes it from
    out 0x20, al                ; OCW2, so this cannot be mistaken for the EOI
    in al, 0x20                 ; the IN-SERVICE register
    mov [cs:khb_pisr], al
    mov al, 0x0A                ; ...and put the selection back to the IRR,
    out 0x20, al                ; because RIS is STICKY: a later `in al, 0x20`
                                ; anywhere else would otherwise be handed the
                                ; in-service register and believe it
    inc byte [cs:khb_beat]      ; ...AFTER the IP read, which needs BX itself -
                                ; taking the framebuffer segment in the middle
                                ; left ES pointing at the stack pointer and the
                                ; whole strip went blank
    mov ax, sp                  ; the depth of the stack we interrupted; the
    mov [cs:khb_spl], al        ; frames above it are a constant
    mov [cs:khb_sph], ah
    ; --- ...and the DEEPEST it has ever been, with the code that took it there
    cmp byte [sch_cur], 0       ; task 0's stack is not a slice of sch_stacks
    je .khbnodeep               ; and its SP means something else entirely
    push bx                     ; the old `neg al` depth assumed 256-byte
    push dx                     ; 256-ALIGNED slices; SCH_STACK is 384 now, so
    mov bl, [sch_cur]           ; odd slots sit at 128 mod 256 and the byte it
    xor bh, bh                  ; produced was garbage on the instrument that
    shl bx, 1                   ; aims the next stack cut. THROUGH sch_stkbase
    mov ax, [bx+sch_stkbase]    ; now, so this reader assumes nothing about the
    add ax, [bx+sch_stksize]    ; layout OR the size: base + size is the TOP and
    sub ax, sp                  ; depth = top - SP, less this sampler's own two
    sub ax, 4                   ; pushes so it agrees with khb_spl/khb_sph
    shr ax, 1                   ; above - stored HALVED, so one byte holds
    pop dx                      ; 0..384 and tools/stkwater.py doubles it back
    pop bx
    cmp al, [cs:khb_deep]
    jbe .khbnodeep
    mov [cs:khb_deep], al
    mov al, [sch_cur]
    mov [cs:khb_dcur], al
    mov ax, [ss:bx+KHB_IPOFF]   ; BX is still SP from the IP read above
    mov [cs:khb_dipl], al
    mov [cs:khb_diph], ah
    mov ax, [ss:bx+KHB_IPOFF+2]
    mov [cs:khb_dcsh], ah
.khbnodeep:
    mov al, 0
    cmp word [ss:STK0_BOT], SCH_MAGIC
    je .khbcan
    dec al                      ; 0xFF -> a SOLID 32px BLOCK: task 0's stack
.khbcan:                        ; has been through the floor
    mov [cs:khb_bad], al
    call khb_paint

    ; --- THE WATCHDOG: when it stops, WRITE IT DOWN IN HEX -------------------
    ; Four rounds of asking the field to photograph bit patterns and I could
    ; not read them reliably at the scale a screenshot arrives in. So once the
    ; machine is provably wedged, print the answer as TEXT and stop asking.
    ;
    ; The trigger is KHB_STUCK ticks with NO ui_task pass. It was the drawing
    ; mutex held continuously for the same span, and the field ran that for
    ; thirty seconds and printed nothing - which is the finding rather than a
    ; failure of the instrument: a loop that takes and releases the mutex
    ; every iteration is invisible to a held-lock timer however long it spins.
    ;
    ; Drawing from inside IRQ0 is not something the kernel may do. At the
    ; instant this fires the machine is PROBABLY never going to run again, so
    ; the rules it breaks have nobody left to protect - but "probably" is the
    ; operative word at a 30-second threshold, so everything it forces open is
    ; banked and put back below.
    mov ax, [khb_uipass]        ; HAS THE UI TASK COMPLETED A PASS? The old
    cmp ax, [khb_uilast]        ; trigger was "the mutex has been held N ticks"
    je .khbheld                 ; and the field never tripped it - because the
    mov [khb_uilast], ax        ; loop takes and releases it every iteration.
    mov word [khb_stuck], 0     ; A livelock is invisible to a held-lock timer
    jmp .khbout                 ; and obvious to a progress one (NEAR: the
                                ; report below put .khbout out of short range)
.khbheld:
    inc word [khb_stuck]
    cmp word [khb_stuck], KHB_STUCK
    jne .khbout                 ; ...and EXACTLY at the threshold, so the
                                ; report is printed once and never repaints
    mov si, khb_msg + KHB_M_CS  ; CS:IP - which routine it stopped in
    mov al, [khb_csh]
    call khb_hex
    mov si, khb_msg + KHB_M_IPH
    mov al, [khb_iph]
    call khb_hex
    mov si, khb_msg + KHB_M_IPL
    mov al, [khb_ipl]
    call khb_hex
    mov si, khb_msg + KHB_M_OWN ; who holds the mutex...
    mov al, [gfx_lock_own]
    call khb_hex
    mov si, khb_msg + KHB_M_CUR ; ...and who is running
    mov al, [sch_cur]
    call khb_hex
    mov si, khb_msg + KHB_M_SPH
    mov al, [khb_sph]
    call khb_hex
    mov si, khb_msg + KHB_M_SPL
    mov al, [khb_spl]
    call khb_hex
    mov bx, sp                  ; ...and three words of the interrupted code's
    mov cx, 3                   ; OWN stack: the return addresses that name the
    mov di, khb_msg + KHB_M_RET ; LOOP, where the IP only names its leaf
.khbret:
    mov ax, [ss:bx+KHB_IPOFF+6]
    push ax
    mov si, di                  ; khb_hex preserves AX, BX, CX and leaves SI
    mov al, ah                  ; alone, so the pair below is all this needs
    call khb_hex
    pop ax
    mov si, di
    inc si
    inc si
    call khb_hex
    add di, 5
    add bx, 2
    loop .khbret
    mov si, khb_msg + KHB_M_IMR ; the PIC's mask...
    mov al, [khb_imr]
    call khb_hex
    mov si, khb_msg + KHB_M_ISR ; ...what it has in service...
    mov al, [khb_pisr]
    call khb_hex
    mov si, khb_msg + KHB_M_FLG ; ...and whether the interrupted code was
    mov al, [khb_flg]           ; running with interrupts off (bit 1 here)
    call khb_hex
    mov si, khb_msg + KHB_M_BEAT ; ...and the pair, which HERE is always one
    mov al, [khb_beat]           ; apart by construction - this runs in the
    call khb_hex                 ; entry block, after the beat and before the
    mov si, khb_msg + KHB_M_CHAIN; chain. It is the STRIP, painted once on each
    mov al, [khb_chain]          ; side of the BIOS call, whose pair answers
    call khb_hex                 ; which side a stopped machine died on; these
                                 ; two only say that both counters are moving
    mov al, [gfx_lock_flag]     ; BANKED, not just forced: the trigger is 30
    mov ah, [wm_clip_n]         ; seconds without a ui_task pass and a healthy
    push ax                     ; machine really can spend that inside ONE
    mov al, [gfx_dis]           ; pass - ModPlug's opening W_PAINT is one - so
    mov ah, [gfx_color]         ; a fire that turns out to be spurious must not
    push ax                     ; leave the mutex open behind it and MAKE the
    mov byte [gfx_lock_flag], 0 ; freeze it was watching for. Forced open
    mov word [wm_clip_n], 0     ; because the primitives assume the caller
    mov byte [gfx_dis], 0       ; holds it, and a live clip would truncate us
    mov byte [gfx_color], CBLACK
    mov cx, 8
    mov dx, MBAR_H + 8
    mov si, khb_msg
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: the line lands on the desktop,
    call font_run               ; which is a 50% dither, and font_str is
                                ; transparent - the first one printed was a
                                ; photograph of black glyphs on a checkerboard
                                ; and could not be read at all
    pop ax
    mov [gfx_dis], al
    mov [gfx_color], ah
    pop ax
    mov [gfx_lock_flag], al
    mov [wm_clip_n], ah         ; the count is a word and its high byte cannot
    mov byte [wm_clip_n+1], 0   ; exceed WM_CLIP_MAX, so the byte is the whole
.khbout:
    popf
    pop es                      ; ...and the four the loop added, in order: an
    pop di                      ; instrument that unbalances the stack of the
    pop si                      ; TIMER ISR wedges the machine in the splash,
    pop dx                      ; which is what this did before it was counted
    pop cx
    pop bx
    pop ax
%endif
    inc word [sch_subs]         ; EVERY entry, sub-ticks included: fsx_wait's
                                ; frame clock waits on this (SPEC.md 53.5.1),
                                ; and it must count the interrupts the PIT
                                ; actually delivers rather than the ticks this
                                ; routine chooses to publish. Above the
                                ; sch_fast split for exactly that reason

    cmp byte [sch_fast], 0      ; the fsx sub-tick (SPEC.md 53.2.1). While it is
    je .full                    ; armed IRQ0 arrives N times a tick and only
    dec byte [sch_fcnt]         ; the Nth entry IS a tick: everything that
    jz .tick                    ; counts in ticks stays on that one, so
                                ; nothing outside this file can tell
    mov al, 0x20                ; ...and a sub-tick owes the EOI itself,
    out 0x20, al                ; because the BIOS handler that normally
                                ; sends it is only chained on the tick
    cmp byte [sch_lock], 0      ; the lock still bars a switch (a floppy
    jne sch_resume              ; transfer must not be pre-empted)
    cmp byte [sch_coop], 0      ; cooperative mode: only the watchdog
    jne sch_resume              ; switches, and it counts TICKS, not these
    jmp sch_switch
.tick:
    mov al, [sch_fast]
    mov [sch_fcnt], al
.full:
    ; --- the ROM's chain, on a stack of its own (SPEC.md 8.5) ---------------
    ; This one call is the largest single item in the interrupt floor, and it
    ; is charged to whichever TASK SLICE was interrupted - 50 bytes plus the 6
    ; of the pushf and the far call that reach it, and the ROM is not ours to
    ; shrink. SS never changes (SPEC.md 1/2.1), so moving it costs SP alone.
    ;
    ; Registers are already saved here, so unlike SPEC.md 9.10's swap this one
    ; needs no `cs:` override and a plain byte flag will do.
%ifndef NO_CHAINPRIV
    cmp byte [sch_chbusy], 0    ; NESTED? (SPEC.md 8.5.1) The ROM sends its EOI
    jne .chskip                 ; and then stis, so a second IRQ0 can re-enter
                                ; this ISR - and that one may not chain again
                                ; on the private stack (sch_chsave is one
                                ; word), and it is ALREADY on that stack, so
                                ; the switch below is refused for it too: a
                                ; task record must never hold a private SP.
                                ; Everything else that can land in that window
                                ; - int 09h, a mouse gate frame, the floppy's
                                ; completion - nests harmlessly, and the stack
                                ; is sized for it
    mov ax, ss
    cmp ax, LOW_SEG             ; **ONLY ON A TASK STACK** (SPEC.md 8.5). SS is
    jne .chplain                ; LOW_SEG for every os8088 context, but not
                                ; inside a ROM service that switched stacks -
                                ; and int 13h runs with IF=1, taking 21 of
                                ; these a transfer (15.3.8). SeaBIOS serves it
                                ; on an internal stack in a segment of its own,
                                ; so moving SP alone there put the chain at
                                ; SS:sch_chstack in the ROM's segment. The IBM
                                ; ROMs do not hop; where one does, the chain
                                ; runs in place as it did before 8.5
    inc byte [sch_chbusy]
    mov [sch_chsave], sp
    mov sp, sch_chstack + SCH_CHSTK
    pushf                       ; chain: BIOS ticks its count and sends EOI
    call far [sch_old08]
    mov sp, [sch_chsave]        ; ...and the ROM's iret restored our IF=0 with
    dec byte [sch_chbusy]       ; the flags the pushf banked, so this is back
    jmp short .chained          ; inside the ISR's own contract
.chplain:
    pushf                       ; not a task stack: chain where we stand
    call far [sch_old08]
    jmp short .chained
.chskip:
    inc word [sch_chskip]       ; the claim in SPEC.md 8.5.1 is a READING, not
                                ; an argument: STKDIAG publishes this
    mov al, 0x20                ; ...and a skipped chain owes the EOI itself,
    out 0x20, al                ; exactly as the sub-tick above does
.chained:
%else
    pushf                       ; chain: BIOS ticks its count and sends EOI
    call far [sch_old08]
%endif
%ifdef KFZTRACE
    inc byte [khb_chain]        ; THE CHAIN CAME BACK. khb_beat counts entries
    call khb_paint              ; ...and the strip is painted a SECOND time, or
                                ; the two counters say nothing: at the entry
                                ; paint they are one apart in a healthy tick and
                                ; in a dying one alike
                                ; to this ISR and this counts the BIOS handler
                                ; returning, so ONE photograph of a stopped
                                ; machine says which side of that call it died
                                ; on: equal means the interrupt completed and
                                ; whatever stopped the machine is in task code,
                                ; one apart means it never left the BIOS - and
                                ; the two were indistinguishable before, which
                                ; is why the field has been asked for a second
                                ; picture every round
%endif

    inc word [ticks]
    call sch_account            ; after the inc, so [ticks] and the freshly
                                ; reloaded counter agree (SPEC.md 8.1); and
                                ; before the sch_lock check, so accounting
                                ; runs every tick even during floppy reads.
                                ; It no-ops itself while the fsx sub-tick is
                                ; armed (SPEC.md 53.2.1)
%ifndef NO_CHAINPRIV
    cmp byte [sch_chbusy], 0    ; a NESTED tick is on sch_chstack (8.5.1):
    jne .nospl                  ; the accounting above and the wake scan
                                ; below fit there; the driver's tick and a
                                ; spinner frame - 58 bytes on top of the
                                ; chain's own 56 - do not, and there is no
                                ; canary under them
%endif
    call snd_tick               ; sound duties (SPEC.md 8.2/34): a called
                                ; leaf, gated on snd_live until snd_init
                                ; publishes; runs every tick in both modes,
                                ; floppy reads included

    ; --- the loading screen's animation (SPEC.md 15.3.8.2) -------------------
    ; snd_tick's shape one line down and for its reason: a gated leaf that runs
    ; every tick, floppy reads INCLUDED - which here is the whole point. This
    ; ISR owns int 08h from sched_init, which is early in kmain, so it drives
    ; the second half of the loading screen's life; spl_chrome's own hook drove
    ; the first, and stage 2 gave it back before the handover.
    ;
    ; [spl_busy] is 15.3.8.3's lock and this takes it exactly as spl_isr does.
    ; What it does NOT do is `sti` first, and that asymmetry is deliberate:
    ; this ISR's contract is IF=0 throughout (see the header), and re-entering
    ; the SCHEDULER is a worse thing than making the floppy's completion
    ; interrupt wait one frame. spl_isr can afford the sti because the only
    ; thing it can re-enter is itself, and [spl_busy] already refuses that.
    ; ...and NOT through SPLCALL, which stages its target in the [spl_fp] the
    ; main line is using: an interrupt that borrows that word sends whatever
    ; SPLCALL it landed inside to splf_anim. kernel.asm's [spl_ifp] is this
    ; ISR's own, and the two instructions either side are SPLCALL's own body -
    ; the blob-gone test and the far call.
    cmp byte [spl_live], 0
    je .nospl
    cmp byte [spl_quiet], 0         ; ...and spl_stepq has the floor (15.3.3.1).
    jne .nospl                      ; THIS handler is the one that matters for
                                    ; it: sched_init takes int 08h early in
                                    ; kmain, so it - not spl_isr - is what fires
                                    ; during mouse_init's identify window, and
                                    ; it does not `sti`. A 28.7 ms spinner frame
                                    ; with IF=0 loses three bytes of a 1200-baud
                                    ; burst the 8250 holds one at a time, which
                                    ; is a mouse on the wire and not detected
    cmp byte [spl_busy], 0
    jne .nospl
    cmp word [spl_fseg], COLD_SEG   ; the blob has been given back (2.9.5)
    jbe .nospl
    inc byte [spl_busy]
    mov word [spl_ifp], splf_anim
    mov ax, [spl_fseg]
    mov [spl_ifseg], ax
    call far [spl_ifp]
    dec byte [spl_busy]
.nospl:

    mov dx, [ticks]             ; wake sleepers whose deadline has passed
    mov bx, sch_tasks
    mov cx, MAX_TASKS
.wake:
    cmp byte [bx+T_STATE], 2
    jne .next
    mov ax, dx
    sub ax, [bx+T_WAKE]         ; wrap-safe: sign of the difference decides
    js .next                    ; still in the future
    mov byte [bx+T_STATE], 1
.next:
    add bx, T_SIZE
    loop .wake

%ifndef NO_CHAINPRIV
    cmp byte [sch_chbusy], 0    ; a NESTED tick is running on sch_chstack
    jne sch_resume              ; (8.5.1): it may account and wake, which fit
                                ; there, but it may not park a PRIVATE SP in a
                                ; task record - the outer tick's frame is the
                                ; interrupted task's and it is resumed below
%endif
    cmp byte [sch_lock], 0      ; locked: count ticks but do not switch
    jne sch_resume
    cmp byte [sch_coop], 0
    je .sw                      ; pre-emptive: switch every tick
    dec byte [sch_hold]         ; cooperative: only the watchdog switches.
    jnz sch_resume              ; DOWN, not up: the decrement's own ZF is the
                                ; test, so a second read and a compare go.
                                ; Same 18 ticks either way - every entry to
                                ; sch_switch re-arms the budget
    inc word [sch_wd_hits]      ; forced switch - diagnostic only
.sw:
    ; fall through into sch_switch

; -----------------------------------------------------------------------------
sch_switch:
    ; STKBALANCE-OK: the context switch. Its iret restores the NEXT task and
    ; returns onto that task's stack, so a depth measured against whatever
    ; entry walked in here is meaningless (SPEC.md 8). The walker stops on
    ; arrival at an exempted routine, so this one line also covers task_exit
    ; and inst_task_die, which both reach the switch by a tail jmp.
    mov ax, ss                  ; **ONLY ON A TASK STACK** - the chain swap's
    cmp ax, LOW_SEG             ; test (SPEC.md 8.5), for the same reason: a
    jne sch_resume              ; ROM that hops stacks for int 16h (SeaBIOS
                                ; does, for AH=01h, which ui_task polls with
                                ; sch_lock CLEAR) would have its SP parked in
                                ; the task record and task 0's canary read
                                ; through its segment - a false STACK OVERFLOW
                                ; panel, or a task resumed on a foreign SP.
                                ; Not switching this tick is the whole cost.
                                ; AX is free here: every entry arrives with
                                ; the full frame saved
    mov byte [sch_hold], SCH_WD_TICKS
                                ; the outgoing task reached the switch path,
                                ; so its watchdog budget is full again
    mov bl, [sch_cur]           ; park the outgoing task's SP
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov [bx+sch_tasks+T_SP], sp

    ; --- the outgoing task's canary (SPEC.md 8), through sch_stkbase above.
    ; SS is LOW_SEG for every task including task 0, so the read needs no
    ; segment set-up beyond the override; the table's own header says why
    ; there is no arithmetic here any more, and why slot 0 needs no branch.
    mov bl, [sch_cur]
    xor bh, bh
    shl bx, 1
    mov bx, [bx+sch_stkbase]
    cmp word [ss:bx], SCH_MAGIC
    jne sch_stkdie

    mov cl, 3                   ; T_SIZE = 8, so a slot index shifts left 3.
                                ; ***CL IS LIVE FROM HERE TO .pick*** - the
                                ; two `shl bx, cl` below share this one load,
                                ; which also takes it out of the up-to-8
                                ; iteration scan body. NOTHING ADDED BETWEEN
                                ; HERE AND .pick MAY WRITE CX: .scan would
                                ; then shift by a wrong count, .pick would
                                ; load SP from a mis-strided record and
                                ; sch_resume would iret into nothing. It is
                                ; set below the canary check and not above it
                                ; because that check is free to use CL. It no
                                ; longer does - sch_stkbase took the multiply
                                ; out - but the ordering is kept: it costs
                                ; nothing and it is one fewer thing to get
                                ; wrong if anything is ever put back there.
                                ; Past .pick, `call sch_account` clobbers CX
                                ; and nothing reads CL again.
    mov dl, [sch_cur]           ; scan cur+1, cur+2, ... (cur itself last)
    mov dh, MAX_TASKS
.scan:
    inc dl
    cmp dl, MAX_TASKS
    jne .nowrap
    xor dl, dl
.nowrap:
    mov bl, dl
    xor bh, bh
    shl bx, cl                  ; CL = 3, set once above the scan
    cmp byte [bx+sch_tasks+T_STATE], 1
    jne .skip
    cmp dl, [sch_idleslot]      ; the idle task is picked at the fallback
    je .skip                    ; below and never by the scan (6.2)
    cmp byte [fsx_task], 0xFF   ; the fsx freeze (SPEC.md 53.2): off means
    je .pick                    ; every ready task is eligible; armed, only
    cmp dl, [fsx_task]          ; the exclusive task itself...
    je .pick
    cmp dl, [fsx_worker]        ; ...its kept worker (FSXF_KEEPWORKER)...
    je .pick
    test byte [bx+sch_tasks+T_FLAGS], TF_SERVICE
    jnz .pick                   ; ...and a driver's service worker may run
.skip:
    dec dh
    jnz .scan
    mov dl, [sch_idleslot]      ; nothing ready: the idle task, if there is
    cmp dl, 0xFF                ; one (6.2). Without it - and before boot
    jne .idlepick               ; starts it - the old behaviour stands, which
    mov dl, [sch_cur]           ; is to resume the outgoing task
.idlepick:
    mov bl, dl
    xor bh, bh
    shl bx, cl                  ; CL = 3, set once above the scan
.pick:
    ; --- charge the OUTGOING task, and ONLY on a real change (SPEC.md 8.1.1)
    ; [sch_cur] still names the task that just ran, which is what sch_account
    ; bills - so the charge belongs above the store and not below it.
    ;
    ; The skip is EXACT rather than an approximation: sch_account's timestamp
    ; is good for an interval of any length, so a no-change switch that is not
    ; charged just leaves sch_pit_last where it is and the next charge bills
    ; the whole longer interval to the same task. Nothing is mis-attributed
    ; and no counter the Task Manager reads changes value. The interval cannot
    ; run away because sch_isr stamps every tick whatever happens here.
    ;
    ; DX and BX are pushed because sch_account clobbers AX/BX/CX/DX/SI and
    ; this is holding the picked index in DL and its record in BX. SP has
    ; already been parked into the outgoing record above, so the four bytes
    ; land in that task's slice below the parked SP - scratch nothing reads,
    ; and 4 bytes against SCH_STACK's 1.8x margin over the measured worst case.
    cmp dl, [sch_cur]
    je .same                    ; resuming the caller: nothing ended, so
    push dx                     ; nothing to charge
    push bx
    call sch_account
    pop bx
    pop dx
    mov [sch_cur], dl
.same:
    mov sp, [bx+sch_tasks+T_SP]
    ; fall through into sch_resume

; -----------------------------------------------------------------------------
; sch_resume - pop the saved frame of [sch_cur]'s stack and iret into it
; -----------------------------------------------------------------------------
sch_resume:
    pop ds
    pop es
    pop bp
    pop di
    pop si
    pop bx
    pop dx
    pop cx
    pop ax
    iret

; -----------------------------------------------------------------------------
; sch_stkdie - a task overran its stack slice (SPEC.md 8)
;
; Reached only from sch_switch's canary check, so the machine is already
; inside a switch with the outgoing frame parked. There is nothing safe to
; do: the overrun has already written into the slice BELOW this one, which is
; another task's stack, so every task table entry is now suspect and any
; attempt to report through the window manager would run on the corrupted
; stack that got us here.
;
; So it stops, loudly and reproducibly. A halted machine is a bug report; a
; machine that carries on is a bug that gets blamed on something else three
; subsystems away.
;
; IT IS NOT A CAN'T-HAPPEN PATH, and this comment said it was. It read "256
; bytes is 1.8x the measured worst case" - a sentence about a SCH_STACK that
; has since been 512 and is now 384, and about a margin the FIELD has since
; spent: docs/FIELD-NOTES.md 29.6 is an overrun of 256, which is what moved
; the constant. So the margin is a measurement that goes stale, not a proof,
; and this is a live check. Re-run the 0xCC fill probe (docs/KERNEL-MEMORY.md,
; tools/stkwater.py) if it ever hits, and read SCH_STACK's own header for what
; the deepest mark actually was.
; -----------------------------------------------------------------------------
sch_stkdie:
%ifdef KFZTRACE
    ; --- SAY SO (KFZ only) ---------------------------------------------------
    ; This is the only cli+hlt in the kernel, so it is the only thing that can
    ; stop the heartbeat - and a stopped heartbeat is exactly what the field
    ; reports. Silent, it is indistinguishable from a wild jump; one solid bar
    ; and a task number turn it into a diagnosis a photograph can carry.
    ; Registers are free: the next instruction halts for ever.
    push cs
    pop ds
    mov bx, [vid_rseg]
    or bx, bx
    jz .nomark                  ; no adapter yet: nothing to say it on
    mov es, bx
    xor dx, dx
    cld
.skbank:
    mov di, dx
    add di, KHB_STK
    mov cx, 8                   ; a 64px black bar: THE CANARY DIED
    xor al, al
    rep stosb
    inc di                      ; ...and one white cell. WHICH TASK is NOT
                                ; painted here: it is cell 3 of the strip
                                ; (sch_cur), the bar lands nowhere near it, and
                                ; nothing has switched between the last
                                ; heartbeat and this halt - so a second copy
                                ; would be a second thing to keep in step. The
                                ; first version put it in ONE framebuffer byte,
                                ; one pixel a bit, which is exactly the width
                                ; the heartbeat spends four pixels to avoid:
                                ; tools/kfzread.py read that pixel as 0 and as 1
                                ; depending on where the threshold fell
    add dx, 0x2000
    cmp dx, 0x8000
    jb .skbank
.nomark:
%endif
    cli
    call sch_diepanel           ; SPEC.md 8.8 - and if anything in there is
                                ; itself broken, the hang below is still the
                                ; behaviour this routine promises
.hang:
    hlt
    jmp short .hang

; -----------------------------------------------------------------------------
; sch_diepanel - WHAT DIED, on the glass (SPEC.md 8.8)
;
; in:  DS = KERNEL_SEG, SS = LOW_SEG, IF = 0, [sch_cur] = the slot that overran
; out: never usefully returns; every register clobbered
;
; The panel a stack overflow used to print was NOTHING AT ALL. `cli`/`hlt` is
; the correct thing to DO and a terrible thing to WATCH: the field reports "it
; froze", which is the same sentence a wild jump, a lost IRQ and a livelock all
; produce, and a session then spends its first hour deciding which of the four
; it is looking at. SPEC.md 8.8 is what these two lines save.
;
; WHY IT IS SAFE TO DRAW HERE, when the comment above sch_stkdie says nothing
; is. That comment is about the WINDOW MANAGER - `wm_paint_all` on the
; corrupted stack that got us here - and this takes none of that. It calls one
; primitive, `font_run`, which takes no lock, claims nothing, and reads only
; the video geometry and the glyph table, both settled since boot. What it
; needs from this routine is three things the dying machine cannot be trusted
; to have left it:
;
;   * **A STACK THAT IS NOT THE DEAD ONE.** SP is currently somewhere below the
;     dead slice's canary, scribbling on the slot underneath. So the evidence -
;     the slot, its parked SP, its instance - is banked into `.bss` FIRST, in
;     nine instructions on the stack we arrived on, and only then is SP moved.
;     It moves to task 0's, the largest in the machine and, at IF = 0 for ever,
;     owned by nobody: if task 0 is itself the corpse then its contents are
;     already banked and overwriting them costs the panel nothing.
;   * **NO ARMED CLIP REGION.** `font_run` honours one (SPEC.md 11.3), and a
;     package that died mid-paint left one armed - so the panel would be
;     clipped to the window of the program that has just killed the machine.
;     One store, and it is the difference between this working and this
;     working *except in the case it exists for*.
;   * **NOTHING ELSE.** No lock is taken and none is waited on: a lock is an
;     agreement between things that are still running, and nothing is.
;
; IT IS `font_run` AND NOT `gfx_fill` + `font_str`, for SPEC.md 6.6's reason
; and one of its own: the pair leaves the glyph TRANSPARENT over whatever the
; desktop had under it, and a panel nobody can read is worse than no panel at
; all. One pass puts a ground under the letters wherever the window happened
; to be - and it is the same primitive on all three adapters, which is what
; keeps this small enough to exist.
; -----------------------------------------------------------------------------
SDP_X       equ 8               ; hard left and under the menu bar, so it needs
SDP_Y       equ 24              ; no geometry: it fits 720, 640 and 640 alike.
                                ; ONE LINE, at 54 characters of the 80 the
                                ; narrowest adapter here has - two would cost a
                                ; row variable, its initialiser and its
                                ; advance, and this has a byte budget

sch_diepanel:
    mov bl, [sch_cur]           ; --- bank the evidence, still on the dead
    mov [sch_dpn], bl           ;     stack: after this nothing here reads a
    xor bh, bh                  ;     task record, so nothing here cares that
    mov cl, 3                   ;     the table is suspect
    shl bx, cl
    mov ax, [bx+sch_tasks+T_SP]
    mov [sch_dpsp], ax
    mov al, [bx+sch_tasks+T_INST]
    mov [sch_dpi], al

    mov sp, STK0_TOP            ; ...and OFF it (see the header)
    mov word [wm_clip_n], 0     ; nothing may clip the panel away
    mov word [sch_dpcol], SDP_X

    mov si, sch_s_over          ; WHAT happened, WHICH task, WHOSE, and where
    call sch_dpstr              ; its SP was. The slice's BASE and SIZE are
    mov al, [sch_dpn]           ; deliberately not here: they are
    call sch_dphex2             ; sch_stkbase[n] and sch_stksize[n] in the very
    mov si, sch_s_sp            ; binary the reader has open, and the slot
    call sch_dpstr              ; number is the whole of what it takes to look
    mov ax, [sch_dpsp]          ; them up
    call sch_dphex4
    mov si, sch_s_in
    call sch_dpstr
    mov al, [sch_dpi]
    cmp al, 0xFF                ; the UI task owns no instance record
    je .ui
    xor ah, ah
    mov cl, 5                   ; I_RECSZ = 32
    shl ax, cl
    add ax, inst_tab + I_NAME
    xchg ax, si
    jmp short .name
.ui:
    mov si, sch_s_ui
.name:
    jmp short sch_dpstr         ; its `ret` is ours

; sch_dpstr - the ASCIIZ at DS:SI, at the pen, which then advances past it
sch_dpstr:
    mov cx, [sch_dpcol]
    mov dx, SDP_Y
    mov ax, 0x0F00              ; AL = 0 ink on AH = 15 ground: BLACK ON WHITE,
                                ; which is the one pair that reads on all three
                                ; adapters and on any desktop pattern under it
    push ds
    pop es                      ; font_run_x takes the string at ES:SI
    call font_run
.adv:
    lodsb                       ; the pen moves by what was actually drawn, so
    or al, al                   ; no caller ever counts a string's length
    jz .out
    add word [sch_dpcol], 8
    jmp short .adv
.out:
    ret

; sch_dphex4 / sch_dphex2 - AX / AL as hex at the pen, most significant first
sch_dphex4:
    push ax
    mov al, ah
    call sch_dphex2
    pop ax
sch_dphex2:
    push ax
    mov cl, 4
    shr al, cl
    call .nib
    pop ax
.nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '0' - 10
.put:
    mov [sch_dphc], al          ; one digit as a one-character STRING, so the
    mov si, sch_dphc            ; hex path and the text path are the same two
    jmp short sch_dpstr         ; lines of drawing code and not two copies

sch_s_over: db 'STACK OVERFLOW  TASK ', 0   ; grep the tree for this sentence
sch_s_sp:   db '  SP ', 0                ; and SPEC.md 8.8 is the first hit
sch_s_in:   db '  ', 0
sch_s_ui:   db 'UI TASK', 0

; -----------------------------------------------------------------------------
; sch_stkbase - where slot n's stack BOTTOM is, one word per slot
;
; The canary word lives at the bottom of every stack (SCH_MAGIC), so this is
; both "the base of the slice" and "where the canary is", and every consumer
; wants one or the other.
;
; IT REPLACES ARITHMETIC, and that is the point rather than a side effect.
; sch_switch used to DERIVE the base from the slot index, which needed a
; multiply in the general case and carried two hand-written special cases
; beside it - one for SCH_STACK = 256 that had not shipped since the 8 x 384
; move, and one for 384 that decomposed it into two shifts. The comment on
; that block records the failure mode the whole time it existed: two
; instruments derived the base a DIFFERENT wrong way (from SP's low bits) and
; read garbage, because a derivation is something every reader has to get
; right independently and a table is something they look up.
;
; A DERIVATION ALSO CANNOT SURVIVE VARIABLE SIZES, which is what
; docs/STACK-SLOTS-PLAN.md 6 needs: with classes there is no expression from
; the slot index to the base at all. So the table is the enabling mechanism as
; well as the simplification, and both of those were true before any class
; existed.
;
; IT SITS BELOW sch_stkdie's HANG LOOP, and that placement is load-bearing.
; The obvious home for it is beside sch_switch, which is where it went first -
; and sch_isr FALLS THROUGH into sch_switch, so sixteen bytes of `dw` placed
; there are sixteen bytes the TIMER ISR EXECUTES on every tick. It cost an hour
; and every symptom pointed elsewhere: kern_big survived it, kern_small booted
; to a task resuming with CS = 0xCCCC (task_spawn's own fill, SPEC.md 8.3), and
; a bisect that reverted every line of logic still failed - because the failure
; was the DATA, not the code. The control that named it was padding a different
; module by the same sixteen bytes, which changed nothing. A `hlt` / `jmp $` is
; the one place in this file nothing can fall through.
;
; IT IS IN .text WITH REAL INITIALISERS, not .bss with a loop. The partition is
; fixed at assembly time - the slices exist from boot and are never handed out
; or given back (STACK-SLOTS-PLAN 6.2) - so a table of `dw`s costs 2 bytes a
; slot and no init code at all, where .bss would cost the same 2 bytes plus the
; loop that fills them. It cannot be corrupted either, which for the word that
; says where a canary is worth having.
;
; SLOT 0 IS IN IT. Task 0 runs on SS:STK0_TOP rather than a slice of
; sch_stacks, and the old arithmetic had to branch around it (`or bl, bl / jz
; .nocanary`). A table just holds STK0_BOT there - which sched_init already
; seeds with SCH_MAGIC - so the branch goes AND task 0's canary is now checked
; on every switch, where before only a KFZ build ever looked at it.
; -----------------------------------------------------------------------------
sch_stkbase:
%macro SCH_SLOT 1
    dw sch_stacks + sch_stk_off
    %assign sch_stk_off sch_stk_off + (%1)
%endmacro
    dw STK0_BOT                 ; slot 0: the UI task, above .lowbss
%assign sch_stk_off 0
    SCH_PARTITION               ; ...and slots 1 up, in SCH_PARTITION's order
%unmacro SCH_SLOT 1

; -----------------------------------------------------------------------------
; sch_stksize - and how BIG it is, one word per slot, in bytes
;
; The other half of sch_stkbase, from the same list, and it exists because a
; base alone stopped being enough the moment the slices stopped being the same
; size. Everything that wants a slice's TOP - task_spawn's frame carve, its
; 0xCC fill, the KFZ deep sampler, the STKDIAG panel - wants base + size, and
; every one of them used to write `+ SCH_STACK` and be right by the luck of
; uniformity.
;
; A WORD AND NOT A BYTE-IN-UNITS-OF-64, which was tried first and cost more
; than the 14 bytes it saved: every consumer had to shift, and a shift by 6
; wants CL, which is a register task_spawn does not have spare. Indexed by
; slot*2 it shares sch_stkbase's own index, so the pair costs one `shl` between
; them and the scan compares a request against a slot in one instruction.
;
; Slot 0 is in it for sch_stkbase's reason: a table with a hole in it is a
; table every reader has to remember the hole in.
; -----------------------------------------------------------------------------
sch_stksize:
%macro SCH_SLOT 1
    dw %1
%endmacro
    dw STK0_SIZE                ; slot 0: the UI task's 1,024
    SCH_PARTITION
%unmacro SCH_SLOT 1

; -----------------------------------------------------------------------------
; sch_clsbytes - the CLASSES a package may ask for, by index (SPEC.md 8.7)
;
; A package declares one byte in its header (LD_H_CLASS) and this is what that
; byte means. An INDEX rather than the size itself, for two reasons: a byte
; cannot hold 384, and an index makes the set of legal answers a closed list
; the kernel owns - so a package cannot ask for 200 and be given a slice that
; does not exist, and the list can be re-cut without every package being wrong.
;
; INDEX 0 IS "NO OPINION" AND MEANS THE LARGEST, which is what makes the field
; free to adopt: every package built before it has 0 there, and 0 gets exactly
; the stack it always had.
; -----------------------------------------------------------------------------
sch_clsbytes:
    dw SCH_STACK                ; 0: unspecified - the largest class
    dw 128                      ; 1: a sleeper. The idle task, a C package
    dw 192                      ;    whose worker only sleeps, a Timer, a
    dw 256                      ;    Bounce, a driver's stream task
    dw SCH_STACK                ; 4: the largest, asked for on purpose
SCH_NCLASS  equ ($ - sch_clsbytes) / 2

; -----------------------------------------------------------------------------
; sch_switch - common switch path (module-internal)
;
; Entered with IF=0, DS=KERNEL_SEG and a complete saved frame on the current
; stack (from sch_isr or task_yield's save stub). Parks SP in the current
; record, picks the next ready task round-robin (falling back to the current
; task if nothing is ready), and resumes it.
;
; RE-ARMING sch_hold is the FIRST thing done here, and deliberately on the
; routine's single entry point rather than at each caller: the counter means
; "ticks of watchdog budget left since the running task last reached the switch
; path", so every one of the three ways in - fall-through from sch_isr, the
; explicit jmp from task_yield's save path, the explicit jmp from task_exit -
; must re-arm it (SPEC.md 8.2). Hence a task that yields voluntarily can never
; trip the watchdog however long it runs in total; the "nothing ready, resume
; the outgoing task" fallback below re-arms it too, so an idle single-task
; system manufactures no watchdog hits; and a dying task re-arms it for its
; successor.

; -----------------------------------------------------------------------------

; =============================================================================
; scheduler state
; =============================================================================
section .lowbss                         ; LOW_SEG, reached through SS (SPEC.md 2.1)

resb ($-$$) % 2                         ; keep the BLOCK on a WORD boundary,
                                        ; and a word is the whole of what it
                                        ; needs (alignb spelled out - NASM
                                        ; refuses the macro form against this
                                        ; nobits section). It was 256 for
                                        ; twelve months and nothing ever asked
                                        ; for 256: with SCH_STACK = 384 an
                                        ; individual slice is 128-aligned
                                        ; anyway, so nothing may derive a
                                        ; slice base from SP's low bits, and
                                        ; every consumer works from
                                        ; sch_stacks + slot * SCH_STACK -
                                        ; sch_switch's canary arm, task_spawn's
                                        ; fill, tests/stackprobe (whose own
                                        ; comment records that the high-byte
                                        ; trick WAS the bug), tools/stkwater.py,
                                        ; tools/kfzread.py and tests/ftpd.py.
                                        ; WORD is required and is required by
                                        ; two: the canary is a word written at
                                        ; the slice base, and task_spawn fills
                                        ; the slice with `rep stosw`. SCH_STACK
                                        ; is even (the %error beside that fill
                                        ; says why), so an even BLOCK base makes
                                        ; every slice base even.
                                        ; It cost 42 bytes of .lowbss on
                                        ; kern_big and 0 on kern_small, which is
                                        ; the tightest rung in the kernel.
%ifndef NO_CHAINPRIV
sch_chstack: resb SCH_CHSTK             ; the ROM int 08h chain's own stack
                                        ; (SPEC.md 8.5). .lowbss and not .bss
                                        ; because SS is LOW_SEG for every task
                                        ; and the swap moves SP alone. It is
                                        ; declared ABOVE sch_stacks' alignment
                                        ; guard on purpose: SCH_CHSTK is even,
                                        ; so it cannot disturb it, and the
                                        ; %error below is what proves that on
                                        ; every build rather than on this one
%endif
sch_stacks: resb SCH_STK_TOTAL          ; slots 1..MAX_TASKS-1, one slice per
                                        ; SCH_SLOT line of SCH_PARTITION in that
                                        ; order; a slice's bottom is
                                        ; sch_stkbase[n] and its size
                                        ; sch_stksize[n] - nothing derives
                                        ; either (SPEC.md 8.6)
                                        ; of .lowbss (font_glyphs precedes
                                        ; it). Slot 0 = the UI task runs on
                                        ; the task-0 stack, SS:STK0_TOP,
                                        ; which grows down from above this
                                        ; section - it needs no reservation
%if (sch_stacks - $$) % 2
  %error "sch_stacks must be WORD-aligned in .lowbss: sch_switch's canary is a word written at the slice base and task_spawn fills the slice with `rep stosw`. The pad above this label is what guarantees it - do not delete it, and do not put an odd `resb` between it and this label"
%endif

section .bss

sch_tasks:  resb MAX_TASKS * T_SIZE     ; task records (layout above)
sch_old08:  resb 4                      ; saved BIOS int 08h vector (off:seg)
sch_dpcol:  resw 1                      ; the death panel's pen x (SPEC.md
                                        ; 8.8); its y is the constant SDP_Y
sch_dpsp:   resw 1                      ; ...and the evidence it banks off the
sch_dpn:    resb 1                      ; dead stack before it leaves it: the
sch_dpi:    resb 1                      ; parked SP, the slot and its instance
sch_dphc:   resb 2                      ; one hex digit, as an ASCIIZ string
%ifndef NO_CHAINPRIV
sch_chsave: resw 1                      ; the task's SP across the chain...
sch_chskip: resw 1                      ; ...how many chains a nested tick has
                                        ; skipped (SPEC.md 8.5.1), so the
                                        ; "one transfer in 280" claim is a
                                        ; reading. STKDIAG publishes it
sch_chbusy: resb 1                      ; ...and non-zero while the ROM has it
%endif
ticks:      resw 1                      ; public tick count, wraps at 65536
sch_cur:    resb 1                      ; index of the running task. ALWAYS a
                                        ; live slot, 0..MAX_TASKS-1: 0xFF is
                                        ; NOT available here as a "nothing
                                        ; running" sentinel, because fsx_mine
                                        ; reads this byte and the one below as
                                        ; ONE WORD and 0xFF is fsx_task's OFF
                                        ; value - two sentinels would compare
                                        ; equal and grant a bracket to nobody.
                                        ; fsx_mine refuses on the RANGE for
                                        ; that reason, so the failure is now a
                                        ; refusal rather than a mode set; this
                                        ; line is why the range test is there
fsx_task:   resb 1                      ; the fsx freeze (SPEC.md 53.2):
                                        ; 0xFF off, else the exclusive
                                        ; task's slot. Scheduler state
                                        ; because sch_switch reads it every
                                        ; scan; fsx_run/fsx_restore write
                                        ; it, sched_init stores the OFF
                                        ; value (0 would mean task 0)
fsx_worker: resb 1                      ; the kept worker's slot, 0xFF none
sch_lock:   resb 1                      ; non-zero: ISR counts but won't switch
sch_cycles: resw MAX_TASKS * 2          ; per-task CPU time, PIT cycles, dword
                                        ; each (lo word first); readers snap-
                                        ; shot under pushf/cli (SPEC.md 8.1)
sch_pit_last: resw 2                    ; last timestamp: phase, then ticks
sch_fast:   resb 1                      ; fsx sub-tick (SPEC.md 53.2.1): 0 off,
sch_fcnt:   resb 1                      ; else N, with this counting down
sch_subs:   resw 1                      ; free-running IRQ0 entries, SUB-TICKS
                                        ; included (SPEC.md 53.5.1). The
                                        ; finest clock a task can WAIT on:
                                        ; [ticks] moves 18.2 times a second
                                        ; whatever sch_fast is, and this moves
                                        ; every time the PIT does. Compared
                                        ; for CHANGE only, so the value it
                                        ; starts at does not matter and the
                                        ; wrap at 65536 is not a case
sch_coop:   resb 1                      ; mode (SPEC.md 8.2): 0 pre-emptive,
                                        ; 1 cooperative; sched_mode_set/_get
sch_hold:   resb 1                      ; ticks of watchdog budget LEFT to the
                                        ; running task: armed to SCH_WD_TICKS
                                        ; on every entry to sch_switch and
                                        ; counted DOWN, only while sch_coop is
                                        ; set and sch_lock clear. The
                                        ; decrement's own ZF is the test, so
                                        ; there is no second read and no
                                        ; compare. The only 0 it holds is the
                                        ; transient one that falls straight
                                        ; into sch_switch and re-arms -
                                        ; sched_init arms it BEFORE it hooks
                                        ; int 08h, so the ISR never sees
                                        ; .bss's zero, which here would wrap
                                        ; to 255 rather than count as 1
sch_uiwake: resb 1                      ; an ISR woke the UI task - or a saver
                                        ; frame came back already due, which
                                        ; is the same statement from inside
                                        ; the pass (SPEC.md 8.1.2.4); spent at
                                        ; ui_task's .idle (SPEC.md 8.1.2.3)
sch_idlewake: resb 1                    ; the same wake, for the idle task's
                                        ; halt guard; spent at sch_idle_body's
                                        ; .rearm (SPEC.md 8.1.2)
sch_idlespin: resb 1                    ; non-zero: IRQ0 was masked at boot, so
                                        ; the idle task spins instead of
                                        ; halting (SPEC.md 8.1.2.1)
sch_idleslot: resb 1                    ; the idle task's slot, or 0xFF for
                                        ; none - sched_init sets it, because
                                        ; a zero here names slot 0, the UI
                                        ; task, and the scan would skip it
sch_wd_hits: resw 1                     ; watchdog-forced switches; diagnostic
                                        ; only (QMP xp on segment 0x0800),
                                        ; wraps mod 65536, never reset

; sch_wake_ui sets BOTH wake bytes with one `mov word [sch_uiwake], 0x0101`.
; The labels cannot be folded at the store's own line, so the guard lives here,
; after the declarations.
%if sch_idlewake - sch_uiwake != 1
  %error "sch_wake_ui sets both wake bytes with ONE word store (SPEC.md 8.1.2) - keep sch_uiwake and sch_idlewake adjacent, in that order"
%endif

section .text
