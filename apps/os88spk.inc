; =============================================================================
; os8088 - apps/os88spk.inc
;
; THE SPEAKER RING PLAYER (SPEC.md 34.11): PCM through the PC speaker, on a
; machine with no card, from inside the caller's own FSXF_RATE bracket. A
; LIBRARY, os88ui.inc's shape: the kernel's half is the door OSAPI_FSX_SPK -
; channel 0 at the sample rate, channel 2 as 34.4's PWM, IRQ0 handed to a
; sample ISR and all of it put back when the bracket ends - and the ISR and
; the ring it plays are here, in the package that plays them. The whole
; player in the kernel measured 554 resident bytes; the door is 221.
;
; THE RING is the external ring's layout (SPEC.md 34.5.3), so a player that
; feeds a Sound Blaster feeds this the same way: RL bytes at seg:0 (RL = 4096
; << a size code), the producer's TOTAL word at RL and the CONS word the
; player writes back at RL + 2, both free-running. What differs is WHAT the
; ring holds: PWM COUNTS, not samples. os88spk_init writes the 256-byte table
; at RL + SND_EXT_TAB, and a sample s goes into the ring as table[s] - so the
; claim is RL + 272, and the translation is paid by the producer in bulk,
; not by the ISR once a sample.
;
; WHAT IT COSTS: the ISR runs once a sample, and that is the machine the
; speaker takes. SPEC.md 34.11 has the measured cycles; the Video Player's
; encoder budgets frames against them (98.2.15).
;
;   os88spk_init  DI = the ring's segment, AH bits 4-5 = its size code, DX =
;                 the rate (4,679..16,124 Hz). Writes the table; plays
;                 nothing. CF=1 the rate cannot be played. Any time
;   os88spk_go    the speaker plays the ring from CONS. INSIDE YOUR OWN
;                 FSXF_RATE BRACKET; CF=1, AX = SPK_E_* refused. The hook's
;                 periods become K samples each (OSAPI_FSX_SPK)
;   SPK2X (a PROTOTYPE define, no Makefile knob): two pulses a sample, the
;   carrier at twice the rate - every other entry the held count again.
;   ~90% of an 8088 at 5,512 Hz against 48%: docs/plans/VIDEO-PLAN.md 15.10
;
;   os88spk_stop  it stops where it is - a pause, a stop, a seek. CONS is
;                 exact; the ring is kept, and os88spk_go goes on from it.
;                 The bracket's end stops it too, so call go again in the next
;                 bracket. Harmless when stopped
;
; All preserve every register but their outputs. Labels os88spk_; the
; state is in the including package's own image, reached through CS.
; =============================================================================

; -----------------------------------------------------------------------------
os88spk_init:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov cl, ah                      ; RL = 4096 << the size code, taken
    and cl, 0x30                    ; before the divide spends AX
    shr cl, 1
    shr cl, 1
    shr cl, 1
    shr cl, 1
    mov bx, 4096
    shl bx, cl                      ; BX = RL
    cmp dx, 4679                    ; N = 1,193,182 / rate: 255 is mode 0's
    jb .no                          ; lobyte...
    push bx
    mov bx, dx
    mov dx, 0x0012
    mov ax, 0x34DE
    div bx                          ; AX = N
    pop bx
    cmp ax, 74                      ; ...and 74 the shortest period a pulse
    jb .no                          ; ends in (SPEC.md 34.4)
    mov [cs:os88spk_n], ax
    mov [cs:os88spk_rl], bx
    mov [cs:os88spk_rseg], di
    mov [cs:os88spk_rp+2], di
    mov es, di
    lea di, [bx+SND_EXT_TAB]        ; THE TABLE: t[s] = 1 + s(N-2)/255 -
    mov cl, al                      ; never 0 (a count of 0 is 65536) and
    sub cl, 2                       ; never N (the pulse ends inside its
    xor dx, dx                      ; period)
    cld
.tab:
    mov ax, dx
    mul cl
    push dx
    mov dl, 255
    div dl
    pop dx
    inc ax
    stosb
    inc dl
    jnz .tab
    mov al, [es:bx+SND_EXT_TAB+128] ; a dry grant's count: the middle, which
    mov [cs:os88spk_sil], al        ; is what silence sounds like here
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
os88spk_go:
    push bx
    push dx
    push si
    push di
    call os88spk_stop               ; whatever was open, closed and counted -
                                    ; the bracket's end may have closed the
                                    ; door under us, so its state is asked
                                    ; of nothing but the door itself
    pushf
    cli
    mov word [cs:os88spk_rp], os88spk_sil   ; the first sample a silent one,
    mov [cs:os88spk_rp+2], cs       ; and its event the first grant
    mov byte [cs:os88spk_dry], 1
    mov word [cs:os88spk_g], 1
    mov word [cs:os88spk_left], 1
    mov word [cs:os88spk_kr], 1     ; ...which ends a period: the door has
    popf                            ; written K and the chain by then
    mov dx, [cs:os88spk_n]
%ifdef SPK2X
    shr dx, 1                       ; SPK2X: two pulses a sample, a carrier
    mov byte [cs:os88spk_ph], 0     ; at twice the rate; the first entry a
