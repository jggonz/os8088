# Kernel size pass 13: the brief every agent reads first

Branch `kernel-size-13`, cut from `elendilon` at `4833dfef`. The pass before
this one was **pass 12** (its record is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P13.md`). Each agent works in a git worktree of its own, created by the
coordinator at `/home/user/wt/<name>` on a branch `ksp13-<name>` cut from the
commit that adds this file, and writes its record to
`docs/plans/completed/ksp13/<name>.md`. The coordinator merges the branches;
**agents do not push**.

Read before writing any code, in this order:

1. this file;
2. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 - pass 1's
   method and the mistakes that cost it time (the authority on method);
3. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P13.md` - pass 12's record,
   especially 1.2 (the shapes that recurred), 3 (its ABI wishes), 5 (what is
   left) and 7 (method lessons);
4. **the REFUSED lists in `docs/plans/completed/ksp12/*.md` and
   `docs/plans/completed/ksp11/*.md`** before proposing anything in a file
   those agents touched - a refused item is not re-proposed unless the reason
   it was refused no longer holds, and then the note says what changed;
5. CLAUDE.md's Hard rules (8086 only, near model, SS != DS, register
   discipline, section discipline, label hygiene) and its banner: **bytes,
   not rungs**.

## 0. The brief

The owner's words: *"This will be a whole kernel pass, anything is a valid
target; but there is a set of recent unpassed work, and each concept should
get an agent to work through it. Note that most of the 'that looks big'
pieces of the kernel have had multiple passes already; you can look there,
but innovative approaches, combinations, refactors may be needed. Keep hot
paths fast, and if it is going to change functionality run it by me first.
.boot2 can grow a sector or two if it needs to, though it should still have
a few spare bytes at this point."*

