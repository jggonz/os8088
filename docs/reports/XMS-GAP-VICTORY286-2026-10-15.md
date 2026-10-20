# Where a copy's two ends sit: the Victory 286's 4 KB interleave

**A measurement, not a description.** Taken 2026-10-15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md), PicoMEM 1.x in, the
bus at 8 wait states: VIDDISK `X` on `1194909e`, three boots, nothing else
open. Every `OSAPI_XMEM_COPY` here is XMEM.DRV's tier-1 LOADALL transport
(SPEC.md 41.13). True of that machine and those boots; not maintained. The
three reports are reproduced whole at the end.

## The finding

**A copy's speed is set by the distance between its two ends, modulo 4 KB,
and by nothing else measured.** The 8 KB copy, conventional offset D against
XMS offset S, both stepped a KB at a time (all three boots to within 3 us):

| (D - S) mod 4 KB | down 8 KB (us) | up 8 KB (us) | ms a KB |
|---|---:|---:|---:|
| 0 | 2,980 | 2,981 | 0.364 |
| 1 KB or 3 KB | 2,321 | 2,321 | 0.283 |
| **2 KB** | **1,661** | **1,661** | **0.203** |

Every row of one diagonal agrees, in both directions. The fine sweep
fills in the middle: the time is linear in the gap, **330 us per 512 bytes**,
a triangle with a 4 KB period - 0.644 us a byte more for each byte whose two
ends fall in the same 2 KB half.

What it is not:

- **Not the XMS address**: sixteen places 64 KB apart read within 1 us.
- **Not the conventional address**: every 64 KB claim from 168 to 520 KB
  reads the same when its gap is the same.
- **Not the disk**: READ_SEQ into the buffer at +0, +1, +2, +3 KB and +0
  again reads 876.7-884.7 KB/s, the spread one tick's quantisation. The
  634 KB/s of the round before was the 14 wait states.
- **Not the boot**: these three boots put the buffer at 196,608, 196,608
  and 131,072 and the XMS block at 1,114,112 - all gap 0 - and all three
  read the slow figure. The "good boots" of earlier rounds are boots whose
  heap happened to leave a gap near 2 KB.

## What it is, generically

A **page-interleaved** memory controller, which this board's two-bank fit
already says it has: consecutive 2 KB pages (a 1 Mbit DRAM's 1,024 columns
x a 16-bit bus) alternate between the banks. A copy whose read and write
fall in different banks keeps both rows open; one whose read and write fall
in the same bank closes a row on every access. The period and the
half-period are the DRAM's page size and so the BOARD's - 256 Kbit parts
would give 1 KB pages and a 2 KB period, a non-interleaved board none at
all - so a fix that hard-codes 2 KB is right for this machine and possibly
the WORST placement on another. It wants measuring once per boot.

## What it is worth

- **Every copy between conventional memory and XMS is up to 1.79x slower
  than it has to be**: a 32 KB copy 11.24 ms at gap 0 against ~6.6 ms at
  2 KB. The bank's fill (READ_SEQ + a 32 KB copy up) reads 671.9 KB/s at
  gap 0; at 2 KB it models at ~748 (+11%).
- **The in-place play (SPEC.md 98.3.18.11) does not pay it**: it copies
  nothing between DRAM ends.
- **It confounds every earlier copy figure taken across boots.** In
  particular docs/reports/LOADALL-L3-VICTORY286-2026-10-15.md compared
  AH=87h and LOADALL on two DIFFERENT boots, whose gaps are not known; its
  piece-size decision (16 KB) stands on a difference smaller than this
  effect and wants re-taking at one gap.
- **RAM to RAM pays it too**: VIDDISK's control copy (gap 16 KB, so 0 mod
  4 KB) is 2,718 us for 8 KB, and VIDBENCH's `movsw 8000 to RAM` 2,642.


## VDXMS1.TXT

