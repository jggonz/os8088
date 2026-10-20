; =============================================================================
; os8088 - apps/pixel/pxfs.inc
;
; FULL SCREEN's numbers (SPEC.md 106.23), shared by the resident package and
; the FULL-SCREEN part (apps/pixel/pxfull.asm, part 5, LINKED), so both sides
; mean the same mode, the same verb and the same answer. Pure equates.
; =============================================================================

%ifndef PXFS_INC
%define PXFS_INC

; --- PiXEL's modes, in the order a display's default is taken and View >
; Screen turns through them (tools/pixelsim.py's FSM_*) -----------------------
PXM_MODEX   equ 0                   ; 320x240x256, Mode X
PXM_VGA13   equ 1                   ; 320x200x256, 13h
PXM_VGA12   equ 2                   ; 640x480x16, the adaptive sixteen
PXM_DESK    equ 3                   ; the display's own mode, its sixteen (an
                                    ; EGA): a SAME-MODE bracket
PXM_C160    equ 4                   ; 160x100x16, the CGA's retime
PXM_CGA320  equ 5                   ; 320x200x4, a chosen set
PXM_CGA640  equ 6                   ; 640x200x2
PXM_HERC    equ 7                   ; 720x348x2
PXM_N       equ 8
PXM_NONE    equ 0xFF

; --- the part's verbs: PXV_DECODE with CL = one of these ---------------------
FSV_ENTER   equ 0                   ; CH = the mode: set it, its colours, the
                                    ; picture whole, the captions
FSV_SHOW    equ 1                   ; another picture: its colours, Fit, whole
FSV_POLL    equ 2                   ; one pass: AX = an FSA_*
FSV_PROG    equ 3                   ; DI = 0: the progress line, where the
                                    ; worker's rows put it; FFFFh: off
FSV_MSG     equ 4                   ; DI = a line of the package's: the
                                    ; bottom band says it for two seconds

; --- FSV_POLL's answers -------------------------------------------------------
FSA_NONE    equ 0                   ; nothing: the loop may wait a frame
FSA_BUSY    equ 1                   ; a render is owed: poll again at once
FSA_LEAVE   equ 2                   ; F, Esc, Alt+Enter
FSA_NEXT    equ 3                   ; Space, N, the left button
FSA_PREV    equ 4                   ; Backspace, P, the right button
FSA_FIRST   equ 5                   ; Home
FSA_LAST    equ 6                   ; End
FSA_SLIDE   equ 7                   ; S
FSA_MODE    equ 8                   ; M
FSA_STOP    equ 9                   ; a key or a press while a slideshow runs

FS_BAND     equ 10                  ; a caption band's rows
FS_HOLD     equ 36                  ; ...and its ticks up (two seconds)

%endif
