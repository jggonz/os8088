# Apps size pass 2, set D — measured 2026-10-16

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file.

Set D of the second apps size pass (docs/plans/HANDOFF-APPS-SIZE-P2.md): the
apps disk's owner-constrained and small packages — **skies, video, fractal,
piano, timer, midirack, chart**. The measure is the **shipped file** and the
**clusters** it takes on each 360KB disk it rides, not image + bss.

- **Tree**: cut from the pass branch at the set-D claim, `aa185194`
  (`elendilon` `2c6edafc` plus the handoff and the A/B/C claims). "Before" is
  `aa185194` built in this container; `os88diskuse.py --save` was taken there
  before any agent started, and every figure below is `--diff` against it, so
  no cluster another set freed is counted here.
- **Box**: the 4-core cloud container this session ran in. Bytes and
  clusters are counted; cycles are MartyPC's guest counter where a row says
  so; milliseconds are cycles at 4.77 MHz.
- **Method**: handoff 4: one agent per package in its own worktree, briefed
  with 4.3 and the package's 4.5 constraints (Video: speed and stability
  outrank size, play-time routines keep their PARITY; Skies: frame, raster,
  3D and flight code only on equal-or-better cycles). The session was paused
  once for a capacity limit and resumed; no agent lost work.

## The table

| package | file before | after | bytes | clusters, each 360KB disk | image+bss before | after | runtime claims |
|---|---:|---:|---:|---|---:|---:|---|
| skies | 45,991 | 44,610 | **-1,381** | apps360 45->44, games360 45->44 | 58,306 | 58,303 | region 61,345 -> 61,329; the 16 KB (C160 8 KB) flight shadow unchanged; one art read and its transient claim at launch GONE |
| midirack | 30,991 | 28,965 | **-2,026** | media360 31->29 | 47,484 | 47,392 | unchanged (faces claim 11 KB, wavetable banks) |
| `MIDIRACK.GFX` | 2,025 | 780 | **-1,245** | media360 2->1 | — | — | — |
| chart | 8,775 | 8,566 | -209 | office360 9->9 (apps.img/apps120 512-B clusters 18->17) | 10,866 | 10,533 | unchanged (19 KB canvas, read claim) |
| timer | 1,813 | 1,467 | -346 | apps360, small360, smallapps360: 2->2 | 2,072 | 1,603 | none |
| fractal | 2,818 | 2,724 | -94 | apps360, small360, smallapps360: 3->3 | 3,511 | 3,403 | unchanged (run cache 4 KB doubling to 32 KB) |
| video | 41,127 | 41,073 | -54 | apps360 41->41 | 57,790 | 57,706 | unchanged; `NOLIVESND` 40,998 -> 40,944, `VPDIAG` 42,280 -> 42,234 |
| piano | 3,052 | 3,041 | -11 | apps360, small360, smallapps360: 3->3 | 4,095 | 4,027 | none. -92 before the About-card fix below spent 81 |
| **total** | | | **-5,366** | | | | **no claim grew; image + bss fell in every package** |

`os88diskuse.py --diff` against the cut point:

| image | free before | after | gained |
|---|---:|---:|---:|
| `os8088-360.img` (system) | 5 | 5 | 0 — no set-D package rides it |
| `apps360.img` | 1 | **2** | **+1** (Skies) |
| `games360.img` | 80 | 81 | +1 (Skies) |
| `media360.img` | 145 | 148 | +3 (MIDIRack 2, its GFX 1) |
| `office360.img`, `network360.img`, `small360.img`, `smallapps360.img` | | | 0 |
| **all eight** | | | **+5** |

No APP_SMALL arm is in this set.

## What each package took

