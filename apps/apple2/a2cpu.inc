; =============================================================================
; os8088 - apps/apple2/a2cpu.inc     the 6502 core (APPLE2-SPEC section 4)
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
;
; ----------------------------------------------------------------------------
; A DERIVED COPY, AND WHAT THAT MEANS (APPLE2-SPEC section 4.1)
; ----------------------------------------------------------------------------
; This file is a DERIVED COPY of apps/c64/c64cpu.inc - the 6510 core of the
; C64 package, itself a reimplementation of VICE 3.10 (GPL-2-or-later,
; Copyright (C) 1996-2025 the VICE team). That core measured 93% generic NMOS
; 6502; the Apple II model replaces the other 7%, which is the read ladder,
; the write ladder, a2_rebias's four boundaries, the ROM bias constants and
; the two I/O call-out names.
;
; **apps/apple2/ is GPL-2-or-later because of this file.** See
; apps/apple2/COPYING. Nothing of VICE's own source is vendored: what is
; carried is the register plan and the shell, by way of a package in this
; tree. AppleWin (GPL-2-or-later) is the authority on everything II+-specific
; below - the mapping boundaries, the slot answer and the reset line.
;
; THE STATED TRADE (APPLE2-SPEC section 4.1): a bug found in the shared 93%
; must be fixed twice, and the second fix will be forgotten. The mitigation is
; that both cores run the SAME fetched oracles - Klaus Dormann's functional
; test at the same pinned SHA-256, and tools/c64dec.py's 262,144 decimal
; cases - which is `make a2cputest`, and which arrives with the core it gates.
;
; ----------------------------------------------------------------------------
; WHAT IS KEPT, AND WHAT IS REPLACED (APPLE2-SPEC section 4.1)
; ----------------------------------------------------------------------------
; KEPT UNCHANGED: the register plan (A = AL, NZC in AH's `lahf` layout, VDIB
; in CH, X = CL, Y = DL, PC = SI, S = DI as the full $0100+S, EA = BX, DS =
; the RAM claim, ES = the fetch bias, BP = frame and scratch), the entry and
; exit shell, a2_getp/a2_setp, all eight addressing modes, every flag tail,
; binary and decimal ADC and SBC, all 151 legal opcodes and every illegal
; family, the interrupt machinery, the `cs:` cycle table, the 256-entry
; dispatch table, the signed-word countdown, the two-compare boundary-guarded
; fetch, and the stack wrap written as `and di,0x00FF` / `or di,0x0100` (the
; one-line `and di,0x01FF` is wrong, and Dormann's $0D89 is what catches it).
;
; REPLACED: the read ladder, the write ladder, a2_rebias's four boundaries
; ($C000, $C100, $D000, $FFFF) with the two-compare guard and the BLO/BOUND
; pair unchanged - a ceiling alone is wrong, because a backward JMP from the
; Monitor to $0800 leaves PC BELOW the region - the ROM bias constants, and
; the two I/O call-out names.
;
; THERE IS NO ALARM SCHEDULER AT ALL (APPLE2-SPEC section 4.2), and that is a
; real simplification this port states rather than an omission: a bare II+ has
; no timers and no interrupt source, so the wake loop is `r = a2_run(budget)`
; with no min() and no advance. The interrupt machinery is still here whole,
; because BRK and RTI are opcodes and a program uses them.
;
; %included by apps/apple2/apple2.asm after the compiled C, and by
; hosttest/a2cputest.asm - the boot-sector harness that runs THIS text on a
; real x86 with SS != DS (APPLE2-SPEC section 4.4).
;
; All cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; the answer in
; AX; BP, DS, SS:SP and DF preserved; AX BX CX DX SI DI are the caller's to
; lose. ES is the KERNEL's on entry to every callback (SPEC.md 20.4) and is
; put back before returning.
; =============================================================================

section .bss

; --- the register file (APPLE2-SPEC section 4.1) -----------------------------
; Reached from C as `extern struct a2_mach a2_m;` - the field order below IS
; the struct's, and the two cannot drift without the C reading the wrong word.
; AM_P is a REAL 6502 P byte in both directions: the core unpacks it into
; AH/CH on entry and packs it back on exit, once per call, so the C and the
; harness read the register the machine has rather than this file's layout.
_a2_m:
    resw 1                          ; AM_RAMSEG  the 64KB RAM claim
    resw 1                          ; AM_ROMSEG  the ROM PART's base segment
    resw 1                          ; AM_PC
    resw 1                          ; AM_A       (low byte)
    resw 1                          ; AM_X
    resw 1                          ; AM_Y
    resw 1                          ; AM_S       (low byte, 0..255)
    resw 1                          ; AM_P       (a 6502 P byte)
    resw 1                          ; AM_CNT     cycles LEFT of the last run
    resw 1                          ; AM_REASON  A2_RUN_* of the last return

AM_RAMSEG equ 0
AM_ROMSEG equ 2
AM_PC     equ 4
AM_A      equ 6
AM_X      equ 8
AM_Y      equ 10
AM_S      equ 12
AM_P      equ 14
AM_CNT    equ 16
AM_REASON equ 18

section .text

; --- what _a2_run answers (APPLE2-SPEC section 4.2) --------------------------
A2_RUN_SLICE equ 0                  ; the wall-slice budget was spent
A2_RUN_JAM   equ 1                  ; a JAM opcode; the machine stops

; --- the core's scratch, IN THE EMULATED MACHINE (APPLE2-SPEC section 3.3) ---
; 256 bytes at $CF00-$CFFF of the 64KB claim, on C64-SPEC §3.5's mechanism and
; for its reason: a hot counter in the package's own bss is a TCG slow path
; under QEMU when that page also holds translated code, and it cost RUNCPM 5x
; on the whole Z80 core (LESSONS.md 13).
;
; IT HAS ZERO STATED DEVIATIONS, which is strictly cleaner than the C64's
; $FFC0 with its two: on a 48K II+ **nothing between $C000 and $FFFF is ever
; RAM to the emulated machine**, so no program can read the scratch and no
; write can reach it - the write ladder drops everything above $C0FF and the
; read ladder answers the soft switches, the slot ladder or the ROM. THE FETCH
; TAKES THE SAME LADDER: a fetch inside $C000-$CFFF goes through a2_rd_bx and
; is never biased to RAM, which is what stops PC = $CF00 reading the scratch
; as code. `make a2cputest` row 7 is that assertion.
;
; THE CONDITION THAT MAKES THAT TRUE IS WRITTEN HERE BECAUSE IT IS THE THING
; THAT CAN STOP BEING TRUE (APPLE2-SPEC section 3.3): it holds only while
; nothing models a Language Card and no card claims $C800-$CFFF expansion ROM.
; Both are refused by this port (section 10.4). A future wave that adds either
; MUST MOVE THE SCRATCH FIRST.
A2_SCR_BASE  equ 0xCF00
; ...and the SAME address as a SIGNED displacement, for any access that
; reaches it through a base register. NASM 2.16 tightened `number-overflow`:
; `[bx + 0xCF00]` is a 16-bit displacement and 52,992 is outside the signed
; range it insists on, so `-w+error` refuses the line - `byte data exceeds
; bounds`, at a line with no `db` on it. The encoding is identical either way;
; only the spelling moves.
A2_SCR_DISP  equ A2_SCR_BASE - 0x10000

A2_SCR_DIRTY equ 0x00               ; 32 bytes: the 256-page dirty bitmap
                                    ;   (APPLE2-SPEC section 7.5)
A2_SCR_BLO   equ 0x20               ; the LOW edge of the region ES is biased
                                    ;   for. The guard is a RANGE and not a
                                    ;   ceiling, which is what makes a
                                    ;   BACKWARD control transfer safe without
                                    ;   re-biasing on every taken branch: a
                                    ;   JMP from the Monitor at $FF69 to
                                    ;   $0800 leaves PC BELOW the biased
                                    ;   region, not above it
A2_SCR_CARRY equ 0x22               ; cycles a2_cut() took out of DEAD, put
                                    ;   back at the expiry so the accounting
                                    ;   `ran = asked - cnt` stays exact
A2_SCR_DEAD  equ 0x24               ; THE countdown - the hot counter, signed
A2_SCR_BOUND equ 0x26               ; the next mapping boundary above PC
A2_SCR_FES   equ 0x28               ; the cached fetch ES
A2_SCR_IRQ   equ 0x2A               ; bit 0 = the IRQ line is asserted, bit 1 =
                                    ;   an NMI EDGE is pending. A BARE II+ HAS
                                    ;   NEITHER SOURCE (section 4.2): there is
                                    ;   no timer and no interrupting card, so
                                    ;   this byte is written by nothing in
                                    ;   this port and the wake loop has no
                                    ;   alarm scheduler at all. The MACHINERY
                                    ;   stays because BRK and RTI are opcodes
A2_SCR_WLO   equ 0x2C               ; the write window: the lowest and the
A2_SCR_WHI   equ 0x2E               ;   highest address written since the last
                                    ;   flush (APPLE2-SPEC section 7.5)
A2_SCR_ANY   equ 0x30               ; ...and one byte that says the core wrote
                                    ;   SOMETHING, so a wake can decide to
                                    ;   flush in one read instead of 32
A2_SCR_WATLO equ 0x32               ; the WATCH RANGE the window is taken
A2_SCR_WATHI equ 0x34               ;   over: the LIVE DISPLAY PAGE, written by
                                    ;   the C from a2_watch_set. Without it
                                    ;   the window degenerates inside one
                                    ;   slice - every JSR writes the stack at
                                    ;   $01xx and every BASIC statement writes
                                    ;   zero page
A2_SCR_CLKB  equ 0x36               ; THE EMULATED CLOCK'S BASE FOR THIS RUN
                                    ;   (APPLE2-SPEC section 5.1's paddle
                                    ;   one-shots). a2_now() answers
                                    ;   CLKB - DEAD, which is exact INSIDE a
                                    ;   run - where the $C064 read happens -
                                    ;   as well as outside one, because
                                    ;   a2_expire leaves what was not spent in
                                    ;   DEAD. The C sets it to `clock + budget`
                                    ;   before each a2_run and re-reads the
                                    ;   clock after
A2_SCR_END   equ 0x38               ; ...and $CF38-$CFFF is spare. The whole
                                    ;   page is reserved because $CF00-$CFFF
                                    ;   is the expansion-ROM window this port
                                    ;   refuses to model, so reserving less
                                    ;   would buy nothing

; --- the Apple II mapping boundaries (APPLE2-SPEC section 3.2) ---------------
A2_IO_BASE   equ 0xC000             ; $C000-$C0FF  the soft switches
A2_SLOT_BASE equ 0xC100             ; $C100-$CFFF  slot space, answering $FF
A2_ROM_BASE  equ 0xD000             ; $D000-$FFFF  the ROM
; The ROM part's layout as an ADDRESS BIAS: the main ROM is at part offset 0
; and covers $D000-$FFFF, so [es:bx + $3000] with bx = $D000 is offset 0 and
; the 16-bit effective address wraps for everything above it.
A2_ROMB_MAIN equ 0x3000
; ...and as a SEGMENT bias, for the fetch, which cannot carry a displacement:
; ES = romseg - ($D000 >> 4), so ES:PC reads ROM offset PC - $D000. It
; underflows - and reads somewhere else entirely - if the ROM part's base
; segment is below $0D00, which the C's os88_main guards out loud rather than
; assuming (APPLE2-SPEC section 1.5).
A2_ROM_BIAS  equ A2_ROM_BASE >> 4
A2_ROMS_MAIN equ A2_ROM_BIAS

; --- the frame scratch (the register plan's twelve bytes below BP) -----------
CT_B equ -10                        ; a general byte temporary
CT_A equ -11                        ; the decimal path's A
CT_M equ -12                        ; ...its operand
CT_C equ -13                        ; ...its carry in
CT_F equ -14                        ; ...the AH it is building
CT_T equ -15                        ; ...its low-nibble sum / half borrow
CT_W equ -18                        ; a WORD temporary (the branch's old PC)

; =============================================================================
; MACROS
; =============================================================================

; the stack page: DI is 0x0100+S throughout (APPLE2-SPEC section 4.1)
%macro PUSHB 1
    mov [di], %1
    call a2_stkdirty
    dec di
    or  di, 0x0100
%endmacro

%macro POPB 1
    inc di
    and di, 0x00FF                  ; ...AND THEN THE PAGE BACK ON. `and di,
    or  di, 0x0100                  ; 0x01FF` is the obvious one line and it is
    mov %1, [di]                    ; WRONG: 0x0200 & 0x01FF is 0x0000, not
%endmacro                           ; 0x0100, so a pull with S = $FF read byte
                                    ; ZERO of the address space and left every
                                    ; later push writing at $0000 downwards.
                                    ; BASIC boots straight through it - the
                                    ; KERNAL never wraps the stack - and Klaus
                                    ; Dormann's "proper stack wrap around" test
                                    ; at $0D89 is what caught it

; ROMBIAS - point ES at the ROM part biased for the main ROM and deliver the
; byte. It goes through AX and an IMMEDIATE rather than through BX, because
; a2_rebias_go is reached from the middle of a2_ea_abs with half an effective
; address already in BX.
%macro ROMBIAS 1
    push ax
    mov ax, [cs:_a2_m+AM_ROMSEG]
    sub ax, %1
    mov es, ax
    pop ax
    jmp short a2_rebias_go.go
%endmacro

; OPR - leave the operand in DH. `imm` is a fetch; every other mode computes
; the effective address into BX and reads it through the banked reader.
%macro OPR 1
  %ifidni %1, imm
    call a2_fetch
  %else
    call a2_ea_%1
    call a2_rd_bx
  %endif
%endmacro

; =============================================================================
; int a2_run(unsigned cycles)
;
; Run the machine for at most `cycles` 6502 cycles and answer A2_RUN_SLICE or
; A2_RUN_JAM (APPLE2-SPEC section 4.2). a2_m.cnt is left holding what was NOT
; spent (negative by up to one instruction's cost), so the caller's
; `ran = cycles - cnt` is exact.
; =============================================================================
_a2_run:
    push bp
    mov bp, sp
    push si                         ; [bp-2]
    push di                         ; [bp-4]
    push ds                         ; [bp-6] the PACKAGE segment
    push es                         ; [bp-8] KERNEL_SEG (SPEC.md 20.4)
    sub sp, 12                      ; the CT_* scratch

    mov ax, [bp+4]
    mov si, [cs:_a2_m+AM_RAMSEG]
    mov ds, si
    mov [A2_SCR_BASE + A2_SCR_DEAD], ax
    mov word [A2_SCR_BASE + A2_SCR_CARRY], 0
    mov word [A2_SCR_BASE + A2_SCR_BOUND], 0   ; the first fetch re-biases

    mov al, [cs:_a2_m+AM_A]
    mov bl, [cs:_a2_m+AM_P]
    call a2_setp                    ; -> AH (N,Z,C) and CH (V,D,I,B)
    mov cl, [cs:_a2_m+AM_X]
    mov dl, [cs:_a2_m+AM_Y]
    mov si, [cs:_a2_m+AM_PC]
    mov di, [cs:_a2_m+AM_S]
    and di, 0x00FF
    or  di, 0x0100

    ; an interrupt pending at the top of a run is taken before the first
    ; instruction. NOTHING IN THIS PORT RAISES ONE (section 4.2) - a bare II+
    ; has no timer and no interrupting card - but the check is two tests and
    ; the machinery below is what BRK and RTI use.
    test byte [A2_SCR_BASE + A2_SCR_IRQ], 0x02
    jnz a2_do_nmi
    jmp a2_irq_chk

; -----------------------------------------------------------------------------
; THE FETCH AND THE DISPATCH
; -----------------------------------------------------------------------------
a2_next:
    cmp word [A2_SCR_BASE + A2_SCR_DEAD], 0
    jle a2_expire
    cmp si, [A2_SCR_BASE + A2_SCR_BOUND]
    jae .slow
    cmp si, [A2_SCR_BASE + A2_SCR_BLO]
    jb  .slow
    mov bl, [es:si]
    inc si
.disp:
    xor bh, bh
    mov dh, [cs:bx+a2_cyc]
    sub [A2_SCR_BASE + A2_SCR_DEAD], dh
    sbb byte [A2_SCR_BASE + A2_SCR_DEAD + 1], 0
    shl bx, 1
    jmp [cs:bx+a2_tab]
.slow:
    call a2_rebias_go
    mov bl, dh
    jmp short .disp

; a2_fetch - one operand byte, through the same guarded path.
a2_fetch:
    cmp si, [A2_SCR_BASE + A2_SCR_BOUND]
    jae a2_rebias_go
    cmp si, [A2_SCR_BASE + A2_SCR_BLO]
    jb  a2_rebias_go
    mov dh, [es:si]
    inc si
    ret

; a2_rebias_go - PC has left the region ES is biased for. Work out the region,
; set ES, BLO and BOUND, and deliver the byte.
;
; THE FOUR BOUNDARIES ARE $C000, $C100, $D000 AND $FFFF (APPLE2-SPEC section
; 4.1), and the guard is a PAIR - BLO and BOUND - and not a ceiling alone,
; because a backward JMP from the Monitor at $FF69 to $0800 leaves PC BELOW
; the biased region rather than above it.
;
; $C000-$CFFF IS THE TRUE SLOW FETCH PATH and is never biased to RAM. A byte
; there is a soft switch, a slot answering $FF, or the core's own scratch page
; at $CF00 - and none of the three is memory the emulated machine may execute
; out of a bias. Every fetch in that range re-enters here and comes back
; through a2_rd_bx, which is exactly what a fetch out of an I/O page costs and
; what it is meant to cost.
a2_rebias_go:
; a2_rebias_go.iofence - THE $C000 FENCE'S OWN COMPARE, LABELLED because
; hosttest/a2cputest.asm's rows 5 and 7 need a core that has LOST it and the
; only honest way to make one is to move the immediate in the shipping text at
; runtime (APPLE2-SPEC section 4.4: every perturbation reaches the environment
; or the tables, never the source). Moving it to $D000 biases $C000-$CFFF to
; RAM, which is exactly the core that fetches the claim's bytes where a soft
; switch, a slot's $FF or the core's own scratch page belongs - so it is the
; negative control for BOTH of those rows and for nothing else. Nothing in the
; package reaches this name.
.iofence:
    cmp si, A2_IO_BASE
    jb  .low
    cmp si, A2_ROM_BASE
    jb  .slow
    ; --- $D000-$FFFF, the ROM. BOUND is $FFFF and not the wrap, so the last
    ; byte of the address space re-biases and the wrap to $0000 cannot be
    ; fetched through the ROM's bias.
    mov word [A2_SCR_BASE + A2_SCR_BLO], A2_ROM_BASE
    mov word [A2_SCR_BASE + A2_SCR_BOUND], 0xFFFF
    ROMBIAS A2_ROMS_MAIN
.low:
    mov word [A2_SCR_BASE + A2_SCR_BLO], 0
    mov word [A2_SCR_BASE + A2_SCR_BOUND], A2_IO_BASE
    mov es, [cs:_a2_m+AM_RAMSEG]
.go:
    mov [A2_SCR_BASE + A2_SCR_FES], es
    mov dh, [es:si]
    inc si
    ret
.slow:
    ; $C000-$CFFF: the region is left marked "always re-bias" and the byte
    ; comes through the data path's own ladder, so a fetch at $C030 CLICKS the
    ; speaker exactly as a read there does and a fetch at $C100 gets $FF.
    mov word [A2_SCR_BASE + A2_SCR_BOUND], 0
    mov word [A2_SCR_BASE + A2_SCR_BLO], 0
    push bx
    mov bx, si
    call a2_rd_bx
    pop bx
    inc si
    ret

; -----------------------------------------------------------------------------
; THE EXITS
; -----------------------------------------------------------------------------
a2_expire:
    ; put back whatever a2_cut() took out, so `ran = asked - cnt` is exact
    push bx
    mov bx, [A2_SCR_BASE + A2_SCR_CARRY]
    mov word [A2_SCR_BASE + A2_SCR_CARRY], 0
    add [A2_SCR_BASE + A2_SCR_DEAD], bx
    pop bx
    mov word [cs:_a2_m+AM_REASON], A2_RUN_SLICE
    jmp short a2_out

o_jam:
    dec si                          ; PC points AT the opcode that jammed
    mov word [cs:_a2_m+AM_REASON], A2_RUN_JAM

; DS is still the RAM claim here, so the countdown is read before it goes.
a2_out:
    mov [cs:_a2_m+AM_PC], si
    mov bx, [A2_SCR_BASE + A2_SCR_DEAD]
    mov [cs:_a2_m+AM_CNT], bx
    mov bx, di
    and bx, 0x00FF
    mov [cs:_a2_m+AM_S], bx
    mov bl, al
    xor bh, bh
    mov [cs:_a2_m+AM_A], bx
    mov bl, cl
    mov [cs:_a2_m+AM_X], bx
    mov bl, dl
    mov [cs:_a2_m+AM_Y], bx
    call a2_getp                   ; -> BL, and BH is the scratch it uses
    xor bh, bh
    mov [cs:_a2_m+AM_P], bx
    mov ax, [cs:_a2_m+AM_REASON]
    add sp, 12
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; P, PACKED AND UNPACKED
; -----------------------------------------------------------------------------
; a2_getp - AH (N,Z,C) + CH (V,D,I,B) -> a 6502 P byte in BL. Clobbers BH.
a2_getp:
    mov bl, ah
    and bl, 0x81                    ; N and C are already in place
    mov bh, ah
    and bh, 0x40                    ; Z sits at bit 6 and belongs at bit 1
    rol bh, 1
    rol bh, 1
    rol bh, 1
    or  bl, bh
    mov bh, ch
    and bh, 0x5C                    ; V, B, D, I
    or  bl, bh
    or  bl, 0x20                    ; bit 5 always reads 1
    ret

; a2_setp - a 6502 P byte in BL -> AH and CH. Clobbers BH.
a2_setp:
    mov ah, bl
    and ah, 0x81
    mov bh, bl
    and bh, 0x02
    ror bh, 1
    ror bh, 1
    ror bh, 1
    or  ah, bh
    mov ch, bl
    and ch, 0x5C
    ret

; -----------------------------------------------------------------------------
; MEMORY (APPLE2-SPEC section 3.2)
; -----------------------------------------------------------------------------
; a2_rd_bx - the byte at BX, into DH. Preserves everything else, BX included,
; because the read-modify-write opcodes need the address back.
;
; THE FAST PATH IS TWO INSTRUCTIONS AND ONE COMPARE, which is shorter than the
; C64's: an Apple II's 6502 has no $00/$01 processor port to test first.
a2_rd_bx:
    cmp bh, 0xC0
    jae .high
    mov dh, [bx]
    ret
.high:
    cmp bh, 0xD0
    jae .rom
    cmp bh, 0xC1
    jb  a2_io_rd_bx                 ; $C000-$C0FF, the soft switches
    mov dh, 0xFF                    ; $C100-$CFFF, slot space. NOTHING IS
    ret                             ; MAPPED THERE in this port, and an
                                    ; unmapped read answers $FF - apple2emu's
                                    ; own posture, and section 5.2's refusal
                                    ; of the true floating bus with the
                                    ; arithmetic attached
.rom:
    push es
    mov es, [cs:_a2_m+AM_ROMSEG]
    mov dh, [es:bx+A2_ROMB_MAIN]
    pop es
    ret

; a2_wr_bx - DH into the byte at BX, and ONE BIT into the 256-page dirty
; bitmap (APPLE2-SPEC section 7.5). Preserves every register, DH included:
; SLO, RLA, SRE, RRA, DCP and ISB all use the written value afterwards.
;
; THE WRITE FENCE IS $C000 AND NOT $D000. On a 48K II+ everything above $BFFF
; is soft switches, slot space or ROM: $C000-$C0FF calls out, and EVERYTHING
; ABOVE $C0FF IS DROPPED. That is also what makes the core's scratch at $CF00
; safe with ZERO stated deviations (section 3.3), where the C64 needed two.
a2_wr_bx:
    cmp bh, 0xC0
    jae .high
.store:
    mov [bx], dh
    push bx
    push dx
    mov bl, bh                      ; the page
    xor bh, bh
    mov dh, [cs:bx+a2_dmask]
    shr bl, 1
    shr bl, 1
    shr bl, 1
    or  [bx + A2_SCR_DISP + A2_SCR_DIRTY], dh
    pop dx
    pop bx
    mov byte [A2_SCR_BASE + A2_SCR_ANY], 1
    ; the WRITE WINDOW, taken over the WATCH RANGE and not over the address
    ; space (APPLE2-SPEC section 7.5): a JSR writes the stack and a BASIC
    ; statement zero page, so a window taken over everything spans every
    ; character row inside one slice and the per-row intersection degenerates
    ; to "all forty cells".
    cmp bx, [A2_SCR_BASE + A2_SCR_WATLO]
    jb  .out
    cmp bx, [A2_SCR_BASE + A2_SCR_WATHI]
    ja  .out
    cmp bx, [A2_SCR_BASE + A2_SCR_WLO]
    jae .nolo
    mov [A2_SCR_BASE + A2_SCR_WLO], bx
.nolo:
    cmp bx, [A2_SCR_BASE + A2_SCR_WHI]
    jbe .out
    mov [A2_SCR_BASE + A2_SCR_WHI], bx
.out:
    ret
.high:
    cmp bh, 0xC1
    jb  a2_io_wr_bx                 ; $C000-$C0FF, the soft switches
    ret                             ; ...and $C100-$FFFF is slot space and ROM
                                    ; on this machine: a write there is
                                    ; DROPPED, which is the whole of why the
                                    ; scratch page needs no deviation

; a2_stkdirty - a stack push is always RAM and always page 1, so its dirty
; bit is a constant and its window test needs no scratch register at all.
a2_stkdirty:
    or  byte [A2_SCR_BASE + A2_SCR_DIRTY], 0x40
    mov byte [A2_SCR_BASE + A2_SCR_ANY], 1
    cmp di, [A2_SCR_BASE + A2_SCR_WATLO]
    jb  .out
    cmp di, [A2_SCR_BASE + A2_SCR_WATHI]
    ja  .out
    cmp di, [A2_SCR_BASE + A2_SCR_WLO]
    jae .nolo
    mov [A2_SCR_BASE + A2_SCR_WLO], di
.nolo:
    cmp di, [A2_SCR_BASE + A2_SCR_WHI]
    jbe .out
    mov [A2_SCR_BASE + A2_SCR_WHI], di
.out:
    ret

; -----------------------------------------------------------------------------
; THE CALLS OUT (APPLE2-SPEC section 3.2) - $C000-$C0FF, IN BOTH DIRECTIONS
;
; DS is the package's for the length of the call and the RAM claim afterwards;
; ES is the C ABI's to clobber, so it comes back from the cached fetch segment
; rather than being trusted. Every core register survives.
; -----------------------------------------------------------------------------
a2_io_rd_bx:                       ; BX = address -> DH; BX preserved
    push si
    push di
    push cx
    push ax
    push dx
    push bx
    push bx                         ; the cdecl argument
    mov ax, cs
    mov ds, ax
    call _a2_io_rd
    add sp, 2
    mov cl, al                      ; CX is on the stack: CL is free
    pop bx
    pop dx
    pop ax
    mov dh, cl
    pop cx
    pop di
    pop si
    mov ds, [cs:_a2_m+AM_RAMSEG]
; a2_io_rd_esfix / a2_io_wr_esfix - the two instructions that put the fetch
; segment back, LABELLED because hosttest/a2cputest.asm's row 6 needs a
; DELIBERATELY STALE ES and the only honest way to make one is to NOP out the
; reload in the shipping text at runtime (APPLE2-SPEC section 4.4). Nothing in
; the package reaches these names.
a2_io_rd_esfix:
    mov es, [A2_SCR_BASE + A2_SCR_FES]
    ret

a2_io_wr_bx:                       ; BX = address, DH = value; both preserved
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
    call _a2_io_wr
    add sp, 4
    pop bx
    pop dx
    pop ax
    pop cx
    pop di
    pop si
    mov ds, [cs:_a2_m+AM_RAMSEG]
a2_io_wr_esfix:
    mov es, [A2_SCR_BASE + A2_SCR_FES]
    ret

; -----------------------------------------------------------------------------
; ADDRESSING (APPLE2-SPEC section 4.2's page-cross penalty is charged HERE)
;
; The `p` forms are the read modes, which pay one more cycle when the index
; carries into the high byte. The bare forms are the write and read-modify-
; write modes, whose base cost in VICE's src/6510core.c's table already includes it.
; -----------------------------------------------------------------------------
a2_ea_zp:
    call a2_fetch
    mov bl, dh
    xor bh, bh
    ret

a2_ea_zpx:
    call a2_fetch
    add dh, cl                      ; the zero-page wrap is the byte add
    mov bl, dh
    xor bh, bh
    ret

a2_ea_zpy:
    call a2_fetch
    add dh, dl
    mov bl, dh
    xor bh, bh
    ret

a2_ea_abs:
    call a2_fetch
    mov bl, dh
    call a2_fetch
    mov bh, dh
    ret

a2_ea_absx:
    call a2_ea_abs
    add bl, cl
    adc bh, 0
    ret

a2_ea_absy:
    call a2_ea_abs
    add bl, dl
    adc bh, 0
    ret

; (zp,X) - the pointer is read through the banked reader, so a pointer at
; $0000/$0001 is the processor port and not the two RAM bytes under it.
a2_ea_indx:
    call a2_fetch
    add dh, cl
    mov bl, dh
    xor bh, bh
    call a2_rd_bx
    push dx                         ; the low byte, and Y with it
    inc bl                          ; the zero-page wrap of the pointer
    call a2_rd_bx
    mov bh, dh
    pop dx
    mov bl, dh
    ret

a2_ea_indp:                        ; the (zp) pointer, unindexed
    call a2_fetch
    mov bl, dh
    xor bh, bh
    call a2_rd_bx
    push dx
    inc bl
    call a2_rd_bx
    mov bh, dh
    pop dx
    mov bl, dh
    ret

a2_ea_indy:
    call a2_ea_indp
    add bl, dl
    adc bh, 0
    ret

; ...and the three READ forms, which pay the page-cross cycle. They are kept
; together so that every `jnc a2_ea_ret` below is a short forward jump.
a2_ea_absxp:
    call a2_ea_abs
    add bl, cl
    jnc a2_ea_ret
    inc bh
    jmp short a2_ea_pen

a2_ea_absyp:
    call a2_ea_abs
    add bl, dl
    jnc a2_ea_ret
    inc bh
    jmp short a2_ea_pen

a2_ea_indyp:
    call a2_ea_indp
    add bl, dl
    jnc a2_ea_ret
    inc bh
a2_ea_pen:
    sub byte [A2_SCR_BASE + A2_SCR_DEAD], 1
    sbb byte [A2_SCR_BASE + A2_SCR_DEAD + 1], 0
a2_ea_ret:
    ret

; -----------------------------------------------------------------------------
; THE FLAG TAILS
; -----------------------------------------------------------------------------
a2_nz_al:
    and ah, 0x01                    ; C survives; N and Z are recomputed
    or  al, al
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

a2_nz_dh:
    and ah, 0x01
    or  dh, dh
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

a2_nz_cl:
    and ah, 0x01
    or  cl, cl
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

a2_nz_dl:
    and ah, 0x01
    or  dl, dl
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

; after a CMP/CPX/CPY: N and Z from the difference, C is the INVERSE of the
; 8086's borrow
a2_cmpfin:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    jmp a2_next

; after ROL/ROR: AH already holds the new carry in bit 0 (rcl/rcr set no sign
; or zero flag on the 8086, so N and Z are taken from the value)
a2_rot_dh:
    or  dh, dh
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

; =============================================================================
; ARITHMETIC (APPLE2-SPEC section 4.2 - decimal mode included, because Klaus
; Dormann-shaped decimal sweep is row 2 of APPLE2-SPEC section 4.4)
; =============================================================================
a2_adc:
    test ch, 0x08
    jnz a2_adc_dec
    shr ah, 1                       ; CF = C
    adc al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    and ch, 0xBF
    jmp a2_next
.ov:
    lahf
    and ah, 0xC1
    or  ch, 0x40
    jmp a2_next

; Decimal ADC, exactly as the NMOS 6502 does it: Z comes from the BINARY sum,
; N and V from the high nibble BEFORE its decimal adjustment, C from after.
a2_adc_dec:
    mov [bp+CT_A], al
    mov [bp+CT_M], dh
    mov bl, ah
    and bl, 1
    mov [bp+CT_C], bl
    mov byte [bp+CT_F], 0
    mov bh, al                      ; the binary sum, for Z alone
    add bh, dh
    add bh, bl
    jnz .nz
    mov byte [bp+CT_F], 0x40
.nz:
    mov bl, al                      ; t = (A & 15) + (M & 15) + C
    and bl, 0x0F
    mov bh, dh
    and bh, 0x0F
    add bl, bh
    add bl, [bp+CT_C]
    cmp bl, 9
    jbe .t1
    add bl, 6
.t1:
    mov [bp+CT_T], bl
    mov bh, al                      ; h = (A >> 4) + (M >> 4) + (t > 15)
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    mov bl, dh
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    add bh, bl
    cmp byte [bp+CT_T], 0x0F
    jbe .h1
    inc bh
.h1:
    mov bl, bh                      ; the high nibble in place, for N and V
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    test bl, 0x80
    jz  .n0
    or  byte [bp+CT_F], 0x80
.n0:
    mov dh, al                      ; V = (A ^ hi) & ~(A ^ M) & 0x80
    xor dh, bl
    mov al, [bp+CT_A]
    xor al, [bp+CT_M]
    not al
    and dh, al
    and ch, 0xBF
    test dh, 0x80
    jz  .v0
    or  ch, 0x40
.v0:
    cmp bh, 9
    jbe .h2
    add bh, 6
.h2:
    cmp bh, 0x0F
    jbe .c0
    or  byte [bp+CT_F], 0x01
.c0:
    mov al, bh
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    mov dh, [bp+CT_T]
    and dh, 0x0F
    or  al, dh
    mov ah, [bp+CT_F]
    jmp a2_next

a2_sbc:
    test ch, 0x08
    jnz a2_sbc_dec
    shr ah, 1
    cmc                             ; the 6502 borrows on NOT C
    sbb al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    xor ah, 0x01
    and ch, 0xBF
    jmp a2_next
.ov:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    or  ch, 0x40
    jmp a2_next

; Decimal SBC: on the NMOS 6502 every FLAG is the binary subtraction's and
; only A is adjusted, which is why the binary path runs first here.
a2_sbc_dec:
    mov [bp+CT_A], al
    mov [bp+CT_M], dh
    mov bl, ah
    and bl, 1
    mov [bp+CT_C], bl
    shr ah, 1
    cmc
    sbb al, dh
    jo  .ov
    lahf
    and ah, 0xC1
    xor ah, 0x01
    and ch, 0xBF
    jmp short .adj
.ov:
    lahf
    and ah, 0xC1
    xor ah, 0x01
    or  ch, 0x40
.adj:
    mov [bp+CT_F], ah
    mov bl, [bp+CT_A]               ; t = (A & 15) - (M & 15) - (1 - C)
    and bl, 0x0F
    mov bh, [bp+CT_M]
    and bh, 0x0F
    sub bl, bh
    mov bh, 1
    sub bh, [bp+CT_C]
    sub bl, bh
    mov byte [bp+CT_T], 0
    test bl, 0x10
    jz  .t1
    sub bl, 6
    mov byte [bp+CT_T], 1
.t1:
    and bl, 0x0F
    mov [bp+CT_B], bl
    mov bl, [bp+CT_A]               ; h = (A >> 4) - (M >> 4) - half borrow
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov bh, [bp+CT_M]
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    sub bl, bh
    sub bl, [bp+CT_T]
    test bl, 0x10
    jz  .h1
    sub bl, 6
.h1:
    and bl, 0x0F
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    or  bl, [bp+CT_B]
    mov al, bl
    mov ah, [bp+CT_F]
    jmp a2_next

; =============================================================================
; INTERRUPTS (APPLE2-SPEC section 4.4)
; =============================================================================
; a2_delay_one - CLI or PLP has just UNMASKED the line, and a 6502 takes that
; IRQ only after ONE MORE INSTRUCTION (VICE's src/6510core.c:1052, mainc64cpu.c:702).
;
; IT IS DONE BY ENDING THE RUN AFTER THAT INSTRUCTION, not by a flag the
; dispatch has to test. a2_next is the hottest path in this machine - four
; instructions before the fetch - and one more compare there is paid by every
; opcode in every program for a case that arises when an IRQ is pending at a
; CLI. So the countdown is set to 1: the next instruction passes
; `cmp DEAD,0 / jle` and executes, its cost takes DEAD negative, the run
; expires, and the C's slice loop calls a2_run again - whose FIRST act is the
; interrupt check (4.4). The unspent budget moves into CARRY exactly as
; a2_cut does and a2_expire puts it back, so `ran = asked - cnt` is still
; exact and nothing is lost or double-counted.
;
; THE ONE CORNER, STATED: if the countdown is already 1 or less the run is
; ending anyway, and the IRQ is then taken at the top of the next run with no
; instruction in between - one instruction early, at a slice boundary the C
; chose. The alternative is the compare on the hot path.
a2_delay_one:
    test byte [A2_SCR_BASE + A2_SCR_IRQ], 0x01
    jz  a2_next                    ; no line asserted: nothing to delay
    test ch, 0x04
    jnz a2_next                    ; ...and I is set after all
    mov bx, [A2_SCR_BASE + A2_SCR_DEAD]
    cmp bx, 2
    jl  a2_next                    ; the run ends by itself (the corner above)
    dec bx                          ; leave exactly one cycle of budget
    add [A2_SCR_BASE + A2_SCR_CARRY], bx
    mov word [A2_SCR_BASE + A2_SCR_DEAD], 1
    jmp a2_next

; a2_irq_chk - taken at the top of a run and after the instructions that can
; change the I flag. An IRQ is due when the line is asserted and I is clear.
a2_irq_chk:
    test byte [A2_SCR_BASE + A2_SCR_IRQ], 0x01
    jz  a2_next
    test ch, 0x04
    jnz a2_next
a2_do_irq:
    mov bx, si
    PUSHB bh
    PUSHB bl
    call a2_getp
    and bl, 0xEF                    ; B is clear on a hardware interrupt
    PUSHB bl
    or  ch, 0x04                    ; I
    mov bx, 0xFFFE
    jmp short a2_vector

a2_do_nmi:
    and byte [A2_SCR_BASE + A2_SCR_IRQ], 0xFD   ; the EDGE is consumed
    mov bx, si
    PUSHB bh
    PUSHB bl
    call a2_getp
    and bl, 0xEF
    PUSHB bl
    or  ch, 0x04
    mov bx, 0xFFFA
; a2_vector - AN INTERRUPT'S seven cycles and the vector. BRK does NOT come
; in here: it is an OPCODE, so the dispatch has already charged its table cost
; of 7 (VICE's src/6510core.c:998 charges BRK once), and entering here charged seven
; more - 14 for a BRK, which is exactly the kind of defect a machine still
; boots with. It jumps to a2_vec_load instead.
a2_vector:
    sub word [A2_SCR_BASE + A2_SCR_DEAD], 7   ; a WORD subtract: the borrow
                                                ; is already in it
a2_vec_load:
    call a2_rd_bx
    mov [bp+CT_B], dh
    inc bx
    call a2_rd_bx
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp a2_next

o_brk:
    inc si                          ; BRK is two bytes: the padding is skipped
    mov bx, si
    PUSHB bh
    PUSHB bl
    call a2_getp
    or  bl, 0x10                    ; B is SET on a software break
    PUSHB bl
    or  ch, 0x04
    mov bx, 0xFFFE
    jmp short a2_vec_load          ; ...and NOT a2_vector: BRK's seven
                                    ; cycles are the dispatch's table entry,
                                    ; charged once (VICE's src/6510core.c:998)

o_rti:
    POPB bl
    call a2_setp
    POPB bl
    mov [bp+CT_B], bl
    POPB bh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp a2_irq_chk

o_rts:
    POPB bl
    mov [bp+CT_B], bl
    POPB bh
    mov bl, [bp+CT_B]
    inc bx
    mov si, bx
    jmp a2_next

o_jsr:
    call a2_fetch
    mov [bp+CT_B], dh
    mov bx, si                      ; PC is ON the high byte: the 6502 pushes
    PUSHB bh                        ; the return address MINUS ONE
    PUSHB bl
    call a2_fetch
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp a2_next

; =============================================================================
; THE HANDLERS
; =============================================================================

; --- loads and stores --------------------------------------------------------
%macro M_LDA 1
o_lda_%1:
    OPR %1
    mov al, dh
    jmp a2_nz_al
%endmacro
M_LDA imm
M_LDA zp
M_LDA zpx
M_LDA abs
M_LDA absxp
M_LDA absyp
M_LDA indx
M_LDA indyp

%macro M_LDX 1
o_ldx_%1:
    OPR %1
    mov cl, dh
    jmp a2_nz_cl
%endmacro
M_LDX imm
M_LDX zp
M_LDX zpy
M_LDX abs
M_LDX absyp

%macro M_LDY 1
o_ldy_%1:
    OPR %1
    mov dl, dh
    jmp a2_nz_dl
%endmacro
M_LDY imm
M_LDY zp
M_LDY zpx
M_LDY abs
M_LDY absxp

%macro M_STA 1
o_sta_%1:
    call a2_ea_%1
    mov dh, al
    call a2_wr_bx
    jmp a2_next
%endmacro
M_STA zp
M_STA zpx
M_STA abs
M_STA absx
M_STA absy
M_STA indx
M_STA indy

%macro M_STX 1
o_stx_%1:
    call a2_ea_%1
    mov dh, cl
    call a2_wr_bx
    jmp a2_next
%endmacro
M_STX zp
M_STX zpy
M_STX abs

%macro M_STY 1
o_sty_%1:
    call a2_ea_%1
    mov dh, dl
    call a2_wr_bx
    jmp a2_next
%endmacro
M_STY zp
M_STY zpx
M_STY abs

; --- the logic group ---------------------------------------------------------
%macro M_ORA 1
o_ora_%1:
    OPR %1
    or  al, dh
    jmp a2_nz_al
%endmacro
M_ORA imm
M_ORA zp
M_ORA zpx
M_ORA abs
M_ORA absxp
M_ORA absyp
M_ORA indx
M_ORA indyp

%macro M_AND 1
o_and_%1:
    OPR %1
    and al, dh
    jmp a2_nz_al
%endmacro
M_AND imm
M_AND zp
M_AND zpx
M_AND abs
M_AND absxp
M_AND absyp
M_AND indx
M_AND indyp

%macro M_EOR 1
o_eor_%1:
    OPR %1
    xor al, dh
    jmp a2_nz_al
%endmacro
M_EOR imm
M_EOR zp
M_EOR zpx
M_EOR abs
M_EOR absxp
M_EOR absyp
M_EOR indx
M_EOR indyp

; --- arithmetic --------------------------------------------------------------
%macro M_ADC 1
o_adc_%1:
    OPR %1
    jmp a2_adc
%endmacro
M_ADC imm
M_ADC zp
M_ADC zpx
M_ADC abs
M_ADC absxp
M_ADC absyp
M_ADC indx
M_ADC indyp

%macro M_SBC 1
o_sbc_%1:
    OPR %1
    jmp a2_sbc
%endmacro
M_SBC imm
M_SBC zp
M_SBC zpx
M_SBC abs
M_SBC absxp
M_SBC absyp
M_SBC indx
M_SBC indyp

%macro M_CMP 1
o_cmp_%1:
    OPR %1
    cmp al, dh
    jmp a2_cmpfin
%endmacro
M_CMP imm
M_CMP zp
M_CMP zpx
M_CMP abs
M_CMP absxp
M_CMP absyp
M_CMP indx
M_CMP indyp

%macro M_CPX 1
o_cpx_%1:
    OPR %1
    cmp cl, dh
    jmp a2_cmpfin
%endmacro
M_CPX imm
M_CPX zp
M_CPX abs

%macro M_CPY 1
o_cpy_%1:
    OPR %1
    cmp dl, dh
    jmp a2_cmpfin
%endmacro
M_CPY imm
M_CPY zp
M_CPY abs

; --- BIT: Z from A & M, but N and V come STRAIGHT off the operand ------------
%macro M_BIT 1
o_bit_%1:
    OPR %1
    and ah, 0x01
    mov bl, dh
    and bl, 0x80
    or  ah, bl
    and ch, 0xBF
    test dh, 0x40
    jz  %%v0
    or  ch, 0x40
%%v0:
    mov bl, al
    and bl, dh
    jnz %%nz
    or  ah, 0x40
%%nz:
    jmp a2_next
%endmacro
M_BIT zp
M_BIT abs

; --- shifts and rotates, in memory ------------------------------------------
;
; EVERY MEMORY READ-MODIFY-WRITE WRITES TWICE, AND THE FIRST WRITE IS THE OLD
; BYTE. That is the 6502's dummy write and VICE models it in the macro itself
; - `dummy_func(tmp_addr, tmp_value)` between the load and the store
; (VICE's src/6510core.c:929 ASL, :1175 INC, :1100 DCP, and every other RMW macro
; beside them). It costs no cycle here: the table already charges the RMW
; modes their 5/6/7, exactly as VICE's CLK_ADD_DUMMY does.
;
; It is not decoration. `INC $D019` and `DEC $DC0D` are real instructions and
; the value the CHIP sees first is the old one, which acknowledges a VIC
; interrupt or reads a CIA ICR before the modified byte lands; and on RAM the
; dirty bit and the write window are raised by the first write too. a2_wr_bx
; preserves every register, DH included, so the pair is two calls and nothing
; else.
;
%macro M_ASL 1
o_asl_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shl dh, 1
    lahf
    and ah, 0xC1
    call a2_wr_bx
    jmp a2_next
%endmacro
M_ASL zp
M_ASL zpx
M_ASL abs
M_ASL absx

%macro M_LSR 1
o_lsr_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr dh, 1
    lahf
    and ah, 0xC1
    call a2_wr_bx
    jmp a2_next
%endmacro
M_LSR zp
M_LSR zpx
M_LSR abs
M_LSR absx

%macro M_ROL 1
o_rol_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcl dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call a2_wr_bx
    jmp a2_rot_dh
%endmacro
M_ROL zp
M_ROL zpx
M_ROL abs
M_ROL absx

%macro M_ROR 1
o_ror_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcr dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call a2_wr_bx
    jmp a2_rot_dh
%endmacro
M_ROR zp
M_ROR zpx
M_ROR abs
M_ROR absx

o_asl_acc:
    shl al, 1
    lahf
    and ah, 0xC1
    jmp a2_next

o_lsr_acc:
    shr al, 1
    lahf
    and ah, 0xC1
    jmp a2_next

o_rol_acc:
    shr ah, 1
    rcl al, 1
    mov ah, 0
    jnc .nc
    mov ah, 1
.nc:
    or  al, al
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

o_ror_acc:
    shr ah, 1
    rcr al, 1
    mov ah, 0
    jnc .nc
    mov ah, 1
.nc:
    or  al, al
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

; --- increments and decrements ----------------------------------------------
%macro M_INC 1
o_inc_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    and ah, 0x01
    inc dh
    call a2_wr_bx
    jmp a2_nz_dh
%endmacro
M_INC zp
M_INC zpx
M_INC abs
M_INC absx

%macro M_DEC 1
o_dec_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    and ah, 0x01
    dec dh
    call a2_wr_bx
    jmp a2_nz_dh
%endmacro
M_DEC zp
M_DEC zpx
M_DEC abs
M_DEC absx

o_inx:
    inc cl
    jmp a2_nz_cl
o_dex:
    dec cl
    jmp a2_nz_cl
o_iny:
    inc dl
    jmp a2_nz_dl
o_dey:
    dec dl
    jmp a2_nz_dl

; --- transfers ---------------------------------------------------------------
o_tax:
    mov cl, al
    jmp a2_nz_cl
o_tay:
    mov dl, al
    jmp a2_nz_dl
o_txa:
    mov al, cl
    jmp a2_nz_al
o_tya:
    mov al, dl
    jmp a2_nz_al
o_tsx:
    mov bx, di                      ; DI is 0x0100+S; CL takes the low byte
    mov cl, bl
    jmp a2_nz_cl
o_txs:
    mov bl, cl                      ; TXS sets NO flags
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    jmp a2_next

; --- the stack ---------------------------------------------------------------
o_pha:
    PUSHB al
    jmp a2_next
o_php:
    call a2_getp
    or  bl, 0x10                    ; PHP always pushes B set
    PUSHB bl
    jmp a2_next
o_pla:
    POPB al
    jmp a2_nz_al
o_plp:
    mov dh, ch                      ; the OLD flags: DH is the scratch and
                                    ; neither POPB nor a2_setp touches it
    POPB bl
    call a2_setp
    test dh, 0x04
    jz  .was_clear
    test ch, 0x04                   ; I WAS set...
    jnz a2_next                    ; ...and still is
    jmp a2_delay_one               ; ...and is now clear: ENABLES
.was_clear:
    test ch, 0x04                   ; I WAS clear...
    jz  a2_irq_chk                 ; ...and still is: the ordinary check
    test byte [A2_SCR_BASE + A2_SCR_IRQ], 0x01
    jz  a2_next
    jmp a2_do_irq                  ; ...and is now set: DISABLES, so the line
                                    ; visible under the old I is still taken

; --- flags -------------------------------------------------------------------
o_clc:
    and ah, 0xFE
    jmp a2_next
o_sec:
    or  ah, 0x01
    jmp a2_next
; --- THE I FLAG'S ONE-INSTRUCTION TIMING (VICE's src/6510core.c:1052 CLI, :1493 PLP,
; :1760 SEI, and :1652 says RTI has NONE of it) ------------------------------
;
; VICE records the transition in the LAST OPCODE's info and the interrupt
; check reads it (`(!LOCAL_INTERRUPT() || OPINFO_DISABLES_IRQ(...))`,
; VICE's src/6510core.c:445; mainc64cpu.c:702 refuses the IRQ after an ENABLES). Two
; halves, and this core had both wrong in opposite directions:
;
;   ENABLES (CLI, or a PLP that clears I): the IRQ is NOT taken at the end of
;   this instruction. One more instruction executes first - which is what lets
;   `CLI / SEI` run with an IRQ pending and take it after the SEI, and what
;   makes the KERNAL's own `CLI / RTS` sequences behave.
;
;   DISABLES (SEI, or a PLP that sets I): an IRQ that was visible under the
;   OLD I value is still taken at the end of this instruction. Taking I at
;   face value here HIDES it, and on a machine that never clears I again it is
;   hidden for good.
;
; a2_delay_one is how the first half is done without putting a test on the
; hottest path in the machine: see its own comment.
o_cli:
    and ch, 0xFB
    jmp a2_delay_one
o_sei:
    test ch, 0x04
    jnz .set                        ; I was already set: nothing transitions
    or  ch, 0x04
    test byte [A2_SCR_BASE + A2_SCR_IRQ], 0x01
    jz  a2_next
    jmp a2_do_irq                  ; DISABLES: the line was visible under the
                                    ; old I and is still taken (VICE's src/6510core.c:445)
.set:
    or  ch, 0x04
    jmp a2_next
o_cld:
    and ch, 0xF7
    jmp a2_next
o_sed:
    or  ch, 0x08
    jmp a2_next
o_clv:
    and ch, 0xBF
    jmp a2_next

; --- jumps and branches ------------------------------------------------------
o_jmp_abs:
    call a2_ea_abs
    mov si, bx
    jmp a2_next

; JMP (ind) keeps the NMOS page-wrap bug: only the LOW byte of the pointer
; increments, so JMP ($10FF) takes its high byte from $1000.
o_jmp_ind:
    call a2_ea_abs
    call a2_rd_bx
    mov [bp+CT_B], dh
    inc bl
    call a2_rd_bx
    mov bh, dh
    mov bl, [bp+CT_B]
    mov si, bx
    jmp a2_next

; A taken branch is one more cycle, and one more again when the target is on
; another page (APPLE2-SPEC section 4.2).
%macro M_BR 3                       ; label, register/flag test, jump-if-taken
o_%1:
    call a2_fetch
    %2
    %3 %%take
    jmp a2_next
%%take:
    dec word [A2_SCR_BASE + A2_SCR_DEAD]
    mov bl, dh
    xor bh, bh
    or  dh, dh
    jns %%pos
    dec bh
%%pos:
    mov [bp+CT_W], si
    add bx, si
    mov si, bx
    mov dh, [bp+CT_W+1]
    cmp dh, bh
    je  %%same
    dec word [A2_SCR_BASE + A2_SCR_DEAD]
%%same:
    jmp a2_next
%endmacro

M_BR bpl, {test ah, 0x80}, jz
M_BR bmi, {test ah, 0x80}, jnz
M_BR bvc, {test ch, 0x40}, jz
M_BR bvs, {test ch, 0x40}, jnz
M_BR bcc, {test ah, 0x01}, jz
M_BR bcs, {test ah, 0x01}, jnz
M_BR bne, {test ah, 0x40}, jz
M_BR beq, {test ah, 0x40}, jnz

; --- the NOPs, documented and not (VICE's src/6510core.c) ------------------------------
o_nop:
    jmp a2_next
o_nop_imm:
    call a2_fetch
    jmp a2_next
o_nop_zp:
    call a2_ea_zp
    jmp a2_next
o_nop_zpx:
    call a2_ea_zpx
    jmp a2_next
o_nop_abs:
    call a2_ea_abs
    jmp a2_next
o_nop_absxp:
    call a2_ea_absxp
    jmp a2_next

; =============================================================================
; THE ILLEGAL OPCODES (VICE's src/6510core.c)
;
; The stable ones are exact. The six unstable ones - ANE, LXA, SHA, SHX, SHY
; and SHS - are the "magic constant" behaviour VICE compiles ($EF for ANE,
; $EE for LXA, VICE's src/6510core.c:847 and :1389), and the four stores carry VICE's
; page-cross address corruption too (:699-711). a2cputest.asm's
; illegal-opcode row is written against VICE and not against silicon nobody
; agrees on.
; =============================================================================
%macro M_SLO 1
o_slo_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shl dh, 1
    lahf
    and ah, 0xC1
    call a2_wr_bx
    or  al, dh
    jmp a2_nz_al
%endmacro
M_SLO zp
M_SLO zpx
M_SLO abs
M_SLO absx
M_SLO absy
M_SLO indx
M_SLO indy

%macro M_RLA 1
o_rla_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcl dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call a2_wr_bx
    and al, dh
    jmp a2_nz_al
%endmacro
M_RLA zp
M_RLA zpx
M_RLA abs
M_RLA absx
M_RLA absy
M_RLA indx
M_RLA indy

%macro M_SRE 1
o_sre_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr dh, 1
    lahf
    and ah, 0xC1
    call a2_wr_bx
    xor al, dh
    jmp a2_nz_al
%endmacro
M_SRE zp
M_SRE zpx
M_SRE abs
M_SRE absx
M_SRE absy
M_SRE indx
M_SRE indy

%macro M_RRA 1
o_rra_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    shr ah, 1
    rcr dh, 1
    mov ah, 0
    jnc %%nc
    mov ah, 1
%%nc:
    call a2_wr_bx
    jmp a2_adc
%endmacro
M_RRA zp
M_RRA zpx
M_RRA abs
M_RRA absx
M_RRA absy
M_RRA indx
M_RRA indy

%macro M_DCP 1
o_dcp_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    dec dh
    call a2_wr_bx
    cmp al, dh
    jmp a2_cmpfin
%endmacro
M_DCP zp
M_DCP zpx
M_DCP abs
M_DCP absx
M_DCP absy
M_DCP indx
M_DCP indy

%macro M_ISB 1
o_isb_%1:
    call a2_ea_%1
    call a2_rd_bx
    call a2_wr_bx                  ; THE 6502'S DUMMY WRITE of the OLD byte
    inc dh
    call a2_wr_bx
    jmp a2_sbc
%endmacro
M_ISB zp
M_ISB zpx
M_ISB abs
M_ISB absx
M_ISB absy
M_ISB indx
M_ISB indy

%macro M_SAX 1
o_sax_%1:
    call a2_ea_%1
    mov dh, al
    and dh, cl
    call a2_wr_bx
    jmp a2_next
%endmacro
M_SAX zp
M_SAX zpy
M_SAX abs
M_SAX indx

%macro M_LAX 1
o_lax_%1:
    OPR %1
    mov al, dh
    mov cl, dh
    jmp a2_nz_al
%endmacro
M_LAX zp
M_LAX zpy
M_LAX abs
M_LAX absyp
M_LAX indx
M_LAX indyp

o_anc:                              ; A &= imm; C is the sign of the result
    call a2_fetch
    and al, dh
    and ah, 0x01
    or  al, al
    jz  .z
    jns .p
    or  ah, 0x81                    ; N, and C follows N
    jmp a2_next
.p:
    and ah, 0xFE
    jmp a2_next
.z:
    mov ah, 0x40
    jmp a2_next

o_alr:                              ; A &= imm; LSR A
    call a2_fetch
    and al, dh
    shr al, 1
    lahf
    and ah, 0xC1
    jmp a2_next

; ARR - "the fanciest opcode ever" (VICE's src/6510core.c:895). A &= imm, then ROR A,
; and then the flags are nothing like a ROR's: C is bit 6 of the result and V
; is bit 6 exclusive-or bit 5.
;
; AND IT HAS A DECIMAL MODE, which this core used to discard. VICE's decimal
; branch (:900-:916) computes the shifted value, takes N from the INCOMING
; carry, Z from the result and V from `(tmp_2 ^ tmp) & 0x40` - all BEFORE the
; BCD fixups - then adds 6 to the low nibble when `(tmp & 0xf) + (tmp & 0x1)`
; passes 5, adds $60 to the high nibble when `(tmp & 0xf0) + (tmp & 0x10)`
; passes $50, and takes the carry from THAT test and nothing else. The high
; test sums to $100 at tmp = $Fx, which is why it is done with the 8086's own
; carry out and not with a compare alone.
o_arr:
    call a2_fetch
    and al, dh                      ; tmp = A & value
    test ch, 0x08                   ; D?
    jnz o_arr_dec
    shr ah, 1
    rcr al, 1
    mov ah, 0
    test al, 0x40
    jz  .c0
    or  ah, 0x01
.c0:
    and ch, 0xBF
    mov dh, al
    shr dh, 1
    xor dh, al
    test dh, 0x20
    jz  .v0
    or  ch, 0x40
.v0:
    or  al, al
    jz  .z
    js  .n
    jmp a2_next
.z:
    or  ah, 0x40
    jmp a2_next
.n:
    or  ah, 0x80
    jmp a2_next

; the decimal branch, transcribed (VICE's src/6510core.c:900-916)
o_arr_dec:
    mov bl, al                      ; BL = tmp
    mov bh, bl
    shr bh, 1
    test ah, 0x01                   ; the INCOMING carry, into bit 7
    jz  .noc
    or  bh, 0x80
.noc:                               ; BH = tmp_2
    mov dh, ah                      ; the old flag byte, for the carry test
    xor ah, ah
    test dh, 0x01
    jz  .n0
    or  ah, 0x80                    ; N = the incoming carry
.n0:
    or  bh, bh
    jnz .nz
    or  ah, 0x40                    ; Z = !tmp_2
.nz:
    and ch, 0xBF
    mov dh, bh
    xor dh, bl
    test dh, 0x40
    jz  .v0
    or  ch, 0x40                    ; V = (tmp_2 ^ tmp) & 0x40
.v0:
    mov dh, bl                      ; ((tmp & 0xf) + (tmp & 0x1)) > 5 ?
    and dh, 0x0F
    mov al, bl
    and al, 0x01
    add dh, al
    cmp dh, 6
    jb  .lo
    mov dh, bh                      ; ...then +6 into the low nibble
    add dh, 6
    and dh, 0x0F
    and bh, 0xF0
    or  bh, dh
.lo:
    mov dh, bl                      ; ((tmp & 0xf0) + (tmp & 0x10)) > 0x50 ?
    and dh, 0xF0
    mov al, bl
    and al, 0x10
    add dh, al                      ; $F0 + $10 is $100: the CARRY is the
    jc  .hi                         ; ninth bit and a compare alone loses it
    cmp dh, 0x51
    jb  .done                       ; C stays 0 - the else branch's only act
.hi:
    mov dh, bh                      ; ...then +$60 into the high nibble
    add dh, 0x60
    and dh, 0xF0
    and bh, 0x0F
    or  bh, dh
    or  ah, 0x01                    ; ...and THAT is where the carry comes from
.done:
    mov al, bh
    jmp a2_next

o_sbx:                              ; X = (A & X) - imm, with a CMP's carry
    call a2_fetch
    mov bl, al
    and bl, cl
    sub bl, dh
    mov cl, bl
    lahf
    and ah, 0xC1
    xor ah, 0x01
    jmp a2_next

o_ane:                              ; the magic constant is VICE's ANE_MAGIC,
                                    ; which is $EF (VICE's src/6510core.c:847). $EE is
                                    ; ANE_RDY_MAGIC (:848), the value on a
                                    ; cycle the RDY line is pulled, and LXA's
                                    ; magic below - "needs to be 0xee for
                                    ; wizball" (:1389). Using $EE here made
                                    ; ANE's bit 0 an AND of A instead of a 1
    call a2_fetch
    or  al, 0xEF
    and al, cl
    and al, dh
    jmp a2_nz_al

o_lxa:                              ; ...and LXA's IS $EE (LXA_MAGIC, :1389)
    call a2_fetch
    or  al, 0xEE
    and al, dh
    mov cl, al
    jmp a2_nz_al

o_las:                              ; A = X = S = M & S
    call a2_ea_absyp
    call a2_rd_bx
    mov bx, di
    and dh, bl
    mov al, dh
    mov cl, dh
    mov bl, dh
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    jmp a2_nz_al

; THE FOUR UNSTABLE STORES, AND BOTH HALVES OF WHAT MAKES THEM UNSTABLE.
;
; The mask is the UNINDEXED base's high byte plus one - `(tmp >> 8) + 1` where
; `tmp` is the address BEFORE the index is added (VICE's src/6510core.c:1769 SHA_ABS_Y,
; :1797 SHX_ABS_Y, :1806 SHY_ABS_X, :1815 SHS_ABS_Y, and :1778 SHA_IND_Y where
; `tmp` is the zero-page pointer). This core used the INDEXED high byte, which
; is the same number only while the index does not carry.
;
; And when it DOES carry, the address itself is corrupted: STORE_ABS_SH_Y
; (:699-711) writes `(tmp2 & 0xff) | (value << 8)` - the high byte of the
; target becomes the VALUE. That is the half this core called "the address
; corruption a real 6502 shows", said it was not carrying, and did not need
; to skip: VICE compiles it and it is four instructions here.
;
; The three EA helpers below are the ordinary indexed ones plus the two facts
; only these opcodes want: the unindexed high byte in CT_B, and whether the
; index carried in CT_T.
a2_ea_absy_sh:
    call a2_ea_abs
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, dl
    jnc a2_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
a2_ea_sh_ret:
    ret

a2_ea_absx_sh:
    call a2_ea_abs
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, cl
    jnc a2_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
    ret

a2_ea_indy_sh:
    call a2_ea_indp                ; the POINTER is the unindexed address
    mov [bp+CT_B], bh
    mov byte [bp+CT_T], 0
    add bl, dl
    jnc a2_ea_sh_ret
    inc bh
    mov byte [bp+CT_T], 1
    ret

o_sha_indy:
    call a2_ea_indy_sh
    mov dh, al
    and dh, cl
    jmp short a2_sh_and
o_sha_absy:
    call a2_ea_absy_sh
    mov dh, al
    and dh, cl
    jmp short a2_sh_and
o_shx:
    call a2_ea_absy_sh
    mov dh, cl
    jmp short a2_sh_and
o_shy:
    call a2_ea_absx_sh
    mov dh, dl
    jmp short a2_sh_and
o_shs:                              ; SHS/TAS: S = A & X, and then SHA
    call a2_ea_absy_sh
    mov dh, al
    and dh, cl
    push bx
    mov bl, dh
    xor bh, bh
    or  bx, 0x0100
    mov di, bx
    pop bx

a2_sh_and:                         ; DH = the register value, BX = the
    push bx                         ; INDEXED address, CT_B = the unindexed
    mov bl, [bp+CT_B]               ; high byte, CT_T = the index carried
    inc bl
    and dh, bl                      ; value = reg & ((base >> 8) + 1)
    pop bx
    cmp byte [bp+CT_T], 0
    je  .store
    mov bh, dh                      ; ...and on a page cross the target's HIGH
                                    ; byte becomes the value (:707-709)
.store:
    call a2_wr_bx
    jmp a2_next

; =============================================================================
; THE C-CALLABLE ENTRIES BESIDE _a2_run
; =============================================================================

; int a2_bread(unsigned a) - one byte the way the 6502 would see it through
; the Apple II ladder. The reset vector lives in the ROM, so the C cannot read
; it with a2_rd (which is RAM by construction, a2mem.inc), and the harness
; needs the same door.
_a2_bread:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    push cx
    push dx
    mov bx, [bp+4]
    mov ds, [cs:_a2_m+AM_RAMSEG]
    call a2_rd_bx
    mov al, dh
    pop dx
    pop cx
    mov ah, 0
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; void a2_cut(void) - end the run that is in progress at once, WITHOUT
; disturbing the cycle accounting: the unspent countdown moves into CARRY and
; a2_expire puts it back, so `ran = asked - cnt` is still exact. a2io.c calls
; it when a soft switch has changed something the slice must not run past.
_a2_cut:
    push ds
    push bx
    mov bx, [cs:_a2_m+AM_RAMSEG]
    mov ds, bx
    mov bx, [A2_SCR_BASE + A2_SCR_DEAD]
    inc bx
    add [A2_SCR_BASE + A2_SCR_CARRY], bx
    mov word [A2_SCR_BASE + A2_SCR_DEAD], -1
    pop bx
    pop ds
    ret

; void a2_rebias(void) - PC moved under the core's feet (a reset, a poke).
; Emptying the fetch region forces the next fetch through a2_rebias_go, so no
; instruction is ever fetched under a stale bias (APPLE2-SPEC section 4.1).
;
; The Apple II's map never MOVES - there is no banking on a 48K II+ - so this
; is the C64's c64_rebank with nothing left to rebank: what it protects is the
; case where the C changes PC between runs.
_a2_rebias:
    push ds
    push ax
    mov ax, [cs:_a2_m+AM_RAMSEG]
    mov ds, ax
    mov word [A2_SCR_BASE + A2_SCR_BOUND], 0
    mov word [A2_SCR_BASE + A2_SCR_BLO], 0
    pop ax
    pop ds
    ret

; void a2_clk_set(unsigned v) - the emulated clock's base for the run about to
; start, which the C sets to `clock + budget` (APPLE2-SPEC section 5.1).
_a2_clk_set:
    push bp
    mov bp, sp
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov ax, [bp+4]
    mov [es:A2_SCR_BASE + A2_SCR_CLKB], ax
    pop es
    pop bp
    ret

; int a2_now(void) - the emulated clock, in 6502 cycles, as a 16-bit wrapping
; counter. It is CLKB - DEAD, which is exact INSIDE a run - which is where the
; paddle one-shots are armed at $C070 and read at $C064 - and exact outside
; one too, because a2_expire leaves what was NOT spent in DEAD and the C
; re-reads the clock from here after every slice.
;
; The one place it is not exact is after a2_cut, which drives DEAD to -1 and
; parks the unspent budget in CARRY: a cut ENDS the run, so nothing reads this
; between the cut and the expiry that puts the carry back.
_a2_now:
    push es
    push bx
    mov es, [_a2_m+AM_RAMSEG]
    mov ax, [es:A2_SCR_BASE + A2_SCR_CLKB]
    mov bx, [es:A2_SCR_BASE + A2_SCR_DEAD]
    sub ax, bx
    pop bx
    pop es
    ret

; =============================================================================
; THE TABLES
; =============================================================================

; the dirty bitmap's bit for a page - read-only and cs:-resident, which is not
; the TCG hazard of LESSONS.md 13 (only WRITES to a translated page are)
a2_dmask:
%assign a2_i 0
%rep 256
    db 0x80 >> (a2_i & 7)
%assign a2_i a2_i+1
%endrep

; --- the per-opcode cycle table (APPLE2-SPEC section 4.2, VICE's src/6510core.c) --------
; The base cost. The page-cross penalty on the indexed READ modes, the
; taken-branch penalty and the branch-page-cross penalty are charged by
; a2_ea_*p and M_BR, because they are conditions and not costs.
a2_cyc:
    db 7,6,2,8,3,3,5,5, 3,2,2,2,4,4,6,6      ; 00
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 10
    db 6,6,2,8,3,3,5,5, 4,2,2,2,4,4,6,6      ; 20
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 30
    db 6,6,2,8,3,3,5,5, 3,2,2,2,3,4,6,6      ; 40
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 50
    db 6,6,2,8,3,3,5,5, 4,2,2,2,5,4,6,6      ; 60
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; 70
    db 2,6,2,6,3,3,3,3, 2,2,2,2,4,4,4,4      ; 80
    db 2,6,2,6,4,4,4,4, 2,5,2,5,5,5,5,5      ; 90
    db 2,6,2,6,3,3,3,3, 2,2,2,2,4,4,4,4      ; A0
    db 2,5,2,5,4,4,4,4, 2,4,2,4,4,4,4,4      ; B0
    db 2,6,2,8,3,3,5,5, 2,2,2,2,4,4,6,6      ; C0
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; D0
    db 2,6,2,8,3,3,5,5, 2,2,2,2,4,4,6,6      ; E0
    db 2,5,2,8,4,4,6,6, 2,4,2,7,4,4,7,7      ; F0

; --- the 256-entry dispatch table (APPLE2-SPEC section 4.1) ---------------------
a2_tab:
    dw o_brk,      o_ora_indx,  o_jam,       o_slo_indx
    dw o_nop_zp,   o_ora_zp,    o_asl_zp,    o_slo_zp
    dw o_php,      o_ora_imm,   o_asl_acc,   o_anc
    dw o_nop_abs,  o_ora_abs,   o_asl_abs,   o_slo_abs
    dw o_bpl,      o_ora_indyp, o_jam,       o_slo_indy
    dw o_nop_zpx,  o_ora_zpx,   o_asl_zpx,   o_slo_zpx
    dw o_clc,      o_ora_absyp, o_nop,       o_slo_absy
    dw o_nop_absxp,o_ora_absxp, o_asl_absx,  o_slo_absx
    dw o_jsr,      o_and_indx,  o_jam,       o_rla_indx
    dw o_bit_zp,   o_and_zp,    o_rol_zp,    o_rla_zp
    dw o_plp,      o_and_imm,   o_rol_acc,   o_anc
    dw o_bit_abs,  o_and_abs,   o_rol_abs,   o_rla_abs
    dw o_bmi,      o_and_indyp, o_jam,       o_rla_indy
    dw o_nop_zpx,  o_and_zpx,   o_rol_zpx,   o_rla_zpx
    dw o_sec,      o_and_absyp, o_nop,       o_rla_absy
    dw o_nop_absxp,o_and_absxp, o_rol_absx,  o_rla_absx
    dw o_rti,      o_eor_indx,  o_jam,       o_sre_indx
    dw o_nop_zp,   o_eor_zp,    o_lsr_zp,    o_sre_zp
    dw o_pha,      o_eor_imm,   o_lsr_acc,   o_alr
    dw o_jmp_abs,  o_eor_abs,   o_lsr_abs,   o_sre_abs
    dw o_bvc,      o_eor_indyp, o_jam,       o_sre_indy
    dw o_nop_zpx,  o_eor_zpx,   o_lsr_zpx,   o_sre_zpx
    dw o_cli,      o_eor_absyp, o_nop,       o_sre_absy
    dw o_nop_absxp,o_eor_absxp, o_lsr_absx,  o_sre_absx
    dw o_rts,      o_adc_indx,  o_jam,       o_rra_indx
    dw o_nop_zp,   o_adc_zp,    o_ror_zp,    o_rra_zp
    dw o_pla,      o_adc_imm,   o_ror_acc,   o_arr
    dw o_jmp_ind,  o_adc_abs,   o_ror_abs,   o_rra_abs
    dw o_bvs,      o_adc_indyp, o_jam,       o_rra_indy
    dw o_nop_zpx,  o_adc_zpx,   o_ror_zpx,   o_rra_zpx
    dw o_sei,      o_adc_absyp, o_nop,       o_rra_absy
    dw o_nop_absxp,o_adc_absxp, o_ror_absx,  o_rra_absx
    dw o_nop_imm,  o_sta_indx,  o_nop_imm,   o_sax_indx
    dw o_sty_zp,   o_sta_zp,    o_stx_zp,    o_sax_zp
    dw o_dey,      o_nop_imm,   o_txa,       o_ane
    dw o_sty_abs,  o_sta_abs,   o_stx_abs,   o_sax_abs
    dw o_bcc,      o_sta_indy,  o_jam,       o_sha_indy
    dw o_sty_zpx,  o_sta_zpx,   o_stx_zpy,   o_sax_zpy
    dw o_tya,      o_sta_absy,  o_txs,       o_shs
    dw o_shy,      o_sta_absx,  o_shx,       o_sha_absy
    dw o_ldy_imm,  o_lda_indx,  o_ldx_imm,   o_lax_indx
    dw o_ldy_zp,   o_lda_zp,    o_ldx_zp,    o_lax_zp
    dw o_tay,      o_lda_imm,   o_tax,       o_lxa
    dw o_ldy_abs,  o_lda_abs,   o_ldx_abs,   o_lax_abs
    dw o_bcs,      o_lda_indyp, o_jam,       o_lax_indyp
    dw o_ldy_zpx,  o_lda_zpx,   o_ldx_zpy,   o_lax_zpy
    dw o_clv,      o_lda_absyp, o_tsx,       o_las
    dw o_ldy_absxp,o_lda_absxp, o_ldx_absyp, o_lax_absyp
    dw o_cpy_imm,  o_cmp_indx,  o_nop_imm,   o_dcp_indx
    dw o_cpy_zp,   o_cmp_zp,    o_dec_zp,    o_dcp_zp
    dw o_iny,      o_cmp_imm,   o_dex,       o_sbx
    dw o_cpy_abs,  o_cmp_abs,   o_dec_abs,   o_dcp_abs
    dw o_bne,      o_cmp_indyp, o_jam,       o_dcp_indy
    dw o_nop_zpx,  o_cmp_zpx,   o_dec_zpx,   o_dcp_zpx
    dw o_cld,      o_cmp_absyp, o_nop,       o_dcp_absy
    dw o_nop_absxp,o_cmp_absxp, o_dec_absx,  o_dcp_absx
    dw o_cpx_imm,  o_sbc_indx,  o_nop_imm,   o_isb_indx
    dw o_cpx_zp,   o_sbc_zp,    o_inc_zp,    o_isb_zp
    dw o_inx,      o_sbc_imm,   o_nop,       o_sbc_imm
    dw o_cpx_abs,  o_sbc_abs,   o_inc_abs,   o_isb_abs
    dw o_beq,      o_sbc_indyp, o_jam,       o_isb_indy
    dw o_nop_zpx,  o_sbc_zpx,   o_inc_zpx,   o_isb_zpx
    dw o_sed,      o_sbc_absyp, o_nop,       o_isb_absy
    dw o_nop_absxp,o_sbc_absxp, o_inc_absx,  o_isb_absx
