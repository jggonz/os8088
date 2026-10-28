# Writing a test for this tree

The suite is `tools/os88test.py` over the registry in `tests/suite.py`;
`tools/os88soak.py` runs the soak tier in parallel. This file is how to add a
row that can fail, costs what it has to and no more, and says what broke when
it goes red. docs/TESTING.md says what each emulator *can* show, and
docs/MARTYPC-DEBUG.md is the instrument's manual.

## Start here

You do not have to read this whole file to write a good row. Read this box,
then the section for the part you are writing.

**The rules that catch most bad rows:**

1. **Break the thing on purpose and watch the row go red** (§1). A row that
   cannot fail is worse than no row.
2. **Put it in `soak`** unless it clearly earns `fast` or `full` (§2).
3. **Drive with `tools/os88ui.py`**, never a remembered coordinate (§6).
4. **Wait on the guest's state, never on a clock** - no `time.sleep`, no
   `advance(frames=N)`, no `launch(boot=N)` (§7).
5. **Assert on guest memory**; compare pixels only when pixels are the
   subject (§8).
6. **Never build in `build/`**: a fixture is `wants=`, another kernel is
   `os88build.tree()` (§5).
7. **Measure `secs`**; don't guess it (§4).
8. **Register it** with a `why` that says what breaks (§11), and tick the
   checklist (§12).

**Where to go:**

| you are... | read |
|---|---|
| choosing a tier | §2 (`fast` §2.1, `full` §2.2) |
| filling in the `Row(...)` | §3 |
| needing a disk image, package or knob kernel | §5 |
| booting, clicking, typing, using breakpoints | §6 |
| waiting for something to happen | §7 |
| reading a package's or a driver's bytes | §8.1 |
| making a slow row cheaper | §4.2 |
| on QEMU, 86Box, Bochs or Unicorn | §9 |
| wondering why a rule exists | §13 (the incident list) |

---

## 1. Before anything: can this row FAIL?

Write the failing case first. A red row gets investigated and a green one
does not, so a row that cannot fail is worse than no row: `dispcp` (a library)
and `mkclick` (a generator) sat in the suite reporting `ok` in a tenth of a
second for months.

**Break the thing on purpose and watch the row go red.** Comment out the
kernel line, poke the byte, feed the wrong fixture. If you cannot make it fail
in five minutes, the assertion is not about what you think it is. The ways a
row passes without testing anything are worth knowing by sight:

- **The expected value was there before the test ran** - the row itself set
  it earlier (incident 71). Ask what the row did to those cells first.
- **A range or loop that is empty** - zero breakpoints armed, zero tiles
  compared, a census that skips what it cannot see (incident 58).
- **A check that matches something else** - GLaBIOS's own POST line contains
  `RAM`, so "the screen says RAM" passed on a machine that hung after POST.
- **A skip that reports as a pass** - a row that prints `SKIP` and exits 0
  scores `ok`. A missing input is DECLARED (`needs=`, `wants=`) so the runner
  skips it visibly (incident 38).

**Add a positive control** where the negative case is easy to pass. `lzfence`
feeds hostile streams that must be refused, and a decoder that refuses
everything passes all of them - so a valid stream runs first and its bytes
are compared one by one.

**A red arm is code too.** A patch that breaks the subject on purpose stops
matching when the code under it moves, and then the row looks healthy while
nothing proves it works (incident 52). Anchor a patch on the fewest bytes that
identify the site, and re-run the red arm whenever the code under it changes.

---

## 2. Which tier

| tier | budget | what belongs there |
|---|---|---|
| `fast` | **30s, enforced** | host-side only: read what `make` just built and check an invariant that breaks silently. Runs inside every `make`. |
| `full` | **180s, enforced** | one question: *did you obviously break the OS?* |
| `soak` | **none, deliberately** | everything else, one `-k` away. |

**Choose by what the row costs and how broadly it fails, never by how
important it is.** Neither expensive tier runs per commit: `full` runs when
major work reaches the integration branch, and `soak` runs scoped to the rows
a change can reach (docs/TESTING.md, *When to run which tier*). A `soak` row
is run by the person who touched its subject, which is who it is for. **When
in doubt, `soak`.**

