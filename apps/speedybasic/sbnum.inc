; =============================================================================
; Speedy Basic compact numeric ABI for the real-mode SmallerC package.
;
; C sees the declarations in sbnum.h.  A and B are global two-word registers,
; because SmallerC has no 32-bit integer and an address of an automatic points
; into SS while every C pointer dereferences through DS.  Calls consume A/B,
; leave the result in A, never change DS/ES/BP/SP, and leave DF clear.
;
; LONG (kind 0) is exact signed two's complement 32-bit.  FIXED (kind 1) is
; signed Q16.16.  Fixed multiply/divide and CINT round nearest, ties to even.
; Include this text after speedybasic.gen.asm and before CC_IMAGE_END.
; =============================================================================

SBN_LONG       equ 0
SBN_FIXED      equ 1
SBN_OK         equ 0
SBN_DIVZERO    equ 1
SBN_OVERFLOW   equ 2
SBN_DOMAIN     equ 3
SBN_BADKIND    equ 4
SBN_NOMODULE   equ 5

section .text

; AX=error, DX=result kind: zero A and publish the failure.
sbn_fail:
    mov [_sbn_err], ax
    mov word [_sbn_alo], 0
    mov word [_sbn_ahi], 0
    mov [_sbn_akind], dx
    stc
    ret

sbn_badkind:
    mov ax, SBN_BADKIND
    mov dx, SBN_LONG
    jmp sbn_fail

; Promote one exact LONG to Q16.16.  The LONG must be a signed 16-bit value.
; CF=1 means it cannot fit.  No error is published here; the caller owns it.
sbn_promote_a:
    cmp word [_sbn_akind], SBN_FIXED
    je .ok
    cmp word [_sbn_akind], SBN_LONG
    jne .bad
    mov ax, [_sbn_alo]
    cwd
    cmp dx, [_sbn_ahi]
    jne .bad
    mov [_sbn_ahi], ax
    mov word [_sbn_alo], 0
    mov word [_sbn_akind], SBN_FIXED
.ok:
    clc
    ret
.bad:
    stc
    ret

sbn_promote_b:
    cmp word [_sbn_bkind], SBN_FIXED
    je .ok
    cmp word [_sbn_bkind], SBN_LONG
    jne .bad
    mov ax, [_sbn_blo]
    cwd
    cmp dx, [_sbn_bhi]
    jne .bad
    mov [_sbn_bhi], ax
    mov word [_sbn_blo], 0
    mov word [_sbn_bkind], SBN_FIXED
.ok:
    clc
    ret
.bad:
    stc
    ret

; Validate both kinds and promote a mixed pair.  CF=1 has already published
; the error and the public operation should return immediately.
sbn_prepare:
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    ja .kind
    cmp word [_sbn_bkind], SBN_FIXED
    ja .kind
    mov ax, [_sbn_akind]
    cmp ax, [_sbn_bkind]
    je .ok
    call sbn_promote_a
    jc .range
    call sbn_promote_b
    jc .range
.ok:
    clc
    ret
.range:
    mov ax, SBN_OVERFLOW
    mov dx, SBN_FIXED
    jmp sbn_fail
.kind:
    jmp sbn_badkind

; Convert A/B in place from signed raw bits to unsigned magnitude.  The sign
; is banked by each caller before this; 0x80000000 deliberately remains a
; representable unsigned magnitude.
sbn_abs_a:
    test word [_sbn_ahi], 0x8000
    jz .out
    not word [_sbn_alo]
    not word [_sbn_ahi]
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
.out:
    ret

sbn_abs_b:
    test word [_sbn_bhi], 0x8000
    jz .out
    not word [_sbn_blo]
    not word [_sbn_bhi]
    add word [_sbn_blo], 1
    adc word [_sbn_bhi], 0
.out:
    ret

; A contains an unsigned magnitude and DI is its wanted sign.  Reject a
; magnitude outside the signed 32-bit range, then apply the sign.
sbn_sign_result:
    or di, di
    jnz .negative
    test word [_sbn_ahi], 0x8000
    jnz .overflow
    clc
    ret
.negative:
    cmp word [_sbn_ahi], 0x8000
    ja .overflow
    jb .negate
    cmp word [_sbn_alo], 0
    jne .overflow
.negate:
    not word [_sbn_alo]
    not word [_sbn_ahi]
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
    clc
    ret
