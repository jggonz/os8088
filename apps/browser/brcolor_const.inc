; Browser part 7, the colour part (SPEC.md 71.34): its layout, shared by the
; part (brext_part.asm) and the primary (brcolor.inc) so the two cannot
; disagree. Every offset is into the part's DATA segment (op_seg), which on
; kern_mega is not the selector its code runs on.
BRCL_MATCH  equ 0                   ; word: the entry - ES:SI..CX a value's
                                    ; span -> CF=0, AL = the colour; far
BRCL_SELF   equ 2                   ; word: the data segment (written at bind)
BRCL_THEME  equ 4                   ; word: far, UI task -> AL = the links'
                                    ; colour: the system theme's accent (71.34)
BRCL_CPN    equ 6                   ; byte: checkpoints kept
BRCL_CPOVER equ 7                   ; byte: they ran out
BRCL_CPOFF  equ 8                   ; dword: ...where
BRCL_CPTAB  equ 12                  ; BR_CPMAX x (word, byte, value)
BR_CPMAX    equ 128
BRCL_CODE   equ BRCL_CPTAB + BR_CPMAX * 4
