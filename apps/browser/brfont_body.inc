; part 7's font body (SPEC.md 71.35) - assembled by brext_part.asm
brf_f_open:
    push ds
    mov ds, [cs:BRCL_SELF]
    call brf_open
    mov al, [brf_ok]                ; AL = 1 the face opened; the part is
    pop ds                          ; good either way (h1 needs no file)
    clc
    retf

brf_f_adv:
    push ds
    push bx
    mov ds, [cs:BRCL_SELF]
    sub al, TY_FIRST
    cmp al, TY_NGLYPH
    jb .in
    xor al, al                      ; outside the face: a space's advance
.in:
    mov bl, al
    xor bh, bh
    mov al, [ty_adv + bx]
    pop bx
    pop ds
    retf

; ES:SI = text, CX = length, DI = the BRFP block in the caller's DS
brf_f_draw:
    push ax
    push cx
    push si
    push di
    push es
    push si
    push cx
    push es
    mov si, di                      ; DS:SI = the caller's block...
    mov es, [cs:BRCL_SELF]
    mov di, brf_par                 ; ...copied into ours
    mov cx, BRFP_SIZE / 2
    cld
    rep movsw
    pop es
    pop cx
    pop si
    push ds
    mov ds, [cs:BRCL_SELF]
    call brf_draw
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop ax
    retf

; --- brf_open - find TIMES.F88 among the system's faces and set it ----------
; out: CF=1 none. AX clobbered
brf_open:
    push bx
    push cx
    push si
    push di
    push es
    call ty_init
    call ty_scan                    ; the families SYSTEM/FONTS holds
    xor bx, bx                      ; BL = the family being looked at
.fam:
    cmp bl, [ty_nfam]
    jae .no
    mov al, bl
    mov ah, TY_NAMSZ
    mul ah
    add ax, ty_fnames
    mov si, ax
    mov di, brf_name
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .next
    inc si
    inc di
    or al, al
    jnz .cmp
    mov al, bl                      ; the family: open it
    call ty_openfam
    jc .no
    xor ah, ah                      ; regular: the only style a face carries
    call ty_use
    jc .no
    mov byte [brf_ok], 1
    clc
    jmp short .out
.next:
    inc bl
    jmp .fam
.no:
    mov byte [brf_ok], 0
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; --- brf_draw - compose the line and put its slice up -----------------------
; in: ES:SI text, CX length, [brf_par] the block. Each letter in its own
; kind: a big one (LM_BIG in its mark) in the face, any other in the line's
; base kind. Clobbers AX, CX, SI, DI
brf_draw:
    push bx
    push dx
    push bp
    mov [brf_left], cx
    mov [brf_tofs], si
    mov [brf_t0], si
    mov [brf_tseg], es
    mov dx, TY_BROWS
    call ty_band                    ; sixteen rows of ground...
    push ds
    pop es
    mov di, ty_bandbuf - 2 * TY_STRIDE
    mov cx, TY_STRIDE               ; ...and the two above them (a word each)
    mov ax, 0FFFFh
    cld
    rep stosw
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL first, AH last
    mov [brf_gseg], dx
    mov [brf_gofs], si
    mov [brf_gfl], ax
    mov bp, [brf_par + BRFP_X0]     ; BP = the pen
.ch:
    cmp word [brf_left], 0
    je .blit
    dec word [brf_left]
    cmp bp, (TY_STRIDE - 3) * 8
    jae .blit                       ; the band's edge: nothing past it
    mov es, [brf_tseg]
    mov bx, [brf_tofs]
    inc word [brf_tofs]
    mov di, [brf_par + BRFP_MOFF]   ; A TABLE CELL'S FIRST LETTER, or a grid
    test byte [es:bx + di], LM_SNAP ; line: the pen snaps to its column, so
    jnz .snap                       ; a narrower face never drifts the next
    mov al, [es:bx]                 ; column over (71.35)
    cmp al, BR_BH
    jb .nosnap
    cmp al, BR_BX
    ja .nosnap
.snap:
    mov bp, bx
    sub bp, [brf_t0]
    shl bp, 1
    shl bp, 1
    shl bp, 1
    add bp, [brf_par + BRFP_X0]
