# The CPU cost of a file read — an opportunity, measured and not taken

**Status: OPEN, and deliberately not started.** Everything here is a
measurement and an argument; nothing in it has been built. It is written down
because the measurement was expensive to take and the conclusion is general:
**the kernel's disk layer costs more CPU than IBM DOS's does, and it is now the
larger of the two gaps between them.**

This is not a DOS-box document. `apps/dos` is where it was found because a DOS
program is the one workload on this machine that does nothing but read files,
so the OS's own share of the time is visible against a real comparison. Every
finding below is about `kernel/disk.inc` and `kernel/diskw.inc`, and every
package on the machine pays it.

**§6 is the WRITE side, added later and larger on a big file**: every
`OSAPI_FILE_APPEND` walks the cluster chain from the front, so a file
written in chunks costs time quadratic in its length - which is FTPD's
large-upload slowness - and there is no write-side `READ_SEQ` yet.

**§7 is the STREAMING case, added 2026-10-10**: on a PIO disk the CPU a
read costs IS the disk speed a video gets, so the read path's cycles a KB
are a quality budget there and not an efficiency nicety. Nothing in it is
built either; it is the list a CPU-usage round would start from.

## 1. How it was measured, which matters more than the numbers

The instrument is a **sampling profiler built from outside the guest**.
MartyPC's debug server answers `status` with the live `CS:IP`, so polling it
from the host is a profiler whose probe effect on the guest is a read of two
registers. No kernel instrumentation, no knob build, and — the point — **the
identical instrument runs against a real IBM DOS 3.30**, which has no symbols
and needs none: the classification that matters is by segment.

Workload: *Prince of Persia* off a 720KB floppy, launched and driven to its
title screen and then into level 1, on `os8088_5150_herc_sb_720_gla` — a
4.77MHz 5150 with a Hercules and a Sound Blaster. The same disk and the same
emulator both times.

**Two traps were paid for on the way and are written down here so the next
person does not pay them again:**

1. **Resolve a kernel sample by LINEAR address, never by offset against the
   `.text` symbol table.** The kernel is a ladder of segments (§2.1) and code
   runs in more than one of them. A `.cold` sample resolved against `.text`
   comes back with a name that is perfectly plausible and simply wrong — the
   first pass of this work reported a `font_*` family and a menu-bar clock
   that do not exist, and two conclusions were drawn off them before the
   error was found. `os88sym.linear()` knows each symbol's own segment;
   `cs*16+ip` against that is right by construction. A symbol in a section
   with no fixed segment (`.boot2`, `.ovl`, an on-demand module) must be
   SKIPPED rather than guessed at, which is `os88sym`'s own rule.
2. **Aggregate by ROUTINE, not by address.** A `.local` label belongs to the
   proc above it, so a per-address histogram spreads one routine over a dozen
   buckets and every one of them looks cheap.

## 2. The numbers

Title stage, both machines, same program:

| | ROM (`int 13h`) | the program | **the OS** | total |
|---|---|---|---|---|
| IBM DOS 3.30 | 42.1s (80.2%) | 6.9s (13.2%) | **3.5s (6.6%)** | 52.6s |
| os8088 | **39.3s (70.3%)** | 7.3s (13.1%) | **9.3s (16.6%)** | 55.9s |

The program's own hot addresses are the **same** on both sides (`9400`,
`A500`, `8F40`, `8F00`, `93C0`), which is the sanity check that the sampler is
honest and that the program is doing the same work.

So: **os8088 is 2.9s ahead on the disk and 5.8s behind on its own overhead**,
and the second number is bigger than the first. The disk half is already won —
§18.91's batching and §18.95's cache make 140 `int 13h` where DOS makes 325,
and spend less ROM time doing it despite moving 74% more sectors.

The kernel's share, by routine (54.4s stage, 1,631 samples):

