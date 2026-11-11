; =============================================================================
; os8088 - apps/pagedesk/pdxijb2.inc              PDXI's JBIG2: the MQ decoder and the
;                                                  generic-region row, in 8086 assembly
;
; Included by PDXI's assembly after the compiled C (-DJB_ASM). The C twins are
; mq_bytein() / mq_bit() / the template-0 branch of gb_nextrow() in pdxijb2.c,
; which the host harness runs and which hosttest/pdbl_asm.py compares with this
; file under Unicorn, bit for bit (PAGEDESK-SPEC 20.22.12).
;
;   _mq_bytein   no arguments; every register kept
;   _mq_bit      C: int mq_bit(pdw o): o is the context's state byte in the
;                workspace (segment [_g_wseg]); DI, SI, ES kept
;   _gb_row0     C: void gb_row0(void): one row of a template-0 generic region
;                with nominal AT pixels, no skip, into [_gb_c] from rows
;                [_gb_p1], [_gb_p2]; the 4,096-entry context hash of pdxijb2.c
;                (4-byte entries at offset 0: tag word, state byte, pad; [_jb_hmask] + 1 of them);
;                sets [_gb_fail] when full.
;
; State byte: bit 7 = occupied (hash), bits 6..1 = index in the Qe table,
; bit 0 = MPS. mqd (internal): BX = state offset, ES = workspace; returns AL =
; the bit; BX = the offset; AX, CX, DX, SI, DI trashed.
; =============================================================================
; the hash: [_jb_hmask] + 1 entries of 4 bytes at workspace offset 0: tag word, state byte, pad

section .bss
gr_ctx:     resw 1
gr_p1:      resw 1
gr_p2:      resw 1
gr_cp:      resw 1
gr_m1:      resb 1
gr_m2:      resb 1
gr_cm:      resb 1
gr_pad:     resb 1
gr_n:       resw 1
section .text

mq_qe:
    dw 0x5601, 0x3401, 0x1801, 0x0AC1, 0x0521, 0x0221, 0x5601, 0x5401, 0x4801, 0x3801
    dw 0x3001, 0x2401, 0x1C01, 0x1601, 0x5601, 0x5401, 0x5101, 0x4801, 0x3801, 0x3401
    dw 0x3001, 0x2801, 0x2401, 0x2201, 0x1C01, 0x1801, 0x1601, 0x1401, 0x1201, 0x1101
    dw 0x0AC1, 0x09C1, 0x08A1, 0x0521, 0x0441, 0x02A1, 0x0221, 0x0141, 0x0111, 0x0085
    dw 0x0049, 0x0025, 0x0015, 0x0009, 0x0005, 0x0001, 0x5601
; the next index, times two (a byte table indexed by the index)
mq_nm:
    db 2, 4, 6, 8, 10, 76, 14, 16, 18, 20, 22, 24, 26, 58, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48
    db 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 90, 92
mq_nl:
    db 2, 12, 18, 24, 58, 66, 12, 28, 28, 28, 34, 36, 40, 42, 28, 28, 30, 32, 34, 36, 38, 38, 40, 42
    db 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 92

; ---- one byte into C (T.88 E.3.4, hardware convention) -------------------------
_mq_bytein:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [_mq_cur], 0xFF
    jne .norm
    cmp word [_mq_nxt], 0x8F
    jbe .ff
    add word [_mq_clo], 0xFF00
    adc word [_mq_chi], 0
    mov word [_mq_ct], 8
    jmp .out
.ff:
    mov ax, [_mq_nxt]
    mov [_mq_cur], ax
    call _mq_fetch
    mov [_mq_nxt], ax
    mov bx, [_mq_cur]
    xor ax, ax
    mov al, bl
    rol al, 1
    and al, 1                       ; cur >> 7
    mov bh, bl
    xor bl, bl
    shl bx, 1                       ; cur << 9
    add [_mq_clo], bx
    adc [_mq_chi], ax
    mov word [_mq_ct], 7
    jmp .out