.overflow:
    mov ax, SBN_OVERFLOW
    mov dx, si                       ; SI banks the operation's result kind
    jmp sbn_fail

; -----------------------------------------------------------------------------
; The four binary operations
; -----------------------------------------------------------------------------

_sbn_add:
    call sbn_prepare
    jc .out
    mov si, [_sbn_akind]
    mov ax, [_sbn_blo]
    add [_sbn_alo], ax
    mov ax, [_sbn_bhi]
    adc [_sbn_ahi], ax               ; OF from the high word is signed-32 OF
    jno .out
    mov ax, SBN_OVERFLOW
    mov dx, si
    call sbn_fail
.out:
    ret

_sbn_sub:
    call sbn_prepare
    jc .out
    mov si, [_sbn_akind]
    mov ax, [_sbn_blo]
    sub [_sbn_alo], ax
    mov ax, [_sbn_bhi]
    sbb [_sbn_ahi], ax
    jno .out
    mov ax, SBN_OVERFLOW
    mov dx, si
    call sbn_fail
.out:
    ret

; The expensive operations live in SPEEDYB.OVL.  Their tiny resident entries
; share one loader/far-call bridge.  Raw tests define SBN_FLAT and assemble
; the same bodies as ordinary near routines, without the package loader.
%ifndef SBN_FLAT
_sbn_mul:   mov bx, sbn_ovl_mul
    jmp sbn_call_cold
_sbn_div:   mov bx, sbn_ovl_div
    jmp sbn_call_cold
_sbn_fdiv:  mov bx, sbn_ovl_fdiv
    jmp sbn_call_cold
_sbn_neg:   mov bx, sbn_ovl_neg
    jmp sbn_call_cold
_sbn_floor: mov bx, sbn_ovl_floor
    jmp sbn_call_cold
_sbn_fix:   mov bx, sbn_ovl_fix
    jmp sbn_call_cold
_sbn_cint:  mov bx, sbn_ovl_cint
    jmp sbn_call_cold
_sbn_float: mov bx, sbn_ovl_float
    jmp sbn_call_cold
_sbn_sqrt:  mov bx, sbn_ovl_sqrt
    jmp sbn_call_cold
_sbn_sin:   mov bx, sbn_ovl_sin
    jmp sbn_call_cold
_sbn_cos:   mov bx, sbn_ovl_cos
    jmp sbn_call_cold

sbn_call_cold:
    call cc_ovneed
    jc .missing
    mov [sbn_ovvec], bx
    mov ax, [cc_ovseg]
    mov [sbn_ovvec+2], ax
    call far [sbn_ovvec]
    ret
.missing:
    mov word [_sbn_err], SBN_NOMODULE
    mov word [_sbn_alo], 0
    mov word [_sbn_ahi], 0
    ret

section .modc
%endif

; Cold copies of the small representation helpers keep module code wholly in
; its own CS.  Only globals are shared through the resident DS.
sbn_cold_fail:
    mov [_sbn_err], ax
    mov word [_sbn_alo], 0
    mov word [_sbn_ahi], 0
    mov [_sbn_akind], dx
    stc
    ret

sbn_cold_badkind:
    mov ax, SBN_BADKIND
    mov dx, SBN_LONG
    jmp sbn_cold_fail

sbn_cold_promote_a:
    cmp word [_sbn_akind], SBN_FIXED
    je .ok
    cmp word [_sbn_akind], SBN_LONG
    jne .bad
    mov ax, [_sbn_alo]
    cwd
    cmp dx, [_sbn_ahi]
    jne .bad
    mov [_sbn_ahi], ax
    mov word [_sbn_alo], 0
    mov word [_sbn_akind], SBN_FIXED
.ok:
    clc
    ret
.bad:
    stc
    ret

sbn_cold_promote_b:
    cmp word [_sbn_bkind], SBN_FIXED
    je .ok
    cmp word [_sbn_bkind], SBN_LONG
    jne .bad
    mov ax, [_sbn_blo]
    cwd
    cmp dx, [_sbn_bhi]
    jne .bad
    mov [_sbn_bhi], ax
    mov word [_sbn_blo], 0
    mov word [_sbn_bkind], SBN_FIXED
.ok:
    clc
    ret
.bad:
    stc
    ret

sbn_cold_prepare:
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    ja .kind
    cmp word [_sbn_bkind], SBN_FIXED
    ja .kind
    mov ax, [_sbn_akind]
    cmp ax, [_sbn_bkind]
    je .ok
    call sbn_cold_promote_a
    jc .range
    call sbn_cold_promote_b
    jc .range
