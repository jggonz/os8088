; =============================================================================
; os8088 - apps/audio/apwork.inc
;
; The Audio Player's worker task (SPEC.md 86.5 / 20.6). One loop:
;
;   OSAPI_TASK_ALIVE   (dies here when the close box is clicked)
;   ap_worker_sleep    AP_SLP_FEED ticks while feeding, AP_SLP_IDLE otherwise
;   ap_feed            poll verb 3 (cheap, for the clock); STAGE/FEED only when
;                      the ring lead has actually dropped below the threshold
;   ap_worker_draw     one gfx-lock hold - AND ONLY when the elapsed second or
;                      the progress-bar pixel changed, and at most every
;                      AP_DRAW_TICKS (~2/s). SPEC.md 86.5.1: the old code
;                      repainted MM:SS + the bar ~18x/s onto identical pixels.
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
    APCNT apc_iter
    call ap_worker_sleep
    call ap_reap_worker
    call ap_feed
    call ap_worker_draw
    jmp .loop

; ap_worker_sleep - short while feeding, long while idle. A stopped or paused
; player has no ring to starve, so it need not wake 18x/s.
ap_worker_sleep:
    push ax
    mov ax, AP_SLP_IDLE
    cmp byte [ap_sopen], 0
    je .go
    cmp byte [ap_paused], 0
    jne .go
    cmp byte [ap_ended], 0
    jne .go
    mov ax, AP_SLP_FEED
.go:
    call OSAPI_TASK_SLEEP
    pop ax
    ret

; ap_reap_worker - the worker cannot close a stream (verb 2 is UI-only); when it
; has latched ap_ended it kicks the UI to advance - but at most every
; AP_REAP_TICKS, not every pass (SPEC.md 86.5.1).
ap_reap_worker:
    cmp byte [ap_ended], 0
    je .out
    push ax
    call OSAPI_GET_TICKS
    push ax
    sub ax, [ap_reap_kick_t]
    cmp ax, AP_REAP_TICKS
    pop ax
    jb .pop
    mov [ap_reap_kick_t], ax
    call ap_kick_ui
.pop:
    pop ax
.out:
    ret

; ap_kick_ui - post one EVT_WAKE for our window (worker/ISR-safe)
ap_kick_ui:
    push ax
    push bx
    APCNT apc_wake
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
    APCNT apc_status
    ; accumulate the consumed delta into ap_played (the elapsed clock's source)
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
    cmp ax, SND_ST_UNDER
    jne .notunder
    APCNT apc_under
.notunder:
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
    ; --- is the SND ring already comfortably full? then do NOTHING this pass
    ;     (SPEC.md 86.5.1): the stale consumed only over-reports fullness, so a
    ;     "skip" that turns out to be one tick too early costs at most another
    ;     ~half KB of DSP drain and never an underrun.
    mov ax, [ap_total]
    sub ax, [ap_consumed]
    mov bx, [ap_ring]
    sub bx, 2 * AP_HALF
    cmp ax, bx
    jae .out                       ; > ring-2halves in hand: nothing to feed

    ; refill request if the look-ahead ring is low
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
    APCNT apc_feed
    pop ax
    inc byte [apw_halves]
    cmp ax, 2
    je .out
    jmp .fill

.dead:
    mov byte [ap_ended], 1
    APCNT apc_wend
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
; ap_worker_draw - the elapsed time + progress bar, redrawn ONLY when the
;                  visible value changed and at most every AP_DRAW_TICKS
;                  (SPEC.md 86.5.1). No lock is taken on the common path.
; =============================================================================
ap_worker_draw:
    cmp byte [ap_diag], 0
    jne .out                       ; the Diagnostics view owns the content
    cmp byte [ap_state], AP_ST_PLAY
    jne .out

    ; refresh the elapsed-seconds value (cheap: from ap_played / rate)
    push ax
    push bx
    push dx
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
    ; did anything the eye can see change?
    mov ax, [ap_elapsed_s]
    cmp ax, [ap_last_s]
    jne .changed
    call apu_progress               ; AX = current bar fill in px
    cmp ax, [ap_last_bar]
    je .done                      ; nothing changed - no lock, no draw
.changed:
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [ap_last_draw_t]
    cmp ax, AP_DRAW_TICKS
    jb .done                      ; throttled: at most ~2 redraws a second
    mov [ap_last_draw_t], bx

    mov bx, [ap_win]
    call OSAPI_WM_OBSCURED         ; CF = 1 fully covered / hidden
    jc .done                      ; a backgrounded player does NO drawing
    call OSAPI_GFX_LOCK
    mov bx, [ap_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    call apu_draw_dyn
    APCNT apc_progupd
    mov ax, [ap_elapsed_s]
    mov [ap_last_s], ax
    call apu_progress
    mov [ap_last_bar], ax
.unlock:
    call OSAPI_GFX_UNLOCK
.done:
    pop dx
    pop bx
    pop ax
.out:
    ret
