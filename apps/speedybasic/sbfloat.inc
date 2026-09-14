; C ABI for os88fp.inc. Operands always live in DS, never on the UI stack.
_sbf_init:
    call fp_init
    ret
_sbf_integer:
    push bp
    mov bp, sp
    mov ax, [bp+6]
    mov di, [bp+4]
    xor dx, dx
    mov word [di], 0
    mov word [di+2], 0
    mov word [di+4], 0
    mov word [di+6], 0
    or ax, ax
    jz .done
    jns .positive
    mov dx, 0x8000
    neg ax
.positive:
    mov cx, 1038
.normalize:
    test ax, 0x8000
    jnz .pack
    shl ax, 1
    dec cx
    jmp short .normalize
.pack:
    mov bx, ax
    push cx
    mov cl, 5
    shl ax, cl
    mov [di+4], ax
    mov cl, 11
    shr bx, cl
    and bx, 15
    pop ax
    mov cl, 4
    shl ax, cl
    or ax, bx
    or ax, dx
    mov [di+6], ax
.done:
    pop bp
    ret
_sbf_parse:
    push bp
    mov bp, sp
    mov si, [bp+6]
    call fp_atof
    mov di, [bp+4]
    call fp_pack_a
    pop bp
    ret
_sbf_format:
    push bp
    mov bp, sp
    mov si, [bp+4]
    call fp_unpack_a
    mov di, [bp+6]
    mov ax, 9
    call fp_ftoa
    pop bp
    ret
_sbf_word:
    push bp
    mov bp, sp
    mov si, [bp+4]
    mov ax, [si+6]
    mov dx, ax
    and ax, 0x7ff0
    mov cl, 4
    shr ax, cl
    sub ax, 1023
    js .zero
    cmp ax, 15
    ja .wide
    mov bx, ax
    mov ax, dx
    and ax, 15
    or ax, 16
    mov cl, 11
    shl ax, cl
    mov cx, [si+4]
    push dx
    mov dx, cx
    mov cl, 5
    shr dx, cl
    or ax, dx
    mov cx, 15
    sub cx, bx
    shr ax, cl
    pop dx
    test dx, 0x8000
    jz .done
    neg ax
    jmp short .done
.wide:
    call fp_unpack_a
    ; Truncate and return the low word, including unsigned video offsets.
    mov cx, [fp_ae]
    neg cx
    cmp cx, 64
    jae .zero
    mov bx, fp_am0
    mov word [fp_sticky], 0
    call fp_shrn
    mov ax, [fp_am0]
    cmp byte [fp_as], 0
    je .done
    neg ax
    jmp short .done
.zero:
    xor ax, ax
.done:
    pop bp
    ret
_sbf_op:
    push bp
    mov bp, sp
    cmp byte [fp_hw], 0
    je .software
    mov ax, [bp+4]
    cmp ax, 5
    jbe .hardware
    cmp ax, 8
    jne .software
    mov si, [bp+6]
    test byte [si+7], 0x80
    jnz .software
.hardware:
    ; Packed operands need no unpack/normalize/pack round trip on an 8087.
    ; As in os88fp.inc, never hold the shared FPU across a timer interrupt.
    pushf
    cli
    mov si, [bp+6]
    mov di, [bp+8]
    fld qword [si]
    cmp ax, 1
    jne .hw_sub
    fadd qword [di]
    jmp .hw_store
.hw_sub:
    cmp ax, 2
    jne .hw_mul
    fsub qword [di]
    jmp .hw_store
.hw_mul:
    cmp ax, 3
    jne .hw_div
    fmul qword [di]
    jmp .hw_store
.hw_div:
    cmp ax, 4
    jne .hw_cmp
    fdiv qword [di]
    jmp .hw_store
.hw_cmp:
    cmp ax, 5
    jne .hw_sqrt
    fcomp qword [di]
    fstsw [fp_sw]
    fwait
    mov ax, [fp_sw]
    test ax, 0x4000
    jnz .hw_equal
    test ax, 0x0100
    jnz .hw_less
    mov ax, 1
    jmp .hw_return
.hw_less:
    mov ax, -1
    jmp .hw_return
.hw_equal:
    xor ax, ax
    jmp .hw_return
.hw_sqrt:
    fsqrt
.hw_store:
    fstp qword [si]
    fwait
    xor ax, ax
.hw_return:
    popf
    pop bp
    ret
.software:
    mov si, [bp+6]
    call fp_unpack_a
    mov si, [bp+8]
    call fp_unpack_b
    mov ax, [bp+4]
    cmp ax, 1
    jne .sub
    call fp_add
    jmp short .pack
.sub:
    cmp ax, 2
    jne .mul
    call fp_sub
    jmp short .pack
.mul:
    cmp ax, 3
    jne .div
    call fp_mul
    jmp short .pack
.div:
    cmp ax, 4
    jne .cmp
    call fp_div
    jmp short .pack
.cmp:
    cmp ax, 5
    jne .floor
    call fp_cmpab
    pop bp
    ret
.floor:
    cmp ax, 6
    jne .trunc
    call fp_floor
    jmp short .pack
.trunc:
    cmp ax, 7
    jne .sqrt
    call fp_trunc
    jmp short .pack
.sqrt:
    call fp_sqrt
.pack:
    mov di, [bp+6]
    call fp_pack_a
    xor ax, ax
    pop bp
    ret

%include "os88fp.inc"
section .bss
fp_as: resb 1
fp_bs: resb 1
fp_ae: resw 1
fp_be: resw 1
fp_am0: resw 1
fp_am1: resw 1
fp_am2: resw 1
fp_am3: resw 1
fp_bm0: resw 1
fp_bm1: resw 1
fp_bm2: resw 1
fp_bm3: resw 1
fp_t0: resw 1
fp_t1: resw 1
fp_t2: resw 1
fp_t3: resw 1
fp_p0: resw 8
fp_sticky: resw 1
fp_tmp: resw 1
fp_dig: resb 24
fp_d10: resw 1
fp_nd: resw 1
fp_sgn: resw 1
fp_sq: resb 8
fp_g: resb 8
fp_tv: resb 8
fp_hw: resb 1
fp_x1: resb 10
fp_x2: resb 10
fp_sw: resw 1
section .text
