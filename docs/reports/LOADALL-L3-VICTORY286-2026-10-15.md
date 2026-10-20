# XMEM.DRV's LOADALL transport on the Victory 286, against AH=87h

**A measurement, not a description.** Taken 2026-10-15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md), PicoMEM 1.x in, the
bus at 8 wait states: VIDDISK `X` once on the build before wave L3 (every
`OSAPI_XMEM_COPY` by `int 15h AH=87h`) and once on `399aec14` (XMEM.DRV's
tier-1 copy by LOADALL in 8 KB pieces, SPEC.md 41.13), each off its own boot,
and VIDBENCH on the second. True of that machine and those boots; not
maintained. The reports are reproduced whole at the end.

## The transport

| copy | AH=87h (us) | LOADALL, 8 KB pieces (us) | change |
|---|---|---|---|
| 4 KB | 1,638 | 1,465 | **-10.6%** |
| 8 KB | 2,823 | 2,652 | **-6.1%** |
| 16 KB | 5,188 | 5,180 | -0.2% |
| 32 KB | 9,952 | 10,236 | **+2.8%** |

Fitted: AH=87h is **0.454 ms a call** and 0.296 ms a KB; LOADALL is **~0.28
ms** for its first piece, and each further 8 KB piece adds **~0.17 ms**,
which is what turns a saving at 4 KB into a loss at 32. The bank's fill
reads the same: READ_SEQ with a 32 KB copy up behind it 691.1 -> 684.7 KB/s.
So the piece became **16 KB** (`XM_LPIECE`): 32 KB is then two borrows
against one reset, level, and the masked window is still half AH=87h's.

The first piece costs ~0.28 ms here against the 0.14 VIDDISK `L` fitted for
a bare borrow on 10-14: the cell's own dispatch, `xm_chk`, the A20 probe and
`xm_lend` are in this figure and not in that one.

## Where the speed comes from - not XMS's placement

Both runs' sixteen 8 KB copies from 64 KB steps across 1 MB of XMS read
**flat to the microsecond** (2,826-2,830 us, then 2,655-2,657). The buffer was
at 132 KB and the block at 1,088 KB on both boots, and the two agree (0.296
and 0.313 ms a KB); the copy has read 0.214, 0.257 and 0.337 on other boots,
whose buffer addresses were not reported. And the RAM -> RAM control, 8 KB
inside that buffer, reads **0.332 ms a KB - slower than either XMS copy**.
The hypothesis this leaves is the CONVENTIONAL end's address: VIDDISK X now
sweeps it (64 KB claims across the heap, each row saying where), which is
the next field run's question.

## VIDBENCH

Within 1% of the 2026-10-15 8 WS run on the heavy rows (SYNTH 7 to the
screen 3,068.7 us against 3,043.8). Rows dominated by segment set-up moved
more - `S P1 row` 280.6 -> 218.1, `S P1 sparse` 113.1 -> 98.7 - and the
RAM rows by up to 4% either way, with no decoder change between: the same
signature as above, memory whose speed depends on where a package lands.
This run is the "before" for wave L1's decode.

## The reports

