# Kernel bytes since the #230 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-10-08 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13.16. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Each of the **216
trees** below was measured in a clean worktree of its own commit: that
commit's own Makefile was parsed for its `KINC`, `VIDDEF`, `KZDEF` and
`KMODARGS` (the parse writes its `buildnum.inc`), the pass-1 kernel was
assembled exactly as its `kernel-full.bin` rule does, its own
`tools/os88mod.py` cut the modules, and its own `kernsize` measured the
sections, on `kern_big` and on `kern_small`. No figure is carried between
points. The ten loadable drivers were built by `make` at six points only
(below). It is true of the commits it names and of no other tree.

It is the eighth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-01.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-03.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `2657efea` | **the last squash with `main`**: *Elendilon -> Main (Standard File Dialog as a Disk Window, Kernel Under 100KB, Desktop Cells Drawn Once, LZ4 Past 64KB, Floppy Cylinder Reads, Bugfixes)* (#230), 2026-10-06 |
| **C** | `95f7e971` | **upstream** `main` (`jggonz/os8088`) at its tip, **+13 commits** over A: #200, #225 (PiXEL), #229 (Covox), #231-#240. The fork's own `origin/main` is the same commit (checked against upstream with `git ls-remote`) |
| **E** | `53d152f4` | `elendilon` at its tip, 2026-10-08: **+172 commits** over `b43231e9`, 127 that are not merges and 45 merges |
| **D** | `d2dc70f1` | `elendilon-next`'s squash: C with E's work applied as one commit (base `b43231e9`, below) |
| **B** | `5b0130fc` | `elendilon-next` at its tip: D **+12 review-fix commits**. This is what the PR carries, and it is cut from C, so there is no trial merge to build: **B is the PR** |

**One commit landed on `elendilon-next` while this was being measured**:
`d926b32c` (*KBDDIAG=1: a keyboard recorder ...*, SPEC.md 9.8.1). Its kernel
source is behind the knob, and it **measures identical to `5b0130fc` on both
kernels, in every section and every module image**, so every B figure below
is its tip's too. It touches no driver.

**Why E's base is `b43231e9` and not A.** #230's head on upstream
(`refs/pull/230/head`, `a7e21899`) is `elendilon` at `b43231e9` plus nine
commits upstream added on the PR itself (eight review fixes and a CLAUDE.md
swap). `a7e21899`'s tree is A's tree, and it **measures identical to A** on
both kernels, in every section and every module. So A is `b43231e9` plus
those nine, and `elendilon`'s round since the squash is exactly
`b43231e9..53d152f4`.

**A chains to the previous report exactly.** That report's B (`08ae9680`)
measures **identical to `b43231e9`** on both kernels, in every section and
every module image (the `main` merge between them moves nothing). A is
then that tree plus #230's own review fixes, below.

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count (SPEC.md 14.2): 192 at A, 205 at C, 206 at D, 218 at B, 460 at
E and 282 at the previous report's B. Three digits at every point measured.

**`associco.inc` is byte-identical at every endpoint** (A, `b43231e9`, E, C,
D and B, built from each point's own packages: md5 `c4b3e546060c...`, the
same on both kernels), and no commit in any range touches
`tools/os88mini.py`. It is the one generated include a PACKAGE commit could
move a kernel byte through, so the 216 trees were assembled against that one
copy.

**`KERN_BUDGET` is 129,536 and `KERN_SMALL_BUDGET` 107,520 at every point.**
Nothing below is a budget move.

## Headline: `kern_big`, the shipped default

| section | A base | C main | B, the PR tip | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 44,304 | 44,304 | 44,308 | 0 | +4 | +4 |
| `.bss` | 5,152 | 5,152 | 5,133 | 0 | −19 | −19 |
| `.cold` | 38,652 | 38,652 | 37,921 | 0 | −731 | −731 |
| `.lowbss` | 5,598 | 5,598 | 5,598 | 0 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 | 0 |
| **resident** | 94,042 | 94,042 | **93,296** | **0** | **−746** | **−746** |
| `.ovl` | 2,470 | 2,470 | 2,470 | 0 | 0 | 0 |
| `.ovlw` | 5,011 | 5,011 | 5,011 | 0 | 0 | 0 |
| **overlay** | 7,481 | 7,481 | **7,481** | **0** | **0** | **0** |
| **`KERN_SIZE`** | 99,840 | 99,840 | **99,328** | **0** | **−512** | **−512** |
| spare of the budget | 29,696 | 29,696 | 30,208 | | | |

**−746 resident and one rung back.** `KERN_SIZE` falls **99,840 → 99,328**,
so every `kern_big` machine gets **512 bytes of heap** back. `main` moved no
resident byte at all between A and C, so the PR's figure against `main` as it
stands (B−C) is the same −746. Resident is `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`; overlay is `.ovl` + `.ovlw`, which `mem_unblob` gives
back once the desktop is up. `.boot2` is 2,265 at every endpoint.

**This was not a resident pass.** The cycle's size work was the
SYSTEM side: the on-demand modules, the ten loadable drivers, `os88ui.inc`
and the module glue. The resident core (`wm.inc`, `files.inc`, `disk.inc`
and the rest) was not passed this cycle: no file outside the module glue
moved by more than 8 bytes, and `wm.inc` and `desk.inc` not at all.
Most of what this squash gives back is in the modules and the drivers below,
which are not in `KERN_SIZE`.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | C main | B, the PR tip | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 32,905 | 32,905 | 32,904 | 0 | −1 | −1 |
| `.bss` | 3,119 | 3,119 | 3,104 | 0 | −15 | −15 |
| `.cold` | 24,428 | 24,428 | 23,950 | 0 | −478 | −478 |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 | 0 |
| **resident** | 63,320 | 63,320 | **62,826** | **0** | **−494** | **−494** |
| `.ovl` | 2,086 | 2,086 | 2,086 | 0 | 0 | 0 |
| `.ovlw` | 1,480 | 1,480 | 1,480 | 0 | 0 | 0 |
| **overlay** | 3,566 | 3,566 | **3,566** | **0** | **0** | **0** |
| **`KERN_SIZE`** | 65,536 | 65,536 | **65,024** | **0** | **−512** | **−512** |
| spare of the budget | 41,984 | 41,984 | 42,496 | | | |

**−494 resident and one rung back.** `KERN_SIZE` is **65,024**, under 64KB
for the first time in this family; the floor machine gets **512 bytes of
heap** back.

## The attribution is exact

Every commit was measured against its first parent, on both kernels, in four
ranges: `main`'s arm (A..C), #230's own tail (`b43231e9..a7e21899`),
`elendilon`'s round (`b43231e9`..E) and the review fixes (D..B). A merge M
gets a figure too, its RESIDUAL: what M moved against its first parent, less
what its second parent moved against the merge base. In every range, the
commits' figures and the merges' residuals **sum to the range's endpoints
exactly, in every section and every module image, on both kernels**
(checked, not assumed).

**44 of `elendilon`'s 45 merges have a residual of 0 in every field.** The
45th, `12756a6` (*Merge FDLG size pass ... into kernel-side-size-p1*), moves
`kern_big`'s `KERN_SIZE` **+512 with no byte of its own**: each of its sides
crossed a cold rung down from the same base, and the two savings together
cross one rung, not two.
**Rungs do not add**, as every report in this family has found: the round's
commits move `kern_big`'s `KERN_SIZE` by −1,536 and the round moved −1,024.

**25 of the 127 non-merge commits move a byte**: 15 a resident one and 10 a
module image only. The other 102, the Video Player and encoder round among
them, measure 0 in every section and module on both kernels.

The one residual that is not a merge commit is the squash itself, D−C less
E−`b43231e9`:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| resident | **+2** (`.cold`, `fdlg.inc`) | **0** |
| `KERN_SIZE` | **+512** | 0 |
| module images | `CTRL.DRV` **+6** | `FDLG.DRV` **+5** |

- **The +2 is mine, at the squash.** `elendilon`'s size pass had rewritten
  `fdlg_grab`'s tail as `clc` / `cmc` (two bytes shorter than
  `stc` / `jmp short` / `clc`), and #230's review fix rewrote the same
  routine to swallow presses into an answered or gone chooser. The squash
  took `main`'s routine whole to keep its fix, and with it the longer tail.
  The two bytes are recoverable: the `cmc` shape fits `main`'s new branches.
