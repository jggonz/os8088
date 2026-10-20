# The order of a scene's sixteen: what it costs, and the hold across a cut

**A measurement, not a description.** Taken 2026-10-13 on branch
`video-palette-enhancements`, cut from `video-palette` at `7fa8729`, with
`tools/os88vorder.py` as committed beside this file, on a 4-core Xeon
container (2.1 GHz) with ffmpeg 6.1.1. True of that tree's encoder and its
`286-fast` profile; not maintained against later trees.

**The owner's question**: at a scene change the new palette is loaded and
every pixel keeps its VALUE. Could the new sixteen be ORDERED so that a
value the old screen shows already means, in the new colours, what the new
scene wants there - fewer pixels to rewrite, and less of a flash while the
cut is drawn? And what else cuts the data rate and the decode that make the
encoder cut frames?

**The clip**: the owner's Gas Gas Gas (not committed), 40 to 114 s, made as
GASSHOTW was: `--preset vga4-full` (640 x 360 on mode 12h), `--profile
286-fast`, `--palette colour16 --scene-palettes`, `--vga4-hold 24
--vga4-hold-score 60 --vga4-plan-cap 64`, PCM8 at 11,025 Hz, `--avg auto
--bank auto`. 2,217 frames, 55 scenes. The baseline encode below is
GASSHOTW's stream: the two files differ in 238 bytes, all of them the title
and the options record (the source's file name).

## 1. Why the order matters at all

A VGA4 pixel's value is its colour's SLOT, and its four bits are the four
planes (SPEC.md 98.1.3.2). A change from colour *a* to *b* writes the planes
where the slots' bits differ, so the ORDER decides what a change costs:
slots 7 to 8 is all four planes, 7 to 5 is one. `colour_kmeans` sorts a
scene's sixteen darkest first, and a Knoll pattern dither moves a pixel
between colours NEAR in lightness - exactly the pairs darkest first puts at
the far corners of the cube (3/4, 7/8, 11/12).

The dither picks every colour by its colour, never by its slot (Knoll's
candidates are sorted by luma; the hold, the plan cap and the budget's error
are all in RGB), so a scene's sixteen in another order is THE SAME PICTURE
in other slots. That makes the order a free parameter of the encode.

## 2. On the decoded file (no re-encode)

`os88vorder.py decode` keeps every frame's index map. Over GASSHOTW:
- **The 13 frames after each of the 54 cuts carry 35% of the file's video
  bytes** (7.0 MB of 20.2 MB). A shot cut redraws ~85% of the canvas
  however the colours are ordered: the old screen and the new scene are
  uncorrelated pixel for pixel.
- **The owner's idea at the cut alone**, an order chosen to maximise the
  pixels whose old value is already the new scene's (an assignment on the
  old-value x new-value co-occurrence, black fixed): **8.1% fewer pixels to
  rewrite, 5.8% fewer stores** across the cuts.
