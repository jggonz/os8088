# Apps size pass 1, set 6 — measured 2026-10-12

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file. All eight packages merged; the coverage
line below is the merged tree's.

The sixth batch of the first apps size pass, and the owner's choice of what
is left: **skies, solitaire, tamegram, tank, telnet, texpad, tracker,
video**.

- **Tree**: branch `apps-size-6`, cut from `apps-size-5` (sets 4 and 5, not
  yet on `elendilon`) with `elendilon` at `dfd80976` merged in: `e8f9e5a1`.
  "Before" is `e8f9e5a1` built in this container.
- **Box**: the 4-core cloud container this session ran in; every figure is
  counted, not timed, unless a row says it reports cycles off MartyPC.
- **Method**: as sets 1-5, one agent per package, no time or capacity limit.
  At the owner's direction **Video** and **Skies** were briefed that speed
  and stability outrank size: Video is very performance sensitive,
  temperamental and timing sensitive, so its playback path is untouchable
  unless measured not slower and not less stable, and a win with any
  playback risk is refused; Skies is very performance optimised, so its
  frame, raster, 3D and flight paths move only on equal-or-better MartyPC
  cycle counts.

## What is left of the first pass, and why (the owner's call)

| package | first pass | reason |
|---|---|---|
| taskmgr | **covered** | the system-side size pass took it as a system app |
| thewire | **covered** | the system-side size pass took it as a system app |
| word | **covered** | two earlier standalone passes of its own |
| sheet | deferred | an office program that needs a rework; the rework takes the size |
| weave | deferred | needs parts and a rework |
| wire | not taken | an instrument, ships on no disk |

With set 6 this finishes the first pass over every package `apps/RETIRED.txt`
does not retire or mark as an instrument, bar the two deferred to their
reworks.

## The table

