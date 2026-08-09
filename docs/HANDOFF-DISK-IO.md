# Handoff — the disk I/O round: revolutions, not sectors

State as of `claude/disk-io-optimization-jlpu15`, cut from `elendilon` @
`b9c8b5f`. Everything below is committed and pushed; nothing is left in the
working tree.

**This is not `docs/HANDOFF.md`** — that file holds a live handover for the
Note Pad latency round and was not mine to overwrite. If the two rounds are
both finished by the time you read this, consolidate them.

**Read first:** SPEC.md §18.95 (what this round ended up building), §18.95.1
(the one rule that makes it pay), §22.5.1 (what measuring it flushed out),
§18.94.2/§18.94.3 (the instrument, and why it is 400 entries long),
§18.9/§18.9.1/§18.9.2/§18.9.3 (the quiet mount, the motor test, the banked
BPB, the batch bracket).

---

## 1. The finding, in one paragraph

*(This section is the state at `b9c8b5f`, kept as the round's starting point.
Section 2 is where it ended.)*

A file operation on os8088 spends **less than half its disk time on the file**.
The progress widget (§12.8) reports the data phase and nothing else, so the
user sees a bar that runs, stalls for seconds with no explanation, runs again —
and the stalls are real disk work the bar was never measuring. The cause is
that the kernel optimised for **sectors** where the media charges for
**revolutions**: `dsk_dirw_next` hands out one LBA at a time and every caller
read it with `cx = 1`, so every metadata sector was its own `int 13h`.

Measured over one whole install (both floppies, 22 files onto a pristine 31M
partition), floppy side only, before any of this round's work:

| phase | sectors | share | int 13h calls | sec/call |
|---|---:|---:|---:|---:|
| BPB (one per mount) | 41 | 4% | 41 | **1.00** |
| FAT read | 12 | 1% | 6 | 2.00 |
| root directory | 72 | 7% | 72 | **1.00** |
| **data — what the bar shows** | **740** | **76%** | **128** | **5.78** |
| subdirectory walks / icon harvest | 109 | 11% | 109 | **1.00** |
| **total** | **974** | | **356** | 2.74 |

Priced with PERFORMANCE.md's own rows — **~400 ms for an `int 13h` in a
coalesced run** (1 to 2 revolutions, near enough whatever it moves) and
**~150–200 ms for an isolated single-sector access** — the 222 calls at
exactly 1.00 sectors are ~39 s and the 134 coalesced ones ~54 s, so the floppy
side is **~93 s** and the payload is under half of it.

**Do not price this with `203 ms a call + 20 ms a sector`.** That is a
two-point linear fit through Set 14 which earlier revisions of this document
and of SPEC.md quoted; it happens to reproduce the ~92 s total and it is not
either of the measured rows. PERFORMANCE.md's "THREE quantities, all
different, and one number was doing all three" is about exactly this, and its
guidance is to quote **calls**, not seconds.

**Nothing here is a field number.** The constants are measured on the 5150; the
call counts are measured on MartyPC, which is cycle-accurate and **30x fast on
a disk**. The owner intends to confirm on the 5150 but has live data on its
hard disk and must back it up first.

---

## 2. What is done

| commit | what |
|---|---|
| `5ecb136` | **§18.94.2** — the per-phase instrument (see §4 below) |
| `2cd933f` | **`KERN_BUDGET` move 14**, 92,160 → 94,208 |
| `cc038e6` | **§18.9.2** — a fixed disk validates its BPB once, ever |
| `131b6ed` | **§19.2.3** — the directory walk reads through a cached window |
| `b1b3cf3` | **§22.5.1** — the copy buffer sits inside one DMA page |
| `20d5033` | **§52.10.8** — Install unmounts the destination before erasing it |
| `9704489` | **§18.9.3** — the batch bracket, and any UI unlock ends it |
| `4ea7ef3` | **§19.2.3.1** — the negative result: aligning the chunk does not pay |
| `17ba4a7` | **PERFORMANCE.md** — price the disk in calls, not a two-point fit |
| `341189d` | **§18.95** — one sector cache, under every read |
| `e982492` | **§18.95.3** — and `sysbench` prices it, in `int 13h` |

