# A precompiled RealDMG cartridge on kern_mega (2026-10-26)

Taken on `mega-dmg` over `a67ab0ef` + the memory branch merged at `fc5e8c96`,
with `realdmg-fullstatic`'s `c274efa9` merged, and the working tree of the
change that lands REALDMG-SPEC 20.15, in a Linux container: Bochs 2.7
(`make megatrap`'s kernel) and this tree's 86Box. True of that tree and of no
other. The commercial cartridges are the owner's and were never committed.

## 1. Bochs: `tests/rdgaotmega.py`

blargg's cpu_instrs precompiled with `--squeeze 0x210` (ten code images, 30
relocations, blocks left to the interpreter), checked against the
interpreter on the host first (`gbaot.py check --serial`: link port
identical, all eleven passed):

| leg | the package | result | images, based at | bank 1 | traps in core/images |
|---|---|---|---|---|---|
| shipped | plain (no `--mega`) | refused | - | - | - |
| embed | `--mega`, ROM inside | passes all eleven | 10, F1CC00h.. 64KB apart | FBCC00h (in place) | 0 (93 the package's own) |
| beside | `--mega`, ROM beside | passes all eleven | 10, F1CC00h.. | FBCC00h | 0 |
| oneclaim | `--mega` + `RW_NOXPLAN`, ROM inside | passes all eleven | 10, F38C00h.. | 02BA00h (one claim) | 0 |
| failing case | the image from before 20.15, its #error and refusal taken out | **red**: the game never starts, S_JITFIX 0 | 0 | - | - |

`--rom` with Kirby's Dream Land (256KB, unsqueezed): precompiled (S_JITON,
S_JITFIX), 4 images at F1CC00h-F4CC00h, banks in place at F5CC00h - or, one
claim, at 02BA00h - 0 traps in the core or an image, ~2,520 frames in 45 s.

## 2. 86Box `286-mega` (the owner's mr286, 4MB): Looney Tunes

The owner's LOONEY.SS1 and LOONEY.RP1 (the replay the static branch uses for
its counters too) on LTC.GB, 1MB. Packages made with `gbaot.py make
--replay` (7 code images, 2 of them hot, 2,681 listed, 2,732 relocations),
then `tests/rdgcore/rdgwin.py --vm 286-mega --replay SS1,RP1 --press
w3000,f7 --secs 70 --persec`. The first two seconds are the state's load
(779% and 800% read as speeds), so the means are of seconds 3-70:

| | kern_big, plain package | kern_mega, the recompiler (mega build) | kern_mega, `--mega` package |
|---|---|---|---|
| mean of seconds 3-70 | 94.9% | 91.5% | **94.7%** |
| seconds under 70% / under 90% / over 105% | 0 / 21 / 9 | 4 / 30 / 6 | 0 / 23 / 8 |
| slowest second | 74% | 60% | 72% |
| code images / tiers | the bank cache's heap | 5 extra tiers, 951 blocks translated | all 7 images, nothing translated |
| banks fetched in the span | 3 | 0 | 0 |

On this replay the precompiled package runs the same on both kernels - what
kern_mega adds is room, not speed: every image and every bank resident at
once, none fetched, and the conventional heap given back. Both precompiled
runs are steadier than the recompiler, which spends its first seconds
translating.

## 3. A defect found on the way: reads into the pool

The first kern_mega runs of the 1MB cartridge - the recompiler's arm and the
package alike - had not loaded in 150 emulated seconds (`rdgwin`'s
`RDGWIN_FAILSHOT` showed the load's progress bar still running). Since
REALDMG-SPEC 16.2.6 the arm declares `OS88_HIGHMEM`, so the in-place load's
16KB read slot, and the frame buffers the package's loader reads its images
through, were claims in the extended pool, and a file read into memory above
1MB bounces a sector at a time. Both are `OSAPI_MEM_CLAIM_DMA` claims now
(conventional, no page constraint) and the same load finds the game; the
numbers above are with that in. Kirby's 256KB had loaded slowly enough to
pass unnoticed in 16.2.6's measurement.

## 4. Not measured

- The load times themselves, before and after the slot fix.
- A cartridge with more images than the pool holds (the heap's claims take
  the rest), and the 14-image maximum.
- Pokemon, Link's Awakening and the other owner cartridges beyond Kirby and
  Looney Tunes.
