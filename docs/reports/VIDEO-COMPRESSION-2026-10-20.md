# Video compression: Microsoft Video-1, a tile codebook, and what else would shrink a `.V88`

**A measurement, not a description.** Taken 2026-10-20 on branch
`video-compression`, cut from `elendilon-next` at `5729d8b`, with
`tools/os88vcomp.py` as committed beside this file, on a 4-core Xeon
container (2.1 GHz) with ffmpeg 6.1.1 and numpy 2.5. Host-side only: no
emulator ran. True of that tree's encoder and player; not maintained
against later trees.

**What is measured and what is estimated.** Every byte count of today's
format is its own writer's (`os88vid.spans` and `record` on the real
records), and every 8088 figure for today's format is `cycles_of`, wave
0's model, which fits MartyPC to 0.4% (docs/reports/VIDEO-PROFILE-2026-09-27.md).
**Every 8088 figure for a NEW coder is an ESTIMATE** by instruction count
against the 8088's fetch floor (section 8 shows the loops). None has run
on any machine. Picture quality is PSNR against a reference, at three blur
radii (section 3). The few remarks on how a picture LOOKS come from
comparison frames sent to the owner with this report, which are not
committed because the clips are not ours.

**The owner's question** (2026-10-20): Microsoft Video-1 reportedly
compresses by about 40% and decodes on a 286. Is the method efficient? Could
it apply here? Does the smaller file pay for the decoding, and on which
CPUs? If not, what else would? Lossy is acceptable. The LZ route was tried
before and did not work.

## 0. The answer

