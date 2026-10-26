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
| US-00 | The plan is reviewable in a draft PR before code | - | Verified | this file, SPEC §122, draft PR #311 opened at 3cde0b9a before any functional change |
| US-01 | WaveRack installs and launches like any package; menus; clean close | US-00 | Implemented — verification pending | `WAVERACK.O88` on the 1.44/1.2 MB apps disks and media360.img; launched, menus, About, closed and relaunched on QEMU VGA (relaunch face identical to the first); `make`, `make KERN_SMALL=1` green; see Wave 1 below |
| US-02 | The same WaveRack in colour and black and white | US-01 | Implemented — verification pending | `docs/waverack/waverack-vga-color-full.png`, `waverack-vga-bw-full.png`, `waverack-ega-mid.png`; identity check `None` on VGA colour, VGA B&W, EGA and across a layout round trip; Hercules MID and COMPACT seen on MartyPC's 5150 + Hercules (`docs/waverack/waverack-herc-mid.png`, `waverack-herc-compact.png`, captured from VRAM at 038ea5ed) |
| US-03 | A real 640 x 200 CGA layout with every control reachable | US-02 | Implemented — verification pending | `docs/waverack/waverack-cga-compact.png` (native 640 x 200) and `-3x.png`; all 15 controls clicked with `--screen 640x200`, each answered; the layout measurement below |
| US-04 | Open a WAV and inspect it without loading it into RAM | US-01 | Implemented — verification pending | `wrwav.inc`/`wrdoc.inc`/`wrenv.inc`; `tools/wrsim.py` reference + fast row `wrsim`; soak row `waverack` (every fixture through File > Open, bss vs the reference, the view cache column for column, zoom to fpp 1, identity); screenshots `docs/waverack/waverack-w2-*.png`; see Wave 2 below |
| US-05 | Play, pause, stop, seek and set volume at the right pitch | US-04 | Implemented — verification pending | `wrxport.inc` (§122.6 shipped): Audio's pipeline over the document, the conversion `R / d` with the box filter, VOLUME's table, Pause closing the stream, Stop restoring the playhead; `tools/wrsim.py`'s `convert`/`play_pos` in the fast `wrsim` row; soak row `waverackplay` (QEMU SB16, the capture sample for sample at 22,050 Hz and at d = 2 on a DSP 2.01); screenshots `docs/waverack/waverack-w3-*.png`; see Wave 3 below. Not seen: underruns at 4.77 MHz (US-11, MartyPC) |
| US-06 | Record from a Sound Blaster into a take that survives | US-04, driver ext | In progress | driver half shipped: 1a027a77, SPEC §34.6.4, soak row `sndcap` passed on MartyPC SB DSP 2.01 (flat 80h - not a microphone); app side is Wave 5 |
| US-07 | See the active level; set input gain separately from volume | US-05, US-06 | In progress | the PLAYBACK half shipped in wave 3 (§122.8): the played half's peak before the volume through `wrart_db`, the hold, PEAK at a rail, `LEVEL  PLAY`, only the changed cells drawn; the soak row reads the lit cells for STEREO16's tone and PEAK at its 16-bit spike. The record meter and the gain are wave 5's |
| US-08 | Select a precise range, audition it, loop it | US-05 | Implemented — verification pending | §122.9's selection half shipped in wave 3: drag, handles, Shift+click, Shift+Left/Right, Select All / None, `[s, e)` clamped, the field's two formats, the inverted span (B&W) / its own ground (colour) drawn by changed columns only; Loop seamless; the soak row drags a range, loops it and holds verb 9's count to `play_pos` and the capture to the selection repeated; identity checks with a selection on VGA colour, B&W, COMPACT and the CGA knob tree |
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
| US-04 | robust RIFF walk; frame timeline; real min/max envelopes, stereo lanes; zoom/scroll/fit/overview; bounded, cancelable scan | host fixtures from `tools/wrsim.py --fixtures` (tones, impulses, odd chunks, extensible, malformed, truncated) checked by the fast row `wrsim` against the Python reference reader and `wave`; the soak row `waverack` opening each on the guest and reading the parsed fields and the view cache back by symbol |
| US-05 | Play/Pause/Stop rules; synchronised playhead; explicit conversion; volume not in the data; EOF/underrun/errors | a soak row on QEMU's SB16 capturing the speaker/SB output (`make test-snd`-style) and checking duration and dominant frequency of a known tone at three rates; state transitions read by symbol; MartyPC for underruns at 4.77 MHz |
| US-06 | capture capability honest; take file valid; pause/resume continuity; disk full, overrun, no input; no monitoring | MartyPC SB 2.0 (flat 80h) for rate, frame count and header; QEMU for the no-input path; injected-count unit checks on the host; **real-microphone recording: pending on hardware** |
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
entry. SPEC.md §122.11–§122.13 are marked shipped with these figures.

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

Built: `apps/waverack/wrwav.inc` (the RIFF walk, SPEC.md §122.4),
`wrdoc.inc` (sources, the piece table, 32-bit arithmetic, `wo_read`, §122.3),
`wrenv.inc` (the scan, the view and its cache, refinement, the ruler, the
lanes, the overview, the cursor, §122.5); `tools/wrsim.py` (the reference and
the fixture generator); `make waverackfix` (`build/wrfix.img`, on demand);
the fast row `wrsim` (`tests/unit/t_wrsim.py`) and the soak row `waverack`
(`tests/waverack.py`). Also in this wave: a greyed key on a 1bpp face keeps
its caption solid, dithers its picture and dots its frame (§122.11).

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

Built: `apps/waverack/wrxport.inc` (`wt_`, §122.6: the state machine, the
look-ahead and its copying fill, the worker, the conversion, VOLUME, the
playhead, the meter's state); the selection model and its class-aware runs
in `wrenv.inc` (§122.9); the line, the meter's mask diff, the selection
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
§122.13; without it the disk is 335 of 354. **No 360 KB disk carries
WaveRack now.** A 360 KB home for it - a disk of its own, or a curation that
moves something else off `media360.img` - is the owner's to decide; nothing
else was moved.

Deviations, each written into §122: the pinned status sentences that do not
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
**L3** §122.5 says `ceil(frames / B)` and LONG.WAV's 1,124 blocks.

The soak row, run once at the end (`python3 tools/os88test.py soak -v -k
waverackplay`). Its FIRST run failed in the test (bytes handed to numpy as
an int list) and at one real check - PEAK never lit for STEREO16's
one-sample spike, because the meter read only the half sounding every three
ticks and a 93 ms half fell between two looks; the meter now takes every
half played since its last look for the hold and PEAK (§122.8). The second
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
