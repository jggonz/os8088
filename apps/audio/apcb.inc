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
;   ap_about     About         a white card over the content (name/version/blurb)
;   ap_onwake    WM_ONWAKE     the disk-refill service + track-end reap + the
;                              launch-document handover (SPEC.md 54.10)
;   ap_onclose   WM_ONCLOSE    stop the DSP cleanly, allow the close
;   ap_onfile    fdlg done     add the chosen file and (if idle) start it
; =============================================================================

    APBUF ap_dlgname, 14

; --- apu_repaint - white-fill our live content, then draw it -----------------
apu_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    APCNT apc_uipaint
    mov bx, [ap_win]
    call OSAPI_WM_CLIP_SET          ; SPEC.md 11.3: outside W_PAINT the gfx_*
    jc .done                        ; calls are absolute - without the region
                                    ; armed this paints over whatever is on
                                    ; top of us (the Open dialog, found on the
                                    ; glass). CF=1: not a pixel of us shows
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
.done:
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
    APCNT apc_uipaint
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
    cmp byte [ap_abon], 0
    je .noab
    mov byte [ap_abon], 0          ; a click anywhere dismisses the About card
    jmp .rep
.noab:
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

    cmp byte [ap_abon], 0
    je .noab
    mov byte [ap_abon], 0          ; any key dismisses the About card
    jmp .rep
.noab:
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
    cmp al, 'd'
    je .diag
    jmp .rep
.diag:
    xor byte [ap_diag], 1
%ifdef APROF
    cmp byte [ap_diag], 0
    je .rep
    mov byte [apdg_req], 1        ; the dump does file I/O - ap_onwake owns that
    mov bx, [ap_win]              ; (W_ONKEY holds the gfx lock; WM_ONWAKE does not)
    call OSAPI_WM_WAKE
%endif
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

    ; --- File menu: Open / Clear / Diagnostics / Exit -------------------
    cmp al, AP_CMD_OPEN
    jne .n_open
    mov al, FDLG_OPEN
    mov bx, [ap_win]
    mov di, ap_onfile
    xor si, si
    call OSAPI_FILE_DLG
    jmp .rep
.n_open:
    cmp al, AP_CMD_CLEAR
    jne .n_clear
    call ap_stream_close
    call apl_clear
    mov byte [ap_state], AP_ST_STOP
    mov word [ap_msg], ap_s_ready
    jmp .rep
.n_clear:
    cmp al, AP_CMD_DIAG
    jne .n_diag
    xor byte [ap_diag], 1
    jmp .rep
.n_diag:
    cmp al, AP_CMD_EXIT
    jne .rep
    call ap_stream_close
    mov bx, [ap_win]
    call OSAPI_WM_CLOSE             ; SPEC.md 75.2 - deferred to the next UI pass
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
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [ap_abon], 1          ; apu_draw draws the card last while this is set
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    APCNT apc_onwake

    ; 0. the settled first repaint (SPEC.md 86.7): the loader shows the window
    ;    after the entry proc, so the first W_PAINT can beat wm_fit.
    cmp byte [ap_first_paint], 0
    je .no_first
    mov byte [ap_first_paint], 0
    call OSAPI_GFX_LOCK
    call apu_repaint
    call OSAPI_GFX_UNLOCK
.no_first:

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

    ; 3. the launch document, once (SPEC.md 54.10) - double-click means "play
    ;    this now", so AP_ADD_CUR | AP_ADD_PLAY.
    cmp byte [ap_arg_pending], 0
    je .noarg
    mov byte [ap_arg_pending], 0
    call OSAPI_GFX_LOCK
    mov si, ap_arg_name
    mov dx, [ap_arg_dir]
    mov bl, [ap_arg_vol]
    mov al, AP_ADD_CUR | AP_ADD_PLAY
    call ap_add_file
    call apu_repaint
    call OSAPI_GFX_UNLOCK
.noarg:

    ; 4. a would-be second instance may have queued a document for us
    ;    (SPEC.md 86.11.1). OFF by default: the poll has to REMOUNT the system
    ;    root to read APQUEUE.DAT (OSAPI_FILE_GOTO), and on a 4.77 MHz XT that
    ;    is seconds - a playing ADPCM track polls often enough that the machine
    ;    spends half its time remounting. Build with -DAP_HANDOFF to re-enable.
%ifdef AP_HANDOFF
    call ap_que_poll
    jc .noque
    call OSAPI_GFX_LOCK
    call apu_repaint
    call OSAPI_GFX_UNLOCK
.noque:
%endif
%ifdef APROF
    ; 5. the Diagnostics 'D' key asked for a counter dump (file I/O, no lock)
    cmp byte [apdg_req], 0
    je .nodump
    mov byte [apdg_req], 0
    call ap_diag_dump
.nodump:
%endif
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
    ; Open adds and selects; it starts the track ONLY if nothing is playing
    ; (SPEC.md 86.3) - it must not interrupt the current song.
    mov al, AP_ADD_CUR | AP_ADD_IDLE
    call ap_add_file
.rep:
    call apu_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
