# RealDMG's recompiler, its window and its full screen: what each bought

**A measurement, taken in emulators.** 2026-10-19, a four-core cloud
container, branch `gameboy`, the tree this file was committed in.
- The 286 figures are 86Box's `vm/286` (the `ami286` at 12.5 MHz, DRAM at 1
  wait state). 86Box times instructions by its model and is not
  cycle-accurate (docs/86BOX.md, *What it is NOT*).
- The bare figures are `tests/rdgcore/rdgbench.py --box` (no os8088, no
  picture); the package's are `tests/rdgcore/rdgwin.py` (os8088 under it, the
  picture drawn, Super Mario Land started and walked right for 20 emulated
  seconds, the speed off the core's own frame counter).

Neither is the owner's iron. docs/reports/REALDMG-CORE-COST-2026-10-19.md is
the interpreter's measurement this one follows; a later one goes in a new
file.

## 1. The package, Super Mario Land in play

| | window | full screen |
|---|---|---|
| the first build (wave 3) | 52% at 3 frames drawn a second | - |
| the worker reading the clock every frame, LYC computed | 72.7% at 2 | - |
| drawing by the clock (every 3 ticks), one yield a frame | 55.7% at 6 | - |
| `rdg_render4` (no second pass over the frame) | 59.5% at 6 | - |
| full screen, mode 0Dh, 6,000-M-cycle budgets | - | 58.0% at 8 |
| **the recompiler** | **80.1%** | **76.0%** |

Kirby's Dream Land, full screen, in play: 27.8% before its two-test
spin-wait was recognised (REALDMG-SPEC section 8), 37.5% after - both
interpreted.

## 2. The recompiler, bare

| | interpreter | recompiler |
|---|---|---|
| gbclock, per busy M-cycle | 26.2 clocks | **13.95** |
| Super Mario Land, CPU only, a frame | 13.04 ms (77.9%) | **11.67 ms (69.7%)** |
| ...the interpreter with the recompiler's hooks live | 15.38 ms (91.9%) | - |
| ...and with them patched back to the plain dispatch | 13.73 ms (82.0%) | - |

The hooks cost a machine that does not translate: every interpreted control
transfer looked at `S_JITON` and RAM on its way, 18% on Super Mario Land.
Without a recompiler the package now writes the plain dispatch back over all
45 of those sites (`rt_dispatch`, REALDMG-SPEC section 16.3), which buys all
but ~5% of it back - the 5150 never translates (section 16.2).

## 3. How it got there

Each step was a profile of the one before (`rdgprof.py --box`, now charging
the cache segment), and three of them were not what was expected:

1. **A first build was 9x slower than the interpreter.** The code went into
   the core's own padding, 476 gaps of ~100 bytes, and a memory template with
   its guards needs 60-112: the allocator skipped most of the cache looking
   for room. A 64KB segment of its own, with copies of the core's helper
   tails at their offsets, fixed the shape.
2. **Then a block was translated on every visit.** A count of entries before
   translating (16) was added, and the counting path lost the bank key -
   every block was filed under bank 0 and never found again. A ring of the
   last eight PCs translated showed it at once.
3. **Then the recompiler was correct and no faster.** 86Box's 286 is bound by
   fetching code bytes, so dropping the dispatch saves its five bytes and
   little else; the memory templates spent ~35 bytes and a jump over their
   slow path. Moving the slow path before the fast access, dropping
   `xor dl,dl`, and choosing a one-`MOV` template for `LDH`/`LD (nn)` by
   operand took gbclock from 24.6 to 13.95 clocks.

Chaining exits to translated blocks, an inline cache on every `RET`, and
`cnt_ff` for the OAM DMA routine's countdown (REALDMG-SPEC section 8.1) are
the rest.

## 4. What is left, by the last profile

Of Super Mario Land's 11.67 ms: translated code ~45%, rdgsys's events ~12%,
`LY`/`STAT` reads ~6%, `tr_enter` and the interpreter ~15%. None of it is a
single fat item.

## 5. Reproduce

```sh
python3 tests/rdgcore/rdgbench.py --box                      # gbclock
python3 tests/rdgcore/rdgbench.py --box --rom SML.GB [--nojit]
python3 tests/rdgcore/rdgprof.py SML.GB --box --samples 3000 [--nojit]
python3 tests/rdgcore/rdgwin.py SML.GB --press w3000,return --hold 0x14D [--fs]
```

The games are the owner's and are not in this repository.
