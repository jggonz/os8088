; =============================================================================
; os8088 - apps/xtmp3/x3demo.inc - THE DEMO ENGINE (S4 only)
;
; The face's other side, until the real one exists. It plays NOTHING: it moves
; the state record (x3face.inc's x3_st) the way an engine will - a check,
; then a track whose clock runs, whose needles swing and whose buffer gauge
; breathes - so every element can be seen changing and the incremental draw
; proved against the full paint. S8's apps/xtmp3/x3eng.inc replaces this file
; and keeps every x3_e_* name below; the face calls nothing else.
;
; THE ENGINE'S INTERFACE (each: the UI task, the gfx lock held; out CF = 0;
; may clobber AX, CX, DX, SI, DI - BX is the caller's):
;
;   x3_e_init         once, from the entry proc: x3_st's first state
;   x3_e_tick         every X3_TICK ticks, from the window's timer
;   x3_e_play   AL    play queue entry AL from its start
;   x3_e_cue    AL    load entry AL without playing it (READY)
;   x3_e_pause / x3_e_resume / x3_e_stop
;   x3_e_seek   AX    to second AX of the current track (clamped)
;   x3_e_qual         X3S_QWANT changed (Q, Options > Quality)
;   x3_e_vol          X3S_VOL changed (the knob, + and -)
;   x3_e_cancel       Esc: a conversion's Cancel
;   x3_e_qadd   AL    entry AL joined the queue: its title and duration are
;                     the parser's to fill (S5); the demo invents a duration
;   x3_e_qdrop  AL    entry AL is about to leave the queue
;   x3_e_qclear       the queue is about to be emptied
;
; Everything the demo invents is marked DEMO and goes with this file.
; =============================================================================

X3DM_CHECK  equ 3                   ; passes 'Checking performance' lasts

; -----------------------------------------------------------------------------
x3_e_init:
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_VOL], 7
    mov byte [x3_st+X3S_FLAGS], X3SF_VOLOK  ; DEMO: a knob that "works"
    mov word [x3_st+X3S_PEAK], 371  ; DEMO: SPEC.md 121.10's own example
    call OSAPI_GET_TICKS
    mov [x3_dm_last], ax
    or ax, 1
    mov [x3_dm_seed], ax
    clc
    ret

; -----------------------------------------------------------------------------
; x3_e_tick - the clock: elapsed time from the tick count, the meters, the
;             buffer; the check runs out into play; a track ends into the next
; -----------------------------------------------------------------------------
x3_e_tick:
    call OSAPI_GET_TICKS
    mov cx, ax
    sub cx, [x3_dm_last]            ; CX = ticks since the last pass
    mov [x3_dm_last], ax
    mov al, [x3_st+X3S_STATE]
    cmp al, X3ST_CHECK
    je .check
    cmp al, X3ST_LIVE
    je .play
    cmp al, X3ST_FILE
    je .play
    cmp al, X3ST_PAUSED
    je .still
    mov byte [x3_st+X3S_METL], 0    ; stopped: the needles at rest
    mov byte [x3_st+X3S_METR], 0
    mov byte [x3_st+X3S_LEVEL], 0
    clc
    ret
.still:
    clc
    ret
.check:
    dec byte [x3_dm_wait]
    jnz .still
    call x3_dm_fmt                  ; the rung chosen: its format, its state
    clc
    ret
.play:
    mov ax, 55                      ; 54.9 ms a tick: whole seconds out of the
    mul cx                          ; milliseconds
    add ax, [x3_dm_ms]
.sec:
    cmp ax, 1000
    jb .ms
    sub ax, 1000
    inc word [x3_st+X3S_ELAP]
    jmp short .sec
.ms:
    mov [x3_dm_ms], ax
    mov ax, [x3_st+X3S_ELAP]
    cmp ax, [x3_st+X3S_TOTAL]
    jb .meters
    mov al, [x3_st+X3S_CUR]         ; the end: the next one, or Finished
    inc al
    cmp al, [x3_qn]
    jae .done
    mov [x3_qsel], al
    jmp x3_e_play
