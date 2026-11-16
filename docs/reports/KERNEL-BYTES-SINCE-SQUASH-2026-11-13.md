# Kernel bytes since the #345 squash, by concept

**A measurement, not a description.** Taken 2026-11-13 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json --strict`, which RE-ASSEMBLES the kernel rather than
reading a build. Every commit of the round, and every parent it names, was
measured in a clean tree of its own: `git archive` of that commit's
`kernel/`, `boot/`, `drivers/`, `tools/`, Makefile and the top-level files of
`apps/`, that commit's `apps/stiklib.asm` assembled there into the
`stiklib.bin` that `deskmod.inc` includes, and that commit's own `kernsize`
measuring **`kern_big`, `kern_small`, `kern_emu` and `kern_mega`**. A tree
whose kernel inputs hash the same as one already measured takes that tree's
figures: **340 points, 170 distinct kernel trees, and every one assembles on
all four kernels but one merge on `kern_mega`** (below). `associco.inc`,
`buildnum.inc` and `ksig.inc` are B's in every tree, each kernel's own: the
build number is held (`BUILD_STR` is the commit count, SPEC.md 14.2) and
`KSIG_OFF` is a canary's file sector, not a byte of any section. Module and
driver images come from `make`, `make small`, `make emu` and `make mega` at
the two endpoints only. It is true of the commits it names and of no other
tree. The clean-tree figures for B were checked against `kernsize` on B's
real `make small` and `make mega` builds: identical.

It is the nineteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-11-09.md`, and is an edit of none
of them. #345 is squashed (`08f0c7ec`), so this round starts a new PR and the
question is the 2026-11-07 report's again: **what does this round cost
`main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `117cd5b8` | **`main`**: #349, *Release 20261109: kern_mega's ARCADE disks in the zip*, on #347, #346 and the #345 squash `08f0c7ec` |
| **E** | `5a2294d5` | `origin/elendilon`: `9adfc9f8` (*Merge fix-flaky*) and one CLAUDE.md/AGENTS.md commit |
| **B** | `314f0c82` | **`elendilon-pr`**: `elendilon` at `9adfc9f8`, then three CLAUDE.md/AGENTS.md commits. B, E and `9adfc9f8` have **one kernel-input hash**: B's kernel is `elendilon`'s |

**Where the round starts on `elendilon`.** After #345 was squashed,
`elendilon` was cut again from `main` rather than merged with it: its first
commit past the squash, `b0670609` (*CLAUDE.md, AGENTS.md: elendilon's map*),
has **one parent, `117cd5b8`**, and every commit of the round descends from
A. The old line (the 2026-11-09 report's E, `3cfb2457`, and #345's review
fixes that the squash carries) is not an ancestor of anything here; it is
kept on `origin/elendilon-old`. So the round is **`117cd5b8..314f0c82`: 339
commits, 71 of them merges**, with A as its only boundary, and **`B − A` is
the round's own work**.

**`main`'s own commits since #345 move no kernel byte.** `08f0c7ec` (#345),
`721aaa9b` (#346, the sound debug disk), `78718d75` (#347) and `117cd5b8`
(#349) measure **identically on all four kernels**. #346 adds
`drivers/sound/snddiag.inc` and its hooks in `sb.inc`, but they are empty
unless `-DSNDDIAG` (the debug disk's own driver), and SOUND.DRV assembled at
`08f0c7ec` and at `117cd5b8` is **byte-identical**. All three are inside A, so
none of them is in `B − A`; unlike the 2026-11-07 round there is no merge of
`main` to take out.

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M − M^1 −
Σ(every in-round commit M brings in)`. **The commits and the merges sum to
`B − A` exactly, in every section, on all four kernels** (checked by the
script, not assumed).

