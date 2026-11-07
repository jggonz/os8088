# Kernel size pass 18: the brief every agent reads first

Branch `kernel-size-18`, cut from `elendilon` at `aede17b1` on 2026-11-07.
The pass before this one was **pass 17** (branch `kernel-size-17`, base
`af5579d2`); its record is `docs/plans/completed/HANDOFF-KERNEL-SIZE-P18.md`
(named for the pass it hands to) and its brief and agent notes are
`docs/plans/completed/ksp17/`. Pass 17 reached `elendilon` inside main's
squash `d323a9a6` (#328), so **`d323a9a6` is the last sized tree** and
everything after it is unsized.

Each agent works in a git worktree of its own, created by the coordinator at
`/home/user/wt/<name>` on a branch `ksp18-<name>` cut from the commit that
adds this file, and writes its record to `docs/plans/completed/ksp18/<name>.md`. The
coordinator merges the branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/completed/ksp14/BRIEF.md` sections 1, 2, 3 and 4** - the
   rules, the method, the environment and the note's shape carry over
   UNCHANGED except where this file says otherwise. Substitute `ksp18` for
   `ksp14`, `docs/plans/completed/ksp18/` for its note directory, and
   `(kernel size pass 18)` for the commit suffix everywhere;
3. `docs/plans/completed/ksp17/BRIEF.md` section 2 (still holds, with the
   agent count and ABI base below);
4. **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P18.md` in full** - pass 17's
   record: section 3's WANTS THE OWNER items are NOT taken, section 6 is what
   is left (its held-for-parity and CROSS-FILE items are free to whoever owns
   the file), section 8 its method lessons;
5. **the speed pass that landed since: `docs/plans/khp1/BRIEF.md` section 1
   (hotness H1-H4) and `docs/plans/khp1/LOG.md` section L** (what the owner
   picked, and why), and `docs/plans/khp1/size.md` sections 3-4 (that pass's
   own size agent: what it REFUSED and held). For any file you touch, the
   khp1 agent note that owned it (`docs/plans/khp1/<disk|files|gfxcall|mono|
   pointer|sched|text|vga|wm>.md`);
6. **the REFUSED, DECLINED and WANTS THE OWNER lists in
   `docs/plans/completed/ksp17/*.md` and `ksp16/*.md`** for any file you
   touch - a refused item is not re-proposed unless the reason no longer
   holds, and then the note says what changed;
7. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**.

## 0. The brief

The owner's words: *"New kernel and speed work has come in at a decent chunk
of bytes. This work is speed sensitive - it needs to not get meaningfully
slower (or re-introduce disk reads we just removed). Size pass it carefully.
Size pass anything coming in from main, and anything else we have added
since the last size pass. ... Also start a few agents on a more general
pass, with the goal to recover 512b resident on big. This pass is still not
the ABI break pass - keep recording those for now, but not taking them."*

**The goal: kern_big resident at least 512 bytes under the base** (`.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`, `tools/kernsize.py
--json`), i.e. **91,255 or less**. kern_small, kern_emu and kern_mega count
too and are reported; none of them may grow.

**ABI breaks are GATHERED, not taken.** Anything frozen that would pay to
break goes in your note's *WANTS AN ABI BREAK* with its bytes and its
callers, priced as precisely as you can - the owner is collecting them, and
the coordinator folds them into `docs/plans/ABI-BREAK-LIST.md`. Do not change
a frozen interface.

### 0.1 The base, assembled (`aede17b1`)

| kernel | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **kern_big** | 40,633 | 5,376 | 38,034 | 5,358 | 336 | 2,030 | **91,767** | 6,456 | 5,117 | 97,280 |
| kern_small | 30,232 | 2,946 | 21,287 | 2,868 | 0 | 0 | **57,333** | 4,742 | 1,474 | 59,392 |
| kern_emu | 40,877 | 5,376 | 38,034 | 5,358 | 336 | 2,030 | **92,011** | 6,571 | 5,117 | 97,792 |
| kern_mega | 46,307 | 5,414 | 46,573 | 5,574 | 336 | - | **127,276** (incl. `.mega` 23,072) | 6,773 | 5,116 | conventional 63,488 of 68,096 |

At the last sized tree (`d323a9a6`) they were kern_big **91,184**, kern_small
57,246, kern_emu 91,438, kern_mega 124,544 (conventional 62,976) - so the
work since is **+583 / +87 / +573 / +2,732**. kern_big's `KERN_SIZE` stepped
96,768 -> 97,280 on the way.

### 0.2 What has not had a pass

`tools/os88rdiff.py kernel d323a9a6 aede17b1`, kern_big: `.text` +57,
`.bss` -35, `.cold` +563, `.vga` -2; **+1,180 added and -550 taken out
across 186 routines**. Three sources:

* **The speed pass, `khp1` (kernel hot-paths pass 1), 93 commits.** Its lean
  picks are the OWNER'S CHOICES - each bought user-visible time - plus
  `KHP_LZ` on kern_big (+190, `0445ea49`) and the standalone disk-3/disk-5
  (+74). Its free items and its own size agent's trims took bytes out.
  Largest: `lz_lz4` 96 -> 178, `lz_lzb` 98 -> 171, `lz_arm` +22, `lz_sync`
  +12 (KHP_LZ); `disk_mount_x` 708 -> 761, `dsk_rah_fill` 163 -> 175 (disk-3,
  disk-5); `cur_move_mono` 303 -> 396 against `cur_mvcols`/`cur_mvrow`/
  `cur_mvbg` (-121) gone; `sch_isr`/`sch_switch`/`sch_idle_body`/
  `task_spawn`/`task_exit` (sched-1, sched-4); `menu_draw_bar` +14 against
  `menu_logo_bmp` -18 gone; `sw_xfer` +16 (mono-2).
* **elendilon's own work**: the Disk window lists up to 1,024 entries
  (`69cb417c`, `15803099`, `82fd9bd1`, SPEC.md 22.6.4 - `fm_draw_rows` split
  into `fm_draw_rcore` 163, the `fmv_*` listing claim `fmv_need`/`fmv_rsz`/
  `fmv_cap` and `fmv_load` 89 -> 152, the `fm_m*` selection bitmap,
  `dsk_ioffx`, `fm_many` 15 -> 40); the Shell sort (`e2c2f85f`, `dsk_sdcmp`
  51 new); the TOASTSTR sweep (`a3acd430`, `d367c543`).
* **Work arriving from main**: `f113f3f7` (logo video, `kbm_isr`'s IRQ1-only
  ROM handoff 95 -> 121, the splash's driver message moved into `.boot2`,
  MOUDIAG), `d7ac8738` WIN16 Phase B/C (`assoc_ownex` 20 new, `assoc_run_x`
  +20), `4faf9339`/`eb1caa77` PCIETHER.DRV (`drv_f_pcieth`, `drv_e9`, the
  `bdt_`/`drvt_` rows, `vesa_row` +16), `d401cf8c` WIN16-NEXT and `a408aaf5`
  (kern_mega: `mega.inc` +1,265 lines, `.mega` +1,360 and `.cold` +920 on
  kern_mega).

The full per-routine list, with the agent that owns each, is
`docs/plans/completed/ksp18/fresh-routines.md`. Regenerate the diff yourself with
`tools/os88rdiff.py kernel d323a9a6 aede17b1` (add `-D KERN_SMALL` for
kern_small).

## 1. THE SPEED RULE (this pass's own, on top of ksp14 section 1)

The owner: *"it needs to not get meaningfully slower (or re-introduce disk
reads we just removed)."*