### 2.1 What earns a `fast` row

`fast` is the one tier nobody opts into, so every second of it is charged to
somebody who never touched its subject. The question is not "is this worth
having" but "is it worth having to somebody who did not touch this?" Either
of these sends a row to `soak`:

1. **It is about ONE package, driver or host tool.** Whoever changes it runs
   its row. Being a build input (a generated include, a committed asset) is
   not an exception.
2. **Only a KERNEL change can break it.** A `.bss` sentinel or an eviction
   rank is defended by whoever edits that subsystem. The exception: a
   kernel-side row stays if code OUTSIDE the kernel has to agree with it (a
   package, a driver, the SDK, a Makefile recipe). Ask whose else's file must
   agree, not which directory the test reads.

What survives is four families; a new `fast` row should name the one it joins:

| family | what it is | some of its rows |
|---|---|---|
| **The boundary** | the kernel and the code loaded onto it, edited by different people | `api-abi`, `stkclass`, `drvovl`, `drvmem`, `fonts`, `pkgdeps` |
| **Rules over every line** | what any assembly in this tree must obey | `asmrules`, `ovlchk`, `textrules`, `stkbalance`, `stkapps`, `swallow` |
| **The shipped artifacts** | the floppies themselves | `image`, `pkg`, `diskverify`, `canary`, `checkreadme` |
| **The tree and its suite** | duplicated constants, generated docs, the gates' own integrity | `mirror`, `checkdocs`, `docindex`, `registry`, `machines`, `qemuown` |

Two named exceptions are the owner's decisions and are **not precedents**:
`kernbudget` stays for the size line it prints on every build, and
`readme8088` because the owner wants it. Moving a row down to `soak` is not
deleting it; it changes who pays for it.

### 2.2 What earns a `full` row

`full` is a smoke test with reach, not a survey: about four emulator rows fit
in three minutes. Three rules:

- **Nothing app-specific.** A row may *drive* an app as the vehicle for a
  generic check (`ctoolchain` builds C packages), but a row whose subject is
  one program is `soak`.
- **A kernel check is allowed, and short.** `kernresident` boots and walks
  `mem_tab` in 12s; a deep sweep of one subsystem is not.
- **The subject is the OS, not the tree or the suite.** Knob builds, the
  emulator harness and size reports are worth having and are `soak`.

The five rows: `bootsmoke` (boots to a desktop on both 1bpp adapters),
`kernresident` (kern_big boots and fits on a 128KB VGA machine), `small128`
(kern_small builds and boots on its floor machine), `ps2mouse` (mouse and
keyboard work), `ctoolchain` (the C toolchain produces a package).

Five more are the owner's ratchets, put in `full` at his word rather than by
the rules above, and **not precedents**: `megahigh`, `sysfree`,
`guidelock-full`, and the pin ratchet's two halves - `pins` (every heap claim
in the source declares itself movable or has a line in `tests/pins.txt`) and
`pinsrt` (the same buckets read back out of `mem_tab` on MartyPC,
docs/plans/NO-PINNING-PLAN.md 4.1).

---

## 3. The row

```python
Row("lzmod", "soak", py("tests/lzmod.py"), 30.0,
    "SPEC.md 20.14.5: BEVERLY.MOD, COMPRESSED, opened by a double-click. "
    "All 116,085 bytes are compared BYTE FOR BYTE, because a decoder that "
    "got one match wrong still opens a window and still plays - a click",
    needs=("marty",), wants=("build/lzmod360.img",)),
```

