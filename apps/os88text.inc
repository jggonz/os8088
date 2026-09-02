; =============================================================================
; os8088 - apps/os88text.inc
;
; A MULTI-LINE TEXT BOX - the sibling of apps/os88line.inc, and its header is
; the argument for both: an include costs each package its own copy and can be
; revised freely because it is source, where a slot costs a published contract.
;
; os88line.inc is ONE LINE and says so - "no wrap, no selection, no undo, and
; that is the whole specification". This is the same control with a document
; instead of a string: several lines, a caret that moves between them, and a
; vertical scroll. It is deliberately NOT Note Pad (SPEC.md 27), which is a
; whole application with word wrap, a layout worker and a find dialog; the
; thing this is for is a cell note, a comment field, an address block - a
; paragraph or two the user types into a dialog and is done with.
;
; --- USING IT ----------------------------------------------------------------
; `%include "os88text.inc"` AT THE END OF YOUR CODE, just before OS88_BSS -
; os88ui.inc's rule and for its reason: the header and any OS88_ICON16 block
; are at FIXED OFFSETS in the image (SPEC.md 20.2), so code emitted between
; them fails the icon macro's own assertion. It needs os88ui.inc's UI_* macros
; and so must be included AFTER it.
;
; YOU OWN THE STATE. This file declares no storage at all: every routine takes
; SI = a TEXT BLOCK you declare, so a window may have two of them and a
; package may have one per window.
;
;   TX_X1,TX_Y1,TX_X2,TX_Y2   4 words: the rect, INCLUSIVE, in SCREEN
;                             coordinates - gfx_frame's convention and
;                             os88ui.inc's, so the drawn control and the
;                             clickable control read the same four words and
;                             cannot drift
;   TX_BUF                    word: -> your NUL-terminated buffer, in YOUR
;                             segment. Lines are separated by a single LF
;                             (0x0A); a CR (0x0D) is tolerated on input and
;                             never written
;   TX_MAX                    word: its capacity INCLUDING the NUL
;   TX_LEN                    word: bytes in it now
;   TX_CAR                    word: the caret, 0..TX_LEN, a BYTE OFFSET - the
;                             same shape texpad's own tp_cur uses, because a
;                             (line,col) pair has to be re-derived after every
;                             edit anyway and an offset never goes stale
;   TX_TOP                    word: the first VISIBLE line
;   TX_FOCUS                  byte: 1 = the caret is drawn and keys land here
;
; Declare `resb OS88TEXT_SZ` in bss and fill the rect plus TX_BUF/TX_MAX
; before the first draw. SI IS THE BLOCK AND NOT BX, for os88line.inc's
; reason: the gfx primitives take AX/BX/CX/DX as their rect and a block in BX
; would mean a shuffle at every call.
;
; --- WHAT IT DOES NOT DO -----------------------------------------------------
; NO WORD WRAP. A line longer than the box is CLIPPED at the right edge, not
; broken - texpad's own note (SPEC.md 57669) is that wrapping turns a
; horizontal move into a whole-pane repaint on every keystroke, and this
; control is for short paragraphs where the caller can size the box. There is
; no horizontal scroll for the same reason.
;
; NO SELECTION, NO UNDO. os88line.inc's boundary exactly.
;
; What a key MEANS beyond editing is not decided here: os88text_key answers
; CF=0 for "I used that keystroke" and CF=1 for "it is yours", so Escape, Tab
; and anything else the caller wants come back. Enter is CONSUMED - in a
; multi-line box it is a newline, which is the one place this differs from
; os88line.inc, where Enter is the caller's submit.
;
; REGISTERS: every routine preserves everything except its documented output.
; =============================================================================

TX_X1     equ 0
TX_Y1     equ 2
TX_X2     equ 4
TX_Y2     equ 6
TX_BUF    equ 8
TX_MAX    equ 10
TX_LEN    equ 12
TX_CAR    equ 14
TX_TOP    equ 16
TX_FOCUS  equ 18
TX_PAD    equ 19
OS88TEXT_SZ equ 20

