# XTMP3: the XT approximation study (story S3)

This study answers what the XT backend (SPEC.md 121.3, 121.4) should compute,
how fast that will be on a 4.77 MHz 8088, and what it sounds like. It is
host-side work. Every quality figure is a host measurement. Every speed
figure is an 8088 cycle MODEL (tools/x3cyc.py), checked against the 5150's
measured instruction costs and against MartyPC on the two largest kernels.
Phase 2 (story S7) replaces the speed figures with MartyPC measurements of
the real `x3xt.asm`. The brief's illustrative speeds ("7-9 seconds per
second") are not used anywhere here.

The numbers live in a generated file,
[XT-STUDY-TABLES.md](XT-STUDY-TABLES.md) (`T1` to `T12` below are its
sections), with the raw results in `xtstudy-results.json`. The listening
material is in [listen/](listen/README.md). This document is the account:
what was tried, what was measured, what was decided and why.

| file | what |
|---|---|
| `tools/x3xtstudy.py` | the harness: corpus, experiments, profiles, reports, WAVs (`--all`, `--selfcheck`, `--check-design`, `--write-design`, `--render`) |
| `tools/x3xtdsp.py` | the ISO 11172-3 analysis filterbank (the encoder's), the float synthesis, the encoder's hybrid (MDCT with block switching and alias reduction) and the decoder's, an MP3-like quantiser, the signals |
| `tools/x3xtmodel.py` | the XT arithmetic: code maps, product tables, byte-split tables, Lee's DCT, and every stage in float, `int` (16-bit `imul`) and table forms |
| `tools/x3xtkern.py` | every candidate kernel written as the NASM the target would run |
| `tools/x3cyc.py` | the 8088 cycle model (BIU/EU simulation; `--calibrate`) |
| `tools/x3cycmarty.py` | the one MartyPC check of the model (a design measurement, not a suite row) |
| `tools/x3xttab.py` | the deterministic generator of the XT table packs (standard library only; `--selfcheck` is the fast row `x3xttab`) |

## 1. The answer in brief

- **Pin the window's product tables. They are the XT backend's one large
  win.** A tap through an 8-bit companded code and a product row costs 41-50
  clocks against 194 for `imul` with a 32-bit sum (T3). Pairing output phases
  j and 32-j lets one code fetch serve two products, which brings a 16-tap
  window to 668 clocks an output sample.
- **Matrixing is the second cost and does not shrink with the window.**
  Lee's 32-point DCT is 80 multiplies and 209 additions a slot. On an 8088
  that is 20.9k clocks a slot even with byte-split product tables, 25-34% of
  the XT Standard decode (the song; the synthetic 16 kbps set). Removing window terms leaves it untouched. So 4
  terms instead of 16 makes the whole decode only 1.3x faster (5.84 to 4.49
  wall seconds an audio second on a real song), not 4x. This is the brief's
  section 7 warning made concrete.
- **On real LAME streams at 11,025 Hz and 24 kbps, the model puts XT
  Standard at 0.17x real time (5.8 s an audio second) and XT Speech at
  0.31x.** The STANDARD decoder's `imul` arithmetic on the same 8088 is 0.075x
  (13.3 s an audio second), so XT Standard is 2.3x faster. No XT rung is real
  time on an XT, so the XT always decodes first (SPEC.md 121.7). A 3-minute
  song converts in about 18 minutes with XT Standard, 10 with XT Speech and 40
  with the proper decoder (model).
- **Quality on real streams:** XT Standard is 31.1-32.4 dB against the float
  decode. XT Fast is 28.4-29.8, XT Turbo 18.7-19.4 and XT Speech 31.8-32.8.
  For scale, the float decode rounded to 8 bits is 35.1-38.9 dB, and the
  MP3-like quantiser's own noise on the synthetic set is 3.5-20 dB (T1).
  Turbo's error reaches -2.4 to -10.3 dB of the signal in its worst octave,
  and its tones carry alias spurs at -19 dB. **XT Turbo stays experimental and out of Auto.**

## 2. Method

### 2.1 Material: training, held out, and real

- **Synthesised corpus** (`x3xtstudy.corpus`), 11,025 Hz, 5 s an item.
  - TRAINING items (`t_*`) choose every map, palette and tap set: a music
    phrase, a formant voice, pink noise, a transient loop and a quiet passage.
  - HELD-OUT items are what every figure reports: a different music phrase
    (`music`), voice (`speech`) and loop (`transient`), plus bass, a 4.5 kHz
    tone, white noise, clipping stress, a -30 dB passage and silence.
  - These are encoded by the study's own encoder: the ISO analysis
    filterbank, the hybrid MDCT with transient-driven long, start, short and
    stop blocks, the alias reduction, and an MP3-LIKE quantiser
    (`|x/step|^0.75`, a global step a granule fitted to a modelled Huffman
    budget). That quantiser is a model of an encoder, not LAME, and it
    under-states density (2.1).
- **Real streams.** The numerics track's committed fixtures (tests/xtmp3/,
  made with LAME from synthesised signals) are parsed and Huffman-decoded by
  `tools/x3sim.py`, imported read-only (`--x3sim DIR`) through its
  `granules()` hook (xtmp3/numerics a6b748b2): requantised lines (int32
  Q28, subband order), block types, Huffman values, each codeword's real
  length, `big_values` and the count1 region. Those feed this study's
  chains and its density and Huffman statistics. Used: `AFTERHRS.MP3`
  (an 11,025 Hz, 24 kbps song: the XT target), `X8K16`, `X11K24`, `X12K32` and
  `M16` (T12).
