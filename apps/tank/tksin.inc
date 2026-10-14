; sin(i) x 16384 for i = 0..64: ONE QUARTER of the wave, 256 units to the
; turn. tk_sin1 (tk3d.inc) reads the other three quarters out of it backwards
; and negated, which is exact - the whole table was symmetric to the unit.
    dw      0,    402,    804,   1205,   1606,   2006,   2404,   2801
    dw   3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897
    dw   6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765
    dw   9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297
    dw  11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395
    dw  13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978
    dw  15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986
    dw  16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379
    dw  16384
