# Kernel size pass 12: the brief every agent reads first

Branch `kernel-size-12`, cut from `elendilon` at `47578d58`. Each agent works
in a worktree of its own on a branch `ksp12-<name>` cut from that commit, and
writes its record to `docs/plans/completed/ksp12/<name>.md`. The coordinator merges the
branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` §3-§6 - pass 1's method and
   the mistakes that cost it time (the authority on method);
3. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P12.md` - pass 11's record,
   especially 1.2 (the shapes that recurred) and §6 (method lessons);
4. **the REFUSED lists in `docs/plans/completed/ksp11/*.md`** before
   proposing anything in a file those agents touched;
5. `docs/reports/KERNEL-BYTES-SINCE-SQUASH-2026-10-14.md`, *What has not had a
   pass*: the table this pass works from;
6. CLAUDE.md's Hard rules (8086 only, near model, SS != DS, register
   discipline, section discipline, label hygiene) and its banner: **bytes, not
   rungs**.

## 0. The brief

The owner's words: *"Today's primary focus is the ~1k of kernel code added
that has not yet had any size pass. The goal is a 30% reduction, although more
would be happily accepted. Optimizations spotted while working are all in
scope. Today we are trying to not break ABIs (which is a difference from a
normal pass), but if anyone spots an optimization that would work better with
a break then record it, and it will likely be taken in the next pass. ABIs
not yet on main CAN be broken - they haven't gone anywhere yet."*

### 0.1 What has not had a pass

Two populations. Measured with `tools/kernsize.py --json`, resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.

