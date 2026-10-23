# What a Game Boy game asks of its CPU, per frame

**A measurement, taken on the host.** 2026-10-19, on a four-core cloud
container, at `elendilon` e8009a6d plus the `gameboy` branch's own commits.
The instrument is `tools/gbprof.py`: Peanut-GB (MIT) at the pinned commit
`d0bcca77` with five counting hooks, built with the host compiler. No 8088,
286 or emulator of either was involved. This file says what the GAME does.
What an os8088 emulator would cost to carry that load is
docs/plans/REALDMG-PLAN.md's question. A later measurement goes in a new file.

It answers the question the feasibility estimate
(docs/reports/GAMEBOY-FEASIBILITY-2026-10-19.md, its section 8 item 1) left
open: **how much of each frame does a real game's CPU spend on work an
emulator cannot skip?**

## 1. Method

A Game Boy game finishes its frame's work early, then waits for vertical
blank. It either `HALT`s or spins on a register. Either way, an emulator that
skips to the next event pays nothing for that time. `gbprof` splits every
frame's T-states four ways:

| bucket | what it is | an emulator pays |
|---|---|---|
| **halt** | the CPU is in `HALT` | nothing: skip to the next event |
| **spin-poll** | a backward loop of 12 bytes or fewer that re-reads memory or I/O and writes nothing | nothing, with idle-loop detection |
| **countdown** | the same shape with no data read (`dec b / jr nz`) | each iteration, or nothing if the loop is recognised |
| **BUSY** | everything else | **all of it** |

BUSY is reported **in T-states per frame**, as a share of the true
70,224-T-state frame. Peanut-GB's `gb_run_frame` overshoots the frame end by
up to a `HALT`'s fast-forward, so its frames average 79-81K T-states. A share
of Peanut's own total would therefore understate the load. The absolute
figure does not.

The input is scripted. START is pressed at frames 150, 400 and 700. After
that, a platformer script holds RIGHT and jumps every 50 frames, and a puzzle
script wanders the d-pad and presses A. **Statistics start at frame 800**, and
the run is 7,200 frames, two minutes of Game Boy time. A screenshot at each
third of the run checks that the game is in play:

- **Super Mario Land** (frames 2,400 and 7,199): Mario running in World 1-1,
  then the scrolling platforms of 1-2.
- **libbet** (frame 4,800): mid-puzzle.

| ROM | what it is | source |
|---|---|---|
| **Super Mario Land** | 64 KB, MBC1, a 1989 launch title; scrolling platformer with a status bar | supplied by the owner, **never committed or shipped** |
| **libbet** (Libbet and the Magic Floor) | 32 KB homebrew puzzle game, Damian Yerrick | built from source at pinobatch/libbet `1232e71d` with rgbds `d766efe9`; zlib licence |
| **cpu_instrs** | Blargg's CPU test, never halts | retrio/gb-test-roms `c240dd7d` |

`cpu_instrs` is the control. A program that never idles must read close to
100% BUSY, and it reads **99.2%**, so the classifier does not invent idle
time.

## 2. Results

