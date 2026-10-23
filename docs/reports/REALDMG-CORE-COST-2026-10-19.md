# RealDMG's core: correctness, cost per M-cycle, and real games on the 286

**A measurement, taken in emulators.** 2026-10-19, on a four-core cloud
container, branch `gameboy`, the tree this file was committed in.
- The 8088 figures are MartyPC's (cycle-accurate, 4.77 MHz,
  `os8088_xt_vga`).
- The 286 figures are 86Box's (`vm/286`: the `ami286` at 12.5 MHz, DRAM at
  1 wait state, the same machine as `vm/286-525` with 3.5" drives). 86Box
  times instructions by its model and is not cycle-accurate
  (docs/86BOX.md, *What it is NOT*).

Neither is the owner's iron, and docs/plans/REALDMG-PLAN.md section 9 says
which way the Victory 286 is expected to differ (slower RAM). A later
measurement goes in a new file.

This is docs/plans/REALDMG-PLAN.md wave 1's gate. The core is the threaded
interpreter of the plan's section 4.1, run bare by `tests/rdgcore/rdghost.asm`
with no os8088 under it, so nothing in these numbers is the OS's.

## 1. Correctness

| gate | result |
|---|---|
| Blargg `cpu_instrs`, all eleven ROMs and the combined MBC1 one | **Passed**, each (`tests/gbcpu.py`) |
| Blargg `instr_timing` | **Passed** |
| negative control: DAA made a no-op | `01-special` **fails**, as it must |
| `tools/gbtrace.py` against Peanut-GB, combined `cpu_instrs` | see 1.1 |

### 1.1 The differential trace, and five places the reference was wrong

`tools/gbtrace.py` compares, before every instruction, PC, A, F, BC, DE, HL,
SP **and the M-cycle clock** of RealDMG (an `RDG_TRACE` core in QEMU, 14-byte
records on COM1) and of Peanut-GB at `tools/gbprof.py`'s pin. Comparing the
clock is what made the trace find things: a wrong cycle count is the first
record whose time differs, not a register a million instructions later.

Every difference it found up to the end of this run was **Peanut-GB's**,
measured against what the hardware does, and the reference is corrected for
each in `tools/gbprof/gbtrace.c`, with the reason beside it:

1. **LY falls behind time.** Each visible line zeroes Peanut-GB's line
   counter and drops the cycles that overran it, so its frames run long -
   **79,794 T-states a frame on Super Mario Land against 70,224**. The
   reference answers an LY read with the exact line instead.
2. **An interrupt's dispatch costs nothing** in Peanut-GB; the hardware takes
   5 M-cycles. The reference charges them.
3. **TIMA is phased to nothing in particular** in Peanut-GB. On the hardware
   DIV and TIMA run off one free-running counter that a DIV write zeroes -
   which is what `instr_timing` forced RealDMG to model. The reference sets
   Peanut-GB's TIMA counter to that phase before every instruction.
4. **HALT advances in whole LCD modes**, so it wakes late. The reference
   steps it one M-cycle at a time; it then wakes on the M-cycle RealDMG does.
5. **STOP is two bytes** (`10 00`); Peanut-GB takes one and runs the `00` as
   a NOP.