%endif                              ; whole one
    mov si, os88spk_isr
    mov di, os88spk_kn              ; K, and the chain after it
    xor al, al
    push di
    call OSAPI_FSX_SPK
    pop di
    jc .out
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
os88spk_stop:
    push ax
    push bx
    push ds
    mov al, 1                       ; the door: no ISR after this (and none
    call OSAPI_FSX_SPK              ; to close is fine)...
    pushf
    cli
    call os88spk_acct               ; ...so what the grant played is final
    popf
    pop ds
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; os88spk_isr - IRQ0, once a sample: the vector is ours while the door is open,
; so this is entered with the interrupt frame alone, IF = 0, on whatever stack
; it hit. THE PER-SAMPLE PATH is one count out and the EOI: a grant runs to its
; next EVENT - the period's end, the data's, the ring's - with one counter, and
; everything else (TOTAL, the wrap, the period, CONS) is the event's. A
; period's last sample goes to the kernel's IRQ0 instead, which does the EOI,
; the tick and the hook. Measured, not guessed: SPEC.md 34.11
; -----------------------------------------------------------------------------
os88spk_isr:
    push ax
%ifdef SPK2X
    xor byte [cs:os88spk_ph], 1     ; SPK2X: every other entry the SAME
    jnz .whole                      ; count again, and nothing else
    mov al, [cs:os88spk_hold]
    out 0x42, al
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
.whole:
%endif
    push si
    push ds
    lds si, [cs:os88spk_rp]
    lodsb                           ; a PWM count, 1..N-1
%ifdef SPK2X
    shr al, 1                       ; (the counts are N's: halved, never 0)
    adc al, 0
    mov [cs:os88spk_hold], al
%endif
    out 0x42, al                    ; ...and the pulse
    mov [cs:os88spk_rp], si
    dec word [cs:os88spk_left]
    jz .ev
    mov al, 0x20                    ; (auto-EOI would save these two, and
    out 0x20, al                    ; is refused: SPEC.md 34.11.6)
    pop ds
    pop si
    pop ax
    iret
.ev:                                ; THE GRANT IS SPENT
    mov ax, [cs:os88spk_kr]
    sub ax, [cs:os88spk_g]          ; the period's samples after it
    jz .per
    mov [cs:os88spk_kr], ax
    call os88spk_grant              ; more of the same period
    mov al, 0x20
    out 0x20, al
    pop ds
    pop si
    pop ax
    iret
.per:                               ; A RATE PERIOD, played: the next one's
    mov ax, [cs:os88spk_kn]         ; samples granted, and the entry the
%ifdef SPK2X
    shr ax, 1                       ; (SPK2X: K is the door's ENTRIES)
%endif
    mov [cs:os88spk_kr], ax         ; kernel would have had
    call os88spk_grant
    pop ds
    pop si
    pop ax
    jmp far [cs:os88spk_chain]

; -----------------------------------------------------------------------------
; os88spk_grant - IF = 0: what the last grant played added to CONS, and the
; next samples granted - no more than are queued (TOTAL - CONS), than this
; period has left ([os88spk_kr]) or than lie before the ring's end. So CONS is
; exact at every period and never a period ahead of the pulse. With NOTHING
; queued the grant is one sample of the silent count, not counted as played:
; the ISR asks again at the next, which costs only while the ring is dry.
; clobbers AX, SI, DS
; -----------------------------------------------------------------------------
os88spk_grant:
    push bx
    call os88spk_acct               ; DS:BX -> TOTAL, CONS after it
    mov ax, [bx]
    sub ax, [bx+2]                  ; queued
    jz .dry
    cmp ax, [cs:os88spk_kr]
    jbe .k
    mov ax, [cs:os88spk_kr]
.k:
    mov si, [bx+2]
    dec bx                          ; RL - 1, the mask: RL is a power of two,
    and si, bx                      ; so a free-running count indexes the
    inc bx                          ; ring as it wraps
    sub bx, si
    cmp ax, bx
    jbe .set
    mov ax, bx
.set:
    mov [cs:os88spk_rp], si
    mov [cs:os88spk_rp+2], ds
    mov byte [cs:os88spk_dry], 0
.g:
    mov [cs:os88spk_g], ax
    mov [cs:os88spk_left], ax
    pop bx
    ret
.dry:
    mov word [cs:os88spk_rp], os88spk_sil
    mov [cs:os88spk_rp+2], cs
    mov byte [cs:os88spk_dry], 1
    inc ax
    jmp short .g

; os88spk_acct - IF = 0: the grant's played samples into CONS (none, for a dry
; one), and the grant closed. out DS:BX -> TOTAL. clobbers AX
os88spk_acct:
    mov ds, [cs:os88spk_rseg]
    mov bx, [cs:os88spk_rl]
    mov ax, [cs:os88spk_g]
    sub ax, [cs:os88spk_left]
    cmp byte [cs:os88spk_dry], 0
    jne .z
    add [bx+2], ax
.z:
    xor ax, ax
    mov [cs:os88spk_g], ax
    mov [cs:os88spk_left], ax
    ret

os88spk_rp:     dw 0, 0             ; the next count, far
os88spk_left:   dw 0                ; the grant's samples not yet played
os88spk_g:      dw 0                ; the grant, as made
os88spk_kr:     dw 0                ; the period's samples after the grant...
os88spk_kn:     dw 0                ; ...and a period's (K, the door's)
os88spk_chain:  dw 0, 0             ; the kernel's IRQ0, far - the door writes
                                    ; these two with os88spk_kn, in this order
os88spk_sil:    db 0                ; a dry grant's count: the table's middle
os88spk_dry:    db 0                ; the grant is that, and plays nothing
os88spk_rl:     dw 0                ; RL: where TOTAL and CONS are
os88spk_rseg:   dw 0                ; the ring's segment
os88spk_n:      dw 0                ; N, the PIT counts a sample
%ifdef SPK2X
os88spk_ph:     db 0                ; SPK2X: 1 after a whole entry
os88spk_hold:   db 0                ; ...and the count it wrote
%endif
