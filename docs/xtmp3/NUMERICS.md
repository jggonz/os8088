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
| `tools/x3fixtures.py` | the 37 fixtures: synthesized, encoded with LAME, plus byte surgery |
| `tests/xtmp3/FIXTURES.txt` | each fixture's recipe, LAME command, input-WAV and MP3 SHA-256, and expectation |
| `tests/xtmp3/X3SIM.txt` | **the port's target**: SHA-256 of the model's PCM for 141 cases |

## 1. The formats

The format table in the docstring is the contract. In short:

| domain | storage | format | why |
|---|---|---|---|
| requantised lines, M/S and intensity stereo, antialias | int32 | Q28 | a step here reaches the PCM ~7× larger (§3) |
| IMDCT inputs and internals | int16 | Q(14+k), a block scale k per subband per granule | 16-bit multiplies in the most-multiplied stage before the window |
| IMDCT output (subband samples), overlap, mono mix, DCT-II | int32 | Q28 | a step here reaches the PCM ~5.6× larger (§3) |
| V history (16 DCT vectors a channel) | int16 | Q14 | a step costs the PCM only 0.8 LSB RMS; feeds `imul` directly |
| window accumulator | int32 | Q28 | 16 products, bounded below 2^31 by `--selfcheck` |
| PCM | int16 | Q15 | `(acc + 2^12) >> 13`, clipped and counted |

Every stored result is rounded half up exactly once. Every overflow saturates
and is counted per stage; output clipping has its own counter.

Constants are Q15, except D, the intensity factors and 2^(k/4), which are
Q14. A coefficient of 1.0 is 32767 in Q15, and that clamp is part of the
definition. No scale crosses a block: the block scale k lives only inside
one subband's IMDCT, and everything carried to the next granule (overlap,
V) is at a fixed format.

## 2. The structure