TX_INSET  equ 3                 ; MINIMUM pixels between the frame and the
                                ; first glyph. The pen is rounded UP from here
                                ; to a multiple of 8, for os88line_pen's
                                ; measured reason: font_run's single-store
                                ; fast path needs an 8-aligned pen, and
                                ; without it every keystroke repaints the row
                                ; through the erase-then-letter pair
TX_CELL   equ 8                 ; the kernel's fixed cell (SPEC.md 6)

; -----------------------------------------------------------------------------
; os88text_pen - the TEXT PEN, 8-ALIGNED. in: SI = the block.
; out: AX = the pen's screen x. clobbers flags.
; -----------------------------------------------------------------------------
os88text_pen:
    mov ax, [si + TX_X1]
    add ax, TX_INSET + 7
    and ax, ~7
    ret

; -----------------------------------------------------------------------------
; os88text_cols - how many characters fit across. in: SI. out: CX (>= 1).
; -----------------------------------------------------------------------------
os88text_cols:
    push ax
    push dx
    call os88text_pen
    mov cx, [si + TX_X2]
    sub cx, ax
    sub cx, TX_INSET - 1
    jns .ok
    xor cx, cx
.ok:
    mov ax, cx
    xor dx, dx
    mov cx, TX_CELL
    div cx
    mov cx, ax
    or cx, cx
    jnz .out
    mov cx, 1
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_rows - how many lines fit down. in: SI. out: CX (>= 1).
; -----------------------------------------------------------------------------
os88text_rows:
    push ax
    push dx
    mov cx, [si + TX_Y2]
    sub cx, [si + TX_Y1]
    sub cx, (TX_INSET - 1) * 2
    jns .ok
    xor cx, cx
.ok:
    mov ax, cx
    xor dx, dx
    mov cx, TX_CELL
    div cx
    mov cx, ax
    or cx, cx
    jnz .out
    mov cx, 1
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_lstart - the byte offset line AX starts at.
; in:  SI = the block, AX = the line number (0-based)
; out: DI = the offset; CF=1 if that line does not exist (DI = TX_LEN then).
;      Every other register preserved.
;
; A SCAN, not an index. The alternative is a line table, which then has to be
; rebuilt or patched by every insert and delete - texpad does the same scan
; for the same reason, and a note is a paragraph, not a novel.
; -----------------------------------------------------------------------------
os88text_lstart:
    push ax
    push bx
    push cx
    mov di, [si + TX_BUF]
    mov cx, ax                         ; cx = lines still to skip
    xor bx, bx                         ; bx = offset into the buffer
.scan:
    or cx, cx
    jz .found
    cmp bx, [si + TX_LEN]
    jae .past
    mov al, [di + bx]
    inc bx
    cmp al, 0x0A
    jne .scan
    dec cx
    jmp .scan
.found:
    add di, bx
    pop cx
    pop bx
    pop ax
    clc
    ret
.past:
    add di, bx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; os88text_llen - how long is the line starting at DI?
; in:  SI = the block, DI = a line's start offset
; out: CX = its length in bytes, excluding the LF. Everything else preserved.
; -----------------------------------------------------------------------------
os88text_llen:
    push ax
    push bx
    mov bx, di
    xor cx, cx
.scan:
    mov ax, [si + TX_BUF]
    add ax, [si + TX_LEN]
    cmp bx, ax
    jae .out
    push si
    mov si, bx
    mov al, [si]
    pop si
    cmp al, 0x0A
    je .out
    inc bx
    inc cx
    jmp .scan
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_carlc - the caret's line and column.
; in:  SI = the block. out: AX = line, BX = column. Everything else preserved.
; -----------------------------------------------------------------------------
os88text_carlc:
    push cx
    push dx
    push di
    xor ax, ax                         ; ax = line
    xor bx, bx                         ; bx = column
    mov di, [si + TX_BUF]
    xor cx, cx                         ; cx = offset
