# XTMP3 numerics: the STANDARD decoder's arithmetic (S1)

This note is the S1 track's contract for SPEC.md §121.4. The binding text is
the module docstring of [`tools/x3sim.py`](../../tools/x3sim.py), which holds
the parser rules and the format table. The constants live in
[`tools/x3tables.py`](../../tools/x3tables.py) and the ISO tables in
[`tools/x3tab_iso.py`](../../tools/x3tab_iso.py). This note records the
evidence behind those choices, the operation counts and the measured
comparison. The integrator folds it into §121.4 and §121.11.

| file | what it is |
|---|---|
| `tools/x3sim.py` | the parser (`x3p_`, the spec of `x3pars.inc`), the STANDARD integer model (`x3s_`, the spec of `x3std.asm`), a float reference, and the harness |
| `tools/x3tables.py` | the STANDARD table blob `x3std.bin` (13,888 bytes) and `x3std.inc`, `--selfcheck` |
| `tools/x3tab_iso.py` | ISO 11172-3/13818-3 tables with their provenance |
| `tools/x3fixtures.py` | the 37 fixtures, synthesized and encoded with LAME, plus byte surgery |
| `tests/xtmp3/FIXTURES.txt` | each fixture's recipe, LAME command, input-WAV and MP3 SHA-256, and expectation |
| `tests/xtmp3/X3SIM.txt` | **the port's target**: SHA-256 of the model's PCM for 141 cases |

## 1. The formats

The format table in the docstring is the contract. In short:

| domain | storage | format | why |
|---|---|---|---|
| requantised lines, M/S and intensity stereo, antialias | int32 | Q28 | a step here reaches the PCM about 7× larger (§3) |
| IMDCT inputs and internals | int16 | Q(14+k), one block scale k per subband per granule | 16-bit multiplies; a quiet band gets up to 14 more bits |
| IMDCT output, overlap | int16 | Q14 (±2) | fixed, so no scale crosses a granule |
| mono downmix | int16 | Q14 | `(a + b + 1) >> 1`, after the hybrid filterbank |
| DCT-II (32, 16, 8 points), V history | int16 | Q14 | |
| window accumulator | int32 | Q28 | 16 products, bounded below 2^31 by `--selfcheck` |
| PCM | int16 | Q15 | `(acc + 2^12) >> 13`, clipped and counted |

Every stored result is rounded half up exactly once. Every overflow saturates
and is counted per stage; output clipping has its own counter. Constants are
Q15 except D (Q14), the intensity factors (Q14) and 2^(k/4) (Q14). A
coefficient of 1.0 is 32767 in Q15, and that clamp is part of the definition.

## 2. The structure

