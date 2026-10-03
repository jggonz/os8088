; =============================================================================
; os8088 - apps/os88spk.inc
;
; THE SPEAKER RING PLAYER (SPEC.md 34.11): PCM through the PC speaker, on a
; machine with no card, from inside the caller's own FSXF_RATE bracket. A
; LIBRARY, os88ui.inc's shape: the kernel's half is the THIN door
; OSAPI_FSX_SPK - channel 2 as 34.4's PWM and owned, the rate period trimmed
; to whole samples, and all of it put back when the bracket ends - and the
; rest is here, in the package that plays: IRQ0's vector and channel 0's
; count (os88spk_go), the ISR and the ring it plays. The whole player in the
; kernel measured 554 resident bytes; the door is ~50 (34.11.1).
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
; A COVOX PLAYS THROUGH THE SAME PLAYER (SPEC.md 34.14). The Covox Speech
; Thing is a resistor DAC on a parallel port's data lines: a sample is one
; `out dx, al` of the SAMPLE ITSELF where the speaker's is an `out 42h, al` of
; a count, and that is the whole difference. So os88spk_lpt names the port
; before os88spk_init, and then: the table is the IDENTITY (t[s] = s, so a
; producer's xlatb and os88spkfx's composed rows pass samples through
; unchanged), silence is 80h, there is one entry a sample whatever CL asked
; for (no carrier to push above hearing), os88spk_go hands IRQ0 to
; os88spk_isrd and turns the SPEAKER off at 61h inside the door's own IF = 0
; window - the door still takes channel 2 and its arbitration, which is what
; keeps a beep from sounding over the play - and the door's close puts 61h
; back as it does for the speaker. The rate range is the speaker's: its
; floors are the ISR's cost on an 8088 as much as the pulse's, and the DAC's
; ISR is a `push`, a `mov` and a `pop` dearer.
;
;   os88spk_init  DI = the ring's segment, AH bits 4-5 = its size code, DX =
;                 the rate, CL = PULSES A SAMPLE (0 or 1: one). The pulse's
;                 period is N / pulses, N = 1,193,182 / rate, and must be
;                 74..255 PIT counts, 48..255 on a 286 or better (34.11.8)
;                 - so the CARRIER is the rate x pulses
;                 (SPEC.md 34.11.7). Writes the table, for that period;
;                 plays nothing. CF=1 it cannot be played. Any time
;   os88spk_go    the speaker plays the ring from CONS. INSIDE YOUR OWN
;                 FSXF_RATE BRACKET; CF=1, AX = SPK_E_* refused. The hook's
;                 periods become K samples each (OSAPI_FSX_SPK)
;   os88spk_stop  it stops where it is - a pause, a stop, a seek. CONS is
;                 exact; the ring is kept, and os88spk_go goes on from it.
;                 The bracket's end stops it too, so call go again in the next
;                 bracket. Harmless when stopped
;   os88spk_lpt   DX = a Covox's data port, or 0 for the speaker (the
;                 default). Before os88spk_init, which builds the table for it
;   os88spk_lptq  out CF = 0, DX = the Covox's port: the sound tier is the
;                 Covox and SOUND.DRV found a port (SND_CAP_LPTDAC, then its
;                 SNDV_DACINFO). CF = 1 none. UI task or worker
;
; All preserve every register but their outputs. Labels os88spk_; the
; state is in the including package's own image, reached through CS.
; =============================================================================

OS88SPK_DRYN    equ 16              ; samples a dry grant plays: 2 ms at 8 kHz

; -----------------------------------------------------------------------------
os88spk_lpt:
    mov [cs:os88spk_lptp], dx
    ret

SNDV_DACINFO    equ 6               ; SOUND.DRV's verb (drivers/sound/sndpkg.inc,
                                    ; typed out again so a package needs no
                                    ; -I drivers/sound/; t_mirror holds the
                                    ; two equal, and NASM takes the second
                                    ; definition of an equal value silently)

; -----------------------------------------------------------------------------
os88spk_lptq:
    push ax
    push bx
    call OSAPI_SND_CAPS             ; AX = caps; BL and DX spent
    test ax, SND_CAP_LPTDAC         ; the user's tier, and a port behind it
    jz .no                          ; (SPEC.md 34.14) - the cap is the driver's
    mov bh, DRVC_SOUND              ; alone, so the verb is SOUND.DRV's
    mov bl, SNDV_DACINFO
    call OSAPI_DRV_CALL             ; out AX = the port, or CF = 1
    jc .no
    mov dx, ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
os88spk_init:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov [cs:os88spk_pin], cl
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
    mov cl, [cs:os88spk_pin]        ; CL = pulses a sample (0 is one)
    cmp word [cs:os88spk_lptp], 0   ; ...and ONE on a Covox, whatever was
    je .ppa                         ; asked: a DAC holds its level, so there
    xor cl, cl                      ; is no carrier to push above hearing
.ppa:
    or cl, cl
    jnz .pp
    inc cx
