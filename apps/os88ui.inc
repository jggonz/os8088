; =============================================================================
; os8088 - apps/os88ui.inc
;
; THE STANDARD CONTROL, and it is ONE SOURCE FOR TWO WORLDS: this file
; assembles into the KERNEL's own dialogs and into any package that wants it,
; from the same text. Define OS88UI_KERNEL before including it and every
; primitive call below becomes the kernel's own (`KERNEL_SEG:cw_*`, the
; cold-to-text shims of SPEC.md 2.6); leave it undefined and they become
; `OSAPI_*` far calls. Nothing else differs.
;
; THAT IS THE WHOLE POINT OF THE FILE. Saving bytes was never the argument -
; measured, consolidating the kernel's private helpers was worth twelve
; (docs/UIHELPERS-PLAN.md 7.1). The argument is that a feature added to the
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
; =============================================================================

%ifdef OS88UI_KERNEL
  %macro UI_FRAME 0
    call KERNEL_SEG:cw_gfx_frame
  %endmacro
  %macro UI_FILL 0
    call KERNEL_SEG:cw_gfx_fill
  %endmacro
  %macro UI_STR 0
    call KERNEL_SEG:cw_font_str
  %endmacro
  %macro UI_WIDTH 0
    call KERNEL_SEG:cw_font_width
  %endmacro
  %macro UI_PEN 0
    call KERNEL_SEG:cw_gfx_pen_cf
  %endmacro
  %macro UI_PIXEL 0
    call KERNEL_SEG:cw_gfx_pixel
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
    call KERNEL_SEG:cw_gfx_hline
  %endmacro
  %macro UI_GRAY 0
    call KERNEL_SEG:cw_gfx_fill_gray
  %endmacro
%else
  %macro UI_FRAME 0
    call OSAPI_GFX_FRAME
  %endmacro
  %macro UI_FILL 0
    call OSAPI_GFX_FILL
  %endmacro
  %macro UI_STR 0
    call OSAPI_FONT_STR
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
                                ; docs/UIHELPERS-PLAN.md 13.4). Set it unless
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

; -----------------------------------------------------------------------------
; os88ui_btn - draw one standard button
; in:  BX = rect {x1,y1,x2,y2} inclusive, SI = NUL label, DI = OS88UI_* flags
;      caller holds the gfx lock (every callback and every kernel painter does)
; out: nothing; all registers preserved
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
os88ui_btn:
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
    UI_STR
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
; It is not cheap: 44 set bits for an empty box and 64 for a crossed one,
; one drawing call each, which at PERFORMANCE.md Part 2's ~756us of fixed
; cost is 35-50ms on the field machine. Draw the ROW that changed, never the
; page.
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
os88ui_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov di, ax                  ; DI = the whole argument: AX is about to be a
                                ; rect corner for the white box, so the
                                ; DISABLED byte cannot stay in AH. It did in
                                ; the first version, `mov ax, cx` overwrote it
                                ; with the high byte of an x coordinate, and
                                ; every glyph on every page came out GREYED -
                                ; which assembles, boots, and draws a
                                ; perfectly plausible ring
    mov si, di                  ; SI = the index, times the 24 bytes a bitmap
    and si, 3                   ; is (8086: no shl by an immediate but 1)
    shl si, 1
    shl si, 1
    shl si, 1                   ; SI = i * 8
    mov bx, si
    shl bx, 1                   ; BX = i * 16
    add si, bx                  ; SI = i * 24
    add si, os88ui_g_roff       ; ...and the four are contiguous, asserted
                                ; below. A table of four pointers is SHORTER
                                ; than this by two bytes, and it was one until
                                ; it was measured: a table is DATA and lands
                                ; in .text, which had THREE bytes of rung left,
                                ; so eight bytes of table cost a whole
                                ; 512-byte step of KERN_BUDGET. The arithmetic
                                ; is CODE and lands in .cold beside the body,
                                ; where there are 479 bytes going spare. Same
                                ; kernel, one rung cheaper, and in a package
                                ; the distinction does not exist at all
    mov bp, cx                  ; BP = left x: every row's pen restarts here
                                ; (a VALUE, never an address - SS is not DS
                                ; in a package)

    UI_WHITE                    ; the box first, so a glyph can be redrawn
    call os88ui_gdn             ; in place. [gfx_dis] does not reach a fill,
    jnz .box                    ; so a stale one cannot tint it - and a
    UI_BLACK                    ; PRESSED glyph is that box in black, over
