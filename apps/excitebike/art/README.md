# EXCITEBIKE art: how it was made

Everything in this directory, in `../tracks/` and in `../audio/` is **original
work for this project**, kept as plain text so a diff shows exactly what
changed. **None of it contains, or was derived from, any NES ROM, CHR data,
disassembly, screenshot or recording.** The package identity is `8BitBike`
and the game is a motocross racer "in the spirit of" the genre - a side-on
course with lanes, ramps and hurdles, a rider on a bike, a temperature bar -
which describes rules and subject matter, not pixels. The wordmark and splash
are ours and do not reproduce anyone's logo.

`tools/excitebike_assets.py` compiles these files into the game's binary art;
`tools/excitebike_art.py` is the offline authoring tool that wrote the first
version of the text files. After generation **the text files, not the
generator, are authoritative**: edit them by hand and rebuild.
`python3 tools/excitebike_art.py --check` says whether a file is still the
generator's exact output (a hand edit is allowed; it just makes the answer
"differs").

## Authoring brief (what the artist pass worked from)

Written down before any pixel, and the only reference the art was drawn from:

* Side-on view, dark outline, high contrast, readable at 320 x 200.
* Ramps, hurdles, mud and arrows must be told apart by **shape and pattern, not
  by colour alone**: the 4-colour CGA and the 2-colour Hercules see the same
  silhouettes (`build/excitebike-art/tiles-cga.png`, `tiles-herc.png`,
  `pieces-cga.png`).
* Every bike pose fits a 24 x 24 box, has at most three ink layers besides the
  outline, and at most 260 opaque pixels (the compiler refuses more).
* No tile or pose is traced from another game.

## Files and how each was made

| file | what it is | how it was made |
|---|---|---|
| `palette.json` | the 16 game palette slots, five course themes (DAC overrides), the two CGA ink maps and the Hercules levels | chosen by hand: a grass/dirt/sky set with a red, yellow and orange accent |
| `tiles.txt` | 70 named 8 x 8 tiles: grass, tufts, banks, dirt and lane dividers, hedge, rough, mud, hurdles, arrows, the striped kicker, ramp and hill profile and body tiles, finish and start gate, crowd, banner, HUD gauge | **procedure** - `tools/excitebike_art.py` (`make_tiles`): flat fills, deterministic speckle from a private LCG, wedge profiles computed from slope end-heights |
| `pieces.txt` | 61 band-dictionary columns (14 tile names for tile rows 8-21 plus a hedge pair), 36 pieces built from them, 21 element scripts (`ANGn` pitch target, `DEFERn` wait, `Hn` ground height) | procedure for the columns (`make_columns`, `make_pieces`); the scripts are authored by hand in the generator's `SCRIPTS` table as our own rules |
| `top.txt` | the 512 x 64 crowd and banner picture as 64 x 8 tile names | procedure (`make_top`) |
| `poses.txt` | 24 bike-and-rider poses and 4 small sprites (shadow, three dust puffs) | **procedure** - shapes (tyre rings, spokes, frame segments, tank, engine, a rider blob with a helmet and visor) rasterised by point sampling, rotated about the rear axle for the pitch poses, outlined by 4-neighbour dilation (`bike_shapes`, `rider_shapes`, `pose`, `lying_pose`, `walking_pose`) |
| `font.txt` | 64 glyphs = ASCII 32-95 | a **hand-written 5 x 7 alphabet** in the generator (`GLYPHS5`), one blank row and column of leading |
| `splash.json` | the desktop splash and help layers (sky bands, sun, ramp, rider pose, wordmark, chequered border) | authored by hand as vector layers; the compiler rasterises the same layers at 432 x 264 and, for the compact desktop, at 432 x 132 |
| `../tracks/t1.trk` .. `t5.trk` | five courses in the piece grammar | t1 and t2: a scratch generator wrote the first draft from a fixed seed; t3-t5: `tools/excitebike_tracks.py` (a seeded walk over the piece catalogue under the compiler's own rules, with a difficulty ramp); the files are the record. `par` and `par2` (first and second pass) are DERIVED: the reference simulator's turbo time + 8% (`tools/exbsim.py --run-track`, `tools/excitebike_tracks.py --pars`) |
| `../audio/sfx.txt` | nine sound effects as (frames, Hz) records | composed by hand; songs (`*.mml`) arrive in wave 5 |

No generated or imported bitmap master is used, so there is no `masters/`
directory and no prompt log. If a later wave adds one (the 1942 route,
`apps/1942/art/README.md`), the master and its prompt are committed beside it
and this table gains a row.

## Contact sheets

`make excitebike-art` writes, into `build/excitebike-art/`:
`tiles.png`, `tiles-cga.png`, `tiles-herc.png` (every tile in three colour
depths, numbered by tile id), `poses.png` (every pose, three rider colours,
CGA and Hercules), `pieces.png`, `pieces-cga.png` (every piece as its columns),
`top.png`, `scene-vga-*.png`, `scene-cga-*.png` (a mock of the race screen from
the compiled dictionaries) and `splash-*.png`. Look at them after any art edit.

## Art rules the scroll engine adds (SPEC.md 102.1)

Speed is a drawing rule here.  The scroll engine writes an entering column only
where it differs from what the sheared memory already holds - line l of the
entering column against line l + 1 of the column 40 to its left (line l + 2 on
the CGA) - so **a plain column is cheap exactly when its pixel rows come in long
vertical runs.**  The generator therefore draws:

* the lawn as VERTICAL BLADES: a fixed colour down each pixel column of the
  tile, every row identical, the same tile all the way down (`grass_b` is the same
  picture under its own name);
* the track surface FLAT (`dirt_a`, `dirt_b`) with the lane dashes as its only
  detail: the texture the eye needs to tell surfaces apart lives in the rough
  and mud pieces, where the rules change;
* the hedge and the bank in vertical streaks, and the three plain columns
  identical apart from the tuft row (8 lines) - a line of one equals the next
  line of another everywhere else.

`python3 tools/excitebike_assets.py --selfcheck` prints what that buys (the lines
of a plain band pair that must be written: 33 of 128 in 7 runs, against all 128).
The crowd and banner stay as drawn: their spectators are the picture, and the
top strip's lists write 55 of 64 lines.  A later art pass may add texture back,
and each pixel of it costs a changing line: measure with the selfcheck first.

## The Hercules (SPEC.md 102.7)

`palette.json`'s `herc` list gives every palette slot one of three levels (0 black, 1 the
mid grey, 2 white) and a game pixel is two card pixels wide, so a level is a PAIR: `00`,
`11`, and for the mid grey a single lit pixel - a tile's alternating `10` / `01` by
(row + column), a pose's and a glyph's always `10` (a pose lands on any row, a tile on
a fixed one, so only the tile can afford a checkerboard).  `8BBH.GFX` is `8BBC.GFX`'s layout
with pairs where the CGA has 2-bit inks: same record sizes, same tile bytes a row.  The levels
were set by looking at `scene-herc-*.png`: the ground is mid grey, so the rider is WHITE (red,
skin) with black outline and black wheels (yellow), not mid grey on mid grey - the first cut
had the red at mid level and the rider was an outline.  The same slots serve the tiles (an
obstacle's red is white too), and the picture stays readable by silhouette as rule 1 above
demands.
