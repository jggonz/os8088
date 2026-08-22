; =============================================================================
; os8088 - apps/c64/c64cpu.inc          the 6510 core (C64-SPEC §4)
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
; VICE is Copyright (C) 1996-2025 the VICE team, GPL-2-or-later; apps/c64/ is
; GPL-2-or-later and apps/c64/COPYING is the licence text. Nothing of VICE's
; source is vendored: the opcode semantics and the per-opcode cycle costs come
; from src/6510core.c and src/interrupt.c, and are reimplemented here.
;
; %included by apps/c64/c64.asm after the compiled C, and by
; hosttest/c64cputest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS (C64-SPEC §4.6, the rcz80test precedent).
;
; -----------------------------------------------------------------------------
; WAVE 1 IS THE SHELL AND THE TABLE, AND SAYS SO.
; -----------------------------------------------------------------------------
; What is here now: the register file `_c64_m`, the scratch layout in the
; emulated machine's own top-of-memory bytes (C64-SPEC §3.5), the
; entry/exit shell of `_c64_run` - the register plan of 4.1 loaded and put
; back, DS banked to the RAM claim, the fetch ES biased - and the 256-entry
; dispatch table. Every entry of that table points at `.jam` today, so a
; `_c64_run` on a wave-1 build answers C64_RUN_JAM on its first fetch and the
; status row says the machine is halted. That is an HONEST empty core: it
; executes nothing and claims nothing, and wave 2 fills the 256 handlers and
; the cycle table in (docs/C64-PORT-PLAN.md wave 2).
;
; NOTHING IN WAVE 1 CALLS `_c64_run`. The wave-1 screen is composed from a
; boot screen poked into the matrix by hand (c64.c, c64_poke_boot), which is
; what lets the composer, the flush, the shadow and the shift test be gated
; before there is a CPU to blame.
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. ES is the KERNEL's on entry to every callback (SPEC.md 20.4) and is
; put back before returning.
; =============================================================================

section .bss

; --- the register file (C64-SPEC §4.1) --------------------------------
; Reached from C as `extern struct c64_mach c64_m;` - the field order below IS
; the struct's, and the two cannot drift without the C reading the wrong word.
_c64_m:
    resw 1                          ; CM_RAMSEG  the 64KB RAM claim
    resw 1                          ; CM_ROMSEG  the 20KB ROM claim
    resw 1                          ; CM_PC
    resw 1                          ; CM_A       (low byte)
    resw 1                          ; CM_X
    resw 1                          ; CM_Y
    resw 1                          ; CM_S
    resw 1                          ; CM_P
    resw 1                          ; CM_CNT     wall-slice cycles left
    resw 1                          ; CM_REASON  C64_RUN_* of the last return

CM_RAMSEG equ 0
CM_ROMSEG equ 2
CM_PC     equ 4
CM_A      equ 6
CM_X      equ 8
CM_Y      equ 10
CM_S      equ 12
CM_P      equ 14
CM_CNT    equ 16
CM_REASON equ 18

section .text

; --- what _c64_run answers (C64-SPEC §4.5) ----------------------------
C64_RUN_SLICE equ 0                 ; the wall-slice budget was spent
C64_RUN_JAM   equ 1                 ; a KIL/JAM opcode; the machine stops

; --- the core's scratch, in the emulated machine (C64-SPEC §3.5) ------
; 58 bytes at the top of the C64's own RAM, BELOW the six vector bytes: a hot
; counter in the package's bss is a TCG slow path under QEMU when that page
; also holds translated code, and it cost RUNCPM 5x on the whole Z80 core
; (LESSONS.md 13). $FFFA-$FFFF stay real RAM, because a program that banks the
; KERNAL out puts its own vectors there.
;
; Two stated deviations follow, and hosttest/c64cputest.asm holds a case for
; each: a read of $FFC0-$FFF9 in an all-RAM map reads the scratch, and a write
; there is dropped.
C64_SCR_BASE  equ 0xFFC0
C64_SCR_DIRTY equ 0x00              ; 32 bytes: the 256-page dirty bitmap (9.2)
C64_SCR_CYCLO equ 0x20              ; the emulated-cycle counter, low word
C64_SCR_CYCHI equ 0x22              ;  ...and high word (two words, 10.2)
C64_SCR_DEAD  equ 0x24              ; the current slice deadline
C64_SCR_BOUND equ 0x26              ; the next mapping boundary above PC (4.3)
C64_SCR_FES   equ 0x28              ; the cached fetch ES (4.3)
C64_SCR_IRQ   equ 0x2A              ; pending IRQ / NMI flags (4.4)
C64_SCR_WLO   equ 0x2C              ; the write window: the lowest and the
C64_SCR_ANY   equ 0x30              ; ...and one byte that says the core wrote
                                    ;   SOMETHING, so a wake can decide to
                                    ;   flush in one read instead of 32
