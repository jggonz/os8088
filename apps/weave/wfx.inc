; =============================================================================
; os8088 - apps/weave/wfx.inc      THE FX FORMULA VM (WEAVE-SPEC 5)
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm after the compiled C, and %included UNCHANGED by
; apps/weave/hosttest/weavevm.asm, which runs it in a raw-QEMU boot sector
; with SS != DS against tools/weavesim.py's own answers (WEAVE-SPEC 12.1.2).
; apps/weave/wvm.inc is the sibling and its header carries the doctrine this
; file obeys word for word: what the gate runs is the SHIPPING TEXT.
;
; -----------------------------------------------------------------------------
; WHY THIS IS ASSEMBLY, AND WHY IT IS A SECOND CORE RATHER THAN PART OF wvm
; -----------------------------------------------------------------------------
; FX values are 32-bit 16.16 fixed point (WEAVE-SPEC 5.2) and this toolchain's
; C has no long at all (SPEC.md 73.7) - so the arithmetic could not be written
; in C even slowly. It is a SEPARATE core from the WJS VM because the two
; share no value model, no stack, no dispatch and no claim: WJS is 16-bit
; signed over the VM claim, FX is 16.16 over the GRID claim, and WEAVE-SPEC
; 2.4's two sections keep their bytecodes apart for the same reason. They
; share WEAVE-SPEC 4.10's slice, and nothing else.
;
; It names NOTHING outside itself. No OSAPI_ slot, no C function, no WEAVE
; state - the boot-sector gate binds it to a cell store it built by hand and
; asks it for answers, with no kernel, no window and no VM claim under it.
;
; -----------------------------------------------------------------------------
; THE SEGMENTS
; -----------------------------------------------------------------------------
;   DS = OURS throughout - the stack slots, the bind state and the scratch
;        are plain DS-relative labels, which is what keeps this file readable
;   ES = the RPN stream's segment (the BUNDLE claim for a WEAVE-SPEC 5.6
;        kind-4 cell, the GRID claim for a kind-6 one), and SWAPPED to the
;        grid claim for the length of one cell read
;   SS:SP = the caller's, untouched
;
; wvm.inc holds the VM claim in DS and reaches its own statics with `cs:`.
; This file does the opposite and it is a decision rather than a difference:
; the WVM's hot memory is ONE claim and it lives there for a whole slice,
; while the FX VM's hot memory is TWO - the stream it is walking and the cell
; store it is reading - and neither is hot enough to pay for the other's
; override. A cell read is a push/pop of ES, about 20 cycles, against
; WEAVE-SPEC 5.5's own 30-60 us per FX op.
;
; C-callable entry points are cdecl (SPEC.md 73.3): arguments at [bp+4],
; [bp+6], ...; the answer in AX; BP, DS, ES, SS:SP and DF preserved.
;
; -----------------------------------------------------------------------------
; THE ARITHMETIC, AND THE ONE PLACE IT WOULD SILENTLY DIVERGE
; -----------------------------------------------------------------------------
; FMUL is `(a * b) >> 16` where the shift is ARITHMETIC over the true 64-bit
; product - the model computes it in Python, where >> floors toward minus
; infinity. Computing it as sign(a)^sign(b) applied to |a|*|b| truncates
; TOWARD ZERO instead, and the two differ by one 65536th on every negative
; product with a fraction. That difference is invisible on a sheet of
; positive numbers and shows up as an off-by-one cent the first time somebody
; subtracts.
;
; So there is NO SIGN HANDLING IN FMUL AT ALL. Two's-complement multiplication
; is congruent mod 2^64, so the UNSIGNED 64-bit product of the two raw 32-bit
; patterns carries the signed product's bits 16..47, and those bits ARE
; wrap32((a*b) >> 16). The routine below is that and nothing else:
;
;   r = high16(a0*b0) + a0*b1 + a1*b0 + (low16(a1*b1) << 16)     mod 2^32
;
; FDIV is the other way round on purpose, because the model is: it divides
; MAGNITUDES (`abs(a << 16) // abs(b)`) and applies the sign afterwards, which
; truncates toward zero. Both are the model's, neither is "the obvious one",
; and the pair is written down here because a core that picked one rule for
; both would agree with the oracle on every positive sheet ever tested.
;
; -----------------------------------------------------------------------------
; THE HELPERS' PROTOCOL, WHICH IS A MEMORY FLAG AND NOT A CPU ONE
; -----------------------------------------------------------------------------
; wfx_one, wfx_two and wfx_agg each have THREE answers - malformed, the error
; value, a number - and an 8086 helper that also has to hand back a 32-bit
; value in DX:AX has no room to say all three in flags without one of them
; being clobbered by the `mov` that reads the next thing. So: CF = 1 means
; malformed (the caller answers -1), and [wfx_iserr] says whether the answer
; is WEAVE-SPEC 5.2's error value, which the helper has ALREADY written into
; the surviving slot. A first draft of this file signalled the middle case in
; ZF and lost it to a `cmp byte [wfx_aggk], 1` two instructions later.
; =============================================================================

