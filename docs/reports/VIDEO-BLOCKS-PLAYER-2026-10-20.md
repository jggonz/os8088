# Version 2 in the player: the block decoder, what a block costs, and the trade re-taken

**A measurement, not a description.** Taken 2026-10-20 on branch
`video-compression`, on the commit that lands this file (on top of
`847cb177`), on the same 4-core Xeon container, ffmpeg 6.1.1 and the
owner's source clips as docs/reports/VIDEO-BLOCKS-2026-10-20.md. The
machine numbers are MartyPC's (built here, `make marty`), its XT with a VGA
(`os8088_xt_vga`): a 4.77 MHz 8088, cycle-exact. True of that tree, and not
maintained against later trees.

**The owner's brief** (2026-10-20): go for the next steps the previous
report named.
1. A block decoder as a PART, fetched at launch for the screen it plays,
   movable while not in use.
2. Bench it before shipping, because the encoder's choices rest on
   estimated prices.
3. A row that plays a version 2 file and holds it to the host's decode.

All three are done. The one-bit codebook is still not played, as that
report recommended.

## 0. The answer

- **The player plays version 2 on a colour screen, exactly.** LIN320, Mode
  X and VGA4 clips whose block frames hold 150 random blocks of every kind
  decode to the byte, or to the pixel on the glass, at every hold. That
  holds full screen, in the window, round the text box, and on the EGA's
  plane-at-a-time arm.
- **The estimates were LOW, from 1.1x to 3.4x.** Measured on the 8088, a
  block costs 371 to 2,019 cycles. `blk_cost` now charges the measured
  price, and the encoder chooses fewer and cheaper blocks for it.
- **The trade survives the honest price.** At the same disk budget VGA8
  still gains 5 to 8 dB (4x4). Left free, it spends CPU for picture: on the
  286-fast its mean decode goes from 14% of the machine to 36-48%.
- **Held to version 1's own CPU, it is SPACE** (section 4). Mechwarrior
  keeps version 1's picture in a file 45% smaller. Last Exile is 0.3 dB
  short in a file 37% smaller.
- **VGA4 changed least**: its blocks were priced closest to the truth
  (1.1x to 1.2x), so it is still a file 36% smaller at the same quality,
  for about version 1's CPU.

## 1. What was built

- **`apps/video/vblk.asm`**, the part (SPEC.md 98.3.23), 2,316 bytes, 1,485
  packed. It has three vectors: SET, LIN320's three lists, and a planar
  lead's list.
  - One walker over 98.1.10's segment grammar, and a handler per kind.
  - The canvas's row step is PATCHED into the stores' displacements at SET
    (58 sites), so a row costs a `mov [es:di+disp16]` and no add.
  - VGA4 uses write mode 3's two Set/Reset colours on a VGA, and a plane at
    a time on an EGA.
  - Round the text box, every byte goes through a far callback into the
    player (`vp_bcb`, then `vc_put` or `vc_m3b`).
- **`VIDEO.O88`** carries it as part 0, `OP_SEG | OP_COMP | OP_LAZY`.
  - It is fetched in the entry proc when a display is a VGA or an EGA.
  - Its claim is movable on the desktop and pinned for a bracket, with the
    session's claims.
  - `vp_parse` takes version 2 and refuses what 98.1.10 refuses.
  - `vp_decrec` hands LIN320's lists and the planar leads of 20h and up to
    the part; a key decoded through `vp_decboth` has none. In-place XMS
    decode is refused for version 2.
- **The encoder** prices a block at what it measured (`BLK_MEASURED`), and
  says so when `--blocks` is asked of one bit, which no player plays.

## 2. Is it right?

