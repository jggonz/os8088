# VIDBENCH on the owner's real 286: a 16 MHz AMD 286 with a PVGA1A

**A measurement, not a description.** Taken 2026-10-08 by the owner on the
same real 286 as docs/reports/VIDDISK-XMS-286-2026-10-08.md: an **AMD 286 at
16 MHz** with a **Paradise PVGA1A on the motherboard** (a chip that can run
8- or 16-bit; on a 286 board taken to be 16), off the floppy `make vid286`
builds on branch `video-xms` at `475e412` - VIDBENCH with the synthetic frames
(`os88vid.py synthxdv`) the 86Box 286 and 486 profiles were fitted from.
Every frame checked OK on both paths. True of that machine; not maintained.
No sound card that answers today (a PicoMEM v1, whose `pminit` has never
worked), so VIDSND was not run.

## The machine against 86Box's mr286 (`286-vga`)

8,000 bytes a row:

| row | this 286 | 86Box mr286 | |
|---|---|---|---|
| `rep movsw` to RAM | **2.64 ms** | 1.05 ms | **2.5x**: the board's RAM has wait states 86Box does not model |
| `rep movsb` to RAM | 5.28 | 2.10 | 2.5x |
| `rep movsw` to the screen | 9.17 | 7.32 | 1.25x |
| `rep movsb` to the screen | 9.79 | 8.36 | 1.17x |
| a READ, screen to RAM | 10.22 | 8.36 | 1.22x |
| the latch copy | 17.06 | 14.63 | 1.17x |

Frames, the player's decoder to the screen ("nat scr"):

| frame | this 286 | mr286 | |
|---|---|---|---|
| SYNTH 7 (slices of 16) | 3,233 us | 2,607 | 1.24x |
| SL40 x60 | 3,076 | 2,377 | 1.29x |
| RUN16 x150 | 2,972 | 2,663 | 1.12x |
| RUN40 x60 | 2,739 | 2,380 | 1.15x |
| P4 x300 | 1,901 | 1,516 | 1.25x |
| P6 x200 | 1,808 | 1,427 | 1.27x |
| P2 x400 | 1,459 | 1,200 | 1.22x |
| P1 x400 | 1,014 | 900 | 1.13x |

**12-29% slower than the profile every 286 encode has been made for.** The
worst frame was 96 per mille of a 30 fps period (77 at 23.976).

## The profile: `286-pvga`

`tools/os88venc.py`'s `CYC_US_286PVGA`, fitted to the twelve synthetic
frames as SPEC.md 98.2.3.3 fits them (the segment held at the 8088 model's
ratio to a P1, P5 between P4 and P6, and here the frame's own cost held at
the empty frame's): **every frame priced back within 0.61%** (tests/videnc.py
5f carries the rows). A slice byte is **1.227 us** against the mr286's 0.914,
a run byte **1.065** against 0.900. The latch row is 2.133 us a byte, and
the disk is VIDDISK's from the same day: 0.9 x 486.3 KB/s = 448,000 B/s, its
curve the other rows.

## What it changes: the lossless Last Exile on THIS machine

`05-320FS.V88` (lossless Mode X, 363 KB/s) priced at this machine's decode:
**median 47% of a frame period, p90 79%, worst 95%** - and the disk costs
~0.85-1.0 ms of CPU a KB besides. `tools/os88vidbuf.py` with its XMS copy
(0.345 ms/KB, the earlier report) and a 64 KB drive cache - stall seconds in
the 93 s play, whatever the disk's speed (the CPU binds every cell):

| disk CPU | ring alone | 1 MB bank | 2 MB | 3 MB |
|---|---|---|---|---|
| 0.85 ms/KB | 1.1 s | 0.4 s | **0.2 s** | 0.3 s |
| 1.0 ms/KB | 2.2 s | 1.2 s | 0.9 s | 0.6 s |

**This corrects the earlier report's conclusion**, which priced the decode at
the mr286 and said so: there, a 2-3 MB bank made the play clean; here it
takes the stalls down three to five times and does not remove them. The
clip is heavier than this CPU. A bank cannot make a frame cheaper, only move
the disk's CPU out of the frames that need it - and past ~2 MB the copies
start to cost back what the bank saves (0.2 s at 2 MB, 0.3 at 3).

The way to a clean play on this machine is an encode **budgeted for
`286-pvga`**, whose CPU bucket keeps every frame inside this machine's
period - and then a bank behind it, which is what lets that encode keep a
disk rate (and so a quality) the ring alone could not.