And one that was **RealDMG's**: FF4C-FF7F, where a DMG has nothing and reads
FF, returned whatever the game last wrote there - `cpu_instrs` writes FF4D
(the CGB's speed switch) and reads it back to decide which machine it is on.
Both sides read FF now.

With the five corrections, **the first 10,000,000 instructions of the
combined `cpu_instrs` agree** - the plan's wave 1 gate - on the core with the
in-core register paths of section 4, so those are checked against the
reference too.

### 1.2 The idle skip, on the games it was built for

The plan's section 4.1 makes a spin-wait free: a taken backward `JR` of -4 to
-7 to a loop whose body is one read of RAM and one test is skipped to the
next event. `gbprof` (Peanut-GB, the same input script) is the reference.

| | RealDMG skips | gbprof: halt + spin-poll | |
|---|---|---|---|
| Kirby, no input (title and attract demo) | **34.96%** | 35.5% | like for like |
| Kirby, played (gbprof's platformer script) | 58.4% | 70.0% | different scenes, below |
| Super Mario Land, played | **73.3%** | 71.5% | different scenes, below |

Kirby's attract demo waits on `BIT 6,(HL) / JR NZ,-4`, a shape the first
detector did not know (it wanted a load into A first); that row was **0%**
until the `(HL)` shapes were added. Kirby in play waits on
`LD A,(FF8C) / BIT 6,A / JR NZ,-7`.

**The played rows are different scenes**, so their shares are not a
comparison. `tests/rdgcore/rdgshot.py` draws RealDMG's last frame from its VRAM
beside gbprof's screenshot: both are in play, but RealDMG's Kirby has scored
800 with enemies on screen where Peanut-GB's is at the start with 0, and
Super Mario Land's two are in 1-1 at different points. The input script is
frame-exact on both; the frames are not the same length (item 1 above).

## 2. The gate: cost per busy M-cycle, fitted on gbclock

`tests/rdgcore/rdgbench.py` runs gbclock (`tools/gbclock.py`) at 0.7%, 25.7%
and 50.7% busy for N frames as fast as the machine can and reads the BIOS
ticks. The slope is the cost of a busy M-cycle, the intercept the fixed cost
of a frame.

| | ticks: CLOCK0 / 25 / 50 | **per busy M-cycle** | fixed, per frame |
|---|---|---|---|
| **286, 12.5 MHz** (600 frames) | 9 / 110 / 210 | **26.20 clocks** (±0.18) | 0.57 ms = 7,079 clocks |
| **8088, 4.77 MHz** (300 frames) | 36 / 276 / 516 | **47.78 clocks** (±0.14) | 5.36 ms = 25,585 clocks |
| no$gmb's XT/AT build, same ROMs (docs/reports/GAMEBOY-NOGMB-2026-10-19.md) | - | ~58 / ~100 | ~14.5 / ~60 ms |
| the plan's prediction (section 2.2) | - | 20-26 / 35-45 | - |

The error is one BIOS tick at each end. An earlier run of the same benches,
before the idle detector reached `JR -4`, read 26.07 and 47.68.

**On the 286 that is the top of the predicted range and 2.2 times leaner
than no$gmb; on the 8088 it is 6% over the top of the range.** The plan's
gate is 26: this is on it, within its error.

## 3. Real games on the 286, which is the number that matters

gbclock's busy loop does no I/O, switches no bank and raises one event a
frame. `rdgbench.py --rom` times a real game instead: gbprof's input script,
the clock restarted at frame 800 so that what is timed is play, **no picture
drawn**.

| 86Box 286, 1,200 frames in play | ms a frame | **of real time** |
|---|---|---|
| Super Mario Land | 13.69 | **81.7%** |
| ...with the in-core I/O paths of section 4 | 13.04 | **77.9%** |
| Kirby (the busy scene of 1.2) | 17.90 | **106.9%** |
| ...with the in-core I/O paths | 17.44 | **104.2%** |

Against the busy M-cycles each run actually executed (26.7% and 41.6% of the
frame), that is **~35 clocks per busy M-cycle all-in on Super Mario Land and
~31 on Kirby**, against gbclock's 26.2. **The 286 runs Super Mario Land's
logic with about a fifth of the machine left for the picture, and does not
quite run this scene of Kirby's at all.**

## 4. Where the time goes

`tests/rdgcore/rdgprof.py` samples CS:IP while the game plays and charges each
sample to the core handler or rdgsys routine holding it: MartyPC's debug
server on the 8088 (exact), 86Box's held machine stepped one emulated
millisecond at a time on the 286.

| Super Mario Land, in play | core | rdgsys (events, I/O, banks) |
|---|---|---|
| 8088, first core | 74.4% | **25.3%** |
| 286, with the in-core I/O paths | **90.4%** | 9.6% |

On the 8088 a quarter of the time was the trip out of the core: switching to
the host stack and saving the registers (`rdg_ior`, `rdg_iow`, `rdg_ret`: about
8%), the event bookkeeping (about 9%) and the I/O and bank logic itself
(about 7%). So the commonest cases are now answered **inside the core**:
the joypad, STAT and LY reads (from `S_TNF`, a frame-relative clock rdgsys
keeps beside `S_TNEXT`), sound and joypad writes, and ROM bank switches on an
MBC1, MBC3 or MBC5. It bought 3.8 points on Super Mario Land and 2.7 on Kirby
on the 286, where the trip out was always the smaller share.

**On the 286 the interpreter itself is the cost**, and no single handler
dominates: `JR NZ` 13.5%, `LDH A,(n)` 6.5%, `ADD HL,DE` 5.1%, then a long
flat tail. The handlers are already short (`CP n` is four instructions); the
fixed part is the dispatch, `mov dh,[si] / inc si / jmp dx`, about 16 clocks
of every instruction's ~30-80.

## 5. What this decides

- **The gate passes on its own terms** (26.20 against 26, within one tick),
  so waves 2 and 3 proceed: the machine, then the picture and the package.
- **The real-game numbers say the plan's levers will be needed for full game
  speed on a 286, and which one.** Superinstructions, the cheapest, are worth
  little here: `gbprof` now ranks adjacent pairs in busy code, and the top
  pair is 2.2% of Super Mario Land's and 3.0% of Kirby's, so fusing the
  first six saves about 2% (estimated from the dispatch it removes). The
  lever with a 2x in it is the plan's section 5 item 4, a recompiler, because
  the dispatch and the per-instruction cycle count are what it deletes.
- **The 5150 demo's budget is now measured**: 47.78 clocks per busy M-cycle
  and 5.36 ms a frame fixed. A demo at the plan's ~250 busy instructions a
  frame (~600 M-cycles) is ~29,000 + ~25,600 clocks of the 5150's 79,900, so
  the picture has about a third of the frame.
- **The Victory 286 is the check.** Its RAM costs about 2.5 times 86Box's
  (docs/reports/VIDBENCH-PVGA286-2026-10-08.md), and an interpreter is
  memory-heavy, so the field figure is expected to land below this one.

## 6. Reproduce

```sh
python3 tools/getgbtest.py                         # the test ROMs, pinned
python3 tests/gbcpu.py                             # section 1
python3 tools/gbtrace.py "$(python3 tools/getgbtest.py --path cpu_instrs/cpu_instrs)" --count 10000000
python3 tests/rdgcore/rdgbench.py --box              # section 2, the 286
python3 tests/rdgcore/rdgbench.py --marty            # section 2, the 8088
python3 tests/rdgcore/rdgbench.py --box --rom GAME.GB   # section 3
python3 tests/rdgcore/rdgprof.py GAME.GB --box          # section 4 (or no --box: MartyPC)
python3 tests/rdgcore/rdgshot.py IMG -o OUT.pgm         # a play/idle image's last frame
```

The games are the owner's and are not in this repository.

**The 10,000,000-instruction run on this tree:** the first 10,000,000
instructions of the combined `cpu_instrs` agree with the corrected
reference, registers and clock, in 1,083 s of QEMU.
