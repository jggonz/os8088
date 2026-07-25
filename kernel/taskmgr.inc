; =============================================================================
; jop - taskmgr.inc
;
; Built-in Task Manager window.  The large CPU meter is deliberately labelled
; "Background CPU activity": task 0 is the busy-polling UI, not an idle task,
; so delta(sch_cpu_busy)/delta(sch_cpu_total) is the honest aggregate metric.
; Per-task percentages are scheduled PIT-share over the same sample.
;
; RAM total is conventional/usable KB returned by BIOS int 12h during
; taskmgr_init (task 0 only).  Used RAM is the kernel text+bss footprint plus
; the validated resident package image+bss, when one exists.  VGA RAM is not
; counted.
;
; Integration contract:
;   - include after loader.inc (forward references to final KTEXT/KBSS equs
;     are intentional);
;   - call taskmgr_init once after loader_init;
;   - show [taskmgr_win] with wm_show under gfx_lock from menu dispatch.
; The updater consumes the fourth/final scheduler slot in the stock build.
; If task_spawn fails, the window remains usable but its sample stays static.
; =============================================================================

TASKMGR_W          equ 380
TASKMGR_H          equ 205
TASKMGR_BAR_W      equ 344
TASKMGR_SAMPLE_TK  equ 18

; -----------------------------------------------------------------------------
; taskmgr_init - create the hidden window and start its sampler
; in/out: nothing (all registers preserved; flags clobbered)
; BIOS int 12h is used here only, while this routine runs on task 0.
; -----------------------------------------------------------------------------
taskmgr_init:
    push ax
    push bx
    push cx
    push di
    push si
    push es

    push ds
    pop es
    xor ax, ax
    mov di, taskmgr_bss_start
    mov cx, taskmgr_bss_end - taskmgr_bss_start
    rep stosb                   ; .bss is not loader-zeroed: initialize all

    int 0x12                    ; AX = conventional/usable memory, KB
    mov [taskmgr_total_kb], ax

    pushf                       ; coherent initial counter snapshot
    cli
    mov ax, [sch_cpu_total]
    mov [taskmgr_last_total], ax
    mov ax, [sch_cpu_busy]
    mov [taskmgr_last_busy], ax
    mov si, sch_task_cpu
    mov di, taskmgr_last_cpu
    mov cx, MAX_TASKS
.last:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .last
    popf

    mov si, taskmgr_template
    call wm_create
    mov [taskmgr_win], bx       ; created hidden

    mov ax, taskmgr_task
    call task_spawn
    jc .done
    mov byte [taskmgr_live], 1
.done:
    pop es
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret

taskmgr_template:
    dw 130, 96, TASKMGR_W, TASKMGR_H
    dw taskmgr_title, taskmgr_paint, 0, 0

taskmgr_title:       db 'Task Manager', 0
taskmgr_cpu_label:   db 'Background CPU activity', 0
taskmgr_used_label:  db 'RAM used by jop:', 0
taskmgr_total_label: db 'Conventional RAM total:', 0
taskmgr_kb_label:    db 'KB', 0
taskmgr_head_task:   db 'Task', 0
taskmgr_head_state:  db 'State', 0
taskmgr_head_cpu:    db 'CPU share', 0
taskmgr_name_ui:     db 'System UI', 0
taskmgr_name_clock:  db 'Clock', 0
taskmgr_name_bounce: db 'Bounce', 0
taskmgr_name_self:   db 'Task Manager', 0
taskmgr_name_other:  db 'Kernel task', 0
taskmgr_state_run:   db 'Running', 0
taskmgr_state_ready: db 'Ready', 0
taskmgr_state_sleep: db 'Sleeping', 0

; -----------------------------------------------------------------------------
; taskmgr_task - one-second sampler/live painter (task entry; never returns)
; -----------------------------------------------------------------------------
taskmgr_task:
.loop:
    mov ax, TASKMGR_SAMPLE_TK
    call task_sleep
    call taskmgr_sample
    call taskmgr_ram_sample

    call gfx_lock
    call taskmgr_publish
    mov bx, [taskmgr_win]
    test bx, bx
    jz .unlock
    test word [bx+W_FLAGS], 2
    jz .unlock
    call wm_obscured
    jc .unlock
    call taskmgr_repaint
