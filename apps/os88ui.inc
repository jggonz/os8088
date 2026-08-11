; =============================================================================
; os8088 - apps/os88ui.inc
;
; THE STANDARD CONTROLS, as source. Optional: `%include "os88ui.inc"` only if
; you want them. It emits near procs and a word of data into YOUR image - it
; is not an API slot and costs the kernel nothing at all.
;
; PUT THE INCLUDE AT THE END OF YOUR CODE, just before OS88_BSS. Not up beside
; os88api.inc: the header, and an OS88_ICON16 block if you have one, are at
; FIXED OFFSETS in the image (SPEC.md 20.2), and code emitted between them
; fails the icon macro's own offset assertion. Anywhere after the icon block
; works; the end is where a library belongs anyway.
;
; os88api.inc stays code-free on purpose: every package includes it
; unconditionally, so a byte there is a byte on every package. This is the
; second shared include in apps/ and the first carrying code.
;
; WHAT IT IS FOR. Two things, and the second is the one that needed a kernel
; change to be possible at all:
;
;   1. ONE button. There were about ten hand-rolled ones and SPEC.md 47's
;      greying rule had been fixed FIVE separate times, each as its own bug -
;      always CDGRAY without [gfx_dis], which is a real grey on VGA and SOLID
;      BLACK on both 1bpp adapters, so the greyed caption came out
;      pixel-identical to a live one on the machine this OS is for. Here the
;      pen is taken through OSAPI_GFX_PEN once, in one place, and rule 1 stops
;      being something to remember.
;   2. The ARM. SPEC.md 13.7 gives a package W_ONMOUSEUP; that is a callback,
;      not a button - the package still has to record what the press landed
;      on, re-test at the release and clear on every path that does not fire.
;      os88ui_arm / os88ui_fire is that, in nine instructions, so it is
;      written once instead of in every package that wants a control which
;      cancels when you slide off it.
;
; WHAT IT IS NOT FOR. A skinned control. ModPlug's bevelled well and LED
; transport are a deliberate port of ModPlugPlayer's look (SPEC.md 56) and
; Minesweeper's cell is its own game's chrome - converting those would be
; undoing intended design, not consolidating it. This is the System-1
; standard button and nothing else.
;
; GEOMETRY IS A POINTER, and that is the point. Every routine here takes
; BX = a 4-word rect {x1, y1, x2, y2}, INCLUSIVE, in screen coordinates -
; gfx_frame's own convention - so the drawn control and the clickable control
; read the same four words and cannot drift. That is fm_hit's discipline
; (SPEC.md 22) applied to a package.
;
; REGISTERS: every routine preserves everything except its documented output.
; =============================================================================

OS88UI_DIS  equ 1               ; disabled: dithered frame AND label (47 r2)
OS88UI_DEF  equ 2               ; the default button's outer ring

; -----------------------------------------------------------------------------
; os88ui_btn - draw one standard button
; in:  BX = rect {x1,y1,x2,y2} inclusive, SI = NUL label, DI = OS88UI_* flags
;      caller holds the gfx lock (every callback does)
; out: nothing; all registers preserved
;
; Frame, then the label centred in both axes. NO FILL: the content behind a
; button is the window's own background, which W_PAINT has already laid down -
; filling here would be PERFORMANCE.md Part 1's erase-and-letter pair, drawn
; twice and briefly blank, for a rectangle that was already the right colour.
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
    mov bp, bx                  ; BP = the rect. SS != DS in a package
                                ; (SPEC.md 20.1), so every read below needs a
                                ; ds: override or it addresses the task stack
    test di, OS88UI_DIS
    jz .live
    stc                         ; CF = 1: CDGRAY *and* [gfx_dis] together,
    jmp short .pen              ; which is SPEC.md 47 rule 1 in one call
.live:
    clc
.pen:
    call OSAPI_GFX_PEN
    mov ax, [ds:bp+0]
    mov bx, [ds:bp+2]
    mov cx, [ds:bp+4]
    mov dx, [ds:bp+6]
    call OSAPI_GFX_FRAME
    test di, OS88UI_DEF
    jz .label
    mov ax, [ds:bp+0]           ; the default ring, 3px outside the frame
    sub ax, 3
    mov bx, [ds:bp+2]
    sub bx, 3
    mov cx, [ds:bp+4]
    add cx, 3
    mov dx, [ds:bp+6]
    add dx, 3
    call OSAPI_GFX_FRAME
.label:
    call OSAPI_FONT_WIDTH       ; AX = the label's width in px; SI intact
    mov cx, [ds:bp+4]
    sub cx, [ds:bp+0]
    inc cx                      ; CX = the button's width
    sub cx, ax
    shr cx, 1
    add cx, [ds:bp+0]           ; ...centred
    mov dx, [ds:bp+6]
    sub dx, [ds:bp+2]
    inc dx                      ; DX = the height
    sub dx, 8                   ; a glyph cell is 8 rows
    shr dx, 1
    add dx, [ds:bp+2]
    call OSAPI_FONT_STR
    clc
    call OSAPI_GFX_PEN          ; ...and the pen back, always
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
