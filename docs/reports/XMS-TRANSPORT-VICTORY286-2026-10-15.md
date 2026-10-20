# XMS copies at the gap: XMEM.DRV's LOADALL against AH=87h, one boot each

**A measurement, not a description.** Taken 2026-10-15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md), PicoMEM 1.x in, the
bus at 8 wait states: VIDDISK `X` on `33a253c0`, two boots. True of that
machine and those boots; not maintained. Both reports are reproduced whole
at the end. It is the measurement LOADALL-PLAN L5 prices the encoder from,
and it re-takes L3's piece-size decision at ONE gap on ONE boot, which
docs/reports/LOADALL-L3-VICTORY286-2026-10-15.md could not
(docs/reports/XMS-GAP-VICTORY286-2026-10-15.md is why).

## The transports, the buffer at the gap the player places at

`xg_gap answers G 2048` on both boots. Boot 1 (boot 2 within 35 us of it
on every row):

| bytes | XMEM.DRV (LOADALL, 16 KB pieces) | `int 15h AH=87h` | both, 0 apart |
|---|---:|---:|---:|
| 4 KB | 969 us | 1,041 | |
| 8 KB | 1,660 | 1,732 | |
| 16 KB | 3,042 | 3,117-3,143 | |
| 32 KB | 5,963 | 5,898-5,901 | 11,206-11,240 |

Up and down agree to within 30 us everywhere.

## What it says

- **The bytes cost the same by either transport: 0.1728 ms a KB**, the
  4-8-16 KB rows' slope for each, to four figures. The memory is the bound,
  not the transport.
- **The difference is fixed cost**: ~278 us a call for XMEM.DRV's LOADALL,
  ~350 for AH=87h - and each further 16 KB piece of a LOADALL copy adds
  ~155 us (the 32 KB row is 278 + 32 x 172.8 + 155). So LOADALL wins at
  16 KB and below by ~72 us, and AH=87h wins at 32 KB by ~60.
- **L3's 16 KB piece stands.** One 32 KB borrow would be ~5,808 us: 155 us
  a 32 KB copy, ~0.4% of a bank's fill at 889.5 KB/s, bought with twice the
  interrupts-off window (5.8 ms). The cross-boot report had LOADALL 2.8%
  WORSE at 32 KB; at one gap it is 1.0% worse, and better below.
- **The gap is worth 1.89x at 32 KB** (11.24 -> 5.96 ms), more than the
  1.79x the 8 KB grid showed: per KB, 0.343 ms at 0 apart against 0.173.
- **For the encoder (L5)**: a copy the player makes now - the bank's copy
  UP, its ring placed (SPEC.md 98.3.18.12) - is ~0.28 ms + 0.173 ms a KB.
  The model that stood, 0.454 ms + 0.296 ms a KB, was a fit across boots of
  unknown gap. There is no copy DOWN any more on a 286 whose LOADALL checks
  out: a held file and a bank both play in place (98.3.18.11, 98.3.18.13).
- **The bank's fill, modelled**: READ_SEQ alone 889.5 KB/s (36.0 ms a 32 KB
  chunk) and a placed copy up 5.96 ms: **~763 KB/s**. VIDDISK's own
  `READ_SEQ + copy up` row (671.9 and 697.5 KB/s here) is NOT placed - its
  buffer is wherever the heap left it.
- READ_SEQ into the buffer at +0..3 KB reads 883.1-902.1 KB/s, flat: the
  disk's end does not care where the buffer is (XMS-GAP's finding again).


