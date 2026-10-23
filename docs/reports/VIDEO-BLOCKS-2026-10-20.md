# Video blocks, version 2: what the encoder gained, at what CPU

**A measurement, not a description.** Taken 2026-10-20 on branch
`video-compression`, on top of `68296b3`, on the same 4-core Xeon
container, ffmpeg 6.1.1 and the owner's source clips as the two reports
before it:
- docs/reports/VIDEO-COMPRESSION-2026-10-20.md;
- docs/reports/VIDEO-CODECS-2026-10-20.md.

It covers the commit that lands this file:
- SPEC.md 98.1.10, version 2 block records;
- `tools/os88vid.py`, the reader, verifier and `cycles_of`;
- `tools/os88venc.py --blocks on`, in the encoder and in its window.

Host-side only, and no emulator ran. True of that tree, and not maintained
against later trees.

**The owner's brief** (2026-10-20): an encoder-only change, borrowing as many
of Video-1's ideas as pay, in a variant of our own with no compatibility
with real Video-1. Space saved at rest is worth having on its own, if it
trades about equal on CPU. **No player plays a version 2 file yet.**
`vp_parse` refuses any version but 1, and that is the behaviour wanted until
one is taught.

## 0. The answer

| format | block kinds | quality at the same disk budget | file at the same budget | CPU |
|---|---|---|---|---|
| **VGA8 on LIN320** | Video-1's three: FILL, TWO, EIGHT | **+9.4 dB** (4x4), +6.0 plain | **-14%** | 2.6x, 14% -> 36% of the 286-fast |
| **VGA8 on Mode X** | the same, a plane byte of four rows | **+9.0 dB** (4x4), +8.0 plain | **-20%** | 1.7x, 25% -> 42% of the 286-fast |
| **VGA4** | PFILL (a Bayer-dithered fill) and TWO, 8x4 | +1.0 to +3.4 dB | -1% to -6% | 1.2x to 1.7x |
| **one bit** (CGA, Hercules) | a 256-tile codebook of 16x4 tiles | +1.6 to +2.9 dB under a tight disk; **none at the 5150-st225's own rate** | -1% to -5% | about 2x; held to equal CPU, the gain is +0.35 dB |

So, in the order the ask weighs them:

1. **VGA8, on both surfaces, is the clear case.**
   - The gain is large, and the files come out smaller at the same budget.
   - The CPU it costs fits on the machine the VGA demos are made for.
   - At equal quality the file is 36% to 57% smaller, at about the CPU
     arm A had (section 4).
2. **VGA4 is a moderate case.**
   - Gas Gas Gas at a 35 KB/s budget with blocks matches 60 KB/s without
     them, in a file **36% smaller**.
   - Bebop is near that, at 0.9 dB short.
   - The CPU rises less than VGA8's.
3. **One bit does not trade equal on CPU.**
   - Under a tight disk the codebook buys quality by spending roughly twice
     the decode time.
   - Held to the CPU it had (`--avg 0.3`), almost all of that gain goes.
   - At the 5150-st225's own disk rate it buys nothing: frames are cut as
     much by the per-frame ceiling as by the disk there.
   - It stays in the format and the encoder, because a format slot costs
     nothing. A one-bit player is not worth building ahead of the VGA ones.

**Every CPU figure for a block is an ESTIMATE.** `os88vid.blk_cost` prices
each kind from the loops in VIDEO-COMPRESSION section 8, and the encoder
ranks against those prices. No 8086 code decodes a block yet, so neither
the prices nor the CPU column above has been benched. The first decoder must
take that bench before any figure here is quoted as measured.

## 1. What was built

- **The format** is SPEC.md 98.1.10. A version 2 file is a version 1 file
  whose stream records may also carry block lists.
  - Blocks use version 1's own segment grammar, skip-coded or absolute.
  - They start on a canvas row that is a multiple of 4 and are 4 rows tall.
  - A key record never holds one, so a seek draws a version 1 picture.
  - One-bit records also carry a codebook delta, and a whole codebook is
    required at frame 0 and after every key.
  - For now version 2 refuses RESIDENT, LIVE, PAGE, KPOOL, a layer, a
    flipped file, and the TEXT, C160 and C512 renditions.
