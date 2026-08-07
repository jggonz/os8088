# What can actually be tested, and where

**Short answer: QEMU covers all three video adapters and all three sound
routes.** 86Box is needed for exactly two things, and they are narrow.

This document exists because the opposite keeps getting concluded. It has
happened for Hercules — `docs/HERCULES-TESTING.md` opens by saying so, and
that claim had sat in CLAUDE.md costing people time — and it keeps happening
for sound, for a duller reason: the AdLib and Sound Blaster recipes are real,
committed and mechanical, but they live in the middle of
`docs/SOUND-PLAN.md`, an 850-line *plan*, interleaved with phase history. A
plan document is not where anyone looks to answer "can I test this?", so the
answer people reach is "no".

Every recipe below was run end to end on a stock QEMU 8.2.2 and the measured
result is quoted with it. If one of them fails, that is a finding about the
tree, not about the emulator.

---

## The matrix

| Capability | QEMU | How | Verified result |
|---|---|---|---|
| VGA 640x480x16 | ✅ | `make test` | boots to Locator; loads packages |
| CGA 640x200 mono | ✅ | `make test VIDEO=cga` | renders; dumps 640x400 (line-doubled) |
| Hercules 720x348 mono | ✅ | `make test VIDEO=herc HERCSEG=0x7000` | renders; 55.8% lit at the desktop |
| PC speaker | ✅ | `make test-snd` (no card) | dominant 880.0 Hz |
| AdLib / OPL2 | ✅ | `make test-snd ADLIB=1` | dominant 880.0 Hz from a keyed 440 |
| Sound Blaster 16 | ✅ | `make test-snd SB16=1` | 2.00 s at 1000.0 Hz |
| Scripted mouse / keys | ✅ | `tools/mouse.py`, `tools/qmp.py` | all adapters, incl. Hercules |
| Performance benchmarks | ✅ | `make bench` (from `tests/`, not in `all`) | numbers are always in flux — see below |
| Video **detection probe** | ❌ | `make xt-cga` / `xt-hercules` | 86Box only |
| 6845 programming | ❌ | `make xt-hercules` | 86Box only |
| Period-correct timing | ❌ | `make xt` (4.77 MHz), `286`, `386` | 86Box only |

`VIDEO=` forces a code path; it does not exercise the probe that would have
chosen it. That distinction is the whole of the ❌ column for video: QEMU
emulates no CGA and no Hercules card, so what is untestable here is the
*choosing*, not the *drawing* — and the drawing is almost all of the code.

---

## Video

CGA works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA
framebuffer, so an ordinary `screendump` shows it. Note the dump comes back
**640x400** — QEMU line-doubles 640x200 — so a crop's Y and H are twice the
kernel's own. VGA is 1:1.

```sh
make test VIDEO=cga
python3 tools/shot.py build/qmp.sock /tmp/cga.png
python3 tools/mouse.py --screen 640x200 build/qmp.sock click X Y
```

Hercules needs its framebuffer relocated into spare RAM (B0000 is unmapped
under QEMU and silently swallows every write), and it is **never**
screendumpable — that framebuffer is guest RAM the VGA device has never heard
of, so `screendump` returns a black or stale VGA screen and does not error.
That silent non-failure is how "Hercules doesn't work" gets concluded from
one screenshot.

```sh
make test VIDEO=herc HERCSEG=0x7000
python3 tools/hercshot.py build/qmp.sock 0x70000 /tmp/herc.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock click X Y
```

`HERCSEG` is a segment and `hercshot` takes the linear address; the missing
zero is the commonest mistake. Full recipe and the four ways to get it
silently wrong: `docs/HERCULES-TESTING.md`.

**`VIDEO=`/`RTC=` are tracked by a stamp file**, so a knob-built kernel is a
*different* kernel and changing the knob rebuilds it. Nothing in `build/` is
committed, so a forced kernel can no longer reach the repo — but it does stay
on your disk images until something rebuilds them, and a release must be built
knob-free:

```sh
rm -f build/os8088.img build/os8088-360.img && make
```

---

## Sound

