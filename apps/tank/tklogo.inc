; TANK ATTACK as OUTLINED heavy-geometric caps (SPEC.md 85.10.1).
; ONE CLOSED LOOP a glyph, listed from its LOWER-LEFT vertex, on a
; 24x32 grid AT 1:1 - the glyph is its own pixels and nothing scales at
; run time. A counter (only A has one) is reached by a BRIDGE that is
; retraced, which is invisible because the same ink is laid twice.
; The third byte of each glyph's tk_lgg row is the segment its two gaps MEET
; at - the lower-right corner wherever the letter has one, and where the two
; halves come out closest to even where it does not (T and C). THIS FILE IS
; THE SOURCE, as tksin.inc and tkover.inc beside it are: the shapes were laid
; out against a renderer and then written down, and nothing regenerates them.
tk_lgpts:
    ; T - 8 points, meets at (24, 6)
    db   0,  6,   0,  0,  24,  0,  24,  6,  15,  6,  15, 32
    db   9, 32,   9,  6
    ; A - 13 points, meets at (24, 32)
    db   0, 32,   8,  0,  16,  0,  24, 32,  18, 32,  16, 26
    db   8, 26,   9, 20,  12,  8,  15, 20,   9, 20,   8, 26
    db   6, 32
    ; N - 10 points, meets at (24, 32)
    db   0, 32,   0,  0,   6,  0,  18, 22,  18,  0,  24,  0
    db  24, 32,  18, 32,   6, 10,   6, 32
    ; K - 11 points, meets at (24, 32)
    db   0, 32,   0,  0,   6,  0,   6, 10,  16,  0,  24,  0
    db  12, 16,  24, 32,  16, 32,   6, 22,   6, 32
    ; C - 18 points, meets at (24, 10)
    db   6, 32,   0, 26,   0,  6,   6,  0,  18,  0,  24,  6
    db  24, 10,  18, 10,  16,  6,   8,  6,   6,  8,   6, 24
    db   8, 26,  16, 26,  18, 22,  24, 22,  24, 26,  18, 32

; per glyph: first point, point count, and the segment the two gaps meet at
tk_lgg:
    db   0,   8,   3          ; T   halves 39 / 81 px
    db   8,  13,   3          ; A   halves 75 / 84 px
    db  21,  10,   6          ; N   halves 126 / 60 px
    db  31,  11,   7          ; K   halves 105 / 38 px
    db  42,  18,   6          ; C   halves 60 / 90 px

; the word, one byte a cell; 0FFh is a space
tk_lgword:
    db 0, 1, 2, 3, 0FFh, 1, 0, 0, 1, 4, 3

TK_LGCELLS equ 11               ; cells in the word, spaces included
TK_LGADV   equ 28               ; ...and the pixels between their origins
TK_LGW     equ TK_LGCELLS * TK_LGADV
TK_LGH     equ 32
