# XTMP3 — the front end and the STANDARD backend on the target (S5, S6)

These are the STANDARD track's notes for the integrator to fold into SPEC.md
§121.3 (the record and the ABI), §121.4 (numerics), §121.10 (memory) and
§121.11 (measured). Everything here is what the code on `xtmp3/std` does,
and every number was measured as this file says; nothing is a model unless
it says so.

| file | what it is |
|---|---|
| `apps/xtmp3/x3gran.inc` | **the contract**, equates only: the `X3G_*` record, a backend part's header and verbs, `X3C`/`X3O`/`X3Z`, the front end's API, `X3I`/`X3R`/`X3PC`, the FRONT part's fixed data layout |
| `apps/xtmp3/x3pars.inc` | the front end's PRIMARY half: the open as a state machine, the queue's probe, seeking, and the thin near entries the engine calls |
| `apps/xtmp3/x3phdr.inc` | the frame header (x3sim.parse_header), shared as source by the primary and the FRONT part |
| `apps/xtmp3/x3pnext.inc` | the frame loop - sync, the resync scan, side information, the reservoir, the verdicts - assembled into the FRONT part as its NEXT verb |
| `apps/xtmp3/x3front.asm` | the FRONT part (eager): NEXT, then per granule scalefactors, Huffman, the quarter exponents, the stereo plan, into the record |
| `apps/xtmp3/x3std.asm` | the STANDARD part (lazy): `x3sim.Decoder.granule()` from `requant_i` on |
| `apps/xtmp3/x3crc.inc` | CRC-32 (nibble table), the parts' table checks |
| `tools/x3fronttab.py` | the ISO tables the front end reads, laid out for the 8086 (`build/x3ftab.inc`) |
| `tools/x3stdgen.py` | the DCT-II and the window as straight-line code, traced from `x3sim.dct2_i` and `IntSynth` (`build/x3sgen.inc`) |
| `tests/x3bench/` | the bench: a bare-metal harness (`x3bench.asm`, `x3bboot.asm`), its QEMU gate (`x3bench.py`, the `x3stdexact` soak row) and its MartyPC timer (`x3marty.py`) |

## 1. The contract (`x3gran.inc`, `X3G_ABI` 1)

SPEC §121.3 already carries a summary. What the integrator should know
beyond it:

- **The record** is 5,228 bytes in the FRONT part's data segment at
  `X3P_REC`. Lines are in bitstream order; the header carries the version's
  13 short-band widths (`X3G_SFBS`), so a backend reorders a short block with
  no band table of its own. `X3G_C_NZ` is exact (the last non-zero line + 1),
  every `ix` at or past it is written zero, and `q` is defined below it. A
  channel with `ok` = 0 is silence; `X3G_PLAN` is `LR` whenever a channel is
  not ok, as in x3sim.
- **A backend verb** is entered with DS = the part's own data segment and
  far-called through `op_cseg`; it keeps every register but AX and its
  outputs, never calls the kernel, never masks interrupts, and uses at most
  64 bytes of the caller's stack. The STANDARD part uses about 32 (six
  pushes, the far call, four levels of near call at most).
- **The front end** is near procs in the primary (`x3p_next` a thin entry
  that far-calls the FRONT part's NEXT verb, where the frame loop lives so
  the primary's segment does not carry it). `x3p_open` never reads a
  file: it answers `X3P_NEED` with `[X3I_WANT, +X3I_WANTN)` until the record
  is complete, so the probe (UI task, file reads) and the engine share it.
  `x3p_next` stages a frame from the engine's `X3R` ring (free-running read
  and write words, a producer EOF byte) and answers `X3P_MORE` without
  losing anything - a resync scan in progress consumes what it rejected and
  resumes. The fold of a frame's main data into the reservoir's tail is
  deferred to the next `x3p_next`, because that frame's granules read the
  main data where it lies.
- **x3pars.inc reaches the FRONT part's data through ES**, with DS the
  package's own (the first version of x3gran.inc said DS; corrected).
