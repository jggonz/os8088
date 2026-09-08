# Kernel bytes since the last squash with `main`, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-09-07 on a four-core cloud
container from a cold checkout — `nasm` 2.16.01, no QEMU, MartyPC built at the
pinned commit. Every figure below comes from `tools/kernsize.py --json` and
`--modules`, which RE-ASSEMBLE the kernel rather than reading a build, so each
point is measured on its own tree and no figure is carried between them. It is
true of the four commits it names and of no other tree; a later measurement is
a new file.

## The four points

| | commit | what it is |
|---|---|---|
| **A** | `c24854a` | **the last squash with `main`** — *1.2MB disks for every application floppy (#150)*, 2026-09-04. `git merge-base origin/main origin/elendilon` |
| **B** | `c26cfbf` | `elendilon` at its tip, **+237 commits** over A |
| **C** | `783a322` | `main` at its tip, **+9 commits** over A |
| **D** | `e6dc3bf` | the merge of B and C |

So **A→B is the branch's own arm**, **A→C is everything `main` did**, and
A→D is what the tree carries now. B and C are siblings, not ancestors: `main`
squash-merges, so A is where the two last agreed.

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count as a decimal string (SPEC.md 14.2) and the four counts are 135,
372, 144 and 382 — three digits at every point, so the About box's string is
three bytes at every point. A comparison that crossed 999→1000 would not be
able to say this.

**`KERN_BUDGET` is 129,536 at all four points and `KERN_SMALL_BUDGET` 107,520.**
Nothing below is a budget move; every figure is a size move, which is the
distinction `kernsize` reports separately and the one that decides whether a
number is a decision somebody took or an answer the assembler gave.

## Headline — `kern_big`, the shipped default

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 50,993 | 50,949 | 51,083 | 51,051 | **−44** | **+90** | **+58** |
| `.bss` | 6,067 | 6,003 | 6,141 | 6,077 | **−64** | **+74** | **+10** |
| `.cold` | 37,310 | 38,544 | 37,734 | 38,967 | **+1,234** | **+424** | **+1,657** |
| `.ovl` | 1,418 | 1,417 | 1,418 | 1,417 | −1 | 0 | −1 |
| `.ovlw` | 5,037 | 5,037 | 5,037 | 5,037 | 0 | 0 | 0 |
| `.lowbss` | 9,182 | 9,182 | 9,182 | 9,182 | 0 | 0 | 0 |
| `.vgabuf` | 848 | 848 | 848 | 848 | 0 | 0 | 0 |
| **sum** | | | | | **+1,125** | **+588** | **+1,724** |
| **`KERN_SIZE`** | 110,080 | 111,616 | 110,592 | 112,128 | **+1,536** | **+512** | **+2,048** |
| spare of `KERN_BUDGET` | 19,456 | 17,920 | 18,944 | 17,408 | | | |

`KERN_SIZE` moves in 512-byte rungs, so the three arms are 3 rungs, 1 rung and
4 rungs. **They are exactly additive here** — 3 + 1 = 4 — which is luck rather
than a law: two arms that each spend 300 bytes of one rung's slack cross it
once together and never apart.

## Headline — `kern_small`, the 128KB floor machine

| section | A base | B elendilon | C main | D merged | **B−A** | **C−A** | **D−A** |
|---|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 39,261 | 39,392 | 39,325 | 39,468 | **+131** | **+64** | **+207** |
| `.bss` | 4,847 | 4,867 | 4,854 | 4,871 | **+20** | **+7** | **+24** |
| `.cold` | 25,885 | 27,186 | 26,070 | 27,373 | **+1,301** | **+185** | **+1,488** |
| `.ovl` | 423 | 423 | 423 | 423 | 0 | 0 | 0 |
| `.ovlw` | 2,789 | 2,789 | 2,789 | 2,789 | 0 | 0 | 0 |
| **`KERN_SIZE`** | 78,336 | 79,872 | **78,336** | 79,872 | **+1,536** | **0** | **+1,536** |
| spare of `KERN_SMALL_BUDGET` | 29,184 | 27,648 | 29,184 | 27,648 | | | |

**The single most useful line in this document is `main`'s zero.** `main`'s
nine features cost the floor machine 256 bytes of section and **not one byte of
footprint** — no rung crossed, `KERN_SIZE` identical to the base.

Not because those bytes are free. `.cold` is RESIDENT — the ladder runs
KERNEL → COLD → FAT → LOW → VGABUF → HEAP and `KERN_SIZE` spans the lot — so
all 256 of them are resident bytes. They fitted inside the slack the current
rungs already had, which is the distinction CLAUDE.md's rung rule is about: a
rung says WHEN the machine pays, never what a change cost. The next 256 bytes
on that build may well cost the full 512. Every byte of the merged +1,536 on
`kern_small` is the branch's own.

Note the shape difference between the two builds: on `kern_big` the branch's
arm is **−44** of `.text` and on `kern_small` it is **+131**. That is not a
contradiction — `vmmouse.inc` leaving `kern_big` for its own kernel
(SPEC.md 9.11.7) is −159 of `.text` that the small build never carried.

## What `main` did, module by module

`main`'s nine commits, newest first:

```
783a322  Telnet becomes a BBS terminal: ANSI-BBS colour, an 80x25 full screen, and Zmodem downloads (#166)
92f99f9  PaccMan: pacman.c ported to a C package, a second Pac-Man beside the Atari one (§91) (#165)
409e073  Fix PS/2 menu bar hit testing (#163)
df08d01  Add os8088 imager for interactive floppy, USB and CD writing (#159)
a20e2cd  Add system font viewer (#158)
93a615e  Port Atari Pac-Man to the native os8088 package ABI (#156)
732eee3  Release zip README: the C64 ROMs are inside C64.O88, not beside it (#155)
8b101e9  The Wire: .WPK archives, run from RAM, and the fix for buttons drawn into a launched window (#154)
7ae7627  The Wire: an online software library, a driver-registered desktop zone and OSAPI_PKG_RUN (#151)
```

Eight kernel modules moved, and **all of it is The Wire and the PS/2 fix** —
the imager, the font viewer and both Pac-Men are package and host-tool work
that costs the kernel nothing.

| module | `.text` | `.cold` | `.bss` | total | what it is |
|---|---:|---:|---:|---:|---|
| `desk.inc` | 0 | **+175** | **+67** | **+242** | the desktop SERVICE zone a driver registers (§26.7) — the Wire's launcher |
| `loader.inc` | 0 | **+137** | +4 | **+141** | `OSAPI_PKG_RUN`: `ld_alloc`/`ld_start` split out so an image already in memory can be run (§21.5) |
| `vga12.inc` | 0 | **+67** | +3 | **+70** | §5.4.2.2.1's Map Mask split — `gfx_blit1`'s fourth refusal lifted |
| `disk.inc` | 0 | **+45** | 0 | **+45** | |
| `ui.inc` | **+45** | 0 | 0 | **+45** | the service zone's double-click, and the PS/2 menu-bar hit test (#163) |
| `kernel.asm` | **+40** | 0 | 0 | **+40** | two API slots and their thunks, plus the callback wrappers |
| `menu.inc` | +4 | 0 | 0 | +4 | |
| `wm.inc` | +1 | 0 | 0 | +1 | |
| **total** | **+90** | **+424** | **+74** | **+588** | |

`main` touched no other kernel file. **All 588 bytes are resident** — 72% of
them are `.cold`, and `.cold` is inside `KERN_SIZE`'s span, not something the
machine gets back. On `kern_big` they crossed one rung; on `kern_small` main's
smaller 256 fitted the slack and crossed none.

## What the branch did, module by module

Twenty-three modules moved on the branch's arm; the ten largest by absolute
total:

| module | `.text` | `.cold` | `.bss` | total |
|---|---:|---:|---:|---:|
| `memory.inc` | +172 | +375 | +2 | **+549** |
| `lz.inc` | 0 | +340 | 0 | **+340** (a new file — the decompressor) |
| `vmmouse.inc` | −159 | −124 | 0 | **−283** (moved to `kern_emu`, §9.11.7) |
| `diskw.inc` | 0 | +244 | +6 | **+250** |
| `files.inc` | +34 | +72 | 0 | **+106** |
| `vidsel.inc` | −82 | 0 | 0 | **−82** |
| `wm.inc` | +12 | +47 | +18 | **+77** |
| `hiber.inc` | −13 | −30 | −24 | **−67** |
| `instance.inc` | +34 | 0 | +24 | **+58** |
| `sched.inc` | +50 | 0 | 0 | **+50** |

The branch's arm is not one feature: it is 237 commits, and the two arms
overlap in only four modules (`loader.inc`, `vga12.inc`, `disk.inc`,
`kernel.asm`, plus a one-byte brush past each other in `wm.inc` and `ui.inc`).

## What the MERGE itself cost, over and above the two arms

Sum the arms and compare with the merged tree:

| | `.text` | `.cold` | `.bss` |
|---|---:|---:|---:|
| (B−A) + (C−A) | +46 | +1,658 | +10 |
| D−A, measured | **+58** | **+1,657** | **+10** |
| **the merge's own cost** | **+12** | **−1** | **0** |

**Twelve bytes of `.text`, and they are the API table collision.** Both sides
appended to the same tail in the same round: the branch took `0x04F8`–`0x0518`
for five slots and `main` took `0x04F8` and `0x0500` for two, and
`apps/os88api.inc` merged with **no conflict marker and two names at one
address** — the exact failure `docs/UPSTREAM.md` carries a check for. Keeping
both means `main`'s two move to `0x0520` and `0x0528`, and the table goes
157 → 164 slots.

| | A | B | C | D |
|---|---:|---:|---:|---:|
| API slots | 157 | 162 (+5) | 159 (+2) | **164 (+7)** |

Seven, not five and not two — which is the whole point of resolving it by
hand. Everything else in the merge was byte-neutral: the `gfx_blit1` frame
grew from seven words to nine on the stack rather than in the image, and
`loader.inc`'s compression steps moved onto `main`'s restructured `.read`
rather than being duplicated.

## Where that leaves the two kernels

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` | 112,128 | 79,872 |
| budget | 129,536 | 107,520 |
| **spare** | **17,408** (34 steps of 512) | **27,648** (54 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` | 57,128 of 65,536 — **8,408 left** | — |

`KERN_CODE_MAX` is the one that cannot be raised (offsets are 16 bits), and it
is the tighter constraint in proportion: 8,408 bytes against `KERN_BUDGET`'s
34 steps. `main`'s arm spent 164 of those 8,408 and the branch's spent −108.

## Two things this measurement does not say

- **It does not price `kern_emu`.** That build is `kern_big` plus §9.11, and
  the branch created it inside this window; measuring it against A would be
  comparing a kernel to one that did not exist.
- **It does not name a section a machine gets back, because almost none of
  this is one.** `.text`, `.bss`, `.cold`, `.lowbss` and `.vgabuf` are all
  inside `KERN_SIZE` — only `.boot2` and `.ovl`/`.ovlw` are loaded into memory
  the machine reuses once it is up, and those did not move here. So of the
  merged +1,724 section bytes, **+1,725 are resident** and the single
  non-resident byte is `.ovl`'s −1. `KERN_SIZE`'s +2,048 is those 1,725 bytes
  billed at 512-byte granularity, not a different quantity.
  docs/KERNEL-MEMORY.md is the authority on which section costs a machine
  what.
