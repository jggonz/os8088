; =============================================================================
; os8088 - apps/os88line.inc
;
; A ONE-LINE TEXT FIELD - the third shared include in apps/, and the second
; carrying code. apps/os88ui.inc is the first, and its header is the argument
; for both: an include costs each package its own copy and can be revised
; freely because it is source, where a slot costs a published contract.
;
; It exists because the two things stages C and D need most is somewhere to
; TYPE AN ADDRESS: a browser's location bar and a terminal's host box are the
; same control with a different label on it, and the alternative was two
; copies of caret arithmetic that would have drifted the first time either
; grew a key. Note Pad has a far better editor and it is a whole application
; (SPEC.md 27); this is ONE LINE - no wrap, no selection, no undo - and that
; is the whole specification.
;
; --- USING IT ----------------------------------------------------------------
; `%include "os88line.inc"` AT THE END OF YOUR CODE, just before OS88_BSS -
; os88ui.inc's rule and for its reason: the header and any OS88_ICON16 block
; are at FIXED OFFSETS in the image (SPEC.md 20.2), so code emitted between
; them fails the icon macro's own assertion. It needs os88ui.inc's UI_* macros
; and so must be included AFTER it.
;
; YOU OWN THE STATE. This file declares no storage at all: every routine takes
; SI = a LINE BLOCK you declare, so a window may have two of them and a
; package may have one per window.
;
;   LN_X1,LN_Y1,LN_X2,LN_Y2   4 words: the rect, INCLUSIVE, in SCREEN
;                             coordinates - gfx_frame's convention and
;                             os88ui.inc's, so the drawn control and the
;                             clickable control read the same four words and
;                             cannot drift (SPEC.md 22's fm_hit discipline)
;   LN_BUF                    word: -> your NUL-terminated buffer, in YOUR
;                             segment
;   LN_MAX                    word: its capacity INCLUDING the NUL
;   LN_LEN                    word: characters in it now
;   LN_CAR                    word: the caret, 0..LN_LEN
;   LN_VIEW                   word: the first character shown. A URL is longer
;                             than any box that fits on a 640px screen, and a
;                             field that cannot scroll is one that silently
;                             refuses text
;   LN_FOCUS                  byte: 1 = the caret is drawn and keys land here
;   LN_DIS                    byte: non-zero = disabled - see its equ below
;
; Declare `resb OS88LINE_SZ` in bss and fill the rect plus LN_BUF/LN_MAX
; before the first draw. SI IS THE BLOCK AND NOT BX, because the gfx
; primitives take AX/BX/CX/DX as their rect and a block in BX would mean a
; shuffle at every call.
;
; --- WHAT IT DOES NOT DECIDE -------------------------------------------------
; What a key MEANS beyond editing. os88line_key answers CF=0 for `I used that
; keystroke` and CF=1 for `it is yours`, so Enter, Tab and Escape come back to
; the caller - the only place that knows whether Enter means GO, SUBMIT or
; nothing. And it repaints nothing but its own rect.
;
; REGISTERS: every routine preserves everything except its documented output.
; =============================================================================

LN_X1     equ 0
LN_Y1     equ 2
LN_X2     equ 4
LN_Y2     equ 6
LN_BUF    equ 8
LN_MAX    equ 10
LN_LEN    equ 12
LN_CAR    equ 14
LN_VIEW   equ 16
LN_FOCUS  equ 18
LN_DIS    equ 19                ; byte: non-zero = DISABLED (SPEC.md 47). It
                                ; was LN_PAD, a byte the record already
                                ; carried and nothing read, so a field that
                                ; greys costs no package a byte of bss and a
                                ; field that never greys reads 0 - the loader
                                ; zeroes a package's bss (SPEC.md 20.4 step 7),
                                ; so every existing caller is unchanged
                                ; without touching a line.
                                ;
                                ; **THE CALLER SETS THE PEN, THIS SETS THE
                                ; SHAPE** (47 rule 2, and cp_vid_btn in
                                ; kernel/ctrl.inc is the kernel's own worked
                                ; example). With it set, `os88line_draw`
                                ; leaves the frame in whatever pen you armed
                                ; with OSAPI_GFX_PEN instead of forcing black,
                                ; and draws no caret. The TEXT needs nothing
                                ; from this - [gfx_dis] is read by font_ink,
                                ; so the same pen stipples it for free - and
                                ; the interior stays WHITE, which is the
                                ; kernel button's answer too
OS88LINE_SZ equ 20

LN_INSET  equ 3                 ; MINIMUM pixels between the frame and the first
                                ; glyph. The pen is rounded UP from here to a
                                ; multiple of 8 - see os88line_pen, which is
                                ; the difference between a field that flickers
                                ; on every keystroke and one that does not
LN_CELL   equ 8                 ; the kernel's fixed cell (SPEC.md 6)

; -----------------------------------------------------------------------------
; os88line_pen - the TEXT PEN, and it is 8-ALIGNED (SPEC.md 6.1)
; in:  SI = the block. out: AX = the pen's screen x. clobbers: flags
;
; **THIS IS WHY THE FIELD STOPPED FLICKERING.** font_run's fast path - the one
; that writes each cell old-to-final in a SINGLE STORE, so a run is never
; momentarily blank - needs the pen on a multiple of 8. Anything else falls
; back to gfx_fill + font_str, which is PERFORMANCE.md Part 1's erase-and-
; letter pair: the box goes white, then the glyphs arrive.
;
; The old pen was LN_X1 + LN_INSET, and every caller's LN_X1 is a content edge
; plus its own small inset - the browser's is content+3, so the pen sat at
; content+6, and WF_SNAP (SPEC.md 11.94.1) makes the content origin 8-aligned,
; which pinned the pen at 6 mod 8 for EVERY window position on EVERY adapter.
; It could never take the fast path. Measured on a cycle-accurate 5150/CGA
; with PERFORMANCE.md Part 3.1's instrument, a 26-character URL: one keystroke
; flashed 246 transient pixels over ~18 cells and a backspace 437 over ~21.
;
; Rounding UP rather than down: down would put the pen inside the frame.
; It costs the field up to 7px of width, which os88line_cols takes off the
; column count so nothing else has to know.
;
; NOTE FOR VGA: font_run's single-store path is 1bpp-only, so mode 12h still
; takes the fill-then-letter fallback and still flashes. That is font_run's to
; fix, not this file's - but the alignment is worth having there anyway
; (SPEC.md 11.94 measures it at 12.5% on the primitive).
; -----------------------------------------------------------------------------
os88line_pen:
    mov ax, [si+LN_X1]
    add ax, LN_INSET + LN_CELL - 1
    and ax, ~(LN_CELL - 1)
    ret

; -----------------------------------------------------------------------------
; os88line_cols - how many characters the box shows
; in:  SI = the block. out: CX = columns, at least 1
; -----------------------------------------------------------------------------
os88line_cols:
    push ax
    push dx
    call os88line_pen
    mov dx, ax
    mov ax, [si+LN_X2]
    sub ax, dx                  ; ...FROM THE ALIGNED PEN, not from LN_X1: the
    sub ax, LN_INSET            ; rounding costs up to 7px and the count is
                                ; the one place that has to know
    js .one                     ; ...INTO AX FIRST: the shift below needs CL,
    mov cl, 3                   ; and an 8086 shifts by 1 or by CL and nothing
    shr ax, cl                  ; else (SPEC.md 1), so a width held in CX
    mov cx, ax                  ; would be eaten by its own shift count
    or cx, cx
    jnz .out
.one:
    mov cx, 1
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_fit - scroll the view so the caret is inside it (internal)
; in:  SI = the block; clobbers nothing
; -----------------------------------------------------------------------------
os88line_fit:
    push ax
    push cx
    push dx
    call os88line_cols
    mov ax, [si+LN_CAR]
    cmp ax, [si+LN_VIEW]
    jae .right
    mov [si+LN_VIEW], ax        ; the caret walked off the left
    jmp short .out
.right:
    mov dx, [si+LN_VIEW]
    add dx, cx
    cmp ax, dx
    jb .out
    sub ax, cx                  ; ...and off the right: show it as the LAST
    inc ax                      ; cell, which is what a one-line field does
    mov [si+LN_VIEW], ax
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_draw - the box, its text, and the caret if it has focus
; in:  SI = the block; caller holds the gfx lock
; out: nothing; all registers preserved
;
; THE TEXT CELLS ARE NEVER BLANKED (SPEC.md 6.1): one opaque OSAPI_FONT_RUN
; puts the background and the glyphs down in a single pass, and only the strip
; to the RIGHT of the text is filled. A fill-then-letter pair would leave the
; field empty between the two calls, which on a 4.77MHz machine is tens of
; milliseconds on EVERY KEYSTROKE - Note Pad's finding (SPEC.md 27.2), and
; the reason this is not simply gfx_fill plus font_str.
;
; The run needs a NUL and the visible span rarely ends at one, so the byte
; past the span is BANKED, replaced and put back. That is what saves this file
; from owning a scratch buffer, which is what saves it from owning any storage
; at all.
; -----------------------------------------------------------------------------
os88line_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88line_fit

    cmp byte [si+LN_DIS], 0     ; the frame - in the CALLER's pen when this
    jne .frame                  ; field is disabled, so the ring and the text
    mov al, CBLACK              ; inside it cannot come out one solid and one
    UI_INK                      ; stippled (SPEC.md 47 rule 2)
.frame:
    mov ax, [si+LN_X1]
    mov bx, [si+LN_Y1]
    mov cx, [si+LN_X2]
    mov dx, [si+LN_Y2]
    UI_FRAME

    call os88line_cols          ; CX = columns the box shows
    mov ax, [si+LN_LEN]
    sub ax, [si+LN_VIEW]
    jns .have
    xor ax, ax
.have:
    cmp ax, cx
    jbe .n
    mov ax, cx
.n:
    mov di, ax                  ; DI = characters to draw
    mov bx, [si+LN_BUF]
    add bx, [si+LN_VIEW]
    mov al, [bx+di]             ; bank the byte the NUL will cover...
    push ax
    mov byte [bx+di], 0
    call os88line_pen           ; ...ALIGNED (see its header): this is what
    mov cx, ax                  ; earns font_run's single-store path
    mov dx, [si+LN_Y1]
    add dx, LN_INSET
    push si
    mov si, bx                  ; ...SI is the TEXT for one call only
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN         ; ...one opaque pass: no blank, ever
    pop si
    pop ax
    mov [bx+di], al             ; ...and the banked byte back

    call os88line_pen           ; the strip past the text, which is the ONLY
    push di                     ; fill in this routine
                                ; It covers the CARET'S CELL as well, and that
                                ; was tried the other way and MEASURED AS
                                ; NOTHING: skipping the cell to stop the fill
                                ; whiting it before the caret blacks it looked
                                ; like PERFORMANCE.md's double-draw and is not
                                ; one, because the caret is a 1px BAR - its
                                ; column goes white-to-black, which is a
                                ; CHANGE, and the six columns either side are
                                ; white before, during and after. 0 transient
                                ; pixels with the skip and 0 without it, so
                                ; the skip is not here
    mov cl, 3
    shl di, cl
    add ax, di
    pop di
    mov cx, [si+LN_X2]
    dec cx
    cmp ax, cx
    ja .caret
    push ax
    mov al, CWHITE
    UI_INK
    pop ax
    mov bx, [si+LN_Y1]
    inc bx
    mov dx, [si+LN_Y2]
    dec dx
    UI_FILL
.caret:
    cmp byte [si+LN_FOCUS], 0
    je .out
    cmp byte [si+LN_DIS], 0     ; ...and a DISABLED field draws no caret, which
    jne .out                    ; is 47 rule 6 in the control rather than in
                                ; each caller's click path: a greyed box with a
                                ; bar blinking in it is offering something
    mov ax, [si+LN_CAR]
    sub ax, [si+LN_VIEW]
    mov cl, 3
    shl ax, cl
    push ax
    call os88line_pen
    mov cx, ax
    pop ax
    add ax, cx
    mov cx, ax
    mov bx, [si+LN_Y1]
    add bx, LN_INSET            ; the bar lives in the GLYPH BAND and not the
    mov dx, bx                  ; whole inner height, because the opaque
    add dx, 7                   ; font_run that repaints this cell is what
                                ; ERASES it - the strip fill above starts past
                                ; the text and reaches no caret inside it, so
                                ; a taller bar left its top and bottom rows
                                ; behind at every column the caret visited
    push ax
    mov al, CBLACK
    UI_INK
    pop ax
    UI_FILL                     ; a 1px bar: x1 = x2 = the caret's column
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_cell - where a buffer index sits on the glass (internal)
; in:  SI = the block, AX = a buffer index
; out: CF=1 = it is outside the visible span, and NOTHING may be drawn for it.
;      CF=0: AX = the cell's left column, DX = the glyph band's top row,
;      BX -> that character in the buffer. CX is clobbered either way.
;
; THE PEN IS os88line_pen's AND THE BAND IS os88line_draw's. A cell repainted
; from arithmetic of its own lands beside the one that was drawn rather than
; on it, which is os88line_click's note (SPEC.md 22's fm_hit discipline) with
; a third party in it.
;
; The carry is not defensiveness: LN_VIEW can have scrolled since the caret
; was put on the glass, and a cell repainted at a stale column is ink LEFT
; BEHIND rather than a repaint that did not happen (SPEC.md 77.45.2).
; -----------------------------------------------------------------------------
os88line_cell:
    push di
    mov bx, [si+LN_BUF]
    add bx, ax                  ; BX -> the character, before AX becomes a
                                ; screen column
    mov di, ax
    sub di, [si+LN_VIEW]
    js .no                      ; scrolled off the left...
    call os88line_cols          ; CX = the columns the box shows
    cmp di, cx
    jae .no                     ; ...or off the right
    mov dx, [si+LN_Y1]
    add dx, LN_INSET            ; the glyph band's top row: the 8 rows the bar
                                ; covers and the 8 the character occupies are
                                ; the SAME 8, which is what os88line_draw's
                                ; caret note is about
    call os88line_pen           ; AX = the aligned pen
    mov cx, di
    shl cx, 1                   ; x8, three shifts: an 8086 shifts by 1 or by
    shl cx, 1                   ; CL and by nothing else, and CL is not free
    shl cx, 1                   ; here (SPEC.md 1)
    add ax, cx
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; -----------------------------------------------------------------------------
; os88line_caron - put the caret ON the glass, and touch nothing else
; in:  SI = the block, AX = the buffer index to put it at; the gfx lock held
; out: nothing; all registers preserved
;
; The same 1px bar os88line_draw ends with, drawn from the same two routines,
; so a field that is repainted whole and one that is not cannot disagree about
; where its caret is.
; -----------------------------------------------------------------------------
os88line_caron:
    push ax
    push bx
    push cx
    push dx
    call os88line_cell
    jc .out
    mov bx, dx                  ; the bar's rows are the glyph band's
    mov dx, bx
    add dx, 7
    mov cx, ax                  ; ...and a 1px bar is x1 = x2
    push ax
    mov al, CBLACK
    UI_INK
    pop ax
    UI_FILL
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_caroff - take the caret OFF, and put back what it was covering
; in:  SI = the block, AX = the buffer index it is at; the gfx lock held
; out: nothing; all registers preserved
;
; ONE OPAQUE CELL (SPEC.md 6.1): the character under the bar is drawn again,
; ground and glyph in a single pass, so the bar is gone and the character
; never blinks on the way. A white fill of the cell followed by the character
; would be PERFORMANCE.md Part 1's erase-and-letter pair on one cell, which is
; the flicker this file's pen alignment exists to avoid.
;
; The 1px bar is drawn at the LEFT edge of its cell and the cell is 8 wide, so
; repainting the whole cell is the smallest thing that certainly covers it -
; and it is also the ONLY thing that covers it when the glyph has ink in
; column 0, which several of the kernel's do.
;
; Past the end of the text there is no character to draw, and the cell is
; simply the white os88line_draw's strip fill left there.
; -----------------------------------------------------------------------------
os88line_caroff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call os88line_cell
    jc .out
    cmp byte [bx], 0
    je .blank
    mov cl, [bx+1]              ; bank the byte the NUL will cover - the run is
    mov byte [bx+1], 0          ; one character long and rarely ends at one,
    push cx                     ; which is os88line_draw's trick for its reason
    push si
    mov si, bx                  ; ...SI is the TEXT for one call only
    mov cx, ax
    mov al, CBLACK
    mov ah, CWHITE
    UI_RUN
    pop si
    pop cx
    mov [bx+1], cl              ; ...and the banked byte back
    jmp short .out
