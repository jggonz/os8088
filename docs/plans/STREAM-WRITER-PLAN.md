# STREAM-WRITER-PLAN - writing a big file without paying for it again every chunk

**Status: OPEN. Stages 1, 2 and 3 BUILT (SPEC.md 18.4.7.6, 18.4.9), the
consumers and the FAT at a hop built (§8, §11), and a SECOND size pass (§12)
took the whole branch from +1,077 to +503 resident bytes on `kern_big`, the
owner accepting 503 for stage 1 in its 16-byte form; `WSEQF_SYS` (+8) then
let five more writers convert at no resident cost (SPEC.md 18.4.9's table).
What is left is the ST-225 (§8 item 4).** This is docs/plans/DISK-CPU-PLAN.md §6 taken on. That
section named the write side and sketched a fix's SHAPE; this is the design,
staged, with what each stage costs and what it must not break.

It exists because the same defect has now come up **four times in two days
of sessions**, each time from a different program, and each time it was
written down as someone else's problem:

| where it surfaced | what was seen |
|---|---|
| VIDDISK `W` on the owner's ST-225 (docs/reports/VIDDISK-ST225-2026-09-27.md) | 12.8 MB in 400 appends of 32 KB: **700 s, 18.2 KB/s**, against 104-110 KB/s reading the same disk |
| FTPD `STOR` (DISK-CPU-PLAN §6.2) | a large upload slows down as it goes - estimated ~230 s of walking in 5 MB |
| the file manager's copy and the installer (DISK-CPU-PLAN §6.2) | chunked writes, same walk, same commits |
| Uncompress To... joining `OS8088.001` onto the ST-225 (branch `split-v88`, not yet on this one) | once the floppy stopped, the hard disk ground with far more head movement than the data explained |

## 1. What one append costs today - MEASURED

`OSAPI_FILE_APPEND` is `OSAPI_FILE_WRITE_AT` at the file's size (SPEC.md
18.4.7.3), and every call is a complete, committed operation by contract.
The owner's layout on MartyPC (`os8088_xt_hdd_720`: booted from C:, a 720 KB
part in A:, the result on C:), every `int 13h` of an Uncompress To...
logged with its cylinder, head and sector. One 32 KB append on the fixed
disk, in the tail where the floppy is no longer read:

| # | transfer | where | why |
|---|---|---|---|
| 1 | read, 3 sectors | cyl 1 | the NAME is looked up again (`dskw_find`): the directory |
| 2-5 | write, 64 sectors in 3-4 calls | cyl 13-15 | the data |
| 6-7 | write, 1 sector each | cyl 0, FAT1 and FAT2 | flush #1: the new sub-chain durable, unlinked |
| 8-9 | write, 1 sector each | cyl 0, **the same two sectors** | flush #2: the link to the old last cluster |
| 10 | write, 1 sector | cyl 1 | the directory entry: the new size |

Three long seeks per 32 KB (directory -> data -> FAT -> directory), and five
small metadata transfers, two of which rewrite exactly what the two before
them wrote. On a volume whose free space is far from cylinder 0 - any full
ST-225 - every one of those seeks is a full stroke. This is what the owner
heard.

And the CPU half, which that trace cannot show: step 1's lookup and a walk
of the cluster chain **from the front to its last cluster**, every call
(SPEC.md 18.4.7.3), so a file written in chunks costs time QUADRATIC in its
length. READ_AT measured that walk at **141.9 ms per MB** of offset
(SPEC.md 18.4.8); VIDDISK `W` on the ST-225 puts ~400 of its 700 s there.

## 2. The three costs, and which lever takes each

| cost | grows with | lever | stage |
|---|---|---|---|
| the duplicate FAT flush | calls | flush ONCE when the whole FAT update is one sector | 1 |
| the name lookup + the chain walk | calls x file length | a CALLER-KEPT cursor, READ_SEQ's mirror | 2 |
| the per-call commit (FAT + entry, 3 seeks) | calls | commit once per FILE, not once per call | 3 |

They are independent and each is worth having alone. They are ordered by
risk: stage 1 changes no contract, stage 2 adds a slot and keeps every
contract, stage 3 changes WHEN the disk is consistent and so needs a
decision from the owner before it is built.

## 3. Stage 1 - one FAT flush when one will do

