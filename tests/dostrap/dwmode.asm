; =============================================================================
; os8088 - tests/dostrap/dwmode.asm - DWMODE.COM, three things a compiled
; BASIC program does to a live DOS window (SPEC.md 96.54.11)
;
; It fills PSP:1000h-1FFFh with A5h - memory a .COM owns and nothing else
; should write - then sets TEXT mode 3 through int 10h AH=00h (BASIC's
; runtime does, at its start) and prints `DWMODE TEXT` with the teletype.
; Then it takes a key with INT 16h AH=00h:
;   g  set CGA mode 4 (the full screen), then mode 3 again, check the A5h
;      block and print `DWMODE MEM OK` or `DWMODE MEM BAD`, wait for a key and
;      exit with code 0 (OK) or 1 (BAD)
;   b  turn the PC speaker ON (port 61h bits 0 and 1, PIT channel 2 at
;      ~1 kHz), as a BEEP does, and spin in its own code for ever - which is
;      a program a close has to stop in the middle of a tone
;   q  exit, code 0
; It runs under a real IBM DOS 3.30 unchanged (b beeps until reset there).
; =============================================================================
cpu 8086
org 0x100

FILL    equ 0x0400
FILLN   equ 0xEC00

start:
    mov di, FILL
    mov cx, FILLN / 2
    mov ax, 0xA5A5
    cld
    rep stosw
    mov ax, 0x0003                  ; TEXT mode: a clear, and nothing more
    int 0x10
    mov si, s_text
    call tty
.key:
    xor ah, ah
    int 0x16
    cmp al, 'q'
    je .ok
    cmp al, 'b'
    je .beep
    cmp al, 'g'
    jne .key
    mov ax, 0x0004                  ; a GRAPHICS mode: the full screen
    int 0x10
    mov ax, 0x0003
    int 0x10
    mov di, FILL
    mov cx, FILLN / 2
    mov ax, 0xA5A5
    repe scasw
    mov si, s_bad
    jne .said
    mov si, s_good
.said:
    push si
    call tty
    xor ah, ah
    int 0x16
    pop si
    cmp si, s_good
    je .ok
    mov ax, 0x4C01
    int 0x21
.ok:
    mov ax, 0x4C00
    int 0x21

.beep:
    mov al, 0xB6                    ; channel 2, square wave
    out 0x43, al
    mov ax, 1193                    ; ~1 kHz
    out 0x42, al
    mov al, ah
    out 0x42, al
    in al, 0x61
    or al, 3                        ; gate and data: the tone
    out 0x61, al
    mov si, s_beep
    call tty
.spin:
    jmp short .spin

tty:                                ; SI = a zero-ended string, by AH=0Eh
    lodsb
    or al, al
    jz .r
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp short tty
.r:
    ret

s_text: db 'DWMODE TEXT', 13, 10, 0
s_good: db 'DWMODE MEM OK', 13, 10, 0
s_bad:  db 'DWMODE MEM BAD', 13, 10, 0
s_beep: db 'DWMODE BEEP', 13, 10, 0

%if $ - $$ + 0x100 > FILL
 %error "the code reaches the A5h block"
%endif
