; =============================================================================
; os8088 - apps/pagedesk/pdxidct.inc        PDXI's JPEG inner loops (8086)
;                                           (PAGEDESK-SPEC 20.22.11)
;
; %included by pdxi.asm after the compiled C. Four cdecl procs, each with a C
; twin in pdxitwin.c that the host test (hosttest/pdi_asm.py, Unicorn) holds this
; file to bit for bit:
;
;   unsigned pdi_hsym(struct dj_huff *t)                  one Huffman symbol
;   unsigned pdi_bits(unsigned n)                         n (0..16) bits
;   int      pdi_block(dc, ac, int *coef)                 one baseline block
;   void     pdi_idct(int *coef, unsigned *qt, mode, dseg, doff, stride)
;
; The bit reader is STATELESS but for one word: _pdi_pos, a BIT index into
; _pdi_ebuf, a buffer of unstuffed entropy bytes the C keeps >= 340 bytes ahead
; of it and zero-padded after its end. 16 bits at a position are two shifts of
; three bytes, so no register carries a bit buffer between calls.
;
; The transforms are libjpeg's jidctint (8x8) and jidctred (4x4, 2x2): every
; sum that feeds a multiply is wrapped to 16 bits, every product is 32 bits.
; Preserved: BP SI DI DS ES SS:SP DF. Clobbered: AX BX CX DX, flags.
; =============================================================================
DJ_LIMIT  equ 512                       ; struct dj_huff offsets (pdxidct.c)
DJ_VALOFF equ 546
DJ_VALS   equ 580

section .bss
idc_ws:    resw 64
idc_i:     resw 8
idc_o:     resd 8
idc_t:     resd 16                      ; kernel temporaries
idc_s3:    resw 1
idc_s4:    resw 1
idc_mode:  resw 1
idc_dseg:  resw 1
idc_doff:  resw 1
idc_stride resw 1
idc_nout:  resw 1
idc_c:     resw 1
idc_r:     resw 1
idc_rnd1:  resw 1
idc_n1:    resb 1
idc_n1c:   resb 1                       ; 16 - n1
idc_rnd2:  resw 1                       ; added to the HIGH word
idc_n2c:   resb 1                       ; n2 - 16
idc_pad:   resb 1

section .text

; --- AX = the 16 bits at bit position [_pdi_pos]; clobbers BX CX DX ----------
pdi_peek:
    mov bx, [_pdi_pos]
    mov cx, bx
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and cl, 7
    mov ah, [_pdi_ebuf+bx]
    mov al, [_pdi_ebuf+bx+1]
    mov dl, [_pdi_ebuf+bx+2]
    xor dh, dh
    shl ax, cl
    shl dx, cl
    or al, dh
    ret

; --- SI = table; AX = symbol; consumes its code; clobbers BX CX DX ------------
pdi_hsym_si:
    call pdi_peek
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov dx, [si+bx]
    or dx, dx
    jz .slow
    mov bl, dh
    xor bh, bh
    add [_pdi_pos], bx
    mov al, dl
    xor ah, ah
    ret
.slow:
    mov dx, ax                          ; DX = the 16 bits
    mov cl, 7                           ; 16 - L, for L = 9 .. 16
    mov bx, DJ_LIMIT + 18
.lp:
    mov ax, dx
    shr ax, cl
    cmp ax, [si+bx]
    jb .found
    add bx, 2
    dec cl
    jns .lp
    add word [_pdi_pos], 16             ; no such code: eat 16 bits, symbol 0
    xor ax, ax
    ret
.found:
    add ax, [si+bx+(DJ_VALOFF-DJ_LIMIT)]
    mov dx, bx
    sub dx, DJ_LIMIT
    shr dx, 1                           ; L
    add [_pdi_pos], dx
    mov bl, al
    xor bh, bh
    mov al, [si+bx+DJ_VALS]
    xor ah, ah
    ret

; --- CX = s (1..15): AX = extend(bits(s), s); clobbers BX CX DX ----------------
pdi_getext:
    push cx
    call pdi_peek
    pop cx
    add [_pdi_pos], cx
    mov dx, cx
    neg cx
    add cx, 16
    shr ax, cl
    mov cx, dx
    mov bx, 1
    shl bx, cl                          ; 1 << s
    mov cx, bx
    shr cx, 1                           ; 1 << (s-1)
    cmp ax, cx
    jae .pos
    sub ax, bx
    inc ax
.pos:
    ret

_pdi_hsym:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    call pdi_hsym_si
    pop si
    pop bp
    ret

_pdi_bits:
    push bp
    mov bp, sp
    call pdi_peek
    mov cx, [bp+4]
    add [_pdi_pos], cx
    neg cx
    add cx, 16
    shr ax, cl                          ; a count of 16 gives 0, as asked for 0 bits
    pop bp
    ret

