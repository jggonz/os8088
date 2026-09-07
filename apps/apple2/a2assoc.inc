; =============================================================================
; os8088 - apps/apple2/a2assoc.inc
;
; APPLE2's association block (SPEC.md 54.6, APPLE2-SPEC section 12): "a
; document wearing this extension is MINE". The BRACKET is crt0.asm's - the
; OS88_ASSOC16 / OS88_ASSOC16_END pair and the offset assertions live there,
; so this file writes only the count byte and the extensions, exactly as
; apps/weave/wvassoc.inc and apps/frotz/frotz.asm do.
;
; ----------------------------------------------------------------------------
; THIS FILE IS NOT %included YET, AND THAT IS THE DECISION AND NOT AN OVERSIGHT
; ----------------------------------------------------------------------------
; `%define CC_ASSOC "apple2/a2assoc.inc"` goes into apps/apple2/apple2.asm in
; the wave that makes File > Load Program... work, and not before.
; APPLE2-SPEC section 12 states the reason: declaring an extension the build
; cannot open means a double-click LAUNCHES THE EMULATOR AND THEN REFUSES,
; which is worse than no association at all - the user has spent a floppy
; seek, a 64KB claim and a window to be told no.
;
; The file exists from wave 1 because it is a WRITTEN PREREQUISITE in the
; Makefile (APPLE2SRC/APPLE2INC): make cannot see through a %include, and a
; file a later wave adds is a build the tree does not know about
; (LESSONS.md 9).
;
; `DSK`, `DO` and `PO` are declared only when the Disk II follow-up PR lands,
; for the same reason one level along.
;
; Declaring costs nothing at run time: the mount's icon harvest already reads
; this sector, so a `.BAS` listing sitting beside APPLE2.O88 opens on the
; FIRST double-click - no prior run, no search - which is what the SDK's
; runtime os88_assoc_set() cannot do (SPEC.md 54.5, LESSONS.md 9).
; =============================================================================
    db 1                            ; the count byte is the author's to emit;
                                    ; crt0.asm's macro only reserves the 16
    OS88_ASSOC_EXT 'BAS'            ; a tokenised Applesoft program
                                    ; (APPLE2-SPEC section 12)