.norm:
    mov ax, [_mq_nxt]
    mov [_mq_cur], ax
    call _mq_fetch
    mov [_mq_nxt], ax
    mov bh, [_mq_cur]
    xor bl, bl                      ; cur << 8
    add [_mq_clo], bx
    adc word [_mq_chi], 0
    mov word [_mq_ct], 8
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ---- one decision (T.88 E.3.2/E.3.3) -----------------------------------------------
; BX = the state byte's offset (ES = workspace). AX = A, CX = Qe then D, DL = the state,
; DH = the new flag|MPS bits, DI = the offset, BX = index * 2.
mqd:
    mov dl, [es:bx]
    mov di, bx
    mov bl, dl
    xor bh, bh
    and bl, 0x7E
    mov cx, [cs:mq_qe + bx]
    mov ax, [_mq_a]
    sub ax, cx
    cmp [_mq_chi], cx
    jae .nolps
    cmp ax, cx
    mov ax, cx                      ; A = Qe (flags kept)
    jb .mps
    jmp .lps
.nolps:
    sub [_mq_chi], cx
    test ax, 0x8000
    jnz .fast
    cmp ax, cx
    jb .lps
.mps:                               ; D = MPS
    mov cl, dl
    and cl, 1
    mov dh, dl
    and dh, 0x81
    shr bx, 1
    mov dl, [cs:mq_nm + bx]
    jmp .rn
.lps:                               ; D = MPS ^ 1
    mov cl, dl
    and cl, 1
    xor cl, 1
    mov dh, dl
    and dh, 0x81
    test bx, bx
    jz .sw
    cmp bx, 12
    je .sw
    cmp bx, 28
    jne .nsw
.sw:
    and dh, 0x80                    ; the MPS flips to D
    or dh, cl
.nsw:
    shr bx, 1
    mov dl, [cs:mq_nl + bx]
.rn:
    cmp word [_mq_ct], 0
    jne .s
    call _mq_bytein
.s:
    shl ax, 1
    shl word [_mq_clo], 1
    rcl word [_mq_chi], 1
    dec word [_mq_ct]
    test ax, 0x8000
    jz .rn
    mov [_mq_a], ax
    or dl, dh
    mov [es:di], dl
    mov al, cl
    xor ah, ah
    ret
.fast:
    mov [_mq_a], ax
    mov al, dl
    and al, 1
    xor ah, ah
    ret

_mq_bit:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov bx, [bp + 4]
    mov es, [_g_wseg]
    call mqd
    pop es
    pop di
    pop si
    pop bp
    ret

; ---- a template-0 row ------------------------------------------------------------------
; The context is kept whole (bits 0..3 the row y, 4..10 row y-1, 11..15 row y-2, as in
; pdxijb2.c) and shifted one pixel at a time: the two incoming pixels are read with a
; rotating mask and a byte pointer (ror + adc advances both).
_gb_row0:
    push si
    push di
    push es
    push bp
    mov es, [_g_wseg]
    mov si, [_gb_p1]
    mov al, [si]
    and al, 0xF0
    xor ah, ah
    mov bx, ax                      ; pixels 0..3 of row y-1 at bits 4..7
    mov [gr_p1], si
    mov byte [gr_m1], 0x08          ; the next pixel to bring in is x+4
    mov si, [_gb_p2]
    mov al, [si]
    and al, 0xE0
    xor ah, ah
    mov cl, 6
    shl ax, cl                      ; (b >> 5) << 11: pixels 0..2 of row y-2 at bits 11..13
    or bx, ax
    mov [gr_ctx], bx
    mov [gr_p2], si
    mov byte [gr_m2], 0x10          ; x+3
    mov ax, [_gb_c]
    mov [gr_cp], ax
    mov byte [gr_cm], 0x80
    mov ax, [_gb_w]
    mov [gr_n], ax
    test ax, ax
    jz .out
.px:
    mov ax, [gr_ctx]
    test ax, ax
    jz .zero
.gen:
    mov bx, ax
    mov bl, bh
    xor bh, bh
    xor bx, ax
    and bx, [_jb_hmask]             ; h = (ctx ^ ctx >> 8) & mask
    mov di, [_jb_hmask]
    inc di