.nosnap:
    mov di, bx                      ; where this letter starts, for the
    sub di, [brf_t0]                ; overlays and the colour runs
    shl di, 1
    mov [brf_px + di], bp
    mov al, [es:bx]                 ; A GRID LINE: down both slices
    cmp al, BR_BV
    jb .notline
    cmp al, BR_BX
    ja .notline
    call brf_vline
    add bp, 8
    jmp .ch
.notline:
    mov al, [brf_par + BRFP_KIND]   ; the line's base kind (a heading's
    cmp al, BRT_CELL                ; wins)...
    jne .k
    mov di, [brf_par + BRFP_MOFF]
    test byte [es:bx + di], LM_BIG
    jz .k
    mov al, BRT_FACE                ; ...or on a cell line, a big letter's
.k:
    cmp al, BRT_FACE
    jne .glyph
    cmp byte [brf_ok], 0
    mov al, BRT_CELL
    je .glyph                       ; no face: the cell
    mov si, bx
    mov cx, 1
    mov ax, bp
    push bp
    call ty_putn                    ; the face...
    xchg ax, bp                     ; BP = the pen after it
    mov di, [brf_par + BRFP_MOFF]
    test byte [es:bx + di], LM_BOLD
    jnz .bold
    cmp byte [brf_par + BRFP_KIND], BRT_FACE
    jne .ul                         ; body text: one strike
.bold:
    inc ax
    call ty_putn                    ; ...and again a pixel right: bold
.ul:
    pop ax                          ; AX = the pen before it
    call brf_ul
    jmp .ch
.glyph:
    mov ah, al
    mov al, [es:bx]
    push bp
    call brf_glyph
    pop ax
    call brf_ul
    jmp .ch
.blit:
    mov ax, [brf_tofs]
    sub ax, [brf_t0]
    mov [brf_n], ax                 ; the letters composed
    mov bx, ax
    shl bx, 1
    mov [brf_px + bx], bp           ; ...and where the last one ended
    call brf_inv                    ; the selection and the focus, inverted
    mov word [brf_rs], 0
    mov ax, [brf_par + BRFP_W]
    mov [brf_re], ax
    cmp byte [brf_par + BRFP_COLR], 0
    je .one
    cmp word [brf_n], 0
    jne .runs
.one:
    mov ax, [brf_par + BRFP_PEN]    ; one pen: the whole row at once
    call brf_blit
    jmp .done
.runs:                              ; A RUN A COLOUR (71.34's attributes):
    xor cx, cx                      ; one blit each, its edges on a byte
.run:
    mov es, [brf_tseg]
    mov bx, [brf_t0]
    add bx, cx
    mov di, [brf_par + BRFP_AOFF]
    mov al, [es:bx + di]            ; AL = this run's attribute
.ext:
    inc cx
    cmp cx, [brf_n]
    jae .last
    inc bx
    cmp [es:bx + di], al
    je .ext
    mov bx, cx
    shl bx, 1
    mov dx, [brf_px + bx]
    and dl, 0F8h                    ; the next run starts on its byte
    jmp short .emit
.last:
    mov dx, [brf_par + BRFP_W]
.emit:
    mov [brf_re], dx
    mov ah, al                      ; the attribute -> AL ink, AH ground
    and al, 0Fh
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    call brf_blit
    mov [brf_rs], dx
    cmp cx, [brf_n]
    jb .run
.done:
    pop bp
    pop dx
    pop bx
    ret

; --- brf_blit - the slice's pixels [brf_rs, brf_re) in pen AX (AL ink, AH
; ground). Clobbers AX, BX, SI, BP, ES; CX and DX kept
brf_blit:
    push cx
    push dx
    mov cx, [brf_re]
    cmp cx, TY_STRIDE * 8
    jbe .w
    mov cx, TY_STRIDE * 8           ; never past the band's own row
.w:
    sub cx, [brf_rs]
    jbe .out
    xchg al, ah                     ; a SET bit is ground here (os88type's
    call OSAPI_GFX_BLIT1_PEN        ; polarity), so the pen goes in swapped;
    mov si, ty_bandbuf - 2 * TY_STRIDE ; a 1bpp adapter reads no pen and a
    cmp byte [brf_par + BRFP_SLICE], 0 ; set bit is lit there, the same
    je .top
    add si, 8 * TY_STRIDE
