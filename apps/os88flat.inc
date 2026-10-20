; =============================================================================
; os8088 - apps/os88flat.inc
;
; THE FLAT VOCABULARY (SPEC.md 20.5.3): the elements of a colour face drawn
; FLAT - filled shapes, one-pixel rims, colour that carries meaning - for a
; package's 4bpp display. MIDIRack's face (SPEC.md 105.9.5) is the bevelled
; look; this is the other one, and the `vga-face` skill names both.
;
; It owns no state but a few scratch words in the image, takes absolute
; screen coordinates (inclusive), wants the gfx lock held (a paint, or after
; OSAPI_WM_CLIP_SET), and preserves every register unless a routine says
; otherwise. Like os88ui.inc it goes AT THE END OF YOUR CODE.
;
; Every element writes each pixel ONCE except flat_card, which is full-paint
; decoration and is allowed one overdraw (its title over its ground).
;
;   flat_card   AX,BX,CX,DX = rect, SI = title or 0       - 4 calls
;   flat_lamp   AX,BX = top-left of an 8x8 light, CL = colour   - 2 calls
;   flat_check  AX,BX = top-left of an 11x11 box (x on the 8 grid for
;               kern_small), CL = 0 empty / 1 ticked      - 2 calls
;   flat_btn    an OS88UI_OWN painter (SPEC.md 13.8.10)   - 6 calls
;   flat_big    a double-size bold caption in one BLIT1   - 2 calls
;
; flat_btn reads two caller arrays, named before the %include:
;   %define FLAT_BCAP  my_caps   ; a word per button: its caption, or 0 for a
;                                ; button that is not on this face (nothing
;                                ; is drawn and its rect should be off-glass)
;   %define FLAT_BSTY  my_styles ; a byte per button: the FACE colour. CWHITE
;                                ; is the plain button (black caption); any
;                                ; other colour gets a white caption
;
; flat_big wants apps/os88gfx.inc included FIRST with GFXE_TEXT, GFXE_SCALE
; and GFXE_BITS, a band 16 rows high, and `%define FLAT_BIG`:
;   in:  SI = caption, DI = a 16-row gfxe_bits icon or 0, AX = x (a multiple
;        of 8, for kern_small's BLIT1), BX = y, CL = ink, CH = paper
;   The WHOLE band is put down every time, so a shorter caption replaces a
;   longer one with no erase: size GFXE_BAND_W for the longest.
; =============================================================================

%ifdef FLAT_BCAP
  %ifndef FLAT_BSTY
    %error "os88flat.inc: FLAT_BCAP wants FLAT_BSTY, the face colours"
  %endif
%endif

; -----------------------------------------------------------------------------
; flat_card - a white card with a dark rim, a blue title and a ruled line
; -----------------------------------------------------------------------------
flat_card:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    inc ax
    inc bx
    dec cx
    dec dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    or si, si
    jz .out
    push cx
    push dx
    mov cx, ax
    add cx, 5
    mov dx, bx
    add dx, 3
    mov ax, (CWHITE << 8) | CBLUE
    clc
    call OSAPI_GFX_PEN
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    sub cx, 3                   ; the rule under the title
    add ax, 3
    push ax
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    pop ax
    mov dx, bx
    add dx, 12
    mov bx, cx
    call OSAPI_GFX_HLINE
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; flat_lamp - an 8x8 status light: a black rim round a coloured face
; -----------------------------------------------------------------------------
flat_lamp:
    push ax
    push bx
    push cx
    push dx
    push si
    push cx
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FRAME
    inc ax
    inc bx
    dec cx
    dec dx
    pop si                      ; the colour (CL), without a second register
    push ax
    mov ax, si
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; flat_check - an 11x11 check box, ONE blit: box and tick in one band
; -----------------------------------------------------------------------------
flat_check:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push ax
    mov al, CDGRAY              ; empty: a grey box
    mov si, flat_chk0
    or cl, cl
    jz .pen
    mov al, CGREEN              ; ticked: green box and tick
    mov si, flat_chk1
.pen:
    mov ah, CWHITE
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    push ds
    pop es
    mov bp, 2
    mov cx, 11
    mov dx, 11
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
flat_chk0:
    dw 0xe0ff,0x2080,0x2080,0x2080,0x2080,0x2080,0x2080,0x2080,0x2080,0x2080,0xe0ff
flat_chk1:                      ; bit 7 of the FIRST byte is the left pixel,
    dw 0xe0ff,0x2080,0xa080,0xa081,0x2083,0x20a6,0x20bc,0x2098,0x2080,0x2080,0xe0ff

%ifdef FLAT_BCAP
; -----------------------------------------------------------------------------
; flat_btn - THE OS88UI_OWN PAINTER (SPEC.md 13.8.10)
; in:  AX = the button's index, BX = its rect (screen, inclusive, ds:), DI =
;      flags with OS88UI_DOWN / OS88UI_DIS resolved; the gfx lock held
; out: every register preserved
;
; A rim, then the face RINGED round an opaque caption - above, below, left,
; right - so no pixel is written twice and a press redraws in place.
; -----------------------------------------------------------------------------
flat_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, ax
    mov al, [FLAT_BSTY+si]
    shl si, 1
    mov si, [FLAT_BCAP+si]
    or si, si
    jz .out                     ; not on this face
    mov ah, CWHITE              ; AH = caption, AL = face
    cmp al, CWHITE
    jne .state
    mov ah, CBLACK
.state:
    mov dl, CBLACK              ; DL = rim
    test di, OS88UI_DIS
    jz .live
    mov ax, (CDGRAY << 8) | CLGRAY
    mov dl, CDGRAY
    jmp short .rim
.live:
    test di, OS88UI_DOWN
    jz .rim
    mov ax, (CWHITE << 8) | CDGRAY
.rim:
    mov [flat_bcol], ax
    mov al, dl
    call OSAPI_SET_COLOR
    mov ax, [bx]
    mov [flat_br], ax
    mov ax, [bx+2]
    mov [flat_br+2], ax
    mov ax, [bx+4]
    mov [flat_br+4], ax
    mov ax, [bx+6]
    mov [flat_br+6], ax
    mov ax, [flat_br]
    mov bx, [flat_br+2]
    mov cx, [flat_br+4]
    mov dx, [flat_br+6]
    call OSAPI_GFX_FRAME
    push si
    call OSAPI_FONT_WIDTH       ; AX = the caption's width
    pop si
    mov bp, ax
    mov cx, [flat_br+4]         ; centred, and kept inside the face
    sub cx, [flat_br]
    sub cx, ax
    sar cx, 1
    add cx, [flat_br]
    inc cx
    cmp cx, [flat_br]
    jg .cx
    mov cx, [flat_br]
    inc cx
.cx:
    mov dx, [flat_br+6]
    sub dx, [flat_br+2]
    sub dx, 7
    sar dx, 1
    add dx, [flat_br+2]
    mov [flat_bt], cx           ; caption x
    mov [flat_bt+2], dx         ; caption y
    add bp, cx
    dec bp                      ; BP = caption x2
    mov al, [flat_bcol]
    call OSAPI_SET_COLOR
    mov ax, [flat_br]           ; above the caption
    inc ax
    mov bx, [flat_br+2]
    inc bx
    mov cx, [flat_br+4]
    dec cx
    mov dx, [flat_bt+2]
    dec dx
    call flat_rfill
    mov bx, [flat_bt+2]         ; below it
    add bx, 8
    mov dx, [flat_br+6]
    dec dx
    call flat_rfill
    mov bx, [flat_bt+2]         ; left of it
    mov dx, bx
    add dx, 7
    mov cx, [flat_bt]
    dec cx
    call flat_rfill
    mov ax, bp                  ; right of it
    inc ax
    mov cx, [flat_br+4]
    dec cx
    call flat_rfill
    mov cx, [flat_bt]
    mov dx, [flat_bt+2]
    mov ax, [flat_bcol]
    xchg al, ah                 ; ink = caption, paper = face
    clc
    call OSAPI_GFX_PEN
    call OSAPI_FONT_RUN
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; A fill that may be empty (x2 < x1 or y2 < y1): nothing is drawn.
flat_rfill:
    cmp cx, ax
    jl .no
    cmp dx, bx
    jl .no
    call OSAPI_GFX_FILL
.no:
    ret
flat_bcol: dw 0
flat_br: dw 0,0,0,0
flat_bt: dw 0,0
%endif

%ifdef FLAT_BIG
; -----------------------------------------------------------------------------
; flat_big - a double-size bold caption, and an icon, in ONE BLIT1
; -----------------------------------------------------------------------------
flat_big:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [flat_gx], ax
    mov [flat_gy], bx
    mov [flat_gc], cx
    push si
    push di
    mov word [gfxe_bh], 16
    xor ax, ax                  ; set bits are ink: a clear band is paper
    call gfxe_bclear
    mov byte [gfxe_tsty], GFXE_INV
    pop si
    xor cx, cx
    or si, si
    jz .text
    xor dx, dx
    call gfxe_bits
    mov cx, 24
.text:
    pop si
    mov byte [gfxe_tsty], GFXE_INV | GFXE_BOLD | GFXE_X2
    xor dx, dx
    call gfxe_text
    mov ax, [flat_gc]
    call OSAPI_GFX_BLIT1_PEN
    push ds
    pop es
    mov si, GFXE_BAND_BUF
    mov bp, GFXE_BAND_ST
    mov ax, [flat_gx]
    mov bx, [flat_gy]
    mov cx, GFXE_BAND_W
    mov dx, 16
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
flat_gx: dw 0
flat_gy: dw 0
flat_gc: dw 0
%endif
