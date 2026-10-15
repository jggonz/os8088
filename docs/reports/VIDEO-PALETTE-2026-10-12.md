# Mid-play palettes for the Video Player: four clips measured

**A measurement, not a description.** Taken 2026-10-12 on branch
`video-palette` at `d65b91e` (cut from `elendilon`), on a 4-core Xeon
container with ffmpeg 6.1.1 and numpy 2.5.3. It is true of that tree's
encoder - its ditherers, its `palettegen` pass, its 286 budget - and is not
maintained against later trees. docs/plans/VIDEO-PALETTE-PLAN.md is what it
means.

## The clips

The owner's four (2026-10-12), not committed and not to be: anime openings,
chosen by the owner as what they encode.

| clip | source | measured | shots |
|---|---|---|---|
| GasGas | 1280 x 720, 30 fps | **40-114 s: the owner's own file's window** | 41 |
| Cowboy Bebop OP | 640 x 480, 23.976 | the whole 90 s | 51 |
| Last Exile OP | 854 x 480, 24 | the whole 104 s | 79 |
| Thundercats | 960 x 720, 23.976 | the whole 78 s | 101 |

The owner's GasGas `.V88` (`03-640FS.V88`, its options block read back) is
`--preset vga4-full --profile 286-vga` (`286-speeddemon`), 640 x 360 on
mode 12h at 30 fps, PCM8 at 11,025 Hz, `--palette auto` - the EGA's
sixteen - 188.5 KB/s and 98.4% of the model's CPU over 74 s.

## 1. The picture: `tools/os88palsim.py`

Each clip at 12 fps through the encoder's own ditherers, every frame scored
as the PSNR of a 3 x 3 box blur of the source against the same blur of the
screen. 320 x 180 except GasGas's sixteen colours, which is the owner's
640 x 360. Policies as the tool's docstring and VIDEO-PALETTE-PLAN 3 define
them: `G` one palette for the clip, `S` one a shot swapped at the cut, `SP`
*P* a shot swapped, `RP` *P* a shot recycled (invisible on any screen), `FP`
`RP` plus `VBL` live slots in the blank (flipped). Mean dB / worst frame /
mean of the shots' cut frames / pixels changed a frame.

```
python3 tools/os88palsim.py CLIP [--start 40 --secs 74] --k 256 --p 160
python3 tools/os88palsim.py CLIP [...] --k 16 --p 8 --vbl 16 [--size 640x360]
```

### 1.1 Sixteen colours (VGA4)

| clip | `EGA` | `G` (clip16) | `S` (a bank a shot) | `RP` (8 + 8) | `FP` |
|---|---|---|---|---|---|
| GasGas 640 x 360 | 28.24 / 23.3 | 34.20 / 11.6 | **35.66** / 22.9 | 32.51 / 16.2 | 30.31 / 8.5 |
| Bebop | 29.28 / 19.0 | 29.68 / 13.9 | **39.09** / 19.1 | 34.26 / 10.1 | 35.99 / 17.3 |
| Last Exile | 32.75 / 19.3 | 33.21 / 13.0 | **35.85** / 17.6 | 33.14 / 11.8 | 32.84 / 15.8 |
| Thundercats | 28.68 / 23.0 | 27.69 / 16.4 | **31.93** / 16.8 | 27.59 / 10.0 | 28.35 / 12.3 |

`S` over `G`, a shot: GasGas median +1.66 dB (19 of 41 shots +2 or more),
Bebop **+8.91** (50 of 51), Last Exile +2.84 (49 of 79), Thundercats +3.68
(68 of 101).

Pixels changed a frame - the bytes' proxy: GasGas `EGA` 17,668, `G` 69,045,
`S` 70,417; Bebop 7,448 / 8,789 / 12,321; Last Exile 14,946 / 18,902 /
25,658; Thundercats 18,970 / 20,614 / 27,396.

Read: **a bank per shot wins on every clip**, by 3.1 to 9.8 dB over the
EGA's sixteen. One palette for the whole clip is worth it on GasGas (+6.0)
and nearly nothing - or less than nothing - on the other three, whose shots
are each their own colour. Halving sixteen to recycle (`RP`) is never worth
it: it is at or under one palette for the clip on three clips (Bebop's
clip sixteen being poor enough to lose to it), and 2 to 5 dB under a bank a
shot on all four. The worst frames are low
everywhere because a 16-colour dither of a flash or a colour-tinted panel is
poor whatever the palette; `S` raises them on three clips of four.

### 1.2 256 colours (VGA8), *P* = 160, `VBL` = 48

