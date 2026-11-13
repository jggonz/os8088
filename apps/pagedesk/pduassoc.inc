; =============================================================================
; os8088 - apps/pagedesk/pduassoc.inc   PageDesk's declared extension
;                                       (SPEC.md 54.6, PAGEDESK-SPEC 20.6)
;
; A .PDF opens PageDesk on the FIRST double-click: the mount's harvest reads
; this block out of PAGEDESK.O88's header, no prior run needed. os88_main
; also claims it at run time (os88_assoc_set), which makes it sticky.
; crt0.asm writes the bracket; the count byte and the rows are ours.
; =============================================================================
    db 1
    OS88_ASSOC_EXT 'PDF'
