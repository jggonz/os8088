; =============================================================================
; os8088 - apps/os88ui.inc
;
; THE STANDARD CONTROL, and it is ONE SOURCE FOR TWO WORLDS: this file
; assembles into the KERNEL's own dialogs and into any package that wants it,
; from the same text. Define OS88UI_KERNEL before including it and every
; primitive call below becomes the kernel's own (`KERNEL_SEG:cw_*`, the
; cold-to-text shims of SPEC.md 2.6, or the `ct_*` cold trampoline in front
; of one when the kernel has one - SPEC.md 2.6.2); leave it undefined and they
; become `OSAPI_*` far calls. Nothing else differs.
;
; THAT IS THE WHOLE POINT OF THE FILE. Saving bytes was never the argument -
; measured, consolidating the kernel's private helpers was worth twelve
; (docs/plans/completed/UIHELPERS-PLAN.md 7.1). The argument is that a feature added to the
; button - a pressed state, a focus ring, a different disabled treatment -
; lands in the Standard File dialog, the Control Panel, the Timer AND every
; package at once, because there is one body rather than ten that agree by
; hand. SPEC.md 47's greying rule was fixed FIVE separate times in this tree,
; each as its own bug, always CDGRAY without [gfx_dis] - a real grey on VGA
; and SOLID BLACK on both 1bpp adapters, so the greyed caption came out
; pixel-identical to a live one on the machine this OS is for. Here the pen
; is taken once, in one place, and rule 1 stops being something to remember.
;
; IT IS NOT AN API SLOT, deliberately. A slot would cost a cell, a published
; contract and a table revision; an include costs the kernel one copy in
; .cold and each package its own, and can be revised freely because it is
; source. (SPEC.md 20.3.1's free list would have paid for a cell - but the
; cell is not the expensive part of a slot, the contract is.)
;
; --- IN A PACKAGE ------------------------------------------------------------
; `%include "os88ui.inc"` AT THE END OF YOUR CODE, just before OS88_BSS. Not
; up beside os88api.inc: the header, and an OS88_ICON16 block if you have one,
; are at FIXED OFFSETS in the image (SPEC.md 20.2), and code emitted between
; them fails the icon macro's own offset assertion.
;
; os88api.inc stays code-free on purpose - every package includes it
; unconditionally, so a byte there is a byte on every package. This is the
; second shared include in apps/ and the first carrying code.
;
; --- IN THE KERNEL -----------------------------------------------------------
; Included ONCE, into `.cold`, with OS88UI_KERNEL defined. Cold because that
; is where its callers are (fdlg.inc and ctrl.inc are cold in their entirety);
; .text reaches it through the one far entry at the bottom of this file, which
; is the fdf_/cpf_ pattern. DS is KERNEL_SEG in both worlds, so the data word
; below needs no override either way.
;
; WHAT IT IS NOT FOR. A skinned control. ModPlug's bevelled well and LED
; transport are a deliberate port of ModPlugPlayer's look (SPEC.md 56) and
; Minesweeper's cell is its own game's chrome - converting those would be
; undoing intended design, not consolidating it.
;
; GEOMETRY IS A POINTER, and that is the point. Every routine here takes
; BX = a 4-word rect {x1, y1, x2, y2}, INCLUSIVE, in screen coordinates -
; gfx_frame's own convention - so the drawn control and the clickable control
; read the same four words and cannot drift. That is fm_hit's discipline
; (SPEC.md 22) applied everywhere at once.
;
; REGISTERS: every routine preserves everything except its documented output.
;
; --- DRAWING RULES FOR A NEW CONTROL, and both of them get broken -----------
; They are PERFORMANCE.md Part 6 rules 1 and 2 in the shape a control author
; meets them, and they are here because the general statement did not fire:
; both were broken in the first version of os88ui_rad by someone who had just
; read them, because the working code next door does it the other way.
;
;   1. NEVER FILL A GROUND AND THEN DRAW ON IT. To clear something, draw its
;      replacement over it in the right colour - one write, no interval. A
;      fill followed by the real content five drawing calls later is ~4ms of
;      BLANK on a 4.77MHz 8088, every repaint. Text is free here: font_run is
;      opaque and lays ground and glyph in one pass (SPEC.md 6.1), so a label
;      never needs a ground laid for it.
;
;   2. A CHANGE REDRAWS WHAT CHANGED. A control that knows its own old and new
;      state redraws the difference and not the row - os88ui_raddot is six
;      pixels' worth of dot and touches no text, where redrawing the row would
;      re-letter a label that did not change.
;
;   ...and the CALLER owns the pane's ground (os88ui_drop's contract). Filling
;   it again inside a control is exactly the second write rule 1 forbids.
;
; NEITHER DEFECT SHOWS IN A SCREENSHOT - the final frame is identical - so a
; gate for a control reads the CALLS (tests/radio.py records every gfx_fill's
; rect and every font_run_x's y). PERFORMANCE.md Part 1 is why: a double-draw
; flash and a visible redraw are two of the three things an emulator cannot
; show, and this project has paid for both repeatedly.
;
; os88ui_chk (SPEC.md 13.15) DID NOT FOLLOW RULE 1 for years - it filled its
; whole rect white and then drew frame, mark and label over it - and it does
; not any more (13.15.2): the label is an opaque font_run and the box is
; cleared by os88ui_glyph, so THE CALLER OWNS THE PANE'S GROUND. It is named
; here because it is the file's most-copied routine and the next author will
; copy it too: os88ui_rad (13.17.4), which lays no ground at all, is the one
; to follow.
; =============================================================================

%ifdef OS88UI_KERNEL
  %macro UI_FRAME 0
    call ct_cw_gfx_frame
  %endmacro
  %macro UI_FILL 0
    call ct_cw_gfx_fill
  %endmacro
  %macro UI_STR 0
    call KERNEL_SEG:font_str
  %endmacro
  %macro UI_RUN 0                   ; ...and the OPAQUE one (SPEC.md 6.1)
    call ct_cw_font_run
  %endmacro
  %macro UI_WIDTH 0
    call ct_font_width
  %endmacro
  %macro UI_PEN 0
    call ct_cw_gfx_pen_cf
  %endmacro
  %macro UI_ICON 0                  ; SPEC.md 25.6: the masked 1bpp sprite
    call KERNEL_SEG:cw_icon_draw    ; pass - CX = x, DX = y, SI -> record
  %endmacro
  %macro UI_IPEN 0                  ; ...and the pair it draws in, one-shot
    call KERNEL_SEG:cw_icon_pen
  %endmacro
  %macro UI_CLIPT 0                 ; ...and may that WHOLE shape be drawn?
    call KERNEL_SEG:cw_wm_clip_test ; (SPEC.md 11.3) - CF = 1 it may not
  %endmacro
  %macro UI_ISMONO 0                ; ZF = 1 on a COLOUR adapter (SPEC.md 39.5)
    cmp byte [vid_mono], 0
  %endmacro
  %macro UI_WHITE 0
    mov byte [gfx_color], CWHITE
  %endmacro
  %macro UI_BLACK 0
    mov byte [gfx_color], CBLACK
  %endmacro
  %macro UI_INK 0
    mov [gfx_color], al
  %endmacro
  %macro UI_HLINE 0
    call ct_cw_gfx_hline
  %endmacro
  %macro UI_GRAY 0
    call KERNEL_SEG:cw_gfx_fill_gray
  %endmacro
  %macro UI_TICKS 0
    mov ax, [ticks]
  %endmacro
  %macro UI_CPUTIER 0               ; AL = CPU_8086 / CPU_286 / CPU_386, the
    mov al, [cpu_tier]              ; published tier (SPEC.md 41.1). AH is
  %endmacro                         ; CLOBBERED in the package arm and not
                                    ; here, so a caller may rely on neither
%else
  %macro UI_FRAME 0
    call OSAPI_GFX_FRAME
  %endmacro
  %macro UI_FILL 0
    call OSAPI_GFX_FILL
  %endmacro
  %macro UI_STR 0
    call OSAPI_FONT_STR_XPARENT
  %endmacro
  %macro UI_RUN 0                   ; ...and the OPAQUE one (SPEC.md 6.1)
    call OSAPI_FONT_RUN
  %endmacro
  %macro UI_WIDTH 0
    call OSAPI_FONT_WIDTH
  %endmacro
  %macro UI_PEN 0
    call OSAPI_GFX_PEN
  %endmacro
  %macro UI_PIXEL 0
    call OSAPI_GFX_PIXEL
  %endmacro
  %macro UI_ICON 0                  ; SPEC.md 25.6: the masked 1bpp sprite
    call OSAPI_ICON_DRAW            ; pass - CX = x, DX = y, SI -> record
  %endmacro
  %macro UI_IPEN 0                  ; ...and the pair it draws in, one-shot
    call OSAPI_ICON_PEN
  %endmacro
  %macro UI_CLIPT 0                 ; ...and may that WHOLE shape be drawn?
    call OSAPI_WM_CLIP_TEST         ; (SPEC.md 11.3) - CF = 1 it may not
  %endmacro
  %macro UI_ISMONO 0                ; ZF = 1 on a COLOUR adapter (SPEC.md 39.5)
    push ax                         ; OSAPI_VIDEO documents four outputs and
    push bx                         ; says nothing about the rest, and every
    push cx                         ; register is live at the one call site -
    push dx                         ; so bank them all. A pop writes no flags,
    push si                         ; which is what lets the answer survive
    push di
    call OSAPI_VIDEO
    cmp dh, 4
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
  %endmacro
  %macro UI_WHITE 0
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
  %endmacro
  %macro UI_BLACK 0
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
  %endmacro
  %macro UI_INK 0
    call OSAPI_SET_COLOR
  %endmacro
  %macro UI_HLINE 0
    call OSAPI_GFX_HLINE
  %endmacro
  %macro UI_GRAY 0
    call OSAPI_GFX_FILL_GRAY
  %endmacro
  %macro UI_TICKS 0
    call OSAPI_GET_TICKS
  %endmacro
  %macro UI_CPUTIER 0               ; ...and the slot answers AL = the tier and
    call OSAPI_CPU_INFO             ; AH = the feature bits in one word, so AH
  %endmacro                         ; is clobbered HERE and is not in the
                                    ; kernel arm: neither may be relied on.
                                    ; Unlike UI_ISMONO this banks nothing -
                                    ; cpu_info is `mov ax, [cpu_tier]` and a
                                    ; `ret`, and three packages already call
                                    ; it mid-routine with registers live
%endif

OS88UI_DIS  equ 1               ; disabled: dithered frame AND label (47 r2)
OS88UI_DEF  equ 2               ; the default button's outer ring, 2px out
OS88UI_FILL equ 4               ; ERASE THE INTERIOR FIRST - and the question
                                ; it answers is NOT "is my background white".
                                ; It is: CAN THIS BUTTON BE DRAWN A SECOND
                                ; TIME WITHOUT THE GROUND BEING REPAINTED
                                ; FIRST? A button whose greying can move
                                ; always can be, and font_char is
                                ; TRANSPARENT - it ors ink in and erases
                                ; nothing - so the second draw's checkerboard
                                ; caption lands on top of the first draw's
                                ; solid one and the greyed label comes out
                                ; PIXEL-IDENTICAL to the live one. The frame
                                ; is not affected and so still dithers, which
                                ; is what makes the failure read as "47 rule 1
                                ; is broken" when the mechanism is fine and
                                ; the erase is what is missing (SPEC.md 47,
                                ; docs/plans/completed/UIHELPERS-PLAN.md 13.4). Set it unless
                                ; the caller white-fills its content first.
OS88UI_INK  equ 8               ; ...and DI's HIGH BYTE is the ink, for a
                                ; button whose label is deliberately not black
                                ; (Piano's song buttons, SPEC.md 47's own
                                ; example of a colour that is decoration
                                ; rather than state). It is a flag plus a byte
                                ; and not simply "AL = the colour", because
                                ; every existing caller reaches here with
                                ; ARBITRARY AX and a silent reinterpretation
                                ; of a live register is the shape of bug this
                                ; file has already produced twice. DI is the
                                ; flag word: every caller either builds it
                                ; from OS88UI_* constants or clears it, so its
                                ; high byte is 0 by construction.
                                ;
                                ; DISABLED WINS. A greyed control is CDGRAY
                                ; and dithered whatever colour it would
                                ; otherwise be - 47 rule 1 is about state and
                                ; this is about decoration, and state is not
                                ; negotiable.
OS88UI_DOWN equ 16              ; THE PRESSED LOOK (SPEC.md 13.8): interior
                                ; BLACK, label WHITE, frame unchanged. It is a
                                ; DRAWN STATE and not an XOR overlay, which is
                                ; the whole of why it is a flag on the painter
                                ; rather than a routine of its own - see the
                                ; block comment below os88ui_btn.
                                ;
                                ; It IMPLIES the fill: a button being drawn
                                ; down is being drawn over its own upright
                                ; self, so the erase is never optional and
                                ; OS88UI_FILL is not consulted on this path.
                                ;
                                ; DISABLED WINS over this too, and the case is
                                ; not hypothetical: a control can be greyed
                                ; between the press and the release (a Stop
                                ; button whose stream ended). A greyed-down
                                ; button would be a black box with a dithered
                                ; caption in it, which says "pressed" about a
                                ; control that will refuse.
                                ;
                                ; IT IS THE LIBRARY'S NOW (SPEC.md 20.5.1.3).
                                ; os88ui_btn resolves it from the record's
                                ; OS88UI_BT_DOWN, so a caller neither sets it
                                ; nor has to remember to pass it back from its
                                ; painter - which was obligation 7 of the
                                ; seven a hand-rolled gesture owed, and the
                                ; one whose omission left a repaint mid-press
                                ; disagreeing with the glass.
OS88UI_LATCH equ 32             ; THE SAME PICTURE, A DIFFERENT CAUSE (SPEC.md
                                ; 20.5.1.4): this control's SETTING is on,
                                ; where OS88UI_DOWN means a press is live on
                                ; it this instant. The caller's, in its flags
                                ; array, because it is the caller that knows.
                                ;
                                ; They draw identically and deliberately so -
                                ; to somebody looking at the screen a held
                                ; button and a latched one mean the same
                                ; thing - so this is folded into DOWN at the
                                ; top of os88ui_btn and the drawing body has
                                ; never heard of it. DISABLED still outranks
                                ; both (47 rule 1).
                                ;
                                ; GENERIC rather than the one caller that
                                ; forced it: Audio drew Shuffle and Repeat
                                ; with OS88UI_DOWN to mean "on", which was
                                ; correct while that flag was the caller's and
                                ; collided the moment it stopped being. A
                                ; latched button is an ordinary thing to want
                                ; and ModPlug, Tracker and Paint each draw one
                                ; by hand today.
OS88UI_IMG equ 64               ; THE LABEL IS A PICTURE, not a string (SPEC.md
                                ; 13.8.9). The labels-array entry points at an
                                ; OSAPI_ICON_DRAW record - `db 1, rows` then
                                ; `rows` MASK words then `rows` DATA words, bit
                                ; 15 leftmost - centred in the button exactly
                                ; as a caption is. Give it a FULL mask (0xFFFF
                                ; every row): the picture is then opaque over
                                ; its own 16 x rows cell, ground and ink in one
                                ; pass, which is what lets the button never
                                ; write a pixel twice. Pressed, the pair
                                ; inverts like a caption does.
                                ;
                                ; ONLY UNDER `%define OS88UI_BIMG`, and that
                                ; define changes the drawing body for the
                                ; package's text buttons too - see
                                ; os88ui_bdraw1. A package that does not set it
                                ; assembles byte-identical: the flag is an equ
                                ; and costs nothing unset.
%ifdef OS88UI_BIMG
 %ifdef OS88UI_KERNEL
  %error "OS88UI_BIMG is a PACKAGE's: the kernel's dialogs draw text buttons"
 %endif
 %ifdef OS88UI_NOBTN
  %error "OS88UI_BIMG draws buttons, and OS88UI_NOBTN/BARONLY has taken them out"
 %endif
%endif

; --- THE RECORD (SPEC.md 20.5.1.3) -------------------------------------------
; One per button GROUP, in the CALLER's data, because that is the shape the
; tree has rather than a preference: Sheet's ten buttons are five dialog
; PAIRS, ftpd's and Ether's are triples, and Calculator's and TexPad's are
; loops over a table. One per window would have been wrong for all five of
; Sheet's.
;
; THE RECTS MUST BE CONTIGUOUS - os88ui_btnpress walks them with os88ui_bfind
; - and that is the single constraint the record imposes. What it buys is that
; the drawn control and the clickable control are ONE description, which is
; SPEC.md 22's fm_hit discipline arriving at the control it was written about.
OS88UI_BT_RECTS  equ 0          ; near ptr: 4-word INCLUSIVE screen rects,
                                ; filled by YOUR painter from OSAPI_WM_CONTENT
                                ; every pass, because a window MOVES and does
                                ; not call its paint proc when it does
OS88UI_BT_LABELS equ 2          ; near ptr: an array of near ptrs to NUL labels
OS88UI_BT_FLAGS  equ 4          ; near ptr: an array of flag WORDS, or 0 for
                                ; none at all - which is the common case, so
                                ; it is a pointer rather than an inline array
OS88UI_BT_N      equ 6          ; how many are LIVE this pass. A paged window
                                ; points ONE record at either page's buttons,
                                ; which is cal_nrect's own device promoted
                                ; into the record
OS88UI_BT_WIN    equ 8          ; the window, written by os88ui_btninit
OS88UI_BT_DOWN   equ 10         ; THE LIBRARY'S: index PLUS ONE of the control
                                ; a press is live on, 0 for none. Read it if
                                ; you like; never write it
OS88UI_BT_ONCLK  equ 12         ; YOUR own W_ONCLICK proc, or 0. The library
                                ; installs ITSELF as the window's click
                                ; handler (SPEC.md 20.5.1.3.3) and chains here
                                ; when the press was not a button's, so a
                                ; package cannot have a click path that skips
                                ; the buttons - which is how ArtfulType's
                                ; splash shipped acting on the press while its
                                ; modal, converted the same day, did not
OS88UI_BT_NEXT   equ 14         ; THE LIBRARY'S: the next record of this
                                ; package, so one click thunk can serve any
                                ; number of windows. A LIST and not a table
                                ; because Sheet has five dialogs and a table
                                ; would have a limit to get wrong
OS88UI_BT_SIZE   equ 16

; OS88UI_BTNREC declares a group's record and lives in apps/os88api.inc, not
; here: a macro is PREPROCESSOR-time and must be defined before it is used,
; and every package includes the SDK at its top and this file near its bottom.
; The offsets above are `equ`s and forward-resolve, which is why they can stay.

; --- OS88UI_BARONLY - the bar WITHOUT the button (SPEC.md 13.10.6.5) ---------
; An OPT-OUT, and it is 13.10.4's own rule pointed the other way: that section
; gates the scroll bar so "a package which never draws a bar does not carry
; one", and the button had no such gate - so a package that wanted only the
; bar paid for os88ui_btn, os88ui_bhit, os88ui_glyph, four 12x12 bitmaps and
; the click machinery as well.
;
; **MEASURED ON FROTZ, WHICH DRAWS NO STANDARD BUTTON AT ALL**: including this
; file cost it 1,132 bytes where Word - which already had it, for its buttons -
; paid 565 for the same bar. The difference is exactly this block.
;
; It is an opt-out rather than an opt-in so that every existing consumer is
; byte-identical, which is checked rather than asserted.
;
; --- OS88UI_NOBTN - the SAME opt-out, for a package with no bar either -------
; OS88UI_BARONLY is the button's opt-out spelled as a statement about the
; SCROLL BAR, and the About card (OS88UI_ABOUT, SPEC.md 20.5.1) is the first
; consumer for which that sentence is simply false: it draws no bar and no
; button, and under the old spelling the only way to say so was a name that
; promised a bar. So the gate is OS88UI_NOBTN and OS88UI_BARONLY now IMPLIES
; it - which is why every existing consumer is still byte-identical, and the
; %error below still fires for the one thing neither name may mean, an include
; that emits no drawing code at all.
%ifdef OS88UI_BARONLY
 %ifndef OS88UI_SCROLL
  %error "OS88UI_BARONLY without OS88UI_SCROLL includes no drawing code at all"
 %endif
 %define OS88UI_NOBTN
%endif
%ifdef OS88UI_NOBTN
 %ifdef OS88UI_KERNEL
  %error "OS88UI_NOBTN/BARONLY is a PACKAGE's: both kernel dialogs draw buttons"
 %endif
 %ifndef OS88UI_SCROLL
  %ifndef OS88UI_ABOUT
   %error "OS88UI_NOBTN with neither OS88UI_SCROLL nor OS88UI_ABOUT includes no drawing code at all"
  %endif
 %endif
%endif
%ifdef OS88UI_NOGEST
 %ifdef OS88UI_KERNEL
  %error "OS88UI_NOGEST is a PACKAGE's: the kernel's dialogs install the gesture"
 %endif
 %ifndef OS88UI_ARM
  %error "OS88UI_NOGEST without OS88UI_ARM leaves NO press path - the buttons would fire on the press (SPEC.md 20.5.1.3.4)"
 %endif
%endif
%ifndef OS88UI_NOBTN
; -----------------------------------------------------------------------------
; os88ui_btn - draw ONE button of a group (SPEC.md 20.5.1.3)
; in:  BX = the record, AL = the button's index PLUS ONE; the gfx lock is held
;      (every callback and every kernel painter does hold it)
; out: nothing; all registers preserved
;
; THE PUBLIC ENTRY, and the ONLY way to draw a standard button. It resolves
; the rect, the label and the flags out of the record, folds in the pressed
; look itself, and hands the result to the drawing body below.
;
; It takes an INDEX rather than a rect so that the pressed state can be the
; library's. That is the whole of the change: os88ui_btn used to take a loose
; rect, label and flag word, and a caller that drew one that way owned the
; entire SPEC.md 13.7 gesture by hand - two slots to install that are
; deliberately not template words, an arm on the press, a track on the drag, a
; fire on the release, and OS88UI_DOWN passed back from its own painter so
; that a repaint agreed with the glass. Seven obligations in three callbacks,
; not one of which fails to ASSEMBLE when it is missing, and twenty-five of
; the tree's fifty-one call sites had skipped all seven and fired on the
; press. docs/plans/BUTTON-GESTURE-PLAN.md is the design record.
;
; PLUS ONE, because 0 is "no button" everywhere else in this file -
; os88ui_bfind's answer, os88ui_arm's sentinel and OS88UI_BT_DOWN's - so a
; whole gesture is those routines' answers handed straight back here with no
; adjustment and no separate "is anything armed" flag to keep in step.
; -----------------------------------------------------------------------------
os88ui_btn:
    push ax
    push bx
    push cx
    push si
    push di
    xor ah, ah
    dec ax                      ; AX = the 0-based index
    js .out                     ; AL = 0 means "no button": draw nothing, so a
                                ; caller may hand us os88ui_bfind's answer raw
    cmp ax, [bx+OS88UI_BT_N]
    jae .out                    ; past the live count: likewise nothing. Both
                                ; guards are three bytes and they are what let
                                ; every caller skip a test of its own
    mov si, ax
    add si, si                  ; SI = index * 2, for the two word arrays

    xor di, di                  ; DI = this button's flags, none by default
    mov cx, [bx+OS88UI_BT_FLAGS]
    jcxz .down                  ; no flags array at all is the common case
    push bx                     ; [bx+si] is the only legal way to index a
    mov bx, cx                  ; near array on an 8086 - CX and DI are not
    mov di, [bx+si]             ; base registers - so the record moves aside
    pop bx                      ; for one instruction and comes straight back
.down:
    or di, OS88UI_FILL          ; **THE ERASE IS NOT OPTIONAL ANY MORE, AND IT
                                ; IS THE CONTROL'S JOB RATHER THAN THE
                                ; CALLER'S** (SPEC.md 20.5.1.3.1). A button
                                ; that could never be drawn PRESSED could skip
                                ; it and leave the ground to whatever drew it;
                                ; once the record carries the gesture, EVERY
                                ; button can be pressed, and the upright
                                ; redraw that follows a release has to wipe
                                ; the black interior the pressed one left.
                                ; Without it the frame is redrawn round a
                                ; black box and the caption is lettered BLACK
                                ; ON BLACK: the button goes dark and its text
                                ; disappears, which is what the DOS box did
                                ; the day it was converted - it had always
                                ; drawn with `xor di, di`, correctly, because
                                ; nothing had ever pressed it.
                                ;
                                ; Paint had already written the rule down at
                                ; its own call site and it was still one
                                ; caller's to remember, which is the whole
                                ; argument for putting it here: a capability
                                ; the button gains has to reach every button.
    mov cx, ax
    inc cx
    cmp cx, [bx+OS88UI_BT_DOWN]
    jne .latch
    or di, OS88UI_DOWN          ; a press is live on THIS one
.latch:
    test di, OS88UI_LATCH       ; ...and a LATCHED control draws the same
    jz .lab                     ; picture, which is why the body below has
    or di, OS88UI_DOWN          ; never heard of OS88UI_LATCH: it is folded
.lab:                           ; into DOWN here and nowhere else
    push bx
    mov bx, [bx+OS88UI_BT_LABELS]
    mov si, [bx+si]             ; SI = the label
    pop bx
    mov cx, ax
    add cx, cx
    add cx, cx
    add cx, cx                  ; CX = index * 8, the rect stride
    add cx, [bx+OS88UI_BT_RECTS]
    mov bx, cx                  ; BX = the rect, and the body below is
                                ; reached by FALLING THROUGH: it was
                                ; os88ui_btnraw until every caller in the tree
                                ; was converted, and a routine only this one
                                ; reaches does not need a name. There is one
                                ; way to draw a button now.
%ifdef OS88UI_BIMG
    call os88ui_bdraw1          ; ...or the one-write body, for a package
%else                           ; that opted into it (SPEC.md 13.8.9)
    call os88ui_bdraw
%endif
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

%ifndef OS88UI_BIMG
; -----------------------------------------------------------------------------
; os88ui_bdraw - the drawing body (PRIVATE: os88ui_btn is the only caller)
; in:  BX = rect {x1,y1,x2,y2} inclusive, SI = NUL label, DI = OS88UI_* flags
;      caller holds the gfx lock
; out: nothing; all registers preserved
;
; This is what os88ui_btn was before the record, unchanged to the byte: the
; conversion moved the ARGUMENTS and left the picture alone, which is what
; makes "every button looks exactly as it did" checkable rather than claimed.
;
; IT WAS CALLED os88ui_btnraw AND WAS REACHABLE FROM ANYWHERE, as the scaffold
; that let the tree convert one caller at a time. That is exactly the thing a
; scaffold must not become: the next author looking for "how do I draw a
; button" would have found a painter with no gesture and copied it. Every
; caller is converted, so the name is gone and this is private.
;
; **THE RECT IS IN SCREEN COORDINATES AND MUST BE COMPUTED FROM THE CONTENT
; ORIGIN EVERY TIME** - OSAPI_WM_CONTENT and OSAPI_WM_GEOM - never from a
; screen origin and never from a copy banked on an earlier pass. A window that
; MOVES does not call its paint proc: the kernel replays its pixels instead
; (SPEC.md 11.96.12), so a banked rect goes on describing where the window used
; to be, and the control is then drawn and hit-tested on somebody else's
; pixels. apps/os88api.inc's header carries the rule and why it looks like it
; works until it does not.
;
; Optional white interior, frame, optional default ring, then the label
; CENTRED IN BOTH AXES.
;
; Centring is not a choice this imposes - it is what all four kernel buttons
; were already doing with the arithmetic precomputed as a literal, which is
; why they could be unified at all: the Control Panel's Activate button adds
; +6 to a 116px box with a 104px label ((116-104)/2), the file dialog adds +3
; in a 14-row box ((14-8)/2), and the Timer writes it out as
; APP_TMR_LT = (APP_TMR_BH - 8) / 2. Three literals and one formula, all the
; same number.
;
; A WHOLE-RECT fill and an INTERIOR fill are the same picture once the frame
; is drawn over the border, so OS88UI_FILL covers both and the Timer's
; whole-rect version needed no flag of its own.
;
; THE PEN IS PUT BACK on every path. Leaving CDGRAY behind is untidy; leaving
; [gfx_dis] behind draws the next string in this lock hold as a checkerboard,
; which is a bug in somebody else's code with its cause in this routine.
;
; --- THE DOWN STATE IS DRAWN, NOT XOR-ED (SPEC.md 13.8) ----------------------
; OS88UI_DOWN makes this routine paint the pressed look: interior black, label
; white, same frame. It is deliberately NOT an `os88ui_bflip` that XORs the
; interior, which is the shorter and the wrong answer, and the reason is a
; hazard Calculator had already met and worked around in private:
;
;   XOR IS ITS OWN INVERSE, WHICH IS EXACTLY THE PROBLEM. Press XORs the
;   button down; a repaint arrives between the two edges - the window dragged,
;   a window above it closing, wm_paint_dmg promoting it - and draws the
;   button UPRIGHT, because a painter knows nothing about an overlay. The
;   release then XORs "back" and inverts a button that was already upright,
;   and it stays inverted for the session. cal_onup's own comment names this,
;   and its answer was to redraw rather than un-XOR, which is this flag by
;   hand at one call site.
;
; Drawn, the state is IDEMPOTENT: a caller that holds "this control is down"
; passes the flag from its W_PAINT as well and every repaint agrees with the
; glass by construction. There is nothing to un-draw and no order to get
; wrong. It costs a fill and a string against one gfx_xor_fill - about 3ms
; more per edge on the field machine at PERFORMANCE.md Part 2's floor, on an
; event a human generates - and it buys a class of bug that cannot happen.
;
; A DOWN button is still drawn by the routine that draws an upright one, so
; its label is centred by the same arithmetic and its frame is in the same
; place. That is the property a separate flip routine cannot have.
; -----------------------------------------------------------------------------
os88ui_bdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                  ; BP = the rect. In a package SS != DS
                                ; (SPEC.md 20.1), so every read below needs a
                                ; ds: override or it addresses the task stack;
                                ; in the kernel it is equally correct and free
    test di, OS88UI_DOWN        ; DOWN implies the fill and picks its colour:
    jz .fillck                  ; black, and the label goes white below
    test di, OS88UI_DIS         ; ...unless it is greyed, in which case it is
    jnz .fillw                  ; not going to fire and must not say it will -
                                ; but it STILL erases, because the ground it is
                                ; being drawn over is a pressed button
    UI_BLACK
    jmp short .fill
.fillck:
    test di, OS88UI_FILL
    jz .pen
.fillw:
    UI_WHITE
.fill:
    mov ax, [ds:bp+0]
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    inc ax                      ; the interior, one pixel in from the frame
    inc bx
    dec cx
    dec dx
    UI_FILL
.pen:
    test di, OS88UI_DIS
    jz .live
    stc                         ; CF = 1: CDGRAY *and* [gfx_dis] together,
    jmp short .set              ; which is SPEC.md 47 rule 1 in one call
.live:
    clc
.set:
    UI_PEN
    test di, OS88UI_DIS         ; a disabled control keeps CDGRAY whatever ink
    jnz .rect                   ; it asked for - state outranks decoration
    test di, OS88UI_INK
    jz .rect
    mov ax, di                  ; AL = the ink, out of DI's high byte
    mov al, ah
    xor ah, ah
    UI_INK
.rect:                          ; ...and the FRAME is drawn with that pen. A
                                ; down button's frame is therefore still black
                                ; (DOWN sets no ink), which is what keeps the
                                ; control the same size and in the same place
                                ; whichever state it is in
    mov ax, [ds:bp+0]
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    UI_FRAME
    test di, OS88UI_DEF
    jz .label
    dec ax                      ; the default ring, 2px outside the frame -
    dec ax                      ; the file dialog's inset, which is the one
    dec bx                      ; that ships (8086: no sub-imm cheaper here)
    dec bx
    inc cx
    inc cx
    inc dx
    inc dx
    UI_FRAME
.label:
    test di, OS88UI_DOWN        ; the caption over a black interior, in white -
    jz .lwidth                  ; the frame above is already drawn, so this
    test di, OS88UI_DIS         ; changes the pen for the STRING alone. Greyed
    jnz .lwidth                 ; wins here as it does everywhere else
    UI_WHITE
.lwidth:
    UI_WIDTH                    ; AX = the label's width in px; SI intact
    mov cx, [ds:bp+4]
    sub cx, [ds:bp+0]
    inc cx                      ; CX = the button's width
    sub cx, ax
    jns .pad                    ; a label WIDER than its button would make the
    xor cx, cx                  ; halved padding a huge unsigned number and put
.pad:                           ; the text off screen entirely - fdlg_blabel's
    shr cx, 1                   ; guard, kept because it is three bytes
    add cx, [ds:bp+0]           ; ...centred
    mov dx, [ds:bp+6]
    sub dx, [ds:bp+2]
    inc dx                      ; DX = the height
    sub dx, 8                   ; a glyph cell is 8 rows
    shr dx, 1
    add dx, [ds:bp+2]

    ; --- OPAQUE WHERE THE INTERIOR IS OURS, transparent where it is not ----
    ; SPEC.md 6.6's rule wants the run, and the one thing that stopped it is
    ; above: a button with neither DOWN nor FILL never fills its interior, so
    ; the ground under these cells belongs to whatever drew it and an opaque
    ; run would repaint it. Every other shape DID fill - black for a pressed
    ; button, white for a filled or a greyed-pressed one - so the paper is
    ; known and the pair can be named.
    ;
    ; THE GREYED CASE NEEDS NOTHING HERE. font_run reads [gfx_dis] itself,
    ; substitutes [gfx_disink] for the ink it was handed and lays SPEC.md 47
    ; rule 1's checkerboard over the result - on EVERY adapter, which the
    ; transparent path does only on a 1bpp one. That is what lets a window
    ; declare its content two colours and still say which of its controls are
    ; dead (SPEC.md 11.96.17.1).
    test di, OS88UI_DOWN | OS88UI_FILL
    jz .xparent
    mov al, CBLACK              ; AL = ink, AH = paper (SPEC.md 6.1)
    mov ah, CWHITE
    test di, OS88UI_INK
    jz .rdown
    mov bx, di                  ; ...or the caller's, out of DI's high byte -
    mov al, bh                  ; BX is free, BP having taken the rect
.rdown:
    test di, OS88UI_DOWN
    jz .run
    test di, OS88UI_DIS
    jnz .run                    ; greyed outranks pressed and .fillw made the
    mov al, CWHITE              ; interior WHITE, so the pair above stands
    mov ah, CBLACK              ; pressed: white on the black fill
.run:
    UI_RUN
    jmp short .lpen
.xparent:
    UI_STR
.lpen:
    clc
    UI_PEN                      ; ...and the pen back, always
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%else
; -----------------------------------------------------------------------------
; os88ui_bdraw1 - the ONE-WRITE drawing body (SPEC.md 13.8.9), OS88UI_BIMG only
; in:  BX = rect {x1,y1,x2,y2} inclusive, SI = the label - a NUL string, or an
;      OSAPI_ICON_DRAW record when DI has OS88UI_IMG - DI = OS88UI_* flags;
;      caller holds the gfx lock
; out: nothing; all registers preserved
;
; os88ui_bdraw's picture, reached by a different road: NO PIXEL IS WRITTEN
; TWICE. The plain body fills the whole interior and then letters the caption
; over it, so every cell the caption covers goes to the ground colour and then
; to its final colour - the fill-then-letter pair SPEC.md 6.1 exists to remove,
; and the one DRAWING RULE 1 at the top of this file names. It is invisible in
; a screenshot and it is a flash on a 4.77 MHz machine, and a button that is
; PRESSED goes through it on both edges.
;
; So the interior is cut in two: the caption's (or picture's) own rect, which
; is drawn OPAQUE - font_run lays ground and glyph in one pass, and a picture
; with a full mask does the same through the sprite pass - and the RING around
; it, filled in the ground colour in at most four strips. The frame is outside
; both. Every pixel of the button is written exactly once, in its final colour.
;
; What it costs is calls: up to four fills where there was one, so ~2 ms more
; per button on the field machine (PERFORMANCE.md's ~756 us floor), on an
; event a human generates. That is why it is an OPT-IN and not os88ui_bdraw's
; replacement - the owner's rule for this library is that a package which does
; not ask for a capability does not carry it.
;
; A PICTURE THE CLIP CUTS draws its ground and not its ink: the sprite pass
; clips a shape WHOLE (SPEC.md 25.6), so a half-covered picture would leave
; its rect holding whatever was there. Filling it in the ground colour keeps
; the button a button; uncovering it is a W_PAINT, which draws the picture.
; -----------------------------------------------------------------------------
os88ui_bdraw1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                  ; BP = the rect, read through ds: (SS != DS)

    ; --- the pair: AL = ink, AH = ground -----------------------------------
    mov ax, CBLACK | (CWHITE << 8)
    test di, OS88UI_INK
    jz .down
    mov bx, di
    mov al, bh                  ; the caller's ink, out of DI's high byte
.down:
    test di, OS88UI_DIS         ; DISABLED WINS over both (47 rule 1): a greyed
    jz .dn                      ; control is never drawn pressed, and its ink
    mov al, CDGRAY              ; is the grey
    jmp short .pair
.dn:
    test di, OS88UI_DOWN
    jz .pair
    mov ax, CWHITE | (CBLACK << 8)
.pair:
    mov [os88ui_bk], ax

    ; --- the inner rect: the caption's or the picture's, centred ------------
    test di, OS88UI_IMG
    jz .txt
    mov al, [si+1]              ; rows
    xor ah, ah
    mov cx, 16                  ; one word a row: 16 wide
    jmp short .cent
.txt:
    UI_WIDTH                    ; AX = the caption's width; SI intact
    mov cx, ax
    mov ax, 8                   ; ...and a cell is 8 rows
.cent:                          ; CX = w, AX = h
    mov dx, [ds:bp+4]
    sub dx, [ds:bp+0]
    inc dx
    sub dx, cx
    jns .xp
    xor dx, dx                  ; wider than the button: flush left
.xp:
    shr dx, 1
    add dx, [ds:bp+0]
    mov [os88ui_ir+0], dx
    add dx, cx
    dec dx
    mov [os88ui_ir+4], dx
    mov dx, [ds:bp+6]
    sub dx, [ds:bp+2]
    inc dx
    sub dx, ax
    jns .yp
    xor dx, dx
.yp:
    shr dx, 1
    add dx, [ds:bp+2]
    mov [os88ui_ir+2], dx
    add dx, ax
    dec dx
    mov [os88ui_ir+6], dx
    ; clamped INTO the interior, so the ring below can never be asked for a
    ; strip of negative size - a caption wider than its button is the caller's
    ; error and is drawn flush left, as os88ui_bdraw draws it
    mov ax, [ds:bp+0]
    inc ax
    cmp [os88ui_ir+0], ax
    jge .c1
    mov [os88ui_ir+0], ax
.c1:
    mov ax, [ds:bp+2]
    inc ax
    cmp [os88ui_ir+2], ax
    jge .c2
    mov [os88ui_ir+2], ax
.c2:
    mov ax, [ds:bp+4]
    dec ax
    cmp [os88ui_ir+4], ax
    jle .c3
    mov [os88ui_ir+4], ax
.c3:
    mov ax, [ds:bp+6]
    dec ax
    cmp [os88ui_ir+6], ax
    jle .c4
    mov [os88ui_ir+6], ax
.c4:

    ; --- the ring, in the ground colour --------------------------------------
    mov al, [os88ui_bk+1]
    UI_INK
    mov ax, [ds:bp+0]           ; top strip: the full interior width, above
    inc ax                      ; the inner rect
    mov bx, [ds:bp+2]
    inc bx
    mov cx, [ds:bp+4]
    dec cx
    mov dx, [os88ui_ir+2]
    dec dx
    cmp dx, bx
    jl .nt
    UI_FILL
.nt:
    mov bx, [os88ui_ir+6]       ; bottom strip
    inc bx
    mov dx, [ds:bp+6]
    dec dx
    cmp dx, bx
    jl .nb
    UI_FILL
.nb:
    mov bx, [os88ui_ir+2]       ; left and right: the inner rect's own rows
    mov dx, [os88ui_ir+6]
    mov cx, [os88ui_ir+0]
    dec cx
    cmp cx, ax
    jl .nl
    UI_FILL
.nl:
    mov ax, [os88ui_ir+4]
    inc ax
    mov cx, [ds:bp+4]
    dec cx
    cmp cx, ax
    jl .nr
    UI_FILL
.nr:

    ; --- the frame, and the default ring: os88ui_bdraw's own pen rules -------
    test di, OS88UI_DIS
    jz .live
    stc
    jmp short .set
.live:
    clc
.set:
    UI_PEN
    test di, OS88UI_DIS
    jnz .rect
    test di, OS88UI_INK
    jz .rect
    mov ax, di
    mov al, ah
    UI_INK
.rect:
    mov ax, [ds:bp+0]
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    UI_FRAME
    test di, OS88UI_DEF
    jz .inner
    dec ax
    dec ax
    dec bx
    dec bx
    inc cx
    inc cx
    inc dx
    inc dx
    UI_FRAME

    ; --- the inner rect, opaque ---------------------------------------------
.inner:
    test di, OS88UI_IMG
    jnz .img
    mov cx, [os88ui_ir+0]
    mov dx, [os88ui_ir+2]
    mov ax, [os88ui_bk]         ; AL = ink, AH = ground; a greyed caption has
    UI_RUN                      ; the disabled pen set and font_run screens it
    jmp short .lpen
.img:
    clc
    UI_PEN                      ; a picture's pair is its own, never the grey
    mov ax, [os88ui_ir+0]       ; pen's
    mov bx, [os88ui_ir+2]
    mov cx, [os88ui_ir+4]
    mov dx, [os88ui_ir+6]
    UI_CLIPT
    jnc .whole
    mov al, [os88ui_bk+1]       ; cut by the clip: its ground, not its ink
    UI_INK
    mov ax, [os88ui_ir+0]
    UI_FILL
    jmp short .lpen
.whole:
    mov ax, [os88ui_bk]
    xchg al, ah                 ; AL = the MASK pass (ground), AH = the DATA
    UI_IPEN                     ; pass (ink) - SPEC.md 25.6's pair
    mov cx, [os88ui_ir+0]
    mov dx, [os88ui_ir+2]
    UI_ICON                     ; CF = 1 is a record wider than a word or
                                ; taller than 16: the caller's error, drawn as
                                ; nothing
.lpen:
    clc
    UI_PEN
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_ir: dw 0, 0, 0, 0        ; the inner rect, and the pair - DATA, inline,
os88ui_bk: dw 0                 ; for os88ui_armw's reason
%endif                          ; OS88UI_BIMG

; -----------------------------------------------------------------------------
; os88ui_bhit - is the point inside this button?
; in:  BX = rect {x1,y1,x2,y2} inclusive, CX = x, DX = y
; out: CF = 0 inside, CF = 1 outside; all registers preserved
;
; CX/DX because that is how W_ONCLICK and W_ONMOUSEUP hand you a point
; (SPEC.md 13/13.7), so a call site needs no shuffle. [bx+n] is DS-relative
; already, which is why the rect stays in BX here and moves to BP only in the
; drawing routine, where BX is one of gfx_frame's own arguments.
; -----------------------------------------------------------------------------
os88ui_bhit:
    cmp cx, [bx+0]
    jl .no
    cmp cx, [bx+4]
    jg .no
    cmp dx, [bx+2]
    jl .no
    cmp dx, [bx+6]
    jg .no
    clc
    ret
.no:
    stc
    ret

; --- OS88UI_NOGLYPH - the button WITHOUT the check-box/radio glyph ----------
; An OPT-OUT, OS88UI_BARONLY's shape one control along: the glyph's body and
; its three shape routines are ~300 bytes every button user carries, and a
; package that draws no check box and no radio carries them for nothing -
; Tracker, whose buttons are pictures and captions only (SPEC.md 13.8.9).
; Unset, every consumer is byte-identical.
%ifdef OS88UI_NOGLYPH
 %ifdef OS88UI_CHK
  %error "OS88UI_CHK draws os88ui_glyph, which OS88UI_NOGLYPH takes out"
 %endif
 %ifdef OS88UI_RAD
  %error "OS88UI_RAD draws os88ui_gring/os88ui_gdot, which OS88UI_NOGLYPH takes out"
 %endif
 %ifdef OS88UI_KERNEL
  %error "OS88UI_NOGLYPH is a PACKAGE's: the Control Panel draws both glyphs"
 %endif
%else
; =============================================================================
; The CHECK BOX and the RADIO BUTTON (SPEC.md 31.2)
;
; 12x12, one word a row, bit 15 = the leftmost pixel - menu_logo_bmp's
; convention, and the low 4 bits of every word are unused. Four bitmaps and
; one body, which is what the Control Panel has always had; what it did not
; have is a way for a PACKAGE to draw the same control, so an app wanting a
; checkbox drew its own and looked like a different operating system.
;
; They are a PAIR because a radio and a check are the same widget with
; different pictures - same size, same white box, same walk - and splitting
; them would be two bodies that have to agree about all three.
; =============================================================================

OS88UI_GRADIO equ 0             ; open ring / ring with a filled centre dot
OS88UI_GCHECK equ 2             ; open square / square with a cross
OS88UI_GON    equ 1             ; ...or'd into either: the SET state
OS88UI_GDOWN  equ 4             ; ...and the PRESSED look (SPEC.md 13.8): the
                                ; box black and the picture white, which is
                                ; os88ui_btn's down state one widget over and
                                ; drawn the same way rather than XOR-ed, for
                                ; the same reason. It rides in AL beside the
                                ; index because the index is masked to 2 bits
                                ; (`and si, 3`) and bit 2 was free; the
                                ; disabled flag has AH to itself and keeps it
OS88UI_GW     equ 12            ; both are 12 x 12
; ...and the size os88ui_glyph's masked-sprite pass is allowed to be. The
; record it hands the kernel is `db 1, OS88UI_GW`, and icon_draw_x refuses
; ww = 0, ww > 1, rows = 0 or rows > ICO_STAGE_H - so the ONLY one of the four
; a change here can trip is the last, and this is it, checked rather than
; reasoned about. ICO_STAGE_H is the kernel's and is not published to a
; package (SPEC.md 25.6.1), so THE GUARD LIVES IN kernel/icons.inc, beside the
; constant it reads. It cannot live here: kernel.asm includes fdlg.inc - and so
; this file - one line BEFORE icons.inc, so an `%ifdef ICO_STAGE_H` written
; here is false on every build and guards nothing. That was tried.

; -----------------------------------------------------------------------------
; os88ui_glyph - one check box or radio button, over its own white box
; in:  CX = x, DX = y (top-left, screen px), AL = OS88UI_G*,
;      AH non-zero = disabled; caller holds the gfx lock
; out: nothing; all registers preserved, and the pen is left LIVE
;
; THE FLAG RIDES IN AH rather than in DI, which is the one place this file
; departs from os88ui_btn's shape. A glyph has exactly one flag, and every
; Control Panel page is already holding the pane's left edge in DI - so DI
; would cost a push and a pop at all six call sites to carry one bit that
; fits beside the index for free.
;
; THE WHITE BOX IS UNCONDITIONAL, where the button's is a flag. A radio's
; whole job is to be redrawn in place when the selection moves, so there is
; no caller for whom the erase is waste - and drawing one bitmap over
; another without it is the same defect the button's OS88UI_FILL note
; describes, one layer down.
;
; It is not cheap: MEASURED at 4.3 to 6.4 ms of the field machine's time
; (docs/reports/GLYPH-AND-LINE-COST-2026-09-10.md, taken on a Hercules 5150
; with tests/glyphbn). The 35-50ms that stood here was 44-64 gfx_pixel calls
; at PERFORMANCE.md Part 2's ~756us - which was the CLIPPED fallback's
; arithmetic, and never what the normal path did. Draw the ROW that changed,
; never the page.
;
; -----------------------------------------------------------------------------
; os88ui_gdn - should this glyph be drawn PRESSED?
; in:  DI = the banked argument word (AL's flags, AH's disabled byte)
; out: ZF = 1 draw it down; all registers preserved
;
; A predicate rather than two open-coded tests because it is asked twice - once
; about the BOX and once about the PICTURE - and the two must never disagree,
; which is a white glyph on a white box (invisible) or a black one on black.
; Disabled outranks it, as it outranks OS88UI_INK on the button (47 rule 1).
; -----------------------------------------------------------------------------
os88ui_gdn:
    push ax
    mov ax, di
    test ax, 0xFF00             ; disabled: never drawn pressed
    jnz .no
    and ax, OS88UI_GDOWN
    cmp ax, OS88UI_GDOWN        ; ZF = 1 exactly when the bit is set
    pop ax                      ; (pop writes no flags)
    ret
.no:
    or ax, ax                   ; AX's high byte is set here, so ZF = 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; --- THE SHAPES, drawn ONCE and shared with os88ui_rad ------------------------
; SPEC.md 13.17.1 settled what a radio looks like here, and there must not be a
; second answer: a 12x12 rounded box - a rectangle with one pixel off each
; corner - and a 6x6 rounded dot. os88ui_rad draws a GROUP of them with labels
; and os88ui_glyph draws ONE bare one at CX/DX, so they share the two routines
; below rather than each carrying a copy that can drift.
;
; Both take CX = x, DX = y and draw in whatever the pen is. Both preserve every
; register, so a caller can lay several without re-loading the position.

; os88ui_gring - the 12x12 rounded box: FOUR RUNS, never a frame.
; A frame lights the four corners, which would then have to be knocked out in
; the ground colour - four more fills AND two more pen changes for the same
; shape. Runs that stop one pixel short are cheaper and every one of them is
; horizontal or vertical (SPEC.md 39.4: a diagonal reads as noise on 1bpp).
os88ui_gring:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cx                  ; DI = x, SI = y - and THAT is the whole trick:
    mov si, dx                  ; UI_FILL wants AX,BX,CX,DX, so a position held
                                ; in either of those has to be pushed and
                                ; popped around all four calls. Held here it is
                                ; four plain `mov`s a run and no stack at all
    lea ax, [di+1]
    lea cx, [di+OS88UI_GW-2]
    mov bx, si
    mov dx, si
    UI_FILL                     ; top
    lea ax, [di+1]
    lea cx, [di+OS88UI_GW-2]
    lea bx, [si+OS88UI_GW-1]
    mov dx, bx
    UI_FILL                     ; bottom
    mov ax, di
    mov cx, di
    lea bx, [si+1]
    lea dx, [si+OS88UI_GW-2]
    UI_FILL                     ; left
    lea ax, [di+OS88UI_GW-1]
    mov cx, ax
    lea bx, [si+1]
    lea dx, [si+OS88UI_GW-2]
    UI_FILL                     ; right
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; os88ui_gdot - the 6x6 rounded dot, centred: 1 + 2 + 6 + 2 + 1 is the box's 12
os88ui_gdot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cx
    mov si, dx
    lea ax, [di+4]
    lea cx, [di+7]
    lea bx, [si+3]
    mov dx, bx
    UI_FILL                     ; the dot's top run
    lea ax, [di+3]
    lea cx, [di+8]
    lea bx, [si+4]
    lea dx, [si+7]
    UI_FILL                     ; ...its body
    lea ax, [di+4]
    lea cx, [di+7]
    lea bx, [si+8]
    mov dx, bx
    UI_FILL                     ; ...and its bottom run
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_glyph - ONE control glyph, drawn with FILLS (SPEC.md 13.15.1)
;
; It used to be four 12x12 BITMAPS put down through the masked sprite pass,
; with a per-pixel fallback for a control a clip fragment cut. All of that is
; gone: the pictures, the sprite record, the per-row dither compose and the
; 45-to-65-call `.gpix` arm.
;
; WHAT THAT BUYS, per copy and in twenty-three of them (the kernel's .cold and
; twenty-two package images): the four pictures, the record and its staging,
; and most of the body. What it COSTS is calls - eight for a set radio against
; the sprite pass's one - and that trade was the owner's to take: a control
; glyph is drawn once and then sits there until someone interacts with it.
;
; THE CLIPPED CASE IS WHERE IT WINS OUTRIGHT. ico_core clips an icon WHOLE
; (SPEC.md 25.6), so a cut control used to cost 45-65 drawing calls and 24-34
; ms. A fill clips per pixel, so there is no second path here at all - one cost
; instead of two, and the bad one is gone with the code that had it.
;
; THE BOX IS STILL CLEARED UNCONDITIONALLY, which is this routine's one
; deliberate departure from SPEC.md 13.14.6 rule 1 and is a published contract
; twenty-two packages rest on: a glyph is redrawn IN PLACE when a selection
; moves, and its caller is not required to have laid a clean ground. At 12x12
; with the shape one call behind it that interval is under a millisecond,
; where the row-wide fill 13.17.4 removed was about four. os88ui_rad is the one
; that needs no clear at all, because it owns its group and can redraw the DOT.
;
; in:  CX = x, DX = y (top-left, screen px), AL = OS88UI_G*,
;      AH non-zero = disabled; caller holds the gfx lock
; out: nothing; all registers preserved, and the pen is left LIVE
; -----------------------------------------------------------------------------
os88ui_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov di, ax                  ; DI = the whole argument: AX is about to be a
                                ; rect corner for the box, so the DISABLED byte
                                ; cannot stay in AH. It did in the first
                                ; version, `mov ax, cx` overwrote it with the
                                ; high byte of an x coordinate, and every glyph
                                ; on every page came out GREYED - which
                                ; assembles, boots, and draws a perfectly
                                ; plausible ring
    mov bp, cx                  ; BP = x, SI = y, held clear of UI_FILL's four
    mov si, dx                  ; registers (os88ui_gring's trick). VALUES, not
                                ; addresses - `lea` computes and never reads,
                                ; so SS not being DS does not enter into it

    ; --- the box: white, or BLACK when the control is pressed ---------------
    UI_WHITE
    call os88ui_gdn
    jnz .box
    UI_BLACK
.box:
    mov ax, bp
    mov bx, si
    lea cx, [bp+OS88UI_GW-1]
    lea dx, [si+OS88UI_GW-1]
    UI_FILL

    ; --- the pen for the shape: SPEC.md 47 rule 1, then inverted if pressed --
    test di, 0xFF00             ; the disabled byte out of the bank, never out
    jz .live                    ; of AH
    stc
    jmp short .set
.live:
    clc
.set:
    UI_PEN
    call os88ui_gdn
    jnz .shape                  ; a PRESSED glyph is a black box with the
    UI_WHITE                    ; shape in white over it

.shape:
    mov cx, bp
    mov dx, si
    test di, OS88UI_GCHECK
    jnz .check

    call os88ui_gring           ; --- RADIO: the rounded box...
    test di, OS88UI_GON
    jz .done
    call os88ui_gdot            ; ...and the dot when it is set
    jmp short .done

.check:                         ; --- CHECK: a square frame and a square mark,
    mov ax, bp                  ; which is os88ui_chk's own look (SPEC.md
    mov bx, si                  ; 13.15) and SPEC.md 13.16.2.1's finding - "a
    lea cx, [bp+OS88UI_GW-1]    ; solid square, which reads on one bit as a
    lea dx, [si+OS88UI_GW-1]    ; tick does not". os88ui_chk CALLS this routine
    UI_FRAME                    ; rather than drawing its own (13.15.2), so
    test di, OS88UI_GON         ; there is no second copy to keep in step and
    jz .done                    ; nothing shared has to be carried by the
    lea ax, [bp+3]              ; twenty-two packages that never opt into a
    lea bx, [si+3]              ; check box
    lea cx, [bp+OS88UI_GW-4]
    lea dx, [si+OS88UI_GW-4]
    UI_FILL

.done:
    clc
    UI_PEN                      ; the pen back, always
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif                          ; OS88UI_NOGLYPH
%endif                          ; OS88UI_NOBTN
%ifdef OS88UI_KERNEL
section .cold
%endif

; --- the click machinery: every PACKAGE, and kern_BIG ------------------------
; A package always has it - it is four small routines and a word, and the
; press/release pair is the SDK's own idiom. The kernel has it only in the big
; build, because kern_small's dialogs still fire on the press (SPEC.md 13.8.3
; is kern_big's, docs/plans/completed/UIHELPERS-PLAN.md 18) and 40 bytes of .cold plus two of
; .bss is a whole 512-byte rung on the guard the 128KB machine lives under -
; measured, not assumed: making these unconditional cost kern_small exactly
; that and nothing else.
%ifdef OS88UI_KERNEL
  %ifdef KERN_BIG
    %define OS88UI_ARM
  %endif
%elifndef OS88UI_NOBTN          ; ...and NOT under OS88UI_NOBTN: os88ui_bfind
  %define OS88UI_ARM             ; walks a button table by calling os88ui_bhit,
%endif                           ; which the block above has just taken out
%ifdef OS88UI_ARM

; --- ...and it is BOTH WORLDS' now -------------------------------------------
; This block used to sit inside `%ifndef OS88UI_KERNEL`, with a note saying
; the day a kernel dialog wanted a release-fired button it was this gate that
; would move and not a second copy that would get written. That day is SPEC.md
; 13.8.3: the Standard File dialog and the Control Panel fire on the release
; now, and they use these four rather than a private arm each.
;
; The BODIES are unconditional. Only the WORD differs, and it has to: a
; package declares its bss as one total with equ offsets so two bytes of image
; is cheaper than a convention, while in the kernel this file is assembled
; into `.cold` - and a word declared there and reached through DS is NOT that
; word (SPEC.md 2.6 rule 1), which is the trap os88ui_krect's own comment
; records costing eight bytes through the middle of sch_isr.

%ifndef OS88UI_KERNEL              ; the kernel walks its own rects (fdlg_bhit, fm_bhit)
; -----------------------------------------------------------------------------
; os88ui_bfind - which button of a table is the point in?
; in:  BX = an array of 4-word rects, AX = how many, CX = x, DX = y
; out: AX = the index PLUS ONE, or 0 for none; all other registers preserved
;
; Plus one so that 0 means "no button", which is the same sentinel
; os88ui_arm / os88ui_fire use - so a whole press-and-release cycle is
; `bfind -> arm` then `fire`, `bfind`, compare, with no separate "was
; anything hit" flag to keep in step.
; -----------------------------------------------------------------------------
os88ui_bfind:
    push bx
    push si
    push di
    mov si, ax                  ; SI = the count
    xor di, di                  ; DI = the index
.next:
    cmp di, si
    jae .none
    call os88ui_bhit
    jnc .hit
    add bx, 8
    inc di
    jmp short .next
.hit:
    mov ax, di
    inc ax
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_arm / os88ui_fire - the press/release pair (SPEC.md 13.7)
;
;   os88ui_arm    in: AX = the control the PRESS landed on (0 = none)
;   os88ui_fire   out: AX = what was armed (0 = nothing), and it is CLEARED
;
; Call arm from W_ONCLICK and fire from W_ONMOUSEUP, then compare fire's
; answer with a fresh os88ui_bfind at the release point: equal and non-zero
; means the user pressed and released on the same control, which is the whole
; gesture. Anything else is a cancel and draws nothing.
;
; fire CLEARS, which is what makes a missed release harmless: the kernel
; guarantees exactly one W_ONMOUSEUP per W_ONCLICK (SPEC.md 13.7), so there is
; no path that can leave this set and no fallback for a package to forget.
;
; The word is DATA, not bss: a package declares its bss as one total with
; equ offsets (OS88_BSS), and a shared include cannot reserve part of that
; without the package agreeing about where. Two bytes of image is cheaper
; than a convention.
%endif                          ; OS88UI_KERNEL: os88ui_bfind is a package's
; -----------------------------------------------------------------------------
os88ui_arm:
    mov [os88ui_armw], ax
    ret

os88ui_fire:
    mov ax, [os88ui_armw]
    mov word [os88ui_armw], 0
    ret

; -----------------------------------------------------------------------------
; os88ui_armed - what is armed, WITHOUT spending it
; out: AX = the control the press landed on (0 = none); nothing else touched
;
; The tracking edge's reader (SPEC.md 13.8.2). W_ONDRAG arrives between the
; press and the release, so it has to ask what was armed and leave it armed -
; which os88ui_fire cannot do, that being the whole of what makes a missed
; release harmless. Two names over one word rather than a flag argument,
; because "peek" and "take" are what the two callers actually mean and a
; caller that got the flag backwards would spend the arm from a drag and then
; find nothing at the release.
; -----------------------------------------------------------------------------
os88ui_armed:
    mov ax, [os88ui_armw]
    ret

%ifndef OS88UI_KERNEL
os88ui_armw: dw 0               ; a package's: DATA, inline, for the reason
%endif                          ; above. The kernel's is in .bss at the bottom

; =============================================================================
; THE GESTURE (SPEC.md 13.7, 13.8, 20.5.1.3)
;
; Four entries, and between them they are every obligation a caller used to
; owe by hand. os88ui_bfind, os88ui_arm, os88ui_fire and os88ui_armed above
; are what they are made of and are the control's INTERNALS now - there is no
; longer a reason for a package to hold them, and a package that does is
; writing the composition again.
;
; The shape is the drop-down's exactly (SPEC.md 13.14), which is the point:
; press, drag, up, over one record the caller declares. The button was the
; only major control in this file that did not have it.
; =============================================================================

; -----------------------------------------------------------------------------
; os88ui_btninit - adopt a window, and INSTALL THE GESTURE'S TWO SLOTS
;
; A PACKAGE'S ONLY (%ifndef OS88UI_KERNEL below). OSAPI_WM_ONMOUSEUP and
; OSAPI_WM_ONDRAG are API-table slots that os88api.inc publishes to programs;
; inside the kernel they are not symbols at all, and a kernel window reaches
; the same two callbacks through wm_pkgcall's near dispatch on a W_SEG of 0
; with a .text thunk apiece (SPEC.md 13.8.3). So the kernel sets its record's
; OS88UI_BT_WIN itself and installs nothing here.
; in:  BX = the record, AX = the window (after wm_create),
;      SI = your W_ONMOUSEUP proc, DI = your W_ONDRAG proc,
;      DX = your own W_ONCLICK proc, or 0 if the buttons are all this window
;      has (near ptrs, this segment)
; out: nothing; all registers preserved
;
; THE INSTALL IS THE LIBRARY'S JOB AND THAT IS THE MOST IMPORTANT LINE IN THIS
; FILE. W_ONMOUSEUP and W_ONDRAG are deliberately not template words (SPEC.md
; 13.7), so a package that never installed them got NO ERROR of any kind - it
; got a button that fired on the press, looked finished, and photographed
; identically to a correct one. That is how twenty-five call sites went wrong.
;
; IT TAKES YOUR TWO PROCS RATHER THAN INSTALLING DEFAULTS OF ITS OWN, and that
; is deliberate to the point of being the design. A version of this installed
; a pair of `ret` thunks so that the "common case needed no proc at all" - and
; that version reintroduces the whole defect one layer in: a package that
; installs its own handler BEFORE calling this would have it silently
; overwritten by a no-op, and a package that never writes one at all gets a
; button that does nothing, which is the same "no error of any kind" with a
; different symptom. Passing them means the caller cannot fail to have thought
; about them. What this removes is the REMEMBERING, not the writing.
;
; THE DRAG SLOT'S CF IS IGNORED ON PURPOSE. OSAPI_WM_ONDRAG answers CF = 1 on
; kern_small (SPEC.md 13.8.2), where the record simply never sees a drag edge:
; the press arms and draws down, the release fires or cancels, and the control
; does not un-draw while the pointer slides off it. That is strictly better
; than what shipped before and is exactly what the kernel's own dialogs do
; there, so a package must not have to ask - and one that did ask would be
; writing a kern_small special case into every application in the tree.
; -----------------------------------------------------------------------------
%ifndef OS88UI_NOGEST
; -----------------------------------------------------------------------------
; THE INSTALL SIDE - 226 bytes, and twenty-three of thirty-one packages never
; reach it (SPEC.md 20.5.1.3.4). `OS88UI_NOGEST` drops it for a package that
; drives its controls itself through os88ui_bfind + arm/fire.
;
; IT REMOVES THE SYMBOLS RATHER THAN ASKING. 20.5.1.3.3's whole finding is
; that a package routing its own press is invisible to anything static, so a
; gate a package could set and then go on calling os88ui_btninit would re-open
; that door. Setting this one and calling it does not assemble.
; -----------------------------------------------------------------------------
%ifndef OS88UI_KERNEL
os88ui_btninit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [bx+OS88UI_BT_WIN], ax
    mov [bx+OS88UI_BT_ONCLK], dx
    mov si, [os88ui_btlist]     ; ...and onto the package's list of records,
    mov [bx+OS88UI_BT_NEXT], si ; which os88ui_btnclick walks to find the one
    mov [os88ui_btlist], bx     ; belonging to the window it was handed
    pop si
    mov cx, ax                  ; CX = the window; BX is the slot call's arg
    mov bx, cx
    mov ax, si
    call OSAPI_WM_ONMOUSEUP
    mov bx, cx
    mov ax, di
    call OSAPI_WM_ONDRAG        ; CF = 1 on kern_small: deliberately unread
    mov bx, cx
    mov ax, os88ui_btnclick     ; **AND THE PRESS IS OURS** (SPEC.md
    call OSAPI_WM_ONCLICK       ; 20.5.1.3.3): the library sees it first and
    pop dx                      ; chains to OS88UI_BT_ONCLK when it was not a
    pop cx                      ; button's, so no click path can skip the
    pop bx                      ; buttons
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_btnclick - W_ONCLICK, the LIBRARY's (SPEC.md 20.5.1.3.3)
; in:  CX = x, DX = y (SCREEN), SI = the window; installed by os88ui_btninit
; out: nothing
;
; THE PRESS REACHES THE BUTTONS BEFORE IT REACHES THE PACKAGE, and that is the
; whole point. A package used to route its own press into os88ui_btnpress, and
; a package with more than one click path had to route them all: ArtfulType
; converted its modal, left its splash on a private hit test, and shipped two
; buttons that drew pressed and still acted on the press. Nothing static could
; see it. Now there is nothing to route.
;
; It also fixes the COORDINATE SPACE for good. The record's rects are screen
; coordinates and CX/DX arrive as screen coordinates, so the press is tested
; in the space it was given; the Audio player armed nothing for a release
; because its own handler had already made them content-relative before
; calling in.
;
; The record is found by walking this package's list on OS88UI_BT_WIN. A list
; rather than a table because Sheet has five dialog windows and a table is a
; limit somebody eventually exceeds.
; -----------------------------------------------------------------------------
os88ui_btnclick:
    push ax
    push bx
    mov bx, [os88ui_btlist]
.find:
    or bx, bx
    jz .out                     ; no record for this window: nothing of ours
    cmp si, [bx+OS88UI_BT_WIN]
    je .got
    mov bx, [bx+OS88UI_BT_NEXT]
    jmp short .find
.got:
    call os88ui_btnpress        ; AX = the button armed, 0 = not one of ours
    or ax, ax
    jnz .out
    mov ax, [bx+OS88UI_BT_ONCLK]
    or ax, ax
    jz .out                     ; the package has no click work of its own
    xchg ax, bx                 ; ...otherwise it gets the press in exactly
    call bx                     ; the environment the kernel handed us - CX,
.out:                           ; DX and SI are untouched all the way down
    pop bx
    pop ax
    ret

os88ui_btlist: dw 0             ; the head of this package's record list -
                                ; DATA, inline, for os88ui_armw's reason
%endif                          ; OS88UI_KERNEL
%endif                          ; OS88UI_NOGEST

; -----------------------------------------------------------------------------
; os88ui_btnpress - W_ONCLICK: arm whatever the press landed on
; in:  BX = the record, CX = x, DX = y (SCREEN); the gfx lock is held
; out: AX = the button's index PLUS ONE, or 0 for "not ours - hand the press
;      on"; all other registers preserved
;
; The rects it walks are the ones your painter filled THIS pass, so recompute
; them from OSAPI_WM_CONTENT before you call this if anything since the last
; paint could have moved the window.
; -----------------------------------------------------------------------------
%ifndef OS88UI_NOGEST
%ifndef OS88UI_KERNEL          ; os88ui_btnclick is their only caller, and it is a package's
os88ui_btnpress:
    push bx
    push si
    push di
    push bx
    mov ax, [bx+OS88UI_BT_N]
    mov bx, [bx+OS88UI_BT_RECTS]
    call os88ui_bfind           ; AX = index + 1, 0 = none
    pop bx
    or ax, ax
    jz .out
    call os88ui_arm
    mov [bx+OS88UI_BT_DOWN], ax ; ...and the picture, in the same breath
    call os88ui_btn             ; (AL = index + 1 already)
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_btndrag - W_ONDRAG: the pressed control FOLLOWS THE POINTER
; in:  BX = the record, CX = x, DX = y (SCREEN); the gfx lock is held
; out: nothing; all registers preserved
;
; Down while the pointer is over the control the press landed on, up the
; moment it is not, down again on the way back. That half of the down state is
; the one worth having: it says the gesture is CANCELLED before the user
; commits to it, which is the whole reason press-and-slide-off is a usable way
; to change your mind (SPEC.md 13.8.1).
;
; IT REDRAWS ONLY ON A CHANGE, and that is not tidiness - this edge arrives
; once per mouse packet, and a redraw per packet is PERFORMANCE.md's input
; overrun with a 3.4ms gfx_frame inside it. A pointer sitting still inside the
; control it pressed costs two compares and no pixels.
;
; os88ui_armed and not os88ui_fire: the arm has to survive this and still be
; there for the release. Spending it here would leave the release with nothing
; armed and the control would never fire at all.
; -----------------------------------------------------------------------------
os88ui_btndrag:
    push ax
    push bx
    push si
    push di
    call os88ui_armed           ; AX = what the press armed, still armed
    or ax, ax
    jz .out                     ; the press landed on nothing: nothing to track
    mov di, ax
    push bx
    mov ax, [bx+OS88UI_BT_N]
    mov bx, [bx+OS88UI_BT_RECTS]
    call os88ui_bfind           ; AX = what is under the pointer NOW
    pop bx
    cmp ax, di
    je .want                    ; back on it, or never left
    xor ax, ax                  ; off it: nothing is down
.want:
    cmp ax, [bx+OS88UI_BT_DOWN]
    je .out                     ; NO CHANGE - and this is the common case
    mov si, [bx+OS88UI_BT_DOWN]
    mov [bx+OS88UI_BT_DOWN], ax
    mov ax, si
    call os88ui_btn             ; the one that WAS down, upright again
    mov ax, [bx+OS88UI_BT_DOWN]
    call os88ui_btn             ; ...and the one that is down now
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_btnup - W_ONMOUSEUP: the control FIRES here, or the gesture is off
; in:  BX = the record, CX = x, DX = y (SCREEN, and possibly outside the
;      window entirely); the gfx lock is held
; out: AX = the button that FIRED, index PLUS ONE, or 0 = cancelled;
;      all other registers preserved
;
; The armed control is put back up FIRST, unconditionally, and by REDRAWING
; rather than by undoing anything. That asymmetry with the press is the whole
; of SPEC.md 13.8's drawn-state argument: a repaint arriving between the two
; edges takes the inversion off the glass by itself, and an un-draw would then
; invert a control that was already upright and leave it that way for the
; session.
;
; CX/DX may be outside the window - that is SPEC.md 13.7's contract rather
; than an edge case, and it is what makes a release you slid away from a
; CANCEL instead of a fire. os88ui_bfind answers 0 out there and 0 is what
; this returns.
; -----------------------------------------------------------------------------
os88ui_btnup:
    push bx
    push si
    push di
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax                   ; - one release per press (SPEC.md 13.7), so
    jz .none                    ; there is no stale arm to guard against
    mov di, ax
    mov word [bx+OS88UI_BT_DOWN], 0
    call os88ui_btn             ; upright again, FIRST and whatever happens
    push bx                     ; next - the caller is about to destroy this
    mov ax, [bx+OS88UI_BT_N]    ; window, for all we know
    mov bx, [bx+OS88UI_BT_RECTS]
    call os88ui_bfind           ; AX = the control under the RELEASE
    pop bx
    cmp ax, di
    je .out                     ; pressed and released on the same one: FIRE
.none:
    xor ax, ax                  ; a different one, or none: cancelled, and the
.out:                           ; caller is told nothing happened
    pop di
    pop si
    pop bx
    ret
%endif                          ; OS88UI_KERNEL
%endif                          ; OS88UI_NOGEST
%endif                          ; OS88UI_ARM


%ifdef OS88UI_SCROLL
; =============================================================================
; THE SCROLL BAR (SPEC.md 13.10)
;
; The second shared element, and the argument for it is not the same as
; os88ui_btn's. A button was five bodies that agreed; a scroll bar was FIVE
; PRIVATE IMPLEMENTATIONS OF ONE WIDGET - kernel/files.inc, kernel/fdlg.inc,
; apps/notepad, apps/frotz/zwin.inc and apps/artful/atrend.inc - each a frame
; plus two arrow cells plus a proportional thumb, each with its own arithmetic
; for where the thumb goes, and no two of them the same picture.
;
; IN THE KERNEL IT IS A NET SAVING, which is unusual for a generalisation and
; is why it was costed before it was built: files.inc's bar was 387 bytes and
; fdlg.inc's 288, against ~385 for this plus a setter apiece. The two of them
; also disagreed by a PIXEL in two places - arrow cells 11 rows against 12,
; arrow centre x1+6 against x1+7 - which is exactly the drift a shared element
; exists to end, so this unifies on files.inc's, whose arrow is symmetric in
; its cell where fdlg's sat one row high.
;
; WHAT IT IS NOT is a scroll POLICY. It answers where the parts are and draws
; them; what an arrow does to a view, how far a page is, and whether a click on
; the thumb pages or does nothing are all the caller's - which is why
; os88ui_sbhit reports the thumb as a part of its own rather than folding it
; into one of the pages. files.inc treats that as page-down and fdlg.inc as
; nothing, and both were right about their own window.
;
; THE BLOCK is seven words, and the caller fills it because it has to know all
; seven anyway:
;
;   +0  x1   +2  y1   +4  x2   +6  y2      the bar, ABSOLUTE and INCLUSIVE
;   +8  total    how many rows there are in all
;   +10 fit      how many are visible
;   +12 pos      the first visible row
;
; Everything else is derived, and derived HERE so that a painter and a
; hit-tester cannot hold different opinions about it:
;
;   the two arrow-cell rules      y1+10 and y2-10
;   the arrow glyphs' centre      x1 + (x2-x1)/2, which is x1+6 on a 14px bar
;   the track                     (x1+1, y1+11) .. (x2-1, y2-11)
;
; A 14-pixel bar is what both kernel callers use and what the arrow glyph is
; drawn for (5 rows, widths 1..9), but nothing here requires it: the centre is
; computed and the arrow clips to the cell by construction.
; =============================================================================

OS88UI_SBNONE  equ 0            ; os88ui_sbhit's answers
OS88UI_SBUP    equ 1            ; the up-arrow cell
OS88UI_SBDOWN  equ 2            ; the down-arrow cell
OS88UI_SBPGUP  equ 3            ; the track ABOVE the thumb
OS88UI_SBPGDN  equ 4            ; ...and below it
OS88UI_SBTHUMB equ 5            ; the thumb itself - a part of its own, because
                                ; the two kernel callers disagree about what it
                                ; means and both are right
OS88UI_SBMINH  equ 8            ; the shortest thumb that is still a thumb
OS88UI_SBCELL  equ 10           ; the arrow cell: the rule sits this far in

; -----------------------------------------------------------------------------
; os88ui_sbthumb - the thumb's geometry
; in:  BX = the block
; out: CF = 1 = there is no thumb (everything fits, or the track is too short
;      to hold one); else CF = 0, AX = its top and DX = its height, ABSOLUTE.
;      Every other register preserved.
;
; The proportional-thumb arithmetic both kernel bars already had, once. The
; degenerate-track refusal is files.inc's and fdlg.inc had none, which is the
; safe direction to unify: fdlg's bar is a fixed size and can never reach it.
; -----------------------------------------------------------------------------
os88ui_sbthumb:
    push bx
    push cx
    push si
    mov ax, [ds:bx+8]           ; total
    cmp ax, [ds:bx+10]          ; ...against fit
    jbe .none
    mov si, [ds:bx+2]
    add si, OS88UI_SBCELL+1     ; SI = the track's top
    mov cx, [ds:bx+6]
    sub cx, OS88UI_SBCELL+1
    sub cx, si
    inc cx                      ; CX = the track's height
    cmp cx, OS88UI_SBMINH
    jl .none                    ; shorter than the shortest thumb
    mov ax, [ds:bx+10]          ; fit * track / total = the height
    mul cx
    div word [ds:bx+8]
    cmp ax, OS88UI_SBMINH
    jge .hok
    mov ax, OS88UI_SBMINH
.hok:
    push bp
    mov bp, cx                  ; BP = track - height: the thumb's TRAVEL,
    sub bp, ax                  ; which is what a drag can actually walk
    push ax                     ; ...banked, because the offset needs the mul
    mov ax, [ds:bx+12]          ; pos * travel / (total - fit): pos runs
    mul bp                      ; 0..total-fit and the top runs 0..travel,
    mov bp, [ds:bx+8]           ; end meeting end. The old pos*track/total
    sub bp, [ds:bx+10]          ; form disagreed with its own inverse the
    div bp                      ; moment the MINH clamp above fired, and the
    pop dx                      ; view could never reach the end of a long
    pop bp                      ; listing by the thumb (13.10.5.3)
    add ax, si                  ; ...and AX the top
    add cx, si                  ; clamp: top <= track end - height + 1
    sub cx, dx
    cmp ax, cx
    jle .out
    mov ax, cx
.out:
    clc
    jmp short .done
.none:
    stc
.done:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_sbar - draw the whole bar
; in:  BX = the block; the gfx lock is held
; out: nothing (all registers preserved); leaves the pen BLACK
; -----------------------------------------------------------------------------
os88ui_sbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, bx                  ; DI = the block: BX is every primitive's y1
    UI_BLACK
    mov ax, [ds:di+0]           ; --- the bar's own frame ---
    mov bx, [ds:di+2]
    mov cx, [ds:di+4]
    mov dx, [ds:di+6]
    UI_FRAME

    mov ax, [ds:di+0]           ; --- the two arrow-cell rules ---
    mov bx, [ds:di+4]
    mov dx, [ds:di+2]
    add dx, OS88UI_SBCELL
    UI_HLINE
    mov dx, [ds:di+6]
    sub dx, OS88UI_SBCELL
    UI_HLINE

    call os88ui_sbcx            ; SI = the arrow glyphs' centre column
    mov bp, 0                   ; --- the up glyph: 5 rows, widths 1..9 ---
    mov dx, [ds:di+2]
    add dx, 3
.up:
    mov ax, si
    sub ax, bp
    mov bx, si
    add bx, bp
    UI_HLINE
    inc dx
    inc bp
    cmp bp, 5
    jb .up

    mov bp, 4                   ; --- ...and the down glyph, widths 9..1 ---
    mov dx, [ds:di+6]
    sub dx, 7
.dn:
    mov ax, si
    sub ax, bp
    mov bx, si
    add bx, bp
    UI_HLINE
    inc dx
    dec bp
    jns .dn

    mov ax, [ds:di+0]           ; --- the track, 50% grey between the rules ---
    inc ax
    mov bx, [ds:di+2]
    add bx, OS88UI_SBCELL+1
    mov cx, [ds:di+4]
    dec cx
    mov dx, [ds:di+6]
    sub dx, OS88UI_SBCELL+1
    cmp bx, dx
    jg .out                     ; degenerate track: no grey and no thumb
    UI_GRAY

    mov bx, di                  ; --- and the thumb, if there is one ---
    call os88ui_sbthumb
    jc .out
    call os88ui_sbthdraw
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_sbcx - the arrow glyphs' centre column (internal)
; in:  DI = the block
; out: SI = the column; nothing else touched
; -----------------------------------------------------------------------------
os88ui_sbcx:
    push ax
    mov si, [ds:di+4]
    sub si, [ds:di+0]
    shr si, 1                   ; (x2-x1)/2, which is 6 on a 14px bar
    add si, [ds:di+0]
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_sbthdraw - draw the thumb: a white body in a black frame (internal)
; in:  DI = the block, AX = its top, DX = its height; pen BLACK on the way out
;
; THE BORDER GOES DOWN FIRST AND THE FILL IS THE INTERIOR IT ENCLOSES, which
; is the same two calls and the same pixels and is NOT the same picture while
; they are running (SPEC.md 13.10.5.9).
;
; It used to be a white fill over the whole thumb and then a frame around it -
; identical output, and between the two calls the thumb was a BORDERLESS WHITE
; SLAB, because the fill had just erased the border the frame was about to put
; back. On a 4.77 MHz 8088 a drawing call is ~756 us whatever it draws, so that
; state is held for the better part of a millisecond every time the thumb
; moves; caught in the act with a breakpoint inside this routine, the old thumb
; and the new one merge into one unbordered mass twice the thumb's height. That
; is PERFORMANCE.md Part 1's DOUBLE-DRAW FLASH exactly - drawn twice, invisible
; in an emulator, and the reason a dragged thumb shimmered.
;
; This way every lit region on the bar is bounded by a border at every instant.
; Mid-move you see two framed boxes for the width of one call, which is
; SPEC.md 42.7.1's own argument: a doubled thumb reads as movement where an
; absent - or an unbordered - one reads as a blink.
; -----------------------------------------------------------------------------
os88ui_sbthdraw:
    push ax
    push bx
    push cx
    push dx
    mov bx, ax                  ; y1
    add dx, ax
    dec dx                      ; y2 = top + height - 1
    mov ax, [ds:di+0]
    add ax, 2
    mov cx, [ds:di+4]
    sub cx, 2
    UI_BLACK
    UI_FRAME                    ; the border...
    inc ax                      ; ...and the interior INSIDE it, which is what
    dec cx                      ; keeps the border off the fill's rect
    inc bx
    dec dx
    cmp ax, cx
    jg .out                     ; a bar narrow enough to have no interior is
    cmp bx, dx                  ; all border, and the frame above already drew
    jg .out                     ; it. Nothing here requires a 14px bar (13.10.2)
    UI_WHITE
    UI_FILL
    UI_BLACK                    ; the header's promise: the pen leaves BLACK
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_sbhit - which part of the bar is this point in?
; in:  BX = the block, CX = x, DX = y (ABSOLUTE)
; out: AL = OS88UI_SB*; AH clobbered, everything else preserved
;
; A point outside the bar answers OS88UI_SBNONE, so a caller may hand it a
; release that landed anywhere on the screen without a test of its own.
; -----------------------------------------------------------------------------
os88ui_sbhit:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, bx
    cmp cx, [ds:di+0]
    jl .none
    cmp cx, [ds:di+4]
    jg .none
    cmp dx, [ds:di+2]
    jl .none
    cmp dx, [ds:di+6]
    jg .none
    mov si, [ds:di+2]
    add si, OS88UI_SBCELL
    cmp dx, si
    jle .up                     ; the up-arrow cell, its rule included
    mov si, [ds:di+6]
    sub si, OS88UI_SBCELL
    cmp dx, si
    jge .down
    mov si, dx                  ; --- the track: SI = the point's y, banked
    mov bx, di                  ; across the thumb call, which answers in DX
    call os88ui_sbthumb
    jc .none                    ; no thumb: the track is inert
    cmp si, ax
    jl .pgup
    add ax, dx                  ; ...one past the thumb's last row
    cmp si, ax
    jge .pgdn
    mov al, OS88UI_SBTHUMB
    jmp short .out
.pgup:
    mov al, OS88UI_SBPGUP
    jmp short .out
.pgdn:
    mov al, OS88UI_SBPGDN
    jmp short .out
.up:
    mov al, OS88UI_SBUP
    jmp short .out
.down:
    mov al, OS88UI_SBDOWN
    jmp short .out
.none:
    mov al, OS88UI_SBNONE
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_sbmove - the view scrolled: move the THUMB and draw nothing else
; in:  BX = the block, already holding the NEW pos; AX = the pos it was at;
;      the gfx lock is held
; out: nothing (all registers preserved); the pen is left BLACK
;
; A scroll does not change the thumb's HEIGHT - that is fit*track/total and a
; scroll moves none of the three - so it TRANSLATES it, and the frame, the two
; rules, the two arrow glyphs and all of the track it did not cover are exactly
; where they were. Three drawing calls instead of the bar's sixteen.
;
; files.inc had this and fdlg.inc did not, which is one of the things being
; shared here rather than merely deduplicated: the dialog's scroll used to
; repaint its whole bar.
;
; THE NEW THUMB IS DRAWN BEFORE THE STRIP IT VACATED IS GREYED, which is
; SPEC.md 42.7.1's ordering: for the width of one fill the thumb is briefly
; DOUBLED, and a doubled thumb reads as movement where an absent one reads as
; a blink. A step too small to move it draws nothing at all.
; -----------------------------------------------------------------------------
os88ui_sbmove:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, bx
    mov bp, ax                  ; BP = the old pos
    call os88ui_sbthumb         ; where it is going
    jc .out                     ; no thumb: nothing moved
    mov si, ax                  ; SI = the new top, CX its height
    mov cx, dx
    mov ax, [ds:di+12]          ; ...and where it was, by putting the old pos
    push ax                     ; back for one call
    mov [ds:di+12], bp
    mov bx, di
    call os88ui_sbthumb
    pop word [ds:di+12]         ; (its CF cannot differ: the refusal is about
                                ; total against fit, and a scroll moves neither)
    cmp ax, si
    je .out                     ; the track is short enough that this step does
                                ; not move it: draw nothing

    mov bp, ax                  ; BP = the old top, wanted after the fills
    push cx
    mov ax, si                  ; the NEW thumb first (see the header)
    mov dx, cx
    call os88ui_sbthdraw
    pop cx

    mov bx, bp                  ; BX/DX = the band it is leaving
    mov dx, bp
    add dx, cx
    dec dx
    cmp bp, si
    jl .down
    mov ax, si                  ; it moved UP: what it vacated is the strip
    add ax, cx                  ; below the new one (= new bottom + 1)
    cmp ax, bx
    jle .grey                   ; disjoint: the whole old band is vacated
    mov bx, ax
    jmp short .grey
.down:
    mov ax, si                  ; it moved DOWN: the strip above it
    dec ax
    cmp ax, dx
    jge .grey
    mov dx, ax
.grey:
    cmp bx, dx
    jg .out
    mov ax, [ds:di+0]           ; the track, between the arrow-cell rules
    inc ax
    mov cx, [ds:di+4]
    dec cx
    UI_GRAY
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef OS88UI_SBDRAG
; =============================================================================
; THE THUMB DRAG (SPEC.md 13.10.5) - a KNOB, and KERN_BIG's
;
; A scroll bar's gesture is not one press. It is a press, a stream of
; movements and a release, and SPEC.md 13.7 forbids a package answering that
; with a polling loop - so until the three edges existed (W_ONCLICK,
; W_ONDRAG, W_ONMOUSEUP) neither kernel bar COULD drag its thumb, whatever it
; wanted to do with one. This is ui_drag's substance without ui_drag's shape:
; the same tracking, spread across three callbacks instead of held inside one
; modal loop with the gfx lock down for the whole gesture.
;
; IT IS STILL GEOMETRY AND NOT POLICY (13.10.1). What is owned here is the
; gesture's ARITHMETIC - the anchor, the clamp, the pointer-to-pos inverse and
; the overlay - and the one question it answers is "the pointer is here, so
; the view belongs at pos". Whether the caller can AFFORD to go there now is
; the caller's, which is what the rate is (13.10.5.4).
;
; x IS NEVER READ (13.10.5.2). os88ui_sbtrack takes DX and does not take CX,
; so the thumb is locked to the bar's vertical extent wherever the hand goes -
; deliberately NOT the Macintosh's snap-back, which needs a width to measure a
; tolerance against and a cancel path to get wrong.
;
; THE STATE IS A STATIC OF THE ELEMENT'S and the seven-word block does not
; grow (13.10.5.6): one drag at a time on the one UI task, os88ui_ksb's
; argument exactly - and a block that grew would move every caller's .bss and
; rebuild every package that ever declared one.
;
; THE CALLER NAMES TWO RATES AND THE MACHINE PICKS (13.10.5.4.1). A following
; view was rate 0 nearly everywhere because ONE commit is 117 to 417 ms on a
; 4.77 MHz 8088 - two to eight ticks BEHIND a hand that has already stopped,
; which is PERFORMANCE.md Part 1's VISIBLE REDRAW
; (docs/reports/SBRATE-COMMIT-COST-2026-09-11.md is the measurement). That is
; a fact about the MACHINE and not about the widget, so the element resolves
; it once for everybody instead of ten callers each asking the same question.
; =============================================================================

; -----------------------------------------------------------------------------
; os88ui_sbrate - which of the caller's two rates THIS machine can afford
; in:  AL = the rate for an 8086/8088, AH = the rate for a CPU_286 or better
;      (both in system ticks, as os88ui_sbgrab takes one - SPEC.md 13.10.5.4)
; out: AL = whichever applies here, ready for os88ui_sbgrab. AH is UNDEFINED;
;      every other register preserved, flags not.
;
; SPEC.md 13.10.5.4.1. It is still 13.10.1's geometry-not-policy: both numbers
; are the caller's and the element picks between them on a FACT the code can
; test (PERFORMANCE.md rule 7), never on a guess about how fast this is.
;
; THE POP IS THE TRICK AND IT IS UI_ISMONO'S: a pop writes no flags, so the
; compare's answer survives the restore and the whole body is nine bytes and
; the macro.
; -----------------------------------------------------------------------------
os88ui_sbrate:
    push ax                     ; the pair, banked whole
    UI_CPUTIER                  ; AL = CPU_8086 / CPU_286 / CPU_386
    cmp al, CPU_286
    pop ax
    jb .out                     ; an 8086/8088: AL is already tier 0's
    mov al, ah
.out:
    ret

; -----------------------------------------------------------------------------
; os88ui_sbgrab - take the thumb: a press landed on it and the hand has it now
; in:  BX = the block, DX = the press's y (ABSOLUTE), AL = the rate in TICKS
;      (0 = the view does not follow at all until the release, 13.10.5.4) -
;      ONE rate, already resolved, which is what os88ui_sbrate above is for
; out: CF = 1 = there is no thumb to take and NOTHING was armed; CF = 0 = the
;      gesture is live. Every register preserved.
;
; The anchor is banked as press_y - thumb_top, so the thumb does not jump
; under the finger on the first movement.
; -----------------------------------------------------------------------------
os88ui_sbgrab:
    push ax
    push cx
    push dx
    mov [os88ui_sbd_rate], al
    mov cx, dx                  ; CX = the press's y; sbthumb answers in DX
    call os88ui_sbthumb         ; AX = the thumb's top, DX = its height
    jc .none                    ; everything fits, or the track is too short
    sub cx, ax
    mov [os88ui_sbd_off], cx    ; THE ANCHOR
    mov ax, [ds:bx+12]
    mov [os88ui_sbd_pos], ax    ; the drag starts where the view already is
    mov ax, [ds:bx+0]           ; ...and WHICH BAR it was taken on (13.10.5.10)
    mov [os88ui_sbd_id], ax
    mov ax, [ds:bx+2]
    mov [os88ui_sbd_id+2], ax
    mov byte [os88ui_sbd_on], 1
    UI_TICKS                    ; AX = the tick the rate is measured from
    mov [os88ui_sbd_last], ax
    clc
    jmp short .out
.none:
    stc
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_sbtrack - the pointer moved and the press is still down
; in:  BX = the block, DX = the pointer's y (ABSOLUTE). CX IS NOT TAKEN
; out: CF = 0 and AX = the pos the view is owed; CF = 1 = nothing is owed.
;      AX is clobbered either way; every other register preserved.
;
; Three ways to be owed nothing, and they are different: no drag is live; the
; hand moved but not far enough to change a row (13.10.5.3's quantisation);
; or the RATE says not yet. Only the first draws nothing at all - the other
; two have already moved the thumb or the overlay, which is the point of
; having a rate rather than a cadence.
; -----------------------------------------------------------------------------
os88ui_sbtrack:
    call os88ui_sbmine
    jc .no
    push cx
    push dx
    push si
    push di
    call os88ui_sbpos           ; DI = where the hand puts the view
    cmp di, [os88ui_sbd_pos]
    je .nothing                 ; the same row: not one pixel changes
    mov ax, [os88ui_sbd_pos]    ; os88ui_sbmove wants the old pos in AX and the
    mov [os88ui_sbd_pos], di    ; new one in the block, and os88ui_sbfix has
    mov [ds:bx+12], di          ; already put the old one there - so the two
    call os88ui_sbmove          ; agree by construction
.owed:
    cmp byte [os88ui_sbd_rate], 0
    je .nothing                 ; RATE 0: the release commits and nothing else
    push bx                     ; does (13.10.5.4)
    UI_TICKS
    mov si, ax
    sub ax, [os88ui_sbd_last]   ; wrap-safe (SPEC.md 8)
    mov bl, [os88ui_sbd_rate]
    xor bh, bh
    cmp ax, bx
    pop bx
    jb .nothing                 ; too soon: the thumb moved, the view waits
    mov [os88ui_sbd_last], si   ; the deadline is banked at the COMMIT, so a
    mov ax, di                  ; hand that pauses and moves again is served
    clc                         ; at once rather than waiting out a window it
    jmp short .out              ; already sat through
.nothing:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; os88ui_sbdrop - the button came up: the gesture is over
; in:  BX = the block, which 13.10.5.10 matches against the gesture's own
; out: CF = 0 and AX = the FINAL pos, which the caller owes the view
;      unconditionally; CF = 1 = no drag was live and AX is untouched.
;      Every other register preserved.
;
; IN THE KERNEL IT IS ALSO THE PAUSE (SPEC.md 13.10.5.4.2), and the same call
; from the same handler: the kernel's two bars install their W_ONMOUSEUP proc
; as their W_ONTIMER proc too, and this is where the two edges part. The UI
; task clears [ui_armw] BEFORE it dispatches a release (ui.inc's .mup), while a
; timer that fires mid-drag runs with the press still armed on the window - so
; "is the hand still down" is one word the kernel already keeps, exact where
; the ISR's [mouse_btn] is not (a release queued behind a long commit reads a
; second press as the first one still held). A pause answers the pos and keeps
; the record; a release answers it and spends it. A timer that outlived its
; gesture - the lost-release door of 13.10.5.7 - arrives with nothing armed and
; is spent as the release it stands in for.
; -----------------------------------------------------------------------------
os88ui_sbdrop:
    cmp byte [os88ui_sbd_on], 0
    je .no
    call os88ui_sbmine          ; a DIFFERENT bar's gesture (13.10.5.10): it is
    jc .spend                   ; spent either way - one nobody will release
                                ; must not outlive this press - but the pos
                                ; below is not this caller's to scroll to
%ifdef OS88UI_KERNEL
    cmp word [ui_armw], 0       ; the hand still has it: a PAUSE, and the
    jne .owed                   ; record survives it (SPEC.md 13.10.5.4.2)
%endif
    mov byte [os88ui_sbd_on], 0
.owed:
    mov ax, [os88ui_sbd_pos]
    clc
    ret
.spend:
    mov byte [os88ui_sbd_on], 0
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; os88ui_sbowed - where a LIVE drag on this bar has got to, WITHOUT spending it
; in:  BX = the block
; out: CF = 0 and AX = the hand's pos; CF = 1 = no drag, or a DIFFERENT bar's
;      (13.10.5.10) and AX is untouched. Every other register preserved.
;
; SPEC.md 13.10.5.4.2's second trigger - the caller's W_ONTIMER asks this after
; IDLE ticks in which the thumb did not move, and commits what it answers.
;
; It is os88ui_sbdrop with the SPEND taken out, and that is the whole reason it
; has to live here: a pause is not the end of the gesture, so the record must
; survive it, and `os88ui_sbd_pos` is the element's own state.
;
; PACKAGES ONLY. The kernel's bars fire their release proc from the timer and
; os88ui_sbdrop tells the two edges apart off [ui_armw], which a package cannot
; read - so a package keeps a W_ONTIMER proc of its own and asks this.
; -----------------------------------------------------------------------------
%ifndef OS88UI_KERNEL
os88ui_sbowed:
    call os88ui_sbmine
    jc .no
    mov ax, [os88ui_sbd_pos]
    clc
.no:
    ret
%endif

; -----------------------------------------------------------------------------
; os88ui_sbdragging - is a thumb drag live?
; out: CF = 1 = no. Every register preserved.
;
; One instruction, and it is a compare against 1 rather than against 0 so that
; the borrow IS the answer: a caller's whole test is a call and a jc.
; -----------------------------------------------------------------------------
os88ui_sbdragging:
    cmp byte [os88ui_sbd_on], 1
    ret

; -----------------------------------------------------------------------------
; os88ui_sbmine - is the block in BX the bar this gesture was taken on?
; in:  BX = a block
; out: CF = 1 = no drag, or a DIFFERENT bar's. Every register preserved.
;
; SPEC.md 13.10.5.10. There is one gesture record and there are two kernel
; bars, so "a drag is live" and "a drag is live ON THIS BAR" stopped being the
; same question the moment the file dialog was wired. Without this,
; os88ui_sbfix would fix the Disk window's dragged pos into the DIALOG's block
; - a thumb drawn where nothing is, and a hit test that agrees with it.
;
; The identity is (x1, y1), which is enough: two bars on screen at the same
; time cannot share a top-left corner.
; -----------------------------------------------------------------------------
os88ui_sbmine:
    cmp byte [os88ui_sbd_on], 0
    je .no
    push ax
    mov ax, [ds:bx+0]
    cmp ax, [os88ui_sbd_id]
    jne .other
    mov ax, [ds:bx+2]
    cmp ax, [os88ui_sbd_id+2]
    jne .other
    pop ax
    clc
    ret
.other:
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; os88ui_sbfix - the caller has just filled its block: put the DRAGGED pos in
; in:  BX = the block
; out: nothing; every register preserved
;
; SPEC.md 13.10.5.6. While a drag is live the thumb is drawn where the HAND is
; and the content is where the VIEW is, and at rate 0 those disagree for the
; whole gesture. That is one fact in two places - 13.8.2's "the glass and the
; byte cannot part" in another costume - and it is answered HERE, once, rather
; than at every drawing site: every reader downstream sees one coherent block
; and none of them knows a drag exists.
;
; -----------------------------------------------------------------------------
os88ui_sbfix:
    call os88ui_sbmine          ; ...on THIS bar (13.10.5.10)
    jc .out
    push ax
    mov ax, [os88ui_sbd_pos]
    mov [ds:bx+12], ax
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; os88ui_sbpos - where the hand puts the view (internal)
; in:  BX = the block, DX = the pointer's y (ABSOLUTE)
; out: DI = the pos; every other register preserved
;
; THE INVERSE of os88ui_sbthumb, and 13.10.5.3 is the proof that its div
; cannot overflow: the clamp holds (top - track_top) to at most the TRAVEL,
; so the quotient is at most total - fit. Reached only with a live drag, which
; means sbthumb said yes at the press - but total and fit are the CALLER's and
; can change under the gesture, so a refusal now keeps the pos it already had.
; -----------------------------------------------------------------------------
os88ui_sbpos:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov bp, dx                  ; BP = the pointer's y
    call os88ui_sbthumb         ; DX = the thumb's height
    jc .keep
    mov di, dx                  ; ...banked: mul is about to want DX
    mov si, [ds:bx+2]
    add si, OS88UI_SBCELL+1     ; SI = the track's top
    mov cx, [ds:bx+6]
    sub cx, OS88UI_SBCELL+1
    sub cx, si
    inc cx                      ; CX = the track's height
    sub bp, [os88ui_sbd_off]    ; the wanted TOP (13.10.5.2)...
    sub bp, si                  ; ...rebased onto the track
    jns .lo
    xor bp, bp
.lo:
    mov ax, cx
    sub ax, di                  ; the last legal top, rebased
    jns .hi
    xor ax, ax
.hi:
    cmp bp, ax
    jle .hok
    mov bp, ax
.hok:
    mov cx, ax                  ; CX = the TRAVEL (track - height): the clamp
    jcxz .keep                  ; above already put BP inside it. No travel -
                                ; a MINH thumb on a MINH track - is nowhere
                                ; to drag and a zero divisor: keep the pos
    mov ax, [ds:bx+8]
    sub ax, [ds:bx+10]          ; AX = total - fit (sbthumb refused
                                ; everything-fits with CF long before here)
    mul bp                      ; x the wanted top -> DX:AX
    div cx                      ; / the travel (13.10.5.3) - the exact
                                ; inverse of sbthumb's pos -> top, clamp
                                ; included, so the two agree at both ends
    mov di, ax
    mov ax, [ds:bx+8]           ; ...and never past the last page
    sub ax, [ds:bx+10]
    jns .cl
    xor ax, ax
.cl:
    cmp di, ax
    jbe .out
    mov di, ax
    jmp short .out
.keep:
    mov di, [os88ui_sbd_pos]
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifndef OS88UI_KERNEL
; A PACKAGE's state is inline DATA (SPEC.md 2.6 rule 1) - os88ui_armw's
; precedent above. The kernel's is in .bss at the bottom of this file, and
; that is a RULE and not a preference: this file assembles into .cold there,
; whose CS is COLD_SEG while DS stays KERNEL_SEG, so a word declared here and
; read through DS would be whatever lives at the same offset in .text.
os88ui_sbd_on:   db 0
os88ui_sbd_rate: db 0
os88ui_sbd_off:  dw 0
os88ui_sbd_pos:  dw 0
os88ui_sbd_last: dw 0
os88ui_sbd_id:   dw 0, 0
%endif
%endif                          ; OS88UI_SBDRAG

%endif                          ; OS88UI_SCROLL

%ifndef OS88UI_KERNEL

%ifdef OS88UI_ALERT
; =============================================================================
; THE ALERT (SPEC.md 75.3) - one line of text over up to three buttons, in a
; window of your own, answered to a completion proc.
;
; **IT IS A PACKAGE'S, NOT THE KERNEL'S, AND THAT IS THE WHOLE POINT.** It was
; built as `kernel/ask.inc` first and measured at **1,067 bytes** of a kernel
; whose footprint guard had 2,048 spare - a windowed dialog with buttons has a
; floor of about 800 bytes wherever it lives, and there is no version of it
; that fits a kernel budget worth spending on it. Here it costs the kernel
; NOTHING and each package that asks for it 607 bytes of its own image, which
; is heap and only while the app is running. That is this file's own
; argument (see the header) applied to something bigger than a button.
;
; It is also SMALLER here than it was there, for three reasons that are all
; about the segment boundary: the message and the title are near pointers in
; YOUR segment, so nothing is staged; the window dies with your region, so
; there is no staleness triple to check; and the dismissal arrives through
; your own W_ONCLOSE rather than through a pass in the UI task.
;
; --- USING IT ---------------------------------------------------------------
;   %define OS88UI_ALERT        ; before the %include, beside OS88UI_SCROLL
;
;   mov al, OS88UI_ASAVE        ; the button set
;   mov bx, si                  ; YOUR window - the alert takes its title
;   mov si, my_question         ; a NUL line, at most OS88UI_AMAX characters
;   mov di, my_answer           ; the completion, a near proc of yours
;   call os88ui_ask             ; CF = 1 refused (one is already up - and it
;                               ; has been RAISED, which is what this has
;                               ; instead of modality)
;
; Your completion is called AL = the 0-based button index, left to right, or
; OS88UI_ACANCEL when the alert was DISMISSED - its close box, its minimize
; box or Esc. The gfx lock is held, the alert is already destroyed, and SI is
; YOUR window. HANDLE OS88UI_ACANCEL: it is the only answer that arrives
; without the user having chosen anything.
;
; --- THE WINDOW IS UNBOUND, WHICH IS WHAT MAKES IT A DIALOG ------------------
; It belongs to no instance (SPEC.md 20.2's second window, ModPlug's Setup
; pane), so it has no dock tile, no Task Manager row and no callback billing,
; and its close box reduces to wm_hide. os88ui_aclose is installed on it with
; OSAPI_WM_ONCLOSE, so that hide is intercepted and turned into the CANCEL
; answer - the close negotiation of SPEC.md 75.1 serving a package's own
; dialog, which is why that half of the feature is in the kernel and this
; half is not.
;
; wm_destroy_seg sweeps it at teardown (SPEC.md 20.2), so an app that dies
; with its alert up leaves nothing behind.
;
; --- AND IT DOES NOT ZOOM OPEN (SPEC.md 11.99.2.1) --------------------------
; os88ui_ask calls OSAPI_WM_NOANIM before it shows the window. Your own second
; window may want the same: the flag is for a DIALOG, something opened and
; dismissed over and over or arriving in answer to a click that meant
; something else. It is not for your main window - whether the system animates
; at all is the user's choice, in Control Panel -> Theme.
; =============================================================================

OS88UI_AOK     equ 0            ; the button sets. Index 0 is the DEFAULT - it
OS88UI_AYESNO  equ 1            ; carries the ring and it is what Enter fires
OS88UI_ASAVE   equ 2            ; Save / Discard / Cancel
OS88UI_ADANGER equ 3            ; Cancel / Proceed - and the ORDER is the whole
                                ; of it. Index 0 carries the ring and Enter
                                ; fires it, so a set whose destructive answer
                                ; is index 0 arms the dangerous button with the
                                ; key a user presses to get rid of a dialog.
                                ; OS88UI_AYESNO cannot say this and neither can
                                ; a caller rewording the question: the default
                                ; is a property of the SET
OS88UI_ACANCEL equ 0FFh         ; ...and the answer for a DISMISSED alert
OS88UI_AMAX    equ 34           ; the message, in characters (it is CLIPPED by
                                ; the window, not refused - keep it short)

OS88UI_AW    equ 288            ; the window
OS88UI_AH    equ 92
OS88UI_ABW   equ 72             ; every button the same width, which is what
                                ; lets the row be a formula rather than a
                                ; measured layout - 153 bytes of the kernel
                                ; version went on measuring one. 72 fits the
                                ; widest label here (7 characters plus the
                                ; standard 16px of padding)
OS88UI_ABG   equ 12             ; ...and the gap between them
OS88UI_ABH   equ 13             ; FD_BH: the same button as both kernel dialogs
OS88UI_AMSGY equ 14             ; the message's pen, from the content top
OS88UI_ABTNY equ 46             ; ...and the button row's

; --- state, INLINE DATA and not bss, for os88ui_armw's reason ----------------
os88ui_atpl:  dw 0, 0, OS88UI_AW, OS88UI_AH   ; x and y are centred at open
              dw 0, os88ui_apaint, os88ui_akey, os88ui_aclick
os88ui_awin:  dw 0               ; the live alert, 0 = none. THE GATE: one at
                                 ; a time, fdlg_win's single word
os88ui_acb:   dw 0               ; your completion
os88ui_arqw:  dw 0               ; ...and your window, which it is handed
os88ui_amsg:  dw 0               ; the message, in your segment
os88ui_aset:  db 0               ; which set is up
os88ui_adown: db 0FFh            ; the button drawn PRESSED, 0xFF = none
os88ui_ar:    dw 0, 0, 0, 0      ; ONE button's rect at a time - os88ui_krect's
                                 ; argument, and neither the painter nor the
                                 ; hit test needs two

; --- ...and the one control's STAGING beside it (SPEC.md 20.5.1.3) ----------
; os88ui_kbtn's shape on the PACKAGE side. os88ui_btn takes a RECORD and an
; index; the alert has neither, because it lays its row out as a FORMULA
; (os88ui_arect) and keeps its own arm - so it stages one button and draws it,
; exactly as the kernel's three dialogs do through os88ui_krec. It cannot USE
; os88ui_krec: that block is inside %ifdef OS88UI_KERNEL and this one is
; inside %ifndef.
;
; **THIS IS WHERE THE ALERT WAS LEFT BEHIND, and it is 2414402c's defect at
; the one call site a grep for `call os88ui_btn` DOES find.** os88ui_abtn1
; went on handing os88ui_ar - a rect - to an os88ui_btn that had started
; taking a record, so the live count came out of the rect's y2, the flags
; pointer out of its x2 and the rect pointer out of its x1. Index 0 was `dec`ed
; to -1 and drew NOTHING; index 1 read OS88UI_BT_DOWN out of os88ui_asets' own
; first word, found 1, and painted a rectangle read from four arbitrary words
; of the package's image with the PRESSED interior - black, across the whole
; screen, which is what the DOS box's "Open windows are lost" question did.
; The registry counted this site as "the library's own" and never asked
; whether it had been converted; tests/unit/t_btnrules.py asks now.
;
; DATA and not bss, for os88ui_armw's reason above. Every field but the label
; and the flags is a CONSTANT, so the record is declared already aimed and
; os88ui_abtn1 writes only the two words that move.
os88ui_arlbl: dw 0               ; the staged label, a one-entry array...
os88ui_arflg: dw 0               ; ...and its flags, likewise
os88ui_arec:
    dw os88ui_ar                 ; OS88UI_BT_RECTS  - the rect above, restaged
    dw os88ui_arlbl              ; OS88UI_BT_LABELS
    dw os88ui_arflg              ; OS88UI_BT_FLAGS
    dw 1                         ; OS88UI_BT_N      - ONE, always
    dw 0                         ; OS88UI_BT_WIN    - os88ui_btn never reads
                                 ; it, and the alert is not on the record's
                                 ; gesture: os88ui_akey has Enter and Esc to
                                 ; serve (SPEC.md 75.3), which os88ui_btnup
                                 ; cannot
    dw 0                         ; OS88UI_BT_DOWN   - stays 0: the pressed
                                 ; look arrives in the FLAGS word instead,
                                 ; which is what OS88UI_LATCH already does and
                                 ; is cheaper than a second copy of
                                 ; [os88ui_adown] to keep in step
    dw 0                         ; OS88UI_BT_ONCLK
    dw 0                         ; OS88UI_BT_NEXT

; the sets: a count byte then three label offsets, 8 bytes a row so the index
; is a shift (an 8086 has no cheap multiply by 7)
os88ui_asets:
    dw 1, os88ui_as_ok,   0,             0
    dw 2, os88ui_as_yes,  os88ui_as_no,  0
    dw 3, os88ui_as_save, os88ui_as_disc, os88ui_as_can
    dw 2, os88ui_as_can,  os88ui_as_proc, 0
os88ui_as_ok:   db 'OK', 0
os88ui_as_yes:  db 'Yes', 0
os88ui_as_no:   db 'No', 0
os88ui_as_save: db 'Save', 0
os88ui_as_disc: db 'Discard', 0
os88ui_as_can:  db 'Cancel', 0
os88ui_as_proc: db 'Proceed', 0  ; SEVEN characters, which is what OS88UI_ABW
                                 ; holds - 'Continue' would not fit the row

; -----------------------------------------------------------------------------
; os88ui_ask - raise the alert (see the header)
; in:  AL = OS88UI_A* set, BX = your window, SI = the message, DI = your
;      completion; THE GFX LOCK IS HELD (a callback, a negotiator or an
;      AM_ONCMD handler)
; out: CF = 0 it is up, CF = 1 refused - one was already up and has been
;      raised, or the window table is full. All registers preserved.
; -----------------------------------------------------------------------------
os88ui_ask:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [os88ui_awin], 0
    je .free
    mov bx, [os88ui_awin]       ; one is up: put it back in front of whatever
    call OSAPI_WM_SHOW          ; hid it - which covers a MINIMIZED alert too,
    jmp short .refuse           ; wm_show being un-hide and raise in one
.free:
    mov [os88ui_aset], al
    mov [os88ui_acb], di
    mov [os88ui_arqw], bx
    mov [os88ui_amsg], si
    mov ax, [es:bx + W_TITLE]   ; YOUR caption, so the alert says which
    mov [os88ui_atpl + WT_TITLE], ax  ; application is asking. ES is
                                ; KERNEL_SEG on entry to every callback and
                                ; the record lives there (SPEC.md 20.1); the
                                ; OFFSET it holds is ours, so it needs no
                                ; translating
    call OSAPI_VIDEO            ; AX = width, BX = height: centre on the LIVE
    sub ax, OS88UI_AW           ; screen (SPEC.md 39), not on 640x480
    sar ax, 1
    mov [os88ui_atpl + WT_X], ax
    sub bx, OS88UI_AH
    sar bx, 1
    cmp bx, MBAR_H + 8
    jge .placed                 ; ...but never up under the menu bar on a
    mov bx, MBAR_H + 8          ; short screen; wm_fit has the other end
.placed:
    mov [os88ui_atpl + WT_Y], bx
    mov si, os88ui_atpl
    call OSAPI_WM_CREATE        ; BX = window ptr, CF = the table is full
    jc .refuse
    mov byte [os88ui_adown], 0FFh
    mov ax, os88ui_aup
    call OSAPI_WM_ONMOUSEUP     ; the buttons fire on the RELEASE (SPEC.md
    mov ax, os88ui_adrag        ; 13.6), they TRACK between the press and it
    call OSAPI_WM_ONDRAG        ; (SPEC.md 13.8.2/75.3.0 - CF = 1 on
                                ; kern_small, which installs nothing and
                                ; degrades to the release re-testing the
                                ; button under it), and the close box is
    mov ax, os88ui_aclose       ; intercepted
    call OSAPI_WM_ONCLOSE       ; (SPEC.md 75.1) so it can be the CANCEL
    call OSAPI_WM_NOANIM        ; ...AND IT DOES NOT ZOOM OPEN (SPEC.md
                                ; 11.99.2.1). The Standard File dialog and the
                                ; notice window already declined and this is
                                ; the third of the same species - except that
                                ; the argument is stronger here, because the
                                ; other two answer a command the user picked
                                ; and this one answers a click on the CLOSE
                                ; BOX: it arrives INSTEAD OF the close that
                                ; was asked for, and a third of a second of
                                ; outline in front of an interruption reads as
                                ; the machine making a show of getting in the
                                ; way. BX is still the window - the four slots
                                ; above all take it and preserve it
    mov [os88ui_awin], bx       ; take the gate LAST: until this store the
    call OSAPI_WM_SHOW          ; window is an ordinary one
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_adone - close, then answer (internal)
; in:  AL = the answer; the gfx lock is held
; out: nothing
;
; Teardown FIRST, callback second - fdlg_commit's order and its reason:
; wm_destroy's repaint has already put back everything the alert covered, so
; the completion draws onto a clean window instead of over a dialog that is
; still on screen.
; -----------------------------------------------------------------------------
os88ui_adone:
    push bx
    push si
    push di
    mov bx, [os88ui_awin]
    or bx, bx
    jz .out
    mov word [os88ui_awin], 0   ; the gate goes first: a repaint that reached
    call OSAPI_WM_DESTROY       ; this module again must not find a dying one
    mov si, [os88ui_arqw]
    mov di, [os88ui_acb]
    or di, di
    jz .out
    push es
    call di                     ; AL = the answer, SI = your window. A NEAR
    pop es                      ; call: we are already in your segment
.out:
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_aclose - the alert's own W_ONCLOSE (SPEC.md 75.1)
; in:  SI = the alert window; gfx lock held
; out: CF = 1 always
;
; The close box and the app-name menu's Close both arrive here, and both mean
; CANCEL. It answers REFUSED because by the time it returns there is nothing
; left to close: os88ui_adone has already destroyed the window, and letting
; app_close_win go on to hide a freed record is the one thing that would not
; survive.
; -----------------------------------------------------------------------------
os88ui_aclose:
    push ax
    mov al, OS88UI_ACANCEL
    call os88ui_adone
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; os88ui_aset_p - SI -> this alert's labels, CX = how many (internal)
; -----------------------------------------------------------------------------
os88ui_aset_p:
    push ax
    mov al, [os88ui_aset]
    xor ah, ah
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1                   ; x8: the row stride
    add si, os88ui_asets
    mov cx, [si]
    add si, 2
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_arect - one button's rect (internal)
; in:  AL = its index; [os88ui_awin] is live
; out: BX -> os88ui_ar, filled; every other register preserved
;
; A FORMULA, not a measured layout: every button is OS88UI_ABW wide, so the
; row is CX*(BW+GAP)-GAP and the i'th starts a fixed step along it. That is
; the single biggest saving over the kernel version this replaces, where
; laying out three buttons by their label widths cost 153 bytes.
; -----------------------------------------------------------------------------
os88ui_arect:
    push ax
    push cx
    push dx
    push si
    push di
    mov di, ax
    and di, 0FFh                ; DI = the index
    call os88ui_aset_p          ; CX = the count (SI is spent)
    mov ax, OS88UI_ABW + OS88UI_ABG
    mul cx                      ; DX:AX - the row plus one trailing gap
    sub ax, OS88UI_ABG          ; AX = the row's whole width
    mov si, [os88ui_awin]
    mov cx, [es:si + W_W]
    sub cx, ax
    sar cx, 1
    add cx, [es:si + W_X]       ; CX = the row's left edge, centred. IN CX AND
                                ; NOT DX: `mul` below writes DX whatever the
                                ; product is, and the first version banked the
                                ; origin there - so button 0 was laid out at
                                ; x = 0, off the left of the alert entirely,
                                ; and every hit test agreed with the drawing
                                ; because both ask this routine. What caught it
                                ; was a click on Cancel opening a Save As
                                ; dialog: the rects were WRONG, not
                                ; INCONSISTENT, which no amount of asking the
                                ; same routine twice can show
    mov ax, OS88UI_ABW + OS88UI_ABG
    mul di
    add ax, cx
    mov [os88ui_ar], ax
    add ax, OS88UI_ABW - 1
    mov [os88ui_ar + 4], ax     ; x2 is INCLUSIVE (gfx_frame's convention)
    mov ax, [es:si + W_Y]
    add ax, TITLE_H + OS88UI_ABTNY
    mov [os88ui_ar + 2], ax
    add ax, OS88UI_ABH
    mov [os88ui_ar + 6], ax
    mov bx, os88ui_ar
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_abtns - draw the button row (internal); the gfx lock is held
; -----------------------------------------------------------------------------
os88ui_abtns:
    push ax
    push cx
    call os88ui_aset_p          ; CX = the count (SI is spent)
    xor al, al
.next:
    call os88ui_abtn1
    inc al
    loop .next
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_abtn1 - draw ONE button (internal)
; in:  AL = its index; an index that is not a button draws nothing, which is
;      what lets os88ui_adn hand it 0xFF for "none was down"
; out: nothing; preserves all registers
;
; SPEC.md 75.3.0: os88ui_adn used to redraw the whole ROW for a state change
; that can only ever touch two buttons, which on OS88UI_ASAVE is three lettered
; where one was needed - PERFORMANCE.md rule 1, in the control every package's
; dialog goes through.
; -----------------------------------------------------------------------------
os88ui_abtn1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_aset_p          ; SI -> the labels, CX = the count
    cmp al, cl
    jae .out                    ; not a button: 0xFF arrives here every press
    mov dl, al
    xor dh, dh
    mov di, dx
    add di, di
    add si, di                  ; SI -> this button's label pointer
    call os88ui_arect           ; os88ui_ar, filled - and os88ui_arec already
                                ; NAMES it, so the rect is staged rather than
                                ; passed (BX is spent below; AX is still the
                                ; index)
    xor di, di                  ; no flags: OS88UI_FILL is the CONTROL's now
                                ; (SPEC.md 20.5.1.3.1) and os88ui_btn ORs it
                                ; in for every button there is
    or dl, dl
    jnz .nodef
    or di, OS88UI_DEF           ; button 0 carries the ring, and Enter fires
.nodef:                         ; it - one fact, drawn and acted on from the
    cmp dl, [os88ui_adown]      ; same place
    jne .nodown
    or di, OS88UI_DOWN          ; ...and the pressed look, DRAWN and not
.nodown:                        ; XOR-ed (SPEC.md 13.8)
    mov si, [si]                ; SI = the label itself, not its slot
    mov [os88ui_arlbl], si      ; ...and both into the one-entry arrays
    mov [os88ui_arflg], di      ; os88ui_arec points at
    mov bx, os88ui_arec         ; THE RECORD, which is what os88ui_btn takes
    mov al, 1                   ; one button, index 0, so PLUS ONE is 1
    call os88ui_btn
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_ahit - which button is the point in? (internal)
; in:  CX = x, DX = y
; out: AX = the index PLUS ONE, 0 = none; every other register preserved
; -----------------------------------------------------------------------------
os88ui_ahit:
    push bx
    push cx
    push dx
    push si
    push di
    push cx                     ; THE POINT'S X, ON THE STACK, because
                                ; os88ui_aset_p answers in CX *and* SI and so
                                ; neither register can bank it. Both attempts
                                ; at this failed the same way and it is worth
                                ; naming: the hit test read a coordinate the
                                ; callee had overwritten, so every button
                                ; missed at every position - while the drawing
                                ; stayed perfect, because the PAINTER never
                                ; asks this routine anything. A shared rect
                                ; keeps the two consistent (SPEC.md 22's
                                ; fm_hit discipline); it cannot make them
                                ; RIGHT
    call os88ui_aset_p          ; CX = the count
    mov bx, cx
    pop cx                      ; ...and x back
    xor di, di
.next:
    cmp di, bx
    jae .none
    mov ax, di
    push bx
    call os88ui_arect           ; BX -> the rect (CX and DX come back intact)
    call os88ui_bhit            ; CF = 0 inside
    pop bx                      ; POP writes no flags, so bhit's answer stands
    jnc .hit
    inc di
    jmp short .next
.hit:
    mov ax, di
    inc ax
    jmp short .out
.none:
    xor ax, ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_apaint - the alert's W_PAINT; SI = its window, gfx lock held
;
; wm_draw_win has already filled the content white, so there is no erase here
; and no pair for SPEC.md 11.3's granularity rule to break.
; -----------------------------------------------------------------------------
os88ui_apaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_GFX_PEN          ; CF = 0: CBLACK, the disabled flag clear (47)
    mov si, [os88ui_amsg]
    call OSAPI_FONT_WIDTH       ; AX = its pixels
    mov cx, [es:bx + W_W]
    sub cx, ax
    sar cx, 1
    add cx, [es:bx + W_X]
    mov dx, [es:bx + W_Y]
    add dx, TITLE_H + OS88UI_AMSGY
    mov si, [os88ui_amsg]
    mov ax, (CWHITE << 8) | CBLACK  ; OPAQUE: an alert's content is the window
    UI_RUN                          ; manager's white fill (SPEC.md 38.12)
    call os88ui_abtns
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_adn - draw the pressed state (internal)
; in:  AL = the button index PLUS ONE that is down, 0 = none
; -----------------------------------------------------------------------------
os88ui_adn:
    push ax
    dec al                      ; the table's 0-based index; 0 -> 0xFF, which
    cmp al, [os88ui_adown]      ; no button is
    je .out                     ; ...AND IT DRAWS NOTHING WHEN THE STATE HAS
                                ; NOT CHANGED, which is the kernel twin's own
                                ; rule (cp_setdown, SPEC.md 13.8.3). Without
                                ; it a press landing on the MESSAGE - or on
                                ; the whitespace between buttons - answered 0,
                                ; wrote the same 0xFF back and re-lettered the
                                ; whole row for nothing (PERFORMANCE.md rule 1)
    mov ah, [os88ui_adown]      ; the one that WAS down...
    mov [os88ui_adown], al      ; ...and the state first, because os88ui_abtn1
    push ax                     ; reads it to decide the pressed look
    mov al, ah
    call os88ui_abtn1           ; upright again
    pop ax
    call os88ui_abtn1           ; ...and the one that is down now
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_aclick / os88ui_aup - the press arms and the RELEASE fires
; in:  CX = x, DX = y, SI = the alert window; gfx lock held
; -----------------------------------------------------------------------------
os88ui_aclick:
    push ax
    call os88ui_ahit            ; AX = the button PLUS ONE, 0 = none
    call os88ui_arm
    call os88ui_adn
    pop ax
    ret

; os88ui_adrag - W_ONDRAG: the pointer moved while a press is armed
; in:  CX = x, DX = y, SI = the alert window; gfx lock held
;
; **IT READS THE ARM** (SPEC.md 75.3.0, 13.8.2), through os88ui_armed's PEEK
; rather than os88ui_fire's take - which is the distinction that routine's own
; header exists to make. What it buys is the armed button going back UP as the
; pointer slides off it, which is what says the gesture is cancelled before the
; finger commits.
;
; Written as "os88ui_aclick without the arm" it pressed whatever the pointer
; happened to be over, armed or not - and a press that started on the alert's
; MESSAGE arms nothing, so the release took os88ui_aup's `jz .out` and never
; put it back up. One drag across an alert left a button drawn pressed for as
; long as the alert stood.
os88ui_adrag:
    push ax
    push bx
    call os88ui_armed           ; PEEK: the arm is the RELEASE's to spend
    or ax, ax
    jz .out                     ; the press armed nothing: track nothing
    mov bx, ax
    call os88ui_ahit            ; AX = the button under the pointer NOW
    cmp ax, bx
    je .set
    xor ax, ax                  ; off the armed one: nothing is down
.set:
    call os88ui_adn             ; ...which draws only on a change
.out:
    pop bx
    pop ax
    ret

os88ui_aup:
    push ax
    push di
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    mov di, ax
    xor ax, ax
    call os88ui_adn             ; UP FIRST, unconditionally: whatever is drawn
                                ; pressed has to come up whether this release
                                ; fires anything or not (SPEC.md 13.8.2)
    or di, di
    jz .out
    call os88ui_ahit            ; AX = the button under the RELEASE
    cmp ax, di
    jne .out                    ; a different button, or none: CANCELLED
    dec al
    call os88ui_adone
.out:
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_akey - the alert's W_ONKEY; AL = ascii, SI = its window
;
; Enter is the default button and Esc is the cancel, which are the two keys
; every dialog in this system already answers to. There is deliberately no
; first-letter shortcut: 'S' would mean Save on one alert and nothing on the
; next, and a key that is live only sometimes is worse than one that never is.
; -----------------------------------------------------------------------------
os88ui_akey:
    push ax
    cmp al, 13
    je .def
    cmp al, 27
    jne .out
    mov al, OS88UI_ACANCEL
    jmp short .go
.def:
    xor al, al                  ; button 0 - the one os88ui_abtns rings
.go:
    call os88ui_adone
.out:
    pop ax
    ret
%endif                          ; OS88UI_ALERT

%ifdef OS88UI_ABOUT
; =============================================================================
; THE ABOUT PANEL (SPEC.md 12.2, 20.5.1)
;
; The fourth shared element, and its argument is the SCROLL BAR's rather than
; the button's: a button was five bodies that agreed, and this was SEVENTEEN
; PRIVATE IMPLEMENTATIONS OF ONE CARD - arkanoid, browser, calc, cyclone,
; ftpd, missile, modplug, paint, solitaire, tamegram, telnet, texpad, tracker,
; wire and three more - each a white fill plus a black frame plus a column of
; centred lines, each with its own arithmetic for where the card goes, and no
; two of them measuring it the same way. Ten more packages had NO About at
; all, which is how the credit for a contributed package went missing: the
; cheapest thing to write was nothing.
;
; --- WHAT IT DOES NOT OWN ----------------------------------------------------
; The panel is DRAWN here and it is REMEMBERED by you. There is no state byte
; in this file and no dismissal: your `[x_abon]`, your painter drawing the
; card last while it is set, and your click/key/menu handler taking it down
; are four lines each and they are entangled with what your app is doing
; underneath - arkanoid PAUSES a live ball, tracker drops the worker's frame,
; calc repaints a keypad. A widget that tried to own that would have to know
; all three.
;
; --- WHY THERE ARE TWO ENTRIES ----------------------------------------------
; `os88ui_about` is for the HANDLER - the proc you gave OSAPI_ABOUT_SET.
; NOTHING HAS ARMED A CLIP REGION THERE: ui_dispatch takes the gfx lock and
; far-calls you, and the kernel arms a clip for a W_PAINT and for nothing else
; (SPEC.md 11.3), so a card drawn without one lands on top of whatever window
; is covering yours. `os88ui_about_d` is for your PAINTER, where the kernel's
; own region is already armed and re-arming would throw the damage rect away
; and redraw the whole card for a two-pixel repair.
;
; Two names over one flag argument, which is os88ui_arm/os88ui_armed's rule:
; "the handler" and "the painter" are what the two callers actually are, and a
; caller that got the flag backwards would draw over its neighbour.
;
; --- USING IT ----------------------------------------------------------------
;   %define OS88UI_ABOUT        ; before the %include, beside OS88UI_SCROLL
;
;   my_ablines:                 ; a line table: near ptrs, 0 terminates.
;       dw my_ab1, my_ab2, my_ab3, 0
;   my_ab1: db 'Thing for os8088', 0
;   my_ab2: db 0                ; an EMPTY string is a blank line
;   my_ab3: db 'Contributed by Someone', 0
;
;   my_about:                   ; ...registered with OSAPI_ABOUT_SET
;       mov byte [my_abon], 1
;       mov bx, [my_win]
;       mov si, my_ablines
;       call os88ui_about       ; CF = 1: not one pixel of you is visible, so
;       ret                     ; nothing was drawn - your flag is still set
;                               ; and the next paint puts the card up
;
; The card is MEASURED from the strings themselves - widest line sets the
; width, count sets the height - and CLAMPED to the live content box, never
; pinned: SPEC.md 39's three adapters give three content widths for one
; window, and a card sized for VGA hangs out of a CGA's.
;
; It is drawn with UI_RUN and not a fill-then-letter pair, so no pixel of it
; is written twice (SPEC.md 6.1); the only fill is the ground the frame and
; the gaps between lines stand on.
; =============================================================================

OS88UI_ABLH   equ 12            ; the line pitch: an 8px cell and 4px of air
OS88UI_ABPADX equ 12            ; ...the margin either side of the widest line
OS88UI_ABPADY equ 7             ; ...and above the first cell, inside the frame

; -----------------------------------------------------------------------------
; os88ui_about - the standard About card, from your ABOUT HANDLER
; in:  BX = your window ptr, SI = your line table; the gfx lock is HELD
; out: CF = 1 nothing was drawn - not one pixel of your content is visible
; preserves every register
; -----------------------------------------------------------------------------
os88ui_about:
    call OSAPI_WM_CLIP_SET      ; see "why there are two entries" above. The
    jc .gone                    ; region dies at the kernel's own gfx_unlock,
    jmp short os88ui_about_d    ; so there is nothing here to undo
.gone:
    ret                         ; CF is set, and BX/SI are wm_clip_set's own

; -----------------------------------------------------------------------------
; os88ui_about_d - the same card, from your PAINTER, with a region already
;                  armed. Draw it LAST: it is opaque over its own rect and
;                  everything under it has been drawn by now.
; in:  BX = your window ptr, SI = your line table; the gfx lock is HELD
; out: CF = 1 the window is not visible and nothing was drawn
; preserves every register
; -----------------------------------------------------------------------------
os88ui_about_d:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_abmeas
    jc .out                     ; a pop writes no flags, so the CF set by
    call os88ui_abdraw          ; os88ui_abmeas survives the seven below
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_abmeas - size and centre the card from the strings (internal)
; in:  BX = window, SI = line table
; out: [os88ui_abx]/[os88ui_aby]/[os88ui_abw]/[os88ui_abh], SCREEN coords;
;      CF = 1 the window is not visible
; -----------------------------------------------------------------------------
os88ui_abmeas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top - the
    mov [os88ui_abx], ax        ; ORIGIN, banked now because the size call
    mov [os88ui_aby], dx        ; below is about to want DX for the height
    call OSAPI_WM_GEOM          ; CX = content width, DX = content height
    jc .gone                    ; ...and CF = 1 if the window is not visible
    push cx
    push dx

    xor di, di                  ; DI = the widest line in pixels...
    xor bp, bp                  ; ...BP = how many there are. BP as a REGISTER
.m:                             ; is free; it is [bp+disp] that addresses SS
    mov ax, [si]
    or ax, ax
    jz .mdone
    add si, 2
    push si
    mov si, ax
    UI_WIDTH                    ; AX = this line's pixels; clobbers nothing
    pop si                      ; else, so DI and BP ride through it
    inc bp
    cmp ax, di
    jbe .m
    mov di, ax
    jmp short .m
.mdone:
    pop dx                      ; the content height...
    pop cx                      ; ...and its width

    push cx                     ; HEIGHT FIRST, because the multiply below
    mov ax, bp                  ; wants DX and the width does not
    push dx
    mov bx, OS88UI_ABLH
    mul bx                      ; AX = lines * the pitch (DX:AX, and a card
    pop dx                      ; taller than 5,461 lines is not a card)
    add ax, OS88UI_ABPADY * 2
    cmp ax, dx
    jbe .hok                    ; CLAMPED to the content box, never pinned:
    mov ax, dx                  ; one window is three content sizes across
.hok:                           ; SPEC.md 39's three adapters
    mov [os88ui_abh], ax
    sub dx, ax
    shr dx, 1
    add [os88ui_aby], dx

    pop cx                      ; ...and the width, the same way
    mov ax, di
    add ax, OS88UI_ABPADX * 2
    cmp ax, cx
    jbe .wok
    mov ax, cx
.wok:
    mov [os88ui_abw], ax
    sub cx, ax
    shr cx, 1
    add [os88ui_abx], cx
    clc
.gone:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_abdraw - the card itself (internal)
; in:  the four words above are current, SI = line table; gfx lock held
; preserves every register
;
; White ground, black frame, black-on-white text: the one pairing that says
; the same thing on all three adapters (SPEC.md 39.4), where a grey would
; round to black on two of them.
; -----------------------------------------------------------------------------
os88ui_abdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call .rect
    UI_WHITE
    UI_FILL
    call .rect                  ; again: a primitive owns its four registers
    UI_BLACK
    UI_FRAME

    mov di, [os88ui_aby]
    add di, OS88UI_ABPADY       ; the first cell's top, inside the frame
.l:
    mov ax, [si]
    or ax, ax
    jz .out
    add si, 2
    push si
    mov si, ax
    UI_WIDTH                    ; AX = its pixels
    mov cx, [os88ui_abw]
    sub cx, ax
    jns .cok                    ; a line WIDER than the clamped card would
    mov cx, OS88UI_ABPADX * 2   ; otherwise centre itself off the LEFT edge -
.cok:                           ; shr on a negative is a large positive - so it
    shr cx, 1                   ; starts at the margin and clips at the frame.
                                ; Keep the lines short: the card is clamped to
                                ; the content box and a CGA's is the narrow one
    add cx, [os88ui_abx]        ; CX = the pen x, DX = the pen y
    mov dx, di
    mov ax, (CWHITE << 8) | CBLACK
    UI_RUN                      ; OPAQUE (SPEC.md 6.1): the cell's ground and
    pop si                      ; its glyph in one pass, so no pixel of the
    add di, OS88UI_ABLH         ; card is written twice
    jmp short .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.rect:                          ; the card as (x1,y1)-(x2,y2), INCLUSIVE -
    mov ax, [os88ui_abx]        ; gfx_frame's convention, which gfx_fill
    mov bx, [os88ui_aby]        ; shares
    mov cx, ax
    add cx, [os88ui_abw]
    dec cx
    mov dx, bx
    add dx, [os88ui_abh]
    dec dx
    ret

; --- the card's rect, INLINE DATA and not bss, for os88ui_armw's reason ------
; It is scratch between os88ui_abmeas and os88ui_abdraw and nothing reads it
; afterwards: both run on the UI task with the gfx lock held, which is the
; whole of the mutual exclusion this needs.
os88ui_abx: dw 0
os88ui_aby: dw 0
os88ui_abw: dw 0
os88ui_abh: dw 0
%endif                          ; OS88UI_ABOUT

%ifdef OS88UI_CHK
%ifdef OS88UI_NOBTN
  %error "OS88UI_CHK draws its box through os88ui_glyph (SPEC.md 13.15.2), which OS88UI_NOBTN leaves out"
%endif
; =============================================================================
; THE CHECK BOX - the fourth shared element (OS88UI_CHK), SPEC.md 13.15
;
; Opt in with `%define OS88UI_CHK` before the include and it costs you a
; dozen bytes of record and the code below; a package that does not costs
; nothing at all. The look is the kernel's own: a square box, a solid mark
; when it is on, and the label to its right in the ordinary pen.
;
; --- THE RECORD, in YOUR data -----------------------------------------------
;   my_check:
;       dw 0, 0, 0, 0           ; OS88UI_CK_RECT: the WHOLE clickable area,
;                               ; {x1,y1,x2,y2} inclusive, SCREEN coords - the
;                               ; box is drawn at its left and the label
;                               ; follows, so a press on either counts
;       dw my_label             ; OS88UI_CK_LABEL: a NUL string
;       db 0                    ; OS88UI_CK_ON: 0 or 1 - yours to read and to
;                               ; set before a paint
;
; --- USING IT ---------------------------------------------------------------
;   in W_PAINT:      BX = my_check, DI = flags (OS88UI_DIS greys it), and
;                    `call os88ui_chk`. THE CALLER OWNS THE PANE'S GROUND
;                    (SPEC.md 13.15.2): the label is an opaque font_run and
;                    the box is cleared by os88ui_glyph, so nothing else
;                    inside CK_RECT is written - lay the pane first
;   in W_ONCLICK:    BX = my_check, CX/DX = the point, `call os88ui_chkhit`.
;                    out CF = 0 it was hit AND TOGGLED - the box is redrawn
;                    where it stands, so there is nothing to repaint - or
;                    CF = 1 the press was not ours
; =============================================================================
OS88UI_CK_RECT  equ 0
OS88UI_CK_LABEL equ 8
OS88UI_CK_ON    equ 10
OS88UI_CK_SIZE  equ 12
OS88UI_CKBOX    equ OS88UI_GW   ; the box's side, and the label's left margin.
                                ; It was 11 and is the GLYPH's 12 now: the two
                                ; shared controls draw the same square through
                                ; the same routines (SPEC.md 13.15.1), and a
                                ; size of its own was the last place they could
                                ; still disagree
OS88UI_CKGAP    equ 6

; -----------------------------------------------------------------------------
; os88ui_chk - draw it as it stands
; in:  BX = the record, DI = flags (OS88UI_DIS); the gfx lock is held
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
os88ui_chk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx
    call os88ui_chkpen          ; the pen: SPEC.md 47 rule 1, and it carries to
                                ; the label below and into the glyph's own
    mov si, [ds:bp+OS88UI_CK_LABEL]     ; --- the label, and OPAQUE (SPEC.md
    mov cx, [ds:bp+0]                   ; 6.1): font_run lays its own ground in
    add cx, OS88UI_CKBOX + OS88UI_CKGAP ; the pass that lays the glyph, so
    mov dx, [ds:bp+2]                   ; nothing has to be blanked for it -
    add dx, [ds:bp+6]                   ; which is the whole of why this
    sub dx, 7                           ; routine no longer fills its rect
    sar dx, 1
    mov al, CBLACK              ; the DIS state rides on the pen, not on AL
    mov ah, CWHITE
    UI_RUN
    call os88ui_chkdraw         ; ...and the box and the mark, through the
    clc                         ; GLYPH (SPEC.md 13.15.2)
    UI_PEN                      ; the pen back, always
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_chkdraw - internal: the box and the mark, through os88ui_glyph
; in:  BP = the record, DI = flags; out: nothing. Every register preserved
;
; IT DRAWS NOTHING ITSELF, and that is the point (SPEC.md 13.15.2). A check box
; and the Control Panel's check glyph are the same picture, and the version of
; this file that drew them separately had them at DIFFERENT SIZES - 11 against
; 12 - for years without anyone noticing. One caller, one picture, no second
; copy to drift; and because the glyph is already in every package that has a
; button, sharing it costs nothing anywhere.
; -----------------------------------------------------------------------------
os88ui_chkdraw:
    push ax
    push cx
    push dx
    mov al, OS88UI_GCHECK
    cmp byte [ds:bp+OS88UI_CK_ON], 0
    je .off
    or al, OS88UI_GON
.off:
    xor ah, ah                  ; AH non-zero = disabled, which is the glyph's
    test di, OS88UI_DIS         ; own argument shape
    jz .live
    mov ah, 1
.live:
    call os88ui_chkbox          ; CX = x, DX = y
    call os88ui_glyph
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_chkbox - internal: the box's top-left, square and centred on the row
; in:  BP = the record; out: CX = x, DX = y. AX and BX preserved
; -----------------------------------------------------------------------------
os88ui_chkbox:
    push ax
    mov ax, [ds:bp+2]
    add ax, [ds:bp+6]
    sub ax, OS88UI_CKBOX - 1
    sar ax, 1
    mov dx, ax
    mov cx, [ds:bp+0]
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_chkpen - internal: live or greyed, from DI's OS88UI_DIS
; -----------------------------------------------------------------------------
os88ui_chkpen:
    test di, OS88UI_DIS
    jz .live
    stc
    jmp short .set
.live:
    clc
.set:
    UI_PEN
    ret

; -----------------------------------------------------------------------------
; os88ui_chkmark - the BOX and the MARK, and NOT the label (SPEC.md 13.15.2)
; in:  BX = the record, DI = flags; the gfx lock is held
; out: nothing; every register preserved
;
; os88ui_raddot's twin, and for its reason: a toggle changes a 6x6 square and
; NOTHING ELSE - the frame is identical and the label is identical - so
; redrawing the control would re-letter text that did not change. It takes no
; set-or-clear argument, reading OS88UI_CK_ON itself, so the caller writes the
; byte and then says "redraw the mark".
; -----------------------------------------------------------------------------
os88ui_chkmark:
    push bp
    mov bp, bx
    call os88ui_chkdraw
    pop bp
    ret

; -----------------------------------------------------------------------------
; os88ui_chkhit - a press: over it, the box toggles and redraws itself
; in:  BX = the record, CX = x, DX = y; the gfx lock is held
; out: CF = 0 it was ours and has been toggled and redrawn, CF = 1 not ours.
;      Every register preserved
; -----------------------------------------------------------------------------
os88ui_chkhit:
    push ax
    call os88ui_bhit            ; the button's own hit test: the rect is at
    jc .out                     ; the same offset and means the same thing
    mov al, [bx+OS88UI_CK_ON]
    xor al, 1
    mov [bx+OS88UI_CK_ON], al
    push di
    xor di, di
    call os88ui_chkmark         ; SPEC.md 13.15.2: the MARK, not the control -
    pop di                      ; the frame and the label did not change
    clc
.out:
    pop ax
    ret

%endif                          ; OS88UI_CHK

%ifdef OS88UI_RAD
%ifdef OS88UI_NOBTN
  %error "OS88UI_RAD draws its ring and dot through os88ui_gring/os88ui_gdot and hit-tests with os88ui_bhit (SPEC.md 13.17.1), which OS88UI_NOBTN leaves out"
%endif
; =============================================================================
; THE RADIO GROUP - the SIXTH shared element (OS88UI_RAD), SPEC.md 13.17
;
; Opt in with `%define OS88UI_RAD` before the include; a package that does not
; costs NOTHING AT ALL, which is os88ui_chk's rule and the whole shape of this
; file.
;
; --- WHY IT IS A GROUP AND NOT A BUTTON --------------------------------------
; A check box is independent and a radio is not: picking one UNPICKS a sibling,
; so a control that draws one button cannot do the only thing a radio is for.
; The record is therefore the whole GROUP - which is os88ui_drop's shape
; (SPEC.md 13.14), and deliberately: a drop-down IS "one pick out of a short
; list" and so is this, so the two carry the same three fields (ITEMS, N, SEL)
; and differ in presentation. A caller can change its mind about which control
; a setting wears without changing its data.
;
; It also buys the redraw. `os88ui_radhit` knows both the old pick and the new
; one, so it repaints EXACTLY TWO ROWS and never the group - which matters
; here rather than in the abstract: PERFORMANCE.md prices a drawing call's
; fixed part at ~756us on the field machine, so a five-row group repainted
; whole is tens of milliseconds to move one dot.
;
; --- THE LOOK, and why it is not the check box's square ----------------------
; A 12x12 ROUNDED BOX - a rectangle with one pixel off each corner - with a
; 6x6 rounded dot in the middle when it is picked. It is the same 12 as
; OS88UI_GW, so a converted call site keeps its geometry to the pixel.
;
; Three things decided the shape and none of them is taste:
;
;   - IT MUST NOT BE A SQUARE. os88ui_chk's mark is a solid square and a radio
;     that is also square says nothing about being one-of-many. At 12px a
;     one-pixel corner cut is what a circle rasterises to, and it is what small
;     1bpp interfaces have always drawn.
;   - IT MUST NOT BE A THIN DIAGONAL OR A DITHER. SPEC.md 39.4: grey rounds to
;     black on both 1bpp adapters, so a dithered ring reads as dotted and a
;     diagonal reads as noise. Every edge here is a horizontal or vertical RUN.
;   - THE RING IS ONE PIXEL because os88ui_chk's frame is one pixel. A heavier
;     ring is more legible greyed and would be the odd control out in a column
;     that has both; matching the neighbour won.
;
; --- WHAT IT COSTS TO DRAW, honestly -----------------------------------------
; Six drawing calls for an unpicked row and nine for the picked one - the
; ground, four runs for the ring, three for the dot, and the label. That is
; MORE than os88ui_glyph's normal path, which is ONE masked-sprite call plus
; the caller's own label, and the trade is taken on the owner's terms: this is
; drawn once and then sits there until someone interacts with it.
;
; What it wins back is the OTHER path. The bitmap os88ui_glyph could not draw
; a control a clip fragment's edge cut - ico_core clips an icon WHOLE - so it
; fell to `.gpix`, ONE DRAWING CALL PER SET BIT: 45 to 65 of them, 24-34 ms
; for one control (PERFORMANCE.md Set 83). A fill clips per pixel, so this
; control has no such path and no such twin to keep in step. One cost instead
; of two, and the bad one is gone - and the remaining one came DOWN as well,
; which nobody expected: the fill glyph measures 4.3-6.4 ms against the
; bitmap's 6.0-6.2, three of its four kinds cheaper and only the set radio
; dearer, by 0.39 ms (docs/reports/GLYPH-AND-LINE-COST-2026-09-10.md).
;
; --- THE RECORD, in YOUR data (one per GROUP) -------------------------------
;   my_radio:
;       dw 0, 0, 0, 0           ; OS88UI_RD_RECT: the WHOLE clickable area,
;                               ; {x1,y1,x2,y2} inclusive, SCREEN coords - fill
;                               ; it in your painter from OSAPI_WM_CONTENT,
;                               ; since a window moves. Row i is PITCH tall
;                               ; from y1; x2 is where the labels' ground ends
;       dw my_items             ; OS88UI_RD_ITEMS: near ptrs to NUL strings,
;                               ; an app menu's AMENU_ITEMS shape
;       dw 3                    ; OS88UI_RD_N: how many, 1..OS88UI_RDMAX
;       dw 0                    ; OS88UI_RD_SEL: the pick, 0-based - yours to
;                               ; read after a press and to set before a paint
;       dw 16                   ; OS88UI_RD_PITCH: row to row, in pixels. IN
;                               ; THE RECORD and not a constant because it
;                               ; varies by PAGE in the first caller that
;                               ; wanted it - the Control Panel's are 16, 20
;                               ; and 16 - so a library constant would have
;                               ; moved existing pixels to save two bytes
;       dw 0                    ; OS88UI_RD_DIS: bit i greys item i (SPEC.md 47
;                               ; rule 2), and BIT 15 IS THE WHOLE GROUP -
;                               ; os88ui_rad writes that bit from its own DI so
;                               ; the press half can refuse without being
;                               ; passed a flag it would have to be given
;                               ; twice. os88ui_drop's OS88UI_DR_DIS is the
;                               ; same trick and is why this is not new
;
; --- USING IT ----------------------------------------------------------------
;   in W_PAINT:      BX = my_radio, DI = flags (OS88UI_DIS greys ALL of it, on
;                    top of the per-item bits), and `call os88ui_rad`. It
;                    draws its own labels, and THE CALLER OWNS THE GROUND
;                    (SPEC.md 13.17.4): nothing outside the rings, the dots
;                    and the labels is written - lay the pane first
;   in W_ONCLICK:    BX = my_radio, CX/DX = the point, `call os88ui_radhit`.
;                    out CF = 1 the press was not ours - and NOTHING ELSE means
;                    anything in that case. CF = 0 it was ours, and then
;                    ZF = 1 exactly when the pick MOVED: the two rows that
;                    changed have been redrawn where they stand, so there is
;                    nothing to repaint, and OS88UI_RD_SEL is the new pick.
;                    ZF = 0 is a press on the pick that was already made, or on
;                    a greyed row - ours, swallowed, nothing drawn and nothing
;                    for the caller to act on
; =============================================================================
OS88UI_RD_RECT  equ 0
OS88UI_RD_ITEMS equ 8
OS88UI_RD_N     equ 10
OS88UI_RD_SEL   equ 12
OS88UI_RD_PITCH equ 14
OS88UI_RD_DIS   equ 16
OS88UI_RD_SIZE  equ 18

OS88UI_RDBOX    equ 12          ; the ring's box - OS88UI_GW, so a converted
                                ; call site keeps its geometry to the pixel
OS88UI_RDGAP    equ 6           ; ...and the label's margin, os88ui_chk's
OS88UI_RDMAX    equ 8           ; bits 0..7 of OS88UI_RD_DIS; 15 is the group
OS88UI_RDGRP    equ 0x8000      ; ...that bit, by name
OS88UI_RDROWH   equ 20          ; **THE RING IS CENTRED IN THIS, NOT IN THE
                                ; PITCH**, when the pitch is bigger (SPEC.md
                                ; 13.17.5). A pitch is row-to-row and is
                                ; ordinarily a row's own height - the Control
                                ; Panel's three are 16, 20 and 16, all at or
                                ; under this, so nothing that shipped moves by
                                ; a pixel. A caller whose arms have
                                ; SUBSECTIONS between them (apps/dos's memory
                                ; page, SPEC.md 96.36.4) has a pitch of sixty,
                                ; and centring in that put the ring and its
                                ; label a quarter of the way down the
                                ; subsection - on top of the controls that
                                ; belong to it. The cap is what makes a tall
                                ; pitch mean "the next HEADING is this far
                                ; down" rather than "this row is that tall"

; -----------------------------------------------------------------------------
; os88ui_rad - draw the whole group as it stands
; in:  BX = the record, DI = flags (OS88UI_DIS); the gfx lock is held
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
os88ui_rad:
    push ax
    push cx
    push si
    mov si, [bx+OS88UI_RD_DIS]  ; bank the group's own bit from DI FIRST, so
    and si, 0x7FFF              ; the press half reads what the paint decided
    test di, OS88UI_DIS         ; (SPEC.md 13.14.5's trick, one control along)
    jz .live
    or si, OS88UI_RDGRP
.live:
    mov [bx+OS88UI_RD_DIS], si
    xor si, si
    mov cx, [bx+OS88UI_RD_N]
    jcxz .out                   ; a group of none draws nothing, rather than
.row:                           ; `loop` wrapping the count round to 65,536
    call os88ui_rad1
    inc si
    loop .row
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_rad_pen - internal: the pen this row draws in (SPEC.md 47 rule 1)
; in:  BP = the record, SI = the row; out: the pen is set. AX and CX preserved
; -----------------------------------------------------------------------------
os88ui_rad_pen:
    push ax
    push cx
    mov ax, 1
    mov cx, si
    jcxz .havebit
.shift:
    shl ax, 1                   ; (8086: no shift by an immediate but 1)
    loop .shift
.havebit:
    test ax, [ds:bp+OS88UI_RD_DIS]
    jnz .grey
    test word [ds:bp+OS88UI_RD_DIS], OS88UI_RDGRP
    jnz .grey
    clc                         ; the PEN carries it, and it carries it to the
    jmp short .set              ; ring, the dot and the label at once - which
.grey:                          ; is why a fill-drawn mark needs no dither
    stc                         ; table of its own, where os88ui_glyph composes
.set:                           ; one row by row
    UI_PEN
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_rad_dt - internal: the 6x6 rounded dot, in whatever the pen is
; in:  BP = the record, DI = the row's y; the pen is already set
;
; Three runs, and it is drawn in the GROUND colour to clear rather than being
; erased and left blank - which is the whole of SPEC.md 13.17.4.
; -----------------------------------------------------------------------------
os88ui_rad_dt:
    push cx
    push dx
    mov cx, [ds:bp+OS88UI_RD_RECT+0]
    mov dx, di
    call os88ui_gdot            ; SPEC.md 13.15.1's shape, shared: there is one
    pop dx                      ; radio look in this file and not two
    pop cx
    ret

; -----------------------------------------------------------------------------
; os88ui_radboxy - DI = the row's top -> DI = the ring's top, centred in the row
; in:  BP = the record, DI = RECT.y1 + row * PITCH; out: DI; AX clobbered
;
; The label is drawn at (PITCH - 7) >> 1 below the row's top, and the first
; cut drew the ring AT the row's top: the two centres coincide only at a
; pitch of 11 or 12, and at the pitches SPEC.md 13.17 names - 16 and 20 - the
; ring sat 2 and 4 pixels above the middle of its own label. (PITCH - 12) >> 1
; puts the ring's centre where the label's is, to the half pixel both
; roundings carry; a pitch under the box keeps the ring at the row's top
; rather than lifting it out.
; -----------------------------------------------------------------------------
os88ui_radboxy:
    call os88ui_radrowh         ; AX = the pitch, capped at OS88UI_RDROWH
    sub ax, OS88UI_RDBOX
    jle .top
    sar ax, 1
    add di, ax
.top:
    ret

; os88ui_radrowh - internal: AX = how tall a ROW is, which is the pitch until
;                  the pitch is bigger than a row (SPEC.md 13.17.5)
; in:  BP = the record; out: AX. Every other register preserved
os88ui_radrowh:
    mov ax, [ds:bp+OS88UI_RD_PITCH]
    cmp ax, OS88UI_RDROWH
    jbe .ok
    mov ax, OS88UI_RDROWH
.ok:
    ret

; -----------------------------------------------------------------------------
; os88ui_rad1 - ONE row, WITHOUT erasing anything first
; in:  BX = the record, SI = the row, 0-based; the gfx lock is held
; out: nothing; every register preserved
;
; NOTHING HERE IS DRAWN TWICE AND NOTHING IS BLANKED (SPEC.md 13.17.4). The
; label is an OPAQUE font_run, so it lays its own ground in the same pass that
; lays the glyph; the ring is four runs; the dot is three. No pixel is written
; by two of them, so there is no interval in which the row is blank - which on
; a 4.77 MHz 8088 is not a nicety, since a whole-row fill followed five drawing
; calls later by the label is about four milliseconds of empty row, every
; repaint, and PERFORMANCE.md Part 1 calls that class of defect invisible in an
; emulator.
;
; THE CALLER OWNS THE GROUND, which is os88ui_drop's contract (SPEC.md 13.14)
; rather than os88ui_chk's. The pane is already filled by whoever laid it out -
; every Control Panel page does it with os88ui_krect - so filling it again here
; would be the second write this routine exists not to make.
;
; It reads OS88UI_RD_DIS rather than taking a flag argument, which is what lets
; the press half redraw without being handed the painter's DI.
;
; X IS RE-READ FROM THE RECORD at every step rather than held in a register.
; UI_FILL wants AX,BX,CX,DX = x1,y1,x2,y2, so a register holding x would be
; pushed and popped around all seven of them; a memory read through BP is one
; instruction, no register, and cannot get out of step.
; -----------------------------------------------------------------------------
os88ui_rad1:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                  ; BP = the record. A VALUE, never an address:
                                ; SS is not DS in a package or in the kernel,
                                ; so every read below carries `ds:`

    mov ax, si                  ; --- the row's top ---
    mul word [ds:bp+OS88UI_RD_PITCH]
    add ax, [ds:bp+OS88UI_RD_RECT+2]
    mov di, ax                  ; DI = this row's y, for the rest of the body
    call os88ui_radboxy         ; ...and the RING's, centred in the row the
                                ; way the label below is (SPEC.md 13.17.1)

    call os88ui_rad_pen

    mov cx, [ds:bp+OS88UI_RD_RECT+0]    ; --- the ring, shared with
    mov dx, di                          ; os88ui_glyph (SPEC.md 13.15.1)
    call os88ui_gring

    cmp si, [ds:bp+OS88UI_RD_SEL]       ; --- the dot: 6x6, rounded the same
    jne .label                          ; way, and centred exactly - 1 + 2 + 6
    call os88ui_rad_dt                  ; + 2 + 1 is the box's own 12

.label:
    mov ax, si                  ; --- the label, OPAQUE (SPEC.md 6.1) ---
    shl ax, 1                   ; SI is still the ROW here; it becomes the
    add ax, [ds:bp+OS88UI_RD_ITEMS]     ; string only now that nothing else
    mov si, ax                          ; wants the index
    mov si, [si]
    mov cx, [ds:bp+OS88UI_RD_RECT+0]
    add cx, OS88UI_RDBOX + OS88UI_RDGAP
    push ax                             ; centred in the row, os88ui_chk's
    call os88ui_radrowh                 ; arithmetic - and in the ROW's height
    mov dx, ax                          ; rather than in the pitch, which is
    pop ax                              ; not the same thing once a caller puts
    sub dx, 7                           ; a whole subsection between two arms
    sar dx, 1
    add dx, di
    mov al, CBLACK              ; the DIS state rides on the pen, not on AL -
    mov ah, CWHITE              ; os88ui_chk's convention, and it is why the
    UI_RUN                      ; label greys with the ring for free

    clc
    UI_PEN                      ; the pen back, always
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_raddot - ONE row's DOT and nothing else: not the ring, not the label
; in:  BX = the record, SI = the row, 0-based; the gfx lock is held
; out: nothing; every register preserved
;
; THE WHOLE POINT OF THE CONTROL'S REDRAW (SPEC.md 13.17.4). Moving a pick
; changes six pixels' worth of shape in two rows and NOTHING ELSE - the rings
; are identical, the labels are identical, the ground between them is
; identical - so re-lettering either row would be re-drawing text that did not
; change, which is exactly the class of waste SPEC.md 6.1 is about.
;
; It takes no "set or clear" argument: it draws in the ink when this row IS the
; pick and in the ground when it is not, so the caller writes SEL first and
; then names the two rows. The row that lost the pick has its dot painted
; OVER in the ground colour rather than being blanked and left - one write,
; no interval.
; -----------------------------------------------------------------------------
os88ui_raddot:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx
    mov ax, si
    mul word [ds:bp+OS88UI_RD_PITCH]
    add ax, [ds:bp+OS88UI_RD_RECT+2]
    mov di, ax
    call os88ui_radboxy         ; the dot sits in the ring, wherever that is
    cmp si, [ds:bp+OS88UI_RD_SEL]
    je .ink
    UI_WHITE                    ; a STORE, not a call: the ground colour is
    jmp short .go               ; where gfx_color lives
.ink:
    call os88ui_rad_pen
.go:
    call os88ui_rad_dt
    clc
    UI_PEN                      ; the pen back, always
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_radhit - a press: the row under it becomes the pick and TWO rows are
;                 redrawn where they stand
; in:  BX = the record, CX = x, DX = y; the gfx lock is held
; out: CF = 1 the press was not ours, and nothing else means anything.
;      CF = 0 it was ours, and ZF = 1 exactly when the pick MOVED - in which
;      case OS88UI_RD_SEL is the new one and the screen is already right.
;      ZF = 0 is a press on the pick already made, on a greyed row, or on the
;      slack below the last row: ours, swallowed, nothing drawn.
;      Every register preserved.
;
; `pop` writes no flags, so the answer set before the epilogue survives it.
; -----------------------------------------------------------------------------
os88ui_radhit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_bhit            ; the button's own hit test: the rect is at the
    jc .no                      ; same offset and means the same thing
    mov bp, bx
    test word [ds:bp+OS88UI_RD_DIS], OS88UI_RDGRP
    jnz .swallow                ; greyed whole (SPEC.md 13.14.5): it is still
                                ; ours, and nothing happens
    cmp word [ds:bp+OS88UI_RD_PITCH], 0
    je .swallow                 ; a pitch of zero is a caller's bug, and `div`
                                ; answers one with INT 0 - five bytes turn a
                                ; crash into a press that does nothing
    mov ax, dx
    sub ax, [ds:bp+OS88UI_RD_RECT+2]
    xor dx, dx
    div word [ds:bp+OS88UI_RD_PITCH]
    cmp ax, [ds:bp+OS88UI_RD_N]
    jae .swallow                ; inside the rect but below the last row
    mov si, ax

    mov ax, 1                   ; ...and this row may be greyed on its own
    mov cx, si
    jcxz .hb
.hs:
    shl ax, 1
    loop .hs
.hb:
    test ax, [ds:bp+OS88UI_RD_DIS]
    jnz .swallow

    cmp si, [ds:bp+OS88UI_RD_SEL]
    je .swallow                 ; already the pick: ours, and NOT a change -
                                ; which is the distinction a caller acting on
                                ; the answer needs, since re-applying a mode
                                ; the machine is already in is not free
    mov di, [ds:bp+OS88UI_RD_SEL]
    mov [ds:bp+OS88UI_RD_SEL], si
    push si
    mov si, di
    call os88ui_raddot          ; the row that LOST the pick...
    pop si
    call os88ui_raddot          ; ...and the one that has it now. Two DOTS, not
    xor ax, ax                  ; the group: ZF = 1, CF = 0
    jmp short .out
.swallow:
    mov ax, 1
    or ax, ax                   ; ZF = 0, CF = 0
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                          ; OS88UI_RAD

%ifdef OS88UI_DROP
; =============================================================================
; THE DROP-DOWN (SPEC.md 13.14): one pick out of a short list
;
; A closed box showing the pick, with a rule and a small triangle in a cell at
; its right end; pressed, it DROPS a list of every item under the box, over
; whatever your content had there; a press on an item picks it and the list
; goes, a press anywhere else just takes the list down. Press, drag onto an
; item and release picks too - the Macintosh gesture - and the highlight
; follows the pointer meanwhile. CLEAR SKIES' plane and airport (SPEC.md
; 88.10) are the first two, and they are why it is here rather than in the
; kernel: a control with a list is ~450 bytes, no kernel dialog wants one,
; and a package pays for it only by defining OS88UI_DROP. The look is the
; kernel's own pull-down (SPEC.md 12): white ground, black frame, 12px cells,
; the current item inverted - DRAWN inverted, never XOR-ed, for the reason
; os88ui_btn's block comment gives about OS88UI_DOWN.
;
; --- THE RECORD, in YOUR data (it holds the open state, so two coexist) -----
;   my_drop:
;       dw 0, 0, 0, 0           ; OS88UI_DR_RECT: the CLOSED box, {x1,y1,x2,y2}
;                               ; inclusive, SCREEN coords - you fill it in
;                               ; your painter, from OSAPI_WM_CONTENT, since a
;                               ; window moves
;       dw my_items             ; OS88UI_DR_ITEMS: near ptrs to NUL strings,
;                               ; an app menu's AMENU_ITEMS shape
;       dw 3                    ; OS88UI_DR_N: how many, 1..OS88UI_DRMAX
;       dw 0                    ; OS88UI_DR_SEL: the pick, 0-based - yours to
;                               ; read after a pick and to set before a paint
;       dw 0                    ; OS88UI_DR_WIN: your window ptr, once created
;       db 0, 0FFh              ; OS88UI_DR_OPEN, OS88UI_DR_HOT: the control's
;       dw 0, 0                 ; OS88UI_DR_SEG, OS88UI_DR_KB: the banked
;                               ; pixels under an open list
;       dw 0                    ; OS88UI_DR_TOP: where the open list starts
;       dw 0                    ; OS88UI_DR_DIS: set from the painter's
;                               ; OS88UI_DIS, so the press half can refuse
;                               ; (13.14.5). All six are the control's own;
;                               ; leave them - and DECLARE them: the record
;                               ; is OS88UI_DR_SIZE bytes and the library
;                               ; writes every one of them
;
; --- USING IT ----------------------------------------------------------------
;   in W_PAINT:      fill the rect, then BX = my_drop, DI = flags (OS88UI_DIS
;                    greys it) and `call os88ui_drop` - it draws the box, and
;                    the list too when it is open, so a repaint of any cause
;                    puts the control back exactly as it stands
;   in W_ONCLICK:    BX = my_drop, CX/DX = the point, `call os88ui_drpress`.
;                    A control last PAINTED with OS88UI_DIS refuses the press
;                    and does not spend it (AH = 0), so a greyed box behaves
;                    as though it were not there.
;                    out AH = 1 the press was SPENT here (do not hand it to
;                    anything else), AL = the new pick or 0FFh, CF = 1 REPAINT
;                    YOUR CONTENT - the list came down over what it covered.
;                    A press that opens the list draws it itself, under a
;                    clip it arms from OS88UI_DR_WIN, and answers CF = 0
;   in W_ONDRAG:     BX = my_drop, CX/DX, `call os88ui_drdrag` - the
;                    highlight follows the pointer; nothing to test
;   in W_ONMOUSEUP:  BX = my_drop, CX/DX, `call os88ui_drup` - the release
;                    over an item picks it; the answers are os88ui_drpress's
;   Esc, a menu, anything that should take an open list down:
;                    BX = my_drop, `call os88ui_drclose`, THE GFX LOCK HELD -
;                    which every handler that can reach it already is
;                    (W_ONKEY, AM_ONCMD and W_ONCLICK all run under it)
;
; CF = 1 OUT OF drpress, drup AND drclose MEANS REPAINT, AND IT IS NOW THE
; UNCOMMON ANSWER. Opening the list banks the pixels it covers (SPEC.md
; 5.3's published save-under, 13.14.1) and taking it down writes them back,
; so the close is one blit rather than a repaint of everything the list lay
; over. Keep the repaint anyway: it is what you get when the heap has no room
; for the bank, when the rect straddles two displays, and after any repaint
; of your own - a bank taken before it describes pixels that have gone.
;
; A bank is pixels and not a promise about them, so it shares one window with
; the kernel's own menu drop (SPEC.md 12.4) and with Word's (68.2): if
; something else draws inside the rect between the open and the close - a
; window under yours repainting on its own account - the write-back puts the
; older pixels back over it. What keeps that narrow is that drawing is
; serialised by the gfx lock and that any repaint of your own drops the bank;
; what makes it acceptable is that it is the window three shipped
; save-unders already stand in.
;
; A press on a greyed control is yours to refuse before calling: the record
; carries no disabled bit because the painter's flag is the one place that
; state is decided, which is os88ui_btn's rule. Size the box to the widest
; item plus the arrow cell: the caption is one opaque run and is not clipped.
; =============================================================================

OS88UI_DR_RECT  equ 0
OS88UI_DR_ITEMS equ 8
OS88UI_DR_N     equ 10
OS88UI_DR_SEL   equ 12
OS88UI_DR_WIN   equ 14
OS88UI_DR_OPEN  equ 16
OS88UI_DR_HOT   equ 17
OS88UI_DR_SEG   equ 18          ; the banked pixels under the open list, or 0
OS88UI_DR_KB    equ 20          ; ...and the claim's size, to give back
OS88UI_DR_TOP   equ 22          ; the OPEN list's first row, screen (13.14.2):
                                ; the row under the box, or higher where a long
                                ; list would not fit the window below it
OS88UI_DR_DIS   equ 24          ; the control's own: NON-ZERO while it is
                                ; DISABLED, written by os88ui_drop from the
                                ; painter's OS88UI_DIS so that the press half
                                ; can refuse (SPEC.md 13.14.5). It is stored
                                ; rather than passed because a caller that
                                ; forgot to pass it would open a control it
                                ; had just drawn greyed, which is the defect
                                ; this field exists for
OS88UI_DR_SIZE  equ 26
%ifndef OS88UI_DRIH
%define OS88UI_DRIH 12          ; an item's pitch: an 8px cell and 4px of air,
%endif                          ; the About card's line and a menu's look -
                                ; and OVERRIDABLE, because a package converting
                                ; an existing control has a look to keep. WORD
                                ; defines 10 before the include: its ribbon
                                ; combos were pseudo-menus (SPEC.md 68.2) and a
                                ; menu's item band is OS88UI_MN_IH = 10, so
                                ; taking the default would have made every
                                ; list two pixels taller per item on a port
                                ; whose whole point is Word 1.1a's look. It is
                                ; a LOOK constant and not a mechanism: nothing
                                ; here reads it but the hit test and the
                                ; painter, and they read the same one
OS88UI_DRAW     equ 12          ; the arrow cell's width
OS88UI_DRMAX    equ 12          ; items a list may hold (12 x 12 = 144 rows)

; -----------------------------------------------------------------------------
; os88ui_drop - draw the control as it stands: the box, and the list if open
; in:  BX = the record, DI = flags (OS88UI_DIS); the gfx lock is held
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
os88ui_drop:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                  ; BP = the record (SS != DS: ds: below)
    mov ax, di                  ; WHAT IT WAS DRAWN AS, REMEMBERED (13.14.5):
    and ax, OS88UI_DIS          ; the press half has no flags argument and
    mov [ds:bp+OS88UI_DR_DIS], al   ; must not open a greyed control
    call os88ui_drbox
    cmp byte [ds:bp+OS88UI_DR_OPEN], 0
    je .shut
    call os88ui_drfit           ; where it goes, decided against the window as
                                ; it is NOW - a drag with a list open repaints
                                ; through here (13.14.2)
    call os88ui_drbank          ; what the list is about to cover, banked, so
    call os88ui_drlist          ; taking it down is a blit and not a repaint
    jmp short .out              ; (SPEC.md 13.14.1)
.shut:
    call os88ui_drfree          ; closed: a bank taken before this repaint
.out:                           ; describes pixels that are no longer there
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drbox - the CLOSED control: its ground, its frame, the arrow cell and
;                THE PICK'S CAPTION (internal)
; in:  BP = the record, DI = the painter's flags; the gfx lock is held
;
; It is its own routine because a pick has to redraw it. The list's
; write-back (13.14.1) puts back the pixels the list covered - which are
; below the box and do not include the caption - so without this the control
; came down showing the item it had before the press. That is what the whole
; repaint used to do on the way past.
; -----------------------------------------------------------------------------
os88ui_drbox:
    push ax
    push bx
    push cx
    push dx
    push si
    UI_WHITE
    mov ax, [ds:bp+0]           ; the interior, one pixel in from the frame
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    inc ax
    inc bx
    dec cx
    dec dx
    UI_FILL
    test di, OS88UI_DIS         ; the pen: SPEC.md 47 rule 1 in one call
    jz .live
    stc
    jmp short .set
.live:
    clc
.set:
    UI_PEN
    mov ax, [ds:bp+0]
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    UI_FRAME
    mov ax, [ds:bp+4]           ; the rule before the arrow cell...
    sub ax, OS88UI_DRAW
    mov bx, [ds:bp+2]
    inc bx
    mov dx, [ds:bp+6]
    dec dx
    call OSAPI_GFX_VLINE
    mov dx, [ds:bp+2]           ; ...and the triangle, four rows about the
    add dx, [ds:bp+6]           ; box's middle, 7 wide at the top
    shr dx, 1
    dec dx
    mov ax, [ds:bp+4]
    sub ax, OS88UI_DRAW / 2 + 3 ; AX = the top row's left end
    mov bx, ax
    add bx, 6
.tri:
    UI_HLINE
    inc ax
    dec bx
    inc dx
    cmp ax, bx
    jbe .tri
    ; the pick's caption, one opaque run (greyed by the pen, SPEC.md 6.1)
    mov si, [ds:bp+OS88UI_DR_SEL]
    shl si, 1
    add si, [ds:bp+OS88UI_DR_ITEMS]
    mov si, [si]
    mov cx, [ds:bp+0]
    add cx, 4
    mov dx, [ds:bp+6]
    sub dx, [ds:bp+2]
    inc dx
    sub dx, 8
    shr dx, 1
    add dx, [ds:bp+2]
    mov al, CBLACK
    mov ah, CWHITE
    UI_RUN
    clc
    UI_PEN                      ; the pen back, always
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drfit - WHERE THE OPEN LIST GOES (internal, SPEC.md 13.14.2)
; The list drops UNDER the box, and the control arms its clip off the window,
; so a list taller than the room below the box is simply cut off by the
; content's bottom edge. It slides UP by the overflow instead - the Macintosh
; popup's placement, and what lets a nine-item list live on a control near the
; foot of a 137-row page. A list taller than the whole content is put at the
; content's top and the clip takes the rest, which is the old behaviour on the
; only case it was ever right for.
; in:  BP = the record. out: OS88UI_DR_TOP written; every register preserved
; -----------------------------------------------------------------------------
os88ui_drfit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [ds:bp+6]
    inc ax                      ; the frame starts the row under the box...
    mov [ds:bp+OS88UI_DR_TOP], ax
    mov bx, [ds:bp+OS88UI_DR_WIN]
    or bx, bx
    jz .out                     ; no window yet: nothing to fit into
    call OSAPI_WM_CONTENT       ; DX = the content's first row
    mov si, dx
    call OSAPI_WM_GEOM          ; DX = its height (CF = 1: not visible)
    jc .out
    mov di, si
    add di, dx
    dec di                      ; DI = the content's LAST row
    mov ax, [ds:bp+OS88UI_DR_N]
    mov dl, OS88UI_DRIH
    mul dl
    add ax, 2                   ; AX = the list's rows, its frame included
    mov cx, di
    sub cx, ax
    inc cx                      ; CX = the highest first row that still fits
    cmp cx, si
    jge .fits
    mov cx, si                  ; taller than the content: the top of it
.fits:
    cmp [ds:bp+OS88UI_DR_TOP], cx
    jle .out                    ; where it wants to be is where it goes
    mov [ds:bp+OS88UI_DR_TOP], cx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drrect - the OPEN list's rect, the one os88ui_drlist draws (internal)
; in:  BP = the record
; out: AX/BX/CX/DX = x1/y1/x2/y2, inclusive, screen. Clobbers nothing else
; -----------------------------------------------------------------------------
os88ui_drrect:
    mov ax, [ds:bp+0]
    mov cx, [ds:bp+4]
    mov bx, [ds:bp+OS88UI_DR_TOP]   ; where os88ui_drfit put it (13.14.2)
    push ax
    mov ax, [ds:bp+OS88UI_DR_N]
    mov dl, OS88UI_DRIH
    mul dl                      ; ...and ends a row past the last cell
    add ax, bx
    inc ax
    mov dx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drbank - BANK THE PIXELS THE LIST IS ABOUT TO COVER (SPEC.md 5.3)
; in:  BP = the record, the gfx lock held; call it IMMEDIATELY before drawing
;      the list, so what it reads is what the list hides
; out: nothing; every register preserved. A failure - no heap, or a rect that
;      straddles two displays - simply leaves OS88UI_DR_SEG at 0, and the
;      close then repaints as it always did
;
; A repaint costs a fill and a text run per item and per cell of the content
; underneath; a write-back is one blit. Word measured its own dropdown's
; close at 521 ms against 19.7 (SPEC.md 68.2), and this control is that same
; trade published once for everybody.
; -----------------------------------------------------------------------------
os88ui_drbank:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call os88ui_drfree          ; a bank we still hold is last frame's
    call os88ui_drrect
    push ax                     ; --- bytes = planes x rows x byte columns ---
    push bx
    mov di, cx                  ; x2, BEFORE `mov cl, 3` takes CX's low byte -
    mov cl, 3                   ; the sizing reads x2 and the shift count out
    shr ax, cl                  ; of the same register (atui.inc's at_surban
    shr di, cl                  ; banks x2 the same way)
    sub di, ax
    inc di                      ; DI = byte columns
    sub dx, bx
    inc dx                      ; DX = rows
    mov ax, dx
    mul di                      ; AX = one plane's bytes (a list is far under
                                ; 64KB of one, OS88UI_DRMAX bounding it)
    mov bx, [ds:bp+OS88UI_DR_WIN]
    or bx, bx
    jz .none
    push ax                     ; ...and the plane count is banked across the
    call OSAPI_WM_DISPLAY       ; call: DH = the bpp OF THE DISPLAY WE ARE ON,
    mov bl, dh                  ; which OSAPI_VIDEO cannot answer on a two-card
    xor bh, bh                  ; machine (SPEC.md 39.16.4) - and CX comes back
    pop ax                      ; as the dock's first row (a buffer sized for
    cmp bl, 4                   ; the wrong one arrives as noise, 39.14.8)
    je .planes
    mov bx, 1
.planes:
    mul bx
    add ax, 1023
    mov cl, 10
    shr ax, cl                  ; AX = KB, rounded up
    or ax, ax
    jz .none
    mov [ds:bp+OS88UI_DR_KB], ax
    call OSAPI_MEM_CLAIM
    jc .none
    mov [ds:bp+OS88UI_DR_SEG], dx
    mov es, dx
    xor di, di
    pop bx
    pop ax
    call os88ui_drrect          ; the rect again: the sizing spent it
    call OSAPI_GFX_SAVE
    jnc .out                    ; refused (it straddles two displays): half a
    call os88ui_drfree          ; bank put back is worse than none
    jmp short .out
.none:
    pop bx
    pop ax
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drback - put the banked pixels back (internal)
; in:  BP = the record, the gfx lock held
; out: CF = 0 the screen is repaired; CF = 1 it is NOT and the caller's content
;      wants repainting - either nothing was banked, or the write-back itself
;      refused. Every register preserved
;
; THE REFUSAL IS THE SAVE'S, ONE DIRECTION LATER. OSAPI_GFX_REST answers CF = 1
; on a rect that straddles two displays, exactly as OSAPI_GFX_SAVE does, and
; os88ui_drbank treats that as important enough to throw the whole bank away -
; "half a bank put back is worse than none". This end dropped the same carry
; and returned "repaired", so a close that restored NOTHING told the caller it
; had nothing to repaint and the list stayed on the glass with the control
; reading shut: 2,710 pixels of it, measured on the title page with the
; write-back forced to refuse.
; -----------------------------------------------------------------------------
os88ui_drback:
    cmp word [ds:bp+OS88UI_DR_SEG], 0
    jne .have
    stc
    ret
.have:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [ds:bp+OS88UI_DR_SEG]
    xor si, si
    call os88ui_drrect
    call OSAPI_GFX_REST
    pushf                       ; ...and its answer is the caller's. The claim
    call os88ui_drfree          ; goes back either way: a bank whose pixels
    popf                        ; could not be written is a bank that never
    pop es                      ; will be, and POP does not touch the flags
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; os88ui_drfree - give the claim back WITHOUT restoring (internal): the
;                 painter's way out, since a repaint has already put fresh
;                 content where the bank's copy is stale. Preserves all
os88ui_drfree:
    push ax
    push dx
    mov dx, [ds:bp+OS88UI_DR_SEG]
    or dx, dx
    jz .out
    mov ax, [ds:bp+OS88UI_DR_KB]
    call OSAPI_MEM_FREE
    mov word [ds:bp+OS88UI_DR_SEG], 0
.out:
    pop dx
    pop ax
    ret

; os88ui_drlist - the dropped list, whole (internal). in: BP = the record
os88ui_drlist:
    call os88ui_drrect          ; AX/BX/CX/DX = the list's rect, wherever
    UI_BLACK                    ; os88ui_drfit put it (13.14.2)
    UI_FRAME
    xor al, al                  ; every item, the hot one (or, with none hot
.item:                          ; yet, the pick) inverted
    mov ah, [ds:bp+OS88UI_DR_HOT]
    cmp ah, 0FFh
    jne .hot
    mov ah, [ds:bp+OS88UI_DR_SEL]
.hot:
    cmp al, ah
    mov ah, 0
    jne .draw
    inc ah
.draw:
    call os88ui_dritem
    inc al
    cmp al, [ds:bp+OS88UI_DR_N]
    jb .item
    ret

; os88ui_dritem - one cell of the list (internal)
; in:  BP = the record, AL = the item, AH = 1 inverted. Preserves everything
os88ui_dritem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                  ; DI = the item, and whether it is inverted
    and ax, 0FFh
    mov bl, OS88UI_DRIH
    mul bl                      ; AX = the cell's offset down the list
    mov bx, [ds:bp+OS88UI_DR_TOP]
    inc bx
    add bx, ax                  ; BX = the cell's first row
    mov dx, bx
    add dx, OS88UI_DRIH - 1     ; DX = its last
    mov cx, [ds:bp+4]
    dec cx
    mov ax, [ds:bp+0]
    inc ax
    UI_WHITE
    test di, 0FF00h
    jz .ground
    UI_BLACK
.ground:
    UI_FILL                     ; the cell's ground (a menu's own way: the
                                ; drop is filled and the items run over it,
                                ; SPEC.md 12.2.1)
    mov dx, bx
    add dx, 2                   ; the run, 2 rows in
    mov si, di
    and si, 0FFh
    shl si, 1
    add si, [ds:bp+OS88UI_DR_ITEMS]
    mov si, [si]
    mov cx, [ds:bp+0]
    add cx, 4
    mov al, CBLACK
    mov ah, CWHITE
    test di, 0FF00h
    jz .run
    xchg al, ah                 ; white on the black cell
.run:
    UI_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; os88ui_dritemat - which item is the point over? (internal)
; in:  BP = the record, CX = x, DX = y
; out: AL = the item, or 0FFh; AH clobbered, nothing else touched
os88ui_dritemat:
    push bx
    push dx
    mov al, 0FFh
    cmp cx, [ds:bp+0]
    jl .out
    cmp cx, [ds:bp+4]
    jg .out
    sub dx, [ds:bp+OS88UI_DR_TOP]
    dec dx                      ; DX = rows below the first cell's top
    js .out
    mov bx, dx
    mov ax, bx
    mov bl, OS88UI_DRIH
    div bl                      ; AL = the cell, AH = the row inside it
    cmp al, [ds:bp+OS88UI_DR_N]
    jb .out
    mov al, 0FFh
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_drpress - the press half of the gesture
; in:  BX = the record, CX = x, DX = y; the gfx lock is held
; out: AH = 1 the press was spent here, else 0; AL = the new pick, or 0FFh;
;      CF = 1 repaint your content (the list came down). Nothing else touched
; -----------------------------------------------------------------------------
os88ui_drpress:
    push bx
    push si
    push bp
    mov bp, bx
    cmp byte [ds:bp+OS88UI_DR_DIS], 0   ; A GREYED CONTROL TAKES NO PRESS
    je .live                            ; (SPEC.md 13.14.5), and does not
    mov ax, 00FFh                       ; SPEND one either - the press is the
    clc                                 ; app's to give to whatever is under
    jmp .out                            ; it, exactly as if we were not here.
                                        ; AH = 0 not spent, AL = 0FFh NO PICK:
                                        ; a plain `xor ax, ax` here would
                                        ; answer "the user chose item 0"
.live:
    cmp byte [ds:bp+OS88UI_DR_OPEN], 0
    jne .open
    call os88ui_bhit            ; closed: a press on the box drops the list
    jc .none
    mov byte [ds:bp+OS88UI_DR_OPEN], 1
    mov byte [ds:bp+OS88UI_DR_HOT], 0FFh
    call os88ui_drfit           ; WHERE THE LIST GOES, BEFORE ANYTHING CAN GO
                                ; WRONG (13.14.2): the two calls below can both
                                ; refuse, and os88ui_dritemat still has to know
                                ; where the cells are. A record with no window
                                ; gets the row under its own box, which is
                                ; where every list went before this existed
    mov bx, [ds:bp+OS88UI_DR_WIN]
    call OSAPI_WM_CLIP_SET      ; W_ONCLICK arms no region of its own, and
    jnc .drew                   ; the list must not land on a window over ours
    mov byte [ds:bp+OS88UI_DR_OPEN], 0
    jmp short .spent            ; NOTHING WAS DRAWN, SO NOTHING IS OPEN. The
                                ; byte went to 1 above because os88ui_drfit
                                ; needs it, and leaving it there was a control
                                ; that believed it had a list on screen when
                                ; it had not: the press after it hit-tested
                                ; cells nobody could see and silently picked
                                ; one. SPEC.md 88.13.6 met this from the other
                                ; end - four records with no OS88UI_DR_WIN, so
                                ; every press here took the refusal - and gave
                                ; them windows; this is the control's own half
.drew:
    call os88ui_drbank          ; the pixels it covers, banked (13.14.1) -
    call os88ui_drlist          ; AFTER the clip, which changes nothing about
                                ; what a save reads, and before the drawing,
                                ; which is the whole point
.spent:
    mov ax, 01FFh               ; spent, no pick, nothing to repaint
    clc
    jmp short .out
.none:
    xor ax, ax                  ; not ours
    mov al, 0FFh
    clc
    jmp short .out
.open:                          ; dropped: an item picks, anything else closes
    call os88ui_dritemat
    cmp al, 0FFh
    je .close
    xor ah, ah
    mov [ds:bp+OS88UI_DR_SEL], ax
    mov ah, 1
    jmp short .down
.close:
    mov ax, 01FFh
.down:
    mov byte [ds:bp+OS88UI_DR_OPEN], 0
    call os88ui_drback          ; the banked pixels back, and CF says whether
                                ; that happened: CF = 1 only when there was no
                                ; bank, which is the repaint the caller has
                                ; always owed on this path
    jc .out                     ; ...and when it did, the BOX still shows the
    push ax                     ; item it had before the press: the list's
    xor di, di                  ; rect is below it and the write-back cannot
    call os88ui_drbox           ; reach it (13.14.1). A press only reaches a
    pop ax                      ; live control, so the pen is the live one
.out:
    pop bp
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_drdrag - the tracking edge: the highlight follows the pointer
; in:  BX = the record, CX = x, DX = y; the gfx lock is held
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
os88ui_drdrag:
    push ax
    push bx
    push bp
    mov bp, bx
    cmp byte [ds:bp+OS88UI_DR_OPEN], 0
    je .out
    call os88ui_dritemat
    cmp al, [ds:bp+OS88UI_DR_HOT]
    je .out
    mov bx, [ds:bp+OS88UI_DR_WIN]
    call OSAPI_WM_CLIP_SET
    jc .out
    push ax
    mov al, [ds:bp+OS88UI_DR_HOT]   ; the old highlight back to plain...
    cmp al, 0FFh
    jne .old
    mov al, [ds:bp+OS88UI_DR_SEL]   ; (none yet: the pick was the one shown)
.old:
    xor ah, ah
    call os88ui_dritem
    pop ax
    mov [ds:bp+OS88UI_DR_HOT], al
    cmp al, 0FFh                ; ...and the new one inverted - or the pick
    jne .new                    ; again, off every item, which is what the
    mov al, [ds:bp+OS88UI_DR_SEL]   ; painter shows for "none hot"
.new:
    mov ah, 1
    call os88ui_dritem
.out:
    pop bp
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_drup - the release half: over an item, it is the pick
; in:  BX = the record, CX = x, DX = y; the gfx lock is held
; out: os88ui_drpress's answers. A release anywhere else leaves the list up,
;      which is what makes a click-then-click work as well as a drag
; -----------------------------------------------------------------------------
os88ui_drup:
    push bx
    push bp
    mov bp, bx
    mov ax, 00FFh
    clc
    cmp byte [ds:bp+OS88UI_DR_OPEN], 0
    je .out
    cmp byte [ds:bp+OS88UI_DR_HOT], 0FFh
    je .out                     ; THE POINTER HAS NOT MOVED ONTO AN ITEM since
                                ; the press that opened the list, so this is
                                ; the release half of one click on the BOX and
                                ; it picks nothing - which is the kernel menu's
                                ; own rule (press the title, release the title,
                                ; nothing happens). Geometry used to say it:
                                ; the list was always BELOW the box, so the
                                ; release landed on no item. Since 13.14.2 a
                                ; long list covers its own box, and without
                                ; this a single click on the control picked
                                ; whichever item happened to be under it
    call os88ui_dritemat
    cmp al, 0FFh
    je .out
    xor ah, ah
    mov [ds:bp+OS88UI_DR_SEL], ax
    mov byte [ds:bp+OS88UI_DR_OPEN], 0
    mov ah, 1
    push ax
    call os88ui_drback          ; the pixels back, CF = 1 only with no bank
    jc .rest
    push di
    xor di, di
    call os88ui_drbox           ; ...and the box, whose caption is the pick
    pop di
    clc
.rest:
    pop ax
.out:
    pop bp
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88ui_drclose - take the list down, if it is up
; in:  BX = the record
; out: CF = 1 it was up, and your content wants repainting; else CF = 0
; -----------------------------------------------------------------------------
os88ui_drclose:
    cmp byte [bx+OS88UI_DR_OPEN], 0
    je .no
    mov byte [bx+OS88UI_DR_OPEN], 0
    push bp
    mov bp, bx
    call os88ui_drback          ; the pixels back where they were banked, and
    pop bp                      ; CF = 1 - repaint - only where they were not
    ret
.no:
    clc
    ret
%endif                          ; OS88UI_DROP

%ifdef OS88UI_MENU
; =============================================================================
; THE IN-WINDOW MENU (SPEC.md 13.16): a bar of titles inside YOUR content, and
; a pull-down under whichever one is pressed
;
; The kernel's bar (SPEC.md 12) is the desktop's, and `MENU_APPMAX` is five
; titles; a package whose own menus outgrow that, or which is imitating a
; program that put its bar inside the document window, draws its own. WORD is
; the first (nine titles, verbatim from Opus's menus.cmd, SPEC.md 68.2) and
; SHEET is the second - and Sheet's `sh_mtrack` says in its own comment that
; it follows *"word.asm's wd_mtrack pattern"*, which is a body copied by hand
; and is exactly what this file exists to stop. It shows in what the copy did
; not take: Word banks the pixels its pull-down covers (SPEC.md 68.2.1) and
; Sheet repaints its whole content on every close.
;
; docs/plans/UI-MENU-ELEMENT.md is the costing. What made it tractable is that
; Word already followed SPEC.md 22's fm_hit discipline - the open panel's rect
; is computed once and the painter, the hit test, the highlight and the close
; all read those same four words - so the whole of its coupling to Word is 22
; globals that group into one record and ONE hook.
;
; --- THE RECORD, in YOUR data ------------------------------------------------
;   my_menu:
;       dw 0,0,0,0      ; OS88UI_MN_RECT: your CONTENT rect {x1,y1,x2,y2},
;                       ; inclusive, SCREEN coords - fill it in your painter
;                       ; from OSAPI_WM_CONTENT, since a window moves
;       dw my_mtab      ; OS88UI_MN_TAB:  the table below
;       dw 9            ; OS88UI_MN_N:    titles ON THE BAR. Rows past it are
;                       ; ANCHORED lists - a ribbon combo - which hang at
;                       ; MN_AX/MN_AY instead of under a title
;       dw my_barstr    ; OS88UI_MN_BAR:  the bar as ONE string, drawn as one
;                       ; opaque run; the table indexes into it by CELL
;       dw 0            ; OS88UI_MN_WIN:  your window, once created
;       db 0FFh, 0FFh   ; OS88UI_MN_OPEN, OS88UI_MN_HOT
;       db 0            ; OS88UI_MN_INK:  the ink its runs letter in
;       db 0            ; (pad)
;       dw 0,0,0,0      ; OS88UI_MN_MRECT: the open panel. The control's
;       dw 0,0,0,0      ; OS88UI_MN_ABOX:  the pressed title's own box
;       dw 0, 0         ; OS88UI_MN_SEG, MN_KB: the bank...
;       dw 0,0,0,0      ; OS88UI_MN_BRECT: ...and the rect it holds
;       dw my_barbuf    ; OS88UI_MN_BBUF: scratch for a truncated bar
;       dw 0            ; OS88UI_MN_CHK: near ptr to your item-state hook, or
;                       ; 0. AL = item index of the OPEN menu; answer the
;                       ; item's live flags in AL (OS88UI_MNF_*). It is the
;                       ; ONE thing the element cannot know: whether Paste is
;                       ; enabled and whether Draft is ticked are the
;                       ; application's facts, and SPEC.md 47's rule is to
;                       ; grey a fact rather than guess one
;
; --- THE TABLE ---------------------------------------------------------------
; One MN_ROW-byte row per menu, then an array of MN_ITEM-byte item records -
; the AMENU_ITEMS shape one level up:
;   db cell            ; where the title starts in MN_BAR, in CELLS; 0FFh for
;                      ; an ANCHORED list, which is not on the bar at all
;   db cells           ; ...and how many
;   db mnemonic        ; the cell to underline, 0 = the first
;   db n               ; items
;   dw items           ; -> the item records
;   dw width           ; the panel's width in pixels, precomputed
;
;   MN_ITEM: db flags, mnemonic index, action, mnemonic letter
;            dw label, caption      ; NUL strings; caption is right-justified
; =============================================================================

OS88UI_MN_RECT   equ 0
OS88UI_MN_TAB    equ 8
OS88UI_MN_N      equ 10
OS88UI_MN_BAR    equ 12
OS88UI_MN_WIN    equ 14
OS88UI_MN_OPEN   equ 16
OS88UI_MN_HOT    equ 17
OS88UI_MN_INK    equ 18
OS88UI_MN_CKX    equ 2           ; SPEC.md 13.16.2: the tick column's mark -
OS88UI_MN_CKY    equ 3           ; a solid square, centred in the 8px column
OS88UI_MN_CKW    equ 5           ; and on the 8px glyph row, os88ui_chk's size
OS88UI_MN_MRECT  equ 20
OS88UI_MN_ABOX   equ 28
OS88UI_MN_SEG    equ 36
OS88UI_MN_KB     equ 38
OS88UI_MN_BRECT  equ 40          ; 4 words: the rect actually BANKED - the
                                 ; panel grown by its shadow and clamped,
                                 ; written on the way down so the way back
                                 ; cannot disagree with it by a pixel. The
                                 ; drop-down recomputes instead
                                 ; (os88ui_drrect) and is right to, its rect
                                 ; being unclamped; this one is not
OS88UI_MN_CHK    equ 48
OS88UI_MN_PICKI  equ 50          ; byte: the item the gesture PICKED, or 0FFh.
                                 ; A record field and not a register, because
                                 ; the gesture's epilogue pops four of them
OS88UI_MN_BBUF   equ 52
OS88UI_MN_OPENH  equ 54          ; near ptr, or 0: called with AL = the menu
                                 ; ABOUT to open, so a caller whose items are
                                 ; not all constant can compose one first.
                                 ; Word's Window menu names the live document,
                                 ; which is a fact about Word and not a menu -
                                 ; and it has to be a hook rather than the
                                 ; caller doing it before os88ui_mnopen,
                                 ; because the GESTURE opens menus too when a
                                 ; drag slides across the bar          ; near ptr to a scratch of your own, at least
                                 ; the bar string's length + 2: os88ui_mnbar
                                 ; composes a TRUNCATED copy there when the
                                 ; window is too narrow for the whole bar.
                                 ; The caller's, not the record's, because a
                                 ; bar is as long as its titles
OS88UI_MN_RPNTH  equ 56          ; near ptr, or 0: the PIECEWISE REPAINT the
                                 ; close owes when there was no bank to write
                                 ; back. It is a hook and not the close's CF
                                 ; alone, because the element closes menus
                                 ; ITSELF - a drag sliding onto another title,
                                 ; a release off the panel - and a caller that
                                 ; only saw the outermost answer would repaint
                                 ; at the wrong moment or not at all. Called
                                 ; from os88ui_mnclose exactly where the debt
                                 ; is incurred, which is where Word's own
                                 ; wd_mclose used to call wd_mrepair
OS88UI_MN_SIZE   equ 58

OS88UI_MN_ROW    equ 8           ; bytes in one table row...
OS88UI_MN_ITEM   equ 8           ; ...and in one item record
OS88UI_MN_NONE   equ 0FFh        ; MN_OPEN/MN_HOT: nothing
OS88UI_MNF_SEP   equ 1           ; item flags: a separator hline
OS88UI_MNF_DIS   equ 2           ; disabled - the disabled pen, never fired
OS88UI_MNF_CHK   equ 4           ; carries a check mark
OS88UI_MN_IH     equ 10          ; an item band: 8px of glyph, 1 above, 1 under
OS88UI_MN_SH     equ 5           ; ...and a separator band
OS88UI_MN_BARH   equ 14          ; the bar's own height. Word's WD_MENU_H and
                                 ; Sheet's SH_MBAR_H are both 14, chosen
                                 ; independently, which is the same evidence
                                 ; the 10 and the 5 above are

; -----------------------------------------------------------------------------
; os88ui_mnrow - AL = menu index -> BX = its table row
; in:  BP = the record; out: BX; preserves everything else
; -----------------------------------------------------------------------------
os88ui_mnrow:
    push ax
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bx, ax
    add bx, [ds:bp+OS88UI_MN_TAB]
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_mnitem - AL = item index, BX = the row -> SI = its 8-byte record
; preserves everything else
; -----------------------------------------------------------------------------
os88ui_mnitem:
    push ax
    mov si, [bx+4]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add si, ax
    pop ax
    ret

os88ui_mngeo:
    push ax
    push bx
    push cx
    push dx
    push si
    call os88ui_mnrow
    mov dl, [bx]
    xor dh, dh
    mov cl, 3
    shl dx, cl
    add dx, [ds:bp+OS88UI_MN_RECT]
    add dx, 8-4                     ; the panel starts 4px left of the title
    mov [ds:bp+OS88UI_MN_MRECT], dx
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    add dx, OS88UI_MN_BARH
    mov [ds:bp+OS88UI_MN_MRECT+2], dx
    mov cl, [bx+3]                  ; height: 4 + 10/5 per item
    xor ch, ch
    mov si, [bx+4]
    mov ax, 4
.hh:
    jcxz .hd
    test byte [si], OS88UI_MNF_SEP
    jz .h10
    add ax, OS88UI_MN_SH
    jmp short .hn
.h10:
    add ax, OS88UI_MN_IH
.hn:
    add si, 8
    dec cx
    jmp short .hh
.hd:
    mov dx, [ds:bp+OS88UI_MN_RECT+6]  ; the content's last row...
    dec dx                          ; ...less the shadow's pixel
    add ax, [ds:bp+OS88UI_MN_MRECT+2]
    dec ax
    cmp ax, dx
    jbe .yok
                                    ; TOO TALL: CLIP. A bar menu hangs under
                                    ; its own title and nowhere else, so there
                                    ; is nothing to slide it into - moving one
                                    ; would put it over the bar it came from.
                                    ; The SLIDE that used to live here was an
                                    ; ANCHORED list's, and an anchored list is
                                    ; os88ui_drop's job now: SPEC.md 13.14.2
                                    ; does the same arithmetic, with the same
                                    ; unsigned-subtraction trap written down,
                                    ; in os88ui_drfit
                                    ;
                                    ; Clipping IS SILENT - the trailing items
                                    ; are drawn nowhere and os88ui_mnfind
                                    ; refuses to find them - which is why the
                                    ; anchored list had to slide and why a bar
                                    ; menu taller than its window is a menu to
                                    ; shorten rather than a case to handle
    mov ax, dx                      ; clipped: trailing items are not drawn
.yok:                               ; and os88ui_mnfind stops at the same edge
    mov [ds:bp+OS88UI_MN_MRECT+6], ax
    mov ax, [bx+6]                  ; the width the table precomputed
    mov dx, [ds:bp+OS88UI_MN_MRECT]
    add dx, ax
    dec dx
    mov cx, [ds:bp+OS88UI_MN_RECT+4]  ; ...and its last column, the same
    dec cx
    cmp dx, cx
    jbe .xok
    mov dx, cx                      ; ride the right edge (Help lives there)
    push dx
    sub dx, ax
    inc dx
    mov cx, [ds:bp+OS88UI_MN_RECT]
    inc cx
    cmp dx, cx
    jae .x1ok
    mov dx, cx
.x1ok:
    mov [ds:bp+OS88UI_MN_MRECT], dx
    pop dx
.xok:
    mov [ds:bp+OS88UI_MN_MRECT+4], dx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mnbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [ds:bp+OS88UI_MN_RECT]
    mov bx, [ds:bp+OS88UI_MN_RECT+2]
    mov cx, [ds:bp+OS88UI_MN_RECT+4]  ; x1 + cw - 1 IS the rect's own x2
    mov dx, bx
    add dx, OS88UI_MN_BARH-2
    call OSAPI_GFX_FILL             ; the strip's ground
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [ds:bp+OS88UI_MN_RECT]
    mov bx, cx
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    add dx, OS88UI_MN_BARH-1
    call OSAPI_GFX_HLINE            ; the rule under the bar
    mov si, [ds:bp+OS88UI_MN_BAR]
    mov ax, [ds:bp+OS88UI_MN_RECT+4]  ; the content's WIDTH, which the rect
    sub ax, [ds:bp+OS88UI_MN_RECT]    ; carries as two edges
    inc ax
    cmp ax, 8 + 56*8
    jae .whole
    sub ax, 16                      ; too narrow: truncate at the cells that
    js .under                       ; fit. Mid-title cuts are the honest
    mov cl, 3                       ; degrade for a 96px window
    shr ax, cl
    jz .under
    cmp ax, 56
    jbe .tr
    mov ax, 56
.tr:
    mov cx, ax
    mov di, [ds:bp+OS88UI_MN_BBUF]
    cld
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    jz .cpd
    loop .cp
    mov byte [di], 0
.cpd:
    mov si, [ds:bp+OS88UI_MN_BBUF]
.whole:
    mov cx, [ds:bp+OS88UI_MN_RECT]
    add cx, 8
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    add dx, 3
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.under:
    mov si, [ds:bp+OS88UI_MN_TAB]   ; the mnemonic underlines
    mov di, [ds:bp+OS88UI_MN_N]
.mn:
    mov al, [si]
    add al, [si+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]  ; (start+len+1)*8 <= cw = fully visible,
    dec ax                          ; as one absolute compare
    cmp ax, [ds:bp+OS88UI_MN_RECT+4]
    ja .mnnext
    mov al, [si]
    add al, [si+2]                  ; start + the title's mnemonic index
    xor ah, ah
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]
    add ax, 8
    mov bx, ax
    add bx, 6
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    add dx, 11
    call OSAPI_GFX_HLINE
.mnnext:
    add si, 8
    dec di
    jnz .mn
    mov al, [ds:bp+OS88UI_MN_OPEN]              ; a bar repaint under an open dropdown
    cmp al, [ds:bp+OS88UI_MN_N]                  ; keeps the title inverted
    jae .out
    call os88ui_mntxor
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mntxor:
    push ax
    push bx
    push cx
    push dx
    call os88ui_mnrow
    mov al, [bx]
    add al, [bx+1]
    inc al
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]  ; ...against the content's own right edge
    dec ax                          ; (the width, one absolute compare along)
    cmp ax, [ds:bp+OS88UI_MN_RECT+4]
    ja .out                         ; off a narrow window's bar: nothing shown
    mov al, [bx]
    xor ah, ah
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]
    add ax, 8-2                     ; 2px into the leading space cell
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    mov cx, ax
    add cx, dx
    add cx, 3                       ; ...and 2px into the trailing one
    mov bx, [ds:bp+OS88UI_MN_RECT+2]
    inc bx
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    add dx, 12
    call OSAPI_GFX_XOR_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mntitler:
    push ax
    push bx
    push cx
    push dx
    call os88ui_mnrow
    mov al, [bx]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]
    add ax, 8-2
    mov [ds:bp+OS88UI_MN_ABOX], ax
    mov dl, [bx+1]
    xor dh, dh
    shl dx, cl
    add ax, dx
    add ax, 3
    mov [ds:bp+OS88UI_MN_ABOX+4], ax
    mov ax, [ds:bp+OS88UI_MN_RECT+2]
    mov [ds:bp+OS88UI_MN_ABOX+2], ax
    add ax, OS88UI_MN_BARH-1
    mov [ds:bp+OS88UI_MN_ABOX+6], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mndraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [ds:bp+OS88UI_MN_MRECT]
    mov bx, [ds:bp+OS88UI_MN_MRECT+2]
    mov cx, [ds:bp+OS88UI_MN_MRECT+4]
    mov dx, [ds:bp+OS88UI_MN_MRECT+6]
    call OSAPI_GFX_FILL
    clc
    call OSAPI_GFX_PEN              ; live pen: CBLACK, dither flag clear
    call OSAPI_GFX_FRAME
    mov ax, [ds:bp+OS88UI_MN_MRECT+4]               ; the drop shadow: right edge...
    inc ax
    mov bx, [ds:bp+OS88UI_MN_MRECT+2]
    inc bx
    mov cx, ax
    mov dx, [ds:bp+OS88UI_MN_MRECT+6]
    inc dx
    call OSAPI_GFX_FILL_GRAY
    mov ax, [ds:bp+OS88UI_MN_MRECT]               ; ...and bottom edge
    inc ax
    mov bx, [ds:bp+OS88UI_MN_MRECT+6]
    inc bx
    mov cx, [ds:bp+OS88UI_MN_MRECT+4]
    inc cx
    mov dx, bx
    call OSAPI_GFX_FILL_GRAY
    ; the items
    mov al, [ds:bp+OS88UI_MN_OPEN]
    call os88ui_mnrow
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [ds:bp+OS88UI_MN_MRECT+2]
    add di, 2
