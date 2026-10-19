# The PicoMEM kit on the Victory 286, at 14 and 8 wait states

**A measurement, not a description.** Taken 2026-10-14/15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md: a 16 MHz AMD 286, 4 MB
on the board, an onboard PVGA1A) with its **PicoMEM 1.x** serving C:, off
`make vidpm`'s floppy built on branch `loadall` at `a94f19c` (README
`tests/vidbench/FIELDPM.TXT`). Every bench ran twice on one boot each: with
the ISA bus at **14 wait states**, then at **8** (the BIOS's "Tune", which is
how the machine runs). A third VIDDISK L ran with a module playing through
the card's Sound Blaster, and a fourth VIDDISK X came off a later boot. True
of that machine and those boots and not maintained. The reports are
reproduced whole at the end.

## What moved with the bus, and what did not

| | 14 WS | 8 WS | 8 / 14 |
|---|---|---|---|
| VIDDISK R, the disk idle (KB/s) | 755.1 | **1,119.9** | 1.48 |
| ...with the hook holding 25 / 50 / 75% | 575.9 / 377.5 / 191.9 | 838.3 / 563.1 / 281.5 | 1.46 / 1.49 / 1.47 |
| READ_SEQ 32 KB at 0 MB (us) | 48,060 | 34,329 | 0.71 |
| VIDBENCH, frames to the screen (SYNTH 7, us) | 3,254.6 | 3,043.8 | 0.935 |
| ...the heavy rows (SL16, SL40, RUN16, RUN40) | | | 0.913 - 0.936 |
| ...the latch copy, 8,000 bytes (us) | 17,079 | 15,730 | 0.921 |
| ...every row to RAM | | | **1.000** |
| VIDDISK X, 32 KB XMS copy (us) | 11,320 | 11,285 | **1.00** |
| VIDDISK L, LOADALL 32 KB (us) | 10,989 | 10,988 | **1.00** |
| VIDDISK E, the card's EMS frame -> RAM 16 KB (us) | 25,693 | 25,691 | 1.00 |

- **The card's disk is the bus's to cross, so it moved most: 48%.** The
  line stays exact - the hook's share comes straight off the top at either
  setting - and the 14 WS rows are the 10-12 multi-sector figures to 0.6%.
- **Writing the screen moved 6-9%.** The PVGA1A is on the board but its
  writes pay the bus setting; RAM does not, row for row.
- **The XMS copy did NOT move with the bus**, either transport. So the
  2026-10-14 run's 0.257 ms a KB is not "Tune".
- The card's EMS frame did not move either.
- **`286-picomem1` is remodelled at 8 WS** (`tools/os88venc.py`, SPEC.md's
  profile table): 0.9 x 563.1 KB = **519,000** where it was 350,000, the
  curve from the 8 WS rows, the latch copy 1.966 us a byte, and the decode
  `CYC_US_286PVGA8` - 10-08's table with a P entry x0.946, a slice byte
  x0.932 and a run byte x0.918, a least-squares fit of the two runs'
  synthetic frames relative to each other. ESTIMATED by scaling.

## The XMS copy changes from boot to boot

| | report | 32 KB copy (us) | ms a KB (fitted, 4-32 KB) |
|---|---|---|---|
| 2026-10-14, L | LOADALL-VICTORY286-2026-10-14.md | 8,625 | 0.257 |
| 2026-10-15, 14 WS, X | below | 11,320 | 0.339 |
| 2026-10-15, 8 WS, X | below | 11,285 | 0.338 |
| 2026-10-15, a later boot, 8 WS, X | below | **7,295** | **0.214** |

Same bench, same block size, the same 1 KB-aligned XMS block (XMEM.DRV
allocates at 1 KB) and a paragraph-aligned buffer, so not alignment. On the
10-14 boot the slow and the fast copies read the SAME in-place 32 KB READ
(4,508 us above 1 MB, 4,352 below) as the slow 10-15 boot did, so reading
is not what changes: the copies, which read one place and write another, do.
The owner sees the same thing in play - "good boots and slow boots" on
files encoded past the edge. The hypothesis is placement: where a boot puts
the copy's two ends, against the board's DRAM banks and page mode. VIDDISK
X now reports both addresses and times one 8 KB copy from sixteen places
across 1 MB of XMS (FIELDPM.TXT step 7), which tells placement from state.
Until then `xcopy` stays 0.345, the slow boot's.

