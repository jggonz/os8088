# XTMP3: the XT backend (story S7)

The XT backend is `apps/xtmp3/x3xt.asm`, a far-called `OP_SEG` part. It
reconstructs MP3 granules into unsigned 8-bit mono PCM using product tables
in place of multiplications (SPEC.md 121.3, 121.4, 121.10). These notes are
the XT track's contract material for SPEC §121.4, §121.10 and §121.11. They
cover what the part computes and where that departs from the study, how it
meets `apps/xtmp3/x3gran.inc`, the evidence that it is bit-exact, and what
MartyPC measured.

| file | what |
|---|---|
| `apps/xtmp3/x3xt.asm` | the part: verbs, the granule chain, the table expansion |
| `tools/x3simxt.py` | THE MODEL: the XT chain bit for bit (its docstring is binding), over x3sim's front end; `--golden`, `--check`, `--quality` |
| `tests/xtmp3/X3XT.txt` | the 51 golden PCM hashes: 14 fixtures by the four profiles |
| `tools/x3xtgen.py` | what the part assembles rather than computes: the claim layout, Lee's DCTs, every profile's window pass, the IMDCT row blocks, constant tables, `--seal`; standard library only (`--fullcheck` is the numpy cross-check) |
| `tests/x3xtunit.py` | the part in Unicorn: the contract, the expansion, the refusals, bit-exact granules (soak row `x3xtunit`) |
| `tests/x3xtbench/` | the part behind the SHIPPING front end on QEMU (soak row `x3xtexact`) |
| `tools/x3xtmarty.py` | the MartyPC measurement (a design tool, not a row) |
| `docs/xtmp3/xt-marty.json` | its numbers |

## 1. What the part computes

`tools/x3simxt.py`'s docstring is the definition, step by step. The study
(XT-STUDY.md) fixed the arithmetic; S7 had to settle what a target actually
runs. Section 9 of the study left two points open, and both were resolved
toward the target:

- **Requantisation reads RQL.** It uses x3xttab's exact table for
  `|ix| <= 15`, and for larger values x3sim's pow43 is scaled to Q13 with
  one rounding. Every profile takes this one linear path. The study's RQ
  code table goes unused, because stereo processing and the mono downmix
  both need linear values. One path is also one kernel.
- **The IMDCT switches per subband.** The switch was measured, not taken
  from the model. On MartyPC the sparse table form costs 2,700 + 1,280 n
  cycles for n nonzero lines, and the factored form costs 17,900 flat.
  `FAST_MIN` is therefore 13 lines, where the study had modelled 9.6.

Further target decisions the study did not make:

- **The factored IMDCT uses `imul`.** It follows minimp3's 18-point
  factorisation, with Q15 constants rounded as `(a c + 2^14) >> 15` and one
  rounding per two-product sum. It does not use split tables, which would
  cost 25 KB of tables for about 1% of time.
- **Fast and Turbo window the IMDCT with IMWIN's byte-split rows**, as
  Standard does. The study's IMWTAB rows needed a Z-map encoder that no
  pack carries; the study's model had used a 64 KB float encoder that the
  target cannot hold.
- **The output is mono.** When both channels' blocks agree, the spectra
  are mixed before one hybrid pass:
  - an M/S line takes M;
  - an L/R line takes `(L + R + 1) >> 1`;
  - an intensity line takes `(L kl + L kr + 2^14) >> 15`.

  When the blocks disagree, each channel runs its own hybrid and the
  windowed outputs are averaged before the one overlap.
- **The V codes come from X.** Each kept DCT output is coded once, and a V
  value's code is plus or minus that code. The sign goes into the add or
  sub instruction.
- **Wrapping arithmetic**, as XT-STUDY.md 3 pins it, everywhere except the
  requantiser's and stereo's saturations (both counted).

Profiles are `std` (16 window terms), `fast` (8), `turbo` (4, experimental)
and `speech` (16 terms at divisor 2 on an MPEG-2.5 source and 4 on an
MPEG-2 one; refused on MPEG-1).

## 2. The ABI (`apps/xtmp3/x3gran.inc`, X3G_ABI 1)

