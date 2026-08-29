; =============================================================================
; os8088 - apps/os88fp.inc
;
; IEEE-754 DOUBLE, IN SOFTWARE, FOR AN 8086 WITH NO FPU.
;
; Stage 4.0 of the spreadsheet roadmap. Sheet's cells have been signed 16-bit
; integers throughout - 123456 commits as -7616, division truncates, and every
; text, date, trigonometric, financial and fraction-returning statistical
; function in Excel's library is blocked on that one fact rather than on
; effort. This is the file that removes it.
;
; --- WHY BINARY DOUBLE AND NOT DECIMAL --------------------------------------
; apps/calc/calc.asm already carries a complete, working decimal engine
; (SPEC.md 65.2): sign-magnitude, a 32-bit 9-digit magnitude, a decimal-place
; count, 64-bit intermediates, add/sub/mul/div with correct rounding. Reusing
; it would have been far less code than this file, and it has the real virtue
; that 0.1 + 0.2 is exactly 0.3.
;
; It was rejected for two reasons specific to a SPREADSHEET rather than a
; calculator:
;
;   1. BIFF's NUMBER record IS an IEEE-754 double, and so is one of RK's four
;      subtypes. File interchange is one of Sheet's real strengths - SYLK, DIF
;      and BIFF, read and written - and a decimal core would mean a lossy
;      conversion at every one of those boundaries, in both directions.
;   2. An 8087 accelerates binary doubles DIRECTLY (fld / faddp / fstp qword)
;      and does essentially nothing for sign-magnitude decimal: its BCD support
;      is load and store, not arithmetic. A decimal core would make the
;      hardware path this roadmap calls for almost pointless.
;
; Nine significant digits against Excel's fifteen settled it.
;
; --- THE STORED FORM ---------------------------------------------------------
; 8 bytes, little-endian, exactly as IEEE-754 and exactly as BIFF wants them:
; 52 fraction bits, an 11-bit exponent biased by 1023, one sign bit.
;
; --- THE WORKING FORM --------------------------------------------------------
; Arithmetic does not run on the packed form. An operand is unpacked into a
; sign byte, a SIGNED UNBIASED exponent, and a 64-bit mantissa normalized so
; bit 63 is always set:
;
;       value = (-1)^sign * m * 2^exp          with m in [2^63, 2^64)
;
; The 11 bits of headroom above the 53-bit significand are guard bits, and they
; are the whole reason results round correctly rather than merely plausibly.
; Packing rounds to nearest, ties to even.
;
; STATED SIMPLIFICATIONS, so they are findable rather than surprising:
;   * SUBNORMALS FLUSH TO ZERO on unpack, and underflow flushes to zero on
;     pack. A spreadsheet that reaches 1e-308 has a modelling problem rather
;     than a rounding one.
;   * NO INFINITY AND NO NaN IS EVER PRODUCED. Overflow CLAMPS to the largest
;     finite double; division by zero returns CF=1 and leaves the result zero.
;     Turning that into Excel's #DIV/0! is the CELL's business, not this
;     file's - an error type is a value-model decision and belongs one layer up.
;   * NEGATIVE ZERO is never written; a zero result packs as +0.
;
; --- USING IT ----------------------------------------------------------------
; `%include "os88fp.inc"` at the END of the package, before OS88_BSS, for the
; fixed-offset reason SPEC.md 20.2 gives.
;
; IT DECLARES NO STORAGE. Like apps/os88chart.inc, the CALLER declares the
; scratch, under these exact names, and every routine here reaches it through
; the caller's own DS - which is never changed anywhere in this file:
;
;   fp_as    db     A's sign, 0 or 1        fp_bs    db     B's sign
;   fp_ae    dw     A's exponent, signed    fp_be    dw     B's exponent
;   fp_am0-3 dw x4  A's mantissa, m0 low    fp_bm0-3 dw x4  B's mantissa
;   fp_t0-3  dw x4  a 64-bit temporary      fp_p0-7  dw x8  the 128-bit product
;   fp_sticky dw    "bits were lost below"  fp_tmp   dw     general scratch
;
; The shape is a two-register machine: unpack into A and B, operate, pack A.
; =============================================================================

FP_SZ      equ 8                    ; a packed double, in bytes
FP_BIAS    equ 1023
FP_EADJ    equ 1086                 ; FP_BIAS + 52 + 11 - the shift between a
                                    ; stored biased exponent and this file's
                                    ; own, given the 11-bit normalization

; =============================================================================
; 64-bit primitives. Every one takes BX = a 4-word block, m0 first.
; =============================================================================

; fp_shl1 - BX left one bit; CF = the bit shifted out of the top
fp_shl1:
    shl word [bx], 1
    rcl word [bx+2], 1
    rcl word [bx+4], 1
    rcl word [bx+6], 1
    ret

; fp_shr1 - BX right one bit; CF = the bit shifted out of the bottom
fp_shr1:
    shr word [bx+6], 1
    rcr word [bx+4], 1
    rcr word [bx+2], 1
    rcr word [bx], 1
    ret

; fp_iszero - CF=1 if the block at BX is all zero
fp_iszero:
    push ax
    mov ax, [bx]
    or ax, [bx+2]
    or ax, [bx+4]
    or ax, [bx+6]
    or ax, ax
    pop ax
    jnz .no
    stc
    ret
.no:
    clc
    ret

; fp_setsticky - OR "any bit set at BX" into fp_sticky
fp_setsticky:
    call fp_iszero
    jc .out
    mov word [fp_sticky], 1
.out:
    ret

; fp_shrn - BX right CX bits, ORing every bit that falls off into fp_sticky.
; CX >= 64 clears the block entirely, which is what an operand too small to
; affect the sum needs.
fp_shrn:
    push cx
    or cx, cx
    jz .out
    cmp cx, 64
    jb .loop
    call fp_setsticky
    mov word [bx], 0
    mov word [bx+2], 0
    mov word [bx+4], 0
    mov word [bx+6], 0
    jmp .out
.loop:
    call fp_shr1
    jnc .next
    mov word [fp_sticky], 1
.next:
    dec cx
    jnz .loop
.out:
    pop cx
    ret

; fp_addt - BX += fp_t (64-bit). CF = carry out of the top.
fp_addt:
    push ax
    mov ax, [fp_t0]
    add [bx], ax
    mov ax, [fp_t1]
    adc [bx+2], ax
    mov ax, [fp_t2]
    adc [bx+4], ax
    mov ax, [fp_t3]
    adc [bx+6], ax
    pop ax
    ret

; fp_subt - BX -= fp_t (64-bit). CF = borrow.
fp_subt:
    push ax
    mov ax, [fp_t0]
    sub [bx], ax
    mov ax, [fp_t1]
    sbb [bx+2], ax
    mov ax, [fp_t2]
    sbb [bx+4], ax
    mov ax, [fp_t3]
    sbb [bx+6], ax
    pop ax
    ret

; fp_cmp64 - compare the block at BX against fp_t; flags as for CMP (unsigned)
fp_cmp64:
    push ax
    mov ax, [bx+6]
    cmp ax, [fp_t3]
    jne .done
    mov ax, [bx+4]
    cmp ax, [fp_t2]
    jne .done
    mov ax, [bx+2]
    cmp ax, [fp_t1]
    jne .done
    mov ax, [bx]
    cmp ax, [fp_t0]
.done:
    pop ax
    ret

; fp_norm - normalize the mantissa at BX so bit 63 is set, decrementing the
; exponent word at DI once per shift. A zero mantissa is left alone.
fp_norm:
    call fp_iszero
    jc .out
.loop:
    test word [bx+6], 0x8000
    jnz .out
    call fp_shl1
    dec word [di]
    jmp .loop
.out:
    ret

; =============================================================================
; Pack and unpack
; =============================================================================

; -----------------------------------------------------------------------------
; fp_unpack_a - DS:SI = a packed double -> the A accumulator. SI preserved.
;
; The 53-bit significand sits in bits 0..52 of the stored form; shifted left by
; 11 its leading 1 lands on bit 63, which is what every routine below assumes.
; The four source words go through fp_t first rather than being juggled in
; registers - there are not enough registers to hold four words, two shift
; counts and a pointer at once, and trying was this file's first bug.
; -----------------------------------------------------------------------------
fp_unpack_a:
    push ax
    push bx
    push cx
    push dx
    mov ax, [si+6]                    ; the top word: sign, exponent, 4 bits
    mov byte [fp_as], 0
    test ax, 0x8000
    jz .sign
    mov byte [fp_as], 1
