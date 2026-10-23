; =============================================================================
; os8088 - apps/scribe/scicon.inc
;
; SCRIBE's icon - read by BOTH roots of SCRIBE.O88 (SPEC.md 95.8.8):
; apps/scribe/scload.asm, the loader the kernel launches and whose header the
; mount's icon harvest reads, and apps/scribe/scribe.asm, the program it hands
; its identity to, whose header the dock reads after. One include, so the Disk
; window and the dock cannot draw two different icons. apps/word/wdicon.inc is
; the same arrangement one package along - except that it carries Word's .DOC
; association block too, and SCRIBE declares none (SPEC.md 95.2).
; =============================================================================

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A white page with a big black 'S' - the document identity, one glyph. The
; mask is the page silhouette dilated 1px, so it sits on a clean white
; underlay over the desktop grey and over a selected row.
;
;   data               mask
;   ................   .#############..
;   ..###########...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.#...#...#...   .#############..
;   ..#.#.......#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.....#...#...   .#############..
;   ..#.#...#...#...   .#############..
;   ..#..###....#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..###########...   .#############..
;   ................   .#############..
;
; The same page silhouette Word's icon has - these are two word processors and
; the outline should say so - with an S where its W is. The MASK is unchanged:
; it is the page dilated 1px and does not depend on the glyph inside.
    OS88_ICON16
    dw 0x7FFC                       ; 16 mask rows
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x0000                       ; 16 data rows
    dw 0x3FF8                       ; the page's top edge
    dw 0x2008
    dw 0x2008
    dw 0x2708                       ; .###.  the S begins
    dw 0x2888                       ; #...#
    dw 0x2808                       ; #....
    dw 0x2708                       ; .###.
    dw 0x2088                       ; ....#
    dw 0x2888                       ; #...#
    dw 0x2708                       ; .###.
    dw 0x2008
    dw 0x2008
    dw 0x2008
    dw 0x3FF8                       ; ...and its bottom
    dw 0x0000
    OS88_ICON16_END
