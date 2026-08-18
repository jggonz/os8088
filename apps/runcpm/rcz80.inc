; =============================================================================
; os8088 - apps/runcpm/rcz80.inc       the Z80 core (SPEC.md 71)
;
; Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
; "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
; Copyright (c) 2017 Mockba the Borg). The instruction semantics and the flag
; results this file reproduces are RunCPM/cpu1.h's - BEHAVIOUR, not lines: a
; C switch of 4,249 lines became 8086 handlers reached through jump tables -
; and the oracle is ZEXDOC off the master disk (tests/rczex.py, and the
; boot-sector harness hosttest/rcz80test.asm that runs the same core in raw
; QEMU). %included by apps/runcpm/runcpm.asm after the compiled C and by both
; harnesses, so what is tested is the shipping text and not a restatement.
;
; THE REGISTER PLAN (SPEC.md 71), which is why the code reads as it does:
;
;     Z80        8086            note
;     A / F      AL / AH         F in `lahf` LAYOUT: S=7 Z=6 H=4 P/V=2 C=0 land
;                                exactly where the Z80 keeps them, so most
;                                8-bit results are one `lahf` and a mask.
;                                N (bit 1) is fixed up per operation - lahf's
;                                bit 1 is always 1, so a subtract keeps it and
;                                an add masks it. P/V after arithmetic is the
;                                8086's OF, taken with a `jo` because the 8086
;                                cannot move OF without a jump. Bits 3 and 5
;                                (X/Y, undocumented) are always 0.
;     BC / DE    CX / DX
;     HL         DI              (HL) is a plain [di]; H and L cost a `mov
;                                bx,di` because DI has no byte halves
;     PC         SI              the fetch is `mov bl,[si] / inc si` - never
;                                lodsb, which would clobber A
;     SP         BP              every stack access is `[ds:bp]`: [bp] alone
;                                would address SS, which is LOW_SEG here
;     IX IY AF' BC' DE' HL' I R IFF IM   the register file _rc_z, reached
;                                with cs:
;     (scratch)  BX              the dispatch index, and free inside a handler
;     DS         the 64KB Z80 claim, for the whole of rc_run; SS the task's
;                stack, CS this package - so a static is [cs:x], Z80 memory
;                is [x], and nothing here ever loads ES. The core's own hot
;                scratch (the slice budget, the CB/DD operands) is in the
;                Z80's top 16 bytes, [0xFFF0..] - see RC_ZS_* below for why
;
; THE DISPATCH is `xor bh,bh / mov bl,[si] / inc si / shl bx,1 / jmp
; [cs:bx+rc_tab]`, and every handler ends in `jmp rc_next` (or in one of the
; flag finishers, which do). CB, ED, DD and FD have tables of their own; DDCB
; and FDCB share the CB operators through a precomputed (IX+d) address; DD
; and FD share ONE table through a pointer to whichever index register the
; prefix named. Undocumented opcodes: the IXH/IXL/IYH/IYL forms ARE
; implemented (ZEXDOC exercises them); DD/FD in front of any other opcode
; without an HL form execute the unprefixed opcode, as on the silicon; ED
; holes are two-byte NOPs; CB 30-37 is SLL with bit 0 set; DDCB/FDCB with a
; register field other than (HL) act on memory only (the copy-to-register is
; not done). X and Y (F bits 3 and 5) are not computed - ZEXDOC masks them.
;
; THE SLICE. rc_run(n) runs until n CONTROL TRANSFERS have been taken (a JP,
; JR, DJNZ, CALL, RET, RST or a block-repeat step - counting only there keeps
; the memory decrement off the straight-line path, and no loop exists without
; one), or until a RST 08h / RST 10h handoff, or HALT. It answers the reason
; and leaves the count that was left in rc_z.cnt, so the caller can service
; a trap and resume the SAME slice. R is not counted per fetch (that would be
; a memory increment on every instruction, a quarter of the dispatch): LD A,R
; answers a value that advances with the slice count, which is what programs
; that read R want of it - a stated deviation (SPEC.md 71).
;
; RunCPM's INT_HANDOFF: RST 08h at BIOSpage+3n and RST 10h at BDOSpage are
; the BIOS and BDOS entries. cpu1.h pushes PC, calls _Bios/_Bdos and pops it
; back; here the core simply RETURNS with SI past the RST and rc_z.pc set, the
; C services the call and re-enters, and the RET that follows the RST in the
; page does the rest - identical machine state, no push. A trap the C cannot
; complete yet (CONIN with an empty key ring) is retried by putting PC back on
; the RST (SPEC.md 71.1's handler contract).
;
; Interrupts never happen (there is no interrupt source): EI/DI keep IFF for
; LD A,I / LD A,R to report, HALT stops the machine, IN answers 0 and OUT is a
; no-op, exactly RunCPM's _HardwareIn/_HardwareOut on a desktop.
; =============================================================================

section .bss

; The register file the C reads and writes (extern struct rc_zregs rc_z in
; runcpm.c - the two layouts are the same by construction: 19 words, in this
; order). Everything the core needs between two calls is here and nowhere else.
_rc_z:
RZ_AF     equ 0
RZ_BC     equ 2
RZ_DE     equ 4
RZ_HL     equ 6
RZ_PC     equ 8
RZ_SP     equ 10
RZ_IX     equ 12
RZ_IY     equ 14
RZ_AF2    equ 16
RZ_BC2    equ 18
RZ_DE2    equ 20
RZ_HL2    equ 22
RZ_I      equ 24                    ; low byte
RZ_R      equ 26                    ; low byte
RZ_IFF    equ 28                    ; bit 0 IFF1, bit 1 IFF2
RZ_IM     equ 30
RZ_SEG    equ 32                    ; the 64KB claim: DS inside rc_run
RZ_CNT    equ 34                    ; the slice budget left (control transfers)
RZ_REASON equ 36                    ; RC_RUN_* of the last return
    resw 19

RC_RUN_SLICE equ 0                  ; the budget ran out, the Z80 is running
RC_RUN_BIOS  equ 1                  ; RST 08h: rc_z.pc is past it
RC_RUN_BDOS  equ 2                  ; RST 10h
RC_RUN_HALT  equ 3                  ; HALT: rc_z.pc is ON it

section .text

; THE CORE'S SCRATCH LIVES IN THE Z80'S OWN TOP 16 BYTES, 0xFFF0-0xFFFF -
; above everything RunCPM's BIOS page uses (cpm.h: the DPH ends at 0xFF9F,
; and 0xFFA0-0xFFFF is never written or read by RunCPM or by CP/M) - as
; DS-relative words with no segment override, rather than in the package's
; own bss with a cs: prefix. Two reasons, one measured. On the 8088 a
; `dec word [0xFFF0]` is a byte shorter and a few clocks quicker than the
; cs:-prefixed form on a path taken at every control transfer. And under
; QEMU the difference was 38 against 180 MHz-equivalent: its translator
; (TCG) sends every write to a page that also holds translated code through
; a slow path, and the package's bss shares its last page with whatever the
; heap put above it - the Sound driver, here, whose code runs every tick -
; so a per-branch write to a package static cost the core five times its
; speed on the machine every gate runs on. Stated in SPEC.md 71 as the one
; place the emulator uses Z80 memory for its own bookkeeping.
RC_ZS_CNT   equ 0xFFF0              ; the slice budget while rc_run runs
RC_ZS_IXP   equ 0xFFF2              ; DD/FD: -> _rc_z+RZ_IX or +RZ_IY (a
                                    ; package offset)
RC_ZS_IXV   equ 0xFFF4              ; the index register's value at the prefix
RC_ZS_EA    equ 0xFFF6              ; DDCB/FDCB: the (IX+d) address
RC_ZS_CBOP  equ 0xFFF8              ; CB: the second opcode byte
RC_ZS_OP    equ 0xFFFA              ; CB: the operator, called indirect
RC_ZS_VAL   equ 0xFFFC              ; CB: the operand in transit (a byte)

; -----------------------------------------------------------------------------
; int rc_run(int n) - cdecl. Run the Z80 in the claim rc_z.seg for up to n
; control transfers; answer RC_RUN_*. Preserves BP, DS, SS:SP, DF (the C ABI
; wants nothing else, SPEC.md 70.3).
; -----------------------------------------------------------------------------
_rc_run:
    push bp
    mov bp, sp
    mov ax, [bp+4]                  ; n
    push ds
    mov ds, [cs:_rc_z+RZ_SEG]
    mov [RC_ZS_CNT], ax
    mov ax, [cs:_rc_z+RZ_AF]
    mov cx, [cs:_rc_z+RZ_BC]
    mov dx, [cs:_rc_z+RZ_DE]
    mov di, [cs:_rc_z+RZ_HL]
    mov si, [cs:_rc_z+RZ_PC]
    mov bp, [cs:_rc_z+RZ_SP]
    jmp short rc_next

