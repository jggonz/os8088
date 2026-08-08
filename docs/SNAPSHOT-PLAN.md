# Saving and restoring machine state

**The question.** Can an agent dump MartyPC's whole state at a chosen point —
say when a watched value changes — reload it later, and continue from there, so
that a test starts from a *known* state rather than from whatever a fresh boot
and two minutes of clicking happened to produce?

**The short answer.** Yes, twice over. The emulator turns out to be **bit-exact
deterministic**, which makes a replay of the inputs exact on its own; and a
`fork()` snapshot makes the same guarantee instant. Both are **built and
working** — §7 and §8 are the patterns to copy. C (a serialized save file) is
tabled until something needs durability.

The two compose, and that is the intended workflow: **A drives you to the
state, B freezes it.** Note what that does to A's requirements — once you
snapshot, the navigation no longer has to be *reproducible*, because the
snapshot captures the state you actually reached rather than a state you hope
to re-reach. Guest-time pacing is still worth having (it is what makes a
scripted click land on the same thing twice), but it stops being load-bearing
the moment a snapshot exists.

---

## 1. What is already there

**No save-state support in MartyPC 0.4.2** (our pin). `marty_core` has no
serialize/restore path for the machine; the only files mentioning "snapshot"
are the keyboard modules, about something else. `serde` is a dependency but no
device struct derives `Serialize`, and only the PIC derives `Clone`.

**Memory watchpoints work**, which is half the request already answered. A
watchpoint on the BIOS tick counter stopped the machine within three seconds:

```sh
python3 -c "... m.breakpoints([{'type':'mem','addr':0x46C}]); m.run()"
#  -> state=breakpoint  cs=0060 ip=3124
```

`bp` takes `exec`, `execseg`, `mem`, `memseg`, `int` and `io`. So "stop when a
monitored value is touched" is available today; what is missing is only the
dump-and-reload either side of it.

---

## 2. The foundation: the emulator is deterministic

Two **independent processes**, same config, same floppy, an exec breakpoint on
the kernel's first instruction:

| | port 9911 | port 9912 |
|---|---|---|
| cycles at the breakpoint | 261,943,446 | 261,943,446 |
| instructions | 21,436,400 | 21,436,400 |
| SHA-256 of the whole 1 MB | `95c3eee02b541b52` | `95c3eee02b541b52` |

And it stays exact **through injected input**, which is the part that was not
obvious. Clearing the breakpoint, stepping a fixed 200,000 instructions,
injecting a keystroke, and stepping again:

| stage | cycles | instructions | memory |
|---|---|---|---|
| +200k | 264,816,109 | 21,634,476 | identical |
| inject `KeyA`, +200k | 267,591,794 | 21,834,469 | identical |
| +200k | 271,138,679 | 22,021,672 | identical |

Every figure matched across both processes. **Given the same starting image and
the same inputs at the same guest positions, MartyPC produces the same machine,
bit for bit.**

### 2.1 …and a wall-clock client destroys it

This is the sharp edge, and it is not a defect in the emulator. The same two
processes, free-running, paused after an identical `sleep(22)`:

| | port 9941 | port 9942 |
|---|---|---|
| cycles | 420,699,609 | **398,980,749** |
| memory | `94e91ca4dccf153f` | `d4d7e46e5598c604` |

**21.7 million cycles apart — 4.5 seconds of guest time — from one sleep.** The
emulator runs at ~3.8x real time and the host scheduler does not divide itself
equally between two of them, so a wall-clock wait lands at a different guest
position in each. Everything downstream diverges.

Two consequences, and the second is about work already in this tree:

- **Any replay must position its inputs in GUEST time** — cycles,
  instructions, frames, or a breakpoint — and never in `time.sleep`. The
  `flicker`/`pace` protocol already has this discipline (inject while paused,
  advance by frames), which is why those measurements repeat.
- **Scripted mouse navigation used to be wall-clock paced and was therefore
  NOT reproducible run to run.** `tools/os88drive.py` is that driver re-paced
  on frames; §7 measures the difference. Anything else still written with
  `time.sleep` around a running machine has the same defect, and it is the
  likeliest reason a measurement moves slightly between sessions.

---

## 3. What a snapshot would have to cover

`Machine` owns `cpu: CpuDispatch`, and the CPU owns the `BusInterface`, which
owns everything else:

- `memory: Vec<u8>` — the bulk, 1 MB
- ~25 optional device slots: `pit`, `pic1`/`pic2`, `dma1`/`dma2`, `ppi`,
  `serial`, `parallel`, `fdc`, `hdc`, `xtide`, `jride`, `mouse`, `ems`,
  `fantasy_ems`, `cart_slot`, `game_port`, `adlib`, `sblaster`, `sound_source`,
  `sn76489`, `a0`, `keyboard`, plus `videocards`
- machine-level counters: `cpu_cycles`, `cpu_instructions`, `system_ticks`,
  `kb_buf`, `events`

**Much of the `BusInterface` is derived, not state**: `timing_table`, `io_map`,
`mmio_map`, `desc_vec`, `cycles_to_ticks` are all built from the config at
construction. A restore should *rebuild* them by constructing the machine
normally and then overwriting only the mutable parts — that is a large
reduction in what has to be serialized, and it removes the whole class of bugs
where a saved lookup table disagrees with the config it came from.

