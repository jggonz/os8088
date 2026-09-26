# Writing a test for this tree

The suite is `tools/os88test.py` over the registry in `tests/suite.py`, and
`tools/os88soak.py` runs the whole of the soak tier in parallel
(docs/plans/SOAK-PARALLEL.md). This file is how to add a row without
reinventing what already exists, and without producing one of the four
failures that keep recurring: a `secs` nobody measured, a `builds=True` where
`wants=` or a private tree was the answer, a hand-rolled click at a remembered
coordinate, and a `time.sleep` that hands the guest less work under load.

docs/TESTING.md is the other half — what each emulator *can* show — and
docs/MARTYPC-DEBUG.md is the instrument's manual.

---

## 1. Before anything: can this row FAIL?

Write the failing case first. It is the only question that decides whether the
row is worth having: a red row gets investigated and a green one does not, so
a row that cannot fail is worse than no row.

Two rows sat green for months doing nothing. `dispcp` is a LIBRARY imported by
over a hundred test files; registered as a soak row it booted nothing,
asserted nothing and reported `ok` in 0.1s against 60 declared. `mkclick` is a
GENERATOR — it writes `build/click.mod`, a metronome for judging A/V sync by
ear — and reported `ok` in 0.0s against 10. Both are in `t_registry`'s
`UNREGISTERED` map now, with the reason.

The runner reports `UNDERRAN n% of its declared Ns` for a row that returns in
under 5% of its declaration (for declarations of 5s or more), which catches the
crudest version. It does not catch a row that boots a machine, drives a session
and then compares something that is true either way.

**So: break the thing on purpose and watch the row go red.** Comment out the
kernel line, poke the byte, feed the wrong fixture. If you cannot make it fail
in five minutes, the assertion is not about what you think it is.

The corollary is the positive control. `lzfence` is the model: hostile streams
that must be REFUSED, and a decoder that refuses everything passes all of them
— so a valid stream runs first and its twelve bytes are compared one by one.

---

## 2. Which tier

| tier | budget | what belongs there |
|---|---|---|
| `fast` | **30s, enforced** | host-side only: read what `make` just built and check an invariant that breaks silently. Runs as part of every `make`. |
| `full` | **180s, enforced** | one question: *did you obviously break the OS?* Does it compile, does it boot, does it do the basic things, is anything critical gone. |
| `soak` | **none, deliberately** | everything else. Where a row goes when it is worth having and does not fit the gate. |

The budgets are `BUDGET` in `tools/os88test.py`, and the runner FAILS a tier
that overruns one.

**Choose the tier by what the row costs and how broadly it fails, never by
how important you think it is.** The two expensive tiers are not run per
commit — `full` runs when a major round of work reaches the integration
branch, and `soak` is scoped to the rows a change can REACH, the whole tier
running only when the owner asks for it (docs/TESTING.md, *When to run which
tier*) — so a row put in `full` to make sure somebody sees
it is a row that runs LESS often than you imagine, and one put in `soak` is
still run by the person who touched its subject, which is who it is for.
`soak` is a real answer and costs nobody any budget.

A `full` row earns its place with **breadth per second**: `bootsmoke` is about
thirteen seconds for a boot to a desktop on both 1bpp adapters, and fails for
almost any serious regression, wherever it was. A row that can only fail for
one narrow reason belongs in `soak`, next to the change that would break it.
Three minutes is about four emulator rows, not fifty — §2.2 is the rest of
that rule.

`soak` having no budget is not an oversight. A budget there would push rows out
of the suite, which is the opposite of the point.

### 2.1 What earns a `fast` row

**`fast` is the one tier nobody opts into.** `all` depends on it, so every
second of it is charged to the contributor who is *not* working on its subject
and has never read the code it defends. The question is therefore never "is
this check worth having" — every row in `tests/` is — it is **"is it worth
having to somebody who did not touch this?"**

Two questions retire a row from `fast`. Either one on its own is enough.

**1. Is it about ONE package or ONE driver?** Then it is `soak`. Whoever
changes SKIES tests SKIES, and charging every other contributor two seconds a
build for it buys them nothing. `soak -k 'cs*'` is one command and it is run
by the person it is for. This is the rule that retired `csworld`, `csworlds`,
`csart`, the three `fr*` rows, `inktab`, `wab`, `lmpack`, `drvmem`, `sfx` and
`stknosave`.

**2. Can only a KERNEL change break it?** Then it is `soak` too. A row about
how the kernel works *inside* — a `.bss` sentinel, `.lowbss`'s order, the
purgeable eviction ranks, `clk_mlen`'s month mask — is defended by whoever
edits that subsystem, and that is exactly the person who will run `soak -k` on
it. `fast` is not where the kernel is proved to still work; it is where a
writer who has never opened that subsystem is caught breaking it by accident.

> **The exception is what makes the rule useful.** A kernel-side row stays if
> code OUTSIDE the kernel can reach it — a package, a driver, the SDK, or a
> Makefile recipe. `api-abi` reads `kernel.bin` and stays, because the other
> end of it is `apps/os88api.inc`. `drvovl` reads the kernel's overlay rule
> and stays, because what broke it was a *Makefile recipe* gaining
> `$(PKGZARG)`. Ask who else's file has to agree, not which directory the
> test reads.

**And one row stays for what it PRINTS.** `kernbudget` is kernel-internal by
any reading of rule 2. It costs 28ms and puts `KERN_BUDGET big <n>, small <n>`
on every build, which is how kernel size drift stays visible between one
person's commits and the next — so a row may earn `fast` by what it puts on
the *screen* as well as by what it catches. The bound is that it must be
effectively free and the number must be one the project actually steers by.
It is the only such row, it is the owner's call, and it is written down here
so the rule above does not evict it again.

What survives is four families, and a new `fast` row should be able to name
the one it is joining:

| family | what it is | some of its rows |
|---|---|---|
| **The boundary** | the kernel and the code loaded onto it, edited by different people, with neither side's build saying they disagree | `api-abi`, `stkclass`, `drvovl`, `fonts`, `pkgdeps` |
| **Rules over every line** | what any assembly in this tree must obey, kernel and package alike | `asmrules`, `ovlchk`, `textrules`, `stkbalance`, `stkapps`, `swallow` |
| **The shipped artifacts** | the floppies themselves, which anybody adding a file to one reaches | `image`, `pkg`, `diskverify`, `canary`, `checkreadme` |
| **The tree and its suite** | duplicated constants, generated docs, and the gates' own integrity | `mirror`, `checkdocs`, `docindex`, `registry`, `machines`, `qemuown`, `fixtures`, `layout`, `stkwalker` |

The membership is whatever carries the tier `fast` in `tests/suite.py`; the
families are how to argue about a new one. If your row joins none of them,
that is the answer.

**Moving a row DOWN is not deleting it.** Every row named above still runs,
still fails the same way, and is still one `-k` away. What changes is who pays
for it: the person who touched its subject, instead of everybody.

### 2.2 What earns a `full` row

**`full` asks one question: did you obviously break the OS?** Does it compile,
does it boot, does it do the basic things, is anything critical gone. It runs
when a major round of work reaches the integration branch — not per commit —
so it is a smoke test with reach, not a survey.

**The budget is 180s, and that is a target for four lanes on an ordinary box.**
A slower machine is expected to take longer; what the ceiling stops is the tier
growing until nobody runs it. Today it uses 60.3s in the runner and 75.3s of
wall on a cold four-core container (37.8s / 51.1s warm), so there is room.

Three rules, and each retired a row when the tier was recut from 14 to 5:

**Nothing app-specific.** A package is not the OS. A row may *drive* an app as
the vehicle for a generic check — `ctoolchain` builds four C packages because
that is what a toolchain produces, and the row is about the toolchain — but a
row whose SUBJECT is one program belongs in `soak`. That moved `weavesmoke`
(72.8s, WEAVE opening a bundle) and `appsmall` out.

**A kernel check is allowed, and should be short.** `kernresident` boots a VGA
machine and walks `mem_tab` in 13.7s; `small128` builds the second shipped
kernel and boots it on the 128KB floor machine. That is the shape. A deep sweep
of one subsystem is not, however true it is — `smallboot` walked three adapters
for 118s where `small128` beside it already proves that kernel builds and
boots.

**The subject is the OS, not the tree and not the suite.** `buildmatrix`'s 99
knob configurations are instruments; `bmshare` and `kernmods` are about
build-speed variables and a size report; `martyconc` gates the emulator harness;
`stackprose` reads prose. Every one is worth having and none can answer this
tier's question — and `buildmatrix` alone was 143s of a 180s budget.

> **What that costs, said plainly.** The knob `%ifdef` arms now assemble at
> `soak` cadence rather than at every integration merge, and `kern_emu` is
> built by no tier at all (`make emu` and `soak -k 'vmmouse'` build it). That
> is the trade the three-minute target buys. `kern_small` is unaffected —
> `small128` builds it every run.

The five that remain, and the part of the question each answers:

| row | answers |
|---|---|
| `bootsmoke` | does it boot to a desktop, on both 1bpp adapters |
| `kernresident` | does it boot on VGA — and does `kern_big` still fit 128KB at the desktop |
| `small128` | does the second shipped kernel still compile, and boot on its floor machine |
| `ps2mouse` | do the mouse and the keyboard still work |
| `ctoolchain` | does the C toolchain still produce a package |

---

## 3. The row

```python
Row("lzmod", "soak", py("tests/lzmod.py"), 30.0,
    "SPEC.md 20.14.5: BEVERLY.MOD, COMPRESSED, opened by a double-click. "
    "... All 116,085 bytes are compared BYTE FOR BYTE, because a decoder "
    "that got one match wrong across the boundary still opens a window, "
    "still shows the title, and still plays - it plays a click",
    needs=("marty",), serial=True,
    wants=("build/lzmod360.img",)),
```

