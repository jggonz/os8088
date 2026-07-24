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
; =============================================================================

; --- task table (SPEC.md 8) --------------------------------------------------
MAX_TASKS   equ 4
T_STATE     equ 0               ; byte: 0 free, 1 ready, 2 sleeping
T_SP        equ 2               ; word: saved SP (frame above)
T_WAKE      equ 4               ; word: [ticks] value to wake at
T_SIZE      equ 8               ; record stride (2 bytes padding)

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

    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; task_spawn - create a task that iret-launches into a near entry point
;
; Builds a complete saved frame at the top of the slot's stack: GP regs zero,
; DS = ES = KERNEL_SEG, CS = KERNEL_SEG, IP = entry, FLAGS with IF=1. The
; timer ISR is live while we work, so the T_STATE=1 store is the LAST write
; to the record - a byte store is atomic, publishing a fully built frame.
; The entry routine must never ret (loop forever).
;
; in:       AX = entry point (near, kernel segment)
; out:      CF = 1 if no free slot (task not created)
; clobbers: flags
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
sch_stacks: resb MAX_TASKS * SCH_STACK  ; slot n's stack tops at (n+1)*1536
sch_old08:  resb 4                      ; saved BIOS int 08h vector (off:seg)
ticks:      resw 1                      ; public tick count, wraps at 65536
sch_cur:    resb 1                      ; index of the running task
sch_lock:   resb 1                      ; non-zero: ISR counts but won't switch

section .text