```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                192
XMS block at (KB)            1088
buffer at (bytes)          196608
XMS block (bytes)         1114112
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    107295    11240.49 us
XMS down 32K                8    107290    11239.96 us
XMS up 16K                  8     54229     5681.16 us
XMS down 16K                8     54224     5680.64 us
XMS up 8K                   8     28456     2981.12 us
XMS down 8K                 8     28436     2979.02 us
XMS up 4K                   8     15561     1630.20 us
XMS down 4K                 8     15549     1628.95 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     28475     2983.11 us
down 8K +1*64K              8     28475     2983.11 us
down 8K +2*64K              8     28470     2982.58 us
down 8K +3*64K              8     28474     2983.00 us
down 8K +4*64K              8     28471     2982.69 us
down 8K +5*64K              8     28479     2983.53 us
down 8K +6*64K              8     28474     2983.00 us
down 8K +7*64K              8     28470     2982.58 us
down 8K +8*64K              8     28471     2982.69 us
down 8K +9*64K              8     28478     2983.42 us
down 8K +10*64K             8     28473     2982.90 us
down 8K +11*64K             8     28476     2983.21 us
down 8K +12*64K             8     28472     2982.79 us
down 8K +13*64K             8     28480     2983.63 us
down 8K +14*64K             8     28472     2982.79 us
down 8K +15*64K             8     28470     2982.58 us
RAM->RAM 8K (control)       8     25941     2717.64 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          232
down 8K into it             8     28444     2979.86 us
conventional at (KB)          264
down 8K into it             8     28451     2980.59 us
conventional at (KB)          296
down 8K into it             8     28447     2980.17 us
conventional at (KB)          328
down 8K into it             8     28446     2980.07 us
conventional at (KB)          360
down 8K into it             8     28448     2980.28 us
conventional at (KB)          392
down 8K into it             8     28445     2979.96 us
conventional at (KB)          424
down 8K into it             8     28446     2980.07 us
conventional at (KB)          456
down 8K into it             8     28449     2980.38 us
conventional at (KB)          101
down 8K into it             8     19014     1991.95 us
conventional at (KB)          133
down 8K into it             8     19020     1992.58 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     28452     2980.70 us
offset in it (bytes)            2
down 8K into it             8     28446     2980.07 us
offset in it (bytes)            4
down 8K into it             8     28437     2979.13 us
offset in it (bytes)            8
down 8K into it             8     28412     2976.51 us
offset in it (bytes)           16
down 8K into it             8     28369     2972.00 us
offset in it (bytes)           32
down 8K into it             8     28266     2961.21 us
offset in it (bytes)           64
down 8K into it             8     28064     2940.05 us
offset in it (bytes)          128
down 8K into it             8     27666     2898.35 us
offset in it (bytes)          256
down 8K into it             8     26880     2816.01 us
offset in it (bytes)          512
down 8K into it             8     25303     2650.80 us
offset in it (bytes)         1024
down 8K into it             8     22160     2321.53 us
offset in it (bytes)         2048
down 8K into it             8     15855     1661.00 us
offset in it (bytes)         4096
down 8K into it             8     28442     2979.65 us
offset in it (bytes)         8192
down 8K into it             8     28449     2980.38 us
offset in it (bytes)        16384
down 8K into it             8     28445     2979.96 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        237568
down D+0K S+0K              8     28448     2980.28 us
down D+0K S+1K              8     22147     2320.17 us
down D+0K S+2K              8     15855     1661.00 us
down D+0K S+3K              8     22155     2321.01 us
down D+1K S+0K              8     22157     2321.22 us
down D+1K S+1K              8     28452     2980.70 us
down D+1K S+2K              8     22144     2319.86 us
down D+1K S+3K              8     15861     1661.63 us
down D+2K S+0K              8     15849     1660.38 us
down D+2K S+1K              8     22154     2320.90 us
down D+2K S+2K              8     28449     2980.38 us
down D+2K S+3K              8     22149     2320.38 us
down D+3K S+0K              8     22154     2320.90 us
down D+3K S+1K              8     15858     1661.32 us
down D+3K S+2K              8     22156     2321.11 us
down D+3K S+3K              8     28448     2980.28 us
up   D+0K S+0K              8     28450     2980.49 us
up   D+1K S+0K              8     22147     2320.17 us
up   D+2K S+0K              8     15859     1661.42 us
up   D+3K S+0K              8     22163     2321.85 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8767
READ_SEQ + copy up           6719
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8831
READ_SEQ +1K                 8767
READ_SEQ +2K                 8831
READ_SEQ +3K                 8831
READ_SEQ +0K                 8847
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         140
pointer x span                  0
pointer y span                  0
pointer x at start            248
pointer y at start            169
pointer x at end              248
pointer y at end              169
```

## VDXMS2.TXT

