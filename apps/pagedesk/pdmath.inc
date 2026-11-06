; =============================================================================
; os8088 - apps/pagedesk/pdmath.inc     PageDesk arithmetic, 8086 (PAGEDESK-SPEC 4, 5.3)
;
; The target body of every function in pdmath.h; apps/pagedesk/pdmath.c is its
; HOST reference and tests/pdmath/ holds the two to each other bit for bit.
; %included by each segment's shim (PDENG, PDREND, PDWRT, the primary) in the
; section .text of an assembly that has CS = DS for its data (a package, or a
; module whose door has loaded DS = CS - PAGEDESK-SPEC 3.1).
;
; CALLING CONVENTION (SPEC.md 73.3, PAGEDESK-SPEC 20.7): SmallerC cdecl. Args
; on the stack, pushed right to left, caller cleans; result in AX; labels are
; the C name with a leading underscore. BP, DS, ES, SS:SP and DF are
; preserved; AX BX CX DX SI DI are the caller's to lose. Struct arguments are
; NEAR pointers into DS (pdmath.h). A result pointer may equal an operand.
;
; STATE: one word, pdm_ovfw (the sticky overflow flag), in THIS assembly's
; section .bss. The shim zeroes its bss on first entry (PAGEDESK-SPEC 3.1), so
; it starts clear; each segment keeps its own copy.
;
; GUARD AND SIZE: the file is safe to include twice in one assembly. A segment
; that needs only part of it defines, BEFORE the include, any of
;   PDM_SKIP_U32  PDM_SKIP_PARSE  PDM_SKIP_MAT  PDM_SKIP_VIEW  PDM_SKIP_FAR
;   PDM_SKIP_CRC
; to leave that group out (PARSE = pdm_fx_parse + pdm_fx_todec; the u32 group
; also holds pdm_u32_todec, which PARSE does not need). Dependencies: VIEW and
; MAT need only the always-present fx core. Calling a skipped function is a
; link error, not a silent miss.
;
; THE PINNED SEMANTICS are in pdmath.c's header and PAGEDESK-SPEC 4.2; the
; ones this file had to settle are listed in PAGEDESK-SPEC 20.7.
; =============================================================================
%ifndef PDMATH_INC
%define PDMATH_INC 1

cpu 8086

; ---- constants (checked against pdabi.h by tools/os88pdabi.py) -------------
PDMI_E_OK       equ 0                   ; PDE_OK
PDMI_E_SYNTAX   equ 1                   ; PDE_SYNTAX
PDMI_E_RANGE    equ 32                  ; PDE_RANGE
; struct pd_view field offsets (pdabi.h; the checker pins them too)
PDMI_V_X0       equ 0
PDMI_V_Y0       equ 2
PDMI_V_X1       equ 4
PDMI_V_Y1       equ 6
PDMI_V_ROT      equ 8
PDMI_V_ZOOM     equ 10
PDMI_V_ASPN     equ 12
PDMI_V_ASPD     equ 14
PDMI_V_OX       equ 16
PDMI_V_OY       equ 18

section .bss
pdm_ovfw:       resw 1
section .text

; -----------------------------------------------------------------------------
; internal helpers (pdmi_*). Register contracts are stated at each.
; -----------------------------------------------------------------------------
pdmi_setovf:                            ; sets the sticky flag; preserves all
        mov     word [pdm_ovfw], 1
        ret

_pdm_ovf:
        mov     ax, [pdm_ovfw]
        ret
_pdm_ovf_clear:
        mov     word [pdm_ovfw], 0
        ret

; store DX:AX through the result pointer at [bp+4] and return (u32 and fx)
pdmi_ret_r:
        mov     di, [bp+4]
        mov     [di], ax
        mov     [di+2], dx
        pop     bp
        ret

; after a 32-bit add/sub with OF valid: clamp DX:AX to +-0x7FFFFFFF (fxs)
pdmi_fxchk:
        jno     .nof
        test    dx, dx                  ; the result's sign is the WRONG one
        js      .pos
        mov     dx, 0x8000              ; true value was below -2^31
        mov     ax, 1
        jmp     pdmi_setovf
.pos:   mov     dx, 0x7FFF
        mov     ax, 0xFFFF
        jmp     pdmi_setovf
.nof:   cmp     dx, 0x8000
        jne     .ok
        test    ax, ax
        jnz     .ok
        mov     ax, 1                   ; exactly -2^31 -> -0x7FFFFFFF
        jmp     pdmi_setovf
.ok:    ret

; DX:AX (signed 32) -> AX clamped to [-32767, 32767]; sticky ovf when it was.
pdmi_addclamp:                          ; DX:AX += sign-extended CX, then clamp
        add     ax, cx
        adc     dx, 0
        test    cx, cx
        jns     pdmi_clamp16
        dec     dx
pdmi_clamp16:
        mov     cx, dx
        cwd
        cmp     cx, dx
        jne     .big
        cmp     ax, 0x8000
        jne     .ok
        mov     ax, 0x8001
        jmp     pdmi_setovf
.ok:    ret
.big:   call    pdmi_setovf
        test    cx, cx
        js      .neg
        mov     ax, 0x7FFF
        ret
.neg:   mov     ax, 0x8001
        ret

; DX:AX = AX - CX for two int16 (17-bit result). Preserves BX, SI, DI.
pdmi_sub16:
        cwd
        sub     ax, cx
        sbb     dx, 0
        test    cx, cx
        jns     .r
        inc     dx
.r:     ret

; -----------------------------------------------------------------------------
; unsigned 32
; -----------------------------------------------------------------------------
%ifndef PDM_SKIP_U32

_pdm_u32_set:                           ; (r, hi, lo)
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        mov     ax, [bp+8]
        jmp     pdmi_ret_r

_pdm_u32_add:                           ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     si, [bp+8]
        mov     ax, [bx]
        mov     dx, [bx+2]
        add     ax, [si]
        adc     dx, [si+2]
        jnc     pdmi_ret_r
.sat:   mov     ax, 0xFFFF
        mov     dx, ax
        call    pdmi_setovf
        jmp     pdmi_ret_r

_pdm_u32_sub:                           ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     si, [bp+8]
        mov     ax, [bx]
        mov     dx, [bx+2]
        sub     ax, [si]
        sbb     dx, [si+2]
        jnc     pdmi_ret_r
        xor     ax, ax
        xor     dx, dx
        call    pdmi_setovf
        jmp     pdmi_ret_r

