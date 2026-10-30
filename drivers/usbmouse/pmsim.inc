; =============================================================================
; pmsim.inc - USBMOUSE.DRV's GATE MODEL of a PicoMEM's BIOS (KERN-MEGA-PLAN
; 14.117). NEVER IN THE SHIPPED IMAGE: only `-DPMSIM` includes it, and only
; tests/megapmmouse.py assembles that.
;
; No emulator here has a PicoMEM, and the half of the card that USBMOUSE.DRV
; talks to directly is small: the port-2A3h ramp (skipped under PMSIM) and
; three int 13h functions. This answers those three the way the card's ROM
; does (SPEC.md 9.12.7, tests/picomem.py's `mouse` leg): function 0 says
; DX = AA55h, BX = the ROM's segment and CH = the card's IRQ line, AND points
; that line's vector at the ROM's multiplexer - which is what the real
; function 0 does when something took it. The multiplexer itself is
; tests/megapmmouse/pmrom.asm, a Bochs option ROM at PMS_ROMSEG whose IRQ
; handler at PMS_ROMOFF loads one report and does `int 33h AX=0060h`, as the
; card's does. So everything after the ATTACH is the real path: the kernel's
; reflection of an option ROM's IRQ, its int 33h in real mode, and the
; driver's handler.
;
; Function 10h/11h (mouse on/off) are counted in pms_on, which the row reads.
; Every register but the documented outputs kept, as the ROM's are.
; =============================================================================

PMS_ROMSEG  equ 0xD000          ; the model ROM's segment (pmrom.asm)
PMS_ROMOFF  equ 0x0100          ; ...and its IRQ handler's offset
PMS_LINE    equ 7               ; the card's IRQ line, the 2.x's field jumper

pms_bios:
    cmp ax, 0x6000
    jne .onoff
    push ax
    push es
    xor ax, ax                  ; the line's vector -> the multiplexer, as
    mov es, ax                  ; function 0 re-hooks it (segment 0: the
    mov word [es:(8 + PMS_LINE) * 4], PMS_ROMOFF       ; kernel's own
    mov word [es:(8 + PMS_LINE) * 4 + 2], PMS_ROMSEG   ; null-selector
    pop es                                             ; trap on kern_mega)
    pop ax
    mov bx, PMS_ROMSEG
    mov ch, PMS_LINE
    mov dx, 0xAA55
    ret
.onoff:
    cmp al, 0x10
    jne .off
    inc byte [pms_on]           ; the mouse ON
    ret
.off:
    dec byte [pms_on]           ; ...and OFF (11h)
    ret

pms_on      db 0