.top:
    mov ax, [brf_rs]
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add si, bx
    add ax, [brf_par + BRFP_X]
    mov bx, [brf_par + BRFP_Y]
    push ds
    pop es
    mov bp, TY_STRIDE
    mov dx, 8
    call OSAPI_GFX_BLIT1
.out:
    pop dx
    pop cx
    ret

; --- brf_inv - every selected or focused letter's pixels inverted, all
; sixteen rows. Clobbers AX, BX, CX, DX, SI, DI, ES
brf_inv:
    mov es, [brf_tseg]
    xor si, si                      ; SI = the letter
.l:
    cmp si, [brf_n]
    jae .ret
    mov bx, [brf_t0]
    add bx, si
    mov di, [brf_par + BRFP_MOFF]
    test byte [es:bx + di], LM_SELECT | LM_FOCUS
    jz .n
    mov bx, si
    shl bx, 1
    mov ax, [brf_px + bx]           ; AX..DX = its pixels
    mov dx, [brf_px + bx + 2]
.p:
    cmp ax, dx
    jae .n
    mov bx, ax
    mov cl, 3
    shr bx, cl
    mov cl, al
    and cl, 7
    mov ch, 80h
    shr ch, cl                      ; CH = the pixel's bit
    add bx, ty_bandbuf - 2 * TY_STRIDE
    mov cl, 16
.r:
    xor [bx], ch
    add bx, TY_STRIDE
    dec cl
    jnz .r
    inc ax
    jmp .p
.n:
    inc si
    jmp .l
.ret:
    ret

