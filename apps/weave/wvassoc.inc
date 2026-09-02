; =============================================================================
; os8088 - apps/weave/wvassoc.inc
;
; WEAVE's association block (SPEC.md 54.6, WEAVE-SPEC 1.5): "a document
; wearing one of these extensions is MINE". The BRACKET is crt0.asm's - the
; OS88_ASSOC16 / OS88_ASSOC16_END pair and the offset assertions live there,
; so this file writes only the count byte and the extensions, exactly as
; apps/frotz/frotz.asm's block does.
;
; ONE EXTENSION, AND THAT IS THE WHOLE OF THE FAMILY'S CLAIM HERE. `.WML` and
; `.WJS` belong to LOOM.O88 (WEAVE-SPEC 1.5 step 2), which is a separate
; package: two programs may not answer to one name, and a runtime that
; claimed the sources would open the wrong program on a double-click for the
; rest of the family's life.
;
; Declaring costs nothing at run time: the mount's icon harvest already reads
; this sector, so a bundle sitting beside WEAVE.O88 opens on the FIRST
; double-click - no prior run, no search (SPEC.md 54.6).
; =============================================================================
    db 1                            ; the count byte is the author's to emit;
                                    ; crt0.asm's macro only reserves the 16
    OS88_ASSOC_EXT 'WAB'            ; the bundle (WEAVE-SPEC 2)
