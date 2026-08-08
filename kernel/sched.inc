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
MAX_TASKS   equ 12              ; mirrored in apps/os88api.inc, which sizes
                                ; every package's snapshot buffer from it
                                ; (SPEC.md 20.9) - change them together and
                                ; rebuild every .o88
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

SCH_STACK   equ 256             ; per-task stack bytes. It was 1,536 when the
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
                                ; It must stay a power of two: sch_switch
                                ; turns the slot index into the slice's base
                                ; with a byte swap rather than a multiply,
                                ; which is what makes the canary check nearly
                                ; free on the switch path
%if SCH_STACK != 256
  %error "sch_switch's canary check assumes SCH_STACK = 256 (a byte swap)"
%endif
SCH_MAGIC   equ 0x5A57          ; the word at the BOTTOM of every slice. A
                                ; stack that reaches it has overrun, and the
                                ; next push corrupts the slice below - which
                                ; is another task's, so the symptom would be
                                ; arbitrary and nowhere near the cause
SCH_FRAME   equ 24              ; saved-frame size: 9 regs + IP/CS/FLAGS

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
; -----------------------------------------------------------------------------
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
    mov byte [sch_hold], 0      ; otherwise (SPEC.md 8.2). All three are
    mov word [sch_wd_hits], 0   ; zeroed HERE by hand: nothing clears .bss at
                                ; boot, so an uninitialised sch_coop would
                                ; boot into a random mode
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
    add bx, 2
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
    ret

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
    mov al, 0x00                ; ...and re-seed the accounting stamp, or the
    out 0x43, al                ; first full tick after this charges a whole
    in al, 0x40                 ; bracket's worth of garbage to somebody
    mov cl, al
    in al, 0x40
    mov ch, al
    neg cx
    mov [sch_pit_last], cx
    mov ax, [ticks]
    mov [sch_pit_last+2], ax
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
; task_spawn - create a task that iret-launches into a near entry point
;
; Builds a complete saved frame at the top of the slot's stack: DX = the
; spawn argument, other GP regs zero, DS = ES = KERNEL_SEG, CS = KERNEL_SEG,
; IP = entry, FLAGS with IF=1. The slot's sch_cycles counter is zeroed
; before the publish (the ISR only charges the RUNNING slot, so a
; pre-publish write cannot race - SPEC.md 8.1). The scan starts at slot 1:
; slot 0 is the UI task, never free, and runs on the boot stack.
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
    pushf                       ; the caller's IF, banked across the window
    cli
    or bx, bx                   ; BX = 0 means the kernel's own segment
    jnz .haveseg
    mov bx, KERNEL_SEG
.haveseg:
    mov [sch_newseg], bx        ; banked: BX is the scan pointer below, and
                                ; this whole routine runs at IF=0 so no
                                ; second spawner can be in here with us

    mov bx, sch_tasks + T_SIZE  ; scan for a free slot, from slot 1
    mov cx, 1                   ; CX = slot index
.find:
    cmp byte [bx+T_STATE], 0
    je .found
    add bx, T_SIZE
    inc cx
    cmp cx, MAX_TASKS
    jb .find
    xor ax, ax                  ; table full: AL = 0, which is never a valid
    jmp .done                   ; slot (slot 0 is the UI task) - .done turns
                                ; it back into CF = 1 after the popf

.found:
    mov si, ax                  ; SI = entry (AX becomes scratch)
    mov ax, cx                  ; stack top = sch_stacks + slot*SCH_STACK
    mov di, SCH_STACK           ; (slot 1 owns the FIRST slice; slot 0 has no
    push dx                     ; slice - it runs on the task-0 stack)
    mul di                      ; DX:AX = AX * SCH_STACK (high half discarded)
    pop dx
    add ax, sch_stacks
    push ax                     ; seed this slice's canary before the frame:
    sub ax, SCH_STACK           ; AX is the slice TOP, so its base is one
    mov di, ax                  ; SCH_STACK below
    mov ax, LOW_SEG
    mov es, ax
    mov word [es:di], SCH_MAGIC
    pop ax
    sub ax, SCH_FRAME           ; carve the fake frame at the very top
    mov di, ax
    mov [bx+T_SP], di

    mov ax, LOW_SEG             ; the frame lives in LOW_SEG, so it is built
    mov es, ax                  ; through ES - DS cannot reach it. AX is dead
                                ; here: DI already carries the frame address
                                ; and the xor below is its next use. CX IS
                                ; NOT DEAD - it is the slot index, and it is
                                ; still needed for the cycle counter and the
                                ; return value below
    mov ax, [sch_newseg]
    mov [es:di], ax                ; DS slot = the task's own segment
    mov word [es:di+2], KERNEL_SEG ; ES slot: the kernel, always (SPEC 20.1)
    xor ax, ax
    mov [es:di+4], ax           ; BP
    mov [es:di+6], ax           ; DI
    mov [es:di+8], ax           ; SI
    mov [es:di+10], ax          ; BX
    mov [es:di+12], dx          ; DX = the spawn argument
    mov [es:di+14], ax          ; CX
    mov [es:di+16], ax          ; AX
    mov [es:di+18], si          ; IP = entry
    mov ax, [sch_newseg]
    mov [es:di+20], ax          ; CS = the task's own segment
    mov word [es:di+22], 0x0202 ; FLAGS: IF=1, reserved bit 1 set
    mov [bx+T_INST], dl         ; owning instance (0xFF = none)
    mov byte [bx+T_FLAGS], 0    ; TF_SERVICE is the driver cell's to set
                                ; (SPEC.md 53.2), never inherited

    mov ax, cx                  ; zero the slot's cycle counter before the
    shl ax, 1                   ; publish, so a reused slot never inherits
    shl ax, 1                   ; a dead task's total (SPEC.md 8.1)
    mov di, ax
    mov word [di+sch_cycles], 0
    mov word [di+sch_cycles+2], 0

    mov byte [bx+T_STATE], 1    ; publish - MUST be the last record write
    mov ax, cx                  ; AL = slot index (1..MAX_TASKS-1)

