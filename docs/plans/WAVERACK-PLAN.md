# WaveRack — delivery plan

Record it. See it. Cut it. Save it.

WaveRack is a native os8088 sound recorder, WAV player and waveform editor
with a hardware face: a silver faceplate, a recessed LCD, an LED level bar
over an input-gain slider, picture transport keys, an editable waveform with
an overview, and a volume slider. The visual brief is the owner's three
mockups in [`docs/waverack/design/`](../waverack/design/) and the written
brief beside them ([`BRIEF.txt`](../waverack/design/BRIEF.txt)). **The
binding contract is [SPEC.md §122](../../SPEC.md#122-waverack--a-sound-recorder-wav-player-and-waveform-editor-appswaverack-waverackos88)**;
this file is the story list and what each wave actually delivered. The diary
is [HOW-WAVERACK-WAS-BUILT.md](../HOW-WAVERACK-WAS-BUILT.md).

Statuses: **Planned**, **In progress**, **Implemented — verification
pending**, **Verified**, **Blocked**. A story whose acceptance needs real
hardware is never marked Verified from an emulator or a synthetic source;
the evidence column says what was actually seen.

## Stories

| ID | user outcome | depends on | status | evidence / commit |
|---|---|---|---|---|
| US-00 | The plan is reviewable in a draft PR before code | - | In progress | this file, SPEC §122, the draft PR |
| US-01 | WaveRack installs and launches like any package; menus; clean close | US-00 | Planned | |
| US-02 | The same WaveRack in colour and black and white | US-01 | Planned | |
| US-03 | A real 640 x 200 CGA layout with every control reachable | US-02 | Planned | |
| US-04 | Open a WAV and inspect it without loading it into RAM | US-01 | Planned | |
| US-05 | Play, pause, stop, seek and set volume at the right pitch | US-04 | Planned | |
| US-06 | Record from a Sound Blaster into a take that survives | US-04, driver ext | Planned | |
| US-07 | See the active level; set input gain separately from volume | US-05, US-06 | Planned | |
| US-08 | Select a precise range, audition it, loop it | US-05 | Planned | |
| US-09 | Trim, delete, cut, copy, paste, undo and redo | US-08 | Planned | |
| US-10 | Save, Save As, Save Selection As without losing the source | US-09 | Planned | |
| US-11 | The design's personality, readable and fast on an XT | US-02..US-10 | Planned | |
| US-12 | A maintainer can build, install and test it from the PR | all | Planned | |

### Waves

The stories land as six waves on one branch and one draft PR:

1. **Face** — US-01, US-02, US-03: the package, the art generator, the three
   layouts in both themes, every control drawn from real (empty-document)
   state, menus with the commands that do not exist yet greyed.
2. **Document** — US-04: the WAV reader, sources and the piece table, the
   coarse envelope scan, the view cache and its refinement, the waveform
   band, zoom, scroll and the overview.
3. **Transport** — US-05, US-08 and US-07's playback half: the state
   machine, the look-ahead and worker, conversion and volume, the playhead,
   the meter while playing, selection, handles and Loop.
4. **Edit and save** — US-09, US-10: edits, undo/redo, the clipboard,
   dirty-close, Save / Save As / Save Selection As, the replace sequence.
5. **Capture** — US-06, US-07's capture half: SOUND.DRV's external record
   ring, the take file and its header, gain, the meter while recording,
   the recovery marker, the short-take fallback.
6. **Acceptance** — US-11, US-12: XT measurements, the guide entry, the
   test matrix, screenshots, the final soak union and `test-full`.

## US-00 — discovery (what the OS already has, and what it does not)

Read first: [docs/INDEX.md](../INDEX.md), §34 (the sound layer), §35 (the
Recorder), §86 (Audio), §119.6 (CDRack), §121 (XTMP3), §13.14 (the
drop-down), §18.4.7–18.4.9 (read-at, write-at, the streaming append).

| question | answer in this tree | consequence |
|---|---|---|
| package location and format | `apps/<name>/`, `nasm -f bin` at org 0 → `os88pkg.py` → `.o88`; no relocation | `apps/waverack/`, `WAVERACK.O88` |
| face technique | CDRack's: generated art, role tables, one band one blit, element keys, three layouts | `wrdraw.inc` copies `crdraw.inc` with prefix `wd_` |
| display modes | VGA 640x480x16, EGA 640x350x16, Hercules 720x348 mono, CGA 640x200 mono, all from one binary; content boxes ~608x416, ~285 rows, ~136 rows | FULL / MID / COMPACT (§122.11) |
| playback | `OSAPI_SND_STREAM` ring stream through SOUND.DRV's Sound Blaster tier: 8-bit mono ≤ 22,222 Hz, ≤ 44,100 on an SB Pro / SB16; verb 11 stereo/16-bit on those | convert to 8-bit mono at `R/d` (§122.6) |
| capture | verb 4, Sound Blaster only (`SND_CAP_PCM_IN`), 8-bit mono, 4,000–15,000 Hz (13,000 on DSP 1.x); **linear into ONE grant, ≤ 20 KB** ("input has no ring mode"); drained by a kernel task that a disk transfer stalls | a driver extension: verb 4 with `SND_OPENF_EXT` (§122.7) |
| capture gain / source | SOUND.DRV's mixer verbs (§34.15) set output volumes and never write the recording route; SB 2.0 records the mic only | digital gain, labelled; source fixed and greyed |
| full duplex | none: one stream record, either direction | no monitoring; Record and Play exclusive |
| file I/O | `FILE_READ_AT` (any offset), `FILE_WRITE_SEQ` (streaming append, kern_big), `FILE_WRITE_AT` (cluster-aligned offset), `FILE_WRITE`, `RENAME`, `DELETE`, `DFREE`; UI task only (§20.6 rule 7) | worker never touches a file; temp + rename for Save |
| no card | the speaker plays PCM only in an exclusive bracket (§34.11, Audio §86.21); a Covox the same | playback greyed with the reason in this delivery; speaker/Covox is a later story |
| shared UI | `os88ui.inc`: buttons (`OS88UI_BOWN`), the drop-down (§13.14), the check box (§13.15), the alert (`OS88UI_ALERT`), the About card | format row = drop-downs; Save/Discard/Cancel = alert |
| app data | `\SYSTEM\APPDATA` on the boot volume (§19.9), `os88data.inc` | the recovery marker `WAVERACK.TAK` |
| kernels | kern_small has no card tiers (§34.0); kern_mega needs a `PKG_MEGA` arm | kern_big only; on `SMALLOMIT`; mega later |
| emulators | QEMU: SB16 plays, **never raises a capture IRQ**; MartyPC: cycle-exact 5150 + SB 2.0, captures a **flat 80h**; 86Box: real-hardware stand-in, cannot be keyed from this host | capture rate/continuity checked on MartyPC with a flat signal; content never claimed from it |

### Resource assumptions (to be replaced by measurements in US-11)

| | estimate | why |
|---|---|---|
| image | ≤ 40 KB | CDRack is ~20 KB with three layouts; WaveRack adds a document model, a parser, a writer and a capture path |
| bss | ≤ 4 KB | state only; buffers are claims |
| heap, idle with a document | ~8–16 KB | the coarse envelope (≤ 8 KB stereo), the view cache, the undo snapshots (~5 KB) |
| heap, playing | + 32 KB look-ahead (16 KB tier) + SOUND.DRV's 16 KB ring grant (8 KB tier) | Audio's numbers |
| heap, recording | + 32 KB DMA ring (16 / 8 KB tiers) | §122.7 |
| the waveform band | ~9 KB claim | 600 x 120 at 1bpp |

The XTMP3 400 KB budget does not apply. A 256 KB XT with a card should be
able to record and play; a 128 KB one is measured, not promised.

## Acceptance and verification per story

Each wave verifies on the host (`make`, which runs the fast tier;
`make KERN_SMALL=1`; `tools/kernsize.py` on both kernels when the kernel or
a driver moves) and by LOOKING (one boot per adapter, screenshots cropped
and zoomed). It names the suite rows it adds or touches. **Soak rows and
`test-full` run once, after the last wave** (CLAUDE.md, *Testing*).

| ID | acceptance (brief, condensed) | verification |
|---|---|---|
| US-01 | real package with header, icon, `.WAV` association decision, menus File/Edit/View/Sound; focus, expose, move; close frees claims, stream, files; unrelated packages unaffected | `make`; boot, launch, close; heap before/after from the Task Manager; `tools/os88pkg.py` validation; nasm `cpu 8086` + `-w+error` is the opcode audit |
| US-02 | colour and 1bpp faces match the mockups' hierarchy; single meter over gain; no needles or knobs; keyboard and mouse; static drawn once, elements keyed | VGA colour + VGA black-and-white theme + Hercules (MartyPC) screenshots against the mockups; the identity check (incremental vs full repaint, PIL diff = none) |
| US-03 | COMPACT chosen by content box; fits 640x200 with readable 8x8/5x7 text; two footer rows; every control reachable | a 640x200 framebuffer screenshot at integer zoom; every control clicked with `tools/mouse.py --screen 640x200`; label widths asserted by a host check over the layout table |
| US-04 | robust RIFF walk; frame timeline; real min/max envelopes, stereo lanes; zoom/scroll/fit/overview; bounded, cancelable scan | host fixtures from `tools/wrwavfix.py` (tones, impulses, odd chunks, extensible, malformed, truncated) checked by a fast host row against a Python reference reader; a soak row opening each on the guest and reading the parsed fields back by symbol |
| US-05 | Play/Pause/Stop rules; synchronised playhead; explicit conversion; volume not in the data; EOF/underrun/errors | a soak row on QEMU's SB16 capturing the speaker/SB output (`make test-snd`-style) and checking duration and dominant frequency of a known tone at three rates; state transitions read by symbol; MartyPC for underruns at 4.77 MHz |
| US-06 | capture capability honest; take file valid; pause/resume continuity; disk full, overrun, no input; no monitoring | MartyPC SB 2.0 (flat 80h) for rate, frame count and header; QEMU for the no-input path; injected-count unit checks on the host; **real-microphone recording: pending on hardware** |
| US-07 | one meter, dB table, peak hold, clip; source shown; digital gain labelled and applied with saturation; gain ≠ volume | host check of the generated table against `20·log10`; fixture playback of fixed-amplitude tones reading the lit-cell count; gain applied to a known take on MartyPC |
| US-08 | `[s,e)` frames, clamps, single-frame and empty; loop sample counts; stable across zoom and layout | a host model of the selection/loop arithmetic; a soak row looping a selection and counting played bytes by verb 9 |
| US-09 | trim/delete/cut/copy/paste/undo/redo; format refusal; no full copy; transport stopped first | host reference model of the piece table run against the same command scripts as the guest; saved results compared byte for byte |
| US-10 | temp + rename; replace prompt; DFREE; cancel/failure leave the original; rebase after save-over; dirty close | reopen saved files with Python's `wave` module; a full scratch disk for disk-full; source preservation checked on the image |
| US-11 | generated art with selfcheck; fonts complete; measured RAM/code/asset sizes; sustained record/play rates; redraw cost | `--selfcheck`; MartyPC cycle counts for a play and a take on the 5150; primitive-call counters; sizes from the build |
| US-12 | docs, guide page, test matrix, screenshots, final gates | the guide lock; the soak union and `test-full` once |

## Known risks

- **Capture content cannot be observed here.** No emulator in this tree
  delivers a real microphone signal (QEMU: no capture IRQ; MartyPC: flat
  80h). The pipeline, the counts and the files are testable; what a
  microphone sounds like is a field run.
- **The floppy may not keep up with a take.** 11,025 B/s is ~40% of an XT
  floppy's write rate. The 32 KB ring buys ~3 s of slack per write; the
  overrun path keeps what was written. Measured in US-11; 8,000 Hz is the
  floppy's rate if it must be.
- **The DMA ring must be page-safe** (§50.3), and a 32 KB head is the
  hardest claim to place; the tiers fall to 16 and 8 KB.
- **XT redraw cost.** The waveform band is the design's answer; a full
  waveform repaint is measured in US-11 against PERFORMANCE.md's table.
- **Floppy reads during refinement** hold `sch_lock`; refinement is bounded
  per pass and abandoned when the view moves.
- **Driver ABI.** The verb-4 bit is additive; the ABI lock is updated
  without a bump, and old packages are unaffected.
