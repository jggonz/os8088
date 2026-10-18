# Kernel bytes since the #252 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-10-17 and 2026-10-18 on a
four-core cloud container: `nasm` 2.16.01, Python 3.13. Every resident figure
comes from `tools/kernsize.py --json` (and `--modules` for the per-file
table), which RE-ASSEMBLES the kernel rather than reading a build. Each of the
**797 commits** below was measured in a clean tree of its own: `git archive`
of that commit's `kernel/`, `boot/`, Makefile, `tools/`, `drivers/`, the
top-level `.inc` files of `apps/` and `apps/stiklib.asm`, with that commit's
own `kernsize` measuring the sections on `kern_big` and `kern_small`. The
module images come from the same trees: that commit's own Makefile was asked
(`make -n`) for its kernel-full `nasm` line and its `os88mod.py` line, on both
kernels, and both were run there. A commit whose every kernel input is
blob-for-blob a tree already measured takes that tree's figures (174 distinct
trees were assembled; the key is the blob list, so identical inputs are
identical bytes by construction). The loadable drivers were built by `make` at
the three endpoints only. It is true of the commits it names and of no other
tree.

It is the tenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-01.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-03.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-08.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-14.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `b47de145` | **the last squash with `main`**: *Elendilon -> Main (Apps Size Pass, File Multi-Select, .img as Drives, Windows Theme, EMS, PicoMEM V1/V2 Support, Video Player XMS and Palettes, Bugfixes)* (#252), 2026-10-14 |
| **C** | `ad1be5d6` | **`main`** at its tip, the fork's `origin/main`: **+11 commits** over A, #249, #251, #253 and #260-#267 |
| **B** | `ed793fc7` | **`elendilon-pr`, the PR**: `elendilon` as it was soaked (`10a03de9`) plus nineteen fixes. It already carries C (merged in during the round), so there is no trial merge to build: **B is the PR**. Its round is `C..B`, **782 commits**, 646 that are not merges and 136 merges |

**The round has a different shape from the last one, and it decides what
"our arm" means.** `elendilon` was not cut from A. It was cut from
`84a3038c` (*elendilon since the #252 squash, squashed onto main (#264)*):
everything `elendilon` carried past the commit #252 was cut from, applied to
`main` at #264 as ONE commit. So that commit is ours, it is in `C..B`, and
its kernel bytes are a row of their own below (*the carried squash*). `main`
then took #265-#267 and `elendilon` merged them in. So `C..B` is exactly
what B adds to `main`, and `A..C` is exactly `main`'s.

**Only one of B's nineteen fixes moves a kernel byte**: `0c9e521`
(*DESK.DRV: drop the image when the last widget goes*), +63 on `kern_big`.
The other eighteen are tests, the Video Player, `kernsize`'s theme table and
a fast-tier gate. **Three more fixes landed after B, in the commit before
this file's**: PiXEL's part 9 under an open's pressure (`fd477d4`),
`pit_now`'s IRR read (`0c82ffc`) and the `midirackcovoxpoll` window
(`548ae89`). They move **no kernel byte and no driver byte**: `kernsize` at
`548ae89` reads B's sections on all three kernels, and `ETHER.DRV`, the one
driver that includes `apps/os88pit.inc`, builds byte-identical (its profiler
is not assembled into the shipped driver). So everything below holds for
the PR's head as well as for B.

**A does not chain to the previous report's B to the byte.** That report's B
(`758eb807`, `elendilon-next` before its review fixes) is not in this
clone, so the chain cannot be checked commit by commit. Its figures against
A: `kern_big` 93,139 against **93,274** resident (+135), `kern_small` 60,235
against **60,238** (+3), and `KERN_SIZE` identical on both (98,816 and
62,464). The +135 is the review fixes #252 took on top of that report.

**The build number is held at 231 for every tree.** `BUILD_STR` is the commit
count (SPEC.md 14.2): 220 at A, 231 at C, and **1,013** at B, because
`elendilon`'s round runs through four digits. A digit is not a cost anybody
spent, so every tree is assembled with C's three-digit number. B built with
its own number is **+1 byte of `.text` on `kern_small`** (31,133 against
31,132) and +0 on `kern_big`, whose padding takes it. The rung table below is
`kernsize`'s own line in each endpoint's worktree, so it carries that byte.

**`associco.inc` is byte-identical at A, C and B** (each built from its own
packages: md5 `c4b3e546060c...`, the same copy the last two reports used). It
is the one generated include a package commit could move a kernel byte
through, so every tree was assembled against that one copy.

**One commit does not assemble on `kern_big`.** `d5d2d21` (*Links (\*.LNK)
and autostart*) overflows `BOOT2_SECS`: *"the boot blob ... does not fit
BOOT2_SECS sectors"*. Its successor `2bff6f7` (*Autostart's walk moves into
an 11-sector blob*) is the fix. `kernsize --json` still reports its sections
(the `-DKERNSIZE` pass stops short of that guard), so the resident figures are
per commit. Its module images cannot be cut, and the PAIR is one row in the
module figures: CTRL.DRV +212.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
64,000 on `kern_small` at A, C and B, with `KERN_GRACE` 2,048 and both
kernels booting on 128 KB (`MIN_RAM_KB`). Every spare figure below is a
size change and nothing else.

## Headline: `kern_big`, the shipped default

| section | A base | C main | B, the PR | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 43,726 | 43,645 | 42,716 | −81 | −1,010 | −929 |
| `.bss` | 5,214 | 5,235 | 5,226 | +21 | +12 | −9 |
| `.cold` | 38,400 | 37,810 | 37,497 | −590 | −903 | −313 |
| `.lowbss` | 5,598 | 5,358 | 5,358 | −240 | −240 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 | 0 |
| **resident** | 93,274 | 92,384 | **91,133** | **−890** | **−2,141** | **−1,251** |
| window | 5,097 | 5,097 | 5,118 | 0 | +21 | +21 |
| blob | 4,857 | 4,891 | 5,896 | +34 | +1,039 | +1,005 |
| **`KERN_SIZE`** | 98,816 | 98,304 | **97,280** | **−512** | **−1,536** | **−1,024** |
| spare of the budget | 2,048 | 2,560 | 3,584 | | | |

**−1,251 resident against `main`, and two rungs back.** `KERN_SIZE` falls
**98,304 → 97,280**, so every `kern_big` machine gets **1,024 bytes of heap**
back on top of the 512 `main`'s own arm returned. Kernel size pass 13 took
**1,815** resident bytes out, and the round's features spent 564 of them
again (below). The blob grows by a whole KB, and that is not a cost to the
resident count: autostart's walk and the pass's cold-to-blob moves put boot
code there on purpose, and `mem_unblob` gives the blob back once the desktop
is up.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | C main | B, the PR | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 31,865 | 31,836 | 31,132 | −29 | −733 | −704 |
| `.bss` | 3,063 | 3,063 | 2,978 | 0 | −85 | −85 |
| `.cold` | 22,442 | 22,482 | 21,949 | +40 | −493 | −533 |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 | 0 |
| **resident** | 60,238 | 60,249 | **58,927** | **+11** | **−1,311** | **−1,322** |
| window | 1,480 | 1,480 | 1,489 | 0 | +9 | +9 |
| blob | 4,341 | 4,341 | 4,387 | 0 | +46 | +46 |
| **`KERN_SIZE`** | 62,464 | 62,464 | **60,928** | **0** | **−1,536** | **−1,536** |
| spare of the budget | 1,536 | 1,536 | 3,072 | | | |

**−1,322 resident against `main` and three rungs back.** `KERN_SIZE` is
**60,928**; the floor machine gets **1,536 bytes of heap** back. The round's
features cost `kern_small` **+26** in all (the packed-part load +33 is most of
it), so pass 13 reaches the floor machine nearly whole: −1,348, most of that
the cross-file agent moving `kern_small`'s adapter switch and glyphs into
`CTRL.DRV`.

## The attribution is exact

Every commit was measured against its first parent, on both kernels, in two
ranges: `main`'s arm (A..C) and our round (C..B). A merge M gets a figure
too, its RESIDUAL: what M moved against its first parent, less what its
second parent moved against the merge base. In both ranges the commits'
figures and the merges' residuals **sum to the range's endpoints exactly, in
every section, the window, the blob and every module image, on both
kernels** (checked, not assumed; the module images bar the one pair that
does not assemble, above).

**132 of the 136 merges have a residual of 0 in every field. The other four
are pass 13's own merges, and they are real bytes**: `a7f9ac2` +4 on both
kernels, `3ba0606` −3 and `033b138` −3 on `kern_big`, and `7c866c4` (pass
13 onto `elendilon`) +6. Joining two sides that each moved code moves jump
distances and pads between them, so a merge can cost or return a few bytes
neither side spent. **This round no merge moves `KERN_SIZE` on its own**: the
commits' rungs sum to the endpoints' (−1,024 and −1,536) exactly, which the
last four reports could not say.

**43 of the 646 non-merge commits move a resident or section byte**, and 12
more move only a module image. The other 591 - the apps size pass 2 and its
fixes, the Video Player's VIDEO-386 round, Sticky Note and the Desk Clock,
PiXEL, isoTron and the manuals pass among them - measure 0 in every section
and module on both kernels.

## `main`'s arm: −890 resident on `kern_big`, +11 on `kern_small`

| commit | what it touched | `kern_big` resident | `kern_small` resident | other |
|---|---|---:|---:|---|
| #264 `a123653` *Timer and Bounce are packages* | `apps.inc`, `kernel.asm` | **−1,647** (`.text` −326, `.cold` −1,070, `.lowbss` −240, `.bss` −11) | **−56** | `KERN_SIZE` −1,024 |
| #251 `0a66250` *DIAs: desktop widgets, the desk layer (DESK.DRV) and the Desk Calendar* | `desk.inc`, `deskmod.inc` (new), `wm.inc`, `mod.inc` | **+427** (`.cold` +433, `.bss` +16, `.text` −22) | **+50** | `KERN_SIZE` +512; `DESK.DRV` 235 new, `CTRL.DRV` +181, `DOCK.DRV` +24 |
| #249 `3987ad0` *Lightweight EGA Color themes with persistent title stripes* | `wm.inc`, the theme | **+203** (`.text` +195, `.bss` +8) | **+9** | blob +34; `CTRL.DRV` +212, `WIN95.DRV` +22 |
| #265 `c22a3d6` *Locator's Sticky Apps menu, and the Sticky App card* | `menu.inc`, `stikmenu.inc` (new) | **+127** (`.text` +72, `.cold` +47, `.bss` +8) | **+8** | `STIKMENU.DRV` 14,758 new; `DISKOPS.DRV` +8 on both |
| #253, #260-#263, #266, #267 | the browser, CLAUDE.md, thm95, livefull, the `\STIKAPPS\` rebrand, Sticky Note, the Desk Clock | 0 | 0 | |

**`main` took two Timer and Bounce packages out of the kernel and gave a
whole KB of `KERN_SIZE` back with them**, which more than paid for the desk
layer, the EGA themes and the Sticky Apps menu it added in the same arm. The
`.lowbss` −240 is the one move of that section in either arm.

## Our arm, by concept

Our round (C..B), per concept, the sum of its commits. Each feature is listed
WITH its own pass-13 branch, so its figure is what it costs now. Every row is
exact, and the rows sum to the round.

| concept | commits | **`kern_big` resident** | **`kern_small` resident** | window / blob, modules |
|---|---|---:|---:|---|
| **Kernel size pass 13**, the six general agents: `dispsw` (Set Primary and the check-box/radio glyph into `CTRL.DRV`) −606, `wkgui` (the window system and drawing) −406, `wkcore` (the core; the attach half of a class driver load leaves resident) −334, `xfile` (the cross-file list) −160, `wkfs` (the file system and the Disk window) −92, `misc` (the smaller unpassed work) −55, and the shared rows −4 | 50 | **−1,657** (`.text` −1,078, `.cold` −576, `.bss` −3; the four merge residuals inside it) | **−1,346** | big blob +792, window +12; small blob +23, window +9. `CTRL.DRV` +1,057 (small +495), `HIBER.DRV` +613, `EXTD.DRV` +480, small `FILECP.DRV` +85, small `FDLG.DRV` +52, and +5 to three more |
| **ASSOC.DAT writes back what the machine learned** (SPEC.md 54.7.6), the installer holding the writer, the slot tables cut to 16/32, with pass 13's `assoc` branch (−32) | 8 | **+290** (`.text` +121, `.cold` +165, `.bss` +4) | **+6** | `KERN_SIZE` +512 alone; `FORMAT.DRV` **+1,881**, its claim 2 → 3 KB (4 before the pass) |
| **Links (`*.LNK`) and autostart from `\AUTOSTRT`**, the walk in an 11-sector blob, with pass 13's `links` branch (−30) | 6 | **+140** (`.cold` +140) | 0 | blob **+199**; `CTRL.DRV` +174 |
| **Sticky Apps, our half**: one shared library in `DESK.DRV` (SPEC.md 110.15), the menu on the desktop bar only, and the image dropped with the last widget, with pass 13's `desk` branch (−16) | 11 | **+41** (`0c9e521`'s drop +63) | −2 | `DESK.DRV` **+4,273**, `STIKMENU.DRV` +20, `DOCK.DRV` −10 |
| **A parted package may ship its image packed** (+33 `.cold` on both) | `a762938` | **+33** | **+33** | |
| **The carried squash**: what `elendilon` had past #252's cut, onto `main` at #264 | `84a3038` | **−25** (`.text` +60, `.cold` −75, `.bss` −10) | **−20** | big window +9, blob +14; small blob +23. `CTRL.DRV` −246, `DISKOPS.DRV` −272, `WIN95.DRV` −185, `HIBER.DRV` +35; small `CTRL` −23, `DISKOPS` −16 |
| **Themes**: Customize clickable, the Windows row and the corner saver back, with pass 13's `theme` branch, which passed `main`'s #249 too (−80) | 10 | **−80** | 0 | `CTRL.DRV` +60, `WIN95.DRV` −13 |
| **Fixes**: the manuals pass's eight bugs (`d511fdd`), `os88ui_gring`'s ring, Shut Down's warm reset, a serial on every disk the machine makes | 5 | **+7** | **+7** | `CTRL.DRV` +104 (small +101), `DISKOPS.DRV` +56 and `FORMAT.DRV` +47 on both |
| **the round** | 782 | **−1,251** | **−1,322** | big window +21, blob +1,005; small window +9, blob +46 |