.box:                           ; which the picture below goes white
    mov ax, cx
    mov bx, dx
    add cx, OS88UI_GW - 1
    add dx, OS88UI_GW - 1
    UI_FILL                     ; AX,BX,CX,DX = x1,y1,x2,y2; BX survives it

    test di, 0xFF00             ; ...then the pen, both halves at once
    jz .live                    ; (SPEC.md 47 rule 1) - the disabled byte out
                                ; of the bank above, never out of AH
    stc
    jmp short .set
.live:
    clc
.set:
    UI_PEN
    call os88ui_gdn             ; ...and the picture in white if it is down,
    jnz .pixpen                 ; which UI_PEN has just overwritten with the
    UI_WHITE                    ; live or the disabled ink
.pixpen:

    mov dx, bx                  ; DX = pen y, back at the top row
    mov bh, OS88UI_GW           ; BH = rows left
.row:
    lodsw                       ; AX = the row's bits, MSB leftmost (DF = 0
    mov cx, bp                  ; by contract); CX = pen x
    mov bl, OS88UI_GW           ; BL = columns left
.col:
    shl ax, 1
    jnc .skip
    UI_PIXEL
.skip:
    inc cx
    dec bl
    jnz .col
    inc dx
    dec bh
    jnz .row

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

%ifdef OS88UI_KERNEL
section .text                   ; DATA, and SPEC.md 2.6 rule 1 is why: a table
%endif                          ; declared in .cold and walked through DS is
                                ; not that table. In a package there are no
                                ; sections and the same text is simply inline
; The four are ONE array, indexed by (kind | on), and os88ui_glyph reaches
; them by arithmetic rather than through a table of pointers - so the order
; and the spacing are load-bearing and are asserted at the bottom.
os88ui_g_roff:                  ; radio, clear: an open ring
    dw 0x0F00                   ;  0  ....####....
    dw 0x30C0                   ;  1  ..##....##..
    dw 0x4020                   ;  2  .#........#.
    dw 0x4020                   ;  3  .#........#.
    dw 0x8010                   ;  4  #..........#
    dw 0x8010                   ;  5  #..........#
    dw 0x8010                   ;  6  #..........#
    dw 0x8010                   ;  7  #..........#
    dw 0x4020                   ;  8  .#........#.
    dw 0x4020                   ;  9  .#........#.
    dw 0x30C0                   ; 10  ..##....##..
    dw 0x0F00                   ; 11  ....####....

os88ui_g_ron:                   ; radio, set: the same ring, 4x4 centre dot
    dw 0x0F00                   ;  0  ....####....
    dw 0x30C0                   ;  1  ..##....##..
    dw 0x4020                   ;  2  .#........#.
    dw 0x4020                   ;  3  .#........#.
    dw 0x8F10                   ;  4  #...####...#
    dw 0x8F10                   ;  5  #...####...#
    dw 0x8F10                   ;  6  #...####...#
    dw 0x8F10                   ;  7  #...####...#
    dw 0x4020                   ;  8  .#........#.
    dw 0x4020                   ;  9  .#........#.
    dw 0x30C0                   ; 10  ..##....##..
    dw 0x0F00                   ; 11  ....####....

os88ui_g_coff:                  ; check, clear: an open square
    dw 0xFFF0                   ;  0  ############
    dw 0x8010                   ;  1  #..........#
    dw 0x8010                   ;  2  #..........#
    dw 0x8010                   ;  3  #..........#
    dw 0x8010                   ;  4  #..........#
    dw 0x8010                   ;  5  #..........#
    dw 0x8010                   ;  6  #..........#
    dw 0x8010                   ;  7  #..........#
    dw 0x8010                   ;  8  #..........#
    dw 0x8010                   ;  9  #..........#
    dw 0x8010                   ; 10  #..........#
    dw 0xFFF0                   ; 11  ############

os88ui_g_con:                   ; check, set: the same square, crossed
    dw 0xFFF0                   ;  0  ############
    dw 0xC030                   ;  1  ##........##
    dw 0xA050                   ;  2  #.#......#.#
    dw 0x9090                   ;  3  #..#....#..#
    dw 0x8910                   ;  4  #...#..#...#
    dw 0x8610                   ;  5  #....##....#
    dw 0x8610                   ;  6  #....##....#
    dw 0x8910                   ;  7  #...#..#...#
    dw 0x9090                   ;  8  #..#....#..#
    dw 0xA050                   ;  9  #.#......#.#
    dw 0xC030                   ; 10  ##........##
    dw 0xFFF0                   ; 11  ############

