# Kernel size pass 19: the flag day - the brief every agent reads first

Branch `kernel-size-19`, cut from `elendilon` at `4beca096` on 2026-11-10.
The pass before this one was **pass 18** (branch `kernel-size-18`); its record
is `docs/plans/completed/HANDOFF-KERNEL-SIZE-P19.md` and its brief and notes
are `docs/plans/completed/ksp18/`.

Each agent works in a git worktree of its own at `/home/user/wt/<name>`, on a
branch `ksp19-<name>` cut from the commit that adds this file, and writes its
record to `docs/plans/ksp19/<name>.md`. **Agents do not push**; the
coordinator merges.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/ABI-BREAK-LIST.md` in full** - section 13 (the big review's
   reconciliation and section 13.3, the recommended flag day) is the plan
   this pass executes. The long form of every row is
   `docs/reports/big-review-2026-11-08/synth/flagday.md`; the Phase 1 items
   are `synth/phase1.md`; bug ids (BR-nnn) are `synth/bugs.md`;
3. `docs/plans/completed/ksp14/BRIEF.md` sections 1-4 (rules, method,
   environment, the note's shape) - they carry over except where this file
   says otherwise; substitute `ksp19` and `(kernel size pass 19)`;
4. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P19.md` (pass 18's record:
   what is left, what is refused, the method lessons);
5. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**. SPEC.md 20
   (the package boundary, the table, 20.2.0 the numbers, 20.3 the cells).

## 0. The brief

The owner's words: *"This will be 'the big one', finally cashing in all the
abi breaking size options we have been collecting in an abi breaks document.
... Other abi breaking size reductions spotted during this pass ARE in scope.
Other non-abi breaking size reductions spotted are also in scope."*

