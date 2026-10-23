; =============================================================================
; os8088 - apps/xtmp3/x3std.asm - XTMP3's STANDARD backend (SPEC.md 121.3,
;                                  121.4), an OP_SEG part
;
; tools/x3sim.py's Decoder.granule() from requant_i on, BIT-EXACT: requantise,
; M/S and intensity stereo, reorder, antialias, the band scale, the IMDCT (36
; and 3 x 12, minimp3's factorisation with the twiddle merged into the
; window) with its overlap, frequency inversion, the optional mono mix, the
; recursive DCT-II, the X-ring synthesis and the PCM with its clip count - at
; divisors 1, 2 and 4, where subbands at and above 32/div are not
; reconstructed. 8086 integer only: imul 16 x 16, 32-bit values in register
; pairs, 48-bit products where a 32-bit value meets a 16-bit constant, and
; every saturation of x3sim's format table a `jo` (or an explicit range test)
; to a clamp that is counted in its stage.
;
; THE ABI is apps/xtmp3/x3gran.inc sections 1-3: the 18-byte header, the six
; far verbs, DS = this part's own data segment on every one. The part carries
; its tables (tools/x3tables.py's x3std.bin from P43M on: the Huffman tables
; are the FRONT part's) and its state; CAPS asks for no work segment.
;
; The DCT-II and the window are build/x3sgen.inc, written by tools/x3stdgen.py
; from a TRACE of x3sim.dct2_i and IntSynth, so their order of operations is
; the reference's; this file defines the arithmetic of each of their macros.
;
; Labels: xs_ (this part), x3g_ (the shared CRC). Stack: at most 24 bytes of
; the caller's beyond the far call (the verbs push seven registers and call
; one level deep).
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "x3gran.inc"
%include "x3std.inc"
%include "x3sgen.inc"

; the stages, x3sim STAGES' order (X3Z_STAGE)
XS_ST_REQ   equ 0
XS_ST_STER  equ 1
XS_ST_ALIAS equ 2
XS_ST_SCALE equ 3
XS_ST_IMDCT equ 4
XS_ST_MIX   equ 5
XS_ST_DCT   equ 6
XS_ST_WIN   equ 7

; the slice of x3std.bin this part carries: from P43M to the end
XS_TOFF     equ X3T_P43M_OFF
%define XS_T(name) (xs_tab + X3T_ %+ name %+ _OFF - XS_TOFF)

; --- the header (x3gran.inc section 2) --------------------------------------
    db 'X3', X3G_ABI, X3K_STD, X3V_COUNT, 0
    dw xs_caps, xs_open, xs_reset, xs_granule, xs_stats, xs_close

; =============================================================================
; THE ARITHMETIC OF THE GENERATED SYNTHESIS (build/x3sgen.inc)
; BX = the slot's first input, DI = ring + 2 pos; every macro keeps BX, DI
; =============================================================================

; XS_CNT stage - one saturation more in a stage (a dword)
%macro XS_CNT 1
    add word [xs_sat + (%1) * 4], 1
    adc word [xs_sat + (%1) * 4 + 2], 0
%endmacro

; XS_SAT32 stage - after an add/adc or sub/sbb into DX:AX: a signed overflow
; becomes the clamp on the side the true value lies, counted. The clamp is
; out of line (xs_fxs_<stage>: a CALL keeps the flags it decides on), five
; bytes a site: it never runs on real material, and there are thousands
%macro XS_SAT32 1
    jno %%ok
  %if (%1) == XS_ST_DCT
    call xs_fxs_dct
  %elif (%1) == XS_ST_STER
    call xs_fxs_ster
  %else
    call xs_fxs_imdct
  %endif
%%ok:
%endmacro

; the clamps: SF = 1 after an overflow means the true value is positive
%macro XS_FXS 2
%1:
    mov ax, 0xFFFF
    mov dx, 0x7FFF
    js %%c
    inc ax                          ; (80000000h)
    inc dx
%%c:
    XS_CNT %2
    ret
%endmacro

%macro XS_ADD 3
    mov ax, [%2]
    mov dx, [%2+2]
    add ax, [%3]
    adc dx, [%3+2]
    XS_SAT32 XS_ST_DCT
    mov [%1], ax
    mov [%1+2], dx
%endmacro

%macro XS_SUB 3
    mov ax, [%2]
    mov dx, [%2+2]
    sub ax, [%3]
    sbb dx, [%3+2]
    XS_SAT32 XS_ST_DCT
    mov [%1], ax
    mov [%1+2], dx
%endmacro

%macro XS_NEG 2
    xor ax, ax
    xor dx, dx
    sub ax, [%2]
    sbb dx, [%2+2]
    XS_SAT32 XS_ST_DCT
    mov [%1], ax
    mov [%1+2], dx
%endmacro

; XS_P first, neg, operand, multiplier - the 48-bit accumulator CX:BP:SI
; (first: =, else +=) plus or minus (neg) the exact product of the int32 at
; the operand by the NON-NEGATIVE word at the multiplier address
%macro XS_P 4
    mov ax, [%3]
    mul word [%4]                   ; low word x c, unsigned
  %if %1
    %if %2
    xor si, si
    xor bp, bp
    xor cx, cx
    sub si, ax
    sbb bp, dx
    sbb cx, 0
    %else
    mov si, ax
    mov bp, dx
    xor cx, cx
    %endif
  %else
    %if %2
    sub si, ax
    sbb bp, dx
    sbb cx, 0
    %else
    add si, ax
    adc bp, dx
    adc cx, 0
    %endif
  %endif
    mov ax, [%3+2]
    imul word [%4]                  ; high word x c, signed, a word up
  %if %2
    sub bp, ax
    sbb cx, dx
  %else
    add bp, ax
    adc cx, dx
  %endif
%endmacro

; XS_R15 dst, stage - sat32((CX:BP:SI + 2^14) >> 15) into dst. Out of
; range, xs_fx15_<stage> loads the clamp PRE-SHIFTED, so the one path after
; it is the same either way
%macro XS_R15 2
    add si, 0x4000
    adc bp, 0
    adc cx, 0
    mov ax, cx                      ; it fits when bits 47 and 46 agree
    add ax, 0x4000
    jns %%fit
  %if (%2) == XS_ST_DCT
    call xs_fx15_dct
  %else
    call xs_fx15_alias
  %endif
%%fit:
    shl si, 1
    rcl bp, 1
    rcl cx, 1
    mov [%1], bp
    mov [%1+2], cx
%endmacro

%macro XS_FX15 2
%1:
    XS_CNT %2
    or cx, cx
    mov cx, 0x3FFF                  ; << 1 makes 7FFF:FFFF
    mov bp, 0xFFFF
    mov si, 0x8000
    jns %%p
    mov cx, 0xC000                  ; << 1 makes 8000:0000
    xor bp, bp
    xor si, si
%%p:
    ret
%endmacro

; XS_ROT dst, a, |c1|, c1 < 0, b, |c2|, c2 < 0 - x3sim imul2_32, sh 15
%macro XS_ROT 7
    XS_P 1, %4, %2, %3
    XS_P 0, %7, %5, %6
    XS_R15 %1, XS_ST_DCT
%endmacro

; XS_MULC dst, a, c - x3sim imul32(a, c, 15)
%macro XS_MULC 3
    XS_P 1, 0, %2, %3
    XS_R15 %1, XS_ST_DCT
%endmacro

; XS_TOV disp, x - V = sat16((x + 2^13) >> 14) into ring column at DI + disp
; (and its copy 16 slots on: the doubled ring)
%macro XS_TOV 2
    mov ax, [%2]
    mov dx, [%2+2]
    add ax, 0x2000
    adc dx, 0
    jno %%n
    call xs_fxv_hi                  ; past int32: far above
%%n:
    mov cx, dx                      ; it fits when DX is in [-2000h, 1FFFh]
    add cx, 0x2000
    test cx, 0xC000
    jz %%ok
    call xs_fxv                     ; (both load the clamp pre-shifted)
%%ok:
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    xor dx, 0x8000                  ; the ring holds V + 32768 (XS_WTP)
    mov [di+%1], dx
    mov [di+%1+32], dx
%endmacro

; the window: SI:BP = the accumulator
%macro XS_W0 0
    xor si, si
    xor bp, bp
%endmacro

; XS_WTP / XS_WTM disp, |D| - one product of the window: the ring word is
; V + 32768 (unsigned), so a MUL (about 15 cycles under an IMUL on the 8088)
; by |D|, added for a positive D and subtracted for a negative one; XS_WOUT's
; correction takes the 32768 sum(D) back out (tools/x3stdgen.py)
%macro XS_WTP 2
    mov ax, %2
    mul word [di+%1]
    add si, ax
    adc bp, dx
%endmacro
%macro XS_WTM 2
    mov ax, %2
    mul word [di+%1]
    sub si, ax
    sbb bp, dx
%endmacro

; XS_WOUT disp, lo, hi - (acc + 2^12) >> 13, clipped and counted, to xs_pcmt
; + disp; lo:hi is 2^12 less the bias's 32768 sum(D), mod 2^32 (xs_fxw loads
; the clip pre-shifted)
%macro XS_WOUT 3
    add si, %2
    adc bp, %3
    mov ax, bp                      ; it fits when BP is in [-1000h, 0FFFh]
    add ax, 0x1000
    test ax, 0xE000
    jz %%ok
    call xs_fxw
%%ok:
    shl si, 1
    rcl bp, 1
    shl si, 1
    rcl bp, 1
    shl si, 1
    rcl bp, 1
    mov [xs_pcmt+%1], bp
%endmacro

; --- the 16-bit arithmetic of the IMDCT's inside (x3sim iadd, isub, ineg) --
%macro XS_IADD 2
    add %1, %2
    jno %%ok
    mov %1, 0x7FFF
    js %%c
    inc %1
%%c:
    XS_CNT XS_ST_IMDCT
%%ok:
%endmacro
%macro XS_ISUB 2
    sub %1, %2
    jno %%ok
    mov %1, 0x7FFF
    js %%c
    inc %1
%%c:
    XS_CNT XS_ST_IMDCT
%%ok:
%endmacro
%macro XS_INEG 1
    neg %1
    jno %%ok
    mov %1, 0x7FFF                  ; only -(-32768)
    XS_CNT XS_ST_IMDCT
