; =============================================================================
; os8088 - apps/pagedesk/pdkit.inc    THE FACE'S COMPOSER (PAGEDESK-SPEC 20.21.3)
;
; Every caption, picture and line of panel text PageDesk's face draws is ORed
; into ONE 1bpp band and put down with ONE OSAPI_GFX_BLIT1 under
; OSAPI_GFX_BLIT1_PEN - the retro-face skill's field, from C. A button's inside
; is then one drawing call whatever it holds, the ground and the ink go down
; in the same pass (nothing is written twice, CLAUDE.md), and the band is
; always the field's width, so a shorter caption erases a longer one.
;
; The kit (fonts and pictures, PDRES kind 9) and the band both live in the
; PDRES INDEX claim (pdumod.c's pdu_bank_load). Without the kit every font is
; the kernel's 8x8 table and a picture draws nothing - a complete face.
;
; cdecl as pdui.inc: arguments at [bp+4]..; AX the answer; BP, DS, ES, SS:SP
; and DF kept; AX BX CX DX SI DI the caller's to lose.
; =============================================================================

PK_STRIDE   equ 96                  ; the band's widest row, bytes (768 px)
PK_ROWS     equ 24                  ; and its tallest (a field is 18, a button 16)
PK_NF       equ 4                   ; fonts: caption, text, title, and 3 =
                                    ; the kernel's 8x8 always (COMPACT)
PK_NKIT     equ 3                   ; ...the first three from the kit

section .text

; -----------------------------------------------------------------------------
; void pk_init(unsigned kitseg, unsigned bandseg) - the kit's segment (0 = no
; kit: the kernel's face) and the band's. Reads the font records once.
; -----------------------------------------------------------------------------
_pk_init:
    push bp
    mov bp, sp
    push es
    mov ax, [bp+6]
    mov [pk_bseg], ax
    mov ax, [bp+4]
    mov [pk_kseg], ax
    or ax, ax
    jz .kernel
    mov es, ax
    cmp word [es:0], 'PD'
    jne .kernel0
    cmp word [es:2], 'UK'
    jne .kernel0
    cmp word [es:6], PK_NKIT        ; the C was built for these three
    jb .kernel0
    mov ax, [es:8]
    mov [pk_nicon], ax
    mov si, 10                      ; the font records
    xor bx, bx
.rec:
    mov al, [es:si]                 ; advance
    mov [pk_fadv + bx], al
    mov al, [es:si+1]               ; rows
    mov [pk_frows + bx], al
    mov ax, [es:si+4]               ; offset in the kit
    shl bx, 1
    mov [pk_foff + bx], ax
    mov ax, [pk_kseg]
    mov [pk_fseg + bx], ax
    shr bx, 1
    add si, 8
    inc bx
    cmp bx, PK_NKIT
    jb .rec
    mov ax, [es:6]                  ; the icons follow every font record
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, 10
    mov [pk_ioff], ax
    mov byte [pk_kit], 1
    mov bx, PK_NKIT                 ; font 3 is the kernel's either way
    jmp short .kern
.kernel0:
    mov word [pk_kseg], 0
.kernel:
    mov byte [pk_kit], 0
    mov word [pk_nicon], 0
    xor bx, bx                      ; every font is the kernel's 8x8 table
.kern:
    push si
    push bx
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL first, AH last
    pop bx
    mov [pk_kfirst], ax
.kf:
    mov byte [pk_fadv + bx], 8
    mov byte [pk_frows + bx], 8
    shl bx, 1
    mov [pk_foff + bx], si
    mov [pk_fseg + bx], dx
    shr bx, 1
    inc bx
    cmp bx, PK_NF
    jb .kf
    pop si
.out:
    pop es
    pop bp
    ret

; int pk_adv(int f) / int pk_rows(int f) / int pk_haskit(void)
_pk_adv:
    mov bx, sp
    mov bx, [ss:bx+2]               ; SS != DS (CLAUDE.md, near model)
    mov al, [pk_fadv + bx]
    xor ah, ah
    ret
_pk_rows:
    mov bx, sp
    mov bx, [ss:bx+2]
    mov al, [pk_frows + bx]
    xor ah, ah
    ret
_pk_haskit:
    mov al, [pk_kit]
    xor ah, ah
    ret

; -----------------------------------------------------------------------------
; int pk_begin(int w, int h, int x) - a clear band w x h for a field whose left
; edge goes at screen x: the band starts on the byte column HOLDING x and is
; composed (x & 7) pixels in (SPEC.md 5.4.2.8). 0 = refused (too big, or no
; band), 1 = ready.
; -----------------------------------------------------------------------------
_pk_begin:
    push bp
    mov bp, sp
    push es
    xor ax, ax
    mov [pk_h], ax                  ; refused until it is not
    cmp [pk_bseg], ax
    je .no
    mov cx, [bp+6]                  ; h
    or cx, cx
    jle .no
    cmp cx, PK_ROWS
    ja .no
    mov bx, [bp+8]
    and bx, 7
    mov [pk_ph], bx
    mov dx, [bp+4]                  ; w
    or dx, dx
    jle .no
    add bx, dx
    add bx, 7
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; the stride
    cmp bx, PK_STRIDE
    ja .no
    mov [pk_st], bx
    mov [pk_w], dx
    mov [pk_h], cx
    mov ax, bx
    mul cl                          ; bytes
    mov cx, ax
    mov es, [pk_bseg]
    xor di, di
    xor ax, ax
    cld
    rep stosb
    mov ax, 1
.no:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; pk_fetch - copy CX bytes from AX:SI into pk_gbuf, so the OR loop below runs
; with DS = the package (its bounds are ordinary words) whatever segment the
; glyph table is in: the kit's claim or the kernel's table. Keeps ES.
; -----------------------------------------------------------------------------
pk_fetch:
    push es
    push ds
    pop es
    mov di, pk_gbuf
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    pop es
    ret

; -----------------------------------------------------------------------------
; pk_or - OR a column of source bytes into the band.
;   in SI -> the first source byte (in pk_gbuf), BX the source step, CX = bit
;   x in the band (phase added), DX = band row (0..), AH = rows; ES = the band.
;   Clips to the band's width and rows. Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
pk_or:
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1                       ; the byte column
    cmp di, [pk_st]
    jae .ret
    mov dh, 0                       ; 1 = the byte right of it is in the row
    inc di
    cmp di, [pk_st]
    jae .one
    mov dh, 1
.one:
    dec di
    and cl, 7
    push ax
    mov al, dl
    mul byte [pk_st]                ; AX = row * stride (DX untouched)
    add di, ax
    pop ax
    mov ch, ah
.row:
    cmp dl, [pk_h]
    jae .ret
    mov al, [si]
    add si, bx
    mov ah, al
    xor al, al
    shr ax, cl
    or [es:di], ah
    or dh, dh
    jz .s
    or [es:di+1], al
.s:
    add di, [pk_st]
    inc dl
    dec ch
    jnz .row
.ret:
    ret

; -----------------------------------------------------------------------------
; int pk_text(int x, int y, const char *s, int f) - font f's glyphs ORed in
; at band x, y; answers the x after the last.
; -----------------------------------------------------------------------------
_pk_text:
    push bp
    mov bp, sp
    push es
    mov es, [pk_bseg]
    mov bx, [bp+10]                 ; f
    cmp bx, PK_NF
    jb .f
    xor bx, bx
.f:
    mov [pk_cf], bx
    mov si, [bp+8]                  ; s
.ch:
    mov al, [si]
    or al, al
    jz .done
    inc si
    push si
    call pk_gidx                    ; AX = the glyph's index, CF = none
    jc .next
    mov bx, [pk_cf]
    mov cl, [pk_frows + bx]
    mul cl                          ; AX = index * rows
    shl bx, 1
    add ax, [pk_foff + bx]
    mov si, ax
    mov ax, [pk_fseg + bx]
    shr bx, 1
    xor ch, ch
    mov cl, [pk_frows + bx]
    call pk_fetch
    mov dx, [bp+6]
    or dx, dx
    js .next
    mov cx, [pk_ph]
    add cx, [bp+4]
    js .next
    mov ah, [pk_frows + bx]
    mov si, pk_gbuf
    mov bx, 1
    call pk_or
.next:
    pop si
    mov bx, [pk_cf]
    mov al, [pk_fadv + bx]
    xor ah, ah
    add [bp+4], ax
    jmp short .ch
.done:
    mov ax, [bp+4]
    pop es
    pop bp
    ret

; AL = a character -> AX = its glyph index in the current font, CF = 1 none
pk_gidx:
    cmp byte [pk_kit], 0
    je .kern
    cmp word [pk_cf], PK_NKIT       ; font 3 is the kernel's
    jae .kern
    cmp al, 32
    jb .mark
    cmp al, 126
    ja .mark
    sub al, 32
    xor ah, ah
    clc
    ret
.mark:
    mov ah, 95
    push si
    mov si, pk_marks
.m:
    cmp byte [si], 0
    je .q
    cmp al, [si]
    je .hit
    inc si
    inc ah
    jmp short .m
.hit:
    pop si
    mov al, ah
    xor ah, ah
    clc
    ret
.q:
    pop si
    mov ax, '?' - 32
    clc
    ret
.kern:
    cmp al, 9Eh                     ; the close box's cross: the kernel's
    jne .k1                         ; face has none (CP437 9Eh is a peseta)
    mov al, 'x'
.k1:
    mov ah, [pk_kfirst]
    cmp al, ah
    jb .none
    cmp al, [pk_kfirst + 1]
    ja .none
    sub al, ah
    xor ah, ah
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; void pk_icon(int x, int y, int k) - kit picture k (16x16), ORed in.
; -----------------------------------------------------------------------------
_pk_icon:
    push bp
    mov bp, sp
    push es
    mov ax, [bp+8]
    cmp ax, [pk_nicon]
    jae .out
    mov cl, 5
    shl ax, cl
    add ax, [pk_ioff]
    mov si, ax
    mov ax, [pk_kseg]
    mov cx, 32
    call pk_fetch
    mov es, [pk_bseg]
    mov dx, [bp+6]
    or dx, dx
    js .out
    mov cx, [bp+4]
    add cx, [pk_ph]
    js .out
    push cx
    push dx
    mov si, pk_gbuf                 ; the left half: every other byte
    mov bx, 2
    mov ah, 16
    call pk_or
    pop dx
    pop cx
    add cx, 8
    mov si, pk_gbuf + 1             ; the right half
    mov bx, 2
    mov ah, 16
    call pk_or
.out:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; void pk_hline(int x1, int x2, int y) - a run of ink in band row y
; -----------------------------------------------------------------------------
_pk_hline:
    push bp
    mov bp, sp
    push es
    mov es, [pk_bseg]
    mov dx, [bp+8]
    cmp dx, [pk_h]
    jae .out
    mov al, dl
    mul byte [pk_st]
    mov di, ax
    mov si, [bp+4]
.px:
    cmp si, [bp+6]
    jg .out
    cmp si, [pk_w]
    jae .out
    mov cx, si
    add cx, [pk_ph]
    mov bx, cx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and cl, 7
    mov al, 80h
    shr al, cl
    or [es:di + bx], al
    inc si
    jmp short .px
.out:
    pop es
    pop bp
    ret

; -----------------------------------------------------------------------------
; int pk_pen(int y, int ink, int paper, int grey) - make the band ready for
; the C to put down at screen row y with os88_gfx_blit1_seg(pk_bseg, 0,
; pk_st, x, y, pk_w, pk_h), and pk_pen0() after it. On a 1bpp display
; (pk_mono) the pen is not read and a set bit is LIT: a band whose ink is
; not white is inverted here, and a greyed one (grey != 0) has its ink ANDed with
; the screen's checkerboard (SPEC.md 39.4) before that; on colour the pen is
; set to ink on paper (an inline cell). Answers 1 ready, 0 nothing begun.
;
; The blit itself is the C's, through the SDK's thunk, so it is charged on
; the UI stack the way every other drawing call the primary makes is
; (tools/os88pdstk.py), and the thunk is the one the thumbnails already use.
; -----------------------------------------------------------------------------
_pk_pen:
    push bp
    mov bp, sp
    push es
    xor ax, ax
    cmp [pk_h], ax
    je .no
    mov es, [pk_bseg]
    cmp byte [pk_mono], 0
    je .pen
    ; --- 1bpp: grey, then black ink -> inverted --------------------------
    cmp word [bp+10], 0
    je .inv
    xor di, di
    xor dx, dx                      ; row
.grow:
    mov bx, [bp+4]
    add bx, dx                      ; the screen row
    mov al, 0AAh                    ; band bit 0 is an even screen x
    test bl, 1
    jz .g
    mov al, 55h
.g:
    mov cx, [pk_st]
.gb:
    and [es:di], al
    inc di
    loop .gb
    inc dx
    cmp dx, [pk_h]
    jb .grow
.inv:
    cmp word [bp+6], CWHITE         ; ink white is the lit bit as it is;
    je .pen                         ; any other ink is black on 1bpp (a
                                    ; greyed caption's DGRAY too, 39.4)
    mov ax, [pk_st]
    mul byte [pk_h]
    mov cx, ax
    xor di, di
.ib:
    not byte [es:di]
    inc di
    loop .ib
.pen:
    mov al, [bp+6]
    mov ah, [bp+8]
    call OSAPI_GFX_BLIT1_PEN
    mov ax, 1
.no:
    pop es
    pop bp
    ret

; void pk_pen0(void) - the pen back to its default, white on black, which
; the thumbnails' and the page's own BLIT1 bands are drawn with
_pk_pen0:
    mov ax, (CBLACK << 8) | CWHITE
    call OSAPI_GFX_BLIT1_PEN        ; a far call into an inline cell
    ret

; void pk_mode(int mono) - the window's display is 1bpp (pdudraw.c's pdu_colors)
_pk_mode:
    mov bx, sp
    mov al, [ss:bx+2]
    mov [pk_mono], al
    ret

section .data
pk_marks    db 10h, 11h, 1Fh, 0FAh, 9Eh, 0  ; glyphs 95.. (os88pdkit.py MARKS)

section .bss
pk_kseg     resw 1                  ; the kit, 0 = none
_pk_bseg:
pk_bseg     resw 1                  ; the band (pk_pen's C reads it)
pk_kit      resb 1
pk_mono     resb 1
pk_nicon    resw 1
pk_ioff     resw 1
pk_kfirst   resw 1                  ; the kernel table's first and last codes
pk_fadv     resb PK_NF
pk_frows    resb PK_NF
alignb 2
pk_foff     resw PK_NF
pk_fseg     resw PK_NF
pk_cf       resw 1
pk_gbuf     resb 32                 ; one glyph or one picture, fetched
_pk_w:
pk_w        resw 1                  ; the band: width, rows, phase, stride
_pk_h:
pk_h        resw 1
pk_ph       resw 1
_pk_st:
pk_st       resw 1

section .text
