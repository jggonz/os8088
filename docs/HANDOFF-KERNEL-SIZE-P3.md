# Handoff: kernel size pass 3, the last one — scope, lock and the parallel soak

> **PASS 3 HAS LANDED. Its record is `docs/HANDOFF-KERNEL-SIZE-P4.md`; this file
> is the brief it was run from.** Two things in it are corrected in place rather
> than deleted: **§1's scope was wrong** (it nominated pass 1's ten; the pass
> took the twenty-one files nobody had swept), and the title's *"the last one"*
> did not survive contact — the pass ended with **three owner decisions, a
> measured-and-refused loader merge, and a costed `jcc` residual**. §2 (the
> lock) and §3 (the parallel soak) are unchanged and still the how-to.

**This is the FINAL pass.** Passes 1 and 2 took the kernel from its own base
to 87,646 bytes of code across 44 files, and this one closes the work out.
Read it before starting; read `docs/HANDOFF-KERNEL-SIZE.md` for pass 1's
**method** and `docs/HANDOFF-KERNEL-SIZE-P2.md` for pass 2's **record** — this
file does not repeat either, it says what is LEFT and how to run it.

Two companions matter as much as those:

* **`docs/HANDOFF-SOAK-FINDINGS.md`** — thirteen open defects the pass-2 soak
  found, none of them a kernel regression. Open it BEFORE diagnosing any test
  failure of your own, because most of them are already in it.
* **`docs/KERNEL-MEMORY.md`**, and specifically "Size pass 2 gave five rungs
  back" — where both budgets stand, and the one decision this pass inherits.

---

## 1. THE SCOPE — **CORRECTED IN PLACE. It was twenty-one files, not the ten this section nominated**

> **This section was wrong when it was written, and the pass did not follow it.**
> It nominated `wm files vga12 disk fdlg diskw mouse ui menu driver`. Those ten
> are **pass 1's**. Pass 3 took the twenty-one kernel files that had never had a
> focused finder at all, and `docs/HANDOFF-KERNEL-SIZE-P4.md` is its record.
>
> The correction is left visible rather than swapped out, because the mistake is
> the reusable part: **an absence in one pass's records was read as evidence
> about a file, when it was evidence about that pass's own coverage.**

### 1.1 What is actually true

`kernel/` holds **45 files** — 44 when this was written; `stkdiag.inc` arrived
with the worker-stack-slots merge and is knob-only. Coverage by *focused
finder*:

| | files | recorded in |
|---|---|---|
| pass 1's twelve | `wm` `files` `vga12` `disk` `fdlg` `diskw` `mouse` `kernel.asm` `ui` `menu` `driver` `ctrl` | `docs/HANDOFF-KERNEL-SIZE.md` §1 |
| pass 2's twelve | `assoc` `memory` `instance` `filecp` `font` `apps` `icons` `softgfx` `vidsel` `snd` `fsx` `sched` | `docs/HANDOFF-KERNEL-SIZE.md` §2, landed per `docs/HANDOFF-KERNEL-SIZE-P2.md` §0 |
| **pass 3's twenty-one** | `band` `blank` `bootprof` `clip` `clock` `clockw` `clone` `cpudet` `desk` `dock` `dskwin` `events` `fprog` `loader` `mod` `moudiag` `splash` `stkdiag` `toast` `viddet` `xmem` | `docs/HANDOFF-KERNEL-SIZE-P4.md` §0 |

12 + 12 + 21 = 45, with no file counted twice. Four of the twenty-one are
knob-only and compile to **0 bytes** in every shipped kernel (`band`,
`bootprof`, `moudiag`, `stkdiag`), so pass 3 gave **seventeen** files a focused
finder, plus a cross-cutting sweep that owned none of them.

### 1.2 The step that was wrong, and two more defects in the same paragraph

