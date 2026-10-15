; =============================================================================
; apps/redline/csfe.inc - the WD FE3600 row's knob (SPEC.md 103.6.2)
;
; Shared as source by REDLINE and RLAPPLY: cs_fe_set is the one write either
; of them makes. The verified sweep is csweep.inc's, REDLINE's alone, and
; RLAPPLY assembles without it (CS_APPLY_ONLY).
;
; The FE3001 makes every bus cycle on an FE3600 board. An 8-bit cycle is Ts +
; Tc and then R4 wait states of two PROCLKs each (62.5 ns at 16 MHz), and the
; cycle ends on a SYSCLK edge, so an odd R4 behaves as the next even one: the
; sweep steps by 2, one bus clock a step [FE3001 p14, p18]. Appendix A gives
; R4 = 10 at 16 MHz [FE3001 p57]. Its registers are WRITE ONLY - "All other
; I/O locations are write only" [FE3001 p15] - so what the BIOS chose cannot
; be read back, and a reset puts it back.
;
;   063h  bit 2 CLKHS (the 16 MHz clock), bit 3 DMA speed, bit 6 STOP THE
;         CLOCKS (never set here), bit 7 lock. 04h unlocks at full speed and
;         standard DMA; 84h locks again [FE3001 p17]
;   072h  the command register pointer, its number in bits 7,6,3,2
;   073h  the value, in the same bits [FE3001A p24]
;
; Measured on the Packard Bell Victory, the one board this row is VERIFIED
; on (docs/reports/FE3001-VICTORY-2026-10-13.md): a card byte 915 ns as the
; BIOS left it, 651 at R4 = 10, 523 at 8, and 6 read 5,108 bytes wrong.
; =============================================================================

CS_FE_BOOK  equ 10                  ; Appendix A, 16 MHz
CS_FE_FLOOR equ 4                   ; the sweep never goes below
CS_FE_UNLK  equ 0x04
CS_FE_LOCK  equ 0x84
CS_FE_R4    equ 4

; cs_fe_enc - AL = a 4-bit number -> AL as 072h/073h take it (bits 7,6,3,2)
cs_fe_enc:
    push cx
    mov ah, al
    and al, 3
    shl al, 1
    shl al, 1                       ; n1,n0 -> bits 3,2
    and ah, 0x0C
    mov cl, 4
    shl ah, cl                      ; n3,n2 -> bits 7,6
    or al, ah
    pop cx
    ret

; cs_fe_set - AL = R4 (0-15): unlock, point at R4, write it, lock again, with
; interrupts off throughout. Preserves every register.
cs_fe_set:
    push ax
    pushf
    cli
    push ax
    mov al, CS_FE_UNLK
    out 0x63, al
    mov al, CS_FE_R4
    call cs_fe_enc
    out 0x72, al
    pop ax
    call cs_fe_enc
    out 0x73, al
    mov al, CS_FE_LOCK
    out 0x63, al
    popf
    pop ax
    ret

%ifndef CS_APPLY_ONLY
; The verified sweep is csweep.inc's, for every row; this file is the FE3600's
; knob and its cycle sum.

; -----------------------------------------------------------------------------
; cs_fewaits - R4 READ OFF THE BUS (SPEC.md 103.6.3.1). The FE3001's book
; makes an 8-bit cycle Ts + Tc + R4 wait states of two PROCLKs - one CPU
; clock - each, ending on a SYSCLK edge [FE3001 p14, p18]. So a read timed
; over RAM is R4 + CS_FE_OVER tenths of a CPU clock: the edge's half a clock
; on average, less what a RAM read costs - fitted on the Victory, whose
; 915, 651 and 523 ns at 16 MHz are R4 14, 10 and 8. And an odd R4 runs as
; the next even one, so the answer is EVEN, and the classes it separates are
; two clocks apart: a reading may be a whole clock off and still name R4.
; in AX = the read in tenths of a CPU clock, or 0FFFFh for none
; out CF = 0, AL = R4 (even, at most CS_FE_MAXW); CF = 1 nothing to read
; -----------------------------------------------------------------------------
CS_FE_OVER  equ 4
CS_FE_IOOVER equ 27                 ; the I/O read's: an IN over a NOP is the
                                    ; whole bus cycle, so R4 + 2.7 clocks -
                                    ; the Victory's 16.9 and 10.6 at R4 14
                                    ; and 8 (SPEC.md 103.6.3.1)
CS_FE_MAXW  equ 18

; cs_fewaitsio - the same answer from the 8-bit I/O READ (cs_iotime): R4
; governs 8-bit I/O as well as memory - the Victory's floppy status read
; moved 6.3 clocks between R4 14 and 8 - so a board with no clean memory
; source still reads its setting. in AX = tenths of a clock or 0FFFFh
cs_fewaitsio:
    cmp ax, 0FFFFh
    je cs_fewaits
    sub ax, CS_FE_IOOVER - CS_FE_OVER
    jnc cs_fewaits
    xor ax, ax
cs_fewaits:
    cmp ax, 0FFFFh
    je .none
    push cx
    push dx
    sub ax, CS_FE_OVER
    jnc .pos
    xor ax, ax
.pos:
    add ax, 10                      ; the nearest EVEN count: (t + 10) / 20 x 2
    xor dx, dx
    mov cx, 20
    div cx
    shl ax, 1
    pop dx
    pop cx
    cmp ax, CS_FE_MAXW
    jbe .ok
    mov ax, CS_FE_MAXW
.ok:
    clc
    ret
.none:
    stc
    ret
%endif