| routine | guest s | what it is |
|---|---|---|
| `dsk_copy_seg_x` | 2.40 | the cache-hit `rep movsw` |
| `dsk_find_x` | 0.87 | the directory search |
| `dsk_synth_x` | 0.67 | ...and its entry synthesis |
| `dsk_rah_have` | 0.63 | the cache's slot scan (`dsk_rah_serve` since §18.95.9) |
| `dsk_xfer` | 0.37 | the transfer loop itself |
| `dsk_dirw_get_x` | 0.17 | a directory sector |
| `dsk_fat_ofs_x` | 0.17 | a FAT offset |
| `fpg_busy` + `fpg_step` | 0.30 | the progress widget's counters |
| the tail | ~1.6 | `dsk_sanit`, `dsk_ent_zero`, `dskw_*`, `drv_row_ix_of`, … |

**The `dsk` family is 5.97 of the kernel's 7.37 guest seconds — 81%.** Every
`gfx` and `sch` sample falls in the first ten seconds, which is the launch
still painting, and is the second sanity check.

## 3. What is worth taking, in the order the evidence ranks it

### 3.1 The stateless-by-name API, ~2.5 guest seconds

`dsk_find_x` (0.87) + `dsk_synth_x` (0.67) + `dsk_rah_have` (0.63) +
`dsk_dirw_get_x` (0.17) + `dsk_fat_ofs_x` (0.17) is **~2.5 guest seconds of
looking a name up again**, and it is structural rather than incidental:

- `OSAPI_FILE_FIND` is stateless **by ordinal** (§19.7.1), so each call
  re-walks the directory from the front. One open of a file at directory
  entry 14 is fourteen walks.
- `OSAPI_FILE_READ_AT` is stateless **by name** (§18.4.4), so every read
  re-stats the file through `dskw_stat_x` and re-walks its cluster chain from
  the front.

**This was measured, refused, and the refusal was wrong.** §96.24.1 priced
both in `int 13h` with §18.95's cache alive and found them free — one open and
close is **0.00** calls, one 8KB read is 2 calls for 16 sectors of data — and
concluded there was nothing to fix. That is true and it is the wrong question:
the walks did not stop happening, they stopped touching the drive. Every one
of them is still a `rep movsw` of a 512-byte sector out of the cache and a
sixteen-entry scan, on a 4.77MHz 8088.

`dskw_stat_x` already exists inside the kernel and answers in one directory
walk what `OSAPI_FILE_FIND` answers in one per ordinal. §96.24.1.1 is the
record of the refusal and this is its reversal.

### 3.2 `dsk_copy_seg_x`, 2.40 guest seconds — and it is NOT waste

The single hottest routine in the kernel on this workload, and the sample
lands on the `rep movsw` itself, which is the best an 8086 has. Its own
comment prices it correctly: **~1.6 ms a sector against the ~400 ms call it is
standing in for** — 250:1. It is the price of a cache hit and the cache is
worth it.

What might be worth asking is whether it happens on paths that could avoid it.
The direct arm (`.nocache`, when a read has no surplus to gain) already reads
straight into the caller's buffer and pays no copy — that is §18.95.1 and it
is already right. So this row is here to be **left alone**, and to stop the
next reader concluding from a flat profile that the memcpy is the problem.

### 3.3 `dsk_rah_have`, 0.63 guest seconds

A linear scan of the slot table per lookup. It was 14 slots and is 7
(§18.95.6), so this has already halved. Worth a look only after 3.1.

**THE SYMBOL IS `dsk_rah_serve` NOW** (§18.95.9), and the row moved with it in
the only direction that matters here: the scan used to run **twice** on a miss
that filled — once to miss, and once more after the fill, to find "by
construction" the slot the fill had just written. It runs once. This 0.63 was
measured before that, on a workload whose whole shape is misses that fill, so
treat it as an **upper bound** and re-take it before ranking this row again.

## 4. What this is NOT

**It is not a reason to make the disk layer do less caching.** The measurement
is unambiguous in the other direction: 140 `int 13h` against DOS's 325, and
2.9 fewer seconds in the ROM. Every structural decision §18.91 and §18.95 took
is paying. What costs is the layer ABOVE them re-deriving the same answer per
call.

