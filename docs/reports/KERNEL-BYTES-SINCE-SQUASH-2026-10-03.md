# Kernel bytes since the #214 squash, by concept, with `main`'s arm separated

**A measurement, not a description.** Taken 2026-10-03 on a four-core cloud
container: `nasm` 2.16.01, Python 3.11.15. Every resident figure comes from
`tools/kernsize.py --json` and `--modules`, which RE-ASSEMBLE the kernel
rather than reading a build. Each of the **109 trees** below was measured in a
clean worktree of its own commit, with that commit's own `kernsize` and its
own generated includes (`buildnum.inc`, and `associco.inc` built the way
`make` builds it for `kern_big` and `make small` for `kern_small`). Module
images come from each point's own `make` of `kernel.bin` on both kernels. No
figure is carried between points. It is true of the commits it names and of
no other tree.

It is the seventh of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-07.md`,
`docs/reports/PR-CYCLE-ACCOUNTING-2026-09-11.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-17.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-25.md`,
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-09-28.md` and
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-01.md`, and is an edit of
none of them. It is taken to open the next `elendilon -> main` PR, so the
question is the PR's: **what does this squash cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `343cb17e` | **the last squash with `main`**: *Elendilon -> Main (PC Speaker Sound with No Card, One Desktop, Streaming Writes, Split Sets Across Floppies, Floppy Page and Low-Level Format, Both Kernels Smaller)* (#214), 2026-10-02 |
| **B** | `08ae9680` | `elendilon` at its tip, 2026-10-03, **+104 commits** over A: 92 that are not merges (9 of them `main`'s) and 12 merges |
| **C** | `277313ae` | **upstream** `main` (`jggonz/os8088`) at its tip, 2026-10-03, **+13 commits** over A: #215, #217, #218, #219, #216, #220, #221, #222, #223, #225, #226, #227 and `277313a` |
| **D** | B merged with C | what the PR carries. **C is NOT yet in B**: `elendilon` merged upstream through #223 (`a4eb738`) and the last four (#225 MIDIRack, #226 the vga-face skill, #227, `277313a` The Wire's 8.3 sidecar names) have landed since. D was built as a trial merge of C into B (one conflict, `CLAUDE.md`, which no kernel reads) and **measures byte-identical to B on both kernels**, so every B figure below is D's |

**The fork's own `origin/main` is not C.** It stops at #215 (`6d1690c`);
upstream has moved twelve commits past it. Every C figure here is upstream's.

**`main`'s #215 is on B's first-parent chain**: the branch was re-cut from
`main` at #215 after the #214 squash, and #216-#223 came in at `a4eb738`.
So A→B contains `main`'s arm minus its last four commits, and those four move
no kernel byte (below).

**The build number contributes nothing to any delta here.** `BUILD_STR` is the
commit count (SPEC.md 14.2): 178 at A, 191 at C, 282 at B, 287 at the trial
D, and between 178 and 282 at every tree measured. Three digits everywhere.

**`associco.inc` is byte-identical in all 109 trees**, on both kernels
(`676544b2...`, the same hash as the previous report's 271). It is the one
generated include a PACKAGE commit could move a kernel byte through. So every
byte below came out of the files a walk of `%include` from
`kernel/kernel.asm` finds, plus `apps/os88ui.inc` and `boot/boot2.asm`.

**`KERN_BUDGET` is 129,536 and `KERN_SMALL_BUDGET` 107,520 at every point.**
Nothing below is a budget move.

**A chains to the previous report in rungs exactly**, at 104,448 and 67,584.
In resident bytes A is **76 heavier on `kern_big`** (`.text` +13, `.cold`
+63) and **12 heavier on `kern_small`** than that report's D (`e165f9d8`),
with `kern_big`'s overlay +16. #214 as merged carries fix commits beyond the
tip that report measured, as #203 did the cycle before. That report said it
would not be edited again, and it was not.

## Headline: `kern_big`, the shipped default

| section | A base | C main | B elendilon = D | **C−A** | **B−A** | **B−C** |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 46,173 | 46,208 | 44,285 | **+35** | **−1,888** | −1,923 |
| `.bss` | 5,297 | 5,301 | 5,150 | **+4** | **−147** | −151 |
| `.cold` | 41,385 | 41,421 | 38,446 | **+36** | **−2,939** | −2,975 |
| `.lowbss` | 5,598 | 5,598 | 5,598 | 0 | 0 | 0 |
| `.vgabuf` | 336 | 336 | 336 | 0 | 0 | 0 |
| **resident** | 98,789 | 98,864 | 93,815 | **+75** | **−4,974** | **−5,049** |
| `.ovl` | 2,489 | 2,489 | 2,470 | 0 | −19 | −19 |
| `.ovlw` | 4,985 | 4,985 | 5,011 | 0 | +26 | +26 |
| **overlay** | 7,474 | 7,474 | 7,481 | 0 | **+7** | +7 |
| **`KERN_SIZE`** | 104,448 | 104,448 | **99,840** | **0** | **−4,608** | **−4,608** |
| spare of `KERN_BUDGET` | 25,088 | 25,088 | 29,696 | | | |

**−4,974 resident, nine rungs back**: `KERN_SIZE` falls **104,448 → 99,840**,
which is **under 100 KB on either reading of a KB** (pass 9's brief asked for
≤ 102,400). Every resident section shrank or held; nothing grew. Resident is
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; overlay is `.ovl` +
`.ovlw`, which `mem_unblob` gives back once the desktop is up.

`.boot2`, the stage-2 blob and not the kernel, is 2,252 at A and C and 2,265
at B: the Floppy page's Cylinder gate (`7a48a57`, `efaf762`) is in
`boot/boot2.asm`.

## Headline: `kern_small`, the 128KB floor machine

| section | A base | C main | B elendilon = D | **C−A** | **B−A** |
|---|---:|---:|---:|---:|---:|
| `.text` | 34,051 | 34,086 | 32,905 | **+35** | **−1,146** |
| `.bss` | 3,218 | 3,222 | 3,117 | **+4** | **−101** |
| `.cold` | 25,431 | 25,467 | 24,415 | **+36** | **−1,016** |
| `.lowbss` | 2,868 | 2,868 | 2,868 | 0 | 0 |
| **resident** | 65,568 | 65,643 | 63,305 | **+75** | **−2,263** |
| `.ovl` | 2,086 | 2,086 | 2,086 | 0 | 0 |
| `.ovlw` | 1,514 | 1,514 | 1,480 | 0 | −34 |
| **overlay** | 3,600 | 3,600 | 3,566 | 0 | **−34** |
| **`KERN_SIZE`** | 67,584 | 67,584 | **65,536** | **0** | **−2,048** |
| spare of `KERN_SMALL_BUDGET` | 39,936 | 39,936 | 41,984 | | |

**−2,263 resident, four rungs back.** `KERN_SIZE` is **exactly 64 KB**. The
floor machine gets **2,048 bytes of heap** back.

## The attribution is exact

Every commit in A..B was measured against its first parent on both kernels.
A merge M gets its own figure too, its RESIDUAL: what M moved against its
first parent, less what its second parent moved against the merge base. The
commits' figures and the merges' residuals **sum to B−A exactly, in every
section on both kernels** (checked, not assumed).

**Ten of the 12 merges have a residual of 0 bytes.** Two do not, and both are
the same thing: a change counted on two branches and kept once.

| merge | `kern_big` | `kern_small` | what it is |
|---|---:|---:|---|
| `b1652e3` *Merge ksp9-shell* | **+4** (`.text`) | **+4** (`.text`) | two of pass 9's agents each deleted `ovw_desk_rowcalc`, the resident far-callable shim in `kernel.asm` the overlay reaches `desk_rowcalc` through. Pass 9's record says its merge *"reproduced the sum of the branches to within 4 bytes"*. This is those 4 |
| `8ff67bd` *Merge kernel-size-p9 into single-fdlg* | **+177** (`.cold`) | **+3** (`.cold`) | **pass 9 shrank code the Standard File chooser deleted** (below) |

The `8ff67bd` residual is entirely in one file each, measured with
`kernsize --modules` on the merge, both its parents and their base
(`e1c204c`):

| | chooser side | pass 9 side | merge, against the chooser side | **residual** |
|---|---:|---:|---:|---:|
| `kern_big` `fdlg.inc` `.cold` | −2,045 | **−177** | 0 | **+177** |
| `kern_small` `files.inc` `.cold` | +133 | −588 | −585 | **+3** |
| `kern_small` `FDLG.DRV` image | −1,692 | **−135** | 0 | **+135** |

**Pass 9's disk agent spent its `fdlg.inc` work on the old browser, and the
chooser deleted the browser.** All 177 of its resident bytes on `kern_big`
and all 135 of its `FDLG.DRV` bytes on `kern_small` are gone at the merge.
The pass's per-agent table (section 1 of its record) still credits them to the agent. On
`kern_small` three of `files.inc`'s bytes interact the same way. Neither is a
defect: two branches cut from one base both worked on `fdlg.inc`, and the
deletion won. The bytes are counted here as **pass 9 credit that did not
survive**, and the arm below carries them as their own row so that every
other row stays exact.

**61 of the 92 non-merge commits move a byte** (resident or overlay, either
kernel); the other 31 measure 0 in every section on both kernels.

## `main`'s arm: +75 resident and +1,232 of `CTRL.DRV`, all of it #215

| commit | what it touched | `kern_big` resident | `kern_small` resident | modules |
|---|---|---:|---:|---|
| #215 `6d1690c` *Add confirmed shutdown with the animated 8088 splash* | `ctrl.inc`, `menu.inc`, `sched.inc`, `shutdown.inc`, `spinner.inc`, `splash.inc`, `ui.inc` | **+75** (`.text` +35, `.bss` +4, `.cold` +36) | **+75** (the same) | `CTRL.DRV` **+1,232** on both kernels |
| #216, #223 | REDLINE, the CPU/graphics lab | 0 | 0 | |
| #219, `277313a` | The Wire's sidecars (32 a record, full 8.3 names) | 0 | 0 | |
| #225 | MIDIRack, the MIDI file player; `apps/os88ui.inc` gains `OS88UI_OWN` | 0 | 0 | |
| #217, #218, #220, #221, #222, #226, #227 | the release skill, the vga-face skill | 0 | 0 | |

**#225 touches a kernel source and costs the kernel nothing**: its 47 lines
of `apps/os88ui.inc` are under `%ifdef OS88UI_BOWN`, which the kernel never
defines, and the block `%error`s if `OS88UI_KERNEL` ever meets it. Measured
0 in every section on both kernels, and no module image moved.

**#215's resident +75 had a pass and its `CTRL.DRV` +1,232 did not.** Pass 9
names #215 in its brief as un-passed work, and the resident 75 went into the
pass's totals. Nothing in the pass reduced the shutdown dialog's module bytes.
They are on demand and on disk (below), so this is a priority for the next
pass, not a defect.

`main`'s packages are billed to floppies, never to resident RAM:
`MIDIRACK.O88` 29,486 bytes on disk (36,320 image), `REDLINE.O88` 15,280
(21,504 image).

## Our arm, by concept

Per concept, the sum of its commits' brackets. Every row is exact.

| concept | commits | **`kern_big` resident** | overlay | **`kern_small` resident** | overlay |
|---|---|---:|---:|---:|---:|
| **desktop cells drawn once, into the dither's own region** (SPEC.md 26.9.9): +1,171 as first built, −775 when the cells moved into the dither's region, the zoom's restore 0 | `03e8ef7` `1b372a1` `8aac7e7` | **+396** | 0 | 0 | 0 |
| **two desktop damage fixes**: an overflowed damage region marks every window it dithers over (+6/+6), the blit1 pen scoped to a callback (+28/0, SPEC.md 5.4.2.2.2) | `006a6da` `900ff63` | **+34** | 0 | **+6** | 0 |
| **LZ4 input, the decode cell and the parts carve past 64KB** | `f5e0c60` | **+43** | 0 | **+43** | 0 |
| **the Floppy page's Cylinder** (SPEC.md 31.14): all of it in `CTRL.DRV` and the boot overlay | `7a48a57` `efaf762` `3dabb6a` | 0 | +5 | 0 | 0 |
| **pass 9's two correctness fixes** (sections 3.1 and 3.3 of its record): a sector straddling a 64KB page goes through `dsk_secbuf` (+137/+137), `WRITE_AT`'s inside arm maps a media error (+3/0) | `cce9218` `f963d5a` | **+140** | 0 | **+137** | 0 |
| **review fixes**: the bounced read skips the cache, write errors mapped, two guards; the chooser's stamp and save target, Cylinder's owed byte, LZ bounds | `6b22c63` `85f4675` | **+50** | 0 | **+15** | 0 |
| **added** | | **+663** | **+5** | **+201** | 0 |
| **kernel size pass 9** (docs/plans/completed/HANDOFF-KERNEL-SIZE-P10.md): 49 commits on six agents' branches, the `kern_emu` overlay fit included, with the shim deleted twice (+4) | merged at `b1652e3` | **−3,790** | **+2** | **−2,499** | **−34** |
| **the Standard File dialog as a Disk window in a chooser role** (SPEC.md 38): the merge −1,721/−10, its keys and requester fixes −41/−20, arrow selection moved into the Disk window −6/−4, then the glue's size pass (SPEC.md 38.13) −337/−34 | `a7b063f` `b35df43` `602ea82` `efac54c` `25defdb` `f57e634` `6f61771` | **−2,099** | 0 | **−43** | 0 |
| **pass 9's credit on code the chooser deleted** (the `8ff67bd` residual, above) | `8ff67bd` | **+177** | 0 | **+3** | 0 |
| **removed** | | **−5,712** | **+2** | **−2,539** | **−34** |

### The arm, whole

| | `kern_big` resident | overlay | `kern_small` resident | overlay |
|---|---:|---:|---:|---:|
| added | +663 | +5 | +201 | 0 |
| removed | −5,712 | +2 | −2,539 | −34 |
| **our net** | **−5,049** | **+7** | **−2,338** | **−34** |
| `main`'s #215 | +75 | 0 | +75 | 0 |
| **B − A** | **−4,974** | **+7** | **−2,263** | **−34** |

**Pass 9 and the chooser took 5,889 resident bytes out of `kern_big`** (5,712
net of the 177 that did not survive the merge) **and 2,542 out of
`kern_small`; this cycle's features and fixes put 663 and 201 back.** The
chooser is the cycle's largest single removal: one design change worth
−2,099 on `kern_big`, the browser it replaced having been a second resident
copy of the Disk window (SPEC.md 38).

**Every pass's own record is confirmed to the byte:**

- **Pass 9** quotes `kern_big` 99,303 → 95,653 (−3,650) and `kern_small`
  65,686 → 63,324 (−2,362), `KERN_SIZE` 104,960 → 101,376 and
  67,584 → 65,536, against `e1c204c`. Its tip `9209554` measures exactly
  that, two correctness fixes included. Its module figures agree too:
  `CTRL.DRV` 12,114 → 12,218 and 5,673 → 5,735, `FORMAT.DRV` −6, `FDLG.DRV`
  −135, `FILECP.DRV` −32.
- **SPEC.md 38.13** (the chooser's glue) quotes `kern_big` 88,134 → 87,797
  (−337) and `kern_small` 60,450 → 60,416 (−34), in `.text` + `.bss` +
  `.cold`. `6f61771` measures exactly that.
- **SPEC.md 38's own headline is 12 bytes low on its BEFORE**, on both
  kernels: it quotes `kern_big` 93,357 → 91,607 and `kern_small`
  62,806 → 62,809, and the chooser's parent `e1c204c` measures **93,369**
  and **62,818** (`.text` + `.bss` + `.cold`). The AFTER (`f57e634`) agrees
  to the byte. So the chooser bought **−1,762** on `kern_big` where SPEC.md
  says −1,750, and **−9** on `kern_small` where it says +3. The difference
  is the same 12 on both kernels, which looks like a base measured one
  commit off rather than a mis-added figure.

**Rungs do not add, and this cycle shows it again.** The commits' own
`KERN_SIZE` moves sum to −4,096 on `kern_big` and −1,536 on `kern_small`.
One merge on each moves a rung with no byte of its own (`3e487ae` −512 on
`kern_big`, `b1652e3` −512 on `kern_small`). The kernel moved −4,608 and
−2,048.

### What has not had a pass

Pass 9 was cut at `e1c204c`, and the chooser had its own glue pass at
`6f61771`. **What landed after both** is the two review-fix commits and the
two desktop damage fixes: **+84 resident on `kern_big`** (`.text` +25,
`.cold` +59) and **+21 on `kern_small`**, plus `CTRL.DRV` +2 and `FDLG.DRV`
+46 (on `kern_small`). Those are exactly the figures `make` printed against
the blessed baseline at B (below). Against the 50% target passes have been
briefed with, that is **~42 bytes owed on `kern_big`** and ~10 on
`kern_small`. **#215's +1,232 of `CTRL.DRV`** (above) is the larger thing
still owed, on demand rather than resident.

## How close B stands to the next rung

This is what the next addition is billed against, not what this cycle cost:

| | image rung (`.text`+`.bss`) | cold rung | low rung |
|---|---:|---:|---:|
| `kern_big` at A | 242 left | 87 left | 34 left |
| `kern_big` at C | 203 left | **51 left** | 34 left |
| `kern_big` at B | 229 left | 466 left | **34 left** |
| `kern_small` at A | 107 left | 169 left | 204 left |
| `kern_small` at C | **68 left** | 133 left | 204 left |
| `kern_small` at B | 330 left | 161 left | 204 left |

**`kern_big`'s low rung is the near one again, 478/512 spent**, unchanged
since the previous report: the next 35 bytes of `.lowbss` cost a whole rung.
**The cold rung was crossed late in the cycle**, by `85f4675`'s +38 of
`.cold` (review fixes) when it stood 13 bytes from its edge, so B's cold
rung is almost empty (46/512). Per CLAUDE.md's banner this decides WHEN the
machine pays and never what a change cost.

## The on-demand modules

A module is read into a heap claim when its feature is used and freed after,
so none of this is in `KERN_SIZE`. Images (unpacked) and files (as shipped),
off each point's own build. C's images are those of `6d1690c` (#215) to the
byte: upstream's other twelve commits move no module.

| module | A image | B image | Δ | A file | B file | claim, whole KB |
|---|---:|---:|---:|---:|---:|---|
| `CTRL.DRV` | 10,455 | 12,220 | **+1,765** | 10,455 | 12,220 | 11 → **12** |
| `FORMAT.DRV` | 1,304 | 1,298 | −6 | 1,222 | 1,220 | 2 → 2 |
| `CLONE.DRV` | 10,073 | 10,073 | 0 | 8,528 | 8,529 | 10 → 10 |
| `HIBER.DRV` | 6,662 | 6,662 | 0 | 5,102 | 5,103 | 7 → 7 |
| `DOCK.DRV` | 2,515 | 2,515 | 0 | 2,234 | 2,235 | 3 → 3 |
| `EXTD.DRV` | 1,566 | 1,566 | 0 | 1,378 | 1,378 | 2 → 2 |
| **`kern_small`** `CTRL.DRV` | 4,441 | 5,735 | **+1,294** | 4,073 | 5,239 | 5 → **6** |
| `kern_small` `FORMAT.DRV` | 1,304 | 1,298 | −6 | 1,222 | 1,220 | 2 → 2 |
| `kern_small` `CLONE.DRV` | 9,204 | 9,204 | 0 | 7,792 | 7,791 | 9 → 9 |
| `kern_small` `FILECP.DRV` | 2,269 | 2,237 | −32 | 2,066 | 2,052 | 3 → 3 |
| `kern_small` `FDLG.DRV` | 3,214 | **1,286** | **−1,928** | 2,922 | 1,239 | 4 → **2** |

A file that moves by one byte while its image length holds is a packed module
whose bytes changed and whose length did not: a module carries the layout of
the kernel it was cut from (SPEC.md 2.8.2).

Along B's first-parent chain, each step measured at its own build:

| step | `kern_big` | `kern_small` |
|---|---|---|
| #215, the confirmed shutdown | CTRL **+1,232** | CTRL **+1,232** |
| the Floppy page's Cylinder (three commits) | CTRL +427 (+11, +112, +304) | |
| pass 9 and the chooser (`e48ed24`) | CTRL +104, FORMAT −6 | CTRL +62, FORMAT −6, FILECP −32, **FDLG −1,974** |
| review fixes (`85f4675`) | CTRL +2 | FDLG +46 |

`FDLG.DRV`'s −1,974 at `e48ed24` is the chooser −1,692 and its glue pass
−282. Pass 9's own −135 there did not survive the merge (above).
**`CTRL.DRV` still ships UNCOMPRESSED on `kern_big`** (the previous report's
`c9928210`, so a desktop gesture can read the settings core alone), so its
growth is system-disk bytes one for one: +1,765.

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved (`.text` / `.cold` /
`.bss`; `.lowbss` moved in no file):

| file | `kern_big` | `kern_small` |
|---|---|---|
| `fdlg.inc` | −66 / **−2,287** / −101 | −66 / −55 / −67 |
| `wm.inc` | **−528** / −2 / 0 | **−512** / 0 / 0 |
| `vga12.inc` | −271 / +78 / −69 | −31 / 0 / −33 |
| `memory.inc` | −2 / −238 / −2 | 0 / −189 / −2 |
| `files.inc` | +8 / −312 / +65 | +8 / **−469** / +28 |
| `diskw.inc` | 0 / −199 / 0 | 0 / −194 / 0 |
| `ui.inc` | −201 / +9 / −2 | −145 / +9 / −2 |
| `mouse.inc` | −147 / 0 / −44 | −80 / 0 / −20 |
| `disk.inc` | −26 / −157 / +1 | −26 / −143 / +1 |
| `instance.inc` | −115 / −29 / −2 | −47 / −29 / −2 |
| `icons.inc` | −118 / 0 / +3 | −9 / 0 / 0 |
| `clock.inc` | −110 / 0 / 0 | −11 / 0 / 0 |
| `kernel.asm` | −106 / +8 / 0 | −71 / +8 / 0 |
| `driver.inc` | −19 / −63 / 0 | — |
| `assoc.inc` | 0 / −77 / 0 | — |
| `menu.inc` | −54 / 0 / 0 | −54 / 0 / 0 |
| `apps.inc` | 0 / −44 / 0 | 0 / −2 / 0 |
| `filecp.inc` | 0 / −32 / 0 | — |
| `fsx.inc`, `softgfx.inc`, `font.inc`, `loader.inc`, `dock.inc` | −20, −17, −16, −12, −11 | −9, −17, −3, −11, −5 |
| `mod.inc` | +8 / −25 / 0 | +8 / −25 / 0 |
| `ctrl.inc` | −44 / +34 / 0 | −45 / +34 / 0 |
| `snd.inc`, `toast.inc`, `viddet.inc`, `sched.inc`, `clip.inc` | −9, −8, −8, −7, −1 | −6, −8, −8, −7, −1 |
| `hiber.inc` | 0 / −4 / +4 | 0 / 0 / −4 |
| `vidsel.inc` | — | −1 / 0 / 0 |
| `lz.inc` | 0 / **+51** / 0 | 0 / **+51** / 0 |
| `desk.inc` | 0 / **+362** / 0 | 0 / −1 / 0 |

Two files grew: `desk.inc` is the desktop's cells drawn once (`kern_big`
only), and `lz.inc` is LZ4 input past 64KB (+43) plus its review fix's
bounds (+8), on both kernels. `fdlg.inc` is the chooser, almost all of it.

## The final bill

**What the PR asks `main` to take, on the kernel**, with D = B:

| | `kern_big` | `kern_small` |
|---|---:|---:|
| `KERN_SIZE` at the #214 squash (A) | 104,448 | 67,584 |
| `KERN_SIZE` at `main`'s tip (C) | 104,448 | 67,584 |
| `KERN_SIZE` at the PR tip (D) | **99,840** | **65,536** |
| **change, A → D** | **−4,608** (nine rungs) | **−2,048** (four rungs) |
| resident bytes, A → D | **−4,974** | **−2,263** |
| resident bytes, C → D (against `main` as it stands, #215 included) | **−5,049** | **−2,338** |
| overlay bytes, A → D | +7 | −34 |
| heap a machine gets back, against A and against C | **4,608** | **2,048** |
| spare of the budget | 25,088 → **29,696** (49 → 58 steps) | 39,936 → **41,984** (78 → 82 steps) |
| `.text`+`.bss` of `KERN_CODE_MAX` (65,536) | 51,470 → **49,435**: 14,066 → **16,101 left** | 37,269 → **36,022**: 28,267 → **29,514 left** |
| guard 5 (boots on `MIN_RAM_KB`) | 87,040 → **91,648** before it cannot boot on 196KB | 54,272 → **56,320** on 128KB |
| on-demand modules | `CTRL.DRV` +1,765 (+1,232 of it #215's), everything else −6 or 0 | `CTRL.DRV` +1,294 (+1,232 #215's), `FDLG.DRV` **−1,928**, `FILECP.DRV` −32 |

**In one line each:**

- **`main`** spent **75 resident bytes on each kernel and 1,232 of
  `CTRL.DRV`** (#215's confirmed shutdown). Its other twelve commits are
  packages, skills and release tooling, and move no kernel byte; that
  includes #225, whose `apps/os88ui.inc` change is under an `%ifdef` the
  kernel never defines.
- **We** added **663 resident bytes on `kern_big` and 201 on `kern_small`**:
  the desktop's cells drawn once (396), pass 9's two correctness fixes
  (140/137), review fixes (50/15), LZ4 past 64KB (43/43) and two damage fixes
  (34/6). **Pass 9 and the Standard File chooser took 5,712 and 2,539 back
  out**, after 177/3 of pass 9's credit went with the code the chooser
  deleted. Every quoted figure was checked against its own record.
- **Together**, the PR tip is **4,608 bytes and nine rungs smaller than the
  #214 squash on `kern_big`**, and **2,048 bytes and four rungs smaller on
  `kern_small`**. Both kernels are smaller again, and `kern_big`'s
  `KERN_SIZE` is under 100 KB for the first time in this family of reports.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s blessed baseline was the chooser's glue pass
  (`6f61771`), and B is past it**: every `make` at B printed `sum +84` and
  *"the cold rung CROSSED: 75 -> 76"* on `kern_big`, and +21 on
  `kern_small`. That is the review fixes and the two damage fixes, which did
  not re-bless. **Closed in the same commit as this file**: all three kernels
  (`big`, `small`, `emu`) are re-blessed at B, and `kernsize` then reads +0
  on each.
- **The fork's `origin/main` is twelve commits behind upstream.** A report
  taken against it would have named #215 as `main`'s tip and missed four
  commits the PR must carry. C here is `jggonz/os8088`'s `main`, fetched
  directly.
- **The upstream merge D needs has one conflict, `CLAUDE.md`**, and
  touches no kernel byte. The trial merge used to measure D was not
  committed.
- **SPEC.md 38's chooser headline is 12 bytes low on its before figure**
  on both kernels (above). The after figure and SPEC.md 38.13's figures are
  exact.

## Appendix: every kernel-moving commit, against its own first parent

*Resident* is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`; *overlay*
is `.ovl` + `.ovlw`. `.lowbss` moved in no commit. The other 31 non-merge
commits in A..B measure 0 in every section on both kernels; the merges are
in *The attribution is exact*. The resident and overlay columns plus the two
merge residuals sum to the headline tables exactly. The `KERN_SIZE` columns
do not, which is the point made above.