.blank:
    mov bx, dx                  ; y1 = the band's top, y2 = 8 rows down
    add dx, 7
    mov cx, ax
    add cx, 7                   ; ...and the whole 8px cell across
    push ax
    mov al, CWHITE
    UI_INK
    pop ax
    UI_FILL
    mov al, CBLACK              ; ...AND THE PEN BACK. fd_clear_content's bug
    UI_INK                      ; is what a colour left behind looks like two
                                ; callers later (SPEC.md 77.14.1)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_edit - repaint what ONE EDIT changed, and not the rest
; in:  SI = the block, AX = LN_VIEW as it was BEFORE the key,
;      BX = LN_LEN as it was before it, DX = LN_CAR as it was before it;
;      the gfx lock held
; out: CX = text cells drawn, for a caller that wants to count them.
;      Everything else preserved.
;
; **WHY THIS EXISTS.** os88line_draw puts ONE opaque font_run over every
; visible character, which is right for a repaint and wrong for a keystroke:
; typing the 21st character into a field redraws twenty that did not change.
; PERFORMANCE.md prices a glyph cell at ~900us on the target machine, so a
; 20-character field cost ~18ms A KEYSTROKE - and none of it showed in the
; flick instrument, because redrawing the same glyph changes no pixel at all.
; It is a TIME defect, and the only way to see it is to count the cells.
;
; **IT IS DELIBERATELY NARROW, and falls back rather than getting clever.**
; Three cases are handled here and everything else goes to os88line_draw:
;
;   the view SCROLLED      -> every visible cell moved. Full draw.
;   the caret is NOT at    -> an insert shifts the whole tail right and a
;   the end                   delete shifts it left. Full draw.
;   the caret IS at the end-> an APPEND touched one cell and a BACKSPACE
;                             blanked one. That is the case worth having,
;                             because it is what typing IS.
;
; A partial repaint that got its arithmetic wrong would leave ink behind, and
; ink left behind is worse than a slow repaint - so the cheap path is taken
; only where it is obviously right, and the expensive one is still correct.
;
; The cells come from os88line_cell and the caret from os88line_caron, which
; is this file's own rule: a cell repainted from arithmetic of its own lands
; beside the one that was drawn rather than on it.
;
; **AND THE THIRD WORD IS THE CARET** (SPEC.md 83.1.1). The bar is a 1px fill
; and nothing else on the machine knows it is there, so a cell this routine
; does not repaint KEEPS it. The append is covered for free - the opaque cell
; it draws is the one the bar was standing in - and the other two were not:
; a backspace blanks the cell the CHARACTER was in, which is the one the caret
; is moving TO, so the bar it left sat one cell right and the trail grew by one
; per key; and End from the middle of a line moves the caret with the text
; unchanged, which is the one shape of caret move that reaches this path at all
; (any other lands away from the end, which is already a full redraw).
; -----------------------------------------------------------------------------
os88line_edit:
    push ax
    push bx
    push dx
    push di
    push si
    mov di, ax                  ; DI = the view as it was, BX = the length
    call os88line_fit           ; ...which may scroll to follow the caret
    xor cx, cx
    cmp di, [si+LN_VIEW]
    jne .full                   ; the whole visible span moved
    mov di, dx                  ; DI = THE CARET AS IT WAS, which is where the
                                ; bar on the glass is. The view question is
                                ; answered and DI is free, so the block needs
                                ; no storage for it - which is this file's
                                ; whole rule (SPEC.md 83.1.1)
    mov ax, [si+LN_CAR]
    cmp ax, [si+LN_LEN]
    jne .full                   ; a mid-string edit shifted the tail
    cmp bx, [si+LN_LEN]
    jb .grew
    ja .shrank
    jmp short .caret            ; neither grew nor shrank: a caret move, and
                                ; caron/caroff have done their own cells
