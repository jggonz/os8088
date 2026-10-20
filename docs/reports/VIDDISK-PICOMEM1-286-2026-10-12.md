# The video encoder's `286-picomem1`: the Victory 286 streaming off a PicoMEM 1.x

**A measurement, not a description.** Taken 2026-10-12 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md): a 16 MHz AMD 286, 4 MB
at 100 ns on the board, the onboard Paradise PVGA1A, and a **PicoMEM 1.x**
serving the hard disk C: from a `.vhd` on its SD card (63 sectors a track,
16 heads). The benches are `make vidpm`'s kit from `cab653fd`, copied by FTP
onto an install of `elendilon` made the same day. True of that machine and
that tree; not maintained.

It is the machine docs/reports/VIDBENCH-PVGA286-2026-10-08.md measured, which
is where `286-fast` came from - that report's disk was the board's own IDE
drive and this one's is the card's. The four reports are reproduced whole
below; EMS (`E`) was not configured and VIDSND did not complete (the end of
this file).

## What it settles

| | this run | 2026-10-08 (`286-fast`) |
|---|---|---|
| disk, hook 0% (KB/s) | **691.1** | 729.5 |
| disk, hook 25% | **518.3** | 684.7 |
| disk, hook 50% | **345.5** | 486.3 |
| disk, hook 75% | **172.7** | 294.3 |
| disk, 50% with interrupts off | 345.5 | |
| int 13h calls, 8 x 32 KB `READ_SEQ` | 8 | |
| streaming WRITE (`W`, held) | 290.4 KB/s, 12.5 MB in 44 s | |
| XMS copy, 32 KB up / down | 11,301 / 11,280 us | 11,301 / 11,282 |
| latch copy, 8,000 bytes | 17,081 us | 17,064 |
| `rep movsw` 8,000 to RAM / screen | 2,642 / 9,185 us | 2,641 / 9,172 |

- **The PicoMEM's disk is the CPU's, exactly.** Each row is the idle rate
  times what the hook leaves: 691.1 x 0.75 = 518.3, x 0.5 = 345.5, x 0.25 =
  172.8. Nothing reads on while the CPU is busy - the IDE drive's curve held
  up to 25% because the drive does - and the interrupts-off row is the
  interrupts-on row, as a disk with no completion interrupt to wait for
  reads. So the profile's disk is 0.9 x 345.5 KB x 1024 = **318,000 B/s**
  and its curve is that line: it gives the most back of any profile here
  when the decode is light, and nothing at all to a frame that takes the
  whole period.
- **The machine is the machine it was.** The raw rows agree with 10-08 to
  0.2% and the XMS copy to the microsecond, so `286-picomem1` is
  `286-fast`'s CPU, latch and copy figures with this disk.
- **The decode table is the conservative reading.** VIDBENCH's binary and
  its frame data are byte-identical to 10-08's (re-assembled at both
  commits and compared), yet the frames moved: every one the table was
  fitted to came in at or under the fit (`S P1 x400` 1,014 -> 890 us, `S P2`
  1,459 -> 1,352, `S P4` 1,901 -> 1,795, `S P6` 1,808 -> 1,732; the slice and
  run frames within 1%), and two frames outside the fit moved both ways
  (`SYNTH 1` 795 -> 887, `SYNTH 6` 1,828 -> 1,694, and their RAM-only rows
  the same way). Same code, same data and the same RAM speed is a
  placement effect - where in this board's 4 MB the buffers landed - and
  is not resolved here. The table, fitted to the slower reading on every
  frame it prices, is kept: a frame priced high plays early, one priced low
  plays late.
- **A bank behind this disk costs what it costs on IDE**: `READ_SEQ` alone
  582.4 KB/s, with every chunk copied up 481.1.

## VIDSND