_pdm_u32_add16:                         ; (r, n): r += n
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     ax, [bx]
        mov     dx, [bx+2]
        add     ax, [bp+6]
        adc     dx, 0
        jnc     pdmi_ret_r
        jmp     _pdm_u32_add.sat

_pdm_u32_cmp:                           ; (a, b) -> -1, 0, 1
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     si, [bp+6]
        mov     ax, [bx+2]
        cmp     ax, [si+2]
        jne     .d
        mov     ax, [bx]
        cmp     ax, [si]
.d:     je      .z
        ja      .p
        mov     ax, 0xFFFF
        pop     bp
        ret
.z:     xor     ax, ax
        pop     bp
        ret
.p:     mov     ax, 1
        pop     bp
        ret

_pdm_u32_mul16:                         ; (r, a, b): r = a*b, never overflows
        push    bp
        mov     bp, sp
        mov     ax, [bp+6]
        mul     word [bp+8]
        jmp     pdmi_ret_r

_pdm_u32_divmod16:                      ; (q, a, d) -> a % d
        push    bp
        mov     bp, sp
        mov     cx, [bp+8]
        jcxz    .zero
        mov     bx, [bp+6]
        mov     ax, [bx+2]
        xor     dx, dx
        div     cx
        mov     si, ax
        mov     ax, [bx]
        div     cx
        mov     di, [bp+4]
        mov     [di], ax
        mov     [di+2], si
        mov     ax, dx
        pop     bp
        ret
.zero:  call    pdmi_setovf
        mov     di, [bp+4]
        mov     word [di], 0xFFFF
        mov     word [di+2], 0xFFFF
        xor     ax, ax
        pop     bp
        ret

_pdm_u32_shr:                           ; (r, a, n)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ax, [bx]
        mov     dx, [bx+2]
        mov     cx, [bp+8]
        cmp     cx, 32
        jae     .zero
        cmp     cx, 16
        jb      .s
        mov     ax, dx
        xor     dx, dx
        sub     cx, 16
.s:     jcxz    .done
.l:     shr     dx, 1
        rcr     ax, 1
        loop    .l
.done:  jmp     pdmi_ret_r
.zero:  xor     ax, ax
        xor     dx, dx
        jmp     pdmi_ret_r

_pdm_u32_shl:                           ; (r, a, n): bits shifted out set ovf
        push    bp
        mov     bp, sp
        mov     si, [bp+6]
        mov     ax, [si]
        mov     dx, [si+2]
        mov     cx, [bp+8]
        cmp     cx, 32
        jae     .big
        xor     bx, bx
        jcxz    .done
.l:     shl     ax, 1
        rcl     dx, 1
        adc     bx, 0
        loop    .l
.done:  test    bx, bx
        jz      pdmi_ret_r
.sat:   mov     ax, 0xFFFF
        mov     dx, ax
        call    pdmi_setovf
        jmp     pdmi_ret_r
.big:   mov     bx, ax
        or      bx, dx
        jnz     .sat
        jmp     pdmi_ret_r              ; 0 shifted by any amount is 0

; (buf, a, width) -> digits written; buf[n] = 0
_pdm_u32_todec:                         ; STKBALANCE-LOOP: digits pushed in one counted loop, popped in another
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ax, [bx]
        mov     dx, [bx+2]
        xor     cx, cx
        mov     si, 10
.d:     mov     bx, ax
        mov     ax, dx
        xor     dx, dx
        div     si
        mov     di, ax                  ; high quotient
        mov     ax, bx
        div     si                      ; AX = low quotient, DX = digit
        push    dx
        mov     dx, di
        inc     cx
        mov     bx, ax
        or      bx, dx
        jnz     .d
        mov     bx, [bp+8]              ; zero-pad to min(width, 10)
        cmp     bx, 10
        jbe     .p
        mov     bx, 10
.p:     cmp     cx, bx
        jae     .w
        xor     ax, ax
        push    ax
        inc     cx
        jmp     .p
.w:     mov     di, [bp+4]
        mov     bx, cx
.o:     pop     ax
        add     al, '0'
        mov     [di], al
        inc     di
        loop    .o
        mov     byte [di], 0
        mov     ax, bx
        pop     bp
        ret

%endif ; PDM_SKIP_U32

; -----------------------------------------------------------------------------
; signed 16.16 (always present)
; -----------------------------------------------------------------------------
_pdm_fx_int:                            ; (r, n)
        push    bp
        mov     bp, sp
        mov     dx, [bp+6]
        xor     ax, ax
        cmp     dx, 0x8000
        jne     pdmi_ret_r
        mov     ax, 1                   ; -32768 * 65536 saturates
        call    pdmi_setovf
        jmp     pdmi_ret_r

_pdm_fx_floor:                          ; (a) -> the high word IS the floor
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     ax, [bx+2]
        pop     bp
        ret

_pdm_fx_round:                          ; (a): nearest, ties away from zero
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     ax, [bx]
        mov     dx, [bx+2]
        test    dx, dx
        jns     .p
        neg     dx
        neg     ax
        sbb     dx, 0
        add     ax, 0x8000
        adc     dx, 0
        mov     ax, dx
        neg     ax
        pop     bp
        ret
.p:     add     ax, 0x8000
        adc     dx, 0
        mov     ax, dx
        pop     bp
        ret

_pdm_fx_add:                            ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     si, [bp+8]
        mov     ax, [bx]
        mov     dx, [bx+2]
        add     ax, [si]
        adc     dx, [si+2]
        call    pdmi_fxchk
        jmp     pdmi_ret_r

_pdm_fx_sub:                            ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     si, [bp+8]
        mov     ax, [bx]
        mov     dx, [bx+2]
        sub     ax, [si]
        sbb     dx, [si+2]
        call    pdmi_fxchk
        jmp     pdmi_ret_r

_pdm_fx_neg:                            ; (r, a): -(-2^31) saturates
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ax, [bx]
        mov     dx, [bx+2]
        cmp     dx, 0x8000
        jne     .n
        test    ax, ax
        jnz     .n
        mov     ax, 0xFFFF
        mov     dx, 0x7FFF
        call    pdmi_setovf
        jmp     pdmi_ret_r
.n:     neg     dx
        neg     ax
        sbb     dx, 0
        jmp     pdmi_ret_r

_pdm_fx_cmp:                            ; (a, b) -> -1, 0, 1 (signed)
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     si, [bp+6]
        mov     ax, [bx+2]
        cmp     ax, [si+2]
        jl      .lt
        jg      .gt
        mov     ax, [bx]
        cmp     ax, [si]
        jb      .lt
        ja      .gt
        xor     ax, ax
        pop     bp
        ret