.grew:
    mov ax, [si+LN_LEN]         ; the character just typed is the last one...
    dec ax
    call os88line_cell          ; AX = its column, DX = the band, BX -> it
    jc .caret                   ; scrolled out of sight: nothing to draw
    mov cx, ax
    push si
    mov si, bx                  ; ...and ONE opaque cell, ground and glyph in
    mov al, CBLACK              ; a single pass (SPEC.md 6.1). No NUL to bank:
    mov ah, CWHITE              ; the last character is already followed by
    call OSAPI_FONT_RUN         ; the buffer's own terminator
    pop si
    mov di, [si+LN_CAR]         ; ...AND NOTHING IS OWED AT .caret: the cell
                                ; just drawn opaquely IS the one the bar was
                                ; standing in, because a caret at the end
                                ; before the key is at LN_LEN-1 after it
    mov cx, 1
    jmp short .caret
.shrank:
    mov ax, [si+LN_LEN]         ; the cell the deleted character had...
    call os88line_cell
    jc .caret
    push ax
    push dx
    mov al, CWHITE              ; ...blanked, and only it
    UI_INK
    pop dx
    pop ax
    mov bx, dx
    mov cx, ax
    add cx, LN_CELL - 1
    add dx, LN_CELL - 1
    UI_FILL
    mov al, CBLACK              ; AND THE PEN BACK: a colour left behind is
    UI_INK                      ; what shows up two callers later
    xor cx, cx
