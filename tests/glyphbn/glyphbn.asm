; =============================================================================
; os8088 - tests/glyphbn/glyphbn.asm
;
; WHAT ONE CONTROL GLYPH COSTS, the bitmap way and the fill way, IN ONE BINARY.
;
; SPEC.md 13.15.1 replaced os88ui_glyph's four 12x12 bitmaps and the masked
; sprite pass with three to eight fills. docs/plans/completed/CTRL-GLYPH-PLAN.md 4 asks
; what that trade cost per call, and the owner set the bar: *"between 1ms and
; 2ms is not huge - this is not a live drawing, it's drawn once then it sits
; there until they interact with it"*. So this exists to catch a REGRESSION,
; not to second-guess a design that was decided on look and on size.
;
; WHY BOTH IMPLEMENTATIONS ARE IN ONE PACKAGE rather than one per checkout. An
; A/B across two trees changes the KERNEL under the measurement as well as the
; routine, and this arc removed the whole gfx_line family from that kernel -
; so a cross-tree figure would carry a second variable it could not separate.
; gbo_glyph below is the pre-13.15.1 routine LIFTED VERBATIM out of
; apps/os88ui.inc at 2324ede, renamed and otherwise untouched; os88ui_glyph is
; today's, out of the library this package includes. One kernel, one boot, one
; adapter, the same coordinates - the only thing that differs between two arms
; is the code being timed.
;
; EVERYTHING IS DRAWN INSIDE THE WINDOW, from OSAPI_WM_CONTENT, and that is not
; cosmetic. A glyph's argument is a SCREEN point, so a bench with a constant in
; it draws over the menu bar - against whatever clip the handler happened to
; inherit, and at whatever that costs, which is not what a control costs.
;
; TWO KEYS, one package:
;   'a'  THE COST ARMS. Nine bracketed spans, read by tests/glyphcost.py off
;        MartyPC's cycle counter. The counter does not advance while a
;        breakpoint holds the guest, so a span is exact guest cycles and not a
;        host timing. GB_N calls an arm, and an EMPTY arm of the same shape
;        measures the harness so the per-call figure can have it taken off.
;   any  THE SHAPE GRID. All four kinds drawn twice, bitmap column and fill
;        column, so the row can diff the two pictures. A cost table is only a
;        comparison if both arms draw the same control.
;
; The cost arms draw at ONE point on purpose. Overdrawing is what a control
; does when a selection moves, it keeps the clip state and the adapter path
; identical between arms, and a glyph that moved would price its own layout.
;
; Prefix gb_ (the bench) and gbo_ (the OLD routine under test).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'GLYPHBN', gb_entry

; --- THE LIBRARY GOES FIRST HERE, and that is NOT radtest's placement --------
; A package's own code normally comes first and os88ui.inc last (radtest,
; apps/skies), because the include EMITS CODE and OS88_HEADER has to be the
; image's first bytes. Here it has to be in front of gbo_glyph instead: the
; lifted routine is written against UI_FILL, UI_ICON and the OS88UI_G*
; constants, and a MACRO - unlike a label - has to be defined before the line
; that uses it. Putting it last gets `label alone on a line without a colon`
; from every UI_ macro in the old body, which reads like a syntax error in
; code that was correct when it shipped.
%include "os88ui.inc"

; gb_gdn - the PRE-size-pass os88ui_gdn, lifted verbatim with the old body
; below: os88ui_glyph decides "pressed" once now and has no such predicate,
; so the bench carries the one its lifted routine was written against.
; in:  DI = the banked argument word; out: ZF = 1 draw it down
gb_gdn:
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

GB_X       equ 88                   ; the COST arms, in CONTENT px - clear of
GB_Y       equ 20                   ; the shape grid, so the two keys never
                                    ; overdraw each other and a shape reading
                                    ; cannot be a cost arm's leftovers