.pr:
    mov si, bx
    shl si, 1
    shl si, 1
    cmp ax, [es:si]
    jne .miss
.fnd:
    lea bx, [si + 2]
    call mqd                        ; AL = the bit
    mov dx, [gr_ctx]
    test al, al
    jz .nb
    mov di, [gr_cp]
    mov cl, [gr_cm]
    or [di], cl
    jmp .nb
.miss:
    cmp byte [es:si + 2], 0
    je .new
    inc bx
    and bx, [_jb_hmask]
    dec di
    jnz .pr
    mov word [_gb_fail], 1
    jmp .out
.new:
    mov [es:si], ax
    mov byte [es:si + 2], 0x80
    jmp .fnd

; ---- the all-white run: context 0 whose MPS is 0 is the bulk of a scan. While the
; MPS path needs no renormalisation and no 1 pixel comes into the window, decide
; and move on with everything in registers (the context stays 0).
.zero:
    test byte [es:2], 1
    jnz .gen
    mov bl, [es:2]
    xor bh, bh
    and bl, 0x7E
    mov cx, [cs:mq_qe + bx]
    mov ax, [_mq_a]
    mov dx, [_mq_chi]
    mov si, [gr_p1]
    mov di, [gr_p2]
    mov bl, [gr_m1]
    mov bh, [gr_m2]
    mov bp, [gr_n]
.zl:
    cmp dx, cx
    jb .zx
    sub ax, cx
    test ax, 0x8000
    jz .zr
    sub dx, cx
    test [si], bl
    jnz .z1
    test [di], bh
    jnz .z2
    ror bl, 1
    adc si, 0
    ror bh, 1
    adc di, 0
    dec bp
    jnz .zl
    jmp .zsave
.zr:
    add ax, cx                      ; not the plain MPS path: the general code decides this pixel
.zx:
    mov [_mq_a], ax
    mov [_mq_chi], dx
    mov [gr_p1], si
    mov [gr_p2], di
    mov [gr_m1], bl
    mov [gr_m2], bh
    mov [gr_n], bp
    call .outpos
    xor ax, ax
    jmp .gen
.z1:
    mov word [gr_ctx], 0x10
    test [di], bh
    jz .zfin
    mov word [gr_ctx], 0x810
    jmp .zfin
.z2:
    mov word [gr_ctx], 0x800
.zfin:
    ror bl, 1
    adc si, 0
    ror bh, 1
    adc di, 0
    dec bp
.zsave:
    mov [_mq_a], ax
    mov [_mq_chi], dx
    mov [gr_p1], si
    mov [gr_p2], di
    mov [gr_m1], bl
    mov [gr_m2], bh
    mov [gr_n], bp
    test bp, bp
    jz .out
    call .outpos
    jmp .px
; the output pixel's byte and mask from the pixels left
.outpos:
    mov ax, [_gb_w]
    sub ax, [gr_n]
    mov bx, ax
    mov cl, 3
    shr bx, cl
    add bx, [_gb_c]
    mov [gr_cp], bx
    and al, 7
    mov cl, al
    mov al, 0x80
    shr al, cl
    mov [gr_cm], al
    ret
.nb:
    shl dx, 1
    and dx, 0xF7EE
    or dl, al
    mov si, [gr_p1]
    mov cl, [gr_m1]
    test [si], cl
    jz .a1
    or dl, 0x10
.a1:
    ror cl, 1
    adc si, 0
    mov [gr_m1], cl
    mov [gr_p1], si
    mov si, [gr_p2]
    mov cl, [gr_m2]
    test [si], cl
    jz .a2
    or dh, 0x08
.a2:
    ror cl, 1
    adc si, 0
    mov [gr_m2], cl
    mov [gr_p2], si
    mov [gr_ctx], dx
    mov cl, [gr_cm]
    ror cl, 1
    adc word [gr_cp], 0
    mov [gr_cm], cl
    dec word [gr_n]
    jnz .px
.out:
    pop bp
    pop es
    pop di
    pop si
    ret