### VDXMS1.TXT - before L3 (AH=87h)
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                132
XMS block at (KB)            1088
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8     95003     9952.75 us
XMS down 32K                8     94894     9941.33 us
XMS up 16K                  8     49520     5187.83 us
XMS down 16K                8     49535     5189.41 us
XMS up 8K                   8     26946     2822.93 us
XMS down 8K                 8     26964     2824.81 us
XMS up 4K                   8     15638     1638.27 us
XMS down 4K                 8     15663     1640.89 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     26978     2826.28 us
down 8K +1*64K              8     27007     2829.32 us
down 8K +2*64K              8     27005     2829.11 us
down 8K +3*64K              8     27008     2829.42 us
down 8K +4*64K              8     27004     2829.00 us
down 8K +5*64K              8     27011     2829.73 us
down 8K +6*64K              8     27005     2829.11 us
down 8K +7*64K              8     27011     2829.73 us
down 8K +8*64K              8     27006     2829.21 us
down 8K +9*64K              8     27009     2829.53 us
down 8K +10*64K             8     26983     2826.80 us
down 8K +11*64K             8     27009     2829.53 us
down 8K +12*64K             8     27011     2829.73 us
down 8K +13*64K             8     27006     2829.21 us
down 8K +14*64K             8     27007     2829.32 us
down 8K +15*64K             8     27009     2829.53 us
RAM->RAM 8K (control)       8     25947     2718.27 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8767
READ_SEQ + copy up           6911
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          50
pointer x span                  0
pointer y span                  0
pointer x at start            184
pointer y at start            238
pointer x at end              184
pointer y at end              238
```

### VDXMS2.TXT - L3, 8 KB pieces
```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                132
XMS block at (KB)            1088
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8     97689    10234.14 us
XMS down 32K                8     97727    10238.12 us
XMS up 16K                  8     49444     5179.87 us
XMS down 16K                8     49452     5180.71 us
XMS up 8K                   8     25310     2651.53 us
XMS down 8K                 8     25320     2652.58 us
XMS up 4K                   8     13996     1466.25 us
XMS down 4K                 8     13981     1464.68 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     25352     2655.93 us
down 8K +1*64K              8     25356     2656.35 us
down 8K +2*64K              8     25353     2656.04 us
down 8K +3*64K              8     25358     2656.56 us
down 8K +4*64K              8     25357     2656.46 us
down 8K +5*64K              8     25349     2655.62 us
down 8K +6*64K              8     25355     2656.25 us
down 8K +7*64K              8     25355     2656.25 us
down 8K +8*64K              8     25360     2656.77 us
down 8K +9*64K              8     25357     2656.46 us
down 8K +10*64K             8     25354     2656.14 us
down 8K +11*64K             8     25348     2655.51 us
down 8K +12*64K             8     25360     2656.77 us
down 8K +13*64K             8     25354     2656.14 us
down 8K +14*64K             8     25355     2656.25 us
down 8K +15*64K             8     25360     2656.77 us
RAM->RAM 8K (control)       8     25946     2718.16 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8831
READ_SEQ + copy up           6847
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          50
pointer x span                  0
pointer y span                  0
pointer x at start            275
pointer y at start            173
pointer x at end              275
pointer y at end              173
```

### VIDBENC1.TXT
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
movsb 8000 to screen        8     87399     9156.13 us
movsw 8000 to screen        8     79965     8377.33 us
movsb 8000 to RAM           8     50405     5280.55 us
movsw 8000 to RAM           8     25220     2642.11 us
movsb 8000 screen->RAM      8     89454     9371.42 us
movsb 8000 to VGA 12h       8     87437     9160.11 us
latch copy 8000 (VGA)       8    150363    15752.40 us
-- (a) per frame: XDC and ours to the screen, ours to RAM --
SYNTH 7 XDC scr             4     13561     2841.36 us
SYNTH 7 nat scr             4     14646     3068.70 us
SYNTH 7 nat ram             4      6437     1348.71 us
SYNTH 1 XDC scr             4      2761      578.49 us
SYNTH 1 nat scr             4      3670      768.95 us
SYNTH 1 nat ram             4      2635      552.09 us
SYNTH 19 XDC scr            4     13588     2847.02 us
SYNTH 19 nat scr            4     14315     2999.35 us
SYNTH 19 nat ram            4      5516     1155.73 us
SYNTH 6 XDC scr             4      6758     1415.96 us
SYNTH 6 nat scr             4      7651     1603.07 us
SYNTH 6 nat ram             4      3623      759.10 us
S empty XDC scr             4        36        7.54 us
S empty nat scr             4       194       40.64 us
S empty nat ram             4       195       40.85 us
S P1 x400 XDC scr           4      2762      578.70 us
S P1 x400 nat scr           4      4724      989.79 us
S P1 x400 nat ram           4      3449      722.65 us
S P1 sparse XDC scr         4       207       43.37 us
S P1 sparse nat scr         4       471       98.68 us
S P1 sparse nat ram         4       408       85.48 us
S P2 x400 XDC scr           4      4771      999.64 us
S P2 x400 nat scr           4      6513     1364.63 us
S P2 x400 nat ram           4      3632      760.99 us
S P3 x300 XDC scr           4      5402     1131.85 us
S P3 x300 nat scr           4      7205     1509.62 us
S P3 x300 nat ram           4      3853      807.29 us
S P4 x300 XDC scr           4      6624     1387.89 us
S P4 x300 nat scr           4      8485     1777.81 us
S P4 x300 nat ram           4      4111      861.35 us
S P6 x200 XDC scr           4      6493     1360.44 us
S P6 x200 nat scr           4      7597     1591.76 us
S P6 x200 nat ram           4      3452      723.28 us
S SL16 x150 XDC scr         4     13582     2845.76 us
S SL16 x150 nat scr         4     14497     3037.48 us
S SL16 x150 nat ram         4      6765     1417.43 us
S SL40 x60 XDC scr          4     13262     2778.72 us
S SL40 x60 nat scr          4     13849     2901.71 us
S SL40 x60 nat ram          4      4596      962.97 us
S RUN16x150 XDC scr         4     11807     2473.86 us
S RUN16x150 nat scr         4     13393     2806.16 us
S RUN16x150 nat ram         4      4029      844.17 us
S RUN40 x60 XDC scr         4     11619     2434.47 us
S RUN40 x60 nat scr         4     11985     2511.15 us
S RUN40 x60 nat ram         4      2094      438.74 us
S P1 row XDC scr            4       706      147.92 us
S P1 row nat scr            4      1041      218.11 us
S P1 row nat ram            4       820      171.81 us
bracket mode (FSXM)             3
adapter kind (VID)              0
-- the worst frame, per mille of a frame period --
XDC scr, 30 fps                85
  ...at 23.976 fps             68
nat scr, 30 fps                92
  ...at 23.976 fps             73

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         110
pointer x span                  0
pointer y span                  0
pointer x at start            185
pointer y at start            204
pointer x at end              185
pointer y at end              204
```