.caret:
    cmp byte [si+LN_FOCUS], 0
    je .out
    push cx
    cmp di, [si+LN_CAR]
    je .cnew                    ; the bar has not moved: it is already right
    mov ax, di                  ; **THE BAR THE CARET LEFT**, and os88line_
    call os88line_caroff        ; caroff reads the buffer as it is NOW, which
                                ; is right rather than lucky: that cell either
                                ; holds a character this edit did not touch, or
                                ; is past the end of a text that just got
                                ; shorter - where its .blank arm is the 8px
                                ; white fill a rubbed-out cell wants
.cnew:
    mov ax, [si+LN_CAR]
    call os88line_caron
    pop cx
.out:
    pop si
    pop di
    pop dx
    pop bx
    pop ax
    ret
.full:
    call os88line_draw
    mov cx, [si+LN_LEN]         ; ...and say what it cost, for a caller that
    sub cx, [si+LN_VIEW]        ; counts
    jns .fout
    xor cx, cx
.fout:
    pop si
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_key - one keystroke
; in:  SI = the block, AL = ascii, AH = scan code
; out: CF=0 the field used it (redraw the field); CF=1 it is the caller's -
;      Enter, Tab, Escape and everything else this does not edit with
; clobbers: flags only
;
; It edits and nothing more, which is what lets one control serve a location
; bar (Enter = go) and a host box (Enter = connect) without knowing either.
; -----------------------------------------------------------------------------
os88line_key:
    cmp byte [si+LN_FOCUS], 0
    je .not                     ; unfocused fields take no keys at all
    cmp al, 8
    je .bs
    cmp al, 0x20
    jb .ctrl
    cmp al, 0x7E
    ja .not
    jmp short os88line_ins
