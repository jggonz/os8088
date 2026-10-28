# Kernel bytes since the #299 squash, by concept

**A measurement, not a description.** Taken 2026-10-26 on a four-core cloud
container: `nasm` 2.16.01, Python 3.13. Every resident figure comes from
`tools/kernsize.py --json` (and `--modules` for the per-file tables), which
RE-ASSEMBLES the kernel rather than reading a build. Each of the **351
non-merge commits** of the round, every parent they name, and every merge,
its two parents and their merge base, was measured in a clean tree of its
own: `git archive` of that commit's `kernel/`, `boot/`, `tools/`,
`drivers/`, Makefile and the top-level files of `apps/`, `apps/stiklib.asm`
assembled there into the `stiklib.bin` that `deskmod.inc` includes, and that
commit's own `kernsize` measuring **`kern_big`, `kern_small`, `kern_emu` and
`kern_mega`**. A tree whose kernel inputs hash the same as one already
measured takes that tree's figures: **225 distinct kernel trees** in all.
`associco.inc` is B's in every tree (`tools/os88mini.py` and the four
packages' icons did not move in the round). Module and driver images come
from `make`, `make small` and `make mega` at the three endpoints only. It is
true of the commits it names and of no other tree.

It is the thirteenth of its family, after
`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-22.md`, and is an edit of none
of them. It is taken for the next `elendilon -> main` round, so the question
is that PR's: **what does this round cost `main`'s kernel?** This round
**is** a size pass, kernel size pass 14
(docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md, `ksp14/`), so for the
first time in the family the answer is a NEGATIVE number on every kernel.

## The points

