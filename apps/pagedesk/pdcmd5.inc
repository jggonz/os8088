; =============================================================================
; os8088 - apps/pagedesk/pdcmd5.inc          MD5 (RFC 1321), 8086 (PAGEDESK-SPEC 20.22.5)
;
; _pdc_md5_init(st) and _pdc_md5_block(st, blk): SmallerC cdecl, near pointers
; into DS, AX BX CX DX lost, BP SI DI DS ES SS:SP and DF kept. The state is
; four 32-bit words A,B,C,D each stored lo,hi (pdcrypto.h). The C twin is
; pdctwin.c; tests/pdcrypt/pdc_diff.py holds the two to RFC 1321's vectors.
; Include it once per assembly, in section .text with DS = CS.
; =============================================================================
%ifndef PDCMD5_INC
%define PDCMD5_INC 1
cpu 8086

section .bss
md5v:   resw 8                      ; a b c d, lo then hi
md5blk: resw 1
md5i:   resw 1
md5mp:  resw 1
section .text

_pdc_md5_init:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov word [bx],    0x2301
    mov word [bx+2],  0x6745
    mov word [bx+4],  0xAB89
    mov word [bx+6],  0xEFCD
    mov word [bx+8],  0xDCFE
    mov word [bx+10], 0x98BA
    mov word [bx+12], 0x5476
    mov word [bx+14], 0x1032
    pop bp
    ret

_pdc_md5_block:
    push bp
    mov bp, sp
    push si
    push di
    mov si, [bp+4]
    mov ax, [bp+6]
    mov [md5blk], ax
    mov bx, md5v
    mov cx, 8
.cp:
    lodsw
    mov [bx], ax
    inc bx
    inc bx
    loop .cp
    mov word [md5i], 0
.step:
    mov bx, [md5i]
    mov si, bx
    mov cl, 4
    shr si, cl                      ; the group 0..3
    mov al, [md5mul+si]
    mul bl                          ; AX = i * mult
    add al, [md5add+si]
    and ax, 15                      ; g
    shl ax, 1
    shl ax, 1
    add ax, [md5blk]
    mov [md5mp], ax                 ; &M[g]
    or si, si
    jz .f0
    dec si
    jz .f1
    dec si
    jz .f2
    ; F3 = c ^ (b | ~d)
    mov ax, [md5v+12]
    not ax
    or ax, [md5v+4]
    xor ax, [md5v+8]
    mov dx, [md5v+14]
    not dx
    or dx, [md5v+6]
    xor dx, [md5v+10]
    jmp short .fdone
.f0:                                ; F0 = d ^ (b & (c ^ d))
    mov ax, [md5v+8]
    xor ax, [md5v+12]
    and ax, [md5v+4]
    xor ax, [md5v+12]
    mov dx, [md5v+10]
    xor dx, [md5v+14]
    and dx, [md5v+6]
    xor dx, [md5v+14]
    jmp short .fdone
.f1:                                ; F1 = c ^ (d & (b ^ c))
    mov ax, [md5v+4]
    xor ax, [md5v+8]
    and ax, [md5v+12]
    xor ax, [md5v+8]
    mov dx, [md5v+6]
    xor dx, [md5v+10]
    and dx, [md5v+14]
    xor dx, [md5v+10]
    jmp short .fdone
.f2:                                ; F2 = b ^ c ^ d
    mov ax, [md5v+4]
    xor ax, [md5v+8]
    xor ax, [md5v+12]
    mov dx, [md5v+6]
    xor dx, [md5v+10]
    xor dx, [md5v+14]
.fdone:
    add ax, [md5v]
    adc dx, [md5v+2]
    mov bx, [md5i]
    shl bx, 1
    shl bx, 1
    add ax, [md5k+bx]
    adc dx, [md5k+bx+2]
    mov bx, [md5mp]
    add ax, [bx]
    adc dx, [bx+2]
    mov bx, [md5i]
    mov si, bx
    mov cl, 4
    shr si, cl
    shl si, 1
    shl si, 1
    and bx, 3
    add bx, si
    mov cl, [md5s+bx]
    cmp cl, 16
    jb .nosw
    xchg ax, dx
    sub cl, 16
.nosw:
    or cl, cl
    jz .rdone
.rl:
    shl ax, 1
    rcl dx, 1
    adc ax, 0
    dec cl
    jnz .rl
.rdone:
    add ax, [md5v+4]
    adc dx, [md5v+6]
    mov bx, [md5v+12]
    mov cx, [md5v+14]               ; the old d
    mov si, [md5v+8]
    mov [md5v+12], si
    mov si, [md5v+10]
    mov [md5v+14], si
    mov si, [md5v+4]
    mov [md5v+8], si
    mov si, [md5v+6]
    mov [md5v+10], si
    mov [md5v+4], ax
    mov [md5v+6], dx
    mov [md5v], bx
    mov [md5v+2], cx
    inc word [md5i]
    cmp word [md5i], 64
    jae .last
    jmp .step
.last:
    mov si, [bp+4]
    mov bx, md5v
    mov cx, 4
.fin:
    mov ax, [bx]
    add [si], ax
    mov ax, [bx+2]
    adc [si+2], ax
    add bx, 4
    add si, 4
    loop .fin
    pop di
    pop si
    pop bp
    ret

md5mul: db 1, 5, 3, 7
md5add: db 0, 1, 5, 0
md5s:   db 7,12,17,22, 5,9,14,20, 4,11,16,23, 6,10,15,21
align 2, db 0
md5k:
        dd 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee
        dd 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501
        dd 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be
        dd 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821
        dd 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa
        dd 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8
        dd 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed
        dd 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a
        dd 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c
        dd 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70
        dd 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05
        dd 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665
        dd 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039
        dd 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1
        dd 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1
        dd 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
%endif
