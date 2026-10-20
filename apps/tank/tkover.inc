; GAME OVER as strokes, pre-composed into ONE list: character n owns
; x = 8n .. 8n+6 of a 0..71 by 0..10 grid, so there is no per-character
; machinery at all - the walk is a table and a scale (SPEC.md 85.8).
;
; The list is the bss's tk_goseg (x1, y1, x2, y2 a segment, TK_GONSEG of
; them, counted by tank.asm), and this file is its STROKES: each TKGO is a
; run of points joined end to end, and tk_mktabs (tank.asm) writes one
; segment for every pair of neighbours, in this order - which is the order
; tk_gooff numbers them by, so every segment keeps its own flight (apps size
; pass 2).
    TKGO  6, 1,  1, 1,  0, 2,  0, 8,  1, 9,  5, 9,  6, 8,  6, 5,  3, 5
    TKGO  8,10, 11, 0, 14,10
    TKGO  9, 7, 13, 7
    TKGO 16,10, 16, 0, 19, 6, 22, 0, 22,10
    TKGO 30, 0, 24, 0, 24,10, 30,10
    TKGO 24, 5, 28, 5
    TKGO 41, 0, 45, 0, 46, 2, 46, 8, 45,10, 41,10, 40, 8, 40, 2, 41, 0
    TKGO 48, 0, 51,10, 54, 0
    TKGO 62, 0, 56, 0, 56,10, 62,10
    TKGO 56, 5, 60, 5
    TKGO 64,10, 64, 0, 69, 0, 70, 1, 70, 4, 69, 5, 64, 5
    TKGO 66, 5, 70,10