## VDXMS1.TXT

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
XMS up 32K                  8    107278    11238.71 us
XMS down 32K                8    107282    11239.13 us
XMS up 16K                  8     54217     5679.90 us
XMS down 16K                8     54213     5679.48 us
XMS up 8K                   8     28428     2978.18 us
XMS down 8K                 8     28426     2977.97 us
XMS up 4K                   8     15539     1627.90 us
XMS down 4K                 8     15530     1626.96 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     28458     2981.33 us
down 8K +1*64K              8     28461     2981.64 us
down 8K +2*64K              8     28453     2980.80 us
down 8K +3*64K              8     28449     2980.38 us
down 8K +4*64K              8     28461     2981.64 us
down 8K +5*64K              8     28457     2981.22 us
down 8K +6*64K              8     28453     2980.80 us
down 8K +7*64K              8     28463     2981.85 us
down 8K +8*64K              8     28459     2981.43 us
down 8K +9*64K              8     28455     2981.01 us
down 8K +10*64K             8     28455     2981.01 us
down 8K +11*64K             8     28459     2981.43 us
down 8K +12*64K             8     28462     2981.75 us
down 8K +13*64K             8     28455     2981.01 us
down 8K +14*64K             8     28455     2981.01 us
down 8K +15*64K             8     28456     2981.12 us
RAM->RAM 8K (control)       8     25938     2717.32 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          168
down 8K into it             8     28440     2979.44 us
conventional at (KB)          200
down 8K into it             8     28435     2978.92 us
conventional at (KB)          232
down 8K into it             8     28440     2979.44 us
conventional at (KB)          264
down 8K into it             8     28438     2979.23 us
conventional at (KB)          296
down 8K into it             8     28437     2979.13 us
conventional at (KB)          328
down 8K into it             8     28437     2979.13 us
conventional at (KB)          360
down 8K into it             8     28438     2979.23 us
conventional at (KB)          392
down 8K into it             8     28439     2979.34 us
conventional at (KB)          424
down 8K into it             8     28437     2979.13 us
conventional at (KB)          456
down 8K into it             8     28436     2979.02 us
conventional at (KB)          488
down 8K into it             8     28437     2979.13 us
conventional at (KB)          520
down 8K into it             8     28439     2979.34 us
conventional at (KB)           63
down 8K into it             8     25289     2649.33 us
conventional at (KB)           95
down 8K into it             8     25292     2649.65 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     28437     2979.13 us
offset in it (bytes)            2
down 8K into it             8     28435     2978.92 us
offset in it (bytes)            4
down 8K into it             8     28426     2977.97 us
offset in it (bytes)            8
down 8K into it             8     28405     2975.77 us
offset in it (bytes)           16
down 8K into it             8     28356     2970.64 us
offset in it (bytes)           32
down 8K into it             8     28255     2960.06 us
offset in it (bytes)           64
down 8K into it             8     28056     2939.21 us
offset in it (bytes)          128
down 8K into it             8     27659     2897.62 us
offset in it (bytes)          256
down 8K into it             8     26873     2815.28 us
offset in it (bytes)          512
down 8K into it             8     25301     2650.59 us
offset in it (bytes)         1024
down 8K into it             8     22152     2320.69 us
offset in it (bytes)         2048
down 8K into it             8     15849     1660.38 us
offset in it (bytes)         4096
down 8K into it             8     28433     2978.71 us
offset in it (bytes)         8192
down 8K into it             8     28435     2978.92 us
offset in it (bytes)        16384
down 8K into it             8     28439     2979.34 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        172032
down D+0K S+0K              8     28438     2979.23 us
down D+0K S+1K              8     22136     2319.02 us
down D+0K S+2K              8     15848     1660.27 us
down D+0K S+3K              8     22150     2320.48 us
down D+1K S+0K              8     22151     2320.59 us
down D+1K S+1K              8     28442     2979.65 us
down D+1K S+2K              8     22133     2318.70 us
down D+1K S+3K              8     15853     1660.79 us
down D+2K S+0K              8     15840     1659.43 us
down D+2K S+1K              8     22150     2320.48 us
down D+2K S+2K              8     28439     2979.34 us
down D+2K S+3K              8     22139     2319.33 us
down D+3K S+0K              8     22135     2318.91 us
down D+3K S+1K              8     15849     1660.38 us
down D+3K S+2K              8     22153     2320.80 us
down D+3K S+3K              8     28440     2979.44 us
up   D+0K S+0K              8     28443     2979.75 us
up   D+1K S+0K              8     22138     2319.23 us
up   D+2K S+0K              8     15846     1660.06 us
up   D+3K S+0K              8     22152     2320.69 us
xg_gap answers G             2048
-- the transports, the buffer G apart (and 0 apart) (us) --
4 KB up and back, 87h   ok
G: XMEM up 32K              8     56918     5962.87 us
G: XMEM down 32K            8     56919     5962.97 us
G: 87h up 32K               8     56330     5901.27 us
G: 87h down 32K             8     56300     5898.12 us
G: XMEM up 16K              8     29039     3042.19 us
G: XMEM down 16K            8     29041     3042.40 us
G: 87h up 16K               8     30000     3142.87 us
G: 87h down 16K             8     29755     3117.20 us
G: XMEM up 8K               8     15847     1660.17 us
G: XMEM down 8K             8     15850     1660.48 us
G: 87h up 8K                8     16528     1731.51 us
G: 87h down 8K              8     16532     1731.93 us
G: XMEM up 4K               8      9248      968.84 us
G: XMEM down 4K             8      9251      969.15 us
G: 87h up 4K                8      9961     1043.53 us
G: 87h down 4K              8      9906     1037.77 us
0: XMEM up 32K              8    107289    11239.86 us
0: XMEM down 32K            8    107281    11239.02 us
0: 87h up 32K               8    106964    11205.81 us
0: 87h down 32K             8    106892    11198.27 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8895
READ_SEQ + copy up           6719
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8895
READ_SEQ +1K                 8831
READ_SEQ +2K                 8831
READ_SEQ +3K                 8831
READ_SEQ +0K                 9021
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         188
pointer x span                  0
pointer y span                  0
pointer x at start            208
pointer y at start            209
pointer x at end              208
pointer y at end              209
```

## VDXMS2.TXT

```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                144
XMS block at (KB)            1088
buffer at (bytes)          147968
XMS block (bytes)         1114112
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8     94678     9918.70 us
XMS down 32K                8     94720     9923.10 us
XMS up 16K                  8     47921     5020.32 us
XMS down 16K                8     47927     5020.95 us
XMS up 8K                   8     25286     2649.02 us
XMS down 8K                 8     25281     2648.50 us
XMS up 4K                   8     13968     1463.32 us
XMS down 4K                 8     13967     1463.21 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     25318     2652.37 us
down 8K +1*64K              8     25325     2653.11 us
down 8K +2*64K              8     25322     2652.79 us
down 8K +3*64K              8     25317     2652.27 us
down 8K +4*64K              8     25324     2653.00 us
down 8K +5*64K              8     25321     2652.69 us
down 8K +6*64K              8     25321     2652.69 us
down 8K +7*64K              8     25319     2652.48 us
down 8K +8*64K              8     25315     2652.06 us
down 8K +9*64K              8     25318     2652.37 us
down 8K +10*64K             8     25325     2653.11 us
down 8K +11*64K             8     25319     2652.48 us
down 8K +12*64K             8     25320     2652.58 us
down 8K +13*64K             8     25325     2653.11 us
down 8K +14*64K             8     25322     2652.79 us
down 8K +15*64K             8     25324     2653.00 us
RAM->RAM 8K (control)       8     25937     2717.22 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          184
down 8K into it             8     25297     2650.17 us
conventional at (KB)          216
down 8K into it             8     25302     2650.70 us
conventional at (KB)          248
down 8K into it             8     25305     2651.01 us
conventional at (KB)          280
down 8K into it             8     25305     2651.01 us
conventional at (KB)          312
down 8K into it             8     25304     2650.91 us
conventional at (KB)          344
down 8K into it             8     25303     2650.80 us
conventional at (KB)          376
down 8K into it             8     25300     2650.49 us
conventional at (KB)          408
down 8K into it             8     25299     2650.38 us
conventional at (KB)          440
down 8K into it             8     25304     2650.91 us
conventional at (KB)          472
down 8K into it             8     25304     2650.91 us
conventional at (KB)           63
down 8K into it             8     25290     2649.44 us
conventional at (KB)           95
down 8K into it             8     25294     2649.86 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     25300     2650.49 us
offset in it (bytes)            2
down 8K into it             8     25291     2649.54 us
offset in it (bytes)            4
down 8K into it             8     25276     2647.97 us
offset in it (bytes)            8
down 8K into it             8     25254     2645.67 us
offset in it (bytes)           16
down 8K into it             8     25208     2640.85 us
offset in it (bytes)           32
down 8K into it             8     25105     2630.06 us
offset in it (bytes)           64
down 8K into it             8     24908     2609.42 us
offset in it (bytes)          128
down 8K into it             8     24514     2568.14 us
offset in it (bytes)          256
down 8K into it             8     23726     2485.59 us
offset in it (bytes)          512
down 8K into it             8     22151     2320.59 us
offset in it (bytes)         1024
down 8K into it             8     19003     1990.80 us
offset in it (bytes)         2048
down 8K into it             8     18990     1989.43 us
offset in it (bytes)         4096
down 8K into it             8     25306     2651.11 us
offset in it (bytes)         8192
down 8K into it             8     25300     2650.49 us
offset in it (bytes)        16384
down 8K into it             8     25301     2650.59 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        188928
down D+0K S+0K              8     25302     2650.70 us
down D+0K S+1K              8     25291     2649.54 us
down D+0K S+2K              8     18985     1988.91 us
down D+0K S+3K              8     19005     1991.01 us
down D+1K S+0K              8     18998     1990.27 us
down D+1K S+1K              8     25301     2650.59 us
down D+1K S+2K              8     25289     2649.33 us
down D+1K S+3K              8     18987     1989.12 us
down D+2K S+0K              8     18986     1989.02 us
down D+2K S+1K              8     19001     1990.59 us
down D+2K S+2K              8     25307     2651.22 us
down D+2K S+3K              8     25284     2648.81 us
down D+3K S+0K              8     25293     2649.75 us
down D+3K S+1K              8     18990     1989.43 us
down D+3K S+2K              8     18999     1990.38 us
down D+3K S+3K              8     25301     2650.59 us
up   D+0K S+0K              8     25295     2649.96 us
up   D+1K S+0K              8     18988     1989.23 us
up   D+2K S+0K              8     19003     1990.80 us
up   D+3K S+0K              8     25297     2650.17 us
xg_gap answers G             2048
-- the transports, the buffer G apart (and 0 apart) (us) --
4 KB up and back, 87h   ok
G: XMEM up 32K              8     56904     5961.40 us
G: XMEM down 32K            8     56913     5962.34 us
G: 87h up 32K               8     56400     5908.60 us
G: 87h down 32K             8     56064     5873.40 us
G: XMEM up 16K              8     29041     3042.40 us
G: XMEM down 16K            8     29042     3042.51 us
G: 87h up 16K               8     30100     3153.35 us
G: 87h down 16K             8     29749     3116.57 us
G: XMEM up 8K               8     15847     1660.17 us
G: XMEM down 8K             8     15850     1660.48 us
G: 87h up 8K                8     16534     1732.14 us
G: 87h down 8K              8     16529     1731.61 us
G: XMEM up 4K               8      9251      969.15 us
G: XMEM down 4K             8      9250      969.05 us
G: 87h up 4K                8      9940     1041.33 us
G: 87h down 4K              8      9899     1037.04 us
0: XMEM up 32K              8    107287    11239.65 us
0: XMEM down 32K            8    107284    11239.34 us
0: 87h up 32K               8    107118    11221.94 us
0: 87h down 32K             8    107097    11219.74 us
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8895
READ_SEQ + copy up           6975
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8831
READ_SEQ +1K                 8831
READ_SEQ +2K                 8831
READ_SEQ +3K                 8831
READ_SEQ +0K                 8847
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         184
pointer x span                  0
pointer y span                  0
pointer x at start            254
pointer y at start            219
pointer x at end              254
pointer y at end              219
```
