# EXCITEBIKE-PLAN: a native 8086 Excitebike for os8088

Status: DESIGN RECORD, written before any code. Branch `game/excitebike`,
worktree `/tmp/exb`. Author: the architect pass of the port workflow.
Revision 2: rewritten for the ART AND AUDIO POLICY below (revision 1 imported
CHR and note streams from the disassembly; that is void).
Authority order when two documents disagree: SPEC.md (the section this port
adds, once written) > this plan > the scout reports in `/tmp/exb-reports/`.
The scout reports are inputs, not contracts; where they disagree with each
other this plan says which one won and why (section 2). Their CHR-import,
`EXCITEBIKE_SOURCE` import and note-stream-import advice is void; only their
sizes, timings and budgets are used.

## 0. The ART AND AUDIO POLICY (binding, user decision)

1. The package, the build, and the shipped disks depend on NO NES ROM, no
   `CHR_ROM.chr`, and no file of the disassembly, at build time or run time.
   A plain `make` on a machine with no `../NES-Games-Disassembly` builds the
   whole game, art and sound included. There is no `EXCITEBIKE_SOURCE` knob
   and no fallback path: there is one art set and it is ours.
2. Every sprite, tile, background, font, splash, palette and sound is ORIGINAL
   work, committed under `apps/excitebike/` (art sources) and compiled by
   committed host tools. "In the spirit of the game" means: a side-on motocross
   course with lanes, ramps and hurdles, a rider on a bike, a temperature bar;
   it does not mean tracing, recolouring or pixel-copying Nintendo's tiles,
   sprites, screens or tunes. The wording and the splash are ours and never
   reproduce Nintendo's logo. The package identity stays `EXCITEBIKE` (the
   task's name, as DrMarco kept the `drmario` directory); whether the
   on-screen title should be a different display name is an open question for
   the user (section 17.11) and is a one-string change.
3. Gameplay RULES are not expression and are re-implemented in our own code:
   physics feel, the kinds of track piece, ramp/hurdle/mud/rough behaviour,
   temperature and overheat, lap timing and ranking. Numeric constants may be
   studied from the disassembly and are committed as our own `const.inc`. The
   five tracks are our own courses, authored as committed text scripts in the
   piece grammar (section 5); they are not the disassembly's streams.
4. Anything that compares against the disassembly or the NES oracle is a
   TEST, lives under `tests/`, reads the external directory only through the
   `EXCITEBIKE_REF` environment variable (default
   `/Users/jggonz/Repos/NES-Games-Disassembly/Excitebike`), never writes into
   the repo, and SKIPS cleanly with a printed reason when the directory is
   absent. No test is required for `make`, and no test result is a build input.
5. Nothing derived from the ROM is committed: no CHR bytes, no bank tables
   dumped as data, no note streams, no `.fm2`. The scouts' hex dumps in
   `/tmp/exb-reports/` stay outside the repo.

This is a NATIVE REMAKE, not an NES emulator, in the shape of DrMarco (SPEC.md
§100) and 1942 (SPEC.md §101): the simulation, the renderer, the sound driver,
the art, the tracks, the songs and the front end are all ours. The disassembly
at `/Users/jggonz/Repos/NES-Games-Disassembly/Excitebike` is a study aid for
the FEEL of the rules and the oracle of an optional test (section 15.2), and
nothing else.

The primary goal is speed on an IBM PC/XT (8088, 4.77 MHz). Every decision
below is priced in 4.77 MHz clocks, using the measured primitives in
`/tmp/exb-reports/scout-perf.md` (MartyPC) and the repo's real-5150 figures
(CLAUDE.md, PERFORMANCE.md Part 2).

## 1. Decisions at a glance

