; =============================================================================
; os8088 - apps/infones/icon.inc
;
; INFONES's embedded 16x16 icon (SPEC.md 20.2, 91.14.1): a game controller
; DRAWN FOR THIS PORT, not copied. InfoNES's own res/InfoNES.ico is the
; reference LOOK - a grey pad with a cross, two round buttons and two bars -
; and nothing of it is transcribed: that file is a colour image of a physical
; object and this is a 1bpp silhouette (SPEC.md 39.4 - grey rounds to black on
; two adapters of three, so there is nothing to round).
;
; Part of INFONES (SPEC.md 91), derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. THIS FILE carries no InfoNES
; material.
;
; crt0.asm puts it at file offset 32 and checks both the offset and the
; length: 16 MASK words - the white underlay - then 16 DATA words, bit 15 the
; leftmost pixel, a SET data bit drawn black. The mask is FULL because the
; icon is a solid tile: it has to read the same on the desktop grey, on a
; selected row of the Disk window and on the dock strip.
; =============================================================================

; --- the mask: a solid 16x16 white tile ------------------------------------
    times 16 dw 0xFFFF

; --- the data ---------------------------------------------------------------
    dw 0x0000                       ; ................
    dw 0x0000                       ; ................
    dw 0x0000                       ; ................
    dw 0x7FFE                       ; .##############.
    dw 0x4002                       ; .#............#.
    dw 0x4002                       ; .#............#.
    dw 0x4802                       ; .#..#.........#.   the d-pad's arm
    dw 0x5CDA                       ; .#.###..##.##.#.   ...its bar, and the
    dw 0x48DA                       ; .#..#...##.##.#.   two buttons
    dw 0x4002                       ; .#............#.
    dw 0x4782                       ; .#...####.....#.   Select and Start
    dw 0x4002                       ; .#............#.
    dw 0x7FFE                       ; .##############.
    dw 0x0000                       ; ................
    dw 0x0000                       ; ................
    dw 0x0000                       ; ................
