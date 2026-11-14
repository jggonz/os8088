; The renderer's glyph and attribute tables, and the attribute indices
; (A_*) wrender.inc draws with.

; a wall edge's glyph by half-row: upper half the middle line, lower the
; bottom one
wa_hgl:     times WA_HR/2 db 0xC4, '_'
; the figure layer's glyph for each 2x2 occupancy mask: bit 0 top-left,
; 1 top-right, 2 bottom-left, 3 bottom-right
wa_fglyph:  db 0x20, 0xDF, 0xDF, 0xDF, 0xDC, 0xDD, 0xDB, 0xDB
            db 0xDC, 0xDB, 0xDE, 0xDB, 0xDC, 0xDB, 0xDB, 0xDB
;              near   mid    far    figure flash  cross
; COLOUR SAYS WHAT A THING IS, not how far (the owner's call: two shades of
; blue never read as depth, and Hercules has no colour at all): a boundary
; wall dark blue, an item - a pillar, a block, a stair to come - cyan, the
; floor grid dark grey, the figures white. On mono every wall and dot is
; normal intensity and the figures alone are bright.
;              room   item   dot    figure flash  cross
; ...and a shielded figure's chest: yellow, or normal intensity against the
; body's bright on mono (WEAPONS-PLAN 2.3)
;              room   item   dot    figure flash  cross  shield (pad)
; ...and a bot firing: yellow, or normal intensity on mono (AI-PLAN 4.3)
;              room   item   dot    figure flash  cross  shield fire
; ...and the railgun's kill, struck and dissolving: its beam's bright cyan,
; or bright on mono (the owner: "the same color" as the shot)
;              room   item   dot    figure flash  cross  shield fire   rail
wa_attrc:   db 0x01,  0x03,  0x08,  0x0F,  0x0C,  0x0E,  0x0E,  0x0E,  0x0B, 0
wa_attrm:   db 0x07,  0x07,  0x07,  0x0F,  0x70,  0x0F,  0x07,  0x07,  0x0F, 0
A_ROOM  equ 0
A_ITEM  equ 1
A_DOT   equ 2
A_FIG   equ 3
A_FLASH equ 4
A_CROSS equ 5
A_SHLD  equ 6
A_FIRE  equ 7
A_RAIL  equ 8
