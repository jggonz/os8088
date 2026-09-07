; =============================================================================
; os8088 - apps/infones/nicpu.inc      the 2A03 core (SPEC.md 91.4)
;
; Part of INFONES (SPEC.md 91). Derived from InfoNES fe3295c0 under
; Apache-2.0 - see apps/infones/LICENSE.TXT. Section 4(b): derived from
; InfoNES fe3295c0, restructured for 8086 real mode.
;
; WHAT THIS FILE FOLLOWS, AND WHAT IT DOES NOT. The opcode set, the per-opcode
; cycle costs, the page-cross and taken-branch penalties, the JMP () page-wrap
; bug and the BRK/NMI/IRQ sequencing are the 6502's, cross-read in InfoNES's
; src/K6502.cpp (151 CLK(n) cases), src/K6502.h and src/K6502_rw.h's addressing
; macros, and confirmed against nofrendo's src/cpu/nes6502.h:26-95 for the flag
; bit values, the vectors and INT_CYCLES = 7. Nothing is transcribed: the
; register plan below is C64-SPEC 4.1's, minus decimal, and a PLAN IS NOT
; CODE - apps/c64/c64cpu.inc is GPL-2-or-later by its VICE ancestry and NO
; TEXT OF IT IS COPIED HERE (SPEC.md 91.1's R9).
;
; %included by apps/infones/infones.asm AFTER the compiled C, and by
; hosttest/nicputest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS against nestest.log and blargg's instr_test-v5
; (SPEC.md 91.14.4). What the gate runs is the shipping text.
;
; -----------------------------------------------------------------------------
; THE REGISTER PLAN (SPEC.md 91.4.1)
; -----------------------------------------------------------------------------
;   A          AL
;   P N,Z,C    AH        in `lahf` LAYOUT - N is SF (bit 7), Z is ZF (bit 6),
;                        C is CF (bit 0) - so most results are one `lahf` and
;                        a mask, and ADC/SBC get their carry in with `shr ah,1`
;   P V,D,I    CH        at their 6502 bit positions (V 0x40, D 0x08, I 0x04).
;                        NEVER a `cs:` static: a write into the package's own
;                        code page is LESSONS.md 13's TCG slow path, and V
;                        moves on every ADC, SBC and BIT
;   X          CL        indexed addressing is `add bl,cl`, whose carry IS the
;                        page-cross penalty, so CH is free for the flags at no
;                        cost
;   Y          DL        DH is the MEMORY DATA byte and the one free scratch
;   PC         SI
;   S          DI        held as the FULL stack address 0x0100 + S, so a push
;                        is `mov [di],v / dec di / or di,0x0100` and the page
;                        wrap is one instruction rather than a mask and an add
;   EA         BX
;   the claim  DS        the 13KB machine claim, for the whole of a run
;   the fetch  ES        biased so [es:si] is PC's byte, with a two-compare
;                        boundary guard
;
; THERE IS NO DECIMAL MODE and therefore no `BP` scratch frame: a 2A03 has no
; decimal ADC or SBC in the silicon, and the whole of C64-SPEC 4.1's twelve
; bytes below BP exists for the 6510's decimal temporaries. **BP IS THE
; CALLER'S FRAME AND STAYS IT.**
;
; THE STACK WRAP IS `and di,0x00FF / or di,0x0100`, not `and di,0x01FF`. The
; one-line form is wrong and silently so - 0x0200 & 0x01FF is 0x0000, not
; 0x0100 - and C64-SPEC 4.1 records the identical defect passing an entire
; BASIC boot before a test ROM caught it.
;
; B IS NOT A STORED BIT (SPEC.md 91.4.3). The P byte always reads with bit 5
; set and bit 4 clear; PHP and BRK push `P | 0x30`, an interrupt entry pushes
; `P | 0x20`, and PLP and RTI ignore both on the way in. That is what
; nestest.log's P column requires, line for line.
;
; -----------------------------------------------------------------------------
; TIME, AND WHAT A RUN PROMISES
; -----------------------------------------------------------------------------
; Every opcode carries its real cost and the core subtracts it from ONE
; countdown, which lives in the MACHINE CLAIM and not in the package's bss
; (SPEC.md 91.3.1's reason). The countdown is checked BETWEEN instructions and
; never inside one, so `ni_run(n)` may overrun n by at most one instruction's
; cost; what was not spent is left NEGATIVE in ni_m.cnt, and the caller's
; `ran = n - cnt` is exact.
;
; INTERRUPTS are taken between instructions, at the top of a run and at the
; three instructions that can unmask one - CLI, PLP and RTI. That is the whole
; of the cost: no per-instruction test of the pending byte. The C raises a
; line from the frame loop, and the frame loop is what ended the run, so the
; latency of a line-raised IRQ is zero instructions.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4]; the answer in AX; BP, DS,
; SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to lose. ES is
; the KERNEL's on entry to every callback (20.4) and is put back.
; =============================================================================

section .bss

; --- the register file (SPEC.md 91.4.1) ------------------------------------
; Reached from C as `extern struct ni_mach ni_m;` - THE FIELD ORDER BELOW IS
; THE STRUCT'S, and the two cannot drift without the C reading the wrong word.
; NIM_P is a REAL 6502 P byte in both directions: the core unpacks it into
; AH/CH on entry and packs it back on exit, ONCE PER CALL, so the C and the
; harnesses read the register the machine has rather than this file's layout.
_ni_m:
    resw 1                          ; NIM_MACHSEG  the 13KB machine claim
    resw 1                          ; NIM_PC
    resw 1                          ; NIM_A        (low byte)
    resw 1                          ; NIM_X
    resw 1                          ; NIM_Y
    resw 1                          ; NIM_S        (low byte, 0..255)
    resw 1                          ; NIM_P        (a real 6502 P byte)
    resw 1                          ; NIM_CNT      cycles LEFT, signed
    resw 1                          ; NIM_REASON   NI_RUN_* of the last return

NIM_MACHSEG equ 0
NIM_PC      equ 2
NIM_A       equ 4
NIM_X       equ 6
NIM_Y       equ 8
NIM_S       equ 10
NIM_P       equ 12
NIM_CNT     equ 14
NIM_REASON  equ 16

section .text

NI_RUN_SLICE equ 0                  ; the budget was spent
NI_RUN_STOP  equ 1                  ; a KIL, or ni_halt() from a bus handler

; --- the machine claim's layout (SPEC.md 91.3.1) ---------------------------
; THE SAME TABLE AS apps/infones/infones.c's NI_O_* AND NI_S_*, and the two
; are ONE table: a value changed in one file and not the other is a core that
; reads the C's nametable as its own scratch. There is no build-time check
; that can see across the two languages, so the rule is written here and in
; the C, and SPEC.md 91.3.1 is the authority both cite.
NI_O_RAM    equ 0x0000              ;  2,048  CPU RAM $0000-$07FF
NI_O_SRAM   equ 0x0800              ;  8,192  the SRAM window $6000-$7FFF
NI_O_VRAM   equ 0x2800              ;  2,048  the two nametables
NI_O_OAM    equ 0x3000              ;    256  OAM
NI_O_PAL    equ 0x3100              ;     32  palette RAM
NI_SCR      equ 0x3120              ;    256  THIS FILE'S SCRATCH

; ...and the SRAM window as a SIGNED displacement, because that is the one
; access reached through a base register. NASM 2.16 tightened
; `number-overflow`: `[bx + 0xA800]` is a 16-bit displacement and 43,008 is
; outside the signed range it now insists on, so `-w+error` refuses the line
; with `byte data exceeds bounds` at a line with no `db` on it. The encoding
; is identical either way; only the spelling moves.
NI_SRAM_DISP equ NI_O_SRAM - 0x6000

NI_S_DEAD   equ 0                   ; the countdown, signed word - THE hot one
NI_S_BLO    equ 2                   ; the fetch bias's low edge...
NI_S_BOUND  equ 4                   ; ...and its bound. The guard is a RANGE
                                    ;   and not a ceiling, which is what makes
                                    ;   a BACKWARD branch safe without
                                    ;   re-biasing: a JMP from $E000 to $0400
                                    ;   leaves PC BELOW the biased region
NI_S_FES    equ 6                   ; the cached fetch ES
NI_S_IRQ    equ 8                   ; bit 0 = the IRQ LEVEL the C maintains,
                                    ;   bit 1 = an NMI EDGE (the core clears
                                    ;   it when it takes it)
NI_S_BUS    equ 10                  ; the open-bus byte (SPEC.md 91.4.3)
NI_S_HALT   equ 11                  ; set by a KIL or by ni_halt()
NI_S_T      equ 20                  ; a word temporary
NI_S_T2     equ 22                  ; ...and a second
NI_S_T3     equ 26                  ; ...and a THIRD, for the read/write path
                                    ;   ALONE. (zp,X) holds half a pointer in
                                    ;   NI_S_T across the two reads that build
                                    ;   the other half, so the reads may not
                                    ;   use that word - which they did in the
                                    ;   first draft, and the address it built
                                    ;   was the low byte twice
NI_S_CSAV   equ 24                  ; C, saved across an op that must not
                                    ;   touch it and cannot spare BX
NI_S_PGC    equ 25                  ; ...and the indexed PAGE CARRY, saved
                                    ;   across the dummy access on the three
                                    ;   write modes. A SEPARATE byte from the
                                    ;   one above, because a read-modify-write
                                    ;   spends both - the EA routine's page
                                    ;   carry outlives the read that follows
                                    ;   it, and the operation body then wants
                                    ;   CSAV for the 6502's own C
NI_S_PRG    equ 0x80                ; THE FOUR $8000-$FFFF WINDOW SEGMENTS,
NI_S_PRGSTR equ 0x20                ;   at a stride of 32 bytes rather than 2.
                                    ;   THE STRIDE IS THE POINT: the window
                                    ;   index is bits 14-13 of the address,
                                    ;   which `and bh,0x60` produces DIRECTLY
                                    ;   as 0x00/0x20/0x40/0x60 - and a shift
                                    ;   by four would need CL, which is X and
                                    ;   is never free on the read path. 98
                                    ;   bytes of a 256-byte scratch buys the
                                    ;   hottest four instructions in the core

; =============================================================================
; MACROS
; =============================================================================

; --- the stack page: DI is 0x0100 + S throughout ---------------------------
%macro NI_PUSHB 1
    mov [di], %1
    dec di
    or  di, 0x0100
%endmacro

%macro NI_POPB 1
    inc di
    and di, 0x00FF                  ; ...AND THEN THE PAGE BACK ON, in two
    or  di, 0x0100                  ; instructions. See the header.
    mov %1, [di]
%endmacro

; --- carrying C across an op that does not touch it ------------------------
; Two spellings, because the cheap one needs BX and a read-modify-write is
; holding the effective address in it.
%macro NI_SAVEC_BL 0
    mov bl, ah
    and bl, 0x01
%endmacro
%macro NI_NZ_BL 0
    lahf
    and ah, 0xC0
    or  ah, bl
%endmacro
%macro NI_SAVEC_M 0
    mov [NI_SCR + NI_S_CSAV], ah
    and byte [NI_SCR + NI_S_CSAV], 0x01
%endmacro
%macro NI_NZ_M 0
    lahf
    and ah, 0xC0
    or  ah, [NI_SCR + NI_S_CSAV]
%endmacro

; --- V, out of the host's OF -----------------------------------------------
; `lahf` does not touch OF and does not read it either, so the overflow is
; branched on BEFORE the mask that would clear it.
%macro NI_V_FROM_OF 0
    jno %%nov
    or  ch, 0x40
    jmp short %%vd
%%nov:
    and ch, 0xBF
%%vd:
%endmacro

; --- the operand, per addressing mode --------------------------------------
%macro NI_OPR 1
  %ifidni %1, imm
    call ni_fetch
  %else
    call ni_ea_%1
    call ni_rd_bx
  %endif
%endmacro

; --- a READ op: one handler per (operation, mode) --------------------------
%macro NI_RD 2
o_%1_%2:
    NI_OPR %2
    NI_B_%1
    jmp ni_next
%endmacro

; --- a STORE op ------------------------------------------------------------
%macro NI_ST 3
o_%1_%2:
    call ni_ea_%2
    mov dh, %3
    call ni_wr_bx
    jmp ni_next
%endmacro

%macro NI_SAX 1
o_sax_%1:
    call ni_ea_%1
    mov dh, al
    and dh, cl                      ; A AND X, and no flag moves
    call ni_wr_bx
    jmp ni_next
%endmacro

; --- a READ-MODIFY-WRITE op ------------------------------------------------
; THE DUMMY ACCESS IS HERE (SPEC.md 91.4.3): a 6502 read-modify-write puts the
; UNMODIFIED byte back before it writes the modified one, and on a register
; address that second access is observable. One compare on a path that already
; costs five to eight cycles, and RAM and ROM addresses skip it entirely.
%macro NI_RMW 2
o_%1_%2:
    call ni_ea_%2
    call ni_rd_bx
    call ni_dummy_bx
    NI_M_%1
    call ni_wr_bx
    jmp ni_next
%endmacro

; --- an UNSTABLE illegal ---------------------------------------------------
; AHX, TAS, SHY, SHX and XAA depend on an unstable bus interaction that
; differs between chips, between temperatures and between the two halves of
; one instruction. THE ADDRESSING AND THE CYCLES ARE EXACT AND THE EFFECT IS
; NOT MODELLED, deliberately: nestest does not test them, no NES title uses
; one, and inventing a value would be inventing a fact (SPEC.md 47's posture,
; one level down). If a title ever needs one, it gets a real handler and a
; row in nicputest - not a guess added quietly here.
%macro NI_UNS 2
o_%1_%2:
  %ifidni %2, imm
    call ni_fetch
  %else
    call ni_ea_%2
  %endif
    jmp ni_next
%endmacro

; =============================================================================
; THE OPERATION BODIES - each acts on DH (the operand) and AL (A)
; =============================================================================

%macro NI_B_ora 0
    NI_SAVEC_BL
    or  al, dh
    NI_NZ_BL
%endmacro

%macro NI_B_and 0
    NI_SAVEC_BL
    and al, dh
    NI_NZ_BL
%endmacro

%macro NI_B_eor 0
    NI_SAVEC_BL
    xor al, dh
    NI_NZ_BL
%endmacro

; ADC and SBC NEVER LOOK AT D (SPEC.md 91.4.3): a 2A03 has no decimal adjust.
%macro NI_B_adc 0
    shr ah, 1                       ; CF = C; AH is rebuilt below
    adc al, dh
    lahf
    NI_V_FROM_OF
    and ah, 0xC1
%endmacro

; SBC is A - M - (1-C): the borrow in is NOT C, and the carry out is NOT the
; borrow - which is two `cmc`-shaped inversions and no table.
%macro NI_B_sbc 0
    shr ah, 1
    cmc
    sbb al, dh
    lahf
    NI_V_FROM_OF
    and ah, 0xC1
    xor ah, 0x01
%endmacro

%macro NI_B_cmp 0
    cmp al, dh
    lahf
    and ah, 0xC1
    xor ah, 0x01
%endmacro

%macro NI_B_cpx 0
    cmp cl, dh
    lahf
    and ah, 0xC1
    xor ah, 0x01
%endmacro

%macro NI_B_cpy 0
    cmp dl, dh
    lahf
    and ah, 0xC1
    xor ah, 0x01
%endmacro

%macro NI_B_lda 0
    NI_SAVEC_BL
    or  dh, dh
    mov al, dh                      ; `mov` moves no flag, so the lahf below
    NI_NZ_BL                        ; still reads the `or` above
%endmacro

%macro NI_B_ldx 0
    NI_SAVEC_BL
    or  dh, dh
    mov cl, dh
    NI_NZ_BL
%endmacro

%macro NI_B_ldy 0
    NI_SAVEC_BL
    or  dh, dh
    mov dl, dh
    NI_NZ_BL
%endmacro

; LAX is LDA and LDX at once - the one illegal nestest leans on hardest.
%macro NI_B_lax 0
    NI_SAVEC_BL
    or  dh, dh
    mov al, dh
    mov cl, dh
    NI_NZ_BL
%endmacro

; BIT: Z from A AND M, N from M bit 7, V from M bit 6, C untouched.
%macro NI_B_bit 0
    NI_SAVEC_BL
    test al, dh
    lahf
    and ah, 0x40
    or  ah, bl
    test dh, 0x80
    jz %%nn
    or  ah, 0x80
%%nn:
    test dh, 0x40
    jz %%nv
    or  ch, 0x40
    jmp short %%vd
%%nv:
    and ch, 0xBF
%%vd:
%endmacro

%macro NI_B_nopr 0
                                    ; the read HAPPENED and that is the whole
                                    ; of a NOP with an addressing mode: it can
                                    ; land on $2007 and that is what
                                    ; cpu_dummy_reads is about
%endmacro

%macro NI_B_nopi 0
%endmacro

; LAS: A = X = S = M AND S. Stable enough to model, and cheap here because S
; is already a full address in DI.
%macro NI_B_las 0
    NI_SAVEC_BL
    mov [NI_SCR + NI_S_T2], di
    and dh, [NI_SCR + NI_S_T2]
    or  dh, dh
    mov al, dh
    mov cl, dh
    mov [NI_SCR + NI_S_T2], dh
    mov byte [NI_SCR + NI_S_T2 + 1], 1
    mov di, [NI_SCR + NI_S_T2]
    NI_NZ_BL
%endmacro

; --- the read-modify-write bodies: act on DH -------------------------------
; -----------------------------------------------------------------------------
; A ROTATE SETS NEITHER N NOR Z, AND A SHIFT SETS BOTH
; -----------------------------------------------------------------------------
; `rcl` and `rcr` touch CF and OF and NOTHING ELSE on an 8086, where `shl` and
; `shr` set SF, ZF and CF together. So the four PURE rotates cannot take their
; N and Z from a `lahf` after the rotate - they would be the flags of whatever
; ran before - and they take them from an `or` of the result instead, with the
; rotate's own carry saved across it.
;
; This was caught by nestest at line 1,020 of 8,991: `ROR A` with A = $01 and
; C = 1 must leave $80 with N set, and the first version reported N = 0, Z = 1
; - the flags of the instruction BEFORE it. Every one of the 1,019 lines above
; it matched, which is exactly why the log is the gate a 6502 is built
; against.
; -----------------------------------------------------------------------------

%macro NI_M_asl 0
    shl dh, 1
    lahf
    and ah, 0xC1
%endmacro

%macro NI_M_lsr 0
    shr dh, 1
    lahf
    and ah, 0xC1
%endmacro

%macro NI_M_rol 0
    shr ah, 1                       ; CF = C
    rcl dh, 1                       ; ...and CF is now the bit rotated OUT
    mov byte [NI_SCR + NI_S_CSAV], 0
    rcl byte [NI_SCR + NI_S_CSAV], 1    ; ...saved as a 0 or a 1
    or  dh, dh                      ; N and Z come from the RESULT, because a
    lahf                            ; rotate did not set them
    and ah, 0xC0
    or  ah, [NI_SCR + NI_S_CSAV]
%endmacro

%macro NI_M_ror 0
    shr ah, 1
    rcr dh, 1
    mov byte [NI_SCR + NI_S_CSAV], 0
    rcl byte [NI_SCR + NI_S_CSAV], 1
    or  dh, dh
    lahf
    and ah, 0xC0
    or  ah, [NI_SCR + NI_S_CSAV]
%endmacro

%macro NI_M_inc 0
    NI_SAVEC_M
    inc dh
    NI_NZ_M
%endmacro

%macro NI_M_dec 0
    NI_SAVEC_M
    dec dh
    NI_NZ_M
%endmacro

; SLO/RLA/SRE: the shift's C survives and the logical op's N and Z replace it.
%macro NI_M_slo 0
    shl dh, 1
    lahf
    and ah, 0x01
    mov [NI_SCR + NI_S_CSAV], ah
    or  al, dh
    NI_NZ_M
%endmacro

%macro NI_M_rla 0
    shr ah, 1
    rcl dh, 1
    lahf
    and ah, 0x01
    mov [NI_SCR + NI_S_CSAV], ah
    and al, dh
    NI_NZ_M
%endmacro

%macro NI_M_sre 0
    shr dh, 1
    lahf
    and ah, 0x01
    mov [NI_SCR + NI_S_CSAV], ah
    xor al, dh
    NI_NZ_M
%endmacro

; RRA is ROR then ADC, and the ADC's carry IN is the one the rotate produced.
%macro NI_M_rra 0
    shr ah, 1
    rcr dh, 1
    lahf
    and ah, 0x01
    shr ah, 1                       ; ...straight back into CF
    adc al, dh
    lahf
    NI_V_FROM_OF
    and ah, 0xC1
%endmacro

%macro NI_M_dcp 0
    dec dh
    cmp al, dh
    lahf
    and ah, 0xC1
    xor ah, 0x01
%endmacro

%macro NI_M_isb 0
    inc dh                          ; `inc` moves no carry, so C survives it
    shr ah, 1
    cmc
    sbb al, dh
    lahf
    NI_V_FROM_OF
    and ah, 0xC1
    xor ah, 0x01
%endmacro

; =============================================================================
; int ni_run(int cycles)
;
; Run for at most `cycles` 2A03 cycles and answer NI_RUN_*. ni_m.cnt is left
; holding what was NOT spent - negative by up to one instruction's cost - so
; the caller's `ran = cycles - cnt` is exact.
; =============================================================================
_ni_run:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    mov ax, [bp+4]
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    mov [NI_SCR + NI_S_DEAD], ax
    mov word [NI_SCR + NI_S_BLO], 0
    mov word [NI_SCR + NI_S_BOUND], 0   ; the first fetch re-biases
    mov byte [NI_SCR + NI_S_HALT], 0
    mov al, [cs:_ni_m + NIM_A]
    mov bl, [cs:_ni_m + NIM_P]
    call ni_setp                    ; -> AH (N,Z,C) and CH (V,D,I)
    mov cl, [cs:_ni_m + NIM_X]
    mov dl, [cs:_ni_m + NIM_Y]
    mov si, [cs:_ni_m + NIM_PC]
    mov di, [cs:_ni_m + NIM_S]
    and di, 0x00FF
    or  di, 0x0100
    jmp ni_int_chk                  ; an interrupt pending at the top of a run
                                    ; is taken before the first instruction

; -----------------------------------------------------------------------------
; THE FETCH AND THE DISPATCH
; -----------------------------------------------------------------------------
ni_next:
    cmp word [NI_SCR + NI_S_DEAD], 0
    jle ni_expire
    cmp si, [NI_SCR + NI_S_BOUND]
    jae .slow
    cmp si, [NI_SCR + NI_S_BLO]
    jb  .slow
    mov bl, [es:si]
    inc si
.disp:
    xor bh, bh
    mov dh, [cs:bx + ni_cyc]
    sub [NI_SCR + NI_S_DEAD], dh
    sbb byte [NI_SCR + NI_S_DEAD + 1], 0
    shl bx, 1
    jmp [cs:bx + ni_tab]
.slow:
    call ni_rebias
    mov bl, dh
    jmp short .disp

; ni_fetch - one operand byte, through the same guarded path.
ni_fetch:
    cmp si, [NI_SCR + NI_S_BOUND]
    jae ni_rebias
    cmp si, [NI_SCR + NI_S_BLO]
    jb  ni_rebias
    mov dh, [es:si]
    inc si
    ret

; -----------------------------------------------------------------------------
; ni_rebias - PC has left the region ES is biased for (or a mapper moved it).
; Work out the region, set ES, BLO and BOUND, and DELIVER THE BYTE in DH with
; SI advanced, so that it can be jumped to from ni_fetch's tail.
;
; A fetch from $2000-$5FFF is the TRUE SLOW FETCH PATH: a biased ES physically
; cannot read a register file, so the byte comes through the same call out the
; data path uses and the region is left marked "always re-bias".
; -----------------------------------------------------------------------------
ni_rebias:
    push ax
    push bx
    cmp si, 0x0800
    jb  .ram
    cmp si, 0x6000
    jb  .io
    cmp si, 0x8000
    jb  .sram
    ; --- $8000-$FFFF: one of the four PRG windows ---------------------------
    ; NO SHIFT BY A COUNT ANYWHERE, because a variable shift needs CL and CL
    ; is X. `and bh,0x60` is the window index already multiplied by the
    ; window table's stride, and four `shl bx,1` turn that into the same
    ; window's paragraph offset - which is why the stride is 32 (NI_S_PRGSTR).
    mov bx, si
    and bx, 0x6000                  ; 0x0000/0x2000/0x4000/0x6000
    mov ax, bx
    or  ax, 0x8000
    mov [NI_SCR + NI_S_BLO], ax     ; the window's base...
    add ax, 0x2000
    jnz .bok
    mov ax, 0xFFFF                  ; ...and its bound. The TOP window's bound
.bok:                               ; is $FFFF and not the wrap, so the last
    mov [NI_SCR + NI_S_BOUND], ax   ; byte of the address space re-biases and
                                    ; the wrap to $0000 cannot be fetched
                                    ; through a PRG bias
    mov bl, bh
    xor bh, bh                      ; BX = the index x 0x20, the table stride
    mov ax, [bx + NI_SCR + NI_S_PRG]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; ...and now x 0x200: the window's own
    add bx, 0x800                   ; paragraph offset, plus $8000 >> 4
    sub ax, bx
    mov es, ax
    jmp short .go
.ram:
    mov word [NI_SCR + NI_S_BLO], 0
    mov word [NI_SCR + NI_S_BOUND], 0x0800
    mov ax, [cs:_ni_m + NIM_MACHSEG]
    mov es, ax                      ; NI_O_RAM is 0, so there is no bias
    jmp short .go
.sram:
    mov word [NI_SCR + NI_S_BLO], 0x6000
    mov word [NI_SCR + NI_S_BOUND], 0x8000
    mov ax, [cs:_ni_m + NIM_MACHSEG]
    add ax, NI_O_SRAM >> 4
    sub ax, 0x6000 >> 4             ; THE BIAS CAN UNDERFLOW IN PRINCIPLE and
    mov es, ax                      ; never does: 0x600 paragraphs is 24KB and
                                    ; the heap starts above the kernel's ~111KB
                                    ; footprint (docs/KERNEL-MEMORY.md)
.go:
    mov [NI_SCR + NI_S_FES], es
    mov dh, [es:si]
    inc si
    pop bx
    pop ax
    ret
.io:
    ; every fetch from here re-enters this routine, which is what a fetch out
    ; of a register file costs and what it is meant to cost
    mov word [NI_SCR + NI_S_BLO], 0
    mov word [NI_SCR + NI_S_BOUND], 0
    mov bx, si
    call ni_io_rd_bx
    inc si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE DATA PATH. BX = the address; the byte travels in DH; BX comes back
; UNCHANGED, because a read-modify-write writes to the address it just read.
;
; Three compares and everything else is a call out (SPEC.md 91.4.3's table).
; -----------------------------------------------------------------------------
ni_rd_bx:
    cmp bh, 0x20
    jb  .ram
    cmp bh, 0x60
    jb  ni_io_rd_bx
    cmp bh, 0x80
    jb  .sram
    ; --- $8000-$FFFF: PRG, through the window table -------------------------
    push ax
    mov [NI_SCR + NI_S_T3], bx
    mov ax, bx
    mov al, ah
    and al, 0x60                    ; the window index AT ITS STRIDE, with no
    xor ah, ah                      ; shift at all (see NI_S_PRGSTR)
    mov bx, ax
    mov es, [bx + NI_SCR + NI_S_PRG]
    mov bx, [NI_SCR + NI_S_T3]
    and bh, 0x1F
    mov dh, [es:bx]
    mov bx, [NI_SCR + NI_S_T3]
    mov es, [NI_SCR + NI_S_FES]     ; the FETCH bias back: this routine
    pop ax                          ; borrowed ES and the next fetch needs it
    ret
.ram:
    mov [NI_SCR + NI_S_T3], bx
    and bh, 0x07                    ; $0800-$1FFF mirrors the 2KB
    mov dh, [bx]
    mov bx, [NI_SCR + NI_S_T3]
    ret
.sram:
    mov dh, [bx + NI_SRAM_DISP]
    ret

ni_wr_bx:
    cmp bh, 0x20
    jb  .ram
    cmp bh, 0x60
    jb  ni_io_wr_bx
    cmp bh, 0x80
    jb  .sram
    jmp ni_io_wr_bx                 ; $8000 and above is the MAPPER
.ram:
    mov [NI_SCR + NI_S_T3], bx
    and bh, 0x07
    mov [bx], dh
    mov bx, [NI_SCR + NI_S_T3]
    ret
.sram:
    mov [bx + NI_SRAM_DISP], dh
    ret

; -----------------------------------------------------------------------------
; ni_dummy_bx - the extra access a page-cross penalty and a read-modify-write
; both perform (SPEC.md 91.4.3). It only MATTERS on a register address, so RAM
; and ROM skip it in two compares and pay nothing.
; -----------------------------------------------------------------------------
ni_dummy_bx:
    cmp bh, 0x20
    jb  .no
    cmp bh, 0x40
    jb  .yes
    cmp bx, 0x4020
    jae .no
.yes:
    push dx
    call ni_io_rd_bx
    pop dx
.no:
    ret

; -----------------------------------------------------------------------------
; THE CALL OUT (SPEC.md 91.4.3). It happens INSIDE an instruction, so every
; live register of the core is saved, DS is swapped to the package and put
; back, and the fetch bias is reloaded - ES is caller-clobbered by the C ABI,
; so it is RELOADED and not merely trusted.
; -----------------------------------------------------------------------------
ni_io_rd_bx:                        ; BX = address -> DH; BX preserved
    push si
    push di
    push cx
    push ax
    push dx
    push bx
    push bx                         ; the cdecl argument
    mov ax, cs
    mov ds, ax
    call _ni_io_rd
    add sp, 2
    mov cl, al                      ; CX is on the stack, so CL is free
    pop bx
    pop dx
    pop ax
    mov dh, cl
    pop cx
    pop di
    pop si
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    mov [NI_SCR + NI_S_BUS], dh     ; the open bus is what the SLOW PATH last
                                    ; put on it (SPEC.md 91.4.3's stated
                                    ; approximation)
; ni_io_rd_esfix / ni_io_wr_esfix - the two instructions that put the fetch
; segment back, LABELLED because nicputest needs a DELIBERATELY STALE ES and
; the only honest way to make one is to NOP out the reload in the shipping
; text at runtime. Nothing in the package reaches these names.
ni_io_rd_esfix:
    mov es, [NI_SCR + NI_S_FES]
    ret

ni_io_wr_bx:                        ; BX = address, DH = value; both preserved
    push si
    push di
    push cx
    push ax
    push dx
    push bx
    mov al, dh
    xor ah, ah
    push ax                         ; the value
    push bx                         ; the address
    mov ax, cs
    mov ds, ax
    call _ni_io_wr
    add sp, 4
    or  ax, ax                      ; THE RE-BIAS FLAG: non-zero means the PRG
    pop bx                          ; mapping moved. `pop` and `mov` move no
    pop dx                          ; flag, so the test survives the unwind
    pop ax
    pop cx
    pop di
    pop si
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    jz  ni_io_wr_esfix
    mov word [NI_SCR + NI_S_BOUND], 0   ; ...so the next fetch re-biases even
    mov word [NI_SCR + NI_S_BLO], 0     ; though PC never left the region
ni_io_wr_esfix:
    mov es, [NI_SCR + NI_S_FES]
    ret

; -----------------------------------------------------------------------------
; P, PACKED AND UNPACKED ONCE PER CALL (SPEC.md 91.4.1)
; -----------------------------------------------------------------------------
; ni_setp: BL = a real 6502 P -> AH (lahf layout) and CH (V, D, I). AL is not
; touched, which is what lets the entry shell load A first.
ni_setp:
    mov ah, bl
    and ah, 0x80                    ; N -> SF
    test bl, 0x02
    jz  .nz
    or  ah, 0x40                    ; Z -> ZF
.nz:
    test bl, 0x01
    jz  .nc
    or  ah, 0x01                    ; C -> CF
.nc:
    mov ch, bl
    and ch, 0x4C                    ; V | D | I, at their 6502 positions
    ret

; ni_getp: -> BL = a real 6502 P byte, bit 5 SET and bit 4 CLEAR. It clobbers
; DH, which every one of its callers has already spent.
ni_getp:
    mov bl, ah
    and bl, 0x80
    test ah, 0x40
    jz  .nz
    or  bl, 0x02
.nz:
    test ah, 0x01
    jz  .nc
    or  bl, 0x01
.nc:
    mov dh, ch
    and dh, 0x4C
    or  bl, dh
    or  bl, 0x20                    ; bit 5 always reads set
    ret

; -----------------------------------------------------------------------------
; THE ADDRESSING MODES. Each leaves the effective address in BX.
;
; The `_w` forms are the STORE and READ-MODIFY-WRITE variants: their extra
; cycle is already in the table, so they add none - but they still perform the
; dummy read at the UN-CARRIED address, which is exactly what
; `cpu_dummy_reads` tests with `STA $2000,X`.
; -----------------------------------------------------------------------------
ni_ea_zp:
    call ni_fetch
    mov bl, dh
    xor bh, bh
    ret

ni_ea_zpx:
    call ni_fetch
    mov bl, dh
    add bl, cl                      ; the zero page WRAPS: one byte add
    xor bh, bh
    ret

ni_ea_zpy:
    call ni_fetch
    mov bl, dh
    add bl, dl
    xor bh, bh
    ret

ni_ea_abs:
    call ni_fetch
    mov [NI_SCR + NI_S_T], dh
    call ni_fetch
    mov [NI_SCR + NI_S_T + 1], dh
    mov bx, [NI_SCR + NI_S_T]
    ret

ni_ea_abx:
    call ni_ea_abs
    add bl, cl
    jnc .done
    call ni_dummy_bx                ; the dummy read is at the address BEFORE
    inc bh                          ; the carry propagates - which is the one
    dec word [NI_SCR + NI_S_DEAD]   ; the hardware actually puts on the bus
.done:
    ret

; ALWAYS the dummy access, cross or no cross: a store and a read-modify-write
; both spend that cycle, and it is what `cpu_dummy_reads` tests with
; `STA $2000,X`.
ni_ea_abxw:
    call ni_ea_abs
    add bl, cl
    mov byte [NI_SCR + NI_S_PGC], 0     ; THE PAGE CARRY IS SAVED BEFORE THE
    rcl byte [NI_SCR + NI_S_PGC], 1     ; DUMMY ACCESS, because a `call` and
    call ni_dummy_bx                    ; every compare inside it destroy CF -
    cmp byte [NI_SCR + NI_S_PGC], 0     ; which made `STA ($33),Y` store one
    je  .done                           ; page HIGH, at nestest line 3,321
    inc bh
.done:
    ret

ni_ea_aby:
    call ni_ea_abs
    add bl, dl
    jnc .done
    call ni_dummy_bx
    inc bh
    dec word [NI_SCR + NI_S_DEAD]
.done:
    ret

ni_ea_abyw:
    call ni_ea_abs
    add bl, dl
    mov byte [NI_SCR + NI_S_PGC], 0     ; THE PAGE CARRY IS SAVED BEFORE THE
    rcl byte [NI_SCR + NI_S_PGC], 1     ; DUMMY ACCESS, because a `call` and
    call ni_dummy_bx                    ; every compare inside it destroy CF -
    cmp byte [NI_SCR + NI_S_PGC], 0     ; which made `STA ($33),Y` store one
    je  .done                           ; page HIGH, at nestest line 3,321
    inc bh
.done:
    ret

; (zp,X): the pointer is formed in the zero page and WRAPS there, both bytes.
ni_ea_izx:
    call ni_fetch
    mov bl, dh
    add bl, cl
    xor bh, bh
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bl                          ; the wrap again, and only the low byte
    xor bh, bh
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov bx, [NI_SCR + NI_S_T]
    ret

; (zp),Y: the pointer wraps in the zero page, the +Y does not.
ni_ea_izy_base:
    call ni_fetch
    mov bl, dh
    xor bh, bh
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bl
    xor bh, bh
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov bx, [NI_SCR + NI_S_T]
    ret

ni_ea_izy:
    call ni_ea_izy_base
    add bl, dl
    jnc .done
    call ni_dummy_bx
    inc bh
    dec word [NI_SCR + NI_S_DEAD]
.done:
    ret

ni_ea_izyw:
    call ni_ea_izy_base
    add bl, dl
    mov byte [NI_SCR + NI_S_PGC], 0     ; THE PAGE CARRY IS SAVED BEFORE THE
    rcl byte [NI_SCR + NI_S_PGC], 1     ; DUMMY ACCESS, because a `call` and
    call ni_dummy_bx                    ; every compare inside it destroy CF -
    cmp byte [NI_SCR + NI_S_PGC], 0     ; which made `STA ($33),Y` store one
    je  .done                           ; page HIGH, at nestest line 3,321
    inc bh
.done:
    ret

; =============================================================================
; THE HANDLERS
; =============================================================================

; --- the accumulator shifts ------------------------------------------------
o_asla:
    shl al, 1
    lahf
    and ah, 0xC1
    jmp ni_next

o_lsra:
    shr al, 1
    lahf
    and ah, 0xC1
    jmp ni_next

o_rola:
    shr ah, 1
    rcl al, 1
    mov dh, 0                       ; DH is free here, where a read-modify-
    rcl dh, 1                       ; write is holding the address in BX
    or  al, al
    lahf
    and ah, 0xC0
    or  ah, dh
    jmp ni_next

o_rora:
    shr ah, 1
    rcr al, 1
    mov dh, 0
    rcl dh, 1
    or  al, al
    lahf
    and ah, 0xC0
    or  ah, dh
    jmp ni_next

; --- the flag instructions -------------------------------------------------
o_clc:
    and ah, 0xFE
    jmp ni_next
o_sec:
    or  ah, 0x01
    jmp ni_next
o_clv:
    and ch, 0xBF
    jmp ni_next
o_cld:
    and ch, 0xF7                    ; D IS STATE AND IS CARRIED (SPEC.md
    jmp ni_next                     ; 91.4.3): nestest's P column reads it,
o_sed:                              ; and no arithmetic here ever looks at it
    or  ch, 0x08
    jmp ni_next
o_sei:
    or  ch, 0x04
    jmp ni_next
o_cli:
    and ch, 0xFB
    jmp ni_int_chk                  ; ...and an IRQ that was masked a moment
                                    ; ago is taken NOW

; --- the register moves ----------------------------------------------------
o_tax:
    NI_SAVEC_BL
    or  al, al
    mov cl, al
    NI_NZ_BL
    jmp ni_next
o_txa:
    NI_SAVEC_BL
    or  cl, cl
    mov al, cl
    NI_NZ_BL
    jmp ni_next
o_tay:
    NI_SAVEC_BL
    or  al, al
    mov dl, al
    NI_NZ_BL
    jmp ni_next
o_tya:
    NI_SAVEC_BL
    or  dl, dl
    mov al, dl
    NI_NZ_BL
    jmp ni_next
o_tsx:
    NI_SAVEC_M                      ; BX is wanted for S, so C goes to memory
    mov bx, di
    or  bl, bl
    mov cl, bl
    NI_NZ_M
    jmp ni_next
o_txs:
    mov bl, cl                      ; TXS MOVES NO FLAG - the one transfer
    mov bh, 0x01                    ; that does not
    mov di, bx
    jmp ni_next
o_dex:
    NI_SAVEC_BL
    dec cl
    NI_NZ_BL
    jmp ni_next
o_inx:
    NI_SAVEC_BL
    inc cl
    NI_NZ_BL
    jmp ni_next
o_dey:
    NI_SAVEC_BL
    dec dl
    NI_NZ_BL
    jmp ni_next
o_iny:
    NI_SAVEC_BL
    inc dl
    NI_NZ_BL
    jmp ni_next
o_nop:
    jmp ni_next

; --- the stack -------------------------------------------------------------
o_pha:
    NI_PUSHB al
    jmp ni_next
o_php:
    call ni_getp
    or  bl, 0x10                    ; B is not a register bit; PHP SUPPLIES it
    NI_PUSHB bl
    jmp ni_next
o_pla:
    NI_SAVEC_BL
    NI_POPB dh
    or  dh, dh
    mov al, dh
    NI_NZ_BL
    jmp ni_next
o_plp:
    NI_POPB bl
    call ni_setp                    ; bits 4 and 5 are ignored on the way in
    jmp ni_int_chk                  ; I may have just been cleared

; --- the jumps -------------------------------------------------------------
o_jmp_abs:
    call ni_ea_abs
    mov si, bx
    jmp ni_next

; JMP () WITH THE PAGE-WRAP BUG: the high byte comes from
; (ptr & 0xFF00) | ((ptr + 1) & 0xFF), which `inc bl` says exactly.
o_jmpi_ind:
    call ni_ea_abs
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bl
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov si, [NI_SCR + NI_S_T]
    jmp ni_next

o_jsr_abs:
    call ni_ea_abs                  ; BX = the target, SI = the next opcode
    dec si                          ; ...and what is pushed is the LAST byte
    mov [NI_SCR + NI_S_T], si       ; of the JSR, which RTS then increments
    mov dh, [NI_SCR + NI_S_T + 1]
    NI_PUSHB dh
    mov dh, [NI_SCR + NI_S_T]
    NI_PUSHB dh
    mov si, bx
    jmp ni_next

o_rts:
    NI_POPB dh
    mov [NI_SCR + NI_S_T], dh
    NI_POPB dh
    mov [NI_SCR + NI_S_T + 1], dh
    mov si, [NI_SCR + NI_S_T]
    inc si
    jmp ni_next

o_rti:
    NI_POPB bl
    call ni_setp
    NI_POPB dh
    mov [NI_SCR + NI_S_T], dh
    NI_POPB dh
    mov [NI_SCR + NI_S_T + 1], dh
    mov si, [NI_SCR + NI_S_T]       ; ...and NO increment: RTI pulls the real
    jmp ni_int_chk                  ; return address

o_brk:
    inc si                          ; BRK is two bytes and the second is
    mov [NI_SCR + NI_S_T], si       ; ignored
    mov dh, [NI_SCR + NI_S_T + 1]
    NI_PUSHB dh
    mov dh, [NI_SCR + NI_S_T]
    NI_PUSHB dh
    call ni_getp
    or  bl, 0x10                    ; B set, as PHP does
    NI_PUSHB bl
    or  ch, 0x04                    ; I
    mov bx, 0xFFFE
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bx
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov si, [NI_SCR + NI_S_T]
    jmp ni_next

; --- KIL: the machine stops, and PC points AT the opcode that stopped it ---
o_kil:
    dec si
    mov byte [NI_SCR + NI_S_HALT], 1
    mov word [NI_SCR + NI_S_DEAD], 0
    jmp ni_expire

; --- the branches ----------------------------------------------------------
; Eight two-instruction heads over one tail. A taken branch costs one more
; cycle and a taken branch that crosses a page costs two.
o_bpl_rel:
    call ni_fetch
    test ah, 0x80
    jz  ni_br_take
    jmp ni_next
o_bmi_rel:
    call ni_fetch
    test ah, 0x80
    jnz ni_br_take
    jmp ni_next
o_bvc_rel:
    call ni_fetch
    test ch, 0x40
    jz  ni_br_take
    jmp ni_next
o_bvs_rel:
    call ni_fetch
    test ch, 0x40
    jnz ni_br_take
    jmp ni_next
o_bcc_rel:
    call ni_fetch
    test ah, 0x01
    jz  ni_br_take
    jmp ni_next
o_bcs_rel:
    call ni_fetch
    test ah, 0x01
    jnz ni_br_take
    jmp ni_next
o_bne_rel:
    call ni_fetch
    test ah, 0x40
    jz  ni_br_take
    jmp ni_next
o_beq_rel:
    call ni_fetch
    test ah, 0x40
    jnz ni_br_take
    jmp ni_next

ni_br_take:
    dec word [NI_SCR + NI_S_DEAD]   ; taken
    mov bl, dh
    xor bh, bh
    cmp bl, 0x80
    jb  .pos
    dec bh                          ; sign-extend the offset
.pos:
    mov [NI_SCR + NI_S_T], si
    add bx, si
    mov dh, bh
    xor dh, [NI_SCR + NI_S_T + 1]   ; did the page move?
    jz  .same
    dec word [NI_SCR + NI_S_DEAD]
.same:
    mov si, bx
    jmp ni_next

; --- SHY and SHX: the two "unstable" stores that are not ------------------
; The address is the ABSOLUTE one plus the index; the value stored is the
; other index register ANDed with the absolute address's HIGH BYTE PLUS ONE;
; and when the index carries into a new page, the address's high byte becomes
; THE VALUE rather than incrementing. That last clause is the whole of their
; reputation, and it is consistent enough that blargg's `07-abs_xy` tests both
; by name - which is why they have real handlers here where AHX, TAS and XAA
; still do not (NI_UNS's header says what would change that).
o_shy_abxw:
    call ni_ea_abs
    mov dh, bh
    inc dh
    and dh, dl                      ; Y AND (high + 1)
    add bl, cl                      ; + X
    jnc .nox
    mov bh, dh                      ; the page becomes the VALUE
.nox:
    call ni_wr_bx
    jmp ni_next

o_shx_abyw:
    call ni_ea_abs
    mov dh, bh
    inc dh
    and dh, cl                      ; X AND (high + 1)
    add bl, dl                      ; + Y
    jnc .nox
    mov bh, dh
.nox:
    call ni_wr_bx
    jmp ni_next

; --- the illegal immediates that ARE stable --------------------------------
; ANC: AND, then C is a copy of the result's bit 7.
o_anc_imm:
    call ni_fetch
    and al, dh
    lahf
    and ah, 0xC0
    test ah, 0x80
    jz  .nc
    or  ah, 0x01
.nc:
    jmp ni_next

; ALR: AND, then LSR A.
o_alr_imm:
    call ni_fetch
    and al, dh
    shr al, 1
    lahf
    and ah, 0xC1
    jmp ni_next

; ARR: AND, then ROR A - with the documented odd flags, C from bit 6 of the
; result and V from bit 6 XOR bit 5.
o_arr_imm:
    call ni_fetch
    and al, dh
    shr ah, 1                       ; CF = C
    rcr al, 1
    or  al, al                      ; N and Z from the RESULT: a rotate sets
    lahf                            ; neither, and blargg's 03-immediate is
    and ah, 0xC0                    ; what said so
    test al, 0x40
    jz  .nc
    or  ah, 0x01                    ; C is the result's bit 6...
.nc:
    mov dh, al
    shl dh, 1
    xor dh, al
    test dh, 0x40
    jz  .nv
    or  ch, 0x40                    ; ...and V is bit 6 XOR bit 5
    jmp short .fin
.nv:
    and ch, 0xBF
.fin:
    jmp ni_next

; AXS: X = (A AND X) - imm, with C as a comparison's carry.
o_axs_imm:
    call ni_fetch
    mov bl, al
    and bl, cl
    sub bl, dh                      ; one `sub` sets N, Z AND the borrow, so
    lahf                            ; there is no separate compare to make
    and ah, 0xC1
    xor ah, 0x01                    ; C is NOT the borrow
    mov cl, bl
    jmp ni_next

; =============================================================================
; THE MACRO-GENERATED HANDLERS
;
; One line per (operation, addressing mode), and the 221 labels they define are
; what the dispatch table at the foot of this file names. THE TWO CANNOT
; DISAGREE BY A TYPO: apps/infones/build.sh expands these invocations, collects
; every literal `o_*:` label beside them, and refuses a build in which any of
; the table's 256 entries is not among them - or in which the cycle table is
; not 256 bytes long. A dispatch table is 512 bytes of hand-written label names
; and that check is what makes it safe to be one.
; =============================================================================
; --- the READ ops: the operand arrives in DH ------------------------
    NI_RD   ora  , abs
    NI_RD   ora  , abx
    NI_RD   ora  , aby
    NI_RD   ora  , imm
    NI_RD   ora  , izx
    NI_RD   ora  , izy
    NI_RD   ora  , zp
    NI_RD   ora  , zpx
    NI_RD   and  , abs
    NI_RD   and  , abx
    NI_RD   and  , aby
    NI_RD   and  , imm
    NI_RD   and  , izx
    NI_RD   and  , izy
    NI_RD   and  , zp
    NI_RD   and  , zpx
    NI_RD   eor  , abs
    NI_RD   eor  , abx
    NI_RD   eor  , aby
    NI_RD   eor  , imm
    NI_RD   eor  , izx
    NI_RD   eor  , izy
    NI_RD   eor  , zp
    NI_RD   eor  , zpx
    NI_RD   adc  , abs
    NI_RD   adc  , abx
    NI_RD   adc  , aby
    NI_RD   adc  , imm
    NI_RD   adc  , izx
    NI_RD   adc  , izy
    NI_RD   adc  , zp
    NI_RD   adc  , zpx
    NI_RD   sbc  , abs
    NI_RD   sbc  , abx
    NI_RD   sbc  , aby
    NI_RD   sbc  , imm
    NI_RD   sbc  , izx
    NI_RD   sbc  , izy
    NI_RD   sbc  , zp
    NI_RD   sbc  , zpx
    NI_RD   cmp  , abs
    NI_RD   cmp  , abx
    NI_RD   cmp  , aby
    NI_RD   cmp  , imm
    NI_RD   cmp  , izx
    NI_RD   cmp  , izy
    NI_RD   cmp  , zp
    NI_RD   cmp  , zpx
    NI_RD   cpx  , abs
    NI_RD   cpx  , imm
    NI_RD   cpx  , zp
    NI_RD   cpy  , abs
    NI_RD   cpy  , imm
    NI_RD   cpy  , zp
    NI_RD   lda  , abs
    NI_RD   lda  , abx
    NI_RD   lda  , aby
    NI_RD   lda  , imm
    NI_RD   lda  , izx
    NI_RD   lda  , izy
    NI_RD   lda  , zp
    NI_RD   lda  , zpx
    NI_RD   ldx  , abs
    NI_RD   ldx  , aby
    NI_RD   ldx  , imm
    NI_RD   ldx  , zp
    NI_RD   ldx  , zpy
    NI_RD   ldy  , abs
    NI_RD   ldy  , abx
    NI_RD   ldy  , imm
    NI_RD   ldy  , zp
    NI_RD   ldy  , zpx
    NI_RD   bit  , abs
    NI_RD   bit  , zp
    NI_RD   lax  , abs
    NI_RD   lax  , aby
    NI_RD   lax  , imm
    NI_RD   lax  , izx
    NI_RD   lax  , izy
    NI_RD   lax  , zp
    NI_RD   lax  , zpy
    NI_RD   nopr , abs
    NI_RD   nopr , abx
    NI_RD   nopr , zp
    NI_RD   nopr , zpx
    NI_RD   nopi , imm
    NI_RD   las  , aby

; --- the STORE ops -------------------------------------------------
    NI_ST   sta  , abs  , al
    NI_ST   sta  , abxw , al
    NI_ST   sta  , abyw , al
    NI_ST   sta  , izx  , al
    NI_ST   sta  , izyw , al
    NI_ST   sta  , zp   , al
    NI_ST   sta  , zpx  , al
    NI_ST   stx  , abs  , cl
    NI_ST   stx  , zp   , cl
    NI_ST   stx  , zpy  , cl
    NI_ST   sty  , abs  , dl
    NI_ST   sty  , zp   , dl
    NI_ST   sty  , zpx  , dl
    NI_SAX  abs
    NI_SAX  izx
    NI_SAX  zp
    NI_SAX  zpy

; --- the READ-MODIFY-WRITE ops -------------------------------------
    NI_RMW  asl  , abs
    NI_RMW  asl  , abxw
    NI_RMW  asl  , zp
    NI_RMW  asl  , zpx
    NI_RMW  lsr  , abs
    NI_RMW  lsr  , abxw
    NI_RMW  lsr  , zp
    NI_RMW  lsr  , zpx
    NI_RMW  rol  , abs
    NI_RMW  rol  , abxw
    NI_RMW  rol  , zp
    NI_RMW  rol  , zpx
    NI_RMW  ror  , abs
    NI_RMW  ror  , abxw
    NI_RMW  ror  , zp
    NI_RMW  ror  , zpx
    NI_RMW  inc  , abs
    NI_RMW  inc  , abxw
    NI_RMW  inc  , zp
    NI_RMW  inc  , zpx
    NI_RMW  dec  , abs
    NI_RMW  dec  , abxw
    NI_RMW  dec  , zp
    NI_RMW  dec  , zpx
    NI_RMW  slo  , abs
    NI_RMW  slo  , abxw
    NI_RMW  slo  , abyw
    NI_RMW  slo  , izx
    NI_RMW  slo  , izyw
    NI_RMW  slo  , zp
    NI_RMW  slo  , zpx
    NI_RMW  rla  , abs
    NI_RMW  rla  , abxw
    NI_RMW  rla  , abyw
    NI_RMW  rla  , izx
    NI_RMW  rla  , izyw
    NI_RMW  rla  , zp
    NI_RMW  rla  , zpx
    NI_RMW  sre  , abs
    NI_RMW  sre  , abxw
    NI_RMW  sre  , abyw
    NI_RMW  sre  , izx
    NI_RMW  sre  , izyw
    NI_RMW  sre  , zp
    NI_RMW  sre  , zpx
    NI_RMW  rra  , abs
    NI_RMW  rra  , abxw
    NI_RMW  rra  , abyw
    NI_RMW  rra  , izx
    NI_RMW  rra  , izyw
    NI_RMW  rra  , zp
    NI_RMW  rra  , zpx
    NI_RMW  dcp  , abs
    NI_RMW  dcp  , abxw
    NI_RMW  dcp  , abyw
    NI_RMW  dcp  , izx
    NI_RMW  dcp  , izyw
    NI_RMW  dcp  , zp
    NI_RMW  dcp  , zpx
    NI_RMW  isb  , abs
    NI_RMW  isb  , abxw
    NI_RMW  isb  , abyw
    NI_RMW  isb  , izx
    NI_RMW  isb  , izyw
    NI_RMW  isb  , zp
    NI_RMW  isb  , zpx

; --- the UNSTABLE illegals: addressed and timed, never modelled ----
    NI_UNS  ahx  , abyw
    NI_UNS  ahx  , izyw
    NI_UNS  tas  , abyw
    NI_UNS  xaa  , imm

; =============================================================================
; INTERRUPTS (SPEC.md 91.4.3)
; =============================================================================
; NMI is an EDGE - latched by the C, consumed here. IRQ is a LEVEL and is
; honoured only when I is clear. Both are checked BETWEEN instructions: at the
; top of a run, and after the three instructions that can unmask one.
ni_int_chk:
    test byte [NI_SCR + NI_S_IRQ], 0x02
    jnz ni_do_nmi
    test byte [NI_SCR + NI_S_IRQ], 0x01
    jz  ni_next
    test ch, 0x04
    jnz ni_next
    mov bx, 0xFFFE
    jmp short ni_int_enter

ni_do_nmi:
    and byte [NI_SCR + NI_S_IRQ], 0xFD  ; the edge is CONSUMED here and
    mov bx, 0xFFFA                      ; nowhere else - a $2002 read clears
                                        ; the vblank FLAG, never a latched
                                        ; edge
ni_int_enter:
    mov [NI_SCR + NI_S_T2], bx
    sub word [NI_SCR + NI_S_DEAD], 7    ; INT_CYCLES
    mov [NI_SCR + NI_S_T], si
    mov dh, [NI_SCR + NI_S_T + 1]
    NI_PUSHB dh
    mov dh, [NI_SCR + NI_S_T]
    NI_PUSHB dh
    call ni_getp                        ; B CLEAR on an interrupt push
    NI_PUSHB bl
    or  ch, 0x04
    mov bx, [NI_SCR + NI_S_T2]
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bx
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov si, [NI_SCR + NI_S_T]
    jmp ni_next

; =============================================================================
; THE EXIT
; =============================================================================
ni_expire:
    mov word [cs:_ni_m + NIM_REASON], NI_RUN_SLICE
    cmp byte [NI_SCR + NI_S_HALT], 0
    je  .out
    mov word [cs:_ni_m + NIM_REASON], NI_RUN_STOP
.out:
    mov [cs:_ni_m + NIM_PC], si
    mov bx, di
    and bx, 0x00FF
    mov [cs:_ni_m + NIM_S], bx
    xor bh, bh
    mov bl, cl
    mov [cs:_ni_m + NIM_X], bx
    mov bl, dl
    mov [cs:_ni_m + NIM_Y], bx
    call ni_getp                    ; -> BL, and it spends DH
    xor bh, bh
    mov [cs:_ni_m + NIM_P], bx
    mov bl, al
    mov [cs:_ni_m + NIM_A], bx
    mov bx, [NI_SCR + NI_S_DEAD]
    mov [cs:_ni_m + NIM_CNT], bx
    mov ax, [cs:_ni_m + NIM_REASON]
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; THE C's THREE WAYS IN, OUTSIDE A RUN
; =============================================================================

; int ni_bread(unsigned a) - ONE banked read. The reset vector is under the
; PRG window table, so even that cannot be a plain peek.
_ni_bread:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    mov bx, [bp+4]
    call ni_rd_bx
    mov al, dh
    xor ah, ah                      ; a shim that answers in AL must clear AH
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; void ni_bwrite(unsigned a, int v)
_ni_bwrite:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    mov bx, [bp+4]
    mov ax, [bp+6]
    mov dh, al
    call ni_wr_bx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; void ni_boot(void) - THE RESET SEQUENCE (SPEC.md 91.4.3): the stack pointer
; drops by three, I is set, and PC is read from $FFFC. The three "pushes" are
; dummies on real silicon and write nothing, which is why this subtracts
; rather than pushing.
_ni_boot:
    push si
    push di
    push ds
    push es
    mov ds, [cs:_ni_m + NIM_MACHSEG]
    mov word [NI_SCR + NI_S_BLO], 0
    mov word [NI_SCR + NI_S_BOUND], 0
    mov bx, [cs:_ni_m + NIM_S]
    sub bl, 3
    xor bh, bh
    mov [cs:_ni_m + NIM_S], bx
    mov bx, [cs:_ni_m + NIM_P]
    or  bl, 0x24                    ; I set, bit 5 set
    and bl, 0xEF                    ; B clear - it is not a register bit
    xor bh, bh
    mov [cs:_ni_m + NIM_P], bx
    mov bx, 0xFFFC
    call ni_rd_bx
    mov [NI_SCR + NI_S_T], dh
    inc bx
    call ni_rd_bx
    mov [NI_SCR + NI_S_T + 1], dh
    mov ax, [NI_SCR + NI_S_T]
    mov [cs:_ni_m + NIM_PC], ax
    pop es
    pop ds
    pop di
    pop si
    ret

; =============================================================================
; THE TABLES (generated - see the head of the handler section)
; =============================================================================
section .rodata

ni_tab:
    dw  o_brk,         o_ora_izx,     o_kil,         o_slo_izx  ; 00-03
    dw  o_nopr_zp,     o_ora_zp,      o_asl_zp,      o_slo_zp  ; 04-07
    dw  o_php,         o_ora_imm,     o_asla,        o_anc_imm  ; 08-0B
    dw  o_nopr_abs,    o_ora_abs,     o_asl_abs,     o_slo_abs  ; 0C-0F
    dw  o_bpl_rel,     o_ora_izy,     o_kil,         o_slo_izyw  ; 10-13
    dw  o_nopr_zpx,    o_ora_zpx,     o_asl_zpx,     o_slo_zpx  ; 14-17
    dw  o_clc,         o_ora_aby,     o_nop,         o_slo_abyw  ; 18-1B
    dw  o_nopr_abx,    o_ora_abx,     o_asl_abxw,    o_slo_abxw  ; 1C-1F
    dw  o_jsr_abs,     o_and_izx,     o_kil,         o_rla_izx  ; 20-23
    dw  o_bit_zp,      o_and_zp,      o_rol_zp,      o_rla_zp  ; 24-27
    dw  o_plp,         o_and_imm,     o_rola,        o_anc_imm  ; 28-2B
    dw  o_bit_abs,     o_and_abs,     o_rol_abs,     o_rla_abs  ; 2C-2F
    dw  o_bmi_rel,     o_and_izy,     o_kil,         o_rla_izyw  ; 30-33
    dw  o_nopr_zpx,    o_and_zpx,     o_rol_zpx,     o_rla_zpx  ; 34-37
    dw  o_sec,         o_and_aby,     o_nop,         o_rla_abyw  ; 38-3B
    dw  o_nopr_abx,    o_and_abx,     o_rol_abxw,    o_rla_abxw  ; 3C-3F
    dw  o_rti,         o_eor_izx,     o_kil,         o_sre_izx  ; 40-43
    dw  o_nopr_zp,     o_eor_zp,      o_lsr_zp,      o_sre_zp  ; 44-47
    dw  o_pha,         o_eor_imm,     o_lsra,        o_alr_imm  ; 48-4B
    dw  o_jmp_abs,     o_eor_abs,     o_lsr_abs,     o_sre_abs  ; 4C-4F
    dw  o_bvc_rel,     o_eor_izy,     o_kil,         o_sre_izyw  ; 50-53
    dw  o_nopr_zpx,    o_eor_zpx,     o_lsr_zpx,     o_sre_zpx  ; 54-57
    dw  o_cli,         o_eor_aby,     o_nop,         o_sre_abyw  ; 58-5B
    dw  o_nopr_abx,    o_eor_abx,     o_lsr_abxw,    o_sre_abxw  ; 5C-5F
    dw  o_rts,         o_adc_izx,     o_kil,         o_rra_izx  ; 60-63
    dw  o_nopr_zp,     o_adc_zp,      o_ror_zp,      o_rra_zp  ; 64-67
    dw  o_pla,         o_adc_imm,     o_rora,        o_arr_imm  ; 68-6B
    dw  o_jmpi_ind,    o_adc_abs,     o_ror_abs,     o_rra_abs  ; 6C-6F
    dw  o_bvs_rel,     o_adc_izy,     o_kil,         o_rra_izyw  ; 70-73
    dw  o_nopr_zpx,    o_adc_zpx,     o_ror_zpx,     o_rra_zpx  ; 74-77
    dw  o_sei,         o_adc_aby,     o_nop,         o_rra_abyw  ; 78-7B
    dw  o_nopr_abx,    o_adc_abx,     o_ror_abxw,    o_rra_abxw  ; 7C-7F
    dw  o_nopi_imm,    o_sta_izx,     o_nopi_imm,    o_sax_izx  ; 80-83
    dw  o_sty_zp,      o_sta_zp,      o_stx_zp,      o_sax_zp  ; 84-87
    dw  o_dey,         o_nopi_imm,    o_txa,         o_xaa_imm  ; 88-8B
    dw  o_sty_abs,     o_sta_abs,     o_stx_abs,     o_sax_abs  ; 8C-8F
    dw  o_bcc_rel,     o_sta_izyw,    o_kil,         o_ahx_izyw  ; 90-93
    dw  o_sty_zpx,     o_sta_zpx,     o_stx_zpy,     o_sax_zpy  ; 94-97
    dw  o_tya,         o_sta_abyw,    o_txs,         o_tas_abyw  ; 98-9B
    dw  o_shy_abxw,    o_sta_abxw,    o_shx_abyw,    o_ahx_abyw  ; 9C-9F
    dw  o_ldy_imm,     o_lda_izx,     o_ldx_imm,     o_lax_izx  ; A0-A3
    dw  o_ldy_zp,      o_lda_zp,      o_ldx_zp,      o_lax_zp  ; A4-A7
    dw  o_tay,         o_lda_imm,     o_tax,         o_lax_imm  ; A8-AB
    dw  o_ldy_abs,     o_lda_abs,     o_ldx_abs,     o_lax_abs  ; AC-AF
    dw  o_bcs_rel,     o_lda_izy,     o_kil,         o_lax_izy  ; B0-B3
    dw  o_ldy_zpx,     o_lda_zpx,     o_ldx_zpy,     o_lax_zpy  ; B4-B7
    dw  o_clv,         o_lda_aby,     o_tsx,         o_las_aby  ; B8-BB
    dw  o_ldy_abx,     o_lda_abx,     o_ldx_aby,     o_lax_aby  ; BC-BF
    dw  o_cpy_imm,     o_cmp_izx,     o_nopi_imm,    o_dcp_izx  ; C0-C3
    dw  o_cpy_zp,      o_cmp_zp,      o_dec_zp,      o_dcp_zp  ; C4-C7
    dw  o_iny,         o_cmp_imm,     o_dex,         o_axs_imm  ; C8-CB
    dw  o_cpy_abs,     o_cmp_abs,     o_dec_abs,     o_dcp_abs  ; CC-CF
    dw  o_bne_rel,     o_cmp_izy,     o_kil,         o_dcp_izyw  ; D0-D3
    dw  o_nopr_zpx,    o_cmp_zpx,     o_dec_zpx,     o_dcp_zpx  ; D4-D7
    dw  o_cld,         o_cmp_aby,     o_nop,         o_dcp_abyw  ; D8-DB
    dw  o_nopr_abx,    o_cmp_abx,     o_dec_abxw,    o_dcp_abxw  ; DC-DF
    dw  o_cpx_imm,     o_sbc_izx,     o_nopi_imm,    o_isb_izx  ; E0-E3
    dw  o_cpx_zp,      o_sbc_zp,      o_inc_zp,      o_isb_zp  ; E4-E7
    dw  o_inx,         o_sbc_imm,     o_nop,         o_sbc_imm  ; E8-EB
    dw  o_cpx_abs,     o_sbc_abs,     o_inc_abs,     o_isb_abs  ; EC-EF
    dw  o_beq_rel,     o_sbc_izy,     o_kil,         o_isb_izyw  ; F0-F3
    dw  o_nopr_zpx,    o_sbc_zpx,     o_inc_zpx,     o_isb_zpx  ; F4-F7
    dw  o_sed,         o_sbc_aby,     o_nop,         o_isb_abyw  ; F8-FB
    dw  o_nopr_abx,    o_sbc_abx,     o_inc_abxw,    o_isb_abxw  ; FC-FF

ni_cyc:
    db  7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6   ; 00
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; 10
    db  6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6   ; 20
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; 30
    db  6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6   ; 40
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; 50
    db  6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6   ; 60
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; 70
    db  2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4   ; 80
    db  2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5   ; 90
    db  2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4   ; A0
    db  2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4   ; B0
    db  2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6   ; C0
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; D0
    db  2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6   ; E0
    db  2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7   ; F0

section .text
