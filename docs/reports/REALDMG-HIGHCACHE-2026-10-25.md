# RealDMG's extra cache tiers in the extended pool (2026-10-25)

Taken on `mega-dmg` over `5dba1093` (and, for 2 and 2.1, `bd887b02`, the
memory branch merged at `f068399f`) with the working tree of the change that
lands REALDMG-SPEC 16.2.6 (docs/plans/REALDMG-HIGHCACHE-PLAN.md R1-R4), in
a Linux container: Bochs 2.7 (`make megatrap`'s kernel) and this tree's
86Box. True of that tree and of no other.

## 1. What changed

| | before (16.2.5) | after (16.2.6) |
|---|---|---|
| extra tiers | cache B's claim regrown, conventional | one extended-pool block, a `SEG_SUBC` pair a tier |
| how many | what conventional memory left, less 32KB (2 on a 640KB machine) | 5 by default; Cache: All or Static up to 55 |
| cache B | conventional, regrown past 64KB | a plain 64KB claim, above 1MB (`OS88_HIGHMEM`) |
| tiers the turn can count | 8 (`S_JITUSED`, a bit each) | 58 (the tiers' generation words) |
| a move of the tiers | flushed the cache (`rt_init`) | a rebase: nothing flushed |

## 2. Bochs: `tests/rdgmega.py`

cpu_instrs under the recompiler, `MEGATRAP=1` (every completed #GP logged):

| leg | cpu_instrs | extra tiers, based at | cache B at | traps in the core or a cache |
|---|---|---|---|---|
| shipped (kern_mega: must refuse) | refused | - | - | - |
| arm (banks in place) | passes all eleven | 5, F4D000h F5D000h F6D000h F7D000h... | FB9000h | 0 (94 the package's own, at init) |
| oneclaim (`RW_NOXPLAN`) | passes all eleven | 5, F81000h... | above 1MB | 0 |
| squeezed (`RT_SQUEEZE`, `RW_XDEF=2`) | passes all eleven | 2, F99000h FA9000h; 6 recycles | above 1MB | 0 |

The squeezed leg first ran with five extra tiers and recycled once against
the eight it requires: eight tiers of 0.5KB hold nearly all of cpu_instrs, so
the turn hardly came round. It is built with two now, as 16.2.5's machine
had, so every tier is written, recycled and written again; the arm leg is
what shows five.

### 2.1 A kernel defect the one-claim leg found

With `OS88_HIGHMEM` the oneclaim leg's 64KB cartridge claim went to the pool,
and the load said **"REALDMG: read failed"** (caught by screenshots every
0.3 s; the machine was idle afterwards, not hung). The same build with the
claim a KB larger (conventional) passed cpu_instrs, and with a 32KB ROM
(blargg's `instr_timing`) in a 32KB pool claim the read worked and the test
passed. So: `OSAPI_FILE_READ`, DX:CX = 1:0000, of a 65,536-byte file into a
64KB pool claim fails, on kern_mega with the memory branch merged at
`f068399f` (its `dcbffc9e`, a transfer past a selector's 64KB stepping its
window, included). RealDMG's arm claims a 64KB one-claim cartridge as 65KB
under `CPU_F_PM`; the table above is with that in.

## 3. 86Box `286-mega` (the owner's mr286, 4MB): the owner's replay

Kirby's Dream Land, slot 1's state and `KIRBY.RP1` (the first level and its
miniboss), `tests/rdgcore/rdgwin.py --vm 286-mega --replay SS1,RP1 --press
w3000,f7 --secs 70 --persec`. Speeds per second from the third (the state's
load lands in the first two):

| | kern_big, shipped, Cache: 128K | kern_mega, the arm |
|---|---|---|
| frames in 70 emulated s | 3,814 (91.2% of real time) | 4,098 (**98.0%**) |
| mean of seconds 3-70 | 92.0% | 99.2% |
| seconds under 70% / under 90% / over 105% | 4 / 30 / 6 | 1 / 3 / 9 |
| slowest second | 52% (second 59, a tier recycled) | 44% (second 12, a scene change, no recycle) |
| tiers recycled in the span | 1 | 0 |
| banks fetched in the span | 3 | 0 |
| extra cache tiers | - | 5 |

The kern_big run is the first of these two, taken by accident with the
default system disk: `rdgwin` boots `build/os8088.img` unless `RDGWIN_A`
names another, and with kern_mega it needs `RDGWIN_A=build/mega.img
RDGWIN_O88=build/megak/apps/realdmg.o88 RDGWIN_LST=build/megak/apps/
realdmg.lst`. It now reads the view through `os88box`'s `lin()` - an LDT
selector's base on kern_mega - rather than `segment x 16`.

The replay is short of what thrashed before: it used ~137KB of code (the
earlier report, 3.1), which two segments and the core's holes nearly hold
already. What the pool's tiers buy shows on a LONG play; this replay shows
that they cost nothing and that the arm's run is steadier.

## 4. Not measured

- The conventional memory given back. By arithmetic, cache B (64KB), the
  two conventional extra tiers 16.2.5 took on a 640KB machine (128KB), the
  frames (17KB) and the cartridge's RAM and damage memory are out of the
  heap; nothing here read the free figure before and after.
- A play long enough to fill five tiers, or Cache: All's fifty-five.
- The hibernate with a game open (R5 is not built).
