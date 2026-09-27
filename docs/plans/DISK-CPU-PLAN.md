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

**Status: OPEN, not started - written down 2026-09-27 at the owner's
request, from the VIDDISK work that found it.** §3.1 is about READS looking
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
