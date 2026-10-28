# WaveRack — delivery plan

Record it. See it. Cut it. Save it.

WaveRack is a native os8088 sound recorder, WAV player and waveform editor
with a hardware face: a silver faceplate, a recessed LCD, an LED level bar
over an input-gain slider, picture transport keys, an editable waveform with
an overview, and a volume slider. The visual brief is the owner's three
mockups in [`docs/waverack/design/`](../waverack/design/) and the written
brief beside them ([`BRIEF.txt`](../waverack/design/BRIEF.txt)). **The
binding contract is [SPEC.md §123](../../SPEC.md#123-waverack--a-sound-recorder-wav-player-and-waveform-editor-appswaverack-waverackos88)**;
this file is the story list and what each wave actually delivered. The diary
is [HOW-WAVERACK-WAS-BUILT.md](../HOW-WAVERACK-WAS-BUILT.md).

Statuses: **Planned**, **In progress**, **Implemented — verification
pending**, **Verified**, **Blocked**. A story whose acceptance needs real
hardware is never marked Verified from an emulator or a synthetic source;
the evidence column says what was actually seen.

## Stories

| ID | user outcome | depends on | status | evidence / commit |
|---|---|---|---|---|
| US-00 | The plan is reviewable in a draft PR before code | - | Verified | this file, SPEC §123, draft PR #311 opened at 3cde0b9a before any functional change |
| US-01 | WaveRack installs and launches like any package; menus; clean close | US-00 | Verified (final pass: every WaveRack row launches it, and `waverackplay` closes and relaunches it, claims given back) | `WAVERACK.O88` on the 1.44/1.2 MB apps disks and media360.img; launched, menus, About, closed and relaunched on QEMU VGA (relaunch face identical to the first); `make`, `make KERN_SMALL=1` green; see Wave 1 below |
| US-02 | The same WaveRack in colour and black and white | US-01 | Implemented — verification pending: the black-and-white theme and EGA were looked at in wave 1 and no final-pass row covers them (VGA colour and Hercules are on the final pass's screens) | `docs/waverack/waverack-vga-color-full.png`, `waverack-vga-bw-full.png`, `waverack-ega-mid.png`; identity check `None` on VGA colour, VGA B&W, EGA and across a layout round trip; Hercules MID and COMPACT seen on MartyPC's 5150 + Hercules (`docs/waverack/waverack-herc-mid.png`, `waverack-herc-compact.png`, captured from VRAM at 038ea5ed) |
| US-03 | A real 640 x 200 CGA layout with every control reachable | US-02 | Implemented — verification pending: CGA COMPACT was looked at and clicked in wave 1; no final-pass row runs a CGA (the guide's COMPACT picture is VGA) | `docs/waverack/waverack-cga-compact.png` (native 640 x 200) and `-3x.png`; all 15 controls clicked with `--screen 640x200`, each answered; the layout measurement below |
| US-04 | Open a WAV and inspect it without loading it into RAM | US-01 | Verified (final pass: `waverack` - every fixture against `wrsim`, the identity check; `waverackdisk` - a 2 MB scan) | `wrwav.inc`/`wrdoc.inc`/`wrenv.inc`; `tools/wrsim.py` reference + fast row `wrsim`; soak row `waverack` (every fixture through File > Open, bss vs the reference, the view cache column for column, zoom to fpp 1, identity); screenshots `docs/waverack/waverack-w2-*.png`; see Wave 2 below |
| US-05 | Play, pause, stop, seek and set volume at the right pitch | US-04 | Verified (final pass: `waverackplay` sample for sample, `waverackdisk` before/after on XT-IDE and a floppy). Heard on hardware: pending - `make xt-waverack` is the machine | `wrxport.inc` (§123.6 shipped): Audio's pipeline over the document, the conversion `R / d` with the box filter, VOLUME's table, Pause closing the stream, Stop restoring the playhead; `tools/wrsim.py`'s `convert`/`play_pos` in the fast `wrsim` row; soak row `waverackplay` (QEMU SB16, the capture sample for sample at 22,050 Hz and at d = 2 on a DSP 2.01); screenshots `docs/waverack/waverack-w3-*.png`; see Wave 3 below. **Wave 4b** (play through disk reads, see below): our own external ring, convert before read, bounded sequential reads per medium, the hold; soak row `waverackdisk` (MartyPC, XT-IDE + SB 2.0, a 1.44 MB floppy and a 321 MB volume of 32 KB clusters, before/after): BIG22 0.00 s silent of 30.2 (Wave 4: 17.67 of 30.3), and 0.00 of 40.3 with a second WaveRack scanning the disk (26.91 of 40.0); LONG off the floppy 0.07 of 30.2 (18.71 of 30.4); 16-bit stereo HELD on an 8088; 32 KB clusters open, play and save (Wave 4: refused). Not heard on 86Box or iron |
| US-06 | Record from a Sound Blaster into a take that survives | US-04, driver ext | Implemented — verification pending ON HARDWARE: a real microphone's take and a real disk's write speed (final pass: `waverackrec`, every take against `wrsim.take_header`, 0 overruns, recovery, disk full, no input; `sndcap`) | driver half 1a027a77 (§34.6.4, `sndcap`); **app side wave 5** (`wrrec.inc`, §123.7 shipped): VOICEnnn.WAV with a one-cluster header patched at Stop, the worker copying ring to 64 KB of staging units and the UI writing whole clusters, Pause/resume, Stop opening the take clean, disk full / write error / overrun / no input each with its sentence, WAVERACK.TAK and File > Recover Take, a DSP 1.x refused, a floppy at 8,000 Hz. Soak row `waverackrec` (MartyPC XT-IDE + SB 2.0 and a VGA XT on 32 KB clusters, a 1.44 MB floppy, QEMU): every take read back off the disk against `wrsim.take_header`, frames = the card's count, 0 overruns, the recovery after a kill. **Not here: a real microphone's content, a real disk's write speed** |
| US-07 | See the active level; set input gain separately from volume | US-05, US-06 | Implemented — verification pending ON HARDWARE: the gain heard and a real signal's lit cells (MartyPC records a flat 80h); the gain table is held to `wrsim` by `waverackrec` and to 20 log10 by the fast `wrsim` row | the PLAYBACK half in wave 3 (§123.8); **the capture half in wave 5**: `LEVEL  REC` from the extremes of what is WRITTEN (after the gain), INPUT GAIN (DIGITAL) -12..+12 dB in 13 steps through a saturating table built from `wrsim.gain_q8` (selfcheck against `20 log10`; the soak row reads the table out of the ring claim at +4 dB against `wrsim.gain_table`), the slider live during a take, VOLUME untouched. **Not here: the gain heard, the lit cells of a real signal** (MartyPC's input is a flat 80h) |
| US-08 | Select a precise range, audition it, loop it | US-05 | Verified (final pass: `waverackplay` loops a selection and counts the played bytes) | §123.9's selection half shipped in wave 3: drag, handles, Shift+click, Shift+Left/Right, Select All / None, `[s, e)` clamped, the field's two formats, the inverted span (B&W) / its own ground (colour) drawn by changed columns only; Loop seamless; the soak row drags a range, loops it and holds verb 9's count to `play_pos` and the capture to the selection repeated; identity checks with a selection on VGA colour, B&W, COMPACT and the CGA knob tree |
| US-09 | Trim, delete, cut, copy, paste, undo and redo | US-08 | Verified (final pass: `waverackedit` - every edit against `wrsim`'s `Editor`) | `wredit.inc` (§123.3, §123.9 shipped): piece lists built and coalesced, the 64-piece refusal, a 9-state undo ring in a 6 KB claim, the 16-piece clipboard kept across New and Open (format refusal, adopted by an empty document), sources swept; edits grey while a play is held (§123.6's table); the dirty mark is the window title; `tools/wrsim.py`'s `Editor` against a flat model in the fast `wrsim` row; soak row `waverackedit` (every edit's piece table, selection and history against the reference); identity `None` after a cut at Fit and at fpp 1; screenshots `docs/waverack/waverack-w4-*.png`; see Wave 4 below |
| US-10 | Save, Save As, Save Selection As without losing the source | US-09 | Verified (final pass: `waverackedit` - saves byte for byte, cancel, rebase, disk full, the questions; a save onto a live take refused, wave 6b) | `wrsave.inc` (§123.10 shipped): `WRSAVE.$$$`, DFREE first, the canonical header, a gulp a tick through a 16 KB output claim (`OSAPI_FILE_WRITE` then `OSAPI_FILE_WRITE_SEQ`), Escape cancels, delete + rename, `Replace NAME?`, the rebase over a source, the save-changes question; `wrsim.write_wav` read back by `wave` and `parse()`; soak row `waverackedit` (each saved file read back off the guest's floppy, byte for byte; the cancel, the rebase, the disk-full refusal, the close question); see Wave 4 below. Not seen: a write-protected or full disk MID-write (the refusal before writing is tested), a real XT's save time **Wave 4c** (wave 4's review, folded into the disk-read wave): the temp is `WRSAVnn.$$$`, the first name not on disk, made empty at once (H1, M1); the save-changes and Replace questions grey the document's commands and re-front on refusal, their answers re-check (H2); upper-case source names (M2); L1-L8; clusters to 32 KB with a cluster-sized output claim. `waverackedit` gains the `temp` and `asked` legs, edited and not run; `waverackdisk`'s `bigclu` leg saves on 32 KB clusters byte for byte |
| US-11 | The design's personality, readable and fast on an XT | US-02..US-10 | Implemented — verification pending ON HARDWARE: a real XT's disk and floppy (the US-11 table's numbers are MartyPC's and QEMU's, completed by the final pass) | the US-11 table below (wave 6a): every number from Waves 1-5 with its source; what the final pass still produces is named by row and leg (`waverackrec`'s `calls` leg, written in 6a: draw calls a tick recording and playing on the 5150, a cut, a full repaint in ms). The art selfcheck now holds the small caps complete for every caption and every status message to 22 characters, in the fast `wrsim` row |
| US-12 | A maintainer can build, install and test it from the PR | all | Verified (wave 6b: the soak union and `test-full` from this branch; the guide's pictures taken) | the US-12 section below (wave 6a): build commands, the disks, `make waverackfix`, `make waverackhd` + `make xt-waverack` (a new 86Box XT with an ST-225 and a Sound Blaster on IRQ 7), the test matrix, what is pending on hardware; the guide's WAVERACK.O88 entry and manual (stamped), SOUND_DRV restamped; INDEX regenerated. The final soak union and `test-full` are wave 6's last step |

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
   **4b. Play through disk reads** — US-05 hardening, after the owner heard
   the play stop on 86Box whenever the disk was read: our own external
   ring, convert before read, reads bounded per medium and made sequential,
   the hold, hard disk first.
5. **Capture** — US-06, US-07's capture half: SOUND.DRV's external record
   ring, the take file and its header, gain, the meter while recording,
   the recovery marker, the short-take fallback (wave 5 refused it with a
   reason instead, §123.7).
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
| display modes | VGA 640x480x16, EGA 640x350x16, Hercules 720x348 mono, CGA 640x200 mono, all from one binary; content boxes ~608x416, ~285 rows, ~136 rows | FULL / MID / COMPACT (§123.11) |
| playback | `OSAPI_SND_STREAM` ring stream through SOUND.DRV's Sound Blaster tier: 8-bit mono ≤ 22,222 Hz, ≤ 44,100 on an SB Pro / SB16; verb 11 stereo/16-bit on those | convert to 8-bit mono at `R/d` (§123.6) |
| capture | verb 4, Sound Blaster only (`SND_CAP_PCM_IN`), 8-bit mono, 4,000–15,000 Hz (13,000 on DSP 1.x); **linear into ONE grant, ≤ 20 KB** ("input has no ring mode"); drained by a kernel task that a disk transfer stalls | a driver extension: verb 4 with `SND_OPENF_EXT` (§123.7) |
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
| heap, recording | + 32 KB DMA ring (16 / 8 KB tiers) | §123.7 |
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
| US-04 | robust RIFF walk; frame timeline; real min/max envelopes, stereo lanes; zoom/scroll/fit/overview; bounded, cancelable scan | host fixtures from `tools/wrsim.py --fixtures` (tones, impulses, odd chunks, extensible, malformed, truncated) checked by the fast row `wrsim` against the Python reference reader and `wave`; the soak row `waverack` opening each on the guest and reading the parsed fields and the view cache back by symbol |
| US-05 | Play/Pause/Stop rules; synchronised playhead; explicit conversion; volume not in the data; EOF/underrun/errors | a soak row on QEMU's SB16 capturing the speaker/SB output (`make test-snd`-style) and checking duration and dominant frequency of a known tone at three rates; state transitions read by symbol; MartyPC for underruns at 4.77 MHz |
| US-06 | capture capability honest; take file valid; pause/resume continuity; disk full, overrun, no input; no monitoring | MartyPC SB 2.0 (flat 80h) for rate, frame count and header; QEMU for the no-input path; the take file modelled in `tools/wrsim.py` (`take_header`, the fast `wrsim` row); **real-microphone recording: pending on hardware**. Wave 5: `waverackrec` |
| US-07 | one meter, dB table, peak hold, clip; source shown; digital gain labelled and applied with saturation; gain ≠ volume | host check of the generated table against `20·log10`; fixture playback of fixed-amplitude tones reading the lit-cell count; gain applied to a known take on MartyPC |
| US-08 | `[s,e)` frames, clamps, single-frame and empty; loop sample counts; stable across zoom and layout | a host model of the selection/loop arithmetic; a soak row looping a selection and counting played bytes by verb 9 |
| US-09 | trim/delete/cut/copy/paste/undo/redo; format refusal; no full copy; transport stopped first | host reference model of the piece table run against the same command scripts as the guest; saved results compared byte for byte |
| US-10 | temp + rename; replace prompt; DFREE; cancel/failure leave the original; rebase after save-over; dirty close | reopen saved files with Python's `wave` module; a full scratch disk for disk-full; source preservation checked on the image |
| US-11 | generated art with selfcheck; fonts complete; measured RAM/code/asset sizes; sustained record/play rates; redraw cost | `--selfcheck`; MartyPC cycle counts for a play and a take on the 5150; primitive-call counters; sizes from the build |
| US-12 | docs, guide page, test matrix, screenshots, final gates | the guide lock; the soak union and `test-full` once |

## Wave 1 — the face (US-01..US-03): what shipped and what was seen

Built: `apps/waverack/` (`waverack.asm`, `wrdraw.inc`, `wrface.inc`),
`tools/os88wrart.py` (pictures, fonts, the meter's dB table, the role tables)
and `tools/os88wrlay.py` (the three layouts as data, and their measurement).
Image 16,310 bytes, packed 11,646, bss 1,414; a 7 KB waveform band claimed at
entry. SPEC.md §123.11–§123.13 are marked shipped with these figures.

Seen on the glass (QEMU): FULL on VGA in both themes, MID on EGA (`VIDEO=ega`),
COMPACT on CGA (`VIDEO=cga`) and on VGA through View > Compact Layout. The
identity check (change the face through it, force a full repaint with the
About card, PIL difference) was `None` on VGA colour, VGA black and white and
EGA, and across COMPACT-and-back on VGA. On the CGA every control was clicked
with `tools/mouse.py --screen 640x200`: the eight keys and the three edit
keys answered with their reasons (`No capture device`, `Nothing to stop`,
`Needs a Sound Blaster`, `Not in this build yet`), Loop with `Loop on`, INPUT
GAIN and VOLUME with their values, the greyed Input drop-down with `Only one
choice here`, and the Rate list opened, slid up over its box and its pick
reached the LCD. Not seen: Hercules (QEMU cannot show it - MartyPC's look),
86Box, real hardware.

**The layout measurement** (`python3 tools/os88wrart.py --selfcheck`, run by
the make rule before the include is written) - this is the brief's "measure
every label, truncation rule, control rectangle and hit target":

```
FULL 608 x 400 (window 610 x 419 on a 640 x 480 screen):
  24 controls, 27 static parts, 10 element texts - no overlap, all inside
  key REC     64 x 42 inside, needs  39 x 25  (RECORD)
  key STOP    64 x 42 inside, needs  27 x 25  (STOP)
  key PAUSE   64 x 42 inside, needs  33 x 25  (PAUSE)
  key PLAY    64 x 42 inside, needs  27 x 25  (PLAY)
  key OPEN    64 x 42 inside, needs  27 x 25  (OPEN)
  key SAVE    64 x 42 inside, needs  27 x 25  (SAVE)
  key ZOUT    44 x 26 inside, needs  19 x 15  (i_minus)
  key ZIN     44 x 26 inside, needs  19 x 15  (i_plus)
  key TRIM   110 x 20 inside, needs  29 x  9  (TRIM)
  key DEL    110 x 20 inside, needs  41 x  9  (DELETE)
  key SAVEAS 102 x 20 inside, needs  65 x  9  (SAVE AS...)
  drop IN    120 px, longest 'Microphone' needs  99
  drop RATE  100 px, longest '11,025 Hz' needs  91
  drop BITS   68 px, longest '8-bit'     needs  59
  drop CH     70 px, longest 'Mono'      needs  51
  selection field 29 chars, needs 25 ('Selection:  ' + 'SS.cc - SS.cc')
  status field 34 chars, longest message 22
MID 608 x 272 (window 610 x 291 on a 640 x 350 screen):
  24 controls, 18 static parts, 10 element texts - no overlap, all inside
  key REC     64 x 28 inside, needs  39 x 25  (RECORD)
  key STOP    64 x 28 inside, needs  27 x 25  (STOP)
  key PAUSE   64 x 28 inside, needs  33 x 25  (PAUSE)
  key PLAY    64 x 28 inside, needs  27 x 25  (PLAY)
  key OPEN    64 x 28 inside, needs  27 x 25  (OPEN)
  key SAVE    64 x 28 inside, needs  27 x 25  (SAVE)
  key ZOUT    44 x 22 inside, needs  19 x 15  (i_minus)
  key ZIN     44 x 22 inside, needs  19 x 15  (i_plus)
  key TRIM   110 x 14 inside, needs  29 x  9  (TRIM)
  key DEL    110 x 14 inside, needs  41 x  9  (DELETE)
  key SAVEAS 102 x 14 inside, needs  65 x  9  (SAVE AS...)
  drop IN    120 px, longest 'Microphone' needs  99
  drop RATE  100 px, longest '11,025 Hz' needs  91
  drop BITS   68 px, longest '8-bit'     needs  59
  drop CH     70 px, longest 'Mono'      needs  51
  selection field 29 chars, needs 25 ('Selection:  ' + 'SS.cc - SS.cc')
  status field 34 chars, longest message 22
CMP 624 x 132 (window 626 x 151 on a 640 x 200 screen):
  24 controls, 8 static parts, 10 element texts - no overlap, all inside
  key REC     74 x 13 inside, needs  39 x  9  (REC)
  key STOP    74 x 13 inside, needs  45 x  9  (STOP)
  key PAUSE   74 x 13 inside, needs  51 x  9  (PAUSE)
  key PLAY    74 x 13 inside, needs  45 x  9  (PLAY)
  key OPEN    74 x 13 inside, needs  48 x  9  (OPEN)
  key SAVE    74 x 13 inside, needs  45 x  9  (SAVE)
  key ZOUT    44 x 13 inside, needs  19 x  9  (h_minus)
  key ZIN     43 x 13 inside, needs  19 x  9  (h_plus)
  key TRIM    54 x  9 inside, needs  29 x  9  (TRIM)
  key DEL     62 x  9 inside, needs  41 x  9  (DELETE)
  key SAVEAS  80 x  9 inside, needs  65 x  9  (SAVE AS...)
  drop IN     52 px, longest 'Mic'       needs  43
  drop RATE   72 px, longest '11 kHz'    needs  67
  drop BITS   60 px, longest '8-bit'     needs  59
  drop CH     52 px, longest 'Mono'      needs  51
  selection field 19 chars, needs 18 ('Sel: ' + 'SS.cc - SS.cc')
  status field 22 chars, longest message 22
os88wrart: selfcheck ok
```

The truncation rule it holds the asm to: a field is padded or CUT to its
character count (`wf_pad`, `wf_padc`), and every message the package can put
there is at most 22 characters (`STATUS_MAX`), COMPACT's status field.

## Wave 2 — the document (US-04): what shipped and what was seen

Built: `apps/waverack/wrwav.inc` (the RIFF walk, SPEC.md §123.4),
`wrdoc.inc` (sources, the piece table, 32-bit arithmetic, `wo_read`, §123.3),
`wrenv.inc` (the scan, the view and its cache, refinement, the ruler, the
lanes, the overview, the cursor, §123.5); `tools/wrsim.py` (the reference and
the fixture generator); `make waverackfix` (`build/wrfix.img`, on demand);
the fast row `wrsim` (`tests/unit/t_wrsim.py`) and the soak row `waverack`
(`tests/waverack.py`). Also in this wave: a greyed key on a 1bpp face keeps
its caption solid, dithers its picture and dots its frame (§123.11).

Measured: image 25,010 bytes (16,310 in wave 1), packed 18,926 (19
clusters), bss 2,525. Heap: the band 10 KB from entry; with a document open
and idle the I/O claim 16 KB, the view cache 4 KB and the envelope 1..8 KB
(the soak row's output, below, has the figure per fixture). `media360.img` is
now **354 of 354 clusters** - the next wave does not fit it without a
curation decision.

Seen on the glass (QEMU, VGA unless named): the tone, the impulses (every
spike at Fit, and in the overview), the stereo file (two lanes, L over R, the
right channel's one spike), LONG.WAV (104 s, its envelope's swells and its
four silent seconds), the tone zoomed to fpp 1 (a sine traced as a line), a
refusal (`Format 0011h: not PCM`, NO FILE, the document empty), the
black-and-white theme and View > Compact Layout on VGA, and COMPACT on the
CGA knob tree (`VIDEO=cga`, the impulses at Fit). Clicks in the overview
centred the view, a click in the waveform put the cursor there, Home/End
went to the ends (the end's line on the last column), PgDn paged, `F` fitted.
The identity check after the greyed-key change, on the black-and-white
face with the stereo file zoomed to fpp 16 and refined (About up and down
for the full repaint, PIL difference): `None`
(`docs/waverack/waverack-w2-bw-stereo.png`); the soak row takes the same
check on the colour face. The wheel was sent and moved neither the view nor VOLUME on QEMU's serial
mouse (no wheel reaches the guest there); the code path is unverified.

The soak row's output (`python3 tools/os88test.py soak -v -k waverack`). Its first pass failed on two faults in the TEST - the pointer drawn into both screenshots, and the claim map read by the wrong owner (a package's claims are owned by its region's segment) - and every check of the package passed; after the fix it was run again, and once more with `-v` because the runner keeps no output for a passing row:

```
os88test: building 3 declared artefact(s): build/os8088.img build/wrfix.img build/waverack.bin
os88test: soak tier - 1 rows, 150s declared, budget none  (caps: lame,mtools,nasm,nasm3,numpy,pil,qemu,qemu9,unicorn)

 ok  waverack                     219.1s
       |    the running WaveRack is build/waverack.bin (make waverackfix) ok
       |    WaveRack launches off B:                                     ok
       | waverack: 17 fixtures, each through File > Open
       |  DATA1ST.WAV - data before fmt
       |    DATA1ST.WAV: opened through the dialog                       ok
       |    DATA1ST.WAV: 8000 Hz 8-bit 1ch, 4000 frames at 20 (note 0)   ok
       |    DATA1ST.WAV: err 0, a document                               ok
       |    DATA1ST.WAV: says 'Ready'                                    ok
       |    DATA1ST.WAV: z 3 first 0 - the view cache is the reference's ok
       |    DATA1ST.WAV: every column exact                              ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  EXTENS.WAV - WAVE_FORMAT_EXTENSIBLE, PCM sub-format
       |    EXTENS.WAV: opened through the dialog                        ok
       |    EXTENS.WAV: 11025 Hz 16-bit 1ch, 5000 frames at 68 (note 0)  ok
       |    EXTENS.WAV: err 0, a document                                ok
       |    EXTENS.WAV: says 'Ready'                                     ok
       |    EXTENS.WAV: z 4 first 0 - the view cache is the reference's  ok
       |    EXTENS.WAV: every column exact                               ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  IMPULSE.WAV - mono 8-bit silence with single-sample spikes
       |    IMPULSE.WAV: opened through the dialog                       ok
       |    IMPULSE.WAV: 11025 Hz 8-bit 1ch, 22050 frames at 44 (note 0) ok
       |    IMPULSE.WAV: err 0, a document                               ok
       |    IMPULSE.WAV: says 'Ready'                                    ok
       |    IMPULSE.WAV: z 6 first 0 - the view cache is the reference's ok
       |    IMPULSE.WAV: every column exact                              ok
       |      heap, idle with it open: [3, 4, 10, 16] KB = 33 KB
       |    IMPULSE: zoomed to fpp 1 about the cursor                    ok
       |    IMPULSE zoomed: z 0 first 4914 - the view cache is the reference's ok
       |    IMPULSE zoomed: every column exact                           ok
       |    IMPULSE zoomed: the spike's column and the next reach it     ok
       |    identity: incremental == a full repaint (zoomed)             ok
       |  LISTODD.WAV - an odd-length LIST chunk (and its pad) before data
       |    LISTODD.WAV: opened through the dialog                       ok
       |    LISTODD.WAV: 8000 Hz 8-bit 1ch, 4000 frames at 58 (note 0)   ok
       |    LISTODD.WAV: err 0, a document                               ok
       |    LISTODD.WAV: says 'Ready'                                    ok
       |    LISTODD.WAV: z 3 first 0 - the view cache is the reference's ok
       |    LISTODD.WAV: every column exact                              ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  NONWHOLE.WAV - a data size not a whole number of frames: floored
       |    NONWHOLE.WAV: opened through the dialog                      ok
       |    NONWHOLE.WAV: 11025 Hz 16-bit 2ch, 1000 frames at 44 (note 2) ok
       |    NONWHOLE.WAV: err 0, a document                              ok
       |    NONWHOLE.WAV: says '3 bytes ignored at end'                  ok
       |    NONWHOLE.WAV: z 1 first 0 - the view cache is the reference's ok
       |    NONWHOLE.WAV: every column exact                             ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  PASTEND.WAV - a data size past the file's end: clamped
       |    PASTEND.WAV: opened through the dialog                       ok
       |    PASTEND.WAV: 8000 Hz 8-bit 1ch, 3000 frames at 44 (note 1)   ok
       |    PASTEND.WAV: err 0, a document                               ok
       |    PASTEND.WAV: says 'Data clamped to file'                     ok
       |    PASTEND.WAV: z 3 first 0 - the view cache is the reference's ok
       |    PASTEND.WAV: every column exact                              ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  STEREO16.WAV - stereo 16-bit 22,050 Hz: L a tone, R silence and one spike
       |    STEREO16.WAV: opened through the dialog                      ok
       |    STEREO16.WAV: 22050 Hz 16-bit 2ch, 22050 frames at 44 (note 0) ok
       |    STEREO16.WAV: err 0, a document                              ok
       |    STEREO16.WAV: says 'Ready'                                   ok
       |    STEREO16.WAV: z 6 first 0 - the view cache is the reference's ok
       |    STEREO16.WAV: every column exact                             ok
       |      heap, idle with it open: [4, 6, 10, 16] KB = 36 KB
       |  TONE8.WAV - mono 8-bit 11,025 Hz, a 1 kHz tone, 1 s
       |    TONE8.WAV: opened through the dialog                         ok
       |    TONE8.WAV: 11025 Hz 8-bit 1ch, 11025 frames at 44 (note 0)   ok
       |    TONE8.WAV: err 0, a document                                 ok
       |    TONE8.WAV: says 'Ready'                                      ok
       |    TONE8.WAV: z 5 first 0 - the view cache is the reference's   ok
       |    TONE8.WAV: every column exact                                ok
       |      heap, idle with it open: [2, 4, 10, 16] KB = 32 KB
       |  UNFIN.WAV - an unfinished take: data size 0, bytes after it
       |    UNFIN.WAV: opened through the dialog                         ok
       |    UNFIN.WAV: 11025 Hz 8-bit 1ch, 8000 frames at 44 (note 3)    ok
       |    UNFIN.WAV: err 0, a document                                 ok
       |    UNFIN.WAV: says 'Recovered; save to fix'                     ok
       |    UNFIN.WAV: z 4 first 0 - the view cache is the reference's   ok
       |    UNFIN.WAV: every column exact                                ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  UNKNOWN.WAV - unknown chunks before and after
       |    UNKNOWN.WAV: opened through the dialog                       ok
       |    UNKNOWN.WAV: 8000 Hz 8-bit 1ch, 4000 frames at 110 (note 0)  ok
       |    UNKNOWN.WAV: err 0, a document                               ok
       |    UNKNOWN.WAV: says 'Ready'                                    ok
       |    UNKNOWN.WAV: z 3 first 0 - the view cache is the reference's ok
       |    UNKNOWN.WAV: every column exact                              ok
       |      heap, idle with it open: [1, 4, 10, 16] KB = 31 KB
       |  ADPCM.WAV - IMA ADPCM, tag 0011h
       |    ADPCM.WAV: opened through the dialog                         ok
       |    ADPCM.WAV: refused with code 4 (17), the reference's         ok
       |    ADPCM.WAV: the document is empty                             ok
       |    ADPCM.WAV: says 'Format 0011h: not PCM'                      ok
       |    ADPCM.WAV: every document claim given back (heap [10] KB)    ok
       |  BADALIGN.WAV - a block align that disagrees
       |    BADALIGN.WAV: opened through the dialog                      ok
       |    BADALIGN.WAV: refused with code 7 (0), the reference's       ok
       |    BADALIGN.WAV: the document is empty                          ok
       |    BADALIGN.WAV: says 'Bad block align'                         ok
       |    BADALIGN.WAV: every document claim given back (heap [10] KB) ok
       |  BITS24.WAV - 24-bit
       |    BITS24.WAV: opened through the dialog                        ok
       |    BITS24.WAV: refused with code 5 (24), the reference's        ok
       |    BITS24.WAV: the document is empty                            ok
       |    BITS24.WAV: says '24-bit is unsupported'                     ok
       |    BITS24.WAV: every document claim given back (heap [10] KB)   ok
       |  CHAN3.WAV - three channels
       |    CHAN3.WAV: opened through the dialog                         ok
       |    CHAN3.WAV: refused with code 6 (3), the reference's          ok
       |    CHAN3.WAV: the document is empty                             ok
       |    CHAN3.WAV: says '3 channels unsupported'                     ok
       |    CHAN3.WAV: every document claim given back (heap [10] KB)    ok
       |  CHUNK70.WAV - 70 chunks before fmt
       |    CHUNK70.WAV: opened through the dialog                       ok
       |    CHUNK70.WAV: refused with code 9 (0), the reference's        ok
       |    CHUNK70.WAV: the document is empty                           ok
       |    CHUNK70.WAV: says 'More than 64 chunks'                      ok
       |    CHUNK70.WAV: every document claim given back (heap [10] KB)  ok
       |  NOTRIFF.WAV - not RIFF
       |    NOTRIFF.WAV: opened through the dialog                       ok
       |    NOTRIFF.WAV: refused with code 1 (0), the reference's        ok
       |    NOTRIFF.WAV: the document is empty                           ok
       |    NOTRIFF.WAV: says 'Not a RIFF/WAVE file'                     ok
       |    NOTRIFF.WAV: every document claim given back (heap [10] KB)  ok
       |  LONG.WAV - ~1.1 MB, 104 s: proves the scan streams
       |    LONG.WAV: opened through the dialog                          ok
       |    LONG.WAV: 11025 Hz 8-bit 1ch, 1150000 frames at 44 (note 0)  ok
       |    LONG.WAV: err 0, a document                                  ok
       |    LONG.WAV: says 'Ready'                                       ok
       |    LONG.WAV: z 11 first 0 - the view cache is the reference's   ok
       |    LONG.WAV: every column exact                                 ok
       |      heap, idle with it open: [3, 4, 10, 16] KB = 33 KB
       | waverack: ok

os88test: 1 passed, 0 failed, 0 skipped in 219.1s, 87.3s charged (budget none)
rc=0
```

## Wave 3 — the transport (US-05, US-08, US-07's playback half): what shipped and what was seen

Built: `apps/waverack/wrxport.inc` (`wt_`, §123.6: the state machine, the
look-ahead and its copying fill, the worker, the conversion, VOLUME, the
playhead, the meter's state); the selection model and its class-aware runs
in `wrenv.inc` (§123.9); the line, the meter's mask diff, the selection
element `wf_e_wsel` and the selection field in `wrface.inc`; the
predicate, the keys, the gestures, `OSAPI_WM_ONWAKE` and the close
negotiator in `waverack.asm`; `tools/wrsim.py`'s playback reference and two
fixtures (TONE8K.WAV, 8,000 Hz 16-bit; TONE44.WAV, 44,100 Hz 8-bit stereo);
the soak row `waverackplay` (`tests/waverackplay.py`); `-DWR_CALLS`, a
counter build of every drawing primitive.

Measured: image 30,179 bytes, packed 23,129 (23 clusters), bss 5,016.
Heap while playing: the idle document's claims plus the 32 KB look-ahead
(the soak row's output below has the figure); the 16 KB ring grant is
SOUND.DRV's. Drawing (the counter build, QEMU, LONG.WAV): **0 calls a tick
idle, 1.70 a tick playing at Fit, 3.08 at 256 frames a column, 5-6 a
selection drag move, 120 a full repaint.**

Seen on the glass (QEMU with `-device sb16`): STEREO16, TONE8, LONG.WAV
played - the playhead moving, the time following it, `LEVEL  PLAY` lit (and
PEAK at STEREO16's spike), the Play key latched; Pause (the Pause key
latched, the position held, the status `Paused`) and resume; Stop putting
the playhead back; a click while playing seeking there, Right while paused
moving the paused position a column from the playhead, End while playing
ending it; a zoomed play paging the view to the playhead; Open greyed
while playing; a drag selecting, both handles dragged, Select All, Select
None, Shift+Right twice; Loop on a selection; View > Theme and View >
Compact Layout with a selection standing still across both. The identity
check (a forced full repaint, the About card up and down, PIL difference)
with a selection present: **`None`** on VGA colour FULL, VGA black and
white FULL, VGA black and white COMPACT, and COMPACT on the CGA knob tree
(`make BUILD=build-cga VIDEO=cga`, a loop playing there too). Screenshots:
`docs/waverack/waverack-w3-play.png`, `-play-zoom.png`, `-paused.png`,
`-sel-col.png`, `-sel-bw.png`, `-sel-cmp-bw.png`, `-loop.png`,
`-cga-cmp-sel.png` (native 640 x 200) and `-cga-cmp-sel-3x.png`.

**The 360 KB disk (an open owner decision).** `media360.img` was 354 of 354
clusters after wave 2 and wave 3's package does not fit beside the rest, so
WaveRack is OFF it, dated 2026-10-26 in the Makefile (`MEDIAARGS360`) and in
§123.13; without it the disk is 335 of 354. **No 360 KB disk carries
WaveRack now.** A 360 KB home for it - a disk of its own, or a curation that
moves something else off `media360.img` - is the owner's to decide; nothing
else was moved.

Deviations, each written into §123: the pinned status sentences that do not
fit COMPACT's 22 characters are shortened (`Out 22,050 Hz mono /2`, `Card
needs 4,000+ Hz`); the black-and-white selection is the band inverted for
its blit rather than a dither (a dither cannot be a blit's paper); Loop is
latched at Play; the line is the playhead while a play is held and the
cursor otherwise, and none beside a selection.

**Folded in from wave 2's review** (`wr-reports/review-wave2.md`, each
verified against the code first): **M1** the walk now tests the carry of
`pos + 8` (a chunk size that carried the next position to FFFFFFF8h wrapped
the header to offset 0..7 and read at ~4 GB, answering E_IO where wrsim
answers E_NODATA/E_NOFMT) - new BAD fixture `WRAPPOS.WAV`, refused on the
glass with `No data chunk`, the document empty; **M2** `we_clamp` rounds the
view's bound UP to a multiple of fpp, so the last `frames mod fpp` frames are
reachable below Fit - LONG.WAV at z = 10: first 548,864 (was 547,840), the
cursor at the end on the last column, Left and Right round it; wrsim's
`view_clamp` and a selfcheck case for every z below Fit; **L1** a frame is
negative only from C000_0000h up, so a step past the end of a document over
2^30 frames clamps to the end, not 0 (code-read; no fixture that large fits
a floppy); **L2** a scan read that delivers nothing short of the source's
frames refuses the document with `Cannot read the file` instead of ending
silently over unscanned blocks (code-read; nothing here shortens a READ_AT);
**L3** §123.5 says `ceil(frames / B)` and LONG.WAV's 1,124 blocks.

The soak row, run once at the end (`python3 tools/os88test.py soak -v -k
waverackplay`). Its FIRST run failed in the test (bytes handed to numpy as
an int list) and at one real check - PEAK never lit for STEREO16's
one-sample spike, because the meter read only the half sounding every three
ticks and a 93 ms half fell between two looks; the meter now takes every
half played since its last look for the hold and PEAK (§123.8). The second
run, on the build before the review's five fixes (which touch the reader and
the view, not the transport):

```
os88test: building 4 declared artefact(s): build/os8088.img build/wrfix.img build/waverack.bin build/mixercfg/SYSTEM.CFG
os88test: soak tier - 1 rows, 420s declared, budget none  (caps: lame,mtools,nasm,nasm3,numpy,pil,qemu,qemu9,unicorn)

 ok  waverackplay                 136.6s
       | waverackplay: rates - an SB16 (PCM_HI), captured at 22,222 Hz
       |    the running WaveRack is build/waverack.bin (make waverackfix) ok
       |    WaveRack launches off B:                                     ok
       |      heap, idle, no document: [10] KB
       |    STEREO16.WAV opens                                           ok
       |      heap, idle with it open: [4, 6, 10, 16] KB = 36 KB
       |    STEREO16: d = 1 at 22,050 Hz (SND_CAP_PCM_HI)                ok
       |    STEREO16: the status said 'Out 22,050 Hz mono'               ok
       |    STEREO16: the meter's caption was LEVEL  PLAY (wr_mwho 2)    ok
       |    STEREO16: the meter lit 22 cells for its tone (wrart_db[93]) ok
       |    STEREO16: PEAK lit at R's spike (32767, a rail)              ok
       |      heap, playing: [4, 6, 10, 16, 32] KB = 68 KB
       |    STEREO16: ended IDLE, no underrun, 'End of the sound'        ok
       |    STEREO16: every claim given back (heap [4, 6, 10, 16] KB)    ok
       |    STEREO16 at 75%: found in the capture                        ok
       |    STEREO16 at 75%: every sample                                ok
       |  loop: a selection dragged in the waveform, looped
       |    the drag selects [7488, 16960): columns 117..264 at fpp 64   ok
       |    the cursor stands at s                                       ok
       |    paused after 4.8 laps: 45728 samples played (verb 9)         ok
       |    the region and the lap: sp 7488 e1 16960 = [s, e)            ok
       |    the paused position 15328 = wrsim.play_pos(45728 x 1) = 15328 ok
       |    the capture holds the selection REPEATED, every sample: 4 whole laps of 9472 ok
       |    Space resumes it                                             ok
       |    Stop: the playhead back where the play began (7488)          ok
       |    Stop: the cursor and the selection where they were           ok
       |    Stop: every claim and the grant back (heap [4, 6, 10, 16] KB) ok
       |    TONE8.WAV opens                                              ok
       |    TONE8.WAV: 11025 Hz on the card (d = 1), status 'End of the sound' ok
       |    TONE8.WAV: the capture's tone 1007.9 Hz, want 1007.8 (+-2%)  ok
       |    TONE8.WAV: 1.044 s long, want 0.992 (+-0.06)                 ok
       |    TONE8K.WAV opens                                             ok
       |    TONE8K.WAV: 8000 Hz on the card (d = 1), status 'End of the sound' ok
       |    TONE8K.WAV: the capture's tone 499.8 Hz, want 500.0 (+-2%)   ok
       |    TONE8K.WAV: 1.008 s long, want 1.000 (+-0.06)                ok
       |  close: the window closed mid-play
       |    STEREO16.WAV opens                                           ok
       |    the window closes                                            ok
       |    no claim of the closed region left ([])                      ok
       |    the running WaveRack is build/waverack.bin (make waverackfix) ok
       |    WaveRack launches again                                      ok
       |    STEREO16.WAV opens again                                     ok
       |    the card opens again - the old stream was closed (Out 22,050 Hz mono) ok
       | waverackplay: d2 - a DSP 2.01 (no PCM_HI), captured at 22222 Hz
       |    the running WaveRack is build/waverack.bin (make waverackfix) ok
       |    WaveRack launches off B:                                     ok
       |    TONE44.WAV opens                                             ok
       |    VOLUME 50% from the keys (wr_vol 50)                         ok
       |    TONE44: d = 2 at 22,050 Hz (the ceiling 22,222)              ok
       |    TONE44: the status said 'Out 22,050 Hz mono /2'              ok
       |    TONE44: ended, no underrun                                   ok
       |    TONE44: 12288 samples played (verb 9) - the frames / 2, padded to a half ok
       |    TONE44 at d = 2, 50%: found in the capture                   ok
       |    TONE44 at d = 2, 50%: every sample                           ok
       | waverackplay: ok

os88test: 1 passed, 0 failed, 0 skipped in 136.6s, 43.9s charged (budget none)
rc=0
```

## Wave 4 — editing and saving (US-09, US-10): what shipped and what was seen

Built: `apps/waverack/wredit.inc` (`wx_`, §123.3 and §123.9: the piece-list
builder with coalescing, the 64-piece refusal, the 9-state undo ring in one
6 KB claim, the 16-piece clipboard kept across New and Open, the source
sweep, the format refusal and the adoption by an empty document);
`apps/waverack/wrsave.inc` (`ws_`, §123.10: `WRSAVE.$$$`, DFREE first, the
canonical header, a gulp a tick through a 16 KB output claim -
`OSAPI_FILE_WRITE`, then `OSAPI_FILE_WRITE_SEQ` - Escape cancels, delete +
rename, `Replace NAME?`, the rebase over a source); the dirty title
(`OSAPI_WM_TITLE`) and the save-changes question (os88ui's alert) in
`waverack.asm`; the predicate's and the reasons' wave 4 rows;
`tools/wrsim.py`'s `pc_add`, `pc_range`, `Editor`, `wav_header`,
`write_wav` with a selfcheck against a flat model (fast `wrsim` row); the
soak row `waverackedit` (`tests/waverackedit.py`).

**The open item "the dirty indicator is not on screen" is resolved**: the
indicator is the window title, `WaveRack - NAME*` (TexPad's shape), set
only when it changes (§123.3). US-04's acceptance line about a dirty
indicator is met by it.

Measured: image 35,953 bytes (packed 27,915, 28 clusters), bss 5,997; heap
idle with a document and a history `[2, 4, 6, 10, 16]` = 38 KB (TONE8.WAV),
saving `[2, 4, 6, 10, 16, 16]` = 54 KB; an edit costs 34-37 drawing calls at
Fit, 81-84 at fpp 1 with the refinement finishing, against 126 for a full
repaint (`-DWR_CALLS`). The apps disks still fit (1.44 MB 2,574 of 2,847
clusters, 1.2 MB 2,081 of 2,371); nothing was moved.

Seen on the glass (QEMU): cut, paste, undo and redo on TONE8.WAV and
STEREO16.WAV; the dirty title; the save-changes alert on New and on the
close box (Cancel keeps it, Discard closes it, Save saves and then closes,
leaving no claim); Save As a new file read back off the floppy byte for
byte; a Save As cancelled with Escape leaving STEREO16.WAV byte-identical;
Save over the source rebasing; Save Selection As over the document's source
refused before the replace question; the replace question (Cancel, then
Proceed); Paste of another format refused naming the clipboard's; New +
Paste adopting the format as UNTITLED.WAV and Save going to Save As. The
identity check after a cut at a non-block boundary: **`None`** at Fit and
at fpp 1. Screenshots: `docs/waverack/waverack-w4-edit-col.png`,
`-edit-bw.png`, `-ask.png`, `-cga-compact.png` (native 640 x 200, the CGA
knob tree) and `-cga-compact-2x.png`.

**The soak row's first run failed six checks in its rebase leg, and the
cause was the TEST's floppy**: `MEDIA/` was built one cluster (16 slots: `.`,
`..`, twelve fixtures), the row's OUT1.WAV and SEL1.WAV filled it, and the
save over STEREO16.WAV was refused `Folder is full` - the kernel never grows
a directory (§18.5). WaveRack behaved as §123.10 says (the reason said, the
original untouched, the document still edited). The row now builds
`MEDIA/` with `--dir-slots MEDIA=64`; the second run passed (below). It ran
on the build with wave 3's review fixes; the one change after it - a stale
`No selection` cleared when a selection is made - touches only the status
line, and was seen on the glass instead.

**Wave 3's review, folded in** (`wr-reports/review-wave3.md`): H1 the end
padded to the card's 4 KB block above 22,222 Hz (and `waverackplay` gained
a `hi` leg - TONE44.WAV at d = 1, an odd number of halves - EDITED, NOT RUN:
the final soak pass runs it); L1 the srcdone snapshot before the counters;
L2 a Loop region under a half plays once (`Too short to loop`); L3 a stale
count not folded; L4 `Sound card stopped`; L5 Resume at a non-looping
play's end finishes it; L6 the pre-roll's silence keeps converted samples;
N1, N2 comments; N4 `wt_err` cleared once said. N3 (the played count's
32-bit product wraps after ~18 h of looping) is left: documented, not worth
a 48-bit playhead.

The soak row (`python3 tools/os88test.py soak -v -k waverackedit`, the
second run, verbatim):

```
os88test: building 3 declared artefact(s): build/os8088.img build/waverack.o88 build/waverack.bin
os88test: soak tier - 1 rows, 420s declared, budget none  (caps: lame,mtools,nasm,nasm3,numpy,pil,qemu,qemu9,unicorn)

 ok  waverackedit                 500.2s
       |    the running WaveRack is build/waverack.bin (make waverackfix) ok
       |    WaveRack launches off B:                                     ok
       |    TONE8.WAV opens                                              ok
       |      heap, idle with it open: [2, 4, 10, 16] KB
       |  edits: TONE8.WAV, every edit against wrsim's Editor
       |    cut: pieces 2, sel 1280-1280, cursor 1280, history 1/2 - the reference's ok
       |    an edit makes it dirty: the title says so                    ok
       |    paste: pieces 4, sel 3840-4832, cursor 3840, history 2/3 - the reference's ok
       |    cut: pieces 2, sel 3840-3840, cursor 3840, history 3/4 - the reference's ok
       |    paste: pieces 1, sel 1280-2272, cursor 1280, history 4/5 - the reference's ok
       |    a cut pasted back where it came from is one piece again      ok
       |    delete: pieces 2, sel 6400-6400, cursor 6400, history 5/6 - the reference's ok
       |    undo: pieces 1, sel 6400-7392, cursor 6400, history 4/6 - the reference's ok
       |    redo: pieces 2, sel 6400-6400, cursor 6400, history 5/6 - the reference's ok
       |    copy: pieces 2, sel 480-832, cursor 480, history 5/6 - the reference's ok
       |    paste: pieces 3, sel 9600-9952, cursor 9600, history 6/7 - the reference's ok
       |    undo: pieces 2, sel 9600-10033, cursor 9600, history 5/7 - the reference's ok
       |    undo: pieces 1, sel 6400-7392, cursor 6400, history 4/7 - the reference's ok
       |    redo: pieces 2, sel 9600-10033, cursor 9600, history 5/7 - the reference's ok
       |    trim: pieces 2, sel 0-0, cursor 0, history 6/7 - the reference's ok
       |    undo: pieces 2, sel 3200-10033, cursor 3200, history 5/7 - the reference's ok
       |      heap, idle with a history: [2, 4, 6, 10, 16] KB
       |  save as: OUT1.WAV and SEL1.WAV read back off the floppy
       |    OUT1.WAV is wrsim.write_wav of the pieces, byte for byte (10078) ok
       |    no WRSAVE.$$$ left                                           ok
       |    TONE8.WAV untouched                                          ok
       |    the document is OUT1.WAV and clean ('WaveRack - OUT1.WAV')   ok
       |    'Saved OUT1.WAV'                                             ok
       |      heap, saving: [2, 4, 6, 10, 16, 16] KB = 54 KB
       |    SEL1.WAV is [800, 1456) byte for byte                        ok
       |    the document is still OUT1.WAV                               ok
       |  cancel: a Save As of STEREO16 stopped with Escape
       |    STEREO16.WAV opens                                           ok
       |    the clipboard names STEREO16.WAV                             ok
       |    'Save cancelled'                                             ok
       |    no OUT2.WAV, no WRSAVE.$$$                                   ok
       |    STEREO16.WAV byte-identical                                  ok
       |    the save's claim given back, the document still edited       ok
       |      heap, saving (sampled before the Escape landed): [4, 6, 6, 10, 16] KB = 42 KB
       |  rebase: File > Save over STEREO16.WAV, the file it reads
       |    STEREO16.WAV is the edited document byte for byte            ok
       |    the document is ONE piece of the saved file ([(1, 0, 16866)]) ok
       |    the history is empty, its claim given back                   ok
       |    the clipboard's pieces of the old file dropped               ok
       |    'Saved; undo cleared'                                        ok
       |    clean: 'WaveRack - STEREO16.WAV'                             ok
       |  full: a document bigger than the disk's free space
       |    refused before writing: 'Not enough disk space' (404784 bytes) ok
       |    nothing on the disk changed                                  ok
       |  pieces: a one-column clip pasted until WO_NPC would pass
       |    refused at 63 pieces: 'Too many edits; save'                 ok
       |  close: the close box on an edited document
       |    'Save changes to TONE8.WAV?' asked                           ok
       |    Discard closes the window                                    ok
       |    no claim of the closed region left ([])                      ok
       | waverackedit: ok

os88test: 1 passed, 0 failed, 0 skipped in 500.2s, 84.5s charged (budget none)
```

## Wave 4b — play through disk reads (US-05 hardening): what shipped and what was measured

**The defect** (the owner, on 86Box): the play goes silent whenever the disk
is read. `kernel/disk.inc` holds `[sch_lock]` across every `int 13h`, so no
task switches while anything reads - the worker cannot convert - and the
card plays alone from what is staged: wave 3's 16 KB grant, 0.74 s at 22 kHz,
behind up to eight back-to-back 16 KB `READ_AT`s that each walked the FAT
chain from the file's front, with nothing making the ring full first. QEMU's
floppy is instantaneous, so no QEMU row could see it. **The owner's
priority: WaveRack reads from hard disks more than floppies** - so every
size below is a fixed disk's first.

**What shipped** (SPEC.md §123.6's last part, §123.13's wave 4b table):

- [x] **Our own external ring** - 32 KB (16 KB tier) + its words in an
  `OSAPI_MEM_CLAIM_DMA_HI` claim, verb 0 `SND_OPENF_EXT`; a half fed by a
  copy and one word; verb 1 resumes a paused card once a whole block is
  queued, asked every pass. The grant and verbs 6/1 remain only for a card
  that refuses an external ring (a single-cycle DSP 1.x) - a deviation from
  the brief, which said they go away: without it a period XT's own card,
  the SB 1.x, would have lost playback.
- [x] **Convert before you read** - the UI tops the ring up, then reads
  ONE bounded gulp, then again; the worker stays for the time the UI task is
  someone else's.
- [x] **Reads bounded per medium** - 16 KB fixed; 12 KB / 9 KB floppy (a
  cylinder's worth, not the brief's track: a call costs a call, and a 4 KB
  read would have cost a 360 KB floppy its 11 kHz play).
- [x] **READ_SEQ** for the play, the scan and the save, a cursor per source,
  with the slide that keeps it sequential when the data offset is not a
  multiple of the block align.
- [x] **The hold** - measured medium + an 8088's measured conversion
  cycles against an 80% budget; `Loading 45%`, Escape cancels; one byte a
  frame; `Disk too slow: gaps` / `CPU too slow: gaps` when neither.
- [x] **The d = 1 8-bit loops** - 114 cycles a frame against the general
  loop's ~227, which could not keep 22 kHz up on an 8088 even from memory.
- [x] **The meter's 32 slots** (a 32 KB ring runs 16 halves ahead).
- [x] SPEC §123.6 / §123.13, this section, the diary.
- [ ] **Heard on 86Box or iron.** No `vm/` profile has a hard disk AND a
  Sound Blaster on an XT: `vm/xt-mfm` (the ST-225 on a Xebec, the machine to
  install on) would need `sndcard = sb2.0` added; `vm/286-video` (mr286, IDE,
  SB16) has both and can be listened on once WAVERACK.O88 and a WAV are on
  its VHD. Not edited here.

**Folded into the wave afterwards, at the orchestrator's word:**

- [x] **Clusters to 32 KB** (FAT16 past 512 MB uses 16 KB, past 1 GB 32
  KB, and Wave 4 refused anything over 8 KB: most real hard disks). The I/O
  claim became a WINDOW on the file, sized max(16 KB, the largest live
  source's cluster) + 1 KB; a read is whole clusters, one at least; the
  header reader takes one cluster a read (two across a cluster's end); the
  save's output claim is max(16 KB, the destination's cluster).
  `tools/os88hdd.py --spc` formats the 321 MB VHD of 32 KB clusters the
  `bigclu` leg runs on, on MartyPC.
- [x] **Wave 4's review** (`review-wave4.md`: 2 high, 2 medium, 8 low, every
  one verified in the code first and fixed): the per-instance temp name
  that never replaces a file (H1, M1), the questions' greying with the
  re-front and the answers' re-checks and the close's cancel (H2), the
  upper-case source names (M2), and L1-L8 (SPEC.md 123.10's last part).
  `waverackedit` gained the `temp` and `asked` legs - edited, NOT run (the
  final pass runs it); H1's first path, a rename failing after the delete,
  is not drivable here.
- [x] **Found on the way**: a refined column spanning two reads lost its
  first part to a draw in between ([we_racc] shared with we_range; Wave 4's
  build has it too) - `ODD46.WAV`, data at byte 46, joins the fixtures and
  found it; the tick's top-up bounded to two halves so the UI stays the
  user's.

**The measurement** - soak row `waverackdisk`, its final run (all legs,
both builds, verbatim; the runs before it found three TEST bugs - a stale
`[fm_vinst]`, a window computed before a long launch, an open waited on
mid-way - and one PACKAGE bug, the header reader's two 32 KB clusters, each
fixed before the run below):

```
 ok  waverackdisk                 266.8s
       | waverackdisk: hdd before - .../WAVERACK.O88 on XT-IDE + SB 2.0
       |      before BIG22 scan: 93.8 s guest, int 13h {'fat': 18, 'dir': 2, 'data': 168}
       |      before BIG22 (2 MB, 22 kHz)    30.3 s: silent 17.67 s,   0 underruns,  280820 samples
       |      before BIG22 + another's scan  40.0 s: silent 26.91 s,   5 underruns,  291294 samples
       |      before (the second WaveRack's launch and Open took 124.2 s under the play)
       |      before ST22S (4 s, 88 KB/s)     3.3 s: silent  2.62 s,   2 underruns,   14688 samples
       |      before ST22L (20 s, 88 KB/s)   15.1 s: silent 11.76 s,   2 underruns,   73482 samples
       | waverackdisk: hdd after - build/waverack.o88 on XT-IDE + SB 2.0
       |      after  BIG22 scan: 77.7 s guest, int 13h {'fat': 18, 'dir': 1, 'data': 168}
       |      after  BIG22 (2 MB, 22 kHz)    30.2 s: silent  0.00 s,   0 underruns,  671157 samples, held=0 slow=0 rdsz=16384 ext=1
       |      after  BIG22 + another's scan  40.3 s: silent  0.00 s,   0 underruns,  897696 samples, held=0 slow=0 rdsz=16384 ext=1
       |      after  (the second WaveRack's launch and Open took 106.3 s under the play)
       |      after  ST22S (4 s, 88 KB/s)     3.3 s: silent  0.00 s,   0 underruns,   75962 samples, held=1 slow=0 rdsz=2048 ext=1
       |      after  ST22L (20 s, 88 KB/s)   15.0 s: silent 10.18 s,  24 underruns,  107894 samples, held=0 slow=2 rdsz=16384 ext=1
       | waverackdisk: hdd, before -> after
       |    BIG22 scan: guest seconds 93.8 -> 77.7, no slower            ok
       |    BIG22 scan: FAT reads 18 -> 18                               ok
       |    BIG22 after: streamed (held 0), the external ring (1)        ok
       |    BIG22 after: no underrun in 30 s (before: 0, 17.67 s silent) ok
       |    ST22S after: HELD - 'Loading' said, read from memory         ok
       |    ST22S after: no underrun (before: 2, 2.62 s silent)          ok
       |    ST22L after: 'CPU too slow: gaps' said, the gaps counted (24 underruns, 10.18 s silent) ok
       |      another window reading: before 5 underruns / 26.91 s silent, after 0 / 0.00 s (its scan still running at the end: True)
       | waverackdisk: floppy before - LONG.WAV off a 1.44 MB floppy
       |      before LONG scan: 108.1 s guest
       |      before LONG (1.1 MB, 11 kHz)   30.4 s: silent 18.71 s,   1 underruns,  130080 samples
       | waverackdisk: floppy after - LONG.WAV off a 1.44 MB floppy
       |      after  LONG scan: 85.4 s guest
       |      after  LONG (1.1 MB, 11 kHz)   30.2 s: silent  0.07 s,   0 underruns,  335018 samples, held=0 slow=0 rdsz=12288 ext=1
       |    LONG after: no underrun in 30 s off the floppy (before: 1, 18.71 s silent) ok
       | waverackdisk: bigclu before - a FAT16 volume of 32 KB clusters
       |      before open BIG22: 'Cluster too large', 0 frames
       | waverackdisk: bigclu after - a FAT16 volume of 32 KB clusters
       |      after  open BIG22: 70.0 s, cluster 32768, I/O claim 33 KB, heap (see the claim map)
       |      after  BIG22 (32 KB clusters)  15.1 s: silent  0.00 s,   0 underruns,  335656 samples, held=0 slow=0 rdsz=16384 ext=1
       |    32 KB clusters before: refused ('Cluster too large')         ok
       |    32 KB clusters after: BIG22 opens (2094750 frames), cluster 32768, I/O claim 33 KB ok
       |    32 KB clusters after: 15 s played, no underrun (0.00 s silent) ok
       |    32 KB clusters after: OUT32.WAV is wrsim.write_wav of MID11, byte for byte (99270), 'Saved OUT32.WAV', a 32768-byte output claim ok
       |    32 KB clusters after: no temp file left (['KERNEL.SYS', 'SOUND.DRV', 'WAVERACK.O88', 'BIG22.WAV', 'MID11.WAV', 'OUT32.WAV']) ok
       | waverackdisk: ok
os88test: 1 passed, 0 failed, 0 skipped in 266.8s, 261.4s charged (budget none)
```

**Before's `0 underruns` beside 17 s of silence** is Wave 4's counter, which
saw an underrun only when a poll caught the stream paused; the silence -
verb 9's count against the guest clock - is the measure that cannot miss
one, and is why the row asserts on it.

**The case for a desktop interrupt-time feeder** (the brief asked for it if
buffering were not enough): on these measurements buffering IS enough for
WaveRack's own reads - 0.00 s and 0.09 s of silence - and what is left is
(a) another program's long disk work, which the 32 KB ring covers for 1.49 s
at 22 kHz, and (b) an 8088's CPU. A feeder at interrupt time would answer
(a) only for a ring that is already FULL - it moves bytes, it cannot read
the disk under `[sch_lock]` either - so it buys exactly what a bigger ring
buys, and costs a resident hook. It is not argued for here. What would earn
one is a measurement of a real ST-225 copy in another window outlasting the
ring.

**Decisions taken on numbers taken once** (CLAUDE.md's exception, by the
agent choosing): the conversion's cycles a frame (`cyc`: 114 / 205 / 276 /
456 / 413 on MartyPC's 5150), which sized the plan's CPU term and showed the
general loop could not keep 22 kHz up; and one calibration play of BIG22
and BIG11 off the VHD (0 underruns each, WaveRack's own 16 KB reads timing
148 KB/s on `os8088_5150_herc_hdd_sb_gla` - where docs/MARTYPC-DEBUG.md's
figure for `os8088_xt_hdd`'s XT-IDE is 38 KB/s read, the figure this wave
was planned against), which moved ST22S from "streams if the disk keeps up"
to "held because the CPU cannot".

## Wave 5 — capture (US-06, US-07's capture half): what shipped and what was measured

**What shipped** (SPEC.md §123.7, §123.8's capture half, §123.12, §123.13's
wave 5 table):

- [x] **Record / Pause / Stop** through `wr_cmdok`, keys, menu and keyboard;
  a take greys everything but Stop, Pause, Record, the theme and the layout
  (`Stop the take first`); Record asks about an edited document first.
- [x] **The take file**: `VOICEnnn.WAV`, DFREE first (`B: 312 KB free - 28
  s`, MB and minutes past 9,999 KB and 999 s, ` free` dropped past 22
  characters), the one-cluster header (`JUNK` to the cluster), whole-cluster
  appends, the header cluster patched with `OSAPI_FILE_WRITE_AT` at Stop.
- [x] **The worker copies, the UI writes** (a change from the pinned design,
  §123.7): ring -> staging units through the gain table on the play's
  worker every tick; the UI writes full units. **The first build copied in
  the UI's tick alone and overran at 0:13** when a second WaveRack opened:
  the UI task was gone 3.4 s for its paint and 4.7 s for a menu held down.
- [x] **Staging units**: max(16 KB, a cluster) on a fixed disk, 32 KB on a
  floppy, 64 KB in all - a single-producer, single-consumer queue whose
  shared count changes by one `inc`/`dec`.
- [x] **Pause/resume** continuous (the soak row: frames = the two streams'
  counts summed); **Stop** opens the take clean.
- [x] **Disk full, write error, overrun, no input, card stopped** each with
  its sentence; a take with nothing on the disk is deleted, the empty
  document redrawn.
- [x] **WAVERACK.TAK** in `\SYSTEM\APPDATA` (state, place, recorder's
  segment), marked done at Stop; **File > Recover Take** at launch unless
  the take is live in another window (`OSAPI_SYS_SNAPSHOT`), re-read before
  acting.
- [x] **INPUT GAIN (DIGITAL)**: 13 Q8 steps from `wrsim.gain_q8`, the
  saturating table in the ring claim, live during a take.
- [x] **The record meter** (`LEVEL  REC`) and **the live strip** (a column a
  1,024 frames, only the new ones drawn).
- [x] **15,000 Hz** (verb 4's top on a DSP 2.00+) as a third rate.
- [x] **A DSP 1.x is refused** (`Needs a DSP 2.00+ card`), not given the
  pinned short take; **a floppy takes 8,000 Hz** (`Floppy: use 8,000 Hz`).

**The four questions** (the brief's), answered with numbers:

1. **Clusters vs. the ring**: the ring is never the write's unit - the
   staging units are, max(16 KB, a cluster), so a 32 KB cluster is written
   from staging. The one-cluster header stands at 32 KB on a 32 KB-cluster
   volume (a 32 KB patch at Stop, 0.37 s once).
2. **Write time vs. the ring**: XT-IDE 16 KB 0.227-0.247 s, 32 KB 0.369 s
   (taken once, MartyPC); 1.44 MB floppy 16 KB 1.734 s, 32 KB 2.41-2.95 s
   (traced). The ring (2.90 s at 11,025 Hz) must outlast a write's block
   and the writes must keep up: a fixed disk does both at both rates, a
   floppy only at 8,000 Hz.
3. **The CPU**: the copy **92.3 cycles a byte** - 21.5% of an 8088 at
   11,025 Hz, 29.3% at 15,000; the XT-IDE's CPU-driven writes another 16.8%
   / 22.8%; about 62% / 48% left.
4. **The live strip**: 1.34 ms a tick (median), 24-44 ms at a page's clear.

**Measured by `waverackrec`** (MartyPC, the final full run; the floppy leg
re-run alone on the final build): XT-IDE 2 KB clusters 60 s at 11,025 Hz
(high water 3,072 bytes) and 15,000 Hz (4,608); 203 s at 11,025 Hz while a
second WaveRack launched, opened and scanned 2 MB (8,704); a VGA XT on 32 KB
clusters 60 s at each rate (5,120, 6,656); 1.44 MB floppy 30 s at 8,000 Hz
(27,648) then disk full with the take kept; 0 overruns; every take read back
against `wrsim.take_header` with frames = the card's count; a take killed
with the machine recovered dirty on the next boot. QEMU: `No input from the
card`, no file left; 11,025 Hz onto its floppy refused.

**Not here**: a real microphone's content, the gain heard, a real disk's
write speed (MartyPC's XT-IDE has no mechanical model; its floppy is the
only timed medium here).

## Wave 6a — acceptance paperwork (US-11, US-12)

### US-11: the numbers, and where each comes from

Every figure below was measured in Waves 1-5 (the reports are kept beside
the PR's work, `wave*-impl.md`; SPEC.md §123.13 carries the sizes and heap
per wave). "Final pass" names the row and leg that produces a number not
yet taken on the target.

| item | measured | source | final pass |
|---|---|---|---|
| **image** | **43,625** bytes after wave 6b (packed 34,249: 34 clusters at 1 KB; wave 5 42,999); the plan's estimate was 40 KB | the build, wave 6b (§123.13) | - |
| **bss** | **7,774** bytes after wave 6b (wave 5 7,706; estimate 4 KB): 1,280 of it the live strip's page, 2 KB the conversion half, 800 the piece table, 84 the marker table | the build, wave 6b | - |
| **art** | 68 pictures, 3,929 bytes, three digit sets, the 5x7 caps, the logo, the icon, the 129-byte dB table, generated by `tools/os88wrart.py` | its build output; `--selfcheck` | the fast `wrsim` row runs the art selfcheck every `make` (6a) |
| **heap, idle** | 10 KB (the band); a document open 31-38 KB (`[4, 6, 10, 17]` with the I/O claim), up to 55 KB on 32 KB clusters | QEMU claim map, waves 2 and 4b (`waverack` row) | `waverack` |
| **heap, editing / saving** | + the history 6 KB (most 68 KB); saving + the output 16 KB (54 KB for TONE8 edited, most 84) | `waverackedit`, wave 4 | `waverackedit` |
| **heap, playing** | + look-ahead 32 + ring 33: STEREO16 102 KB, LONG.WAV 99 KB; held + one byte a frame (STEREO16 124 KB) | QEMU claim map, wave 4b | `waverackplay`, `waverackdisk` |
| **heap, recording** | 10 + ring 33 + staging 64 = **107 KB** on 2 KB and 32 KB clusters | MartyPC claim map, wave 5 (`waverackrec`) | `waverackrec` |
| **draw calls a tick** | QEMU VGA by hand (waves 3-4): idle 0; playing at Fit 1.70, at 256 frames a column 3.08; a selection drag 5-6 a move; an edit at Fit 34-37; a full repaint 120-126. **The 5150 + Hercules (final pass, `waverackrec` `calls`): idle 0 (188 ticks); recording 0.99 a tick; playing the take at Fit 1.02; Select All 7; one cut 16** | `-DWR_CALLS` builds | done |
| **redraw time** (the 5150, Hercules) | the live strip 1.34 ms a tick median (19-44 ms at a page's clear); **`wr_redraw` while recording 60.4 ms a tick median** (107 max: the time element's big digits are most of it); **a full repaint 1,122 ms (COMPACT, 77 primitives) and 1,459 ms (MID, 90)** | MartyPC breakpoints, `waverackrec` (wave 5 and the final pass) | done |
| **conversion cycles** (play) | a frame: 8-bit mono d = 1 **114**, 8-bit stereo 205, 16-bit mono 276, 16-bit stereo **456**, 8-bit mono d = 2 413 - 53% of an 8088 at 22,050 Hz 8-bit mono; 16-bit stereo 2.1x the machine (held) | MartyPC cycle counts, wave 4b (taken once, design-deciding) | - |
| **copy cycles** (record) | **97.8** a byte with wave 6b's count re-read after every chunk (92.3 before it): 22.8% of an 8088 at 11,025 Hz (the card's 11,111), 31.0% at 15,000; the XT-IDE's CPU-driven writes 16.8% / 22.8% more | MartyPC breakpoints, `waverackrec` (final pass) | done |
| **sustained play** | final pass: BIG22 (2 MB, 22 kHz) off XT-IDE **0.00 s silent of 30.4**, 0 underruns (before 4b: 16.99); with a second WaveRack scanning **0.14 of 40.5** (27.35); LONG.WAV off a 1.44 MB floppy **0.00 of 30.4** (18.60); ST22S held, 0 underruns; ST22L 'CPU too slow: gaps' (22 counted) | MartyPC, `waverackdisk` (before/after) | done |
| **sustained record** | final pass: XT-IDE 60 s at 11,025 and 15,000 Hz, 0 overruns, ring high water 3,072 / 4,096 bytes of 32,256; 32 KB clusters (VGA XT) 5,120 / 6,656; 204 s beside a second WaveRack's launch, Open and 2 MB scan **16,384** (wave 5: 8,704 - the copy's re-check makes the worker's pass longer); 1.44 MB floppy 8,000 Hz 30 s 27,648 (11,025 refused there); disk full kept | MartyPC, `waverackrec` | done |
| **disk writes** | XT-IDE 16 KB 0.227-0.247 s, 32 KB 0.369 s; floppy 16 KB 1.734 s, 32 KB 2.41-2.95 s | MartyPC, wave 5 (taken once / traced) | - |
| **scan times** | final pass: BIG22 (2 MB) on XT-IDE **77.8 s** (before 4b: 98.8; int 13h 18 FAT, 1 dir, 169 data); LONG.WAV (1.1 MB) off a 1.44 MB floppy **85.1 s** (113.1) | MartyPC, `waverackdisk` | done |
| **readable** | every status message at most 22 characters (135 in the source, longest 22); every 5x7 caption has its glyphs; the layouts' controls and captions fit (three layouts, measured); FULL, MID, COMPACT and both themes looked at on VGA, EGA, CGA (640 x 200) and Hercules (MartyPC); the identity check (incremental against full repaint) `None` | the art selfcheck (fast `wrsim` row, 6a); screenshots waves 1-5 | `waverack` (identity), the final look |

What the target-speed numbers do not say: MartyPC's hard disk has no
mechanics and its floppy is the only timed medium; QEMU's draw-call counts
are exact and its milliseconds are not. A real XT's disk, a real
microphone and the sound itself are hardware (US-12's last table).

### US-12: build, install and test it from the PR

**Build** (`make deps` once on a new box):

```
make                         # every image; WAVERACK.O88 on the 1.44 MB and 1.2 MB apps disks (APPS/)
make waverack                # the package alone (build/waverack.o88)
make waverackfix             # build/wrfix.img: WaveRack and every wrsim fixture WAV in MEDIA/
make waverackhd              # build/WAVERACK-MFM.img: the whole install on an ST-225 + long WAVs in MEDIA/
make xt-waverack             # 86Box: a 4.77 MHz XT, that disk on a Xebec, a Sound Blaster 2.0 on IRQ 7
make KERN_SMALL=1 BUILD=...  # kern_small leaves WaveRack off (SMALLOMIT: no SOUND.DRV there)
```

**The disks that carry it**: the 1.44 MB and 1.2 MB apps disks, the
everything set (`make allapps`) and the live media (`make live`). Not the
720 KB apps disk (full, §24.6.1's 2026-10-25 note) and **no 360 KB disk**:
§123.13's 360 KB note says what does not fit where and leaves the choice to
the owner. An XT reaches it through a hard disk: `make waverackhd`.

**The machine to listen on**: `make xt-waverack`. It is `vm/xt-mfm`
(an ibmxt86 at 4.77 MHz, 640 KB, VGA, an ST-225 on a Xebec) with
`vm/xt-sound`'s Sound Blaster 2.0 added - **on IRQ 7, because the Xebec
owns IRQ 5** and SOUND.DRV refuses a card there on an XT (§34.5.6) - and A:
empty so it boots `build/WAVERACK-MFM.img`. On it: open `MEDIA\BIG22.WAV`
or `LONG.WAV` from `APPS\WAVERACK.O88` and play; open a second WaveRack
and another WAV while it plays (the stall the owner heard is gone); record
onto C: at 11,025 or 15,000 Hz and play the take back. The disk runs at 86Box's
`ST225` speed preset (`vm/pc5150`'s, where `xt-mfm` has `ramdisk`): real
seek and rotation times, which is where the owner first heard the stall -
`ramdisk` would reproduce only its CPU half. The host cannot key 86Box: this
machine is for listening, never a gate.

**The test matrix** - every row that covers WaveRack:

| row | tier | emulator | what it proves |
|---|---|---|---|
| `wrsim` | fast | - | the reference reader, envelope, view-cache columns, play positions, editor, writer and take header against Python's `wave` and a flat model; the gain table against 20 log10; the art selfcheck (pictures, layouts, small caps, status lengths) |
| `waverack` | soak | QEMU | every fixture opened on the guest, the parse and the view cache read back by symbol against `wrsim`; zoom to one frame a column; the identity check |
| `waverackplay` | soak | QEMU (SB16) | playback captured sample for sample against `wrsim` at three rates and at d = 2 on a DSP 2.01; loop; close; 44.1 kHz wide |
| `waverackedit` | soak | QEMU | every edit's piece table, selection and history against `wrsim`'s `Editor`; saves read back off the floppy byte for byte; cancel, rebase, disk full, the close question, the temp name, the questions' greying |
| `waverackdisk` | soak | MartyPC | play through disk reads, BEFORE and AFTER wave 4b: silence and underruns off XT-IDE and a floppy, a second window reading, 16-bit stereo held, 32 KB clusters opened, played and saved |
| `waverackrec` | soak | MartyPC + QEMU | takes on XT-IDE (two cluster sizes) and a floppy read back against `wrsim.take_header`, frames = the card's count, 0 overruns, pause, a second window, disk full, Recover after a kill, no input; **`calls`** (6a): US-11's draw calls and repaint time |
| `sndcap` | soak | MartyPC | the driver half: verb 4 into an external ring (§34.6.4) |
| `sbirq5` | soak | unicorn (host) | the IRQ 5 refusal that decides the listening machine's jumper |

**What no emulator here can prove, and is pending on hardware**: a real
microphone's sound in a take and the input gain heard; the play heard on a
real XT's ST-225 (MartyPC's hard disk has no mechanics; 86Box's `ST225`
preset is a model of one); a real floppy's write speed, which decides whether the 8,000 Hz
floppy rule is right; a Sound Blaster 1.x's refusal on a real card. The
5150 in docs/FIELD-MACHINES.md is the machine for the first three.

**Final gates**, run once after wave 6b's review fixes: the soak union
(`sndcap`, `waverack`, `waverackplay`, `waverackedit`, `waverackdisk`,
`waverackrec`) - three passed, three failed on harness bugs, fixed and
re-run together (3 of 3) - and `test-full`: 71 passed, `megahigh` failed
(WaveRack had never declared its kern_mega class bits; exempted with
reasons in `tests/megahigh.txt`) and passed alone, `ctoolchain` skipped (no
`cc`). The guide's three pictures were taken on MartyPC's VGA XT.

## Wave 6b — the review of 4b and 5, and the final pass

**The review** (`review-w4b5.md`: High 1, Medium 2, Low 9) was checked
against the code and every finding stood:

- [x] **H1 + M2** - the recovery marker named the recorder by its segment,
  which a region move changes, and one marker served every window (a second
  window's refused Record wiped the first's protection): now four entries
  keyed by the INSTANCE SLOT, read-modify-written; a live take is never
  offered; a save onto it is refused (`Take still recording`); Recover
  clears its entry only after the open succeeds (§123.7).
- [x] **M1** - the ring's count is read again after every chunk, and a chunk
  the card began a block over while it was copied is discarded. No row can
  FORCE that stall: MartyPC's breakpoints stop the whole machine, the card's
  DMA with it, so a copier stalled while the card runs cannot be staged; the
  re-check runs on every chunk of every take, and the final pass's 0 overruns
  are its no-false-positive evidence.
- [x] **L1-L7, L9** fixed (§123.7 lists them); **L8** not needed - the card
  counts whole 512-byte blocks, so the data is never odd.

**The final pass** found no package bug. Its three failures were harness
bugs, each fixed in the test: `waverack` waited for the Open dialog behind
the "Save changes?" question a recovered (dirty) take raises since wave 4;
`waverackedit` read the source record at wave 4's 40-byte stride (64 since
4b); `waverackrec`'s new `calls` leg counted the 87 primitives of the update
after its launch as idle (no `wr_paint` among them; idle is 0 a tick after
it). `test-full`'s one failure, `megahigh`, was WaveRack's missing kern_mega
declaration, exempted with the code that would break.

**The listening machine** runs its disk at 86Box's `ST225` speed preset
(`vm/pc5150`'s): real seek and rotation, where the owner first heard the
stall.

## Known risks

- **Capture content cannot be observed here.** No emulator in this tree
  delivers a real microphone signal (QEMU: no capture IRQ; MartyPC: flat
  80h). The pipeline, the counts and the files are testable; what a
  microphone sounds like is a field run.
- **The floppy does not keep up at 11,025 Hz** - measured in wave 5: a 32 KB
  write blocks the copy 2.41-2.95 s against a ring of 2.90 s and sustains
  11-13 KB/s against 11.1, and one run overran at the first write. So a
  floppy takes 8,000 Hz (`Floppy: use 8,000 Hz`), where it held 30 s with
  the ring's high water at 27,648 of 32,256 bytes - a 14% margin, thin. A
  real drive may be faster or slower than MartyPC's; a field run on the
  5150's floppy is the measurement this needs.
- **The DMA ring must be page-safe** (§50.3), and a 32 KB head is the
  hardest claim to place; the tiers fall to 16 and 8 KB.
- **XT redraw cost.** The waveform band is the design's answer; a full
  waveform repaint is measured in US-11 against PERFORMANCE.md's table.
- **Floppy reads during refinement** hold `sch_lock`; refinement is bounded
  per pass and abandoned when the view moves.
- **Driver ABI.** The verb-4 bit is additive; the ABI lock is updated
  without a bump, and old packages are unaffected.