.item:
    or cx, cx
    jnz .live
    jmp .done                       ; out of the short branch's reach
.live:
    test byte [si], OS88UI_MNF_SEP
    jz .norm
    mov ax, di                      ; a separator: one hline mid-band
    add ax, OS88UI_MN_SH-1
    cmp ax, [ds:bp+OS88UI_MN_MRECT+6]
    jae .done
    mov ax, [ds:bp+OS88UI_MN_MRECT]
    inc ax
    mov bx, [ds:bp+OS88UI_MN_MRECT+4]
    dec bx
    mov dx, di
    add dx, 2
    call OSAPI_GFX_HLINE
    add di, OS88UI_MN_SH
    jmp .next
.norm:
    mov ax, di
    add ax, OS88UI_MN_IH-1
    cmp ax, [ds:bp+OS88UI_MN_MRECT+6]
    jae .done                       ; clipped by a short window: stop
    push cx
    mov byte [ds:bp+OS88UI_MN_INK], CBLACK
    test byte [si], OS88UI_MNF_DIS
    jnz .dis
    clc
    jmp short .pen
.dis:
    mov byte [ds:bp+OS88UI_MN_INK], CDGRAY      ; ...and the run's ink is the same answer
    stc                             ; (SPEC.md 68.14): a package cannot read
