; KBHOLD.COM - DOES A KEY ARRIVING ON A FULL BIOS BUFFER MAKE THE ROM BEEP?
; OURS, MIT with the rest of the tree.
;
; The reported failure (SPEC.md 96.50) is: hold a direction key in a game busy
; drawing a frame, the BIOS key buffer fills, and the ROM's `int 09h` beeps -
; for longer than the typematic interval, so the next repeat overflows DURING
; the beep and it never stops. os8088's kernel has SPEC.md 9.8's guard in front
; of the ROM for exactly this; `kern_dos` did not.
;
; THE BUFFER IS FORGED FROM IN HERE, and that is the whole reason this is a DOS
; program rather than four host-side pokes. A forge written from the host is
; DRAINED before the test key lands - the guest is sitting in `int 16h` and
; empties it between the write and the keystroke, which is what an early
; attempt measured and misread as "both arms survived". Nothing drains it while
; this program is spinning, because this program never reads a key.
;
; THE OBSERVABLE IS THE SPEAKER AND NOT THE BUFFER, and that is not a detour:
; with the guard the tail winds back and the key is stored, without it the ROM
; drops the key - and BOTH leave a full buffer. The difference is audible and
; nothing else. So this samples port 61h's timer-2 gate and speaker-data bits
; across the window and counts how many samples had either set. A gated tone
; holds them for its whole duration and a toggled one has them on about half
; the samples; both count.
;
; It prints the int 09h VECTOR first, because that is the one-line answer to
; "which machine am I on": F000:xxxx is the ROM's own handler and anything else
; is a guard in front of it.
    org 0x100
    cpu 8086

BDA         equ 0x40
KB_HEAD     equ 0x1A
KB_TAIL     equ 0x1C
KB_BUFB     equ 0x1E
KB_BUFE     equ 0x3E
ROUNDS      equ 10              ; outer turns of a 65,536-sample inner loop.
                                ; ~35 cycles a sample on a 4.77MHz 8088 is
                                ; ~0.48s a round, so this is ~5 seconds - and
                                ; it is a COUNT rather than a clock read on
                                ; purpose: the window has to be the same
                                ; amount of WORK on both arms of the A/B, and
                                ; a beep that stops the machine dead would
                                ; make a tick-bounded window measure itself.
                                ; The first draft was 24,000 samples, which is
                                ; 0.1s: the program finished and the bracket
                                ; tore down between two 0.25s polls, so the
                                ; harness only ever saw the desktop behind it

