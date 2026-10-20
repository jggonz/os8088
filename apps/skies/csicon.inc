; =============================================================================
; os8088 - apps/skies/csicon.inc
;
; CLEAR SKIES' 16x16 icon, in a file of its own because TWO packages carry it
; now (SPEC.md 88.10.4): the LOADER's header is what the Disk window draws
; before the launch, and the PROGRAM's is what the dock draws after the
; re-home - and the user must not be able to tell that there were two.
; =============================================================================

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) --------------------------
; A high-wing single seen from above: the Cessna's own silhouette.
;
;   ................
;   .......#........
;   ......###.......
;   ......###.......
;   .......#........
;   .......#........
;   ###############.
;   ###############.
;   .......#........
;   .......#........
;   .......#........
;   ......###.......
;   .....#####......
;   .......#........
;   ................
;   ................
    OS88_ICON16
    dw 0x0000                       ; 16 mask rows (white underlay)
    dw 0x0100
    dw 0x0380
    dw 0x0380
    dw 0x0100
    dw 0x0100
    dw 0xFFFE
    dw 0xFFFE
    dw 0x0100
    dw 0x0100
    dw 0x0100
    dw 0x0380
    dw 0x07C0
    dw 0x0100
    dw 0x0000
    dw 0x0000
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0100
    dw 0x0380
    dw 0x0380
    dw 0x0100
    dw 0x0100
    dw 0xFFFE
    dw 0xFFFE
    dw 0x0100
    dw 0x0100
    dw 0x0100
    dw 0x0380
    dw 0x07C0
    dw 0x0100
    dw 0x0000
    dw 0x0000
    OS88_ICON16_END