**This IS the ABI break pass.** The brief commit has already bumped
**`PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14** (ABI-BREAK-LIST section 0 item
2) and recorded it in `tools/abi.lock.json`. Nothing built before runs. So:

* When your change moves `apps/os88api.inc` or `drivers/os88drv.inc`, run
  `python3 tools/abilock.py --update "<what and why> (kernel size pass 19)"`
  **without** a further bump. The lock file WILL conflict between branches;
  the coordinator re-runs `--update` at each merge. Do not fight it.
* Every package, driver and C package in the tree is rebuilt by `make`
  (SPEC.md 20.8 rule 4). A source you change to follow a new contract is part
  of your row. A C package is part of it too: the C SDK (`apps/cc/os88.h`,
  `os88thunk.asm`, `crt0.asm`) moves in the same commit as the SDK, and you
  build the C packages (`make cword runcpm c64 apple2 paccman lemmings weave
  loom` - the toolchain is `tools/setup-cc.sh`, docs/C-TOOLCHAIN.md) before
  you call it done.
* **A frozen interface is no longer frozen** - but a break is taken only if
  it is a row in ABI-BREAK-LIST, or you price it in your note (bytes per
  kernel, packages, cycles, who it breaks) and it is a clear size win. A
  break that only buys cleanliness at a cost is RECORDED, not taken.
  Non-ABI size reductions you find in files you own are in scope as in any
  size pass (ksp14 section 2's method).

### 0.0 NO ROW GROWS BYTES (the owner, 2026-11-10)

*"We're doing a size pass, so why are we growing bytes at all? We'll handle
the additions/speedups/etc in another pass where we decide which ones are
worth it."* A row is taken only if it is net SMALLER - resident kernel bytes
on every kernel it reaches AND the packages' total. A correctness fix, a
speed-up or a cleaner contract that costs bytes is RECORDED for that later
pass, not taken. Where a size row has a cost on one side (a kernel saving
paid for in package bytes), take the form with the smallest total, and say
the total.

**Two rulings under 0.0 (the owner, 2026-11-10):** small package growth
that buys RESIDENT kernel bytes is fine (R14's 8-16-byte growths in seven
small packages stand); and **C1 is kept** in nogrow's KB-arm form (~-40
resident per kernel against ~+330 package bytes, ~+900 once T5 lands) rather
than the six KB doors restored as door cells. R14's +214 clocks a callback
stands unless the final pass's `os88hot ab` shows it on the glass.

### 0.1 The owner's answers to flagday.md 3.5 (2026-11-10)

1. **T5, the software-interrupt door**, is the table row (not T1).
2. **The IF rule: audit, then decide.** `door` tags every rare cell's
   callers; if none calls with IF=0, the door is the lean one and "rare
   cells are called with IF=1" goes into SPEC.md 20.3. Otherwise it restores
   IF from the stacked FLAGS.
3. **R14: C packages DECLARE their class** - `os88.h` grows a class-struct
   form and the C packages are edited to declare one.
4. **L1: design kern_small's fill first**; take L1 only on a design where
   kern_small comes out neutral or better.
5. D8's `OSAPI_IRQ` cell and L3 are not in this pass (L3 is blocked on The
   Wire; D8 is a feature, not bytes). L7, mc-10, D8 phase 1, R16 and R9's
   universal value are out too (ABI-BREAK-LIST 13.3, "Out of it").

### 0.2 The base, assembled (`4beca096`, `tools/kernsize.py --json`)

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(+ `.mega` on kern_mega).

| kernel | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | **resident** | `KERN_SIZE` | `.ovl` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **kern_big** | 39,976 | 5,378 | 37,821 | 5,358 | 336 | 2,030 | **90,899** | 96,256 | 3,160 |
| kern_small | 29,680 | 2,948 | 21,037 | 2,868 | 0 | 0 | **56,533** | 58,368 | 1,474 |
| kern_emu | 40,222 | 5,378 | 37,821 | 5,358 | 336 | 2,030 | **91,145** | 96,768 | 3,160 |
| kern_mega | 45,728 | 5,416 | 48,583 | 5,600 | 336 | - | **130,367** (`.mega` 24,704) | conventional 62,464 of 68,096 | 3,156 |

Measure with `python3 tools/kernsize.py --build build --json -DKERN_BIG`,
`--build build/smallk ... -DKERN_SMALL`, `--build build/emuk ... -DKERN_EMU`,
`--build build/megak ... -DKERN_BIG -DKERN_MEGA` after `make`, `make small`,
`make emu`, `make mega`. Quote spans, never rungs.

## 1. Waves and agents

The table renumbers ONCE, in wave B. Wave A's rows withdraw and re-contract
cells and build the machinery; a withdrawn cell is DELETED from the table
(SPEC.md 20.3.1), so offsets move in wave A too - that is fine, every package
rebuilds. **Do not hand-number anything**: name cells by `OSAPI_*` symbol.

### Wave A (parallel, from the brief commit)

| agent | rows | owns |
|---|---|---|
| **gates** | Phase 1's host gates: abi-3/gkm-2 (`t_api_abi` over all four kernels, ready to decode T5's index table and `CD nn idx` sites), engsys-11 (the C SDK into `PKG_FMT`'s fingerprint and `t_mirror`), drivers-16 (VX/XM/HD private versions fingerprinted), **T6's mechanism**: evaluate (b) first - `%imacro call` overloaded once in `os88api.inc` on a per-cell kind attribute - and prove it with a `cmp` of EVERY built image (packages, drivers, C packages, all four kernels' disks) before and after; then the surviving-far-call gate (`os88pkg.py`/`os88drv.py` refuse a far call to a rare cell's offset once T5 flips) built but armed by a single switch T5 throws. If (b) cannot work under `-w+error`, do (a) and teach every source gate the spelling, breaking each on purpose | `tests/unit/t_api_abi.py`, `t_mirror.py`, `tools/abilock.py`, `tools/os88pkg.py`/`os88drv.py`'s scans, the kind-attribute generator, the call-macro region of `os88api.inc`. Coordinate the kind attribute with `door` through your notes: `door` decides what the attribute must carry |
| **door** | **T5's door, proven on kernel-private traffic first (P4)**: the IF audit (owner answer 2), the vector choice in 81h-FFh and its SPEC.md text, the door body on every kernel (IVT at boot; kern_mega's IDT trap gate; the real-mode FSX bracket's refusal `retf 2`), `api_rs.tail`/rare-X `retf 2` tails, the out-of-range refusal (CF=1, AX=1), DOS's AH=25h/35h virtualising the vector in `apps/dos/` AND `kerndos/`, abi-4's `API_RN_DSOFF`. Then P4: the `cw_` shims on the door. **V7**: a knob build with a counter in the rare path, one MartyPC reading of rare-cell frequency over a few ordinary sessions (it DECIDES the hot/rare line, so you take it; note it once). The door must be ready for `t5` to flip packages onto without re-design | `kernel/kernel.asm`'s `api_r*` bodies and the door, `kernel/mega.inc`'s gate, `kernel/fsx.inc`'s bracket refusal, the DOS vector paths. Do NOT renumber the table and do NOT touch package call sites |
| **drvtab** | N1, D4 (the floor and `OS88_DRIVER`'s `%error`), histsys-7 (class 1's `drv_svc` copy kept, the others read in place; `drv_svcix` gone, so D1/drivers-9 are moot), R3 (volume bridge in ES:DI), F1 (`'VC'` folded), F3 (the compression mark off the NTRes case bits: since every disk is rebuilt and every package re-made at this flag day, go straight to the new mark - no dual reader - unless you find a reader that must still see old disks), R2-1 and R2-3 (kern_mega) | `kernel/driver.inc`, `drvvol.inc`, `drivers/os88drv.inc`, `drivers/sound/` (the bridge), the config code, `tools/os88disk.py`'s mark and its readers, `mega.inc`'s `mega_hibnc`/`MEGA_WTN` |
| **memdoors** | BR-105 (C1's correctness half; kern_dos's twin), the kernel's own ~20 KB-door callers to the paragraph entries, then **C1** (the six KB memory doors withdrawn; every package, driver and C SDK site converted - a shared helper in `apps/` if it is smaller than inlining, the DISK/CPU class decided per site), **C2** (APPEND), **C4'** (APPEND_SYS alone), **R1** (REHOME's bytes form), R13 only if you touch the Task Manager anyway | `kernel/memory.inc`'s KB doors, `diskw.inc`'s append doors, `loader.inc`'s REHOME, the package sites of those cells |
| **class** | **R14 + K17-2**: the window class in the package (`W_CLASS` near pointer to {length, style, handler offsets}), the creation-time flags as the style word, one runtime `OSAPI_WM_FLAG`, runtime handler changes through a table of their own, kern_small's dispatch-count answer from `OSAPI_WM_CREATE`, BR-28's liveness re-test in the one helper; every windowed package's `WT_*` template and setter sites; `os88stik.inc`, `deskclok`, drivers' windows; **C packages declare a class** (owner answer 3). The six kern_mega `MEGA_ICWB` stubs go with the setters | `kernel/wm.inc`, `events.inc`, `instance.inc`'s window parts, `apps/os88ui.inc`'s window templates, every package's window setup, `apps/cc/` |
| **recon** | R2 (snapshots bounded by the caller), R12 (one display-index register), R15 (clipboard format byte), C10 (desk item by record), R2-4 and R2-5 (FSX_SPK's open verbs; FSXF_REAL's answer), V1 (clobbering rect entries beside the raw bodies; C SDK half first), V2 (the yield cell) - and then, with what is left of your budget, **find more**: ABI shapes in the table the list missed (single-use register conventions, cells whose bodies are a wrapper for another cell's, records with fields nobody reads) - price, take the clear wins, record the rest | the cells and package sites of those rows; `fsx.inc`, `snd.inc`'s FSX_SPK, `apps/os88spk.inc`, the clipboard and snapshot code |
| **l1** | **L1's design** (owner answer 4): what fixed-size kern_small content fills the region below a shared `KERNEL_SEG` so kern_small is neutral or better, and whether kern_mega packs its ladder; then, if the design holds, **build it** on your branch (`KERNEL_SEG` 0x0060 -> 0x01A0 on every kernel, the FAT window and `dsk_secbuf` below the image, every host tool and test that names 0x0060, both boot paths, the hard-disk loader, the ROMs, `COLDHMA.SYS`, hibernate). The coordinator merges L1 LAST, after T5. If no design makes kern_small neutral, stop at the design and say so | `kernel/kernel.asm`'s ladder, `boot/`, `tools/os88sym.py` and the address-naming tools; read-only elsewhere until merge |

### Wave B (from the merged wave-A tip)

| agent | rows |
|---|---|
| **t5** | T5 itself: sort the rare set by kind and by V7's frequency (promote what V7 says is hot), renumber the table once, flip the call spelling's rare arm to the door, arm `gates`' surviving-far-call gate, V2 in the same renumber, T6's renames with one cycle of `%define` aliases, `t_api_abi` on four kernels, `tools/os88wire.py`'s scans |
| **l1** (resumed) | L1 rebased onto the T5 tip, if its design held |

### Wave C (from the merged wave-B tip)

General sweeps over the files the flag day touched, non-ABI, as in passes
16-18 (one sweep per landed row).

## 2. Rules for this pass

* **Ownership is by FILE and ROUTINE as above.** A file in no row is free to
  whoever reaches it first: say so in your note's TAKEN line. A conflict you
  cannot avoid (two rows in `os88api.inc`, `kernel.asm`'s table) is fine
  textually - keep your edits in your rows' lines and the coordinator
  resolves. Package call sites of YOUR cells are yours wherever they are.
* **Speed**: no rare-cell or hot-path change may make a hot path slower
  without the cycles in your note (pass 18's section 1 rule, ksp18/BRIEF.md,
  holds). T5 and R14 have priced costs the owner accepted; stay inside them.
* **Builds**: `make -j2`, never two builds in one tree at once. Each tree
  builds all four kernels (`make`, `make small`, `make emu`, `make mega`) and
  the C packages before the agent calls a row done; `make budget-check` is
  the coordinator's, at the end.
* **TESTING - the owner's words: "save longer soaks for the end (not every
  agent needs to resoak half the kernel that this will touch)".** Each agent
  runs the host checks (`make`, which runs the fast tier, on every kernel)
  and **NAMES** the `tests/suite.py` rows its change can reach in its note.
  It runs at most a few short rows that are ABOUT the mechanism it built
  (`door`: a boot, the DOS rows that exercise AH=25h/35h; `l1`: a boot on
  each kernel) - never a soak scope, never `test-full`. The coordinator runs
  the deduplicated union once, after the last wave. A boot to LOOK is not a
  row; take one when the change is visible.
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree. It is built in the main tree.
* Commit messages end in `(kernel size pass 19)`. Run
  `python3 tools/os88index.py` AFTER `git add` of a new file, then
  `--check`, then commit (pass 18's lesson, a fifth time).
* SPEC.md is updated BEFORE or WITH the change, never after: every
  re-contracted cell's SPEC.md 20.3 entry and its own section, and the C
  SDK's `os88.h` prose.
* `apps/wireball/art/wbfront.inc` regenerates with different bytes on every
  build: `git checkout` it before you commit.

## 3. The note (`docs/plans/ksp19/<name>.md`)

As ksp14 section 4: TAKEN (each row, bytes per kernel MEASURED off spans and
`kernsize`, packages' bytes, cycles where a path changed), REFUSED (with the
price), WANTS THE OWNER, WANTS AN ABI BREAK (new rows found and NOT taken,
priced), ROWS (the `tests/suite.py` rows your change can reach - the final
pass runs their union), and anything the next wave must know.
