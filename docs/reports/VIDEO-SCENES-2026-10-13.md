# A palette a scene against one palette: Gas Gas Gas 40-114 s

**A measurement, not a description.** Taken 2026-10-13 on branch
`video-palette`, on the working tree on top of `512900af` that became the
commit landing SPEC.md 98.1.3.2.3, on a 4-core Xeon container with ffmpeg
6.1.1. True of that tree's encoder and its `286-fast` profile; not
maintained against later trees. docs/plans/VIDEO-PALETTE-PLAN.md section 11
is what it means.

**The clip**: the owner's Gas Gas Gas (not committed), 40 to 114 s, at
`--preset vga4-full` (640 x 360 on mode 12h), 30 fps, PCM8 at 11,025 Hz,
`--palette colour16`, `--profile 286-fast`. The scene pass finds 22 scenes
(21 changes).

| file | arms | base stream | cut of 2,217 | by the disk | broken | flickering back |
|---|---|---|---|---|---|---|
| GASONE | one sixteen, hold 24 | 230.4 KB/s | 237 | 54 | 3.1 s | 696 |
| GASSCN | `--scene-palettes`, hold 24 | 284.8 KB/s | 517 | 296 | 3.8 s | 460 |
| GASS40 | `--scene-palettes`, hold 40 | 289.7 KB/s | 540 | 344 | 3.4 s | 265 |
| (GASMAT) | GASSCN with each scene's order matched to the last | 287.5 KB/s | 570 | 341 | 3.9 s | 429 |

**At the scene changes** (a one-off decode of each file, record by record
as the player draws it):
- the share of the canvas left standing by the change's first frame - not
  black, and the same value as the frame before: **33.1%** on GASONE
  across GASSCN's own 21 changes, **38.6%** on GASSCN, 39.5% on GASMAT. A
  single-palette file tears at a cut just as much; what the table adds is
  that the part left over is shown in the NEW scene's colours.
- how far those pixels' colour moves when the sixteen change: a mean of
  **15.2 dE** (CIELAB 1976) on GASSCN, whose sixteen are each sorted
  darkest first, and **17.9** on GASMAT, whose order was matched to the
  last scene's by an assignment on CIELAB distance - so the sort already
  does what a matching would, and the matching is not kept.
- **0 of 21 drawn whole on any arm, and on `--profile lossless` as well**
  (20 s, 40-60, 2 changes): a record holds 32 KB and a whole 640 x 360
  four-plane canvas is several times that, so a change of the whole picture
  is three or more frames on every profile, and the switch is drawn
  across them.

**What the table costs in bytes**: 40 to 60 s lossless, hold 24, asks
256.7 KB/s with one sixteen and 270.6 with three scenes' (+5%). Over the
whole 74 s on `286-fast` the per-scene files ask past what the disk gives
(296 and 344 frames cut by it, against 54), so their base stream is the
disk's and not the picture's, and a larger hold does not bring it down
(hold 40 asks as much as hold 24 and flickers less).

**Caution**: none of this sees what the owner's eye does - whether a third
of the old scene in the new colours for a tenth of a second is noticed,
and whether each scene's colours are worth the cut frames. That is the
files' question.