.unlock:
    call gfx_unlock
    jmp .loop

; -----------------------------------------------------------------------------
; taskmgr_sample - atomically snapshot scheduler counters, then derive shares
; -----------------------------------------------------------------------------
taskmgr_sample:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    pushf
    cli
    mov ax, [sch_cpu_total]
    mov [taskmgr_new_total], ax
    mov ax, [sch_cpu_busy]
    mov [taskmgr_new_busy], ax
    mov al, [sch_cur]
    mov [taskmgr_next_cur], al
    xor di, di                  ; byte slot offset
    mov si, sch_tasks
.snap:
    mov ax, di
    shl ax, 1                   ; word-array offset
    mov bx, ax
    mov ax, [bx+sch_task_cpu]
    mov [bx+taskmgr_new_cpu], ax
    mov ax, [bx+sch_task_entry]
    mov [bx+taskmgr_next_entry], ax
    mov al, [si+T_STATE]
    mov [di+taskmgr_next_state], al
    add si, T_SIZE
    inc di
    cmp di, MAX_TASKS
    jb .snap
    popf

    mov bp, [taskmgr_new_total]
    sub bp, [taskmgr_last_total] ; wrap-safe sample denominator
    mov ax, [taskmgr_new_total]
    mov [taskmgr_last_total], ax

    mov ax, [taskmgr_new_busy]
    mov dx, ax
    sub ax, [taskmgr_last_busy]
    mov [taskmgr_last_busy], dx
    mov bx, bp
    call taskmgr_ratio
    mov [taskmgr_next_activity], al

    xor di, di
.shares:
    mov si, di
    shl si, 1
    mov ax, [si+taskmgr_new_cpu]
    mov dx, ax
    sub ax, [si+taskmgr_last_cpu]
    mov [si+taskmgr_last_cpu], dx
    mov bx, bp
    call taskmgr_ratio
    mov [di+taskmgr_next_pct], al
    inc di
    cmp di, MAX_TASKS
    jb .shares

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; in: AX=numerator, BX=denominator; out AL=0..100; clobbers AX,CX,DX only
taskmgr_ratio:
    or bx, bx
    jz .zero
    xor dx, dx
    mov cx, 100
    mul cx
    div bx
    cmp ax, 100
    jbe .done
    mov ax, 100
.done:
    ret
.zero:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; taskmgr_ram_sample - stage current jop footprint in whole KB
; -----------------------------------------------------------------------------
taskmgr_ram_sample:
    push ax
    push bx
    push cx
    mov ax, KTEXT_SIZE + KBSS_SIZE
    mov bx, [ld_appwin]
    or bx, bx
    jz .round
    add ax, [APP_LOAD_OFF+LD_H_IMG]
    add ax, [APP_LOAD_OFF+LD_H_BSS]
.round:
    add ax, 1023
    mov cl, 10
    shr ax, cl
    mov [taskmgr_next_used_kb], ax
    pop cx
    pop bx
    pop ax
    ret

; Publish the staged sample only while holding gfx_lock, so wm_paint_all
; cannot observe half of a new display snapshot.
taskmgr_publish:
    push ax
    push bx
    push cx
    push si
    xor si, si
.slot:
    mov al, [si+taskmgr_next_pct]
    mov [si+taskmgr_view_pct], al
    mov al, [si+taskmgr_next_state]
    mov [si+taskmgr_view_state], al
    mov ax, si
    shl ax, 1
    mov bx, ax
    mov ax, [bx+taskmgr_next_entry]
    mov [bx+taskmgr_view_entry], ax
    inc si
    cmp si, MAX_TASKS
    jb .slot
    mov al, [taskmgr_next_cur]
    mov [taskmgr_view_cur], al
    mov al, [taskmgr_next_activity]
    mov [taskmgr_view_activity], al
    mov ax, [taskmgr_next_used_kb]
    mov [taskmgr_used_kb], ax
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; taskmgr_repaint - white-fill this window's content and redraw it
; in BX=window ptr, gfx lock held; all registers preserved
; -----------------------------------------------------------------------------
taskmgr_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx
    call wm_content             ; AX=x1, DX=y1
    push dx
    mov cx, [si+W_X]
    add cx, [si+W_W]
    sub cx, 2
    mov dx, [si+W_Y]
    add dx, [si+W_H]
    sub dx, 2
    pop bx
    mov byte [gfx_color], CWHITE
    call gfx_fill
    call taskmgr_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; taskmgr_paint - W_PAINT: meters, RAM figures and active task rows
