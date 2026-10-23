# Video codecs, round two: sixteen colours, codebooks, the 5150's disk, Video-1 files, and the player's segment

**A measurement, not a description.** Taken 2026-10-20 on branch
`video-compression` (cut from `elendilon-next` at `5729d8b`), on the same
4-core Xeon container, ffmpeg 6.1.1 and the owner's source clips as
docs/reports/VIDEO-COMPRESSION-2026-10-20.md. It uses `tools/os88vcomp.py` as
committed beside this file, which adds `budget1` and `budgetc` to that
report's tool. Host-side only; no emulator ran. True of that tree, and not
maintained against later trees.

**The owner's questions** (2026-10-20, after the first report):
1. Investigate the 16-colour option.
2. Investigate the Cinepak-like ideas.
3. Make the block codebook efficient. The 5150 seems almost always
   disk-limited, so the codebook could trade well there. Check it on the
   `5150-st225` profile, where the disk cuts rather than the CPU.
4. If we implemented Video-1 itself, would it play real Video-1 files? Is
   there an advantage to our own variant?
5. The player is near its segment limit. What would it take to load only
   the codec that is playing, through parts?

**What is measured and what is estimated** is as in the first report:
- Today's format is the real writer and `cycles_of`.
- A new coder's cycles are an ESTIMATE from that report's section 8 loops.
- Every score is PSNR at three blur radii.

What is new here is a BUDGET MODEL (section 6). It reduces the encoder to
the two things a coder changes, bytes and cycles, under the profile's
buckets. Its lists-only rows are checked against the real encoder.

## 0. The answers