- **The real streams corrected the model.** LAME at 24 kbps and 11,025 Hz
  leaves 204 nonzero lines and 27.5 active subbands a granule. My quantiser's
  24 kbps left 159 and 22. Every speed figure quoted in section 1 and in the
  profiles comes from the REAL fixtures' own statistics (T12). The synthetic
  rates (T2) are kept as a second view.
- **The float reference is cross-checked.** This study's float decode of each
  fixture matches x3sim.py's own float decode at 82.3-86.3 dB (T12). Those are
  two independent ISO decoders; x3sim rounds to 16 bits, which is the floor
  there. The window table is the ISO's Table 3-B.3 as integers
  (`x3xttab.ISO_WINBASE`, D = value / 65536, exact), read out of the host's
  libmpg123 to be sure of every digit. Analysis then synthesis reconstructs
  white noise at 84 dB.

### 2.2 What is measured

Every approximation is first measured ALONE: its stage in its XT form, every
other stage in float. Then the profiles combine them. Each result is against
the float decode of the SAME quantised data, so the codec's own noise is not
in it. Metrics:

- SNR and segmental SNR;
- the worst and mean OCTAVE-BAND error-to-signal ratio (125 Hz up): where an
  error would be heard first, which SNR hides;
- peak error and DC offset, both in 8-bit LSBs;
- gain (least-squares fit);
- clip counts at 8 bits (the profile's and the reference's), 16-bit wraps and
  saturations;
- for the term choice, the full set: a 40-tone sweep's largest spurious
  component (alias), a click's energy before and after its peak (pre-echo and
  ringing), and a constant input's DC gain and 32-sample ripple (the "metallic"
  tone at k x 344.5 Hz).

### 2.3 The cycle model

`tools/x3cyc.py` is a clock-stepped simulation of the 8088's two units:

- The EU runs each instruction for its 8086 book time. The book's 4 clocks a
  bus cycle are taken out, and the 8088's transfers are put back a byte at a
  time.
- The BIU owns the 8-bit bus, with 4-clock cycles, a 4-byte prefetch queue
  filled whenever the bus is idle, and EU transfers taking priority over the
  next prefetch.
- A taken branch flushes the queue.
- DRAM refresh steals 6 clocks every 72. That is the one fitted constant,
  set so that a run of `nop`s costs PERFORMANCE.md's measured 4.34 clocks a
  byte.

PERFORMANCE.md's `max(clocks, 4.34 x bytes)` floor then falls out of the
simulation rather than being imposed. Kernels are written as NASM
(`x3xtkern.py`), never as an instruction count. Each includes the code
extraction, index scaling, segment overrides, reads, accumulation and the
output's normalisation and saturation. `cost()` is the steady-state clock
count of one unit.

**Calibration.** The model was checked twice:

- Against PERFORMANCE.md Part 2's 16 instruction shapes measured on the 5150
  (`python3 tools/x3cyc.py --calibrate`): mean error 3.1%, worst 12% (a taken
  short `jmp`, 20.4 against 18.2).
- Once, as the design-deciding measurement the brief allows, against
  MartyPC's cycle-accurate 5150 (`tools/x3cycmarty.py`). The tool boots
  GLaBIOS, parks the CPU on each kernel repeated R times over zeroed tables,
  and takes the cycle slope between two R values:

| kernel | MartyPC | model | error |
|---|---|---|---|
| window, 16 taps, outputs j and 32-j paired (two outputs) | 1,277.5 | 1,336.0 | +4.6% |
| window, 8 taps paired, [si+d8] (two outputs) | 760.0 | 798.3 | +5.0% |
| Lee DCT-32, byte-split multiplies (one slot) | 20,190 | 20,857 | +3.3% |
| byte-split multiply with its load and store | 146.0 | 150.7 | +3.2% |

The model is slightly PESSIMISTIC (3-5%) on exactly the kernels that hold
52-70% of XT Standard's time (the song; the synthetic 16 kbps set). Two things it does not model:

- **Data-dependent timing.** `imul` takes a fixed 141 clocks, the middle of
  the book's range.
- **Instruction scheduling a hand-coder would do.** Section 6 lists where
  that could pay.

`End to end` here means the decoder alone: parse, Huffman, requantise,
antialias, IMDCT, synthesis and the 8-bit output. The parser's side-info,
scalefactor and reservoir work is an estimate of 12k clocks a granule. Disk,
the UI and the feed are not included (section 7).

## 3. Formats (the XT model's Q table)

