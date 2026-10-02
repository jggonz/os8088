; =============================================================================
; Confirmed shutdown (SPEC.md 12.3.2), inside CTRL.DRV.
; DS stays KERNEL_SEG; all private state is CS-relative. No resident buffers.
; =============================================================================
section .modc
modc_e_shutdown:
    call sd_confirm
    or al, al
    jz .out
    call cp_flush_close_x       ; flush without dropping the executing module
    cmp byte [cp_wdirty], 0
    jne .out                   ; the existing settings toast explains failure
    call COLD_SEG:drv_shutdown_x ; workers must still be able to yield here
    call KERNEL_SEG:cw_gfx_lock ; no GUI drawing after this point
    mov byte [cur_barok], 0     ; the final wipe also covers the pointer, not just the bar
    mov bp, cur_unlazy          ; restore the cursor BEFORE wiping its background
    call KERNEL_SEG:cw_mem_disp
    mov bp, sched_stop
    call KERNEL_SEG:cw_mem_disp ; BIOS IRQ0, PIT, mouse and speaker restored
    cli
    mov al, 0xFE               ; only the BIOS timer may interrupt the halt
    out 0x21, al
    cmp byte [cpu_tier], CPU_8086
    je .motor
    mov al, 0xFF
    out 0xA1, al
.motor:
    mov dx, 0x3F2
    mov al, 0x0C               ; FDC enabled, both floppy motors off
    out dx, al
    sti
    call KERNEL_SEG:spw_vid_setmode ; clear the display and reset planar state
    mov ax, [vid_cw]
    shr ax, 1
    mov [cs:sd_l_cx], ax
    sub ax, SPL_SPINB*4
    and ax, 0xFFF8
    mov [cs:sd_l_spp], ax
    mov cl, 3
    shr ax, cl
    mov [cs:sd_l_spx], ax
    mov ax, [vid_ch]
    shr ax, 1
    sub ax, 42
    mov [cs:sd_l_spn], ax
    add ax, 58
    mov dx, ax
    mov cx, [cs:sd_l_cx]
    sub cx, 140
    mov si, sd_safe
    mov ax, 0x000F             ; startup splash's white on black
    call sd_text
    mov byte [cs:sd_last], 0xFF
.loop:
    call sd_clock
    mov cl, SPL_SPINSH
    shr ax, cl
    and al, 15
    cmp al, [cs:sd_last]
    je .idle
    mov [cs:sd_last], al
    call sd_spin
.idle:
    hlt                        ; wake on IRQ0; no scheduler or disk I/O
    jmp .loop
.out:
    retf

; A modal overlay owns the gfx lock, like menu tracking. It polls the same
; mouse state and BIOS keys as ui_task and keeps the pointer visible. Workers
; can finish while it yields; they cannot draw over the question.
sd_confirm:
    call KERNEL_SEG:cw_gfx_lock
    mov ax, [vid_cw]
    sub ax, 320
    shr ax, 1
    mov [cs:sd_left], ax
    mov bx, [vid_ch]
    sub bx, 96
    shr bx, 1
    mov [cs:sd_top], bx
    mov cx, ax
    add cx, 319
    mov dx, bx
    add dx, 95
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CBLACK
    call KERNEL_SEG:cw_gfx_frame
    mov cx, [cs:sd_left]
    add cx, 16
    mov dx, [cs:sd_top]
    add dx, 16
    mov si, sd_question
    mov ax, (CWHITE << 8) | CBLACK
    call sd_text
    add dx, 16
    mov si, sd_save
    call sd_text
    mov byte [cs:sd_down], 0
    mov byte [cs:sd_arm], 0
    mov byte [cs:sd_prev], 0
    mov al, 1
    call sd_button
    mov al, 2
    call sd_button
    mov bp, cursor_show
    call KERNEL_SEG:cw_mem_disp
    mov byte [cs:sd_active], 1
.poll:
    ; Redraw the pointer only when its hotspot actually moved.
    mov ax, [mouse_x]
    mov bx, [mouse_y]
    cmp ax, [cur_drawn_x]
    jne .move
    cmp bx, [cur_drawn_y]
    je .key
.move:
    mov bp, cur_move
    call KERNEL_SEG:cw_mem_disp
.key:
    mov ah, 1
    int 0x16
    jz .mouse
    xor ah, ah
    int 0x16
    cmp al, 27
    je .cancel
    cmp al, 13
    je .yes