.pp:
    cmp cl, 2                       ; (two is what os88spk_isrm plays;
    ja .no                          ; 3 and 4 are the format's, 34.11.7)
    mov [cs:os88spk_pp], cl
    div cl                          ; AL = N / pulses, the pulse's period
    or ah, ah                       ; (N < 1,024 here, so AH is the
    jnz .no                         ; remainder): exact, or not played
    xor ah, ah
    cmp ax, 74                      ; ...and 74 the shortest period a pulse
    jae .nlo                        ; ends in (SPEC.md 34.4) - or 48 on a
    cmp al, 48                      ; 286 or better, whose ISR is a fraction
    jb .no                          ; of an 8088's (34.11.8). THE ONLY
    push ax                         ; CHECK: the door trusts this one
    call OSAPI_CPU_INFO
    cmp al, CPU_8086
    pop ax
    je .no
.nlo:
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
    cmp word [cs:os88spk_lptp], 0
    je .tab
.ltab:                              ; ...or a COVOX'S: t[s] = s, the sample
    mov al, dl                      ; is the voltage (SPEC.md 34.14)
    stosb
    inc dl
    jnz .ltab
    jmp short .sil
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
.sil:
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
    push ax                         ; A DRY RING PLAYS A RUN OF SILENCE,
    push cx                         ; OS88SPK_DRYN samples a grant: one
    push di                         ; sample a grant made the grant's event
    push es                         ; every pulse, ~400 cycles more each,
    push cs                         ; and a producer the ring had outrun
    pop es                          ; was then starved by the very pulses it
    mov di, os88spk_silb            ; was behind - and never caught up
    mov al, [cs:os88spk_sil]        ; (SPEC.md 34.11.2)
    mov cx, OS88SPK_DRYN
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    mov word [cs:os88spk_rp], os88spk_silb  ; the first sample a silent one,
    mov [cs:os88spk_rp+2], cs       ; and its event the first grant
    mov byte [cs:os88spk_dry], 1
    mov word [cs:os88spk_g], 1
    mov word [cs:os88spk_left], 1
    mov word [cs:os88spk_kr], 1     ; ...which ends a period: K and the
    popf                            ; chain are set by then, below
    mov dx, [cs:os88spk_n]
    mov si, os88spk_isr
    mov ax, [cs:os88spk_lptp]
    or ax, ax
    jz .spk
    mov [cs:os88spk_isrd.port+1], ax ; A COVOX (SPEC.md 34.14): its port is
    mov si, os88spk_isrd            ; the ISR's own immediate
    jmp short .one
.spk:
    mov al, [cs:os88spk_pp]
    cmp al, 1
    je .one
    mov byte [cs:os88spk_isrm.j+1], os88spk_isrm.whole - (os88spk_isrm.j + 2)
    mov al, [cs:os88spk_sil]        ; PULSES A SAMPLE (34.11.7): the first
    mov [cs:os88spk_isrm.hv+1], al  ; entry a whole one, and the half path
                                    ; silence until it has a count
    mov si, os88spk_isrm
.one:
    mov bx, cs                      ; the ISR's segment, for the door
    xor al, al
    pushf                           ; IF = 0 FROM THE DOOR until IRQ0 is the
    cli                             ; ISR's and channel 0 counts N (34.11.1)
    call OSAPI_FSX_SPK              ; AX = K; BX and DX spent
    jc .no
    mov [cs:os88spk_kn], ax
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, si                      ; IRQ0 IS THE ISR'S, and what it was -
    xchg ax, [0x20]                 ; the kernel's own IRQ0, which the door's
    mov [cs:os88spk_chain], ax      ; close puts back - is the chain a
    mov ax, cs                      ; period's last sample jumps to
    xchg ax, [0x22]
    mov [cs:os88spk_chain+2], ax
    pop ds
    mov al, 0x34                    ; channel 0 at N, a sample an IRQ0: mode
    out 0x43, al                    ; 2, lobyte then hibyte - exactly the N
    mov ax, [cs:os88spk_n]          ; the door trimmed the period to, or
    out 0x40, al                    ; [ticks] drifts
    mov al, ah
    out 0x40, al
    cmp word [cs:os88spk_lptp], 0
    je .pwm
    in al, 0x61                     ; A COVOX: the door turned the speaker on
    and al, 0xFC                    ; for a PWM nobody will write, so it goes
    out 0x61, al                    ; off again in the same window - gate and
                                    ; data both, read-modify-write as 34.1
                                    ; binds. The close's spk_pcm_idle puts the
                                    ; boot state back, exactly as for the
                                    ; speaker
.pwm:
    popf
    xor ax, ax
    jmp short .out
.no:
    popf
    stc
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
    push si
    push ds
    cld                             ; the interrupted code's DF is not ours
    lds si, [cs:os88spk_rp]
    lodsb                           ; a PWM count, 1..N-1
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
    cmp byte [cs:os88spk_pp], 1     ; kernel would have had - K is the
    je .k1                          ; door's ENTRIES, a sample's pulses
    push dx                         ; each (34.11.7): once a period
    xor dx, dx
    push bx
    mov bl, [cs:os88spk_pp]
    xor bh, bh
    div bx
    pop bx
    pop dx
.k1:
    mov [cs:os88spk_kr], ax
    call os88spk_grant
    pop ds
    pop si
    pop ax
    jmp far [cs:os88spk_chain]

; -----------------------------------------------------------------------------
; os88spk_isrm - os88spk_isr for TWO PULSES A SAMPLE (34.11.7): channel 0 runs
; at N / 2, so the carrier is twice the rate. A sample's first entry is the
; WHOLE path - os88spk_isr's, and its count written into the half path's own
; immediate; the second is the HALF PATH - that count again and the EOI,
; nothing read, nothing counted. Which one an entry takes is the displacement
; of the jump it starts with, each path pointing it at the other: 35 cycles
; a whole pulse cheaper than a counter byte (34.11.7.1). The event paths
; are os88spk_isr's, entered with the same three words pushed
; -----------------------------------------------------------------------------
os88spk_isrm:
.j:
    jmp short .whole                ; (the displacement is the toggle: the
.half:                              ; whole path points it here, this path
    push ax                         ; back there)
    mov byte [cs:.j+1], .whole - (.j + 2)
.hv:
    mov al, 0                       ; (the whole path's count, patched in)
    out 0x42, al
    mov al, 0x20
    out 0x20, al
    pop ax
    iret
.whole:
    push ax
    mov byte [cs:.j+1], .half - (.j + 2)
    push si
    push ds
    cld                             ; (os88spk_isr's reason)
    lds si, [cs:os88spk_rp]
    lodsb                           ; a PWM count, made for N / pulses
    out 0x42, al
    mov [cs:.hv+1], al
    mov [cs:os88spk_rp], si
    dec word [cs:os88spk_left]
    jz .ev
    mov al, 0x20
    out 0x20, al
    pop ds
    pop si
    pop ax
    iret
.ev:
    jmp os88spk_isr.ev

; -----------------------------------------------------------------------------
; os88spk_isrd - os88spk_isr for a COVOX (SPEC.md 34.14): the sample out of the
; parallel port's data register instead of a count into channel 2. DX is the
; one more register, and the port is the `mov`'s own immediate, written by
; os88spk_go - so nothing is read per sample that os88spk_isr does not read.
; The event paths are os88spk_isr's, entered with its three words pushed
; -----------------------------------------------------------------------------
os88spk_isrd:
    push ax
    push si
    push ds
    push dx
    cld                             ; (os88spk_isr's reason)
    lds si, [cs:os88spk_rp]
    lodsb                           ; a sample, unsigned
.port:
    mov dx, 0x0378                  ; (the port, patched in)
    out dx, al                      ; ...and the voltage
    pop dx
    mov [cs:os88spk_rp], si
    dec word [cs:os88spk_left]
    jz .ev
    mov al, 0x20
    out 0x20, al
    pop ds
    pop si
    pop ax
    iret
.ev:
    jmp os88spk_isr.ev

; -----------------------------------------------------------------------------
; os88spk_grant - IF = 0: what the last grant played added to CONS, and the
; next samples granted - no more than are queued (TOTAL - CONS), than this
; period has left ([os88spk_kr]) or than lie before the ring's end. So CONS is
; exact at every period and never a period ahead of the pulse. With NOTHING
; queued the grant is a run of OS88SPK_DRYN samples of the silent count (or
; the period's rest), not counted as played - short enough that data arriving
; waits ~2 ms for it, long enough that a dry ring does not make every pulse an
; event.
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
    mov word [cs:os88spk_rp], os88spk_silb
    mov [cs:os88spk_rp+2], cs
    mov byte [cs:os88spk_dry], 1
    mov ax, OS88SPK_DRYN            ; a run of silence, the period's end the
    cmp ax, [cs:os88spk_kr]         ; sooner
    jbe .g
    mov ax, [cs:os88spk_kr]
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
os88spk_chain:  dw 0, 0             ; the kernel's IRQ0, far: what the vector
                                    ; held when os88spk_go took it
os88spk_sil:    db 0                ; a dry grant's count: the table's middle
os88spk_silb:   times OS88SPK_DRYN db 0     ; ...a run of it, os88spk_go's
os88spk_dry:    db 0                ; the grant is that, and plays nothing
os88spk_rl:     dw 0                ; RL: where TOTAL and CONS are
os88spk_rseg:   dw 0                ; the ring's segment
os88spk_n:      dw 0                ; the PIT counts a PULSE: N / pulses
os88spk_pp:     db 1                ; pulses a sample (34.11.7)
os88spk_pin:    db 0                ; ...as init was handed it
os88spk_lptp:   dw 0                ; a Covox's data port, 0 = the speaker
                                    ; (os88spk_lpt, SPEC.md 34.14)