**Why there are two.** SPEC.md 18.4's commit order: the data, then the FAT,
then the entry, so a crash leaks clusters rather than corrupting a file. An
append at the end refines that for the FAT itself: flush the new sub-chain
(allocated, terminated, unreachable), THEN set the link from the file's old
last cluster and flush again. A flush writes the dirty RANGE of FAT sectors
in one multi-sector transfer, and a power cut part-way through one of those
can land some sectors and not others. If the link's sector lands and the
new chain's does not, the file's last cluster points into clusters the disk
still shows FREE - and the next allocation hands them to another file. That
is a cross-link, the one failure the order exists to rule out. So the two
flushes are deliberate, and correct.

**Why one is enough when the update is one sector.** A sector is written
whole or not at all. When the link and every new entry share ONE FAT sector
(per copy), flush #1 and flush #2 write the same sector, and the second write
is the whole update: there is no partial state for the first to guard. That
is the common case by a wide margin - a 32 KB append on a 2 KB-cluster disk
is 16 FAT16 entries, 32 bytes, beside the link - and it is exactly what the
trace shows being written twice.

**The rule:** after the data is written and the sub-chain allocated, set the
link FIRST if the dirty range (with the link's entry in it) is exactly one
sector, and flush once. Otherwise keep today's two flushes. FAT12's entries
straddle sectors, and a straddling link makes the range two sectors, which
takes the safe path by the same test.

**BUILT (SPEC.md 18.4.7.6)**: 81 bytes of `.cold` on each kernel,
resident. MEASURED over eleven 32 KB appends of VIDDISK's W on MartyPC
(`VD_TRACE=12 tests/viddisk.py --floppy`): one-sector writes **5.2 -> 3.2**
an append, data writes 3.3 and the directory read 1.0 unchanged. W's time
575 -> 570 guest seconds, because MartyPC's XT-IDE makes a write cheap and
the walk is what W pays for there; on the ST-225 each saved write is at
least a revolution. It does nothing about the seeks: those are stage 3's.

## 4. Stage 2 - `OSAPI_FILE_WRITE_SEQ`, the cursor (`kern_big`)

READ_SEQ's design (SPEC.md 18.4.8) carried over, with one thing more to keep.

**The cursor is the CALLER's, 16 bytes, a cache the kernel can always
rebuild from the name.** It holds the mount generation it was seeded under,
the volume, the file's LAST cluster, its size - and, because a write must
update the ENTRY, where the entry is: its directory sector and slot. A call
with a valid cursor:
- skips the name lookup (step 1): the entry's place is in the cursor;
- skips the walk: it links forward from the cursor's last cluster;
- allocates, writes, commits as stage 1 does, and hands the cursor back
  with the new last cluster and size, stamped with the generation the
  write itself produced.

**The generation needs one decision READ_SEQ did not.** Every write bumps
`[dsk_mgen]` in `dskw_sync_x` so that every READ_SEQ cursor re-seeds. A
WRITE_SEQ cursor must survive ITS OWN write, so the wrapper re-stamps it
after the call with the generation that call produced. Any OTHER write, a
remount or a media change still invalidates it, and the next call re-seeds
from the name: one lookup and one walk, which is what APPEND pays every
time today.

**What it does not change:** every call is still a complete, committed
operation. A crash between calls loses nothing that was acknowledged.

**Consumers**, in the order they are worth converting:
1. the split-set join (`Uncompress` and `Uncompress To...`, branch
   `split-v88`) - kernel-side, `CLONE.DRV`,
   through the far door the module already uses for APPEND;
2. the file manager's copy (SPEC.md 22.5) - kernel-side;
3. FTPD `STOR` - a package, through the new slot;
4. VIDDISK `W` - the instrument, so the ST-225 figure can be re-taken.

`kern_small` gets the cell and a refusal, as READ_SEQ did (SPEC.md 20.8
rule 4), unless the stage-1 measurement says the small machine's chunked
writers (the copy) need it too. That is a question for the numbers, not a
default.

## 5. Stage 3 - commit once per file (DECIDE FIRST)

This is Set 24's lever: the same bytes as 8 KB appends against one write
cost **2.36x on the floppy and 3.81x on the ST-225** (PERFORMANCE.md),
before any file was large enough for the walk to matter. With stage 2 in, it
is also the only thing left between a chunked write and the disk's own
sequential rate, because it is the three seeks per chunk.

The shape: a WRITE_SEQ cursor may be opened HELD. A held call writes the
data and extends the chain in the FAT window, but does not flush the FAT
and does not store the entry. Those happen once, at a CLOSE - and at every
point where the machine could lose track of them:
- the FAT window has to move (it already flushes then);
- the batch bracket ends (SPEC.md 18.9.3: `gfx_unlock`), which is the moment
  the user is allowed to touch anything, including the floppy;
- any other file operation on that volume.

**What it changes is the contract**: between calls, the disk does not yet
show the file's new length. A crash loses everything written since the last
commit (the data is there, unreachable). It can never cross-link, because
the close writes chain-then-link as stage 1 does, and nothing else runs.

**The decision it needs**: whether a held write may span a PROMPT. The
join's `Put FILEA.002 in A:` ends the batch, so under the rule above the
join commits at every prompt. That is correct and costs one commit per part
rather than one per file, which is still ~20x fewer than today on a 720 KB
part. Holding across a prompt would mean trusting that the user does not
take out the TARGET disk while being asked for a SOURCE disk; that is
UI-FREEZE-PLAN §3.2's removable-media consistency model, and it is not this
plan's to change.

## 6. What each stage must be measured against

- **The transfer trace** of one 32 KB append on the fixed disk (§1's table):
  10 transfers and 3 long seeks today. Stage 1: 8 and 3. Stage 2: 7 and 2
  (no directory read). Stage 3, held: 3-4 (the data) and 0 between commits.
- **VIDDISK `W`** (`tests/viddisk.py --floppy`, the `viddiskfd` row):
  guest seconds to write 12.5 MB in 400 appends. On MartyPC's XT-IDE, which
  moves bytes with the CPU, so its transfer rate is not the ST11M's; the
  WALK is CPU either way and this is where stage 2 shows.
- **An fsck** (`tools/os88disk.py --verify`) of every volume a gate writes.
- **The ST-225**, for the number that started this: 700 s for 12.8 MB.

## 7. What this plan does NOT do

- It does not make writes asynchronous. The UI task still does the disk;
  the freeze is SPEC.md 18's model, and UI-FREEZE-PLAN owns it.
- It does not add a handle table or an open-file model. The cursor is the
  caller's, as READ_SEQ's is, precisely so that there is no kernel state a
  second writer or a remount could leave stale.
- It does not touch the read side, which READ_SEQ already fixed.

## 8. What was built, and what is left (2026-09-29)

Stages 2 and 3 are one slot, `OSAPI_FILE_WRITE_SEQ` (SPEC.md 18.4.9), and
the owner decided stage 3's question: HELD is right for a file that is
useless until it is finished, and plain stays for a program that appends
over a long time and needs every chunk to survive. Measured on VIDDISK's W
(12.5 MB, 400 x 32 KB) on MartyPC: APPEND 570 guest seconds, WRITE_SEQ plain
**204**, HELD **191**, with one-sector writes per append 3.2 -> 0.2.

Three things the design found that this plan did not have:
- **The generation had to be a new one.** READ_SEQ's `[dsk_mgen]` moves on
  every mount, including a batch's banked hop between volumes, and the join
  hops every block. So WRITE_SEQ keys on `[dsk_wgen]`, which moves on a
  write or a mount that re-reads a boot sector, and on nothing a batch
  vouches for. (§12 then made that READ_SEQ's rule too, and there is one
  generation again, `[dsk_mgen]`.)
- **A held chain is never linked until the commit.** Linking as it grew
  would put a link on the disk at the first window move, ahead of the chain
  it points into. Unlinked, any flush of it is harmless, and a power cut
  leaves the file at its committed size (`wseqcut`).
- **The unlock commit is the one that matters for a floppy target**, not
  the mount hook. A mount after the user swapped the disk would commit onto
  the wrong disk. `wsequnclosed` is arranged so that nothing else can
  commit, which took two tries: the bench's own trip home was committing
  first.

What is left, in order:
1. ~~The consumers.~~ **All three are converted**: the split-set join
   (section 11) and the file manager's copy are HELD, one stream per file,
   closed before the rename or at the copy's end (SPEC.md 22.5.3); FTPD's
   `STOR` is PLAIN (SPEC.md 77.49). Before and after on every one of them,
   against `origin/elendilon`, is
   docs/reports/STREAM-WRITER-AB-2026-09-29.md.
2. ~~The FAT at a hop~~ - **BUILT** as SPEC.md 18.8.5, on section 11's
   measurement, at +198 bytes against section 9's 120-160 estimate.
3. ~~A size pass.~~ **Done, and it was a design fix rather than a squeeze**
   (section 10): WRITE_SEQ had grown a body of its own beside the append
   body, and folding it in took 259 bytes out. The stream writer is
   **+777** on `kern_big`: stage 1's 81 and the slot's 696, of which 14
   are the fix for the defect the folding found (end of section 10).
   With the bank, the copy's room check and the consumers the branch was
   **+1,077**, over the 500 its owner set, and §12 is the second pass.
4. **The ST-225.** 700 s for 12.8 MB is the number that started this;
   VIDDISK's new `p` and `h` keys are how to re-take it.

## 9. The FAT at a hop - what "flush" means, and what removing it would take

**It is a disk write, with the seeks.** The per-volume FAT caches are real:
every volume has its own FAT WINDOW, either the kernel's own (`FAT_SEG`,
"the pin", 18.8.3) or a heap claim (18.8.1, 18.8.2), and a hop back to a
volume reuses its window without reading a sector (the banked sector and
the disk signature, `dsk_fatw_pick`). What is NOT per volume is the record
of which of those sectors hold edits the disk has not seen:
`[dsk_fatd0, dsk_fatd1]` is ONE pair, and `dskw_flush_x` writes it with the
CURRENT volume's geometry. So `dsk_fatw_park`, at the top of every mount,
writes the outgoing volume's dirty range to FAT1 and FAT2: two `int 13h`
writes, each a seek to the FAT area and back.

Its comment says this never costs an I/O, "every commit point already
flushes", and that was true until HELD. Every other writer flushes before
it returns; a held stream leaves its unlinked chain's allocations dirty on
purpose. So a join that hops source -> target -> source writes the target's
FAT twice per block, and a copy onto the same volume never does. On a
floppy target that is the same order as the data write itself (PERFORMANCE:
~400 ms a call, whatever it moves). **Not yet measured**: the join is not
converted, and VIDDISK hops nowhere.

**The fix is to bank the dirty range per volume**, beside the three words
each volume already banks (`dsk_fatww`, `dsk_fatwc`, `dsk_fatwsig`):
- `park` banks the pair instead of flushing;
- `pick` restores it with the window it reuses.

That is the easy half, and ~40 bytes. The rest is four places where a dirty
window that is not the live one could be lost. Each needs an answer, and
the third is the one that decides whether this is worth doing:
1. **`pick` reloads**: a full mount, a banked sector of 0xFFFF, or a
   signature that no longer matches. Same signature: flush the banked range
   first, the window still holding the bytes. Different disk: drop it, and
   abandon any hold on that volume by section 8's failed-call rule.
2. **A shed or a compaction takes a banked dirty claim.** `mem_fatw_dirty`
   refuses the live window today and must learn the banked ones: scan
   `dsk_fatwc` for the record's segment and ask that volume's range.
3. **The pin is evicted from a dirty holder.** `dsk_fatw_want`'s `.evict`
   hands the kernel's window to a volume the heap refused, and the holder's
   dirt would be overwritten. This is the common case, not the corner:
   the boot volume takes the pin, and on an INSTALLED machine that is C:
   (docs/plans/completed/SETTINGS-COST.md 5.1). So the join's target is
   usually the pin holder. The answer is that the incoming volume claims at
   a higher rank, shedding caches. If even that refuses (a 4.5 KB claim
   with the caches gone), the MOUNT fails. That fails the operation and
   loses nothing: the hold is committed at the `gfx_unlock` that follows.
4. **Hibernate** zeroes `[dsk_fatd0]` (`kernel/hiber.inc`). It must flush
   every banked range first, which means hopping to each dirty volume, or
   refuse while one is dirty.

Estimated ~120-160 bytes of `.cold` and `DVOL_MAX * 4` of `.text`, against
two writes per hop on a held stream only. **Recommendation: convert the
join, count its target writes per block with VIDDISK's `VD_TRACE` tally
pointed at the join, and decide on the number.** If the join's block is
small, a larger block cuts the hops for nothing. The block is `CLONE.DRV`'s
to choose, and on a one-drive machine the hops are disk swaps anyway.

## 10. The write API: one body per verb, and the doors are not the cost

The concern was that the disk API grows a near-copy of itself every time a
path gets optimised. Taking stock of the file-contents slots after this
pass:

| slots | body | what it is |
|---|---|---|
| `WRITE`, `WRITE_SYS` | `dskw_wbody` | create or replace a whole file |
| `APPEND`, `APPEND_SYS`, `WRITE_AT`, `WRITE_SEQ` | `dskw_wabody` | grow at the end, or rewrite inside, by position |
| `READ` | `dskw_rbody` | a whole file, sized before any I/O, CZ expanded |
| `READ_AT`, `READ_SEQ` | `dskw_read_at_x` | by position |

Eight slots and four bodies: two verbs times whole-or-positional, with a
cursor as an optional input to the positional body. That is the shape a
designed API would have, spelled as eight entry points. A slot and its door
cost 6 bytes and 5 to 10 more, so the slots were never where the bytes
went.

**WRITE_SEQ was the one exception, and it has been fixed.** As first built it
had a body of its own: a second lookup, a second cluster-multiple check, a
second walk, a second allocate-flush-link sequence and a second entry store,
805 bytes beside the append body's 458. It is now a 10-byte door and a
15-byte shim into `dskw_wabody`, and the cursor is an optional input there:
- **cold** is an ordinary append that remembers what its lookup and walk
  found;
- **hot** skips both;
- **HELD** changes only what `.grow` does once the data is down.

Two smaller shares came with it. The READ_SEQ and WRITE_SEQ far doors had
the same 50-byte copy-in/call/copy-out frame, and it is now one (`dsq_door`).
The grow's entry store is an in-place patch of the two fields a grow
changes, which the hot path needed anyway and which is 4 bytes shorter than
a whole-entry copy.

**A single request-block slot is NOT recommended**, although the table
would allow it: it is unfrozen while the OS is in alpha and `make` rebuilds
every caller (SPEC.md 20.8 rule 4). One `FILE_IO` taking a verb, a name, a
buffer, a count, an offset, a cursor and flags would be the tidiest possible
spelling. But the bodies behind it are the four above whatever the
spelling, the doors it would delete are the cheap part, and every package's
SOURCE would change: a request block in each caller's segment where it now
loads registers, which is bytes in every package for none in the kernel. The discipline worth
keeping is the one this pass applied: **a new capability is an input to an
existing body, never a new body.** Two other families are worth the same
audit before they grow again: the directory slots (`FIND`, `FIND_RAW`) and
the change-directory ones (`GOTO`, `GOTO_Q`, `GOTO_QM`). Not audited here.

One defect was found by the folding and fixed with it. A HELD call that
failed in its allocation or its data write rolled back with APPEND's
`dskw_refat`, which drops the FAT window, including the held chain's
unflushed allocations, and **kept the hold**. MEASURED with a dying disk
at chunk 100 (`wseqioerr`): the commit wrote an entry of 3,276,800 bytes
over a 17-cluster chain, the next chunk's clusters being free on the disk.
The rollback now FLUSHES instead of dropping. The held chain is unreachable
until the commit, so that is safe, and the failed call's own sub-chain
becomes lost clusters. So a failed held call loses itself and nothing else,
as a failed APPEND does (SPEC.md 18.4.9). The first fix abandoned the whole
stream instead. It was correct, but it threw away 4 MB that a full disk had
already flushed, and `wseqfull` now guards against that.

## 11. The join converted, and what the FAT at a hop measured (2026-09-29)

`tests/czseq.py` joins a 640 KB set from B: onto A: with Uncompress To...,
every block a hop. It catches every `int 13h` and files it by drive,
direction and region, with the ROM time of each call. Guest seconds, Enter
to verdict:

| writer | guest s | target FAT writes | first write after a hop |
|---|---|---|---|
| `OSAPI_FILE_APPEND` (before) | 127.7 | 48 | ~190 ms (a READ) |
| `WRITE_SEQ`, HELD | 134.6 | 42 | ~1,500 ms |
| HELD, FAT dirt banked (18.8.5) | **116.8** | **6** | ~1,500 ms |

**HELD alone was slower, and the FAT was not why.** The ROM runs ONE floppy
motor at a time, so every hop stops the other drive, and it waits out the
spin-up before a WRITE and not before a read. APPEND's per-block name lookup
was a read, so A: spun up for about 190 ms of useful work. WRITE_SEQ took
the lookup away, and the first thing after a hop became a write that waits
the full second. The bank takes the per-hop FAT flush off instead, and that
more than pays it back: 8.7% faster than today.

**From a floppy to a fixed disk** (`--hdd`: a VHD boot, the parts on a
360 KB B:) the target's metadata per block falls from 4 calls (a directory
read, FAT1, FAT2, the entry) to 0.6. The counts are exact. MartyPC's XT-IDE
takes 6 ms a call with no seek time, and its Xebec model has no delays at
all, so what that saves on the owner's ST-225 is a PREDICTION: two long
seeks to cylinder 0 and back per block. The source floppy dominates that
join (26 of 29 seconds of ROM time for 300 KB), so the saving is a few
percent of it. It is for 86Box's `pc5150` or the 5150 itself to confirm.

