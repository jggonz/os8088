; =============================================================================
; os8088 - apps/infones/niassoc.inc
;
; INFONES's association block (SPEC.md 54.6, 91.14.1): "a document wearing one
; of these extensions is MINE". The BRACKET is crt0.asm's - the OS88_ASSOC16 /
; OS88_ASSOC16_END pair and the offset assertions live there, so this file
; writes only the count byte and the extensions, exactly as apps/frotz's block
; does.
;
; ONE EXTENSION. A NES ROM is an iNES file and its extension is `.NES`;
; nothing else here belongs to this program.
;
; Declaring costs nothing at run time - the mount's icon harvest already reads
; this sector - so a ROM sitting beside INFONES.O88 opens on the FIRST
; double-click, with no prior run and no search. os88_assoc_set() (SPEC.md
; 54.5) is the RUNTIME half and infones.c calls it as well: the two are not
; alternatives, and .claude/skills/port-to-os8088/LESSONS.md 9's line saying
; the C SDK has no build-time association block is stale - crt0.asm:292-321
; implements CC_ASSOC.
;
; Part of INFONES (SPEC.md 91), derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. THIS FILE carries no InfoNES
; material.
; =============================================================================
    db 1                            ; the count byte is the author's to emit;
                                    ; crt0.asm's macro only reserves the 16
    OS88_ASSOC_EXT 'NES'            ; an iNES image (SPEC.md 91.2's header row)