| package | image+bss before | after | % | file before | after | runtime claims |
|---|---:|---:|---:|---:|---:|---|
| tank | 31,562 | 22,594 | -28.4% | 14,146 | 13,567 | the 32 KB shadow claim unchanged; the device-row table moved into its unused 768-byte tail. One projection table instead of two (-4 KB of bss), octave depth buckets for `tk_kx` (2,874 B against 8,192 for the pair), a quarter-wave sine, the halo erase made by flipping the draw walks in place (`tk_eflip`). Frame **1.5-2.2% faster** on Hercules and CGA (MartyPC, cycle-exact `tankperf`), the GAME OVER halo ~16% faster; VGA's Mode X is retrace-bound and did not move. The fixed scenes are pixel-identical; the heavy scene shifts ~490 pixels by one pixel (mean projection error 0.620 -> 0.629 px, max unchanged) |
| tank (APP_SMALL) | 32,577 | 23,922 | -26.6% | 14,704 | 14,114 | the 18/17/16 KB claim ladder unchanged, so `t_appsmall`'s image + bss + claim falls by the same 8,655 |
| solitaire | 7,425 | 5,735 | -22.8% | 5,252 | 4,259 | none, before or after. The entry proc is laid out as the start of the card back's buffer (-198 bss), the win plaque and the About card are one routine, pips are stored as bytes, piles at their own depth. **Every hot path faster** on MartyPC, same seeded deal and moves: New Game -8.3% CGA / -7.2% Hercules / -3.6% VGA, a drop -5.7..-7.3%, a stock click -6.4..-9.5%. Every capture pixel-identical to the base on all three adapters |
| solitaire (APP_SMALL) | 6,973 | 5,532 | -20.7% | 4,878 | 4,067 | none. Played on the 128 KB kern_small machine. Merging the plaque and About took the About-only bytes out of BOTH arms, so the small arm's saving is 3.5% and `t_appsmall` carries a 3% floor for Solitaire with the reason |
| telnet | 30,562 | 21,392 | **-30.0%** | 12,867 | 12,535 | the 8 KB Zmodem staging buffer is a claim made when a download's Save dialog opens and freed when the batch ends, so an idle terminal is 8 KB smaller and a download's peak (29,584) is still under the old idle figure; a refused claim skips the file with `Not enough memory` and the batch goes on. **Faster on every path measured** (MartyPC, 480 bytes through `te_feed`, IRQ0 masked): Zmodem data -15.1%, SGR-heavy ANSI -5.8%, plain text -1.4% |
| tamegram | 6,280 | 3,943 | **-37.2%** | 4,229 | 3,172 | none; the region itself 7 -> 4 KB (the grid two cells a byte is the step from 5 to 4). **Faster on every tick kind** (MartyPC CGA, 600 ticks, prepared board): a piece moved -24.6%, a whole repaint -31.2%, a settle -28.1%, the whole run -29.3% (Hercules -27.2%, VGA -29.6%). The same seeded game tick for tick, pixel-identical at every checkpoint on CGA and Hercules; worker stack high water 142-146 -> 78-98 of its 256 class. New rows `tgplay` and `tgui`, each broken on purpose and seen red |
| skies (program, part 0) | 51,648 | 50,977 | -1.3% | 45,288 | 42,497 | **the heap is the number**: the 11 KB title-art claim is gone (the art shares a bss union with the flight's projection tables), the 16 KB shadow is held only during a flight and is 8 KB in C160, and the region is cut to the program (52,736 -> 51,200). Heap on the title page 62.5 -> **50.0 KB**, in flight 78.5 -> **66.0 KB** (C160 58.0), the title page after a flight 78.5 -> 50.0. One streams part instead of ten: -2,278 B of sector padding, three clusters off each disk. Frame, raster, 3D and flight code not edited: cycles per frame equal to the base within 0.006% and all 12 pinned pictures byte-identical on Hercules, CGA, C160 and Mode X. **One cost: leaving a flight 644 -> 1,129 ms** on a 360 KB floppy (the art is re-read) - the alternative, art kept packed in memory, is +90 ms for +4.4 KB of region |
| tracker | 47,229 | 43,110 | -8.7% | 27,478 | 26,517 | the Scope's 1,856-byte band left the bss for a 2 KB claim held only while Scope is the picked meter (movable through `trk_reloc`; a refused claim picks Off); module blob and ring unchanged. The mixers, the speaker shaper and the `trk_inrend` restart protocol untouched; every changed hot path faster (MartyPC): `trk_render` -4.6%, `trk_feed` -4.1%, `tsp_frame` -7.2%, `mp_tickfx` -5.4%, Scope compose -10.1%. Replayer output identical to the base at every row and tick end (600/600) on a module carrying every effect; `mp_load`'s tables byte-identical for 7 modules, hostile ones included |
| texpad | 36,153 | 29,896 | -17.3% | 18,877 | 16,554 | the claims now hold their CONTENTS instead of their ceilings: the source claim is sized to the document (1-8 KB; 2 KB for PAPER.TEX, was 8), and the export claim grows in 4 KB steps as the file is composed and is freed once it is written (was 24 KB held for the rest of the session after the first export) - **68 -> 32 KB held after one export**. Run and box records 10 -> 7 bytes. Typing and caret keys at or under the base, typeset (F5) -11%, the preview arrow -8%; PAPER.TEX's PDF and PS byte-identical to the base; 33 scripted frames pixel-identical to the base with its defects fixed |
| video | 52,889 | 52,051 | -1.6% | 35,040 | 34,623 | unchanged - no claim moved, resized or deferred, by design. **Cold code only** (UI, open/parse, the info card, setup and teardown), per the owner's direction that speed and stability outrank size: the play loop, reader, ring, hook, card clock, `vp_adue`, Live's sound feed, the decoder, seek, bank and XMS paths are instruction-identical to the base in all three arms (default, `NOLIVESND=1`, `VPDIAG=1`), audited per routine with relocations masked, and every routine a play runs sits at its base address PARITY - five branches NASM had relaxed differently were written back in their base form. 12 demo files driven through the same keys on three machines read 0 differences in ~110 player variables against the base. `NOLIVESND` 52,658 -> 51,820, `VPDIAG` 53,913 -> 53,059 |

| **total (shipped arms)** | **263,748** | **229,698** | **-12.9%** | **163,177** | **153,724** | plus the heap: Skies 12.5 KB less on the title page and in flight and 28.5 KB less on the title page after a flight, TeXpad 68 -> 32 KB held after an export, Telnet's idle terminal 8 KB, Tracker's Scope 2 KB unless picked, TameGram's region 7 -> 4 KB |

## Coverage

The merged tree (`apps-size-6`, all eight packages) built clean (fast tier
41/41) and ran the union of the eight agents' rows, plus the shared rows
their changes reach (`regapp`, `regwork`, `appsmall`, `smallreq`, `movable`,
`dispapps`, `dispmodex`, `dispsize`, `dispfsx`, `dispprefer`, `editmove`,
the `lz*` family, the association rows, `sndmove`, `usbmouse`, `smallboot`,
`small128`), through `tools/os88soak.py` at lane 4: **278/278 ok in 1:01:03,
0 FAIL, 0 SKIP** - every `tank*`, `tg*`, `tel*`, `skies*`/`cs*`, `trk*`,
`tp*` and `vid*` row. `mtools` was installed for this run, so `vidbigclus`
and `vidkeyclus`, which skipped in Video's own runs, ran and passed. The two
rows flagged as intermittent during the pass (`vidsndmute`, `skiescrash`)
both passed.

Not covered by any row:

- **Solitaire has no soak row of its own.** It is reached by `dispapps`,
  `appsmall` and `smallreq`; its agent's pixel A/B tools (`tools/solcheck.py`,
  `tools/solpanel.py`, `tools/solperf.py`, `tools/solswitch.py`) read 0
  differing pixels against the base on all three adapters and both arms, and
  are committed but not registered.