## VIDBENCH.TXT
```
VIDBENCH - a video frame, decoded three ways (VIDEO-PLAN W0)
-- the picture: each frame onto black, three ways --
SYNTH 7 check           OK
SYNTH 1 check           OK
SYNTH 19 check          OK
SYNTH 6 check           OK
S empty check           OK
S P1 x400 check         OK
S P1 sparse check       OK
S P2 x400 check         OK
S P3 x300 check         OK
S P4 x300 check         OK
S P6 x200 check         OK
S SL16 x150 check       OK
S SL40 x60 check        OK
S RUN16x150 check       OK
S RUN40 x60 check       OK
S P1 row check          OK
-- the bracket: the mode the player takes --
-- (d) raw: 8000 bytes, rep movsb / rep movsw --
movsb 8000 to screen        8     93489     9794.14 us
movsw 8000 to screen        8     87549     9171.85 us
movsb 8000 to RAM           8     50383     5278.24 us
movsw 8000 to RAM           8     25209     2640.95 us
movsb 8000 screen->RAM      8     97510    10215.39 us
movsb 8000 to VGA 12h       8     93491     9794.35 us
latch copy 8000 (VGA)       8    162883    17064.03 us
-- (a) per frame: XDC and ours to the screen, ours to RAM --
SYNTH 7 XDC scr             4     14441     3025.75 us
SYNTH 7 nat scr             4     15430     3232.97 us
SYNTH 7 nat ram             4      6562     1374.90 us
SYNTH 1 XDC scr             4      2867      600.70 us
SYNTH 1 nat scr             4      3793      794.72 us
SYNTH 1 nat ram             4      2446      512.49 us
SYNTH 19 XDC scr            4     14435     3024.49 us
SYNTH 19 nat scr            4     15371     3220.60 us
SYNTH 19 nat ram            4      5370     1125.14 us
SYNTH 6 XDC scr             4      7039     1474.84 us
SYNTH 6 nat scr             4      8724     1827.89 us
SYNTH 6 nat ram             4      3949      827.41 us
S empty XDC scr             4        34        7.12 us
S empty nat scr             4       121       25.35 us
S empty nat ram             4       122       25.56 us
S P1 x400 XDC scr           4      2861      599.45 us
S P1 x400 nat scr           4      4840     1014.10 us
S P1 x400 nat ram           4      3457      724.32 us
S P1 sparse XDC scr         4       216       45.25 us
S P1 sparse nat scr         4       484      101.41 us
S P1 sparse nat ram         4       393       82.34 us
S P2 x400 XDC scr           4      4941     1035.26 us
S P2 x400 nat scr           4      6961     1458.50 us
S P2 x400 nat ram           4      3713      777.96 us
S P3 x300 XDC scr           4      5685     1191.14 us
S P3 x300 nat scr           4      7236     1516.12 us
S P3 x300 nat ram           4      3734      782.36 us
S P4 x300 XDC scr           4      7163     1500.82 us
S P4 x300 nat scr           4      9074     1901.22 us
S P4 x300 nat ram           4      4295      899.90 us
S P6 x200 XDC scr           4      7065     1480.29 us
S P6 x200 nat scr           4      8631     1808.41 us
S P6 x200 nat ram           4      3858      808.34 us
S SL16 x150 XDC scr         4     14434     3024.28 us
S SL16 x150 nat scr         4     15413     3229.40 us
S SL16 x150 nat ram         4      6701     1404.02 us
S SL40 x60 XDC scr          4     14208     2976.93 us
S SL40 x60 nat scr          4     14679     3075.61 us
S SL40 x60 nat ram          4      4601      964.02 us
S RUN16x150 XDC scr         4     12798     2681.50 us
S RUN16x150 nat scr         4     14183     2971.69 us
S RUN16x150 nat ram         4      3933      824.06 us
S RUN40 x60 XDC scr         4     12479     2614.66 us
S RUN40 x60 nat scr         4     13073     2739.12 us
S RUN40 x60 nat ram         4      2120      444.19 us
S P1 row XDC scr            4       738      154.62 us
S P1 row nat scr            4      1309      274.26 us
S P1 row nat ram            4       938      196.53 us
bracket mode (FSXM)             3
adapter kind (VID)              0
-- the worst frame, per mille of a frame period --
XDC scr, 30 fps                90
  ...at 23.976 fps             72
nat scr, 30 fps                96
  ...at 23.976 fps             77

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         110
pointer x span                  0
pointer y span                  0
pointer x at start            270
pointer y at start            188
pointer x at end              270
pointer y at end              188
```
