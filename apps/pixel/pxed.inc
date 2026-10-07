; =============================================================================
; os8088 - apps/pixel/pxed.inc
;
; PiXEL's EDITING numbers (SPEC.md 106.24) - pure equates, shared by the
; resident package and the two parts LINKED against it: the EDIT part
; (apps/pixel/pxedit.asm, the palette and pixel operations) and the WRITE
; part (apps/pixel/pxwrite.asm, Save As's five writers). Both halves read
; them, so the two cannot disagree about a verb or a format.
; =============================================================================

%ifndef PXED_INC
%define PXED_INC

; --- THE EDIT PART'S VERBS, in CL on its DECODE vector ---------------------------
; The palette operations, on the UI task: [px_pal] edited in place, nothing
; else touched (SPEC.md 106.24). Their parameters are [px_ep] words
EV_INVERT   equ 0
EV_GREY     equ 1
EV_SEPIA    equ 2
EV_BRICON   equ 3                   ; [px_ep] brightness, [px_ep + 2] contrast
EV_GAMMA    equ 4                   ; [px_ep] an index into the gammas
EV_POSTER   equ 5                   ; [px_ep] levels, 2..16
EV_THRESH   equ 6                   ; [px_ep] the level, 0..255
EV_LEVELS   equ 7                   ; the shown master's histogram's 1%/99%
EV_NPAL     equ 8                   ; (the palette verbs are below this)
; The pixel operations, on the WORKER: the shown master ([px_cur]) into a
; destination ([px_edseg], 0 = in place, [px_edw] x [px_edh]) through the
; work claim ([px_ewseg])
EV_ROTCW    equ 8
EV_ROTCCW   equ 9
EV_ROT180   equ 10                  ; in place
EV_FLIPH    equ 11                  ; in place
EV_FLIPV    equ 12                  ; in place
EV_CROP     equ 13                  ; [px_ep] x1, y1, x2, y2 (inclusive)
EV_RESIZE   equ 14                  ; [px_ep] num, [px_ep + 2] den
EV_BLUR     equ 15
EV_SHARP    equ 16
EV_EDGE     equ 17
EV_EMBOSS   equ 18
EV_PIXEL    equ 19                  ; blocks of PE_PIXN
EV_N        equ 20
EV_WORK     equ 0x80                ; | an operation, on the UI task: AX = the
                                    ; work claim's KB it needs, and
                                    ; [px_edw] [px_edh] the destination's
                                    ; size; CF = 1 it cannot be done (too
                                    ; wide for one work segment)

PE_PIXN     equ 8                   ; Pixelate's block, master pixels
PE_NGAMMA   equ 15                  ; the gammas (pixelsim's GAMMAS)
PE_GAMMA1   equ 7                   ; ...and the index of 1.0

; --- THE WRITE PART'S FORMATS, in CL on its DECODE vector (on the worker) ------
WF_PNG      equ 0
WF_GIF      equ 1
WF_BMP      equ 2
WF_PCX      equ 3
WF_PIX      equ 4
WF_BMP24    equ 5                   ; offered on a CUBE master only
WF_N        equ 6
WF_SIZE     equ 0x80                ; | a format, on the UI task: CF = 1 the
                                    ; master cannot be written in it (PIX's
                                    ; one-segment block), AX = the reason

; --- THE UI TASK'S VERBS (CL, bit 6 set): the parameter cards and the XMS
; copy are the EDIT part's; Save As's names, its pump and its end, and Copy,
; the WRITE part's. Both reach the resident through pxsvc.inc's gate
EV_UOPEN    equ 0x40                ; card [px_pcon] up: its defaults
EV_ULAY     equ 0x41                ; its buttons laid out, those under it hid
EV_UDRAW    equ 0x42                ; drawn whole (the painter's, clip armed)
EV_UKEY     equ 0x43                ; AX = a key: out AX = PCA_*
EV_UFIRE    equ 0x44                ; AX = its button that fired: out PCA_*
EV_UOK      equ 0x45                ; OK: out AL = the verb to run, its
                                    ; parameters in [px_ep]; 0FFh Save As
EV_UROOM    equ 0x46                ; [px_eop]'s "needs NK; MK free" into
                                    ; px_cline
EV_UXOUT    equ 0x47                ; the shown master out to XMS: CF = 1 no
EV_UXIN     equ 0x48                ; ...and back, the copy given back
EV_UPAL     equ 0x49                ; AX = a palette verb: banked for undo,
                                    ; done, shown
EV_USTART   equ 0x4A                ; AX = a pixel verb: its claims made,
                                    ; the worker started
EV_UFIN     equ 0x4B                ; the worker answered it: committed (or
                                    ; cancelled), shown
EV_UUNDO    equ 0x4C                ; Edit > Undo: the exchange, the flip
                                    ; again, or the copy back

OC_PAL      equ 0                   ; an operation's class
OC_FLIP     equ 1                   ; in place; undone by doing it again
OC_DEST     equ 2                   ; a second master, always
OC_EITHER   equ 3                   ; a second master, or in place over an
                                    ; XMS copy
UK_NONE     equ 0                   ; what undo holds
UK_PAL      equ 1                   ; the other palette
UK_FLIP     equ 2                   ; nothing: a flip is done again
UK_MASTER   equ 3                   ; the old master's claim
UK_XMS      equ 4                   ; the old master above 1 MB
WV_UNAME    equ 0x40                ; [px_sname] := the picture's name, the
                                    ; format's extension (the dialog's)
WV_UDONE    equ 0x41                ; the dialog answered [px_sname]: the
                                    ; format by its extension, the folder;
                                    ; CF = 1 the name is taken (px_aq asks)
WV_USTART   equ 0x42                ; the job started: CF = 1 no memory
WV_UPUMP    equ 0x43                ; W_ONWAKE: the full slots written
WV_UFIN     equ 0x44                ; the worker answered: the end
WV_UCOPY    equ 0x45                ; Edit > Copy: CF = 1 refused (said)

JOB_EDIT    equ 3                   ; the worker's two jobs (pxpump.inc's
JOB_SAVE    equ 4                   ; JOB_*), and [px_busy] while they run
PXB_EDIT    equ 3
PXB_SAVE    equ 4

PCA_SPENT   equ 0                   ; what a card's key or press asks for
PCA_OK      equ 1
PCA_CANCEL  equ 2

PC_BC       equ 1                   ; [px_pcon]: the card up, 0 none
PC_GAM      equ 2
PC_POST     equ 3
PC_THR      equ 4
PC_RES      equ 5
PC_SAVE     equ 6

PX_B_C1M    equ 21                  ; a card's buttons, in the window's one
PX_B_C1P    equ 22                  ; record (pixel.asm's PX_B_*)
PX_B_C2M    equ 23
PX_B_C2P    equ 24
PX_B_COK    equ 25
PX_B_CCAN   equ 26
PX_NBTN     equ 27
PE_DIS      equ 1                   ; os88ui.inc's OS88UI_DIS
PE_DRSZ     equ 26                  ; ...and its drop-down record
PE_DR_RECT  equ 0
PE_DR_ITEMS equ 8
PE_DR_N     equ 10
PE_DR_SEL   equ 12
PE_DR_WIN   equ 14
PE_DR_OPEN  equ 16
PE_DR_HOT   equ 17

PE_IVSZ     equ 24                  ; Image Info's value buffers (pixel.asm's
PE_IPICK    equ 8                   ; PX_IVSZ) and the Picked line's (pxui.inc's
                                    ; PX_IPICK): Copy Info reads them

; --- what both write back: the answers -------------------------------------------
PXW_OK      equ 0
PXW_PIXBIG  equ 1                   ; a PIX block would pass one segment
PXW_DISK    equ 2                   ; the resident's: the volume refused

%endif