The pipeline runs requantise, stereo, reorder, antialias, block scale, IMDCT
(36- or 3×12-point, minimp3's factorisation), frequency inversion, optional
mono mix, then synthesis. The synthesis is the N-point DCT-II of the subband
samples, a ring of the last 16 DCT vectors (the "X ring", which replaces
ISO's 1,024-word V), and 16 products an output against D.

Three structures are new and were chosen for the 8088:

* **The twiddle merged into the window.** minimp3 computes `s = co tw1 +
  si tw0`, rounds it, then forms `o w - s w9`. Here `out = o w - co (tw1 w9)
  - si (tw0 w9)` is one 32-bit sum and one rounding. It uses the same eight
  multiplies per output pair and removes one rounding from every subband
  sample. `--selfcheck` proves the sum below 2^31: `|w| + |a| + |b| <= √3` by
  Princen-Bradley.
* **The X-ring synthesis.** Output j of a slot reads one X index from each
  of the 16 most recent slots: `a_j` from the even ages and `b_j` from the
  odd ones. The window table SYNW is stored in that consumption order with
  the signs folded in.
* **The exact reduced rate.** When subbands at and above N = 32/div are
  zero, the decimated output needs only `X[div·l]`, and that is the N-point
  DCT-II of the N kept subbands. Divisor 2 therefore runs a 16-point DCT and
  divisor 4 an 8-point one. Both reuse rows `div·m` of the same window
  table. In float, the reduced output equals the decimated full-rate output
  bit for bit; this was checked on C128 and X8K16 at both divisors.

The DCT-II is recursive: `X[2k]` comes from a half-length DCT-II of
`x[n] + x[N-1-n]`, and `X[2k+1]` from a half-length DCT-IV of the
difference. The DCT-IV runs as a pre-rotation, a quarter-length complex
radix-2 FFT and a post-rotation. Every multiplier is therefore a cosine or
sine ≤ 1. Lee's algorithm (minimp3, mpg123) multiplies by up to 10.19, which
16-bit arithmetic cannot carry. Swapping this DCT for an exact one improves
the total error by only about 10% (§3).

## 3. How the formats were chosen (measured)

**First design (all int16 Q14):** 53.2 dB on X8K16, 60.0 on NEONDRV and
62.6 on C128 against mpglib, with error RMS 4.8 to 8.4 LSB. That was too
close to the brief's ~60 dB floor.

**Where the noise comes from.** The float reference was run with one
quantiser inserted at a time (RMS error in 16-bit LSB against the
unquantised float):

| quantiser inserted | X8K16 | C128 | NEONDRV |
|---|---|---|---|
| PCM rounding only | 0.28 | 0.29 | 0.29 |
| spectral lines at Q14, fixed | 4.86 | 2.73 | 3.75 |
| spectral lines at Q15, fixed | 2.54 | 1.47 | 1.91 |
| spectral lines, 15 bits below each subband's peak | 0.27 | 0.45 | 0.76 |
| spectral lines, 15 bits below each granule's peak | 0.85 | 0.81 | 1.73 |
| subband samples at Q14 | 2.87 | 1.06 | 1.79 |
| subband samples at Q15 | 1.43 | 0.58 | 0.90 |
| V (X ring) at Q14 | 0.76 | 0.80 | 0.81 |
| V at Q15 | 0.38 | 0.40 | 0.41 |

A rounding step in the spectral domain reaches the output about 7× larger,
and one in the subband domain about 5.6× (the synthesis's √32). One in V
reaches it only about 1.4× larger. With their real ranges (spectral up to
1.27 on CLIP and the ISO sine; subband samples up to 1.58 on CLIP), the
first two domains need ~3 bits more than the PCM. V does not.

**Headroom.** On real material no stage exceeded ±2.0 in absolute terms,
which is why a fixed Q14 never saturated. A block scale must leave room
above the block's own peak, though: the IMDCT grows up to 2.7 bits above
its input peak, and the DCT up to 3.3 bits above its subband peak. So:

* The spectral domain is int32. Requantisation already has the 32-bit
  product. M/S is add/adc. Intensity is rare. Only the antialias pays double
  multiplies, about 9% of a granule.
* The IMDCT runs in 16 bits at a per-subband scale k = 25 − bitlength(peak),
  which leaves 4 bits of headroom (`X3_IMDCT_HEAD`). A loud band stays at
  k = 0 (Q14) and keeps the fixed format's absolute bound. Headroom of 2 bits
  saturated on 6 fixtures and 3 bits on 2; 4 bits on none.
* The subband samples and V stay at Q14 int16, as SPEC §121.4 outlined. Carrying
  the subband samples and the DCT in int32 too ("D3") would bring the floor
  to about 0.8 to 1.1 LSB, roughly +8 dB, for about 2,200 more
  double-multiplies a granule-channel. That is about 40 to 50% more decode
  time, which on a 386 or 486 is the difference between Original and Half.
  Left as an option; not taken.

**Result (D4, the shipped model).** Every music fixture measures 60.3 to
72.0 dB against mpglib at the source rate, and every fixture's error RMS is
5.05 LSB or less. Nothing saturates on any fixture or on any ISO vector short
of `l3-si_huff` (§6).

## 4. Tolerance

`x3sim.py --compare` fails when either of these is broken:

* **Divisor 1 against `lame --decode`:** error RMS ≤ **6.0 LSB** and peak
  ≤ **48 LSB** on every fixture compared. The worst measured are RMS 5.05
  (X12K32) and peak 26 (AFTERHRS). Music fixtures must also reach SNR
  ≥ **60 dB**; the worst measured is 60.3 (AFTERHRS, 11 kHz mono at 24 kbps).
* **Divisors 2 and 4 against the float reference at the same divisor:**
  the same RMS and peak limits. The reduced synthesis is exact in float, so
  this isolates the integer arithmetic. Worst measured: RMS 4.20, peak 21.

Five fixture families do not reach 60 dB SNR at divisor 1, so the music
limit is not applied to them. They meet the absolute RMS and peak limits.
SNR is relative to the signal, and their signals are either quiet or spread
over every subband, where the fixed floor of about 3 to 5 LSB RMS dominates:

* QUIET is a −50 dBFS passage: 24.9 dB, RMS 1.93 LSB.
* NOISE is broadband: 57.0 dB.
* X8K16, M16, X12K32 and M32K are the formant voice at 8 to 32 kHz:
  58.6 to 61.3 dB.

On the ISO compliance stream `l3-compl` the error is RMS 2.49 LSB and peak 15.
That is inside ISO 11172-4's *limited accuracy* (RMS < 2^-11/√12 of full
scale = 4.62 LSB). It is not *full accuracy* (0.29 LSB), which needs more
than 16-bit subband arithmetic; the float model is 0.3 LSB. The DC column
(mean error) stays within ±0.26 LSB: rounding half up everywhere leaves a
bias of a few hundredths to a tenth of an LSB.

## 5. The comparison (measured, `x3sim.py --compare`)

Divisor 1 is measured against mpglib. Divisors 2 and 4 are measured against
the float model at the same divisor, and "leak" is the float model against
an ideal lowpass-and-decimate of mpglib's output (it includes the
transition-band behaviour of §7).

| fixture | d1 SNR dB | d1 RMS | d1 peak | d2 SNR | d2 peak | d4 SNR | d4 peak | leak d2/d4 dB |
|---|---|---|---|---|---|---|---|---|
| NEONDRV (44.1k JS 56) | 65.1 | 3.73 | 21 | 65.7 | 20 | 66.5 | 18 | 76.4 / 45.9 |
| NIGHTSIG (22k JS 40) | 63.0 | 4.29 | 21 | 63.7 | 21 | 64.8 | 17 | 44.9 / 39.9 |
| AFTERHRS (11k mono 24) | 60.3 | 4.88 | 26 | 61.8 | 20 | 63.2 | 17 | 45.6 / 38.5 |
| C128 (L/R 128) | 65.8 | 3.30 | 15 | 66.1 | 14 | 66.9 | 14 | 87.5 / 86.2 |
| C320 (JS 320) | 64.1 | 4.36 | 22 | 65.6 | 16 | 67.1 | 12 | 40.3 / 41.5 |
| VBR2 (JS -V2) | 63.9 | 4.28 | 23 | 65.3 | 18 | 66.8 | 12 | 40.7 / 41.5 |
| MSF (forced M/S) | 65.5 | 3.42 | 16 | 65.8 | 16 | 66.6 | 13 | 89.6 / 85.7 |
| MONO44 (short blocks) | 65.0 | 3.80 | 21 | 66.2 | 17 | 67.7 | 12 | 35.9 / 36.4 |
| DUAL44 | 65.8 | 3.30 | 15 | 66.1 | 14 | 66.9 | 14 | 87.5 / 86.2 |
| RSV32 (reservoir 335 B) | 65.8 | 3.29 | 16 | 66.4 | 15 | 67.2 | 13 | 89.5 / 39.4 |
| SWEEP | 70.9 | 3.14 | 18 | 71.3 | 16 | 71.6 | 15 | 27.8 / 26.1 |
| BASS | 72.0 | 2.86 | 15 | 72.1 | 13 | 72.4 | 12 | 93.4 / 91.3 |
| NOISE | 57.0 | 3.86 | 23 | 57.4 | 16 | 57.2 | 12 | 24.6 / 19.1 |
| SILENT | exact | 0 | 0 | exact | 0 | exact | 0 | — |
| QUIET (−50 dBFS) | 24.9 | 1.93 | 9 | 25.4 | 7 | 27.4 | 7 | 41.4 / 40.8 |
| CLIP (clips 8,416 / 8,413) | 74.2 | 4.90 | 24 | 75.3 | 20 | 76.8 | 16 | 36.7 / 33.7 |
| C48 (48k) | 66.7 | 3.03 | 15 | 67.0 | 15 | 67.6 | 13 | 87.5 / 86.1 |
| M32K (32k voice) | 61.3 | 3.56 | 20 | 62.3 | 16 | 64.1 | 16 | 39.4 / 40.4 |
| M22 (MPEG-2 22k) | 63.3 | 4.39 | 19 | 64.4 | 19 | 65.6 | 15 | 40.1 / 41.1 |
| M24 (MPEG-2 24k) | 65.5 | 3.36 | 17 | 65.8 | 14 | 66.2 | 15 | 88.0 / 85.8 |
| M16 (MPEG-2 16k voice) | 59.6 | 4.28 | 21 | 61.2 | 16 | 62.7 | 13 | 38.9 / 35.8 |
| X8K16 (2.5, 8k voice) | 58.6 | 4.53 | 23 | 60.1 | 18 | 61.6 | 16 | 36.6 / 26.6 |
| X11K24 (2.5, 11k) | 63.4 | 4.43 | 22 | 65.0 | 15 | 66.6 | 12 | 38.6 / 39.0 |
| X12K32 (2.5, 12k) | 59.8 | 5.05 | 21 | 61.3 | 17 | 62.7 | 13 | 28.9 / 40.0 |
| IS44 (MPEG-1 intensity) | 65.5 | 3.52 | 19 | 66.0 | 15 | 67.4 | 12 | 47.1 / 44.2 |
| ISMS44 (intensity + M/S) | 65.1 | 3.71 | 21 | 65.6 | 16 | 67.0 | 15 | 47.2 / 44.2 |
| IS22 (MPEG-2 intensity, vs float) | 64.0 | 4.23 | 18 | 64.9 | 16 | 66.2 | 14 | 40.6 / 40.3 |
| BID3V1, BID3BIG (tagged base) | 62.1 | 3.18 | 17 | 62.8 | 15 | 64.4 | 13 | 44.3 / 37.8 |

**Malformed fixtures** end as documented, with no exception:

* Refused by name: BFREE (FREE FORMAT) and BLAYER2 (LAYER II).
* Recovered with counts: BTRUNC (truncated 1), BBADSIDE (badside 3),
  BHUGE23 (badside 1) and BBIGVAL (badside 1).
* Decoded to the clean stream's PCM exactly: BGARBAGE (resync 2, 377 bytes
  skipped), BID3BIG (title and artist from TIT2/TPE1) and BVBRI (VBRI frame
  count 43).

