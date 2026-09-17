# RADBOX — "RAD BoomBox", and the OPL3 under it

Status: **PLAN, nothing built.** SPEC.md sections are written from this
before any code (CLAUDE.md: update the contract first). Section numbers
below are placeholders — the next free top-level is §96.

## 1. What was asked, and what is already decided

> Can we add OPL3 support by extending our OPL2 support? I want to be able
> to play RAD files like Reality AdLib Tracker does.

Decisions the user took, 2026-09-16 — not to be re-opened without them:

| # | question | decision |
|---|---|---|
| D1 | which files | **RAD v1 (file version 1.0) and RAD v2 (file version 2.1).** v1 plays on an OPL2 or an OPL3. v2 plays on an OPL3 and, on an OPL2-only card, **refuses with its reason** (§47) — no downmix |
| D2 | replay clock | **RTC periodic interrupt (IRQ8) on an AT-class machine; the system tick on an XT**, and the XT says it is tick-paced rather than pretending |
| D3 | where | **a new package, `RADBOX`, titled "RAD BoomBox".** Windowed: a small player view with minimal information on XT-class machines; colour in the window **on VGA only**. **F** goes full screen — **VGA Mode X 320x240**, **CGA 320x200 colour**, **Hercules 720x348 mono** — and **Esc** comes back. The mouse works in full screen at every resolution |
| D4 | tunes | **ship none.** RAD v2.0a's archive carries no licence for `player20.cpp` or its six tunes, so RADBOX opens `.RAD` files the user brings and no disk carries a song |

## 2. Facts that shape it

**The existing FM surface cannot host a tracker.** `OSAPI_SND_FM`
(SPEC.md §34.2, `drivers/sound/sound.asm` `opl_fm_op`) is note-level: note-on
in Hz, note-off, an 11-byte 2-op patch, all-off, channels 0–7, **channel 8
reserved for the tone tier** (§34.8). A RAD replayer computes its own F-Number
and block, drives all 9 channels, rewrites operator registers mid-note
(volume, multiplier, feedback effects) and — for v2 — addresses the second
register array. It needs register-level access to the whole chip.

**RAD v2 (`player20.cpp`, RAD 2.0a, file version 0x21 at offset 0x10):**

- 9 tracker channels, 100 patterns x 64 lines, 10 riff tracks per channel
  range, 127 instruments.
- An instrument has an **algorithm 0–6**: 0–1 are 2-op, 2–6 are 4-op. On an
  OPL3, tracker channels 0–2 pair OPL3 channels 0/3, 1/4, 2/5; channels 3–5 pair
  100h/103h … 102h/105h in the second array; channels 6–8 are 2-op only
  (`ChanOffsets3`/`Chn2Offsets3`). So v2 needs **104h (4-op connection
  select)** and **105h bit 0 (OPL3 NEW)**, stereo panning in C0h bits 4–5, and
  registers 000h–1FFh. The player keeps a **512-byte register shadow**.
- Rate: 50 Hz by default; a BPM field gives `BPM * 2 / 5` Hz; a "slow-timer"
  flag gives **18 Hz** — which is the system tick, so those tunes need no
  pacer at all.
- Effects: portamento up/down, tone slide, tone+volume slide, volume slide,
  set volume, jump to line, set speed, and the v2 letters I/M/R/T/U/V
  (ignore, multiplier, riff, transpose, feedback, volume).
- The player "does no checking" — `validate20.cpp` is the separate check.
  **Every byte off a floppy is hostile** here, so validation is not optional
  and runs before the first register write.

**RAD v1 (file version 0x10)** is a different, simpler format (9 x 2-op
channels, 11-byte instruments, orders, 32 patterns with packed lines, the
same core effect set). `player20.cpp` refuses it, so v1 is **a second, small
replayer of our own** sharing the note/effect engine. Its layout is to be
pinned from Reality's v1 player source in their public file system, read for
reference and not shipped.

**OPL3 detection** is one read on top of the probe we already run: after the
timer-flag dance, `status & 06h` is `06h` on an OPL2 and `00h` on an OPL3.
The second array is at **38Ah/38Bh**.

**Timing, and why the replayer runs at interrupt time.** §34.1 forbids
re-rating PIT channel 0 (outside §53.2.1's `FSXF_FASTTICK` bracket). IRQ8 is
free — nothing in `kernel/` or `drivers/` programs the RTC's periodic
interrupt, and `kernel/clock.inc` reads the CMOS through `clk_at_lock` at
IF=0, so an IRQ8 handler cannot land between its index and data. But **a
worker task woken from IRQ8 does not buy accurate time**: only IRQ0 enters
`sch_switch` (§8.1.2), so a ready worker still waits for the running slice to
end — up to 55 ms whenever the UI task is drawing, which is exactly when a
full-screen visualiser is busiest. The replay step therefore runs **in the
interrupt**: IRQ8 on an AT, `DSV_TICK` inside IRQ0 on an XT. A frame is a
few dozen register writes and some table lookups, and each write is two
`out`s.

**Full screen already exists.** §53 gives `FSXM_MODEX` (320x240x256, 3
pages, `OSAPI_FSX_PAGE`), `FSXM_CGA320` and `FSXM_HERC`. Inside the bracket
the mouse ISR keeps `mouse_x/y/btn` live and `OSAPI_MOUSE` answers (§53.1),
but nothing draws a pointer — **RADBOX draws its own** in each mode. A
driver's IRQ keeps running across the bracket (§53.2 freezes tasks, not
interrupts), so the music does not stop on F or Esc.