```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                192
XMS block at (KB)            1088
buffer at (bytes)          196608
XMS block (bytes)         1114112
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    107289    11239.86 us
XMS down 32K                8    107287    11239.65 us
XMS up 16K                  8     54227     5680.95 us
XMS down 16K                8     54222     5680.43 us
XMS up 8K                   8     28450     2980.49 us
XMS down 8K                 8     28437     2979.13 us
XMS up 4K                   8     15555     1629.58 us
XMS down 4K                 8     15549     1628.95 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     28476     2983.21 us
down 8K +1*64K              8     28475     2983.11 us
down 8K +2*64K              8     28472     2982.79 us
down 8K +3*64K              8     28473     2982.90 us
down 8K +4*64K              8     28475     2983.11 us
down 8K +5*64K              8     28472     2982.79 us
down 8K +6*64K              8     28474     2983.00 us
down 8K +7*64K              8     28474     2983.00 us
down 8K +8*64K              8     28465     2982.06 us
down 8K +9*64K              8     28474     2983.00 us
down 8K +10*64K             8     28476     2983.21 us
down 8K +11*64K             8     28469     2982.48 us
down 8K +12*64K             8     28471     2982.69 us
down 8K +13*64K             8     28473     2982.90 us
down 8K +14*64K             8     28472     2982.79 us
down 8K +15*64K             8     28470     2982.58 us
RAM->RAM 8K (control)       8     25940     2717.53 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          232
down 8K into it             8     28449     2980.38 us
conventional at (KB)          264
down 8K into it             8     28447     2980.17 us
conventional at (KB)          296
down 8K into it             8     28444     2979.86 us
conventional at (KB)          328
down 8K into it             8     28445     2979.96 us
conventional at (KB)          360
down 8K into it             8     28448     2980.28 us
conventional at (KB)          392
down 8K into it             8     28450     2980.49 us
conventional at (KB)          424
down 8K into it             8     28452     2980.70 us
conventional at (KB)          456
down 8K into it             8     28449     2980.38 us
conventional at (KB)          101
down 8K into it             8     19020     1992.58 us
conventional at (KB)          133
down 8K into it             8     19016     1992.16 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     28444     2979.86 us
offset in it (bytes)            2
down 8K into it             8     28447     2980.17 us
offset in it (bytes)            4
down 8K into it             8     28436     2979.02 us
offset in it (bytes)            8
down 8K into it             8     28418     2977.14 us
offset in it (bytes)           16
down 8K into it             8     28368     2971.90 us
offset in it (bytes)           32
down 8K into it             8     28260     2960.58 us
offset in it (bytes)           64
down 8K into it             8     28067     2940.36 us
offset in it (bytes)          128
down 8K into it             8     27658     2897.52 us
offset in it (bytes)          256
down 8K into it             8     26882     2816.22 us
offset in it (bytes)          512
down 8K into it             8     25304     2650.91 us
offset in it (bytes)         1024
down 8K into it             8     22160     2321.53 us
offset in it (bytes)         2048
down 8K into it             8     15853     1660.79 us
offset in it (bytes)         4096
down 8K into it             8     28444     2979.86 us
offset in it (bytes)         8192
down 8K into it             8     28450     2980.49 us
offset in it (bytes)        16384
down 8K into it             8     28443     2979.75 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        237568
down D+0K S+0K              8     28448     2980.28 us
down D+0K S+1K              8     22145     2319.96 us
down D+0K S+2K              8     15861     1661.63 us
down D+0K S+3K              8     22156     2321.11 us
down D+1K S+0K              8     22158     2321.32 us
down D+1K S+1K              8     28449     2980.38 us
down D+1K S+2K              8     22150     2320.48 us
down D+1K S+3K              8     15861     1661.63 us
down D+2K S+0K              8     15849     1660.38 us
down D+2K S+1K              8     22150     2320.48 us
down D+2K S+2K              8     28445     2979.96 us
down D+2K S+3K              8     22148     2320.27 us
down D+3K S+0K              8     22150     2320.48 us
down D+3K S+1K              8     15860     1661.53 us
down D+3K S+2K              8     22155     2321.01 us
down D+3K S+3K              8     28443     2979.75 us
up   D+0K S+0K              8     28455     2981.01 us
up   D+1K S+0K              8     22153     2320.80 us
up   D+2K S+0K              8     15855     1661.00 us
up   D+3K S+0K              8     22169     2322.47 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8831
READ_SEQ + copy up           6719
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8831
READ_SEQ +1K                 8767
READ_SEQ +2K                 8831
READ_SEQ +3K                 8831
READ_SEQ +0K                 8680
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         140
pointer x span                  0
pointer y span                  0
pointer x at start            210
pointer y at start            184
pointer x at end              210
pointer y at end              184
```

## VDXMS3.TXT

