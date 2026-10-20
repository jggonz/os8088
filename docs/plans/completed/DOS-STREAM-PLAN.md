# The DOS box on the stream reader and the stream writer

**Status: BUILT - SPEC.md 96.52, 96.53, 96.53.1, 18.4.9.1 and 18.4.9.2 are
the contract, and this is the design record behind them.** Branch
`dos-stream-io`, cut from `elendilon` at `c459871`. Written 2026-09-30 at
the owner's request as the DOS half deferred from SPEC.md 18.4.8.1, and built
the same day on the owner's three answers: HELD, `kern_dos` included, and
*"as small as possible, but up to 1.5 KB is worth the load and write time"*.
**Section 9 is what the build found against what this predicted**, and it is
the part to read first: two of its five defects were the kernel's.

**The one-paragraph version.** A DOS program's every data byte goes through
ONE 8 KB, cluster-aligned window in the box (`dos_wseg`). Reading refills it
with `OSAPI_FILE_READ_AT`, which re-stats the name and walks the chain from
the front on every refill; writing drains it with one `OSAPI_FILE_WRITE` and
then one `OSAPI_FILE_APPEND` per window, and every append walks the whole
chain and commits the FAT and the entry. Both are quadratic in the file's
length. The two slots that fix exactly that shipped this cycle (SPEC.md
18.4.8, 18.4.9) and the box uses neither. The design keeps the window, keeps
every `int 21h` handler's shape, and changes what the window's two edges
call: a 16-byte READ_SEQ cursor and a WRITE_SEQ token live **in the handle
record**, where the record's own zeroing gives the "cursor names no file"
rule of SPEC.md 18.4.8.1 for nothing. It is DOS-shaped, too: a real DOS's
SFT keeps the current cluster for the same reason.

Three defects surfaced on the way and are **pre-existing**. Two bind this work
(section 3) and are fixed first. Everything marked ESTIMATE below is
arithmetic on a measured rate and wave 0 exists to replace it.

---

## 1. What the box does today

Every file call goes through a `dos_be_*` door (22 of them, `DBE_NENT`,
`apps/dos/dos.asm:5017-5053`). A door stores an ordinal and jumps to
`dos_be_go`, which runs the host's `dos_k_*` body on the UI task's stack
(SPEC.md 96.4.1, 96.4.1.1). There are two hosts: the box's own bodies
(`dos.asm` under `%ifndef KD_BACKEND`, through `OSAPI_*` cells) and
`kern_dos`'s (`kerndos/kdback.inc`, straight into the kernel's disk layer). The
core that calls the doors is one source, assembled once and joined to either
host (SPEC.md 96.44).

### 1.1 The handle and the window

- **8 records of 23 bytes** in the core's bss (`dos.asm:12773-12811`, table at
  `DOS_B_FHTAB`): `FH_NAME` (13), `FH_FLAGS`, `FH_POS` (dd), `FH_SIZE` (dd),
  `FH_VOL`. No folder, no cursor, no token. `dos_fh_new` hands a record out
  ZEROED (`dos.asm:16792`).
- **One window for all eight** (`dos_fh_setup`, `dos.asm:12888`): 8 KB rounded
  to the cluster, carved off the top of the program's arena, owned by one
  handle at a time (`[dos_wown]`); `dos_fh_take` flushes and resets it when
  another handle asks (`dos.asm:17504`).

### 1.2 Reads

`AH=3Fh` -> `dos_fh_rdloop` -> `dos_fh_fill` (`dos.asm:17535`). A miss aligns
the window's base down to the cluster and calls `DBE_RDAT` (READ_AT) with the
window as the buffer and `[dos_wbytes]` as the capacity (`dos.asm:17576-17596`),
so READ_AT's alignment rules hold by construction. The same refill is the
read half of an in-place write's read-modify-write. A compressed file
(`FHF_WHOLE`) is read whole with `OSAPI_FILE_READ` instead - unchanged here.
The shell's COPY (`dsh_stream`, `apps/dos/dosh.inc:1044`) and TYPE
(`dsh_c_type`) read with `DBE_RDAT` too, a chunk at a time.

### 1.3 Writes

- **A created handle (`3Ch`) is an append-only accumulator**
  (`dos_fh_wrloop`, `dos.asm:16587`): the window fills, and a full window is
  flushed by `dos_fh_flush` (`dos.asm:17407`) - the FIRST flush a
  `DBE_WRITE` (create or replace), every later one a `DBE_APPEND`, each a
  cluster multiple until the last at close.
