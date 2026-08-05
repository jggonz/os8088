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
*different* kernel. Rebuild knob-free before committing or `make
check-images` reports STALE:

```sh
rm -f build/os8088.img build/os8088-360.img && make && make check-images
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

## The testing apps live in `tests/`, and their numbers move

`tests/` holds testing apps, and it is **not** `apps/`. Nothing under it is
built by `all`, no artifact of it is tracked, and none of it reaches a
shipped floppy — so a normal build and every image the project ships are
exactly what they were before it existed. It is on-demand only:

```sh
make bench                                                 # build the two disks
make test                            TESTAPPS=build/bench.img
make test VIDEO=cga                  TESTAPPS=build/bench.img
make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
```

`make test TESTAPPS=…` builds the disk on its own — `TESTAPPS` is a
prerequisite of the test targets — so `make bench` is for building without
booting, e.g. to write `build/bench360.img` to a real floppy.

Two packages so far. `fontbench` prices the *primitive* (SPEC.md §6.1.1): one
ten-character run drawn four ways, as the hand-written `gfx_fill` +
`font_str` pair and as one `font_run`, each byte-aligned and again at x+5.
`typebench` prices the *keystroke* (SPEC.md §11.94): 40 characters typed into
a 40-cell line with the whole line redrawn after each, which is what
`np_redraw` does to its dirty band.

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

One trap if you ever track a bench artifact: `make check-images` reads its
list from `git ls-files build`, so a tracked image `all` does not build reads
as **ORPHAN** and one it builds differently reads as **STALE**. Leaving them
untracked is what lets `all` stay free of them. Tracking one would force it
back into `all` — which is exactly the arrangement this folder replaced.

---

## What 86Box is genuinely for

Real period hardware: the video **detection probe**, the 6845 programming, a
4.77 MHz 8088's actual timing, a real CGA or Hercules card, an SB 2.0 on an
XT bus, and the 286/386 machines. `make xt`, `xt-640`, `xt-cga`,
`xt-hercules`, `xt-sound`, `286`, `286-sound`, `386sx`, `386`, `386-sound`.

It is not installed in the web container and needs BIOS ROMs, so those
targets do not run there. Nothing above them does.

Two 86Box-specific traps worth knowing before blaming the OS: it silently
clamps `mem_size` to the machine's maximum, and a `wp://` prefix on an
`fdd_0N_fn` path mounts that floppy write-protected — which the OS then
faithfully reports as "Write protected", and which means `SYSTEM.CFG`
settings do not survive a reboot.