| field | rule |
|---|---|
| `name` | what `-k` matches (an `fnmatch` glob); the script's name where there is one |
| `tier` | §2 |
| `cmd` | `py("tests/x.py", "--flag")`, run with `cwd=ROOT` |
| `secs` | **measured** (§4) |
| `why` | what breaks if this goes red, with the § that owns it - for somebody who finds it failing in a year |
| `needs` | capabilities, probed: `marty`, `qemu`, `qemu9`, `86box`, `bochs`, `nasm`, `nasm3`, `cc`, `pil`, `unicorn`, `ffmpeg`, `dosbox`, ... A Python module the row imports is a capability too. A missing one SKIPS the row - the box declining to answer, never a pass |
| `wants` | build artefacts the row OPENS that `make all` does not build - **paths, never make targets** (§5.1) |
| `serial` | it drives an emulator; implied by `needs=("marty",)`, say it for QEMU |
| `builds` | it runs `make` in `build/`. **You almost certainly do not want this** (§5) |
| `alone` | its ANSWER needs the cores - a rate or a frames-per-second (§4.1) |
| `locks` | a named HOST resource it cannot share - `("port:8090",)` for a QEMU row whose fixture server the guest reaches at a fixed 10.0.2.2 port. The lane never runs two rows holding one name at once; nothing else about the row is serialised (§9.1) |
| `timeout` | defaults to `max(60, 4 x secs + 30)`; raise it for a row whose shape is slow, never to cover a wait |

---

## 4. Declaring `secs`: measure it

Run the row and write down what it took, at about 1.25x. `secs` is what the
runner schedules by, prices a `--resume` against and derives `timeout` from:
the compression family once declared 2,721s for rows that take 701. The runner
flags a row over 2x + 1s (`SLIP`) or under 5% (`UNDERRAN` - the row that ran
nothing). Include a private tree's build if the row makes one (~40s cold).

### 4.1 `alone` is about cores, not the tree

Only a row whose assertion is a RATE OF THE HOST needs the machine to itself.
Guest cycle counts, `disk()` counts and pixel comparisons are exact at any
load, because they are counted - so a MartyPC frame rate in guest cycles is
NOT one (the Excitebike perf rows shared the lane once their script feed
stopped racing the guest, docs/plans/SOAK-PARALLEL.md 17.5). If your row needs
`alone` and does not measure a host rate, it has a host-clock wait (§7).

**On QEMU the guest's clock IS the host's** (no `-icount`): the PIT and the
BIOS tick follow wall time, so a guest starved of CPU is a slower machine. A
QEMU row whose answer is the guest's speed against its own clock - a bench
that picks a quality rung, a ladder with five per cent of headroom - does
not get `alone` for it: it boots with `os88qemu.icount()` (`-icount
shift=N,align=off,sleep=on`), which puts the PIT, the tick and the SB16 on
an instruction-counted clock, so load stretches the wall time and moves no
answer (`xtmp3rate`, `vidbankspk*`, docs/plans/SOAK-PARALLEL.md 17.8). Read
the os88qemu note before using it: it changes the cost model (an I/O
instruction costs what a register move does), an I/O-dense guest needs a
larger shift to stay affordable, and a row about a THROTTLED disk's rate
cannot use it, the throttle being real time. Every QEMU row shares the lane.

### 4.2 Where a row's time goes, and cutting it

A row's time is almost always **waiting**, not testing. Before optimising,
find out where it goes:

- `OS88_WAITLOG=<file>` logs every wait with its call site and its guest
  cost - that is the work list.
- When the wall time is much bigger than the logged waits, the host is the
  cost: `python3 -m cProfile -s tottime tests/<row>.py`. Fixture encoding,
  per-pixel image parsing and Python loops over a capture have each been most
  of a row; vectorise them and check the result equal to the old loop.
- Compare **guest seconds**, not wall time, on a busy box.