.pen:                               ; the pen back, so it is decided here
    call OSAPI_GFX_PEN              ; CF IS the argument (SPEC.md 47)
    ; the check column
    test byte [si], OS88UI_MNF_CHK
    jz .nochk
    mov al, [si+2]                  ; the CHECK hook (SPEC.md 13.16.1): the
    push bx                         ; one thing the element cannot know, and
    mov bx, [ds:bp+OS88UI_MN_CHK]   ; a record without one simply never ticks
    or bx, bx
    jz .chkno
    call bx
    jmp short .chkd
.chkno:
    clc
.chkd:
    pop bx
    jnc .nochk
    mov ax, [ds:bp+OS88UI_MN_MRECT]     ; A SOLID SQUARE, not a tick
    add ax, OS88UI_MN_CKX              ; (SPEC.md 13.16.2) - os88ui_chk's own
    mov bx, di                         ; mark and its own reason, which is that
    add bx, OS88UI_MN_CKY              ; a thin diagonal READS AS NOISE on the
    mov cx, ax                         ; two 1bpp adapters this OS is for
    add cx, OS88UI_MN_CKW - 1          ; (SPEC.md 39.4). The pen is already the
    mov dx, bx                         ; row's - gfx_pen_cf set [gfx_color] to
    add dx, OS88UI_MN_CKW - 1          ; CBLACK or CDGRAY above - so a fill
    UI_FILL                            ; inherits the greying for free
