; =============================================================================
; jop - taskmgr.inc (SPEC.md 28)
;
; Built-in Task Manager window (module prefix tm_, not a loadable .jop
; package - it ships in the kernel image like About/Disk). Shows a bar-graph
; breakdown of CPU usage per scheduler task slot (from sch_cpu_ticks, §8) and
; a segmented RAM-usage bar/text (from the boot-time int 12h query). Created
; hidden by tm_init (About/Disk pattern) and shown via Special > Task Manager
; (CMD_TASKMGR, menu.inc/ui.inc). Its own background task (tm_task) recomputes
; the CPU-percentage cache every ~1s unconditionally (Clock's time-keeping
; pattern), then, only if the window is visible and not wm_obscured (checked
; under the gfx lock, Clock/Bounce-style), white-fills the whole content rect
; and re-runs tm_paint - Note Pad onkey's "white-fill then re-run the paint
; proc" redraw, chosen over Clock's minimal-rect approach because this
; window's content is several bars, not one small string, and a 1s full
; redraw is cheap while keeping the drawing logic single-sourced in tm_paint.
; =============================================================================

; --- task-row geometry (content-relative unless noted) -----------------------
TM_ROW_H    equ 16              ; task row height, px
TM_ROW_Y0   equ 16              ; first task row top
TM_BAR_X    equ 64              ; task bar left
TM_BAR_W    equ 100             ; task bar outer size, 100x10 (1px frame)
TM_BAR_H    equ 10
TM_PCT_X    equ 170             ; percentage text left

; --- memory-bar geometry -------------------------------------------------
TM_MEM_Y0   equ 84               ; "Memory" label y
TM_MEM_BX   equ 4                ; memory bar left
TM_MEM_BY   equ 96               ; memory bar top
TM_MEM_BW   equ 200              ; memory bar outer size, 200x14 (1px frame)
TM_MEM_BH   equ 14
TM_MEM_SEGW equ 197              ; kb/total ratio divisor for the segments -
                                 ; the interior fill is inset 1px each side
                                 ; from the frame (198px), and the colored
                                 ; segments start 1px further in again, so a
                                 ; fully-used bar still leaves a 1px white
                                 ; margin before the frame on the right; see
                                 ; tm_paint for the exact coordinates.
TM_TXT1_Y   equ 114              ; "Kernel NNK  App NNK" / "App none"
TM_TXT2_Y   equ 124              ; "Free NNNK / NNNK total"