The cuts that keep paying, roughly in order of size (the measurements behind
each are docs/plans/completed/SOAK-SPEED.md's numbered techniques):

| cut | how |
|---|---|
| wait on state, not on the screen or a clock | §7. This alone often halves a row. |
| boot on kernel state | `os88ui.boot(..., quick=True)` or `launch(boot=False)` + `run()` + `os88marty.kernel_up(m)` instead of a picture settle (§6.1) |
| one boot for legs that start from the same state | run the legs on one machine, each asserting it found the state it expects; or boot once and fork: `with os88marty.Forks(ui.m) as fk: with fk.restore() as r: ...` (~0.1s a restore) |
| independent legs on independent machines | legs that share nothing (two knob trees, three layouts, RAM sizes) run at once, an emulator each in a thread |
| make a host fixture once per input | `os88fixture.cached(dest, key, produce, sources=...)`, keyed on the inputs and the producing tool's source; check a cache hit equal to a fresh make once |
| ask `os88build.tree()` once per configuration | even a warm tree costs a `make` to prove it current |
| don't stop at breakpoints you only count | `bp_trace(..., log=True)` for a timing trace; disarm a "did X run" breakpoint at its first hit |
| let the emulator run flat out | when its speed is not the subject (DOSBox `cycles=max`, 86Box floppy turbo) |

A cut must not weaken the row: never delete an assertion, never shorten a
window that IS the measurement, and if you changed how something is read,
break the subject again (§1).

---

## 5. Never build in `build/`

`builds=True` is two rows (`buildmatrix`, `ctoolchain`). A row that runs
`make` in `build/` rewrites it under every other row reading it - one row's
mid-run `make` once cost a soak nine rows. There are two legitimate needs and
each has its own answer.

### 5.1 A fixture: `wants=`

An image, package or driver that `make all` does not build. Declare the PATH;
the runner builds every declared artefact before any row starts, and a row
whose artefact will not build SKIPS.

```python
    wants=("build/lzmod360.img",)
# and in the script:
from os88fixture import need
need("build/lzmod360.img")    # a no-op under the runner; an error if undeclared
```

- **A path, never a make target.** `wants=("marty",)` builds the emulator,
  then finds no file called `marty` and skips the row for ever (incident 76).
  `t_registry` refuses an entry with no `/` in it.
- **Never on a `fast` row** - the prebuild runs `make` inside `make`.
- **`wants=` guards existence; the Makefile guards freshness.** A rule whose
  recipe builds a private tree must list that tree's sources as
  prerequisites, or the artefact is up to date and wrong (incident 33).

### 5.2 A different KERNEL: `os88build.tree()`

A knob build, `kern_small`, a `PKGZ=` set - any kernel that is not the shipped
one:

```python
t = os88build.tree("NOPLANE=1").apply()          # builds, or reuses
with os88marty.launch(t.img("os8088-360.img"), apps=t.img("apps360.img")) as m:
    ...
```

The tree is `build/trees/<knob>-<hash>/`, under a per-tree lock, so two rows
never meet and `build/` is never written. Use **`.apply()`**, not `env=`: the
symbol reader needs it, and helpers like `no_saver` read its module default.
**Never restate nasm defines** - a knob's make variable and its define differ
(`VGADIRTY=1` is `-DVGA_DIRTY`). **`make -n` with a knob pointed at `build/`
is destructive**: it deletes the kernel when the knob set differs.

First check you need a kernel at all: four of eight compression rows that
built one needed only the shipped kernel and a different fixture.

### 5.3 Paths the HOST opens, and paths a row writes

- A host-side read of `build/...` goes through **`os88build.at()`**, so under a
  frozen soak it reads the run's tree and not the operator's (incident 70).
  `os88marty.launch` and `scratch_disk` already do this for their paths.
- **Anything a row writes is the row's alone.** Three copies of a row run at
  once; a fixed `build/hiber.vhd` was one disk mounted in three machines and
  failed at a different step every time (incident 17). Write scratch through
  `tempfile` (the runner points `$TMPDIR` at `build/tmp/`) or key it to the
  process, and use `os88marty.scratch_disk` for a manufactured image.

---

## 6. Driving the machine: `tools/os88ui.py`

**Start here, not at the mouse.** Every position is resolved from the guest's
own tables and every verb is CONFIRMED by reading guest state:

```python
import os88ui

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 quick=True) as ui:
    ui.path("B:/APPS/CALC.O88")         # drive, folder, package - each checked
    ui.menu_pick("Calc", "Close")
```

Verbs include `open_drive`, `open`, `path`, `window`, `wait_window`, `front`,
`raise_window`, `move_window`, `drag_window`, `close`, `listing`, `entry`,
`scroll_to`, `menu_pick`, `wait_toast`, `chooser`, `still` and `settle`. `ui.m`
is the Marty and `ui.mo` the mouse.

- **Nothing is aimed at a remembered coordinate.** A layout change moves every
  table at once, and a listing row number is not a file.
- **Verifying is faster than settling**: the same navigation is 0.35x the time
  of the settle-and-hope spelling (`tests/uilayer.py`).
- **A verb that cannot confirm RAISES**, naming what it saw, where it happened.
- **`raise_window` before `menu_pick`** on a package you just launched:
  launches are asynchronous, and the menu bar is the front window's
  (incident 43).
- `boot()` turns the screen saver off and resolves an IBM machine name to its
  GLaBIOS twin. A row calling `os88marty.launch` directly gets neither.

The mouse under it is `tools/os88mouse.py`: absolute and closed-loop, with a
`dblclick` of its own. `tools/os88mouserel.py` is relative and is only for the
mouse itself under test, a bit-exact replay, or motion with no destination (a
paint stroke).

### 6.1 Booting on state

`quick=True` ends the boot when the kernel says the desktop is up (`desk_rows`
and `menu_nbar` live, `[spl_live]` = 0, the tick moving) instead of waiting for
the picture to settle. Use it whenever the first act is an `os88ui` verb or a
guest read. A bare launch is `launch(boot=False)`, `m.run()`,
`os88marty.kernel_up(m)`. **`launch(boot=N)` is a blind N-second pause** - one
row spent 64 of its 69 seconds in it. Keep the picture settle only when the
first thing you do is compare the boot screen.

### 6.2 When a hand-rolled click is right

Rarely, and with a comment saying why: the widget is not a window, menu or
listing row (a radio button, a game cell - derive it from the window rectangle
and a layout constant); the row is testing the pointer itself; or the machine
is QEMU (§9). Then confirm by reading the state the click was meant to change.

### 6.3 Breakpoints and UI verbs: `os88marty.bp_trace`

An armed breakpoint stops the guest, and a stopped guest publishes nothing a
verb can confirm against - so arm breakpoints only inside a `bp_trace` block,
and the body is ordinary code:

```python
with os88marty.bp_trace(m, "wm_su_try", "gfx_restore") as tr:
    ui.raise_window(w)
    tr.until(lambda mm: tr.count("gfx_restore") >= 1, "the restore to run")
assert tr.count("wm_su_try") == 1
```

- **Stay in the block until the work has run** (`tr.until`): a gesture returns
  when it is decoded, not when its repaint finishes.
- `regs=True` records registers at each stop; `on_hit=f` runs while the guest
  is stopped, for a value true only inside the routine.
- **`log=True`** has the debug server record each hit (address, cycles) and
  carry on without stopping - for a trace whose hits only feed arithmetic.
- **Arm the narrowest symbol** that answers the question; every stop costs
  round trips. Never hand-roll the pump (incident 49).

---

## 7. Waiting: the guest's clock, never the host's

**`time.sleep` in a row holding a machine is a defect.** Two host seconds buy
9.6 guest seconds on an idle box and 2.4 on a busy one, so the same test hands
the machine four times less work under load and fails looking like the thing
under test. **Contention does not make a row slow, it makes it less thorough.**

| want | use |
|---|---|
| a specific thing to HAPPEN | `os88marty.until(m, cond, what, guest=N)` |
| the UI to have FINISHED with an input | `os88marty.ui_done(m)` (`cap=` for a ceiling; `lock=False` for a game whose worker always holds the gfx lock) |
| a key or a string to have been READ by the guest | `os88marty.key_taken(m, name)` / `text_taken(m, text)` |
| guest bytes to stop changing | `os88marty.quiesce(m, read, guest=...)` - put `m.disk()["reads"]` in the key if the work can stall on the drive |
| the screen finished, before a pixel read | `ui.still()` (`ui_done` plus three frames); `os88marty.settle(m)` only when no input caused the change |
| time itself to pass, nothing else will do | `os88marty.guest_sleep(m, N)` |

All of them run on the emulator's cycle counter, so a loaded box cannot shorten
them, and a guest that has stopped fails in ~2 seconds naming the machine.

### 7.1 Wait for the thing, not for a duration

```python
#   NO
time.sleep(20)                                     # "give the load time"
m.advance(frames=300)                              # a blind wait, unlogged
mo.click(x, y, settle=4)                           # a fixed 4 x 4.5 guest s

#   YES
os88marty.until(m, claimed, "Tracker to claim the module", guest=60.0)
os88marty.ui_done(m)
mo.click(x, y, settle=0); os88marty.ui_done(m, cap=18)
```

- `advance(frames=N)` stops wherever the guest is, often in a half-drawn
  frame (incident 44). When the subject is what the guest DREW, stop where
  the frame is whole - a breakpoint at the device copy.
- `for _ in range(N): ... advance()` gives the guest fewer rounds exactly when
  more is happening.
- **Capture the state inside the predicate**, at the moment the condition
  first holds - the guest keeps running while `until` returns (incident 34).
- **A poll interval is a host floor on every wait**: poll at 0.05 or less (the
  harness default adapts from 20 ms).
- **A wait whose PASS is its timeout** ("X must not happen within N seconds")
  pays the whole budget on every green run. End it when the work X would be
  part of is finished (`ui_done`) plus a short guest tail - and break the
  subject once to see X caught inside it.

### 7.2 Pick a signal that means what you think

**Read the kernel and find the write that happens LAST.** `lzdrv` waited on
`DRVR_SEG`, which `drv_load` writes before the file is read, checked, expanded
or attached; `drv_owner` is written last. A verb that returns when a listing's
NAMES change has returned before its icons are harvested (incident 77).

**A confirmed input is not a confirmed gesture.** The mouse proves every
packet and every button edge, and a press can still be dropped from a full
event ring (incident 47). Wait on the state the gesture is FOR - `menu_dropd`
for a menu, `ui_dragwin` for a drag - and confirm a key's RELEASE before the
next press when two keys share a byte (incident 42).

### 7.3 `settle` is the expensive one

`settle` waits for several identical screen captures, and used to be half of
a row's time. It is right only when the next line compares PIXELS. Before a
memory read use `ui_done`; after an input, before a pixel read, use
`ui.still()`. `settle` is positively wrong during an operation that holds the
gfx lock - the screen is *stiller* while it works than when it is done, so a
settle returns five seconds into a hard-disk install.

---

## 8. Asserting: guest state over pixels

Read the byte the feature writes - `trk_modseg`, `ld_status`, `drv_tab` -
because it is exact, cheap, and survives a layout change. Compare a whole
image **byte for byte** where the subject is bytes. Compare pixels when pixels
ARE the subject (a redraw, a glyph, a greyed control) and say which pixels and
why; MartyPC's `flicker`/`pace` measure what a screendump cannot (PERFORMANCE.md
Part 3).

