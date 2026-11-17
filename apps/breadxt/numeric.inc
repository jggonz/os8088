; Breadboard XT original cdecl adapters to the shared software arithmetic.
; SPEC.md 130.3. DS remains the package; SS addresses cdecl arguments.
; Fixed scratch is deliberately not reentrant. Only lock-free wakes call it.
; bb_fp_scratch is a 76-byte C BSS array; no section change belongs here.
fp_as      equ _bb_fp_scratch
fp_bs      equ fp_as+1
fp_ae      equ fp_as+2
fp_be      equ fp_as+4
fp_am0     equ fp_as+6
fp_am1     equ fp_as+8
fp_am2     equ fp_as+10
fp_am3     equ fp_as+12
fp_bm0     equ fp_as+14
fp_bm1     equ fp_as+16
fp_bm2     equ fp_as+18
fp_bm3     equ fp_as+20
fp_t0      equ fp_as+22
fp_t1      equ fp_as+24
fp_t2      equ fp_as+26
fp_t3      equ fp_as+28
fp_p0      equ fp_as+30
fp_sticky  equ fp_as+46
fp_tmp     equ fp_as+48
fp_sgn     equ fp_as+50
fp_hw      equ fp_as+52
fp_x1      equ fp_as+53
fp_x2      equ fp_as+63
fp_sw      equ fp_as+73

; Incoming packed record BX, valid zero or 2^-64 <= |x| < 2^40.
bb_num_packed_ok:
    mov ax,[bx+6]
    and ax,0x7fff
    mov dx,ax
    or dx,[bx]
    or dx,[bx+2]
    or dx,[bx+4]
    jz .ok
    and ax,0x7ff0
    cmp ax,0x3bf0
    jb .bad
    cmp ax,0x4270
    jae .bad
.ok:
    clc
    ret
.bad:
    stc
    ret

; A's unpacked normalized mantissa has its leading bit at 63.
bb_num_a_ok:
    mov ax,[fp_am0]
    or ax,[fp_am1]
    or ax,[fp_am2]
    or ax,[fp_am3]
    jz .ok
    cmp word [fp_ae],-127
    jl .bad
    cmp word [fp_ae],-23
    jge .bad
.ok:
    clc
    ret
.bad:
    stc
    ret

bb_num_fail:
    mov word [_bb_num_error],14       ; BB_ENUMERIC; never editor scratch
    xor ax,ax
    ret

_bb_nset:
    push bp
    mov bp,sp
    mov byte [fp_hw],0
    mov ax,[bp+6]
    call fp_i2a
    mov di,[bp+4]
    call fp_pack_a
    pop bp
    ret

_bb_nuint:
    push bp
    mov bp,sp
    mov byte [fp_hw],0
    mov ax,[bp+6]
    mov [fp_am0],ax
    mov word [fp_am1],0
    mov word [fp_am2],0
    mov word [fp_am3],0
    mov word [fp_ae],0
    mov byte [fp_as],0
    mov bx,fp_am0
    mov di,fp_ae
    call fp_norm
    mov di,[bp+4]
    call fp_pack_a
    pop bp
    ret

_bb_nop:
    push bp
    mov bp,sp
    mov byte [fp_hw],0
    mov bx,[bp+6]
    call bb_num_packed_ok
    jc .bad
    mov bx,[bp+8]
    call bb_num_packed_ok
    jc .bad
    mov si,[bp+6]
    call fp_unpack_a
    mov si,[bp+8]
    call fp_unpack_b
    mov ax,[bp+10]
    or ax,ax
    jz .add
    cmp ax,1
    je .sub
    cmp ax,2
    je .mul
    cmp ax,3
    jne .bad
    call fp_div
    jc .bad                          ; carry belongs only to division
    jmp .check
.add:
    call fp_add
    jmp .check
.sub:
    call fp_sub
    jmp .check
.mul:
    call fp_mul
.check:
    call bb_num_a_ok
    jc .bad
    mov di,[bp+4]
    call fp_pack_a
    mov ax,1
    pop bp
    ret
.bad:
    call bb_num_fail
    pop bp
    ret

_bb_ncmp:
    push bp
    mov bp,sp
    mov byte [fp_hw],0
    mov bx,[bp+4]
    call bb_num_packed_ok
    jc .bad
    mov bx,[bp+6]
    call bb_num_packed_ok
    jc .bad
    mov si,[bp+4]
    call fp_unpack_a
    mov si,[bp+6]
    call fp_unpack_b
    call fp_cmpab
    pop bp
    ret
.bad:
    call bb_num_fail
    pop bp
    ret

_bb_nround:
    push bp
    mov bp,sp
    mov byte [fp_hw],0
    mov bx,[bp+4]
    call bb_num_packed_ok
    jc .bad
    mov si,[bp+4]
    call fp_unpack_a
    xor ax,ax
    mov al,[fp_as]
    push ax
    mov byte [fp_as],0
    call fp_half_add
    pop ax
    mov [fp_as],al
    call fp_a2i
    jc .bad
    pop bp
    ret
.bad:
    call bb_num_fail
    pop bp
    ret

; Exact bounded period*duty/1000 for the clock. Inputs are unsigned words.
; Quotient overflow or zero denominator sets the independent numeric error.
_bb_umuldiv:
    push bp
    mov bp,sp
    mov ax,[bp+4]
    mul word [bp+6]
    mov cx,[bp+8]
    or cx,cx
    jz .bad
    cmp dx,cx
    jae .bad
    div cx
    pop bp
    ret
.bad:
    call bb_num_fail
    pop bp
    ret

%define FP_NO_FTOA
%define FP_NO_SQRT
%define FP_NO_FLOOR
%define FP_NO_POW
%define OS88FP_NOTRANS
%include "os88fp.inc"
