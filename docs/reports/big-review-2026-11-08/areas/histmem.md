# LENS: systemic comparison - memory, loading, resources

Reviewer key `histmem`, tree `the-big-review` (shallow clone, cut from
`elendilon`), 2026-11-08. Read-only: nothing assembled, no emulator run. Every
cycle or millisecond figure below is **PREDICTED** from PERFORMANCE.md's 5150
table and 8088 instruction timings unless it says MEASURED; the parts-body
sizes are MEASURED off `build/*.map` symbol spans.

## Scope read

| file | lines | what was read |
|---|---:|---|
| `kernel/memory.inc` | 6,617 | every proc's code: the record and tags (1-520), `mem_init_x`/`mem_unblob_x`/`mem_floor_ax`, the KB and paragraph doors, `mem_claim_1`, `mem_pg_own`/`mem_shed_one`/`mem_pg_forget`/`mx_shedf`, `mem_next`/`mem_runfree`/`mem_hifit`/`mem_bcopy`, `mem_can_move` and its predicates, the whole `mem_cp_*` walk, `mem_rr_walk`/`mem_region_reloc`/`mem_rr_tab`, `mem_reloc_call`, `mem_compact`, `mem_runat`/`mem_bigrun`, `mem_find_own`, free/movable/regrow, `mem_free_owner_x`/`mem_reown_x`/`mem_free_rec_x`, avail and the sums, every `osapi_*`/`mmf_*` door, `mem_own`/`mem_owner_of_x`, the snapshots, `mem_bytes_*`, `mem_ringtop` |
| `kernel/loader.inc` | 2,279 | 1-1,600 (header constants, `ld_hdr_ok`, `ld_hdr_size`, `ld_check_hdr`, `loader_run_x`, `ld_take`, `ld_where`, `ld_run_body_x`, `ld_alloc`, `ld_start` through the abort and the re-home entry) and the `.bss` |
| `kernel/mod.inc` | 1,006 | all |
| `kernel/instance.inc` | 2,495 | 1-310 (record, kinds), 356-1072 (walks, park, alloc, `inst_caller`), 1700-1730, 1905-2240 (teardown, fences, spawn, alive) |
| `apps/os88parts.inc` | 633 | all |
| `apps/os88partsbody.inc` | 2,647 | `op_read` (765-872), `op_fetch` (1934-2230) |
| `apps/os88api.inc` | 7,104 | the memory slots 1585-1830, `OSAPI_MEM_P` 2372-2420, the snapshots 2700-2760 |
| `kernel/kernel.asm` | - | `APP_MAX_SIZE`/`PKG_FILE_HI` (74-100), `spl_fseg` (5232-5260), the cell comments at 4062-4185, guards 6/6b (9620-9660) |
| `kernel/deskmod.inc`, `kernel/ui.inc` | - | `desk_drop_x` (118-135), the hover call (985-1012) |
| SPEC.md | - | 20.1, 20.6 rule 7, 20.12.4, 21 (opening through step 1), 29.1, 50.2, 50.6.2, 50.6.6, 66.6.1/66.6.1.1, 92622 |
| docs | - | `KERNEL-MEMORY.md` (the ladder's alignment paragraph and a grep for every heap claim), `HEAP-CLAIMS.md` 1-60, `ABI-BREAK-LIST.md` (whole, by row), `KERNEL-BYTE-QUEUE.md` (whole), `CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (its memory rows) |

## Verdict

At the level of the model, os8088's memory system holds up well against the
period systems. It is one out-of-band claim table. Identity comes from the
calling segment. Purgeable caches are ranked by what they cost to lose.
Compaction plans first and copies only when the plan pays. The heap is
allocated from both ends and knows about 64KB DMA pages. Loading has no
relocation at all. For a floppy-bound 8088 this is a better fit than DOS's
in-band MCB chain, and simpler than the handle machinery of Windows real mode
or GEOS.

The weaknesses are mostly places where one mechanism's discipline did not
reach a neighbour:

- `mem_regrow` copies at IF=0 while the compactor copies at IF=1.
- The owner fence was widened for one door and the widening applies to three.
- A kernel module is dropped from a worker while the UI task may be running
  inside it.
- The per-owner cap was written for packages and also binds the kernel's own
  driver tag.

The one systemic thing the period systems did that os8088 has not is let an
APPLICATION give memory back under pressure: the Mac's purgeable handles,
Windows's discardable segments, GEOS's discardable blocks. The relocation-proc
machinery os8088 already has makes that addition small (hm-9). The deeper
alternative, handles in place of segment identity, is a flag-day question
already on the list (R2-6). This review adds an argument for pairing it with
C1.

## Strengths

1. **The claim table is out of band** (`mem_tab` in `.lowbss`,
   memory.inc:6261). DOS's MCB headers sit in front of each block, so one
   stray write in a program becomes "Memory allocation error, system halted".
   Here a package that scribbles past its claim corrupts only data. The record
   *is* the allocator (freeing is one store, memory.inc:4462), so there is no
   free list that can disagree with the map. Keep it.
2. **The owner word cannot be forged.** `mem_own` (memory.inc:5894-5994)
   derives the owner from the ES the API stub stamped from the caller's DS.
   This is DOS's "owner = PSP segment" without DOS's mutable "current PSP"
   global, which TSRs had to swap by hand. Teardown frees by owner
   (`mem_free_rec_x`, 4825). Its order is right and is written down: windows
   go before claims, so no repaint far-calls a freed region
   (instance.inc:1716-1724).
3. **Purgeable caches are ranked by what losing them costs**
   (memory.inc:340-349, `mem_shed_one` 1402). The Mac had one purgeable bit;
   Windows discarded code segments by LRU. A five-level ladder priced in user
   wait suits a machine where a refill is ~400 ms of int 13h, and the floor
   (`OSAPI_MEM_FLOOR`) lets a disk-heavy program protect the read-ahead.
   Better than any of the period systems.
4. **The purge notice is a zeroed master word** (`mem_pg_forget`, 1456). It
   is the Mac's purged-handle-is-NIL convention, and it needs no callback
   protocol because each consumer already has a "no buffer" path.
5. **The compactor plans before it moves** (`mem_cp_plan`/`mem_cp_run`,
   2910; `mem_compact` 4060-4084). It copies only when the plan would fund
   the pending claim, and it packs each half toward its own door. Windows'
   `GlobalCompact` and the Mac's on-demand compaction moved first and then
   asked. Planning first is the right call where a memcpy costs about
   2.6 ms/KB.
6. **The allocator knows about 64KB DMA pages** (`mem_claim_1` 1150-1169,
   `mem_cp_dest` 2784, `mem_hifit`). The page rule is recorded in the claim,
   so a move keeps it. DOS and Windows left this to every driver author.
7. **Grow in place first** (`mem_regrow` path 2), as the Mac's
   `SetHandleSize` did. Path 3 lands highest-fit, so the next grow extends.
8. **There are no relocations.** A package is an org-0 image in its own
   segment. File = image, plus an optional LZ stream expanded in place
   (`ld_run_body_x.expand`, loader.inc:998-1083). DOS EXE fixups and NE
   per-segment relocation records both cost a parse and a patch pass per load.
   os8088 pays nothing at load, and packing is built in where DOS needed
   third-party PKLITE or LZEXE.
9. **Fixed-offset "resources" in the first sector** (the clear prefix: icon,
   association, glyph, card; loader.inc:1018-1035). The mount gets a package's
   icon in the sector it already peeks. An NE icon costs the MZ header, the NE
   header, the resource table and then the resource: three or four seeks on a
   floppy.
10. **Kernel modules are built in the kernel's own assembly** and stamped with
    the build and the layout (mod.inc:9-22, 113-141). There is no ABI between
    the kernel and its own modules. Windows' KRNL/USER needed the whole NE
    loader to demand-load their own discardable segments. `mod_gone`
    (mod.inc:282) makes a call into an absent module answer CF=1 rather than
    land in freed memory. It is the cheapest safe form of Windows' reload
    thunks.
11. **The parts standard costs the kernel no byte** (os88parts.inc:9-15). The
    kernel-side version was measured at 2,560 resident and thrown away.
    `OP_LAZY` with `op_fetch`/`op_drop` is the Mac Segment Loader's
    `LoadSeg`/`UnloadSeg` with the package choosing when. On a one-floppy
    machine that is the right policy: an automatic LRU reload there means a
    disk-swap prompt.
12. **`mem_reown_x` and `OSAPI_PKG_REHOME`'s KEEP form** (memory.inc:4779)
    are DOS's `int 21h/31h` "keep DX paragraphs", a TSR's install code dropped
    after use, done as a re-stamp under one `cli`. The loader shape
    (`wdload`, `scload`, `csload`, `dosload`) uses it to throw away its whole
    loader, parts body included.

## Findings

Ranked by value. Bytes are signed and say where they land.

### hm-1 (bug, medium, now): `mem_regrow`'s move copies the whole block with interrupts off

`mem_regrow` opens one `pushf`/`cli` window at memory.inc:4559-4560 and closes
it at 4664. Path 3 calls `mem_bcopy` inside that window (4640), so a grow that
has to move copies up to the whole block at IF=0:

- the kernel's own figure, ~86 ms for 32KB (memory.inc:2030);
- about twice that for a 64KB Paint or Note Pad buffer.

The compactor moves the same kind of block at **IF=1 under `[sch_lock]`**
(2973-2977; only a driver image moves at IF=0). That is because nothing at
interrupt level touches `mem_tab`, so only other tasks need excluding.

At IF=0 for 86-170 ms:

- the 8259 holds one IRQ0, so every tick past the first 55 ms is lost (clock
  drift, missed task-slice accounting);
- a serial mouse at 1200 baud (8.3 ms a byte) overruns the 8250's two-deep
  receive path inside ~17 ms and drops packets;
- a 2400-baud link (WireBall, Serial) loses bytes;
- a Sound Blaster auto-init IRQ is served late.

`apps/fractal/fractal.asm:846-848` already plans around "mem_regrow's IF=0
window".

**Fix:** keep the map writes at IF=0 but run the copy under the scheduler
lock. After `mem_hifit` has found CX:

```
inc byte [sch_lock]
popf / pushf
mem_bcopy
cli
```

then the record update, then `dec byte [sch_lock]`. The destination run cannot
be claimed while switching is paused, because no ISR claims. **~+10 `.cold`.**

### hm-2 (hazard, medium, now): the owner fence's self-match lets a package FREE or REGROW its own region, and the victim is the next claimant

`mem_find_own` (memory.inc:4399-4425) matches `DX == BX`, a record based at the
caller's own segment, so `OSAPI_MEM_MOVABLE` can declare a region. The routine
is the shared fence for `mem_free_x` (4459) and `mem_regrowp` (4576) as well.

SPEC.md 66.6.1.1 (around line 120290) accepts this as "a package shooting
itself". It is not only itself:

- **`OSAPI_MEM_FREE` with `DX = CS`** zeroes the region's record while the
  instance is live. Its windows still far-call `W_SEG`, and the next claim (a
  raise cache, another package's canvas) is handed paragraphs that are still
  executing.
- **`OSAPI_MEM_REGROW` path 3** copies the region elsewhere and updates
  `MC_SEG`, but calls no relocation proc (by design, 66.7) and no
  `mem_region_reloc`. `I_SPTR`, `W_SEG` and `MB_SEG` all keep naming the old
  base, and so does the caller's own return CS.

Both are one wrong register in a package, and both corrupt something else.

**Fix:** refuse `DX == BX` in `mem_free_x`, and refuse it in `mem_regrowp`
except on path 1 (a shrink, which keeps the base). **~+6 to +8 `.cold`.**
Alternatively, gate the self arm on a flag only `mem_movable_x` sets (~+4).

### hm-3 (hazard, medium-low, now): DESK.DRV is dropped on a worker while the UI task may be executing in it

`inst_task_die` (instance.inc:1938-1940) calls `desk_drop_x`, which calls
`mod_drop` (deskmod.inc:129-130), on the dying worker. That is "a Sticky App on
its worker arm closes HERE, on that worker".

The UI task enters DESK.DRV's `dkd_hover` with no lock
(`call far [DKDFP + DKD_HOVER]`, ui.inc:1006), so the worker can pre-empt it
there. `mod_disarm`'s `cli` (mod.inc:880-886) covers the torn slot and nothing
else. `mod_drop`'s own banner (mod.inc:947-960) says the caller must know the
module is not on the stack, "which does not fault".

- On kern_big the UI task resumes in a freed claim. It is harmless until
  something claims and writes those paragraphs.
- On kern_mega `mod_free_row` frees the selector (mod.inc:915-919). An IRQ
  inside `dkd_hover` then IRETs to a dead CS: #GP.

**Fix:** from a non-UI task, post the drop instead of doing it: a byte the UI
pass tests, the `inst_launch_post` shape (instance.inc:2237). **~+8 to +12
resident.**

### hm-4 (hazard, medium-low, now): `MEM_OWNER_MAX` binds the kernel's own tags, and every driver image shares one tag

`mem_claim_1`'s owner count (memory.inc:1093-1112) counts every record whose
`MC_OWN == BX`, with no exemption. Every driver image is claimed under the one
tag `MEM_K_DRV` (driver.inc:3513, 3522, 3856, 3945, 4341, 4493), and
kern_big's `drv_tab` has `9 + DRV_VX + DRV_PX` rows (driver.inc:80). So on a
machine with no HMA, the ninth conventional driver image is refused with
"not enough memory" over a free heap. HDDTOOL's second image counts against
the same eight. Every module is likewise `MEM_K_MOD`.

**Fix:** skip the count for `BH = 0xFF` (kernel tags). The cap exists to charge
a package for exhausting the table, and the kernel is not that package.
**~+5 `.cold`.** No gate covers it today (`grep MEM_OWNER_MAX tests/`).

### hm-5 (perf/hazard, medium-low, now): the long walks hold IF=0 where `[sch_lock]` is enough

`mem_avail_lvl_p` (memory.inc:4927-5023) runs the whole planning walk inside
`cli`, and so does `osapi_mem_compact_x`'s what-if (5753-5772).

The walk is O(n²). `mem_cp_next` (2680-2720) scans all 32 records for every
live one, and `mem_can_move` runs per record (`mem_in_xfer`, `inst_ptr`,
`mem_in_nest`). PREDICTED: ~2.5k cycles per `mem_cp_next` × 16 live claims,
plus ~6k for the predicates, comes to **~10 ms at IF=0** for one
`OSAPI_MEM_AVAIL`, and ~20 ms with a full table. That is past the 1200-baud
mouse's overrun window. Every claim-then-avail package pays it twice.

The map is never touched at interrupt level, so `[sch_lock]` gives the same
exclusion. `[mem_cp_busy]` and `[mem_cp_pl]` are already the re-entry guards.
`mem_claim_1`'s scan (1070-1238) is O(restarts × n) and short. Leave it.

**~0 to +6 `.cold`.**

### hm-6 (bug, low, now): `mem_claim_dma_x`'s head shift trusts CF, which is only the last bit out

memory.inc:860-868 shifts the head with `mov cl, 6 / shl si, cl / jnc .set`.
CF is bit 10 alone, so:

| CX (KB) | SI after the shift | CF | result |
|---|---|---|---|
| 1024 | 0 | 1 | refused, correctly |
| 2048 | 0 | 0 | **"no page constraint", granted silently** |
| 65-1023 | > 0x1000 | - | refused by `mem_claim_1`'s 0x1000 test |

This is the same defect CODE-REVIEW-TWO-CYCLES #6 fixed in `mem_regrow`'s KB
entry, which now guards with `cmp ax, 1024` first. Its sibling was missed. It
reaches packages through `OSAPI_MEM_CLAIM_DMA[_HI]`.

**Fix:** `cmp cx, 64 / ja <refuse>` before the shift, then drop
`jnc / mov si, 0xFFFF` (five bytes). **~-2 `.cold`.**

### hm-7 (hazard, low-medium, now): a lazy part is re-read BY NAME with no check that the file is the one the program came from

`op_fetch` reads through `op_read`, which hands `os88_rseq` the 8.3
`op_name` captured at launch (os88partsbody.inc:789-794). It checks only that
enough bytes arrived (829-831).

Suppose the floppy is swapped, or the file is replaced (The Wire, FTPD, a
host copy) while the program runs. A plain `OP_SEG` lazy part is then fetched
from another build at the same offset and far-called. The kernel's loader
defends exactly this case by re-running `ld_check_hdr` on what the full read
delivered (loader.inc:1091-1093). Windows, OS/2 and GEOS kept the executable
open for later segment reads, and the Mac kept the resource file open.

**Fix:** `op_load` banks the file's 32-bit size and first cluster (one
`dskw_stat` it can ask anyway), and `op_fetch` compares them before trusting
the read. A refusal toasts "Disk changed". **~+20 bytes per parts package,
0 kernel.**

### hm-8 (bug, low, now): a cache table that is out of RECORDS triggers a whole compaction before the shed that would fix it

When `mem_tab` has no free record, `mem_claim_1` fails at `.fail0`
(1103-1105). `mem_claim_x` then runs `mem_compact` (953-955), which plans,
and if a run would fit, copies kilobytes and calls every relocation proc. Then
it retries and fails again for want of a record. Only after that does it
`mem_shed_one`, which is what frees a record.

A compaction can never create a record. Return "no record" distinctly (for
example DX = 1 on the refusal; DX = 0 is the other one) and skip straight to
the shed. **~+6 `.cold`.** It saves a full copy pass on the full-table path.

### hm-9 (rethink / prior-art, medium value, now): package purgeable claims, through the relocation proc that already exists

The Mac (`HPurge`), Windows (discardable) and GEOS (discardable blocks) all let
an APPLICATION hold memory "on loan". In os8088 only kernel tags can be
purgeable: `mem_pg_own` is a closed kernel list (memory.inc:1306-1331). Two
consumers are waiting:

- Skies' title art across a flight, worth -485 ms of exit
  (HANDOFF-APPS-SHARED.md:153);
- the Text Screen's shadow, which costs ~420 ms to rebuild
  (SPEC.md ~92629).

SPEC.md 50.6.2 (~99912) says packages "will need a handle-based ABI rather
than a bare segment". **The new argument:** since §66, a package that holds a
movable claim already has to be trusted to re-point every word through its
`MC_RLOC` proc. A purge is a move to nowhere: call the same proc through
`mem_reloc_call` with `DX = 0`, and the package zeroes its words. It is the
contract `MEM_P_*` consumers already obey, run through the package's own
dispatcher.

Mechanics:

- **The flag:** `MC_DMA` bit 13 is free, because a head is at most 0x1000 and
  bits 0-12 hold it (`MC_DMA_HEAD` masks 0x3FFF for no reason).
- **The rank:** fixed at `MEM_PG_TRIV`, or the bit reads as TRIV/LOW.
- **The guard:** `mem_shed_one` and the compactor's drop arm must ask
  `mem_can_move` first, so a worker holding the block, or a callback standing
  in it, pins it.
- **The door:** a verb on `OSAPI_MEM_P` (`MEMP_PURGEABLE`, AX = the proc),
  appended, so there is no bump.

**~+50 to +70 `.cold`.** Not on kern_small, whose shed has no proc call
(or, cheaper there, purge-without-notice is refused).

### hm-10 (rethink, flag day): name a claim by its RECORD at the API, keep segments for access

A claim's identity is its segment value, and that one choice costs in four
places:

1. **Holders are found by scanning for a value.** `mem_rr_tab` has 26 rows
   across four address spaces (memory.inc:3349-3484), and every new kernel
   word that can name a package segment must be registered or a move silently
   corrupts. The IVT case is the proof: an unfiltered scan rewrote an XT-IDE
   ROM's scratch word (3121-3139).
2. **Every door walks the table to find a record.** `mem_find_own` does it on
   every free, regrow and movable.
3. **The fence has holes.** hm-2 is one.
4. **kern_mega pays twice.** It carries dual selector paths in every door
   (`MEGA_ISHI` forks, ABI-BREAK-LIST R2-6: +48/+97/+42/+88/+47 measured
   forks).

Windows real mode, the Mac and GEOS all separated the handle from the address.
os8088 does not need the double indirection for ACCESS: its holders can keep
loading segments, as a Windows real-mode program did after `GlobalLock`. The
cheap half is that `OSAPI_MEM_P`'s claim verbs also return the record index
(stable across moves, unique while live), and FREE, REGROW and MOVABLE take
it. That deletes `mem_find_own`'s walk on those paths, and kern_mega's
selector/base dual paths with it.

- **Listed as** R2-6 ("recorded"), paired with C1.
- **New argument:** C1 already converts every package call site to
  `OSAPI_MEM_P`. Doing both at once is one conversion, not two. The record
  index also makes hm-2's fence exact: the owner is checked against a known
  record, not inferred from a base.
- **Bytes:** ESTIMATED **-30 to -60 `.cold` on kern_big**, several hundred
  on kern_mega (R2-6), +2 per package call site.

### hm-11 (size, medium, now, package-side): the parts body is resident in every parts package that does not re-home

MEASURED `op_*` code spans:

| map | bytes |
|---|---:|
| `build/browser.map` | 1,106 |
| `build/pixel.map` | 1,388 |
| `build/video.map` | 1,610 |
| `build/dosp.map` | 1,998 |

Twenty-one packages carry the body. The four loader-shaped ones (Word,
Scribe, Skies, DOS) discard it by re-homing (SPEC.md 20.12.10). The rest keep
`op_load`/`op_size`/`op_claim`/`op_read`/`op_unpack`/`op_xload` for their
whole life, and pay again in every instance (hm-15). The part of it used after
the entry proc is `op_seg`, and `op_fetch`/`op_drop` when there are lazy rows.

DOS TSRs, Windows' discardable INIT segments and Linux's `__init` all drop
init-only code. Assemble the load-time body after the program's own code with
an `OP_INITDROP` option. Once `op_load` has run, the entry proc shrinks its
own region to the program (the KEEP re-home, or `OSAPI_MEM_P` MEMP_REGROW's
shrink, path 1). That needs the bss chain moved below the dropped tail, which
the parts bss equ chain already parameterises (`OP_BSS_AT`).

**~-0.8 to -1.5KB resident per running instance, 0 kernel bytes.** Its cost is
in the parts include and `os88pkg.py`.

### hm-12 (weakness, low-medium, now): `MEM_OWNER_MAX = 8` is the most-negotiated constant in the package tree, and there is no shared sub-allocator

SPEC.md names Sheet "at 8 of 8" at least six times (142136, 148100, 149167,
149393, 149491, 150964). The parts standard's single carve and WIN16's arena
allocator (SPEC.md 235006, 241357) both exist to get under the cap. Sheet,
DOS, WIN16 and WEAVE (`wgrid.c`) each hand-roll a sub-allocator inside a claim.

The period answer was a second-level allocator owned by the system: Windows'
`LocalAlloc`, OS/2's `DosSubAlloc`, GEOS's LMem heaps. Two options:

- **(a)** Raise `MEM_OWNER_MAX` to 10 on kern_big. 0 bytes; the table stays 32
  records and the cap still charges the app that hogs it.
- **(b)** Lift WIN16's arena allocator into a shared `apps/os88lmem.inc`
  (SPEC.md 20.5's "one source for two worlds" shape). 0 kernel bytes.

(a) is free. (b) is the systemic fix.

### hm-13 (weakness, low, now): the floor is stamped per TASK, but every package callback is the same task, and any teardown lifts it

`mmf_osapi_mem_floor` stamps `[sch_cur]` (memory.inc:5850). Every package's
callbacks run on task 0, so one package's floor lowers every other package's
claims on the UI task, which the SDK prose concedes (os88api.inc:1808-1812).

`mem_free_rec_x` (4826) then lifts the floor on ANY instance's teardown,
whoever set it. So another window closing between DOS's post and its wake
undoes DOS's floor.

**Fix:** stamp the instance (`inst_caller`'s DH) instead of the task, lift
only when the dying slot matches, and change "your task's" to "your
instance's" in the SDK. **~+6 to +8 `.cold`**, and a contract clarified, so
no bump.

### hm-14 (consistency, low, now): `OSAPI_MEM_P` treats any unknown verb as `MEMP_AVAIL`

`osapi_mem_p` (memory.inc:5582-5644) answers BL ≥ 7 with the avail answer, and
returns with the caller's entry flags (`mem_avail_lvl_p` ends in `popf`). An
unknown verb therefore "succeeds" with whatever CF the caller had.

**Fix:** `cmp bl, MEMP_KPARTS / ja <stc retf>`. **~+4 `.cold`**, or write the
behaviour into the SDK.

### hm-15 (prior-art record, not recommended): instances share no code

Two instances of one package load two complete images (`ld_alloc` claims
`roundup512(img + bss)` per launch, loader.inc:1166-1227). Windows shared code
segments between instances. That was what `hInstance`, the per-instance
DGROUP and `MakeProcInstance` existed for. OS/2 shared DLL and EXE code.

Here CS = DS = the region by contract (CLAUDE.md "near model"), so sharing
means a DS ≠ CS package model. That is every package rewritten, not a row. Two
cheaper shapes could still help:

- a refcounted shared `OP_SEG` part keyed by (file, part), since a far-called
  part already runs on the caller's DS. ~+80-120 kernel `.cold` for the
  registry, worth it only once a multi-instance package with a large code part
  exists;
- the parts init-drop of hm-11.

### hm-16 (record): two-ended placement is Windows' fixed/moveable/discardable zoning in two zones

Windows real mode put FIXED blocks at the bottom of the global heap,
discardable code at the top, and moveable in between. os8088's two doors
(memory.inc:770-801) are the same idea, with `MC_RLOC` doing what the zone
did. SPEC.md 50.2 now says the split is a preference. The memory.inc comment
still says it is a law (doc mismatch D-5). Nothing to change in the code.

## Rethinks

- **hm-9, package purgeable via `MC_RLOC`.** The one systemic feature every
  period GUI had and os8088 lacks. It is small because the proc machinery is
  already paid for.
- **hm-10, handles at the API.** Take it at the C1 flag day or not at all.
- **hm-11, discard init code.** The DOS/TSR and Windows INIT-segment idea
  applied to the parts body.
- **hm-12(b), one system sub-allocator,** replacing four hand-rolled ones and
  most of the `MEM_OWNER_MAX` negotiation.
- **Refused here, on purpose:** automatic demand loading and LRU discarding of
  package code (Windows' reload thunks, GEOS swap). On a one-floppy XT every
  automatic reload can be a disk-swap prompt in the middle of a gesture. The
  explicit `op_fetch`/`op_drop` and `mod_need`/`mod_drop` are the right policy
  here, and the Mac reached the same conclusion with `UnloadSeg`.

## Prior art

| system | model | what it did better | what os8088 does better | cheap to adopt |
|---|---|---|---|---|
| DOS 2-6 | MCB chain in front of each block. Owner = PSP. Strategies first/best/last fit. UMBs. TSR keep-paragraphs. EXE fixups | Unbounded block count. Best-fit as an option. Every program can resize its own block (4Ah) | The table is out of band, so a program cannot corrupt the allocator. 64KB pages are known. Compaction exists | Nothing structural. The keep-paragraphs idea is already `OSAPI_PKG_REHOME` KEEP |
| Windows 3.0/3.1 real mode | Handles over a moveable/discardable global heap. `GlobalLock` gives a segment. NE segment table with LOADONCALL/PRELOAD/DISCARDABLE. Reload thunks (INT 3Fh). Stack walking patches return addresses into discarded segments. `MakeProcInstance` loads DS. Resources separately discardable | Handles decouple identity from address. Applications get discardable memory. Code is shared across instances. Resources are discardable | No relocation records. The `PKG_DISP` dispatcher does once what `MakeProcInstance` did per callback. Planning compaction. Cost-ranked caches instead of LRU | hm-9 (discardable application memory via the proc). hm-10 (handle at the API) |
| Windows 3.x standard/386, OS/2 1.x | LDT selectors per process. Segment-not-present faults load on demand. DLLs shared. Swapping to disk. `DosSubAlloc` | Moves need no cooperation (rebase the descriptor). One copy of shared code. A system sub-allocator | kern_mega already rebases selectors instead of telling holders (memory.inc:2995-2998). That is OS/2's model, available where the CPU has it | hm-12(b) (sub-allocator include) |
| Mac System 5-7 | Master pointers (handles). `HLock`/`HPurge`. A per-application heap under MultiFinder. GrowZone proc. Resource Manager with purgeable resources. CODE resources with a jump table, `LoadSeg`/`UnloadSeg` | Applications can mark data purgeable. The GrowZone callback lets an application free memory on demand. Partitions isolate applications from each other | One shared heap, so no partition sizing guesswork (MultiFinder's "Memory Requirements" box). Explicit `op_fetch`/`op_drop` = `LoadSeg`/`UnloadSeg` with the same honesty | hm-9 is HPurge plus the GrowZone in one proc call |
| GEOS (8088) | A handle table for everything. Blocks movable/discardable/swappable. LMem chunk heaps inside blocks. VM files. Geode resources with relocation tables. Swap drivers to XMS/EMS/disk | Automatic swap to XMS/disk. LMem as the system sub-allocator. Geode code resources discardable and reloadable | Far less machinery: no handle indirection on every access, no relocation pass, and the kernel's resident cost stays inside 100KB | LMem's idea as hm-12(b). os8088's XMS store (§41) is explicit, which suits a machine without a swap driver's footprint |
| AmigaOS | `AllocMem` with attribute flags (CHIP/FAST/PUBLIC). No ownership, no protection, no compaction | The attribute flags are the clean form of os8088's DISK/CPU/HEAD and `_HI` doors | Ownership and teardown by owner. AmigaOS leaked memory on every crash | Already done: `OSAPI_MEM_P`'s verb bits are attribute flags |

## Doc/code mismatches

| id | file:line | current text (short quote) | what the code does | proposed text |
|---|---|---|---|---|
| D-1 | kernel/memory.inc:4-5 | "above the package pool, handed out in KB-granular, segment-aligned blocks" | There is no pool (SPEC.md 20.1). Claims are `MEM_GRAN` = 32-paragraph (512-byte) multiples (memory.inc:458-463, 954-960) | "every paragraph of conventional memory above the kernel and stage 2's blob, handed out in 512-byte-granular (MEM_GRAN), segment-aligned blocks" |
| D-2 | kernel/memory.inc:77-78 | "owner - 0..INST_MAX-1 is an instance slot; 0xFF00 \| tag is the kernel's own" | A package's and a driver's DATA claims carry the caller's SEGMENT (`mem_own` .yes, 5964-5966), and purgeable caches are 0xFBxx-0xFExx | "owner - 0..INST_MAX-1 an instance slot (a region or a built-in's claim); a segment (a package's or a driver's own data claims, SPEC.md 50.2); 0xFF00\|tag the kernel's own; 0xFB00-0xFEFF a purgeable kernel cache, the high byte its rank" |
| D-3 | kernel/memory.inc:605-606 | "COLD_SEG is its seed and is below the heap, so the compare refuses it" | `spl_fseg` is seeded 0 (kernel.asm:5240) and the test is `cmp word [spl_fseg], 0` (611) | "0 is its seed (kernel.asm's spl_fseg), so the compare skips the raise and that kernel keeps the whole floor" |
| D-4 | kernel/memory.inc:531 | "out:      nothing (all registers preserved)" (mem_init) | `mov bp, mem_floor_ax` (583) clobbers BP. The caller is known not to care | "out: nothing; every register preserved but BP (wm_cbd's argument - kmain_o holds nothing in it)" |
| D-5 | kernel/memory.inc:784-787 | "a package region can NEVER move, because its base IS its CS ... SPEC.md 66 compacts the DATA arena and deliberately leaves regions out" | `mem_can_move` moves a frameless region (2054-2083). `mem_region_reloc` rewrites its holders (3229). SPEC.md 20.1 calls the inference "wrong wherever it appears" | "a package region is a CS, so moving it costs its holders' procs and mem_region_reloc (SPEC.md 66.6.1): it CAN move when frameless, and putting it high keeps it out of the data traffic. The door is a preference about cost, not a statement about movability - MC_RLOC alone says that" |
| D-6 | kernel/memory.inc:1946-1950 | "any claim with MC_DMA != 0 is pinned outright" | `mem_can_move` no longer tests MC_DMA (its own comment, 1987-2000; SPEC.md 66.4.2). `mem_cp_dest` places a head page-safely | "INTERRUPTS - not bounded by sch_lock, so a block a chip is armed on is pinned by its holder never declaring it (66.4.2); MC_DMA is a PLACEMENT rule that mem_cp_dest honours, not a pin" |
| D-7 | kernel/memory.inc:1952 | "(a worker may not claim - SPEC.md 20.6 rule 7)" | Workers do claim and regrow. The kernel serves them first-fit only (948-956, 4704-4706), and Fractal's worker regrows (`apps/fractal/fractal.asm:858`) | "(a worker's claim is served first fit only - no compaction, no shed, SPEC.md 66.3.1 - so the compactor never runs under a worker's frame)" |
| D-8 | kernel/memory.inc:2601-2603 | "a purgeable block is never relocated - 63KB with a 64KB page head cannot be packed tight" | A purgeable cache that declares a proc moves (`mem_can_move`, 2004-2037: "the window is 32KB and claims no head") | "a purgeable block that declared no proc is never relocated, and dropping it is cheaper than the move it was refused" |
| D-9 | kernel/memory.inc:3207, 3214, 3218-3219 | "SPEC.md 66.6 lists twelve holders" / "~70 compares" / "THREE ADDRESS SPACES, which is why there are three loops ... the seven scalars" | One loop (`mem_rr_walk`) over FOUR address spaces (DS, SS, 0 and kern_mega's IVT walk), 26 `MEM_RR_ROW`/`MEM_RR_ONE` rows, and a dozen-plus scalars (`mem_rr_lone`) | "every holder `mem_rr_tab` names (SPEC.md 66.6) ... the walk is a few dozen compares ... FOUR ADDRESS SPACES, ONE LOOP (mem_rr_walk): .bss through DS, .lowbss through SS, the IRQ vectors through segment 0" |
| D-10 | kernel/memory.inc:6417 | "The claim heap's unit is the kilobyte" | The unit is `MEM_GRAN` paragraphs. The KB doors are one of two families (`mem_claimp_*`, `OSAPI_MEM_P`) | "The KB doors' unit is the kilobyte, so every KB caller that starts from a byte count spells this out" |
| D-11 | kernel/memory.inc:5705, kernel/kernel.asm:4133, apps/os88api.inc:1737 | "exactly as OSAPI_MEM_AVAIL_LVL's" / "out AX/BX as OSAPI_MEM_AVAIL_LVL" | `OSAPI_MEM_AVAIL_LVL` is retired (SPEC.md 50.6.6.1, ~100354). The level is the caller's `OSAPI_MEM_FLOOR` | "out AX/BX as OSAPI_MEM_AVAIL at the level in AL" |
| D-12 | apps/os88api.inc:1793, 1806 | "OSAPI_MEM_AVAIL_MAX at the same level" / "post OSAPI_MEM_COMPACT_WAKE" | Both were merged into `OSAPI_MEM_COMPACT` (MEMC_WHATIF / MEMC_POST, SPEC.md ~118639) | "(and OSAPI_MEM_COMPACT's MEMC_WHATIF at the same level)" / "post OSAPI_MEM_COMPACT's MEMC_POST" |
| D-13 | apps/os88api.inc:1814-1815 | "0x0568 beside it is RETIRED (stc/ret)" | The cell is deleted (kernel.asm:4074 "(dropped)", SPEC.md 20.3.1). The table ends below 0x0568 | "OSAPI_MEM_CLAIM_LVL, the cell that once carried the floor as an argument, is withdrawn (SPEC.md 20.3.1)" |
| D-14 | SPEC.md:100387 | "posts `OSAPI_MEM_COMPACT_WAKE`, RETURNS" | as D-12 | "posts `OSAPI_MEM_COMPACT`'s `MEMC_POST`, RETURNS" |
| D-15 | SPEC.md:98903 | "`MEM_MAX` (**32**) records × **8** bytes" (and the record listing below it) | `MC_SIZE` = 10. `MC_RLOC` at +8. `MC_DMA` bit 15 is `MC_DMA_HI`, bit 14 `MC_DISK`, bits 0-13 the head (memory.inc:76-125, 157-166). kern_small `MEM_MAX` = 16 | "`MEM_MAX` (32 on kern_big, 16 on kern_small) records × 10 bytes", and add `MC_RLOC 8 word: relocation proc, 0 = pinned (§66.2)` and the two `MC_DMA` high bits to the listing |
| D-16 | SPEC.md:98974-98981 | "`MEM_K_SAVE` \| `MENU_SAVE_KB` = 20 ... released before it returns" / "the back buffer stays off ... a Disk window reads the global mount snapshot" | The save-under is `MEM_P_MSAVE`, a TRIV purgeable sized from the rect (menu.inc:1836-1890). 0xFF01 is retired (memory.inc:212-213). The back buffer (§32) and the mount snapshot (LISTING-HOME-PLAN 13) are gone | Replace the table with the live kernel tags (`MEM_K_DRV`, `_COPY`, `_MOD`, `_CLONE`, `_BAND`, `_HIB`, `_CMPR`, `_FONT`, `_SC`, `_IMG`, `_CLIP`, `_ASC`, the `MEM_P_*` caches) with a "see memory.inc" pointer, and the fallbacks as they are now: the save-under repaints, a Disk window re-reads its listing |
| D-17 | SPEC.md:50444, 50447 | "now a whole-KB multiple because that is `mem_claim`'s granularity" / "No compaction — regions never move once loaded" | `ld_alloc` claims `roundup512(img+bss)` (loader.inc:1166-1227). A frameless region moves (§66.6.1) | "a 512-byte multiple (`roundup512(img + bss)`) ... A region moves only when frameless (§66.6.1); it is allocated from the far end because that keeps it out of the data traffic" |
| D-18 | SPEC.md:50450 | "`ld_alloc`, the first-fit-over-`inst_tab` allocator this replaced, is gone" | `ld_alloc` exists (loader.inc:1166), now steps 4-5: the record and the region claim | "The first-fit-over-`inst_tab` allocator this replaced is gone; its name is reused by steps 4-5's `ld_alloc`, which reserves the record and claims the region" |
| D-19 | SPEC.md:50496, 50502 | "HIGH word is below `PKG_FILE_HI`** (16 —" / "1..15 pass ... up to 1MB" | `PKG_FILE_HI` = 512 (kernel.asm:87), tested as DH < 2 (`ld_take`, loader.inc:797). Files under 32MB pass | "below `PKG_FILE_HI` (512 - else status 3) ... 1..511 pass, because a package carrying parts may be up to 32MB less a byte" |
| D-20 | kernel/kernel.asm:95-96 | "a part's offset is a word of 512-byte sectors (SPEC.md 20.12.3)" | A part's `off` is in 16-byte PARAGRAPHS since §20.12.2.1 (os88parts.inc:198-214). A word of them reaches 1MB, not 32MB | "a FILE_READ_AT offset's div by 512 needs the high word under 512 - though a part's own `off` is a word of PARAGRAPHS (SPEC.md 20.12.2.1), so a part starts inside the first 1MB of the file whatever this allows" |
| D-21 | SPEC.md:67667-67668, 67671, 67675 | "the package-region allocator derives occupancy from it" / "`INST_MAX equ 12`" / "bit0 = minimized" | Regions are heap claims and `mem_tab` is the allocator. kern_small `INST_MAX` = 6 (instance.inc:69). `I_FLAGS` also has `IF_FSMIN` (bit 1) and `IF_DESK` (bit 2) (instance.inc:27-48) | "the claim heap (§50) holds each package's region" / "`INST_MAX` 12 (6 on kern_small)" / "bit0 minimized, bit1 IF_FSMIN, bit2 IF_DESK" |
| D-22 | docs/KERNEL-MEMORY.md:757-758 | "`mem_claim` rounds to whole KB, so every base it hands out is `HEAP_SEG` + n·64 paragraphs" | `mem_claim` rounds to `MEM_GRAN` = 32 paragraphs. Guard 6b says so (kernel.asm:9642-9655) | "`mem_claim` rounds every size to `MEM_GRAN` paragraphs (512 bytes), so every base it hands out is the floor, the top or a claim edge - 512-aligned" |
| D-23 | docs/HEAP-CLAIMS.md:36 | "`MC_HI` in the record is that fact" | There is no `MC_HI` field. The direction is `MC_DMA` bit 15, `MC_DMA_HI` (memory.inc:157) | "`MC_DMA_HI` - bit 15 of the record's `MC_DMA` word - is that fact" |
| D-24 | SPEC.md 20.6 rule 7 (~46081-46087) | "Only the background-task surface: `gfx_*` ... `wm_saveu`" | The kernel deliberately serves worker claims and regrows first-fit (memory.inc:944-956, 4704) and Fractal's worker regrows its cache | Add "`OSAPI_MEM_CLAIM`/`_REGROW`/`_AVAIL` (first fit only: a worker's claim never compacts or sheds, §66.3.1)", or, if workers are meant not to claim, record Fractal as the exception |

## Open questions

1. **hm-1:** is the regrow copy at IF=0 deliberate beyond the comment's
   "nothing can have cleared the record"? `[sch_lock]` gives that, unless
   something at interrupt level claims. The audit found nothing that does.
2. **hm-4:** does any shipped `SYSTEM.CFG` combination load nine
   conventional driver images? On a 286+ the HMA arm and kern_mega's high move
   take most of them out of `mem_tab`. On an XT none do.
3. **hm-9:** should a package purge be refused outright while the owner's
   worker is unparked, or should the claim be pinned for that pass the way a
   move is? This review assumes "pinned", i.e. reuse `mem_can_move` unchanged.
4. **hm-11:** which of the seventeen non-loader parts packages are ever
   multi-instance? That decides whether the init-drop pays per instance or
   only once.