; -----------------------------------------------------------------------------
; tm_init - create the Task Manager window (hidden), read total conventional
;           RAM once (int 12h - same boot-time hardware-query precedent as
;           desk_init's int 11h, SPEC.md 26), zero the CPU-percentage cache,
;           and spawn tm_task.
; in:  nothing
; out: nothing; [tm_win] = window ptr
; clobbers: nothing (flags only)
; Module state is initialized BEFORE task_spawn: tm_task starts running the
; instant it is spawned, so any .bss it reads must already be valid (mirrors
; apps_init's rationale for app_clock_task/app_bounce_task, SPEC.md 14).
; -----------------------------------------------------------------------------
tm_init:
    push ax
    push bx
    push cx
    push si
    push di

    int 0x12                    ; AX = KB of contiguous conventional RAM;
    mov [tm_ram_total_kb], ax   ; 0 = "unknown" - guarded at paint time

    mov word [tm_ticks_last], 0
    xor ax, ax
    mov di, tm_cpu_last
    mov cx, MAX_TASKS
.zcpu:
    mov [di], ax
    add di, 2
    loop .zcpu
    mov di, tm_pct
    mov cx, MAX_TASKS
.zpct:
    mov byte [di], 0
    inc di
    loop .zpct

    mov si, tm_tpl
    call wm_create
    mov [tm_win], bx            ; stays hidden until CMD_TASKMGR

    mov ax, tm_task
    call task_spawn             ; lands in slot 3 - MAX_TASKS is exactly
                                 ; full after boot (0=UI, 1=Clock, 2=Bounce)

    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
tm_tpl:     dw 170, 250, 240, 210, tm_ttl, tm_paint, 0, 0
tm_ttl:     db 'Task Manager', 0

; --- fixed per-slot labels: task spawn order is boot-pinned, exactly like the
;     software floppy's directory order (SPEC.md 4: "Directory order on the
;     apps disk is pinned in the Makefile"). apps_init spawns app_clock_task
;     then app_bounce_task (slots 1, 2) before tm_init spawns tm_task
;     (slot 3); slot 0 is the boot thread that becomes the UI task. ----------
tm_lbl0:    db 'UI/Boot', 0
tm_lbl1:    db 'Clock', 0
tm_lbl2:    db 'Bounce', 0
tm_lbl3:    db 'TaskMgr', 0
tm_lbl_free: db '(free)', 0

tm_labels:  dw tm_lbl0, tm_lbl1, tm_lbl2, tm_lbl3

tm_s_cpu:     db 'CPU', 0
tm_s_mem:     db 'Memory', 0
tm_s_ram_unk: db 'RAM: unknown', 0
tm_s_kernel:  db 'Kernel ', 0
tm_s_k_app:   db 'K  App ', 0
tm_s_none:    db 'none', 0
tm_s_k:       db 'K', 0
tm_s_free:    db 'Free ', 0
tm_s_slash:   db 'K / ', 0
tm_s_total:   db 'K total', 0

; =============================================================================
; background task
; =============================================================================

; -----------------------------------------------------------------------------
; tm_task - background task: recompute the CPU% cache every ~1s (always),
;           redraw only when the window is visible and not obscured (never
;           returns)
; in:  nothing (task entry)
; -----------------------------------------------------------------------------
tm_task:
.loop:
    mov ax, 18                  ; ~1 second at ~18.2 Hz
    call task_sleep

    call tm_recalc              ; unconditional, Clock's time-keeping pattern

    call gfx_lock
    mov bx, [tm_win]
    test word [bx+W_FLAGS], 2   ; re-check under the lock (SPEC.md 14)
    jz .skip
    call wm_obscured
    jc .skip

    mov si, bx                  ; SI = window ptr, for tm_paint below
    call wm_content              ; in BX = win ptr; out AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call gfx_fill                ; white-fill the whole content rect
    call tm_paint                ; SI still = window ptr
.skip:
    call gfx_unlock
    jmp .loop

; -----------------------------------------------------------------------------
; tm_recalc - recompute tm_pct[] from sch_cpu_ticks deltas (wrap-safe
;             subtraction idiom, SPEC.md 8). [ticks] and sch_cpu_ticks[] are
;             sampled together under a short pushf/cli/popf (SPEC.md 1) into
;             tm_cpu_snap so a PIT tick landing mid-sample can't credit a
;             slot's counter after delta_t was already fixed, which would
;             otherwise let delta_i exceed delta_t; the division work runs
;             outside the critical section. Runs every tm_task iteration
;             regardless of visibility.
; in:  nothing
; out: nothing; tm_pct[0..MAX_TASKS-1] updated (unchanged if delta_t == 0)
; clobbers: flags only
; -----------------------------------------------------------------------------
tm_recalc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    pushf
    cli                          ; atomic snapshot of ticks + sch_cpu_ticks[]
    mov ax, [ticks]
    mov si, sch_cpu_ticks
    mov di, tm_cpu_snap
    mov cx, MAX_TASKS
.snap:
    mov dx, [si]
    mov [di], dx
    add si, 2
    add di, 2
    loop .snap
    popf

    mov dx, ax
    sub ax, [tm_ticks_last]      ; delta_t, wrap-safe
    mov [tm_ticks_last], dx
    mov [tm_delta_t], ax
    test ax, ax
    jz .done                    ; defensive div-by-zero guard (shouldn't
                                 ; happen - task_sleep 18 always elapses real
                                 ; ticks - but this codebase guards DIV at
                                 ; every boundary, e.g. Clock's bounded
                                 ; quotient, SPEC.md 14)

    mov si, tm_cpu_snap
    mov di, tm_cpu_last
    mov bx, tm_pct
    mov cx, MAX_TASKS
.slot:
    push cx
    mov ax, [si]
    mov dx, ax
    sub ax, [di]                 ; delta_i, wrap-safe
    mov [di], dx
    mov cx, 100
    mul cx                       ; DX:AX = delta_i * 100 (fits in AX: delta_i
                                 ; <= delta_t, ~18-20, so product < 65535)
    mov cx, [tm_delta_t]
    div cx                       ; AX = delta_i*100/delta_t
    mov [bx], al
    pop cx
    add si, 2
    add di, 2
    inc bx
    loop .slot
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; paint
; =============================================================================

; -----------------------------------------------------------------------------
; tm_paint - W_PAINT for the Task Manager window (also called directly by
;            tm_task's redraw step, under its own already-held lock)
; in:  SI = window ptr (content already white-filled by the caller, gfx lock
;      held per SPEC.md 11)
; out: nothing
; clobbers: nothing (flags only)
; Must not lock, block or spawn (SPEC.md 11).
; -----------------------------------------------------------------------------
tm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call wm_content              ; AX = content left, DX = content top
    mov [tm_cx], ax
    mov [tm_cy], dx

    mov byte [gfx_color], CBLACK
    mov cx, ax
    add cx, 4
    mov dx, [tm_cy]
    add dx, 4
    mov si, tm_s_cpu
    call font_str

    ; --- four task rows (MAX_TASKS is pinned at 4) ---
    xor bp, bp                   ; BP = slot index, 0..MAX_TASKS-1
.rowloop:
    mov ax, TM_ROW_H
    mul bp                       ; AX = row index * 16
    add ax, TM_ROW_Y0
    add ax, [tm_cy]
    mov [tm_rowy], ax            ; absolute row top

    mov byte [gfx_color], CBLACK ; slot label at content x=4
    mov cx, [tm_cx]
    add cx, 4
    mov dx, [tm_rowy]
    mov si, bp
    shl si, 1
    mov si, [tm_labels+si]
    call font_str

    mov si, bp                   ; T_STATE for this slot (SPEC.md 8)
    mov cl, 3
    shl si, cl                   ; SI = slot * T_SIZE
    cmp byte [sch_tasks+si+T_STATE], 0
    jne .active
    mov cx, [tm_cx]              ; free slot (defensive only - nothing ever
    add cx, TM_BAR_X             ; frees a task slot in this OS): "(free)",
    mov dx, [tm_rowy]            ; no bar/pct
    mov si, tm_lbl_free
    call font_str
    jmp .rownext

.active:
    mov ax, [tm_cx]
    add ax, TM_BAR_X
    mov [tm_barx], ax

    mov ax, [tm_barx]            ; 1px black frame, 100x10
    mov bx, [tm_rowy]
    mov cx, [tm_barx]
    add cx, TM_BAR_W - 1
    mov dx, [tm_rowy]
    add dx, TM_BAR_H - 1
    mov byte [gfx_color], CBLACK
    call gfx_frame

    mov ax, [tm_barx]            ; white-fill the 98x8 interior
    inc ax
    mov bx, [tm_rowy]
    inc bx
    mov cx, [tm_barx]
    add cx, TM_BAR_W - 2
    mov dx, [tm_rowy]
    add dx, TM_BAR_H - 2
    mov byte [gfx_color], CWHITE
    call gfx_fill

    mov al, [tm_pct+bp]           ; sub-fill width = pct * 98 / 100
    xor ah, ah
    mov cx, 98
    mul cx
    mov cx, 100
    div cx
    test ax, ax
    jz .pctdraw                  ; 0 width: no fill call at all
    mov cx, ax                   ; CX = fill width
    mov ax, [tm_barx]
    inc ax                       ; interior left
    mov bx, [tm_rowy]
    inc bx                       ; interior top
    add cx, ax
    dec cx                       ; CX = x2 = interior_left + width - 1
    mov dx, [tm_rowy]
    add dx, TM_BAR_H - 2          ; interior bottom
    mov byte [gfx_color], CBLACK
    call gfx_fill

.pctdraw:
    mov al, [tm_pct+bp]           ; "0".."100" + "%"
    xor ah, ah
    mov di, tm_numbuf
    call fm_utoa                 ; in AX=value, DI=dest; out DI->stored NUL
                                 ; (files.inc, contract fits directly)
    mov byte [di], '%'
    inc di
    mov byte [di], 0
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, TM_PCT_X
    mov dx, [tm_rowy]
    mov si, tm_numbuf
    call font_str

.rownext:
    inc bp
    cmp bp, MAX_TASKS
    jb .rowloop

    ; --- Memory label + bar ---
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 4
    mov dx, [tm_cy]
    add dx, TM_MEM_Y0
    mov si, tm_s_mem
    call font_str

    mov ax, [tm_cx]
    add ax, TM_MEM_BX
    mov [tm_membx], ax
    mov ax, [tm_cy]
    add ax, TM_MEM_BY
    mov [tm_memby], ax

    cmp word [tm_ram_total_kb], 0
    je .mem_unknown

    mov ax, [tm_membx]           ; white-fill the interior first (1px inset)
    inc ax
    mov bx, [tm_memby]
    inc bx
    mov cx, [tm_membx]
    add cx, TM_MEM_BW - 2
    mov dx, [tm_memby]
    add dx, TM_MEM_BH - 2
    mov byte [gfx_color], CWHITE
    call gfx_fill

    mov ax, KTEXT_SIZE + KBSS_SIZE + 511  ; kernel_kb, rounded up (forward
    mov cx, 1024                          ; reference to an equ resolved in a
    xor dx, dx                            ; later pass - same pattern as
    div cx                                ; jopapi.inc's JOP_HEADER, §20.5)
    mov [tm_kernel_kb], ax

    cmp word [ld_appwin], 0
    je .noapp
    mov ax, APP_MAX_SIZE + 1023  ; app_kb, rounded up
    mov cx, 1024
    xor dx, dx
    div cx
    jmp .haveapp
.noapp:
    xor ax, ax
.haveapp:
    mov [tm_app_kb], ax

    mov ax, [tm_kernel_kb]        ; kernel_w = kernel_kb * 197 / total
    mov cx, TM_MEM_SEGW
    mul cx
    mov cx, [tm_ram_total_kb]
    div cx
    mov [tm_kernel_w], ax

    mov ax, [tm_app_kb]           ; app_w = app_kb * 197 / total
    mov cx, TM_MEM_SEGW
    mul cx
    mov cx, [tm_ram_total_kb]
    div cx
    mov [tm_app_w], ax

    mov ax, [tm_membx]            ; segments start 1px in from the frame
    inc ax
    mov [tm_segx], ax

    mov cx, [tm_kernel_w]
    test cx, cx
    jz .nokseg
    mov ax, [tm_segx]
    mov bx, [tm_memby]
    inc bx
    mov cx, [tm_segx]
    add cx, [tm_kernel_w]
    dec cx
    mov dx, [tm_memby]
    add dx, TM_MEM_BH - 2
    mov byte [gfx_color], CDGRAY
    call gfx_fill
    mov ax, [tm_segx]
    add ax, [tm_kernel_w]
    mov [tm_segx], ax
.nokseg:
    mov cx, [tm_app_w]
    test cx, cx
    jz .noaseg
    mov ax, [tm_segx]
    mov bx, [tm_memby]
    inc bx
    mov cx, [tm_segx]
    add cx, [tm_app_w]
    dec cx
    mov dx, [tm_memby]
    add dx, TM_MEM_BH - 2
    mov byte [gfx_color], 1      ; blue - distinct from the kernel's CDGRAY
    call gfx_fill
.noaseg:

    mov ax, [tm_membx]            ; 1px black frame LAST, so it stays crisp
    mov bx, [tm_memby]
    mov cx, [tm_membx]
    add cx, TM_MEM_BW - 1
    mov dx, [tm_memby]
    add dx, TM_MEM_BH - 1
    mov byte [gfx_color], CBLACK
    call gfx_frame

    ; --- line 1: "Kernel NNK  App NNK" / "Kernel NNK  App none" ---
    mov di, tm_buf1
    mov si, tm_s_kernel
    call fm_scat                 ; files.inc helper: SI=NUL src, DI=dest;
                                 ; out DI->stored NUL (contract fits directly)
    mov ax, [tm_kernel_kb]
    call fm_utoa
    mov si, tm_s_k_app
    call fm_scat
    cmp word [ld_appwin], 0
    je .noappstr
    mov ax, [tm_app_kb]
    call fm_utoa
    mov si, tm_s_k
    call fm_scat
    jmp .line1done
.noappstr:
    mov si, tm_s_none
    call fm_scat
.line1done:
    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 4
    mov dx, [tm_cy]
    add dx, TM_TXT1_Y
    mov si, tm_buf1
    call font_str

    ; --- line 2: "Free NNNK / NNNK total" ---
    mov ax, [tm_ram_total_kb]
    sub ax, [tm_kernel_kb]
    sub ax, [tm_app_kb]
    jns .freeok
    xor ax, ax                   ; clamp: shouldn't go negative, cheap guard
.freeok:
    mov [tm_free_kb], ax

    mov di, tm_buf2
    mov si, tm_s_free
    call fm_scat
    mov ax, [tm_free_kb]
    call fm_utoa
    mov si, tm_s_slash
    call fm_scat
    mov ax, [tm_ram_total_kb]
    call fm_utoa
    mov si, tm_s_total
    call fm_scat

    mov byte [gfx_color], CBLACK
    mov cx, [tm_cx]
    add cx, 4
    mov dx, [tm_cy]
    add dx, TM_TXT2_Y
    mov si, tm_buf2
    call font_str
    jmp .paintdone

.mem_unknown:
    mov ax, [tm_membx]            ; frame only, no segments
    mov bx, [tm_memby]
    mov cx, [tm_membx]
    add cx, TM_MEM_BW - 1
    mov dx, [tm_memby]
    add dx, TM_MEM_BH - 1
    mov byte [gfx_color], CBLACK
    call gfx_frame

    mov cx, [tm_cx]
    add cx, 4
    mov dx, [tm_cy]
    add dx, TM_TXT1_Y
    mov si, tm_s_ram_unk
    call font_str

.paintdone:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

tm_win:          resw 1          ; Task Manager window record ptr
tm_ram_total_kb: resw 1          ; int 12h result at boot; 0 = unknown

tm_ticks_last:   resw 1          ; last [ticks] sample (tm_recalc)
tm_cpu_last:     resw MAX_TASKS  ; last sch_cpu_ticks[] sample, per slot
tm_cpu_snap:     resw MAX_TASKS  ; tm_recalc scratch: sch_cpu_ticks[] sampled
                                 ; atomically with [ticks] under cli
tm_pct:          resb MAX_TASKS  ; last-computed CPU percentage, 0..100
tm_delta_t:      resw 1          ; tm_recalc scratch: ticks elapsed this pass

tm_cx:           resw 1          ; paint scratch: content left
tm_cy:           resw 1          ; paint scratch: content top
tm_rowy:         resw 1          ; paint scratch: current row's absolute top
tm_barx:         resw 1          ; paint scratch: current task bar's left
tm_membx:        resw 1          ; paint scratch: memory bar left
tm_memby:        resw 1          ; paint scratch: memory bar top
tm_segx:         resw 1          ; paint scratch: next free segment x
tm_kernel_kb:    resw 1          ; paint scratch: kernel image+bss, KB
tm_app_kb:       resw 1          ; paint scratch: resident package, KB
tm_kernel_w:     resw 1          ; paint scratch: kernel segment width, px
tm_app_w:        resw 1          ; paint scratch: app segment width, px
tm_free_kb:      resw 1          ; paint scratch: free KB (clamped >= 0)

tm_numbuf:       resb 6          ; fm_utoa scratch: up to "100%",0
tm_buf1:         resb 32         ; "Kernel NNNNNK  App NNNNNK",0
tm_buf2:         resb 32         ; "Free NNNNNK / NNNNNK total",0

section .text
