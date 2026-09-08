# Audio Player plan

> **Design record for SPEC.md §86.** SPEC.md is the binding description of what
> shipped; this is how it got there — the audit that preceded the code, the
> options weighed, the 86Box benchmark procedure, and what is deliberately left
> for later. The full pre-implementation audit lives in the session scratchpad;
> the parts that survive into the interface are in SPEC.md §86, which wins on
> conflict.

## Goal

`AUDIO.O88` — lightweight **background** music playback on an IBM XT (8088 @
4.77 MHz, 640 KB, Sound Blaster 2.0), usable while Sheet / Note Pad / the
desktop stay responsive. Streamed WAV, never the whole file in RAM. Formats:
`WAVE_FORMAT_PCM` 0x0001 (unsigned 8-bit mono) and `WAVE_FORMAT_DVI_ADPCM`
0x0011 (IMA/DVI 4-bit mono). Rates accepted: 8000, 11025, 16000, 22050 Hz —
not hard-coded to one.

## What the audit established

1. **Reuse §34.5's ring stream; do not build a DMA/SB driver.** `SOUND.DRV`
   owns the 8237, page register, IRQ and DSP. The app is a client of
   `OSAPI_SND_STREAM` verbs 0/1/2/3/6/7 and the kernel's transient refill
   task. Nothing in the app touches hardware.

2. **A MOD mixer is not viable on the XT; a PCM/ADPCM streamer is.** Tracker's
   4-channel mixer is 44–165 % of a 4.77 MHz 8088 (`PERFORMANCE.md` Sets
   20/68) — "a QEMU/286-era luxury" in its own plan. A streamer does no
   mixing: PCM8 is a copy, IMA ADPCM is an estimated ~10–15 % at 11 kHz.

3. **Streaming from disk is the central problem.** `int 13h` (and an HDD
   read) holds `sch_lock`, which pauses both the kernel refill task and the
   package worker — §34.5's pinned rule is "a stream fed live from disk *will*
   underrun". The mitigation is a **two-stage buffer**: disk → a large
   look-ahead ring in a heap claim (filled in cluster bursts by the UI task)
   → decoder → the 16 KB SND ring. A burst's `sch_lock` hold is short
   relative to the double-buffer + ring cushion, and the decode/feed is
   decoupled from the disk cadence.

4. **The disk reads must be on the UI task** (file slots are UI-context-only,
   §20.6 rule 7), so the worker drives them through `OSAPI_WM_WAKE` →
   `OSAPI_WM_ONWAKE` — the FTPD / RunCPM / Frotz handshake (§77.1). Here it is
   non-blocking: the worker requests and carries on; a transient look-ahead
   underrun just feeds less that wake.

5. **C is not an option for the audio half.** `apps/cc/os88.h` deliberately
   does not wrap `OSAPI_SND_STREAM` / `_FM` / `_PLAY` (§73.7) and directs a
   sound app to assembly; all three existing audio apps (Tracker, ModPlug,
   Recorder) are pure NASM. The audit's preferred "C shell + asm shim" route
   also could not be built in the implementation environment (no host C
   compiler to bootstrap SmallerC). The engine, the decoder and the shim are
   therefore one assembly, the repo norm for this app class.

6. **IMA/DVI ADPCM decodes straight to PCM8.** The predictor is signed 16-bit,
   clamped each step; the sample handed on is `(predictor >> 8) + 128`
   (arithmetic shift), which is exactly [0, 255] with no clamp and **no
   intermediate PCM16 buffer**. Implemented from the public IMA spec — the
   89-entry step table and 16-entry index table only; nothing vendored.

## Codec comparison (audit summary)

| codec | ~KB/s @ 11 kHz | decode cost (8088) | WAV tag | FFmpeg | verdict |
|---|---|---|---|---|---|
| PCM u8 mono | 11.0 | ~0 (a copy) | 0x0001 | `-c:a pcm_u8` | **baseline, shipped** |
| IMA/DVI ADPCM 4-bit | ~5.6 | ~10–15 % | 0x0011 | `-c:a adpcm_ima_wav` | **shipped** — best interoperability, cheapest real option, ~2× smaller than PCM8 |
| blueduckjf / alexriegler ADPCM | ~5.6 | ~same | none (raw) | no | studied; no interop gain over IMA, licence unverified — not adopted |
| alexriegler 2-bit | ~2.75 | ~8–12 % | none | no | noted for a future very-low-bandwidth mode (network radio); not music |
| superctr / AICA / PSX ADPCM | ~4–5.6 | very cheap | none | partial | chip/console formats, no WAV container — rejected |

## Benchmark procedure (86Box — owed)

