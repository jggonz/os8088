; =============================================================================
; os8088 - apps/audio/apwork.inc
;
; The Audio Player's worker task (SPEC.md 86.5 / 20.6). One loop:
;
;   OSAPI_TASK_ALIVE  (dies here when the close box is clicked)
;   OSAPI_TASK_SLEEP 1
;   ap_feed           lock-free: poll verb 3, request a disk refill if the
;                     look-ahead ring is low (OSAPI_WM_WAKE -> ap_onwake),
;                     then decode+stage+feed up to AP_MAXFEED halves
;   ap_worker_draw    one short gfx-lock hold: the elapsed time and the
;                     progress bar, and only while visible and playing
;
; [ap_mixing] brackets the feed pass (set first, cleared last); ap_stream_close
; on the UI task drains it before touching engine state (SPEC.md 45.2).
; =============================================================================

    APB apw_halves                 ; halves fed this pass
    APD ap_played                  ; total PCM8 bytes the DSP has consumed
    APW ap_consumed_prev           ; last polled verb-3 count (for the delta)
    APD ap_total_pcm               ; total PCM8 bytes in the track (0 = unknown)

; =============================================================================
ap_worker:
.loop:
    mov bx, [ap_win]
    call OSAPI_TASK_ALIVE
    mov ax, 1
    call OSAPI_TASK_SLEEP
    call ap_reap_worker            ; if a track ended, kick the UI to advance
    call ap_feed
    call ap_worker_draw
    jmp .loop

; ap_reap_worker - the worker cannot close a stream (verb 2 is UI-only); when
; it has latched ap_ended it just keeps kicking the UI until ap_onwake reaps.
ap_reap_worker:
    cmp byte [ap_ended], 0
    je .out
    call ap_kick_ui
.out:
    ret

; ap_kick_ui - post one EVT_WAKE for our window (worker/ISR-safe)
ap_kick_ui:
    push ax
    push bx
    mov bx, [ap_win]
    call OSAPI_WM_WAKE
    pop bx
    pop ax
    ret

; =============================================================================
; ap_feed - the feed pass (SPEC.md 86.5).  Worker context, lock-free.
; =============================================================================
ap_feed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [ap_mixing], 1        ; FIRST - before any guard
    cmp byte [ap_sopen], 0
    je .out
    cmp byte [ap_ended], 0
    jne .out

    mov al, 3
    mov ah, [ap_hand]
    call OSAPI_SND_STREAM          ; AX = state, DX = consumed (free-running)
    ; accumulate the consumed delta into ap_played
    push ax
    mov ax, dx
    sub ax, [ap_consumed_prev]
    mov [ap_consumed_prev], dx
    mov [ap_consumed], dx
    xor dx, dx
    add [ap_played], ax
    adc [ap_played+2], dx
    pop ax

    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    cmp byte [ap_paused], 0
    jne .out                       ; paused: stop feeding (stream underrun-pauses)

    cmp byte [ap_lasthalf], 0
    je .canfeed
    ; the final half is already staged - just wait for the DSP to drain it
    mov ax, [ap_total]
    cmp ax, [ap_consumed]
    jne .out
    mov byte [ap_ended], 1
    call ap_kick_ui
    jmp .out

.canfeed:
    call ap_la_avail
    cmp ax, AP_LA_LOW
    jae .nofill
    cmp byte [ap_src_done], 0
    jne .nofill
    cmp byte [ap_req], FR_NONE
    jne .nofill
    mov byte [ap_req], FR_READ
    call ap_kick_ui
.nofill:
    mov byte [apw_halves], 0
.fill:
    cmp byte [ap_sopen], 0
    je .out
    cmp byte [ap_paused], 0
    jne .out
    mov al, [apw_halves]
    cmp al, AP_MAXFEED
    jae .out
    mov ax, [ap_total]
    sub ax, [ap_consumed]
    mov bx, [ap_ring]
    sub bx, AP_HALF
    cmp ax, bx
    jae .out                       ; the SND ring is full enough
    call ap_stage_half            ; AX = 0 underrun / 1 half / 2 final half
    or ax, ax
    jz .out
    push ax
    mov cx, [ap_total]
    mov al, 1
    mov ah, [ap_hand]
    call OSAPI_SND_STREAM         ; verb 1: publish the new valid total
    pop ax
    inc byte [apw_halves]
    cmp ax, 2
    je .out
    jmp .fill

.dead:
    mov byte [ap_ended], 1
    call ap_kick_ui
.out:
    mov byte [ap_mixing], 0        ; LAST
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ap_worker_draw - the worker's one gfx-lock hold: elapsed time + progress
;                  bar, redrawn only while the window is visible and playing.
; =============================================================================
ap_worker_draw:
    cmp byte [ap_state], AP_ST_PLAY
    jne .out
    ; recompute elapsed seconds from ap_played / rate
    mov ax, [ap_played]
    mov dx, [ap_played+2]
    mov bx, [ap_rate]
    or bx, bx
    jz .noel
    cmp dx, bx
    jae .noel
    div bx
    mov [ap_elapsed_s], ax
.noel:
    mov bx, [ap_win]
    call OSAPI_WM_OBSCURED         ; CF = 1 fully covered / hidden
    jc .out
    call OSAPI_GFX_LOCK
    mov bx, [ap_win]
    call OSAPI_WM_CLIP_SET         ; CF = 1 nothing visible
    jc .unlock
    call apu_draw_dyn
.unlock:
    call OSAPI_GFX_UNLOCK
.out:
    ret