GB_SX      equ 8                    ; the shape grid: the BITMAP column...
GB_SX2     equ GB_SX + 20           ; ...and the FILL column, a box and a gap
GB_SY      equ 8
GB_SPITCH  equ 16
GB_N       equ 8                    ; calls an arm. Cycles are EXACT at N=1;
                                    ; eight is against a first call paying for
                                    ; something the next seven do not
GB_BSS     equ 16

; -----------------------------------------------------------------------------
gb_entry:
    push si
    mov si, gb_tpl
    call OSAPI_WM_CREATE
    mov [gb_win], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; gb_place - the window's content origin into [gb_gx]/[gb_gy]
; in:  SI = the window
;
; rt_place's job (tests/radtest), one control simpler: every drawing call below
; wants a SCREEN point and the window moves, so the origin is read from the
; kernel each time rather than remembered.
; -----------------------------------------------------------------------------
gb_place:
    push ax
    push bx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov [gb_gx], ax
    mov [gb_gy], dx
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_paint - W_ONPAINT: ONE glyph, so the window is not blank, and NO markers.
;
; The bench is on a KEY and not on the paint, deliberately: a marker address
; must be reached ONCE per run for a span to mean anything, and a window gets
; repainted whenever something uncovers it - a mouse move over a menu, a
; settle, the harness raising it. Hanging the arms off a key makes the run the
; row's to trigger, and the WM holds the gfx lock around an event handler just
; as it does around a paint (UI-FREEZE-PLAN 1), so the arms are as legal there.
; -----------------------------------------------------------------------------
gb_paint:
    push ax
    push cx
    push dx
    call gb_place
    mov ax, OS88UI_GCHECK
    mov cx, [gb_gx]
    add cx, GB_X
    mov dx, [gb_gy]
    add dx, GB_Y
    call os88ui_glyph
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_onkey - W_ONKEY (AL = ascii): 'a' is THE BENCH, anything else the shapes.
;
; Two keys rather than two packages: the cost arms and the shape columns want
; the same two routines in the same image, and splitting them would let the
; pair drift apart.
; -----------------------------------------------------------------------------
gb_onkey:
    cmp al, 'a'
    je .bench
    cmp al, 'A'
    je .bench
    jmp gb_shape
.bench:

; -----------------------------------------------------------------------------
; THE COST ARMS.
;
; Each is `<label> ... <label>_e`, and NOTHING may sit between the two that is
; not the thing being priced: the pushes are outside, the origin is resolved
; before the first arm, and the counter is bumped before it too. Every arm has
; the SAME shape - push, three argument loads, the call, pop, loop - so the
; empty one below subtracts the whole of the harness rather than most of it.
; -----------------------------------------------------------------------------
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call gb_place
    mov si, [gb_gx]
    add si, GB_X                ; SI/DI = the arms' screen point, resolved ONCE
    mov di, [gb_gy]             ; and held in registers no arm's body touches
    add di, GB_Y
    inc word [gb_runs]

gb_m_nul:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GCHECK
    mov cx, si
    mov dx, di
    call gb_nul
    pop cx
    loop .l
gb_m_nul_e:

    ; --- OLD: the bitmap glyph through the masked sprite pass ---------------
gb_m_ocoff:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GCHECK
    mov cx, si
    mov dx, di
    call gbo_glyph
    pop cx
    loop .l
gb_m_ocoff_e:

gb_m_ocon:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GCHECK | OS88UI_GON
    mov cx, si
    mov dx, di
    call gbo_glyph
    pop cx
    loop .l
gb_m_ocon_e:

gb_m_oroff:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GRADIO
    mov cx, si
    mov dx, di
    call gbo_glyph
    pop cx
    loop .l
gb_m_oroff_e:

gb_m_oron:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GRADIO | OS88UI_GON
    mov cx, si
    mov dx, di
    call gbo_glyph
    pop cx
    loop .l
gb_m_oron_e:


    ; --- NEW: the same four, drawn with fills (SPEC.md 13.15.1) -------------
gb_m_ncoff:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GCHECK
    mov cx, si
    mov dx, di
    call os88ui_glyph
    pop cx
    loop .l