| | commit | what it is |
|---|---|---|
| **A** | `e631e0ab` | **`main` at the cut**: #299 (*Elendilon -> Main, kern_mega …*, `9cbbb93b`) plus #300, #289 and #301-#303 |
| **C** | `b7fb2a37` | **`main` at its tip**: A plus six commits - XTMP3 (#304, #308), SOUND.DRV's XT IRQ 5 refusal (#307), Browser 1.5-1.6c (#306), the shared agent skills (#309) and The Wire's level-4 fence (#310) |
| **B** | `a54fb773` | **`elendilon-pr`**: `origin/elendilon` (`290ddaf1`) with C merged in. Elendilon was cut from A as `553f2a21` (*elendilon squashed onto main, after #299, #289, #301-#303*) and took `main` in once before (`c6c8d83b`); `a54fb773` brings #310, the one commit it did not have. C is wholly inside B, so B's kernel IS the squash a PR would make |

**The round is `e631e0ab..a54fb773`: 351 commits that are not merges** - 6
of them `main`'s (A..C) and 345 ours - and 93 merges.

**The build number is held at 708 for every tree**: `BUILD_STR` is the
commit count (SPEC.md 14.2), and a digit is not a cost anybody spent.

**The budgets did not move.** `KERN_BUDGET` is 100,864 on `kern_big` and
`kern_emu` and 64,000 on `kern_small`, with `KERN_GRACE` 2,048.
**`make budget-check` passes at B on all three shipped kernels** - 3,584,
4,096 and 3,072 spare - so B could go to `main` as it stands. `kern_mega` is
a `KERN_KNOB` build: no `KERN_BUDGET` binds it.

## Headline: `kern_big`, the shipped default

| section | A = C, `main` | B | **B − C** (ours) |
|---|---:|---:|---:|
| `.text` | 44,122 | 41,064 | **−3,058** |
| `.bss` | 5,312 | 5,398 | **+86** |
| `.cold` | 37,939 | 37,644 | **−295** |
| `.vga` | - | 2,032 | **+2,032** (new) |
| `.lowbss` | 5,486 | 5,358 | **−128** |
| `.vgabuf` | 336 | 336 | 0 |
| **resident** | 93,195 | **91,832** | **−1,363** |
| `.ovl` (window) | 5,118 | 5,110 | −8 |
| `.boot2` (blob) | 5,925 | 6,444 | +519 |
| **`KERN_SIZE`** | 99,328 | **97,280** | **−2,048** |
| spare of the budget | 1,536 | 3,584 | |

**`main` spent nothing.** Its six commits move no kernel byte on any kernel
(#307's `kernel/driver.inc` change is `DRVM_IMG_SND` 11 -> 12, an `equ` that
names a driver's size). A = C to the byte, so C − A is not a column.

**Ours is −1,363 resident and four rungs of `KERN_SIZE`.** Kernel size pass
14 took −1,851 (with the four of its merges that resolve cross-branch items),
and the round's features spent +488 against it - the largest the movable
`.cold` waves' +129 and the squash that opened the round's +113.

**`.text` −3,058 is two different things.** −2,032 of it MOVED, into the new
VGA rung (SPEC.md 2.10.9, the movable `.cold` waves 3a and 3b), and is still
resident on a VGA or EGA machine; −1,026 is the size pass. What the rung buys
is a MONO machine: on CGA, Hercules or MDA `.cold` slides down over it at
boot, so **the heap floor of a 1bpp machine falls by 2,032 bytes on top of
the −1,363** (the ladder's `VGA 0x0e4f -> COLD 0x0ece`, 127 paragraphs).

**The boot blob grew a sector.** `BOOT2_SECS` is 13 where A had 12:
`ksp14 vesa` (`a30df75f`) moved VESA.DRV's attach (`vx_boot_x`) and its
pointer block out of resident `.text`/`.cold` into a thirteenth blob sector,
and `ksp14 reach` moved `vid_apply` and `mod_init` into the blob after it -
that is the +519 above, and none of it is resident. `t_blobruns` priced the
thirteenth sector at no extra `int 13h` on any geometry (kernel.asm's
`BOOT2_SECS` comment). `kernsize`'s blob is 6,444 of the 6,656 thirteen
sectors hold.

## Headline: `kern_small`, the 128KB floor machine

| section | A = C | B | **B − C** |
|---|---:|---:|---:|
| `.text` | 31,355 | 30,796 | **−559** |
| `.bss` | 2,993 | 2,969 | **−24** |
| `.cold` | 22,052 | 21,541 | **−511** |
| `.lowbss` | 2,868 | 2,868 | 0 |
| **resident** | 59,268 | **58,174** | **−1,094** |
| `.ovl` (window) | 1,486 | 1,482 | −4 |
| `.boot2` (blob) | 4,381 | 4,562 | +181 |
| **`KERN_SIZE`** | 61,952 | **59,904** | **−2,048** |
| spare of the budget | 2,048 | 4,096 | |

**Size pass 14 took −1,074 here** (−1,070 with its merges), and the largest
single cut on the floor machine is `ksp14 reach` putting kern_small's button
in `CTRL.DRV` and `FDLG.DRV` at their callers (−298, `4a0baaeb`) and its
`vid_apply` in the blob (−196, `0e2eb5cf`). Against it the round spent +34
(the mouse contest), +31 (the sector cache) and +6 (movable `.cold` wave 2),
and the squash that opened the round SAVED 95 there.
The blob stays at eleven sectors.

**`kern_small`'s image rung has 27 bytes left at B** (it had 468 at A). Per
CLAUDE.md's banner that decides WHEN the machine pays and never what a change
cost - but the next 28 bytes of `.text` or `.bss` anybody spends there are
billed 512.

## `kern_emu`

`kern_big` plus SPEC.md 9.11's absolute pointer. Resident **92,088** at B
against 93,464 at C: **−1,376**, which is `kern_big`'s −1,363 and −13 more
from size-pass cuts that land differently around its extra code. `KERN_SIZE` 99,840 ->
**97,792**, 3,072 spare. Its blob is a sector longer than `kern_big`'s again
(the knob's block), as `BOOT2_SECS`'s comment says.

## `kern_mega`, the 286+ protected-mode kernel

| section | A = C | B | **B − C** |
|---|---:|---:|---:|
| `.text` | 47,542 | 46,880 | **−662** |
| `.bss` | 5,348 | 5,396 | **+48** |
| `.cold` | 42,531 | 42,284 | **−247** |
| `.lowbss` | 7,257 | 7,257 | 0 |
| `.vgabuf` | 336 | 336 | 0 |
| conventional resident (the five above) | 103,014 | **102,153** | **−861** |
| `.mega` | 14,623 | 14,785 | +162 |
| `.mega`'s scratch | 1,479 | 1,493 | +14 |
| **`KERN_SIZE` at boot** | 125,952 | **124,928** | **−1,024** |
| **resident once switched** | 66,560 | **66,048** | **−512** |

kern_mega has no VGA rung (`COLD_SLIDE` is kern_big's and kern_emu's), so
the planar code stays in its `.text`. Its `.mega` +162 is a reflection
paying its periods to the rate hook (`68d9f13d`, +85), the squash (+75) and
the keyboard rows (+2). What binds it:

- **the 64 KB segment**: `tools/os88megaseg.py` at B reads `.text`+`.bss`
  52,276, **13,260 left** (12,646 at A), and kern_mega carrying **3,782
  bytes beyond `kern_big`** there (3,456 at A) - `kern_big`'s VGA rung now
  counts in its window (`ccf528c1`), so the two figures are not the same
  measure across the round; `tests/megaseg.txt` still holds the ratchet at
  4,029;
- **above 1MB**: `.cold` 42,284 + `.mega` 14,785 + its scratch 1,493 =
  58,774 of the 110,592 between `MEGA_COLD_LIN` and `MEGA_XPOOL`, **51,818
  left**;
- its rungs at B: image 460 left, cold 212, low 423, `.mega` 106.

## The attribution is exact

Every non-merge commit was measured against its first parent; every merge M
was measured as `M − P1 − (P2 − base(P1, P2))`. **The commits and the merges
sum to `a54fb773 − e631e0ab` exactly, in every section, on all four
kernels** (checked, not assumed). **285 of the 351 commits move no kernel
byte.** The merges' remainder is four merges of kernel size pass 14's own
branches resolving their cross-file items:

| merge | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|
| `84064932` *kernel-size-14 into ksp14-api, and reach's cross-file items* | `.cold` −83, blob −97 | | `.cold` −83, blob −96 |
| `89a78a47` *no-pinning (heap compaction) into kernel-size-14* | `.text` +8 | | |
| `2d3de3ac` *ksp14-api: the API table and driver-table additions* | | `.text` +2 | |
| `0c990066` *kernel-size-14 (nopin) into ksp14-fold* | `.cold` +2 | `.cold` +2 | `.cold` +2 |
| **the merges** | **−73** | **+4** | **−81** |

## Our arm, by concept (B − C)

Resident bytes (`.text`+`.bss`+`.cold`+`.vga`+`.lowbss`+`.vgabuf`).

| concept | commits | **`kern_big`** | **`kern_small`** | **`kern_emu`** | **`kern_mega`** |
|---|---|---:|---:|---:|---:|
| **Kernel size pass 14**, with its four merges | 45 + 4 merges | **−1,851** | **−1,070** | **−1,864** | **−1,304** |
| **Movable `.cold`**: modules re-point themselves, the ladder's top packed, the VGA rung and the slide (SPEC.md 2.10.9) | `d9737d88`, `32687c49`, `71173cad`, `636ce3ed`, `ccf528c1` | **+129** (`.text` −1,987, `.vga` +2,032, `.cold` +84) | +6 | +129 | 0 |
| **The squash that opened the round**: elendilon's work after #299 onto `main` - the menu-bar rework, HTTPFS.DRV's row, `OSAPI_IMG_NAME`/`OSAPI_ARG_TEXT`, VESA's `fpg_hide` arm | `553f2a21` | **+113** (`.text` −151, `.bss` +63, `.cold` +201) | **−95** | +113 | +109 (`.mega` +75) |
| **No-pinning**: only the UI task compacts, DMA pins are the driver's (K8, K1a, K2), a worker's call into an image is counted (K9), `fmv_icorefs` | `8c209a11`, `e598b802`, `4bc6dbc5` | **+108** | 0 | +108 | +108 |
| **The sector cache**: a chunk being read is spared once, the second chance is earned by a visit, BIG.BMP's first rows (SPEC.md 18.x) | `2fb4a67a`, `ea5a8f67`, `159991b5` | **+77** (`.cold`) | **+31** | +77 | +77 |
| **The mouse contest**: a port reporting line errors cannot win it (SPEC.md 9.5.6) | `61b4054d` | **+34** | **+34** | +34 | +34 |
| **The keyboard from `int 15h AH=4Fh`** (SPEC.md 9.7.2), its size pass, and kern_mega's int 09h shape | `d2bc6957`, `bf1c4344`, `2b6d4d43` | **+20** | 0 (blob −19) | +20 | **+88** (`.mega` +2) |
| **ETHER.DRV's socket pool on demand** | `4a8085f7` | **+15** | 0 | +15 | +19 |
| **`vid_depth_set`**: the VESA arm joins the shared stores | `f953b6cf` | **−8** | 0 | −8 | 0 |
| **kern_mega's own**: `megakbtrip`'s IRQ1, a reflection's rate-hook periods | `c704b42f`, `68d9f13d` | 0 | 0 | 0 | +8 (`.mega` +85) |
| **SOUND.DRV with no mixer hands the popup back** | `b87538cf` | 0 (blob +40) | 0 | 0 (blob +40) | 0 (blob +40) |
| the other 279 | | 0 | 0 | 0 | 0 |
| **ours** | 66 of 345 | **−1,363** | **−1,094** | **−1,376** | **−861** |

**Every feature of the round together spent +488 on `kern_big`**, and the
size pass took that back almost four times over. On `kern_small` they come to
−24: +71 of features against the squash's −95.

### Kernel size pass 14, by theme

Its own record is docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md and
`docs/plans/completed/ksp14/`. Resident, commits only (the merges above are
−73 / +4 / −81 more):

| theme | commits | `kern_big` | `kern_small` | `kern_mega` |
|---|---:|---:|---:|---:|
| `vesa` - the hook is the far call itself, VESA.DRV's attach into the blob, one depth compare per body, the mouse ISR stack back to 128 | 9 | **−490** (blob +181) | 0 | 0 |
| `reach` - single-caller code written at its callers or moved to the blob, `CTRL.DRV`, `FDLG.DRV` or `EXTD.DRV` | 11 | **−330** (blob +288) | **−552** (blob +208) | −332 |
| `fold` - shared tails and fall-through entries, `OSAPI_DRV_DLG`'s body into `CTRL.DRV`, relocation procs as `mem_rr_tab` rows | 9 | **−280** | −92 | −258 |
| `files` - the format's reach test into `FORMAT.DRV`, single-caller routines in line | 7 | **−250** | −141 | −250 |
| `menubar` - the bar's composition, the speaker, the progress widget, `menu_tbuf` | 8 | **−215** | −157 | −223 |
| `api` - inline cells, the drivers' titles out of resident | 6 | **−159** (blob +79) | −23 | −104 |
| `serial` - the lease's resident half, the serial ISR falling into the mouse's | 5 | −37 | −35 | −35 |
| `nopin` - the no-pinning hunks size-passed | 3 | −17 | −74 | −17 |
| the rest (a retired class's table, `mem_rr_tab`'s VESA defect, the loading-screen field, notes) | 10 | 0 | 0 | −4 |
| **the pass** | 68 | **−1,778** | **−1,074** | **−1,223** |

(The pass's commit count here is every commit whose subject names it, the
documentation ones included; 45 of them move a byte.)

## Where the bytes are, by file

`kernsize --modules`, A → B, `kern_big` and `kern_small`, every row that
moved. **This table cannot see the VGA rung**: `kernsize --modules` has no
`.vga` column (see *Found on the way*), so a file whose code moved into the
rung reads as a `.text` saving here. On `kern_big` that is `vga12.inc` most
of all (−1,716) and `icons.inc` and `mouse.inc`'s save-under and cursor
(wave 3b) - 2,032 bytes in all, which are still resident on a VGA.

| file | `kern_big` `.text` / `.cold` / `.bss` / `.lowbss` | `kern_small` `.text` / `.cold` / `.bss` |
|---|---|---|
| `vga12.inc` | **−1,716** / 0 / 0 / 0 *(the rung)* | - |
| `viddet.inc` | −227 / 0 / 0 / **−696**, blob +224 | −195 / 0 / 0, blob +213 |
| `mouse.inc` | −130 / 0 / 0 / **+568** | +32 / 0 / 0, blob −19 |
| `menu.inc` | **−414** / 0 / +36 / 0 | **−304** / 0 / −2 |
| `vidext.inc` | −143 / −120 / 0 / 0, blob +182 | - |
| `icons.inc` | −163 / 0 / 0 / 0 *(mostly the rung)* | - |
| `files.inc` | +5 / −136 / 0 / 0 | 0 / −120 / 0 |
| `driver.inc` | −34 / −129 / +38 / 0, blob +92 | - |
| `ui.inc` | −105 / 0 / 0 / 0 | −8 / 0 / 0 |
| `wm.inc` | −92 / +25 / 0 / 0 | −92 / −46 / −18 |
| `fdlg.inc` | - | 0 / **−298** / 0 |
| `extmod.inc` | 0 / −46 / 0 / 0 | - |
| `ctrl.inc` | −4 / −33 / 0 / 0 | −3 / −9 / 0 |
| `serial.inc` | −26 / −7 / 0 / 0 | −26 / −7 / 0 |
| `toast.inc` | −29 / 0 / 0 / 0 | −29 / 0 / 0 |
| `filecp.inc` | 0 / −27 / 0 / 0 | - |
| `desk.inc` | 0 / −25 / 0 / 0 | 0 / −8 / 0 |
| `diskw.inc` | 0 / −20 / 0 / 0 | 0 / −20 / 0 |
| `font.inc`, `dock.inc`, `loader.inc`, `assoc.inc` | −8, −6, −2 (`.cold`), −1 | `dock.inc` −6 |
| `instance.inc`, `xmem.inc`, `hiber.inc` | +4, +4, +8 (`.bss`) | −2, -, −4 (`.bss`) |
| `desksc.inc` | 0 / +22 / +2 / 0 | - |
| `fprog.inc` | **+33** / 0 / 0 / 0 | **+69** / 0 / 0 |
| `disk.inc` | +2 / +34 / 0 / 0 | 0 / −5 / 0 |
| `mod.inc` | 0 / +41 / 0 / 0 | 0 / −1 / 0 |
| `memory.inc` | +20 / +36 / +2 / 0 | 0 / +3 / 0 |
| `kernel.asm` | −18 / +82 / 0 / 0, blob +34 | +5 / 0 / 0 |

`viddet.inc`'s −696 and `mouse.inc`'s +568 of `.lowbss` are one buffer
changing owner: `ksp14 vesa` (`cb0484c3`) put the mouse ISR's stack back to
128 bytes over the row table, and the net is the −128 in the headline.

## How close B stands to the next rung

| | image rung | top rung |
|---|---:|---:|
| `kern_big` at A = C | 230 left | (the old ladder: cold 461, low 146) |
| `kern_big` at B | **130 left** | 198 left |
| `kern_small` at A = C | 468 left | (cold 476, low 204) |
| `kern_small` at B | **27 left** | 167 left |
| `kern_emu` at B | 386 left | 198 left |
| `kern_mega` at B | 460 left | (cold 212, low 423, `.mega` 106) |

Movable `.cold` wave 2 (`32687c49`) packed the top of `kern_big`'s ladder
into ONE rounding, so its `cold`/`low` rungs are one `top` rung at B and the
two rows are not the same measure.

**Guard 5**, before a kernel cannot BOOT on 128 KB: `kern_big` 23,040 (21,504
at A), `kern_small` 61,440 (59,392), `kern_emu` 22,016 (20,480).

## The on-demand modules

Image unpacked plus bss, `make` / `make small` / `make mega` at the
endpoints. The claim is the image rounded up to whole KB. Every module not
listed is the same at A, C and B.

| module | A = C | B | **B − C** | claim, KB |
|---|---:|---:|---:|---|
| `CTRL.DRV` | 14,647 | 15,266 | **+619** | 15 → 15 |
| `HIBER.DRV` | 8,401 | 8,817 | **+416** | 9 → 9 |
| `EXTD.DRV` | 1,518 | 1,837 | **+319** | 2 → 2 |
| `SERIAL.DRV` | 781 | 909 | **+128** | 1 → 1 |
| `WIN95.DRV` | 1,590 | 1,600 | +10 | 2 → 2 |
| `DISKOPS.DRV` | 12,181 | 12,183 | +2 | 12 → 12 |
| `STIKMENU.DRV` | 14,874 | 14,876 | +2 | 15 → 15 |
| `FORMAT.DRV` | 4,539 | 3,195 | **−1,344** | 5 → **4** |
| **`kern_small`** `CTRL.DRV` | 4,946 | 5,498 | **+552** | 5 → **6** |
| **`kern_small`** `FDLG.DRV` | 2,564 | 2,868 | **+304** | 3 → 3 |
| **`kern_small`** `SERIAL.DRV` | 781 | 909 | +128 | 1 → 1 |
| **`kern_small`** `FORMAT.DRV` | 1,247 | 1,279 | +32 | 2 → 2 |
| **`kern_small`** `FILECP.DRV` | 5,085 | 5,070 | −15 | 5 → 5 |
| **`kern_small`** `DISKOPS.DRV` | 7,968 | 7,932 | −36 | 8 → 8 |

- **The module growth is mostly the size pass's own `reach` and `fold`**,
  which is what it is for: code that was resident on every machine moved
  into the module the only callers live in. `CTRL.DRV` took `OSAPI_DRV_DLG`'s
  body (`e96e6906`) and, on `kern_small`, the button (`4a0baaeb`) - which is
  the one row that **crosses a KB on a claim** (5 → 6 KB while the Control
  Panel is open on the floor machine). `EXTD.DRV` took `ui_drag_dead` and
  `ui_drag_phase`'s second-display arm, `FDLG.DRV` the button.
- **`FORMAT.DRV` −1,344** on both `kern_big` and `kern_mega`.
- kern_mega's own `HIBER.DRV` moves by the same +416 (11,473 → 11,889).

**`kern_mega`'s modules at B against A**: `CTRL.DRV` +477, `HIBER.DRV`
+416, `EXTD.DRV` +319, `SERIAL.DRV` +149, `FORMAT.DRV` −1,344, `DISKOPS.DRV`
−6, `STIKMENU.DRV` +2.

## The loadable drivers

Image + bss, `make` at the endpoints. Every driver not listed is the same.

| driver | A | C | B | **C − A** (`main`) | **B − C** (ours) |
|---|---:|---:|---:|---:|---:|
| `SOUND.DRV` | 11,134 | 11,907 | 12,045 | **+773** | +138 |
| `HTTPFS.DRV` | - | - | 5,490 | | **new** |
| `HTTPPAGE.DRV` | - | - | 2,350 | | **new** |
| `ETHER.DRV` | 16,986 | 16,986 | 17,408 | 0 | **+422** |
| `VESA.DRV` | 7,873 | 7,873 | 7,903 | 0 | +30 |
| `HDDTOOL.DRV` | 18,322 | 18,322 | 16,510 | 0 | **−1,812** |
| `XMEM.DRV` | 2,098 | 2,098 | 2,042 | 0 | −56 |
| `NET.DRV`, `RAMPAGE.DRV`, `SAVER.DRV`, `THMPAL.DRV` | | | | 0 | −6 each |

**`main`'s only bytes this round are a driver's**: #307's IRQ 5 refusal on an
XT took `SOUND.DRV` +773 and its claim 11 → 12 KB (hence the `DRVM_IMG_SND`
row). `HTTPFS.DRV` and `HTTPPAGE.DRV` arrive with the squash (SPEC.md 122),
and `HDDTOOL.DRV`'s claim falls 18 → 17 KB.

## The final bill

| | `kern_big` | `kern_small` | `kern_emu` | `kern_mega` |
|---|---:|---:|---:|---:|
| `KERN_SIZE` at `main` (C) | 99,328 | 61,952 | 99,840 | 125,952 at boot |
| `KERN_SIZE` at B | **97,280** | **59,904** | **97,792** | **124,928** at boot, **66,048 resident** |
| resident bytes, ours against `main` | **−1,363** | **−1,094** | **−1,376** | **−861** conventional, `.mega` +162 |
| ...of which kernel size pass 14 | −1,851 | −1,070 | −1,864 | −1,304 |
| ...and on a 1bpp machine, the VGA rung besides | **−2,032** | - | −2,032 | - |
| `KERN_BUDGET` | 100,864 | 64,000 | 100,864 | none (a knob) |
| spare | **3,584** (7 steps) | **4,096** (8 steps) | **3,072** (6 steps) | segment 13,260, above 1MB 51,818 |
| image rung left | 130 | **27** | 386 | 460 |

**In one line each:**

- **`main`** spent **no kernel byte** since the #299 squash; its six commits
  cost `SOUND.DRV` +773 and a KB of every sound machine's heap.
- **We** spent **−1,363 on `kern_big`** and **−1,094 on `kern_small`**,
  FOUR 512-byte rungs of `KERN_SIZE` on each, and a 1bpp `kern_big` machine
  gets 2,032 bytes more again from the VGA rung. Kernel size pass 14 is
  −1,851 / −1,070 of it; the round's features together spent +488 on
  `kern_big`.
  The boot blob is a sector longer on `kern_big` and nothing of it is
  resident. On `kern_small` the round's features net −24.

## Found on the way

- **`main`'s tip does not build here.** A plain `make` at `b7fb2a37` fails in
  `tools/x3xtgen.py` (`build/x3xtk.inc`, XTMP3's XT kernels): the generated
  source trips nasm 2.16.01's `-w+error=number-overflow` (*byte data exceeds
  bounds*) at its `kernel_sizes` self-measurement, twice in a row. A builds,
  and so does B - `x3xtgen.py` was reworked on elendilon
  (`58ff6334`, *XTMP3: pay down the pin debt*, and the `c6c8d83b` merge) - so
  the squash fixes it for `main`, but `main` as it stands is red on this
  toolchain. C's driver and module images above come from `make -k`, which
  built everything but XTMP3's package.
- **`kernsize --modules` has no `.vga` column.** Movable `.cold` wave 3 gave
  `kern_big` a seventh resident section and the per-module pass did not grow
  with it, so the table attributes the rung's 2,032 bytes to nobody - its
  `.text` column sums to 41,064, and the rung is invisible there. The
  headline (`--json`) is right; only the per-file split cannot see it.
- **`docs/KERNEL-MEMORY.md`'s baselines did not describe B** - `kernsize`'s
  "was" figures were A's (1,536 spare on `kern_big`, 2,048 on `kern_small`).
  **Re-blessed at B in the same commit as this file**, `kern_big`,
  `kern_small` and `kern_emu`.
