; =============================================================================
; os8088 - apps/apple2/a2cpu.inc     the 6502 core: the register file, the
;                                    scratch, the memory hooks and the shell
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
; the write ladder, `a2_rebias`'s four boundaries, the ROM bias constants and
; the two I/O call-out names.
;
; **apps/apple2/ is GPL-2-or-later because of this file.** See
; apps/apple2/COPYING. Nothing of VICE's own source is vendored: what is
; carried is the register plan and the shell, by way of a package in this
; tree.
;
; THE STATED TRADE (APPLE2-SPEC section 4.1): a bug found in the shared 93%
; must be fixed twice, and the second fix will be forgotten. The mitigation is
; that both cores run the SAME fetched oracles - Klaus Dormann's functional
; test at the same pinned SHA-256, and tools/c64dec.py's 262,144 decimal
; cases - which is `make a2cputest`, and which arrives with the core it gates.
;
; ----------------------------------------------------------------------------
; WHAT IS IN THIS FILE IN WAVE 1, AND WHAT IS NOT
; ----------------------------------------------------------------------------
; Wave 1 is the shell: the register file the C reads as `struct a2_mach`, the
; scratch layout every other file addresses through, the entry and exit
; sequence, the countdown accounting, `a2_cut` and `a2_rebias`, and the
; 256-entry dispatch table. **Every entry of that table points at the JAM
; handler**, so a wave-1 machine executes nothing and says so through
; A2_RUN_JAM rather than running off into memory - which is the honest state
; of a port whose core arrives in wave 2, and is what `a2_memtest` asserts.
;
; The 151 legal opcodes, the illegal families, decimal ADC and SBC, the
; interrupt machinery, the cycle table and the addressing modes are wave 2's
; (docs/APPLE2-PORT-PLAN.md), and they are the part that is the derived copy.
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
; read ladder answers the slot ladder or the ROM.
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
                                    ;   alarm scheduler at all
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
A2_SCR_END   equ 0x36               ; ...and $CF36-$CFFF is spare. The whole
                                    ;   page is reserved because $CF00-$CFFF
                                    ;   is the expansion-ROM window this port
                                    ;   refuses to model, so reserving less
                                    ;   would buy nothing

; --- the Apple II mapping boundaries (APPLE2-SPEC section 3.2) ---------------
A2_IO_BASE   equ 0xC000             ; $C000-$C0FF  the soft switches
A2_SLOT_BASE equ 0xC100             ; $C100-$CFFF  slot space
A2_ROM_BASE  equ 0xD000             ; $D000-$FFFF  the ROM
; The fetch bias for a PC in the ROM: ES = romseg - ($D000 >> 4), so that
; ES:PC reads ROM offset PC - $D000. It underflows - and reads somewhere else
; entirely - if the ROM part's base segment is below $0D00, which the C's
; os88_main guards out loud rather than assuming (APPLE2-SPEC section 1.5).
A2_ROM_BIAS  equ A2_ROM_BASE >> 4

; =============================================================================
; int a2_run(unsigned cycles)
;
; Run the 6502 for `cycles` emulated cycles, or until it JAMs. Answers
; A2_RUN_*, and leaves in _a2_m+AM_CNT what was NOT spent, so the caller's
; `ran = asked - cnt` is exact (APPLE2-SPEC section 4.2).
;
; WAVE 1 IS THE SHELL. The dispatch table below sends every one of the 256
; opcodes to .jam, so a wave-1 machine fetches once, answers A2_RUN_JAM and
; stops. Wave 2 fills the table in; nothing above this line changes when it
; does, which is the point of writing the shell first.
;
; THERE IS NO ALARM SCHEDULER (section 4.2), and that is a real simplification
; this port states rather than an omission: a bare II+ has no timers and no
; interrupt source, so the wake loop is `r = a2_run(budget)` with no `min()`
; and no advance.
; =============================================================================
_a2_run:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es

    ; EVERY PACKAGE-DS READ HAPPENS BEFORE DS MOVES. Inside a run, DS is the
    ; emulated machine's RAM - that is the whole point of the register plan -
    ; so `[_a2_m+...]` is unreadable from `.step` onwards and the reason code
    ; travels back in DX rather than being stored where it cannot be.
    mov ax, [bp+4]                  ; the budget ([bp+n] is SS-relative and
                                    ; would stay readable, but the register
                                    ; file is package-DS and would not)
    mov si, [_a2_m+AM_PC]
    mov bx, [_a2_m+AM_RAMSEG]
    mov ds, bx                      ; DS = the emulated machine's RAM
    mov [A2_SCR_BASE + A2_SCR_DEAD], ax
    mov word [A2_SCR_BASE + A2_SCR_CARRY], 0
    mov word [A2_SCR_BASE + A2_SCR_BOUND], 0    ; the first fetch re-biases

    ; ...and the rest of the register file into registers. The plan is
    ; C64-SPEC §4.1's, unchanged: A = AL, NZC in AH's `lahf` layout, VDIB in
    ; CH, X = CL, Y = DL, PC = SI, S = DI as the full $0100+S, EA = BX. Wave 2
    ; loads them; wave 1's shell fetches nothing, so it loads none of them.

