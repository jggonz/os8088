; =============================================================================
; os8088 - apps/loom/lmassoc.inc
;
; LOOM's association block (SPEC.md 54.6, WEAVE-SPEC 1.5 step 2): "a document
; wearing one of these extensions is MINE". The BRACKET is crt0.asm's - the
; OS88_ASSOC16 / OS88_ASSOC16_END pair and the offset assertions live there,
; so this file writes only the count byte and the extensions, exactly as
; apps/weave/wvassoc.inc does.
;
; TWO EXTENSIONS, AND THEY ARE THE SOURCES AND NOT THE BUNDLE. WEAVE-SPEC 1.5
; step 2 says "Loom registers `.WML` and `.WJS` the same way, so
; double-clicking a source opens the IDE", and apps/weave/wvassoc.inc says the
; other half out loud: `.WAB` belongs to WEAVE.O88, which is a separate
; package. Two programs may not answer to one name (the WORD/CWORD precedent),
; and an IDE that claimed the bundle would open the editor on a double-click
; for the rest of the family's life.
;
; `.WFX` and `.WSP` are deliberately NOT here. The block holds sixteen bytes -
; four extensions at most - and a project's sheet and sprite sources are
; reached THROUGH the `.WML` beside them (WEAVE-SPEC 11.2), never opened on
; their own: LOOM has no notion of a project whose root is a `SHEET.WFX`.
; Claiming an extension it cannot open a project from would put an icon on a
; document that launches an editor with nothing to edit.
;
; Declaring costs nothing at run time: the mount's icon harvest already reads
; this sector, so a source sitting beside LOOM.O88 opens on the FIRST
; double-click - no prior run, no search (SPEC.md 54.6).
; =============================================================================
    db 2                            ; the count byte is the author's to emit;
                                    ; crt0.asm's macro only reserves the 16
    OS88_ASSOC_EXT 'WML'            ; the markup - a project's root (11.2)
    OS88_ASSOC_EXT 'WJS'            ; ...and its script
