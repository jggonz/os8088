; =============================================================================
; os8088 - tests/dostrap/dwleave.asm - DWLEAVE.COM, a program that leaves the
; MACHINE changed behind it (SPEC.md 96.54.12)
;
; What compiled BASIC does for the length of a note (Galaxy Trek, 1983), done
; for good and with no attempt to undo it:
;   * IVT 08h written DIRECTLY to its own handler, which counts 31 ticks
;     itself (EOI, iret) and chains the 32nd to the old vector;
;   * PIT channel 0 set to divisor 2048 - 32 times the tick rate - in the
;     ROM's mode 3, as a BASIC runtime writes it;
;   * IVT 0Bh (IRQ3) written directly to a handler in its own memory, and
;     IRQ3 UNMASKED for it;
; Then it prints `DWLEAVE ON` and takes a key with INT 16h AH=00h:
;   s  spin in its own code for ever - a program a close has to stop
;   q  MASK IRQ4 - the serial mouse - as Trek's exit does whatever it
;      found, and exit, code 0, leaving every one of the above in place
; It runs under a real IBM DOS 3.30 unchanged, and a machine it runs on needs
; a reset after it - which is the point.
; =============================================================================
cpu 8086
org 0x100

DIV0    equ 2048                    ; 582 Hz: Trek's note rate

start:
    cli
    xor ax, ax
    mov es, ax
    mov ax, [es:0x08 * 4]           ; the old IRQ0, kept to chain
    mov [old08], ax
    mov ax, [es:0x08 * 4 + 2]
    mov [old08 + 2], ax
    mov word [es:0x08 * 4], irq0
    mov [es:0x08 * 4 + 2], cs
    mov word [es:0x0B * 4], irq3    ; IRQ3: a handler in our own memory
    mov [es:0x0B * 4 + 2], cs
    mov al, 0x36                    ; channel 0, lo/hi, mode 3 (BASIC's)
    out 0x43, al
    mov ax, DIV0
    out 0x40, al
    mov al, ah
    out 0x40, al
    in al, 0x21
    and al, 0xF7                    ; IRQ3 live
    out 0x21, al
    sti
    mov si, s_on
.say:
    lodsb
    or al, al
    jz .key
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp short .say
.key:
    xor ah, ah
    int 0x16
    cmp al, 'q'
    je .quit
    cmp al, 's'
    jne .key
.spin:
    jmp short .spin
.quit:
    in al, 0x21                     ; IRQ4 - the serial mouse - masked, as
    or al, 0x10                     ; Trek's exit does whatever it found
    out 0x21, al
    mov ax, 0x4C00                  ; and NOTHING put back
    int 0x21

irq0:
    dec byte [cs:count]
    and byte [cs:count], 31
    jnz .eoi
    jmp far [cs:old08]              ; the 32nd: the old handler's own EOI
.eoi:
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret

irq3:
    push ax
    mov al, 0x20
    out 0x20, al
    pop ax
    iret

old08:  dd 0
count:  db 0
s_on:   db 'DWLEAVE ON', 13, 10, 0