| question | answer |
|---|---|
| **16 colours** | **A DITHERED FILL**, two colours and a mix level through a Bayer pattern (2.5 bytes for 8 x 4 pixels), gives today's lossless look at **46-55% of the bytes** on both anime clips, at 71-117% of an 8088 (section 3). It is Video-1's flat fill with its one failure fixed |
| **Cinepak-like codebooks** | **One bit: yes, at 16 x 4 tiles** (section 2). **Sixteen colours: as good as the fill but dearer** (section 3). **256 colours: no**, 4-8 dB under Video-1 (section 4) |
| **The codebook on the 5150** | **At `5150-st225`'s own disk it buys ~0-1 dB**, because on these clips the CPU ceiling binds wherever the disk does (section 1). **For a smaller file it buys 3-8 dB**: the same look at about **two-thirds of the bytes**, inside the 85% ceiling. It is made efficient by **16 x 4 tiles** (~32 cycles a screen byte, ESTIMATED, against today's ~35 per changed byte) and by **choosing each tile on the scarcer budget**, as the encoder does |
| **Real Video-1 files** | **The block grammar is compatible, the files are not**: AVI, bottom-up blocks, 16-bit colour in most, and no budget for the machine. **Compatibility belongs in a host-side import** (`os88vid import`'s job), which can convert an 8-bit file block for block with nothing lost. **Our own variant is better than adopting Video-1**: it mixes block modes with today's entries, adds the dithered fill, and suits Mode X and VGA4's layouts (section 5) |
| **The player's segment** | **59,610 of 65,024 bytes; 5,414 spare.** There is ONE decoder for every adapter, so "load only the playing codec" means moving the few self-contained format-specific families out: the speaker sound (2,715), C160 (1,565) and write mode 3 (1,088). That is **~4.8 KB net, PiXEL's shape** (lazy compressed code parts with a far vector), fetched at launch, movable while not in use like every claim, and kept from moving only during a play. **New coders should be born as parts** (section 7) |

**Every "same look at fewer bytes" figure is the budget MODEL's, not the
real encoder's** (section 6). The model is close where the disk binds,
which is where those figures were taken. The real encoder's burst
handling is the one thing it lacks.

## 1. On the 5150 the CPU ceiling cuts as often as the disk

Today's encoder at `--profile 5150-st225` with its PCM8 11 kHz sound, on
15 s of each clip. The counts are the encoder's own report ("cut by the
disk ..., the per-frame ceiling ..."). The scores are against a lossless
file of the same clip with the same sound settings, so both files sample
the source at the same frames. Without that, a 30.00 against 29.96 fps
mismatch drifts by a frame and reads as 10 dB of error.

| clip, preset | lossless needs | 5150-st225 gives | frames cut: disk | frames cut: ceiling | seen |
|---|---|---|---|---|---|
| Take On Me, `cga` | 74.0 KB/s, 40.8% | 72.9 KB/s, 40.2% | 0 | 39 | 33.5 dB |
| Star Wars, `cga` | 41.3 KB/s, 25.2% | 41.3 KB/s, 25.1% | 0 | 4 | 40.3 dB |
| Bad Carrot, `herc-full` | 56.3 KB/s, 33.3% | 55.9 KB/s, 33.1% | 0 | 10 | 28.0 dB |
| Star Wars, `herc-full` | 79.5 KB/s, 48.3% | 75.3 KB/s, 46.1% | 19 | 64 | 30.3 dB |
| **Take On Me, `herc-full`** | **145.7 KB/s, 80.3%** | 101.5 KB/s, 57.5% | **131** | 62 | **23.8 dB** |

- **At 116 KB/s these clips mostly fit.** Only full-screen Hercules live
  action outruns the disk, and it outruns the CPU in the same frames.
- **The average CPU share is not the binding one.** `5150-st225`'s
  `--avg` is AUTO, which is its per-frame ceiling (SPEC.md 98.2.1.5), so
  no frame was cut "by the CPU's average". The 50% in the profile table is
  not what a play is held to.
- **So a coder that saves bytes by spending cycles helps the 5150 only
  where the disk binds and the ceiling does not.** That means a smaller
  file made on purpose (section 2), a floppy or a slower disk.

## 2. One bit: the codebook under the 5150's budget

`os88vcomp.py budget1`, against a lossless file with the sound, at the
`5150-st225` budget: 106,500 B/s for the picture (116 KB/s less PCM8 at
11 kHz and padding), a 288 KB bucket (the disk's 96 KB and the 192 KB
reserve), 85% a frame. A codebook is 256 tiles, one every 2 s, its bytes
taken from the stream.

**At the profile's own disk, it buys nearly nothing:**

| clip | today (model) | 8 x 4 codebook | 16 x 4 codebook |
|---|---|---|---|
| Take On Me, `cga` | 72.6 KB/s, 40.0%, 24.0 dB | never chosen | 67.9 KB/s, 39.8%, 24.6 dB |
| Star Wars, `cga` | 40.5 KB/s, 24.7%, 37.5 dB | never chosen | 37.0 KB/s, 24.5%, 37.3 dB |
| Bad Carrot, `herc-full` | 55.1 KB/s, 32.6%, 19.8 dB | never chosen | 36.2 KB/s, 30.9%, 20.6 dB |
| Star Wars, `herc-full` | 74.5 KB/s, 45.4%, 29.7 dB | never chosen | 66.6 KB/s, 45.3%, 29.6 dB |
| Take On Me, `herc-full` | 118.0 KB/s, 65.1%, 19.0 dB | 3 tiles a frame | 116.9 KB/s, 71.0%, 20.0 dB |

**For a SMALLER file, `--disk` turned down on purpose, it buys a great
deal:**

| clip | spent | today (model) | 8 x 4 codebook | 16 x 4 codebook |
|---|---|---|---|---|
| Take On Me, `cga` | 62.8 KB/s | 21.3 dB, 34.7% | 23.2 dB, 44.7% | **24.0 dB, 38.9%** |
| Take On Me, `cga` | 43.2 KB/s | 17.2 dB, 24.1% | 21.3 dB, 42.1% | **21.5 dB, 34.5%** |
| Take On Me, `herc-full` | 62.8 KB/s | 15.1 dB, 34.9% | 18.0 dB, 69.1% | **18.7 dB, 55.3%** |
| Take On Me, `herc-full` | 43.2 KB/s | 12.9 dB, 24.3% | 16.8 dB, 59.4% | **17.5 dB, 47.4%** |
| Bad Carrot, `herc-full` | 36-39 KB/s | 12.2 dB, 23.2% | 17.4 dB, 36.4% | **20.6 dB, 30.9%** |

(Star Wars at `cga` needs only 40.5 KB/s, so neither smaller budget cut it.
On a 15 s clip the half-full 288 KB bucket lets a `--disk 30000` encode
spend ~43 KB/s; a long clip spends what it is given.)

- **The same look at about two-thirds of the bytes.** Take On Me's codebook
  at 43.2 KB/s matches today's at 62.8. Bad Carrot's at 36.2 KB/s beats
  today's at 55.1.
- **16 x 4 tiles, two bytes wide, are the shape.** They score best and
  spend LESS of the 8088 than 8 x 4. An index puts 8 screen bytes up for
  ~260 cycles (word stores, `mov ax, [bx+CBn]` / `mov [es:di+d], ax` a row),
  which is ~32 cycles a byte against today's ~35 per changed byte
  (`cycles_of`, calibrated per clip). A tile is CPU-neutral once most of it
  changed, and the encoder can choose it only then.
- **8 x 4 tiles are never chosen once CPU is priced.** Their ~225 cycles
  for 4 bytes lose to today's entries whenever the ceiling is the scarcer
  budget; the first report priced them by bytes alone.
- **What makes it efficient is the CHOICE, not the loop.** Ranking every
  candidate by the share of the SCARCER budget it spends (the encoder's
  rule, SPEC.md 98.2.1) is what keeps the CPU inside the ceiling. A
  codebook chosen by bytes alone spent its CPU first and left disk unspent.

## 3. Sixteen colours: a dithered fill, and a codebook

`os88vcomp.py budgetc` at `286-fast`'s CPU (225% average, 375% a frame).
It scores against the tone-matched source, as the first report did.

The modes:
- **pfill**, a DITHERED fill: two of the sixteen and a mix level (0-16),
  drawn through a screen-anchored 4 x 4 Bayer pattern. 2.5 bytes a block
  of 8 x 4 pixels. Decoded in write mode 3 as a 2-colour block whose masks
  come from a 17-entry table instead of the stream, ~400 cycles.
- **two**: Video-1's 2-colour block, 5.75 bytes.
- **cb**, a CODEBOOK: the interval's 256 most frequent changed blocks,
  16 bytes each (four planes by four rows), sent every 2 s. An index is
  one byte and its skip, ~450 cycles (four Map Mask `OUT`s, sixteen
  stores).

**Gas Gas Gas**, 640 x 360, 30 fps. Today's lossless file is 149.3 KB/s and
scores **32.1** at 4 x 4, 28.2 at 2 x 2:

| disk | modes | spent | 8088 | plain | 2 x 2 | 4 x 4 | blocks taken |
|---|---|---|---|---|---|---|---|
| 100,000 | today (model) | 88.8 KB/s | 71% | 19.1 | 24.9 | 26.6 | lists 100% |
| 100,000 | pfill | 82.6 | 114% | 19.3 | **28.2** | **31.6** | lists 63%, pfill 37% |
| 100,000 | pfill + two | 84.6 | 117% | 19.7 | 28.5 | 31.4 | |
| 100,000 | cb | 82.3 | 143% | 20.8 | 29.1 | 31.2 | lists 51%, cb 49% |
| 100,000 | pfill + two + cb | 85.3 | 158% | 20.3 | 29.3 | 31.6 | |
| 50,000 | today (model) | 48.0 | 39% | 17.5 | 21.0 | 21.9 | |
| 50,000 | pfill | 47.3 | 87% | 18.6 | **26.5** | **29.1** | lists 46%, pfill 54% |
| 50,000 | pfill + two | 47.6 | 88% | 18.8 | 26.3 | 28.6 | |
| 50,000 | cb | 47.3 | 127% | 20.6 | 27.9 | 29.3 | lists 26%, cb 74% |
| 50,000 | pfill + two + cb | 47.7 | 135% | 20.3 | 27.8 | 29.2 | |

**Cowboy Bebop**, 320 x 240, 24 fps. Today's lossless file is 99.5 KB/s
and scores **29.3** at 4 x 4, 23.4 at 2 x 2:

| disk | modes | spent | 8088 | plain | 2 x 2 | 4 x 4 |
|---|---|---|---|---|---|---|
| 60,000 | today (model) | 51.6 KB/s | 50% | 12.1 | 15.3 | 16.4 |
| 60,000 | pfill + two | 45.7 | 71% | 13.7 | **23.0** | **27.8** |
| 60,000 | cb | 44.6 | 77% | 14.8 | 23.0 | 27.0 |
| 60,000 | pfill + two + cb | 44.0 | 79% | 14.6 | 23.5 | 27.8 |
| 30,000 | today (model) | 27.9 | 27% | 10.2 | 12.2 | 13.1 |
| 30,000 | pfill + two | 27.5 | 53% | 12.7 | 19.9 | 22.6 |
| 30,000 | cb | 26.2 | 59% | 14.1 | 20.1 | 22.4 |

- **The dithered fill is the 16-colour answer.** At 83 KB/s Gas Gas Gas
  scores what today's LOSSLESS file scores at 149 (2 x 2 equal, 4 x 4 0.5
  dB under). At 46 KB/s Bebop is within 1.5 dB of its lossless file at
  99.5. **That is the same look at 55% and 46% of the bytes**, at a decode
  of 71-117% of an 8088, inside `286-fast`'s 225%.