| row | what | result |
|---|---|---|
| `vblkemu` | the part under Unicorn, against a model of the Map Mask, write mode 3 and Set/Reset: random blocks, every kind, three paths, two origins | 168 rounds exact, 5.2 s |
| `vidv2` | LIN320 on MartyPC: 7 holds full screen, 3 frame steps round the box | 0 bytes wrong at each, 22 s |
| `vidv2mx` | the same in Mode X, read off the glass | 0 pixels wrong, 22 s |
| `vidv24` | VGA4: full screen, the box, IN THE WINDOW, and with `[vp_m3gate]` 0 (the EGA's arm on the VGA) | 0 pixels wrong on all four, 34 s |
| `vidv2no` | a CGA XT: nothing fetched; a colour file says *Needs the block decoder*, a one-bit one *A newer .V88 than this player* | both, 15 s |

`vidv2` also reads the part's claim out of the kernel's map: movable (a
relocation proc) after the load, pinned at a hold in the bracket, movable
again after the play.

**Broken on purpose, each goes red where it should:**
- `vblkemu`, eight ways: a quadrant's planes, b's Set/Reset, the EGA's
  `not`, the box's colour, TWO's xor, EIGHT's c0 after a shift, a box
  pixel's x, and the skip's width.
- On MartyPC, four ways:

| mutation | layout | red at |
|---|---|---|
| the skip counted one byte where a block is four | LIN320 | every hold |
| `vp_bcb` writing nothing | Mode X | the box leg and what follows it |
| b's Set/Reset given a's colour | VGA4 | every write mode 3 hold |
| the EGA's `not` dropped | VGA4 | only the no-write-mode-3 arm |

## 3. What it costs

**The package, version 1's frames and the stack:**
- **The image** grows 57,126 -> 58,612 bytes (+1,486): the parts
  standard's table and code are 924, the new routines 446, and the edits in
  place 116. The bss grows +102. 3,826 bytes remain of `APP_MAX_SIZE`.
- **A version 1 frame** pays two compares, and nothing it runs moved by an
  odd amount: `os88rdiff` against `847cb177` finds every hot routine
  identical at an even shift. The exceptions are `vp_decrec`, `vp_decram`
  and `vp_decboth`, which carry the compares.
- **The rate hook's stack:**
  - The deepest block path is a LIN320 or Mode X block round the box. It
    is 40 bytes from `vp_hook`: `vp_frame` 2 and `vp_decrec` 2, the far
    call 4, the walker 2, the handler 2, the callback 4, and `vp_bcb`'s
    chain of 18 (stkdepth).
  - The limit is 48 (SPEC.md 53.2.2), and the play's deepest stays 46,
    through flipping, which version 2 refuses.
  - `vp_bcb` pushes one register for that reason.

**A block, on the 8088** (`vidv2bench`): one kind a frame, 40 and 280
random blocks; the slope is a block's cost and the intercept the call's.

| kind | 40 blocks | 280 blocks | cycles a block | the estimate it replaces | ratio |
|---|---:|---:|---:|---:|---:|
| LIN320 FILL | 19,119 | 131,961 | **470** | 292 | 1.61 |
| LIN320 TWO | 43,890 | 307,108 | **1,097** | 516 | 2.13 |
| LIN320 EIGHT | 80,504 | 565,017 | **2,019** | 600 | 3.36 |
| Mode X FILL | 15,118 | 104,059 | **371** | 196 | 1.89 |
| Mode X TWO | 31,911 | 223,728 | **799** | 548 | 1.46 |
| Mode X EIGHT | 57,870 | 409,526 | **1,465** | 1,012 | 1.45 |
| VGA4 PFILL | 33,898 | 236,411 | **844** | 684 | 1.23 |
| VGA4 TWO | 30,136 | 207,976 | **741** | 684 | 1.08 |

- **Where the estimate went wrong.** It priced a block's stores and forgot
  the walker: on 8088 cycles, a block's address, the indirect call to its
  handler, and the count and width kept in memory come to ~150 a block,
  which is a third of a FILL. LIN320's TWO and EIGHT are table lookups
  through CS, two to four a row. A 16-byte slice is ~370 by comparison.
- **What the encoder does with it.** `blk_cost` keeps its shape, which is
  how a price reaches another profile's table, times the factor that makes
  the 8088's price the measured one.
  - The encoder still adds its per-entry and per-segment share on top. The
    slope already includes the walker's share of that, so the price is
    ~10% HIGH: the safe direction.
  - The 286's own figures are not here, because MartyPC is an 8088. The
    286-fast profile prices a block through its table by the same shape.

## 4. The trade, re-taken at the measured price

The same encodes as VIDEO-BLOCKS-2026-10-20.md section 3 and section 4,
arm B re-run with the measured prices (`--profile 286-fast`, `--audio
none`, 15 seconds from 0:30, scored the same way, 4x4 dB):

| clip | arm | budget | 4x4 | file | 286-fast CPU, mean |
|---|---|---|---:|---:|---:|
| Mechwarrior 2, LIN320 | A | 60 KB/s | 26.62 | 1,697,280 | 14% |
| | B, estimated prices | 60 KB/s | 36.02 | 1,461,760 | 36% |
| | **B, measured** | 60 KB/s | **34.28** | 1,272,832 (-25%) | 48% |
| | **B, measured** | 25 KB/s | **32.12** | 851,968 (-50%) | 36% |
| Last Exile, Mode X | A | 100 KB/s | 20.24 | 2,343,424 | 25% |
| | B, estimated prices | 100 KB/s | 29.24 | 1,870,336 | 42% |
| | **B, measured** | 100 KB/s | **25.44** | 1,547,776 (-34%) | 55% |
| | **B, measured** | 40 KB/s | **21.97** | 918,528 (-61%) | 36% |
| Gas Gas Gas, VGA4 640x360 | A | 60 KB/s | 26.05 | 1,418,752 | 18% |
| | **B, measured** | 60 KB/s | **27.92** | 1,331,200 (-6%) | |
| | **B, measured** | 35 KB/s | **26.26** | 905,216 (-36%) | |
| | **B, measured** | 25 KB/s | 25.15 | 713,728 (-50%) | |
| Bebop, VGA4 320x240 | A | 60 KB/s | 21.97 | 1,256,448 | |
| | **B, measured** | 60 KB/s | **25.20** | 1,233,408 (-2%) | |

The VGA4 rows' CPU, as `cycles_of`'s share of an 8088: Gas 66% (A) against
112%, 87% and 73%; Bebop 72% against 85%.

**At version 1's own CPU** (`--avg` held to arm A's mean on the 286-fast):

