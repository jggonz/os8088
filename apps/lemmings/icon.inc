; =============================================================================
; os8088 - apps/lemmings/icon.inc
;
; LEMMINGS's embedded 16x16 icon (SPEC.md 20.2, 116.9): a walking lemming -
; the hair, the face, the robe and the stride - DRAWN FOR THIS PORT, as a 1bpp
; silhouette, and not copied: nothing of MAIN.DAT's lemming sprites (which are
; fetched at build time and never committed, SPEC.md 116.2) and nothing of any
; reference tree's icon is transcribed into it. Grey rounds to black on two
; adapters of three (SPEC.md 39.4), so there is nothing here to round.
;
; THE PICTURE IN THE COMMENT IS THE SOURCE (wave 6). Each data row carries its
; own sixteen pixels beside it, '#' a set bit (drawn black), '.' clear, bit 15
; the leftmost; apps/lemmings/build.sh re-derives every dw from its picture
; and stops the build when one disagrees, so the bytes cannot drift from the
; drawing a reader sees.
;
; Part of LEMMINGS, a reimplementation of Lemmings (DMA Design / Psygnosis,
; 1991) for os8088 (SPEC.md 116). This file carries no derived material.
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words. The mask is full
; because the icon is a solid tile: it sits on a clean underlay on the desktop
; grey and on a selected row of the Disk window alike.
; =============================================================================
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
    dw 0x0000                       ; 16 data rows   ................
    dw 0x0700                       ;                .....###........
    dw 0x0FC0                       ;                ....######......
    dw 0x1FE0                       ;                ...########.....
    dw 0x1F20                       ;                ...#####..#.....
    dw 0x0E10                       ;                ....###....#....
    dw 0x0430                       ;                .....#....##....
    dw 0x03C0                       ;                ......####......
    dw 0x07E0                       ;                .....######.....
    dw 0x0DE0                       ;                ....##.####.....
    dw 0x19E0                       ;                ...##..####.....
    dw 0x01E0                       ;                .......####.....
    dw 0x0320                       ;                ......##..#.....
    dw 0x0610                       ;                .....##....#....
    dw 0x0E18                       ;                ....###....##...
    dw 0x0000                       ;                ................
