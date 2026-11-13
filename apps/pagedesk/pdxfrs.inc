; =============================================================================
; os8088 - apps/pagedesk/pdxfrs.inc          PDXF's rasteriser inner loops (PAGEDESK-SPEC 20.22.8)
;
; _pdx_accline and _pdx_pack, %included by pdxf.asm. They are the assembly of
; pdx_accline() / pdx_pack() in pdxfol.c (the C twins, built on the host and held to
; these byte for byte by hosttest/pdx_primtest.py under Unicorn). The signed-area
; accumulator: pdx_acc (AC_W x AC_H words), pdx_acw / pdx_ach (the glyph's width and rows).
;
;   void pdx_accline(int x0, int y0, int x1, int y1)    26.6 device coordinates, y down
;   unsigned pdx_pack(unsigned dseg, unsigned doff)     the coverage -> 1 bit a pixel, MSB first
;
; cdecl; BP, SI, DI preserved; DS = the module's; ES is used by pdx_pack and is NOT restored
; (the C caller never holds anything in it). Frames: accline 46 + 6 pushed + the muldiv call.
; =============================================================================
PDX_ACW_CELLS equ 34                ; AC_W

; -----------------------------------------------------------------------------
; int pdx_muldiv(int a, int b, int c)
; round-half-away(a*b/c) with a 32-bit intermediate, saturating at +-32767;
; c = 0 answers 0. (The host twin is hosttest/pdx_host.c's pdx_muldiv.)
; -----------------------------------------------------------------------------
_pdx_muldiv:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+4]
    imul word [bp+6]                ; DX:AX = a*b
    mov cx, [bp+8]
    xor si, si                      ; SI bit 0 = the result is negative
    or cx, cx
    jz .zero
    jns .cpos
    neg cx
    xor si, 1
.cpos:
    or dx, dx
    jns .ppos
    neg dx
    neg ax
    sbb dx, 0
    xor si, 1
.ppos:
    mov bx, cx
    shr bx, 1
    add ax, bx                      ; + |c|/2: round half away from zero
    adc dx, 0
    cmp dx, cx
    jae .sat                        ; the quotient would not fit 16 bits
    div cx
    cmp ax, 32767
    ja .sat
    jmp .sign
.sat:
    mov ax, 32767
.sign:
    test si, 1
    jz .out
    neg ax
    jmp .out
.zero:
    xor ax, ax
.out:
    pop si
    pop bp
    ret


; ax = muldiv(a, b, c): the three arguments are words in memory or registers (cdecl: c, b, a pushed)
%macro MULDIV3 3
    push %3
    push %2
    push %1
    call _pdx_muldiv
    add sp, 6
%endmacro

_pdx_accline:
    push bp
    mov bp, sp
    push si
    push di
    sub sp, 46
    mov word [bp-2], 1                  ; dir
    mov ax, [bp+6]                      ; y0
    mov bx, [bp+10]                     ; y1
    cmp ax, bx
    jne .nz
    jmp .ret
.nz:
    jl .ordered
    mov word [bp-2], -1                 ; dir = -1: swap the ends
    mov cx, [bp+4]
    mov dx, [bp+8]
    mov [bp+4], dx
    mov [bp+8], cx
    mov [bp+6], bx
    mov [bp+10], ax
.ordered:
    mov ax, [_pdx_ach]
    mov cl, 6
    shl ax, cl
    mov [bp-40], ax                     ; ymax = ach << 6
    mov bx, [bp+10]
    or bx, bx
    jg .y1ok
    jmp .ret                            ; y1 <= 0
.y1ok:
    mov ax, [bp+6]
    cmp ax, [bp-40]
    jl .y0ok
    jmp .ret                            ; y0 >= ymax
.y0ok:
    mov ax, [bp+8]
    sub ax, [bp+4]
    mov [bp-4], ax                      ; dx
    mov ax, [bp+10]
    sub ax, [bp+6]
    mov [bp-6], ax                      ; dyl
    mov ax, [bp+6]
    or ax, ax
    jns .rpos
    xor ax, ax
    jmp .rset
.rpos:
    mov cl, 6
    shr ax, cl
.rset:
    mov [bp-8], ax                      ; row
.rowloop:
    mov ax, [bp-8]
    cmp ax, [_pdx_ach]
    jb .rowok
    jmp .ret                            ; row >= ach
.rowok:
    mov cl, 6
    shl ax, cl                          ; row << 6
    cmp ax, [bp+10]
    jl .rowin
    jmp .ret                            ; (row << 6) >= y1
.rowin:
    mov [bp-10], ax                     ; ya = row << 6
    mov bx, [bp+6]
    cmp ax, bx
    jge .yaok
    mov [bp-10], bx                     ; ya = y0
.yaok:
    add ax, 64
    mov [bp-12], ax                     ; yb = (row << 6) + 64
    mov bx, [bp+10]
    cmp ax, bx
    jle .ybok
    mov [bp-12], bx                     ; yb = y1
.ybok:
    mov ax, [bp-10]
    sub ax, [bp+6]
    MULDIV3 ax, word [bp-4], word [bp-6]
    add ax, [bp+4]
    mov [bp-42], ax                     ; xa
    mov ax, [bp-12]
    sub ax, [bp+6]
    MULDIV3 ax, word [bp-4], word [bp-6]
    add ax, [bp+4]
    mov bx, ax                          ; xb
    mov ax, [bp-42]                     ; xa
    cmp ax, bx
    jle .ordx
    xchg ax, bx
.ordx:                                  ; ax = xl, bx = xr
    mov dx, [_pdx_acw]
    mov cl, 6
    shl dx, cl                          ; acw << 6
    or ax, ax
    jns .c1
    xor ax, ax
.c1:
    cmp ax, dx
    jle .c2
    mov ax, dx
.c2:
    or bx, bx
    jns .c3
    xor bx, bx
.c3:
    cmp bx, dx
    jle .c4
    mov bx, dx
.c4:
    mov [bp-14], ax                     ; xl
    mov [bp-16], bx                     ; xr
    mov ax, [bp-12]
    sub ax, [bp-10]
    shl ax, 1
    shl ax, 1                           ; (yb - ya) * 4
    cmp word [bp-2], 0
    jge .dpos
    neg ax
.dpos:
    mov [bp-18], ax                     ; d
    mov ax, [bp-8]
    mov bx, PDX_ACW_CELLS
    mul bx                              ; row * AC_W
    shl ax, 1
    add ax, _pdx_acc
    mov [bp-38], ax                     ; &a[row][0]
    mov ax, [bp-14]
    mov cl, 6
    shr ax, cl
    mov [bp-20], ax                     ; il = xl >> 6
    shl ax, cl                          ; il << 6
    mov bx, [bp-16]
    sub bx, ax                          ; xr - (il << 6)
    cmp bx, 64
    jg .multi
    ; ---- inside one cell
    mov bx, [bp-14]
    add bx, [bp-16]
    shr bx, 1                           ; (xl + xr) >> 1: both non-negative
    sub bx, ax                          ; xm
    mov cx, 64
    MULDIV3 word [bp-18], bx, cx             ; t = muldiv(d, xm, 64)
    mov [bp-46], ax
    mov si, [bp-20]
    shl si, 1
    add si, [bp-38]                     ; &a[il]
    mov dx, [bp-18]
    sub dx, ax
    add [si], dx                        ; a[il] += d - t
    add [si+2], ax                      ; a[il+1] += t
    jmp .next
.multi:
    mov ax, [bp-16]
    sub ax, [bp-14]
    mov [bp-24], ax                     ; wd = xr - xl
    mov ax, [bp-16]
    add ax, 63
    mov cl, 6
    shr ax, cl
    mov [bp-22], ax                     ; ir = (xr + 63) >> 6
    mov ax, [bp-20]
    shl ax, cl
    mov bx, [bp-14]
    sub bx, ax                          ; xl - (il << 6)
    mov ax, 64
    sub ax, bx
    mov [bp-30], ax                     ; l0
    mov ax, [bp-22]
    dec ax
    shl ax, cl
    mov bx, [bp-16]
    sub bx, ax
    mov [bp-32], bx                     ; lr = xr - ((ir - 1) << 6)
    MULDIV3 word [bp-18], word [bp-30], word [bp-24]
    mov cx, 128
    MULDIV3 ax, word [bp-30], cx
    mov [bp-26], ax                     ; a0
    MULDIV3 word [bp-18], word [bp-32], word [bp-24]
    mov cx, 128
    MULDIV3 ax, word [bp-32], cx
    mov [bp-28], ax                     ; am
    mov si, [bp-20]
    shl si, 1
    add si, [bp-38]                     ; &a[il]
    mov ax, [bp-22]
    sub ax, [bp-20]
    cmp ax, 2
    jne .wide
    ; ---- ir == il + 2: a[il] += a0; a[il+1] += d - a0 - am; a[ir] += am
    mov ax, [bp-26]
    add [si], ax
    mov ax, [bp-18]
    sub ax, [bp-26]
    sub ax, [bp-28]
    add [si+2], ax
    mov ax, [bp-28]
    add [si+4], ax
    jmp .next
.wide:
    mov cx, 64
    MULDIV3 word [bp-18], cx, word [bp-24]
    mov [bp-34], ax                     ; sd = muldiv(d, 64, wd)
    mov ax, [bp-30]
    add ax, 32
    mov cx, 64
    MULDIV3 word [bp-34], ax, cx
    mov [bp-36], ax                     ; a1 = muldiv(sd, 32 + l0, 64)
    mov si, [bp-20]
    shl si, 1
    add si, [bp-38]                     ; &a[il] (MULDIV3 does not preserve SI)
    mov ax, [bp-26]
    add [si], ax                        ; a[il] += a0
    mov ax, [bp-36]
    sub ax, [bp-26]
    add [si+2], ax                      ; a[il+1] += a1 - a0
    mov cx, [bp-22]
    sub cx, [bp-20]
    sub cx, 3                           ; the count of whole middle cells: ir - il - 3 (>= 0)
    add si, 4                           ; &a[il + 2]
    mov bx, cx
    mov dx, [bp-34]
    or cx, cx
    jz .mdone
.mloop:
    add [si], dx                        ; a[cell] += sd
    add si, 2
    loop .mloop
.mdone:                                 ; si = &a[ir - 1]
    mov ax, bx
    imul word [bp-34]                   ; (ir - il - 3) * sd
    add ax, [bp-36]                     ; a2 = a1 + that
    mov cx, [bp-18]
    sub cx, ax
    sub cx, [bp-28]                     ; d - a2 - am
    add [si], cx                        ; a[ir - 1] += ...
    mov ax, [bp-28]
    add [si+2], ax                      ; a[ir] += am
.next:
    inc word [bp-8]
    jmp .rowloop
.ret:
    add sp, 46
    pop di
    pop si
    pop bp
    ret

; unsigned pdx_pack(unsigned dseg, unsigned doff): rows 0..ach-1 of acw cells -> bits, returns rows * ((acw+7)/8)
_pdx_pack:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov es, ax
    mov di, [bp+6]
    mov ax, [_pdx_acw]
    add ax, 7
    mov cl, 3
    shr ax, cl
    mov dx, ax                          ; wb
    mov si, _pdx_acc
    mov bx, [_pdx_ach]
    or bx, bx
    jz .done
.row:
    push bx
    xor bx, bx                          ; BX = running sum
    mov cx, [_pdx_acw]
    mov ah, 0                           ; AH = bits collected in AL
    xor al, al
    mov bp, 0                           ; BP = bit count (restored below)
    push dx
    mov dl, 0
.px:
    add bx, [si]
    add si, 2
    mov bp, bx
    or bp, bp
    jns .abs
    neg bp
.abs:
    shl al, 1
    cmp bp, PDXF_THR_ASM
    jb .nobit
    or al, 1
.nobit:
    inc dl
    cmp dl, 8
    jne .nextpx
    stosb
    xor al, al
    xor dl, dl
.nextpx:
    loop .px
    or dl, dl
    jz .rowend
    mov cl, 8
    sub cl, dl
    shl al, cl                          ; the last byte's bits to the left
    stosb
.rowend:
    pop dx
    mov ax, [_pdx_acw]
    mov cx, PDX_ACW_CELLS
    sub cx, ax                          ; skip the unused cells of the row
    shl cx, 1
    add si, cx
    pop bx
    dec bx
    jnz .row
.done:
    mov ax, [_pdx_ach]
    mul dx                              ; rows * wb
    pop di
    pop si
    pop bp
    ret
