; =============================================================================
; os8088 - apps/apple2/icon.inc
;
; APPLE2's embedded 16x16 icon (SPEC.md 20.2): a monitor on a II+ case, DRAWN
; FOR THIS PORT and not copied. Apple's rainbow mark is trade dress and is not
; here in any form; what is here is a 1bpp silhouette of the machine's shape,
; which is a picture of a beige box (SPEC.md 39.4 - grey rounds to black on
; two adapters of three, so there is nothing to round).
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
; apps/apple2/ is GPL-2-or-later; this file carries no material derived from
; any reference tree. See apps/apple2/COPYING.
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words, bit 15 the leftmost
; pixel, a SET data bit drawn black. The mask is full because the icon is a
; solid tile: it sits on a clean underlay on the desktop grey and on a
; selected row of the Disk window alike.
;
;   ................
;   ..############..       the monitor
;   ..#..........#..
;   ..#.########.#..       ...and its screen
;   ..#.#......#.#..
;   ..#.#......#.#..
;   ..#.########.#..
;   ..#..........#..
;   ..############..
;   .....######.....       the neck
;   .##############.       the case
;   .#............#.
;   .#.####.#####.#.       the keyboard
;   .#............#.
;   .##############.
;   ................
; =============================================================================
    dw 0xFFFF                       ; 16 mask rows (white underlay)
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x0000                       ; 16 data rows
    dw 0x3FFC
    dw 0x2004
    dw 0x2FF4
    dw 0x2814
    dw 0x2814
    dw 0x2FF4
    dw 0x2004
    dw 0x3FFC
    dw 0x07E0
    dw 0x7FFE
    dw 0x4002
    dw 0x5EFA
    dw 0x4002
    dw 0x7FFE
    dw 0x0000