.nochk:
    ; the label
    mov cx, [ds:bp+OS88UI_MN_MRECT]
    add cx, 8
    mov dx, di
    inc dx
    push si
    mov si, [si+4]
    mov ah, CWHITE                  ; OPAQUE (SPEC.md 68.14): the panel's white
    mov al, [ds:bp+OS88UI_MN_INK]               ; is a constant and the item's own cells
    call OSAPI_FONT_RUN             ; are one decision each. [gfx_dis] is
    pop si                          ; already set, so 6.1.12 folds 47's
                                    ; checkerboard into the run's own mask
    ; the mnemonic underline (enabled items only: a grey line rounds to
    ; solid black at 1bpp and a greyed mnemonic answers no key anyway)
    test byte [si], OS88UI_MNF_DIS
    jnz .nomn
    cmp byte [si+3], 0
    je .nomn
    mov al, [si+1]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_MRECT]
    add ax, 8
    mov bx, ax
    add bx, 6
    mov dx, di
    add dx, 9
    call OSAPI_GFX_HLINE
.nomn:
    ; the caption, right-justified in the panel
    mov bx, [si+6]
    or bx, bx
    jz .nocap
    push si
    mov si, bx
    call OSAPI_FONT_WIDTH           ; AX is the WIDTH here, so the pair goes
    mov cx, [ds:bp+OS88UI_MN_MRECT+4]               ; in after it and not before
    sub cx, 6
    sub cx, ax
    mov dx, di
    inc dx
    mov ah, CWHITE
    mov al, [ds:bp+OS88UI_MN_INK]
    call OSAPI_FONT_RUN
    pop si