### The arm, whole

| | `kern_big` resident | `kern_small` resident | `kern_big` `KERN_SIZE` | `kern_small` `KERN_SIZE` |
|---|---:|---:|---:|---:|
| kernel size pass 13, all ten branches | −1,815 | −1,348 | −1,536 | −1,536 |
| the features and fixes, with their pass rows taken out | +564 | +26 | +512 | 0 |
| **our net (B − C)** | **−1,251** | **−1,322** | **−1,024** | **−1,536** |
| `main`'s arm (C − A) | −890 | +11 | −512 | 0 |
| **B − A** | **−2,141** | **−1,311** | **−1,536** | **−1,536** |

**Every figure pass 13 quotes for itself is confirmed to the byte, on its
branch.** The ten agents' merges quote `kern_big` −606, −160, −338, −77,
−89, −406, −55, −16, −30 and −32 and `kern_small` 0, −715, −49, 0, −132,
−428, −29, −2, 0, 0, and each branch measures exactly that. The pass's own
merge quotes *kern_big −1,821, kern_small −1,348*, and its branch is exactly
that too. **As merged it is −1,815 on `kern_big`**: the merge residuals above
(+4, −3, −3 and the +6 of the merge onto `elendilon`) are what joining the
branches cost. `assoc`'s *FORMAT.DRV 4 KB → 3 KB claim* is exact (3,069
bytes).