; the three ways out. rc_exit packs the registers and answers the reason.
rc_slice_out:
    mov word [cs:_rc_z+RZ_REASON], RC_RUN_SLICE
    jmp short rc_exit
rc_trap_bios:                       ; RST 08h: SI is past the RST
    mov word [cs:_rc_z+RZ_REASON], RC_RUN_BIOS
    jmp short rc_exit
rc_trap_bdos:                       ; RST 10h
    mov word [cs:_rc_z+RZ_REASON], RC_RUN_BDOS
    jmp short rc_exit
h_halt:                             ; 76: PC stays ON the HALT (cpu1.h --PC)
    dec si
    mov word [cs:_rc_z+RZ_REASON], RC_RUN_HALT
rc_exit:
    mov [cs:_rc_z+RZ_AF], ax
    mov [cs:_rc_z+RZ_BC], cx
    mov [cs:_rc_z+RZ_DE], dx
    mov [cs:_rc_z+RZ_HL], di
    mov [cs:_rc_z+RZ_PC], si
    mov [cs:_rc_z+RZ_SP], bp
    mov ax, [RC_ZS_CNT]
    mov [cs:_rc_z+RZ_CNT], ax
    mov ax, [cs:_rc_z+RZ_REASON]
    pop ds
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE FETCH
; -----------------------------------------------------------------------------
rc_next:
    xor bh, bh
    mov bl, [si]
    inc si
    shl bx, 1
    jmp [cs:bx+rc_tab]

; =============================================================================
; MACROS - the flag finishers and the shapes that repeat
; =============================================================================

; after an 8-bit ADD/ADC: S Z H C from lahf, P/V = OF, N = 0
%macro FIN_ADD 0
    lahf
    jo %%o
    and ah, 0D1h
    jmp rc_next
%%o:
    and ah, 0D1h
    or ah, 4
    jmp rc_next
%endmacro