.scan:
    cmp cx, [si + TX_CAR]
    jae .out
    push si
    mov si, di
    add si, cx
    mov dl, [si]                       ; DL, NOT CH: CX is the live offset and
    pop si                             ; loading its high half would corrupt
    inc cx                             ; the count past 255 bytes
    cmp dl, 0x0A
    jne .col
    inc ax
    xor bx, bx
    jmp .scan
.col:
    inc bx
    jmp .scan
.out:
    pop di
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; os88text_show - scroll TX_TOP the least amount that puts the caret in view.
; in: SI. Preserves everything.
; -----------------------------------------------------------------------------
os88text_show:
    push ax
    push bx
    push cx
    call os88text_carlc                ; ax = the caret's line
    cmp ax, [si + TX_TOP]
    jae .below
    mov [si + TX_TOP], ax
    jmp .out
.below:
    call os88text_rows                 ; cx = visible rows
    mov bx, [si + TX_TOP]
    add bx, cx
    cmp ax, bx
    jb .out
    sub ax, cx
    inc ax
    mov [si + TX_TOP], ax
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_draw - the box, its visible lines, and the caret if it has focus.
; in: SI = the block; caller holds the gfx lock. All registers preserved.
;
; ONE OPAQUE FONT_RUN PER ROW, like os88line_draw and texpad: a fill-then-
; letter pair leaves the box empty between the two calls, which on a 4.77MHz
; machine is tens of milliseconds on every keystroke. The byte past the
; visible span is BANKED, NUL'd and put back, which is what saves this file
; from owning a scratch buffer - and therefore from owning any storage at all.
; -----------------------------------------------------------------------------
os88text_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    UI_BLACK
    mov ax, [si + TX_X1]               ; the frame
    mov bx, [si + TX_Y1]
    mov cx, [si + TX_X2]
    mov dx, [si + TX_Y2]
    UI_FRAME

    mov ax, [si + TX_X1]               ; a degenerate rect draws its frame
    inc ax                             ; and nothing else
    mov cx, [si + TX_X2]
    dec cx
    cmp ax, cx
    jg .nolines
    mov bx, [si + TX_Y1]
    inc bx
    mov dx, [si + TX_Y2]
    dec dx
    cmp bx, dx
    jg .nolines

    ; The interior is NOT cleared whole: that is the fill-then-letter pair
    ; the header forbids, at box scale - the box sat blank while the runs
    ; re-covered it, a visible flash on every keystroke of a note. Instead
    ; each span is owned by exactly one draw: the LEFT MARGIN here (the pen
    ; is rounded up to a multiple of 8, so 3..10px belong to no run), the
    ; strip above the first row's band, each row as one opaque run plus the
    ; strip past its text, and one fill below the last line.
    UI_WHITE
    call os88text_pen
    mov cx, ax
    dec cx                             ; the margin's right edge
    mov ax, [si + TX_X1]
    inc ax
    cmp ax, cx
    jg .nomargin
    UI_FILL                            ; bx/dx: the full inner height, still
.nomargin:
    call os88text_pen                  ; the strip above the first band
    mov cx, [si + TX_X2]
    dec cx
    cmp ax, cx
    jg .notop
    mov bx, [si + TX_Y1]
    inc bx
    mov dx, [si + TX_Y1]
    add dx, TX_INSET - 1
    cmp bx, dx
    jg .notop
    UI_FILL
.notop:

    call os88text_rows
    mov bp, cx                         ; bp = rows still to draw
    mov ax, [si + TX_TOP]
    call os88text_lstart               ; di = the top line's offset
    mov dx, [si + TX_Y1]
    add dx, TX_INSET
.row:
    or bp, bp
    jz .below
    mov ax, [si + TX_BUF]
    add ax, [si + TX_LEN]
    cmp di, ax
    ja .below                          ; past the end of the document
    call os88text_llen                 ; cx = this line's length
    push cx
    call os88text_cols                 ; cx = how many fit
    mov bx, cx
    pop cx
    cmp cx, bx
    jbe .fits
    mov cx, bx                         ; CLIP: no wrap, no h-scroll