- **Ask the kernel for its own constants**: `os88sym.equates()`, not a typed
  number. `lzship` walked `drv_tab` on a stride of 10 where the record is 16
  (incident 14), and constants differ between kernels.
- **Confirm what you read has been WRITTEN**, and read it inside the window it
  is a claim about (incidents 13, 69).
- **One sample per verdict**: a verdict and its message read separately can
  contradict each other (incident 60).
- **A pointer is unsigned**, and a host-side reader of a package with parts
  must lay the part in rather than index the image (incident 54).

### 8.1 Reading a package's memory

**Never read a base out of a kernel record and then read through it yourself.**
`seg = W_SEG; m.read((seg << 4) + off, n)` is two round trips with the guest
running between them, and the compactor copies a region *before* it rewrites
the W_SEG naming it (SPEC.md 66) - land between the two and you read the old
base's shifted bytes, which decode as a plausible wrong answer (call5's exit
code 161, dirwshed's 29,888 KB arena). Banking the base at the top of a
session is the same defect held open for the whole run. Use:

```python
os88marty.pkg_read(m, w, off, n)            # a window's package; w or slot
p = os88marty.Pkg(m, w, image=img)          # a live view: p.word(bss_off)
os88marty.drv_read(m, row, off, n)          # a drv_tab row's driver
os88marty.seg_snap(m, word, spans)          # any kernel word naming a seg
```

