; =============================================================================
; os8088 - apps/weave/wnum.inc
;
; WEAVE-SPEC 5.1's decimal-to-16.16 conversion, and nothing else.
;
; IT IS A FILE OF ITS OWN BECAUSE TWO PACKAGES NEED IT. apps/weave/wfx.inc
; %includes it where the routine used to stand (the extraction is byte for
; byte the image that was there), and apps/loom/loom.asm %includes it because
; LOOM's FX pre-compiler is apps/weave/wfxc.c itself - WEAVE-SPEC 1.2's rule
; that what the two share they share as SOURCE, never as a second copy, said
; about ten instructions instead of about a grammar.
;
; Section discipline: it opens nothing and leaves the assembler in whatever
; section its includer was in, which for both callers is .text.
; =============================================================================

; unsigned wfx_frac(unsigned num, unsigned den) - (num*65536 + den/2) / den,
; which is WEAVE-SPEC 5.1's decimal-to-16.16 conversion, rounded half up. The
; quotient fits a word because num < den always (5.1 bounds the fraction at
; four digits, so den <= 10000 and num <= 9999).
_wfx_frac:
    push bp
    mov bp, sp
    mov dx, [bp+4]                  ; num * 65536
    mov ax, [bp+6]
    shr ax, 1                       ; + den/2
    div word [bp+6]
    pop bp
    ret