| format (who plays it) | Video-1 | what does work | verdict |
|---|---|---|---|
| **VGA8**, 256 colours: LIN320 and Mode X (the 286 files) | **Wins by a wide margin.** At the same bytes it is 10-12 dB better than today's encoder under a budget. It matches today's LOSSLESS file at 28-43% of its bytes, for about the same decode | Video-1 itself, as a new record kind | **build a prototype** |
| **VGA4**, 16 colours on mode 12h (the 286's desktop-colour files) | **Loses at the top.** Two flat colours a block from sixteen posterise: 25 dB at 4 x 4 at any size, where today's lossless file is 32. **It wins at the bottom**: on flat anime it matches today's 53 KB/s file at 7 KB/s | a DITHERED block fill for the bottom of the budget (untested, section 7) | not as it is |
| **one bit**, CGA and Hercules (the XT) | **Cannot help.** A 4 x 4 block's two-colour mask is the very 16 bits a one-bit screen stores | a 256-entry **tile codebook** (Cinepak's idea, not Video-1's). Cartoon content: **-37% with no loss**. Dithered live action: 0-15% at equal look. Both cost **+20 to +30 points of an 8088** | optional; worth it for flat content and floppies |
| every format | | **sound**: Creative's 2- and 2.6-bit ADPCM, decoded by the card. **keys**: the interval. **today's size knob**, which does nothing on VGA4 and Mode X | cheap levers, section 7 |

**Why the "40%" does not transfer.** Video-1's compression is quoted
against RAW frames. Its SKIP block, which leaves a 4 x 4 block as it was,
is what saves most of that. A `.V88` already does the same, at a finer
grain: a byte, not a block (SPEC.md 98.1.3). So the question is never "Video-1
against nothing". It is "Video-1's way of drawing a block that DID change,
against our lists". On a one-bit screen that way is no smaller at all. On a
256-colour screen it is four times smaller per block, and that is where
it wins.

**LZ is confirmed dead a third time** (section 2). LZ4 keeps 84-98% of the
picture bytes and LZB 59-98%, at 50 and 207 cycles an output byte
(docs/plans/VIDEO-PLAN.md, wave 10). Our own decode is ~18 cycles per stream byte.

## 1. What Video-1 is

Microsoft Video 1 ("MSVC", "CRAM") shipped with Video for Windows in
1992, in an 8-bit palettised form and a 15-bit one. Every frame is cut
into 4 x 4 blocks, and each block is one of four things:

| code | bytes (8-bit) | what it draws |
|---|---|---|
| SKIP | 2 for a run of up to 1,023 blocks | nothing: the block is as it was |
| FILL | 2 | one colour |
| 2-COLOUR | 4: a 16-bit mask, two colours | each pixel one of two |
| 8-COLOUR | 10: a mask, a pair for each 2 x 2 quadrant | each quadrant two of its own |

The usual encoder, and this report's, splits a block's pixels about their
mean luminance and takes each side's mean; the quality setting is an
error threshold. It is lossy
always, and there is no raw block. It decodes with no multiplies, no bit
streams and no tables bigger than a byte expansion, which is why a 286
plays it.

## 2. Where today's bytes go

`os88vcomp.py bytes`, over the demo disks (`apps/video/demo/`), as a share
of a stream's PICTURE bytes (its sound excluded):

| file | format | P1..P6 addresses | P1..P6 data | slices | runs | segments and heads | entries that are P1 |
|---|---|---|---|---|---|---|---|
| cga/04-640FS | one bit | 18.0% | 37.3% | 37.3% | 3.5% | 4.0% | 45% |
| herc/03-PCM11 | one bit | 24.7% | 46.2% | 18.9% | 4.1% | 6.1% | 41% |
| vga/04-MECH2 | VGA8 LIN320 | 11.4% | 20.8% | 63.5% | 1.4% | 2.8% | 50% |
| vga/05-LASTX | VGA8 Mode X | 27.1% | 46.0% | 20.3% | 1.4% | 5.1% | 61% |
| vga/01-BCRRT | VGA8 Mode X, 64 greys | 36.3% | 50.3% | 7.3% | 1.1% | 4.9% | 72% |
| vga/07-GAS | VGA4 | **40.6%** | 49.4% | 2.6% | 1.3% | 6.1% | **75%** |

**The picture streams are mostly scattered small changes.** On the
16-colour and Mode X files, three changes in four are a single byte with
its own address, so **36-41% of the picture is addresses**. That is the
cost of a byte-grained skip on a dithered picture whose dither moves. A
block coder pays a few bits per block for the same job. The P1 share is
what decides which files a block coder can help.

**The LZ figures** (picture bytes kept, sampled through every file):

| | LZ4 | LZB | zlib -9 |
|---|---|---|---|
| one bit (12 files) | 84-94% | 73-87% | 69-80% |
| VGA8 and VGA4 (10 files) | 89-98% | 78-98% | 72-88% |
| text and composite (5 files) | 71-99% | 59-97% | 56-90% |

The order-0 entropy is 61-92%, so a static Huffman code could take only
what zlib takes. PiXEL's JPEG Huffman decode measured **184 cycles a pixel**
(SPEC.md 106.19), against our ~18 cycles a stream byte. **Refused,
with these numbers.**

**What is not picture** (`os88vcomp.py overhead`):

| | sound | keys | the rest |
|---|---|---|---|
| CGA and Hercules files, PCM8 11 kHz | 10.8-15.7% | 2.3-6.7% | ~0.8% padding |
| speaker files (02-BWPCS, 02-PCSPK) | 30.1-38.5% | 2.3-2.7% | |
| herc/01-TEXT | **58.7%** | 8.4% | |
| VGA files, PCM8 22 kHz | 2.3-15.4% | 1.6-3.6% | **up to 13.3%**: key parts, the page pool, the layer |

## 3. The experiment

**The clips**: the owner's sources (not committed), 15 seconds from
30 s, silent. Each was encoded with the shipping presets (`os88venc`) at
`--profile lossless`, and then at a ladder of smaller sizes using the knob
that works on that format: `--aim size --worth 2/4/8` for one bit, and
`--profile 286-fast --disk N` for VGA (section 7, item 5, is why).

| clip | preset | canvas | fps |
|---|---|---|---|
| Take On Me | `cga` | 640 x 200, Bayer | 24 |
| Star Wars (StarClip) | `cga` | 640 x 114, Bayer | 30 |
| Bad Carrot | `herc-full --dither threshold` | 720 x 261 | 30 |
| Mechwarrior 2 | `vga8` (LIN320, detail 2x1) | 320 x 200 | 15 |
| Last Exile | `modex` (detail 2x1) | 320 x 180 | 24 |
| Gas Gas Gas | `vga4-full` | 640 x 360 | 30 |
| Cowboy Bebop | `vga4` | 320 x 240 | 24 |

**One bit: the codebook** (`os88vcomp.py tiles`) runs on the LOSSLESS
file's own frames. A changed 8 x 4 tile (one byte by four rows) is sent as
an index into the clip's 256 most frequent changed tiles when the codebook
holds it within *k* bits, priced at one byte and ~1.1 for its skip.
Everything else goes through the real list writer, byte- and
cycle-exact. A tile within *k* bits of the screen is left
alone. The "lists" row at the same *k* is that skip rule with no codebook,
which isolates the codebook's own share. Its score is PSNR against the
lossless file, both blurred 4 x 4 ("seen"). The same score is taken on the
`--worth` encodes, so the tables compare like with like.

**VGA: Video-1** (`os88vcomp.py video1`) runs on the SOURCE, as Video-1
would. It uses the lossless file's own palette and its 4 x 4 blocks; on
VGA4 the blocks are one byte column by four rows. Everything, today's
files included, is scored against that source. The source is first
**aligned** (searched for; Mechwarrior's is one frame off) and
**tone-matched** onto the lossless file, channel by channel. Without that,
Mechwarrior's darker encode cost today's file ~2 dB that had nothing to do
with compression.

**The three readings, and why three**: PLAIN PSNR counts dither noise in
full. 2 x 2 and 4 x 4 box-blurred PSNR are the picture seen from further
back. **The 4 x 4 reading cannot see a 4 x 4 block coder's own edges**,
so it flatters Video-1. **The plain reading counts dither noise as error**,
so it flatters any coder that does not dither. None is quoted alone, and
every claim below says which readings it holds on.

## 4. VGA8: Video-1 against today's file

**Mechwarrior 2**, LIN320, 15 fps, against the tone-matched source:

| | KB/s | 8088 | plain | 2 x 2 | 4 x 4 |
|---|---|---|---|---|---|
| today, lossless | 211.1 | 107% | 31.6 | 35.7 | 38.2 |
| today, no dither (`--vga8-dither 0`), lossless | 185.0 | 106% | 33.2 | 35.9 | 38.1 |
| today, `--disk 120000` | 150.8 | 77% | 28.9 | 31.4 | 33.0 |
| today, `--disk 70000` | 100.6 | 53% | 25.3 | 26.8 | 27.9 |
| today, `--disk 40000` | 60.4 | 33% | 22.5 | 23.6 | 24.6 |
| **Video-1**, t = 50 | 96.4 | 164%* | 34.7 | 36.9 | 39.0 |
| **Video-1**, t = 100 | **60.2** | 109%* | **32.8** | **34.8** | **36.9** |
| **Video-1**, t = 200 | 36.6 | 68%* | 30.3 | 32.0 | 33.9 |
| **Video-1**, t = 400 | 23.0 | 42%* | 28.2 | 29.6 | 31.4 |

\* an estimate, section 8.

**Last Exile**, Mode X, 24 fps:

| | KB/s | 8088 | plain | 2 x 2 | 4 x 4 |
|---|---|---|---|---|---|
| today, lossless | 350.5 | 184% | 29.4 | 33.2 | 34.8 |
| today, `--disk 200000` | 225.5 | 116% | 24.1 | 25.9 | 26.7 |
| today, `--disk 120000` | 158.7 | 85% | 21.2 | 22.7 | 23.5 |
| today, `--disk 120000` + the window's degrade ladder | 156.0 | 68% | 20.8 | 21.4 | 21.8 |
| today, `--disk 70000` | 100.1 | 55% | 17.9 | 19.2 | 19.9 |
| **Video-1**, t = 100 | **150.3** | 199%* | **31.6** | **32.9** | **34.0** |
| **Video-1**, t = 200 | 103.5 | 134%* | 29.9 | 31.0 | 32.1 |
| **Video-1**, t = 400 | 70.8 | 89%* | 27.6 | 28.5 | 29.6 |

What the two tables say:
- **At the same bytes, Video-1 is 10-12 dB better on every reading.** At
  60 KB/s on Mechwarrior that is 32.8 against 22.5 plain. At ~100 KB/s on
  Last Exile it is 29.9 against 17.9.
- **It matches today's lossless file at a third of the bytes.** On
  Mechwarrior, t = 100 is 60 KB/s against 211. It is +1.2 dB plain, -0.9
  at 2 x 2 and -1.3 at 4 x 4. On Last Exile, t = 100 is 150 KB/s against
  351, at +2.2, -0.3 and -0.8.
- **Today's encoder degrades by leaving pixels stale; Video-1 degrades by
  coarsening everywhere.** A budget-cut `.V88` keeps the dither and leaves
  whatever it could not afford wrong for frames, which shows as streaks
  and ghosting behind motion. Video-1 at the same bytes shows 4 x 4
  blocks. Both are visible. By the numbers the blocks are much the
  smaller error, and in the owner's comparison pictures the blocks read as
  the same picture made coarse.
- **Not dithering is not the reason.** Today's file with no dither
  (`--vga8-dither 0`) is 12% smaller and no better once blurred, and its
  budget-cut file (87 KB/s) scores as `--disk 70000` does.
- **The degrade ladder did not rescue it.** It made Last Exile at 156 KB/s
  2 dB worse at 4 x 4: the coarse cells it reaches for are flat, and
  stale.
- **The decode is about the same at the same quality.** It is 109% of an
  8088 against 107% on Mechwarrior, and 199% against 184% on Last Exile.
  Both fit `286-fast`'s 225% average. A block of skips is nearly free; a
  fill is cheaper than the scattered P1s it replaces; a 2- or 8-colour
  block costs about what its sixteen pixels would as slices.

## 5. VGA4: Video-1 loses at the top and wins at the bottom

**Gas Gas Gas**, mode 12h, 640 x 360, 30 fps, against the tone-matched
source:

| | KB/s | 8088 | plain | 2 x 2 | 4 x 4 |
|---|---|---|---|---|---|
| today, lossless | 149.3 | 119% | 19.6 | 28.2 | **32.1** |
| today, `--disk 100000` | 108.5 | 86% | 19.4 | 26.1 | 28.2 |
| today, `--disk 60000` | 83.6 | 66% | 19.2 | 24.6 | 26.1 |
| today, `--disk 35000` | 52.7 | 42% | 18.8 | 22.9 | 24.0 |
| Video-1 style, t = 400 | 302.0 | 563%* | 24.0 | 24.5 | **24.9** |
| Video-1 style, t = 1600 | 43.5 | 77%* | 23.8 | 24.3 | 24.8 |
| Video-1 style, t = 3200 | **6.7** | 14%* | 23.1 | 23.5 | **24.0** |

**Cowboy Bebop**, 320 x 240, 24 fps: today's lossless file is 99.5 KB/s
and scores 14.6 / 23.4 / 29.3. Video-1 style is 18.1 at 4 x 4 at
299 KB/s and 199 KB/s alike.

**Sixteen colours need the dither, and Video-1 has none.** Two of sixteen
fixed colours a block is a posterised picture: skin goes to one grey and
a shadow to black. More bytes do not fix that, so its curve is flat at
24-25 dB. Today's lossless file is 32. Plain PSNR prefers Video-1 by 4 dB
only because it counts the dither as noise, the wrong reading for a
dithered format.

**At the bottom of the budget it is the other way round.** Gas Gas Gas
is flat anime, and a posterised picture that holds still is cheap. At
6.7 KB/s Video-1 scores what today's file scores at 52.7 KB/s, and at
43.5 KB/s it is 0.8-1.4 dB better than today's at 52.7. Today's encoder
under a tight budget keeps the dither and leaves the rest stale, the same
failure as on VGA8. So for VGA4 the measurements point at a block mode
for the bottom of the ladder, given its dither back (section 7), not at
Video-1 as it is.

## 6. One bit: Video-1 cannot help, a codebook can

**Video-1 on a one-bit screen is a no-op.** A 4 x 4 block is 16 bits on
the screen, and Video-1's 2-colour block is a 16-bit mask plus two colours
the screen cannot use. Only its SKIP survives, and a `.V88` already skips
by the byte. **Cinepak's** idea, a codebook of whole small blocks (vector
quantisation), is the one that fits. An 8 x 4 tile, one byte wide, is
four bytes on the screen and one byte as an index.

Against each clip's lossless file, both scored 4 x 4 ("seen"):

**Take On Me** (CGA, Bayer, 24 fps). The codebook holds 66.7% of changed tiles:

| | KB/s | 8088 | seen dB |
|---|---|---|---|
| today, lossless | 74.0 | 40.8% | - |
| today, `--worth 2` | 49.7 | 27.8% | 30.7 |
| today, `--worth 4` | 31.7 | 18.4% | 24.9 |
| codebook, k = 0 (lossless) | 69.9 | 66.4%* | - |
| codebook, k = 1 | 52.3 | 68.9%* | 31.7 |
| codebook, k = 2 | 39.4 | 61.7%* | 28.3 |
| codebook, k = 3 | 30.1 | 52.5%* | 26.1 |

**Star Wars** (CGA, Bayer, 30 fps). The codebook holds 69.4%:

| | KB/s | 8088 | seen dB |
|---|---|---|---|
| today, lossless | 41.3 | 25.2% | - |
| today, `--worth 2` | 31.5 | 19.4% | 32.0 |
| today, `--worth 4` | 20.6 | 12.4% | 26.0 |
| codebook, k = 0 (lossless) | 40.9 | 46.1%* | - |
| codebook, k = 1 | 31.9 | 48.0%* | 32.4 |
| codebook, k = 2 | 24.5 | 43.7%* | 28.3 |
| codebook, k = 3 | 19.2 | 37.3%* | 26.1 |

**Bad Carrot** (Hercules, threshold dither, cartoon, 30 fps). The codebook holds 84.0%:

| | KB/s | 8088 | seen dB |
|---|---|---|---|
| today, lossless | 56.5 | 33.4% | - |
| today, `--worth 2` | 52.9 | 31.5% | 40.0 |
| today, `--worth 4` | 46.2 | 27.8% | 29.2 |
| today, `--worth 8` | 34.9 | 21.1% | 23.4 |
| **codebook, k = 0 (lossless)** | **35.4** | 52.9%* | **-** |
| codebook, k = 1 | 30.9 | 52.7%* | 41.4 |
| codebook, k = 3 | 25.2 | 48.9%* | 32.6 |

- **On flat, cartoon-like content the codebook is a clean win**: Bad
  Carrot is **37% smaller with nothing lost**. That is smaller than today's
  `--worth 8`, which costs 23 dB. Bad Apple, logos and line art are this
  shape.
- **On dithered live action it is close to today's own knob.** Measured
  at equal "seen" error it is 0-8% smaller on Star Wars and 7-15% on Take
  On Me. In the owner's comparison picture, `--worth 4` leaves ghosts behind
  motion and the codebook leaves small speckle. The Bayer dither of a
  moving picture makes too many different tiles for 256 entries.
- **It costs the XT 20-30 points of its machine.** An index tile costs
  ~225 cycles, and it often stands in for one or two P1s at ~48 each. On
  `5150-st225` (50% average, 85% peak) Take On Me goes from 41% to 66%, so
  on busy live action the budget would refuse it. The encoder's own cycle
  model decides that per frame (section 9).
- The codebook is the clip's here, one 1 KB table for 15 seconds. A long
  file needs one per scene or per key, like PALEV's palette a scene. The
  rows above count one 1 KB table; one every 2 s would add ~0.5 KB/s.

## 7. The other levers, ranked

1. **Video-1 blocks for VGA8**: sections 4 and 9. This is the big one.
2. **Sound in the card's own small ADPCM.** A Sound Blaster DSP 2.x
   decodes Creative's **2.6-bit** (DSP 76h/77h, auto-init 7Fh) and
   **2-bit** (16h/17h, auto-init 1Fh) ADPCM as well as the 4-bit one this
   player already uses (SPEC.md 98.1.1.1). That is 3 and 4 samples a byte against
   PCM8's one, at **zero CPU**, since the card does the work. On the
   CGA/Hercules demo files PCM8 sound is 10-16% of the file, so 2-bit
   saves 8-12% outright. On herc/01-TEXT it is 59%, so 2-bit takes the
   file to about 55% of its size. The quality will be worse than 4-bit,
   which is already audibly worse than PCM8 (SPEC.md 98.1.1.1). Field questions:
   what a real card does with them; MartyPC's ADPCM patch carries the
   4-bit tables only. The speaker has no card to decode it, so this does
   nothing for the speaker files.
3. **The key interval.** Keys are 1.6-8.4% of a file at one every 2 s.
   `--keysecs 4` halves that and costs seek granularity. On the VGA4 files
   the key parts and page pool are another 10-12%; worth a look on their
   own.
4. **The one-bit tile codebook**: section 6, for flat content, floppies
   and Live.
5. **A finding about today's knob**: `--aim size --worth N` writes
   **byte-identical records** at every N for VGA4 and Mode X under
   `--profile lossless` (Gas Gas Gas and Last Exile: 0 of 450 and 0 of 360
   records differ). Only the options block changes. So the encoder's
   smallest-file option does nothing for those formats today. That is
   why section 3 used `--disk` for them.

**Untested, but the measurements point at it: a DITHERED fill for
VGA4.** Video-1's failure on sixteen colours is the flat fill. A block of
two colours and a MIX level (one byte), drawn from the patterned dither's
own table, keeps the in-between colours and costs ~2 bytes a block. That
is `--coarse-cut`'s flat cell given a pattern, and it is the shape to try
before calling VGA4 closed.

**Refused, with numbers**:
- LZ4, LZB, zlib (section 2), the third time;
- static Huffman, at PiXEL's 184 cycles a pixel (SPEC.md 106.19);
- addressing changes by **bitmap** instead of skip bytes. One bitmap byte
  for eight positions, then the changed bytes, comes out **+0 to +34%**
  (CGA +12%, Hercules +24%, Mode X +34%, VGA4 -0.4%). Today's skip byte is
  already the cheaper address at these densities;
- Video-1 as it is on VGA4, except at the bottom of the budget (section
  5), and on one bit (section 6).

## 8. What the decode costs, and the loops it was counted from

The 8088 is fetch-bound, so each instruction is priced as the larger of its
execution clocks and 4.34 clocks a byte of code, plus 4 a memory transfer.
CGA's and Hercules' wait states are not in these figures, and a real
Hercules is slower (SPEC.md 88.3.6). **They are estimates, to be
replaced by a bench before anything is built on them.**

**A one-bit tile from the codebook**, Hercules: four rows at +0, +2000h,
+4000h, +6000h, the codebook kept as four 256-byte planes:

```
.t: lodsw                   ; AL = skip, AH = index      16
    mov  bl, al             ;                            ~9 (fetch)
    add  di, bx             ; BH = 0                     ~9
    mov  bl, ah             ;                            ~9
    mov  al, [bx+CB0]       ;                            ~18
    mov  [es:di], al        ;                            ~16
    mov  al, [bx+CB1]
    mov  [es:di+2000h], al  ;                            ~22
    ...                     ; rows 2 and 3 the same
    loop .t                 ;                            17
```

That is **~215-240 cycles a tile**, priced at **225**. A P1 is 47.7
measured. CGA is the same with +0, +2000h, +50h, +2050h.

**A Video-1 2-colour block on LIN320**: four rows of four pixels, each
row's nibble picking a dword mask from a 16-entry table, AND with the
colour difference and XOR with the second colour, two word stores, a
`add di, 316`. **~500 cycles a block**, about 31 a pixel. FILL is ~200 and
8-COLOUR ~700. Today's SLICE is ~13-18 a byte and P1 ~48.

**The same on Mode X** is cheaper, because the plane mask IS the pixel
mask. Per row: `out dx, al` the nibble, store colour A, `xor al, 0Fh`,
`out`, store colour B, `add di, 80`. That is **~380 a 2-colour block,
~120 a fill** (one `out` 0Fh and four stores), and ~620 an 8-colour block.

**VGA4 in write mode 3**: Set/Reset to colour A, four AND-stores of the
mask; Set/Reset to B, four of its complement. ~400 cycles for 32 pixels.

## 9. Does the smaller file pay for its decode, and on which CPU?

The goal here is DISK SPACE, and every profile gets that the same way: an
encode made smaller with `--disk`, `--worth` or a threshold. So "does it
pay" means two things. Does a smaller file at the same look still decode
in its machine's CPU budget? And does a smaller file buy a better picture
where the DISK is what limits it?

| machine (profile) | disk | CPU average / peak | what limits it today | VGA8 Video-1 | one-bit codebook |
|---|---|---|---|---|---|
| 5150, ST-225 (`5150-st225`) | 119 KB/s | 50% / 85% | neither, on these clips (41-74 KB/s) | not its format, except an XT with a VGA: at t = 400, Mechwarrior is 23 KB/s at 42%* and 28-31 dB, where today's file needs 107% | **space for CPU**: -37% on cartoon at +20 points; on live action the budget would refuse it |
| any machine on a floppy (`floppy`) | 15 KB/s | 50% / 85% | the DISK, always | as above | **yes**: more picture through 15 KB/s, and the CPU is idle |
| 286, fast (`286-fast`) | 448 KB/s | 225% / 375% | the CPU (VIDEO-LATCH-PLAN 2: every frame it cut was the ceiling's) | **yes**: a third of the bytes at the same look, at the same decode (109% against 107%; 199% against 184%) | n/a |
| 486 (`486-dx2-66`) | 1.95 MB/s | 650% / 1,000% | neither | yes, and Last Exile at 486 detail is 70.5 MB today (vga/10-LX486) | n/a |
| Live, resident, from XMS | memory | Live's share | MEMORY: the clip must fit | **yes**: a third of the memory is three times the clip | yes, for flat content |

So:
- **The 286 is the case that wants Video-1.** It is CPU-bound, and
  Video-1 shrinks the file without spending CPU.
- **The XT is the case that does not want the codebook on live action.**
  Its CPU is already doing the work, and a 20-30 point rise is what the
  budget refuses.
- **Anything disk- or memory-bound wants both.** That means the floppy
  and Live.

## 10. What a first wave would be

1. **A bench, before any format work.** Put Video-1's FILL, 2-COLOUR and
   8-COLOUR decoders on LIN320 and Mode X into `tests/vidbench` and time
   them on MartyPC's XT VGA and on the owner's 286 (`VIDBENCH-PVGA286`'s
   machine). Section 8's estimates are the hypothesis that bench tests.
2. **A block sub-record for VGA8.** The record's lists stay. A new lead
   (Mode X) or list (LIN320) carries `skip-run | fill | two | eight`
   blocks, so the encoder can use blocks where they win and lists where
   they do not. Nothing in keys, page-ahead, layers or the bank needs to
   know, because a block list only writes screen bytes.
3. **The encoder prices a block like any other construct.** Its spans
   are already priced by `cycles_of` and chosen against two budgets
   (SPEC.md 98.2.1). A block is one more construct with a byte cost, a cycle cost
   and an error. The threshold becomes the budget's lever, not a fixed
   `t`.
4. **Then look at it**: `flicker` and `pace` on the glass (PERFORMANCE.md
   Part 3.1), since a block coder's typical fault is blocks popping in and
   out frame to frame, and nothing in this report can see that.

The one-bit codebook, the ADPCM sound and the dithered VGA4 fill are each
their own small wave, in that order of how much they would buy.

## 11. Re-taking it

```
python3 tools/os88vcomp.py bytes    apps/video/demo/*/*.V88
python3 tools/os88vcomp.py overhead apps/video/demo/*/*.V88
python3 tools/os88vcomp.py tiles    TOM_LOSSLESS.V88 --k 0 1 2 3
python3 tools/os88vcomp.py video1   Mechwarrior2.mp4 MW_LOSSLESS.V88 MW_DISK40.V88 --t 50 100 200 400
```

The lossless files are `os88venc.py SRC OUT --preset P --profile lossless
--audio none --start 30 --end 45` and the budgeted ones
`--profile 286-fast --disk N` (VGA) or `--profile lossless --aim size
--worth N` (one bit). `video1` needs ffmpeg and the source clip, and
`tiles` and `video1` need numpy.
