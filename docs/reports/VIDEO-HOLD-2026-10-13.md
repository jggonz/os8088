# The colour hold, gated by motion: staleness measured

**A measurement, not a description.** Taken 2026-10-13 on branch
`video-palette` at `3e11d40a`, on a 4-core Xeon container. True of that
tree's encoder; not maintained. docs/plans/VIDEO-PALETTE-PLAN.md 10.2 is
what it means.

## What the owner saw

Watching CHR16H24 and CHR16H40 (docs/reports/VIDEO-PALETTE-CHOICE-2026-10-13.md:
the chroma-weighted sixteen with `--vga4-hold` 24 and 40, Gas Gas Gas 40-60
s, `286-fast`): **the palette and colour are VERY good**. The hold has the
dot dither's old failure - it keeps dim areas for ever:
- on every scene change and large camera move, hold 40 keeps the whole sky
  or mountain the LAST scene showed; 24 mostly changes them away;
- on the first turn the mountain stops going by - completely at 40, mostly
  at 24 - where the EGA's sixteen kept it moving, on less data.

Why: `--vga4-hold` keeps a pixel's colour while it is within D of the source
NOW. It has no memory and no notion of motion - and it bypasses the one
test the shipped rule has that does see motion, `moved < --vga4-stable`
since the colour was chosen. A dark blue sky and a dark grey mountain are
within 40; a panning texture's every pixel drifts a little and stays within
D of what is shown.

## The gate

A scratch copy of the encoder: the colour hold applies only while the
pixel's SOURCE has moved less than M = 24 since its colour was chosen - so
only the plan test is relaxed, and a source that moves on releases the
pixel, which then holds at its new colour. Optionally (`HOLD_CUT` 20) no
colour hold at all on a frame whose source moved by a mean of 20 levels.

## STALENESS, the score

On 4 x 4 blocks in CIELAB, every frame of the decoded file: a block is
STALE when what it shows has not changed (dE < 2) since the source was last
caught up with, while the source has since moved by dE > 6 and differs from
what is shown by dE > 6. "Stuck" is a block stale for 15 frames (0.5 s) or
more at once. At looser thresholds (4 / 15 / 12) every file read 0.3-0.4%:
the stuck skies are DARK, where hue that is plainly visible is a small dE.

| `286-fast`, GasGas 40-60 s | KB/s | cut of 599 | stale | stuck | worst frame |
|---|---|---|---|---|---|
| the EGA's sixteen | 158.0 | 27 | 4.28% | 1.50% | 55.6% |
| chroma 16 | 314.7 | 163 | 2.23% | 0.56% | 58.8% |
| chroma 16, hold 24 | 201.7 | 34 | 5.83% | 2.40% | 58.1% |
| chroma 16, hold 40 | 141.0 | 21 | **16.77%** | **9.44%** | 59.3% |
| chroma 16, hold 24 gated at 24 | 220.6 | 57 | **3.74%** | **1.32%** | 58.6% |
| chroma 16, hold 40 gated at 24 | 208.5 | 48 | 4.64% | 1.85% | 58.8% |
| ...and no hold on a whole-frame change | 206.0 | 46 | 4.80% | 2.01% | 57.2% |

The score ranks the two ungated holds as the owner did - 40 far worst, 24
next. Gated, both come to the EGA's staleness or under it, at 209-221 KB/s.
The cut release buys nothing: a cut moves the source, and the gate already
lets go. Sent to the owner as TH24 and TH40.

## The owner: both gated holds still stick - so the SCORE

The owner watched TH24 and TH40: both still stick, 40 more than 24. So the
temporal judgement in its own form, in the same scratch encoder: a pixel the
dither wants to change is held by colour (within D of the source) only while
its SCORE is under T. The score is a leaky sum - decay 0.8 a frame - of how
far the held colour is from the source, added only on frames where the
dither WANTED another colour; a pixel that changes starts again at 0. Noise
comes and goes and never builds; a colour that is wrong and stays wrong
builds and is let go, after which the pixel may hold at its new colour.

| `286-fast`, GasGas 40-60 s | KB/s | cut of 599 | stale | stuck | worst frame |
|---|---|---|---|---|---|
| hold 40, score under 60 | 253.7 | 64 | 3.24% | **0.60%** | 58.8% |
| hold 24, score under 60 | 252.2 | 54 | 2.99% | 0.67% | 60.0% |
| hold 40, score under 100 | 195.1 | 25 | 6.36% | 1.69% | 59.7% |

At 60 the stuck share is the no-hold file's (0.56%) and under the EGA's
(1.50%), for 252-254 KB/s against the no-hold 315 - a fifth of the bytes
and 100 of the 163 cut frames back. At 100 it sticks as the gated holds
did. Sent to the owner as S40T60, S24T60 and S40T100.
