# kern_mega spans no reviewer owned: fsx.inc's real-mode bracket service twins and mega.inc's image/region/SUB/window-table family

Gap review, big review 2026-11-08. Tree at `14428309` (the round-1 doc commit,
comments only over `42072b88`); spans from `build/megak/kernel.map` (built
after `42072b88`, so current for code). Nothing was run: no make, no emulator.

## Scope read

| file | lines | what |
|---|---|---|
| `kernel/fsx.inc` | 1,455-2,960 in full | `fsx_rprepf` (the door), `fsx_rmvck`, `fsx_rlowclaim`, `fsx_rfree`, `fsx_rlist`, `fsx_rsvcf`, `fsx_rxrows`, `fsx_rdrvf`, `fsx_rextf`, `fsx_rdlow`, `fsx_rsndq`, `fsx_rcopydf/_rcopyf/_rcprow/_rcp1`, `fsx_rdownf`, `fsx_rsnddn/_rsndv/_rsndup`, `fsx_rkeep`/`fsx_rkeepm`, `fsx_rvdn/_rvup`, `fsx_rexfill`, `fsx_rserdn/_rserup`, `fsx_rupf` |
| `kernel/fsx.inc` | 2,962-3,134 | `fsx_rwalkf` (`.cold`) |
| `kernel/fsx.inc` | 3,135-4,109 in full | `rms_lo`..`rms_hi`: `rms_ncmp`, `rms_rseq`, `rms_xfer`, `rms_xcaps`, `rms_xcopy`, `rms_xchk`, `rms_ucopy`, `rms_lcopy`, `rms_lend`, `rms_drvx/_drva/_drvb`, `rms_stick`, `rms_sstr`, `rms_spk`; the `.lowbss` state |
| `kernel/fsx.inc` | 1,250-1,455 | `fsx_rcall`, `fsx_rland`, `fsx_rtick` (for the exits; video.md owns them) |
| `apps/os88la.inc`, `apps/os88ub.inc`, `apps/os88real.inc` (head) | all / all / 1-40 | LOADALL borrow, the 386 loaded base, the package half |
| `kernel/mega.inc` | 12,200-12,760 | `mega_pkghif`, `mega_imghuge`, `mega_imghif`, `mega_pkghifree`, `mega_rgnblk`, `mega_rgnsubfree`, `mega_subsegf`, `mega_rgnconv`, `mega_drvhifree`, `mega_imghifree` |
| `kernel/mega.inc` | 12,880-12,990, 13,195-13,940 | `mega_segf`'s SUB arm, `mega_wfind`, `mega_wset`, `mega_wnew`, `mega_wslot`, `mega_blkof`, `mega_bclip`, `mega_wbind`, `mega_bown`, `mega_segbw`, `mega_segtile`, `mega_segh`, `mega_segaim` |
| `kernel/mega.inc` | 13,940-14,830 | `mx_inblk`, `mega_dfind`, `mega_ishif`, `mega_mpix`, `mega_mpbase/_mpnewf/_mpsyncf/_mpgap`, `mega_mpfreef`, `mega_mpmovef`, `mega_mpall`, `mega_rseg`, `mega_useg`, `mega_vword`, `mega_mxtinit`, `mega_vwalk`, `mega_vxpm`, `mega_cdin`, `mega_iswin`, `mega_cdfree`, `mega_dklibf`, `mega_dklibfree`, `mega_ldtd` |
| for the twin diffs | | `mx_chk`/`mx_key`/`mx_tophys`/`mx_rel` (mega.inc 9,180-9,760), `mx_dfree` (9,414), `mega_ldt_alloc2`/`mega_ldt_free`, `mega_up`, `osapi_fsx_spk`/`spk_off` (snd.inc 1,034-1,150), `osapi_segr`/`osapi_mem_subseg` (kernel.asm 6,280-6,420), `mem_own` (memory.inc 5,917), `mod_free_row` (mod.inc 909), `ld_unreserve` (loader.inc 230) |
| SPEC.md | 107,093-107,500 | §53.2.3 whole, §34.5.3 (external ring) |

## Verdict

Two very different families share this gap.

