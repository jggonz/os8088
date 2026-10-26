; =============================================================================
; os8088 - apps/waverack/wrxport.inc - the transport (SPEC.md 123.6, 123.8)
;
; ONE STATE MACHINE, wt_state: IDLE, PLAYING, PLAY_PAUSED (and RECORDING and
; REC_PAUSED, wave 5's, whose values are reserved). BUSY - a scan - is not a
; state of its own here: it is [we_busy] = 1, and wr_cmdok reads it. Every
; command's greying is a function of the state and the card (one predicate,
; SPEC.md 47 rule 2).
;
; THE PIPELINE IS AUDIO'S (SPEC.md 86.5), with a document under it instead of
; a file:
;
;   the UI task   reads document frames - piece by piece, through wo_read and
;                 the I/O claim, restanding in each source's folder - and
;                 COPIES them into the look-ahead ring (a 32 KB claim, 16 KB
;                 tier) on OSAPI_WM_ONWAKE; the end of a lap is just a refill
;                 position, so Loop wraps to the region's start seamlessly
;   the worker    converts out of the look-ahead into a 2 KB half - 8-bit mono
;                 at R / d, the box filter of d frames, a 16-bit sample's high
;                 byte, the stereo average, VOLUME's table - and stages each
;                 full half into the stream's ring grant (16 KB, 8 KB tier)
;                 with verbs 6 and 1; reads verb 9 for the exact played count;
;                 notes each half's peak for the meter; and WAKES the UI for a
;                 refill and at the end. It touches no file and draws nothing
;                 (SPEC.md 20.6 rule 7)
;
; [wt_mixing] brackets a feed pass (set first, cleared last), and wt_halt
; drains it before it closes the stream or hands the grant back (SPEC.md
; 34.6's rule: never free a grant the worker may be staging into).
;
; PAUSE CLOSES THE STREAM: the played count is read once more, the stream
; closed and the position kept, and Play opens a new one from it - so Pause
; is immediate, nothing stale is ever played, and resume is a seek.
;
; THE PLAYHEAD is a frame derived from verb 9's count: n output samples are
; n x d document frames into the play, through the first lap and then round
; the loop (wt_place; tools/wrsim.py's play_pos is the same arithmetic).
; =============================================================================

WT_IDLE     equ 0
WT_PLAY     equ 1
WT_PPAUSE   equ 2
WT_REC      equ 3                   ; wave 5's (SPEC.md 123.6, reserved)
WT_RPAUSE   equ 4
WT_HALF     equ 2048                ; a staged half: the ring stream's unit
WT_LAKB     equ 32                  ; the look-ahead claim...
WT_LAKB2    equ 16                  ; ...and its tier
WT_RINGKB   equ 16                  ; the ring grant (verb 7)...
WT_RINGKB2  equ 8                   ; ...and its tier
WT_PREROLL  equ 6                   ; halves staged before the open, at most
WT_MAXFEED  equ 4                   ; halves a feed pass stages, at most
WT_CEIL     equ 22222               ; the card's ceiling (SPEC.md 34.5)...
WT_CEILHI   equ 44100               ; ...with SND_CAP_PCM_HI
WT_MINOUT   equ 4000                ; and its floor
WT_SLPFEED  equ 1                   ; the worker's sleep: playing...
WT_SLPIDLE  equ 9                   ; ...and not
WT_MTICKS   equ 3                   ; the meter: at most every 3 ticks
WT_HOLDT    equ 18                  ; its peak hold: 1 s
WT_PEAKT    equ 36                  ; PEAK: 2 s
WT_FILLMAX  equ 8                   ; reads a refill makes, at most

; -----------------------------------------------------------------------------
; wt_active - CF = 0 when the transport holds a play: PLAYING or PLAY_PAUSED.
; keeps every register
; -----------------------------------------------------------------------------
wt_active:
    push ax
    mov al, [wt_state]
    dec al
    cmp al, 2                       ; 1, 2 -> 0, 1: below 2 sets the carry
    pop ax
    cmc
    ret

; -----------------------------------------------------------------------------
; wt_vbuild - VOLUME's table (SPEC.md 123.6): out = 80h + (in - 80h) x v /
;             100, truncated toward 80h - the worker passes every sample
;             through it, after the meter has read it. keeps every register
; -----------------------------------------------------------------------------
wt_vbuild:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bl, [wr_vol]
    xor bh, bh
    mov di, wt_vtab
    xor cx, cx
.v:
    mov ax, cx
    sub ax, 128
    imul bx                         ; |t x v| <= 12,800: AX holds it
    push bx
    mov bx, 100
    cwd
    idiv bx                         ; toward zero
    pop bx
    add al, 128
    mov [di], al
    inc di
    inc cx
    cmp cx, 256
    jb .v
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE COMMANDS (the UI task, from a key, a menu item or a click: gfx lock held)
; =============================================================================

; -----------------------------------------------------------------------------
; wt_go - Play from IDLE: a non-empty selection [s, e), else the cursor to the
;         end (from frame 0 when the cursor stands at the end); Loop repeats
;         the selection, or with none the whole document (SPEC.md 123.6).
;         in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_go:
    call we_hassel
    jc .whole
    mov ax, [wo_s]                  ; the selection: its start, its region
    mov dx, [wo_s+2]
    mov [wt_r0], ax
    mov [wt_r0+2], dx
    mov cx, [wo_e]
    mov [wt_r1], cx
    mov cx, [wo_e+2]
    mov [wt_r1+2], cx
    jmp short .go
.whole:
    xor ax, ax                      ; the whole document...
    mov [wt_r0], ax
    mov [wt_r0+2], ax
    mov ax, [wo_frames]
    mov [wt_r1], ax
    mov ax, [wo_frames+2]
    mov [wt_r1+2], ax
    mov ax, [wo_cur]                ; ...from the cursor
    mov dx, [wo_cur+2]
    cmp ax, [wo_frames]
    jne .go
    cmp dx, [wo_frames+2]
    jne .go
    xor ax, ax                      ; (at the end: from the start)
    xor dx, dx
.go:
    mov cl, [wr_loop]               ; Loop as it stands now
    mov [wt_lp], cl
    mov [wt_p0], ax                 ; where Stop puts the playhead back
    mov [wt_p0+2], dx
    mov [wt_ph], ax
    mov [wt_ph+2], dx
    call wt_begin
    jc wt_refuse
    mov byte [wt_state], WT_PLAY
    mov byte [wr_mwho], 2           ; LEVEL  PLAY
    call we_cmoved
    jmp wt_outsay

; wt_refuse - wt_begin refused (SI = why): IDLE again, and the status bar
; says so. clobbers AX, BX, CX, DX, SI
wt_refuse:
    push si
    call wt_release
    pop si
    mov byte [wt_state], WT_IDLE
    call wt_quiet
    call we_cmoved
    jmp wr_say

; -----------------------------------------------------------------------------
; wt_pause - PLAYING -> PLAY_PAUSED: the exact position read, the stream
;            closed, the position kept. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_pause:
    call wt_halt                    ; (the count read last)
    call wt_place
    mov ax, [wt_ph]
    mov [wt_ppos], ax
    mov ax, [wt_ph+2]
    mov [wt_ppos+2], ax
    mov byte [wt_state], WT_PPAUSE
    call wt_dark
    call we_cmoved
    mov si, wr_s_paused
    call wr_say
    jmp wt_reref

; -----------------------------------------------------------------------------
; wt_resume - PLAY_PAUSED -> PLAYING: a new stream from the paused position.
;             in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_resume:
    mov ax, [wt_ppos]
    mov dx, [wt_ppos+2]
    call wt_begin
    jc wt_refuse
    mov byte [wt_state], WT_PLAY
    jmp wt_outsay

; -----------------------------------------------------------------------------
; wt_stop - Stop: the output silenced, every claim given back, and the
;           playhead put back where the play began; the cursor and the
;           selection do not move (SPEC.md 123.6). clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
wt_stop:
    call wt_release
    mov byte [wt_state], WT_IDLE
    mov ax, [wt_p0]
    mov [wt_ph], ax
    mov ax, [wt_p0+2]
    mov [wt_ph+2], ax
    call wt_quiet
    call we_cmoved
    mov si, wr_s_stopped
    call wr_say
    ; (falls into wt_reref)

; wt_reref - a refinement the play held back goes on (wrenv.inc). keeps all
wt_reref:
    cmp byte [we_busy], 2
    jne .out
    call wr_arm
.out:
    ret

; -----------------------------------------------------------------------------
; wt_seek - the cursor moved while the transport holds a play: PLAYING
;           restarts the stream there (closed and reopened - no stale ring
;           bytes), PLAY_PAUSED moves the paused position (SPEC.md 123.6).
;           in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_seek:
    mov ax, [wo_cur]
    mov dx, [wo_cur+2]
    cmp byte [wt_state], WT_PPAUSE
    jne .play
    mov [wt_ppos], ax
    mov [wt_ppos+2], dx
    mov [wt_ph], ax
    mov [wt_ph+2], dx
    jmp we_cmoved
.play:
    cmp byte [wt_state], WT_PLAY
    jne .out
    push ax
    push dx
    call wt_halt
    pop dx
    pop ax
    mov [wt_ph], ax
    mov [wt_ph+2], dx
    call wt_begin
    jc wt_refuse
    call we_cmoved
.out:
    ret

; -----------------------------------------------------------------------------
; wt_finish - the stream ended, or failed: IDLE, every claim back, and the
;             status bar says why - the read error, the underruns counted, or
;             the end. UI task, gfx lock held. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_finish:
    call wt_active
    jc .out
    call wt_release
    mov byte [wt_state], WT_IDLE
    mov byte [wt_ended], 0
    call wt_quiet
    call we_cmoved
    mov si, ww_e12                  ; 'Cannot read the file'
    cmp byte [wt_err], 0
    jne .say
    mov si, wr_s_end
    mov ax, [wt_nunder]
    or ax, ax
    jz .say
    mov di, wr_tbuf2                ; '3 underruns'
    call dec_u16
    mov si, wr_s_unders
    cmp word [wt_nunder], 1
    jne .pl
    mov si, wr_s_under1
.pl:
    call wr_put
    mov byte [di], 0
    mov si, wr_tbuf2
.say:
    call wr_say
    jmp wt_reref
.out:
    ret

; wt_quiet / wt_dark - the meter dark, its caption LEVEL (quiet) or kept
; (dark: a pause is still the play's). keep every register
wt_quiet:
    mov byte [wr_mwho], 0
wt_dark:
    mov byte [wr_lit], 0
    mov byte [wr_hold], 0
    mov byte [wr_peak], 0
    ret

; -----------------------------------------------------------------------------
; wt_outsay - the status bar: what the card plays (SPEC.md 123.6) -
;             'Out 22,050 Hz mono /2', or with d = 1 'Out 11,025 Hz mono'.
;             clobbers AX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_outsay:
    mov di, wr_tbuf2
    mov si, wt_s_out
    call wr_put
    mov ax, [wt_out]
    call wr_ratetxt
    mov si, wt_s_mono
    call wr_put
    mov al, [wt_d]
    cmp al, 1
    je .z
    mov byte [di], ' '
    mov byte [di+1], '/'
    add al, '0'
    mov [di+2], al
    add di, 3
.z:
    mov byte [di], 0
    mov si, wr_tbuf2
    jmp wr_say

; =============================================================================
; THE STREAM
; =============================================================================

; -----------------------------------------------------------------------------
; wt_begin - A STREAM FROM DOCUMENT FRAME DX:AX: the card asked again (SPEC.md
;            123.2), the divisor and the output rate, the claims (kept across
;            a pause), the look-ahead primed, the pre-roll converted into the
;            grant, the ring stream opened (verb 0), the worker hired once.
;            The region [wt_r0, wt_r1), [wt_lp] and [wt_p0] are the caller's.
; in:  BX = the window. out: CF = 0 playing; CF = 1 refused, SI = why (the
;      caller gives everything back). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_begin:
    mov [wt_sp], ax                 ; the stream starts here...
    mov [wt_sp+2], dx
    mov [wt_fpos], ax               ; ...and so does the fill
    mov [wt_fpos+2], dx
    mov cx, [wt_r1]                 ; the first lap ends at the region's end -
    mov si, [wt_r1+2]               ; or, begun past it, the document's
    cmp dx, si
    jb .lap
    ja .doc
    cmp ax, cx
    jb .lap
.doc:
    mov cx, [wo_frames]
    mov si, [wo_frames+2]
.lap:
    mov [wt_e1], cx
    mov [wt_e1+2], si
    mov [wt_lend], cx
    mov [wt_lend+2], si
    call OSAPI_SND_CAPS             ; THE CARD NOW, not at launch
    mov [wr_caps], ax
    mov si, wr_s_nocard
    test ax, SND_CAP_PCM_BG
    jz .no
    mov cx, WT_CEIL                 ; d: the smallest whole number that brings
    test ax, SND_CAP_PCM_HI         ; the rate to the card's ceiling
    jz .ceil
    mov cx, WT_CEILHI
.ceil:
    mov ax, [wo_rate]
    xor dx, dx
    add ax, cx                      ; ceil(R / ceiling), in 32 bits
    adc dx, 0
    sub ax, 1
    sbb dx, 0
    div cx
    mov bx, ax
    mov [wt_d], al
    mov ax, [wo_rate]               ; the card's rate: R / d
    xor dx, dx
    div bx
    mov [wt_out], ax
    mov si, wr_s_lowrate            ; under 4,000 Hz: refused
    cmp ax, WT_MINOUT
    jb .no
    mov bl, [wo_bits]               ; the format's inner loop
    shr bl, 1
    shr bl, 1                       ; 8 -> 2, 16 -> 4
    add bl, [wo_ch]                 ; + 1 or 2: 3, 4, 5, 6
    sub bl, 3
    xor bh, bh
    shl bx, 1
    mov ax, [wt_cvtab + bx]
    mov [wt_cvp], ax
    call wt_claims                  ; CF: SI = why
    jc .no
    push es                         ; the pipeline at nought
    push ds
    pop es
    mov di, wt_zero0
    mov cx, wt_zero1 - wt_zero0
    xor al, al
    cld
    rep stosb
    pop es
    call wt_refill                  ; the look-ahead primed
    call wt_refill
    mov si, ww_e12
    cmp byte [wt_err], 0
    jne .no
    mov byte [wt_ntry], 24          ; the pre-roll: halves into the grant
.pre:
    call wt_conv
    or ax, ax
    jnz .got
    mov si, ww_e12
    cmp byte [wt_err], 0
    jne .no
    call wt_refill
    dec byte [wt_ntry]
    jnz .pre
    jmp short .half
.got:
    cmp ax, 2
    je .half
    inc byte [wt_npre]
    mov al, [wt_npre]
    cbw
    cmp ax, [wt_preroll]
    jb .pre
.half:
    mov cx, WT_HALF                 ; at least the card's half: 4 KB above
    cmp word [wt_out], WT_CEIL      ; 22,222 Hz (SPEC.md 34.5) - a short
    jbe .hk                         ; sound's padded half and silence
    shl cx, 1
.hk:
    cmp [wt_total], cx
    jae .open
    call wt_silence
    jmp short .half
.open:
    mov al, 0                       ; verb 0: the ring stream
    mov ah, SND_OPENF_RING
    mov si, [wt_grant]
    mov cx, [wt_total]
    mov dx, [wt_out]
    call OSAPI_SND_STREAM           ; AL = 0 and AH = the handle, else why
    or al, al
    jnz .snderr
    mov [wt_hand], ah
    mov byte [wt_sopen], 1
    cmp byte [wt_wk], 0             ; THE WORKER, hired once and kept
    jne .run
    mov ax, wt_worker
    mov bx, [wr_win]
    call OSAPI_TASK_SPAWN           ; CF: refused (transient, SPEC.md 20.6)
    mov si, wr_s_notask
    jc .no
    mov byte [wt_wk], 1
    ; ...AND IT MAY BE RESTARTED (SPEC.md 66.6.2): its whole state is in
    ; statics, and it parks only in OSAPI_TASK_ALIVE / OSAPI_TASK_SLEEP at
    ; the top of its loop, so a restart there loses nothing
    OS88_WORKER_RESTARTABLE_NOKEEP wt_worker
.run:
    call wr_arm                     ; the UI's tick: the playhead, the meter
    clc
    ret
.snderr:
    mov si, wr_s_cbusy              ; 1: busy (another stream, a clip)
    cmp al, 1
    je .no
    mov si, wr_s_rateno             ; 2: the rate refused
    cmp al, 2
    je .no
    mov si, wr_s_noopen
.no:
    stc
    ret

; wt_cvtab - the format's inner loop: 8-bit mono, stereo; 16-bit mono, stereo
wt_cvtab:   dw wt_cv8m, wt_cv8s, wt_cv16m, wt_cv16s

; -----------------------------------------------------------------------------
; wt_claims - the look-ahead claim (32 KB, 16 KB tier) and the ring grant
;             (verb 7: 16 KB, 8 KB tier), each made once and kept.
; out: CF = 1 refused, SI = why. clobbers AX, CX, DX
; -----------------------------------------------------------------------------
wt_claims:
    cmp word [wt_laseg], 0
    jne .ring
    mov ax, WT_LAKB
    call OSAPI_MEM_CLAIM
    mov cx, WT_LAKB * 1024
    jnc .la
    mov ax, WT_LAKB2
    call OSAPI_MEM_CLAIM
    mov cx, WT_LAKB2 * 1024
    jc .nomem
.la:
    mov [wt_laseg], dx
    mov [wt_lasz], cx
    dec cx
    mov [wt_lamask], cx
.ring:
    cmp byte [wt_ghave], 0
    jne .ok
    mov cx, WT_RINGKB * 1024
.try:
    mov ax, 7                       ; verb 7, sub-op 0: a grant of CX
    call OSAPI_SND_STREAM           ; AX = 0, SI = its offset
    or ax, ax
    jz .got
    cmp cx, WT_RINGKB2 * 1024
    jbe .nomem
    mov cx, WT_RINGKB2 * 1024
    jmp short .try
.got:
    mov [wt_grant], si
    mov [wt_ring], cx
    mov ax, cx
    dec ax
    mov [wt_rmask], ax
    inc ax                          ; the pre-roll follows the ring: halves =
    mov cl, 11                      ; ring / 2 KB - 1, at most 6 (Audio's
    shr ax, cl                      ; rule: verb 0 refuses a total past the
    dec ax                          ; ring)
    cmp ax, WT_PREROLL
    jbe .pr
    mov ax, WT_PREROLL
.pr:
    mov [wt_preroll], ax
    mov byte [wt_ghave], 1
.ok:
    clc
    ret
.nomem:
    mov si, ww_e15                  ; 'Not enough memory'
    stc
    ret

; -----------------------------------------------------------------------------
; wt_halt - the stream closed (if open): the worker told to feed no more, its
;           pass drained, the played count read ONCE MORE so the position is
;           exact, verb 2; then the grant handed back. The look-ahead claim is
;           kept. UI task. clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
wt_halt:
    cmp byte [wt_sopen], 0
    je .free
    mov byte [wt_sopen], 0          ; the worker feeds no more...
.drain:
    cmp byte [wt_mixing], 0         ; ...and is out of its pass (bounded: it
    jne .drain                      ; re-checks wt_sopen first, SPEC.md 45.2)
    cmp byte [wt_ended], 0
    jne .close
    call wt_poll                    ; the worker parked: the count is ours
.close:
    mov al, 2
    mov ah, [wt_hand]
    call OSAPI_SND_STREAM
.free:
    cmp byte [wt_ghave], 0
    je .out
    mov si, [wt_grant]
    mov ax, 0x0107                  ; verb 7, sub-op 1: free
    call OSAPI_SND_STREAM
    or ax, ax
    jnz .out                        ; refused: kept, and used next time
    mov byte [wt_ghave], 0
.out:
    ret

; wt_release - wt_halt, and the look-ahead claim given back too.
; clobbers AX, BX, CX, DX, SI
wt_release:
    call wt_halt
    mov dx, [wt_laseg]
    call wo_free
    mov [wt_laseg], dx
    ret

; -----------------------------------------------------------------------------
; wt_poll - verb 9 (verb 3 on a driver without it): the stream's state and
;           the bytes PLAYED EXACTLY, the delta folded into [wt_played]. The
;           worker's, and the UI's once the worker is drained.
; out: AX = the state (SND_ST_*). clobbers BX, DX
; -----------------------------------------------------------------------------
wt_poll:
    mov al, SND_V_PPOS
    mov ah, [wt_hand]
    call OSAPI_SND_STREAM           ; AX = the state, DX = played (exact)
    jnc .got
    mov al, 3
    mov ah, [wt_hand]
    call OSAPI_SND_STREAM
.got:
    mov bx, dx
    sub bx, [wt_cons]
    mov [wt_cons], dx
    pushf                           ; the UI reads the dword whole
    cli
    add [wt_played], bx
    adc word [wt_played+2], 0
    popf
    ret

; -----------------------------------------------------------------------------
; wt_place - THE PLAYHEAD from the played count: F = played x d frames into
;            the play; SP + F through the first lap, then the region round and
;            round (Loop), or the lap's end (tools/wrsim.py play_pos).
;            clobbers AX, BX, CX, DX
; -----------------------------------------------------------------------------
wt_place:
    pushf
    cli
    mov ax, [wt_played]
    mov dx, [wt_played+2]
    popf
    mov bl, [wt_d]
    xor bh, bh
    mov cx, 1
    call wo_muldiv                  ; DX:AX = F
    mov bx, [wt_e1]                 ; the first lap: e1 - sp frames
    mov cx, [wt_e1+2]
    sub bx, [wt_sp]
    sbb cx, [wt_sp+2]
    cmp dx, cx
    jb .lap1
    ja .past
    cmp ax, bx
    jb .lap1
.past:
    cmp byte [wt_lp], 0             ; not looping: the lap's end
    je .end
    sub ax, bx                      ; looping: r0 + (F - lap) mod (r1 - r0)
    sbb dx, cx
    mov bx, [wt_r1]
    mov cx, [wt_r1+2]
    sub bx, [wt_r0]
    sbb cx, [wt_r0+2]
    mov si, bx
    or si, cx
    jz .end
    push si
    call wo_div32                   ; CX:BX = the remainder
    pop si
    mov ax, [wt_r0]
    mov dx, [wt_r0+2]
    add ax, bx
    adc dx, cx
    jmp short .set
.end:
    mov ax, [wt_e1]
    mov dx, [wt_e1+2]
    jmp short .set
.lap1:
    add ax, [wt_sp]
    adc dx, [wt_sp+2]
.set:
    mov [wt_ph], ax
    mov [wt_ph+2], dx
    ret

; =============================================================================
; THE UI'S SIDE WHILE PLAYING
; =============================================================================

; -----------------------------------------------------------------------------
; wt_tick - the window's one-shot timer while PLAYING (gfx lock held): the
;           stream's end or failure finished; else the playhead from the
;           count (the time and the line follow it), the view paged to it
;           when it leaves, and the meter. Re-arms itself.
;           in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_tick:
    cmp byte [wt_ended], 0
    jne .fin
    cmp byte [wt_err], 0
    jne .fin
    push word [wt_ph]
    push word [wt_ph+2]
    call wt_place
    pop dx
    pop ax
    cmp ax, [wt_ph]
    jne .moved
    cmp dx, [wt_ph+2]
    je .meter
.moved:
    call we_cmoved                  ; the time and the line
    call we_ready                   ; the view follows: a PAGE, when the
    jc .meter                       ; playhead leaves it - never a scroll a
    mov ax, [wt_ph]                 ; tick (SPEC.md 123.6)
    mov dx, [wt_ph+2]
    call we_fcol
    jnc .meter
    mov ax, [wt_ph]
    mov dx, [wt_ph+2]
    call we_seefirst                ; the playhead at the view's left edge
.meter:
    call wt_meter
    jmp wr_arm
.fin:
    jmp wt_finish

; -----------------------------------------------------------------------------
; wt_meter - the meter's state (SPEC.md 123.8), at most every WT_MTICKS: the
;            peak of the half SOUNDING now (the worker noted it before the
;            volume) through wrart_db for the bar; the halves played since
;            the last look for the hold and PEAK, so a transient shorter than
;            the meter's own period (a half is 93 ms at 22 kHz, three ticks
;            165 ms) still lights them; the hold's 1 s decay and PEAK's 2 s.
;            The face draws only what changed. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_meter:
    call OSAPI_GET_TICKS
    mov di, ax
    sub ax, [wt_mtick]
    cmp ax, WT_MTICKS
    jb .out
    mov [wt_mtick], di
    mov si, [wt_cons]               ; the half sounding: its number (mod 32)
    mov cl, 11
    shr si, cl
    mov ax, si                      ; the halves since the last look, at
    sub al, [wt_mlast]              ; most 7 (the slots hold 16, and the
    and al, 31                      ; feed runs 8 ahead)
    cmp al, 7
    jbe .n
    mov al, 7
.n:
    mov cx, si
    mov [wt_mlast], cl
    mov cl, al                      ; CL = how many before this one
    xor dx, dx                      ; DL = their peak, DH = a rail in any
.back:
    mov bx, si
    and bx, 15
    mov al, [wt_pks + bx]
    cmp al, dl
    jbe .b1
    mov dl, al
.b1:
    or dh, [wt_clips + bx]
    dec si
    dec cl
    jns .back
    mov si, [wt_cons]               ; the bar: the half sounding
    mov cl, 11
    shr si, cl
    and si, 15
    mov al, [wt_pks + si]
    mov bx, wrart_db
    xlatb                           ; AL = lit cells, 0..24
    mov [wr_lit], al
    mov al, dl                      ; the hold: the loudest since the last
    xlatb                           ; look - a new high, or decayed
    cmp al, [wr_hold]
    jae .hnew
    mov ax, di
    sub ax, [wt_holdt]
    cmp ax, WT_HOLDT
    jb .hk
    mov al, [wr_lit]
.hnew:
    mov [wr_hold], al
    mov [wt_holdt], di
.hk:
    or dh, dh                       ; PEAK: lit at a rail, for 2 s
    jz .nclip
    mov byte [wr_peak], 1
    mov [wt_peakt], di
    ret
.nclip:
    mov dx, di
    sub dx, [wt_peakt]
    cmp dx, WT_PEAKT
    jb .out
    mov byte [wr_peak], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wt_onwake - OSAPI_WM_ONWAKE's share (UI task, gfx lock NOT held): a refill
;             the worker asked for (cleared LAST, SPEC.md 77.1), and the end
;             or a failure finished under the lock. clobbers all but BP
; -----------------------------------------------------------------------------
wt_onwake:
    cmp byte [wt_req], 0
    je .nr
    cmp byte [wt_sopen], 0          ; (a Stop got there first: nothing to do)
    je .clr
    call wt_refill
    call wt_lalow                   ; still low: come back
    jnc .clr
    mov bx, [wr_win]
    call OSAPI_WM_WAKE
.clr:
    mov byte [wt_req], 0
.nr:
    cmp byte [wt_ended], 0
    jne .fin
    cmp byte [wt_err], 0
    je .out
.fin:
    call OSAPI_GFX_LOCK
    mov bx, [wr_win]
    call wt_finish
    mov bx, [wr_win]
    call wr_redraw
    call OSAPI_GFX_UNLOCK
.out:
    ret

; wt_lalow - CF = 1 when the look-ahead holds under half its size and the
; document has more to give. keeps everything but AX
wt_lalow:
    cmp byte [wt_srcdone], 0
    jne .no
    push bx
    mov ax, [wt_lwr]
    sub ax, [wt_lrd]
    mov bx, [wt_lasz]
    shr bx, 1
    cmp ax, bx
    pop bx
    ret                             ; (below: the carry)
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wt_refill - THE LOOK-AHEAD TOPPED UP from the document (UI task only, SPEC.md
;             20.6 rule 7): whole frames through wo_read - piece by piece,
;             each source's folder restood - COPIED into the ring. The lap's
;             end is a refill position: Loop goes on from the region's start
;             in the same pass, otherwise the document is done. A read error
;             latches [wt_err]. clobbers AX, BX, CX, DX, SI, DI, ES
; -----------------------------------------------------------------------------
wt_refill:
    mov byte [wt_nfill], WT_FILLMAX
.more:
    cmp byte [wt_srcdone], 0
    jne .out
    mov ax, [wt_lasz]               ; the free frames
    sub ax, [wt_lwr]
    add ax, [wt_lrd]
    mov cl, [wo_sh]
    shr ax, cl
    jz .out
    mov bx, [wt_lend]               ; ...no further than the lap's end
    mov dx, [wt_lend+2]
    sub bx, [wt_fpos]
    sbb dx, [wt_fpos+2]
    jc .lap                         ; (passed it: the lap is over)
    or dx, dx
    jnz .k
    cmp bx, ax
    jae .k
    mov ax, bx
.k:
    mov cx, ax
    jcxz .lap
    mov ax, [wt_fpos]
    mov dx, [wt_fpos+2]
    push bp
    mov bp, WO_IOKB * 1024
    call wo_read                    ; ES:DI = the frames, CX = how many
    pop bp
    jc .err
    jcxz .lap                       ; (the document ended under the lap)
    push cx
    mov ax, cx
    mov cl, [wo_sh]
    shl ax, cl
    call wt_lacopy                  ; into the ring; published
    pop cx
    add [wt_fpos], cx
    adc word [wt_fpos+2], 0
    mov ax, [wt_fpos]
    cmp ax, [wt_lend]
    jne .next
    mov ax, [wt_fpos+2]
    cmp ax, [wt_lend+2]
    jne .next
.lap:
    cmp byte [wt_lp], 0             ; the lap's end: round again, or done
    je .done
    mov ax, [wt_r0]
    mov [wt_fpos], ax
    mov ax, [wt_r0+2]
    mov [wt_fpos+2], ax
    mov ax, [wt_r1]
    mov [wt_lend], ax
    mov ax, [wt_r1+2]
    mov [wt_lend+2], ax
.next:
    dec byte [wt_nfill]
    jnz .more
.out:
    ret
.err:
    mov byte [wt_err], 1            ; the UI stops, and says why
.done:
    mov byte [wt_srcdone], 1
    ret

; wt_lacopy - AX bytes (whole frames, at most the ring's free room) from
; ES:DI into the look-ahead at [wt_lwr], split at the seam; [wt_lwr]
; published after the bytes. clobbers AX, BX, CX, DX, SI, DI, ES
wt_lacopy:
    mov si, di
    mov bx, [wt_lwr]
    and bx, [wt_lamask]             ; the ring's offset
    mov dx, [wt_laseg]
    push ax
    mov cx, [wt_lasz]
    sub cx, bx                      ; to the seam
    cmp cx, ax
    jbe .c1
    mov cx, ax
.c1:
    sub ax, cx                      ; AX = what wraps
    push ds
    push es
    pop ds                          ; DS = the I/O claim
    mov es, dx                      ; ES = the ring
    mov di, bx
    cld
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    mov cx, ax
    xor di, di
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    pop ds
    pop ax
    add [wt_lwr], ax                ; PUBLISH, last
    ret

; =============================================================================
; THE WORKER (SPEC.md 20.6): lock-free, no file, no drawing
; =============================================================================
wt_worker:
.loop:
    mov bx, [wr_win]
    call OSAPI_TASK_ALIVE           ; (dies here when the window closes)
    mov ax, WT_SLPIDLE
    cmp byte [wt_sopen], 0
    je .slp
    mov ax, WT_SLPFEED
.slp:
    call OSAPI_TASK_SLEEP
    call wt_feed
    jmp short .loop

; -----------------------------------------------------------------------------
; wt_feed - THE FEED PASS: the count, the state (an underrun counted once, the
;           end seen), a refill asked for when the look-ahead is half empty,
;           and halves converted and staged while the ring has room for one.
;           Worker context. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_feed:
    mov byte [wt_mixing], 1         ; FIRST - before any guard
    cmp byte [wt_sopen], 0
    je .out
    cmp byte [wt_ended], 0
    jne .out
    call wt_poll                    ; AX = the state
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
    cmp ax, SND_ST_UNDER            ; an underrun: counted when it BEGINS,
    jne .st                         ; and not the end's own
    cmp byte [wt_ust], SND_ST_UNDER
    je .st
    cmp byte [wt_last], 0
    jne .st
    inc word [wt_nunder]
.st:
    mov [wt_ust], al
    cmp byte [wt_last], 0
    je .feed
    mov ax, [wt_total]              ; the last half staged: the end when the
    cmp ax, [wt_cons]               ; card has played it all
    jne .out
.dead:
    mov byte [wt_ended], 1
    call wt_kick
    jmp short .out
.feed:
    call wt_lalow                   ; the look-ahead half empty: a refill
    jnc .nofill
    cmp byte [wt_req], 0
    jne .nofill
    mov byte [wt_req], 1
    call wt_kick
.nofill:
    mov byte [wt_nfeed], WT_MAXFEED
.f:
    cmp byte [wt_sopen], 0
    je .out
    mov ax, [wt_total]              ; room for a half in the ring?
    sub ax, [wt_cons]
    mov bx, [wt_ring]
    sub bx, WT_HALF
    cmp ax, bx
    ja .out
    call wt_conv                    ; AX = 0 not yet, 1 a half, 2 the last
    or ax, ax
    jz .out
    push ax
    mov cx, [wt_total]
    mov al, 1                       ; verb 1: the new total
    mov ah, [wt_hand]
    call OSAPI_SND_STREAM
    pop ax
    cmp ax, 2
    je .out
    dec byte [wt_nfeed]
    jnz .f
.out:
    mov byte [wt_mixing], 0         ; LAST
    ret

; wt_kick - one OSAPI_WM_WAKE for our window (worker-safe). keeps all
wt_kick:
    push bx
    mov bx, [wr_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; -----------------------------------------------------------------------------
; wt_conv - THE CONVERSION (SPEC.md 123.6): look-ahead frames into the half
;           (wt_hbuf) until it is full or the input runs out. A full half is
;           staged; at the document's end the half is padded with 80h (a part
;           box group dropped) and staged as the last. Any task - the worker,
;           or the UI before the stream opens.
; out: AX = 0 nothing staged (more input wanted), 1 a half, 2 the last half.
;      clobbers BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wt_conv:
.more:
    mov ax, WT_HALF                 ; room, in output samples
    sub ax, [wt_hfill]
    jz wt_stage
    mov cl, [wt_d]                  ; ...in frames: room x d - the group begun
    xor ch, ch
    mul cx
    sub al, [wt_bk]
    sbb ah, 0
    mov bx, ax
    mov ax, [wt_lwr]                ; the frames in the look-ahead
    sub ax, [wt_lrd]
    mov cl, [wo_sh]
    shr ax, cl
    jz .dry
    cmp ax, bx
    jbe .a
    mov ax, bx
.a:
    mov si, [wt_lrd]                ; ...up to the seam (a frame never
    and si, [wt_lamask]             ; straddles it: the ring is a multiple of
    mov dx, [wt_lasz]               ; four bytes)
    sub dx, si
    shr dx, cl
    cmp ax, dx
    jbe .b
    mov ax, dx
.b:
    cmp ax, 255                     ; the inner loop counts in CL
    jbe .c
    mov ax, 255
.c:
    push ax
    mov cx, ax
    mov di, [wt_hfill]
    add di, wt_hbuf
    mov dx, [wt_pkcl]               ; DL = the half's peak, DH = its clip
    push bp
    mov bp, [wt_bsum]
    mov bx, wt_vtab
    mov ax, [wt_laseg]
    push es
    push ds
    pop es                          ; ES = ours: the half, the table, d
    push ds
    mov ds, ax                      ; DS = the look-ahead
    cld
    call word [es:wt_cvp]
    pop ds
    pop es
    mov [wt_bsum], bp
    pop bp
    mov [wt_pkcl], dx
    sub di, wt_hbuf
    mov [wt_hfill], di
    pop ax                          ; the frames taken
    mov cl, [wo_sh]
    shl ax, cl
    add [wt_lrd], ax                ; PUBLISH, last
    jmp .more
.dry:
    cmp byte [wt_srcdone], 0
    jne .end
    xor ax, ax                      ; more input wanted
    ret
.end:
    mov byte [wt_bk], 0             ; the document's end: a part group goes
    mov word [wt_bsum], 0
    mov byte [wt_last], 1
    mov cx, WT_HALF
    sub cx, [wt_hfill]
    cmp cx, WT_HALF
    jne .pad
    mov ax, 2                       ; nothing begun: the last half was the
    ret                             ; one before
.pad:
    mov di, [wt_hfill]              ; the rest of the half: silence
    add di, wt_hbuf
    call wt_fill80
    ; (falls into wt_stage)

; wt_stage - the full half into the grant at [wt_total] (verb 6), its peak
; and clip into the slot the meter reads, and the total on. out AX = 1, or 2
; for the last. clobbers BX, CX, SI, DI
wt_stage:
    mov di, [wt_total]
    and di, [wt_rmask]
    add di, [wt_grant]
    mov si, wt_hbuf
    mov cx, WT_HALF
    mov ax, 6                       ; verb 6: staged from our segment
    call OSAPI_SND_STREAM
    mov bx, [wt_total]              ; the half's slot: total / 2 KB, mod 16
    mov cl, 11
    shr bx, cl
    and bx, 15
    mov ax, [wt_pkcl]
    mov [wt_pks + bx], al
    mov [wt_clips + bx], ah
    mov word [wt_pkcl], 0
    add word [wt_total], WT_HALF
    mov word [wt_hfill], 0
    mov ax, 1
    cmp byte [wt_last], 0
    je .out
    inc ax
.out:
    ret

; wt_silence - a half of silence staged (a short sound's pre-roll must reach
; the card's half). clobbers AX, BX, CX, SI, DI
wt_silence:
    mov di, wt_hbuf
    mov cx, WT_HALF
    call wt_fill80
    jmp wt_stage

; wt_fill80 - CX bytes of 80h at DS:DI. clobbers AX, CX, DI
wt_fill80:
    push es
    push ds
    pop es
    mov al, 0x80
    cld
    rep stosb
    pop es
    ret

; -----------------------------------------------------------------------------
; THE INNER LOOPS - CL (1..255) frames from DS:SI (never across the seam) to
; ES:DI: each frame to an unsigned 8-bit mono value, its peak |u - 80h| into
; DL and a rail into DH (both BEFORE the volume, SPEC.md 123.8), the box sum
; of d frames in BP ([es:wt_bk] of them so far), and the mean through
; VOLUME's table at ES:BX. clobbers AX, CH
; -----------------------------------------------------------------------------
%macro WT_CLIP8 0                   ; AL = u: a rail (00h, FFh) sets DH
    test al, al
    jz %%c
    cmp al, 0xFF
    jne %%n
%%c:
    mov dh, 1
%%n:
%endmacro

%macro WT_HI16 0                    ; AX = a 16-bit sample: a rail (8000h,
    cmp ax, 0x7FFF                  ; 7FFFh) sets DH; AL = its high byte as
    je %%c                          ; unsigned
    cmp ax, 0x8000
    jne %%n
%%c:
    mov dh, 1
%%n:
    mov al, ah
    xor al, 0x80
%endmacro

%macro WT_PEAK 0                    ; AL = u: DL = max(DL, |u - 80h|); AH spent
    mov ah, al
    xor ah, 0x80
    jns %%p
    neg ah                          ; (-128 is 80h: 128, the table's last)
%%p:
    cmp ah, dl
    jbe %%q
    mov dl, ah
%%q:
%endmacro

%macro WT_EMIT 0                    ; AL = u: out through the box and VOLUME
    cmp byte [es:wt_d], 1
    jne %%box
    es xlatb
    stosb
    jmp short %%next
%%box:
    xor ah, ah
    add bp, ax
    inc byte [es:wt_bk]
    mov al, [es:wt_bk]
    cmp al, [es:wt_d]
    jb %%next
    mov ax, bp
    div byte [es:wt_d]              ; the mean, floored
    xor bp, bp
    mov byte [es:wt_bk], 0
    es xlatb
    stosb
%%next:
%endmacro

wt_cv8m:
    lodsb
    WT_CLIP8
    WT_PEAK
    WT_EMIT
    dec cl
    jz .out
    jmp wt_cv8m
.out:
    ret

wt_cv8s:
    lodsb
    WT_CLIP8
    WT_PEAK
    mov ch, al
    lodsb
    WT_CLIP8
    WT_PEAK
    add al, ch                      ; (L + R) >> 1, the carry the ninth bit
    rcr al, 1
    WT_EMIT
    dec cl
    jz .out
    jmp wt_cv8s
.out:
    ret

wt_cv16m:
    lodsw
    WT_HI16
    WT_PEAK
    WT_EMIT
    dec cl
    jz .out
    jmp wt_cv16m
.out:
    ret

wt_cv16s:
    lodsw
    WT_HI16
    WT_PEAK
    mov ch, al
    lodsw
    WT_HI16
    WT_PEAK
    add al, ch
    rcr al, 1
    WT_EMIT
    dec cl
    jz .out
    jmp wt_cv16s
.out:
    ret

; --- strings -----------------------------------------------------------------------
wt_s_out:    db 'Out ', 0
wt_s_mono:   db ' Hz mono', 0
