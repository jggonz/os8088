; =============================================================================
; os8088 - apps/win16/w16fmt.inc
;
; wsprintf and wvsprintf (USER.420, USER.421): USER's own printf, after
; Wine's user/wsprintf.c. %[-][#][0][width][.precision][l]type, the types
; c d i u x X s and %%; %s and the l forms take a far argument, as Win16's
; did; no floating point, none in USER. At most 1,024 characters are
; written, then a NUL. Answers the characters written, the NUL not counted.
; =============================================================================

W16W_MAX    equ 1024

; wsprintf (USER.420) - cdecl: +0 lpOut, +4 lpFormat, +8 the arguments
w16u_wsprintf:
    call w16_args
    mov ax, [es:bx]
    mov [w16w_out], ax
    mov ax, [es:bx + 2]
    mov [w16w_out + 2], ax
    mov ax, [es:bx + 4]
    mov [w16w_fmt], ax
    mov ax, [es:bx + 6]
    mov [w16w_fmt + 2], ax
    lea ax, [bx + 8]
    mov [w16w_arg], ax
    mov [w16w_arg + 2], es
    jmp short w16w_go

; wvsprintf (USER.421) - pascal: +0 lpArgs, +4 lpFormat, +8 lpOut
w16u_wvsprintf:
    call w16_args
    mov ax, [es:bx]
    mov [w16w_arg], ax
    mov ax, [es:bx + 2]
    mov [w16w_arg + 2], ax
    mov ax, [es:bx + 4]
    mov [w16w_fmt], ax
    mov ax, [es:bx + 6]
    mov [w16w_fmt + 2], ax
    mov ax, [es:bx + 8]
    mov [w16w_out], ax
    mov ax, [es:bx + 10]
    mov [w16w_out + 2], ax
    ; fall into w16w_go

; w16w_go - format [w16w_fmt] with [w16w_arg] into [w16w_out]. AX = count
w16w_go:
    mov word [w16w_cnt], 0
.next:
    call w16w_fch
    or al, al
    jz .end
    cmp al, '%'
    je .spec
.lit:
    call w16w_put
    jmp short .next
.end:
    les di, [w16w_out]
    mov byte [es:di], 0
    mov ax, [w16w_cnt]
    ret
.spec:
    xor ax, ax
    mov [w16w_left], al
    mov [w16w_alt], al
    mov [w16w_long], al
    mov [w16w_wid], ax
    mov byte [w16w_pad], ' '
    mov word [w16w_prec], 0xFFFF    ; none
.flag:
    call w16w_fch
    cmp al, '-'
    jne .nl
    mov byte [w16w_left], 1
    jmp short .flag
.nl:
    cmp al, '0'
    jne .nz
    mov byte [w16w_pad], '0'
    jmp short .flag
.nz:
    cmp al, '#'
    jne .wid
    mov byte [w16w_alt], 1
    jmp short .flag
.wid:
    mov si, w16w_wid
    call w16w_num                   ; AL = the character after the digits
    cmp al, '.'
    jne .size
    mov word [w16w_prec], 0
    call w16w_fch
    mov si, w16w_prec
    call w16w_num
.size:
    cmp al, 'l'
    je .long
    cmp al, 'L'
    jne .type
.long:
    mov byte [w16w_long], 1
    call w16w_fch
.type:
    or al, al
    jz .end
    cmp al, 'd'
    je .sgn
    cmp al, 'i'
    je .sgn
    cmp al, 'u'
    je .uns
    cmp al, 'x'
    je .hex
    cmp al, 'X'
    je .hex
    cmp al, 'c'
    je .chr
    cmp al, 's'
    je .str
    jmp .lit                        ; '%%' and anything else: the character
.chr:
    call w16w_arg1                  ; AX
    mov [w16w_buf], al
    mov si, w16w_buf
    mov cx, 1
    xor dx, dx                      ; no sign, no prefix
    jmp .field
.str:
    call w16w_arg1                  ; a far pointer: offset, then segment
    push ax
    call w16w_arg1
    mov dx, ax
    pop ax
    mov cx, ax
    or cx, dx
    jnz .sl
    mov ax, w16w_s_null             ; NULL: "(null)", as Wine has it
    mov dx, ds
.sl:
    mov [w16w_sp], ax
    mov [w16w_sp + 2], dx
    les di, [w16w_sp]               ; its length, cut to the precision
    xor cx, cx
.slen:
    cmp cx, [w16w_prec]
    jae .sgot
    cmp byte [es:di], 0
    je .sgot
    inc di
    inc cx
    jmp short .slen
.sgot:
    mov [w16w_n], cx
    mov ax, [w16w_wid]              ; right-aligned: the blanks first
    sub ax, cx
    jbe .s0
    cmp byte [w16w_left], 0
    jne .s0
    xchg ax, cx
.sb:
    mov al, ' '
    call w16w_put
    loop .sb
    mov cx, [w16w_n]
.s0:
    jcxz .sx
    les si, [w16w_sp]
.sc:
    mov al, [es:si]
    inc si
    push es
    call w16w_put
    pop es
    loop .sc
.sx:
    mov ax, [w16w_wid]              ; left-aligned: the blanks after
    sub ax, [w16w_n]
    jbe .sdone
    cmp byte [w16w_left], 0
    je .sdone
    mov cx, ax
.sa:
    mov al, ' '
    call w16w_put
    loop .sa
.sdone:
    jmp .next
.sgn:
    mov byte [w16w_neg], 1
    mov word [w16w_base], 10
    jmp short .num