Each is the base and the bytes in one compactor-safe snapshot, at one round
trip when nothing is moving - less than the naive pair; writes (`pkg_write`,
`drv_write`, `p.write`) always pause the guest. `dosmap.instance` and
`dispapps.pkg_seg` return a `LiveSeg` that `dosmap.rect`/`centre`,
`dispapps.words` and `m.readseg(seg, ...)` re-resolve per read
(`os88marty.pkg_live(m, w)` makes one); QEMU objects take the same calls.
docs/MARTYPC-DEBUG.md has the argument; `tests/pkgread.py` is the gate.

---

## 9. Which emulator

**MartyPC is the default instrument**, cycle-exact against the field 5150.
QEMU is a fallback with a closed list (docs/TESTING.md owns it): 286/386, the
hard-disk driver's rung 1, the awkward mouse cases, the PS/2 mouse, the
Ethernet card, the RTC write half, the VMware pointer. VGA is NOT on the list.
Several instances of any emulator run at once; take the address off the object
(`m.addr`), never type a port.

Each emulator's harness has the same shape - gate the boot on kernel state,
wait on guest memory, a closed-loop pointer - spelled for that emulator:

| emulator | harness | notes |
|---|---|---|
| MartyPC | `tools/os88marty.py`, `tools/os88ui.py` | `m.planes()` reads VGA planes straight off the card; `m.batch()` sends several commands in one round trip |
| QEMU | `tests/os88qemu.py` | `make_test("ETHER=1", "TESTIMG=...")` is `make test`'s machine with a private socket, pidfile and disk COPIES, and `Private(...)` / `launch_test(...)` are the older spellings of the same - **never `make test`, and never a fixed `build/*.sock`** (§9.1). An image you READ BACK goes in `os88qemu.scratch(...)` first and is booted in place. Waits are `acted` / `ui_done` / `quiesce` on guest ticks; the mouse is `mouse_to` / `mouse_click` / `Mouse(...)`, closed-loop on `mouse_x`/`mouse_btn`; keys are `key(m, qcode)` / `type_text(sock, text)`, CONFIRMED - HMP `sendkey` loses a key to a starved guest; `xmcheck.keep(sock)` holds one QMP connection |
| 86Box | `tools/os88box.py` (docs/86BOX.md) | `Box.kernel_up(prompts=)`, `ui_done(cap_ms=)`, `until(...)`, `launch(turbo=True)` where floppy timing is not the subject (not on kern_mega) |
| Bochs | `tools/os88bochs.py` (docs/BOCHS.md) | a debugger read is ~20 ms, so every sleep becomes a wait: `Box.up()`, `ui_done`, `pkg_read`; inside a fullscreen bracket gate on `in_fsx()`. `Box(display=take_display())` for parallel machines |
| Unicorn | per row | a per-instruction Python hook is usually the whole row's cost: hook an address range, not every instruction |

