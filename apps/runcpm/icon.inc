; =============================================================================
; os8088 - apps/runcpm/icon.inc
;
; RUNCPM's embedded 16x16 icon (SPEC.md 20.2, 71.5): RunCPM's OWN icon,
; RunCPM/RunCPM.ico (IDI_ICON1 in RunCPM.rc - the one 16x16 image in it),
; transcribed pixel for pixel: a two-row window title strip - a white pixel
; at its left, a red close box at its right - over a dark field with a large
; green `A>` on it, the A seven pixels wide, the > five, rows 4-13. It is what
; the Disk window and the dock show for the package, and it is included by
; the shim through CC_ICON at file offset 32 (crt0.asm checks the offset and
; the length: 16 mask words - the white underlay - then 16 data words - the
; black pixels; bit 15 is the leftmost pixel).
;
; Derived from RunCPM (https://github.com/MockbaTheBorg/RunCPM), MIT License,
; Copyright (c) 2017 Mockba the Borg - the attribution every file in this
; package that carries RunCPM material wears (SPEC.md 71).
;
; It is a 1bpp reading of that image (SPEC.md 39.4 - no grey to round away):
; the dark field and the blue strip are black, the green glyph and the red
; close box are white. The ONE deviation is row 2, white where the .ico has
; it dark: on a 1bpp screen a black strip over a black field is no strip, so
; a one-pixel white gap separates the two the way the colour edge does in the
; original. The mask is all 16 rows full: the icon is a solid tile, so it sits
; on a clean white underlay on the desktop grey and on a selected row.
;
;   RunCPM.ico (b blue, r red, w white,   data (# black, . white)
;   . dark, o green)
;   wbbbbbbbbbbbbrrr                      .############...
;   wbbbbbbbbbbbbbbr                      .##############.
;   ................                      ................  <- the gap
;   ................                      ################
;   ...ooo..........                      ###...##########
;   ..ooooo..oo.....                      ##.....##..#####
;   .oo...oo..oo....                      #..###..##..####
;   .oo...oo...oo...                      #..###..###..###
;   .oo...oo....oo..                      #..###..####..##
;   .ooooooo.....oo.                      #.......#####..#
;   .oo...oo....oo..                      #..###..####..##
;   .oo...oo...oo...                      #..###..###..###
;   .oo...oo..oo....                      #..###..##..####
;   .oo...oo.oo.....                      #..###..#..#####
;   ................                      ################
;   ................                      ################
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
    dw 0x7FF8                       ; 16 data rows: the title strip, close box
    dw 0x7FFE
    dw 0x0000                       ; the gap (the deviation)
    dw 0xFFFF                       ; the field
    dw 0xE3FF                       ; A>  (.ico rows 4-13, green -> white)
    dw 0xC19F
    dw 0x9CCF
    dw 0x9CE7
    dw 0x9CF3
    dw 0x80F9
    dw 0x9CF3
    dw 0x9CE7
    dw 0x9CCF
    dw 0x9C9F
    dw 0xFFFF
    dw 0xFFFF