**The float model** reaches 102 to 113 dB against mpglib on every valid
fixture (peak 1 LSB). On the ISO vectors in minimp3's repository it reaches
96.0 to 103.2 dB, the integer model 52 to 66 dB (RMS 2.0 to 4.6 LSB). That
covers every stream: `compl`, `he_32/44/48khz`, `hecommon`, `si`,
`si_block`, `test45` and `test46`.

## 6. Where mpglib is not the reference

* **MPEG-2 intensity stereo.** mpglib treats is_pos 7 as illegal in MPEG-2,
  and on an all-zero right channel it leaves part of the intensity bands
  unfilled. ISO and minimp3 make 2^slen−1 illegal and give the top band the
  default position when the band below is not empty. On the ISO vectors
  `l3-test45/46`, mpglib scores 16.7 and 14.2 dB; x3sim's float model
  scores 97.2 and 99.7 dB, and its integer model 56.8 and 58.4 dB. IS22 is
  therefore compared against the float model (`expect: decode iso`). mpglib
  scores 7.7 dB on it.
* **Reservoir underflow.** mpglib decodes silence for such a frame and so
  does x3sim. minimp3 drops the frame's samples.
* **Mixed-flag start and stop blocks.** Subbands 0 and 1 take the long
  normal window for every block type (ISO dist10). minimp3 and mpglib agree.
  The rule matters on `l3-si_block`: 26 dB without it, 96 with it.