.lt:    mov     ax, 0xFFFF
        pop     bp
        ret
.gt:    mov     ax, 1
        pop     bp
        ret

; fx multiply. In: DX:AX = x, CX:BX = y (signed 16.16). Out: DX:AX, truncated
; toward zero, saturated to +-0x7FFFFFFF with the sticky flag. Preserves SI/DI.
pdmi_fxmul:
        push    bp
        push    si
        push    di
        mov     di, dx
        xor     di, cx                  ; bit 15 = the signs differ
        test    dx, dx
        jns     .xp
        neg     dx
        neg     ax
        sbb     dx, 0
.xp:    test    cx, cx
        jns     .yp
        neg     cx
        neg     bx
        sbb     cx, 0
.yp:    push    di                      ; frame: [bp] yl [bp+2] yh [bp+4] xl
        push    dx                      ;        [bp+6] xh [bp+8] sign
        push    ax
        push    cx
        push    bx
        mov     bp, sp
        mov     ax, [bp+4]
        mul     word [bp]               ; xl*yl: only its high word survives
        mov     si, dx                  ; limb 1
        xor     di, di                  ; limb 2
        xor     bx, bx                  ; limb 3
        mov     ax, [bp+4]
        mul     word [bp+2]             ; xl*yh
        add     si, ax
        adc     di, dx
        adc     bx, 0
        mov     ax, [bp+6]
        mul     word [bp]               ; xh*yl
        add     si, ax
        adc     di, dx
        adc     bx, 0
        mov     ax, [bp+6]
        mul     word [bp+2]             ; xh*yh
        add     di, ax
        adc     bx, dx
        test    bx, bx                  ; q = bx:di:si; q > 0x7FFFFFFF ?
        jnz     .ov
        cmp     di, 0x8000
        jae     .ov
        mov     ax, si
        mov     dx, di
        jmp     .sg
.ov:    call    pdmi_setovf
        mov     ax, 0xFFFF
        mov     dx, 0x7FFF
.sg:    test    word [bp+8], 0x8000
        jz      .done
        neg     dx
        neg     ax
        sbb     dx, 0
.done:  add     sp, 10
        pop     di
        pop     si
        pop     bp
        ret

_pdm_fx_mul:                            ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     si, [bp+6]
        mov     di, [bp+8]
        mov     ax, [si]
        mov     dx, [si+2]
        mov     bx, [di]
        mov     cx, [di+2]
        call    pdmi_fxmul
        jmp     pdmi_ret_r

; unsigned 48 / 32 division.  Stack (pushed in this order): N2, N1, N0, Dh, Dl
; (N = N2:N1:N0, D = Dh:Dl, D != 0); callee pops them. Out: Q = DX:CX:BX
; (Q2:Q1:Q0), AX nonzero iff the remainder is.  The CALLER removes the five
; words (add sp, 10): stkbalance.py cannot see a callee that pops its arguments.
; Fast path (Dh = 0): three DIVs.  Slow path: 48 restoring steps (~2.4 ms on
; an 8088, estimated; only fx_div with a divisor >= 1.0 takes it).
pdmi_udiv48:
        push    bp
        mov     bp, sp
        mov     si, [bp+4]
        cmp     word [bp+6], 0
        jne     .slow
        xor     dx, dx
        mov     ax, [bp+12]
        div     si
        mov     di, ax                  ; Q2
        mov     ax, [bp+10]
        div     si
        mov     cx, ax                  ; Q1
        mov     ax, [bp+8]
        div     si
        mov     bx, ax                  ; Q0
        mov     ax, dx
        mov     dx, di
        pop     bp
        ret
.slow:  xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        xor     si, si                  ; R = di:si
        xor     di, di
        mov     ax, 48
.lp:    shl     word [bp+8], 1
        rcl     word [bp+10], 1
        rcl     word [bp+12], 1
        rcl     si, 1
        rcl     di, 1
        jc      .sub
        cmp     di, [bp+6]
        jb      .no
        ja      .sub
        cmp     si, [bp+4]
        jb      .no
.sub:   sub     si, [bp+4]
        sbb     di, [bp+6]
        stc
        jmp     .q
.no:    clc
.q:     rcl     bx, 1
        rcl     cx, 1
        rcl     dx, 1
        dec     ax
        jnz     .lp
        mov     ax, si
        or      ax, di
        pop     bp
        ret

; DX:CX:BX = AX... helpers for the view and for fx_div -------------------------
_pdm_fx_div:                            ; (r, a, b)
        push    bp
        mov     bp, sp
        mov     si, [bp+6]
        mov     di, [bp+8]
        mov     ax, [si]
        mov     dx, [si+2]
        mov     bx, [di]
        mov     cx, [di+2]
        mov     si, cx
        or      si, bx
        jnz     .nz
        call    pdmi_setovf             ; y = 0: saturate toward x's sign
        test    dx, dx
        js      .ns
        mov     dx, 0x7FFF
        mov     ax, 0xFFFF
        jmp     pdmi_ret_r
.ns:    mov     dx, 0x8000
        mov     ax, 1
        jmp     pdmi_ret_r
.nz:    mov     di, dx
        xor     di, cx
        push    di                      ; sign
        test    dx, dx
        jns     .xp
        neg     dx
        neg     ax
        sbb     dx, 0
.xp:    test    cx, cx
        jns     .yp
        neg     cx
        neg     bx
        sbb     cx, 0
.yp:    push    dx                      ; N2 = |x|.hi
        push    ax                      ; N1 = |x|.lo
        xor     si, si
        push    si                      ; N0 = 0   (N = |x| << 16)
        push    cx                      ; Dh
        push    bx                      ; Dl
        call    pdmi_udiv48
        add     sp, 10
        test    dx, dx                  ; q > 0x7FFFFFFF ?
        jnz     .ov
        cmp     cx, 0x8000
        jae     .ov
        mov     dx, cx
        mov     ax, bx
        jmp     .sg
.ov:    call    pdmi_setovf
        mov     ax, 0xFFFF
        mov     dx, 0x7FFF
.sg:    pop     di
        test    di, di
        jns     pdmi_ret_r
        neg     dx
        neg     ax
        sbb     dx, 0
        jmp     pdmi_ret_r

%ifndef PDM_SKIP_PARSE

; DX:AX *= 10. Clobbers BX, CX.
pdmi_mul10:
        shl     ax, 1
        rcl     dx, 1
        mov     bx, ax
        mov     cx, dx
        shl     ax, 1
        rcl     dx, 1
        shl     ax, 1
        rcl     dx, 1
        add     ax, bx
        adc     dx, cx
        ret