- **A packed MP3 is refused by the probe.** `OSAPI_FILE_READ_AT` and
  `READ_SEQ` deliver a packed file raw (SPEC.md 20.14.3), so an `.MP3`
  stored lz4-wrapped cannot be streamed. `make` wraps data files by explicit
  rule only, so an MP3 rides plain unless a rule is added (S10's disks).

## 2. The STANDARD backend

It is x3sim's integer model operation for operation. Three parts of it are
not hand-transcribed:

- **The DCT-II and the window are generated.** `tools/x3stdgen.py` runs
  `x3sim.dct2_i` on symbolic inputs with `iadd32`/`isub32`/`imul32`/
  `imul2_32`/`s32` patched to record what they are asked, and writes the
  operations it saw, in order, as macro calls (`XS_ADD`, `XS_SUB`, `XS_NEG`,
  `XS_MULC`, `XS_ROT`, `XS_TOV`); the window's taps come from `IntSynth`'s
  own table. Its `--selfcheck` interprets the emitted program in Python with
  the macros' semantics against `x3sim.dct2_i`, `to_v` and `IntSynth` on
  random and saturating vectors. The DCT-32 is 56 rotations, 149 adds and
  negations and 32 roundings into V a slot.
- **Exact fast paths, each argued exact and each covered by the gate:**
  - an IMDCT output pair in 32-bit registers when its overlap word has
    |o| < 2^30 (then `(o w) >> 14 = 4 (o_hi w) + ((o_lo w) >> 14)` fits
    int32, and `U -/+ t` saturates exactly as a 32-bit overflow says); the
    48-bit general path stays for the rest and for k < 0;
  - a subband whose 18 lines and 9 overlap words are all zero, and an
    antialias boundary whose 16 lines are zero, are skipped: x3sim's
    arithmetic on them yields exactly zero (k = 14, every rounding of 0 is
    0, the new overlap 0);
  - the window multiplies unsigned: the ring holds V + 32768 (V XOR 8000h),
    each product is a `MUL` by |D| added or subtracted by D's sign, and the
    32768 sum(D) this adds is cancelled in each output's rounding constant,
    modulo 2^32 - where the true accumulator lies, so the wrap is exact.
- **Saturation is out of line.** Every clamp of x3sim's format table is a
  `jo`/range test to a CALL that loads the clamp (pre-shifted where a shift
  follows) and counts it in its stage: five bytes a site instead of twenty.
  It never runs on real material and there are thousands of sites; moving it
  took the part from 63,952 to 54,520 bytes before the fast paths.

Divisors 1, 2 and 4 select the DCT-32/16/8 and window bodies once at OPEN;
subbands at and above 32/div are cut after reorder and get no antialias
across the cut, no scale and no IMDCT, as x3sim does. The mono downmix is
`(a >> 1) + (b >> 1) + ((a | b) & 1)`, which is `(a + b + 1) >> 1` without a
33rd bit. Gain (`X3O_GAIN`, Q8) is applied after the clip, saturating; 0 and
256 are the exact model. U8 output is the int16's high byte XOR 80h.

## 3. Exactness (the evidence)

**QEMU, `tests/x3bench/x3bench.py` (the `x3stdexact` soak row, 70 s):**

- the FRONT END: on all 37 fixtures and 6 hostile ones, the open (title,
  artist, frames, duration, exactness, delay, padding, the first audio
  frame, the bytes skipped) against `x3sim.Stream`, every granule record
  (ok, block type, mixed, window switching, NZ, ix and q below NZ, the
  plan) against x3sim's own `granule_spectra`/`quarter_exponents`/
  `stereo_plan`, and the verdict counters against `Stream.counts`: **43 of
  43**;
- the STANDARD part: **all 141 cases of `tests/xtmp3/X3SIM.txt`** - PCM
  SHA-256, length, clip count and saturation count - **and 16 HOSTILE
  cases**: six fixtures (C128, MSF, IS44, IS22, AFTERHRS, MONO44) with every
  granule's global_gain raised by 24, decoded by x3sim inside the gate,
  which saturate in requantisation, stereo, antialias, the IMDCT (k < 0
  subbands, overlap words past 2^30) and the DCT. **157 of 157 exact.**

No golden fixture saturates (`sat 0` on every line of X3SIM.txt), so the
hostile cases are what holds the clamps to the model. They found one defect
on their first run: the requantiser's left shift tested SF after `RCL`,
which sets no SF, so it read the flag of the `SHL` before it. Normal streams
never take that path (s >= 1).

The harness reads its input through an X3R ring a 700- or 1,500-byte chunk
at a time (4 or 8 KB rings), so `X3P_MORE`, the wrap and a scan across a
refill run on every fixture; it runs the parts on a 384-byte stack in its
own segment (SS != DS), whose deepest use was 98 bytes, the harness's own
frames included.

**MartyPC (an 8088 at 4.77 MHz): the same harness and parts on a 360 KB
floppy.** For every measured case the harness folds the PCM it writes into a
CRC-32, which `x3marty.py` compares with the CRC-32 of x3sim's PCM for the
same granules: **exact on every case measured** (eight cases, 12 granules
each). QEMU's i386 and MartyPC's 8088 agree with the model.

## 4. Measured: cycles a granule on the 8088

`tests/x3bench/x3marty.py`, MartyPC's 5150 (`os8088_5150_cga`, run as its
GLaBIOS twin: the ROM does not enter into these cycles). An exec breakpoint
at every stage boundary; the cycles between two are that stage's. Twelve
granules a case, the first three (reservoir silence) excluded; the mean of
the other nine. A granule is 576 source samples (one channel, or both for
stereo). "x real" is audio seconds a wall second at 4.77 MHz.

