# Version 2 blocks: one bit's player, and the features blocks now combine with

Date 2026-10-20, branch `video-compression`, the working tree on top of
`e4b13e8c` that became the commit after it. Box: a 4-core Intel Xeon at
2.10 GHz in a cloud container. The machines are MartyPC's: the CGA 5150 and
Hercules 5150 (GLaBIOS twins) for one bit, the XT VGA for colour.

This follows docs/reports/VIDEO-BLOCKS-PLAYER-2026-10-20.md, which built the
colour decoder and left one bit's codebook for last. The owner asked for it
*to be complete*, and for every feature that refused blocks to be logged and
as many as possible resolved. SPEC.md 98.3.23.1 is the decoder and SPEC.md
98.1.10.1 the table.

## 1. What a tile costs

`tests/vidv2bench.py --layout cga|herc`: a one-bit clip whose frames each
hold 40 or 200 tiles, the whole codebook's two copies untimed, played full
screen with breakpoints where the player calls the part and where it
returns. The slope through the two densities is a tile:

| walk | CGA 5150 | Hercules 5150 |
|---|---|---|
| the generic walk (`vb_walk`, a handler a block) | 539 cycles | 541 cycles |
| a tile's own walk (`vb_tiles`) | **395 cycles** | (not re-taken; the code is the same) |

The encoder had priced a tile at an ESTIMATE of 264. The generic walk's
indirect call, its count in memory and its width added from memory were
most of the difference, and a walk of its own took 27% off. 395 is
`BLK_MEASURED["tile"]`.

## 2. The trade at that price

The same clips, profile and budgets as the first report's one-bit table
(`--profile 5150-st225`, 15 seconds from 0:30), scored the same way. Arm A
is version 1 and arm B `--blocks on`. B-first is the file the first report
made (priced at 264), re-scored at today's price. B is encoded at today's
price. E is B held to A's CPU with `--avg`.

| clip | arm | KB/s | 8088 | 4x4 dB |
|---|---|---|---|---|
| Take On Me, Hercules, 40 KB/s | A | 34.0 | 22% | 16.77 |
| | B-first | 31.8 | 74% | 19.63 |
| | B | 30.3 | 60% | 19.14 |
| | E (`--avg 0.25`) | 34.2 | 23% | 16.60 |
| Starfield, CGA, 25 KB/s | A | 19.6 | 12% | 18.70 |
| | B-first | 19.8 | 26% | 20.33 |
| | B | 19.7 | 26% | 20.30 |
| | E (`--avg 0.14`) | 16.8 | 10% | 18.79 |

- **The first file's CPU was understated.** Re-scored at the measured
  price, Take On Me's B file needs 74% of the 8088 where the first report
  said 53%. At the measured price the encoder takes fewer tiles: 60%, for
  most of the gain.
- **Held to version 1's CPU, one bit buys little.** Take On Me is the same
  picture at the same size. The starfield is the same picture in a file
  14% smaller.
- So the first report's finding stands with a firmer number. One bit's
  blocks buy quality by spending the CPU, and at equal CPU they buy space
  only on some clips. The colour kinds remain the ones that save space at
  rest.

## 3. What blocks combine with now

Each row below was ENCODED by `tools/os88venc.py --blocks on` and passed
`os88vid verify`. The `tests/vidv2.py --enc` rows also played full screen on
MartyPC, held at five frames and after three steps round the text box,
every pixel against the decode:

| feature | file's blocks | played and held |
|---|---|---|
| page-ahead, Mode X | 34,890 | exact (`vidv2pgmx`) |
| flipped every frame, Mode X | 23,138 | exact (`vidv2flmx`) |
| against its own page, Mode X | 600 | encoded and verified |
| three pages and the page pool, VGA4 320 x 240 | 10,819 | exact (`vidv2pg4`) |
| a seam, LIN320, Repeat on | 33,740 | exact, lap 1 and after the seam (`vidv2lp`) |
| a layer, VGA4 | 11,030 | exact (`vidv2ly4`; the XT does not take the layer) |
| page-ahead, Hercules, one bit | tiles | exact (`vidv2hpa`) |

Block counts are from 6-second encodes of the same test card. A page-ahead
or flipped play on the XT draws its first frames before its hook holds, so
those rows hold from frame 18, as `vidpage`'s do.

Still refused, with the reason the encoder's window shows: Live and
resident (decoded into RAM, which the part does not write), `--bands`,
`--cut tear`, one bit's seam, and the formats with no block kinds (CGA4,
TEXT, C160, C512). In-place XMS plays a version 2 file by copying, and the
encoder now budgets for that play.

## 4. Cost

- `VIDEO.O88`'s image is **58,632 bytes**, 20 more than the colour
  decoder's commit (3,806 short of `APP_MAX_SIZE`).
- The part is **4,741 bytes, 1,760 packed**, up from 2,316 and 1,485. 2,048
  of the bytes are the codebook, which is zeros and packs to almost
  nothing.
- The part is fetched on every machine. On a CGA or a Hercules that is a
  4.7 KB claim for the session that was not there before. It is movable on
  the desktop and pinned only while a play is running.
- The kernel does not change.
