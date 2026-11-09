# Kernel hot-paths pass 1: the brief every agent reads first

Branch `kernel-hotpaths`, cut from `claude/review-test-soak-7okfut` at
`a7ee7fab` on 2026-11-06. This is the first **speed** pass. The kernel has
had eighteen size passes (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P18.md`
is the latest record) and every one of them treated the hot paths as a
place to leave alone. This pass does the opposite.

Each agent works in its own git worktree at `/home/user/wt/khp1-<name>`, on
a local branch `khp1-<name>` cut from the commit that adds this file. Each
agent writes its record to `docs/plans/khp1/<name>.md`. The coordinator
merges the branches into `kernel-hotpaths`. **Agents do not push.**

## 0. The brief

The owner's words: *"This will be a speed pass - similar to past kernel size
passes, but this time focusing specifically on the hot paths. As such, this
is a performance sensitive pass. Most size passes either avoid hot paths, or
greatly limit changes made to them. This pass's goal is to speed up hot
paths that have a user noticeable impact. Graphics, mouse, window, etc.
Often speed means less bytes, but here even adding bytes is acceptable - so
long as the bytes pay for their gain. That isn't just percentage of the hot
path, but also the hot path's impact in a user visible way. Keep a log of
each change and bytes vs performance, and the hot paths 'hotness'. I will
review things that gain bytes at the end and may pick between them."*

In practice:

* **The goal is time the user can perceive on the target**, an IBM 5150/XT
  with an 8088 at 4.77 MHz, on all three 1983 adapters (CGA, Hercules/MDA,
  and EGA/VGA as the contrast). A 40% speedup of a routine that costs 0.2 ms
  per window raise is worth less than a 5% speedup of something a window
  raise spends 200 ms in. **Price every item in user-visible milliseconds
  for a named operation, as well as in cycles for the routine.**
* **Bytes are allowed, and they must pay.** A change that is faster AND
  smaller or the same size is TAKEN outright. A change that costs bytes is
  also taken, but **it is recorded as an owner's choice**. Each such change
  goes in a commit of its own whose subject starts `[+N]` (kern_big resident
  bytes). It must revert cleanly on its own, so that the owner can drop it.
  Do not stack two byte-costing items in one commit, and do not make a later
  free item depend on an earlier byte-costing one when that can be avoided.
  If it cannot be avoided, say so in both commits.
* **Budget headroom.** At the base, kern_big's `KERN_SIZE` is 96,768 against
  `KERN_BUDGET` 100,864, which leaves 4,096 bytes plus the branch's 2,048 of
  `KERN_GRACE`. kern_small's is 59,392 against 64,000, which leaves 4,608.
  That headroom is shared by NINE agents and is the owner's to spend, so it is
  no licence. A byte-costing item states its bytes on EVERY kernel. An item that pays on
  kern_big and not on kern_small may be built for kern_big alone (`%ifdef`),
  and the note says so. The `gfx_points` precedent in CLAUDE.md is that
  shape: *"yes on kern_big and no on kern_small"*.
* **ABI breaks are GATHERED, not taken.** A speedup that needs a frozen
  interface changed (a cell contract, a record layout, a register contract in
  `apps/os88api.inc`, a driver hook) goes in your note's *WANTS AN ABI
  BREAK*, priced in cycles, ms and bytes, with its callers. **Finding those is
  in scope and is a deliverable.** The coordinator folds them into
  `docs/plans/ABI-BREAK-LIST.md`'s new section V (speed).
* **Off-hot-path size finds are noted, not taken.** When you see something
  outside a hot path that would cut kernel RESIDENT bytes, do not stop to do
  it. Append it to `/home/user/wt/khp1-size-inbox/<name>.md` (file, routine,
  estimated bytes, one line on why) and carry on. A separate agent processes
  that inbox after the speed work merges. That file is outside git so no merge
  can touch it, and it is append-only.

### 0.1 The base, assembled (`a7ee7fab`, from `make`'s own `kernsize` lines)

| kernel | `.text` | `.bss` | `.cold` | `.vga` | `.lowbss` | `.vgabuf` | **resident** | `KERN_SIZE` / budget |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| **kern_big** | 40,631 | 5,377 | 37,796 | 2,032 | 5,358 | 336 | **91,530** | 96,768 / 100,864 |
| kern_small | 30,220 | 2,945 | 21,225 | 0 | 2,868 | 0 | **57,258** | 59,392 / 64,000 |

Measure kern_emu and kern_mega yourself at your base (`make emu`,
`make mega`). They must keep building and must not regress in speed on a
path they share.

## 1. Hotness: the scale every item is rated on

Every item in every note carries a **hotness** grade and a
**user-visible impact**. The owner reads both to decide byte-costing items,
so grade honestly. A routine on the H1 list that no user-visible operation
spends real time in is not hot for this pass.

| grade | runs | examples |
|---|---|---|
| **H1** | per pixel, per byte or per scanline, inside a primitive | the VGA planar fill loop, a 1bpp span's mask-and-store, the glyph row loop, LZ's literal copy, the per-sector copy |
| **H2** | per primitive call, per glyph cell, per interrupt or per event | the `gfx_*` call's fixed part (**756 us** on the 5150, PERFORMANCE.md Part 2), `font_run_cell` (~900 us a cell), a mouse byte's ISR, `sch_isr` at 18.2 Hz, `sch_switch`, the API cell (46.7 us), a clip-rect iteration |
| **H3** | per user action | a window raise or drag step, a menu open or close, a keypress echoed, a scroll, a cursor move across a window edge |
| **H4** | per launch or file operation | opening a Disk window, launching a package, `dsk_xfer` per call, directory scans, module (`.DRV`) loads |

**User-visible impact** is the measured or carefully predicted time on the
XT for a NAMED operation. Examples: *"raise Note Pad over Paint on CGA:
412 -> 371 ms"*, *"cursor across the desktop: ISR share 9.1% -> 6.4% of the
CPU"*, *"open A: on a 360KB disk: 2.31 -> 2.05 s"*. Say whether the figure is
MEASURED (on MartyPC guest cycles, which emulate a real 4.77 MHz 8088) or
PREDICTED (cycles times PERFORMANCE.md Part 2's calibration). "Faster" with
no number is not an item.

## 2. The ten owners

| agent | owns | the hot paths |
|---|---|---|
| **bench** (runs first, others proceed in parallel) | new `tools/os88hot.py`, a baseline report `docs/reports/HOTPATH-BASE-2026-11-06.md` | Builds the shared instrument the speed agents use; see section 3. Edits no kernel source. |
| **gfxcall** | the `OSAPI_SLOT` cell and the gfx and font slot shims in `kernel/kernel.asm`, the gfx lock (`gfx_lock`/`gfx_unlock`, cursor exclusion and its lazy hide, SPEC.md 7.1), `kernel/clip.inc`, `kernel/band.inc`, the coordinate and clip set-up every primitive runs before its loop, and the VESA hook heads (their **non-VESA** cost) | **The fixed part of every drawing call.** It is 756 us, against a primitive's per-pixel work that is often less. Every other drawing agent's numbers sit on top of it. Includes the `.cold` far-call thunks on drawing slots (`gfx_blit1` reaches `.cold` through `call COLD_SEG:`): moving a hot body back to `.text` costs `KERN_CODE_MAX` room and **no resident byte**, which makes it a free item under this pass's rules |
| **vga** | `kernel/vga12.inc` (and the `.vga` section) except what gfxcall owns: the planar fill, hline, vline, frame, rect, scroll and copy, `gfx_blit4` and `gfx_blit1` on VGA/EGA, VGA's cursor save and restore if it lives here | H1 loops on a 4-plane card. MartyPC `os8088_xt_vga`. Read SPEC.md 39 and 5 first. 86Box for a 286/386 VGA (`vm/`) |
| **mono** | `kernel/softgfx.inc`: the 1bpp CGA, Hercules and MDA renderer, the backbuffer path (`[bb_on]`), dithering to 1bpp, the mono blit runs | H1 loops on the two adapters a 4.77 MHz machine actually has. MartyPC `os8088_5150_cga_gla` and `os8088_5150_herc_gla` |
| **text** | `kernel/font.inc` entire: `font_run`, `font_run_x`, `font_run_cell`, `font_char`, `font_str`, proportional and `.f88` fonts, string-width measurement, glyph fetch | ~900 us a cell and 71 ms a 78-cell row is the single largest drawing cost a user sees: menus, title bars, every list, every dialog. All three adapters |
| **pointer** | `kernel/mouse.inc`, `kernel/mouproto.inc`, `kernel/vmmouse.inc`, the cursor (`cur_move`, `cur_move_1`, cursor draw, save and restore per adapter), the serial and PS/2 ISRs, `kbm_isr`, the keyboard ISR, and posting input events | H2 per mouse byte. The cost of moving the pointer is paid continuously while the user does anything. Input overrun is one of CLAUDE.md's three named defects. Serial on MartyPC. PS/2 on 86Box `386-ps2` (MartyPC has no 8042) or QEMU. The VMware pointer on QEMU only |
| **wm** | `kernel/wm.inc`, `kernel/instance.inc`, `kernel/menu.inc`, `kernel/dock.inc`, `kernel/toast.inc`, `kernel/blank.inc`, `kernel/ui.inc` (all but `ui_task`'s idle pass, which is sched's), the consumer side of `kernel/events.inc` | H3: raise, drag, move and resize, close, title strip, damage repaint (`wm_paint_all` and SPEC.md 11.90-11.96), menu open, track and close, the dock. **PERFORMANCE.md Part 5 is your bar**: a path in that table must not get worse and should get better. `tests/deskbench.py` is the user-scenario instrument |
| **files** | `kernel/files.inc`, `kernel/icons.inc`, `kernel/desk.inc`, `kernel/desksc.inc`, `kernel/assoc.inc`, `kernel/dskwin.inc`, `kernel/fdlg.inc` | H3 and H4: opening a Disk window, laying out and drawing icon and list views, scrolling one, selecting, rubber-banding, double-click to launch, the file dialog |
| **disk** | `kernel/disk.inc`, `kernel/diskw.inc`, `kernel/drvvol.inc`, `kernel/lz.inc`, `kernel/lzfilt.inc`, `kernel/loader.inc`, `kernel/mod.inc`, `kernel/compress.inc` | H4 with H1 inside: `dsk_xfer`, `dsk_runcap`, `dsk_next_clus_x`, directory search, the LZ and lz4 decoders (every package load), module loads. `int 13h` is ~400 ms a CALL, so **calls saved beat cycles saved**, and coalescing is the lever. MartyPC's FDC counters (`tools/os88boot.py`, docs/MARTYPC-DEBUG.md) count calls exactly. 86Box for AT and HDD |
| **sched** | `kernel/sched.inc`, `kernel/kernel.asm` except gfxcall's part (the non-drawing API shims, `api_x`, `api_far`, `kmain`), `ui_task`'s idle pass, `kernel/clock.inc`, `kernel/clockw.inc`, `kernel/snd.inc`, `kernel/serial.inc`, `kernel/memory.inc`, `kernel/xmem.inc`, the queue primitives of `kernel/events.inc` (`evq_*`) | H2: IRQ0 and `sch_isr`, `sch_switch`, `OSAPI_TASK_YIELD` (693 us), the far-call floor of every API call, `ui_task`'s idle pass, the once-a-second clock redraw, heap allocation and compaction cost on the paths users wait on |

A file not in any row is free to whoever reaches it first: say so in your
note. **Do not edit a routine another agent's row names.** When you find a
speedup in someone else's file, put it in your note's *CROSS-FILE* section
with its numbers, and the coordinator routes it. When two rows meet at a
seam, for example font.inc calling into vga12.inc's span writer, the CALLEE's
owner owns the change. The caller's owner documents what it needs.

## 3. The instruments: use the right emulator for the question

All four emulators the project supports are built in the coordinator's tree:

* **MartyPC**, cycle-accurate 8088 and the DEFAULT. Its guest cycle counter
  is exact under any host load. Use it for every timing claim about the XT.
  In your worktree, before your first emulator run:
  `mkdir -p build && ln -sfn /home/user/os8088/build/martypc build/martypc`.
  If `build/martypc/` is still building when you need it, wait for it. Do not
  build a second copy. Machines are listed in
  `tools/martypc/configs/os8088_machines.toml`. **There is no IBM ROM**, so
  use the `_gla` twins (`os88ui.boot()` substitutes them). VGA IS modelled
  (`os8088_xt_vga`).
* **86Box**, built with automation (docs/86BOX.md): 286 and 386 machines, AT
  BIOSes, the PS/2 mouse (`386-ps2`), the 1.2MB drive, `xt-multimon`, HDDs.
  It is not cycle-accurate. Use it for "does it still work there", for
  relative A/B on one machine, and for the hardware MartyPC lacks. In your
  worktree: `ln -sfn /home/user/os8088/build/86box build/86box` (the binary
  and the ROM set). Read its *AT machines start in BIOS setup once* first.
* **Bochs**, installed (docs/BOCHS.md): kern_mega only. Use it if your change
  touches a path kern_mega shares.
* **QEMU**: the closed list in CLAUDE.md only (PS/2, NIC, VMware pointer,
  RTC write, 286+). It is exact about WORK (instruction counts,
  `-icount`) and useless about time.

**Speed claims are made in guest CYCLES on MartyPC**, converted at
4.77 MHz (209.6 ns a cycle). The ways to get them, cheapest first:

1. **`tools/os88hot.py`**, which the `bench` agent is building now. It will
   be committed to `kernel-hotpaths`, and you take it with
   `git merge kernel-hotpaths` when its commit lands. The coordinator says so
   in this file's section 8. It is planned to give (a) cycles per call of a
   named kernel routine across a scripted scenario, entry-to-return by
   breakpoints, as a histogram; (b) a sampling profile of the kernel by
   ROUTINE over a scripted scenario, resolved by LINEAR address
   (`os88sym.linear`, never offsets against `.text`); (c) `gfxbench` and
   `sysbench` run headless on each adapter with their report files read
   back, plus a two-report diff; (d) a base-versus-tip A/B of any of those,
   in two private build trees.
2. **What exists already**: `os88marty.bp_count`, `bp_trace`, `breakpoints`,
   `advance(cycles=)` and the cycle counter in `status`
   (docs/MARTYPC-DEBUG.md); `m.flicker()` and `m.pace()` (PERFORMANCE.md
   Parts 3.1 and 3.2) for frames on the glass; `tests/deskbench.py` (busy
   desktop user actions, per adapter, in guest ms); `tests/gfxbench/` and
   `tests/sysbench/` (`make bench` builds their disk; docs/TESTING.md
   *gfxbench and sysbench* says how to drive them); `tools/os88boot.py` (the
   FDC counters, for disk calls).
3. **Instruction counts** (QEMU `-icount`) only to confirm a work count.
   Never as a time.

Until `os88hot.py` lands, spend your time reading and profiling with what
exists. **Read before you measure, and measure before you change**: the
first hour of a good note is a ranked list of where the time actually goes
on a named operation, with numbers.

**Concurrency.** Ten agents share 4 cores and 15 GB. Use `make -j2` and
never two builds at once in your own tree. Run at most ONE emulator instance
of your own at a time. Cycle counts are unaffected by host load, so **never
wait for the load to drop** before measuring. Wall-clock-budgeted suite rows
are the coordinator's final pass, not yours.

## 4. The rules this pass

* **Pixels and behaviour are byte-identical.** The final picture after any
  operation must be identical to the base's on every adapter, and so must
  every refusal path and every API result. A change that alters what is drawn
  (draw order that removes a transient flash, say) is welcome, but it must be
  measured with `m.flicker` (fewer transient pixels, same settled frame) and
  marked in the note as **CHANGES TRANSIENTS**. A change that alters the
  settled picture or a result goes in *WANTS THE OWNER* and is not taken.
* **Every adapter.** A speedup on VGA that costs on CGA or Hercules is not a
  speedup. Measure on each adapter a path runs on, and look at 1bpp
  (CLAUDE.md, *Look at a drawing or greying change on a 1bpp adapter*).
* **Every CPU.** The 8088 is the target, but an 8086, V20/V30, 286 and 386
  run the same binary. On a 16-bit bus, word alignment of hot loop targets
  and word operands matters (`tests/unit/t_hotpar.py`'s header,
  PERFORMANCE.md Part 7 item 7). The 8088 has a 4-byte prefetch queue and a
  byte-wide bus, so **encoding length is often the cost**: count
  `max(clocks, 4 x instruction bytes)`-ish fetch floors (PERFORMANCE.md
  Part 2), not 8086 book clocks. A change that wins on the 8088 and loses on
  a 286 says so, and both numbers are in the note. Use 86Box for the 286 and
  386 side.
* **Parity.** `hotpar` (fast tier) fails `make` when a pinned hot routine
  moves parity. In this pass rewriting a hot routine is expected. When you
  move one ON PURPOSE, edit the table in `tests/unit/t_hotpar.py` in the
  same commit and say in the commit message what you measured. When you move
  one by accident (a cold edit upstream of it), pad it back. Prefer
  **`align 2` on a hot loop's head** where that pays on a 16-bit bus, and
  price the pad bytes. Add newly hot routines to the table when you speed
  them.
* **Placement is a lever.** `.cold` is resident but far-called: a hot body in
  `.cold` pays a far `call`/`retf` (~2 x 11 us on top of a near call) plus a
  thunk. Moving a hot body into `.text` costs `KERN_CODE_MAX` room (64 KB
  near window, 32,371 bytes left on kern_small and plenty on kern_big; check
  `kernsize`'s `segment` line), not resident bytes. Moving it into `.vga` is
  only for VGA/EGA-only code (CLAUDE.md, *Code only a VGA or an EGA runs goes
  in `.vga`*). The reverse holds too: H3/H4 code sitting in `.text` costs
  nothing to move to `.cold`, so do it only where it frees near-window room a
  hot move needs.
* **Hard rules hold** (CLAUDE.md's *Hard rules*): `cpu 8086`, register
  discipline, `pushf`/`cli`/`popf`, section discipline, label prefixes,
  `font_run` not `font_str`, `[vid_w]` not `SCREEN_W`, ES = KERNEL_SEG in
  callbacks, SS != DS.
* **kern_small, kern_emu and kern_mega must still build** (`make small`,
  `make emu`, `make mega`). Code under `%ifdef KERN_MEGA` sits under
  `tests/megaseg.txt`'s rule. A `.text` byte added there needs its line.
* **Equivalence.** Where no suite row reaches a path, prove the rewrite
  another way: a unicorn harness against the base kernel's own bytes (the
  size passes did this often, and `unicorn` is installed), or a MartyPC A/B
  that compares screen and memory. "It assembled" is not equivalence.
* **ABI.** `main` is `origin/main` at `86fc03da`. `git diff origin/main HEAD
  -- apps/os88api.inc drivers/os88drv.inc apps/cc/os88.h` is the list of
  what is NOT frozen. Everything else in those files, every cell offset,
  every module and driver header, every file format and every `SYSTEM.CFG`
  key is FROZEN. A driver's resident hook (VIDEXT, the serial lease) is
  kernel-to-driver, and `DRV_VER`'s rule applies.

## 5. Method

* **First command**: `cd /home/user/wt/khp1-<name> && git status`. You are on
  `khp1-<name>`. If you are not, stop and say so. **Never `cd` into
  `/home/user/os8088` to build or edit**: that is the coordinator's tree.
  `make deps` has run, and `mtools`, `unicorn` and Bochs are installed.
* **Scratch** goes in your OWN directory,
  `/tmp/claude-0/-home-user-os8088/feee9752-2bd7-5bef-b4ec-fc1c1815b0ff/scratchpad/<name>/`.
  The scratchpad is shared by every agent. **Never `git stash`**: it is one
  stack for every worktree of the repository. For a base comparison use
  `git show a7ee7fab:path`, `os88rdiff`, or `make BUILD=<private dir>` of a
  `git worktree add` of the base in your scratch.
* **The first `make -j2` in a fresh worktree builds everything.** Start it
  first, while you read.
* **Find where the time goes before choosing what to change.** Profile a
  named user operation on the slowest adapter it runs on. Rank routines by
  their share of it. Work down the ranking. Write the ranking in your note.
  It is useful to the owner even for items you do not take.
* **Per item**: base cycles and tip cycles of the routine on a stated input,
  measured; the user-visible operation and its ms before and after; bytes on
  kern_big and kern_small resident (`tools/kernsize.py --json`, and the
  routine span from `kernel.map`: **the span, not the rung**); the 286 effect
  where it differs; the rows that cover it.
* `tools/os88rdiff.py kernel a7ee7fab .` shows every routine you changed and
  what shifted. Run it before each commit.
* **Commit in small batches.** Gate each one with `make -j2` (which runs the
  fast tier) and `make -j2 small`, plus `make emu` and `make mega` at your
  tip. Run `tools/stkbalance.py` over every file you touched, base against
  tip, and add no unbalanced path. Run `python3 tools/checkdocs.py` AFTER
  `git add`. Commit messages end in `(kernel hot-paths pass 1)` and carry
  the attribution trailer your session gives you. Never put a model name in a
  commit or a file.
* **SPEC.md first** if a change alters a documented contract or a documented
  cost. A § that quotes a cycle or ms figure you just changed gets updated.
  PERFORMANCE.md Part 5's row gets updated when you move its number, and a
  new set in Part 9 is the coordinator's at the close. Put your measurements
  in your note.
* **Rows.** Name the `tests/suite.py` rows that cover what you changed (grep
  it). You may run THE ROWS FOR YOUR OWN SUBJECTS, scoped with `-k`, ONCE,
  after your last code commit (`python3 tools/os88test.py soak -k '<row>' -k
  ... --marty-jobs 1`). That is one pass, not one per batch, and a row is a
  row however it is started (CLAUDE.md's Testing section). **NEVER run the
  whole soak tier and never `test-full`.** The coordinator runs the union at
  the close. If a row fails, re-run it alone on the BASE before blaming
  yourself (`python3 tools/os88bisect.py classify <row>`). Measurement runs
  of your own instrument (os88hot, bp_count, a profile) are not rows and may
  be repeated, but keep them purposeful: a measurement decides something.

## 6. The note: `docs/plans/khp1/<name>.md`

Sections, in this order:

1. **BASE AND TIP**: resident and `KERN_SIZE` on all four kernels, base and
   tip, assembled.
2. **WHERE THE TIME GOES**: the ranked profile of the named user operations
   you studied, per adapter, measured.
3. **TAKEN**: one row per item, in the shape of the LOG table (section 7).
   Mark each item **FREE** (no resident byte added on any kernel) or
   **[+N] OWNER'S PICK** (its own commit, its hash, revertable alone).
4. **TRIED AND REFUSED**: what did not pay, with the number, so nobody
   re-tries it.
5. **WANTS AN ABI BREAK**: the speedup, the frozen interface, the cycles and
   ms it would buy, the bytes, the callers.
6. **WANTS THE OWNER**: anything that changes the settled picture or
   behaviour, priced.
7. **CROSS-FILE**: speedups in files you do not own, priced.
8. **ROWS RUN** with results, and the rows that cover your items.
9. **DEFECTS FOUND**: fixed or not, and how you found them.

Then run `python3 tools/os88index.py` (docs/INDEX.md is generated) and
commit the note and the index together.

**Your final message to the coordinator is ONE message**: the branch tip;
each item with its bytes and ms; the byte-costing items listed separately
with their commit hashes; the ABI-break items; the rows you ran and their
results; and anything that blocked you.

## 7. The LOG

`docs/plans/khp1/LOG.md` is the coordinator's consolidation of every
agent's TAKEN rows. Its columns are what each note's TAKEN table carries:

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |

*M/P* is MEASURED or PREDICTED. The ids are `<agent>-<n>`.

## 8. Coordinator notices (updated during the pass)

* **`tools/os88hot.py` landed** on `kernel-hotpaths` at `98b08327`
  (`git merge kernel-hotpaths`). `python3 tools/os88hot.py --help`, and
  read its header. Subcommands: `scenarios`, `time <scen> [-n 3]`,
  `profile <scen> [--top 25] [--local]`, `cycles <sym> <scen>`,
  `gfxbench|sysbench --json f`, `diff A.json B.json`, and
  `ab --base <commit> <subcommand ...>`. `--machine cga|herc|vga|<name>`,
  `--all` and `--json` work everywhere. Every window starts aligned to IRQ0,
  which makes reps repeatable to about 1%. Compare medians over the same
  `-n`. Its limits: disk TIME is MartyPC's fast model, so price disk in
  int 13h calls (the `fdc` column) at about 400 ms each; profiles bucket
  modules per file; gfxbench and sysbench are validated on CGA only so
  far; `ab` leaves a worktree under `build/os88hot-ab/`; 86Box support is
  coming in bench's phase 2. Use `ab --base 6d6c23ae` (or `a7ee7fab`, the
  same kernel) for base-versus-tip.
* First readings (bench, CGA unless named): raise 181 ms (Hercules 219, VGA
  370), drag 504 ms, menu open 45 / close 22 ms, opendisk 1,741 ms with 4
  int 13h calls, launch Calc 971 ms, PgDn 131 ms, typing 31 keys 620 ms busy,
  VGA boot 16.0 s with 18 int 13h calls. An idle desktop is 94% halted and
  its busy time is `sch_switch` 45%, `sch_account` 15%. A CGA raise is
  `sw_xfer` 36%, `sw_col` 18%, `sw_plane_op` 7%.
* **Merge `kernel-hotpaths` (`763f4437`) now.** It carries three shared
  fixes. (1) `mod_cfix` tests 9A/EA exactly (disk, `6209b524`, +2 `.cold`
  on kern_big). Its old `(op | 70h) == FAh` test accepted 8A and BA too,
  which failed `make` at `build/coldhma.sys` whenever resident growth moved
  the HMA `COLD_HOME` onto such a byte (vga hit it at 08B3h). (2) `os88sym`
  resolves the `.vga` rung's labels (vga). (3) `deskbench` raises a buried
  placement through the dock (wm). **What is left of the hazard**: 14 homes
  remain that no opcode test can tell from a real site. The nearest one
  above today's 08AEh is **090Bh**, about 1.5 KB of resident growth BEFORE
  `.cold` away. That is a pass-wide limit, and the coordinator watches the
  merged total. If your branch alone adds more than ~300 bytes before
  `.cold`, say so in your final message.
* **files and sched have landed** (`kernel-hotpaths` after `e8c636ca`).
  Three things they found that bind the rest of the pass. (1) The boot
  overlay's window half (`.ovl`) is at **5,117 of 5,120 bytes on kern_big**:
  anything added to `.ovl` needs a move out first. (2) `tools/os88hot.py`
  finds the idle task's `hlt` at `sch_idle_body.rearm - 1`. sched-4 keeps it
  there, and an edit that moves it makes busy read 100% rather than raise an
  error. (3) sysbench's `PIT/tick want 65536` row can read 131050 from
  unsigned jitter in `sb_ticklen`, and its `int 16h` row moves ~10% with tick
  phase. Both are noise in an A/B.