.sign:
    mov dx, ax
    and dx, 0x7FF0
    mov cl, 4
    shr dx, cl                        ; dx = the biased exponent
    or dx, dx
    jz .zero                          ; 0 = zero or subnormal - flushed
    sub dx, FP_EADJ
    mov [fp_ae], dx
    and ax, 0x000F
    or ax, 0x0010                     ; put the implicit leading 1 back
    mov [fp_t3], ax
    mov ax, [si+4]
    mov [fp_t2], ax
    mov ax, [si+2]
    mov [fp_t1], ax
    mov ax, [si]
    mov [fp_t0], ax
    ; the whole 53 bits, left 11
    mov cl, 11
    mov ax, [fp_t0]
    shl ax, cl
    mov [fp_am0], ax
    mov ax, [fp_t1]
    shl ax, cl
    mov [fp_am1], ax
    mov ax, [fp_t2]
    shl ax, cl
    mov [fp_am2], ax
    mov ax, [fp_t3]
    shl ax, cl
    mov [fp_am3], ax
    mov cl, 5                         ; ...and the bits carried up from below
    mov ax, [fp_t0]
    shr ax, cl
    or [fp_am1], ax
    mov ax, [fp_t1]
    shr ax, cl
    or [fp_am2], ax
    mov ax, [fp_t2]
    shr ax, cl
    or [fp_am3], ax
    jmp .out
.zero:
    mov word [fp_ae], 0
    mov word [fp_am0], 0
    mov word [fp_am1], 0
    mov word [fp_am2], 0
    mov word [fp_am3], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fp_unpack_b - the same into B. Written as a copy through A rather than
; duplicated, so there is exactly one piece of unpacking arithmetic to be
; wrong: unpack into A, move A to B, and put the caller's A back.
fp_unpack_b:
    push ax
    call fp_push_a
    call fp_unpack_a
    call fp_a_to_b
    call fp_pop_a
    pop ax
    ret

; fp_a_to_b / fp_b_to_a - move one accumulator to the other
fp_a_to_b:
    push ax
    mov al, [fp_as]
    mov [fp_bs], al
    mov ax, [fp_ae]
    mov [fp_be], ax
    mov ax, [fp_am0]
    mov [fp_bm0], ax
    mov ax, [fp_am1]
    mov [fp_bm1], ax
    mov ax, [fp_am2]
    mov [fp_bm2], ax
    mov ax, [fp_am3]
    mov [fp_bm3], ax
    pop ax
    ret

fp_b_to_a:
    push ax
    mov al, [fp_bs]
    mov [fp_as], al
    mov ax, [fp_be]
    mov [fp_ae], ax
    mov ax, [fp_bm0]
    mov [fp_am0], ax
    mov ax, [fp_bm1]
    mov [fp_am1], ax
    mov ax, [fp_bm2]
    mov [fp_am2], ax
    mov ax, [fp_bm3]
    mov [fp_am3], ax
    pop ax
    ret

; fp_push_a / fp_pop_a - bank A on the stack (6 words), for the two places
; that need a third operand's worth of room
fp_push_a:
    ; STKBALANCE-NET: +6 - banks the unpacked A on the caller's stack; fp_pop_a takes it off
    pop ax                            ; the return address, put back last
    push word [fp_am3]
    push word [fp_am2]
    push word [fp_am1]
    push word [fp_am0]
    push word [fp_ae]
    push word [fp_as]
    push ax
    ret

; NOTE both of these CLOBBER AX, and fp_pop_a restores only A's sign BYTE.
; `push word [fp_as]` necessarily reads two bytes, and fp_bs is the next one -
; so popping the pair back would silently undo whatever had just been written
; to B. That is not hypothetical: it was this file's first real bug, and it
; presented as "every operation with a negative operand is wrong" while
; same-sign addition looked perfect.
fp_pop_a:
    ; STKBALANCE-NET: -6 - the other half of fp_push_a
    pop ax
    pop word [fp_tmp]                 ; the saved (as, bs) pair
    pop word [fp_ae]
    pop word [fp_am0]
    pop word [fp_am1]
    pop word [fp_am2]
    pop word [fp_am3]
    push ax
    mov al, [fp_tmp]                  ; ...and put back ONLY A's half of it
    mov [fp_as], al
    ret

; fp_swap_ab - exchange the two accumulators
fp_swap_ab:
    push ax
    push bx
    mov al, [fp_as]
    mov bl, [fp_bs]
    mov [fp_as], bl
    mov [fp_bs], al
    mov ax, [fp_ae]
    mov bx, [fp_be]
    mov [fp_ae], bx
    mov [fp_be], ax
    mov ax, [fp_am0]
    mov bx, [fp_bm0]
    mov [fp_am0], bx
    mov [fp_bm0], ax
    mov ax, [fp_am1]
    mov bx, [fp_bm1]
    mov [fp_am1], bx
    mov [fp_bm1], ax
    mov ax, [fp_am2]
    mov bx, [fp_bm2]
    mov [fp_am2], bx
    mov [fp_bm2], ax
    mov ax, [fp_am3]
    mov bx, [fp_bm3]
    mov [fp_am3], bx
    mov [fp_bm3], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_pack_a - the A accumulator -> the packed double at DS:DI. DI preserved.
; A is left undisturbed; the rounding happens on a copy in fp_t.
;
; Rounds to nearest, TIES TO EVEN. Half-up would bias every long column of
; sums upward, which in a spreadsheet is exactly the visible kind of wrong.
; -----------------------------------------------------------------------------
fp_pack_a:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, fp_am0
    call fp_iszero
    jc .zero
    mov ax, [fp_am0]                  ; round on a copy
    mov [fp_t0], ax
    mov ax, [fp_am1]
    mov [fp_t1], ax
    mov ax, [fp_am2]
    mov [fp_t2], ax
    mov ax, [fp_am3]
    mov [fp_t3], ax
    mov dx, [fp_ae]
    mov word [fp_sticky], 0
    mov bx, fp_t0
    mov cx, 10                        ; ten bits away, keeping them sticky...
    call fp_shrn
    mov ax, [fp_t0]                   ; ...the eleventh is the guard bit
    and ax, 1
    mov [fp_tmp], ax
    call fp_shr1
    cmp word [fp_tmp], 0
    je .rounded                       ; guard clear: nothing to decide
    cmp word [fp_sticky], 0
    jne .roundup                      ; guard set and something below: up
    mov ax, [fp_t0]
    and ax, 1
    jz .rounded                       ; an exact tie to an EVEN significand
.roundup:
    add word [fp_t0], 1
    adc word [fp_t1], 0
    adc word [fp_t2], 0
    adc word [fp_t3], 0
    test word [fp_t3], 0x0020         ; carried into bit 53? renormalize
    jz .rounded
    call fp_shr1
    inc dx
.rounded:
    add dx, FP_EADJ                   ; back to a stored biased exponent
    cmp dx, 0x7FF
    jl .noover
    mov word [fp_t0], 0xFFFF          ; clamp to the largest finite double
    mov word [fp_t1], 0xFFFF
    mov word [fp_t2], 0xFFFF
    mov word [fp_t3], 0x001F
    mov dx, 0x7FE
.noover:
    cmp dx, 1
    jl .zero                          ; underflow flushes
    mov ax, [fp_t0]
    mov [di], ax
    mov ax, [fp_t1]
    mov [di+2], ax
    mov ax, [fp_t2]
    mov [di+4], ax
    mov ax, [fp_t3]
    and ax, 0x000F                    ; drop the implicit 1
    mov cl, 4
    shl dx, cl
    or ax, dx
    cmp byte [fp_as], 0
    je .nosign
    or ax, 0x8000
.nosign:
    mov [di+6], ax
    jmp .out
.zero:
    mov word [di], 0
    mov word [di+2], 0
    mov word [di+4], 0
    mov word [di+6], 0
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Integer conversion
; =============================================================================

; fp_i2a - AX = a signed 16-bit integer -> the A accumulator
fp_i2a:
    push ax
    push bx
    push di
    mov byte [fp_as], 0
    or ax, ax
    jge .abs
    mov byte [fp_as], 1
    neg ax
.abs:
    mov [fp_am0], ax
    mov word [fp_am1], 0
    mov word [fp_am2], 0
    mov word [fp_am3], 0
    mov word [fp_ae], 0
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    pop di
    pop bx
    pop ax
    ret