; (r, s, len) -> PDE_OK / PDE_SYNTAX / PDE_RANGE
_pdm_fx_parse:
        push    bp
        mov     bp, sp
        sub     sp, 16                  ; [bp-2] neg [bp-4] ip [bp-6] fn.lo
        xor     ax, ax                  ; [bp-8] fn.hi [bp-10] fd.lo [bp-12] fd.hi
        mov     [bp-2], ax              ; [bp-14] any [bp-16] digit
        mov     [bp-4], ax
        mov     [bp-6], ax
        mov     [bp-8], ax
        mov     [bp-12], ax
        mov     [bp-14], ax
        mov     word [bp-10], 1
        mov     si, [bp+6]
        mov     di, si
        add     di, [bp+8]              ; end of the text
        cmp     si, di
        jae     .int
        mov     al, [si]
        cmp     al, '-'
        jne     .plus
        mov     word [bp-2], 1
        inc     si
        jmp     .int
.plus:  cmp     al, '+'
        jne     .int
        inc     si
.int:   cmp     si, di
        jae     .frac
        mov     al, [si]
        sub     al, '0'
        cmp     al, 9
        ja      .frac
        xor     ah, ah
        mov     bx, [bp-4]
        cmp     bx, 4000
        jae     .cap
        mov     dx, bx
        shl     bx, 1
        shl     bx, 1
        add     bx, dx
        shl     bx, 1
        add     bx, ax
        jmp     .st
.cap:   mov     bx, 40000
.st:    mov     [bp-4], bx
        mov     word [bp-14], 1
        inc     si
        jmp     .int
.frac:  cmp     si, di
        jae     .done
        cmp     byte [si], '.'
        jne     .done
        inc     si
.fl:    cmp     si, di
        jae     .done
        mov     al, [si]
        sub     al, '0'
        cmp     al, 9
        ja      .done
        xor     ah, ah
        mov     [bp-16], ax
        mov     dx, [bp-12]             ; take the digit while fd < 100000000
        cmp     dx, 0x05F5
        ja      .skip
        jb      .take
        cmp     word [bp-10], 0xE100
        jae     .skip
.take:  mov     ax, [bp-6]
        mov     dx, [bp-8]
        call    pdmi_mul10
        add     ax, [bp-16]
        adc     dx, 0
        mov     [bp-6], ax
        mov     [bp-8], dx
        mov     ax, [bp-10]
        mov     dx, [bp-12]
        call    pdmi_mul10
        mov     [bp-10], ax
        mov     [bp-12], dx
.skip:  mov     word [bp-14], 1
        inc     si
        jmp     .fl
.done:  cmp     word [bp-14], 0
        je      .syn
        cmp     si, di
        jne     .syn
        ; q = floor(fn * 65536 / fd) in 16 steps; round up when 2*rem >= fd
        mov     ax, [bp-6]
        mov     dx, [bp-8]
        xor     bx, bx
        mov     cx, 16
.dl:    shl     ax, 1
        rcl     dx, 1
        shl     bx, 1
        cmp     dx, [bp-12]
        jb      .nd
        ja      .sb
        cmp     ax, [bp-10]
        jb      .nd
.sb:    sub     ax, [bp-10]
        sbb     dx, [bp-12]
        inc     bx
.nd:    loop    .dl
        shl     ax, 1
        rcl     dx, 1
        mov     cx, [bp-4]              ; integer part
        cmp     dx, [bp-12]
        jb      .nr
        ja      .ru
        cmp     ax, [bp-10]
        jb      .nr
.ru:    add     bx, 1
        adc     cx, 0
.nr:    test    cx, 0x8000              ; raw >= 0x80000000: out of range
        jnz     .rng
        mov     ax, bx
        mov     dx, cx
        xor     bx, bx                  ; return value PDE_OK
        jmp     .sgn
.rng:   mov     ax, 0xFFFF
        mov     dx, 0x7FFF
        mov     bx, PDMI_E_RANGE
.sgn:   cmp     word [bp-2], 0
        je      .out
        neg     dx
        neg     ax
        sbb     dx, 0
.out:   mov     di, [bp+4]
        mov     [di], ax
        mov     [di+2], dx
        mov     ax, bx
        add     sp, 16
        pop     bp
        ret
.syn:   mov     di, [bp+4]
        mov     word [di], 0
        mov     word [di+2], 0
        mov     ax, PDMI_E_SYNTAX
        add     sp, 16
        pop     bp
        ret

; (buf, a) -> length.  At most 4 fraction digits, rounded; "-0" becomes "0".
_pdm_fx_todec:                          ; STKBALANCE-LOOP: digits pushed in a counted loop, popped in another
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     ax, [bx]
        mov     dx, [bx+2]
        xor     si, si                  ; SI = sign flag
        test    dx, dx
        jns     .p
        inc     si
        neg     dx
        neg     ax
        sbb     dx, 0
.p:     push    dx                      ; vi (0..32768)
        mov     cx, 10000
        mul     cx                      ; vf * 10000
        add     ax, 32768
        adc     dx, 0
        mov     bx, dx                  ; fr = (vf*10000 + 32768) >> 16
        pop     ax                      ; vi
        cmp     bx, 10000
        jb      .nc
        xor     bx, bx                  ; rounded up to a whole unit
        inc     ax
.nc:    mov     di, ax
        or      di, bx
        jnz     .keep
        xor     si, si                  ; a negative value that rounds to 0
.keep:  mov     di, [bp+4]
        test    si, si
        jz      .ip
        mov     byte [di], '-'
        inc     di
.ip:    xor     cx, cx                  ; integer digits, via the stack
        mov     si, 10
.d:     xor     dx, dx
        div     si
        push    dx
        inc     cx
        test    ax, ax
        jnz     .d
.o:     pop     ax
        add     al, '0'
        mov     [di], al
        inc     di
        loop    .o
        test    bx, bx
        jz      .end
        mov     byte [di], '.'
        inc     di
        mov     cx, 4                   ; drop trailing zeros of the fraction
.z:     mov     ax, bx
        xor     dx, dx
        div     si
        test    dx, dx
        jnz     .fd
        mov     bx, ax
        dec     cx
        jmp     .z
.fd:    mov     ax, bx                  ; CX digits of BX, zero padded
        mov     bx, cx
.f1:    xor     dx, dx
        div     si
        push    dx
        loop    .f1
        mov     cx, bx
.f2:    pop     ax
        add     al, '0'
        mov     [di], al
        inc     di
        loop    .f2