gb_m_ncoff_e:

gb_m_ncon:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GCHECK | OS88UI_GON
    mov cx, si
    mov dx, di
    call os88ui_glyph
    pop cx
    loop .l
gb_m_ncon_e:

gb_m_nroff:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GRADIO
    mov cx, si
    mov dx, di
    call os88ui_glyph
    pop cx
    loop .l
gb_m_nroff_e:

gb_m_nron:
    mov cx, GB_N
.l:
    push cx
    mov ax, OS88UI_GRADIO | OS88UI_GON
    mov cx, si
    mov dx, di
    call os88ui_glyph
    pop cx
    loop .l
gb_m_nron_e:

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_nul - the same call/ret shape as an arm's body, drawing nothing. Its arm
; is what a per-call figure has SUBTRACTED, so the answer is the routine and
; not the harness around it.
; -----------------------------------------------------------------------------
gb_nul:
    ret

; -----------------------------------------------------------------------------
; gb_shape - the four glyphs BOTH WAYS, side by side, so the cost table is
; known to be comparing two pictures of the same control and not two different
; controls.
;
; The bitmap column is at GB_SX and the fill column a box and a gap right of
; it; each row is one kind, at GB_SPITCH. The row reads both columns off the
; framebuffer and diffs them - a question no count can answer and no assertion
; in tests/radio.py asks, because that row only ever sees today's.
; -----------------------------------------------------------------------------
gb_shape:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call gb_place
    mov bx, gb_kinds
    xor di, di                  ; DI = the row
.row:
    mov ax, [bx]
    mov cx, [gb_gx]
    add cx, GB_SX
    mov dx, [gb_gy]
    add dx, GB_SY
    add dx, di
    call gbo_glyph
    mov ax, [bx]
    mov cx, [gb_gx]
    add cx, GB_SX2
    mov dx, [gb_gy]
    add dx, GB_SY
    add dx, di
    call os88ui_glyph
    add di, GB_SPITCH
    add bx, 2
    cmp bx, gb_kinds_end
    jb .row
    inc word [gb_shapes]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

gb_kinds:
    dw OS88UI_GRADIO
    dw OS88UI_GRADIO | OS88UI_GON
    dw OS88UI_GCHECK
    dw OS88UI_GCHECK | OS88UI_GON
gb_kinds_end:

; -----------------------------------------------------------------------------
gb_tpl:
    dw 120, 30, 140, 110        ; the shape grid is 4 rows of 16 from y=8 and
                                ; two 12px columns from x=8; the cost arms
                                ; draw at (88,20), right of both
    dw gb_ttl, gb_paint, gb_onkey, 0

gb_ttl:  db 'GlyphBench', 0

; =============================================================================
; THE PRE-13.15.1 GLYPH, lifted verbatim from apps/os88ui.inc at 2324ede.
;
; DO NOT "TIDY" IT and do not fix anything in it. Its whole value is being the
; code that shipped: an edit here makes the arm measure something that never
; ran, and the comparison stops being one. The only changes made were the
; symbol prefix (os88ui_ -> gbo_ , so both routines fit one namespace) and
; dropping the `%ifdef OS88UI_KERNEL` section switch, which a package has no
; sections for.
; =============================================================================
gbo_glyph:
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
    add si, gbo_g_roff       ; ...and the four are contiguous, asserted
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

    ; --- ...but is the whole 12x12 box drawable? (SPEC.md 11.3) ------
    ; ico_core clips an icon WHOLE - one shape or none, the font_char rule -
    ; so a control a clip fragment's edge cuts draws NOTHING through the pass
    ; below, where the pixel arm draws the visible half: the gfx_* fills clip
    ; per pixel. That is the pair the SDK publishes this slot for. It is asked
    ; here and not after the compose because DI still carries the argument
    ; and SI still points at the picture at this line.
    mov ax, cx
    mov bx, dx
    add cx, OS88UI_GW - 1
    add dx, OS88UI_GW - 1
    UI_CLIPT                    ; preserves every register, and answers CF = 0
    mov cx, ax                  ; when no clip is armed at all
    mov dx, bx
    jc .gpix

    ; --- ONE CALL, through the masked sprite pass (SPEC.md 25.6) -----
    ; `.gpix` below is the ORIGINAL, and is what a cut control still draws
    ; with: a white box and then one drawing call per set bit, 45 to 65 of
    ; them, 24-34 ms of the field machine's time for one 12x12 control
    ; (PERFORMANCE.md Set 83) - a price paid where nothing else can. A glyph
    ; is a 1bpp shape with a mask, which is exactly what ico_core draws - so
    ; the mask rows are the 12x12 box, the data rows are the bitmap this
    ; routine already has, and the box and the picture arrive together.
    ;
    ; The pair: white box / black picture, exchanged when the control is
    ; PRESSED, and the picture in CDGRAY when it is disabled (SPEC.md 47
    ; rule 1). Disabled outranks down inside gb_gdn, so the two never
    ; both apply.
    mov ax, (CBLACK << 8) | CWHITE
    call gb_gdn
    jnz .gpair
    mov ax, (CWHITE << 8) | CBLACK