- **The order chosen for the planes inside each scene** (a local search on
  the planes each scene's own changes write): **16.9% fewer stores** over
  the 1,630 frames not in a cut's first 13 - re-encoded losslessly through
  the shipped `vga4_subs` and `record` (SUB2's map), **14.1% fewer record
  bytes** (one frame pair in eight). That is the bigger lever by far.
- **Both at once** (the intra-scene changes plus the cut from the last
  scene's screen, one objective): -12.7% stores on sampled frames, intra
  -14.6%, cuts -7.0%. They do not conflict: the cut is one more term in the
  same search.
- **The flash.** Darkest first already keeps like colours in like slots:
  the old screen shown in the new sixteen is 11.5 dE (CIELAB 1976, a pixel's
  mean) from what it was, 21.5 from what the new scene wants there. An order
  chosen for stores alone takes the first to **19.4** - the old picture
  scrambles at the switch. A third term weighs it: `--lam` times that
  distance, against the colour the pixel WAS (`--guard old`) or the new
  scene's (`--guard target`):

| guard, weight | stores, sampled | from the old colour | from the new scene's |
|---|---|---|---|
| darkest first | 0 | 11.5 dE | 21.5 dE |
| none | -12.7% | 19.4 | 21.0 |
| target 0.05 / 0.2 | -12.0% / -12.7% | 18.4 / 17.9 | 16.4 / 15.7 |
| **old 0.05 / 0.2** | -11.6% / -10.0% | **9.7 / 8.3** | 21.1 / 21.7 |
| old 1 | -5.5% | 7.6 | 21.9 |

  Guarded by the old colour, the order beats darkest first on BOTH
  distances and still saves a tenth of the stores.
- **The order has to come from the scene's own changes**: chosen from the
  palette alone (each pixel's two nearest colours, as a dither would mix
  them) it saves 5.2% where the real changes save 15.4%. So the encoder
  needs a scene's dithered frames before it picks the scene's order.

## 3. Through the encoder

`os88vorder.py encode` runs os88venc with each scene's sixteen permuted by
an order chosen off the BASELINE's decode - a second pass, the same clip,
the same options. With `--hold-across`, each scene's ditherer starts from
the last scene's screen instead of from nothing: a pixel's value is a slot,
so under the new sixteen it means the new colour, and the colour hold keeps
any pixel whose slot's NEW colour is already within 24 of the new source -
the owner's idea in full, since the order now decides how many such pixels
there are. Every row is the encoder's own report:

| file | stream | CPU mean | cut of 2,217 | pixels wrong | error as seen | worst second | broken | cuts drawn whole |
|---|---|---|---|---|---|---|---|---|
| baseline (GASSHOTW) | 280.2 KB/s | 185.5% | 466 | 4.69% | 0.59% | 7.04% | 4.4 s | 0 of 54 |
| order, no guard (intra) | 263.5 | 168.2% | 389 | 4.41% | 0.67% | 9.47% | 4.9 s | 0 |
| order, no guard (+ cut) | 264.2 | 169.0% | 400 | 4.27% | 0.72% | 10.50% | 4.3 s | 1 |
| order, guard target 0.2 | 262.3 | 171.0% | 415 | 4.32% | 0.71% | 9.88% | 4.2 s | 0 |
| **order, guard old 0.2** | **264.3** | **172.7%** | **406** | 4.39% | **0.57%** | 7.37% | **4.0 s** | 1 |
| order, guard old 0.05 | 264.1 | 170.7% | 415 | 4.33% | 0.60% | 7.74% | 4.4 s | 0 |
| hold across, darkest first | 288.2 | 188.4% | 449 | 4.11% | 0.55% | 6.65% | 4.2 s | 7 |
| hold across, guard target 0.2 | 270.0 | 174.1% | 394 | 3.88% | 0.68% | 9.55% | 4.1 s | 8 |
| hold across, guard old 0.05 | 271.9 | 173.8% | 392 | 3.90% | 0.56% | 7.75% | 4.2 s | 7 |
| **hold across, guard old 0.2** | **272.9** | **176.3%** | **388** | **3.93%** | **0.52%** | **6.69%** | **3.9 s** | **8** |

Every cut on every row is the per-frame CPU ceiling's (the disk cut none),
so stores - which are what the 286's decode spends - turn straight into
frames kept.

What it says:
- **Order guarded by the old colour, weight 0.2, is free**: -5.7% of the
  stream, -6.9% of the mean decode, 60 fewer frames cut (-13%), and the
  picture equal or better on every score but the worst second (+0.3
  points). It changes no player byte and no format: the table already
  carries each scene's sixteen in whatever order they come.
- **With the hold across the cut it is better on EVERY score at once**:
  -2.6% of the stream, -5.0% of the decode, **78 fewer frames cut (-17%)**,
  8 of 54 scene changes drawn whole against none, 62,150 pixels of the last
  scene left by a change against 89,178, error as seen 0.52% against 0.59%,
  the worst second 6.69% against 7.04%, 3.9 s visibly broken against 4.4.
  The hold spends some of what the order saves (+3% of the stream alone)
  on picture.
- **An order without the guard is worse to look at than it is cheap**: the
  error as seen rises (0.59% to 0.67-0.72%) and the worst second is a run
  of four six-frame scenes at 0:28 - the leftovers of each cut shown in
  colours unrelated to the old picture's, three times in a second. **The
  target guard does not fix it**: its reference is the scene "once
  settled", 12 frames on, and a six-frame scene is never settled, where the
  old colour is always known. The weaker old guard (0.05) gives back the
  stores it saved on sampled frames as worse cuts.

## 4. The other ideas, measured on the decoded file

- **VRAM latch copies** (write mode 1, all four planes a byte, a block of
  the last screen moved by a whole byte across and a multiple of 4 down so
  the 4 x 4 dither cell stays in step): per 64 x 8 block, the best of 45
  shifts against none, each copy charged as 4 stores - **4.3% fewer
  stores**, median 0.4% a frame, 7.9% at the 90th percentile. This is
  driving footage, whose motion is perspective rather than a pan, and a
  hold-dithered picture; it needs a list type and a player path, so it is
  priced here and not built. It stacks with the order (it is a different
  set of bytes).
- **A coarse cut**: the cut's first frame as each 8-pixel byte's commonest
  colour, then the detail - the coarse picture is **66% of the bytes of the
  whole one** (8 x 1 cells; 8 x 2 is no better), and coarse plus detail is
  114% of whole. It buys a WHOLE picture a frame sooner at a cut, which is
  where 35% of the bytes are, at the cost of one blocky frame; the owner's
  ruling that a change while the scene changes is not seen is the case for
  it. Not tried through the budget.
- **Ordering for fewer colours** is `vid.PLANE_CODES` already, for 2, 4
  and 8.

## 5. To build it

The measurement chose each order off a decoded first encode. In the
encoder it is one buffer: a scene's dithered targets held until the scene
ends (the longest here is 155 frames, 36 MB at 640 x 360), their changes
counted, the search run (milliseconds: 105 swaps a round over a 16 x 16
table), the targets relabelled through a 16-byte table and the scene's
sixteen permuted to match before its entry goes into the table. The hold
across is four lines of `SceneDitherer.settle`. Neither reaches the player,
the format or the kernel. The search should be re-checked on the
DITHERED targets, not the decoded file's (which the budget cut), and on
the other clips VIDEO-PALETTE-PLAN measured.

## 6. What the owner saw, and why (2026-10-13, later)

**The owner, on GASORDH and GASORD**: both work, and both have a side
effect, so both are OPTIONS and not defaults.
- **The hold across a cut keeps leftovers**: the last scene's picture stays
  where its slot's new colour is near enough, and fades out over the new
  scene's frames - valid, and an obvious pattern from the last scene is
  seen standing in the next.
- **The order flashes more**, which the averages of section 2 did not
  predict.

**Why the order flashes.** At the switch the old screen is shown in the
new sixteen, slot for slot. Darkest first maps the old picture onto the
new colours in LIGHTNESS ORDER, so everything shifts a little together and
reads as a tint. The guarded order of section 3 minimised the AVERAGE move
(8.3 dE against darkest first's 11.5), and an average is met by matching
most slots nearly exactly and leaving a few on far colours - and those
pixels jump. Decoded off GASORD against GASSHOTW, the share of the screen
moved more than 25 dE at the switch:

| scene | darkest first | the guarded order |
|---|---|---|
| 12 | 10.7% | 23.7% |
| 13 | 0.5% | 13.5% |
| 16 | 0.5% | 10.5% |
| 29 | 7.3% | 14.7% |
| 31 | 5.1% | 17.2% |

while pixels shown in a colour that is neither the old picture's nor the
new one's, over the 12 frames after each cut, FELL (9,814 a frame to
9,329), and flicker inside scenes did not move (55 to 58 pixels a frame
past 25 dE). So the flash is the switch's tail, not the cut's draw. The
answer is a RULE rather than a weight: no cut may move more pixels past 25
dE than darkest first would, and the search starts from the order that
gives each old value the colour darkest first would have given it, so the
rule holds at every cut by construction. Inside a scene an order changes
nothing that can be seen.

## 7. Latch copies, priced in microseconds (2026-10-13, later)

Section 4 priced a copy as stores. The 286's binding limit is its DECODE
(every frame `286-fast` cut was the per-frame CPU ceiling's), so a copy has
to be priced as the time it takes: VM3BENCH timed a `rep movsb` FROM the
screen at 1.18 us a byte and TO it at 1.15, so a screen-to-screen move is
about 2.3 us a byte, all four planes (an estimate: no bench has timed
it), plus a block's and a row's set-up; against it, the residual record
through the shipped writer and the profile's own table. On 154 frame pairs
of GASSHOTW, 8 x 8 byte blocks, the shifts that keep the dither's cell:

| a copied byte | blocks a frame | decode | bytes |
|---|---|---|---|
| 2.3 us (the 286's) | 0.0 | +0.0% | +0.0% |
| 1.5 us | 0.1 | -0.0% | -0.1% |
| 0.5 us (a fast local-bus card's) | 1.8 | -0.5% | -1.1% |
| free (only the block's set-up) | 5.2 | -1.6% | -2.1% |

A copy pays for EVERY byte of its block, and a store pays only where a
byte changed, so a copy wins only where nearly every byte changes on
several planes and the moved picture is nearly exact - which dithered
driving footage almost never has. On the 286 it buys nothing; the ceiling,
free, is two percent. Not built: it is a format flag, a player path (and
a byte-at-a-time one round the text box, and a RAM one), and a gate, for
that. A flat-shaded clip that pans - a cartoon - is where to measure it
again. It is recorded, with the design as far as it was worked and
what would make it worth taking up, as docs/plans/VIDEO-LATCH-PLAN.md.

## 8. The coarse cut, through the encoder (2026-10-13, later)

Section 4's coarse picture measured, as a record, at **~19 KB a cut in
16 x 8 cells written in write mode 3** against ~48 KB for the whole new
picture and ~28 KB for the same cells by planes: a flat byte is one write
mode 3 store and up to two plane ones, and a flat run is one RUN, so the
cells fit a record where the picture needs two or three. 64 x 8 is ~11 KB;
the cell's height hardly matters, a row being stored on its own. Built as
`--coarse-cut WxH` (SPEC.md 98.2.5.2). Encoded over the whole 74 s at the
tree this file's later sections were taken on (the baseline moved: the
same options with no coarse cut is GASBASE2):

| file | stream | cut | error as seen | worst second | broken | last scene left at a change |
|---|---|---|---|---|---|---|
| GASBASE2 | 280.2 KB/s | 465 | 0.45% | 5.73% | 3.9 s | 89,140 px |
| GASCO16 (16 x 8) | 275.3 | 469 | 0.39% | 4.84% | 3.5 s | 79,216 |
| GASCO64 (64 x 8) | 274.2 | 451 | 0.41% | 4.79% | 3.8 s | 90,754 |

The coarse frame is still not drawn whole (the per-frame ceiling cuts it
too: a whole screen's bytes are touched however flat). **The owner, on
GASCO16 and GASCO64: "really good"** - the corner scenes that were mush
have content, the transitions "block in" instead of "chop chop", and the
switch flashes less. 64 x 8 is the default.

## 9. The order, with the rule, through the encoder (2026-10-13, later)

`--vga4-order planes` as built (SPEC.md 98.2.5.2): the search on STORES
(twelve pairs a scene, every fourth row) - a search on plane flips alone
was measured promising 8.5% fewer flips on 3 s and costing 1.1% more
stores, the player storing planes that want one value as one write - and
the switch's rule. Same tree and options as GASBASE2, no coarse cut:

| file | stream | decode | cut | error as seen | worst second | broken | a switch's pixels past 25 dE |
|---|---|---|---|---|---|---|---|
| GASBASE2 | 280.2 KB/s | 185.5% | 465 | 0.45% | 5.73% | 3.9 s | 10.9% |
| order | 258.0 | 172.0% | 421 | 0.45% | 5.96% | 3.7 s | 5.9% |
| order + `--scene-hold` | 266.0 | 175.4% | 400 | 0.43% | 6.00% | 3.7 s | |

The last column off the decoded files, every cut: the order's share is
above darkest first's by more than a point at NONE of the 54, and its mean
is half of it.
