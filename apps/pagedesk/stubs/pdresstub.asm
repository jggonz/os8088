; =============================================================================
; os8088 - apps/pagedesk/stubs/pdresstub.asm    STAND-IN PDRES.BIN (PAGEDESK-SPEC
;                                                2.2, 11.2)
;
; The asset part the fonts agent builds with tools/os88pdfont.py. Until it
; lands, the package carries this: a 512-byte block whose first eight bytes
; say STUB, so the primary's lazy read-by-offset path (pdui.inc's
; pdu_read_at_seg) has a real part to read and PDREND's FONTBANK verb a real
; segment to be handed. Nothing reads its contents.
; =============================================================================
    db 'PDRSTUB', 0
    times 512 - ($ - $$) db 0
