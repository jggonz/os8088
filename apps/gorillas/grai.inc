; Solo opponent: try one candidate throw per worker tick, keeping input,
; menus and pause responsive. Predict the same wind/gravity substeps as the
; live shot, aiming at the human's center. Buildings may intercept a throw;
; these misses make real craters. No scores, terrain or live shot are poked.
gr_aitick:
    cmp byte [gr_players], 1
    jne .out
    cmp byte [gr_turn], 1
    jne .out
    cmp word [gr_aipower], 0
    jne .batch
    mov word [gr_aipower], 1
    mov word [gr_aierror], 0ffffh
    mov word [gr_aibest], 70
.batch:
    call gr_aipredict
    add word [gr_aipower], 2
    cmp word [gr_aipower], 150
    ja .fire
.out:
    ret
.fire:
    mov word [gr_angle], 55
    mov ax, [gr_aibest]
    mov [gr_power], ax
    mov word [gr_aipower], 0
    call gr_fire
    jmp gr_hudpaint

gr_aipredict:
    mov ax, [gr_gx+2]
    add ax, 8
    mov cl, 6
    shl ax, cl
    mov [gr_aix], ax
    mov ax, [gr_gy+2]
    sub ax, 3
    shl ax, cl
    mov [gr_aiy], ax
    mov word [gr_aiyhi], 0
    mov ax, [gr_sin+55*2]
    mul word [gr_aipower]
    mov cx, 80
    div cx
    neg ax
    mov [gr_aivy], ax
    mov ax, [gr_sin+35*2]
    mul word [gr_aipower]
    div cx
    neg ax
    mov [gr_aivx], ax
    mov word [gr_aiwrem], 0
    mov word [gr_aigrem], 0
    mov bp, 512
.frame:
    mov si, 4
.step:
    mov ax, [gr_aivx]
    sar ax, 1
    sar ax, 1
    add [gr_aix], ax
    mov ax, [gr_aivy]
    sar ax, 1
    sar ax, 1
    cwd
    add [gr_aiy], ax
    adc [gr_aiyhi], dx
    ; The human's center is 48 pixels from the left edge.
    cmp word [gr_aix], 48*64
    jbe .crossed
    cmp word [gr_aix], 256*64
    jae .out
    cmp word [gr_aiyhi], 0
    jl .next
    jg .out
    cmp word [gr_aiy], 128*64
    jae .out
.next:
    dec si
    jnz .step
    mov ax, [gr_grav10]
    shl ax, 1
    shl ax, 1
    add ax, [gr_aigrem]
    xor dx, dx
    mov bx, 98
    div bx
    mov [gr_aigrem], dx
    add [gr_aivy], ax
    mov ax, [gr_wind]
    add ax, [gr_aiwrem]
    cwd
    mov bx, 4
    idiv bx
    mov [gr_aiwrem], dx
    add [gr_aivx], ax
    dec bp
    jnz .frame
.out:
    ret
.crossed:
    cmp word [gr_aiyhi], 0
    jne .out
    mov ax, [gr_gy]
    add ax, 10
    mov cl, 6
    shl ax, cl
    sub ax, [gr_aiy]
    jns .positive
    neg ax
.positive:
    cmp ax, [gr_aierror]
    jae .out
    mov [gr_aierror], ax
    mov ax, [gr_aipower]
    mov [gr_aibest], ax
    ret