.fits:                                 ; cx = chars to draw, di = the line's
    push bp                            ; start, dx = the pen's y, bp = rows left
    push dx
    push di
    mov bx, di
    add bx, cx                         ; bx = the byte just past the visible
    mov al, [bx]                       ; span - BANKED and NUL'd so the run
    mov byte [bx], 0                   ; stops there, which is what saves this
    push bx                            ; file from owning a scratch buffer
    push ax
    call os88text_pen
    mov cx, ax                         ; cx = the pen's x
    push si
    mov si, di                         ; ...SI is the TEXT for one call only
    mov al, CBLACK                     ; AL = ink, AH = paper: ONE opaque run,
    mov ah, CWHITE                     ; never a blank-then-letter pair
    call OSAPI_FONT_RUN
    pop si
    pop ax
    pop bx
    mov [bx], al                       ; ...and the banked byte back
    mov ax, bx                         ; the strip past this row's text: the
    sub ax, di                         ; byte past the span, minus the line's
    shl ax, 1                          ; start = the characters just drawn...
    shl ax, 1
    shl ax, 1                          ; ...times 8 (8086 shifts by 1 or CL)
    add ax, cx                         ; + the pen = the strip's left edge
    mov cx, [si + TX_X2]
    dec cx                             ; ...and its right
    cmp ax, cx
    jg .nostrip                        ; the text reaches the frame
    UI_WHITE
    mov bx, dx                         ; this row's own 8px band and nothing
    add dx, TX_CELL - 1                ; more - the rows below are the tail
    UI_FILL                            ; fill's, after the loop
.nostrip:
    pop di
    pop dx
    pop bp

    call os88text_llen                 ; cx = the line's FULL length
    add di, cx                         ; step past its text...
    mov ax, [si + TX_BUF]
    add ax, [si + TX_LEN]
    cmp di, ax
    jae .lastrow                       ; no LF follows: that was the last line,
    inc di                             ; and stopping here is what keeps a
    add dx, TX_CELL                    ; document with no trailing newline from
    dec bp                             ; drawing blank rows forever
    jmp .row
.lastrow:
    add dx, TX_CELL                    ; the tail fill starts under it
.below:
    ; everything below the last text row, in ONE fill - the whole-interior
    ; clear's only remaining job
    call os88text_pen
    mov cx, [si + TX_X2]
    dec cx
    cmp ax, cx
    jg .caret
    mov bx, dx
    mov dx, [si + TX_Y2]
    dec dx
    cmp bx, dx
    jg .caret
    UI_WHITE
    UI_FILL
.caret:
    cmp byte [si + TX_FOCUS], 0
    je .nolines
    call os88text_carlc                ; ax = line, bx = column
    sub ax, [si + TX_TOP]
    js .nolines
    push ax
    push bx
    call os88text_rows
    pop bx
    pop ax
    cmp ax, cx
    jae .nolines                       ; scrolled off the bottom
    push ax
    push bx
    call os88text_cols
    pop bx
    pop ax
    cmp bx, cx
    jae .nolines                       ; off the CLIPPED right edge - this box
                                       ; has no h-scroll, so the caret can be
                                       ; somewhere it cannot be drawn
    push ax
    push bx
    call os88text_pen                  ; ax = the pen's x
    pop bx
    mov cx, ax
    mov ax, bx
    shl ax, 1
    shl ax, 1
    shl ax, 1                          ; column * 8 (8086 shifts by 1 or CL)
    add cx, ax                         ; cx = the caret's x
    pop ax                             ; ax = its VISIBLE row
    mov bx, TX_CELL
    mul bx                             ; (clobbers DX, which is set below)
    add ax, [si + TX_Y1]
    add ax, TX_INSET
    mov bx, ax                         ; bx = y1
    mov dx, ax
    add dx, TX_CELL - 1                ; dx = y2
    mov ax, cx                         ; A 1px BAR: x1 = x2 = the caret's
    UI_BLACK                           ; column, os88line_draw's own shape
    UI_FILL