.done:
    popf                        ; IF back - and the caller's CF with it, so
    cmp al, 1                   ; the answer is re-derived from AL here:
                                ; AL = 0 -> CF = 1 (table full), else CF = 0.
                                ; The pops below touch no flags.
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
    mov ax, KERNEL_SEG          ; caller's DS is on the stack; we need ours
    mov ds, ax
    call sch_account            ; charge the slice that just ended (IF=0)
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
task_sleep:
    push ax
    push bx
    push cx

    mov bl, [sch_cur]           ; BX = current task record
    xor bh, bh
    mov cl, 3
    shl bx, cl
    add ax, [ticks]
    mov [bx+sch_tasks+T_WAKE], ax       ; deadline first...
    mov byte [bx+sch_tasks+T_STATE], 2  ; ...then state (both stores atomic)
    call task_yield             ; switch away; we resume once state -> 1

    pop cx
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
; the resume-the-outgoing-task fallback cannot pick a dead task because it
; is only reachable when NOTHING is ready, and task 0 (the UI task) never
; sleeps and never exits. Because the release byte and T_STATE are both
; cleared inside one IF=0 window, an instance slot and its task slot free
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
    mov bl, [sch_cur]           ; free our own task record
    xor bh, bh
    mov cl, 3
    shl bx, cl
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
; clobbers: AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
sch_account:
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
    mov cl, [sch_cur]           ; sch_cycles[sch_cur] += elapsed
    xor ch, ch
    shl cx, 1
    shl cx, 1
    mov si, cx
    add [si+sch_cycles], ax
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
    call sch_account
    mov cl, [sch_cur]
    xor ch, ch
    shl cx, 1
    shl cx, 1
    mov si, cx
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
    call sch_account
    mov cl, [sch_cur]
    xor ch, ch
    shl cx, 1
    shl cx, 1
    mov si, cx
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
    mov byte [sch_hold], 0      ; the incoming mode starts with a full budget
    popf
    pop ax
    ret

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
sched_mode_get:
    mov al, [sch_coop]
    ret

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
    mov ax, KERNEL_SEG
    mov ds, ax

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
    pushf                       ; chain: BIOS ticks its count and sends EOI
    call far [sch_old08]

    inc word [ticks]
    call sch_account            ; after the inc, so [ticks] and the freshly
                                ; reloaded counter agree (SPEC.md 8.1); and
                                ; before the sch_lock check, so accounting
                                ; runs every tick even during floppy reads.
                                ; It no-ops itself while the fsx sub-tick is
                                ; armed (SPEC.md 53.2.1)
    call snd_tick               ; sound duties (SPEC.md 8.2/34): a called
                                ; leaf, gated on snd_live until snd_init
                                ; publishes; runs every tick in both modes,
                                ; floppy reads included

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

    cmp byte [sch_lock], 0      ; locked: count ticks but do not switch
    jne sch_resume
    cmp byte [sch_coop], 0
    je .sw                      ; pre-emptive: switch every tick
    inc byte [sch_hold]         ; cooperative: only the watchdog switches
    cmp byte [sch_hold], SCH_WD_TICKS
    jb sch_resume
    inc word [sch_wd_hits]      ; forced switch - diagnostic only
.sw:
    ; fall through into sch_switch