.ctrl:
    or al, al
    jnz .not                    ; a control character that is not a scan-code
    cmp ah, 0x4B                ; extended
    je os88line_left
    cmp ah, 0x4D
    je os88line_right
    cmp ah, 0x47
    je os88line_home
    cmp ah, 0x4F
    je os88line_end
    cmp ah, 0x53
    je os88line_del
.not:
    stc
    ret
.bs:
    cmp word [si+LN_CAR], 0
    je .used                    ; USED, not passed on: a backspace at the
                                ; start of a field is a keystroke the field
                                ; owns and does nothing with, and handing it
                                ; back would let a caller act on it twice
    dec word [si+LN_CAR]
    jmp short os88line_del
.used:
    clc
    ret

; --- os88line_ins - AL into the buffer at the caret --------------------------
os88line_ins:
    push ax
    push bx
    push cx
    push di
    mov cx, [si+LN_LEN]
    inc cx
    cmp cx, [si+LN_MAX]
    jae .full                   ; ...INCLUDING the NUL: LN_MAX is a capacity
    mov bx, [si+LN_BUF]
    mov di, [si+LN_LEN]         ; shift the tail up one, from the END so an
.shift:                         ; overlapping move cannot eat itself
    cmp di, [si+LN_CAR]
    jbe .put
    mov ah, [bx+di-1]           ; AH is the scratch, not CH: CX is the length
    mov [bx+di], ah             ; test above and AL is the character to insert
    dec di
    jmp short .shift
