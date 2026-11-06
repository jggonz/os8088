; =============================================================================
; os8088 - apps/pagedesk/pduglyph.inc   the 8x8 a .PDF wears inside its page
;                                       (SPEC.md 54.3.2) - PLACEHOLDER until
;                                       the fonts agent's art lands
;
; Three lines of text and a highlight bar: shipped rather than reduced out of
; the icon, whose outline the 2x2 majority reduction would empty.
; =============================================================================
    db 0x00, 0x7C, 0x00, 0x7E, 0x00, 0x7E, 0x7E, 0x00