### 9.1 A QEMU row shares the lane, so nothing in it may be shared

The runner puts a QEMU row in the same concurrent lane as the MartyPC rows
(docs/plans/SOAK-PARALLEL.md 17), and hands it `$OS88_QEMU_DIR`; everything
`tests/os88qemu.py` makes - `RUNDIR`, `SOCK`, `PIDFILE`, `paths()`,
`workdir()` - lives there, and a timeout sweeps only that directory. So:

- **The socket and pidfile are `os88qemu.SOCK`/`PIDFILE` or `paths()`**,
  never `build/qmp.sock` or a `build/<row>.sock`.
- **A shared image is copied or booted `snapshot=on`.** Two QEMUs on one
  writable `build/apps.img` is `Failed to get "write" lock` for the second -
  and ONE writable opener is enough: QEMU refuses even a `snapshot=on` open
  of a file another QEMU writes, so the row that pays is a well-behaved one
  (`ps2mouse` and `vesashut` did this to `wheelps2` and `vmmouse`,
  docs/plans/SOAK-PARALLEL.md 17.10).
- **A daemonised launch goes through `os88qemu.start(cmd, ...)`**, which
  raises QEMU's own stderr and names the process holding a locked image;
  `t_qemuown` fails any other spelling.
- **A fixed host port is a `locks=("port:N",)`** on the row and a
  `os88qemu.lease_port(N)` before the bind. A slirp guest reaches the host at
  10.0.2.2:N, the port is in the fixture's disks, and `guestfwd` is refused on
  the gateway - so it cannot be made private, only taken in turn. A row whose
  GUEST dials a port (dospkt's segment to 8099) holds it too, even with no
  server of its own: another row's server would answer.

---

## 10. Output

One verdict, on stdout, flushed; the runner prints it only on failure or
`-v`, so write for somebody reading a failure.

- **Collect failures, do not raise on the first** - a renumbering breaks fifty
  slots and the useful report is all fifty.
- **A failure message carries its numbers**: not "expanded WRONG" but "%d of
  %d bytes differ, first at %d, which is past the 64KB boundary".
- **A message must distinguish its causes** - check them in causal order, each
  its own sentence (incident 62).
- **Exit non-zero on failure.** Host-side rows under `tests/unit/` use
  `harness.check(cond, what, why=...)` and `done("t_name")`.
- A row that cannot test its condition exits with a sentence, or declares
  `needs=` so it SKIPS. Never report a pass about a machine it never ran on.

---

## 11. Register it, or exempt it with a reason

`tests/unit/t_registry.py` requires every file under `tests/` to be a row in
`tests/suite.py` or listed in its `UNREGISTERED` map with a reason - a
library, a fixture, a retired package's row, or a row that can only run on
demand with material never in the tree (`rdgspeed286`). The reason is the
point: an unexplained exemption is how a broken test gets filed as one never
meant to run.

```
python3 tools/os88test.py --list                     # the registry as the runner sees it
python3 tools/os88test.py soak -k '<row>'            # your row
python3 tools/os88test.py soak -k '<row>' --marty-jobs 3   # ...and loaded
```

**Run it loaded.** Passing alone and failing in a wide lane is the most common
way a new emulator row lands broken. **In a shared checkout, prefix
`MAKELEVEL=1`**: a row with `wants=` otherwise makes the runner run `make` in
`build/` under everybody else.

---

## 12. Checklist

- [ ] I broke the thing on purpose and watched the row go red.
- [ ] It is in `soak`, or I can say which `fast` family or `full` question it
      answers (§2).
- [ ] `secs` is a number I measured.
- [ ] No `builds=True`: a fixture is `wants=` (paths), a kernel is
      `os88build.tree(...).apply()`.
- [ ] No `time.sleep`, `advance(frames=N)` or `launch(boot=N)` near a machine;
      every wait is on a signal written LAST.
- [ ] Navigation is `os88ui` verbs; the boot is `quick=True` unless the boot
      screen is the subject.
- [ ] Assertions read guest state, or say why pixels are the subject;
      constants come from `os88sym.equates()`.
- [ ] Host reads of `build/` go through `os88build.at()`; the row writes
      nothing shared.
- [ ] Failures are collected, carry their numbers, and exit non-zero.
- [ ] `needs=` names every capability, so a box without one SKIPS.
- [ ] Registered with a `why` that says what breaks; passes at
      `--marty-jobs 3`.

---

## 13. The incidents

The rules above are each the shape of a row that went wrong. The list - what
happened, and the rule it produced - is **[docs/TEST-INCIDENTS.md](TEST-INCIDENTS.md)**,
numbered as before, so "incident 59" anywhere in the tree still finds it. Read
it when a rule seems arbitrary, or when a row is failing in a way you cannot
explain: the odds are good it has happened before.

---

## 14. Where the rest of it is

| document | for |
|---|---|
| **docs/TESTING.md** | what each emulator can and cannot show, per capability, and when each tier runs |
| **docs/MARTYPC-DEBUG.md** | the instrument: `launch`, `settle`, `sym`, the debug server, reading the guest's floppy |
| **docs/86BOX.md**, **docs/BOCHS.md** | those two emulators' harnesses |
| **docs/plans/SOAK-PARALLEL.md** | the parallel runner and where the suite's time goes |
| **docs/plans/completed/SOAK-SPEED.md** | the speed pass: every technique above, measured, and the per-row ledger |
| **PERFORMANCE.md** Part 7, Parts 3.1/3.2 | checking a change; flicker and smoothness harnesses |
| **`tests/suite.py`** | the registry, and its header on the tiers |
