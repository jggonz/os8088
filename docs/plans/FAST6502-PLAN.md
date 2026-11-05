# FAST6502 - a fast 6502 core for APPLE2 and C64 (plan)

A PLAN (docs/README.md). The survey that led here is
docs/plans/EMU-SURVEY-286.md, recommendation 1. The contracts this changes
are docs/APPLE2-SPEC.md section 4 and docs/C64-SPEC.md section 4, and each
gets its section before its wave's code.

## 1. Where we start - MEASURED

86Box `286-victory` (the owner's Victory 286: 16 MHz, PVGA1A), the shipping
packages at `emu-survey` 2026-11-04, the package's own speed figure read out
of the guest once an emulated second (`a2_pct`, `c64_pct`). 86Box is not
cycle-accurate (docs/86BOX.md), so these are relative figures for this
plan's before/after, to be confirmed on the machine itself.

| | idle at the prompt | a BASIC loop |
|---|---|---|
| APPLE2 | 10% | 13.6% (`10 I=I+1 / 20 GOTO 10`) |
| C64 | 11.6% | 13% (`FOR I=1 TO 30000:NEXT`) |

The XT (APPLE2-SPEC 16.4.1) is 0.54% / 0.64%, with ~415 8088 clocks per
emulated cycle in the core and half of every second in the flush.

## 2. Why the current core is slow

The core (`c64cpu.inc`, copied as `a2cpu.inc`) maps registers well. It is
slow because of what every instruction pays besides its work:

- the countdown is a MEMORY word: `cmp [DEAD],0` before the fetch, and a
  table lookup plus `sub`/`sbb` on memory after it;
- every opcode AND operand byte is fetched through a `call` that does two
  memory compares (`BLO`, `BOUND`) for the bank boundary;
- addressing modes are nested `call`s (`ea_abs` calls `fetch` twice), then
  a `call` to the read ladder;
- N and Z are set by a branchy tail;
- every write to RAM ORs a page bit and widens a write window, whatever page
  it hits.

## 3. The design

RealDMG's (docs/REALDMG-SPEC.md 2, 4, 6), adapted to the 6502:

1. **A generated core.** `apps/m6502/m65gen.py` writes one 8086 handler per
   opcode from a single table of semantics, with each machine's memory hooks
   as a profile. Nothing generated is committed.
2. **Handler for opcode X at X x 256**, in a 64KB segment of its own: a LAZY
   `OP_SEG` part of the package. Dispatch is `mov dh,[si] / inc si /
   jmp dx` with DL = 0, inlined at the end of every handler.
3. **Registers**: A = AL; N, Z, C in AH in `lahf`'s layout (as today);
   X = CL; Y = CH; PC = SI; S = DI as `$0100+S` (as today); BX the effective
   address; DH data; **BP the cycle countdown**. V, D, I and B live in the
   scratch page.
4. **The guest is one 64KB segment, DS**, with the ROM copied into it, so a
   fetch is `[si]` and a RAM or ROM read is one compare and one `mov`.
5. **The countdown**: every handler ends `sub bp,cycles / jle expire`, with
   page-cross and branch penalties in the generated code. The semantics are
   today's exactly: the run stops BEFORE the instruction that finds the budget
   spent, and `ran = asked - cnt`.
6. **Hand-back to the accurate core.** Whatever the fast core does not do
   inline - a fetch from I/O space, decimal ADC/SBC, an interrupt - it hands
   to the existing core for one instruction, or until PC leaves I/O space.
   Both cores work on the same `a2_m`/`c64_m` record, so the existing core
   stays the oracle, the setting a user can pick (CPU > Accurate), and the
   fallback when the part cannot be had.

## 4. Apple II (wave 1)

- The guest image: `$0000-$BFFF` RAM, `$C000-$C0FF` a TRAP opcode (a fetch
  there - fall-through or a branch from `$BFxx` - hands back),
  `$C100-$C1FF` the core's scratch as today, `$D000-$FFFF` the ROM. The RAM
  claim grows from 49KB to 64KB only when the fast core is in use, by
  re-claiming at the first wake.
- Reads: below `$C000` and from `$D000` up are direct; `$C000-$CFFF` goes to
  today's ladder (soft switches, slot space, the Disk II ROM).
- Writes: zero page and the stack store and nothing else. Other writes drop
  above `$BFFF` (or call out at `$C0xx`), then mark damage ONLY on the
  display pages (`$0400-$0BFF`, `$2000-$5FFF`), which are all the flush ever
  reads from the page map and the window.
- JMP, JSR, RTS, RTI and BRK hand back when their target is in
  `$C000-$CFFF`.

## 5. C64 (wave 2)

The same generator. Its own profile: `$00`/`$01` (zero-page writes test
two addresses; reads see a read-back image), the bank maps (the ROM copied
into the image while it is mapped, with RAM under it in a shadow), I/O at
`$D000-$DFFF` through today's calls, a page dirty byte on every write, and
the alarm model through the same `expire`.

## 6. Gates

- **Correctness**: `make a2cputest` / `c64cputest` gain a fast-core leg -
  Dormann's functional test and `c64dec.py`'s 262,144 decimal cases against
  the generated core. Each row keeps a negative control that must fail.
- **Speed**: the 286 figures of section 1, re-taken (86Box), and the XT
  (MartyPC) for the cycle count, in the final pass.

## 7. Waves

| wave | outcome | done |
|---|---|---|
| 0 | baseline measured (section 1) | yes |
| 1 | APPLE2 on the generated core: boots to `]`, Dormann and the decimal oracle pass, a2cputest's fast-core leg | **landed** - 48% idle, 61% under BASIC on the 286 (APPLE2-SPEC 4.6); Dormann and all rows pass on both cores |
| 1b | the core's own cost: a profile shows the idle loop at ~120 host clocks an emulated instruction, taken jumps and code bytes | **landed** - one-test jump targets, word JSR/RTS, RAM reads fall through, the KEYIN counter: 547% idle, 67% under BASIC |
| 1c | Applesoft and the Monitor translated AHEAD OF TIME (the ARCADE branch's `arcaot.py` shape: the ROM is fixed), the interpreter keeping RAM programs | **landed** - 4,596 ROM PCs in 56KB, 109% under BASIC on the 286 under warp (APPLE2-SPEC 4.7); Dormann runs through the translation |
| 2 | C64 on the generated core | **landed** - 40% idle, 47% under BASIC on the 286 (C64-SPEC 4.7); Dormann and every applicable row pass on the fast leg |
| 2b | BASIC and the KERNAL translated ahead of time, a segment each, the run's end kept exact | **landed** - 99.8% idle (throttled), 81% under BASIC on the 286 (C64-SPEC 4.8); Dormann runs through the translation |
| 2c | the instruction merge and the rest of the way to 100%: groups, inline `PLP`, dead V, the raster wait, the display latch | **landed** - C64 BASIC 97-99.5%, a raster-split demo 96.6% (C64-SPEC 4.7.1, 4.8, 9.9); APPLE2 141.5% under warp |
| 3 | the speed pass: 286 and XT measured, the diary, the SPEC sections' measured lines | |