.nocap:
    clc
    call OSAPI_GFX_PEN              ; pen back live before the next item
    pop cx
    add di, OS88UI_MN_IH
.next:
    add si, 8
    dec cx
    jz .done
    jmp .item
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mnfind:
    push bx
    push cx
    push dx
    push si
    push di
    cmp cx, [ds:bp+OS88UI_MN_MRECT]
    jb .miss
    cmp cx, [ds:bp+OS88UI_MN_MRECT+4]
    ja .miss
    cmp dx, [ds:bp+OS88UI_MN_MRECT+2]
    jb .miss
    cmp dx, [ds:bp+OS88UI_MN_MRECT+6]
    ja .miss
    mov al, [ds:bp+OS88UI_MN_OPEN]
    call os88ui_mnrow
    mov cl, [bx+3]
    xor ch, ch
    mov si, [bx+4]
    mov di, [ds:bp+OS88UI_MN_MRECT+2]
    add di, 2
    cmp dx, di
    jb .miss                        ; in the top pad
    xor bx, bx                      ; BL = index
.scan:
    jcxz .miss
    test byte [si], OS88UI_MNF_SEP
    jnz .sep
    mov ax, di
    add ax, OS88UI_MN_IH-1
    cmp ax, [ds:bp+OS88UI_MN_MRECT+6]
    jae .miss                       ; this item is clipped: so is the rest
    cmp dx, ax
    ja .below
    test byte [si], OS88UI_MNF_DIS        ; found the band
    jnz .miss
    mov al, bl
    jmp short .out