- **An opened handle (`3Dh` mode 1/2) is a view** (`dos_fh_wiloop`,
  `dos.asm:16354`): the window refills, takes the bytes, and flushes with
  `DBE_WRAT` (WRITE_AT) at its base. Growing past the last cluster
  (`.iappend`, `dos.asm:16486`) flushes, drops `FHF_INPLC`, and hands on to
  the accumulator, so from there on the flushes are APPENDs.
- The shell's COPY writes `DBE_WRITE` then `DBE_APPEND` per chunk, hopping to
  the source and back on every chunk (`dosh.inc:1049-1092`).

### 1.4 Context

The box has **no worker**. A program runs inside `OSAPI_FSX_RUN` on the UI
task **with the gfx lock held from launch to exit** (`dos_run`,
`dos.asm:1451-1464`), and every file call is made on that task. So SPEC.md
20.6 rule 7 is met trivially - and, for this plan, a HELD WRITE_SEQ stream
can never be committed by the gfx unlock until the program has exited. Its
commit points inside a run are the ones SPEC.md 18.4.9 lists: a close
(`CX=0`), a cold call, and any other write on that volume.

### 1.5 kern_dos

Its bodies are `dskw_read_at_x`, `dskw_append_x`, `dskw_write_at_x`
(`kdback.inc:50-72`). It has **neither stream slot**: READ_SEQ is `%ifdef
KERN_BIG` (`kernel/diskw.inc`) and the kdos root defines no `KERN_BIG`, and
`DSKW_WSEQ` is explicitly `%ifndef KD_BUILD` (`kernel/diskw.inc:158-162`).

---

## 2. What it costs

The walk is MEASURED once, on the read side: **141.9 ms a MB of offset** on an
XT-IDE fixed disk with 2 KB clusters (SPEC.md 18.4.8), which is **~0.28 ms a
FAT link**. Everything in this table is that rate times a link count - an
ESTIMATE, and a floppy's link is not the same price as a fixed disk's (the
FAT sits in a different window; nobody has timed it):

| workload | refills or appends | links walked | walk (ESTIMATE) |
|---|---|---|---|
| read a 1 MB file, 2 KB clusters, 8 KB window | 128 | ~32,800 | **~9 s** |
| read a 300 KB file off a 1 KB-cluster floppy | 38 | ~5,700 | ~1.6 s |
| write a 1 MB file, same disk, 8 KB appends | 127 | ~32,500 | **~9 s**, plus a commit per append |
| write 300 KB to a floppy | 37 | ~5,500 | ~1.5 s, plus a commit per append |

The commit per append is the other half and it is MEASURED: PERFORMANCE.md Set
24 has 8 KB appends against one write at **2.36x on the floppy and 3.81x on
the ST-225**, 22 `int 13h` calls becoming 126 - before any file is big enough
for the walk to matter. SPEC.md 18.4.9's own figures for VIDDISK's 12.5 MB
are APPEND 570 s, PLAIN stream 204 s, HELD stream 191 s.

**Why §96.24.1.1's refusal does not decide this.** That section priced the
re-stat and the re-walk in `int 13h` calls with the cache alive and found
them free, and docs/plans/DISK-CPU-PLAN.md §3.1 reversed it on CPU: on Prince
of Persia the OS spent **9.3 s against IBM DOS 3.30's 3.5**, ~2.5 of it the
stateless-by-name layer. Part of that 2.5 is `OSAPI_FILE_FIND` per ordinal at
OPEN, which this plan does not touch (section 8); the rest is the READ_AT
re-stat and re-walk this plan removes.

---

## 3. Three pre-existing defects, found on the way

All three are read out of the code (both surveys and a check of the lines
cited); wave 0 confirms each with a probe before anything is fixed.

1. **A handle forgets its folder.** `3Dh` walks a path with `dos_fh_enter`
   and walks back with `dos_fh_leave`, but the record keeps only the bare
   name and the volume; a refill (`dos_fh_fill` `.onvol` -> `dos_vol_to`,
   `dos.asm:17657`, `17257`) stands in the drive's CURRENT folder. So a file
   opened as `SUB\X.DAT` from the root, or read after an `AH=3Bh`, is looked
   up in the wrong folder on its first refill - the wrong file, or none.
   **It binds this plan**: a hot READ_SEQ cursor would read the right
   clusters regardless, and a cold one - any remount - would then re-seed
   by name in the wrong folder, so the defect would go from always-wrong to
   intermittently-wrong. Fixed first: an `FH_DIR` word, stood in on refill
   and flush.
