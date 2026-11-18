; Part 7's font entries (SPEC.md 71.35), after its colour block, and the
; parameter block of a draw, shared by the part (brext_part.asm) and the
; primary (brfont.inc) so the two cannot disagree. Needs brcolor_const.inc.
BRFN_OPEN   equ BRCL_CODE + 0       ; word: far, on the UI task: open the face
                                    ; -> AL = 1 opened, 0 none (no SYSTEM/FONTS,
                                    ; no memory); the part serves h1 either way
BRFN_ADV    equ BRCL_CODE + 2                   ; word: far, AL = a character -> AL = its
                                    ; advance in pixels in the face; all else kept
BRFN_DRAW   equ BRCL_CODE + 4                   ; word: far, ES:SI = the text, CX = its
                                    ; length, DI = a BRFP block in the CALLER's
                                    ; DS -> one 8-row slice of a tall line drawn
BRFN_SELF   equ BRCL_SELF           ; the data segment: the colour block's word
BRTB_ENTRY  equ BRCL_CODE + 6       ; word: far, the table composer's br_table

; the draw's parameters, BRFP_SIZE bytes in the caller's segment
BRFP_X      equ 0                   ; word: the row's screen x
BRFP_Y      equ 2                   ; word: ...and y
BRFP_W      equ 4                   ; word: the row's width in pixels
BRFP_PEN    equ 6                   ; word: AL ink, AH ground (BLIT1_PEN's)
BRFP_X0     equ 8                   ; word: the text's pen, pixels into the row
BRFP_KIND   equ 10                  ; byte: BRT_FACE or BRT_X2
BRFP_SLICE  equ 11                  ; byte: 0 the top 8 rows, 1 the bottom 8
BRFP_MOFF   equ 12                  ; word: a letter's mark is at its byte + this
BRFP_AOFF   equ 14                  ; word: ...and its attribute at + this
BRFP_COLR   equ 16                  ; byte: 1 the row has colours (a run each)
BRFP_ULM    equ 17                  ; byte: the marks underlined (71.36)
BRFP_SIZE   equ 18

BRT_CELL    equ 0                   ; the 8x8 cell, on a tall line's baseline
BRT_FACE    equ 1                   ; a tall line in the face (h2, h3)
BRT_X2      equ 2                   ; ...in the 8x8 cell doubled, bold (h1)
BRT_X2ADV   equ 16                  ; an x2 character's advance
LM_LINK     equ 1                   ; (browser.asm's marks, read by the draw)
LM_FOCUS    equ 4
LM_SELECT   equ 8
LM_BOLD     equ 32
LM_UNDER    equ 128
LM_SNAP     equ 16                  ; a table cell's first letter: the pen
                                    ; snaps to its column (71.35)
LM_BIG      equ 2                   ; a cell's mark: a big letter (<big>,
                                    ; <font size>), set in the face