.below:
    add di, OS88UI_MN_IH
    jmp short .adv
.sep:
    mov ax, di
    add ax, OS88UI_MN_SH-1
    cmp dx, ax
    jbe .miss                       ; a separator answers nothing
    add di, OS88UI_MN_SH
.adv:
    add si, 8
    inc bx
    dec cx
    jmp short .scan
.miss:
    mov al, 0xFF
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

os88ui_mnhl:
    cmp al, 0xFF
    je .no
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, al                      ; DL = target index
    mov al, [ds:bp+OS88UI_MN_OPEN]
    call os88ui_mnrow
    mov si, [bx+4]
    mov di, [ds:bp+OS88UI_MN_MRECT+2]
    add di, 2
    xor cx, cx
.w:
    cmp cl, dl
    je .found
    test byte [si], OS88UI_MNF_SEP
    jz .i10
    add di, OS88UI_MN_SH
    jmp short .n
.i10:
    add di, OS88UI_MN_IH
.n:
    add si, 8
    inc cx
    jmp short .w
.found:
    mov ax, di
    add ax, OS88UI_MN_IH-1
    cmp ax, [ds:bp+OS88UI_MN_MRECT+6]
    jae .done                       ; clipped: it was not drawn either
    mov dx, ax
    mov ax, [ds:bp+OS88UI_MN_MRECT]
    inc ax
    mov bx, di
    mov cx, [ds:bp+OS88UI_MN_MRECT+4]
    dec cx
    call OSAPI_GFX_XOR_FILL
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    ret