The pipeline runs requantise, stereo, reorder, antialias, block scale, IMDCT
(36- or 3×12-point, minimp3's factorisation), frequency inversion, optional
mono mix, then synthesis. The synthesis is the N-point DCT-II of the subband
samples, a ring of the last 16 DCT vectors (the "X ring", which replaces
ISO's 1,024-word V), and 16 products an output against D.

Three structures are new and were chosen for the 8088:

* **The twiddle merged into the window.** minimp3 computes `s = co tw1 +
  si tw0`, rounds it, then forms `o w - s w9`. Here `out = o w - co (tw1 w9)
  - si (tw0 w9)` is one sum and one rounding. It uses the same eight
  multiplies per output pair and removes a rounding from every subband
  sample. `--selfcheck` proves the 16-bit part's 32-bit sum bounded: `|w| +
  |a| + |b| <= √3` by Princen-Bradley.
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
sine ≤ 1. Lee's algorithm (minimp3, mpg123) multiplies by up to 10.19.
Replacing this DCT with an exact one moved the int16 design's error by only
about 10%.

## 3. How the formats were chosen (measured)

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
1.27 on CLIP and on the ISO sine; subband samples up to 1.58 on CLIP), the
first two domains need ~3 bits more than the PCM. V does not.

**Headroom.** On real material no stage exceeded ±2.0 in absolute terms.
A block scale must leave room above the block's own peak, though: the IMDCT
grows up to 2.7 bits above its input peak, and the DCT up to 3.3 bits above
its subband peak. The IMDCT's block scale is k = 25 − bitlength(peak), which
leaves 4 bits of headroom (`X3_IMDCT_HEAD`). Headroom of 2 bits saturated
on 5 of the 6 fixtures tried and 3 bits on 2; 4 bits on none of the 37. A
loud band stays at k = 0 (Q14) and keeps the fixed format's absolute bound.

**Three designs, measured (`--compare` against mpglib, divisor 1):**

| design | X8K16 | AFTERHRS | NEONDRV | C128 | QUIET RMS | 8088 cycles, 44.1k mono granule |
|---|---|---|---|---|---|---|
| D1: every stage int16 Q14 | 53.2 dB | – | 60.0 | 62.6 | – | ≈ 3.0 M |
| D4: int32 spectral domain, block-scaled IMDCT, int16 Q14 subband samples, overlap and DCT | 58.6 | 60.3 | 65.1 | 65.8 | 1.93 LSB | 3.18 M |
| **D5 (shipped): D4 with the subband samples, overlap and DCT in int32 Q28** | **63.8** | **65.2** | **68.8** | **67.7** | **0.91 LSB** | **3.69 M** |

D5 costs ~16% more cycles than D4 (model, §8) for 3 to 5 dB and half the
quiet floor. It was taken because the STANDARD backend is the product's
"proper decoder", and the Half and Quarter rungs exist for the machines it
slows. The remaining floor is V's Q14 (0.8 LSB), the block-scaled IMDCT of
loud bands at Q14, and the PCM rounding. Carrying the IMDCT's inside in
int32 too would double its ~2,800 multiplies a granule-channel for perhaps
1 dB more. Not taken.

## 4. Tolerance

`x3sim.py --compare` fails when any of these is broken:

* **RMS error ≤ 4.0 LSB** on every fixture compared. The worst measured is
  3.11 (CLIP).
* **Peak error ≤ 32 LSB** on every fixture compared. The worst measured is
  19 (CLIP).
* **SNR ≥ 60 dB** on every fixture whose reference is louder than −40 dBFS
  RMS. The worst measured is 62.8 (NOISE, divisor 4) and 63.8 at divisor 1
  (X8K16).

At divisor 1 the reference is `lame --decode` (mpglib). At divisors 2 and 4
it is the float model at the same divisor: the reduced synthesis is exact in
float, so this isolates the integer arithmetic. The SNR limit is waived
below −40 dBFS because a quiet passage's SNR measures its level, not the
decoder. QUIET, a −50 dBFS chord, measures 31.5 dB with RMS 0.91 and peak 3,
and is held by the RMS and peak limits alone. SILENT decodes to exact zeros.

The DC column (mean error) stays within ±0.25 LSB; rounding half up
everywhere leaves a bias of hundredths of an LSB. On the ISO compliance
stream `l3-compl` the error is RMS 2.17 LSB and peak 10. That is inside ISO
11172-4's *limited accuracy* (RMS < 2^-11/√12 of full scale = 4.62 LSB). It
is not *full accuracy* (0.29 LSB), which would need V and the IMDCT inside
in more than 16 bits; the float model is 0.3 LSB.

## 5. The comparison (measured, `x3sim.py --compare`)

**Every fixture at every divisor** (d1 against mpglib, d2 and d4 against the
float model):

| fixture | d1 SNR dB | d1 RMS | d1 peak | d2 SNR / peak | d4 SNR / peak | leak d2 / d4 dB |
|---|---|---|---|---|---|---|
| NEONDRV (44.1k JS 56) | 68.8 | 2.44 | 13 | 68.8 / 13 | 68.8 / 12 | 76.4 / 45.9 |
| NIGHTSIG (22k JS 40) | 66.7 | 2.79 | 15 | 66.7 / 14 | 66.8 / 15 | 44.9 / 39.9 |
| AFTERHRS (11k mono 24) | 65.2 | 2.79 | 16 | 65.2 / 15 | 65.2 / 15 | 45.6 / 38.5 |
| C128 (L/R 128) | 67.7 | 2.68 | 12 | 67.7 / 12 | 67.9 / 12 | 87.5 / 86.2 |
| C320 (JS 320) | 69.0 | 2.48 | 11 | 69.0 / 10 | 69.3 / 10 | 40.3 / 41.5 |
| VBR2 (JS -V2) | 68.8 | 2.43 | 12 | 68.8 / 12 | 69.1 / 11 | 40.7 / 41.5 |
| MSF (forced M/S) | 67.6 | 2.68 | 13 | 67.6 / 12 | 67.8 / 13 | 89.6 / 85.7 |
| MONO44 (short blocks) | 69.8 | 2.21 | 18 | 69.9 / 10 | 69.9 / 10 | 35.9 / 36.4 |
| DUAL44 | 67.7 | 2.68 | 12 | 67.7 / 12 | 67.9 / 12 | 87.5 / 86.2 |
| RSV32 (reservoir 335 B) | 69.4 | 2.18 | 11 | 69.4 / 11 | 69.4 / 10 | 89.5 / 39.4 |
| SWEEP | 71.9 | 2.80 | 16 | 72.1 / 13 | 72.3 / 12 | 27.8 / 26.1 |
| BASS | 73.4 | 2.42 | 11 | 73.4 / 10 | 73.4 / 10 | 93.4 / 91.3 |
| NOISE | 64.6 | 1.60 | 15 | 64.2 / 12 | 62.8 / 12 | 24.6 / 19.1 |
| SILENT | exact | 0 | 0 | exact | exact | – |
| QUIET (−50 dBFS) | 31.5 | 0.91 | 3 | 31.6 / 3 | 31.6 / 3 | 41.4 / 40.8 |
| CLIP (clips 8,405 / 8,413) | 78.2 | 3.11 | 19 | 78.3 / 15 | 78.5 / 13 | 36.7 / 33.7 |
| C48 (48k) | 68.7 | 2.41 | 12 | 68.7 / 12 | 68.7 / 11 | 87.5 / 86.1 |
| M32K (32k voice) | 67.0 | 1.85 | 13 | 67.0 / 12 | 67.0 / 11 | 39.4 / 40.4 |
| M22 (MPEG-2 22k) | 67.5 | 2.70 | 15 | 67.5 / 13 | 67.6 / 12 | 40.1 / 41.1 |
| M24 (MPEG-2 24k) | 67.0 | 2.84 | 13 | 67.0 / 13 | 66.9 / 14 | 88.0 / 85.8 |
| M16 (MPEG-2 16k voice) | 64.7 | 2.38 | 14 | 64.7 / 13 | 64.7 / 12 | 38.9 / 35.8 |
| X8K16 (2.5, 8k voice) | 63.8 | 2.47 | 13 | 63.8 / 13 | 63.7 / 14 | 36.6 / 26.6 |
| X11K24 (2.5, 11k) | 69.3 | 2.24 | 12 | 69.4 / 12 | 69.4 / 11 | 38.6 / 39.0 |
| X12K32 (2.5, 12k) | 64.4 | 2.97 | 13 | 64.4 / 13 | 64.4 / 13 | 28.9 / 40.0 |
| IS44 (MPEG-1 intensity) | 69.3 | 2.28 | 11 | 69.3 / 11 | 69.3 / 9 | 47.1 / 44.2 |
| ISMS44 (intensity + M/S) | 69.1 | 2.33 | 12 | 69.1 / 12 | 69.2 / 11 | 47.2 / 44.2 |
| IS22 (MPEG-2 intensity; vs float) | 67.9 | 2.68 | 11 | 68.0 / 11 | 67.9 / 10 | 40.6 / 40.3 |
| BID3V1, BID3BIG (tagged base) | 67.5 | 1.71 | 9 | 67.5 / 9 | 67.6 / 9 | 44.3 / 37.8 |

**The mono downmix** (one synthesis of the averaged subband samples) is held
against the float model's own downmix. Every stereo fixture at divisor 1
measures 63.3 to 77.6 dB, RMS ≤ 2.65 and peak ≤ 15 (QUIET: 29.4 dB, 0.89
LSB). Against the average of mpglib's two channels it measures the same
within 0.2 dB, except on CLIP: mpglib clips each channel before an average
can be taken, so on a stream past full scale that average is a different
signal (35.4 dB).

The "leak" column compares the float model with an ideal lowpass-and-
decimate of mpglib's output. It mixes §7's transition-band leak with the
energy the zeroed subbands leave out below the cut, so it is reported, not
held.

**Malformed fixtures** end as documented, with no exception:

* Refused by name: BFREE (FREE FORMAT) and BLAYER2 (LAYER II).
* Recovered with counts: BTRUNC (truncated 1), BBADSIDE (badside 3),
  BHUGE23 (badside 1) and BBIGVAL (badside 1).
* Decoded to the clean stream's PCM exactly: BGARBAGE (resync 2, 377 bytes
  skipped), BID3BIG (title and artist from TIT2/TPE1) and BVBRI (VBRI frame
  count 43).

**The float model** reaches 102 to 113 dB against mpglib on every valid
fixture louder than −40 dBFS, with peak error 1 LSB; QUIET is 81.9 dB.

**ISO vectors.** On the conformance vectors in minimp3's repository (used
locally, never committed):

| | float SNR | integer SNR | integer RMS / peak |
|---|---|---|---|
| `compl` | 103.2 | 60.5 | 2.17 / 10 |
| `he_32khz`, `he_44khz`, `he_48khz` | 96.9–97.0 | 66.5 | 1.39 / 4 |
| `hecommon` | 105.3 | 70.9 | 1.51 / 5 |
| `si` | 97.8 | 60.3 | 0.89 / 4 |
| `si_block` | 96.0 | 51.4 | 0.90 / 3 |
| `test45` (MPEG-2 intensity) | 97.2 | 64.1 | 2.00 / 22 |
| `test46` (MPEG-2 intensity) | 99.7 | 64.5 | 2.30 / 20 |

`si` and `si_block` are quiet, hence their low SNR at sub-1-LSB RMS.
`si_huff` is the one exception, §11.

## 6. Where mpglib is not the reference

* **MPEG-2 intensity stereo.** mpglib treats is_pos 7 as illegal in MPEG-2,
  and on an all-zero right channel it leaves part of the intensity bands
  unfilled. ISO and minimp3 make 2^slen−1 illegal and give the top band the
  default position when the band below is not empty. On the ISO vectors
  `l3-test45/46`, mpglib scores 16.7 and 14.2 dB and x3sim's float model
  97.2 and 99.7 dB. IS22 is therefore compared against the float model
  (`expect: decode iso`). mpglib scores 7.7 dB on it.
* **Reservoir underflow.** mpglib decodes silence for such a frame and so
  does x3sim (the standard's answer). minimp3 drops the frame's samples.
* **Mixed-flag start and stop blocks.** Subbands 0 and 1 take the long
  normal window for every block type (ISO dist10). The rule matters on
  `l3-si_block`: 26 dB without it, 96 with it.

## 7. Reduced rate: alias leakage (`x3sim.py --leak`, SWEEP)

| divisor | kept tones 0.5–0.98 N′ | image of a tone at 1.02–1.05 N′ | 1.05–1.10 N′ | 1.10–1.20 N′ | beyond |
|---|---|---|---|---|---|
| 2 | −0.04 … +0.11 dB | −28.8 dBc | −45.1 dBc | −92.2 dBc | below measurement |
| 4 | −0.02 … +0.06 dB | −19.7 dBc | −38.7 dBc | −88.9 dBc | below measurement |

Every level is relative to the full-rate float decode's level of the sweep
in the same time window. The images come from the encoder's analysis: it
left part of a tone just above the new Nyquist N′ in the top kept subband,
and the zeroed subband above would have cancelled it. The leak is confined
to the 10% above N′, which is the 32-band prototype's own transition band.
Kept tones are flat. The butterflies across the cut are skipped. One option
for S8's listening is to attenuate the top kept subband, trading about 6 dB
of leak for the top 1/N of the band.

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

Counts are per granule, measured on the fixtures. The first four columns are
mono sources; the last two are C128, an L/R stereo source, decoded to stereo
and downmixed to mono:

| stage | 11k mono d1 | 44.1k mono d1 | 44.1k mono d2 | 44.1k mono d4 | stereo d1 | stereo→mono d1 |
|---|---|---|---|---|---|---|
| Huffman symbols | 205 | 131 | 131 | 131 | 253 | 253 |
| requant mul | 244 | 132 | 132 | 132 | 160 | 160 |
| antialias mul32 | 182 | 833 | 403 | 188 | 1,967 | 1,967 |
| block scale add32 + shift | 576 | 576 | 288 | 144 | 1,152 | 1,152 |
| IMDCT mul / mul32 | 1,979 / 576 | 2,189 / 576 | 1,094 / 288 | 547 / 144 | 4,475 / 1,152 | 4,475 / 1,152 |
| IMDCT add / add32 | 1,931 / 1,296 | 3,002 / 1,296 | 1,501 / 648 | 751 / 324 | 6,500 / 2,592 | 6,500 / 2,592 |
| mix add32 | – | – | – | – | – | 1,152 |
| DCT mul32 / add32 | 2,034 / 3,348 | 2,034 / 3,348 | 810 / 1,296 | 306 / 468 | 4,068 / 6,696 | 2,034 / 3,348 |
| window mul (= add) | 8,748 | 8,748 | 4,284 | 2,088 | 17,496 | 8,748 |
| **≈ 8088 cycles** | **3.44 M** | **3.69 M** | **1.77 M** | **0.86 M** | **7.48 M** | **4.93 M** |

The 11k column is X11K24 at divisor 1; at divisor 2 it is 1.67 M. A
stereo→mono decode at divisor 4 is 1.19 M. M/S adds 1,152 add32 a granule
(about 32 k cycles); intensity adds one mul32 per intensity line.

Shares at 44.1k mono, divisor 1:

| stage | share |
|---|---|
| window | 49% |
| DCT | 21% |
| IMDCT | 19% |
| antialias | 8% |
| block scale | 1% |
| requantise | 1% |
| Huffman | 1% |

A real-time granule on a 4.77 MHz 8088 is 62 k cycles at 44.1 kHz and 249 k
at 11.025 kHz. By this model the STANDARD decoder is about 59× and 14× too
slow there. Halving to 5.5 kHz output still leaves it about 6.7× too slow,
and X3_FLOOR forbids that rung anyway. This is why the XT path is the XT
backend or Decode first. These figures are a model; S6 replaces them with
MartyPC cycle counts.

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

**Decoder state** comes to about 4.8 KB per channel, plus 576 words of
scratch:

| state | bytes a channel |
|---|---|
| overlap (32 × 9 × int32) | 1,152 |
| V ring (16 × 32 × int16) | 1,024 |
| requantised lines (576 × int32) | 2,304 |
| reservoir (shared) | 4,096 |

## 11. Open items

* **Overload past ±2.0 in V.** A stream whose DCT outputs exceed ±2.0, more
  than 6 dB past full scale, saturates in V instead of clipping only at the
  PCM. Its neighbourhood of 16 slots is disturbed. Only `l3-si_huff` does
  this: a synthetic Huffman-table test with spectral lines up to 4.9. CLIP,
  a real overdriven master, peaks at 1.42 there.
* **Mode changes mid-stream** (`l3-he_mode`) are decoded and adapted; the
  transitions differ from minimp3's per-frame output, as both decoders'
  stale histories differ.
* **Mixed blocks at MPEG-2.5 8 kHz** are refused as a bad granule: ISO does
  not define them and LAME never writes mixed blocks. The mixed-block code
  path is exercised only by the ISO vector `l3-si_block`.
* **CRC is parsed and not checked** (SPEC.md 121.2).
* **The 8088 cycle figures are a model**, to be replaced by S6's
  measurements.