; in SI=window ptr, gfx lock held; all registers preserved
; -----------------------------------------------------------------------------
taskmgr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, si
    mov byte [gfx_color], CBLACK

    mov cx, [bp+W_X]
    add cx, 16
    mov dx, [bp+W_Y]
    add dx, 25
    mov si, taskmgr_cpu_label
    call font_str
    mov al, [taskmgr_view_activity]
    call taskmgr_format_pct
    mov si, taskmgr_num_buf
    mov cx, [bp+W_X]
    add cx, 304
    call font_str

    mov al, [taskmgr_view_activity]
    mov bx, [bp+W_Y]
    add bx, 38
    call taskmgr_meter          ; AL=percent, BP=win, BX=y

    mov cx, [bp+W_X]
    add cx, 16
    mov dx, [bp+W_Y]
    add dx, 61
    mov si, taskmgr_used_label
    call font_str
    mov ax, [taskmgr_used_kb]
    mov cx, [bp+W_X]
    add cx, 216
    call taskmgr_draw_kb

    mov cx, [bp+W_X]
    add cx, 16
    mov dx, [bp+W_Y]
    add dx, 73
    mov si, taskmgr_total_label
    call font_str
    mov ax, [taskmgr_total_kb]
    mov cx, [bp+W_X]
    add cx, 216
    call taskmgr_draw_kb

    mov ax, [taskmgr_used_kb]
    mov bx, [taskmgr_total_kb]
    call taskmgr_ratio
    mov bx, [bp+W_Y]
    add bx, 87
    call taskmgr_meter

    mov dx, [bp+W_Y]
    add dx, 112
    mov cx, [bp+W_X]
    add cx, 16
    mov si, taskmgr_head_task
    call font_str
    mov cx, [bp+W_X]
    add cx, 176
    mov si, taskmgr_head_state
    call font_str
    mov cx, [bp+W_X]
    add cx, 280
    mov si, taskmgr_head_cpu
    call font_str

    mov ax, [bp+W_X]
    add ax, 16
    mov bx, [bp+W_X]
    add bx, 360
    mov dx, [bp+W_Y]
    add dx, 122
    call gfx_hline

    xor di, di
.row:
    cmp byte [di+taskmgr_view_state], 0
    je .next
    mov dx, di
    mov cl, 4
    shl dx, cl
    add dx, [bp+W_Y]
    add dx, 129

    mov ax, di
    shl ax, 1
    mov bx, ax
    mov ax, [bx+taskmgr_view_entry]
    call taskmgr_name           ; out SI
    mov cx, [bp+W_X]
    add cx, 16
    call font_str

    mov al, [di+taskmgr_view_state]
    cmp al, 2
    je .sleeping
    mov al, [taskmgr_view_cur]
    xor ah, ah
    cmp ax, di
    je .running
    mov si, taskmgr_state_ready
    jmp .state
.running:
    mov si, taskmgr_state_run
    jmp .state
.sleeping:
    mov si, taskmgr_state_sleep
.state:
    mov cx, [bp+W_X]
    add cx, 176
    call font_str

    mov al, [di+taskmgr_view_pct]
    call taskmgr_format_pct
    mov si, taskmgr_num_buf
    mov cx, [bp+W_X]
    add cx, 304
    call font_str
