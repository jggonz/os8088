# Kernel bytes since the #268 squash, by concept

**A measurement, not a description.** Taken 2026-10-20 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Each of the **107
commits** of the round, and every parent and merge base they name, was
measured in a clean tree of its own: `git archive` of that commit's
`kernel/`, `boot/`, `tools/`, `drivers/`, Makefile and the top-level files of
`apps/`, `apps/stiklib.asm` assembled there into the `stiklib.bin` that
`deskmod.inc` includes, and that commit's own `kernsize` measuring
`kern_big` and `kern_small`. A tree whose every kernel input is blob for blob
one already measured takes that tree's figures. The module and driver images
come from `make` and `make small` at the three endpoints only. It is true of
the commits it names and of no other tree.

It is the eleventh of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-18.md` and the nine that report
lists, and is an edit of none of them. It is taken for the next
`elendilon -> main` round, so the question is that PR's: **what does this
round cost `main`'s kernel?** It is a short round - three days - and **it has
had no size pass**, so every byte below is unpassed.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `571befce` | **the last squash with `main`**: *Elendilon -> Main (Kernel in ROM and the HMA, …)* (#268), 2026-10-19 - `elendilon-pr` as cut at `10a03de9`, plus `main`'s seven review commits on top |
| **C** | `1e3608d8` | **`main`** at its tip: A plus #273 (*review-fork-pr: post a kickoff comment*), which touches `.claude/skills/` alone. **C is A to the byte** on every kernel input, so it is not a column below |
| **B** | `f982b603` | **`elendilon-next`**: `elendilon`'s round since `10a03de9` squashed onto C as one three-way merge with that base. Its round is `10a03de9..cb5debf7`, **107 commits**, 87 that are not merges and 20 merges |

**The round's base is not A, and that is the one thing to keep straight.**
`elendilon` kept going from `10a03de9`, the commit `elendilon-pr` was cut
from, while `main` took `elendilon-pr` plus its review fixes as #268. So the
round's commits are measured against their own parents on `elendilon`'s
chain, and B − A is that sum less what both sides carried: **one commit**,
`DESK.DRV: drop the image when the last widget goes`, which is `0c9e521` on
`elendilon-pr` and `a34c5059` on `elendilon` - the same change, +63 on
`kern_big`, counted once in B. The squash merge itself moves nothing on its
own: B − A is the round's sum less that +63, to the byte, in every section,
on both kernels.

**The build number is held at 236 for every tree** (B's own): `BUILD_STR` is
the commit count (SPEC.md 14.2), `elendilon`'s is four digits, and a digit is
not a cost anybody spent.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
64,000 on `kern_small` at A and B, with `KERN_GRACE` 2,048 and both kernels
booting on 128 KB. `make budget-check` passes at B on all three kernels -
`kern_emu` 97,792, 3,072 spare - so B could go to `main` as it stands.

## Headline: `kern_big`, the shipped default

| section | `10a03de9` | A, `main` | B | **A − base** | **B − A** |
|---|---:|---:|---:|---:|---:|
| `.text` | 42,706 | 42,716 | 42,798 | +10 | **+82** |
| `.bss` | 5,226 | 5,226 | 5,253 | 0 | **+27** |
| `.cold` | 37,444 | 37,501 | 37,459 | +57 | **−42** |
| `.lowbss` | 5,358 | 5,358 | 5,358 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 |
| **resident** | 91,070 | 91,137 | **91,204** | +67 | **+67** |
| window | 5,118 | 5,118 | 5,118 | 0 | 0 |
| blob | 5,896 | 5,896 | 5,929 | 0 | +33 |
| **`KERN_SIZE`** | 97,280 | 97,280 | **97,280** | 0 | **0** |
| spare of the budget | 3,584 | 3,584 | 3,584 | | |

**+67 resident against `main`, and no rung.** The round crossed TWO rungs and
gave both back: `gfx_blit4`'s region walk took `KERN_SIZE` +512 and `filecp`
over `READ_SEQ` took −512, both through the cold rung, so the footprint ends
where it started. The +67 is what is left of the round's +130 once the
`DESK.DRV` drop `main` already has is taken out.

## Headline: `kern_small`, the 128KB floor machine

| section | `10a03de9` | A, `main` | B | **A − base** | **B − A** |
|---|---:|---:|---:|---:|---:|
| `.text` | 31,132 | 31,132 | 31,138 | 0 | **+6** |
| `.bss` | 2,978 | 2,978 | 2,979 | 0 | **+1** |
| `.cold` | 21,949 | 21,949 | 21,970 | 0 | **+21** |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 |
| **resident** | 58,927 | 58,927 | **58,955** | 0 | **+28** |
| window | 1,489 | 1,489 | 1,486 | 0 | −3 |
| blob | 4,387 | 4,387 | 4,387 | 0 | 0 |
| **`KERN_SIZE`** | 60,928 | 60,928 | **60,928** | 0 | **0** |
| spare of the budget | 3,072 | 3,072 | 3,072 | | |

**+28 resident and no rung.** The floor machine pays for the Disk window's
folders-first sort, the review's two kernel fixes, Format's pair of sizes and
the larger `APP_MAX_SIZE`; the wheel is `kern_big`'s, and on `kern_small` it
is six bytes of `.text` in `kernel.asm`.

## The attribution is exact

Every commit was measured against its first parent, on both kernels. A merge
M gets a figure too, its RESIDUAL: what M moved against its first parent,
less what its second parent moved against the merge base. **All 20 merges
have a residual of 0 in every field**, and the commits sum to
`cb5debf7 − 10a03de9` exactly, in every section, the window and the blob, on
both kernels (checked, not assumed). **78 of the 87 commits move no kernel
byte**: the Video Player and its encoder, the VGA demo drive, the project
guide, PiXEL, the plans and the tests.

## `main`'s arm: the review fixes, +67 on `kern_big`

`10a03de9 → A`, what #268 carried past the commit `elendilon-pr` was cut
from. Measured per commit on `elendilon-pr`; two of its 34 commits move a
kernel byte.

| commit | what it touched | `kern_big` resident | `kern_small` resident |
|---|---|---:|---:|
| `0c9e521` *DESK.DRV: drop the image when the last widget goes* | `deskmod.inc`, `instance.inc`, `loader.inc` | **+63** (`.text` +10, `.cold` +53) | 0 |
| `a265d20c` *Kernel and ROM: …, DESK.DRV drop* (jggonz) - `ld_alloc`'s region refusal drops `DESK.DRV` | `loader.inc` | **+4** (`.cold`) | 0 |
| the other 32: tests, docs, PiXEL, `ETHER.DRV`, 1942, the HMA driver pool through a hibernate, `os88sym` | | 0 | 0 |

`main`'s review fixes cost **four resident bytes**; everything else they did
is in the modules (below). `0c9e521` is the commit `elendilon` carries too.

## Our arm, by concept

`10a03de9 → cb5debf7`, per concept, the sum of its commits. Every row is
exact and the rows sum to B − A once the `DESK.DRV` row is taken out.

| concept | commit | **`kern_big` resident** | **`kern_small` resident** | `KERN_SIZE`, window / blob, modules |
|---|---|---:|---:|---|
| **The mouse WHEEL scrolls the front window** (SPEC.md 13.18) | `51f123b` | **+131** (`.text` +68, `.bss` +26, `.cold` +37) | **+6** (`.text`) | big blob **+33**; small window −3. `USBMOUSE.DRV` +30 |
| **`gfx_blit4` under a region**: a block the region cuts walks it, every fragment a fast-path piece | `597de31` | **+86** (`.text` −4, `.cold` +90) | 0 | big `KERN_SIZE` **+512** |
| **`gfx_blit4`: a narrow block asks how dense its runs are** (SPEC.md 5.4.1.6) | `d2f6686` | **+18** (`.text`) | 0 | |
| **Review since the #252 squash: five small fixes** - `mod_disarm` under `cli`, `sc_walk`'s 13-character refusal (the other three are packages and `ETHER.DRV`) | `3a87088` | **+12** (`.cold`) | **+8** | |
| **The Disk window sorts folders ahead of files** (SPEC.md 19.4) | `d04f8d3` | **+12** (`.cold`) | **+12** | |
| **`APP_MAX_SIZE` 60 KB → 63.5 KB** | `3a229c1` | **+3** (`.cold`) | **+3** | |
| **Format Disk offers the drive's two sizes** | `0cdf1cf` | **−1** (`.bss` +1, `.cold` −2) | **−1** | `FORMAT.DRV` +43 on both |
| **`filecp` reads through `READ_SEQ`, shares the directory walk, moves `fcp_goto`** | `41a833a` | **−194** (`.cold`) | 0 | big `KERN_SIZE` **−512**; small `FILECP.DRV` −18 |
| **the round, less what `main` already has** | 8 | **+67** | **+28** | `KERN_SIZE` 0 / 0 |
| `DESK.DRV` drop with the last widget (also `main`'s `0c9e521`, above) | `a34c505` | +63 | 0 | |
| **the round as measured** | 9 of 107 | **+130** | **+28** | |

**The wheel is the round's one real resident cost**, and on `kern_big` alone:
the decode in the mouse ISR and the scroll in the UI task (`mouse.inc` +50
`.text`, `ui.inc` +13, `wm.inc` +24 `.bss`) and 33 bytes of boot blob that
`mem_unblob` gives back. **`filecp` paid for the `gfx_blit4` region walk and
then some**: it moved 345 bytes out of `filecp.inc`'s `.cold` and 155 into
`disk.inc`'s, where `READ_SEQ`'s directory walk is shared rather than copied.

## Where the bytes are, by file

`kernsize --modules`, A → B (`.text` / `.cold` / `.bss`), every row that
moved:

| file | `kern_big` | `kern_small` |
|---|---|---|
| `filecp.inc` | 0 / **−345** / 0 | 0 / −67 / 0 |
| `disk.inc` | −1 / **+155** / 0 | 0 / +79 / 0 |
| `vga12.inc` (`gfx_blit4`) | +14 / +90 / 0 | - |
| `mouse.inc` (the wheel) | +50 / 0 / +2 | - |
| `files.inc` (folders first, Format's sizes) | 0 / +35 / +1 | 0 / −2 / +1 |
| `wm.inc` (the wheel's scroll target) | 0 / 0 / +24, blob +34 | - |
| `ui.inc` (the wheel's event) | +13 / 0 / 0 | - |
| `mod.inc` (`mod_disarm` under `cli`) | 0 / +8 / 0 | 0 / +8 / 0 |
| `diskw.inc` | 0 / +8 / 0 | - |
| `kernel.asm` | +6 / 0 / 0, blob −1 | +6 / 0 / 0 |
| `desksc.inc` (`sc_walk`) | 0 / +4 / 0 | - |
| `loader.inc` (`APP_MAX_SIZE`) | 0 / +3 / 0 | 0 / +3 / 0 |

No file was added to the kernel this round, so `kernsize --modules` lists
nothing *(undescribed)*.

## How close B stands to the next rung

What the next addition is billed against, not what this round cost
(`kernsize`'s own rung line):

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A | 186 left | 387 left | 274 left |
| `kern_big` at B | **77 left** | 429 left | 274 left |
| `kern_small` at A | 194 left | 67 left | 204 left |
| `kern_small` at B | 187 left | **46 left** | 204 left |

**`kern_big`'s image rung is the near one now, 435/512 spent** - the wheel's
`.text` and `.bss` went there. `kern_small`'s cold rung is still the near
one, 466/512. Per CLAUDE.md's banner this decides WHEN the machine pays and
never what a change cost.

## The on-demand modules

Images unpacked, as `os88mod.py` reports them; the claim is the image rounded
up to whole KB. Every module not listed is the same image at all three points.

| module | `10a03de9` | A | B | **B − A** | claim A → B, KB |
|---|---:|---:|---:|---:|---|
| `CTRL.DRV` | 13,897 | 13,970 | 13,970 | 0 | 14 → 14 |
| `FORMAT.DRV` | 3,069 | 3,088 | 3,131 | **+43** | 4 → 4 |
| `HIBER.DRV` | 6,145 | 6,145 | 6,283 | **+138** | 7 → 7 |
| **`kern_small`** `FILECP.DRV` | 1,855 | 1,855 | 1,837 | **−18** | 2 → 2 |
| **`kern_small`** `FORMAT.DRV` | 1,188 | 1,188 | 1,231 | **+43** | 2 → 2 |

- **`main`'s review fixes put 73 bytes in `CTRL.DRV`** (`a265d20c`'s `.LNK`
  self-overwrite refusal in `sc_m_lnk`) and 19 in `FORMAT.DRV` (the
  ASSOC.DAT writer's re-index and read errors, `ascw.inc`). The HMA driver
  pool through a hibernate (`f4b56ebc`) re-laid `HIBER.DRV` and left it the
  same 6,145 bytes.
- **`HIBER.DRV` +138** is the PS/2 keyboard handed back on wake (`f9903bd`,
  `60e3cca`) and the wheel's mouse restored with it (`51f123b`). It was one
  byte over 6 KB at the last report, so the claim was already 7 KB and stays
  there.
- **`FORMAT.DRV` +43 on both kernels** is Format's two sizes.

## The loadable drivers

`make` at the three endpoints, image + bss. Every driver not listed is
byte-for-byte the same size.

| driver | `10a03de9` | A | B | **B − A** |
|---|---:|---:|---:|---:|
| `ETHER.DRV` | 17,006 | 17,011 | 17,011 | 0 |
| `USBMOUSE.DRV` | 1,396 | 1,396 | 1,426 | **+30** |

`ETHER.DRV`'s +5 is `main`'s: the detach RST at `SND.NXT` (`9d815b9f`),
−11 of image and +16 of bss. The review fix `3a87088` put `tcp_timers`'
retry on queued bytes in zero bytes. `USBMOUSE.DRV`'s +30 is the wheel.

## The final bill

**What the next round asks `main` to take, on the kernel**, with B the
squash as it stands:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at `main` (A = C) | 97,280 | 60,928 |
| `KERN_SIZE` at B | **97,280** | **60,928** |
| resident bytes, against `main` | **+67** | **+28** |
| window / blob (not resident) | 0 / +33 | −3 / 0 |
| `KERN_BUDGET` (unchanged) | 100,864 | 64,000 |
| spare of the budget | **3,584** (7 steps) | **3,072** (6 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 47,942 → **48,051**: **17,485 left** | 34,110 → **34,117**: **31,419 left** |
| guard 5, before it cannot boot on 128 KB | **23,552** | **60,416** |
| on-demand modules | `HIBER.DRV` +138, `FORMAT.DRV` +43 | `FORMAT.DRV` +43, `FILECP.DRV` −18 |
| loadable drivers | `USBMOUSE.DRV` +30 | the same drivers |

**In one line each:**

- **`main`** took `elendilon-pr` and spent **four resident bytes** fixing it,
  on `ld_alloc` dropping `DESK.DRV` after a refused region; the rest of its
  review is in `CTRL.DRV`, `FORMAT.DRV` and `ETHER.DRV`.
- **We** spent **+67 resident on `kern_big`** and **+28 on `kern_small`**,
  most of it the mouse wheel (+131), the `gfx_blit4` region walk (+86) and
  its density test (+18), and paid **194** of it back by sharing `filecp`'s
  directory walk with `READ_SEQ`. No rung moved on either kernel, and none of
  it has had a size pass.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s baselines did not describe B.** `make` at B
  printed `sum +100` on `kern_big`, `+102` on `kern_emu` and `+24` on
  `kern_small`: they were blessed with #268's review, before this round.
  **Re-blessed at B in the same commit as this file**, all three kernels.
- **SPEC.md carried `### 105.9 The window` twice, on consecutive lines**, on
  `main` and on `elendilon` alike - the duplicate-heading check in
  `docs/UPSTREAM.md` printed it. One copy removed on `elendilon-next`.
- **The squash's clean merge put `t_moddrop`'s paragraph into AGENTS.md
  twice**, each side having added it at a different line. Git reports nothing
  for that; a per-file count of lines the merge holds more often than either
  side found it. Main's placement kept.