| commit | subject | big `.text` | big `.bss` | big `.cold` | big overlay | **big resident** | big `KERN_SIZE` | **small resident** | small overlay | small `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `6d1690c` | **main:** Add confirmed shutdown with the animated 8088 splash (#215) | +35 | +4 | +36 | 0 | **+75** | 0 | **+75** | 0 | 0 |
| `03e8ef7` | desk: a cell is drawn ONCE, and only where it shows (SPEC.md 26.9.9) | +104 | +153 | +914 | 0 | **+1,171** | +1,536 | **0** | 0 | 0 |
| `1b372a1` | desk: cells drawn into the dither's own region, 775 resident bytes back | −169 | −149 | −457 | 0 | **−775** | −1,024 | **0** | 0 | 0 |
| `7a48a57` | Floppy page: Cylinder is back, and a FAILED canary is what turns it off | 0 | 0 | 0 | +5 | **0** | 0 | **0** | 0 | 0 |
| `f5e0c60` | lz: LZ4 input, the decode cell and the parts carve all cross 64KB | 0 | 0 | +43 | 0 | **+43** | 0 | **+43** | 0 | 0 |
| `801c35d` | disk: ksp9 batch 1 - synth, next_clus, read_chain, dot links, relays | 0 | −1 | −166 | 0 | **−167** | 0 | **−146** | 0 | 0 |
| `b11d081` | memory.inc: one record walk, the direction in BP, CF tails in two bytes | −2 | −2 | −151 | 0 | **−155** | 0 | **−155** | 0 | 0 |
| `cb0f029` | gfx: the primitive union, one cursor-rect test, the display dispatch re-cut | −119 | −101 | −10 | 0 | **−230** | 0 | **−44** | 0 | 0 |
| `af042f4` | files: kernel size pass 9 - the Disk window | −2 | +4 | −463 | −3 | **−461** | −512 | **−440** | −3 | −512 |
| `4e846cf` | disk: ksp9 batch 2 - FAT offset arithmetic, run caps, error arms, CF | 0 | 0 | −129 | 0 | **−129** | 0 | **−88** | 0 | 0 |
| `db0a223` | gfx: mou_apply's staging word, mou_newround, the hot-spot load, table builders | −47 | 0 | 0 | 0 | **−47** | −512 | **−44** | 0 | 0 |
| `480e3d1` | instance.inc, apps.inc: the pool pick in registers, the snapshot header as stosw | −57 | −2 | −43 | 0 | **−102** | 0 | **−71** | 0 | 0 |
| `d896127` | ui, menu: the UI task's dispatch, ladder and drag bookkeeping | −228 | −2 | +9 | 0 | **−221** | 0 | **−169** | 0 | 0 |
| `d2756de` | clock: the six RTC port helpers are a copy in each image (SPEC.md 37.94) | −118 | 0 | 0 | +71 | **−118** | −512 | **−11** | 0 | 0 |
| `a8937bf` | snd, fsx, loader, assoc, hiber, sched: CF tails, the death panel's reads | −25 | 0 | −12 | 0 | **−37** | 0 | **−24** | 0 | 0 |
| `f4cd824` | wm: the save-under layout without a branch per fragment, the raise box in three calls | −356 | 0 | 0 | 0 | **−356** | −512 | **−351** | 0 | 0 |
| `f93e0ab` | gfx: hot-loop compactions that are also faster, the clip arms above their entries | −93 | −9 | 0 | 0 | **−102** | 0 | **−61** | 0 | 0 |
| `67b9c13` | files, filecp, lz, mod: kernel size pass 9, second round | +8 | 0 | −129 | 0 | **−121** | 0 | **−88** | 0 | 0 |
| `c54b565` | disk: ksp9 batch 3 - one-sector helpers, driver kinds, find's state | 0 | −4 | −64 | 0 | **−68** | 0 | **−67** | 0 | 0 |
| `ded2556` | gfx: gfx_points' three loops share one miss arm, the first box resolved up front | −84 | 0 | 0 | 0 | **−84** | 0 | **−8** | 0 | 0 |
| `719d48c` | kernel.asm: thirteen cw_ shims become their OSAPI cells; mem_regrow, ld_start | −52 | 0 | −20 | 0 | **−72** | 0 | **−64** | 0 | 0 |
| `8706510` | disk: ksp9 batch 4 - one BPB bank body, fdlg's shared prologue | 0 | 0 | −75 | 0 | **−75** | 0 | **−36** | 0 | 0 |
| `352b356` | files, filecp: kernel size pass 9, third round | 0 | 0 | −56 | 0 | **−56** | 0 | **−48** | 0 | 0 |
| `f437a78` | gfx: clipped arms tail-jump, the planar cursor's cell computed once | −16 | 0 | 0 | 0 | **−16** | 0 | **−7** | 0 | 0 |
| `235d39f` | disk: ksp9 batch 5 - redundant range tests, alloc, the folder icon as runs | −26 | 0 | −33 | 0 | **−59** | −512 | **−56** | 0 | 0 |
| `e551923` | gfx: gfx_blit4's seam cut keeps its state in the registers the nested call returns | −29 | −4 | 0 | 0 | **−33** | 0 | **0** | 0 | 0 |
| `d26f95c` | wm: single-caller helpers folded in, one-byte tests, ladder tails | −80 | 0 | −2 | 0 | **−82** | 0 | **−62** | 0 | 0 |
| `b64abb9` | sched hot path, mem_compact's tail, three resident thunks gone | −31 | 0 | −2 | 0 | **−33** | 0 | **−16** | 0 | 0 |
| `85f5a01` | disk: ksp9 batch 6 - fdlg_findname reuse, fdlg_textk, dsk_xfer success arm | 0 | 0 | −35 | 0 | **−35** | 0 | **−5** | 0 | 0 |
| `e20b4d1` | gfx: points dispatch ink-first, the slow path's array segment, three CF tidies | −15 | 0 | 0 | 0 | **−15** | 0 | **0** | 0 | 0 |
| `5e05a7c` | gfx: the BIOS data area through segment 0 | −6 | 0 | 0 | 0 | **−6** | 0 | **−6** | 0 | 0 |
| `e8fcfa2` | files: kernel size pass 9, fourth round | 0 | 0 | −25 | 0 | **−25** | 0 | **−25** | 0 | 0 |
| `5ff281e` | single-caller routines inlined: instance, apps, assoc, memory | −69 | 0 | −73 | 0 | **−142** | 0 | **−30** | 0 | 0 |
| `41f0057` | disk: ksp9 batch 7 - BPB rules, mount tails, name83, FAT window helpers | 0 | 0 | −121 | 0 | **−121** | 0 | **−99** | 0 | −512 |
| `57d0bc8` | disk: ksp9 batch 8 - the name box, the two-line button, newfolder | 0 | 0 | −46 | 0 | **−46** | 0 | **0** | 0 | 0 |
| `4b43a5b` | gfx: the union's floor covers a thinned blit4 set (NOPLANE=1); mou_b1 in mou_masks | −4 | 0 | 0 | 0 | **−4** | 0 | **−4** | 0 | 0 |
| `3a156a3` | files: kernel size pass 9, fifth round | 0 | 0 | −10 | 0 | **−10** | 0 | **−5** | 0 | 0 |
| `99f8e44` | wm, icons, font, toast: more single-caller folds and shorter shapes | −70 | 0 | 0 | 0 | **−70** | 0 | **−48** | 0 | −512 |
| `23bbd5b` | gfx: the planar cursor's save/restore setup in cur_rect, no AX/BX banking | −11 | 0 | 0 | 0 | **−11** | 0 | **−7** | 0 | 0 |
| `253c6d8` | files: kernel size pass 9, sixth round | 0 | 0 | −8 | 0 | **−8** | 0 | **−8** | 0 | 0 |
| `e7cb3d0` | driver, ctrl, ui, desk, dock: one stamped far call, the save verdicts | −87 | 0 | −60 | 0 | **−147** | 0 | **−64** | 0 | 0 |
| `c1e7151` | more single-caller routines written at their call sites | −29 | 0 | −95 | 0 | **−124** | −512 | **−23** | 0 | 0 |
| `8c0e593` | gfx: gfx_points asks 1bpp? once, vid_apply loads [vid_seg] once | −21 | 0 | 0 | 0 | **−21** | 0 | **−7** | 0 | 0 |
| `6935c82` | wm: the damage pass keeps the rect it already holds; front, seed, CF tidy | −65 | 0 | 0 | 0 | **−65** | 0 | **−32** | 0 | 0 |
| `8e1d0ba` | gfx: kbm_ui banks no AX around the release it posts | −2 | 0 | 0 | 0 | **−2** | 0 | **−2** | 0 | 0 |
| `799b770` | menu, desk, driver, clock, ui, ctrl: the last small cuts, and .ovlw back | −38 | 0 | −26 | −42 | **−64** | 0 | **−41** | −31 | 0 |
| `8640260` | wm, icons, clip: zeros from a register, CF from the compare, one store for two | −50 | 0 | 0 | 0 | **−50** | 0 | **−41** | 0 | 0 |
| `19df158` | files: keep the scroll-paint row path off the shared prologue | 0 | 0 | +9 | 0 | **+9** | 0 | **+9** | 0 | 0 |
| `6f40762` | wm, font: the precover walk banks nothing it does not need | −13 | 0 | 0 | 0 | **−13** | 0 | **−9** | 0 | 0 |
| `a7b063f` | Standard File dialog: a Disk window in a chooser role (SPEC.md 38) | −39 | −36 | −1,646 | 0 | **−1,721** | −1,536 | **−10** | 0 | 0 |
| `cce9218` | disk: a sector that straddles a 64KB page goes through dsk_secbuf | 0 | +6 | +131 | 0 | **+137** | 0 | **+137** | 0 | 0 |
| `efac54c` | fm_edit_end spares the requester while a chooser is up; trim the chooser's keys | 0 | 0 | −27 | 0 | **−27** | 0 | **+13** | 0 | 0 |
| `269eb66` | driver: drv_boot_x's three row walks ask one question, and kern_emu fits again | 0 | 0 | 0 | −24 | **0** | 0 | **0** | 0 | 0 |
| `25defdb` | fm_count need not excuse the modal chooser | 0 | 0 | −8 | 0 | **−8** | 0 | **−8** | 0 | 0 |
| `f963d5a` | diskw: WRITE_AT's inside arm maps a media error as one again | 0 | 0 | +3 | 0 | **+3** | 0 | **0** | 0 | 0 |
| `f57e634` | Arrow selection is the Disk window's on kern_big (SPEC.md 22.26) | 0 | 0 | −6 | 0 | **−6** | 0 | **−4** | 0 | 0 |
| `6f61771` | Standard File chooser: a size pass on the glue (SPEC.md 38.13) | −11 | −4 | −322 | 0 | **−337** | −512 | **−34** | 0 | 0 |
| `6b22c63` | review fixes: the bounced read skips the cache, write errors mapped, two guards | +4 | 0 | +7 | 0 | **+11** | 0 | **+20** | 0 | 0 |
| `85f4675` | review fixes: chooser stamp and save target, Cylinder's owed byte, LZ bounds | +1 | 0 | +38 | 0 | **+39** | +512 | **−5** | 0 | 0 |
| `006a6da` | wm: an overflowed damage region marks every window it dithers over | +6 | 0 | 0 | 0 | **+6** | 0 | **+6** | 0 | 0 |
| `900ff63` | gfx: the blit1 pen is scoped to a callback, not to a lock hold | +14 | 0 | +14 | 0 | **+28** | 0 | **0** | 0 | 0 |