**Budget: 92,160 of 94,208 — 2,048 spare, four steps.** §19.2.3 crossed the
image rung and spent 512; §18.95 replaced it and gave that back, and §18.95.4's
run count is 54 bytes of `.bss` into a rung that had room.

### The result, on the scenario the round is named after

`make DISKCNT=1`, floppy side, sectors / **calls**, against `b9c8b5f` — the
three scenarios the whole round has been measured on, at each stage of it:

| | `b9c8b5f` | no cache¹ | §19.2.3's window | **§18.95** |
|---|---|---|---|---|
| hard-disk install | 975 / **356** | 940 / **316** | 816 / **155** | 925 / **114** |
| copy `B:\APPS` → `A:` | 513 / **199** | 474 / **160** | 469 / **131** | 573 / **115** |
| open a Disk window, enter a folder | — | — | 27 / **11** | 51 / **8** |

¹ today's tree built `DIRW1=1`, so it carries §18.9.2's banked BPB and
§18.9.3's bracket and no cache at all — the A/B, not a historical commit.

**356 `int 13h` → 114 on the install, 3.1x.** The per-phase split of the
*before* column is §1's table; the after column has only been recorded as a
total, because §18.95 sits under every phase at once and the interesting
question stopped being which phase.

**The sectors column going up is the trade, not a defect.** The cache buys
revolutions with sectors and PERFORMANCE.md's own rows say a revolution is
worth nine of them. Priced at ~400 ms a call and ~44 ms for a marginal sector
inside one, the last step alone is ~16 s of calls saved against ~5 s of
sectors spent. In seconds the whole round is ~93 s before and **~35–46 s**
after, the width being PERFORMANCE.md's "1 to 2 revolutions per call" band
whose middle nobody has measured. The call count is the measurement and the
seconds are an estimate — quote the former.

**Still not a field number.** Every count here is MartyPC's, which is
cycle-accurate on the CPU and 30x fast on a disk. The constants are the
5150's. The two have never been multiplied together on the iron.

### What §19.2.3 turned out to be, which is not what was planned

The plan in the previous handoff was "coalesce the directory walks, worth ~30%
of the calls", on the reasoning that both directory shapes are contiguous runs.
That reasoning is right and **the change built from it is worth nothing**,
which is the most useful thing this round learned:

> **The walks this exists for are ONE SECTOR LONG.** `fcp_scan` (§22.5) and
> `dsk_find` (§19.7.1) both re-seek from an ordinal for every entry they
> return, so a directory of N entries is N walks — and a walk that reads one
> sector cannot coalesce with itself.

Measured, A/B on the reference copy: the read-ahead-buffer version turned 9
root sectors into 30 and 41 subdirectory sectors into 79 and saved **not one
`int 13h`**. What pays is the *second* walk of the same directory finding its
sector already in memory, so the window has to **outlive the walk** — which is
the metadata cache the previous handoff had filed under item (3), arrived at
from the other direction. It is keyed on §18.8.1's `(volume, dsk_bpb_sig)`, a
full mount drops it and a quiet one does not, and `disk_write` drops a run by
range and same-volume.

Two bounds are load-bearing and both were found by measurement, not design:

- **A refill must be exactly one `int 13h`** (`dsk_rah_cap` bounds the run at
  the track, `mem_claim_dma` keeps the claim inside one 64KB page), or the
  cache can cost calls: a boot's `KERNEL.SYS` lookup matches in the root's
  first sector, and an unbounded 8-sector read took it from **1 call to 2**.
- **The claim must be gated on `mem_avail` reporting twice its size.**
  `mem_claim` sheds and retries, so a purgeable consumer that asks outright
  takes another purgeable consumer's block — and this one runs from
  `dsk_dirw_start`, i.e. every directory walk in the machine.

### The reference measurement

`make DISKCNT=1`, copying `B:\APPS` (9 packages, 90KB) to `A:` through the
file manager on `os8088_5150_cga_gla`, against `make DIRW1=1`:

| phase | before | after |
|---|---|---|
| root directory | 9 / **9** | 18 / **6** |
| subdirectory walks | 41 / **41** | 29 / **16** |
| data | 396 / 84 | 396 / **84** |
| BPB | 43 / **43** | 43 / **43** |
| FAT write | 20 / 20 | 20 / 20 |
| FAT read | 4 / 2 | 4 / 2 |
| **total** | **513 / 199** | **510 / 171** |

**§22.5.1 is in that `data` row and is worth reading as a finding in its own
right.** With the directory cache in and the copy buffer untouched, the data
phase went **84 calls → 93 at an identical sector count**, and it tracked the
*size of the new claim* (84 none, 88 at 4KB, 93 at 8 and 16KB). `fcp_bufget`
claimed its buffer 512-byte aligned and not 64KB-page aligned, so
`dsk_runcap` split a chunk depending on where the buffer happened to land —
the copy engine's cost depended on the heap's history, and the old number was
luck rather than merit. Claimed page-aligned it is 84 again and stays there.

### Verification

- `tests/filetest`, **25/25 PASS** with the free-space equality closing, on the
  ordinary 360KB image **and** on `--scramble`'s legally fragmented one, and
  again under `DIRW1=1` so the refusal path is gated too. Run it on
  `os8088_xt_vga` — the 25 result rows do not fit a 640x200 CGA screen.
- The reference copy's result checked from inside the OS: 9 files in
  `A:\APPS` at their exact source sizes, icons harvested.
- **MartyPC does not write a mounted floppy back to its file**, not even on a
  clean `quit` — so host-side `os88disk.py --verify` of what the guest wrote
  is not available. Verify from inside the guest.

---

## 3. What is NOT done — and where the time actually is now

The round moved the bottleneck twice. §19.2.3 took it off the metadata and
onto the file data; §18.95 then took **36 of the data phase's 130 calls** as
well, because a cache under `dsk_xfer` does not care which phase a sector
belongs to. The install now issues **114 `int 13h` against a floor of about
83** — 925 sectors cannot move in fewer than 925/9 ≈ 103 track-bounded calls
minus what the cache serves, and the residue is files that start mid-track and
chains that do not run on.

**Every option below is now competing for tens of calls, not hundreds.** Read
them against 114, not against §1's 356.

### (0) `DSK_RAH_RUNS` 4 → 8 → 14 — DONE (§19.2.3.1, §18.95.4)

Measured on §19.2.3's window as 163 → 154 calls at 8, and carried into
§18.95. It now ships at **14**, and 14 is a CEILING rather than a choice: the
whole cache is addressed by one 16-bit offset from `dsk_rah_seg`, so
`RUNS × SECS ≤ 128` and 15 would wrap it silently. There is a `%error` on it.

Simulated, 8 → 14 is 96 → 93 calls on the install and 109 → 107 on the copy,
which is within one call of what an unbounded 16 would give. Cost: 54 bytes of
`.bss`, 27KB more purgeable heap, and the claim gate rising from 72KB of free
heap to 126KB — so the band of machines between those two bars loses the cache
entirely.

### (1) Rework `fcp_scan` / `dsk_find` to a resumable cursor — SIMULATED, NOT RECOMMENDED (§18.95.2)

The originally-requested item, and the one the round was expected to end on.
Both routines re-seek from an ordinal for every entry they return, so a
directory of N entries is N walks and each re-reads every sector before the
one it wants. The shape that fixes it safely is a **caller-held cursor the
kernel validates rather than trusts** — (directory cluster, sector index, slot
index) plus a stamp of `(volume, dsk_bpb_sig, directory cluster)`; matching
stamp resumes, anything else falls back to today's re-seek — at the price of
changing slot 0x0348's contract and adding invariant surface to the file path.

**Simulated against the install's and the copy's own traces, a PERFECT cursor
— one that never re-reads anything, ever — is worth five calls and three:**

