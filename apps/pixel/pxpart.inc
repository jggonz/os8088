; =============================================================================
; os8088 - apps/pixel/pxpart.inc
;
; PiXEL's PART ABI (SPEC.md 106.5): the header every far-called code part
; starts with, and the numbers both sides of the boundary agree on. It is
; included by the RESIDENT package (apps/pixel/pixel.asm) and by every part
; (apps/pixel/pxhelp.asm today, the decoders later), so the two cannot hold
; different opinions about where a vector is.
;
; A PART IS AN OP_SEG ROW (SPEC.md 20.12): a flat binary assembled at org 0,
; carried inside PIXEL.O88, read into a claim of its own by op_fetch when it
; is first wanted and given back by op_drop. Its CODE runs with CS = that
; claim, so it is reached by a FAR call and it answers with `retf`. Nothing
; else about it is special, and the rules below are SPEC.md 95.8's module
; rules (SCRIBE.OVL) carried over to a part:
;
;   1. ITS OWN TABLES THROUGH CS. A part's strings and constants are in the
;      part, so they are read `[cs:...]`. `push cs / pop ds` is correct ONLY
;      for the part's own data and never to get back to the package's.
;   2. THE PACKAGE THROUGH DS, OR THROUGH PXP_PKG. A vector is entered with
;      DS = the package's segment, exactly as the caller had it, so package
;      bss is `[name]` until the part repoints DS. A part that does repoint it
;      gets the package back from `[cs:PXP_PKG]`, which the resident stamps
;      before EVERY call - so a region that moved between two calls (SPEC.md
;      66) is never named stale. A part must not keep that segment anywhere
;      that outlives the call.
;   3. IT NEVER SPEAKS. No toast, no drawing, no file call: a part answers a
;      NUMBER (PXE_*) and the resident caller words it. Those are SPEC.md
;      20.12.5's ES-fenced slots and 20.6 rule 7's file slots, and a part
;      that runs on PiXEL's worker (later waves) could reach neither anyway.
;   4. IT RETURNS CF. CF = 0 success with the verb's own outputs; CF = 1 and
;      AX = a PXE_* number.
;
; The resident's half - fetch, validate, stamp, far call, drop - is px_pcall
; and px_pdrop in apps/pixel/pixel.asm, and tests/pxparts.py is its gate.
; =============================================================================

%ifndef PXPART_INC
%define PXPART_INC

; --- the header, at offset 0 of every part ------------------------------------
PXP_SIG     equ 0                   ; word: 'PX' - a part that does not start
                                    ; with it is not one of ours, and the
                                    ; resident refuses to call into it
PXP_ABI     equ 2                   ; byte: PXP_ABIVER. A part and a resident
                                    ; built from different trees disagree here
                                    ; before they disagree anywhere worse
PXP_NVEC    equ 3                   ; byte: how many vectors follow
PXP_PKG     equ 4                   ; word: THE PACKAGE'S SEGMENT, written by
                                    ; the resident immediately before every
                                    ; call (rule 2 above)
PXP_VEC     equ 6                   ; PXP_NVEC words: near offsets in the part
PXP_HDRSZ   equ 6

PXP_SIGVAL  equ 'PX'                ; as `dw 'PX'`: 'P' at +0, 'X' at +1
PXP_ABIVER  equ 1

; --- the vectors, in this order in every part ---------------------------------
PXV_INIT    equ 0                   ; out AX = PXP_PROBE. Nothing else: it is
                                    ; the call the gate makes to prove the
                                    ; boundary, and a decoder's place to build
                                    ; tables it would rather not ship
PXV_DECODE  equ 1                   ; a decoder's body (SPEC.md 106.5); a part
                                    ; that is not a decoder answers PXE_NOTSUP
PXV_INFO    equ 2                   ; in ES:DI = a buffer, CX = its bytes. The
                                    ; part's text or facts, copied out
PXV_N       equ 3

PXP_PROBE   equ 0x5850 ^ 0x1D06     ; PXV_INIT's answer, which no register a
                                    ; caller set up could already hold

; --- the answers -----------------------------------------------------------------
PXE_OK      equ 0
PXE_NOTSUP  equ 1                   ; this part has no such verb
PXE_ROOM    equ 2                   ; the caller's buffer is too small
PXE_PART    equ 3                   ; THE RESIDENT'S: the part could not be
                                    ; fetched. op_fetch has already said why
PXE_BADPART equ 4                   ; THE RESIDENT'S: it was fetched and is
                                    ; not a PiXEL part of this ABI - its
                                    ; signature, version or vector count is
                                    ; wrong. Nothing has said so yet

; --- PXPART_HEAD init, decode, info -------------------------------------------
; The header a part opens with. Assemble the part with `org 0`.
%macro PXPART_HEAD 3
    dw PXP_SIGVAL               ; +0
    db PXP_ABIVER               ; +2
    db PXV_N                    ; +3
    dw 0                        ; +4 PXP_PKG, the resident's to write
    dw %1                       ; +6 PXV_INIT
    dw %2                       ; +8 PXV_DECODE
    dw %3                       ; +10 PXV_INFO
%endmacro

%endif
