# The parallel soak: one command, and why contention was the wrong suspect

> **BUILT.** `tools/os88soak.py` is the command, `os88marty`'s guest clock is
> the mechanism, and `tests/unit/t_machines.py` is the gate. This file is the
> design record: what was measured, what the measurement overturned, and what
> is deliberately left as a knob.

**A failing row is a defect until it is diagnosed, and there is no list of
rows it is acceptable to find red.** There used to be one — a one-time
handoff of what a size pass's soak turned up — and it was read as a standing
permission to look a failure up rather than fix it, so it is gone. A row that
an agent runs and then ignores the result of is worse than no row: it costs
the emulator time and buys nothing. This file is about the RUN rather than
the rows.

---

## 0. THE COMMAND

```sh
python3 tools/os88soak.py check     # can this box answer? what would skip?
python3 tools/os88soak.py start     # preflight, then run detached
python3 tools/os88soak.py status    # cheap progress read - SAFE to poll
python3 tools/os88soak.py stop
```

`make test-soak` still exists and still runs the tier **serially**
(`--marty-jobs 1`), which is why the parallel invocation lived in two handoff
documents as a line to remember (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md` §3).
Anything a reader has to remember is something the next reader will not.

---

## 1. THE MEASUREMENT THAT OVERTURNED THE THEORY

Twelve shareable emulator rows, run twice on a four-core container: once at
`--marty-jobs 1` on an idle box, once at `--marty-jobs 3` with **two extra CPU
hogs** — deliberately past the core count, which is the state an agent
checking in or a small side task produces.

| | idle, width 1 | width 3 + 2 hogs |
|---|---:|---:|
| rows passed | **12 / 12** | **12 / 12** |
| sum of row times | 893 s | 949 s — **1.06x** |
| worst single row | — | **1.17x** (`evqfull`) |
| wall clock | 892 s | **486 s** |

**Nothing timed out, and nothing came close.** `dispmine` — the row B5 records
as failing in a 3-wide lane and passing alone — went 45.5 s to 49.7 s against a
270-second timeout.

### 1.1 So what does contention actually do?

`OS88_WAITLOG` recorded every wait in both arms — **118 in each, the same
waits in the same scripts** — with what each cost in host seconds and in guest
seconds:

| | idle, width 1 | width 3 + 2 hogs |
|---|---:|---:|
| waits recorded | 118 | 118 |
| **host** seconds per wait, median | **2.2** | **2.2** |
| **guest** seconds per wait, median | **7.3** | **5.9** |
| guest rate, median | 3.27x real | 2.54x — **78% of idle** |

Per script, same wait, same count:

| script | waits | idle guest s | contended | |
|---|---:|---:|---:|---:|
| `curshape` | 11 | 12.0 | 7.6 | **−37%** |
| `dispdrag` | 8 | 8.0 | 5.1 | **−36%** |
| `assocopen` | 8 | 9.2 | 6.0 | **−35%** |
| `dtfield` | 3 | 7.4 | 5.2 | −30% |
| `dockmark` | 21 | 6.5 | 5.3 | −18% |
| `calcflick` | 29 | 7.2 | 6.1 | −15% |

**The host column does not move and the guest column does.** A row's time is
host `time.sleep`, and a sleep does not stretch under load — so the same line
of the same test takes the same wall time and hands the machine **up to 37%
less work to do**.

> **Contention does not make a row slow. It makes it LESS THOROUGH, at the same
> wall time.** The row then fails somewhere further on, wearing a symptom that
> looks like the thing under test.

That is why "it passed alone" has been such an unsatisfying diagnosis for so
long: **the wall times never showed anything, because there was never anything
to see there.** Every classification run that went looking for a slow row was
looking in the one place the effect does not appear.

### 1.2 What follows, and what does not

* **Raising timeouts was never going to work**, and would have made things
  worse: a deadline that fires on nothing today would fire on nothing after,
  while a genuinely stuck row sat there longer.
* **`--marty-jobs 3` on four cores is sound.** 12/12 passed with the box
  oversubscribed by two whole CPUs. The width is not the problem.
* **The 465 blind `time.sleep` calls across 99 files in `tests/` are the
  contention surface**, and they are invisible to every instrument the suite
  has.

---

## 2. WHAT SHIPPED — the guest clock

`tools/os88marty.py` now hangs every wait off `GUEST_HZ`, the 8088's own
4.772727 MHz. MartyPC **counts** cycles rather than timing them, so the guest
clock is exact and reproducible whatever the host is doing. Two different
questions come off it and they wanted different answers.

### 2.1 PROGRESS — the half that makes a stuck row fail FASTER

`_Progress.check()` watches the guest cycle counter. A guest that has not
advanced for `GUEST_STALL` (2 s) is **stopped**, and a stopped machine can
never satisfy any condition — so the wait says so at once instead of sitting
out its deadline.

Measured, on a machine paused mid-session against `settle(limit=120)`:

```
failed after 2.1s host
  the GUEST CLOCK HAS NOT MOVED for 2.1s while waiting for the screen to stop
  changing - it is 'paused' at 0060:4A99. A machine that is not executing can
  never make a condition true, so this is reported now rather than at the end
  of the 360 guest-second budget. A breakpoint still armed, `run` never
  called, a triple fault, or the emulator gone.
```

**2.1 seconds against 120** — 57x — and the message names the machine and its
address rather than blaming the condition. Two seconds is many thousands of
guest instructions at any rate a running emulator achieves, so this fires on a
machine that is STOPPED and never on one that is merely slow.

### 2.2 BUDGET — the half that takes contention off the table

`settle`, `until` and `wait_stop` keep their `limit=` in host seconds, because
194 files pass one, and convert it at `GUEST_BUDGET_RATIO` (3.0) into a budget
of the guest's **own** seconds. The conversion is deliberately **below** the
idle rate and **above** the contended one, so:

* no wait gets tighter than the machine has ever needed — the widest wait
  measured across the two runs spends **21.2 guest seconds**, against a
  `limit=120` budget of 360: **17x headroom**;
* no wait gets looser because the box is busy.

The point is not tolerance, it is **evidence**. A failure now reads *"the
machine really did run 360 seconds of its own time without it happening"* —
a sentence that is equally true on an idle box and a loaded one, and that
contention cannot explain. That is what "stop blaming contention" actually
requires: not a wider margin, a currency the load does not move.

`OS88_WAITLOG=<file>` records what every successful wait cost, in guest
seconds, host seconds and rate. **That is how these numbers get re-derived
from a real soak rather than argued about**, and how the ratio should be
re-set if it ever needs to be.

### 2.3 `guest_sleep()` — the standard replacement for `time.sleep`

`os88marty.guest_sleep(m, secs)` spends a fixed amount of the GUEST's time.
It is the primitive new code should use wherever a test is giving the machine
time to do something, and it fails fast if the guest stops rather than
sleeping through a dead machine.

---

## 3. THE PACE IS GUEST TIME - it was the one thing left as a knob

`os88marty.GUEST_PACE` is how many GUEST seconds a harness pause spends per
second the caller wrote, and every pause in the harness goes through
`os88marty.pace()`: `settle`'s stillness window, `os88mouse`'s click, drag and
menu settles and its gap between packets, `os88mouserel`'s wall pacing,
`bp_count`'s arm, quiet and first windows, and `launch(boot=<secs>)`. The
default is **4.5**, the idle ratio measured on this container (4.4-4.8 over
three samples, idle desktop), so every existing call spends what it spent on a
quiet box - and now spends it on a busy one too.

It was a knob, **off by default**, while it waited for "a full soak behind
it"; the flip came with a sweep of the individual `time.sleep` calls in
`tests/` onto guest state and guest time, validated row by row.
`OS88_GUEST_PACE=0` puts the host sleeps back, for an A/B and nothing else.

**The asymmetry that made it safe to flip:** raising the guest time a wait
spends can only turn a failure into a pass. It cannot change what a passing
row measures. What it costs is wall clock under load, which is the honest
price of asking for the same work.

---

## 4. THE RUN — preflight, width, survival, check-in

### 4.1 The preflight is the item the pass-3 soak found by hand

Zero skips is the property worth having, and the pass-3 soak reached it only
after somebody noticed, after fifteen runs, that four disks the suite never
builds for itself were missing (`docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md` §9). **A skip
is the box declining to answer, not a pass**, and 196-of-200 reads like a good
run whether the rest skipped or never existed.

`os88soak.py check` says so before the hours rather than after them, names the
rows that would skip, and prints the command that fixes each gap.

### 4.2 The width is ONE PER CORE — and it was CORES-1 for a year

Measured aggregate guest speed on four cores: 3.4x at one instance, 13.1x at
four, 13.9x at six, 13.4x at eight — **flat past the core count**: four to six
buys 6% and six to eight *loses* 4%. That series has never been in dispute and
still sets the ceiling. What was in dispute is the LAST core.

The argument for leaving it was that it is what a `status` poll, an editor or a
small side task runs on — *a run sized to fill the box exactly is one that
anything else on the box perturbs* — and every perturbed row is an hour of
somebody deciding whether the failure was real. Two things retired it:

* **`status` reads a FILE** (4 above). It was never the load the argument
  feared, and nothing else in the workflow is either: `start` is detached, and
  the rule has always been to check in with `status` rather than by running
  rows beside the run.
* **Every row width 4 was blamed for has been diagnosed, and not one was a
  starved guest.** 15.2 read four failures at width 4 and concluded *"the pass
  rate does not hold"*; three of them were the private-tree rebuild race (8.9),
  a row's own kernel being rewritten underneath it, which width made more
  likely to OVERLAP and did not cause; the fourth was an `EVT_MDOWN` dropped
  from a full ring behind a confirmed button level (15.4). Both are fixed at
  the cause, and the full soak has since run at width 4 clean and repeatedly.

So `widths()` fills the box, 15.2's **16.8% off the wall** comes with it, and
15.1's niced fourth slot is retired with the same evidence — it existed to buy
that core back, and the core was never the problem.

**`os88test.py` run by hand still leaves one**, and that is the one place the
old argument survives intact: somebody is at that keyboard. `--marty-jobs`
overrides either way.

### 4.3 Surviving an idle container — HOLD A WAITING TASK

**This is the part that is not obvious, and it is a fact about the harness
rather than about the soak.** Observed on the pass-3 soak: an agent that ended
its turn — went to "completed" — with only the background soak running had the
soak **die about five minutes later**. The same soak, under an agent that was
*waiting* on it or had a waiting task, ran to the end.

So `setsid` is necessary and nowhere near sufficient. It makes the run outlive
the **shell**; nothing makes it outlive the **container**. The run must be held
open by something the harness can see:

```sh
until [ -f build/soak/latest/finished ]; do sleep 30; done
```

Run that as the background task and do not end the turn until it returns. The
run writes `finished` itself — through a shell wrapper around the runner, not
from `status` — so the loop ends on its own when the soak does and nothing has
to poll for it to work.

**A periodic check-in is not a substitute.** Between two of them the session is
idle, and idle is the state that kills the run. Poll `status` as often as you
like *on top of* the held task; it reads a file and cannot perturb anything.

And for when it dies anyway: every row that reports is **journalled**, and
`start --resume` re-runs only what did not finish. **The floor of a soak is its
`builds=True` lane** — 58 rows and 3.3 declared hours that no width helps — so
a restart from zero is the expensive way to recover from a session going idle.

### 4.4 Checking in cannot perturb the run

`status` reads a file. It starts no emulator, takes no lock and spends no
measurable CPU. That is the property that makes it safe to poll on a timer,
and it is the whole answer to "an agent waking to check the status must not
cause failures". What DOES perturb a run is running rows beside it, or a
`make` — both rewrite `build/` under the rows reading it.

---

## 5. THE IBM ROM — the case, and why none of the rows made it

The pass-3 soak found that *"a machine naming an IBM romset SILENTLY
RESOLVES to `glabios_pc` when the ROM file is absent, so the handful of rows
that ask for one were not testing it either."* Nine rows named a non-GLaBIOS
machine; four were registered; **not one had ever run on the ROM it asked
for**, and nothing in any output said so.

The ROM is IBM's, has never been licensed for redistribution and cannot be in
this tree (CONTRIBUTING.md §6). **We own the machine set**, though — a twin
differing only in `rom_set` is four lines of TOML, and four such twins already
existed. So the policy is the strict direction: **naming the IBM machine
requires a reason, and "it is the machine I was using" is not one.**

Audited row by row, no registered row made the case:

| row | what it actually does | verdict |
|---|---|---|
| `fillpat` | calls `gfx_fill_pat` through the debugger over rows it zeroed itself | the BIOS is not on that path |
| `icoclip` | calls `icon_draw` through the debugger over a zeroed background | same |
| `int0sweep` | traps INT 0 with a **debugger breakpoint**, which fires before any ROM handler runs | the ROM changes the CONSEQUENCE of a divide overflow, not the detection |
| `dskwstage` | calls `dskw_runadd` through the debugger | E2 ran it on both ROMs and got the identical hang — the ROM is **measured** not to be the variable |

The strongest evidence is `assocglyph`'s, which was already in the tree: its
two references were taken under GLaBIOS and answer **0 differing pixels** under
the IBM part.

### 5.1 What shipped

* `os88marty.machine(name)` resolves an IBM-romset name to its GLaBIOS twin —
  **always**, not "when the ROM is missing". A row whose machine depends on
  which files happen to be lying about is a row whose result cannot be
  compared with anybody else's. `why_ibm=<a sentence>` is the opt-out, and
  supplying one with no ROM present **raises** rather than falling back.
* `os88marty.assert_rom(m, ibm)` ends the silence E3 names: a row that means
  the IBM ROM checks the banner at 0xFE001 and fails if it did not get one.
* `tests/unit/t_machines.py`, in the **fast** tier, host-side and free: no row
  may name an IBM machine directly, every machine a test names must exist, and
  every twin must still differ from its original in `rom_set` **alone** — a
  drifted twin measures the config's difference and calls it the kernel's.

All four registered rows pass on the twins: `drvcall` 57.1 s, `fillpat` 24.5 s,
`icoclip` 37.8 s, `int0sweep` 188.6 s.

### 5.2 Two things the audit turned up on the way

* **`brclick` and `brscroll` tested the path `tools/martypc/roms/ibm5150`**,
  which is not the ROM's filename — so their IBM arm could never be taken at
  all, on any box, ever. Both carried the conditional; neither could reach it.
* **Seven of the eleven IBM machines have no twin, and none was invented
  here.** Writing four lines of TOML is cheap, but a machine nothing boots is a
  machine nothing checks. `t_machines` fails when a row names an IBM machine
  *without* a twin, so the twin gets written the day something needs it.

---

## 6. TWO FALSE GREENS FOUND WHILE MEASURING

### 6.1 `dispcp` was a LIBRARY registered as a soak row

`tests/dispcp.py` is imported by **104 files** — the most-reused thing in
`tests/` — and is 22 function definitions with no call. It was registered as a
soak row declaring 60 seconds, and reported **`ok` in 0.1 s**: it booted
nothing, asserted nothing, and could never fail.

The pass-2 soak turned up three rows that FAILED in 0.1 s
where they meant to skip, and those got investigated **because they were red**.
A green row that tests nothing is the worse half of the same shape, because
nobody investigates a pass.

It is in `t_registry`'s `UNREGISTERED` now, beside `dispcells.py`, with the
reason.

### 6.2 The general check: a row that UNDERRUNS its declaration

`os88test.py` has always reported a row that OVERRAN its declared seconds
(`SLIP`). The other direction had no report at all, and is the worse one. A
row finishing in under 5% of its declaration did not do the work, whatever its
exit code says.

It found `dispcp` and, immediately, two mis-declarations: `fillpat` and
`icoclip` were both declared at **600 s** and measure **24.5** and **37.8** —
so both carried a 2,430-second timeout and inflated the tier's declared total.
Both re-declared at 60.

---

## 7. WHAT IS NOT DONE

* **No contention failure was reproduced**, and one row failed the other way
  round. Twelve rows passed in the wide lane; the only failure in either arm
  was **`tmowner`, on the IDLE box** — `PACKAGE: no row names its cache 3740 -
  the list was never composed in full under capture`.

  Sampled six times on HEAD it is **1 fail / 5 pass**, and the fail is the
  idle one:

  | | tmowner |
  |---|---|
  | idle, width 1, before this work | ok, 270.2 s |
  | width 3 + 2 hogs | **ok**, 289.2 s |
  | idle, width 1 | **FAIL**, 270.8 s |
  | idle, four more runs alone | ok, ok, ok, ok |

  So `tmowner` is **intermittent, and it is not contention** — which inverts
  B5's classification of it exactly as B5 has already had to invert
  `dispmine`'s. B5 rested tmowner on *one* passing run at width 3 and said so
  ("one passing run is not a classification"); this is the same lesson
  arriving from the other side, and it is a row for the test-fixing pass
  rather than for this one.
* **The `guest_sleep` sweep has been taken** (§3): the harness pauses are
  guest time by default and the individual sleeps in `tests/` wait on guest
  state or spend guest time.
* **`GUEST_BUDGET_RATIO` is set from one box's measurement.** `OS88_WAITLOG`
  on the next full soak is what confirms or moves it; the widest wait seen so
  far leaves 17x of headroom, so it is not close.
* **`blitp` and `blitpair` fail, and they failed before this work** — same
  20,327 pixels, same message, at `af1f2e0`. The base-worktree recipe
  settled it in four minutes.
* **No full soak has been run behind this.** The gates that have: `make`'s
  fast tier (44/44), `os88test full` (56 passed, 0 failed, 0 skipped, 401.8 s),
  twelve rows twice in both contention arms, the four ROM rows, six rows on
  the `until`/`wait_stop` paths, and `tmowner` six times.
* **The `builds=True` lane is the floor and nothing here touched it**: 58 rows
  and 3.3 declared hours that no width helps. Roughly a quarter of them only
  build a FIXTURE rather than a knob kernel, and a prewarm phase that built
  every fixture up front would let those rejoin the shared lane. Priced, not
  taken.

---

## 8. PRIVATE BUILD TREES — `builds=True`, and why the lock is gone

> **`tools/os88build.py` is the mechanism and `tools/martylock.py` IS DELETED.**

### 8.1 The problem, in one paragraph

A row that wanted a knob kernel had exactly one place to put it: `build/`. So
it ran `make NOPLANE=1`, used the result, and ran a bare `make` in a `finally`
to put the tree back — **two full builds for one measurement**, and in between,
`build/kernel.bin` was a kernel nobody else had asked for. Any row reading the
tree in that window drives a kernel its symbol map describes perfectly and that
nobody wanted.

That is what `builds=True` protects against, and it is why 58 rows ran **one at
a time however wide the lane**: 3.3 declared hours that no parallelism improved.
It is also, less obviously, the entire reason `martylock.py` existed — two
agents in one checkout could not both work, because either might `make`.

### 8.2 The fix was already in the Makefile

`$(BUILD)` is a variable and the Makefile uses it **965 times**. `make
BUILD=<dir>` has always worked and produces a **byte-identical** image
(verified: `md5sum` of an out-of-tree `os8088-360.img` against the in-tree
one). `tools/os88sym.py` has honoured `$OS88_BUILD` and `$OS88_DEFINES` for
just as long.

Nothing had to be invented. What was missing was somewhere to put the trees and
a helper that made using one shorter than not:

```python
knob = os88build.tree("NOPLANE=1")        # builds, or reuses in 0.3 s
knob.apply()                              # point the symbol reader at it
img = knob.img("os8088-360.img")
```

A tree is **1.8 MB** and lives in `build/trees/<knob>-<hash>/`, so twenty of
them is 36 MB and `make clean` sweeps them.

### 8.3 What it guarantees, and why the lock could go

* two rows with **different** knobs never touch the same file;
* two rows with the **same** knobs share one tree, and the second waits only
  for the first's *build*, not for its run;
* **`build/` is never written by a row at all**, so a person or another agent
  may `make` in the checkout while a soak runs.

**THE ARTEFACTS ARE FROZEN AND THE SOURCE IS NOT — so nothing may change the
checkout's source while a soak runs: no merge, no pull, no edit under
`kernel/`, `apps/`, `drivers/` or `boot/`.** Two things still read the working
tree. `tools/os88sym.py` assembles `kernel.asm` from the checked-out source and
refuses a map that does not match the tree's `kernel.bin`, and every row that
builds INTO the run's tree (`make telnettest` and the like) builds it from
that source too. Soak `20260925-003439` was lost to exactly this: a merge
landed a real change to `kernel/disk.inc` twenty-four minutes in, the next
`make` a row ran rebuilt the frozen tree's `kernel.bin` beside images that
still carried the old one, and every result after it was about two kernels at
once. It was stopped and re-run from zero. Merge after the run, or soak a
separate checkout.

**The lock is `flock`, held only across the build.** That is the argument for
deleting `martylock.py` rather than reusing it: a lease was needed there
because a holder worked across many shells and no PID could answer "is the
holder alive". A build lock is taken and released inside **one process**, so
the kernel releases it however that process dies — no lease, no expiry, no
`break` command, nothing to wedge and nothing an agent has to remember.

### 8.4 Two traps found while building it, both worth keeping

* **`make -n` IS NOT A DRY RUN OF THE PARSE.** Two `$(shell ...)` assignments
  run whatever goal is asked and whatever `-n` says, and one of them —
  `$(VIDSTAMP)`'s rule — **deletes `$(BUILD)/kernel.bin`, kernel-full.bin,
  three drivers and every boot sector** when the knob set differs from the one
  that built that directory. The first draft of `defines_for()` defaulted to
  `build/`, so `os88build.py defines NOPLANE=1` duly emptied the shared tree.
  It takes a private directory now. `tools/os88fixture.py` carries the same
  warning from the other end ("DO NOT CALL IT FROM A KNOB GATE").

* **A knob's make VARIABLE and its nasm DEFINE are not the same string.**
  `VGADIRTY=1` compiles `-DVGA_DIRTY`. Hand os88sym the variable and it
  re-assembles the *plain* kernel, then refuses the map with a message about a
  stale build — which reads as "run make" and is not. So the defines are
  **derived** from `make -n` on the kernel target and no row restates them.

  The same shape one level down: **`Tree.apply()` sets os88sym's MODULE
  DEFAULT as well as the environment**, because a row threading `defines`
  through its own lookups still calls library helpers that do not take them.
  `blitcut` died inside `os88marty.no_saver` — three frames below its own code
  — resolving `ss_idle` against the module default.

### 8.5 What is converted, and what the audit found

**Nine rows so far** — `blitplane`, `blitcut`, `paintpack`, `dljunk`, `gfxlk`,
`vgadirty`, `disptitle`, `curdisk`, `dispseam` — all passing, all leaving
`build/kernel.bin` byte-identical, and verified running **at the same time**,
which the old code could never do.

**Measured, the nine together at `--marty-jobs 3`:**

| | |
|---|---:|
| sum of the nine rows' own times | **1,546 s** |
| wall clock at width 3 | **744 s** — 2.08x |
| `build/kernel.bin` after | **byte-identical** |

The 2.08x is parallelism alone and understates the change, because it does not
count the builds that no longer happen: each of these rows used to run **two**
full builds — the knob, then a bare `make` to put `build/` back — at ~37 s
each. Nine rows is roughly 660 s of build that has simply gone, and the knob
half is now cached between runs as well (a reused tree costs **0.3 s**).

That is **nine of the nineteen knob files**, and the nine include every shape
the other ten take: a two-arm A/B (`blitplane`, `blitcut`, `curdisk`,
`dispseam`), a knob-only row (`vgadirty`, `gfxlk`, `disptitle`), one that
shells out to two child scripts and must hand them an environment
(`paintpack`), and one that wants a DIFFERENT TREE PER VALUE of the knob
(`dljunk`, which builds `DLJUNK=0x61` and `DLJUNK=1` and keeps both cached).

`tests/unit/t_registry.py` now checks the flag in **both** directions, so it
comes off as rows convert rather than being dropped by hand and drifting back:
a private builder marked `builds=True` costs the soak its parallelism for
nothing, and a shared-tree builder marked `False` is the corruption the
original check exists to stop.

**The audit of all 52 builder files, which answers "do we really need this
many":**

| kind | files | what it actually does |
|---|---:|---|
| **knob kernel** | 19 | genuinely needs a different kernel — the case private trees are for |
| **fixture only** | 13 | `need()` a scratch disk. **Prewarmed** by `os88soak.py` now, so the row's own `make` finds an up-to-date tree |
| **on-demand artefact** | ~14 | `make weave.img`, `make spantest`, `make loom` — same answer: prewarm |
| **not a build at all** | ~6 | `heapmap` runs `make -n --always-make`; `minesrc` and the `TESTAPPS=` rows run **`make test`**, which launches QEMU. Their real conflict is `build/qmp.sock`, not the tree |

So the honest answer to "only a knob should need a build" is **yes, and only 19
of 52 are knobs.** The rest are an artefact that should exist before the run
(now prewarmed, which also settles B4's `muptest.img` ordering bug) or a row
whose exclusivity is about the QEMU socket and was never about `build/` at all.

### 8.6 The knob conversion is finished — and it found two broken rows

**Every knob-kernel file is converted.** The six that followed the first nine:
`knobhd`, `msegnomem`, `mseglazy`, `fatwpin`, `bootfloor` and `bmshare`.
`builds=True` is **58 → 40**.

`t_bmshare` needed no code change at all: it had been building out of tree with
its own `BUILD=` since it was written, and was marked `builds=True` anyway
**because the detector looked for the word `make` rather than for where the
output went**. It counts a `make` carrying `BUILD=` as private now, whoever
spells it — which is also what keeps `t_buildmatrix` correctly flagged, since
that one does *both*.

`t_buildmatrix` **keeps the flag deliberately** (§9.1): it still asks the shared
tree for `associco.inc`, and it runs itself at `-j4`.

**Two rows turned out to be broken, and a private tree is what exposed both.**
A tree has to NAME what it wants, and naming it is a question the shared
`build/` never asked:

* **`mseglazy` and `msegnomem` never built `mseg`.** Their own usage line says
  `make mseg && python3 tests/mseglazy.py`, `mseg` is not in `all`, and neither
  row built it — so on any tree where nobody had typed that by hand they died
  with `FileNotFoundError` on `build/mseg.o88`. That is
  the ABSENT-artefact shape exactly: an **absent** gate
  reading as a failing one. Both build it in their tree now, and `mseglazy`
  passes in 39 s.
* **`msegnomem` then produced its FIRST EVER VERDICT, and it is a failure** —
  the row's own header carries the diagnosis.

**And a third row could only ever run ONCE per checkout.** `knobhd` installs to
a hard disk in one machine and boots it in another, which needs a run tree that
survives an instance closing — `os88marty.stage_run_dir(tag)`. That tree is
keyed on the TAG, so a **second** run found the FIRST run's disk still in it,
already installed, and `instdeep` refused with:

> the disk this instance was cloned from is ALREADY installed... Something
> wrote through to [the shared master]. Restore it from its `.pristine` copy.

**That diagnosis is wrong, and expensively so**: it sends the reader to restore
a file that is pristine. Measured — the shared master's mtime was `make marty`'s
and the staged clone's was the previous run's. `stage_run_dir` clears the tree
by default now: what has to persist persists **within** a run, so an install in
one machine is there for a boot in another; across runs it is a stale disk
wearing the name of a fresh one.

A third defect came out of the same work, in a tool rather than a row:
**`os88map.Syms` hardcoded `build/`** where `os88sym` has honoured
`$OS88_BUILD` for as long as out-of-tree builds have existed. `mseglazy` built
`mseg.bin` into its own tree and then died on *"os88map: no build/mseg.bin —
build it first"*, about a file it had just made. It follows `$OS88_BUILD` now,
and **resolves lazily**: every caller spells `Syms(...)` at module scope, which
runs at IMPORT, so resolving in the constructor captured `build/` whatever the
row did afterwards.

### 8.6.1 A tree pays for its own assembly and nothing else

A plain `make BUILD=<dir> <target>` still runs gates that are not about that
tree: measured on one, **`os88ovlchk` once, `kernsize` three times and
`checkreadme` once**.

Not a new idea — `t_buildmatrix` found it and uses it for all 81 of its rows,
and the Makefile's own comment beside `NOKERNSIZE` is the argument:

> it RE-ASSEMBLES the kernel to measure it, which is the single most expensive
> thing in a knob build and is pure waste when the caller is going to discard
> the text.

`os88build` passes `NOOVLCHK=1 NOKERNSIZE=1` now. Both are gates whose answer
is a function of `kernel/` alone, so running them per tree is one answer N
times, and **neither changes a byte** — which is the property that makes them
safe and the one `t_bmshare` asserts.

Measured, one fresh tree, `NOPLANE=1`, to `os8088-360.img`:

| | |
|---|---:|
| with the gates | 24.8 s |
| without | **19.0 s** — 23.5% |
| image md5 | **identical** |

**`ICODIR=build` is deliberately NOT passed.** It shares the default build's
packages, which is right for a knob reaching only the kernel and *wrong* for
one in `$(PKGSBDEF)` — and getting that wrong means a row silently no longer
assembling the package it exists for. `t_buildmatrix` derives the exclusion
from the Makefile; a tree here does not know which knob it was handed, so it
pays for its own packages.

### 8.7 What is left

* **The fixture and on-demand rows keep `builds=True`** even though prewarming
  makes their `make` a no-op, because "no-op in practice" is not "cannot write
  the tree". Giving `os88fixture.need()` a shared-tree `flock` would let them
  join the shared lane; priced, not taken. The **seven Weave rows are 2,620 s
  of the remaining lane** and are the biggest single item left.
* **The `make test` rows want a private QEMU socket**, not a private tree —
  `msegxms`, `trkscrl`, `xmcheck`, `minesrc`. `tests/os88qemu.py` already
  supports a per-row pidfile.

### 8.8 `builds=True` was hiding a row that needed the CORES

`curdisk` passed alone and failed in a 3-wide lane. Sampled properly rather
than guessed at — B5's own lesson is that one passing run is not a
classification:

| | curdisk |
|---|---|
| width 3 + two CPU hogs | **2 FAIL / 4** |
| idle, alone | **0 FAIL / 4** |

Its assertion is `arrow MOVED during the freeze 0 times`, sampled off the
screen while a disk operation runs — so its answer is paced by host sampling
against guest time, which is exactly `alone=True`'s claim and not `builds`'s.

**And the counter-example matters as much**: `gfxlk` failed in the same wide
lane and looks identical from that one run — but sampled both ways it is
**2 FAIL / 3 idle against 1 / 3 loaded**, worse alone than loaded. It is an
intermittent control check, not a contention casualty, and `alone=True` would
have buried it (F3). One failing run in a wide lane does not tell the two
apart; only sampling the idle arm does.

**The interesting part is why it never showed before.** `builds=True` forced
the row into the one-at-a-time lane, so it had been getting core isolation as
a *side effect* of a flag that claims something else entirely. Converting the
builders is what separated the two — which is B6's distinction (*a builder
cannot share the TREE, an `alone` row cannot share the CORES*) arriving from
the other direction, and it is worth expecting more of as the remaining rows
convert.

### 8.9 A MARKER IS NOT A PRODUCT — the sweep that rebuilt every tree

`_sweep_truncated` (8.4's sibling: an interrupted `make` leaves an output file
created and empty, and an empty file is NEWER than everything it was built
from, so make reports the tree up to date and the next consumer fails
somewhere else entirely) deleted **every zero-length file** at the top of a
tree, on the rule that *"a zero-length product is never legitimate here —
every rule in the Makefile writes bytes."*

That sentence is true of products and false of **markers**, and the Makefile
has nineteen of them. Every knob stamp is created with a bare `touch` and is
therefore exactly zero bytes — `$(VIDSTAMP)` above all, whose whole job is to
answer *"was this directory built with these knobs?"*. So every `tree()` call
swept the stamp, and the `make` that followed read its absence as **the knob
set has changed** and did what 8.4 describes: deleted `kernel.bin`,
`kernel-full.bin`, `kernel.sys`, both boot sectors and six drivers, then built
the lot again.

**Two costs, and the second is the one that took rows down.**

The first is that the reuse this section advertises — *"a second call with the
same knobs re-runs make over an up-to-date tree and returns in under a
second"* — **never happened once**. Measured on `diskcnt-62f860de`, a warm
`tree()`:

| | a second `tree()` over an up-to-date tree |
|---|---|
| sweeping the marker | **19.9 s** — a whole kernel, again |
| sparing it | **0.4 s**, and not one file in the tree written |

The second is a **race**, and it is why this is in §8 rather than in a
changelog. The lock serialises two `make`s and **cannot** serialise a `make`
against the first row's *reader*: row A builds, releases, boots, and starts
resolving symbols against `<tree>/kernel.bin` while row B — which was blocked
on the lock — deletes and rewrites that very file. Four pairs share a tree:

| tree | rows |
|---|---|
| `diskcnt` | `mseglazy`, `msegnomem` |
| `noplane` | `blitplane`, `paintpack` |
| `small` | `small128`, `smallboot` |
| `fatwnone` / `vgadirty` | `fatwpin`, `vgadirty`, `t_registry` |

That is **msegnomem's soak failure twice and paintpack's once**, every one of
them passing when run alone — and os88sym's *"the file was written 1.8 s
ago"* (12) is what named it, having been written for exactly this and never
yet believed. Reproduced deliberately: the two `diskcnt` rows run cold and
concurrent failed **2 times in 2**, and pass **cold and concurrent** with the
marker spared. All eight rows above were then re-run the same way and pass.

The fix is one condition — the test is what the file **is**, not how big it
is — and the Makefile spells a marker two ways, both of which are now spared:
`$(BUILD)/.<name>` for the sixteen knob stamps and `$(BUILD)/<name>.stamp` for
the three fetch stamps. `tests/unit/t_treesweep.py` is the ratchet and it
reads the **Makefile**, not a list of its own: every `touch`ed target is
scraped out and asserted spared, so a marker named a third way fails in a
twentieth of a second rather than in a soak row three hours in. Reverted on
purpose, it goes red on all nineteen.

### 8.10 `os88map` followed the RUN's tree, never the row's own

The same run turned up a second cross-tree read, and it is 8.4's shape one
variable along. `tools/os88map.py` resolves `build/mseg.bin` through
`os88build.at()`, which follows **`$OS88_TREE`** — deliberately, because
`$OS88_BUILD` names sub-directories (`build/smallk`) that hold a kernel and no
packages at all. Correct for a frozen run, and blind to a row that has just
built the package into a **private** tree: `mseglazy` booted its own tree's
`MSEG.O88` and resolved its symbols against the shared `build/mseg.bin`, a day
stale and genuinely different, then reported *"this map describes a DIFFERENT
build"* about a file it had made correctly one directory along.

So there is a third claim, and it needed a home of its own:

| | what it answers | who owns it |
|---|---|---|
| `$OS88_TREE` | where the RUN's artefacts live | the runner (14.2) |
| `$OS88_BUILD` | which KERNEL a symbol map describes | the row, per kernel |
| `os88build._LOCAL` | where **this process's** artefacts live | `Tree.apply()` |

It is a module global and **not** an environment variable, which is the whole
point: `$OS88_TREE` reaches every sub-make a row spawns — the bug behind
`at()`'s `build/trees/` guard — and a process-local answer reaches none of
them. `plain().apply()` restores it for free, because `plain()`'s own
directory *is* `at("build")`.

**And it is EXISTENCE-CHECKED where `$OS88_TREE` is not.** The asymmetry is
the point, and 8.11 is why it had to be there before this shipped. A run's
tree is the WHOLE build and the only directory a soak may read, so a miss must
say so where it happened — falling back to `build/` would half-run a soak
against the directory the tree exists to avoid. A private tree is a **subset**,
cut to the `targets` one row asked for: `tree(targets=("small",))` holds a
kern_small and no 360KB pair at all. Redirecting every `build/...` string into
it unconditionally would point 8.11's thirty-four sites at files it was never
asked to build, an hour into a soak, as a `FileNotFoundError` naming a private
tree — which is the exact shape of the bug those sites were converted to fix.
So the private tree answers for what it HAS and everything else falls through
to the run's, which still never falls back to `build/`.

### 8.11 The two fixes that arrived from the other side, and how they compose

Two commits landed on `elendilon` from a parallel session while 8.9 and 8.10
were being written, and both belong here because a reader hitting one of these
mechanisms should find all four in one place.

**`89d0e5b` — the reader keeps a SHARED hold.** The same soak failure, reached
from the symptom: `_Lock` was exclusive across the build and dropped after it,
on the claim that the second row's `make` is a no-op in a finished tree. It
downgrades to `LOCK_SH` and holds that for the reading process's life instead,
so a row that wants to BUILD a tree waits for the rows READING it and two
readers never wait for each other.

**That is the belt and 8.9 is the braces, and it is worth being exact about
which does what.** 8.9 is the cause: the second `make` was *never* a no-op,
because the sweep ate `$(VIDSTAMP)` and make read its absence as a changed
knob set. With the marker spared, a second `tree()` writes nothing at all
(0.4 s, measured) and there is nothing for a hold to protect against — on a
tree that is current. On one that is genuinely stale, which a frozen run
cannot produce and an interactive session can, the hold is still what stops
the rewrite. Neither makes the other redundant, and the cost of keeping both
is that a row sharing a tree waits for its partner's whole run rather than its
build.

**`a30b6d9` — a row that opens a build artefact must resolve it.** Ten rows
failed a 368-row soak on a missing artefact and nine of the ten already
declared a `wants=`, so the fix everyone reached for was the one they had. The
bug is one layer along: `at()` was called at the few places a path is USED —
`launch` staging floppies, `scratch_disk`, `os88sym` — and a row that opens an
artefact ITSELF is a use site none of those cover. With `$OS88_TREE` unset
`at()` is the identity function, so it works perfectly by hand and fails only
inside a soak, several frames from the cause, as `FileNotFoundError:
'build/pinme.o88'`. **thirty-four call sites, across twenty-four files**, go through `at()` now, in four
shapes of which only the first is a literal a scanner can see (a plain
`open("build/x")`, an argparse DEFAULT the row later probes, an
`os.path.join(ROOT, "build", ...)` carrying no `build/x` string at all, and a
`TESTAPPS=` handed to `make test` after `BUILD=` had already been redirected).

Those thirty-four sites are why 8.10's redirect is existence-checked rather
than unconditional.

---

## 9. THE PRE-MERGE GATE — 402 s to 227 s, and no flag to remember

`make test-full` ran its emulator lane at `--marty-jobs 1`, so the tier that is
*almost entirely emulator rows* used one core of four. **402 s → 226.8 s**,
measured twice, with `build/kernel.bin` and `build/hddtool.drv` byte-identical
either side.

Two changes, and the first is the one that mattered.

### 9.1 Three of the gate's six builders stopped owning the tree

`buildmatrix`, `smallboot`, `small128`, `weavesmoke` — the four biggest rows —
were `builds=True`, which forces the one-at-a-time lane whatever the width.
Each is now a private tree:

* **`t_buildmatrix`** already built its 81 knob kernels out of tree in
  `build/bm-<name>/`; what dirtied `build/` was its **`make small`**. Its own
  comment says why that is not innocent: *"it left `build/hddtool.drv`
  disagreeing with the copy already on the shipped images, and a later plain
  `make` did not put it back because the file was newer than its sources"* — so
  the row ran a **whole second `make -j` afterwards** to restore the tree.
  `make BUILD=<dir> small` writes none of it (verified, `hddtool.drv`
  included), so the restore build is deleted.
* **`smallboot` and `small128` share one tree** — same targets, so the second
  to run finds it built. small128 went to 11 s.
* **`weavesmoke`** builds `weave360.img` and the system floppy into a tree of
  its own.

`t_buildmatrix` **keeps** `builds=True` and correctly: it still asks the shared
tree for `build/associco.inc`, and it runs itself at `-j4`, so it wants the box
rather than a share of it. `t_registry` was tightened to allow exactly that —
the "must be `builds=False`" rule fires only on a row that builds **privately
and not also in the shared tree**.

### 9.2 The default width is CORES-1 now, not 1

The old default's reasoning was arithmetic, not caution: guest cycle counts are
exact at any width, but `settle`, `until` and a row's timeout were **host**
seconds, so widening spent slack some rows had not got. §2 removed that — the
waits are guest-denominated — so the default follows.

**This is the gate's width and it stays CORES-1 while the soak's fills the
box** (§4.2). The difference is not about the rows, which are the same rows:
somebody is at the keyboard for a five-minute gate and nobody is for a
two-hour detached run.

### 9.3 Two Makefile facts found on the way

Both are about `$(BUILD)` not being as parameterised as it looks:

* **An out-of-tree build must use a RELATIVE `BUILD=`.** The C toolchain's
  path is spelled `$(CURDIR)/$(CC_SC)` where `CC_SC := $(BUILD)/cc/SmallerC`,
  so an absolute `BUILD` produces `$(CURDIR)//tmp/...` and every C package
  fails with `smlrpp` not found — a message about a missing compiler, on a tree
  that has one.
* **A private tree must not rebuild the pinned instruments.** `build/cc` and
  `build/martypc` are fetched at pinned commits and `make clean` spares both,
  so `os88build` symlinks them into each tree. Nothing else may be shared: a
  shared writable *disk* is what contaminated pass 2's first bisect.

### 9.4 What the gate's floor is now

`buildmatrix` ~100 s alone (it saturates four cores by itself) plus ~127 s of
everything else at width 3. Pushing further means making `buildmatrix` cheaper,
not wider — it is already `-j4` internally.

---

## 10. BISECTING — `tools/os88bisect.py`, and the three errors it makes impossible

```sh
python3 tools/os88bisect.py classify <row>                    # the protocol
python3 tools/os88bisect.py sample <row> --at HEAD --at <ref>
python3 tools/os88bisect.py search <row> --good <ref> --bad <ref>
python3 tools/os88bisect.py clean
```

The pass-3 soak ran a bisect that was published-adjacent and
**wrong**: it named a commit whose entire diff to shipped code is four comment
lines. Three errors stacked, each cheap to repeat by hand:

### 10.1 N=1 read as a rate

Six points, one run each, `ok/ok/ok/ok/fail/fail` — which reads as a clean
bisect and was a coin landing in an order. The row failed **10 times in 15
everywhere**.

Every point is sampled `-n` times (default 3) and the answer is a **rate**. A
point that is neither 0/N nor N/N is `INTERMITTENT` and is **never** a side —
`search` stops rather than picking one, because a bisect step over a rate is
luck and carrying on does not recover from it.

Demonstrated on `gfxlk`: `INTERMITTENT 1/4 failed`, with the leg named.

### 10.2 A row's exit code read as one verdict

`dispsize` has six independent legs; the run that showed **0 differing pixels
on leg C** still exited 1 on leg E, so it was filed as a failure and the flake
stayed invisible for four more runs.

The row's own output is parsed into **legs**, rated separately. That needed
**four** spellings, counted rather than guessed at — the first version handled
one and a half and returned *nothing* for `gfxlk` and `msegnomem`, which is the
same silence one layer up:

| form | example | files |
|---|---|---:|
| `FAIL: <what>` | `harness.py`'s `done()` | 72 |
| `<row>: FAIL: <leg>: …` | `dispsize`, legs are letters | — |
| `  [FAIL] <name>` + a `FAILED: a, b` summary | `gfxlk` | — |
| `<row>: FAIL` then an **indented list** | `msegnomem` | 22 |

`dispsize`'s form yields `['C', 'E']` — precisely what E1 needed and did not
have. A pass yields none, and so does a bare traceback; the caller tells those
apart by the exit code.

### 10.3 The points did not share a base

Four of the six forked from a different commit and carried the pre-pass kernel,
so they were never comparable with the other two. Every command checks
**ancestry first**: the points must form one line — every point an ancestor of
the newest — or they are siblings on different branches and what is being
measured is the branch. It also refuses outright on a **shallow clone**, where
git answers ancestry confidently and wrongly (CLAUDE.md rule 1).

### 10.4 Two things it says that a person would not

* **Step 1 runs alone, and step 2 only if step 1 fails.** The protocol's own
  economy — two of pass 2's fifteen were settled by step 1 — so a row that
  passes at HEAD never builds the base at all. Measured: 35 s to answer
  `bootsmoke`.
* **Both points failing is not automatically "pre-existing".** If the legs
  DIFFER, the shared ones are pre-existing and anything only at HEAD is a
  regression worth `search`ing on its own. And if one side has legs and the
  other has none, that side **never got as far as an assertion** — a row that
  cannot RUN at a point has not been measured there. `msegnomem` is the worked
  example: it asserts at HEAD and dies on a missing fixture at the base, and
  the script refuses to call that pre-existing.

### 10.5 What it shares between worktrees

Each point is a detached `git worktree` with its own `build/`, so two points
never write one file. `build/martypc` and `build/cc` are symlinked in — pinned
upstream artefacts, identical by construction. **Nothing else**: a shared
writable disk is what contaminated pass 2's first bisect.

A worktree is ~60 MB and they are kept between runs (`clean` removes them),
because the build is most of the cost of a point.

---

## 11. MARGIN — where the suite's time actually goes, and what came off

`OS88_WAITLOG=<file>` records every wait, and it records **two kinds** — which
is what makes it worth reading. `settle` and `until` are waits **for**
something and end the moment it happens; a fixed sleep after a click is
**unconditional** — the wait *is* the cost, every time, whether or not the
guest needed it. A profile that records only the conditional half accounts for
the part of a row that is already efficient and is silent about the part that
is pure margin. `os88mouse._wait` therefore logs too, as `fixed`, and what it
records is what the wait **bought**: guest seconds across the sleep, because
"1.5 host seconds bought 5.2 guest seconds of a machine that needed 0.4" is an
argument and "1.5 seconds" is not.

### 11.1 The baseline: 57% of a row is waiting, and 48% is one function

Four rows — `assocopen`, `fmcommit`, `dispnp`, `alertbtn` — 269.2 s of row
time between them:

| | calls | host | guest |
|---|---|---|---|
| `settle` | 43 | **130.0 s** | 420.7 s |
| fixed (click/dblclick/drag) | 19 | 19.0 s | 59.1 s |
| `until` | 7 | 3.2 s | 9.0 s |
| | | **152.2 s of 269.2 = 57%** | |

`settle` alone is **48% of everything**, at 3.0 s a call. And most of that is
not the screen changing.

### 11.2 `settle`'s floor is 2.0 s and it cannot be tuned away

`stable` identical captures `quiet` apart is the signal, so a settle that
returns has, by construction, spent `stable × quiet` = **2.0 host seconds**
proving stillness — plus three framebuffer captures. Measured average 3.0 s,
so **two-thirds of every settle is that floor**.

The obvious cut is `stable=1`, halving it. **The log says no.** A `gap` row is
written whenever a settle sees the screen change, carrying how much stillness
that change *interrupted* — which is exactly the number that decides `stable`.
Over 48 settles:

| a change arrived after | count |
|---|---|
| `run=0` (the screen was changing continuously) | 18 |
| `run=1` (one whole `quiet` of stillness, then a change) | **1** |

So `stable=2` earns its keep **1 time in 19**, and dropping it would end one
settle mid-repaint per 48. That is not zero, and 1-in-48 is precisely the shape
that costs a soak run. Finer granularity does not help either: the same window
with a smaller `quiet` needs proportionally more captures.

**The floor is real, so the only way to spend less is not to settle** — which
is what `tools/os88ui.py` is for, and this is the measurement that made it
worth building.

**...and later it came down anyway, by asking the machine instead of the
screen.** The floor is real for the case the gap log measured - a screen that
goes still *while something is still working* - and that case is visible
from inside: ui_task is not asleep, the gfx lock is held, an event is queued
or the drive is reading. `os88marty.ui_idle` reads those five facts, and
`settle` keeps its full `quiet` window for any interval where one of them is
true and uses 0.2 guest seconds where none is, a capture only counting as the
same if the UI was still idle and the drive still unmoved at its end. On
`dispcheck` the settles after a gesture fell from 9+ guest seconds each to
0.8-0.9, while a display-mode change kept its full 5.5 because the UI was
repainting through all of it. The mouse verbs' fixed pauses went the same
way (`ui_done`, capped at the old pause). `OS88_SETTLE_UI=0` is the A/B.

### 11.3 What came off, in four steps

Same four rows, same machine, all passing at every step:

| | wall | `settle` | fixed |
|---|---|---|---|
| baseline | 269.2 s | 43 / 130.0 s | 19 / 19.0 s |
| lazy screen bands + a confirming `to()` | 244.8 s | 43 / 125.8 s | 19 / 19.0 s |
| the trailing settle out of the navigation verbs | 220.1 s | 32 / 98.5 s | 19 / 19.1 s |
| the Control Panel verbs confirmed | **206.6 s** | 28 / 94.3 s | 19 / **9.6 s** |

**23%**, and two rows much more than that: `dispnp` 73.8 → 41.0 s and
`dispthm` 81.2 → 51.1 s.

1. **`_Screen`'s three bands are lazy.** Only the boot *gate* reads
   `field`/`rule`/`dock`; a plain `settle(m)` has no gate and was computing
   them anyway — on VGA a Python generator over 640×480 with index arithmetic
   per pixel, three times a settle, answer discarded. The comparison a settle
   actually makes is `bytes == bytes`.
2. **`Mouse.to` confirms each packet instead of sleeping after it.** It slept a
   fixed **0.25 host seconds** per packet — about 0.9 guest seconds on an idle
   box here and 0.25 on a full one, so the same click bought the machine four
   times less work under load. It reads the pointer back now (`_landed`),
   which on a quiet guest answers in a few milliseconds; the 1.0 **guest**
   second is only the deadline, and reaching it means the packet was dropped,
   which `to`'s loop already handles by sending another. `to` sends two or
   three packets per click, so this was most of the cost of every click in the
   suite.
3. **The trailing settle is out of `open_drive` / `open_named` / `scroll_to`,
   behind `paint=True`.** It was kept for one commit on the grounds that a
   caller might be about to compare pixels. It can be counted, and the count
   settles it: of the **401 call sites** in `tests/`, **2** read the
   framebuffer within six lines — both in `tmrepair.py`, and both already
   behind a `raise_win` that settles anyway.
4. **The Control Panel verbs confirm.** `open_panel` waits for the window and
   then for `[cp_sel]`; `set_mode` for `[vid_dmode]`/`[vid_dlay]`;
   `close_panel` for the window to go and then for `[cp_wdirty]` to clear —
   which is **the SYSTEM.CFG write landing**, where the old spelling was
   `time.sleep(1.0)` against an operation measured in guest time, so on a busy
   box it returned mid-write and a persistence test read a file that was not
   there yet.

### 11.4 Two things the confirmations caught immediately

Both were found by the checks failing loudly rather than by anything going
wrong, and both had been silent:

- **`[cp_vsel]` holds the VID_* KIND, not the row.** `cp_vid_slot` compacts the
  adapter rows over `[vid_avail]`, so row 0 on a Hercules+CGA machine is
  `VID_HERC` = 1. A confirmation comparing the two called a correct slot-0
  click a miss. `dispcp.adapter_kind` is the faithful inverse of
  `adapter_row` — `cp_vid_rowok` is exactly `vid_avail_test`, a plain bit
  test, which is what makes a mirror possible at all.
- **Set Primary's button is legitimately a no-op** when the dot is already on
  the running adapter (`cpf_vidok` greys it), so a missed click and a correct
  no-op are indistinguishable from outside. It is deliberately *not* confirmed;
  the ROW click is the half that can be checked, and it is the half that was
  silently going missing.

### 11.5 What is left

`settle` is still **46%** of what remains, and every one of those calls is a
test's own. Cutting them is per-test work — replacing "click, then wait for the
screen to go quiet" with `os88ui`'s "click, then read the thing that says it
worked" — and the same 0.35× applies wherever it is done (§ *Start at
`tools/os88ui.py`* in docs/MARTYPC-DEBUG.md). The declared `secs` in
`tests/suite.py` want re-deriving from a full soak afterwards, in both
directions: `knobhd` declares 900 s against 153 measured, and `wmartifact`
declared 60 against **218**.