.mouse:
    mov al, [mouse_btn]
    and al, 1
    cmp al, [cs:sd_prev]
    je .track
    mov [cs:sd_prev], al
    or al, al
    jz .release
    call sd_hit
    mov [cs:sd_arm], al
    mov [cs:sd_down], al
    call sd_rebuttons
    jmp .yield
.release:
    call sd_hit
    cmp al, [cs:sd_arm]
    jne .clear
    cmp al, 1
    je .yes
    cmp al, 2
    je .cancel
.clear:
    mov byte [cs:sd_arm], 0
    mov byte [cs:sd_down], 0
    call sd_rebuttons
.track:
    cmp byte [cs:sd_prev], 0
    je .yield
    call sd_hit
    cmp al, [cs:sd_arm]
    je .highlight
    xor al, al
.highlight:
    cmp al, [cs:sd_down]
    je .yield
    mov [cs:sd_down], al
    call sd_rebuttons
.yield:
    call KERNEL_SEG:cw_task_yield
    jmp .poll
.cancel:
    xor ax, ax
    jmp .done
.yes:
    mov ax, 1
.done:
    push ax
    mov bp, cursor_hide
    call KERNEL_SEG:cw_mem_disp
    mov di, ui_ev
    mov bp, evq_drain           ; modal clicks consumed, package wakes owed
    call KERNEL_SEG:cw_mem_disp
    call KERNEL_SEG:cw_wm_paint_all
    call KERNEL_SEG:cw_gfx_unlock
    mov byte [cs:sd_active], 0
    pop ax
    ret

sd_rebuttons:
    mov bp, cursor_hide
    call KERNEL_SEG:cw_mem_disp
    mov al, 1
    call sd_button
    mov al, 2
    call sd_button
    mov bp, cursor_show
    call KERNEL_SEG:cw_mem_disp
    ret

; Both paint and hit-testing use the shared control rectangle.
sd_rect:
    mov bx, os88ui_krect
    mov cx, [cs:sd_left]
    add cx, 48
    mov si, sd_shutdown
    cmp al, 1
    je .first
    add cx, 128
    mov si, sd_cancel
.first:
    mov [bx], cx
    add cx, 95
    mov [bx+4], cx
    mov dx, [cs:sd_top]
    add dx, 60
    mov [bx+2], dx
    add dx, 23
    mov [bx+6], dx
    ret

sd_button:
    mov di, OS88UI_FILL
    cmp al, 1
    jne .flags
    or di, OS88UI_DEF
.flags:
    cmp al, [cs:sd_down]
    jne .up
    or di, OS88UI_DOWN
.up:
    call sd_rect
    call cp_stage
    call COLD_SEG:os88ui_btn_f
    ret

sd_hit:
    mov al, 1
.again:
    call sd_rect
    mov cx, [mouse_x]
    mov dx, [mouse_y]
    cmp cx, [bx]
    jb .next
    cmp cx, [bx+4]
    ja .next
    cmp dx, [bx+2]
    jb .next
    cmp dx, [bx+6]
    jbe .out
.next:
    inc al
    cmp al, 3
    jb .again
    xor al, al
.out:
    ret

sd_text:
    push ds
    push cs
    pop ds
    call KERNEL_SEG:apic_font_run
    pop ds
    ret

sd_spinadr:
    mov ax, [cs:sd_l_spn]
    call KERNEL_SEG:spw_gfx_rowbase
    add ax, [cs:sd_l_spx]
    mov di, ax
    mov es, [vid_seg]
    ret

OS88_SPINNER sd, 0

sd_question db 'Shut down os8088?', 0
sd_save     db 'Save your work before continuing.', 0
sd_shutdown db 'Shut Down', 0
sd_cancel   db 'Cancel', 0
sd_safe     db 'You can now turn off your computer.', 0
sd_left     dw 0
sd_top      dw 0
sd_active   db 0
sd_arm      db 0
sd_down     db 0
sd_prev     db 0
sd_last     db 0
sd_l_cx     dw 0
sd_l_spn    dw 0
sd_l_spx    dw 0
sd_l_spp    dw 0
sd_radd     dw 0
sd_rwbit    dw 0
sd_rwfix    dw 0
sd_cos      dw 64
sd_rbar     db 0
sd_rdig     db 0
sd_rowa     times SPL_SPINB db 0
sd_rowb     times SPL_SPINB db 0
sd_cos_tab  db 64, 59, 45, 24, 0, -24, -45, -59
            db -64, -59, -45, -24, 0, 24, 45, 59
