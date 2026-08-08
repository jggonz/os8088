# tools/notepad — Note Pad on the bench

Host-side instruments for **Note Pad**, the app. Named for the app because
that is what they only work on: the offsets are parsed out of
`apps/notepad/notepad.asm` and the labels out of its listing.

They drive a headless MartyPC through `tools/os88marty.py`, so every number
here is a **guest cycle count on a cycle-accurate 4.77MHz 8088** — the machine
the app is written for. Nothing reads a wall clock, because in this container
the guest runs at whatever multiple of real time the host manages
(docs/NOTEPAD-NOTES.md 6.4).

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
python3 tools/notepad/lab.py state --rows
```

`press` is the summary and `trace` is what to reach for when `press` reports
a number you cannot place.

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

Written up in full as docs/NOTEPAD-NOTES.md §6; the short form:

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

## Files

| | |
|---|---|
| `lab.py` | the CLI; everything below is a module it uses |
| `pkg.py` | find the running package by its own header, resolve labels, verify against the build |
| `state.py` | Note Pad's bss, with the guard that refuses stale offsets |
| `trace.py` | the breakpoint tracer |
| `drive.py` | cold boot, and the mouse work to get README.TXT open |