section .text

; --- WEAVE-SPEC 5.3's slot types ---------------------------------------------
WFX_TNUM   equ 0
WFX_TRANGE equ 1
WFX_TERR   equ 2

WFX_DEPTH  equ 16               ; 5.3's cap; a deeper formula refused at pack
WFX_SLOT   equ 6                ; type byte, pad, 4-byte payload

; --- WEAVE-SPEC 5.6's cell record --------------------------------------------
WFX_HDR    equ 16               ; the grid claim's header, before the array
WFX_CELL   equ 4                ; kind byte, flags byte, payload word
WFX_FCIRC  equ 1                ; flags bit 0 - displays #CIRC
WFX_FDIRTY equ 2                ; flags bit 1 - 5.5.1's row-dirty mirror
WFX_FERR   equ 4                ; flags bit 2 - the cached value IS #DIV0

; --- the aggregate codes, in wfx_agg's own order -----------------------------
WFX_ASUM   equ 0
WFX_AMIN   equ 1
WFX_AMAX   equ 2
WFX_AAVG   equ 3
WFX_ACOUNT equ 4

; -----------------------------------------------------------------------------
; void wfx_bind(unsigned gseg, unsigned cols, unsigned rows)
;
; The grid claim and its dimensions.  gseg = 0 unbinds, and an unbound core
; answers every cell read "empty" rather than reading paragraph 0.
; -----------------------------------------------------------------------------
_wfx_bind:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov [wfx_gseg], ax
    mov ax, [bp+6]
    mov [wfx_cols], ax
    mov ax, [bp+8]
    mov [wfx_rows], ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; int wfx_cell(int r, int c, int *out2)   - one cell's VALUE, 0-based