| clip | `G` | `S` | `SP` | `RP` | `FP` |
|---|---|---|---|---|---|
| GasGas | 34.42 / 27.0 | 34.74 / 31.7 | 34.66 / 31.7 | 34.45 / 31.6 | 34.49 / 31.6 |
| Bebop | 41.03 / 32.3 | 42.76 / 33.1 | 42.37 / 32.1 | 42.63 / 21.3 | 42.78 / 27.8 |
| Last Exile | 39.80 / 15.6 | 42.27 / 30.6 | 41.84 / 29.7 | 41.82 / 22.8 | 41.94 / 30.2 |
| Thundercats | 36.88 / 22.1 | 39.42 / 31.8 | 38.87 / 32.1 | 38.09 / 16.3 | 38.53 / 24.0 |

`S` over `G`, a shot: GasGas median +0.13 (3 of 41 shots +2 or more), Bebop
+1.96 (25 of 51), Last Exile **+2.72** (51 of 79), Thundercats +2.65 (72 of
101).

DAC traffic, `RP`: GasGas 116 patches, 2,127 slots (6,381 bytes over 74 s);
Bebop 154 / 4,397 (13,191 over 90 s); Last Exile 321 / 9,005 (27,015 over
104 s); Thundercats 518 / 11,264 (33,792 over 78 s), the largest one 153
slots. `FP` needed its blank at 9, 15, 40 and 72 cuts, each at the 48 cap.

Read: **recycling gets most of the MEAN of a per-shot palette** (within
0.15 dB of `S` on three clips, 1.3 on Thundercats) **and not its worst
frame**: a cut whose new shot does not
fit the slack the old one left draws one poor frame - 21.3 dB on Bebop
against `G`'s 32.3 - and then recovers as the old shot's slots free up. On a
flipped screen 48 slots in the blank take most of that back (27.8, 30.2).
Section 1.3 is the same at *P* = 128.

### 1.3 *P* = 128

| clip | `G` | `S` | `SP` | `RP` | `FP` |
|---|---|---|---|---|---|
| Bebop | 41.03 / 32.3 | 42.76 / 33.1 | 42.14 / 32.0 | 42.83 / **21.2** | 42.93 / 32.5 |
| Last Exile | 39.80 / 15.6 | 42.27 / 30.6 | 41.59 / 27.3 | 41.73 / **23.1** | 41.82 / 29.8 |

**A smaller cap did not fix the worst frame**, and the reason is the finding
of this section. Instrumented on Bebop (`RP`, *P* = 128), the slots on the
glass at the frame before each bad cut were **250 to 256** - for shots of
128 colours. The ditherer's nearest-colour table covers all 256 DAC
entries, so a shot spreads its pixels over every slot left holding a colour
near its own, from any earlier shot, pins all of them, and the next shot
finds no room: at Bebop's shot 32, 109 of its colours were still missing
at its cut, and the cut frame scored 21.2 dB against the clip palette's 43.

So recycling needs a FENCE: the dither sees only the shot's own slots (and
0), every other slot is unreachable, and an old shot's slots come free as
its pixels are redrawn. `--fence` is that; section 1.4 is it measured.

### 1.4 Fenced, *P* = 160

| clip | `G` | `S` | `RP` unfenced | `RP` fenced | blank needed (`FP`) |
|---|---|---|---|---|---|
| GasGas | 34.42 / 27.0 | 34.74 / 31.7 | 34.45 / 31.6 | 34.45 / 31.6 | 0 cuts |
| Bebop | 41.03 / 32.3 | 42.76 / 33.1 | 42.63 / 21.3 | 41.98 / **26.3** | 3 cuts |
| Last Exile | 39.80 / 15.6 | 42.27 / 30.6 | 41.82 / 22.8 | 41.80 / **26.7** | 0 cuts |
| Thundercats | 36.88 / 22.1 | 39.42 / 31.8 | 38.09 / 16.3 | 38.57 / **25.6** | 1 cut, 6 slots |

`python3 tools/os88palsim.py CLIP ... --fence`. Fenced, the next shot FITS:
the flipped arm needed the blank at 0 to 3 cuts per clip where it needed it
at 9 to 72, so on VGA8 recycling alone does the work and no retrace wait is
needed. The worst frames come up on every clip that had one - 4 to 9 dB -
and the mean stays within 0.05 dB of unfenced on three clips and gives up
0.65 on Bebop, whose shots were borrowing each other's colours to good
effect. The worst frames are still under `SP`'s (26-27 dB against ~30):
what is left is the allocator's (it overwrites the outgoing shot's unused
colours on its last frame, and places the incoming shot's by popularity),
which is the build's to tune and not a property of the mechanism.

DAC traffic, fenced: 55 to 254 patches a clip, 6 to 42 KB in all, the
largest 153 slots.

## 2. The bytes: the encoder on the 286