| stage | AFTERHRS d1 | AFTERHRS d2 | AFTERHRS d4 | C128 d1 | C128 d2 | C128 d4 | C128 d1 mono | C128 d4 mono |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| parse (x3p_next) | 30,760 | 30,892 | 30,764 | 33,993 | 34,001 | 33,929 | 34,065 | 33,998 |
| FRONT (GRAN) | 304,717 | 304,779 | 304,579 | 358,354 | 358,150 | 358,349 | 358,151 | 358,218 |
| requant | 224,082 | 224,086 | 224,142 | 240,410 | 240,372 | 240,420 | 240,420 | 240,428 |
| stereo | 0 | 0 | 0 | 229 | 229 | 229 | 229 | 229 |
| reorder | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| alias | 370,636 | 245,361 | 115,427 | 358,131 | 292,710 | 179,145 | 358,110 | 179,601 |
| scale | 276,397 | 167,347 | 85,983 | 280,841 | 223,535 | 134,405 | 280,858 | 134,215 |
| imdct | 1,100,661 | 643,154 | 317,412 | 1,463,961 | 1,093,450 | 623,515 | 1,463,703 | 623,306 |
| freqinv | 23,617 | 11,949 | 6,122 | 47,309 | 23,989 | 12,260 | 47,503 | 12,324 |
| hybrid.cut | 942 | 9,366 | 13,378 | 1,904 | 18,552 | 26,779 | 1,966 | 26,779 |
| mix | 950 | 944 | 947 | 946 | 947 | 948 | 167,427 | 42,736 |
| dct | 1,568,897 | 637,089 | 248,631 | 3,132,742 | 1,273,547 | 496,615 | 1,567,876 | 248,510 |
| window | 1,575,609 | 772,282 | 377,437 | 3,151,091 | 1,544,998 | 754,914 | 1,575,563 | 377,174 |
| pcm | 120,582 | 65,581 | 37,709 | 241,978 | 131,637 | 76,159 | 120,649 | 37,770 |
| std.entry | 842 | 844 | 844 | 841 | 841 | 841 | 841 | 841 |
| std.exit | 490 | 490 | 490 | 490 | 490 | 489 | 489 | 490 |
| **TOTAL** | **5.60 M** | **3.11 M** | **1.76 M** | **9.31 M** | **5.24 M** | **2.94 M** | **6.22 M** | **2.32 M** |
| **x real** | **0.045** | **0.080** | **0.141** | **0.007** | **0.012** | **0.021** | **0.010** | **0.027** |

AFTERHRS is MPEG-2.5 11,025 Hz mono at 24 kbps (long blocks); C128 is
MPEG-1 44.1 kHz L/R stereo at 128 kbps, "mono" its downmix. "parse" is
x3p_next (the frame); "FRONT (GRAN)" is the FRONT part's granule work, of
which Huffman is about 75% (`--fine`: scalefactors 13 k / 24 k, Huffman
218 k / 287 k, the exponents 13 k / 22 k, the line map and the plan under 6 k,
for the two d1 cases). With `--fine` the IMDCT splits as about 1.0 M / 1.6 M
in its dct9s and unrolled output pairs (the pairs are not separately marked
since they were unrolled) against 0.05 M / 0.08 M of the rest. The row after
this table's measurement, FRONT's one-window pair, took FRONT (GRAN) to
275 k / 351 k and the totals to 5.57 M / 9.31 M.

**Against the model** (NUMERICS.md §8, 11,025 Hz mono, divisor 1): the
model said 3.44 M; the first measurement was **6.81 M**, now **5.57 M**.
The window was where the model put it (1.80 M against 1.75 M, now 1.58 M).
The model under-priced a 32 x 16 product (`mul32`, 330 cycles: a real one
with its 48-bit accumulation and rounding is ~460), and with it the DCT
(1.56 M against 0.77 M), the IMDCT and the antialias; the band scale, the
requantisation and Huffman were 4 to 10 times its figures and are the
stages the optimisations below halved or better.