- **`vidbench` reports `ok` in 0.1 s and ran nothing**: it skips without the
  owner's XDC sample streams (`$OS88_XDC_SAMPLES`), which are not in the tree.
- Telnet's single-buffered staging arm, the over-8,192 cluster refusal and
  F6-F12 on the wire; TameGram's region actually moved by a compaction
  (`regwork`'s arena did not move it on the base either); Tank's corrupt
  high-score file; Skies' exit timing and C160's 8 KB shadow (agent's own
  MartyPC scripts); Tracker's row-and-tick equivalence and cycle counts
  (agent's scratch harnesses).

## Defects

- **Video: `vidsndmute` is flaky on the base too** (1 in 6 there, 1 in 7
  here): `tests/vidsound.py --button` reads `vp_done` before the press edge,
  so a press on frame 60 before the key at 61 makes the player act a frame
  later, and the row reads that as a restart past its frame. The race is in
  the test. Not fixed.
- **TeXpad, fixed, all in the base** (SPEC.md 69.3.1): `\textbf`/`\emph`
  around two or more words froze the machine (`tp_space` clobbered SI); the
  first word after a mid-paragraph page break was overwritten (`tp_footer`
  cleared DX); `~` wrote a byte outside the source claim; `\pagestyle
  {myheadings}` matched as `headings`; a word over 82 characters inside an
  argument overran `tp_wbuf`; a table after a `center` block printed a stray
  fragment; `[` and `]` could not be typed in the source pane.
- **TeXpad, not fixed**: boxes are drawn 1pt tall (full height changes the
  picture); `tp_tab_spec` can over-read an unterminated `p{` spec;
  PAPER.TEX stops at the 480-run cap with its text pool 29% used - a
  combined run/text pool is a CAPACITY change, recommended to the owner.
- **Solitaire, fixed**: an adapter change re-picked the card metrics and never
  re-rendered the back (a VGA game moved to CGA drew a 32x44 back at the old
  stride through a 28x28 card). 3 bytes, SPEC.md 43.12.1. Not fixed: SPEC.md
  43 opens with Paint's 42.19 text pasted under its heading.
- **Telnet, fixed (latent)**: a repeated ZFILE restarted the file at 0 but
  kept a pending CRC-recovery skip count, silently dropping the restart's
  first bytes.
- **TameGram, fixed**: the HUD band was filled black and then filled again on
  every whole repaint. **Not fixed, the owner's call (base too)**: the matrix
  border runs through the edge cells and a piece's erase blacks a whole cell,
  so a piece moving along an edge rubs the border out until the next whole
  repaint (SPEC.md 49.8).
- **Skies, fixed (latent)**: `cs_wldget` computed the high word of a 32-bit
  file offset wrongly for any stream past 64 KB of the file.
- **Tracker**: `trkclick` failed on the smaller image because
  `tools/os88rate.py`'s `scan()` returns the first TRACKER header in memory,
  which during a load is the compressed FILE read above the region - fixed in
  the row, LATENT in `trkrate`, `trklcd`, `trktxsurf` and `trkscrl`. And
  (base too, not investigated) the speaker path never starts a dense effects
  module.
- **Tank**: `tankperf` could pin a scene before the round started, so
  before/after frame times were not comparable - fixed; `tankaim` hard-coded
  the 256-entry sine - fixed.
- **Stale documents, corrected after the merges**: the "32KB shadow" in
  CLAUDE.md, the SMALLOMIT comment and HEAP-CLAIMS (Skies' shadow is 16 KB and
  its art claim is gone); HEAP-CLAIMS' TeXpad row; CLAUDE.md's `VPDIAG=1`
  figure (608/144, already 844/180 on the base, 828/180 now).

## Missing components and shared wins (folded into HANDOFF-APPS-SHARED.md 0.4)

- **An implied-solid-mask picture form** for `OS88UI_IMG`/BIMG records, now
  raised by three packages: PiXEL 512 B (set 5), Tracker ~160 B, Video
  ~160 B.
- **The 8.3 filename mangle exists twice** (Telnet ~330 B, FTPD ~299 B) and
  `telzm` polices the two against drift by test; a shared include saves
  ~300 B and the drift.
- `OSAPI_FERR_TEXT` gains a fifth and sixth carrier (Telnet ~90 B, FTPD).
- **Content-relative drawing** (TameGram 154 B of wrappers), **one slot for
  the content origin AND size** (every package calls `WM_CONTENT` and
  `WM_GEOM` as a pair), no-preserve forms of `OS88_REGION_MOVABLE` /
  `OS88_WORKER_RESTARTABLE`, and an `os88ui_about` with a line-pitch ladder
  and left-aligned lines (TameGram keeps a private ~220 B panel; `os88ui_about_d`
  is 175 B in every carrier, Tank).
- `os88partsbody.inc` opt-out for a lazy part a package reads itself (Skies'
  loader -512 B, one cluster); the kernel re-home trimming the carve's TAIL
  as well as its head; a purgeable claim for packages.
- A shared draw/erase polarity for `os88gfx.inc` walks (Tank's `tk_eflip`
  patches 27-28 bytes to turn a draw walk into its erase).
- A shared "update a scroll bar only if total/fit changed" beside
  `os88ui_sbar` (~60 B per two-bar package, TeXpad).
- **Tooling**: a per-routine build diff with relocations masked and an
  address-parity report (NASM relaxes branches differently after a cold edit
  and silently moves hot code by an odd amount - Video); `tools/stkbalance.py`
  reported one of two seeded broken jumps into a shared epilogue;
  `tools/os88rate.py`'s `scan()` race (Tracker); harness writes to `/tmp`
  (`dispapps._map`, `csworlds.world_map`); MartyPC's debug server cannot set
  IP; no general fixture forces a named package's region to move; a golden
  frame row would catch a consistently mis-drawn bar that `tpdraw`'s
  incremental-against-full comparison cannot.

