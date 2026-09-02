; =============================================================================
; os8088 - apps/weave/wspr.inc          THE SPRITE COMPOSER (WEAVE-SPEC 6.10.2)
;
; %included by apps/weave/wcanvas.asm, which is WEAVE.WSM and NOT the package
; (WEAVE-SPEC 1.2.2). DS = CS = the module throughout; ES = the canvas claim.
;
; -----------------------------------------------------------------------------
; WHAT IT DOES, AND THE TWO RULES IT EXISTS TO KEEP
; -----------------------------------------------------------------------------
; Sprite transparency is resolved IN RAM, because no masked blit exists on
; this machine - GFX_BLIT1 and GFX_BLIT4 are opaque (WEAVE-SPEC 9.6). So every
; sprite ships beside a pre-built AND mask (2.11), and the composition is
;
;     dst = (dst AND mask) OR image
;
; per byte, into the canvas's own 1bpp buffer, and ONE GFX_BLIT1 then puts a
; whole run of bands down. That is PERFORMANCE.md's two rules applied to a
; frame: nothing repaints more than it changed (only DIRTY bands are composed,
; and a still sprite on a still background costs nothing at all), and nothing
; writes a pixel twice (a byte is cleared once and OR-ed once per covering
; sprite, and reaches the glass in one call).
;
; THE BUDGET IS COUNTED, NOT FELT. WEAVE-SPEC 14 prices a two-sprite frame at
; 2-4 gfx calls and ~2-5 ms; wsm_blits and wsm_frames are in the state block
; and tests/weavegame reads the quotient. A change that moves it is a
; regression against a documented number.
;
; -----------------------------------------------------------------------------
; POLARITY, WHICH IS THE ONE THING TO GET RIGHT - AND THE FIRST BOOT GOT IT
; WRONG, WHICH IS WHY THIS PARAGRAPH IS LONGER THAN IT LOOKS LIKE IT SHOULD BE
; -----------------------------------------------------------------------------
; The framebuffer's 1 is a LIT pixel (SPEC.md 5.4.2) and 2.11's sprite images
; are packed the other way up, in "final screen polarity" with 1 = INK. The
; obvious reconciliation is OSAPI_GFX_BLIT1_PEN - set ink = CBLACK, paper =
; CWHITE and a set band bit becomes ink - and it was written that way, and on
; the glass PONG came up as a black field with white paddles.
;
; SPEC.md 5.4.2.2: **on 1bpp the pen is not read**. A set band bit is LIT and
; nothing a package does changes that; the pen is the VGA path's, where the
; colours really are a mapping. Two adapters of three are 1bpp (SPEC.md 39),
; so the pen is not the mechanism - it is a mechanism that works on one
; adapter and is silently ignored on the two this project targets.
;
; So THE BUFFER IS THE FRAMEBUFFER'S WAY UP: 1 = lit = paper, 0 = ink. A band
; run is cleared to 0xFF, and 2.11's rule
;
;     dst = (dst AND mask) OR image          ...in the sprite's polarity
;
; becomes, complemented throughout,
;
;     dst = (dst OR coverage) AND NOT image  ...in the buffer's
;
; where `coverage` is NOT(mask). The two are the same statement and the
; instruction count is identical - the shift register already has both halves
; - and it works with the DEFAULT pen on all three adapters, which is exactly
; what apps/weave/wband.inc does and why that file composes `glyph XOR 0xFF`.
;
; The AND mask is shifted as its COMPLEMENT for a second, independent reason:
; a mask shifted directly would bring ZEROS in from the end of the byte, and a
; zero mask bit means "the sprite covers this pixel" - so a sprite at an
; unaligned x would grow a covered column out of nothing. The coverage brings
; zeros in correctly, and the last output byte falls out of its carry.
; =============================================================================