### 11.6 `quiesce` — settle on the BYTES a test is about to read

Adding the call site to `OS88_WAITLOG` turned the totals into a work list, and
the list has a very sharp head. Over ten rows:

| site | calls | host |
|---|---|---|
| `dispcalc.py:280` (`typed`'s settle) | 60 | **137.9 s** |
| `dispcalc.py:135` | 9 | 31.4 s |
| `dispcalc.py:213` (`menu_pick`'s settle) | 12 | 26.2 s |
| `dispcalc.py:212` (`mo.menu`'s fixed 2.0 s) | 12 | 24.0 s |
| everything else | | |
| | | **settle total 480.4 s** |

One line was **30% of all settle time in the sample**. And what follows it is
`shown(m, seg, cal)` — thirteen bytes of the Calculator's own composition
buffer. The screen was never the question.

`os88marty.quiesce(m, read, guest=0.5, stable=2)` is `settle`'s shape applied
to those bytes: the same signal — `stable` identical readings a fixed interval
apart — over a handful of bytes instead of a framebuffer, and over **GUEST**
seconds instead of host ones. It is an order of magnitude cheaper *and* a
stricter statement, because a screen settle can be satisfied by a screen that
happens to be still and defeated by an animation elsewhere on it, while a
settle on the buffer a test is about to read can be neither.

Measured on `dispcalc`, the largest row in the suite:

| | wall |
|---|---|
| before | 376.3 s |
| `typed`/`ctrl` quiesce on the display buffer (60 settles → 68 quiesces, 137.9 s → **24.1 s**) | 235.4 s |
| `menu_pick` through `os88ui` (which proves `menu_sel` before releasing) | **197.2 s** |

**48%**, and the row still passes every assertion. Two things made it safe:

- the watcher covers **everything the commands move**, not just the number
  field. `menu_pick` folds the history pane, which does not touch `nbuf` at
  all — a watcher reading only `shown` would answer "already still" and wait
  for nothing. `[cal_open]` and `[cal_nvis]` are in it for that reason.
- the nine `repaint_diff` sites still use `settle`, and must: a pixel
  comparison is exactly the case where the SCREEN has to be still.

**Where else this applies:** 56 settles in 34 files are immediately followed by
a guest-memory read and no framebuffer read at all — `dispclose` 5, `rdmove` 5,
`dispcalcx` 3, `fmbtn` 3, `telnet` 3. Each is worth about two seconds, and each
needs one line saying what to watch.

---

## 12. THE FIVE-MINUTE WINDOW IN WHICH EVERY EMULATOR ROW FAILS

Found by the first full soak after §11's work, and it is not a row's bug — it
is the run's own, it fires once per run, and the preflight designed to catch it
is **structurally blind to it**.

**The mechanism.** The About box's build number is the commit count (§14.2) and
it reaches the kernel through a *generated* include:

```make
BUILDINC := $(BUILD)/buildnum.inc
BUILDNUM := $(shell python3 tools/buildnum.py -o $(BUILDINC))
```

That `$(shell)` runs at make **parse** time. So:

1. You commit. `build/buildnum.inc` and `build/kernel.bin` are both stale, and
   **they are stale together** — `os88sym` re-assembles `kernel.asm` with
   `-I build/`, picks up the old number, and gets bytes identical to the old
   `kernel.bin`. Every check passes. `os88soak check` says `ok kernel map`.
2. The first `make` of the run — *any* target, from any row, even one that
   builds nothing — rewrites `buildnum.inc` in milliseconds.
3. `build/kernel.bin` is relinked seconds or **minutes** later.

Between 2 and 3 every symbol read in the tree re-assembles with the new number
and compares it against a kernel built with the old one. Every emulator row in
flight dies on its first `S("…")` with *"the map describes a DIFFERENT kernel
from build/kernel.bin"* — which points at the kernel, and reads as nine
unrelated features breaking at once.

**Measured on the run that found it:** `buildnum.inc` rewritten at 14:59:14,
`kernel.bin` at 15:04:08 — a **4 m 54 s window**, opened 23 seconds after the
soak started. Nine rows fell into it: `weavesession`, `weavegrid`, `weavegfx`,
`weaveprev`, `weaveone`, `weavepack`, `weavelat`, `c64part`, `fcpsmall`. Every
one of them is a false failure and every one of them passes on a re-run.

**Two fixes, and they are different questions.**

- `_buildnum_current()` compares `build/buildnum.inc`'s `BUILD_NUM` against
  `git rev-list --count HEAD`. The re-assembly check cannot ask this — both of
  its inputs move together — so it is a separate predicate, and it is the one
  that actually catches "you committed and did not `make`".
- `prewarm()` runs a plain `make` **first, always**, before the first row
  exists. That closes the window rather than reporting it, and costs nothing
  when the tree is already current.

**And it makes a rule for the operator: DO NOT COMMIT WHILE A SOAK IS
RUNNING.** A commit moves the count, which makes `build/buildnum.inc` stale
against the tree — and the next `make` any row runs then re-opens the window
under the rows in flight. The fixes above close it at the *start* of a run;
nothing can close it against a commit landing in the middle of one. Land the
work before `start`, or after `finished`.

**The lesson generalises past this file.** A generated include written at make
*parse* time is a shared mutable input to every symbol lookup in the tree, and
nothing about a `$(shell)` announces that. `os88mini.py`, `os88font.py` and
`buildnum.py` all write one; only `buildnum.py`'s changes on a commit, which is
why only this one has ever bitten.


---

## 13. THE DECLARATIONS, RE-DERIVED FROM A FULL RUN

267 rows, **244 ok / 23 fail / 0 skip in 1:52:03** — and the declarations were
**10.2 hours against 3.6 measured**, which is not a safety margin, it is a
number nobody had checked since it was guessed.

`secs` is not a timeout (that is `timeout=`); it is what the row is expected to
cost, and the runner already gives it margin — `SLIP = 2.0` flags a row taking
more than twice its declaration, `UNDER = 0.05` flags one using less than a
twentieth. Declaring 4x the truth disables the first of those; declaring half
the truth makes it cry wolf. Both were happening:

| over-declared | | under-declared | |
|---|---|---|---|
| `weavepack` | 1500 → 225 measured | `dispfrac` | 60 → 149 measured |
| `knobhd` | 900 → 136 | `dispmcfs` | 60 → 132 |
| `msegnomem` | 600 → 40 | `trackmove` | 60 → 125 |
| `frpromise` | 600 → 129 | `rdmove` | 60 → 128 |
| `dmgcull` | 420 → 41 | `weavefuzz` | 75 → 138 |

161 rows re-declared at **1.25 × measured**, rounded to something that reads as
a decision (10 s / 30 s / 60 s steps). That leaves `SLIP` firing at 2.5x
measured — a real drift alarm rather than a formality. Declared total
**10.2 h → 6.0 h** against 3.6 measured; the gap that remains is the 23
failures, which keep their old declarations.

**A FAILING ROW'S TIME IS TIME-TO-FAILURE** and says nothing about what the row
costs when it works. Declaring off one would give a row that dies in four
seconds a four-second declaration, and it would then trip `SLIP` the day it is
fixed — so the 23 are excluded and re-declared when they pass.

Three rows the sweep could not reach — `fcpsmall`, `fdlgsmall`, `stkbalance` —
spell their command across several lines with commas in it, which no
single-line pattern can span. Two of the three are in the failing set anyway.

## 14. `builds=True`, AND WHY IT WAS THE SERIAL LANE'S WHOLE COST

**BUILT. 41 rows carried the flag and 3 do.** What follows is the design as it
was written, then §14.1 on what it cost that the design did not foresee, and
§14.2 on the question it does not answer.

The 2026-09-04 run is 267 rows in 1:51:52, and its first hour is one row at a
time. `builds=True` keeps a row out of the shareable emulator lane whatever
`--marty-jobs` says (§8), 40 rows carry it, and the run's own status line says
what that costs: `in flight 1 emulator(s)` for the first 55 minutes, then
`in flight 3` for the rest, over the same four cores.

§8's audit says 19 of the 52 were knobs and 27 an artefact that should just
exist before the run. `Row(wants=...)` and `os88test.prebuild` now build the
artefacts up front, so the second half of that is half done — nine rows
declare, the runner builds them all before any row starts, and an artefact
that *cannot* be built costs only the rows that named it rather than the run
(measured: one missing Inform took a five-hour soak down at 37 minutes with 0
of 267 reported).

**What is left is three edits and not forty**, because the flag is not where
it looks:

1. **`tools/os88fixture.need()` always runs `make`**, even for a target that
   is already current. After `prebuild`, every `need()` in a run is asking for
   something that exists. Skip a target `make -q` says is up to date and the
   call stops writing the shared tree; a row run standalone still builds its
   own.
2. **Each such row declares `wants=`** for exactly the targets it needs. That
   is the machine-readable version of the claim its script already makes.
3. **`tests/unit/t_registry.py` learns the pair.** Its `_invokes_make` keys on
   the IMPORT of `os88fixture` — deliberately, and that is why a dozen rows
   carry `builds=True` with no `make` anywhere in them. A row that imports it
   MAY be `builds=False` when it declares `wants=` covering every target it
   passes to `need()`, which is readable from the script the same way the
   import is.

The rows sort into five kinds, and only the first is a plain conversion:

| kind | rows |
|---|---|
| a plain artefact build | `weavesession` `weavegrid` `weavegfx` `weaveprev` `weaveone` `weavepack` `weavelat` `spantest` `spantest-vga` `fcpcopy` `fcpsmall` `fdlggrey` `fdlgsmall` `paintmove` `trackmove` |
| NOT a build — `make test` is a QEMU BOOT | `msegxms` `xmcheck` `minesrc` `trkscrl` |
| NOT a build — `make -n`, and §8 records that this is DESTRUCTIVE with a knob in it | `bootstatus` `heapmap` |
| genuinely builds; keep the flag | `buildmatrix` `ctoolchain` |
| `builds=True` with no `make` in the script at all — the `os88fixture` import | `c64part` `heapcheck` `drvcall` `fdlgdrop` `fdlgup` `fdlgthumb` `pkgthumb-*` `fsxdisp` `mouseup` `sbar` `trkrate` `trktxsurf` |

`fdlgthumb` is the one already excused in `t_registry`'s
`BUILDS_WITHOUT_MAKE`, and its reason — a knob gate that may not call the
fixture helper at all, because `$(VIDSTAMP)`'s rule would delete the kernel it
is about to test — is the shape of the exception the other twelve need in the
opposite direction.

### 14.1 What it took, and the two things the design missed

The three edits were the three edits. Two more were needed and neither was
visible from the plan.

**`prebuild` has to ask make for EVERY declared artefact, not the absent
ones.** It skipped anything that already existed - safe only while each row
still ran `make <art>` for itself, which is exactly what this change removes.
The moment the rows stopped building their own, a stale artefact stopped being
refreshed by anything at all: `build/c64.bin` survived a cherry-pick from an
earlier tree and `c64part` failed with *"the re-assembly of apps/c64/c64.asm
is not byte-identical"*, which reads as a broken package and is a file nobody
rebuilt. `paintmove` and `trackmove` went the same way. **An existing file
says nothing about whether it is current; make is the dependency graph and the
whole point of asking it.** They go in ONE make now, because the parse is most
of the cost of an up-to-date target and paying it thirty times to be told
thirty times that nothing needs doing is a fixed cost a soak notices; a
failure re-runs them singly, because "one of these thirty did not build" is
not a usable message.

**`make -n` and `make test` write to `build/` too**, and neither is a build.
`BUILDNUM` is a `:=` shell assignment, so `tools/buildnum.py` rewrites
`build/buildnum.inc` at PARSE time on every make whatever the goal - and `-n`
is not a dry run of the parse. So a row that only lifts a recipe out
(`heapmap`, `bootstatus`) and a row that only launches an emulator
(`msegxms`, `xmcheck`, `minesrc`, `trkscrl`) both change a file under `build/`
while a soak is reading that directory. `os88fixture.make()` is that one
`make` with the stamp put back - `need()`'s own restore, made shared because
the next one will forget - and those six rows go through it.

**The enforcement point is `need()` and not a static check**, which is the
part worth keeping. Whether a `wants=` is COMPLETE cannot be settled by
reading a script: `need(DISK)` and `need(a.apps)` are as common here as a
literal path. So under the runner an undeclared target is an ERROR at the call
rather than a build - the row that owns the wrong declaration fails, by name,
instead of the run beside it. `tests/unit/t_registry.py` therefore accepts
`makes and wants and not builds` without trying to verify it.

`tests/unit/t_qemuown.py` caught the one thing that would have gone quiet: its
launcher detector keys on the argv list, so moving four rows to
`os88fixture.make` dropped its count 14 -> 10 and its own liveness check fired
within the minute. A launcher that file cannot see is a launcher nothing
checks owns its instance.

**What it bought**, measured on this box:

| rows | declared | wall |
|---|---|---|
| `sbar` `fdlgup` `drvcall` `spantest` `heapcheck` | 250 s | **79.1 s** |
| fifteen, incl. `trkrate` `trktxsurf` `editmove` `pkgthumb-*` | 1,370 s | **389.0 s** |

All of them were one-at-a-time before.

**The three that keep the flag**, and each is honest: `buildmatrix` builds 81
knob kernels and runs itself at `-j4`, so it wants the box rather than a share
of it; `ctoolchain` builds the C toolchain; `fdlgthumb` writes its fixture
with nasm and os88pkg directly, and its `BUILDS_WITHOUT_MAKE` entry says why
it may not call `need()` at all.

### 14.2 An agent CAN work while a soak runs — the run reads a tree of its own

**BUILT.** `os88soak.py start` builds a tree before the first row and exports
`$OS88_BUILD` to the run; `build/` is then the operator's for the duration.
`--shared-build` opts out and says what it costs.

**The blocker was never the mechanism, it was 725 literal `build/...` strings
across 256 files** — and 356 of those are the two shipped images. Editing the
callers was never going to happen, so nothing does: the path is resolved where
it is USED, and there turn out to be three places.

  * `os88marty.launch` stages both floppies in one loop — 85 call sites, one
    line;
  * `os88marty.scratch_disk` reads its inputs in another, and a scratch disk
    built out of the shared tree's bytes would defeat the whole thing;
  * `os88sym` has honoured `$OS88_BUILD` since it was written.

`tools/os88build.at()` is the resolver, and with the variable unset it is the
identity function — so every interactive run and every `python3 tests/x.py`
behaves exactly as before.

**The A/B, because "it should be safe now" is not a measurement.** A loop
alternating `make VIDEO=cga` and `make` in the shared tree — the documented
worst case, a knob build landing in `build/` — run against the same three rows
twice:

| | `sbar` `mouseup` `fdlgup` |
|---|---|
| reading `build/`, hammer running | **0 of 3**, all *"the map describes a DIFFERENT kernel"* |
| reading the frozen tree, same hammer | **3 of 3** |

The first arm is the point: it is not a hypothetical hazard, it is a
reproducible one, and it takes about twenty seconds to produce.

**A hammer that only rebuilds is not the experiment**, and the first attempt
was exactly that: `touch kernel/kernel.asm; make` in a loop passed 3 of 3 on
the shared tree, because an unchanged source rebuilds byte-identically and the
map still matched. What breaks a run is the tree becoming a DIFFERENT
build — a knob, a `make small`, a half-finished write — not a busy one.

**Two things the freeze then made wrong until they followed it.** `prebuild`
was still running `make` in the shared tree, which is the one thing the freeze
exists to make unnecessary — under the hammer it duly printed *"`make` failed
before the declared artefacts"* while every row went on to pass. With a tree it
CONFIRMS instead: the tree is built from the same union of declarations, so
the artefacts are there by construction. And `os88fixture.make` passes
`BUILD=<tree>`, or the ten QEMU rows would boot the shared images through
`make test` while everything around them read the tree — the one arrangement
worse than either. Their socket and pidfile stay under `build/` because the
Makefile spells them literally, which is harmless: no `make` writes those.

**A COMMIT is a different build too, and the freeze did not cover it until
2026-09-27.** The kernel carries the commit count (SPEC.md 14.2), and
`tools/buildnum.py` answered for the LIVE checkout's HEAD. A row that builds a
declared artefact runs `make` in the tree hours after the freeze - so the
first commit made while the whole soak ran put the tree's `kernel.bin` one
build ahead of the images already in it, and `dispcold`, which compares the
running kernel's `.cold` with the file's, reported one byte of damage at
`mod_check+34`: the immediate of `cmp word [es:MOD_H_BUILD], BUILD_NUM`, 0xA7
in memory against 0xA9 on disk. `os88soak.py start` now exports
`$OS88_BUILDNUM`, the count it froze at, and `buildnum.py` honours it. **A
SOURCE edit is still not frozen**: a row that rebuilds an artefact inside the
tree assembles the live `kernel/`, so commit tools and tests freely while a
soak runs and hold kernel edits until it ends.

**What is still exposed, and it is the other direction.** The soak no longer
minds the operator; the operator may still mind the soak. **Three** rows write
`build/` — `buildmatrix`, `ctoolchain` and `fdlgthumb`, as §14.1 records. The
compression work added eight more and §14.3 is what became of them.

### 14.3 The compression rows, and what converting them found

The merge arrived with eight `builds=True` rows — `lzload`, `lzload-lz4`,
`lzmod`, `lzmod-lzb`, `lzship`, `lzship-lzb`, `kzboot`, `kzboot-off` — each
in the shape §8 describes: `make <knob>` into `build/`, use it, `make` in a
`finally` to put the tree back. All eight are gone, and **only four of them
ever needed a build at all**:

| row | what it actually needed |
|---|---|
| `lzload-lz4` | a `COMPRESS=lz4` tree — a private one now |
| `lzship`, `lzship-lzb` | `PKGZ=<fmt> COMPRESS=<fmt>` — a tree each |
| `kzboot-off` | a `NOKZIP=1` tree |
| `lzload`, `lzmod`, `kzboot` | **nothing**: the shipped kernel and a fixture |
| `lzmod-lzb` | **nothing**: the shipped kernel carries both decoders, so the arms differ only in which format the FIXTURE is wrapped in |

`make zset` is the one worth naming, because the target exists only to work
around the missing tree: the compressed images land at the same paths a plain
build uses, so it copied them aside and deleted the originals either side —
three steps to stop the next `make` shipping a compressed floppy. `BUILD=`
has nothing to copy and nothing to delete.

**The declarations were 2,721 seconds and the rows are 701.** Every one was
declared against the old shape, where the row's own cost was two full builds.
Measured on this box, warm trees, in a lane of three: `lzship` 33.3s against
420 declared, `lzship-lzb` 31.7 against 600, `kzboot` 16.0 against 120,
`lzmod-lzb` 21.7 against 300. A cold private tree is **39s**, which is what
the four building rows carry above their measured time.

**Three defects came out of the conversion and none is about compression.**

1. **`lzdrv` was reading an unconfirmed value and passing by luck.**
   DRVCALL's probe strings are rewritten by `dc_probe`, which runs from
   `dc_paint` — and the window's FIRST paint does not reach the driver, so
   the three lines are still the image's own `Ping: ..` after the window is
   up and the screen still. `tests/drvcall.py` has always clicked the window
   to force a second probe; this row did not. Alone it won the race; with a
   second emulator on the box **both the converted row and the row at HEAD
   read `Ping: ..`**, which reads exactly like a driver that failed to
   expand. It now clicks and then waits for the string to change.
2. **`DRVR_SEG` is not "the driver is up".** `drv_load` writes it the moment
   `mem_claim_hi_x` answers — before the file is read, checked, expanded, its
   bss re-made and `drv_attach` far-called. A wait on it returns three
   quarters of the way through a load. `drv_owner` for the class is the
   signal, because `drv_publish` is reached from `drv_attach` and nothing
   else writes it.
3. **`lzship` walked `drv_tab` on a stride of 10** where `DRVR_SIZE` is 16,
   over sixteen rows where `DRV_MAX` is five — 256 bytes of an 80-byte table,
   on a stride matching no field. It never said so because the only assertion
   was `live < 1`. Both numbers come out of `os88sym.equates()` now, which
   also makes them right on `kern_small` (`DRV_MAX` is 4 there).

**And one green row that ran nothing.** `mkclick` is a GENERATOR — it writes
`build/click.mod`, a metronome module for judging A/V sync by eye and ear, and
asserts nothing. Registered as a soak row declaring 10s, it reported `ok` in
0.0s. That is `dispcp`'s failure from §6 exactly, and the same rule applies:
nobody investigates a pass. It is unregistered with a reason, and it was also
the last row in the suite that wrote `build/` without meaning to.

**`plain()` had to follow the freeze too.** `os88build.plain()` answered
`build/` literally, so the six rows that take an A/B — `blitcut`,
`blitplane`, `dispseam`, `curdisk`, `fatwpin`, and now `kzboot` — read the
operator's directory for their SHIPPED arm while the knob arm came out of a
tree. Half a row against a directory somebody may be building in, and only on
the arm that is supposed to be the control. It resolves through `at("build")`
now.

**A tree left half-built is poison, and make cannot see it.** An interrupted
`make` — a timeout, a killed agent, a container reclaimed — can leave an
output created and EMPTY, and an empty file is newer than everything it was
built from, so make reports the tree up to date and the next consumer fails
somewhere else. `build/trees/plain-1c902f1e` kept a 0-byte `kernel-full.bin`
through this work and `t_buildmatrix`'s `make small` row failed with
*"os88mod: kernel image is impossibly short"* — which reads as kern_small
being broken and is a truncated file nobody rebuilt. `os88build.tree` now
deletes zero-length products at the top of a tree before make looks at it; no
rule in the Makefile writes an empty file, so there is nothing legitimate to
lose.

**The trap to write down is `env=`.** `os88marty.launch` takes no `env`
argument — the emulator needs no environment, the symbol reader does — and
`os88build`'s own header showed `launch(..., env=t.env)`. Three converted rows
copied it and died with a `TypeError` in their first second. `t.env` is for a
subprocess; inside one process the call is `.apply()`, which sets os88sym's
module default as well as the environment. The header is corrected.

---

## 15. THE FOURTH CORE — a NICED emulator slot. NOT STARTED; measure first

> **SUPERSEDED — §4.2 is the answer now.** `widths()` returns
> `(mj = cores, hj = cores)`: the reservation is gone and so is the case for a
> niced slot, because the four rows this section was written to explain were
> a build race and a dropped event rather than starved guests. The section is
> kept because its measurement and its hazard are both still true of anyone
> who reaches for `nice` again.

`widths()` returned `(mj = cores - 1, hj = cores)`, so **the host lane already
ran at the full core count** and the reservation cost only the *emulator*
slot. §1 explains what that bought: twelve rows at width 3 with two extra CPU
hogs passed 12/12 and ran 1.06× slower than the same rows alone. That headroom
was what a `status` poll, an editor, a `git log` or a small side task ran on,
on the argument that a run sized to fill the box exactly is one that anything
else on the box perturbs.

**What it costs is larger than the aggregate series makes it look.** Read
per-instance rather than in total, on the four-core box those numbers were
taken on:

| instances | aggregate | per guest | % of solo |
|---|---|---|---|
| 1 | 3.4× | 3.40× | 100% |
| 4 | 13.1× | 3.28× | **96%** |
| 6 | 13.9× | 2.32× | 68% |
| 8 | 13.4× | 1.68× | 49% |

At four instances each guest is still at **96% of solo speed** — the box is not
saturated there, because these guests are not purely CPU-bound. So width 4 is
about **+30% throughput for a 4% per-guest slowdown**: roughly 25 minutes off
an 86-minute soak.

**A fourth row running flat out is not the answer**, because it does not leave
the core free — that is `--marty-jobs 4` with extra bookkeeping, and it gives
back exactly the property the reservation exists for.

### 15.1 `nice` is the mechanism that gets both

Run the fourth slot's emulator at a lower priority. It soaks up genuinely idle
time and yields instantly to anything else on the box, so the headroom is
preserved *and* spent. Three properties make that safe here, and none of them
was true a week ago:

  * guest **cycle counts, `disk()` counts and pixel comparisons are COUNTED,
    not timed**, so they are exact at any scheduling;
  * every wait is anchored to the **guest's own clock** (§2), so a slower
    fourth guest simply takes more host seconds for the same work;
  * **`alone=True` already names the rows this must never apply to** — the
    ones whose ANSWER is a rate.

The eligible set is therefore "everything not `alone`", and the slot is
**self-limiting**: under load it just gets less done, which is the behaviour
wanted rather than a compromise.

### 15.2 Measured: 16.8% off the wall, and the pass rate does NOT hold

The 13.1× above is **aggregate guest speed, not row outcomes**, and this
document's own §1 is the reason not to confuse the two: contention does not
make a row slow, it makes it LESS THOROUGH, and that does not show in a wall
time. So the same 299 rows were run twice, same tree, same box, back to back:

| | `--marty-jobs 3` | `--marty-jobs 4` |
|---|---|---|
| wall | **1:39:04** | **1:22:25** |
| ok / FAIL | 296 / 3 | 295 / 4 |
| failed | msegnomem, bootfloor-ab, paintpack | msegnomem, paintpack, **uilayer**, **hdboot** |

**The throughput is 16.8%, not the ~30% predicted above**, and the reason is
in the progress: both arms were at ~77 of 299 rows after 19 minutes. The first
fifth of a soak is the SERIAL and BUILD lane, where the emulator width buys
nothing at all; the 30% estimate priced the whole run as if it were emulator
work. 16.8% of 99 minutes is still 17 minutes a run.

**Three of the failures are not about width.** `bootfloor-ab` was a Makefile
guard fixed between the arms and cannot fail in the second. `msegnomem` and
`paintpack` failed in BOTH, and are one cause: rows that SHARE a private
tree — `diskcnt` has two rows, `noplane` has five — where the second's
`os88build.tree()` re-runs `make` and rewrites `kernel.bin` under the first.
os88sym's identity check now names that outright ("the file was written 2.9 s
ago"), which is how it was finally told apart from a stale tree.

**The two that ARE about width are `uilayer` and `hdboot`, and they are the
same kind**: a drag that moved nothing, and a click whose menu never dropped
inside a 10-guest-second budget. **Input delivery**, not a measurement — so
§15.1's eligibility rule is wrong as stated. `alone` names the rows whose
ANSWER is a rate; neither of these is one, and both broke anyway. The rule a
niced slot needs is narrower: a row is safe there when **every input it
sends is CONFIRMED** — the treatment SPEC-less rows like tests/hibernate.py
got (a key resent if the machine did not take it, a click retried while its
target is still there). An unconfirmed input carries the whole row, and width
is just what exposes it.

So, against this section's own fork: **pass rate does not hold, and the niced
slot is the way to have the throughput** — but it does not come free with an
`alone` filter. The honest order is (1) confirm the inputs in the rows that
fail this way, (2) then the eligible set is nearly everything, and (3) the
default can rise as well, because at that point width 4 flat out is no worse.

### 15.3 The hazard to size

`HOST_BACKSTOP = 10.0`: a wait that spends ten times its guest budget in HOST
seconds raises *"the guest is running below a tenth of the 4.77 MHz it is
emulating — that is the BOX, not this wait."* A niced guest starved behind
three others plus an agent's `make` could plausibly reach it, and the message
would then be read as a broken box rather than as a deprioritised slot working
exactly as designed. **A niced lane probably wants its own, larger backstop**,
and whatever it gets should say in its own words that the row was niced.

### 15.4 A CONFIRMED LEVEL IS NOT A QUEUED EVENT — what width 4 was really exposing

15.2's fork said the pass rate does not hold at width 4 and that the way to
have the throughput is to confirm the inputs. Two rows were left after the
first instalment (`de8add1`), and `hdboot` is the one that had been analysed
and deliberately **not** converted, on this reasoning: `mo._edge(True)` only
returns once the guest's own `mouse_btn` carries the bit, so the ISR *has* the
press and re-sending cannot help. Every word of that is true and the
conclusion was wrong.

**What the machine was actually doing.** The row was made to say so rather
than guessed at again, and it named itself on the second sample at width 4:

```
hdboot: FAIL - pressed 'Builtins' (cell 2 of 3) at x=199 in [160,239], y=10,
and no menu dropped in 10 guest seconds. ... At the timeout: pointer (199,10)
btn 0x1, menu_dropd 0, menu_cell 2, menu_y1 20.
```

The pointer is exactly where it was asked for, the button is **down**, and no
menu is on the screen after **182 ticks** on a machine that is not busy. Ten
more seconds would have changed nothing.

`mouse_btn` is a **LEVEL** and `mou_isr` sets it. What the UI acts on is an
`EVT_MDOWN` in the ring — and **SPEC.md 10.1 says in as many words what
happens when that ring is full**: `evq_push` drops a record (the oldest,
unless the oldest is an `EVT_WAKE`, which is a promise rather than a sample).
So the press is real, the level is right, the machine saw it, and the gesture
never happened. `ui.inc` carries the same fact from the other side in three
places — *"fallback: the EVT_MUP was dropped (queue full) — the level still
says so"*.

That is why the `_edge` confirmation could not catch it, and it is one level
below what 15.2 fixed: 15.2's rows sent a confirmed PACKET and no gesture;
this one sends a confirmed **level** and no EVENT.

**The retry has to be a fresh EDGE.** `mo._edge`'s own docstring is explicit
that a Microsoft packet carries the button's LEVEL, so a re-sent packet says
what the guest already believes — which is exactly what makes re-sending safe
*there* and useless *here*. Only a release and a second press queue a second
`EVT_MDOWN`. `os88ui.UI._edge_until` is that: press, wait for the guest to
have ACTED, and on a timeout release and press again, with the first attempts
on a short budget and the last on the caller's full one. `_grab` takes it too
— it confirmed and never retried — and its `prep` zeroes `ui_dragwin` per
attempt so a second try cannot confirm itself off the first one's residue.

| `hdboot` at a lane of four | |
|---|---|
| before, confirmed level only | **1 FAIL / 2** |
| after, `_edge_until` | **0 FAIL / 3** (and `uilayer` 0/3 beside it) |

**The lesson generalises past the mouse.** Every layer here now has a
confirmation and each proves only its own claim: `to()` proves the published
cursor, `_edge()` proves `mouse_btn`, and neither proves that the kernel's
event ring took anything. The only honest confirmation of a gesture is the
state the gesture is *for* — `ui_dragwin` for a drag, `menu_dropd` for a menu
— and a row that waits on anything else is waiting on a fact it already had.

## 16. `/dev/null` — THE BOX ITSELF, and it broke mid-run here

**CLOSED: it was NASM, run by this repo's own tests.** A container gave this
project a `/dev/null` that was a **regular file** rather than a character
device, and it did it *during* a soak - the run went 14:24→16:18 and the
node's birth time was **15:14**. It happened again on 2026-09-24, born at
15:33:45 with the `kd*`/`dos*` rows in flight, and this time the cause was
measured rather than inferred. NASM treats its output files as its own: on a
FAILED assembly it unlinks the file `-o` names, and it REPLACES the file `-l`
names even on a successful one. Measured on 2.16.01 and 3.02 against a private
`mknod c 1 3`:

| | success | failure |
|---|---|---|
| `-o <device>` | still a character device | **unlinked** |
| `-l <device>` | **a regular file** | **a regular file** |

This container runs as root, so pointed at `/dev/null` that is the device
itself. `tests/kerndos.py` assembled `kdboot.asm` with `-l /dev/null` on every
soak - the `kerndos` row sat inside the minute the node was born - and eleven
more sites passed `-o /dev/null` and were one failed assembly away from the
same thing. The earlier reading that nasm "truncates rather than replaces"
was true of the one case it tested, a successful `-o`, and of neither of the
two that break it. Every site now writes a temp file beside the listing or map
it wanted, and `tests/unit/t_nulldev.py` (fast tier) fails the build on the
ARGUMENT - `-o` or `-l` then `/dev/null` in a command that names nasm - so it
is caught at the edit rather than two hours into a run.

**Ask before a two-hour run, because nothing it breaks names it:**

```sh
stat -c %F /dev/null        # must say: character special file
```

`os88soak.py check` asks it now, beside the assembler, and prints the repair as
its fix line:

```sh
mknod /dev/null.new c 1 3 && chmod 666 /dev/null.new \
    && mv -f /dev/null.new /dev/null        # as root
```

### 16.1 Why it is worth one `stat`

Four separate failures, none of which mentions `/dev/null`:

- **`./configure` dies on a spliced `config.status`.** autoconf's default
  `cache_file` **is** `/dev/null`, so its cache flush's
  `diff "$cache_file" confcache >/dev/null 2>&1` writes instead of discarding,
  and the diff lands inside the `config.status` being generated, which then
  fails on `0a1,180: command not found`. That is the whole reason an nasm 3
  could not be built here (docs/MARTYPC-DEBUG.md, *An nasm 3 in a fresh
  container*), and `--cache-file=` does not help because the `>/dev/null` is
  the broken half rather than the cache.
- **IT GROWS ON DISK WHERE OUTPUT SHOULD VANISH**, which is the one to
  remember, because a soak that runs out of space looks like a soak that runs
  out of space. Measured here: Python's `subprocess.DEVNULL` opens with
  `O_RDWR` and **no `O_TRUNC`**, so 1 MB written is 1 MB on disk — and a shell
  `>` truncating under a long-lived holder does **not** reclaim it, because the
  holder keeps its own offset. This tree's own use is a short `cp`, and
  MartyPC's output goes to a real log file, so the repo is not the big writer;
  nothing stops a third-party tool being one.
- **`diff`, `cmp` and `test -s` against it answer wrongly**, and a read gives
  junk instead of EOF.
- **At mode 0644 a non-root writer gets `EACCES`** outright.

### 16.2 What settled it

A watcher polling `[ -c /dev/null ]` at 0.2 s, logging `lsof` and every
process younger than 90 s, caught one destruction and named nothing: a nasm
that unlinks its output and exits is gone long before a 0.2 s poll can look
for it, which is also why only `/dev/null` was ever wrong and never `zero` or
`tty` - nothing here hands those to an assembler. What named it was the
node's BIRTH time (`stat -c %w /dev/null`) laid against the soak's own row
order, and then a `mknod` of our own to test nasm against without breaking
the box.

---

## 17. THE LAST ONE-AT-A-TIME LANE — 85 rows, and why each one was in it

**BUILT.** The question was the owner's: in a full soak 85 of 1,401 rows ran
one at a time, and that lane was **3,866 measured seconds** of the run on
2026-10-25 (`build/soak/20261025-140933`: 1,953 s of QEMU rows, 1,524 s of
`builds=True` rows, 389 s of `alone=True` rows) — over an hour in which three
of four cores did nothing of the soak's. Each row was asked why it had to,
and the only answers accepted were *it uses every core itself* or *its answer
is a host rate*. "It fails under contention" was not one: an emulator row
runs in guest time, so a row that fails under load is doing something on the
host's clock, and the fix is the row.

### 17.1 What each row was there for, and the verdict

| rows | why it ran alone | verdict | now |
|---|---|---|---|
| 45 QEMU rows already launching privately - `xtmp3live`, `hdtake*`, `cdrom*`, `httpmount*`, `httpinstall`, `netinst*`, `msegxms`, `dosxmsq`, `xmcheck`, `trkscrl`, `trkclick`, `dos*`, `minesrc`, `pxswin-qemu`, `vidclose`, `vidly*`, `rdxms`, `viddiskcpu`, `netcoexist`, `syscheck`, `fmtpair`, `weavevm`, `weavecanvas`, `colorpresets` | the runner put a row in the concurrent lane only if it needed `marty` - nothing about the row | HARNESS | the lane takes `qemu` rows (§17.2) |
| 11 QEMU rows on a FIXED socket or a SHARED image in place - `vgadirty`, `heapmap`, `wheelps2`, `vesa*` (8), `vmmouse` | `build/qmp.sock`, `build/vesa.sock`; `build/apps.img` booted writable (the second machine on it is `Failed to get "write" lock`) | HARNESS | `os88qemu.paths()`, `snapshot=on`, a private blank disk for heapmap's IDE |
| 16 QEMU rows booting through `make test` (or, for `wirezone`, building its disk with `make ethertest` first) - `thewire`, `wirezone`, `brarena`, `brcapacity`, `fddpage`, `wirechat`, `telansi`, `ethernet`, `brdownload` (3), `brmem`, `ethcfg`, `telzm`, `lemmings`, `excitebikeg1` | `build/qmp.sock` and `build/qemu.pid` spelled into the Makefile; the shared images write-locked; a `make` parse in the shared tree; and `builds=True` for the `make ethertest`/`browsertest`/`telnettest` each ran first, often after DELETING the built image because the guest dirtied it | HARNESS | `os88qemu.make_test(...)` with disk COPIES (so nothing is ever dirtied and nothing needs deleting) and `wants=` for the artefacts - 15 `builds=True` flags gone, `buildmatrix` the 16th |
| `buildmatrix` | `builds=True` | LEGIT for the cores, HARNESS for the tree | its 99 knobs already build in `build/bm-<name>/`; its one shared-tree `make` (`associco.inc`, which under a frozen run rebuilt the OPERATOR's build/ while the row read the run's) is a `wants=`. It stays ALONE: it runs itself at `-j4`, so it saturates the box and sharing it buys no wall |
| `excitebikeperf`, `-selfb`, `-perfherc`, `-selfbherc`, `pxsbench` | "A RATE, so it wants the machine to itself" | WRONG FLAG | the rate is MartyPC's cycle counter between frame marks, which no host load moves; nothing in them is host-timed. Shared the lane, passed at 8+ emulators on 4 cores |
| `excitebikelap`, `-lapherc`, `-audiolap` | the same | TEST BUG | the reference rider's script is a 256-step ring the harness fed FROM THE BODY while the guest ran free, asserting it stayed ahead - about a host second of slack at MartyPC's 3.4x. The frame mark now STOPS and the feed runs there (a frame is at most `XB_STEPS_MAX` steps), so the ring is ahead by construction; the stop costs the guest no cycle |
| `excitebikeaudiocap` | heard 3 of 4 engine tones in two parallel soaks | TEST BUG, already fixed | the capture is guest time (the debug server drains the speaker per emulated batch); the row had since stopped depending on the segmentation that moved. Shares the lane, passed under load |
| `xtmp3eng` | `serial` | SPLIT | its RATE legs are `xtmp3rate`, ALONE: `ladder`, `slowdown` and `rungfail` (the stub's cost is spun on the PIT, so the verdict is guest CPU against QEMU's HOST clock: 1.05x has 5% headroom, and `rungfail`'s 4x Half calibrated at 0.87x under load), and the four that assert NO UNDERRUN on the SB16 (`playwav`, `pause`, `wavdirs`, `xtpeak` - the card drains its ring on the host's clock, and `playwav` underran under load). The other 21 legs are behaviour and share the lane. **Since 17.8 `xtmp3rate` shares it too**, its legs on an instruction-counted clock (`-icount shift=4`). `xtmp3live` asserts no underrun too and stays in the lane on evidence: its decode has QEMU's whole headroom over an XT, and it never underran in four loaded runs - it is the row to move first if it ever does |
| `vidbankspkbank`, `vidbankspkfast` | `serial` | LEGIT host rate | the player benches key 1's decode on the PIT, which without `-icount` is the host's clock; at eight emulators on four cores it timed nothing and muted. ALONE - **until 17.8**: `-icount shift=6`, and the lane |

So **the one-at-a-time lane was four rows**: `buildmatrix` (~540 s),
`xtmp3rate` (~250 s) and the two speaker benches (~50 s) - about 840 s where
it was 3,866. **Since 17.8 it is ONE**, `buildmatrix`, which runs itself at
`-j4`: the three QEMU rate rows run on a fixed-speed guest and share the lane.

### 17.2 What made QEMU shareable: MartyPC's answer, one emulator along

`os88marty.launch` solved this for MartyPC long ago - a port, a directory and
disks per instance. `tests/os88qemu.py` is the same thing:

* **`RUNDIR`** is a directory per process, under `$OS88_QEMU_DIR` when the
  runner hands the row one (else `/tmp/os88q-*`), and it owns the default
  `SOCK`/`PIDFILE`. `/tmp` by NAME: the runner points a row's `TMPDIR` at
  `build/tmp/`, and a worktree's path left no room in a socket's 108 bytes -
  the first run failed exactly that way.
* **`make_test(*knobs)`** is the Makefile's `test:` recipe (`TESTIMG`,
  `TESTAPPS`, `TESTHD`, `ETHER`, `ETHDUMP`, `ADLIB`, `SB16`, `NOCARD`,
  `VMPORT`, `MOUSEPORT=ps2`; a kernel knob is REFUSED by name rather than
  ignored), on copies of the floppies - an image already inside `RUNDIR` is
  the row's own scratch and boots in place (`scratch()` makes one, for a row
  that reads its disk back). It points `os88qemu.SOCK` and
  `tests/ethernet.py`'s `SOCK` at the machine, so every helper that defaulted
  to "the" socket drives this one. The Makefile's pidfile is `$(QEMU_PID)`
  now as well, for two machines started by hand.
* **The runner sweeps a timed-out row's OWN directory**, never `build/*.pid`
  (which in a shared lane could be any row's), and **charges a QEMU row's
  timeout in its emulators' CPU too** - found by the pidfiles in that
  directory, since `-daemonize` reparents them out of the row's tree. Before
  that a QEMU row's timeout was wall seconds, and `vidlysplitbase` timed out
  at 150 having done ~40 s of work on a box running eight emulators.
* **`t_qemuown`** counts `make_test(` as a launch that owns its instance.

### 17.3 The one thing that cannot be private: a fixed host port

A slirp guest reaches the host at the gateway - `10.0.2.2:<n>` is the host's
`127.0.0.1:<n>` - and the port is baked into fixtures: `WIRE.CFG` (8092),
`HTTPDRV.CFG` (8095), `install-gate.o88`'s `IN_URL` (8096), `dospkt.asm`'s
segment (8099), the browser fixture's links (8090). The remap that would make
it private is `guestfwd`, and libslirp refuses it on the gateway:

    -netdev user,id=n0,guestfwd=tcp:10.0.2.2:8090-cmd:cat
    ... Conflicting/invalid host:port in guest forwarding rule

So the port is TAKEN IN TURN. `Row(locks=("port:8090",))` names it, and the
concurrent lane (`_locked_lane`) never runs two rows holding one name -
picking another row meanwhile, lock groups and long rows first, rather than
parking a lane on the wait. `os88qemu.lease_port(n)` is the cross-runner half
(an flock on `/tmp/.os88port-<n>.lock`): another checkout's soak serving the
port is waited for, where it used to be an `Address already in use` dressed
as the feature. A row whose GUEST dials a port holds it even with no server
of its own - dospkt's SYN to 8099 would otherwise be accepted by dosxlat's
listener - and `telzm` moved from 8095 to 8098, having shared httpmount's.

| lock | rows |
|---|---|
| `port:8090` | `ethernet`, `brdownload` (3), `brmem` |
| `port:8092` | `thewire` |
| `port:8093` / `8094` / `8097` / `8098` | `wirechat` / `telansi` / `httpinstall` / `telzm` |
| `port:8095` | `httpmount`, `httpmountname` (+ `port:53`, its DNS stub), `netinst`, `netinstbad` (the drive their disk names) |
| `port:8096` | `netinst`, `netinstbad` |
| `port:8099` | `dosxlat`, `dospkt`, `dosnetarena` |

### 17.4 The builds=True half

Every QEMU `builds=True` row was building an artefact `wants=` can name -
`make ethertest`, `browsertest`, `telnettest`, `wirechattest`, `thewiretest`
- and most deleted the built image first, because the guest wrote to it.
Booting a COPY ends that hazard outright, so the image is never dirty and the
row declares it. **`builds=True` is on two rows now** (`ctoolchain`, full
tier, and the fast `brcapacity-fixtures`, which the registry reads through a
shared script), where it was 16 in the soak.

### 17.5 What load found, and the shape of each fix

Every converted row was run four wide with this box ALSO running another
checkout's whole soak (four MartyPCs) and, for the last passes, two CPU hogs
started and killed by PID - load averages of 11 to 18 on four cores, about
twice the oversubscription a real soak runs at. Rows that had passed alone
failed there, a different handful each pass, and every one was a host-timed
step:

* **HMP `sendkey` loses a key to a starved guest** - its release follows the
  press by 100 ms of host time, and a guest that has not run in that window
  takes both at one IRQ1. `telansi` typed `10..2.2:8094`; `lemmings` lost
  its F11 pause, so the walking lemmings read as a save-under defect;
  `excitebikeg1` never reached the race; `vidlybankcopy`'s Play never
  started. The fix is in the CLIENTS rather than in 29 scripts: a plain
  `sendkey <keys>` is CONFIRMED by `tools/qmp.py` (and so by
  `xmcheck.qmp`, which is it in process) and by the four hand-rolled HMP
  clients (`ethernet`, `telansi`, `thewire`, `wirechat`'s `Qemu.hmp`, and
  `tools/heapmap.py`'s `Qmp`) - the press, then the release only once the
  BIOS buffer's tail has moved, or after 0.3 guest seconds (inside the 500 ms typematic delay) for a key the ROM
  stores nothing for, modifiers held across it. `os88qemu.key` was that
  already, for one row; `type_text` is it for a string, and three copies
  of a typing loop are one.
* **A dead-reckoned click lands somewhere else.** `xmcheck.goto` - under
  every `xmcheck.click`/`dblclick` - was `tools/mouse.py`, which pins
  against the clamp and walks back on host sleeps; `httpmountname` and
  `rdxms` timed out waiting for a listing the double-click never asked
  for. It drives `os88qemu.mouse_to` now, closed loop on `mouse_x`, and
  falls back to `tools/mouse.py` only where the kernel's words cannot be
  read.
* **A driver's segment read mid-boot.** `brmem` and `ethcfg` read
  `eth_mode 211`, `dhcp_st 194` and an address of `195.249.195.80` for the
  whole row: `drv_tab`'s segment is armed BEFORE the image is read, and
  measured it moves once more 0.2 s AFTER the desktop is up
  (39040 -> 39296). `os88qemu.kernel_up` gates on the boot and the segment
  is taken once it has held still (`quiesce`), in `ethernet`, `ethcfg` and
  `brdownload`. Earlier still is worse: `drv_tab` is `.bss`, and until stage
  2 has expanded the packed kernel those bytes are the LZ4 stream - a junk
  word satisfied `dosnetarena`'s poll and the row reported "never attached"
  off the cleared one, in 0.7 s. The three DOS NIC rows gate on the boot too.
* **A fetch is not a layout.** `brdownload` read the line table the moment
  the page's state said loaded, and a starved guest had no line holding the
  link yet; it waits for `br_nlines`/`br_doclen` to hold still
  (`brcapacity` had already learned this).
* **A raise is not finished when the window is frontmost.** `minesrc` case G
  (red once before, in the 2026-09-21 soak) pressed again after 20 quiet
  ticks; it waits for the UI to go idle.
* **A fixed pause as a load's whole budget.** `lemmings` gave a C package
  three paced seconds to open its window; on QEMU those are the host's, so
  a starved guest did less of the load in them. It waits on the window.
  `xtmp3eng`'s `Rig.until` was `time.time()` and its 29 pauses
  `time.sleep` - `XTMP3's window` stopped arriving inside 20 host seconds;
  both are the guest's clock now (`gpace`), but for one 45-second WALL
  soak that is about QEMU's own IRQ0 rate.
* **`xtmp3live`'s seek** read the clock after a host `time.sleep(0.5)` meant
  to clear a 14 ms transient; it waits for the same second on two readings a
  guest tick apart.
* **A ceiling that was too low to be one.** `telansi` gave the Ctrl+]
  bracket 5 guest seconds; it has 30, and ends the moment the byte is set.
* **The Excitebike lap feed** (17.1).
* **A QEMU row's wall-clock timeout** (17.2).
* **`xtmp3eng`'s rate legs and the vidbank speaker benches** are the
  honest kind: QEMU's guest clock is the host's, so a starved guest IS a
  slower machine (17.1) - until `-icount` made the guest's clock its own
  (17.8).

**The lesson under all of these is QEMU's and not MartyPC's**, and
`tests/os88qemu.py`'s clock note says it now: without `-icount` a QEMU tick
is the HOST's, so a tick budget cannot be shortened by load the way a
`time.sleep` is - but it buys a starved guest less of the machine. On
MartyPC a guest second is a fixed amount of work; on QEMU it is not. So on
QEMU a budget is a generous CEILING on a wait for state, never a pause that
something is assumed to have finished inside, and an answer that IS the
guest's speed against that clock is a host rate.

### 17.6 The runs, and what is still unconfirmed

All four wide, all on a four-core box ALSO running another checkout's whole
soak; "hogs" is two `python3 -c 'while 1: pass'` started and killed by PID.
The rows are every `serial` QEMU soak row plus the nine former `alone=True`
MartyPC rows; each pass ran the code as it stood, so the failures shrink as
the fixes in 17.5 landed.

| pass | load | rows | wall | passed | failed, and what each one was |
|---|---|---|---|---|---|
| 1 | soak only | 77 QEMU | 995 s | 70 | xtmp3live (seek sleep), vidbankspk x2 (host rate), vidlysplitbase (wall timeout), xtmp3eng (slowdown leg), lemmings (sendkey), telansi (sendkey) |
| 2 | soak only | 43 (excitebike set + fixed rows) | 1,247 s | 41 | excitebikeg1 (sendkey); excitebikeflowherc (MartyPC, already concurrent - 17.7) |
| 3 | + 2 hogs | 87 | 1,525 s | 80 | httpmountname (dead-reckoned click), brmem + ethcfg (driver segment), xtmp3eng (host waits), lemmings (window pause), rdxms (click), vidlybankcopy (sendkey) |
| 4 | + 2 hogs | 87 | 1,414 s | 80 | vidbankspkfast (host rate), brdownload-length, telansi (5 s ceiling), httpinstall + vidclose + vidlysplituntimed (double-click split), xtmp3live (window) |
| 5 | + 2 hogs | 87 | 1,309 s | 84 | xtmp3eng (playwav underrun), minesrc + trkscrl (a one-argument `hmp` the burst double-click called with four) |
| 6 | + 2 hogs, + buildmatrix | 88 | 3,979 s | 84 | brdownload-auto (layout), dosnetarena (boot-time `.bss`), xtmp3eng (rungfail), minesrc (case G). buildmatrix passed, 2,445 s at load 16 against its 540 alone |

**The fixes for pass 6's four failures are NOT confirmed under load** -
they landed after it, and a seventh pass was stopped at the owner's word
because the passes were slowing the soak beside them. The next full soak is
their test. Nothing here has run at the soak's own width on an otherwise idle
box, which is the condition the lane will actually see; every pass above was
at roughly twice that oversubscription.

### 17.7 What is left

* ~~**`-icount` for the QEMU rate rows.**~~ DONE - 17.8. `xtmp3rate` and
  `vidbankspk*` run on an instruction-counted clock and share the lane; the
  one-at-a-time lane is `buildmatrix` alone.
* ~~**`trkscrl`'s own QMP client**~~ DONE - 17.9: its one `sendkey` is the
  confirmed press-then-release over its own connection. `sendkey <keys>
  <hold>` with an explicit hold time is still deliberately left alone
  everywhere.
* ~~**Host sleeps in the other QEMU rows.**~~ DONE for every registered row
  that had one doing a guest's job - 17.9 is the list, and what was LEFT
  as a host sleep says why at the site.
* ~~**A shared image booted writable.**~~ DONE - 17.10: `ps2mouse` and
  `vesashut` held `build/os8088.img`/`apps.img` writable and locked every
  other row out of them; every daemonised launch now raises QEMU's own
  words and names the holder.
* ~~**`brdownload-length`**~~ DONE - 17.11: the row read a scratch line
  INSIDE the completion's repaint; it reads the ETA field off the glass.
* **`vesagor`** - 17.11. The "not one of the seven Enters" reading was the
  loop's, and one way an Enter is lost here is found and closed (the key
  helpers released a key the ROM had not yet stored); the failing run's
  full-screen field reading is NOT explained, and the row now keeps the
  picture and names where a lost Enter went. The other half of the loss is
  gone at its source: SPEC.md 9.7.2 takes the key through `int 15h AH=4Fh`
  wherever the ROM offers it (SeaBIOS does), so `kbm_isr` no longer reads
  port 60h ahead of the ROM and QEMU's 8042 has no byte to drop.
* ~~**`excitebikeflow` / `excitebikeflowherc`**~~ DONE - 17.12. Two
  different causes. `excitebikeflow`'s 2026-10-25 failure (`state 2 (it is
  3)`, the held Enter) was the GAME's and was already fixed by dc527c99,
  which landed on the integration branch the same afternoon this bullet was
  written on another. `excitebikeflowherc`'s `Up flips the mode` was the
  HARNESS: the key's release was sent on the host's clock, a stalled client
  held Up past the keyboard's 0.5 s typematic delay and the repeat flipped
  the mode back. Every key is now held an exact number of guest cycles.
* **`ftpd.py`/`ftpkeep.py`** are unregistered (`t_registry`'s exemptions),
  and each runs `make ftpdtest`/`ftpkeeptest` in the tree - ftpd.py deleting
  and rebuilding `build/ether360.img`, a `wants=` of the browser rows - and
  boots its images `copy=False`. Harmless by hand; not lane material.

### 17.8 The rate rows on a fixed-speed guest: `-icount`

`os88qemu.icount(shift)` is `-icount shift=N,align=off,sleep=on`. Every
guest instruction costs 2^N ns of VIRTUAL time, and QEMU's PIT, the BIOS
tick, the SB16's DMA and the wav audiodev all run on that clock, so a bench
spun on the 8253 counts instructions, and a starved host stretches the wall
time and moves no answer. `align=off` runs as fast as the host can;
`sleep=on` keeps an IDLE guest (`hlt`) on real time, which a disk waiting
on the host needs (with `sleep=off` virtual time warps to the next timer
while the I/O is still in flight). `OS88_QEMU_ICOUNT=0` is the A/B and `=N`
forces a shift. The two known catches were real and neither bit: the SB16
DOES move to virtual time, which is the point - its ring drains in
instructions, against the decoder's own - and the disk throttle stays on
real time, where neither row's answer is a disk rate. `vidlysplit`'s
slow-disk rows ARE about one, so they stay off icount, and must.

**xtmp3eng's ladder, the same fixtures, the stub's measured speed a rung:**

| arm | capable 4.00 | middling 1.00 / 2.00 / 4.00 | slower 0.60 / 1.00 / 2.00 | xt44 0.80 / 2.00 | xt 0.50 / 0.80 |
|---|---|---|---|---|---|
| no icount, idle box | 3.91 | 0.98 / 1.89 / 3.90 | 0.59 / 0.99 / 1.92 | 0.78 / 1.91 | 0.49 / 0.78 |
| no icount, 2 CPU hogs | 3.39 | 0.94 / 1.83 / 3.37 | 0.50 / 0.83 / 1.71 | 0.78 / 1.73 | 0.47 / 0.76 |
| shift 4, idle | 3.72 | 0.98 / 1.93 / 3.73 | 0.59 / 0.98 / 1.93 | 0.78 / 1.93 | 0.49 / 0.78 |
| shift 4, 2 CPU hogs | 3.72 | 0.98 / 1.93 / 3.73 | 0.59 / 0.98 / 1.93 | 0.78 / 1.93 | 0.49 / 0.78 |
| shift 6, idle | 3.08 | 0.93 / 1.75 / 3.12 | 0.57 / 0.93 / 1.75 | 0.75 / 1.75 | 0.48 / 0.75 |

Without icount two hogs take 13% off the capable machine and 15% off the
slower one's 0.60x - more than the 1.05x leg's 5% of headroom. With it the
loaded figures are the idle ones TO THE DIGIT. The SHIFT is the trade: the
stub's own work costs 2^N ns an instruction, so a larger shift is a slower
guest under the configured rung, and at 6 a configured 1.05x reads under
1.0 - the slowdown leg would underrun by construction. **Shift 4** (62.5
MIPS) reads a configured 1.00x as 0.98, as the idle box did, and costs the
seven legs **295 s against 198** idle; under two hogs (load to 12) **342,
369, 347 and 327 s, every leg passing**, four passes of four.

**vidbank's speaker benches take shift 6.** A speaker play is a PIT write a
sample, icount ends a translation block at every I/O instruction, and at
shift 4 the play ran ~20 host seconds a guest second (`30 frames`: 18 ticks
in 20.6 s) - `spkbank` 80 to 217 s against 26 without icount. At 6 the bench
reads 52 (520 us a decode; 12 at shift 4, and 266-387 without icount, whose
cost there is MMIO the icount model makes nearly free) against yardsticks of
1 and 65,535 - margins of 26x and 630x. `spkbank` 50-52 s and `spkfast`
17-22 s under two hogs, every run green from the fix below on.

**And it found the row's real flake, which was not the clock.** The first
pass under hogs failed `spkbank` *from key 1 the bench timed nothing*. One
`sendkey right` is TWO keystrokes at the player on QEMU (the extended key's
E0 prefix; `tests/trkscrl.py` had measured arrows 2, every ASCII key 1), so
the step to key 1 went 0 -> 2 and the step back 2 -> 0, and the loop reached
key 1 only by reading it BETWEEN the two strokes - after which the second
stroke moved the selection under the Play. Waited out properly it
oscillated, 15 steps in one run. The keypad's 6 and 4 are the same scan
codes with no prefix (NumLock off: the ROM's Right and Left) and step once.

What icount CHANGES, and why it is these rows only: the cost model. An I/O
or MMIO instruction costs what a register move does, where without icount
QEMU's device emulation is most of the host time - so a bench's absolute
number is a different machine's, and the verdicts survive because they are
relative (rungs against each other, a decode against a yardstick). A row
ABOUT host behaviour, or whose throttle is the rate it compares, cannot use
it.

**17.8.1 - icount did NOT put the Sound Blaster on the guest's clock, and
`xtmp3rate` kept failing under load because of it.** The paragraphs above
say the SB16's DMA runs on the virtual clock. It does not: QEMU's 8237
(`hw/dma/i8257.c`) moves a running channel from an IDLE BOTTOM HALF on the
main loop, polled at host intervals of up to 10 ms, while under TCG the vCPU
thread executes without the BQL - so guest time keeps passing while the main
loop is not scheduled, and the card's block IRQ is late by however long that
was. Made exact by stopping ONLY QEMU's main thread (ptrace, its tid) during
a live play: 120 ms stalls every second play to the end, a single 300 ms
stall is SOUND.DRV's watchdog (2 block periods + 3 ticks) and *The sound
card stopped*, with 0 underruns - the pause leg's "under 0" exactly - on
8.2.2 and on 9.2.4 alike. Stopping the WHOLE process for 500 ms is harmless
(the guest's clock stops with it): it is the main loop falling behind the
vCPU, which is what a loaded box does. `tools/qemu9/i8257-icount.patch`
runs the channels off a QEMU_CLOCK_VIRTUAL timer under -icount, which the
vCPU thread runs at the instruction count of its deadline; the same play then
survives seven TWO-SECOND stalls of the main thread with 0 underruns, and
the seven rate legs pass. `tools/setup-qemu9.sh` builds it into build/qemu9
with a pkgversion naming the patch set (`os8088-<hash>`), `os88build.qemuvt()`
reads that back, and `xtmp3rate` needs `qemuvt`. The DISK is not covered:
IDE's aio completes on the main loop as well, so a read's latency is still
the host's - a look-ahead ring absorbs it here, and it is the next place to
look if a rate row underruns.

The same row's other failure was the PRODUCT: `rungfail`'s *no ERROR on the
way* saw state 10 because XTMP3's ladder published ERROR for an instant on
every retry of a rung that could not start (seven times a run, under a
watchpoint), and a 0.1 s host poll caught it more often the less guest time
each poll covered. Fixed in `apps/xtmp3/x3eng.inc`, +6 bytes.

### 17.9 The host sleeps, row by row

Prioritised by the soaks' FAIL lines (`build/soak/*/run.log` in both
checkouts - `vesafsx`, `rltune` and `brdownload*` had failed) and by the
count of sleeps. Every one below waited in HOST time for GUEST work; on an
idle box each conversion is the same wait, and on a loaded one it is the
same amount of the machine.

| row(s) | what it was | what it is |
|---|---|---|
| `rltune`, `brarena`, `brcapacity`, `brdownload*`, `ethcfg`, `lemmings`, `excitebikeg1`, `thewire`, ftpd.py, ftpkeep.py | `ethernet.Mouse`: tools/mouse.py per gesture, pin-and-walk on paced moves. `rltune` died in it in the 2026-10-25 soak (`to 592 54`, exit 1, in `dispcp.open_drive`) | closed loop on `mouse_x`/`mouse_btn` (`os88qemu.mouse_to`/`mouse_button`/`mouse_dblclick`), tools/mouse.py only where those words cannot be read; each verb's guest pauses kept, in guest seconds. `thewire`'s own copy of the dead-reckoned class is now this one on its socket |
| the browser fixtures (`ethernet`) | twelve `while len(srv.requests) < N and time.time() < deadline` | `os88qemu.acted`: the deadline in guest seconds |
| `vesafsx` (red 2026-10-25: Alt+Enter took no bracket), `vesapref`, `vesablit4`, `vesagor` | a 60 s host deadline on `vid_kind`, then `pace(12)` "for the autostart's load and first paint"; `pace(4)`/`pace(5)` around Alt+Enter and Esc | `vesa.desk_up`: `kernel_up`, `vid_kind`, the autostart's instance live, `ui_done`; `vesafsx` waits on `fsx_task` in and out and on the card's mode. Declared 90 s and running in 3-25, they are re-declared |
| `rltune` | `until`: a host deadline | `acted` |
| `thewire` | File > Refresh as down / `sleep 0.5` / to / `sleep 0.4` / up | each step waited on `menu_dropd`, then `menu_sel` |
| `trkscrl` | `sendkey` raw over its one QMP connection (the last unconfirmed client); a dead `goto`/`click` on a host PACE | the press, the ROM's tail moving, the release - over that connection; the dead code deleted |
| `netinst` | `sendkey` + `sleep 0.3`; `sleep 3` and a four-second HOST quiesce of C: across the Restart; `sleep 4` for the autostart | guest pauses and `os88qemu.quiesce` (the POST's tick re-set counts as one tick) |
| `netcoexist` | `system_reset`, `sleep(1.0)`, then a desktop wait the OLD kernel's RAM could satisfy | `desk_rows`/`menu_nbar` zeroed (`os88qemu.poke`) before the reset, `kernel_up` after |
| `httpmount`, `httpmountname` | `QH.click`: tools/mouse.py | `mouse_click` |
| `lemmings` | `pace(8)` after the boot | `kernel_up` |
| ftpd.py (unregistered) | 35 sleeps; a 200 x 0.4 s HOST poll for DHCP; a host window poll; a STOR-after-LIST timed in host seconds | `gpace`; DHCP after `kernel_up`, on guest seconds; the STOR timed in guest seconds (a starved guest is not a SYN backoff). 387 s, all assertions passing |
| ftpkeep.py (unregistered) | host settles and pauses; a 120 s host deadline for 1.2 MB on the disk | `ui_done`, `gpace`, `acted`; all assertions passing |

Left as host sleeps, each with its reason at the site: a SERVER's pacing
(`ethernet`'s chunk delay, `brdownload`'s, `brnav`'s `_slow`, `netinstbad`'s
hold); a socket or file appearing, a process exiting; `lemmings`' relative
walk (the msmouse line's pacing - the chardev queues a step, none is lost);
`vmmouse`'s drag (absolute positions: a step polled late is overtaken, not
lost); `telansi`/`telzm`'s 0.3/0.6 s around a host listener; `hmahiber`'s
`host_wait` (sectors QEMU has that the file does not yet). `vesabench` still
polls its report on a 300 s host deadline: a bench, nothing in it failed.

**The runs.** Two lanes, no hogs: the 22 converted rows, 21 green
(`brdownload-length`, 17.7). Two lanes and two CPU hogs started and killed
by PID, other agents' emulators beside them (load to 7.9): 28 rows - every
converted one, 17.10's two victims and the three rate rows - 27 green
(`vesagor`, 17.7).

### 17.10 A shared image booted writable locks everyone else out

In a four-lane pass `wheelps2` and `vmmouse` died ~0.3 s after launch,
`exit 1`, and passed alone. Both boot the TREE's images `snapshot=on` -
read-only - and QEMU refuses even that while ANOTHER QEMU holds the same
file writable:

    qemu-system-i386: Failed to get shared "write" lock
    Is another process using the image [build/apps.img]?

Two rows did: **`ps2mouse`** (full tier; `build/os8088.img` and `apps.img`,
in place) and **`vesashut`** (`build/vesa.img` and `apps.img` - and the Shut
Down it presses WRITES to them, so it was dirtying the tree's images too).
Both boot `snapshot=on` now, on `os88qemu.paths()` sockets in place of fixed
`build/*.sock`. Every other launcher was checked: `cdrom`, `vidxms` (and its
`Q`'s callers), `p2hiber`, `redline`, `syscheck`, `vidspkat`, `hdtake`,
`httpinstall`, `httpmount`, `colorpresets`, `fmtpair`, `heapmap`,
`vesabench` and the xtmp3 rigs boot private copies or `snapshot=on`, and
`make_test(copy=False)` has two callers, ftpd.py and ftpkeep.py, booting
images they built themselves.

**`os88qemu.start(cmd, ...)`** is `subprocess.run` for a daemonised QEMU
that raises `QemuRefused` with QEMU's own stderr and, for a lock, every live
QEMU whose command line names the file, with its pidfile - which names the
row's own directory. All 24 daemonising files use it, and `t_qemuown`
(fast) fails a file that daemonises any other way, mutation-tested.
**The proof**: `wheelps2` and `vmmouse` started 1.5 s after a writer, all
three at once:

| writer | wheelps2 | vmmouse | what the victims said |
|---|---|---|---|
| `ps2mouse` as it was | exit 1 | exit 1 | `held by: pid 17501 (pidfile .../build/ps2.pid): file=build/apps.img,format=raw,if=floppy,index=1` |
| `vesashut` as it was | exit 1 | exit 1 | the same shape, `build/apps.img` and `build/os8088.img` |
| `ps2mouse` now, 3 rounds | ok x3 | ok x3 | |
| `vesashut` now, 3 rounds | ok x3 | ok x3 | |

### 17.11 A scratch line read mid-paint, and a key released before it was taken

**`brdownload-length`.** `br_dl_disptxt` is not the ETA's buffer. It is one
scratch line that `br_dl_drawcut` (the File, Source, Destination and Status
rows), the Speed field and the ETA field each compose into and draw at once
(`apps/browser/brdownload_paint.inc`), and `br_dl_onwake` writes `DL_DONE`
and THEN repaints the whole manager. The row polls that state byte, so it
read the buffer inside the repaint. Sampled as fast as QMP allows after
`DL_DONE` on an IDLE box, the line reads

    '0 sec' -> 'DOWNLOAD.BIN' -> 'http://10.0.2.2:8090/download-te' ->
    'B:\DOWNLOAD.BIN' -> 'Download complete.' -> '5 KB/s' -> '0 sec'

so the failure needed no load at all, only a read landing between the Source
row's drawcut and the Destination row's. **It is not a product bug**: every
painter runs on the UI task and draws what it composed before the next one
writes, so the screen never shows another field's text. The assertion now
waits for the UI task to finish with the completion (`ui_done`) and reads
the ETA field's cells off a screendump, matched against the kernel's own
glyphs (`[font_seg]:[font_base]`, `tests/glass.py`'s reason), until it
holds - a timer pass still armed from the transfer repaints the manager once
more, and a dump inside that white fill is a blank, not an answer. **Broken
on purpose** (the ETA drawn at x+208 instead of x+200) it fails `the
completed ETA field shows ' 0 sec' on the glass` - and the buffer read
passed that build, the buffer still saying `0 sec`. Two waits on the same
path were pauses and are state now: the link menu was read the instant
`tools/mouse.py` returned (`br_ctxopen`), and the Enter into the Save chooser
went one guest second after the click (`fdlg_win`). Two lanes and two CPU
hogs, the other lane `vesagor`: 22 of 22.

**`vesagor`.** Four findings, and the last is what is still open.

1. *The reading was the loop's.* It waited `gr_setupfield > i` and carried
   on either way, so ONE lost Enter fails all seven waits. Dropping Enter 1
   on purpose reproduces the failing line exactly - `after seven Enters the
   state is 5`, six questions answered. "None of the seven reached
   Gorillas" was never established.
2. *How an Enter is lost on this row.* Gorillas' entry proc arms the
   key-state map (`OS88_ALTENTER_SEED`) and no mouse ever speaks here
   (`mou_ptr` 0 for the whole row: the msmouse is attached and never moved),
   so `kbm_isr` peeks port 60h on every IRQ1 - and QEMU's 8042 loses a press
   queued together with its release (SPEC.md 9.7). Sent as one pair at the
   setup page: 7 of 7 Enters lost, the buffer's tail never moving.
3. *The helpers made that pair whenever the guest was late.* `tools/qmp.py`'s
   `key` and `os88qemu.sendkey` released ANY key after 0.3 guest seconds,
   seven ticks, stored or not - the ceiling meant for a key the ROM keeps
   no buffer entry for applied to every key. Press-to-store on this row
   over 203 Enters, two lanes and two hogs, in ticks as a QMP poll sees
   them: 157 within the tick, 37 one, 7 two, 2 four - so seven is a margin
   and not a guarantee. **A/B**, IRQ1
   masked at the master 8259 for one host second while IRQ0 runs (HMP `o
   /b 0x21`): the old `tools/qmp.py` lost 3 of 3, the new one and
   `os88qemu.sendkey` delivered 3 of 3. A FLAG key (shift, alt, ctrl, the
   locks) is still confirmed by 0040:0017 inside `secs`; every other key
   waits for the tail up to 10 guest seconds - `os88qemu.key`'s own default
   and `type_text`'s, a ceiling left the moment the tail moves - and a tail
   that never moves is said on stderr. Once the kernel takes the scancode
   from `int 15h AH=4Fh` instead of the peek (branch `kbd4f`, SPEC.md
   9.7.2), a queued pair stops being a loss on QEMU at all; the helper is
   right either way.
4. *What is NOT explained.* The failing run's field read `blue region
   1023x743, 0 sky rows, longest solid building run 847 px`: blue at both
   screen edges, the menu bar to the dock. The setup page and the choice
   page read 527x263 in the window whether or not an Enter was lost; so do
   a stuck map Alt bit (Gorillas enters `gr_exclusive` and waits in
   `gr_release_chord`, the seven Enters left in the buffer) and a ROM Alt
   flag (every Enter is fetched, and Gorillas ignores what it is handed),
   each poked in to see. Something blue was on the glass outside Gorillas'
   window, and that is not a key. It did not recur in 36 runs of the old
   Enter loop (without the `ui_done` guess) nor in 10 of the new, all two
   lanes wide under two hogs, and the run left no picture. The row now
   confirms each Enter answered exactly one question before the next goes,
   stops at the first that did not with the buffer's head and tail either
   side, the
   map's Alt and Enter bits and latch, the ROM's shift flags, `gr_fs`, the
   fsx bracket, the saver, the front window and whether the instance moved,
   and saves the screen to `build/vesagor-fail-<pid>.png`, as it does when
   the field check fails. A test that dropped Enter 3 on purpose stops
   there: `buffer head/tail 34/34 -> 34/34 ... state 5 field 2`.

That pass ran `vesagor` in the HOST pool beside `rltune` alone: it declared
no `needs` and no `serial`, so `os88test` put it with `par` rather than in
the emulator lane, and it would not have skipped on a box without QEMU. It
declares `needs=("qemu", "nasm"), serial=True` now, as its siblings on the
same disk do (`vesasu` has the same gap and is not changed here). The same
pass rules out a shared file for that run, though the file was real:
`vesa.screendump` wrote every `vesa*` row's dump to the one
`build/vesa-shot.ppm` and unlinked it, so two of them in the lane's two
slots could read each other's screen. It writes into the process's `RUNDIR`
now.

**What the key helpers reach, run once.** Every QEMU row whose script sends
a plain `sendkey` through `tools/qmp.py` or `os88qemu`, plus the `vesa*`
rows `vesa.screendump` serves - 75 rows, two lanes, no hogs: 71 passed,
three skipped (`hmahiber`, `p2hiber`, `hmadosret` want QEMU 9) and
`ps2mouse`, the full tier's, passed on its own. **`hmadrv` failed once**,
`...and gave the block back with the stream`, and passed alone four times -
a third flake with its cause in the code: `sbl_stop` publishes
`[sbl_str_act]` = 0 FIRST and frees the double buffer afterwards
(`sbl_unpin` -> `sbl_dma_put`, which zeroes `[sbl_seg]` last), and the row
waited for the flag and then read the block. It waits for the flag AND the
buffer now; a driver that never frees it still fails ten guest seconds on.

### 17.12 Excitebike's flow rows: one game bug, and a key released on the host's clock

**`excitebikeflow`, 2026-10-25 soak** (`/home/user/wt`, e01d797d): `state 2
(it is 3)` in `item_menus` - the mode screen's Enter, held ~2.7 guest seconds
on purpose, ran the course screen straight into a race. That was the GAME:
`xb_rdkey` takes one Enter per press, and a typematic repeat and the break
after it can land inside one 18 ms frame, so the next poll read Enter up and
took the repeat still in the BIOS buffer as a new press. dc527c99 (SPEC.md
102's paragraph) drops the Enters at the buffer's head when the release is
seen; its own sweep was 5-6 of 24 release phases red before and 0 after. It
was on the integration branch before 17.7's bullet was written on this one,
which is why the bullet said "not diagnosed".

**`excitebikeflowherc`, pass 2 of 17.6** (`Up flips the mode`, 36.9 s in, a
fifth MartyPC on the four-core box): the HARNESS. The mode screen's Up and
Down TOGGLE the selection (`flow.inc` `.flip`), so a key that repeats is two
presses. `Flow.key` held a key with `key(up=False)`, `pace(.05)`,
`key(down=False)`: the press and the release were separate commands with the
guest running free between them, so the hold was whatever the client's
polling made it. And MartyPC's 83-key keyboard repeats a key held past
**500 ms** (`keyboard_common.rs`), in guest time.

* **The holds, measured** off the cycle counts the debug server answers each
  `key` with: 2,106 short keys across one idle run and seventeen loaded ones
  (three, six and eight lanes, two hogs, load to 8.6) were 0.267-0.317 guest
  seconds - all within 0.2 s of the delay, none past it. That is the margin a
  host stall eats: the guest runs ~4.5x real time, so 45 ms of the client
  not running is 0.2 guest seconds.
* **A host stall, injected** (the client sleeping before it sends the
  release, nothing else changed): 60 ms put Down at 0.533 guest s and the row
  failed `Down flips it back` - the same assertion's twin; 150 ms put every
  arrow at 0.62-0.93 and it failed later, in `to_race`'s `the mode cursor`,
  because the parity of the repeats decides which line sees it.
* **Exactly, with no host in it**: Up held 0.45 guest s passes, 0.55 fails
  `Up flips the mode` - the soak's own line.

**The fix** is `os88marty.keystroke(m, name, hold)`: pause, press,
`advance` by the hold's cycles, release, `go`. The machine is stopped while
the host decides anything, so no stall reaches the hold; every key in
`excitebike_flow.py` goes through it (`Flow.key`, 0.2 guest seconds, and the
re-entry Enter in `item_leave`; the deliberate long Enter is 2.7). It is
`tests/isotronlive.py`'s `tap_exact`, which isoTron found the same way. With
the same 150 ms stall injected the whole Hercules flow passes and all 28
arrow holds read exactly 0.200. Loaded, both rows twice at once under two
hogs (four MartyPCs and two hogs on four cores), two passes: 8 of 8. Alone
the Hercules row is 67.5 s, as before.

**What is NOT swept.** The pattern is everywhere a test holds a key:
`key(up=False)` appears in 67 files under `tests/`, `excitebike_front.key`
is the same three lines for `excitebikefront` and its siblings, and
`Marty.alt`'s `hold` is a `pace`. A key whose repeat is harmless (Enter into
a reader that drops repeats, an arrow at a list's end) cannot fail this way;
one that toggles, counts or navigates can, and that is the next row to look
at when one of those goes red under load.

### 17.13 A frozen tree is not `build/` to the kern_mega harness - and it was never load

A scoped run of 36 rows at `--marty-jobs 3` (the VESA work, KERN-MEGA-PLAN
14.115) failed seven that all passed when re-run with `os88test.py soak
--marty-jobs 1`: `mega-lzcomp` (*"os88bochs: no kern_mega line in 240s"*),
and six kern_big QEMU rows that had run AFTER the `mega-vesa*` family -
`vesa` and `vesa8` with *"vid_kind 134, want 4 ... (vesa_row DRVR_SEG C818,
DRVR_ERR 139)"*, `vesacp`, `vesafsx`, `vesablit4` and `vesagor` with *"the
desktop never came up"*. The obvious reading was contention: three Bochs
lanes at host real time, and a VESA.DRV refusal under a loaded QEMU.

**Neither is load, and neither needs a second lane.** The re-run that passed
did not only drop the width - `os88test.py` reads `build/`, and `os88soak.py`
reads a FROZEN TREE (14.2), so every row is handed
`build/trees/<tree>/os8088-360.img` for `build/os8088-360.img`. Three places
in the kern_mega harness asked "is this the shipped image?" as
`dirname(p) == build/`, and under a tree that answers NO for exactly the
image it was written to recognise:

* **`os88megacommon.mega_images`** swaps a shipped system disk for
  `mega.img`. It did not swap the tree's, so `mega-lzcomp` booted **kern_big**
  on Bochs and waited four minutes for a `kern_mega:` E9 line that kernel
  never prints. `os88soak.py start -k mega-lzcomp --marty-jobs 1` is red on
  its own, one Bochs on an idle box.
* **`os88qmpmega._scratch`** decides which of a row's images the guest may
  write back. It took the tree's `vesa.img` for a row's scratch, so the
  `mega-vesa` row's Server made that path a SYMLINK into Bochs's run
  directory for the whole boot and, at close, copied the live A: - a
  `mega_twin`, i.e. **kern_mega's** `KERNEL.SYS` - back over it. `make` then
  sees the fixture newer than its inputs and never rebuilds it, so the tree
  stays poisoned for every later run on it. The kern_big `vesa` row booted
  kern_mega under QEMU, and `DRVR_ERR 139` is kern_mega's memory read through
  kern_big's map - not a refusal at all. Measured on the failing run's own
  tree: `vesa`, `vesa8`, `vesafsx` and `vesablit4.img` all carried
  `mega.img`'s `KERNEL.SYS`, mtimes inside the `mega-vesa*` window, while
  `vesapref` and `vesablitd`, which no mega row boots, carried kern_big's.
  `apps.img` was rewritten too (byte-identical this time; a guest that
  saved to B: would not have been).
* **every `OS88_MEGA_BUILD` fallback** (`os88megabochs`, `os88mega86box`,
  `os88sym`'s kern_mega map, `os88map`, `os88box86api`, `dispapps`) read
  `build/` - mega.img, megaapps.img and `megak/kernel.map` out of the
  operator's directory while the row's own disks came out of the tree.

**The fix** is three functions in `tools/os88build.py`, used by all of them:
`mega_build()` (`$OS88_MEGA_BUILD`, else the run's tree, else `build/`),
`shipped_dir(d)` (`build/` or the run's tree) and `shared_dir(d)` (those,
or any tree under `build/trees/` - a knob tree is shared by every row asking
for that knob). `_scratch` is now `not shared_dir`. Nothing a guest runs
changed. `tests/unit/t_megatree.py` (`megatree`, soak) asserts all three
answers with `$OS88_TREE` set and ratchets the `OS88_MEGA_BUILD` fallback
spelling out of `tools/` and `tests/`; against the old predicates it fails
four of its eleven checks.

| run (`os88soak.py start`, `--marty-jobs 1`) | before | after |
|---|---|---|
| `-k mega-lzcomp` | FAIL, no kern_mega line in 240s | ok |
| `-k mega-vesa -k vesa`, first run on a fresh tree | ok, ok - but the tree's `vesa.img` now carries kern_mega | ok, ok - `vesa.img` keeps kern_big's kernel |
| the same, second run on that tree | **`vesa` FAIL**, the soak's own `DRVR_ERR 139` line | ok, ok |

and the original 36-row set at `--marty-jobs 3`, in the failing run's own
lane order (the six `mega-vesa*` rows before every `vesa*` one): **36 of 36
ok** in 951 s, against 29 of 36 before, with `mega-lzcomp` 177.7 s - and
every `vesa*.img` in the tree byte-identical before and after the run.

A harness change took the same run: `os88bochs`'s file-size cap
(`_fsize_cap`, the 2GB guard in the same merge) raised `RLIMIT_FSIZE` to its
cap even when the shell's hard limit was lower, so under an operator's
`ulimit -f` every Bochs row died in `Popen` with *"Exception occurred in
preexec_fn"* before Bochs existed. It now stays at or under the inherited
hard limit.

**Two things to carry.** A row that passes under `os88test.py` and fails
under `os88soak.py` is not proof of contention: re-run it ALONE under
`os88soak.py` first (`--marty-jobs 1`), because the tree is the other
difference and costs nothing to rule out. And **a tree a row has written to
is poisoned for every later run on it** - the operator's
`build/trees/plain-898921f7/` from the failing run still carries kern_mega
in its four `vesa*` fixtures and must be deleted, or the next soak that
reuses that key fails the same six rows on a fixed harness.

## 18. BOCHS AND 86BOX ROWS — the 333 ports that were still one at a time

**BUILT.** 17 emptied the one-at-a-time lane of QEMU and left it holding
something nobody had put there on purpose: every `mega-<row>` and `box-<row>`
port (tests/megaport.txt, tests/box86port.txt; tests/suite.py `_ports`) - 307
on Bochs and 26 on 86Box. A port is `serial=True` for the emulator lane, and
`_ports` DROPS the base row's `marty`, `qemu` and `qemu9` needs because the
port runs on neither - so the lane's admission test, *needs `marty` or
`qemu`*, refused every one and they ran end to end after everything else. On
a four-core box a full soak then sat at a load average under one for hours.
No row had ever been found unable to share; the test simply did not name the
emulator.

### 18.1 What the instances already had

The emulator half was done before this started, one emulator at a time:

| emulator | what is private per row | where |
|---|---|---|
| Bochs | an X display from the runner's pool (`:110`-`:149`, an flock on `/tmp/.os88bochs-<n>.lock` that `os88bochs.take_display` honours from `:150` up for a row's extra machines); a run dir under `build/bochs/inst/`; every process it counts or kills is the one whose environment names ITS display | `tools/os88test.py` `_bochs_display`, `tools/os88megabochs.py` `launch`, `tools/os88bochs.py` `running`/`stop` |
| 86Box | a run dir under `build/86box/inst/` and a name from it, its automation socket inside that, a GDB port from `free_port`; `reap` removes only TEMP runs whose process is gone | `tools/os88box.py` `launch`, `tools/os88box86api.py`, `tools/os88mega86box.py` |

Neither binds a fixed host port: Bochs's slirp forwards only what
`$OS88_BOCHS_SLIRP` names, which no registered row sets, and a port that
serves a fixture inherits the base row's `locks=`.

### 18.2 What was NOT private: the SCRIPT's own scratch

A row's scratch is private against every OTHER script, which is what the
shared lane has always relied on, and against nothing else. 120 of the 296
scripts tests/megaport.txt ports write a fixed path under `build/` -
`dosram`'s `build/dosram.vhd`, which the Bochs port then MOUNTS IN PLACE
(`os88megacommon.row_disk`), `dosfix`'s `build/dosfix-run.img`,
`chartgal`'s `build/chartgal.img` - and that was safe only because the port
ran in a lane that never overlaps the shared one. In one lane `dosram` and
`mega-dosram` would build and boot one disk at once, and so would
`mega-tmselfsu` with `mega-tmselfsu-16m`, `box-hdtake` with `mega-hdtake`,
`megapixel` with `megapixelship`.

The fix is in the runner, not in 120 scripts: `_locked_lane` holds a row's
SCRIPT as an implicit lock (`lane_locks`, `script:tests/<x>.py` beside the
row's own `locks=`), so two rows of one script never run at once whatever the
script writes, and the next row's author has nothing to remember. It is a
guard by construction rather than a cure for an observed failure - the A/B
with the lock taken out ran `dosfix`+`mega-dosfix`, `dosram`+`mega-dosram`
and `chartgal`+`mega-chartgal` side by side and all six passed, the
overlapping windows missing each other - and it costs the lane nothing it
can measure: over the whole soak the largest script group is `vidpage`'s 26
rows, 1,955 declared seconds against a ~40,000 s lane at width 3, and a lock
group is started first (`_locked_lane` orders by what a lock has left), so
it is never the tail.

One race of the same kind was in the harness and is fixed there:
`os88box86api.cmos_primed` takes `build/86box/nvrcache/<hash>/` the moment
every FILE NAME exists, and wrote each file with a plain `open(..., "wb")` -
which makes the name before the bytes, so a second row priming the same
machine could boot a truncated CMOS. It stages under its own pid and renames.

### 18.3 The rule, and the runs

The lane takes a row needing ANY of `marty`, `qemu`, `bochs`, `86box`
(`tools/os88test.py` `EMULATORS`), less `builds=True` and `alone=True` as
before. No Bochs or 86Box row is kept out of it. The 51 standalone Bochs and
86Box rows (`megahiber`, `gbreplay`, ...) are untouched: they are not
`serial`, so they were never in the one-at-a-time lane - they run in the
host lane at `-j`, as 47 QEMU rows do (17 left those where they were too).

Measured on 2026-10-27 in a worktree on the four-core container, with two
other soaks running on the box (load average ~2 before these started):

| set | `--marty-jobs 1` (the old lane) | `--marty-jobs 3` |
|---|---|---|
| 19 rows: 14 Bochs ports, `mega-zonedmg-86box`, 3 `box-*`, and `fcpapi` (MartyPC) beside its port | 19/19, **371.0 s** of rows | 19/19, **122.3 s** - 3.03x |
| 32 more: 20 Bochs ports, 9 `mega-*-86box`, 3 `box-*` | - | 31/32 in 250.0 s; `box-ethernet` fails the SAME way at width 1 on an idle lane (`ethernet.py:1012`, `wins[-1]` of an empty list: B:'s window never opened), so it is the row's and not the lane's |

### 18.4 A Bochs guest's clock was the HOST's - the last place contention reached the guest

**BUILT** (docs/BOCHS.md, *The clock*). §1's finding is that contention makes
a row less thorough because its waits run on the guest's clock - and on
Bochs, until 2026-10-27, the guest's clock WAS the host's. `clock:
sync=realtime` ran the PIT off the wall clock, so a starved Bochs was a slower
machine to the guest and a stall was caught up in ticks it never executed,
and the shim's `status()["cycles"]` was host seconds of running. So kern_mega
rows failed under load and passed alone, in the guest's own terms: a
double-click read 18-50 ticks against a window of 9 (`mega-alertbtn`,
`-trkcompact`, `-bptrace`, `-int0sweep`), and Tracker's speaker calibration
refused its Play (`mega-ringmove-tracker`). The clock is `sync=slowdown` now
at Bochs's default 4M instructions a guest second - guest time is the
instruction count, read off `ptime` - and a double-click is queued on a stopped
guest (the shim's `queued_input`, `os88bochs.Box.dblclick` for the rows that
drive a Box) so its spacing is no host interval at all. With the soak running
and a CPU hog beside the rows: base 4 of 5 of those rows FAILED (load 8-16),
the fix 5 of 5 passed, and with `mega-install` and `mega-clipgrow` 7 of 7,
repeated (load 2-15). One speed was tried and refused on the way: 100M, the
CPU the rows had under `realtime`, starved `mega-trkcompact`'s ten Trackers to
1/17 of real time and fired `pace`'s host backstop (docs/BOCHS.md).
`megakbtrip`, `megapiclose` (its green arm) and `megasheet` fail at the BASE
too, unloaded and on either clock - trip 2's typed command never hands over,
the macro's message never comes - so they are not this.

## 19. A BOCHS ROW IS A QUARTER OF A LANE — the kern_mega pass ran at a load of one

**BUILT** (`tools/os88test.py` `lane_weight`, `BOCHS_PER_CORE`,
`BOCHS_WARM`, `BOCHS_IDLE`). 18 put the 307 Bochs ports into the shared
lane, and 18.4 then capped every Bochs guest at real time (`sync=slowdown`,
4M instructions a guest second, and `tools/bochs/bxclock.c` so a stop costs
the guest nothing). Together they made a Bochs row a row that spends most of
its life WAITING: `bochs-bin` sleeps in the slowdown timer whenever the guest
is ahead of the wall clock, which on a desktop that HLTs is nearly always. A
lane of four such rows on a four-core box sat at a load average of ~1, and a
pass scoped to ~388 rows, mostly `mega-*`, took ~4 hours. The lane counted
ROWS where the cost was CORES.

### 19.1 What a Bochs row costs, measured

Two instruments, both on the four-core container with another soak (and, it
turned out, another agent's Bochs rows) on the box throughout:

**The guest alone** - N machines booted at once, BIOS ticks read twice 30 host
seconds apart, `bochs-bin` + `Xvfb` CPU off `/proc` (a scratch probe, not a
row):

| guests | idle desktop: ticks/real | core/guest | busy guest (a 4M-IPS loop, no HLT): ticks/real, min-median | core/guest | box busy |
|---|---|---|---|---|---|
| 1 | 0.999 | 0.036 | 0.999 | 0.128 | ~1 of 4 |
| 8 | 0.996 | 0.033 | 0.994-1.001 | 0.132 | 2.2 |
| 16 | 1.000 | 0.030 | 0.992-1.000 | 0.120 | 3.3 |
| 24 | - | - | **0.633-0.731** | 0.104 | **3.95** |
| 32 | 0.999 | 0.028 | **0.300-0.386** | 0.133 | **3.99** |

So the guest itself is 3% of a core idle and 13% flat out, and it keeps real
time until the BOX runs out - not before. Sixteen flat-out guests beside
another soak still kept it; twenty-four did not.

**The row** - a whole kern_mega row run by hand under a process-tree sampler
(`mega-sheetdel`, `mega-vidgrey`): **0.1-0.25 of a core** steady (the
harness's reads, `xdotool`, `xwd`), and **~0.5 for its first ~5 s** - the
script's imports, the map, the disks it builds, Xvfb, Bochs starting.

### 19.2 The rule

The emulator lane is a BUDGET of `--marty-jobs` cores. A row needing Bochs
and no other emulator weighs `cpus / BOCHS_PER_CORE`; every other row weighs
1 exactly as before, so a lane of MartyPC, QEMU and 86Box rows is the old
lane. Three refinements, each from a measurement:

- **A starting row weighs a whole lane** for its first `BOCHS_WARM` = 15 host
  seconds. The first pass admitted sixteen at once: the box held 4.0 of 4
  cores for a minute while the lane filled, and in that minute two rows
  failed that are host-sensitive (`mega-vidfskeysvga4`, `mega-dosmouevt` -
  19.3 has what each turned out to be). A row's first seconds are its
  dearest, so the warm-up fills the lane in four waves of four instead.
- **A fractional row waits for `BOCHS_IDLE` = 0.75 idle cores** over the last
  3 s, read off `/proc/stat`, so it counts EVERYBODY's load - another soak, a
  `make`, an agent - and not only the lane's own weights. A lane with none of
  our rows running always admits one, so the gate cannot stall a run.
- **The pick is never passed over for size**: when the row the lane's order
  chooses does not fit, the lane waits rather than filling the room with
  lighter rows behind it, which would hold a whole-lane row out until the
  last Bochs row had run.

`BOCHS_WARM` and `BOCHS_IDLE` are HOST quantities and that is legitimate for
exactly the reason a timeout is not: they decide WHEN a row starts, never
what it does. `--bochs-per-core N` / `$OS88_BOCHS_PER_CORE` set the weight
(`tools/os88soak.py` passes it through and records it in `meta.json`;
`status` prints it), `$OS88_BOCHS_WARM` and `$OS88_BOCHS_IDLE` the other
two, and **`--bochs-per-core 1` is the old lane**, which is the A/B.

**Why 4 and not more.** The correctness argument is 18.4's: a guest capped at
real time is the same machine on any box, so packing changes nothing a row
asserts SO LONG AS EVERY GUEST STILL GETS ITS SHARE. Past that each guest
runs slower than real time. The guest-clock waits absorb that, but not every
wait is the guest's - `os88bochs.Box.until`'s and `up`'s budgets, `GLASS`,
`ui_done`'s hold, `_run_exact`'s *"under a tenth of real time"* backstop and
the serial mouse's fallback chain are host seconds - so the width
must stay UNDER saturation, not merely near it. Four a core is sixteen rows
on four cores: 1.6-4 cores of rows by 19.1 at the worst, sixteen FLAT-OUT
guests kept real time in the probe, and the idle gate covers the rest. On
the measured set it left three cores idle (19.3), and 6 a core passed too -
but twenty-four flat-out guests is where the probe saturated, so 4 is the
default that holds in the worst case rather than the typical one. 19.1's
numbers predate b46662b5, which makes a halted guest's X-GUI `select` a
poll; the post-fix runs in 19.3 read LOWER box load per row, not higher.

### 19.3 The runs

The validation set: 39 Bochs ports - `mega-stikmenu`, `-assocopen`, `-czto`,
`-vidfskeysvga4`, `-redlinechipvga-tune`, `-tpdraw`, `-dosmouevt` and 32
drawn at random - plus the standalone `lemmega`, `x3mega`, `pxmega` and
`megasheet`, which are not `serial` and run in the HOST lane at `-j` as
before (18.3), unchanged by this. `--marty-jobs 4 -j 4`, from a worktree.
"Lane" is the emulator lane's own wall time, from the last host-lane row to
the end; `mega-czto` is the set's longest row and so the lane's floor at any
width.

**On the tree with 8d57160d and b46662b5** (the serial mouse's paced
double-click; bxclock's non-blocking X-GUI `select` and the 50 ms SIGINT
re-kick), on an otherwise idle box:

| `--bochs-per-core` | rows | lane | whole run | box busy, mean (of 4) | Bochs at once, max |
|---|---|---|---|---|---|
| 1 (the old lane) | 43/43 | 601 s | 701.4 s | 0.40 | 4 |
| **4** | **43/43** | **320 s** | 427.2 s | 0.85 | 11 |
| **4** | **43/43** | **304 s** | 404.2 s | 0.66 | 12 |
| 6 | 43/43 | 260 s | 360.0 s | 0.74 | 14 |

So 4 a core is **1.9x** on the lane and the box still has three cores idle;
the lane is now bounded by `mega-czto` (~208 s) and the set's script
groups, not by width - which is why 6 buys only another 15%. Every row's
wall time is the same at every width (czto 203-210 s), which is the point:
packing changed when rows ran and nothing about how.

**Before those two fixes**, the same set beside another soak and another
agent's Bochs rows: `--bochs-per-core 1` 43/43, lane 791 s; 4 a core 43/43
(lane 392 s), 42/43, 43/43 and 42/43 over four runs. Every failure there was
one of three things the fixes have since diagnosed, and each failed at
`--bochs-per-core 1` and in that other soak too, so none was the width:
`mega-trkcompact` (a halted CPU slept the host in the X GUI's `select`, so a
Tracker guest at 8 kHz PIT ran at an eighth of real time; it failed at every
width and was left out of the set), `mega-vidfskeysvga4` (a lost SIGINT
let a 0.045 s look run a guest second), and the seven SERIAL-mouse rows
(`OS88_BOCHS_MOUSE=serial`), whose double-click was the one input still
spaced in host seconds - run as a set twice at each width they failed 4 of
14 at 1 a core and 2 of 14 at 4. An attempt here to give those rows the
lane to themselves doubled the lane's wall and still failed 2 of 7, and was
reverted; 8d57160d is the real fix.

### 19.4 What it buys the whole pass

PREDICTED, not measured: the last full pass at the old lane, 388 rows mostly `mega-*`, took 4:03:54. Of it,
the 307 Bochs ports declare 27,025 s, which four lanes take ~6,750 s at the
least; sixteen take ~1,700 s, where the floor is the largest script group,
`vidpage`'s 26 rows at 1,955 s declared (`lane_locks` runs a script's rows
end to end). So that part of a pass goes from ~2 hours to ~35-40 minutes at
the same pass rate (the 1.9x above, bounded by script groups), and the 41 standalone Bochs rows in the host lane are
untouched - several of them run two to four machines in threads
(`megavidv2`, `megavidring`, `megapiclose`, `megasweep`, ...) with
`cpus=1`, so a fractional weight would undercount them; moving them wants a
declared machine count first.