.ok:
    clc
    ret
.range:
    mov ax, SBN_OVERFLOW
    mov dx, SBN_FIXED
    jmp sbn_cold_fail
.kind:
    jmp sbn_cold_badkind

sbn_cold_abs_a:
    test word [_sbn_ahi], 0x8000
    jz .out
    not word [_sbn_alo]
    not word [_sbn_ahi]
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
.out:
    ret

sbn_cold_abs_b:
    test word [_sbn_bhi], 0x8000
    jz .out
    not word [_sbn_blo]
    not word [_sbn_bhi]
    add word [_sbn_blo], 1
    adc word [_sbn_bhi], 0
.out:
    ret

sbn_cold_sign_result:
    or di, di
    jnz .negative
    test word [_sbn_ahi], 0x8000
    jnz .overflow
    clc
    ret
.negative:
    cmp word [_sbn_ahi], 0x8000
    ja .overflow
    jb .negate
    cmp word [_sbn_alo], 0
    jne .overflow
.negate:
    not word [_sbn_alo]
    not word [_sbn_ahi]
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
    clc
    ret
.overflow:
    mov ax, SBN_OVERFLOW
    mov dx, si
    jmp sbn_cold_fail

%macro SBN_COLD_ENTRY 3
%ifdef SBN_FLAT
%1:
%else
%2:
    call %3
    retf
%endif
%3:
%endmacro

SBN_COLD_ENTRY _sbn_mul, sbn_ovl_mul, sbn_mul_body
    call sbn_cold_prepare
    jc .out
    mov si, [_sbn_akind]
    xor di, di
    test word [_sbn_ahi], 0x8000
    jz .as
    xor di, 1
.as:
    test word [_sbn_bhi], 0x8000
    jz .bs
    xor di, 1
.bs:
    call sbn_cold_abs_a
    call sbn_cold_abs_b

    mov ax, [_sbn_alo]               ; p = a0*b0
    mul word [_sbn_blo]
    mov [sbn_p0], ax
    mov [sbn_p1], dx
    mov word [sbn_p2], 0
    mov word [sbn_p3], 0

    mov ax, [_sbn_ahi]               ; p += (a1*b0)<<16
    mul word [_sbn_blo]
    add [sbn_p1], ax
    adc [sbn_p2], dx
    adc word [sbn_p3], 0

    mov ax, [_sbn_alo]               ; p += (a0*b1)<<16
    mul word [_sbn_bhi]
    add [sbn_p1], ax
    adc [sbn_p2], dx
    adc word [sbn_p3], 0

    mov ax, [_sbn_ahi]               ; p += (a1*b1)<<32
    mul word [_sbn_bhi]
    add [sbn_p2], ax
    adc [sbn_p3], dx

    cmp si, SBN_FIXED
    je .fixed
    mov ax, [sbn_p2]
    or ax, [sbn_p3]
    jnz .overflow
    mov ax, [sbn_p0]
    mov [_sbn_alo], ax
    mov ax, [sbn_p1]
    mov [_sbn_ahi], ax
    jmp .signed
.fixed:
    cmp word [sbn_p3], 0
    jne .overflow
    mov ax, [sbn_p1]                 ; p >> 16
    mov [_sbn_alo], ax
    mov ax, [sbn_p2]
    mov [_sbn_ahi], ax
    cmp word [sbn_p0], 0x8000        ; nearest, ties to even
    jb .signed
    ja .round
    test word [_sbn_alo], 1
    jz .signed
.round:
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
    jc .overflow
.signed:
    call sbn_cold_sign_result
    ret
.overflow:
    mov ax, SBN_OVERFLOW
    mov dx, si
    call sbn_cold_fail
.out:
    ret

SBN_COLD_ENTRY _sbn_div, sbn_ovl_div, sbn_div_body
    call sbn_cold_prepare
    jc .out
    mov si, [_sbn_akind]
.ready:
    mov ax, [_sbn_blo]
    or ax, [_sbn_bhi]
    jz .divzero
    xor di, di
    test word [_sbn_ahi], 0x8000
    jz .as
    xor di, 1
.as:
    test word [_sbn_bhi], 0x8000
    jz .bs
    xor di, 1
