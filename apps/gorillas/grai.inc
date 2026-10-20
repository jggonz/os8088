; Solo opponent: try one candidate throw per worker tick, keeping input,
; menus and pause responsive. Predict the same wind/gravity substeps as the
; live shot, aiming at the human's center with an imperfect release power.
; Buildings may intercept a throw;
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
    mov ax, [gr_gx]
    add ax, 8
    mov cl, 6
    shl ax, cl
    mov [gr_aitarget], ax
.batch:
    call gr_aipredict
    add word [gr_aipower], 2
    cmp word [gr_aipower], 360
    ja .fire
.out:
    ret
.fire:
    mov word [gr_angle], 55
    ; A fresh -8..+8 velocity error makes even a clear shot fallible.
    call gr_rand
    xor dx, dx
    mov bx, 17
    div bx
    mov ax, [gr_aibest]
    add ax, dx
    sub ax, 8
    cmp ax, 1
    jge .upper
    mov ax, 1
.upper:
    cmp ax, 360
    jle .power
    mov ax, 360
.power:
    mov [gr_power], ax
    mov word [gr_aipower], 0
    call gr_fire
    jmp gr_hudpaint

gr_aipredict:
    mov si, gr_aix
    mov bp, 2
    mov ax, 55
    xor dx, dx
    mov di, [gr_aipower]
    call gr_initmotion
    mov di, 512
.frame:
    call gr_framebegin
    mov bp, [gr_steps]
.step:
    call gr_motionstep
    mov ax, [gr_aitarget]
    cmp [gr_aix], ax
    jbe .crossed
    cmp word [gr_aix], 256*64
    jae .out
    cmp word [gr_aiyhi], 0
    jl .next
    jg .out
    cmp word [gr_aiy], 128*64
    jae .out
.next:
    dec bp
    jnz .step
    xor bx, bx
    call gr_accelerate
    dec di
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