### What has not had a pass

Pass 13 merged at `7c866c4`. Everything it was briefed with is passed:
`main`'s #249 (the `theme` branch), #251 and #265 (`desk`), #264, #252's
leftovers and `a762938` (`misc`). What landed after it, or in `main` after
#265, and no size-pass commit names:

| | `kern_big` resident | `kern_small` resident | outside the resident count |
|---|---:|---:|---|
| `DESK.DRV`'s drop with the last widget (`0c9e521`, this PR's one kernel fix) | +63 | 0 | |
| the manuals pass's bugs (`d511fdd`) | +7 | +7 | `CTRL.DRV` +56 (small +53) |
| the Sticky Apps menu on the desktop bar only (`f8f4376`) | −6 | 0 | `STIKMENU.DRV` +20 |
| the Sticky App library and its two users (`532437a`, `11cfcb0`, `d3151e0`) | 0 | 0 | `DESK.DRV` +4,267 |
| `os88ui_gring` (`37487d3`, `222cb79`) | 0 | 0 | `CTRL.DRV` +36 on both |
| a serial of its own on every disk (`d8993e7`) | 0 | 0 | `DISKOPS.DRV` +56, `FORMAT.DRV` +47, on both |
| **total** | **+64** | **+7** | |

**This is the smallest unpassed list the family has recorded**: 64 resident
bytes on `kern_big` against the last report's 865. The module side is where
the unpassed bytes are now, and almost all of it is `DESK.DRV`'s library, a
deliberate MOVE of code every Sticky App used to carry into the one module
they share.