**The design section 9 feared was simpler than it looked**: one bank, not
one per volume, because only a held stream leaves dirt for a park, and
there is one hold at a time. Its four hazards became one rule (the hold is
LOST, POISONED so its cursor's next call answers `FERR_IO`) plus one fence
section 9 had not seen. A machine whose heap refused the windows
(`FATWNONE=1`) ping-pongs the pin between two volumes at EVERY hop, so
banking the pin there would lose every held join at its first hop.
MEASURED: `Disk error` 19 guest seconds in. So the park banks the pin only
when the incoming volume has a claim of its own, and otherwise flushes as
before.

Gated by `czseq` (the table, and at most one FAT write per two blocks),
`czseqlose` (the poison; without it the join says `Uncompressed` over a file
with a hole in it) and `czseqnone` (the fence).

## 12. The second size pass: +1,077 -> +503 (2026-09-30)

The branch arrived at +1,077 resident bytes on `kern_big` against a budget
of 500. docs/reports/STREAM-WRITER-SIZE-2026-09-30.md is the step-by-step
measurement; what it found, in the order it mattered:

- **The caller's cursor was the expensive part, not the append.** A 16-byte
  cursor copied in and out of a kernel copy, and a held stream keeping a
  SECOND copy of the same fields for its commit, is two records and two
  doors' worth of moves. The kernel keeping ONE record and the caller one
  WORD (the generation that call left) is the same information: a token that
  equals `[dsk_mgen]` can only name the stream the record describes, because
  every write, every boot-sector read and every new hold moves it. Two
  streams at once stay correct and merely go cold on each other. -346 in the
  first step, with the commit made an ENTRY into the append body's own seal.
- **§10's rule applied one level down.** "A new capability is an input to an
  existing body" had been applied to the append and not to its COMMIT, which
  still carried its own flush-link-flush-store; entering `.seal` removed it.
- **Hazards met where they converge.** The bank's shed and pin-eviction
  hooks each guarded a window going away; both leave the volume's banked
  sector `0xFFFF`, so both are met at the LOAD that the way back must make,
  where the re-read hazard was already met. One hook instead of three, at
  the price that a shed now LOSES a held stream rather than being refused.
- **One generation.** READ_SEQ's `[dsk_mgen]` moved on every mount; moving it
  only on writes and boot-sector reads is `[dsk_wgen]`'s rule, and it keeps
  a READ_SEQ cursor hot across quiet hops too.
- **Stage 1 needed no arithmetic.** It was withdrawn as the smallest
  measured win, then put back at the owner's word in a 16-byte form: store
  the last cluster's end mark back unchanged, so its sector joins the dirty
  range, and ask whether the range is still one sector (SPEC.md 18.4.7.6).
  It re-measures exactly as `dskw_onesec` did, 3.2 one-sector writes an
  append.

What changed in behaviour is listed in the report, and the ABI change is
SPEC.md 18.4.9's: `ES:BX` bytes, `AL` flags, `DI` the token.

