; =============================================================================
; os8088 - apps/skies/cswone.asm
;
; ONE WORLD, ASSEMBLED ON ITS OWN (SPEC.md 88.10.5) - the wrapper tools/
; csworlds.py builds each of Clear Skies' eight world files through.
;
; It lays the shared vocabulary at CS_WLD_ORG and the world immediately after
; it, at CS_WLD_ORG + CS_VOCAB_MAX, which is exactly where the program's
; overlay puts them. So every near pointer inside a world - into its own
; models, and into the eleven vocabulary symbols it names - is already right
; when the bytes are copied in, and cs_scene goes on reading the world with DS
; and no override on the hottest path in the program.
;
; THE VOCABULARY'S BYTES ARE ASSEMBLED AND THEN THROWN AWAY, once per world:
; they are here to give the world's references somewhere to point, and
; csworlds.py keeps only what follows csw_body. The vocabulary is packed once,
; on its own, by the same script.
;
; CS_WLD_ORG, CS_VOCAB_MAX and CSW_FILE all come from the command line, so this
; file names no world and hard-codes no address.
; =============================================================================
    cpu 8086
    bits 16
    org CS_WLD_ORG

%include "cswdefs.inc"          ; the constants a world needs (t_mirror holds
                                ; them to skies.asm)
%include "cswmac.inc"           ; the macros, which emit nothing...
%include "csvocab.inc"          ; ...and the tables a world points into

%if ($ - $$) > CS_VOCAB_MAX
  %error "csvocab.inc has outgrown CS_VOCAB_MAX - raise it in \
tools/csworlds.py, which is the only place the overlay's four numbers are \
declared: skies.asm reads them out of the cswidx.inc it generates"
%endif
    times CS_VOCAB_MAX - ($ - $$) db 0

csw_body:                       ; ...and the world itself, which is all
%include CSW_FILE               ; csworlds.py keeps
csw_end:
