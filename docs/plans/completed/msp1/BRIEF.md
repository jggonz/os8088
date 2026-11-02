# kern_mega size pass 1: the brief every agent reads first

Branch `mega-size-1`, cut from `elendilon-next` at `e4896481`. This is the
**first size pass kern_mega has had** (docs/plans/KERN-MEGA-PLAN.md). Each
agent works in a git worktree of its own at `/home/user/wt/<name>`, on a
branch `msp1-<name>` cut from the commit that adds this file, and writes its
record to `docs/plans/completed/msp1/<name>.md`. The coordinator merges the
branches; **agents do not push**.

## 0. The brief

The owner's words: *"We have been developing the mega kernel, and it has come
to an initial stable state. This will be its first size pass; mega is allowed
to grow MUCH bigger than kern_big, but that isn't an excuse to waste space.
Size pass the mega kernel, with a general focus on mega only additions - but
if you spot opportunities that would apply to big as well, they are not out of
scope. ... have them record opportunities they spot but are not able to get to
this round."*

So: **no byte goal; the target is waste in kern_mega-only code** - code under
`%ifdef KERN_MEGA`, the `.mega` section, and `kernel/mega*.inc`. kern_big,
kern_small and kern_emu wins seen on the way are in scope. **Everything you
see and do not get to goes in your note's DEFERRED list with its estimated
bytes** - that list is half of this pass's product, because pass 2 starts
from it.

Read before writing any code, in this order:

1. this file;
2. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 (the method and
   the mistakes that cost earlier passes time), and
   `docs/plans/completed/ksp14/BRIEF.md` sections 0.2-2 (the shapes that
   paid, the rules and the method - this brief adapts it and wins where they
   differ);
3. `docs/plans/KERN-MEGA-PLAN.md` - at least its head (*Open before kern_mega
   goes live*) and the parts of section 14 that name the code you own;
   `docs/plans/completed/KERN-MEGA-SEGMENTS-PLAN.md` for what `.mega` is and
   who may name it, and `docs/plans/completed/KERN-MEGA-MEMORY-PLAN.md` if
   you own memory code;
4. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**. kern_mega is
   a 286+ kernel but it is assembled `cpu 8086` like the rest except where a
   file already says otherwise - check what the file you are in does
   (`cpu 286`/`cpu 386` blocks exist in `mega.inc`) before using a 286
   instruction, and never use one in code kern_big also assembles.

### 0.1 What kern_mega is made of (base, assembled)