.nolines:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_ins - insert AL at the caret. in: SI. out: CF=1 = no room.
; -----------------------------------------------------------------------------
os88text_ins:
    push ax
    push bx
    push cx
    push di
    mov bx, [si + TX_LEN]
    inc bx
    cmp bx, [si + TX_MAX]
    jae .full                          ; TX_MAX counts the NUL
    mov di, [si + TX_BUF]
    add di, [si + TX_LEN]              ; di = the NUL
    mov cx, [si + TX_LEN]
    sub cx, [si + TX_CAR]              ; cx = bytes to shift right
.shift:
    or cx, cx
    jz .place
    mov bl, [di - 1]
    mov [di], bl
    dec di
    dec cx
    jmp .shift
.place:
    mov [di], al
    inc word [si + TX_LEN]
    inc word [si + TX_CAR]
    mov di, [si + TX_BUF]
    add di, [si + TX_LEN]
    mov byte [di], 0
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret
.full:
    pop di
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; os88text_del - delete the byte AT the caret (Delete's meaning). in: SI.
; -----------------------------------------------------------------------------
os88text_del:
    push ax
    push cx
    push di
    mov ax, [si + TX_CAR]
    cmp ax, [si + TX_LEN]
    jae .out                           ; nothing to the right
    mov di, [si + TX_BUF]
    add di, ax
    mov cx, [si + TX_LEN]
    sub cx, ax
.shift:
    or cx, cx
    jz .done
    mov al, [di + 1]
    mov [di], al
    inc di
    dec cx
    jmp .shift
.done:
    dec word [si + TX_LEN]
.out:
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; os88text_key - one keystroke.
; in:  SI = the block, AL = ascii, AH = scan code
; out: CF=0 the box used it (redraw the box); CF=1 it is the caller's.
;      clobbers flags only.
; -----------------------------------------------------------------------------
os88text_key:
    cmp byte [si + TX_FOCUS], 0
    je .theirs
    cmp al, 0x0D                       ; Enter: a NEWLINE here, unlike
    je .newline                        ; os88line.inc where it is the
    cmp al, 0x0A                       ; caller's submit
    je .newline
    cmp al, 8
    je .backspace
    cmp al, 0x20
    jb .scancodes
    cmp al, 0x7E
    ja .theirs
    call os88text_ins
    call os88text_show
    clc
    ret
.newline:
    push ax
    mov al, 0x0A
    call os88text_ins
    pop ax
    call os88text_show
    clc
    ret
.backspace:
    cmp word [si + TX_CAR], 0
    je .used
    dec word [si + TX_CAR]
    call os88text_del
    call os88text_show
    clc
    ret
.scancodes:
    cmp ah, 0x4B                       ; Left
    je .left
    cmp ah, 0x4D                       ; Right
    je .right
    cmp ah, 0x48                       ; Up
    je .up
    cmp ah, 0x50                       ; Down
    je .down
    cmp ah, 0x47                       ; Home
    je .home
    cmp ah, 0x4F                       ; End
    je .end
    cmp ah, 0x53                       ; Delete
    je .delete
.theirs:
    stc
    ret
.left:
    cmp word [si + TX_CAR], 0
    je .used
    dec word [si + TX_CAR]
    jmp .used
.right:
    push ax
    mov ax, [si + TX_CAR]
    cmp ax, [si + TX_LEN]
    pop ax
    jae .used
    inc word [si + TX_CAR]
    jmp .used
.delete:
    call os88text_del
    jmp .used
.home:
    call os88text_tohome
    jmp .used
.end:
    call os88text_toend
    jmp .used
.up:
    call os88text_updown_up
    jmp .used
.down:
    call os88text_updown_dn
.used:
    call os88text_show
    clc
    ret

; Home/End within the caret's own line.
os88text_tohome:
    push ax
    push bx
    push di
    call os88text_carlc                ; ax = line, bx = col
    sub [si + TX_CAR], bx
    pop di
    pop bx
    pop ax
    ret

os88text_toend:
    push ax
    push bx
    push cx
    push di
    call os88text_carlc                ; ax = line, bx = col
    call os88text_lstart               ; di = that line's start
    call os88text_llen                 ; cx = its length
    sub cx, bx                         ; how far the caret is from the end
    add [si + TX_CAR], cx
    pop di
    pop cx
    pop bx
    pop ax
    ret

; Up/Down keep the column where they can - the ordinary behaviour of every
; editor, and the reason the caret is stored as an offset: the target line's
; length decides where the column actually lands.
os88text_updown_up:
    push ax
    push bx
    push cx
    push di
    call os88text_carlc
    or ax, ax
    jz .out                            ; already on the first line
    dec ax
    call os88text_vmove
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

os88text_updown_dn:
    push ax
    push bx
    push cx
    push di
    call os88text_carlc
    inc ax
    push ax
    call os88text_lstart
    pop ax
    jc .out                            ; no such line
    call os88text_vmove
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; in: AX = the target line, BX = the column to aim for
os88text_vmove:
    push cx
    push di
    call os88text_lstart               ; di = the target line's start
    call os88text_llen                 ; cx = its length
    cmp bx, cx
    jbe .col
    mov bx, cx                         ; the line is shorter: land at its end
.col:
    sub di, [si + TX_BUF]
    add di, bx
    mov [si + TX_CAR], di
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; os88text_click - CX = x, DX = y, SI = the block.
; out: CF=0 it was inside: FOCUS IS SET and the caret placed where clicked;
;      CF=1 it was outside and the block is untouched.
;
; THE CALLER DECIDES WHAT AN OUTSIDE CLICK MEANS - os88line.inc's rule, and
; for its reason: clearing focus here would be one control deciding another
; control's business.
; -----------------------------------------------------------------------------
os88text_click:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp cx, [si + TX_X1]
    jb .outside
    cmp cx, [si + TX_X2]
    ja .outside
    cmp dx, [si + TX_Y1]
    jb .outside
    cmp dx, [si + TX_Y2]
    ja .outside
    mov byte [si + TX_FOCUS], 1
    ; which row?
    sub dx, [si + TX_Y1]
    sub dx, TX_INSET
    jns .rowok
    xor dx, dx
.rowok:
    mov ax, dx
    xor dx, dx
    push cx
    mov cx, TX_CELL
    div cx
    pop cx
    add ax, [si + TX_TOP]              ; ax = the document line clicked
    ; which column?
    push ax
    call os88text_pen
    sub cx, ax
    jns .colok
    xor cx, cx
.colok:
    mov ax, cx
    xor dx, dx
    mov cx, TX_CELL
    div cx
    mov bx, ax                         ; bx = the column clicked
    pop ax
    call os88text_vmove                ; finds the line ITSELF and clamps the
                                       ; column to it, so a click past the end
                                       ; of a short line lands at its end
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.outside:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; os88text_set - replace the contents with the NUL string at DS:DI.
; in:  SI = the block, DI = the string. The caret lands at the START, which is
;      where a just-loaded note should be read from.
; out: nothing; all registers preserved. TRUNCATES at TX_MAX rather than
;      refusing, os88line_set's policy and for its reason.
; -----------------------------------------------------------------------------
os88text_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [si + TX_BUF]
    xor cx, cx
    mov dx, [si + TX_MAX]
    dec dx                             ; room excluding the NUL - in DX, NOT
                                       ; AX, because AL below is the byte being
                                       ; copied and AH is half of that limit
.copy:
    cmp cx, dx
    jae .done
    mov al, [di]
    or al, al
    jz .done
    cmp al, 0x0D                       ; CR tolerated on input, never stored
    je .skip
    mov [bx], al
    inc bx
    inc cx
.skip:
    inc di
    jmp .copy
.done:
    mov byte [bx], 0
    mov [si + TX_LEN], cx
    mov word [si + TX_CAR], 0
    mov word [si + TX_TOP], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