- **Header**: `'X3'`, `X3G_ABI`, `X3K_XT`, six verbs, and their vectors.
  The seal follows at offsets 18-25 (below).
- **CAPS** (AL profile, AH divisor, CL 1): `X3C_OFFER` = 0Fh. The window
  segments, SY, H1 and H2 are separate claims of at most 4,096 paragraphs
  each:

  | profile | `X3C_NSEG` | claims (KB) | total | `X3C_TKB` |
  |---|---|---|---|---|
  | std | 5 | 61, 62, 42, 48, 23 | 236 KB | 223 |
  | fast | 5 | 64, 5, 42, 48, 23 | 182 KB | 160 |
  | turbo | 4 | 36, 42, 48, 23 | 149 KB | 128 |
  | speech | 4 | 63, 26, 48, 23 | 160 KB | 148 |

  `X3C_PARTKB` = 29: the resident half (section 6).
- **OPEN** takes one output channel only (`X3E_CHAN` otherwise), U8 or S16,
  and the gain. A divisor that is not the profile's is `X3E_DIV`; XT Speech's
  divisor must match the version. A missing or zero segment is `X3E_MEM`. A
  version or rate index out of range is `X3E_RECORD`. A tail or table that
  fails its check is `X3E_TABLE`. OPEN implies RESET.
- **GRANULE** returns CX = 576 / divisor samples and AX = this granule's
  clips. A record of another version or rate than OPEN's is `X3E_RECORD`;
  GRANULE before OPEN is `X3E_NOTOPEN`. U8 is the model's own byte. S16 is
  `(u8 - 128) << 8`. A gain multiplies that int16, floors the shift and
  saturates.
