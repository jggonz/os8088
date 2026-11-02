# kern_mega size pass 2: the brief every agent reads first

Branch `mega-size-2`, cut from `heap-size` at `3d5acee7`. This is kern_mega's
**second** size pass; pass 1's record is `docs/plans/completed/msp1/` and its
DEFERRED lists are where this pass starts. Each agent works in a git worktree
of its own at `/home/user/wt/<name>`, on a branch `msp2-<name>` cut from the
commit that adds this file, and writes its record to
`docs/plans/completed/msp2/<name>.md`. The coordinator merges the branches;
**agents do not push**.

## 0. The brief

The owner's words: *"We will be continuing with a second size pass of kern
mega. ... Things found that also shrink other kernels ARE in scope. Make or
continue listing ABI breaks that would clean up the ABI overall, or help with
sizing. We will not take them this round, but having the list built gives us
a starting point for when we do."*

So: **no byte goal; the target is waste in kern_mega**, with wins that also
shrink kern_big, kern_small or kern_emu welcome (measured on every kernel
they reach). **Every ABI break you see that would pay - in bytes, or in a
cleaner ABI - goes in your note's WANTS AN ABI BREAK with its estimated
bytes and what it would break**; the `abilist` agent is building the
consolidated list (`docs/plans/ABI-BREAK-LIST.md`) and the coordinator folds
yours into it. None is taken this round.

Read before writing any code, in this order:

1. this file;
2. **pass 1's note for your area** (`docs/plans/completed/msp1/<area>.md`) -
   its DEFERRED, REFUSED and CROSS-FILE lists. A REFUSED item is not
   re-proposed without a new argument;
3. `docs/plans/completed/msp1/BRIEF.md` sections 0.3-3 - the rules and the
   method; **this brief adapts it and wins where they differ**;
   `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6;
4. `docs/plans/KERN-MEGA-PLAN.md` (its head, and the parts of section 14 that
   name your code), `docs/plans/completed/KERN-MEGA-SEGMENTS-PLAN.md` (what
   `.mega` is and who may name it), and
   `docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md` if you own memory code;
5. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**.

### 0.1 What kern_mega is made of (base, assembled)

`python3 tools/kernsize.py --build build/megak --json` after `make mega`, and
`python3 tools/os88megaseg.py` for the per-file split against kern_big:

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.mega` (+scratch) | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 40,957 | 5,390 | 37,573 | 5,358 | 336 | - | 6,456 | 5,119 | 97,280 |
| kern_mega | 46,579 | 5,427 | 46,165 | 6,751 | 336 | 21,440 (+1,490) | 6,709 | 5,118 | 134,656 |

kern_mega's footprint after the switch is **65,024 of `KERN_BUDGET` 68,096**
(3,072 spare); `.text`+`.bss` is 52,006 of the 64KB window (13,530 left).
`os88megaseg`'s `.text` delta over kern_big: `mega.inc` +1,681, `vga12.inc`
+1,541 (kern_mega carries `.vga` inside `.text`), `?macro` +817,
`megados.inc` +353, `fsx.inc` +330, `mouse.inc` +251, `kernel.asm` +172,
`icons.inc` +112; `.cold`: `mega.inc` +3,731, `memory.inc` +1,233 (the heap
granule work since pass 1 added kern_mega arms here), `?macro` +699,
`driver.inc` +625, `disk.inc` +575, `diskw.inc` +510, `fsx.inc` +444,
`desksc.inc` +221.

**Where a byte lives matters**: `.cold` and `.mega` go above 1MB at the
switch; what stays below 1MB is `.text` + `.bss` + `.lowbss` + `.vgabuf`. **A
conventional byte is worth more than an above-1MB byte**, and a move from
`.text` into `.mega`/`.cold` that keeps the hot path fast is a real saving.
`KERN_SIZE` at boot matters too - say what yours did.

### 0.2 The eight agents and what each owns

Line numbers are `kernel/mega.inc` at the base. A hunk that straddles a
boundary belongs to the lower range.