## 7. Reduced rate: alias leakage (`x3sim.py --leak`, SWEEP)

| divisor | kept tones 0.5–0.98 N′ | image of a tone at 1.02–1.05 N′ | 1.05–1.10 N′ | 1.10–1.20 N′ | beyond |
|---|---|---|---|---|---|
| 2 | −0.04 … +0.11 dB | −28.8 dBc | −45.1 dBc | −89.5 dBc | < −105 dBc |
| 4 | −0.02 … +0.06 dB | −19.7 dBc | −38.7 dBc | −86.3 dBc | < −106 dBc |

The images come from the encoder's analysis. It left part of a tone just
above the new Nyquist N′ in the top kept subband, and the zeroed subband
above would have cancelled it. The leak is confined to the 10% above N′,
which is the 32-band prototype's own transition band. Kept tones are flat.
The butterflies across the cut are skipped. An option left to the S8
listening tests is attenuating the top kept subband, which trades about
6 dB of leak for the top 1/N of the band.

## 8. Operation counts and a first 8088 estimate (`x3sim.py --opcount`)

**The timing model** charges cycles per counted operation. Each charge takes
the larger of execution time from the 8088's tables and fetch at 4 cycles
per instruction byte, plus memory operands:

| operation | cycles | basis |
|---|---|---|
| mul | 150 | `imul r16` 128–154, one operand from memory |
| mul32 | 330 | int32 × int16: `mul` + `imul`, three adds, sign fix-up |
| add | 16 | `add` + `jo` (not taken), 4 bytes, fetch-bound |
| add32 | 28 | `add` + `adc` + `jo` |
| shift | 40 | `add ax,4000h / adc dx,0 / shl ax,1 / rcl dx,1` |
| read | 18 | `mov r16,[bx+si+d]`: 8 + EA + 4 for a word on an 8-bit bus |
| Huffman symbol | 220 | 1.04 lookups (8-bit first level, a byte move), the leaf's shift, signs, stores, tests |