## VIDSND works on the PicoMEM

The bench that ended in STACK OVERFLOW on 10-12 ran clean at both settings
with the PicoMEM path (`vs_pmup`): card at 2A0h on IRQ 5, the Sound Blaster
at 220h on IRQ 7, DSP 2.01. PCM8 22 kHz at 735 a block gave 149 and 151
interrupts in 5 s (30 a second), 8 kHz at 134 gave 300 (60): one a frame at
30 and at 60 fps. **4-bit ADPCM gave 0** - the card's DSP does not play
it, so a Video Player file for this card is PCM8. Channel 1 raced and was
masked (4), as SOUND.DRV does.

The 14 WS VDLALL1.TXT stops after its first LOADALL row (no `errors` line),
so its 32 KB rows are the only ones the bus comparison has for L.

## LOADALL with a module playing

VDLALL1.TXT taken with Tracker playing through the card reads the 8 WS run
to the microsecond, every row, zero errors. The masked windows LOADALL
needs (up to ~11 ms for a 32 KB copy) did not disturb the card's sound
enough to show in the timings; whether they were audible is the owner's to
say.

## GasGas24 and the Bad Carrot encodes

`03-640FS.V88` in VGA.7z is `GasGas24.V88`, byte for byte. BC7 is
`286-fast`, `--avg 7 --peak 7`, Mode X 8-bit, **30 fps**, flipped; BCLL is
the `lossless` profile at **25 fps**, flipped.

BCLL is the evidence: it plays on this machine, with three pauses - the
zoom on the fire, the zoom on the sun and the flashing suns - and nowhere
else. Its own records priced by 286-fast's table, flipped (each frame pays
its record and the last one again, 98.3.8), put the sustained overload at
**1:28-1:34, 1:37-1:40 and 3:06-3:10** - up to 16, 20 and 31 frames behind
the sound - which are those three scenes. So the per-frame model is right.

What cut BC7 was the encoder's **flip schedule** (98.2.1.1.1), not the
average and not `--peak`. The schedule caps a flipped frame at what keeps
the NEXT one less than a whole frame behind the sound - about one period
of the machine once it is a call behind - so any `--peak` above that cap
changes nothing, which is what the owner saw between 4 and 7. The summary
blamed "the CPU's average" for those cuts because the attribution compared
the room NET of the back page's debt with the ceiling, and on a flipped
file that debt is never zero: a re-encode of BCLL's frames at 30 fps, peak
7, cut 16 times, every one of them with the 1-second bucket holding over
200 periods and the schedule's ceiling at 0.99 of the machine. Fixed in
`tools/os88venc.py`: the summary now says "the flip schedule N", and the
file it writes is byte-identical.

The schedule's limit is the open question. BCLL, modelled the same way,
falls a whole frame or more behind at 0:14, 0:26, 0:48, 1:01, 2:01 and 3:33
(1 to 1.5 frames) and at 1:17-1:21 and 1:43-1:49 (3.5 to 6.5 frames), all
scenes the owner did not see pause. An allowance - the picture may run N
frames behind and must catch up - is the lever, and those scenes are what
sets N.

## The reports

