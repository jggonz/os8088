; =============================================================================
; os8088 - tests/spantest/spantest.asm
;
; SPANTEST: the gate on SPEC.md 5.10's gfx_spans, and it exists because
; nothing else covers the primitive except apps/paint - which only ever asks
; for the shapes a brush chord makes. Four things in the contract had no test
; at all: an EMPTY row, the vertical clip, a middle grey's row alternation,
; and the refusal.
;
; THE ASSERTION IS THE PRIMITIVE AGAINST ITS OWN DOCUMENTED FALLBACK. SPEC.md
; 5.10.3 says a refused call is answered by one GFX_FILL a row out of the same
; list, drawing the identical pixels - so that loop is not a second opinion
; about the shape, it is the SAME span list through machinery that predates
; this feature by years. Every case below is drawn twice, once each way, over
; a ground erased identically first, and the host compares the two.
;
; The list is GENERATED from an arithmetic ramp rather than written out,
; because what varies between the interesting cases is one step: x1 and x2
; converging is what produces empty rows, a step of zero is what produces the
; wide and clipped shapes, and both are two words of table instead of sixteen.
;
; Never shipped: it is built into its own scratch image, like fmtest.
;
;   make spantest && python3 tests/spantest.py
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SPANS', st_entry

ST_MAXROWS equ 64                   ; the deepest case here is 20

STF_YREL equ 1                      ; y0 is relative to the BOTTOM of the screen
STF_XREL equ 2                      ; x2 is relative to its RIGHT edge

%macro STCASE 8                     ; y0, rows, x1, dx1, x2, dx2, colour, flags
    dw %1, %2, %3, %4, %5, %6
    db %7, %8
%endmacro
ST_CSZ equ 14

; -----------------------------------------------------------------------------
; st_entry - package entry point (SPEC.md 20.2)
; -----------------------------------------------------------------------------
st_entry:
    push si
    mov si, st_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; -----------------------------------------------------------------------------
; st_paint - W_PAINT: a line of text, and nothing this test reads
;
; The cases are NOT drawn here. A W_PAINT can run with the clip region armed
; since SPEC.md 11.3.3's cull, and gfx_spans REFUSES an armed region (5.10.3),
; so a paint-driven test would pass or fail by whether a damage pass happened
; to ask for a cull. W_ONKEY is on the UI task under the lock with nothing
; armed, which is the environment the contract describes.
; -----------------------------------------------------------------------------
st_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, 8
    add dx, 12
    mov si, st_s_line
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; st_onkey - draw [st_case] in mode [st_mode], both of which the HOST wrote
; in:  AL = ascii, AH = scan, SI = window ptr; the gfx lock is held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
st_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call st_run
    inc byte [st_done]              ; the host waits on this, not on a settle
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; st_run - erase this case's box, then draw the case the requested way
; -----------------------------------------------------------------------------
st_run:
    call OSAPI_VIDEO                ; AX = width, BX = height
    mov [st_w], ax
    mov [st_h], bx
    mov al, [st_case]               ; SI -> the descriptor
    xor ah, ah
    mov bx, ST_CSZ
    mul bx
    mov si, ax
    add si, st_cases

    mov ax, [si+0]                  ; y0, resolved against the screen
    test byte [si+13], STF_YREL
    jz .y0
    add ax, [st_h]
.y0:
    mov [st_y0], ax
    mov ax, [si+2]
    mov [st_rows], ax
    mov ax, [si+8]                  ; x2, likewise
    test byte [si+13], STF_XREL
    jz .x2
    add ax, [st_w]
.x2:
    mov [st_x2], ax

    ; --- generate the list, and the bounding box the host will hash ---------
    mov di, st_buf
    mov ax, [si+4]                  ; AX = x1 running, DX = x2 running
    mov dx, [st_x2]
    mov [st_bx1], ax                ; seed the box with row 0's own interval
    mov [st_bx2], dx
    mov cx, [st_rows]
.row:
    mov [di], ax
    mov [di+2], dx
    cmp ax, [st_bx1]
    jge .lo
    mov [st_bx1], ax