- **STATS** (`X3Z_*`):
  - `X3Z_SAT` is the requantiser's and stereo's saturations;
  - `X3Z_STAGE[0]` is requantisation and `[1]` is stereo;
  - `[6]` counts V values past ±2 FS (ENCV's slow path, a clamp to the code
    map's top, not counted in `X3Z_SAT`);
  - `X3Z_SILENT` counts channel-granules decoded as silence.
- **Every call** keeps every register except AX and its outputs, leaves DF
  clear, never touches IF, and never calls the kernel. Its deepest measured
  stack use is 50 bytes, counting the far call's four; `tests/x3xtunit.py`
  holds every call it makes to 64. The bench's 384-byte worker stack measured a low-water
  mark of 104 bytes, the harness's own frames included.

## 3. Bit-exact: the evidence

| witness | what ran | result |
|---|---|---|
| `x3xtexact` (QEMU) | the shipping `x3pars.inc` and `build/x3front.bin` into `build/x3xt.bin`, every case of `X3XT.txt` | 51 of 51 exact (12 s); a damaged twiddle constant is reported at its first differing sample |
| MartyPC's 8088 core | `tools/x3xtmarty.py`: AFTERHRS, X8K16, X11K24 by the four profiles, every granule's PCM against the model | bit-exact on all 12 |
| `x3xtunit` (Unicorn) | four fixtures and 30 synthetic split-path records a profile (disagreeing blocks, every plan code, `|ix|` to 8,206, q past both ends of RQL, 75% of outputs unclipped), with OPEN's half overwritten by `int3` after every OPEN | bit-exact; every expanded table byte-identical to `x3xtgen.expanded()` |

Negative controls (`x3xtunit --expand`):

- an unbiased recurrence is caught by the tail check, and caught again by
  the table check after re-sealing;
- a damaged XTGEN.BIN is refused;
- an OPEN after the tail is gone is refused;
- every `X3E_*` refusal is checked.

## 4. Speed: MartyPC's IBM 5150 at 4.77 MHz

These figures come from the cycle counter. They cover the part's GRANULE
only: the FRONT part's side information, scalefactors and Huffman decode
are not included. The study modelled those at about 88,000 cycles a granule
on the song, about 6%.

| profile | fixture | divisor | cycles a granule (mean / min / max) | audio s a wall s | wall s an audio s |
|---|---|---|---|---|---|
| XT Standard | AFTERHRS (11,025 Hz, 24 kbps song) | 1 | 1,505,847 / 917,942 / 1,887,482 | 0.166x | 6.04 |
| XT Standard | X8K16 (8 kHz, 16 kbps voice) | 1 | 1,450,529 / 1,022,842 / 1,743,049 | 0.237x | 4.22 |
| XT Standard | X11K24 (11,025 Hz, clicks on a tone) | 1 | 1,440,692 / 1,051,399 / 1,683,247 | 0.173x | 5.78 |
| XT Fast | AFTERHRS | 1 | 1,363,354 / 791,168 / 1,739,425 | 0.183x | 5.47 |
| XT Fast | X8K16 | 1 | 1,317,441 / 889,753 / 1,598,428 | 0.261x | 3.83 |
| XT Fast | X11K24 | 1 | 1,308,932 / 924,625 / 1,533,139 | 0.190x | 5.25 |
| XT Turbo | AFTERHRS | 1 | 1,276,955 / 704,768 / 1,653,025 | 0.195x | 5.12 |
| XT Turbo | X8K16 | 1 | 1,231,043 / 803,353 / 1,512,006 | 0.279x | 3.58 |
| XT Turbo | X11K24 | 1 | 1,222,534 / 838,225 / 1,446,739 | 0.204x | 4.90 |
| XT Speech | AFTERHRS | 2 | 839,890 / 430,292 / 931,685 | 0.297x | 3.37 |
| XT Speech | X8K16 | 2 | 797,360 / 495,541 / 896,081 | 0.431x | 2.32 |
| XT Speech | X11K24 | 2 | 799,647 / 511,010 / 895,837 | 0.312x | 3.21 |

Where the time goes on the song, from a breakpoint trace of 16 granules
spread over it:

| stage | Standard | Fast | Turbo | Speech |
|---|---|---|---|---|
| window (the product rows) | 432,286 (28.7%) | 305,512 (22.5%) | 219,112 (17.2%) | 219,958 (26.1%) |
| matrixing (Lee DCT and the V codes) | 389,374 (25.9%) | 389,374 (28.6%) | 389,374 (30.6%) | 158,902 (18.9%) |
| IMDCT (long blocks) | 296,863 (19.7%) | 292,602 (21.5%) | 292,602 (23.0%) | 212,852 (25.3%) |
| IMDCT window, with the overlap fused in | 152,366 (10.1%) | 150,713 (11.1%) | 150,713 (11.8%) | 97,632 (11.6%) |
| requantisation and stereo | 96,043 (6.4%) | 96,043 (7.1%) | 96,043 (7.5%) | 72,182 (8.6%) |
| antialias | 67,547 (4.5%) | 56,713 (4.2%) | 56,713 (4.5%) | 46,766 (5.6%) |
| short blocks (IMDCT and window) | 32,154 (2.1%) | 32,154 (2.4%) | 32,154 (2.5%) | 19,496 (2.3%) |
| overlap (short and zero subbands) | 19,305 (1.3%) | 19,743 (1.5%) | 19,743 (1.5%) | 3,884 (0.5%) |
| output copy, dispatch | 17,952 | 17,963 | 17,971 | 10,445 |

**What this means.**

- **Nothing on the XT is real time**, as SPEC.md 121.5 expects: an 8088
  decodes first.
- **A 3-minute 11 kHz song converts in about 18 minutes with XT Standard**
  (6.04 s an audio second, decode only), 16 with Fast, 15 with Turbo and 10
  with Speech.
- **Measured against the study's model** (song, decode only, the model's
  88,000-cycle front end removed):
  - Standard is 1.51 M cycles against the model's 1.37 M;
  - Speech is 0.84 M against 0.71 M.
- **Turbo takes 6.3% fewer cycles than Fast on the song**, where the study
  said about 8%. Matrixing does not shrink with the window, and it is the
  largest stage of both.

## 5. OPEN: expansion and the checks

| profile | OPEN | of which the Fletcher (tables + the tail check) | first build | model |
|---|---|---|---|---|
| XT Standard | 2.959 s | 0.867 s | 4.253 s | 2.11 s (Fletcher 0.77) |
| XT Fast | 2.322 s | 0.651 s | 3.349 s | 1.62 s |
| XT Turbo | 1.990 s | 0.539 s | 2.907 s | 1.37 s |
| XT Speech | 2.100 s | 0.608 s | 3.098 s | 1.49 s |