### 14WS/VIDBENC1.TXT
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
movsb 8000 to screen        8     93559     9801.47 us
movsw 8000 to screen        8     87671     9184.63 us
movsb 8000 to RAM           8     50411     5281.18 us
movsw 8000 to RAM           8     25230     2643.15 us
movsb 8000 screen->RAM      8     97522    10216.64 us
movsb 8000 to VGA 12h       8     93535     9798.96 us
latch copy 8000 (VGA)       8    163030    17079.43 us
-- (a) per frame: XDC and ours to the screen, ours to RAM --
SYNTH 7 XDC scr             4     14427     3022.81 us
SYNTH 7 nat scr             4     15533     3254.55 us
SYNTH 7 nat ram             4      6530     1368.19 us
SYNTH 1 XDC scr             4      2879      603.22 us
SYNTH 1 nat scr             4      3881      813.16 us
SYNTH 1 nat ram             4      2519      527.79 us
SYNTH 19 XDC scr            4     14427     3022.81 us
SYNTH 19 nat scr            4     15299     3205.52 us
SYNTH 19 nat ram            4      5427     1137.09 us
SYNTH 6 XDC scr             4      7059     1479.03 us
SYNTH 6 nat scr             4      7941     1663.83 us
SYNTH 6 nat ram             4      3430      718.67 us
S empty XDC scr             4        37        7.75 us
S empty nat scr             4       204       42.74 us
S empty nat ram             4       202       42.32 us
S P1 x400 XDC scr           4      2905      608.67 us
S P1 x400 nat scr           4      4916     1030.02 us
S P1 x400 nat ram           4      3531      739.83 us
S P1 sparse XDC scr         4       222       46.51 us
S P1 sparse nat scr         4       555      116.28 us
S P1 sparse nat ram         4       459       96.17 us
S P2 x400 XDC scr           4      4958     1038.82 us
S P2 x400 nat scr           4      7062     1479.66 us
S P2 x400 nat ram           4      3794      794.93 us
S P3 x300 XDC scr           4      5658     1185.49 us
S P3 x300 nat scr           4      7649     1602.65 us
S P3 x300 nat ram           4      4044      847.31 us
S P4 x300 XDC scr           4      7139     1495.79 us
S P4 x300 nat scr           4      9263     1940.83 us
S P4 x300 nat ram           4      4317      904.51 us
S P6 x200 XDC scr           4      7046     1476.31 us
S P6 x200 nat scr           4      7931     1661.74 us
S P6 x200 nat ram           4      3421      716.78 us
S SL16 x150 XDC scr         4     14419     3021.14 us
S SL16 x150 nat scr         4     15508     3249.31 us
S SL16 x150 nat ram         4      6778     1420.16 us
S SL40 x60 XDC scr          4     14181     2971.27 us
S SL40 x60 nat scr          4     14720     3084.20 us
S SL40 x60 nat ram          4      4669      978.27 us
S RUN16x150 XDC scr         4     12840     2690.30 us
S RUN16x150 nat scr         4     14262     2988.24 us
S RUN16x150 nat ram         4      4030      844.38 us
S RUN40 x60 XDC scr         4     12507     2620.52 us
S RUN40 x60 nat scr         4     13087     2742.05 us
S RUN40 x60 nat ram         4      2099      439.79 us
S P1 row XDC scr            4       744      155.88 us
S P1 row nat scr            4      1378      288.72 us
S P1 row nat ram            4      1006      210.78 us
bracket mode (FSXM)             3
adapter kind (VID)              0
-- the worst frame, per mille of a frame period --
XDC scr, 30 fps                90
  ...at 23.976 fps             72
