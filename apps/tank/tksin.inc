; sin(i) x 16384 for i = 0..64: ONE QUARTER of the wave, 256 units to the
; turn. tk_sin1 (tk3d.inc) reads the other three quarters out of it backwards
; and negated, which is exact - the whole table was symmetric to the unit.
;
; It ships as its SECOND DIFFERENCES (SPEC.md 85.5): sin 0 is 0, the first
; step is TK_SIN_D1, and each TKSIN number is how much the next step is
; SHORTER than the one before - 0..11 on a quarter wave, so two to a byte.
; tk_mktabs (tank.asm) adds them back up into the bss's tk_sintab, which is
; the 65 words this file used to hold, to the unit. The last number is a pad.
TK_SIN_D1 equ 402
    TKSIN  0,  1,  0,  1,  2,  1,  2,  1,  3,  2,  3,  3,  2,  4,  4,  4
    TKSIN  3,  5,  4,  4,  6,  5,  5,  5,  6,  6,  6,  6,  7,  7,  6,  7
    TKSIN  7,  8,  7,  8,  7,  9,  7,  9,  8,  8, 10,  7, 10,  9,  8, 10
    TKSIN  9,  9,  9, 10, 10,  9, 10,  9, 10, 10, 10,  9, 11,  9, 10,  0
