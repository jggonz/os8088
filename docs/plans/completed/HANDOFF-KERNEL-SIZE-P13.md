# Kernel size pass 12: the record, and what is left for a thirteenth

**PASS 12 HAS LANDED** on `kernel-size-12`, cut from `elendilon` at
`47578d58`. The file is named `-P13` for the reason the three records before
it are named one ahead: each pass's record is named for the pass it hands
to. Its companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P12.md`**: pass 11's record.
* **`docs/plans/completed/ksp12/`**: the brief every agent read
  (`BRIEF.md`) and the eight agents' own notes, one file each: every item
  TAKEN with its bytes on each kernel, every item REFUSED with the bytes it
  would have been and why, the ABI breaks each one wanted, and the
  cross-file lists. **Read the refused lists before re-proposing anything.**
* **`docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-14.md`**, *What has not
  had a pass*: the table this pass worked from.
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand, blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The owner's brief: *"the ~1k of kernel code added that has not yet had any
size pass. The goal is a 30% reduction, although more would be happily
accepted. Optimizations spotted while working are all in scope. Today we are
trying to not break ABIs (which is a difference from a normal pass), but if
anyone spots an optimization that would work better with a break then record
it, and it will likely be taken in the next pass. ABIs not yet on main CAN be
broken."*

**What had not had a pass**, resident (`.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`, `tools/kernsize.py --json`):

| | kern_big | kern_small | ABI |
|---|---:|---:|---|
| (a) the 2026-10-14 report's *never passed* table (Mount a `.IMG`, the Windows theme, two file classes, the REHOME keep form, Force Color, `OSAPI_FERR_TEXT`, `ICON_SOLID`, a failed mount, the `DA` key / HDD autoload / the rename, `LD_EVER`, port pins) | +865 | +54 | on `main` (#252): FROZEN |
| (b) since the report, `b6acc4f7`..`47578d58` (the ROM sockets, the HMA, `OSAPI_WM_QUIET`, FTP's send window, the Video Player's display, Uncompress To) | +435 | +178 | not on `main`: free |
| **total** | **~1,300** | **~232** | |

(b) had crossed kern_big's cold rung: KERN_SIZE 98,816 -> 99,328.

**The outcome**, `47578d58` against the close (`4a00d618`):

| | base | close | Δ |
|---|---:|---:|---:|
| **kern_big resident** | 93,574 | **93,085** | **-489** (37.6% of ~1,300) |
| kern_big `.text` / `.bss` / `.cold` | 43,738 / 5,212 / 38,690 | 43,640 / 5,204 / 38,307 | -98 / -8 / -383 |
| kern_big `.boot2` / `.ovl` | 4,884 / 5,102 | 4,873 / 5,114 | -11 / +12 |
| kern_big `KERN_SIZE` | 99,328 | **98,816** | **-512** (the cold rung (b) crossed, uncrossed) |
| **kern_small resident** | 60,413 | **60,215** | **-198** (85% of ~232) |
| kern_small `.text` / `.bss` / `.cold` | 31,967 / 3,063 / 22,515 | 31,870 / 3,063 / 22,414 | -97 / 0 / -101 |
| kern_small `KERN_SIZE` | 62,464 | 62,464 | 0 |
| kern_emu resident | 93,833 | 93,344 | -489 |
| the ROM window (kern_big, `.cold` + tail + spare) | 2,254 for tail and spare | 2,637 | **+383**, and the window now has room for a module it could not hold |

kern_big now stands **54 bytes BELOW** the 2026-10-14 report's blessed
baseline: everything (b) added is paid for, and 54 bytes of (a) with it.

Loadable images (UNPACKED, which is what lives in RAM):

| image | base | close | Δ | claim |
|---|---:|---:|---:|---|
| `CTRL.DRV`, kern_big | 12,346 | 12,089 | **-257** | **13 KB -> 12 KB** |
| `CTRL.DRV`, kern_small | 4,406 | 4,383 | -23 | 5 KB |
| `DISKOPS.DRV`, kern_big | 10,352 | 10,079 | **-273** | **11 KB -> 10 KB** |
| `DISKOPS.DRV`, kern_small | 7,184 | **7,168** | -16 | **8 KB -> 7 KB, with NO margin** (§5) |
| `WIN95.DRV` | 1,565 | 1,382 | -183 | 2 KB |
| `ETHER.DRV` | | | -44 | |
| `SOUND.DRV` | 7,183 | 7,176 | -7 | |
| `HIBER.DRV` | 5,529 | 5,528 | -1 | |
| `COLDHMA.SYS` | 2,920 | | -20 (hma) -31 (rom) | |
| `HDD.DRV`, `EXTD.DRV` | | | 0 (HDD's -5 fell into its `align 512` pad) | |

**Every merge reproduced the sum of the branches exactly** on both kernels,
read after each, bar one byte in the merge's favour (theme + imgmount).

## 1. WHO TOOK WHAT

Seven concept agents, each in a worktree cut at `df8603a1` (the brief on
`47578d58`) and owning one concept's hunks rather than a set of files; then
an eighth, `xfile`, on the merged tree for the cross-file lists, with the
round agent's files barred until that branch landed and then freed mid-round.

| agent | concept | big | small | images |
|---|---|---:|---:|---|
| **files** | two file classes, REHOME keep, `FERR_TEXT`, a failed mount, the `DA` key, HDD autoload, `LD_EVER`, port pins' resident side | -97 | -23 | |
| **hma** | `.cold` and three drivers in the HMA, `OSAPI_MEM_HMA` | -94 | -2 | `COLDHMA.SYS` -20, `SOUND.DRV` -7 |
| **theme** | the Windows theme, Force Color, `ICON_SOLID` | -83 | 0 | `WIN95.DRV` -183 |
| **xfile** | the cross-file lists, single-caller routines, parity | -68 | -62 | `CTRL.DRV` -24 |
| **imgmount** | Mount a `.IMG` | -62 | 0 | `DISKOPS.DRV` -268 / -16 |
| **rom** | `.cold` in the 5150's ROM sockets | -46 | -65 | `COLDHMA.SYS` -31 |
| **round** | `OSAPI_WM_QUIET`, FTP's send window, the Video Player's display, Uncompress To | -38 | -46 | `ETHER.DRV` -44, `DISKOPS.DRV` -5 |
| **ctrl** | `CTRL.DRV`: the Ports view, the panel halves of Force Color, the theme, `DA`, HDD autoload | 0 | 0 | `CTRL.DRV` -233 / -23 |
| merge | theme + imgmount | -1 | 0 | |
| **total** | | **-489** | **-198** | |

### 1.1 What each concept came to

* **(a), frozen**: the agents took what an internal reshape could reach and
  recorded the rest as ABI wishes (§3). Mount a `.IMG` +355 -> about +293
  (its transfer arm is a re-entrant loop with little slack; its module took
  the bigger cut, a KB of claim on both kernels); the Windows theme
  232 -> 163; Force Color 29 -> 15; `ICON_SOLID` 15 -> 15 (no shorter
  spelling); files' concepts 234 -> 137; `CTRL.DRV`'s unpassed ~1,045 -> ~790
  in bytes, and its claim a KB down.
* **(b), free**: the ROM's own cost 67/110 -> 21/45; the HMA's 245 -> 151;
  the round's ~113/~60 -> ~75/~14. The breaks taken were all inside (b):
  `ROMF_FIX`/`ROMF_PKG` renumbered, `ROMF_HPLACE` takes no CX, `HFREE`
  folded into `ROMF_HCLAIM` with CX = 0, `WN_NENT` 2 -> 1, `OSAPI_WM_QUIET`
  a bare `ret` on kern_small.

### 1.2 The shapes that recurred

* **Single-caller routines written at their one call** (xfile: ten of them,
  -56 / -51), and doors with one caller each made into a far call at that
  caller (theme, -15).
* **A compare chain that wanted a table** (`drv_cls_svc_x` on kern_big,
  DISKOPS.DRV's 9-verb ladder) - and the converse: kern_small keeps the
  chain, where a table does not pay.
* **A list a callee already filters** (`drv_release`'s four-class list -
  `dsk_vol_drop_drv_x` already drops only its class's rows; the list had gone
  stale twice).
* **A figure computed per call that boot fixes** (`osapi_sys_kb`'s VGA arm:
  stored once by `mem_init_x`; `mem_floor_ax` reading one word the ROM's
  patch walk already moves).
* **Module scratch in the claim's KB rounding** (`WIN95.DRV`'s 125 bytes in
  a no-bits `.modwb`, MODULE-SELFCONTAIN-PLAN's mechanism, asserted).
* **Two near-identical routines folded** (the Ports view's four value
  painters into `cp_pval`; the arrow-cell and button-painter tails in
  CTRL.DRV; `HFREE` into `HCLAIM`; `sk_txfold` in ETHER.DRV).
* **Copied code whose reason was false** (`5b931426` copied
  `dsk_rah_flush`'s loop into DISKOPS.DRV on a comment saying it was `.text`;
  it is `.cold` on both kernels).

## 2. HOT PATHS

None got slower. `gfx_blit1`'s planar tail is ~48 clocks faster a tail row
on VGA and ~16 on 1bpp (a Windows title bar on VGA 29.6 -> 28.9 ms,
cycle-counted); `drv_cls_svc_x` is a table lookup where it was a chain;
`dsk_runcap`'s HMA arm moved out of line, 12 clocks a run faster for heap
buffers. The costs paid on purpose, all once per operation: up to three far
calls into the ROM's `rom_none` door per module load on a kernel whose
`.cold` is in RAM (against a ~400 ms disk load), and the keypad mouse's
`kbm_paint` written at its call.

**Parity**: at `26c45e1d` (six agents merged) every hot `.text` primitive -
`gfx_blit4`, `font_run`, both mouse ISRs, `kbm_isr`, `sch_isr`,
`sch_switch` - and `dsk_xfer` and the FAT walk in `.cold` sat at an ODD
shift against the base, the theme agent's dropped table pad being the
`.text` cause. xfile put every one of them, plus `ui_task`, `dsk_runcap` and
`dsk_next_clus_x`, back at an even shift on both kernels for +2 bytes of
kern_big (§5 has the two runs still odd, both off the hot list).

## 3. WANTS AN ABI BREAK (for the next pass)

All ESTIMATED; each agent's note has the callers and what breaks.

| | big | small | breaks |
|---|---:|---:|---|
| `OSAPI_MEM_HMA` as an arm of `OSAPI_MEM_CLAIM_DMA_HI` / `OSAPI_MEM_FREE` (hma) | ~-14 | ~-6 | DMA_HI gains an AX output; `SOUND.DRV` -12 too |
| `OSAPI_PKG_REHOME` with the KEEP form only (files) | ~-16 | | `PKG_FMT`: an old `.o88` would be bounded wrongly |
| `DRVC_FILE2` renumbered below the uncopied classes (files) | ~-8 | | `DRV_VER`; `RD_CLASS`, `USBMOUSE.DRV` |
| the `'VC'` key folded into a spare bit of another record (theme; ctrl agrees, as a bit of `VM`) | -1 `.bss`, -5 blob | | `SYSTEM.CFG`: a saved Force Color lost once |
| `WIN95.DRV` renamed shorter (theme) | -2 | | a user-visible file name on every system disk |

## 4. DEFECTS FOUND

* **`drv_publish` wrote a driver pointer's segment before its offset**
  (hma, FIXED). A tick between the two stores - `snd_tick` inside IRQ0 -
  would far-call an HMA driver at segment:000C, the BIOS ROM: SOUND.DRV
  ticked on mid-session on a 286 with the HMA. Found by reading; no row
  reaches it.
* **`.img`'s past-the-end refusal left `[dsk_ioerr]` stale** (imgmount
  found, xfile FIXED for 0 bytes: it stores 04h, sector not found).
* **`tests/shutdown.py`'s write-protect leg wrote 5 bytes over the 3-byte
  `cp_cfg_save` jump**, the last thing in `.modc`, so it ran into `.modu`
  (ctrl found it the hard way, xfile FIXED: the patch now lands in
  `cpc_save`'s body and asserts it covers no label).
* **`tools/stkbalance.py` did not follow `jmp SEG:label`** as its docstring
  says, nor treat `jmp far [..]` as computed (theme, FIXED; nothing was
  hiding behind it).
* **`tools/os88rom.py` reads data labels across both `%if` arms**, so a name
  that is an `equ` in one arm and a `dw` in the other split the ROM hash's
  spans (+4 tail bytes); the rom agent gave the `dw` its own name. A trap,
  not fixed in the tool.
* **Stale documentation, FIXED**: SPEC.md 51.8's sentence on what
  `drv_release` drops; SPEC.md 2.8.7's `MODS_SIZE` (2,447 / 5 sectors; it is
  2,615 / 6); `5b931426`'s comment on `dsk_rah_flush`'s section.
* **Not fixed**: `hmadrv`'s *"two halves are the demo's samples"* check is
  racy in the test (4 of 6 arms at the base and the tip alike; it passed on
  the close's re-run); `tests/ftpd.py`'s assertion 8b is intermittent under
  load at the base (unregistered, so the suite cannot see it); `kbm_key`'s
  header says it preserves every register and its move path never kept AX
  (both callers reload it).

## 5. WHAT IS LEFT

* **kern_small's `DISKOPS.DRV` is exactly 7,168 bytes**: one byte more, from
  anyone, and its claim is 8 KB again. kern_big's `CTRL.DRV` has ~47 bytes
  of its 12 KB.
* **Two `.text` runs still at an odd shift on kern_big**, `blk_act..wm_hit`
  and `wm_dock_under..fpg_arm` (`wm_paint_all` among them), from the round
  agent moving `[blk_on]`: one `.text` byte moved fixes it for nothing
  (`ksp12/xfile.md`'s cross-file list).
* **§3's ABI wishes**, ~-40 resident between them.
* **The relaxed-jump remainder**: 68 on kern_big and 22 on kern_small by
  xfile's listing scan (71 / 22 at `26c45e1d`); pass 11's table still says
  which are deliberate.
* **The Mount-a-`.IMG` transfer arm** (~130 bytes, the largest block of (a)
  left) - a re-entrant loop with little slack; `ksp12/imgmount.md` has what
  was tried.
* Carried from pass 11 §5, untouched: `HDD.DRV` at its floor, the 2A3h
  PicoMEM ramp in four places, `xm_release_rec` on kern_small, `rect_get` /
  `rect_put` at the damage-repaint sites, the module bss assertion's general
  fix.

## 6. WHAT WAS RUN

* Every agent: `make`, `make small` and the fast tier after every batch,
  `make emu` (and the ROM agents `make rom`) at its tip, `checkdocs`, and
  `tools/stkbalance.py` over the files it touched, base against tip - no new
  unbalanced path anywhere. Equivalence where no row reaches: pixel diffs
  against the base on MartyPC (ctrl: every panel page on VGA, CGA+Hercules,
  VGA+HDD and kern_small; theme: 32 frames of the Windows chrome on VGA and
  Hercules), listing diffs (round's inlined TCP retransmit), a scratch
  harness (xfile's keypad mouse). Each agent broke the thing it changed on
  purpose at least once and watched its row go red.
* About 160 distinct per-agent soak rows, green, listed in each note.
* **The integration soak**: on the merged tree at `4cb1b15d`, 196 rows -
  the union of the agents' rows widened to every subject family the pass
  touched (`small*`, `kd*`, `hd*`, `fdlg*`, `fm*`, `fcp*`, `wseq*`, `heap*`,
  `reg*`, `rehome*`, `covox*`, `ems*`, `rom*`, `hma*`, `thm*`, `cp*`,
  `img*`, `cz*`, `rd*`, `drv*`, `snd*`, `fsx*`, `assoc*`, `blit*`, the
  Video Player rows that reach the kernel, `int0sweep` on the IBM ROM...).
  **195 ok, 1 SKIP** (`czdos`: the box has no dosbox), counting §6.1's
  re-run. The whole soak tier was not run: it is the owner's to ask for.
* The full tier and `soak -k buildmatrix` at the close: §6.1.

### 6.1 The close

On the merged tree at `4a00d618` (identical to `ksp12-xfile`'s tip):

* **The full tier: 50/50** - kern_small on its 128 KB floor machine, both
  1bpp adapters and VGA, the C toolchain, `int0sweep` on the IBM 5150 ROM
  (27OCT82; the first pass to have it in the container).
* **`soak -k buildmatrix`: ok** - every knob kernel and knob module on both
  arms, so no module crossed a KB boundary downward with its bss on top.
* **The integration soak's 35 failures re-run here: 35/35 ok** (34 were §7's
  symbol-map refusal, the 35th `hmadrv`, §4).
* `make emu` and `make rom` build. The kern_big ROM window had 778 bytes
  spare at the base (the hma and rom notes) and at the close holds
  `FORMAT.DRV` with 62 spare; the kern_small one holds FDLG, FILECP, CTRL,
  FORMAT and the Task Manager with 4,235 spare.
* `docs/KERNEL-MEMORY.md` re-blessed on all three kernels.

## 7. METHOD LESSONS

* **Do not merge into the checkout while a soak is running in it.** The
  soak's frozen tree freezes the ARTEFACTS, not the sources, and every row
  that resolves a symbol re-assembles `kernel.asm` from the checkout: the
  coordinator merged xfile mid-run, and 34 rows that started after it failed
  with *"the map describes a DIFFERENT kernel"* - none of them about the
  kernel. All 35 failures re-ran green on the final tree. Pass 11's lesson
  one level up.
* **An `isolation: worktree` agent may start on `origin/main` rather than
  the coordinator's HEAD** (at least two of eight did, and reset themselves). Name
  the commit in the brief and have the agent check out a branch at it first.
* **Run `checkdocs` after `git add`, not before**: it reads tracked files,
  so an untracked brief passed the coordinator's check and failed `make` for
  every agent (two citations of the predecessor handoff's section 1.2
  written with a section sign, which `checkdocs` reads as a SPEC.md
  heading that does not exist). Seven agents fixed it seven ways, and
  every merge then conflicted on it.
* **By concept, with hunk ownership, again worked**: `driver.inc` had four
  owners and `kernel.asm`/`memory.inc`/`osrom.asm` two, and the code merges
  were clean every time; only the brief and the generated index conflicted.
* **The cross-file agent earned its round a third time**, -68 / -62, and the
  mid-round hand-off of the barred files worked: start it on the merged tree
  with one concept outstanding and free those files when the branch lands.
* **A freed byte goes somewhere**: the ROM window filled with a module the
  moment the pass gave it room, so "spare" in a window that packs itself is
  not spare - quote what it HOLDS.