.end:   mov     byte [di], 0
        mov     ax, di
        sub     ax, [bp+4]
        pop     bp
        ret

%endif ; PDM_SKIP_PARSE

_pdm_fx_to_pt16:                        ; (a) -> pt16, nearest, saturating
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        mov     ax, [bx]
        mov     dx, [bx+2]
        mov     si, dx
        test    dx, dx
        jns     .p
        neg     dx
        neg     ax
        sbb     dx, 0
.p:     add     ax, 2048
        adc     dx, 0
        cmp     dx, 0x0800              ; q = (m + 2048) >> 12 > 32767 ?
        jae     .ov
        mov     cl, 12
        shr     ax, cl
        mov     cl, 4
        shl     dx, cl
        or      ax, dx
        jmp     .sg
.ov:    call    pdmi_setovf
        mov     ax, 32767
.sg:    test    si, si
        jns     .r
        neg     ax
.r:     pop     bp
        ret

_pdm_pt16_to_fx:                        ; (r, p)
        push    bp
        mov     bp, sp
        mov     ax, [bp+6]
        mov     dx, ax
        mov     cl, 4
        sar     dx, cl
        mov     cl, 12
        shl     ax, cl
        jmp     pdmi_ret_r

; -----------------------------------------------------------------------------
; matrices
; -----------------------------------------------------------------------------
%ifndef PDM_SKIP_MAT

_pdm_mat_ident:                         ; (m)
        push    bp
        mov     bp, sp
        mov     bx, [bp+4]
        xor     ax, ax
        mov     cx, 12
.l:     mov     [bx], ax
        inc     bx
        inc     bx
        loop    .l
        mov     bx, [bp+4]
        mov     word [bx+2], 1
        mov     word [bx+14], 1
        pop     bp
        ret

; t.X = a.p * b.q + a.r * b.s   (SI = a, DI = b; result into the frame)
%macro PDMI_MAC2 5                      ; dst, a1, b1, a2, b2
        mov     ax, [si+%2]
        mov     dx, [si+%2+2]
        mov     bx, [di+%3]
        mov     cx, [di+%3+2]
        call    pdmi_fxmul
        push    dx
        push    ax
        mov     ax, [si+%4]
        mov     dx, [si+%4+2]
        mov     bx, [di+%5]
        mov     cx, [di+%5+2]
        call    pdmi_fxmul
        pop     bx
        pop     cx
        add     ax, bx
        adc     dx, cx
        call    pdmi_fxchk
%endmacro

_pdm_mat_mul:                           ; (r, a, b): r = a x b
        push    bp
        mov     bp, sp
        sub     sp, 24
        mov     si, [bp+6]
        mov     di, [bp+8]
        PDMI_MAC2 0, 0, 0, 4, 8
        mov     [bp-24], ax
        mov     [bp-22], dx
        PDMI_MAC2 4, 0, 4, 4, 12
        mov     [bp-20], ax
        mov     [bp-18], dx
        PDMI_MAC2 8, 8, 0, 12, 8
        mov     [bp-16], ax
        mov     [bp-14], dx
        PDMI_MAC2 12, 8, 4, 12, 12
        mov     [bp-12], ax
        mov     [bp-10], dx
        PDMI_MAC2 16, 16, 0, 20, 8
        add     ax, [di+16]
        adc     dx, [di+18]
        call    pdmi_fxchk
        mov     [bp-8], ax
        mov     [bp-6], dx
        PDMI_MAC2 20, 16, 4, 20, 12
        add     ax, [di+20]
        adc     dx, [di+22]
        call    pdmi_fxchk
        mov     [bp-4], ax
        mov     [bp-2], dx
        mov     di, [bp+4]              ; copy out (r may alias a or b)
        lea     si, [bp-24]
        mov     cx, 12
.c:     mov     ax, [ss:si]
        mov     [di], ax
        inc     si
        inc     si
        inc     di
        inc     di
        loop    .c
        add     sp, 24
        pop     bp
        ret

_pdm_mat_apply:                         ; (x, y, m): (x,y) <- (x,y) x m
        push    bp
        mov     bp, sp
        sub     sp, 4
        mov     si, [bp+4]
        mov     ax, [si]
        mov     dx, [si+2]
        mov     di, [bp+8]              ; DI = m
        mov     bx, [di]                ; x * m.a
        mov     cx, [di+2]
        call    pdmi_fxmul
        push    dx
        push    ax
        mov     si, [bp+6]
        mov     ax, [si]                ; y * m.c
        mov     dx, [si+2]
        mov     di, [bp+8]
        mov     bx, [di+8]
        mov     cx, [di+10]
        call    pdmi_fxmul
        pop     bx
        pop     cx
        add     ax, bx
        adc     dx, cx
        call    pdmi_fxchk
        mov     di, [bp+8]
        add     ax, [di+16]
        adc     dx, [di+18]
        call    pdmi_fxchk
        mov     [bp-4], ax              ; nx
        mov     [bp-2], dx
        mov     si, [bp+4]
        mov     ax, [si]                ; x * m.b
        mov     dx, [si+2]
        mov     bx, [di+4]
        mov     cx, [di+6]
        call    pdmi_fxmul
        push    dx
        push    ax
        mov     si, [bp+6]
        mov     ax, [si]                ; y * m.d
        mov     dx, [si+2]
        mov     di, [bp+8]
        mov     bx, [di+12]
        mov     cx, [di+14]
        call    pdmi_fxmul
        pop     bx
        pop     cx
        add     ax, bx
        adc     dx, cx
        call    pdmi_fxchk
        mov     di, [bp+8]
        add     ax, [di+20]
        adc     dx, [di+22]
        call    pdmi_fxchk
        mov     si, [bp+6]              ; *y = ny
        mov     [si], ax
        mov     [si+2], dx
        mov     si, [bp+4]              ; *x = nx
        mov     ax, [bp-4]
        mov     dx, [bp-2]
        mov     [si], ax
        mov     [si+2], dx
        add     sp, 4
        pop     bp
        ret

%endif ; PDM_SKIP_MAT

; -----------------------------------------------------------------------------
; the view transform (PAGEDESK-SPEC 5.3)
; -----------------------------------------------------------------------------
%ifndef PDM_SKIP_VIEW

; Floor/ceiling helpers return DX:AX = a signed value whose magnitude is
; SATURATED at 65536: anything that large lands outside int16 once the +-32768
; page-box or origin offset is added, so the clamp gives the same answer.