## How close B stands to the next rung

This is what the next addition is billed against, not what this cycle cost
(`kernsize`'s own rung line in each endpoint's worktree, B with its own
four-digit build number):

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at C | 272 left | 78 left | 274 left |
| `kern_big` at B | **186 left** | 391 left | 274 left |
| `kern_small` at C | 429 left | **46 left** | 204 left |
| `kern_small` at B | **193 left** | **67 left** | 204 left |

**No rung is near on `kern_big`**: the cold rung that was the near one in the
last two reports has 391 bytes in it after pass 13, and the low rung, near
for four reports running, has 274 since `main`'s #264 took 240 bytes of
`.lowbss` out. **`kern_small`'s cold rung is the near one, 445/512 spent.**
Per CLAUDE.md's banner this decides WHEN the machine pays and never what a
change cost.

## Where the bytes are, by file

`kernsize --modules`, A → B (so both arms), every row that moved (`.text` /
`.cold` / `.bss`; `.lowbss` moved in one file):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `apps.inc` | **−71 / −1,070 / −11**, `.lowbss` −240 (`main`'s #264) | +1 / 0 / 0 |
| `vidsel.inc` | −304 / 0 / 0 | −123 / 0 / 0 |
| `font.inc` | −240 / 0 / 0 | −22 / 0 / 0 |
| `driver.inc` | +27 / −263 / +5, blob +809 | 0 / −5 / 0 |
| `fdlg.inc` | 0 / −222 / 0 | 0 / −219 / −16 |
| `instance.inc` | −189 / 0 / −1 | −59 / 0 / −2 |
| `vga12.inc` | −169 / +158 / −2 | −19 / 0 / −1 |
| `kernel.asm` | −94 / −12 / 0 | −41 / −12 / 0 |
| `disk.inc` | −67 / 0 / −2 | 0 / −98 / 0 |
| `files.inc` | −14 / −43 / −10 | −9 / −76 / −10 |
| `dock.inc` | −9 / −45 / 0 | - |
| `filecp.inc` | −40 / +26 / 0 | 0 / −28 / −44 |
| `wm.inc` | −35 / +1 / 0 | −145 / 0 / −4 |
| `mouse.inc` | +10 / 0 / 0 | **−226** / 0 / 0 |
| `diskw.inc` | 0 / +2 / 0 | 0 / −96 / 0 |
| `viddet.inc` | blob +3 only | −56 / 0 / 0 |
| `sched.inc`, `winmod.inc` (big); `sched.inc`, `icons.inc`, `blank.inc`, `menu.inc` (small) | −35 / −8 / 0 | −39 / −22 / −4 |
| `menu.inc`, `ui.inc`, `fsx.inc`, `memory.inc`, `clock.inc`, `hiber.inc`, `desk.inc`, `blank.inc`, `mod.inc` (big); `fsx.inc`, `memory.inc`, `clock.inc`, `mod.inc`, `ctrl.inc` (small) | +122 / +101 / +33, blob +3 | +6 / +49 / −4, blob +2 |
| `loader.inc` | 0 / +84 / 0 | 0 / +14 / 0 |
| `desksc.inc` (links) | 0 / +155 / 0, blob +195 | - |
| `assoc.inc` (the writer) | **+98 / +130 / 0** | - |
| `deskmod.inc` (`DESK.DRV`'s resident doors, new in #251) | 0 / +64 / 0 | - |
| `stikmenu.inc` (the Sticky Apps cell, new in #265) | 0 / +39 / 0 | - |

**The pass took the adapter switch out of resident code on both kernels**
(`vidsel.inc`, `font.inc`'s glyph, `kern_small`'s `mouse.inc` and `wm.inc`)
and put it in `CTRL.DRV`, which is the module the Control Panel already
loads to switch adapters. What grew resident is the ASSOC.DAT writer
(`assoc.inc`), links (`desksc.inc`) and the desk layer's doors.

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked) and files (as shipped),
off each point's own `os88mod.py`. The claim is the image rounded up to whole
KB.

| module | A image | C image | B image | **B−C** | B file | claim A → C → B, KB |
|---|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 12,355 | 12,748 | 13,897 | **+1,149** | 13,897 | 13 → 13 → **14** |
| `DESK.DRV` | — | 235 | 4,508 | **+4,273** | 4,236 | — → 1 → **5** |
| `DISKOPS.DRV` | 10,371 | 10,379 | 10,168 | **−211** | 9,077 | 11 → 11 → **10** |
| `DOCK.DRV` | 2,233 | 2,257 | 2,252 | **−5** | 2,054 | 3 → 3 → 3 |
| `EXTD.DRV` | 1,023 | 1,023 | 1,511 | **+488** | 1,490 | 1 → 1 → **2** |
| `FORMAT.DRV` | 1,136 | 1,136 | 3,069 | **+1,933** | 2,868 | 2 → 2 → **3** |
| `HIBER.DRV` | 5,497 | 5,497 | **6,145** | **+648** | 5,137 | 6 → 6 → **7** |
| `STIKMENU.DRV` | — | 14,758 | 14,778 | **+20** | 4,066 | — → 15 → 15 |
| `WIN95.DRV` | 1,605 | 1,628 | 1,430 | **−198** | 1,339 | 2 → 2 → 2 |
| **`kern_small`** `CTRL.DRV` | 4,406 | 4,406 | 4,979 | **+573** | 4,772 | 5 → 5 → 5 |
| **`kern_small`** `DISKOPS.DRV` | 7,184 | 7,192 | 7,232 | **+40** | 6,570 | 8 → 8 → 8 |
| **`kern_small`** `FDLG.DRV` | 1,232 | 1,232 | 1,284 | **+52** | 1,258 | 2 → 2 → 2 |
| **`kern_small`** `FILECP.DRV` | 1,770 | 1,770 | 1,855 | **+85** | 1,800 | 2 → 2 → 2 |
| **`kern_small`** `FORMAT.DRV` | 1,136 | 1,136 | 1,188 | **+52** | 1,147 | 2 → 2 → 2 |

**Most of the module growth is resident code moved OUT, which is what a
module is for**: `CTRL.DRV`'s +1,149 is pass 13 putting the adapter switch
and the glyph there (+1,057), `EXTD.DRV`'s +488 the seam cuts that are only
ever an extended desktop's, `HIBER.DRV`'s +613 the attach half of a class
driver load. The two that are new code rather than moved code are
`FORMAT.DRV`'s +1,881 (the ASSOC.DAT writer, which rides there because only
an install and a format write the file) and `DESK.DRV`'s +4,267 (the Sticky
App library: one copy in the module instead of one in every Sticky App).
**`HIBER.DRV` is 6,145 bytes, one byte over 6 KB**, so a hibernate claims 7
KB for it: see *Found on the way*.

