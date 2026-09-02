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

## Limitations / future work

- **86Box performance figures are owed** — the numbers above are QEMU
  functional checks; QEMU is exact about work done and useless about how long
  it takes (`PERFORMANCE.md`).
- No seeking (an ADPCM seek must respect block boundaries and decoder state).
- No `.M3U` playlists yet, no clickable playlist rows, no drag-drop.
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