**And it is not urgent.** os8088 loads Prince to its title screen in 55.9s
against IBM DOS 3.30's 52.6, and into level 1 in 111.9 against 102.4 — "close
enough" was the owner's judgement and the numbers support it. This is written
down as an opportunity, with its instrument and its traps, so that whoever
wants the 5.8 seconds does not have to find them again.

## 5. A separate gap the same session found: the drivers' memory

**An exclusive fullscreen program cannot reach the RAM the drivers gave back
for it**, and on a Sound Blaster machine that is 14KB.

`OSAPI_DRV_SUSPEND` (§51.11) is an unload and a reload — it "unhooks the
vector and gives the memory back". `apps/dos` calls it from `dos_drv_take`,
which runs inside the fsx bracket. The arena is claimed in `dos_run`, **before**
the bracket is entered. Measured on the claim map, at the desktop and then
with the program running:

```
at the desktop           with PRINCE.EXE running
9C800..9E800  8.0K ring   (gone)
9E800..A0000  6.0K image  (gone)
                          2FC00..98800  419.0K  the arena
                          98800..9C800   16.0K  the box's own region
453.5K unclaimed          26.5K unclaimed
```

The 14KB is freed **after** the claim that could have used it, and then sits
above the arena for the whole run.

**The ordering cannot simply be swapped, and the reason is the interesting
part.** Three things bind:

1. `OSAPI_DRV_SUSPEND` refuses outside an fsx bracket — `dos_drv_take` reads
   its CF as "not our bracket: nothing moved".
2. §96.2's order is binding the other way: the program is READ before the gfx
   lock is taken, because a floppy read under that lock is the freeze §7.4
   exists to avoid. The claim has to precede the read, so the claim is outside
   the bracket by construction.
3. **And even with perfect ordering it would not be contiguous.** A package
   REGION is claimed top-down, so the box's own 16KB region sits BETWEEN the
   arena and the space the drivers vacated. Absorbing it needs the region to
   move — and a region cannot move while its package is inside a kernel →
   package call, which is every moment it could be claiming: `mem_frameless`
   asks `mem_in_nest`, and that is exact and deliberate (§66.6.1). A stack
   scan is refused for §66.3 rule 5's reason, so there is no cheaper test
   hiding behind it.

So this is a design and not a reorder. Three shapes are worth costing, and
none has been:

- **Claim the arena in two stages** — take what is available before the
  bracket, enter it, suspend the drivers, then `OSAPI_MEM_REGROW` upward. It
  fails on (3) as things stand: the region is in the way.
- **Let the box's region be claimed bottom-up**, so the ceiling above the
  arena is only drivers. This is a loader question (§50.3.2), not a DOS one,
  and it trades against the reason regions are at the ceiling in the first
  place.
- **Suspend the drivers before the package is loaded at all**, which is the
  only ordering that puts the freed space where a top-down claim can take it —
  and needs a door that does not exist, because §51.11's is gated on a bracket
  the package has not entered yet.

The 14KB is real on every machine with a sound card, and the same argument
scales: `ETHER.DRV` is bigger.

## 6. The WRITE side: an append walks the whole chain, every call

**Status: BUILT as SPEC.md 18.4.9 (`OSAPI_FILE_WRITE_SEQ`), and every
chunked writer in the tree but the DOS box moved onto it; the box is
docs/plans/completed/DOS-STREAM-PLAN.md.** Written down 2026-09-27 at the owner's
request, from the VIDDISK work that found it. §3.1 is about READS looking
a name up again. This is the same shape on the way OUT, and on a large file
it is the bigger of the two, because it grows with the file.

### 6.1 What happens

`OSAPI_FILE_APPEND` IS `OSAPI_FILE_WRITE_AT` with the file's size for an
offset (SPEC.md 18.4.7.3), and every call is a complete operation by
contract. So every call:

1. stats the name (a directory walk, as §3.1's reads do);
2. **walks the cluster chain from the front to its last cluster** - the
   walk is bounded by the entry's size (18.4.7.3), so it is exactly as long
   as the file already is;
3. allocates, writes the data, and commits: flush the FAT, link, flush
   again, write the directory sector - four small scattered transfers
   (PERFORMANCE.md Set 24).

Step 2 is the same walk `OSAPI_FILE_READ_AT` makes, and that one is
MEASURED: **141.9 ms per MB of offset** on a fixed disk (VIDEO-W0
2026-09-25; 133 on 86Box's ST11R), the CPU walking the FAT, not the drive.
SPEC.md 18.4.8 fixed the READ side with `OSAPI_FILE_READ_SEQ`: the caller
keeps a 16-byte cursor holding the cluster it stands on, and a call steps
one FAT link past what it read however far into the file that is. **There
is no write-side equivalent**, so writing a file in chunks is QUADRATIC in
its length: chunk *k* walks *k* chunks.

Step 3 is the other half and it is per call, not per MB: Set 24 measured the
same bytes written as 8 KB appends against one write at **2.36x on the
floppy and 3.81x on the ST-225** - `int 13h` calls 22 -> 126 on the hard
disk - before any file was large enough for step 2 to matter.

### 6.2 Who pays it

| caller | shape | what it costs (ESTIMATED from the 141.9 ms/MB walk) |
|---|---|---|
| FTPD `STOR` (`fd_do_write`, `apps/ftpd/ftpd.asm`) | `OSAPI_FILE_WRITE` then `OSAPI_FILE_APPEND` per 8 KB stage (`FD_STGSZ`) | a 5 MB upload is 640 appends averaging 2.5 MB of walk: **~230 s of walking**, against ~340 s to receive 5 MB at FTP-PERF's ~15 KB/s. By the end each 8 KB commit carries ~0.7 s of walk against ~0.5 s to receive it - which is the "horribly slow on a large file" the owner reports |
| FTPD `RETR` (`fd_do_read`) | `OSAPI_FILE_READ_AT` per chunk | the READ side of the same thing, and it needs NO kernel change: `OSAPI_FILE_READ_SEQ` already exists (kern_big, which is the only kernel FTPD ships for - there is no NIC on kern_small) |
| VIDDISK `W` (`tests/vidbench/viddisk.asm`) | 400 x 32 KB appends, 12.5 MB | ~355 s of walking in all, most of it at the end |
| Tracker's render to disk (docs/plans/completed/SPEAKER-PCM-PLAN.md §9, W5 - NOT BUILT, waiting on this) | a song's speaker counts, one byte a sample: BEVERLY.MOD is 2.6 MB at 5,512 Hz and 3.8 MB at 8,000 | in 16 KB appends ~30 s and ~65 s of walking against a 4-6 minute render on a 5150, growing with the song's length. The owner put THIS fix first: the render is built on `WRITE_SEQ`, not on `APPEND` |
| the file manager's copy (SPEC.md 22.5), the installer's big files (52.10.11), any package saving more than its buffer | chunked write | as above, per its chunk; the copy's inner path is kernel-side and should be checked rather than assumed |

**VIDDISK's W row is MEASURED now** (docs/reports/VIDDISK-ST225-2026-09-27.md):
on the owner's ST-225, 12,800 KB in 400 32 KB appends took **700 s, 18.2
KB/s**, against a read-side stream of 104-110 KB/s off the same disk - and
the drive's `READ_AT` walk there is 160 ms a MB, not 142, so the walking
share of those 700 s is ~400. Every other row is still an ESTIMATE: it
multiplies a measured walk rate by a chunk count. The walk is per CLUSTER, so its rate per MB depends on the volume's
cluster size - a floppy's 512- or 1,024-byte clusters walk more links per MB
than a fixed disk's - and it has not been timed on the write path at all.
`tests/viddisk.py --floppy` prints W's guest seconds, and VIDDISK's
`VDWRITE.TXT` prints the write rate on any machine, which is the first
instrument to point at this.

### 6.3 What a fix looks like - the shape, not a design

- **`OSAPI_FILE_WRITE_SEQ`, READ_SEQ's mirror**: the caller's 16-byte cursor
  holds the volume, the mount generation, the file's LAST cluster and its
  size; a call writes from there, allocating and linking forward with no
  walk, and re-seeds from the name exactly as READ_SEQ does after anything
  that remounts or writes elsewhere. That takes out step 2 whole and turns
  the quadratic back into a line. It is the cheap half and it keeps the
  contract that every call is a complete, consistent operation.
- **Committing once per file rather than once per call** is Set 24's lever
  (2.4x to 3.8x on every chunked write) and it is the expensive half: the
  FAT and the directory entry would lag the data between calls, so it
  needs a close verb and an answer for a floppy taken out mid-file - which
  is UI-FREEZE-PLAN §3.2's removable-media consistency model, not a detail.
  `OSAPI_BATCH_BEGIN`/`END` (SPEC.md 18.9.3) is the existing bracket in this
  area; whether it already defers any of step 3 is the first thing to read.
- **Package-side, today, with no kernel byte**: FTPD's `RETR` moves to
  `OSAPI_FILE_READ_SEQ`. Worth doing whenever FTPD is next touched, and it
  is the half of FTPD's large-file slowness that is already solved.

Measure before building (§1's instrument; PERFORMANCE.md's rule 4): time an
FTPD upload of 1, 2 and 4 MB and check the per-MB rate falls the way 6.2
predicts before anybody writes a slot.

## 7. The streaming case: where the CPU IS the disk (2026-10-10)

The rest of this file is about a workload that WAITS on the CPU between
reads. The Video Player on the owner's 16 MHz 286 is the other kind: an IDE
drive moved by programmed I/O, a decode holding about half of every 30 Hz
period, and a reader that gets what is left. There, every clock the read
path spends a KB is a clock the decode does not get or a KB the stream does
not get - **the CPU is the disk's speed**, which is the opposite regime to
the 5150's, where the drive binds and spending CPU to issue fewer, larger
transfers is free. This project has optimised for resident bytes and for
peak speed; it has never had a round aimed at CYCLES A KB, and this case is
why it may want one.

### 7.1 What has been measured

SPEC.md 52.1.2 is the only change made for it so far: HDD.DRV's rung 1 moved
words with `in`/`stosw`/`loop` (~16 clocks a word) where the BIOS uses
`rep insw` (~4), which is ~15% of the 286 at 384 KB/s, and taking it was the
difference between a reader one chunk ahead with 312 stalls and a clean
play. Its VIDDISK run matches the BIOS route's ceiling at every hook share,
so after it the two routes cost the same CPU a KB, to within the
instrument's resolution.

That gives a BOUND and not an answer. VIDDISK's ceiling with the hook
holding 75% is 300.7 KB/s: the reader had at most 25% of 16 MHz, ~4 million
clocks a second, so the whole read path costs **at most ~13,300 clocks a KB,
~26 a word**, all in. How much of that is the ISA bus's own I/O and memory
cycles, which no code removes, and how much is ours (the kernel's READ_SEQ
call, the driver's command and per-sector setup, any copy) is the first
thing to measure, and it decides whether a round is worth anything.

### 7.2 Candidates, as first listed (7.6 prices them)

- **The kernel's per-call path.** READ_SEQ is already the cheap verb (§3.1's
  re-walks do not apply to it), but a 32 KB call still crosses the API
  cell, the volume and cursor checks and the driver's far entry. Measurable
  on MartyPC through any disk route, since it is not the driver's.
- **The driver's per-sector cost.** One `hd_ide_drq` poll and one
  `hd_buf_step` per 512 bytes; small beside 256 words of transfer, but not
  measured. ATA's READ MULTIPLE (0xC4, after SET MULTIPLE) hands the host
  several sectors per DRQ and is the standard answer if the per-sector part
  turns out to matter. It needs a drive that supports it, which is a probe.
- **Polling against IRQ14.** `hd_ide_drq` spins on BSY while the drive
  fetches. Whether that spin costs the decode anything depends on WHO runs
  the decode: inside the bracket the decode is on the 30 Hz hook and
  pre-empts the spin, so the spin only spends leftover time; a windowed play
  whose decode is a task competing with the reader would pay it. Which of
  the two shapes binds has to be read off the player before IRQ-driven
  completion is costed (the AT BIOS's own answer is `int 15h` AH=90h/91h,
  the "device busy" hooks a multitasking OS was meant to take).
- **Any copy between the drive and the ring.** If a transfer lands in a
  kernel buffer and is copied on (§3.2's `dsk_copy_seg_x` is that copy on
  the floppy path), the copy is a second pass over every byte. Whether the
  rung-1 stream reads straight into the caller's buffer is to be checked,
  not assumed.

### 7.3 The instrument

The 286 is the only machine here that runs rung 1 and times it: MartyPC is
an 8088 (rung 1 needs `CPU_286`) and QEMU counts instructions but not bus
cycles. So the split is: the kernel's share on MartyPC, cycle-exact, through
a route it can host; the driver's instruction count under QEMU; and the
total on the 286 with VIDDISK, whose ceiling rows are the number the
encoder's `disk_at` curve is made of. A change that moves the 75% row is a
change the encoder can spend on picture.

### 7.4 Parts 1 and 2, measured (2026-10-10)

**A better bound first, off the VIDDISK run already in hand.** The four
ceilings fit a two-term model - a read costs C clocks a KB of the reader's
CPU plus D of drive time that does not overlap it - almost exactly: the 0%
and 75% rows give **C = ~10,700 clocks a KB and D = ~0.66 ms a KB**, and that
pair predicts the 50% row at 502 KB/s against 480 measured and the 25% row
at 647 against 685. So the reader's CPU is ~10,700 clocks a KB (~21 a word),
not 7.1's 13,300, which charged the drive's time to the CPU as well.

**Part 2, the instruction count (QEMU, rung 1, `tests/viddiskcpu.py`'s
fixture and a gdbstub single-stepper).** QEMU steps `rep insw` one word at
a time, so the census is exact. One steady 32 KB READ_SEQ call:

| where | instructions | what |
|---|---|---|
| `rep insw` | 16,384 | the transfer: 256 words a sector, 64 sectors |
| HDD.DRV, the rest | 2,430 | per sector: `hd_ide_drq` 16, the loop around the transfer ~15, `hd_buf_step` 5; ~150 of command setup |
| kernel `.cold` | 2,073 | the chain walk, ~90 a 2 KB cluster over 16 (`dsk_read_chain_x`, `dsk_next_clus_x`, `dsk_fat_ofs_x`, `dsk_clus2lba_x`, `dsk_fat_window`), the call wrappers, READ_SEQ and READ_AT's entries |
| kernel `.text` | 402 | arming the progress widget and its one fill |

The kernel coalesces the whole contiguous chain into **one driver call**,
and the driver issues **one IDE command** for its 64 sectors. Nothing is
copied: no `dsk_copy_seg_x` in the census, so rung 1 reads straight into
the caller's buffer and 7.2's fourth candidate is answered NO. The first
call after a seek reads 10 more sectors (a FAT window refill) and costs
25,792 steps against the steady 21,289.

Everything that is not the transfer is **~4,900 instructions a 32 KB call,
~153 a KB**. At an ESTIMATED 4-6 clocks an instruction on a 286, plus the
64 status reads that are ISA cycles themselves, that is ~700-1,000 clocks a
KB: **~7-9% of C**. The other ~90% is `rep insw` against the bus, which no
code removes.

**Part 1, the cycle count (MartyPC, an XT with an XT-IDE, through the
ROM's `int 13h`).** One 32 KB READ_SEQ call, single-stepped with the CPU's
cycle counter read at every instruction:

| where | windowed (VIDDISK's R) | inside an fsx bracket (its ceiling rows) |
|---|---|---|
| the XT-IDE ROM | 633,920 (62.2%) | 633,906 (86.9%) |
| kernel `.cold` | 56,481 (5.5%) | 57,328 (7.9%) |
| kernel `.text` | 327,439 (32.1%) | 36,068 (4.9%) |
| the call | **1,019,794 = 213.7 ms** | **729,455 = 152.8 ms** |

Two findings, both about the 8088 and neither about the 286's case:

- **The progress widget is 29% of a windowed 32 KB read on an XT**
  (~290,000 cycles, ~61 ms). Each READ_SEQ call is a job of its own to
  fpg_begin, so a chunked reader on the desktop re-runs the WHOLE bar from
  0 to 62 pixels on every chunk: ~62 `gfx_fill`s a call, through `sw_col`,
  `fpg_step`, `gfx_rect_setup` and the cursor. The Video Player does not
  pay it - every read it makes is inside a bracket, where fpg_arm refuses
  (SPEC.md 12.8.5.2) - but a package reading a file in CHUNKS on the
  desktop does, one bar per call: the hold the player fills into XMS from
  its window timer after a play is one (SPEC.md 98.3.18). A package load or
  a copy is a single job with one scale and redraws the bar once.
- **A refused widget still costs ~2.2-3.4% of a bracketed read**, because
  dsk_xfer's `.notch` loop calls `splf_step` and `fpg_step` through
  `ct_cw_mem_disp` once per SECTOR and each answers "not armed" only after
  its own `pushf`, `fpg_baron` and compare: ~22 instructions a sector,
  ~25,000 cycles a 32 KB call on the XT. This loop is the BIOS path's only -
  a DVK_DRV volume (rung 1) does not run it.

Of the kernel's own 93,000 bracketed cycles, the chain walk is ~23,000,
dsk_xfer's run loop 14,000, that notch loop ~16,000-25,000, the tick and the
scheduler ~12,000 and the call wrappers ~8,000: **12.8% of an XT read is the
kernel's, 87% the ROM's transfer**.

### 7.5 Part 3: the 286 itself (VIDDISK C, VDCPU.TXT)

The 286 runs rung 1 and no emulator here can time it, so VIDDISK grew a
mode for it (C, `tests/viddiskcpu.py` its gate on QEMU). It answers, per
sector and PIT-timed with interrupts off: the bus's own floor (`rep insw`
of 256 words from a drive that already has the sector), the OLD loop's real
price (`in`/`stosw`/`loop`, which closes 52.1.2's "~15%, arithmetic"), the
drive's wait for DRQ and a command's first-sector latency; then 4 MB of
32 KB READ_SEQ calls against 4 MB of the bench's own 64-sector commands,
interrupts on, whose difference is the kernel's and HDD.DRV's CPU on the
real machine. Parts 1 and 2 predicted that difference at ~7-9% of the
transfer.

**MEASURED on the owner's 16 MHz AMD 286 (2026-10-10, VDCPU.TXT; the
drive taken by rung 1, 17 sectors and 12 heads).** Per sector, PIT-timed with
interrupts off, each span net of one PIT read (8.73 us on this board):

| | per sector | per word | per KB |
|---|---|---|---|
| `rep insw`, 256 words | **226 us** | 0.88 us, ~14 clocks | 452 us |
| `in`/`stosw`/`loop` (rung 1 before 52.1.2) | **469 us** | 1.83 us, ~29 clocks | 938 us |
| the wait for DRQ after the first, with `rep insw` | 319 us | | |
| a command's first DRQ (seek and turn) | ~1.0-1.6 ms | | |

And streamed, interrupts on, 4 MB each: **32 KB READ_SEQ calls 42.91 ms a
call (764 KB/s); the bench's own 64-sector commands 40.34 ms (812 KB/s).**

What it says:

- **The DRIVE binds this machine with the hook idle, not the CPU.** A sector
  is ready every ~545 us (226 moving it, 319 waiting for the next): ~940 KB/s
  off the media, and the reader is polling for most of every sector. VIDDISK
  R's 0% ceiling, 774 KB/s in the same session, is that rate less our share.
- **The old loop is MEASURED now: 2.08x `rep insw`**, 243 us more a sector,
  so at the 384 KB/s the field files stream at it cost **18.7% of the CPU** -
  more than 52.1.2's ~15% of arithmetic. That is the stall the Lead 1 card
  showed.
- **Our own CPU is 2.57 ms a 32 KB call, 6.0% of a stream at 0%** - ~80 us a
  KB, ~1,285 clocks, against the transfer's 452 us. The census's 4,900
  instructions a call come to ~8.4 clocks each on this board (its RAM has
  wait states, VIDBENCH's 12-29%), which is above the 4-6 the estimate
  assumed: ~15% of the reader's CPU where 7.4 said 7-9%. At the 75% row,
  where the reader has a quarter of the machine, that is ~2.4% of all of it
  and ~10% of what the reader gets - the most a CPU round on this path could
  give back, and only if all of it went.
- **Re-measured tick-quantised:** method T over 128 calls is ~100 ticks, so
  each per-call figure is good to ~0.4 ms and the difference to ~0.6.

**The verdict for the 286: no further round.** The bus floor is 61% of a
32 KB read's time-to-ready on this drive and none of it is ours; the drive's
pace is the binding term at low decode load; and the whole of our share,
spent perfectly, is worth ~10% of the reader's slice at the 75% row. The
8088's two rows in 7.6 are where CPU a KB was worth bytes, and the cheaper
one is built.

### 7.6 The candidates, priced against 7.4

- **The driver's per-sector cost (READ MULTIPLE).** ~36 instructions a
  sector, ~2,300 of a 32 KB call's 4,900: at most ~4% of the 286's read
  CPU, and only on a drive that takes SET MULTIPLE. Not worth a probe and
  a second command path until 7.5 says the driver's share is larger than
  the census does.
- **The kernel's chain walk.** ~1,450 instructions a 32 KB call on 2 KB
  clusters, ~90 a cluster, ~2% on the 286. A run-length cache would take
  most of it; bigger clusters would take it for free. Not worth bytes for
  the 286.
- **The waits' bound - not a CPU row, found by this work and FIXED (SPEC.md
  52.1.3).** Rung 1 gave up on a drive after 65,536 status reads, ~0.2 s, and
  nothing above it retries: a loaded QEMU failed 5 of 128 READ_SEQ calls that
  way, and a real drive spinning up from standby would fail the same way.
  HD_WAITN x 65,536 reads now, seconds, with a floating bus answered at once
  so a probe is no slower: +16 bytes of HDD.DRV.
- **Polling against IRQ14.** Unpriced still, and not a CPU-a-KB question:
  inside the bracket the decode pre-empts the poll, so the poll only spends
  time nobody else wanted.
- **A copy.** None (7.4).
- **dsk_xfer's per-sector notch loop - the 8088's, and the cheapest row in
  this file. BUILT (SPEC.md 18.91.6).** The loop asks once per RUN whether
  either bar is live (`[spl_fseg]` above `COLD_SEG`, `[fpg_total]` non-zero)
  and skips itself when neither is: **+15 bytes of `.cold` on both kernels,
  resident, `kern_dos` byte-identical**. Re-traced on MartyPC, the same
  bracketed 32 KB read went **729,455 -> 691,196 cycles, 5.2%** - more than
  the ~25,000 estimated, because the loop's own instructions in `dsk_xfer`
  (14,120 -> 4,694 cycles) went with the calls - and the kernel's share of
  the read 12.8% -> 8.3%. Every Video Player stream through the BIOS on a
  5150 gets it. `tests/fpgnotch.py` is the gate that a LIVE bar still
  moves.
- **The windowed widget's per-chunk redraw - the 8088's, and the biggest
  number here.** 29% of a desktop chunked read on an XT. The shape of a fix
  is the widget's, not the disk's: a job that never moves the bar by a
  whole pixel per chunk, or one scale across a caller's chunks, which
  READ_SEQ's cursor could carry. Not designed, and it changes what the
  user sees, so it is the owner's to decide.

