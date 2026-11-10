; =============================================================================
; os8088 - apps/pagedesk/pdxjasm.inc        PDXJ's inner loops (PAGEDESK-SPEC 20.22.13)
;
; The MQ decoder and the lifting/scaling/dequantising/grey loops, called once per
; code-block pass decision or once per ROW of coefficients (PAGEDESK-SPEC 2.3).
; pdxjtw.c is each routine's host twin and hosttest/pdxj_asm.py holds the two
; equal bit for bit (Unicorn). %included by pdxj.asm in section .text, after the
; sections are declared; it ends in section .text.
;
; CALLING CONVENTION (SPEC.md 73.3): SmallerC cdecl - args on the stack pushed
; right to left, the caller cleans, the result in AX, labels are the C name with
; a leading underscore. BP, DS, ES, SS:SP and DF are preserved; AX BX CX DX SI DI
; are the caller's to lose. NO string instruction touches ES (the kernel's, in a
; callback; the caller's in a module door), so every store is an explicit mov.
; DS = the module's data segment on entry (CS = DS, PAGEDESK-SPEC 3.1).
; =============================================================================
cpu 8086

section .bss
_xj_cx:   resb 19                       ; MQ contexts: index * 2 + mps
_xj_a:    resw 1                        ; MQ interval, C register (hi, lo), bit count
_xj_chi:  resw 1
_xj_clo:  resw 1
_xj_ct:   resb 1
          resb 1
_xj_bp:   resw 1                        ; the byte pointer
_xj_t1:   resw 1                        ; scratch for the loops that need CL and BP both
_xj_t2:   resw 1
_xj_t3:   resw 1
_xj_t4:   resw 1
_xj_t5:   resw 1

section .rodata
%include "pagedesk/hosttest/pdxj_mqtab.inc"

section .text

; ---- the MQ decoder ------------------------------------------------------------
; BYTEIN: preserves everything but AX and DX
xj_bytein:
    push bx
    mov bx, [_xj_bp]
    mov al, [bx]
    cmp al, 0xFF
    jne .norm
    mov al, [bx+1]
    cmp al, 0x8F
    ja .marker
    inc bx
    mov [_xj_bp], bx
    mov ah, al
    xor al, al
    xor dx, dx
    shl ax, 1
    rcl dx, 1                           ; dx:ax = B1 << 9
    add [_xj_clo], ax
    adc [_xj_chi], dx
    mov byte [_xj_ct], 7
    pop bx
    ret
.marker:
    add word [_xj_clo], 0xFF00
    adc word [_xj_chi], 0
    mov byte [_xj_ct], 8
    pop bx
    ret
.norm:
    inc bx
    mov [_xj_bp], bx
    mov al, [bx]
    mov ah, al
    xor al, al
    add [_xj_clo], ax
    adc word [_xj_chi], 0
    mov byte [_xj_ct], 8
    pop bx
    ret

; void xj_mqinit(pdb *buf)
_xj_mqinit:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov [_xj_bp], bx
    xor ax, ax
    mov al, [bx]
    mov [_xj_chi], ax
    mov word [_xj_clo], 0
    call xj_bytein
    mov cx, 7
.s: shl word [_xj_clo], 1
    rcl word [_xj_chi], 1
    loop .s
    sub byte [_xj_ct], 7
    mov word [_xj_a], 0x8000
    pop bp
    ret

; void xj_cxreset(void)
_xj_cxreset:
    mov bx, _xj_cx
    mov cx, 19
    xor al, al
.z: mov [bx], al
    inc bx
    loop .z
    mov byte [_xj_cx+0], 4*2
    mov byte [_xj_cx+17], 3*2
    mov byte [_xj_cx+18], 46*2
    ret

; pdw xj_mqdec(pdw cx)
_xj_mqdec:
    push bp
    mov bp, sp
    push si
    push di
    mov bx, [bp+4]
    xor ah, ah
    mov al, [_xj_cx+bx]
    mov si, ax                          ; state = index*2 + mps
    mov di, ax
    shl di, 1
    mov dx, [_xj_qe94+di]               ; Qe
    mov ax, [_xj_a]
    sub ax, dx
    cmp [_xj_chi], dx
    jae .mpsp
    mov [_xj_a], dx                     ; the LPS interval: A = Qe
    cmp ax, dx
    jb .la
    mov di, si
    and di, 1
    xor di, 1                           ; D = 1 - mps
    mov al, [_xj_nlps94+si]
    jmp short .lset