os88ui_mnbarhit:
    push bx
    push cx
    push dx
    push si
    mov ax, [ds:bp+OS88UI_MN_RECT+2]
    cmp dx, ax
    jb .no
    add ax, OS88UI_MN_BARH-1
    cmp dx, ax
    ja .no
    mov ax, cx
    sub ax, [ds:bp+OS88UI_MN_RECT]
    sub ax, 8
    js .no
    mov cl, 3
    shr ax, cl                      ; AL = the cell the click is in (AH = 0)
    mov si, [ds:bp+OS88UI_MN_TAB]
    xor bx, bx
.s:
    mov dl, [si]
    cmp al, dl
    jb .next
    mov dh, dl
    add dh, [si+1]
    cmp al, dh
    jae .next
    mov cl, [si]                    ; visible? (start+len+1)*8 <= cw
    add cl, [si+1]
    inc cl
    xor ch, ch
    push ax
    mov ax, cx
    mov cl, 3
    shl ax, cl
    add ax, [ds:bp+OS88UI_MN_RECT]
    dec ax
    cmp ax, [ds:bp+OS88UI_MN_RECT+4]
    pop ax
    ja .next
    mov al, bl
    jmp short .out
.next:
    add si, 8
    inc bx
    cmp bl, [ds:bp+OS88UI_MN_N]
    jb .s