.step:
    cmp word [A2_SCR_BASE + A2_SCR_DEAD], 0
    jle .expire
    ; WAVE 1: no opcode is implemented, so the fetch is not taken and control
    ; goes straight to the dispatch table's one live entry. Wave 2 replaces
    ; this line with the biased fetch and `jmp [cs:a2_disp+bx]`.
    jmp short .jam

.jam:
    mov dx, A2_RUN_JAM
    jmp short .out

.expire:
    ; the cycles a2_cut() took out of the countdown go back, so that
    ; `asked - cnt` is what really ran and not what the cut pretended
    mov ax, [A2_SCR_BASE + A2_SCR_CARRY]
    add [A2_SCR_BASE + A2_SCR_DEAD], ax
    mov dx, A2_RUN_SLICE

.out:
    mov ax, [A2_SCR_BASE + A2_SCR_DEAD]     ; the countdown, before DS goes
    pop es
    pop ds
    mov [_a2_m+AM_PC], si
    mov [_a2_m+AM_CNT], ax
    mov [_a2_m+AM_REASON], dx
    mov ax, dx
    pop di
    pop si
    pop bp
    ret

; =============================================================================
; void a2_cut(void) - end the run in progress, exactly.
;
; The countdown is driven to zero and what was taken out of it is remembered
; in CARRY, so the expiry above can put it back and the caller's accounting
; stays exact. Called from the C when a soft switch has changed something the
; slice must not run past.
; =============================================================================
_a2_cut:
    push bp
    mov bp, sp
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov ax, [es:A2_SCR_BASE + A2_SCR_DEAD]
    cmp ax, 0
    jle .out                        ; already expired: nothing to carry
    add [es:A2_SCR_BASE + A2_SCR_CARRY], ax
    mov word [es:A2_SCR_BASE + A2_SCR_DEAD], 0
.out:
    pop es
    pop bp
    ret

; =============================================================================
; void a2_rebias(void) - the map moved: no instruction is fetched under a
; stale bias.
;
; The four boundaries are $C000, $C100, $D000 and $FFFF (APPLE2-SPEC section
; 4.1), and the guard is a PAIR - BLO and BOUND - and not a ceiling alone,
; because a backward JMP from the Monitor to $0800 leaves PC BELOW the biased
; region rather than above it.
; =============================================================================
_a2_rebias:
    push bp
    mov bp, sp
    push si
    push es
    mov es, [_a2_m+AM_RAMSEG]
    mov si, [_a2_m+AM_PC]
    mov ax, [_a2_m+AM_RAMSEG]
    cmp si, A2_IO_BASE
    jae .high
    ; $0000-$BFFF: RAM, no bias at all
    mov word [es:A2_SCR_BASE + A2_SCR_BLO], 0
    mov word [es:A2_SCR_BASE + A2_SCR_BOUND], A2_IO_BASE
    jmp short .set
.high:
    cmp si, A2_ROM_BASE
    jae .rom
    ; $C000-$CFFF: the soft switches and the slot ladder. Nothing is FETCHED
    ; from there in this port - a slot answers $FF and $FF is ISC $FFFF, which
    ; the core runs and gets nowhere with - so the region is biased to RAM and
    ; bounded so the next boundary crossing re-asks.
    mov word [es:A2_SCR_BASE + A2_SCR_BLO], A2_IO_BASE
    mov word [es:A2_SCR_BASE + A2_SCR_BOUND], A2_ROM_BASE
    jmp short .set
.rom:
    ; $D000-$FFFF: the ROM part, reached by segment arithmetic off its base
    mov word [es:A2_SCR_BASE + A2_SCR_BLO], A2_ROM_BASE
    mov word [es:A2_SCR_BASE + A2_SCR_BOUND], 0xFFFF
    mov ax, [_a2_m+AM_ROMSEG]
    sub ax, A2_ROM_BIAS
.set:
    mov [es:A2_SCR_BASE + A2_SCR_FES], ax
    pop es
    pop si
    pop bp
    ret

; =============================================================================
; THE DISPATCH TABLE (APPLE2-SPEC section 4.1)
;
; 256 near offsets, indexed by the opcode byte doubled. It is `cs:`-resident
; read-only data sitting inside .text, which is where the core's own tables
; live: DS is the emulated machine's RAM inside a run, so a DS-relative table
; would be unreachable without a segment reload per fetch.
;
; WAVE 1 POINTS EVERY ENTRY AT .jam. That is not a placeholder that pretends
; to work - it is the machine this wave describes: no opcode is implemented,
; every one of them stops the machine and says so. Wave 2 replaces the table
; wholesale with the derived copy's.
; =============================================================================
a2_disp:
%rep 256
    dw _a2_run.jam
%endrep