.put:
    mov di, [si+LN_CAR]
    mov [bx+di], al
    inc word [si+LN_LEN]
    inc word [si+LN_CAR]
    mov di, [si+LN_LEN]
    mov byte [bx+di], 0
.full:
    pop di
    pop cx
    pop bx
    pop ax
    clc                         ; a full field still CONSUMED the key
    ret

; --- os88line_del - remove the character AT the caret ------------------------
os88line_del:
    push ax
    push bx
    push di
    mov ax, [si+LN_CAR]
    cmp ax, [si+LN_LEN]
    jae .out
    mov bx, [si+LN_BUF]
    mov di, ax
.shift:
    mov al, [bx+di+1]
    mov [bx+di], al
    inc di
    cmp di, [si+LN_LEN]
    jb .shift
    dec word [si+LN_LEN]
.out:
    pop di
    pop bx
    pop ax
    clc
    ret

os88line_left:
    cmp word [si+LN_CAR], 0
    je .out
    dec word [si+LN_CAR]
.out:
    clc
    ret

os88line_right:
    push ax
    mov ax, [si+LN_CAR]
    cmp ax, [si+LN_LEN]
    jae .out
    inc word [si+LN_CAR]
.out:
    pop ax
    clc
    ret

os88line_home:
    mov word [si+LN_CAR], 0
    clc
    ret