.bs:
    call sbn_cold_abs_a
    call sbn_cold_abs_b
    mov ax, [_sbn_blo]
    mov [sbn_dlo], ax
    mov ax, [_sbn_bhi]
    mov [sbn_dhi], ax
    cmp si, SBN_FIXED
    je .fixed_num
    mov ax, [_sbn_alo]               ; unsigned LONG numerator in 48 bits
    mov [sbn_n0], ax
    mov ax, [_sbn_ahi]
    mov [sbn_n1], ax
    mov word [sbn_n2], 0
    jmp .num_done
.fixed_num:                           ; Q16.16 division: (raw A << 16) / raw B
    mov word [sbn_n0], 0
    mov ax, [_sbn_alo]
    mov [sbn_n1], ax
    mov ax, [_sbn_ahi]
    mov [sbn_n2], ax
.num_done:
    mov word [sbn_q0], 0
    mov word [sbn_q1], 0
    mov word [sbn_q2], 0
    mov word [sbn_rlo], 0
    mov word [sbn_rhi], 0
    mov cx, 48
.bit:
    shl word [sbn_q0], 1
    rcl word [sbn_q1], 1
    rcl word [sbn_q2], 1
    shl word [sbn_n0], 1
    rcl word [sbn_n1], 1
    rcl word [sbn_n2], 1
    rcl word [sbn_rlo], 1
    rcl word [sbn_rhi], 1
    jc .subtract                     ; the 33rd remainder bit means R >= D
    mov ax, [sbn_rhi]
    cmp ax, [sbn_dhi]
    jb .next
    ja .subtract
    mov ax, [sbn_rlo]
    cmp ax, [sbn_dlo]
    jb .next
.subtract:
    mov ax, [sbn_dlo]
    sub [sbn_rlo], ax
    mov ax, [sbn_dhi]
    sbb [sbn_rhi], ax
    or word [sbn_q0], 1
.next:
    dec cx
    jnz .bit

    cmp si, SBN_FIXED
    jne .range
    shl word [sbn_rlo], 1            ; compare 2*remainder with divisor
    rcl word [sbn_rhi], 1
    jc .round
    mov ax, [sbn_rhi]
    cmp ax, [sbn_dhi]
    jb .range
    ja .round
    mov ax, [sbn_rlo]
    cmp ax, [sbn_dlo]
    jb .range
    ja .round
    test word [sbn_q0], 1            ; exact half: make the quotient even
    jz .range
.round:
    add word [sbn_q0], 1
    adc word [sbn_q1], 0
    adc word [sbn_q2], 0
.range:
    cmp word [sbn_q2], 0
    jne .overflow
    mov ax, [sbn_q0]
    mov [_sbn_alo], ax
    mov ax, [sbn_q1]
    mov [_sbn_ahi], ax
    call sbn_cold_sign_result
    ret
.divzero:
    mov ax, SBN_DIVZERO
    mov dx, si
    call sbn_cold_fail
    ret
.overflow:
    mov ax, SBN_OVERFLOW
    mov dx, si
    call sbn_cold_fail
.out:
    ret

; BASIC `/` needs a fractional result even for two exact LONG operands.  The
; shared divider already has the required 48-bit numerator path; select it
; directly with a FIXED result kind instead of first narrowing either input.
SBN_COLD_ENTRY _sbn_fdiv, sbn_ovl_fdiv, sbn_fdiv_body
    cmp word [_sbn_akind], SBN_LONG
    jne .normal
    cmp word [_sbn_bkind], SBN_LONG
    jne .normal
    mov word [_sbn_err], SBN_OK
    mov word [_sbn_akind], SBN_FIXED
    mov si, SBN_FIXED
    jmp sbn_div_body.ready
.normal:
    call sbn_div_body
    ret

; -----------------------------------------------------------------------------
; Compare and unary integer operations
; -----------------------------------------------------------------------------

%ifndef SBN_FLAT
section .text                       ; compare is the resident branch hot path
%endif
_sbn_cmp:
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    ja .bad
    cmp word [_sbn_bkind], SBN_FIXED
    ja .bad
    mov ax, [_sbn_akind]
    cmp ax, [_sbn_bkind]
    je .raw
    cmp word [_sbn_akind], SBN_LONG
    jne .b_long
    mov ax, [_sbn_alo]               ; an out-of-Q-range LONG compares by sign
    cwd
    cmp dx, [_sbn_ahi]
    jne .a_out
    call sbn_promote_a
    jmp .raw