.la:
    mov di, si
    and di, 1                           ; D = mps
    mov al, [_xj_nmps94+si]
.lset:
    mov [_xj_cx+bx], al
    call .renorm
    jmp short .done
.mpsp:
    sub [_xj_chi], dx
    mov [_xj_a], ax
    test ax, 0x8000
    jnz .mnr
    cmp ax, dx
    jb .mlps
    mov di, si
    and di, 1
    mov al, [_xj_nmps94+si]
    jmp short .mset
.mlps:
    mov di, si
    and di, 1
    xor di, 1
    mov al, [_xj_nlps94+si]
.mset:
    mov [_xj_cx+bx], al
    call .renorm
    jmp short .done
.mnr:
    mov di, si
    and di, 1
.done:
    mov ax, di
    pop di
    pop si
    pop bp
    ret
.renorm:
.r1:
    cmp byte [_xj_ct], 0
    jne .r2
    call xj_bytein
.r2:
    shl word [_xj_a], 1
    shl word [_xj_clo], 1
    rcl word [_xj_chi], 1
    dec byte [_xj_ct]
    test word [_xj_a], 0x8000
    jz .r1
    ret

; ---- lifting --------------------------------------------------------------------
; dst[i] -= ((a[i] + b[i]) * k + 8192) >> 14.   DS:DI dst, DS:SI a, DS:BX b, CX n, BP k
xj_lift_core:
    jcxz .e
.l: mov ax, [si]
    add ax, [bx]
    imul bp
    add ax, 8192
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    sub [di], dx
    add si, 2
    add bx, 2
    add di, 2
    loop .l
.e: ret

; 5/3: mode 0  dst -= (a + b + 2) >> 2     mode 1  dst += (a + b) >> 1     (BP = mode)
xj_l53_core:
    jcxz .e
.l: mov ax, [si]
    add ax, [bx]
    test bp, bp
    jnz .m1
    add ax, 2
    sar ax, 1
    sar ax, 1
    sub [di], ax
    jmp short .nx
.m1:
    sar ax, 1
    add [di], ax
.nx:
    add si, 2
    add bx, 2
    add di, 2
    loop .l
.e: ret

; dst[i] = (dst[i] * k + 8192) >> 14.   DS:DI dst, CX n, BP k
xj_scal_core:
    jcxz .e
.l: mov ax, [di]
    imul bp
    add ax, 8192
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov [di], dx
    add di, 2
    loop .l
.e: ret

; void xj_lift_n(pdi *dst, pdi *a, pdi *b, pdw n, pdi k)
_xj_lift_n:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
    mov bx, [bp+8]
    mov cx, [bp+10]
    mov bp, [bp+12]
    call xj_lift_core
    pop di
    pop si
    pop bp
    ret
; void xj_lift_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdi k)
_xj_lift_f:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    mov ds, [bp+4]
    mov di, [bp+6]
    mov si, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov bp, [bp+14]
    call xj_lift_core
    pop ds
    pop di
    pop si
    pop bp
    ret
; void xj_l53_n(pdi *dst, pdi *a, pdi *b, pdw n, pdw mode)
_xj_l53_n:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
    mov bx, [bp+8]
    mov cx, [bp+10]
    mov bp, [bp+12]
    call xj_l53_core
    pop di
    pop si
    pop bp
    ret
; void xj_l53_f(pdw seg, pdw dst, pdw a, pdw b, pdw n, pdw mode)
_xj_l53_f:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    mov ds, [bp+4]
    mov di, [bp+6]
    mov si, [bp+8]
    mov bx, [bp+10]
    mov cx, [bp+12]
    mov bp, [bp+14]
    call xj_l53_core
    pop ds
    pop di
    pop si
    pop bp
    ret
; void xj_scal_n(pdi *dst, pdw n, pdi k)
_xj_scal_n:
    push bp
    mov bp, sp
    push di
    mov di, [bp+4]
    mov cx, [bp+6]
    mov bp, [bp+8]
    call xj_scal_core
    pop di
    pop bp
    ret
