# Floppy I/O and the FAT read side

Big review, 2026-11-08. Area key `diskread`. Read-only review of the tree on
`the-big-review` (cut from `elendilon`). Nothing was built or booted; every
claim is from source, with file:line.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/disk.inc` | 11,457 | all of it. The icon-store key builders (10093-10400) were read for logic; their comments were skimmed |
| `kernel/dskwin.inc` | 360 | all |
| `kernel/drvvol.inc` | 271 | all |
| `SPEC.md` §18 intro, 18.1-18.3, 18.8.2, 18.9.3 | ~450 | read; 18.4-18.101 and 19.x read only where a code comment cited them |
| `boot/boothd.asm` | 1-330 | the geometry and hand-off half, for dr-1 |
| `kernel/fprog.inc` 440-660, `kernel/vga12.inc` 5150-5220, `kernel/hiber.inc` 1660-1675 / 2775-2790, `kerndos/kdshim.inc` 540-600, `kernel/files.inc` 1575-1712, `tools/os88disk.py` 120-140 / 620-655 / 1465-1480 | spot reads to check cross-file claims |
| `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`, `docs/plans/DISK-CPU-PLAN.md` (1-260), `docs/FIELD-NOTES.md` 33 and the D-table at 60-75 | | checked for rows already listed or refused |

Build arms are named per finding: kern_big (`KERN_BIG`, `OS88_BIGVOL`,
`DSK_BIGDIR`, `OS88_IMG`, `OS88_DRIVERS`), kern_small (`KERN_SMALL`,
`DSK_FAT_SECS` = 2), kern_dos (`KD_BUILD`), kern_mega (`KERN_MEGA`).

## Verdict

The read side is in good shape as a mechanism. It has one transfer routine
for both directions and every volume kind. A hostile-BPB table guards every
divide that runs under `sch_lock`. One sector cache sits under `disk_read`,
invalidated at the one write choke point. The FAT window is keyed by a
signature that `os88disk.py` now makes unique per shipped image. Batching is
measured on the 5150, and so is the fix that stopped trusting AL.

Its weaknesses are at the edges, not in the core loop:

- The boot partition's mount geometry comes from a different source
  (`int 13h AH=08h`) from the one `boothd` and `dsk_bpb_check` use (the BPB).
  That is the FIELD-NOTES 33 disagreement, still live in the kernel.
- Two safety arguments are written down and not enforced: the batch
  bracket's "the caller holds the UI frozen", and the 64KB buffer bound.
- The retry ladder treats a read-ahead fill like a real read, and throws away
  status 06h, the one swap signal an AT gives for free.

The largest problem is the documentation, not the code. `disk.inc` is
about 85% comment. Its file header, the `disk_read`, `disk_mount`,
`dsk_sortdir` and `dsk_fdd_probe` headers, about ten orphaned headers for
deleted routines, and SPEC §18's intro, symbol tables and §18.3 mount
sequence all describe designs that have since been replaced:

- one sector per call
- the FAT snapshot
- `disk_icons`
- the fixed-address listing
- unit 1 only
- the 128KB FAT-window gate
- byte-identical boot sectors

A reader who trusts SPEC §18.3 today gets the wrong mount.

## Strengths

1. **One transfer routine, one retry policy** (`dsk_xfer`,
   `disk.inc:1387-1405`). Read and write differ by one byte, `[dsk_op]`. The
   CHS conversion, cylinder guard, DMA bounce, EOT write and retry ladder
   exist once. `.img` (`2458-2532`) and the cache fill (`7206`) re-enter it,
   so they get all of that free and keep no CHS code of their own. Copy this
   shape anywhere two paths are tempted to diverge.
2. **Divide safety is arithmetic, not hope.** Two checks:
   - `cmp dx, cx / jae .jfail` before the CHS `div` (`1761-1767`).
   - rule 14's `cmp dx, bx / jae` before the cluster-count `div`
     (`5269-5275`).

   Both turn a quotient overflow, which would be a #DE with `sch_lock` held,
   into a refusal. The derived layout is carry-checked at every add
   (`5233-5241`).
3. **The BPB table names the consumer each rule protects** (SPEC 18.2), and
   the code follows it in order. Several rules are written as range tests:
   rule 4's `x & (x-1)`, rule 6's and rule 7's "value-1 <= N", and rule 11's
   delta walk. Each is short and also correct at 0.
4. **`dsk_runcap`'s 17-bit negation** (`2596-2646`). "Bytes to the next page"
   is computed so that a value already at the boundary means a whole page,
   with no zero test. It covers both the DMA page and BX wrap. A run of 0
   becomes a one-sector bounce through `dsk_secbuf`, which is 512-aligned by
   an `%error` (`dskwin.inc:356`). This is the right place to solve 64KB DMA
   once.
5. **CF=0 is the contract, not AL** (`2075-2106`). The 5150 measurement that
   found AL=1 for a 9-sector read is the reason, and the A/B knob is kept.
   This is the kind of field-proven correction that should never be "tidied".
6. **The cache mirrors the disk** (`6654-6713`). The design is right:
   - `dsk_rah_wr` hangs off `disk_write_x`, so no future write can forget
     to invalidate.
   - It fills from the requested sector to the track's end, never from the
     track's start, by measurement.
   - It keeps a pollution gate (no fill when the request already covers the
     chunk) and skips a slot that straddles a page.
   - A one-sector `RAH_HIT` second chance stands in for LRU.
   - It was simulated against the install trace before it was built.
   - The width is solved from the free run and capped at a quarter of the
     whole arena.
7. **Size-driven chain walk with no EOC constants** (`dsk_read_chain_x`,
   `8913-9107`). Every pass consumes at least one sector, so a cyclic or
   cross-linked chain cannot hang it. Runs are kept in clusters, not LBAs, so
   32-bit volumes cost the walk nothing. `DSK_DIRW_MAX` (`6597`) gives the
   directory walker, which has no size, its own bound.
8. **The owned DPT** (`1291-1332`, `1993-1994`). The table is copied rather
   than replaced, so this machine's timings stand, and EOT is set to the
   volume's own spt before every call. This is DOS's fix, done properly,
   with a `DPTROM=1` arm kept for AT tables that swap per medium.
9. **The presence probe fails toward keeping a drive** (`10700-10971`). It
   retires a drive only on the positive ST0=7x absence shape, checked
   against the field 5150 and the Victory. The asymmetric-cost argument in
   its header is the model for any destructive probe.
10. **The instruments ship with the design.** The published 'DD' block, the
    (LBA, run) trace, the per-phase counters, the `dsk_flstr` toast template
    with a label per field (`693-720`) and the fdd ST3/ST0 rows. These are
    why the AL bug, the head-crossing write failure and the ST0 discriminator
    were findable from the field.
11. **Layout couplings are `%error`s, not comments.** For example:
    `dsk_lbahi` following `dsk_rah_busy` (`6902`), `RAH_REC == DSK_RAH_SECS`
    (`6834`), `IMG_DVSEG` inside `DV_LBL` (`347`), `VT_* == DVK_*`
    (`3252`), the `dsk_img_swap` byte order (`2570`) and the
    `MEM_P_FATW_N >= DVOL_MAX` purge range (`247`). Worth copying into
    every module that reads two cells as one.

## Findings

Ranked most valuable first. Phase: **now** means no ABI break, **flagday**
means it needs one, **record** means write it down without recommending it.
Bytes are signed estimates, not measurements.

### dr-1 - The boot partition is mounted with `AH=08h` geometry while the volume is addressed with the BPB's (hazard, medium, now)

- `dsk_boot_from_x` asks `int 13h AH=08h` and stores
  `dsk_bootspt`/`dsk_boothds` (`disk.inc:2796-2807`).
- `disk_mount_x` uses that pair for the one read that fetches the BPB
  (`4076-4081`, LBA 0 plus the partition base).
- `dsk_bpb_check` then overwrites `disk_spt`/`disk_heads` with the BPB's
  fields (`5327-5330`), and that geometry addresses everything after.
- `boot/boothd.asm:16-31` says in as many words that `AH=08h` was the wrong
  invariant (FIELD-NOTES 33): "the geometry that must be used is THE ONE
  THAT WROTE THE VOLUME", and `dsk_bpb_check` is cited as the model.

So on exactly the machine FN33 describes, the kernel's first read lands on a
different sector and the BPB fails. That machine is a volume written with
one geometry on a drive whose ROM answers another, or does not answer.
`dsk_bootspt` = 0 drops to the floppy fallback, 9/2, which is worse. The
symptom is the one the comment at `4097-4107` already describes: the machine
boots and C: never mounts. The same pair feeds hibernate
(`hiber.inc:1669-1672`, `2782-2785`).

**Recommendation.** Hand the BPB's geometry over from `boothd`: it already
holds `[spt]` and `[bpb_heads]`. Use two registers that the hand-off leaves
free, for example SI = spt and DI = heads, plus a marker in AX so that an
older `BOOTHD.SYS` is detected. In `dsk_boot_from_x .hard`, take them when
the marker matches and fall back to `AH=08h` otherwise. Once every shipped
`boothd` carries the marker, delete the `AH=08h` arm.

**Bytes.** +8 in `boothd` (it has about 18 spare). About +6 `.ovl` now, and
about -28 `.ovl` once the fallback goes. 0 resident.

**Verify** on the FN33 dumped images in 86Box before and after; the
prediction above is from reading, not from a run.

### dr-2 - The batch bracket's safety argument is not enforced at the door packages can call (hazard, medium, now)

`dsk_batch_begin_x` (`940-953`) sets the batch unconditionally. With a batch
open, a floppy's banked BPB and signature are trusted with no LBA 0 read
(`1028-1091`), and the banked FAT window and cache are trusted with them
(`5838-5848`).

The whole argument for that is "any unlocking of the user interface ends it"
(`913-919`, SPEC 18.9.3, `vga12.inc:5191`). It assumes the caller is inside
a gfx-lock hold. But:

- The door is a published slot, `OSAPI_BATCH_BEGIN` 0x02B0
  (`kernel.asm:3217`, `os88api.inc:6071`), and has a C SDK thunk
  (`apps/cc/os88thunk.asm:2054`).
- `fprog.inc:633-646` records that package file I/O does not hold the mutex,
  with FTPD as the example.

So a package that begins a batch outside a lock keeps it open until the next
`gfx_unlock` anywhere. On an idle desktop that can be a long time, and a
floppy swapped in that window is written with the previous disk's FAT.

**Recommendation.** `dsk_batch_begin_x` should do nothing unless
`[gfx_lock_flag]` is set and `[gfx_lock_own]` == `[sch_cur]`. This is the
same three instructions `fpg_arm` uses (`fprog.inc:616-620`). A refused
begin is just slower and never unsafe.

**Bytes.** About +12 `.cold`.

### dr-3 - A failing read-ahead fill runs the whole retry ladder, and then the direct read runs it again (weakness, medium, now)

- A cache miss fills to the track's end through `disk_read_x`
  (`7206-7208`).
- The fill gets `dsk_xfer`'s full ladder: 3 attempts at the run, then 3 at a
  single sector, with a controller reset between each (`2025-2073`).
- If any sector of the speculative tail is bad, the fill fails, the slot
  stays empty, and `dsk_rah_serve` hands back CF=1 (`7023-7029`). The caller
  then reads only what it wanted, with the ladder again.
- Nothing is cached, so the next read anywhere on that track misses and
  refills, and pays the failing tail's attempts again.

One marginal sector at the end of a track therefore taxes every access to its
good neighbours with three or more failed attempts and resets. That is the
"30 seconds of grinding" shape from SPEC 12.8.3.2, caused by a sector nobody
asked for.

**Recommendation.** A fill should fail at its first error, with no reset and
no ladder, and leave the retrying to the direct read of the sectors that were
wanted.

`[dsk_rah_busy]` cannot be the test as it stands: the mount's swap-detector
read of LBA 0 (`4202-4204`) also raises it, and that read must keep its
ladder. So mark the fill distinctly, for example `add byte [dsk_rah_busy], 2`
and `sub` rather than `inc`/`dec`. Then test bit 1 at the top of `dsk_xfer`'s
failure arm.

Optionally, cache the good prefix: after a run failure, a fill that has
already advanced DI has those sectors in its slot.

**Bytes.** About +10 `.cold`.

### dr-4 - Status 06h ("media changed") is retried as a generic error and discarded (hazard, medium-low, now)

On an AT-class machine the BIOS returns AH=06h on the first access after the
change line rose. `dsk_xfer` treats it like a CRC error: reset and retry
(`2044-2056`). The retry succeeds, so the one hardware swap signal the
machine gives is lost.

The change line was considered before as a *replacement* for the motor
heuristic, via an `AH=16h` call, and declined because a 5150 with a TM100 has
none (FIELD-NOTES 3, `docs/FIELD-NOTES.md:63-67`). This is the
other use of it: additive and free. The status arrives on a read already
being made, so an XT simply never reports it.

**Recommendation.** In the failure arm, on AH=06h with `[dsk_mntok]` = 1,
which means inside a validated mount and not the mount's own LBA 0 read:

1. Clear `[dsk_mntok]`, which closes the write gate, so the next operation
   remounts.
2. Fail the transfer with `[dsk_ioerr]` = 06h and no retry.

During a mount (`[dsk_mntok]` = 0) keep today's retry, since the mount is
the re-validation anyway. This closes the write-after-swap case on every AT
whose user beats the two-second motor timeout.

**Prior art.** DOS 3.2+ "Invalid disk change" (int 24h). AmigaOS broadcasts
a disk-change interrupt to the file system.

**Bytes.** About +16 `.cold`.

### dr-5 - `OSAPI_FILE_FIND` stays quadratic, but the kernel already has the resume-from-slot walk a cursor needs (rethink, medium, now)

**Already listed** as DISK-CPU-PLAN.md 3.1: about 2.5 guest seconds of
re-walking on the Prince of Persia load.

**The new argument is the price.** On kern_big, `dsk_dscan` (`7886-7974`)
already takes a raw slot to start from (`CX`) and answers the slot to
continue at. `fcp_scan` uses it exactly that way. `dsk_find_x` calls it with
`CX = 0` and an ordinal (`8034-8038`), which is the only reason a listing of
N entries costs N(N+1)/2 entry visits.

A `FILE_FIND_SEQ` would follow `READ_SEQ`'s caller-held 16-byte cursor
(§18.4.8): the folder cluster, the raw slot and `[dsk_mgen]`. It is a thin
wrapper:

- Check the generation.
- Call `dsk_dscan` with (slot, 0).
- Write the next slot back.

Because the cursor is caller-held, it survives the "find, read, write, find"
installer loop that `dsk_find`'s header rejected a kernel cursor for
(`7988-7996`).

**Prior art.** DOS FindFirst/FindNext keeps the search state in the caller's
DTA, which is the same answer.

**Bytes.** About +45 `.cold` and +6 for a cell at the end of the table. No
ABI break. kern_big only (`DSK_DSCAN`).

### dr-6 - Fixed overhead on every transfer, cache hits included (weakness, low-medium, now)

The head of every `dsk_xfer` (`1414-1560`) does, on every call:

- `dsk_media_ok`: a row lookup plus two BIOS-data-area reads
  (`1419-1423`).
- a far call to `fpg_busy` (`1434`).
- a second `dsk_vol_row_x` (`1497-1516`).
- the `dsk_vbase` and cylinder-bound loads (`1546-1560`).

That is about 600-700 cycles, roughly 0.14 ms on the 5150. It is paid by
every cache hit, whose real work is a 1.6 ms copy, and by the
`.img` and fill re-entries.

**Already listed** as DISK-CPU-PLAN §7: `fpg_busy` plus `fpg_step` measured
at 0.30 s.

**New suggestion.** `dsk_media_ok` is only asked in order to pick
`FPG_WARM` for the widget. Skip it when the widget is already armed
(`FPG_ARMED`, two instructions), and skip the second row lookup by having
`dsk_media_ok` hand back the row.

**Bytes.** About ±0. Saves about 250 cycles a transfer.

### dr-7 - A run past the buffer's segment wraps silently (hazard, low, now)

`dsk_runcap` caps each call at BX's wrap, but `dsk_xfer` then carries on from
ES:0000 (`.notched`, `2150-2157`): the next cap is a full 128 sectors from
offset 0. "A transfer is at most 128 sectors" (`1430-1432`) is prose, and so
is every caller's "ES:BX budget must cover count*512" (`1241-1242`). The
callers bound it today: they convert a 16-bit byte count through
`dsk_bytes_secs_x`. A future caller that does not overwrites the start of its
own buffer, with CF=0.

**Recommendation.** At `.notched`, an `add bx, ax` that carries while SI != 0
means the next sector has nowhere to go, so fail. The test runs once per
int 13h call, not per sector.

**Bytes.** About +6 `.cold`.

### dr-8 - `dsk_rah_fill` computes the track phase on the volume-relative LBA (hazard, low, now)

The fill's "sectors left in this track" divides DI by `disk_spt` (`7095-7099`).
The transfer itself converts DI plus `[dsk_base]` (`1759-1760`).

On a floppy and on a driver volume the base is 0, so the two agree. On a
BIOS boot partition whose base is not a multiple of spt, they disagree, and
the fill is mis-sized. Either it under-fills, or `dsk_xfer` splits it at the
real track end into two int 13h calls. The data is correct either way.

`boothd`'s installer layout starts at head 1, sector 1, so it is aligned. A
partition from a foreign `FDISK` aligned to 1MB (2048 mod 63 ≠ 0) is not.

**Recommendation.** Add the base's low word into the dividend before the
`div`. `[dsk_base]` was already loaded by this transfer's head.

**Bytes.** About +8 `.cold`.

### dr-9 - `dsk_find` hands a redirected volume's name back unterminated if the driver's is 13+ bytes (hazard, low, now)

- `osapi_fs_ent` forces a NUL at +15 for every listed entry (`3807`).
- `dsk_find`'s FSV_ENUM arm takes the driver's staged entry straight into
  `dsk_ent` and copies 13 bytes out (`8110-8119`, `8196-8199`), with no
  NUL forced.

A driver name of 13 to 15 characters reaches the package with its NUL
missing, and +13 is the attribute byte.

**Recommendation.** Write `mov byte [es:di-1], 0` after the `rep movsb`.

**Bytes.** +4 `.cold`.

### dr-10 - `.bounce`'s "no recursion" argument contradicts the routine's own two re-entries (consistency, low, now)

The bounce explains that it is "NOT a recursive dsk_xfer: its head calls
fpg_busy, which may draw, and a draw inside this sch_lock can wait on a gfx
lock another task holds" (`1890-1894`). Two things contradict it:

- The cache fill (`7207`) and the `.img` arm (`2516`) both re-enter
  `dsk_xfer` inside the outer `sch_lock`.
- `fpg_arm` refuses, rather than waits, when another task owns the lock
  (`fprog.inc:616-620`). So the draw cannot wait.

What *can* happen is the first chrome draw landing inside the lock with ES
pinned. That is what `1449-1453` says it avoids. For example, a 1-sector
miss accumulates `fpg_warm` below `FPG_WARM`, then the inner 9-sector fill
crosses the threshold and arms.

**Recommendation.** It is harmless but should be stated correctly. Either
pass CX = 0 for the inner call (no accumulation), or rewrite the bounce
comment to give the real reason the bounce stays in-line, which is that
`[dsk_bnc_*]` is one set of cells.

**Bytes.** 0 if the comment is fixed; about +3 to pass CX = 0 for a fill.

### dr-11 - A listing mount flushes every volume's cached chunks, not only its own (perf, low, now)

`dsk_rah_flush` clears all `DSK_RAH_RUNS` records (`4181-4186`, `7259-7272`).
So a Refresh or navigation on A: drops B:'s cached directory and data
sectors, although B:'s disk has not changed.

The `.resig` loop (`4227-4233`) then stamps *every* record with the new disk's
signature, which is safe only because the cache is empty.

**Recommendation.**
1. Drop by volume: `dsk_rah_wr` with AX = 0, CX = 0xFFFF, which already
   matches the volume.
2. Make `.resig` stamp only records whose `RAH_VOL` is `[disk_drive]`.

**Bytes.** About +10 `.cold`.

### dr-12 - The retry ladder does its single-sector fallback after a TIMEOUT too (weakness, low, record)

AH=80h (drive not ready or no index) on a multi-sector run costs 3 attempts
and resets, then 3 more at one sector (`2057-2066`). On a 5150 each timeout
costs seconds.

The fallback exists for a bad *sector*. A timeout is about the drive, and
one sector will not fare better. There is a counter-case: the head-crossing
write that failed with 80h (`1820-1834`), although writes no longer cross.

**Recommendation.** For 80h, one attempt at the run and then one at a single
sector, not 3 + 3.

**Bytes.** About +6.

**Recorded rather than recommended**, because the no-disk case mostly hits
the mount's 1-sector LBA 0 read, which already stops at 3.

### dr-13 - `dsk_boot_from`'s 255-head guard is dead (bug, low, now)

```nasm
    mov al, dh                  ; heads = last head + 1
    xor ah, ah
    inc ax
    jz .nogeom                  ; DH = 0FFh is not a geometry