nat scr, 30 fps                97
  ...at 23.976 fps             78

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         110
pointer x span                  0
pointer y span                  0
pointer x at start            354
pointer y at start            356
pointer x at end              354
pointer y at end              356
```

### 8WS/VIDBENC1.TXT
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
movsb 8000 to screen        8     87331     9149.01 us
movsw 8000 to screen        8     79884     8368.84 us
movsb 8000 to RAM           8     50410     5281.07 us
movsw 8000 to RAM           8     25228     2642.94 us
movsb 8000 screen->RAM      8     89561     9382.63 us
movsb 8000 to VGA 12h       8     87377     9153.83 us
latch copy 8000 (VGA)       8    150153    15730.40 us
-- (a) per frame: XDC and ours to the screen, ours to RAM --
SYNTH 7 XDC scr             4     13498     2828.16 us
SYNTH 7 nat scr             4     14527     3043.76 us
SYNTH 7 nat ram             4      6525     1367.15 us
SYNTH 1 XDC scr             4      2753      576.82 us
SYNTH 1 nat scr             4      3674      769.79 us
SYNTH 1 nat ram             4      2526      529.26 us
SYNTH 19 XDC scr            4     13496     2827.74 us
SYNTH 19 nat scr            4     14304     2997.04 us
SYNTH 19 nat ram            4      5428     1137.30 us
SYNTH 6 XDC scr             4      6471     1355.83 us
SYNTH 6 nat scr             4      7470     1565.15 us
SYNTH 6 nat ram             4      3436      719.92 us
S empty XDC scr             4        35        7.33 us
S empty nat scr             4       201       42.11 us
S empty nat ram             4       201       42.11 us
S P1 x400 XDC scr           4      2763      578.91 us
S P1 x400 nat scr           4      4762      997.75 us
S P1 x400 nat ram           4      3528      739.20 us
S P1 sparse XDC scr         4       200       41.90 us
S P1 sparse nat scr         4       540      113.14 us
S P1 sparse nat ram         4       462       96.80 us
S P2 x400 XDC scr           4      4732      991.47 us
S P2 x400 nat scr           4      6718     1407.58 us
S P2 x400 nat ram           4      3786      793.26 us
S P3 x300 XDC scr           4      5401     1131.64 us
S P3 x300 nat scr           4      7250     1519.05 us
S P3 x300 nat ram           4      4046      847.73 us
S P4 x300 XDC scr           4      6772     1418.90 us
S P4 x300 nat scr           4      8630     1808.20 us
S P4 x300 nat ram           4      4321      905.35 us
S P6 x200 XDC scr           4      6542     1370.71 us
S P6 x200 nat scr           4      7461     1563.26 us
S P6 x200 nat ram           4      3424      717.41 us
S SL16 x150 XDC scr         4     13459     2819.99 us
S SL16 x150 nat scr         4     14514     3041.04 us
S SL16 x150 nat ram         4      6777     1419.95 us
S SL40 x60 XDC scr          4     13255     2777.25 us
S SL40 x60 nat scr          4     13684     2867.14 us
S SL40 x60 nat ram          4      4666      977.64 us
S RUN16x150 XDC scr         4     11778     2467.78 us
S RUN16x150 nat scr         4     13188     2763.21 us
S RUN16x150 nat ram         4      4027      843.75 us
S RUN40 x60 XDC scr         4     11386     2385.65 us
S RUN40 x60 nat scr         4     11952     2504.24 us
S RUN40 x60 nat ram         4      2093      438.53 us
S P1 row XDC scr            4       714      149.60 us
S P1 row nat scr            4      1339      280.55 us
S P1 row nat ram            4      1007      210.99 us
bracket mode (FSXM)             3
adapter kind (VID)              0
-- the worst frame, per mille of a frame period --
XDC scr, 30 fps                84
  ...at 23.976 fps             67
nat scr, 30 fps                91
  ...at 23.976 fps             72

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         110
pointer x span                  0
pointer y span                  0
pointer x at start            388
pointer y at start            361
pointer x at end              388
pointer y at end              361
```

### 14WS/VIDDISK1.TXT
```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              63
heads                          16
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3    196608    54925.72 us t
READ_AT 32K @3 MB           3    262144    73234.29 us t
READ_AT 32K @6 MB           3    327680    91542.86 us t
READ_AT 32K @9 MB           3    327680    91542.86 us t
READ_AT 32K @12 MB          3    458752   128160.01 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1     65536    54925.72 us t
READ_SEQ 32K @0 MB          8    458752    48060.00 us t
READ_SEQ seek 12MB 1st      1    131072   109851.44 us t
READ_SEQ 32K @12 MB         8    524288    54925.72 us t
READ_SEQ 16K @12 MB         8    262144    27462.86 us t
READ_SEQ 8K @12 MB          8    131072    13731.43 us t
int13 calls, 8 x 32K            9
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%             7551
ceiling, hook 25%            5759
ceiling, hook 50%            3775
ceiling, hook 75%            1919
ceiling, 50% ints off        3798
data at 12 MB           ok
-- int 13h on unit 80h: a whole track, one sector --
int13 one track            40   1966080    41194.29 us t
int13 one sector           60    131072     1830.85 us t
-- int 13h: ONE call across a head (the kernel ends at a track) --
one call across a head  ok - the same bytes
one call across a cyl   ok - the same bytes
kernel shapes read             48
...WRONG BYTES                  0
...refused                      0
sectors timed, each way        80
N sectors, track calls     12    720896    50348.57 us t
N sectors, one call        12    720896    50348.57 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          30
pointer x span                  0
pointer y span                  0
pointer x at start            495
pointer y at start            145
pointer x at end              495
pointer y at end              145
```

