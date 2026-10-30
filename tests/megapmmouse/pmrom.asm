; =============================================================================
; os8088 - tests/megapmmouse/pmrom.asm
;
; A TEST instrument (nothing under tests/ ships): the PicoMEM's IRQ
; multiplexer as far as its USB mouse goes, as a Bochs option ROM at D000:0000
; (tests/megapmmouse.py, KERN-MEGA-PLAN 14.117). Its handler at 0100h is what
; the card's BIOS does with a mouse report (SPEC.md 9.12.7): load it - DX the
; buttons, BX dx, CX dy, signed - and `int 33h` with AX = 0060h.
;
; The report comes from the BIOS's inter-application area, 0040:00F0, where
; tests/megapmmouse/pmtrig.asm puts it before it raises the line:
;   +0  buttons  +1 dx  +2 dy (signed bytes)  +3 how many this handler took
; Being an OPTION ROM's (C000h-EFFFh) is the whole point: that is what makes
; kern_mega reflect the line to real mode (mega_irqrom) instead of dismissing
; it, which is the path the card's reports take on a real machine.
;
; The init at +3 does nothing: driver function 0 (drivers/usbmouse/pmsim.inc)
; points the line's vector here, as the card's own function 0 re-hooks it.
; The last byte is the checksum, filled in by the row.
; =============================================================================
    cpu 8086
    bits 16
    org 0

    db 0x55, 0xAA, 1            ; one 512-byte block
    retf                        ; init: nothing

    times 0x100 - ($ - $$) db 0
pr_irq:                         ; 0100h - PMS_ROMOFF
    push ax
    push bx
    push cx
    push dx
    push ds
    mov ax, 0x0040
    mov ds, ax
    mov dl, [0xF0]
    xor dh, dh
    mov al, [0xF1]
    cbw
    mov bx, ax
    mov al, [0xF2]
    cbw
    mov cx, ax
    inc byte [0xF3]
    mov ax, 0x0060
    int 0x33
    pop ds
    pop dx
    pop cx
    pop bx
    pop ax
    iret

    times 511 - ($ - $$) db 0
    db 0                        ; the checksum
