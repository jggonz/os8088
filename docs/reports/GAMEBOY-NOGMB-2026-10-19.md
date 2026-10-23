# no$gmb's XT/AT build on an 8088 and a 286: the bar RealDMG has to clear

**A measurement, taken in emulators.** 2026-10-19, on a four-core cloud
container, on branch `gameboy`.
- The 8088 figures are MartyPC's (cycle-accurate, 4.77 MHz).
- The 286 figures are 86Box's at the pin in `tools/86box/UPSTREAM`. 86Box
  models the 286 per instruction, but it is not cycle-accurate
  (docs/86BOX.md, *What it is NOT*).

Neither machine is the owner's iron. A later measurement goes in a new file.

## 1. What and why

**no$gmb** (Martin Korth, 1997-2000) is a Game Boy emulator and debugger
written in assembly, and its v2.5 shipped a "Special DOS XT/AT compatible
version". It is the only existing Game Boy emulator found that runs on an
8086 or 286 at all (docs/plans/REALDMG-PLAN.md section 1). The owner supplied
the build (`NO$GMB.EXE`, 122,148 bytes, 2000-07-23) and an IBM PC DOS 3.30
boot floppy. **Neither is in this repository.**

What it gets out of these machines is the bar for RealDMG, the native
os8088 emulator the plan designs.

## 2. Method

