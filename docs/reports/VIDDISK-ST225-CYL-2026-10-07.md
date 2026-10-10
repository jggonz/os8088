# VIDDISK on the ST-225: cylinder-length reads (`HDCYLRUN=1`) against the track

**A measurement, not a description.** Taken 2026-10-07 by the owner on the
5150 - **Seagate ST-225 on a real ST11M**, the disk and controller of
docs/reports/VIDDISK-ST225-2026-09-27.md - with the system floppies built at
`8538aa0`: the stock `os8088-360.img` and the same tree built `make
HDCYLRUN=1` (SPEC.md 18.91.5). It is true of that tree and that machine and
is not maintained against later ones. Both reports are reproduced whole.

## How it was taken

Each system floppy was written to a 360 KB disk and BOOTED (so C: is reached
through `HDD.DRV`'s rung 0 on both, which the knob changes in
`hd_bios_run` - the only difference between the two runs is the run's bound).
On each: VIDDISK copied to C:, `W` (STREAM.DAT, 12.5 MB, the HELD streaming
write), `R`, the report renamed.

## What it settles

| | stock (`VIDNORM.TXT`) | `HDCYLRUN=1` (`VIDCYL.TXT`) | |
|---|---|---|---|
| int 13h calls, 8 x 32 KB `READ_SEQ` | 38 | **16** | -58% |
| `READ_SEQ` 32 KB @ 12 MB | 288.4 ms | 240.3 ms | -17% |
| ceiling, hook 0% (KB/s) | 112.7 | 134.8 | +20% |
| ceiling, hook 25% | 109.2 | 131.5 | +20% |
| **ceiling, hook 50%** | **102.3** | **130.1** | **+27%** |
| ceiling, hook 75% | 92.9 | 113.9 | +23% |
| ceiling, 50% interrupts off | 105.3 | 131.5 | +25% |
| one cylinder (68 sectors): a call a track / one call | 288.4 / 233.4 ms | (the same raw rows) | -19% |
| the sweep, 48 kernel-shaped runs | 0 wrong, 0 refused | 0 wrong, 0 refused | |
| data at 12 MB | ok | ok | |

- **The ST11M's ROM carries one call across a head and a cylinder, from any
  starting sector**: both crossing rows and all 48 sweep shapes read the
  bytes a track a call reads, on the machine and not only in 86Box.
- **The stream is +27% at the encoder's row** (`5150-st225` takes 0.9 of
  the 50% row): 96,000 B/s would become ~117,000.
- **The streaming WRITE on iron**: `W` (WRITE_SEQ held) wrote 12.5 MB in
  **178 s, ~72 KB/s**, where the 2026-09-27 report's APPEND took 700 s,
  18.2 KB/s. Writes still stop at the track on both kernels.
- The stock ceiling agrees with the 2026-09-27 report (102.3 against 104.2
  at 50%), so the disk is the disk it was.

## VIDNORM.TXT (stock)

```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              17
heads                           4
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3   1507328   421097.19 us t
READ_AT 32K @3 MB           3   2555904   714034.38 us t
READ_AT 32K @6 MB           3   4653056  1299908.74 us t
READ_AT 32K @9 MB           3   5767168  1611154.50 us t
READ_AT 32K @12 MB          3   7274496  2032251.69 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1    655360   549257.21 us t
READ_SEQ 32K @0 MB          8   2686976   281494.32 us t
READ_SEQ seek 12MB 1st      1   2293760  1922400.25 us t
READ_SEQ 32K @12 MB         8   2752512   288360.03 us t
READ_SEQ 16K @12 MB         8   1376256   144180.01 us t
READ_SEQ 8K @12 MB          8    786432    82388.58 us t
int13 calls, 8 x 32K           38
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%             1127
ceiling, hook 25%            1092
ceiling, hook 50%            1023
ceiling, hook 75%             929
ceiling, 50% ints off        1053
data at 12 MB           ok
-- int 13h on unit 80h: a whole track, one sector --
int13 one track            40   3473408    72776.58 us t
int13 one sector           60   1507328    21054.85 us t
-- int 13h: ONE call across a head (the kernel ends at a track) --
one call across a head  ok - the same bytes
one call across a cyl   ok - the same bytes
kernel shapes read             48
...WRONG BYTES                  0
...refused                      0
sectors timed, each way        68
N sectors, track calls     12   4128768   288360.03 us t
N sectors, one call        12   3342336   233434.31 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          30
pointer x span                  0
pointer y span                  0
pointer x at start            271
pointer y at start            216
pointer x at end              271
pointer y at end              216
```

## VIDCYL.TXT (`HDCYLRUN=1`)

```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              17
heads                           4
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3   1376256   384480.05 us t
READ_AT 32K @3 MB           3   2424832   677417.23 us t
READ_AT 32K @6 MB           3   4390912  1226674.44 us t
READ_AT 32K @9 MB           3   5505024  1537920.20 us t
READ_AT 32K @12 MB          3   6946816  1940708.82 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1    655360   549257.21 us t
READ_SEQ 32K @0 MB          8   2228224   233434.31 us t
READ_SEQ seek 12MB 1st      1   2228224  1867474.53 us t
READ_SEQ 32K @12 MB         8   2293760   240300.03 us t
READ_SEQ 16K @12 MB         8   1114112   116717.15 us t
READ_SEQ 8K @12 MB          8    655360    68657.15 us t
int13 calls, 8 x 32K           16
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%             1348
ceiling, hook 25%            1315
ceiling, hook 50%            1301
ceiling, hook 75%            1139
ceiling, 50% ints off        1315
data at 12 MB           ok
-- int 13h on unit 80h: a whole track, one sector --
int13 one track            40   3473408    72776.58 us t
int13 one sector           60   1441792    20139.43 us t
-- int 13h: ONE call across a head (the kernel ends at a track) --
one call across a head  ok - the same bytes
one call across a cyl   ok - the same bytes
kernel shapes read             48
...WRONG BYTES                  0
...refused                      0
sectors timed, each way        68
N sectors, track calls     12   4128768   288360.03 us t
N sectors, one call        12   3342336   233434.31 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          30
pointer x span                  0
pointer y span                  0
pointer x at start            250
pointer y at start            180
pointer x at end              250
pointer y at end              180
```