; after an 8-bit SUB/SBC/CP/NEG: as above with N = 1 (lahf's bit 1 is 1)
%macro FIN_SUB 0
    lahf
    jo %%o
    and ah, 0D3h
    jmp rc_next
%%o:
    and ah, 0D3h
    or ah, 4
    jmp rc_next
%endmacro

; after AND/OR/XOR: S Z P from lahf, C = 0, N = 0, H = %1 (10h for AND)
%macro FIN_LOGIC 1
    lahf
    and ah, 0C4h
  %if %1
    or ah, %1
  %endif
    jmp rc_next
%endmacro

%macro ALU_ADD 1
    add al, %1
    FIN_ADD
%endmacro
%macro ALU_ADC 1
    shr ah, 1                       ; CF = C
    adc al, %1
    FIN_ADD
%endmacro
%macro ALU_SUB 1
    sub al, %1
    FIN_SUB
%endmacro
%macro ALU_SBC 1
    shr ah, 1
    sbb al, %1
    FIN_SUB
%endmacro
%macro ALU_AND 1
    and al, %1
    FIN_LOGIC 10h
%endmacro
%macro ALU_XOR 1
    xor al, %1
    FIN_LOGIC 0
%endmacro
%macro ALU_OR 1
    or al, %1
    FIN_LOGIC 0
%endmacro
%macro ALU_CP 1
    cmp al, %1
    FIN_SUB
%endmacro

; the ten operand forms of one ALU operation: B C D E H L (HL) A n (IX+d).
; %1 = the ALU_ macro, %2 = the name stem
%macro ALU_ROW 2
h_%2_b:
    %1 ch
h_%2_c:
    %1 cl
h_%2_d:
    %1 dh
h_%2_e:
    %1 dl
h_%2_h:
    mov bx, di
    %1 bh
h_%2_l:
    mov bx, di
    %1 bl
h_%2_m:
    %1 byte [di]
h_%2_a:
    %1 al
h_%2_n:
    mov bl, [si]
    inc si
    %1 bl
h_%2_x:
    call rc_ixea
    %1 byte [bx]
%endmacro

; INC/DEC of an 8-bit operand: C kept (shr moves it into CF, inc/dec leave CF
; alone, lahf brings it back), S Z H from the result, P/V = OF, N 0/1.
; %2, if given, is an instruction run between the operation and the finisher
; (the `mov di, bx` that puts H or L back - mov writes no flags).
%macro INC8 1-2
    shr ah, 1
    inc %1
  %if %0 > 1
    %2
  %endif
    lahf
    jo %%o
    and ah, 0D1h
    jmp rc_next
%%o:
    and ah, 0D1h
    or ah, 4
    jmp rc_next
%endmacro
%macro DEC8 1-2
    shr ah, 1
    dec %1
  %if %0 > 1
    %2
  %endif
    lahf
    jo %%o
    and ah, 0D3h
    jmp rc_next
%%o:
    and ah, 0D3h
    or ah, 4
    jmp rc_next
%endmacro

; ADD HL,rr (%1 = cx dx di bp): C from the add, H = carry out of bit 11 =
; bit 12 of (HL ^ rr ^ sum), N = 0, S Z P/V kept
%macro ADD16 1
    push ax
    mov bx, di
    xor bx, %1
    add di, %1
    lahf
    xor bx, di
    and bh, 10h
    and ah, 1
    or ah, bh
    pop bx                          ; bh = the old F
    and bh, 0C4h
    or ah, bh
    jmp rc_next
%endmacro

; ADC HL,rr / SBC HL,rr (ED): every flag - S Z from the 16-bit result, H as
; above, P/V = OF, N, C
%macro ADC16 1
    mov bx, di
    xor bx, %1
    shr ah, 1
    adc di, %1
    lahf
    jo %%o
    and ah, 0C1h
    jmp short %%m
%%o:
    and ah, 0C1h
    or ah, 4
%%m:
    xor bx, di
    and bh, 10h
    or ah, bh
    jmp rc_next
%endmacro
%macro SBC16 1
    mov bx, di
    xor bx, %1
    shr ah, 1
    sbb di, %1
    lahf
    jo %%o
    and ah, 0C3h
    jmp short %%m
%%o:
    and ah, 0C3h
    or ah, 4
%%m:
    xor bx, di
    and bh, 10h
    or ah, bh
    jmp rc_next
%endmacro

; a control transfer was taken: spend one of the slice's budget
%macro COUNT 0
    dec word [RC_ZS_CNT]
    jz %%out
    jmp rc_next
%%out:
    jmp rc_slice_out
%endmacro

; JR: the signed displacement at [si], PC past it
%macro JR_TAKE 0
    xor bh, bh
    mov bl, [si]
    inc si
    test bl, bl
    jns %%p
    dec bh
%%p:
    add si, bx
    COUNT
%endmacro

; the conditional forms: %1 = the F mask, %2 = jz|jnz - the jump that SKIPS
; (taken when the condition is false)
%macro JRCC 2
    test ah, %1
    %2 %%no
    JR_TAKE
%%no:
    inc si
    jmp rc_next
%endmacro
%macro JPCC 2
    test ah, %1
    %2 %%no
    mov si, [si]
    COUNT
%%no:
    inc si
    inc si
    jmp rc_next
%endmacro
%macro CALLCC 2
    test ah, %1
    %2 %%no
    jmp h_call
%%no:
    inc si
    inc si
    jmp rc_next
%endmacro
%macro RETCC 2
    test ah, %1
    %2 %%no
    jmp h_ret
%%no:
    jmp rc_next
%endmacro

%macro PUSH16 1
    dec bp
    dec bp
    mov [ds:bp], %1
    jmp rc_next
%endmacro
%macro POP16 1
    mov %1, [ds:bp]
    inc bp
    inc bp
    jmp rc_next
%endmacro

%macro RSTN 1
    dec bp
    dec bp
    mov [ds:bp], si
    mov si, %1
    COUNT
%endmacro

; DD/FD: swap HL and the named index register so a base-shaped body works on
; it; done twice around the body
%macro IXSWAP 0
    mov bx, [RC_ZS_IXP]
    xchg di, [cs:bx]
%endmacro

; =============================================================================
; THE BASE HANDLERS (opcodes 00-FF)
; =============================================================================

h_nop:
    jmp rc_next

; --- 8-bit loads --------------------------------------------------------------
h_ld_b_n:   mov ch, [si]
            inc si
            jmp rc_next
h_ld_c_n:   mov cl, [si]
            inc si
            jmp rc_next
h_ld_d_n:   mov dh, [si]
            inc si
            jmp rc_next
h_ld_e_n:   mov dl, [si]
            inc si
            jmp rc_next
h_ld_h_n:   mov bx, di
            mov bh, [si]
            inc si
            mov di, bx
            jmp rc_next
h_ld_l_n:   mov bx, di
            mov bl, [si]
            inc si
            mov di, bx
            jmp rc_next
h_ld_m_n:   mov bl, [si]
            inc si
            mov [di], bl
            jmp rc_next
h_ld_a_n:   mov al, [si]
            inc si
            jmp rc_next

; LD r,r' - the 64: rows are the destination, columns the source
h_ld_b_c:   mov ch, cl
            jmp rc_next
h_ld_b_d:   mov ch, dh
            jmp rc_next
h_ld_b_e:   mov ch, dl
            jmp rc_next
h_ld_b_h:   mov bx, di
            mov ch, bh
            jmp rc_next
h_ld_b_l:   mov bx, di
            mov ch, bl
            jmp rc_next
h_ld_b_m:   mov ch, [di]
            jmp rc_next
h_ld_b_a:   mov ch, al
            jmp rc_next

h_ld_c_b:   mov cl, ch
            jmp rc_next
h_ld_c_d:   mov cl, dh
            jmp rc_next
h_ld_c_e:   mov cl, dl
            jmp rc_next
h_ld_c_h:   mov bx, di
            mov cl, bh
            jmp rc_next
h_ld_c_l:   mov bx, di
            mov cl, bl
            jmp rc_next
h_ld_c_m:   mov cl, [di]
            jmp rc_next
h_ld_c_a:   mov cl, al
            jmp rc_next

h_ld_d_b:   mov dh, ch
            jmp rc_next
h_ld_d_c:   mov dh, cl
            jmp rc_next
h_ld_d_e:   mov dh, dl
            jmp rc_next
h_ld_d_h:   mov bx, di
            mov dh, bh
            jmp rc_next
h_ld_d_l:   mov bx, di
            mov dh, bl
            jmp rc_next
h_ld_d_m:   mov dh, [di]
            jmp rc_next
h_ld_d_a:   mov dh, al
            jmp rc_next

h_ld_e_b:   mov dl, ch
            jmp rc_next
h_ld_e_c:   mov dl, cl
            jmp rc_next
h_ld_e_d:   mov dl, dh
            jmp rc_next
h_ld_e_h:   mov bx, di
            mov dl, bh
            jmp rc_next
h_ld_e_l:   mov bx, di
            mov dl, bl
            jmp rc_next
h_ld_e_m:   mov dl, [di]
            jmp rc_next
h_ld_e_a:   mov dl, al
            jmp rc_next

h_ld_h_b:   mov bx, di
            mov bh, ch
            mov di, bx
            jmp rc_next
h_ld_h_c:   mov bx, di
            mov bh, cl
            mov di, bx
            jmp rc_next
h_ld_h_d:   mov bx, di
            mov bh, dh
            mov di, bx
            jmp rc_next
h_ld_h_e:   mov bx, di
            mov bh, dl
            mov di, bx
            jmp rc_next
h_ld_h_l:   mov bx, di
            mov bh, bl
            mov di, bx
            jmp rc_next
h_ld_h_m:   mov bx, di
            mov bh, [di]
            mov di, bx
            jmp rc_next
h_ld_h_a:   mov bx, di
            mov bh, al
            mov di, bx
            jmp rc_next

h_ld_l_b:   mov bx, di
            mov bl, ch
            mov di, bx
            jmp rc_next
h_ld_l_c:   mov bx, di
            mov bl, cl
            mov di, bx
            jmp rc_next
h_ld_l_d:   mov bx, di
            mov bl, dh
            mov di, bx
            jmp rc_next
h_ld_l_e:   mov bx, di
            mov bl, dl
            mov di, bx
            jmp rc_next
h_ld_l_h:   mov bx, di
            mov bl, bh
            mov di, bx
            jmp rc_next
h_ld_l_m:   mov bx, di
            mov bl, [di]
            mov di, bx
            jmp rc_next
h_ld_l_a:   mov bx, di
            mov bl, al
            mov di, bx
            jmp rc_next

h_ld_m_b:   mov [di], ch
            jmp rc_next
h_ld_m_c:   mov [di], cl
            jmp rc_next
h_ld_m_d:   mov [di], dh
            jmp rc_next
h_ld_m_e:   mov [di], dl
            jmp rc_next
h_ld_m_h:   mov bx, di
            mov [di], bh
            jmp rc_next
h_ld_m_l:   mov bx, di
            mov [di], bl
            jmp rc_next
h_ld_m_a:   mov [di], al
            jmp rc_next

h_ld_a_b:   mov al, ch
            jmp rc_next
h_ld_a_c:   mov al, cl
            jmp rc_next
h_ld_a_d:   mov al, dh
            jmp rc_next
h_ld_a_e:   mov al, dl
            jmp rc_next
h_ld_a_h:   mov bx, di
            mov al, bh
            jmp rc_next
h_ld_a_l:   mov bx, di
            mov al, bl
            jmp rc_next
h_ld_a_m:   mov al, [di]
            jmp rc_next

h_ld_bc_a:  mov bx, cx
            mov [bx], al
            jmp rc_next
h_ld_de_a:  mov bx, dx
            mov [bx], al
            jmp rc_next
h_ld_a_bc:  mov bx, cx
            mov al, [bx]
            jmp rc_next
h_ld_a_de:  mov bx, dx
            mov al, [bx]
            jmp rc_next
h_ld_nn_a:  mov bx, [si]
            inc si
            inc si
            mov [bx], al
            jmp rc_next
h_ld_a_nn:  mov bx, [si]
            inc si
            inc si
            mov al, [bx]
            jmp rc_next

; --- 16-bit loads -------------------------------------------------------------
h_ld_bc_nn: mov cx, [si]
            inc si
            inc si
            jmp rc_next
h_ld_de_nn: mov dx, [si]
            inc si
            inc si
            jmp rc_next
h_ld_hl_nn: mov di, [si]
            inc si
            inc si
            jmp rc_next
h_ld_sp_nn: mov bp, [si]
            inc si
            inc si
            jmp rc_next
h_ld_nn_hl: mov bx, [si]
            inc si
            inc si
            mov [bx], di
            jmp rc_next
h_ld_hl_nn_: mov bx, [si]             ; LD HL,(nn)
            inc si
            inc si
            mov di, [bx]
            jmp rc_next
h_ld_sp_hl: mov bp, di
            jmp rc_next
h_push_bc:  PUSH16 cx
h_push_de:  PUSH16 dx
h_push_hl:  PUSH16 di
h_push_af:  mov bx, ax                ; memory: F low, A high
            xchg bl, bh
            PUSH16 bx
h_pop_bc:   POP16 cx
h_pop_de:   POP16 dx
h_pop_hl:   POP16 di
h_pop_af:   mov ax, [ds:bp]
            xchg al, ah
            inc bp
            inc bp
            jmp rc_next
h_ex_de_hl: xchg dx, di
            jmp rc_next
h_ex_af:    xchg ax, [cs:_rc_z+RZ_AF2]
            jmp rc_next
h_exx:      xchg cx, [cs:_rc_z+RZ_BC2]
            xchg dx, [cs:_rc_z+RZ_DE2]
            xchg di, [cs:_rc_z+RZ_HL2]
            jmp rc_next
h_ex_sp_hl: xchg di, [ds:bp]
            jmp rc_next

; --- 8-bit arithmetic ---------------------------------------------------------
ALU_ROW ALU_ADD, add
ALU_ROW ALU_ADC, adc
ALU_ROW ALU_SUB, sub
ALU_ROW ALU_SBC, sbc
ALU_ROW ALU_AND, and
ALU_ROW ALU_XOR, xor
ALU_ROW ALU_OR,  or
ALU_ROW ALU_CP,  cp

h_inc_b:    INC8 ch
h_inc_c:    INC8 cl
h_inc_d:    INC8 dh
h_inc_e:    INC8 dl
h_inc_h:    mov bx, di
            INC8 bh, {mov di, bx}
h_inc_l:    mov bx, di
            INC8 bl, {mov di, bx}
h_inc_m:    INC8 byte [di]
h_inc_a:    INC8 al
h_inc_x:    call rc_ixea
            INC8 byte [bx]
h_dec_b:    DEC8 ch
h_dec_c:    DEC8 cl
h_dec_d:    DEC8 dh
h_dec_e:    DEC8 dl
h_dec_h:    mov bx, di
            DEC8 bh, {mov di, bx}
h_dec_l:    mov bx, di
            DEC8 bl, {mov di, bx}
h_dec_m:    DEC8 byte [di]
h_dec_a:    DEC8 al
h_dec_x:    call rc_ixea
            DEC8 byte [bx]

; --- general purpose arithmetic -----------------------------------------------
; DAA: the 8086's own DAA/DAS do the adjustment and S Z P once AF and CF
; hold H and C (sahf reads them out of AH's lahf layout); H and, after a
; subtract, C are the Z80's own rules (cpu1.h 0x27): after an add H = (low
; nibble > 9); after a subtract H = H && (low nibble < 6) and C = C || (A >
; 99h) - DAS would also set C on the low nibble's borrow, which the Z80 does
; not; all of the value BEFORE the adjustment. Probed against cpu1.h's rule
; over every A and H/N/C (hosttest, wave 2) and ZEXDOC's group.
h_daa:
    mov bl, al
    and bl, 0Fh                     ; the low nibble before
    mov bh, ah                      ; F before
    test bh, 2                      ; (before the sahf: test clears CF)
    jnz .sub
    sahf                            ; AF = H, CF = C
    daa
    lahf
    and ah, 0C5h                    ; S Z P C
    cmp bl, 9
    jbe .d
    or ah, 10h
.d: jmp rc_next
.sub:
    mov bl, al                      ; A before, whole
    sahf
    das
    lahf
    and ah, 0C4h                    ; S Z P; C is the Z80's rule, not DAS's:
    or ah, 2                        ; N
    test bh, 1                      ; C = old C || A before > 99h (DAS also
    jnz .c1                         ; sets it on the low nibble's borrow)
    cmp bl, 99h
    jbe .nc
.c1:
    or ah, 1
.nc:
    test bh, 10h                    ; H = old H && low nibble < 6
    jz .d
    and bl, 0Fh
    cmp bl, 6
    jae .d
    or ah, 10h
    jmp rc_next

h_cpl:      not al                    ; H = 1, N = 1, the rest kept
            or ah, 12h
            jmp rc_next
h_scf:      and ah, 0C4h
            or ah, 1
            jmp rc_next
h_ccf:      test ah, 1                ; H = the old C, C inverted, N = 0
            jz .was0
            and ah, 0C4h
            or ah, 10h
            jmp rc_next
.was0:      and ah, 0C4h
            or ah, 1
            jmp rc_next
h_di:       mov word [cs:_rc_z+RZ_IFF], 0
            jmp rc_next
h_ei:       mov word [cs:_rc_z+RZ_IFF], 3
            jmp rc_next

; --- 16-bit arithmetic --------------------------------------------------------
h_add_hl_bc: ADD16 cx
h_add_hl_de: ADD16 dx
h_add_hl_hl: ADD16 di
h_add_hl_sp: ADD16 bp
h_inc_bc:   inc cx
            jmp rc_next
h_inc_de:   inc dx
            jmp rc_next
h_inc_hl:   inc di
            jmp rc_next
h_inc_sp:   inc bp
            jmp rc_next
h_dec_bc:   dec cx
            jmp rc_next
h_dec_de:   dec dx
            jmp rc_next
h_dec_hl:   dec di
            jmp rc_next
h_dec_sp:   dec bp
            jmp rc_next

; --- rotates on A (C from the rotate; S Z P/V kept; H = N = 0) ----------------
h_rlca:     rol al, 1
            sbb bl, bl                ; bl = -C
            and ah, 0C4h
            sub ah, bl
            jmp rc_next
h_rrca:     ror al, 1
            sbb bl, bl
            and ah, 0C4h
            sub ah, bl
            jmp rc_next
h_rla:      mov bl, ah
            shr bl, 1                 ; CF = C
            rcl al, 1
            sbb bl, bl
            and ah, 0C4h
            sub ah, bl
            jmp rc_next
h_rra:      mov bl, ah
            shr bl, 1
            rcr al, 1
            sbb bl, bl
            and ah, 0C4h
            sub ah, bl
            jmp rc_next

; --- jumps, calls, returns ------------------------------------------------------
h_jp:       mov si, [si]
            COUNT
h_jp_hl:    mov si, di
            COUNT
h_jr:       JR_TAKE
h_djnz:     dec ch
            jz .no
            JR_TAKE
.no:        inc si
            jmp rc_next
h_jr_nz:    JRCC 40h, jnz
h_jr_z:     JRCC 40h, jz
h_jr_nc:    JRCC 1, jnz
h_jr_c:     JRCC 1, jz
h_jp_nz:    JPCC 40h, jnz
h_jp_z:     JPCC 40h, jz
h_jp_nc:    JPCC 1, jnz
h_jp_c:     JPCC 1, jz
h_jp_po:    JPCC 4, jnz
h_jp_pe:    JPCC 4, jz
h_jp_p:     JPCC 80h, jnz
h_jp_m:     JPCC 80h, jz
h_call:     mov bx, [si]
            inc si
            inc si
            dec bp
            dec bp
            mov [ds:bp], si
            mov si, bx
            COUNT
h_call_nz:  CALLCC 40h, jnz
h_call_z:   CALLCC 40h, jz
h_call_nc:  CALLCC 1, jnz
h_call_c:   CALLCC 1, jz
h_call_po:  CALLCC 4, jnz
h_call_pe:  CALLCC 4, jz
h_call_p:   CALLCC 80h, jnz
h_call_m:   CALLCC 80h, jz
h_ret:      mov si, [ds:bp]
            inc bp
            inc bp
            COUNT
h_ret_nz:   RETCC 40h, jnz
h_ret_z:    RETCC 40h, jz
h_ret_nc:   RETCC 1, jnz
h_ret_c:    RETCC 1, jz
h_ret_po:   RETCC 4, jnz
h_ret_pe:   RETCC 4, jz
h_ret_p:    RETCC 80h, jnz
h_ret_m:    RETCC 80h, jz
h_rst00:    RSTN 00h
h_rst08:    jmp rc_trap_bios          ; the BIOS handoff (cpm.h RST_08)
h_rst10:    jmp rc_trap_bdos          ; the BDOS handoff (cpm.h RST_10)
h_rst18:    RSTN 18h
h_rst20:    RSTN 20h
h_rst28:    RSTN 28h
h_rst30:    RSTN 30h
h_rst38:    RSTN 38h

; --- I/O: nothing is connected (abstraction_posix.h _HardwareIn = 0) ----------
h_out_n_a:  inc si
            jmp rc_next
h_in_a_n:   inc si
            mov al, 0
            jmp rc_next

; --- the prefixes ---------------------------------------------------------------
h_cb:       jmp cb_prefix
h_ed:       jmp ed_prefix
h_dd:       mov bx, [cs:_rc_z+RZ_IX]
            mov [RC_ZS_IXV], bx
            mov word [RC_ZS_IXP], _rc_z+RZ_IX
            jmp dd_dispatch
h_fd:       mov bx, [cs:_rc_z+RZ_IY]
            mov [RC_ZS_IXV], bx
            mov word [RC_ZS_IXP], _rc_z+RZ_IY
            jmp dd_dispatch

; =============================================================================
; THE BASE TABLE
; =============================================================================
section .rodata
align 2
rc_tab:
    ; 00
    dw h_nop, h_ld_bc_nn, h_ld_bc_a, h_inc_bc, h_inc_b, h_dec_b, h_ld_b_n, h_rlca
    dw h_ex_af, h_add_hl_bc, h_ld_a_bc, h_dec_bc, h_inc_c, h_dec_c, h_ld_c_n, h_rrca
    ; 10
    dw h_djnz, h_ld_de_nn, h_ld_de_a, h_inc_de, h_inc_d, h_dec_d, h_ld_d_n, h_rla
    dw h_jr, h_add_hl_de, h_ld_a_de, h_dec_de, h_inc_e, h_dec_e, h_ld_e_n, h_rra
    ; 20
    dw h_jr_nz, h_ld_hl_nn, h_ld_nn_hl, h_inc_hl, h_inc_h, h_dec_h, h_ld_h_n, h_daa
    dw h_jr_z, h_add_hl_hl, h_ld_hl_nn_, h_dec_hl, h_inc_l, h_dec_l, h_ld_l_n, h_cpl
    ; 30
    dw h_jr_nc, h_ld_sp_nn, h_ld_nn_a, h_inc_sp, h_inc_m, h_dec_m, h_ld_m_n, h_scf
    dw h_jr_c, h_add_hl_sp, h_ld_a_nn, h_dec_sp, h_inc_a, h_dec_a, h_ld_a_n, h_ccf
    ; 40
    dw h_nop, h_ld_b_c, h_ld_b_d, h_ld_b_e, h_ld_b_h, h_ld_b_l, h_ld_b_m, h_ld_b_a
    dw h_ld_c_b, h_nop, h_ld_c_d, h_ld_c_e, h_ld_c_h, h_ld_c_l, h_ld_c_m, h_ld_c_a
    ; 50
    dw h_ld_d_b, h_ld_d_c, h_nop, h_ld_d_e, h_ld_d_h, h_ld_d_l, h_ld_d_m, h_ld_d_a
    dw h_ld_e_b, h_ld_e_c, h_ld_e_d, h_nop, h_ld_e_h, h_ld_e_l, h_ld_e_m, h_ld_e_a
    ; 60
    dw h_ld_h_b, h_ld_h_c, h_ld_h_d, h_ld_h_e, h_nop, h_ld_h_l, h_ld_h_m, h_ld_h_a
    dw h_ld_l_b, h_ld_l_c, h_ld_l_d, h_ld_l_e, h_ld_l_h, h_nop, h_ld_l_m, h_ld_l_a
    ; 70
    dw h_ld_m_b, h_ld_m_c, h_ld_m_d, h_ld_m_e, h_ld_m_h, h_ld_m_l, h_halt, h_ld_m_a
    dw h_ld_a_b, h_ld_a_c, h_ld_a_d, h_ld_a_e, h_ld_a_h, h_ld_a_l, h_ld_a_m, h_nop
    ; 80
    dw h_add_b, h_add_c, h_add_d, h_add_e, h_add_h, h_add_l, h_add_m, h_add_a
    dw h_adc_b, h_adc_c, h_adc_d, h_adc_e, h_adc_h, h_adc_l, h_adc_m, h_adc_a
    ; 90
    dw h_sub_b, h_sub_c, h_sub_d, h_sub_e, h_sub_h, h_sub_l, h_sub_m, h_sub_a
    dw h_sbc_b, h_sbc_c, h_sbc_d, h_sbc_e, h_sbc_h, h_sbc_l, h_sbc_m, h_sbc_a
    ; A0
    dw h_and_b, h_and_c, h_and_d, h_and_e, h_and_h, h_and_l, h_and_m, h_and_a
    dw h_xor_b, h_xor_c, h_xor_d, h_xor_e, h_xor_h, h_xor_l, h_xor_m, h_xor_a
    ; B0
    dw h_or_b, h_or_c, h_or_d, h_or_e, h_or_h, h_or_l, h_or_m, h_or_a
    dw h_cp_b, h_cp_c, h_cp_d, h_cp_e, h_cp_h, h_cp_l, h_cp_m, h_cp_a
    ; C0
    dw h_ret_nz, h_pop_bc, h_jp_nz, h_jp, h_call_nz, h_push_bc, h_add_n, h_rst00
    dw h_ret_z, h_ret, h_jp_z, h_cb, h_call_z, h_call, h_adc_n, h_rst08
    ; D0
    dw h_ret_nc, h_pop_de, h_jp_nc, h_out_n_a, h_call_nc, h_push_de, h_sub_n, h_rst10
    dw h_ret_c, h_exx, h_jp_c, h_in_a_n, h_call_c, h_dd, h_sbc_n, h_rst18
    ; E0
    dw h_ret_po, h_pop_hl, h_jp_po, h_ex_sp_hl, h_call_po, h_push_hl, h_and_n, h_rst20
    dw h_ret_pe, h_jp_hl, h_jp_pe, h_ex_de_hl, h_call_pe, h_ed, h_xor_n, h_rst28
    ; F0
    dw h_ret_p, h_pop_af, h_jp_p, h_di, h_call_p, h_push_af, h_or_n, h_rst30
    dw h_ret_m, h_ld_sp_hl, h_jp_m, h_ei, h_call_m, h_fd, h_cp_n, h_rst38
section .text


; =============================================================================
; CB: rotates, shifts, BIT, RES, SET
;
; Compact by design (SPEC.md 71: 8080-era software never issues one, so the
; base table takes the bytes and CB takes the indirections): one of eight
; GETTERS moves the operand into rc_val, cb_oper runs one of 32 OPERATORS on
; it (BL in, BL out, BH scratch, ret-terminated), and one of eight PUTTERS
; stores it back. DDCB/FDCB reach cb_oper with the (IX+d) byte and put it
; back through rc_ea. BIT stores the unchanged byte back, which is harmless.
; The three tables are indexed from the opcode's own fields.
; =============================================================================
cb_prefix:
    xor bh, bh
    mov bl, [si]
    inc si
    mov [RC_ZS_CBOP], bx
    and bl, 7
    shl bx, 1
    jmp [cs:bx+cb_get]

cbg_b:  mov [RC_ZS_VAL], ch
        jmp short cb_do
cbg_c:  mov [RC_ZS_VAL], cl
        jmp short cb_do
cbg_d:  mov [RC_ZS_VAL], dh
        jmp short cb_do
cbg_e:  mov [RC_ZS_VAL], dl
        jmp short cb_do
cbg_h:  mov bx, di
        mov [RC_ZS_VAL], bh
        jmp short cb_do
cbg_l:  mov bx, di
        mov [RC_ZS_VAL], bl
        jmp short cb_do
cbg_m:  mov bl, [di]
        mov [RC_ZS_VAL], bl
        jmp short cb_do
cbg_a:  mov [RC_ZS_VAL], al
cb_do:
    call cb_oper
    mov bx, [RC_ZS_CBOP]
    and bl, 7
    shl bx, 1
    jmp [cs:bx+cb_put]

cbp_b:  mov ch, [RC_ZS_VAL]
        jmp rc_next
cbp_c:  mov cl, [RC_ZS_VAL]
        jmp rc_next
cbp_d:  mov dh, [RC_ZS_VAL]
        jmp rc_next
cbp_e:  mov dl, [RC_ZS_VAL]
        jmp rc_next
cbp_h:  mov bx, di
        mov bh, [RC_ZS_VAL]
        mov di, bx
        jmp rc_next
cbp_l:  mov bx, di
        mov bl, [RC_ZS_VAL]
        mov di, bx
        jmp rc_next
cbp_m:  mov bl, [RC_ZS_VAL]
        mov [di], bl
        jmp rc_next
cbp_a:  mov al, [RC_ZS_VAL]
        jmp rc_next

; cb_oper - run operator (rc_cbop >> 3) on rc_val; clobbers BX
cb_oper:
    mov bx, [RC_ZS_CBOP]
    and bl, 0F8h
    shr bl, 1
    shr bl, 1
    mov bx, [cs:bx+cb_op]
    mov [RC_ZS_OP], bx
    mov bl, [RC_ZS_VAL]
    call [RC_ZS_OP]
    mov [RC_ZS_VAL], bl
    ret

; the operators: BL in, BL out, flags in AH, BH scratch
cbo_rlc:    rol bl, 1
            sbb bh, bh                ; bh = -C
            test bl, bl               ; S Z P from the result, CF = 0
            lahf
            and ah, 0C4h
            sub ah, bh                ; + C
            ret
cbo_rrc:    ror bl, 1
            sbb bh, bh
            test bl, bl
            lahf
            and ah, 0C4h
            sub ah, bh
            ret
cbo_rl:     shr ah, 1                 ; CF = C in
            rcl bl, 1
            sbb bh, bh
            test bl, bl
            lahf
            and ah, 0C4h
            sub ah, bh
            ret
cbo_rr:     shr ah, 1
            rcr bl, 1
            sbb bh, bh
            test bl, bl
            lahf
            and ah, 0C4h
            sub ah, bh
            ret
cbo_sla:    shl bl, 1                 ; shl/sar/shr set S Z P C themselves
            lahf
            and ah, 0C5h
            ret
cbo_sra:    sar bl, 1
            lahf
            and ah, 0C5h
            ret
cbo_sll:    stc                       ; undocumented: SLA with bit 0 set
            rcl bl, 1
            sbb bh, bh
            test bl, bl
            lahf
            and ah, 0C4h
            sub ah, bh
            ret
cbo_srl:    shr bl, 1
            lahf
            and ah, 0C5h
            ret
; BIT b: Z = !bit, S = bit 7 tested and set, P/V = Z, H = 1, N = 0, C kept -
; which is exactly what `test` leaves in SF ZF PF for a one-bit mask
%macro CBBIT 1
            mov bh, ah
            test bl, %1
            lahf
            and ah, 0C4h
            or ah, 10h
            and bh, 1
            or ah, bh
            ret
%endmacro
cbo_bit0:   CBBIT 01h
cbo_bit1:   CBBIT 02h
cbo_bit2:   CBBIT 04h
cbo_bit3:   CBBIT 08h
cbo_bit4:   CBBIT 10h
cbo_bit5:   CBBIT 20h
cbo_bit6:   CBBIT 40h
cbo_bit7:   CBBIT 80h
cbo_res0:   and bl, ~01h
            ret
cbo_res1:   and bl, ~02h
            ret
cbo_res2:   and bl, ~04h
            ret
cbo_res3:   and bl, ~08h
            ret
cbo_res4:   and bl, ~10h
            ret
cbo_res5:   and bl, ~20h
            ret
cbo_res6:   and bl, ~40h
            ret
cbo_res7:   and bl, ~80h
            ret
cbo_set0:   or bl, 01h
            ret
cbo_set1:   or bl, 02h
            ret
cbo_set2:   or bl, 04h
            ret
cbo_set3:   or bl, 08h
            ret
cbo_set4:   or bl, 10h
            ret
cbo_set5:   or bl, 20h
            ret
cbo_set6:   or bl, 40h
            ret
cbo_set7:   or bl, 80h
            ret

section .rodata
align 2
cb_get: dw cbg_b, cbg_c, cbg_d, cbg_e, cbg_h, cbg_l, cbg_m, cbg_a
cb_put: dw cbp_b, cbp_c, cbp_d, cbp_e, cbp_h, cbp_l, cbp_m, cbp_a
cb_op:  dw cbo_rlc, cbo_rrc, cbo_rl, cbo_rr, cbo_sla, cbo_sra, cbo_sll, cbo_srl
        dw cbo_bit0, cbo_bit1, cbo_bit2, cbo_bit3, cbo_bit4, cbo_bit5, cbo_bit6, cbo_bit7
        dw cbo_res0, cbo_res1, cbo_res2, cbo_res3, cbo_res4, cbo_res5, cbo_res6, cbo_res7
        dw cbo_set0, cbo_set1, cbo_set2, cbo_set3, cbo_set4, cbo_set5, cbo_set6, cbo_set7
section .text

; =============================================================================
; DD / FD: the index registers
;
; h_dd/h_fd bank the register's value in rc_ixv and its address in rc_ixptr
; and dispatch through dd_tab, whose entries are the BASE handlers wherever
; the opcode has no HL form (the prefix is then a no-op, as on the silicon
; for the documented set) and the handlers below where it has: (IX+d) through
; rc_ixea, and the HL-register forms by swapping HL with the index register
; around a base-shaped body (IXSWAP).
; =============================================================================
dd_dispatch:
    xor bh, bh
    mov bl, [si]
    inc si
    shl bx, 1
    jmp [cs:bx+dd_tab]

; rc_ixea - BX = IX+d (or IY+d), the displacement byte consumed. Everything
; but BX and SI preserved. A near call, because it is wanted from ~30 places.
rc_ixea:
    xor bh, bh
    mov bl, [si]
    inc si
    test bl, bl
    jns .p
    dec bh
.p: add bx, [RC_ZS_IXV]
    ret

; the HL-register forms, base bodies between two swaps
d_add_bc:   IXSWAP
            push ax
            mov bx, di
            xor bx, cx
            add di, cx
            jmp short d_add_fin
d_add_de:   IXSWAP
            push ax
            mov bx, di
            xor bx, dx
            add di, dx
            jmp short d_add_fin
d_add_ix:   IXSWAP
            push ax
            xor bx, bx
            add di, di
            jmp short d_add_fin
d_add_sp:   IXSWAP
            push ax
            mov bx, di
            xor bx, bp
            add di, bp
d_add_fin:  lahf
            xor bx, di
            and bh, 10h
            and ah, 1
            or ah, bh
            pop bx
            and bh, 0C4h
            or ah, bh
            IXSWAP
            jmp rc_next
d_ld_ix_nn: IXSWAP
            mov di, [si]
            inc si
            inc si
            IXSWAP
            jmp rc_next
d_ld_nn_ix: IXSWAP
            mov bx, [si]
            inc si
            inc si
            mov [bx], di
            IXSWAP
            jmp rc_next
d_ld_ix_nn_: IXSWAP
            mov bx, [si]
            inc si
            inc si
            mov di, [bx]
            IXSWAP
            jmp rc_next
d_inc_ix:   mov bx, [RC_ZS_IXP]
            inc word [cs:bx]
            jmp rc_next
d_dec_ix:   mov bx, [RC_ZS_IXP]
            dec word [cs:bx]
            jmp rc_next
d_pop_ix:   IXSWAP
            mov di, [ds:bp]
            inc bp
            inc bp
            IXSWAP
            jmp rc_next
d_push_ix:  mov bx, [RC_ZS_IXV]
            PUSH16 bx
d_ex_sp_ix: IXSWAP
            xchg di, [ds:bp]
            IXSWAP
            jmp rc_next
d_jp_ix:    mov si, [RC_ZS_IXV]
            COUNT
d_ld_sp_ix: mov bp, [RC_ZS_IXV]
            jmp rc_next

; the (IX+d) forms
d_ld_b_x:   call rc_ixea
            mov ch, [bx]
            jmp rc_next
d_ld_c_x:   call rc_ixea
            mov cl, [bx]
            jmp rc_next
d_ld_d_x:   call rc_ixea
            mov dh, [bx]
            jmp rc_next
d_ld_e_x:   call rc_ixea
            mov dl, [bx]
            jmp rc_next
d_ld_h_x:   call rc_ixea
            xchg bx, di               ; DI = the address, BX = HL
            mov bh, [di]
            mov di, bx
            jmp rc_next
d_ld_l_x:   call rc_ixea
            xchg bx, di
            mov bl, [di]
            mov di, bx
            jmp rc_next
d_ld_a_x:   call rc_ixea
            mov al, [bx]
            jmp rc_next
d_ld_x_b:   call rc_ixea
            mov [bx], ch
            jmp rc_next
d_ld_x_c:   call rc_ixea
            mov [bx], cl
            jmp rc_next
d_ld_x_d:   call rc_ixea
            mov [bx], dh
            jmp rc_next
d_ld_x_e:   call rc_ixea
            mov [bx], dl
            jmp rc_next
d_ld_x_h:   call rc_ixea
            xchg bx, di
            mov [di], bh
            mov di, bx
            jmp rc_next
d_ld_x_l:   call rc_ixea
            xchg bx, di
            mov [di], bl
            mov di, bx
            jmp rc_next
d_ld_x_a:   call rc_ixea
            mov [bx], al
            jmp rc_next
d_ld_x_n:   call rc_ixea
            xchg bx, di               ; DI = the address, BX = HL
            push bx
            mov bl, [si]
            inc si
            mov [di], bl
            pop di
            jmp rc_next
d_cb:       call rc_ixea              ; DD CB d op
            mov [RC_ZS_EA], bx
            mov bl, [bx]
            mov [RC_ZS_VAL], bl
            xor bh, bh
            mov bl, [si]
            inc si
            mov [RC_ZS_CBOP], bx
            call cb_oper
            mov bx, [RC_ZS_EA]
            push ax
            mov al, [RC_ZS_VAL]
            mov [bx], al
            pop ax
            jmp rc_next

; the IXH/IXL forms (undocumented, and ZEXDOC exercises them - SPEC.md 71):
; the halves of the index register, addressed through rc_ixptr as bytes
; (+0 = the low half IXL, +1 = the high half IXH), so one macro serves both
%macro DX_LD_R_X 2                      ; LD %1, IXH/IXL   (%2 = 0|1)
            mov bx, [RC_ZS_IXP]
            mov %1, [cs:bx+%2]
            jmp rc_next
%endmacro
%macro DX_LD_X_R 2                      ; LD IXH/IXL, %1
            mov bx, [RC_ZS_IXP]
            mov [cs:bx+%2], %1
            jmp rc_next
%endmacro
%macro DX_ALU 2                         ; ALU A, IXH/IXL  (%1 = the ALU_ macro)
            mov bx, [RC_ZS_IXP]
            %1 byte [cs:bx+%2]
%endmacro
d_ld_b_xh:  DX_LD_R_X ch, 1
d_ld_b_xl:  DX_LD_R_X ch, 0
d_ld_c_xh:  DX_LD_R_X cl, 1
d_ld_c_xl:  DX_LD_R_X cl, 0
d_ld_d_xh:  DX_LD_R_X dh, 1
d_ld_d_xl:  DX_LD_R_X dh, 0
d_ld_e_xh:  DX_LD_R_X dl, 1
d_ld_e_xl:  DX_LD_R_X dl, 0
d_ld_a_xh:  DX_LD_R_X al, 1
d_ld_a_xl:  DX_LD_R_X al, 0
d_ld_xh_b:  DX_LD_X_R ch, 1
d_ld_xh_c:  DX_LD_X_R cl, 1
d_ld_xh_d:  DX_LD_X_R dh, 1
d_ld_xh_e:  DX_LD_X_R dl, 1
d_ld_xh_a:  DX_LD_X_R al, 1
d_ld_xl_b:  DX_LD_X_R ch, 0
d_ld_xl_c:  DX_LD_X_R cl, 0
d_ld_xl_d:  DX_LD_X_R dh, 0
d_ld_xl_e:  DX_LD_X_R dl, 0
d_ld_xl_a:  DX_LD_X_R al, 0
d_ld_xh_xl: mov bx, [RC_ZS_IXP]
            push cx
            mov cl, [cs:bx]
            mov [cs:bx+1], cl
            pop cx
            jmp rc_next
d_ld_xl_xh: mov bx, [RC_ZS_IXP]
            push cx
            mov cl, [cs:bx+1]
            mov [cs:bx], cl
            pop cx
            jmp rc_next
d_ld_xh_n:  mov bx, [RC_ZS_IXP]
            push cx
            mov cl, [si]
            inc si
            mov [cs:bx+1], cl
            pop cx
            jmp rc_next
d_ld_xl_n:  mov bx, [RC_ZS_IXP]
            push cx
            mov cl, [si]
            inc si
            mov [cs:bx], cl
            pop cx
            jmp rc_next
d_inc_xh:   mov bx, [RC_ZS_IXP]
            INC8 byte [cs:bx+1]
d_inc_xl:   mov bx, [RC_ZS_IXP]
            INC8 byte [cs:bx]
d_dec_xh:   mov bx, [RC_ZS_IXP]
            DEC8 byte [cs:bx+1]
d_dec_xl:   mov bx, [RC_ZS_IXP]
            DEC8 byte [cs:bx]
d_add_xh:   DX_ALU ALU_ADD, 1
d_add_xl:   DX_ALU ALU_ADD, 0
d_adc_xh:   DX_ALU ALU_ADC, 1
d_adc_xl:   DX_ALU ALU_ADC, 0
d_sub_xh:   DX_ALU ALU_SUB, 1
d_sub_xl:   DX_ALU ALU_SUB, 0
d_sbc_xh:   DX_ALU ALU_SBC, 1
d_sbc_xl:   DX_ALU ALU_SBC, 0
d_and_xh:   DX_ALU ALU_AND, 1
d_and_xl:   DX_ALU ALU_AND, 0
d_xor_xh:   DX_ALU ALU_XOR, 1
d_xor_xl:   DX_ALU ALU_XOR, 0
d_or_xh:    DX_ALU ALU_OR, 1
d_or_xl:    DX_ALU ALU_OR, 0
d_cp_xh:    DX_ALU ALU_CP, 1
d_cp_xl:    DX_ALU ALU_CP, 0

section .rodata
align 2
dd_tab:
    ; 00
    dw h_nop, h_ld_bc_nn, h_ld_bc_a, h_inc_bc, h_inc_b, h_dec_b, h_ld_b_n, h_rlca
    dw h_ex_af, d_add_bc, h_ld_a_bc, h_dec_bc, h_inc_c, h_dec_c, h_ld_c_n, h_rrca
    ; 10
    dw h_djnz, h_ld_de_nn, h_ld_de_a, h_inc_de, h_inc_d, h_dec_d, h_ld_d_n, h_rla
    dw h_jr, d_add_de, h_ld_a_de, h_dec_de, h_inc_e, h_dec_e, h_ld_e_n, h_rra
    ; 20
    dw h_jr_nz, d_ld_ix_nn, d_ld_nn_ix, d_inc_ix, d_inc_xh, d_dec_xh, d_ld_xh_n, h_daa
    dw h_jr_z, d_add_ix, d_ld_ix_nn_, d_dec_ix, d_inc_xl, d_dec_xl, d_ld_xl_n, h_cpl
    ; 30
    dw h_jr_nc, h_ld_sp_nn, h_ld_nn_a, h_inc_sp, h_inc_x, h_dec_x, d_ld_x_n, h_scf
    dw h_jr_c, d_add_sp, h_ld_a_nn, h_dec_sp, h_inc_a, h_dec_a, h_ld_a_n, h_ccf
    ; 40
    dw h_nop, h_ld_b_c, h_ld_b_d, h_ld_b_e, d_ld_b_xh, d_ld_b_xl, d_ld_b_x, h_ld_b_a
    dw h_ld_c_b, h_nop, h_ld_c_d, h_ld_c_e, d_ld_c_xh, d_ld_c_xl, d_ld_c_x, h_ld_c_a
    ; 50
    dw h_ld_d_b, h_ld_d_c, h_nop, h_ld_d_e, d_ld_d_xh, d_ld_d_xl, d_ld_d_x, h_ld_d_a
    dw h_ld_e_b, h_ld_e_c, h_ld_e_d, h_nop, d_ld_e_xh, d_ld_e_xl, d_ld_e_x, h_ld_e_a
    ; 60
    dw d_ld_xh_b, d_ld_xh_c, d_ld_xh_d, d_ld_xh_e, h_nop, d_ld_xh_xl, d_ld_h_x, d_ld_xh_a
    dw d_ld_xl_b, d_ld_xl_c, d_ld_xl_d, d_ld_xl_e, d_ld_xl_xh, h_nop, d_ld_l_x, d_ld_xl_a
    ; 70
    dw d_ld_x_b, d_ld_x_c, d_ld_x_d, d_ld_x_e, d_ld_x_h, d_ld_x_l, h_halt, d_ld_x_a
    dw h_ld_a_b, h_ld_a_c, h_ld_a_d, h_ld_a_e, d_ld_a_xh, d_ld_a_xl, d_ld_a_x, h_nop
    ; 80
    dw h_add_b, h_add_c, h_add_d, h_add_e, d_add_xh, d_add_xl, h_add_x, h_add_a
    dw h_adc_b, h_adc_c, h_adc_d, h_adc_e, d_adc_xh, d_adc_xl, h_adc_x, h_adc_a
    ; 90
    dw h_sub_b, h_sub_c, h_sub_d, h_sub_e, d_sub_xh, d_sub_xl, h_sub_x, h_sub_a
    dw h_sbc_b, h_sbc_c, h_sbc_d, h_sbc_e, d_sbc_xh, d_sbc_xl, h_sbc_x, h_sbc_a
    ; A0
    dw h_and_b, h_and_c, h_and_d, h_and_e, d_and_xh, d_and_xl, h_and_x, h_and_a
    dw h_xor_b, h_xor_c, h_xor_d, h_xor_e, d_xor_xh, d_xor_xl, h_xor_x, h_xor_a
    ; B0
    dw h_or_b, h_or_c, h_or_d, h_or_e, d_or_xh, d_or_xl, h_or_x, h_or_a
    dw h_cp_b, h_cp_c, h_cp_d, h_cp_e, d_cp_xh, d_cp_xl, h_cp_x, h_cp_a
    ; C0
    dw h_ret_nz, h_pop_bc, h_jp_nz, h_jp, h_call_nz, h_push_bc, h_add_n, h_rst00
    dw h_ret_z, h_ret, h_jp_z, d_cb, h_call_z, h_call, h_adc_n, h_rst08
    ; D0
    dw h_ret_nc, h_pop_de, h_jp_nc, h_out_n_a, h_call_nc, h_push_de, h_sub_n, h_rst10
    dw h_ret_c, h_exx, h_jp_c, h_in_a_n, h_call_c, h_dd, h_sbc_n, h_rst18
    ; E0
    dw h_ret_po, d_pop_ix, h_jp_po, d_ex_sp_ix, h_call_po, d_push_ix, h_and_n, h_rst20
    dw h_ret_pe, d_jp_ix, h_jp_pe, h_ex_de_hl, h_call_pe, h_ed, h_xor_n, h_rst28
    ; F0
    dw h_ret_p, h_pop_af, h_jp_p, h_di, h_call_p, h_push_af, h_or_n, h_rst30
    dw h_ret_m, d_ld_sp_ix, h_jp_m, h_ei, h_call_m, h_fd, h_cp_n, h_rst38
section .text

; =============================================================================
; ED: the extended set. Only 40h-BFh means anything; the rest is a two-byte
; NOP (cpu1.h's default), so the table covers 40h-BFh and the prefix handler
; folds everything else onto e_nop.
; =============================================================================
ed_prefix:
    xor bh, bh
    mov bl, [si]
    inc si
    sub bl, 40h
    jb e_nop
    cmp bl, 7Fh
    ja e_nop
    shl bx, 1
    jmp [cs:bx+ed_tab]
e_nop:
    jmp rc_next

; IN r,(C): the value is 0 (nothing is connected); S Z H N from it, P = its
; parity (even), C kept: F = (F & 1) | 44h
%macro E_IN 1
            mov %1, 0
            and ah, 1
            or ah, 44h
            jmp rc_next
%endmacro
e_in_b:     E_IN ch
e_in_c:     E_IN cl
e_in_d:     E_IN dh
e_in_e:     E_IN dl
e_in_h:     mov bx, di
            mov bh, 0
            mov di, bx
            and ah, 1
            or ah, 44h
            jmp rc_next
e_in_l:     mov bx, di
            mov bl, 0
            mov di, bx
            and ah, 1
            or ah, 44h
            jmp rc_next
e_in_f:     and ah, 1                 ; ED 70: flags only
            or ah, 44h
            jmp rc_next
e_in_a:     E_IN al
e_out:      jmp rc_next               ; OUT (C),r: nothing to write to

e_sbc_bc:   SBC16 cx
e_sbc_de:   SBC16 dx
e_sbc_hl:   SBC16 di
e_sbc_sp:   SBC16 bp
e_adc_bc:   ADC16 cx
e_adc_de:   ADC16 dx
e_adc_hl:   ADC16 di
e_adc_sp:   ADC16 bp

e_ld_nn_bc: mov bx, [si]
            inc si
            inc si
            mov [bx], cx
            jmp rc_next
e_ld_nn_de: mov bx, [si]
            inc si
            inc si
            mov [bx], dx
            jmp rc_next
e_ld_nn_hl: mov bx, [si]
            inc si
            inc si
            mov [bx], di
            jmp rc_next
e_ld_nn_sp: mov bx, [si]
            inc si
            inc si
            mov [bx], bp
            jmp rc_next
e_ld_bc_nn: mov bx, [si]
            inc si
            inc si
            mov cx, [bx]
            jmp rc_next
e_ld_de_nn: mov bx, [si]
            inc si
            inc si
            mov dx, [bx]
            jmp rc_next
e_ld_hl_nn: mov bx, [si]
            inc si
            inc si
            mov di, [bx]
            jmp rc_next
e_ld_sp_nn: mov bx, [si]
            inc si
            inc si
            mov bp, [bx]
            jmp rc_next

e_neg:      mov bl, al
            mov al, 0
            sub al, bl
            FIN_SUB
e_retn:     mov bx, [cs:_rc_z+RZ_IFF]   ; IFF1 = IFF2, then RET
            mov bh, bl
            shr bl, 1
            and bx, 0201h
            or bl, bh
            and bx, 3
            mov [cs:_rc_z+RZ_IFF], bx
            jmp h_ret
e_im0:      mov word [cs:_rc_z+RZ_IM], 0
            jmp rc_next
e_im1:      mov word [cs:_rc_z+RZ_IM], 1
            jmp rc_next
e_im2:      mov word [cs:_rc_z+RZ_IM], 2
            jmp rc_next
e_ld_i_a:   mov [cs:_rc_z+RZ_I], al
            jmp rc_next
e_ld_r_a:   mov [cs:_rc_z+RZ_R], al
            jmp rc_next
; LD A,I / LD A,R: S Z from the value, H = N = 0, P/V = IFF2, C kept
e_ld_a_i:   mov al, [cs:_rc_z+RZ_I]
            jmp short e_air
e_ld_a_r:   mov al, [cs:_rc_z+RZ_R]     ; bit 7 as LD R,A left it, the rest
            and al, 80h                 ; advancing with the slice count
            mov bl, [RC_ZS_CNT]         ; (SPEC.md 71: not per fetch)
            xor bl, [cs:_rc_z+RZ_R]
            add bl, [cs:_rc_z+RZ_PC]
            and bl, 7Fh
            or al, bl
e_air:      mov bl, ah
            test al, al
            lahf
            and ah, 0C0h
            and bl, 1
            or ah, bl
            test byte [cs:_rc_z+RZ_IFF], 2
            jz .n
            or ah, 4
.n:         jmp rc_next

; RRD / RLD: the nibble rotates through (HL) and A's low nibble
e_rrd:      push cx
            mov bl, [di]              ; m
            mov bh, al                ; a
            mov cl, 4
            shl bh, cl                ; a low nibble -> high
            mov ch, bl
            shr ch, cl                ; m >> 4
            or bh, ch                 ; new m
            and bl, 0Fh
            and al, 0F0h
            or al, bl                 ; new A
            mov [di], bh
            pop cx
            jmp short e_rxd_fin
e_rld:      push cx
            mov bl, [di]              ; m
            mov bh, bl
            mov cl, 4
            shl bh, cl                ; m low nibble -> high
            mov ch, al
            and ch, 0Fh
            or bh, ch                 ; new m = (m << 4) | (a & 0F)
            shr bl, cl                ; m >> 4
            and al, 0F0h
            or al, bl                 ; new A
            mov [di], bh
            pop cx
e_rxd_fin:  mov bl, ah                ; S Z P from A, H = N = 0, C kept
            test al, al
            lahf
            and ah, 0C4h
            and bl, 1
            or ah, bl
            jmp rc_next

; the block moves and compares. LDI/LDD: H = N = 0, P/V = (BC != 0), S Z C
; kept; the repeat forms step PC back two bytes while BC != 0 (a control
; transfer, counted), so a slice can end inside one.
%macro E_LDX 1                          ; %1 = inc|dec
            mov bx, dx
            push ax
            mov al, [di]
            mov [bx], al
            pop ax
            %1 di
            %1 dx
            and ah, 0C1h
            dec cx
            jz %%z
            or ah, 4                  ; (ZF stays 0: the result is not 0)
%%z:                                  ; ZF = (BC == 0) for the repeat forms
%endmacro
e_ldi:      E_LDX inc
            jmp rc_next
e_ldd:      E_LDX dec
            jmp rc_next
e_ldir:     E_LDX inc
            jz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
e_lddr:     E_LDX dec
            jz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
; CPI/CPD: A - (HL) for S Z H, N = 1, P/V = (BC != 0), C kept
%macro E_CPX 1
            mov bl, [di]
            mov bh, ah
            cmp al, bl
            lahf
            and ah, 0D2h
            and bh, 1
            or ah, bh
            %1 di
            dec cx
            jz %%z
            or ah, 4
%%z:
%endmacro
e_cpi:      E_CPX inc
            jmp rc_next
e_cpd:      E_CPX dec
            jmp rc_next
e_cpir:     E_CPX inc                 ; repeat while BC != 0 and A != (HL)
            jz .done
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
e_cpdr:     E_CPX dec
            jz .done
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
; INI/IND/OUTI/OUTD and their repeats: nothing is connected, so an input
; stores 0 and an output reads and drops; B decrements, HL steps; Z from B,
; N = 1 (the documented flags of the group), the rest of F kept
%macro E_IOX 2                          ; %1 = inc|dec, %2 = 1 for input
  %if %2
            mov byte [di], 0
  %endif
            %1 di
            and ah, 0BDh              ; clear Z, set N...
            or ah, 2
            dec ch
            jnz %%nz
            or ah, 40h                ; ...Z from the new B
%%nz:
%endmacro
e_ini:      E_IOX inc, 1
            jmp rc_next
e_ind:      E_IOX dec, 1
            jmp rc_next
e_outi:     E_IOX inc, 0
            jmp rc_next
e_outd:     E_IOX dec, 0
            jmp rc_next
e_inir:     E_IOX inc, 1
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
e_indr:     E_IOX dec, 1
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
e_otir:     E_IOX inc, 0
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next
e_otdr:     E_IOX dec, 0
            test ah, 40h
            jnz .done
            dec si
            dec si
            COUNT
.done:      jmp rc_next

section .rodata
align 2
ed_tab:
    ; 40
    dw e_in_b, e_out, e_sbc_bc, e_ld_nn_bc, e_neg, e_retn, e_im0, e_ld_i_a
    dw e_in_c, e_out, e_adc_bc, e_ld_bc_nn, e_neg, e_retn, e_im0, e_ld_r_a
    ; 50
    dw e_in_d, e_out, e_sbc_de, e_ld_nn_de, e_neg, e_retn, e_im1, e_ld_a_i
    dw e_in_e, e_out, e_adc_de, e_ld_de_nn, e_neg, e_retn, e_im2, e_ld_a_r
    ; 60
    dw e_in_h, e_out, e_sbc_hl, e_ld_nn_hl, e_neg, e_retn, e_im0, e_rrd
    dw e_in_l, e_out, e_adc_hl, e_ld_hl_nn, e_neg, e_retn, e_im0, e_rld
    ; 70
    dw e_in_f, e_out, e_sbc_sp, e_ld_nn_sp, e_neg, e_retn, e_im1, e_nop
    dw e_in_a, e_out, e_adc_sp, e_ld_sp_nn, e_neg, e_retn, e_im2, e_nop
    ; 80
    dw e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop
    dw e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop
    ; 90
    dw e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop
    dw e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop, e_nop
    ; A0
    dw e_ldi, e_cpi, e_ini, e_outi, e_nop, e_nop, e_nop, e_nop
    dw e_ldd, e_cpd, e_ind, e_outd, e_nop, e_nop, e_nop, e_nop
    ; B0
    dw e_ldir, e_cpir, e_inir, e_otir, e_nop, e_nop, e_nop, e_nop
    dw e_lddr, e_cpdr, e_indr, e_otdr, e_nop, e_nop, e_nop, e_nop
section .text