2. **A dirty window is lost at exit.** `dos_terminate` / `dos_prog_done`
   (`dos.asm:4520-4549`) never flush; `dos_fh_setup` clears the window at the
   next launch. A program that exits without closing loses up to 8 KB, where
   DOS closes every handle of a terminating process. **It binds this plan**:
   under a HELD stream the same exit would also leave the FAT and entry of
   everything BEFORE that window uncommitted until the gfx unlock. Fixed
   first: exit flushes and closes every open handle, which is DOS's rule.
3. **The comment at `dos.asm:3130-3134` says the access mode is not
   honoured**, and 20 lines below it is (SPEC.md 96.11.6). Stale text only.

---

## 4. The design

### 4.1 The state lives in the handle record

```
FH_NAME  13   (unchanged)
FH_FLAGS  1
FH_POS    4
FH_SIZE   4
FH_VOL    1
FH_DIR    2   NEW - the folder the name resolves in (defect 1)
FH_TOK    2   NEW - the WRITE_SEQ token, 0 = cold
FH_CUR   16   NEW - the READ_SEQ cursor; only +12 is ours (SPEC.md 18.4.8)
         --
         43   (23 today) - 8 records, +160 bytes of core bss
```

**Why in the record and not beside the window**, which would be 18 bytes and
not 144:

- `dos_fh_new` already zeroes the record, so a new file on a reused slot
  starts with a cold cursor and a cold token. SPEC.md 18.4.8.1's one rule the
  helper cannot keep - *zero the cursor when the FILE changes* - is then
  satisfied by code that exists, rather than by a new call at every open that
  a later open path could forget.
- **The window thrashes by design**: a program copying A to B alternates two
  handles, and `dos_fh_take` resets the window on every switch. A cursor
  beside the window would be zeroed at every switch and every refill of A
  would walk from the front - the common copy loop would gain nothing. Per
  handle, A's cursor stays where A's last refill left it.

### 4.2 Two new doors, two bodies each

| door | registers | box body | kern_dos body |
|---|---|---|---|
| `DBE_RSEQ` | READ_AT's (`SI` name, `ES:BX` buffer, `CX`, `DX:AX` offset) + `DI` = the cursor in the core's `DS` | `call os88_rseq` - it IS this contract, and its `FERR_NAME` fallback covers `kern_small` | `dskw_read_at_x`, `DI` ignored, until wave 5 |
| `DBE_WSEQ` | APPEND's (`SI`, `ES:BX`, `CX`) + `AL` flags + `DI` token in/out | `OSAPI_FILE_WRITE_SEQ`; on `FERR_NAME` (the small kernel) `OSAPI_FILE_APPEND` | `dskw_append_x`, `DI` passed back unchanged, until wave 5 |

`DBE_NENT` goes 22 -> 24 in both tables, in the same order; `t_dosseam` and
`t_kdfar` are the gates that already enforce that. The old `DBE_RDAT` and
`DBE_APPEND` stay: `dos_fh_shrink` and the prefix-read paths are single calls
that gain nothing.

### 4.3 The read side

- `dos_fh_fill`'s refill: `DBE_RDAT` -> `DBE_RSEQ` with `DI` = the owner's
  `FH_CUR`. The offset stays the core's (`[dos_wbase]`), so a seek, a
  re-read and a retry after an error need nothing (SPEC.md 18.4.8.1).
- The shell's COPY and TYPE: one cursor in the shell's own state, zeroed per
  file.
- `dos_load` (one whole-file `READ`) and `FHF_WHOLE` are unchanged.

### 4.4 The write side

- `dos_fh_flush`'s accumulator arm: the FIRST flush stays `DBE_WRITE` (the
  file has to exist before WRITE_SEQ will take it, SPEC.md 18.4.9); every
  later one is `DBE_WSEQ` with `FH_TOK` - where it was `DBE_APPEND`.
- **The stream is CLOSED** (`DBE_WSEQ`, `CX=0`) at: `AH=3Eh`, `AH=0Dh`,
  `AH=68h`/`6Ah` (commit - dispatched today as invalid, so this ADDS them, as
  flush-and-commit of one handle), a process's exit (defect 2's fix), and
  `dos_fh_shrink` before it touches the file.