**The first build took about twice the model's time.** Three changes, with
every expanded byte unchanged, took 30% off it:

- the Fletcher loop unrolled eight words a pass;
- the encoder walks keep the midpoint they wait for in a register, so an
  entry costs a compare and two stores;
- the byte-split planes and the window rows keep their constants in
  registers and store with STOSW.

The Fletcher stays a READ-BACK of the expanded tables, not a sum folded into
the stores (XT-STUDY.md 6). A read-back is what catches a bad RAM cell or a
misplaced store on period hardware, and a store-order sum cannot.

The window rows are now the largest part of OPEN: 1.23 s on Standard, for
241 rows, their Fletcher included.

**Calibration on an XT pays an OPEN per XT rung probed.** The ladder (S8)
should treat that as a known cost: 3.0 s for XT Standard.

## 6. Memory

**The part has two halves.** The image is 58,634 bytes; 23,841 are on the
disk, lz4-packed.

- **The resident half**, `x3x_head` .. `x3x_tail`, is 29,568 bytes (29
  KB). It holds the verbs, GRANULE's whole chain, the constant tables, and
  a slot of 8,308 bytes. OPEN copies the chosen profile's window pass and
  Lee kernel into that slot.
- **OPEN's half** is 29,066 bytes. It holds OPEN's body, the expansion, the
  row lists, every profile's kernels and XTGEN.BIN (5,578 bytes; the part
  carries it because the OPEN record has no slot for an asset).

So once OPEN has succeeded, **the engine may shrink the part's claim to
`X3C_PARTKB` = 29 KB** with `OSAPI_MEM_REGROW`, which always succeeds in
place. A part shrunk that way must be fetched whole again before another
OPEN. OPEN's resident stub checks the tail's Fletcher-32 before entering it.
`tools/x3xtgen.py --seal` stamps that sum in the make rule. A tail that is
gone or damaged is `X3E_TABLE` and never runs, and an intact one takes OPEN
after OPEN. `tests/x3xtunit.py` decodes every granule with the tail
overwritten.

**The working set against 400,000 bytes.** Every item below is in KB as
claimed. The primary is the engine track's current one: image 49,182 + bss
13,900. The ring is the engine's 32 KB look-ahead plus a granule's PCM. The
grant is SOUND.DRV's 16 KB.

| profile | during OPEN (no ring yet) | after OPEN and the shrink, with the ring | with the grant too |
|---|---|---|---|
| XT Standard | 62 + 22 + 58 + 236 = 378 KB = 387,072 | 62 + 22 + 29 + 236 + 35 = 384 KB = 393,216 | 400 KB = 409,600: over |
| XT Fast | 324 KB = 331,776 | 330 KB = 337,920 | 346 KB = 354,304 |
| XT Turbo | 291 KB = 297,984 | 297 KB = 304,128 | 313 KB = 320,512 |
| XT Speech | 302 KB = 309,248 | 308 KB = 315,392 | 324 KB = 331,776 |

**XT Standard fits only if the engine orders its claims.** The XT part is
fetched, its segments claimed and OPEN run, and the part shrunk, all before
the 35 KB ring is held. With the ring held through OPEN, Standard peaks at
413 KB (422,912 bytes).

**The sound grant never coexists with the XT part on an XT.** A
decode-first conversion needs no grant, and a finished WAV plays with the
XT part dropped. Fast, Turbo and Speech fit in every order.

The S3 study had planned 380,682 bytes. The difference is:

- this part's code (29 KB resident, against a 20 KB estimate);
- the FRONT part (22 KB, against an 8 KB Huffman estimate plus a 4 KB
  reservoir);
- the claims' KB rounding.

## 7. Optimisations, measured on MartyPC

All of these are bit-exact. XT Standard on the song went from **1,662,211**
cycles a granule (first build, model threshold) to **1,505,847** (−9.4%):

