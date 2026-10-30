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
;                 land in 74..255), BX = 256 bytes of SCRATCH in your own
;                 segment (CS = DS) for the rescale table - anything idle for
;                 the length of the call, since nothing else of yours runs
;                 while it blocks. out AX = 0 played / 1 busy (a clip, or a
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
; WHAT IT COSTS: ~180 bytes of code and no state of its own. The rescale table
; is the CALLER'S 256 bytes (BX), not the library's: it is rebuilt every clip
; and dead between clips, so it belongs in a buffer the caller already has
; idle while the desktop is frozen - Recorder's is its demo stage buffer. It
; was a `times 256 db 0` here, 256 bytes of every includer's image (SPEC.md
; 34.4). The labels are os88pcm_; N and the button byte's address are patched
; into the loop's own operands, written through DS and read through CS, and
; the click baseline is on the stack.
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
    mov di, dx                      ; (DI, not BX: BX is the caller's table)
    mov dx, 0x0012
    mov ax, 0x34DE
    div di                          ; AX = N
    mov bp, ax                      ; BP = N, PIT counts a sample
    push cx                         ; THE RESCALE TABLE: t[s] = 1 + s(N-2)/255
    sub al, 2                       ; - never 0 (a count of 0 is 65536) and
    mov dl, al                      ; never N (the pulse ends inside its
                                    ; period). BX = the caller's table. Non-
    mov cl, 255                     ; destructive: the clip is never
                                    ; modified, so replay and re-rate
    mov di, 255                     ; work (SPEC.md 34.4)
.tab:
    mov ax, di
    mul dl                          ; AX = s(N-2) <= 64,515
    div cl
    inc ax
    mov [bx+di], al                 ; (DS: the package's, still)
    dec di
    jns .tab
    pop cx
    push bx                         ; the table, across the grant: the door
.grant:                             ; (tests/sndplay.py marks the clip here)
    xor al, al                      ; THE GRANT: channel 2 is ours as the PWM,
    call OSAPI_SND_PLAY             ; the scheduler locked, the sub-tick
    pop di                          ; answers BX (DI is free until the
    jc .out                         ; deadline); CF = 1 AX = its refusal
    mov [.mbd], bx                  ; the mouse button byte, KERNEL_SEG:BX -
                                    ; into the abort check's own displacement
    mov [.pn1], bp                  ; ...and N into the loop's two immediates,
    mov [.pn2], bp                  ; not a word it would read every sample.
    push es                         ; THROUGH DS, the package's, while it still
    pop ds                          ; is: under kern_mega CS is a read-only code
    mov ax, KERNEL_SEG              ; half and a store through it is a #GP per
    mov es, ax                      ; store (KERN-MEGA-MEMORY-PLAN MC7).
    mov al, [es:bx]                 ; DS:SI = the samples. THE CLICK-ABORT
    push ax                         ; BASELINE (34.4), a button already down
    mov bp, sp                      ; never aborting: ON THE STACK at [bp], the
                                    ; one writable segment the loop has free
    mov bx, di                      ; BX = the table, for xlat through CS
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
    cmp dx, strict word 0xFFFF      ; late by more than a period (N, .pn1)?
.pn1 equ $ - 2
    jbe .emit                       ; THE RESYNC RULE: the deadline to now -
    add di, dx                      ; samples dropped, never a burst (34.4)
.emit:
    mov al, ah
    out 0x42, al                    ; the pulse
    add di, strict word 0xFFFF      ; the next deadline (N, .pn2)
.pn2 equ $ - 2
.mb:
    mov al, [es:0xFFFF]             ; (the button byte: .mbd, patched above)
.mbd equ $ - 2
    and [bp], al                    ; FOLD RELEASES INTO THE BASELINE first,
    cmp al, [bp]                    ; then a button down now and not in it
    jne .click                      ; is a click: the fold leaves the
    loop .next                      ; baseline a subset of AL, so != is it
.done:
    add sp, 2                       ; (the baseline)
    mov al, 1                       ; THE RELEASE, played out
    call OSAPI_SND_PLAY
    xor ax, ax                      ; AX = 0, CF = 0
    jmp short .out
.click:
    add sp, 2                       ; (the baseline)
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