; -----------------------------------------------------------------------------
; wsm_markall - every band dirty (the UI task's full repaint).
; Clobbers AX BX CX DI ES.
; -----------------------------------------------------------------------------
wsm_markall:
    mov es, [wsm_cvseg]
    mov ax, [es:WSC_H]
    mov cl, 3
    shr ax, cl                      ; bands = H / 8, and H is a multiple of 8
    mov cx, ax                      ; by 2.5's rounding, so the last band is
    jcxz .out                       ; whole
    mov di, wsm_dirty
    mov al, 1
    push es
    push cs
    pop es
    rep stosb
    pop es
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_markrows - mark the bands covering pixel rows AX..BX (inclusive, signed),
; clipped to the buffer. Clobbers AX BX CX DX SI.
; -----------------------------------------------------------------------------
wsm_markrows:
    or ax, ax
    jns .lo
    xor ax, ax
.lo:
    mov dx, [es:WSC_H]
    dec dx
    cmp bx, dx
    jle .hi
    mov bx, dx
.hi:
    cmp ax, bx
    jg .out                         ; wholly outside the buffer
    mov cl, 3
    sar ax, cl
    sar bx, cl
    mov si, ax
    sub bx, ax
    inc bx                          ; BX = how many bands
    mov cx, bx
    add si, wsm_dirty
.m: mov byte [si], 1
    inc si
    loop .m
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_markspr - mark the bands one sprite's OLD and NEW rectangles touch, if
; anything about it changed since the last composition.
;
; in:  ES = the canvas claim, BX = the sprite index
; out: nothing. Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
wsm_markspr:
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov di, ax                      ; ES:DI = the record
    mov al, [es:di + WSR_FLAGS]
    mov ah, al
    and ah, WSRF_DIRTY              ; a JS write already said so
    jnz .dirty
    ; shown-ness changed?
    mov ah, al
    and al, WSRF_SHOWN
    and ah, WSRF_WASSH
    shr ah, 1
    shr ah, 1                       ; WSRF_WASSH >> 2 == WSRF_SHOWN
    cmp al, ah
    jne .dirty
    or al, al
    jz .out                         ; not shown, and was not: nothing to do
    mov ax, [es:di + WSR_X]
    cmp ax, [es:di + WSR_OX]
    jne .dirty
    mov ax, [es:di + WSR_Y]
    cmp ax, [es:di + WSR_OY]
    jne .dirty
    mov al, [es:di + WSR_FRAME]     ; the low nibble against the high one
    mov ah, al
    mov cl, 4
    shr ah, cl
    and al, 0x0F
    cmp al, ah
    je .out
.dirty:
    ; wsm_markrows does not touch DI, which is the record - so the old
    ; rectangle and the new one are two calls with nothing saved between them
    test byte [es:di + WSR_FLAGS], WSRF_WASSH
    jz .new
    mov ax, [es:di + WSR_OY]
    mov bx, ax
    mov cl, [es:di + WSR_PH]
    xor ch, ch
    add bx, cx
    dec bx
    call wsm_markrows
.new:
    test byte [es:di + WSR_FLAGS], WSRF_SHOWN
    jz .out
    mov ax, [es:di + WSR_Y]
    mov bx, ax
    mov cl, [es:di + WSR_PH]
    xor ch, ch
    add bx, cx
    dec bx
    call wsm_markrows
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_composed - a sprite's state as it now stands IS what is on the glass.
; in: ES = the claim, BX = the index. Clobbers AX CX SI.
; -----------------------------------------------------------------------------
wsm_composed:
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov si, ax
    mov ax, [es:si + WSR_X]
    mov [es:si + WSR_OX], ax
    mov ax, [es:si + WSR_Y]
    mov [es:si + WSR_OY], ax
    mov al, [es:si + WSR_FRAME]
    and al, 0x0F
    mov ah, al
    mov cl, 4
    shl ah, cl
    or al, ah
    mov [es:si + WSR_FRAME], al
    mov al, [es:si + WSR_FLAGS]
    and al, ~(WSRF_WASSH | WSRF_DIRTY) & 0xFF
    test byte [es:si + WSR_FLAGS], WSRF_SHOWN
    jz .st
    or al, WSRF_WASSH
.st:
    mov [es:si + WSR_FLAGS], al
    ret

; -----------------------------------------------------------------------------
; wsm_drawspr - compose ONE sprite into the buffer, clipped to pixel rows
; [wsm_r0]..[wsm_r1].
;
; in:  ES = the canvas claim, BX = the sprite index
; Clobbers everything but BP, DS, ES, SS:SP.
; -----------------------------------------------------------------------------
wsm_drawspr:
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov si, ax
    test byte [es:si + WSR_FLAGS], WSRF_SHOWN
    jz .out
    mov al, [es:si + WSR_WB]
    or al, al
    jz .out                         ; no image bound to this component
    xor ah, ah
    mov [wsm_wb], ax
    mov al, [es:si + WSR_PH]
    xor ah, ah
    mov [wsm_ph], ax

    ; --- the row range: the sprite, the run and the buffer, intersected -----
    mov ax, [es:si + WSR_Y]
    mov [wsm_sy0], ax               ; the sprite's own top row
    mov bx, ax
    add bx, [wsm_ph]
    dec bx                          ; ...and its bottom
    cmp ax, [wsm_r0]
    jge .t1
    mov ax, [wsm_r0]
.t1:
    or ax, ax
    jns .t2
    xor ax, ax
.t2:
    cmp bx, [wsm_r1]
    jle .b1
    mov bx, [wsm_r1]
.b1:
    mov cx, [es:WSC_H]
    dec cx
    cmp bx, cx
    jle .b2
    mov bx, cx
.b2:
    cmp ax, bx
    jg .out
    mov [wsm_rtop], ax
    sub bx, ax
    inc bx
    mov [wsm_nrow], bx              ; rows to compose, 1..64

    ; --- copy the frame's rows into the scratch (6.10.2's fixed separation) --
    mov ax, [wsm_ph]
    mov bx, [wsm_wb]
    mul bx                          ; AX = one frame's image bytes
    mov [wsm_fsz], ax
    mov cx, ax
    shl cx, 1                       ; a frame is image THEN mask
    mov al, [es:si + WSR_FRAME]
    and ax, 0x000F
    mul cx                          ; AX = the frame's offset within the sprite
    add ax, [es:si + WSR_DOFF]
    mov di, ax                      ; the image, in the bundle claim
    mov ax, [wsm_rtop]
    sub ax, [wsm_sy0]               ; the first source row
    mul word [wsm_wb]
    add di, ax
    mov cx, [wsm_nrow]
    mov ax, [wsm_wb]
    mul cx
    mov cx, ax                      ; bytes to copy, <= 512 (2.11's cap)
    mov [wsm_cpy], cx
    mov ax, [es:si + WSR_X]
    mov [wsm_sx0], ax
    mov ax, di
    add ax, [wsm_fsz]               ; the mask rows sit one frame-image on
    mov [wsm_msrc], ax
    push es
    push ds
    mov si, di
    push cs
    pop es
    mov di, wsm_simg
    mov ds, [cs:wsm_bseg]
    rep movsb                       ; the image rows...
    mov cx, [cs:wsm_cpy]
    mov si, [cs:wsm_msrc]
    mov di, wsm_smsk
    rep movsb                       ; ...and the mask rows
    pop ds
    pop es

    ; --- the pixel loop -----------------------------------------------------
    mov ax, [wsm_sx0]
    mov cx, ax
    and cx, 7
    mov [wsm_shift], cl             ; the sub-byte shift, 0..7
    mov cl, 3
    sar ax, cl                      ; the first output byte; ARITHMETIC, so a
    mov bx, ax                      ; sprite at a negative x lands left of the
                                    ; buffer rather than at 8,191 bytes into it
    mov ax, [wsm_rtop]
    mul word [es:WSC_STRIDE]
    add ax, [es:WSC_BUF]
    mov [wsm_rowlo], ax
    add ax, [es:WSC_STRIDE]
    mov [wsm_rowhi], ax
    sub ax, [es:WSC_STRIDE]
    add ax, bx
    mov di, ax                      ; ES:DI = where source byte 0 lands
    mov si, wsm_simg
.row:
    push si
    push di
    mov cl, [wsm_shift]
    mov ch, [wsm_wb]
    xor bx, bx                      ; BL = the image carry, BH = the coverage
.byte:
    mov ah, [si]                    ; the image byte...
    xor al, al
    shr ax, cl                      ; AH = v >> s, AL = v << (8-s)
    mov dh, [si + WSM_SIMGMAX]      ; ...and the mask, at the fixed separation
    not dh                          ; coverage = NOT mask: shifting THIS is
    xor dl, dl                      ; what brings the right bits in from the
    shr dx, cl                      ; end of the byte
    or ah, bl                       ; the carry from the byte to the left
    or dh, bh
    mov bl, al                      ; ...and the carry out
    mov bh, dl
    cmp di, [wsm_rowlo]
    jb .skip
    cmp di, [wsm_rowhi]
    jae .skip
    mov al, [es:di]
    or al, dh                       ; the coverage lights it...
    not ah                          ; ...and the image's ink puts it out again
    and al, ah                      ; (6.10.2's polarity - see the head of this
    mov [es:di], al                 ; file: the buffer is the FRAMEBUFFER's way
.skip:                              ; up, 1 = LIT, because the pen is not read
                                    ; on 1bpp and two adapters of three are)
    inc si
    inc di
    dec ch
    jnz .byte
    ; the tail byte: what the last shift carried past the sprite's right edge
    cmp di, [wsm_rowlo]
    jb .norow
    cmp di, [wsm_rowhi]
    jae .norow
    mov al, [es:di]
    or al, bh
    mov dh, bl
    not dh
    and al, dh
    mov [es:di], al
.norow:
    pop di
    pop si
    add si, [wsm_wb]
    mov ax, [es:WSC_STRIDE]
    add di, ax
    add [wsm_rowlo], ax
    add [wsm_rowhi], ax
    dec word [wsm_nrow]
    jnz .row
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_emit - one span of one run onto the glass (WEAVE-SPEC 6.10.2 step 3,
; 6.10.7's spans).
;
; in:  ES = the canvas claim, AX = the span's first byte COLUMN, CX = how many
;      columns, DL = the ink a CLEAR bit takes, or 0xFF for "no pen".
;      [wsm_r0]/[wsm_r1] are the run's pixel rows and [wsm_bofs] its first
;      byte. Clobbers AX BX CX DX SI DI BP.
;
; The pen is (AL = the canvas's PAPER colour, AH = this span's ink) and the
; names cross over because the buffer is the FRAMEBUFFER's way up: a set bit
; is the background (6.10.2). An implementation that "fixes" the naming by
; turning the buffer over reintroduces wave 5's inverted CGA picture exactly.
; -----------------------------------------------------------------------------
wsm_emit:
    mov di, ax                      ; DI = the span's first column
    mov bx, cx                      ; BX = its column count
    mov word [wsm_curpen], 0xFFFF
    cmp dl, 0xFF
    je .nopen
    mov dh, dl                      ; DH = this span's ink...
    mov dl, [wsm_pen]               ; ...DL = the canvas's paper colour
    mov [wsm_curpen], dx            ; the harness's reading: low byte paper,
                                    ; high byte ink, which is the model's own
                                    ; order and not 5.4.2.2's register one
%ifndef WSM_HOSTTEST
    mov al, dl                      ; AL = ink: what a SET band bit becomes,
    mov ah, dh                      ; which is the BACKGROUND (6.10.2)
    call OSAPI_GFX_BLIT1_PEN        ; preserves every register (5.4.2.2), and
                                    ; is a no-op on 1bpp - where the load path
                                    ; has already left `colored` clear anyway
%endif
.nopen:
    mov si, [wsm_bofs]
    add si, di                      ; ES:SI = the span's first byte
    mov bp, [es:WSC_STRIDE]         ; ...and the walk is still the BUFFER's
    mov ax, di                      ; stride, which is what lets a narrower
    mov cl, 3                       ; blit read the same rows
    shl ax, cl
    add ax, [wsm_sx]                ; AX = the span's screen x
    shl bx, cl                      ; ...and BX = 8 x its columns. CX is loaded
    push bx                         ; LAST, because CL is the shift count and
    mov bx, [wsm_sy]                ; `mov cx, bx` would eat it - which it did,
    add bx, [wsm_r0]                ; and the harness caught it as a 256-column
    mov dx, [wsm_r1]                ; span
    sub dx, [wsm_r0]
    inc dx
    pop cx
%ifdef WSM_HOSTTEST
    call wsm_hostblit               ; the boot-sector differential has no
%else                               ; kernel under it, so it records the span
    call OSAPI_GFX_BLIT1            ; x and CX are multiples of 8 by
%endif                              ; construction (3.3 requires w to be, and
                                    ; 7.1.2 puts the content origin on one)
    inc word [wsm_nblit]
    ret

; -----------------------------------------------------------------------------
; wsm_mkcolmap - 6.10.7's colour map for the run [wsm_r0]..[wsm_r1]: one byte a
; byte column, 0xFF where no shown sprite reaches.
;
; in: ES = the canvas claim. Clobbers AX BX CX DX SI DI.
;
; UISTREAM order, LAST writer wins - which is composition order, so a byte
; column two sprites share wears the colour of the one whose pixels are on
; top. Two frames of PONG's contact, and stated rather than fixed: the
; alternative is a second pass over the same pixels, which is
; PERFORMANCE.md's double-draw flash bought with a colour.
; -----------------------------------------------------------------------------
wsm_mkcolmap:
    mov di, wsm_colmap
    mov cx, [wsm_ncol]
    mov al, 0xFF
    push es
    push cs
    pop es
    cld
    rep stosb
    pop es
    xor bx, bx
.s:
    cmp bx, [wsm_nspr]
    jae .out
    mov al, WSR_SIZE
    mul bl
    add ax, WSC_SPR
    mov si, ax                      ; ES:SI = the record
    test byte [es:si + WSR_FLAGS], WSRF_SHOWN
    jz .next
    cmp byte [es:si + WSR_WB], 0
    je .next
    ; does it reach these rows?  SIGNED, and the same two compares 6.10.2's
    ; compose makes about the same rectangle
    mov ax, [es:si + WSR_Y]
    cmp ax, [wsm_r1]
    jg .next
    mov cl, [es:si + WSR_PH]
    xor ch, ch
    add ax, cx
    dec ax
    cmp ax, [wsm_r0]
    jl .next
    ; ...and which byte columns
    mov ax, [es:si + WSR_X]
    mov cl, 3
    sar ax, cl                      ; ARITHMETIC: a sprite at a negative x is
    or ax, ax                       ; left of the buffer, not 8,191 bytes into
    jns .c0
    xor ax, ax
.c0:
    push ax
    mov ax, [es:si + WSR_X]
    mov cl, [es:si + WSR_PW]
    xor ch, ch
    add ax, cx
    dec ax
    mov cl, 3
    sar ax, cl
    mov dx, [wsm_ncol]
    dec dx
    cmp ax, dx
    jle .c1
    mov ax, dx
.c1:
    pop cx                          ; CX = the first column
    cmp ax, cx
    jl .next                        ; wholly outside the buffer's columns
    sub ax, cx
    inc ax                          ; AX = how many
    mov di, cx
    add di, wsm_colmap
    mov cx, ax
    mov al, [es:si + WSR_FLAGS]
    push cx
    mov cl, WSRF_COLSH
    shr al, cl
    pop cx
    and al, 0x0F
    push es
    push cs
    pop es
    rep stosb
    pop es
.next:
    inc bx
    jmp .s
.out:
    ret

; -----------------------------------------------------------------------------
; wsm_flush - compose and emit every dirty band run.
;
; in:  ES = the canvas claim, AX = the canvas's screen x, BX = its screen y.
;      The gfx lock is HELD by the caller and the clip is armed.
; out: AX = the blits emitted. The dirty array is cleared and every sprite's
;      "as last composed" state is brought up to date.
; -----------------------------------------------------------------------------
wsm_flush:
    mov [wsm_sx], ax
    mov [wsm_sy], bx
    mov word [wsm_nblit], 0
    mov ax, [es:WSC_H]
    mov cl, 3
    shr ax, cl
    mov [wsm_nband], ax
    xor bx, bx                      ; the band cursor
.scan:
    cmp bx, [wsm_nband]
    jae .done
    mov si, bx
    add si, wsm_dirty
    cmp byte [si], 0
    jne .run
    inc bx
    jmp short .scan
.run:
    mov [wsm_b0], bx
.ext:
    inc bx
    cmp bx, [wsm_nband]
    jae .emit
    mov si, bx
    add si, wsm_dirty
    cmp byte [si], 0
    jne .ext
.emit:
    mov [wsm_b1], bx                ; exclusive
    push bx
    ; the run's pixel rows
    mov ax, [wsm_b0]
    mov cl, 3
    shl ax, cl
    mov [wsm_r0], ax
    mov bx, [wsm_b1]
    shl bx, cl
    mov [wsm_r1], bx
    dec word [wsm_r1]
    ; clear it to paper, then compose every shown sprite that reaches it
    mov ax, [wsm_r1]
    sub ax, [wsm_r0]
    inc ax
    mul word [es:WSC_STRIDE]
    mov cx, ax
    mov ax, [wsm_r0]
    mul word [es:WSC_STRIDE]
    add ax, [es:WSC_BUF]
    mov di, ax
    mov [wsm_bofs], di
    mov al, 0xFF
    rep stosb                       ; the background is PAPER, which in the
                                    ; framebuffer's own polarity is every bit
                                    ; LIT. v1 carries no background art, and a
                                    ; bundle that wants one draws it with
                                    ; sprites
    xor bx, bx
.spr:
    cmp bx, [wsm_nspr]
    jae .blit
    push bx
    call wsm_drawspr
    pop bx
    inc bx
    jmp short .spr
.blit:
    mov ax, [es:WSC_STRIDE]
    mov [wsm_ncol], ax
    cmp byte [wsm_pen + 1], 0
    jne .colored
    ; --- the uncoloured run: 6.10.2 step 3, unchanged and un-penned ---------
    xor ax, ax
    mov cx, [wsm_ncol]
    mov dl, 0xFF                    ; "no GFX_BLIT1_PEN call at all"
    call wsm_emit
    jmp short .runend
    ; --- 6.10.7's colour map and span walk ---------------------------------
.colored:
    call wsm_mkcolmap
    xor si, si                      ; the column cursor
    mov word [wsm_sstart], 0
    mov byte [wsm_scur], 0xFF       ; ...and the span's colour: NONE yet
.cw:
    cmp si, [wsm_ncol]
    jae .ctail
    mov bx, si
    mov al, [wsm_colmap + bx]
    cmp al, 0xFF
    je .cnext                       ; NEUTRAL: every bit is paper, so its ink
                                    ; cannot show and it absorbs into the span
                                    ; to its LEFT
    cmp byte [wsm_scur], 0xFF
    jne .ccmp
    mov [wsm_scur], al              ; the run's first coloured column names
    jmp short .cnext                ; the span that started at column 0
.ccmp:
    cmp al, [wsm_scur]
    je .cnext
    push si
    push ax
    mov ax, [wsm_sstart]
    mov cx, si
    sub cx, ax
    mov dl, [wsm_scur]
    call wsm_emit
    pop ax
    pop si
    mov [wsm_sstart], si
    mov [wsm_scur], al
.cnext:
    inc si
    jmp short .cw
.ctail:
    mov dl, [wsm_scur]
    cmp dl, 0xFF
    jne .ct1
    xor dl, dl                      ; a run no sprite reaches is all paper, so
                                    ; the ink is arbitrary - and CBLACK is the
                                    ; one colour legal against every paper
                                    ; (0 is a subset of all sixteen), which is
                                    ; why the fallback is pinned rather than
                                    ; borrowed from the canvas's `ink`
.ct1:
    mov ax, [wsm_sstart]
    mov cx, [wsm_ncol]
    sub cx, ax
    call wsm_emit
.runend:
    pop bx
    jmp .scan
.done:
    ; The pen dies with the gfx lock (SPEC.md 5.4.2.2) and the caller holds it
    ; past this return - WSMV_PAINT runs inside a paint callback whose other
    ; components are lettered after us, and wband.inc puts down bands too. So
    ; a coloured flush that drew anything puts the default pair back.
    cmp byte [wsm_pen + 1], 0
    je .nopenrst
    cmp word [wsm_nblit], 0
    je .nopenrst
%ifndef WSM_HOSTTEST
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
%endif
.nopenrst:
    mov di, wsm_dirty
    mov cx, WSM_MAXBAND
    xor al, al
    push es
    push cs
    pop es
    rep stosb
    pop es
    xor bx, bx
.upd:
    cmp bx, [wsm_nspr]
    jae .fin
    push bx
    call wsm_composed
    pop bx
    inc bx
    jmp short .upd
.fin:
    mov ax, [wsm_nblit]
    add [wsm_blits], ax
    ret
