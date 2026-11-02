# Kernel size pass 17: the brief every agent reads first

Branch `kernel-size-17`, cut from `elendilon` at `af5579d2`. The pass before
this one was **pass 16**; its record is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P17.md` (named for the pass it hands
to) and its brief and agent notes are `docs/plans/completed/ksp16/`.

Each agent works in a git worktree of its own, created by the coordinator at
`/home/user/wt/<name>` on a branch `ksp17-<name>` cut from the commit that
adds this file, and writes its record to `docs/plans/completed/ksp17/<name>.md`.
The coordinator merges the branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/completed/ksp14/BRIEF.md` sections 0.2, 1, 2, 3 and 4** -
   the rules, the method, the environment and the note's shape all carry over
   UNCHANGED except where this file says otherwise. Substitute `ksp17` for
   `ksp14` and `(kernel size pass 17)` for the commit suffix everywhere;
3. `docs/plans/completed/ksp16/BRIEF.md` section 2 (what pass 16 changed
   about those rules - it still holds, with the agent count and ABI base
   below);
4. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 (pass 1's
   method and the mistakes that cost it time);
5. **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P17.md` in full** - pass 16's
   record: section 3's WANTS THE OWNER items are NOT taken (they are function
   changes awaiting a decision), section 6 is what is left (its CROSS-FILE
   odds and ends are free to whoever owns the file), section 8 its method
   lessons;
6. **the REFUSED, DECLINED and WANTS THE OWNER lists in
   `docs/plans/completed/ksp16/*.md`, `ksp15/*.md` and
   `HANDOFF-KERNEL-SIZE-P16.md` section 3** for any file you touch - a
   refused item is not re-proposed unless the reason no longer holds, and
   then the note says what changed;
7. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**.

## 0. The brief

The owner's words: *"This is a kernel size pass - another somewhat small
round. ... The goal is another 512b of bytes recovered. The targets will be
the recently added unsized work, plus an indepth whole kernel pass (which is
likely where the bulk will come from). ABI breaks are still being gathered -
likely to be taken next round rather than this round."*

**The goal: kern_big resident at least 512 bytes under the base** (`.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`, `tools/kernsize.py
--json`), i.e. **91,367 or less**. kern_small, kern_emu and kern_mega count
too and are reported; none of them may grow.

**ABI breaks are GATHERED, not taken.** Anything frozen that would pay to
break goes in your note's *WANTS AN ABI BREAK* with its bytes and its callers,
as precisely as you can price it - the owner is collecting them for the next
round, so a well-priced entry is a deliverable of this pass, not a
consolation. Do not change a frozen interface.

### 0.1 The base, assembled

| kernel | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **kern_big** | 40,925 | 5,435 | 37,793 | 5,358 | 336 | 2,032 | **91,879** | 6,455 | 5,116 | 97,280 |
| kern_small | 30,494 | 2,963 | 21,393 | 2,868 | 0 | 0 | **57,718** | 4,745 | 1,474 | 59,904 |
| kern_emu | 41,179 | 5,435 | 37,793 | 5,358 | 336 | 2,032 | **92,133** | 6,570 | 5,116 | 97,792 |
| kern_mega | 46,167 | 5,473 | 45,925 | 5,570 | 336 | - | **125,183** (incl. `.mega` 21,712) | 6,772 | 5,115 | - |

Pass 16's tip was kern_big 91,517, kern_small 57,668, kern_emu 91,771,
kern_mega 124,980 - so the work since is **+362 / +50 / +362 / +203**.

### 0.2 What has not had a pass

Pass 16 merged at `920b839b`. `tools/os88rdiff.py kernel 920b839b HEAD`,
kern_big: `.text` +6, `.bss` +20, `.cold` +336, and a new module `.modfs`
(5,679, FSCHECK.DRV - not resident). By commit:

| commit | what | the resident routines |
|---|---|---|
| `e10d32c5` | a driver volume's sector across a 64KB page is staged (SPEC.md 18.91.4.1) | **`dsk_xfer` 758 -> 938 (+180)** - HOT, the per-sector path; `disk_write_x` 8 -> 22 |
| `e193cdc9`, `7282243f` | the near-call filter, a stream-level E8 BCJ (SPEC.md 20.13.9; `kernel/lzfilt.inc`) | `lz_decomp_big` 71 -> 115 (+44), `assoc_run_x` +12 (?), `mod_fpr` |
| `8bc149c3`, `f0db927e`, `23cf0ad5` | System Check, FSCHECK.DRV (SPEC.md 118.14; `kernel/fsck.inc`) | `osapi_vol_check` 73, `fsk_*` resident half (`.cold` 76, `.bss` 20), `apic_fs_op` 6 -> 12, the mod table row |
| `5cf63d75`, `7466861d`, `78528ca9`, `c69774f3` | DOS.O88's loader decides a Windows program; parts on paragraphs; DOS headless | `ld_pkg_byname` 71 -> 81, `loader.inc` +13 lines, `assoc.inc` +8 lines |
| `83454ce5` | `kds_expand`'s LZB decoder (HIBER.DRV, not resident) | `.modh` only |

The module images (`.modfs`, `.modh`) are not resident and are not the goal,
but a module that a change of yours fills past its claim is a regression
(`tools/os88modcost.py`), and FSCHECK.DRV has had no size pass at all - a
byte off it is a byte of system disk.

## 1. The seven agents and what each owns

| agent | owns | the work |
|---|---|---|
| **fresh** | every hunk `git diff 920b839b af5579d2 -- kernel/ boot/` shows (the commits in 0.2), the routines those hunks sit in, `kernel/lzfilt.inc` and `kernel/fsck.inc` entire (resident half AND the FSCHECK.DRV image) | The unsized work first: `dsk_xfer`'s staging (+180 - hot; cycle-neutral or faster on the sector loop, the staging arm itself is a rare path and may cost cycles), the filter's resident half, `osapi_vol_check` and the fsck doors, `.bss` +20. Then a finder over FSCHECK.DRV's image (system disk bytes, reported separately). |
| **files** | `files.inc`, `fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc` (resident half), `assoc.inc` (not fresh's hunks), `icons.inc` | A general finder, largest routines first, on what pass 16's fsys/place did NOT read. |
| **disk** | `disk.inc` (not fresh's hunks), `diskw.inc`, `loader.inc` (not fresh's hunks), `lz.inc` (not fresh's), `compress.inc` | The same. `dsk_xfer`'s loop, `dsk_runcap`, `dsk_next_clus_x`, the LZ and directory-scan loops are HOT. |
| **wm** | `wm.inc`, `instance.inc`, `clip.inc`, `events.inc`, `dock.inc`, `apps.inc` | The same; the damage-repaint path keeps its speed (pass 15 DECLINED 3). `wm.inc` is the second-largest file in the kernel. |
| **draw** | `vga12.inc`, `softgfx.inc`, `font.inc`, `mouse.inc`, `cursor` code, `viddet.inc`, `vidsel.inc`, `vidext.inc`, `blank.inc`, `fsx.inc`, `band.inc` | The same; per-pixel loops, `font_run`, the blit run loops, the mouse ISRs are HOT. |
| **ui** | `ui.inc`, `menu.inc`, `toast.inc`, `fprog.inc`, `snd.inc`, `sched.inc`, `ctrl.inc`'s resident half, `kernel.asm` (the API table's shims, `kmain`, everything not fresh's) | The same; `ui_task`'s idle pass, `sch_isr`, `sch_switch` and the API cell are HOT. |
| **sys** | `memory.inc`, `mod.inc`, `driver.inc`, `xmem.inc`, `hiber.inc`'s resident half (not fresh's), `clock.inc`, `serial.inc`, `cpudet.inc`, AND where code lives: `boot/boot2.asm`'s kernel blocks, `.cfx`, `.ovl`/`.ovlw` | The same over the files, then the placement lever (pass 16 place.md says it is spent without a function change - read it before re-walking; a new routine since then is the only new ground). |

A file not in any row is free to whoever reaches it first: SAY SO in your
note's TAKEN line, and do not edit a routine another agent's row names.
**The parity-pad pairs are the coordinator's, at the close** - do not take
them; DO list any new pad your change adds.

**The whole-kernel pass is where the owner expects the bulk** - pass 16's
four general finders took 497 of its 619 and its lesson 4 was that **a
single-caller routine's register saves are the cheapest bytes left**. Read
every routine in your files, not only the large ones: the long tail paid
before.

## 2. What is different from pass 16's rules

* **Seven agents at once on 4 cores and 15 GB.** `make -j2`, never two
  builds in your own tree at once, at most ONE MartyPC instance of your own,
  and run your rows ONCE, after your last code commit; a row that fails on a
  TIMEOUT is re-run alone before it is believed. The first `make` in a fresh
  worktree builds everything - start it first, and expect it to be slow
  while six others do the same.
* **ABI**: `main` is `origin/main` at `7b4b441d`. `git diff origin/main
  HEAD -- apps/os88api.inc drivers/os88drv.inc apps/cc/os88.h` is the list of
  what is NOT frozen; everything else in those files is. Do not break a
  frozen one: price it for *WANTS AN ABI BREAK* (section 0).
* Commit messages end in `(kernel size pass 17)`. Each agent's note goes in
  `docs/plans/completed/ksp17/<name>.md`; run `python3 tools/os88index.py`
  after adding it.
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree before your first emulator row. It is being
  built in the main tree at the start of the pass; if
  `/home/user/os8088/build/martypc/martypc_headless` (or whatever
  `tools/os88marty.py` looks for) is not there yet when you reach your rows,
  wait for it rather than building a second copy. `unicorn` and `mtools` are
  installed.
* `disk.inc`'s `align 2` after `dsk_img16` swallows any odd `.text` delta
  above it (pass 15 section 6): measure with `kernsize --json` AND the
  routine span, and say which.
* Pass 16's `megaseg` lesson: a `.text` byte added under `%ifdef KERN_MEGA`
  needs `tests/megaseg.txt`; `make mega` at your tip catches it.