- **It is cheap because it fixes Video-1's one failure.** The first
  report's VGA4 rows posterised because a flat fill has no in-between
  colours. The mix level puts them back, and a block is 2.5 bytes for 32
  pixels.
- **The codebook does about as well, at more CPU and more bytes of its
  own.** It wins the PLAIN reading by 1-1.5 dB, because it reproduces
  today's own dither patterns exactly. It costs 2 KB/s of codebook and
  ~30% more decode. **Cinepak's idea works on sixteen colours, but the
  fill gets the same picture for less.**
- **Today's entries still carry 40-60% of the blocks**, the detailed
  ones. A block mode is an addition to the lists, not a replacement for
  them.
- The model's lists-only row at 100,000 (88.8 KB/s, 26.6 at 4 x 4) agrees
  with the real encoder's `--disk 60000` file (83.6 KB/s, 26.1), so this
  table is in the regime where the model holds (section 6).

## 4. 256 colours: Video-1 against a codebook

The same model on the VGA8 clips. Video-1's three modes are fill, two
and eight; cb is a codebook of 4 x 4 blocks of palette indices, 16 bytes
each.

| clip, disk | modes | spent | 8088 | plain | 2 x 2 | 4 x 4 |
|---|---|---|---|---|---|---|
| Mechwarrior 2, 60,000 | today (model) | 60.5 KB/s | 31% | 22.5 | 23.5 | 24.6 |
| | **Video-1** | 60.5 | 114% | **31.7** | **34.4** | **37.7** |
| | cb | 60.5 | 97% | 27.5 | 29.6 | 31.5 |
| | Video-1 + cb | 60.5 | 146% | 30.5 | 33.2 | 35.9 |
| Mechwarrior 2, 100,000 | today (model) | 96.6 | 49% | 25.6 | 27.1 | 28.4 |
| | **Video-1** | 100.9 | 180% | **32.4** | **35.2** | **38.8** |
| | cb | 95.9 | 121% | 29.0 | 31.8 | 34.1 |
| Last Exile, 100,000 | today (model) | 100.9 | 54% | 16.8 | 17.2 | 17.7 |
| | **Video-1** | 100.9 | 118% | **25.5** | **26.3** | **27.1** |
| | cb | 100.9 | 229% | 22.2 | 23.0 | 23.5 |
| Last Exile, 150,000 | today (model) | 151.4 | 80% | 19.4 | 20.0 | 20.5 |
| | **Video-1** | 151.4 | 177% | **29.1** | **30.8** | **32.5** |
| | cb | 151.4 | 222% | 22.9 | 23.8 | 24.3 |

