; =============================================================================
; os8088 - apps/weave/icon.inc
;
; WEAVE's embedded 16x16 icon (SPEC.md 20.2, WEAVE-SPEC 1.2): four ribbons -
; two across, two down - drawn as OUTLINES and passing alternately over and
; under one another. It is a 1bpp silhouette because two of the three adapters
; are 1bpp and grey rounds to black there (SPEC.md 39.4), so there is nothing
; to round: every pixel is ink or paper and the tile reads the same on all
; three.
;
; The ribbons are the whole idea of the program - a display list of bands laid
; out by a flow walk (WEAVE-SPEC 7) - and the over/under breaks are what keep
; it from reading as a plain grid at this size.
;
; OUTLINES RATHER THAN SOLID STRIPS, and that is what the first version got
; wrong: a solid basket weave is about three quarters ink, so on the glass it
; is a black tile with notches, and the shrunken mark the association harvest
; puts on every .WAB document (SPEC.md 54.6) reduces to a solid square. An
; icon has to survive being made smaller.
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words, bit 15 the leftmost
; pixel, a SET data bit drawn black. The mask is full because the icon is a
; solid tile: it sits on a clean underlay on the desktop grey and on a
; selected row of the Disk window alike.
;
;   ..#...#..#...#..
;   ..#...#..#...#..
;   ##########...###
;   .........#...#..
;   .........#...#..
;   .........#...#..
;   ##########...###
;   ..#...#..#...#..
;   ..#...#..#...#..
;   ###...##########
;   ..#...#.........
;   ..#...#.........
;   ..#...#.........
;   ###...##########
;   ..#...#..#...#..
;   ..#...#..#...#..
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
    dw 0x2244                       ; 16 data rows
    dw 0x2244
    dw 0xFFC7
    dw 0x0044
    dw 0x0044
    dw 0x0044
    dw 0xFFC7
    dw 0x2244
    dw 0x2244
    dw 0xE3FF
    dw 0x2200
    dw 0x2200
    dw 0x2200
    dw 0xE3FF
    dw 0x2244
    dw 0x2244