`make test-snd` is `make test` plus a wav capture at `build/snd.wav`,
finalized when QMP `quit` stops QEMU — so **run `tools/sndcheck.py` only
after `quit`**, or you measure a partial file. The capture is stream-on time,
not wall time: a silent boot yields an empty file, which is a pass for
`--expect-silence` rather than a broken harness.

Without `ADLIB=1`/`SB16=1` there is no card, the tone route falls to the PC
speaker, and that is what gets captured:

```sh
make test-snd TESTAPPS=build/fmtest.img
# launch FMTEST, then:
python3 tools/qmp.py build/qmp.sock 'sendkey b' 'sleep 2' 'quit'
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz
```

The two gate packages are the mechanical checks. Neither ever ships on the
apps disks — each rides its own scratch image.

```sh
# AdLib: click once. The patch sets carrier MULT=2, so a keyed 440 must SOUND
# at 880 - that doubling is the assertion, and it only holds if the caller's
# patch bytes reached the operator registers.
make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz

# Sound Blaster: click once for a synthesised 1 kHz square, staged in 20
# chunks and played for 2 s.
make test-snd SB16=1 TESTAPPS=build/sbtest.img
python3 tools/sndcheck.py build/snd.wav 1000         # -> 2.00 s at 1000.0 Hz
```

The window says which half failed: FMTEST shows `K` (both verbs fine), `P`
(patch refused) or `N` (note-on refused), and a bare `N` means the frequency
never reached the driver. SBTEST shows `g:` grant and `o:` open.

`make test ADLIB=1` (without `-snd`) is the same card with no capture — the
right thing when you want to watch the driver attach rather than measure a
tone. **With no card the probe correctly finds nothing**, which is the right
answer and not the one you are trying to test; `sound.drv` ships on the boot
disk and `drv_boot` loads it before the first paint, so a driver that failed
to attach announces itself by opening the Control Panel on its Drivers page.

Depth, including the underrun and capture edge cases: `docs/SOUND-PLAN.md`
Phase 4.

---

## Everything not shipped lives in `tests/`

`tests/` holds every package that is not shipping software, and it is **not**
`apps/`. Nothing under it is built by `all`, no artifact of it is tracked,
and none of it reaches a shipped floppy — so a normal build and every image
the project ships are exactly what they were before it existed.

Two kinds live there, and the difference is what they assert.

**Gates** answer pass/fail against a capability, and are the mechanical
checks referenced throughout this document:

| Package | Asserts | Run it with |
|---|---|---|
| `fmtest` | the AdLib FM surface (SPEC.md §34.2/§51.4) | `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img` |
| `sbtest` | the Sound Blaster streams (§34.5/§34.6) | `make test-snd SB16=1 TESTAPPS=build/sbtest.img` |
| `filetest` | the write path (§18.4) | `make test TESTAPPS=build/filetest.img` |

`filetest` also has a fragmented-volume variant, `build/filetest-frag.img`,
and its results are worth pairing with the host-side fsck — the in-kernel
free-space check and `python3 tools/os88disk.py --verify <img>` catch
different bugs.

**Benchmarks** answer *how fast*. `fontbench` prices the *primitive* (SPEC.md
§6.1.1): one ten-character run drawn four ways, as the hand-written
`gfx_fill` + `font_str` pair and as one `font_run`, each byte-aligned and
again at x+5. `typebench` prices the *keystroke* (§11.94): 40 characters typed
into a 40-cell line with the whole line redrawn after each, which is what
`np_redraw` does to its dirty band.

```sh
make bench                                                 # build the two disks
make test                            TESTAPPS=build/bench.img
make test VIDEO=cga                  TESTAPPS=build/bench.img
make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
```

Every one of these images builds on demand — `TESTAPPS` is a prerequisite of
the test targets, so naming one is enough. `make bench` exists for building
the two benchmark disks *without* booting, e.g. to write `bench360.img` to a
real floppy.

The rest of this section is about the benchmarks, because a gate's answer is
a boolean and does not rot the way a number does.

**The `testing` branch still exists, and is now for developing these**, not
for holding them. A harness takes several rounds to get right — two of the
three corrections below were to the measuring apparatus, not the thing
measured — and that iteration does not belong in `experimental`'s history. A
finished harness lands here; the midway artifacts of writing one stay there.

**Treat every number as provisional and cite where it came from.** This is
not a caveat about tidiness — the figures have been wrong in ways only real
hardware exposed, twice in quick succession: the elapsed counter was 16-bit
and a real run overflowed it, and then the ratio overflowed because it came
from counts shifted right by 4 that real rows exceed. A third correction went
the other way: SPEC.md §6.1.1 predicted `font_run`'s true win sat near the
framebuffer-traffic figure, and a 4.77 MHz 8088 with a Hercules card measured
1.30x — the *instruction* figure to three digits. Per-cell overhead dominates
the byte-writes it guards. So a benchmark number quoted without a date and a
machine is worth very little.

**Under QEMU the numbers are not time at all.** QEMU runs the guest at host
speed, so add `-icount shift=3,sleep=off` and the PIT counts guest
*instructions* — reproducible and machine-independent, but not microseconds,
and it understates the mono win because what alignment removes is
disproportionately memory traffic. `build/bench360.img` on a real 4.77 MHz
8088 (or 86Box) is where the PIT is a wall clock and the microsecond column
means microseconds. That is where these numbers are worth taking. A VGA run
measures the *fallback* path by design — `font_run`'s fast path is mono-only.

Nothing under `build/` is committed — bench artifacts included, along with the
shipped images themselves — so there is no way for one of these disks to reach
the repo or a release. What keeps them off a normal build is `all`, which
builds nothing from `tests/`: that is the arrangement this folder exists for.

---

## Modelling the old machine from a fast one

Everything above is about *where* to run a test. This is about the systematic
error in running it anywhere but the target, and it has now cost four bugs, so
it is worth stating as a method rather than a warning.

**The container is roughly three orders of magnitude faster than a 4.77 MHz
8088.** Every constant you size while looking at QEMU is sized against the
wrong machine, and the failures are not proportional — they are structural,
because the constants encode *ranges*:

| what was sized against QEMU | what a real XT did |
|---|---|
| a 16-bit elapsed counter, one subtraction start-to-end | rows are 1.5M counts; it lapped silently into a small plausible number |
| `>= 32768 means the run overran` | most legitimate rows are 32768..65535; it discarded them |
| a ratio computed from `counts >> 4` | `>> 4` is still 90,000; it overflowed the word and printed 696 for 134 |
| `OSAPI_WM_GROW` on every keystroke | free in the emulator; a visible flicker in a 13×13 corner at 33 ms a keystroke |

The rule that falls out: **when a harness has to hold a range, size it from the
slowest machine it will ever run on, not the one in front of you.** A 32-bit
accumulator folded per iteration costs a few instructions and cannot lap; a
16-bit one sized "generously" against QEMU is wrong by 20x on hardware.

### Two calibration numbers, so an estimate needs no machine

- **About 1 ms per 8×8 glyph cell** on a 4.77 MHz 8088 with a Hercules card.
  Two independent harnesses agree: `fontbench` 10.09 ms per ten cells,
  `typebench` 33.3 ms per forty. A 40-cell line redraw is ~33 ms, so a
  keystroke that redraws its row costs about that.
- **Instructions are the better proxy, not framebuffer traffic.** SPEC.md
  §6.1.1 predicted the opposite and was corrected by measurement: per-cell
  overhead dominates the byte-writes it guards.

### Count work, don't time it — QEMU is exact about the first and useless at the second

The container's clock tells you nothing about a 4.77 MHz machine, but the
*amount of work* the guest does is identical on both, and QEMU will report it
exactly. So when the question is "is this slow because it does too much?",
**instrument a counter and read it over QMP** rather than reaching for 86Box:

```nasm
; kernel/font.inc, in .text so the offset is fixed
dbg_cells:  dw 0
...
font_run_cell:
    inc word [cs:dbg_cells]
```

```sh
nasm ... -l /tmp/k.lst   &&  grep dbg_cells /tmp/k.lst     # -> 0x1E78
python3 tools/qmp.py build/qmp.sock 'xp /2xh 0x2478'       # KERNEL_SEG*16 + off
```

`h` is a word; HMP's `w` is four bytes. Editing any include **before** the one
holding the counter moves the offset, so re-derive it after every rebuild.