.lo:
    cmp dx, [st_bx2]
    jle .hi
    mov [st_bx2], dx
.hi:
    add ax, [si+6]                  ; dx1
    add dx, [si+10]                 ; dx2
    add di, 4
    loop .row

    mov ax, [st_bx1]                ; two pixels of margin, so a span that
    sub ax, 2                       ; leaked sideways lands INSIDE the box the
    mov [st_bx1], ax                ; host hashes rather than outside it
    mov ax, [st_bx2]
    add ax, 2
    mov [st_bx2], ax
    mov ax, [st_y0]
    dec ax
    mov [st_by1], ax
    mov ax, [st_y0]
    add ax, [st_rows]
    mov [st_by2], ax

    ; --- the ground, identically for both modes -----------------------------
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [st_bx1]
    mov bx, [st_by1]
    mov cx, [st_bx2]
    mov dx, [st_by2]
    call OSAPI_GFX_FILL

    mov al, [si+12]                 ; the case's ink
    call OSAPI_SET_COLOR
    mov byte [st_cf], 0
    cmp byte [st_mode], 0
    jne .fills

    mov ax, [st_y0]                 ; ONE call
    mov cx, [st_rows]
    mov si, st_buf
    call OSAPI_GFX_SPANS
    jnc .out
    mov byte [st_cf], 1             ; refused - the host asserts on this
    ret

.fills:                             ; ...against SPEC.md 5.10.3's fallback
    mov si, st_buf
    mov bx, [st_y0]
    mov cx, [st_rows]
.frow:
    push cx
    mov ax, [si]
    mov cx, [si+2]
    cmp ax, cx
    jg .fnext                       ; an empty row draws nothing
    mov dx, bx
    push bx
    push si
    call OSAPI_GFX_FILL             ; AX = x1, BX = y1, CX = x2, DX = y2
    pop si
    pop bx
.fnext:
    add si, 4
    inc bx
    pop cx
    loop .frow
.out:
    ret

; -----------------------------------------------------------------------------
st_tpl:
    dw 40, 300, 200, 40
    dw st_ttl, st_paint, st_onkey, 0

st_ttl:     db 'Spans', 0
st_s_line:  db 'driven by the host', 0

; y0, rows, x1, dx1, x2, dx2, colour, flags
st_cases:
    STCASE  30, 16,  40, 1,  55, 1, CBLACK, 0          ; 0 a swept parallelogram
    STCASE  55, 16,  40, 3,  85,-3, CBLACK, 0          ; 1 converging -> EMPTY rows
    STCASE  80, 12,  40, 0, 300, 0, CBLACK, 0          ; 2 wide: a rep stosb interior
    STCASE 100, 16,  48, 1,  48, 1, CBLACK, 0          ; 3 one pixel: both masks, one byte
    STCASE 125, 10, -40, 0,  40, 0, CBLACK, STF_XREL   ; 4 off BOTH side edges
    STCASE  -8, 20, 100, 0, 160, 0, CBLACK, 0          ; 5 starts ABOVE the screen
    STCASE -10, 20, 200, 0, 260, 0, CBLACK, STF_YREL   ; 6 runs off the BOTTOM
    STCASE 140, 16, 300, 1, 340, 1, CLGRAY, 0          ; 7 a middle grey: 39.4's dither
    STCASE 160,  8,  40, 0,  47, 0, CBLACK, 0          ; 8 one whole BYTE, aligned
ST_NCASE equ 9

st_case:    db 0                    ; the host writes these two...
st_mode:    db 0                    ; 0 = OSAPI_GFX_SPANS, 1 = a fill a row
st_done:    db 0                    ; ...and waits on this
st_cf:      db 0                    ; 1 = the call was REFUSED
st_w:       dw 0
st_h:       dw 0
st_y0:      dw 0
st_rows:    dw 0
st_x2:      dw 0
st_bx1:     dw 0                    ; the box the host hashes
st_by1:     dw 0
st_bx2:     dw 0
st_by2:     dw 0
st_buf:     times ST_MAXROWS * 2 dw 0

    OS88_IMAGE_END