; fp_a2i - the A accumulator -> AX, a signed 16-bit integer, TRUNCATED toward
; zero. CF=1 if it did not fit, with AX clamped to 32767 / -32768.
fp_a2i:
    push bx
    push cx
    push dx
    mov bx, fp_am0
    call fp_iszero
    jc .zero
    mov dx, [fp_ae]
    cmp dx, 0
    jge .over                         ; 2^63 or more: never fits
    mov cx, dx
    neg cx
    cmp cx, 64
    jae .zero                         ; below 1: truncates to 0
    mov ax, [fp_am0]
    mov [fp_t0], ax
    mov ax, [fp_am1]
    mov [fp_t1], ax
    mov ax, [fp_am2]
    mov [fp_t2], ax
    mov ax, [fp_am3]
    mov [fp_t3], ax
    mov bx, fp_t0
    mov word [fp_sticky], 0
    call fp_shrn
    mov ax, [fp_t1]
    or ax, [fp_t2]
    or ax, [fp_t3]
    or ax, ax
    jnz .over
    mov ax, [fp_t0]
    cmp byte [fp_as], 0
    jne .neg
    cmp ax, 0x7FFF
    ja .over
    jmp .ok
.neg:
    cmp ax, 0x8000
    ja .over
    neg ax
.ok:
    pop dx
    pop cx
    pop bx
    clc
    ret
.zero:
    xor ax, ax
    pop dx
    pop cx
    pop bx
    clc
    ret
.over:
    mov ax, 0x7FFF
    cmp byte [fp_as], 0
    je .oout
    mov ax, 0x8000
.oout:
    pop dx
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; Arithmetic. Every operation is A op= B, leaving the result in A.
; =============================================================================

; fp_neg64 - two's-complement negate the block at BX
fp_neg64:
    not word [bx]
    not word [bx+2]
    not word [bx+4]
    not word [bx+6]
    add word [bx], 1
    adc word [bx+2], 0
    adc word [bx+4], 0
    adc word [bx+6], 0
    ret

; fp_azero - make A a clean positive zero
fp_azero:
    mov byte [fp_as], 0
    mov word [fp_ae], 0
    mov word [fp_am0], 0
    mov word [fp_am1], 0
    mov word [fp_am2], 0
    mov word [fp_am3], 0
    ret

; fp_b_to_t - copy B's mantissa into the temporary
fp_b_to_t:
    push ax
    mov ax, [fp_bm0]
    mov [fp_t0], ax
    mov ax, [fp_bm1]
    mov [fp_t1], ax
    mov ax, [fp_bm2]
    mov [fp_t2], ax
    mov ax, [fp_bm3]
    mov [fp_t3], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_add - A = A + B
;
; The larger exponent wins the alignment, so the two are ordered first and the
; SMALLER operand is the one shifted right - shifting the larger one would
; throw away the bits that matter instead of the ones that do not.
; -----------------------------------------------------------------------------
fps_add:
    push ax
    push bx
    push cx
    push di
    mov bx, fp_bm0
    call fp_iszero
    jc .out                           ; B is zero: A already is the answer
    mov bx, fp_am0
    call fp_iszero
    jc .takeb
    mov ax, [fp_ae]
    cmp ax, [fp_be]
    jge .ordered
    call fp_swap_ab
.ordered:
    mov cx, [fp_ae]
    sub cx, [fp_be]                   ; cx = how far to bring B down
    call fp_b_to_t
    mov word [fp_sticky], 0
    mov bx, fp_t0
    call fp_shrn
    cmp word [fp_sticky], 0           ; bits fell below the working form: JAM
    je .nojam                         ; them into the aligned operand's low
    or word [fp_t0], 1                ; bit, so the sum (or the borrow) still
.nojam:                               ; carries them and rounding can see them
    mov al, [fp_as]
    cmp al, [fp_bs]
    jne .unlike
    mov bx, fp_am0                    ; like signs: a plain sum
    call fp_addt
    jnc .out
    call fp_shr1                      ; it carried out of the top - the carry
    jnc .nodrop                       ; is the new leading 1, and the bit the
    or word [fp_am0], 1               ; shift pushed out stays jammed in the
.nodrop:                              ; low bit for the same reason
    or word [fp_am3], 0x8000
    inc word [fp_ae]
    jmp .out
.unlike:
    mov bx, fp_am0
    call fp_cmp64                     ; A's magnitude against the aligned B's
    jb .bwins
    call fp_subt
    jmp .renorm
.bwins:
    call fp_subt                      ; A - B underflows, so negate the wrap:
    call fp_neg64                     ; |B - A| without needing a third block
    mov al, [fp_bs]
    mov [fp_as], al
.renorm:
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    call fp_iszero
    jnc .out
    call fp_azero                     ; exact cancellation packs as +0
    jmp .out
.takeb:
    call fp_b_to_a
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; fp_sub - A = A - B. One flip and a sum; there is no separate subtraction path
; to get wrong.
fps_sub:
    push ax
    mov al, [fp_bs]
    xor al, 1
    mov [fp_bs], al
    call fp_add
    pop ax
    ret                               ; B IS LEFT UNDEFINED on purpose: fp_add
                                      ; may swap the two operands to align
                                      ; them, so "flip B's sign back" would
                                      ; flip whichever one ended up there.
                                      ; Every caller reloads B before using it.

; -----------------------------------------------------------------------------
; fp_mul - A = A * B
;
; A full 64x64 -> 128 product, of which the top 64 bits become the result and
; the bottom 64 become the sticky bit. Sixteen word multiplies with carry
; propagation; the loop is indexed rather than unrolled because sixteen copies
; of the same six lines is where a transcription error hides.
; -----------------------------------------------------------------------------
fps_mul:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [fp_as]
    xor al, [fp_bs]
    mov [fp_tmp], ax                  ; the result's sign, banked
    mov bx, fp_am0
    call fp_iszero
    jc .zero
    mov bx, fp_bm0
    call fp_iszero
    jc .zero
    mov di, fp_p0                     ; clear the 128-bit accumulator
    mov cx, 8
.clr:
    mov word [di], 0
    add di, 2
    dec cx
    jnz .clr
    xor si, si                        ; si = i*2
.iloop:
    xor di, di                        ; di = j*2
.jloop:
    mov bx, fp_am0
    add bx, si
    mov ax, [bx]
    mov bx, fp_bm0
    add bx, di
    mul word [bx]                     ; dx:ax = am[i] * bm[j]
    mov bx, fp_p0
    add bx, si
    add bx, di
    add [bx], ax
    adc [bx+2], dx
    jnc .nocarry
    add bx, 4
.carry:
    add word [bx], 1
    jnc .nocarry
    add bx, 2
    cmp bx, fp_p0 + 16
    jb .carry
.nocarry:
    add di, 2
    cmp di, 8
    jb .jloop
    add si, 2
    cmp si, 8
    jb .iloop
    mov dx, [fp_ae]                   ; e = ea + eb + 64
    add dx, [fp_be]
    add dx, 64
    test word [fp_p0 + 14], 0x8000    ; is the product already 2^127-aligned?
    jnz .aligned
    mov bx, fp_p0                     ; no: one 128-bit shift left
    call fp_shl1                      ; CF = the bit crossing the 64-bit seam,
    mov ax, 0                         ; and it has to be BANKED NOW - the
    adc ax, 0                         ; second fp_shl1 overwrites the flags
    mov bx, fp_p0 + 8
    call fp_shl1
    or [fp_p0 + 8], ax                ; ...then carried into the low bit
    dec dx
.aligned:
    mov ax, [fp_p0]                   ; everything below the top 64 is sticky
    or ax, [fp_p0 + 2]
    or ax, [fp_p0 + 4]
    or ax, [fp_p0 + 6]
    mov word [fp_sticky], 0
    or ax, ax
    jz .nostick
    mov word [fp_sticky], 1
.nostick:
    mov ax, [fp_p0 + 8]
    mov [fp_am0], ax
    mov ax, [fp_p0 + 10]
    mov [fp_am1], ax
    mov ax, [fp_p0 + 12]
    mov [fp_am2], ax
    mov ax, [fp_p0 + 14]
    mov [fp_am3], ax
    mov [fp_ae], dx
    mov ax, [fp_tmp]
    mov [fp_as], al
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    cmp word [fp_sticky], 0           ; the low 64 bits recorded above: JAM
    je .out                           ; them into the result's low bit, inside
    or word [fp_am0], 1               ; fp_pack_a's sticky region, so an
                                      ; apparent tie there stays inexact
    jmp .out
.zero:
    call fp_azero
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fp_t -= fp_bm
fp_subb_from_t:
    push ax
    mov ax, [fp_bm0]
    sub [fp_t0], ax
    mov ax, [fp_bm1]
    sbb [fp_t1], ax
    mov ax, [fp_bm2]
    sbb [fp_t2], ax
    mov ax, [fp_bm3]
    sbb [fp_t3], ax
    pop ax
    ret

