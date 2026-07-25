; =============================================================================
; jop - sched.inc
;
; Pre-emptive round-robin scheduler off the PIT (SPEC.md 8). Up to MAX_TASKS
; tasks, each with a 1536-byte stack in .bss; task 0 is the boot thread (the
; UI task). All tasks run tiny-model: CS = DS = SS = KERNEL_SEG, so a context
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
;
; CPU accounting. PIT channel 0 is re-programmed to mode 2 (rate generator,
; binary, divisor 65536) so the interrupt rate stays exactly the BIOS ~18.2 Hz
; but the readable counter becomes a straight 65535..0 down-count. Mode 3 (the
; BIOS setting) decrements by two and reloads twice per period, so its counter
; cannot be turned into an unambiguous timestamp. sch_account samples that
; counter on every context switch and charges the whole elapsed interval, in
; PIT input clocks, to the task that was running - sch_cur only ever changes
; inside sch_switch, so all time since the previous sample belongs to it.
; Time spent in task 0's idle poll (ui.inc raises sch_idle_flag around it) is
; charged to a separate idle accumulator instead, so the meter reports real
; load rather than a permanent 100%. sch_cpu_take snapshots and zeroes the
; accumulators for taskman.inc.
; =============================================================================

; --- task table (SPEC.md 8) --------------------------------------------------
; Record layout, 8 bytes (the stride stays a power of two so every index is a
; "shl bx,cl" with cl=3); the old padding word at offset 6 is now the name:
;       +0 T_STATE  byte    0 free, 1 ready, 2 sleeping
;       +1          byte    unused
;       +2 T_SP     word    saved SP (frame above), valid while suspended
;       +4 T_WAKE   word    [ticks] value to wake at, only if T_STATE = 2
;       +6 T_NAME   word    near ptr to an ASCIIZ name, 0 = unnamed
MAX_TASKS   equ 4
T_STATE     equ 0               ; byte: 0 free, 1 ready, 2 sleeping
T_SP        equ 2               ; word: saved SP (frame above)
T_WAKE      equ 4               ; word: [ticks] value to wake at
T_NAME      equ 6               ; word: near ptr to ASCIIZ name, 0 = unnamed
T_SIZE      equ 8               ; record stride

SCH_STACK   equ 1536            ; per-task stack bytes
SCH_FRAME   equ 24              ; saved-frame size: 9 regs + IP/CS/FLAGS

; --- PIT channel 0 (SPEC.md 8) -----------------------------------------------
PIT_CH0     equ 0x40            ; counter 0 data port
PIT_CMD     equ 0x43            ; mode/command register
PIT_M2      equ 0x34            ; counter 0, lo/hi, mode 2, binary
PIT_M3      equ 0x36            ; counter 0, lo/hi, mode 3, binary (BIOS)
PIT_LATCH   equ 0x00            ; counter-latch command for counter 0

; -----------------------------------------------------------------------------
; sched_init - set up the task table, re-program the PIT and hook int 08h
;
; Task 0 (the caller) becomes current and ready; its T_SP is filled in by the
; first switch away from it. Saves the BIOS vector for chaining and unhook.
; The PIT switch to mode 2 keeps the tick rate (65536 input clocks) but gives
; sch_account a readable, unambiguous down-counter; it happens inside the same
; cli window as the vector install so no tick can land half-way through.
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
    mov word [sch_tasks+T_NAME], sch_name_sys   ; name before the publish store
    mov byte [sch_tasks+T_STATE], 1     ; task 0 = this thread, ready

    xor ax, ax                  ; CPU accounting starts from a clean slate:
    xor bx, bx                  ; .bss is not zeroed by the loader and the ISR
    mov cx, MAX_TASKS           ; goes live a few instructions from here
.clracc:
    mov [bx+sch_cpu_lo], ax
    mov [bx+sch_cpu_hi], ax
    mov [bx+sch_snap_lo], ax
    mov [bx+sch_snap_hi], ax
    add bx, 2
    loop .clracc
    mov [bx+sch_snap_lo], ax    ; BX = MAX_TASKS*2: the idle snapshot slot
    mov [bx+sch_snap_hi], ax
    mov [sch_idle_lo], ax
    mov [sch_idle_hi], ax
    mov [sch_last_lo], ax
    mov [sch_last_hi], ax
    mov byte [sch_idle_flag], 0

    xor ax, ax                  ; vector table segment
    mov es, ax
    pushf
    cli                         ; no half-written vector, ever

    mov al, PIT_M2              ; counter 0 -> mode 2, binary, lo/hi
    out PIT_CMD, al
    jmp short .io1              ; ISA I/O settling delay between writes
.io1:
    xor al, al                  ; divisor 0 = 65536 input clocks = the BIOS
    out PIT_CH0, al             ; ~18.2 Hz rate, unchanged
    jmp short .io2
.io2:
    out PIT_CH0, al

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

sch_name_sys:   db 'System', 0  ; task 0's name (data, not .bss - see SPEC.md 1)

