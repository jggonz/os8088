; THE RIDGE'S PROFILE (SPEC.md 85.5.1): the horizon silhouette, one height
; per 1/256 turn, as the STRAIGHT RUNS it is made of. TKRDG0 is the height at
; angle 0; each TKRDG n, h after it is a run of n steps ending at height h, and
; step k of a run from h0 is (h0 (n - k) + h k + n / 2) / n - the rounding the
; old 256-byte table was written to, so tk_mktabs (tank.asm) expands these
; 49 runs back to it TO THE BYTE. The runs add to 255: the last height is
; angle 255's, and angle 0 is where the profile closes on itself.
    TKRDG0 3
    TKRDG  4,   3, 10,  61,  6,  26,  1,  30,  2,  49,  5,  94,  2, 113,  2, 131
    TKRDG  7,  67,  1,  63,  1,  77,  4, 137,  9,   3,  7,   3, 23,  87, 12,  43
    TKRDG  3,  34,  2,  29,  5,  16,  1,  13,  1,  11,  1,  12,  4,  22, 12,  50
    TKRDG  2,  55,  3,  62, 11,  36,  2,  31,  1,  30, 12,  95,  5, 122,  5,  95
    TKRDG  3,  79,  1,  79,  7, 121,  1, 126,  4, 150, 17,  50,  1,  54,  4,  80
    TKRDG  7, 125,  5,  93,  1,  98,  1,  89,  1,  81,  1,  78,  4, 112, 13,   3
    TKRDG 18,   3
