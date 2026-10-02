; =============================================================================
; os8088 - apps/os88pcm.inc
;
; THE PWM CLIP PLAYER (SPEC.md 34.4): a sampled clip through the PC speaker, on
; the desktop, run to completion - SPEC.md 34.4's pulse-width trick, channel 2
; in mode 0 with one count written a sample, paced off channel 0's phase. A
; LIBRARY, os88spk.inc's shape: the kernel's half is the door OSAPI_SND_PLAY -
; channel 2 granted and released, the scheduler lock held from one to the
; other, the fsx sub-tick parked, an aborting click drained - and the player is
; here, in the package that plays. The whole player in the kernel was ~630
; resident bytes on both kernels, a 256-byte table among them, for a caller
; that ships on the live media alone (kernel size pass 8).
;
;   os88pcm_play  ES:SI = 8-bit unsigned mono samples, CX = count (<= 65535),
;                 DX = the rate, 4,679..16,124 Hz (N = 1,193,182 / rate must
;                 land in 74..255). out AX = 0 played / 1 busy (a clip, or a
;                 PCM_BG stream open, 34.9) / 2 the rate / 3 disabled by the
;                 user / 4 nothing can sound / 5 a click skipped it; CF
;                 mirrors AX != 0. UI TASK ONLY - a window callback - and it
;                 BLOCKS for the whole clip with the desktop frozen (PCM_EXCL).
;                 Preserves every register but AX.
;
; The same contract OSAPI_SND_PLAY had before the door was cut down, so a
; caller swaps `call OSAPI_SND_PLAY` for `call os88pcm_play` and nothing else.
; A rate outside the range is ERR 2, NEVER A SILENT CLAMP (SPEC.md 34.4).
;
; WHAT IT COSTS: 256 bytes of the including package's image for the rescale
; table (zeros on the disk, which compress to nothing) and ~180 of code. The
; labels are os88pcm_; the state is in the package's own image, through CS.
; =============================================================================

os88pcm_play:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, 2                       ; rate -> N = 1193182 / rate, 74..255:
    cmp dx, 4679                    ; the checks double as the divide's
    jb .err                         ; guard (a tiny rate would overflow it)
    cmp dx, 16124
    ja .err
    mov bx, dx
    mov dx, 0x0012
    mov ax, 0x34DE
    div bx                          ; AX = N
    mov bp, ax                      ; BP = N, PIT counts a sample
    push cx                         ; THE RESCALE TABLE: t[s] = 1 + s(N-2)/255
    sub al, 2                       ; - never 0 (a count of 0 is 65536) and
    mov dl, al                      ; never N (the pulse ends inside its
    mov bx, os88pcm_tab             ; period). Non-destructive: the clip is
    mov cl, 255                     ; never modified, so replay and re-rate
    mov di, 255                     ; work (SPEC.md 34.4)
.tab:
    mov ax, di
    mul dl                          ; AX = s(N-2) <= 64,515
    div cl
    inc ax
    mov [cs:bx+di], al
    dec di
    jns .tab
    pop cx
.grant:                             ; (tests/sndplay.py marks the clip here)
    xor al, al                      ; THE GRANT: channel 2 is ours as the PWM,
    call OSAPI_SND_PLAY             ; the scheduler locked, the sub-tick
    jc .out                         ; parked; CF = 1 AX = its refusal
    mov [cs:.mbd], bx               ; the mouse button byte, KERNEL_SEG:BX -
    push es                         ; into the abort check's own displacement
    pop ds                          ; DS:SI = the samples
    mov ax, KERNEL_SEG
    mov es, ax
    mov al, [es:bx]                 ; THE CLICK-ABORT BASELINE (34.4): a
    mov [cs:os88pcm_b0], al         ; button already down never aborts
    mov bx, os88pcm_tab             ; BX = the table, for xlat through CS
    cld
    pushf                           ; prime the deadline: one atomic channel 0
    cli                             ; latch-and-read (SPEC.md 34.1, binding),
    mov al, 0                       ; so the first sample is due at once
    out 0x43, al
    in al, 0x40
    mov dl, al
    in al, 0x40
    mov dh, al
    popf
    neg dx
    mov di, dx                      ; DI = the deadline
    jcxz .done
.next:
    lodsb                           ; a sample...
    cs xlatb                        ; ...as a count, 1..N-1
    mov ah, al                      ; parked in AH across the polls
.wait:
    pushf                           ; } one atomic ch0 latch+read triple
    cli                             ; } (SPEC.md 34.1, binding)
    mov al, 0
    out 0x43, al
    in al, 0x40
    mov dl, al
    in al, 0x40
    mov dh, al
    popf                            ; IF = 1 between polls
    neg dx                          ; the phase, rising at 1,193,182 Hz...
    sub dx, di                      ; ...less the deadline, wrap-safe signed
    js .wait                        ; early: poll again
    cmp dx, bp                      ; late by more than a period? THE RESYNC
    jbe .emit                       ; RULE: the deadline to now - samples
    add di, dx                      ; dropped, never a burst (34.4)
.emit:
    mov al, ah
    out 0x42, al                    ; the pulse
    add di, bp                      ; the next deadline
.mb:
    mov al, [es:0xFFFF]             ; (the button byte: .mbd, patched above)
.mbd equ $ - 2
    and [cs:os88pcm_b0], al         ; FOLD RELEASES INTO THE BASELINE first,
    cmp al, [cs:os88pcm_b0]         ; then a button down now and not in it
    jne .click                      ; is a click: the fold leaves the
    loop .next                      ; baseline a subset of AL, so != is it
.done:
    mov al, 1                       ; THE RELEASE, played out
    call OSAPI_SND_PLAY
    xor ax, ax                      ; AX = 0, CF = 0
    jmp short .out
.click:
    mov al, 2                       ; THE RELEASE after a click: the kernel
    call OSAPI_SND_PLAY             ; drains the aborting press first
    mov ax, 5
.err:
    stc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

os88pcm_b0:     db 0                ; the click-abort baseline
os88pcm_tab:    times 256 db 0      ; the rescale table, rebuilt every clip