; -----------------------------------------------------------------------------
; sched_unhook - put the PIT back in mode 3, restore int 08h, unhook the mouse
;
; Used before reboot so the ROM owns the machine again. The PIT goes back to
; the BIOS mode BEFORE the vector, so no tick can reach our ISR while the
; counter is being re-programmed.
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

    mov al, PIT_M3              ; counter 0 -> mode 3 (square wave), the BIOS
    out PIT_CMD, al             ; setting the ROM expects
    jmp short .io1
.io1:
    xor al, al                  ; divisor 0 = 65536, same ~18.2 Hz
    out PIT_CH0, al
    jmp short .io2
.io2:
    out PIT_CH0, al

    mov ax, [sch_old08]
    mov [es:0x20], ax
    mov ax, [sch_old08+2]
    mov [es:0x22], ax
    popf

    call mouse_unhook           ; restores int 0Ch, masks IRQ4

    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; task_spawn - create a task that iret-launches into a near entry point
;
; Builds a complete saved frame at the top of the slot's stack: GP regs zero,
; DS = ES = KERNEL_SEG, CS = KERNEL_SEG, IP = entry, FLAGS with IF=1. The
; timer ISR is live while we work, so the T_STATE=1 store is the LAST write
; to the record - a byte store is atomic, publishing a fully built frame; the
; name therefore goes in before it, so nothing can observe a used record with
; a garbage T_NAME. The entry routine must never ret (loop forever).
;
; in:       AX = entry point (near, kernel segment)
;           SI = near ptr to an ASCIIZ task name for taskman.inc, 0 = unnamed
; out:      CF = 1 if no free slot (task not created)
; clobbers: flags (SI is an input and is preserved)
; -----------------------------------------------------------------------------
task_spawn:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, sch_tasks           ; scan for a free slot
    xor dx, dx                  ; DX = slot index
.find:
    cmp byte [bx+T_STATE], 0
    je .found
    add bx, T_SIZE
    inc dx
    cmp dx, MAX_TASKS
    jb .find
    stc                         ; table full
    jmp .done

.found:
    mov cx, ax                  ; CX = entry (AX is about to be clobbered)
    mov ax, dx
    inc ax                      ; stack top = sch_stacks + (slot+1)*SCH_STACK
    mov di, SCH_STACK
    mul di                      ; DX:AX = AX * 1536 (DX high half discarded)
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
    mov [di+12], ax             ; DX
    mov [di+14], ax             ; CX
    mov [di+16], ax             ; AX
    mov [di+18], cx             ; IP = entry
    mov word [di+20], KERNEL_SEG; CS
    mov word [di+22], 0x0202    ; FLAGS: IF=1, reserved bit 1 set

    mov [bx+T_NAME], si         ; name before state, never after
    mov byte [bx+T_STATE], 1    ; publish - MUST be the last record write
    clc

