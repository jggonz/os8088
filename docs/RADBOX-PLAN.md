# RADBOX — "RAD BoomBox", and the OPL3 under it

Status: **wave 1 (the contract) written.** SPEC.md §34.11–§34.13 (OPL3, the
RAD verbs, the pacer) and §96 (RADBOX, the file formats, validation, fixtures)
are the contract now, and where they settled or changed something below,
section 7 says what and why. Where this plan and SPEC.md disagree, SPEC.md
wins.

## 1. What was asked, and what is already decided

> Can we add OPL3 support by extending our OPL2 support? I want to be able
> to play RAD files like Reality AdLib Tracker does.

Decisions the user took, 2026-09-16 (D1-D4 at intake, D5-D6 answering wave
1's two questions, D7 answering wave 3's) — not to be re-opened without them:

| # | question | decision |
|---|---|---|
| D1 | which files | **RAD v1 (file version 1.0) and RAD v2 (file version 2.1).** v1 plays on an OPL2 or an OPL3. v2 plays on an OPL3 and, on an OPL2-only card, **refuses with its reason** (§47) — no downmix |
| D2 | replay clock | **RTC periodic interrupt (IRQ8) on an AT-class machine; the system tick on an XT**, and the XT says it is tick-paced rather than pretending |
| D3 | where | **a new package, `RADBOX`, titled "RAD BoomBox".** Windowed: a small player view with minimal information on XT-class machines; colour in the window **on VGA only**. **F** goes full screen — **VGA Mode X 320x240**, **CGA 320x200 colour**, **Hercules 720x348 mono** — and **Esc** comes back. The mouse works in full screen at every resolution |
| D4 | tunes | **ship none.** RAD v2.0a's archive carries no licence for `player20.cpp` or its six tunes, so RADBOX opens `.RAD` files the user brings and no disk carries a song |
| D5 | beeps during playback (wave 1's Q1) | **the tone tier REFUSES while a tune holds the chip** - `OSAPI_SND_TONE` is silent during playback, and no kernel byte is spent on a speaker fallback. SPEC.md §34.12.3 as pinned |
| D6 | the XT's tick (wave 1's Q2; replaces its option (a), "publish at attach") | **`DSV_TICK` is DRIVER-SWITCHED at run time.** A kernel change of exactly one shape at three sites in `kernel/snd.inc` - after `drv_svc_call` in `osapi_snd_fm`, in `osapi_snd_stream`'s main path (not verb 3's), and in `snd_release_inst`'s `DSV_RELINST` call: `jc .keep / mov [drv_svc+DSV_TICK], dx / .keep:`. `SOUND.DRV` answers DX = its one combined tick proc while a tick-class tune plays or a Sound Blaster stream is open, else 0. Prototyped by the coordinator at **+18 bytes kern_big `.text`, no rung crossed** (image rung 23 -> 5 left), kern_small unaffected. Lands in wave 2, SPEC text first. SPEC.md §34.13.7 |
| D7 | the 8088's bound tick (wave 3's question: §34.13.6's 27 ms rule against the measured 161 ms) | **On `CPU_8086`, `RAD_PNMAX` stays 32 and the 27 ms bound-tick rule is RETIRED** (option 1). A known cost, recorded: a 2.1 tune on an 8088 + OPL3 spends 10-25% of the machine at IF = 0 in the tick (worst tick ~31-33 ms on real tunes, 161 ms on the bound tune `HEAVY`), which can drop serial-mouse bytes and make the pointer stutter while it plays; a hostile heavy file holds an 8088 ~160 ms a tick. AT-class machines keep the rule. The replayer stays the on-demand overlay `RADPLAY.DRV`. SPEC.md §34.13.6, §96.1 |
| D8 | the `drv_svc_call_x` hang (wave 3's question: a verb 6 poll against an unmounted `SOUND.DRV`) | **It is a PRE-EXISTING defect on `main` and is fixed in a PR of its own** - no kernel file changes in this work. Wave 3 keeps `RADGATE`'s `rt_nopoll` workaround in `tests/radmove.py`, with a comment naming the defect and where it is fixed; SPEC.md §96.8 records it, and **§96.2 puts the guard on the package**: a program polling a sound verb tests `SND_CAP_RAD` from `OSAPI_SND_CAPS` (which reads 0 once the driver is gone) before every verb 6 and stops polling when the sink disappears, so wave 4's RADBOX is correct on a kernel with or without the fix. SPEC.md §96.2, §96.8 |

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
acc >= 182: frame` - and the driver asks for that tick only while a tune plays
(D6, SPEC.md §34.13.7) — frames arrive in bursts of up to three per tick, and the
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
| `tools/radsim.py --selfcheck` | host, `make` | a Python reference replayer (htmsim's shape) turns a tune into a **per-frame register log**; its v2 half is checked against `player20.cpp` compiled on the host from the fetched archive when present, and SKIPS saying why when absent |
| `tests/unit/t_rad.py` | host, `make` (the radsim stamp; soak row `rad`) | the validator refuses every row of a hostile-file table (truncations, out-of-range indices, runaway riffs) — the package's refusal codes and the simulator's must agree |
| fixture tunes | committed, **ours** | written by `tools/radsim.py --make`: a v1 and a v2 tune that touch every effect, 4-op algorithm and pan. No Reality tune is ever committed (D4) |
| `tests/radopl3.py` | MartyPC + patch 05 | OPL3 detected; the v2 fixture's register writes, logged by a `-DRADLOG` driver build (§45.14's `trklog` shape), equal radsim's log byte for byte; the capture is not silent |
| `tests/radopl2.py` | MartyPC, `MARTYPC_OPL2=1` | the v2 fixture is refused on an OPL2 with the right sentence; the v1 fixture plays tick-paced |
| `tests/radrtc.py` | QEMU `ADLIB=1` | IRQ8 pacing: frames per second over 10 s is 50 ± 0; register B restored after close; the BIOS clock unharmed |
| `tests/radfsx.py` | QEMU on all three adapters | F and Esc round-trip, the pointer is drawn, a click lands, the frame counter never stalls across either transition |
| listening | `vm/386-radbox` (SB16, OPL3), `vm/xt-radbox` (SB Pro 2 on an XT if 86Box allows it) | a person hears it |

## 5. Waves

1. **Contract.** SPEC.md §34 amendments (OPL3, verbs 4–6, the IRQ8 pacer) and
   a new §96 for RADBOX; `apps/os88api.inc` and `apps/cc/os88.h`; INDEX
   regenerated. MartyPC patch 05. `tools/radsim.py` and the fixtures.
2. **Driver OPL3, and D6's switched tick.** Probe, the second array,
   `SND_CAP_OPL3`, OPL2-mode reset on all-off; every existing FM gate still
   green (`fmtest`, Frotz's `zs_fm`, Piano). **The kernel change** (SPEC.md
   §34.13.7): the three sites - site 3 (`snd_release_inst`) wrapped in its
   OWN `%ifdef OS88_SNDCARD`, because that routine assembles on kern_small -
   and `tools/kernsize.py` before and after on BOTH kernels (kern_big +18
   `.text`, no rung; kern_small +0), the knob kernels and kern_small in
   `buildmatrix`, `os88sym --all` on both kernels; `SOUND.DRV` answers DX on
   every successful FM verb, stream verb but 3 (verbs 8 and 9 included) and
   `DSV_RELINST`, publishes 0 at attach, and on each of them recomputes the
   value from live state, writes its own cell and loads DX inside one
   `pushf`/`cli` ... `popf` window - never reading the cell back;
   `snd_release_both` (and `sbl_release_inst`) end in an explicit `clc`,
   because today's release procs leave CF as they found it and site 3's `jc`
   would skip the write at random; the new stream **verb 9** (TICK, driver
   internal) and a call to it through the public slot once a pass, and once
   more on the `.die` path before `OSAPI_DRV_TASK AX=0`, in both
   `sbl_refill_task` and `sbl_drain_task`, which is the heal for a stale 0
   written into the kernel's copy by a task switch between the driver's
   answer and the kernel's write (or inside `drv_publish`);
   `sbl_tick` becomes the watchdog half of one tick proc;
   `snd_str_busy`'s header gains `clobbers: DX`; the gate `tests/sndtick.py`
   on `SB16=1` and `ADLIB=1` - the A/B, plus the PLANTED lost update: a
   `-DSBPOLL` sbtest owner that polls verb 3 only, 0 poked into both the
   kernel's and the driver's `DSV_TICK` cell, both non-zero again within 2
   ticks; `-DSNDREADBACK` (verb 9 reads its cell) and `-DSNDNOHEAL` (no verb
   9 in the loops) as its two negative controls, each still 0 after 36
   ticks; a `tools/stkwater.py` reading of the refill worker's slot with verb
   9 in the loop; and a host ratchet: a driver source that writes a
   non-zero `DSV_FM`, `DSV_STREAM` or `DSV_RELINST` cell and never names
   `DSV_TICK` fails `make`. **Merge order with `origin/codex/hda-1015pn`**:
   its `hda_stream` publishes `DSV_STREAM` and does not answer DX; whichever
   of this wave and that branch merges second makes every CF = 0 exit of
   `hda_stream` (verb 8 included) answer DX = 0, or an HDA machine far-calls
   garbage from IRQ0 on its first stream verb (SPEC.md §34.13.7).
3. **Replayer + pacer.** Validator, v2 engine, v1 loader, IRQ8 and tick paths,
   `-DRADLOG`; the MartyPC and QEMU rows.
4. **RADBOX windowed.** Open, transport, status, VGA meters, the XT minimal
   view, association; and D8's guard - `OSAPI_SND_CAPS` tested before every
   verb 6 poll, polling stopped when `SND_CAP_RAD` goes clear (SPEC.md §96.2).
5. **Full screen.** Mode X, CGA 320, Hercules; own pointer; `tests/radfsx.py`.
6. **Ship.** Disk placement (every geometry that has room, and `make live` —
   `t_livefull` will fail `make` until it is placed), `§24.5` omission,
   86Box targets, CLAUDE.md's machine list, PERFORMANCE.md figures from
   MartyPC for a frame's cost on a 4.77 MHz 8088.

## 6. Open, to settle while building

- ~~**Replayer resident size** (§3.2) — measured, then either kept or split.~~
  Split (wave 3, §7).
- **XT burst pacing.** Three frames in one tick is correct on average and
  audibly uneven for fast tunes; whether to spread them with a sub-tick
  `pit_now` check inside `DSV_TICK` is a measurement, not a guess.
- **Tune size ceiling.** The largest real v2 tune seen is 14,820 bytes; the
  claim ceiling is chosen from the format's worst case, not from that.
- **v1 layout** is pinned from Reality's v1 player source before wave 3.

## 7. Settled by the contract (wave 1)

What writing SPEC.md §34.11–§34.13 and §96 decided, changed or found. Each
row names the section that is now binding.

| point | settled as | why | § |
|---|---|---|---|
| verbs 4 and 6's pointer | **BX:SI / BX:DI**, not ES | `OSAPI_SND_FM` is an X cell and `osapi_snd_fm` replaces ES with `KERNEL_SEG` before the driver runs; BX is unused by verbs 4–6, so no kernel byte moves | 34.12 |
| "the tone tier moves to the speaker" | **the tone tier REFUSES while a tune holds the chip** (beeps are silent during playback) - **decision D5** | the kernel copies `DSV_TONE` only at attach and `DRVV_TIER`, so a driver cannot swap it; the speaker fallback would need a kernel change (`snd_tone_out` falling back to `spk_tone` on the sink's refusal), and the user chose silence over spending kernel bytes on it | 34.12.3 |
| when the chip is taken | at **start**, not at load; given back at **stop** | a loaded-but-stopped tune should not lock Piano out of FM | 34.12.2 |
| refusal codes | `RADE_*` 0..10, with 0 = `RADE_NOSINK`; `RADC_*` details 1..18 with a file offset in CX; **meaningful only when the driver sets `SND_CAP_RAD` = 40h** | one vocabulary from "no driver" to "bad byte at offset" - and round 0 found that `kern_small` and a pre-RAD `SOUND.DRV` answer CF=1 with AL = the verb, which reads as `RADE_NEEDOPL3`, so the capability bit is tested first | 34.12, 96.4.4, 96.6 |
| tune size ceiling | `RAD_MAXLEN` = **49,152** | chosen from addressing (tune + an 8KB working set + stack in one claim), because the format's own worst case is ~300KB and no segment holds it | 34.12.1 |
| where the replay step's stack lives | **1,024 bytes at the top of the tune's own claim**, SS swapped, never `sti` | a riff recursion 8 deep inside IRQ0/IRQ8 on a 192-byte task stack class is not survivable; and a machine that never opens a tune pays no resident stack | 34.12.5 |
| repeated register writes | a **shadow filter** on frame writes; start/stop sequences unfiltered | an IF=0 frame on a 4.77 MHz 8088 is the serial mouse's problem (8.3 ms a byte), and repeats are no-ops on the chip | 34.12.6, 34.13.6 |
| the default patches after a tune | reloaded **at once** on every end: through `opl_wr` at the caller's IF on stop/all-off, through the replay-path writer `opl_wrf` at IF=0 in `DSV_RELINST` | the lazy reload of the first draft landed on a tone-on, which runs inside `snd_tone_req`'s cli window - it moved the 28 ms rather than removing it (round 0) | 34.12.3 |
| the replay path's port writes | `opl_wrf`: index out, data out, no counted status reads | ~70 writes of a retriggering line are ~19 ms at IF=0 through `opl_wr`; Reality's players write back to back | 34.11.2 |
| a frame's work | at most `RAD_PNMAX` = **32** note plays, and the frame that asks for more **halts the tune** (2.1 deviation 5, `RSTF_HALTED`); at most `RAD_FRMAX` = 7 frames an interrupt; **no frame starts in a BIOS tick whose note plays reached `RAD_PNMAX`**, and such a tick earns the RTC no credit - at most 63 note plays and 2,560 computed writes a tick | riffs fan out: a valid 226-byte file makes one frame of the reference play ~13 million notes (round 0); a per-frame cap times 7 frames an interrupt, re-armed by the RTC credit, held a legal 1,989-byte file at IF = 0 every tick (round 1) | 96.4.5, 34.13.3, 34.13.5, 34.13.6 |
| RTC frames longer than a period | the **tick discipline**: the BIOS tick credits periods the RTC could not deliver | the RTC raises nothing until register C is read, so counting interrupts runs slow by every period a frame outlasts (round 0) | 34.13.3 |
| a start after a stop | **re-zeroes the replay state**; the stream is a function of the tune alone | `player20.cpp`'s `Stop()` does not, and a driver matching radsim byte for byte needs one answer | 34.12.2, 96.4.5 |
| a MIDI instrument | **7 bytes** counting the algorithm byte | `player20.cpp` and both MIDI tunes; `RAD.HTML` and `validate20.cpp` say 6 and are wrong | 96.4.3 |
| `NEW` and the second array | no 1xxh write but 105h while NEW = 0; asserted over every radsim stream; MartyPC patch 05 models the address decode and has `MARTYPC_OPL2=1` | a 104h written after 105h <- 00h lands on 04h on a real YMF262, and MartyPC had no OPL2 for the refusal gate (round 0) | 34.11.2, 96.8 |
| RTC vs tick | decided **at attach**: `CPU_8086` → tick; else register A = 26h **and B & 70h = 0** → RTC | the class is a property of the machine; what the class costs is not decided at attach (next row) | 34.13.2 |
| when `DSV_TICK` is paid | **only while something needs it** - the driver switches it through DX on its verbs' return (**decision D6**); a tick-class tune or an open Sound Blaster stream asks for it, nothing else does | the kernel copied the table only at attach and `DRVV_TIER`, so the contract as first pinned made an XT+AdLib pay ~16 bytes of every slice for a tune it never opened; the user took a +18-byte kernel change instead, which also stops every Sound Blaster machine paying for its watchdog while nothing streams. Found while pinning it: **verb 8** reaches the main path through the public slot, so the driver answers DX there too and `snd_str_busy`'s callers must not rely on DX (they do not); something ended at interrupt time (the SB watchdog, a HALT) keeps the tick until that instance's next site verb; and a `SOUND.DRV` from before the change under a kernel after it far-calls garbage from IRQ0 - named in the SPEC, not guarded, because a guard is kernel bytes. Found in review (fix round 0): the write is pre-emptible at sites 1 and 2, so a task switch can put a STALE 0 over a newer proc; it is healed, not prevented - the SB workers re-assert through a new internal stream verb 9 once a pass, and RADBOX's verb 6 poll runs from its UI task for as long as a tune is loaded - because prevention is kernel bytes beyond D6. The DX duty binds every sound-class driver, and `codex/hda-1015pn`'s HDA driver does not meet it yet (merge order, wave 2). Site 3 needs its own `%ifdef OS88_SNDCARD` or kern_small grows 6 bytes | 34.13.7, 8.7.5, 51.6, 96.2 |
| the RTC already in use | refused as `RADE_PACER` | the BIOS int 15h 83h/86h wait is chained and survives; an enabled AIE/UIE/PIE owner is somebody else's and there is no tick fallback on an RTC-class machine | 34.13.3 |
| `DSV_NAME` "AdLib (OPL3)" | **not done** | nothing paints `DSV_NAME` (§34.8); `SND_CAP_OPL3` is the fact | 34.11.1 |
| OPL3 NEW-bit ordering | a v2 start **prepends 105h ← 01h** | `player20.cpp` clears the second array before setting NEW, which a YMF262 ignores | 96.4.5 |
| the RAD 1.0 layout | pinned from RAD V1.1a's `RAD.DOC` and `PLAYER.ASM` (fetched from Reality's public file system into the scratchpad, never committed) | — | 96.4.2 |
| validation | stricter than `validate20.cpp` in four places, and corrects its BPM-flag bug (bit 6 vs bit 5); all six V2.0a tunes and both V1 tunes pass | — | 96.4.4 |
| "a drop onto the window" | **dropped** | the OS has no file-drop-onto-window mechanism; association (§54) and File ▸ Open cover it | 96.5 |
| EGA geometry | windowed: no meters (VGA-only colour, D3); full screen: `FSXM_CGA320` | EGA's fsx caps are the CGA modes (§53.4) | 96.2, 96.3 |
| a tune stopped by another program's F key | the driver unloads it (§53.3 releases every other instance's sound); RADBOX says "Stopped by another program." and replays from its own copy | — | 34.12.4, 96.6.1 |
| fixtures | `RV1.RAD`, `RV1SLOW.RAD`, `RV2.RAD`, `RV2BPM.RAD`, `RV2SLOW.RAD` in `tests/fixtures/rad/`; `.RLG` 3-byte register logs with FFFEh/FFFFh/FFFDh markers | — | 96.7, 96.8 |

**Settled in wave 2**: the OPL3 probe's status mask alone is not enough -
QEMU's `-device adlib` (MAME's old `fmopl.c`) never sets status bits 1-2 and
aliases 38Ah onto 388h, so it read OPL3. A maybe-OPL3 is now asked two more
questions through 38Ah: with `NEW` = 1 timer 1 must NOT start (an aliasing
OPL2 fails), and with `NEW` = 0 it MUST (a card with nothing at 38Ah/38Bh -
stock MartyPC's - fails; review round 0 found the first version read that card
as OPL3). SPEC.md 34.11.1 also records the rejected alternative: a read at
38Ah is FFh on 86Box's OPL3. The 105h <- 00h on all-off and `DSV_RELINST` is
the tune OWNER's (SPEC.md 34.11.3, 34.12.4) and lands in wave 3; the stream
workers' verb 9 is made at IF = 0 for the nest stack's sake (34.13.7).
`tests/opl3.py` (four arms, `MARTYPC_NO38A=1` the new one) and
`tests/sndtick.py` are the wave's gates.

**Settled in wave 3** (SPEC.md §34.12.1, §34.12.2, §34.12.5, §34.12.7, §34.12.8,
§34.13.6, §96.1, §96.6, §96.7, §96.8):

| point | settled as | why |
|---|---|---|
| the 2KB rule | **split**: `RADPLAY.DRV`, an overlay in HDDTOOL.DRV's shape (SPEC.md 52.11), read by verb 4 into the tune's own claim | the replayer's code and tables are 8,075 bytes - four times the rule; the resident half is 1,646 (wave 3's fixers moved int 70h's entry and BIOS chain into it, SPEC.md 34.13.3, and re-stamp the resident's segment at every entry into the claim because the image can move, 34.12.8). The price is a read off the system volume on the first load, `RADE_NOPLAYER` when that disk is not in its drive |
| the claim | overlay image, working set, then the tune at `RAD_TUNE` = 13,312, stack at the end; 62KB at `RAD_MAXLEN` | CS = DS = SS inside a frame, every state field an absolute address |
| verb 4's order | busy and the claim before validation, and validation on the COPY | the validator is in the overlay; the copy is what the interrupt-time engine reads. A corrupt file on a busy chip or a full heap now answers `RADE_BUSY` / `RADE_NOMEM` |
| a new refusal | `RADE_NOPLAYER` = 11, "The RAD player needs the system disk in its drive." | the split's one new failure |
| the tick path | 6 bytes more on the interrupted stack than a resident pacer | the far call into the overlay and its dispatcher |
| the status block | position and channel halves built at verb 6, not every frame | 13% of a real tune's frame instructions on the 8088 |
| the gates | `radopl3`, `radopl2`, `radrtc` green; RADGATE (`tests/radgate/`) is the client; the hostile-file table moved to `tests/unit/radrows.py` so t_rad and both MartyPC rows read one table | — |
| the 8088's bound tick | **decision D7**: `RAD_PNMAX` stays 32 on `CPU_8086` and §34.13.6's 27 ms rule is retired there; AT-class machines keep it, its measurement owed (SPEC.md §96.8) | measured on MartyPC's 8088 after the engine pass: `HEAVY` 161 ms a tick, Reality's own 2.1 tunes ~31 ms worst and 10-13 ms mean, `RV2.RAD`'s worst note frame 26.8 ms. 27 ms would need `RAD_PNMAX` ~5, which halts every Reality tune (busiest frame 14), and real music is over 27 ms regardless. The options judged and not taken: refuse 2.1 on `CPU_8086` (a cut against D1), `RAD_PNMAX` ~5 (halts real music everywhere), the replay step on a worker (§2 rejected it for timing) |
| verb 5 against an interrupt-time HALT | **every state test commits in the IF = 0 window it was made in** (SPEC.md §34.12.2's table): pause, stop and the unload read the state and disarm in one window; resume and start set "playing" and arm in one; start's disarm is a new private verb, `RADV_DISARM` (`RAD_ABI_VER` 2), called BEFORE the resident claims, and the claim re-reads `opl_own`; `rad_tickset` reads and writes in one window. +55 bytes resident, +52 overlay | wave 1's verification left it open and wave 3's first overlay had it in all four verbs: `tests/unit/t_radrace.py` fires the pacer at every instruction boundary and the first version failed pause (a HALTed tune marked paused), stop (1,068 writes after the HALT), resume and start (a HALTed tune left "playing" with the pacer disarmed) |
| the fixtures' reach | `RV2.RAD` gains pattern 2, `RV1.RAD` pattern 5, `RV2BPM.RAD` one Set Volume, reaching the seven 2.1 and two 1.0 replay outcomes only Reality's tunes reached, plus three more (and two unnamed branches) the same coverage run found unreached; `radsim --selfcheck` asserts each within 1,000 frames from the engines' own `hits`; the `player20.cpp` cross-check applies deviation 4 to its scratch copy (`--faithful-v2` leaves it out) | wave 1's verification, finding 3: the driver gates compare against the fixtures alone, so a driver bug in any of those branches passed. An off-by-one octave-wrap constant now fails `RV2.RAD` in radsim's cross-check and in the driver's register log alike |
| the verb 6 poll against an unmounted driver | **decision D8**: the `drv_svc_call_x` near-`ret`-from-a-far-call hang is a defect on `main` and is fixed in a PR of its own - no kernel byte here. `tests/radmove.py` keeps the `rt_nopoll` hold-off across its Control Panel unmount (RADGATE's comment names SPEC.md §96.8), and §96.2 makes the package guard itself: `OSAPI_SND_CAPS` before every verb 6, polling stopped when `SND_CAP_RAD` goes clear | found by `radmove`'s arena, which unmounts `SOUND.DRV` while RADGATE polls. `osapi_snd_fm` does not pre-test its cell (`osapi_snd_stream` does), so an FM verb with no sink runs into `.lowbss` and hangs the UI task with the graphics lock held. The guard costs a poll nothing and is correct on either kernel; the kernel fix is 4 bytes of `.cold` and belongs to whoever owns `main` |
| verb 4's requester byte | written at **step 7, under the verb lock**, with the other working copies | steps 8 and 9 yield for hundreds of ms, so a second instance's verb 4 - the one about to be refused `RADE_BUSY` - reached that byte first and the commit stamped its id on the lock holder's tune (§34.12.1) |
| the validator's and the engines' breadth | `tests/unit/t_radfuzz.py`, a committed **soak** row: 2,500 seeded mutants of the fixtures through the shipped `RADV_LOAD`, and the ones both accept PLAYED and compared with radsim record for record | the hostile-file table is 116 hand-built rows and the fixtures are five; an accepted mutant is a valid file nobody wrote, which is the only way to reach the engines at breadth |
| `radrtc`'s BIOS-wait row | **one-sided**, against the shorter of two controls taken back to back | both sides are host-paced samples and a stall can only lengthen one; a two-sided +-5% band made it go red on unchanged bytes |

**Still open**: XT burst spreading (§6, unchanged).