; int pdi_block(struct dj_huff *dc, struct dj_huff *ac, int *coef)
; frame: bp+4 dc, bp+6 ac, bp+8 coef; locals [bp-8] the DC difference, [bp-10] k
_pdi_block:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, [bp+8]
    mov cx, 64
    xor ax, ax
    rep stosw
    mov si, [bp+4]
    call pdi_hsym_si
    and ax, 15
    mov cx, ax
    xor ax, ax
    jcxz .dcz
    call pdi_getext
.dcz:
    push ax                             ; [bp-8] diff
    mov ax, 1
    push ax                             ; [bp-10] k
    mov si, [bp+6]
    mov di, [bp+8]
.acl:
    cmp word [bp-10], 64
    jae .done
    call pdi_hsym_si                    ; AL = run/size
    mov bx, ax
    and ax, 15
    mov cl, 4
    shr bx, cl
    or ax, ax
    jnz .coef
    cmp bx, 15
    jne .done
    add word [bp-10], 16
    jmp .acl
.coef:
    add [bp-10], bx
    cmp word [bp-10], 63
    ja .done
    mov cx, ax
    call pdi_getext
    mov bx, [bp-10]
    mov dl, [_pdi_zz+bx]
    xor dh, dh
    shl dx, 1
    mov bx, dx
    mov [di+bx], ax
    inc word [bp-10]
    jmp .acl
.done:
    pop ax
    pop ax                              ; the DC difference
    pop es
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; the transforms
; =============================================================================
%define I0 idc_i+0
%define I1 idc_i+2
%define I2 idc_i+4
%define I3 idc_i+6
%define I4 idc_i+8
%define I5 idc_i+10
%define I6 idc_i+12
%define I7 idc_i+14
%define TMP(n) idc_t+4*(n)
%define OUT(n) idc_o+4*(n)

; dst = word[src] * K       (DX:AX)
%macro MULK 3
    mov ax, [%2]
    mov cx, %3
    imul cx
    mov [%1], ax
    mov [%1+2], dx
%endmacro
; dst = AX * K
%macro MULKS 2
    mov cx, %2
    imul cx
    mov [%1], ax
    mov [%1+2], dx
%endmacro
%macro ACC32 2                          ; dst += a
    mov ax, [%2]
    add [%1], ax
    mov ax, [%2+2]
    adc [%1+2], ax
%endmacro
%macro ADD32 3                          ; dst = a + b
    mov ax, [%2]
    mov dx, [%2+2]
    add ax, [%3]
    adc dx, [%3+2]
    mov [%1], ax
    mov [%1+2], dx
%endmacro
%macro SUB32 3                          ; dst = a - b
    mov ax, [%2]
    mov dx, [%2+2]
    sub ax, [%3]
    sbb dx, [%3+2]
    mov [%1], ax
    mov [%1+2], dx
%endmacro

; temporaries: TMP(0..15)
%define Z1  TMP(0)
%define T0  TMP(1)
%define T1  TMP(2)
%define T2  TMP(3)
%define T3  TMP(4)
%define P10 TMP(5)
%define P11 TMP(6)
%define P12 TMP(7)
%define P13 TMP(8)
%define Z5  TMP(9)
%define U0  TMP(10)
%define U1  TMP(11)
%define U2  TMP(12)
%define U3  TMP(13)
%define Q1  TMP(14)
%define Q2  TMP(15)

idc_k8:                                 ; jidctint's 1-D, idc_i -> idc_o
    mov ax, [I2]
    add ax, [I6]
    MULKS Z1, 4433
    MULK T2, I6, -15137
    ACC32 T2, Z1
    MULK T3, I2, 6270
    ACC32 T3, Z1
    mov ax, [I0]
    add ax, [I4]
    MULKS T0, 8192
    mov ax, [I0]
    sub ax, [I4]
    MULKS T1, 8192
    ADD32 P10, T0, T3
    SUB32 P13, T0, T3
    ADD32 P11, T1, T2
    SUB32 P12, T1, T2
    mov ax, [I7]
    add ax, [I3]
    mov [idc_s3], ax
    mov ax, [I5]
    add ax, [I1]
    mov [idc_s4], ax
    add ax, [idc_s3]
    MULKS Z5, 9633
    MULK U0, I7, 2446
    MULK U1, I5, 16819
    MULK U2, I3, 25172
    MULK U3, I1, 12299
    mov ax, [I7]
    add ax, [I1]
    MULKS Q1, -7373
    mov ax, [I5]
    add ax, [I3]
    MULKS Q2, -20995
    ACC32 U0, Q1
    ACC32 U3, Q1
    ACC32 U1, Q2
    ACC32 U2, Q2
    mov ax, [idc_s3]
    MULKS T0, -16069                    ; z3
    ACC32 T0, Z5
    mov ax, [idc_s4]
    MULKS T1, -3196                     ; z4
    ACC32 T1, Z5
    ACC32 U0, T0
    ACC32 U2, T0
    ACC32 U1, T1
    ACC32 U3, T1
    ADD32 OUT(0), P10, U3
    SUB32 OUT(7), P10, U3
    ADD32 OUT(1), P11, U2
    SUB32 OUT(6), P11, U2
    ADD32 OUT(2), P12, U1
    SUB32 OUT(5), P12, U1
    ADD32 OUT(3), P13, U0
    SUB32 OUT(4), P13, U0
    ret

