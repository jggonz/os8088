; =============================================================================
; apps/deskclok/dkface.inc - THE NINE FACES (SPEC.md 110.14.3), as PROGRAMS
;
; A face is a few dozen bytes of ops interpreted by dk_face, not a drawing
; routine: every coordinate is a fraction of the face - a radius of the dial
; (/256), an angle in 3-degree steps (0 = 12 o'clock, 30 = 3), or a share of
; the face's width and height - so one program draws the face at every size,
; in a gallery tile, and on every adapter, and a circle stays round on CGA's
; tall pixels because every row is scaled by [dk_ys], the screen's aspect.
;
; dk_face draws the program SI into the composer at [ck_ox], [ck_oy] for a
; face [dk_fw] x [dk_fh], the time in dk_hh.. as dk_now left it.
; =============================================================================


; the ops
OP_END      equ 0
OP_INK      equ 1               ; lc
OP_ELL      equ 2               ; f - the dial, centred, radius f (/256)
OP_TICKS    equ 3               ; n, f1, f2, width f, flags (DKT_*)
OP_NUMS     equ 4               ; f, lc - 12, 3, 6 and 9 at radius f
OP_TRI      equ 5               ; a1, f1, a2, f2, a3, f3
OP_DOT      equ 6               ; a, f, size f
OP_POLY     equ 7               ; n, width f, then n x (a, f)
OP_SQ       equ 8               ; a, f, half-size f
OP_HANDS    equ 9               ; hour lc, minute lc, second lc, their lengths
                                ; (f), the hour's and minute's widths (f)
OP_CAP      equ 10              ; lc, f
OP_RRECT    equ 11              ; x, y, w, h (/256 of the face), inset px,
                                ; corner px (at a 128-px face)
OP_TEXTC    equ 12              ; x, y, the string - centred on x
OP_DATE     equ 13              ; x, y - `MO 8-24`
OP_HLINE    equ 14              ; x0, x1, y
OP_TIME     equ 15              ; x, y, w, h, style, lit, dim (0FFh none), aux
OP_DYN      equ 16              ; what follows changes with the time: the
                                ; face cache (dkwid.inc) holds what comes
                                ; before it, and a partial redraw draws only
                                ; what comes after (SPEC.md 110.14.4)
DKT_SKIP5   equ 1               ; OP_TICKS: no tick where an hour's is
DKT_SKIPQ   equ 2               ; ...nor at 12, 3, 6 and 9
DKT_BIG     equ 4               ; ...and only on a face 120 px or wider
DKS_SEG     equ 0               ; OP_TIME's styles: seven segments,
DKS_GLYPH   equ 1               ; ...bitmap digits,
DKS_FLIP    equ 2               ; ...split-flap panels

; dk_faces: the gallery - name, kind, program
; dk_fhr: each face's height, /256 of its width (a dial's is the screen's
; aspect, 0 here): the mockup's proportions
dk_fhr:     db 0, 0, 0, 136, 168, 168, 128, 128, 120

