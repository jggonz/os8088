; PiXEL's application icon (SPEC.md 20.5, 106.1): sixteen MASK rows - the
; frame's silhouette, so nothing of the desktop shows inside it - then
; sixteen DATA rows. Included between OS88_ICON16 and OS88_ICON16_END.
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
    dw 0xFFFF
    dw 0xFFFF    ; ################
    dw 0x8001    ; #..............#
    dw 0xBFFD    ; #.############.#
    dw 0xA005    ; #.#..........#.#
    dw 0xA035    ; #.#.......##.#.#
    dw 0xA079    ; #.#......####..#
    dw 0xA035    ; #.#.......##.#.#
    dw 0xA005    ; #.#..........#.#
    dw 0xA205    ; #.#...#......#.#
    dw 0xA715    ; #.#..###...#.#.#
    dw 0xAFBD    ; #.#.#####.####.#
    dw 0xBFFD    ; #.############.#
    dw 0x8001    ; #..............#
    dw 0x8001    ; #..............#
    dw 0x8001    ; #..............#
    dw 0xFFFF    ; ################

; ...and its DOCUMENT GLYPH (SPEC.md 54.3.2): the 8x8 a picture of ours wears
; inside its page. Included between OS88_DOCGLYPH8 and OS88_DOCGLYPH8_END.
%macro PXI_DOCGLYPH 0
    db 0xFF    ; ########
    db 0x81    ; #......#
    db 0x85    ; #....#.#
    db 0x81    ; #......#
    db 0x91    ; #..#...#
    db 0xBB    ; #.###.##
    db 0xFF    ; ########
    db 0x00    ; ........
%endmacro
