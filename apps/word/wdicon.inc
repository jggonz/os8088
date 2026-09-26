; =============================================================================
; os8088 - apps/word/wdicon.inc
;
; WORD's icon and its association block - read by BOTH roots of WORD.O88
; (SPEC.md 68.10): apps/word/wdload.asm, the loader the kernel launches and
; whose header the mount's icon harvest reads, and apps/word/word.asm, the
; program it hands its identity to, whose header the dock reads after. One
; include, so the Disk window and the dock cannot draw two different icons
; and the .DOC association cannot drift between them. apps/skies/csicon.inc
; and apps/dos/dosicon.inc are the same arrangement.
; =============================================================================

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A white page with a big black 'W' - the document identity, one glyph. The
; mask is the page silhouette dilated 1px, so it sits on a clean white
; underlay over the desktop grey and over a selected row.
;
;   data               mask
;   ................   .#############..
;   ..###########...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#.#.....#.#...   .#############..
;   ..#.#.....#.#...   .#############..
;   ..#.#.....#.#...   .#############..
;   ..#.#..#..#.#...   .#############..
;   ..#.#..#..#.#...   .#############..
;   ..#.#.#.#.#.#...   .#############..
;   ..#.##...##.#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..#.........#...   .#############..
;   ..###########...   .#############..
;   ................   .#############..
    OS88_ICON16
    dw 0x7FFC                       ; 16 mask rows
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x7FFC
    dw 0x0000                       ; 16 data rows
    dw 0x3FF8
    dw 0x2008
    dw 0x2008
    dw 0x2828
    dw 0x2828
    dw 0x2828
    dw 0x2928
    dw 0x2928
    dw 0x2AA8
    dw 0x2C68
    dw 0x2008
    dw 0x2008
    dw 0x2008
    dw 0x3FF8
    dw 0x0000
    OS88_ICON16_END

; --- the association block (SPEC.md 54/68.4): double-clicking a .DOC on the
; desktop launches Word with the document in OSAPI_ARG_FILE. Declared in the
; header (flags bit 1) so the mount's icon harvest reads it for free.
    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'DOC'
    OS88_ASSOC16_END