- **Video-1 is the 256-colour answer, and a codebook is not.** Natural
  256-colour footage makes too many different 4 x 4 blocks for 256 entries
  to cover, and the colour error of a near miss is large. Mixing the
  codebook in made Mechwarrior WORSE, because the ranking takes the cheaper
  index first.
- **Video-1 is what the owner's comparison frames showed**: the same
  picture, made of squares. Section 4 of the first report has it against
  today's lossless file. The codebook loses on both clips, by 4-8 dB.

## 5. Would our Video-1 play real Video-1 files?

**The block grammar, yes; the files as they ship, no.** A real Video-1
file is an AVI (RIFF) file:
- the frames are `00dc` chunks behind a BITMAPINFOHEADER and its palette,
  with palette changes in their own chunks;
- the sound is interleaved PCM, often 16-bit;
- there is an `idx1` index at the end, and keyframes are AVI flags;
- the blocks walk the picture **bottom-up**, because a DIB is stored that
  way.

Two kinds of Video-1 were made:
- **8-bit, palettised.** This maps onto LIN320 directly: its palette goes
  to the DAC, and its FILL, 2-COLOUR and 8-COLOUR blocks are the modes
  section 4 measures.
- **16-bit RGB555.** This is what ffmpeg's own encoder writes, and most
  later files were. A VGA cannot show it. Each block names only 2 to 8
  colours, though, so a 32 KB RGB555-to-palette table makes it one lookup
  per COLOUR, not per pixel: cheap at decode, if the picture is allowed a
  fixed palette.