## The loadable drivers

Built by `make` at A, C and B. The figure is the heap claim a mounted driver
costs, image + bss (byte +31 of the header, in paragraphs), with the claim
rounded up to whole KB; the file is as shipped. A driver is not measured per
commit, so the commits named are the ones the log names for its sources and
the shared includes it assembles.

| driver | A image+bss | C | **B** | **B−C** | claim A → B, KB | file A → B |
|---|---:|---:|---:|---:|---|---|
| `XMEM.DRV` | 1,014 | 1,014 | **2,041** | **+1,027** | **1 → 2** | 966 → 1,870 |
| `HDD.DRV` | 3,584 | 3,584 | **4,096** | **+512** | 4 → 4 | 3,157 → 3,614 |
| `THMPAL.DRV` (new in `main`) | - | 2,512 | **2,617** | **+105** | - → 3 | - → 2,307 |
| `RAMPAGE.DRV` | 2,381 | 2,381 | **2,469** | **+88** | 3 → 3 | 2,147 → 2,222 |
| `ETHER.DRV` | 16,963 | 16,963 | **17,006** | **+43** | 17 → 17 | 11,153 → 11,243 |
| `SOUND.DRV` | 7,240 | 7,240 | **7,283** | **+43** | 8 → 8 | 6,187 → 6,236 |
| `NET.DRV` | 4,153 | 4,153 | **4,161** | **+8** | 5 → 5 | 3,613 → 3,618 |
| `EMS.DRV`, `RAMDISK.DRV`, `USBMOUSE.DRV` | 7,373 | 7,373 | **7,373** | **0** | 1, 6, 2 | unchanged |
| `SAVER.DRV` | 10,403 | 10,403 | **10,251** | **−152** | 11 → 11 | 7,388 → 7,269 |
| `HDDTOOL.DRV` | 17,507 | 17,507 | **16,499** | **−1,008** | **18 → 17** | 13,416 → 12,614 |
| **all twelve** | 70,618 | 73,130 | **73,796** | **+666** | | |