idc_k4:                                 ; jidctred's 4-output 1-D
    MULK T0, I0, 16384
    MULK T2, I2, 15137
    MULK T3, I6, -6270
    ACC32 T2, T3
    ADD32 P10, T0, T2
    SUB32 P12, T0, T2
    MULK U0, I7, -1730
    MULK T1, I5, 11893
    ACC32 U0, T1
    MULK T1, I3, -17799
    ACC32 U0, T1
    MULK T1, I1, 8697
    ACC32 U0, T1
    MULK U1, I7, -4176
    MULK T1, I5, -4926
    ACC32 U1, T1
    MULK T1, I3, 7373
    ACC32 U1, T1
    MULK T1, I1, 20995
    ACC32 U1, T1
    ADD32 OUT(0), P10, U1
    SUB32 OUT(3), P10, U1
    ADD32 OUT(1), P12, U0
    SUB32 OUT(2), P12, U0
    ret

idc_k2:                                 ; jidctred's 2-output 1-D
    mov ax, [I0]
    mov dx, ax
    sar dx, 1
    and ax, 1
    mov cl, 15
    shl ax, cl
    mov [T0], ax
    mov [T0+2], dx
    MULK U0, I7, -5906
    MULK T1, I5, 6967
    ACC32 U0, T1
    MULK T1, I3, -10426
    ACC32 U0, T1
    MULK T1, I1, 29692
    ACC32 U0, T1
    ADD32 OUT(0), T0, U0
    SUB32 OUT(1), T0, U0
    ret

idc_kern:                               ; by mode
    mov ax, [idc_mode]
    or ax, ax
    jz idc_k8
    dec ax
    jz idc_k4
    jmp idc_k2

; --- DX:AX -> AX, pass 1: (x + rnd1) >> n1, low 16 bits -----------------------
idc_d1:
    push cx
    add ax, [idc_rnd1]
    adc dx, 0
    mov bx, dx
    mov cl, [idc_n1]
    shr ax, cl
    mov cl, [idc_n1c]
    shl bx, cl
    or ax, bx
    pop cx
    ret
; --- pass 2: (x + (rnd2 << 16)) >> n2, low 16 bits ----------------------------
idc_d2:
    push cx
    add dx, [idc_rnd2]
    mov cl, [idc_n2c]
    sar dx, cl
    mov ax, dx
    pop cx
    ret

; --- AX = pass 2's DC input -> AL = the flat pixel it makes: ((i0 + 16) >> 5) + 128, clamped
;     (exactly what the full 1-D gives a row whose other seven inputs are 0, for all
;     three transforms: i0 * 2^13..15 + the rounding term, shifted by 18..20)
idc_dcv:
    cwd
    add ax, 16
    adc dx, 0
    mov cl, 5
    shr ax, cl
    mov cl, 11
    shl dx, cl
    or ax, dx
    cmp ax, 127
    jg .hi
    add ax, 128
    jns .ok
    xor ax, ax
    ret
.hi:
    mov ax, 255
.ok:
    ret

; void pdi_idct(int *coef, unsigned *qt, mode, dseg, doff, stride)
_pdi_idct:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov ax, [bp+8]
    mov [idc_mode], ax
    mov ax, [bp+10]
    mov [idc_dseg], ax
    mov ax, [bp+12]
    mov [idc_doff], ax
    mov ax, [bp+14]
    mov [idc_stride], ax
    ; a block with no AC term is one flat value: skip both passes
    mov di, [bp+4]
    add di, 2
    mov cx, 63
    xor ax, ax
    repe scasw
    jne .generic
    mov bx, [bp+4]
    mov ax, [bx]
    mov bx, [bp+6]
    imul word [bx]
    shl ax, 1
    shl ax, 1
    call idc_dcv
    mov bx, [idc_mode]                  ; nout = 8 >> mode
    mov cx, 8
    xchg cx, bx
    shr bx, cl                          ; (BX = 8 >> mode, kept in BX)
    mov es, [idc_dseg]
    mov di, [idc_doff]
    mov dx, bx                          ; rows left
