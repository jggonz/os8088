# The gap measured by the player's own routine, on the Victory 286

**A measurement, not a description.** Taken 2026-10-15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md), PicoMEM 1.x in, the
bus at 8 wait states: VIDDISK `X` on `4e116792`, one boot. True of that
machine and that boot; not maintained. The report is reproduced whole at
the end. It is the check docs/reports/XMS-GAP-VICTORY286-2026-10-15.md asked
for: the first run of `apps/os88xgap.inc` (SPEC.md 41.14) on a board that
interleaves.

## What it says

- **`xg_gap answers G 2048`** - the gap the hand-stepped grid of the report
  before calls fastest, found by the routine the Video Player places by.
- **The model holds at a phase it had not been seen at.** This boot's first
  claim is at 180,736 = 512 mod 4 KB (the earlier three were at 0), so every
  grid row is shifted 512 bytes, and the triangle predicts every one of
  them: the gap 512 or 3.5 KB reads 2,649-2,651 us (2,651 predicted), 1.5 or
  2.5 KB reads 1,989-1,992 (1,991). The fine sweep's flat top - 1,990 at
  +1 KB and at +2 KB - is the two points either side of the 2 KB minimum
  this boot's offsets never sample.
- **In the field**: on that build the owner's "on the edge" files, which
  played on some boots and failed on others, played right on six boots
  running.
- VIDDISK's own `READ_SEQ + copy up` (691.1 KB/s) is NOT placed - its
  buffer is wherever the heap put it, here 512 from the block - so it is not
  the player's figure.


## VDXMS1.TXT

```
VIDDISK X - extended memory: a stream bank (VIDEO-XMS-PLAN 3.2)
CPU tier (1 286, 2 386)         1
CPU feature bits                3
XMS free (KB)                3392
buffer at (KB)                136
XMS block at (KB)            1088
buffer at (bytes)          139776
XMS block (bytes)         1114112
32 KB up and back       ok
-- OSAPI_XMEM_COPY, a call (us; on a 286 all of it masked) --
XMS up 32K                  8     94682     9919.12 us
XMS down 32K                8     94728     9923.94 us
XMS up 16K                  8     47924     5020.63 us
XMS down 16K                8     47946     5022.94 us
XMS up 8K                   8     25283     2648.71 us
XMS down 8K                 8     25290     2649.44 us
XMS up 4K                   8     13977     1464.26 us
XMS down 4K                 8     13974     1463.95 us
-- 8K down from 16 places 64 KB apart (us) --
down 8K +0*64K              8     25328     2653.42 us
down 8K +1*64K              8     25325     2653.11 us
down 8K +2*64K              8     25328     2653.42 us
down 8K +3*64K              8     25326     2653.21 us
down 8K +4*64K              8     25325     2653.11 us
down 8K +5*64K              8     25325     2653.11 us
down 8K +6*64K              8     25323     2652.90 us
down 8K +7*64K              8     25324     2653.00 us
down 8K +8*64K              8     25327     2653.31 us
down 8K +9*64K              8     25327     2653.31 us
down 8K +10*64K             8     25327     2653.31 us
down 8K +11*64K             8     25323     2652.90 us
down 8K +12*64K             8     25335     2654.15 us
down 8K +13*64K             8     25322     2652.79 us
down 8K +14*64K             8     25325     2653.11 us
down 8K +15*64K             8     25323     2652.90 us
RAM->RAM 8K (control)       8     25937     2717.22 us
-- 8K down into conventional memory, 64 KB claims (us) --
conventional at (KB)          176
down 8K into it             8     25306     2651.11 us
conventional at (KB)          208
down 8K into it             8     25305     2651.01 us
conventional at (KB)          240
down 8K into it             8     25309     2651.43 us
conventional at (KB)          272
down 8K into it             8     25309     2651.43 us
conventional at (KB)          304
down 8K into it             8     25306     2651.11 us
conventional at (KB)          336
down 8K into it             8     25309     2651.43 us
conventional at (KB)          368
down 8K into it             8     25307     2651.22 us
conventional at (KB)          400
down 8K into it             8     25309     2651.43 us
conventional at (KB)          432
down 8K into it             8     25306     2651.11 us
conventional at (KB)          464
down 8K into it             8     25306     2651.11 us
conventional at (KB)           65
down 8K into it             8     19006     1991.11 us
conventional at (KB)           97
down 8K into it             8     19004     1990.90 us
-- ...into the first claim at offsets (us) --
offset in it (bytes)            0
down 8K into it             8     25304     2650.91 us
offset in it (bytes)            2
down 8K into it             8     25296     2650.07 us
offset in it (bytes)            4
down 8K into it             8     25281     2648.50 us
offset in it (bytes)            8
down 8K into it             8     25262     2646.51 us
offset in it (bytes)           16
down 8K into it             8     25207     2640.74 us
offset in it (bytes)           32
down 8K into it             8     25111     2630.69 us
offset in it (bytes)           64
down 8K into it             8     24919     2610.57 us
offset in it (bytes)          128
down 8K into it             8     24517     2568.46 us
offset in it (bytes)          256
down 8K into it             8     23733     2486.32 us
offset in it (bytes)          512
down 8K into it             8     22152     2320.69 us
offset in it (bytes)         1024
down 8K into it             8     19002     1990.69 us
offset in it (bytes)         2048
down 8K into it             8     18990     1989.43 us
offset in it (bytes)         4096
down 8K into it             8     25306     2651.11 us
offset in it (bytes)         8192
down 8K into it             8     25310     2651.53 us
offset in it (bytes)        16384
down 8K into it             8     25305     2651.01 us
-- 8K, conventional D and XMS S stepped (us) --
first claim (bytes)        180736
down D+0K S+0K              8     25303     2650.80 us
down D+0K S+1K              8     25289     2649.33 us
down D+0K S+2K              8     18989     1989.33 us
down D+0K S+3K              8     19015     1992.05 us
down D+1K S+0K              8     19003     1990.80 us
down D+1K S+1K              8     25304     2650.91 us
down D+1K S+2K              8     25289     2649.33 us
down D+1K S+3K              8     18989     1989.33 us
down D+2K S+0K              8     18990     1989.43 us
down D+2K S+1K              8     19013     1991.84 us
down D+2K S+2K              8     25310     2651.53 us
down D+2K S+3K              8     25289     2649.33 us
down D+3K S+0K              8     25291     2649.54 us
down D+3K S+1K              8     18990     1989.43 us
down D+3K S+2K              8     19000     1990.48 us
down D+3K S+3K              8     25307     2651.22 us
up   D+0K S+0K              8     25300     2650.49 us
up   D+1K S+0K              8     18991     1989.54 us
up   D+2K S+0K              8     19009     1991.43 us
up   D+3K S+0K              8     25306     2651.11 us
xg_gap answers G             2048
the stream              STREAM.DAT
-- the bank filling: 32 KB READ_SEQ for 5 s (KB/s x 10) --
READ_SEQ alone               8895
READ_SEQ + copy up           6911
-- READ_SEQ alone into the buffer at +n KB (KB/s x 10) --
READ_SEQ +0K                 8895
READ_SEQ +1K                 8831
READ_SEQ +2K                 8831
READ_SEQ +3K                 8831
READ_SEQ +0K                 8847
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken         144
pointer x span                  0
pointer y span                  0
pointer x at start            141
pointer y at start            245
pointer x at end              141
pointer y at end              245
```