| agent | owns | start from |
|---|---|---|
| **pmcore** | `mega.inc` **1-5019**: the switch, GDT/LDT/IDT, IRQ/ROM halves (`mega_t08`, `mega_k09`, `mega_i16`), reflection (`mega_svc*`), `mega_tell`, the fault reporter and the #GP fixups, the E9 macro/strings block | msp1/pmcore.md DEFERRED (bootstrap GDT 48, `mega_k09` tables, `mega_svc_back` twins, `mega_report` table, the `mega_*f` doors once callers name `COLD_SEG:`), `mega_pic_pm` taking its base in AH (msp1/shared CROSS-FILE) |
| **pmfd** | `mega.inc` **5020-8035**: the API cells' kern_mega stubs, hex/string helpers, the huge-claim code, the native floppy | msp1/pmfd.md DEFERRED (the `.fall` table, the command runs, the `mega_hb*` frame, PM-only state out of `.text`) |
| **pmmem** | `mega.inc` **8036-end**, and every `mx_tab`/`MX_*` access site in any file | **msp1/pmmem DEFERRED 3 is the headline**: `mx_tab` (960), `mega_wtab` (128), `mem_sel` (64), `mega_ustab` (32) - 1,184 bytes of `.lowbss`, conventional for ever, read only in protected mode. Then items 1-2, 5-12 |
| **megados** | `kernel/megados.inc`, `megapark.inc`, `megares.inc` | msp1/pmmem CROSS-FILE (nine `mega_lincopyf` callers -> `mx_lcopy`, -18 `.mega`, then -6 `.text`), msp1/pmcore CROSS-FILE (`mega_irqromf`), msp1/shared DEFERRED 4 (~170 bytes of `.text` data written only by `.mega`/`hiber.inc`) and 5 (`mega_dropr`'s ICWs) |
| **shared** | every `%ifdef KERN_MEGA` hunk in the kern_big files **except** the CSDO/KSEG sites `modcsdo` owns and the routines `bootonce` owns: `memory.inc`, `disk.inc`, `driver.inc`, `diskw.inc`, `loader.inc`, `mod.inc`, `lz.inc`, `fsx.inc`, `mouse.inc`, `icons.inc`, `snd.inc`, `font.inc`, `menu.inc`, `wm.inc`, `sched.inc`, `vga12.inc`, `xmem.inc`, `hiber.inc`'s non-CSDO hunks, and **`kernel/kernel.asm`** (bar kmain's order, which is `bootonce`'s); the `?macro` growth (+817 `.text`, +699 `.cold`) | msp1/shared DEFERRED 2, 3, 7, 8, 9; the door callers in its files (`lz.inc`, `fsx.inc`'s `mega_lincopyf`, ... -> `COLD_SEG:` bodies, msp1/pmcore CROSS-FILE); the `.text` kern_mega arms of `vga12.inc`/`mouse.inc`/`fsx.inc`; the heap granule work's new kern_mega arms in `memory.inc` (never size-passed) |
| **modcsdo** | the CSDO/KSEG sites in the on-demand modules and `.modl`: `compress.inc` (125), `stikmenu.inc` (105), `ascw.inc` (51), `hiber.inc` (43), `serial.inc`, `shutdown.inc`, `desksc.inc`, `winmod.inc`, `dockmod.inc`, `ctrl.inc` - and the whole of `compress.inc`, `stikmenu.inc`, `ascw.inc`, `serial.inc`, `shutdown.inc`, `winmod.inc`, `dockmod.inc` | msp1/shared DEFERRED 1: load DS ONCE per routine instead of per site (est. -1,500 in `compress.inc` alone), a near door per module for what is left. Module images are disk and heap while loaded, not resident - quote image and claim |
| **bootonce** | the mechanism for code that runs ONCE around the switch, and **kmain's order**: msp1/pmcore's ~370 (`mega_frames_fix`, `mega_mods_fix`, `mega_ivt_sync`'s first build, `mega_strcal`), msp1/pmmem DEFERRED 4's ~670 (`mega_gatef`, `mega_coldupf`, `mega_a20`/`a20s`/`a20t`/`kbw`, `mega_tasks_ok`, `mega_tlowf`, `mega_tset`, `mega_rung`, `mega_mdesc`/`hilin`), msp1/shared DEFERRED 10's ~660 (`mem_hicaches_f`, `mem_hiclaims`, `as_bootc`, `drv_hiboot_f`, `mod_hiboot`), and msp1/pmcore's `mega_bgdt`/`mega_bwin` | whether the switch's real-mode half can run before `mem_unblob` (so it lives in the blob and is given back - `KERN_SIZE` and `.cold` both fall), or a `.mega` init tail the kernel drops after `mega_pmtail`. **Those routines are bootonce's to MOVE**; every other agent leaves them alone |
| **abilist** | no kernel code: `docs/plans/ABI-BREAK-LIST.md` | every WANTS AN ABI BREAK section in `docs/plans/completed/ksp*/`, `msp1/`, the `HANDOFF-*` and `apps*` size-pass records, plus an audit of `apps/os88api.inc`, `drivers/os88drv.inc`, `apps/cc/os88.h`, `apps/os88mega.inc` and SPEC.md 20 for cleanup candidates (dead or `stc`/`ret` cells, duplicate doors, inconsistent register contracts, header fields nothing reads). See section 4 |

**Other opportunities seen in your code are yours** (a neighbour, a routine
your code calls); anything in another agent's range goes in *CROSS-FILE* with
its bytes, not into your diff. kern_big-only code (no `KERN_MEGA` anywhere
near it) in a file you own is in scope this pass - **measure it on every
kernel it reaches** and keep kern_small and kern_emu building.

## 1. The rules this pass

msp1/BRIEF.md section 1 binds, with these changes:

* **kern_big may change** when the change is meant for it (in scope this
  round): then it is measured on kern_big, kern_small and kern_emu as well as
  kern_mega, and the hot-path rule applies there too. A change NOT meant for
  kern_big must leave it byte-identical - check it (msp1/BRIEF section 1's
  method: same build number, or `kernsize --json` sections and
  `tools/os88rdiff.py`). **kern_small's budget is met exactly at a merge**
  (CLAUDE.md, rule 1 of memory): a kern_small change that grows it is not
  taken without saying so.
* **ABI**: nothing in `apps/os88api.inc`, `drivers/os88drv.inc`,
  `apps/cc/os88.h`, `apps/os88mega.inc`, a cell offset, a module's or
  driver's header, or a file format changes. `make` fails if one does
  (`tools/abi.lock.json`). One that would pay goes in WANTS AN ABI BREAK.
* **Functionality**: byte-identical behaviour; a change to what the user sees
  goes in WANTS THE OWNER. E9 output on the shipped kern_mega is already gone
  (msp1/e9gate.md) - check `MEGA_E9` before assuming a string ships.
* **Hot paths** as msp1/BRIEF section 1, cycle-neutral or faster, count in
  the note; `hotpar` (fast tier) stays green.
* **The 286 reset path** runs on neither QEMU nor Bochs - but it runs on
  86Box (docs/86BOX.md, `vm/286-mega`), which the coordinator built
  mid-pass on the owner's word: prove equivalence by reading, and NAME the
  86Box check; the coordinator runs those after the merge.

## 2. Method

msp1/BRIEF.md section 2 binds, with these changes:

* **First command**: `cd /home/user/wt/<name> && git status` - you are on
  `msp2-<name>`. **Never edit or build in `/home/user/os8088`.**
* **Scratch**: `/tmp/msp2/<name>/`. Never `git stash`.
* **Builds**: 4 cores and 15 GB shared by SEVEN building agents - **`make
  -j1` only**. Start `make -j1 && make -j1 mega` first thing; it takes a
  while. Most batches need only `make -j1 mega` to measure; plain `make -j1`
  (fast tier) before each commit; `make -j1 small` / `make -j1 emu` if you
  touched code they assemble.
* **Measure base and tip yourself**: base figures are in
  `/tmp/msp2/base-mega.json` and `/tmp/msp2/base-big.json` (from the
  coordinator's tree at the base commit; your worktree's own base build is
  equal bar the build number).
* **Commits**: small batches, messages ending `(kern_mega size pass 2)` and
  the session's attribution trailer, never a model name. Gate: `make -j1
  mega`, `make -j1`, `python3 tools/os88ovlchk.py` if you moved code between
  sections, `tools/stkbalance.py` over touched files, `python3
  tools/checkdocs.py` after `git add`.
* **Rows**: name the rows (`mega-*` in `tests/megaport.txt`, `mega*` in
  `tests/suite.py`, and for a kern_big change the rows about what you
  touched). **Do NOT run emulator rows** - the coordinator runs the
  deduplicated union of every agent's named rows ONCE after the merge (the
  owner's rule: soak runs once, after the last batch). Host checks only.

## 3. The note

`docs/plans/completed/msp2/<name>.md`, msp1's shape: **BASE AND TIP** (kern_mega
all sections, conventional-resident total, KERN_SIZE; kern_big/small/emu
byte-identical or their delta), **TAKEN** (bytes per section per kernel,
cycles if hot), **REFUSED**, **DEFERRED** (estimated bytes, labelled - pass 3's
work list, be generous), **WANTS THE OWNER**, **WANTS AN ABI BREAK** (each:
what changes, who it breaks, estimated bytes or what it cleans), **CROSS-FILE**,
**ROWS TO RUN** (named, not run), **DEFECTS FOUND**. After adding the note run
`python3 tools/os88index.py` and commit both.

**Your final message to the coordinator is ONE message**: branch tip, the
kern_mega delta per section and conventional-resident total, other kernels'
deltas, WANTS THE OWNER, WANTS AN ABI BREAK (one line each), rows to run,
and anything that blocked you.

## 4. abilist

`docs/plans/ABI-BREAK-LIST.md` is a WORK LIST for a future flag day, not a
decision. One row per break: an id, what changes (cell, header field,
register contract, file format, SDK macro), who it breaks (every package,
drivers, C packages, modules, files on disk), what it buys (resident bytes on
which kernel, package bytes, or a cleaner contract - say which), where it was
found (the note or the § that names it), and its status (proposed / refused /
superseded). Group by kind. Mark estimates as estimates. Do not edit kernel or
SDK code. Read `tools/abi.lock.json` and SPEC.md 20.2.0.1 for what an ABI
change costs procedurally, and say at the head what a flag day must also
carry (PKG_FMT / DRV_VER bumps, `abilock.py --update`, every package rebuilt,
the C SDK). Commit the file and an `os88index.py` regeneration on
`msp2-abilist`.