; compare fp_t against fp_bm, flags as for CMP
fp_cmpt_b:
    push ax
    mov ax, [fp_t3]
    cmp ax, [fp_bm3]
    jne .d
    mov ax, [fp_t2]
    cmp ax, [fp_bm2]
    jne .d
    mov ax, [fp_t1]
    cmp ax, [fp_bm1]
    jne .d
    mov ax, [fp_t0]
    cmp ax, [fp_bm0]
.d:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_div - A = A / B. CF=1 and A=0 if B is zero.
;
; Restoring shift-and-subtract, 64 iterations, producing 64 quotient bits.
; A is brought below B first so the quotient cannot need a 65th bit.
; -----------------------------------------------------------------------------
fps_div:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, fp_bm0
    call fp_iszero
    jc .divzero
    mov al, [fp_as]
    xor al, [fp_bs]
    mov [fp_tmp], ax
    mov bx, fp_am0
    call fp_iszero
    jc .zero
    mov dx, [fp_ae]                   ; e = ea - eb - 64
    sub dx, [fp_be]
    sub dx, 64
    mov ax, [fp_am0]                  ; the remainder starts as A's mantissa
    mov [fp_t0], ax
    mov ax, [fp_am1]
    mov [fp_t1], ax
    mov ax, [fp_am2]
    mov [fp_t2], ax
    mov ax, [fp_am3]
    mov [fp_t3], ax
    mov word [fp_sticky], 0
    call fp_cmpt_b
    jb .small
    mov bx, fp_t0                     ; A >= B: bring it under, and pay for it
    call fp_shr1                      ; with one exponent instead of a 65th bit
    inc dx                            ; (INC leaves CF alone)
    jnc .small                        ; ...and a dividend bit pushed out here
    mov word [fp_sticky], 1           ; makes the quotient inexact too
.small:
    mov word [fp_p0], 0
    mov word [fp_p0 + 2], 0
    mov word [fp_p0 + 4], 0
    mov word [fp_p0 + 6], 0
    mov cx, 64
.dloop:
    mov bx, fp_p0
    call fp_shl1
    mov bx, fp_t0
    call fp_shl1
    jc .dosub                         ; past 2^64, so certainly >= the divisor
    call fp_cmpt_b
    jb .nosub
.dosub:
    call fp_subb_from_t
    or word [fp_p0], 1
.nosub:
    dec cx
    jnz .dloop
    mov ax, [fp_p0]
    mov [fp_am0], ax
    mov ax, [fp_p0 + 2]
    mov [fp_am1], ax
    mov ax, [fp_p0 + 4]
    mov [fp_am2], ax
    mov ax, [fp_p0 + 6]
    mov [fp_am3], ax
    mov [fp_ae], dx
    mov ax, [fp_tmp]
    mov [fp_as], al
    mov bx, fp_t0                     ; a non-zero remainder means the quotient
    call fp_setsticky                 ; was cut short, which rounding must know
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    cmp word [fp_sticky], 0           ; ...so JAM it into the low bit, where
    je .exact                         ; fp_pack_a's sticky region can see it
    or word [fp_am0], 1
.exact:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.zero:
    call fp_azero
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.divzero:
    call fp_azero
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fp_cmpab - compare A against B.
; out: AX = -1 (A < B), 0 (equal) or 1 (A > B), and the flags set from it, so
; a caller may use either. Preserves both accumulators.
; -----------------------------------------------------------------------------
fps_cmpab:
    push bx
    push cx
    mov bx, fp_am0
    call fp_iszero
    mov cl, 0
    jnc .anz
    mov cl, 1
.anz:
    mov bx, fp_bm0
    call fp_iszero
    mov ch, 0
    jnc .bnz
    mov ch, 1
.bnz:
    cmp cl, 1
    jne .aisnz
    cmp ch, 1
    je .equal                         ; both zero
    cmp byte [fp_bs], 0               ; A is zero: B's sign decides
    je .less
    jmp .greater
.aisnz:
    cmp ch, 1
    jne .bothnz
    cmp byte [fp_as], 0               ; B is zero: A's sign decides
    je .greater
    jmp .less
.bothnz:
    mov cl, [fp_as]
    cmp cl, [fp_bs]
    je .samesign
    cmp byte [fp_as], 0               ; different signs: the negative is less
    je .greater
    jmp .less
.samesign:
    mov cx, [fp_ae]                   ; same sign: exponent, then mantissa
    cmp cx, [fp_be]
    jl .magless
    jg .magmore
    call fp_b_to_t
    mov bx, fp_am0
    call fp_cmp64
    jb .magless
    ja .magmore
    jmp .equal
.magless:
    cmp byte [fp_as], 0               ; a smaller magnitude is only "less" when
    je .less                          ; both are positive
    jmp .greater
.magmore:
    cmp byte [fp_as], 0
    je .greater
    jmp .less
.less:
    mov ax, -1
    jmp .done
.greater:
    mov ax, 1
    jmp .done
.equal:
    xor ax, ax
.done:
    pop cx
    pop bx
    cmp ax, 0
    ret

; =============================================================================
; DECIMAL CONVERSION - the half that makes any of this visible to a user.
;
; Everything above computes; nothing above can read "3.5" or write it back.
; These two are what a spreadsheet cell actually needs, and they are the
; reason the whole file exists rather than being an exercise.
;
; The caller owes three more scratch items for these:
;   fp_dig   resb 24    the digit string being built
;   fp_d10   dw         the decimal exponent
;   fp_nd    dw         how many digits are in fp_dig
; =============================================================================

; 10^(2^i) for i = 0..8, host-computed so the table is exact rather than
; accumulated. Any 10^k for |k| <= 511 is then a product of at most nine of
; these, chosen by the bits of k - which costs nine multiplies at worst and,
; more importantly, does not drift the way repeated x10 does.
fp_pow10tab:                          ; 10^(2^i), i = 0..8, host-computed
    dw 0x0000, 0x0000, 0x0000, 0x4024      ; 1e1
    dw 0x0000, 0x0000, 0x0000, 0x4059      ; 1e2
    dw 0x0000, 0x0000, 0x8800, 0x40C3      ; 1e4
    dw 0x0000, 0x0000, 0xD784, 0x4197      ; 1e8
    dw 0x8000, 0x37E0, 0xC379, 0x4341      ; 1e16
    dw 0x6E17, 0xB505, 0xB8B5, 0x4693      ; 1e32
    dw 0xF9F5, 0xE93F, 0x4F03, 0x4D38      ; 1e64
    dw 0x1D32, 0xF930, 0x7748, 0x5A82      ; 1e128
    dw 0xBF3C, 0x7F73, 0x4FDD, 0x7515      ; 1e256

; -----------------------------------------------------------------------------
; fp_scale10 - A = A * 10^CX, with CX signed. Preserves CX.
; -----------------------------------------------------------------------------
fp_scale10:
    push ax
    push bx
    push cx
    push si
    push di
    or cx, cx
    jz .out
    mov di, 0                         ; di = 0 multiply, 1 divide
    jns .pos
    neg cx
    mov di, 1
.pos:
    cmp cx, 512                       ; the table stops at 1e256, so |k| > 511
    jb .inrange                       ; cannot walk it - and no double survives
    or di, di                         ; 10^512 either way, so SATURATE to the
    jnz .flush                        ; stated posture instead of silently
                                      ; dropping exponent bits (1e600 as 1e88)
    mov word [fp_ae], 0x1000          ; growing: overflow - an exponent this
    jmp .out                          ; size is what fp_pack_a clamps
.flush:
    call fp_azero                     ; shrinking: underflow flushes to zero
    jmp .out
.inrange:
    mov si, fp_pow10tab
    mov bx, 1                         ; bx = the bit under test
.bit:
    cmp bx, 512
    jae .out
    push cx
    and cx, bx
    pop cx
    jz .nextbit
    push cx
    push si
    call fp_unpack_b                  ; B = this power of ten
    pop si
    or di, di
    jnz .div
    call fp_mul
    jmp .bitdone
.div:
    call fp_div
.bitdone:
    pop cx
.nextbit:
    add si, 8
    shl bx, 1
    jmp .bit
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_u64_to_a - the unsigned 64-bit integer in fp_t becomes A (sign cleared).
; -----------------------------------------------------------------------------
fp_u64_to_a:
    push ax
    push bx
    push di
    mov ax, [fp_t0]
    mov [fp_am0], ax
    mov ax, [fp_t1]
    mov [fp_am1], ax
    mov ax, [fp_t2]
    mov [fp_am2], ax
    mov ax, [fp_t3]
    mov [fp_am3], ax
    mov byte [fp_as], 0
    mov word [fp_ae], 0
    mov bx, fp_am0
    mov di, fp_ae
    call fp_norm
    pop di
    pop bx
    pop ax
    ret