| # | question | decision | why, in one line |
|---|---|---|---|
| D1 | VGA video mode | `FSXM_VGA0D` 320x200x16 planar, 8 px per byte per plane, own CRTC programming; **not** Mode X | a tile is one latch byte per row; scroll costs 2.05k clk/px against 5.5k in Mode X (scout-perf 4.1, 4.3) |
| D2 | CGA video mode | `FSXM_CGA320` 320x200x4, one buffer, 8-px MC6845 start-address steps, RAM-composed sprite boxes | no other scroll exists on a CGA |
| D3 | Hercules | **Wave 6, go/no-go**: the CGA path re-parameterised, NES pixel = 2 Herc pixels wide, 8 NES px = 16 Herc px = ONE 6845 step. Ships only if it meets the gate; otherwise refuses like DrMarco and 1942 | at 2:1 the 6845's 16-px granularity is exactly one tile, which is why scout-perf's "refuse" (computed at 1:1) does not apply |
| D4 | screen layout | ONE 25-tile-row, 320x200 layout on all adapters (24 world rows + 1 HUD row) | one world renderer, one set of tile ids, three thin back ends |
| D5 | scroll | "spiral" (sheared) layout: address of (row r, world column x) = `base + pitch*r + x`; each 8-px step writes only the entering byte column; no ring, no mirror, no rebase | scout-gfx section 10.1 and scout-perf 4.6, unified; a whole race is <= 1,500 steps |
| D6 | scroll granularity | 8 px (one byte) on every adapter in the baseline; VGA 1-px pan is a Wave 6 option behind a measured gate | avoids 8 sprite phases and the ATC/PPM risk; CGA cannot do better anyway |
| D7 | pages | VGA: two display pages (flip by CRTC start address). CGA/Herc: one buffer, entering column written in the vertical blank, sprites composed in RAM and transferred once | both precedents (1942) chose exactly this |
| D8 | HUD | VGA: CRTC line compare pins the bottom row at address 0 (fallback: in-stream). CGA/Herc: in-stream, ONE 8-line strip, black ground, <= 160 painted pixels | CGA has no split, and a tall HUD costs 20.9k clk per step (scout-perf 4.6.4) |
| D9 | sprites | ORIGINAL poses (24 designed, not 29 inherited) stored once as compact layer masks; pre-shifted into even x phases (VGA 4, CGA 2) in a heap claim; rider colour is a variable, not baked | 24 poses x 3 colours would otherwise triple the data |
| D10 | timestep | the simulation is EXACTLY one NES frame per step (60.0988 Hz virtual), run k steps per display frame from a fractional accumulator, capped at 8; rendering happens once per display frame | step-for-step comparable with the study oracle on the shared rules, frame-rate independent, no spiral |
| D11 | frame rate | governor picks a fixed display period of 1..4 sub-ticks (54.6/27.3/18.2/13.7 Hz). HARD gate: Selection A >= 27.3 Hz mean, Selection B >= 18.2 Hz mean on MartyPC 4.77 MHz. Model says Selection A holds 54.6 Hz at 51-61% | even pacing beats peak rate (PERFORMANCE.md Part 3.2) |
| D12 | scope | Selection A (solo time trial, 5 original tracks, 2 laps, par times, ranking, qualification, second-pass "hard" flag) + Selection B (3 AI riders) + attract demo. DESIGN mode is dropped | section 3 |
| D13 | input | arrows = D-pad, Z = A (throttle), X = B (turbo), Enter = Start, Esc = leave | section 10 |
| D14 | audio | one sequencer, `OSAPI_SND_FM` on AdLib/SB, `OSAPI_SND_TONE` on the speaker, engine pitch follows speed, no PCM, no IRQ hook, no worker; ORIGINAL tunes and effects composed as committed text scores | DrMarco's shape, not 1942's (scout-audio 1); policy section 0 |
| D15 | package class + art source | `local` in `apps/RETIRED.txt` (DrMarco's class): not in `all`, not on live media. ONE art set, original, committed as text/JSON sources under `apps/excitebike/art/` and compiled by a stdlib-only tool; no source-directory knob, no fallback arm (section 12) | user policy (section 0) |
| D16 | names | dir `apps/excitebike/`, header name `EXCITEBIKE`, file `EXCBIKE.O88` (8.3 limit: `EXCITEBIKE.O88` is refused by `name83`), art files `EXBV.GFX` `EXBC.GFX` `EXBH.GFX`, sound blob `EXB.SND` (incbin'd into the package image, NOT a disk file), splash `EXBSPL.VGA/.CGA/.HRC` | scout-integ section 2 |
| D17 | SPEC | new section **102** (`## 102. Excitebike ...`), written before the code of each wave | scout-integ G; renumber before merge if a parallel branch also takes 102 |
| D18 | kernel | ZERO kernel bytes, no new `OSAPI_*` slot, no new driver | CLAUDE.md, KERNEL-MEMORY.md |

## 2. Where the scouts disagreed, and who won

1. **Mode X 256x240 (scout-gfx) versus 0Dh 320x200 (scout-perf).** scout-gfx
   wants a 1:1 NES pixel grid and the original 240 lines; scout-perf measured
   the scroll cost. Mode X: 422 clk a tile, mirror x2, two pages, 26 rows =
   44k clk per 8-px step, i.e. 5.5k per pixel scrolled; 0Dh: 261 clk a tile,
   2.05k per pixel. At the maximum 204 px/s that is 1.1M clk/s (23% of the CPU)
   against 0.42M clk/s (8.8%), and the Mode X two-page 320-px working set does
   not fit a 64 KB plane at all (66.6 KB before HUD and cache). **0Dh wins.**
   What it costs: 40 lines. Section 4.1 recovers them without losing gameplay
   (the crowd is uniform and the HUD shrinks to one row; 99.5% of sprite tiles
   are in tile rows 6-22).
2. **Line compare and parallax.** The NES has two scroll values (crowd/banner
   run 1.125x the track). A VGA has one split and it pins the bottom. **Parallax
   is dropped** (cost 0, visible only in banner plates); the split is used for
   the HUD.
3. **Hercules.** scout-perf refused at 1:1 (16-px granularity, a 95 ms software
   shift). scout-gfx proposed 2:1 horizontal doubling. Under 2:1 one 6845 step
   is one NES tile column, the same shape as CGA, so the honest position is
   "same path, different constants, prove it" (D3), not "refuse".
4. **Pan.** scout-perf recommends the ATC pan for 1-px steps; scout-gfx flags
   MartyPC's pan model as unverified. The baseline needs no pan (D6).
5. **Race length.** scout-gfx estimated ~700 addresses of travel from the
   traces; scout-game decoded the disassembly's tracks: 667/604/730/746/650
   columns per lap (706/638/750/797/674 in the second pass), two laps, plus a
   43-column runway. Our tracks are our own, but their SIZE budget is taken
   from that measurement, so the arithmetic stays honest: **each of our laps
   is authored to at most 797 columns (both passes), the runway is 43**, and a
   race is up to 43 + 2 x 797 = 1,637 columns = 13.1 kpx. That is still linear, still a plain start-address ramp
   (1,637 bytes on 0Dh, 3,274 on CGA), and section 4.3 shows it needs no wrap on
   VGA and a modulo-8192 ring on CGA.
6. **Sim cost.** scout-perf assumed a literal port at 60k clk per NES frame
   and rejected fixed 60 Hz stepping. scout-game shows the per-frame logic is
   ~2-5k 6502 cycles for four bikes; a native 8086 step is budgeted at 4.5k
   clk for the player plus 2.5k per AI bike (section 7). Fixed exact stepping
   with catch-up is affordable and buys bit-exact verification.

## 3. Scope

Ships (in wave order):
- Selection A: one rider, five tracks in the real order, the real track
  pieces, all 21 ramp/hill element scripts and their 18 handlers, hurdles,
  mud, rough ground, the striped kicker, arrows, the finish gate, two laps,
  the temperature/overheat loop, crash and walk-back recovery, wheelie flip,
  landing windows and bounce, lap flash, elapsed clock at 1.6 cs/frame, best
  times, rank, qualification, the second-pass ("flag 1") obstacle set and par
  times, the last-track repeat with the shrinking qualify window.
- Selection B: three AI riders (target speed, throttle, lane change, hurdle
  wheelie, bike-bike collisions, respawn from the screen edges) and the fallen
  rider set-piece. Ships only if its 18.2 Hz gate passes on both adapters; if
  it does not, it is greyed with the reason on `CPU_8086` (SPEC.md §47: grey a
  fact) and stays on for faster tiers.
- Attract demo: nearly free: the normal race with the AI driving bike 0, so it
  costs no game code once the AI exists.
- Title, mode select, track select, results, best times, pause, "Loading
  graphics", and a desktop splash window (DrMarco's shape).

Dropped, with reasons:
- DESIGN mode and its cursor/edit UI, the Famicom Data Recorder states and
  SAVE/LOAD (states $8-$D). 21-item editor, its own sprites, palette states
  3-17, and none of it is on the race path. The design-track DATA format (a
  <= 224-byte stream) stays supported through an optional `EXBTRACK.DAT`
  beside the package, which is also how tests inject controlled tracks
  without a knob.
- Sprite-0 split, NMI upload queue, OAM rotation and the 8-sprite-per-line
  flicker (we do not emulate the limit), the second pad, the 64-bit RNG (any
  LFSR does: nothing in Selection A reads it).
- The parallax crowd (section 2, item 2).
- PCM sound (section 11).
- Two-player.

## 4. Video

### 4.1 One layout, 25 tile rows, 200 lines

```
tile rows 0-4    crowd            5 rows, period-8 uniform along x        (40 lines)
tile rows 5-7    banner           sky band, plates, BEST text             (24 lines)
tile rows 8-23   track band       grass 8-13, tuft 14, lanes 15-21, hedge (128 lines)
tile row  24     HUD              TIME  |  TEMP bar  |  position          (8 lines)
                                                                      = 200 lines
```
5+3+16 = 24 world rows = 192 lines, +8 for the HUD strip = 200. The NES had
30 rows (240 lines); the 40 lines dropped are five of the six HUD rows and the
NES's unused rows 29 plus a blank row 24; gameplay lives in rows 6-22 for 99.5%
of sprite tiles (scout-gfx 5; for our art it is a DESIGN RULE: every bike pose, obstacle and lane lives in tile rows 6-22, asserted by the compiler). The picture is 320 px wide (the NES was 256):
the player stays at x = 88 (byte column 11) and the view shows 232 px ahead
instead of 200. `VIEW_W` = 320 on VGA and CGA, 360 on Hercules, and every
spawn/cull test reads it.

Bikes are simulated in world pixel coordinates; the NES's 8-bit screen-x
page trick (`$0084` = 0/1/2) becomes a plain signed 16-bit offset from the
player, culled at +-`VIEW_W`.

### 4.2 What a "world column" is

The art compiler (section 12) reduces OUR world to:
- a **band dictionary**: the distinct 14-tile track columns plus the hedge
  pair, baked into 128-line strips. HARD BUDGET: at most **72 entries**, at
  most 1,008 tile ids (72 x 14), authored in `art/pieces.txt` (section 5). On
  VGA each strip is 128 bytes in offscreen VRAM (72 x 128 = 9.2 KB), on
  CGA/Herc 256 bytes in RAM (72 x 256 = 18.4 KB, see 13);
- a **top dictionary** of 64 columns x 64 lines (crowd + banner sky band,
  period 512 px like the original layout so the arithmetic below is
  unchanged), 4 KB VGA, authored as one 512x64 picture in `art/top.txt`;
- per track a **column id array** `cid[x]`, one byte per column, expanded from
  the run-length stream at race start (<= 1,637 bytes);
- per dictionary column a 6-byte **collision row set** (the six lowest
  non-blank tiles, bottom-up), 432 bytes at 72 entries, so the terrain under
  a bike is `coll[cid[x]][row]`: two loads, no ring buffer, no per-column
  generator on the 8088. The 43-column runway is 43 leading plain ids.

The pixel content of (world column x, line l) is
`W(x, l) = l < 64 ? top[x & 63][l] : band[cid[x]][l - 64]` for l < 192.

### 4.3 The spiral layout, with the arithmetic

Hardware scroll moves the CRTC start address S. With row pitch P bytes,
the display shows memory `S + P*r + c` for row r, column c (0 <= c < P).
Increment S by one unit: row r now shows what row r+1's leftmost cell held,
at its right edge. So if world column x of row r is stored at

```
A(r, x) = base + P*r + x                      (VGA 0Dh: P = 40, unit = 1 byte)
```

then advancing the window one unit needs exactly ONE new byte per row (the
entering column), and it lands in the cell that the row below just scrolled
out of. There is no ring, no mirror and no rebase; memory is a sheared image
of the world and `A` does not depend on S at all. The window invariant is that
row r only holds columns `[S, S+P)`; a cell is shared by (r, x) and
(r+1, x-P), which is why nothing outside the window may be trusted to hold
anything.

- **VGA 0Dh**: P = 40, S counts bytes (8 px). A page is 40 x 192 = 7,680
  bytes of window + at most 1,637 bytes of travel = 9,317 bytes (call it
  9,472, aligned). Two pages 18.9 KB, HUD 320 B (40 x 8), band dictionary
  9.2 KB, top dictionary 4 KB, glyph/extras ~1 KB: **~34 KB of the 64 KB
  plane, ~29 KB spare**, so no wrap arithmetic anywhere on VGA.
- **CGA**: address of line l = `(l&1)*0x2000 + ((l>>1)*80 + 2x) mod 8192`,
  S counts 2-byte words. The window of 100 rows x 80 B = 8,000 B in an
  8,192-B bank leaves 192 spare, so cells never alias inside a window
  (same reasoning as 1942's `and 1fffh`). Every column write splits at the
  wrap once per column (compute the wrap row once, two loops), never a
  per-row `and`.
- **Hercules (Wave 6)**: line l at `(l&3)*0x2000 + ((l>>2)*90 + 2x) mod 8192`,
  87 rows x 90 B = 7,830 B of 8,192; only the four rows bounding the 200-line
  field need a blank-cell clear per step (entering cell of the row above the
  field is the field's own left cell), 4 x 2 bytes.

**Skip test (the tile-shadow idea, corrected).** Because the entering byte of
line l replaces the cell that held line l+1 of column x-P, "the ring already
holds it" means comparing `W(x, l)` with `W(x-P, l+1)`, NOT the same line.
Flat lines match; boundaries between flat bands do not. Doing that compare
at run time costs as much as the write (128 x ~15 clk), so it is precomputed:
the two default plain columns (75% of all column writes in the five tracks:
2,842 + 2,838 of 7,537) have a fixed sparse **write list** for the case
"entering plain, leaving plain of the same parity" (P is even, so the parity
always matches), ~30 lines of 192 (the 24 textured lines plus band
boundaries). Every other pairing (an obstacle on either side) writes the full
column. Cost model, VGA: plain pair 30 x 27 = 0.8k clk; full 192 x 27 =
5.2k clk; obstacle columns are ~25% of a lap and each costs twice (entering,
then again when it leaves), so ~42% of writes are full:
0.58 x 0.8k + 0.42 x 5.2k = **2.65k clk per column write, the number the
budget table uses.** The top dictionary has the same trick for the crowd
rows.

**Invariant that makes the skip legal: a page is sprite-clean at the moment
columns are written.** Frame order per page is fixed:

1. erase this page's previous sprite footprints (restore from `W`);
2. write the entering columns for the distance moved since this page was last
   made current (two frames' worth on VGA, one on CGA);
3. draw sprites and overlays;
4. VGA: flip (CRTC start address, latched at vertical retrace); CGA/Herc: in
   the blank, write S then the entering columns (section 4.5).

Erase on VGA is a latch copy of the bike box: 4 byte-columns x 32 lines =
128 `movsb` + row steps (~3.5k clk) plus the world lookups (~0.7k) =
**4.2k per bike per page**.

### 4.4 VGA specifics

- Mode set: `OSAPI_FSX_MODE` with `FSXM_VGA0D` (SPEC.md §53.4), then our own
  CRTC writes for the offset register (pitch 40), start address, line compare.
  FSX's `OSAPI_FSX_PAGE` does NOT serve 0Dh (SPEC.md §53.10: Mode X and
  Hercules only), so the flip is ours: write start hi/lo, then wait for the
  retrace with `OSAPI_FSX_WAIT(FSXW_VSYNC)` (bounded, SPEC.md §53.5), then
  the ATC pel-pan register stays 0. `FSXF_FASTTICK` is passed to
  `OSAPI_FSX_RUN` so `FSXW_FRAME` is the 54.6 Hz sub-tick (SPEC.md §53.5.1).
- Latch discipline (scout-perf section 7): `movsb` never `movsw` for
  VRAM-to-VRAM; restore write mode 0, bit mask FF, set/reset off, map mask F
  after every latch or mode-3 run; `es` is loaded from `[FSI_SEG]` per routine.
- HUD: line compare at scanline 2 x 192 = 384 (0Dh is double-scanned; the
  exact value including the off-by-one and the overflow bits in regs 07h/09h
  is determined empirically and pinned in SPEC by the Wave 2 pixel gate). The
  fixed HUD row lives at address 0 and is drawn once per change, once, for both
  pages. **Fallback (decided now, not later):** if line compare cannot be pinned
  on MartyPC and QEMU, the VGA path switches to the CGA-style in-stream HUD via
  the same `hud_instream` routine the CGA path uses; cost ~4.4k clk per byte
  step, +12k per frame at 18.2 Hz, still inside the budget.
- Palette: pixel values are palette SLOT indices (0-15) and the five track
  themes and the banner flash are DAC rewrites, not new art. One race screen
  uses 13 colours of the 16 (scout-gfx 3.1): BG 9, sprite 7 with shared
  black/white; three slots spare. Rider colour variants (red, violet, teal)
  swap three DAC-free slot assignments in the sprite layer table (section 6).

### 4.5 CGA specifics

- Mode `FSXM_CGA320`; MC6845 start address counts words; `S & 0x1FFF`.
- Beat the beam: the 6845 reloads S at the top of the frame, so per display
  frame the order is (1) wait for vertical retrace start, (2) write S, (3)
  write the entering column and HUD strip top to bottom (the CPU at 45 clk a
  row is ~6x faster than the raster at 63 us a row, so it stays ahead), all
  inside the ~3.9 ms blank (62 lines x 63.5 us) which a 192-row column at
  45.3 clk (8.6k clk = 1.8 ms) fits with room for 1.4 columns.
- Sprites: 1942's discipline. Per bike: rebuild the bike's (old union new)
  damage box from the world tiles in a RAM box (16 tiles x 16 B = 256 B at
  13.4 clk/B = 3.4k), draw the pose into it (compiled or AND/OR data, ~7.7k),
  transfer to VRAM once (192 B x 18.3 = 3.5k): **~15k clk per bike** and no
  erased state is ever visible.
- HUD: one strip, in-stream. The painted content is shifted one unit right in
  memory each step so it stays put on screen; only the 2 stale bytes x 8 rows
  are erased and the painted 40 bytes x 8 rows rewritten (~6k clk per step,
  not the naive 20.9k). Content: TIME (7 tiles = 56 px), TEMP bar (64 px),
  position/lap (24 px) = 144 px <= 160.
- Colours: hand-authored per-SLOT ink map (grass, ground, sky, ramp face,
  rider), not per NES colour, so the five themes reuse one map. Two profiles
  from scout-gfx (P0-high: black/light green/light red/yellow; P0-low:
  black/green/red/brown), `C` cycles as in 1942. Nearest-colour mapping loses
  the ramps and is forbidden; the CGA gate looks at the picture.

### 4.6 Hercules (Wave 6, go/no-go)

Same code as CGA with a different address function (4 banks, stride 90), a
different pixel expander (colour -> 2 Herc pixels wide, 3 grey levels from
lit-pixel count, a row-alternating pattern for the mid level, outlines forced
black), `VIEW_W` = 360, and the extra blank-cell clear of section 4.3. Tile
and pose byte counts are IDENTICAL to CGA (8 NES px = 16 Herc px = 2 bytes;
a 24-px pose = 6 bytes wide). Go criteria: Selection A >= 18.2 Hz mean on a
MartyPC Hercules XT and a readable picture in `tools/hercshot.py`. No-go:
refuse fullscreen with a sentence exactly as DrMarco and 1942 do.
`OSAPI_FSX_PAGE`'s Hercules second page is NOT used (it conflicts with a
colour card, SPEC.md §53.10) and is not needed.

### 4.7 EGA and others

`OSAPI_FSX_CAPS` (SPEC.md §53.4) has no 0Dh bit on EGA; EGA does offer
`FSXM_CGA320`, so an EGA machine runs the CGA path unchanged. Wave 7 verifies
this with `make test VIDEO=ega` and drives it with `--screen 640x350`; if it
does not hold, EGA refuses with a sentence. MDA/other: refuse.

**Wave 7 decided: EGA refuses** (SPEC.md 102.8.5). An EGA has no port 3D9h, which every CGA
engine here writes directly, and nothing in the tree hosts a real EGA - `VIDEO=ega` is a VGA card
the kernel believes is an EGA and would happily say the path works. `excitebikeega` holds the
refusal.

### 4.8 Pixel-exact reference

`tools/exbsim.py` contains a host renderer that produces the expected
320x200 field from (track, scroll position, bike list, HUD values) with the
same `W` function and the sprite art, independently of the guest's spiral
memory. The video gate reads the guest's visible bytes and compares. This is
1942's "full-refresh equivalence" and is the only thing that makes the skip
test and the erase invariant safe to trust.

### 4.9 What wave 2 measured, and what it changed (SPEC.md 102.1 is the contract)

The plan's cost model priced VRAM from the scouts' bench numbers; the first
whole-frame measurements on MartyPC's 4.77 MHz XT (`tests/excitebike_perf.py`,
PERFORMANCE.md Set 148) corrected it, and the corrections are decisions:

1. **Three VGA pages, not two.** A start-address write takes effect up to one CRT
   frame later, so with two pages the next frame may write the page still on the
   glass unless it spins for the retrace (up to 14 of a sub-tick's 18 ms). Three
   pages make that impossible by construction (SPEC.md 102.1 has the argument) for
   ~1.5x the column work, ~2.4k clocks a frame.
2. **VGA sprites use write mode 2, not 3.** MartyPC's VGA does not implement write
   mode 3 (it logs `Invalid`), so the plan's ~73-clock set/reset loop could not be
   verified on the one emulator that counts cycles; mode 2 is standard everywhere
   and measures ~130 clocks a (byte, layer) pair against the plan's 94.6 for the
   data-driven mode-3 loop.
3. **The VRAM word is 2-3x the model.** VGA latch copies are near the model
   (37 clocks a line); a VGA sprite pair is 1.4x and a CGA VRAM word is ~86 clocks
   against the 36 the plan used. The CGA design follows: everything on the card
   after the retrace in raster order, at most two columns a frame, sprites composed
   in RAM boxes and written once, an unchanged bike skipped whole, and the HUD
   painting only its changed cells when the window did not move.
4. **Skip lists are an art contract.** With the first art a plain band pair still
   differed on 103 of 128 lines; the lawn, the track surface, the hedge and the
   plain columns were redrawn in vertical runs and a pair now writes 33 of 128 lines
   in 7 runs (the run start costs 155 clocks, so lines the merge would rewrite are
   cheaper than another run).
5. **The HUD split is emulator-dependent by one scan line** (MartyPC starts it ON the
   compare value, QEMU one line after), so the HUD's first pixel row is blank and
   page line 192 is kept black: the same picture on both, gate G1 passing on QEMU
   without the in-stream fallback.
6. **The sub-tick clock is made, not read**: no API returns it, so the frame samples
   PIT channel 0 and counts reloads; the governor's work figure excludes the CGA's
   retrace wait and the first frames' full-window writes.
7. The CGA plan gate "sprite draw + erase <= 15,000 clocks" is met a frame under the
   scripted scroll (13.0k) and MISSED a rewrite (26.5k): see SPEC.md 102.6.

### 4.10 What wave 3 measured, and what it changed (SPEC.md 102.3 is the contract)

The rider went in over the wave-2 engine and a whole course at turbo was driven
through it on MartyPC (`tests/excitebike_perf.py --lap`, PERFORMANCE.md Set 149).
What the measurement changed:

1. **The pacing was wrong for n >= 2 and is fixed.** The sub-tick clock (wave 2)
   counts a reload only when the PIT count read is LARGER than the last sample's; a
   wait of n >= 2 samples once a sub-tick, so about half the reloads went unseen and
   the loop waited a whole extra sub-tick: a CGA frame that should have been 2
   sub-ticks was 3.6 or 4.5 (p99 420k clocks). `FSXW_FRAME` returns only after a new
   IRQ0, so the wait itself now counts the boundary it proves.
2. **The CGA is paced by its retrace, not by n.** The retrace spin (`xc_vr`) was 30-75k
   idle clocks a frame on top of ~60k of work, so the frame overran its sub-tick
   whatever n said. With the wait gone the loop settles at one CRT frame (79.6k clocks)
   when the work fits and two when it does not: a whole course averages
   58-59 frames a second instead of 26.7. `n` is still computed and reported.
3. **Steps a frame come from the frame's length in PIT counts**, not from whole
   sub-ticks: at frame rates that are not a whole number of sub-ticks (every CGA
   frame now) a whole-sub-tick clock makes 0, 1 or 2 steps in an irregular pattern,
   which reads as judder. A 32-bit accumulator in 2^-24 of a step takes 845 a count,
   and frames are measured start sample to start sample so nothing is lost between
   them (the first version measured to the end of the wait and ran 2% slow).
4. **The plan's 24 poses were already drawn (wave 1); the shadow and the dust are
   the four small sprites turned into 24 x 24 EFFECT poses by the compiler** (ids
   24-27), so the machinery that draws a bike draws them and the erase invariant
   covers them. The cost is a record each: a bike in the air is two records, a
   landing three. **`XB_MAXBIKES` = 4 has no room for three opponents and their
   shadows: wave 4 raises it and re-measures the CGA**, whose rewrite is the
   costly part.
5. **Physics constants follow the study tables where a table exists** (speed steps and
   caps, heat targets and rates, gravity, pitch cadence, lane geometry: 17 of 21 rules
   identical, `tests/excitebike_ref_deviations.txt` names and explains the other four:
   the mud rule, the landing window, the take-off, the pitch scale). The rider's
   pitch is stored RELATIVE to the terrain's slope, which is what made every pose one
   subtract; the take-off keeps 1.5 x the last sloped rise so a ramp launches in
   proportion to speed with no per-element code.
6. **A step costs ~3.0k clocks**, not the plan's 4.5k, so the simulation is 3-4% of a
   VGA frame; the gate is by the `xm_s0` / `xm_s1` counter.
7. **The harness is the guest's**: a script ring and a 16-byte-per-step trace ring in
   `.bss` (4.3 KB) let `tests/excitebike_ref.py` compare the guest with `exbsim.py` on
   EVERY step of 29,465 (a breakpoint per step, the plan's first idea, would be 29k
   round trips), and `tests/excitebike_perf.py --lap` feed a whole lap without stopping
   the machine.

### 4.11 What wave 4 measured, and what it changed (SPEC.md 102.4 is the contract)

The opponents, the menus and the campaign went in over the wave-3 rider, and Selection B was driven through a whole
course on MartyPC (`tests/excitebike_perf.py --selfb`, PERFORMANCE.md Set 150). What the measurement changed:

1. **The sub-tick clock was wrong for any frame over one sub-tick between samples**, which the plan's model never
   priced because it assumed 45-60k frames. Four bikes are 130-250k and the game ran at HALF SPEED (30 steps a second)
   until `xb_clk_sample` was called at the seams of the back ends. A decision, not tuning: **a routine that can run for more
   than a sub-tick must sample inside itself**, conditionally, so that the lone rider (the wave-2 scroll gate) pays nothing.
2. **Opponents are the same rider on a swapped state block**, not a second simulation (plan section 7 priced 2.5k a bike
   for a lighter step): 3.1k for `xm_step` plus 0.3k of brain, a 600-clock swap each way, one rule of physics, and the
   player's guest == model gate untouched. Their lane look-ahead (the first version, 26k a frame) is four 61-byte tables and
   one pass.
3. **The governor predicts** `work - simulation / n` because at n = 3 it measures 3.3 steps of work and could never conclude
   that n = 2 would fit.
4. **G4 is met with the plan's own fallback on the CGA**: three opponents cost the CGA 364k a frame (mean) and 488k (p99);
   two cost 249k / 339k. The VGA races three (247k / 298k). The CGA's compositing of overlapping bikes (`xc_comp`) is a
   clipped draw of every bike a box meets, ~10k a box with the pack bunched; the plan's "at most 15k a bike" was already
   missed by the wave-2 measurement (a card word ~86 clocks, not 36).
5. **The second pass is the harder set on both laps** (wave 3 had compiled `pass2` as lap 2), with its own par; the campaign
   is: rank by the time against par (par, +4 s, +8 s), qualified rides go on, the fifth starts the second pass, the last
   course repeats with windows shrinking 0.5 s and 1 s a repeat. Best times live in the window's bss (a package may not
   write a file from the UI path and the tree keeps no score elsewhere).
6. **Not built, on purpose**: the fallen-rider set-piece and the marker rider (plan section 3, risk 6), a clipping blitter
   (opponents pop at the edges: wave 6), sound (wave 5).

## 5. World and track data (our own courses, our own piece grammar)

Nothing here is imported. The world is authored in three committed text
sources under `apps/excitebike/art/` and `apps/excitebike/tracks/`, compiled by
`tools/excitebike_assets.py` (section 12), and the numbers the 8086 code uses
as immediates live in the committed `apps/excitebike/const.inc`.

**Piece grammar.** A track is a sequence of PIECES; a piece is a run of world
columns with a dictionary column id, a terrain class and optionally an element
script. The KINDS of piece are the game's rules (policy section 0.3) and are
ours to name and draw: `plain` (two visually distinct plain ids so the grass
texture does not stripe), `rough` (slows), `mud` (slows sharply, heats),
`hurdle-low`, `hurdle-high` (wheelie or crash), `arrow` (cools the engine),
`kicker` (striped launch ramp), `ramp-a..ramp-h` (rising/falling ramps of
different length and height), `hill-a..hill-f` (multi-column rises with a
down-slope), `finish`, `start-gate`. That is 21 element scripts and ~36
piece ids, the same count as the study material so the physics code is
exercised over the same variety, but every piece's picture, name and
length is ours. An element script is a list of `(x, code)` keyframes of the
same three kinds (`ANGn`: set pitch target n; `DEFERn`: wait n columns; `Hn`:
set ground height n) interpreted by one 120-line 8086 routine; the scripts
themselves are committed data in `apps/excitebike/art/pieces.txt` next to the
tile pictures they belong to, so a piece is defined in ONE place: name,
picture (14 tile ids per column), class, script.

**Tracks.** Five text files `apps/excitebike/tracks/t1.trk .. t5.trk`, each a
list of `piece [xN]` lines plus `lap 2`, `par mm:ss.cc`, `theme n`, `pass2:`
substitutions (the second pass swaps some pieces for harder ones, the
"flag" mechanic of scope item S1). Authoring rules the track compiler ASSERTS
(so a hand edit cannot break the budget): lap length <= 797 columns in both
passes; at least 70% of columns are the two plain ids (the skip-list model of
4.3 depends on it); no two obstacles closer than 8 columns (a rider must be
able to land); every track is completable at turbo on the reference
simulator (`tools/exbsim.py --run-track` fails otherwise); par time is a
result of that run (par = best turbo time + 8%), not a guess. Difficulty
ramps t1 to t5 by obstacle density (30 to 55 obstacle pieces per lap) and by
the number of hills. Track data compiles to a run-length stream (id, count,
bit 7 = pass-2-only) of ~130 bytes a track, ~650 bytes total, expanded to
`cid[]` per (track, pass) at race start.

**Element scripts and handlers.** The 18 per-element handlers (what an
element does to pitch/height/speed when a bike is over it) are ported as our
code from the rules (~120 lines of 8086). The tile-to-element trigger is one
256-byte class table generated from `pieces.txt`.

**Physics tables** (speed steps, turbo caps, heat rates, landing windows,
bounce rule, lane geometry) are numeric constants in `const.inc`, tuned to
the feel studied in the reference game; `tests/excitebike_ref.py` compares
them with the disassembly's tables when the reference directory is present
(15.2) and otherwise just self-checks that the two stated ranges (top speed
3.4 px/step, turbo cap 3.25) hold in `exbsim.py`.

Per-column terrain lookup for a bike: `row = lane_band_row(lane)`
(lane < $10 -> 5, $10-$17 -> 4, $18-$1F -> 3, $20-$27 -> 2, $28-$2F -> 1,
else 0; four lanes, our own numbering), `tile = coll[cid[x]][row]`,
`elem = class[tile]`. Each bike owns its column; a bike more than a screen
ahead reads plain ground.

## 6. Sprites (original poses)

Inventory (ORIGINAL, designed to the budget rather than inherited): 24 bike
and rider poses, each within a 24 x 24 pixel box (3 x 3 tiles); a pose has at
most three ink layers besides transparent (bike body, rider, outline/highlight)
and at most 260 opaque pixels; the poses are: level 1-2 (two wheel-turn frames),
pitch up 1-3 (wheelie), pitch down 1-3, ramp climb 1-3, airborne level and
nose-down, landing squash, crash-slide 1-2, rider thrown / tumbling 1-3,
recovery walk 1-2 (some poses are mirrors or shared, budget counted after
sharing: 24 stored). Plus small sprites: shadow (16 x 4), dust puff 1-3 (8 x 8),
LAP flash glyphs and finish-gate signage (drawn from the tile set, not
sprites), temperature gauge cells (filled cells). Rider colours: three
variants (red, violet, teal) as three layer-colour triples, not three copies.
Design target for the visible count: 4 bikes x (bike + shadow) + dust <= 12
sprites at once (the reference game's 19.6 average came from NES 8 x 8
sprites; ours are whole poses, so the count is per pose).

Authoring and format: each pose is an ASCII pixel grid in
`apps/excitebike/art/poses.txt` (one character per pixel, `.` transparent,
`0-3` layer ink index), 24 x 24, drawn by hand by the artist pass with
`tools/excitebike_art.py` (which also emits a contact-sheet PNG for review,
section 12.3). The compiler stores each pose ONCE as up to four one-bit layer
masks (opaque mask, plus one layer per ink 1-3), 24 x 24 bits = 72 bytes a
layer, 24 poses <= 6.9 KB in the file. The rider colour is a per-instance
`layer_colour[3]` variable read by the blitter, so red/violet/teal cost no
extra data (on VGA a layer is one set/reset OUT, whose value is the variable;
on CGA the colours are baked into two bits so CGA ships ONE rider colour and
distinguishes opponents by lane and position - it has 4 colours).

Phases: the load step expands each pose into the memory-phase variants the
back end needs, in a heap claim:
- VGA 0Dh: x mod 8 in {0,2,4,6} (even x only, as 1942), ~450 B per
  (pose, phase) as `(byte delta, mask)` records grouped by layer, 24 x 4 x 450
  = **~43 KB in one 64 KB claim**. Fallback if the spike says otherwise:
  phases {0,4} (22 KB) with x quantised to 4 px.
- CGA: x mod 4 in {0,2}, mask+data rows of 6-7 bytes, 24 x 2 x 336 = 16 KB.
- The player is at a fixed screen byte column with the baseline's 8-px scroll,
  so it needs phase 0 only and takes the aligned fast path (3 bytes per row,
  no shifting): ~8k clk instead of ~16k.

Draw cost, VGA data-driven: 168 (byte, layer) pairs at 94.6 clk = 15.9k
(that is the ceiling; our <= 260 opaque-pixel rule keeps the average nearer
130 pairs = 12.3k); compiled hot frames (7.4k at 44.3 clk a pair) are a Wave 6
optimisation for the 6-8 most common player poses only (12 KB of code per
pose across 8 phases, one phase for the aligned player).

Culling: a bike whose 4-byte box is not wholly inside `[0, 40)` byte columns is
not drawn (the baseline; a clipping blitter is a Wave 6 item). AI riders
therefore pop in at the edge; this is recorded in SPEC as a deliberate
simplification.

## 7. Simulation

**One step = one NES frame, exactly.** Speed is 8.8 fixed point in px/frame;
acceleration/deceleration is applied once every 4 frames per bike
(`frame & 3` picks whose turn it is); world scroll per step is
`int(speed) + carry(frac accumulator)`; take-off, gravity `$34` (`$18` with
Left held), pitch stepping every 6/4 frames, the 13-step wheelie flip, heat
(targets 8/32/17, rates `$3F/$0F/$07`, cool `$0B`, overheat at 32, 258-frame
stall, arrows to 8), lanes (1 unit a frame, stops at the centres $0E/$1A/$26/$32,
clamps 7/8..$39), the landing windows (`D86C/D87C`) and the bounce rule are
ported verbatim (scout-game 5, "Keep exact"). What is replaced: PPU/OAM
machinery, the ring buffer (section 5), the RNG, the recorder.

Why exact stepping: Selection A is deterministic and RNG-independent (verified
on the oracle: bit-identical trajectories with different RNG phase), so
`tools/exbsim.py` and the guest can be diffed against the NES oracle step for
step. A coarse-dt design would give that up and would need its own
calibration for a game whose feel lives in integer cadence.

Cost budget (8088 clk per step, to be met in Wave 3 and asserted by test):

| part | budget |
|---|---|
| player bike: input decode, pitch/lane/heat, speed on the 4th frame, scroll add, column crossing, terrain lookup, element keyframe, air step | 4,500 |
| each AI bike: control byte synthesis (target speed, pitch toward slope, hazard look-ahead of 4 columns) + the same physics | 2,500 |
| clock, lap/finish, timers (once per step) | included in the player figure |

Steps per display frame come from a fractional accumulator: `acc += elapsed
sub-ticks x 1.1003` (60.0988 Hz / 54.6195 Hz sub-tick), run `floor(acc)`
steps, cap 8 (below ~7.5 display fps the game slows instead of spiralling,
matching SPEC.md §101.4's no-catch-up rule). Elapsed time is read from the
sub-tick counter after the pacing wait, so game speed is correct on any
machine. The game clock adds 1.6 cs per step (our rule, matching the study game's; 250
steps = 4.00 s), i.e. it deliberately shows 4% less than wall time.

Rendering runs once per display frame from the state after the last step: the
scroll distance is the sum of the steps' `$0060` values (<= 8 x 5 = 40 px,
<= 5 byte columns, always inside the window).

RNG: 16-bit LFSR seeded from the tick counter for AI and the marker rider only.

## 8. Pacing: the governor

Frame period is a whole number n of 54.6 Hz sub-ticks (FASTTICK, SPEC.md
§53.5.1), waited with `FSXW_FRAME` (which yields; `FSXW_VSYNC` only inside the
present, SPEC.md §53.5.1 explains why it must not be the clock).

```
work = sub-ticks the last frame's work took (rounded up)
n   := max(n, work)                     immediately when a frame overruns
n   := n - 1                            only after 64 consecutive frames whose
                                        work fits (n-1) x 0.8 (hysteresis)
```
Even pacing beats a higher average rate. On a faster tier the same code lands
on n = 1. `n` is a visible SPEC-documented behaviour and the test reads it.

## 9. Per-frame budget (arithmetic, 4.77 MHz, MartyPC clocks)

Sub-tick = 87,381 clk (4,772,727 / 54.6195). Steps per frame = 1.1003 n. Top
speed 3.4 px/step (turbo cap 3.25; a kicker briefly 4.4). Components:
- VGA columns: 2 pages x (px/8) columns x 2.65k... the model uses the
  conservative 0.5 x 5.18k = 2.6k per column write;
- VGA HUD 1.2k (line compare: digits only); CGA in-stream 6k x columns + 2k;
- VGA bike 20.2k (erase 4.2k + draw 16k), CGA bike 15k; extras (dust, rider,
  lap letters, gauge) 6k per frame in both;
- sim 4.5k + 2.5k per AI, per step; audio 2k speaker / 3.5k FM peak; keys
  2.5k (six `OSAPI_KEY_DOWN` at 46.7 us); present 0.5k; bookkeeping 5k.

Computed with `/tmp/exb-reports/exbbench/model.py`-style arithmetic (the
script for this table is reproduced by Wave 2 in `tests/excitebike_perf.py
--model`):

| adapter / mode | n | steps | px/frame | total clk | % of period | x1.3 VRAM factor |
|---|---|---|---|---|---|---|
| VGA, Selection A | 1 (54.6 Hz) | 1.1 | 3.7 | 44.8k | 51% | 61% |
| VGA, Selection A | 2 (27.3 Hz) | 2.2 | 7.5 | 52.2k | 30% | 35% |
| VGA, Selection A | 3 (18.2 Hz) | 3.3 | 11.2 | 59.5k | 23% | 27% |
| VGA, Selection B | 2 (27.3 Hz) | 2.2 | 7.5 | 130.8k | 75% | 91% |
| VGA, Selection B | 3 (18.2 Hz) | 3.3 | 11.2 | 146.4k | 56% | 67% |
| CGA, Selection A | 1 | 1.1 | 3.7 | 42.8k | 49% | 58% |
| CGA, Selection A | 2 | 2.2 | 7.5 | 52.5k | 30% | 36% |
| CGA, Selection A | 3 | 3.3 | 11.2 | 62.3k | 24% | 28% |
| CGA, Selection B | 2 | 2.2 | 7.5 | 115.5k | 66% | 79% |
| CGA, Selection B | 3 | 3.3 | 11.2 | 133.6k | 51% | 60% |

Reading: Selection A holds 27.3 Hz with a 3x margin over the model and will
often govern to n = 1. Selection B holds 18.2 Hz comfortably and 27.3 Hz only
without real-card wait states. The MartyPC VGA has no wait-state penalty
(scout-perf 3.2), so every VRAM-touching term is a LOWER bound; the x1.3
column is the honest planning figure and a field run (docs/FIELD-MACHINES.md)
is the only thing that closes it. These are EMULATOR results; SPEC will say
so in SPEC.md §101.3's words.

The one figure that could sink the plan is sprite draw (26k of the 52k in
Selection A). That is why the sprite spike (Wave 2) precedes any game logic
and why the fallback ladder in section 15 exists.

## 10. Input

`OSAPI_KEY_DOWN` (level read of the make map, 46.7 us a call) polled once per
frame, and the typed-key buffer emptied every frame (SPEC.md §67.11.2: a held
key still arrives as keystrokes and will otherwise back up the UI task).

| NES | key |
|---|---|
| D-pad Up / Down (lane) | Up / Down arrow |
| D-pad Left / Right (pitch) | Left / Right arrow |
| A (throttle) | Z |
| B (turbo, heats the engine) | X |
| Start (pause, confirm) | Enter |
| Select (menu cursor) | Space (menus also take Up/Down) |
| leave / desktop | Esc |
| sound on/off | M |
| CGA palette profile | C |
| full screen | Alt+Enter (`OS88_ALTENTER_ARM`, `apps/os88alt.inc`) |

`OSAPI_KEY_DOWN` is advice, not an oracle (SPEC.md §67.11.1): a lost break
code leaves a key down. Pause and focus loss clear the held state.

## 11. Audio (original music, original effects)

Decision: DrMarco's shape (scout-audio 1). One sequencer emits notes to
`OSAPI_SND_FM` on AdLib/SB (3 music voices + 1 effect voice, claimed
all-or-nothing at entry, refusal falls back to the speaker, honour BL) or the
lead voice to `OSAPI_SND_TONE` on the speaker. No PCM, no IRQ hook, no mixer,
no worker (1942 spent 13 KB and 172 lines on three 8 kHz samples; a motocross
game's sounds are engine, thud, whistle and blips and FM patches cover them).

**Authoring pipeline (no reference dependency).** The songs are composed by
us as committed text scores `apps/excitebike/audio/*.mml`: one file per song,
a header (`title`, `tempo` in beats per minute, `loop` bar or `once`) and up
to three voice lines in a plain note notation (`o4 c8 e8 g4 r8 ...`: octave,
note name, duration as a divisor, dotted, rest, tie). The effects are
committed as `apps/excitebike/audio/sfx.txt`: one line per effect,
`name  priority  (frames hz)+`. `tools/excitebike_audio.py` (Python standard
library only, `--selfcheck`) compiles them into ONE binary `EXB.SND` and a
generated include `build/exbsnd.inc` (song offsets, effect ids, engine table).
The tunes are original compositions in a driving, syncopated 4/4 rock-chiptune
character; they are not transcriptions or paraphrases of Nintendo's cues and
their titles are ours. A pass by the audio author (Wave 5) writes them and
`--selfcheck` prints each song's bar count, voice count, duration in frames and
loop point so a reviewer can hear the structure without a speaker.

**Songs** (each 8-16 bars, <= 40 s before its loop): `title`, `select`,
`results`, `finish` (a 3-4 s fanfare, plays once), `gameover` (once). A pause
blip and the start-light beeps are effects. Music plays only on non-race
screens, so the race frame carries only the engine and one-shot cues
(scout-audio 5). Songs loop at their `loop` bar (a title that goes silent
after half a minute is a defect).

**Budget.** `EXB.SND` <= 3,072 bytes: at 2 bytes an event (note+duration byte
pair, run-length rests), <= 900 events over all songs = 1.8 KB, effects
<= 300 B, engine table 128 B, headers ~200 B. `audio.inc` resident code
<= 3 KB. Per-frame cost as in section 9.

**Engine sound.** Our own model: a quantised speed step `k` in 0..63 (speed
divided down by the top speed) chooses a tone from a generated table
`hz[k] = base x 2^(k/24)` (64 words, 128 B, computed by the tool, no run-time
maths), with a small vibrato table indexed by a frame counter and an
overheat mode alternating two Hz. `k` chases the throttle target one step per
game step in each direction at rates set in `const.inc`, so the note glides.
**Speaker engine is shipped an octave up** (base 175 Hz to ~830 Hz;
`base`/span are two patchable words: a real PC speaker rolls off below
~100 Hz, SKIES-ENGINE-SOUND section 4/5). FM adds a fifth-below voice as an
optional second note. At most **one `OSAPI_SND_TONE` call or FM pair per
frame, and only when the quantised step changed** (engine step unchanged
~30 clk; changed, speaker ~0.15 ms; FM 0.3-0.7 ms).

**Effects** (original short records, at most one new effect a frame,
priority crash > landing > overheat warning > start beep > blip): `crash`
(falling whistle), `land-soft`, `land-hard` (two thuds), `bump`, `hurdle`,
`overheat` (two alternating tones), `lap` (chirp), `start-light` (three
beeps then a rising go), `pause`.

Pause silences engine, jingle and FM claims and resumes held notes.

**Timing.** The sequencer counts in game steps, converted from the score's
tempo at build time (`frames = 3600 / (tempo x divisor/4)` rounded, then
scaled by 546/1000 in 8.8 to game steps at 60.0988 Hz), one event per call,
bounded catch-up. Notes shorter than a display frame merge into held notes
(DrMarco's "one new note per voice per frame"). Notes are octave-folded into
the driver's 19..6208 Hz range; rest = 0 Hz; `OSAPI_SND_TONE` always with a
finite lease (20 ticks, refreshed every 16).

## 12. Original art and sound pipeline (no reference dependency)

There is NO `EXCITEBIKE_SOURCE`, no CHR importer, no ROM hash check and no
"ROM art or fallback art" switch: a plain `make excitebikedisk` builds from
committed files only, on any machine. (The 1942 precedent is followed in
shape - committed art masters plus a stdlib-only production compiler, with the
optional Pillow-based re-import as an authoring tool - but not in its ROM arm.)

### 12.1 What is committed

```
apps/excitebike/art/
  palette.json     the 16 palette slots (RGB), the five track themes as DAC
                   sets (sky, grass, ground, ramp face, hedge slots vary), the
                   two CGA ink maps (P0-high, P0-low) and the Hercules grey
                   levels. Original colour choices.
  tiles.txt        the tile set: ASCII grids 8x8, `.` and 0-9,a-f = palette
                   slot; ~64 named tiles (lane surfaces, grass, tuft, hedge,
                   ramp faces and edges, hurdle, mud, rough, arrow, finish
                   chequer, start-gate parts, crowd, banner-plate frame)
  pieces.txt       pieces (section 5): 14 tile ids per column, class, script
  top.txt          the 512 x 64 crowd + banner picture, as a grid of tile ids
                   plus a few 16 x 16 blocks
  poses.txt        the 24 rider/bike poses (24 x 24, ink layers 0-3), small
                   sprites (shadow, dust)
  font.txt         our own 8 x 8 HUD/menu alphabet and digits (0-9, A-Z, a
                   subset of punctuation), 64 glyphs
  splash.json      splash scene description (see 12.4)
  README.md        how it was authored, the prompt log if generated art was
                   used, and the statement that it contains no NES data
apps/excitebike/tracks/t1.trk .. t5.trk    section 5
apps/excitebike/audio/*.mml, sfx.txt       section 11
```

All are plain text so a diff shows exactly what changed, and they are the
SOURCE, not derivatives of a binary. Nothing is derived from a ROM.

### 12.2 How the art is authored, reproducibly

Two tools, both committed:
- `tools/excitebike_assets.py` (Python standard library only): the production
  compiler that `make` runs. Inputs: everything in 12.1. Deterministic: same
  inputs, byte-identical outputs. `--selfcheck` asserts the budgets of 12.5.
- `tools/excitebike_art.py` (offline AUTHORING tool, not a build dependency;
  Pillow optional): (a) generates the first committed versions of
  `tiles.txt`, `poses.txt`, `top.txt` and `font.txt` from drawing procedures
  (parametric shapes: wheel circles, frame lines, rider blob and helmet,
  ramp wedges, crowd dots) so the initial art exists without a graphic
  artist and the procedure is the record of how; (b) renders contact sheets
  (`build/excitebike-art/*.png`: every tile, every pose x every phase, every
  piece, the five themes) for human review; (c) optionally imports a
  hand-drawn or generated PNG master into the text grids, quantising to the 16
  slots (the 1942 route, `apps/1942/art/README.md`), in which case the master
  PNG and its prompt are committed beside it in `art/masters/` as the source
  of record. After authoring, the text files are edited by hand as needed;
  they, not the generator, are authoritative.

The art-authoring rules (checked by `--selfcheck` or the review pass):
side-on view, dark outline, high-contrast readable at 320 x 200 and on a
2-colour and 4-colour adapter, ramps and hurdles recognisable by SHAPE not
colour; no tile or pose is traced from another game (the artist pass works from
the written art brief in `art/README.md`, not from screenshots).

### 12.3 What the compiler emits

`tools/excitebike_assets.py -o build/excitebike-art/`:
1. `EXBV.GFX` (VGA), `EXBC.GFX` (CGA), `EXBH.GFX` (Hercules, Wave 6): magic
   `EXBV`/`EXBC`/`EXBH`, word total length, word record offsets, 16-bit byte
   checksum verified by the guest as in 1942, host refuses a claim overflow.
   Records: tile bytes in the adapter's native layout; band and top dictionary
   sources (tile ids); pose layer masks with per-phase expansions done at
   LOAD time by the guest (`sprite.inc`), not stored; font; palette/DAC sets;
   collision rows; class table; track streams (adapter-independent, in the
   package image instead - see 13).
2. `exbtables.inc` (generated NASM, `-I build/`): symbol offsets, counts,
   the element-script table, the physics constants that come from
   `const.inc` are NOT regenerated (they are committed source).
3. `EXBSPL.VGA`/`.CGA`/`.HRC`: the desktop splash art in DrMarco's DMF1
   record format, via `tools/drmario_assets.py`'s writer imported by name
   (not copied), from `splash.json` (12.4).
4. `EXB.SND` and `exbsnd.inc` from `tools/excitebike_audio.py` (section 11);
   `EXB.SND` is `incbin`'d into the package image (one file less to load, no
   claim), so it is a build intermediate and never a disk file.
5. a `.exb-art` stamp (hash of every input) so an edited grid rebuilds the
   package and disks; JSON sidecars (palette, pose boxes) for the test readers.

### 12.4 Splash and front-end art

The desktop splash window and the loading/title screens are original: a
side-on rider mid-jump over a ramp against a striped sky, our own wordmark
drawn from `font.txt` letters enlarged 3x (no Nintendo logo, no NES title
screen), a checkered border in the DrMarco surround idiom. `splash.json`
lists the layers (tile ids, pose ids, text strings, positions) and the
compiler rasterises them to the three native formats.

### 12.5 Hard byte budgets (asserted by `--selfcheck` and `t_*` rows)

| artefact | budget | notes |
|---|---|---|
| tiles.txt | <= 80 tiles | 64 authored + slack; VGA 32 B, CGA 16 B, Herc 8 B a tile |
| `EXBV.GFX` | <= 20,480 B | tiles 2.6 KB + poses 6.9 KB + top 1.2 KB + font 2 KB + palettes/tables ~2 KB |
| `EXBC.GFX` | <= 16,384 B | same contents at 2 bpp |
| `EXBH.GFX` | <= 16,384 B | (Wave 6) |
| each `EXBSPL.*` | <= 24,000 B on disk | DMF1 repeated-row packets; expands 19,200 (VGA)/8,000 (CGA) |
| `EXB.SND` | <= 3,072 B | section 11 |
| package `image + bss` | <= 61,440 B | 45 KB estimated (13) |
| the whole game on a 360KB disk | <= 130 KB of 354 KB | package ~30 KB packed + GFX + SND + splash + README |
| sprite claim | VGA <= 44 KB, CGA <= 17 KB | 24 poses, section 6 |
| band dictionary | <= 72 columns | 4.2 |

If an asset exceeds its budget the compiler fails the build with the name and
the number: the budget is a decision to take, not a build fix (CLAUDE.md,
memory rules).

### 12.6 Reproducibility and the licence line

Rebuilding from a clean clone gives byte-identical GFX/SND (no timestamps, no
host font, sorted iteration). `art/README.md` records the authoring method for
each asset class (procedure, or master + prompt) so the provenance of every
pixel is documented; there is no external asset in the tree. A unit test
(`tests/unit/t_excitebike_clean.py`, fast tier, < 0.2 s) asserts that no file
under `apps/excitebike/`, `tools/excitebike_*.py` or `tests/excitebike_*.py`
mentions `CHR_ROM`, `bank_FF`, `.fm2` or an absolute path into the
disassembly, except `tests/excitebike_ref.py` and `tools/exboracle/` which are
the only place `EXCITEBIKE_REF` may appear.

## 13. Memory map (all figures are Wave 1/2 estimates, pinned in SPEC)

Package: header `OS88_HEADER 'EXCITEBIKE', xb_entry, 1, OS88_STACK_256`,
image + bss <= 61,440 bytes (the DrMarco note; the loader's `APP_MAX_SIZE`).
Estimated image: game/sim/world/AI ~14 KB, three video back ends ~16 KB,
front end ~5 KB, audio code ~3 KB, tables ~7 KB (track streams 0.65 KB,
element scripts ~1.5 KB, physics 0.6 KB, EXB.SND <= 3 KB, class table 256 B,
collision rows 0.45 KB) = ~45 KB, bss ~4 KB (bike state 4 x 64 B, `cid[]` 1,637 B, HUD/pacing/misc).
`os88parts.inc` (SPEC.md §20.12) is the escape hatch if this is wrong, not
the plan.

Instance-owned MOVABLE claims (`OSAPI_MEM_MOVABLE` with a relocation proc,
SPEC.md §66; only the current adapter's file is loaded; every disk-visible
base 512-aligned):

| claim | VGA | CGA | Herc |
|---|---|---|---|
| G: sprite records (phases) | ~43 KB (budget 44) | ~16 KB (budget 17) | ~16 KB |
| G: tile/dictionary RAM copy for restore and load staging | streamed to VRAM, then freed | band dict 18.4 KB + top 4 KB + tile bytes ~2.6 KB | same |
| C: canvas / bike boxes | none (no RAM framebuffer) | 4 boxes x 192 B + damage lists ~2 KB | same |

VGA VRAM per plane (0Dh): HUD 0x0000 (320 B), page 0, page 1 (9.5 KB each),
band dictionary 9.2 KB, top dictionary 4 KB, glyph/extras ~1 KB =
~34 KB of 64 KB, about 30 KB spare. CGA VRAM is the 16 KB card, single
buffer. No pixel framebuffer in RAM on VGA. XT target stays 640 KB; the
256 KB floor machine is not a target (the game says so and returns to the
launcher rather than fail mid-race, SPEC.md §47).

## 14. File and section layout

```
apps/excitebike/
  excitebike.asm    OS88_HEADER, icon, entry (OSAPI_WM_CREATE, OS88_REGION_MOVABLE,
                    OS88_ALTENTER_ARM, OSAPI_WM_PREFER), bracket, state machine
  const.inc         committed numeric constants (our own tuned values; compared
                    with the disassembly by tests/excitebike_ref.py when it exists)
  front.inc         desktop splash window, title/select/track/results screens
                    (tile-drawn inside the bracket), loading screen (resident bitmap,
                    drawn before the first disk read), refusal sentences
  game.inc          race loop, pacing governor, flow, rank/qualification, records
  sim.inc           bike physics, elements + handlers, heat, crash, landing
  world.inc         cid[] expansion, collision lookup, spawn marker, track select
  ai.inc            AI control, bike-bike collision, marker rider, respawn
  video.inc         cell function W, damage lists, sprite scheduling, page order
  vga.inc  cga.inc  herc.inc    the three back ends (mode set, column writers,
                    HUD, erase, sprite draw, present)
  sprite.inc        layer/phase loader (expands poses into the claim)
  hud.inc           digits, gauge, lap flash, "BURNING"
  audio.inc         sequencer, engine, SFX (patterned on apps/drmario/audio.inc)
  input.inc         key map, buffer drain
  README.md         (DrMarco ships README.md)
  art/  tracks/  audio/    the committed original sources of section 12.1
tools/excitebike_assets.py  excitebike_audio.py  excitebike_art.py (authoring)
      exbsim.py (reference model + reference renderer, no ROM data)
      exboracle/ (OPTIONAL host NES oracle used only by tests/excitebike_ref.py)
tests/excitebike_front.py  excitebike_ref.py  excitebike_video.py
      excitebike_perf.py  excitebike_flow.py  excitebike_audio.py  exbbench/
tests/unit/t_excitebike_clean.py   fast-tier provenance gate (12.6)
```

Integration checklist (scout-integ 3, kept exact):
- Makefile block after DrMarco's: `excitebike`,
  `excitebikedisk` (FOUR geometries 1440/720/1200/360; `os88disk.py --verify`
  in the recipe because `t_diskverify` walks shipped images only),
  `excitebiketest`; the `$(BUILD)/excitebike.bin:` rule spelled exactly so
  `tools/os88index.py` finds it; one art stamp over every source file of 12.1
  (an edited grid must rebuild the package and disks); NOT in
  `all`, `APPS` or `ALLAPPSFILES`.
- `apps/RETIRED.txt`: `local      excitebike  # SPEC.md 102. ...` and
  `PKG_STEM = {"excitebike": "excbike"}` in `tests/unit/t_retired.py`.
- `tests/suite.py`: SOAK rows only (`needs=("marty","nasm")`,
  `wants=("build/excitebike-360.img","build/os8088-360.img")`); the fast tier is
  at 23.5 of 30 s (scout-integ 0) so no fast row above ~1 s. Any test file not
  in the suite goes in `tests/unit/t_registry.py` `UNREGISTERED` with a reason.
- SPEC.md: `## 102. Excitebike - native racer (`apps/excitebike/`)` appended at
  the end, `### 102.1 Video and memory`, `102.2 Asset files`, `102.3 Simulation`,
  `102.4 Flow and modes`, `102.5 Audio`, `102.6 Validation and performance`;
  written BEFORE the code of each wave. `python3 tools/os88index.py` after every
  SPEC/Makefile change (`docindex` fails `make` when stale).
- README.md make-target line; PERFORMANCE.md `### Set 148 - Excitebike ...`
  with the MartyPC scroll/sprite/frame numbers (the next free set; two equal
  Set numbers fail checkdocs check 5).
- Text is `font_run` only on the desktop splash window; inside the bracket text
  is tile drawing. No transparent `font_char`/`font_str` (textsites ratchet).
- No 86Box config edited (shared `vm/*/86box.cfg` are dirtied by builders; add
  a machine only with its own directory and a fresh uuid, and only in Wave 7).
- Builders name files explicitly in `git add`, never `-A`/`-u`, and never
  broad-`pkill`; MartyPC processes are killed by pidfile only. The plan itself
  is committed by the orchestrator, not by a builder.

## 15. Testing

Everything that needs MartyPC is SOAK; the disassembly (`EXCITEBIKE_REF`) is
needed only by the reference-model rows, which skip without it; nothing here
adds to the 30-second fast tier except sub-second host rows (`t_excitebike_clean`,
the asset compiler's `--selfcheck` at ~0.5 s if it fits, else soak).

1. **Asset compiler / host gate** `tools/excitebike_assets.py --selfcheck`
   (needs nothing external): every budget of 12.5, the tile/pose grid
   grammar (sizes, ink indices, at most 3 layers, <= 260 opaque pixels a pose),
   every piece has a picture, a class and a script that terminates, every
   track compiles and meets 5's authoring rules (lap <= 797, >= 70% plain,
   obstacle spacing), determinism (two runs byte-identical), GFX checksums,
   the segment-overflow refusal, and audio `--selfcheck` (bars, voices,
   durations, loop points, budget). Plus `t_excitebike_clean` (12.6).
2. **Reference model check** `tests/excitebike_ref.py`. Tests only; skips with
   the printed reason `EXCITEBIKE_REF not found` when the disassembly is
   absent; never writes into the repo.
   - TABLE CHECK: a stdlib reader of the disassembly's `.byte` tables (its own
     parser, independent of any compiler) compares the rule tables in our
     `const.inc` with the study source: speed steps and caps, heat targets and
     rates, landing windows, lane geometry, element key-frame codes. Every
     difference must be either fixed or listed by name in a committed
     `tests/excitebike_ref_deviations.txt` with the reason (our tuning is
     allowed to differ; an unexplained difference is not).
   - `tools/exbsim.py` (our reference model, written from the rules, no ROM
     data) is diffed step for step against the study oracle
     (`tools/exboracle/`: a small C 6502 harness, no ROM data; the ROM
     is read at run time from `EXCITEBIKE_REF` into `build/` only) on
     SYNTHETIC piece sequences we feed both (the oracle through the game's own
     track memory, exbsim through the piece grammar), because our tracks are not
     the disassembly's: flat-out A, B and coast curves, the heat curve and stall,
     each element kind at speeds 1.5-3.4 in every lane, landing outcome against
     held pitch, wheelie flip timing, mud/rough/kicker/arrow, crash recovery,
     where our rules are the same rules. Exact per-step equality on speed,
     pitch, lane, height, heat, column, element, or the deviation is listed.
   - the guest against `exbsim.py` (needs NO external directory): the harness
     pokes the held-key byte and breaks at `exb_step_done` (MartyPC debug
     server, docs/MARTYPC-DEBUG.md), reads the same variables, diffs.
   - Stress input: the study replay movies are NOT used (they are ROM-derived
     and do not replay bit-exactly); the AI-invariant test of Wave 4 generates
     its own pad scripts.
3. **Video gates** `tests/excitebike_video.py --adapter vga|cga|herc`:
   - reference-renderer equivalence at randomly chosen scroll positions and
     bike layouts, on a pinned MartyPC model per adapter, pixel-exact;
   - **QEMU by name for anything hardware-scan dependent on VGA** (line
     compare, start-address latching, 0Dh double-scan): MartyPC crops and
     under-models the VGA (docs/TESTING.md item on Mode X capture, scout-perf
     7.9) and QEMU implements real VGA;
   - screenshot gates: title, select, race on each adapter, the CGA colour
     profiles, on 1bpp adapters look at the picture (CLAUDE.md: grey rounds to
     black); `tools/shot.py`, `tools/hercshot.py`;
   - the erase invariant: after a scripted race segment with sprites, a
     full-refresh redraw equals the incrementally maintained visible window.
4. **Frame-rate gate** `tests/excitebike_perf.py`: MartyPC cycle counter
   between successive `exb_present` breakpoints, converted to guest Hz
   (4,772,727 clk/s), over a scripted full first lap at turbo, VGA and CGA
   machines. Hard: Selection A mean period <= 174,763 clk (27.3 Hz) and 99th
   percentile <= 262,144; Selection B mean <= 262,144 and 99th <= 349,525;
   game-speed check: virtual NES steps per real second 60.1 +-2%. Also per
   component counters (`exb_ctr_col`, `exb_ctr_spr`, ...) to print the table of
   section 9 measured beside modelled. Emulator numbers only; a field run
   (docs/FIELD-MACHINES.md) is asked for separately.
5. **Flow gate** `tests/excitebike_flow.py`: menus, race start lights, both
   laps, finish, rank thresholds at the measured boundaries (par 1:16.00,
   d < 4 -> 2nd, 4..7 -> 3rd, >= 8 -> not qualified), second-pass flag and
   next track, last-track repeat, game over to title, attract entry after 560
   idle frames, pause, Esc, heap leak check after leaving.
6. **Audio gate** `tests/excitebike_audio.py`: sequencer duration sums equal
   the decoder's totals; engine step to Hz mapping read from guest state (like
   `tests/skiessound.py` reads `[cs_tone]`); at most one tone call per frame;
   `make test-snd` capture for the speaker path.
7. **Geometry and regression**: `make excitebikedisk` verifies all four
   images; the small floor claim refusal; `make` exit-code check
   (`make > log 2>&1; echo EXIT=$?`, then `grep -c "Error" log`); the final
   `python3 tools/os88test.py full`.

Decision gates that can change the design (each has its fallback DECIDED here):

| gate | measured in | if it fails |
|---|---|---|
| G1 VGA line compare + 0Dh start address on QEMU and MartyPC | Wave 2 spike | in-stream HUD on VGA (section 4.4) |
| G2 VGA sprite draw <= 20k clk per bike incl. erase | Wave 2 spike | shrink phases to {0,4}; compile hot poses (Wave 6 pulled forward) |
| G3 Selection A >= 27.3 Hz VGA and CGA | Wave 3 | data-driven to compiled sprites; drop extras; n governor still holds 18.2 |
| G4 Selection B >= 18.2 Hz | Wave 4 | reduce to two AI riders, then grey B on `CPU_8086` |
| G5 CGA column + HUD inside the blank | Wave 2 | write columns before S and accept one torn frame at 8 px, or split HUD strip |
| G6 Hercules >= 18.2 Hz | Wave 6 | refuse Hercules (D3) |

## 16. Waves

Each wave ends with its acceptance commands passing, the SPEC.md 102
subsection for that wave written first, `python3 tools/os88index.py` clean,
`make` exit 0 with the fast tier green, and files added by name.

### Wave 1 - original art/sound compilers, skeleton, build plumbing, splash
Scope: `tools/excitebike_art.py` (authoring generator) and the FIRST committed
art sources (`art/palette.json`, `tiles.txt`, `pieces.txt`, `top.txt`,
`poses.txt`, `font.txt`, `splash.json`, `README.md`, contact sheets for review),
two starter track files (t1, t2; t3-t5 arrive in Wave 4), `tools/excitebike_assets.py`
(compiler, `--selfcheck`, budgets of 12.5) with the audio compiler
`tools/excitebike_audio.py` reduced to the effects file for now; the package
skeleton (header, icon, entry, splash window per SPEC.md §100's front end, the
loading screen, refusal sentences, Alt+Enter, Esc); Makefile block, four disk
geometries, `RETIRED.txt`, `t_retired`, `suite.py`/`UNREGISTERED`,
`t_excitebike_clean`, SPEC.md 102 skeleton + 102.1/102.2, `os88index.py`,
README line, `EXBSPL.*` splash art from `splash.json`. NOTHING external is
read.
Acceptance:
- On a tree with no disassembly reachable (`EXCITEBIKE_REF=/nonexistent`),
  `make excitebikedisk` exits 0 and the four `os88disk.py --verify` are clean.
- `python3 tools/excitebike_assets.py --selfcheck` passes and prints every
  budget line of 12.5 with its actual number; run twice, the two output trees
  are byte-identical (`diff -r`).
- `python3 tests/unit/t_excitebike_clean.py` passes.
- Contact sheets exist (`build/excitebike-art/tiles.png`, `poses.png`,
  `pieces.png`) and were looked at: side-on rider, ramps distinguishable by
  shape, readable in 2 and 4 colours.
- `make > /tmp/exb-make.log 2>&1; echo EXIT=$?` is 0 and `grep -c "Error"` is 0;
  the log ends with `os88test: N passed, 0 failed`.
- `python3 tools/os88test.py soak -k 'excitebike*'` (front row): on a MartyPC
  VGA and CGA XT the package opens from B:, the splash paints native pixels,
  Enter shows the loading screen then a placeholder, Esc returns, heap is
  back to baseline; EGA/Hercules print the refusal sentence.

### Wave 2 - video and horizontal scroll at the target rate
Scope: SPEC 102.1 in full (layout, spiral, memory map); `vga.inc`, `cga.inc`,
`video.inc`; band/top dictionaries and the column writers with the plain-pair
skip lists; page order and invariants; HUD (line compare / in-stream); pacing
governor; a scrolling TEST TRACK (the compiled `cid[]` of our track t1 at a scripted speed,
one stationary bike, ticking HUD digits); the sprite spike (data-driven
blitter, layer/phase loader) with counters; `tools/exbsim.py` reference
renderer; PERFORMANCE.md Set 148 stub.
Acceptance:
- `python3 tests/excitebike_video.py --adapter vga` and `--adapter cga` pass:
  reference-renderer equivalence at 20 random positions, both adapters,
  pixel-exact; QEMU line-compare/start-address check passes (G1) or the
  fallback is switched on and recorded in SPEC.
- `python3 tests/excitebike_perf.py --scroll` on MartyPC: scripted 3.4 px/step
  scroll with one bike: mean period <= 87,381 x 2 clk (27.3 Hz) VGA and CGA,
  and the printed per-component clocks are within 25% of section 9 (G2, G5);
  sprite draw + erase per bike <= 20,000 clk VGA, <= 15,000 CGA.
- Long-scroll check: 1,637 columns with no artefact, no page overrun, CGA ring
  wrap exercised (S passes 8,192).
- Pacing governor test: a busy-loop injected into a frame raises `n` within
  one frame and lowers it only after 64 quiet frames.

### Wave 3 - rider physics, controls, obstacles, ramps, crashes, HUD
**STATUS: DONE (wave 3 implementer, 4.10 and SPEC.md 102.3/102.6 record what it measured and changed).**
Open items it hands on: the CGA does not composite overlapping bikes and `XB_MAXBIKES` is 4 (wave 4); the study-game oracle diff of section 15 item 2 is not built (the table check and the guest == exbsim equality are); no field numbers.
Scope: SPEC 102.3 and `const.inc`; `tools/exbsim.py` physics + element scripts
FIRST (and the optional oracle diff where the disassembly is present), then
`sim.inc`, `world.inc`, `input.inc`, `hud.inc`; the remaining original poses
(the Wave 1 set is the level pose plus placeholders; the full 24 land here,
including tumble, dust, shadow, gauge, "BURNING" text); Selection A over a full
lap of t1 and t2 with heat, overheat stall, crash recovery.
Acceptance:
- `python3 tests/excitebike_ref.py`: guest against exbsim for the scripted
  inputs, exact per-step equality (always runs); the table check and the
  oracle diff of section 15 item 2 pass with every deviation named in
  `tests/excitebike_ref_deviations.txt`, or print `SKIP: EXCITEBIKE_REF not
  found` when the disassembly is absent (both outcomes exit 0).
- `python3 tools/exbsim.py --run-track t1` and `t2` finish at turbo (the
  authoring rule of 5).
- `python3 tests/excitebike_perf.py --lap`: Selection A full lap at turbo,
  mean <= 174,763 clk, p99 <= 262,144, VGA and CGA (G3); sim <= 4,500 clk a
  step measured by counter.
- `python3 tests/excitebike_video.py` still passes with the player moving over
  ramps (erase invariant), 3 screenshots per adapter (flat run, mid-ramp,
  crash) inspected.

### Wave 4 - race flow, timer, rank, AI, modes, attract
**STATUS: DONE (wave 4 implementer; 4.11 and SPEC.md 102.4 / 102.6 record what it measured and changed).** Handed on: the CGA races two opponents (G4's recorded fallback); no field numbers; opponents pop in and out at the edges (wave 6's clipping blitter); `M` and all sound are wave 5.
Scope: SPEC 102.4; `game.inc` flow and records, `front.inc` title/select/track/
results/best times, lap flash and finish gate, rank and qualification with the
flag-1 pass and last-track repeat, `ai.inc` (three riders, collisions, marker
rider, respawn), attract demo, pause; our tracks t3-t5 authored and their pars
derived by `exbsim.py --run-track`; `EXBTRACK.DAT` custom stream.
Acceptance:
- `python3 tests/excitebike_flow.py` passes every item of section 15 item 5.
- `python3 tests/excitebike_perf.py --selfb`: Selection B mean <= 262,144 clk,
  p99 <= 349,525, VGA and CGA (G4), with three AI riders on screen most of the
  lap; or the recorded fallback applied.
- AI invariants over a 5,000-step run: speed <= caps unless kicker, no rider
  stuck, respawns occur, no sim fault; a seeded pseudo-random pad script
  (generated in the test, not taken from any recording) as stress input
  without invariant violation.

### Wave 5 - audio
Scope: SPEC 102.5; `tools/excitebike_audio.py` complete, `audio.inc`; the
original songs composed as `audio/*.mml` and effects as `sfx.txt` (section 11),
engine pitch, SFX, pause, claim/refusal path, speaker octave-up constants.
Acceptance:
- `python3 tools/excitebike_audio.py --selfcheck` and
  `python3 tests/excitebike_audio.py` pass: `EXB.SND` <= 3,072 B, song
  duration sums equal the compiler's, loop points valid, engine mapping,
  <= 1 tone call per frame, refusal falls back to the speaker.
- `make test-snd` capture verified with `tools/sndcheck.py` for the engine and
  one song; `python3 tests/excitebike_perf.py --lap` repeated with audio on:
  the Wave 3 gate still holds and audio adds <= 4,000 clk a frame at peak.

**STATUS: DONE (wave 5 implementer; SPEC.md 102.5 and 102.6, PERFORMANCE.md Set 151 record what it measured).**
Handed on: the capture is MartyPC's (`MARTYPC_WAV`), not `make test-snd`'s QEMU one, because a script cannot reach
into a game's fullscreen bracket there; the plan's engine vibrato is not built (it would change the pitch every
frame against the rule that keeps the frame cheap); **the "<= 4,000 clk a frame at peak" gate is met for the work in
the frame (p99 2.5k) and not for the whole (p99 4.5-4.7k: one `OSAPI_SND_TONE` is 2.1k), the call being made in the
idle wait so the period does not grow**; the engine's speed law is a 76-byte table in `EXB.SND`. FM has been heard
only by the sound driver's registers (`opl_b0` key-on bits), not by a capture; and nobody has listened to any of it.

### Wave 6 - Hercules go/no-go, performance tuning
Scope: `herc.inc` per section 4.6 (G6); compiled hot player poses; a clipping
blitter for edge riders; the optional VGA 1-px pan behind a measured gate
(only if 8 phases fit, else stays off and SPEC says why); PERFORMANCE.md Set 148
filled with the measured tables.
Acceptance:
- `python3 tests/excitebike_video.py --adapter herc` reference-equivalent and
  `tests/excitebike_perf.py --herc --lap` >= 18.2 Hz, or Hercules refuses with
  the sentence and the Wave 1 refusal test is kept.
- No regression of the Wave 3/4 perf gates (rerun both).
- Set 148 numbers cite the tests that produced them.

**STATUS: DONE (wave 6 implementer; SPEC.md 102.7 is the contract, PERFORMANCE.md Set 148 (wave 6 block) and Set 152 the numbers).**
Hercules is a GO (G6): `herc.inc` is the card side of the shared CGA engine (a game pixel is two card pixels, 45 columns of
16, four banks, R6/R7 programmed to show the 200-line field), `EXBH.GFX` has `EXBC.GFX`'s layout with pixel pairs, the Selection A
lap holds 50.3 Hz against the 18.2 Hz gate, Selection B with two opponents 21.6 Hz (one opponent 27.1 Hz, three 14.8 Hz: over
both gates, as on the CGA). Handed on: the hot poses are **compiled** (CGA/Hercules 9 poses x 2 phases, VGA 3 poses x 4 phases; the plan's estimate of
12 KB a pose across 8 phases was for a VGA that draws with the ATC pan, which stays off); the edge riders are drawn by clipping
blitters on all three adapters (the plan's "AI riders pop in at the edge" is retired); the VGA 1-pixel pan was measured and stays
off (eight phases are 58 KB against the 44 KB claim, the window would need a 42-column pitch, and the compiled poses would need
another ~24 KB; MartyPC does model the pel-pan register, so it is testable). Not built: the Hercules's skip lists (an entering
column writes all 200 lines), a compiled draw for the poses outside the hot set, and any field number.

### Wave 7 - polish, all adapters, all disk geometries, documentation
Scope: art polish pass (second look at every contact sheet on the three
adapters); all four geometries booted on MartyPC/QEMU
(`excitebikedisk` images, 360KB on the XT, 1.2MB on `tests` machine where one
exists); EGA path via `make test VIDEO=ega`; the five-theme palette pass and
banner flash; low-memory refusal; README.md, SPEC 102 final, INDEX regenerated,
one optional 86Box machine directory with its own uuid; docs review.
Acceptance:
- `python3 tools/os88test.py full` green (the pre-merge gate) and
  `python3 tools/os88test.py soak -k 'excitebike*'` green.
- `make excitebikedisk` in each geometry: `os88disk.py --verify` clean and a
  boot-and-launch on 360KB and 1.44MB.
- `python3 tools/checkdocs.py` clean; `docs/INDEX.md` `--check` clean.

**STATUS: DONE (wave 7 implementer; SPEC.md 102.8 is the contract, PERFORMANCE.md Set 152's wave-7 block the cost).**
The art pass found one real defect and fixed it: the lane dashes were invisible on the CGA and the Hercules (white, the
light dirt and the dirt all map to one ink), so a tile can now carry `mono=9>8` and `Art.rows(i, kind)` is the one place
that says what a tile looks like on an adapter (compiler, reference renderer, tests and sheets all ask it). The banner flash
is a VGA DAC rewrite once a frame (`xb_banner`). The low-memory refusal (`NOT ENOUGH MEMORY`, `XB_ERR_MEM`) found a bug that had
refused every machine that could not give the 28 KB sprite claim (a `cmp`'s borrow read as a load error); the fallback now
runs, and a row runs it. All four geometries verified by an independent FAT12 walk; 360KB (VGA XT), 720KB (Hercules XT) and
1.44MB (VGA XT) boot and reach the race; the 1.2MB floppy is walked and not booted (no MartyPC machine has a 5.25" HD drive).
EGA stays refused (section 4.7). `vm/xt-excitebike` and `make xt-excitebike` exist, unexercised. Not done: any field run,
art for the fullscreen title and results screens, and a real EGA.

## 17. Risks and open questions

1. **Sprite draw dominates** (26k of 52k). Fallback ladder in G2/G3. Do the
   sprite spike before any game logic (Wave 2), on QEMU for correctness and
   MartyPC for cycles.
2. **VGA line compare / 0Dh double-scan value** unproven on both emulators;
   G1 with a decided fallback.
3. **MartyPC VGA has no wait states** and no beam hazards; every VRAM number
   is a lower bound (x1.0-1.6 on real ISA VGA, 1.78x measured for a CGA word
   write on a real 5150). Ask for a field run; never promise 27.3 Hz on iron.
4. **Skip-list correctness** rests on the sprite-clean invariant; the
   equivalence gate is what trusts it.
5. **Music quality**: the tunes are composed from text scores with no ear in
   the loop; Wave 5's `--selfcheck` prints structure, and the `make test-snd`
   capture is the only audible check. Expect one revision pass after a human
   listens.
6. **The Selection B fallen-rider set-piece** is optional; keep only if it fits
   the budget.
7. **SPEC number 102** may collide with a parallel branch; renumber before
   merge (checkdocs fails on a duplicate heading).
8. **Provenance**: the tree stays ROM-free and derivation-free; the fast-tier
   `t_excitebike_clean` enforces it; the optional oracle reads the disassembly
   only at test time and caches under `build/`. Risk: original art that
   drifts toward recognisable NES sprites; mitigated by authoring from the
   written brief in `art/README.md`, never from screenshots, and by the review.
9. **Hill/ramp curves** are our own tuning; the feel is checked against the
   study oracle where present and by the "every track completable at turbo"
   rule everywhere.
10. Design mode as a later wave is possible: its data path already works.
11. **Display name.** The package identity is `EXCITEBIKE` (the task's name).
    DrMarco shows that this project prefers a distinct on-screen name; if the
    user wants one, only the header string, the splash wordmark and the README
    change. Decision left to the user; the plan does not block on it.
12. **Original art quality** is bounded by the authoring route (procedural
    generator, optionally a generated master via the 1942 route). Wave 1 ships
    a serviceable first set; Wave 7's polish pass is where it is judged on all
    three adapters.

## 18. Wave summary (the machine-readable copy is the workflow's return value)

| wave | title | one-line acceptance |
|---|---|---|
| 1 | Original art/sound compilers, skeleton, plumbing, splash | clean-tree `make excitebikedisk` + four `--verify` + `--selfcheck` + splash on VGA and CGA, no external file read |
| 2 | Video and scroll engine | pixel-exact reference renderer on VGA/CGA, scroll >= 27.3 Hz on MartyPC, sprite <= 20k/15k clk |
| 3 | Physics, controls, obstacles, HUD | guest == exbsim per step, lap >= 27.3 Hz, table check (skips without the disassembly) |
| 4 | Race flow, AI, modes, attract | flow gate, Selection B >= 18.2 Hz, AI invariants, tracks t3-t5 |
| 5 | Audio | `EXB.SND` <= 3 KB, <= 1 tone call/frame, `make test-snd` capture, no perf regression |
| 6 | Hercules go/no-go, tuning | Herc >= 18.2 Hz or refusal, no regression, Set 148 filled |
| 7 | Polish, all adapters/geometries, docs | `os88test.py full` and soak green, all four images verified, docs gates clean |