| | Super Mario Land | libbet | cpu_instrs (control) |
|---|---|---|---|
| **BUSY, T-states/frame** | **16,383** | **9,637** | 69,680 |
| **BUSY, share of a 70,224-T frame** | **23.3%** | **13.7%** | 99.2% |
| BUSY per frame, p50 / p90 / p99 | 27.9% / 39.6% / 50.5% | 13.7% / 14.2% / 16.6% | 100.6% / 100.9% / 100.9% |
| BUSY per frame, max | 100% (level loads, screen off) | 22.5% | 101% |
| halt / spin-poll / countdown (of Peanut's frame) | 75.1% / 0.9% / 3.4% | 87.4% / 0.0% / 0.8% | 0.0% / 0.2% / 1.2% |
| instructions per frame | 2,153 | 1,167 | 7,861 |
| average M-cycles per instruction (not halted) | 2.29 | 2.20 | 2.25 |
| `CB`-prefixed instructions | 2.0% | 7.4% | 1.3% |
| **MBC (bank switch) writes per frame** | **7.8** | 0 | 0 |
| VRAM tile-data / map writes per frame | 4.8 / 1.9 | 0.0 / 0.0 | 1.0 / 0.5 |
| OAM DMA per frame | 1.0 | 1.0 | 0 |
| **lines with a mid-frame `SCX`/`SCY`/`LCDC`/palette/window write** | **1.1** | 0 | 0 |

Super Mario Land's I/O traffic per frame:
- reads: `STAT` (FF41) 21.5, the joypad 8.9;
- writes: the joypad 3.3, `SCX` 2.2, `SCY` 1.1, DMA 1.1.

**Its most expensive opcodes, by share of busy cycles:**

| opcode | instruction | share |
|---|---|---|
| `20` | `jr nz` | 14.7% |
| `F0` | `ldh a,(n)` | 8.5% |
| `CD` | `call` | 5.7% |
| `FE` | `cp n` | 4.7% |
| `19` | `add hl,de` | 4.4% |
| `E0` | `ldh (n),a` | 4.2% |
| `28` | `jr z` | 3.2% |
| `7E` | `ld a,(hl)` | 2.7% |
| `FA` | `ld a,(nn)` | 2.5% |

libbet's heaviest are `jr nz` 10.8%, `call` 7.9% and the `CB` page 6.5%.

## 3. What the numbers say

1. **A real game leaves most of the CPU idle, and that time is free to an
   emulator.** Super Mario Land, a full-screen scrolling platformer, uses
   **23%** of its CPU on average and half of it in its worst 1% of frames.
   A quiet puzzle game uses **14%**, with almost no spread. The feasibility
   estimate's corner case ("a game busy under ~20%") contains at least one
   real homebrew game, and comes within a few points of containing a
   commercial one.
2. **The work an emulator carries is about 2,150 instructions a frame for
   Super Mario Land and 1,170 for libbet**, at about 2.2-2.3 M-cycles each.
   That instruction count, times the cost per instruction, is the CPU budget
   docs/plans/REALDMG-PLAN.md is built on.
3. **Bank switching must be cheap. A 16 KB copy per switch is ruled out by a
   real game.** Super Mario Land writes the MBC 7.8 times a frame, which
   prices the feasibility estimate's 205,000-clock copy at about 20 frames of
   5150 time per Game Boy frame. Banking has to be a pointer or page-table
   change.
4. **A frame-at-once renderer is wrong for an ordinary game.** Super Mario
   Land rewrites a scroll register once per frame mid-screen, to split its
   status bar from the playfield. The renderer has to apply register changes
   at the line where they happen. That costs nothing per pixel if writes are
   logged with their `LY` and the frame is drawn in bands between them.
5. **`ldh` is an eighth of the busy cycles.** `ldh a,(n)` and `ldh (n),a`
   together are 12.7% of Super Mario Land's. They reach HRAM variables and
   I/O registers, so the `FFxx` path must not go through the general memory
   map.
6. **VRAM traffic is small.** Super Mario Land streams about 2 map cells and
   5 tile bytes a frame, and libbet's measured stretch writes none. Most of
   what the picture costs is redrawing unchanged data, not converting new
   data.

## 4. Reproducing it

```
python3 tools/gbprof.py SML.GB                       # 7,200 frames, platformer
python3 tools/gbprof.py libbet.gb --mode puzzle
python3 tools/gbprof.py cpu_instrs.gb --frames 3000 --mode puzzle
```

libbet was built with `make libbet.gb` in a clone of pinobatch/libbet at
`1232e71d`, with rgbds `d766efe9` (`make` in its clone) on `PATH`. ROM
SHA-256s: libbet `3a749f36...edb908f`, Super Mario Land
`49fbd2f6...be41dc`.

## 5. Prior art found alongside, not measured

**no$gmb** (Martin Korth, 1997-2000) is a Game Boy emulator written in
assembly. The Internet Archive's copy of version 2.5 lists
`no_gmb0.zip`, a "Special DOS XT/AT compatible version - a bit slower".
Another archive reports that it runs on an XT, needs EMS for larger ROMs and
has weak sound. This container's network policy refuses archive.org and the
emulator's own pages, so the build was neither read nor run here. It is the
nearest existing calibration for what an expert assembly implementation does
on these exact machines. A field run on the 5150 and the 286 with a homebrew
ROM would be worth having, and docs/plans/REALDMG-PLAN.md asks for it.