.uns:
    mov byte [w16w_neg], 0
    mov word [w16w_base], 10
    jmp short .num
.hex:
    mov byte [w16w_neg], 0
    mov word [w16w_base], 16
    mov byte [w16w_hexa], 'a' - 10
    cmp al, 'x'
    je .num
    mov byte [w16w_hexa], 'A' - 10
.num:
    mov [w16w_tch], al
    call w16w_arg1                  ; DX:AX = the value
    xor dx, dx
    cmp byte [w16w_long], 0
    je .short
    push ax
    call w16w_arg1
    mov dx, ax
    pop ax
    jmp short .signed
.short:
    cmp byte [w16w_neg], 0          ; a 16-bit signed value, widened
    je .signed
    cwd
.signed:
    mov byte [w16w_minus], 0
    cmp byte [w16w_neg], 0
    je .digits
    or dx, dx
    jns .digits
    mov byte [w16w_minus], 1
    not dx
    neg ax
    sbb dx, -1
.digits:
    mov di, w16w_buf + 12           ; the digits, backwards
    xor cx, cx
.dv:
    mov bx, ax                      ; DX:AX / base, 32 by 16 in two steps
    mov ax, dx
    xor dx, dx
    div word [w16w_base]
    xchg ax, bx                     ; BX = the high quotient
    div word [w16w_base]            ; AX = the low, DX = the digit
    xchg ax, dx
    cmp al, 10
    jb .dec
    add al, [w16w_hexa]
    jmp short .dput
.dec:
    add al, '0'
.dput:
    dec di
    mov [di], al
    inc cx
    mov ax, dx
    mov dx, bx
    or bx, ax
    jnz .dv
    ; the precision is the least number of digits
    mov ax, [w16w_prec]
    cmp ax, 0xFFFF
    je .pfx
    cmp ax, 11
    jbe .pz
    mov ax, 11
.pz:
    cmp cx, ax
    jae .pfx
    dec di
    mov byte [di], '0'
    inc cx
    jmp short .pz
.pfx:
    mov si, di                      ; SI/CX = the digits; DX = the sign or
    xor dx, dx                      ;   the 0x before them
    cmp byte [w16w_minus], 0
    je .alt
    mov dl, '-'
    jmp short .field
.alt:
    cmp byte [w16w_alt], 0
    je .field
    cmp byte [w16w_tch], 'x'
    je .ax
    cmp byte [w16w_tch], 'X'
    jne .field
.ax:
    mov dl, '0'
    mov dh, [w16w_tch]
; .field - DS:SI/CX = the text, DL (and DH) what goes before it: padded to
; the width, on the left with [w16w_pad] or on the right with blanks
.field:
    mov [w16w_n], cx
    mov ax, cx                      ; the field's own length
    or dl, dl
    jz .f1
    inc ax
.f1:
    or dh, dh
    jz .f2
    inc ax
.f2:
    mov bx, [w16w_wid]
    sub bx, ax
    jnc .f3
    xor bx, bx
.f3:                                ; BX = the padding
    cmp byte [w16w_left], 0
    jne .fpre
    cmp byte [w16w_pad], '0'
    je .fpre
    call .blanks
.fpre:
    or dl, dl
    jz .fp2
    mov al, dl
    call w16w_put
.fp2:
    or dh, dh
    jz .fp3
    mov al, dh
    call w16w_put
.fp3:
    cmp byte [w16w_left], 0
    jne .ftext
    cmp byte [w16w_pad], '0'
    jne .ftext
    mov cx, bx                      ; zeros between the sign and the digits
    xor bx, bx
    jcxz .ftext
.fz:
    mov al, '0'
    call w16w_put
    loop .fz
.ftext:
    mov cx, [w16w_n]
    jcxz .fend
.ft:
    lodsb
    call w16w_put
    loop .ft
.fend:
    cmp byte [w16w_left], 0
    je .fdone
    call .blanks
.fdone:
    jmp .next
.blanks:                            ; BX blanks, and BX = 0
    mov cx, bx
    xor bx, bx
    jcxz .bout
.bl:
    mov al, ' '
    call w16w_put
    loop .bl
.bout:
    ret

w16w_s_null: db '(null)', 0

; w16w_fch - AL = the format's next character (it does not move past a NUL)
w16w_fch:
    push es
    push si
    les si, [w16w_fmt]
    mov al, [es:si]
    or al, al
    jz .out
    inc word [w16w_fmt]
.out:
    pop si
    pop es
    ret

; w16w_num - AL = a character: while a digit, [SI] = [SI] * 10 + it.
; AL = the first character that is not one
w16w_num:
    cmp al, '0'
    jb .out
    cmp al, '9'
    ja .out
    sub al, '0'
    cbw
    push ax
    mov ax, 10
    mul word [si]
    pop dx
    add ax, dx
    mov [si], ax
    call w16w_fch
    jmp short w16w_num
.out:
    ret

; w16w_arg1 - AX = the next argument word
w16w_arg1:
    push es
    push si
    les si, [w16w_arg]
    mov ax, [es:si]
    add word [w16w_arg], 2
    pop si
    pop es
    ret

; w16w_put - AL to the output, while there is room. Keeps all but flags
w16w_put:
    cmp word [w16w_cnt], W16W_MAX
    jae .out
    push es
    push di
    les di, [w16w_out]
    mov [es:di], al
    pop di
    pop es
    inc word [w16w_out]
    inc word [w16w_cnt]
.out:
    ret