- **In-place (`FHF_INPLC`) flushes stay `WRITE_AT`.** There is no sequential
  slot for an overwrite in the middle, and SPEC.md 18.4.9's other-write gate
  means such a WRITE_AT on the same volume commits any held stream first,
  so the two mix correctly - the stream is merely cold at its next call.
- The shell's COPY: the same, one token.

### 4.5 HELD or PLAIN - the owner's decision

| | HELD (`WSEQF_HELD`) | PLAIN |
|---|---|---|
| cost a flush | data only; FAT + entry once, at the close | FAT + entry every flush, as APPEND today |
| measured (VIDDISK, 12.5 MB, XT-IDE, SPEC.md 18.4.9) | 191 s, **0.2** one-sector writes an append | 204 s, 3.2 one-sector writes an append |
| gain on a floppy, 8 KB flushes | Set 24's 2.36x on the commits | the walk only |
| a crash or a power cut mid-file | the file is what the last commit said: the FIRST flush's 8 KB, or whatever the last close left | everything up to the last flush survives |
| a floppy swapped with the file open | the stream is lost (SPEC.md 18.8.5's bank) and the next call answers `FERR_IO` -> DOS error 5 | each flush landed or failed on its own |
| what a real DOS does | **this** - the directory entry is written at close or commit, FAT sectors sit in DOS's buffers, and a crash leaves lost clusters and an old size | - |

**Recommendation: HELD.** It is what a DOS program was written against, it is
the larger win on the machine this is for, and the commits it defers are the
ones DOS deferred. PLAIN is one flag away (the `AL` passed to `DBE_WSEQ`), so
the choice does not shape anything else in the design.

### 4.6 The one hazard HELD adds: a handle reading back its own tail

A held stream's directory entry keeps its LAST COMMITTED size, and a read by
name - READ_AT or READ_SEQ - is bounded by that entry: *a read commits no
hold*. This is the exact reason the compressor's second pass is PLAIN and not
HELD (SPEC.md 22.22.5.1), so it is a known trap and not a guess. So a handle that has flushed held windows and then
seeks back and misses the window would be handed a short file by its own
refill. **Rule: a refill by a handle whose `FH_TOK` names a live hold closes
the stream first** (`DBE_WSEQ`, `CX=0`), and the next flush opens a new one,
cold. One compare in `dos_fh_fill`, one commit per read-back - which is when
DOS itself would have been asked to know the size.

A SECOND handle opened on the file sees the old size until the first closes.
So does DOS: it reads the size out of the directory at open, and the writer
has not updated it yet. That is left as DOS has it.

### 4.7 Volumes, hops and swaps

| event | READ_SEQ cursor | WRITE_SEQ hold |
|---|---|---|
| a refill of the same file, same volume | hot: one FAT link | - |
| the box's own `WRITE_SEQ` flushes to another file | hot (a held call moves no generation) | - |
| a PLAIN write, `WRITE_AT` or `DELETE` anywhere | cold once (one READ_AT's walk) | committed first, then cold |
| a hop to another floppy and back, outside a batch | cold (the mount re-reads a boot sector) | banked across the hop (SPEC.md 18.8.5) |
| the floppy swapped under the file | re-seeds by name on the NEW disk and reads what that disk has, as READ_AT would today | lost -> DOS error 5 |

Nothing in the table is slower than today: a cold call costs exactly the
READ_AT or APPEND it replaces.

---

## 5. What it costs

| where | what | bytes | who pays |
|---|---|---|---|
| core bss | `FH_DIR`, `FH_TOK`, `FH_CUR` x 8 | **+160** (MEASURED arithmetic; the budget has **0** slack - `CORE_BSS_SIZE` 3,328 is exact) | the DOS program, in both hosts |
| core code | two door stubs, the refill and flush edits, the close points, `68h`/`6Ah`, the exit sweep, the folder stand | ESTIMATE ~150-200, against **131** of `CORE_MAX` slack (16,253 of 16,384) | the DOS program, in both hosts, if `CORE_MAX` moves |
| box host | two bodies + `os88_rseq` (76) | ESTIMATE ~110 | the box's region |
| kern_dos host, waves 1-4 | two bodies that fall back | ESTIMATE ~15 | `KD_IMG_KB` |
| kern_dos, wave 5 | READ_SEQ (~190) + WRITE_SEQ's gated code (**418 + 21 bss**, MEASURED off the kern_big listing) + the disk-layer hooks | ESTIMATE ~650-750 | `KD_IMG_KB` |
| kern_big resident | nothing - every slot already ships | **0** | - |

**Two things about `kern_dos`'s image rung are worth knowing now.** It
measures **34,780 bytes against `KD_IMG_KB` 35 (35,840)** - 1,060 bytes spare,
and **36 bytes under the 34 KB rung**. Setting `KD_IMG_KB` to 34 today would
hand every `kern_dos` program a kilobyte back, with nothing in front of it but
the 36-byte margin; wave 5 spends that kilobyte instead. Which of the two is
worth more is the owner's (section 7).

---

## 6. The waves

Each wave is its own commit, gated before the next starts. Rows named here
are soak rows; `-k` them, never the tier.

### W0 - measure and confirm (no shipped byte)

- **The cost, per position, in guest cycles.** `tests/dostrap/diskcost.asm`
  counts `int 13h` from outside; the walk is CPU, so the new probe
  (`seqcost.asm`, the same shape: parameters, key wait, run under a real DOS
  as well) reads N x 8 KB from offset X and writes N x 8 KB to a new file, and
  the host brackets it with MartyPC's cycle counter. Points at X = 0,
  256 KB, 1 MB on `os8088_5150_herc_hdd_sb_gla`, and the 360 KB floppy.
  **The prediction to check is the table in section 2**: per-read cost
  rising linearly with X, per-append cost rising linearly with the file.
- **Prince of Persia's split, re-taken** with DISK-CPU-PLAN §1's sampler on
  today's tree, so wave 6 has a before that is not two months and eight size
  passes old.
- **The three defects as probes**, each run against a real DOS for the
  reference: open `SUB\X.DAT` (> 8 KB) from the root and read past the first
  window; write 3 KB and exit without a close; and a handle that writes
  20 KB, seeks to 0 and reads it all back unclosed (section 4.6's case, which
  must pass today and after wave 3).

### W1 - the two defects that bind (behaviour, not speed)

`FH_DIR` and the stand on refill and flush; the exit sweep. Record grows to
25 bytes (+16 bss). Rows: W0's first two probes as rows, plus `dosfile`,
`dosdrv`, `dosgap`, `dosshell`, `dosexec`, `kdos`, `kdmix`. **Break it on
purpose**: drop the stand and the folder row must go red; drop the sweep and
the exit row must go red.

### W2 - the read side

`DBE_RSEQ` in both tables, `FH_CUR`, `dos_fh_fill`, the shell's COPY/TYPE.
Rows: `dosfile`, `dosdrv`, `dostype`, `dosshell`, `dosexe`, `kdos`,
`kdmix`, `kdbigexe`, `kdcwd`, `dosseam`, `kdfar`. W0's read probe re-run:
the per-read cost must stop rising with X. **Break it on purpose**: take the
zeroing out of the slot reuse (a test hook, not a shipped path) and the
reuse row must read the wrong file - which proves the rule is load-bearing
and the row can see it.

### W3 - the write side, HELD (or PLAIN, section 4.5)

`DBE_WSEQ`, `FH_TOK`, the flush, the close points, `68h`/`6Ah`, section 4.6's
rule. Rows: `dosfile` (it crosses the window twice: one WRITE, two stream
calls, a close), `dosgap`, `dosshell`, `dosdev` (the `0Dh` flush), W0's
read-back probe. W0's write probe re-run: per-append cost flat. **Break it
on purpose**: remove section 4.6's close and the read-back row must go red;
remove the exit sweep's close and the exit row must.

### W4 - the shell and the edges

COPY's token and cursor (W2/W3 give it the doors), MOVE's fallback, and a
row for a swap mid-hold: the program is told error 5 and the disk's file is
the last commit, never a torn chain (host fsck of the image).

### W5 - `kern_dos` (only if section 7 says so)

READ_SEQ and `DSKW_WSEQ` into the `KD_BUILD` assembly, the two bodies
switched over, `KD_IMG_KB` measured. Rows: every `kd*` row.

### W6 - the after

W0's probes and the Prince sampler again, the numbers into SPEC.md §96 as a
new section and into this document's section 2 as MEASURED; this plan to
`docs/plans/completed/`.

---

## 7. The owner's decisions

1. **HELD or PLAIN** for a DOS program's writes (section 4.5). Recommended:
   HELD.
2. **`kern_dos`: wave 5 or the kilobyte.** ~700 bytes of stream slots in
   `kern_dos`, against returning `KD_IMG_KB` to 34 now (section 5).
   Recommended: take the kilobyte now, and do wave 5 only if W6's numbers on
   the box say the stream is worth more to a `kern_dos` program than 1 KB of
   its memory.
3. **`CORE_MAX`**: if the core edits come to more than the 131 bytes of
   slack, the constant moves, and both hosts' programs pay those bytes. Wave 2
   measures and says so before it commits.

---

## 8. Not in this plan

- **The open's half of DISK-CPU-PLAN §3.1**: `dos_fh_stat` enumerates
  `OSAPI_FILE_FIND` by ordinal, which re-walks the directory per ordinal
  (SPEC.md 96.24.1). A stat-by-name slot would fix it and is a kernel ABI
  decision §96.24.1.1 already refused once; it wants its own measurement.
- **The window's thrash itself**: two handles alternating each flush a
  partial window and refill a whole one. A second window would take that
  out; this plan makes the thrash cheaper and leaves it in place.
- **Overwrites in the middle** (`FHF_INPLC`): WRITE_AT still walks to its
  offset. A sequential overwrite slot does not exist and is not proposed.
- **FCB reads and writes**: not implemented at all today (they answer
  invalid function), so there is nothing to convert.

---

## 9. What building it found

**The result, measured** (`tests/dosseq.py`, SPEC.md 96.53's table): on a
1 MB file off an XT-IDE C:, the box's write 29.9 -> 16.4 s and read 19.8 ->
10.5 s, `kern_dos`'s 29.3 -> 16.4 and 19.1 -> 9.9, every block flat where it
climbed; on a 360 KB floppy the write 51.4 -> 28.2 s and the read unchanged,
the drive being the whole of it. Section 2's estimate of the read walk was
right to within the tick: 17 ms more per refill per 128 KB of offset,
against the 0.28 ms a link it was built from.

**The cost, against the 1.5 KB allowed**: `kern_dos`'s image +823 (inside
its existing 35 KB, 237 to spare, so no program memory); the core's bss +179
and code +123, the latter inside `CORE_MAX` with 8 bytes left; `kern_big`
resident +13. Section 5 had estimated the core's code at 150-200 and the
`kern_dos` arm at 650-750: the core came in under, the arm over, because
the FAT bank had to come with it (SPEC.md 96.53.1 says why).

**Five defects, three the box's and two the kernel's**:

1. **The folder** (section 3.1): worse than predicted. The refill that
   re-resolved the bare name ran at the very FIRST read, not the first
   refill, so `SUB\X.DAT` opened from anywhere else was the wrong file
   from byte 0 - and a file CREATED in a subfolder landed in the current
   one. SPEC.md 96.52.
2. **The exit** (section 3.2): as predicted, and worse for a small file -
   a file that never filled one window was not on the disk at all.
3. **Interleaved writers** - NOT in this plan, found by the probe written
   for 1 and 2: two created files written in turn in non-cluster chunks got
   `access denied` at the second round, because a window taken away mid-
   cluster flushed partial and the next append was refused. Section 8's
   *"the window's thrash itself"* was filed as a cost and was also a
   correctness defect. SPEC.md 96.52.
4. **A held call that runs out of room leaked what it took**: the rollback
   flushed its half-built sub-chain rather than freeing it, which on a full
   disk is every free cluster left. SPEC.md 18.4.9.1, +6 bytes.
5. **DELETE, MKDIR and RMDIR never committed a pending hold**: they open
   with `dskw_gate`, which returns through `dskw_mounted`'s `ret` without
   running its hold test - so a delete of the held file freed only the
   committed chain (345 lost clusters, measured) and left the hold to link
   a freed cluster later. Found by `tests/dosshell.py`'s out-of-space COPY,
   which the probe then reduced to eight lines. SPEC.md 18.4.9.2, +7 bytes.

4 and 5 are the stream writer's own, from the cycle before, and were
invisible to every writer that shipped then because none of them deletes
its own file mid-stream or streams onto a full disk. A DOS program does
both as a matter of course - which is the argument for putting the box on
a new kernel mechanism early rather than late.

**The rows**: `dosfix` (1-3), `dosfull` (4-5, verified red on each),
`dosseq` (the shape, both hosts). All three fail on the tree before this
work.