**The goal is 1 KB (1,024 bytes) off RESIDENT, more welcome.** Resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`, read with
`tools/kernsize.py --json`. kern_big is where the unpassed bytes are and is
the primary target; kern_small and kern_emu count too and are reported.

**Base** (`4833dfef`, assembled):

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 43,756 | 5,223 | 38,084 | 5,358 | 336 | **92,757** | 5,129 | 5,106 | 98,816 |
| kern_small | 31,847 | 3,063 | 22,482 | 2,868 | 0 | **60,260** | 4,364 | 1,480 | 62,464 |

kern_big's cold rung was CROSSED by the unpassed work since the last bless
(38,084 of 38,400: 316 left, 196 accrued). That is reported for the record
only - the target is bytes.

### 0.1 What has not had a pass

Measured routine by routine (`tools/os88rdiff.py kernel 3bb038f3`), pass 12's
close (`3bb038f3`, the tree, not an ancestor - the history was re-cut at the
#264 squash) against the base. kern_big resident, by concept:

| agent | concept | commits | kern_big resident | outside it | ABI |
|---|---|---|---:|---|---|
| **assoc** | `ASSOC.DAT` written back at runtime (SPEC.md 54.7.6): `asc_learn`, `asc_post`, `asc_flush`, `osapi_assoc_flush_x`, the installer's hold, the 16/32 slot tables, `kernel/ascw.inc` | `13c9752f` `eb90b2ca` `52fb5822`, the `4833dfef` merge | ~+300 (`assoc.inc` +264, `diskw.inc` +47) | `FORMAT.DRV` (`.modf`) +1,977 - the writer rides it | **free**: `OSAPI_ASSOC_FLUSH` and its `ASSOC_FL_*` are not on `main` |
| **theme** | EGA Color themes with title stripes (#249); Theme page Customize (SPEC.md 76.14.10, `thmv_*`); WIN95 follows the title style (#261) | `3987ad05` `bf1e4a05` `2bd968a4`, the `834c0a6b` merge | ~+210 (`vga12.inc` +195, `thmv_row` +16) | `CTRL.DRV` +~250, `WIN95.DRV` +61, `drivers/thmpal` | `OSAPI_THM_SET` and the #249 config keys are on `main`: FROZEN. Customize is free |
| **desk** | the desk layer, `DESK.DRV`, DIAs/Sticky Apps (SPEC.md 110, 11.103), `OSAPI_WM_DESK`, `OSAPI_GFX_DESK`, `OSAPI_CLK_GET` | `0a662504` `b1ea9155` | ~+150 (`wm.inc` +81, `deskmod.inc` +36, `clock.inc` +32, `mouse.inc` +16, `mod.inc`, `dock.inc`, `desk.inc` +11, `stik_boot` +66) | `DESK.DRV` (`.modj`) 235, `.modk` +24 | on `main`: FROZEN |
| **links** | `*.LNK` links and autostart from `\AUTOSTRT` (SPEC.md 26.10): `sc_walk`, `lnk_open`, `lnk_is`, `sc_m_lnk`, `as_boot` | `d5d2d211` `2bff6f77`, the `2cb3c15e` merge | ~+120 (`desksc.inc`'s link hunks; `sc_open` gave back 134) | `.boot2` +256 (the walk), `CTRL.DRV`'s `sc_m_*` +~300 | **free** |
| **misc** | everything else since `3bb038f3` that no agent above owns: a parted package's packed image (+33 `.cold`, both kernels); what Timer/Bounce leaving (#264) left behind (`apic_inst_minimize` +32, `inst_*`, the `cw_*` shims); #252's PR-side fixes (`dsk_img_orphan`/`dsk_img_host`/`dsk_vol_rmv` +84, `fcp_lrec` +40, `fm_dgdrop` +57); `ui_task` +30, `ui_drag` +11, `fsx_page` +10, `loader.inc`'s other +52 | `a7629386` `a123653b`, and the content of `b47de145`/`84a3038c` not above | ~+330 | | on `main` except `a7629386`: FROZEN except that |

The rest of the kernel is the whole-kernel agents', by area - **these are
not concept agents and are where most of the 1 KB has to come from**:

| agent | area (resident code and data in these files, bar the hunks a concept agent owns) |
|---|---|
| **wkfs** | the file system and the Disk window: `disk.inc`, `diskw.inc`, `files.inc`, `filecp.inc`, `fdlg.inc`, `drvvol.inc`, `dskwin.inc`, `fprog.inc`, `icons.inc`, `assoc.inc`, `clip.inc`, `lz.inc` |
| **wkgui** | the window system and drawing: `wm.inc`, `ui.inc`, `menu.inc`, `font.inc`, `vga12.inc`, `softgfx.inc`, `band.inc`, `blank.inc`, `toast.inc`, `dock.inc`, `desk.inc`, `mouse.inc`, `events.inc`, `vidsel.inc`, `viddet.inc`, `splash.inc`, `spinner.inc`, `clockw.inc` |
| **wkcore** | the core: `kernel.asm` (the API table, the cells, the shims, the ladder), `memory.inc`, `driver.inc`, `loader.inc`, `mod.inc`, `instance.inc`, `sched.inc`, `clock.inc`, `hiber.inc`, `shutdown.inc`, `fsx.inc`, `xmem.inc`, `snd.inc`, `cpudet.inc`, `boot/boot2.asm` and the `.ovl` overlay |

A cross-file agent runs on the merged tree afterwards and takes every agent's
*CROSS-FILE* list.

### 0.2 What the whole-kernel agents are for

Most of what looks big has had three or more passes, so a fourth pass of the
same peephole kind will find little. Look for **structure**:

* **Where code lives, not just how long it is.** A byte moved out of
  resident is a byte saved. The levers, in order of how often they have
  paid: (1) code only ever called from a MODULE (`CTRL.DRV`, `DISKOPS.DRV`,
  `FORMAT.DRV`, `HIBER.DRV`, `DOCK.DRV`, `EXTD.DRV`, `DESK.DRV`, `WIN95.DRV`,
  and on kern_small `FILECP.DRV`/`FDLG.DRV`) can move INTO that module
  (SPEC.md 2.8; `tools/os88ovlchk.py` says what may name what); (2) code only
  run during boot can move into `.ovl` or the blob (`.boot2`), which are given
  back (`.cold` is RESIDENT - CLAUDE.md says why); (3) data only a module
  reads can move into the module.
  **A move that makes a feature need the system disk where it did not before
  is a FUNCTIONALITY change**: record it as a proposal (section 4's *WANTS
  THE OWNER*) - do not take it.
* **Two routines that are one** across files (a second copy of a loop, a
  second spelling of a parse, a hand-rolled version of a primitive the
  kernel already has); a family of near-identical routines that wants a
  table; a shared epilogue reached by a near `jmp` (stack-neutral); a
  register convention that makes every caller set up the same thing.
* **Data**: tables a computation could replace, strings that share a tail,
  `.bss`/`.lowbss` buffers whose lifetimes never overlap and could share
  storage (prove it), a resident copy of something a module or the blob
  could rebuild.
* **The relaxed-jump remainder** (pass 12 section 5: ~68 on kern_big, ~22 on
  kern_small) - read the listing, and pass 11's table says which are
  deliberate.

### 0.3 Why `.cold` is worth more than it used to be

`kernsize[big]: rom .cold 38,084 of the 40,944 the 40KB window holds` - the
ROM arm (SPEC.md 2.10) puts `.cold` in a 5150's spare ROM sockets, and the
HMA arm loads it above 1 MB; both pack themselves, so a `.cold` byte is a
byte of those windows too.

## 1. The rules this pass

* **Functionality**: the owner's words are *"if it is going to change
  functionality run it by me first."* An item that changes what the user
  sees or can do - a message reworded or dropped, a feature that needs the
  system disk where it did not, a limit lowered, a behaviour in an edge case
  - is NOT taken: it goes in the note's *WANTS THE OWNER* with its bytes,
  and the coordinator puts it to the owner. Byte-identical behaviour is the
  bar, including on the refusal paths.
* **ABI**: `main` is `origin/main` at `a123653b`. An interface is FROZEN if it
  is in `main`'s `apps/os88api.inc`, `drivers/os88drv.inc`, `apps/cc/os88.h`,
  a cell offset, a module's or driver's header, a file format or a
  `SYSTEM.CFG` key.
  `git diff origin/main HEAD -- apps/os88api.inc drivers/os88drv.inc` is the
  list of what is NOT frozen: `OSAPI_WM_QUIET` (0x048D), `OSAPI_MEM_HMA`
  (0x0487), `OSAPI_ASSOC_FLUSH` (0x0493) and its `ASSOC_FL_*`,
  `DRVM_CEIL_NET`'s value, and the HMA driver contract. Those may change
  freely - **so pass 12's first ABI wish (`OSAPI_MEM_HMA` as an arm of
  `OSAPI_MEM_CLAIM_DMA_HI`/`OSAPI_MEM_FREE`, ~-14/-6) is wkcore's to take**.
  Keep the SDK, the drivers and the packages in step; `abilock.py` records
  it (SPEC.md 20.2.0.1). A frozen one that would pay to break goes in
  *WANTS AN ABI BREAK*, with bytes and callers.
* Kernel-internal interfaces (`_x` routines, `.bss` layout, module entry
  tables only the kernel and its modules share) are not ABI - but a
  module/kernel interface is checked by `os88mod.py`, so change both halves.
* **Hot paths are off limits for anything that costs cycles**: per-pixel
  loops, the mouse ISRs, IRQ0/`sch_isr`, `sch_switch`, the API cell,
  `font_run`, `gfx_blit4`/`gfx_blit1`'s per-run loops, `dsk_xfer`'s
  per-sector path, `ui_task`'s idle pass. A change there must be cycle-neutral
  or faster, with the count in the note. **Parity**: after your last commit,
  run `tools/os88rdiff.py kernel <base> --hot ...` over the hot list above
  and put every hot routine back at an EVEN shift against the base (pass 12
  section 2 did it for +2 bytes).
* **`.boot2`** may grow by a sector or two if a move into it pays; say so in
  the note with `BOOT2_SECS` before and after, and re-check `KSIG_OFF`'s band
  (`tests/unit/t_canary.py` is in the fast tier).
* **Ownership**: a concept agent owns the hunks its concept added since
  `3bb038f3` (`git diff 3bb038f3 HEAD -- <file>` shows them) plus anything it
  finds inside the routines they touched. A whole-kernel agent owns
  everything else in its files and must not edit a concept hunk. Anything
  outside your ownership goes in your note's *CROSS-FILE* list with its bytes.
  Two agents editing the same lines is the one thing that costs the
  coordinator a day.

## 2. Method (passes 11 and 12, which worked)

* **First command**: `cd /home/user/wt/<name> && git status` - you are on
  `ksp13-<name>`; if you are not, stop and say so. Never `cd` into
  `/home/user/os8088` to build or edit: that is the coordinator's tree.
* **Measure base and tip yourself**, absolute figures from
  `tools/kernsize.py --json` on all three kernels: kern_big (`make`),
  kern_small (`make small`, then `--build build/smallk --ico build/smallk
  -DKERN_SMALL`), kern_emu (`make emu`, `--build build/emuk --ico build/emuk
  -DKERN_BIG -DKERN_EMU`). Quote **assembled** bytes, never estimates.
  `kernsize`'s printed deltas are against the blessed baseline, not your
  base - use the absolutes. Module and driver images: the UNPACKED image
  (`os88drv.image_unwrap`), and say whether a saving fell into a pad.
* `tools/os88rdiff.py kernel <base>` (add `-D KERN_SMALL` for kern_small) is
  how you see every routine you changed, what relaxed, and what shifted odd.
* **Commit in small batches**, each gated by `make -j2`, `make -j2 small` and
  the fast tier (`python3 tools/os88test.py fast`), plus `make emu` at your
  tip. `tools/stkbalance.py` over every file you touched, base against tip:
  no new unbalanced path. Run `python3 tools/checkdocs.py` AFTER `git add`.
* **Equivalence**: where no row reaches a path, prove it another way - a
  scratch unicorn harness, a model, a reading recorded in the note. "It
  assembled" is not equivalence.
* **Rows**: name the `tests/suite.py` rows covering what you changed (grep
  it). You may run THE ROWS FOR YOUR OWN SUBJECTS, scoped with `-k`, ONCE,
  after your last code commit (`python3 tools/os88test.py soak -k '<row>' -k
  ... --marty-jobs 1`) - one pass, not one per batch, and a row is a row
  however it is started (CLAUDE.md's Testing section). **NEVER the whole soak
  tier**, never `test-full` (the coordinator runs it once at the close). Do
  not run a row while you are editing kernel sources in the same tree. Break
  the thing you changed on purpose once where a row is new to you, and watch
  it go red. A row that fails: re-run it alone on the BASE before blaming
  yourself (`python3 tools/os88bisect.py classify <row>`).
* **The shapes that paid last time** (P13 handoff 1.2): single-caller routines
  written at the call; a compare chain that wants a table (and the converse
  on kern_small); a list a callee already filters; a figure computed per call
  that boot fixes; module scratch in the claim's KB rounding; two
  near-identical routines folded; copied code whose reason was false; code
  assembled on a kernel arm where its guard can never pass; register banks a
  callee did not need; relaxed jumps.

## 3. The environment

* 4 cores, 15 GB, shared by eight agents. Use `make -j2`. At most ONE MartyPC
  instance of your own at a time (`--marty-jobs 1`).
* `make deps` has run. MartyPC is built ONCE in the main tree: in your
  worktree run `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` before your first emulator row. **There is no IBM ROM in
  this container**: rows go through the GLaBIOS twins (`os88ui.boot()`
  substitutes them), and a row that needs the IBM ROM itself SKIPs - say so.
* A fresh worktree has no `build/`: the first `make` builds everything
  (several minutes under eight agents - start it first).

## 4. The note

`docs/plans/completed/ksp13/<name>.md`, pass 12's shape
(`docs/plans/completed/ksp12/theme.md` is a good model): BASE AND TIP (a
table, all three kernels and any image), TAKEN (each item with its bytes per
kernel and its cycles if on a hot path), REFUSED (with the bytes it would
have been and why), WANTS THE OWNER (functionality changes, with bytes),
WANTS AN ABI BREAK, CROSS-FILE, ROWS RUN (with results), DEFECTS FOUND
(fixed or not, and how found). After adding the note run
`python3 tools/os88index.py` (docs/INDEX.md is generated) and commit both.
Commit messages end in `(kernel size pass 13)` and carry the attribution
trailer the session gives you. Never a model name in a commit or a file.

Your final message to the coordinator: the branch tip, the resident delta on
each kernel, the WANTS THE OWNER items, and anything that blocked you.