**The harness.** `tools/gbcal.py` boots DOS 3.30 from A: on a fresh machine
for each ROM. B: is built with mtools and holds `NO$GMB.EXE`, its
`NO$GMB.CFG` and the ROMs. The harness types `B:` and `NO$GMB <ROM>`,
dismisses the start-up card, runs 70 emulated seconds and screenshots every
10 s. It presses SPACE (no$gmb's START) twice in the third interval.

**The instrument.** `tools/gbclock.py` builds gbclock, a ROM of our own
(`tools/gbprof/gbclock.asm`). It counts emulated VBlanks on screen, then
keeps the Game Boy CPU busy for a set share of each frame. Speed is

    (counter at t070 - counter at t020) / (elapsed guest seconds x 59.7275)

**no$gmb's settings** are its fastest, set once in its own setup screen (F11)
and saved to `NO$GMB.CFG`:
- Game System: Standard Gameboy (fast)
- Emulation Speedup: Both Tricks (careful!)
- Sound: None (fast)
- Display Redraw Mode: Whole Screen (dirty/fast)
- everything else at its default, including LCD refresh "Auto".

no$gmb's own defaults are much slower: Super Game Boy colour, Sound Blaster
stereo, per-line redraw, no tricks. On the 286 they run gbclock at 0% busy at
**51.9%** by the counter (310 frames in 10.000 s), where the fast settings
run it at 100%.

**The machines:**
- **286:** `vm/286-525`. 86Box's `ami286` ("[NEAT] DataExpert 286") at
  **12.5 MHz**, 1 MB, DRAM at 1 wait state, OTI-067 VGA. This is the owner's
  "~12 MHz 286" target.
- **8088:** MartyPC `os8088_xt_vga_720b`. An IBM 5160 at 4.77 MHz with a VGA
  and a 720 KB B:. The 5150 class, with the adapter no$gmb wants.

**Counter against meter.** no$gmb draws its own speed meter in the corner.
On the 286 the counter and the meter agree: 51.9% against "52%". On the 8088
the meter reads low: "17%" where the counter says 26.6%. The counter is
emulated VBlanks over guest cycles, so it is the figure quoted. The meter
appears only as a check, and for real games, which have no counter.

## 3. Results

### 3.1 gbclock, by the counter

| gbclock busy (measured, `tools/gbprof.py`) | 286, 12.5 MHz | 8088, 4.77 MHz |
|---|---|---|
| 0.7% (`CLOCK0`) | **100.1%**, held at real time: 3,589 frames in 60.0 s | **26.6%**: 810 frames in 51.07 s |
| 10.7% (`CLOCK10`) | **72.1%**: 2,160 in 50.13 s | **16.7%**: 510 in 51.07 s |
| 25.7% (`CLOCK25`) | **47.1%**: 1,410 in 50.13 s; **47.5%** on a second run | **10.8%**: 330 in 51.07 s |
| 50.7% (`CLOCK50`) | **30.1%**: 900 in 50.13 s | - |

### 3.2 Real games, by no$gmb's meter

| | 286 | 8088 |
|---|---|---|
| **Super Mario Land**, 1-1 in play | **32-50%**, median ~36% across five shots | title screen only: 16-29%. The XT runs never started play |
| **Kirby's Dream Land**, title screen | 23-35% | 1-5% (default settings) |
| **libbet**, story text screen | 94-108% | - |

## 4. What the numbers say

**no$gmb's costs.** Fit as a fixed cost per Game Boy frame plus a cost per
busy M-cycle (gbclock's loop is 26 M-cycles; 1% busy is 175.6 M-cycles):

| | fixed, per Game Boy frame | per busy M-cycle | per average instruction (2.3 M-cycles) |
|---|---|---|---|
| **286, 12.5 MHz** | **~14.5 ms** (~87% of a 16.74 ms frame) | **~4.6 us = ~58 clocks** | ~133 clocks |
| **8088, 4.77 MHz** | **~60 ms** | **~21 us = ~100 clocks** | ~231 clocks |

The two CPUs differ by **4.1x on the fixed part and 4.6x on the CPU part**.

**What it means for RealDMG:**

1. **At no$gmb's per-M-cycle cost, the 286 cannot carry Super Mario Land at
   all, even drawing nothing.** Super Mario Land averages ~4,100 busy M-cycles
   a frame (docs/reports/GAMEBOY-WORKLOAD-2026-10-19.md). At ~58 clocks each
   that is ~238,000 clocks, against the 209,284 a 12.5 MHz frame has. The
   meter agrees: about 36% in play.
2. **RealDMG's core has to be about twice as lean as no$gmb's**, at no more
   than ~26 clocks per M-cycle, to leave 40% of a frame for the picture. That
   is the plan's predicted 45-60 clocks per instruction, expressed per
   M-cycle. So the prediction does not move, but it is now measured against
   something: **the best period implementation is 2.2 times off it**, and
   that is wave 1's gate.
3. **no$gmb's fixed cost is the other half of the gap.** It spends ~14.5 ms
   a frame on the 286 whatever the game does: whole-screen redraw at 59.73 Hz
   ("Auto" LCD refresh did not skip enough to show). RealDMG's design draws
   only the frames the wall clock has time for, compares against a shadow and
   writes Game Boy bit-planes straight into VGA planes. That term is where it
   expects to win most clearly.
4. **On the 8088 even an idle game runs at a quarter speed** because of
   no$gmb's ~60 ms of fixed cost. A demo that is to run at full speed on the
   5150 needs a fixed cost under ~16 ms a frame, and a frame it barely draws
   (plan section 6).

## 5. Two more workloads (`tools/gbprof.py`, host)

Measured the same way as the workload report. The ROMs are the owner's,
never committed or shipped.

| | Kirby's Dream Land | Looney Tunes (USA) |
|---|---|---|
| ROM | 256 KB, MBC1, DMG | 1 MB, MBC5, CGB-enhanced (`$80`: runs on a DMG) |
| **BUSY, T-states/frame** | **20,616 = 29.4%** (p90 34.9%, p99 49.3%) | **21,746 = 31.0%** (p50 10.5%, p90 82.0%, p99 99.3%) |
| BUSY instructions per frame | 2,245 (2.30 M-cycles each) | 2,532 (2.15 each) |
| halt / **spin-poll** / countdown | **0.0% / 70.1%** / 1.1% | 71.4% / 0.1% / 1.0% |
| MBC writes per frame | 8.35 | 10.11 |
| raster lines per frame | 0 | 0.94 |
| `CB`-prefixed | 22.6% | 2.9% |

Super Mario Land (re-run with the busy-instruction count) is 1,747 busy
instructions a frame at 2.34 M-cycles; libbet is 1,089 at 2.21.

**Kirby never executes `HALT`.** It waits for each frame by spinning on a
flag: `ld a,(nn)` is 32% and `jr nz` 26% of its non-HALT cycles. An emulator
that does not recognise that loop as idle pays for **all** of it, roughly
3.4x Kirby's real load. That is consistent with no$gmb's 1-5% on its title
screen on the 8088. **Idle-loop recognition is a requirement for the target
ROM**, not the optional lever docs/plans/REALDMG-PLAN.md section 5 first
called it.

Looney Tunes is light on average but has long bursts near a full frame
(p90 82%).

## 6. Reproducing it

```
python3 tools/gbclock.py                                  # build/gbclock/*.GB
python3 tools/os88box.py build                            # once per container
python3 tools/gbcal.py --dos DOS330.IMG --emu 'NO$GMB.EXE' --cfg 'NO$GMB.CFG' \
    --box 286-525 build/gbclock/CLOCK25.GB
python3 tools/gbcal.py ... --marty os8088_xt_vga_720b build/gbclock/CLOCK25.GB
```

`NO$GMB.CFG` is written by no$gmb itself: F11, the settings in section 2,
`S` to save, Esc twice.