C64_SCR_WHI   equ 0x2E              ;   highest address written since the last
                                    ;   flush (C64-SPEC §9.2). Four
                                    ;   more instructions on the write path,
                                    ;   and what turns a one-byte poke back
                                    ;   into a one-CELL compose
C64_SCR_WATLO equ 0x32              ; the WATCH RANGE the window is taken over:
C64_SCR_WATHI equ 0x34              ;   mbase .. mbase+999, written by the C
                                    ;   from c64_frame_regs (C64-SPEC §9.2).
                                    ;   Without it the window spans the whole
                                    ;   address space the moment the core runs
                                    ;   - every JSR writes the stack at $01xx
                                    ;   and every BASIC statement zero page and
                                    ;   the variables above $0800 - so within
                                    ;   one slice it covers every matrix row in
                                    ;   full and the whole mechanism degenerates
                                    ;   to "recompose 40 cells"
C64_SCR_END   equ 0x3A              ; ...$FFC0 + 0x3A - 1 = $FFF9

; -----------------------------------------------------------------------------
; int c64_run(unsigned cycles) - run the machine for at most `cycles` 6510
; cycles and answer C64_RUN_SLICE or C64_RUN_JAM (C64-SPEC §4.5).
;
; The register plan of 4.1 is loaded here and put back at .out:
;   A -> AL, P(NZC) -> AH (lahf layout), P(VDIB) -> CH, X -> CL, Y -> DX
;   (DH held 0), PC -> SI, S -> DI, EA scratch -> BX, RAM -> DS, fetch -> ES.
; The dispatch is `xor bh,bh / mov bl,[es:si] / inc si / shl bx,1 /
; jmp [cs:bx+c64_tab]`, and wave 1's table sends all 256 to .jam.
; -----------------------------------------------------------------------------
_c64_run:
    push bp
    mov bp, sp
    push si                         ; [bp-2]
    push di                         ; [bp-4]
    push ds                         ; [bp-6] the PACKAGE segment
    push es                         ; [bp-8] KERNEL_SEG (SPEC.md 20.4)

    mov ax, [bp+4]
    mov [_c64_m+CM_CNT], ax         ; the wall-slice budget

    mov ax, [_c64_m+CM_A]
    mov ah, [_c64_m+CM_P]           ; P(N,Z,C) in lahf layout
    mov cx, [_c64_m+CM_X]
    mov ch, [_c64_m+CM_P+1]         ; P(V,D,I,B) - the byte 4.1 gives CH
    mov dx, [_c64_m+CM_Y]
    mov si, [_c64_m+CM_PC]
    mov di, [_c64_m+CM_S]
    mov es, [_c64_m+CM_RAMSEG]      ; the fetch bias: wave 2 computes it from
                                    ; the bank map and the boundary word
    mov ds, [_c64_m+CM_RAMSEG]

    ; --- the fetch, guarded by the boundary word (C64-SPEC §4.3) ------
    ; Wave 2 puts `cmp si, [C64_SCR_BASE + C64_SCR_BOUND]` here and recomputes
    ; ES and the boundary on a crossing. Wave 1 fetches and dispatches, which
    ; is all the shell has to prove.
.fetch:
    xor bh, bh
    mov bl, [es:si]
    inc si
    shl bx, 1
    jmp [cs:bx+c64_tab]

; --- the only handler wave 1 has: every opcode is a JAM ----------------------
; It is not a stub that pretends: a wave-1 build reports C64_RUN_JAM, the
; status row says the machine is halted, and nothing on the glass claims a
; running C64. Wave 2 replaces the table's 256 entries.
c64_jam:
    dec si                          ; PC points AT the opcode that jammed
    mov bx, C64_RUN_JAM
    jmp short c64_out

c64_slice:                          ; wave 2's budget-exhausted exit
    mov bx, C64_RUN_SLICE

; BX carries the reason; DS is still the RAM claim, so the package's segment
; comes back off the stack FIRST (a BP-relative read addresses SS, which is
; where it was pushed - SPEC.md 1's SS != DS rule used deliberately).
c64_out:
    mov ds, [bp-6]
    mov [_c64_m+CM_REASON], bx
    mov [_c64_m+CM_A], al
    mov byte [_c64_m+CM_A+1], 0
    mov [_c64_m+CM_P], ah
    mov [_c64_m+CM_P+1], ch
    mov [_c64_m+CM_X], cl
    mov byte [_c64_m+CM_X+1], 0
    mov [_c64_m+CM_Y], dx
    mov [_c64_m+CM_PC], si
    mov [_c64_m+CM_S], di
    mov ax, bx                      ; the cdecl answer

    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; --- the 256-entry dispatch table (C64-SPEC §4.1) ---------------------
; Wave 2 replaces the 256 `dw c64_jam` with the handlers. The table is here,
; and it is `cs:`-addressed and read-only.
c64_tab:
%rep 256
    dw c64_jam
%endrep