.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
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
; stack (from sch_isr or task_yield's save stub). Charges the elapsed interval
; to the outgoing task, parks SP in the current record, picks the next ready
; task round-robin (falling back to the current task if nothing is ready), and
; resumes it.
;
; sch_account must run before [sch_cur] changes and is the scheduler's only
; accounting call site: when sch_lock is held the ISR jumps straight to
; sch_resume, and that time is correctly charged to the still-current task at
; the next real switch.
; -----------------------------------------------------------------------------
sch_switch:
    call sch_account            ; close the outgoing task's interval

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

; -----------------------------------------------------------------------------
; sch_account - charge the time since the last sample to the current task
;
; Module-internal. Reads PIT channel 0 and builds an absolute 32-bit timestamp
; in PIT input clocks:
;       now = [ticks] * 65536 + (65536 - count)
; (count == 0 is the carry case: low word 0, high word [ticks]+1). The delta
; against sch_last goes to sch_idle_* when task 0 is sitting in ui.inc's poll
; loop, otherwise to sch_cpu_*[sch_cur].
;
; A NEGATIVE delta means the counter has already wrapped but our int 08h has
; not run yet (we sample with IF=0, so [ticks] is one behind the hardware).
; That is exactly one uncounted wrap, so the timestamp is REBUILT with
; [ticks]+1 rather than dropped. Dropping it would not merely defer the
; interval: sch_last would stay put and the next sample would charge the whole
; span to whatever task is current by then, so one dropped sample after a long
; sch_lock section (a multi-sector disk_read) hands another task seconds of
; time it never used.
;
; If it is still negative after the rebuild, the 32-bit timestamp itself has
; wrapped - [ticks] rolls over every 65536 ticks, about 3.6 hours of uptime.
; The elapsed time is then unknowable, so that one sample is dropped, but
; sch_last is RESYNCED to now: leaving it in the future would make every
; later delta negative too and freeze the accounting for good.
;
; in:       nothing; MUST be called with IF=0 and DS = KERNEL_SEG
; out:      nothing
; clobbers: nothing (all registers and flags preserved)
; -----------------------------------------------------------------------------
sch_account:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov al, PIT_LATCH           ; freeze counter 0 so lo/hi are one reading
    out PIT_CMD, al
    jmp short .io1              ; ISA I/O settling delay
.io1:
    in al, PIT_CH0              ; low byte first (lo/hi access mode)
    mov bl, al
    jmp short .io2
.io2:
    in al, PIT_CH0
    mov bh, al                  ; BX = live down-count, 65535..0

    mov bp, [ticks]             ; BP = timestamp high word
    xor ax, ax
    sub ax, bx                  ; AX = 65536 - count (mod 65536)
    jnz .stamp
    inc bp                      ; count == 0: the full 65536 carried out
.stamp:
    mov cx, ax                  ; CX:BP = now, kept for the sch_last store
    mov dx, bp
    sub ax, [sch_last_lo]       ; DX:AX = now - sch_last
    sbb dx, [sch_last_hi]
    or dx, dx
    jns .pick

    inc bp                      ; the counter wrapped before our int 08h ran:
    mov ax, cx                  ; the true high word is [ticks]+1. Rebuild the
    mov dx, bp                  ; timestamp and retry, so the interval lands on
    sub ax, [sch_last_lo]       ; the task that actually spent it.
    sbb dx, [sch_last_hi]
    or dx, dx
    jns .pick

    mov [sch_last_lo], cx       ; [ticks] itself has wrapped (~3.6 h): this one
    mov [sch_last_hi], bp       ; interval is lost, but resync or every later
    jmp short .done             ; delta stays negative and accounting stops

.pick:
    mov bl, [sch_cur]           ; pick the accumulator: DI = lo, SI = hi
    xor bh, bh
    or bl, bl
    jnz .task
    cmp byte [sch_idle_flag], 0
    je .task
    mov di, sch_idle_lo         ; task 0 idling in ui_task's poll loop
    mov si, sch_idle_hi
    jmp short .charge
.task:
    shl bx, 1                   ; word index into the per-task accumulators
    mov di, bx
    add di, sch_cpu_lo
    mov si, bx
    add si, sch_cpu_hi
.charge:
    add [di], ax
    adc [si], dx
    mov [sch_last_lo], cx       ; only advance the clock on a kept sample
    mov [sch_last_hi], bp

.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; sch_cpu_take - snapshot and reset the CPU accounting (taskman.inc)
;
; Closes the in-flight interval first (so the caller's own time up to this
; instant is counted), then moves every accumulator into sch_snap_* and zeroes
; the live ones, so consecutive calls measure disjoint intervals. Snapshot
; order is task 0..MAX_TASKS-1 followed by the idle accumulator in slot
; MAX_TASKS. Task level only - it runs a critical section, not an ISR.
;
; in:       nothing
; out:      nothing (results in sch_snap_lo/hi, MAX_TASKS+1 dwords)
; clobbers: flags
; -----------------------------------------------------------------------------
sch_cpu_take:
    push ax
    push bx
    push cx
    pushf
    cli                         ; the timer ISR must not land mid-snapshot

    call sch_account            ; charge the interval in progress

    xor bx, bx
    mov cx, MAX_TASKS
.copy:
    mov ax, [bx+sch_cpu_lo]
    mov [bx+sch_snap_lo], ax
    mov word [bx+sch_cpu_lo], 0
    mov ax, [bx+sch_cpu_hi]
    mov [bx+sch_snap_hi], ax
    mov word [bx+sch_cpu_hi], 0
    add bx, 2
    loop .copy

    mov ax, [sch_idle_lo]       ; BX = MAX_TASKS*2: the idle snapshot slot
    mov [bx+sch_snap_lo], ax
    mov word [sch_idle_lo], 0
    mov ax, [sch_idle_hi]
    mov [bx+sch_snap_hi], ax
    mov word [sch_idle_hi], 0

    popf
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; scheduler state
; =============================================================================
section .bss

sch_tasks:  resb MAX_TASKS * T_SIZE     ; task records (layout above)
sch_stacks: resb MAX_TASKS * SCH_STACK  ; slot n's stack tops at (n+1)*1536
sch_old08:  resb 4                      ; saved BIOS int 08h vector (off:seg)
ticks:      resw 1                      ; public tick count, wraps at 65536
sch_cur:    resb 1                      ; index of the running task
sch_lock:   resb 1                      ; non-zero: ISR counts but won't switch

; --- CPU accounting, all figures in PIT input clocks (1.193 MHz) -------------
sch_cpu_lo:     resw MAX_TASKS          ; per-task accumulator, low word
sch_cpu_hi:     resw MAX_TASKS          ; per-task accumulator, high word
sch_idle_lo:    resw 1                  ; task 0 spinning in ui_task's poll
sch_idle_hi:    resw 1
sch_last_lo:    resw 1                  ; timestamp of the last kept sample
sch_last_hi:    resw 1
sch_idle_flag:  resb 1                  ; set by ui.inc while task 0 idles
sch_apad:       resb 1                  ; keeps the arrays below word-aligned
sch_snap_lo:    resw MAX_TASKS + 1      ; sch_cpu_take output: tasks, then idle
sch_snap_hi:    resw MAX_TASKS + 1

section .text