- **The +512 is #230's review fixes meeting `elendilon`'s rung.**
  `elendilon` never had those fixes: its round took `kern_big`'s `.cold` to
  37,713, two rungs under A. The squash adds #230's +206 of `.cold` back on
  top (+2 above), lands at 37,921, and crosses one of the two back up. So
  against C the PR is two rungs of work and one rung of heap.
- **`CTRL.DRV` +6 and `FDLG.DRV` +5 are the three `ctrl.inc` hunks and the
  `fdlg.inc` hunks the squash resolved by hand** (main's Covox page and
  Cylinder message onto the size-passed panel; main's chooser fixes onto the
  moved `fdlg_grab` / `fdlg_top`). They were not split further.

## `main`'s arm: 0 resident bytes, +286 of `CTRL.DRV`, +259 of `SOUND.DRV`

| commit | what it touched | `kern_big` resident | `kern_small` resident | modules and drivers |
|---|---|---:|---:|---|
| #229 `fe9a559` *Covox Speech Thing* | `ctrl.inc`, `snd.inc`, `drivers/sound/` | **0** | **0** | `CTRL.DRV` **+286** (`kern_small` **+258**), `SOUND.DRV` **+259** |
| #238 `a5b474f` *wm_dmg_gray: keep .whole's zone re-owe* | `wm.inc` | 0 | 0 | |
| #225, #200, #231-#237, #239, #240 | PiXEL, the Covox disks and bench, the CF settings keeper, usb-emu, the strict-near jumps, the deskwhole/deskpen gates, the refresh-stale-pr skill | 0 | 0 | |

