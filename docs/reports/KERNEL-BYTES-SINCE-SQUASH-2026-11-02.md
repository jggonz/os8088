# Kernel bytes since the #326 squash, by concept

**A measurement, not a description.** Taken 2026-11-02 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file table), which
RE-ASSEMBLES the kernel rather than reading a build. Every commit of the
round, and every parent it names, was measured in a clean tree of its own:
`git archive` of that commit's `kernel/`, `boot/`, `tools/`, `drivers/`,
Makefile and the top-level files of `apps/`, `apps/stiklib.asm` assembled
there into the `stiklib.bin` that `deskmod.inc` includes, and that commit's
own `kernsize` measuring **`kern_big`, `kern_small`, `kern_emu` and
`kern_mega`**, with `-DKERN_KNOB` added only where a tree's `kern_mega`
would not assemble without it. A tree whose kernel inputs hash the same as
one already measured takes that tree's figures: **464 points, 262 distinct
kernel trees**. `associco.inc` is B's in every tree. Module and driver
images come from `make`, `make emu` and `make mega` at the two endpoints
only. It is true of the commits it names and of no other tree.

It is the sixteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-31.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?**

## The points

| | commit | what it is |
|---|---|---|
| **A** | `6dfafef4` | **`main`**: #326, *Elendilon -> Main (kern_mega Real-Mode Video Bracket and Budget, ...)* - the squash this round starts from. Its tree is byte-identical to `elendilon-pr`'s tip `b3589aea` |
| **E** | `d854e1df` | `origin/elendilon`: *Merge branch 'kernel-size-17' into elendilon* |
| **B** | `2a148820` | **`elendilon-next`**: A, then `70d222f3` (E's tree with the eight `elendilon-pr` review commits E never carried replayed onto it, as ONE commit), then `092e7835`'s Tracker change cherry-picked |

`elendilon-pr` was cut from `elendilon` at **`c844ccc0`** for #326, and both
then merged `main`'s #322 (`0b05a62c`). So the round is the range
**`c844ccc0..d854e1df`**: **455 commits, 98 of them merges** - 454 of
`elendilon`'s and #322 itself, which `main` already has. The
squash and the cherry-pick that bring it home are measured separately:

| step | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` conventional | `kern_mega` `.mega` |
|---|---:|---:|---:|---:|---:|
| `A − c844ccc0` (the PR side: #322, the merge, the review fixes) | +20 | +20 | +20 | +20 | 0 |
| `E − c844ccc0` (the range) | −338 | −595 | −340 | −2,191 | +256 |
| `70d222f3 − E` (the eight review commits, replayed) | 0 | 0 | 0 | 0 | +16 |
| `B − 70d222f3` (Tracker) | 0 | 0 | 0 | 0 | 0 |
| **`B − A`** | **−358** | **−615** | **−360** | **−2,211** | **+272** |

#322's +20 is on both sides, so it cancels in `B − A`. The `.mega` +16 is
`fsx_rupf` zeroing `[spk_rdsv]` (`e4fc94d7`, SPEC.md 53.2.3), a six-byte
store; on `A`'s side the same commit measured no change in `.mega`. **No other
review commit moves a kernel byte**, and Tracker is a package.

On `kern_big` **84** of the range's commits and 2 of its merges move a
resident byte; on `kern_small` 60 and 2; on `kern_mega` 114 and 9.

**The build number is held at 412 for every tree**: `BUILD_STR` is the
commit count (SPEC.md 14.2), and a digit is not a cost anybody spent.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu`, 64,000 on `kern_small` and 68,096 on `kern_mega`, with
`KERN_GRACE` 2,048. **`make budget-check` passes at B on all four** - 4,096,
4,608, 3,584 and 5,120 spare - so B could go to `main` as it stands.

## Headline: `kern_big`, `kern_small` and `kern_emu`

**The round SAVES 358 resident bytes on `kern_big`, 360 on `kern_emu` and
615 on `kern_small`, and `KERN_SIZE` falls one 512-byte rung on all three.**
Two size passes saved 1,372 bytes on `kern_big`; five feature rows spent
1,014 of them.

| section | `kern_big` A | B | B − A | `kern_small` A | B | B − A | `kern_emu` A | B | B − A |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `.text` | 40,971 | 40,576 | **−395** | 30,695 | 30,208 | **−487** | 41,227 | 40,830 | **−397** |
| `.bss` | 5,390 | 5,411 | +21 | 2,965 | 2,945 | −20 | 5,390 | 5,411 | +21 |
| `.cold` | 37,439 | 37,455 | +16 | 21,329 | 21,221 | **−108** | 37,439 | 37,455 | +16 |
| `.vga` | 2,032 | 2,032 | 0 | - | - | | 2,032 | 2,032 | 0 |
| `.lowbss` | 5,358 | 5,358 | 0 | 2,868 | 2,868 | 0 | 5,358 | 5,358 | 0 |
| `.vgabuf` | 336 | 336 | 0 | - | - | | 336 | 336 | 0 |
| **resident** | 91,526 | **91,168** | **−358** | 57,857 | **57,242** | **−615** | 91,782 | **91,422** | **−360** |
| `.ovl` (window) | 5,119 | 5,116 | −3 | 1,477 | 1,474 | −3 | 5,119 | 5,116 | −3 |
| `.boot2` (blob) | 6,455 | 6,455 | 0 | 4,745 | 4,745 | 0 | 6,570 | 6,570 | 0 |
| **`KERN_SIZE`** | 97,280 | **96,768** | **−512** | 59,904 | **59,392** | **−512** | 97,792 | **97,280** | **−512** |
| spare of the budget | 3,584 | 4,096 | | 4,096 | 4,608 | | 3,072 | 3,584 | |

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** |
|---|---:|---:|---:|---:|
| **Kernel size pass 17** (`HANDOFF-KERNEL-SIZE-P18.md`) | 50 + 9 merges | **−753** | **−510** | **−753** |
| **Kernel size pass 16** (`HANDOFF-KERNEL-SIZE-P17.md`) | 40 + 9 merges | **−619** | **−316** | **−621** |
| **Queued toasts, one progress scale per operation, `OSAPI_FS_OP`** (`1cee9991` .. `41f31be6`; SPEC.md 12.8.8-12.8.10) | 8 | **+470** | +6 | **+470** |
| **A driver volume's sector across a 64 KB page is staged, not refused** (`e10d32c5`, SPEC.md 18.91) | 1 | **+180** | 0 | **+180** |
| **The heap's granule and `OSAPI_MEM_P`** (`3d349548`, `327c7492`, `9d03e89b` and seven more; SPEC.md 50.2.1, 50.3.7) | 10 + 1 merge | **+160** | **+141** | **+160** |
| **System Check, `FSCHECK.DRV`** (`8bc149c3`, `f0db927e`, `ec2a4caa`, and `cd1a0a88` + `7aaf626c` as one; SPEC.md 118.14) | 4 + 1 merge | **+118** | +10 | **+118** |
| **The near-call filter**, an E8 BCJ in the kernel's unpacker (`e193cdc9`, SPEC.md 20.13.9) | 1 | +44 | +44 | +44 |
| **A Windows program launched from DOS** (`5cf63d75`) and its merge with #322 (`cc9b679b`) | 1 + 1 merge | +32 | +10 | +32 |
| **Task Manager's MAX RUN is the what-if** (`9939bd3d`) | 1 | +10 | 0 | +10 |
| the DOS window's word moves, in and out again (`5fc12615`, `f40229b7`) | 2 | 0 | 0 | 0 |
| the other 238 and 77 merges (kern_mega's own work among them) | | 0 | 0 | 0 |
| **the round, against A** | | **−358** | **−615** | **−360** |

**Size pass 17's −753** is `.text` −383, `.cold` −348, `.bss` −22. Its
largest rows: single callers and unread banks across `wm`, `instance`,
`dock`, `events` and the apps (`12c075d3`, −135), callee banks no caller
reads (`cf0c78b7`, −120), `dsk_xfer`'s page cut (`3095eea9`, −85), `disk`'s
single callers and skip chains (`7514cc43`, −74), the driver and module
loaders and the clock formatters (`d8f7ec36`, −55). **Size pass 16's −619**
is `.text` −370, `.cold` −246, `.bss` −3: the heap's doors and walks
(`12a2de92`, −104), single callers at their calls in the GUI (`61f23232`,
−72; `51a29888`, −48), the file side (`1e1435d8`, −65) and the sweep's
single-caller bodies (`23ff2925`, −67). **No commit in either pass adds a
resident byte on `kern_big`.**

**The toast queue and `OSAPI_FS_OP`'s +470** is `.text` +332, `.cold` +110,
`.bss` +28: the prototype queue and scale (`1cee9991`, +300), the package's
operation that parks the progress widget (`dde803bc`, +128), and five
follow-ups (+42). `kern_small` takes neither the queue nor the scale
(`cfcc380d`), so it pays +6. Size pass 16 then wrote it smaller (`c856417e`
−47, `c1aae22d` −13, `81a733aa` −8, `80747b22` −6), which this table charges
to the pass.

**The staged sector's +180** is all `.cold`, on `kern_big` and `kern_emu`
only: a driver volume's sector that straddles a 64 KB physical page goes
through a staging buffer instead of answering error 09h.

**The granule's +160** is `.text` +6 and `.cold` +154: claims rounding to
`MEM_GRAN` in one place (`3d349548`, +30), package regions, drivers and the
disk caches claiming to the granule (`327c7492`, +28), `OSAPI_MEM_P` itself
(`9d03e89b`, +78), `MEMP_KPARTS` for the Task Manager (`928f1083`, +14) and
`mem_regrow`'s 1 MB refusal (`8c58f9af`, +6). On `kern_small` a module's bss
stopped being claimed separately (`aafe18bc`, −22), so it pays +141.

**System Check's +118** is `.text` +10, `.cold` +90, `.bss` +18 for the
kernel's `OSAPI_VOL_CHECK` door and its state. The check itself is
`FSCHECK.DRV`, an on-demand image with no row (below).

## Headline: `kern_mega`, the 286+ protected-mode kernel

| section | A | B | **B − A** |
|---|---:|---:|---:|
| `.text` | 46,593 | 45,820 | **−773** |
| `.bss` | 5,427 | 5,449 | +22 |
| `.cold` | 45,920 | 45,641 | **−279** |
| `.lowbss` | 6,751 | 5,570 | **−1,181** |
| `.vgabuf` | 336 | 336 | 0 |
| conventional sections (the five above) | 105,027 | **102,816** | **−2,211** |
| `.mega` | 21,440 | 21,712 | +272 |
| `.mega`'s scratch | 1,490 | 1,510 | +20 |
| `.boot2` (blob) | 6,708 | 6,772 | +64 |
| **`KERN_SIZE` at boot** | 134,144 | **132,608** | **−1,536** |
| **resident once switched** (`MEGA_RESIDENT`, the budgeted figure) | 65,024 | **62,976** | **−2,048** |

| concept | commits | conventional | `.mega` | **resident once switched** |
|---|---:|---:|---:|---:|
| **kern_mega's tables out of conventional memory** (`df8e5697`, `bb7f72b5`, `a7969197`) | 3 | **−1,115** | +176 | **−1,536** |
| **kern_mega size pass 2** | 41 + 15 merges | **−762** | −256 | **−512** |
| **Kernel size pass 17**'s `kern_mega` rows and the shared code | 50 + 9 merges | −697 | −16 | 0 |
| **Kernel size pass 16**'s | 40 + 9 merges | −603 | −32 | −512 |
| **Queued toasts and `OSAPI_FS_OP`** | 8 | +471 | 0 | **+512** |
| **The granule and `OSAPI_MEM_P`**, with `kern_mega`'s pool offered first (`f4e195ee`) and `MEMP_CONV` | 10 + 1 merge | +271 | 0 | 0 |
| **System Check on `kern_mega` and hard disks** (`f0db927e`) | 4 + 1 merge | +135 | 0 | 0 |
| the near-call filter, the Windows launch, MAX RUN, task 0's stack | 4 + 1 merge | +90 | 0 | 0 |
| **The real-mode bracket's fixes** (`75442582`, `1dea2435`, `ecc277fc`, `86d820ea`) | 4 | −1 | **+384** | 0 |
| the replayed review fix (`e4fc94d7`, in `70d222f3`) | | 0 | +16 | 0 |
| **the round, against A** | | **−2,211** | **+272** | **−2,048** |

What a running `kern_mega` keeps is **2,048 bytes less**. Three rungs come
out with the tables: `mx_tab` and `mem_sel` move into the tables' tail above
1MB (`bb7f72b5`, `.lowbss` −1,024), and the window and real-segment tables
follow them (`df8e5697`, −512). Size pass 2 takes one more and size pass 16
one, and the toast queue puts one back. At boot it is **−1,536**, because the
size passes' `.cold` and `.mega` savings are read in conventional memory too.

**The bracket's fixes are `.mega` only** (+384): the speaker ring closed
before the rate (`1dea2435`), the speaker close itself in `.mega`
(`ecc277fc`), the protected-mode stores moved there to keep
`tests/megaseg.txt` green (`86d820ea`), and the park header's text page
(`75442582`).

What binds `kern_mega` at B:

- **the budget**: resident 62,976 of 68,096, **5,120 spare** (3,072 at A);
- **the 64 KB segment**: `tools/os88megaseg.py` reads `.text`+`.bss`+`.vga`
  51,269, **14,267 left** (13,516 at A), `kern_mega` carrying **3,250 bytes
  beyond `kern_big`** there (3,627 at A). `tests/megaseg.txt` allows 3,250,
  so it has no headroom left;
- **above 1MB**: `.cold` 45,641 + `.mega` 21,712 + its scratch 1,510 =
  69,302 of the code area's 261,120-byte cap, **191,818 left** (192,110 at A);
- its rungs: image 443 left (204 at A), cold 439 (160), **low 62** (417),
  vgabuf 176 (176), `.mega` 330 (110).

## The attribution is exact

Every non-merge commit was measured against its first parent. A merge is
charged whatever makes the running total equal its own measurement: `M −
c844ccc0 − Σ(every in-range commit M reaches)`. **The commits and the merges
sum to `E − c844ccc0` exactly, in every section, on all four kernels**
(checked, not assumed).

**Two stretches of the range do not assemble**, and for them a tree takes
its first parent's figures, so the bytes land on the commit that makes the
build work again:

- **27 commits of the heap-granule branch do not assemble `kern_mega`**,
  `9d03e89b` (*OSAPI_MEM_P*) to `05de8a14` (*parts: the standard claims
  through OSAPI_MEM_P*): `disk.inc:6989`, *short jump is out of range*. The
  other three kernels assemble throughout. The branch's `kern_mega` cost is
  charged to `f4e195ee` (+84) and the merge `a7bd95c9` (+78).
- **The merge `cd1a0a88`** (*Merge origin/elendilon into the kernel size
  pass 17 branch*) assembles **no** kernel: `fsck.inc:478`, *symbol `fsk_ws`
  not defined*. Its own next commit, `7aaf626c`, repairs it. The two are
  reported as one row: +4 on `kern_big`, `kern_emu` and `kern_mega`, +4 on
  `kern_small`, inside System Check's row.

## Where the bytes are, by file

`kernsize --modules`, A → B, every row that moved more than 4 bytes of code
(`.text` / `.cold`), or any `.bss`.

| file | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|
| `mega.inc` | - | - | **−370 / −54**, `.lowbss` **−1,182** |
| `wm.inc` | **−320 / −4** | **−263 / 0** | **−320 / −4** |
| `menu.inc` | −110 / −2 | −108 / −2 | −110 / −2 |
| `mouse.inc` | −106 / 0 | −90 / 0 | −114 / 0 |
| `files.inc` | 0 / −97 | 0 / −94 | 0 / −97 |
| `driver.inc` | −23 / −54 | 0 | −23 / −128 |
| `vidsel.inc` | −60 / 0 | 0 | −61 / 0 |
| `sched.inc` | −58 / 0 | −58 / 0 | −60 / 0 |
| `loader.inc` | 0 | 0 / −20 | 0 / −57 |
| `mod.inc` | 0 / −14 | 0 / −39 | 0 / −44 |
| `diskw.inc` | 0 / −31 | 0 / −40 | 0 / −21 |
| `disk.inc` | +1 / **+115** | 0 / −34 | 0 / −14 |
| `vga12.inc` | −30 / −4 | −18 / 0 | −30 / −4 |
| `dock.inc`, `ctrl.inc` | −27, −24 | −23, −24 | −27, −24 |
| `instance.inc` | +24 / 0, `.bss` −16 | +24 / 0, `.bss` −16 | +24 / 0, `.bss` −16 |
| `events.inc`, `viddet.inc` | +21, +20 | +21, −9 | +21, +21 |
| `lz.inc` | 0 / +43 | 0 / +43 | 0 / +46 |
| `fsck.inc` | 0 / +47, `.bss` +14 | - | 0 / +64, `.bss` +14 |
| `memory.inc` | −14 / +64 | −2 / +101 | −14 / +88 |
| `toast.inc` | +88 / 0, `.bss` +31 | −7 / 0 | +88 / 0, `.bss` +31 |
| `ui.inc` | **+125** / 0 | **+110** / 0 | **+125** / 0 |
| `fprog.inc` | **+160** / 0 | - | **+160** / 0 |

## How close B stands to the next rung

| | image rung | top / cold rung |
|---|---:|---:|
| `kern_big` at A | 231 left | top 403 left |
| `kern_big` at B | **93 left** | top 387 left |
| `kern_small` at A | 132 left | top 379 left |
| `kern_small` at B | 127 left | top 487 left |
| `kern_emu` at A | 487 left | top 403 left |
| `kern_emu` at B | 351 left | top 387 left |
| `kern_mega` at A | 204 left | cold 160, low 417, vgabuf 176, `.mega` 110 |
| `kern_mega` at B | 443 left | cold 439, **low 62**, vgabuf 176, `.mega` 330 |

Per CLAUDE.md's banner, that decides WHEN a machine pays and never what a
change cost. Every shipped kernel is a rung smaller at B, so each now stands
nearer the top of a lower rung: `kern_big`'s image has 93 bytes before it is
back where it was at A, and `kern_mega`'s low rung 62.

**Guard 5**, before a kernel cannot BOOT: `kern_big` 23,552 (23,040 at A),
`kern_small` 61,952 (61,440), `kern_emu` 22,528 (22,016) on 128 KB, and
`kern_mega` 511,488 in 640 KB (509,952).

## The on-demand modules

Image unpacked plus bss, as the image header records it, at the endpoints.
**The claim changed its unit in this round**: A rounds a claim up to whole
KB, B to the 512-byte granule (SPEC.md 50.2.1, `aafe18bc`). Every module not
listed is byte-identical.

| module | kernel | A | B | **B − A** | claim, A → B |
|---|---|---:|---:|---:|---|
| `STIKMENU.DRV` | `kern_big`, `kern_emu` | 14,780 | 7,814 | **−6,966** | 15 KB → **8,192** |
| `STIKMENU.DRV` | `kern_mega` | 15,549 | 8,241 | **−7,308** | 16 KB → **8,704** |
| `FSCHECK.DRV` | `kern_big` | - | 5,919 | **new** | → 6,144 |
| `FSCHECK.DRV` | `kern_emu`, `kern_mega` | - | 5,908, 5,904 | **new** | → 6,144 |
| `HIBER.DRV` | `kern_big`, `kern_emu` | 7,169 | 7,808 | +639 | 8 KB → 8,192 |
| `HIBER.DRV` | `kern_mega` | 11,265 | 11,552 | +287 | 12 KB → 11,776 |
| `CTRL.DRV` | `kern_big`, `kern_emu` | 15,092 | 15,338 | +246 | 15 KB → 15,360 |
| `CTRL.DRV` | `kern_mega` | 15,426 | 15,538 | +112 | 16 KB → 15,872 |
| `CTRL.DRV` | `kern_small` | 5,619 | 5,652 | +33 | 6 KB → 6,144 |
| `DISKOPS.DRV` | `kern_big`, `kern_emu` | 10,135 | 10,231 | +96 | 10 KB → 10,240 |
| `DISKOPS.DRV` | `kern_mega` | 11,647 | 11,406 | −241 | 12 KB → 11,776 |
| `WIN95.DRV` | `kern_big`, `kern_emu` | 1,440 | 1,566 | +126 | 2 KB → 2,048 |
| `WIN95.DRV` | `kern_mega` | 1,516 | 1,607 | +91 | 2 KB → 2,048 |
| `FORMAT.DRV` | `kern_mega` | 3,551 | 3,432 | −119 | 4 KB → 3,584 |
| `DOCK.DRV`, `EXTD.DRV` | `kern_mega` | 2,330, 1,897 | 2,323, 1,890 | −7, −7 | 3 KB → 2,560, 2 KB → 2,048 |
| `FILECP.DRV`, `FDLG.DRV` | `kern_small` | 1,821, 1,587 | 1,886, 1,600 | +65, +13 | 2 KB → 2,048 |

**`STIKMENU.DRV` halves**: its own size pass (`84c2acbb`, 14,780 → 7,888,
and `f19fa9f2` on disk), a 15 KB claim → 8 KB while the Sticky Apps menu is
up. **`FSCHECK.DRV` is new**: System Check's checker and defragmenter, an
image `os88mod.py` cuts with no `mod_tab` row (SPEC.md 118.14.1), read while
the check runs.

## The loadable drivers

Image + bss as the header records it, `make` at the endpoints; a driver is
the same file on every kernel. The claim's unit moved with the granule, and
so did `os88drv.py`'s strip rung (DRV_VER 11 → 12), which is where the +512s
come from.

| driver | A | B | **B − A** | claim, A → B |
|---|---:|---:|---:|---|
| `ETHER.DRV` | 16,400 | 16,901 | +501 | 17 KB → 17,408 |
| `CDROM.DRV` | 8,202 | 8,714 | +512 | 9 KB → 9,216 |
| `VESA.DRV` | 7,177 | 7,689 | +512 | 8 KB → 8,192 |
| `RAMDISK.DRV` | 5,220 | 5,331 | +111 | 6 KB → 5,632 |
| `SOUND.DRV` | 10,497 | 10,511 | +14 | 11 KB → 10,752 |
| `HTTPFS.DRV` | 5,135 | 5,127 | −8 | 6 KB → 5,632 |
| `SERIAL.DRV` (`kern_mega`'s) | 1,021 | 1,000 | −21 | 1 KB → 1,024 |

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at `main` (A) | 97,280 | 59,904 | 97,792 | 134,144 at boot, 65,024 resident |
| `KERN_SIZE` at B | **96,768** | **59,392** | **97,280** | **132,608** at boot, **62,976 resident** |
| resident bytes, ours against `main` | **−358** | **−615** | **−360** | **−2,211** conventional, `.mega` +272, **−2,048** once switched |
| ...of which size passes | −1,372 | −826 | −1,374 | −2,062 conventional (passes 16, 17 and mega 2), −1,024 once switched |
| ...of which the tables' move | - | - | - | −1,115 conventional, −1,536 once switched |
| ...of which features | +1,014 | +211 | +1,014 | +966 conventional, +512 once switched |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | 68,096 |
| spare | **4,096** (8 steps) | **4,608** (9 steps) | **3,584** (7 steps) | **5,120** (10 steps) |
| image rung left | 93 | 127 | 351 | 443 |

**In one line each:**

- **`main`**'s side carries #322's +20, which `elendilon` merged too, so it
  is not this round's.
- **We** saved **358 bytes on `kern_big`** and **615 on `kern_small`**,
  with two size passes worth −1,372 on `kern_big` paying for the toast queue
  and `OSAPI_FS_OP` (+470), the staged page-crossing sector (+180), the
  granule (+160) and System Check's door (+118), and every shipped kernel is
  a rung smaller.
- **`kern_mega`** keeps **2,048 bytes less** once switched, 1,536 of it the
  tables moving above 1MB, and its segment overhead over `kern_big` fell
  3,627 → 3,250.
- **`STIKMENU.DRV`** halves (15 KB → 8 KB), and **`FSCHECK.DRV`** is new at
  6 KB while a check runs.

## Found on the way

- **`docs/KERNEL-MEMORY.md`'s baselines described neither A nor B.**
  `elendilon`'s were blessed before #326's own re-bless, so `kernsize` at B
  read *"was 60"* left in `kern_big`'s image rung against A's real 231, and
  `kern_mega` *"was 4,096 [−1,024]"* where A was 3,072. **All four
  re-blessed at B in the same commit as this file.**
- **Two stretches of history do not assemble** (above): 27 heap-granule
  commits on `kern_mega` and the merge `cd1a0a88` on every kernel. Neither
  reached a tip; both are fixed later on their own branches.
- **The ABI lock did not bump**: `PKG_FMT` stays 10 and `DRV_VER` is 12,
  the one break of the round being `elendilon`'s own granule decision
  (DRV_VER 11 → 12), recorded in `tools/abi.lock.json`.
- **`main`'s tip builds here**, and so does B: `make`, `make small`,
  `make emu` and `make mega` clean. `make budget-check` passes at B on all
  four budgeted kernels.