What a real file does not carry is everything the player is built round:
- sector-aligned super-packets for the reader and its ring;
- a key table to seek on;
- sound carried ahead;
- page flipping;
- the layer;
- above all, a data rate made for the machine. A Video for Windows file
  was made for a 386 reading a 150 KB/s CD-ROM. The player would have to
  learn AVI, read unaligned chunks with no ring plan, and stall wherever
  the file asks more than the machine has.

**So compatibility belongs on the host, as XDC's does.** `os88vid
import` already converts XDC streams (SPEC.md 98.2). An AVI import would
convert an 8-bit Video-1 file block for block into our block records,
with nothing lost: a skip stays a skip, a fill a fill, the rows turned
top-down. The 16-bit kind goes through a palette. The file then plays
with every one of the player's features, at a rate the encoder has
checked against a profile.

**Our own variant has measured advantages, beyond the container:**
- **Mixing.** The budgeted runs in sections 3 and 4 choose per block
  between a block mode and today's own entries. On VGA4, today's entries
  still carry 35-45% of the blocks a frame. A Video-1 stream has no such
  fallback, so on content that is not blocky it has no lossless path at
  all (the first report's VGA4 rows).
- **The dithered fill.** It is what makes sixteen colours work (section
  3), and Video-1 has no such mode.
- **The layouts.** A Mode X block is one byte column of four rows on
  four planes, and its 2-colour row is two plane-mask `OUT`s. A VGA4
  block is a write-mode-3 pair. Video-1's raster order suits neither, and
  the player's addresses are always the surface's own (SPEC.md 98.1.2).
- **The good ideas of its contemporaries.** QuickTime's SMC keeps caches
  of recently used colour pairs, quads and octets, so a 2-colour block
  names its pair in one byte instead of two; that is ~20% off a 2-colour
  block, and nothing here has measured it. Cinepak's codebook is the `cb`
  rows of sections 2-4.

## 6. The budget model, and how far to trust it

`budget1` and `budgetc` reduce today's encoder to what a coder changes:
- a disk bucket and a CPU bucket with the per-frame ceiling;
- every candidate ranked by error fixed per share of the scarcer budget,
  aged 1 + n/8;
- today's entries priced per changed unit, off the lossless file's own
  records and `cycles_of`.

It has none of the real encoder's look-ahead, owed time at a cut
(98.2.1.1), lag search (98.2.1.1.2) or error-as-seen ranking (98.2.1.2).

**Against the real encoder at the same budget** (one bit, `5150-st225`'s
CPU, `--disk N`, silent, scored against the same lossless file):

| clip, budget | real encoder | model, lists only |
|---|---|---|
| Bad Carrot, 40,000 | 12.97 dB | 13.57 dB |
| Bad Carrot, 25,000 | 12.58 dB | 10.28 dB |
| Star Wars, 40,000 | 29.41 dB | 26.27 dB |
| Star Wars, 25,000 | 22.89 dB | 21.08 dB |
| Take On Me, 40,000 | 18.12 dB | 18.47 dB |
| Take On Me, 25,000 | 17.19 dB | 15.45 dB |
| Take On Me, `herc-full`, the profile's own disk | 23.77 dB | 18.98 dB |
| Take On Me, `cga`, the profile's own disk | 33.46 dB | 24.04 dB |

- **Where the DISK binds, the model is close and on the safe side**:
  within -0.6 to +3.8 dB, and the real encoder is usually the better one.
- **Where the per-frame CEILING binds, the model is up to 9 dB
  pessimistic.** That is today's Take On Me at its own disk, cut 39 times by
  the ceiling: the real encoder spends owed time and lag on exactly those
  bursts, and the model cannot.
- **So the gains in sections 2-4 are model against model.** They are
  measured in the regime where the model holds (the disk binding). The real
  encoder's burst handling would serve a block mode as well as it serves
  today's entries, but that is an expectation, not a measurement. The way
  to make it one is section 8's first step: the block modes as constructs
  in `os88venc` itself.

## 7. The player's segment: loading only the codec that plays

Measured on this tree with NASM's symbol map, the listing, and
`tools/stkdepth.py`. The build is `cmp`-identical to `make`'s
`build/video.bin`.

**The segment today:**

| build | image | bss | together | spare to `APP_MAX_SIZE` (65,024) |
|---|---|---|---|---|
| shipped | 57,126 | 2,484 | **59,610** | **5,414** |
| `VPDIAG=1` | 58,666 | 2,664 | 61,330 | 3,694 |
| `NOLIVESND=1` | 56,893 | 2,484 | 59,377 | 5,647 |

The segment holds the image and bss only: no stack lives in it. The player
is in `$(SMALLOMIT)`. `VIDEO.O88` is 43,830 bytes on disk, LZ4, and
`apps360.img` is at 328 of 354 clusters.

**There is no decoder per adapter.** There is ONE (`vdec.inc:33`):
`vd_native` decodes CGA, Hercules, LIN80, LIN320, Mode X and VGA4 planes
and TEXT, because a file's addresses are already its surface's own. What
is exclusive to one kind of play is a short list:

| family | bytes | used only by | self-contained? |
|---|---|---|---|
| `os88spk` + `os88spkfx` + tables: the speaker/Covox sound | **2,715** | a speaker or Covox play | **yes**: CS-relative, calls nothing, 6 package cells; the sample ISR already takes BX = the caller's CS (SPEC.md 34.11.1) |
| `vd_c160` | **1,565** | a C160 file | **yes**: CS-relative jump tables |
| `vd_wmap` (write mode 3) | **1,088** | VGA4 colour leads on a VGA | yes, but for the in-place-XMS yield |
| the flip family | 1,732 | Mode X flipping, page-ahead | no: 15 routines, 41 data cells |
| clip round the OSD | 1,039 | the text up | no |
| bank (XMS/EMS) | 5,813 | XMS and EMS machines | no: 111 data cells |
| the layer | 3,611 | a layered file | no: 117 data cells |

**The shape that fits is PiXEL's** (SPEC.md 106, `apps/pixel/pxpart.inc`).
It has nine lazy, compressed code parts, one per format, each a separate
`.asm` at org 0 with a vector table. The program calls through
`call far [px_pfar]`, and the part reaches the program's bss through
`tools/pxlink.py`. What binds a video codec part, in order:
1. **CS != DS in the decode, and no free segment register.** While a
   record decodes, DS is the record's segment, ES the surface, SS
   `LOW_SEG`. The decoders reach their state through `[cs:]` (127 uses in
   `video.asm`). A codec part must therefore carry every byte it reads
   during a decode. `vd_c160` and `vd_wmap` already do.
2. **The hook's stack: 46 of the 48 bytes** SPEC.md 53.2.2 allows,
   measured from `vp_hook`. A far call costs 2 more a crossing. The C160,
   write-mode-3 and speaker crossings sit on ~30-byte chains, which is
   fine; the flip family as a part would be over.
3. **Fetch at launch, not at Play.** `op_fetch` claims and reads, which a
   hook may not do. On the 360KB set the video is usually on
   `media360.img`, so the disk holding `VIDEO.O88` may be out at Play.
   Fetch in `vp_entry`, choosing by the adapters present, and keep the
   part for the session.
4. **The part's claim is MOVABLE while it is not in use**, as every claim
   a program makes must be (SPEC.md 66): `OSAPI_MEM_MOVABLE` with a
   relocation proc that rewrites the part's far vector. It must not move
   while the hook can be mid-decode inside it, which is exactly the play.
   A play is already a pinned interval: the bracket's `fsx_run` frame pins
   the player's own region for it (SPEC.md 66.6.2). So the part must be
   pinned for the play and movable outside it. How to express "in use" to
   the compactor is the open question: a busy count the proc can refuse
   on, or the play's own pin covering it. (Corrected after the owner's
   review; this said "pinned" first, which breaks the rule every claim
   lives under.)
5. **The cost a frame is negligible**: a direct far call is ~54 cycles
   inbound (PERFORMANCE.md), one or a few a frame.
6. **The parts standard's own code lands in the segment**: 871-1,925
   bytes, measured on stub packages. `OP_LAZY|OP_SELF` rows the player
   reads itself (Clear Skies' shape) cost **104**, plus the player's own
   reader, which it already has (it reads by name and expands LZ4 today).

**Moving the three self-contained families frees 5,368 bytes (measured),
about 4.8 KB net through `OP_SELF`.** That takes the spare from 5,414 to
~10 KB. Disk is likely +1-2 clusters on `apps360.img`.

**The new coders should be born as parts.** A block decoder for one layout
is one self-contained family by construction: CS-relative tables, one
crossing a record. Its estimated size (section 8) is paid only by plays
that use it. The bank and the layer are the large prize after these, but
they need `pxlink` taught to export DATA labels.

## 8. What to build, in order

1. **The block modes in the ENCODER first, behind no format commitment.**
   That means `pfill` (and `two`) for VGA4, Video-1's three for VGA8, and
   the 16 x 4 codebook for one bit, added as constructs in `os88venc`'s
   own selection and in `os88vid`'s writer, reader and `verify`. Encode
   the clips at their profiles and score them against today. This replaces
   section 6's model with the real rate control, look-ahead and owed time,
   and it is host-only.
2. **Bench the decoders before writing the player's.** Run them on
   MartyPC's 5150 for the one-bit tile and its XT VGA for the colour
   blocks, and on the owner's 286 (`VIDBENCH-PVGA286`'s machine). Every
   cycle figure here is an estimate.
3. **Make room before adding code.** Move the speaker sound, C160 and
   write mode 3 into parts (section 7, ~4.8 KB). Then each new block
   decoder goes in as a part of its own: per layout, fetched at launch by
   the adapters present, and paid for only by the plays that use it. Their
   estimated sizes are:
   - ~0.4-0.6 KB for the one-bit tile (both banks' offsets);
   - ~1 KB for VGA4's fill, two and codebook;
   - ~1-1.5 KB for Video-1's modes on each of LIN320 and Mode X.
4. **The format needs a version 2.** All sixteen header flag bits are
   taken (SPEC.md 98.1.1, KPOOL was the last), so a new record kind cannot
   be announced by a flag. A version 2 file is refused by every player
   made before it, which is the behaviour wanted. The new kinds can then
   be new sub-record leads (VGA4 and Mode X already dispatch on the lead;
   98.1.3.1 and 98.1.3.2.4 define 01h-1Fh, and the page sections must be
   checked before one above it is taken) and a list kind for the linear
   layouts.
5. **Value, highest first:**
   1. **VGA4's fill.** Half the VGA demo disk is VGA4, it targets the
      286, where the CPU has room, and it is the largest gain measured.
   2. **VGA8's Video-1.** It is a quality-for-size choice: blocks instead
      of a cut frame's stale streaks.
   3. **The one-bit codebook.** It is for the smaller file and the floppy,
      not for the 5150 at its own disk.

## 9. Re-taking it

```
# the lossless references, with the play's own sound settings
python3 tools/os88venc.py SRC OUT.V88 --preset P --profile lossless \
    --audio pcm8 --rate 11025 --start 30 --end 45
# today's real encoder at the profile, for section 1
python3 tools/os88venc.py SRC OUT.V88 --preset P --profile 5150-st225 \
    --start 30 --end 45
python3 tools/os88vcomp.py budget1 LOSSLESS.V88                  # 5150-st225
python3 tools/os88vcomp.py budget1 LOSSLESS.V88 --disk 30000     # smaller
python3 tools/os88vcomp.py budgetc SRC VGA4_LOSSLESS.V88 --disk 100000 50000 \
    --modes lists pfill pfill+two cb pfill+two+cb
python3 tools/os88vcomp.py budgetc SRC VGA8_LOSSLESS.V88 --disk 100000 60000 \
    --modes lists fill+two+eight cb fill+two+eight+cb
```

The VGA lossless files are the first report's (`--audio none`).