; fp_t = fp_t * 10 + AL, as a 64-bit unsigned integer. Used only while the
; digits still fit, so no overflow check is needed here - fp_atof stops
; feeding it at 18 digits and counts the rest as exponent.
fp_t_mul10add:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, ax                        ; si = the digit, banked
    mov bx, fp_t0                     ; t * 2
    call fp_shl1
    mov ax, [fp_t0]                   ; ...banked as t*2
    mov [fp_p0], ax
    mov ax, [fp_t1]
    mov [fp_p0+2], ax
    mov ax, [fp_t2]
    mov [fp_p0+4], ax
    mov ax, [fp_t3]
    mov [fp_p0+6], ax
    call fp_shl1                      ; t*4
    call fp_shl1                      ; t*8
    mov ax, [fp_p0]                   ; t*8 + t*2 = t*10
    add [fp_t0], ax
    mov ax, [fp_p0+2]
    adc [fp_t1], ax
    mov ax, [fp_p0+4]
    adc [fp_t2], ax
    mov ax, [fp_p0+6]
    adc [fp_t3], ax
    mov ax, si                        ; ...and the digit
    xor ah, ah
    add [fp_t0], ax
    adc word [fp_t1], 0
    adc word [fp_t2], 0
    adc word [fp_t3], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_atof - DS:SI = text -> A. SI advances past what was consumed.
; out: CF=1 and A=0 if there was no number here at all.
;
; Digits accumulate into a 64-bit INTEGER first and are converted once, rather
; than being folded in one at a time as `A = A*10 + d`. Eighteen digits fit
; exactly that way, so "0.1" comes back as the nearest double to 0.1 instead
; of the nearest double to a chain of nine rounded multiplications.
; -----------------------------------------------------------------------------
fp_atof:
    push ax                           ; the value returns through A, never AX
    push bx
    push cx
    push dx
    mov [fp_tmp], si                  ; where the text began, for the CF=1 path
    mov word [fp_t0], 0
    mov word [fp_t1], 0
    mov word [fp_t2], 0
    mov word [fp_t3], 0
    mov byte [fp_sgn], 0              ; The sign gets its OWN word, not fp_tmp:
                                      ; fp_scale10 below reaches fp_pop_a
                                      ; through fp_unpack_b, and fp_pop_a uses
                                      ; fp_tmp itself - so a sign parked there
                                      ; was destroyed by any value with a
                                      ; fraction or an exponent.
    xor bx, bx                        ; bx = digits taken
    xor cx, cx                        ; cx = digits after the point
    xor dx, dx                        ; dx = digits dropped past 18
    cmp byte [si], '-'
    jne .noneg
    mov byte [fp_sgn], 1
    inc si
    jmp .intpart
.noneg:
    cmp byte [si], '+'
    jne .intpart
    inc si
.intpart:
    mov al, [si]
    cmp al, '0'
    jb .dot
    cmp al, '9'
    ja .dot
    inc si
    inc bx
    cmp bx, 18
    ja .dropint
    sub al, '0'
    call fp_t_mul10add
    jmp .intpart
.dropint:
    inc dx                            ; too many: count it as a power of ten
    jmp .intpart
.dot:
    cmp byte [si], '.'
    jne .expo
    inc si
.frac:
    mov al, [si]
    cmp al, '0'
    jb .expo
    cmp al, '9'
    ja .expo
    inc si
    inc bx
    cmp bx, 18
    ja .frac_next                     ; past 18 digits a fraction digit is
    sub al, '0'                       ; simply dropped - it cannot matter
    call fp_t_mul10add
    inc cx
.frac_next:
    jmp .frac
.expo:
    or bx, bx
    jz .none                          ; not a number at all
    push cx
    push dx
    call fp_u64_to_a
    pop dx
    pop cx
    mov al, [si]
    cmp al, 'e'
    je .haveexp
    cmp al, 'E'
    jne .scale
.haveexp:
    push si
    inc si
    push cx
    call fp_pexp                      ; ax = the exponent, or CF=1
    pop cx
    jc .noexp
    add sp, 2                         ; the parse stuck: keep the advanced SI
    add dx, ax
    jmp .scale
.noexp:
    pop si                            ; not really an exponent: leave SI on it
.scale:
    mov ax, dx
    sub ax, cx
    mov cx, ax
    call fp_scale10
    mov al, [fp_sgn]
    mov [fp_as], al
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.none:
    call fp_azero
    mov si, [fp_tmp]                  ; nothing was a number, so nothing was
    pop dx                            ; consumed - not even a leading sign
    pop cx
    pop bx
    pop ax
    stc
    ret

; fp_pexp - DS:SI at an exponent's sign/digits. out: AX = it, SI advanced,
; CF=1 if there were no digits.
fp_pexp:
    push bx
    push cx
    xor ax, ax
    xor cx, cx
    xor bx, bx                        ; BL = 1 if negative, BH = a digit was
                                      ; seen. NOT CX: CL is reloaded with each
                                      ; character below, so a flag kept there
                                      ; is destroyed on the way round - which
                                      ; made every exponent look absent and
                                      ; "1e3" parse as 1.
    cmp byte [si], '-'
    jne .nopos
    mov bl, 1
    inc si
    jmp .dig
.nopos:
    cmp byte [si], '+'
    jne .dig
    inc si
.dig:
    mov cl, [si]
    cmp cl, '0'
    jb .done
    cmp cl, '9'
    ja .done
    sub cl, '0'
    cmp ax, 1000                      ; CAPPED at 9999: one more x10 past here
    jae .full                         ; could wrap AX, and 10^9999 is already
    push dx                           ; past any double - fp_scale10 saturates
    mov dx, 10                        ; it, so only SI still needs to move
    mul dx
    pop dx
    xor ch, ch
    add ax, cx
.full:
    inc si
    mov bh, 1
    jmp .dig
.done:
    or bh, bh
    jz .nodigits
    or bl, bl
    jz .out
    neg ax
.out:
    pop cx
    pop bx
    clc
    ret
.nodigits:
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fp_i2b - AX (signed int) -> the B accumulator, leaving A untouched.
;
; Written out rather than expressed as "bank A, use fp_i2a, copy, restore":
; fp_push_a CLOBBERS AX (it routes the return address through it), so that
; version handed fp_i2a a CODE ADDRESS instead of the caller's integer, and
; every B came out as the same meaningless constant. It cost a long hunt,
; because the arithmetic underneath was provably correct the whole time and
; only the operands were wrong.
; -----------------------------------------------------------------------------
fp_i2b:
    push ax
    push bx
    push di
    mov byte [fp_bs], 0
    or ax, ax
    jge .abs
    mov byte [fp_bs], 1
    neg ax
.abs:
    mov [fp_bm0], ax
    mov word [fp_bm1], 0
    mov word [fp_bm2], 0
    mov word [fp_bm3], 0
    mov word [fp_be], 0
    mov bx, fp_bm0
    mov di, fp_be
    call fp_norm
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_ftoa - A -> a NUL-terminated decimal string at DS:DI.
; in:  AX = how many significant digits to show (clamped to 1..17)
; out: DI preserved; the string is written in place.
;
; %g's shape: fixed notation when the decimal exponent sits in a readable
; range, scientific outside it, and trailing zeros trimmed either way -
; which is what makes 2.5 print as "2.5" and not "2.500000000".
;
; The decimal exponent is ESTIMATED from the binary one and then corrected,
; rather than found by dividing by ten until the value is small enough. The
; estimate costs one multiply; the loop would cost up to 308 divisions and
; would drift a little with each of them.
; -----------------------------------------------------------------------------
fp_ftoa:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, 1
    jge .clamp1
    mov ax, 1
.clamp1:
    cmp ax, 17
    jle .clamp2
    mov ax, 17
.clamp2:
    mov [fp_nd], ax
    push di                           ; the caller's buffer, kept for the end
    mov bx, fp_am0
    call fp_iszero
    jnc .nonzero
    pop di
    mov byte [di], '0'
    mov byte [di+1], 0
    jmp .out
.nonzero:
    pop di
    cmp byte [fp_as], 0
    je .positive
    mov byte [di], '-'
    inc di
    mov byte [fp_as], 0               ; the magnitude from here on
