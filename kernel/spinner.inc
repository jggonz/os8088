; Shared splash row composer. DS = KERNEL_SEG; CS owns the state.
; OS88_SPINNER prefix, still: emits the clock, spinner and row helpers.
; The caller supplies prefix_l_{cx,spn,spx,spp}, row buffers and cosine table.
%macro OS88_SPINNER 2
%1 %+ _clock:
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    ret

; -----------------------------------------------------------------------------
; spl_spin - one animation step: the "8088" with x scaled by cos(angle), angle
;            = the WALL CLOCK mod 16 of a revolution (SPEC.md 15.3.6). cos < 0
;            mirrors the endpoints, so the back face renders itself.
;            clobbers: ax, bx, cx, dx, si, di, bp, es
;
; IT COMPOSES EACH ROW AND STORES IT ONCE (SPEC.md 15.3.4). It used to blank
; the whole 15x41 box with spl_fill and then stroke nineteen lines into it -
; the erase-then-letter pair at the largest scale in the tree, 615 bytes
; cleared to redraw about 370, with the logo ABSENT in between. A boot
; repaints ~29 times half a second apart, so it is not subliminal: it reads as
; the logo BLINKING and was reported from the field as exactly that.
;
; The shape is what makes the composer cheap: 38 of the 41 rows are the same
; row - the eight verticals - and only 0, 20 and 40 carry a bar. So two 15-byte
; buffers hold every row this frame needs, they are built in RAM where a store
; is a store, and the box is written top to bottom with each byte visited
; ONCE. No blank, no read-modify-write, no Graphics Controller OUT.
; -----------------------------------------------------------------------------
SPL_SPINB   equ 15              ; the spinner box, in bytes
SPL_SPINR   equ 41              ; ...and its rows. WHERE it is is the layout's
                                ; (SPEC.md 15.3.5.2): it was SPL_TOP * 80 + 33,
                                ; a hard stride of 80 that is VGA's and not
                                ; Hercules' 90
%1 %+ _spin:
%if %2
    mov word [cs:%1 %+ _cos], 64   ; SPEC.md 15.3.7: the logo is STILL and the
                                ; stars are the animation. It is composed and
                                ; written every frame all the same - the box
                                ; is every-pixel-once (15.3.4), so redrawing
                                ; the same picture is invisible, and both arms
                                ; of the A/B then pay the identical logo cost
%else
    call %1 %+ _clock              ; SPEC.md 15.3.6: the angle is the WALL CLOCK,
    mov cl, SPL_SPINSH          ; and [spl_done] drives the BAR alone
    shr ax, cl
    and ax, 15
    mov bx, ax
    mov al, [cs:%1 %+ _cos_tab + bx]  ; the table is signed BYTES; [spl_cos] stays
    cbw                            ; a word, which spl_xform's imul needs
    mov [cs:%1 %+ _cos], ax
%endif

    mov ax, [vid_rowadd]        ; the row walk's three words, banked into CS
    mov [cs:%1 %+ _radd], ax       ; because everything below runs with DS = CS
    mov ax, [vid_wrapbit]
    mov [cs:%1 %+ _rwbit], ax
    mov ax, [vid_wrapfix]
    mov [cs:%1 %+ _rwfix], ax
    call %1 %+ _spinadr            ; ES:DI, through gfx_rowbase like every other
                                ; row here - so CGA's bank interleave and
                                ; Hercules' four are SPL_NEXTROW_CS's, as they
                                ; already are for the bar. BEFORE the DS
                                ; switch below: it reads [vid_seg], which is
                                ; the KERNEL's, and pointing DS at CS first
                                ; read a `.boot2` byte instead and left ES
                                ; wherever that landed - the spinner vanished
                                ; on all three adapters
    push ds
    mov ax, cs
    mov ds, ax                  ; ...so the row copy is one `rep movsb`

    xor al, al                  ; ...the plain row: verticals only
    mov bx, %1 %+ _rowa
    call %1 %+ _rowmk
    mov al, 0x0F                ; ...and every digit's bar, for rows 0 and 40
    mov bx, %1 %+ _rowb
    call %1 %+ _rowmk

    call %1 %+ _putrow             ; row 0
    mov cx, 19
    call %1 %+ _putplain           ; rows 1..19
    mov al, 0x0D                ; row 20 is the MIDDLE bar, and the second
    mov bx, %1 %+ _rowb            ; digit is a 0 - SPL_DIG0 has no middle
    call %1 %+ _rowmk
    call %1 %+ _putrow             ; row 20
    mov cx, 19
    call %1 %+ _putplain           ; rows 21..39
    mov al, 0x0F
    mov bx, %1 %+ _rowb
    call %1 %+ _rowmk
    call %1 %+ _putrow             ; row 40
    pop ds
    ret