; sign handling for a magnitude Q = DX:CX:BX with remainder flag AX and the
; sign word SI (bit 15 set = negative): FLOOR of +-Q, CEILING of +-Q
pdmi_flsign:
        or      dx, cx
        jnz     pdmi_satsign
        test    si, si
        jns     .pos
        test    ax, ax
        jz      .nr
        inc     bx
        jnz     .nr
        xor     ax, ax                  ; -(65535 + 1)
        mov     dx, 0xFFFF
        ret
.nr:    xor     dx, dx
        mov     ax, bx
        neg     ax
        sbb     dx, 0
        ret
.pos:   xor     dx, dx
        mov     ax, bx
        ret
pdmi_satsign:
        xor     ax, ax
        mov     dx, 1
        test    si, si
        jns     .r
        mov     dx, 0xFFFF
.r:     ret

pdmi_clsign:
        or      dx, cx
        jnz     pdmi_satsign
        test    si, si
        js      .neg
        test    ax, ax
        jz      .p
        inc     bx
        jnz     .p
        xor     ax, ax
        mov     dx, 1
        ret
.p:     xor     dx, dx
        mov     ax, bx
        ret
.neg:   xor     dx, dx
        mov     ax, bx
        neg     ax
        sbb     dx, 0
        ret

; DX:AX (|.| <= 65535) -> AX = magnitude, SI = sign word
pdmi_absw:
        mov     si, dx
        test    dx, dx
        jns     .r
        neg     ax
.r:     ret

; DX:AX = DX:AX * CX as a 48-bit DX:CX:AX (N2, N1, N0).  Clobbers SI, DI.
pdmi_mul3216:
        mov     si, dx
        mul     cx
        mov     di, ax
        mov     ax, si
        mov     si, dx
        mul     cx
        add     ax, si
        adc     dx, 0
        mov     cx, ax
        mov     ax, di
        ret

; floor(s * zoom / 1600).  In DX:AX = s (|s| <= 65535), BX = view.
pdmi_scx:
        call    pdmi_absw
        mul     word [bx+PDMI_V_ZOOM]
        mov     cx, ax
        mov     ax, dx
        xor     dx, dx
        mov     di, 1600
        div     di
        xchg    ax, cx
        div     di                      ; q = CX:AX, remainder DX
        mov     bx, ax
        mov     ax, dx
        xor     dx, dx
        jmp     pdmi_flsign

; DX:CX:BX (a 48-bit N) / SI (16-bit, nonzero) -> DX:CX:BX, AX = remainder.
; floor(floor(N / a) / b) = floor(N / (a * b)) for positive integers, with a
; zero remainder exactly when both steps leave none: so a divisor that is a
; PRODUCT of two words is two of these, three DIVs each, and never the 48-step
; loop (a 1600 * asp_den divisor reaches 2^26).  Clobbers DI.
pdmi_divw:
        mov     ax, dx
        xor     dx, dx
        div     si
        mov     di, ax                  ; Q2
        mov     ax, cx
        div     si
        mov     cx, ax                  ; Q1
        mov     ax, bx
        div     si
        mov     bx, ax                  ; Q0
        mov     ax, dx
        mov     dx, di
        ret

; Q = N / (a * b): the shared body of scy and icy.  In: DX:CX:AX = N, SI = a,
; and b pushed by the caller (who removes it: add sp, 2).  Out: Q = DX:CX:BX, AX
; nonzero iff there is a remainder.
pdmi_divab:
        push    bp
        mov     bp, sp
        mov     bx, ax
        call    pdmi_divw
        push    ax                      ; first remainder
        mov     si, [bp+4]              ; b
        call    pdmi_divw
        pop     si
        or      ax, si
        pop     bp
        ret

; floor(s * zoom * asp_num / (1600 * asp_den)).  In DX:AX = s, BX = view.
pdmi_scy:
        cmp     word [bx+PDMI_V_ASPD], 0
        jne     .ok
        call    pdmi_setovf             ; degenerate view: outputs 0
        xor     ax, ax
        xor     dx, dx
        ret
.ok:    call    pdmi_absw
        push    si                      ; the sign
        mul     word [bx+PDMI_V_ZOOM]
        mov     cx, [bx+PDMI_V_ASPN]
        call    pdmi_mul3216            ; N = DX:CX:AX
        push    word [bx+PDMI_V_ASPD]   ; b
        mov     si, 1600                ; a
        call    pdmi_divab
        add     sp, 2
        pop     si
        jmp     pdmi_flsign

; ceil(s * 1600 / zoom).  In DX:AX = s, BX = view (zoom != 0).
pdmi_icx:
        call    pdmi_absw
        mov     cx, 1600
        mul     cx
        mov     cx, ax
        mov     ax, dx
        xor     dx, dx
        mov     di, [bx+PDMI_V_ZOOM]
        div     di
        xchg    ax, cx
        div     di
        mov     bx, ax
        mov     ax, dx
        xor     dx, dx
        jmp     pdmi_clsign

; ceil(s * 1600 * asp_den / (zoom * asp_num)).  In DX:AX = s, BX = view.
pdmi_icy:
        call    pdmi_absw
        push    si
        mov     cx, 1600
        mul     cx
        mov     cx, [bx+PDMI_V_ASPD]
        call    pdmi_mul3216            ; N = DX:CX:AX
        push    word [bx+PDMI_V_ASPN]   ; b
        mov     si, [bx+PDMI_V_ZOOM]    ; a
        call    pdmi_divab
        add     sp, 2
        pop     si
        jmp     pdmi_clsign

; (px*, py*, v, x, y)
_pdm_view_fwd:
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        mov     dx, [bx+PDMI_V_ROT]
        cmp     dx, 90
        je      .r90
        cmp     dx, 180
        je      .r180
        cmp     dx, 270
        je      .r270
        mov     ax, [bp+10]             ; u = x - x0 ; w = y1 - y
        mov     cx, [bx+PDMI_V_X0]
        call    pdmi_sub16
        push    dx
        push    ax
        mov     ax, [bx+PDMI_V_Y1]
        mov     cx, [bp+12]
        jmp     .w
.r90:   mov     ax, [bp+12]             ; u = y - y0 ; w = x - x0
        mov     cx, [bx+PDMI_V_Y0]
        call    pdmi_sub16
        push    dx
        push    ax
        mov     ax, [bp+10]
        mov     cx, [bx+PDMI_V_X0]
        jmp     .w
.r180:  mov     ax, [bx+PDMI_V_X1]      ; u = x1 - x ; w = y - y0
        mov     cx, [bp+10]
        call    pdmi_sub16
        push    dx
        push    ax
        mov     ax, [bp+12]
        mov     cx, [bx+PDMI_V_Y0]
        jmp     .w