**Three of `main`'s commits touch kernel source and none of them costs a
resident byte**: #238's 13 lines of `wm.inc` are comment lines (no
instruction changes), and #229's `ctrl.inc` is the Sound page, which is
`CTRL.DRV`'s (the `.modu` section) and on demand; its `snd.inc` change is
two `equ`s. Its `SOUND.DRV` growth is a driver, loaded only on a machine
that asks for sound.

## #230's own review fixes: +227 on `kern_big`, +15 on `kern_small`, in A

These are the nine commits upstream added on #230's head after it left
`elendilon`. They are in A and therefore in no delta above, but they are
the whole of the difference between the previous report's tip and A, and
they had no size pass, so they are written down here:

| commit | `kern_big` resident | `kern_small` resident | modules |
|---|---:|---:|---|
| `7cd330e` Standard File chooser: nothing reaches a chooser that has ended | **+73** | **+15** | `kern_small` `FDLG.DRV` +43 |
| `3b503fd` Disk window arrows and `gfx_blit1` | **+7** | 0 | |
| `e890eb7` wm/desk: a zone or an L that would overflow the dither's region | **+147** | 0 | |
| `67237c0` Floppy page Cylinder test | 0 | 0 | `CTRL.DRV` +39 |
| the other five (tests, docs, REDLINE references, CLAUDE.md) | 0 | 0 | |
| **total** | **+227** (`.text` +19, `.bss` +2, `.cold` +206) | **+15** (`.bss` +2, `.cold` +13) | |

Neither kernel crossed a rung on them: `KERN_SIZE` is 99,840 and 65,536 at
both ends.

## Our arm, by concept

`elendilon`'s round (`b43231e9`..E), per concept, the sum of its commits.
Every row is exact.

| concept | commits | **`kern_big` resident** | **`kern_small` resident** | its module |
|---|---|---:|---:|---|
| **`FILECP` size pass** (SPEC.md 22.3), with `fcp_claim`'s span fix (+2) and, on `kern_small`, `FILECP.DRV` calling out through `fcpx_go` instead of 34 resident `xf_` thunks (−146) | `30cf3bd` `dcc0c27` `adb7a1e` | **−354** | **−178** | `kern_small` `FILECP.DRV` −297 |
| **`FDLG` glue size pass** (SPEC.md 38.13.1) and `FDLG.DRV` calling out through `fdx_go` | `c54f4a6` `b05b601` | **−122** | **−58** | `kern_small` `FDLG.DRV` −78 |
| **`os88ui.inc` size pass, the kernel's copy** (assembled into `fdlg.inc`): About card, glyph, button body, gesture, scroll bar, then check box, radio, drop box, alert | `4fa17c9` `9eed89a` | **−267** | **−253** | |
| **module stubs and glue**: FORMAT's four stubs share one refusal (−9/−7) and its prologue is `dskw_fmt_go` (+4/+4), `drv_find` asks the file (−4/−4), `hb_wakep` deleted (−4/0), EXTD's +4 of `.text`, CTRL's `cpc_buf` (+1/0), one byte of `ctrl.inc` (0/−1) | `6b6bdd6` `d253bd2` `e74054a` `553c50c` `4fcd708` `a76522b` `fb9cae6` | **−8** | **−8** | |
| **one fix**: a shed Disk window re-claims its store before the re-list names it | `bfa28cf` | **+3** | **+3** | |
| **the round** | | **−748** | **−494** | |