- **`XMEM.DRV` doubles**, +1,027: `b0304a4` (*XMEM.DRV copies by loaded
  bases; a 386 decodes in place*, the Video Player's VIDEO-386 round). It is
  loaded only where there is memory above 1 MB, so no XT pays it.
- **`HDD.DRV` reaches exactly 4,096**: `06ca6c9` (*fill straight into XMS
  via HDD.DRV*), `d8993e7` (a serial of its own on every disk) and the
  ASSOC.DAT writer (`13c9752`, `eb90b2c`). Its claim stays 4 KB with nothing
  left in it.
- **`HDDTOOL.DRV` and `SAVER.DRV` shrink**: the same installer work moved
  code out of `HDDTOOL.DRV`, and `SAVER.DRV`'s bss fell 160 with
  `OS88LINE_NOEDIT` (`ea716a7`).
- **`main`'s arm moved no driver but `THMPAL.DRV`**, which #249 added.
- **`VMMOUSE.DRV`** (`kern_emu` only) was not measured.

## The final bill

**What the PR asks `main` to take, on the kernel**, with B the PR:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #252 squash (A) | 98,816 | 62,464 |
| `KERN_SIZE` at `main`'s tip (C) | 98,304 | 62,464 |
| `KERN_SIZE` at the PR (B) | **97,280** | **60,928** |
| **change against `main`** | **−1,024** (two rungs) | **−1,536** (three rungs) |
| resident bytes, against C | **−1,251** | **−1,322** |
| resident bytes, against A | −2,141 | −1,311 |
| window / blob (not resident), against C | +21 / +1,005 | +9 / +46 |
| heap a machine gets back, against `main` | **1,024** | **1,536** |
| `KERN_BUDGET` (unchanged) | 100,864 | 64,000 |
| spare of the budget | 2,560 → **3,584** (7 steps) | 1,536 → **3,072** (6 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 48,880 → **47,942**: **17,594 left** | 34,899 → **34,111**: **31,425 left** |
| guard 5, before it cannot boot on 128 KB | 23,552 → **23,552** | 59,392 → **60,416** |
| on-demand modules, against C | `CTRL.DRV` +1,149, `DESK.DRV` +4,273, `FORMAT.DRV` +1,933, `HIBER.DRV` +648, `EXTD.DRV` +488, `DISKOPS.DRV` −211, `WIN95.DRV` −198 | `CTRL.DRV` +573, `FILECP.DRV` +85, `FDLG.DRV` +52, `FORMAT.DRV` +52, `DISKOPS.DRV` +40 |
| the loadable drivers, against C | **+666** of claim, `XMEM.DRV` 1 → 2 KB and `HDDTOOL.DRV` 18 → 17 | the same drivers |

`kern_big`'s guard-5 headroom stands still while `KERN_SIZE` falls 1,024,
because the boot limit falls with it: the blob is two sectors longer
(autostart's walk, and pass 13's boot-only moves), and guard 5 counts the
blob's room.

**In one line each:**

- **`main`** took **890 resident bytes and a rung out of `kern_big`**, by
  moving Timer and Bounce out into packages, and spent 757 of the 1,647
  that bought on the desk layer, the EGA themes and the Sticky Apps menu;
  `kern_small` +11. Pass 13 has since passed all of it.
- **We** took **1,815** resident bytes out of `kern_big` and **1,348** out of
  `kern_small` in kernel size pass 13, most of it the adapter switch and the
  glyphs leaving for `CTRL.DRV`, and spent **+564 of it on `kern_big`**: the
  ASSOC.DAT writer, links and autostart, our half of Sticky Apps, the packed
  part. Only **64 resident bytes** have never had a pass.
- **Together**, the PR is **two rungs smaller on `kern_big` and three on
  `kern_small`** than `main`, and three on both against the #252 squash.

## Found on the way

- **`HIBER.DRV` is one byte over 6 KB.** 6,145 bytes, so a hibernate claims
  7 KB for a module that would fit 6. `ae46a3d` (*the attach half of a driver
  load leaves resident .cold*) moved 613 bytes into it, which was the right
  move for the resident count. Per CLAUDE.md the rung is not a design input,
  but this one is a whole KB of heap while the machine hibernates, for one
  byte, and a pass on the image would buy it back. `SAVER.DRV`'s 10,251 is
  the same shape, 11 bytes past 10 KB.
- **`d5d2d21` does not assemble on `kern_big`**: the blob overflowed
  `BOOT2_SECS` and the next commit raised it. A bisect that lands there
  reads a build failure that is not the bug it is looking for.
- **`kernsize --modules` listed four files as *(undescribed)***:
  `deskmod.inc` and `stikmenu.inc` (`main`'s #251 and #265), `ascw.inc` (the
  ASSOC.DAT writer, all module image) and `kbddiag.inc` (a knob, 0 in every
  shipped kernel). Each was added with no row in KERNEL-MEMORY.md's table,
  which is where the description lives (the last report found `winmod.inc`
  the same way). **Written in the same commit as this file.**
- **`docs/KERNEL-MEMORY.md`'s baselines did not describe B.** `make` at B
  printed `sum +64` on `kern_big` and on `kern_emu` and `sum +8` on
  `kern_small`: the baselines were blessed with pass 13, before the three
  commits after it. **Re-blessed at B in the same commit as this file.**
- **No quoted figure was found wrong.** Every pass-13 branch's own numbers,
  and `assoc`'s FORMAT.DRV claim, agree with their commits to the byte. The
  pass's headline −1,821 is its branch's; as merged it is −1,815.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`. `.lowbss`
moved in one commit (`main`'s #264, −240) and `.vgabuf` in none. *Window* is
the overlay loaded onto the FAT window and *blob* the boot blob; neither is
resident. `main`'s rows come first; the rest are our round in topological
order, and with the merge residuals they sum to B − C exactly, as do the
`KERN_SIZE` columns this time. The module column is unpacked image bytes.

| commit | subject | big `.text` | big `.bss` | big `.cold` | **big resident** | big `KERN_SIZE` | small `.cold` | **small resident** | small `KERN_SIZE` | window / blob | modules (image) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| `3987ad0` | **main:** Add lightweight EGA Color themes with persistent title stripes (#249) | +195 | +8 | 0 | **+203** | 0 | 0 | **+9** | 0 | big blob +34 | CTRL +212, WIN95 +22 |
| `0a66250` | **main:** DIAs: desktop widgets, the desk layer (DESK.DRV) and the Desk Calendar (SPEC.md 110, 11.103) (#... | −22 | +16 | +433 | **+427** | +512 | +32 | **+50** | 0 |  | CTRL +181, DESK +235, DOCK +24, WIN95 +1 |
| `a123653` | **main:** Timer and Bounce are packages (SPEC.md 14.8, 111, 112) (#264) | −326 | −11 | −1,070 | **−1,647** | −1,024 | 0 | **−56** | 0 |  |  |
| `c22a3d6` | **main:** Locator's Sticky Apps menu, and the Sticky App card (SPEC.md 110.12) (#265) | +72 | +8 | +47 | **+127** | 0 | +8 | **+8** | 0 |  | DISKOPS +8, STIKMENU +14,758, small DISKOPS +8 |
| `84a3038` | elendilon since the #252 squash, squashed onto main (#264) | +60 | −10 | −75 | **−25** | 0 | −25 | **−20** | 0 | big window +9, big blob +14, small blob +23 | CTRL −246, DISKOPS −272, EXTD +8, HIBER +35, WIN95 −185, small CTRL −23, small DISKOPS −16 |
| `d5d2d21` | Links (*.LNK) and autostart from \AUTOSTRT, Shift to skip (SPEC.md 26.10) | +11 | +2 | +167 | **+180** | 0 | 0 | **0** | 0 | big blob +33 | (does not assemble alone: see the text) |
| `2bff6f7` | Autostart's walk moves into an 11-sector blob; system disks ship \AUTOSTRT | −9 | 0 | −1 | **−10** | 0 | 0 | **0** | 0 | big blob +191 | with `d5d2d21`: CTRL +212 |
| `2bd968a` | Theme page: Customize clickable, Windows row and corner Screen Saver back (SPEC.md 76.14.10) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL +67 |
| `13c9752` | ASSOC.DAT: write back what the machine learned (SPEC.md 54.7.6) | +7 | +4 | +164 | **+175** | +512 | 0 | **+6** | 0 |  | FORMAT +1,975 |
| `eb90b2c` | assoc: installer holds the writer; caps raised to what fits 5KB | +562 | 0 | +34 | **+596** | +512 | 0 | **0** | 0 |  | FORMAT +2 |
| `52fb582` | assoc: kernel slot tables 16/32, not 32/64 | −448 | 0 | −1 | **−449** | −512 | 0 | **0** | 0 |  |  |
| `a762938` | kernel: a parted package may ship its image packed (+33 .cold, both kernels) | 0 | 0 | +33 | **+33** | 0 | +33 | **+33** | 0 |  |  |
| `30538ce` | Shut Down: let Ctrl-Alt-Del warm-reset from the terminal screen | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL +12, small CTRL +12 |
| `2f676da` | ascw.inc: the flush's scratch off BP, its resident calls by inline word (kernel size pass 13) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | FORMAT −92 |
| `8eab430` | assoc: asc_flush one body, locate's learn test off [bx+di], no ico_n clear on claim (kernel siz... | 0 | 0 | −24 | **−24** | 0 | 0 | **0** | 0 |  |  |
| `4868925` | assoc: the capture as 17-byte records; asc_learn banks AX and ES only (kernel size pass 13) | 0 | 0 | −8 | **−8** | 0 | 0 | **0** | 0 |  | FORMAT −4 |
| `96a5f12` | Links: the walk tells its record by SP, and the Alt-drop shares the desk drop's tail (kernel si... | −2 | −2 | −26 | **−30** | 0 | 0 | **0** | 0 | big blob −25 | CTRL −31 |
| `9ff6043` | CTRL.DRV: the link's name before its record, one kind clamp, a cmpsb compare (kernel size pass ... | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL −7 |
| `a1c9ccb` | desk: wm_top, OSAPI_CLK_GET, desk_live, db_key/db_hit, stik_boot, DOCK.DRV's tile tests (kernel... | −6 | 0 | −9 | **−15** | 0 | −2 | **−2** | 0 |  | DOCK −10 |
| `6d99307` | desk: fdlg_top_x stands before DESK.DRV's dkd_key asks (kernel size pass 13) | 0 | 0 | −1 | **−1** | 0 | 0 | **0** | 0 |  | DESK +6 |
| `6382b82` | kernel: misc concept, batch 1 - the leftovers of #264, #252 and a7629386 (kernel size pass 13) | −132 | 0 | +80 | **−52** | 0 | −6 | **−26** | 0 |  |  |
| `836dc20` | kernel: the deferred launch is a Disk window's, and a byte (kernel size pass 13) | −2 | −1 | 0 | **−3** | 0 | 0 | **−3** | 0 | big window −3, small window −3 |  |
| `e9d5fbb` | wkgui: dead and constant arms - wm_clip_rect off shortcut kernels, font_str to BOOTPROF, kern_s... | −76 | 0 | 0 | **−76** | 0 | −23 | **−389** | −512 |  |  |
| `a1b3027` | wkgui: kern_small's VGA row, MDA text table, depth test, the 8s and the zoom byte (kernel size ... | 0 | 0 | 0 | **0** | 0 | 0 | **−43** | 0 |  |  |
| `bef5c87` | wkgui: the two seam cuts are EXTD.DRV's (kernel size pass 13) | −338 | +8 | 0 | **−330** | −512 | 0 | **0** | 0 |  | EXTD +367 |
| `8ebc917` | wkgui: ss_pub off kern_small, EXTD's landing latch in its image, hot-path parity pads (kernel s... | 0 | 0 | 0 | **0** | 0 | +1 | **+4** | 0 |  | EXTD +8 |
| `ecffef0` | wkfs batch 1: FILECP's doors and op scratch into its image on kern_small, dskw_fdir, shared dis... | −16 | 0 | −30 | **−46** | 0 | −60 | **−104** | 0 |  | small FILECP +85 |
| `763dde7` | wkfs batch 2: dsk_free_slow written at its one call, the Disk window's thumb-scroll tail shared... | 0 | 0 | −32 | **−32** | 0 | −18 | **−18** | 0 |  |  |
| `828cafe` | wkfs batch 3: fm_numbuf shares fm_name (kernel size pass 13) | 0 | −10 | 0 | **−10** | 0 | 0 | **−10** | 0 |  |  |
| `7aaa43d` | wkfs: assoc_tryvol's APPS load as a byte, which puts dsk_xfer, dsk_runcap and the FAT walk back... | 0 | 0 | −1 | **−1** | 0 | 0 | **0** | 0 |  |  |
| `033b138` | Merge ksp13-wkfs: kernel size pass 13, the file system and the Disk window (kern_big -89, kern_... (merge residual) | 0 | 0 | −3 | **−3** | 0 | 0 | **0** | 0 |  |  |
| `9e6622e` | theme: thm_set asks the adapter once, OSAPI_THM_SET jumps to its cold body, THMPAL.DRV's name m... | −18 | 0 | −43 | **−61** | 0 | 0 | **0** | 0 |  | CTRL +10 |
| `229c76d` | theme: thm_set and api_thm_set_x reach .text through cw_mem_disp, cw_dock_force and cw_wm_su_dr... | −8 | 0 | +4 | **−4** | 0 | 0 | **0** | 0 |  |  |
| `d20e6de` | theme: wm_tpen_bg/wm_tpen_ink sit beside thm_pen and read the inactive pair themselves, thm_ibg... | −8 | 0 | 0 | **−8** | 0 | 0 | **0** | 0 |  |  |
| `51fb7e7` | theme: wn_buttons picks both of its pens on one test of the style bit (kernel size pass 13) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | WIN95 −13 |
| `dcba9c1` | theme: CTRL.DRV's Theme page toasts its refusal through sc_m_say and banks through kentm_bp (ke... | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL −17 |
| `6699258` | theme: the title pen pair reaches the active doors with a short je, the xor head back to the ba... | −4 | 0 | 0 | **−4** | 0 | 0 | **0** | 0 |  |  |
| `3ba0606` | Merge ksp13-theme: kernel size pass 13, EGA Color themes, Customize and WIN95 (kern_big -77, WI... (merge residual) | 0 | 0 | −3 | **−3** | 0 | 0 | **0** | 0 |  |  |
| `ae46a3d` | driver.inc: the attach half of a driver load leaves resident .cold (kernel size pass 13) | 0 | 0 | −266 | **−266** | −512 | 0 | **0** | 0 | big blob +277 | CTRL +277, HIBER +613 |
| `67bb735` | Four resident bodies out to their images, five shims to their cells (kernel size pass 13) | −54 | 0 | −6 | **−60** | 0 | −3 | **−39** | 0 | big blob +6 | CTRL +36, small CTRL +12 |
| `6cb3eb6` | kernel.asm: three more cw_ shims are their API cells (kernel size pass 13) | −12 | 0 | 0 | **−12** | 0 | 0 | **0** | 0 |  |  |
| `47fd403` | kern_small: mod_init written out in the blob (kernel size pass 13) | 0 | 0 | 0 | **0** | 0 | −10 | **−10** | 0 | small blob +15 |  |
| `a7f9ac2` | Merge ksp13-wkcore: kernel size pass 13, the core (kern_big -338, kern_small -49; the attach ha... (merge residual) | +4 | 0 | 0 | **+4** | 0 | 0 | **+4** | 0 |  |  |
| `ef93481` | xfile batch 1: the cross-file list's small items - kern_small's dead .cold and .bss, the system... | −21 | 0 | −16 | **−37** | 0 | −193 | **−224** | 0 |  | CTRL +13 |
| `15bfdf0` | xfile batch 2: the module-only far shims leave resident - CTRL.DRV's CPXK/CPXC hop, FDLG.DRV's ... | −71 | 0 | −54 | **−125** | 0 | −20 | **−83** | −512 | big window +15, big blob +20, small window +12, small blob +8 | CTRL +66, DISKOPS +5, DOCK +5, FORMAT +5, small CTRL +54, small FDLG +14, small FORMAT +5 |
| `a231bc0` | xfile batch 3: kern_small's check-box and radio glyph are CTRL.DRV's - os88ui.inc assembles the... | 0 | 0 | 0 | **0** | 0 | −207 | **−207** | −512 |  | small CTRL +229 |
| `f0be8d4` | xfile batch 4: FDLG.DRV's dialog state is the image's bss on kern_small (.moddb, zeroed by mod_... | 0 | 0 | 0 | **0** | 0 | 0 | **−16** | 0 |  | small FDLG +38 |
| `417e585` | xfile batch 5: kern_small's adapter switch is CTRL.DRV's - vid_switch and the five routines onl... | 0 | 0 | 0 | **0** | 0 | 0 | **−187** | 0 |  | small CTRL +200 |
| `17cd36c` | Parity pads: every hot routine back at an even shift against aedadd25 (kernel size pass 13) | +2 | 0 | 0 | **+2** | 0 | +1 | **+2** | 0 |  |  |
| `c74fb1e` | wm_su_rect answers a FULLSCREEN window's frame too: OSAPI_WM_DAMAGE's whole-content rect no lon... | −10 | 0 | 0 | **−10** | 0 | 0 | **+3** | 0 |  |  |
| `0efc715` | dispsw: kern_big's adapter switch is CTRL.DRV's - vid_switch, vid_disp_init, wm_refit and the D... | −254 | 0 | −45 | **−299** | −512 | 0 | **0** | 0 | big blob +384 | CTRL +331 |
| `75ff7e7` | dispsw: kern_big's check-box and radio glyph are CTRL.DRV's too - OS88UI_KGLYPH on both kernels... | 0 | 0 | −207 | **−207** | 0 | 0 | **0** | 0 |  | CTRL +229 |
| `cb4caaa` | dispsw: vid_equip and vid_desk_union are written once and expanded into CTRL.DRV, the boot blob... | −100 | 0 | 0 | **−100** | 0 | 0 | **0** | 0 | big blob +105 | CTRL +105, EXTD +105 |
| `7c866c4` | Merge kernel-size-13 into elendilon: kernel size pass 13 (kern_big -1,821 resident, kern_small ... (merge residual) | +2 | 0 | +4 | **+6** | 0 | 0 | **0** | 0 |  |  |
| `d511fdd` | Manual bugs: Audio, Video, MIDIRack radio, greyed captions, CP, Disk header, toast, REDLINE | +8 | 0 | −1 | **+7** | 0 | −1 | **+7** | 0 |  | CTRL +56, small CTRL +53 |
| `37487d3` | os88ui_gring: the round ring in six runs, not eight | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL −21, small CTRL −21 |
| `222cb79` | os88ui_gring: four runs and ONE gfx_points for the corners | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL +57, small CTRL +57 |
| `532437a` | The Sticky App library: one copy, in DESK.DRV (SPEC.md 110.15) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | DESK +4,237 |
| `f8f4376` | Sticky Apps menu: desktop bar only, and the freeze ends with the scan | −6 | 0 | 0 | **−6** | 0 | 0 | **0** | 0 |  | STIKMENU +20 |
| `11cfcb0` | Sticky Note and the Desk Clock on the shared library (SPEC.md 110.15) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | DESK +16 |
| `d3151e0` | Sticky Apps: quiet home stand, NOTES banked (SPEC.md 110.5.1, 110.13.2) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | DESK +14 |
| `d8993e7` | Every disk the machine makes gets a serial of its own (SPEC.md 18.8.2.1) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | DISKOPS +56, FORMAT +47, small DISKOPS +56, small FORMAT +47 |
| `0c9e521` | DESK.DRV: drop the image when the last widget goes (SPEC.md 11.103.10) | +10 | 0 | +53 | **+63** | 0 | 0 | **0** | 0 |  |  |