It did not finish: pressing `R` ended in the kernel's **stack overflow
panel - `TASK 00  SP 15D8  UI TASK`** - with the window's two title lines
still drawn. VIDSND programs the DSP, DMA channel 1 and the PIC by hand
with the drivers suspended, and finds its IRQ with DSP F2h; the 1.x's Sound
Blaster is the card's emulation and not a DSP, and something in that path
nests on the UI task's stack. The cause is not chased: SOUND.DRV's own path
to the card is the one the player uses, it works and no longer crackles
(docs/FIELD-NOTES.md 66), and the profile's sound is the 22,050 Hz PCM8 every
286 profile carries. A video played with sound on this machine is the check
that matters, and it is a play, not a bench.

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
movsb 8000 to screen        8     93530     9798.43 us
movsw 8000 to screen        8     87678     9185.36 us
movsb 8000 to RAM           8     50402     5280.23 us
movsw 8000 to RAM           8     25222     2642.31 us
movsb 8000 screen->RAM      8     97516    10216.01 us
movsb 8000 to VGA 12h       8     93561     9801.68 us
latch copy 8000 (VGA)       8    163045    17081.00 us
-- (a) per frame: XDC and ours to the screen, ours to RAM --
SYNTH 7 XDC scr             4     14419     3021.14 us
SYNTH 7 nat scr             4     15353     3216.83 us
SYNTH 7 nat ram             4      6493     1360.44 us
SYNTH 1 XDC scr             4      2887      604.89 us
SYNTH 1 nat scr             4      4233      886.91 us
SYNTH 1 nat ram             4      2865      600.28 us
SYNTH 19 XDC scr            4     14443     3026.16 us
SYNTH 19 nat scr            4     15402     3227.10 us
SYNTH 19 nat ram            4      5317     1114.04 us
SYNTH 6 XDC scr             4      7075     1482.38 us
SYNTH 6 nat scr             4      8087     1694.42 us
SYNTH 6 nat ram             4      3423      717.20 us
S empty XDC scr             4        34        7.12 us
S empty nat scr             4       125       26.19 us
S empty nat ram             4       125       26.19 us
S P1 x400 XDC scr           4      2875      602.38 us
S P1 x400 nat scr           4      4249      890.27 us
S P1 x400 nat ram           4      2884      604.27 us
S P1 sparse XDC scr         4       218       45.67 us
S P1 sparse nat scr         4       482      100.99 us
S P1 sparse nat ram         4       388       81.29 us
S P2 x400 XDC scr           4      4986     1044.69 us
S P2 x400 nat scr           4      6452     1351.85 us
S P2 x400 nat ram           4      3289      689.12 us
S P3 x300 XDC scr           4      5714     1197.22 us
S P3 x300 nat scr           4      7236     1516.12 us
S P3 x300 nat ram           4      3516      736.68 us
S P4 x300 XDC scr           4      7183     1505.01 us
S P4 x300 nat scr           4      8569     1795.41 us
S P4 x300 nat ram           4      3812      798.70 us
S P6 x200 XDC scr           4      7102     1488.04 us
S P6 x200 nat scr           4      8266     1731.93 us
S P6 x200 nat ram           4      3433      719.29 us
S SL16 x150 XDC scr         4     14438     3025.12 us
S SL16 x150 nat scr         4     15301     3205.94 us
S SL16 x150 nat ram         4      6624     1387.89 us
S SL40 x60 XDC scr          4     14206     2976.51 us
S SL40 x60 nat scr          4     14709     3081.90 us
S SL40 x60 nat ram          4      4663      977.01 us
S RUN16x150 XDC scr         4     12837     2689.67 us
S RUN16x150 nat scr         4     14114     2957.23 us
S RUN16x150 nat ram         4      3870      810.86 us
S RUN40 x60 XDC scr         4     12509     2620.94 us
S RUN40 x60 nat scr         4     13102     2745.19 us
S RUN40 x60 nat ram         4      2148      450.05 us
S P1 row XDC scr            4       754      157.98 us
S P1 row nat scr            4      1303      273.01 us
S P1 row nat ram            4       932      195.27 us
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
pointer x at start            305
pointer y at start            174
pointer x at end              305
pointer y at end              174
```

## VIDDISK.TXT
```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              63
heads                          16
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3    262144    73234.29 us t
READ_AT 32K @3 MB           3    327680    91542.86 us t
READ_AT 32K @6 MB           3    327680    91542.86 us t
READ_AT 32K @9 MB           3    393216   109851.44 us t
READ_AT 32K @12 MB          3    458752   128160.01 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1    131072   109851.44 us t
READ_SEQ 32K @0 MB          8    524288    54925.72 us t
READ_SEQ seek 12MB 1st      1    131072   109851.44 us t
READ_SEQ 32K @12 MB         8    524288    54925.72 us t
READ_SEQ 16K @12 MB         8    262144    27462.86 us t
READ_SEQ 8K @12 MB          8    131072    13731.43 us t
int13 calls, 8 x 32K            8
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%             6911
ceiling, hook 25%            5183
ceiling, hook 50%            3455
ceiling, hook 75%            1727
ceiling, 50% ints off        3455
data at 12 MB           ok
-- int 13h on unit 80h: a whole track, one sector --
int13 one track            40   2162688    45313.72 us t
int13 one sector           60    131072     1830.85 us t
-- int 13h: ONE call across a head (the kernel ends at a track) --
one call across a head  ok - the same bytes
one call across a cyl   ok - the same bytes
kernel shapes read             48
...WRONG BYTES                  0
...refused                      0
sectors timed, each way        80
N sectors, track calls     12    851968    59502.86 us t
N sectors, one call        12    786432    54925.72 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          30
pointer x span                  0
pointer y span                  0
pointer x at start            266
pointer y at start            179
pointer x at end              266
pointer y at end              179
```

## VDWRITE.TXT
```
VIDDISK W - writing STREAM.DAT, 12.5 MB, for R to read back
the writer              W: WRITE_SEQ, HELD
the stream is on        C:
free on the disk (KB)      227864
to write (KB)               12800
written this run (KB)       12800
write KB/s x 10              2904
write took (s)                 44
STREAM.DAT is whole: press R to run the bench, D to delete it
```

## VDXMS.TXT
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    107874    11301.14 us
XMS down 32K                8    107673    11280.09 us
XMS up 16K                  8     56079     5874.97 us
XMS down 16K                8     56043     5871.20 us
XMS up 8K                   8     30135     3157.01 us
XMS down 8K                 8     30272     3171.37 us
XMS up 4K                   8     17207     1802.64 us
XMS down 4K                 8     17218     1803.80 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               5824
READ_SEQ + copy up           4811
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          32
pointer x span                  0
pointer y span                  0
pointer x at start            272
pointer y at start            168
pointer x at end              272
pointer y at end              168
```