**What limits it now** is the 8088's multiply: a window output is 16
products (~143 cycles each with its operand), a DCT-32 rotation is four
16 x 16 multiplies (~600 of its ~900 cycles), and the IMDCT is 10 multiplies
an output pair. The window and the DCT are within about 20% of their
multiply floor; the rest of the decoder is a third of the time.

**The verdict for SPEC §121.5:** STANDARD is nowhere near real time on an
8088 - 11,025 Hz mono at divisor 1 runs at 0.045x (a granule must cost 249 k
cycles to be real time; it costs 22 times that), and 44.1 kHz stereo at
0.007x. The XT path is the XT backend or Decode first, as SPEC §121.5
already says; this is now measured rather than modelled. For Decode first
with STANDARD on an XT, a 3-minute 11 kHz mono track takes about
67 minutes.

**Optimisations, each re-measured and each exact on the gate:**

(Each row a 10-granule run, the first three excluded; the last row a
12-granule one, as the table above.)

| change | 11k mono d1 | 44.1k stereo d1 |
|---|---|---|
| first measurement | 6.81 M | 12.86 M |
| IMDCT pair in 32-bit registers when |o| < 2^30 | 6.51 M | 12.29 M |
| band scale's shift picked once a subband; FRONT's Huffman reader in BP; clamps out of line | 6.32 M | 12.11 M |
| the nine long IMDCT pairs unrolled; window coefficients immediate | 6.09 M | 11.62 M |
| all-zero subbands and boundaries skipped; the window's unsigned MULs | 5.61 M | 9.48 M |
| FRONT's common Huffman pair from one window | 5.57 M | 9.31 M |

## 5. Memory

| what | bytes | |
|---|---|---|
| the primary: image + bss | 19,670 + 10,213 = 29,883 | after the face's art moved to its part; of which x3pars.inc **4,023 of code and tables and 362 of bss** (a 256-byte X3I record for the queue's probe among them). It was 5,491 + 389 until the frame loop moved into the FRONT part (the primary is the package's tightest segment) |
| FRONT part (eager, in the carve) | 24,509 | 9,048 fixed data (the record 5,228, the main-data buffer 1,968, the frame 1,448, side information and scalefactors), the Huffman tables 8,924, the frame loop and the granule code the rest; 8,982 packed on the disk |
| STANDARD part (lazy, op_fetch) | 57,732 | code 38,702 (the generated DCT-32/16/8 and windows are most of it), tables 4,860 (x3std.bin from P43M on), state 14,170: xr 4,608, the reorder's copy 2,304, overlap 2,304, the two rings 4,096, the regrouped IMDCT window 336; 22,533 packed |
| the probe's transient claim | 8,192 (2 clusters, 32 KB at most) | UI task, freed before it returns |

**The STANDARD path's peak**, the engine's rings and the sound driver's
grant excluded (they are the engine track's to add): the primary 29,883 +
FRONT 24,509 + the face's ART part 12,985 (eager) + STD 57,732 = **125,109
bytes** (about 122 KB), with 8 KB more for a moment while a probe reads a
file. The STANDARD part is never fetched on an
XT's own path, and the XT backend's part replaces it rather than joining it
(SPEC §121.3).

## 6. Open items

- **The integrator's merges**: `xtmp3.asm` carries the parts table (a marked
  place for the XT rows), `op_load` and `x3p_init` at the entry, the bss
  offset by `OP_BSS`, and `x3pars.inc` included with `X3P_WANT_PROBE`;
  `x3demo.inc`'s `x3_e_qadd` is the probe's one call and shows the engine
  the same call. The Makefile block is marked.
- **Seeking** (`x3p_seekpos`, `x3p_reset`) is written and assembled but not
  exercised by a row: x3sim has no seek, and S8's live playback is its
  first user.
- **CRC is parsed and not checked** (SPEC §121.2), as in x3sim.
- **A 32 KB-cluster volume**: the probe's buffer is capped at 32 KB, so a
  request that straddles one such cluster is not answered and the probe
  gives up (no title). Only 1-2 GB FAT16 partitions have such clusters.
- **Further speed** would come from a register-allocated DCT (its adds load
  and store every operand) and dct9, about 10% together by estimate; the
  multiplies themselves are the floor.
