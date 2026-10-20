# VIDDISK L on the Victory 286: LOADALL works, and what it costs

**A measurement, not a description.** Taken 2026-10-14 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md: a 16 MHz AMD 286, 4 MB
on the board, an onboard PVGA1A), with its **PicoMEM 1.x** in and the bus
set to **8 wait states**, off `make vid286`'s floppy built on branch
`loadall` at `55485a5` (VIDDISK as of `e7557e1`). It is wave L0 of
docs/plans/LOADALL-PLAN.md. True of that machine in that configuration and
not maintained. The report is reproduced whole at the end.

## What it settles

- **The 286's LOADALL runs on this AMD part with the table as
  LOADALL-PLAN 1 lays it out**, the CS cache's access byte **93h** - `L`,
  not `M`. The caches took (checked below 1 MB before anything was written
  through one), a 32 KB read of extended memory IN PLACE matched what
  `OSAPI_XMEM_COPY` put there, the same read 1 KB off differed, and a 32 KB
  write in place came back intact through `OSAPI_XMEM_COPY`. Zero errors,
  zero writes skipped.
- **This BIOS leaves A20 OPEN after `int 15h AH=87h`** - open before any
  copy and open after one. So on this machine the bank decoded in place
  (LOADALL-PLAN wave L4) does not wait for XMEM.DRV's LOADALL transport (L3).
- **Memory above 1 MB is not slower.** 32 KB read above 1 MB took 4,508 us
  against 4,353 below; the difference is the one borrow in the first row.
- **The two transports cost the same per KB and differ only in their fixed
  part**, fitted over the four sizes:

| | fixed a call | per KB | 32 KB | 8 KB | 4 KB | 1 KB |
|---|---|---|---|---|---|---|
| `int 15h AH=87h` (`OSAPI_XMEM_COPY`) | **412 us** | 256.9 us | 8,624 | 2,496 | 1,473 | 617 |
| LOADALL, a borrow a copy | **139 us** | 256.7 us | 8,344 | 2,218 | 1,197 | 349 |
| LOADALL's saving | | | 3.3% | 11.2% | 18.8% | **43.5%** |

  The BIOS's copy already runs at the board's memory speed; what LOADALL
  takes away is the reset. A borrow with no work in it reads **168 us**
  (the fit's 139 is the same thing net of the row's own set-up), against
  LOADALL-PLAN's ESTIMATED ~120.
- **The board copies faster than it did on 2026-10-08**: 0.257 ms a KB
  against the 0.339 VIDDISK `X` read then
  (docs/reports/VIDDISK-XMS-286-2026-10-08.md), both transports alike. The
  wait-state setting is the ISA bus's and these rows are board RAM to board
  RAM, so it is not the obvious cause; whatever changed between the two
  runs, the profile's `xcopy` of 0.345 is now high for this machine.

## Not settled here

- **The fill straight into XMS** - `No drive answering at 1F0h master`.
  With the PicoMEM in, C: is the card's disk, read through the card's own
  ROM by `int 13h`: rung 0 (SPEC.md 52.1), which no LOADALL can reach
  (LOADALL-PLAN 4.2). The question of how many sectors a drive hands over
  inside one borrow needs an IDE drive at 1F0h, master - the board's own,
  if it is still fitted - and on THIS configuration the fill wave (L2) has
  nothing to apply to.
- **A masked window against the PicoMEM's Sound Blaster** - the second run
  with a module playing was not taken.

## What it means for the plan

`tools/os88vidbuf.py` on `GasGas24.V88` (`--profile 286-fast --ring 8
--dcache 64 --pio 0.85`), the copy at this run's 0.27 ms a KB (32 KB
pieces) and a drain in place at ~0.045 (a borrow of ~0.15 ms every frame
and every ~5 ms of decode), the slowest disk that plays, KB/s:

| bank | today | drain in place |
|---|---|---|
| 1 MB | 449 | **374** |
| 2 MB | 284 | **266** |
| 2,464 KB (the file's) | 270 | **253** |

Cheaper copies leave less for the drain to take: 6% of disk at the file's
own bank where the profile's 0.345 made it 12%, and 17% at 1 MB. The fill
is not in the table, this configuration's disk being out of its reach.

## The report

### VDLALL1.TXT
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
a borrow, no work           8      1600      167.62 us
XMEM_COPY up 32K            8     82337     8625.82 us
XMEM_COPY down 32K          8     82308     8622.79 us
LOADALL up 32K              8     79733     8353.02 us
LOADALL down 32K            8     79558     8334.69 us
XMEM_COPY up 8K             8     23753     2488.42 us
XMEM_COPY down 8K           8     23901     2503.92 us
LOADALL up 8K               8     21235     2224.63 us
LOADALL down 8K             8     21100     2210.48 us
XMEM_COPY up 4K             8     14146     1481.97 us
XMEM_COPY down 4K           8     13976     1464.16 us
LOADALL up 4K               8     11487     1203.40 us
LOADALL down 4K             8     11356     1189.68 us
XMEM_COPY up 1K             8      5893      617.36 us
XMEM_COPY down 1K           8      5880      616.00 us
LOADALL up 1K               8      3389      355.04 us
LOADALL down 1K             8      3266      342.15 us
read 32K above 1 MB         8     43032     4508.13 us
read 32K below 1 MB         8     41547     4352.56 us
writes skipped, A20 SHUT        0
errors (any row)                0

-- the run: what the person driving it was doing --
pointer moved (samples)         0
pointer samples taken          38
pointer x span                  0
pointer y span                  0
pointer x at start            246
pointer y at start            176
pointer x at end              246
pointer y at end              176
```