- **skies** (SPEC.md 88.5.9.1, 88.10.5.7, 88.10.7.2, 88.10.10). The sine
  table ships as its second differences, rebuilt in place at entry (~7 ms,
  estimated); the kept title bands are linked into the image where the
  region already held their room, which deletes the art stream, its loader
  and the re-read on the way out of a flight; world object tables are packed
  field-major and transposed back after decompression through the read
  buffer the load already holds (~6 ms a world load, estimated); shared
  epilogue ladders pay for the two decoders. The program had stood 95 bytes
  under `APP_MAX_SIZE`, so the segment and not the disk was the binding
  limit. **Every routine a frame runs is instruction-identical** (`os88rdiff.py
  --lst`, base against final), at its base address or two bytes below — so no
  cycle count was owed and none was taken. New generator `tools/cssin.py`.
- **midirack** (SPEC.md 105.6.1, 105.8.4, 105.9.5, 105.10). Five smooth
  tables (`mrt_tl`, `mrt_amp`, `mrt_fn`, `mrt_fq`, `mrt_pow`) ship as 2-bit
  second-difference codes and the FM bank and drum kit as columns, both
  expanded at entry; the encoded data and its decoders live in an
  end-of-image area that two written-before-read bss buffers overlay, so it
  costs no RAM. `MIDIRACK.GFX` ships only the five "up" faces and an 86-byte
  recipe; `mra_derive` builds the pressed and greyed faces in the claim.
  Generators (`tools/os88midi.py`, `tools/os88midart.py`) mirror every
  decoder and refuse a stream that does not read back.
- **chart** (SPEC.md 82.15). Callbacks save only what their contract owes;
  SYLK and DIF share one line scanner; BIFF checks a record's length once;
  one `Chart` string; the `_NOKEEP` region form. Checked by a host
  differential (the old and new builds side by side on an emulated 8086 over
  125 scenarios — every reader on hand-made, mutated and fuzzed files, every
  chart type, the BMP export — with the canvas, the BMP and every kernel call
  compared): identical throughout, with negative controls caught.
- **timer** (SPEC.md 111.6). `OS88UI_NOGLYPH`; each routine saves only what
  its caller reads; state kept once (the About card's flag is the button
  record's count); the worker's window from the button record. Image -186
  brought LZ4's layout back inside the region, so the package compresses
  again.
- **fractal** (SPEC.md 40.9). `_NOKEEP` declarations; menu dispatch falls into
  `fr_kick`; shorter encodings off the hot paths; `fr_stepv`'s step as
  `neg`/`js`/`add` (2,646,700 steps against `tools/frref.py`, no
  disagreement); the per-row path restructured with no long-form branch
  left; `fr_inset`'s box tests as one unsigned range test each (659,479
  points, base and new agree). The percentage field is 4 cells, not 5 — the
  fifth was a space on ground nothing draws under.
- **video** (SPEC.md 98). Cold code only, as the owner directed: the bench's
  second copies of `vp_div32`/`vp_mul32` deleted, `vp_canp1` and `vp_parse`
  tightened, four odd cuts summing to an even six. **Every hot routine is
  identical at an EVEN shift in all three arms** (`os88rdiff.py --hot-file
  apps/video/rdiffhot.txt`); five card-text routines sit at an odd shift and
  no play runs them. The two string merges the earlier pass refused stay
  refused, for parity. Small and honest, as 4.5 said to expect.
- **piano** (SPEC.md 36). Step and sharp tables as one; key painters reuse
  each rectangle; one loop over the five buttons; shared viewer tails; the
  sharp erase reads the column's note. -92 file bytes, then the About-card
  fix below spent 81 of them on the owner's word.

## Hot paths

| package | path | before | after |
|---|---|---:|---:|
| timer | `tm_tick`, no digit to draw (5150 CGA, median of 24) | 9,180 | 8,536 |
| timer | `tm_tick`, one digit drawn | 16,137 | 15,527 |
| piano | key -> tone (min) | 5,288 | 5,259 |
| piano | white key redraw (min) | 66,112 | 66,001 |
| piano | black key redraw (min) | 15,784 | 15,763 |
| piano | viewer append (min) | 8,921 | 8,861 |
| piano | keyboard paint, 3 pairs (min) | 867,276 | 866,017 |
| piano | button-row paint, CGA / VGA | 310,275 / 245,647 | 297,627 / 235,553 |
| fractal | whole frame, View > Redraw (`os8088_xt_vga`) | 260.85M / 261.13M | 260.20M |
| fractal | `fr_emit` per computed row | 201,454 / 201,602 | 200,574 |
| fractal | replay, worker -> `fr_emit`, per row | 16,082 | 14,757 |
| skies, video, midirack, chart | — | no hot-path instruction changed (audited), so none taken | |

