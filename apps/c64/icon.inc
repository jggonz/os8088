; =============================================================================
; os8088 - apps/c64/icon.inc
;
; C64's embedded 16x16 icon (SPEC.md 20.2, C64-SPEC §2): a breadbin
; drawn FOR THIS PORT, not copied. VICE's own data/common/vice-x64_16.png is
; the reference LOOK - a beige wedge with three rows of keys and a dark lip -
; and nothing of it is transcribed: it is a colour photograph of a machine and
; this is a 1bpp silhouette (SPEC.md 39.4 - grey rounds to black, so there is
; nothing to round).
;
; Part of C64, a reimplementation of VICE 3.10's x64 (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; this file
; carries no VICE material. See apps/c64/COPYING.
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words, bit 15 the
; leftmost pixel, a SET data bit drawn black. The mask is full because the
; icon is a solid tile: it sits on a clean underlay on the desktop grey and
; on a selected row of the Disk window alike.
;
;   ................       the top of the case
;   ..############..
;   ..#..........#..
;   ..#..........#..
;   .##############.       the keyboard deck
;   .#............#.
;   .#.###.###.##.#.       three rows of keys
;   .#............#.
;   .#.###.###.##.#.
;   .#............#.
;   .#.###.###.##.#.
;   .#............#.
;   .##############.
;   ################       the front lip
;   #..............#
;   ################
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
    dw 0x2004
    dw 0x7FFE
    dw 0x4002
    dw 0x5DDA
    dw 0x4002
    dw 0x5DDA
    dw 0x4002
    dw 0x5DDA
    dw 0x4002
    dw 0x7FFE
    dw 0xFFFF
    dw 0x8001
    dw 0xFFFF