**(a) Before the 2026-10-14 report, listed there as never passed** (all of it
is now ON `main`, squashed in #252 - so its ABI is FROZEN this pass). Its
unsquashed commits are on `origin/elendilon-old` (and partly
`origin/elendilon-pr`), not on `elendilon`, whose history restarts at the
#248 squash `758eb807`:

| | commits | kern_big | kern_small | outside the resident count |
|---|---|---:|---:|---|
| Mount a `.IMG` | `c356413` `cc8d977` `d69e19b` `f84001b` `066c054` | +355 | -28 | `DISKOPS.DRV` +1,078 |
| Windows theme | `c4b43d0` `62b7505` | +232 | 0 | `WIN95.DRV` 1,565, `CTRL.DRV` +77 |
| Two file classes | `5ebea5a` | +103 | 0 | `HIBER.DRV` +4 |
| `OSAPI_PKG_REHOME`'s keep form | `c04ffc4` | +41 | 0 | |
| Force Color | `c461c5b` | +29 | 0 | blob +15, `CTRL.DRV` +238 |
| `OSAPI_FERR_TEXT` | `4094670` | +25 | +25 | |
| `ICON_SOLID` | `ac7b5b1` | +15 | 0 | |
| a failed mount (`78298be`) | `78298be` | +15 | +15 | |
| the `DA` key, `HDD.DRV` autoload, the rename | `edd0efb` `8cead00` `a32dfc3` | +4 | +2 | blob +86, window +30, `CTRL.DRV` +44 |
| `main`: #244 `LD_EVER`, #248 port pins | `c193155` `dde6b19` | +46 | +40 | blob +20, `CTRL.DRV` +686 |
| **subtotal** | | **+865** | **+54** | |

**(b) Since the report** (`b6acc4f7` .. `47578d58`, on `elendilon` and NOT on
`main` - so its ABI may be broken freely):

| | kern_big | kern_small |
|---|---:|---:|
| resident | **+435** (`.text` +127, `.cold` +310, `.bss` -2) | **+178** (`.text` +102, `.cold` +76) |
| also | `.boot2` +27, `.ovl` -3, **KERN_SIZE 98,816 -> 99,328 (the cold rung crossed)** | `.boot2` +25 |

by file on kern_big: `driver.inc` +154, `memory.inc` +98, `mod.inc` +51,
`fsx.inc` +36, `wm.inc` +33, `blank.inc` +28, `kernel.asm` +15, `viddet.inc`
+14, `disk.inc` +7, `fprog.inc` +7, `vga12.inc` +4, a few -1s.

| commit | what |
|---|---|
| `ae906414` | ROM: `.cold` in a 5150's U28-U32 sockets (SPEC.md 2.10, docs/plans/ROM-PLAN.md) |
| `6e5d2a1c` `12f48233` `2d5dbdf6` `9690c3bb` | HMA: `.cold` from the HMA, the heap gets the cold rung, SOUND/HDD/NET from the HMA, `OSAPI_MEM_HMA` (SPEC.md 2.10.7, 2.10.8, docs/plans/HMA-PLAN.md) |
| `477376be` | Saver vs FTPD: no file widget over a session, `OSAPI_WM_QUIET` (79.6.2, 79.10) |
| `145c25cc` | FTP downloads: a TCP send window (`ETHER.DRV`), FTPD polls its ACK (72.4.3, 77.51) |
| `a0fa096c` `f2e525d9` | Video Player on the display that can; `vid_apply`'s first-tick stores |
| `5b931426` | Uncompress To...: Enter re-reads a swapped-back floppy (`DISKOPS.DRV`'s `compress.inc`) |

**Total unpassed: ~1,300 kern_big, ~230 kern_small.** 30% is **~390 bytes on
kern_big and ~70 on kern_small**; the passes before this one were briefed at
50%, so more is welcome. Module images (`CTRL.DRV`, `DISKOPS.DRV`,
`WIN95.DRV`) and driver images are in scope - they are RAM while loaded and
disk always - but **resident bytes come first**.

### 0.2 Why `.cold` is worth more than it used to be

Two lines `make` prints now:

* `kernsize[big]: rom .cold 38,690 of the 40,944 the 40KB window holds before
  its tail -> 2,254 for the tail and spare`. The ROM arm (SPEC.md 2.10) puts
  `.cold` in a 5150's spare ROM sockets, and the window is ONE option ROM:
  every `.cold` byte is a byte of that window.
* kern_big's cold rung was crossed by (b): 290/512 accrued. Its low rung is
  at 478/512.

## 1. The ABI rule this pass

* **`main` is `origin/main` at `b47de145`.** An interface is FROZEN if it is
  in `main`'s `apps/os88api.inc`, `drivers/os88drv.inc`, `apps/cc/os88.h`,
  `kernel/` cell offsets, a module's or driver's header, a file format or
  `SYSTEM.CFG` key. Do not change a frozen one.
  `git diff origin/main origin/elendilon -- apps/os88api.inc drivers/os88drv.inc`
  is the list of what is NOT frozen: `OSAPI_MEM_HMA` (0x046D),
  `OSAPI_WM_QUIET` (0x0473), the HMA driver contract, `DRVM_CEIL_NET`'s
  value, and anything else that diff shows. Those may change freely (keep the
  SDK, the drivers and the packages that use them in step; `abilock.py`
  records it - SPEC.md 20.2.0.1).
* **An optimisation that wants a frozen ABI broken is RECORDED, not taken**:
  a section *WANTS AN ABI BREAK* in your note, with the bytes on each kernel
  (assembled where you can, estimated and marked so where you cannot), what
  breaks, and who calls it. The next pass will likely take it.
* Kernel-internal interfaces (`_x` routines, `.bss` layout, module entry
  tables that only the kernel and its own modules share) are not ABI - but a
  module/kernel interface is checked by `os88mod.py`, so change both halves.

## 2. Method (pass 11's, which worked)

* **Measure base and tip yourself**, absolute figures from
  `tools/kernsize.py --json` on all three kernels: kern_big (`make`), kern_small
  (`make small`, then `--build build/smallk --ico build/smallk -DKERN_SMALL`),
  kern_emu (`make emu`, `--build build/emuk --ico build/emuk -DKERN_BIG
  -DKERN_EMU`). Quote **assembled** bytes, never estimates. `kernsize`'s
  printed deltas are against the blessed baseline at `b6acc4f7`, not against
  your base - use the absolutes. Module and driver images: the UNPACKED
  image (`os88drv.image_unwrap`), and say whether a saving fell into an
  `align 512` pad.
* Where it matters, also the ROM arm (`make rom`, SPEC.md 2.10.3) and the HMA
  arm (the `COLDHMA.SYS` build, 2.10.7) - **the hma and rom agents must**.
* **Commit in small batches**, each gated by `make -j2`, `make -j2 small` and
  the fast tier (`python3 tools/os88test.py fast`), plus `make emu` at your
  tip. `tools/stkbalance.py` over every file you touched, base against tip:
  no new unbalanced path.
* **Equivalence**: where no row reaches a path, prove it another way - a
  scratch unicorn harness (pass 11 did two), a model, a reading recorded in
  the note. "It assembled" is not equivalence.
* **Rows**: the soak rows for the subjects you touched, scoped with `-k`
  (`python3 tools/os88test.py soak -k '<rows>'`, or `tools/os88soak.py start
  -k ...`). **NEVER the whole soak tier** - it is the owner's to ask for. Find
  the rows with `grep` over `tests/suite.py`. **Do not run a row while you are
  editing kernel sources in the same tree** (`os88sym` then reports "the map
  describes a DIFFERENT kernel"). Break the thing on purpose once if the row
  is new to you, and see it go red.
* **Hot paths are off limits for anything that costs cycles**: per-pixel
  loops, the mouse ISR, IRQ0/`sch_isr`, `sch_switch`, the API cell,
  `font_run`, `dsk_xfer`'s per-sector path, `gfx_blit4`'s per-run loop. A
  change there must be cycle-neutral or faster, with the count in the note.
* **The shapes that paid last time** (P12 handoff 1.2): code assembled on a
  kernel arm where its guard can never pass; a table copy the kernel keeps of
  something it reads one cell of; relaxed jumps (a `jcc` assembled NEAR - read
  the listing); register banks a callee did not need; single-caller routines
  written at the call site; a `.bss` word whose twin holds the same value; a
  compare chain that wants a table; an unpublished cell folded into a
  published one's reserved register (only where the published one is not
  frozen).
* **Ownership**: you own the hunks your concept's commits added, plus
  anything you find inside the routines they touched. Anything else you spot
  goes in your note's *CROSS-FILE* list with its bytes - the cross-file agent
  takes it after the merge. Two agents editing the same lines is the one
  thing that costs the coordinator time.

## 3. The environment

* 4 cores, 15 GB, shared by up to eight agents. Use `make -j2`. At most ONE
  MartyPC instance of your own at a time (`--marty-jobs 1`), and prefer the
  rows you can name over broad patterns.
* `make deps` has run. MartyPC is built ONCE in the main tree: in your
  worktree run `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` before your first emulator row (instances run in private
  directories, so sharing the binary is safe). The IBM 5150 ROM (27OCT82,
  U33) is at `/home/user/os8088/tools/martypc/roms/` and is gitignored: copy
  it into your worktree's `tools/martypc/roms/`. It is what
  `tests/int0sweep.py` and every IBM-machine row needs, and this is the first
  pass that has it.
* A fresh worktree has no `build/`: the first `make` builds everything.

## 4. The note

`docs/plans/completed/ksp12/<name>.md`, pass 11's shape (`ksp11/caption.md` is a good
model): BASE AND TIP (a table, all three kernels and any image), TAKEN (each
item with its bytes per kernel and its cycles if on a hot path), REFUSED (with
the bytes it would have been and why), WANTS AN ABI BREAK, CROSS-FILE, ROWS
RUN (with results), DEFECTS FOUND (fixed or not, and how found). Commit
messages end in `(kernel size pass 12)` and carry the attribution trailer the
session gives you. Never a model name in a commit or a file.