.gpair:
    mov cx, 0xFFFF              ; CX = the per-row dither, and 0xFFFF is none
    test di, 0xFF00
    jz .gdith
    mov ah, CDGRAY
    UI_ISMONO                   ; ...and on a 1bpp adapter a grey is a 50%
    jz .gdith                   ; STIPPLE that a mask pass has nowhere to put,
    mov cx, 0x5555              ; so the caller lays it into the data rows.
    mov bx, bp                  ; gfx_ink's dither class is screen-absolute in
    add bx, dx                  ; (x + y) parity, which is what this reads -
    test bl, 1                  ; and bit 15 of a row word is its LEFTMOST
    jz .gdith                   ; pixel, so an odd sum keeps the even columns
    mov cx, 0xAAAA
.gdith:
    push ax                     ; the pair, wanted after the compose
    push dx                     ; ...and y, which DX is about to stop being
    xor dx, dx                  ; DX = the row-to-row phase toggle: nothing
    cmp cx, 0xFFFF              ; unless this is a stipple, exactly as
    je .gnodt                   ; font_run's [font_rn_dt] is (SPEC.md 6.1.12)
    mov dx, 0xFFFF
.gnodt:
    mov bx, gbo_grec_d       ; only the DATA rows are composed: the header
    mov di, OS88UI_GW           ; and the 12 mask words are constants and are
.gdata:                         ; declared filled in
    lodsw
    and ax, cx
    xor cx, dx
    mov [bx], ax
    add bx, 2
    dec di
    jnz .gdata
    pop dx
    pop ax
    UI_IPEN                     ; AL = the box's colour, AH = the picture's
    mov cx, bp                  ; ...and back to the pen this was called with
    mov si, gbo_grec
    UI_ICON
    jmp short .gout
                                ; ...and no fallback for a REFUSAL, because
                                ; the kernel CANNOT refuse this record and the
                                ; branch that used to catch it could not have
                                ; drawn one (SPEC.md 25.6.1). The arm below is
                                ; the CLIP's, and is why the test above stands
                                ; where it does.
                                ;
                                ; icon_draw_x refuses a width that is not
                                ; ICO_STAGE_WW - ww = 0 and ww > 1 in one
                                ; compare - and rows = 0, rows > ICO_STAGE_H.
                                ; gbo_grec is `db 1, OS88UI_GW` - a
                                ; CONSTANT, one word a row and twelve rows -
                                ; so all four are decided at assembly time and
                                ; the %if below is that decision, made by the
                                ; build instead of by this comment. Grow the
                                ; glyph past the stage and it is a build
                                ; failure, which is what the branch was
                                ; standing in for.
                                ;
                                ; THE BRANCH WAS ALSO BROKEN, which is why it
                                ; went rather than being kept for luck. The
                                ; compose loop above leaves DI = 0 - it counts
                                ; rows down in DI - and DI carried the whole
                                ; argument including SPEC.md 47's disabled
                                ; byte, so `test di, 0xFF00` always fell to
                                ; .live and a DISABLED control drew in the
                                ; live pen with no [gfx_dis] dither: rule 1's
                                ; exact failure, a dead control that looks
                                ; alive. SI was wrong too - it points at
                                ; gbo_grec, so the per-pixel pass re-read
                                ; the record's header and its twelve constant
                                ; mask words as if they were the picture.
                                ; Both are the reason the arm below is reached
                                ; from ABOVE the compose, where DI is still
                                ; the argument and SI is still the bitmap.

