# Which sixteen: a palette for the colours, not the pixels

**A measurement, not a description.** Taken 2026-10-13 on branch
`video-palette` at `c8ff34c` (`elendilon` `ab73396` merged in), on a 4-core
Xeon container with ffmpeg 6.1.1. True of that tree's encoder; not
maintained. docs/plans/VIDEO-PALETTE-PLAN.md section 10 is what it means.

## What started it: the owner looked

The owner watched docs/reports/VIDEO-RATE-2026-10-13.md's sample files -
Gas Gas Gas 40-60 s, 640 x 360, 30 fps, encoded for `286-fast` - and judged:
- **own 4 and own 8 picked only greys**: no red headlights, no faintly blue
  sky. Nowhere near the EGA's sixteen.
- **own 16** finally has the red headlights, but still overvalues greys. "A
  very dark scene, but the colours are what set it apart. **Greys can be
  dithered into existence, but colours cannot.**"
- **The EGA's sixteen looked best of all** - on colour, not only on cutting.
- The colour hold (24, 40) went down to "a blob of the same colour" where
  the EGA's is "a texture" - but own 16 at full shows the blob as well, so
  the hold was to be tried again once the palette was right.

**The score had said the opposite**: blurred-RGB PSNR rated own 16 4.8 dB
above the EGA's. In a dark scene PSNR is lightness, and lightness is greys;
the 5% of pixels that carry colour barely move it.

## A score that agrees with the eye

CIELAB of a 3 x 3 blur of the source against the same of the screen:
- **lost**: of the CHROMATIC source pixels (C* > 12 - 5.2% of this clip),
  the share drawn at under half their chroma: the red headlight drawn grey;
- **dC**: their mean chroma error (a*, b*);
- **dL**: every pixel's mean lightness error.

On 40 frames dithered by the encoder's `KnollDitherer` (no stream, no cuts):

| palette | lost | dC | dL | old PSNR |
|---|---|---|---|---|
| the EGA's sixteen | 7.6% | 11.23 | 2.46 | 28.52 |
| palettegen 16 (`--palette clip16`) | **55.7%** | 11.78 | 1.10 | 33.26 |
| palettegen 8 | 32.1% | 12.95 | 1.83 | 30.03 |
| palettegen 4 | 69.7% | 14.34 | 2.12 | 28.08 |
| EGA-seeded k-means 16 (Lab) | 38.1% | 8.14 | 1.86 | 31.29 |
| ...pixels weighted by chroma | 33.8% | 7.96 | 2.02 | 30.83 |
| **chroma-weighted k-means 16** | **7.2%** | **6.00** | **1.13** | 34.61 |
| chroma-weighted k-means 8 | 57.9% | 9.04 | 1.97 | 30.49 |
| chroma-weighted k-means 4 | 45.8% | 9.34 | 2.21 | 28.29 |

`lost` ranks them as the owner did. **Chroma-weighted k-means**: k-means in
CIELAB over the clip's pixels (every 4th row and column of up to 200
frames), each pixel weighted 1 + C*/8, seeded k-means++ by the same weight,
black fixed at entry 0, 12 rounds. It keeps the EGA's reach (7.2% lost),
halves its chroma error and has palettegen's greys. The palettes, six-bit:

- palettegen 16: a grey ramp, one dull red (49,31,29), two blue-greys;
- chroma 16: three reds (27,7,8) (50,14,14) (62,10,7), a warm yellow
  (58,53,40), four blues, the rest greys.

At 8 and 4 it does not hold yet (58% and 46% lost): with so few colours
neither population nor this weight keeps the reach. Not solved.

## The encoder, with it

A scratch copy of the encoder (`vga8_palette` replaced by the chooser,
`--palette clip16`), the same 20 s, then decoded as the player does and
scored as above:

| `286-fast` | KB/s | cut of 599 | broken | lost | dC | dL | PSNR |
|---|---|---|---|---|---|---|---|
| the EGA's sixteen | 158.0 | 27 | 0.3 s | 16.0% | 11.03 | 3.12 | 26.47 |
| palettegen 16 | 338.6 | 289 | 0.9 s | 57.6% | 11.99 | 1.54 | 31.12 |
| ...hold 24 | 227.7 | 71 | 0.5 s | 58.7% | 11.98 | 1.82 | 29.90 |
| ...hold 40 | 164.3 | 28 | 0.4 s | 61.1% | 12.17 | 2.63 | 27.54 |
| **chroma 16** | 314.7 | 163 | 0.7 s | **8.2%** | **6.04** | 1.83 | 31.40 |
| **chroma 16, hold 24** | 201.7 | **34** | 0.4 s | 15.7% | 6.46 | 1.93 | 30.71 |
| chroma 16, hold 40 | 141.0 | 21 | 0.4 s | 18.6% | 6.73 | 2.59 | 28.16 |

Lossless, what each asks for: chroma 16 368.3 KB/s (palettegen 16: 433.6),
with hold 24 209.7, with hold 40 146.5.

**The chroma sixteen is also CHEAPER than palettegen's** (368 against 434
KB/s asked): a grey ramp puts many close colours where the picture is, and
the dither moves between them; colours spread for reach are further apart.
Every figure here wants the owner's eye on the files before it means
anything; the samples were sent as CHR16, CHR16H24 and CHR16H40.