`os88venc` on GasGas 40-60 s, `--preset vga4-full --profile 286-speeddemon
--audio pcm8 --rate 11025`, as the owner's file but 20 s. Arms e1 on are on
`--screen 640x400` (640 x 300), the only screens that take a palette of
their own, so e1 against e2 is the palette and nothing else.

| arm | | KB/s | frames cut |
|---|---|---|---|
| e0 | the owner's: screen 0, 640 x 360, EGA's sixteen, 30 fps | 160.5 | 25 of 599 |
| e1 | 640 x 400 screen, EGA's sixteen | 137.9 | 14 of 599 |
| e2 | ...its own sixteen (`clip16`) | **363.0** | **135 of 599** |
| e3 | ...flipped | 269.5 | **333 of 600** |
| e4 | e2 with `--vga4-stable 48` | 357.8 | 124 of 599 |
| e5 | e2 at 15 fps, flipped | 252.1 | 94 of 300 |
| e6 | e2 at 15 fps | 278.6 | 71 of 300 |
| e7 | e2 with the luma order GRAY-CODED onto the planes (below) | 338.1 | 109 of 599 |
| e8 | e3 (own sixteen, flipped) on `486-dx2-66` | 377.3 | 65 of 599 |

Read: **on the 286 an own sixteen costs 2.6 times the bytes of the EGA's**,
and the budget cuts five to ten times the frames. Doubling the stability
buys 1.4%: the cost is not the dead band but the palette's DENSITY - a
clip's sixteen are close together, so Knoll's pattern moves between them on
small changes in the source where the EGA's, 85 levels apart, holds.
Flipping (e3, e5) makes it far worse, every record decoded twice on a
machine already at the budget. On the 486 class (e8) the same flipped own
sixteen is 377 KB/s with 65 frames of 599 cut, 54 of them by the CPU's
average - mostly carried.

**e7 is one lever that looked worth one encode.** `vid.PLANE_CODES[16]` is
the identity, so the clip's sixteen - sorted darkest first - put colours 7
and 8, neighbours that the dither alternates between, at pixel values 0111b
and 1000b: every alternation there changes all four planes, and VGA4's cost
is per plane (98.1.3.2). A Gray code (`k ^ (k >> 1)`) makes every pair of
luma neighbours differ in ONE plane. It took
the stream 363.0 to 338.1 KB/s (-7%) and the frames cut 135 to 109 (-19%),
with the log confirming the values (`0 1 3 2 6 7 5 4 C D F E A B 9 8`). Real,
cheap, and not the answer: the density costs the rest. (It also moves pixel
value 0Fh off the brightest colour, which the full screen's text relies on,
98.3.8.2 - a black-at-0 path through all sixteen values cannot end on 0Fh,
the two having the same parity - so taking it means choosing the text's
colour another way.)

## 3. CGA4: the pick, and per shot

`cga4_pick` (whole clip; and each shot, counted):

| clip | whole clip | per shot, most common |
|---|---|---|
| GasGas | 40h: mode 5 dim, black | 40h x27, 50h x9, 20h x2, 58h x2, 00h x1 |
| Bebop | 40h | **00h green/red/brown x18**, 20h x13, 40h x9, 50h x8 |
| Last Exile | 58h: mode 5 bright, dark grey | 28h x17, 40h x15, 48h x10, 50h x9, 58h x8, 20h x7 |
| Thundercats | 40h | 48h x32, 40h x22, 28h x6, 58h x6, 00h x4, 4Fh x4 |

The RMS distance to the nearest of the four (lower is better), over the
whole clip at 2 fps and 160 x 100 - the same metric `cga4_pick` minimises -
for each set at its best background:

| clip | m5 dim | m5 bright | set 1 dim | set 1 bright | set 0 dim | set 0 bright |
|---|---|---|---|---|---|---|
| GasGas | **51.7** | 55.8 | 52.5 | 58.9 | 68.5 | 59.2 |
| Bebop | **75.0** | 85.6 | 91.6 | 113.4 | 100.2 | 91.1 |
| Last Exile | **87.9** | 94.9 | 89.2 | 96.8 | 122.6 | 123.2 |
| Thundercats | **86.3** | 102.7 | 92.5 | 107.0 | 121.7 | 116.5 |

Mode 5's dim set - black, cyan, red, light grey - is first on all four at
this sampling (`cga4_pick`'s own subsample takes 58h for Last Exile, a near
tie). Per shot the pick moves a lot more on Bebop and Thundercats than the
whole-clip answer suggests.

## 4. Two things to know before re-running it

- **A shot that is pure black has a palette of one colour.** Bebop and
  Thundercats both open on one; the first cut of the tool fell over on it.
- **The tool models the picture, not the stream.** Its `S` is a quality
  ceiling the 286 cannot reach at 30 fps on these clips (section 2); its
  pixels-changed column is the only thing in it that speaks to bytes.
