; =============================================================================
; os8088 - apps/pagedesk/icon.inc     PageDesk's 16x16 icon (PLACEHOLDER)
;
; A PLACEHOLDER until the fonts agent's art (PAGEDESK-SPEC 17.6, built by
; tools/os88pdart.py into apps/pagedesk/art/) lands and the lead points
; CC_ICON at it. A page with a folded corner and four lines of text, the last
; one a highlight bar - an outline at ~35% ink so it survives being reduced to
; the document mark (apps/loom/icon.inc's lesson).
;
;   ..##########....
;   ..#........##...
;   ..#........#.#..
;   ..#........####.
;   ..#.#####.....#.
;   ..#...........#.
;   ..#.#######...#.
;   ..#...........#.
;   ..#.#######...#.
;   ..#...........#.
;   ..#.####......#.
;   ..#...........#.
;   ..#..#######..#.
;   ..#...........#.
;   ..#############.
;   ................
;
; 16 mask words (a full white underlay), then 16 data words, bit 15 leftmost.
; =============================================================================
    times 16 dw 0xFFFF
    dw 0x3FF0, 0x2018, 0x2014, 0x201E
    dw 0x2F82, 0x2002, 0x2FE2, 0x2002
    dw 0x2FE2, 0x2002, 0x2F02, 0x2002
    dw 0x27F2, 0x2002, 0x3FFE, 0x0000
