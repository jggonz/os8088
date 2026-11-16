# Kernel size pass 20: the brief every agent reads first

Branch `kernel-size-20`, cut from `elendilon` at `8ef75fa1` on 2026-11-12.
The pass before this one was **pass 19, the flag day** (branch
`kernel-size-19`, merged into `elendilon` at `2cf57256`); its record is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P20.md` (named for the pass it
hands to) and its brief and agent notes are `docs/plans/completed/ksp19/`.
**`2cf57256` is the last sized tree**; everything after it is unsized.

Each agent works in a git worktree of its own, created by the coordinator at
`/home/user/wt/<name>` on a branch `ksp20-<name>` cut from the commit that
adds this file, and writes its record to `docs/plans/completed/ksp20/<name>.md`. The
coordinator merges the branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/completed/ksp14/BRIEF.md` sections 2, 3 and 4** - the
   method, the environment and the note's shape carry over UNCHANGED except
   where this file says otherwise. Substitute `ksp20` for `ksp14`,
   `docs/plans/completed/ksp20/` for its note directory, and `(kernel size pass 20)`
   for the commit suffix everywhere. Its section 1 (rules) is replaced by
   section 1 below;
3. **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P20.md` in full** - pass 19's
   record: section 4 (WANTS THE OWNER - not taken), 5 (deferred to the
   additions pass - not taken), 6 (what is left) and 8 (method lessons);
4. **`docs/plans/completed/ksp18/BRIEF.md` section 1, THE SPEED RULE** -
   it applies to this pass word for word, with "khp1" read as "the speed work
   since `2cf57256`" (section 0.2 below) as well as khp1;
5. **the REFUSED, DECLINED and WANTS THE OWNER lists in
   `docs/plans/completed/ksp19/*.md` and `ksp18/*.md`** for any file you
   touch - a refused item is not re-proposed unless the reason no longer
   holds, and then the note says what changed;
6. `docs/plans/KERNEL-BYTE-QUEUE.md` and `docs/plans/LAST-DROP-BYTES.md` for
   any item in your files;
7. CLAUDE.md's Hard rules and its banner: **bytes, not rungs** - section 0.1
   is this pass's one, owner-sanctioned exception, and it is kern_small's.

## 0. The brief

The owner's words: *"This kernel size pass will go over the recently
added/enabled speed, big-pass etc work. It may also include side forays to
find a few more bytes. The goal is to save ~300b, and to get kern small down
one more rung (it should be within 200b of that; normally we do not aim for
rungs, but kern small is approaching its final form and size, leading to
there being value for a rung there)."*

And: *"We are in the middle of an ABI break round; the ABI may be altered
now, and size benefits from ABI breaks are allowed. You do not need to
increment package/driver ABI versions; they have already been incremented
this cycle and we have not yet released the work they were changed for."*

**The goals, as resident bytes** (`.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` + `.vga`, `tools/kernsize.py --json`):

* **kern_big: about -300** against the base (section 0.2's table).
* **kern_small: one rung down.** Its `accrued` line reads **168/512** at the
  base, so the image rung drops when kern_small's rung members (`.text`,
  `.bss`, `.lowbss`, `.cold`, the FAT window, `stk0`) come down by **at least
  169 bytes** - less whatever paragraph rounding gives back between them, so
  aim the pass at **-200 or more** on kern_small. **Every kern_small byte
  counts in this pass**: say each item's kern_small figure even when it is 0.
* kern_emu and kern_mega are reported; **none of the four may grow**.

### 0.1 The rung exception, stated narrowly

The owner sanctions aiming at kern_small's rung because kern_small is near
its final form. That changes WHAT the pass aims at, not how a byte is
priced: every item is still priced in bytes, a saving that "only" moves the
accrual is still a saving, and nothing is shaved or cut down to reach the
rung. A functionality change to reach it is WANTS THE OWNER as always.

### 0.2 The base, assembled (`8ef75fa1`)

| kernel | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | **resident** | `KERN_SIZE` | accrued |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **kern_big** | 39,345 | 4,892 | 37,933 | 5,358 | 336 | 2,222 | **90,086** | 95,232 | 486/512 |
| **kern_small** | 29,112 | 2,843 | 21,153 | 2,868 | 0 | 0 | **55,976** | 57,856 | **168/512** |
| kern_emu | 39,591 | 4,892 | 37,933 | 5,358 | 336 | 2,222 | **90,332** | 95,744 | - |
| kern_mega | 44,929 | 4,930 | 48,412 | 5,600 | 336 | - | **128,895** (incl. `.mega` 24,688) | conventional 61,440 of 68,096 | - |

Measure your own base in your worktree anyway (ksp14 section 2).

### 0.3 What has not had a pass

`tools/os88rdiff.py kernel 2cf57256 8ef75fa1` - kern_big: `.text` -92,
`.bss` +3, **`.cold` +364, `.vga` +192**; kern_small (`-D KERN_SMALL`):
`.text` -96, `.bss` +3, **`.cold` +248**. Its sources:

* **speed-and-fix** (`df6578b2`, 129 commits): disk read-ahead and prefetch
  (`dsk_rah_find` 58, `dsk_rah_pick` 63, `dsk_rah_rdst` 59, `dsk_rah_pre`
  125 new; `dsk_rah_serve`/`dsk_rah_fill` reworked; `DSK_RAH_SPEC`'s single
  attempt), the directory writer (`dskw_mkclus` 54, `dskw_dirgrow` 54 new,
  `dskw_mkbody`, `dskw_wbody`), `dsk_find_x` 146 -> 171 and
  `api_file_find_seq` (V1/FIND_SEQ ABI), the damage-pass repaint
  (`wm_paint_dmg` 183 -> 195, `wm_paint_all` 108 -> 19, `wm_zwalk` gone),
  hibernate without a dialog (`hb_probe_x` 76 -> 214 in the blob, `hbm_*` in
  HIBER.DRV, `hb_spend`/`hb_s_skip`), the B4 driver wake (`drv_boot2_x`,
  `bdb_pass`), BR fixes (`mem_free_rec_x` 26 -> 32, `mem_rr_tab`,
  `mem_pg_fatwrow`, `inst_task_die`, `app_close_now`, `assoc_ownex`,
  `fcp_paste`, `os88ui_sbar` 128 -> 141, `desk_paint_mask_x`);
* **the enabled VGA speed knobs** (`KHP_VGA*`, LOG L.6): `.vga` +192 -
  `gfx_blitp_v` 382 -> 410, `vga_solid_rect_v` 157 -> 193,
  `gfx_fill_gray_raw_v` 144 -> 162, `vga_save_vram_v` 77 -> 95,
  `vga_restore_vram_v` 96 -> 119, `cur_saveu`/`cur_restoreu` 17 -> 56/53,
  `cur_vspan` 25 new, `cur_rect` gone. These had a size pass of their own
  (`f1f35307`, `a47b9326`, `4dee4931`, `5b78b2c7`, `866ccc31`): read those
  commits first;
* **the module cache** (`8cbd0d13`, `1336dcb8`, SPEC.md 2.8.3): `mod_live`
  7 -> 24, `mod_need` 228 -> 274, `mod_drop` 12 -> 67 (resident), plus
  `.modc`/`.modn`/`.modu` growth (module images: disk bytes, not resident);
* **Sticky Apps' scan** (`3edb638a`, `1ce3010e`): `stm_*` in STIKMENU.DRV
  (module image) and its resident thunks;
* **Paint** (`a93c99bb`): package only.

Regenerate the routine list yourself: `tools/os88rdiff.py kernel 2cf57256
8ef75fa1` (add `-D KERN_SMALL` for kern_small).

## 1. The rules this pass

* **No row grows bytes** (pass 19's BRIEF.md 0.0, still the owner's): an
  item is taken only if it is net SMALLER - resident bytes on every kernel
  it reaches, and the packages' total. Small package growth that buys
  RESIDENT kernel bytes is fine; say the total.
* **ABI**: not frozen. A change that is smaller and not slower on a hot path
  is TAKEN, ABI or not, **with every caller in the tree converted** (asm
  packages, drivers, the C SDK `apps/cc/` and the C packages, tests, DOS's
  two hosts if they reach it) and SPEC.md moved with it. **Do not bump
  `PKG_FMT` or `DRV_VER`** - they were bumped this cycle and nothing has
  shipped since. When `apps/os88api.inc`, `drivers/os88drv.inc` or the C SDK
  moves, run `python3 tools/abilock.py --update "<what and why> (kernel size
  pass 20)"` WITHOUT a bump; the lock file will conflict between branches and
  the coordinator re-runs it at each merge. A C SDK change means building the
  C packages (`make cword runcpm c64 apple2 paccman lemmings weave loom`, and
  the test C packages `chello cfsx covl lembench` - pass 19 lesson).
* **Not taken without the owner** (pass 19's section 4): `OSAPI_BATCH_END`
  withdrawn, `W_DISP`'s offset dropped. **Not taken** (they cost cycles on a
  common path): the RCS door for `call COLD_SEG:x`.
* **Functionality**: byte-identical behaviour is the bar, refusal paths
  included. A message reworded or dropped, a feature that needs the system
  disk where it did not, a limit lowered: WANTS THE OWNER, with its bytes.
* **Speed**: `docs/plans/completed/ksp18/BRIEF.md` section 1, word for word.
  The speed work in section 0.3 is the owner's choice and stays; making it
  SMALLER AT THE SAME SPEED is the point of this pass. **No change adds an
  `int 13h` call to any operation** - the read-ahead and prefetch exist to
  remove them, so anything on the mount, listing, read-ahead, FAT-window,
  directory-write or loader path names its scenario and its call count before
  and after. `hotpar` is a gate: pad back and say so.
* **kern_mega must still build** (`make mega`) and may not grow; a shared
  change says what it did to kern_mega's conventional and `.mega`.
* **Modules are not resident.** A byte moved from `.text`/`.cold` into a
  module that qualifies (CLAUDE.md, *ON-DEMAND MODULE*; the feature already
  needs the system disk) is a resident byte saved - and a module's own image
  growth is disk bytes, reported separately. A move that makes a feature need
  the system disk where it did not is WANTS THE OWNER.

## 2. The five agents and what each owns

**Ownership is by FILE.** A file in no row is free to whoever reaches it
first: say so in your note's TAKEN line. Anything you see in another agent's
file goes in your note's *CROSS-FILE* with its bytes. Everyone reads every
routine in their files - **single-caller routines written at the call,
callee-saves no caller reads, and shared tails were the largest share of
every finder's bytes** in passes 16-19 - and the fresh routines of section
0.3 first.

| agent | owns | start with |
|---|---|---|
| **disk** | `disk.inc`, `diskw.inc`, `drvvol.inc`, `loader.inc`, `lz.inc`, `lzfilt.inc`, `compress.inc`, `fsck.inc`, `diskops.inc` | the read-ahead and prefetch (`dsk_rah_*`, `DSK_RAH_SPEC`), the directory writer (`dskw_mkclus`, `dskw_dirgrow`, `dskw_mkbody`, `dskw_wbody`), `dsk_find_x` and FIND_SEQ, `dsk_xfer`. **The largest kern_small lever** (`disk.inc`+`diskw.inc`+`loader.inc` are ~10KB of kern_small, and the read-ahead is most of kern_small's +248). The int 13h counts are the bar |
| **sys** | `mod.inc`, `driver.inc`, `memory.inc`, `instance.inc`, `sched.inc`, `hiber.inc`, `hbstub.inc`, `hbstage.inc`, `hbmark.inc`, `shutdown.inc`, `serial.inc`, `clock.inc`, `clockw.inc`, `cpudet.inc`, `xmem.inc`, `snd.inc`, `toast.inc`, `events.inc`, `clip.inc`, `apps.inc`, `kernel.asm` (the API table, the door, `kmain`, the shims), `boot/` | the module cache (`mod_need`, `mod_drop`, `mod_live`, `mod_cfix`, SPEC.md 2.8.3), the BR fixes in `memory.inc`/`instance.inc`, hibernate's resident half, the B4 wake's resident part. `sch_isr`/`sch_switch` and the API door are hot |
| **draw** | `vga12.inc`, `softgfx.inc`, `font.inc`, `mouse.inc`, `mouproto.inc`, `vmmouse.inc`, `viddet.inc`, `vidsel.inc`, `vidext.inc`, `blank.inc`, `fsx.inc`, `band.inc`, `icons.inc`, `splash.inc` | the enabled `KHP_VGA*` bodies in `.vga` and the cursor's `cur_saveu`/`cur_restoreu`/`cur_vspan` - **at their speed**: these are H1/H2 hot paths and were enabled FOR speed, so only setup, slow paths and shared tails, or a byte-saving that is cycle-neutral with the count shown. Read `docs/plans/khp1/` notes for any file you touch. Then the kern_small-resident parts of your files (`mouse.inc` 2.5KB, `vga12.inc` 2.3KB, `fsx.inc` 1.4KB, `font.inc`, `softgfx.inc`, `icons.inc` on kern_small) |
| **wmui** | `wm.inc`, `ui.inc`, `menu.inc`, `dock.inc`, `dockmod.inc`, `fprog.inc`, `ctrl.inc`, `winmod.inc`, `extmod.inc`, `deskmod.inc`, `spinner.inc`, and the kernel's use of `apps/os88ui.inc` (shared with packages: a change there rebuilds every package that includes it, and the packages' total counts) | the damage-pass repaint (`wm_paint_dmg`, `wm_paint_all` - Part 5's budget is the bar), `ui_drag_size` (EXTD.DRV), `os88ui_sbar`; then a general sweep. `wm.inc` is 7.5KB of kern_small, the largest single file there |
| **files** | `files.inc`, `fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc`, `stikmenu.inc`, `assoc.inc`, `ascw.inc`, `dskwin.inc` | `fcp_paste`, `desk_paint_mask_x`, `assoc_ownex`/`assoc_run_x`, Sticky Apps' resident thunks; then a general sweep. `files.inc` is 7.2KB of kern_small |

## 3. What is different from pass 19's environment

* **Five agents at once on 4 cores and 15 GB, ~29 GB of disk.** `make -j2`,
  never two builds in your own tree at once, at most ONE MartyPC instance of
  your own. **Agents run NO soak rows** (CLAUDE.md's Testing section: this
  pass is batches, and the coordinator runs the deduplicated union once after
  the last merge): you verify on the host (`make`, which runs the fast tier;
  `make small`, `make emu`, `make mega`; `kernsize`; `stkbalance`; a unicorn
  harness or a reading for equivalence) and NAME the `tests/suite.py` rows
  that cover each change. One boot to LOOK at a change is allowed; re-running
  rows is not. A number that DECIDES a design (an int 13h count, a cycle
  count on a hot path) is taken once by you and written in your note. The first
  `make` in a fresh worktree builds everything (several minutes while four
  others do the same): start it first. **Remove nothing outside your
  worktree; never `git stash`** (one stack for every worktree).
* Your kern_small figures: `make -j2 small`, then `python3 tools/kernsize.py
  --json --build build/smallk --ico build/smallk -DKERN_SMALL` (`make` alone
  also builds kern_small through its sub-make). kern_emu: `make -j2 emu`,
  `--build build/emuk --ico build/emuk -DKERN_BIG -DKERN_EMU`. kern_mega:
  `make -j2 mega`, `--build build/megak --ico build/megak -DKERN_BIG
  -DKERN_MEGA`.
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree before your first emulator row. If it is
  not built yet, wait for it rather than building a second copy.
* `apps/wireball/art/wbfront.inc`: if it shows as modified, `git checkout`
  it before you commit, never commit it.
* Commit messages end in `(kernel size pass 20)` and carry the attribution
  trailer the session gives you. Never a model name in a commit or a file.
  After adding your note, run `python3 tools/os88index.py` and commit
  `docs/INDEX.md` with it.

**Your final message to the coordinator is ONE message**: the branch tip,
the resident delta on each of the four kernels (absolute base and tip
`kernsize --json` figures), kern_small's `accrued` line at your tip, the
WANTS THE OWNER items, the rows you NAME as covering your changes (and any
you ran, with results), and anything that blocked you.