| field | rule |
|---|---|
| `name` | what `-k` matches (an `fnmatch` glob). Short, and the same as the script where there is one. |
| `tier` | §2. |
| `cmd` | `py("tests/x.py", "--flag")`. Run with `cwd=ROOT`, so paths are ROOT-relative. |
| `secs` | **measured**, not guessed. §4. |
| `why` | what breaks if this row goes red, with the § that owns it. Written for somebody who finds it failing in a year and does not know what it was defending. |
| `needs` | capabilities, PROBED not configured: `marty`, `qemu`, `nasm`, `nasm3`, `cc`, `wiredisk` (`capabilities()` in the runner). A missing one SKIPS the row, and a skip is the box declining to answer — never a pass. `nasm3` is a second assembler and not a newer one: it is read out of `nasm -v` (`os88build.nasm3()`), so a `nasm3` on PATH that is a symlink to 2.16 is absence. |
| `serial` | it drives an emulator. The runner keeps those in their own lane, `--marty-jobs` wide (default cores−1); the host-side rows fan out ahead of them. Forgetting it puts an emulator row in the host lane, where it competes with every other row on the box. |
| `wants` | build artefacts this row OPENS that `make all` does not produce. §5. |
| `builds` | it shells out to `make` and writes `build/`. **You almost certainly do not want this.** §5. |
| `alone` | its ANSWER needs the cores — a rate, a frames-per-second, a millisecond redraw. Not the same claim as `builds`. §4.1. |
| `timeout` | defaults to `max(60, 4 × secs + 30)`, generous on purpose — it is there to stop a hung emulator eating the tier, not to police a slow box. Raise it for a row whose *shape* is slow, never to paper over a wait. |

---

## 4. Declaring `secs`: measure it

**Run the row and write down what it took.** That is the whole procedure, and
it is skipped constantly. The compression family arrived declaring 2,721
seconds for fourteen rows that take 701 — every declaration honest when
written, against a shape where the row's own cost was two full builds, and
none revisited when that stopped being true.

A wrong declaration is not cosmetic. `secs` is what the runner schedules by,
what `--resume` prices a restart against, what the ETA is computed from, and
what `timeout` is derived from. A row declaring 600 and taking 30 makes the
soak's own estimate useless, and a row declaring 30 and taking 600 gets killed.

* The runner reports `(declared Ns)` when a row exceeds **2× + 1s** (`SLIP`),
  which is there to catch a row that got 3× slower, not to police a loaded box.
* It reports `UNDERRAN` below **5%** (`UNDER`), which is there to catch the row
  that ran nothing.
* Between those two it says nothing, so the number is yours to keep honest.

Include a private tree's build if the row builds one — a cold tree is ~40s on
this container — and measure the rest warm, which is what it costs on every
run after the first.

### 4.1 `alone` is about cores, not the tree

A row whose assertion is a RATE cannot share four cores with two other guests.
That is not flakiness, it is the wrong measurement. `saverate`, `deskbench`,
`uilat` and `curdisk` carry it.

Everything else should NOT: guest cycle counts, `disk()` counts and pixel
comparisons are exact at any oversubscription, because they are counted rather
than timed. If your row needs `alone` and is not measuring a rate, what it
actually has is a host-clock wait — see §7.

---

## 5. Never build in `build/`

`builds=True` is **three** rows (`buildmatrix`, `ctoolchain`, `fdlgthumb`).
Do not add a fourth without reading this section and deciding it applies to
you.

The hazard is reproducible in about twenty seconds: three rows that pass 3/3
against a frozen tree pass **0/3** against `build/` with a `make VIDEO=cga`
loop running, all three dying with *"the map describes a DIFFERENT kernel"*.
One row's mid-run `make` once cost a soak nine rows in a four-minute window.

There are exactly two legitimate needs, and each has its own answer.

### 5.1 A fixture: `wants=`

An image, package or driver under `tests/` that `make all` does not build.
Declare the PATH; the runner builds every declared artefact before any row
starts, when nothing else is running. An artefact that will not build is a
capability gap: the rows that named it SKIP, and the rest of the run goes on.

```python
    wants=("build/lzmod360.img",)
```

and in the script:

```python
from os88fixture import need
need("build/lzmod360.img")       # `all` builds nothing under tests/
```

`need()` is a no-op when the runner already built it, and under the runner an
UNDECLARED target is a hard error naming the row — because `need(DISK)` and
`need(a.apps)` are as common as a literal, and no static check can see through
them.

Make the Makefile own the dependency graph. A fixture embeds the SDK, so one
cached against an earlier kernel is a stale-scratch-disk trap (a stale
`build/c64.bin` once survived a cherry-pick and `c64part` failed "not
byte-identical", which reads as a broken package); make's rules already name
those includes as prerequisites.

**IT IS A PATH, NEVER A MAKE TARGET, and getting that wrong is a row that
SKIPS FOR EVER.** The runner builds each entry with `make <entry>` and then
asks `os.path.exists` on it, so a PHONY target compiles perfectly, exits 0,
and is still reported as an artefact that "would not build" — after which
every row naming it skips, on every run, for ever. `btngesture` landed with
`wants=("marty",)`, which is the emulator CAPABILITY and belongs in `needs=`;
it skipped from the day it was written and had never once run. **Nobody
investigates a skip**, which is §1's rule wearing different clothes: a row
that cannot fail is worthless, and a row that cannot RUN is worse, because
the suite reports it as a line of output either way.
`tests/unit/t_registry.py` checks it now — an entry with no `/` in it is a
build failure naming the row.

**NEVER ON A FAST ROW, and the reason is a fork bomb rather than a
convention.** The fast tier runs as part of `make all`, and the prebuild that
satisfies `wants=` opens with a plain `make` — so one `wants=` on a fast row
puts a `make` inside a `make`, which re-enters `all`, which runs the fast
tier, which reaches the prebuild again. Measured, it took a container to
hundreds of nested makes in about a minute, with no error to read: the tree
just stops building. `tools/os88test.py` refuses to build anything when
`$MAKELEVEL` says it is already inside one, which turns the recursion into a
skip, but the row was wrong to declare it either way — **`wants=` is for what
`make all` does NOT build**, and anything a fast row can open, `all` has
already made.

### 5.2 A different KERNEL: `os88build.tree()`

A knob build, `kern_small`, a `COMPRESS=` or `PKGZ=` set — anything where the
kernel under test is not the shipped one.

```python
import os88build

t = os88build.tree("NOPLANE=1").apply()          # builds, or reuses
with os88marty.launch(t.img("os8088-360.img"),
                      apps=t.img("apps360.img")) as m:
    ...
```

`tree()` builds into `build/trees/<knob>-<hash>/` (default targets
`os8088-360.img` and `apps360.img`; pass `targets=` for others) under a
per-tree `flock` held across the build and no longer. Two rows with different
knobs never meet; two with the same knob share one tree and the second waits
only for the first's BUILD. `build/` is never written, so a person can `make`
in the checkout while a soak runs.

Four things to get right:

1. **`.apply()`, not `env=`.** `os88marty.launch` takes no `env` argument — the
   emulator needs no environment, the SYMBOL READER does. `t.env` is for a
   subprocess you spawn yourself; inside one process `apply()` sets os88sym's
   module default *as well as* the environment, and that second half is what a
   row cannot do for itself: library helpers that take no `defines`
   (`no_saver` resolving `ss_idle`, `Marty.sym` resolving whatever it is asked)
   go to the module default. Setting only `OS88_BUILD` fails exactly where the
   row is not looking — `blitcut` died inside `no_saver`, three frames below
   its own code.
2. **Never restate the nasm defines.** A knob's make VARIABLE and its nasm
   DEFINE differ (`VGADIRTY=1` compiles `-DVGA_DIRTY`), and a name without its
   value re-assembles a different kernel. `tree()` asks `make -n` for them.
3. **`plain()` is the other arm of an A/B**, and it resolves through
   `$OS88_BUILD` — so under a frozen run the control arm reads the run's tree
   and not the operator's directory.
4. **`make -n` is not a dry run of the PARSE.** `$(VIDSTAMP)`'s rule deletes
   `$(BUILD)/kernel.bin` and every boot sector when the knob set differs, so a
   `make -n` with a knob in it pointed at `build/` is a DESTRUCTIVE command.
   This is also why `os88fixture.need()` may not be called from a knob gate.

### 5.3 Check whether you need a build at all

Ask what actually differs before reaching for a tree. Four of the eight
compression rows that shipped with `builds=True` needed nothing but the
shipped kernel and a fixture: `lzmod-lzb` built a whole kernel under a knob to
test the LZB decoder, and the shipped kernel carries both decoders (SPEC.md
20.13.6), so the two arms came out byte-identical and the only real difference
was which format the FIXTURE was wrapped in.

### 5.4 Reading `build/` from the host: `os88build.at()`

A row that opens a shipped artefact on the HOST — to compare it against what
the guest holds — must resolve the path:

```python
plain = os88drv.image_unwrap(open(os88build.at("build/ramdisk.drv"), "rb").read())
```

`os88marty.launch` and `scratch_disk` already do this for the paths they are
handed, and `os88sym` honours `$OS88_BUILD`. With the variable unset `at()` is
the identity function, so an interactive run is unchanged. Without it, under a
frozen soak, a row asserts about one build's fixture and boots another's.

---


### 5.5 A path your row WRITES is yours alone, and `build/` is not

§5 is about not building in `build/`. This is the other half: a row's own
scratch ARTEFACT at a fixed path is shared with every copy of that row, and
the runner runs three at once.

`os88marty.launch` copies each **floppy** into the instance's private run
directory, which is what makes most rows safe without thinking about it. It
copies nothing else. `extra=["--mount", "hd:0:" + VHD]` goes to the emulator
verbatim, so `tests/hibernate.py`'s fixed `build/hiber.vhd` was one hard disk
mounted read-write in three machines, each rebuilding it from the template
under the others.

**What makes this worth its own rule is how it presents.** It does not fail in
one place. It fails at whichever step happens to collide, so it reads as
several unrelated defects — a boot that never reaches a desktop, a file the
host cannot see, and a click that is *proven* at the pointer and *proven* at
the guest's own `mouse_btn` and still does nothing, which reads as a broken
hit test in the product. Three rounds of investigation went into the first
two before the third made the shape obvious.

Key anything you write to the process (`os.getpid()`) or take it from the
instance's own run directory, and sweep it on the way out — a per-PID name
cannot be reclaimed by the next run the way a fixed one was, because `pid_max`
wraps in minutes on a busy box.


## 6. Driving the machine: `tools/os88ui.py`

**Start here, not at the mouse.** Every position is resolved from the guest's
own live tables and every verb is CONFIRMED by reading guest state.

```python
import os88ui

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img") as ui:
    w = ui.path("B:/APPS/CALC.O88")       # drive, folder, package - all checked
    ui.menu_pick("Calc", "Close")
```

The drive prefix matters on a fresh boot: without one, `path()` starts from
the ACTING Disk window, and there is none until something has opened one.

The verbs: `open_drive`, `open`, `path`, `window`, `wait_window`, `windows`,
`titles`, `front`, `raise_window`, `move_window`, `drag_window`, `close`,
`uncover`, `clear_desktop`, `disk_window`, `listing`, `entry`, `scroll_to`,
`menus`, `menu_pick`, `toast`, `wait_toast`, `fs_of`, `settle`. `ui.m` is the
Marty and `ui.mo` the mouse, so dropping a layer needs no second connection.

Three rules make it worth using:

1. **Nothing is aimed at a remembered coordinate.** `dsk_vtab` says which zone
   a drive owns, `wm_wins` where a window is, the staged listing which row a
   name is on, `menu_bar[]` where a menu title sits. A layout change moves them
   all at once. A row number is not a file: SPEC.md 19.4 sorts by name, so a
   folder that gains one entry renumbers every row after it.
2. **Every verb confirms by reading guest state**, which is FASTER than
   settling — polling `wm_wins` is a 408-byte read against a whole framebuffer.
   The cheap thing and the correct thing are the same one.
3. **A verb that cannot confirm RAISES**, naming the step and what it saw. The
   failure is reported where it happened rather than twenty steps later wearing
   the costume of the feature under test.

Measured (`tests/uilayer.py`): the same three-step navigation is **0.35×** the
host time and 0.35× the guest cycles of the settle-and-hope spelling.

`boot()` also does the two things scripts forget — it turns the screen saver
off (five GUEST minutes of no input is reachable in a wide lane, and what a row
then compares is a black screen; `saver=True` keeps it for a row whose subject
it is) and resolves an IBM machine name to its GLaBIOS twin unless the row
makes a case in `why_ibm`. A row that calls `os88marty.launch` directly gets
neither, and one that hardcodes an IBM ROM name runs on a box that has it and
silently not on one that does not.

**The mouse under it is `tools/os88mouse.py`** — absolute, closed-loop, reading
the cursor back rather than dead-reckoning, with `dblclick` as a verb of its
own. `tools/os88mouserel.py` is the RELATIVE one and is for a short list: the
mouse itself under test, a bit-exact replay, or motion with no destination (a
paint stroke, a window drag). A dead-reckoned click that misses raises nothing.

### 6.1 When a hand-rolled click is right

Rarely, and always for a stated reason:

* the widget is not a window, a menu or a listing row — a Control Panel radio
  button, a scrollbar arrow, a game board cell. Derive the position from the
  window rectangle and the layout constant, never from a screen coordinate;
* the row IS testing the pointer, the driver or the packet stream;
* QEMU, where `os88ui` does not run (`tests/dispcp.py` keeps a blind path for
  exactly that).

Even then, confirm afterwards by reading the state the click was supposed to
change.

### 6.2 A breakpoint and a UI verb: `os88marty.bp_trace`

**They cannot be spelled one after the other**, and the reason is rule 2 above
turned against you: every verb confirms by READING GUEST STATE, and a guest
stopped at a breakpoint publishes nothing new. So an armed breakpoint does not
send a click to the wrong place - it makes the click's own proof unobtainable,
and the verb reports a machine that refused to go where it was sent.

Put the breakpoints in a `bp_trace` block and the body is ordinary code:

```python
with os88marty.bp_trace(m, "wm_su_try", "gfx_restore") as tr:
    ui.raise_window(w)                  # os88ui verbs, unmodified
assert tr.count("wm_su_try") == 1
```

**Stay in the block until the work has RUN** - `tr.until(cond, what)`. A `with`
block ends when its body ends, and a gesture returns when it is DECODED, not
when the repaint it triggers has finished. Leaving there clears the
breakpoints first and the row reports the kernel never doing the thing.

The pump runs on a daemon and resumes at every hit. `regs=True` records a
register set at each stop; `on_hit=f` is called while the guest is STOPPED and
its answer kept, which is the only way to read a value that is true only
inside the routine the breakpoint is on - a damage rect, `wm_clip_n`, a return
address off the guest's own stack. Do NOT hand-roll the pump: it has two traps
that have each cost a run, and `bp_trace` carries both fixes
(docs/MARTYPC-DEBUG.md, *Driving the UI with breakpoints armed*).

**Arm the narrowest symbol that answers the question.** Every hit costs two or
three round trips plus the pump's poll interval of stopped guest, so a
breakpoint on a hot symbol runs the machine at a fraction of its speed and the
wait around it fails on its host backstop. When one packet is the whole
gesture, `tools/os88span.py`'s arm-late pattern is cheaper still.

---

## 7. Waiting: the guest's clock, never the host's

**`time.sleep` in a row holding a Marty is a defect.** `time.sleep(2)` means
"give the guest whatever two seconds of this box buys today" — on this
container that is 9.6 guest seconds idle and 2.4 with the box full. The same
test hands the machine four times less work under load and fails further on,
looking like the thing under test.

That is the soak's central finding (docs/plans/SOAK-PARALLEL.md 1): twelve
rows at width 1 and at width 3 with two extra CPU hogs were 1.06× slower and
12/12 passed in both arms, while the same waits cost the guest up to **37%
less work**. **Contention does not make a row slow, it makes it LESS
THOROUGH** — which is why wall times never show it.

Five tools, in the order to reach for them:

| want | use |
|---|---|
| a specific thing to HAPPEN | `os88marty.until(m, cond, what, poll=…, guest=N)` |
| guest state to STOP CHANGING | `os88marty.quiesce(m, read, guest=…, stable=…, budget=N)` |
| the SCREEN to stop changing, before a pixel comparison | `os88marty.settle(m)` |
| the UI to have FINISHED with a click, key or drag | `os88marty.ui_done(m)` |
| time to pass, and nothing else will do | `os88marty.guest_sleep(m, N)` |

All five anchor their deadline to the emulator's own cycle counter, so a loaded
box does not shorten what the wait allows — and a guest that has STOPPED
executing fails in ~2 seconds naming the machine (`it is 'paused' at
0060:3C19`) instead of sitting out the whole budget.

The harness's own pauses - `settle`'s stillness window, a click's settle, the
gap between mouse packets - are GUEST time: `os88marty.pace(m, secs)` spends
what `time.sleep(secs)` bought on an idle box (`GUEST_PACE`, 4.5 guest seconds
a second, measured), whatever the box is doing. Use it yourself only where
there is genuinely nothing to wait ON; 7.1 below is the rule. And a mouse
verb called WITHOUT `settle=` no longer pauses at all: it waits for
`ui_done`, capped at the pause it used to spend (docs/MARTYPC-DEBUG.md,
`settle`) - so pass a number only when the next step needs more than the UI,
a worker's result or a package's own timer, and then prefer waiting on that
state.

### 7.1 Wait for the thing, not for a duration

```python
#   NO
for _ in range(30):
    time.sleep(1)
    if len(win_list(m, S)) > n0:
        break
time.sleep(20)                      # "give the load time"

#   YES
os88marty.until(m, lambda mm: len(win_list(mm, S)) > n0,
                "Tracker's window to open", poll=0.2, guest=40.0)
os88marty.until(m, claimed, "Tracker to claim the module",
                poll=0.2, guest=60.0)
```

`[trk_modseg]` going non-zero IS "the 116KB expanded and Tracker holds it". The
wait then costs what the decode costs, and on a slow box it waits LONGER rather
than reading a zero and blaming the decoder.

`for _ in range(N): if not served(): advance()` has the same defect in a
different costume — it gives the guest fewer rounds exactly when more is
happening.

### 7.2 Pick a signal that means what you think

`lzdrv` waited on `DRVR_SEG` going non-zero and called it "the driver
attached". `drv_load` writes that word the moment `mem_claim_hi_x` answers —
**before** the file is read, checked, expanded, its bss re-made and
`drv_attach` far-called. The wait returned three quarters of the way through a
load. `drv_owner` for the class is the signal, because `drv_publish` is reached
from `drv_attach` and nothing else writes it.

Read the kernel and find the write that happens LAST.

**And a CONFIRMED INPUT is not a confirmed GESTURE — the same rule one layer
down.** `os88mouse` proves every packet it sends: `to()` against the published
cursor, `_edge()` against the guest's own `mouse_btn`. Neither proves that
anything *acted*. `mouse_btn` is a LEVEL that `mou_isr` sets; what the UI acts
on is an `EVT_MDOWN` in the ring, and SPEC.md 10.1 says what happens when that
ring is full — `evq_push` drops a record. So a press can be confirmed at every
layer the mouse has and still be a gesture that never happened, which is
`hdboot` at a lane of four: pointer confirmed at (199,10), button confirmed
down, and no menu on the screen after **182 ticks**.

Wait on the state the gesture is FOR — `menu_dropd` for a menu, `ui_dragwin`
for a drag — and let `os88ui.UI._edge_until` do the pressing, because the
recovery is a **fresh edge** and not a longer wait or a re-sent packet: a
Microsoft packet carries the LEVEL, so re-sending says what the guest already
believes.

### 7.3 `settle` is expensive and often the wrong question

`settle` is **48% of a row**, and two-thirds of every settle is its own floor:
`stable` identical captures `quiet` apart is `stable × quiet` = 2.0 HOST
seconds at the defaults before it can return, and that cannot come down (a
change arriving after one whole quiet round happens 1 time in 19, so halving
it would end one settle mid-repaint per 48).

**The floor is now paid only when the machine is BUSY.** Those gaps are
repaints and loads in flight, and `settle` reads that directly: while
ui_task is asleep with nothing queued, locked or read off the drive
(`os88marty.ui_idle`), its interval is 0.2 guest seconds rather than
`quiet` at `GUEST_PACE`, and the full window is kept for the intervals where
the UI is still working. Measured on `dispcheck`, the settles after a gesture
fell from 9+ guest seconds to 0.8-0.9 each; a display-mode change still took
its full 5.5, because the UI was repainting for all of it.

So the way to spend less is still not to settle when pixels are not the
question. `quiesce` is settle's shape
over a handful of bytes instead of a framebuffer, and over guest seconds
instead of host ones — `dispcalc` went **376.3s → 197.2s** with every
assertion still passing, because one line was 30% of all its settle time and
what it was waiting for was thirteen bytes of a composition buffer.

`settle` remains right when the next line compares PIXELS. It is positively
wrong for an operation that holds the gfx lock: the screen is *more* still
while such an operation is busy than when it is finished, so a settle returns
five seconds into a hard-disk install and the `with` block kills the emulator
mid-copy. `until` on the commit is the answer there.

`OS88_WAITLOG=<path>` records every wait with its CALL SITE, which is how the
totals become a work list.

---

## 8. Asserting: guest state over pixels

Read the byte the feature actually writes. It is exact, it is cheap, and it
survives a layout change.

* `mp_loaded`, `trk_modseg`, `ld_status`, `np_len`, `drv_tab` — a number that
  is right or wrong.
* A whole image compared BYTE FOR BYTE where the subject is bytes. A decoder
  that got one match wrong still opens a window, still shows the title, and
  still plays — it plays a click.
* The HOST side of a mounted image, where the commit is a single write.

Compare pixels when pixels ARE the subject — a redraw, a glyph, a greyed
control, a straddled window — and then say which pixels and why. Three defects
are invisible in an emulator and cost this project bug after bug: a visible
redraw, a double-draw flash, and input overrun. None showed in a screendump;
every one was found on hardware or by counting.

**Ask the kernel for its own constants.** `lzship` walked `drv_tab` on a stride
of 10 where `DRVR_SIZE` is 16, over sixteen rows where `DRV_MAX` is five — 256
bytes of an 80-byte table, on a stride matching no field. It never said so,
because the only assertion was `live < 1`. `os88sym.equates()` answers both,
and answers them correctly on `kern_small` too (`DRV_MAX` is 4 there, and 6 on
`kern_emu`).

**And confirm that what you are reading has been WRITTEN.** `lzdrv` read
DRVCALL's probe strings straight after the window opened. `dc_probe` runs from
`dc_paint`, and the window's FIRST paint does not reach the driver — so the
three lines were still the image's own `Ping: ..`. Alone the row won the race
and passed; with a second emulator on the box, both the row under test and the
row at HEAD read `Ping: ..`, which looks exactly like a driver that failed to
expand.

---

## 9. Which emulator

**MartyPC is the default instrument. QEMU is a fallback with a closed list**
(docs/TESTING.md owns it):

1. 286 and 386
2. rung 1 of the hard-disk driver — gated on `CPU_286`
3. SPEC.md 9.5's awkward mouse cases — COM2, the cross-wired IRQ4 card, a modem
4. the PS/2 mouse — MartyPC is an 8088
5. the Ethernet card — MartyPC has no NIC of any kind
6. the RTC ladder's WRITE half — a 5150 has no real-time clock
7. the VMware absolute pointer (SPEC.md 9.11) — MartyPC has no backdoor

"It is quicker to type" is not on the list. Everything that runs on an 8088 —
all three adapters, input, screenshots, sound — is MartyPC, which agrees with
the field machine to 0–4% on 45 of 47 `gfxbench` rows.

Several instances run at once and nothing has to be arranged between them:
`launch()` gives each its own port, run directory and disks. Take the address
off the object (`m.addr`), never type 9001.

---

## 10. Output

One row, one verdict, on stdout, flushed. The runner prints a row's output only
when it fails or under `-v`, so write for somebody reading a failure.

```python
def say(*a):
    print(*a, flush=True)

fails = []
...
if got == want:
    say("  bytes      ok  (all %d, byte for byte)" % len(want))
else:
    bad = [i for i in range(len(want)) if got[i] != want[i]]
    fails.append("%d of %d bytes differ, first at %d - which is %s the 64KB "
                 "boundary" % (len(bad), len(want), bad[0],
                               "past" if bad[0] >= 0x10000 else "before"))

for f in fails:
    say("  FAIL: " + f)
say("lzmod: %s" % ("FAILED" if fails else "ok"))
return 1 if fails else 0
```

* **Collect failures, do not raise on the first.** A renumbering breaks fifty
  slots and the useful report is all fifty.
* **A failure message carries the numbers on either side of it** — the offset,
  the byte, the symbol it resolved to. "expanded WRONG" is not actionable;
  "%d of %d bytes differ, first at %d, which is past the 64KB boundary" is.
* **Exit non-zero on failure.** That is what the runner reads.
* Host-side rows under `tests/unit/` use `harness.check(cond, what, why=…)`
  (or `harness.eq(got, want, what)`) and `done("t_name")` instead, which does
  all of the above and adds `why=` — say what breaks, for the reader who finds
  it red in a year.

A row that hits a condition it cannot test should `sys.exit("…")` with a
sentence, or declare `needs=` so it SKIPS. Never report a pass about a machine
it never ran on.

---

## 11. Register it, or exempt it with a reason

`tests/unit/t_registry.py` requires every file under `tests/` to be in
`tests/suite.py` or in its `UNREGISTERED` map with a reason.

**The reason matters more than the exemption.** "Needs a build prerequisite"
and "needs hardware nothing here has" are facts a reader should find without
running it; an unexplained exemption is how a test that has simply broken gets
filed as one that was never meant to run.

`t_registry` also checks `builds=True` against the script rather than trusting
it — a row that gains a `make` and not the flag is a suite that fails one run
in five for no visible reason — and `BUILDS_WITHOUT_MAKE` is where a row that
writes `build/` by some other route says so.

Then run the gates:

```
python3 tools/os88test.py --list                  # the registry as the runner sees it
python3 tools/os88test.py fast                     # 30s, and part of every make
python3 tools/os88test.py soak -k '<yourrow>'      # your row, alone
python3 tools/os88test.py soak -k '<yourrow>' --marty-jobs 3   # ...and loaded
make test-full                                     # only if this row is a `full` row
```

**The loaded run is not optional for an emulator row.** Passing alone and
failing in a wide lane is the single most common way a new row lands broken,
and §7 is why.

---

## 12. Checklist

- [ ] I broke the thing on purpose and watched the row go red.
- [ ] `secs` is a number I measured, not one I guessed.
- [ ] No `builds=True`. A fixture is `wants=`; a different kernel is
      `os88build.tree(...).apply()`.
- [ ] No `time.sleep` anywhere near a Marty. Every wait is `until` / `quiesce`
      / `settle` / `guest_sleep`, on the guest's clock.
- [ ] Every wait is on a signal that means what I think — I found the write
      that happens LAST.
- [ ] Navigation is `os88ui` verbs, by name. Any hand-rolled click has a
      comment saying why and a confirmation after it.
- [ ] Assertions read guest state, or say why pixels are the subject.
- [ ] Kernel constants come from `os88sym.equates()`, not from my typing.
- [ ] Host-side reads of `build/…` go through `os88build.at()`.
- [ ] The row writes nothing under `build/`.
- [ ] Failures are collected, carry their numbers, and the script exits
      non-zero.
- [ ] `needs=` names what it cannot do without, so a box that lacks it SKIPS.
- [ ] Registered in `tests/suite.py` with a `why` that says what breaks.
- [ ] If I put it in `fast`: it is not about one package or one driver, and
      something outside the kernel can break it (§2.1).
- [ ] If I put it in `full`: it helps answer *did you obviously break the OS*,
      it is not about one package, and it is short (§2.2).
- [ ] `make test-full` is green, and the row passes at `--marty-jobs 3`.

---

## 13. The failures this document is made of

Kept as a list because the abstract rule is forgettable and the incident is
not. Each one can still happen today.

| # | what happened | rule |
|---|---|---|
| 1 | `dispcp` (a library) and `mkclick` (a generator) registered as rows: `ok` in 0.1s and 0.0s against 60 and 10 declared | §1 |
| 2 | Fourteen compression rows declaring 2,721s and taking 701 | §4 |
| 3 | One row's mid-run `make` opened a four-minute window that cost nine rows | §5 |
| 4 | Three rows, 0/3 against `build/` under a knob hammer, 3/3 against a tree | §5.2 |
| 5 | `launch(..., env=t.env)` — no such argument; three rows died in their first second | §5.2 |
| 6 | `blitcut` died inside `no_saver`, three frames below its own code, because only `OS88_BUILD` was set | §5.2 |
| 7 | A stale `build/c64.bin` survived a cherry-pick; `c64part` failed with "not byte-identical", which reads as a broken package | §5.1 |
| 8 | Twelve rows under load: 1.06× slower, up to 37% less guest work done | §7 |
| 9 | Four of five consecutive soak failures were `for _ in range(N)` giving the guest fewer rounds under load | §7.1 |
| 10 | `lzdrv` waited on `DRVR_SEG`, which is written before the read, the expand, the bss and the attach | §7.2 |
| 11 | `settle` returning five seconds into a hard-disk install, because the gfx lock makes the screen *more* still while it works | §7.3 |
| 12 | `dispcalc` spending 30% of all settle time waiting for the screen when the subject was thirteen bytes | §7.3 |
| 13 | `lzdrv` reading DRVCALL's probe strings before the probe had run — green alone, red beside one other emulator | §8 |
| 14 | `lzship` walking `drv_tab` on a stride of 10 where the record is 16 | §8 |
| 15 | A row hardcoding an IBM ROM name, running on a box that had it and silently not on one that did not | §6 |
| 16 | A row whose screen saver came on during a wait, comparing a black screen | §6 |
| 17 | `hibernate` mounting ONE `build/hiber.vhd` read-write in three emulators at once: 2 runs in 6, at a different leg every time, one of them a proven click on a proven pointer doing nothing | §5.5 |
| 18 | `reap()` racing itself — every `launch` reaps, so one process dropped a finished instance's tree while another wrote its record | §5.5 |
| 19 | An A/B of a five-byte deletion in `apps/os88type.inc` reading **exactly zero** on both arms, because `word.bin`'s rule never listed the file and `make` said "up to date". `apps/os88ui.inc` was missing from **nine** shipped packages the same way. `tests/unit/t_pkgdeps.py` is the row that came out of it | §1 |
| 20 | `t_pkg` reporting **every apps image stale** after a `make browsertest`, because its artefact map is `build/` by BASENAME and that target writes an uncompressed `DEMO.HTM` beside the compressed one the disks carry. A row whose whole subject is "is this image current" said no about a build that was. `build/zdata*/` now overrides, which also compares four data files that were compared against nothing | §5.1 |
| 21 | `paintmove` and `editmove` slicing `m.vram()` as `rows[y][cx0 // 8 : cx1 // 8]` — it is a BYTE per pixel, not packed bits, so the "repaint identical" check compared a strip of DESKTOP and was green by construction. The mouse pointer is in the framebuffer too, and with the rectangle fixed it was the next six pixels of difference | §1 |
| 22 | `bootstatus` reading `spl_mline` through `[spl_fseg]` while that word still held its SEED. It is `.text` initialised `COLD_SEG` so that a `SPLCALL` before stage 2's handoff refuses instead of jumping to offset 0 — a legal-looking segment, not a zero a test would notice — and the row read `uV` out of cold memory and reported that §15.6.4's two lines do not precede the settings read, about a boot that was correct. What the garbage IS depends on the packed kernel's byte count, so it fails for whoever next grows `.text` and passes for the commit after. The same seed comes back when `kmain` returns the blob, and that end read as raw code | §8 |
| 23 | `regpin` green with the thing it tests REMOVED FROM THE KERNEL, twice, for two different reasons. First the subject was a region at the CEILING, which cannot move whatever any predicate says - `regmove`'s own header warns of it one step along. Then the subject was the package making the forcing ask, and a package reaches `mem_claim` only from inside its own callback: `[wm_pkgd]` held its segment and the kernel refused it for having a frame in it, correctly and for the wrong reason. Only the A/B found either - the row passed, read plausibly, and printed a segment that had not changed | §1 |
| 24 | `regpin`'s first draft carrying `I_TASK = 8` where the kernel says **3**. `t_mirror` caught it before a run did; unaught, assertion 2 would have read a neighbouring byte and reported a hired worker as missing - and the row would then have been asserting the pin on a package that had no worker | §8 |
| 25 | `OS88_REGION_MOVABLE` placed at the SPAWN site in five packages, where a package that hires no worker never reaches it - so Audio and ftpd, which hire only when playback starts and when the card is up, declared **nothing**. They are the packages that move most easily. Found in the first minute by a row that reads `MC_RLOC` back out of the kernel's own table rather than trusting the call | §1 |
| 26 | `regapp` built as a MOVE row and green-adjacent twice: its forcing ask was granted out of a hole that had been there all along (93KB, then 24KB), so no compaction ever had to happen and the region correctly did not move. The arena a move row needs - a pinned wall above the subject and every free run smaller than the ask - is `regmove`'s, and building it per app is five emulator rounds to re-prove one kernel path. The row was recut to prove the thing nothing else covered instead | §1 |
| 27 | `sndmove` moving nothing, twice, against a kernel that was right. Its arena was built by opening PAINT as a spacer the way `regmove` does - but a package REGION is claimed top-down, so it landed in the largest ceiling run, which was the one *directly under* the subject: 33KB of pinned region between the ring and the low arena, so packing the ring up merged nothing and a correct compaction left it alone. A spacer is a wall, and where the wall lands is not the row's choice | §1 |
| 28 | ...and then the INSTRUMENT sealing the hole the row had just opened. `tests/filler`'s fill is first fit ascending and undeclared - therefore pinned - so it took the ceiling hole `sndmove` had made above the sound driver and put 13KB of immovable claim against the very block the following ask needed moved. Every round after that was refused for want of room the instrument had taken. It grew an 'S' key (ask, do not fill) for rows that build their own arena | §1 |
| 29 | `sndmove` reporting the ring **GONE** on the first run that moved it. It looked the claim up by `MC_OWN == <the segment the driver booted at>`, and a move rewrites the owner of every claim the holder held - so the correct answer read as a missing block. A lookup key that the thing under test is supposed to change is not a key | §8 |
| 30 | `sndmove`'s vector check green-by-vacuum: it asserted that no interrupt vector still named the old image, and **no vector named it at all**, because `SOUND.DRV` hooks its IRQ at the first stream open and not at attach. The A/B is what said so - with the kernel's IVT patch removed the desktop still drew and only that check went red, which is also the reason it exists. `SBTEST.O88` now rides on the disk to open and close one stream first | §1 |
| 31 | A kernel change that passed its own new row, `make test-full` and thirteen of the fifteen heap rows, and **broke the hard disk**: `hdmove` alone went red, with `No hardware found` on the glass. `mem_region_reloc` had been given a 256-entry IVT sweep, and int C1h/C3h on `os8088_xt_hdd` are SCRATCH WORDS the XT-IDE option ROM keeps in unused vectors - one held a value that was also a heap base. Run the whole family after touching the COMPACTOR — `mem_can_move`, the `mem_cp_*` placement and packing routines, `mem_region_reloc`, the claim table's shape, or a relocation proc — and A/B a failure against the base before believing it was already broken: `rdmove` in the same run WAS already broken, and the two look identical from the summary line. **USING the heap is not touching it**: a package that adds a claim, or declares one movable, earns its own row and `heapcheck`, not fifteen emulator rounds re-proving kernel paths it cannot reach — an undeclared claim is pinned by default and changes nothing the compactor does | §1 |
| 32 | `skiesease` pressing a key and then `advance(frames=6)`: on a loaded box the guest had not got it, and the aeroplane standing still for seven ticks reads exactly like a broken flight model. **A GUEST-clock wait is not a confirmation either** — the press is queued in the emulator, not in the guest, so the thing to poll is the guest's own `[cs_kroll]` | §7.1 |
| 33 | `skiesdiag` reading a `build/skiesdiag/` that `make skiesdiag` had not been re-run for: **a private tree nothing rebuilds is a stale tree**, so nine assertions failed on plausible wrong addresses and read as the watchdog being broken. A row that assembles its own symbol map must hold the tree to it — the same rule `os88sym` applies to `build/kernel.bin`, one level down. **`wants=` guards EXISTENCE, not freshness**, and the same session hit this twice: `vmmouse` died on a `build/emuk/` staled by an unrelated `kernel/mouse.inc` edit. `os88sym` names it correctly there ("the map describes a DIFFERENT kernel"), which is the behaviour to copy — fail naming the tree, never assert against it. **The root cause there was a MAKEFILE gap, not the row**: `$(BUILD)/vmmouse.img`'s recipe recursed into the emu sub-make but no kernel source was among its prerequisites, so a kernel edit left the target up to date and the sub-make never ran. A rule that builds a private tree in its RECIPE must carry that tree's sources in its PREREQUISITES, or `wants=` is guarding a file that lies | §5.1 |
| 34 | `tests/skies.py` asserting the crash RESET fifteen frames after it happened — `until` steps in blocks, so the aeroplane has been flying again by the time the loop returns, and the throttle moves on its own. Read 6 for 0 on a loaded box and passed every time it ran alone. **Capture the state in the predicate, at the moment the condition first holds** | §7 |
| 35 | `skiesset` writing a pinned pose while a `cs_step` was in flight: `m.pause()` lands anywhere, the step finishes on resume and writes its own position over the pin, and at Low detail a different position is a different object count — 4 in one run and 11 in the next off the same script. **Set the pause, let a frame by, THEN write, then read it back** | §7.1 |
| 36 | `tests/skies.py` sampling `cs_ww`/`cs_wh` off a running guest to ask which raster it took, and reading the box's height because the panel's clip BORROWS that word while it draws. The read was correct on the day it was written and started lying when the cockpit got dense enough to still be inside the bracket | §8 |
| 37 | `skiesset` counting what one frame filed while `CSO_SKIP` was still running: that word is **the tick the cull next looks at the object**, not a flag, so a count taken without clearing it reads the DEFERRAL PHASE rather than the rung. The guest free-runs between every pause for a host-timing dependent number of frames, and at six-way concurrency the None rung read 0 filed with 6 objects deferred in 3 runs of 12 and 3 filed with 3 deferred in the other 9 — same pose, same world, same setting, and a re-clear brought it straight back to 3. It reads exactly like an empty world. **Clear the state the reading is of, in the same bracket as the reading**; with the clear in `frames()` all twelve runs return the identical six counts | §7 |
| 38 | `skiesdiag` printing `SKIP: ... run \`make skiesdiag\` first` and **returning 0**: nothing in the suite built its private tree, so every soak scored it `ok` in 0.1s against 20s declared and the freeze watchdog went undriven for its whole life. It is entry 1 wearing a different coat — a row cannot report its own absence and be believed, because the runner has one channel for "I passed" and the row used it to say "I did not run". **A missing input is DECLARED** — `wants=` on the row, and a REAL make target behind it (row 33's rule: a rule whose recipe builds a private tree must carry that tree's sources in its prerequisites). That makes the skip visible in the tally, turns into a failure under `--strict`, and gets the tree built. A capability probed on the artefact was the first fix and is only half of one: it cures the false green and not the staleness, so the next `apps/skies` edit left a tree that existed and lied, and the row went red on two separate runs of the same change. What caught the original was the underrun warning §4 exists for — the only reason anybody looked at a green row | §1, §5.1, §11 |
| 39 | `advance(frames=N)` is EMULATOR frames, and a CLEAR SKIES frame at High/Ultra is **~195 ms of guest time** - so `advance(frames=10)` is about a *sixth* of one. A pose written and then read back at the pixels compared two pictures neither of which had been drawn, and the panel differences it reported were **the message strip ageing out between the two captures**: 52 "escapes" in 252 poses, every one of them noise, and the counts were suspiciously constant (77, 41, 36) because they were glyphs. `cs_frames` said 1 -> 1 across the whole wait and that was the tell. **Wait on the GUEST's frame** - a breakpoint at the render entry, n+1 stops for n complete frames - whenever the subject is what the guest DREW | §7 |
| 40 | `skiespanel` forcing a state change with `advance(frames=8)` and then asking whether the panel repainted. The panel repaints an item at a GATE every `CS_PRATE` **ticks**, and eight CARD frames is a fifth of a tick on Mode X — so under load the intermediate state was never painted and the row reported *"the key did not change"*, which was **perfectly true and told nobody anything**. A crash frame that got 24 line segments slower was enough to tip it. **Confirm the thing you are setting up, don't count frames at it**: a breakpoint on the painter proves it ran, and six concurrent runs then agree | §7.1 |
| 41 | `skiespitts` pressing a roll key and sampling for 300 frames without confirming the guest had it: under load the trainer **never rolled at all**, and the row said `the trainer stops at its roll limit (0 of 10923)` — a PASS — before failing the return-to-level check beside it and pointing at a flight model that had not been asked to do anything. Incident 19's fix, one row along and never generalised. **A press is confirmed at `[cs_kroll]`, not waited for** | §7.1 |
| 42 | `skiesease` confirming a press at `[cs_kroll] != 0` when the PREVIOUS arrow was still down. The two arrows on one axis are −1 and +1 in the same byte, so both down is **0 — the same byte a key nobody has got** — and a confirmation that only asks "is it non-zero" returns at once on the stale latch. The row then measured six ticks of an aeroplane holding perfectly still and reported `held AWAY from level every tick is the full rate ([0, 0, 0, 0, 0])`. **Confirm the RELEASE before the press**, not only the press | §7.1 |
| 43 | `skies160` calling `menu_pick` on a package it had only just opened. `menu_pick` reads the FRONT window's bar and a launch is ASYNCHRONOUS, so under load the Disk window was still in front: the bar read `['Apple', 'File', 'Edit', 'Nav', 'Builtins', 'Clear Skies']` — the app running and contributing its name, its window behind — and the row raised on a menu that was simply not there, 1 run in 4 at four-way concurrency. **`raise_window` before any menu**, which is free when the window is already front (one read of `wm_zord`, no click) | §6, §7.1 |
| 44 | `skiesflat` reading the shadow after `advance(frames=200)`. `advance` is EXACT in guest time and stops at an arbitrary instruction — which, in a program that spends most of its frame in `cs_scene`, is usually **inside a half-drawn picture**: the ground painted and the tower not reached yet. Which half depends on the free-run phase, which depends on the HOST, so the row passed 5 times in 5 alone and failed 4 in 4 at three-way concurrency, on the same bytes and the same pinned pose — and the failure it printed was `the platform bar is drawn AT THE TOWER (nothing over x=100)`, which reads as the fix under test not working. Entry 39's rule for a different reason: 39 is about not waiting long enough, this is about not stopping in the right PLACE. **Stop where the frame is whole** — a breakpoint at the device copy (`cs_blit`), not a frame count | §7, §7.1 |
| 45 | The `fast` tier at 55 rows and 62.7s of work, of which over half was one package's business (three SKIES rows, three FRACTAL, Paint's ink masks, the Weave family's two) or a kernel internal no package can reach (`.lowbss`'s order at 5.1s, the LZ codec at 4.7s, a `.bss` sentinel, the month mask). Nothing was wrong with any of them - they were being charged to the wrong person, on every build, for ever. The tier is the one nobody opts into, so the test is not "is this valuable" but "is it valuable to somebody who did not touch this" | §2.1 |
| 46 | The `full` tier at 14 rows and 452s of row time, of which `buildmatrix` alone was 143s assembling 99 knob configurations — instruments, not the OS — while `weavesmoke` spent 73s opening one package's bundle and `martyconc` gated the emulator harness rather than the machine. A pre-merge smoke test that takes five minutes and is mostly about the tree rather than the product is one that gets skipped | §2.2 |
| 47 | `hdboot` pressing a menu with the pointer and the button both CONFIRMED, and no menu for 182 ticks: the `EVT_MDOWN` was dropped from a full ring while the level stood | §7.2 |
| 48 | Every `os88build.tree()` call sweeping `$(VIDSTAMP)` — a legitimately empty marker — so make rebuilt the whole kernel each time and two rows sharing a tree rebuilt it under each other | §5.2 |
| 49 | Four rows hand-rolling the SAME breakpoint pump, each with the driving gesture on a daemon thread and the resume loop in `main` - because an armed breakpoint makes every `os88ui` and `os88mouse` verb unable to confirm, so the two could not be written one after the other. Two of the four counted a stop as *anything not running*, which makes the driving thread's own `advance()` and `pause()` read as entries that never happened - in `paintanchor` each one appended an EMPTY damage rect to the list its assertion is over. `paintsu` additionally carried `serialise(m)`, a monkey-patch wrapping `m.cmd` in a lock of its own, years after that lock landed IN `cmd`. **`os88marty.bp_trace` is the one pump**; and outside a trace an armed breakpoint now fails in 2.2s naming the clock, where it took **332.1s** and surfaced from `guest_sleep`'s stall arm - the only thing in the path that was watching | §6.2 |
| 50 | ...and then 21 more sites in 16 files that armed a breakpoint and drove the mouse with NOTHING pumping it. Every one passed, and passed for a reason that is not a guarantee: the symbol under watch cannot be reached until the gesture has been decoded, so the ordering held right up until it would not have. `int0sweep` is the sharpest - it arms INT 0 across a whole UI sweep and was sound exactly as long as it was passing, because the first real divide error would have frozen the sweep at the step AFTER it; its own `check` then cleared the breakpoint set at the first fire, so a machine raising two reported one and swept the rest unarmed. **Not every such site is a defect**, and TWO are not: `paintrow`'s second one WANTS the machine stopped inside `pt_blit`, because that is the context its patch runs in; and `paintlzw`'s `paint_base` RETURNS with the guest held at `toast_show`, the whole decode bracket after it starting from that stop. Converting the second one reached the toast correctly and then timed out at 190s - the trace resumes on the way out, and two round trips of a free-running guest is past `pt_gif_in`. Both carry a comment saying why they are bare arms | §6.2 |
| 51 | `skiesadi` waiting for a stop that had already happened AND could not happen again: `m.advance(frames=3)` then `m.wait_stop(3.0)`. `advance` ENDS STOPPED and takes the resume mark, so the wait asks for a SECOND stop nothing is coming to make - and a stopped guest burns no cycles, so it cannot even time out on its guest budget; it sat on the same stop until the host-time grace fired. Before the mark existed the same line returned AT ONCE with the stop it was asked to wait past, which is a green row for a gesture that never happened. `advance`'s own reply IS the answer - the server leaves a hit latched as `breakpoint` rather than overwriting it with `paused` - and a sweep of all 473 `advance(` sites found this shape exactly once | §7.2 |
| 52 | ...and the same row's RED ARM had stopped running at all, silently. `--clobber-adi` puts the unguarded divide back by finding `mov bx,cx / call cs_cdiv` and overwriting it; `470bd4c` moved cos out of CX - sin owns that register - so the pattern matched nothing and the arm exited instead of asserting. The green arm stayed green throughout, so the row looked healthy while the only thing that proves it works was gone. **A red run is code too, and nothing runs it**: anchor a patch on the fewest bytes that identify the site, and re-run the red arm whenever the code under it moves | §1 |
| 53 | `tests/skies.py`'s `until` documenting *"the guest's own clock, never the host's"* and calling `m.run()` inside the loop, so the guest free-runs across the pred read and the next advance: measured at **+6.0-6.4%** of the declared frames on an idle box, varying run to run, against **+0.1%** for the advances alone. The surplus decides how long a key is HELD - `until` releases the stick a block after liftoff - so a loaded box entered the climb at 584 units of pitch where an idle one entered at 874, and a climb assertion whose budget bought EXACTLY the 30 m it asked for went red at 27. Two rules in one incident: **a budget is not a requirement** - size it so the machine that behaves returns early and only a box that would have failed ever spends it - and **check what a helper's docstring promises against what it does**, because the next reader will believe it. It could not simply be made deterministic: `key` presses and releases with no guest cycles between them, so the guest must be executing for the keyboard to deliver both | §7 |
| 54 | `tests/unit/t_csworld.py` and `t_csworlds.py` reading Clear Skies' `cs_ports` after SPEC.md 88.10.5 moved every location's record into a world OVERLAY, so the addresses went from small image offsets to 0xBF60 and the tests' own SIGNED `w()` made all nine negative. `t_csworld` reported **"1 worlds behind 9 locations, all clear of their own water"** in 0.6s, having read the package HEADER as a location name nine times — a gate whose whole subject is buildings standing in rivers, passing without looking at one. `t_csworlds` was the same defect and happened to fail loudly, only because its `BASE` lookup is by name. **A registry does not notice a green row**, which is §1 again; what caught it was auditing what the change could reach rather than what the runner said. Two rules fell out: **a pointer is unsigned**, and a host-side reader of a package whose data is a PART must lay the part in (`csworlds.overlay`) instead of indexing the image | §1 |
| 55 | `tmowner` at **1 pass in 4, alone, at every point measured** - including trees that predate the session that re-rated it - and classified twice as a contention artefact on the strength of a single passing re-run. It is neither: `dispcells.Pump.serve` DROPS BREAKPOINT STOPS and says so in its own docstring, along with the rule that a gate counting rare events here must RETRY rather than fail on one observation. The row reassembles the heap page from `font_run` calls, the page indents a claim two characters INSIDE the text, so a row that loses its FIRST chunk loses the indent, `group_in` promotes it to a heading and files everything after it under a claim row. It failed saying *"its cache belongs under System - it is under 'Read 2000  63K HIGH'"*, which is the `DirRead` row one chunk short. **A capture is not automatically whole**: test the invariant that matters (here, every row kept its leftmost chunk), drop the frames that fail it, and retry the gesture until the capture ANSWERS. 3/3 failed before, 0/4 after, same sampler | §1, §7 |
| 56 | `tests/skiescount.py` dividing a WHOLE-FRAME A/B by a PER-FRAME COUNT and printing the quotient with no warning, on scenes where that count is a handful. It read a dedup test at **-283 cycles an edge**, a winding cross at **-1031**, and one `or [es:di], al` at **2819.3** against a true cost near 13 - an ADDED term cannot be negative and cannot be 200x, so all three were noise wearing a measurement's clothes, and they very nearly chose the next piece of work. The tool was written for `city` and the `df*` three, where those counts are in the hundreds; `--fly` and `--roll`, added the day before to answer a different question, made it reachable on scenes with three of something a frame. Two rules: **an instrument must measure its own resolution** - a null A/B, the same arm twice, which is the identical self-control that saved the pixel-identity gate one task earlier - and **a derived figure is only as good as its worst input**, so a net of three A/Bs is not printed at all when one did not resolve. It also turned out the two modes are exclusive: a POPULATION needs the world flying and a PRICE needs both arms to see the same scene, and flying the resolution is **26-43 ms on a 217 ms frame** against +/-16 to +/-111 cycles pinned. `--fly` skips the pricing half now and says why | §1 |
| 57 | `dotdel`'s leg A reading `dd_x`/`dd_y` twice **two HOST seconds apart** and reporting *"the demo's Smiles has not moved - the attract screen is a still picture"* for a demo that was walking about perfectly well. It went red exactly once, on a CGA lane sharing four cores with two other guests, and never alone; a 90-second watch of the same guest found no stall longer than **one sample**. The check is the game's own tick counter now - wait for `dd_anim` to advance 24, then compare - and it says which of the two things went wrong, because a guest that advanced no ticks is a starved lane and not a still picture. **Every `time.sleep` between two reads of the same guest variable is this bug waiting for a busy box** | §7 |
| 58 | A pixel census that **skipped a tile with nothing lit in it** — `if not cs: continue` — and so could not see the one defect it was written for. It ran for two rounds proving DOT DELIRIUM's maze was never the *wrong colour*, while a repaired corner was coming back **black**: most wall tiles legitimately hold no ink at all (SPEC.md 93.2.1), so an emptied tile is indistinguishable from an ordinary one unless the row knows which is which. It took a field report — *"corners are back to disappearing"* — to notice, and the fix was to stop reading the screen against ITSELF and read it against `dd_bdseg`, the picture the renderer copies out of: **108 wall-ink pixels black in 2 tiles** on the very build the colour census called clean, plus a second, older defect on a different adapter that nothing had ever looked for. **A census over what is THERE cannot see something removed** — anchor one to the artefact that says what should be there | §1 |
| 59 | `wdcombo` sleeping `time.sleep(1.4)` for a press whose own work is a **disk scan** — the Font combo's first open is `wd_fontscan` walking `SYSTEM/FONTS`, and an `int 13h` is ~400 ms whatever it moves. Green alone; under three CPU hogs it failed **2 of 2** with five reds in a row, opening `the press drops the list  FAIL DR_OPEN=0` and closing `the release picks item 0 and closes  FAIL OPEN=1 SEL=0` — **the same word reading 0 and then 1 four steps later**, which is one machine doing exactly the right thing and one test looking too early, reported as five different broken behaviours. The cascade is the expensive part: a single late event dressed as a broken record, a broken bank, a missing drag edge and 3,135 wrong pixels. It also cost a **bisect**, which came out non-monotonic and landed on the hourglass commits — the hourglass IS the disk path, so it correlates with the failing row without ever having caused it. **A wait is sized by what the guest has to DO, not by what the gesture looks like**; rewritten on `os88marty.until`, the row went 4/4 and its wall time stopped moving with load at all (idle 76s, loaded 78/80/75) | §7, §7.1 |
| 60 | `bptrace` reading `m.status()` **twice** — once for the verdict and once for the message — and printing `FAIL  the guest is at a breakpoint after the gesture ('breakpoint')`. A verdict contradicting its own diagnostic is the most expensive thing a failure can print: it sends whoever reads it after the harness instead of after the race. The race is real and is about WHERE the confirmation lands — `open_drive` confirms on the window RECORD, which `wm_draw_win` is reached after, so on a loaded box the verb returns while the guest is still short of the breakpoint. **One sample per verdict**, and a bounded wait on the guest's clock when it lands short | §7, §8 |
| 61 | Two rows failing a scoped soak for the **same** reason, neither of them naming it. `ddsmall` went red with *"the map describes a DIFFERENT kernel from `build/smallk/kernel.bin` … the file was written 7442.3 s ago"* — a message that says *run `make`* about a build that **was** current; only the private tree behind it was two hours stale. `fcpapi` died in 0.2s on `FileNotFoundError: build/trees/plain-<hash>/fcpapi-run.img`, about a copy engine it had not reached. Both passed at once when their artefacts were deleted and rebuilt by hand, and both had a `wants=`. **The cause was one line in `os88soak.prewarm`**: it skipped any target whose file already `os.path.exists`, so `wants=` guarded EXISTENCE and not freshness — row 33's rule, one layer up, in the code that was supposed to be enforcing it. `make` is the tool that knows whether a target is out of date and a `path.exists` in front of it is an optimisation that defeats the only thing being asked for. Measured: always asking is **1.0s for seven targets** on a current tree, against the **21.3s** plain `make` prewarm already spent — and the first pass after the guard came out ran **50.4s**, which was seven artefacts a run would otherwise have tested against. | §5.1 |
| 62 | …and `fcpapi`'s **other** half, which is the rule for any row that MANUFACTURES a file. `os88marty.launch` puts every image through `os88build.at`, so a parallel run boots out of its frozen tree — but the row wrote its scratch with a literal `build/…` string, into the SHARED one, and then asked the runner to boot it. The two disagreed and the failure named neither. **A path a row READS is resolved for it; a path a row WRITES is the row's own to resolve.** `os88marty.scratch_disk` already does this (inputs and output both) and is what to reach for; where a row copies a pre-built fixture instead, put both ends through `os88build.at`, which is the identity function standalone and the tree's path under the runner. | §5.1 |
| 61 | `tests/doscable.py`'s host end timing out in **20 seconds** against a leg that is minutes of stepped wall clock. It closed its socket, the far side went `NSK_CLOSING`, the box closed the flow, and the row reported *"the host got nothing back from the DOS program"* — about a program that had answered correctly. Then, with the timeout fixed, it waited for **512 bytes of a 24-byte answer that never closes**, blocking past the caller's `join` so `got` read as `None`. Both are §8's rule about the HOST's clock, in the one place it is easy to forget: not a `time.sleep` but a **socket timeout** | §8 |
| 62 | The same row reporting `LSN 0` — one message for the environment never read, `NETV_LISTEN` refused, a frame staged and refused by the client, a frame staged and never collected, and a connection that never came. Five bugs in four files behind one sentence, and four runs of nine minutes each spent guessing between them. It reads the box's own listener rows, flow table, pending word and the driver's dropped counter now, and **checks them in causal order**, so each is its own sentence. A failure message that cannot distinguish its causes is a failure message that sends you to the wrong file | §1, §7 |
| 63 | `Partner.serve` waiting for each command byte at `STEP` = **400 guest cycles a debug round trip** — 655 round trips per idle guest tick — while `idle_until_wire`, in the same file with its safety argument already written down, does the same wait in 25,600-cycle chunks. `doscable` ran past **twenty minutes** and is **525 seconds**. The harness primitive that made a phase affordable existed and was being used for one phase only: before raising a row's `secs`, check whether the wait it is paying for already has a cheaper spelling | §8 |
| 64 | The whole of the **region-movable follow-up**, which is incident 25 one level out: the door opened, six asm packages walked through it, `crt0.asm` took every C package along for free, and **twenty-eight asm packages were never converted** — for a cycle, until a user asked. Nothing caught it because an undeclared region is *invisible from inside the package*: nothing refuses, nothing warns, the program runs perfectly and the heap quietly cannot pack. That is SPEC.md 6.6's transparent text exactly, so it gets 6.6's answer — `tests/unit/t_movable.py`, an exemption registry that turns one way, and a reason per line. **A mechanism landed with adopters is not a mechanism adopted**: if the next package has to remember, write the row when the mechanism lands, not when somebody notices | §1 |
| 65 | ...and the first version of that row's crt0 check **asserted nothing**. `crt0.asm` calls `OSAPI_MEM_MOVABLE` **twice** — once for the region and once for a part's claim under `CC_HAS_PARTS` — so `"crt0 mentions the slot"` stayed green with the region declaration deleted, which is the exact case the check existed for (deleting it un-declares seven C packages at once, silently). It matches the three instructions in order now. **Found only by §1's break-it-on-purpose run**, which is the whole argument for doing it: four of the five failure modes went red first try and this one did not, and nothing else in the run distinguishes a check that passes from a check that cannot fail | §1 |
| 66 | A bare `OS88_REGION_MOVABLE` added to **Clear Skies**, which would have been *silent corruption* rather than a missed optimisation. It is a RE-HOMED program (SPEC.md 20.12.10): its loader hands it the art and the world streams **by absolute segment**, and those live inside its own carve, so they move with the region — `[cs_artseg]`, `cs_hand`'s `CSH_ART`, nine `CSH_WDIR` entries and `CSH_CLB` all needed the delta and the macro's proc is a `ret`. Worse, it is only *accepted* on 1.44MB, where 512-byte clusters put the program at the carve base, so three geometries of four would have tested clean. `tests/rehomemove.py` already said all of this in its own docstring, about `rhprog`. **Before declaring a region movable, ask what segment words the package holds that point INSIDE itself** — the answer is almost always none, and the two shapes where it is not (a stamped segment, a re-homed carve) are the two the registry now names | §1, §2 |
| 67 | `dosmap.package()` with no arguments meaning **the box nobody ships**. The helper mapped `apps/dos/dos.asm` with no defines, which was right for two years and stopped being right the hour `$(SYSROOT)` moved to the PARTED `DOS.O88` (SPEC.md 96.40.3) - `-DDOS_EXTCORE` takes the INT 21h core out of the image and puts a reservation in, so every offset past it moves about 1,800 bytes. Eleven rows read the shipped package through a map of a different one. **It does not fail as a wrong address**: `dosmedia` reported *"the box did not move to B:, it is on volume 16"*, which reads as the DOS box losing `CD`, and the rows that CLICK a rect read one out of rubble and hang in `os88mouse` waiting for a cursor that can never arrive. Three rows already passed the right defines by hand, as a named constant with a comment explaining the trap - so the knowledge was written down and the DEFAULT still had the old answer in it. **A helper that models a shipped artefact takes its default FROM the shipped artefact**, and a caller's own knob goes on top of that rather than replacing it | §8 |
| 68 | `dirwshed` asserting its own quantity **with the sign reversed**, and saying so: *"a spread of -32 where the cache is 32 KB"*. `[dos_keepc]` was a CHECK BOX whose ON byte meant *keep the cache*; SPEC.md 96.36's third arm made it an `OS88UI_RD_SEL` where arm **0** is `DOS_MEM_KEEP` and **1** is `DOS_MEM_DUMP`, and nothing re-read the row — so `for tick in (1, 0)` labelled both launches backwards and `got[0] - got[1]` compared them backwards too. `tests/dosmem.py`'s own header names this trap in as many words, which is the part worth keeping: **the warning existed, in the neighbouring file, and a row written before it was not re-read when the control changed.** A row that names a control's arms by NUMBER should name them by constant instead, so that flipping the encoding is a compile-time edit rather than a silent one | §1, §8 |
| 69 | The same row's last assertion reading `[dsk_rah_seg]` **after the program had exited**. The cache is claimed at a MOUNT (SPEC.md 18.95.5) and the DOS box re-mounts every volume on the way out of an fsx bracket, so the word it read was a cache that had come BACK — the shed had happened and the row could not see it. **Read the state inside the window it is a claim about**, not once the machine has been put back; and where a row has two arms, assert the other one the other way (the KEEP arm must still HAVE the cache), because a single-sided check passes on a box that sheds unconditionally | §1 |
| 70 | `kdbigexe` reporting *"build/BIG.EXE is not built - `make kdostest`"* in 0.3 s **about an artefact the run was holding**. A soak reads a FROZEN TREE and not `build/` (docs/plans/SOAK-PARALLEL.md §14.2), and `_frozen_targets` had already built it there from the row's own `wants=`; the row then opened `os.path.join(ROOT, "build", "BIG.EXE")` by hand. Every path a row hands to `os88marty` is resolved for it, so the only ones that bite are the few the HOST opens itself — and the failure text points at the operator's `make` rather than at the row, which is the expensive half. **A host-side `build/` path in a row goes through `os88build.at()`**, and `os.path.join(ROOT, at(...))` keeps it anchored either way | §8 |
| 71 | `kdreturn` asserting the DOS handoff's return on `[dos_exit]` and `[dos_state]` — **two cells its own earlier step had already set to those values**. The row runs the program WINDOWED first (to compare the arena), and a windowed DOSHELLO exits 42 into `DST_RAN` exactly as a returning one does, so a return that poked nothing at all passed for as long as the row has existed. It hid **two** defects at once, either of which alone kills the feature: `hbm_wake` read `HS_DOSCODE` *after* setting the graphics mode, and on a CGA or a Hercules the staging area IS the desktop's framebuffer (SPEC.md 96.41.3) — it worked on VGA, which is every machine anybody had looked at; and `dos_wake`'s package-launch test jumped to `.notpkg`, which is **past** the block that reads the code (96.41.4). The row now zeroes the three cells before the handoff and reads the console's **log line**, which is a sentence the return has to have WRITTEN rather than a byte it has to have left alone. **An assertion whose expected value is already present before the thing under test runs is not an assertion** — and the tell is cheap to look for: ask what the row itself did to those cells earlier | §1 |
| 72 | ...and the same row could not see the field's actual bug because it only ever **booted the fixed disk**. `hb_pick` puts the image on the boot volume when that is fixed and otherwise on the first fixed volume there is — a completely different arm, reaching a DRIVER-backed volume the launch-block gather wrote off as `DVK_FREE` (SPEC.md 96.46.1). The live resume refused and fell back to `int 19h`, and the only visible difference is that coming back takes a whole POST and a whole boot. `kdreturnf` is the same script with `--boot floppy`, and it checks the fixed disk is `DVK_DRV` **before** asserting anything, so it cannot quietly become the row it was cloned from. Red on the broken kernel at **19.1 guest seconds against 3.3**. **A predicate with two arms wants a row per arm**, and the cheap one to forget is the one the developer's own machine never takes | §1 |
| 73 | `tests/dosram.py` opening its program with `ui.path("C:/DOSHELLO.COM")` and then reading a page that had never painted. **An association open of a `.COM` RUNS it** (SPEC.md 54), so the box arrives inside its own fsx bracket with the program's output on the screen: no window, no page, and every rect read as zeros. The click at the Setup button's own resolved centre went to the text screen and the row reported `OSAPI_DRV_CLASSK says DRVC_DISK is holding nothing` — a sentence about the kernel, for a test that had not got as far as looking. Dismiss the run first; the box comes back with the program still NAMED, which is the state arm 3 wants anyway | §1, §7 |
| 74 | ...and the same row's first fixture put `HDD.DRV` on the disk and **nothing that asked for it** (SPEC.md 51.3). It still booted off the fixed disk — the boot partition is a `DVK_BIOS` row served by `int 13h` and not by the driver (18.7.1) — so `C:` was there, the page opened, and `OSAPI_DRV_CLASSK` answered 0 for a class that really was holding nothing. **A TRUE ANSWER ABOUT A MACHINE THE ROW DID NOT MEAN TO BUILD**, which is the shape that passes: had the assertion been `>= 0` instead of `> 0` it would have been green for ever. A fixture that needs a driver writes the `SYSTEM.CFG` that asks for it, and says in its own words why the disk being reachable is not evidence | §1 |
| 75 | The Memory page's press riding in **DI** across five controls. It worked while the block held one — `os88ui_rad` preserves every register — and stopped the moment there were five: `os88ui_drpress` does `xor di, di` and `dos_mck_di` ANSWERS in DI. The limit field then hit-tested at a garbage x, which reads as *the box refused the click*. Same task, same shape one layer up: the limit field was tested AFTER the radio whose rect covers it, so a press on it picked the arm it was already in and `doslnk` reported a `.LNK` carrying `memkb: 0` — a shortcut losing a setting, for a control that was never reached. **When a block grows past one control, the point belongs in memory and the innermost control asks first** | §1, §7 |
| 76 | `btngesture` registered with `wants=("marty",)` — the emulator CAPABILITY put in the field that names build ARTEFACTS. The runner builds a `wants=` entry with `make <entry>` and then asks `os.path.exists(ROOT/<entry>)`, and `marty` is a PHONY target with no file at the root: cargo compiled it, `make` exited 0, and the runner still printed *"1 artefact(s) would not build: marty"* and skipped the row. **It had never run once.** The registry's own comment on the field already said *"Paths, not make targets"*; nothing checked it, and nothing needed to check it for the row to look fine — a SKIP is a line of output like any other, and **nobody investigates a skip**, which is §1's rule in different clothes. The gesture work under it was correct and passed the moment the row could run. `t_registry` fails an entry with no `/` in it now | §1, §5.1 || 77 | `dosglyph` reading a slot **the mount had not written yet**, and a whole session spent proving it was the kernel. The row navigates into a folder and immediately reads `assoc_glyph`; `os88ui.open` returned when the listing NAMES changed, which a mount does long before its icon harvest runs, so the read landed in the gap. Measured exactly: at the moment the verb returned the slot held the poison, and **21 ms of guest time later it held the shipped glyph with the FDC read count unchanged** - the sector had been read, the kernel had not yet reached the store. It went red on an unrelated kernel branch and stayed red 5/5, so it read as that branch's defect, and the elimination table built for it concluded *"dead code flips it and the same number of inert bytes does not"* - true, and not the answer: **THE KERNEL IS COMPRESSED** (SPEC.md 2.9.13), so zero padding costs the packed `KERNEL.SYS` nothing and real code costs it sectors, and every padding arm was a control holding the boot timing fixed. The same fault had already taken `tanksmall`, `pathcost`, `uilat` and `wireflick` red, where it read as four different product bugs. **A row that flips on unrelated kernel bytes has a race in it, and the first question is what it waits for** - never what the bytes are. And a build-size experiment that separates code from padding is asking the packer a question before it asks the kernel one | §1, §6 |


---

## 14. Where the rest of it is

| document | for |
|---|---|
| **docs/TESTING.md** | what each emulator can and cannot show, per capability, with a recipe |
| **docs/MARTYPC-DEBUG.md** | the instrument: `launch`/`settle`/`sym`, the debug server, reading the guest's floppy back |
| **docs/plans/SOAK-PARALLEL.md** | the parallel runner, where the suite's time goes, and every measurement quoted above |
| **PERFORMANCE.md** Part 7 | checking a change; Parts 3.1/3.2 for flicker and smoothness harnesses |
| **`tests/suite.py`** | the registry, and its header on what earns a `fast` row and why `full` is curated |