; void xj_scal_f(pdw seg, pdw dst, pdw n, pdi k)
_xj_scal_f:
    push bp
    mov bp, sp
    push di
    push ds
    mov ds, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    mov bp, [bp+10]
    call xj_scal_core
    pop ds
    pop di
    pop bp
    ret

; void xj_inter(pdi *out, pdi *e, pdi *o, pdw lw, pdw nh)
_xj_inter:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]
    mov si, [bp+6]
    mov bx, [bp+8]
    mov cx, [bp+12]
    jcxz .tail
.l: mov ax, [si]
    mov [di], ax
    mov ax, [bx]
    mov [di+2], ax
    add si, 2
    add bx, 2
    add di, 4
    loop .l
.tail:
    mov ax, [bp+10]
    cmp ax, [bp+12]
    jbe .done
    mov ax, [si]
    mov [di], ax
.done:
    pop di
    pop si
    pop bp
    ret

; void xj_deq(pdw *mag, pdw *flg, pdw seg, pdw off, pdw n, pdw m, pdw sh, pdw rnd)
;   plane[i] = +-min(32767, (mag[i] * m + rnd) >> sh); the sign is flg[i] bit 15
_xj_deq:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+16]
    mov [_xj_t1], ax                    ; sh
    mov ax, [bp+18]
    mov [_xj_t2], ax                    ; rnd
    mov cx, [bp+12]
    mov [_xj_t3], cx                    ; n
    mov si, [bp+4]
    mov bx, [bp+6]
    mov es, [bp+8]
    mov di, [bp+10]
    mov bp, [bp+14]                     ; m
    test cx, cx
    jz .done
.l: mov ax, [si]
    test ax, ax
    jz .zero
    mul bp                              ; dx:ax = mag * m
    add ax, [_xj_t2]
    adc dx, 0
    mov cx, [_xj_t1]
    jcxz .sd
.sl:
    shr dx, 1
    rcr ax, 1
    loop .sl
.sd:
    test dx, dx
    jnz .sat
    cmp ax, 0x7FFF
    jbe .ok
.sat:
    mov ax, 0x7FFF
.ok:
    test word [bx], 0x8000
    jz .pos
    neg ax
.pos:
    mov [es:di], ax
    jmp short .nx
.zero:
    mov word [es:di], 0
.nx:
    add si, 2
    add bx, 2
    add di, 2
    dec word [_xj_t3]
    jnz .l
.done:
    pop es
    pop di
    pop si
    pop bp
    ret

; void xj_grey(pdw sseg, pdw soff, pdw dseg, pdw doff, pdw n, pdi add, pdw sh,
;              pdw maxv, pdb *tab, pdw inv)
;   byte[i] = inv? 255 - v : v,  v = tab? tab[c] : c,  c = clamp(sat16(plane[i] + add) >> sh, 0, maxv)
_xj_grey:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    mov ax, [bp+14]
    mov [_xj_t1], ax                    ; add
    mov ax, [bp+18]
    mov [_xj_t3], ax                    ; maxv
    mov ax, [bp+20]
    mov [_xj_t4], ax                    ; tab
    mov ax, [bp+22]
    mov [_xj_t5], ax                    ; inv
    mov cx, [bp+16]                     ; sh in CL
    mov dx, [bp+12]                     ; n
    mov si, [bp+6]
    mov di, [bp+10]
    mov es, [bp+8]
    mov ds, [bp+4]
    test dx, dx
    jz .done
.l: mov ax, [si]
    add si, 2
    add ax, [cs:_xj_t1]
    jno .a
    mov ax, 0x7FFF
.a: sar ax, cl
    test ax, ax
    jns .b
    xor ax, ax
.b: cmp ax, [cs:_xj_t3]
    jbe .c
    mov ax, [cs:_xj_t3]
.c: mov bx, [cs:_xj_t4]
    test bx, bx
    jz .d
    add bx, ax
    mov al, [cs:bx]
.d: cmp word [cs:_xj_t5], 0
    je .e
    not al
.e: mov [es:di], al
    inc di
    dec dx
    jnz .l
.done:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret
section .text