; --- brf_glyph - AL = a character in the 8x8 cell, AH = BRT_CELL or BRT_X2,
; at pen BP (any pixel) -> BP past it. Visual rows: the cell at 5..12 (its
; baseline on the face's, row 11), the doubled cell at 0..15
brf_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp al, [brf_gfl + 1]
    ja .sp
    sub al, [brf_gfl]
    jnc .gi
.sp:
    xor al, al
.gi:
    mov [brf_cls], ah
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [brf_gofs]
    mov si, ax
    mov es, [brf_gseg]
    mov ax, bp
    and al, 7
    mov [brf_sh], al
    mov di, bp
    shr di, 1
    shr di, 1
    shr di, 1
    add di, ty_bandbuf - 2 * TY_STRIDE
    mov cx, 8
    cmp byte [brf_cls], BRT_X2
    je .x2
    add di, 5 * TY_STRIDE
.c1:
    mov dh, [es:si]
    inc si
    xor dl, dl
    call brf_put16
    add di, TY_STRIDE
    loop .c1
    add bp, 8
    jmp short .out
.x2:
    mov al, [es:si]
    inc si
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov dh, [brf_xt + bx]           ; the high nibble, doubled
    mov bl, al
    and bl, 0Fh
    mov dl, [brf_xt + bx]           ; ...and the low
    mov ax, dx
    shr ax, 1
    or dx, ax                       ; bold: a second strike a pixel right
    call brf_put16
    add di, TY_STRIDE
    call brf_put16
    add di, TY_STRIDE
    loop .x2
    add bp, BRT_X2ADV
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; brf_vline - a grid's vertical at pen BP, all sixteen rows: br_bglyph's
; bevel pair - solid in column 6, a dither in column 1. All kept
brf_vline:
    push ax
    push bx
    push cx
    push dx
    mov bx, bp
    add bx, 6
    call brf_vbit                   ; AL = its mask, BX = its byte
    mov dx, bx
    mov ah, al
    mov bx, bp
    inc bx
    call brf_vbit                   ; AL / BX: column 1's
    mov cx, 16
.r:
    xchg bx, dx
    and [bx], ah                    ; dark, every row
    xchg bx, dx
    test cl, 1
    jz .skip
    and [bx], al                    ; light, every other
.skip:
    add bx, TY_STRIDE
    add dx, TY_STRIDE
    loop .r
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; brf_vbit - BX = a pixel x -> BX = its byte at visual row 0, AL = the mask
; that clears it. CX kept
brf_vbit:
    push cx
    mov cl, bl
    and cl, 7
    mov al, 80h
    shr al, cl
    not al
    mov cl, 3
    shr bx, cl
    add bx, ty_bandbuf - 2 * TY_STRIDE
    pop cx
    ret

; brf_ul - ES:BX's letter, from pen AX to BP: its link's or <u>'s rule on
; visual row 14, under the face's descenders' end. All kept
brf_ul:
    push ax
    push bx
    push cx
    push di
    mov di, [brf_par + BRFP_MOFF]
    mov al, [brf_par + BRFP_ULM]
    test [es:bx + di], al
    jz .out
.p:
    cmp ax, bp
    jae .out
    mov bx, ax
    mov cl, 3
    shr bx, cl
    mov cl, al
    and cl, 7
    mov ch, 80h
    shr ch, cl
    not ch
    and [ty_bandbuf + 12 * TY_STRIDE + bx], ch
    inc ax
    jmp .p
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; brf_put16 - DX = 16 pixels of ink, shifted right [brf_sh], into DS:DI's
; three bytes (ink CLEARS: os88type's polarity). All kept
brf_put16:
    push ax
    push bx
    push cx
    mov ax, dx
    xor bl, bl
    mov cl, [brf_sh]
    or cl, cl
    jz .w
.s:
    shr ax, 1
    rcr bl, 1
    dec cl
    jnz .s
.w:
    not ax
    not bl
    and [di], ah
    and [di + 1], al
    and [di + 2], bl
    pop cx
    pop bx
    pop ax
    ret

; a nibble's four bits, each doubled
brf_xt:
    db 00h, 03h, 0Ch, 0Fh, 30h, 33h, 3Ch, 3Fh
    db 0C0h, 0C3h, 0CCh, 0CFh, 0F0h, 0F3h, 0FCh, 0FFh

brf_name:   db 'TIMES.F88', 0

%define OS88TY_NOHIT
%define OS88TY_NOWIDTH
%define OS88TY_NOWIDTHN
%define OS88TY_NOFIT
%define OS88TY_NOPUT
%define OS88TY_NOADVOF
%define OS88TY_NOFAMN
%define OS88TY_NOGETASC
%define OS88TY_NOGETLEAD
%define OS88TY_NOGRAY
%define OS88TY_FACEBUF brf_facebuf
%include "os88type.inc"

; --- brf_facebuf - os88type's hook: the face in THIS part's memory ----------
; The Browser holds eight claims by the time a page has a heading - the cap a
; package has (SPEC.md 50) - so a claim for the face is refused with 355KB
; free. Real mode only: under kern_mega a segment is a selector, its sum is
; nothing, and the cap there is twelve, so the face claims as usual.
; in: AX = KB; out: CF=0 DX = the buffer's segment, CF=1 claim. All else kept
BRF_FBKB    equ 2                   ; a face is 1,498-1,688 bytes (6.4.1)
brf_facebuf:
    cmp ax, BRF_FBKB
    ja .no
    push ax
    call OSAPI_CPU_INFO
    test ah, CPU_F_PM
    pop ax
    jnz .no
    push ax
    push cx
    mov dx, ds
    mov ax, brf_fbuf + 15
    mov cl, 4
    shr ax, cl
    add dx, ax                      ; the buffer's first whole paragraph...
    add dx, 31
    and dx, 0FFE0h                  ; ...up to 32 of them: 512 bytes, linear
    pop cx
    pop ax
    clc
    ret
.no:
    stc
    ret

brf_ok:     db 0
brf_tseg:   dw 0
brf_tofs:   dw 0
brf_left:   dw 0
brf_gseg:   dw 0
brf_gofs:   dw 0
brf_gfl:    dw 0
brf_cls:    db 0
brf_t0:     dw 0
brf_n:      dw 0
brf_rs:     dw 0
brf_re:     dw 0
brf_px:     times 100 dw 0          ; each letter's first pixel, and the end
brf_sh:     db 0
brf_par:    times BRFP_SIZE db 0
            times 2 * TY_STRIDE db 0FFh ; ground above the band (a face line)
brf_bss:    times TY_BSS_SIZE db 0
brf_fbuf:   times BRF_FBKB * 1024 + 512 + 16 db 0 ; the face (brf_facebuf)
    TY_BSS brf_bss
