; =============================================================================
; os8088 - apps/audio/apcb.inc
;
; The Audio Player's window callbacks (SPEC.md 86.7). All near procs ending in
; a near `ret` (the header dispatcher, SPEC.md 20.1). The engine and the
; playlist do the work; these route to it and repaint.
;
;   ap_paint     W_PAINT       full content redraw (content pre-white'd)
;   ap_onclick   W_ONCLICK     hit-test the transport buttons, act, repaint
;   ap_onkey     W_ONKEY       Space/Enter play-pause, S stop, N/P next/prev
;   ap_oncmd     menu          File > Open / Clear ; Play > the 7 commands
;   ap_about     About         a one-line credit toast
;   ap_onwake    WM_ONWAKE     the disk-refill service + track-end reap + the
;                              launch-document handover (SPEC.md 54.10)
;   ap_onclose   WM_ONCLOSE    stop the DSP cleanly, allow the close
;   ap_onfile    fdlg done     add the chosen file and (if idle) start it
; =============================================================================

    APBUF ap_dlgname, 14
apu_cred: db 'Audio Player - WAV PCM8 / IMA ADPCM', 0

; --- apu_repaint - white-fill our live content, then draw it -----------------
apu_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_layout
    call apu_origin
    mov al, 15
    call OSAPI_SET_COLOR
    mov ax, [apu_ox]
    mov bx, [apu_oy]
    mov cx, ax
    add cx, [apu_cw]
    dec cx
    mov dx, bx
    add dx, [apu_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov al, 0
    call OSAPI_SET_COLOR
    call apu_draw
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
ap_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_draw
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
ap_onclick:                        ; CX = x, DX = y (SCREEN); SI = window
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ap_reap
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov [apu_ox], ax
    mov [apu_oy], dx
    pop dx
    pop cx
    sub cx, [apu_ox]
    sub dx, [apu_oy]
    call apu_layout
    call apu_hit                    ; AX = AP_B_* or AP_B_NONE
    cmp al, AP_B_NONE
    je .rep
    call ap_dispatch_btn
.rep:
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ap_dispatch_btn - AL = AP_B_*
ap_dispatch_btn:
    cmp al, AP_B_PREV
    jne .n0
    call ap_cmd_prev
    ret
.n0:
    cmp al, AP_B_PLAY
    jne .n1
    call ap_cmd_play
    ret
.n1:
    cmp al, AP_B_PAUSE
    jne .n2
    call ap_cmd_pause
    ret
.n2:
    cmp al, AP_B_STOP
    jne .n3
    call ap_cmd_stop
    ret
.n3:
    cmp al, AP_B_NEXT
    jne .n4
    call ap_cmd_next
    ret
.n4:
    cmp al, AP_B_SHUF
    jne .n5
    call ap_cmd_shuffle
    ret
.n5:
    cmp al, AP_B_REP
    jne .done
    call ap_cmd_repeat
.done:
    ret

; =============================================================================
ap_onkey:                          ; AL = ascii, AH = scancode; SI = window
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    call ap_reap
    pop ax

    cmp ah, KSC_LEFT
    je .prev
    cmp ah, KSC_RIGHT
    je .next
    cmp al, ' '
    je .toggle
    cmp al, 0x0D
    je .toggle
    or al, 0x20                     ; fold case
    cmp al, 's'
    je .stop
    cmp al, 'n'
    je .next
    cmp al, 'p'
    je .prev
    jmp .rep
.toggle:
    cmp byte [ap_state], AP_ST_PLAY
    je .dopause
    call ap_cmd_play
    jmp .rep
.dopause:
    call ap_cmd_pause
    jmp .rep
.stop:
    call ap_cmd_stop
    jmp .rep
.next:
    call ap_cmd_next
    jmp .rep
.prev:
    call ap_cmd_prev
.rep:
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
ap_oncmd:                          ; AL = item, AH = menu; SI = window
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    call ap_reap
    pop ax
    or ah, ah
    jnz .playmenu

    ; --- File menu -------------------------------------------------------
    cmp al, 0
    jne .clear
    ; Open...
    mov al, FDLG_OPEN
    mov bx, [ap_win]
    mov di, ap_onfile
    xor si, si
    call OSAPI_FILE_DLG
    jmp .rep
.clear:
    call ap_stream_close
    call apl_clear
    mov byte [ap_state], AP_ST_STOP
    mov word [ap_msg], ap_s_ready
    jmp .rep

.playmenu:
    ; item 0..6 map directly onto the AP_B_* command ids
    call ap_dispatch_btn
.rep:
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
ap_about:                          ; SI = window; gfx lock held
    push si
    push cx
    push ds
    pop es
    mov si, apu_cred
    xor cx, cx                      ; default ~3s
    call OSAPI_TOAST
    pop cx
    pop si
    ret

; =============================================================================
; ap_onwake - UI task, NO gfx lock (SPEC.md 74.1)
; =============================================================================
ap_onwake:                         ; SI = window
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; 1. the disk-refill handshake (time-critical: do it first)
    cmp byte [ap_req], FR_READ
    jne .norefill
    call ap_refill_chunk
    mov byte [ap_req], FR_NONE      ; clear LAST (SPEC.md 77.1)
    call ap_la_avail
    cmp ax, AP_LA_LOW
    jae .norefill
    cmp byte [ap_src_done], 0
    jne .norefill
    mov bx, [ap_win]
    call OSAPI_WM_WAKE              ; still low - come back next pass
.norefill:

    ; 2. a track that ended -> advance / stop
    cmp byte [ap_ended], 0
    je .noend
    call OSAPI_GFX_LOCK
    call ap_autoadvance
    call apu_repaint
    call OSAPI_GFX_UNLOCK
.noend:

    ; 3. the launch document, once (SPEC.md 54.10)
    cmp byte [ap_arg_pending], 0
    je .noarg
    mov byte [ap_arg_pending], 0
    mov si, ap_arg_name
    mov dx, [ap_arg_dir]
    mov bl, [ap_arg_vol]
    call apl_add                    ; CF if the list is full
    mov al, [apl_n]
    or al, al
    jz .noarg
    dec al
    mov [apl_cur], al
    call OSAPI_GFX_LOCK
    call ap_open_track
    call apu_repaint
    call OSAPI_GFX_UNLOCK
.noarg:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
ap_onclose:                        ; SI = window; gfx lock held
    call ap_stream_close
    clc                            ; allow the close (no unsaved state)
    ret

; =============================================================================
; ap_onfile - the Standard File dialog's completion proc
; in:  AL = mode, SI = window, DI = chosen name (KERNEL_SEG), DX:CX = size
;      UI task, gfx lock HELD (SPEC.md 38.6)
; =============================================================================
ap_onfile:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; copy the name out of the kernel's buffer (valid only during this call)
    mov si, di
    mov di, ap_dlgname
    mov cx, 13
.cn:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .cnd
    inc si
    inc di
    dec cx
    jnz .cn
.cnd:
    mov byte [ap_dlgname+13], 0
    call OSAPI_FILE_HERE            ; DX = current dir cluster, BL = drive
    mov si, ap_dlgname
    call apl_add                    ; SI = name, DX = dir, BL = vol
    mov al, [apl_n]
    or al, al
    jz .rep
    ; if nothing is playing, make this the current track and start it
    cmp byte [ap_state], AP_ST_PLAY
    je .rep
    mov al, [apl_n]
    dec al
    mov [apl_cur], al
    call ap_stream_close
    call ap_open_track
.rep:
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