.done:
    mov ax, [x3_st+X3S_TOTAL]
    mov [x3_st+X3S_ELAP], ax
    mov byte [x3_st+X3S_STATE], X3ST_DONE
    clc
    ret
.meters:                            ; DEMO: music-ish levels, and a buffer
    call x3_dm_rand                 ; that sways near full
    and al, 0x7F
    add al, 0x50
    mov ah, [x3_st+X3S_METL]        ; a quarter of the way to it
    call x3_dm_ease
    mov [x3_st+X3S_METL], al
    call x3_dm_rand
    and al, 0x7F
    add al, 0x48
    mov ah, [x3_st+X3S_METR]
    call x3_dm_ease
    mov [x3_st+X3S_METR], al
    call x3_dm_rand
    and al, 0x1F
    add al, 0xC8
    mov [x3_st+X3S_LEVEL], al
    clc
    ret

; x3_dm_ease - AL = a target, AH = the level -> AL = halfway there
x3_dm_ease:
    shr al, 1
    shr ah, 1
    add al, ah
    ret

; x3_dm_rand - AL = the next byte of a 16-bit LFSR. clobbers AH
x3_dm_rand:
    mov ax, [x3_dm_seed]
    shr ax, 1
    jnc .n
    xor ax, 0xB400
.n:
    mov [x3_dm_seed], ax
    ret

; -----------------------------------------------------------------------------
; x3_dm_fmt - the rung the demo "chose" and what it outputs: Auto pretends
;             to be an XT and picks XT FAST, the mockup's; a choice of the
;             user's is taken as given. Mono output for every XT rung, so the
;             right dial is the BUFFER gauge (SPEC.md 121.8)
; -----------------------------------------------------------------------------
x3_dm_fmt:
    mov al, [x3_st+X3S_QWANT]
    cmp al, X3Q_AUTO
    jne .q
    mov al, X3Q_XTFAST              ; DEMO
.q:
    mov [x3_st+X3S_QUAL], al
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [x3_dm_rates+bx]
    mov [x3_st+X3S_RATE], ax
    mov ax, [x3_dm_rates+bx+2]      ; AL = channels, AH = bits
    mov [x3_st+X3S_CHANS], al
    mov [x3_st+X3S_BITS], ah
    mov al, X3ST_FILE               ; an XT rung plays the decoded file...
    cmp byte [x3_st+X3S_QUAL], X3Q_QUART
    ja .st
    mov al, X3ST_LIVE               ; ...the standard decoder, live
    mov word [x3_st+X3S_SPEED], 131 ; DEMO: 1.31x
.st:
    mov [x3_st+X3S_STATE], al
    ret

x3_dm_rates:                        ; by X3Q_*: rate, channels, bits
    dw 11025
    db 1, 8                         ; AUTO (never used: AUTO picks a rung)
    dw 44100
    db 2, 16                        ; ORIGINAL
    dw 22050
    db 2, 16                        ; HALF
    dw 11025
    db 2, 16                        ; QUARTER
    dw 11025
    db 1, 8                         ; XT STANDARD
    dw 11025
    db 1, 8                         ; XT FAST
    dw 11025
    db 1, 8                         ; XT TURBO
    dw 5512
    db 1, 8                         ; XT SPEECH

; -----------------------------------------------------------------------------
x3_e_play:
    call x3_e_cue
    mov byte [x3_st+X3S_STATE], X3ST_CHECK
    mov byte [x3_dm_wait], X3DM_CHECK
    clc
    ret

