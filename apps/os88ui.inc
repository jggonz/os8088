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
  %macro UI_INK 0
    mov [gfx_color], al
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
  %macro UI_INK 0
    call OSAPI_SET_COLOR
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
    test di, OS88UI_FILL
    jz .pen
    UI_WHITE
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
.rect:
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
    mov ax, cx                  ; in place. [gfx_dis] does not reach a fill,
    mov bx, dx                  ; so a stale one cannot tint it
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

%ifndef OS88UI_KERNEL
; --- the package-side click machinery ----------------------------------------
; Not assembled into the kernel: fdlg.inc and ctrl.inc have their own hit
; tests (fdlg_hit, cp_onclick's ladders) and nothing kernel-side arms a
; control yet. The code is here rather than elsewhere so that the day a
; kernel dialog wants a release-fired button, it is this gate that moves and
; not a second copy that gets written.

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

os88ui_armw: dw 0
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
os88ui_glyph_f:
    call os88ui_glyph
    retf
%endif