```

AX is at most 256 and is never zero, so the `jz` is never taken
(`2803-2806`). 256 heads (DH = FFh) is accepted, which is legal CHS. Either
delete the guard or make it what the comment says (`inc al`).

**Bytes.** -2 `.ovl` to delete it.

**Superseded by dr-1** if the `AH=08h` arm goes.

### dr-14 - `dsk_fdd_probe` still clears unit 1's calibrated bit whatever unit it probed (consistency, low, now)

`.out` runs `and byte [es:0x3E], 0x7D` with the comment "re-seek unit 1"
(`10962`). §18.98 made the probe run for units 1 to 3 (`10772`).

After its own RECALIBRATE, the 765's PCN for the probed unit is 0 and true,
so the BIOS has nothing to re-learn. That leaves the bit-1 clear either
pointless (unit 1) or aimed at the wrong unit (units 2 and 3).

**Recommendation.** Clear bit 7 alone. That is -0, by changing the
immediate. Or derive the mask from `fdd_unit` (+6 `.ovl`).

### dr-15 - `dsk_rah_fill` reads `[dsk_totsec]` before the mount that owns it (hazard, low, record)

The listing mount reads LBA 0 *through* a fill (`4220-4223`). The fill clamps
against `[dsk_totsec]` (`7115-7122`), which at that moment is the previous
volume's value. Before the first FAT mount of a session it is `.bss` garbage
(`11325`).

It is benign: the clamp can only shorten the fill or ask for one sector. But
it is a `.bss` read before write that nothing states.

**Recommendation.** Write a comment. Or seed `dsk_totsec` = 0xFFFF in
`.text`, a word moved from `.bss` (0 net).

### dr-16 - The documentation drift in this area is a defect of its own (consistency, medium, now)

See *Doc/code mismatches* below for the list, about 40 entries. Three
patterns account for most of them:

- **Orphan headers.** These routines are deleted, yet each keeps a full
  contract block: `dsk_vol_kind` (`3293`), `osapi_vol_paint` (`3868`),
  `dsk_relist` (`8603`), `dsk_copy_low` (`9171`), `dsk_copy_out`
  (`9435`), `dsk_put_icon` (`9513`). So does `dsk_sortdir` (`5562`), whose
  body was inlined.
- **Running counts and caps** that moved: the 32-entry listing, 0..31
  indices, "64 on kern_big", the 128KB gate, unit 1, 9/2 for every
  LBA 0 read.
- **Retired designs described as current**: the FAT snapshot, one sector
  per call, `disk_icons`, the `.lowbss` listing, byte-identical boot
  sectors.

`checkdocs.py` cannot see any of these because none is a dangling §
reference.

**Recommendation.** Do one sweep. Fold each orphan header into a single
"retired routines" line. Replace counts with the constant's name, for
example "`DSK_NENT`" rather than "32".

**Bytes.** 0.

### dr-17 - `dsk_up_open` writes index 0 without asking `[dsk_nmax]` (hazard, low, record)

The '..' link is stored before the scan's cap check (`5533-5560`, `4369`).
That is correct only because every aim supplies room for at least one entry
(`files.inc:1696-1701`: `FS_VKB*32`), which is prose in another file.

**Recommendation.** Leave as is. Or skip the link when `[dsk_nmax]` is 0
(+5).

## Rethinks

- **R1. Mount and listing are one routine.** `disk_mount_x` does three
  jobs: validates the volume, builds the caller's listing, and harvests
  icons (`4012-5010`). It has grown quiet flags (`dsk_quiet`, a zero
  `dsk_dseg`) to switch jobs two and three off.

  DOS, OS/2 and Mac separate "the volume is valid" (media check, VCB) from
  "list a directory", which is FindFirst/FindNext or PBGetCatInfo in a loop
  owned by the shell.

  The cleaner shape is `disk_mount` = steps 1-2b, with the
  scan/sort/harvest as a separate routine that `fmv_load` calls. The code
  is already split at `.listing`, so this is mostly moving a label and a
  call. Resident bytes are about ±10. The gain is that "mount" stops having
  two contracts. **now**, and worth doing at the same time as dr-16's sweep.

- **R2. Three caches where DOS has one.** The FAT window, with per-volume
  claims, a pin, signatures, park/pick/want/evict/demote and dirty ranges
  (`5761-6296`, about 500 bytes of `.cold`), sits beside a sector cache
  that already serves the FAT window's loads (`6656-6657`).

  DOS's BUFFERS= pool holds FAT, directory and data sectors uniformly, with
  a dirty bit and a drive tag. A unified buffer cache with dirty bits would
  delete the window's machinery. But it would cost the direct ES:BX
  addressing `dsk_next_clus_x` relies on: FAT12 entries straddle sectors,
  and the window guarantees DX and DX+1 are resident. **record**: a large
  rework for maybe -300 bytes, with risk in the write path.

- **R3. Ask for the volume by name.** On a signature mismatch where state is
  owed, today's answer is to drop the state silently: `dws_hload` loses a
  held stream, and the FAT window reloads. "State is owed" means a held
  `WRITE_SEQ` stream or a dirty FAT.

  Mac System 5-7 kept a VCB "offline" and asked "Please insert the disk:
  <name>". With the derived `BS_VolID` (§18.8.2.1) and the label now unique
  per disk, the kernel could toast "Put back <label>" and refuse the write,
  rather than complete it against the wrong disk. **record**: about +40
  bytes, plus a toast string.

## Prior art

- **DOS 2-6.** Several DOS mechanisms map directly onto os8088's:
  - **Media check.** Per-drive state is the BDS, and media check returns
    *unchanged / don't know / changed*, in that order of evidence:
    - the change line on an AT
    - a 2-second "recently accessed" rule on an XT
    - the volume serial and label compare (DOS 4+)

    os8088 has the second (the motor countdown, `dsk_media_ok`) and the
    third (the BPB signature). It lacks the first: dr-4 takes it free.
  - **The DPT EOT patch.** DOS patches the DPT EOT at boot, as os8088 does,
    and os8088 also follows the mounted geometry per call.
  - **Retries.** DOS retries 3 times and asks the user (int 24h).
    os8088's toast plus progress chrome is the non-modal equivalent.
  - **FindNext.** DOS FindNext keeps its state in the caller's DTA, which is
    dr-5.
  - **Where os8088 is better.** It validates every BPB field DOS trusted.
    It batches multi-sector runs: 140 int 13h calls against DOS's 325 on the
    same workload (DISK-CPU-PLAN §2). It also bounces 64KB straddles
    instead of failing them.
  - **Where DOS is better.** Its read path is CPU-cheaper. DISK-CPU-PLAN
    puts the OS share at 9.3 s against 3.5 s, mostly re-walking by name.
- **Windows 3.x.** SmartDrive's read cache is the same idea as §18.95:
  whole-track read-ahead with write invalidation. os8088 correctly refuses
  SmartDrive's write-behind on removable media. 32-bit disk access, the
  FastDisk VxD that bypasses the BIOS, is the DVK_DRV / `DSV_BLK` model:
  a driver serves the blocks and the kernel never learns the controller.
- **OS/2 1.x.** Its block device drivers take request packets through a
  strategy routine. `DSV_BLK` plus `drv_cls_svc_x` is a lighter version of
  the same contract. OS/2 also kept the BIOS out of the disk path entirely,
  which os8088 gets on an AT with HDD.DRV's take (`osapi_vol_add_x .take`).
- **Mac System 5-7.** Software eject made the swap problem disappear on
  400K/800K drives: the OS, not the user, decided when a disk left. Drive
  queue elements and VCBs separate drive from volume, and an ejected volume
  stays known as "offline" (R3). The volume/transport split here, DV_KIND
  against the medium, is the same separation reached from the other end.
- **GEOS (C64 and PC/GEOS).** C64 GEOS identifies a disk by its name and
  the ID in the BAM, and checks it on every OpenDisk. That is what §18.8.2's
  derived serial plus signature now does. Its turbo loaders read whole
  tracks, like the fill. PC/GEOS sat on DOS's own disk layer, so the
  closest peer on an 8088 did not try to own the FAT. os8088 owning it is
  what lets it batch, bounce and validate.
- **AmigaOS.** `trackdisk.device` always reads and writes whole tracks
  through a track buffer. os8088's measured "from the requested sector to
  the track's end" is a refinement of that. The disk-change interrupt is
  broadcast to the file system, which is dr-4's model.

## Doc/code mismatches

File:line, a short exact quote, what the code does, and proposed text.

| # | file:line | current text (exact) | code does | proposed |
|---|---|---|---|---|
| 1 | disk.inc:8 | `snapshotted into FAT_SEG at mount` | a FAT *window* of up to DSK_FAT_SECS sectors in a per-volume claim or the FAT_SEG pin (`dsk_fat_window` 3947, `dsk_fatw_pick` 5828) | "the FAT is reached through a window (SPEC.md 18.8) - the whole FAT on every floppy, a sliding DSK_FAT_SECS on a fixed disk -" |
| 2 | disk.inc:16 | `Reads go one sector per int 13h call (AH=02, AL=1) so track boundaries` | runs to the track or cylinder end, capped by the DMA page and BX, with a single-sector fallback (1784-2073) | "A transfer goes as few int 13h calls as the track (or cylinder), the 64KB DMA page and the buffer allow (SPEC.md 18.91); a run that fails three times drops to one sector at a time, three attempts each with an AH=00 reset between" |
| 3 | disk.inc:20 | `The three big buffers - the directory, the icon table and the sector` | only `dsk_secbuf` is in `.lowbss` (dskwin.inc:291); the listing is the caller's store (`dsk_dest_x`), icons a machine-wide store (25.9) | "The sector scratch, dsk_secbuf, lives in .lowbss; a listing is written into the store its caller supplies (dsk_dest_x) and icon bodies into the machine-wide store (SPEC.md 25.9)" |
| 4 | disk.inc:28 | `dsk_next_clus is its single reader.` | `dsk_free_clus_x` reads it too (6405-6481); ES is `[dsk_fatseg]`, not FAT_SEG | "dsk_next_clus and dsk_free_clus's count are its readers, through ES = [dsk_fatseg]" |
| 5 | disk.inc:201 | `a volume is capped at 65,535 sectors (31.99MB) by BPB rule 8, which is also` | kern_big carries 32-bit LBAs and TotSec32 (18.7.5, `OS88_BIGVOL`), and rule 8 is now rule 14's "not 0"; the boot partition's base is in `dsk_vbase` (425) | "...LBAs are VOLUME-RELATIVE; on kern_big they are 32-bit (SPEC.md 18.7.5) and kern_small caps a volume at 65,535 sectors. The one exception to 'the driver adds the base' is the BIOS boot partition, whose base is dsk_vbase's" |
| 6 | disk.inc:1253 | `LBA -> CHS with 16-bit div (LBA < 2880 so it never overflows):` | one 32/16 `div` with a `cmp dx, cx` overflow refusal, on LBA + base (1750-1767) | "LBA (+ the partition base) -> CHS with one 32/16 div, refused when the quotient would pass a word:" |
| 7 | disk.inc:1256 | `CH = cyl low 8 (cyl < 80, no high bits)` | cylinder bits 9:8 in CL 7:6, and the bound is `[dsk_cylmax]`, 80 or 1024 (1555-1560, 1954-1961) | "CH = cyl low 8, CL = sector \| cylinder bits 9:8 in 7:6 (0 on a floppy); the bound is 80 on a floppy, 1024 on a fixed disk" |
| 8 | disk.inc:1243 | `some sector failed all 3 attempts, or` | 3 attempts at the run and then 3 more at a single sector | "the run failed three times and its first sector three more, or" |
| 9 | disk.inc:1890 | `NOT a recursive` (through "...another task holds", 1890-1892) | the fill (7207) and `.img` (2516) recurse; `fpg_arm` refuses rather than waits (fprog.inc:616) | "Not a recursive dsk_xfer, because [dsk_bnc_*] is one set of cells and an inner bounce would overwrite the outer's" |
| 10 | disk.inc:2806 | `DH = 0FFh is not a geometry` | dead: AX = DH+1 is never 0 | delete the line, or `inc al` if 256 heads is to be refused (dr-13) |
| 11 | disk.inc:3293-3297 | `dsk_vol_kind - what kind of volume is mounted right now?` | no such routine (`[dsk_vkind]` is the byte) | delete the header |
| 12 | disk.inc:3869 | `osapi_vol_paint - slot 0x0288: repaint the desktop's drive zones` | the cell is retired (`kernel.asm:2966`, `os88api.inc:2471`) | delete the header block (3868-3874) |
| 13 | disk.inc:3997-4003 | `the FAT snapshot in FAT_SEG,` / `the synthesized directory in disk_dir and the harvested icons in` | FAT window; listing into `[dsk_dseg]:[dsk_doff]`, icons as reference bytes into the store; quiet when there is no destination; the boot partition's LBA 0 uses `dsk_bootspt` | "...the FAT window's first sector resident (SPEC.md 18.8), and - when the caller aimed one with dsk_dest_x - the sorted listing and its icon references in the caller's store; a mount with no destination is quiet (18.9). LBA 0 is read with the fallback 9/2 on a floppy and the boot drive's geometry on a BIOS boot partition" |
| 14 | disk.inc:4453 | `so equal keys keep their order.` | a Shell sort is not stable across gaps | "Insertion within a gap moves an entry only past a STRICTLY greater one; a Shell sort is still not stable across gaps, which FAT's unique names make moot" |
| 15 | disk.inc:5232 | `(1 or 2: rule 4 above)` | the rule is 6 | "(1 or 2: rule 6 above)" |
| 16 | disk.inc:5277 | `provably dead after rule 13, kept as reordering insurance` | rule 13 is skipped for a non-floppy whose `DV_SECS` is 0 (5207-5210) | "dead after rule 13 on a floppy; on a volume whose size its driver did not declare it is the live bound" |
| 17 | disk.inc:5564-5590 | `ES = LOW_SEG` and `Selection sort: n <= 32` | ES = `[dsk_dseg]`; kern_big runs a Shell sort up to DSK_NENT = 1,024 (4445-4544) | "ES = [dsk_dseg]. kern_small: a selection sort over at most DSK_NENT (32). kern_big: a Shell sort (SPEC.md 19.4.1)" |
| 18 | disk.inc:5889 | `tools/os88disk.py pins BS_VolID (0x88000888) and every FAT timestamp to` (5886-5893) | `os88disk.vol_id` derives the serial from content, `sig_slot`/`steer_sig` give each shipped image its own (tools/os88disk.py:620-655, 1465-1480; SPEC 18.8.2.1-2) | "It cannot tell apart two disks whose CONTENT is identical - a dd'd pair - because os88disk.py derives BS_VolID from the volume's content and this machine's Format, Clone and Write Img stamp a fresh one (SPEC.md 18.8.2.1); that residual is SPEC.md 18.8.2.1's" |
| 19 | disk.inc:5924 | `Gated on 128KB of FREE HEAP, not on the 4.5KB the claim needs` | `DSK_FATW_MINK` = twice the claim, at `MEM_PG_MED` (5948-5957, 5973-5978) | "Gated on twice the claim's KB free at this cache's own rank (SPEC.md 18.8.4)" |
| 20 | disk.inc:5943 | `two os8088-built floppies of one geometry have byte-identical` | see #18 | "...and 18.8.2's signature cannot catch that between two disks of identical content" |
| 21 | disk.inc:6188 | `which is where the 128KB gate lives.` | see #19 | "which is where the gate (DSK_FATW_MINK) lives" |
| 22 | disk.inc:6250-6253 | `LOW_SEG for the life of the machine. Both rows are out of mem_rr_tab with it.` | `[dsk_dseg]` is 0 at rest and a caller's claim during a loud mount, and has a `MEM_RR_ONE dsk_dseg` row (memory.inc:3369) | "dsk_dseg_reloc lived here; [dsk_dseg] names the CALLER's store during a loud mount and is relocated by its mem_rr_tab row (memory.inc)" |
| 23 | disk.inc:6594-6595 | `far past the` / `32-entry listing cap.` | the cap is DSK_NENT, 1,024 on kern_big | "...4,096 entries, past anything real and past DSK_NENT on every build" |
| 24 | disk.inc:6629-6630 | `spc is` / `at most 128` | rule 4 caps it at 64 | "spc is at most 64 (rule 4)" |
| 25 | disk.inc:6695-6697 | `whose boot sectors os88disk.py makes byte-identical` | see #18 | "...the residual - two disks of identical content (SPEC.md 18.8.2.1)" |
| 26 | disk.inc:8603-8612 | `dsk_relist - make the global snapshot a real mount again` | no such routine (4332 says it is gone) | delete the header |
| 27 | disk.inc:8000 | `moving file data (PERFORMANCE.md).` (238 ms a sector) | SPEC 18.8.2 now quotes about 65 ms a sector | "against a copy spending ~65 ms a sector moving file data (PERFORMANCE.md)" |
| 28 | disk.inc:9171-9181 | `dsk_copy_low - copy CX bytes between two LOW_SEG offsets` | no such routine | delete |
| 29 | disk.inc:9435-9446 | `dsk_copy_out - copy CX bytes from LOW_SEG:SI into an arbitrary segment` | no such routine | delete |
| 30 | disk.inc:9450 | `AX = entry index (0..31; the caller validates against disk_nfiles)` | indices to DSK_NENT-1, read from `[dsk_dseg]` | "AX = entry index (below disk_nfiles); read from the listing at [dsk_dseg]:[dsk_doff]" |
| 31 | disk.inc:9513-9518 | `dsk_put_icon / dsk_put_icon_k - one 64-byte icon body -> a listing slot` | no such routines (25.9 made it a reference byte) | delete |
| 32 | disk.inc:10404 | `AX = entry index (0..31)` | as #30 | "AX = entry index (below disk_nfiles)" |
| 33 | disk.inc:10701-10704 | `is unit 1 really there?` / `in:  nothing` | AL = the unit, 1..3 (10772, 18.98) | "dsk_fdd_probe - is unit AL really there? (SPEC.md 18.97, 18.98) / in: AL = the unit (1..3)" |
| 34 | disk.inc:10717 | `it is only ever asked about unit 1` | asked about every claimed unit above 0 | "it is never asked about unit 0" |
| 35 | disk.inc:11024 | `SENSE DRIVE STATUS on unit 1 -> AL` | `[fdd_unit]` | "SENSE DRIVE STATUS on [fdd_unit] -> AL" |
| 36 | disk.inc:10962 | `re-seek unit 1 (bit 1)` | clears bit 1 whatever the unit (dr-14) | fix the code, or "forget the interrupt our recalibrate raised (bit 7)" |
| 37 | dskwin.inc:4 | `Three buffers and the four constants that size them.` (4-27, with the 7,936-byte table) | one buffer, `dsk_secbuf`, plus the knob pad (291-353) | "One buffer - dsk_secbuf - and the constants that size the listing. ... FAT_SEG (DSK_FAT_SECS x 512) + 512 dsk_secbuf [+ DSK_OVLPAD]" |
| 38 | dskwin.inc:196 | `64 bytes on kern_big,` | DSK_NENT is 1,024 on kern_big and the index follows the caller's store | "one byte per entry in the caller's store (SPEC.md 25.9)" |
| 39 | dskwin.inc:211 | `**AND IT IS PER-BUILD SINCE SPEC.md 22.6.2**: 64 on kern_big, 32 on` | 1,024 (a ceiling, `DSK_BIGDIR`) on kern_big, 64 on kern_dos, 32 on kern_small (233-258) | "...1,024 on kern_big (a ceiling: the listing is sized to its folder, 22.6.4), 64 on kern_dos, 32 on kern_small" |
| 40 | dskwin.inc:270 | `` `disk_icons` is **2,048 bytes of `.lowbss`** `` | `disk_icons` no longer exists (25.9) | "The icon REFERENCES are the Disk window's (SPEC.md 96.44.7) ..." |
| 41 | drvvol.inc:4, 29 | `FOUR ROUTINES` / `A PURE MOVE. Not a line of the four bodies changed` | five bodies (`dskw_czsize`, 230), and `drv_mounted` was rewritten (quiet, `[dsk_bootvol]`) | "THE VOLUME PLUMBING - drv_mounted, the bank pair, drv_find and kern_big's dskw_czsize ... (moved from driver.inc; changed since)" |
| 42 | drvvol.inc:34 | `make drive A: the mounted volume, if it can be` | mounts `[dsk_bootvol]` | "make the BOOT volume's root the current one, if it can be" |
| 43 | drvvol.inc:39-42 | `A full remount every time, deliberately` | a quiet `dsk_chdir_q_x`, skipped entirely when already there with the motor running (55, 8275-8291) | "A QUIET mount (SPEC.md 18.9), and none at all when §18.9.1's predicate says nothing can have moved" |
| 44 | drvvol.inc:191 | `drive A: already mounted` | the boot volume | "the boot volume already current (drv_mounted)" |
| 45 | kernel.asm:1561 | `and every FAT16 volume there can be is` | rule 10's cap binds a floppy only; a fixed volume has no cap (disk.inc:5148-5151) | "...and every FAT16 FLOPPY is refused by this number alone; a fixed volume's FAT is a window and has no cap (SPEC.md 18.2 rule 10)" |
| 46 | SPEC.md:34314 | `Transfers go **one sector per int 13h` | see #2; it also contradicts the table row three lines down | "Transfers go as few int 13h calls as the track, the DMA page and the buffer allow (§18.91); a failed run drops to single sectors, three attempts each with an AH=00 reset between." |
| 47 | SPEC.md:34332 | `` 2048-byte **`.lowbss`** buffer (§2.1): 32 × 64-byte **harvested** icon bodies `` | retired by §25.9 | "**RETIRED** (§25.9): a listing holds one reference byte per entry into the machine-wide icon store" |
| 48 | SPEC.md:34333 | `Stages that entry's 32 bytes from `LOW_SEG`` | DSK_DE_STRIDE (24) bytes from `[dsk_dseg]` | "Stages that entry's DSK_DE_STRIDE bytes from the listing at `[dsk_dseg]`" |
| 49 | SPEC.md:34335-34336 | `With `dsk_copy_low`, one of the two routines that load DS=`LOW_SEG`` and the whole `dsk_copy_low` row | `dsk_copy_low` is gone; `dsk_copy_seg_x` is the one body that loads a foreign DS (disk.inc:9110-9151) | drop the `dsk_copy_low` row; "a wrapper of `dsk_copy_seg`, the one body that loads a foreign DS" |
| 50 | SPEC.md:34338-34339 | `all in` / `` `.text` — disk code stays hot and near `disk_read` `` | all `.cold` (disk.inc:1232, 3041, 6254, ...) | "all in `.cold` (§2.6), resident" |
| 51 | SPEC.md:34343 | `Clobbers CF only (internally saves AX,BX,CX,DX,SI,DI)` / `first 64 bytes` | clobbers AX..DI and ES (disk.inc:5019); stages DSK_BPB_SZ = 18 bytes from offset 11 (5063-5066) | "Clobbers AX..DI, ES and flags (its one caller carries none across). Rules 1-2 read the sector; bytes 11..28 are staged into `dsk_bpbh`" |
| 52 | SPEC.md:34344 | `16-bit mul; cannot wrap` | kern_big is 32-bit, the high word in `[dsk_c2hi]` (5706-5709) | "kern_big: 32-bit, the high word to `[dsk_c2hi]` (§18.7.5); kern_small: 16-bit, DX≠0 refused" |
| 53 | SPEC.md:34345 | `The **single reader of `FAT_SEG`** (§2.1): push ES; ES = `FAT_SEG`` | ES = `[dsk_fatseg]`, the window, with `dsk_free_clus` as a second reader | "A reader of the FAT window (§18.8): ES = `[dsk_fatseg]`" |
| 54 | SPEC.md:34433 | `2. **FAT snapshot.** Read `dsk_fatsz` sectors` | the window's first load, or a quiet mount's banked window (dsk_fatw_want/pick, 4278-4288); steps 2a (cwd check) and 2b (quiet stop) are missing | rewrite step 2 as "the FAT window" and add 2a/2b as in disk.inc:4290-4346 |
| 55 | SPEC.md:34448 | `4. **Icon harvest.** For each accepted entry i: type≠1 → zero the 64-byte` | the index is blanked 0xFF, `ico_ref_try` answers most from the store, at most DSK_HARV_MAX peeks a listing (kern_big), version == `PKG_FMT` (4639-4835) | rewrite step 4 against §25.9 and §22.6.4 |
| 56 | SPEC.md:34462 | `Mount I/O budget, honest numbers: sectors read = 1 + FATSz (2..32)` | the cache and `ASSOC.DAT` change every term; §18.8.2 has the measured row | point at §18.8.2's measured table, or re-measure |

## Open questions

1. **dr-1.** Does the FN33 field image's C: mount today? The arithmetic says
   it does not. A boot of the dumped image in 86Box (`tools/os88box.py`)
   answers it in one run.
2. **dr-2.** Does any shipped C package call `os88_batch_begin` outside a
   lock hold? `grep` finds only the thunk in this tree. The hazard is the
   contract, not a known caller.
3. **dr-4.** On the IBM AT and Victory ROMs, is 06h reported to a read with
   CF=1, or cleared inside the ROM's own retry? The AT TRM says CF=1 with
   AH=06h. A real run on the Victory (FIELD-MACHINES) would confirm it
   before the code relies on it.
4. **R1.** Splitting mount from listing touches `fmv_load`, in files.inc,
   another reviewer's area. That reviewer should agree the seam.