| | install | copy |
|---|---|---|
| no cache, today's walkers | 316 | 160 |
| no cache, perfect cursor | **135** | 115 |
| §18.95's cache, today's walkers | 96 | 109 |
| §18.95's cache, perfect cursor | **91** | **106** |
| §18.95's cache, 16 slots, today's walkers | 92 | 107 |

Before the cache a cursor was worth 181 calls; after it, five — because a
re-seek's re-reads are now cache *hits*. Row 5 is the punchline: raising
`DSK_RAH_RUNS` collects four of those five with no interface change at all.

What a cursor would still buy is **CPU** — the entries between sector 0 and
the wanted one are re-classified on every call — and that is a different
measurement against a different budget. Nothing here says it is zero, only
that it is not revolutions. **If it is built, build it for that reason and
measure it with a cycle counter, not with §18.94.**

### (2) Coalesce ACROSS files when the data is contiguous — SUBSUMED

The other reading of "if the actual disk data is contiguous": the boundary
*between* two files sharing a track, about 22 of the install's data calls. It
was never reachable from `fcp_scan`/`dsk_find` — it needs a cache below
`dskw_read`, which is what §18.95 is. A second file starting on a track the
first one's tail already pulled in is now served without a call.

### (3) A whole-track read cache — DONE (§18.95)

Built, and it is what the last commit of the round is. The two things worth
knowing before touching it:

- **§18.95.1's no-pollute gate is not a refinement.** Without it the copy's
  subdirectory walks went 15 calls / 27 sectors to **17 / 101** — *worse in
  calls* — because streaming file data evicted the directory chunks between
  one `fcp_scan` and the next. Classic sequential-scan pollution, and it was
  in the first build.
- **It reads forward from the sector that missed**, never from the track's
  start. §19.2.3.1 measured the other thing and it is 3 calls worse than no
  cache at all on a 360KB volume, because reaching the root drags in the boot
  sector and both FAT copies.

### (4) What is genuinely left

- **`DSK_RAH_RUNS` 8 → 16**, above. Four calls, 36KB more purgeable heap.
- **The write side.** Every count in this document is reads; the copy still
  issues 20 FAT-write calls and one directory commit per file, and §18.4's
  commit-order rule (SPEC.md §18.4) is what any batching there has to survive.
  Nothing has been measured or simulated.
- **The 5150.** Below.

### And before any of it: the 5150

Every figure in this document is a MartyPC call count. The constants that turn
counts into seconds are the 5150's, measured, but the product has never been
checked on the iron. `make field` builds the disks; `sysbench` reports
`boot ticks`, a 16KB read, and — since §18.95.3 — the cache's own five rows in
`int 13h`. **A round that is 3.1x on counts and unmeasured in time is exactly
what §18.91 was, and that one turned out to be 13% slower on the machine that
matters.**

## 4. The instrument — use it, do not re-derive it

`make DISKCNT=1` then read `dsk_dbg_ph` (six sector counters) and
`dsk_dbg_phc` (the same six as int 13h calls) by name through
`tools/os88sym.py`. They need **no caller tagging**: the volume's own layout
says which region a sector is in and `[fpg_on]` — the progress widget's own
flag — says whether the bar was counting it, so the payload bucket is *by
construction* exactly what the bar shows.

```python
import os88sym
base = os88sym.linear("dsk_dbg_ph", ("DISK_COUNTERS",))
```

`os88sym` refuses a map that describes a different kernel, so pass the
`--define` — and pass `DIRW_ONE` too when resolving against a `DIRW1=1`
build. Both counter blocks sit **after** §18.94's published block, whose
offsets are an ABI `tests/sysbench` reads by number.

**It counts the BIOS-rung path only.** A `DVK_DRV` volume leaves `dsk_xfer`
before the run loop, so on a machine installing to a hard disk these are the
*floppy* side of the work — which is why the hard-disk mount below reads 0.

**`make DIRW1=1`** is §18.95's A/B: the cache is never claimed, so every read
moves exactly the sectors asked for, through the same refusal path a machine
with no room for it takes. It shares the `VIDEO=` stamp, so changing it
rebuilds — and note that a plain `make` in between DELETES `kernel.bin` at
parse time, so `make bench` after `make DISKCNT=1` leaves you with no kernel
and the previous images.