.r270:  mov     ax, [bx+PDMI_V_Y1]      ; u = y1 - y ; w = x1 - x
        mov     cx, [bp+12]
        call    pdmi_sub16
        push    dx
        push    ax
        mov     ax, [bx+PDMI_V_X1]
        mov     cx, [bp+10]
.w:     call    pdmi_sub16
        call    pdmi_scy
        mov     bx, [bp+8]
        mov     cx, [bx+PDMI_V_OY]
        call    pdmi_addclamp
        mov     di, [bp+6]
        mov     [di], ax
        pop     ax
        pop     dx
        mov     bx, [bp+8]
        call    pdmi_scx
        mov     bx, [bp+8]
        mov     cx, [bx+PDMI_V_OX]
        call    pdmi_addclamp
        mov     di, [bp+4]
        mov     [di], ax
        pop     bp
        ret

; (w*, h*, v)
_pdm_view_size:
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        mov     ax, [bx+PDMI_V_X1]
        mov     cx, [bx+PDMI_V_X0]
        call    pdmi_sub16
        push    dx                      ; W
        push    ax
        mov     bx, [bp+8]
        mov     ax, [bx+PDMI_V_Y1]
        mov     cx, [bx+PDMI_V_Y0]
        call    pdmi_sub16              ; H in DX:AX
        pop     cx
        pop     si                      ; W in SI:CX
        mov     bx, [bp+8]
        cmp     word [bx+PDMI_V_ROT], 90
        je      .sw
        cmp     word [bx+PDMI_V_ROT], 270
        je      .sw
        push    dx                      ; keep H for the y scale
        push    ax
        mov     dx, si
        mov     ax, cx
        jmp     .go
.sw:    push    si                      ; keep W for the y scale; scale H in x
        push    cx
.go:    call    pdmi_scx
        xor     cx, cx
        call    pdmi_addclamp
        mov     di, [bp+4]
        mov     [di], ax
        pop     ax
        pop     dx
        mov     bx, [bp+8]
        call    pdmi_scy
        xor     cx, cx
        call    pdmi_addclamp
        mov     di, [bp+6]
        mov     [di], ax
        pop     bp
        ret

; (x*, y*, v, px, py): the smallest pt16 that lands on the pixel
%macro PDMI_INV_OUT 4                   ; slot lo, slot hi, negate?, box field
        mov     ax, [bp+%1]
        mov     dx, [bp+%2]
%if %3
        neg     dx
        neg     ax
        sbb     dx, 0
%endif
        mov     bx, [bp+8]
        mov     cx, [bx+%4]
        call    pdmi_addclamp
%endmacro
PDMI_UL equ -4
PDMI_UH equ -2
PDMI_WL equ -8
PDMI_WH equ -6

_pdm_view_inv:
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        cmp     word [bx+PDMI_V_ZOOM], 0
        je      .deg
        cmp     word [bx+PDMI_V_ASPN], 0
        jne     .ok
.deg:   call    pdmi_setovf
        mov     di, [bp+4]
        mov     word [di], 0
        mov     di, [bp+6]
        mov     word [di], 0
        pop     bp
        ret
.ok:    mov     ax, [bp+10]
        mov     cx, [bx+PDMI_V_OX]
        call    pdmi_sub16
        mov     bx, [bp+8]
        call    pdmi_icx
        push    dx                      ; u
        push    ax
        mov     bx, [bp+8]
        mov     ax, [bp+12]
        mov     cx, [bx+PDMI_V_OY]
        call    pdmi_sub16
        mov     bx, [bp+8]
        call    pdmi_icy
        push    dx                      ; w
        push    ax
        mov     bx, [bp+8]
        mov     dx, [bx+PDMI_V_ROT]
        cmp     dx, 90
        je      .r90
        cmp     dx, 180
        je      .r180
        cmp     dx, 270
        je      .r270
        PDMI_INV_OUT PDMI_UL, PDMI_UH, 0, PDMI_V_X0    ; X = u + x0
        mov     di, [bp+4]
        mov     [di], ax
        PDMI_INV_OUT PDMI_WL, PDMI_WH, 1, PDMI_V_Y1    ; Y = y1 - w
        jmp     .sy
.r90:   PDMI_INV_OUT PDMI_WL, PDMI_WH, 0, PDMI_V_X0    ; X = w + x0
        mov     di, [bp+4]
        mov     [di], ax
        PDMI_INV_OUT PDMI_UL, PDMI_UH, 0, PDMI_V_Y0    ; Y = u + y0
        jmp     .sy
.r180:  PDMI_INV_OUT PDMI_UL, PDMI_UH, 1, PDMI_V_X1    ; X = x1 - u
        mov     di, [bp+4]
        mov     [di], ax
        PDMI_INV_OUT PDMI_WL, PDMI_WH, 0, PDMI_V_Y0    ; Y = w + y0
        jmp     .sy
.r270:  PDMI_INV_OUT PDMI_WL, PDMI_WH, 1, PDMI_V_X1    ; X = x1 - w
        mov     di, [bp+4]
        mov     [di], ax
        PDMI_INV_OUT PDMI_UL, PDMI_UH, 1, PDMI_V_Y1    ; Y = y1 - u
.sy:    mov     di, [bp+6]
        mov     [di], ax
        add     sp, 8
        pop     bp
        ret

%endif ; PDM_SKIP_VIEW

; -----------------------------------------------------------------------------
; far memory (PAGEDESK-SPEC 4.3).  DS and ES come back as they went in and DF
; is clear.  A span that wraps the 64KB segment, or n > 0xFFF0, is refused:
; the function returns 0 and touches nothing.
; -----------------------------------------------------------------------------
; (shared by the far movers and crc16_far)  BX = off, CX = n -> CF clear if off + n <= 0x10000 and n <= 0xFFF0.
pdmi_okspan:
        cmp     cx, 0xFFF0
        ja      .bad
        mov     ax, bx
        add     ax, cx
        jnc     .ok
        test    ax, ax
        jnz     .bad
.ok:    clc
        ret
.bad:   stc
        ret

%ifndef PDM_SKIP_FAR

_pd_fcopy_in:                           ; (seg, off, dst, n)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     cx, [bp+10]
        call    pdmi_okspan
        jc      .no
        push    ds
        push    es
        cld
        mov     ax, ds
        mov     es, ax
        mov     di, [bp+8]
        mov     si, bx
        mov     ds, [bp+4]
        shr     cx, 1
        rep movsw
        jnc     .ev
        movsb
