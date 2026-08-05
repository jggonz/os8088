# os8088 Tracker plan

> **Historical planning document**, kept as the design record for SPEC.md §45.
> Its two "kernel amendments" — a worker-safe stream and a file read with no
> 64KB ceiling — both landed, here as well as on the branch it was written for.
> The second has since been folded away: `dskw_readbig` and slot `0x01E8` are
> retired, and `dskw_read` itself carries the contract this document asked for
> (SPEC.md §18.4.1). Read every `readbig` below as `dskw_read`.
> Where it describes memory it describes an arena in paragraphs; this tree has
> a claim heap in KB (SPEC.md §50), which is the one correction to carry while
> reading. SPEC.md §45 is the binding description of what was built.

**Standing plan for the Tracker package (SPEC.md §45) and the two kernel amendments
it needs.** SPEC.md is the binding contract for what the kernel *is*; this document is
the design record for how os8088 gets a FastTracker-style MOD player — why each
amendment is shaped the way it is, and what is deliberately not promised. The research
that grounds every claim here lives in the session scratchpad (`research-*.md`); the
facts that survive into interfaces are restated in SPEC.md, which wins on conflict.

Lens: the tracker is the app class the sound layer was built toward ("a music player
plays … staged PCM via 0x0100 on an SB machine", SPEC §34.6) — and building it exposes
exactly two missing pieces, both additive: a worker task cannot feed a stream, and no
file API can deliver a file ≥ 64KB. Both land as SPEC amendments *before* code.

## What ships

- `apps/tracker/` — **TRACKER.O88**, "TRACKER" in the header name field. An FT2-homage
  4-channel ProTracker MOD player: launches windowed with a splash, any key or click
  enters **fullscreen** (`wm_fullscreen`, its first shipped package client), Esc
  returns. Loads `.MOD` files through the Standard File dialog, plays them through a
  ring-mode SB background stream fed by its worker task, and draws the FT2 pattern
  view: black pattern area, full-width current-row band, hex row numbers both sides,
  separator-ruled channel columns, position/BPM/speed readouts, instrument list,
  per-channel volume-bar scopes.
- **Kernel amendment 1 — worker-safe stream verbs + ring mode** (§34/§20.3/§20.6).
- **Kernel amendment 2 — `dskw_readbig`, API slot 0x01E8** (§18.4/§20.3).
- **Tooling** — `tools/mkmod.py` (deterministic 5.6KB test MOD), a data-file mode in
  `tools/os88disk.py`, Makefile wiring, `apps/tracker/beverly.mod` (Beverly Hills Cop,
  116,085 bytes, user-supplied) shipped as `BEVERLY.MOD` in the APPS folder.

## Kernel amendment 1 — worker-fed streams

Everything here is grounded in `research-sound-feasibility.md`; the hazards are real
code cites, not caution.

### 1a. The task-qualified dispatch stamp (prerequisite, fixes a latent bug)

`[snd_inst]` is one task-blind global: a worker calling any `snd_req_inst`-routed verb
while another task dispatches a callback reads the FOREIGN stamp and gets its
grant/tone/channel misattributed — the misattributed teardown then force-frees a live
grant. Fix: stamp `[sch_cur]` into the spare high byte at `snd_disp_set`;
`snd_req_inst` (and `osapi_snd_play`'s inline copy) honor the low byte only when the
high byte equals the current `[sch_cur]`, else fall to the running task's `T_INST`.
Nesting push/pop is untouched. This retroactively hardens TONE, FM and PLAY.

### 1b. Verbs 1/3/5/6/7 become any-task

- **feed (1)**: hcheck + direction + bound checks + the `sbl_total` store become ONE
  `pushf`/`cli`…`popf` window (all compares + one word store — bounded, ISR-legal).
- **status (3)**: same window around hcheck + the two loads (stale must read stale).
- **read/stage (5/6)**: the caller segment moves from the `[osapi_dseg]` global into a
  register threaded by the slot wrapper (BX is free in every verb's contract) — a
  preempted caller's segment can no longer be clobbered by the next caller's wrapper.
- **grant (7)**: alloc is already atomic; the free path's find → overlap-check → clear
  gets the same one-window treatment.
- Verbs **0/2/4 stay UI-callback-only** (open/close/open-in). The tracker opens and
  closes from W_ONKEY / the fdlg completion proc, which is natural anyway.
- New author rule: never verb-7-free a grant your worker may be mid-stage into.
- `OSAPI_SND_FM` joins the worker whitelist (safe by the same construction; verified).

### 1c. Ring mode — endless playback without a ring ABI

Linear streams are bounded: total/fed/consumed are monotonic 16-bit offsets capped at
the grant end (~52KB pool → ~4.7s at 11kHz), and a close+reopen seam is an audible
hole every few seconds. Ring mode reuses the same three counters as **free-running
16-bit values** (mod 65536), which keeps every comparison a subtraction:

- **open-out (verb 0), AH bit 0 = `SND_OPENF_RING`**: SI = grant offset, ring length
  RL = grant end − SI; RL must be a **power of two, 4096..32768** (else err 7). CX =
  initial valid total (**2048 ≤ CX ≤ RL** — a ring never pads, so the open must cover
  one whole half the DSP can start on; SPEC §34.5 rule 3). The physical offset of
  stream byte *n* is `SI + (n & (RL−1))`.
- **fills are whole 2048-byte halves only** — `fed` stays 2048-aligned, so with RL a
  power of two a half never crosses the ring seam and the copy needs no split. Fewer
  than 2048 bytes available (`total − fed < 2048`) is the normal underrun path —
  **ring fills never pad**; the linear mode's 80h tail pad is unreachable here.
- **feed (verb 1)** bounds change: `new_total − old_total < 0x8000` (monotonic
  forward) and `new_total − fed ≤ RL` (never overwrite bytes not yet copied out).
- **stage (verb 6) is unchanged**: the caller computes physical grant offsets itself
  and splits its own copy at the seam. status/close/underrun-resume are unchanged;
  status DX (consumed) is free-running in ring mode and the owner works in deltas.
- A feeder keeps `total − consumed ≤ RL` on its side (stricter than the kernel's
  `fed` bound, needs no new ABI) and `total − consumed ≥ 2048` ahead.

Estimated delta for 1a+1b+1c: ~200–250 bytes of `.text` (ISR-adjacent; §34.7's section
rule bars any of it from `.fartext`) against 19,278 bytes of measured guard-1 headroom.

## Kernel amendment 2 — `dskw_readbig` (API slot 0x01E8)

`dskw_read`'s CX is a 16-bit byte count into one ES:BX segment: a file ≥ 65,536 bytes
is FERR_BIG *unconditionally*, and real-world MODs (BEVERLY.MOD is 116,085 bytes) live
above it. New op in `kernel/diskw.inc`, reached by packages at slot 0x01E8
(`OSAPI_FILE_READBIG`, table becomes 60×8):

- in SI = NUL 8.3 name (marshalled like dskw_read's), **ES = destination base
  segment** (buffer starts at ES:0000 — arena grants are paragraph-aligned, so this
  costs callers nothing), **DX:CX = capacity in bytes** (32-bit).
- out CF=0, **DX:AX = bytes read**; CF=1, AX = FERR_* (FERR_BIG when the file exceeds
  the capacity, destination untouched — decided before any I/O from the directory
  entry's 32-bit size).
- Implementation: the same cluster-chain walk as `dskw_read`'s body, but the
  destination advances **by segment** — after each 512-byte sector, ES += 32 with BX
  held at 0 — so the 64KB segment limit never binds and `dsk_xfer`'s 8237 page-straddle
  staging keeps working per sector unchanged. UI-task context only, same as every file
  slot. Resolves in the current directory like its siblings.

## The package

Files: `apps/tracker/tracker.asm` (header, 16×16 note icon, entry, callbacks, worker,
stream plumbing) + `trkplay.inc` (MOD loader/validator, replayer, mixer) +
`trkui.inc` (adapter-parameterized layout + all drawing). One package, `trk_`/`mp_`
prefixes, built like every other app (org 0, `os88pkg.py`, appended at the **end of
APPS_TOOLS** → row 6 in APPS).

### Memory

- **Module blob**: one `OSAPI_MEM_ALLOC` arena grant of
  `min(MEM_AVAIL largest run, 8192 paragraphs)` taken in the fdlg completion proc and
  held for the module's lifetime — NOT sized from the file (no package-facing stat
  slot exists, readbig only answers after the alloc, and a free + re-alloc after the
  read is unsafe: first-fit may relocate the base). Consequence (SPEC §45.4): while a
  module is loaded the grant occupies the largest free arena run, so other package
  loads / other apps' `MEM_ALLOC` may refuse until the Tracker closes. BEVERLY.MOD
  needs ~116KB of arena — fine on a 640KB machine (~233KB arena), honestly "Out of
  memory" on 512KB (~107KB); TEST.MOD (5,596 bytes) loads everywhere the arena exists.
- **Samples** are addressed via normalized per-sample bases: `seg = blob_seg +
  (start >> 4)`, `off = start & 15`, play length capped at 60,000 bytes
  (`MP_MAXSMP`, SPEC §45.5) — tighter than the 64KB segment bound so
  `off + position + step·chunk` can never wrap a 16-bit offset in the mixer
  (step int ≤ 7 even at the 4,000 Hz mix-rate floor).
- **Volume tables**: 65 × 256 signed bytes (16,640 B) in package bss, built at load:
  `vt[vol][byte] = (int8)byte * vol >> 6`.
- **Stream ring**: one 16KB grant from the SND_SEG staging pool (verb 7) — 1.49s of
  audio at ~11kHz, allocated at first play, freed at teardown by the kernel.
- Package image+bss well under the 64KB cap (~35KB with the tables).

### Replayer + mixer (`trkplay.inc`)

ProTracker semantics per `research-ft2-reference.md`/`research-mod-format.md`
(pinned formulas, vibrato table, period table, BCD Dxx, Fxx split at 32, etc.).
Effects v1: 0 1 2 3 4 5 6 7 9 A B C D E1 E2 E6 E9 EA EB EC ED EE F. Ignored
honestly: 8xx pan (mono), E3/E4/E7 waveform control, E5 finetune (v1 plays finetune 0;
the 16-row PT table is future work). F00 = stop. Loader validation follows the 13-point
hostile-input checklist in `research-mod-format.md` §4 — every offset bounded against
the real file size before anything is trusted; magic ∈ {M.K., M!K!, 4CHN, FLT4}.

Mix rate ~11,000 Hz (kernel quantizes via TC). Per MOD tick the mixer renders
`mixrate·5/(2·BPM)` samples: per channel, fetch signed byte from the sample segment,
translate through the volume table, accumulate into a 16-bit chunk buffer; output
byte = 128 + (sum >> 2). 16.16 step from `3546895 / period` with the period clamped
to [113..856] before the DIV (the clamp *is* the #DE guard). Loop/one-shot semantics
per the research: loop iff loopStart + loopLength > 2 bytes (ft2_load_mod.c:308 — a
1-word loop at a nonzero start loops), ft2's overflow fix-ups mirrored.

Interface to the worker: `mp_gen` (CX ≤ 2048 bytes → fills `mp_outbuf`, advances
song state), `mp_play`/`mp_stop`/`mp_setpos`, state words the UI reads
(`mp_songpos/mp_row/mp_speed/mp_bpm/mp_chvol[4]/mp_playing`).

### The worker

Standard §20.6 loop (ALIVE → SLEEP 1 → compute lock-free → one lock hold to draw).
While playing, each wake polls status (verb 3, now legal here), computes
`room = RL − (total − consumed)`, and for at most 6 halves per wake: `mp_gen` 2048 →
stage (verb 6) at `ringbase + (total & (RL−1))` → feed (verb 1) `total += 2048`.
The draw burst re-reads WM_GEOM, arms WM_CLIP_SET, and redraws only the dynamic
regions (pattern view on row change, readouts, scopes) — every erase+text pair gated
per the §11.3 granularity rule. The top MBAR_H-row band is repainted every ~16 frames
(covers the documented fdlg-cancel menu-bar strip repaint, which has no callback).

### UI (`trkui.inc`)

Layouts from `research-ft2-reference.md` §D, chosen by `OSAPI_VIDEO` height: 640×480
VGA (FT2-proportioned: top desktop y=0..171, pattern y=172..479, ~16/18 rows around a
9px band), 720×348 Hercules (mid layout), 640×200 CGA (compact: title line, bars,
21-row pattern view). Colors by DH: VGA = FT2 "Arctic" mapping (bg 0, pattern text 9,
band 7 + text 15, bevels 15/8, block accent 1); mono = text 15 on 0 with an inverted
band (colour-9 text would reduce to invisible — glyphs never dither).

### Keys, menus, lifecycle

Enter = play song, Space = stop/play toggle, P = play pattern loop, ←/→ song position,
↑/↓ scroll rows stopped, 1..4 channel mutes, L = Load (fdlg), F = fullscreen toggle,
Esc = leave fullscreen. Windowed menu set: File ▸ Open…; View ▸ Fullscreen; About via
`OSAPI_ABOUT_SET` (crediting FastTracker II and 8bitbubsy's ft2-clone as reference).
Fullscreen is entered ONLY from W_ONKEY/W_ONCLICK (the documented intent); the fdlg
opens fine on top of the fullscreen surface (code-verified in research). Load flow:
fdlg completion proc MEM_ALLOCs `min(MEM_AVAIL, 8192 paras)` (see Memory above — the
completion proc has no way to learn the file's size before the read), readbigs with
the grant's capacity, validates, builds tables, then starts playback inline (alloc
ring, mix two halves, open ring stream) — all sanctioned UI-lock context. Close box / teardown: the kernel
force-frees the stream, the pool grant and the arena grant; the worker dies in ALIVE.

### No Sound Blaster

`OSAPI_SND_CAPS` gates play: without PCM_BG the app still loads and views modules
(pattern scrolling driven by a tick-timed silent advance is NOT attempted in v1 —
play is refused with a status-line message; viewing/scrolling stopped patterns works).
FM fallback is future work now that FM is whitelisted.

## Tooling and disks

- `tools/mkmod.py` per `research-mod-format.md` §5: deterministic, stdlib-only,
  5,596-byte `OS8088 TEST` MOD (Ode to Joy; square/triangle/noise/sine synthesized
  samples; the pinned effect spread). **Rows 0..7 of pattern 0 play the square lead
  solo** so sndcheck has a clean dominant frequency (~327 Hz: period 339 → 10,463 Hz
  through a 32-byte loop).
- `tools/os88disk.py` gains a data-file mode: non-`.O88` files skip `validate_o88`
  and the extension gate, land as ordinary FAT12 files (the kernel already lists them
  as type-0 entries and the file dialog already Opens them). SPEC §24 amended: a
  folder may carry data files; APPS order becomes …, paint, **tracker, BEVERLY.MOD**.
- Makefile: tracker build rules (mines pattern), `beverly.mod` copied+renamed onto
  both apps images, `build/tracker-test.img` (root-level TRACKER.O88 + TEST.MOD,
  filetest.img pattern) for `make test-snd SB16=1 TESTAPPS=build/tracker-test.img`.

## Test recipe (phase gate)

1. `make` clean build; kernel guards pass; `os88disk.py --verify` both apps images.
2. Boot `make test-snd SB16=1 TESTAPPS=build/tracker-test.img`. Double-click drive B,
   launch TRACKER row 0, `sendkey ret` → screendump: fullscreen FT2 layout.
3. `sendkey l`, type `TEST.MOD`, `sendkey ret` → auto-play. Screendump after ~3s:
   row band advanced, readouts live. `sendkey spc` stop (closes stream), `sendkey esc`
   windowed, QMP quit, then `tools/sndcheck.py build/snd.wav 327 --tol 0.08`.
4. Load BEVERLY.MOD the same way on the shipped apps.img (manual/visual gate).
5. `make test VIDEO=cga` boot + screendump for the compact layout (no SB asserted).

## Deliberately not promised

Finetune (v1), pan, editing of any kind, XM/6+ channel formats, real-8088 mixing
throughput (the mixer is honest about being a QEMU/286+-era luxury; the floor machine
still gets the viewer), FM playback fallback, and scope waveforms (volume bars stand
in — a full waveform scope wants sample-window reads the UI can add later).