.fl:
    mov cx, bx
    push di
    rep stosb
    pop di
    add di, [idc_stride]
    dec dx
    jnz .fl
    jmp .out
.generic:
    ; dequantise: ws[i] = coef[i] * qt[i] (low 16 bits)
    mov si, [bp+4]
    mov bx, [bp+6]
    mov di, idc_ws
    mov cx, 64
.dq:
    lodsw
    or ax, ax
    jz .dz
    imul word [bx]
.dz:
    stosw
    add bx, 2
    loop .dq
    ; the mode's constants
    mov ax, [idc_mode]
    mov word [idc_nout], 8
    mov byte [idc_n1], 11
    or ax, ax
    jz .md
    mov word [idc_nout], 4
    mov byte [idc_n1], 12
    cmp ax, 1
    je .md
    mov word [idc_nout], 2
    mov byte [idc_n1], 13
.md:
    mov cl, [idc_n1]
    mov bx, 1
    dec cl
    shl bx, cl
    mov [idc_rnd1], bx                  ; 1 << (n1 - 1)
    mov al, 16
    sub al, [idc_n1]
    mov [idc_n1c], al                   ; 16 - n1
    mov cl, [idc_n1]
    sub cl, 10                          ; n2 - 17, n2 = n1 + 7
    mov bx, 1
    shl bx, cl
    mov [idc_rnd2], bx
    mov al, [idc_n1]
    sub al, 9
    mov [idc_n2c], al                   ; n2 - 16

    ; ---- pass 1: the columns
    mov word [idc_c], 0
.col:
    mov bx, [idc_c]
    cmp word [idc_mode], 1
    jne .m2
    cmp bx, 4
    je .nextcol
    jmp .cok
.m2:
    cmp word [idc_mode], 2
    jne .cok
    cmp bx, 2
    je .nextcol
    cmp bx, 4
    je .nextcol
    cmp bx, 6
    je .nextcol
.cok:
    mov si, bx
    shl si, 1
    add si, idc_ws                      ; ws + 2c
    mov bx, idc_i
    mov cx, 8
.ld1:
    mov ax, [si]
    mov [bx], ax
    add si, 16
    add bx, 2
    loop .ld1
    mov di, [idc_c]
    shl di, 1
    add di, idc_ws
    mov cx, [idc_nout]
    mov ax, [I1]
    or ax, [I2]
    or ax, [I3]
    or ax, [I4]
    or ax, [I5]
    or ax, [I6]
    or ax, [I7]
    jnz .full
    mov ax, [I0]
    shl ax, 1
    shl ax, 1
.sc:
    mov [di], ax
    add di, 16
    loop .sc
    jmp .nextcol
.full:
    call idc_kern
    mov si, idc_o
    mov cx, [idc_nout]
.st1:
    mov ax, [si]
    mov dx, [si+2]
    add si, 4
    call idc_d1
    mov [di], ax
    add di, 16
    loop .st1
.nextcol:
    inc word [idc_c]
    cmp word [idc_c], 8
    jb .col

    ; ---- pass 2: the rows, to the far plane
    mov es, [idc_dseg]
    mov word [idc_r], 0
.row:
    mov si, [idc_r]
    mov cl, 4
    shl si, cl
    add si, idc_ws
    mov bx, idc_i
    mov cx, 8
.ld2:
    lodsw
    mov [bx], ax
    add bx, 2
    loop .ld2
    mov ax, [I1]
    or ax, [I2]
    or ax, [I3]
    or ax, [I4]
    or ax, [I5]
    or ax, [I6]
    or ax, [I7]
    jnz .rfull
    mov ax, [I0]
    call idc_dcv
    mov bx, ax                          ; the flat pixel
    mov ax, [idc_r]
    mul word [idc_stride]
    add ax, [idc_doff]
    mov di, ax
    mov ax, bx
    mov cx, [idc_nout]
    rep stosb
    jmp .rnext
.rfull:
    call idc_kern
    mov ax, [idc_r]
    mul word [idc_stride]
    add ax, [idc_doff]
    mov di, ax
    mov si, idc_o
    mov cx, [idc_nout]
.st2:
    mov ax, [si]
    mov dx, [si+2]
    add si, 4
    call idc_d2
    cmp ax, 127
    jg .hi
    add ax, 128
    jns .put
    xor ax, ax
    jmp .put
.hi:
    mov ax, 255
.put:
    mov [es:di], al
    inc di
    loop .st2
.rnext:
    inc word [idc_r]
    mov ax, [idc_r]
    cmp ax, [idc_nout]
    jb .row
.out:
    pop es
    pop di
    pop si
    pop bp
    ret