**Emulators.**

| | OPL2 | OPL3 | RTC IRQ8 | automatable |
|---|---|---|---|---|
| QEMU | `-device adlib` | **no** | yes | yes |
| MartyPC | yes (Nuked-OPL3 core) | core yes, **ports 38Ah/38Bh not mapped** | **no** (an 8088) | yes |
| 86Box | yes | SB Pro 2 / SB16 | AT machines | no — listening only |

So: **patch 05 for MartyPC** maps the secondary register file, which makes
OPL3 detection, v2 playback and the XT tick path scriptable with sound
capture on an 8088. QEMU covers IRQ8 with a v1 tune on its OPL2. The AT +
OPL3 + IRQ8 combination is only on 86Box, where a person listens.

## 3. Design

### 3.1 `SOUND.DRV` — OPL3

- `opl_probe` reads the OPL3 bit and records `[opl_is3]`. On an OPL3, init
  also clears 104h and 105h and leaves the chip in **OPL2-compatible mode**, so
  every existing `OSAPI_SND_FM` user and the tone tier behave exactly as today.
- New capability bit **`SND_CAP_OPL3` (20h)** in `OSAPI_SND_CAPS`; the Control
  Panel's card row can read "AdLib (OPL3)" / "Sound Blaster (OPL3)".
- `opl_wr` grows an array select: register numbers 100h–1FFh go to 38Ah/38Bh.
  Everything that exists passes 000h–0FFh and is unchanged.

### 3.2 `SOUND.DRV` — the RAD replayer (`drivers/sound/rad.inc`)

New `OSAPI_SND_FM` verbs, all refusing with CF=1 when there is no FM sink:

| verb | name | contract |
|---|---|---|
| 4 | `SNDFM_RADLOAD` | ES:SI → a tune in YOUR segment, CX = length. The driver **validates** it (every offset bounded by CX, every pattern/riff/instrument index in range, the format's own terminators found), copies it into a pinned heap claim of its own, and answers AL = file version (10h/21h), AH = rate in Hz. Refusals are distinct codes: not RAD, bad version, corrupt at offset, **v2 needs OPL3**, too big, **chip busy** (another instance holds a channel), no memory |
| 5 | `SNDFM_RADPLAY` | start/pause/resume/stop (AH). Start takes the **whole chip**: the claim map is filled for the caller, the tone tier moves to the speaker for the duration (`DSV_TONE` swapped, §34.8), and on an OPL3 with a v2 tune 105h/104h are set |
| 6 | `SNDFM_RADSTAT` | ES:DI → a status block in YOUR segment: order, pattern, line, speed, rate, **pacer (1 = RTC, 0 = tick)**, and per channel the note, instrument, volume and a key-on counter — everything the visualiser draws, one far call a frame |
| 3 | all-off (existing) | also stops a tune and gives the chip back: key-off on all 18 channels, 104h/105h cleared, OPL2 mode restored, tone tier back on the OPL, claim freed |

`DSV_RELINST` already tears a dying instance's claims down; it frees the tune
the same way, so a crashed or closed RADBOX cannot leave the chip in OPL3 mode.

**Pacer.** On an AT (`OSAPI_CPU_INFO` above `CPU_8086` **and** the RTC
answers `clk_at` probing), the driver hooks int 70h, sets register B bit 6
(PIE), keeps register A's rate at the BIOS's 1,024 Hz, and runs a Bresenham
accumulator (`acc += hz; while acc >= 1024: frame`) so 50 Hz is exact rather
than 1024/20 = 51.2. The handler reads register C (re-arms), chains to the
old vector when a PF was not ours, and sends EOI to both PICs. Unhook restores
register B and the vector. On an XT, `DSV_TICK` runs `acc += hz * 10; while
acc >= 182: frame` — frames arrive in bursts of up to three per tick, and the
status block says so.

**Size.** The replayer has to cost nothing on machines that never open a
tune. Measure it once written: if it is more than ~2KB resident, it becomes an
**on-demand part of the driver** read in by verb 4 and freed by all-off,
the §2.8 shape applied one level down.

### 3.3 `apps/radbox/` — the package

- `.RAD` association; File > Open; a drop onto the window.
- **Windowed, every adapter:** title/author line from the tune's description
  text, order/pattern/line, elapsed time, play/pause/stop, rate and pacer.
  **Minimal on XT class** (`CPU_8086`): no per-channel meters, repaint only
  the fields that changed (§11.90), text through `font_run`.
- **Windowed, VGA:** the same plus nine coloured channel meters. 1bpp adapters
  get no greyed meters — they get none.
- **F → full screen**, `OSAPI_FSX_RUN` with the mode the adapter answers:

| adapter | mode | view |
|---|---|---|
| VGA | `FSXM_MODEX` 320x240x256, page-flipped with `OSAPI_FSX_PAGE` | nine-channel spectrum/VU, scrolling pattern lines, order strip, tune text |
| CGA | `FSXM_CGA320` 320x200x4 | the same layout in the 4-colour palette, dirty rects only |
| Hercules | `FSXM_HERC` 720x348 mono | the same, 1bpp |

  Own pointer drawn in each mode from `OSAPI_MOUSE`; transport buttons are
  clickable; **Esc** returns; the tune keeps playing across both transitions.
- Refusals in the window's own words: no sound driver; no OPL; *"This tune
  needs an OPL3 — this card is an OPL2"*; the validator's reason with its
  offset; chip in use by another program.
- **Not on `kern_small`**: it cannot reach `SOUND.DRV` there, so it joins
  §24.5's derived omission list.

## 4. Testing

| gate | where | asserts |
|---|---|---|
| `tools/radsim.py --selfcheck` | host, fast tier | a Python reference replayer (htmsim's shape) turns a tune into a **per-frame register log**; its v2 half is checked against `player20.cpp` compiled on the host from the fetched archive when present, and SKIPS saying why when absent |
| `tests/unit/t_rad.py` | host, fast tier | the validator refuses every row of a hostile-file table (truncations, out-of-range indices, runaway riffs) — the package's refusal codes and the simulator's must agree |
| fixture tunes | committed, **ours** | written by `tools/radsim.py --make`: a v1 and a v2 tune that touch every effect, 4-op algorithm and pan. No Reality tune is ever committed (D4) |
| `tests/radopl3.py` | MartyPC + patch 05 | OPL3 detected; the v2 fixture's register writes, logged by a `-DRADLOG` driver build (§45.14's `trklog` shape), equal radsim's log byte for byte; the capture is not silent |
| `tests/radopl2.py` | MartyPC | the v2 fixture is refused on an OPL2 with the right sentence; the v1 fixture plays tick-paced |
| `tests/radrtc.py` | QEMU `ADLIB=1` | IRQ8 pacing: frames per second over 10 s is 50 ± 0; register B restored after close; the BIOS clock unharmed |
| `tests/radfsx.py` | QEMU on all three adapters | F and Esc round-trip, the pointer is drawn, a click lands, the frame counter never stalls across either transition |
| listening | `vm/386-radbox` (SB16, OPL3), `vm/xt-radbox` (SB Pro 2 on an XT if 86Box allows it) | a person hears it |

## 5. Waves

1. **Contract.** SPEC.md §34 amendments (OPL3, verbs 4–6, the IRQ8 pacer) and
   a new §96 for RADBOX; `apps/os88api.inc` and `apps/cc/os88.h`; INDEX
   regenerated. MartyPC patch 05. `tools/radsim.py` and the fixtures.
2. **Driver OPL3.** Probe, the second array, `SND_CAP_OPL3`, OPL2-mode reset
   on all-off; every existing FM gate still green (`fmtest`, Frotz's `zs_fm`,
   Piano).
3. **Replayer + pacer.** Validator, v2 engine, v1 loader, IRQ8 and tick paths,
   `-DRADLOG`; the MartyPC and QEMU rows.
4. **RADBOX windowed.** Open, transport, status, VGA meters, the XT minimal
   view, association.
5. **Full screen.** Mode X, CGA 320, Hercules; own pointer; `tests/radfsx.py`.
6. **Ship.** Disk placement (every geometry that has room, and `make live` —
   `t_livefull` will fail `make` until it is placed), `§24.5` omission,
   86Box targets, CLAUDE.md's machine list, PERFORMANCE.md figures from
   MartyPC for a frame's cost on a 4.77 MHz 8088.

## 6. Open, to settle while building

- **Replayer resident size** (§3.2) — measured, then either kept or split.
- **XT burst pacing.** Three frames in one tick is correct on average and
  audibly uneven for fast tunes; whether to spread them with a sub-tick
  `pit_now` check inside `DSV_TICK` is a measurement, not a guess.
- **Tune size ceiling.** The largest real v2 tune seen is 14,820 bytes; the
  claim ceiling is chosen from the format's worst case, not from that.
- **v1 layout** is pinned from Reality's v1 player source before wave 3.