start:
    mov ah, 0x1A                ; our own DTA (tests/dostrap/dosref.asm's
    mov dx, dta                 ; reason), before anything else
    int 0x21

    ; --- 1. WHOSE int 09h IS IT? --------------------------------------------
    mov si, s_vec
    call puts
    xor ax, ax
    mov es, ax
    mov ax, [es:0x09*4+2]
    call hex4
    mov al, ':'
    call putc
    mov ax, [es:0x09*4]
    call hex4
    call eol

    ; --- 2. PROVE THE INSTRUMENT, BEFORE ANYTHING IS FORGED -----------------
    ; **A ZERO NOBODY CAN VALIDATE IS NOT A MEASUREMENT.** If port 61h bit 1
    ; never reads back set on this machine then the silent arm and the beeping
    ; arm both count zero, and the probe reports the defect fixed on every
    ; build for ever. So: sound one deliberately and count it.
    mov al, 0xB6                ; timer 2, square wave
    out 0x43, al
    mov al, 0x00
    out 0x42, al
    mov al, 0x08
    out 0x42, al
    in al, 0x61
    mov [spk_sv], al
    or al, 0x03                 ; gate the timer AND let it reach the cone
    out 0x61, al
    xor ax, ax
    mov [spk], ax
    mov [spk+2], ax
    mov [arr], ax
    mov [arr+2], ax
    mov byte [rounds], 1
    call watch
    mov al, [spk_sv]            ; ...and off again, exactly as it was
    out 0x61, al
    mov si, s_ctl
    call puts
    mov ax, [spk+2]
    call hex4
    mov ax, [spk]
    call hex4
    call eol

    ; --- 3. FORGE A FULL BUFFER, AND WATCH IT WITH NOTHING IN BETWEEN -------
    ; **NO I/O BETWEEN THE FORGE AND THE WINDOW**, and the head/tail are
    ; captured into our own words rather than re-read afterwards. The first
    ; draft printed `forged FULL head=.. tail=..` between the two, and a key
    ; landing during those `int 21h` calls wound the tail back BEFORE the
    ; measurement started - so the probe reported the condition it had set up
    ; as 003A and its own harness failed it for not setting up 003C. An
    ; intermittent that is entirely the instrument's.
    ;
    ; Full, by the BIOS's own definition, is tail + 2 == head: 1Eh with 3Ch is
    ; that, the next slot the ROM would fill wrapping to the head.
    mov ax, BDA
    mov es, ax
    mov di, KB_BUFB
    mov cx, (KB_BUFE - KB_BUFB) / 2
    mov ax, 0x2C5A              ; **A MARKER, AND NOT THE KEY THE HARNESS
                                ; SENDS** (it sends 'A'). 'Z', scancode 2Ch,
                                ; sixteen times: what distinguishes the two
                                ; arms is whether an arriving key ever REACHES
                                ; a slot, and that cannot be read off the
                                ; POINTERS - the guard frees the newest slot
                                ; and the ROM immediately refills it, so head
                                ; and tail come back to 1Eh/3Ch on both arms
                                ; and which phase the window ends in is luck.
                                ; The first draft asserted the tail and was
                                ; flaky for exactly that reason
    cld
    rep stosw
    xor ax, ax
    mov [spk], ax
    mov [spk+2], ax
    mov [arr], ax
    mov [arr+2], ax
    mov byte [rounds], ROUNDS
    cli
    mov word [es:KB_HEAD], KB_BUFB
    mov word [es:KB_TAIL], KB_BUFE - 2
    mov ax, [es:KB_HEAD]
    mov [h0], ax
    mov ax, [es:KB_TAIL]
    mov [t0], ax
    sti
    call watch
    mov ax, BDA
    mov es, ax
    mov ax, [es:KB_HEAD]
    mov [h1], ax
    mov ax, [es:KB_TAIL]
    mov [t1], ax
    ; ...and how many slots the marker has been driven out of, which IS the
    ; measurement: one or more means an arriving key was STORED on a full
    ; buffer, zero means every one was dropped.
    xor dx, dx
    mov di, KB_BUFB
    mov cx, (KB_BUFE - KB_BUFB) / 2
.count:
    mov ax, [es:di]
    cmp ax, 0x2C5A
    je .same
    inc dx
.same:
    inc di
    inc di
    loop .count
    mov [stored], dx

    ; --- ...and only now, the report ----------------------------------------
    mov si, s_forged
    call puts
    mov ax, [h0]
    call hex4
    mov si, s_tail
    call puts
    mov ax, [t0]
    call hex4
    call eol
    mov si, s_spk
    call puts
    mov ax, [spk+2]
    call hex4
    mov ax, [spk]
    call hex4
    call eol
    mov si, s_arr
    call puts
    mov ax, [arr+2]
    call hex4
    mov ax, [arr]
    call hex4
    call eol
    mov si, s_stored
    call puts
    mov ax, [stored]
    call hex4
    call eol
    mov si, s_after
    call puts
    mov ax, [h1]
    call hex4
    mov si, s_tail
    call puts
    mov ax, [t1]
    call hex4
    call eol

    ; --- 4. leave the machine as we found it --------------------------------
    mov ax, BDA
    mov es, ax
    cli
    mov word [es:KB_HEAD], KB_BUFB
    mov word [es:KB_TAIL], KB_BUFB
    sti
    mov si, s_done
    call puts
    ; **AND WAIT, so the text screen outlives the bracket.** A program that
    ; prints and exits takes its screen with it: the box tears the bracket
    ; down and the harness, polling four times a second, reads the graphical
    ; desktop behind it and reports that nothing was printed.
