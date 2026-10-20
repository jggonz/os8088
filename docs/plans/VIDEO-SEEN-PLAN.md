# VIDEO-SEEN-PLAN - "error as seen" as a person sees it, and an encoder that spends by it

**Status: section 2's score BUILT as a report line (2a, 2026-10-16), in three sets - colour, grey and 1-bit dither - and judging `--aim quality`'s trials and page-ahead's keep beside today's figure; section 3 BUILT as two options, `--edges 1` the default and `--attend` off; 3a's wave 1 BUILT as `--min-fps`, off until watched; the rest is PLAN.** Recorded from the owner's
review of the GasGas and Bad Carrot comparison encodes, and parked while
the encoder's choice in one scene is fixed first: the opening car shot of
GasGas (0:40), which the owner's own tune plays clean on the Victory 286
and every re-encode on this branch breaks. That is a CPU/budget question
before it is a metric question, and it is worked first.

The video encoder is `tools/os88venc.py`; its contract is SPEC.md 98.2.
"Error as seen" is the `picture:` line of an encode's summary
(`Encoder.measure`), and the same measure ranks what a cut frame draws
(`Encoder.value`, 98.2.1.2).

## 1. What the number is today, and why it reads low

Per frame, the shown picture and the target are low-passed the way an eye
averages dither (`sblur`/`tblur`), and the mean absolute colour difference
over EVERY pixel of the screen is taken (`dist`, 0..1 per channel,
averaged). The clip's figure is the mean of those means. So "0.30%" says
the average pixel is 0.3% of full scale off.

That is a fair RELATIVE gauge between two encodes of one clip and a
misleading ABSOLUTE one. The owner reads the GasGas file as 5-10% broken
"as seen" where it reports 0.26-0.30%, and the reason is in the opening
shot: the sky, the trees and the road - most of the screen - are right,
and every one of their pixels votes "fine", while the car, the thing the
eye is on, is visibly broken for seconds. A person does not average a
frame; they look at the car and judge the frame by it.

Scaling it up does not fix it: it is the POOLING that is wrong, not the
units, and a scaled mean would pass 100% on a clip that is merely busy.

## 2. Three observations, each measurable without a model

**2.1 Attention.** The eye goes to brightness, to contrast against the
surroundings, and to MOVEMENT - and in this shot to movement against the
camera's: the whole frame shakes, so "this pixel changed" calls the sky
salient too. Estimate the frame's global shift (one FFT phase
correlation between consecutive targets, cheap in numpy), and take motion
as the residual after it. The car moves against the shake; the sky moves
with it. Weight = f(luminance, local contrast, residual motion), from the
TARGET, so it is the same for every encode of the clip.

**2.2 Breaks that do not fade in.** A cut leaves a stale band of the old
picture - hard horizontal edges and a block outline that are not in the
source (the owner's screenshots: rows of the car's old position torn
across its new one). A wrong dither inside texture hides in the texture.
So compare the EDGES of the shown picture with the target's (the
structure term of SSIM): an edge the source does not have is a visible
break, and error inside existing texture counts for less (contrast
masking). Note 98.2.1.2 refused the error's own gradient as an ALLOCATION
weight - it starved mid-contrast regions for ever - which is a different
use from scoring.

**2.3 Pooling and persistence.** Judge a frame by its worst
attention-weighted patch (or a high percentile of patches), not by the
mean; and judge a stretch by how LONG the salient thing stays broken. A
car broken for 40 frames is the scene broken; the same error for one
frame is a blink. The existing `q_bad` (share of the frame off by
`VIS_BAD` or more) and `broken_spans` are the seed of this: they already
count seconds visibly broken, without attention.

The output reads "the share of where you are looking that is broken", in
0..100%. Its scale cannot be derived; it can be FITTED - the owner scores
a handful of clips by eye (GasGas ~5-10% is the first point, their tune
of it "fine" the second) and a few weights are regressed. That is a small
regression, not a trained model.

## 2a. The first calibration (2026-10-16): BUILT as a report line

