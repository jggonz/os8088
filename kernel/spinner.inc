; Shared splash row composer. DS = KERNEL_SEG; CS owns the state.
; OS88_SPINNER prefix, still: emits the clock, spinner and row helpers.
; A COPY OUTSIDE `.boot2` IS A MODULE'S - CTRL.DRV's Shut Down screen
; (kernel/shutdown.inc, `.modu`) - which kern_mega runs in protected mode,
; where CS is a read-only code alias: that copy stores its state through the
; image's DATA segment (CSDO, PUSH_CSDATA: kernel/kernel.asm), byte for byte
; the plain copy off kern_mega. It took ~99 completed #GPs a second while the
; screen spun (KERN-MEGA-PLAN 14.150). The boot's copies (splash.inc, in
; `.boot2`) run in real mode, before mega_enter, and stay plain. The section
; decides, so no instance can forget to ask.
; The caller supplies prefix_l_{cx,spn,spx,spp}, row buffers and cosine table.
; FROM spl_prow DOWN, DS = CS: spl_spin points it there before its first call
; and nothing else calls them, so they reach the CS state with no `cs:` - 19
; bytes a copy (kernel size pass 18; the boot blob's copy is the one counted).
%macro OS88_SPINNER 2
%ifidni __?SECT?__, [section .boot2]
 %assign OS88SPN_MOD 0
%else
 %assign OS88SPN_MOD 1
%endif
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
 %if OS88SPN_MOD
    CSDO mov [CSEG %1 %+ _cos], ax
 %else
    mov [cs:%1 %+ _cos], ax
 %endif
%endif

%if OS88SPN_MOD
    mov ax, [vid_rowadd]        ; (the module's: CSDO, as above)
    CSDO mov [CSEG %1 %+ _radd], ax
    mov ax, [vid_wrapbit]
    CSDO mov [CSEG %1 %+ _rwbit], ax
    mov ax, [vid_wrapfix]
    CSDO mov [CSEG %1 %+ _rwfix], ax
%else
    mov ax, [vid_rowadd]        ; the row walk's three words, banked into CS
    mov [cs:%1 %+ _radd], ax       ; because everything below runs with DS = CS
    mov ax, [vid_wrapbit]
    mov [cs:%1 %+ _rwbit], ax
    mov ax, [vid_wrapfix]
    mov [cs:%1 %+ _rwfix], ax
%endif
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
%if OS88SPN_MOD
 %ifdef KERN_MEGA
    PUSH_CSDATA                 ; ...the image's DATA segment on kern_mega,
    pop ds                      ; whose CS cannot be written through
 %else
    mov ax, cs
    mov ds, ax
 %endif
%else
    mov ax, cs
    mov ds, ax                  ; ...so the row copy is one `rep movsb`
%endif

    xor al, al                  ; ...the plain row: verticals only
    mov bx, %1 %+ _rowa
    call %1 %+ _rowmk
    mov al, 0x0F                ; ...and every digit's bar, for rows 0 and 40
    call %1 %+ _mkput              ; row 0
    call %1 %+ _putplain           ; rows 1..19
    mov al, 0x0D                ; row 20 is the MIDDLE bar, and the second
    call %1 %+ _mkput              ; digit is a 0 - SPL_DIG0 has no middle
    call %1 %+ _putplain           ; rows 21..39
    mov al, 0x0F
    call %1 %+ _mkput              ; row 40
    pop ds
    ret

; -----------------------------------------------------------------------------
; spl_putplain - 19 rows of [spl_rowa].  spl_mkput - build [spl_rowb] with the
; bars in AL (spl_rowmk) and put it, one row.  spl_putrow - one row of
; [spl_rowb]. All take ES:DI at the row's first byte and leave it at the next
; row's. clobbers: ax, cx, si (mkput: rowmk's too)
;
; THE THREE SEQUENCES spl_spin SPELLED OUT ARE THESE TWO ENTRIES (kernel size
; pass 18, -14 a copy): each bar row was `mov bx, rowb / call rowmk / call
; putrow` and each plain run `mov cx, 19 / call putplain`. It is the blob's
; copy that pays - kern_big's boot blob is full to the byte - and the
; shutdown's in CTRL.DRV comes with it.
; -----------------------------------------------------------------------------
%1 %+ _putplain:
    mov cx, 19
%%plain:
    mov si, %1 %+ _rowa
    call %1 %+ _prow
    loop %%plain
    ret
%1 %+ _mkput:
    mov bx, %1 %+ _rowb
    call %1 %+ _rowmk           ; (BX and DI it keeps) - and falls into:
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
    add di, [%1 %+ _radd]
    test di, [%1 %+ _rwbit]
    jz .nextrow
    add di, [%1 %+ _rwfix]
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
    mov [%1 %+ _rbar], al       ; spent below and spl_spin rebuilds the bar
    mov di, bx                  ; row twice DURING the walk down the box, so
    mov cx, SPL_SPINB           ; forgetting this drew the whole spinner into
    xor al, al                  ; whatever the zero loop left behind - which
.z:                             ; is a box of nothing, measured
    mov [di], al             ; (the buffer is CS's, so no rep stosb: that
    inc di                      ; would address ES, the framebuffer)
    loop .z

    mov byte [%1 %+ _rdig], 0
.digit:
    mov al, 28
    mul byte [%1 %+ _rdig]      ; AL = the digit's box-relative x
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
    mov cl, [%1 %+ _rdig]
    mov ch, 1
    shl ch, cl                  ; CH = this digit's bit
    test [%1 %+ _rbar], ch
    jnz .bar
    push dx                     ; no bar: the two verticals, a pixel each
    mov dx, ax
    call %1 %+ _span
    pop dx
    mov ax, dx
.bar:
    call %1 %+ _span
    inc byte [%1 %+ _rdig]
    cmp byte [%1 %+ _rdig], 4
    jb .digit
    pop di
    ret

; -----------------------------------------------------------------------------
; spl_span - OR the run of screen x from AX to DX (AX <= DX) into the buffer at
;            CS:BX.  clobbers: ax, cx, dx, si
; -----------------------------------------------------------------------------
%1 %+ _span:
    sub ax, [%1 %+ _l_spp]      ; box-relative, in pixels
    sub dx, [%1 %+ _l_spp]
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
    or [bx+si], al
    ret
.multi:
    or [bx+si], al
    inc si
.mid:
    cmp si, dx
    jae .last
    mov byte [bx+si], 0xFF
    inc si
    jmp short .mid
.last:
    or [bx+si], ah
    ret

; -----------------------------------------------------------------------------
; spl_xform - coin-flip a spinner x: AL = box-relative x (0..104) ->
;             AX = [spl_l_cx] + (AL - SPL_HALFW) * cos / 64. clobbers: nothing else
; -----------------------------------------------------------------------------
%1 %+ _xform:
    push cx
    push dx
    xor ah, ah
    sub ax, SPL_HALFW
    imul word [%1 %+ _cos]         ; DX:AX = rel * cos; |result| <= 3328
    mov cl, 6
    sar ax, cl
    add ax, [%1 %+ _l_cx]       ; ...and NOT 320, which is VGA's centre alone
    pop dx
    pop cx
    ret

%endmacro