.positive:
    push di                           ; where the digits begin

    ; --- bring the value into [1, 10), counting decades as we go ---
    ;
    ; A FIRST VERSION ESTIMATED this from the binary exponent
    ; (d10 ~ (e+63)*log10(2)) and corrected it once. It was faster and it was
    ; wrong, and finding out why cost far more than the loop it was avoiding
    ; ever could. A spreadsheet value sits within a few decades of 1, so this
    ; runs a handful of times - and unlike the estimate it CANNOT be off by a
    ; decade, because it stops exactly when the value is in range.
    mov word [fp_d10], 0
.toobig:
    mov ax, 10
    call fp_i2b
    call fp_cmpab
    jl .notbig
    call fp_div
    inc word [fp_d10]
    jmp .toobig
.notbig:
    mov ax, 1
    call fp_i2b
    call fp_cmpab
    jge .haverange
    mov ax, 10
    call fp_i2b
    call fp_mul
    dec word [fp_d10]
    jmp .notbig
.haverange:

    ; --- extract the digits ---
    mov si, fp_dig
    mov cx, [fp_nd]
    inc cx                            ; one extra, to round on
.digit:
    push cx
    call fp_a2i                       ; ax = floor(A), A in [0,10)
    cmp ax, 0
    jge .dok
    xor ax, ax
.dok:
    cmp ax, 9
    jle .dok2
    mov ax, 9
.dok2:
    push ax
    add al, '0'
    mov [si], al
    inc si
    pop ax
    call fp_i2b
    call fp_sub                       ; A -= that digit
    mov ax, 10
    call fp_i2b
    call fp_mul                       ; A *= 10
    pop cx
    dec cx
    jnz .digit

    ; --- round on the extra digit, carrying leftward ---
    mov si, fp_dig
    add si, [fp_nd]
    cmp byte [si], '5'
    jb .trim
    mov cx, [fp_nd]
.carry:
    dec si
    inc byte [si]
    cmp byte [si], '9'
    jbe .trim
    mov byte [si], '0'
    dec cx
    jnz .carry
    mov si, fp_dig                    ; carried off the front: 999 -> 1000, so
    mov byte [si], '1'                ; the value gained a decimal place
    inc word [fp_d10]
.trim:
    mov cx, [fp_nd]                   ; drop trailing zeros
    mov si, fp_dig
    add si, cx
.trimloop:
    cmp cx, 1
    jle .trimmed
    dec si
    cmp byte [si], '0'
    jne .trimmed
    dec cx
    jmp .trimloop
.trimmed:
    mov [fp_nd], cx
    push si                           ; NUL-terminate the digit string, so a
    mov si, fp_dig                    ; caller (or a test) can look at exactly
    add si, cx                        ; what was extracted before formatting
    mov byte [si], 0
    pop si
    pop di                            ; back to where the digits go

    ; --- fixed or scientific? ---
    mov ax, [fp_d10]
    cmp ax, -5
    jle .sci
    cmp ax, 15
    jge .sci

    ; fixed
    or ax, ax
    jl .lessthan1
    ; d10 >= 0: some digits before the point
    mov cx, ax
    inc cx                            ; digits before the point
    mov si, fp_dig
    mov bx, [fp_nd]
.fint:
    or cx, cx
    jz .fpoint
    or bx, bx
    jz .fzero
    mov al, [si]
    inc si
    dec bx
    jmp .fput
.fzero:
    mov al, '0'
.fput:
    mov [di], al
    inc di
    dec cx
    jmp .fint
.fpoint:
    or bx, bx
    jz .done                          ; nothing after the point
    mov byte [di], '.'
    inc di
.ffrac:
    or bx, bx
    jz .done
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec bx
    jmp .ffrac
.lessthan1:
    mov byte [di], '0'
    inc di
    mov byte [di], '.'
    inc di
    mov cx, [fp_d10]
    neg cx
    dec cx                            ; that many leading zeros
.lzero:
    or cx, cx
    jz .lzdone
    mov byte [di], '0'
    inc di
    dec cx
    jmp .lzero
.lzdone:
    mov si, fp_dig
    mov cx, [fp_nd]
.lfrac:
    or cx, cx
    jz .done
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp .lfrac

.sci:
    mov si, fp_dig
    mov al, [si]
    mov [di], al
    inc si
    inc di
    mov cx, [fp_nd]
    dec cx
    jcxz .expo
    mov byte [di], '.'
    inc di
.sfrac:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .sfrac
.expo:
    mov byte [di], 'E'
    inc di
    mov ax, [fp_d10]
    or ax, ax
    jge .eplus
    mov byte [di], '-'
    neg ax
    jmp .edig
.eplus:
    mov byte [di], '+'
.edig:
    inc di
    mov cx, 0
    mov bx, 10
.epush:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .epush
.epop:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    dec cx
    jnz .epop
.done:
    mov byte [di], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fp_div32_10e5 - DX:AX / 100000, rounded toward MINUS INFINITY, into AX.
; A plain idiv truncates toward zero, which for a negative exponent estimate
; gives the wrong decade and puts the decimal point one place out.
fp_div32_10e5:
    push bx
    push cx
    push dx
    mov cx, dx
    or cx, cx
    jns .pos
    ; negative: negate, divide, negate, and step down unless it divided exactly
    neg dx
    neg ax
    sbb dx, 0
    mov cx, ax
    mov bx, dx
    call fp_udiv32_10e5
    push ax
    mov ax, cx
    mov dx, bx
    call fp_umod32_10e5
    or ax, ax
    pop ax
    jz .negexact
    neg ax
    dec ax
    jmp .out
.negexact:
    neg ax
    jmp .out
.pos:
    call fp_udiv32_10e5
.out:
    pop dx
    pop cx
    pop bx
    ret

; unsigned DX:AX / 100000 -> AX. Repeated subtraction, which is fine here
; because the dividend is at most |exp|*30103 and the quotient is therefore
; well under 400 - a 32-by-32 divide routine would be more code for a loop
; that runs a few hundred times at most, once per number formatted.
fp_udiv32_10e5:
    push bx
    push cx
    push dx
    xor cx, cx                        ; quotient
.loop:
    cmp dx, 1                         ; is DX:AX >= 100000 (0x000186A0)?
    jb .done
    ja .sub
    cmp ax, 0x86A0
    jb .done
.sub:
    sub ax, 0x86A0
    sbb dx, 1
    inc cx
    jmp .loop
.done:
    mov ax, cx
    pop dx
    pop cx
    pop bx
    ret

; unsigned DX:AX mod 100000 -> AX
fp_umod32_10e5:
    push cx
    push dx
.loop:
    cmp dx, 1
    jb .done
    ja .sub
    cmp ax, 0x86A0
    jb .done
.sub:
    sub ax, 0x86A0
    sbb dx, 1
    jmp .loop
.done:
    pop dx
    pop cx
    ret

; =============================================================================
; Whole-number and root operations (stage 4.0, after the evaluator went
; double). These were integer stand-ins while every value was an integer:
; SQRT was floor(sqrt(n)) and ROUND could only round to tens and up. Once a
; cell can hold 3.14159 both of those are visibly wrong rather than merely
; limited.
;
; Three more scratch items the caller owes:
;   fp_sq  resb 8    the input to fp_sqrt, kept across its iterations
;   fp_g   resb 8    the running guess
;   fp_tv  resb 8    a general packed temporary
; =============================================================================

; -----------------------------------------------------------------------------
; fp_trunc - A = A truncated toward zero, still a double.
;
; No conversion to an integer and back: that would cap the result at a signed
; word and lose everything above 32767. The fraction bits are simply CLEARED -
; the mantissa is normalized, so bit 63 survives and the result needs no
; renormalizing.
; -----------------------------------------------------------------------------
fp_trunc:
    push ax
    push bx
    push cx
    push dx
    mov bx, fp_am0
    call fp_iszero
    jc .out
    mov cx, [fp_ae]
    cmp cx, 0
    jge .out                          ; 2^63 or more: already whole
    neg cx                            ; cx = how many bits are fraction
    cmp cx, 64
    jb .mask
    call fp_azero                     ; |value| < 1 truncates to zero
    jmp .out
.mask:
    mov ax, cx
    mov dx, cx
    and dx, 15                        ; dx = the partial word's bit count
    mov cl, 4
    shr ax, cl                        ; ax = whole words to clear
    mov cx, ax
    mov bx, fp_am0
    jcxz .partial
.clrw:
    mov word [bx], 0
    add bx, 2
    dec cx
    jnz .clrw