### The arm, whole

| | `kern_big` resident | `kern_small` resident | `kern_big` `KERN_SIZE` | `kern_small` `KERN_SIZE` |
|---|---:|---:|---:|---:|
| `elendilon`'s round | −748 | −494 | −1,024 | −512 |
| the squash's residual | +2 | 0 | +512 | 0 |
| the review fixes (D..B) | 0 | 0 | 0 | 0 |
| **our net (B − C)** | **−746** | **−494** | **−512** | **−512** |
| `main`'s arm (C − A) | 0 | 0 | 0 | 0 |
| **B − A** | **−746** | **−494** | **−512** | **−512** |

**The review fixes cost the kernel nothing.** All twelve (`a5e5916`..`5b0130f`)
measure 0 resident on both kernels; one moves a module (`4750cee`, `CLONE.DRV`
−3), the two `RAMDISK.DRV` fixes (`5920362`, `568ce07`) put +13 into that
driver between them, and `5bac0cf`'s `HDDTOOL.DRV` fix packs 2 bytes smaller
at the same image (drivers measured at the endpoints only).

**Every pass's own record is confirmed to the byte** where it quotes a
module image or a section: `FILECP` (image 2,237 → 1,898, `kern_big`
`.cold` −344), `FDLG` (image 1,286 → 1,164, `.cold` −109), `FORMAT.DRV`
(1,298 → 1,150 → 1,136), `DOCK.DRV` (2,515 → 2,256 → 2,246), `HIBER.DRV`
(6,662 → 5,499), `CTRL.DRV` pass 1 (12,220 → 11,649 and 5,735 → 5,347),
`EXTD.DRV` (1,566 → 1,069 → 1,022, its +4 of `.text`) and `CLONE.DRV`
(−492, −30) all measure exactly what their commits say.

### What has not had a pass

`elendilon`'s pass was cut at `b43231e9`, so three things in B have never
had one:

- **#230's review fixes, +227 resident on `kern_big` and +15 on
  `kern_small`** (above), of which `e890eb7`'s desktop overflow count is
  147. They are resident, so they are the first thing a pass should read.
- **#229's Covox page**, `CTRL.DRV` **+292** on `kern_big` (+286 as written,
  +6 at the squash) and **+258** on `kern_small`, and its `SOUND.DRV` half,
  **+252** onto the size-passed driver (+259 as written onto the old one).
- **The squash's +2** in `fdlg_grab` (above).

Against the 50% target passes have been briefed with, that is **~114 bytes
owed on `kern_big`** and ~8 on `kern_small`, plus ~146 of `CTRL.DRV` and
~126 of `SOUND.DRV`.

## How close B stands to the next rung

This is what the next addition is billed against, not what this cycle cost:

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A and at C | 208 left | 260 left | **34 left** |
| `kern_big` at B | 223 left | 479 left | **34 left** |
| `kern_small` at A and at C | 328 left | 148 left | 204 left |
| `kern_small` at B | 344 left | **114 left** | 204 left |

**`kern_big`'s low rung is the near one for the third report running,
478/512 spent**: the next 35 bytes of `.lowbss` cost a whole rung. Its cold
rung is almost empty (33/512) because the squash crossed it (above).
**`kern_small`'s cold rung is the one that moved closer**, 398/512: its 512
came off the cold rung, and the 478 bytes of `.cold` it saved leave the new
rung that full. Per CLAUDE.md's banner this decides WHEN the machine
pays and never what a change cost.

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked) and files (as shipped),
off each point's own `os88mod.py`. The claim is the image rounded up to whole
KB.