;
;   0  empty or a label      (out2 untouched)
;   1  a number              (out2[0] = the low word, out2[1] = the high word)
;   2  the error value       (WEAVE-SPEC 5.2's #DIV0)
;
; The C side's `g.cell()`, the painter and the recalc driver all come through
; here, so there is ONE reader of WEAVE-SPEC 5.6's kinds and not three.
; -----------------------------------------------------------------------------
_wfx_cell:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, [bp+4]                  ; row
    mov cx, [bp+6]                  ; col
    call wfx_read                   ; AX = the answer, DX:[wfx_lo] = the value
    cmp ax, 1
    jne .out
    mov di, [bp+8]
    mov cx, [wfx_lo]
    mov [di], cx
    mov [di+2], dx
    mov ax, 1
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; wfx_read - THE cell reader.  IN: BX = row, CX = col, both 0-based signed
; words that may be out of range.
;
; OUT: AX = 0 empty/label, 1 a number, 2 the error value; and when AX is 1 the
; value is DX (its HIGH word) and [wfx_lo] (its LOW word).  The low half goes
; through memory because AX is already carrying the ANSWER, and a routine with
; three answers and a 32-bit result has one register too few - the alternative
; is two entry points that read WEAVE-SPEC 5.6's six kinds twice.
;
; Clobbers AX BX CX DX SI ES.  ES is left holding the GRID CLAIM: every caller
; either wants it there (wfx_agg walks a range) or puts the RPN stream back
; itself, and restoring it here would be a segment load per cell of every
; aggregate.
; -----------------------------------------------------------------------------
wfx_read:
    mov word [wfx_lo], 0
    cmp word [wfx_gseg], 0
    je .none                        ; unbound: every cell is empty
    or bx, bx
    js .none
    cmp bx, [wfx_rows]
    jae .none
    or cx, cx
    js .none
    cmp cx, [wfx_cols]
    jae .none
    mov ax, bx
    mul word [wfx_cols]             ; row * cols; DX is 0 - 5.6 caps a grid at
    add ax, cx                      ; 6,140 cells, so the offset is one word
    shl ax, 1
    shl ax, 1                       ; * WFX_CELL
    add ax, WFX_HDR
    mov si, ax
    mov es, [wfx_gseg]              ; cc8086:allow - the grid claim, and the
                                    ; caller's own `pop es` restores it
    mov cl, [es:si]                 ; kind
    mov ch, [es:si+1]               ; flags
    mov bx, [es:si+2]               ; payload
    cmp cl, 1
    je .inl
    cmp cl, 2
    je .pool
    cmp cl, 4
    je .form
    cmp cl, 6
    je .form
.none:                              ; 0 empty, 3 atom label, 5 pool string
    xor ax, ax
    xor dx, dx
    ret
.inl:
    mov dx, bx                      ; a whole number: the value IS payload<<16,
    mov ax, 1                       ; so the low word stays 0
    ret
.pool:
    mov dx, [es:bx+2]
    mov ax, [es:bx]
    mov [wfx_lo], ax
    mov ax, 1
    ret
.form:
    test ch, WFX_FERR
    jnz .err
    mov dx, [es:bx+4]               ; 5.6: past the index or length word
    mov ax, [es:bx+2]
    mov [wfx_lo], ax
    mov ax, 1
    ret
.err:
    xor dx, dx
    mov ax, 2
    ret

; -----------------------------------------------------------------------------
; THE THREE 32-BIT FAVOURS THE C SIDE ASKS FOR
;
; They are here rather than in wgrid.c because this toolchain has no long
; (SPEC.md 73.7) and every one of them is a 16x16 product that does not fit
; sixteen bits. Each is four instructions; putting them in the core that
; already owns 16.16 keeps the C free of the pretence that it can do this.
; -----------------------------------------------------------------------------

; int wfx_mag(int lo, int hi, int *out2) - |value| into out2; 1 when negative.
_wfx_mag:
    push bp
    mov bp, sp
    push bx
    mov ax, [bp+4]
    mov dx, [bp+6]
    mov bx, [bp+8]
    or dx, dx
    jns .pos
    neg dx
    neg ax
    sbb dx, 0
    mov [bx], ax
    mov [bx+2], dx
    mov ax, 1
    pop bx
    pop bp
    ret
.pos:
    mov [bx], ax
    mov [bx+2], dx
    xor ax, ax
    pop bx
    pop bp
    ret

; unsigned wfx_cents(unsigned lo) - (lo * 100) >> 16, TRUNCATED.
; WEAVE-SPEC 5.2.1's two decimal places, and the truncation is that section's:
; rounding here would make 0.999 display as 1 while `=A1=1` stayed false.
_wfx_cents:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov dx, 100
    mul dx
    mov ax, dx                      ; >> 16
    pop bp
    ret

; unsigned wfx_frac(unsigned num, unsigned den) - (num*65536 + den/2) / den,
; which is WEAVE-SPEC 5.1's decimal-to-16.16 conversion, rounded half up. The
; quotient fits a word because num < den always (5.1 bounds the fraction at
; four digits, so den <= 10000 and num <= 9999).
_wfx_frac:
    push bp
    mov bp, sp
    mov dx, [bp+4]                  ; num * 65536
    mov ax, [bp+6]
    shr ax, 1                       ; + den/2
    div word [bp+6]
    pop bp
    ret

; -----------------------------------------------------------------------------
; int wfx_eval(unsigned seg, unsigned off, unsigned len, int *out3)
;
; Run one RPN stream (WEAVE-SPEC 5.3).  Answers the number of ops dispatched -
; what WEAVE-SPEC 4.10's budget is counted in - or -1 for a stream this core
; will not run: a bad opcode, an operand past the end, a stack that would
; overflow or underflow, or an FEND with anything but one number on it.
;
;   out3[0] = 0 a number, 2 the error value
;   out3[1] = the value's low word, out3[2] its high word
;
; -1 IS NOT A SCRIPT ERROR AND NOT #DIV0. It means the bytes were not a
; formula, which for a bundle stream means a `.WAB` that never went through a
; packer (WEAVE-SPEC 10.4's own rule) and for a typed one cannot happen -
; 6.9.2 compiled it. The caller displays the cell as #DIV0 and carries on,
; that being the only display the format has for "no answer".
; -----------------------------------------------------------------------------
_wfx_eval:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov word [wfx_sp], 0
    mov word [wfx_ops], 0
    mov ax, [bp+4]
    mov es, ax                      ; cc8086:allow - the RPN stream's segment,
    mov si, [bp+6]                  ; restored by the pop at .out
    mov ax, si
    add ax, [bp+8]
    mov [wfx_end], ax

.step:
    cmp si, [wfx_end]
    jae .bad
    inc word [wfx_ops]
    mov al, [es:si]
    inc si
    cmp al, 0x16
    ja .bad
    xor ah, ah
    mov bx, ax
    shl bx, 1
    jmp [bx+wfx_tab]

; --- 0x00 FEND ---------------------------------------------------------------
.op_end:
    cmp word [wfx_sp], 1
    jne .bad
    mov bx, wfx_stk
    mov al, [bx]
    cmp al, WFX_TRANGE
    je .bad                         ; a range is not an answer (5.3)
    mov di, [bp+10]
    xor ah, ah
    mov [di], ax                    ; 0 a number, 2 the error value
    mov ax, [bx+2]
    mov [di+2], ax
    mov ax, [bx+4]
    mov [di+4], ax
    mov ax, [wfx_ops]
    jmp short .out
.bad:
    mov ax, -1
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; --- 0x01 FNUM  dword --------------------------------------------------------
.op_num:
    mov ax, si
    add ax, 4
    cmp ax, [wfx_end]
    ja .bad
    mov ax, [es:si]
    mov dx, [es:si+2]
    add si, 4
    call wfx_pushn
    jc .bad
    jmp .step

; --- 0x02 FCELL  row8 col8 ---------------------------------------------------
.op_cell:
    mov ax, si
    add ax, 2
    cmp ax, [wfx_end]
    ja .bad
    mov bl, [es:si]
    xor bh, bh
    mov cl, [es:si+1]
    xor ch, ch
    add si, 2
    push si
    call wfx_read                   ; AL 0 empty/label, 1 number, 2 error
    mov cx, ax
    mov ax, [wfx_lo]
    mov es, [bp+4]                  ; cc8086:allow - ES back to the stream
    pop si
    cmp cl, 2
    je .op_cellerr
    cmp cl, 1
    je .op_cellnum
    xor ax, ax                      ; empty and label both read as 0 (5.3)
    xor dx, dx
.op_cellnum:
    call wfx_pushn
    jc .bad
    jmp .step
.op_cellerr:
    call wfx_pusherr
    jc .bad
    jmp .step

; --- 0x03 FRANGE  r1 c1 r2 c2 ------------------------------------------------
.op_range:
    mov ax, si
    add ax, 4
    cmp ax, [wfx_end]
    ja .bad
    mov ax, [es:si]                 ; r1 in AL, c1 in AH
    mov dx, [es:si+2]               ; r2 in DL, c2 in DH
    add si, 4
    call wfx_slot
    jc .bad
    mov byte [bx], WFX_TRANGE
    mov [bx+2], ax
    mov [bx+4], dx
    inc word [wfx_sp]
    jmp .step

; --- 0x04..0x07 the four arithmetic ops --------------------------------------
.op_add:
    call wfx_two
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step                       ; an operand was the error value
    add ax, cx
    adc dx, bx
    jmp short .setn
.op_sub:
    call wfx_two
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step
    sub ax, cx
    sbb dx, bx
    jmp short .setn
.op_mul:
    call wfx_two
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step
    call wfx_mul
    jmp short .setn
.op_div:
    call wfx_two
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step
    call wfx_div
    jc .seterr
.setn:
    call wfx_setn                   ; overwrite the surviving slot
    jmp .step
.seterr:
    call wfx_seterr
    jmp .step

; --- 0x08 FNEG ---------------------------------------------------------------
.op_neg:
    call wfx_one
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step
    neg dx
    neg ax
    sbb dx, 0
    jmp short .setn

; --- 0x09..0x0E the six comparisons ------------------------------------------
; The model compares SIGNED 32-bit values and yields 1.0 or 0.0 (5.2).
.op_eq:
    mov cl, 0
    jmp short .cmp
.op_ne:
    mov cl, 1
    jmp short .cmp
.op_lt:
    mov cl, 2
    jmp short .cmp
.op_le:
    mov cl, 3
    jmp short .cmp
.op_gt:
    mov cl, 4
    jmp short .cmp
.op_ge:
    mov cl, 5
.cmp:
    mov [wfx_cmpk], cl
    call wfx_two                    ; DX:AX = a, BX:CX = b
    jc .bad
    cmp byte [wfx_iserr], 0
    jne .step
    call wfx_docmp                  ; -> DX:AX = 1.0 or 0.0
    jmp short .setn

; --- 0x0F..0x13 the five aggregates ------------------------------------------
.op_sum:
    mov al, WFX_ASUM
    jmp short .agg
.op_min:
    mov al, WFX_AMIN
    jmp short .agg
.op_max:
    mov al, WFX_AMAX
    jmp short .agg
.op_avg:
    mov al, WFX_AAVG
    jmp short .agg
.op_count:
    mov al, WFX_ACOUNT
.agg:
    mov [wfx_aggk], al
    push si
    call wfx_agg
    mov es, [bp+4]                  ; cc8086:allow - ES back to the stream
    pop si
    jc .bad2
    cmp byte [wfx_iserr], 0
    jne .aggerr
    jmp short .setn
.bad2:
    jmp .bad
.aggerr:
    jmp .seterr

; --- 0x14 FIF  (c a b -> a or b), both arms already evaluated (eager) --------
.op_if:
    cmp word [wfx_sp], 3
    jb .bad2
    sub word [wfx_sp], 2
    mov bx, [wfx_sp]
    dec bx                          ; the slot holding `c`
    call wfx_addr
    mov al, [bx]
    or al, [bx+WFX_SLOT]
    or al, [bx+2*WFX_SLOT]
    test al, WFX_TERR               ; eager: any arm in error is the answer
    jnz .iferr
    test al, WFX_TRANGE
    jnz .bad2
    mov ax, [bx+2]
    or ax, [bx+4]
    jz .ifb
    mov ax, [bx+WFX_SLOT+2]         ; c != 0: the `a` arm
    mov dx, [bx+WFX_SLOT+4]
    jmp short .ifset
.ifb:
    mov ax, [bx+2*WFX_SLOT+2]
    mov dx, [bx+2*WFX_SLOT+4]
.ifset:
    mov byte [bx], WFX_TNUM
    mov [bx+2], ax
    mov [bx+4], dx
    jmp .step
.iferr:
    mov byte [bx], WFX_TERR
    xor ax, ax
    mov [bx+2], ax
    mov [bx+4], ax
    jmp .step

; --- 0x15 FABS ---------------------------------------------------------------
.op_abs:
    call wfx_one
    jc .bad2
    cmp byte [wfx_iserr], 0
    jne .stepj
    or dx, dx
    jns .setnj
    neg dx
    neg ax
    sbb dx, 0
.setnj:
    jmp .setn
.stepj:
    jmp .step

; --- 0x16 FROUND -------------------------------------------------------------
; ip = (|a| + 0x8000) >> 16, then the sign put back: half AWAY FROM ZERO
; (WEAVE-SPEC 5.2), with the fraction bits cleared.
.op_round:
    call wfx_one
    jc .bad2
    cmp byte [wfx_iserr], 0
    jne .stepj
    mov bx, dx
    and bx, 0x8000                  ; the sign, banked
    or dx, dx
    jns .rpos
    neg dx
    neg ax
    sbb dx, 0
.rpos:
    add ax, 0x8000
    adc dx, 0
    mov ax, dx                      ; ip
    xor dx, dx
    xchg ax, dx                     ; DX:AX = ip << 16
    or bx, bx
    jz .setnj
    neg dx
    neg ax
    sbb dx, 0
    jmp short .setnj

; =============================================================================
; THE STACK HELPERS
; =============================================================================

; wfx_addr - BX = the address of slot BX.  AX and every other register kept.
wfx_addr:
    push ax
    mov ax, bx
    add bx, bx
    add bx, bx                      ; * 4
    add bx, ax
    add bx, ax                      ; * 6 = WFX_SLOT
    add bx, wfx_stk
    pop ax
    ret

; wfx_slot - BX = the address of the NEXT FREE slot; CF = 1 when full.
wfx_slot:
    push ax
    mov bx, [wfx_sp]
    cmp bx, WFX_DEPTH
    jae .full
    call wfx_addr
    pop ax
    clc
    ret
.full:
    pop ax
    stc
    ret

; wfx_pushn - push DX:AX as a number.  CF = 1 when the stack is full.
wfx_pushn:
    push bx
    call wfx_slot
    jc .no
    mov byte [bx], WFX_TNUM
    mov [bx+2], ax
    mov [bx+4], dx
    inc word [wfx_sp]
    clc
.no:
    pop bx
    ret

; wfx_pusherr - ...and push WEAVE-SPEC 5.2's error value.
wfx_pusherr:
    push ax
    push bx
    call wfx_slot
    jc .no
    mov byte [bx], WFX_TERR
    mov word [bx+2], 0
    mov word [bx+4], 0
    inc word [wfx_sp]
    clc
.no:
    pop bx
    pop ax
    ret

; wfx_setn - overwrite the TOP slot with the number DX:AX.
wfx_setn:
    push bx
    mov bx, [wfx_sp]
    dec bx
    call wfx_addr
    mov byte [bx], WFX_TNUM
    mov [bx+2], ax
    mov [bx+4], dx
    pop bx
    ret

; wfx_seterr - ...and with the error value.
wfx_seterr:
    push ax
    push bx
    mov bx, [wfx_sp]
    dec bx
    call wfx_addr
    mov byte [bx], WFX_TERR
    mov word [bx+2], 0
    mov word [bx+4], 0
    pop bx
    pop ax
    ret

; wfx_one - read the top slot as a NUMBER, leaving it in place for the caller
; to overwrite.  CF = 1 malformed; else [wfx_iserr] says whether the answer is
; the error value (already sitting in the slot), and DX:AX is the value.
wfx_one:
    mov byte [wfx_iserr], 0
    cmp word [wfx_sp], 1
    jb .empty
    mov bx, [wfx_sp]
    dec bx
    call wfx_addr
    mov al, [bx]
    cmp al, WFX_TRANGE
    je .empty
    cmp al, WFX_TERR
    je .iserr
    mov ax, [bx+2]
    mov dx, [bx+4]
    clc
    ret
.iserr:
    mov byte [wfx_iserr], 1
    clc
    ret
.empty:
    stc
    ret

; wfx_two - the same for a PAIR.  The stack drops by one; the surviving slot
; is the caller's to overwrite; DX:AX = a and BX:CX = b.  An error operand
; leaves the error value in the survivor and sets [wfx_iserr].
wfx_two:
    mov byte [wfx_iserr], 0
    cmp word [wfx_sp], 2
    jb .empty
    dec word [wfx_sp]
    mov bx, [wfx_sp]
    call wfx_addr                   ; BX = &b, the slot just dropped
    mov al, [bx]
    or al, [bx-WFX_SLOT]
    test al, WFX_TRANGE
    jnz .empty                      ; either operand a range: malformed
    test al, WFX_TERR
    jnz .iserr
    mov cx, [bx+2]
    mov ax, [bx+4]
    mov [wfx_lo], ax                ; b's high word, parked while AX is reused
    mov ax, [bx-WFX_SLOT+2]
    mov dx, [bx-WFX_SLOT+4]
    mov bx, [wfx_lo]                ; BX:CX = b, DX:AX = a
    clc
    ret
.iserr:
    call wfx_seterr                 ; the survivor becomes the error value
    mov byte [wfx_iserr], 1
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; wfx_cmp32 - DX:AX against BX:CX, SIGNED.  -> AL = 0 equal, 1 a < b, 2 a > b.
; Clobbers AX CX DX.
;
; The multi-word subtract's LAST sbb leaves SF and OF right for the signed
; comparison and ZF right for the HIGH WORD ONLY, so the equality test has to
; come after the less-than has been banked in a register - `mov` does not
; touch flags, which is what makes the order below work.
; -----------------------------------------------------------------------------
wfx_cmp32:
    sub ax, cx
    sbb dx, bx
    mov cl, 1                       ; assume a < b
    jl .keep
    mov cl, 2                       ; ...else a > b, or equal
.keep:
    or ax, dx
    jnz .out
    mov cl, 0                       ; the whole 32-bit result is zero
.out:
    mov al, cl
    xor ah, ah
    ret

; wfx_docmp - DX:AX = a, BX:CX = b, [wfx_cmpk] = the op.  -> DX:AX = 1.0 or 0.
wfx_docmp:
    call wfx_cmp32                  ; AL = 0 eq, 1 lt, 2 gt
    mov bl, [wfx_cmpk]
    xor dx, dx
    xor cx, cx                      ; CX = 1 when the answer is true
    cmp bl, 0
    je .eq
    cmp bl, 1
    je .ne
    cmp bl, 2
    je .lt
    cmp bl, 3
    je .le
    cmp bl, 4
    je .gt
    cmp al, 1                       ; GE: not less than
    jne .yes
    jmp short .no
.gt:
    cmp al, 2
    je .yes
    jmp short .no
.le:
    cmp al, 2                       ; LE: not greater than
    jne .yes
    jmp short .no
.lt:
    cmp al, 1
    je .yes
    jmp short .no
.ne:
    cmp al, 0
    jne .yes
    jmp short .no
.eq:
    cmp al, 0
    jne .no
.yes:
    mov cx, 1
.no:
    mov dx, cx                      ; 1.0 in 16.16 is 0x0001:0x0000
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; wfx_mul - DX:AX = a, BX:CX = b  ->  DX:AX = wrap32((a*b) >> 16)
; The header says why there is no sign handling.  Clobbers AX BX CX DX SI DI.
; -----------------------------------------------------------------------------
wfx_mul:
    push si
    push di
    mov [wfx_a0], ax
    mov [wfx_a1], dx
    mov [wfx_b0], cx
    mov [wfx_b1], bx

    mov ax, [wfx_a0]
    mul word [wfx_b0]
    mov di, dx                      ; SI:DI = the running 32-bit answer, whose
    xor si, si                      ; bottom starts at high16(a0*b0)

    mov ax, [wfx_a0]
    mul word [wfx_b1]
    add di, ax
    adc si, dx

    mov ax, [wfx_a1]
    mul word [wfx_b0]
    add di, ax
    adc si, dx

    mov ax, [wfx_a1]
    mul word [wfx_b1]
    add si, ax                      ; only its LOW word reaches bits 32..47

    mov ax, di
    mov dx, si
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; wfx_div - DX:AX = a, BX:CX = b  ->  DX:AX = wrap32(a / b); CF = 1 on b == 0
;
; The model's arithmetic: `q = abs(a << 16) // abs(b)` with the pair's sign
; applied afterwards, so the truncation is TOWARD ZERO and not the floor FMUL
; takes. 48 bits over 32 by restoring shift-subtract - an 8086 `div` is no use
; at any step, because the quotient does not fit sixteen bits.
;
; It is the expensive op in this core (48 iterations, ~1,500 cycles, ~310 us
; on the target) and it is the rare one: a sheet divides where it averages.
; WEAVE-SPEC 5.5's own 30-60 us per FX op is the mean and this is the tail.
; -----------------------------------------------------------------------------
wfx_div:
    push si
    push di
    xor si, si                      ; SI = 0xFFFF when the answer is negative
    or bx, bx
    jnz .bok
    or cx, cx
    jz .zero
.bok:
    or dx, dx
    jns .apos
    neg dx
    neg ax
    sbb dx, 0
    not si
.apos:
    or bx, bx
    jns .bpos
    neg bx
    neg cx
    sbb bx, 0
    not si
.bpos:
    mov [wfx_n2], dx                ; N = |a| << 16, 48 bits, MSW first
    mov [wfx_n1], ax
    mov word [wfx_n0], 0
    mov [wfx_d0], cx
    mov [wfx_d1], bx
    xor cx, cx                      ; CX:DX = the running remainder (hi:lo)
    xor dx, dx
    xor ax, ax
    mov [wfx_q0], ax
    mov [wfx_q1], ax
    mov di, 48
.bit:
    shl word [wfx_n0], 1            ; remainder <<= 1, taking N's top bit in
    rcl word [wfx_n1], 1
    rcl word [wfx_n2], 1
    rcl dx, 1
    rcl cx, 1
    mov ax, 0                       ; `mov` leaves CF alone, which is what
    rcl ax, 1                       ; makes this the remainder's 33rd bit
    shl word [wfx_q0], 1            ; quotient <<= 1
    rcl word [wfx_q1], 1
    or ax, ax
    jnz .sub                        ; overflowed 32 bits: certainly >= divisor
    cmp cx, [wfx_d1]
    jb .next
    ja .sub
    cmp dx, [wfx_d0]
    jb .next
.sub:
    sub dx, [wfx_d0]
    sbb cx, [wfx_d1]
    or word [wfx_q0], 1
.next:
    dec di
    jnz .bit
    mov ax, [wfx_q0]
    mov dx, [wfx_q1]
    or si, si
    jz .done
    neg dx
    neg ax
    sbb dx, 0
.done:
    pop di
    pop si
    clc
    ret
.zero:
    pop di
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; wfx_agg - the five aggregates (WEAVE-SPEC 5.3, 5.4).  The top slot holds a
; range descriptor and is replaced by the answer.
;   CF = 1            malformed: no range on top
;   [wfx_iserr] = 1   a cell in the range held the error value
;   else              DX:AX = the answer
;
; Empty and LABEL cells are SKIPPED, which is why COUNT counts numeric cells
; only and AVG over an all-empty range is 0 rather than a division by zero.
; ES is left holding the grid claim; the dispatcher puts the stream back.
; -----------------------------------------------------------------------------
wfx_agg:
    mov byte [wfx_iserr], 0
    cmp word [wfx_sp], 1
    jb .bad
    mov bx, [wfx_sp]
    dec bx
    call wfx_addr
    cmp byte [bx], WFX_TRANGE
    jne .bad
    mov ax, [bx+2]                  ; r1 in AL, c1 in AH
    mov dx, [bx+4]                  ; r2 in DL, c2 in DH
    mov [wfx_r1], al
    mov [wfx_c1], ah
    mov [wfx_r2], dl
    mov [wfx_c2], dh
    mov word [wfx_acc], 0
    mov word [wfx_acc+2], 0
    mov word [wfx_n], 0
    mov byte [wfx_anyerr], 0
    mov byte [wfx_first], 1
    mov al, [wfx_r1]
    mov [wfx_rr], al
.row:
    mov al, [wfx_rr]
    cmp al, [wfx_r2]
    ja .fin
    mov al, [wfx_c1]
    mov [wfx_cc], al
.col:
    mov al, [wfx_cc]
    cmp al, [wfx_c2]
    ja .rownext
    mov bl, [wfx_rr]
    xor bh, bh
    mov cl, [wfx_cc]
    xor ch, ch
    call wfx_read
    mov cx, ax
    mov ax, [wfx_lo]
    cmp cl, 2
    jne .notf
    mov byte [wfx_anyerr], 1
    jmp short .colnext
.notf:
    cmp cl, 1
    jne .colnext                    ; empty and label: SKIPPED (5.3)
    inc word [wfx_n]
    call wfx_accum
.colnext:
    inc byte [wfx_cc]
    jmp short .col
.rownext:
    inc byte [wfx_rr]
    jmp short .row
.fin:
    cmp byte [wfx_anyerr], 0
    jne .iserr
    mov al, [wfx_aggk]
    cmp al, WFX_ACOUNT
    je .docount
    cmp al, WFX_AAVG
    je .doavg
    mov ax, [wfx_acc]               ; SUM, MIN and MAX all live in the
    mov dx, [wfx_acc+2]             ; accumulator already
    clc
    ret
.docount:
    mov dx, [wfx_n]
    xor ax, ax
    clc
    ret
.doavg:
    cmp word [wfx_n], 0
    je .avgzero
    mov ax, [wfx_acc]
    mov dx, [wfx_acc+2]
    mov bx, [wfx_n]                 ; BX:CX = n << 16
    xor cx, cx
    call wfx_div
    clc
    ret
.avgzero:
    xor ax, ax
    xor dx, dx
    clc
    ret
.iserr:
    mov byte [wfx_iserr], 1
    clc
    ret
.bad:
    stc
    ret

; wfx_accum - fold DX:AX into the accumulator per [wfx_aggk].
wfx_accum:
    mov cl, [wfx_aggk]
    cmp cl, WFX_ACOUNT
    je .out                         ; COUNT keeps only [wfx_n]
    cmp cl, WFX_AMIN
    je .minmax
    cmp cl, WFX_AMAX
    je .minmax
    add [wfx_acc], ax               ; SUM and AVG both accumulate a sum
    adc [wfx_acc+2], dx
.out:
    ret
.minmax:
    cmp byte [wfx_first], 0
    jne .take                       ; the first value is taken unconditionally
    push ax
    push dx
    mov bx, [wfx_acc+2]
    mov cx, [wfx_acc]
    call wfx_cmp32                  ; AL = 0 equal, 1 new < acc, 2 new > acc
    mov ch, al
    pop dx
    pop ax
    cmp byte [wfx_aggk], WFX_AMIN
    je .ismin
    cmp ch, 2                       ; MAX: take when the new one is bigger
    je .take
    ret
.ismin:
    cmp ch, 1                       ; MIN: ...and when it is smaller
    je .take
    ret
.take:
    mov [wfx_acc], ax
    mov [wfx_acc+2], dx
    mov byte [wfx_first], 0
    ret

section .rodata

; The dispatch table: WEAVE-SPEC 5.3's 23 opcodes in their pinned order.
; Unlike wvm.inc's it is not generated - 5.3's table is 23 rows in this file's
; own sight, where the WVM's 38 are shared with a model that emits them.
wfx_tab:
    dw _wfx_eval.op_end,   _wfx_eval.op_num,   _wfx_eval.op_cell
    dw _wfx_eval.op_range, _wfx_eval.op_add,   _wfx_eval.op_sub
    dw _wfx_eval.op_mul,   _wfx_eval.op_div,   _wfx_eval.op_neg
    dw _wfx_eval.op_eq,    _wfx_eval.op_ne,    _wfx_eval.op_lt
    dw _wfx_eval.op_le,    _wfx_eval.op_gt,    _wfx_eval.op_ge
    dw _wfx_eval.op_sum,   _wfx_eval.op_min,   _wfx_eval.op_max
    dw _wfx_eval.op_avg,   _wfx_eval.op_count, _wfx_eval.op_if
    dw _wfx_eval.op_abs,   _wfx_eval.op_round

section .bss

wfx_gseg:   resw 1                  ; the grid claim, 0 = unbound
wfx_cols:   resw 1
wfx_rows:   resw 1
wfx_sp:     resw 1                  ; slots in use, 0..WFX_DEPTH
wfx_ops:    resw 1
wfx_end:    resw 1                  ; one past the stream's last byte
wfx_lo:     resw 1                  ; wfx_read's value, parked past its answer
wfx_iserr:  resb 1                  ; the helpers' middle answer - see the head
wfx_cmpk:   resb 1                  ; which comparison is running
wfx_aggk:   resb 1                  ; which aggregate is running
wfx_first:  resb 1                  ; MIN/MAX: has a value been taken yet?
wfx_anyerr: resb 1
wfx_r1:     resb 1
wfx_c1:     resb 1
wfx_r2:     resb 1
wfx_c2:     resb 1
wfx_rr:     resb 1
wfx_cc:     resb 1
wfx_n:      resw 1                  ; numeric cells seen, for COUNT and AVG
wfx_acc:    resd 1
wfx_a0:     resw 1                  ; wfx_mul's four halves
wfx_a1:     resw 1
wfx_b0:     resw 1
wfx_b1:     resw 1
wfx_n0:     resw 1                  ; wfx_div's 48-bit dividend...
wfx_n1:     resw 1
wfx_n2:     resw 1
wfx_d0:     resw 1                  ; ...its 32-bit divisor...
wfx_d1:     resw 1
wfx_q0:     resw 1                  ; ...and its quotient
wfx_q1:     resw 1
wfx_stk:    resb WFX_DEPTH * WFX_SLOT

section .text                       ; SWITCH BACK (CLAUDE.md's section rule)