```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                128
XMS block at (KB)            1088
buffer at (bytes)          131072
XMS block (bytes)         1114112
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8    107298    11240.80 us
XMS down 32K                8    107290    11239.96 us
XMS up 16K                  8     54230     5681.27 us
XMS down 16K                8     54224     5680.64 us
XMS up 8K                   8     28457     2981.22 us
XMS down 8K                 8     28439     2979.34 us
XMS up 4K                   8     15549     1628.95 us
XMS down 4K                 8     15548     1628.84 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     28469     2982.48 us
down 8K +1*64K              8     28473     2982.90 us
down 8K +2*64K              8     28471     2982.69 us
down 8K +3*64K              8     28473     2982.90 us
down 8K +4*64K              8     28476     2983.21 us
down 8K +5*64K              8     28476     2983.21 us
down 8K +6*64K              8     28474     2983.00 us
down 8K +7*64K              8     28476     2983.21 us
down 8K +8*64K              8     28473     2982.90 us
down 8K +9*64K              8     28466     2982.16 us
down 8K +10*64K             8     28474     2983.00 us
down 8K +11*64K             8     28475     2983.11 us
down 8K +12*64K             8     28473     2982.90 us
down 8K +13*64K             8     28472     2982.79 us
down 8K +14*64K             8     28475     2983.11 us
down 8K +15*64K             8     28473     2982.90 us
RAM->RAM 8K (control)       8     25938     2717.32 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          168
down 8K into it             8     28450     2980.49 us
conventional at (KB)          200
down 8K into it             8     28448     2980.28 us
conventional at (KB)          232
down 8K into it             8     28446     2980.07 us
conventional at (KB)          264
down 8K into it             8     28449     2980.38 us
conventional at (KB)          296
down 8K into it             8     28445     2979.96 us
conventional at (KB)          328
down 8K into it             8     28449     2980.38 us
conventional at (KB)          360
down 8K into it             8     28446     2980.07 us
conventional at (KB)          392
down 8K into it             8     28446     2980.07 us
conventional at (KB)          424
down 8K into it             8     28448     2980.28 us
conventional at (KB)          456
down 8K into it             8     28446     2980.07 us
conventional at (KB)          488
down 8K into it             8     28445     2979.96 us
conventional at (KB)          520
down 8K into it             8     28450     2980.49 us
conventional at (KB)           61
down 8K into it             8     19021     1992.68 us
conventional at (KB)           93
down 8K into it             8     19015     1992.05 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     28449     2980.38 us
offset in it (bytes)            2
down 8K into it             8     28444     2979.86 us
offset in it (bytes)            4
down 8K into it             8     28435     2978.92 us
offset in it (bytes)            8
down 8K into it             8     28415     2976.82 us
offset in it (bytes)           16
down 8K into it             8     28364     2971.48 us
offset in it (bytes)           32
down 8K into it             8     28267     2961.32 us
offset in it (bytes)           64
down 8K into it             8     28065     2940.15 us
offset in it (bytes)          128
down 8K into it             8     27657     2897.41 us
offset in it (bytes)          256
down 8K into it             8     26879     2815.91 us
offset in it (bytes)          512
down 8K into it             8     25307     2651.22 us
offset in it (bytes)         1024
down 8K into it             8     22160     2321.53 us
offset in it (bytes)         2048
down 8K into it             8     15855     1661.00 us
offset in it (bytes)         4096
down 8K into it             8     28451     2980.59 us
offset in it (bytes)         8192
down 8K into it             8     28451     2980.59 us
offset in it (bytes)        16384
down 8K into it             8     28442     2979.65 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        172032
down D+0K S+0K              8     28447     2980.17 us
down D+0K S+1K              8     22150     2320.48 us
down D+0K S+2K              8     15854     1660.90 us
down D+0K S+3K              8     22156     2321.11 us
down D+1K S+0K              8     22157     2321.22 us
down D+1K S+1K              8     28450     2980.49 us
down D+1K S+2K              8     22153     2320.80 us
down D+1K S+3K              8     15860     1661.53 us
down D+2K S+0K              8     15859     1661.42 us
down D+2K S+1K              8     22161     2321.64 us
down D+2K S+2K              8     28448     2980.28 us
down D+2K S+3K              8     22154     2320.90 us
down D+3K S+0K              8     22144     2319.86 us
down D+3K S+1K              8     15855     1661.00 us
down D+3K S+2K              8     22153     2320.80 us
down D+3K S+3K              8     28447     2980.17 us
up   D+0K S+0K              8     28449     2980.38 us
up   D+1K S+0K              8     22152     2320.69 us
up   D+2K S+0K              8     15860     1661.53 us
up   D+3K S+0K              8     22161     2321.64 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8831
READ_SEQ + copy up           6719
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8767
READ_SEQ +1K                 8831
READ_SEQ +2K                 8767
READ_SEQ +3K                 8767
READ_SEQ +0K                 8847
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         148
pointer x span                  0
pointer y span                  0
pointer x at start            242
pointer y at start            198
pointer x at end              242
pointer y at end              198
```