| module | A image | C image | B image | **B−C** | B file | claim A → C → B, KB |
|---|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 12,259 | 12,545 | 11,450 | **−1,095** | 11,450 | 12 → 13 → 12 |
| `FORMAT.DRV` | 1,298 | 1,298 | 1,136 | **−162** | 1,097 | 2 → 2 → 2 |
| `CLONE.DRV` | 10,073 | 10,073 | 9,211 | **−862** | 8,214 | 10 → 10 → 9 |
| `HIBER.DRV` | 6,662 | 6,662 | 5,489 | **−1,173** | 4,815 | 7 → 7 → 6 |
| `DOCK.DRV` | 2,515 | 2,515 | 2,246 | **−269** | 2,046 | 3 → 3 → 3 |
| `EXTD.DRV` | 1,566 | 1,566 | 1,023 | **−543** | 1,023 | 2 → 2 → 1 |
| **`kern_small`** `CTRL.DRV` | 5,735 | 5,993 | 4,621 | **−1,372** | 4,408 | 6 → 6 → 5 |
| **`kern_small`** `FORMAT.DRV` | 1,298 | 1,298 | 1,136 | **−162** | 1,097 | 2 → 2 → 2 |
| **`kern_small`** `CLONE.DRV` | 9,204 | 9,204 | 7,196 | **−2,008** | 6,535 | 9 → 9 → 8 |
| **`kern_small`** `FILECP.DRV` | 2,237 | 2,237 | 1,940 | **−297** | 1,892 | 3 → 3 → 2 |
| **`kern_small`** `FDLG.DRV` | 1,329 | 1,329 | 1,256 | **−73** | 1,221 | 2 → 2 → 2 |

**Every module shrank, and six claims are a KB smaller**: `CLONE.DRV`,
`HIBER.DRV` and `EXTD.DRV` on `kern_big`, and `kern_small`'s `CTRL.DRV`, `CLONE.DRV` and
`FILECP.DRV`; `CTRL.DRV` on `kern_big` is back at 12 KB after the Covox
page took it to 13. `CTRL.DRV` and `EXTD.DRV` still ship UNCOMPRESSED on
`kern_big` (file = image), so their savings are system-disk bytes one for
one.

## The loadable drivers

New in this family: the cycle's size work was mostly here, so the ten
drivers `make` builds for every kernel are measured too, at A, C, E and B
(and at `b43231e9`, which agrees with A, and D, which is B less the review
fixes). The
figure is the heap claim a mounted driver costs, image + bss (byte +31 of
the header, in paragraphs), with the claim rounded up to whole KB.

| driver | A image+bss | C | E (elendilon tip) | **B** | **B−C** | claim A → B, KB | file A → B |
|---|---:|---:|---:|---:|---:|---|---|
| `ETHER.DRV` | 19,034 | 19,034 | 16,778 | **16,778** | **−2,256** | 19 → 17 | 11,894 → 10,975 |
| `HDD.DRV` | 5,120 | 5,120 | 3,584 | **3,584** | **−1,536** | 5 → 4 | 3,600 → 3,013 |
| `HDDTOOL.DRV` | 19,867 | 19,867 | 16,995 | **16,995** | **−2,872** | 20 → 17 | 14,317 → 13,741 |
| `NET.DRV` | 6,650 | 6,650 | 4,153 | **4,153** | **−2,497** | 7 → 5 | 5,148 → 3,613 |
| `RAMDISK.DRV` | 8,952 | 8,952 | 5,247 | **5,260** | **−3,692** | 9 → 6 | 5,210 → 4,567 |
| `RAMPAGE.DRV` | 3,697 | 3,697 | 2,375 | **2,375** | **−1,322** | 4 → 3 | 3,024 → 2,141 |
| `SAVER.DRV` | 15,001 | 15,001 | 10,403 | **10,403** | **−4,598** | 15 → 11 | 8,447 → 7,388 |
| `SOUND.DRV` | 7,721 | 7,980 | 6,469 | **6,721** | **−1,259** | 8 → 7 | 5,997 → 5,669 |
| `USBMOUSE.DRV` | 1,648 | 1,648 | 1,151 | **1,151** | **−497** | 2 → 2 | 1,390 → 1,119 |
| `XMEM.DRV` | 1,568 | 1,568 | 1,014 | **1,014** | **−554** | 2 → 1 | 1,317 → 966 |
| **all ten** | 89,258 | 89,517 | 68,169 | **68,434** | **−21,083** | | 60,344 → 53,192 |