.a_out:
    test word [_sbn_ahi], 0x8000
    jnz .less
    jmp .greater
.b_long:
    mov ax, [_sbn_blo]
    cwd
    cmp dx, [_sbn_bhi]
    jne .b_out
    call sbn_promote_b
    jmp .raw
.b_out:
    test word [_sbn_bhi], 0x8000
    jnz .greater
    jmp .less
.raw:
    mov ax, [_sbn_ahi]
    cmp ax, [_sbn_bhi]
    jl .less
    jg .greater
    mov ax, [_sbn_alo]
    cmp ax, [_sbn_blo]
    jb .less
    ja .greater
    xor ax, ax
    ret
.less:
    mov ax, -1
    ret
.greater:
    mov ax, 1
    ret
.bad:
    call sbn_badkind
    xor ax, ax
    ret

%ifndef SBN_FLAT
section .modc
%endif
SBN_COLD_ENTRY _sbn_neg, sbn_ovl_neg, sbn_neg_body
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    ja .bad
    cmp word [_sbn_ahi], 0x8000
    jne .do
    cmp word [_sbn_alo], 0
    jne .do
    mov dx, [_sbn_akind]
    mov ax, SBN_OVERFLOW
    call sbn_cold_fail
    ret
.do:
    not word [_sbn_alo]
    not word [_sbn_ahi]
    add word [_sbn_alo], 1
    adc word [_sbn_ahi], 0
    ret
.bad:
    call sbn_cold_badkind
    ret

SBN_COLD_ENTRY _sbn_floor, sbn_ovl_floor, sbn_floor_body
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_LONG
    je .out
    cmp word [_sbn_akind], SBN_FIXED
    jne .bad
    mov ax, [_sbn_ahi]               ; arithmetic >>16 is already floor
    mov [_sbn_alo], ax
    cwd
    mov [_sbn_ahi], dx
    mov word [_sbn_akind], SBN_LONG
.out:
    ret
.bad:
    call sbn_cold_badkind
    ret

SBN_COLD_ENTRY _sbn_fix, sbn_ovl_fix, sbn_fix_body
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_LONG
    je .out
    cmp word [_sbn_akind], SBN_FIXED
    jne .bad
    mov ax, [_sbn_ahi]
    or ax, ax
    jns .put
    cmp word [_sbn_alo], 0           ; negative fraction: step floor toward 0
    je .put
    inc ax
.put:
    mov [_sbn_alo], ax
    cwd
    mov [_sbn_ahi], dx
    mov word [_sbn_akind], SBN_LONG
.out:
    ret
.bad:
    call sbn_cold_badkind
    ret

SBN_COLD_ENTRY _sbn_cint, sbn_ovl_cint, sbn_cint_body
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_LONG
    je .out
    cmp word [_sbn_akind], SBN_FIXED
    jne .bad
    xor di, di
    test word [_sbn_ahi], 0x8000
    jz .mag
    mov di, 1
.mag:
    call sbn_cold_abs_a
    mov dx, [_sbn_ahi]               ; integer magnitude
    mov ax, [_sbn_alo]               ; discarded fraction
    cmp ax, 0x8000
    jb .put
    ja .up
    test dx, 1
    jz .put
.up:
    inc dx
.put:
    mov [_sbn_alo], dx
    mov word [_sbn_ahi], 0
    mov word [_sbn_akind], SBN_LONG
    mov si, SBN_LONG
    call sbn_cold_sign_result
.out:
    ret
.bad:
    call sbn_cold_badkind
    ret

; -----------------------------------------------------------------------------
; Square root: restoring integer sqrt of raw<<16 (FIXED) or LONG<<32.
; Thirty-two two-bit turns return a rounded Q16.16 root.
; -----------------------------------------------------------------------------

SBN_COLD_ENTRY _sbn_sqrt, sbn_ovl_sqrt, sbn_sqrt_body
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    ja .bad
    test word [_sbn_ahi], 0x8000
    jnz .domain
    cmp word [_sbn_akind], SBN_FIXED
    je .fixed
    cmp word [_sbn_ahi], 0x3fff      ; larger roots do not fit signed Q16.16
    ja .overflow
    mov word [sbn_n0], 0
    mov word [sbn_n1], 0
    mov ax, [_sbn_alo]
    mov [sbn_n2], ax
    mov ax, [_sbn_ahi]
    mov [sbn_n3], ax
    jmp .start