QEMU is functional verification only. Performance figures come from **86Box**:
base config `vm/xt-sound` (IBM XT, 8088 @ 4.77 MHz, 640 KB, SB 2.0 @ 0x220 /
IRQ 5 / DMA 1 — DSP 2.01, the classic `0x48`+`0x1C` auto-init path). Add a
config with a hard disk, or boot the live-USB `C:` image — the stock
`xt-sound` is two floppies, which is the worst-case disk path; keep it as the
"floppy truth" test and add HDD as the realistic one.

Matrix (from the brief): **A** player only, **B** + Task Manager, **C** + Note
Pad, **D** + Sheet, **E** + Sheet while typing/scrolling. Each at {8000,
11025, 16000, 22050} Hz × {PCM8, IMA-ADPCM}, same source track throughout.

Instrument: a rebuild-cheap cycle counter in `apd_pull`'s inner loop
(× `PERFORMANCE.md`'s table); the worker's feed **lead** min/median/max +
underrun count, the `PERFORMANCE.md` Set 21 table shape; the SB driver's
`snd_sb_under` via the §57 debug registry; `OSAPI_MEM_AVAIL` before/after.

Pass bar: zero underruns over a full track in scenario **E**, feed lead-min
≥ 2 halves, no user-perceptible UI lag in Sheet. Expected sweet spot from the
audit: **11.025 kHz ADPCM** on floppy, everything up to 22.05 kHz from HDD.

## Test WAVs

```
ffmpeg -i SRC -ar RATE -ac 1 -c:a pcm_u8        pcmNNk.wav      # PCM8
ffmpeg -i SRC -ar RATE -ac 1 -c:a adpcm_ima_wav adpNNk.wav      # IMA/DVI ADPCM
```

8.3 stems (`pcm11k`, `adp11k`, …) so `tools/os88disk.py` accepts them.
`make audiodisk AUDIOWAV="build/wav/pcm11k.wav build/wav/adp11k.wav"` packs a
stand-alone floppy for
`make test-snd SB16=1 TESTAPPS=build/audio-test.img` (add `SNDSNIFF=sb` so
QEMU's SB16 is detected at boot; on 86Box / real hardware the boot probe finds
it without the knob).

## Functional verification done (QEMU + SB16, `SNDSNIFF=sb`)

- Fresh launch and empty-state render; `.WAV` file-association launch +
  autoplay; File ▸ Open → add + play; multi-file playlist with a current
  marker.
- PCM8 @ 22 kHz: **28 s streamed gap-free** — 0 quiet windows in the SB
  capture, longest continuous quiet 0.00 s — with **14 s of it while the Disk
  window held the focus** (background playback).
- IMA/DVI ADPCM @ 11 kHz and 22 kHz: decoded to coherent tonal audio (verified
  against the source in the capture).
- RIFF parser against real FFmpeg files (PCM with a `LIST`/`INFO` chunk before
  `data`; ADPCM with `fmt `(20) + `fact` + `LIST` before `data`).
- Duration (8 s and 28 s both exact); progress bar + elapsed clock (worker
  dynamic redraw); natural end → reap → "End of playlist".
- Play / Pause / resume / Stop / Next / Previous (with wrap) / Shuffle /
  Repeat toggles; About → credit toast.

Three defects were found on the glass and fixed: a blank first paint before
`wm_fit` settled (self-`OSAPI_WM_WAKE` + settled repaint in `ap_onwake`); a
3px dead lane between transport buttons (`apu_hit` now tiles the row
gap-free); `apu_layout` / `apu_origin` now keep last-good geometry on CF=1.

## The performance round — why PCM8/8k was heavier than ModPlug

Real 86Box testing on an XT reported PCM u8 @ 8000 Hz — the *cheapest* format,
no decoder at all — sitting near ~70 % CPU per the Task Manager, and heavier
than ModPlug playback on the same machine. The brief called that suspicious and
asked for a measured root cause before any optimisation.

**Instrumentation.** `make … AUDIOPROF=1` builds `AUDIOP.O88` with a block of
`inc word` counters (all inside `%ifdef APROF`, so the shipping binary carries
none). The **D** key shows them in the window and writes `APDIAG.TXT` to the
system-volume root. A 20-second PCM8/8k run under QEMU, before the fix:

| counter | /20 s | note |
|---|---|---|
| worker iterations | 455 | ~23/s — adaptive sleep working, not a busy loop |
| `verb 3` status polls | 455 | one per iteration; ~1 ms/s on the target — negligible |
| `verb 6`/`verb 1` stage/feed | 103 / 97 | ~5/s — one 2 KB half, matches 8 kB/s + margin |
| **`FILE_READ_AT` calls** | **106** | **~5/s — the problem** |
| refill events | 105 | one disk read per `FILE_READ_AT` |
| `WM_WAKE` / `WM_ONWAKE` | 90 / 182 | the worker→UI refill handshake, ~5–9/s |
| `ap_open_track` / `apw_parse` / `apw_slide` | 1 / 1 / 0 | **no parser re-entry** — an earlier screenshot misread of "106" as "6176" had suggested a parse storm; there is none |
| underruns / watchdog | 0 / 0 | |
| progress updates | 42 | ~2/s — the redraw throttle holds |

**Root cause.** `ap_refill_chunk` read **one cluster (2 KB) at a time**, ~5×/s.
On the target an `int 13h` costs ~1–2 disk revolutions *whatever it moves*
(`PERFORMANCE.md`: cost disk work in calls, not sectors), and the read holds
`sch_lock`, freezing the scheduler — the kernel refill task and this package's
own worker included (§34.5). Five system-wide stalls a second, plus the
worker→`WM_WAKE`→`ap_onwake` handshake driving them (182 `WM_ONWAKE` in 20 s).

That is exactly what ModPlug does *not* pay: its module is resident, so it
issues **zero** `int 13h` during playback. It is invisible on an emulator,
which is exact about work and useless about time — every QEMU run streamed
fine.

**Fix.** `AP_RD_CHUNK` = 16 KB: `ap_refill_chunk` now reads a gulp bounded by
the ring's free room and floored to a whole number of clusters, and
`ap_scratch` is sized to hold it (bss +8 KB; the shipped build is a 9,216-byte
image, padded to 512 for the aligned read bounce, + 20,958 bytes of bss, ~30 KB
— under half of `APP_MAX_SIZE`). Same 20-second run after:

| counter | before | after |
|---|---|---|
| `FILE_READ_AT` | 106 | **14** (~0.6/s) |
| refill events | 105 | 14 |
| `WM_WAKE` / `WM_ONWAKE` | 90 / 182 | 14 / 14 |
| underruns | 0 | 0 |

7–8× fewer disk reads, the same drop in `sch_lock` stalls and in the WM
round-trip. IMA/DVI ADPCM @ 8 kHz — the "severe stalls" case — was the *same*
bug (8 reads/20 s after the fix, 0 underruns); it is not decoder cost. Per the
brief, the IMA nibble loop was left alone: measurement did not justify touching
it.

Also in this round: adaptive worker sleep (`AP_SLP_FEED` 1 tick while feeding,
`AP_SLP_IDLE` 6 otherwise); the end-of-track UI kick throttled to
`AP_REAP_TICKS`; `ap_feed` stages nothing while the SND ring still holds
> `ring − 2 halves`; and the dynamic redraw is change-gated (no lock, no draw
when neither the elapsed second nor the bar pixel moved) and capped at
`AP_DRAW_TICKS` (~2/s) and skipped entirely when `OSAPI_WM_OBSCURED`.

## The file-workflow round

- **One add path.** File ▸ Open, the launch document, the startup
  `OSAPI_ARG_FILE` and the single-instance handoff all converge on
  `ap_add_file(name, dir, vol, flags)` (§86.2.1). It validates the extension,
  de-duplicates on name + dir + vol, and starts the track per the flags.
- **Open does not interrupt.** `ap_onfile` calls `ap_add_file` with
  `AP_ADD_CUR | AP_ADD_IDLE`: add and select always, begin playback only when
  the player is idle.
- **Single instance (§86.11.1) — built, then DISABLED by default.** A `.WAV`
  double-clicked while a player runs was meant to reach the running instance
  through an `APQUEUE.DAT` rendezvous file on the system-volume root: the
  second instance's `ap_entry` finds the sibling (`OSAPI_SYS_SNAPSHOT`), writes
  the document, and returns CF = 1 (no window); the running instance's
  `ap_onwake` polls, replays through `ap_add_file`, deletes. It works under
  QEMU. **On an 86Box XT it does not**: the poll must `OSAPI_FILE_GOTO` the
  system root to read the file — a full remount (BPB, FAT window, root scan,
  sort, icon harvest), *seconds* on a 4.77 MHz machine — and a playing ADPCM
  track polls often enough that the machine spends much of its time
  remounting. APDIAG.TXT off the disk: `quechk 10`, `opentk 3` (a stale
  two-record queue had the poll re-add and restart both tracks), heard as
  "plays a second, stalls". The whole path is now behind `-DAP_HANDOFF`, off;
  a second double-click opens a second window, the OS default. What the
  attempt established is kept in §86.11.1:
  - `assoc_run` has no already-running check and there is no public "wake that
    window" slot (a snapshot record carries no window pointer).
  - `GOTO_Q` does **not** redirect `OSAPI_FILE_WRITE`/`_READ` by name — they
    resolve in the instance's own folder — so a cross-folder rendezvous needs
    the real `OSAPI_FILE_GOTO`, which is the expensive part.
  - The queue file must be a whole-file read-modify-write (`OSAPI_FILE_WRITE`),
    not `OSAPI_FILE_APPEND` (which needs a pre-existing cluster-aligned file).
- **Drag-and-drop is not possible** with the current window manager — see
  Limitations.

## Limitations / future work

- **86Box performance figures are owed** — the numbers above are QEMU
  functional checks; QEMU is exact about work done and useless about how long
  it takes (`PERFORMANCE.md`).
- No seeking (an ADPCM seek must respect block boundaries and decoder state).
- No `.M3U` playlists yet, no clickable playlist rows.
- **High rates (24/32/44.1 kHz) need a Sound Blaster Pro or an SB16.**
  `apw_parse` accepts all seven rates and lets the driver decide. `SOUND.DRV`
  originally gated anything above 22,222 Hz at `sbl_verhi >= 4` (an SB16), and
  an 8-bit ISA XT has no SB16 — so `drivers/sound/sb.inc` gained an **SB Pro
  high-speed path**: DSP ≥ 3.00, DSP command `90h` after a `40h` time
  constant, the wide regime's 4 KB double buffer, and — because `90h` locks
  the DSP out of commands — `sbl_halt` masks 8237 channel 1 rather than
  writing `D0h`, `sbl_go_on` unmasks, and `sbl_stop_stream` runs a DSP reset
  to leave high-speed mode. Everything ≤ 22,222 Hz and every DSP < 3.00 is
  byte-for-byte unchanged. Below DSP 3.00 the player reports *"Rate needs a
  Sound Blaster Pro"* and moves on. Tested: 24/32/44.1 kHz PCM u8 mono plays
  on QEMU's emulated card (the SB16 wide path) with pause/resume intact; the
  SB Pro high-speed path itself needs an 86Box SB Pro v1/v2 to validate on the
  glass (QEMU only emulates DSP 4.05).
- **No drag-and-drop from the File Manager.** The window manager has no
  cross-app file-drop event: `fm_drag` (`files.inc`) recognises only a Disk
  window's folder targets and acts by a file *move* (`fcp_paste`), and
  `OSAPI_WM_ONDRAG` is the scrollbar/content tracking edge (§13.8.2), not a
  receiver. This needs a new `W_ONDROP` event carrying a file payload — an OS
  feature, out of scope for the package. Until then a dragged `.WAV` reaches
  the player by File ▸ Open, by double-click (handed over to the running
  instance), or as the launch document.
- Transport buttons fire on `W_ONCLICK` (press), not the arm-on-press /
  fire-on-release model (§13.7) — acceptable for single-shot commands, could
  be tightened.
- The WAV chunk walk slides at most `APW_WINDOWS` cluster windows past a large
  preamble; a header bigger than that is refused, not mis-read. Real encoders
  put `fmt `/`data` in the first ~100 bytes, so the slide is a safety net.
- Written as one assembly rather than the audit's preferred C shell + asm
  shim, because the C cross-compiler could not be bootstrapped in the
  implementation environment and pure asm is the repo norm for audio apps.
  A future C rewrite would keep this engine as the shim verbatim.

## Tooling fixes made along the way

Two pre-existing Windows-host portability bugs in the build tooling, each a
one-line fix, unrelated to the Audio Player but blocking `make` on Windows:

- `tools/os88ovlchk.py`: `glob.glob('kernel/*.inc')` yields backslash paths on
  Windows, so the `EXTRA` map that files `clockw.inc` / `splash.inc` into
  their real sections never matched — 42 false "near call crosses a segment
  boundary" positives. Normalise to `/`.
- `tools/os88index.py`: the writer used text-mode `open`, so on Windows it
  rewrote `docs/INDEX.md` CRLF and every line showed as changed. `newline="\n"`.
- `tools/os88index.py` package discovery: it scans each `$(BUILD)/x.bin:` rule
  for an `apps/x/x.asm` on the dependency line. The Audio Player's rule was
  refactored to `$(BUILD)/audio.bin: $(AUDIO_SRC)`, which hid the path and
  dropped AUDIO PLAYER from the generated INDEX. Fixed by naming
  `apps/audio/audio.asm` explicitly on the rule line (alongside the variable).