.next:
    inc di
    cmp di, MAX_TASKS
    jb .row

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; taskmgr_meter - framed 344px-wide meter; AL percent, BP win, BX top
taskmgr_meter:
    push ax
    push bx
    push cx
    push dx
    push si
    xor ah, ah
    mov si, ax                  ; preserve percentage across frame drawing
    mov ax, [bp+W_X]
    add ax, 16
    mov cx, ax
    add cx, TASKMGR_BAR_W+1
    mov dx, bx
    add dx, 12
    mov byte [gfx_color], CBLACK
    call gfx_frame
    mov ax, si
    mov si, TASKMGR_BAR_W
    mul si
    mov si, 100
    div si
    or ax, ax
    jz .done
    mov cx, [bp+W_X]
    add cx, 17
    xchg ax, cx                 ; AX=inner left, CX=width
    add cx, ax
    dec cx
    inc bx
    mov dx, bx
    add dx, 10
    call gfx_fill_gray
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; in AX=value, CX=x, DX=y; draws decimal plus KB
taskmgr_draw_kb:
    push ax
    push cx
    push dx
    push si
    push di
    push cx                     ; formatter uses CX for digit count
    push dx                     ; and DX for division
    mov di, taskmgr_num_buf
    call taskmgr_format_u16
    pop dx
    pop cx
    mov si, taskmgr_num_buf
    call font_str
    call font_width
    add cx, ax
    add cx, 8
    mov si, taskmgr_kb_label
    call font_str
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; in AX=entry; out SI=name; clobbers flags only
taskmgr_name:
    cmp ax, ui_task
    je .ui
    cmp ax, app_clock_task
    je .clock
    cmp ax, app_bounce_task
    je .bounce
    cmp ax, taskmgr_task
    je .self
    mov si, taskmgr_name_other
    ret
.ui:
    mov si, taskmgr_name_ui
    ret
.clock:
    mov si, taskmgr_name_clock
    ret
.bounce:
    mov si, taskmgr_name_bounce
    ret
.self:
    mov si, taskmgr_name_self
    ret

; in AL=0..100; output taskmgr_num_buf = right-aligned "  0%"
taskmgr_format_pct:
    mov byte [taskmgr_num_buf], ' '
    mov byte [taskmgr_num_buf+1], ' '
    cmp al, 100
    jne .under
    mov word [taskmgr_num_buf], '10'
    mov byte [taskmgr_num_buf+2], '0'
    jmp .tail
.under:
    aam
    or ah, ah
    jz .ones
    add ah, '0'
    mov [taskmgr_num_buf+1], ah
.ones:
    add al, '0'
    mov [taskmgr_num_buf+2], al
.tail:
    mov byte [taskmgr_num_buf+3], '%'
    mov byte [taskmgr_num_buf+4], 0
    ret

; in AX=value, DI=buffer; output ASCIIZ; clobbers AX,BX,CX,DX,DI
taskmgr_format_u16:
    xor cx, cx
    mov bx, 10
.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .divide
.emit:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .emit
    mov al, 0
    mov [di], al
    inc di
    ret

; -----------------------------------------------------------------------------
section .bss
taskmgr_bss_start:
taskmgr_win:           resw 1
taskmgr_total_kb:      resw 1
taskmgr_used_kb:       resw 1
taskmgr_live:          resb 1
taskmgr_last_total:    resw 1
taskmgr_last_busy:     resw 1
taskmgr_last_cpu:      resw MAX_TASKS
taskmgr_new_total:     resw 1
taskmgr_new_busy:      resw 1
taskmgr_new_cpu:       resw MAX_TASKS
taskmgr_next_pct:      resb MAX_TASKS
taskmgr_next_state:    resb MAX_TASKS
taskmgr_next_entry:    resw MAX_TASKS
taskmgr_next_cur:      resb 1
taskmgr_next_activity: resb 1
taskmgr_next_used_kb:  resw 1
taskmgr_view_pct:      resb MAX_TASKS
taskmgr_view_state:    resb MAX_TASKS
taskmgr_view_entry:    resw MAX_TASKS
taskmgr_view_cur:      resb 1
taskmgr_view_activity: resb 1
taskmgr_num_buf:       resb 8
taskmgr_bss_end:
section .text