.partial:
    or dx, dx
    jz .out
    mov cx, dx
    mov ax, 0xFFFF
    shl ax, cl                        ; keep the high bits of the straddled
    and [bx], ax                      ; word, drop the fractional low ones
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; fp_a_to_tv / fp_tv_to_a - park A in the general temporary and get it back
fp_a_to_tv:
    push di
    mov di, fp_tv
    call fp_pack_a
    pop di
    ret
fp_tv_to_a:
    push si
    mov si, fp_tv
    call fp_unpack_a
    pop si
    ret
fp_tv_to_b:
    push si
    mov si, fp_tv
    call fp_unpack_b
    pop si
    ret

; -----------------------------------------------------------------------------
; fp_floor - A = the largest whole number <= A. Excel's INT, which is NOT
; truncation: INT(-3.7) is -4 where TRUNC(-3.7) is -3.
; -----------------------------------------------------------------------------
fp_floor:
    push ax
    call fp_a_to_tv                   ; keep the original to compare against
    call fp_trunc
    cmp byte [fp_as], 0
    je .out                           ; positive: truncation IS the floor
    call fp_a_to_b                    ; negative: did anything get cut off?
    call fp_tv_to_a
    call fp_cmpab
    je .out                           ; it was already whole
    call fp_b_to_a                    ; step one below the truncation
    mov ax, 1
    call fp_i2b
    call fp_sub
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_sqrt - A = sqrt(A). CF=1 and A=0 if A was negative.
;
; Newton-Raphson, from an initial guess built by HALVING THE BINARY EXPONENT:
; the value is in [2^(e+63), 2^(e+64)), so 2^((e+63)/2) is within a factor of
; two of the answer. Newton doubles the correct bits each pass, so from one
; correct bit six passes reach 53 - and six is what this does, unconditionally,
; because a convergence test costs more than the iteration it would save.
; -----------------------------------------------------------------------------
fp_sqrt:
    push ax
    push bx
    push cx
    push si
    push di
    mov bx, fp_am0
    call fp_iszero
    jc .out                           ; sqrt(0) = 0
    cmp byte [fp_as], 0
    jne .neg
    mov di, fp_sq                     ; bank the input: every pass needs it
    call fp_pack_a
    mov ax, [fp_ae]                   ; the guess: 2^((e+63)/2)
    add ax, 63
    sar ax, 1
    sub ax, 63
    mov word [fp_am0], 0
    mov word [fp_am1], 0
    mov word [fp_am2], 0
    mov word [fp_am3], 0x8000
    mov [fp_ae], ax
    mov byte [fp_as], 0
    mov cx, 6
.iter:
    push cx
    mov di, fp_g                      ; g, banked for this pass
    call fp_pack_a
    mov si, fp_sq                     ; A = a / g
    call fp_unpack_a
    mov si, fp_g
    call fp_unpack_b
    call fp_div
    mov si, fp_g                      ; A = a/g + g
    call fp_unpack_b
    call fp_add
    mov ax, 2                         ; A = that / 2
    call fp_i2b
    call fp_div
    pop cx
    dec cx
    jnz .iter
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.neg:
    call fp_azero
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fp_round - A = A rounded to CX decimal places, half away from zero, which is
; the rule Excel's ROUND states. CX may be negative (round to tens, hundreds).
; -----------------------------------------------------------------------------
fp_round:
    push ax
    push cx
    call fp_scale10                   ; bring the wanted digits above the point
    mov al, [fp_as]                   ; work on the MAGNITUDE, so "half away
    push ax                           ; from zero" is one rule and not two
    mov byte [fp_as], 0
    call fp_half_add                  ; + 0.5 then truncate is the standard
    call fp_trunc                     ; way to say "nearest, ties outward"
    pop ax
    mov [fp_as], al                   ; the sign goes back on; a zero result
    neg cx                            ; still packs as +0
    call fp_scale10                   ; and the point goes back where it was
    pop cx
    pop ax
    ret

; fp_half_add - A = A + 0.5. Its own routine because 0.5 has no integer form
; to build B from, and one exact constant is cheaper than a division.
fp_half_add:
    push ax
    push si
    mov si, fp_half
    call fp_unpack_b
    call fp_add
    pop si
    pop ax
    ret

fp_half:  dw 0x0000, 0x0000, 0x0000, 0x3FE0    ; 0.5, exactly

; =============================================================================
; THE COPROCESSOR PATH
;
; Everything above this line runs on an 8088 with an empty socket. This part
; runs when the socket is filled, and the two must agree to the last bit -
; that is the acceptance test, not a hope.
;
; --- WHY THE 80-BIT FORM AND NOT `fld qword` ---------------------------------
; The obvious shape is to pack A and B back into doubles, `fld qword` them and
; let the part work. It is wrong twice. It ROUNDS: a value part-way through
; fp_atof or fp_scale10 carries all 64 mantissa bits, and squeezing it into 53
; to hand it to the coprocessor throws away exactly the bits the software path
; is keeping - the two paths would then disagree in the last place on values
; that had nothing wrong with them. And it is SLOW, because fp_pack_a is one
; of the longest routines in this file and this would run it three times per
; operation.
;
; The 80-bit temporary real is instead a NEAR-EXACT MATCH FOR THIS FILE'S OWN
; WORKING FORM, which is not a coincidence - both are a 64-bit normalized
; mantissa with an explicit top bit, a sign, and a biased exponent. So the
; conversion is five word moves and one add, it is exact in both directions,
; and both paths then compute with 64 mantissa bits and round to 53 exactly
; once, in fp_pack_a, at the end. That is what makes them agree.
;
;       this file:  value = (-1)^s * m * 2^ae          m in [2^63, 2^64)
;       80-bit:     value = (-1)^s * m * 2^(E - 16446)
;       so          E = ae + 16446
;
; --- THE ONE THING THAT WOULD HANG A MACHINE ---------------------------------
; A coprocessor instruction on a machine with no coprocessor does not fault on
; an 8088 - the WAIT the assembler puts in front of it blocks forever, waiting
; on a BUSY line nothing will lower. So NOTHING below is reached without
; fp_hw, and fp_hw is only set by fp_init after the kernel's boot probe says
; the part answered. Call fp_init once before any arithmetic; if it is never
; called, fp_hw is zero and the software path runs, which is the safe default
; in the right direction.
;
; SPEC.md 1 rule 1 needs no exception for any of this: the 8087 shipped WITH
; the 8086 and NASM assembles its base instruction set at `cpu 8086` already.
; The gate here is a RUN-TIME one, and no `cpu` directive appears anywhere.
;
; The caller owes four more scratch items:
;   fp_hw    db      0 = software, non-zero = the part is there and is used
;   fp_x1    resb 10  A in 80-bit form
;   fp_x2    resb 10  B in 80-bit form
;   fp_sw    dw       where the status word lands
; =============================================================================

FPX_EADJ   equ 16446                  ; 16383 + 63: the extended bias, plus the
                                      ; 63 places between "mantissa is the
                                      ; integer part" and "bit 63 is the 1s"

; -----------------------------------------------------------------------------
; fp_init - decide which path this machine gets. Call once, at startup.
;
; A package that wants the software path on a machine that has the part - to
; compare them, which is exactly what apps/fptest does - clears fp_hw after
; this returns. Nothing else reads the capability, so that is the whole toggle.
; -----------------------------------------------------------------------------
fp_init:
    push ax
    mov byte [fp_hw], 0
    call OSAPI_CPU_INFO               ; AL = tier, AH = CPU_F_* bits
    test ah, CPU_F_X87
    jz .done
    mov byte [fp_hw], 1
    pushf                             ; the stack this empties is SHARED: it
    cli                               ; must not fire mid-way through another
    fninit                            ; instance's fpx_* op (see fpx_add)
    popf
                                      ; round to nearest, EXTENDED precision,
                                      ; every exception masked - the defaults,
                                      ; and the three this file depends on
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_a_to_x / fp_b_to_x - BX = a 10-byte buffer; write A (or B) into it.
;
; A zero has no normalized 80-bit form, so it is written as a true zero rather
; than as a mantissa of 0 with a live exponent - which is an "unnormal" the
; 8087 will happily compute with and later parts will not.
; -----------------------------------------------------------------------------
fp_a_to_x:
    push ax
    mov ax, [fp_am0]
    mov [bx], ax
    mov ax, [fp_am1]
    mov [bx+2], ax
    mov ax, [fp_am2]
    mov [bx+4], ax
    mov ax, [fp_am3]
    mov [bx+6], ax
    or ax, [fp_am0]                   ; AX still holds am3, so this ORs all
    or ax, [fp_am1]                   ; four words together
    or ax, [fp_am2]
    jz .zero
    mov ax, [fp_ae]
    add ax, FPX_EADJ
    jmp .sign