| change | saved a granule (Standard, song) |
|---|---|
| FAST_MIN 10 → 13 (the measured crossover) | ~9,600 |
| antialias stops at the last boundary below nonzero lines; zero subbands skip their test | ~22,000 |
| Lee's bottom DCT-4 groups in registers (8 memory accesses for 19) | ~32,000 |
| the long IMDCT window fused with the overlap (no Y36 round trip) | ~36,000 |
| Lee's odd-add runs through a 3-register write-back cache | ~9,500 |
| the window's PCM8 conversion reads the clip sign from CF; pairs grouped by segment | ~5,000 |
| a stride-specific requantiser loop (CWD's branch-free sign) | ~7,000 |
| short-block windows whose six codes are zero skip their twelve multiplies | ~1,000 (X8K16: ~29,000) |
| antialias butterflies whose two lines are zero skip | ~33,000 |

**Left**, roughly in order of what the measurements say is worth it:

- the factored IMDCT's 9-point DCT in registers, about 2%;
- the V codes written straight into the rings, about 1%;
- a linear rather than doubled V-code ring, about 1%; Fast's first window
  segment is exactly 64 KiB, so this needs a layout change;
- the Fletcher folded into OPEN's stores, about 1 s of OPEN.

The window (the product rows) is within 11% of the 1,277 cycles a 16-tap
pair measured alone in the study: 1,413 a pair, its ring stores and the PCM
conversion included.

## 8. Quality: against the float decode, the 8-bit float, and lame

`tools/x3simxt.py --quality` covers all 14 fixtures. SNR is against x3sim's
float decode at the same divisor. Peak and DC are in 8-bit LSBs. "float8"
is the float decode rounded to 8 bits, which is what any 8-bit player
gives.

| profile | SNR vs float | peak | DC | clips | float8 for scale |
|---|---|---|---|---|---|
| XT Standard | 30.7-33.7 dB | 2.0-5.7 | -0.01 to -0.05 | 0-2 | 34.4-39.2 dB |
| XT Fast | 28.6-30.8 dB | 2.4-6.9 | -0.03 to -0.06 | 0-2 | |
| XT Turbo | 18.2-20.0 dB | 9.0-25.4 | -0.04 to -0.11 | 0-1 | |
| XT Speech | 31.3-33.7 dB | 1.9-5.4 | 0.00 to -0.04 | 0 | |

On the study's five real streams (AFTERHRS, X8K16, X11K24, X12K32, M16) the
target's chain measures:

| profile | measured here | the study's |
|---|---|---|
| Standard | 30.7-31.9 | 31.1-32.4 |
| Fast | 28.6-30.0 | 28.4-29.8 |
| Turbo | 18.8-19.5 | 18.7-19.4 |
| Speech | 31.3-33.0 | 31.8-32.8 |

Fast and Turbo agree with the study to 0.2 dB, and Speech to 0.5 dB.
Standard sits 0.4-0.5 dB below it. About 0.3 dB of that comes from the
measured IMDCT threshold, because more subbands now take the coded sparse
form. The rest comes from the target's exact requantiser and coding where
the study's model rounded in floats.

Against `lame --decode` the figures equal the float ones, except:

- **IS22 (about 10 dB)**: mpglib's intensity stereo is the known outlier
  (NUMERICS.md 6);
- **BBADSIDE (about 12 dB)**: lame conceals damaged frames differently;
- **X12K32 on Speech (27.4 dB)**: lame's full rate, decimated, is a
  different filter from the reduced rate.

## 9. Open items

1. **The engine's claim order and the shrink** (section 6) decide whether
   XT Standard fits 400,000 bytes. The numbers have been sent to the engine
   track.
2. **The part carries XTGEN.BIN.** If the integrator prefers it as an
   asset, the OPEN record needs a segment field for it.
3. **OPEN takes 2.0-3.0 s.** A calibration that probes XT rungs on an 8088
   pays that each time.
4. **End-to-end speed with the real FRONT part**, and the conversion's disk
   share, are S8/S10's measurement.
5. **These figures come from MartyPC.** No 86Box 286/386/486 run has been
   made yet. Those runs would decide whether XT Standard or Fast is real
   time live on a 286 (SPEC.md 121.5's Auto).