**`tests/sysbench` prices the cache without a timer** (§18.95.3): five rows in
the counter block, `make DISKCNT=1` against `make DIRW1=1`. Expected on the
360KB bench floppy — 16KB read 33 sectors / 5 calls → 27 / 3, *identical on the
repeat either way*, and all three 1-sector rows 2 calls → 0. A 0 there is the
whole operation served without touching the drive, not a broken measurement.

**`[dsk_batch]` read out of the guest is §18.9.3's own gate**, and it is worth
keeping in any harness that drives a file operation: it must be **0** at every
moment the user can act. Checked this round at the desktop, after a driver
load, after Mount and at the installer's swap prompt.

---

## 5. Harness traps, all of which cost real time

- **`settle` is the wrong wait for an install or a copy.** It watches the
  framebuffer, and an operation holding the gfx lock freezes the UI for its
  whole run — so the screen is *more* still while busy than when finished.
  `os88marty.until(m, cond, what)` is the twin for this; for a disk operation
  the best condition is **the per-phase counters going quiet**, which is a
  direct "the disk stopped working" and needs nothing host-side.
- **The IBM 5150 BIOS is not in this tree** (it is IBM's), so the five
  `os8088_5150_*` machines that ask for `ibm5150_82_v4` will not start in a
  fresh container. **`os8088_5150_cga_gla`** is the GLaBIOS CGA 5150 and is
  what to develop on; `os8088_xt_vga` is the VGA one, and it is also the only
  way to see more than ~15 rows of a test package's output.
- **All the machines have 360KB drives.** `build/filetest.img` is 1.44MB and
  will not mount; build a 360KB one with
  `python3 tools/os88disk.py -o /tmp/filetest360.img --size 360
  build/filetest.o88 build/big.dat`.
- **A Disk window's rows are SORTED** (§19.4), so "the package I put on the
  image" is not row 0. On `filetest.img` row 0 is `BIG.DAT`, and
  double-clicking it correctly says `Bad package`, which reads exactly like a
  broken change.
- **Scripted clicks assume the thing you mean is frontmost.** Anything opened
  first covers the Control Panel; Mount renames its own button to Unmount and
  shifts the row. Drive from a screenshot, not fixed coordinates.
- **The install recipe**, since finding it cost a session: `os8088_xt_hdd`,
  chip menu → Control Panel → Drivers → tick Hard Drive → Hard Drive page →
  Install → Install (arms, `Erases it: Install again to confirm`) → Install
  (runs). With the apps disk in B: both stages run under one click and it ends
  `Done - remove the floppy, Restart`; with fd:1 empty it stops at
  `Insert the APPS disk, then Install`, which is the swap path §18.9.3 is
  written around. The VHD is **not** copied per run the way the floppies are,
  so a second install starts from what the first one left.
- **`pkill -f martypc_headless` kills the calling shell**, because the pattern
  matches the shell's own command line. Use `os88marty.launch`, which kills
  survivors by PID out of `/proc` and waits for the port.
- **A stale emulator holding :9001 makes the new one fail to bind** and log it
  only to its own file — every later command then drives the *old* machine.
  `launch` asserts `cycles == 0`.
- **Piping a long-running script through `tail` buffers everything** until it
  exits, so a progress trace prints nothing for ten minutes and looks hung.
- **`make DISKCNT=1` then `make` does rebuild** — the stamp file covers the
  knob. Verified: the plain kernel is md5-identical either way.

---

## 6. Two things worth knowing about the tree

- **`kernsize --bless` records the baseline in docs/KERNEL-MEMORY.md.** Run it
  when you cross a rung deliberately, or the next person's build prints
  `the image rung CROSSED` for a step that arrived with somebody else's work —
  which is how a real crossing gets ignored.
- **The clone starts shallow with several phantom roots.** `git fetch
  --unshallow` before any ancestry claim, per CLAUDE.md's Rule 0.
