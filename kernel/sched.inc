; =============================================================================
; jop - sched.inc
;
; Pre-emptive round-robin scheduler off the PIT (SPEC.md 8). Up to MAX_TASKS
; tasks; task 0 is the boot thread (the UI task) and runs on the boot stack
; (SS:0xFFFE) — it owns no slice of sch_stacks. Slots 1..MAX_TASKS-1 are
; dynamic (task_spawn / task_exit), each with a 1536-byte stack in .bss.
; All tasks run tiny-model: CS = DS = SS = KERNEL_SEG, so a context
; switch is nothing more than swapping SP between stacks that share one
; segment.
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
MAX_TASKS   equ 12
T_STATE     equ 0               ; byte: 0 free, 1 ready, 2 sleeping
T_SP        equ 2               ; word: saved SP (frame above)
T_WAKE      equ 4               ; word: [ticks] value to wake at
T_INST      equ 6               ; byte: owning instance index (SPEC.md 29),
                                ; 0xFF = none (the UI task)
T_SIZE      equ 8               ; record stride (1 byte padding)

SCH_STACK   equ 1536            ; per-task stack bytes
SCH_FRAME   equ 24              ; saved-frame size: 9 regs + IP/CS/FLAGS

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
    mov byte [sch_tasks+T_STATE], 1     ; task 0 = this thread, ready
    mov byte [sch_tasks+T_INST], 0xFF   ; the UI task owns no instance

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
; sched_unhook - restore the BIOS int 08h vector, then unhook the mouse
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
; IP = entry, FLAGS with IF=1. The timer ISR is live while we work, so the
; T_STATE=1 store is the LAST write to the record - a byte store is atomic,
; publishing a fully built frame. The slot's sch_cycles counter is zeroed
; before the publish (the ISR only charges the RUNNING slot, so a
; pre-publish write cannot race - SPEC.md 8.1). The scan starts at slot 1:
; slot 0 is the UI task, never free, and runs on the boot stack.
; The entry routine must never ret - it terminates only via task_exit
; (usually through inst_task_die, SPEC.md 29) or loops forever.
;
; in:       AX = entry point (near, kernel segment)
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

    mov bx, sch_tasks + T_SIZE  ; scan for a free slot, from slot 1
    mov cx, 1                   ; CX = slot index
.find:
    cmp byte [bx+T_STATE], 0
    je .found
    add bx, T_SIZE
    inc cx
    cmp cx, MAX_TASKS
    jb .find
    stc                         ; table full
    jmp .done

.found:
    mov si, ax                  ; SI = entry (AX becomes scratch)
    mov ax, cx                  ; stack top = sch_stacks + slot*SCH_STACK
    mov di, SCH_STACK           ; (slot 1 owns sch_stacks[0..1535]; slot 0
    push dx                     ; has no slice - it runs on the boot stack)
    mul di                      ; DX:AX = AX * 1536 (high half discarded)
    pop dx
    add ax, sch_stacks
    sub ax, SCH_FRAME           ; carve the fake frame at the very top
    mov di, ax
    mov [bx+T_SP], di

    mov word [di], KERNEL_SEG   ; DS slot
    mov word [di+2], KERNEL_SEG ; ES slot
    xor ax, ax
    mov [di+4], ax              ; BP
    mov [di+6], ax              ; DI
    mov [di+8], ax              ; SI
    mov [di+10], ax             ; BX
    mov [di+12], dx             ; DX = the spawn argument
    mov [di+14], ax             ; CX
    mov [di+16], ax             ; AX
    mov [di+18], si             ; IP = entry
    mov word [di+20], KERNEL_SEG; CS
    mov word [di+22], 0x0202    ; FLAGS: IF=1, reserved bit 1 set
    mov [bx+T_INST], dl         ; owning instance (0xFF = none)

    mov ax, cx                  ; zero the slot's cycle counter before the
    shl ax, 1                   ; publish, so a reused slot never inherits
    shl ax, 1                   ; a dead task's total (SPEC.md 8.1)
    mov di, ax
    mov word [di+sch_cycles], 0
    mov word [di+sch_cycles+2], 0

    mov byte [bx+T_STATE], 1    ; publish - MUST be the last record write
    mov ax, cx                  ; AL = slot index
    clc

.done:
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
; sch_isr - int 08h handler (PIT, ~18.2 Hz)
;
; Chains to the BIOS tick FIRST (it maintains its own count and sends the
; EOI - we must not), then bumps [ticks], wakes due sleepers, and switches
; round-robin unless sch_lock is held. Runs with IF=0 throughout; calls no
; BIOS service beyond the chain; never sti.
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

    pushf                       ; chain: BIOS ticks its count and sends EOI
    call far [sch_old08]

    inc word [ticks]
    call sch_account            ; after the inc, so [ticks] and the freshly
                                ; reloaded counter agree (SPEC.md 8.1); and
                                ; before the sch_lock check, so accounting
                                ; runs every tick even during floppy reads

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
    ; fall through into sch_switch

; -----------------------------------------------------------------------------
; sch_switch - common switch path (module-internal)
;
; Entered with IF=0, DS=KERNEL_SEG and a complete saved frame on the current
; stack (from sch_isr or task_yield's save stub). Parks SP in the current
; record, picks the next ready task round-robin (falling back to the current
; task if nothing is ready), and resumes it.
; -----------------------------------------------------------------------------
sch_switch:
    mov bl, [sch_cur]           ; park the outgoing task's SP
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov [bx+sch_tasks+T_SP], sp

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
    je .pick
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

; =============================================================================
; scheduler state
; =============================================================================
section .bss

sch_tasks:  resb MAX_TASKS * T_SIZE     ; task records (layout above)
sch_stacks: resb (MAX_TASKS-1) * SCH_STACK ; slots 1..11 only: slot n's stack
                                        ; top at n*1536 (slot 0 = the UI task
                                        ; runs on the boot stack, SS:0xFFFE)
sch_old08:  resb 4                      ; saved BIOS int 08h vector (off:seg)
ticks:      resw 1                      ; public tick count, wraps at 65536
sch_cur:    resb 1                      ; index of the running task
sch_lock:   resb 1                      ; non-zero: ISR counts but won't switch
sch_cycles: resw MAX_TASKS * 2          ; per-task CPU time, PIT cycles, dword
                                        ; each (lo word first); readers snap-
                                        ; shot under pushf/cli (SPEC.md 8.1)
sch_pit_last: resw 2                    ; last timestamp: phase, then ticks

section .text