- **`tools/os88vid.py`** reads and verifies it, and refuses it wherever the
  section refuses it.
  - Its selfcheck builds one fixture per surface by hand from the SPEC's
    wording, not through the encoder: VGA4, LIN320, Mode X, CGA and
    Hercules.
  - It also refuses five new corruptions: a version 2 file read as version
    1, a tile with no codebook, a record after a key with no codebook, a
    block on a row that is not a multiple of 4, and a block over a list's
    byte.
  - Broken on purpose three ways, the selfcheck goes red each time: a
    Video-1 kind swapped, the PFILL mask's comparison reversed, and the
    codebook's first index ignored.
- **`tools/os88venc.py --blocks on`** offers blocks as candidates beside
  spans in every cut frame.
  - Each block is priced in bytes and in the profile's own cycles, as spans
    are.
  - Blocks are ranked with spans by error removed per share of the scarcer
    budget (98.2.1's existing ranking).
  - A frame that fits is still exact. The next frames sharpen what a block
    approximated, by the same ageing weights a cut span gets.
  - `--blocks` refuses every combination 98.1.10 refuses, naming it.
  - Options version 47, migrating 46's files to `blocks: off`.
  - The encoder window has the option under Trade-offs, labelled "a version 2
    file (no player plays it yet)".
- **With `--blocks off` the encoder is unchanged.** Four 4-second encodes
  were run through this tree and through `68296b3`: VGA8, Mode X, VGA4 and
  Hercules on the 5150-st225. In each, every record and every log line is
  identical. The only differing bytes are the options block, which records
  version 47 and the new key.

## 2. How it was measured

- **Clips.** 15 seconds of each, from 0:30.
- **VGA.** `--profile 286-fast --audio none`, with page-ahead off for both
  arms where it was set. A version 2 file never runs page-ahead.
- **One bit.** `--profile 5150-st225`, PCM8 at 11,025 Hz sound, as the
  CGA and Hercules demos are made.
- **Arm A** is today's encoder and **arm B** adds `--blocks on`. Both run at
  the same `--disk`.
- **Every version 2 file passed `os88vid.py verify`**: each frame decoded
  through its keyframe by 98.1.10's rules.
- **Scores.** Each is PSNR of the decoded picture against the source at the
  canvas size, with the source aligned and tone-matched as in the earlier
  reports.
  - Three readings are taken: plain, 2x2 box-blurred and 4x4 box-blurred.
  - The 4x4 reading is the "seen from the chair" one. It also hides a 4x4
    block's own edges, so the plain reading is given beside it for the VGA8
    rows, where blocks are 4x4.
  - Each file is aligned on its own: sound ahead moves the frames.
- **KB/s** is the picture's stream rate.
- **8088 %** is `cycles_of`'s price for the stream as a share of a 4.77 MHz
  8088. Blocks are priced at their estimated cost.
- The 286-fast shares quoted in the tables come from the encoder's own
  model of that machine.

## 3. The results

### VGA8 on LIN320 (Mechwarrior 2, 320x200, 256 colours)

| arm | budget | KB/s | 8088 % | plain | 2x2 | 4x4 | file |
|---|---|---|---|---|---|---|---|
| lossless | - | 211.1 | 107% | 31.56 | 35.69 | 38.17 | - |
| A | 60 KB/s | 87.5 | 46% | 24.28 | 25.62 | 26.62 | 1,697,280 |
| **B** | 60 KB/s | 72.6 | 121% | **30.27** | **33.08** | **36.02** | 1,461,760 |

- On the 286-fast, the mean decode goes from 14% of the machine to 36%.
- The worst frame stays under the per-frame ceiling, as in arm A.
- Arm B at a third of the lossless stream's rate is 2 dB of 4x4 short of
  lossless.

### VGA8 on Mode X (Last Exile, 320x240)

| arm | budget | KB/s | 8088 % | plain | 2x2 | 4x4 | file |
|---|---|---|---|---|---|---|---|
| lossless | - | 350.5 | 184% | 29.38 | 33.17 | 34.82 | - |
| A | 100 KB/s | 138.0 | 74% | 18.58 | 19.62 | 20.24 | 2,343,424 |
| **B** | 100 KB/s | 112.1 | 168% | **26.60** | **28.00** | **29.24** | 1,870,336 |

- On the 286-fast, the mean decode goes from 25% of the machine to 42%.
- The keyframes shrink as well, from 205 KB to 130 KB.
- A key is still version 1, but the screen it records is flatter where
  blocks drew it. This is the likely reason the keys shrink; it has not
  been checked separately.

### VGA4 (Gas Gas Gas 640x360, Bebop 320x240)

| clip | arm | budget | KB/s | 8088 % | plain | 2x2 | 4x4 | file |
|---|---|---|---|---|---|---|---|---|
| Gas | A | 35 KB/s | 52.7 | 42% | 18.84 | 22.92 | 23.98 | 930,304 |
| Gas | **B** | 35 KB/s | 50.6 | 86% | 18.48 | 24.82 | **26.60** | 908,288 |
| Gas | A | 60 KB/s | 83.6 | 66% | 19.21 | 24.56 | 26.05 | 1,418,752 |
| Gas | **B** | 60 KB/s | 77.9 | 110% | 18.74 | 26.00 | **28.31** | 1,334,272 |
| Gas | A | 100 KB/s | 108.5 | 86% | 19.36 | 26.07 | 28.17 | 1,818,112 |
| Gas | **B** | 100 KB/s | 102.6 | 106% | 19.13 | 26.65 | **29.21** | 1,721,344 |
| Bebop | A | 30 KB/s | 46.0 | 47% | 12.68 | 16.56 | 17.95 | 832,000 |
| Bebop | **B** | 30 KB/s | 45.5 | 63% | 13.27 | 18.94 | **21.10** | 826,368 |
| Bebop | A | 60 KB/s | 72.5 | 72% | 13.85 | 19.73 | 21.97 | 1,256,448 |
| Bebop | **B** | 60 KB/s | 71.3 | 82% | 14.02 | 21.65 | **25.36** | 1,240,576 |

- PFILL is a dithered fill, so on Gas the plain reading falls slightly
  while the seen readings rise. The dither's pattern counts against plain
  PSNR in full, exactly as the arm A dither does.
- On the 286-fast the mean decode is 18% of the machine in arm A at 60 KB/s
  and 26% in arm B.

### One bit on the 5150-st225 (Take On Me and Bad Carrot on Hercules, a starfield on CGA)

| clip | arm | budget | KB/s | 8088 % | plain | 2x2 | 4x4 | file |
|---|---|---|---|---|---|---|---|---|
| Take On Me | A | the disk's own | 101.5 | 58% | 9.01 | 17.64 | 21.45 | 1,316,352 |
| Take On Me | B | the disk's own | 99.5 | 61% | 9.02 | 17.62 | 21.42 | 1,291,776 |
| Take On Me | A | 40 KB/s | 34.0 | 22% | 8.37 | 14.65 | 16.77 | 578,048 |
| Take On Me | **B** | 40 KB/s | 31.8 | 53% | 8.88 | 16.63 | **19.63** | 550,400 |
| Take On Me | B, `--avg 0.3` | 40 KB/s | 34.0 | 28% | 8.51 | 14.98 | 17.12 | 576,000 |
| Bad Carrot (threshold) | A | 30 KB/s | 22.9 | 16% | 14.80 | 15.59 | 16.31 | 556,544 |
| Bad Carrot (threshold) | **B** | 30 KB/s | 22.4 | 34% | 18.20 | 18.66 | **19.04** | 551,936 |
| Starfield (CGA) | A | 25 KB/s | 19.6 | 12% | 8.52 | 16.44 | 18.70 | 293,376 |
| Starfield (CGA) | **B** | 25 KB/s | 19.8 | 20% | 8.76 | 17.57 | **20.33** | 293,888 |

- **At the disk's own rate** Take On Me gains nothing.
  - 64 of 252 frames are cut by the per-frame ceiling rather than the disk.
  - A whole codebook after every key costs 2 KB.
  - The tiles the encoder can afford there draw what spans would have drawn.
- **Under a tight disk** the codebook helps (+1.6 to +2.9 dB of 4x4), but
  the 8088 time doubles.
- Held to roughly the CPU it had, with `--avg 0.3`, the gain is +0.35 dB.
- This is the half of the brief, "if it trades about equal on CPU", that
  one bit does not meet.

## 4. Same quality, smaller file

The tables above hold the budget fixed. The owner's question is the other
way round: hold the picture and ask for the bytes.

Three more arm B encodes were run at lower budgets, with the settings of
section 3. In each pair, arm B's file is smaller and looks at least as good:

| clip | arm A | arm B | 4x4, A -> B | file, A -> B | 8088 %, A -> B |
|---|---|---|---|---|---|
| Mechwarrior 2, LIN320 | 60 KB/s | 25 KB/s | 26.62 -> **33.02** | 1,697,280 -> 908,800, **-46%** | 46% -> 80% |
| Last Exile, Mode X | 100 KB/s | 40 KB/s | 20.24 -> **23.01** | 2,343,424 -> 1,018,880, **-57%** | 74% -> 98% |
| Gas Gas Gas, VGA4 | 60 KB/s | 35 KB/s | 26.05 -> **26.60** | 1,418,752 -> 908,288, **-36%** | 66% -> 86% |
| Gas Gas Gas, VGA4 | 60 KB/s | 25 KB/s | 26.05 -> 25.23 | 1,418,752 -> 720,384, **-49%** | 66% -> **65%** |

- **This is the trade the brief asked for, and it holds on VGA.**
  - On the 286-fast, Mode X at 40 KB/s decodes at a mean of 25% of the
    machine, the same as arm A at 100 KB/s. The file is 57% smaller and
    2.8 dB better.
  - VGA4 at 25 KB/s costs what arm A cost and is half the file, 0.8 dB
    short.
  - LIN320 spends more CPU (24% of the 286-fast against 14%) for a file 46%
    smaller and 6.4 dB better. A budget between the two would land on
    equal CPU.
- **One bit has no such row.** Its gain comes from spending the CPU, and
  section 3's `--avg 0.3` row shows what is left when it may not.
- The `-57%` and `-46%` rows are also the cases where arm B looks clearly
  better. A budget chosen for equal quality would save more again; these
  are budgets chosen in advance, not searched for.

## 5. What it would take to play one

Nothing here changes the player, and these items are not built:

1. **One decoder per surface, as a PART.** VIDEO-CODECS section 7 shows
   the player at 59,610 of 65,024 bytes.
   - A block decoder belongs in a code part fetched at launch for the
     surface it plays.
   - The part is movable while not in use, like every claim, and kept from
     moving only during a play.
   - LIN320 and Mode X share the three Video-1 kinds and want one part
     between them. VGA4's PFILL and TWO want another.
2. **A bench before a ship.**
   - `blk_cost`'s prices drive every choice the encoder made here.
   - The first decoder is measured against them on the 286 (QEMU `-icount`,
     PERFORMANCE.md's composer-bench method, read on the second run). Those
     prices are only an estimate until that is done.
   - Where they are wrong, the encoder is re-priced, and these tables are
     re-taken as a new report.
3. **A soak row** in `tests/vidplay.py`'s shape: a version 2 file played
   and held byte for byte to `os88vid.py`'s decode.
4. **The one-bit codebook last, if at all.** The way to change its trade is
   a cheaper tile: a fixed codebook, or a smaller tile, would save the copy
   in `blk_cost`'s tile price. That is a new measurement, not this one.
