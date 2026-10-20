# tools/notepad — Note Pad on the bench

Host-side instruments for **Note Pad**, the app. Named for the app because
that is what they only work on: the offsets are parsed out of
`apps/notepad/notepad.asm` and the labels out of its listing.

They drive a headless MartyPC through `tools/os88marty.py`, so every number
here is a **guest cycle count on a cycle-accurate 4.77MHz 8088** — the machine
the app is written for. Nothing reads a wall clock, because in this container
the guest runs at whatever multiple of real time the host manages
(docs/plans/completed/NOTEPAD-NOTES.md 6.4).

**No code is added to the guest.** The measurements are of the shipped
instruction stream.

## The other instrument, and which to reach for

`tests/npbench.inc` (`make npbench`, Ctrl-B) times a routine from **inside**,
in guest ticks, and runs unattended on the field machine. This times a **real
keystroke from outside** and can say which branch it took — something no
in-guest timer can answer.

- *How long does one walk cost on the 5150?* → `npbench`.
- *Where did this keystroke's 4.9 seconds go?* → here.

## Running it

```sh
make npbench                                     # the disk these drive
python3 tools/notepad/lab.py boot                # cold boot, open README.TXT
python3 tools/notepad/lab.py verify              # guest memory == the build?
python3 tools/notepad/lab.py press ArrowDown 20  # one line per keystroke
python3 tools/notepad/lab.py trace ArrowDown 3   # the breakdown
python3 tools/notepad/lab.py click 80 30         # price a raise or a bar hit
python3 tools/notepad/lab.py state --rows
```

`press` is the summary and `trace` is what to reach for when `press` reports
a number you cannot place.

**Where a trace ENDS decides what it measures.** A keystroke ends at
`np_redraw.out`; a click may not reach `np_redraw` at all, so it ends at
`pkg_retf` — the `retf` at offset 14 of the package header (SPEC.md §20.2),
which every callback in the format returns through. Terminating anywhere
inside the module bills the last routine's own cost to nobody: the same raise
reads 445 ms ending at `np_walk` and 1,026 ms ending at the dispatcher.

## After every rebuild, re-cut the listing and verify

The labels come from a NASM listing, and a stale one resolves every address
to somewhere plausible and wrong:

```sh
nasm -f bin -w+error -DNPBENCH -I apps/ -I tests/ \
     -l build/npbench.lst -o /tmp/x.bin apps/notepad/notepad.asm
cmp /tmp/x.bin build/npbench.bin && python3 tools/notepad/lab.py verify
```

`verify` reads the package straight out of guest RAM and diffs it against the
binary — `os88marty.py verify` for the kernel, for a package. It has already
caught a session measuring a stale disk.

## The four traps, all of which produced a confident wrong answer

Written up in full as docs/plans/completed/NOTEPAD-NOTES.md §6; the short form:

1. **A trace leg is billed to the label at its END.** A routine with no
   breakpoint of its own is invisible and its cost lands on the next traced
   label. This sent one fix to the wrong routine.
2. **`run` does not resume from a breakpoint** — MartyPC stays latched in
   `BreakpointHit` and advances nothing, reporting success and zero cycles.
   `Tracer.collect` does `step(1)` then `advance()`.
3. **An `advance` budget is a silent truncation**, and reads as "the path
   ended there".
4. **A fixed warmup does not put two builds in the same state** when they
   differ in how long a keypress takes. Read the state back, and compare the
   runs only where they agree.
5. **Injected input is queued, not delivered.** A bare `m.key()` or `m.mouse()`
   followed by `advance(frames=…)` can run no guest time at all — every
   `advance` leaves the machine paused, and the input then turns up during
   some *later* advance, so a run inherits a press nobody asked for. Use
   `drive.tap()` / `chord()` / `click()` / `dclick()`, which give each event
   its own `step(1)` and guest time. It bites the mouse twice over: the serial
   mouse is 1200 baud and `mou_isr` decodes three bytes at a time, so packets
   sent faster than the UART carries them overrun its receive register —
   `tools/mouse.py`'s pacing rule for QEMU, true here for the same reason.
6. **Wait on a condition, never on a clock.** `drive.wait_until()` advances
   guest time until the thing has actually happened. A wall sleep cannot be
   matched across two builds that differ in speed, which is trap 4 again and
   §6.1's whole wrong answer.

## Files

| | |
|---|---|
| `lab.py` | the CLI; everything below is a module it uses |
| `pkg.py` | find the running package by its own header, resolve labels, verify against the build |
| `state.py` | Note Pad's bss, with the guard that refuses stale offsets |
| `trace.py` | the breakpoint tracer |
| `drive.py` | cold boot, and the mouse work to get README.TXT open |
| `pixcheck.py` | is the incrementally-drawn content the same pixels a FULL repaint makes? Covers the window and raises it to force one, then diffs the content rect. The check that cleared SPEC.md §27.13 and found docs/plans/completed/NOTEPAD-NOTES.md §5.2.1. **Run `--self-test` first** (below) |
| `notecheck.py` | is the document BUFFER right? Reads the claim out of guest RAM and diffs it against README.TXT with the session's edits applied on the host. What cleared §27.12's `rep movsb` |

## A check that cannot fail is not a check

```sh
python3 tools/notepad/pixcheck.py --self-test    # ...before believing a zero
python3 tools/notepad/pixcheck.py
```

`pixcheck.py`'s "forced full repaint" is a raise, and SPEC.md §11.96's raise
cache can serve a raise **without calling `W_PAINT` at all** — which makes the
forced repaint a byte copy of the capture it is being compared against, so it
agrees with itself on any build, correct or not. That is not hypothetical: it
is how SPEC.md §11.96.2's blank window shipped, past a verification that
counted `W_PAINT` calls and found the expected number of them.

So `--self-test` asks the two questions the check rests on, and **fails loudly
rather than passing quietly**:

| leg | state | what must happen |
|---|---|---|
| 1 | the cache left armed | `np_paint` must **not** run — so a tautological pass is reachable *and caught* |
| 2 | cache armed, `WF_SAVEU` cleared afterwards | `np_paint` **must** run — so withdrawing the promise defeats a bank that already exists (§11.96.2's test in `wm_su_ck`) |

Exit codes: 0 both legs passed, 3 inconclusive (no cache was banked — a
refused claim looks like this), 4 leg 1 failed, 5 leg 2 failed. Leg 2 was
itself verified by breaking it: NOP `wm_su_ck`'s `jz` in the running guest and
it exits 5 and names the missing kernel test.

**The generalisable part is the shape, not the flag.** Any host-side check of
the form *"drive it, then force the reference state, then diff"* is only worth
what its control is worth — and the failure is always silent, because a
reference state that was never produced reads as agreement. `win_geom`,
`front_win` and `cache_for` are importable for that reason; the module has a
`__main__` guard so importing it runs nothing.

**Crop, and settle, before believing a diff.** An uncropped framebuffer diff is
dominated by the menu-bar clock and the other window's title bar; an unsettled
one is dominated by a toast expiring. Both produced a confident wrong "regression"
in one session. `pixcheck.py` crops to `np_tx-8 .. np_rgt` by construction, and
an idle screen sampled twice must read 0 differing pixels before any other
number from it means anything.