%%ok:
%endmacro
; XS_IHALF r - (r + 1) >> 1, which never saturates
%macro XS_IHALF 1
    sar %1, 1
    adc %1, 0
%endmacro
; XS_IMUL k - AX = round(AX k / 2^15) for a DCT9 constant (never saturates:
; |k| < 2^15). clobbers DX
%macro XS_IMUL 1
    imul word [%1]
    add ax, 0x4000
    adc dx, 0
    shl ax, 1
    rcl dx, 1
    mov ax, dx
%endmacro

; XS_PAIRL i - xs_mpf's arithmetic for output pair i of a long block, with
; every address a constant off BX (the overlap), DI (the window's groups) and
; [xs_bx] (the outputs); a pair whose |o| >= 2^30 takes xs_mpair
%macro XS_PAIRL 1
    mov ax, [bx + 4*%1 + 2]         ; |o| < 2^30?
    add ax, 0x4000
    jns %%f
    mov ax, [xs_co + 2*%1]          ; no: the general pair
    mov [xs_mco], ax
    mov ax, [xs_si + 2*%1]
    mov [xs_msi], ax
    mov ax, [xs_bx]
    add ax, 4*%1
    mov [xs_mlo], ax
    mov ax, [xs_bx]
    add ax, 68 - 4*%1
    mov [xs_mhi], ax
    push bx
    push di
    add bx, 4*%1
    add di, 16*%1
    call xs_mpair
    pop di
    pop bx
    jmp %%done
%%f:
    mov ax, [xs_co + 2*%1]          ; t1 = (co a + si b) >> k
    imul word [di + 16*%1 + 4]
    mov cx, ax
    mov si, dx
    mov ax, [xs_si + 2*%1]
    imul word [di + 16*%1 + 6]
    add ax, cx
    adc dx, si
    call [xs_srkp]
    mov [xs_mt1], ax
    mov [xs_mt1+2], dx
    mov ax, [xs_co + 2*%1]          ; t2 = (co c + si d) >> k
    imul word [di + 16*%1 + 8]
    mov cx, ax
    mov si, dx
    mov ax, [xs_si + 2*%1]
    imul word [di + 16*%1 + 10]
    add ax, cx
    adc dx, si
    call [xs_srkp]
    mov [xs_mt2], ax
    mov [xs_mt2+2], dx
    mov ax, [bx + 4*%1 + 2]         ; U(w) = 4 o_hi w + ((o_lo w) >> 14)
    imul word [di + 16*%1]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov cx, ax
    mov si, dx
    mov ax, [bx + 4*%1]
    mul word [di + 16*%1]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
    mov dx, 0
    adc dx, 0
    add ax, cx
    adc dx, si
    sub ax, [xs_mt1]                ; lo = (sat32(U - t1) + 1) >> 1
    sbb dx, [xs_mt1+2]
    XS_SAT32 XS_ST_IMDCT
    sar dx, 1
    rcr ax, 1
    adc ax, 0
    adc dx, 0
    mov si, [xs_bx]
    mov [si + 4*%1], ax
    mov [si + 4*%1 + 2], dx
    mov ax, [bx + 4*%1 + 2]         ; U(w9)
    imul word [di + 16*%1 + 2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov cx, ax
    mov si, dx
    mov ax, [bx + 4*%1]
    mul word [di + 16*%1 + 2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, dx
    mov dx, 0
    adc dx, 0
    add ax, cx
    adc dx, si
    add ax, [xs_mt2]                ; hi = (sat32(U + t2) + 1) >> 1
    adc dx, [xs_mt2+2]
    XS_SAT32 XS_ST_IMDCT
    sar dx, 1
    rcr ax, 1
    adc ax, 0
    adc dx, 0
    mov si, [xs_bx]
    mov [si + 68 - 4*%1], ax
    mov [si + 68 - 4*%1 + 2], dx
    mov ax, [xs_co + 2*%1]          ; o' = ((co tw0 - si tw1) >> k + 1) >> 1
    imul word [di + 16*%1 + 12]
    mov cx, ax
    mov si, dx
    mov ax, [xs_si + 2*%1]
    imul word [di + 16*%1 + 14]
    sub cx, ax
    sbb si, dx
    mov ax, cx
    mov dx, si
    call [xs_srkp]
    add ax, 1
    adc dx, 0
    sar dx, 1
    rcr ax, 1
    mov [bx + 4*%1], ax
    mov [bx + 4*%1 + 2], dx
%%done:
%endmacro

; the out-of-line clamps
    XS_FXS xs_fxs_dct, XS_ST_DCT
    XS_FXS xs_fxs_ster, XS_ST_STER
    XS_FXS xs_fxs_imdct, XS_ST_IMDCT
    XS_FX15 xs_fx15_dct, XS_ST_DCT
    XS_FX15 xs_fx15_alias, XS_ST_ALIAS
xs_fxv_hi:                          ; TOV: x + 2^13 past int32: 7FFFh
    XS_CNT XS_ST_DCT
    mov dx, 0x1FFF                  ; (<< 2 makes 7FFF; and passes the range
    mov ax, 0xC000                  ;  test after it, so it is counted once)
    ret
xs_fxv:                             ; TOV: out of int16, by the sign
    XS_CNT XS_ST_DCT
    or dx, dx
    mov dx, 0x1FFF
    mov ax, 0xC000
    jns .p
    mov dx, 0xE000                  ; (<< 2 makes 8000h)
    xor ax, ax
.p:
    ret
xs_fxw:                             ; WOUT: a clipped sample, counted
    inc word [xs_gclip]
    or bp, bp
    mov bp, 0x0FFF                  ; (<< 3 makes 7FFFh)
    mov si, 0xE000
    jns .p
    mov bp, 0xF000                  ; (<< 3 makes 8000h)
    xor si, si
.p:
    ret

; the generated bodies, one instance each
xs_dct32:
    X3SG_DCT32
    ret
xs_dct16:
    X3SG_DCT16
    ret
xs_dct8:
    X3SG_DCT8
    ret
xs_win1:
    X3SG_WIN1
    ret
xs_win2:
    X3SG_WIN2
    ret
xs_win4:
    X3SG_WIN4
    ret

; =============================================================================
; THE VERBS
; =============================================================================

; -----------------------------------------------------------------------------
; CAPS - AL = profile, AH = divisor, CL = output channels, ES:DI -> X3C
; -----------------------------------------------------------------------------
xs_caps:
    push bx
    push cx
    push di
    or al, al
    jnz .prof
    cmp ah, 1
    je .d
    cmp ah, 2
    je .d
    cmp ah, 4
    jne .div
.d:
    cmp cl, 1
    jb .chan
    cmp cl, 2
    ja .chan
    push di
    mov cx, X3C_SIZE
    xor al, al
    cld
    rep stosb
    pop di
    mov byte [es:di+X3C_OFFER], 1
    mov word [es:di+X3C_PARTKB], (xs_end - $$ + 1023) / 1024
    clc
    jmp short .out
.prof:
    mov ax, X3E_PROFILE
    stc
    jmp short .out
.div:
    mov ax, X3E_DIV
    stc
    jmp short .out
.chan:
    mov ax, X3E_CHAN
    stc
.out:
    pop di
    pop cx
    pop bx
    retf

; -----------------------------------------------------------------------------
; OPEN - ES:DI -> X3O
; -----------------------------------------------------------------------------
xs_open:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld
    mov byte [xs_isopen], 0
    cmp byte [es:di+X3O_PROF], 0
    jne .prof
    mov al, [es:di+X3O_DIV]
    mov bl, 32
    cmp al, 1
    je .d
    mov bl, 16
    cmp al, 2
    je .d
    mov bl, 8
    cmp al, 4
    jne .div
.d:
    mov [xs_div], al
    mov [xs_nsb], bl
    mov word [xs_dctp], xs_dct32    ; the synthesis bodies of this divisor
    mov word [xs_winp], xs_win1
    cmp al, 2
    jb .dp
    mov word [xs_dctp], xs_dct16
    mov word [xs_winp], xs_win2
    je .dp
    mov word [xs_dctp], xs_dct8
    mov word [xs_winp], xs_win4
.dp:
    mov al, [es:di+X3O_SCH]
    cmp al, 1
    jb .chan
    cmp al, 2
    ja .chan
    mov [xs_sch], al
    mov al, [es:di+X3O_OCH]
    cmp al, 1
    jb .chan
    cmp al, [xs_sch]
    ja .chan
    mov [xs_och], al
    mov al, [es:di+X3O_FMT]
    cmp al, X3_FMT_U8
    ja .fmt
    mov [xs_fmt], al
    mov al, [es:di+X3O_VER]
    cmp al, 1
    je .v
    cmp al, 2
    je .v
    cmp al, 25
    jne .rec
.v:
    mov [xs_ver], al
    mov al, [es:di+X3O_SRI]
    cmp al, 2
    ja .rec
    mov [xs_sri], al
    mov ax, [es:di+X3O_GAIN]
    cmp ax, 256
    jne .g
    xor ax, ax                      ; 256 is unity too: the exact path
.g:
    mov [xs_gain], ax
    cmp byte [xs_crcok], 0          ; the tables, once
    jne .t
    call xs_tcheck
    jc .tab
    mov byte [xs_crcok], 1
.t:
    call xs_imcbuild
    call xs_clear
    xor ax, ax                      ; the counters
    mov di, xs_sat
    push ds
    pop es
    mov cx, (xs_cnt_end - xs_sat) / 2
    rep stosw
    mov byte [xs_isopen], 1
    clc
    jmp short .out
.prof:
    mov ax, X3E_PROFILE
    jmp short .err
.div:
    mov ax, X3E_DIV
    jmp short .err
.chan:
    mov ax, X3E_CHAN
    jmp short .err
.fmt:
    mov ax, X3E_FMT
    jmp short .err
.rec:
    mov ax, X3E_RECORD
    jmp short .err
.tab:
    mov ax, X3E_TABLE
.err:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; -----------------------------------------------------------------------------
; RESET / CLOSE / STATS
; -----------------------------------------------------------------------------
xs_reset:
    push cx
    push di
    push es
    call xs_clear
    pop es
    pop di
    pop cx
    clc
    retf

xs_close:
    mov byte [xs_isopen], 0
    clc
    retf

xs_stats:                           ; ES:DI -> X3Z
    push bx
    push cx
    push si
    push di
    mov ax, [xs_ngran]
    mov [es:di+X3Z_GRAN], ax
    mov ax, [xs_ngran+2]
    mov [es:di+X3Z_GRAN+2], ax
    mov ax, [xs_nclip]
    mov [es:di+X3Z_CLIP], ax
    mov ax, [xs_nclip+2]
    mov [es:di+X3Z_CLIP+2], ax
    mov ax, [xs_nsil]
    mov [es:di+X3Z_SILENT], ax
    mov ax, [xs_nsil+2]
    mov [es:di+X3Z_SILENT+2], ax
    xor bx, bx                      ; the total, and each stage saturating
    xor cx, cx                      ; at FFFFh
    mov si, xs_sat
.s:
    mov ax, [si]
    add bx, ax
    adc cx, [si+2]
    cmp word [si+2], 0
    je .w
    mov ax, 0xFFFF
.w:
    mov [es:di+X3Z_STAGE], ax
    add si, 4
    add di, 2
    cmp si, xs_sat + 32
    jb .s
    pop di
    mov [es:di+X3Z_SAT], bx
    mov [es:di+X3Z_SAT+2], cx
    pop si
    pop cx
    pop bx
    clc
    retf

; xs_clear - every history: overlap, rings, positions, block types
; clobbers: AX, CX, DI, ES
xs_clear:
    push ds
    pop es
    xor ax, ax
    mov di, xs_ovl
    mov cx, (xs_hist_end - xs_ovl) / 2
    cld
    rep stosw
    mov ax, 0x8000                  ; the rings hold V + 32768: V = 0
    mov di, xs_ring
    mov cx, 2 * 32 * 32
    rep stosw
    ret

; xs_tcheck - every table of the slice against its CRC-32 (x3std.inc)
; out: CF = 1 on a mismatch. clobbers: AX, BX, CX, DX, SI, DI
xs_tcheck:
    mov bx, xs_tcrc
.t:
    mov si, [bx]
    or si, si
    jz .ok
    mov cx, [bx+2]
    mov ax, 0xFFFF
    mov dx, ax
    push bx
    call x3g_crc32
    pop bx
    not ax
    not dx
    cmp ax, [bx+4]
    jne .bad
    cmp dx, [bx+6]
    jne .bad
    add bx, 8
    jmp short .t
.ok:
    clc
    ret
.bad:
    stc
    ret

%macro XS_TCRC 1
    dw XS_T(%1), X3T_ %+ %1 %+ _LEN, X3T_ %+ %1 %+ _CRC & 0xFFFF, X3T_ %+ %1 %+ _CRC >> 16
%endmacro
xs_tcrc:
    XS_TCRC P43M
    XS_TCRC P43E
    XS_TCRC P43F
    XS_TCRC IS
    XS_TCRC AA
    XS_TCRC DCT9
    XS_TCRC IMW
    XS_TCRC ROT
    XS_TCRC SYNW
    XS_TCRC SYNIDX
    dw 0

; xs_imcbuild - the IMDCT's merged twiddle-window words regrouped by output
; index (x3gran-independent; the shipped IMW is checked): for the normal and
; stop long windows nine groups, for the short one three, each
; {w, w9, a, b, c, d, tw0, tw1}. clobbers: AX, BX, CX, SI, DI
XS_IMW      equ XS_T(IMW)
xs_imcbuild:
    mov di, xs_imc
    xor bx, bx                      ; window 0, 1: 9 entries of stride 18
.w:
    xor si, si
.i:
    push si
    shl si, 1
    add si, bx                      ; + 108 per window
    mov cx, 6
.k:
    mov ax, [XS_IMW+si]
    mov [di], ax
    add di, 2
    add si, 18
    loop .k
    pop si
    push si
    shl si, 1
    mov ax, [XS_IMW+216+si]         ; tw0, tw1
    mov [di], ax
    mov ax, [XS_IMW+234+si]
    mov [di+2], ax
    add di, 4
    pop si
    inc si
    cmp si, 9
    jb .i
    add bx, 108
    cmp bx, 216
    jb .w
    xor si, si                      ; the short window: 3 of stride 6
.s:
    push si
    shl si, 1
    mov cx, 6
.sk:
    mov ax, [XS_IMW+252+si]
    mov [di], ax
    add di, 2
    add si, 6
    loop .sk
    pop si
    push si
    shl si, 1
    mov ax, [XS_IMW+288+si]
    mov [di], ax
    mov ax, [XS_IMW+294+si]
    mov [di+2], ax
    add di, 4
    pop si
    inc si
    cmp si, 3
    jb .s
    ret

; =============================================================================
; GRANULE - ES:SI -> the record, DX:DI -> the PCM
; =============================================================================
xs_granule:
    push bx
    push dx
    push si
    push di
    push bp
    push es
    cld
    cmp byte [xs_isopen], 0
    jne .open
    mov ax, X3E_NOTOPEN
    jmp .err
.open:
    mov al, [es:si+X3G_VER]         ; this OPEN's version and rate only
    cmp al, [xs_ver]
    jne .rec
    mov al, [es:si+X3G_SRI]
    cmp al, [xs_sri]
    jne .rec
    mov al, [es:si+X3G_NCH]
    cmp al, 1
    jb .rec
    cmp al, 2
    ja .rec
    mov [xs_fnch], al
    mov [xs_rseg], es
    mov [xs_roff], si
    mov [xs_oseg], dx
    mov [xs_ooff], di
    mov word [xs_gclip], 0
    push ds
    pop es
    ; --- requantise each channel of the frame (bitstream order) ---
    mov byte [xs_ch], 0
.rq:
    call xs_requant
    inc byte [xs_ch]
    mov al, [xs_ch]
    cmp al, [xs_fnch]
    jb .rq
    ; --- stereo ---
    cmp byte [xs_fnch], 2
    jne .nost
    call xs_stereo
.nost:
%ifdef XS_DUMPRQ
    jmp .dump                       ; (DEBUG: the requantised lines)
%endif
    ; --- the hybrid filterbank, each channel of the frame ---
    mov byte [xs_ch], 0
.hy:
    call xs_hybrid
    inc byte [xs_ch]
    mov al, [xs_ch]
    cmp al, [xs_fnch]
    jb .hy
    ; --- the synthesis ---
%ifdef XS_DUMP
.dump:
    ; A DEBUG BUILD ONLY (never shipped): channel 0's subband samples, int32,
    ; as the "PCM" - 2,304 bytes, which the bench reads as 1,152 mono frames
    push es
    mov es, [xs_oseg]
    mov di, [xs_ooff]
    mov si, xs_xr
    mov cx, 576 * 2
    rep movsw
    mov si, xs_ovl                  ; ...then channel 0's overlap
    mov cx, 32 * 9 * 2
    rep movsw
    pop es
    mov word [xs_gclip], 0
    add word [xs_ngran], 1
    adc word [xs_ngran+2], 0
    mov cx, 1152 + 576
    xor ax, ax
    clc
    jmp .out
%endif
    call xs_synth
.m_end:                             ; (a mark for tests/x3bench/x3marty.py)
    ; --- the answer ---
    add word [xs_ngran], 1
    adc word [xs_ngran+2], 0
    mov ax, [xs_gclip]
    add [xs_nclip], ax
    adc word [xs_nclip+2], 0
    mov al, 18                      ; 576 / div = 18 nsb frames
    mul byte [xs_nsb]
    mov cx, ax
    mov ax, [xs_gclip]
    clc
    jmp short .out
.rec:
    mov ax, X3E_RECORD
.err:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    retf

; xs_chrec - SI -> channel [xs_ch]'s part of the record (in [xs_rseg])
xs_chrec:
    mov si, [xs_roff]
    add si, X3G_CH0
    cmp byte [xs_ch], 0
    je .o
    add si, X3G_CSZ
.o:
    ret

; xs_chxr - DI -> channel [xs_ch]'s 576 lines
xs_chxr:
    mov di, xs_xr
    cmp byte [xs_ch], 0
    je .o
    add di, 576 * 4
.o:
    ret

; =============================================================================
; REQUANTISATION (x3sim.requant_i): |ix|^(4/3) 2^(q/4), int32 Q28, into
; xr[ch] in bitstream order
; =============================================================================
xs_requant:
    call xs_chxr                    ; zero the channel's lines
    push di
    mov cx, 576 * 2
    xor ax, ax
    rep stosw
    pop di
    mov es, [xs_rseg]
    call xs_chrec
    cmp byte [es:si+X3G_C_OK], 0
    jne .ok
    add word [xs_nsil], 1           ; silence: zeros through the filterbank
    adc word [xs_nsil+2], 0
    jmp short .out
.ok:
    mov cx, [es:si+X3G_C_NZ]
    cmp cx, 576
    jbe .n
    mov cx, 576
.n:
    jcxz .out
    add si, X3G_C_IX
.l:
    mov ax, [es:si]
    or ax, ax
    jz .z
    mov bx, [es:si+1152]            ; its q (X3G_C_Q - X3G_C_IX)
    push cx
    push si
    call xs_rq1
    pop si
    pop cx
    mov [di], ax
    mov [di+2], dx
.z:
    add si, 2
    add di, 4
    loop .l
.out:
    push ds
    pop es
    ret

; xs_rq1 - AX = ix (not 0), BX = q -> DX:AX = the line. clobbers: BX, CX, SI
xs_rq1:
    mov [xs_rsign], ah
    or ax, ax
    jns .p
    neg ax
.p:
    cmp ax, X3_P43_N
    jae .big
    mov si, ax                      ; a table read: M, E
    mov cl, [XS_T(P43E)+si]
    shl si, 1
    mov ax, [XS_T(P43M)+si]
    jmp short .m
.big:
    ; interpolated between j = a >> 3 and j + 1, in j's octave (x3sim.pow43_i)
    mov dx, ax
    and dx, 7
    mov [xs_rf], dx                 ; f
    mov cl, 3
    shr ax, cl
    mov si, ax                      ; j
    mov ch, [XS_T(P43E)+si]         ; E[j]
    mov cl, [XS_T(P43E)+si+1]
    sub cl, ch                      ; E[j+1] - E[j]: 0 or 1
    shl si, 1
    mov dx, [XS_T(P43M)+si+2]
    shl dx, cl                      ; mj1, in j's octave
    mov ax, [XS_T(P43M)+si]         ; mj
    push ax
    sub dx, ax                      ; mj1 - mj (>= 0)
    mov ax, dx
    mul word [xs_rf]                ; x f
    add ax, 4
    adc dx, 0
    shr dx, 1                       ; >> 3
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    shr dx, 1
    rcr ax, 1
    pop dx
    add ax, dx                      ; mi (< 65536)
    mov cl, ch
    add cl, 4                       ; ei = E[j] + 4
    test ax, 0x8000
    jz .m
    add ax, 1                       ; mi >= 32768: (mi + 1) >> 1, E + 1
    rcr ax, 1
    inc cl
.m:
    ; AX = M, CL = E. p = M F[q & 3]; s = -E - (q >> 2)
    mov si, bx
    and si, 3
    shl si, 1
    mul word [XS_T(P43F)+si]        ; DX:AX = p (< 2^30)
    sar bx, 1
    sar bx, 1                       ; q >> 2
    mov ch, 0
    add bx, cx                      ; E + (q >> 2)
    neg bx                          ; s
    cmp bx, 31
    jg .zero
    cmp bx, 1
    jl .left
    mov cx, bx                      ; 1 <= s <= 31: ((p >> (s - 1)) + 1) >> 1
    dec cx
    call xs_shr32
    add ax, 1
    adc dx, 0
    shr dx, 1
    rcr ax, 1
    jmp short .sign
.left:
    cmp bx, -8                      ; s <= 0: p << -s, past int32 saturating
    jl .sat
    mov cx, bx
    neg cx
    jcxz .sign
.ls:
    shl ax, 1
    rcl dx, 1                       ; (a rotate sets no SF: test bit 31)
    test dh, 0x80
    jnz .sat
    loop .ls
    jmp short .sign
.sat:
    XS_CNT XS_ST_REQ
    mov ax, 0xFFFF
    mov dx, 0x7FFF
.sign:
    test byte [xs_rsign], 0x80
    jz .out
    neg dx                          ; -DX:AX
    neg ax
    sbb dx, 0
.out:
    ret
.zero:
    xor ax, ax
    xor dx, dx
    ret

; xs_shr32 - DX:AX >>= CX (0..31), logical. clobbers: CX, SI
xs_shr32:
    jcxz .out
    cmp cx, 16
    jb .lt
    mov ax, dx
    xor dx, dx
    sub cl, 16
    shr ax, cl
    ret
.lt:
    mov si, dx
    shr dx, cl
    shr ax, cl
    neg cl
    add cl, 16
    shl si, cl
    or ax, si
.out:
    ret

; xs_sar32 - DX:AX >>= CL (0..31), arithmetic. clobbers: CX, SI
xs_sar32:
    xor ch, ch
    jcxz .out
    cmp cl, 16
    jb .lt
    mov ax, dx
    cwd
    sub cl, 16
    sar ax, cl
    ret
.lt:
    mov si, dx
    sar dx, cl
    shr ax, cl
    neg cl
    add cl, 16
    shl si, cl
    or ax, si
.out:
    ret

; =============================================================================
; STEREO (x3sim.stereo_i), bitstream order, on xr[0] and xr[1]
; =============================================================================
xs_stereo:
    mov es, [xs_rseg]
    mov si, [xs_roff]
    mov al, [es:si+X3G_PLAN]
    cmp al, X3G_PLAN_LR
    jne .go
    jmp .out
.go:
    mov [xs_plan], al
    mov cx, [es:si+X3G_CH0+X3G_C_NZ] ; the lines: up to the larger NZ
    mov ax, [es:si+X3G_CH1+X3G_C_NZ]
    cmp ax, cx
    jbe .n
    mov cx, ax
.n:
    cmp cx, 576
    jbe .n2
    mov cx, 576
.n2:
    or cx, cx
    jnz .n3
    jmp .out
.n3:
    mov [xs_scnt], cx
    ; the intensity factor tables of this frame (x3tables' IS: MPEG-1
    ; (kl, kr) x 7 at 0, x sqrt 2 at 28; MPEG-2 k x 16 by scale at 56 and 88,
    ; x sqrt 2 at 120 and 152; sqrt 2 at 184)
    mov bx, XS_T(IS)
    mov ax, 16384
    cmp byte [es:si+X3G_VER], 1
    jne .m2
    cmp byte [es:si+X3G_MS], 0
    je .tb
    add bx, 28
    jmp short .tb
.m2:
    add bx, 56
    cmp byte [es:si+X3G_ISSC], 0
    je .sc
    add bx, 32
.sc:
    cmp byte [es:si+X3G_MS], 0
    je .tb
    add bx, 64
    mov ax, [XS_T(IS) + 184]
.tb:
    mov [xs_istab], bx
    mov [xs_isone], ax              ; the other factor of an MPEG-2 pair
    add si, X3G_PL
    mov [xs_ppl], si
    mov word [xs_sl], xs_xr
.line:
    mov al, X3G_PL_MS               ; all M/S, or the line's plan
    cmp byte [xs_plan], X3G_PLAN_MS
    je .do
    mov es, [xs_rseg]
    mov si, [xs_ppl]
    mov al, [es:si]
    push ds
    pop es
.do:
    mov bx, [xs_sl]
    cmp al, X3G_PL_LR
    je .next
    cmp al, X3G_PL_MS
    jne .is
    ; M/S: L' = L + R, R' = L - R, saturating
    mov ax, [bx]
    mov dx, [bx+2]
    add ax, [bx+2304]
    adc dx, [bx+2306]
    XS_SAT32 XS_ST_STER
    mov cx, ax
    mov bp, dx
    mov ax, [bx]
    mov dx, [bx+2]
    sub ax, [bx+2304]
    sbb dx, [bx+2306]
    XS_SAT32 XS_ST_STER
    mov [bx+2304], ax
    mov [bx+2306], dx
    mov [bx], cx
    mov [bx+2], bp
    jmp short .next
.is:
    ; intensity, position p: L' = L kl, R' = L kr (Q14)
    and al, 0x7F
    mov dl, al                      ; DL = p
    xor ah, ah
    mov si, ax
    shl si, 1
    cmp byte [xs_ver], 1
    jne .is2
    shl si, 1                       ; MPEG-1: the pair (kl, kr)
    add si, [xs_istab]
    mov ax, [si]
    mov [xs_kl], ax
    mov ax, [si+2]
    mov [xs_kr], ax
    jmp short .ism
.is2:
    add si, [xs_istab]              ; MPEG-2: (k, one) for odd p, else
    mov ax, [si]                    ; (one, k)
    mov cx, [xs_isone]
    test dl, 1
    jnz .odd
    xchg ax, cx
.odd:
    mov [xs_kl], ax
    mov [xs_kr], cx
.ism:
    XS_P 1, 0, bx, xs_kr            ; R' first, while L is still L
    call xs_r14
    mov [bx+2304], ax
    mov [bx+2306], dx
    XS_P 1, 0, bx, xs_kl
    call xs_r14
    mov [bx], ax
    mov [bx+2], dx
.next:
    add word [xs_sl], 4
    inc word [xs_ppl]
    dec word [xs_scnt]
    jz .out
    jmp .line
.out:
    push ds
    pop es
    ret

; xs_r14 - DX:AX = sat32((CX:BP:SI + 2^13) >> 14), counted in stereo
; clobbers: CX, BP, SI
xs_r14:
    add si, 0x2000
    adc bp, 0
    adc cx, 0
    mov ax, cx                      ; it fits when bits 47..45 agree
    add ax, 0x2000
    test ax, 0xC000
    jnz .sat
    shl si, 1
    rcl bp, 1
    rcl cx, 1
    shl si, 1
    rcl bp, 1
    rcl cx, 1
    mov ax, bp
    mov dx, cx
    ret
.sat:
    XS_CNT XS_ST_STER
    mov ax, 0xFFFF
    mov dx, 0x7FFF
    or cx, cx
    jns .o
    inc ax
    inc dx
.o:
    ret

; =============================================================================
; THE HYBRID FILTERBANK of channel [xs_ch] (x3sim Decoder.hybrid): reorder,
; the cut at 18 nsb, antialias, the band scale and IMDCT of each kept
; subband with its overlap, frequency inversion. xr[ch] ends as 18 slots of
; each subband: subband b, slot t at 18 b + t
; =============================================================================
xs_hybrid:
    mov es, [xs_rseg]
    call xs_chrec
    mov bl, [xs_ch]
    xor bh, bh
    cmp byte [es:si+X3G_C_OK], 0
    je .sil
    mov al, [es:si+X3G_C_BT]
    mov [xs_bt], al
    mov al, [es:si+X3G_C_MIX]
    mov [xs_mix], al
    mov al, [es:si+X3G_C_WS]
    mov [xs_ws], al
    mov byte [xs_real], 1
    jmp short .blk
.sil:
    mov al, 0                       ; a silent granule (x3sim _silent_gi): a
    mov ah, [xs_prevbt+bx]          ; stop block after a start or short one
    cmp ah, 1
    je .stop
    cmp ah, 2
    jne .nrm
.stop:
    mov al, 3
.nrm:
    mov [xs_bt], al
    mov byte [xs_mix], 0
    mov ah, 0
    or al, al
    jz .ws
    inc ah
.ws:
    mov [xs_ws], ah
    mov byte [xs_real], 0
.blk:
    mov al, [xs_bt]
    mov [xs_prevbt+bx], al
    push ds
    pop es
    cmp byte [xs_bt], 2             ; a real short block: reorder
    jne .nre
    cmp byte [xs_real], 0
    je .nre
    call xs_reorder
.nre:
    call xs_chxr                    ; the cut: from 18 nsb on, zero
    mov al, 18 * 4
    mul byte [xs_nsb]
    add di, ax
    mov cx, 576 * 4
    sub cx, ax
    shr cx, 1
    xor ax, ax
    rep stosw
    mov al, 31                      ; antialias: 31 boundaries, 1 for a mixed
    cmp byte [xs_bt], 2             ; short block, none for a short one
    jne .aa
    mov al, 1
    cmp byte [xs_mix], 0
    jne .aa
    xor al, al
.aa:
    mov ah, [xs_nsb]
    dec ah
    cmp al, ah
    jbe .aa2
    mov al, ah
.aa2:
    or al, al
    jz .noaa
    call xs_alias
.noaa:
    mov byte [xs_b], 0              ; each kept subband
.sb:
    ; a subband whose 18 lines AND 9 overlap words are all zero comes out
    ; zero with a zero overlap - x3sim's arithmetic on it is exactly that -
    ; so it is skipped (the top of every band-limited stream)
    call xs_chxr
    mov al, 18 * 4
    mul byte [xs_b]
    add di, ax
    mov cx, 36
    xor ax, ax
    repe scasw
    jne .work
    mov di, xs_ovl                  ; its overlap: [ch][b][9]
    cmp byte [xs_ch], 0
    je .o0
    add di, 32 * 9 * 4
.o0:
    mov al, 36
    mul byte [xs_b]
    add di, ax
    xor ax, ax
    mov cx, 18
    repe scasw
    je .skip
.work:
    call xs_scale
    call xs_imdct
.skip:
    inc byte [xs_b]
    mov al, [xs_b]
    cmp al, [xs_nsb]
    jb .sb
.m_fi:                              ; (a mark for tests/x3bench/x3marty.py)
    call xs_chxr                    ; frequency inversion: odd subbands, odd
    add di, 18 * 4 + 4              ; slots, negated (saturating)
    mov dl, [xs_nsb]
    shr dl, 1
.fi:
    mov cx, 9
    push di
.fs:
    xor ax, ax
    xor bx, bx
    sub ax, [di]
    sbb bx, [di+2]
    jno .fo
    mov ax, 0xFFFF                  ; -(-2^31)
    mov bx, 0x7FFF
    XS_CNT XS_ST_IMDCT
.fo:
    mov [di], ax
    mov [di+2], bx
    add di, 8
    loop .fs
    pop di
    add di, 2 * 18 * 4
    dec dl
    jnz .fi
    ret

; xs_reorder - xr[ch] from bitstream into subband order (x3sim reorder_index)
; through xs_tmp: in short band s (from 3 when mixed, its lines from 36), the
; line base + w width + i goes to base + 3 i + w
xs_reorder:
    call xs_chxr
    mov [xs_rdst], di
    mov si, di
    mov di, xs_tmp
    mov cx, 576 * 2
    rep movsw
    xor dl, dl                      ; DL = the band
    mov word [xs_rbase], 0
    cmp byte [xs_mix], 0
    je .s
    mov dl, 3
    mov word [xs_rbase], 36
.s:
    mov es, [xs_rseg]
    mov bl, dl
    xor bh, bh
    add bx, [xs_roff]
    mov al, [es:bx+X3G_SFBS]        ; its width
    push ds
    pop es
    xor ah, ah
    mov [xs_rw], ax
    mov si, [xs_rbase]
    shl si, 1
    shl si, 1
    mov di, si
    add si, xs_tmp
    add di, [xs_rdst]
    mov bx, ax
    shl bx, 1
    shl bx, 1                       ; the width in bytes of int32
    mov cx, ax
.i:
    mov ax, [si]                    ; w = 0
    mov [di], ax
    mov ax, [si+2]
    mov [di+2], ax
    mov ax, [si+bx]                 ; w = 1
    mov [di+4], ax
    mov ax, [si+bx+2]
    mov [di+6], ax
    shl bx, 1
    mov ax, [si+bx]                 ; w = 2
    mov [di+8], ax
    mov ax, [si+bx+2]
    mov [di+10], ax
    shr bx, 1
    add si, 4
    add di, 12
    loop .i
    mov ax, [xs_rw]                 ; base += 3 width
    add [xs_rbase], ax
    add [xs_rbase], ax
    add [xs_rbase], ax
    inc dl
    cmp dl, 13
    jb .s
    ret

; xs_alias - AL boundaries of xr[ch] (x3sim.alias_i): u' = u cs - d ca,
; d' = d cs + u ca across subbands b-1 | b, eight a boundary, Q15
xs_alias:
    mov [xs_acnt], al
    call xs_chxr
    add di, 18 * 4                  ; boundary 1: BX = &x[18], DI = &x[17]
    mov bx, di
    sub di, 4
.b:
    push di                         ; eight lines each side all zero: the
    sub di, 7 * 4                   ; butterflies give zeros (skipped)
    mov cx, 32
    xor ax, ax
    repe scasw
    pop di
    jne .bw
    jmp .bn
.bw:
%assign XS_I 0
%rep 8
    XS_P 1, 0, bx + 4 * XS_I, XS_T(AA) + 2 * XS_I
    XS_P 0, 1, di - 4 * XS_I, XS_T(AA) + 16 + 2 * XS_I
    XS_R15 xs_au, XS_ST_ALIAS
    XS_P 1, 0, di - 4 * XS_I, XS_T(AA) + 2 * XS_I
    XS_P 0, 0, bx + 4 * XS_I, XS_T(AA) + 16 + 2 * XS_I
    XS_R15 xs_ad, XS_ST_ALIAS
    mov ax, [xs_au]
    mov [bx + 4 * XS_I], ax
    mov ax, [xs_au+2]
    mov [bx + 4 * XS_I + 2], ax
    mov ax, [xs_ad]
    mov [di - 4 * XS_I], ax
    mov ax, [xs_ad+2]
    mov [di - 4 * XS_I + 2], ax
%assign XS_I XS_I + 1
%endrep
.bn:
    add bx, 18 * 4
    add di, 18 * 4
    dec byte [xs_acnt]
    jz .out
    jmp .b
.out:
    ret

; =============================================================================
; xs_scale - subband [xs_b] of xr[ch] into xs_x16 at its own Q(14 + k)
; (x3sim.band_scale): bl = the bit length of its largest |x|, k = 25 - bl
; (bl >= 30) or clip(25 - bl, 0, 14), at least -6; x16 = sat16(round(x >>
; (14 - k))). [xs_k] = k, [xs_kb] its byte; [xs_bx] -> the subband's lines
; =============================================================================
xs_scale:
    call xs_chxr
    mov al, 18 * 4
    mul byte [xs_b]
    add di, ax
    mov [xs_bx], di
    xor bx, bx                      ; BX:CX = the largest |x|, unsigned
    xor cx, cx
    mov si, di
    mov bp, 18
.m:
    mov ax, [si]
    mov dx, [si+2]
    or dx, dx
    jns .p
    neg dx
    neg ax
    sbb dx, 0
.p:
    cmp dx, bx
    jb .n
    ja .y
    cmp ax, cx
    jbe .n
.y:
    mov bx, dx
    mov cx, ax
.n:
    add si, 4
    dec bp
    jnz .m
    xor ax, ax                      ; AX = the bit length
    or bx, bx
    jz .lw
    mov al, 16
    mov cx, bx
.lw:
    jcxz .bl
.bit:
    inc ax
    shr cx, 1
    jnz .bit
.bl:
    mov dx, 25
    sub dx, ax
    cmp ax, 30
    jae .lo
    or dx, dx
    jns .c1
    xor dx, dx
.c1:
    cmp dx, 14
    jle .lo
    mov dx, 14
.lo:
    cmp dx, X3_K_MIN
    jge .k
    mov dx, X3_K_MIN
.k:
    mov [xs_k], dx
    mov [xs_kb], dl
    mov word [xs_mpp], xs_mpair     ; k < 0: the general pair (a left shift)
    or dx, dx
    js .kp
    mov word [xs_mpp], xs_mpf       ; k >= 0: the fast pair, and its shift
    mov bx, dx
    shl bx, 1
    mov ax, [xs_srkt+bx]
    mov [xs_srkp], ax
.kp:
    mov cx, 14
    sub cx, dx
    mov [xs_sh], cl                 ; sh = 14 - k (0..20)
    jcxz .s0
    mov bx, cx                      ; ((x >> (sh - 1)) + 1) >> 1: the
    dec bx                          ; shift by sh - 1, chosen once
    shl bx, 1
    mov ax, [xs_srkt+bx]
    mov [xs_scp], ax
.s0:
    mov si, [xs_bx]
    mov di, xs_x16
    mov bp, 18
.v:
    mov ax, [si]
    mov dx, [si+2]
    cmp byte [xs_sh], 0
    je .fit
    call [xs_scp]
    add ax, 1
    adc dx, 0
    sar dx, 1
    rcr ax, 1
.fit:
    mov cx, dx                      ; sat16: DX:AX fits when DX is AX's sign
    cwd
    cmp cx, dx
    je .st
    XS_CNT XS_ST_SCALE
    mov ax, 0x7FFF
    or cx, cx
    jns .st
    inc ax
.st:
    mov [di], ax
    add di, 2
    add si, 4
    dec bp
    jnz .v
    ret

; =============================================================================
; xs_imdct - subband [xs_b] of channel [xs_ch]: x16 -> its 18 outputs over
; the 18 lines at [xs_bx], with its overlap (x3sim imdct36_i / imdct_short_i)
; =============================================================================
XS_IMC_L    equ 9 * 16              ; a long window's nine groups
xs_imdct:
    mov bx, xs_ovl                  ; the overlap: [ch][b][9] int32
    cmp byte [xs_ch], 0
    je .c0
    add bx, 32 * 9 * 4
.c0:
    mov al, 36
    mul byte [xs_b]
    add bx, ax
    mov [xs_ovp], bx
    ; the kind: long normal, long stop, or short
    mov di, xs_imc                  ; normal
    cmp byte [xs_ws], 0
    je .knd
    cmp byte [xs_mix], 0
    je .k2
    cmp byte [xs_b], 2
    jb .long
.k2:
    cmp byte [xs_bt], 2
    jne .k3
    jmp xs_imdct12
.k3:
    cmp byte [xs_bt], 3
    jne .long
    add di, XS_IMC_L                ; stop
.knd:
.long:
    mov [xs_wg], di
    ; co, si from x16 (x3sim imdct36_i), 16-bit saturating
    mov ax, [xs_x16]
    XS_INEG ax
    mov [xs_co], ax                 ; co[0] = -x0
    mov ax, [xs_x16+34]
    mov [xs_si], ax                 ; si[0] = x17
%assign XS_I 0
%rep 4
    mov ax, [xs_x16 + 2 * (4 * XS_I + 1)]
    mov bx, ax
    XS_ISUB ax, [xs_x16 + 2 * (4 * XS_I + 2)]
    mov [xs_si + 2 * (8 - 2 * XS_I)], ax
    XS_IADD bx, [xs_x16 + 2 * (4 * XS_I + 2)]
    mov [xs_co + 2 * (1 + 2 * XS_I)], bx
    mov ax, [xs_x16 + 2 * (4 * XS_I + 4)]
    mov bx, ax
    XS_ISUB ax, [xs_x16 + 2 * (4 * XS_I + 3)]
    mov [xs_si + 2 * (7 - 2 * XS_I)], ax
    XS_IADD bx, [xs_x16 + 2 * (4 * XS_I + 3)]
    XS_INEG bx
    mov [xs_co + 2 * (2 + 2 * XS_I)], bx
%assign XS_I XS_I + 1
%endrep
    mov si, xs_co
    call xs_dct9
    mov si, xs_si
    call xs_dct9
%assign XS_I 1
%rep 4
    mov ax, [xs_si + 2 * XS_I]
    XS_INEG ax
    mov [xs_si + 2 * XS_I], ax
%assign XS_I XS_I + 2
%endrep
    ; nine output pairs and nine overlap words: unrolled when k >= 0
    cmp word [xs_k], 0
    jl .gen
    mov bx, [xs_ovp]
    mov di, [xs_wg]
%assign XS_I 0
%rep 9
    XS_PAIRL XS_I
%assign XS_I XS_I + 1
%endrep
    ret
.gen:
    xor cx, cx
.pair:
    push cx
    mov bx, cx
    shl bx, 1
    mov ax, [xs_co+bx]
    mov [xs_mco], ax
    mov ax, [xs_si+bx]
    mov [xs_msi], ax
    shl bx, 1                       ; 4 i
    mov ax, [xs_bx]
    add ax, bx
    mov [xs_mlo], ax                ; out[i]
    mov ax, [xs_bx]
    add ax, 17 * 4
    sub ax, bx
    mov [xs_mhi], ax                ; out[17 - i]
    mov di, bx
    shl di, 1
    shl di, 1                       ; 16 i
    add di, [xs_wg]
    add bx, [xs_ovp]                ; BX -> ovl[i]
    call [xs_mpp]
    pop cx
    inc cx
    cmp cx, 9
    jb .pair
    ret

; xs_imdct12 - a short block's subband: out[0..5] = the overlap's first six,
; then three 12-point windows chained through its last three; the third's
; outputs are the new overlap's first six (x3sim imdct_short_i)
xs_imdct12:
    mov si, [xs_ovp]
    mov di, [xs_bx]
    mov cx, 12
    rep movsw                       ; out[0..5] = ovl[0..5]
    mov di, xs_o3                   ; o3 = ovl[6..8]
    mov cx, 6
    rep movsw
    mov byte [xs_w], 0
.win:
    ; g[j] = x16[w + 3 j]
    mov bl, [xs_w]
    xor bh, bh
    shl bx, 1
    add bx, xs_x16                  ; BX -> g0; g_j at BX + 6 j
    ; co = idct3(-g0, g2 + g1, g4 + g3)
    mov ax, [bx]
    XS_INEG ax
    mov [xs_g3a], ax
    mov ax, [bx+12]
    XS_IADD ax, [bx+6]
    mov [xs_g3a+2], ax
    mov ax, [bx+24]
    XS_IADD ax, [bx+18]
    mov [xs_g3a+4], ax
    ; si = idct3(g5, g4 - g3, g2 - g1)
    mov ax, [bx+30]
    mov [xs_g3b], ax
    mov ax, [bx+24]
    XS_ISUB ax, [bx+18]
    mov [xs_g3b+2], ax
    mov ax, [bx+12]
    XS_ISUB ax, [bx+6]
    mov [xs_g3b+4], ax
    mov si, xs_g3a
    mov di, xs_co
    call xs_idct3
    mov si, xs_g3b
    mov di, xs_si
    call xs_idct3
    mov ax, [xs_si+2]               ; si[1] = -si[1]
    XS_INEG ax
    mov [xs_si+2], ax
    ; where this window's six go
    mov ax, [xs_ovp]                ; window 2: the overlap
    cmp byte [xs_w], 2
    je .d
    mov ax, [xs_bx]
    add ax, 6 * 4                   ; window 0: out[6..11]
    cmp byte [xs_w], 0
    je .d
    add ax, 6 * 4                   ; window 1: out[12..17]
.d:
    mov [xs_wd], ax
    xor cx, cx
.pair:
    push cx
    mov bx, cx
    shl bx, 1
    mov ax, [xs_co+bx]
    mov [xs_mco], ax
    mov ax, [xs_si+bx]
    mov [xs_msi], ax
    shl bx, 1                       ; 4 i
    mov ax, [xs_wd]
    add ax, bx
    mov [xs_mlo], ax                ; dst[i]
    mov ax, [xs_wd]
    add ax, 5 * 4
    sub ax, bx
    mov [xs_mhi], ax                ; dst[5 - i]
    mov di, bx
    shl di, 1
    shl di, 1                       ; 16 i
    add di, xs_imc + 2 * XS_IMC_L   ; the short groups
    add bx, xs_o3
    call [xs_mpp]                   ; o3[i] becomes the new
    pop cx
    inc cx
    cmp cx, 3
    jb .pair
    inc byte [xs_w]
    cmp byte [xs_w], 3
    jae .done
    jmp .win
.done:
    mov si, xs_o3                   ; ovl[6..8] = o3
    mov di, [xs_ovp]
    add di, 6 * 4
    mov cx, 6
    rep movsw
    ret

XS_C1       equ XS_T(DCT9) + 0
XS_C2       equ XS_T(DCT9) + 2
XS_C3       equ XS_T(DCT9) + 4
XS_C4       equ XS_T(DCT9) + 6
XS_C5       equ XS_T(DCT9) + 8
XS_S3       equ XS_T(DCT9) + 10
XS_C6       equ XS_T(DCT9) + 12

; xs_dct9 - minimp3's 9-point DCT-III in place on the nine words at SI
; (x3sim.dct3_9_i). clobbers: AX, BX, CX, DX
xs_dct9:
    ; the even half
    mov ax, [si+12]                 ; t0 = s0 + s6/2
    XS_IHALF ax
    XS_IADD ax, [si]
    mov [xs_e_t0], ax
    mov ax, [si]                    ; s0' = s0 - s6
    XS_ISUB ax, [si+12]
    mov [xs_e_s0], ax
    mov ax, [si+8]                  ; t4 = (s4 + s2) c2
    XS_IADD ax, [si+4]
    XS_IMUL XS_C2
    mov [xs_e_t4], ax
    mov ax, [si+16]                 ; t2 = (s8 + s2) c1
    XS_IADD ax, [si+4]
    XS_IMUL XS_C1
    mov [xs_e_t2], ax
    mov ax, [si+8]                  ; s6' = (s4 - s8) c3
    XS_ISUB ax, [si+16]
    XS_IMUL XS_C3
    mov [xs_e_s6], ax
    mov bx, [si+16]                 ; s4' = s4 + (s8 - s2)
    XS_ISUB bx, [si+4]
    mov ax, [si+8]
    XS_IADD ax, bx
    mov [xs_e_s4], ax
    mov bx, ax                      ; s2' = s0' - s4'/2
    XS_IHALF bx
    mov cx, [xs_e_s0]
    XS_ISUB cx, bx
    mov [xs_e_s2], cx
    mov bx, [xs_e_s0]               ; o4 = s4' + s0'
    XS_IADD ax, bx
    mov [xs_e_o4], ax
    mov ax, [xs_e_t0]               ; s8' = (t0 - t2) + s6'
    XS_ISUB ax, [xs_e_t2]
    XS_IADD ax, [xs_e_s6]
    mov [xs_e_s8], ax
    mov ax, [xs_e_t0]               ; s0'' = (t0 - t4) + t2
    XS_ISUB ax, [xs_e_t4]
    XS_IADD ax, [xs_e_t2]
    mov [xs_e_s0], ax
    mov ax, [xs_e_t0]               ; s4'' = (t0 + t4) - s6'
    XS_IADD ax, [xs_e_t4]
    XS_ISUB ax, [xs_e_s6]
    mov [xs_e_s4], ax
    ; the odd half
    mov ax, [si+6]                  ; s3' = s3 x s3c
    XS_IMUL XS_S3
    mov [xs_o_s3], ax
    mov ax, [si+10]                 ; t0 = (s5 + s1) c4
    XS_IADD ax, [si+2]
    XS_IMUL XS_C4
    mov [xs_o_t0], ax
    mov ax, [si+10]                 ; t4 = (s5 - s7) c5
    XS_ISUB ax, [si+14]
    XS_IMUL XS_C5
    mov [xs_o_t4], ax
    mov ax, [si+2]                  ; t2 = (s1 + s7) c6
    XS_IADD ax, [si+14]
    XS_IMUL XS_C6
    mov [xs_o_t2], ax
    mov ax, [si+2]                  ; s1' = ((s1 - s5) - s7) s3c
    XS_ISUB ax, [si+10]
    XS_ISUB ax, [si+14]
    XS_IMUL XS_S3
    mov [xs_o_s1], ax
    mov ax, [xs_o_t0]               ; s5' = (t0 - s3') - t2
    XS_ISUB ax, [xs_o_s3]
    XS_ISUB ax, [xs_o_t2]
    mov [xs_o_s5], ax
    mov ax, [xs_o_t4]               ; s7' = (t4 - s3') - t0
    XS_ISUB ax, [xs_o_s3]
    XS_ISUB ax, [xs_o_t0]
    mov [xs_o_s7], ax
    mov ax, [xs_o_t4]               ; s3'' = (t4 + s3') - t2
    XS_IADD ax, [xs_o_s3]
    XS_ISUB ax, [xs_o_t2]
    mov [xs_o_s3], ax
    ; the nine outputs
    mov ax, [xs_e_s4]
    XS_ISUB ax, [xs_o_s7]
    mov [si], ax                    ; y0 = s4'' - s7'
    mov ax, [xs_e_s2]
    XS_IADD ax, [xs_o_s1]
    mov [si+2], ax                  ; y1 = s2' + s1'
    mov ax, [xs_e_s0]
    XS_ISUB ax, [xs_o_s3]
    mov [si+4], ax                  ; y2 = s0'' - s3''
    mov ax, [xs_e_s8]
    XS_IADD ax, [xs_o_s5]
    mov [si+6], ax                  ; y3 = s8' + s5'
    mov ax, [xs_e_o4]
    mov [si+8], ax                  ; y4 = o4
    mov ax, [xs_e_s8]
    XS_ISUB ax, [xs_o_s5]
    mov [si+10], ax                 ; y5 = s8' - s5'
    mov ax, [xs_e_s0]
    XS_IADD ax, [xs_o_s3]
    mov [si+12], ax                 ; y6 = s0'' + s3''
    mov ax, [xs_e_s2]
    XS_ISUB ax, [xs_o_s1]
    mov [si+14], ax                 ; y7 = s2' - s1'
    mov ax, [xs_e_s4]
    XS_IADD ax, [xs_o_s7]
    mov [si+16], ax                 ; y8 = s4'' + s7'
    ret

; xs_idct3 - the three words at SI -> three at DI (x3sim.idct3_i):
; m1 = x1 s3c, a1 = x0 - x2/2: (a1 + m1, x0 + x2, a1 - m1). clobbers AX..DX
xs_idct3:
    mov ax, [si+2]
    XS_IMUL XS_S3
    mov cx, ax                      ; m1
    mov bx, [si+4]
    XS_IHALF bx
    mov ax, [si]
    XS_ISUB ax, bx                  ; a1
    mov bx, ax
    XS_IADD ax, cx
    mov [di], ax
    XS_ISUB bx, cx
    mov [di+4], bx
    mov ax, [si]
    XS_IADD ax, [si+4]
    mov [di+2], ax
    ret

; =============================================================================
; xs_mpair - one output pair and one overlap word of the IMDCT (x3sim _merge
; and _ovl): [xs_mco] co, [xs_msi] si, DI -> the group {w, w9, a, b, c, d,
; tw0, tw1}, BX -> the overlap dword o (replaced by the new one), [xs_mlo]
; and [xs_mhi] -> the two outputs:
;   lo = (sat32(((o w) >> 14) - t1) + 1) >> 1,  t1 = kshift(co a + si b)
;   hi = (sat32(((o w9) >> 14) + t2) + 1) >> 1, t2 = kshift(co c + si d)
;   o' = round(co tw0 - si tw1, 1 + k)
; clobbers: AX, CX, DX, BP, SI
; =============================================================================
xs_mpair:
    mov ax, [xs_mco]                ; t1
    imul word [di+4]
    mov cx, ax
    mov bp, dx
    mov ax, [xs_msi]
    imul word [di+6]
    add ax, cx
    adc dx, bp
    call xs_kshift
    mov [xs_mt1], ax
    mov [xs_mt1+2], dx
    mov ax, [xs_mco]                ; t2
    imul word [di+8]
    mov cx, ax
    mov bp, dx
    mov ax, [xs_msi]
    imul word [di+10]
    add ax, cx
    adc dx, bp
    call xs_kshift
    mov [xs_mt2], ax
    mov [xs_mt2+2], dx
    mov cx, [di]                    ; lo
    call xs_ow14
    mov ax, [xs_mt1]
    mov dx, [xs_mt1+2]
    call xs_usub
    mov si, [xs_mlo]
    mov [si], ax
    mov [si+2], dx
    mov cx, [di+2]                  ; hi
    call xs_ow14
    mov ax, [xs_mt2]
    mov dx, [xs_mt2+2]
    call xs_uadd
    mov si, [xs_mhi]
    mov [si], ax
    mov [si+2], dx
    mov ax, [xs_mco]                ; the new overlap
    imul word [di+12]
    mov cx, ax
    mov bp, dx
    mov ax, [xs_msi]
    imul word [di+14]
    sub cx, ax
    sbb bp, dx
    mov ax, cx
    mov dx, bp
    call xs_oround
    mov [bx], ax
    mov [bx+2], dx
    ret

; =============================================================================
; xs_mpf - xs_mpair's contract, FAST, for k >= 0 (the caller's choice): the
; same arithmetic in 32-bit registers wherever the overlap word |o| < 2^30 -
; then U = (o w) >> 14 = 4 (o_hi w) + ((o_lo w) >> 14) fits int32, and
; U -/+ t saturates exactly as a 32-bit sub/add's overflow says. A larger
; overlap word (a stream far past full scale) takes the general xs_mpair.
; clobbers: AX, CX, DX, BP, SI
; =============================================================================
xs_mpf:
    mov ax, [bx+2]                  ; |o| < 2^30?
    mov cx, ax
    add cx, 0x4000
    jns .fast
    jmp xs_mpair
.fast:
    ; t1 = (co a + si b) >> k, t2 = (co c + si d) >> k
    mov ax, [xs_mco]
    imul word [di+4]
    mov cx, ax
    mov si, dx
    mov ax, [xs_msi]
    imul word [di+6]
    add ax, cx
    adc dx, si
    call [xs_srkp]
    mov [xs_mt1], ax
    mov [xs_mt1+2], dx
    mov ax, [xs_mco]
    imul word [di+8]
    mov cx, ax
    mov si, dx
    mov ax, [xs_msi]
    imul word [di+10]
    add ax, cx
    adc dx, si
    call [xs_srkp]
    mov [xs_mt2], ax
    mov [xs_mt2+2], dx
    ; lo = (sat32(U(w) - t1) + 1) >> 1
    mov bp, [di]
    call xs_uw                      ; DX:AX = (o w) >> 14
    sub ax, [xs_mt1]
    sbb dx, [xs_mt1+2]
    XS_SAT32 XS_ST_IMDCT
    sar dx, 1
    rcr ax, 1
    adc ax, 0
    adc dx, 0
    mov si, [xs_mlo]
    mov [si], ax
    mov [si+2], dx
    ; hi = (sat32(U(w9) + t2) + 1) >> 1
    mov bp, [di+2]
    call xs_uw
    add ax, [xs_mt2]
    adc dx, [xs_mt2+2]
    XS_SAT32 XS_ST_IMDCT
    sar dx, 1
    rcr ax, 1
    adc ax, 0
    adc dx, 0
    mov si, [xs_mhi]
    mov [si], ax
    mov [si+2], dx
    ; the new overlap: ((co tw0 - si tw1) >> k + 1) >> 1
    mov ax, [xs_mco]
    imul word [di+12]
    mov cx, ax
    mov si, dx
    mov ax, [xs_msi]
    imul word [di+14]
    sub cx, ax
    sbb si, dx
    mov ax, cx
    mov dx, si
    call [xs_srkp]
    add ax, 1
    adc dx, 0
    sar dx, 1
    rcr ax, 1
    mov [bx], ax
    mov [bx+2], dx
    ret

; xs_uw - DX:AX = floor(o BP / 2^14), o the int32 at [BX] with |o| < 2^30,
; BP the window word (>= 0): 4 (o_hi BP) + ((o_lo BP) >> 14), exactly
; clobbers: CX, SI
xs_uw:
    mov ax, [bx+2]
    imul bp                         ; o_hi w, |.| < 2^29
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov cx, ax
    mov si, dx                      ; SI:CX = 4 o_hi w
    mov ax, [bx]
    mul bp                          ; o_lo w, unsigned, < 2^31
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1                       ; CF:DX = its bits 14..30
    mov ax, dx
    mov dx, 0
    adc dx, 0
    add ax, cx
    adc dx, si
    ret

; xs_srk0 .. xs_srk19 - DX:AX >>= n, arithmetic: [xs_srkp] for the IMDCT's
; k and [xs_scp] for the band scale's sh - 1, picked once a subband by
; xs_scale. n >= 8 moves bytes first, n >= 16 a word
xs_srkt:
%assign XS_I 0
%rep 20
    dw xs_srk %+ XS_I
%assign XS_I XS_I + 1
%endrep
%assign XS_I 20
%rep 20
%assign XS_I XS_I - 1
xs_srk %+ XS_I:
  %if XS_I >= 16
    mov ax, dx                      ; >> 16: the high word down, DX the sign
    cwd
    %rep XS_I - 16
    sar ax, 1
    %endrep
  %elif XS_I >= 8
    mov al, ah                      ; >> 8: the bytes down, DX the sign
    mov ah, dl
    mov dl, dh
    xchg ax, dx
    cbw
    xchg ax, dx
    %rep XS_I - 8
    sar dx, 1
    rcr ax, 1
    %endrep
  %else
    %rep XS_I
    sar dx, 1
    rcr ax, 1
    %endrep
  %endif
    ret
%endrep

; xs_kshift - DX:AX >>= k (arithmetic, k >= 0), or <<= -k saturating (k < 0,
; counted in imdct). clobbers: CX, SI
xs_kshift:
    mov cl, [xs_kb]
    or cl, cl
    jz .out
    js .left
    jmp xs_sar32
.left:
    neg cl
    mov ch, dh                      ; the sign, for a clamp
.l:
    shl ax, 1
    rcl dx, 1
    jo .sat                         ; bits 31 and 30 differed
    dec cl
    jnz .l
.out:
    ret
.sat:
    XS_CNT XS_ST_IMDCT
    mov ax, 0xFFFF
    mov dx, 0x7FFF
    or ch, ch
    jns .o
    inc ax
    inc dx
.o:
    ret

; xs_oround - the overlap word from p = DX:AX at Q(29 + k): (p + 2^k) >> (1 +
; k) for k >= 0, else p << (-1 - k) saturating (counted in imdct)
; clobbers: CX, SI
xs_oround:
    mov cl, [xs_kb]
    or cl, cl
    js .left
    call xs_sar32                   ; ((p >> k) + 1) >> 1
    add ax, 1
    adc dx, 0
    sar dx, 1
    rcr ax, 1
    ret
.left:
    neg cl                          ; -1 - k (0..5)
    dec cl
    jz .out
    mov ch, dh
.l:
    shl ax, 1
    rcl dx, 1
    jo .sat
    dec cl
    jnz .l
.out:
    ret
.sat:
    XS_CNT XS_ST_IMDCT
    mov ax, 0xFFFF
    mov dx, 0x7FFF
    or ch, ch
    jns .o
    inc ax
    inc dx
.o:
    ret

; xs_ow14 - U = floor(o w / 2^14), o the int32 at [BX], w = CX >= 0: its low
; 32 bits to [xs_mu], its top (bits 32, 33, signed) to [xs_mu+4]
; clobbers: AX, CX, DX, BP, SI
xs_ow14:
    mov ax, [bx]
    mul cx
    mov si, ax
    mov bp, dx
    mov ax, [bx+2]
    imul cx
    add bp, ax
    adc dx, 0                       ; P = DX:BP:SI
    mov ax, dx
    mov cl, 14
    sar ax, cl
    mov [xs_mu+4], ax               ; P's bits 46, 47
    shl si, 1
    rcl bp, 1
    rcl dx, 1
    shl si, 1
    rcl bp, 1
    rcl dx, 1
    mov [xs_mu], bp                 ; P's bits 14..45
    mov [xs_mu+2], dx
    ret

; xs_usub / xs_uadd - DX:AX = (sat32(U -/+ t) + 1) >> 1, t = DX:AX in, U =
; [xs_mu] (48 bits). clobbers: CX, BP, SI
xs_usub:
    mov si, [xs_mu]
    mov bp, [xs_mu+2]
    mov cx, [xs_mu+4]
    sub si, ax
    sbb bp, dx
    sbb cx, 0
    or dx, dx
    jns xs_vfin
    inc cx                          ; t's sign extension, subtracted
    jmp short xs_vfin
xs_uadd:
    mov si, [xs_mu]
    mov bp, [xs_mu+2]
    mov cx, [xs_mu+4]
    add si, ax
    adc bp, dx
    adc cx, 0
    or dx, dx
    jns xs_vfin
    dec cx
xs_vfin:
    mov ax, bp                      ; V = CX:BP:SI fits int32 when CX is
    cwd                             ; BP's sign
    cmp dx, cx
    jne .sat
    mov ax, si
    mov dx, bp
.half:
    sar dx, 1                       ; (u + 1) >> 1
    rcr ax, 1
    adc ax, 0
    adc dx, 0
    ret
.sat:
    XS_CNT XS_ST_IMDCT
    mov ax, 0xFFFF
    mov dx, 0x7FFF
    or cx, cx
    jns .half
    inc ax
    inc dx
    jmp short .half

; =============================================================================
; THE SYNTHESIS (x3sim Decoder.synth, IntSynth): the mono mix or the copy,
; then per output channel and slot the DCT-II into the ring and the window
; into PCM
; =============================================================================
xs_synth:
    cmp byte [xs_och], 1
    jne .nomix
    cmp byte [xs_fnch], 2
    jne .nomix
    ; the mono mix: (a + b + 1) >> 1 = (a >> 1) + (b >> 1) + ((a | b) & 1)
    mov al, 18
    mul byte [xs_nsb]
    mov cx, ax
    mov si, xs_xr
.mx:
    mov ax, [si]
    mov dx, [si+2]
    mov bx, ax
    or bx, [si+2304]
    and bx, 1
    sar dx, 1
    rcr ax, 1
    mov bp, [si+2306]
    mov di, [si+2304]
    sar bp, 1
    rcr di, 1
    add ax, di
    adc dx, bp
    add ax, bx
    adc dx, 0
    mov [si], ax
    mov [si+2], dx
    add si, 4
    loop .mx
.nomix:
    mov al, 2                       ; bytes a sample, and a frame's stride
    cmp byte [xs_fmt], X3_FMT_S16
    je .s16
    mov al, 1
.s16:
    mov [xs_bps], al
    mul byte [xs_och]
    mov [xs_stride], ax
    mov byte [xs_oc], 0
.chan:
    mov ax, xs_xr                   ; the source: channel oc's, or channel 0's
    cmp byte [xs_fnch], 1           ; when the frame has one
    je .src
    cmp byte [xs_oc], 0
    je .src
    add ax, 576 * 4
.src:
    mov [xs_src], ax
    mov ax, [xs_ooff]               ; this channel's first output sample
    cmp byte [xs_oc], 0
    je .o0
    mov al, [xs_bps]
    xor ah, ah
    add ax, [xs_ooff]
.o0:
    mov [xs_op], ax
    mov al, [xs_oc]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov [xs_rpp], bx                ; its ring position's word
    mov cl, 11
    shl ax, cl                      ; x 2048: its ring
    add ax, xs_ring
    mov [xs_rb], ax
    mov byte [xs_slot], 0
.slot:
    mov bx, [xs_rpp]
    mov ax, [xs_rpos+bx]            ; pos = (pos + 1) & 15
    inc ax
    and ax, 15
    mov [xs_rpos+bx], ax
    shl ax, 1
    add ax, [xs_rb]
    mov di, ax                      ; DI = ring + 2 pos
    mov bl, [xs_slot]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add bx, [xs_src]                ; BX = subband 0 of slot t
    push di
    call [xs_dctp]
    pop di
    call [xs_winp]
    call xs_put
    inc byte [xs_slot]
    cmp byte [xs_slot], 18
    jb .slot
    inc byte [xs_oc]
    mov al, [xs_oc]
    cmp al, [xs_och]
    jb .chan
    ret

; xs_put - the slot's nsb samples at xs_pcmt to the output: gain, format,
; interleave. clobbers: AX, BX, CX, DX, SI, DI, ES
xs_put:
    mov es, [xs_oseg]
    mov di, [xs_op]
    mov si, xs_pcmt
    mov cl, [xs_nsb]
    xor ch, ch
    mov bx, [xs_stride]
.s:
    mov ax, [si]
    cmp word [xs_gain], 0
    je .g
    imul word [xs_gain]             ; (v gain) >> 8, saturating to int16:
    mov al, ah                      ; AX = the product's bits 8..23, which
    mov ah, dl                      ; is the answer when DH is DL's sign
    push ax
    mov al, dl
    cbw
    cmp ah, dh
    pop ax
    je .g
    mov ax, 0x7FFF
    or dh, dh
    jns .g
    inc ax
.g:
    cmp byte [xs_fmt], 0
    jne .u8
    mov [es:di], ax
    jmp short .n
.u8:
    mov al, ah
    xor al, 0x80
    mov [es:di], al
.n:
    add di, bx
    add si, 2
    loop .s
    mov [xs_op], di
    push ds
    pop es
    ret

; =============================================================================
; THE TABLES - x3std.bin from P43M on, CRC-checked at the first OPEN
; =============================================================================
%include "x3crc.inc"
    align 2
xs_tab:     incbin "x3std.bin", XS_TOFF, X3T_BYTES - XS_TOFF
xs_rot      equ XS_T(ROT)
xs_synw     equ XS_T(SYNW)

; =============================================================================
; THE STATE (zero in the part's image; OPEN and RESET clear what they own)
; =============================================================================
    align 2
xs_isopen:  db 0
xs_crcok:   db 0
xs_div:     db 0
xs_nsb:     db 0
xs_sch:     db 0
xs_och:     db 0
xs_fmt:     db 0
xs_ver:     db 0
xs_sri:     db 0
xs_fnch:    db 0                    ; this frame's channels
xs_ch:      db 0
xs_b:       db 0
xs_bt:      db 0                    ; the channel's block: type, mixed, window
xs_mix:     db 0                    ; switching, and whether it is the
xs_ws:      db 0                    ; record's (1) or silence's (0)
xs_real:    db 0
xs_rsign:   db 0
xs_plan:    db 0
xs_acnt:    db 0
xs_kb:      db 0
xs_sh:      db 0
xs_w:       db 0
xs_slot:    db 0
xs_oc:      db 0
xs_bps:     db 0
    align 2
xs_gain:    dw 0
xs_rseg:    dw 0                    ; the record and the output
xs_roff:    dw 0
xs_oseg:    dw 0
xs_ooff:    dw 0
xs_gclip:   dw 0                    ; this granule's clips
xs_k:       dw 0                    ; the band scale
xs_bx:      dw 0
xs_rf:      dw 0
xs_scnt:    dw 0                    ; stereo
xs_istab:   dw 0
xs_isone:   dw 0
xs_ppl:     dw 0
xs_sl:      dw 0
xs_kl:      dw 0
xs_kr:      dw 0
xs_rdst:    dw 0                    ; reorder
xs_rbase:   dw 0
xs_rw:      dw 0
xs_ovp:     dw 0                    ; the IMDCT
xs_wg:      dw 0
xs_wd:      dw 0
xs_mco:     dw 0
xs_msi:     dw 0
xs_mlo:     dw 0
xs_mhi:     dw 0
xs_mt1:     dw 0, 0
xs_mt2:     dw 0, 0
xs_mu:      dw 0, 0, 0
xs_au:      dw 0, 0                 ; antialias
xs_ad:      dw 0, 0
xs_src:     dw 0                    ; the synthesis
xs_op:      dw 0
xs_stride:  dw 0
xs_rpp:     dw 0
xs_rb:      dw 0
xs_dctp:    dw 0
xs_winp:    dw 0
xs_mpp:     dw 0                    ; the IMDCT's pair routine, its shift
xs_srkp:    dw 0
xs_scp:     dw 0                    ; the band scale's shift
xs_e_t0:    dw 0                    ; xs_dct9's temporaries
xs_e_s0:    dw 0
xs_e_t4:    dw 0
xs_e_t2:    dw 0
xs_e_s6:    dw 0
xs_e_s4:    dw 0
xs_e_s2:    dw 0
xs_e_o4:    dw 0
xs_e_s8:    dw 0
xs_o_s3:    dw 0
xs_o_t0:    dw 0
xs_o_t4:    dw 0
xs_o_t2:    dw 0
xs_o_s1:    dw 0
xs_o_s5:    dw 0
xs_o_s7:    dw 0
xs_g3a:     dw 0, 0, 0
xs_g3b:     dw 0, 0, 0
xs_co:      times 9 dw 0
xs_si:      times 9 dw 0
xs_x16:     times 18 dw 0
xs_o3:      times 6 dw 0
xs_pcmt:    times 32 dw 0
xs_t:       times X3SG_TSLOTS dd 0
xs_imc:     times 2 * XS_IMC_L + 3 * 16 db 0
; the counters (OPEN zeros them)
xs_sat:     times 8 dd 0
xs_ngran:   dd 0
xs_nclip:   dd 0
xs_nsil:    dd 0
xs_cnt_end:
; the history (RESET zeros it)
xs_ovl:     times 2 * 32 * 9 dd 0
xs_ring:    times 2 * 32 * 32 dw 0  ; [channel][column][16 slots, twice]
xs_rpos:    dw 0, 0
xs_prevbt:  db 0, 0
xs_hist_end:
    align 2
xs_xr:      times 2 * 576 dd 0
xs_tmp:     times 576 dd 0
xs_end:
