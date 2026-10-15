; =============================================================================
; os8088 - apps/fptest/fpcases.inc - every case FPTEST checks (SPEC.md 84.4)
;
; Every expected double here is what a real IEEE-754 double produced on the
; host, not this tree's arithmetic restated - see fptest.asm's header. The two
; exceptions are deliberate and marked: the overflow CLAMP and the subnormal
; FLUSH are os88fp.inc's STATED simplifications (its header), where a real
; double produces an infinity or a subnormal instead.
;
; THE ENCODING is a size decision and not a weaker test (the first apps size
; pass). The 70 cases it was written for named only 68 distinct doubles between them, and 29 of
; those are zero below their top word, so each double is written ONCE, into a
; POOL, and a case names it by a one-byte index that fpt_ld expands back into
; the eight bytes the old tables carried inline. The bytes compared are the
; same bytes - a case still passes only if all eight match - and each pool
; name says which value it is (d_0p1 is 0.1, d_m3 is -3, d_1em2 is 1e-2).
; Bit 7 of an index byte is free (fpt_ld masks it), so a case's OPERATOR
; rides there, in the two bytes fpt_op reads.
; =============================================================================

%assign fpt_np 0                        ; the pool's running index
%assign fpt_ncase 0                     ; and the cases', over every group

%macro FPW 2                            ; name, top word: a double whose low
%1 equ fpt_np                           ; 48 bits are zero
%assign fpt_np fpt_np+1
    dw %2
%endmacro

%macro FPQ 5                            ; name, four words, low word first
%1 equ fpt_np
%assign fpt_np fpt_np+1
    dw %2, %3, %4, %5
%endmacro

; --- the POOL: each distinct double once. The word class comes FIRST, so an
; index below FPT_NW is a top word and one at or above it a whole double.

fpt_pw:                                ; ...the low 48 bits zero: the top word
    FPW d_0,                   0x0000           ; 0.0
    FPW d_2em53,               0x3CA0           ; 1.1102230246251565e-16
    FPW d_0p125,               0x3FC0           ; 0.125
    FPW d_0p25,                0x3FD0           ; 0.25
    FPW d_0p5,                 0x3FE0           ; 0.5
    FPW d_1,                   0x3FF0           ; 1.0
    FPW d_1p5,                 0x3FF8           ; 1.5
    FPW d_2,                   0x4000           ; 2.0
    FPW d_2p25,                0x4002           ; 2.25
    FPW d_2p5,                 0x4004           ; 2.5
    FPW d_3,                   0x4008           ; 3.0
    FPW d_4,                   0x4010           ; 4.0
    FPW d_5,                   0x4014           ; 5.0
    FPW d_6,                   0x4018           ; 6.0
    FPW d_7,                   0x401C           ; 7.0
    FPW d_10,                  0x4024           ; 10.0
    FPW d_12,                  0x4028           ; 12.0
    FPW d_42,                  0x4045           ; 42.0
    FPW d_100,                 0x4059           ; 100.0
    FPW d_144,                 0x4062           ; 144.0
    FPW d_m1,                  0xBFF0           ; -1.0
    FPW d_m1p5,                0xBFF8           ; -1.5
    FPW d_m2,                  0xC000           ; -2.0
    FPW d_m2p5,                0xC004           ; -2.5
    FPW d_m3,                  0xC008           ; -3.0
    FPW d_m4,                  0xC010           ; -4.0
    FPW d_m4p5,                0xC012           ; -4.5
    FPW d_m7p5,                0xC01E           ; -7.5
    FPW d_m9,                  0xC022           ; -9.0
    FPW d_32768,               0x40E0           ; 32768.0  (fp_a2i: one past)
    FPW d_m32768,              0xC0E0           ; -32768.0 (the int16 floor)
    FPW d_m65536,              0xC0F0           ; -65536.0 (fp_a2i: under)
FPT_NW equ fpt_np                       ; the word class ends here
fpt_pq:                                ; ...and the rest, whole
    FPQ d_0p1,                 0x999A, 0x9999, 0x9999, 0x3FB9 ; 0.1
    FPQ d_0p2,                 0x999A, 0x9999, 0x9999, 0x3FC9 ; 0.2
    FPQ d_0p3plus,             0x3334, 0x3333, 0x3333, 0x3FD3 ; 0.30000000000000004
    FPQ d_10000000000,         0x0000, 0x2000, 0xA05F, 0x4202 ; 10000000000.0
    FPQ d_10000000001,         0x0000, 0x2008, 0xA05F, 0x4202 ; 10000000001.0
    FPQ d_0p3,                 0x3333, 0x3333, 0x3333, 0x3FD3 ; 0.3
    FPQ d_0p2minus,            0x9999, 0x9999, 0x9999, 0x3FC9 ; 0.19999999999999998
    FPQ d_100000,              0x0000, 0x0000, 0x6A00, 0x40F8 ; 100000.0
    FPQ d_third,               0x5555, 0x5555, 0x5555, 0x3FD5 ; 0.3333333333333333
    FPQ d_0p01,                0x147B, 0x47AE, 0x7AE1, 0x3F84 ; 0.01
    FPQ d_1000000000000,       0x0000, 0xA200, 0x1A94, 0x426D ; 1000000000000.0
    FPQ d_2third,              0x5555, 0x5555, 0x5555, 0x3FE5 ; 0.6666666666666666
    FPQ d_1ulp,                0x0001, 0x0000, 0x0000, 0x3FF0 ; 1.0000000000000002
    FPQ d_1ulp2,               0x0002, 0x0000, 0x0000, 0x3FF0 ; 1.0000000000000004
    FPQ d_1em16,               0x89BC, 0x97D8, 0xD2B2, 0x3C9C ; 1e-16
    FPQ d_1e300,               0x759C, 0x8800, 0xE43C, 0x7E37 ; 1e+300
    FPQ d_1e301,               0x9303, 0xAA00, 0xDD4B, 0x7E6D ; 1e+301
    FPQ d_1em300,              0xF359, 0xC2F8, 0x6E1F, 0x01A5 ; 1e-300
    FPQ d_1em301,              0xF5E1, 0x3593, 0x24E6, 0x0171 ; 1e-301
    FPQ d_1e16,                0x8000, 0x37E0, 0xC379, 0x4341 ; 1e+16
    FPQ d_2em53x,              0x0000, 0x0000, 0x0300, 0x3CA0 ; 1.1110361762545207e-16
    FPQ d_msk_a,               0xD8F0, 0x0002, 0x0000, 0x3FF0 ; 1.0000000000414353
    FPQ d_msk_b,               0x7ABD, 0x4CF3, 0xF7A0, 0x3FF8 ; 1.56045560893078
    FPQ d_msk_r,               0xEC37, 0x4CF7, 0xF7A0, 0x3FF8 ; 1.560455608995438
    FPQ d_max,                 0xFFFF, 0xFFFF, 0xFFFF, 0x7FEF ; 1.7976931348623157e+308
    FPQ d_subn,                0xFFFF, 0xFFFF, 0xFFFF, 0x000F ; 2.225073858507201e-308
    FPQ d_sqrt2,               0x3BCD, 0x667F, 0xA09E, 0x3FF6 ; 1.4142135623730951
    FPQ d_1000000,             0x0000, 0x0000, 0x8480, 0x412E ; 1000000.0
    FPQ d_1000,                0x0000, 0x0000, 0x4000, 0x408F ; 1000.0
    FPQ d_3p7,                 0x999A, 0x9999, 0x9999, 0x400D ; 3.7
    FPQ d_m3p7,                0x999A, 0x9999, 0x9999, 0xC00D ; -3.7
    FPQ d_0p9,                 0xCCCD, 0xCCCC, 0xCCCC, 0x3FEC ; 0.9
    FPQ d_3p14159,             0x866E, 0xF01B, 0x21F9, 0x4009 ; 3.14159
    FPQ d_3p14,                0x851F, 0x51EB, 0x1EB8, 0x4009 ; 3.14
    FPQ d_1234,                0x0000, 0x0000, 0x4800, 0x4093 ; 1234.0
    FPQ d_1200,                0x0000, 0x0000, 0xC000, 0x4092 ; 1200.0
    FPQ d_0p13,                0x70A4, 0x0A3D, 0xA3D7, 0x3FC0 ; 0.13
    FPQ d_0p001,               0xA9FC, 0xD2F1, 0x624D, 0x3F50 ; 0.001
    FPQ d_m0p025,              0x999A, 0x9999, 0x9999, 0xBF99 ; -0.025
    FPQ d_32767,               0x0000, 0x0000, 0xFFC0, 0x40DF ; 32767.0
%if fpt_np > 128
%error "the pool has outgrown a 7-bit index: bit 7 of an index byte is an op bit"
%endif

; --- the CASES, in four groups. A group header is its case count, the x of
; its verdict column, its first row and the handler that runs one record;
; fpt_runall walks headers until a zero count. Every handler leaves ZF = pass.
%macro GRP 4                            ; count, verdict x, first row, handler
    db %1, %2, %3
    dw %4
%assign fpt_nc 0
%endmacro

%macro GEND 1                           ; name the group's count
%1 equ fpt_nc
%assign fpt_ncase fpt_ncase+fpt_nc
%endmacro

; BIN op, a, b, expected, 'name' - A = a op b: all eight bytes, and CF for O_DIV
O_ADD equ 0                             ; fp_add
O_SUB equ 1                             ; fp_sub
O_MUL equ 2                             ; fp_mul
O_DIV equ 3                             ; fp_div, through fpt_div
%macro BIN 5
    db %4, %2 | (((%1) & 2) << 6), %3 | (((%1) & 1) << 7), %5, 0
%assign fpt_nc fpt_nc+1
%endmacro

; UNA op, a, expected, digits, 'name' - sqrt / trunc / floor / round
O_SQRT equ 0                            ; fp_sqrt
O_TRUNC equ 1                           ; fp_trunc
O_FLOOR equ 2                           ; fp_floor
O_ROUND equ 3                           ; fp_round, CX = the digit count
%macro UNA 5
    db %3 | (((%1) & 2) << 6), %2 | (((%1) & 1) << 7), %4, %5, 0
%assign fpt_nc fpt_nc+1
%endmacro

; ATOF 'text', expected - text -> double alone. It exists to SPLIT a round-trip
; failure: if these pass, the parser is right and the formatter is wrong.
%macro ATOF 2
    db %1, 0, %2
%assign fpt_nc fpt_nc+1
%endmacro

; RT 'in' [, 'out'] - text -> double -> text at ten significant digits, which
; is what a spreadsheet cell shows. An omitted 'out' means 'in' itself and is
; written as an empty string: most of them come back exactly as they went in.
; I2A n, expected, 'name' - fp_i2a: the signed 16-bit integer n -> A, all
; eight bytes. Sheet and Chart turn every integer they hold into a double
; through it.
%macro I2A 3
    db %2
    dw %1
    db %3, 0
%assign fpt_nc fpt_nc+1
%endmacro

; A2I a, n, cf, 'name' - fp_a2i: A -> AX truncated toward zero, and CF; an
; overflow is CF=1 with AX clamped to 32767 / -32768. cf is 0 or 1.
%macro A2I 4
    db %1
    dw %2
    db -(%3), %4, 0
%assign fpt_nc fpt_nc+1
%endmacro

; FCMP a, b, r, 'name' - fp_cmpab: AX = -1 / 0 / 1 for A < = > B, AND the
; flags to match (a caller may branch on either). On a machine with an 8087
; the second pass runs fpx_cmpab, whose answer is the coprocessor's.
%macro FCMP 4
    db %1, %2, %3, %4, 0
%assign fpt_nc fpt_nc+1
%endmacro

%macro RT 1-2
%if %0 == 1
    db %1, 0, 0
%else
    db %1, 0, %2, 0
%endif
%assign fpt_nc fpt_nc+1
%endmacro

fpt_data:
    GRP FPT_N, 6, 0, fpt_hbin              ; rows 0-32, verdict at x 6
    BIN O_ADD, d_1, d_1, d_2, '1+1'                           ; 1+1 = 2.0
    BIN O_ADD, d_0p1, d_0p2, d_0p3plus, '0.1+0.2'             ; 0.1+0.2 = 0.30000000000000004
    BIN O_ADD, d_2p5, d_m4, d_m1p5, '2.5+(-4)'                ; 2.5+(-4) = -1.5
    BIN O_ADD, d_10000000000, d_1, d_10000000001, '1e10+1'    ; 1e10+1 = 10000000001.0
    BIN O_SUB, d_3, d_5, d_m2, '3-5'                          ; 3-5 = -2.0
    BIN O_SUB, d_0p3, d_0p1, d_0p2minus, '0.3-0.1'            ; 0.3-0.1 = 0.19999999999999998
    BIN O_SUB, d_7, d_7, d_0, '7-7'                           ; 7-7 = 0.0
    BIN O_MUL, d_6, d_7, d_42, '6*7'                          ; 6*7 = 42.0
    BIN O_MUL, d_0p5, d_0p5, d_0p25, '0.5*0.5'                ; 0.5*0.5 = 0.25
    BIN O_MUL, d_m3, d_2p5, d_m7p5, '-3*2.5'                  ; -3*2.5 = -7.5
    BIN O_MUL, d_100000, d_100000, d_10000000000, '1e5*1e5'   ; 1e5*1e5 = 10000000000.0
    BIN O_DIV, d_1, d_3, d_third, '1/3'                       ; 1/3 = 0.3333333333333333
    BIN O_DIV, d_10, d_4, d_2p5, '10/4'                       ; 10/4 = 2.5
    BIN O_DIV, d_m9, d_2, d_m4p5, '-9/2'                      ; -9/2 = -4.5
    BIN O_DIV, d_10000000000, d_0p01, d_1000000000000, '1e10/1e-2'; 1e10/1e-2 = 1000000000000.0
    BIN O_DIV, d_2, d_3, d_2third, '2/3'                      ; 2/3 = 0.6666666666666666
    BIN O_ADD, d_1, d_2em53, d_1, 'tie-even'                  ; tie-even = 1.0
    BIN O_ADD, d_1ulp, d_2em53, d_1ulp2, 'tie-up'             ; tie-up = 1.0000000000000004
    BIN O_ADD, d_1, d_1em16, d_1, '1+1e-16'                   ; 1+1e-16 = 1.0
    BIN O_MUL, d_1e300, d_10, d_1e301, '1e300*10'             ; 1e300*10 = 1e+301
    BIN O_DIV, d_1em300, d_10, d_1em301, '1e-300/10'          ; 1e-300/10 = 1e-301
    BIN O_MUL, d_1e300, d_1em300, d_1, 'big-small'            ; big-small = 1.0
    BIN O_SUB, d_5, d_5, d_0, '-0 result'                     ; -0 result = 0.0
    BIN O_ADD, d_1e16, d_1, d_1e16, '1e16+1'                  ; 1e16+1 = 1e+16
    BIN O_DIV, d_7, d_7, d_1, '7/7'                           ; 7/7 = 1.0
    BIN O_MUL, d_m1, d_m1, d_1, '-1*-1'                       ; -1*-1 = 1.0
    BIN O_MUL, d_0, d_5, d_0, '0*5'                           ; 0*5 = 0.0
    BIN O_DIV, d_0, d_5, d_0, '0/5'                           ; 0/5 = 0.0
    BIN O_ADD, d_1, d_2em53x, d_1ulp, 'tie-sticky'            ; 1+(2^-53+2^-64+2^-65): the tie is broken by bits below the working form
    BIN O_MUL, d_msk_a, d_msk_b, d_msk_r, 'mul-sticky'        ; a product whose low 64 bits alone break an apparent tie in the top 64
    BIN O_MUL, d_1e300, d_1e300, d_max, '1e300*1e300'         ; 1e300*1e300 = the CLAMP (stated posture; a real double is inf)
    BIN O_DIV, d_5, d_0, d_0, '5/0'                           ; 5/0 = CF=1 and A=0
    BIN O_MUL, d_subn, d_1e300, d_0, 'subn*1e300'             ; subnormal*1e300 = 0: the FLUSH (stated posture; a real double is 2.2e-8)
    GEND FPT_N
    GRP FPT_MN, 150, 0, fpt_huna           ; rows 0-14, verdict at x 150
    UNA O_SQRT, d_2, d_sqrt2, 0, 'sqrt2'                      ; sqrt2 -> 1.4142135623730951
    UNA O_SQRT, d_144, d_12, 0, 'sqrt144'                     ; sqrt144 -> 12.0
    UNA O_SQRT, d_0p25, d_0p5, 0, 'sqrt.25'                   ; sqrt.25 -> 0.5
    UNA O_SQRT, d_1000000, d_1000, 0, 'sqrt1e6'               ; sqrt1e6 -> 1000.0
    UNA O_SQRT, d_2p25, d_1p5, 0, 'sqrt2.25'                  ; sqrt2.25 -> 1.5
    UNA O_TRUNC, d_3p7, d_3, 0, 'trunc3.7'                    ; trunc3.7 -> 3.0
    UNA O_TRUNC, d_m3p7, d_m3, 0, 'trunc-3.7'                 ; trunc-3.7 -> -3.0
    UNA O_TRUNC, d_0p9, d_0, 0, 'trunc.9'                     ; trunc.9 -> 0.0
    UNA O_FLOOR, d_3p7, d_3, 0, 'floor3.7'                    ; floor3.7 -> 3.0
    UNA O_FLOOR, d_m3p7, d_m4, 0, 'floor-3.7'                 ; floor-3.7 -> -4.0
    UNA O_ROUND, d_3p14159, d_3p14, 2, 'rnd pi,2'             ; rnd pi,2 -> 3.14
    UNA O_ROUND, d_2p5, d_3, 0, 'rnd 2.5'                     ; rnd 2.5 -> 3.0
    UNA O_ROUND, d_m2p5, d_m3, 0, 'rnd -2.5'                  ; rnd -2.5 -> -3.0
    UNA O_ROUND, d_1234, d_1200, -2, 'rnd1234,-2'             ; rnd1234,-2 -> 1200.0
    UNA O_ROUND, d_0p125, d_0p13, 2, 'rnd.125,2'              ; rnd.125,2 -> 0.13
    GEND FPT_MN
    GRP FPT_IN, 150, 15, fpt_hi2a          ; rows 15-19, verdict at x 150
    I2A 1, d_1, 'i2a 1'
    I2A -1, d_m1, 'i2a -1'
    I2A 1000, d_1000, 'i2a 1000'
    I2A 32767, d_32767, 'i2a 32767'
    I2A -32768, d_m32768, 'i2a -32768'
    GEND FPT_IN
    GRP FPT_TN, 150, 20, fpt_ha2i          ; rows 20-26, verdict at x 150
    A2I d_3p7, 3, 0, 'a2i 3.7'                                ; truncation, not rounding
    A2I d_m3p7, -3, 0, 'a2i -3.7'                             ; ...toward ZERO
    A2I d_0p9, 0, 0, 'a2i .9'
    A2I d_32767, 32767, 0, 'a2i 32767'
    A2I d_m32768, -32768, 0, 'a2i -32768'                     ; the floor FITS
    A2I d_32768, 32767, 1, 'a2i 32768'                        ; one past: clamp, CF
    A2I d_m65536, -32768, 1, 'a2i -65536'                     ; under: clamp, CF
    GEND FPT_TN
    GRP FPT_CN, 150, 27, fpt_hcmp          ; rows 27-32, verdict at x 150
    FCMP d_1, d_2, -1, 'cmp 1<2'
    FCMP d_2, d_1, 1, 'cmp 2>1'
    FCMP d_3, d_3, 0, 'cmp 3=3'
    FCMP d_m1, d_1, -1, 'cmp -1<1'                             ; the signs decide
    FCMP d_m3, d_m1, -1, 'cmp -3<-1'                           ; both negative
    FCMP d_0, d_m1, 1, 'cmp 0>-1'                              ; a zero operand
    GEND FPT_CN
    GRP FPT_AN, 200, FPT_N, fpt_hatof      ; rows 33-42, verdict at x 200
    ATOF '1', d_1
    ATOF '2.5', d_2p5
    ATOF '100', d_100
    ATOF '0.1', d_0p1
    ATOF '1e3', d_1000
    ATOF '0.001', d_0p001
    ATOF '1e-3', d_0p001                                      ; a NEGATIVE text exponent
    ATOF '-2.5e-2', d_m0p025                                  ; sign AND exponent together
    ATOF '1e600', d_max                                       ; past any double: the CLAMP, never a folded 1e88
    ATOF '1e-600', d_0                                        ; ...and the FLUSH the other way
    GEND FPT_AN
    GRP FPT_SN, 6, FPT_N, fpt_hstr         ; rows 33-44, verdict at x 6
    RT '1'
    RT '2.5'
    RT '-3.75'
    RT '0.1'
    RT '100'
    RT '0.001'
    RT '123.456'
    RT '1e3', '1000'
    RT '-0.5'
    RT '1000000'
    RT '0'
    RT '3.14159'
    GEND FPT_SN
    dw 0                                   ; no more groups

%if fpt_ncase > 99
%error "fpt_appn prints a failure count in at most two digits"
%endif