dk_faces:                       ; the names are the Face menu's items
    dw dk_t_f0 + 2, dkp_classic     ; (deskclok.asm's DK_ITEM)
    dw dk_t_f1 + 2, dkp_memphis
    dw dk_t_f2 + 2, dkp_neon
    dw dk_t_f3 + 2, dkp_mono
    dw dk_t_f4 + 2, dkp_lcd
    dw dk_t_f5 + 2, dkp_vga
    dw dk_t_f6 + 2, dkp_red
    dw dk_t_f7 + 2, dkp_amber
    dw dk_t_f8 + 2, dkp_flip

    DK_PARITY 1                 ; the programs and the ops: their measured
                                ; parity (deskclok.asm's DK_PARITY)
dk_strs:    dw dks_casio
dks_casio:  db 'CASIO', 0

; --- 1. CLASSIC: a white dial, black hands, ticks and 12/3/6/9 --------------
dkp_classic:
    db OP_INK, LC_BLK,  OP_ELL, 255
    db OP_INK, LC_WHT,  OP_ELL, 244
    db OP_INK, LC_BLK
    db OP_TICKS, 60, 226, 238, 2, DKT_SKIP5 | DKT_BIG
    db OP_TICKS, 12, 214, 238, 8, 0
    db OP_NUMS, 166, LC_BLK
    db OP_DYN
    db OP_HANDS, LC_BLK, LC_BLK, LC_RED, 132, 196, 206, 12, 7
    db OP_CAP, LC_BLK, 14
    db OP_END
; --- 2. MEMPHIS '80: cyan, magenta and yellow shapes, a zigzag ---------------
dkp_memphis:
    db OP_INK, LC_BLK,  OP_ELL, 255
    db OP_INK, LC_WHT,  OP_ELL, 243
    db OP_INK, LC_MAGB
    db OP_POLY, 5, 9,  95, 150,  99, 112,  103, 152,  107, 114,  111, 150
    db OP_INK, LC_CYND
    db OP_TRI, 10, 196,  20, 196,  16, 120
    db OP_INK, LC_YLWD
    db OP_TRI, 38, 104,  50, 196,  52, 110
    db OP_INK, LC_MAGB
    db OP_DOT, 82, 120, 30
    db OP_INK, LC_RED
    db OP_SQ, 0, 232, 6
    db OP_INK, LC_BLK
    db OP_TICKS, 12, 216, 238, 6, DKT_SKIPQ
    db OP_NUMS, 166, LC_BLK
    db OP_DYN
    db OP_HANDS, LC_BLK, LC_BLK, LC_MAGB, 132, 196, 206, 12, 7
    db OP_CAP, LC_BLK, 16
    db OP_END
; --- 3. NEON '90: a navy dial, cyan and magenta markers ----------------------
dkp_neon:
    db OP_INK, LC_BLK,  OP_ELL, 255
    db OP_INK, LC_NAVY, OP_ELL, 246
    db OP_INK, LC_CYNW
    db OP_TICKS, 12, 200, 232, 9, DKT_SKIPQ
    db OP_INK, LC_MAGW
    db OP_TRI, 116, 240,  4, 240,  0, 204
    db OP_SQ, 30, 226, 8
    db OP_SQ, 60, 226, 8
    db OP_SQ, 90, 226, 8
    db OP_NUMS, 166, LC_CYNW
    db OP_DYN
    db OP_HANDS, LC_CYNW, LC_MAGW, LC_WHT, 132, 200, 206, 12, 7
    db OP_CAP, LC_MAGW, 16
    db OP_END
; --- 4. MONO DIGITAL: big black bitmap digits on white -----------------------
dkp_mono:
    db OP_INK, LC_BLK,  OP_RRECT, 0, 0, 255, 255, 0, 10
    db OP_INK, LC_WHT,  OP_RRECT, 0, 0, 255, 255, 3, 8
    db OP_DYN
    db OP_TIME, 18, 40, 220, 176, DKS_GLYPH, LC_BLK, 0FFh, 0
    db OP_END
; --- 5. CASIO LCD: a dark bezel, green glass, black segments -----------------
dkp_lcd:
    db OP_INK, LC_DGRY, OP_RRECT, 0, 0, 255, 255, 0, 12
    db OP_INK, LC_BLK,  OP_RRECT, 0, 0, 255, 255, 2, 10
    db OP_INK, LC_WHT,  OP_TEXTC, 128, 14, 0
    db OP_INK, LC_LCD,  OP_RRECT, 22, 64, 212, 176, 0, 3
    db OP_INK, LC_BLK,  OP_HLINE, 30, 226, 126
    db OP_DYN
    db OP_INK, LC_BLK,  OP_DATE, 34, 76
    db OP_TIME, 30, 140, 196, 92, DKS_SEG, LC_BLK, 0FFh, 0
    db OP_END
; --- 6. CASIO VGA: navy, a cyan rim, magenta accents, yellow digits ----------
dkp_vga:
    db OP_INK, LC_CYNW, OP_RRECT, 0, 0, 255, 255, 0, 12
    db OP_INK, LC_NAVY, OP_RRECT, 0, 0, 255, 255, 2, 10
    db OP_INK, LC_MAGW, OP_RRECT, 0, 0, 255, 255, 4, 9
    db OP_INK, LC_NAVY, OP_RRECT, 0, 0, 255, 255, 5, 8
    db OP_INK, LC_CYNW, OP_TEXTC, 128, 14, 0
    db OP_INK, LC_MAGW, OP_HLINE, 26, 50, 22
    db OP_HLINE, 206, 230, 22
    db OP_INK, LC_CYNW, OP_HLINE, 30, 226, 126
    db OP_DYN
    db OP_INK, LC_YLWW, OP_DATE, 34, 76
    db OP_TIME, 30, 140, 196, 92, DKS_SEG, LC_YLWW, 0FFh, 0
    db OP_END
; --- 7. RED LED: vivid red segments, the dark ones dim -----------------------
dkp_red:
    db OP_INK, LC_DGRY, OP_RRECT, 0, 0, 255, 255, 0, 10
    db OP_INK, LC_BLK,  OP_RRECT, 0, 0, 255, 255, 2, 8
    db OP_DYN
    db OP_TIME, 20, 44, 216, 168, DKS_SEG, LC_LRED, LC_RDIM, 0
    db OP_END
; --- 8. AMBER LED: amber segments on black ------------------------------------
dkp_amber:
    db OP_INK, LC_DGRY, OP_RRECT, 0, 0, 255, 255, 0, 10
    db OP_INK, LC_BLK,  OP_RRECT, 0, 0, 255, 255, 2, 8
    db OP_DYN
    db OP_TIME, 20, 44, 216, 168, DKS_SEG, LC_AMB, LC_ADIM, 0
    db OP_END
; --- 9. FLIP CLOCK: white numbers on black split-flap panels -----------------
dkp_flip:
    db OP_INK, LC_DGRY, OP_RRECT, 0, 0, 255, 255, 0, 8
    db OP_INK, LC_BLK,  OP_RRECT, 0, 0, 255, 255, 2, 6
    db OP_DYN
    db OP_TIME, 12, 30, 232, 196, DKS_FLIP, LC_WHT, 0FFh, LC_DGRY
    db OP_END

dk_sin:
    dw 0, 13, 27, 40, 53, 66, 79, 92, 104, 116, 128, 139
    dw 150, 161, 171, 181, 190, 199, 207, 215, 222, 228, 234, 239
    dw 243, 247, 250, 253, 255, 256, 256, 256, 255, 253, 250, 247
    dw 243, 239, 234, 228, 222, 215, 207, 199, 190, 181, 171, 161
    dw 150, 139, 128, 116, 104, 92, 79, 66, 53, 40, 27, 13
    dw 0, -13, -27, -40, -53, -66, -79, -92, -104, -116, -128, -139
    dw -150, -161, -171, -181, -190, -199, -207, -215, -222, -228, -234, -239
    dw -243, -247, -250, -253, -255, -256, -256, -256, -255, -253, -250, -247
    dw -243, -239, -234, -228, -222, -215, -207, -199, -190, -181, -171, -161
    dw -150, -139, -128, -116, -104, -92, -79, -66, -53, -40, -27, -13

;       a    b    c    d    e    f    g
dk_seg:     db 3Fh, 06h, 5Bh, 4Fh, 66h, 6Dh, 7Dh, 07h, 7Fh, 6Fh, 0
DKD_BLANK   equ 10              ; a digit that is not lit at all

dk_days:    db 'SUNMONTUEWEDTHUFRISAT'
dk_mons:    db 'JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC'
dk_mdays:   db 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
dk_sak:     db 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4

; -----------------------------------------------------------------------------
; dk_now - the clock into dk_hh .. dk_year and dk_dow (0 = Sunday), and
; [dk_rtc]. Keeps everything
; -----------------------------------------------------------------------------
dk_now:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_CLK_GET
    mov [dk_year], cx
    mov [dk_mon], dh
    mov [dk_day], dl
    mov [dk_hh], bh
    mov [dk_mm], bl
    mov [dk_ss], ah
    mov [dk_rtc], al
    xor bx, bx                  ; the day of the week (Sakamoto's)
    mov bl, dh
    mov al, [dk_sak+bx-1]
    xor ah, ah
    push ax
    cmp dh, 3
    jae .y
    dec cx
.y:
    mov ax, cx                  ; y + y/4 - y/100 + y/400
    mov bx, cx
    shr bx, 1
    shr bx, 1
    add ax, bx
    push ax
    mov ax, cx
    xor dx, dx
    mov bx, 100
    div bx
    mov bx, ax                  ; y / 100
    pop ax
    sub ax, bx
    shr bx, 1
    shr bx, 1
    add ax, bx                  ; + y / 400
    pop bx
    add ax, bx
    mov bl, [dk_day]
    xor bh, bh
    add ax, bx
    xor dx, dx
    mov bx, 7
    div bx
    mov [dk_dow], dl
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_hour - AL = the hour as shown: 0..23, or 1..12 with AH = 1 AM / 2 PM
; under the 12-hour setting (AH = 0 under 24). Keeps the rest
dk_hour:
    mov al, [dk_hh]
    xor ah, ah
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .r
    mov ah, 1
    cmp al, 12
    jb .am
    mov ah, 2
    sub al, 12
.am:
    or al, al
    jnz .r
    mov al, 12
.r:
    ret

; dk_put2 - AL (0..99) as two digits at DI; DI past them. Keeps the rest
dk_put2:
    push ax
    aam                         ; AH = tens, AL = units
    add ax, 3030h
    xchg al, ah
    stosw
    pop ax
    ret

; dk_put1 - AL (0..99) as one or two digits at DI. Keeps the rest
dk_put1:
    cmp al, 10
    jae dk_put2
    push ax
    add al, '0'
    stosb
    pop ax
    ret

; dk_put3 - SI = a 3-letter table, AL = the index: its three letters at DI
dk_put3:
    push ax
    push cx
    push si
    mov ah, 3
    mul ah
    add si, ax
    mov cx, 3
    cld
    rep movsb
    pop si
    pop cx
    pop ax
    ret

; dk_sdate - dk_line = `MON 24 AUG 2026`. Keeps everything
dk_sdate:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, dk_line
    mov al, [dk_dow]
    mov si, dk_days
    call dk_put3
    mov al, ' '
    stosb
    mov al, [dk_day]
    call dk_put1
    mov al, ' '
    stosb
    mov al, [dk_mon]
    dec ax
    mov si, dk_mons
    call dk_put3
    mov al, ' '
    stosb
    mov ax, [dk_year]
    push ax
    mov al, 20
    call dk_put2                ; the century this clock can read: 1980..2099
    pop ax
    sub ax, 2000
    jae .c
    add ax, 100
    mov byte [di-2], '1'
    mov byte [di-1], '9'
.c:
    call dk_put2
    xor al, al
    stosb
    pop es
    pop di
    pop si
    pop ax
    ret

; dk_scasio - dk_line = `MO 8-24`. Keeps everything
dk_scasio:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, dk_line
    mov al, [dk_dow]
    mov si, dk_days
    call dk_put3
    dec di                      ; two letters of three
    mov al, ' '
    stosb
    mov al, [dk_mon]
    call dk_put1
    mov al, '-'
    stosb
    mov al, [dk_day]
    call dk_put1
    xor al, al
    stosb
    pop es
    pop di
    pop si
    pop ax
    ret

; dk_stime - dk_line = `20:58` (24-hour) or `8:58` (12): the hours and the
; minutes, for the info card's readout. Keeps everything
dk_stime:
    push ax
    push di
    push es
    push ds
    pop es
    mov di, dk_line
    call dk_hour
    call dk_put1
    mov al, ':'
    stosb
    mov al, [dk_mm]
    call dk_put2
    xor al, al
    stosb
    pop es
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; the face's geometry: [dk_fw] x [dk_fh]; the dial's centre and radii
; -----------------------------------------------------------------------------
; dk_fgeo - from [dk_fw], [dk_fh]. Keeps everything
dk_fgeo:
    push ax
    mov ax, [dk_fw]
    shr ax, 1
    mov [dk_cx], ax
    mov [dk_R], ax
    mov ax, [dk_fh]
    shr ax, 1
    mov [dk_cy], ax
    mov [dk_Ry], ax
    pop ax
    ret

; dk_rf / dk_ryf - AL = f: AX = the radius f (/256) across / down. Keeps the rest
dk_rf:
    push dx
    xor ah, ah
    inc ax
    mul word [dk_R]
    mov al, ah
    mov ah, dl
    pop dx
    ret
dk_ryf:
    push dx
    xor ah, ah
    inc ax
    mul word [dk_Ry]
    mov al, ah
    mov ah, dl
    pop dx
    ret

; dk_wf - AL = f: CX = a stroke's width, (R x (f + 1) + 128) / 256 - at
; least 1 - and the brush set to it. Keeps the rest
dk_wf:
    push ax
    push dx
    xor ah, ah
    inc ax
    mul word [dk_R]
    add ax, 128
    adc dx, 0
    mov al, ah
    mov ah, dl
    mov cx, ax
    call ck_brush
    pop dx
    pop ax
    ret

; dk_fxf / dk_fyf - AL = f: AX = f/256 of the face's width / height
dk_fxf:
    push dx
    xor ah, ah
    mul word [dk_fw]
    mov al, ah
    mov ah, dl
    pop dx
    ret
dk_fyf:
    push dx
    xor ah, ah
    mul word [dk_fh]
    mov al, ah
    mov ah, dl
    pop dx
    ret

; dk_sy - AX = px across: AX = the rows that look as tall (at least 1)
dk_sy:
    push dx
    mul word [dk_ys]
    add ax, 128
    adc dx, 0
    mov al, ah
    mov ah, dl
    or ax, ax
    jnz .r
    inc ax
.r:
    pop dx
    ret

; dk_pol - AL = an angle (0..119, 3-degree steps clockwise from 12), CX = a
; radius in px across: AX, BX = the point. Keeps the rest
dk_pol:
    push cx
    push dx
    push si
    xor ah, ah
    mov si, ax
    shl si, 1
    push si
    mov ax, cx
    imul word [dk_sin+si]
    add ax, 128
    mov al, ah
    cbw
    add ax, [dk_cx]
    pop si
    push ax
    mov ax, cx                  ; the radius down: scaled by the aspect
    mul word [dk_Ry]
    div word [dk_R]
    mov cx, ax
    add si, 60                  ; cos = sin(a + 30 steps)
    cmp si, 240
    jb .c
    sub si, 240
.c:
    mov ax, cx
    imul word [dk_sin+si]
    add ax, 128
    mov al, ah
    cbw
    neg ax
    add ax, [dk_cy]
    mov bx, ax
    pop ax
    pop si
    pop dx
    pop cx
    ret

; dk_polf - AL = an angle, AH = f: the point at radius f
dk_polf:
    push cx
    push ax
    mov al, ah
    call dk_rf
    mov cx, ax
    pop ax
    call dk_pol
    pop cx
    ret

; -----------------------------------------------------------------------------
; dk_face - SI = a face program: drawn at the origin for [dk_fw] x [dk_fh].
; Keeps everything
; -----------------------------------------------------------------------------
dk_face:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call dk_fgeo
    mov byte [ck_gbold], 0
    cld
    cmp byte [dk_fmode], 2      ; the dynamic part alone: skip to OP_DYN
    jne .op
.skip:
    call dk_opskip
    jc .x
.op:
    lodsb
    or al, al
    jz .x
    cmp al, OP_DYN
    jne .run
    cmp byte [dk_fmode], 1      ; the static part alone ends here
    je .x
    jmp short .op
.run:
    dec al
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dk_ops+bx]
    jmp short .op
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_opskip - SI = a program: SI past its OP_DYN, CF = 1 it has none
dk_opskip:
    push ax
    push bx
.n:
    lodsb
    or al, al
    jz .none
    cmp al, OP_DYN
    je .y
    cmp al, OP_POLY
    jne .f
    lodsb                       ; OP_POLY: n, the width, n points
    xor ah, ah
    shl ax, 1
    inc ax
    add si, ax
    jmp short .n
.f:
    mov bl, al
    xor bh, bh
    mov al, [dk_oplen + bx - 1]
    xor ah, ah
    add si, ax
    jmp short .n
.y:
    clc
    jmp short .x
.none:
    stc
.x:
    pop bx
    pop ax
    ret
; each op's argument bytes, OP_INK .. OP_TIME (OP_POLY's is counted above)
dk_oplen:   db 1, 1, 5, 2, 6, 3, 0, 3, 8, 2, 6, 3, 2, 3, 8
%if $ - dk_oplen != OP_TIME
  %error "dkface: dk_oplen has a length for every op up to OP_TIME"
%endif

dk_ops:     dw dko_ink, dko_ell, dko_ticks, dko_nums, dko_tri, dko_dot
            dw dko_poly, dko_sq, dko_hands, dko_cap, dko_rrect, dko_textc
            dw dko_date, dko_hline, dko_time

dko_ink:
    lodsb
    jmp ck_ink

dko_ell:
    lodsb
    mov dl, al
    call dk_rf
    mov cx, ax
    mov al, dl
    call dk_ryf
    mov dx, ax
    mov ax, [dk_cx]
    mov bx, [dk_cy]
    jmp ck_ell

; OP_TICKS n, f1, f2, width, flags
dko_ticks:
    lodsb
    mov [dk_tn], al
    lodsb
    mov [dk_tf1], al
    lodsb
    mov [dk_tf2], al
    lodsb
    call dk_wf                  ; the width
    lodsb
    mov [dk_tfl], al
    test al, DKT_BIG
    jz .go
    cmp word [dk_fw], 120
    jb .r
.go:
    mov ax, 120
    div byte [dk_tn]
    mov [dk_tst], al            ; the step between ticks
    xor cx, cx                  ; CL = the tick
.t:
    cmp cl, [dk_tn]
    jae .r
    mov al, cl
    mul byte [dk_tst]           ; AL = its angle
    test byte [dk_tfl], DKT_SKIP5
    jz .q
    push ax
    xor ah, ah
    mov bl, 10
    div bl
    or ah, ah
    pop ax
    jz .n
.q:
    test byte [dk_tfl], DKT_SKIPQ
    jz .d
    push ax
    xor ah, ah
    mov bl, 30
    div bl
    or ah, ah
    pop ax
    jz .n
.d:
    push cx
    mov dl, al
    mov ah, [dk_tf1]
    call dk_polf
    push ax
    push bx
    mov al, dl
    mov ah, [dk_tf2]
    call dk_polf
    mov cx, ax
    mov dx, bx
    pop bx
    pop ax
    call ck_line
    pop cx
.n:
    inc cx
    jmp short .t
.r:
    ret

; OP_NUMS f, lc - the four numerals, centred on their places; none on a face
; narrower than 64 px, where they would cover the dial
dko_nums:
    lodsb
    mov [dk_numf], al
    lodsb
    cmp word [dk_fw], 64
    jb .r
    call ck_ink
    mov al, 1
    cmp word [dk_fw], 152
    jb .s
    inc ax
.s:
    mov [ck_gsx], al
    xor ah, ah
    call dk_sy
    mov [ck_gsy], al
    mov byte [ck_gbold], 1
    mov di, dk_numt
.n:
    mov al, [di]
    cmp al, 0FFh
    je .x
    mov ah, [dk_numf]
    call dk_polf                ; AX, BX = its centre
    push si
    lea si, [di+1]
    mov cx, ax
    mov dx, bx
    mov al, [ck_gsy]
    shl al, 1
    shl al, 1                   ; half its height
    xor ah, ah
    sub dx, ax
    call ck_textc
    pop si
.sk:
    inc di
    cmp byte [di], 0
    jne .sk
    inc di
    jmp short .n
.x:
    mov byte [ck_gbold], 0
.r:
    ret
dk_numt:    db 0, '12', 0, 30, '3', 0, 60, '6', 0, 90, '9', 0, 0FFh

; OP_TRI a1, f1, a2, f2, a3, f3
dko_tri:
    mov di, dk_tp
    mov cx, 3
.p:
    lodsw                       ; AL = a, AH = f
    call dk_polf
    mov [di], ax
    mov [di+2], bx
    add di, 4
    loop .p
    jmp ck_tri

; OP_DOT a, f, size
dko_dot:
    lodsw
    call dk_polf
    push ax
    push bx
    lodsb
    mov dl, al
    call dk_rf
    mov cx, ax
    mov al, dl
    call dk_ryf
    mov dx, ax
    pop bx
    pop ax
    jmp ck_ell

; OP_POLY n, width, n x (a, f)
dko_poly:
    lodsb
    mov cl, al
    xor ch, ch
    lodsb
    push cx
    call dk_wf
    pop cx
    lodsw
    call dk_polf
    dec cx
.l:
    push cx
    push ax
    push bx
    lodsw
    call dk_polf
    mov cx, ax
    mov dx, bx
    pop bx
    pop ax
    call ck_line
    mov ax, cx
    mov bx, dx
    pop cx
    loop .l
    ret

; OP_SQ a, f, half-size
dko_sq:
    lodsw
    call dk_polf
    push ax
    push bx
    lodsb
    call dk_rf
    or ax, ax
    jnz .h
    inc ax
.h:
    mov cx, ax                  ; the half-width
    call dk_sy
    mov dx, ax                  ; ...and the half-height
    pop bx
    pop ax
    sub ax, cx
    sub bx, dx
    shl cx, 1
    shl dx, 1
    jmp ck_rect

; OP_HANDS hour lc, minute lc, second lc, their lengths, two widths
dko_hands:
    mov di, si
    add si, 8
    mov al, [dk_hh]             ; the hour hand's angle: (h mod 12) x 10 +
                                ; m / 6, so it sits between the hours
    xor ah, ah
    mov bl, 12
    div bl
    mov al, ah
    mov bl, 10
    mul bl
    push ax
    mov al, [dk_mm]
    xor ah, ah
    mov bl, 6
    div bl
    pop bx
    add al, bl
    mov dl, al                  ; DL = the hour's angle
    mov al, [di]
    call ck_ink
    mov al, [di+6]
    call dk_wf
    mov al, dl
    mov ah, [di+3]
    call dk_hand
    mov al, [dk_mm]             ; the minute's: m x 2, and a step more
    shl al, 1                   ; past the half minute
    cmp byte [dk_ss], 30
    jb .m
    inc ax
.m:
    mov dl, al
    mov al, [di+1]
    call ck_ink
    mov al, [di+7]
    call dk_wf
    mov al, dl
    mov ah, [di+4]
    call dk_hand
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jz .r
    mov al, [di+2]
    call ck_ink
    mov cx, 1
    call ck_brush
    mov al, [dk_ss]
    shl al, 1
    mov ah, [di+5]
    call dk_hand
.r:
    ret

; dk_hand - AL = an angle, AH = f: a line from the centre out - none at
; all when its box misses the columns being composed (a hand's redraw
; composes its own box: the other two hands are elsewhere)
dk_hand:
    push ax
    push bx
    push cx
    push dx
    call dk_polf
    mov cx, ax
    mov dx, bx
    mov ax, [dk_cx]
    mov bx, [dk_cy]
    push ax
    push cx
    cmp ax, cx                  ; AX = its left, CX = its right, on the canvas
    jle .o
    xchg ax, cx
.o:
    sub ax, [ck_bw]
    add cx, [ck_bw]
    add ax, [ck_ox]
    add cx, [ck_ox]
    cmp cx, [ck_cx0]
    jl .no
    cmp ax, [ck_cx1]
    jg .no
    pop cx
    pop ax
    call ck_line
    jmp short .x
.no:
    pop cx
    pop ax
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; OP_CAP lc, f
dko_cap:
    lodsb
    call ck_ink
    jmp dko_ell

; OP_RRECT x, y, w, h, inset px, corner px
dko_rrect:
    call dk_frect               ; AX, BX, CX, DX
    push ax
    lodsb                       ; the inset
    xor ah, ah
    push ax
    or ax, ax
    jz .i
    call dk_sy
.i:
    mov di, ax                  ; DI = the rows' inset
    pop ax                      ; AX = the columns'
    add bx, di
    sub dx, di
    sub dx, di
    sub cx, ax
    sub cx, ax
    pop di
    add ax, di                  ; AX = x
    push ax
    push cx
    push dx
    lodsb                       ; the corner, at a 128-px face
    xor ah, ah
    mul word [dk_fw]
    mov cl, 7
    shr ax, cl
    mov [ck_rx], ax
    or ax, ax
    jz .r0
    call dk_sy
.r0:
    mov [ck_ry], ax
    pop dx
    pop cx
    pop ax
    jmp ck_rrect

; dk_frect - x, y, w, h (fractions of the face) at SI: AX, BX, CX, DX
dk_frect:
    lodsb
    call dk_fxf
    push ax
    lodsb
    call dk_fyf
    push ax
    lodsb
    call dk_fxf
    inc ax
    push ax
    lodsb
    call dk_fyf
    inc ax
    mov dx, ax
    pop cx
    pop bx
    pop ax
    ret

; OP_TEXTC x, y, the string
dko_textc:
    lodsb
    call dk_fxf
    mov cx, ax
    lodsb
    call dk_fyf
    mov dx, ax
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call ck_sc1
    cmp word [dk_fw], 120
    jb .t
    mov byte [ck_gbold], 1
.t:
    push si
    mov si, [dk_strs+bx]
    call ck_textc
    pop si
    mov byte [ck_gbold], 0
    ret

; OP_DATE x, y - `MO 8-24`, on a face wide enough for it
dko_date:
    lodsb
    call dk_fxf
    mov cx, ax
    lodsb
    call dk_fyf
    mov dx, ax
    cmp word [dk_fw], 88
    jb .r
    call ck_sc1
    call dk_scasio
    push si
    mov si, dk_line
    call ck_text
    pop si
.r:
    ret

; OP_HLINE x0, x1, y
dko_hline:
    lodsb
    call dk_fxf
    push ax
    lodsb
    call dk_fxf
    push ax
    lodsb
    call dk_fyf
    mov dx, ax
    pop cx
    pop ax
    jmp ck_span

; -----------------------------------------------------------------------------
; OP_TIME x, y, w, h, style, lit, dim, aux - the time, in the rect: HH:MM big,
; and - when there are seconds to show or a 12-hour clock's AM/PM - a narrow
; right column with the seconds small at its foot and the AM/PM at its head
; -----------------------------------------------------------------------------
dko_time:
    call dk_frect
    mov [dk_tx], ax
    mov [dk_ty], bx
    mov [dk_tw], cx
    mov [dk_th], dx
    lodsb
    mov [dk_tsty], al
    lodsb
    mov [dk_tlit], al
    lodsb
    mov [dk_tdim], al
    lodsb
    mov [dk_taux], al
    push si
    call dk_hour                ; the digits: hours (a blank tens under 12-hour)
    mov [dk_tam], ah
    aam
    xchg al, ah
    mov word [dk_dig], ax
    test byte [DKP + DKP_FLAGS], DKF_H24
    jnz .h2
    or al, al
    jnz .h2
    mov byte [dk_dig], DKD_BLANK
.h2:
    mov al, [dk_mm]
    aam
    xchg al, ah
    mov word [dk_dig+2], ax
    mov al, [dk_ss]
    aam
    xchg al, ah
    mov word [dk_dig+4], ax
    mov byte [dk_tcol], 0       ; the right column?
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jnz .col
    cmp byte [dk_tam], 0
    je .main
.col:
    mov byte [dk_tcol], 1
    mov ax, [dk_tw]             ; the main group is 30 units of 39
    mov cx, 30
    mul cx
    mov cx, 39
    div cx
    mov [dk_tw], ax
.main:
    mov ax, [dk_tw]             ; a digit 5/27 of the group: four, the colon
    mov cx, 5                   ; and the gaps fill it
    mul cx
    mov cx, 27
    div cx
    mov [dk_dw], ax
    mov ax, [dk_tw]
    mov cl, 30
    div cl
    xor ah, ah
    or ax, ax
    jnz .g
    inc ax
.g:
    mov [dk_gap], ax
    ; the four digits and the colon, left to right
    mov ax, [dk_tx]
    mov [dk_cur], ax
    xor bx, bx
    call dk_dcell               ; hours
    inc bx
    call dk_dcell
    call dk_colon
    inc bx
    call dk_dcell               ; minutes
    inc bx
    call dk_dcell
    cmp byte [dk_tcol], 0
    je .x
    ; the right column: seconds small at the foot, AM/PM at the head
    mov ax, [dk_tx]
    add ax, [dk_tw]
    add ax, [dk_gap]
    add ax, [dk_gap]
    mov [dk_cur], ax
    mov ax, [dk_dw]             ; small digits: half the size
    shr ax, 1
    mov [dk_dw], ax
    mov ax, [dk_th]
    shr ax, 1
    push word [dk_ty]
    push word [dk_th]
    add [dk_ty], ax
    mov [dk_th], ax
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jz .nos
    mov bx, 4
    call dk_dcell
    inc bx
    call dk_dcell
.nos:
    pop word [dk_th]
    pop word [dk_ty]
    cmp byte [dk_tam], 0
    je .x
    mov al, [dk_tlit]
    call ck_ink
    call ck_sc1
    mov si, dks_am
    cmp byte [dk_tam], 1
    je .am
    mov si, dks_pm
.am:
    mov cx, [dk_tx]
    add cx, [dk_tw]
    add cx, [dk_gap]
    add cx, [dk_gap]
    mov dx, [dk_ty]
    mov ax, [dk_th]
    cmp ax, 22                  ; under 22 rows there is no room for the
    jb .dot                     ; letters: a dot means PM
    call ck_text
    jmp short .x
.dot:
    cmp byte [dk_tam], 2
    jne .x
    mov ax, cx
    mov bx, dx
    mov cx, 2
    mov dx, 2
    call ck_rect
.x:
    pop si
    ret
dks_am:     db 'AM', 0
dks_pm:     db 'PM', 0

; dk_colon - the colon after the hours at [dk_cur]: two dots (a gap on a flip
; clock, whose panels say it); [dk_cur] past it
dk_colon:
    push ax
    push bx
    push cx
    push dx
    mov ax, [dk_dw]
    shr ax, 1
    shr ax, 1                   ; the colon's width: a third of a digit,
    add ax, [dk_gap]            ; about
    cmp byte [dk_tsty], DKS_FLIP
    je .adv
    push ax
    mov al, [dk_tlit]
    call ck_ink
    mov cx, [dk_dw]             ; the dot: a segment's thickness, square
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or cx, cx
    jnz .w
    inc cx
.w:
    cmp cx, 2
    jae .w2
    mov cx, 2
.w2:
    mov ax, cx
    call dk_sy
    mov dx, ax
    pop ax
    push ax
    sub ax, cx
    shr ax, 1
    add ax, [dk_cur]            ; AX = x
    mov bx, [dk_th]
    push dx
    xor dx, dx
    push cx
    mov cx, 3
    xchg ax, bx
    div cx
    xchg ax, bx                 ; BX = h / 3
    pop cx
    pop dx
    push bx
    add bx, [dk_ty]
    call ck_rect                ; the upper dot at a third
    pop bx
    shl bx, 1
    add bx, [dk_ty]
    call ck_rect                ; ...and the lower at two thirds
    pop ax
.adv:
    add [dk_cur], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_dcell - BX = the digit's index in dk_dig: drawn at [dk_cur], [dk_ty],
; [dk_dw] x [dk_th] in the style; [dk_cur] past it and its gap. Keeps all
dk_dcell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [dk_dig+bx]
    mov [dk_d], al
    mov al, [dk_tsty]
    cmp al, DKS_SEG
    je .seg
    cmp al, DKS_FLIP
    jne .gl
    call dk_panel
.gl:
    call dk_gcell
    jmp short .adv
.seg:
    call dk_seg7
.adv:
    mov ax, [dk_dw]
    add ax, [dk_gap]
    add [dk_cur], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_panel - a flip clock's card behind the digit at [dk_cur]: the edge in
; [dk_taux]'s ink, the card black, and the split across its middle
dk_panel:
    mov ax, [dk_cur]
    sub ax, 1
    mov bx, [dk_ty]
    mov cx, [dk_dw]
    add cx, 2
    mov dx, [dk_th]
    mov word [ck_rx], 2
    push ax
    mov ax, 2
    call dk_sy
    mov [ck_ry], ax
    mov al, [dk_taux]
    call ck_ink
    pop ax
    call ck_rrect
    inc ax
    inc bx
    sub cx, 2
    sub dx, 2
    push ax
    mov al, LC_BLK
    call ck_ink
    pop ax
    jmp ck_rrect

; dk_gcell - the digit [dk_d] as a bitmap glyph centred in its cell, scaled
; to it (bold); a flip clock's split drawn through it
dk_gcell:
    mov al, [dk_d]
    cmp al, DKD_BLANK
    je .r
    mov ax, [dk_dw]             ; the scale: the cell's width / 6 across,
    mov cl, 6                   ; its height / 7 down (dk_dfont is 6 x 7)
    div cl
    or al, al
    jnz .sx
    inc ax
.sx:
    mov [ck_gsx], al
    mov ax, [dk_th]
    mov cl, 7
    div cl
    or al, al
    jnz .sy
    inc ax
.sy:
    push ax                     ; ...but no taller than 3/2 of its width,
    mov al, [ck_gsx]            ; to the eye: a numeral, not a fence post
    mov ah, 3
    mul ah
    shr ax, 1
    call dk_sy
    mov cl, al
    pop ax
    cmp al, cl
    jbe .sy2
    mov al, cl
.sy2:
    mov [ck_gsy], al
    mov byte [ck_gbold], 1
    mov al, [dk_tlit]
    call ck_ink
    mov al, [ck_gsx]            ; centred: (cell - 6 sx) / 2 across,
    mov ah, 6                   ; (cell - 7 sy) / 2 down
    mul ah
    mov cx, [dk_dw]
    sub cx, ax
    sar cx, 1
    add cx, [dk_cur]
    mov al, [ck_gsy]
    mov ah, 7
    mul ah
    mov dx, [dk_th]
    sub dx, ax
    sar dx, 1
    add dx, [dk_ty]
    mov al, [dk_d]
    call dk_bmp
    mov byte [ck_gbold], 0
    cmp byte [dk_tsty], DKS_FLIP
    jne .r
    mov al, LC_BLK              ; the split, through the card
    call ck_ink
    mov ax, [dk_cur]
    mov cx, ax
    add cx, [dk_dw]
    dec cx
    mov dx, [dk_th]
    shr dx, 1
    add dx, [dk_ty]
    jmp ck_span
.r:
    ret

; dk_seg7 - the digit [dk_d] in seven segments at [dk_cur], [dk_ty], [dk_dw]
; x [dk_th]: lit in [dk_tlit], dark in [dk_tdim] (0FFh: not drawn)
dk_seg7:
    mov ax, [dk_dw]             ; the thickness: a fifth of the width, and as
    mov cl, 5                   ; tall down as that is across
    div cl
    xor ah, ah
    or ax, ax
    jnz .t
    inc ax
.t:
    mov [dk_stx], ax
    call dk_sy
    mov [dk_sty], ax
    mov bl, [dk_d]
    xor bh, bh
    mov al, [dk_seg+bx]
    mov [dk_sbits], al
    mov ax, [dk_th]             ; the middle bar's top
    sub ax, [dk_sty]
    shr ax, 1
    mov [dk_smid], ax
    mov si, dk_segs
    mov byte [dk_sbit], 1
.s:
    cmp si, dk_segs + 7 * 2
    jae .r
    mov al, [dk_sbits]
    test al, [dk_sbit]
    mov al, [dk_tlit]
    jnz .ink
    mov al, [dk_tdim]
    cmp al, 0FFh
    je .n
.ink:
    call ck_ink
    call [si]
.n:
    add si, 2
    shl byte [dk_sbit], 1
    jmp short .s
.r:
    ret
dk_segs:    dw dk_sa, dk_sb, dk_sc, dk_sd, dk_se, dk_sf, dk_sg

; the horizontal bars: x + stx + 1 .. x + w - stx - 2, at a row
dk_sh_at:                       ; BX = its top row (cell-relative)
    mov ax, [dk_cur]
    add ax, [dk_stx]
    inc ax
    mov cx, [dk_dw]
    sub cx, [dk_stx]
    sub cx, [dk_stx]
    sub cx, 2
    add bx, [dk_ty]
    mov dx, [dk_sty]
    jmp ck_rect
dk_sa:
    xor bx, bx
    jmp dk_sh_at
dk_sg:
    mov bx, [dk_smid]
    jmp dk_sh_at
dk_sd:
    mov bx, [dk_th]
    sub bx, [dk_sty]
    jmp dk_sh_at
; the verticals: AX = x (cell-relative), rows from BX for DX
dk_sv_at:
    add ax, [dk_cur]
    add bx, [dk_ty]
    inc bx
    sub dx, 2
    mov cx, [dk_stx]
    jmp ck_rect
dk_sf:
    xor ax, ax
    jmp short dk_sv_up
dk_sb:
    mov ax, [dk_dw]
    sub ax, [dk_stx]
dk_sv_up:                       ; rows sty .. mid - 1
    mov bx, [dk_sty]
    mov dx, [dk_smid]
    sub dx, bx
    jmp dk_sv_at
dk_se:
    xor ax, ax
    jmp short dk_sv_lo
dk_sc:
    mov ax, [dk_dw]
    sub ax, [dk_stx]
dk_sv_lo:                       ; rows mid + sty .. h - sty - 1
    mov bx, [dk_smid]
    add bx, [dk_sty]
    mov dx, [dk_th]
    sub dx, [dk_sty]
    sub dx, bx
    jmp dk_sv_at