; ...and the invariant the arithmetic rests on, checked by the assembler
; rather than by whoever edits the pictures. Reordering them or dropping a
; row would otherwise draw a perfectly plausible wrong glyph.
%if (os88ui_g_ron - os88ui_g_roff) != OS88UI_GW * 2
  %error "os88ui: radio-on is not one bitmap past radio-off"
%endif
%if (os88ui_g_coff - os88ui_g_ron) != OS88UI_GW * 2
  %error "os88ui: check-off is not one bitmap past radio-on"
%endif
%if (os88ui_g_con - os88ui_g_coff) != OS88UI_GW * 2
  %error "os88ui: check-on is not one bitmap past check-off"
%endif
%if (OS88UI_GRADIO | OS88UI_GON) != 1 || OS88UI_GCHECK != 2
  %error "os88ui: the glyph indices no longer walk the array in order"
%endif
%ifdef OS88UI_KERNEL
section .cold
%endif

; --- the click machinery: every PACKAGE, and kern_BIG ------------------------
; A package always has it - it is four small routines and a word, and the
; press/release pair is the SDK's own idiom. The kernel has it only in the big
; build, because kern_small's dialogs still fire on the press (SPEC.md 13.8.3
; is kern_big's, docs/UIHELPERS-PLAN.md 18) and 40 bytes of .cold plus two of
; .bss is a whole 512-byte rung on the guard the 128KB machine lives under -
; measured, not assumed: making these unconditional cost kern_small exactly
; that and nothing else.
%ifdef OS88UI_KERNEL
  %ifdef KERN_BIG
    %define OS88UI_ARM
  %endif
%else
  %define OS88UI_ARM
%endif
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
    push ax                     ; ...banked, because the offset needs the mul
    mov ax, [ds:bx+12]          ; pos * track / total = the offset down it
    mul cx
    div word [ds:bx+8]
    pop dx                      ; DX = the height
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
    UI_WHITE
    UI_FILL
    UI_BLACK
    UI_FRAME
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
; =============================================================================

OS88UI_AOK     equ 0            ; the button sets. Index 0 is the DEFAULT - it
OS88UI_AYESNO  equ 1            ; carries the ring and it is what Enter fires
OS88UI_ASAVE   equ 2            ; Save / Discard / Cancel
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

; the sets: a count byte then three label offsets, 8 bytes a row so the index
; is a shift (an 8086 has no cheap multiply by 7)
os88ui_asets:
    dw 1, os88ui_as_ok,   0,             0
    dw 2, os88ui_as_yes,  os88ui_as_no,  0
    dw 3, os88ui_as_save, os88ui_as_disc, os88ui_as_can
os88ui_as_ok:   db 'OK', 0
os88ui_as_yes:  db 'Yes', 0
os88ui_as_no:   db 'No', 0
os88ui_as_save: db 'Save', 0
os88ui_as_disc: db 'Discard', 0
os88ui_as_can:  db 'Cancel', 0

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
    mov ax, os88ui_aclose       ; 13.6), and the close box is intercepted
    call OSAPI_WM_ONCLOSE       ; (SPEC.md 75.1) so it can be the CANCEL
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
    push bx
    push cx
    push dx
    push si
    push di
    call os88ui_aset_p          ; SI -> the labels, CX = the count
    xor dx, dx                  ; DX = the index
.next:
    push cx
    push si
    mov ax, dx
    call os88ui_arect           ; BX -> the rect
    mov di, OS88UI_FILL
    or dx, dx
    jnz .nodef
    or di, OS88UI_DEF           ; button 0 carries the ring, and Enter fires
.nodef:                         ; it - one fact, drawn and acted on from the
    cmp dl, [os88ui_adown]      ; same place
    jne .nodown
    or di, OS88UI_DOWN          ; ...and the pressed look, DRAWN and not
.nodown:                        ; XOR-ed (SPEC.md 13.8)
    mov si, [si]
    call os88ui_btn
    pop si
    add si, 2
    inc dx
    pop cx
    loop .next
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
    call OSAPI_FONT_STR
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
    mov [os88ui_adown], al
    call os88ui_abtns
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

os88ui_aup:
    push ax
    push di
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov di, ax
    xor ax, ax
    call os88ui_adn             ; upright again, whatever happens next
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

%ifdef OS88UI_SCROLL
; -----------------------------------------------------------------------------
; os88ui_ksb - THE kernel's scroll-bar block, and there is one for the same
; reason os88ui_krect is one: every kernel user of it is the UI task under the
; gfx lock, and nothing in a drawing or a hit-testing path re-enters. A package
; declares its own (the block is seven words of anything).
; -----------------------------------------------------------------------------
os88ui_ksb: resw 7
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
    call os88ui_btn
    retf
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