.gpix:                          ; the CUT control (SPEC.md 11.3), pixel by
    UI_WHITE                    ; pixel: the box first, so a glyph can be
    call gb_gdn             ; redrawn in place. [gfx_dis] does not reach a
    jnz .box                    ; fill, so a stale one cannot tint it - and a
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
    call gb_gdn             ; ...and the picture in white if it is down,
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
.gout:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; (the kernel-only `section .text` switch is dropped: a package has no sections)
                                ; not that table. In a package there are no
                                ; sections and the same text is simply inline
; The four are ONE array, indexed by (kind | on), and gbo_glyph reaches
; them by arithmetic rather than through a table of pointers - so the order
; and the spacing are load-bearing and are asserted at the bottom.
; gbo_grec - THE control-glyph record (SPEC.md 25.6): a two-byte header,
; twelve mask words and twelve data words, handed to the masked sprite pass.
; The header and the mask are CONSTANTS - one word per row, twelve rows, and
; the mask is the 12x12 box every control fills - so they are declared filled
; in and only the data rows are composed per call.
;
; ONE of it, for os88ui_krect's reason: every caller is the UI task under the
; gfx lock and nothing in a drawing path re-enters. In `.text` and NOT `.bss`
; because it is initialised, and .text is writable RAM - the rule os88ui_krect
; states is about `.cold`, which a cold module reaches through a DS that is
; still KERNEL_SEG.
gbo_grec:
    db 1, OS88UI_GW             ; ww = 1 word a row, h = 12 rows
    times OS88UI_GW dw 0xFFF0   ; the mask: twelve bits, left-aligned
gbo_grec_d:
    times OS88UI_GW dw 0        ; ...and the data, written per call

gbo_g_roff:                  ; radio, clear: an open ring
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

gbo_g_ron:                   ; radio, set: the same ring, 4x4 centre dot
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

gbo_g_coff:                  ; check, clear: an open square
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

gbo_g_con:                   ; check, set: the same square, crossed
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
%if (gbo_g_ron - gbo_g_roff) != OS88UI_GW * 2
  %error "os88ui: radio-on is not one bitmap past radio-off"
%endif
%if (gbo_g_coff - gbo_g_ron) != OS88UI_GW * 2
  %error "os88ui: check-off is not one bitmap past radio-on"
%endif
%if (gbo_g_con - gbo_g_coff) != OS88UI_GW * 2
  %error "os88ui: check-on is not one bitmap past check-off"
%endif
%if (OS88UI_GRADIO | OS88UI_GON) != 1 || OS88UI_GCHECK != 2
  %error "os88ui: the glyph indices no longer walk the array in order"
%endif
; (the kernel-only `section .text` switch is dropped: a package has no sections)
    OS88_BSS GB_BSS
    OS88_IMAGE_END

gb_win     equ os88_image_end + 0    ; word: our window
gb_runs    equ os88_image_end + 2    ; word: BENCH runs, so the row can tell a
                                     ; bench that ran from one that did not
gb_shapes  equ os88_image_end + 4    ; word: ...and shape draws, the same way
gb_gx      equ os88_image_end + 6    ; word: the window's content origin, which
gb_gy      equ os88_image_end + 8    ; word: moves and is re-read every time