.fixed:
    mov word [sbn_n0], 0
    mov ax, [_sbn_alo]
    mov [sbn_n1], ax
    mov ax, [_sbn_ahi]
    mov [sbn_n2], ax
    mov word [sbn_n3], 0
.start:
    mov word [sbn_q0], 0            ; root
    mov word [sbn_q1], 0
    mov word [sbn_rlo], 0
    mov word [sbn_rhi], 0
    mov cx, 32
.pair:
    shl word [sbn_q0], 1
    rcl word [sbn_q1], 1
    shl word [sbn_n0], 1
    rcl word [sbn_n1], 1
    rcl word [sbn_n2], 1
    rcl word [sbn_n3], 1
    rcl word [sbn_rlo], 1
    rcl word [sbn_rhi], 1
    shl word [sbn_n0], 1
    rcl word [sbn_n1], 1
    rcl word [sbn_n2], 1
    rcl word [sbn_n3], 1
    rcl word [sbn_rlo], 1
    rcl word [sbn_rhi], 1
    mov ax, [sbn_q0]                ; trial = 2*root + 1
    mov dx, [sbn_q1]
    shl ax, 1
    rcl dx, 1
    add ax, 1
    adc dx, 0
    mov bx, [sbn_rhi]
    cmp bx, dx
    jb .next
    ja .take
    mov bx, [sbn_rlo]
    cmp bx, ax
    jb .next
.take:
    sub [sbn_rlo], ax
    sbb [sbn_rhi], dx
    add word [sbn_q0], 1
    adc word [sbn_q1], 0
.next:
    dec cx
    jnz .pair
    mov ax, [sbn_rhi]               ; nearest integer root: round iff rem>root
    cmp ax, [sbn_q1]
    jb .put
    ja .round
    mov ax, [sbn_rlo]
    cmp ax, [sbn_q0]
    jbe .put
.round:
    add word [sbn_q0], 1
    adc word [sbn_q1], 0
.put:
    mov ax, [sbn_q0]
    mov [_sbn_alo], ax
    mov ax, [sbn_q1]
    mov [_sbn_ahi], ax
    mov word [_sbn_akind], SBN_FIXED
    ret
.domain:
    mov ax, SBN_DOMAIN
    mov dx, SBN_FIXED
    call sbn_cold_fail
    ret
.overflow:
    mov ax, SBN_OVERFLOW
    mov dx, SBN_FIXED
    call sbn_cold_fail
    ret
.bad:
    mov ax, SBN_BADKIND
    mov dx, SBN_FIXED
    call sbn_cold_fail
    ret

; -----------------------------------------------------------------------------
; SIN/COS: Q16.16 radians, reduced to [-pi/2,pi/2], seventh-order sine.
; The worst polynomial error is below 0.00017; Q16.16 coefficient rounding
; keeps the measured whole-domain error below 0.00035.
; -----------------------------------------------------------------------------

sbn_to_fixed_a:
    mov word [_sbn_err], SBN_OK
    cmp word [_sbn_akind], SBN_FIXED
    je .ok
    cmp word [_sbn_akind], SBN_LONG
    jne .bad
    call sbn_cold_promote_a
    jc .range
.ok:
    clc
    ret
.range:
    mov ax, SBN_OVERFLOW
    mov dx, SBN_FIXED
    jmp sbn_cold_fail
.bad:
    mov ax, SBN_BADKIND
    mov dx, SBN_FIXED
    jmp sbn_cold_fail

SBN_COLD_ENTRY _sbn_float, sbn_ovl_float, sbn_float_body
    call sbn_to_fixed_a
    ret

; Reduce A modulo 2*pi to [-pi,pi].  Constants are nearest Q16.16.
sbn_reduce_angle:
.above:
    mov ax, [_sbn_ahi]
    cmp ax, 3                        ; +pi = 0003:243f
    jg .subtau
    jl .below
    cmp word [_sbn_alo], 0x243f
    jbe .below
.subtau:
    sub word [_sbn_alo], 0x487f     ; 2*pi = 0006:487f
    sbb word [_sbn_ahi], 6
    jmp .above
.below:
    mov ax, [_sbn_ahi]
    cmp ax, -4                       ; -pi = fffc:dbc1
    jl .addtau
    jg .out
    cmp word [_sbn_alo], 0xdbc1
    jae .out
