; =============================================================================
; os8088 - apps/loom/icon.inc
;
; LOOM's embedded 16x16 icon (SPEC.md 20.2, WEAVE-SPEC 1.2): a loom - two
; beams top and bottom, two uprights, the warp threads strung between them,
; and the SHUTTLE crossing the middle, drawn as an outline lens.
;
; IT IS A 1BPP SILHOUETTE because two of the three adapters are 1bpp and grey
; rounds to black there (SPEC.md 39.4), so there is nothing to round: every
; pixel is ink or paper and the tile reads the same on all three.
;
; OUTLINES, AND 41% INK. apps/weave/icon.inc paid for this lesson once: a
; solid basket weave is about three quarters ink, so on the glass it is a black
; tile with notches and the shrunken mark the association harvest puts on every
; document (SPEC.md 54.6) reduces to a solid square. An icon has to survive
; being made smaller, so the shuttle is a two-pixel outline rather than a
; filled boat, and the warp is every third column rather than every other.
;
; DISTINCT FROM WEAVE'S AT A GLANCE, which is the other requirement: WEAVE is
; four ribbons over and under one another, edge to edge and unframed; this is
; a FRAMED grid of verticals with one horizontal lens in the middle. At 16x16
; the two read as "a plaid" and "a box with an eye in it", and at half that
; they still do.
;
;   ################
;   #..............#
;   #.#..#..#..#...#
;   #.#..#..#..#...#
;   #.#..#..#..#...#
;   #..............#
;   #....######....#
;   #..##......##..#
;   #.#..........#.#
;   #..##......##..#
;   #....######....#
;   #..............#
;   #.#..#..#..#...#
;   #.#..#..#..#...#
;   #.#..#..#..#...#
;   ################
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words, bit 15 the leftmost
; pixel, a SET data bit drawn black. The mask is full because the icon is a
; solid tile: it sits on a clean underlay on the desktop grey and on a
; selected row of the Disk window alike.
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
    dw 0xFFFF                       ; 16 data rows - the top beam
    dw 0x8001
    dw 0xA491                       ; the warp, every third column
    dw 0xA491
    dw 0xA491
    dw 0x8001
    dw 0x87E1                       ; the shuttle: top edge
    dw 0x9819                       ; ...its sides
    dw 0xA005                       ; ...and its two points
    dw 0x9819
    dw 0x87E1
    dw 0x8001
    dw 0xA491
    dw 0xA491
    dw 0xA491
    dw 0xFFFF                       ; ...and the bottom beam