**One tree does not assemble**: `kern_mega` at the merge `a6fc912a` (*Merge
ksp19-drvtab*), `kernel.asm` guard 6, *the heap base is not 512-byte
aligned*. Its only child `aa5c07f5` (*kern_mega's image rounds to 512 again*)
is the fix. The merge takes its first parent's figures on `kern_mega`, so the
pair is one step: the driver tables' −212 lands on `aa5c07f5`, the merge's
+212 and the fix's −212 cancel, and `0db247e0`'s own −212 is the row.

On `kern_big` and `kern_emu` **68** of the round's commits move a resident
byte and no merge does; on `kern_small` 58 commits and 2 merges; on
`kern_mega`'s conventional sections 61 commits and 4 merges, and 5 commits
move `.mega`.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The round SAVES 1,233 resident bytes on `kern_big` and `kern_emu` and 746
on `kern_small`. `KERN_SIZE` falls two 512-byte rungs on `kern_big` and
`kern_small` and three on `kern_emu`.** Kernel size passes 19 and 20 took
1,694 bytes off `kern_big`; speed-and-fix and the module cache spent 467 of
them.

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 39,976 | 39,199 | **−777** | 29,680 | 28,952 | **−728** | 40,222 | 39,445 | **−777** |
| `.bss` | 5,378 | 4,878 | **−500** | 2,948 | 2,831 | **−117** | 5,378 | 4,878 | **−500** |
| `.cold` | 37,821 | 37,677 | −144 | 21,037 | 21,136 | +99 | 37,821 | 37,677 | −144 |
| `.vga` | 2,030 | 2,218 | **+188** | - | - | | 2,030 | 2,218 | **+188** |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 90,899 | **89,666** | **−1,233** | 56,533 | **55,787** | **−746** | 91,145 | **89,912** | **−1,233** |
| `.ovl` (window) | 3,160 | 2,050 | **−1,110** | 1,474 | 1,472 | −2 | 3,160 | 2,050 | **−1,110** |
| `.boot2` (blob) | 3,362 | 3,206 | −156 | 2,936 | 3,010 | +74 | 3,377 | 3,221 | −156 |
| `.boot3` (0 resident) | 6,148 + 860 nobits | 7,728 + 720 nobits | **+1,580** | 2,388 | 2,400 | +12 | 6,244 + 764 nobits | 7,840 + 864 nobits | **+1,596** |
| **`KERN_SIZE`** | 96,256 | **95,232** | **−1,024** | 58,368 | **57,344** | **−1,024** | 96,768 | **95,232** | **−1,536** |
| spare of the budget | 4,608 | **5,632** | | 5,632 | **6,656** | | 4,096 | **5,632** | |

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---:|---:|---:|---:|
| **Kernel size pass 19, the flag day**: `PKG_FMT` 10 → 13, `DRV_VER` 12 → 14 (merge `2cf57256`; `docs/plans/completed/ksp19/`) | 25 movers | **−1,280** | **−712** | **−1,280** |
| **Kernel size pass 20** (merge `d112a6ee`, in `20cba9e3`; `HANDOFF-KERNEL-SIZE-P21`) | 30 movers | **−414** | **−511** | **−414** |
| **The damage pass**: `wm_paint_all` over the whole desktop (`4f94d792`), `files_refresh` retired (`930738c3`) | 2 | **−156** | **−146** | **−156** |
| **The four VGA `KHP_*` knobs on `kern_big` and `kern_emu`** (`5788a036` +214; its seven size passes −26) | 9 | **+188** | 0 | **+188** |
| **Disk read-ahead prefetch** (`48ba64c5` +138, `258de415` +28, `2649a396` +10; SPEC.md 18.95.14) | 3 | **+176** | **+176** | **+176** |
| **The module cache**: a dropped module stays as a purgeable cache (`a294d3f2` +104, `211c0242` −20), survives compaction (`4abfb3ae`, `e94899f7`), every module kept (`74fdcfd8` +34; SPEC.md 2.8.3) | 5 | **+128** | 0 | **+128** |
| **kern_small's caches**: a claim evicts them (`a6d71662` +224), then the icon store and listings MOVE instead (`c3eb4ec4` +96), the read-ahead too (`9f5c3c9c` +6), the parity pad (`c0eedd5f` +1) and the merge with size pass 20 (`20cba9e3` +1; SPEC.md 50.6.13) | 4 + 1 merge | 0 | **+328** | 0 |
| **The BR fixes**: BR-60 a full subdirectory grows (+69), BR-11 (+17), BR-16 (+10), BR-17, BR-2 (`kern_small` +8), BR-4 (−14) | 6 | +81 | +106 | +81 |
| `OSAPI_FILE_FIND_SEQ` (`63330646` +31) and the V1 line cells (`d350e5fa`, `bff19461`, `3c3fbd7b`) | 4 | +46 | +19 | +46 |
| **`gfx_points` spends the deferred cursor hide only unarmed** (`782c6375`, fix-misc, SPEC.md 7.1.4) | 1 | **−6** | **−6** | **−6** |
| the other ~250 commits and 65 merges (hibernate without a dialog, MDA/MCGA at boot, Paint, tests, tiers, docs) | | 0 | 0 | 0 |
| **the round, against A** | | **−1,233** | **−746** | **−1,233** |

**Size pass 19's −1,280** is the flag day: every rare cell behind the
`int 81h` door, the table renumbered once (`c36f4401` −78, `cd54fdb5` −352,
together −430); the window class in the package and one `OSAPI_WM_FLAG` door
(`1212c463`, −406, `.bss` −288 of it); every driver class's service table read
in place (`0db247e0`, −229, `.bss` −199); the memory and file doors and every
claim in paragraphs (`41ceb2dd` −141, `d55ca8d7` +50 for C1's KB arm,
`1d51b331` −50); and nine sweep commits (−75), with `ca265c04` (+1) a fix
that rode in. On `kern_small` it is −712: the door −373, the class −196 (−2
of it the `385d14c7` merge), the doors −97, the sweeps −46.

**Size pass 20's −414** is the Disk window (`files:`, −130), the read-ahead's
code (`disk:`, −93), `sys:` (−67), `wmui:` (−63), `draw:` (−57) and
`sched_mode_get` gone (−4). `kern_small` takes −511 and the rung the merge
names.

**The VGA knobs' +188 is all `.vga`**, the rung VGA and EGA machines pay
and a mono machine slides `.cold` over (SPEC.md 2.10.9). `kern_mega` already
carried them, so they cost it nothing here.

**kern_small's caches** are the largest spend on `kern_small`: the first
version evicted the icon store and listings when a claim landed on them
(+224); moving them instead kept what the user had (+96, +6). This is what
leaves `kern_small` 21 bytes short of its next rung (below).

**The boot-only homes.** Hibernate B2 probes before the mouse and the drivers
from `.boot3` instead of the overlay window (`23f7e1c6`: `.ovl` −1,122,
`.boot3` +1,264), and hibernate A and B1 add +112 there; `font_init` went back
to `.boot3` (`5001f067`: `.boot2` −236, `.boot3` +240) to give the blob room
for telling a plain MDA from a Hercules (`6462ea6d`, +72) and an MCGA from a
VGA (`e31381b1`, +8). None of it is resident.

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 45,728 | 44,792 | **−936** |
| `.bss` | 5,416 | 4,916 | **−500** |
| `.cold` | 48,583 | 48,576 | −7 |
| `.lowbss` | 5,600 | 5,600 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| conventional sections (the five above) | 105,663 | **104,220** | **−1,443** |
| `.mega` | 24,704 | 24,752 | +48 |
| `.mega`'s scratch | 1,510 | 1,510 | 0 |
| `.ovl` | 3,156 | 3,168 | +12 |
| `.boot2` (blob) | 3,281 | 3,123 | −158 |
| `.boot3` (0 resident) | 5,941 + 715 nobits | 6,209 + 959 nobits | +268 |
| **`KERN_SIZE` at boot** | 137,728 | **136,704** | **−1,024** |
| **resident once switched** (`MEGA_RESIDENT`, the budgeted figure) | 62,464 | **61,440** | **−1,024** |

| concept | conventional | `.mega` |
|---|---:|---:|
| **Kernel size pass 19**: the door −436, the class −498, the driver tables −212, the doors −205, the sweeps −171 (`6228a829`'s dead KB-DMA pool head −100), megacmp putting the pool DMA head back (`7f7ebc06` +137, merge `150cce3b` −21), `ca265c04` +1 | **−1,405** | −16 |
| **Kernel size pass 20** (−386 in commits, −27 in merges: `d112a6ee` −25 `.cold` onto the module-cache parity, `c4e7d568` −2) | **−413** | 0 |
| **The damage pass** | **−188** | 0 |
| the VGA knobs' size passes (`kern_mega` already had the knobs) | −16 | 0 |
| **The module cache on `kern_mega`**: dropped modules kept as purgeable pool pairs, prefetch and the kept scan (`5d1ced81` +379), below 1MB too (`206b5dfb` +53), `4abfb3ae` +3 | **+435** | +64 |
| the BR fixes | +83 | 0 |
| `FILE_FIND_SEQ` and the line cells | +47 | 0 |
| read-ahead: a speculative read tried once (`2649a396`) | +10 | 0 |
| hibernate (A): no Resume/Discard question (`ce54288b`) | +10 | 0 |
| `gfx_points`' cursor hide | −6 | 0 |
| **the round, against A** | **−1,443** | **+48** |

What a running `kern_mega` keeps is **1,024 bytes less**. The 64 bytes `.mega`
gained are the module cache's protected-mode half, above 1MB once switched.

What binds `kern_mega` at B:

- **the budget**: resident 61,440 of 68,096, **6,656 spare** (5,632 at A);
- **the 64 KB segment**: `.text`+`.bss` 49,708, **15,828 left** (14,392 at
  A). `kern_mega` carries **3,413 bytes beyond `kern_big`** there (3,760 at A),
  and `tests/megaseg.txt` was ratcheted from 3,760 to 3,555 in the round, so
  it has **142 bytes of headroom** again;
- **above 1MB**: `.cold` 48,576 + `.mega` 24,752 + scratch 1,510 = 74,902 of
  the code area's 261,120-byte cap, 186,218 left (186,266 at A).

## Where the bytes are, by file

`kernsize --modules`, A → B, `.text` / `.cold` (and `.bss` where it moved), every
row of 10 bytes or more on any kernel. `kernel.asm`'s own code is bracketed
under `boot3.inc` (it is %included twice, SPEC.md 2.9.15), so that row is
`kernel.asm` + `boot3.inc`: the API table, which the rare-cell door
shortened. `.vga` is not a column of the table (`vga12.inc`'s `.vga` is the
+188 above).

| file | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|
| `kernel.asm` + `boot3.inc` | **−754 / 0** | **−740 / +4** | **−787 / 0** |
| `wm.inc` | **−175 / 0**, `.bss` −288 | −149 / 0, `.bss` −108 | **−189 / 0**, `.bss` −288 |
| `files.inc` | +26 / −182, `.bss` −6 | +24 / −209, `.bss` −6 | +26 / −182, `.bss` −6 |
| `driver.inc` | −13 / −29, `.bss` −199 | 0 / −3 | −13 / −15, `.bss` −199 |
| `loader.inc` | 0 / −61 | 0 / −44 | 0 / −68 |
| `memory.inc` | +10 / −64 | **+22 / +142** | +6 / −51 |
| `menu.inc`, `instance.inc`, `desk.inc` | −30/−8, −31/−3, 0/−31 | −28/−8, −25/−3, 0/−24 | −30/−8, −31/−3, 0/−31 |
| `vga12.inc` (`.text` / `.cold`) | −33 / +2 | −44 / 0 | −37 / +2 |
| `sched.inc`, `icons.inc`, `ui.inc`, `assoc.inc` | −22/0, −13/0, −13/0, 0/−12 | −29/0, −16/0, −5/0, 0 | −22/0, −13/0, −13/0, 0/−12 |
| `mega.inc`, `blank.inc`, `mouse.inc` | -, 0/−6, −6/0 | -, −4/0, −4/0 | **−66/−8**, −32/−6, −22/0 |
| `ctrl.inc`, `hiber.inc` | +14/−5, +14/−1 | +8/−15, 0 | +14/−5, +14/−1 |
| `diskw.inc` | 0 / +28 | 0 / +62 | 0 / +28 |
| `mod.inc` | 0 / **+107** | 0 / **+101** | 0 / **+176** |
| `disk.inc` | +1 / **+139** | 0 / +96 | +2 / **+186** |
| `fsx.inc` | **+261 / −12** | **+279 / −12** | **+270 / −12** |

`fsx.inc`'s +261 is not a feature: the size pass's door and sweeps moved
shared tails into it, and `kernel.asm` + `boot3.inc` is the other side of the
same moves. `mod.inc` and `disk.inc` are the module cache and the read-ahead
prefetch; `memory.inc` on `kern_small` is the cache move.

## How close B stands to the next rung

From size pass 19 (`5b542b10`, *the image pad goes*) `KIMG_PARA` rounds to the
paragraph, so `kern_big`, `kern_small` and `kern_emu` have ONE kernel rung
where A had an image rung and a top rung.

| | rungs |
|---|---|
| `kern_big` at A | image 214 left, top 23 left |
| `kern_big` at B | kernel **446 left** |
| `kern_small` at A | image 140 left, top 159 left |
| `kern_small` at B | kernel **21 left** (491 of 512 accrued) |
| `kern_emu` at A | image 480 left, top 23 left |
| `kern_emu` at B | kernel 200 left |
| `kern_mega` at A | image 56, cold 57, low 32, vgabuf 176, `.mega` 410 |
| `kern_mega` at B | image 468, cold 64, low 32, vgabuf 176, `.mega` 362 |

Per CLAUDE.md, that decides WHEN a machine pays and never what a change cost.
**But `kern_small`'s 21 bytes are close to a hard line**, not just a rung:

**`kern_small`'s ladder at B is 57.5 KB** (`HEAP 0x0e60`; 58.5 KB at A), so
the 128 KB machine keeps **70.5 KB of heap after boot**, which is the floor
CLAUDE.md now sets. The next 22 resident bytes on `kern_small` take
`KERN_SIZE` to 57,856 and the ladder to 58.0 KB, under that floor.
`kern_big`'s ladder is 94.5 KB (95.5 at A), `kern_emu`'s 94.5 (96.0) and
`kern_mega`'s heap starts at `0x21c0` at boot and `0x0f40` after the switch
(`0x2200` and `0x0f80`).

**Guard 5**, before a kernel cannot BOOT, is unchanged on every kernel:
`kern_big` 19,968 on 128 KB, `kern_small` 59,392, `kern_emu` 19,456 and
`kern_mega` 499,712 in 640 KB. The boot ceilings (`bootmax`) fell by exactly
what `KERN_SIZE` fell, because `.boot3` grew into the room: hibernate's probe
and `font_init` are boot-time memory, and the blob's lift counts them.

## The on-demand modules and the drivers

As `make`, `make small`, `make emu` and `make mega` built them at A and B.
**A module's claim is its image** (`MOD_H_IMG`: the file's whole size, the
bss's zeros included, `mod.inc`); a driver's is its image plus the bss
paragraphs at header byte +31. Every module and driver not listed is
byte-identical. **None crosses a 512-byte claim rung.**

| image | kernel | A | B | **B − A** |
|---|---|---:|---:|---:|
| `CTRL.DRV` (module) | `kern_big`, `kern_emu` / `kern_mega` / `kern_small` | 15,405, 15,423 / 15,588 / 5,658 | 15,459, 15,477 / 15,649 / 5,671 | +54 / +61 / +13 |
| `DESK.DRV` (module) | `kern_big`, `kern_emu`, `kern_mega` | 4,520 / 4,776 | 4,432 / 4,688 | **−88** |
| `HIBER.DRV` (module) | `kern_big`, `kern_emu` / `kern_mega` | 7,795 / 11,630 | 7,715 / 11,581 | −80 / −49 |
| `STIKMENU.DRV` (module) | `kern_big`, `kern_emu` / `kern_mega` | 7,876 / 8,326 | 7,926 / 8,395 | +50 / +69 |
| `EXTD.DRV` (module) | big, emu / mega | 1,955 / 1,890 | 1,927 / 1,862 | −28 |
| `DISKOPS.DRV` (module) | big, emu / mega / small | 10,236 / 11,411 / 7,201 | 10,216 / 11,395 / 7,195 | −20 / −16 / −6 |
| `DOCK.DRV` (module) | big, emu / mega | 2,252 / 2,323 | 2,232 / 2,303 | −20 |
| `SERIAL.DRV` (module) | all four | 909 (mega 1,000) | 919 (1,010) | +10 |
| `FDLG.DRV`, `FILECP.DRV`, `FORMAT.DRV` (modules) | `kern_small` | 1,600, 1,871, 1,279 | 1,590, 1,876, 1,280 | −10, +5, +1 |
| `WIN95.DRV`, `FSCHECK.DRV` (modules) | big, emu, mega | | | +4, +2 |
| `FORMAT.DRV` (module) | big, emu, mega | 3,179 / 3,432 | same size | 0 (image differs) |
| `SOUND.DRV` | all | 10,517 | 10,478 | **−39** |
| `USBMOUSE.DRV` | `kern_big` | 1,435 | 1,521 | +86 |
| `RAMDISK.DRV`, `EMS.DRV`, `PCIETHER.DRV` | `kern_big` | 5,443, 749, 17,053 | 5,400, 785, 17,076 | −43, +36, +23 |
| `HTTPFS.DRV`, `NET.DRV`, `CDROM.DRV`, `THMPAL.DRV`, `ETHER.DRV`, `VMMOUSE.DRV`, `SAVER.DRV`, `XMEM.DRV`, `HDDTOOL.DRV` | `kern_big` | | | −22, −14, −12, −11, +10, +13, −6, −4, −2 |
| `HDD.DRV`, `VESA.DRV`, `RAMPAGE.DRV`, `HTTPPAGE.DRV` | `kern_big` | | | 0 (image differs) |

Every module grew a header word in the round (`MOD_H_SCR`, the module cache's
scratch tail, SPEC.md 2.8.3), so +2 of each module's figure is that; every
driver and module image also moved with the renumbered table (`PKG_FMT` 13,
`DRV_VER` 14).

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` on `main` (A) | 96,256 | 58,368 | 96,768 | 137,728 at boot, 62,464 resident |
| `KERN_SIZE` at B | **95,232** | **57,344** | **95,232** | **136,704** at boot, **61,440 resident** |
| resident bytes, this round | **−1,233** | **−746** | **−1,233** | **−1,443** conventional, `.mega` +48, **−1,024 once switched** |
| ...of which size passes 19 and 20 | −1,694 | −1,223 | −1,694 | −1,818 |
| ...of which everything else | +461 | +477 | +461 | +375 |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | 68,096 |
| spare | **5,632** (11 steps) | **6,656** (13 steps) | **5,632** (11 steps) | **6,656** (13 steps) |

**`make budget-check` passes at B on all four**, with no grace: 5,632,
6,656, 5,632 and 6,656 spare. B could go to `main` as it stands.

**In one line each:**

- **We** saved **1,233 bytes on `kern_big` and `kern_emu`** and **746 on
  `kern_small`**: size passes 19 and 20 took 1,694 off `kern_big` and paid for
  the VGA knobs (+188), the read-ahead prefetch (+176), the module cache
  (+128), the BR fixes and `FIND_SEQ`, and the damage pass saved 156 more.
- **`kern_small`** spent 328 bytes on keeping its caches and sits **21 bytes**
  from the 58.0 KB ladder that would break the 70.5 KB heap floor.
- **`kern_mega`** keeps **1,024 bytes less** once switched, after paying 435
  for the module cache there.
- **Modules and drivers** moved by tens of bytes, none across a claim rung.

## Found on the way

- **The last two reports' module rows read header byte +31 as bss, and a
  module has none there.** +31 is the bss paragraph count of a DRIVER
  (`tools/os88drv.py`); a module's image already holds its bss as zeros
  (`MOD_H_IMG`) and +31 is inside its entry table: the high byte of an entry
  offset. So the 2026-11-09 report's `kern_mega` `HIBER.DRV` 13,278 is image
  11,630 plus 1,648 bytes of an offset, and its `CTRL.DRV` 15,940 is 15,588
  plus 352. The 2026-11-07 report's driver rows (`SOUND.DRV`, `PCIETHER.DRV`,
  `ETHER.DRV`) are right. It showed this round because `MOD_H_SCR` moved the
  entry table two bytes along: the same reading gave `DOCK.DRV` **+2,972**,
  `WIN95.DRV` +2,084 and `kern_mega`'s `STIKMENU.DRV` −2,123, none of it real.
  The table above reads modules by their image.
- **`kern_mega` does not assemble at `a6fc912a`** (guard 6), fixed by the
  next commit (above). Nothing ships from that tree; a bisect over this range
  will land on it.
- **Two merge subjects do not match what they measure against their first
  parent.** `2cf57256` says *kern_mega conventional −1,138* and measures
  **−1,405**; `d112a6ee` says *kern_mega −388* and its side measures −386 in
  commits plus −27 in merges. `kern_big`'s and `kern_small`'s figures match
  the subjects to the byte (−1,281 vs −1,280 is `ca265c04`'s +1 riding in).
- **docs/KERNEL-MEMORY.md's baselines described A, not B** (`kern_big`
  `.text` 39,978, two bytes off A's 39,976), so `kernsize` at B printed the
  whole round as its delta. **All four re-blessed at B in the same commit as
  this file**, with the module and theme tables.
- **The fast tier is 44 rows at B against 77 at A**, all passing: the tier
  lock (`07596d0b`) and two retiering commits (`48b8dabd`, `f24016bb`) moved
  rows to soak with the owner's reasons. `make` builds B cleanly, and so do
  `make small`, `make emu` and `make mega`.
- **The ABI moved by decision**: `PKG_FMT` 10 → 13 and `DRV_VER` 12 → 14 at
  size pass 19's flag day, with `VX_ABI_VER` 5, `XM_ABI_VER` 2 and
  `HD_ABI_VER` 7 newly locked (no bump: byte-identical images). Every later
  lock entry in the round is an addition without a bump.