**The real-mode bracket's twins are good engineering, placed in the right
bytes.** The whole protected-mode door/drop/return plus the twins is 5,068
bytes of `.mega` (`fsx_rprepf` 4844h to `rms_hi` 5C10h), `fsx_rwalkf` 443 of
`.cold`, and only 282 bytes of `.text` (`fsx_rgone` 2, `fsx_rcall` 24,
`fsx_rland` 67, `fsx_rtick` 189) plus 98 of `.lowbss` - so on a switched
machine almost none of it is in `MEGA_RESIDENT`. The service area's twins
(2,352 bytes) are COPIED out of `.mega` into a per-bracket conventional claim,
which costs no resident byte and no disk read. The twins I diffed are true
twins or documented specialisations: `rms_xchk` is `mx_chk` specialised to a
token owner (the only kind the bracket's slot is), `rms_spk` is
`osapi_fsx_spk` + `spk_off` with the tone case refused as SPEC says,
`rms_drvb` is `drv_pkg_call_x`'s shape with EMS's slot in BH, `osapi_segr` is
one body for both callers. The LOADALL and loaded-base transports leave the
descriptor caches sane: `mega_up` (mega.inc 3,695) reloads GDTR, IDTR, LDTR,
CS (far jump), DS, ES and SS, which is every cache LOADALL's zeroed table or
`ub_set` can leave, and kern_mega has no TSS for LOADALL's zeroed TR to hurt.

**One guard in the door checks the wrong owner (gms-1).** `fsx_rmvck` matches
`MC_OWN` against the instance SLOT, but a package's own claims - exactly the
ones `os88real_prep` banks - are recorded under its SEGMENT (`mem_own`). So
the guard is blind to what SPEC.md 53.2.3 says it protects, and the one thing
it does catch (the region itself moving) is a needless fallback. The same
defect was found and fixed in `mega_bown` (14.139's follow-up) and not here.

**The derived-descriptor family is where the risk lives.** os8088 has ten
kinds of LDT descriptor (claim pair, region pair, SUB, SUBC pair, tile,
handle, bound window, unbound window, `mega_ustab` row, DESK.DRV's library
alias) told apart by geometry and side tables, and at least seven walkers
(`mx_dfree`, `mega_cdfree`, `mega_rgnsubfree`, `mega_mpmovef`, `mx_desc`,
`mega_mpgap`, the two match scans) each re-derive which kinds they may
touch. Each new kind has needed a fix in each walker (14.80, 14.128, 14.135,
14.139). In this span two walkers missed a rule the others enforce:
`mega_rgnsubfree` frees unbound windows (gms-2, the 14.80 class) and
`mega_subsegf`'s private match answers a window or a handle descriptor
(gms-3, the 14.128 class). And the SUB family computes a base and then
writes a descriptor with no `sch_lock`, the rule `mega_subsegf`'s own
comment states (gms-4). The fixes are small and most of them SAVE bytes;
the systemic answer is a kind byte per LDT entry (R1).

## Strengths

- **The table swap costs no conventional byte and cannot drift.**
  `fsx_rdownf` (fsx.inc 2,349-2,404) banks the 1,259-byte table in the park's
  scratch above 1MB, writes it all `stc`, a `retf` before each kept run, and
  copies the kept runs back; the list `fsx_rkeep` is pairs of `rmk_*` labels
  in the table itself, so a run is exactly its cells. A refused cell is CF = 1
  wherever it is entered. The `.km` loop's `jnz` on `sub bx, 4`'s flags across
  four `mov`s is correct and saves the `or`.
- **Fallback, never refusal, one test a reason** (`fsx_rprepf`, 1,475-1,571):
  each `FSXR_*` is one test, `fsx_rfree` unwinds every claim on every refusal,
  and the reasons are tested in an order that takes nothing before the last
  one that can fail.
- **The service area is the right shape**: one conventional claim sized
  exactly code + rows + runs + pool rows + ring + font (`fsx_rsvcf`,
  1,826-1,935), every claim owned by the instance's slot so a teardown frees
  what an exit did not, the twins' offsets being `.mega`'s own with CS x 16 =
  base - `rms_lo`.
- **SOUND.DRV's rename rule** (`fsx_rsndup`, 2,518-2,581): a word untouched
  since the drop gets its selector back, a word the bracket wrote gets
  `mega_p2d`'s selector or 0 - so no real-mode segment outlives the bracket,
  and a stream opened in either mode plays in the other. The copy at the drop
  and the return (IF = 0), not at the arm, is the right answer to a card that
  is live across both switches.
- **`mega_ldt_free` is a good central point** (mega.inc 9,984-10,030): it
  nulls any `mega_wtab` row naming the freed selector and the caller's
  stacked ES, so a double free through a stale row and a `pop es` of a freed
  entry are both closed in one place.
- **`mega_iswin`'s rule is written down where it is applied** (14,650-14,700),
  with the incident that made it - which is what made gms-2 findable.
- **`mega_imghif`'s FFFFh case** (12,354-12,420) is handled where the loader's
  saturation makes it common, with the reason (WIREFLET, 14.67) beside it.
- **`os88la.inc`/`os88ub.inc` written once, assembled by each caller**, with
  `LA_ORDER` failing the build rather than the borrow.

## Findings

### gms-1 (bug, medium, CONFIRMED by reading) - the door's move guard compares the wrong owner

`fsx_rmvck` (fsx.inc 1,583-1,631) snapshots "the base of every MOVABLE claim
the instance DI owns" by `cmp [ss:si+MC_OWN], bx` (1,601) with BX = the
instance SLOT. But a claim a package makes itself is recorded under the
package's SEGMENT: `mem_own` "BX = owner word = the calling segment"
(memory.inc 5,286, 5,917-5,960; on kern_mega in protected mode the region's
data selector, `I_SPTR`). `mega_bown` (mega.inc 13,508-13,580) carries the
same lesson in its `.f` arm: "matched against the slot alone, a package's own
claim was nobody's and SEG_BWIN refused it on a 1MB machine (14.139's
follow-up)". So:

1. the claims `os88real_prep` banks - the package's own, the only ones a
   package can list - are never in the snapshot, and a door claim that
   compacts one of them leaves a stale real segment in the bank that
   `os88real_swap` installs inside the bracket: silent corruption in real
   mode, with no protection to catch it;
2. what the guard DOES see is the slot-owned REGION (declared movable by
   every package, §66.6.1.1). For a conventional region `[fsx_rseg]` is
   computed at 1,543, BEFORE `fsx_rsvcf`/`fsx_rdrvf`/`fsx_rextf` claim
   (1,544-1,549); a compaction that moves the region trips the guard and the
   bracket falls back with `FSXR_ROOM` - a fallback that recomputing
   `[fsx_rseg]` after the last claim would have avoided (the package is told
   to take its segment from CS, so nothing of its own banks the region).

Shipped callers are safe only because the Video Player pins its listed claims
(`vp_smov`), which SPEC.md 53.2.3 also requires; the guard paragraph that
follows the pinning rule describes a safety net that is not there.

**Recommendation (now):** in `fsx_rmvck` match `MC_OWN` against
`[inst_tab + slot*32 + I_SPTR]` as well as the slot (mega_bown's `.f` shape,
BP is free under the `pusha`), and for a conventional region convert
`[fsx_rseg]` after `fsx_rextf`, not before `fsx_rsvcf`. About +10 bytes of
`.mega`, 0 conventional. Then SPEC.md 53.2.3's guard paragraph is true as
written.

### gms-2 (bug, medium, PLAUSIBLE reach) - `mega_rgnsubfree` frees unbound windows: 14.80 again, on the region path

`mega_rgnsubfree` (mega.inc 12,487-12,534) frees "every present LDT
descriptor in the allocator's range whose base lies inside the block" except
the region's pair, with its own 24-bit base compare (12,513-12,523) and no
`mega_iswin` test. Every other block-free walker has one: `mega_cdin`
(14,604-14,640, used by `mega_cdfree` and `mega_mpmovef`) and `mx_dfree`
(9,414-9,478, `call mega_iswin / jnc .din`). `mega_iswin`'s header is the rule:
"A WINDOW IS ITS OWNER'S AND NOBODY ELSE'S ... the C64's ROM-bank windows are
based BELOW its ROM part, in whatever claim happens to sit there ... Freeing it
there handed the C64 a dead selector and jammed the 6510 at PC 0008 the moment
another package's claim was freed (KERN-MEGA-PLAN 14.80)". A REGION above 1MB
is a pool block too (`mega_pkghif`, owner 80h | slot), so when a package
placed above 1MB exits (`ld_unreserve`, loader.inc 254; memory.inc 4,875),
any other instance's unbound `SEG_WIN`/`SEG_WINL` window whose base happens to
lie inside that region is freed; `mega_ldt_free` clears its `mega_wtab` row,
and the holder's next load of it is `mega_gpfix`'s `.stale` NULL - the 14.80
symptom. A `mega_ustab` row cannot be hit here (its base is below 1MB).

**Recommendation (now):** make the walk call `mega_cdin` per present entry
(DL:AX = the block's base, CX = KB x 64 paragraphs - a region block is far
under 1MB of paragraphs), keeping the region-pair exemption. That deletes the
private 24-bit compare: about -15 to -20 bytes of `.mega`.

### gms-3 (bug, medium, CONFIRMED by reading) - `mega_subsegf`'s private match answers a window or a handle descriptor (14.128 on `OSAPI_MEM_SUBSEG`)

`OSAPI_MEM_SUBSEG`'s body has its own "one over that base already" scan
(mega.inc 12,610-12,624): access byte, base and limit, and nothing else.
`OSAPI_SEG`'s SUB arm asks `mega_dfind` (13,975-14,012) instead, which also
refuses a `MEGA_HAVL` handle descriptor (13,994) and any `mega_wtab` window
(13,999): "a window is its owner's to re-aim, and one left over this very base
answered as a SUB moved that SUB at its next aim (KERN-MEGA-PLAN 14.128)". The
older cell never got the fix. A window or a WIN16 handle descriptor (data,
92h) over exactly the paragraph and limit a package asks `OSAPI_MEM_SUBSEG`
for is handed out as its SUB; the window's owner re-aiming it, or freeing it,
moves or kills the SUB under the package.

**Recommendation (now):** replace the scan with `call mega_dfind` - the inputs
are already DX:AX, CX, BL - about -25 bytes of `.mega`. Better, R2 below.

### gms-4 (bug, medium, PLAUSIBLE timing) - the SUB family reads a base and writes a descriptor with no `sch_lock`

`mega_subsegf` raises `sch_lock` across the whole call (12,559) with the
reason: "no task compacts the pool between reading the block's base and aiming
a descriptor at it (mx_alloc's rule)". `mega_segf`'s SUB/SUBC arm does not:
`mega_selbase` at 12,915, `mega_bclip`, `mega_dfind`, then `mega_ldt_alloc`
writes the descriptor with the base computed before (12,934-12,945), at IF = 1
and unlocked; `osapi_seg` and `api_rxc` (kernel.asm 4,611) take no lock either.
`mega_segtile` (13,662-13,676) reads and clips before its `cli`; `mega_segh`
and `mega_segbw` the same. A pre-emption in that window by a task whose
`mx_alloc` compacts, or whose conventional claim compacts the heap, moves the
block and rebases every descriptor in it (`mx_desc`, `mega_mpmovef`) - except
the one this call is about to create, which is then written at the block's
OLD place and never rebased: a selector over somebody else's memory, silent.

**Recommendation (now):** `inc byte [sch_lock]` / `dec` around the
read-to-write window in `mega_segf.sub1`'s path (shared by SUB and SUBC),
`mega_segtile`, `mega_segh` and `mega_segbw` - not `SEG_AIM`, which is legal
in a rate hook and writes under its own `cli`. About +24 bytes of `.mega`.

### gms-5 (weakness, low) - `SEG_TILE` checks the first and the last tile, not the run

`mega_segtile` (13,638-13,735) proves only that the first tile's base and the
last tile's base each lie in SOME block (two `mega_bclip`s, 13,662-13,670).
A run whose last tile lands in a neighbouring block passes, and a middle tile
in a gap between blocks gets limit FFFFh, because `.put`'s `mega_bclip`
(13,693) leaves CX unclipped when the base is in no block. The header promises
"the last cut at the block's end" and "3 a tile in no block". WIN16's callers
size CX from their own block, so no shipped path is known to hit it.

**Recommendation (now):** `mega_blkof` once on the first base; refuse
(AX = 3) when (n-1) x 64KB reaches the block's end; clip only the last tile
from that end; drop the per-tile `mega_bclip` (one table walk a tile, under
`cli`). About -10 bytes of `.mega`, and n-1 fewer `mem_tab`/`mx_tab` walks
with interrupts off.

### gms-6 (hazard, low) - `mega_mpmovef`'s move is a 16-bit signed paragraph count

`mega_mpmovef` (14,240-14,275) takes the move as `sub bx, ax` (14,250) and
sign-extends it with `sar bp, 12` (14,252). A conventional move of 32,768
paragraphs (512KB) or more reads with the wrong sign, and every descriptor in
the block is rebased the wrong way. kern_mega's heap is about 35,800
paragraphs on a 640KB machine (MEGA_RESIDENT 68KB), so a near-empty heap
compacting one claim from the top to the floor can reach it. Rare, and
silent when it happens.

**Recommendation (now):** take the 17th bit from the `sub`'s borrow (`sbb bp,
bp` before forming the high part) - about +4 bytes of `.mega`.

### gms-7 (weakness, medium) - a fault inside a real-mode bracket is a silent hang, not a reported stop

SPEC.md 53.2.3 rules that a fault inside "is a real-mode fault: no `kern_mega
FAULT` line, and the machine stops as kern_big's would". On a 286 or later a
divide error and an invalid opcode are FAULTS (the saved IP is the faulting
instruction), so the BIOS's own int 0/int 6 handlers' `iret` re-executes the
instruction for ever: a hang that shows nothing, on the machines (286/386) whose
field reports most need a CS:IP. Nothing in the drop (`fsx_rdownf`) touches
IVT 00h-07h.

**Recommendation (now):** a small reporter in the service area (`rms_*`, so 0
conventional bytes at rest): the drop banks IVT 00h, 06h (and 0Dh's NMI-free
neighbours if wanted) and points them at it; it prints "real-mode bracket
fault vv at cccc:iiii" through int 10h teletype and halts; the return puts
the cells back. About +60 bytes of `.mega`, +0 resident. Not recovery - the
owner's ruling stands - just the same sentence `mega_fault` gives in protected
mode.

### gms-8 (size, low) - `mega_dklibfree` is resident for a path that does not need it

`mega_dklibfree` (18 bytes) is in `.text` (mega.inc 14,770-14,780, map 4B91h)
because `mod_free_row`'s CONVENTIONAL arm (mod.inc 932-937) calls it even in
real mode. On that arm the descriptor itself is already freed by the claim's
own free (`mem_free_x` -> `mega_mpfreef` -> `mega_cdfree`, whose `mega_cdin`
covers code descriptors); only `[mega_dklib]` needs zeroing.

**Recommendation (now):** that arm does `and word [mega_dklib], 0` (5 bytes,
as the far call it replaces) after the free, and the proc moves to `.mega`
for the above-1MB arm. -18 conventional (`MEGA_RESIDENT`), +18 `.mega`.

### gms-9 (size, low) - dead save and load in `mega_imghif`

`mega_imghif` (12,385-12,395) does `push bx / ... / mov bx, ax / call
mega_ldt_alloc2 / ... / pop bx`; `mega_ldt_alloc2` reads DX:AX and CX only.
-4 bytes of `.mega`.

### gms-10 (perf, low) - the walkers in this span are O(LDT) under `cli`

`mega_rgnsubfree`, `mega_cdfree`, `mega_mpmovef` (IF = 0 for the whole pass)
and both match scans run to `[mega_ldte]`, the whole LDT (512 entries and up,
mega.inc 103), on every free, move and SUB. megacore.md's high-water mark
(its (a) under the allocator scans) is the fix for all of them, not only the
two allocators and `mx_dfree` it names; PREDICTED, not measured: on a 6 MHz
286, ~512 x 12 + present x 60 clocks is about 2 ms of IF = 0 per moved
conventional claim, near an 8250's one-byte budget at 9,600 baud.

**Recommendation:** fold into megacore.md's high-water mark row; +0 here.

## Rethinks

### R1 (now, internal) - a KIND byte per LDT entry instead of geometry plus side tables

Ownership of a derived descriptor is inferred today: "based inside the block"
plus `mega_wtab` (32 rows x 4), `mega_ustab` (8 x 4), byte 6's `MEGA_HAVL`
bit, the access byte (F2h tiles and handles), and the code/data `+8` rule. A
386 leaves no room in the descriptor itself (byte 6 is limit 19-16, AVL, the
reserved bit, D and G; byte 7 is base 31-24), so the tag has to live beside
it: one byte per LDT entry in the tables' tail above 1MB, written by the
allocators - {claim pair, region pair, SUB, SUBC, tile, handle, bound window,
window, physical (`ustab`), lone code alias} in the low nibble and the
owner's slot where it has one. Then:

- every walker asks one byte: "is this kind the block's?" - gms-2 and gms-3
  could not have happened;
- `mega_iswin`, `mega_wslot` and `mega_ldt_free`'s `wtab` scan become
  indexed reads, and `mega_wtab` shrinks to nothing or to the owner byte;
- instance teardown frees windows by kind + owner, not by a row walk.

Cost: about 600 bytes of table in the tail (above 1MB, 0 conventional),
against about -100 to -200 bytes of `.mega` code (two scan procs, the walkers'
per-entry calls), ESTIMATED. No ABI.

### R2 (now, then flag day) - `OSAPI_MEM_SUBSEG` as an adapter over `OSAPI_SEG`

`mega_subsegf` (201 bytes) is a second implementation of `SEG_SUB`/`SEG_SUBC`
with the older match rule (gms-3) and, ironically, the better lock (gms-4).
NOW: make it a ~40-byte adapter that calls `mega_segf`'s SUB arm with DX = ES,
CX = 0 or the caller's (BL bit 1), the code bit mapped to `SEG_SUBC`, and AX
kept on a refusal (the old cell keeps every register) - about -150 bytes of
`.mega`, one match rule, one lock. AT THE NEXT FLAG DAY: retire the cell
(Clear Skies and Pixelstein move to `SEG_SUB`), and fold `SEG_WIN`,
`SEG_WINL` and `SEG_AT` into one window verb (base as a paragraph or a
linear, an optional window to re-aim) - DPMI 0.9 reached the same ground with
allocate / free / set base / set limit / create alias and nothing else.
Proposed as a new ABI-BREAK-LIST row (section L).

### R3 (now) - the bracket's "words real mode reads" as a registry

Every kind of kernel word the drop must make real has its own pair of
routines: `mega_vwalk`'s video and font words, `fsx_rexfill` for `EXFP`,
`fsx_rserdn/_rserup` for `SERFP` and the ring, `fsx_rsnddn/_rsndup` for the
driver's list, the font copy, `[vx_ep]`, `[k_seg0]`, `CPU_F_PM`. SPEC.md
53.2.3 records that `EXFP` was "the one kernel word the plan's census
missed" and the first build hung. A single table of {address, kind} (segment
word, far pointer's segment, a ring to copy) walked by one drop and one
return would make the next census miss a missing row rather than a missing
routine. ESTIMATED 0 to -100 bytes of `.mega`; worth doing only alongside the
next resource the bracket must carry.

## Prior art

- **OS/2 1.x** returns a selector from `DosAllocSeg` and keeps it across
  `DosReallocSeg` (the descriptor re-aimed in place - `SEG_HND`'s model);
  `DosAllocHuge` hands consecutive selectors at `DosGetHugeShift`'s step
  (`SEG_TILE`); a code alias is an explicit `DosCreateCSAlias` freed by
  `DosFreeSeg` (`SEG_SUBC`). The difference that matters here: the kernel
  records every selector's owner and kind in its own segment table, so a free
  never infers ownership from where a base lies.
- **DPMI 0.9** function 0002h ("segment to descriptor") returns the same
  descriptor for the same real segment every time and documents that it can
  never be freed - exactly `mega_ustab`'s rule, reached independently.
  Functions 0000h/0001h/0007h/0008h/000Ah are the whole of what a client
  needs to build windows and aliases (R2).
- **Windows 3.0 standard mode** runs a DOS program by freezing the protected
  world, giving the program conventional memory and real mode, and putting
  everything back on the switch - `FSXF_REAL`'s shape - and it does it by
  SWAPPING the low memory image out and in rather than renaming each kernel
  word, which is R3's argument in its strongest form. OS/2 1.x's DOS box
  likewise left protected mode through the CMOS shutdown-code reset on a 286,
  as `mega_r286` does.

## Doc/code mismatches

| file:line | current text | code does | proposed |
|---|---|---|---|
| kernel/fsx.inc:3146 | "these are ~1.6 KB that a machine needs only inside a real-mode bracket" | `rms_lo`..`rms_hi` is 930h = 2,352 bytes (map 52E0h-5C10h) | "these are 2,352 bytes (kern_mega map) that a machine needs only inside a real-mode bracket" |
| SPEC.md:107302 | "(2,384 bytes of code and data, 128 of rows, 6 a run: 3 KB for a contiguous file)" | 2,352 bytes | "(2,352 bytes of code and data, 128 of rows, 6 a run: 3 KB for a contiguous file)" |
| SPEC.md:107278 | "(`KERNEL_SEG:0x0010`, 1,241 bytes) is SWAPPED" | `osapi_table_end` is 04FBh: 1,259 bytes (kernel.asm 4,441) | "(`KERNEL_SEG:0x0010`, 1,259 bytes) is SWAPPED" |
| kernel/fsx.inc:2910 | "db: the classes asked (1 DISK, 2 EMS)" | `fsx_rlist` also sets 4 for `DRVC_SOUND` (1,790-1,794) | "db: the classes asked (1 DISK, 2 EMS, 4 SOUND)" |
| kernel/fsx.inc:1981 | "SOUND's at the drop and the return, fsx_rsndf" | no `fsx_rsndf` exists; the copies are `fsx_rsnddn` and `fsx_rsndup` | "SOUND's at the drop and the return, fsx_rsnddn and fsx_rsndup" |
| kernel/fsx.inc:2210 | "(not SOUND's: fsx_rsndf, at IF = 0)" | as above | "(not SOUND's: fsx_rsnddn / fsx_rsndup, at IF = 0)" |
| kernel/mega.inc:12243 | "(the region, image and bss, 1..65534)" | `mega_imghif` takes CX = FFFFh as a whole 64KB region, "not a rare case" (12,354-12,362) | "(the region, image and bss, 1..65534, or FFFFh for a whole 64KB region: mega_imghif)" |
| kernel/mega.inc:10003 | "a block's free takes a window based in it (mega_cdfree, mx_dfree)" | both exempt an UNBOUND window (`mega_cdin` and `mx_dfree` call `mega_iswin`); only a bound window goes with the block | "a block's free takes a BOUND window based in it (mega_cdfree, mx_dfree - an unbound one is its owner's, mega_iswin)" |
| kernel/fsx.inc:2868-2871 | "so nothing in protected mode writes the window's copy meanwhile - nor a low driver's image" | the protected-mode serial ISR writes the high ring of a lease the bracket package owns (`fsx_rserb` = 1), fsx_rserdn's own known limit | "so nothing in protected mode writes the window's copy meanwhile - but a SERIAL.DRV lease of the package's own (fsx_rserdn's known limit) - nor a low driver's image" |
| kernel/mega.inc:14604 | "; mega_cdin -ES:DI = a present descriptor" | (typo) | "; mega_cdin - ES:DI = a present descriptor" |

Not mismatches but worth a line once gms-1/-3/-5 land: SPEC.md 53.2.3's guard
paragraph, `mega_subsegf`'s "A descriptor already over that base ... is
answered", and `mega_segtile`'s "the last cut at the block's end" describe
what the fixed code will do.

## Open questions

1. Can the UI task be pre-empted between `fsx_rprepf` (claims taken,
   `[fsx_rmode]` = 1) and the freeze's arm in a way that tears the instance
   down? `[fsx_rmode]` is reset only in `fsx_rupf`, and a later bracket that
   does not ask `FSXF_REAL` never reaches the `.ask` path that would reset
   the state - so a stale 1 would send it down `fsx_rcall`'s real path with
   a freed service area. I found nothing that kills the calling instance in
   that window; not proved.
2. Does any shipped package rely on the door's move guard instead of pinning
   (gms-1's reach)? The Video Player pins (`vp_smov`); the rest were not
   surveyed.
3. `fsx_rvup` restores the video words from the bank blind; a mode set inside
   the bracket wrote real segments there. `fsx_restore` re-establishes the
   desktop afterwards - not traced to confirm it does not read those words
   between the two.
4. gms-2's reach in practice: can a C64 (or WIN16) unbound window's base fall
   inside ANOTHER package's region above 1MB? The C64 comment says "in
   whatever claim happens to sit there"; a pool neighbour that is a region is
   the case not yet covered.