### 8WS/VIDDISK1.TXT
```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              63
heads                          16
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3    131072    36617.14 us t
READ_AT 32K @3 MB           3    196608    54925.72 us t
READ_AT 32K @6 MB           3    262144    73234.29 us t
READ_AT 32K @9 MB           3    262144    73234.29 us t
READ_AT 32K @12 MB          3    393216   109851.44 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1     65536    54925.72 us t
READ_SEQ 32K @0 MB          8    327680    34328.57 us t
READ_SEQ seek 12MB 1st      1    131072   109851.44 us t
READ_SEQ 32K @12 MB         8    327680    34328.57 us t
READ_SEQ 16K @12 MB         8    196608    20597.14 us t
READ_SEQ 8K @12 MB          8     65536     6865.71 us t
int13 calls, 8 x 32K            9
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%            11199
ceiling, hook 25%            8383
ceiling, hook 50%            5631
ceiling, hook 75%            2815
ceiling, 50% ints off        5610
data at 12 MB           ok
-- int 13h on unit 80h: a whole track, one sector --
int13 one track            40   1310720    27462.86 us t
int13 one sector           60     65536      915.42 us t
-- int 13h: ONE call across a head (the kernel ends at a track) --
one call across a head  ok - the same bytes
one call across a cyl   ok - the same bytes
kernel shapes read             48
...WRONG BYTES                  0
...refused                      0
sectors timed, each way        80
N sectors, track calls     12    458752    32040.00 us t
N sectors, one call        12    524288    36617.14 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         1
pointer samples taken          60
pointer x span                 36
pointer y span                 26
pointer x at start            257
pointer y at start            189
pointer x at end              293
pointer y at end              163
```

### 14WS/VDWRITE1.TXT
```
VIDDISK W - writing STREAM.DAT, 12.5 MB, for R to read back
the writer              W: WRITE_SEQ, HELD
the stream is on        C:
free on the disk (KB)      405816
to write (KB)               12800
written this run (KB)       12800
write KB/s x 10              2904
write took (s)                 44
STREAM.DAT is whole: press R to run the bench, D to delete it
```

### 14WS/VDXMS1.TXT
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    108054    11320.00 us
XMS down 32K                8    107863    11299.99 us
XMS up 16K                  8     55883     5854.44 us
XMS down 16K                8     56051     5872.04 us
XMS up 8K                   8     30273     3171.47 us
XMS down 8K                 8     30108     3154.18 us
XMS up 4K                   8     17389     1821.71 us
XMS down 4K                 8     17224     1804.42 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               6335
READ_SEQ + copy up           5183
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          46
pointer x span                  0
pointer y span                  0
pointer x at start            495
pointer y at start            145
pointer x at end              495
pointer y at end              145
```

### 8WS/VDXMS1.TXT
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    107719    11284.91 us
XMS down 32K                8    107641    11276.74 us
XMS up 16K                  8     55898     5856.01 us
XMS down 16K                8     55887     5854.86 us
XMS up 8K                   8     29957     3138.37 us
XMS down 8K                 8     30117     3155.13 us
XMS up 4K                   8     17057     1786.93 us
XMS down 4K                 8     17066     1787.87 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8703
READ_SEQ + copy up           6719
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          16
pointer x span                  0
pointer y span                  0
pointer x at start            236
pointer y at start            185
pointer x at end              236
pointer y at end              185
```

### 14WS/VDLALL1.TXT
```
VIDDISK L - LOADALL: extended memory in place (LOADALL-PLAN 8)
CS cache access byte          147
CPU tier (1 286, 2 386)         1
XMS free (KB)                3392
MSW (SMSW)                  65520
IDTR base                       0
IDTR limit                  65535
A20 before any copy     open
LOADALL                 ran
caches, below 1 MB      ok
-- the fill: 64 sectors by the task file straight into XMS --
No drive answering at 1F0h master - the READ_SEQ row only
-- in place against OSAPI_XMEM_COPY, the other transport --
A20 after XMEM_COPY     open
32K read in place       ok
...and 1 KB off         differs, as it must
32K written in place    ok
-- a borrow and the copies, PIT-timed, ints off --
a borrow, no work           8      1611      168.77 us
XMEM_COPY up 32K            8    107877    11301.46 us
XMEM_COPY down 32K          8    107657    11278.41 us
LOADALL up 32K              8    104892    10988.74 us
writes skipped, A20 SHUT        0

```