.addtau:
    add word [_sbn_alo], 0x487f
    adc word [_sbn_ahi], 6
    jmp .below
.out:
    ret

; Reload B from the saved raw value at sbn_y, then fixed-multiply A by it.
sbn_mul_y:
    mov ax, [sbn_ylo]
    mov [_sbn_blo], ax
    mov ax, [sbn_yhi]
    mov [_sbn_bhi], ax
    mov word [_sbn_bkind], SBN_FIXED
    call sbn_mul_body
    ret

SBN_COLD_ENTRY _sbn_sin, sbn_ovl_sin, sbn_sin_body
    call sbn_to_fixed_a
    jc .out
    call sbn_reduce_angle
    mov ax, [_sbn_ahi]
    cmp ax, 1                        ; +pi/2 = 0001:9220
    jg .posfold
    jl .negcheck
    cmp word [_sbn_alo], 0x9220
    ja .posfold
    jmp .save
.posfold:                            ; x = pi - x
    mov ax, 0x243f
    mov dx, 3
    sub ax, [_sbn_alo]
    sbb dx, [_sbn_ahi]
    mov [_sbn_alo], ax
    mov [_sbn_ahi], dx
    jmp .save
.negcheck:
    cmp ax, -2                       ; -pi/2 = fffe:6de0
    jl .negfold
    jg .save
    cmp word [_sbn_alo], 0x6de0
    jae .save
.negfold:                            ; x = -pi - x
    mov ax, 0xdbc1
    mov dx, 0xfffc
    sub ax, [_sbn_alo]
    sbb dx, [_sbn_ahi]
    mov [_sbn_alo], ax
    mov [_sbn_ahi], dx
.save:
    mov ax, [_sbn_alo]
    mov [sbn_xlo], ax
    mov [_sbn_blo], ax
    mov ax, [_sbn_ahi]
    mov [sbn_xhi], ax
    mov [_sbn_bhi], ax
    mov word [_sbn_bkind], SBN_FIXED
    call sbn_mul_body                ; A = x^2
    mov ax, [_sbn_alo]
    mov [sbn_ylo], ax
    mov ax, [_sbn_ahi]
    mov [sbn_yhi], ax

    mov word [_sbn_alo], 0xfff3     ; Horner: -1/5040
    mov word [_sbn_ahi], 0xffff
    mov word [_sbn_akind], SBN_FIXED
    call sbn_mul_y
    add word [_sbn_alo], 546         ; +1/120
    adc word [_sbn_ahi], 0
    call sbn_mul_y
    sub word [_sbn_alo], 10923       ; -1/6
    sbb word [_sbn_ahi], 0
    call sbn_mul_y
    add word [_sbn_ahi], 1           ; +1.0
    mov ax, [sbn_xlo]
    mov [_sbn_blo], ax
    mov ax, [sbn_xhi]
    mov [_sbn_bhi], ax
    mov word [_sbn_bkind], SBN_FIXED
    call sbn_mul_body                ; x * polynomial
.out:
    ret

SBN_COLD_ENTRY _sbn_cos, sbn_ovl_cos, sbn_cos_body
    call sbn_to_fixed_a
    jc .out
    call sbn_reduce_angle
    add word [_sbn_alo], 0x9220      ; cos(x) = sin(x + pi/2)
    adc word [_sbn_ahi], 1
    call sbn_sin_body
.out:
    ret

; Scratch, the ABI registers and one far vector spend 56 bytes of resident BSS.
section .bss
_sbn_alo:   resw 1
_sbn_ahi:   resw 1
_sbn_akind: resw 1
_sbn_blo:   resw 1
_sbn_bhi:   resw 1
_sbn_bkind: resw 1
_sbn_err:   resw 1
sbn_p0:     resw 1
sbn_p1:     resw 1
sbn_p2:     resw 1
sbn_p3:     resw 1
sbn_n0:     resw 1
sbn_n1:     resw 1
sbn_n2:     resw 1
sbn_n3:     resw 1
sbn_q0:     resw 1
sbn_q1:     resw 1
sbn_q2:     resw 1
sbn_rlo:    resw 1
sbn_rhi:    resw 1
sbn_dlo:    resw 1
sbn_dhi:    resw 1
sbn_xlo:    resw 1
sbn_xhi:    resw 1
sbn_ylo:    resw 1
sbn_yhi:    resw 1
sbn_ovvec:  resw 2
section .text