The STANDARD decoder's formats are x3sim.py's. These are the XT backend's,
pinned by measured ranges (T1's stress items, and the Lee node peaks below).

| quantity | held as | range | why |
|---|---|---|---|
| requantised line (IMDCT input) | int16 Q13 (FS = 8,192) | +-4.0 FS | lines peak at 1.25 FS on a full-scale sweep |
| its code (XT Fast/Turbo, and the IMDCT rows) | 8-bit code over `R_MAP` (Lloyd-Max, 127 levels a sign, Q13) | | |
| subband sample (synthesis input) | int16 Q11 | +-16 FS | Lee's DCT nodes reach 11 FS on a full-scale sweep (Q12 overflowed) |
| V (Lee's output) | int16 Q11 | | |
| V code | 8-bit code over `V_MAP` (`optseg8`) | 127 levels a sign, top 2.585 FS | |
| window output | int16 Q13 | +-4.0 FS | every product entry fits a word (the largest is 24,245); partial sums wrap as the target's `add` does; the final sum is in range |
| PCM out | `sat((y + 32) >> 6) + 128`, unsigned 8-bit | | two `shl`/`jo` pairs catch \|y\| >= 1.0 FS |

## 4. Findings, stage by stage

### 4.1 The window's V code (T5)

| map (8-bit unless named) | window-stage SNR | quiet passage | finding |
|---|---|---|---|
| uniform 8-bit | 26.8 | 1.0 | quiet material destroyed |
| uniform 10-bit | 38.3 | 10.2 | 472 KB of rows: impossible |
| mu-law 8 | 37.4 | 26.5 | |
| G.711 segments 8 | 36.6 | 27.0 | |
| **optseg8** (power-of-two segments, searched) | **36.8** | 26.8 | expands by additions alone |
| Lloyd-Max 8 | 37.4 | 26.5 | +0.2 dB for a multiply an entry at OPEN |
| optseg 7-bit | 32.6 | 19.7 | half the rows' memory (60 KB) |
| optseg 10-bit | 48.4 | 27.2 | 472 KB |

- **Companding is worth 10 dB overall and 25 dB on quiet material.** It is
  global and fixed, with no block scaling at all, so there is no history to
  re-align and no pumping.
- **Lloyd-Max buys 0.2 dB over the power-of-two segment shape.** The segment
  shape expands with additions only (x3xttab's recurrence, 0.52 s for the
  window at OPEN). A Lloyd-Max map needs a `mul` an entry, about 2 s more. So
  **optseg8**.
- **The encoder table's index shift costs real SNR.** The table is ENC[v >> s].
  - s = 0 over the whole int16 range is 64 KB.
  - s = 2 costs 0.7 dB, and 11 dB on quiet material.
  - The answer is **s = 0 over |V| < 2.0 FS (8 KB) with an unsigned range
    test**: 3 instructions, and a slow path for the rare larger V.
- **A shifted ENC must round, not floor.** Indexing by v >> s without the
  rounding `add` maps silence to a nonzero code. The DC offset that follows
  showed up as silence decoding to -inf dB.

### 4.2 Product rows and their layout (T3, T6)

- **What a tap costs.** One tap is the code word read and an `add` (or `sub`)
  from the product row. The coefficient's sign is the instruction, never
  data. Model costs: 41.8 clocks with the pair kernel's shared fetch, against
  194 for `imul` with a 32-bit sum.
- **Rows.** There is one row a distinct |D| (236 for the full window), 512
  bytes each (8-bit codes, word entries). The rows are SHARED between mirror
  phases: |D[32i + j]| = |D[32(15-i) + 32-j]|.
- **Pairing phases j and 32-j in one pass** reads every unique V value once
  for two products and needs one ring for the pair. It takes the 16-tap
  window from 460k to 385k clocks a granule and the V coding from 130k to
  83k. XT Standard goes from 0.214x to 0.239x at 16 kbps (T2, "unpaired").
- **Layouts compared** (one output, 16 taps):

| layout | clocks |
|---|---|
| a per-phase ring of code words read with `lodsw` | 799 |
| the same ring with `[si+d8]` | 863 |
| the standard 16-slot V ring with `[si+d16]` | 926 |
| a LOOPED tap (row pointer stepped, `loop`) | 71 a tap, against about 49 unrolled: 46% slower |

  A segment override on every tap (rows and ring in different segments) costs
  48 against about 44 a tap. So the ring is kept beside the rows, one ring for each
  pair in that pair's own segment.
- **Segments.** The 236 rows plus their rings fit two 64 KiB segments (120
  and 121 rows): 5 rows are duplicated, and one `mov ds` a segment a slot
  costs 22 clocks.
- **Palettes.** Clustering |D| into K error-aware representatives
  (importance = the tap input's training energy, assigned in the log domain)
  costs 1.5 dB at 192 rows, 2.8 dB at 128 and 7.3 dB at 64. The exact rows fit
  the budget, so **no palette**. 128 representatives is the fallback if a
  smaller pack is ever needed.

### 4.3 Matrixing (T7, T3)

- **Lee's DCT, not the 2cos(phi) form.** The 2cos(phi) form's odd outputs
  come out of an alternating running sum. At Q11 it lost about 1 dB on music
  and speech, and it overflowed on the clipping stress and the full-scale
  sweep, where Lee measured 54 and 59 dB. Lee's multipliers are 1/(2cos(phi)),
  up to 10.19, and its nodes stay small on real signals (music peaks at 0.77
  FS, a sweep at 11).

| form | matrixing alone | cost a slot |
|---|---|---|
| `imul` Q11 rounded | 41.6 dB | 24.3k (Q15 + shifts: 30.6k) |
| **byte-split product tables** | 40.4 dB | **20.9k** |
| 8-bit coded product tables | 29.2 dB | 20.5k |
| 10-bit coded product tables | 37.3 dB | needs a 64 KB ENC |
| shift-and-add, 2-4 powers of two | 21-26 dB | |

- **Coded tables lose because every one of the 80 multiplies re-codes its
  input**, and the coding errors compound through the recursion.
- **Byte-split tables keep 16-bit precision.**
  `x*c = H_c[x >> 8] + L_c[x & 255]`: two lookups, no code map, error under
  one LSB, 1 KB a constant, 31 constants. **Byte-split it is.**
- **Shift-and-add constants fail on precision (T3's list).** Lee's
  constants need 4-5 signed powers of two for 1e-4 accuracy, which costs 132-176
  clocks against the split's 151 with its load and store. And every `sar`
  floors, which accumulates a bias: more terms measured WORSE (sa5 21 dB, sa3
  26 dB).
- **Pruning the DCT's zero inputs saves almost nothing:** 19.7k against 20.9k
  at 16 live subbands. Lee's multiplies survive a zero partner. The one
  pruning that pays is in the OUTPUTS, which the reduced rates do (4.6).
- **The additions are as costly as the multiplies.** 209 memory-to-memory
  additions are 9-10k of the 20.9k. Section 6 lists register scheduling as
  the next saving.

### 4.4 The hybrid stage (T8, T12)

- **Never skip the antialias.** Skipping it leaves 12.8 dB against the
  float decode, where the `imul` antialias measures 53.9.
- **The antialias, three ways:**

| form | quality alone | clocks a butterfly |
|---|---|---|
| byte-split | 53.9 dB-class (`imul`'s figure; split is within 1.5 dB) | 607 |
| `imul` | 53.9 dB | 946 |
| 8-bit tables (Lloyd-Max) | 38.3 dB | 349 |

  Split is the right trade for XT Standard (102k clocks a granule on the song)
  and tables for XT Fast and Turbo (59k).
- **IMDCT: sparse direct form against a fast factored form.**
  - The sparse direct form through 18 coded product rows costs 1,179 clocks
    a nonzero line. A fast factored form with byte-split multiplies is about
    11.3k a subband (43 multiplies and 115 additions, estimated from the
    9-point factorisation x3sim.py uses).
  - The crossover is about 9.6 lines a subband. At 16 kbps (synthetic) most
    subbands are sparse. LAME's 24 kbps song has 266 lines over 27.5
    subbands, about 9.7 a subband: right on the crossover.
  - So the target should **choose per subband by its line count**. That
    costs 238k on the song, against 314k for either form alone.
  - The table form's quality is 36.1 dB (Lloyd-Max codes) against 45.9 for
    `imul`. Its codes come straight from the requantiser for lines the
    antialias left alone, and from ENCR (16 KB, s = 2, rounding) for the
    rest. 10-bit IMDCT codes would give +1.6 dB end to end for about
    44 KB more (word rows, a word ENCR), not taken (T1).
- **The IMDCT window and overlap, per subband:** byte-split 6.3k clocks
  (44 dB-class) against tables 4.3k (36.5 dB). Split for Standard, tables for
  Fast and Turbo. Both together cost XT Standard 1.2-1.7 dB, measured on the
  fixtures.

### 4.5 Shortened synthesis: 8 and 4 terms (T9)

Taps were chosen per output phase. Mirror phases (j, 32-j) always get
mirrored taps, so they share rows. Methods:

| method | 8 terms SNR | 8 terms alias worst | 4 terms SNR | 4 terms alias worst | finding |
|---|---|---|---|---|---|
| first N taps | 2.0 | +1.8 | 0.0 | +20.2 | the brief's strawman: unusable |
| middle N (truncation) | 32.5 | -33.7 | 17.4 | -17.4 | flat error, DC gain -1.5 dB at 4 |
| largest \|D\| | 35.4 | -38.5 | 19.5 | -18.9 | |
| exhaustive least-error ("opt") | 35.4 | -38.6 | 19.9 | -19.1 | its error goes where the music is quiet |
| **opt, then least mean octave-band error ("optband")** | **34.6** | **-39.4** | **19.6** | **-19.4** | chosen |
| least squares retuned | 35.5 | -36.9 | 19.9 | -17.8 | fails validation: -2.2 dB on the held-out 4.5 kHz tone |
| DC-gain renormalised | 32.0 | -34.9 | 16.0 | -19.2 | gain fixed, error worse |

- **Why "optband".** The MSE-optimal subset wins its SNR by pushing error into
  the top octaves, where training music is weak. On the real song its top
  band reached -9.5 dB of the signal at 4 terms, against -15.3 for plain
  truncation.
  - The selection used is "optband", with a stated objective: the mean of
    every octave band's error-to-signal ratio in dB on the training items,
    equal weight a band. It is found by coordinate descent over each mirror
    group's 12 least-error subsets, with coefficients unchanged.
  - On held-out material and the real fixtures it gives up 0.8 dB of SNR at 8
    terms. In return its mean band error is 1.2-1.3 dB lower, its worst band
    1.2-2.0 dB lower, and its alias rejection 0.8-2 dB better.
- **Retuning the kept coefficients (least squares on training correlations)
  does not survive held-out validation.** It moved SNR by +0.1 dB and cost
  2.2 dB on the held-out tone, so the coefficients stay the window's own.
- **Beyond SNR,** at 8 / 4 terms (optband):

| measure | 8 terms | 4 terms | full filter |
|---|---|---|---|
| click pre-echo | -43.7 dB | -24.6 dB | -97 |
| click ringing | -40.9 dB | -22.8 dB | |
| DC gain | -0.16 dB | -0.76 dB | |
| 32-sample ripple (a 344.5 Hz-family tone on low-frequency content) | -38.7 dB | -23.2 dB | |
| rows | 132 | 67 | 236 |

  - No gain was raised, so no new clipping: max row gain is 0.98 and 0.88 of
    the full filter's.
- **Four terms is not a quality setting.** Every 4-term selection leaves one
  of the held-out items with an octave whose error is within 2-4 dB of the
  signal.

### 4.6 Reduced bandwidth (T10)

- **What runs.** Subbands at and above 32/div are neither requantised,
  antialiased nor IMDCT'd, but they are still Huffman-PARSED: the bitstream
  requires it. Only the kept output phases are computed: the even ones at
  div 2, every fourth at div 4.
- **Only the matching DCT outputs are formed.** Even V positions need only
  the even DCT outputs, which are a plain 16-point DCT of the live subbands;
  a 4:1 rate needs an 8-point one. Matrixing falls from 20.9k to 8.2k (div 2)
  and 3.0k (div 4) a slot.
- **The band edge.** A PQMF alias at the new Nyquist folds back ONTO the tone
  itself, so the band edge shows as a gain error rather than a spur:

| divisor | tone gain error at the band edge |
|---|---|
| 2 | under 0.01 dB to 98% of the new Nyquist |
| 4 | -0.28 dB at 98% |

- **Against an ideal output** (the full-rate float decode brick-walled at 0.9
  of the new Nyquist), music and speech come out at 29-35 dB at div 2. The
  transient loop is 18 dB: its broadband energy straddles the cutoff, which is
  a filter-shape difference, not noise.
- **XT Speech's error against the float reduced-rate decode** is 32.6 dB
  (synthetic) and 31.8-32.8 dB (real).
- **The definition.** The brief's "about 2.76 kHz bandwidth" is the 2:1 path
  on 11,025 Hz (output 5,512.5 Hz). 4:1 there gives 2,756 Hz and 1.38 kHz,
  below the Sound Blaster's 3,906 Hz floor. Hence XT Speech's definition in
  section 5.

### 4.7 Requantisation and Huffman (T4, T12)

- **Small values dominate.** |ix| <= 15 is 94.2-99.3% of nonzero lines on the
  real fixtures. One table row a scale exponent covers them:
  - **`RQL`** gives a linear Q13 word (XT Standard, Speech): 2,816 bytes.
  - **`RQ`** gives a Lloyd-Max code byte (XT Fast, Turbo): 1,408 bytes.
  - Both are indexed by x3sim.py's own exponent q, where
    xr = ix^(4/3) 2^(q/4) FS. The rows cover q = -80..7, and both are made by
    exact integer arithmetic.
  - Real streams have q from -130 to -13. Below -80 every small value rounds
    to 0 at Q13 (so the target reads q < -80 as zero), and above 7 it
    saturates.
  - |ix| > 15 (escapes, 0.7-5.8% of lines) takes the 179-clock general path.
- **Huffman, with the REAL Layer III tables** (x3sim's `huff_decode` counting
  the bits of each codeword):
  - Pair codewords average 3.6-4.2 bits.
  - 24-37% are longer than 4 bits, 11-26% longer than 6, 4-12% longer than 8.
  - Count1 quads: 11-26% are longer than 4 bits.
- **The first-level width barely matters.** A first level of 4, 6 or 8 bits
  changes the Huffman stage by under 15%. The whole stage is about 5% of
  the decode (76k of 1,456k clocks a granule on the song), and an 8-bit
  first level saves about 5% of that against 6 bits. So the choice is
  memory's: **6 bits**, agreeing with the numerics track's
  `X3_HUF_FIRST = 6` (about 8 KB against 12 KB).
- **The Huffman kernel's cost** includes the bit extraction (a shift of the
  window by CL, 8 + 4n clocks), the refill (70 clocks a byte, amortised) and
  the sign bits (62 clocks each).

### 4.8 Other multiplications (T3)

**Quarter-square table:** `x*y = Q(|x+y|) - Q(|x-y|)`, 65,537 four-byte
entries. It costs 337 clocks a product: the 17-bit sum, two segment selects
over four 64 KB segments, and two 32-bit reads. `imul` costs 178 with its
load. It is slower, and it takes 262,148 bytes. **Dropped.**

## 5. The recommended profiles

Every profile shares the parser and Huffman decoder (the primary's), the 8-bit
output, Q formats, Lee's DCT with byte-split tables, the V code optseg8
through ENCV at s = 0 with a clamp, and pairing of output phases j and 32-j
with one ring a pair.

| | XT Standard | XT Fast | XT Turbo (experimental) | XT Speech |
|---|---|---|---|---|
| window terms an output | 16 (the full filter) | 8, optband | 4, optband | 16 |
| output phases | all 32 | all 32 | all 32 | every 2nd (8-12 kHz source) or 4th (16-24 kHz) |
| window kernel | `lodsw` ring, paired | `[si+d8]` ring, paired | `[si+d8]` ring, paired | `lodsw` ring, paired |
| window rows | 236 (241 laid out) | 132 | 67 | even phases' 123 |
| requantise (\|ix\| <= 15) | RQL, linear | RQ, code | RQ, code | RQL, linear |
| antialias | byte-split | product tables | product tables | byte-split (live subbands) |
| IMDCT | sparse table / fast split, by line count | same | same | same, live subbands only |
| IMDCT window | byte-split | product tables (Z map) | product tables | byte-split |
| matrixing | Lee DCT-32 | DCT-32 | DCT-32 | DCT-16 (div 2) / DCT-8 (div 4) |
| pack (x3xttab) | `std`: 228,672 B, 4 segments | `fast`: 153,536 B, 4 segments | `turbo`: 120,256 B, 3 segments | `speech`: 151,360 B, 3 segments |
| OPEN expansion, model | 2.11 s | 1.62 s | 1.37 s | 1.49 s |
| SNR vs float, real (synthetic) | 31.1-32.4 (32.3) | 28.4-29.8 (29.1) | 18.7-19.4 (19.2) | 31.8-32.8 (32.6) |
| worst octave, real | -15.4 to -22.9 dB | -13.0 to -21.2 | -2.4 to -10.3 | -15.0 to -23.8 |
| alias, the term choice alone | -93.9 dB | -39.4 | -19.4 | -93.9 |
| speed, 11,025 Hz 24 kbps song (model) | 0.171x (5.84 s/s) | 0.207x (4.83) | 0.223x (4.49) | 0.311x (3.21) |
| speed, 8,000 Hz 16 kbps voice (model) | 0.242x | 0.290x | 0.313x | 0.447x |
| speed, 12,000 Hz 32 kbps (model) | 0.147x | 0.177x | 0.189x | 0.277x |
| in Auto | yes, rank 4 (SPEC 121.5) | yes, rank 5 | **no** | **no**: explicit, below X3_FLOOR |

Notes on the table:

- **"SNR vs float, real"** is over the five fixtures; in brackets, the
  synthetic held-out core.
- **"Speed"** is audio seconds a wall second, decode only. The STANDARD
  decoder's arithmetic on the same 8088 is 0.075x, 0.104x and 0.067x on those
  three rows.
- **XT Speech's divisor:** 2 for an 8,000-12,000 Hz source (output 4-6 kHz,
  bandwidth 2-3 kHz) and 4 for a 16,000-24,000 Hz source. For a 32-48 kHz
  source a 5.5 kHz output would need 8:1, which is not offered; those sources
  use STANDARD's quarter rate. SPEC.md 121.4 calls it "the 4:1 pruned rate",
  and that wording should become "the divisor whose output lands at 4-6 kHz".
- **The pack sizes are the tables only** (x3xttab's manifest, T11). Each pack
  holds its own window, synthesis and hybrid segments. 5,578 bytes of
  generator input expand into all of them.

**What Auto does with these on an XT.** On the 8088, no XT rung reaches the
1.25x safety margin; even XT Speech 4:1 is 0.59x on the song. So on an XT Auto
always reaches Decode First, as SPEC.md 121.5 and 121.7 expect. The
recommendation for that conversion is:

- **Default to XT Standard.** It is 2.3x faster than the proper decoder on
  the same machine, at 31-32 dB against the proper decode.
- **Offer Original (the STANDARD decoder) as an explicit choice** for a user
  who prefers quality over conversion time. The brief requires that full
  quality stay available offline.
- **Offer XT Fast as the explicit faster choice.**

Where the XT rungs matter LIVE is a faster machine, a 286 for instance. There
Auto tries XT Standard, then XT Fast, after Quarter. That is phase 2's
measurement on 86Box.

**Kept out of Auto:**

- **XT Turbo.** 19 dB against the float decode. A worst octave within
  2.4-10.3 dB of the signal on every fixture. Alias spurs at -19 dB on tones, and a
  -23 dB ripple tone family on low frequencies. All of that buys 8% over XT
  Fast end to end, because matrixing does not shrink. It is offered only
  under its own name, as experimental, until the owner's listening says
  otherwise.
- **XT Speech.** Muffled by design, and below `X3_FLOOR` on an 8-12 kHz
  source: an explicit choice only, labelled as muffled.
- **The 7-bit window codes.** 30.3 dB, -2 dB, 60 KB smaller. This is a
  low-memory fallback of XT Standard, not a rung; not built as a pack until
  a machine needs it.
- **Uniform codes, first-N or plain-truncation tap sets, and least-squares
  retuning.** Rejected. The first two are in listen/ as negative examples.

## 6. What phase 2 can still win (estimates, not modelled)

- **Matrixing register scheduling.** Hold a radix-4 group's four values in
  AX, CX, SI and BP across two Lee levels instead of storing every node. That
  cuts the memory traffic that is 9-10k of the 20.9k a slot; perhaps -25% of
  matrixing, about -90k clocks a granule.
- **Cheaper multiplies for near-power-of-two constants.** Lee's constants
  within 1% of a power of two (0.5006, 0.5024, 0.5055, 0.9726, 1.0607) can
  take a shift plus a single high-byte correction lookup instead of the
  two-lookup split (T3's shift-add list shows which).
- **The IMDCT's per-subband switch.** The 9.6-line threshold should be
  measured, not modelled.
- **The fl32 check during the expansion's own stores.** Folding it into the
  stores saves a second read of 228 KB (0.77 s of the 2.11 s at OPEN).

## 7. End to end, and the memory partition of 400,000 bytes

**A conversion is the decode plus disk.** For a 3-minute 11,025 Hz, 24 kbps
mono song, the decode alone (model, the song's statistics) is:

| profile | decode time |
|---|---|
| XT Standard | 17.5 min |
| XT Fast | 14.5 min |
| XT Speech | 9.6 min |
| STANDARD on the 8088 | 39.8 min |

Disk is not modelled. The WAV written is 1.98 MB and the MP3 read 0.54 MB. At
an assumed 50 KB/s of effective hard-disk writing on an XT (to be measured)
the writing is about 40 s, +4%. OPEN's expansion adds 1.4-2.1 s. The UI's
throttled progress redraw (two a second, SPEC 121.7) is not included.

**The XT Standard working set during a conversion** (SOUND.DRV is not needed
until the finished file plays):

| item | bytes | source |
|---|---|---|
| primary package image + bss | 57,344 | SPEC 121.10's planning figure (not yet measured) |
| XT part (code) | 20,480 | estimate from the kernels' sizes: 17 window pair passes at 206 B, Lee 3.5 KB, sparse IMDCT line blocks 2.8 KB + short 1 KB, fast IMDCT 4 KB, IMDCT window 1.1 KB, antialias 0.9 KB, expansion and glue 3 KB. SPEC's 12 KB is too small for unrolled kernels |
| generator input (XTGEN) | 5,578 | x3xttab; freeable after OPEN |
| window segments 1 and 2 | 124,480 | 241 rows + 17 pair rings |
| synthesis segment (LEE, ENCV) | 39,936 | |
| hybrid segment (AASPL, IMDCT, ENCR, IMWIN, RQL) | 64,256 | |
| Huffman tables, 6-bit first level | 8,192 | the parser's, estimate |
| decoder state (overlap 1,152, IMDCT and Lee scratch 1,216, a granule's lines, codes and ix 3,456, side info 256) | 6,144 | |
| reservoir | 4,096 | SPEC 121.2 |
| compressed look-ahead | 16,384 | SPEC 121.10 |
| PCM staging | 32,768 | SPEC 121.10 |
| claim rounding and headers (about 12 claims) | 1,024 | |
| **total** | **380,682** | 19,318 bytes under 400,000 |

Every table segment is within 64 KiB and each hot row lies inside one
segment (x3xttab's layout, checked by `--selfcheck`). With XT Fast's pack the
total is 305,546; with XT Turbo's 272,266; with XT Speech's 303,370. A
machine with less than about 381 KB free takes XT Fast. Below about 306 KB
the XT backend states its minimum and refuses.

## 8. Table packs: the generator and the recurrences (tools/x3xttab.py)

- **What ships and what OPEN does.** What ships is XTGEN.BIN, the generator
  input: 5,578 bytes holding the code maps, the tap sets, the integer
  constants, and the RQ and RQL rows. OPEN expands the packs a profile needs
  by EXACT integer recurrences, chosen so an entry costs additions where the
  code map allows. The full specification is the docstring of
  `tools/x3xttab.py`; in outline:
- **A window product row** for |ib| (D = ib/65536) over the V map's segments:
  - `A = 2^15`, then for each segment s, `B = 4|ib| << (s0 + s)`, and for
    each level `A += B; T = A >> 16`.
  - That is round-half-up of |ib|·lv/2^14, exactly: the entry is the running
    sum's high word.
  - On the 8088 that is `add bx,bp / adc ax,si / stosw`, 29 clocks an entry,
    and the negative half is a negation pass.
  - The formula is T[k] = round(2^13·|D|·decode_V(k)/2^11), Q11 in and Q13
    out, and every entry fits a word.
- **A byte-split constant** c16 = round(c·2^16):
  - `L[l] = (2^15 + l·c16) >> 16` by a running sum.
  - `H[h] = (2^7 + h·c16) >> 8`, stored MOD 2^16. x = 256h + l with l >= 0,
    so an in-range product can need an H beyond a word, and the word sum is
    exact mod 2^16 wherever |x·c| < 2^15, which is the node's proven range.
  - Four 256-byte planes, indexed by the byte itself.
- **The encoders** (ENCV 8 KB, ENCR 16 KB) are one walk over v with the code
  advancing when 2v passes the next midpoint sum: nearest level, ties toward
  zero.
- **The 18 IMDCT rows** (and XT Fast's antialias and IMDCT-window rows) are
  over a Lloyd-Max map and take one `mul` an entry.
- **Checks.** Each table's and segment's CRC-32 is recorded as the identity,
  for x3sim.py, the cache key and a test build. OPEN checks `fl32`, a
  Fletcher sum over words (s1 += w, s2 += s1, mod 2^16).
  - A plain word sum would be worthless: every odd-symmetric row sums to
    zero, so a mirrored recurrence error is invisible to it. The selfcheck's
    negative control proves that.
  - A table-driven CRC-32 at OPEN would add 4.7 s on the 5150 (model).
- **`--selfcheck` (0.2 s, the fast row `x3xttab`)** rebuilds every entry by
  the recurrence and compares it with its direct formula. That covers:
  - every window row of every profile;
  - both encoders (sampled);
  - every split table, including that the pair reconstructs x·c within one
    LSB over its range;
  - the IMDCT rows;
  - RQ and RQL (exact integer 12th roots; no float on the path to a byte);
  - odd symmetry, word range, monotonicity, the mirrored tap sets and the
    64 KiB layout;
  - determinism;
  - that the integer constants are what `--derive`'s formulas round to.

  Four negative controls prove the checks can fail: an unbiased row, c16 + 1,
  a ties-up walk, and a mirrored error.
- **`x3xtstudy.py --check-design`** (soak row `x3xtdesign`) proves the
  generator's bytes are, entry for entry, the tables every quality figure
  here was measured with.

## 9. How the XT model plugs into tools/x3sim.py

- **The interface.** The XT backend's input per granule and channel is what
  x3sim.py's parser and Huffman decoder already produce: the 576 `ix` (in
  bitstream order), the side info `gi` (block type, mixed flag) and the
  per-line exponent `q` of `quarter_exponents()`, with
  xr = sign·|ix|^(4/3)·2^(q/4) FS. This study reads them through
  `x3sim.granules()` (`FixtureItem`).
- **Where the formats meet the STANDARD model's** (x3sim.py's docstring and
  docs/xtmp3/NUMERICS.md bind those):
  - STANDARD's requantised lines (`xr`) and subband samples are int32 Q28.
    The XT's lines are int16 Q13, so Q28 >> 15 with rounding. The XT's
    subband samples are int16 Q11, so Q28 >> 17. That cut is the natural
    16-bit seam if the XT ever sits behind STANDARD's hybrid stage.
  - STANDARD's synthesis keeps the last 16 DCT-II vectors as int16 Q14 and
    windows them with D in Q14. The XT keeps the same vectors as 8-bit V
    CODES at Q11 (and only the 32 unique values a slot), and reads products
    instead of multiplying.
  - The noise gains x3sim's author measured line up with the budget here: a
    rounding step in the X/V history reaches the PCM about 1.4x larger,
    which is why the V code dominates the XT's error. A step in the subband
    domain reaches it about 5.6x larger, which is why the subband samples
    stay 16-bit and the matrixing stays at byte-split precision.
- **Requantise.** |ix| <= 15 reads RQL[q][|ix|] (linear Q13) or RQ[q][|ix|]
  (a code), with q clamped as in 4.7. |ix| > 15 uses x3sim's integer pow43,
  scaled to Q13 (its requant_i). Short blocks are reordered as x3sim does,
  then laid out window by window (`[6w + k]` here; x3sim interleaves
  `[3k + w]`).
- **Stereo.** A stereo source is stereo-processed by x3sim's integer stage,
  then down-mixed: before the hybrid when both channels' block types agree
  (one hybrid pass), else after it. The XT outputs mono.
- **The hybrid and synthesis** are x3xtmodel.py's integer chain for the
  profile:
  - antialias (`stage_antialias`), IMDCT and window (`stage_imdct`,
    `_win_apply`), overlap and frequency inversion (`stage_overlap`);
  - Lee's DCT (`dct_split`), V to codes (`CodeMap.enc_table(0, 11)` with the
    clamp), the window (`window_tab`);
  - PCM8 = `sat((y + 32) >> 6) + 128`;
  - every wrap and rounding as written there, with the tables from
    `x3xttab.py`.

  x3sim.py's XT model should port those functions (or import them). They are
  numpy integer code with the target's word sizes, so a bit-exact port is
  mechanical.
- **Two places where this study's model is not yet bit-exact to the
  intended target, and the x3sim port must decide:**
  - The study requantises with float rounding (`to_int(xr, 13)`) where the
    target reads RQL. They agree except on exact half-LSB ties.
  - The study's IMDCT 'tab' form is the direct form for every subband. The
    target's per-subband switch takes the fast byte-split form for dense
    subbands, which is more precise (44 dB-class against 36). The study is
    pessimistic there.

## 10. Listening notes (reasoned from the metrics and spectra: for the owner to confirm by ear)

Files and their metrics are in [listen/README.md](listen/README.md). They are
three real LAME streams: the 11,025 Hz song, the 8,000 Hz voice and clicks on
a tone. Compare each against `*-ref8.wav`, the float decode at 8 bits, which
is what any 8-bit player would give.

- **XT Standard.** The error is broadband at about -30 dB, 6-7 dB above the
  8-bit rounding the reference already has. Its worst octave is the top one
  (2.8-5.5 kHz on the song, -15 dB of a signal that is quiet there). Expect a
  faint extra grain or hiss on sustained bright passages, probably under the
  24 kbps codec's own artefacts. There are no tonal artefacts: alias -94 dB,
  click pre-echo identical to the reference.
- **XT Fast.** About 2 dB more broadband error, and alias spurs at -39 dB.
  Expect it nearly indistinguishable from Standard on the song and voice. A
  faint metallic edge is possible on pure tones (`clicks-xt-fast.wav`).
- **XT Turbo.** Error at -19 dB, with octaves where it comes within 8-10 dB of
  the signal. Alias spurs at -19 dB on tones. Click pre-echo at -25 dB (heard
  as a slight smear before the clicks). A -23 dB 32-sample ripple, which
  rides on bass as a buzz near 344 Hz and its multiples. Expect audible
  roughness and a metallic, ringing quality on the song. The voice should stay
  intelligible but harsh.
- **XT Speech.** Muffled by design: nothing above 2.76 kHz (song, clicks) or
  2.0 kHz (voice at 8 kHz). Otherwise as clean as XT Standard (32 dB against
  the reduced-rate float decode).
- **Uniform codes (rejected).** Grain that grows as the music gets quieter.
  The quiet synthetic passage measured 1 dB SNR.
- **4 terms by plain truncation (rejected).** Flatter error than XT Turbo's
  but more of it overall. A -1.5 dB DC/bass gain loss and a stronger click
  smear (-22 dB pre-echo).

## 11. Open questions for the integrator

1. **XT Speech's definition.** SPEC.md 121.4 says "the 4:1 pruned rate". On
   an 11,025 Hz source that gives 2,756 Hz, below the Sound Blaster's floor.
   The proposal is "the divisor whose output lands at 4-6 kHz" (section 5).
   Sent to the lead on 2026-10-22.
2. **The CRC-32 at OPEN** (SPEC.md 121.10) would cost about 4.7 s on the
   5150. The proposal: OPEN checks fl32 (0.77 s, or about nothing if folded
   into the stores), and the CRC-32 stays the identity record checked by the
   host and a test build.
3. **The XT part is about 20 KB of unrolled code, not SPEC 121.10's 12 KB.**
   The partition above still fits 400,000 bytes with 19 KB spare, but SPEC's
   planning row should move.
4. **Auto on a 286.** Whether XT Standard or Fast passes 1.25x there is
   phase 2's 86Box measurement. This study's model is an 8088 model.
5. **These are models.** Every speed here is a model within about 5% on its
   largest kernels. The disk throughput during a conversion is an
   assumption.
6. **x3sim.py's hook.** The fixture figures come through
   `x3sim.granules()` (xtmp3/numerics a6b748b2, 2026-10-22). Without that
   hook the study falls back to calling x3sim's parser directly, and it
   fails soft (fixtures skipped) if neither works. `x3xtstudy.py --all`
   re-takes the figures once x3sim.py is merged into `tools/`.

## 12. Reproduce

```
python3 tools/x3cyc.py --calibrate          # the model against the 5150's table
python3 tools/x3xtstudy.py --selfcheck      # filterbank, hybrid, Lee, table formula
python3 tools/x3xtstudy.py --all            # everything: ~1 minute; needs numpy,
                                            # and tools/x3sim.py (here or in
                                            # ../os8088-x3num/tools) for T12 and listen/
python3 tools/x3xtstudy.py --check-design   # the generator's tables == the model's
python3 tools/x3xttab.py --selfcheck        # the generator, standard library only
python3 tools/x3xttab.py --emit DIR         # XTGEN.BIN, every pack, manifest.json
python3 tools/x3cycmarty.py                 # the MartyPC check (nasm, build/martypc/run)
```

`--write-design` rewrites x3xttab.py's DESIGN block and constants from a
run. It is how the literals there were made, and `--check-design` then
proves the two agree.