.ev:    pop     es
        pop     ds
        mov     ax, [bp+10]
        pop     bp
        ret
.no:    xor     ax, ax
        pop     bp
        ret

_pd_fcopy_out:                          ; (src, seg, off, n)
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        mov     cx, [bp+10]
        call    pdmi_okspan
        jc      .no
        push    es
        cld
        mov     es, [bp+6]
        mov     di, bx
        mov     si, [bp+4]
        shr     cx, 1
        rep movsw
        jnc     .ev
        movsb
.ev:    pop     es
        mov     ax, [bp+10]
        pop     bp
        ret
.no:    xor     ax, ax
        pop     bp
        ret

; AX = seg, BX = off -> DX:AX = seg*16 + off.  Clobbers CX.
pdmi_lin:
        mov     dx, ax
        mov     cl, 4
        shl     ax, cl
        mov     cl, 12
        shr     dx, cl
        add     ax, bx
        adc     dx, 0
        ret

_pd_fcopy:                              ; (dseg, doff, sseg, soff, n): memmove
        push    bp
        mov     bp, sp
        mov     cx, [bp+12]
        mov     bx, [bp+6]
        call    pdmi_okspan
        jc      .no
        mov     bx, [bp+10]
        call    pdmi_okspan
        jc      .no
        mov     ax, [bp+4]              ; destination linear address
        mov     bx, [bp+6]
        call    pdmi_lin
        push    dx
        push    ax
        mov     ax, [bp+8]              ; source linear address
        mov     bx, [bp+10]
        call    pdmi_lin
        pop     si                      ; DI:SI = destination
        pop     di
        ; backward only when source < destination < source + n
        mov     bx, ax
        mov     cx, dx                  ; CX:BX = source
        cmp     di, cx
        jb      .fwd
        ja      .gt
        cmp     si, bx
        jbe     .fwd
.gt:    add     bx, [bp+12]
        adc     cx, 0                   ; source + n
        cmp     di, cx
        ja      .fwd
        jb      .bwd
        cmp     si, bx
        jae     .fwd
.bwd:   push    ds
        push    es
        mov     es, [bp+4]
        mov     ds, [bp+8]
        mov     cx, [bp+12]
        jcxz    .bd
        mov     di, [bp+6]
        add     di, cx
        dec     di
        mov     si, [bp+10]
        add     si, cx
        dec     si
        std
        rep movsb
        cld
.bd:    pop     es
        pop     ds
        mov     ax, [bp+12]
        pop     bp
        ret
.fwd:   push    ds
        push    es
        cld
        mov     es, [bp+4]
        mov     ds, [bp+8]
        mov     di, [bp+6]
        mov     si, [bp+10]
        mov     cx, [bp+12]
        shr     cx, 1
        rep movsw
        jnc     .fe
        movsb
.fe:    pop     es
        pop     ds
        mov     ax, [bp+12]
        pop     bp
        ret
.no:    xor     ax, ax
        pop     bp
        ret

_pd_fpeekb:                             ; (seg, off)
        push    bp
        mov     bp, sp
        push    es
        mov     es, [bp+4]
        mov     bx, [bp+6]
        mov     al, [es:bx]
        xor     ah, ah
        pop     es
        pop     bp
        ret

_pd_fpeekw:                             ; (seg, off); off = 0xFFFF is undefined
        push    bp
        mov     bp, sp
        push    es
        mov     es, [bp+4]
        mov     bx, [bp+6]
        mov     ax, [es:bx]
        pop     es
        pop     bp
        ret

_pd_fpokeb:                             ; (seg, off, v)
        push    bp
        mov     bp, sp
        push    es
        mov     es, [bp+4]
        mov     bx, [bp+6]
        mov     al, [bp+8]
        mov     [es:bx], al
        pop     es
        pop     bp
        ret

_pd_fpokew:                             ; (seg, off, v); off = 0xFFFF undefined
        push    bp
        mov     bp, sp
        push    es
        mov     es, [bp+4]
        mov     bx, [bp+6]
        mov     ax, [bp+8]
        mov     [es:bx], ax
        pop     es
        pop     bp
        ret

_pd_ffill:                              ; (seg, off, v, n)
        push    bp
        mov     bp, sp
        mov     bx, [bp+6]
        mov     cx, [bp+10]
        call    pdmi_okspan
        jc      .no
        push    es
        cld
        mov     es, [bp+4]
        mov     di, bx
        mov     al, [bp+8]
        mov     ah, al
        shr     cx, 1
        rep stosw
        jnc     .ev
        stosb
.ev:    pop     es
        mov     ax, [bp+10]
        pop     bp
        ret
.no:    xor     ax, ax
        pop     bp
        ret

_pd_ds:
        mov     ax, ds
        ret

%endif ; PDM_SKIP_FAR

; -----------------------------------------------------------------------------
; CRC-16/CCITT (poly 0x1021, MSB first), a nibble at a time
; -----------------------------------------------------------------------------
%ifndef PDM_SKIP_CRC

pdmi_crct:
        dw 0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7
        dw 0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF

%macro PDMI_CRCNIB 0                    ; CL = 4
        mov     bl, ah
        shr     bl, cl
        xor     bh, bh
        shl     bx, 1
        shl     ax, cl
        xor     ax, [cs:bx+pdmi_crct]
%endmacro

%macro PDMI_CRCLOOP 1                   ; %1 = the data segment register
        mov     cl, 4
.b:     mov     dl, [%1:si]
        inc     si
        xor     ah, dl
        PDMI_CRCNIB
        PDMI_CRCNIB
        dec     di
        jnz     .b
%endmacro

_pdm_crc16:                             ; (crc, p, n)
        push    bp
        mov     bp, sp
        mov     ax, [bp+4]
        mov     si, [bp+6]
        mov     di, [bp+8]
        test    di, di
        jz      .r
        PDMI_CRCLOOP ds
.r:     pop     bp
        ret

_pdm_crc16_far:                         ; (crc, seg, off, n): refused -> crc
        push    bp
        mov     bp, sp
        mov     bx, [bp+8]
        mov     cx, [bp+10]
        call    pdmi_okspan
        mov     ax, [bp+4]              ; a refusal answers the crc unchanged
        jc      .r
        mov     di, cx
        test    di, di
        jz      .r
        push    es
        mov     es, [bp+6]
        mov     si, [bp+8]
        PDMI_CRCLOOP es
        pop     es
.r:     pop     bp
        ret

%endif ; PDM_SKIP_CRC

%endif ; PDMATH_INC