**The serialization blockers**, counted across `devices/`, `bus/` and
`cpu_808x/`: `Instant` in 1 file, `Sender<…>` in 5, `File` in 4, `Box<dyn …>`
in 5, `Arc` in 2. Each needs `#[serde(skip)]` plus a re-attach step on load —
the audio channels in particular are live endpoints owned by the frontend, not
state.

**The floppy images are mutable state too.** os8088 writes `SYSTEM.CFG`, and a
snapshot that restores RAM but not the disk image restores a machine whose disk
has moved on. Either the images go in the snapshot or a restore must re-mount
them from a pristine copy.

---

## 4. Three ways to do it

### A. Deterministic replay — zero emulator work, available now

Record every input with the **guest position** it was delivered at; to restore,
boot a fresh machine and replay the log to that position. Correct by §2.

- **Cost per restore:** 18.5 s wall to a settled desktop (71 s of guest time at
  3.8x real time), plus the replay of whatever navigation the state needed.
- **Strength:** no emulator change, no format to get wrong, and it reaches *any*
  point — including one you did not think to snapshot.
- **Weakness:** the deep states are the ones worth iterating on, and they are
  the expensive ones. Getting to "Tracker fullscreen playing a module" took
  ~2 minutes of scripted clicking and a 50-second module load. Paying that per
  iteration is the whole problem.

### B. `fork()` snapshot — instant, exact, in-memory

At the snapshot point the emulator forks; the child blocks, holding a
copy-on-write image of the entire process — every device, the CPU, all memory,
bit for bit, with **no serialization code at all**. Restore hands control back
to a re-fork of the child.

- **Viability confirmed:** the headless path spawns **no threads** (the only
  `thread::spawn` calls are in `cpu_test`, which we never run), so `fork` has
  no threading hazard.
- **Cost:** the debug server's TCP listener is the one awkward part — simplest
  is for the restored child to bind a new port and have the client reconnect.
  Perhaps 100–200 lines.
- **Weakness:** in-memory only (gone when the process exits), Linux-only, and
  a chain of snapshots is a chain of live processes.

### C. Serde snapshot to a file — the "real" feature

Derive `Serialize`/`Deserialize` across the CPU and the ~25 devices, skip and
re-attach the blockers, and rebuild the derived tables on load.

- **Strength:** persists across runs and across machines. A state could be
  committed, shared, or attached to a bug report.
- **Cost:** the largest of the three by a wide margin, and it lands in
  `tools/martypc/patches/`, which is carried against a pinned upstream — the
  patch is ~700 lines today and this would multiply it.
- **The risk that matters:** a snapshot missing one field restores a machine
  that looks right and is not. That is the exact failure shape this tree keeps
  getting bitten by — `peek_range` returning zeroes rather than erroring, the
  VGA reporting `graphics: false`, the cursor flattering `pace`. A partial
  snapshot would be the worst of them, because everything downstream inherits
  it silently.

---

## 5. Recommendation (and what was built)

**Do A now and B if the iteration cost justifies it. Do not start with C.**

A is available today at zero cost and is *definitively* correct — §2 is the
proof, not an argument. The discipline it needs (inputs positioned in guest
time) is one this harness should adopt regardless, because §2.1 shows the
current navigation helper is not reproducible without it.

B is the upgrade that actually addresses the complaint. It is instant, it is
exact for the same reason a process image is exact, and it needs no format —
which means it cannot be *subtly* wrong, only obviously broken. The threading
check above is the main thing that could have ruled it out and did not.

C is a real feature and a poor first move: the most work, carried against a
pin, for the one benefit (durability) that neither A nor B provides but that
nothing in the current workflow has actually asked for. If it is wanted later,
the honest way in is to build C's *verification* first — restore a snapshot and
diff the full machine against a replayed one, using §2's determinism as the
oracle. A snapshot format that cannot be checked against a known-good state is
a snapshot format nobody should trust.

### What was built

All three of the steps this section originally proposed, and C was left alone:

1. **`advance`** — the guest-time primitive (`frames=` or `cycles=`), plus a
   cycle stamp on every `key`/`mouse` reply and an input log in the client.
2. **`tools/os88drive.py`** — the mouse/menu driver, re-paced on frames.
3. **`snapshot`/`restore`** — the fork holder, restorable any number of times.

§7 and §8 below are how to use them.

---

## 6. Measured facts, for anyone re-costing this

| | |
|---|---|
| boot to settled desktop | **18.5 s wall**, 71 s guest, **3.8x real time** |
| two processes to the same breakpoint | 261,943,446 cycles, identical 1 MB hash |
| divergence from one 22 s wall-clock sleep | **21.7 M cycles** (4.5 s guest) |
| memory watchpoint latency | fired within 3 s on a 18.2 Hz counter |
| devices to cover | ~25 slots + CPU + 1 MB + 3 machine counters |
| serialization blockers | `Instant` x1, `Sender` x5, `File` x4, `Box<dyn>` x5, `Arc` x2 |
| threads in the headless path | **none** |