Fractal's whole replay span reads 35K cycles (0.1%) higher while every part
it is made of reads lower; an intermediate build with byte-identical replay
code read higher still, so the span is taken to end on yields and the tick
and to move with layout. That is an inference, recorded and not reverted.

## Launch cost (work moved to entry)

| package | what | cost at 4.77 MHz |
|---|---|---|
| midirack | `mrt_unz` (tables and bank), every adapter | **73.0 ms**, measured |
| midirack | the picture load on VGA/EGA, 15 faces read -> 5 read + derived | 364.5 -> 413.8 ms (**+49.3**), measured |
| skies | sine rebuild | ~7 ms, estimated — against one disk read and one claim FEWER |
| skies | per world load (Fly or a location pick, never in a frame) | ~6 ms, estimated |

MIDIRack's total on a colour screen is +122 ms, over the 100 ms flag line.
The face derivation was put to the owner at ~+110 ms against ~1,000 file
bytes and **accepted** (2026-10-16); the measured face share came in at
+49 ms after the first build's +123 was cut down, and the table half is
73 ms on every adapter.

## User-visible changes

None, except the two defects below. Pictures are pixel-identical where
compared: Piano's window (0 of 35,030 pixels), Timer's six states, Fractal's
window at rest, MIDIRack's window on VGA (and its faces claim byte for byte
against the generator), Chart under the host differential.

## Defects

| package | defect | state |
|---|---|---|
| timer | while the About card was up, each tick drew the changed digits straight through the card's text ("Timer for 1:18088"); the base did the same | **fixed** (+6 file bytes): `tm_clipd` keeps time but draws nothing while the card is up, and dismissal repaints the digit line |
| piano | dismissing the About card left fragments of its text in the ground between the viewer, the button rows and the keyboard (193 px on CGA, 60 on VGA); the base did the same | **fixed on the owner's word** (+81 file bytes): `pn_abdismiss` fills the ten ground rects in the card's reach, `SONGS:` is drawn opaque with `font_run` — each pixel once; fresh paint and dismissed card now 0 px apart on both adapters |
| (tree) | `build/lzdrv360.img` needs 363 clusters of 354 and does not build, so `lzdrv`/`lzdrv-nohint` cannot run. It is the 360KB SYSTEM disk with `RAMDISK.DRV` swapped; no set-D file is on it and the system disk is unchanged by this set, so it predates the set | **not fixed** — not this set's; excluded from the soak |

## Coverage

`make -j4` (fast tier 47/47), `make small`, `make smallapps` and a final
`make -j4` green on the merged tree.

**Soak** — one `os88soak.py start` over the deduplicated union of the
agents' named rows and the handoff's shared rows (every `vid*` family the
Video agent named, every `skies*`/`cs*` row, `midtab`, `midirack*`,
`mrdraw*`, `mediadisk`, the `fr*` rows bar `frinsetfull`, `dispfrac`,
`editmove`, `timerbounce`, `fptest`, `decfmt`, `stkapps`, `appsmall`,
`smallreq`, `movable`, `dispapps`, `regapp`, `regwork`, `lz*`, `assoc*`,
`instassoc`, `buildmatrix`), 199 rows in 39 minutes at width 4:
**195 ok, 3 FAIL, 1 SKIP** (`vidbench`: the owner's XDC streams are not in
the container). The three failures are **pre-existing** -
`tools/os88bisect.py classify --base aa185194 -n 2` fails each at the cut
point on the same legs, 2 runs of 2:

| row | what it reads | at `aa185194` |
|---|---|---|
| `vidspk2` | opened muted, why 2: `vp_mdet` mutes a two-pulse speaker play on an 8088 by design (34.11.7.1), and the row expects sound | fails, same legs |
| `vidspkunmute` | the same verdict at 11,025 Hz | fails, same legs |
| `vidfskeysvga4pl` | `vp_m3ok` is 1 on a VGA told it has no write mode 3 | fails, same leg |

`lzdrv` and `lzdrv-nohint` were excluded: their disk does not build (Defects).

**`make test-full`**: 54 passed, 0 failed (42 s).

Rule slips, recorded: the MIDIRack and Skies agents each ran one host-side
unit row by hand once (`t_midtab.py`, `t_cssin.py`, both passing) before
catching the brief.

## Wins no set could take

| where | what | bytes | carriers |
|---|---|---:|---|
| packer / format | LZB instead of LZ4 for `VIDEO.O88`: 36,564 against 41,106 | ~4,500 | Video, on every apps disk; ~4x slower launch decode (O88-COMPRESSION-PLAN weighs it) — the owner's call |
| `apps/os88chart.inc` | `ch_sintab`, 362 bytes, computable at entry | ~300 each | Chart, Sheet |
| `apps/os88chart.inc` | `ch_hdrtpl` + `ch_pal_bgra` (BMP header and palette), partly computable | <118 each | Chart, Sheet |
| kernel | let `OSAPI_DECOMP` use the bytes below `DI` as a dictionary (the bound is already `cmp bx, di`; only the DI = 0 refusal stops it) | 458 for Skies' worlds | Skies; any package whose streams share a prefix |
| `apps/os88ui.inc` | `os88ui_btn` (~215 packed in Piano, ~450 in Video) and `os88ui_about_d` (~140) — ~12% of Piano's file, ~705 image bytes of Timer's | per carrier | every package with buttons or an About card |
| `apps/os88ui.inc` | opt-outs for the default-button ring (~19), the ink colour (~25), the latch flag (~9) | ~50 each | most of the 38 button carriers |
| `apps/os88ui.inc` | `os88ui_btninit` opt-out for one-record packages | ~25 each | single-button packages |
| `apps/os88spk*.inc` | ~1.1 KB of image in Video, not analysed | ? | Video and the other speaker carriers |
| kernel / SDK | document that `OSAPI_MEM_CLAIM` preserves AX (pass 1's C.6) | ~3 each | every package recording its claim size |
| kernel / SDK | `OSAPI_FMT_DEC`, content-relative text (pass 1's lists) | ~25-30, ~8 | Fractal and others |
| package format (ABI) | a mirror flag in the icon header | 32 a symmetric icon | Fractal and every symmetric icon |
| `apps/os88partsbody.inc` | parts pad to 512 bytes (pass 1's list) | 89 in Skies | every parts package |

## Left for later, not taken

- Skies: a second cluster needs ~578 more bytes and is judged out of reach
  without the `OSAPI_DECOMP` change.
- Video: ~113 more bytes would take it from 41 clusters to 40; nothing safe
  was found. `apps/video/rdiffhot.txt` does not list `vp_rnext`/`vp_rcur`,
  which run in a resident play — the hot list should name them.
- MIDIRack: `mr_entry` into the end-of-image area (~240 bytes of RAM, no file
  bytes); `mrt_syn`/`mrt_syndr` computable (~40 bytes); the GM name list
  packs to ~1,165 already, so compressing it buys ~85, not pass 1's ~1,100.
- Piano: a single-division keyboard hit test (~10 image bytes) waits on
  confirming the kernel clips click x to the window.
- Fractal: deriving `fr_cmax` from `fr_ckb` would add instructions to every
  cached row; refused.
- **A build trap for the final pass**: after `make small` / `make
  smallapps`, `build/kernel.bin` can disagree with what `os88sym` assembles,
  and emulator tooling then refuses the symbol map; a plain `make` restores
  it. Run `make` last.