| clip | arm | budget | 4x4 | file | 286-fast CPU, mean |
|---|---|---|---:|---:|---:|
| Mechwarrior 2, LIN320 | A | 60 KB/s | 26.62 | 1,697,280 | 14% |
| | **B, `--avg 0.635`** | 25 KB/s | **26.67** | **941,568 (-45%)** | 15% |
| | **B, `--avg 0.635`** | 40 KB/s | **27.01** | 1,248,256 (-26%) | 15% |
| Last Exile, Mode X | A | 100 KB/s | 20.24 | 2,343,424 | 25% |
| | **B, `--avg 1.137`** | 60 KB/s | 19.90 | **1,472,512 (-37%)** | 26% |
| | **B, `--avg 1.137`** | 40 KB/s | 19.41 | 1,034,240 (-56%) | 26% |

`--avg` is in units of a 4.77 MHz 8088 (98.2.1.5), so it is arm A's own
mean in those units: 0.635 and 1.137.

**This is the trade the brief asked for, and it holds at the measured
price.**
- **Mechwarrior** keeps version 1's picture (+0.05 dB) in a file 45%
  smaller, for the CPU version 1 spent.
- **Last Exile** is 0.3 dB short in a file 37% smaller, or 0.8 dB short at
  56%.
- **VGA4's** 25 KB/s row in the table above is half the file of version 1's
  60 KB/s, 0.9 dB short, at about the same CPU (73% of an 8088 against
  66%).
- The extra CPU the unheld rows spend buys picture, not size.

## 5. What this does not show

- **A 286.** MartyPC is an 8088. The owner's 286 is where the VGA demos
  play, and a field run there (`vidv2bench` is the instrument it would
  want) is what turns section 4's CPU column from a model into a
  measurement.
- **A real EGA.** The plane-at-a-time arm ran on MartyPC's VGA, which takes
  write mode 0 a plane at a time as an EGA does, behind `[vp_m3gate]`. No
  emulator here has an EGA.
- **Clustered blocks.** The bench places blocks at random, so most are
  absolute entries carrying an address each. An encoder clusters them into
  skip-coded segments, which costs one byte less a block and a little less
  time. The prices are therefore conservative.
- **Optimisation.** The walker is a third of a FILL, and LIN320's EIGHT
  costs 2,019 where Mode X's costs 1,465. Both are a decoder change for
  later, and each would move `BLK_MEASURED`, which is the reason to
  re-take this bench after it.