`python3 tools/kernsize.py --build build/megak` after `make mega`, and
`python3 tools/os88megaseg.py` for the per-file split against kern_big:

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.mega` (+scratch) | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 41,136 | 5,398 | 37,780 | 5,358 | 336 | - | 6,447 | 5,110 | 97,280 |
| kern_mega | 47,548 | 5,435 | 44,957 | 7,679 | 336 | 18,237 (+1,493) | 6,620 | 5,109 | 131,584 |

kern_mega carries `.vga` inside `.text` (no `.vga` rung), so 1,541 of its
`.text` delta in `vga12.inc` is that and not new code. **Where a byte lives
matters on kern_mega in a way it does not on kern_big**: `.cold` and `.mega`
are copied above 1MB at the switch and their conventional rungs (65,024
bytes) go to the heap, so what stays below 1MB is `.text` + `.bss` +
`.lowbss` + `.vgabuf` (~66.5KB). Report all of it, but **a `.text`/`.bss`/
`.lowbss` byte is worth more than a `.cold`/`.mega` byte here** - it is
conventional memory for ever, and `.text`+`.bss` is the 64KB window kern_mega
shares with kern_big's growth (os88megaseg: 12,553 left). A move from `.text`
into `.mega` or `.cold` that keeps the hot path fast is a real saving on
kern_mega. **`KERN_SIZE` at boot matters too** (it is what a machine must
hold to boot) - say what yours did.

### 0.2 The four agents and what each owns

| agent | owns |
|---|---|
| **pmcore** | `kernel/mega.inc` lines **1-4968** at the base: the switch in and out (`mega_enter`, the 286 reset path, the 386 CR0 path), the GDT/LDT/IDT build, the IRQ/ROM halves (`mega_t08`, `mega_k09`, `mega_i16`), reflection (`mega_svc`), `mega_tell` and the field notes, the fault reporter and every #GP fixup (`mega_gpfix`, `mega_gpw`, `mega_null0`, `mega_vfix`, the CS-write and stray-segment fixups) |
| **pmfd** | `kernel/mega.inc` lines **4969-8107**: the API cells' kern_mega stubs (`OSAPI_SLOT`/`OSAPI_FJCELL`/`MEGA_ICW` arms), the hex/strings helpers, the huge-claim code (`mega_hugebf`, `mega_hgfree`), and **the native floppy** (`mega_fd*`, the 765/8237 path and its fallbacks and diagnostics) |
| **pmmem** | `kernel/mega.inc` lines **8108-13715**: memory above 1MB - `.cold` up (`mega_coldup`), the XMEM pool (`xm_dsp`'s arm, `mega_lincopy`), the LDT allocator, selectors, claims and regrow (`mega_claim*`, `mega_rgn*`, `mega_seg*`), A20, module/region ownership, and the `.mega` bodies low code calls at the file's end |
| **shared** | everything **outside `mega.inc`** that kern_mega adds: `kernel/megados.inc` (3,391 `.mega`), `megapark.inc`, `megares.inc`, and every `%ifdef KERN_MEGA` hunk in the kern_big files - `memory.inc` (+1,137 `.cold`), `disk.inc` (+575), `driver.inc` (+560), `diskw.inc` (+510), `kernel.asm`, `hiber.inc`, `compress.inc`, `lz.inc`, `loader.inc`, `mod.inc`, `desksc.inc`, `mouse.inc`, `snd.inc`, `icons.inc`, `sched.inc`, `font.inc`, `menu.inc`, `wm.inc`, `xmem.inc` and the rest `grep -l KERN_MEGA kernel/` lists - plus the macro-expansion growth os88megaseg shows as `?macro` (+899 `.text`, +680 `.cold`: find which macros grow on kern_mega and why) and **`.lowbss` +2,321 over kern_big** (find what it is and whether all of it is needed - stacks are the last thing to trim on an estimate, `tools/stkwater.py`) |

Line ranges are the base's; a hunk that straddles a boundary belongs to the
lower range. **Other opportunities seen in your code are yours** (a
neighbour, a routine your code calls); anything in another agent's range goes
in *CROSS-FILE* with its bytes, not into your diff. kern_big-only code
(no `KERN_MEGA` anywhere near it) is in scope only as found on the way, and
goes in *DEFERRED* if you do not take it.

### 0.3 Where the bytes are likely to be

kern_mega grew fast, feature by feature, mostly debugged off photographs of a
Victory 286 - so expect:

* **Diagnostics that outlived the bug.** `MEGADIAG=1`, `MEGATRAP=1`,
  `PICLOSE=` and `MEGAFDMASK=1` already gate the field instruments; look for
  diagnostic code that is NOT under one of them and should be (the E9 lines
  the shipped kernel writes, hex dumpers, counters nothing reads). **Moving
  user-visible text behind a knob is a FUNCTIONALITY change** - E9 output is
  not user-visible, a toast is. Say which.
* **Two routines that are one** - kern_mega often has a real-mode half and a
  protected-mode half of the same job, and a `.text` copy and a `.mega` copy;
  the `...f` far twins of near routines (`mega_claimfreef`, `mega_xdescf`)
  may be shims a direct call could replace.
* **Code that is in `.text` but only ever runs in protected mode** (could be
  `.mega`) or only at boot (could be `.ovl`/the blob - `.ovl` is given back).
  `tools/os88ovlchk.py` says what may name what; `.mega` is reached from low
  code only through the routes KERN-MEGA-SEGMENTS-PLAN describes.
* **kern_big arms assembled on kern_mega** whose guard can never pass there
  (and the reverse), and kern_big code that kern_mega replaces but still
  carries.
* The shapes that paid on kern_big (ksp14 BRIEF 0.2): single-caller routines
  written at the call, shared epilogues by near `jmp`, tables vs compares,
  strings sharing tails, relaxed jumps, a register kept across a call.

## 1. The rules this pass

* **Functionality**: byte-identical behaviour is the bar, refusal paths
  included. A change to what the user sees or can do goes in *WANTS THE
  OWNER* with its bytes, not into the diff. **E9 debug-console output on the
  shipped kern_mega counts as developer-visible**: gating it behind
  `MEGADIAG=1` is allowed if nothing (a test row, `tools/megatraps.py`,
  docs/BOCHS.md's recipes) reads it on the shipped build - grep `tests/` and
  `tools/` for the string first, and say what you found.
* **ABI**: nothing in `apps/os88api.inc`, `drivers/os88drv.inc`,
  `apps/cc/os88.h`, `apps/os88mega.inc`, a cell offset, a module's/driver's
  header or a file format changes. One that would pay goes in *WANTS AN ABI
  BREAK*.
* **kern_big must stay byte-identical unless the change is meant for it.**
  Most of this pass is `%ifdef KERN_MEGA` code: after each batch, `cmp`
  `build/kernel.bin` against the base's (built at the SAME commit count - the
  build number is the commit count, SPEC.md 14.2, so compare by building the
  base's sources with your tree's `buildnum.inc`, or diff `kernsize --json`
  sections and `tools/os88rdiff.py kernel <base> .` routine lists, which
  ignore it). A kern_big change that IS meant must keep kern_small and
  kern_emu building and is measured on all four.
* **Hot paths are off limits for anything that costs cycles**: IRQ0 and
  `mega_t08`, the keyboard ISR and `mega_k09`, the mouse ISRs, `sch_switch`,
  the API cells and their kern_mega stubs, the #GP fixups that run per
  faulting instruction in a loop (`mega_gpfix` etc. - MEGATRAP's census is
  why they matter), `mega_lincopy`'s inner copy, the native floppy's per-byte
  and per-sector paths, `font_run`, the blit loops. Cycle-neutral or faster,
  with the count in the note. `hotpar` (fast tier) gates the parity of the
  hot routines it knows; keep it green.
* **The 286 reset path cannot run on QEMU or Bochs** (both are 386s). Code on
  it (`mega_r286`, the CMOS shutdown handshake, `PICLOSE`'s reasons) is
  changed only where equivalence is provable by reading, and the note says
  so explicitly; docs/86BOX.md is the only emulator that runs it.
* **Ownership**: four agents run at once. Do not edit another agent's range
  (0.2). `kernel/kernel.asm` is **shared**'s; if you need a line there, put
  it in CROSS-FILE.

## 2. Method

* **First command**: `cd /home/user/wt/<name> && git status` - you are on
  `msp1-<name>`; if not, stop and say so. **Never edit or build in
  `/home/user/os8088`** - it is the coordinator's tree.
* **Scratch**: your own directory under the session scratchpad,
  `<scratchpad>/<name>/`. **Never `git stash`** (one stack for every
  worktree). `git show <base>:path` or a private `git worktree add` of the
  base are the safe ways to compare.
* **Builds**: 4 cores and 15 GB shared by FOUR agents - **`make -j1`** (or
  `-j2` briefly). A fresh worktree has no `build/`: start `make -j1` and
  `make -j1 mega` first. Most of your batches need only `make mega` to
  measure; run plain `make` (which runs the fast tier) before each commit,
  and `make small` / `make emu` only if you touched code they assemble.
* **Measure base and tip yourself**, absolute figures from
  `tools/kernsize.py --build build/megak --json` (and plain `--json` for
  kern_big). `tools/os88rdiff.py kernel <base> . -D KERN_BIG -D KERN_MEGA`
  (check its `--help` for how it takes a kern_mega build) shows every routine
  you changed. Quote **assembled** bytes, never estimates - except in
  DEFERRED, where an estimate is expected and is labelled as one.
* **Commit in small batches** with messages ending `(kern_mega size pass 1)`
  and the session's attribution trailer; never a model name. Gate: `make
  -j1 mega`, `make -j1` (fast tier), `python3 tools/os88ovlchk.py` if you
  moved code between sections, `tools/stkbalance.py` over files you touched,
  `python3 tools/checkdocs.py` after `git add`.
* **Equivalence**: "it assembled" is not equivalence. Read every moved entry
  (a reference walk misses fall-through entries); where a path has no row,
  prove it by reading or a scratch harness and record how.
* **Rows**: kern_mega's rows are `mega-*` (Bochs, `tests/megaport.txt`) and
  the `mega*` rows in `tests/suite.py`. Bochs is being installed by the
  coordinator (`which bochs`); MartyPC is not built in this container and
  kern_mega does not run on it anyway. Name the rows that cover what you
  changed; run **only those, scoped with `-k`, ONCE, after your last code
  commit**, with `--marty-jobs 1`. Never the whole soak, never `test-full`.
  A row that fails: re-run it alone on the BASE before blaming yourself
  (`python3 tools/os88bisect.py classify <row>`). If Bochs is unavailable,
  say so and list the rows you would have run.

## 3. The note

`docs/plans/completed/msp1/<name>.md`, the ksp13/ksp14 shape: **BASE AND
TIP** (a table: kern_mega all sections + KERN_SIZE, and kern_big - "byte-
identical" or its delta), **TAKEN** (each item: bytes per section per kernel,
cycles if hot), **REFUSED** (bytes and why), **DEFERRED** (seen, not done this
round - estimated bytes, where, what it would take; be generous, this is the
next pass's work list), **WANTS THE OWNER**, **WANTS AN ABI BREAK**,
**CROSS-FILE**, **ROWS RUN** (with results), **DEFECTS FOUND**. After adding
the note run `python3 tools/os88index.py` and commit both.

**Your final message to the coordinator is ONE message**: the branch tip, the
kern_mega delta per section and conventional-resident total, kern_big's
delta (or byte-identical), WANTS THE OWNER items, rows run with results, and
anything that blocked you.
