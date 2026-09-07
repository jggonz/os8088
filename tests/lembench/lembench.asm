; =============================================================================
; os8088 - tests/lembench/lembench.asm
;
; The assembly shim of LEMBENCH, the LEMMINGS raster's BENCH (SPEC.md 92.7).
;
; IT %includes THE SHIPPING RASTER, IT DOES NOT CARRY A COPY OF IT. The three
; .inc files below are apps/lemmings' own, included from their own directory,
; so what this bench times is the code that ships and a change to either half
; cannot leave them measuring different things (WEAVE-SPEC 1.2's rule for the
; Weave family, and the same reason). That is also why this is a package rather
; than a hand-written loop: the numbers have to come off the real routines,
; through the real cdecl boundary, with the real segment arithmetic.
;
; NOTHING UNDER tests/ SHIPS (CLAUDE.md): `make lembench` builds it on demand and
; no floppy `all` writes carries it.
; =============================================================================

%define CC_PKG_NAME 'LEMBENCH'
%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_FSX                  ; the bench runs INSIDE the bracket, because
                                    ; half of what it times is VRAM and port I/O

%include "cc/crt0.asm"
%include "lembench.gen.asm"          ; the compiled C, found through -I build/

; The shipping raster, in the order lemmings.asm includes it: lemblit.inc reads
; lemmask.inc's lm_seg and lem_rowoff, and lemfont.inc reads lemblit.inc's
; lb_rpix.
%include "lemmings/lemmask.inc"
%include "lemmings/lemblit.inc"
%include "lemmings/lemfont.inc"

    CC_IMAGE_END