os88line_end:
    push ax
    mov ax, [si+LN_LEN]
    mov [si+LN_CAR], ax
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; os88line_hit - is the point CX,DX inside this field? NOTHING IS MOVED
; in:  SI = the block, CX = x, DX = y (screen)
; out: CF=0 = inside, CF=1 = outside; every register preserved, flags aside
;
; os88line_click's first half, published on its own because a caller that
; repaints only the CARET has to know which field is about to take the press
; before the press lands: once os88line_click has answered, the column the old
; bar was at is gone and the two cells that changed cannot be named any more
; (SPEC.md 77.45.1).
; -----------------------------------------------------------------------------
os88line_hit:
    cmp cx, [si+LN_X1]
    jb .no
    cmp cx, [si+LN_X2]
    ja .no
    cmp dx, [si+LN_Y1]
    jb .no
    cmp dx, [si+LN_Y2]
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; os88line_click - CX = x, DX = y, SI = the block
; out: CF=0 it was inside: FOCUS IS SET and the caret placed at the column
;      clicked; CF=1 it was outside and the block is untouched
;
; THE CALLER DECIDES WHAT AN OUTSIDE CLICK MEANS. Clearing LN_FOCUS here would
; be a control deciding another control's business, and a window with two
; fields would then need this routine not to do it.
; -----------------------------------------------------------------------------
os88line_click:
    push ax
    call os88line_hit           ; ...the SAME test, so a caller that asks first
    jc .no                      ; and a caller that does not cannot disagree
    push dx                     ; ...FROM THE SAME PEN THE PAINTER USED, or
    call os88line_pen           ; the drawn caret and the clicked one part
    mov dx, ax                  ; company by up to 7px (SPEC.md 22's fm_hit
    mov ax, cx                  ; discipline). DX is the click's y and is
    sub ax, dx                  ; banked because os88line_pen answers in AX,
    pop dx                      ; which is where the click's x has to go
    jns .col
    xor ax, ax