; -----------------------------------------------------------------------------
; spl_putplain - CX rows of [spl_rowa].  spl_putrow - one row of [spl_rowb].
; Both take ES:DI at the row's first byte and leave it at the next row's.
; clobbers: ax, cx, si
; -----------------------------------------------------------------------------
%1 %+ _putplain:
    mov si, %1 %+ _rowa
    call %1 %+ _prow
    loop %1 %+ _putplain
    ret
%1 %+ _putrow:
    mov si, %1 %+ _rowb
%1 %+ _prow:
    push cx
    push si
    push di
    mov cx, SPL_SPINB
    rep movsb                   ; DS = CS (spl_spin set it): OPAQUE, and the
    pop di                      ; only write each of these bytes gets
    pop si
    pop cx
    add di, [cs:%1 %+ _radd]
    test di, [cs:%1 %+ _rwbit]
    jz .nextrow
    add di, [cs:%1 %+ _rwfix]
.nextrow:
    ret

; -----------------------------------------------------------------------------
; spl_rowmk - build a row into the 15-byte buffer at CS:BX: the eight verticals
;             always, plus the horizontal bar of each digit whose bit is set in
;             AL.  clobbers: ax, cx, dx, si, bp - and PRESERVES DI
;
; A digit's bar runs from its left vertical to its right one, so the two x
; values a digit needs are the same three ways up. spl_span does a single
; pixel as the degenerate case (x1 == x2), which is why there is no setpixel.
; -----------------------------------------------------------------------------
%1 %+ _rowmk:
    push di                     ; DI IS THE CALLER'S VRAM ROW POINTER. It is
    mov [cs:%1 %+ _rbar], al       ; spent below and spl_spin rebuilds the bar
    mov di, bx                  ; row twice DURING the walk down the box, so
    mov cx, SPL_SPINB           ; forgetting this drew the whole spinner into
    xor al, al                  ; whatever the zero loop left behind - which
.z:                             ; is a box of nothing, measured
    mov [cs:di], al             ; (the buffer is CS's, so no rep stosb: that
    inc di                      ; would address ES, the framebuffer)
    loop .z

    mov byte [cs:%1 %+ _rdig], 0
.digit:
    mov al, 28
    mul byte [cs:%1 %+ _rdig]      ; AL = the digit's box-relative x
    push ax
    call %1 %+ _xform
    mov bp, ax                  ; BP = one end, screen x
    pop ax
    add al, 20
    call %1 %+ _xform              ; AX = the other
    mov dx, ax
    mov ax, bp
    cmp ax, dx                  ; a mirrored frame swaps them
    jbe .ordered
    xchg ax, dx
.ordered:
    mov cl, [cs:%1 %+ _rdig]
    mov ch, 1
    shl ch, cl                  ; CH = this digit's bit
    test [cs:%1 %+ _rbar], ch
    jnz .bar
    push dx                     ; no bar: the two verticals, a pixel each
    mov dx, ax
    call %1 %+ _span
    pop dx
    mov ax, dx
.bar:
    call %1 %+ _span
    inc byte [cs:%1 %+ _rdig]
    cmp byte [cs:%1 %+ _rdig], 4
    jb .digit
    pop di
    ret

; -----------------------------------------------------------------------------
; spl_span - OR the run of screen x from AX to DX (AX <= DX) into the buffer at
;            CS:BX.  clobbers: ax, cx, dx, si
; -----------------------------------------------------------------------------
%1 %+ _span:
    sub ax, [cs:%1 %+ _l_spp]      ; box-relative, in pixels
    sub dx, [cs:%1 %+ _l_spp]
    mov cx, ax
    and cl, 7
    mov si, ax
    mov ax, 0x00FF
    shr al, cl                  ; AL = the first byte's mask
    mov cx, dx
    and cl, 7
    xor cl, 7
    mov ah, 0xFF
    shl ah, cl                  ; AH = the last byte's mask
    mov cl, 3                   ; ...and ONE shift count for both byte indices:
    shr si, cl                  ; SI = the first byte, still unshifted here
    shr dx, cl                  ; DX = the last
    cmp si, dx
    jne .multi
    and al, ah                  ; one byte carries both edges
    or [cs:bx+si], al
    ret
.multi:
    or [cs:bx+si], al
    inc si
.mid:
    cmp si, dx
    jae .last
    mov byte [cs:bx+si], 0xFF
    inc si
    jmp short .mid
.last:
    or [cs:bx+si], ah
    ret

; -----------------------------------------------------------------------------
; spl_xform - coin-flip a spinner x: AL = box-relative x (0..104) ->
;             AX = 320 + (AL - SPL_HALFW) * cos / 64. clobbers: nothing else
; -----------------------------------------------------------------------------
%1 %+ _xform:
    push cx
    push dx
    xor ah, ah
    sub ax, SPL_HALFW
    imul word [cs:%1 %+ _cos]         ; DX:AX = rel * cos; |result| <= 3328
    mov cl, 6
    sar ax, cl
    add ax, [cs:%1 %+ _l_cx]       ; ...and NOT 320, which is VGA's centre alone
    pop dx
    pop cx
    ret

%endmacro