Counts are per granule, measured on the fixtures. The first three blocks
are mono (1 source channel); the last two are C128, an L/R stereo source.

| stage | 11k mono d1 | 44.1k mono d1 | 44.1k mono d2 | 44.1k mono d4 | stereo d1 | stereo→mono d1 |
|---|---|---|---|---|---|---|
| Huffman symbols | 205 | 131 | 131 | 131 | 253 | 253 |
| requant mul | 244 | 132 | 132 | 132 | 160 | 160 |
| alias mul32 | 182 | 833 | 403 | 188 | 1,967 | 1,967 |
| scale add32/shift | 576 | 576 | 288 | 144 | 1,152 | 1,152 |
| IMDCT mul / add | 2,555 / 3,227 | 2,765 / 4,298 | 1,382 / 2,149 | 691 / 1,075 | 5,627 / 9,092 | 5,627 / 9,092 |
| mix add | – | – | – | – | – | 1,152 |
| DCT mul / add | 2,034 / 3,690 | 2,034 / 3,690 | 810 / 1,440 | 306 / 522 | 4,068 / 7,380 | 2,034 / 3,690 |
| window mul | 8,748 | 8,748 | 4,284 | 2,088 | 17,496 | 8,748 |
| **≈ 8088 cycles** | **2.94 M** | **3.18 M** | **1.56 M** | **0.77 M** | **6.48 M** | **4.30 M** |

