# RealDMG drawing by damage, and the draw clock: what they bought

**A measurement, taken in emulators.** 2026-10-19, a four-core cloud
container, branch `gameboy`, the tree this file was committed in.
- **The 8088 figures are MartyPC's** IBM 5150 (`os8088_5150_herc_sb_720_gla`,
  a Hercules card). MartyPC is cycle-exact, so these are the 5150's numbers.
- **The 286 figures are 86Box's `vm/286`** (the `ami286` at 12.5 MHz, DRAM at
  1 wait state). 86Box is not cycle-accurate (docs/86BOX.md).

Neither is the owner's iron. docs/reports/REALDMG-JIT-2026-10-19.md is the
measurement before this one.

## 1. What changed

- **`rdg_renderd`** (REALDMG-SPEC section 13.2) composes only the lines whose
  inputs changed since the last draw, and writes only the lines that then
  differ.
- **Hercules and CGA full-screen writers** (section 15.1) put those lines
  straight on the page.
- **The draw clock** (section 15.2): a draw is due only once as long as the
  last one took has passed since it ended. A machine behind catches up half
  a second, not eight frames.
- **A HALT lets the budget lapse** instead of stepping it 700 M-cycles at a
  time, which had been 25 events a frame.

## 2. The 5150

| ROM, where | before | after |
|---|---|---|
| gbclock's do-nothing variant, Hercules window | 43% | **100%** |
| `GBDEMO.GB` (REALDMG-SPEC section 18), full screen | 42% (drawing every frame) | **1.000**, 4 frames drawn a second |
| `GBDEMO.GB`, window | 84% | **0.999**, 3 frames drawn a second |
| `GBDEMO.GB` full screen, with the draw clock always due (the negative control) | - | 0.388 |

The 43% was the drawing and not the CPU. Composing a whole frame and
dithering it cost ~0.7 s of a 5150, and the frames owed during that time
were lost to an eight-frame cap.

## 3. The 286

`tests/rdgspeed286.py`, 20 emulated seconds each, walking right:

| | window | full screen |
|---|---|---|
| Super Mario Land, before (the recompiler) | 80.1% | 76.0% |
| **Super Mario Land, now** | **100.7%**, 7 drawn a second | **101.5%**, 34 drawn a second |
| Kirby's Dream Land, now | 31.6%, 3 drawn a second | 37.2%, 4 drawn a second |

**Kirby is bound by the Game Boy CPU, not by drawing.** A profile of it full
screen gives this split:

| | share |
|---|---|
| translated code | 48% |
| the interpreter and `tr_enter` | 32% (`tr_enter` alone 12%) |
| the package (drawing and rdgsys) | 18% |

Sampling the Game Boy PC puts the time across ~30 routines, with no spin
left unrecognised. Kirby simply runs more instructions a frame than Super
Mario Land, and a larger share of them reach `tr_enter` through exits that
are not chained.

## 4. Reproduce

```sh
python3 tests/gbdemo.py                       # the 5150 demo, full screen
python3 tests/gbdemo.py --window
OS88_GB_ROMS=<dir> python3 tests/rdgspeed286.py
python3 tests/rdgcore/rdgwin.py KIRBY.GB --press w15000,return,w5000,return \
        --hold 0x14D --fs --profile 3000
```

The games are the owner's and are not in this repository.
