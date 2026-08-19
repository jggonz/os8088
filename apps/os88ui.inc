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