.zero:
    xor ax, ax
.sign:
    cmp byte [fp_as], 0
    je .st
    or ax, 0x8000
.st:
    mov [bx+8], ax
    pop ax
    ret

fp_b_to_x:
    push ax
    mov ax, [fp_bm0]
    mov [bx], ax
    mov ax, [fp_bm1]
    mov [bx+2], ax
    mov ax, [fp_bm2]
    mov [bx+4], ax
    mov ax, [fp_bm3]
    mov [bx+6], ax
    or ax, [fp_bm0]
    or ax, [fp_bm1]
    or ax, [fp_bm2]
    jz .zero
    mov ax, [fp_be]
    add ax, FPX_EADJ
    jmp .sign
.zero:
    xor ax, ax
.sign:
    cmp byte [fp_bs], 0
    je .st
    or ax, 0x8000
.st:
    mov [bx+8], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; fp_x_to_a - BX = a 10-byte buffer; make it A.
;
; An exponent of 0 is a zero or a denormal and both flush, which is what the
; software path's unpack does with the same case. An exponent of 0x7FFF is an
; infinity the part produced on overflow; it is left as the enormous exponent
; it converts to, and fp_pack_a clamps it to the largest finite double - which
; is precisely what the software path does with its own overflow, so the two
; agree there too.
; -----------------------------------------------------------------------------
fp_x_to_a:
    push ax
    mov ax, [bx+8]
    mov byte [fp_as], 0
    test ax, 0x8000
    jz .pos
    mov byte [fp_as], 1
.pos:
    and ax, 0x7FFF
    jz .zero
    sub ax, FPX_EADJ
    mov [fp_ae], ax
    mov ax, [bx]
    mov [fp_am0], ax
    mov ax, [bx+2]
    mov [fp_am1], ax
    mov ax, [bx+4]
    mov [fp_am2], ax
    mov ax, [bx+6]
    mov [fp_am3], ax
    test ax, 0x8000                   ; not normalized: a denormal the part
    jz .zero                          ; produced, which this file flushes
    jmp .out
.zero:
    call fp_azero
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; The five dispatchers. Every call site in this file and in every package goes
; through these, so an internal caller (fp_scale10, fp_sqrt, fp_atof) gets the
; coprocessor exactly as an external one does, without knowing about it.
;
; A jump, not a call, so the tail routine's return value and flags are the
; dispatcher's - fps_div's CF and fps_cmpab's AX-and-flags survive untouched.
; -----------------------------------------------------------------------------
fp_add:
    cmp byte [fp_hw], 0
    jne fpx_add
    jmp fps_add

fp_sub:
    cmp byte [fp_hw], 0
    jne fpx_sub
    jmp fps_sub

fp_mul:
    cmp byte [fp_hw], 0
    jne fpx_mul
    jmp fps_mul

fp_div:
    cmp byte [fp_hw], 0
    jne fpx_div
    jmp fps_div

fp_cmpab:
    cmp byte [fp_hw], 0
    jne fpx_cmpab
    jmp fps_cmpab

; -----------------------------------------------------------------------------
; fpx_stage - both operands into their 10-byte buffers. Which one is pushed
; onto the coprocessor stack first is each operation's own business: the four
; arithmetic ones want A underneath, and fpx_cmpab wants it on top.
; -----------------------------------------------------------------------------
fpx_stage:
    mov bx, fp_x1
    call fp_a_to_x
    mov bx, fp_x2
    call fp_b_to_x
    ret

; A = A + B
;
; EVERY fpx_* HOLDS THE PART ONLY INSIDE A CRITICAL SECTION. The kernel saves
; no coprocessor state across a pre-emption, and the register stack is one
; machine-wide resource: a tick landing between the fld and the fstp can run
; another fp-using instance, whose fp_init runs fninit - and this routine then
; resumes by popping an EMPTY stack, which with exceptions masked stores an
; indeterminate value silently. So pushf/cli from the first fld to the fwait
; after the store (the repo's idiom, never cli/sti), by which point the result
; is back in this instance's own memory. The longest guarded stretch is one
; divide plus two loads and a store - well under a tick even at 4.77 MHz.
fpx_add:
    push ax
    push bx
    call fpx_stage
    pushf
    cli
    fld tword [fp_x1]                 ; ST(0) = A
    fld tword [fp_x2]                 ; ST(0) = B, ST(1) = A
    faddp st1, st0                    ; ST(1) += ST(0), pop: ST(0) = A + B
    fstp tword [fp_x1]
    fwait                             ; the STORE has to have LANDED before the
                                      ; 8088 reads it back - the WAIT the
                                      ; assembler puts in FRONT of an
                                      ; instruction guards the previous
                                      ; operation, not this one's memory write
    popf
    mov bx, fp_x1
    call fp_x_to_a
    pop bx
    pop ax
    ret

; A = A - B.
;
; THE ONE ENCODING WORTH BEING CAREFUL ABOUT. For the register forms the
; reversed variant has the LOWER opcode - DE E0+i is FSUBRP and DE E8+i is
; FSUBP - which is the opposite way round from the memory forms (/4 FSUB, /5
; FSUBR), and is why assemblers have historically disagreed here. NASM emits
; DE E9 for `fsubp st1, st0`, which is Intel's FSUBP ST(1), ST(0) computing
; ST(1) = ST(1) - ST(0). So A goes on FIRST and ends up underneath, exactly as
; in fpx_add. This was checked against the disassembly and then against the
; arithmetic, because a swap here is silent and gives B - A.
fpx_sub:
    push ax
    push bx
    call fpx_stage
    pushf                             ; fpx_add says why
    cli
    fld tword [fp_x1]                 ; ST(0) = A
    fld tword [fp_x2]                 ; ST(0) = B, ST(1) = A
    fsubp st1, st0                    ; ST(1) = A - B, pop
    fstp tword [fp_x1]
    fwait
    popf
    mov bx, fp_x1
    call fp_x_to_a
    pop bx
    pop ax
    ret

; A = A * B
fpx_mul:
    push ax
    push bx
    call fpx_stage
    pushf                             ; fpx_add says why
    cli
    fld tword [fp_x1]
    fld tword [fp_x2]
    fmulp st1, st0
    fstp tword [fp_x1]
    fwait
    popf
    mov bx, fp_x1
    call fp_x_to_a
    pop bx
    pop ax
    ret

; A = A / B, CF=1 and A=0 if B was zero - the software path's contract exactly,
; and the reason the divisor is tested HERE rather than left to the part: a
; masked divide by zero produces an infinity, and this file never produces one.
fpx_div:
    push ax
    push bx
    mov bx, fp_bm0
    call fp_iszero
    jc .divzero
    call fpx_stage
    pushf                             ; fpx_add says why
    cli
    fld tword [fp_x1]                 ; ST(0) = A
    fld tword [fp_x2]                 ; ST(0) = B, ST(1) = A
    fdivp st1, st0                    ; ST(1) = A / B, pop - DE F8+i, the same
    fstp tword [fp_x1]                ; asymmetry fpx_sub explains
    fwait
    popf                              ; before the CLC: popf rewrites CF
    mov bx, fp_x1
    call fp_x_to_a
    clc
    pop bx
    pop ax
    ret
.divzero:
    call fp_azero
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fpx_cmpab - AX = -1 / 0 / 1 with the flags to match, as fps_cmpab returns.
;
; A is loaded LAST so FCOMPP compares ST(0)=A against ST(1)=B in that order,
; and the condition codes then read as an ordinary unsigned compare of A to B:
; C0 is "less", C3 is "equal", and SAHF puts them in CF and ZF where JB and JE
; already look for them.
; -----------------------------------------------------------------------------
fpx_cmpab:
    push bx
    call fpx_stage
    pushf                             ; fpx_add says why
    cli
    fld tword [fp_x2]                 ; ST(0) = B
    fld tword [fp_x1]                 ; ST(0) = A, ST(1) = B
    fcompp                            ; compare A with B, pop both
    fstsw word [fp_sw]
    fwait
    popf                              ; before the SAHF, which rewrites flags
    mov ah, [fp_sw+1]                 ; C3 -> bit 6, C2 -> bit 2, C0 -> bit 0
    sahf                              ; ZF = C3 = equal, CF = C0 = A < B
    jne .ne
    xor ax, ax
    jmp .done
.ne:
    mov ax, 1
    jnc .done
    mov ax, -1
.done:
    pop bx
    cmp ax, 0
    ret