A **package** can write the same counter — `mov ax, KERNEL_SEG / mov es, ax /
inc word [es:0x1E7E]` — which is how a walk inside an app is counted without
knowing the segment its region was claimed at.

This is what settled the Note Pad question (SPEC.md §27.4). A user reported
typing getting slower as a note grew and inferred that more than one character
was being redrawn. The cell counter said **2 cells per keystroke at every note
length and every window width** — the drawing was already right — and a
counter in the layout walk said 404 iterations, growing linearly. The cost was
in a place no screenshot could show and no wall clock here could measure.

Two rules that fall out of it:

- **Measure before redesigning.** The obvious hypothesis (the delta span is
  growing) was wrong, and the fix it would have produced was a fix to working
  code.
- **A counter is not a timer.** It tells you how many times something ran, not
  what it cost. Multiply by the calibration numbers above to get milliseconds,
  and say that you did — ~500 8086 cycles per walk iteration is a reading of
  the instruction stream, not a measurement.

### Prefer a self-checking harness to a careful one

Three of the four bugs above were caught by **one number on screen
contradicting another**, not by inspection:

- `typebench`'s CHAR row does 1.33x `fontbench`'s PAIR work, so it cannot be
  the smaller number — yet PAIR reported the overrun sentinel and CHAR
  reported 15551. Only one reading is consistent, and it identified the lap
  and its size.
- The ratio was wrong while the counts and milliseconds beside it were right,
  which localises the fault to the one column computed differently.

So put **redundant quantities on the screen**: a raw count *and* a derived
time, two rows whose relative sizes are known in advance, a ratio you can
recompute by hand from the columns next to it. A harness that reports one
number per run is one you have to trust.

### What the emulator cannot show at all

Not "shows inaccurately" — cannot show:

- **Flicker.** The erase-and-letter pair leaves a line blank between the fill
  and the last glyph. On an XT that gap is tens of milliseconds and plainly
  visible; under QEMU it is microseconds and invisible at any frame rate a
  screendump can sample. Note Pad's per-keystroke flash (SPEC.md §27.2) and
  the grow box's were both found by a person watching the real machine, and
  neither appears in any timing column, because the two methods take
  comparable *time* and differ in what is on screen during it.
- **Perceived latency and input overrun.** Whether a human can outpace the
  redraw — and start losing keystrokes to a full BIOS buffer — is a property
  of the real machine's speed against a real person's typing.

For both, the emulator's role is to prove *correctness* before you burn a
floppy. The judgement is made on hardware.

## What 86Box is genuinely for

Real period hardware: the video **detection probe**, the 6845 programming, a
4.77 MHz 8088's actual timing, a real CGA or Hercules card, an SB 2.0 on an
XT bus, and the 286/386 machines. `make xt`, `xt-640`, `xt-cga`,
`xt-hercules`, `xt-sound`, `286`, `286-sound`, `386sx`, `386`, `386-sound`,
`486`, `pentium`.

The last two are the *fast* end rather than the period end: a 486DX2/66 and a
Pentium 133, both with an SB16. 8086 real-mode code runs on them verbatim, so
what they answer is whether the constants sized against a 4.77 MHz 8088 still
behave two orders of magnitude up — typematic deadlines, the tracker's ring
refill, Arkanoid's frame pacing. QEMU cannot answer that either: it does not
model a clock speed at all.

It is not installed in the web container and needs BIOS ROMs, so those
targets do not run there. Nothing above them does.

Three 86Box-specific traps worth knowing before blaming the OS: it silently
clamps `mem_size` to the machine's maximum; a `wp://` prefix on an
`fdd_0N_fn` path mounts that floppy write-protected — which the OS then
faithfully reports as "Write protected", and which means `SYSTEM.CFG`
settings do not survive a reboot; and an unrecognised `cpu_family` is
**silently replaced** rather than rejected, at that family's *default* speed.
`cpu_family = pentium` is not a name 86Box knows: it boots a P54C at 75MHz
while the config still says 133. The cheap check for any candidate machine or
CPU is the one in CLAUDE.md — launch 86Box on a throwaway copy of the config,
`kill -TERM` it, and read the file back, because 86Box rewrites it on exit
with whatever it actually accepted.