The 11k column is X11K24 at divisor 1; at divisor 2 it is 1.46 M. M/S
adds 1,152 add32 a granule (about 32 k cycles); intensity adds one mul32 per
intensity line.

Shares at 44.1k mono, divisor 1:

| stage | share |
|---|---|
| window | 56% |
| IMDCT | 19% |
| DCT | 13% |
| antialias | 9% |
| Huffman | 1% |
| requantise | 1% |

A real-time granule on a 4.77 MHz 8088 is 62 k cycles at 44.1 kHz and 249 k
at 11.025 kHz. By this model the STANDARD decoder is about 51× and 12× too
slow there. Halving to 5.5 kHz output still leaves it about 6× too slow, and
X3_FLOOR forbids that rung anyway. This is why the XT path is the XT backend
or Decode first. These figures are a model; S6 replaces them with MartyPC
cycle counts.

## 9. Huffman first level (`x3sim.py --huffstudy`, 724,493 symbols)

| first / sub bits | table bytes | lookups a symbol |
|---|---|---|
| 4 / 4 | 4,424 | 1.312 |
| 6 / 4 | 5,372 | 1.140 |
| **8 / 4 (chosen)** | **8,856** | **1.040** |
| 8 / 8 | 9,612 | 1.038 |

With 8 bits, the bit window's high byte is the index, so taking it is a byte
move. 6 or 4 bits would cost a shift by CL (8 + 4n) on every symbol. A
tree shorter than 8 bits (1, 2, 3, 5, 6, count1 B) indexes its first level
with its longest code. The entry formats are in x3tables.py.

## 10. The table blob (`x3tables.py --report`)

`x3std.bin`, 13,888 bytes, CRC-32 84E29B65; every table ships raw:

| table | bytes | why raw |
|---|---|---|
| HUFDIR + HUF | 68 + 8,856 | building them from the 1,474 codewords at OPEN needs ~3.4 KB of codebook to save ~5.4 KB, plus a recursive builder |
| P43M + P43E | 2,054 + 1,027 | x^(4/3) has no exact integer recurrence a 16-bit CPU runs cheaply |
| P43F, IS, AA, DCT9 | 8, 186, 32, 14 | tiny |
| IMW, ROT | 300, 150 | products of rounded trigonometry; a recurrence would not reproduce the per-entry rounding |
| SYNW, SYNIDX | 1,024, 64 | |D[i]| = |D[512−i]| would save 512 bytes for a permuting loop: not worth the code |

SPEC §121.10's expansion argument belongs to the XT packs (224 KB). The
STANDARD backend's tables are 14 KB, and it is never loaded first on an XT.

## 11. Open items

* **Subband-domain overload.** A stream whose subband samples exceed ±2.0,
  more than 6 dB past full scale, saturates at Q14 instead of clipping only
  at the output. Its neighbouring granules are disturbed through the overlap
  and V. Only `l3-si_huff` does this (a synthetic Huffman-table test with
  spectral lines up to 4.9); CLIP, a real overdriven master, peaks at 1.58.
* **Mode changes mid-stream** (`l3-he_mode`) are decoded and adapted; the
  transitions differ from minimp3's per-frame output, as both decoders'
  stale histories differ.
* **Mixed blocks at MPEG-2.5 8 kHz** are refused as a bad granule (ISO does
  not define them; LAME never writes mixed blocks). The mixed-block code
  path is exercised only by the ISO vector `l3-si_block`, since LAME writes
  none.
* **CRC is parsed and not checked** (SPEC.md 121.2).
* The 8088 cycle figures are a model, to be replaced by S6's measurements.
  The D3 option (int32 subband samples and DCT, about +8 dB for about
  +40–50% time) is recorded in §3 for whoever revisits the ladder on a 486.