; x3_e_cue - entry AL into the state: its title, its duration; READY
x3_e_cue:
    cmp al, [x3_qn]
    jae .out
    mov [x3_st+X3S_CUR], al
    call x3_qent                    ; SI = its record
    mov ax, [si+QE_DUR]
    mov [x3_st+X3S_TOTAL], ax
    mov word [x3_st+X3S_ELAP], 0
    mov word [x3_dm_ms], 0
    mov byte [x3_st+X3S_CHANS], 0   ; no output until it plays
    mov byte [x3_st+X3S_QUAL], X3Q_AUTO
    and byte [x3_st+X3S_FLAGS], ~(X3SF_EST | X3SF_NOTOT)
    mov al, [si+QE_FLAGS]
    and al, X3SF_EST
    or [x3_st+X3S_FLAGS], al
    add si, QE_TITLE
    mov di, x3_st+X3S_TITLE
    mov cx, 40
    call x3_putn
    mov byte [di], 0
    mov byte [x3_st+X3S_STATE], X3ST_READY
.out:
    clc
    ret

x3_e_pause:
    mov byte [x3_st+X3S_STATE], X3ST_PAUSED
    clc
    ret

x3_e_resume:
    call x3_dm_fmt                  ; back to what was playing
    clc
    ret

x3_e_stop:
    mov word [x3_st+X3S_ELAP], 0
    mov word [x3_dm_ms], 0
    mov byte [x3_st+X3S_CHANS], 0
    mov byte [x3_st+X3S_QUAL], X3Q_AUTO
    mov al, X3ST_READY
    cmp byte [x3_st+X3S_CUR], 0xFF
    jne .s
    mov al, X3ST_IDLE
.s:
    mov [x3_st+X3S_STATE], al
    clc
    ret

x3_e_seek:
    cmp ax, [x3_st+X3S_TOTAL]
    jb .s
    mov ax, [x3_st+X3S_TOTAL]
    or ax, ax
    jz .s
    dec ax
.s:
    mov [x3_st+X3S_ELAP], ax
    mov word [x3_dm_ms], 0
    clc
    ret

x3_e_qual:                          ; a new rung: a new check, as the ladder's
    call x3_playing                 ; override would (SPEC.md 121.5)
    jne .out
    mov byte [x3_st+X3S_STATE], X3ST_CHECK
    mov byte [x3_dm_wait], X3DM_CHECK
.out:
    clc
    ret

x3_e_vol:                           ; DEMO: no mixer, nothing to tell
x3_e_cancel:                        ; no conversion to cancel until S9
    clc
    ret

x3_e_qadd:                          ; DEMO: a duration out of a table - the
    push bx                         ; parser (S5) reads the real one
    call x3_qent
    mov bl, al
    xor bh, bh
.m:
    cmp bl, X3DM_NDUR
    jb .d
    sub bl, X3DM_NDUR
    jmp short .m
.d:
    shl bx, 1
    mov ax, [x3_dm_durs+bx]
    mov [si+QE_DUR], ax
    pop bx
    clc
    ret

x3_dm_durs: dw 278, 232, 306, 251, 199, 344     ; 4:38 3:52 5:06 ... (DEMO)
X3DM_NDUR   equ ($ - x3_dm_durs) / 2

; x3_e_qdrop - AL is leaving: the one shown stops and goes; a later one moves
; up a place with the list
x3_e_qdrop:
    mov ah, [x3_st+X3S_CUR]
    cmp ah, 0xFF
    je .out
    cmp al, ah
    ja .out
    jb .up
    call x3_e_stop                  ; the one shown
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    jmp short .out
.up:
    dec byte [x3_st+X3S_CUR]
.out:
    clc
    ret

x3_e_qclear:
    call x3_e_stop
    mov byte [x3_st+X3S_CUR], 0xFF
    mov byte [x3_st+X3S_STATE], X3ST_IDLE
    clc
    ret

; the demo's own bss, laid out by xtmp3.asm's X3B
%macro X3_DEMOBSS 0
    X3B x3_dm_last, 2               ; the tick count at the last pass
    X3B x3_dm_ms, 2                 ; milliseconds toward the next second
    X3B x3_dm_seed, 2               ; the levels' generator
    X3B x3_dm_wait, 1               ; passes left of the check
%endmacro
