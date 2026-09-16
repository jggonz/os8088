; =============================================================================
; os8088 - apps/pixelstein/pxicon.inc
;
; PIXELSTEIN 3D's 16x16 icon (SPEC.md 20.2): a corridor seen head-on - the
; frame, the far wall and the four edges running to it. ONE FILE, TWO
; IMAGES: the loader (pxstein.asm) carries it because the Disk window draws
; the loader's icon before the launch, and the program (pxgame.asm) carries
; it because the dock draws the program's after it (apps/skies/csicon.inc's
; reason). 16 mask rows then 16 data rows, bit 15 the leftmost pixel.
; =============================================================================
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF   ; the mask: a solid square
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 1111111111111111b                ; the data
    dw 1000000000000001b
    dw 1010000000000101b
    dw 1001000000001001b
    dw 1000100000010001b
    dw 1000010000100001b
    dw 1000001111000001b
    dw 1000001001000001b
    dw 1000001001000001b
    dw 1000001111000001b
    dw 1000010000100001b
    dw 1000100000010001b
    dw 1001000000001001b
    dw 1010000000000101b
    dw 1000000000000001b
    dw 1111111111111111b
