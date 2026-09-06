; =============================================================================
; os8088 - apps/paccman/icon.inc
;
; PACCMAN's embedded 16x16 icon (SPEC.md 20.2, 90): a Pac-Man silhouette DRAWN
; FOR THIS PORT, not copied. The reference has no icon at all - pacman.c 727
; asks sokol for `.icon.sokol_default` - and the arcade sprite it evokes
; (sprite 44/48) is a 16x16 four-colour cel that would be a colour photograph
; here; this is a plain 1bpp disc with a wedge, computed from a circle of
; radius 7.6 about (7.5, 7.5) with a 33-degree mouth. Nothing of the ROM data
; is transcribed into it.
;
; Part of PACCMAN, a reimplementation of Andre Weissflog's pacman.c
; (github.com/floooh/pacman.c, MIT, (c) 2020 Andre Weissflog) at commit
; 0f5ec5a. See apps/paccman/LICENSE and apps/paccman/README.md.
;
; crt0.asm puts it at file offset 32 and checks the offset and the length:
; 16 mask words - the white underlay - then 16 data words, bit 15 the leftmost
; pixel, a SET data bit drawn black. The mask is full because the icon is a
; solid tile: it sits on a clean underlay on the desktop grey and on a
; selected row of the Disk window alike.
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
    dw 0x0180                       ; 16 data rows   .......##.......
    dw 0x0FF0                       ;                ....########....
    dw 0x1FF8                       ;                ...##########...
    dw 0x3FFC                       ;                ..############..
    dw 0x7FF8                       ;                .############...
    dw 0x7FF0                       ;                .###########....
    dw 0x7FC0                       ;                .#########......
    dw 0xFF80                       ;                #########.......
    dw 0xFF80                       ;                #########.......
    dw 0x7FC0                       ;                .#########......
    dw 0x7FF0                       ;                .###########....
    dw 0x7FF8                       ;                .############...
    dw 0x3FFC                       ;                ..############..
    dw 0x1FF8                       ;                ...##########...
    dw 0x0FF0                       ;                ....########....
    dw 0x0180                       ;                .......##.......