**−21,083 bytes of driver claim against C**, and **91 → 73 KB** of claims
if all ten are mounted at once. `elendilon`'s round took 21,089 out
(89,258 → 68,169); `main`'s Covox put 259 into `SOUND.DRV`, which lands as
252 on the passed driver; the review fixes put 13 back into `RAMDISK.DRV`.

- **`RAMDISK.DRV`'s −3,692 is not all removed.** Its pass took 1,419
  (8,952 → 7,533); `5106137` then moved the 96-row directory, 2,304 bytes,
  out of the image and into the claim Mount already makes. An idle driver is
  6 KB where it was 9, and a mounted 64 KB disk costs the same 9 KB it
  did after the pass, split 6 + 3 instead of 8 + 1 (`5106137`'s own figures).
- **`NET.DRV`'s commit quotes its bss twice.** `88d039e` says *"6,730 ->
  4,654 bytes of image+bss"*; its own parent and itself measure **6,650 →
  4,590** (image 6,570 + 80 of bss, then 4,526 + 64). The quoted figures are
  each exactly one more bss on top. Its claims (7 → 5 KB) are right, and so
  is everything after it: the wire zone's packed icon (`63b0301`) takes
  **292** off both `NET.DRV` and `ETHER.DRV`, as its commit says.
- **`VMMOUSE.DRV`** (`kern_emu` only) was not measured; its commit quotes
  521 → 357.

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved (`.text` / `.cold` /
`.bss`; `.lowbss` moved in no file):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `fdlg.inc` (with `os88ui.inc`'s kernel copy) | 0 / **−388** / −3 | 0 / **−304** / −3 |
| `filecp.inc` | 0 / **−342** / −12 | 0 / **−170** / −12 |
| `diskw.inc` | 0 / −9 / 0 | 0 / −9 / 0 |
| `hiber.inc` | 0 / −2 / −6 | — |
| `driver.inc` | 0 / +6 / +2 | 0 / +2 / 0 |
| `files.inc` | 0 / +3 / 0 | 0 / +3 / 0 |
| `mod.inc` | 0 / +1 / 0 | — |
| `kernel.asm` | +4 / 0 / 0 | — |
| `ctrl.inc` | — | −1 / 0 / 0 |

Every other file, `wm.inc` and `desk.inc` included, is byte-identical at A
and B on both kernels.

## The final bill

**What the PR asks `main` to take, on the kernel**, with B the PR:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #230 squash (A) and at `main`'s tip (C) | 99,840 | 65,536 |
| `KERN_SIZE` at the PR tip (B) | **99,328** | **65,024** |
| **change** | **−512** (one rung) | **−512** (one rung) |
| resident bytes, against A and against C | **−746** | **−494** |
| overlay bytes | 0 | 0 |
| heap a machine gets back | **512** | **512** |
| spare of the budget | 29,696 → **30,208** (58 → 59 steps) | 41,984 → **42,496** (82 → 83 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 49,456 → **49,441**: 16,080 → **16,095 left** | 36,024 → **36,008**: 29,512 → **29,528 left** |
| guard 5 (boots on `MIN_RAM_KB`) | 91,648 → **92,160** before it cannot boot on 196KB | 56,320 → **56,832** on 128KB |
| on-demand modules, against C | `CTRL.DRV` −1,095, `HIBER.DRV` −1,173, `CLONE.DRV` −862, `EXTD.DRV` −543, `DOCK.DRV` −269, `FORMAT.DRV` −162 | `CLONE.DRV` −2,008, `CTRL.DRV` −1,372, `FILECP.DRV` −297, `FORMAT.DRV` −162, `FDLG.DRV` −73 |
| the ten loadable drivers, against C | **−21,083** of claim, 91 → 73 KB | the same drivers |

**In one line each:**

- **`main`** spent **no resident byte on either kernel**: #229's Covox is
  `CTRL.DRV` +286 (+258) and `SOUND.DRV` +259, and its other twelve commits
  are packages, disks, gates and skills. #230's own review fixes, which
  landed in A, are +227 / +15 resident and have not had a pass.
- **We** took **748 resident bytes out of `kern_big` and 494 out of
  `kern_small`**, all of it in the module glue and `os88ui.inc`'s kernel
  copy, and put back 3 for one fix; the squash cost 2 more. Every module
  and every driver shrank, the drivers by 21,089 bytes of claim. Every
  quoted module and section figure was checked against its own commit, and
  one driver figure was found double-counted.
- **Together**, the PR tip is **512 bytes and one rung smaller than the #230
  squash on each kernel**, and `kern_small`'s `KERN_SIZE` is under 64 KB for
  the first time in this family of reports.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s `small` and `emu` baselines were A's
  predecessor, not D's.** The squash re-blessed `big` only, so every
  `make small` at B printed `sum +15` and every `make emu` `sum +229` with
  *"the cold rung CROSSED"*: exactly #230's review fixes, which `elendilon`'s
  baselines had never seen. **Closed in the same commit as this file**:
  `small` and `emu` are re-blessed at B, and `kernsize` reads +0 on all
  three.
- **The squash's commit message mis-attributed its own +229.** It says the
  delta over `elendilon`'s blessed baseline is *"main's #230 review fixes and
  the Covox page"*. It is #230's review fixes (+227) and the squash's
  residual (+2); the Covox page is `CTRL.DRV`'s and moves no resident byte.
- **The squash cost 2 bytes it need not have** (`fdlg_grab`'s tail, above).
- **`NET.DRV`'s size-pass commit double-counts its bss** (above). No other
  driver quote was checked per commit: the drivers were measured at the
  endpoints only.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`. `.lowbss`,
`.vgabuf` and the overlay moved in no commit, and `kern_small`'s `.text` and
`.bss` only where its resident column differs from its `.cold` one. #230's
review rows are in A and are listed for the chain; the rest sum, with the
squash residual, to B − A exactly. The `KERN_SIZE` columns do not, which is
the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | **big resident** | big `KERN_SIZE` | small `.cold` | **small resident** | small `KERN_SIZE` | modules (image) |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `7cd330e` | **#230 review:** Standard File chooser (SPEC.md 38.2, 38.6, 38.8): nothing reaches a chooser that has en... | +1 | +2 | +70 | **+73** | 0 | +13 | **+15** | 0 | small FDLG +43 |
| `3b503fd` | **#230 review:** Disk window arrows and gfx_blit1 (SPEC.md 22.26, 5.4.2.8): an arrow move shuts the doub... | 0 | 0 | +7 | **+7** | 0 | 0 | **0** | 0 |  |
| `e890eb7` | **#230 review:** wm/desk: a zone or an L that would overflow the dither's region has an answer of its ow... | +18 | 0 | +129 | **+147** | 0 | 0 | **0** | 0 |  |
| `67237c0` | **#230 review:** Floppy page Cylinder test (SPEC.md 31.14): an erroring crossing refuses, one toast, a f... | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CTRL +39 |
| `fe9a559` | **main:** Covox Speech Thing: SOUND.DRV finds the LPT ports, the Sound page picks one, Tracker/MI... | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CTRL +286, small CTRL +258 |
| `bfa28cf` | files: a shed Disk window re-claims its store BEFORE the re-list names it | 0 | 0 | +3 | **+3** | 0 | +3 | **+3** | 0 |  |
| `7bd0846` | FORMAT.DRV: image 1,298 -> 1,150 bytes, file 1,220 -> 1,109 (size pass) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | FORMAT −148, small FORMAT −148 |
| `330adaa` | DOCK.DRV size pass: image 2,515 -> 2,256 bytes (-10.3%), file 2,235 -> 2,047 | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | DOCK −259 |
| `359f28b` | HIBER.DRV size pass: image 6,662 -> 5,499 bytes, claim 7KB -> 6KB | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | HIBER −1,163 |
| `30cf3bd` | FILECP: size pass - image 2,237 -> 1,898, claim 3KB -> 2KB; kern_big .cold -344 | 0 | −12 | −344 | **−356** | −512 | −20 | **−32** | 0 | small FILECP −339 |
| `dcc0c27` | FILECP: fcp_claim carries the cluster span in DX across the second fcp_goto | 0 | 0 | +2 | **+2** | 0 | 0 | **0** | 0 | small FILECP +2 |
| `bb63cb0` | CTRL.DRV size pass 1: image 12,220 -> 11,649 (kern_big), 5,735 -> 5,347 (kern_small) | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CTRL −571, small CTRL −388 |
| `b066f93` | EXTD.DRV size pass: image 1,566 -> 1,069 bytes (-31.7%), no resident byte | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | EXTD −497 |
| `c54f4a6` | FDLG: size pass on the Standard File dialog's glue (SPEC.md 38.13.1) | 0 | −3 | −109 | **−112** | −512 | −15 | **−18** | 0 | small FDLG −122 |
| `12756a6` | Merge FDLG size pass (image 1,286 -> 1,164; kern_big .cold -109) into kernel-side-size-p1 (merge residual) | 0 | 0 | 0 | **0** | +512 | 0 | **0** | 0 |  |
| `90ed414` | CLONE.DRV size pass 1: helpers for the cloner's and compressor's repeated shapes | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CLONE −492, small CLONE −490 |
| `50ef927` | CLONE.DRV size pass 1b: fix cmz_pack's state block, two more shared shapes | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CLONE −30, small CLONE −30 |
| `4fa17c9` | os88ui size pass: About card, glyph, button body, gesture, scroll bar | 0 | 0 | −259 | **−259** | −512 | −245 | **−245** | 0 |  |
| `9eed89a` | os88ui size pass 2: check box, radio, drop box, alert; picture and bfind gates | 0 | 0 | −8 | **−8** | 0 | −8 | **−8** | 0 |  |
| `e74054a` | drv_find asks the FILE when a driver has no hint: one dskw_czknow judgement (SPEC.md 20... | 0 | 0 | −4 | **−4** | 0 | −4 | **−4** | 0 |  |
| `553c50c` | hiber: delete hb_wakep - written at step 4, read by nothing (SPEC.md 87.4) | 0 | −4 | 0 | **−4** | 0 | 0 | **0** | 0 | HIBER −10 |
| `adb7a1e` | FILECP.DRV calls out through fcpx_go, not 34 resident xf_ thunks (kern_small) | 0 | 0 | 0 | **0** | 0 | −146 | **−146** | −512 | small FILECP +40 |
| `6b6bdd6` | FORMAT's four resident stubs share one refusal; two HIBER stubs share a ret | 0 | 0 | −9 | **−9** | 0 | −7 | **−7** | 0 |  |
| `d253bd2` | FORMAT stubs: the refusing prologue is dskw_fmt_go, and dskw_fmt_load keeps its CF cont... | 0 | 0 | +4 | **+4** | 0 | +4 | **+4** | 0 |  |
| `4fcd708` | EXTD.DRV: 1,069 -> 1,022 bytes, its claim 2 KB -> 1 KB (+4 .text) | +4 | 0 | 0 | **+4** | 0 | 0 | **0** | 0 | EXTD −47 |
| `a843102` | DOCK.DRV: theme pens as near copies, 2,256 -> 2,246 bytes | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | DOCK −10 |
| `15ca9c3` | FORMAT.DRV: the reach test's marker is the LBA thrice, 1,150 -> 1,136 bytes | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | FORMAT −14, small FORMAT −14 |
| `0daf361` | EXTD.DRV: keep OSAPI_WM_DISPLAY and the CAPS worker slice no worse, 1,023 bytes | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | EXTD +1 |
| `a76522b` | CTRL.DRV: cpc_buf is module bss on kern_big too - mod_need claims the whole image for M... | 0 | 0 | +1 | **+1** | 0 | 0 | **0** | 0 | CTRL −131 |
| `fb9cae6` | CTRL.DRV and CLONE.DRV each a KB smaller claim on kern_big and kern_small | 0 | 0 | 0 | **0** | 0 | 0 | **−1** | 0 | CLONE −337, CTRL −399, small CLONE −1,485, small CTRL −984 |
| `b05b601` | FDLG.DRV calls out through fdx_go; the dialog's size is dskw_czknow's | 0 | 0 | −10 | **−10** | 0 | −40 | **−40** | 0 | small FDLG +44 |
| `4750cee` | **review:** clone: clo_geom sets the two-head default AFTER AH=08h, not before | 0 | 0 | 0 | **0** | 0 | 0 | **0** | 0 | CLONE −3, small CLONE −3 |