; -----------------------------------------------------------------------------
; sch_switch - common switch path (module-internal)
;
; Entered with IF=0, DS=KERNEL_SEG and a complete saved frame on the current
; stack (from sch_isr or task_yield's save stub). Parks SP in the current
; record, picks the next ready task round-robin (falling back to the current
; task if nothing is ready), and resumes it.
;
; Clearing sch_hold is the FIRST thing done here, and deliberately on the
; routine's single entry point rather than at each caller: the counter means
; "ticks since the running task last reached the switch path", so every one of
; the three ways in - fall-through from sch_isr, the explicit jmp from
; task_yield's save path, the explicit jmp from task_exit - must clear it
; (SPEC.md 8.2). Hence a task that yields voluntarily can never trip the
; watchdog however long it runs in total; the "nothing ready, resume the
; outgoing task" fallback below clears it too, so an idle single-task system
; manufactures no watchdog hits; and a dying task clears it for its successor.
; -----------------------------------------------------------------------------
sch_switch:
    mov byte [sch_hold], 0      ; the outgoing task reached the switch path
    mov bl, [sch_cur]           ; park the outgoing task's SP
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov [bx+sch_tasks+T_SP], sp

    ; --- the outgoing task's canary (SPEC.md 8): slot n's slice starts at
    ; sch_stacks + (n-1)*SCH_STACK, and with SCH_STACK = 256 the offset part
    ; is the slot index in the HIGH byte and nothing else - no multiply on
    ; the switch path. Task 0 owns no slice of sch_stacks and is skipped. SS
    ; is LOW_SEG, so the read needs no segment set-up. sch_stacks is NOT at
    ; .lowbss offset 0 (font's glyph block precedes it); the math here is
    ; symbol-relative and does not care, but the alignb at the declaration
    ; keeps the slices 256-ALIGNED, which SP-only arithmetic (tests/
    ; stackprobe) does depend on.
    mov bl, [sch_cur]
    or bl, bl
    jz .nocanary                ; task 0: its stack is above .lowbss, not in it
    dec bl
    mov bh, bl                  ; BX = (cur-1) * 256
    xor bl, bl
    cmp word [ss:bx+sch_stacks], SCH_MAGIC
    jne sch_stkdie
.nocanary:

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
    mov cl, 3
    shl bx, cl
    cmp byte [bx+sch_tasks+T_STATE], 1
    jne .skip
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
    mov dl, [sch_cur]           ; nothing ready (task 0 never sleeps, so
    mov bl, dl                  ; only reachable if every task slept):
    xor bh, bh                  ; resume the outgoing task
    mov cl, 3
    shl bx, cl
.pick:
    mov [sch_cur], dl
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
; subsystems away. This is a can't-happen path - 256 bytes is 1.8x the
; measured worst case - and it exists so that "can't" is checked rather than
; assumed. Re-run the 0xCC fill probe (docs/KERNEL-MEMORY.md) if it ever hits.
; -----------------------------------------------------------------------------
sch_stkdie:
    cli
.hang:
    hlt
    jmp short .hang

; -----------------------------------------------------------------------------

; =============================================================================
; scheduler state
; =============================================================================
section .lowbss                         ; LOW_SEG, reached through SS (SPEC.md 2.1)

resb (256 - (($-$$) % 256)) % 256       ; keep every slice on a 256-byte
                                        ; boundary (alignb spelled out - NASM
                                        ; refuses the macro form against this
                                        ; nobits section). The canary math
                                        ; above is symbol-relative and does
                                        ; not need this, but tests/stackprobe
                                        ; derives its slice top from SP
                                        ; alone, and anything else doing SP
                                        ; arithmetic will assume it too. Free
                                        ; today: font's 768-byte block
                                        ; precedes this one and 768 is
                                        ; already a multiple
sch_stacks: resb (MAX_TASKS-1) * SCH_STACK ; slots 1..11 only: slot n's stack
                                        ; top at n*SCH_STACK relative to
                                        ; sch_stacks, which is NOT offset 0
                                        ; of .lowbss (font_glyphs precedes
                                        ; it). Slot 0 = the UI task runs on
                                        ; the task-0 stack, SS:STK0_TOP,
                                        ; which grows down from above this
                                        ; section - it needs no reservation

section .bss

sch_tasks:  resb MAX_TASKS * T_SIZE     ; task records (layout above)
sch_old08:  resb 4                      ; saved BIOS int 08h vector (off:seg)
ticks:      resw 1                      ; public tick count, wraps at 65536
sch_cur:    resb 1                      ; index of the running task
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
sch_hold:   resb 1                      ; ticks the running task has held the
                                        ; CPU since the last entry to
                                        ; sch_switch; only counts up while
                                        ; sch_coop is set and sch_lock clear,
                                        ; and is compared against
                                        ; SCH_WD_TICKS (18) on the tick that
                                        ; incremented it, so it cannot pass 18
sch_newseg: resw 1                      ; task_spawn scratch: the code segment
                                        ; of the task being built (IF=0, so
                                        ; it cannot race a second spawner)
sch_wd_hits: resw 1                     ; watchdog-forced switches; diagnostic
                                        ; only (QMP xp on segment 0x0800),
                                        ; wraps mod 65536, never reset

section .text