`os88venc.seen_share` is 2's score, and every encode's summary carries it
beside today's figure: `the view as seen: X% of it broken on average, Y%
at its worst (t); Z s with a tenth or more of it broken`. It is fitted to
the owner's scores of the GasGas car shot (0:40.6-0:41.0, 24 fps, the old
tune: 286-fast, peak 4.0, owed time, page-ahead off), frames 15-23 - the
frames the 24 fps encode broke and the 20 fps one did not:

| frame | today's figure | the owner | seen_share |
|---:|---:|---:|---:|
| 15 | 1.14% | ~33% | 35.9% |
| 16 | 2.58% | ~62% ("a bunch of blocks") | 59.7% |
| 17 | 1.55% | ~45% | 48.6% |
| 18 | 0.46% | 30-33% | 24.7% |
| 19 | 0.47% | ~28% | 29.2% |
| 20 | 0.44% | ~25% | 26.1% |
| 21 | 1.51% | ~35% | 36.8% |
| 22 | 0.33% | 15-18% | 19.5% |
| 23 | 0.42% | ~ frame 22 | 19.9% |

The clean frames either side are 0 in both. What the owner's notes taught
it, each now a term: **colour counts** (frame 22's wrong-coloured
headlight centre and the red on a black tire are hue errors at near the
right brightness - a brightness-only first cut scored that frame 9%);
**hard lines count** (stale rows through the cars' tops); **soft blur
counts least** (the blurring on both cars "is not a major contributor");
and **a fault on an object breaks the object** (the spread term). Fitted:
soft brightness 0.75, colour 0.25, edges 1.0, a patch broken at 0.04, the
spread 0.75. Persistence (a patch broken in the frame before counting
more) was fitted too and came out 0 on these frames: frame 18, nearly
frame 17's picture, is the one miss (25 against 31.5).

**The second clip says the weights are the PICTURE's** (2026-10-16). Bad
Carrot 3:05-3:20 on the owner's tune (Mode X, 64 greys, flipped, peak 4.8,
lag 208 ms): eight cut frames, rated blind - the colour weights read them
2-5 times too high (A 26% against 5, B 81 against 30, G 58 against 12),
the order right. One set for both clips missed by 11 points on average,
taking the car frames down to half the owner's - and the owner had said why
first: "black and white, so the blur ends up counting for more here, and
edges matter more". So a GREY target (every pixel R = G = B) has its own
set, fitted on those eight (rms 7.8, "we're just going for general"):
brightness 1.0, colour 0, edges 2.0, a patch broken at 0.12 on a curve of
1.5, no spread. The clip reads 2.8% broken on average, worst 78.9%, and
exactly its 46 cut frames register at all.

| frame | the owner | seen_share (grey) |
|---|---:|---:|
| A | 5% | 3% |
| B | 30% | 35% |
| C | 50% | 62% |
| D | 90% | 75% |
| E | 10% | 9% |
| F | 5% | 1% |
| G | 12% | 13% |
| H | 15% | 6% |

**The third clip says a DITHERED picture is a third kind** (2026-10-16).
Take On Me, an old Hercules file (640 x 309 mono, 24 fps, 5150-st225 at
peak 0.9 - 518 of 971 frames cut, 418 of them by the disk), its own frames
decoded as the screen and today's encoder's 1-bit targets beside them
(today's encode of the same options differs from the old file by a few
dozen pixels a frame, and scores the same: 6.0% against 5.9% on average,
7.6 s broken in both). Ten frames rated blind; the GREY set it fell under
matched six and read the blur inside the dither as breakage (D 20 against
3, E 19 against 5), because a 5-pixel box does not see past an 8 x 8
dither. No weighting fixed it while an edge lost counted the same as an
edge gained; splitting the edge term did, the owner's own reading of
every frame being that the LINES a cut leaves count and the blur does not
("the face shape survives, just blurred"). So DITHER: brightness over a
13-pixel box, edges ADDED 1.0 and LOST 0, a patch broken at 0.12 on a
curve of 3, spread 0.75 - rms 3.4 over nine frames. A 1-bit target takes
it when a quarter of its pixels average to a mid grey: Take On Me is
42-93% so in every frame, Bad Carrot on the same Hercules options 4% at
the median (solid black and white, GREY as the owner predicted for Bad
Apple on a Hercules), its 51 frames of fade dithered and taking DITHER.

| frame | the owner | seen_share (dither) |
|---|---:|---:|
| A (a scene change, the old scene dominating) | 80% | 82% |
| B | 12-15% | 10% |
| C | 0% | 1% |
| D | 3% | 6% |
| E | 5% | 9% |
| F | 2% | 0% |
| G (a scene change) | 70% | 21% |
| H | 30% | 29% |
| I | 10% | 3% |
| J | 25% | 23% |

**G is a class of its own and is left out of the fit.** It is a cut at a
scene change that leaves the OLD scene showing whole, a light sketch over
a light wide shot, so patch for patch the brightness is near and the
score reads it low. The owner: "the individual frame is VERY broken but
the 'video' is not" - in play it "looks like a fade". A is the same kind
of frame and reads 82 because its old scene is high contrast against the
new. So for the encoder's purposes the low reading is the RIGHT one and
A's is the one too high, and the term this asks for (section 3) is the
other way round from a standalone rating: a cut frame that shows a whole
coherent picture - the old scene, or the new one not yet arrived - is a
FADE and costs less than one torn between two. That is also page-ahead's
idea (98.2.1.8: a scene change built on the hidden page and swapped in
whole) and 3a's hold, which shows the old frame whole for a period; the
score should not punish what those two buy.

**In the trial encodes** (2026-10-16): `--aim quality` takes a step only
while the view as seen stays within 1.25 x the asked one's plus a point
(`AIM_SEEN0`), beside the error as seen's own test; page-ahead's keep and
its prune weigh the view's mean and its seconds a tenth broken beside the
error as seen's three (`PAGE_PICTURE`). Take On Me 0:10-0:30 at `--preset
herc`: `herc-mid` not taken, the view 1.0% broken against 0.4% (and 16%
of frames cut against 2%); GasGas 0:38-0:48 in Mode X, page-ahead KEPT
with the view 0.4% against the plain encode's 0.7%. `page_hurt` - which
build hurt its own stretch - still sums today's per-frame figure.

**Next**: section 3. A colour clip of flat cartoon colour is a fourth check.

## 3. Using it in the encoder

`Encoder.value` already decides, per pixel, what writing it now is worth
(this frame and the look-ahead), discounted to what an eye sees. Two
changes follow from section 2 and both are local:

- **Weight `value` by 2.1's attention map.** In a cut frame the car's
  spans then outrank the shaking sky's. Cheap: the map is per target and
  computed once.
- **Penalise leaving a hard edge.** When a frame is cut, a span that
  completes an object (closes a stale band's edge) is worth more than its
  pixel count says. A first form: score the stale edge each candidate
  span REMOVES, from 2.2's edge comparison.

**BUILT as two options, both off by default** (2026-10-16): `--attend S`
weights `value` by `seen_attention`'s map (its mean 1, so a frame's whole
worth is what it was and only where it is spent moves), and `--edges S`
adds the edge term priced as `plain` is - how much nearer the target's
edges the screen is for writing the pixel, less the target's own - with
the picture kind's weights (`seen_weights`: on dither only the edges a cut
ADDS). Five arms on the three clips, each on its own options (GasGas
0:40-0:50 the old tune at 24 fps, page-ahead off; Bad Carrot 3:05-3:20
the owner's 64 greys; Take On Me 0:15-0:30 the old Hercules options):

| clip | arm | the view broken | s a tenth broken | worst | error as seen |
|---|---|---:|---:|---:|---:|
| GasGas | today | 6.84% | 1.54 | 95.8 | 0.396% |
| | `--attend 1` | 6.97% | 1.54 | 95.7 | 0.392% |
| | `--edges 1` | 6.80% | 1.54 | 95.8 | 0.398% |
| | `--edges 2` | 6.81% | 1.54 | 95.8 | 0.399% |
| | both 1 | 6.93% | 1.54 | 95.6 | 0.392% |
| Bad Carrot | today | 2.76% | 0.75 | 81.8 | 0.397% |
| | `--attend 1` | 2.70% | 1.04 | 76.6 | 0.389% |
| | `--edges 1` | **2.49%** | 1.00 | **70.2** | **0.356%** |
| | `--edges 2` | 2.81% | 0.92 | 75.2 | 0.398% |
| | both 1 | 2.68% | 1.00 | 72.1 | 0.389% |
| Take On Me | today | 5.45% | 1.87 | 90.9 | 2.158% |
| | `--attend 1` | **5.18%** | **1.75** | 89.5 | 2.200% |
| | `--edges 1` | 5.37% | 1.87 | 91.2 | 2.168% |
| | `--edges 2` | 5.44% | 1.92 | 91.3 | 2.198% |
| | both 1 | 5.15% | 1.79 | 90.0 | 2.208% |

**The car is not an allocation question.** GasGas's car frames (14-23)
read the same in every arm (37 62 52 26 35 28 37 20 23 today, 36 63 51 25
36 26 37 19 22 with edges): the same frames are cut by the same ceiling,
and no order of spans makes the ~20 KB a 24 fps frame has draw what needs
27-30 KB. That is 3a's lever, a hold, and not this section's.

**Where it does move, it is a TRADE.** Bad Carrot's cut burst with
`--edges 1`: frames 44-53 read 35 69 70 50 47 46 47 30 25 56 against
today's 62 54 82 71 52 56 75 58 68 59 - frames 50 and 52 are the carrot
drawn whole where today's are torn bands - and frames 54-57 and 66-68
read 31 32 22 20 / 20 13 20 against 22 14 8 4 / 10 6 6: the worst repaired
sooner, a milder residue left longer. The owner's eye on the strip and in
play: 44-52 "much better", a slight outline at 55-57, and it tears "very
slightly less" in play - so **`--edges 1` is the default** (2026-10-16).
`--attend` moved nothing a strip of frames shows and stays off.

What NOT to do first: change allocation and the metric in the same step.
The metric is the instrument the allocation is judged by; build the score
(report only), check it against the owner's eye on the clips we have, and
only then let it steer `value`.

## 3a. A frame-rate step on the degrade ladder (the owner, 2026-10-15)

**The finding that prompted it.** The owner's GasGas tune that plays the
opening car shot whole is a **20 fps** file; the copy compared against it
for a day was the same options at **24 fps**, and at 24 the car frames
(0:40.7-0:41.0) were cut by the per-frame ceiling into stale bands - the
calibrated model and the owner's tune agree once the frame rates match.
At 20 fps each of those frames is a 27-30 KB record drawn whole; at 24 the
same moment is ~20 KB records with the car torn. So: **20 fps unbroken
looks better than 24 fps broken, and 24 fps unbroken looks better than
either.**

**The step.** Today the encoder's answer to a frame that will not fit is a
CUT, after (16 colours only) a palette step-down (98.2.15's `step_down`).
A frame-rate drop is the third kind of degrade and sits BEFORE the cuts:
HOLD a frame - write nothing, the picture stays as it is, the sound plays
on - and give its period of CPU and its bytes to the next frame, which is
drawn whole and ON TIME. 24 -> 20 is one hold in six; 24 -> 12 every other
frame. It is owed time's and the lag's idea once more (98.2.1.1,
98.2.1.1.2), with the difference that matters for sync: owed time and the
lag draw EVERY frame and draw them late; a hold draws FEWER frames and
draws each one at its own time. A high-change scene goes choppier for its
length rather than broken.

**No format change looks needed**: a record that writes nothing is a frame
today (a still scene's sound-only records are 0.5 KB), and the player shows
each record at its period whatever it holds. The encoder needs the
decision AHEAD - the look-ahead already prices the next frames, so a hold
is chosen when the frame after it fits whole in two periods and does not
in one.

**THE LADDER IS BUILT** (2026-10-16, SPEC.md 98.2.1.2's `--ladder`, the
encoder window's Size vs quality -> Degrade ladder): the owner's "a 'Use
Degrade Ladder' checkbox at the top of said tab... pick from an option for
each rung (from options supported by the current mode and settings) -
which overrides the global settings for how we degrade". Its bound is not
a number per rung but the one test every rung shares: the first prefix
that draws the stretch WHOLE is taken, else the least broken by the view
as seen. The default ladder came off four clips on their own options:

| clip | today | `lag:100, slow:5/6, colours:12, slow:2/3, coarse:auto, slow:1/2` |
|---|---:|---:|
| Last Exile 0:10-0:40, flipped Mode X, 286-speeddemon | 24.3% (400 cut) | **3.2%** (57 cut) |
| Bad Carrot 3:05-3:20, the owner's flipped tune | 1.8% | **1.0%** |
| Take On Me 0:15-0:30, Hercules | 5.5% | **4.7%** |
| GasGas54's options at 24 fps (page-ahead) | 2.3% | **1.7%** |

Coarse cells were second when this table was taken and are FIFTH since
2026-10-17 (the owner: "coarse is a type of cut"); every file came out the
same - Gas Gas Gas 1.7% with its stretches at rungs 2 and 6 as before, Bad
Carrot 1.0%, and Last Exile and Take On Me by construction (no cells were
drawn in either). `slow:5/6` before `lag:100` made the same four files: a
stretch that needs either needs both before it draws whole.

**A CUT RUNG** (2026-10-17, the owner: "lets add 'cut by x%' into the
ladder. That way we can choose which type of cut we want - some videos
accept normal cuts better than others"): `cut:X` STOPS the climb where no
frame of the stretch is more than X% broken as seen, and the frames are
cut as the budgets cut them. It draws nothing new, so its try is the try
before it judged again (`degrade_choose` keeps the last), and past it
every rung is judged so too, by the largest cut rung. It is in no
default: `lag:100, cut:5, slow:5/6, ...` is a file that would rather tear
a little than drop to 20 fps, and which one a video is, is the owner's
eye's. Lag stays first, being the
rung nobody sees. Most of Last Exile's stretches went to the last rung,
half the rate - the owner's eye is what says whether 12 fps for that long
is the trade they want, and the ladder is theirs to reorder per file.

The tab carries the checkbox and six rungs, each a list of the rungs this
form can use (a card's sound offers `lag`, the speaker does not; `coarse`
on a planar screen, `colours` only for 16 colours a scene) - and under
them **Instead of cutting**: Lag, Min fps, Slow cost and Flip hold, moved
there off **When a frame is cut**, and Coarse cut off **Scenes**. Those are
the global settings the ladder stands in for (Lag, Min fps and Coarse cut
grey while it is on; the charge and the held frames it uses), and taking
them off Choices is what brought that tab back inside the default window
with the inner tab bar on it.

**Coarse cells left a palette a scene's for every PLANAR screen**
(2026-10-17, SPEC.md 98.2.5.2; the owner: "is there any reason that
doesn't apply to other display types?"). The gain is a flat byte being one
store for many pixels, which is VGA4's write mode 3 and Mode X's Map Mask
0Fh and nobody else's: a scene change drawn in cells took 32-51% of its
cycles on Last Exile's Mode X (4x1 as cheap as 64x8 - the group of four is
the gain, so Mode X's ladder is the one cell) and 87-100% on Take On Me's
Hercules, where a flat byte is only a run. Mode X without a palette a scene
finds its scenes with the same scene pass, read for the cells alone. On
Last Exile with no ladder that is 24.3% -> 23.4% of the view broken and 400
-> 389 frames cut (14 changes in 4x1, 4 whole); WITH the ladder it is the
same file as before, because all 46 stretches the ladder decides are
INSIDE shots and the coarse rung acts at a scene's first frame. A cell at
any would-be cut, not only a scene's first, is the open question that
leaves - a moving picture blurred across for a frame, which only the view
as seen can price - and **it is on the owner's list to look at once it is
built** (2026-10-17), not before.

**BUILT the same day as its own rung, `coarseany:CELL`** (the owner: "we
can split it between scenes/all, so it can own two different spots on the
ladder. At scenes, coarse is known acceptable. During fast motion we have
yet to test"). Its first shape - cells on the stretch's FIRST frame only,
as the scene rung draws them - bought nothing, and the ladder's own log
says why: Last Exile's motion stretches are heavy for all twelve frames (6
to 12 of them cut), so one frame of cells at the head moved a stretch's
view 37.7% -> 37.2%, 58.2% -> 53.3%, and 39.2% -> 42.7% the wrong way. So
`coarseany` draws EVERY frame of the stretch the budgets would cut in
cells at its own time (`coarse_every`), which is the comparison the
question is really about: the width given up at the full frame rate,
against the rate given up at the full width. On the same stretches that
is 37.7% -> 21.6%, 58.2% -> 26.7%, 34.1% -> 21.2%. Last Exile 0:10-0:40,
Mode X flipped, `286-speeddemon`, whole clip:

| | the view broken | s visibly broken | worst second | cut | in cells |
|---|---:|---:|---:|---:|---:|
| cut (no ladder) | 24.3% | 3.3 | 4.79% | 400 | 0 |
| `lag:100, coarseany:frame` - 24 fps, 4x1 where it would cut | 16.5% | **0.4** | 1.75% | 102 | 297 |
| the default ladder - 12 fps through the heavy stretches | **3.2%** | 0.6 | **0.71%** | 57 | 0 |

The cells end the TEARING - visibly broken 3.3 s -> 0.4 s, under even the
half rate's 0.6 - and the score still ranks the half rate far ahead,
because it charges a held frame `--slow-cost` (2 points) and a 4x1 frame
its whole lost width. Which of those is the better picture IN PLAY is the
owner's eye's: the strip and the three files went to the owner. Put in a ladder before the slower rates,
`coarseany` is passed over exactly as the slow rungs' whole frames beat
it by the score; behind them it is never reached. A `cut:` rung after it
is how a person who prefers it says so. Gas Gas Gas (VGA4 at 640x400,
cells 16x4 and coarser) never took it: 15 of 18 stretches drawn as they
were, the score rating its cells worse than a cut. It costs the encode:
every frame the budgets would cut is tried on a copy, so Last Exile took
2,256 s against 547. The owner on watching it: "overall, a really good
effect; though it doesn't fit every scene" - the scene-by-scene editor's
case again - and the half rate alone "gets too jerky AND still cuts".

**A MIXED SCREEN, `coarseany:mix`** (2026-10-17, the owner: "only
coursifying the parts that can't be afforded until it fits, instead of the
full frames... I want to see a comparison"). `coarse_mix`: the frame in
32x8 tiles (`MIX_TILE`, eight of Mode X's groups), ranked by what their
cells save - the pixels to change at full detail less those in cells -
and a binary search on copies of the encoder for the FEWEST tiles in
cells (4x1 on Mode X, 16x4 on VGA4) that let the budgets draw the frame
whole; the rest at full detail. Last Exile 0:10-0:25, the same options:

| | the view broken | worst frame | s visibly broken | cut | encode |
|---|---:|---:|---:|---:|---:|
| cut (no ladder) | 25.3% | 99.7% | 2.4 | 174 | 99 s |
| `lag:100, coarseany:frame` (whole frames in 4x1) | 18.0% | 94.3% | **0.3** | **21** | 1,042 s |
| `lag:100, coarseany:mix` | **17.0%** | **79.6%** | 0.5 | 53 | 1,708 s |
| the default ladder (half rate) | 3.7% | 67.3% | 0.5 | 26 | 242 s |

By the score the mix is the better of the two at 24 fps - a point less
broken on average and the worst frame 15 points better - and on the
strip it keeps the girders and the sky sharp where the whole-frame cells
smear them; what it gives back is a little of the tearing (0.5 s visibly
broken against 0.3, 53 frames cut against 21), because a mixed frame is
fitted to the frame's own budget and the disk can still fall short a few
frames on. A tile's edge between full detail and cells is the thing to
look for in play. Its price is the search: about eight tries a frame it
mixes, 1.6x the whole-frame cells' encode.

The owner on watching the four: mix "is a good option, but it doesn't
win this round, because on its own it's not enough to save the
additional cuts"; no more tearing seen, and in the heavier scenes it
"drop[s] to effectively all", which is right - **mix IS mix-then-whole**,
a frame at a time: where every tile in cells still does not fit, the
whole frame goes in what `auto` picks (on VGA4 the finest of 16x4, 32x8
and 64x8 that fits; on Mode X 4x1), so a separate `coarseany:frame` after
it would add encode time and change nothing.

**THE DEFAULT LADDER, 2026-10-17 (the owner)**: the half rate OUT, the
scene rung renamed `coarsescene` (`coarse:` still read), and
`coarseany:mix` the sixth rung in the half rate's place -
`lag:100, slow:5/6, colours:12, slow:2/3, coarsescene:frame, coarseany:mix`:

| clip | with the half rate | the new default |
|---|---:|---:|
| Bad Carrot 3:05-3:20 | 1.0% (worst 33.3%) | **0.4%** (worst 9.2%) - all 5 stretches mixed |
| Gas Gas Gas, the owner's tune | 1.7% | 2.1% |
| Take On Me, Hercules (no cells) | 4.7% | 5.0% |
| Last Exile 0:10-0:25 | 3.7% (0.5 s visibly broken) | 18.9% (1.4 s) |

Last Exile's heavy stretches take rung 6, which is 16 fps AND mixed
cells, and that scores worse than the same cells at 24 fps alone (17.0%,
0.5 s - `lag:100, coarseany:mix`): on that clip the slower rate and the
cells pay twice. The owner on watching it: **"a huge win; that is the
best bad carrot's heavy sunscene has ever looked"**, and Last Exile
"still cut a bunch, and needs coarseany/full [`coarseany:frame`], but as a
default, its better than full cuts" - so the default stands, and Last
Exile is a clip that wants its own ladder, which is the scene-by-scene
editor's case once more.

**`frame` for `auto`** (2026-10-17, the owner: "can that become 'full
frame' or somesuch so its clear to the user that they are doing a full
frame at a time?"): a coarse rung's whole-frame cell is `frame` - "whole
frame" in the window, beside "mixed: only the tiles it must" - and a
ladder that says `auto` still reads as it. A name only: every file above
is the same.

**THE ENCODER SPEED PASS, first round (2026-10-17)** (the owner: "encode
times aside; add an item to do an encoder optimization pass"). Measured
first: Last Exile 0:10-0:15, Mode X flipped on `286-speeddemon`, the
default ladder (mixed cells), `--jobs 1`, under cProfile - 152 s, of which
the encoder's COPIES were 32 s (179 of them at 0.18 s), Mode X's `frame`
64 s, ffmpeg's palette 29 s (once, outside the encoder) and the cells' own
nearest colour 10 s. What it came to, unprofiled, **90 s -> 60 s** - the
encoder's own share, the palette taken out, **64 s -> 34 s**:

| change | what it was | the file |
|---|---|---|
| a copy SHARES the read-only tables (`ENC_SHARED`) | the geometry's 65,536-entry row lists, deep-copied for every trial | byte-identical |
| `modex_subs` over whole arrays | a Python loop over every group of four, 43 ms a frame | byte-identical (`modex_subs_loop` kept as the reference; videnc leg 3c') |
| Mode X's model screen updated in one array write | numpy calls per span, a third of `frame`'s own time | byte-identical |
| the cells' nearest colour by one matrix product, in float64 | a 14,400 x 256 x 3 difference array, 0.65 s a frame | 0.2% of cells change: the float32 array picked a FARTHER colour by rounding; this is the exact nearest |

Of the three candidates this item was written with: **pricing a frame
without running `frame()` is REFUSED**, measured - it is not exact: a
frame whose first estimate overruns its room can still come out whole,
the fall-back's split spans costing less and a record under its room
growing the room again, and stopping at the first estimate made a
different file and a WORSE one (1.7% of the view broken against 0.6%).
**Sharing a copy** is the first row above - the copy itself was the cost,
not how often it was taken. **Reusing a rung's stretch** was already
there: `degrade_choose` keeps the last try where a prefix changes nothing
it draws (a cut rung, a scene rung away from a scene). Still in the
profile and not taken: the record writer (`record`/`to_lists`, 12.6 s), a
tight serializer already; 16-colour VGA's model stores, whose write-mode-3
read-modify-write is order-dependent and so not a whole-array write; and
the mix's eight probes a frame, which are its search.

What it was sketched as:

    hold to 20 fps
    a pixel cut up to 0.x% (as seen)
    hold to 16 fps
    a coarse cut up to 0.x%
    ...

with today's palette step-down as one more rung and today's behaviour
(no holds) as the default ladder until the A/B says otherwise. The rungs'
bounds are in "as seen" terms, which is the other reason section 1 matters:
a ladder bounded by today's whole-screen mean would let the car break
"up to 0.3%".

**Also owed**: the palette step-down is 16 colours' only; the ladder is the
natural place to give the other formats theirs.

**Wave 1 (2026-10-16): VARIABLE FRAME RATE, `--min-fps F`.** The owner:
the goal is "variable framerate - slowing down to X when it would have
drawn better", and the hold is only the means. Built so:
- **Where**: at the first frame the budgets would cut (on a copy:
  `vfr_cut`), and at a scene's first frame, the next half-second
  (`VFR_SPAN`, stopping short of the next scene's palette) is encoded on
  copies of the encoder at each rate down to F - 5/6, 2/3, 1/2, 1/3, 1/4 of
  the rate (`VFR_RATES`, `vfr_pattern`: the stretch's first frame held, its
  last always drawn) - and the rate whose stretch is least broken is
  committed for it. The full rate's verdict is committed for two frames
  only (`VFR_AGAIN`), so a cut a frame or two on is asked again: committed
  for the stretch, the car shot's 12 fps decision never came.
- **A held frame DRAWS TOWARD THE NEXT FRAME SHOWN**, which finishes it.
  An EMPTY hold was built first and bought nothing on the car: its frames
  are cut by the per-frame ceiling, which a hold does not raise (the
  player draws the records that are due and starts none early), and the
  frame after an empty hold has two frames' change to draw. Drawn toward
  the next, the held period's CPU and bytes go into that picture.
- **Judged by the view as seen, a held frame against the picture it
  draws** (it is the first half of the next one, as a slower file's frame
  is drawn over the same time), plus `VFR_HOLD`, 2 points, for its
  choppiness. Judged against its own moment's target the motion it skips
  read as breakage and the car was never slowed. It is `--slow-cost PTS`
  (2 by default): the owner, on the three GasGas files, found `--min-fps
  12` and 20 fps throughout "very similar" and the fixed 24 "lots of
  tearing liney cars" - 12 fps "not dropping too low, in this clip" - and
  expects another clip to want more penalty before it drops, so it is an
  option, and a per-scene one belongs to the scene editor this plan's
  family has had parked for a while.
- **At a scene start it competes with the coarse cut** (98.2.1.9): the
  full rate's first frame draws the cells, as today, and a slower rate
  that wins draws the scene in full detail instead.
- **The report** says which stretches slowed to what; held frames are
  measured as the decision measured them. `OS88_VENC_VFRLOG=FILE` logs
  every decision.

Measured (each clip on its own options; GasGas 0:40-0:50 the old tune
without its layer, page-ahead off, at lag 0; Take On Me 0:15-0:30):

| clip | arm | the view broken | s a tenth broken | s visibly broken |
|---|---|---:|---:|---:|
| GasGas | 24 fps | 6.80% | 1.5 | 0.4 |
| | `--min-fps 12` | **5.31%** | 1.2 | 0.4 |
| | 20 fps throughout | 5.0% | 0.9 | 0.3 |
| GasGas, lag 100 | 24 fps | 6.41% | 1.3 | 0.5 |
| | `--min-fps 12` | 5.86% | 1.1 | 0.4 |
| Take On Me | 24 fps | 5.5% | 1.9 | 7.8 |
| | `--min-fps 12` | **4.8%** | 1.6 | 6.2 |

The car frames 14-25: 36 63 51 25 36 26 37 19 22 0 0 0 at 24 fps, 36 63 51
40 4 20 29 0 0 19 5 0 slowed. Its first three are a scene change - its
palette arrives at frame 15 and the content a frame before - and break at
20 fps throughout too. The encode takes ~15% longer on GasGas (285 s ->
328 s) and the default is OFF.

Not on a flipped file (every record flips, so a held one would show the
page two frames back), a layered file or a Live one, and in wave 1 not on
a page-ahead file either.

**Wave 2 (2026-10-16): WITH PAGE-AHEAD - BUILT.** The owner: "page ahead
is pretty critical to things like gasgas54".
- **The plan pass decides, the real passes replay.** The plan pass is the
  plain encode and decides the rates as wave 1 does; it logs the held
  frames (`H`), each slowed stretch (`S`, with its rate), and makes the
  frame drawn after a run of holds a page-ahead TARGET (`HT` where it was
  not one already). Every real pass holds exactly `H` and decides nothing
  (`vreplay`), so the lag search's copies and the prune's pass agree.
- **A held frame in a real pass writes NOTHING on the page shown**, so the
  picture stays whole, and its whole period goes to the hidden page
  (`PageAhead.after` gives an uncut frame its ceiling less its record),
  built toward the frame after it and flipped to there. This is page-
  ahead's own mechanism and the better place for the hold to draw: the
  held period is spent where nobody sees it torn.
- **Kept stretch by stretch** (`vfr_page_select`). The plan pass judged its
  holds without the hidden page, and on GasGas54 at 24 fps they cost
  page-ahead its 0:07.5 burst (frames 179-189 read 72 30 82 81 85 67 68
  with them against 28 0 69 77 36 81 20 without) while they won the car
  shot (18-22 0 against 16 11 3 18). So with holds in the plan the
  page-ahead pass runs WITH them and WITHOUT them, each slowed stretch is
  kept where its frames' view as seen was less broken with its holds, a
  third pass runs with only the kept ones when the verdict is mixed, and
  the least broken of the passes is the file - page-ahead's prune
  (98.2.1.8.2), one level up.
- **A held frame counts with the cut ones** in every comparison (`cutf`):
  holds turn cuts into holds, and counted apart the plain encode, holding
  where page-ahead drew, read as cutting less - which alone dropped a
  page-ahead file that was better on every picture measure.

GasGas54's own options (16 colours 640 x 400, 286-picomem1, peak 5.4,
page-ahead auto), 0:40-0:50:

| | the view broken | worst | s visibly broken |
|---|---:|---:|---:|
| 20 fps, page-ahead | 1.9% | 81.5 | 0.2 |
| 20 fps, page-ahead + `--min-fps 10` | **1.3%** | **49.1** | **0.1** |
| 24 fps, page-ahead | 2.3% | 80.6 | 0.2 |
| 24 fps, page-ahead + `--min-fps 12` (2 of 3 stretches kept) | **2.2%** | 80.6 | 0.2 |

**Wave 3 (2026-10-16): FLIPPED - BUILT, with no format change.** A flipped
record is drawn on the page the glass is not showing and then shown, so a
hold drawn toward the next frame would be SEEN half drawn. A flipped
file's hold writes NOTHING: the back page is brought up to the frame shown
(98.3.8), which is shown again whole. It gives the frame after it no CPU
head start - the per-frame ceiling is the same - but it keeps the period's
bytes, and on a flipped file the LAG SCHEDULE is what cuts (Bad Carrot's
tune: 32 of its 43 cuts), and the call that draws nothing is a call the
picture catches up in, so the frame after starts level with the whole of
its allowance. The rate is chosen the same way, simulated with the empty
hold (`vfr_try(empty=)`) and the held frame judged against the picture it
shows. Flipped AND built ahead (98.1.3.7.2) takes the same empty hold in
both passes and wave 2's selection.

| Bad Carrot 3:05-3:20, the owner's tune (Mode X, 64 greys, flipped, lag 208) | the view broken | worst | frames cut |
|---|---:|---:|---:|
| as today | 1.8% | 57.0 | 43 |
| `--min-fps 12` | **0.9%** | **30.4** | 18 |
| `--page-ahead on --pages 3` | 1.8% | 55.4 | 41 |
| ...and `--min-fps 12` (2 of 5 stretches kept) | 1.2% | 53.6 | 30 |

**The held record, BUILT (2026-10-16, SPEC.md 98.1.3.9, 98.3.8.5.1)**: a
record that draws the back page WITHOUT showing it, so the held period
draws toward the next frame there as page-ahead's hidden page does - the
case of a flipped clip the per-frame ceiling cuts, which the owner names
as Last Exile. Byte 37 = 6, y0 bit 15, and the record after a run of them
brings its page up by the copy of all their rows. **+67 bytes of player**
(against the ~100 estimated: the held state rides in `vp_prevn`, and the
text needed no guard). `--flip-hold ahead` is the default, `empty` the
hold above. On Last Exile 0:10-0:40, Mode X flipped on `286-speeddemon`:

| | the view broken | s a tenth broken | s visibly broken | whole | cut |
|---|---:|---:|---:|---:|---:|
| as today | 24.3% | 14.8 | 3.3 | 321 | 400 |
| `--min-fps 12 --flip-hold empty` | 17.4% | 10.3 | 2.5 | 397 | 229 |
| `--min-fps 12 --flip-hold ahead` | 17.5% | **9.0** | **2.3** | **435** | **183** |

The mean is the same and everything that counts breaks is better: 38
more frames whole, 46 fewer cut, 1.3 s less with a tenth of the view
broken. What holds it back is its price - the frame after each run pays
the copy of both records' rows out of its own ceiling - and that price is
the next thing to look at if a clip wants more.

**Both trims of that price MEASURED and REFUSED** (2026-10-16): the
encoder priced every bring-up after a held run on Last Exile three ways.
| bring-up after a held run | mean cycles | of that frame's ceiling |
|---|---:|---:|
| the hull copy (shipped) | 565,916 | 77.1% |
| two bands, no gap | 565,976 | 77.1% |
| the held rows copied, the next record decoded again | 719,952 | 98.1% |

A held record draws toward the next frame across the whole picture (its
band 179 of the canvas's 180 rows), so the hull IS the canvas and there is
no gap to skip; and the next record's decode costs more than its rows'
copy, which is why the player's own rule already takes the copy. The best
of the three is 0.04% under the hull. **The price is structural**: a page
two records behind, with one record kept, is brought up by a copy of
everything the run touched - 77% of the next frame's ceiling here, which
is the held period's gain paid back a frame later, and why the mean did
not move. What would escape it is a different FLIPPED FILE, not a better
hold: every record a change against ITS OWN PAGE (the frame two back,
98.1.3.7.2's `REC_BUILT` - "drawn on as it stands") so no page is ever
brought up. It cannot be taken only after a hold (the record after it
would leave the other page needing the same copy); it is the whole
flipped encode's question, and UNMEASURED: whether a change against the
frame two back costs less than bringing a page up plus a change against
the frame one back.

**Not combined**: a LAYERED file (the layer is the better machine's frames
drawn over the base's, so a base drawn toward the next frame would have
the layer drawing it back - paid for twice), and LIVE (a pass a tick on
the desktop).

## 3b. OPEN: a flipped file whose every record is against its own page

**Recorded, not started** (the owner, 2026-10-16: "record the option for
changing the format and let's move on for now"). The held record's bring-up
is 77% of the next frame's ceiling on Last Exile (3a, wave 3), and it is
the shape of every flipped file, not only a held one: today a flipped
record is a change against the frame BEFORE it, so the page it is drawn on
(which holds the frame two back) is first brought up - the last record
decoded again, or its rows copied off the glass (98.3.8, 98.3.8.1) - and
every frame pays that before it draws.

**The option**: a flipped file whose every record is a change against ITS
OWN PAGE - the frame two back - drawn "as it stands", as 98.1.3.7.2's
`REC_BUILT` already is. No page is ever brought up: no kept record, no
`vp_prevseg` claim, no copy, and a held record (98.1.3.9) is then free,
its page simply drawn on twice.

**What decides it is one number, measurable on the host before any byte is
written**: per frame, the cost of the change against the frame two back
against the cost of the bring-up plus the change against the frame one
back - priced by the profile's table (98.2.3.3) over the clips that flip
(Last Exile, Bad Carrot, Trackmania). A change two back is the UNION of
two frames' changes, so it is larger where the picture moves on, and the
bring-up it removes is a whole record's decode or a band's copy: which is
cheaper is the clip's, and may be the frame's.

**Its shape, if it wins**:
- **Format**: rendition byte 37 = 7 (a player before it refuses), every
  frame record flipped and drawn on its own page as it stands. Keys are a
  picture onto both pages, as today; a seek lands on two equal pages, so
  the first record after a key is a change against the key on either.
- **Player**: `vp_flipdec` without its bring-up - SMALLER than today's, and
  `vp_prevseg`'s 31 KB claim gone with it. The text's two saves are a page
  each already (98.3.8.5).
- **Encoder**: the `Flipped` model keeps BOTH pages' pictures, a frame's
  change taken against its own page's; the budgets lose `prev_c`. A
  per-frame choice between the two kinds would need both in one file (a
  record bit, `REC_BUILT`'s), and bringing a page up again after a record
  against its own page is the copy this exists to avoid - so the choice,
  if any, is the file's.
- **Not combined** with page-ahead's flipped files (98.1.3.7.2), which
  already build a page as it stands where they flip to one.

## 4. The order

1. (FIRST, not this plan) Fix why the opening GasGas shot breaks at all
   on this encoder when the owner's tune of the same frames does not -
   it is the encoder's budget/choice, not the metric. Two-second clip
   (0:40-0:42), against the owner's original file and its profile.
2. Error maps for that shot: where the wrong pixels are, and which spans
   each cut frame chose - does the budget go on the shake or the car.
3. The attention-weighted score as a REPORT line beside today's, fitted
   to the owner's scores.
4. Then the score into `value` (section 3), A/B'd on the same clips.