.no:
    mov al, 0xFF
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

os88ui_mninrect:
    cmp cx, [ds:bp+OS88UI_MN_MRECT]
    jb .no
    cmp cx, [ds:bp+OS88UI_MN_MRECT+4]
    ja .no
    cmp dx, [ds:bp+OS88UI_MN_MRECT+2]
    jb .no
    cmp dx, [ds:bp+OS88UI_MN_MRECT+6]
    ja .no
    clc
    ret
.no:
    stc
    ret

os88ui_mnbank:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call os88ui_mnfree          ; A BANK WE STILL HOLD IS LAST FRAME'S, and
                                ; zeroing the word without giving it back was
                                ; a claim orphaned for the session: a caller
                                ; whose own repaint drops an open panel does
                                ; exactly that (Word's W_PAINT sets its
                                ; [wd_mopen] to none and never reaches
                                ; os88ui_mnback), so the next open leaked the
                                ; last one's save-under. os88ui_drbank has
                                ; had this line since 13.14.1; this is the
                                ; same line one control along
    mov word [ds:bp+OS88UI_MN_SEG], 0
    mov ax, [ds:bp+OS88UI_MN_MRECT]               ; the rect INCLUDING the drop shadow, and
    mov [ds:bp+OS88UI_MN_BRECT], ax              ; clamped exactly the way a repaint grows
    mov bx, [ds:bp+OS88UI_MN_MRECT+2]               ; and clamps its own - the two must name
    mov [ds:bp+OS88UI_MN_BRECT+2], bx              ; the same pixels or the restore is short
    mov ax, [ds:bp+OS88UI_MN_MRECT+4]               ; by a column
    inc ax
    mov dx, [ds:bp+OS88UI_MN_RECT]
    mov dx, [ds:bp+OS88UI_MN_RECT+4]
    inc dx
    dec dx
    cmp ax, dx
    jbe .x2ok
    mov ax, dx
.x2ok:
    mov [ds:bp+OS88UI_MN_BRECT+4], ax
    mov ax, [ds:bp+OS88UI_MN_MRECT+6]
    inc ax
    mov dx, [ds:bp+OS88UI_MN_RECT+2]
    mov dx, [ds:bp+OS88UI_MN_RECT+6]
    inc dx
    dec dx
    cmp ax, dx
    jbe .y2ok
    mov ax, dx
.y2ok:
    mov [ds:bp+OS88UI_MN_BRECT+6], ax
    ; bytes = planes * rows * ((x2>>3) - (x1>>3) + 1)
    mov ax, [ds:bp+OS88UI_MN_BRECT+4]
    mov cl, 3
    shr ax, cl
    mov bx, [ds:bp+OS88UI_MN_BRECT]
    shr bx, cl
    sub ax, bx
    inc ax                          ; AX = byte columns
    mov bx, [ds:bp+OS88UI_MN_BRECT+6]
    sub bx, [ds:bp+OS88UI_MN_BRECT+2]
    inc bx                          ; BX = rows
    mul bx                          ; AX = one plane's bytes (DX:AX, and a
                                    ; panel cannot reach 64KB of one plane)
    mov bx, [ds:bp+OS88UI_MN_WIN]
    or bx, bx
    jz .no
    push ax
    call OSAPI_WM_DISPLAY           ; DH = bpp OF THE DISPLAY WE ARE ON, which
    mov bl, dh                      ; OSAPI_VIDEO cannot answer on a two-card
    xor bh, bh                      ; machine (SPEC.md 39.16.4)
    pop ax
    cmp bl, 4
    je .planes
    mov bx, 1                       ; 1bpp adapter: one plane
.planes:
    mul bx
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; AX = KB, rounded up
    or ax, ax
    jz .no
    mov [ds:bp+OS88UI_MN_KB], ax
    call OSAPI_MEM_CLAIM            ; out CF=0, DX = base segment
    jc .no
    mov [ds:bp+OS88UI_MN_SEG], dx
    mov es, dx
    xor di, di
    mov ax, [ds:bp+OS88UI_MN_BRECT]
    mov bx, [ds:bp+OS88UI_MN_BRECT+2]
    mov cx, [ds:bp+OS88UI_MN_BRECT+4]
    mov dx, [ds:bp+OS88UI_MN_BRECT+6]
    call OSAPI_GFX_SAVE             ; CF=1 = the rect straddles two displays,
    jnc .out                        ; and half a bank put back is worse than
    mov dx, [ds:bp+OS88UI_MN_SEG]              ; none: hand it straight back
    mov ax, [ds:bp+OS88UI_MN_KB]
    call OSAPI_MEM_FREE
.no:
    mov word [ds:bp+OS88UI_MN_SEG], 0
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; os88ui_mnfree - give a bank back without drawing it (internal)
; in:  BP = the record; out: MN_SEG cleared; preserves every register
os88ui_mnfree:
    push ax
    push dx
    mov dx, [ds:bp+OS88UI_MN_SEG]
    or dx, dx
    jz .out
    mov ax, [ds:bp+OS88UI_MN_KB]
    call OSAPI_MEM_FREE
    mov word [ds:bp+OS88UI_MN_SEG], 0
.out:
    pop dx
    pop ax
    ret

os88ui_mnback:
    cmp word [ds:bp+OS88UI_MN_SEG], 0
    jne .have
    stc
    ret
.have:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [ds:bp+OS88UI_MN_SEG]
    xor si, si
    mov ax, [ds:bp+OS88UI_MN_BRECT]
    mov bx, [ds:bp+OS88UI_MN_BRECT+2]
    mov cx, [ds:bp+OS88UI_MN_BRECT+4]
    mov dx, [ds:bp+OS88UI_MN_BRECT+6]
    call OSAPI_GFX_REST             ; the same rect the save took, off the
                                    ; banked copy of it and not off MN_MRECT,
                                    ; which the caller's repaint may consume
    mov dx, [ds:bp+OS88UI_MN_SEG]
    mov ax, [ds:bp+OS88UI_MN_KB]
    call OSAPI_MEM_FREE             ; before the picked item runs (SPEC.md
    mov word [ds:bp+OS88UI_MN_SEG], 0          ; 12.4): whatever it claims gets a heap
    pop es                          ; this menu has already left
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

os88ui_mnopen:
    push ax
    push cx                         ; EVERY register but the documented output
    push si                         ; (os88ui.inc's own rule, and the one that
    push di                         ; matters most here): W_ONCLICK holds the
                                    ; WINDOW in SI, and ui_task does
                                    ; `mov [ui_armw], si` when the handler
                                    ; comes back - so a handler that returns
                                    ; with SI clobbered arms the RELEASE to
                                    ; whatever it left there and the far call
                                    ; goes through it. tests/skiesui.py check 2
                                    ; reads [ui_armw] for exactly this, and
                                    ; this routine reached it by inheriting
                                    ; three pushes that had been there for a
                                    ; string copy and losing them with it
    push bx                         ; the caller's chance to compose an item
    mov bx, [ds:bp+OS88UI_MN_OPENH] ; before it is drawn (SPEC.md 13.16.4)
    or bx, bx
    jz .noh
    call bx
.noh:
    pop bx
    mov [ds:bp+OS88UI_MN_OPEN], al
    mov byte [ds:bp+OS88UI_MN_HOT], 0xFF
    call os88ui_mngeo
    call os88ui_mntxor              ; EVERY menu has a bar title to invert now:
                                    ; the guard that used to stand here was
                                    ; for an ANCHORED list, which is
                                    ; os88ui_drop's (SPEC.md 68.2.3)
    call os88ui_mnbank                  ; ...before a pixel of the panel is drawn
    call os88ui_mndraw
    pop di
    pop si
    pop cx
    pop ax
    ret

os88ui_mnclose:
    push ax
    mov al, [ds:bp+OS88UI_MN_OPEN]
    cmp al, OS88UI_MN_NONE
    je .out
    call os88ui_mntxor                   ; the title back to normal video
    mov byte [ds:bp+OS88UI_MN_OPEN], OS88UI_MN_NONE
    mov byte [ds:bp+OS88UI_MN_HOT], 0xFF
    call os88ui_mnback              ; the banked pixels back...
    jnc .done                       ; ...or CF = 1, and the piecewise repaint
    push bx                         ; that was the only path before SPEC.md
    mov bx, [ds:bp+OS88UI_MN_RPNTH] ; 68.2.1 is owed HERE, not at whatever
    or bx, bx                       ; call happens to be outermost
    jz .noh
    call bx
.noh:
    pop bx
    stc                             ; ...and the answer stands for a caller
.done:                              ; that wants it as well
    pop ax
    ret
.out:
    clc
    pop ax
    ret

os88ui_mnpace:
    push ax
    push bx
    call OSAPI_GFX_UNLOCK
    call OSAPI_GET_TICKS
    mov bx, ax
.spin:
    call OSAPI_TASK_YIELD
    call OSAPI_GET_TICKS
    cmp ax, bx
    je .spin
    call OSAPI_GFX_LOCK
    pop bx
    pop ax
    ret

os88ui_mntrack:
    push ax
    push bx
    push cx
    push dx
    call os88ui_mnopen
.loop:
    call os88ui_mnpace                 ; unlock - yield to the tick - relock
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .release
    mov al, [ds:bp+OS88UI_MN_OPEN]  ; AL is dead here - the buttons were spent
    cmp al, [ds:bp+OS88UI_MN_N]     ; on the test above - and a byte in the
                                    ; record cannot be compared to another.
    jae .items                      ; Dragging across the bar slides between
                                    ; menus; NONE is 0FFh and so above N, which
                                    ; is the whole of what this now excludes
                                    ; (it used to exclude an anchored list too)
    call os88ui_mnbarhit
    cmp al, 0xFF
    je .items
    cmp al, [ds:bp+OS88UI_MN_OPEN]
    je .items
    push ax
    call os88ui_mnclose
    pop ax
    call os88ui_mntitler
    call os88ui_mnopen
    jmp short .loop
.items:
    call os88ui_mnfind                   ; the XOR highlight follows the pointer
    cmp al, [ds:bp+OS88UI_MN_HOT]
    je .loop
    push ax
    mov al, [ds:bp+OS88UI_MN_HOT]
    call os88ui_mnhl                     ; old band off (0xFF-safe)...
    pop ax
    mov [ds:bp+OS88UI_MN_HOT], al
    call os88ui_mnhl                     ; ...new band on
    jmp short .loop
.release:
    call os88ui_mnfind
    cmp al, 0xFF
    jne .fire
    push bx
    lea bx, [bp+OS88UI_MN_ABOX]   ; LEA is arithmetic: no SS
    call os88ui_bhit                ; released back on the anchor?
    pop bx
    jc .away
    jmp short .out                  ; yes: STICKY - the menu stays open
.away:
    call os88ui_mnclose                  ; released elsewhere: dismissed
    jmp short .out
.fire:
    mov [ds:bp+OS88UI_MN_PICKI], al ; THE PICK IS THE CALLER'S (SPEC.md
                                    ; 13.16.4): the caller's action lookup reads which
                                    ; is open, so the menu is still open here
                                    ; and the close is the caller's too

.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

os88ui_mnclickopen:
    push ax
    push dx
    call os88ui_mnfind
    cmp al, 0xFF
    jne .fire
    push bx
    lea bx, [bp+OS88UI_MN_ABOX]   ; LEA is arithmetic: no SS
    call os88ui_bhit
    pop bx
    jnc .toggle
    call os88ui_mninrect                 ; a separator or disabled item: stay open
    jnc .out
    mov al, [ds:bp+OS88UI_MN_OPEN]  ; AL is dead here - the buttons were spent
    cmp al, [ds:bp+OS88UI_MN_N]     ; on the test above - and a byte in the
                                    ; record cannot be compared to another;
    jae .dismiss                    ; NONE is 0FFh and so above N
    call os88ui_mnbarhit                 ; a click on another title slides there
    cmp al, 0xFF
    je .dismiss
    cmp al, [ds:bp+OS88UI_MN_OPEN]
    je .toggle
    push ax
    call os88ui_mnclose
    pop ax
    call os88ui_mntitler
    call os88ui_mntrack                  ; ...and the new press tracks as a press
    jmp short .out
.toggle:
.dismiss:
    call os88ui_mnclose
    jmp short .out
.fire:
    mov [ds:bp+OS88UI_MN_PICKI], al ; THE PICK IS THE CALLER'S (SPEC.md
                                    ; 13.16.4): the caller's action lookup reads which
                                    ; is open, so the menu is still open here
                                    ; and the close is the caller's too

.out:
    pop dx
    pop ax
    ret

%endif                          ; OS88UI_MENU

%endif


%ifdef OS88UI_KERNEL
section .bss
; -----------------------------------------------------------------------------
; os88ui_krect - THE kernel's button rect, and there is exactly one on purpose
;
; Every kernel caller builds its rect here and calls straight into os88ui_btn,
; which only READS it - so one scratch serves the file dialog, both Control
; Panel pages and the Timer. They are all UI-task, all under the gfx lock, and
; nothing in the drawing path re-enters, so two can never be in flight.
;
; **IT IS IN .bss, AND THAT IS A RULE RATHER THAN A PREFERENCE.** A cold
; module has a CS of its own and DS is STILL KERNEL_SEG (SPEC.md 2.6), so a
; word declared in `.cold` and reached through DS is not that word at all - it
; is whatever lives at the same offset in `.text`. Three of these were
; declared in .cold for one commit, and the write and the read used the same
; wrong address, so the buttons drew perfectly while `fdlg_brect` put eight
; bytes of screen coordinates through the middle of **sch_isr** - the PIT tick
; handler - and `cp_brect` through **wm_destroy**. Nothing failed, no gate
; caught it, and the only reason it was found is that a symbol dump was open
; for a different question. ctrl.inc's radio bitmaps carry the same rule in
; their own comment and had it right; this is what forgetting it costs.
; -----------------------------------------------------------------------------
os88ui_krect: resw 4
os88ui_krlbl: resw 1            ; ...and the one control's staging beside it
os88ui_krflg: resw 1            ; (SPEC.md 20.5.1.3). The kernel cannot use
os88ui_krec:  resw OS88UI_BT_SIZE / 2
                                ; OS88UI_BTNREC - that macro is in
                                ; apps/os88api.inc, which the kernel does not
                                ; include - so this record is .bss and
                                ; os88ui_kbtn aims it at the call. It is ONE
                                ; record for os88ui_krect's own reason: every
                                ; kernel user is the UI task under the gfx
                                ; lock, and a gesture is one press and one
                                ; release


%ifdef OS88UI_SCROLL
; -----------------------------------------------------------------------------
; os88ui_ksb - THE kernel's scroll-bar block, and there is one for the same
; reason os88ui_krect is one: every kernel user of it is the UI task under the
; gfx lock, and nothing in a drawing or a hit-testing path re-enters. A package
; declares its own (the block is seven words of anything).
; -----------------------------------------------------------------------------
os88ui_ksb: resw 7
%ifdef OS88UI_SBDRAG
; The thumb drag's state (SPEC.md 13.10.5.6), and it is one for os88ui_ksb's
; reason: one gesture at a time on the one UI task. .bss and not .cold - see
; os88ui_krect's comment above for what forgetting that costs.
os88ui_sbd_on:   resb 1         ; 1 = a thumb drag is live
os88ui_sbd_rate: resb 1         ; the window's throttle, in ticks; 0 = the
                                ; release commits and nothing before it
os88ui_sbd_off:  resw 1         ; the anchor: press y - thumb top
os88ui_sbd_pos:  resw 1         ; the pos the HAND is at (not the view's)
os88ui_sbd_last: resw 1         ; [ticks] at the last commit
os88ui_sbd_id:   resw 2         ; WHICH BAR: its x1 and y1 (SPEC.md 13.10.5.10)
%endif
%endif

; -----------------------------------------------------------------------------
; os88ui_armw - THE kernel's arm, and there is exactly one on purpose
;
; os88ui_krect's argument exactly: every kernel caller is the UI task under the
; gfx lock, a gesture is one press and one release, and nothing in the dispatch
; path re-enters - so two can never be in flight. The file dialog is MODAL
; (SPEC.md 38.2), which makes it stronger still there: while one is up nothing
; else is clickable at all.
;
; .bss and not .cold, for the reason the block above gives.
; -----------------------------------------------------------------------------
%ifdef OS88UI_ARM
os88ui_armw: resw 1
%endif
section .cold

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; os88ui_kbtn - the kernel's ONE-BUTTON staging for os88ui_btn (20.5.1.3)
; in:  BX = the rect, SI = the label, DI = the flags; the gfx lock is held
; out: nothing; all registers preserved
;
; The kernel's buttons are staged one at a time through os88ui_krect rather
; than described as a contiguous group, and its gesture is its own arm
; (SPEC.md 13.8.3) rather than the record's - so this aims the record at
; whichever rect was staged and draws it. It exists so that os88ui_btnraw
; could be DELETED: there is one way to draw a button and this is the kernel
; reaching it, not a second control.
; -----------------------------------------------------------------------------
os88ui_kbtn:
    push ax
    push bx
    mov [os88ui_krlbl], si
    mov [os88ui_krflg], di
    mov [os88ui_krec+OS88UI_BT_RECTS], bx
    mov word [os88ui_krec+OS88UI_BT_LABELS], os88ui_krlbl
    mov word [os88ui_krec+OS88UI_BT_FLAGS], os88ui_krflg
    mov word [os88ui_krec+OS88UI_BT_N], 1
    mov word [os88ui_krec+OS88UI_BT_DOWN], 0
    mov bx, os88ui_krec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88ui_btn_f / os88ui_glyph_f - the far entries (SPEC.md 2.6)
;
; The bodies above are near procs in .cold, which is where MOST of their
; callers are - fdlg.inc is cold in its entirety. Two kinds of caller are
; not, and both reach them through here, the same fdf_/cpf_ shape every other
; crossing in this kernel uses: apps.inc, which is .text (the Timer's button),
; and ctrl.inc, which is an ON-DEMAND MODULE now (SPEC.md 2.8) and so runs
; from a heap claim with a CS of its own.
;
; The Control Panel is why the glyph entry exists. Its eleven near calls to
; os88ui_glyph and os88ui_btn were legal for as long as ctrl.inc was cold and
; became a jump into its own image the day it stopped being - which
; tools/os88ovlchk.py could not see, because it globbed kernel/*.inc and this
; file is not one. It scans this file now; that is the more important half of
; the fix, and the EXTRA table there is where a third such file goes.
;
; Packages never see either: both are inside %ifdef OS88UI_KERNEL.
; -----------------------------------------------------------------------------
os88ui_btn_f:
    call os88ui_kbtn            ; **os88ui_kbtn AND NOT os88ui_btn.** This
    retf                        ; entry's published contract is BX = a RECT,
                                ; SI = the label, DI = the flags - the
                                ; loose-register one - and the far callers are
                                ; ctrl.inc and hiber.inc, which are on-demand
                                ; modules with a CS of their own and cannot be
                                ; converted by editing apps/ or kernel/ alone.
                                ; os88ui_kbtn is exactly that contract staged
                                ; into the one control, so the far side is
                                ; unchanged and there is still only one
                                ; painter underneath.
                                ;
                                ; It pointed straight at os88ui_btn for one
                                ; build and that was a REGRESSION: os88ui_btn
                                ; takes a RECORD now, so a rect arrived where
                                ; a record was expected, the live count and
                                ; the rect pointer were read out of the rect's
                                ; own coordinates, and the Control Panel's
                                ; Date/Time page filled the whole screen white
                                ; while Theme lost its buttons. A FAR call is
                                ; invisible to a grep for `call os88ui_btn`,
                                ; which is how the sweep missed both.
%ifdef OS88UI_ARM
; ...and the arm's three, for ctrl.inc - which is an ON-DEMAND MODULE
; (SPEC.md 2.8) running from a heap claim with a CS of its own, so it reaches
; everything here by far call exactly as it reaches os88ui_btn.
os88ui_arm_f:
    call os88ui_arm
    retf
os88ui_fire_f:
    call os88ui_fire
    retf
os88ui_armed_f:
    call os88ui_armed
    retf
%endif
os88ui_glyph_f:
    call os88ui_glyph
    retf
%endif
