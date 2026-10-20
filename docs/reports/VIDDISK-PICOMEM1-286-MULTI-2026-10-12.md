# `286-picomem1` again: the Victory 286 on the PicoMEM's multi-sector firmware

**A measurement, not a description.** Taken 2026-10-12 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md) - the machine and the
card of docs/reports/VIDDISK-PICOMEM1-286-2026-10-12.md, the disk C: a `.vhd`
on the PicoMEM 1.x's SD card - with the card's firmware changed and nothing
else. The benches are VIDDISK from `make vidpm` and PMDMA from `563fd60c`.
True of that machine, that firmware and that tree; not maintained.

The firmware is `Elendilon/ISA-PicoMEM`'s `os8088-pm114-disk` branch:

- `6d251db`: a multi-sector int 13h read goes off the SD card a run of
  sectors at a time, and the card reads the next run while the PC copies
  the last. The stock 1.x firmware reads one sector, waits for the PC's
  copy, then reads the next.
- `e8e9e25`: the first runs of a call are 1 and then 2 sectors, so the
  PC starts its first copy after one sector's SD read and not four.

The same day both were folded into `pm1x-sb-fixes`' one commit (`338a2ae`),
and `os8088-pm114` was rebased onto it (`bea75b4`, the DK01 record on top).
The rebased branch's tree is the measured one, byte for byte (`e62eb2d`),
so the hashes above are kept as the record of what ran. The branch that
carried them is retired.

## What it settles

VIDDISK's silent player, 32 KB `READ_SEQ`, KB/s:

| hook | stock firmware | multi-sector (`6d251db`) | + the 1-2-4 ramp (`e8e9e25`) | the bus's ceiling |
|---|---|---|---|---|
| 0% | 684.7 | 748.7 | **755.1** | 786 |
| 25% | 518.3 | 569.5 | **575.9** | 590 |
| 50% | 345.5 | 377.5 | **379.8** | 393 |
| 75% | 172.7 | 189.9 | **189.9** | 197 |

The stock and `6d251db` columns are the same day's two earlier VIDDISK
runs, `F` flipping the card between its two read paths. The 691.1 /
345.5 of the first report is the stock figure from that day's first
install.

- **The profile's disk is 0.9 x 379.8 KB x 1024 = 350,000 B/s**, the rule
  every profile here takes. The curve is the run's own rows. The new
  firmware is +9.9% on stock at half the period. The ramp's own share is
  +0.6%, a third of what was predicted for it.
- **The bus binds, not the card.** PMDMA's `I` reads 512 bytes with
  `rep lodsb`. Card memory costs 915 ns a byte more than RAM, and the
  VGA's own memory costs 923. So the PicoMEM answers at exactly the
  speed of this board's 8-bit bus and adds no wait of its own. `K`'s copy
  is 651 us a sector, `movsw` and `movsb` alike, which is 1.27 us a byte
  and an idle ceiling of 786 KB/s. The run sits at 96.6% of that ceiling.
  What is left is int 13h's entry and exit and the card's handshakes,
  which only reading ahead across calls could reach.
- **On the stock firmware this profile is 9% optimistic.** A machine on
  the shipped 1.x firmware is the first report's 318,000.
- **`K`'s disk record here is not the stream's.** It counts 442 sectors in
  50 calls, which is boot and package loading: it was saved before `R`
  ran. Its copy rows are the ones that matter, and they agree with the
  earlier run to 0.1%.

## PMBUS.TXT
```
I  THE BUS: 512 accesses a row (REP), then ns a byte net of RAM
measurement                 N    counts         us/op
RAM read x512 lodsb        64     10492      137.39 us
card mem read x512         64     46282      606.07 us
screen read x512           64     46607      610.33 us
RAM write x512 stosb       64      8040      105.28 us
card mem write x512        64     41514      543.63 us
card I/O read x512         64     81320     1064.91 us
board I/O read x512        64     80662     1056.29 us

card read, ns a byte           915
card write, ns a byte          856
screen read, ns a byte         923
card I/O over board, ns         16
```

## PMDISK.TXT
```
K  THE CARD'S DISK PATH: the copy, and what the card measured
measurement                 N    counts         us/op
card->RAM 512 movsw        64     49745      651.42 us
card->RAM 512 movsb        64     49768      651.72 us
RAM->RAM 512 movsw         64     13303      174.20 us
card byte read x512        64     90563     1185.95 us

  read path: MULTI-SECTOR (F flips it)
read calls                      50
sectors read                   442
...by the multi-sector p       442
us on the SD card           140070
us waiting for the PC       171303
us in read calls            320925
SD us per sector               316
PC-wait us per sector          387
call us per sector             726
  (cleared: read now, then press K again)
```

## VIDDISK.TXT
```
VIDDISK - streaming off the fixed disk (VIDEO-PLAN W0 b, W2, W3)
sectors per track              63
heads                          16
the stream is on        C:
the stream              STREAM.DAT
-- READ_AT 32 KB, by offset (it re-walks the chain) --
READ_AT 32K @0 MB           3    196608    54925.72 us t
READ_AT 32K @3 MB           3    262144    73234.29 us t
READ_AT 32K @6 MB           3    262144    73234.29 us t
READ_AT 32K @9 MB           3    327680    91542.86 us t
READ_AT 32K @12 MB          3    393216   109851.44 us t
-- READ_SEQ: a seek is one walk, then from where it stands --
READ_SEQ seek 0, 1st        1     65536    54925.72 us t
READ_SEQ 32K @0 MB          8    458752    48060.00 us t
READ_SEQ seek 12MB 1st      1    131072   109851.44 us t
READ_SEQ 32K @12 MB         8    458752    48060.00 us t
READ_SEQ 16K @12 MB         8    262144    27462.86 us t
READ_SEQ 8K @12 MB          8    131072    13731.43 us t
int13 calls, 8 x 32K            8
...under cylinder 16            0
-- the silent player: 32 KB READ_SEQ, 30 Hz hook holding n% --
   (KB/s x 10; the hook has interrupts ON unless it says off)
ceiling, hook 0%             7551
ceiling, hook 25%            5759
ceiling, hook 50%            3798
ceiling, hook 75%            1899
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
N sectors, one call        12    786432    54925.72 us t
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          30
pointer x span                  0
pointer y span                  0
pointer x at start            307
pointer y at start            176
pointer x at end              307
pointer y at end              176
```