**The observation below is right; the inference drawn from it is not.** Pass 2's
finding ids really do contain no `F-wm-`, `F-files-`, `F-vga12-`, `F-disk-`,
`F-diskw-`, `F-fdlg-`, `F-mouse-`, `F-ui-`, `F-menu-` or `F-driver-` — checked
again, and those ten strings occur in this repository's history **only inside
the commit that added this document**. They are missing because **pass 2 did not
cover those files. Pass 1 did.**

`git show 2f33456 --stat` settles it in one command: pass 1 changed `wm.inc` by
971 lines, `files.inc` 751, `diskw.inc` 706, `vga12.inc` 703, `disk.inc` 459,
`fdlg.inc` 393, `ui.inc` 378, `driver.inc` 369, `mouse.inc` 231 and `menu.inc`
157.

* **The prefix list is also incomplete.** Six more landed in pass 2 and are not
  in it: `F-clip-` (`42d7b72`), `F-cpudet-` (`f98ddc5`), `F-desk-` (`79c25cf`),
  `F-fprog-` (`ff215f7`), `F-toast-` (`401aecb`, `26135ee`) and `F-xmem-`
  (`e32b873`). Those came out of pass 2's **four tail-sweep groups**
  (`docs/HANDOFF-KERNEL-SIZE-P2.md` §2: *"one per named file, four tail-sweep
  groups covering the remaining ~20 files"*) — which is the distinction a
  finding id cannot carry, because a group's id is spelled like a file's.
* **The exclusion rule contradicts the inclusion rule.** "What is NOT in scope"
  below drops `kernel.asm` and `ctrl.inc` for being *"a third look, not a
  first"*. Every one of the ten this section nominated would have been a
  **second** look, and the twenty-one it never named were the only **first**
  looks left. Applied consistently, its own rule selects pass 3's actual scope.

### 1.3 What the error would have cost

Nothing was lost — the pass ran on the twenty-one from its first day — but the
failure mode is worth naming. **The ten are the largest files in the kernel**,
so a pass that took them would have *looked* productive while re-reading swept
code. The table below is still the right table for whoever takes a second look
at them, and pass 3's own duplicate scan says where the largest remaining
machine-detectable duplication in the kernel is: `vga12.inc` and `font.inc`,
both hot, both already swept (`docs/HANDOFF-KERNEL-SIZE-P4.md` §2.4).

### 1.4 Pass 1's ten — still the largest files, and a second look at them is real work

**They are 65.7% of the kernel:**

| file | `.text` | `.cold` | code | `.bss` | pass 1 took | pass 2 took |
|---|---:|---:|---:|---:|---:|---:|
| `wm.inc` — the window manager (§11) | 11,706 | 94 | **11,800** | **1,074** | −701 | −125 |
| `files.inc` — the Disk window (§22) | 1,049 | 8,195 | **9,244** | 471 | −520 | −111 |
| `vga12.inc` — VGA planar primitives (§5) | 7,217 | 702 | **7,919** | 162 | −573 | −48 |
| `disk.inc` — volumes, mount, FAT read (§18–19) | 395 | 5,771 | **6,166** | **890** | −487 | −56 |
| `fdlg.inc` — the Standard File dialog (§38) | 241 | 4,958 | **5,199** | 168 | −318 | −80 |
| `diskw.inc` — the FAT write path (§18.4–18.6) | 179 | 4,565 | **4,744** | 155 | −441 | −61 |
| `mouse.inc` — serial mouse and cursor (§9) | 3,701 | — | **3,701** | 149 | −1,105 | −22 |
| `ui.inc` — the UI task and event ladder (§13) | 3,353 | — | **3,353** | 58 | −144 | −19 |
| `menu.inc` — menu bar and pull-downs (§12) | 2,744 | 177 | **2,921** | 197 | −147 | −18 |
| `driver.inc` — loadable drivers, `SYSTEM.CFG` (§51) | 496 | 2,003 | **2,499** | 348 | −496 | −39 |
| **total** | | | **57,546** | **3,672** | **−4,932** | **−579** |

The "pass 2 took" column is NET of feature growth over the same window, so the
real take is a little larger and the conclusion is unchanged: **8.4% from pass
1, and roughly 1% from pass 2's cross-cutting reach.**

### 1.5 The one row that makes the case — still the argument for a SECOND look

`ctrl.inc` was pass 1's twelfth file. **Pass 1 took 5 bytes off it. Pass 2 gave
it a finder and took 465** — 44% of what was left. It is not a fair sample (it
is mostly `.cold` and mostly comment), and the counter-example is honest too:
`kernel.asm` had a pass-2 finder as well and gave up 32 bytes on 3,050, about
1%. **So the re-sweep rate ranges from 1% to 44% on the only two data points
there are, and this handoff deliberately does not pick a number.** Producing
one is what phase 2 (consolidation) is for, off assembled byte counts, and
PERFORMANCE.md rule 4 applies to size as much as to speed.

For scale: pass 2's own twelve went 22,303 → 19,552, a **12.3%** take on files
nobody had swept.

### 1.6 What this section excluded — and how each exclusion reads after the correction

**Four of these five bullets were written against the wrong scope.** They are
kept verbatim, with what actually happened underneath each.


* **`kernel.asm`, `ctrl.inc`** — pass 2 gave both a finder. Re-sweeping them is
  a third look, not a first.
* **`band.inc`, `bootprof.inc`, `moudiag.inc`** — knob-only. They compile to
  **0 bytes** in every shipped kernel, and CLAUDE.md's memory rule says no
  guard binds a knob build. Bytes there cost nobody RAM.
  > **Right, and `stkdiag.inc` joins them** — four knob-only files now. But the
  > rule *"knob-only files compile to nothing"* was then applied to FILES and
  > not to BLOCKS, and a block-level `%ifdef` inside a shipped file compiles to
  > nothing just as surely. Three separate censuses in pass 3 counted such
  > blocks as resident (`docs/HANDOFF-KERNEL-SIZE-P4.md` §6.3).
* **`splash.inc`** — the only shipped file neither pass swept, and it compiles
  to **0 `.text`, 0 `.cold` and 2,015 `.boot2`**. `.boot2` is the stage-2 blob:
  it is disk and boot time, not resident RAM, and it is not inside
  `KERN_BUDGET` at all. Worth a look for BOOT time (`docs/BOOT-PERF-PLAN.md`),
  not for this pass's guard. **Careful there**: `SPL_RESIDENT` gates what the
  splash may call, and `tests/unit/t_resident.py` exists because one converted
  site put a boot on a jump into sectors the floppy had not delivered.
  > **Right about the currency, wrong about the priority.** `splash.inc` was in
  > pass 3's twenty-one and its batch had to go **FIRST**: `.boot2` 2,439 →
  > 2,254 retired `BOOT2_SECS_STARS`, which let `KSIG_OFF` move 50,176 → 6,656
  > and took the `.text` FLOOR from 50,178 to 6,658. Without that, the rest of
  > the pass was a build break that the boot canary would have passed
  > (`docs/HANDOFF-KERNEL-SIZE-P4.md` §1).
* **`F-sched-14` and `F-sched-12`** — refused by the owner in pass 2, because
  the scheduler is about to be redesigned (`docs/SCHED-IDLE-PLAN.md`). Do not
  re-propose them.
* **`F-crosscut-13`** (a call layer under a published slot for 9 bytes) and
  **`F-assoc-18`** (`imgpara` derives from `.text + .bss` together, so moving
  bytes between them changes the footprint by **zero** — measured twice,
  independently). Both refused on their merits; both will be re-found.

### 1.7 One thing has changed about WHICH bytes are worth taking

**For the first time in this project's life, the SEGMENT binds and not the
footprint.** `.text + .bss` is 56,374 of `KERN_CODE_MAX`'s 65,536 — 9,162 left,
and that limit **cannot be raised at all** — while `KERN_BUDGET` has 19,456
(38 steps). See `docs/KERNEL-MEMORY.md`, "Which guard binds", which is
corrected in place.

**Those two figures are pass 2's close, and this whole subsection held.** Pass 3
took the segment to **55,886 with 9,650 left** and the footprint to
`KERN_SIZE` 109,056 — 40 steps — so the gap between the two guards widened
rather than narrowed (`docs/HANDOFF-KERNEL-SIZE-P4.md` §0).

Two consequences for this pass:

1. **A `.bss` byte is worth exactly as much as a `.text` byte**, because both
   are in the segment. `wm.inc`'s **1,074** and `disk.inc`'s **890** are now
   the two largest single `.bss` claims in the kernel and both are in scope.
   Pass 2 went after `instance.inc`'s 694 for this reason and got it to 630.
2. **Moving `.text` into `.cold` relieves the binding guard**, which it did not
   usefully do when the footprint was tight. It is a DEFERRAL and not a saving
   — the footprint does not move — and KERNEL-MEMORY's warning stands: with a
   full rung, the first byte moved out of the segment costs a whole 512-byte
   step of footprint. Price both sides before proposing one.

---

## 2. THE LOCK — read this before running anything

`tools/martylock.py` serialises two hazards that share one cure:

1. **One machine, one client.** Every emulator test drives MartyPC's debug
   server. A second client on one instance is refused in a sentence naming the
   holder now (it used to hang) — but two SESSIONS building the same tree still
   collide.
2. **A rebuild invalidates the symbol map.** `tools/os88sym.py` refuses an
   address unless a fresh assembly of `kernel.asm` is byte-identical to
   `build/kernel.bin`, and the About box's build number is the **commit count**
   (SPEC.md §14.2).

   **Which step fires it matters**, because a hook or a person will need to
   commit mid-session. The count reaches the assembler through
   `build/buildnum.inc`, which only `make` regenerates. So **`git commit` alone
   touches nothing under `build/`** — it ARMS the hazard; **`make` FIRES it**,
   rebuilding `kernel.bin` and rewriting every floppy image *including the one
   the emulator has mounted*, which is the worse half.

**The rule is: hold the lock to use MartyPC, OR to `make`, OR to `git commit`,
OR to edit `kernel/*`.**

```sh
python3 tools/martylock.py status
python3 tools/martylock.py run --holder <you> --why build -- make   # safest form
python3 tools/martylock.py acquire --holder <you> --why marty \
        --purpose "..." --ttl 45 --wait 1800
python3 tools/martylock.py renew   --holder <you> --ttl 30
python3 tools/martylock.py release --holder <you>
```

Exit **75** = held by someone else, retrying is meaningful. Leases expire, so a
dead holder cannot wedge it; `build/martylock.log` is the audit trail. **Never
`break` a lease that has not expired.** The lock is a directory on one
filesystem, so it is PER-CHECKOUT and does not reach another container.

**Pass 2 committed once mid-run anyway**, having correctly deferred three times
and then argued itself out of it under stop-hook pressure. Eight rows died on
*"the map describes a DIFFERENT kernel"*. `make` resyncs it; the cost was the
re-run.

---

## 3. HOW TO RUN THE PARALLEL SOAK

The soak is ~2.8 hours wall on a four-core box and it is the pass's closing
gate. It runs in **three passes**, and the split is not optional.

### The commands

```sh
# prerequisites - the suite SKIPS what a box cannot answer, but only if the
# artefact exists to be found (docs/HANDOFF-SOAK-FINDINGS.md B4)
tools/setup-cc.sh            # once; nothing in `all` needs it
make wiredisk                # `all` deliberately does not build it (§78.9)
make                         # AFTER any commit - see the lock above

# THE SOAK - one command. The rows that cannot share the box say so
# themselves now (alone=True), so there is no second pass to remember.
python3 tools/os88test.py soak --marty-jobs 3

# ...and anything that SKIPPED for a capability the box has since gained
python3 tools/os88test.py soak -k 'weave*' -k ctoolchain --marty-jobs 2
```

**IT WAS THREE PASSES**, and the first two were `-x saverate -x deskbench -x
uilat -x wirefps` followed by the same four with `-k`. That is a property of
those rows and it lives on the rows now — anything the reader has to remember
is something the next reader will not.

### Why the lanes are what they are

* **`--marty-jobs N` is how many EMULATOR rows may run at once** (default 1).
  Instances are isolated — own port, own directory, own disks — so this is a
  question about cores, not safety. **Rows marked `builds=True` run alone
  whatever it says**, because they own the tree.
* **`-j N` is the host-side lane** and is separate.
* **Three lanes run in order**, and the order is the point: host-side parallel
  first, then the serial and `builds=True` rows one at a time, then the
  shareable emulator lane LAST — never beside the builders, because a `make`
  halfway through rewriting `build/kernel.bin` is exactly what an emulator row
  must not be reading.
* **A row whose assertion is a frames-per-second figure cannot share four cores
  with three emulators.** That is exactly what corrupts it, and widening a
  tolerance to suit would be permanent. Those rows carry **`alone=True`**,
  which is a different claim from `builds=True`: a builder cannot share the
  TREE, an `alone` row cannot share the CORES. Both land in the
  one-at-a-time lane of the same run.
* **`-x` prints what it dropped.** A green result must not quietly be a green
  result over less.

**Three is the right width on four cores.** Measured aggregate guest speed
against a real 4.77MHz 8088: 3.4x at one instance, 13.1x at four, 13.9x at six,
**13.4x at eight** — flat past the core count, so a ninth instance does not add
throughput, it slows the other eight. Going wider is slower, not broken: guest
cycle counts, `disk()` counts and pixel comparisons stay exact at any
oversubscription because they are counted rather than timed. What loses slack is
host wall-clock — `settle`, `until`, a row's timeout — which is
`docs/HANDOFF-SOAK-FINDINGS.md` **B5**, and B5 is why `dispmine` and `tmowner`
fail in the wide lane and pass alone.

### Expect it to take about 2.8 hours

`soak` runs all three tiers — **243 rows as this is written**, of which 196 are
the soak tier itself, 37 fast and 10 full. At pass 2's count of 239 that was
9.24h **declared serial**, and a critical path at `--marty-jobs 3 -j 4` of 4.69h
declared. **Declared ≈ 1.75 × actual**, measured across both passes, which gave
**pass A ≈ 2.7h and pass B ≈ 8 min**.

The durable part of that is the ratio and the floor, not the hours: recompute
from `--list` rather than quoting these. **The floor is the `builds=True` set**
— 36 rows, ~1.4h actual — and no width helps them.

### Classifying a failure — cheapest-decisive first, and in this order

1. **Re-run the row ALONE on HEAD.** Settles contention, which is the commonest
   cause and one row of emulator time. Two of pass 2's fifteen never got past
   this step.
2. **Run it at the pass's base commit** in a scratch worktree. Settles
   pre-existing vs regression.
3. **Bisect** — only where 1 and 2 disagree.

Pass 2 deferred the baseline soak (the owner's decision, saving four hours) and
paid for it here, fifteen times. That was still the right trade, but budget for
it: **a deferred baseline is not free, it is a classification run per failing
row.**

Keep a worktree at the base commit for step 2 — `git worktree add /tmp/base<x>
<base>` — and give it the shared instruments rather than rebuilding them:
`ln -s <tree>/build/martypc`, `ln -s <tree>/build/cc`. **Both are pinned
upstream artefacts and identical by construction; nothing else may be shared,
and a shared writable DISK is what contaminated pass 2's first bisect.**

### The three traps that cost pass 2 the most time here

* **A stale QEMU from an earlier row holds `build/os8088.img` for hours** and
  the next row fails wearing a message about the wrong subject
  (`HANDOFF-SOAK-FINDINGS.md` B9). `ps -o pid,etime -C qemu-system-i386` before
  believing a lock error, and **kill by PID** — `pkill -f` matches the killing
  shell's own command line and exits 144 having killed nothing.
* **`os88marty.py reap` kills ORPHANS only** and leaves live work alone. Use
  `instances` to see whose is whose; take the address off the object
  (`m.addr`, `m.port`) rather than typing 9001.
* **A test that catches its own exception and prints `str(e)` gives you
  nothing.** `deskbench` cost a 150-second re-run to locate a `list index out of
  range`; a traceback in the handler named it on the first attempt. If a row
  fails illegibly, fix the report before the defect.

---

## 4. The baseline, as it stands NOW

Blessed into `docs/KERNEL-MEMORY.md` at the close of pass 2, so `make`'s delta
is `+0` until something moves it:

```
big    text 50,435  bss 5,939  cold 37,211  lowbss 8,798  ovlw 5,215
       KERN_SIZE 110,080 of KERN_BUDGET 129,536 -> 19,456 spare (38 steps)
       .text+.bss 56,374 of KERN_CODE_MAX 65,536 -> 9,162 left    <- BINDING
       accrued: image 54/512, cold 347/512

small  text 40,449  bss 5,427  cold 34,528  lowbss 8,328  ovlw 4,681
       KERN_SIZE  95,232 of KERN_BUDGET 107,520 -> 12,288 spare (24 steps)
       .text+.bss 45,876 of 65,536 -> 19,660 left
```

**`tools/kernsize.py` is the instrument and it does more than print.**

```sh
python3 tools/kernsize.py                    # report against the baseline
python3 tools/kernsize.py --modules          # PER-FILE attribution - the scoping tool
python3 tools/kernsize.py --modules -DKERN_SMALL   # small's, on demand
python3 tools/kernsize.py --bless            # rewrite baseline AND the doc tables
python3 tools/kernsize.py --bless -DKERN_SMALL
```

**Bless BOTH variants and do it in the same commit as the change**, pasting the
report into the message. The baseline lives in the document, so the delta `make`
prints is "since this document last told the truth" — an un-blessed change shows
up as a non-zero delta on every build until somebody explains it or blesses it.
`--bless` writes the module and theme tables only for the **big** variant, on
purpose.

**Read the `accrued` line before believing `spare`.** 347 of 512 is already
spent into `.cold`'s current rung, so the next 165 bytes of cold code cross a
step whatever the spare says. And **think in bytes, not rungs**: the amortised
price of a byte is a byte. Pass 2 uncrossed five rungs and an unrelated branch
took one of them back with **228 bytes** against the 174 the pass had left in
that rung — that arithmetic is in KERNEL-MEMORY and it is what "slack's expected
cost is 100%" looks like with numbers on it.

---

## 5. The method, in order — seven phases

1. **Baseline.** `kernsize` both variants, `sysbench`/`gfxbench` per adapter.
   The soak baseline is the owner's call; pass 2 deferred it (see §3).
2. **Find — one agent per file**, briefed from one shared document. Ask for the
   byte count to be **assembled**, not estimated. Ten files means ten agents
   plus a cross-cutting duplication sweep that owns no file.
3. **Consolidate and de-duplicate.** Finders report cross-file duplicates from
   both ends by design, so a naive sum is wrong: pass 1's de-dup moved the total
   by 528 bytes and found 12% of `.cold` claims double-counted.
4. **Adverse review**, grouped by shared RISK rather than by file.
5. **Integration review** — what only appears in combination.
6. **Implement in gated batches**, each gating with `make` + `os88test.py fast`
   + `os88ovlchk.py` + a `stkbalance` diff + **a boot**.
7. **Soak and bench comparison** (§3).

**Phases 4 and 5 are not optional.** In pass 1 they removed about a fifth of the
proposed bytes and found four real bugs that were not size findings at all; in
pass 2 they are why 253 findings became 4,661 bytes with no behavioural
regression.

### What pass 2 added that pass 1 did not have — reuse it, do not rebuild it

| | |
|---|---|
| **the shared epilogue ladder** (SPEC.md §15.1.2) | `jmp kret_di` ≡ a seven-`pop` run. 141 sites, three ladders (`.text`, `.cold`, far-`.cold`), −485 bytes, and the arithmetic matched the measurement **to the byte**. It is BUILT — new sites just use it. **A site costs 31 clocks / 6.5 µs each time it is taken**, measured (PERFORMANCE.md, *What one rung of the ladder costs*), not the ~18-22 an instruction table gives: free on a routine-level exit, NOT free per run, per cell or per pixel |
| **`tests/unit/t_resident.py`** | no `jmp kret*` in `.text` below `spw_resident_end`. Mutation-tested both ways. **It exists because one converted site of the 141 shipped a kernel that booted on nothing** |
| **`t_asmrules` check 4** | a local block reachable by neither name nor fall-through. Lands green with no exception list; it found the DMA staging bug |
| **`t_asmrules` rung-aware `crossed_pops`** | the ladder would otherwise have blinded it on 141 routines |
| **`stkbalance` now covers the KERNEL** | pass 2's handoff listed "a push/pop balance gate for the kernel" as work that did not exist. It does now, plus `stkapps` for every shipped package and driver. Annotate a deliberate imbalance `; STKBALANCE-OK:` with the reason |
| **`os88test -x`, the `wiredisk` capability** | §3 |
| **`tools/martylock.py`** | §2 |
| **`benchdiff.py`** (pass-2 scratchpad) | catches a regression, a cross-ROM comparison, and a comparison that did not happen |

### The `jcc` census, if you re-run Batch 8's idea

NASM expands an out-of-reach conditional to `7X 03 E9 xx xx` — 5 bytes for 2.
Pass 2 counted **306 expansions, 261 after batches 0–7 had pulled 45 back into
reach for free**, and took 131 of a ~147 takeable ceiling with only **14 new
trampolines for 25 clusters** (13 clusters wanted a label and nothing else).
Re-take the census rather than reusing that number, and **count it AFTER the
other batches land** — counting first is double-counting.

---

## 6. The one thing that will cost you time, stated plainly

**Fourteen times in pass 2, a check ran and nothing read its answer.** Not one
was a check that FAILED. Every one passed for the wrong reason:

* `kernsize` reporting figures for a tree that did not assemble
* `gate.sh` testing `tee`'s exit status instead of the gate's
* `runadapter` poised to overwrite the reference it would later be compared to
* a baseline script that would have read an argparse error as a result
* a bisect verdict returning "untestable" for a hang, while hunting a hang
* a positive control fitted to a 32-row icon that a 14-row record cannot reach
* an `apps/gfxbench` path that does not exist, whose empty diff proved nothing
* Batch 8 deleting a `jmp` and passing seven gates including a boot
* Batch 7 shipping a kernel that booted on **no adapter**, with `make`, the fast
  tier, `stkbalance`, `os88ovlchk`, `t_asmrules` and `checkdocs` all green

**The defence that worked, every time, was to break the thing on purpose and
confirm the gate noticed.** Do that for every gate you write, and write down
that you did.

The corollary is the boot: `gate.sh` gained a seventh step because steps 1–6
were green on a kernel no machine would start. **A batch is not gated until a
machine has started.**

---

## 7. Work that does not collide, if a second session is free

1. **The soak findings queue** — `docs/HANDOFF-SOAK-FINDINGS.md`, thirteen
   items. B1 (install-then-boot in one instance), B3 (`cold_span`'s `.ovlw`
   bound, one line), B5/B6 (the host-timing waits) and B9 (the leaked QEMU) all
   make this pass's own soak cheaper and none of them touches kernel bytes.
2. **A field run on real hardware.** `docs/FIELD-MACHINES.md` says who has the
   iron. Nothing in this pass can measure a real 8088, and **the Hercules
   reading `docs/MONO-RECLAIM-PLAN.md` needs has never been taken.**
3. **Re-measure PERFORMANCE.md's Hercules VRAM rows at N ≥ 48**
   (`HANDOFF-SOAK-FINDINGS.md` C1). The documented write ratio of 1.36 is really
   **1.671** — the published figure is an N=8 reading of a bimodal quantity —
   and `docs/MONO-RECLAIM-PLAN.md`'s whole case reasons from it.
4. **`tests/int0sweep.py` on the IBM ROM.** Pass 1's worst bug hard-locks an IBM
   machine and is a wrong clip index on GLaBIOS, and every other MartyPC row
   runs GLaBIOS, so the class is invisible. **`tools/martypc/roms/` is
   gitignored** — IBM's ROM cannot be redistributed under this repo's MIT
   licence — so **a fresh container has no ROM and supplying one is a
   per-session act.** Verified fingerprint when present: md5
   `f453eb2df6daf21ec644d33663d85434`, `0xFE001` reads `501476 COPR. IBM`
   against GLaBIOS's `GLaBIOS [`, reset vector dated 10/27/82. **A missing IBM
   ROM is a loud rc=1 startup failure, not a silent substitution** — established
   by reading the pinned MartyPC's `rom_manager`, correcting pass 1's belief.
   **Fingerprint what loaded; never infer it from the config name.**
5. **`docs/LAST-DROP-BYTES.md` upkeep** — the live register of `.ovl`-eligible
   bodies, and its §7 is the list of merges that look available and are not.
   Every row this pass lands or refuses belongs in it.
6. **`tools/os88geom.py`'s `_MIRROR`** — auditing which host tools still
   hard-code a kernel constant is self-contained and needs no lock.

---

## 8. The rules that bite hardest, restated

`SS ≠ DS`, so `[bp+disp]` addresses SS. **Sections are different segments** —
`.text` is `KERNEL_SEG`, `.cold` is `COLD_SEG`, `.ovl`/`.ovlw` is the boot
overlay released at `spl_finish`, `.lowbss` is `LOW_SEG` reached through SS —
so a near call across a section boundary assembles cleanly and runs wrong, and
"merge these two identical routines" is only free when both ends are in the same
section. **A NASM local belongs to the last non-local label, so moving code
re-parents every local inside it.** `.bss` cannot hold a non-zero resting value.
`[vid_w]`/`[vid_h]`/`[vid_stride]`, never `SCREEN_W`/`SCREEN_H`. Text is
`font_run`, and `tests/textsites.txt` only goes down. 8086 only: no `pusha`, no
`push imm`, no `movzx`, no 32-bit registers.

**`.ovl` has no partial credit.** A body reached from `ui_task` on any pass, from
an ISR, from a published `OSAPI_*` slot, or through a pointer in a table that
outlives boot is disqualified completely — it is a freed heap claim being
executed. `tests/ovlrefs.txt` registers the "what guarantees this runs before
`spl_finish`?" answer and `tools/os88ovlchk.py` enforces it.

**Treat `gfx_*`, `mouse.inc` and `sched.inc` as externally hot.** Pass 2 refused
123 bytes of the `jcc` ceiling on that rule alone, and the owner refused two
scheduler findings outright. A byte that costs a taken jump in an ISR is not a
byte this pass wants.

**Think in bytes, not rungs.** The amortised price of a byte is a byte. Quote
`kernsize`'s SUM and its ACCRUED line, never its step count.

---

## 9. Two conventions of this fork, so the first hour is not spent finding them

* **Unshallow before believing any ancestry answer.** A fresh clone is shallow
  and `git merge-base` returns confidently wrong answers on one.
  `git rev-list --max-parents=0 HEAD | wc -l` must print **1**; six means
  shallow, not unrelated. `docs/UPSTREAM.md` carries this as its Rule 0.
* **"Merge to elendilon" means the BRANCH**, which is the integration branch
  this work lands on; `main` is behind it and is not the target. **Send the
  360KB image set after every commit** — all three of `os8088-360.img`,
  `apps360.img` and `media360.img`, because `BEVERLY.MOD` comes off the apps
  disk at that geometry — and **attach the files**; a path into this
  container's `build/` is not a delivery. **Do not boot an image after building
  it**; that relaxes neither the testing that earns a commit nor the rebuild
  and boot a merge onto `elendilon` needs. CLAUDE.md's last section is the full
  set and it is the section a PR going upstream deletes.
