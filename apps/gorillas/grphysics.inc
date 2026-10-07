; Numeric editors and reference-scaled fixed-point ballistics.
; Gravity retains thousandths; angle and velocity accept whole numbers only.
; AX=value, BX=0 angle / 2 velocity, DI=seven-cell output (space padded).
gr_shotnumber:
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    mov bp, di
    add bp, 7
    mov si, di
    call gr_number
    mov cx, 3
.leading:
    cmp cx, 1
    je .copy
    cmp byte [si], '0'
    jne .copy
    inc si
    dec cx
    jmp .leading
.copy:
    lodsb
    mov [di], al
    inc di
    loop .copy
.pad:
    cmp di, bp
    jae .done
    mov byte [di], ' '
    inc di
    jmp .pad
.done:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

gr_aimkey:
    xor bx, bx
    mov bl, [gr_field]
    shl bx, 1
    cmp al, 8
    je .back
    cmp ah, 48h
    je .up
    cmp ah, 4dh
    je .up
    cmp ah, 50h
    je .down
    cmp ah, 4bh
    je .down
    cmp al, '0'
    jb .out
    cmp al, '9'
    ja .out
    sub al, '0'
    xor ah, ah
    mov cx, ax
    mov ax, [gr_angle+bx]
    cmp byte [gr_edit], 0
    jne .append
    xor ax, ax
.append:
    mov dx, 10
    mul dx
    add ax, cx
    cmp ax, 360
    ja .out
    jmp .store
.back:
    mov ax, [gr_angle+bx]
    xor dx, dx
    mov si, 10
    div si
    jmp .store
.up:
    mov ax, [gr_angle+bx]
    cmp ax, 360
    jae .out
    inc ax
    jmp .store
.down:
    mov ax, [gr_angle+bx]
    or ax, ax
    jz .store
    dec ax
.store:
    mov [gr_angle+bx], ax
    mov byte [gr_edit], 1
    jmp gr_inputpaint
.out:
    ret

; SI=NUL decimal -> AX=whole 0..9999, DX=thousandths. CF invalid.
; Accept a leading/trailing dot as BASIC does; require at least one digit.
gr_decimalparse:
    xor bx, bx
    xor di, di
    xor bp, bp
    xor cx, cx
.next:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    jne .digit
    test bp, 8000h
    jnz .bad
    or bp, 8000h
    jmp .next
.digit:
    sub al, '0'
    cmp al, 9
    ja .bad
    xor ah, ah
    inc cx
    test bp, 8000h
    jnz .frac
    push ax
    mov ax, bx
    mov dx, 10
    mul dx
    mov bx, ax
    pop ax
    or dx, dx
    jnz .bad
    add bx, ax
    cmp bx, 9999
    ja .bad
    jmp .next
.frac:
    cmp bp, 8003h
    jae .bad
    inc bp
    push ax
    mov ax, di
    mov dx, 10
    mul dx
    mov di, ax
    pop ax
    add di, ax
    jmp .next
.end:
    jcxz .bad
    and bp, 7fffh
.pad:
    cmp bp, 3
    jae .done
    mov ax, di
    mov dx, 10
    mul dx
    mov di, ax
    inc bp
    jmp .pad
.done:
    mov ax, bx
    mov dx, di
    clc
    ret
.bad:
    stc
    ret

; Acceleration numerator in Q6 per .1-second sample, denominator 35000:
; g * 64 * 128/350 * .1^2 = g * 8192/35000.
gr_gravity:
    mov ax, [gr_gfrac]
    mov bx, 8192
    mul bx
    mov bx, 1000
    div bx
    mov si, ax
    mov ax, [gr_gwhole]
    mov bx, 8192
    mul bx
    add ax, si
    adc dx, 0
    mov [gr_grava], ax
    mov [gr_grava+2], dx
    ret

; Shared live/AI motion record: Q6 x, Q6 signed 32-bit y, Q6 vx/vy,
; gravity/wind and sweep division remainders. SI points to either record.
%define GM_X 0
%define GM_Y 2
%define GM_YHI 4
%define GM_VX 6
%define GM_VY 8
%define GM_GREM 10
%define GM_WREM 12
%define GM_XREM 14
%define GM_YREM 16

; AX=whole degrees -> AX=sin(angle)*16384. Other registers preserved.
gr_sine:
    push bx
    shl ax, 1
    mov bx, ax
    mov ax, [gr_sin+bx]
    pop bx
    ret

; SI=record, BP=gorilla word index, AX=angle, DI=power.
gr_initmotion:
    push ax
    call gr_sine
    imul di
    mov bx, 7000             ; 16384 / (64 * .1 * 128/350)
    idiv bx
    neg ax
    mov [si+GM_VY], ax
    pop ax
    add ax, 90
    cmp ax, 360
    jb .cos
    sub ax, 360
.cos:
    call gr_sine
    imul di
    mov bx, 6400             ; 16384 / (64 * .1 * 256/640)
    idiv bx
    or bp, bp
    jz .vx
    neg ax
.vx:
    mov [si+GM_VX], ax
    mov ax, [ds:gr_gx+bp]
    add ax, 2
    or bp, bp
    jz .x
    add ax, 8
.x:
    mov cl, 6
    shl ax, cl
    mov [si+GM_X], ax
    mov ax, [ds:gr_gy+bp]
    sub ax, 7
    shl ax, cl
    mov [si+GM_Y], ax
    mov word [si+GM_YHI], 0
    mov word [si+GM_GREM], 0
    mov word [si+GM_WREM], 0
    ; Midpoint velocity makes each discrete interval match .5*a*t^2.
    mov bl, 1
    jmp gr_accelerate

gr_accelerate:
    mov ax, [gr_grava]
    mov dx, [gr_grava+2]
    or bl, bl
    jz .gravity
    shr dx, 1
    rcr ax, 1
.gravity:
    add ax, [si+GM_GREM]
    adc dx, 0
    mov cx, 35000
    div cx
    mov [si+GM_GREM], dx
    add [si+GM_VY], ax
    mov ax, [gr_wind]
    mov cx, 32
    or bl, bl
    jz .wind
    mov cx, 16
.wind:
    imul cx
    add ax, [si+GM_WREM]
    adc dx, 0
    ; Signed carry into DX must account for the signed remainder.
    mov cx, [si+GM_WREM]
    or cx, cx
    jns .divide
    dec dx
.divide:
    mov cx, 625              ; wind/5 * .1^2 * 256/640 * 64
    idiv cx
    mov [si+GM_WREM], dx
    add [si+GM_VX], ax
    ret

; Choose enough substeps that neither axis moves more than one logical pixel.
gr_framebegin:
    mov ax, [si+GM_VX]
    or ax, ax
    jns .x
    neg ax
.x:
    mov bx, [si+GM_VY]
    or bx, bx
    jns .y
    neg bx
.y:
    cmp ax, bx
    jae .max
    mov ax, bx
.max:
    mov cl, 6
    shr ax, cl
    inc ax
    mov [gr_steps], ax
    mov word [si+GM_XREM], 0
    mov word [si+GM_YREM], 0
    ret

gr_motionstep:
    mov ax, [si+GM_VX]
    add ax, [si+GM_XREM]
    cwd
    idiv word [gr_steps]
    mov [si+GM_XREM], dx
    add [si+GM_X], ax
    mov ax, [si+GM_VY]
    add ax, [si+GM_YREM]
    cwd
    idiv word [gr_steps]
    mov [si+GM_YREM], dx
    cwd
    add [si+GM_Y], ax
    adc [si+GM_YHI], dx
    ret