* **No khp1 pick is reverted for its bytes.** The `[+N]` items in
  `docs/plans/khp1/LOG.md` section L and `KHP_LZ` stay. Making one of them
  SMALLER at the same speed is in scope - and is where `hot`'s bytes are.
* **A routine on the khp1 hot lists (H1, H2), or a routine a khp1 commit
  sped up, may change only if it is cycle-neutral or faster on the path it
  was sped up for**, with the count in your note (`tools/os88hot.py cycles
  <routine> <scenario>`, a unicorn harness against both kernels' bytes, or a
  hand count in the 8088's `max(clocks, 4 x bytes)` terms that you show).
  H3 and H4 paths: a cost under 1% of the named operation is allowed, with
  the operation and the figure in the note; more is WANTS THE OWNER.
* **No change may add an `int 13h` call to any operation.** The disk-3 and
  disk-5 counts are the bar (360KB and 720KB: open a root 2 calls, enter a
  folder 2, Up 1; a cached launch none). Anything on the mount, listing,
  read-ahead, FAT-window or loader path names the scenario and its call count
  before and after (`tools/os88hot.py time open --machine cga` reports them;
  MartyPC's FDC counters, docs/MARTYPC-DEBUG.md, count them exactly).
* **Parity is still a gate** (`hotpar`, fast tier). khp1 added hot routines
  to `tests/hotpar.txt`; pad back and say so.
* The `KHP_*` knobs no kernel defines, and kern_mega's KHP arms, are not
  resident on kern_big and are not targets. kern_mega's KHP arm must keep
  building (`make mega`).

## 2. The seven agents and what each owns

Three on the FRESH work (section 0.2), four on a GENERAL whole-kernel pass.

| agent | owns | the work |
|---|---|---|
| **hot** | every routine `fresh-routines.md` assigns `hot`: khp1's lean picks, `KHP_LZ`, disk-3/disk-5, the free items' routines | Shrink the speed work AT ITS SPEED. Start with `lz_lz4`/`lz_lzb`/`lz_arm`/`lz_sync` (KHP_LZ re-took disk-1 and disk-2, so the khp1 size agent's size-12 - the four `lz_lz4` slow paths onto one `.sync` stub, -8, reverted only because disk-1 was out at the time - may hold again: check), then `disk_mount_x`/`dsk_rah_fill`/`dsk_rah_hint` (the int 13h counts are the bar), `cur_move_mono`, sched's +47/+12, the rest. Slow paths and setup around a fast path are fair game; the fast path itself must not lose a cycle. |
| **dir** | every routine `fresh-routines.md` assigns `dir`: the 1,024-entry Disk window, the Shell sort, the TOASTSTR sweep's kernel side | The listing claim, `fmv_*`, `fm_draw_rcore`/`fm_draw_rows`, the `fm_m*` bitmap, `dsk_sdcmp`, `dsk_ioffx`. **`fm_draw_rcore` is files-2's PgDn path and the row draw** (H3) and the sort sits on every listing open (H4): their speed is the bar. A 1,024-entry folder must still list, sort and select exactly as SPEC.md 22.6.4 says. |
| **main** | every routine `fresh-routines.md` assigns `main`: what arrived from main (`f113f3f7`, `d7ac8738`, `4faf9339`, `eb1caa77`, `d401cf8c`, `a408aaf5`) | `kbm_isr` (H2, per key - cycle-neutral), the splash's `.boot2` move, PCIETHER's kernel rows and `vesa_row`, WIN16's `assoc_*` hunks, `task_*` if WIN16's. Then, reported SEPARATELY as system-disk bytes: the PCIETHER.DRV image (no size pass yet) and ETHER.DRV's split into `ethdrv.inc`. kern_mega's `.mega`/`.cold` growth (+1,360/+920): read it and price what you find; take only what is free and does not touch the KHP arms. |
| **wmui** | `wm.inc`, `instance.inc`, `clip.inc`, `events.inc`, `dock.inc`, `apps.inc`, `ui.inc`, `menu.inc`, `toast.inc`, `fprog.inc`, `snd.inc`, `ctrl.inc`'s resident half, `kernel.asm` (the API table's shims, `kmain`) - except routines `fresh-routines.md` assigns | A general finder, every routine, not only the large ones. The damage repaint, `wm_paint_all`, the API cell and `ui_task`'s idle pass are hot. |
| **draw** | `vga12.inc`, `softgfx.inc`, `font.inc`, `mouse.inc`, `mouproto.inc`, `vmmouse.inc`, `moudiag.inc`, the cursor code, `viddet.inc`, `vidsel.inc`, `vidext.inc`, `blank.inc`, `fsx.inc`, `band.inc` - except routines `fresh-routines.md` assigns | The same. Per-pixel loops, `font_run`, the blit run loops and the mouse ISRs are H1/H2 and khp1 just tuned several - read khp1's `vga.md`, `mono.md`, `text.md`, `gfxcall.md` and `pointer.md` before touching them. |
| **files** | `files.inc`, `fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc` (resident half), `assoc.inc`, `icons.inc`, `dskwin.inc`, `diskops.inc`, `fsck.inc`'s resident half - except routines `fresh-routines.md` assigns | The same. |
| **sys** | `disk.inc`, `diskw.inc`, `drvvol.inc`, `loader.inc`, `lz.inc`, `lzfilt.inc`, `compress.inc`, `memory.inc`, `mod.inc`, `driver.inc`, `xmem.inc`, `hiber.inc`'s resident half, `clock.inc`, `clockw.inc`, `serial.inc`, `cpudet.inc`, `sched.inc`, and where code lives (`boot/boot2.asm`'s kernel blocks, `.cfx`, `.ovl`) - except routines `fresh-routines.md` assigns | The same. `dsk_xfer`'s loop, `dsk_runcap`, `dsk_next_clus_x`, the LZ and directory-scan loops, `sch_isr`/`sch_switch` are hot. Pass 17's held-for-parity items in these files (P18 handoff 6) are yours to pair. |

**A routine `fresh-routines.md` names belongs to the fresh agent it names,
whatever file it is in**; a general agent does not edit it, and lists
anything it sees there under CROSS-FILE. A file in no row is free to whoever
reaches it first: SAY SO in your note's TAKEN line. The parity-pad pairs
across rows are the coordinator's at the close - list any new pad you add.

**Pass 16 and 17's lesson 4 holds**: single-caller routines and callee-saves
no caller reads were the largest share of every finder's bytes. Read every
routine in your files.

## 3. What is different from pass 17's rules

* **Seven agents at once on 4 cores and 15 GB.** `make -j2`, never two
  builds in your own tree at once (`make` in a tree is single-tenant - pass
  17 lost a packed kernel to two), at most ONE MartyPC instance of your own,
  and run your rows ONCE, after your last code commit; a row that fails on a
  TIMEOUT is re-run alone before it is believed. The first `make` in a fresh
  worktree builds everything - start it first, and expect it to be slow while
  six others do the same.
* **ABI**: `main` is `origin/main` at `86fc03da`. `git diff origin/main
  HEAD -- apps/os88api.inc drivers/os88drv.inc apps/cc/os88.h` is the list of
  what is NOT frozen; everything else in those files is.
* Commit messages end in `(kernel size pass 18)`. Each agent's note goes in
  `docs/plans/completed/ksp18/<name>.md`; run `python3 tools/os88index.py` and commit
  `docs/INDEX.md` with it. (The coordinator moves the directory to
  `docs/plans/completed/` at the close.)
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree before your first emulator row. It is
  built in the main tree; if it is not there when you reach your rows, wait
  for it rather than building a second copy. `tools/os88hot.py` measures
  YOUR worktree's `build/` (it derives ROOT from its own path); `ab --base
  <commit>` builds the base in a tree of its own.
* `disk.inc`'s `align 2` after `dsk_img16` swallows any odd `.text` delta
  above it: measure with `kernsize --json` AND the routine span, and say
  which.
* `apps/wireball/art/wbfront.inc` regenerates with different bytes on every
  build here (P18 handoff 5.5): `git checkout` it before you commit, never
  commit it.
* The khp1 inbox (`/home/user/wt/khp1-size-inbox/`) did not survive into this
  container; `docs/plans/khp1/size.md` section 3 is what remains of it.