### 8WS/VDLALL1.TXT
```
VIDDISK L - LOADALL: extended memory in place (LOADALL-PLAN 8)
CS cache access byte          147
CPU tier (1 286, 2 386)         1
XMS free (KB)                3392
MSW (SMSW)                  65520
IDTR base                       0
IDTR limit                  65535
A20 before any copy     open
LOADALL                 ran
caches, below 1 MB      ok
-- the fill: 64 sectors by the task file straight into XMS --
No drive answering at 1F0h master - the READ_SEQ row only
-- in place against OSAPI_XMEM_COPY, the other transport --
A20 after XMEM_COPY     open
32K read in place       ok
...and 1 KB off         differs, as it must
32K written in place    ok
-- a borrow and the copies, PIT-timed, ints off --
a borrow, no work           8      1601      167.72 us
XMEM_COPY up 32K            8    107694    11282.29 us
XMEM_COPY down 32K          8    107539    11266.05 us
LOADALL up 32K              8    104884    10987.91 us
LOADALL down 32K            8    104757    10974.60 us
XMEM_COPY up 8K             8     29956     3138.26 us
XMEM_COPY down 8K           8     30127     3156.17 us
LOADALL up 8K               8     27525     2883.58 us
LOADALL down 8K             8     27399     2870.38 us
XMEM_COPY up 4K             8     17232     1805.26 us
XMEM_COPY down 4K           8     17068     1788.08 us
LOADALL up 4K               8     14636     1533.30 us
LOADALL down 4K             8     14504     1519.47 us
XMEM_COPY up 1K             8      7591      795.25 us
XMEM_COPY down 1K           8      7430      778.38 us
LOADALL up 1K               8      4963      519.93 us
LOADALL down 1K             8      4837      506.73 us
read 32K above 1 MB         8     43029     4507.82 us
read 32K below 1 MB         8     41540     4351.83 us
writes skipped, A20 SHUT        0
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         1
pointer samples taken          54
pointer x span                  1
pointer y span                  0
pointer x at start            236
pointer y at start            185
pointer x at end              235
pointer y at end              185
```

### SNDL/VDLALL1.TXT
```
VIDDISK L - LOADALL: extended memory in place (LOADALL-PLAN 8)
CS cache access byte          147
CPU tier (1 286, 2 386)         1
XMS free (KB)                3392
MSW (SMSW)                  65520
IDTR base                       0
IDTR limit                  65535
A20 before any copy     open
LOADALL                 ran
caches, below 1 MB      ok
-- the fill: 64 sectors by the task file straight into XMS --
No drive answering at 1F0h master - the READ_SEQ row only
-- in place against OSAPI_XMEM_COPY, the other transport --
A20 after XMEM_COPY     open
32K read in place       ok
...and 1 KB off         differs, as it must
32K written in place    ok
-- a borrow and the copies, PIT-timed, ints off --
a borrow, no work           8      1601      167.72 us
XMEM_COPY up 32K            8    107677    11280.51 us
XMEM_COPY down 32K          8    107656    11278.31 us
LOADALL up 32K              8    104884    10987.91 us
LOADALL down 32K            8    104760    10974.91 us
XMEM_COPY up 8K             8     29949     3137.53 us
XMEM_COPY down 8K           8     30128     3156.28 us
LOADALL up 8K               8     27526     2883.69 us
LOADALL down 8K             8     27400     2870.49 us
XMEM_COPY up 4K             8     17053     1786.51 us
XMEM_COPY down 4K           8     17066     1787.87 us
LOADALL up 4K               8     14632     1532.88 us
LOADALL down 4K             8     14505     1519.58 us
XMEM_COPY up 1K             8      7431      778.49 us
XMEM_COPY down 1K           8      7428      778.17 us
LOADALL up 1K               8      4964      520.04 us
LOADALL down 1K             8      4839      506.94 us
read 32K above 1 MB         8     43035     4508.45 us
read 32K below 1 MB         8     41545     4352.35 us
writes skipped, A20 SHUT        0
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          38
pointer x span                  0
pointer y span                  0
pointer x at start            473
pointer y at start             82
pointer x at end              473
pointer y at end               82
```