---

## 7. Pattern A — guest-time pacing and replay

**The rule: never wait in wall time while the machine is running.** Use
`advance`, which runs a bounded amount of *guest* time and stops.

```python
m.advance(frames=30)      # 30 completed video frames (~0.5 s on a 60 Hz card)
m.advance(cycles=500000)  # stops at the first instruction boundary past it
```

A breakpoint ends either one early and `state` says so, so `advance` doubles as
"run until something happens, but not forever".

### Driving the UI reproducibly

`tools/os88drive.py` is the mouse/menu driver, paced on frames. Use it exactly
like the old one — `home()`, `goto()`, `click()`, `dblclick()`, `menu()` — and
scripted navigation becomes bit-exact:

```python
from os88drive import Pointer
p = Pointer(m, 640, 200)
p.home(); p.dblclick(608, 105); m.advance(frames=500)
```

Measured, two independent processes driven from reset by that exact script:

| | port 9991 | port 9992 |
|---|---|---|
| after `advance(frames=1500)` | 120,157,285 cycles | 120,157,285 |
| memory | `c28e95b1f66d918c` | `c28e95b1f66d918c` |
| after the scripted navigation | 167,309,139 cycles | 167,309,139 |
| memory | `27a1854a328be5a4` | `27a1854a328be5a4` |

**Identical, cycle for cycle and byte for byte.** The same script written with
`time.sleep` diverged by 21.7 million cycles (§2.1).

`dblclick` is the case that shows why this matters beyond reproducibility:
SPEC.md §13's double-click window is 9 ticks of *guest* time, so a wall-clock
script can miss it on a loaded host and hit it on an idle one.

### Replaying

Every `key` and `mouse` call is stamped with the guest cycle it landed at:

```python
log = m.input_log()          # [{'cycles': …, 'kind': 'mouse', 'args': {…}}, …]
fresh.replay(log, settle=60) # re-drive them at the same guest positions
```

`replay` advances to each entry's cycle position before delivering it. It needs
a machine from the same image at or below the first entry's cycle count — in
practice, a fresh one.

---

## 8. Pattern B — fork snapshots

**A snapshot is a process, not a file.** `fork()` gives a copy-on-write image of
everything: the CPU, all memory, every device, the video card's raster
position, the FDC's pending command — bit for bit, with no serialization. That
is the property that matters: **nothing can be left out of it**, which is the
one guarantee a hand-written save format cannot make.

```python
m = Marty("127.0.0.1:9001")
#  … drive the machine to the state you care about, however you like …
s  = m.snapshot()                 # {'id': 1, 'pid': 746, 'cycles': 167309139}
r  = m.restore(s["id"], 9995)     # a Marty connected to the restored machine
#  … break it, measure it, poke it …
r.quit()
r2 = m.restore(s["id"], 9995)     # the SAME snapshot again, pristine
```

Measured end to end:

| | cycles | memory |
|---|---|---|
| original at the snapshot | 167,309,139 | `27a1854a328be5a4` |
| restored #1 | **167,309,139** | **`27a1854a328be5a4`** |
| …after `advance(frames=600)` | 215,098,097 | `fb09987d44fc12fa` |
| restored #2, same snapshot | **167,309,139** | **`27a1854a328be5a4`** |
| the original, untouched throughout | 167,309,139 | `27a1854a328be5a4` |

### Four things to know

- **The connection you snapshot from stays the registry.** A restored machine
  is a *different process* and knows nothing about the snapshot list. Keep the
  original connection open to restore again; open a second connection to
  whatever you are currently driving. `m.restore()` returns that second
  connection for you.
- **A snapshot is reusable.** The holder re-forks itself before going live, so
  the same `id` can be restored any number of times, always from the same
  state. Pick a fresh port each time, or quit the previous one first.
- **You choose the port.** `restore(id, port)` binds it; the client polls until
  it answers.
- **Watchpoints are how you choose the moment.** `bp mem <flat>` stops the
  machine when a value is touched, which is exactly the "snapshot when this
  changes" case:

  ```python
  m.breakpoints([{"type": "mem", "addr": 0x46C}])
  m.run()                       # …until it stops
  s = m.snapshot()
  ```

### Limits

- **Unix only**, and gone when the process exits. Nothing persists to disk;
  that is C, and C is tabled.
- **Each holder is a live process** holding a copy-on-write image. They are
  cheap until they are not — a long chain of them is a long chain of processes,
  so quit the ones you are done with.
- **A holder's audio capture is turned off** on fork, because two processes
  appending to one wav produce a file that is neither of them. Only the live
  machine writes. A restored machine inherits the *file handle* of whatever
  `MARTYPC_WAV` named, so if you care about audio after a restore, point the
  restored run at its own capture.
- **The floppy image is shared, not copied.** A snapshot restores the machine's
  RAM and devices exactly; it does not roll back writes the guest already made
  to the mounted image. If the test writes to disk — os8088 saves `SYSTEM.CFG`
  on a Control Panel close — restore from a pristine copy of the image, or
  accept that the disk has moved on while the RAM has not.