.hold:
    mov ah, 0x00
    int 0x16
    cmp al, 'x'
    jne .hold
    mov ax, 0x4C00
    int 0x21

; --- watch - sample port 61h [rounds] x 65,536 times, counting the speaker --
; Clobbers AX and CX; [spk] is the 32-bit tally and the caller zeroes it.
;
; **AND IT COUNTS ARRIVALS TOO, WHICH IS NOT A REFINEMENT.** A dropped key
; changes NOTHING in the BDA - that is what dropped means - so "the tail did
; not move" is equally consistent with `the ROM dropped every key` and with
; `no key ever reached the guest`. The first draft of this probe could not
; tell those apart and its unguarded arm was therefore VACUOUS: it reported a
; drop it had not witnessed.
;
; 0040:0017 is the shift-state byte, and the ROM updates it from a modifier's
; make and break codes whether or not there is room in the buffer. So a
; harness holding SHIFT down moves it, a full buffer notwithstanding, and a
; count of how many samples saw it DIFFER from the last one is proof that
; int 09h was running at all.
;
; A COUNT AND NOT A CLOCK, deliberately: the window has to be the same amount
; of WORK on both arms of the A/B, and a beep that stops the machine dead
; would make a tick-bounded window measure itself.
watch:
    push es
    push bx
    mov ax, BDA
    mov es, ax
    mov al, [es:0x17]
    mov [kflast], al
.round:
    xor cx, cx                  ; 0 is 65,536 turns of `loop`
.w:
    mov al, [es:0x17]           ; the shift state, which a modifier moves even
    cmp al, [kflast]            ; on a full buffer
    je .nokf
    mov [kflast], al
    add word [arr], 1
    adc word [arr+2], 0
.nokf:
    in al, 0x61
    test al, 0x02               ; **BIT 1, THE SPEAKER DATA LINE, AND NOT BIT 0
                                ; AS WELL.** Bit 0 is the timer-2 GATE and a PC
                                ; leaves it set whether or not anything is
                                ; sounding, so `test al, 3` matched every
                                ; sample of a SILENT machine - 655,360 of
                                ; 655,360, which reads exactly like a beep that
                                ; never stops.
    jz .next
    add word [spk], 1
    adc word [spk+2], 0
.next:
    loop .w
    dec byte [rounds]
    jnz .round
    pop bx
    pop es
    ret

; --- kbstate - "head=xxxx tail=xxxx" ----------------------------------------
kbstate:
    push ax
    push es
    mov ax, BDA
    mov es, ax
    mov si, s_head
    call puts
    mov ax, [es:KB_HEAD]
    call hex4
    mov si, s_tail
    call puts
    mov ax, [es:KB_TAIL]
    call hex4
    call eol
    pop es
    pop ax
    ret

; --- helpers (tests/dostrap/dosref.asm's, and through statics for its reason)
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
hex2:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call hexd
    pop ax
    and al, 0x0F
    call hexd
    pop cx
    pop ax
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
puts:
    push ax
.l:
    mov al, [si]
    inc si
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret

s_vec:    db 'KBHOLD int09=', 0
s_forged: db 'forged FULL   head=', 0
s_after:  db 'after  watch  head=', 0
s_head:   db 'head=', 0
s_tail:   db ' tail=', 0
s_ctl:    db 'speaker CONTROL     =', 0
s_spk:    db 'speaker-on samples  =', 0
s_arr:    db 'shift-state changes =', 0
s_stored: db 'slots taken by a key=', 0
s_done:   db 'KBHOLD READY', 13, 10, 0
spk:      dd 0
arr:      dd 0
kflast:   db 0
spk_sv:   db 0
stored:   dw 0
h0:       dw 0
t0:       dw 0
h1:       dw 0
t1:       dw 0
rounds:   db 0
dta:      times 128 db 0