.col:
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    add ax, [si+LN_VIEW]
    cmp ax, [si+LN_LEN]
    jbe .set
    mov ax, [si+LN_LEN]         ; past the text is the END of it, which is
.set:                           ; what every one-line field in every system
    mov [si+LN_CAR], ax         ; does and is what a user expects
    mov byte [si+LN_FOCUS], 1
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; os88line_resync - the BUFFER was written behind the field's back; re-derive
;                   LN_LEN, LN_CAR and LN_VIEW from what is in it now
; in:  SI = the block. LN_BUF already holds a NUL string somebody else wrote
; out: nothing; all registers preserved
;
; **THE CASE os88line_set CANNOT SERVE**, and mistaking one for the other is
; a silent corruption rather than a wrong picture: `set` copies from DS:DI
; INTO LN_BUF, so a caller whose data is ALREADY in LN_BUF has nothing to put
; in DI and copies whatever it held - into the very buffer it meant to keep.
; The DOS box hit exactly that (SPEC.md 96.21.1): a shortcut writes the
; arguments straight into `dos_args`, which IS the field's buffer, and the
; reload then overwrote them from a stale DI, so a link with arguments ran
; with none.
;
; The length is measured rather than trusted: LN_MAX bounds the walk, so a
; buffer somebody filled without a terminator ends the field at the cap
; instead of running off it.
; -----------------------------------------------------------------------------
os88line_resync:
    push ax
    push bx
    push cx
    mov bx, [si+LN_BUF]
    mov cx, [si+LN_MAX]
    dec cx                      ; ...the most a NUL string in there can be
    xor ax, ax
.walk:
    cmp ax, cx
    jae .done
    cmp byte [bx], 0
    je .done
    inc bx
    inc ax
    jmp short .walk
.done:
    mov byte [bx], 0            ; a buffer filled with no terminator gets one
    mov [si+LN_LEN], ax
    mov [si+LN_CAR], ax         ; the caret lands at the END, as `set` leaves it
    mov word [si+LN_VIEW], 0
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88line_set - replace the contents with the NUL string at DS:DI
; in:  SI = the block, DI = the string. The caret lands at the END.
; out: nothing; all registers preserved
;
; TRUNCATES AT LN_MAX rather than refusing: the callers are a location bar
; being handed a link and a host box being handed a default, and neither has
; anywhere to report a refusal to.
; -----------------------------------------------------------------------------
os88line_set:
    push ax
    push bx
    push cx
    push di
    push dx
    mov bx, [si+LN_BUF]
    mov cx, [si+LN_MAX]
    dec cx                      ; ...leaving room for the NUL
    xor ax, ax
.copy:
    cmp ax, cx
    jae .done
    mov dl, [di]                ; DL and not AH: AX is the COUNT, and `inc ax`
    or dl, dl                   ; with a character in the high half carries
    jz .done                    ; into it the one time the count wraps a byte
    mov [bx], dl
    inc bx
    inc di
    inc ax
    jmp short .copy
.done:
    pop dx
    mov byte [bx], 0
    mov [si+LN_LEN], ax
    mov [si+LN_CAR], ax
    mov word [si+LN_VIEW], 0
    pop di
    pop cx
    pop bx
    pop ax
    ret
