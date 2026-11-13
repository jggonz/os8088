; os8088 - apps/pixel/pxbayer.inc - the 8x8 ordered-dither table
; (SPEC.md 106.11, pixelsim's BAYER8), shared with the Browser (71.26)
px_bayer:
    db  0, 32,  8, 40,  2, 34, 10, 42
    db 48, 16, 56, 24, 50, 18, 58, 26
    db 12, 44,  4, 36, 14, 46,  6, 38
    db 60, 28, 52, 20, 62, 30, 54, 22
    db  3, 35, 11, 43,  1, 33,  9, 41
    db 51, 19, 59, 27, 49, 17, 57, 25
    db 15, 47,  7, 39, 13, 45,  5, 37
    db 63, 31, 55, 23, 61, 29, 53, 21
