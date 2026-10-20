# Kernel bytes since the #241 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-10-14 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13.16. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Each of the **844
commits** below was measured in a clean worktree of its own: that commit's
own Makefile was asked for its pass-1 `nasm` line and its `os88mod.py` line
(`make -n --always-make` into a throwaway `BUILD`), the pass-1 kernel was
assembled with that line, that commit's `tools/os88mod.py` cut the modules,
and that commit's `kernsize` measured the sections, on `kern_big` and on
`kern_small`. A commit whose every kernel input - `kernel/`, `boot/`, the
Makefile, the top-level `.inc` files of `apps/` and `drivers/`, and the five
tools in the chain - is blob-for-blob a tree already measured takes that
tree's figures (257 distinct trees were assembled; the key is the blob list,
so identical inputs are identical bytes by construction). The loadable
drivers were built by `make` at the four endpoints only. It is true of the
commits it names and of no other tree.

It is the ninth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-01.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-03.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-08.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `26db8ec6` | **the last squash with `main`**: *Elendilon -> Main (System-Side Size Pass: Every Driver and Module Smaller, Video Player Sound Ahead and 16 Color Page Flipping, Fixed Disks Read to the Cylinder's End, Bugfixes)* (#241), 2026-10-10 |
| **C** | `dde6b19b` | **upstream** `main` (`jggonz/os8088`) at its tip, **+7 commits** over A: #242-#248. The fork's own `origin/main` is the same commit |
| **E** | `e51f21ba` | `elendilon` at its tip, 2026-10-14: **+819 commits** over `9d39bf2d`, 639 that are not merges and 180 merges |
| **B** | `758eb807` | `elendilon-next`: C with E's work applied as ONE commit, a three-way merge on base `9d39bf2d`. It is cut from C, so there is no trial merge to build: **B is the PR**. No review fixes have landed on it yet |

**Video Player fixes still to come** (`video-palette`, to be cherry-picked
onto `elendilon-next`) are expected to touch no kernel source. Any that does
moves B, and this report is then out of date by exactly that commit.

**Why E's base is `9d39bf2d` and not A.** #241's head was `elendilon-pr`
(`50d1a2af`): `elendilon` at `9d39bf2d` plus twelve commits made on the PR
itself, two CLAUDE.md swaps and ten bugfixes pushed individually to it. Its
tree IS A's (`git diff` is empty), and it **measures identical to A** on both
kernels, in every section and module. So A is `9d39bf2d` plus those twelve,
and `elendilon`'s round since the squash is exactly `9d39bf2d..e51f21ba`.
**The twelve move no kernel byte**: the ten fixes are in `HDD.DRV`,
`RAMDISK.DRV`, `RAMPAGE.DRV`, the Video Player, `os88ui.inc` (outside the
part the kernel assembles) and MartyPC's patches.

**A chains to the previous report exactly.** That report's B (`5b0130fc`)
measures **identical to `9d39bf2d`** on both kernels, in every section and
every module image.

**The build number is held at 214 for every tree.** `BUILD_STR` is the commit
count (SPEC.md 14.2): 206 at A, 213 at C, 214 at B - but `elendilon` runs
from 220 to **1039** across the round, so its own numbers cross into four
digits, and E built with its own number is **+1 byte of `.text` on both
kernels** (43,580 against 43,579 on `kern_big`, 31,843 against 31,842 on
`kern_small`, checked). A digit is not a cost anybody spent, so every tree is
assembled with the same three-digit number; the PR, B, is 214 in fact.

**`associco.inc` is byte-identical at A, C, E and B** (each built from its
own packages: md5 `c4b3e546060c...`, the same copy the previous report used),
and it is the one generated include a package commit could move a kernel byte
through, so every tree was assembled against that one copy.

**The sections were renamed inside the round** (SPEC.md 2.9.14, 2.9.14.1):
the boot overlay `.ovl` joined `.boot2` (`170394db`), and the FAT-window
overlay `.ovlw` then took the name `.ovl` (`8c950871`). Both commits move
**zero bytes** (`kern_big`'s blob 2,265 + 2,486 = 4,751 exactly). The tables
below therefore carry two columns that mean the same thing at every point:
**window**, the overlay loaded onto the FAT window (old `.ovlw`, new `.ovl`),
and **blob**, the boot blob (old `.boot2` + `.ovl`, new `.boot2`). Neither is
resident: `mem_unblob` gives both back once the desktop is up.

## The budgets moved, and that is not a size move

`bc5e7543` (*Kernel budgets reset: kern_big resides in 100KB, kern_small in
64KB, both boot on 128KB*) is inside the round. So every *spare* figure below
changes for two reasons at once, and the second is a decision rather than a
measurement:

| | A and C | E and B |
|---|---:|---:|
| `kern_big` `KERN_BUDGET` | 129,536 | **100,864** |
| `kern_small` `KERN_BUDGET` | 107,520 | **64,000** |
| `KERN_GRACE` (branch work may run this far over) | - | 2,048 |
| `kern_big` boots on (`MIN_RAM_KB`, guard 5) | 196 KB | **128 KB** |
| `kern_small` boots on | 128 KB | 128 KB |

## Headline: `kern_big`, the shipped default

| section | A base | C main | B, the PR | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 44,308 | 44,340 | 43,611 | +32 | −697 | −729 |
| `.bss` | 5,133 | 5,135 | 5,214 | +2 | +81 | +79 |
| `.cold` | 37,921 | 37,933 | 38,380 | +12 | +459 | +447 |
| `.lowbss` | 5,598 | 5,598 | 5,598 | 0 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 | 0 |
| **resident** | 93,296 | 93,342 | **93,139** | **+46** | **−157** | **−203** |
| window | 5,011 | 5,011 | 5,105 | 0 | +94 | +94 |
| blob | 4,735 | 4,755 | 4,857 | +20 | +122 | +102 |
| **`KERN_SIZE`** | 99,328 | 99,328 | **98,816** | **0** | **−512** | **−512** |
| spare of the budget | 30,208 | 30,208 | 2,048 | | | |

**−203 resident against `main`, and one rung back.** `KERN_SIZE` falls
**99,328 → 98,816**, so every `kern_big` machine gets **512 bytes of heap**
back. This round's size-pass commits took 3,214 resident bytes out (passes 10
and 11, and each feature's own pass rows) and nearly two-thirds of that was
spent again on features (below). The spare falls
because the budget was cut to 100KB, not because the kernel grew.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | C main | B, the PR | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 32,904 | 32,927 | 31,865 | +23 | −1,039 | −1,062 |
| `.bss` | 3,104 | 3,104 | 3,063 | 0 | −41 | −41 |
| `.cold` | 23,950 | 23,967 | 22,439 | +17 | −1,511 | −1,528 |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 | 0 |
| **resident** | 62,826 | 62,866 | **60,235** | **+40** | **−2,591** | **−2,631** |
| window | 1,480 | 1,480 | 1,480 | 0 | 0 | 0 |
| blob | 4,351 | 4,351 | 4,341 | 0 | −10 | −10 |
| **`KERN_SIZE`** | 65,024 | 65,024 | **62,464** | **0** | **−2,560** | **−2,560** |
| spare of the budget | 42,496 | 42,496 | 1,536 | | | |

**−2,631 resident against `main` and five rungs back.** `KERN_SIZE` is
**62,464**; the floor machine gets **2,560 bytes of heap** back. Almost all
the features this round are `kern_big`'s, so the passes reach the floor
machine nearly whole, and the kern_small diet (below) adds 1,153 of its own.

## The attribution is exact

Every commit was measured against its first parent, on both kernels, in two
ranges: `main`'s arm (A..C) and `elendilon`'s round (`9d39bf2d`..E). A merge
M gets a figure too, its RESIDUAL: what M moved against its first parent,
less what its second parent moved against the merge base. In both ranges the
commits' figures and the merges' residuals **sum to the range's endpoints
exactly, in every section, the window, the blob and every module image, on
both kernels** (checked, not assumed).

**169 of the 180 merges have a residual of 0 in every field. The other
eleven move `KERN_SIZE` only, with no byte of their own**: each joins two
sides that crossed rungs from the same base, and **rungs do not add**, as
every report in this family has found. The round's commits move `kern_big`'s
`KERN_SIZE` by −2,560 and the round moved −512; `kern_small`'s commits
−1,536 and the round −2,560.

**108 of the 639 non-merge commits move a byte**: 105 a resident one, 3 a
module image, the window or the blob only. The other 531 - the apps size
pass, the Video Player round, the PiXEL, Weave and game work among them -
measure 0 in every section and module on both kernels.

**The squash itself has a residual of zero**: B − C equals E − `9d39bf2d` in
every section, the window, the blob and every module image, on both kernels.
The conflicts resolved by hand at the squash (the `SYSTEM.CFG` key table, the
V88 flags, the NE2000 probe, `SOUND.DRV`'s READY) cost no kernel byte beyond
what each side had already spent. Unlike the last round, there is nothing
here to recover.

## `main`'s arm: +46 resident on `kern_big`, +40 on `kern_small`

| commit | what it touched | `kern_big` resident | `kern_small` resident | other |
|---|---|---:|---:|---|
| #244 `c193155` *Packages know which os8088 they are for: `LD_EVER`, the ABI lock* | `loader.inc` | **+40** (`.text` +23, `.cold` +17) | **+40** | |
| #248 `dde6b19` *Port pins: the Drivers page's Ports view* | `driver.inc`, `ctrl.inc` | **+6** (`.text` +9, `.bss` +2, `.cold` −5) | 0 | blob **+20**, `CTRL.DRV` **+686** |
| #242, #243, #245, #246, #247 | the release skill, The Wire's v2 archives, the live demo's sound, the NIC/MPU probes, MIDIRack's live songs | 0 | 0 | |

#246's probe changes and #248's port pin are in the DRIVERS: `SOUND.DRV`
+93 of claim and `ETHER.DRV` +49 (below). Nothing in `main`'s arm has had a
size pass.

## Our arm, by concept

`elendilon`'s round (`9d39bf2d`..E), per concept, the sum of its commits.
Each feature is listed WITH its own size-pass rows, so its figure is what it
costs now. Every row is exact, and the rows plus the merge residuals sum to
the round.

| concept | commits | **`kern_big` resident** | **`kern_small` resident** | window / blob, modules |
|---|---|---:|---:|---|
| **Kernel size pass 10** - eleven agents by file (files, shell, new, hw, input, covox, wm, gfx, core, disk, cross-file) and the `kent_bp`/`kent_di` prologue ladder | 56 | **−2,177** (`.text` −1,109, `.cold` −1,040, `.bss` −28) | **−1,641** | big window −10, small blob −10; `CTRL.DRV` −150 (small −131), small `FDLG` −9, `FILECP` −4 |
| **Kernel size pass 11**, its shared rows: `WSEQF_KEEP`/`CKPT` (the STOR door's pass), short error jumps, `gfx_blit1`'s frame | `b1b35d2` `0cc9a99` `4aff93f` | **−67** | **−22** | `DOCK.DRV` −13 |
| **The kern_small diet** (pass 11): the redirector and driver-volume arms that are refusals there, fenced cells, no relocation proc for a heap that never moves, the re-home not assembled | `b2c40be` `e2c478d` `2e8417f` `d1329190` `a5f3ec4` `99bd94e` | 0 | **−1,153** | small `FILECP` −166, `CTRL` −84, `FDLG` −14, `CLONE` −12 |
| **Disk window multi-select** (SPEC.md 22.27), built (+1,558) and passed three times (−140, −621, −79) | 7 | **+718** (`.cold` +660, `.bss` +45, `.text` +13) | −11 | |
| **Mount a `.IMG` as a drive**, the Disk window's menus re-cut, the diskette icon packed | `c356413` `cc8d977` `d69e19b` `f84001b` `066c054` | **+355** | −28 | `DISKOPS.DRV` +1,078 |
| **Windows theme**: `WIN95.DRV` and the fourth theme (SPEC.md 76.14) | `c4b43d0` `62b7505` | **+232** (`.text` +141, `.cold` +83, `.bss` +8) | 0 | `WIN95.DRV` +1,565, `CTRL.DRV` +77 |
| **Two file classes**: the RAM disk and the cable at once | `5ebea5a` | **+103** | 0 | `HIBER.DRV` +4 |
| **FTPD STOR commits every 256 KB** on a fixed disk (its pass is in pass 11's shared rows) | `4f76933` | **+108** | 0 | |
| **EMS.DRV** (SPEC.md 107), with its pass-11 row | `8e1d317` `d45ba1d` | **+88** | 0 | blob +1, `CTRL.DRV` +1 |
| **`dskw_alloc`**: a FAT16 window by `repne scasw`, with its pass | `cd0cfe0` `6de33d0` | **+72** | **+72** | |
| **`wm_clip_rows`** grows the winner's rows (SPEC.md 11.3.4.3), with its pass | `55e5cc9` `82fd9ed` | **+64** | −8 | |
| **Seek on any cluster size**: `READ_AT` in whole sectors | `01f38d6` | **+56** | 0 | |
| **`OSAPI_VOL_TAKE`**, then folded into `OSAPI_VOL_ADD`'s DX (SPEC.md 52.1.1) | `737d08f` `e9fd2b6` | **+54** | **+66** | |
| **`OSAPI_PKG_REHOME`'s keep form** (SPEC.md 20.12.10.5) | `c04ffc4` | **+41** | 0 | |
| **PicoMEM**: `SOUND.DRV`'s tier, USB mouse, B: from the card, warm reset; `int 33h` follows a moved image | `a98c4d2` `9a536e1` `e14c391` | **+39** | **+35** | window +74 |
| **Force Color** (SPEC.md 39.28) | `c461c5b` | **+29** | 0 | blob +15, `CTRL.DRV` +238 |
| **`OSAPI_FERR_TEXT`** | `4094670` | **+25** | **+25** | |
| **`OSAPI_ICON_DRAW` takes `ICON_SOLID`** | `ac7b5b1` | **+15** | 0 | |
| **The `DA` key**: an unanswered driver row is the machine's (SPEC.md 51.5.4) | `edd0efb` | **+2** | 0 | blob +55, `CTRL.DRV` +25 |
| **`HDD.DRV` loads itself** when the BIOS knows a disk | `8cead00` | 0 | 0 | window +30, blob +31, `CTRL.DRV` +19 |
| **`CLONE.DRV` renamed `DISKOPS.DRV`** | `a32dfc3` | **+2** | **+2** | the module, renamed |
| **Eight fixes**: the chooser's reap, `fm_choose`'s repaint, `drv_svc_call_x`'s far return, SI across `W_ONCLICK`, `sw_col`'s one store, a failed mount, the timer re-check, one bar test a run | `eb9407a` `2da1ad0` `77622e8` `e061a2f` `2b20cca` `78298be` `288304a` `339ed45` | **+38** | **+32** | small `FDLG` −1 |
| **the round** | | **−203** | **−2,631** | big window +94, blob +102; small blob −10 |

### The arm, whole

| | `kern_big` resident | `kern_small` resident | `kern_big` `KERN_SIZE` | `kern_small` `KERN_SIZE` |
|---|---:|---:|---:|---:|
| the size passes (10, 11 and the kern_small diet) | −2,244 | −2,816 | | |
| the features and fixes | +2,041 | +185 | | |
| **`elendilon`'s round** | **−203** | **−2,631** | −512 | −2,560 |
| the squash's residual | 0 | 0 | 0 | 0 |
| **our net (B − C)** | **−203** | **−2,631** | **−512** | **−2,560** |
| `main`'s arm (C − A) | +46 | +40 | 0 | 0 |
| **B − A** | **−157** | **−2,591** | **−512** | **−2,560** |

**Every figure a merge commit quotes for itself is confirmed to the byte**:
pass 10 (*kern_big −2,176 resident, KERN_SIZE 99,328 → 97,280*), pass 11
(*kern_big −276, kern_small −1,128, both KERN_SIZE −512*) and each of pass
11's agents (cross-file −21/−564, multi-select −79/−11 with the feature
797 → 755, PicoMEM −5/−5, the VOL_TAKE fold −55/−440, the caption fix
−28/−8, disk write −88/−57) measure exactly what they say. So do multi-select
size pass 2's *1,418 → 797* and `EMS.DRV`'s *1,208 → 702*.

**The 2 bytes the last report said the #230 squash lost** in `fdlg_grab`'s
tail are back: `055c583` (*a grab ends in cmc*) is −11 of `.cold` in `fdlg`
and is where they went.

### What has not had a pass

Every feature below landed after pass 11 merged (`0371d27`), or in `main`,
and no size-pass commit names it:

| | `kern_big` resident | `kern_small` resident | outside the resident count |
|---|---:|---:|---|
| Mount a `.IMG` (its icon was packed, its code not passed) | +355 | −28 | `DISKOPS.DRV` +1,078 |
| Windows theme | +232 | 0 | `WIN95.DRV` 1,565, `CTRL.DRV` +77 |
| Two file classes | +103 | 0 | |
| `OSAPI_PKG_REHOME`'s keep form | +41 | 0 | |
| Force Color | +29 | 0 | blob +15, `CTRL.DRV` +238 |
| `OSAPI_FERR_TEXT` | +25 | +25 | |
| `ICON_SOLID` | +15 | 0 | |
| a failed mount is not a place to go back to (`78298be`) | +15 | +15 | |
| the `DA` key, `HDD.DRV` autoload, the rename | +4 | +2 | blob +86, window +30, `CTRL.DRV` +44 |
| **`main`**: #244 `LD_EVER` and #248 port pins | +46 | +40 | blob +20, `CTRL.DRV` +686 |
| **total** | **+865** | **+54** | |

Against the 50% target passes have been briefed with, that is **~430 bytes
owed on `kern_big`** and ~25 on `kern_small`, plus ~520 of `CTRL.DRV`
(+1,045 unpassed, most of it #248's Ports view; +896 net since A, pass 10
having taken 150 out) and ~540 of `DISKOPS.DRV`.

## How close B stands to the next rung

This is what the next addition is billed against, not what this cycle cost
(`kernsize`'s own rung line at each point):

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at C | 189 left | 467 left | **34 left** |
| `kern_big` at B | 327 left | **20 left** | **34 left** |
| `kern_small` at C | 321 left | 97 left | 204 left |
| `kern_small` at B | 400 left | 89 left | 204 left |

**`kern_big`'s cold rung is the near one now, 492/512 spent**: the next 21
bytes of `.cold` cost a whole rung, and with `KERN_BUDGET` cut to 100,864
that rung is one of the last four. **Its low rung is near for the fourth
report running**, 478/512. Per CLAUDE.md's banner this decides WHEN the
machine pays and never what a change cost - but with 2,048 bytes of spare
against the new budget, the next cold-rung crossing is a quarter of what is
left.

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved (`.text` / `.cold` /
`.bss`; `.lowbss` moved in no file):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `wm.inc` | **−198** / 0 / 0 | **−247** / 0 / 0 |
| `menu.inc` | −152 / −11 / −25 | −155 / −11 / −25 |
| `ui.inc` | −108 / 0 / 0 | −159 / 0 / 0 |
| `vga12.inc` | −102 / +49 / 0 | −49 / −2 / 0 |
| `mouse.inc` | −89 / 0 / 0 | −84 / 0 / 0 |
| `sched.inc` | −65 / 0 / 0 | −5 / 0 / 0 |
| `instance.inc` | −44 / −5 / 0 | −38 / −5 / 0 |
| `font.inc` | −38 / 0 / 0 | −14 / 0 / 0 |
| `fsx.inc` | −36 / 0 / 0 | −33 / 0 / 0 |
| `icons.inc` | −32 / 0 / 0 | −46 / 0 / 0 |
| `snd.inc` | −31 / 0 / −2 | −44 / 0 / −2 |
| `viddet.inc` | −21 / 0 / 0 | −20 / 0 / 0 |
| `kernel.asm` | −14 / 0 / 0 | −87 / 0 / 0 |
| `vidsel.inc`, `clip.inc`, `toast.inc`, `softgfx.inc`, `events.inc`, `dock.inc`, `clock.inc` | −42 / 0 / 0 | −29 / 0 / 0 (no `vidsel` change there) |
| `desk.inc` | 0 / −94 / 0 | 0 / −12 / 0 |
| `diskw.inc` | 0 / −82 / +2 | 0 / −297 / 0 |
| `memory.inc` | +57 / −109 / 0 | 0 / −113 / 0 |
| `fdlg.inc` | 0 / −30 / 0 | 0 / −16 / 0 |
| `assoc.inc`, `apps.inc` | 0 / −17 / 0 | - |
| `ctrl.inc` | +1 / −8 / 0 | 0 / −28 / 0 |
| `hiber.inc` | +2 / −10 / +8 | - |
| `loader.inc` | 0 / +41 / +2 | 0 / −81 / −4 |
| `desksc.inc` | +2 / +63 / −1 | - |
| `filecp.inc` | 0 / +133 / +4 | 0 / −21 / 0 |
| `driver.inc` | +53 / +36 / +49 | −55 / −4 / 0 |
| `disk.inc` | +24 / +196 / +3 | −22 / **−658** / −6 |
| `files.inc` | +79 / **+275** / +41 | +46 / −263 / −4 |
| `mod.inc`, `xmem.inc` | +21 / 0 / 0 | +2 / 0 / 0 |
| `clone.inc` → `diskops.inc` (the rename) | 0 / 0 / 0 | 0 / 0 / 0 |
| `winmod.inc` (new: the Windows theme's doors) | +36 / +32 / 0 | 0 / 0 / 0 |

**The resident core was passed this time**: `wm.inc`, `ui.inc`, `menu.inc`,
`mouse.inc`, `vga12.inc` and the scheduler all came down in `.text`, where
the last cycle's pass had not touched them. What grew is the file system's
`.cold` on `kern_big` - multi-select, image mounting, the two file classes and
the STOR door in `files.inc`, `disk.inc`, `filecp.inc` and `driver.inc`.

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked) and files (as shipped),
off each point's own `os88mod.py`. The claim is the image rounded up to whole
KB. `CLONE.DRV` is `DISKOPS.DRV` from `a32dfc3` on, so read those two rows
as one: `kern_big`'s is **9,211 → 10,289** (image mounting's +1,078) and
`kern_small`'s **7,196 → 7,184**.
| module | A image | C image | E image | B image | **B−C** | B file | claim A → C → B, KB |
|---|---:|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 11,450 | 12,136 | 11,660 | 12,346 | **+210** | 12,346 | 12 → 12 → 13 |
| `FORMAT.DRV` | 1,136 | 1,136 | 1,136 | 1,136 | **0** | 1,097 | 2 → 2 → 2 |
| `CLONE.DRV` | 9,211 | 9,211 | — | — | **−9,211** | — | 9 → 9 → — |
| `HIBER.DRV` | 5,489 | 5,489 | 5,493 | 5,493 | **+4** | 4,821 | 6 → 6 → 6 |
| `DOCK.DRV` | 2,246 | 2,246 | 2,233 | 2,233 | **−13** | 2,034 | 3 → 3 → 3 |
| `EXTD.DRV` | 1,023 | 1,023 | 1,023 | 1,023 | **0** | 1,023 | 1 → 1 → 1 |
| `DISKOPS.DRV` | — | — | 10,289 | 10,289 | **+10,289** | 9,198 | — → — → 11 |
| `WIN95.DRV` | — | — | 1,565 | 1,565 | **+1,565** | 1,320 | — → — → 2 |
| **`kern_small`** `CTRL.DRV` | 4,621 | 4,621 | 4,406 | 4,406 | **−215** | 4,233 | 5 → 5 → 5 |
| **`kern_small`** `FORMAT.DRV` | 1,136 | 1,136 | 1,136 | 1,136 | **0** | 1,099 | 2 → 2 → 2 |
| **`kern_small`** `CLONE.DRV` | 7,196 | 7,196 | — | — | **−7,196** | — | 8 → 8 → — |
| **`kern_small`** `FILECP.DRV` | 1,940 | 1,940 | 1,770 | 1,770 | **−170** | 1,740 | 2 → 2 → 2 |
| **`kern_small`** `FDLG.DRV` | 1,256 | 1,256 | 1,232 | 1,232 | **−24** | 1,213 | 2 → 2 → 2 |
| **`kern_small`** `DISKOPS.DRV` | — | — | 7,184 | 7,184 | **+7,184** | 6,522 | — → — → 8 |


**Two `kern_big` claims grow**: `DISKOPS.DRV` by two KB (9 → 11, image
mounting's +1,078 taking 10,289 bytes just past 10 KB) and `CTRL.DRV` by one
(12 → 13, #248's Ports view +686 and Force Color +238 less pass 10's −150).
`WIN95.DRV` is new, 2 KB, and loaded only while the Windows theme is picked.
Everything `kern_small` loads shrank.

## The loadable drivers

Built by `make` at A, C, E and B. The figure is the heap claim a mounted
driver costs, image + bss (byte +31 of the header, in paragraphs), with the
claim rounded up to whole KB; the file is as shipped.

| driver | A image+bss | C | E (elendilon tip) | **B** | **B−C** | claim A → B, KB | file A → B |
|---|---:|---:|---:|---:|---:|---|---|
| `EMS.DRV` (new, SPEC.md 107) | - | - | 702 | **702** | **+702** | - → 1 | - → 670 |
| `ETHER.DRV` | 16,778 | 16,827 | 16,912 | **16,963** | **+136** | 17 → 17 | 10,975 → 11,153 |
| `HDD.DRV` | 3,584 | 3,584 | 3,584 | **3,584** | **0** | 4 → 4 | 2,986 → 3,126 |
| `HDDTOOL.DRV` | 16,995 | 16,995 | 16,995 | **16,995** | **0** | 17 → 17 | 13,741 → 13,737 |
| `NET.DRV` | 4,153 | 4,153 | 4,153 | **4,153** | **0** | 5 → 5 | 3,613 → 3,613 |
| `RAMDISK.DRV` | 5,272 | 5,272 | 5,260 | **5,272** | **0** | 6 → 6 | 4,576 → 4,574 |
| `RAMPAGE.DRV` | 2,381 | 2,381 | 2,375 | **2,381** | **0** | 3 → 3 | 2,147 → 2,147 |
| `SAVER.DRV` | 10,403 | 10,403 | 10,403 | **10,403** | **0** | 11 → 11 | 7,388 → 7,388 |
| `SOUND.DRV` | 6,721 | 6,814 | 7,120 | **7,213** | **+399** | **7 → 8** | 5,669 → 6,169 |
| `USBMOUSE.DRV` | 1,151 | 1,151 | 1,396 | **1,396** | **+245** | 2 → 2 | 1,119 → 1,354 |
| `XMEM.DRV` | 1,014 | 1,014 | 1,014 | **1,014** | **0** | 1 → 1 | 966 → 966 |
| **all eleven** | 68,452 | 68,594 | 69,914 | **70,076** | **+1,482** | | |

**This was not a driver round** - the last one took 21,083 bytes of claim out
and this one puts 1,482 back, 702 of it a new driver.

- **`SOUND.DRV` crosses into an 8th KB at the squash.** `main`'s arm put +93
  into it (#246's probe reads before writes, #248's port pin), `elendilon`
  +399 (the PicoMEM tier in it by default, net of pass 11's driver rows),
  and the two land together at 7,213: B − E is exactly C − A.
  `DRVM_IMG_SND` was moved 7 → 8 in the squash for this reason
  (`t_drvmem` caught it).
- **`ETHER.DRV` is the one driver whose two arms do not add.** `main`'s
  #246 is +49 and `elendilon`'s round +134 (the PicoMEM NE2000 map test
  among it), and B is **+2 of claim** above their sum: the image is 14 bytes
  under it and the bss, which the header counts in paragraphs, 16 over. It
  is the driver whose probe was resolved by hand at the squash
  (`ne_probe`: `main`'s six-equal-bytes test, then `elendilon`'s map test).
  No kernel byte is involved. **`USBMOUSE.DRV`'s +245** is the PicoMEM USB
  mouse.
- **`RAMDISK.DRV` and `RAMPAGE.DRV` are E's −12 and −6 against A** because
  #241's two bugfixes (`rd_dir_clear`, `rp_init`) went to `elendilon-pr` and
  not to `elendilon`; B carries them again.
- **`HDD.DRV`'s claim is unchanged at 3,584** while its file grows 140 bytes
  (the autoload and the take); `EMS.DRV`'s *1,208 → 702* in its pass-11 merge
  is exact.
- **`VMMOUSE.DRV`** (`kern_emu` only) was not measured.

## The final bill

**What the PR asks `main` to take, on the kernel**, with B the PR:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #241 squash (A) and at `main`'s tip (C) | 99,328 | 65,024 |
| `KERN_SIZE` at the PR (B) | **98,816** | **62,464** |
| **change** | **−512** (one rung) | **−2,560** (five rungs) |
| resident bytes, against C | **−203** | **−2,631** |
| resident bytes, against A | −157 | −2,591 |
| window / blob (not resident), against C | +94 / +102 | 0 / −10 |
| heap a machine gets back | **512** | **2,560** |
| `KERN_BUDGET` (a decision in the round, not a measurement) | 129,536 → **100,864** | 107,520 → **64,000** |
| spare of the budget | 30,208 → **2,048** (4 steps) | 42,496 → **1,536** (3 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 49,441 → **48,825**: **16,711 left** | 36,008 → **34,928**: **30,608 left** |
| guard 5 | 92,160 before it cannot boot on 196KB → **23,040** before 128KB | 56,832 → **59,392** on 128KB |
| on-demand modules, against C | `DISKOPS.DRV` (was `CLONE.DRV`) +1,078, `WIN95.DRV` +1,565 new, `CTRL.DRV` +210, `DOCK.DRV` −13, `HIBER.DRV` +4 | `CTRL.DRV` −215, `FILECP.DRV` −170, `FDLG.DRV` −24, `DISKOPS.DRV` −12 |
| the loadable drivers, against C | **+1,482** of claim, `EMS.DRV` new and `SOUND.DRV` 7 → 8 KB | the same drivers |

**In one line each:**

- **`main`** spent **46 resident bytes on `kern_big` and 40 on
  `kern_small`** (#244's `LD_EVER` and #248's port pins), plus 686 of
  `CTRL.DRV` and 93 of `SOUND.DRV`. None of it has had a pass.
- **We** took **3,214** resident bytes out of `kern_big` and **2,900** out of
  `kern_small` in passes 10 and 11 and each feature's own pass rows,
  passing the resident core (`wm`, `ui`, `menu`, `mouse`, `vga12`), which
  the last round's pass had not touched. We then spent **+2,041 of it on `kern_big`**:
  multi-select, image mounting, the Windows theme, two file classes, EMS,
  the STOR door, and a dozen smaller features, of which ~865 resident bytes
  have never had a pass.
- **Together**, the PR is **one rung smaller on `kern_big` and five on
  `kern_small`** than the #241 squash, and the squash itself costs nothing:
  its residual is zero in every field.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s three baselines did not describe B or E.**
  `make` at B printed `sum +1,015` on `kern_big` with *"the image rung
  CROSSED"* and *"the cold rung CROSSED"*, `kern_small` `sum −79` with *"the
  cold rung UNCROSSED"*, and `kern_emu` `sum +991`; at E the same three read
  +950, −118 and +926. The baselines were last blessed partway through the
  round, before the features after pass 11, so every `make` since has
  reported their cost as if it were the change in hand. **Closed in the same
  commit as this file**: all three are re-blessed at B, and `kernsize` reads
  +0 on all three.
- **`kernsize --modules` listed `winmod.inc` as *(undescribed)*.** The Windows
  theme added the file without a row in KERNEL-MEMORY.md's table, which is
  where its description lives. Written in the same commit.
- **E's own build number is four digits** (1039), and that alone is +1 byte
  of `.text` on both kernels at E against the PR's 214. It is why every tree
  here is assembled at one number, and it will be +1 of `.text` on `main` the
  day `main`'s count reaches 1000.
- **No quoted figure was found wrong.** Every size-pass merge's own numbers,
  multi-select's, `EMS.DRV`'s and `HDD.DRV`'s were checked against their
  commits and agree to the byte.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`. `.lowbss`
and `.vgabuf` moved in no commit. *Window* and *blob* are as defined above,
so `170394d` and `8c95087`, which only rename sections, measure 0 and
are not listed. `main`'s rows
come first; the rest are `elendilon`'s round in topological order, and with
the merge residuals they sum to E − `9d39bf2d` exactly. The `KERN_SIZE`
columns do not, which is the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | **big resident** | big `KERN_SIZE` | small `.cold` | **small resident** | small `KERN_SIZE` | overlay / blob | modules (image) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| `c193155` | **main:** Packages know which os8088 they are for: LD_EVER, the Wire's format check, the ABI lock (S... | +23 | 0 | +17 | **+40** | 0 | +17 | **+40** | 0 |  |  |
| `dde6b19` | **main:** Port pins: the Drivers page's Ports view picks where the network card and the MPU-401 are ... | +9 | +2 | −5 | **+6** | 0 | 0 | **0** | 0 | big blob +20 | CTRL +686 |
| `bedc01c` | files: key table, view/row helpers, count-down ladders (kernel size pass 10) | +17 | 0 | −133 | **−116** | −512 | −115 | **−98** | 0 |  |  |
| `eb9407a` | files: fm_choose's in-place move repaints the WINDOW, not its path buffer | 0 | 0 | +2 | **+2** | 0 | 0 | **0** | 0 |  |  |
| `9fad9ff` | files: one path seed, the press wait on both kernels, pth_* shortened (kernel size pass 10) | 0 | 0 | −126 | **−126** | 0 | −61 | **−65** | 0 |  |  |
| `561ef06` | files, filecp: dead banks, fm_row16, one typing tail (kernel size pass 10) | 0 | 0 | −39 | **−39** | 0 | −35 | **−35** | 0 |  | small FILECP −4 |
| `6d6bad2` | files: Format's greying steps Clone's, fm_cells for three budgets (kernel size pass 10) | 0 | 0 | −21 | **−21** | 0 | −21 | **−21** | 0 |  |  |
| `f419c2c` | ui: one event-point load, ui_raise, ui_bill's stamp in the payload slots (kernel size pass... | −86 | 0 | 0 | **−86** | 0 | 0 | **−85** | 0 |  |  |
| `e44d5a1` | driver, assoc: answers the compare already gave, two byte multiplies, repe cmpsb (kernel s... | −1 | −1 | −47 | **−49** | −512 | 0 | **−1** | 0 |  |  |
| `ecb152b` | ui: grow clamp and tracker sum written once, the region dispatch in one compare (kernel si... | −58 | 0 | 0 | **−58** | 0 | 0 | **−57** | 0 |  |  |
| `d3dacdb` | ui, clock, driver: four tests the arriving state already answered (kernel size pass 10) | −10 | 0 | 0 | **−10** | 0 | −2 | **−12** | 0 |  |  |
| `55dfa53` | Merge ksp10-shell: kernel size pass 10, the shell agent's files (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |  |
| `939f212` | desk: one ring for the zone mask, one walk for revealed and fits (kernel size pass 10) | 0 | 0 | −74 | **−74** | −512 | 0 | **0** | 0 |  |  |
| `2da1ad0` | fdlg: a key reaches no window between a chooser's answer and its reap (SPEC.md 38.2) | 0 | 0 | −1 | **−1** | 0 | 0 | **0** | 0 |  | small FDLG −1 |
| `055c583` | fdlg: the hook falls into its body, a grab ends in cmc, one word opens the answer (kernel ... | 0 | 0 | −11 | **−11** | 0 | 0 | **0** | 0 |  | small FDLG −5 |
| `98bd41f` | desk: epilogues by the ladder, the cell's two xchgs, one lea for x2 (kernel size pass 10) | 0 | 0 | −16 | **−16** | 0 | −10 | **−10** | 0 |  |  |
| `8f068f6` | desk, fdlg: the reflow's two 0xFF stores, one live-record test (kernel size pass 10) | 0 | 0 | −5 | **−5** | 0 | 0 | **0** | 0 |  | small FDLG −3 |
| `6736d48` | fdlg: the gate's both-bits test is inc/and, the reap's answer rides DL (kernel size pass 10) | 0 | 0 | −1 | **−1** | 0 | 0 | **0** | 0 |  | small FDLG −1 |
| `0cbad09` | Merge ksp10-new: kernel size pass 10, the new agent's files (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |  |
| `05ce09e` | fsx, vid*, xmem, hiber: port moves by DL, fsx_insync owns its timeout, one axis loop in vi... | −50 | 0 | −10 | **−60** | 0 | 0 | **−35** | 0 |  |  |
| `2e84083` | vidsel, viddet, vmmouse: two ladder exits and gfx_ink by xlatb (kernel size pass 10) | −4 | 0 | 0 | **−4** | 0 | 0 | **−2** | 0 |  |  |
| `3bc0f9c` | viddet: vid_6845_prog writes each CRTC register with one word OUT (kernel size pass 10) | −5 | 0 | 0 | **−5** | 0 | 0 | **−5** | 0 |  |  |
| `b14ee66` | vidsel, fsx: two more DL-only port moves (kernel size pass 10) | −2 | 0 | 0 | **−2** | 0 | 0 | **−1** | 0 |  |  |
| `272ab18` | Merge ksp10-hw: kernel size pass 10, the hw agent's files (merge residual) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | −512 |  |  |
| `4a8de04` | mouse, menu: the packet decoders, the 1bpp move and the bar (kernel size pass 10) | −136 | 0 | −11 | **−147** | 0 | −11 | **−142** | 0 |  |  |
| `bdad097` | icons, toast, keyboard mouse: one bbop entry, one KB_FLAG reader (kernel size pass 10) | −53 | 0 | 0 | **−53** | 0 | 0 | **−53** | −512 |  |  |
| `9c23b0d` | menu, events: one dropper head, the save-under arithmetic inline, a drain that is evq_mup ... | −70 | 0 | 0 | **−70** | 0 | 0 | **−70** | 0 |  |  |
| `3a65253` | menu, mouse: menu_tbuf is the clock's field, two single stores (kernel size pass 10) | −6 | −25 | 0 | **−31** | −512 | 0 | **−31** | 0 |  |  |
| `f23a81a` | clip: clip_get banked two registers it never writes (kernel size pass 10) | −4 | 0 | 0 | **−4** | 0 | 0 | **−4** | 0 |  |  |
| `b6ebe50` | Merge ksp10-input: kernel size pass 10, the input agent's files (merge residual) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | +512 |  |  |
| `ae1e007` | Covox: the Sound page and SOUND.DRV's port code, size-passed (kernel size pass 10) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | CTRL −149, small CTRL −131 |
| `59dca54` | ctrl, snd: cp_open_x's three exits share one epilogue, snd_release_inst trusts drv_svc_cal... | −4 | 0 | −8 | **−12** | 0 | −6 | **−10** | 0 |  |  |
| `e908617` | snd: the tone expiry's generation compare could never refuse - gone (kernel size pass 10) | −24 | −2 | 0 | **−26** | 0 | 0 | **−26** | 0 | big window −10, small blob −10 |  |
| `d3c0d8e` | snd: snd_town_off makes its zero once (kernel size pass 10) | −3 | 0 | 0 | **−3** | 0 | 0 | **−3** | 0 |  |  |
| `6ae9cd6` | wm: one occlusion walk, one empty box, a register-free re-seed (kernel size pass 10) | −82 | 0 | 0 | **−82** | 0 | 0 | **−66** | 0 |  |  |
| `146f787` | wm: rect_get/rect_put at fourteen sites, the title bar off one BX, small ones (kernel size... | −207 | 0 | 0 | **−207** | −512 | 0 | **−203** | −512 |  |  |
| `1db2fbb` | wm: wm_kent_bp for sixteen prologues; back out the held rect_get/put sites (kernel size pa... | +98 | 0 | 0 | **+98** | +512 | 0 | **+104** | +512 |  |  |
| `6b9ed65` | wm: keep the save-under routines off wm_kent_bp (kernel size pass 10) | +25 | 0 | 0 | **+25** | 0 | 0 | **+25** | 0 |  |  |
| `a22183c` | wm: wm_znext, wm_top_dbp, grow box's fill-and-frame shared (kernel size pass 10) | −40 | 0 | 0 | **−40** | 0 | 0 | **−42** | −512 |  |  |
| `029d900` | wm: dead clears in wm_title_set and wm_covered, wm_clip_set keeps BX (kernel size pass 10) | −20 | 0 | 0 | **−20** | 0 | 0 | **−20** | 0 |  |  |
| `0890bb9` | wm: six routines share another's identical epilogue (kernel size pass 10) | −16 | 0 | 0 | **−16** | 0 | 0 | **−16** | 0 |  |  |
| `9a7e1c6` | gfx: shared tails, a dead scroll counter, font_run's colour pair as a word (kernel size pa... | −100 | 0 | 0 | **−100** | 0 | 0 | **−28** | 0 |  |  |
| `32768cd` | gfx: gfx_denter gone, gfx_disp_run clamps through gfx_rect_isectcf, gfx_sub_arm in registe... | −66 | 0 | 0 | **−66** | 0 | 0 | **−19** | 0 |  |  |
| `617a2f7` | gfx: gfx_xor_rect falls into its VRAM entry, clip_run without the SI swap, font loops on e... | −16 | 0 | 0 | **−16** | 0 | 0 | **−14** | 0 |  |  |
| `74bb897` | gfx: string loads in the seam cut, the opaque cell row and the run's edge columns; rcr in ... | −16 | 0 | 0 | **−16** | 0 | 0 | **−2** | 0 |  |  |
| `03f8b36` | gfx: gfxbench says the gray fill's shared tail costs 0.29% - give it back; the frame falls... | +5 | 0 | 0 | **+5** | 0 | 0 | **0** | 0 |  |  |
| `abf913c` | gfx: gfx_points' unarmed box is written once a pass by gfx_pt_resolve (kernel size pass 10) | −12 | 0 | 0 | **−12** | 0 | 0 | **0** | 0 |  |  |
| `869d34f` | Merge ksp10-gfx: kernel size pass 10, the gfx agent's files (merge residual) | 0 | 0 | 0 | **0** | −512 | 0 | **0** | 0 |  |  |
| `d12ffcf` | core: shared exits and folded walks in memory, instance, apps (kernel size pass 10) | −47 | 0 | −52 | **−99** | −512 | −19 | **−59** | 0 |  |  |
| `277fe05` | core: short jumps by placement, inst_where, loader refusals (kernel size pass 10) | −22 | 0 | −5 | **−27** | 0 | −5 | **−24** | 0 |  |  |
| `9d8b3d5` | core: unused register banks, store walks, one compare (kernel size pass 10) | −11 | 0 | −50 | **−61** | 0 | −42 | **−53** | 0 |  |  |
| `f83353f` | core: single-caller bodies inline in the compactor, a far-pointer verb (kernel size pass 10) | −23 | 0 | −27 | **−50** | 0 | 0 | **−10** | 0 |  |  |
| `d6d1cba` | core: banks the API cell already holds, two small reorders (kernel size pass 10) | −5 | 0 | −4 | **−9** | 0 | −4 | **−9** | 0 |  |  |
| `4293c0a` | core: cbw where AL is known small (kernel size pass 10) | −4 | 0 | −3 | **−7** | 0 | 0 | **−3** | 0 |  |  |
| `72a5e2d` | disk: two dead routines out, FAT flush/window keep their registers, one BPB zap loop (kern... | 0 | 0 | −129 | **−129** | −512 | −129 | **−129** | 0 |  |  |
| `940b627` | diskw: dskw_wdata and dskw_rdata share one cluster body, dskw_xclus (kernel size pass 10) | 0 | 0 | −45 | **−45** | 0 | −46 | **−46** | 0 |  |  |
| `8834f28` | disk/diskw/drvvol: kentc_di in eleven bodies, one widget arm, the DMA bounce as a far poin... | 0 | 0 | −60 | **−60** | 0 | −46 | **−46** | 0 |  |  |
| `47cf0ec` | disk/diskw: one name-formatter loop, a dead KANJI test, flag-dead word stores (kernel size... | 0 | 0 | −61 | **−61** | 0 | −60 | **−60** | 0 |  |  |
| `118b1a4` | diskw/disk: register banks no caller needed, two masks for two divides (kernel size pass 10) | 0 | 0 | −46 | **−46** | 0 | −46 | **−46** | 0 |  |  |
| `f37d373` | diskw: one FSV_STAT-and-refuse for the two redirected read bodies (kernel size pass 10) | 0 | 0 | −10 | **−10** | 0 | −10 | **−10** | 0 |  |  |
| `c5434be` | diskw: three far entries lose their thunk (kernel size pass 10) | 0 | 0 | −14 | **−14** | 0 | −10 | **−10** | 0 |  |  |
| `fe08340` | disk/diskw: dskw_dotents unbanked, dsk_xfer's bounce laid out so its jumps stay short (ker... | 0 | 0 | −20 | **−20** | 0 | −20 | **−20** | 0 |  |  |
| `f57e5c9` | Merge ksp10-disk: kernel size pass 10, the disk agent's files (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | −512 |  |  |
| `c4ee5a9` | xfile: cw_mem_disp is spw_near, wm_dmg_gray's dead jc, ui_krect4 inlined (kernel size pass... | −5 | 0 | −4 | **−9** | 0 | −16 | **−19** | 0 |  |  |
| `86d63d9` | xfile: the word-immediate sweep - four register masks a byte each, and a free-cluster coun... | 0 | 0 | −8 | **−8** | 0 | −8 | **−8** | 0 |  | CTRL −1 |
| `9677dc6` | kernel: a .text prologue ladder - kent_bp (wm_kent_bp, moved) and kent_di, at 19 once-per-... | −46 | 0 | 0 | **−46** | 0 | 0 | **−42** | 0 |  |  |
| `77622e8` | kern_small: drv_svc_call_x's stub returns FAR, as the thunk that calls it does | 0 | 0 | 0 | **0** | 0 | +4 | **+4** | 0 |  |  |
| `55e5cc9` | wm_clip_rows: grow the winner's rows through adjacent fragments (SPEC.md 11.3.4.3) | +92 | 0 | 0 | **+92** | 0 | 0 | **0** | 0 |  |  |
| `8e1d317` | EMS.DRV: expanded memory on a LIM EMS board (SPEC.md 107) | +72 | +38 | +3 | **+113** | 0 | 0 | **0** | 0 | big blob +1 | CTRL +1 |
| `288304a` | Kernel: ui_timer_pass asks again under the lock before calling a timer | +12 | 0 | 0 | **+12** | 0 | 0 | **0** | 0 |  |  |
| `737d08f` | Kernel + HDD.DRV: OSAPI_VOL_TAKE hands the boot volume to the driver | +6 | 0 | +78 | **+84** | 0 | +78 | **+84** | 0 |  |  |
| `94c6d00` | Disk window multi-select (SPEC.md 22.27), kern_big: all three waves | +25 | +66 | +1,467 | **+1,558** | +1,024 | 0 | **0** | 0 |  |  |
| `791cc93` | Multi-select: size pass (-138 bytes) and the record of what it cost | 0 | 0 | −140 | **−140** | 0 | 0 | **0** | 0 |  |  |
| `d6b7904` | Multi-select size pass 2: 1,418 -> 797 resident bytes | −12 | −21 | −588 | **−621** | −512 | 0 | **0** | 0 |  |  |
| `a98c4d2` | PicoMEM: tier in SOUND.DRV by default, USB mouse, B: from the card, Restart warm-resets | 0 | 0 | +40 | **+40** | 0 | +40 | **+40** | 0 | big window +85 |  |
| `339ed45` | dsk_xfer: ask once a run whether a bar is live, not once a sector | 0 | 0 | +15 | **+15** | 0 | +15 | **+15** | 0 |  |  |
| `4f76933` | FTPD STOR: commit every 256 KB on a fixed disk, not every chunk (SPEC.md 77.50, 18.4.9.3) | +34 | +2 | +72 | **+108** | 0 | 0 | **0** | 0 |  |  |
| `cd0cfe0` | dskw_alloc: search a FAT16 window with repne scasw (SPEC.md 18.4.10) | 0 | 0 | +114 | **+114** | 0 | +114 | **+114** | 0 |  |  |
| `bc6b0d8` | Merge ftp-speed into elendilon: a STOR to a fixed disk commits every 256 KB instead of eve... (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |  |
| `6de33d0` | diskw.inc: dskw_alloc's FAT16 scan shares the loop's wrap (kernel size pass 11) | 0 | 0 | −42 | **−42** | 0 | −42 | **−42** | 0 |  |  |
| `b1b35d2` | diskw/disk/ui/vga12: WSEQF_KEEP and WSEQF_CKPT, 31 bytes back (kernel size pass 11) | −9 | 0 | −22 | **−31** | −512 | 0 | **0** | 0 |  |  |
| `0cc9a99` | disk/diskw: four relaxed jumps on error paths made short (kernel size pass 11) | 0 | 0 | −15 | **−15** | 0 | −15 | **−15** | 0 |  |  |
| `82fd9ed` | wm.inc: wm_clip_rows' exit above the walk, the grow in line (kernel size pass 11) | −28 | 0 | 0 | **−28** | 0 | 0 | **−8** | 0 |  |  |
| `e9fd2b6` | disk/kernel.asm/xmem/ui: OSAPI_VOL_TAKE folds into OSAPI_VOL_ADD's DX; one-store take and ... | −18 | 0 | −12 | **−30** | 0 | −12 | **−18** | 0 |  |  |
| `b2c40be` | disk/driver: kern_small's driver-volume slots are the stub block's refusals (kernel size p... | 0 | 0 | 0 | **0** | 0 | −418 | **−422** | 0 |  |  |
| `d45ba1d` | driver: DRVC_EMS keeps no table copy; its door rides in drv_fptr7's offset half (kernel si... | +1 | −36 | +10 | **−25** | 0 | 0 | **0** | 0 |  |  |
| `cce02d3` | Merge ksp11-voltake: kernel size pass 11, the video-xms kernel side (kern_big -55, kern_sm... (merge residual) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | −512 |  |  |
| `9a536e1` | disk.inc: PicoMEM warm reset -5, dsk_fdd_pmemu banks nothing, park out of kern_dos (kernel... | 0 | 0 | −5 | **−5** | 0 | −5 | **−5** | 0 | big window −11 |  |
| `0ce30c9` | files.inc: multi-select's third size pass, and fm_onkey_x's relaxed jumps (kernel size pas... | 0 | 0 | −62 | **−62** | −512 | −7 | **−7** | 0 |  |  |
| `92cdf8e` | files.inc: fm_hit's miss, and fm_draw_status's swap prompt past the exit (kernel size pass... | 0 | 0 | −6 | **−6** | 0 | −4 | **−4** | 0 |  |  |
| `9a3cdaa` | files.inc: fm_mods' zero is the chooser's answer, fm_armset banks nothing (kernel size pas... | 0 | 0 | −7 | **−7** | 0 | 0 | **0** | 0 |  |  |
| `65efb44` | files.inc: fm_mset banks through kentc_di; SPEC.md 22.27 at 755 bytes (kernel size pass 11) | 0 | 0 | −4 | **−4** | 0 | 0 | **0** | 0 |  |  |
| `065e19c` | Merge ksp11-multisel: kernel size pass 11, multi-select's third pass (kern_big -79, kern_s... (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |  |
| `e2c478d` | disk/diskw/loader/driver/snd/ctrl/fdlg/compress: kern_small carries none of the redirector... | 0 | 0 | 0 | **0** | 0 | −333 | **−403** | 0 |  | small CLONE −12, small CTRL −84, small FDLG −14 |
| `4aff93f` | driver/menu/vga12/dockmod/stkbalance: relaxed jumps made short, gfx_blit1's frame torn dow... | −5 | 0 | −16 | **−21** | 0 | −2 | **−7** | 0 |  | DOCK −13 |
| `2e8417f` | kernel/driver/ui/instance/ctrl/desk: kern_small's fenced file cells and driver hooks are t... | 0 | 0 | 0 | **0** | 0 | −32 | **−115** | 0 |  |  |
| `d132919` | disk: kern_small declares no relocation proc for a heap that never moves (kernel size pass... | 0 | 0 | 0 | **0** | 0 | −17 | **−39** | 0 |  |  |
| `a5f3ec4` | filecp/memory/files/driver: kern_small's last redirected arms, and the refusing stubs they... | 0 | 0 | 0 | **0** | 0 | −43 | **−43** | 0 |  | small FILECP −166 |
| `01f38d6` | Seek on any cluster size: READ_AT in whole sectors on kern_big | 0 | +1 | +55 | **+56** | 0 | 0 | **0** | 0 |  |  |
| `e061a2f` | ui.inc: bank SI across the W_ONCLICK dispatch - a handler that clobbers SI as SPEC.md 12.2... | +2 | 0 | 0 | **+2** | 0 | 0 | **+2** | 0 |  |  |
| `e14c391` | Driver move: int 33h follows the image, so a PicoMEM USB mouse survives a compaction | +4 | 0 | 0 | **+4** | 0 | 0 | **0** | 0 |  |  |
| `2b20cca` | sw_col: one store a byte - the "flaky" CGA flash in deskflash was real | −7 | 0 | 0 | **−7** | 0 | 0 | **−4** | 0 |  |  |
| `c461c5b` | Force Color: undo a VGA BIOS's POST "monochrome display" verdict (SPEC.md 39.28) | 0 | +1 | +28 | **+29** | 0 | 0 | **0** | 0 | big blob +15 | CTRL +238 |
| `c4b43d0` | Windows theme, phase 1: WIN95.DRV and the fourth theme (SPEC.md 76.14) | +141 | +8 | +83 | **+232** | +1,024 | 0 | **0** | 0 |  | CTRL +77, WIN95 +1,560 |
| `ac7b5b1` | OSAPI_ICON_DRAW takes ICON_SOLID records: the mask is implied, not stored (SPEC.md 25.6.2) | +15 | 0 | 0 | **+15** | 0 | 0 | **0** | 0 |  |  |
| `4094670` | OSAPI_FERR_TEXT: the kernel's own words for a FERR_*, handed to a package (SPEC.md 18.4.11) | +25 | 0 | 0 | **+25** | 0 | 0 | **+25** | 0 |  |  |
| `c04ffc4` | OSAPI_PKG_REHOME's KEEP form: the re-home cuts the carve's tail to what the loader keeps (... | 0 | +2 | +39 | **+41** | 0 | 0 | **0** | 0 |  |  |
| `99bd94e` | kern_small: the re-home is not assembled at all - its test, step 8a, mem_reown_x and their... | 0 | 0 | 0 | **0** | 0 | −127 | **−131** | −512 |  |  |
| `a32dfc3` | Rename CLONE.DRV to DISKOPS.DRV: it is the disk operations, not the cloner | +2 | 0 | 0 | **+2** | 0 | 0 | **+2** | 0 |  | CLONE −9,211, DISKOPS +9,211, small CLONE −7,184, small DISKOPS +7,184 |
| `c356413` | Mount a .IMG as a drive: double-click to mount, again to unmount (SPEC.md 18.101) | 0 | +2 | +196 | **+198** | +512 | 0 | **0** | 0 |  | DISKOPS +932 |
| `5ebea5a` | Two file classes: the RAM disk and the cable attached at once (SPEC.md 62.9.19) | −11 | +42 | +72 | **+103** | 0 | 0 | **0** | 0 |  | HIBER +4 |
| `44f365d` | Merge origin/elendilon (the Windows theme) into elendilon after mount-img (merge residual) | 0 | 0 | 0 | **0** | −512 | 0 | **0** | 0 |  |  |
| `62b7505` | Windows theme: a right-click leaves the context menu up | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 |  | WIN95 +5 |
| `cc8d977` | Disk window: the bar is File / Edit / Nav / Disk, and images mount from a menu | +10 | 0 | +31 | **+41** | 0 | −15 | **−31** | 0 |  | DISKOPS +150 |
| `d69e19b` | Images: unmount off the desktop icon, and a .IMG draws the diskette | +2 | 0 | +151 | **+153** | 0 | 0 | **0** | 0 |  | DISKOPS −4 |
| `f84001b` | Disk window: the empty-space menu is what you see, a rule, then what you do | +3 | 0 | 0 | **+3** | 0 | 0 | **+3** | 0 |  |  |
| `f9a3287` | Merge mount-img into elendilon: the Disk window's menus re-cut, images mount from them (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |  |
| `066c054` | .IMG icon: store the half-size diskette instead of folding it, 109 -> 69 bytes | +24 | 0 | −64 | **−40** | −512 | 0 | **0** | 0 |  |  |
| `edd0efb` | drivers: a row the user never answered is the machine's, every boot - the DA key (SPEC.md ... | 0 | +2 | 0 | **+2** | 0 | 0 | **0** | 0 | big blob +55 | CTRL +25 |
| `8cead00` | drivers: HDD.DRV loads itself when the BIOS knows a disk, and a default never nags or ling... | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | big window +30, big blob +31 | CTRL +19 |
| `78298be` | kernel: a failed mount is not a place to go back to (SPEC.md 51.5.2.1) | 0 | 0 | +15 | **+15** | 0 | +15 | **+15** | 0 |  |  |