### 14WS/VDEMS1.TXT
```
VIDDISK E - expanded memory: a Lo-tech-style EMS board
EMS board at            0268h, frame D000h
EMS pages (16 KB)             128
-- the board (us a row; 16 KB copies, rep movsw) --
map 4 pages (4 OUTs)        8       208       21.79 us
frame -> RAM 16K            8    245247    25692.68 us
RAM -> frame 16K            8    177723    18618.70 us
RAM -> RAM 16K              8     26241     2749.07 us
the stream              STREAM.DAT
-- 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ into RAM            6335
READ_SEQ into frame          4747
frame holds the file    ok
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken           8
pointer x span                  0
pointer y span                  0
pointer x at start            202
pointer y at start            220
pointer x at end              202
pointer y at end              220
```

### 8WS/VDEMS1.TXT
```
VIDDISK E - expanded memory: a Lo-tech-style EMS board
EMS board at            0268h, frame D000h
EMS pages (16 KB)             128
-- the board (us a row; 16 KB copies, rep movsw) --
map 4 pages (4 OUTs)        8       190       19.90 us
frame -> RAM 16K            8    245235    25691.43 us
RAM -> frame 16K            8    177709    18617.23 us
RAM -> RAM 16K              8     26228     2747.71 us
the stream              STREAM.DAT
-- 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ into RAM            8703
READ_SEQ into frame          4927
frame holds the file    ok
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken           8
pointer x span                  0
pointer y span                  0
pointer x at start            171
pointer y at start            222
pointer x at end              171
pointer y at end              222
```

### 14WS/VIDSND1.TXT
```
VIDSND - a frame interrupt off the card (VIDEO-PLAN W0 c,e)
PicoMEM base (0 none)         672
...the card's own IRQ           5
DSP base port                 544
IRQ line found                  7
DSP version                   513
-- (c) interrupts in 5 s: PCM8 30 fps, PCM8 60 fps, ADPCM4 --
IRQs PCM8 22k/735             149
IRQs PCM8 8k/134              300
IRQs ADPCM4 22k/368             0
-- (e) tracks read in 5 s, the ISR burning 0/25/50/75% --
tracks, burn 0%               118
tracks, burn 25%              111
tracks, burn 50%              104
tracks, burn 75%               99
tracks, 50% ints on: NOT RUN - a PicoMEM ISR may not sti
sectors per track              63
DMA ch 1 masked (4)             4
flags                          64
```

### 8WS/VIDSND1.TXT
```
VIDSND - a frame interrupt off the card (VIDEO-PLAN W0 c,e)
PicoMEM base (0 none)         672
...the card's own IRQ           5
DSP base port                 544
IRQ line found                  7
DSP version                   513
-- (c) interrupts in 5 s: PCM8 30 fps, PCM8 60 fps, ADPCM4 --
IRQs PCM8 22k/735             151
IRQs PCM8 8k/134              300
IRQs ADPCM4 22k/368             0
-- (e) tracks read in 5 s, the ISR burning 0/25/50/75% --
tracks, burn 0%               177
tracks, burn 25%              167
tracks, burn 50%              158
tracks, burn 75%              150
tracks, 50% ints on: NOT RUN - a PicoMEM ISR may not sti
sectors per track              63
DMA ch 1 masked (4)             4
flags                          64
```

### the later boot, 8 WS: VDXMS1.TXT
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8     69635     7295.13 us
XMS down 32K                8     69608     7292.30 us
XMS up 16K                  8     36974     3873.48 us
XMS down 16K                8     36971     3873.17 us
XMS up 8K                   8     20655     2163.86 us
XMS down 8K                 8     20665     2164.91 us
XMS up 4K                   8     12432     1302.40 us
XMS down 4K                 8     12422     1301.35 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8831
READ_SEQ + copy up           7359
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          16
pointer x span                  0
pointer y span                  0
pointer x at start            317
pointer y at start            170
pointer x at end              317
pointer y at end              170
```
