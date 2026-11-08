# Bug and hazard register - kernel review of 2026-11-08

This is the FINAL register of DEFECTS the review found: every finding of kind
`bug` or `hazard` that survived verification in the 25 area reviews and the 12
gap reviews, plus the findings filed under another kind (`weakness`,
`consistency`, `perf`) that describe something that is wrong rather than
something that could be better. The same defect often arrived from two or three
angles; each such group is merged into ONE item here, and every source finding
id is listed against it so the reports (`docs/reports/big-review-2026-11-08/areas/*.md`)
and the verified data (`.../data/*.json`) can be followed back.

It is a dated measurement of one tree (the branch `the-big-review` at the time of
the review), not a maintained document.

## What changed in this revision

The first register was written before the gap reviews and before the three
adversarial lenses (`synth/challenge-truth.json`, `challenge-cost.json`,
`challenge-fit.json`) read its top items. This revision applies every lens
amendment to BR-1..BR-8 and carries the consequences into the later items and
the batches; merges the 12 gap reviews (40 new items, BR-173..BR-212, and 14
more gap findings folded into existing items, each citing both sources); and
re-sums every batch per kernel. What moved the most:

- **BR-1**: kern_small's 360KB/720KB canary is INERT too, not only out-run by
  the splash; the new KSIG_OFF newly assembles an 11-byte KZIP ES adjustment on
  kern_big (.boot2 +19..+23, not +8); `crosses()` must model the KZ head split;
  the fix costs one more int 13h per 8088 boot on four of the six system disks.
  (The byte figure and the date are superseded by the follow-up - next
  section.)
- **BR-4**: the posted-close fix does not work as first written
  (app_close_win re-hides a dying record); two working routes, -6..+50 bytes.
- **BR-5**: the stall is a Book8088/CH375 machine's, not a PicoMEM's.
- **BR-6**: +60..+110 .boot2 on kern_big and kern_mega, and a 14th blob sector
  is certain on kern_big. (Superseded: the owner refused that sector, and
  BOOT3-PLAN's wave 1 pays BR-6 instead - next section.)
- **BR-7**: +32..+36 .cold, not ~+24. **BR-8**: a bound alone trades a dead
  machine for a write into another package's window; the door refusals join
  the fix (+29..+46).
- **BR-9** is re-priced from the gap review that scoped it (~+55 kern_big,
  ~+40 kern_small) and raised to high.
- **Two windows are full on kern_big** and every figure in them waits on room:
  the blob (4 bytes free) and the boot overlay's window half (1 byte free; the
  register asks it for +82..+99). See *Cross-cutting constraints*.
- **The owner's "packages are trusted" ruling** reads on about 107 core bytes
  (BR-8's doors, BR-22, BR-29, BR-30, BR-37, BR-173), which become the owner's
  call. (Answered: not taken - next section.)
- **New Batch I** (HDD.DRV, XMEM.DRV and DOS.O88's extended-memory face) holds
  the driver-side gap items. Core totals are now about **+508 resident on
  kern_big and +372 on kern_small** (were ~+420/~+300); all-items about
  **+945 / +586** (were ~+780/~+455).
- Two more findings were refuted and are dropped: gfce-4 and gms-7.

## After the owner's first answers (2026-11-08)

The owner answered the review the same day (`../owner-decisions.md`), and five
follow-ups re-read the items those answers touched (`../followup-canary.md`,
`../followup-purgeable.md`, `../followup-decisions-6-8.md`,
`../followup-already-exists.md`, `../areas/gap-bss-zeroing.md`), with
`docs/plans/BOOT3-PLAN.md` replacing the refused 14th blob sector. Every item
that moved is marked **(owner, 2026-11-08)** or **(follow-up, 2026-11-08)**
where it moved. In short:

- **Packages are trusted** (owner, decision 3) is now a recorded principle,
  not an open ruling. The six package-argument fences (BR-8's doors, BR-22,
  BR-29, BR-30, BR-37, BR-173) are **not taken** as retail code - an
  APICHK-style knob at most (phase1 WP19) - and their bytes leave every batch
  sum. BR-8's bound is not taken alone either (its own fix text says why).
  Three low rows the register already called "the same kind" (BR-104, BR-107,
  BR-109's doors) leave the sums too, as follow-up applications of the same
  principle; each stays open to the owner as a named exception. So does
  BR-85's code half (+3, the SS-independent `evq_push`): it defends against a
  package IRQ handler calling WAKE on a foreign SS, "and none was traced", and
  the SDK already states the precondition; its comment fix stays.
- **A diagnostic for a failure nobody has met goes behind a build knob, off in
  shipped kernels** (owner, decision 1). BR-9's INT 0/#UD panel (its stubs,
  install and the generalised panel) is a knob; kern_mega already closes the
  faulting package. The same weighing moves BR-80's tripwires, BR-82's ISR
  stack canary, BR-173's optional self-take panel and BR-68's `[mega_kfix]`
  counter behind knobs (follow-up), and their bytes leave the sums.
- **BR-1** is re-dated and re-priced (follow-up): the 360KB/720KB canary has
  been inert since **2026-09-05** (`175926d5`, KZIP the default), not since the
  12 -> 13 blob step; moving it costs **0 bytes on kern_big and kern_mega and
  saves 11 on kern_small**; cylinder runs are READS only, so the harm is a
  crossing read; and the Pravetz record is the int 15h freeze (#343), not a
  cylinder-read report.
- **BR-6** follows the owner's rule: an MDA alone prints a message and halts;
  with a CGA present the machine switches to it. Its blob bytes are paid from
  BOOT3-PLAN wave 1, so **no 14th blob sector** (owner, decision 2).
- A second round of answers the same day (`../owner-decisions.md`, "Second
  round") moves no register item: it takes the keyboard mouse, text-7, the
  mono figure and the API level (phase1's and systemic's), and asks for
  `.boot3` to be generic across the kernels, which only widens BOOT3-PLAN's
  room for BR-1 and BR-6 on kern_small and kern_mega, both of which already
  fit.
- The core totals become about **+341 resident on kern_big and +220 on
  kern_small** (were ~+508/~+372), all-items about **+744 / +400** (were
  ~+945/~+586); kern_mega conventional ~+179 core, ~+342 all. The side-by-side
  table at the end carries the per-batch figures.

## How to read it

**Inputs.** The 25 area reviews' 170 findings of kind bug or hazard (66 bug,
104 hazard) plus about 60 findings of other kinds that are defects; the 12 gap
reviews' 132 findings, of which 28 are bug or hazard (6 bug, 22 hazard), and about 30
of other kinds are carried as defects or as the fix of one. Four findings were refuted and are
DROPPED: isr-5 (WireBall's queue tail), wm-20 (wm_anim yields holding the gfx
lock), gfce-4 (OSAPI_FSX_SPK dividing by N) and gms-7 (a fault inside a
real-mode bracket). Verdicts are carried through: **confirmed** (the verifier
re-derived it), **corrected** (the verifier changed a detail, a byte count or
the fix; the corrected form is what is written here), **unchecked** (not
re-derived; lower confidence, said so where it occurs). A finding the verifier
marked PLAUSIBLE (argued from code, not run) says so in its mechanism.

**Lenses.** `truth`, `cost` and `fit` are the three challenge files above; each
gave BR-1..BR-8 a verdict of stands or amend (none was refuted). Where they
disagree on a figure, the item re-derives it from the code or the maps, or
states the range and why; it never picks one silently.

**Source ids.** Area ids are as in `data/*.json`. Gap ids are unique except in
two gap files that reuse short names: gap-mouse-cursor-half's findings are
written `cursor/mc-N` here (megacore's `mc-N` are a different set), and
gap-os88ui-kernel-copy's are written `os88ui/F-N`.

**Ranking.** Severity x likelihood ON REAL MACHINES x blast radius, each scored
H/M/L. Likelihood is about the field: "every machine, every day" is H; "a common
configuration or an ordinary action" is M; "a hostile file, a package breaking a
stated rule, or a narrow race" is L. Blast: H is data or memory corruption, a
dead machine or a lost session; M is a feature broken or wrong for the session;
L is cosmetic or a cost. Ties go to the SHIPPED kernels (kern_big, kern_small)
over kern_mega, which is budgeted but unreleased.

**Kernels.** `small` = kern_small, `big` = kern_big, `emu` = kern_emu,
`mega` = kern_mega. kern_emu and kern_mega both define KERN_BIG, so anything
marked `big` reaches them too unless the item says otherwise. "modules" means an
on-demand `.DRV` (named).

**Bytes** are signed: the reviewers' figures as corrected by the verifiers and
the lenses. They are ESTIMATED from instruction encodings unless marked
MEASURED, which here means read off `build/kernel.map`, `build/smallk/kernel.map`
or `build/megak/kernel.map` at this tree. `res` is RESIDENT, and `.cold` IS
resident (it is billed by KERN_BUDGET exactly as `.text`; only its CS differs).
`.boot2`, `.ovl` and the boot sector are transient - but both of the first two
are full on kern_big, so transient is not free there. Module bytes are named by
module and cost no resident byte; a driver image (HDD.DRV, XMEM.DRV) is a heap
claim, not kernel-resident. On kern_mega, `.cold` and `.mega` go above 1MB after
the switch and do not count against MEGA_RESIDENT's 68KB of conventional memory;
`.text`, `.bss`, `.lowbss` and `.vga` do. Where a kern_mega placement is quoted
it is inferred from the section the fix lands in on kern_big, not built. Times
are PREDICTED (arithmetic) unless marked MEASURED, with the measurement named.

**ABI.** `none`; `SDK text` (an `apps/os88api.inc` contract comment changes, so
`python3 tools/abilock.py --update "why"` WITHOUT a PKG_FMT bump - a contract
clarified); `additive` (a new cell or flag at the end, no bump); `flag day` (a
break, to be batched with docs/plans/ABI-BREAK-LIST.md). Almost everything here
is `none`: these are defects, and the fixes are inside the kernel.

**Rows** are `tests/suite.py` rows (tier in brackets) that cover the AREA - the
rows an implementation session names and runs once in its final pass. "no row"
means nothing in the suite reaches the defect; most items say what row would.
A row that covers the area has not, by being green today, shown the defect is
absent: almost none of these defects is exercised by any row, which is why they
survived.

## The fifteen that matter most

| rank | id | defect | kernels | fix bytes |
|---|---|---|---|---|
| 1 | BR-1 | The boot canary protects one of the six shipped system disks | big, small | 0 res; the canary move 0 big/mega, -11 small; the head bound .boot2 +8..+12 (big after boot-7 or BOOT3 wave 1) (follow-up, 2026-11-08) |
| 2 | BR-2 | kern_small: a shed during a mount writes the listing into the FAT window | small | +9..+10 .cold |
| 3 | BR-3 | gfx_blit4 trusts the sign of its width: 64KB of heap overwritten | all | 0 (+2 .cold optional) |
| 4 | BR-4 | A dying worker repaints and drops DESK.DRV on its own 128-byte slice | all | -6..+50 res, by route |
| 5 | BR-9 | No divide-error or invalid-opcode handler: one INT 0 is a dead machine | big, small | **knob only (owner, 2026-11-08)**: 0 shipped; ~+55 big / ~+40 small and +12..+20 .ovl in the knob build |
| 6 | BR-6 | A plain MDA is driven with Hercules graphics CRTC timings | big, small, mega | +60..+110 .boot2 big/mega, paid from BOOT3-PLAN wave 1 (no 14th sector, owner, 2026-11-08), +25..+65 small |
| 7 | BR-7 | The loader trusts a package header against its own region | all | +32..+36 .cold (0 conventional on mega) |
| 8 | BR-8 | wm_ptr2idx divides an unvalidated window pointer (INT 0) | all | **not taken: packages are trusted (owner, 2026-11-08)**; 0 retail (was +29..+46 with the door refusals); the doors in an APICHK knob |
| 9 | BR-10 | Esc on the Resume face deletes the hibernated session | big | +0..+4 HIBER.DRV |
| 10 | BR-11 | The purge floor is lifted by any teardown: a paste stops part-way and reports success | big, small | +8..+18 .cold |
| 11 | BR-12 | mem_regrow copies a whole canvas with interrupts off (~170 ms per 64KB, PREDICTED) | big, small | +10..+16 .cold |
| 12 | BR-180 | DOS.O88 does not own int 15h: a correct pre-XMS DOS program can write the pool and the HMA, or shut A20 | big (286+) | +45..+60 DOS.O88, 0 kernel |
| 13 | BR-13 | kern_mega: a reflection on the lent stack reads .lowbss through SS (#SS, halt) | mega | +16..+40 above 1MB |
| 14 | BR-14 | kern_small's fsx_page turns Hercules page 1 on beside a CGA | small (big guard reshaped) | +13 small, +2 big |
| 15 | BR-15 | The clock gains about 31 seconds a day | all | 0 |

Close behind, and the first to take if a batch has room: BR-5 (every driver
unload waits on USBMOUSE's worker - now known to be a Book8088/CH375 machine's
problem, which is what moved it out of the fifteen), BR-181 (XMEM.DRV never
re-proves A20 on a 386), BR-173 (a worker dying under the gfx lock deadlocks
the desktop; not taken as retail code, packages are trusted - owner,
2026-11-08), BR-182 (HDV_ARM redirects the kernel's own FAT reads), BR-16
(media-change status discarded), BR-17 (FAT16 write leaks every free cluster on
Disk full), BR-18 (16550 FIFO left on silences the mouse), BR-19 (a tick parks
the AT ROM's int 09h with IRQ1 in service).

---

## Register - high and medium

### BR-1 The boot canary protects one of the six shipped system disks

- **Sources:** boot-1 (bug, high, confirmed), boot-2 (hazard, high, corrected), boot-10 (rethink), boot-7 (size; supplies the room). Lenses: truth amend, cost amend, fit amend.
- **Kernels:** the canary is INERT (its sector's run crosses no head) on kern_big 360KB/720KB and on kern_small 360KB/720KB, and the splash's first tick runs .text before it on kern_small 360KB/720KB and kern_big 1.2MB. Only kern_big 1.44MB is protected today. (The fit lens corrected kern_small: the area report's "both kern_small disks are still correct" is false.) **When it broke (follow-up, 2026-11-08):** on the shipped KZIP kernels the 360KB/720KB canary (both kernels) has been inert since **2026-09-05** (`175926d5`, KZIP the default; main `2ce1e37b` #172), because the KZ head is read in runs of its own and the canary's run never starts before a flip on a 9-sector disk at any blob length - not since the 12 -> 13 blob step, which broke only the unshipped `NOKZIP=1` arm and on the shipped kernel RE-armed 1.2MB (inert since KZIP) and 1.44MB (inert for the one week of the 12-sector blob, from `0efc7158`). SPEC 2.9.13.3's "the canary is unmoved, and its reasoning is untouched", written in the KZIP commit, is the error (`../followup-canary.md` section 3).
- **Rank:** S high, L medium (an EOT-8 clone BIOS, or a 286 with Cylinder ticked), B high (every crossing multi-sector READ of the session returns the other head's sectors on that BIOS, and a FAT or directory sector read wrong is then rewritten; cylinder runs are reads only, SPEC 18.91.3 - corrected from "write" (follow-up, 2026-11-08)). **The Pravetz record (follow-up, 2026-11-08):** no record says a Pravetz cannot do cylinder reads. The tree's one Pravetz record is #343 (`974c976d`, 2026-11-06), the int 15h AH=C0h freeze that left IF clear; `pravetzboot` boots the 1.44MB image and reads neither `boot_cylrun` nor `b2_cylok`. "Boots fine but longer" is what either WORKING rescue looks like (the error path's retry, or a live canary on 1.44MB), so it is no evidence that anything broke lately; a lying ROM on a 360KB/720KB disk would not boot fine today. Which disk the reporter booted is not recorded: the follow-up's FIELD-NOTES entry and an extended `pravetzboot` run (its section 5) settle it. Separately, `rdiag360.img`, FIELD-NOTES 31's survey instrument, no longer tests cylinder runs (stage 1's flat arm is track-bounded now), and needs its own line here.
- **Mechanism:** KSIG_OFF 4096 is file sector 21 = LBA 35 on kern_big's 9/2 disks. KZIP is the shipped default (Makefile:1394-1395); kern_big's 13-sector blob puts .text at LBA 27 = head 1 sector 1, so the 9-sector plain head (boot2.asm:341-344) and the run 27..35 lie on head 1 alone (truth lens, from the BPBs of the built images: KERNEL.SYS at LBA 14 on os8088-360/720, 29 on -120, 33 on os8088.img, 12 on small360/720). kern_small's canary is memory 5120 = kernel sector 10, past KZ_HEADSEC 9 (`build/smallk/kernel.kz.json` headsecs 9), so it sits in the tail, which starts at LBA 32 (head 1, sector 6); the tail's first run is 32..35 on head 1 only and the canary is LBA 33 (fit lens). `t_canary`'s `crosses()` walks from the data area (tests/unit/t_canary.py:99-117) and so passes all of it. Separately, the splash's first tick far-calls vid_setmode (.text 0x7F5) and gfx_rowbase (0x8C4) (build/kernel.map:3335, 3347, MEASURED) as soon as the head lands, BEFORE the canary at the end of the load (boot2.asm:350-377, 419-473): on kern_big 1.2MB .text crosses a head at .text sector 3, on small360/720 at sector 4. A missed canary publishes dsk_cylrun = 1 (disk.inc:5343-5346).
- **Fix:** (1) KSIG_OFF := 22528 (file sector 57). Truth and fit modelled the KZIP head split, the tail restart and the DMA-page bound on all six shipped system images with blob lengths 13, 14 and 15: LBAs 71/71/86/90/69/69 each sit past a head flip, no 64KB split comes first, and the sector sums differ from both neighbours in kernel.sys, smallk and megak. The Makefile rewrites KSIG_OFF per build arm (Makefile:2242 `-512`, 2252 `+1024`), so every arm moves with it. **Re-priced (follow-up, 2026-11-08): drop the segment arm.** The `%if KSIG_OFF >= KZ_HEADSEC * 512` ES bump only undoes the tail's lift, so under KZIP the sum is `mov si, KSIG_OFF + KZ_RPARA*16` (the same 3-byte `mov si, imm16`) inside the 64KB the handoff's ES already names - 40960 on kern_big, 33792 on kern_small, 46080 on kern_mega - with an assemble-time `%error` if that sum plus 512 passes 65536. That costs **0 bytes on kern_big and kern_mega and saves 11 on kern_small**, whose 5120 assembles the arm today; the first pass's `KZ_RPARA=0` changes the immediate's value, not its length. (2) The splash: under KZIP the place is the KZ head loop (boot2.asm:336-345), not the SPL_RESIDENT tick gate - read the head at the TRACK bound and widen [b2_runmax] to the cylinder only after it. Moving the widening block (298-316) naively to after `.head_next` breaks .rerun's one-shot guarantee (524-533): .rerun would widen again and the canary would loop for ever on exactly the BIOS it exists for. The safe form needs no guard: `mov ax,[b2_spt] / xchg ax,[b2_runmax] / push ax` before the head loop and `pop word [b2_runmax]` after it (.rerun's SP reset at 533 makes the push harmless) (cost lens). (3) `crosses()` must split the walk as the loader does - KZ_HEADSEC from kernel.kz.json in its own runs (track-bound once (2) lands), then the tail restarting at .text+KZ_HEADSEC, with the DMA-page bound; starting at data+BOOT2_SECS (the earlier text) still passes small360's inert sector. Break it on purpose: KSIG_OFF back to 4096 must turn os8088-360.img AND small360.img red - and, per the follow-up's model, os8088-720.img and small720.img too. **Urgency (follow-up, 2026-11-08): land (1) and (3) now** - they need no room, cost 0 bytes on the shipped kernel and re-arm the canary on the four disks an 8088 actually boots; (2) follows the room (boot-7, or BOOT3-PLAN wave 1). Record fixes that go with it: SPEC 2.9.13.3's "the canary is unmoved" becomes the run-start rule, and rdiag either gets the cylinder bound in stage 1's flat arm behind a define or is retired, with `tests/rdiag.asm:6-8` and `docs/MARTYPC-DEBUG.md:1302` fixed in the same change. (4) Later, boot-10: compute KSIG_OFF per build arm with an implementation `t_canary` does not import - a gate that runs the generator's own model agrees with it by construction, which is how this defect passed (fit lens, phase1 WP6) - or let blobruns or a booted geometry row be the independent reader. Sequencing: boot-4 (SPL_RESIDENT 9 -> 5, phase1 WP6) changes the head window; land its first-tick audit before (2) or re-check (2) after. If batch A takes kern_big's blob to 14 sectors, sector 57 still crosses at 14 and 15, but KSIG_OFF's memory literal drops by 512.
- **Bytes (follow-up, 2026-11-08):** 0 res. .boot2: the canary move (1) is **0 on kern_big and kern_mega and -11 on kern_small** (above; the first figure, +11 for newly assembling the KZIP ES adjustment, is withdrawn), so what remains is the head bound (2): **+12** in the safe form (+8 with a rerun guard) - kern_big and kern_mega **+8..+12**, kern_small **-3..+1** net. kern_mega fits its 285 free bytes and kern_small its 702. kern_big's blob has 4 bytes free (0x19FC of 0x1A00, build/kernel.map:3127, MEASURED), so there (2) needs boot-7's -41 (phase1 WP6, which owns that saving) or BOOT3-PLAN wave 1 (about 508 free, docs/plans/BOOT3-PLAN.md section 11) first; (1) and (3) need no room. Boot time, PREDICTED: the track-bounded head costs one more int 13h call (one revolution or two, ~170-400 ms) per 8088 boot on FOUR of the six system disks - small360 and small720 (head LBA 23..31) and kern_big 1.2MB and 1.44MB (head LBA 42..50 and 46..54) - and nothing on kern_big 360/720 or on a 286+ that is already read track-bound (boot2.asm:311). **ABI:** none. **Confidence:** confirmed / corrected (all three lenses amend).
- **Rows:** canary (fast) - must go red on the old value for the four 9-sector images; blobruns, splashbar, splashspin, smallboot, kzboot, pravetzboot (extended to read `boot_cylrun`/`b2_cylok` and catch `.rerun`, follow-up) (soak); bootsmoke, small128 (full). No row models a BIOS that flips heads at the wrong sector.

### BR-2 kern_small: a shed during a Disk-window mount hands the listing's destination to the read-ahead or FAT window

- **Sources:** files-1 (bug, high, confirmed). Lenses: truth, cost and fit all stand. Related: ctx-6 (BR-188, any task's claim may shed on kern_small).
- **Kernels:** kern_small.
- **Rank:** S high, L medium (the 128KB machine is under memory pressure by construction), B high (FAT-window bytes overwritten in RAM are written back to the disk on the next write).
- **Mechanism:** DSK_BIGDIR is not defined under KERN_SMALL (kernel/dskwin.inc:36, 248), so fmv_load aims dsk_dest at the window's purgeable FS_VSEG claim through dsk_dest_b0 and mounts (files.inc:1693-1715; disk.inc:8605-8610). At mount step 2, before step 3 writes the listing, dsk_rah_want claims MEM_P_DIRW at HIGH and dsk_fatw_want MEM_P_FATW at MED (disk.inc:4262, 4288, 5997-6005), both above MEM_P_VIEW at LOW (memory.inc:373, 394, 439); mem_shed_one takes the cheapest record below the claimant (memory.inc:1420-1445) and mem_pg_forget's KERN_SMALL-only .view arm calls fmv_demote, which zeroes FS_VSEG and [fm_vseg] but not [dsk_dseg] (files.inc:1410-1437). The retrying claim can receive those paragraphs and step 3 writes entries through `mov es,[dsk_dseg]` (disk.inc:4440). MEM_RR_ONE dsk_dseg (memory.inc:3391) covers a MOVE only, and kern_small has no compactor, so mem_shed_one is the one door. SPEC 22.6.3's "the one claimer in the whole mount is asc_use" is false (SPEC.md:53470); ico_need/asc_use have the same exposure.
- **Fix:** in mem_shed_one (.cold on kern_small, smallk kernel.map:4903), under %ifdef KERN_SMALL, skip a record whose MC_SEG equals [dsk_dseg], beside the mem_fatw_dirty skip: `mov bx,[dsk_dseg] / cmp bx,[ss:si] / je .scan` (DL carries the level, so not through AX; BX is free inside the scan). [dsk_dseg] is 0 at rest (disk.inc:6268) and mem_next skips free records, so the skip acts only inside an aimed mount and cannot pin a VIEW cache across the session (fit lens), and the shed door also covers the harvest-time claimers without per-site fixes. Carry SPEC 22.6.3's correction in the same change.
- **Bytes:** +9..+10 .cold res kern_small (9 by encoding; ~40 clocks per cache record while a claim is shedding); 0 kern_big/emu/mega (MEM_P_VIEW exists only on kern_small). **ABI:** none. **Confidence:** confirmed.
- **Rows:** fcpsmall, fdlgsmall, fdlgchsmall (soak); small128 (full). No row: add one that fills a 128KB heap until VIEW caches are the cheapest record, navigates a B: window and compares the FAT window with the disk; it must go red without the skip.

### BR-3 gfx_blit4 trusts the sign of its width and height

- **Sources:** gfx-1 (bug, high, corrected). Lenses: truth, cost and fit all stand.
- **Kernels:** all (planar half big/emu/mega; 1bpp half both; a #GP on mega).
- **Rank:** S high, L medium (an empty rect's x2-x1+1 from a package; PaccMan hit exactly this in gfx_blitp), B high (VGA: 64KB of VGABUF_SEG from vga_pbuf, the bottom of the heap above the kernel; 1bpp: ~8KB of framebuffer per row, into the CGA aperture on a dual-card Hercules).
- **Mechanism:** gfx_blit4 refuses only zero, `or cx,cx / jz vga_b4out / or dx,dx / jz vga_b4out` (vga12.inc:1896-1899, short jumps back to the stub at 1714). Both decoders prove the block on screen with an unsigned add that wraps (the planar gate at 2056-2066: `add dx,cx / cmp dx,[vid_cw] / ja` - x=20, w=0xFFF6 wraps to 10 and passes; the 1bpp gate at 2016-2020 likewise), and `cmp cx,16 / jb` passes any negative. In vga_blit_prow the group count comes out 0, so `dec bp / jnz .g` runs 65,536 groups from vga_pbuf (DS = VGABUF_SEG, or mega_vbsel). gfx_blitp already carries the jle fix (2866-2870).
- **Fix:** `jz vga_b4out` -> `jle vga_b4out` at both sites: the same 2-byte short Jcc, the same reach and timing (16/4 clocks). After `or`, OF = 0, so jle means ZF or SF: zero and negative are refused and any width up to 32767 passes, which covers every screen; the SDK gives widths no sign meaning (os88api.inc:1964-1967), so this is no ABI event. The DX guard covers a negative height too. Optionally gfx_blit1_x's `jcxz .refuse` (vga12.inc:5915, .cold) becomes `or cx,cx / jle .refuse`.
- **Bytes:** 0 .text on every kernel; +2 .cold if gfx_blit1 is taken too. **ABI:** none. **Confidence:** corrected (group count, not size of the damage).
- **Rows:** dispblit, blitcut, blitpair, blitplane, atblit, atblitvga (soak). None passes a negative width; the row should, and must go red on `jz`.

### BR-4 A dying worker tears down windows, and drops DESK.DRV, on its own stack slice

- **Sources:** histsys-1 (hazard, high, confirmed), hm-3 (hazard, medium, confirmed), mem-10 (hazard, low, confirmed), ctx-1 (rethink, high, confirmed: the same fix at kernel scope). Lenses: truth amend, cost amend, fit amend; systemic T1. ctx-2's lock release is BR-173 and lands in the same diff (BR-173 is not taken as retail code: packages are trusted, owner, 2026-11-08).
- **Kernels:** all (the DESK.DRV half is kern_big; kern_mega's mega_fkill .worker also jumps into inst_task_die, on the stack that just faulted, mega.inc:2705-2721).
- **Rank:** S high, L medium (closing Bounce, or any 192-class package with a worker, over another window or the dock), B high (STACK OVERFLOW panel at close; on kern_big the UI task resumes in a freed claim; on kern_mega a #GP through a freed selector).
- **Mechanism:** (as the truth lens corrected it) app_close_win sets I_STATE 1 -> 2 and, for a record with a worker (I_TASK != 0xFF), only hides it (instance.inc:1662-1667, 1693-1694). The worker's next OSAPI_TASK_ALIVE takes .die into inst_task_die on its own slice (2212-2220, 1929-1947): gfx_lock, inst_unwin -> wm_destroy, desk_drop_x, gfx_unlock, mem_free_rec, task_exit. The window is already hidden, so wm_destroy banks an EMPTY rect (wm.inc:3054-3060) and wm_paint_dmg takes .chrome: wm_dock_under and menu_draw_bar's font_run chain. Another package's W_PAINT is reached only through wm_dock_under -> wm_dmg_wins for windows over the dock (wm.inc:5526-5555), or through wm_destroy_seg on a still-visible unbound window. Those chains under kent_bp frames exceed Bounce's 128-byte class (apps/bounce/bounce.asm:32) less its 64-byte interrupt floor; `t_stkclass` checks only the worker's own depth (tests/unit/t_stkclass.py:31-36). PLAUSIBLE, unmeasured. desk_drop_x -> mod_drop on the worker can free DESK.DRV while the UI task is in its lock-free DKD_HOVER far call (ui.inc:1006-1012); mod.inc:880-886 admits the race and its cli prevents only a torn slot. ctx-1 adds three more consequences of the same placement: the worker's claim cannot compact so raise caches decline a copy (memory.inc:974-978), mem_free_rec_x stores the global [mem_pg_floor] from the worker (memory.inc:4847-4848; BR-11), and BR-28's click race.
- **Fix:** post the teardown to the UI task - but NOT as first written. A posted wm_clreq reaches .cl_slot -> app_close_win, whose `cmp byte [di+I_STATE],1 / jne .hide` (instance.inc:1669-1670) re-hides a record already at state 2: no teardown runs, and the record, its region and its dock tile leak (truth and fit). What any working form needs: (a) a teardown entry that accepts I_STATE 2 with I_TASK = 0xFF, skips wm_ask_close and the state test, and enters app_close_win's tail at .noflush (inst_rel_rec, inst_unwin, mem_free_rec, desk_drop_x, I_STATE = 0) under gfx_lock; (b) the worker task_exits with BX = 0 and no release byte, as the .orphan precedent does (2234-2235) - today inst_task_die passes BX = DI, the I_STATE byte (1946-1947), which would free the record early; (c) I_TASK = 0xFF, the post and task_exit share one IF=0 window, so the UI task never frees a record a live task then releases; (d) a reap for a windowless worker record, since wm_clreq is per window slot; (e) kern_mega's fault-kill entry; (f) SPEC first: SPEC 20.6's "the ONLY code that can free its instance record is the worker's own task_exit" and SPEC 11.103.10's "a Sticky App on its worker arm closes HERE, on that worker" (SPEC.md:22809; instance.inc:1945-1947) are rewritten before the code. Two routes meet (a)-(e): **route (i)** keeps the wm_clreq bit (the systemic fit lens's preference - no new drain loop) and adds a state-2 arm to app_close_win plus a reap byte for windowless records; **route (ii)** is ctx-1's own drain, a [inst_reap] word drained in ui_task step 3 on the inst_launch_post precedent (the BR-4 fit lens's preference). inst_rel_rec and toast_owner_gone may stay on the worker (shallow; fit). mem-10's second half - the "frame standing in this image" fact the module pin needs - is still wanted by phase1 WP2; a one-line assert that mod_drop runs on task 0 replaces its deleted arm (systemic fit T1). Add a stkwater reading of Bounce closed over a second window.
- **Bytes:** the lenses disagree and nobody built it, so this is a range with its reasons. The cost lens found the windowed path SAVES: inst_task_die loses 22 bytes (gfx_lock 3, mov bx 3, inst_unwin 3, far desk_drop_x 5, gfx_unlock 3, mem_free_rec 3, mov bx,di 2) against ~+12 added, -15 for the windowed path and about +15 with the windowless reap (+1 .bss, ~+27) - but it priced the wm_clreq route without the state-2 arm truth and fit show is missing (~+9 more by encoding: a jb/ja split of the state test, an I_TASK compare, a jump to .noflush). **Route (i): about -6..+25.** **Route (ii): +16..+50** - ctx-1's verifier +16..+26 (inst_task_die ~-8, drain +22..+32, +2 .bss), the truth lens +30..+50, and re-derived here at ~+37 kern_big (worker -22 +20, drain ~+39) and ~+42 kern_small: the kern_small worker never had the 5-byte desk_drop_x to lose, so it saves 5 bytes LESS (ctx-1's "about 5 less on kern_small" has the sign backwards). Quoted: **-6..+50 res, .text/.cold, +1..+2 .bss**, ESTIMATED; batch C carries +22 kern_big and +27 kern_small. Savings come out of .text and relieve KERN_CODE_MAX as well as KERN_BUDGET. **ABI:** none. **Confidence:** confirmed (the overflow plausible, not measured).
- **Rows:** stkclass, stkapps (fast); stk0water, stkpanel, timerrace, deskbench (soak). New (ul-10): stkdie (soak) - Bounce over a second visible window, close Bounce, read the slot with tools/stkwater.water(), breakpoint sch_stkdie; break it by reverting the post.

### BR-5 One global drv_wcnt makes every driver's unload wait on every driver's workers

- **Sources:** drivers-1 (bug, high, confirmed). Lenses: truth amend, cost amend, fit amend. This is NO-PINNING-PLAN K1b.
- **Kernels:** big, emu, mega (OS88_DRIVERS); 0 kern_small.
- **Rank:** S high, L LOW-medium (corrected: the stall needs a driver worker, and USBMOUSE.DRV spawns one only on the CH375 arm - the Book8088's socket, ports 260h/261h; "a PicoMEM's mouse arrives by interrupt: no chip to poll, so no worker", drivers/usbmouse/usbmouse.asm:139-142), B medium (5 s stall per untick; a detached SOUND.DRV stays published and snd_tick far-calls its DSV_TICK; a second DETACH restores stale vectors; 5 s per earlier row at reboot).
- **Mechanism:** drv_wcnt is one byte (driver.inc:6132), moved at spawn, refused spawn and exit (4592, 4609, 4617). drv_unload_x (.cold, build/kernel.map:9601) calls DETACH, waits 91 ticks on the global count and on timeout `jmp short .out` skips drv_release, leaving the row loaded and its class published (4157-4189). ctrl.inc:7140-7146 clears WANT/ERR whatever happened; drv_shutdown_x unloads rows in order (4199-4212); memory.inc:2143 pins every driver image on any non-zero count. K1b deferred the per-row count "until a second driver spawns", which one now does.
- **Fix:** land it AS K1b (docs/plans/NO-PINNING-PLAN.md:177, 260, whose row already says "Deferred; its condition is now met" and cites drivers-1) and update that row in the same commit. A per-row worker table in .bss - it cannot ride in drv_tab, whose DRVR_SIZE is a full 16 bytes (driver.inc:249-270) - indexed at spawn and exit through drv_row_ix_of / drv_owns_seg_x (2014-2020); unload and the .drvimg pin test the row's byte. On timeout also clear DRVR_ENT or mark the row detached, not only unpublish the class, or the next untick calls DRVV_DETACH a second time (4157-4160) and the stale-vector restore remains (truth). With per-row counts only a stuck driver reaches the timeout, so its unpublish is a secondary part of the fix, inside the existing "leak, never a free under a live CS" rule (4178-4181) (fit).
- **Bytes:** +20..+35 .cold res (the cost lens's site-by-site count: three index sites +3..+5 each, the unload's compares +3..+5, the pin's image-to-row map +5, the timeout unpublish +5..+10; truth +25..+30); .bss +DRV_MAX: +11 kern_big and kern_mega (DRV_MAX 0Bh in both maps, MEASURED), +12 kern_emu (DRV_MAX equ 10 + DRV_VX + DRV_PX, driver.inc:63), each one less if the table replaces drv_wcnt rather than sitting beside it for K1a's pin. The plan prices K1b at ~45 + DRV_MAX; reconcile it there. On kern_mega the .cold is above 1MB and the .bss conventional. **ABI:** none (neither DRVV_DETACH nor the worker-spawn contract moves). **Confidence:** confirmed.
- **Rows:** usbmouse, heapdrv (soak); drvmem (fast). No row unticks a second driver while USBMOUSE's worker is up.

### BR-6 A plain MDA primary is programmed with Hercules graphics CRTC timings

- **Sources:** video-2 (hazard, high, confirmed). Lenses: truth stands, cost amend, fit amend.
- **Kernels:** big, small, and mega (the cost lens: kern_mega runs the same vid_detect, megak kernel.map:2999, and an AT with an MDA primary is plausible).
- **Rank:** S high, L medium (MDA is the period's commonest mono card; MDA+CGA with switches at mono is the commonest dual-monitor machine), B high (dead screen at best; period documentation warns about out-of-spec drive of a fixed-frequency 5151; the usable CGA sits idle).
- **Mechanism:** vid_detect makes every mono-flagged machine VID_HERC (viddet.inc:588-594; the scope cut is written at 527-530), and vid_setmode writes 3BFh = 1, 3B8h and vid_6845's 720x348 table, R0 = 35h (397-398, 1391-1413). On an MDA the 6845 counts 9-dot characters at about 1.806 MHz, so 54 totals give about 33.4 kHz against the 5151's 18.4 kHz; only a Hercules in graphics mode (16-dot characters, about 18.8 kHz) matches the table (truth lens). SPEC 39.1's cut (SPEC.md:76233-76236, from 1518792c) argues "strictly less wrong than driving it as a CGA at B800" - about an MDA ALONE; it says nothing about MDA+CGA. vid_cga_equip already flips 40:10 so that int 10h reaches the CGA (viddet.inc:1496-1515). vidsel.inc's alias and 3BFh retry test discriminates MDA from Hercules, for the SECONDARY only (131-180, 231-253).
- **Fix:** SPEC first: rewrite SPEC 39.1's cut AND SPEC 39.11.1's closing sentence ("that cut still stands for detection", SPEC.md:77212-77214). In the mono arm write 3BFh = 1 FIRST - an unconfigured Hercules aliases like an MDA (SPEC 39.11.1.1), and an MDA does not decode 3BFh - then discriminate with Wilton's 3BAh bit-7 toggle (the fit lens's preference: no copy of vid_memchk for the MDA test) or vid_memchk's 0x1000 alias test. The secondary's probe cannot simply be called: vid_probe_avail runs after the mode set, by which time the splash has written the CRTC (kernel.asm:5720-5728), and vid_memchk is .ovl on kern_big and kern_mega but .boot2 on kern_small (vidsel.inc:110-128, 157). An MDA with a CGA probes B800 - still a memory test, which the 3BAh toggle does not replace (cost lens) - and becomes VID_CGA; an MDA alone halts with a message. **That is the owner's rule (owner, 2026-11-08):** driving an MDA with Hercules timings can damage the monitor, and os8088 does not run on an MDA - print an MDA message and halt, or switch to the usable CGA when one is present. Land it with BR-14 so that [vid_avail]'s CGA bit means one thing on both kernels (fit).
- **Bytes:** the area and fit figure, +25..+35 .boot2, prices the probe alone. Re-derived with what the probe needs on kern_big and kern_mega (cost lens): a blob copy of vid_memchk (55 bytes, 0xCE00..vid_probe_avail 0xCE37 in build/kernel.map and the same 37h in megak, MEASURED), the 3BFh write (~6), the B000/B800 probes and branches (~14) and the MDA-alone halt with its message (~30-40): **+60..+110 .boot2 on kern_big and kern_mega** (the 3BAh form trims only the MDA test, ~20 against 55). A cheaper shape on kern_big moves vid_memchk from .ovl into .boot2 as kern_small has it: .ovl -55 (which the full overlay would welcome, see *Cross-cutting constraints*), blob +~85. **kern_small +25..+65 .boot2** (its vid_memchk is already in the blob, smallk kernel.map:2034; +~30 for the message). 0 resident on all. Room: kern_big's blob has 4 bytes free, so a 14th blob sector was certain there, even after boot-7's -41. **The owner refused that sector (owner, 2026-11-08)** - it delays the progress bar by 0.4-0.8 s - and BOOT3-PLAN takes its place: BR-6 **stays in `.boot2`** (it runs in `vid_detect`'s mono arm in the first tick, before `.boot3` exists), `vid_memchk` is written once as a macro and expanded in `.boot2` and `.ovl`, and the alias-test form is paid from wave 1's ~508 free blob bytes (~549 after boot-7), so the 3BAh form is no longer forced by room (docs/plans/BOOT3-PLAN.md section 11). The cheaper shape above (moving vid_memchk out of `.ovl`) is no longer needed. kern_small has 702 bytes free and kern_mega 285: both fit. **ABI:** none. **Confidence:** confirmed (the monitor-damage risk is lore, not verified; the dead screen and idle CGA stand on the code).
- **Rows:** no row. MartyPC's `_vga_mda` profile carries the MDA as a secondary; an MDA-primary profile is wanted.

### BR-7 The loader trusts a package header against its own region

- **Sources:** loader-1 (bug, high, confirmed), loader-3 (bug, medium, confirmed), loader-2 (bug, medium, confirmed). Lenses: truth stands, cost amend, fit amend.
- **Kernels:** all.
- **Rank:** S high, L low-medium (a corrupt or foreign file; The Wire feeds the image arm network bytes), B high (memory above the region overwritten, with top-down placement the last-launched package's code, instead of "Bad package").
- **Mechanism:** ld_check_hdr bounds the image only to at least LD_HDR_SIZE and at most APP_MAX_SIZE, and the file only through the image-against-file ladder (loader.inc:445-500). Step 6a builds the clear prefix in BX, up to 32+64+16+16+192 = 320 bytes (1018-1035), and does `sub cx,bx / sub dx,bx` with no carry test (1066-1067) before handing DX to lz_decomp_x as its output bound: image 64 with every prefix flag set gives DX = 0xFF00. ld_where's R = roundup512(image - file) (858-889) is never checked against ld_alloc's roundup512(img+bss) (1168-1180) - the code's own comment says "a foreign header can, by up to 512 bytes, and nothing here refuses it" - so image 64, file 48, bss 0 lands the read at offset 512 of a 512-byte region, and the image-arm copy can overrun by 511 (983-991, 1923-1933). The swap re-check (1093-1094) rewrites [ld_img]/[ld_bss]/[ld_ent] through ld_hdr_size, bounded only by APP_MAX_SIZE, and step 7's `rep stosb` (1303-1307) is never compared with [ld_need]. The module's cmz_uncomp/cmz_hdr and os88pkg.py check what the kernel does not. SPEC 20.13.8.1 records BOTH guards as "not taken" (SPEC.md:49182-49197; the 512 one from 571befce, the image > prefix sentence from this review's own doc pass, 14428309), and loader.inc:876-881 says "this cannot overflow it".
- **Fix:** SPEC first: turn SPEC 20.13.8.1's two guards to "taken" and correct loader.inc:876-881. Then the in-place shape (cost lens; the fit lens's cheaper alternative): loader-1's two `cmp/jbe` pairs at step 6a with a pop-ds LD_EBAD stub; loader-3's fit test in ld_where after `and bx,0FE00h`, against [ld_need]; loader-2's `cmp dx,[ld_need] / ja` to an LD_EBAD exit at the swap re-check's CALL SITE after :1094 - NOT inside ld_check_hdr, whose peek at :962 runs before ld_alloc sets [ld_need], and whose header documents DX as clobbered (513), although DX = image+bss does survive it today (504-524) (fit). The first register's "compute the prefix once in ld_check_hdr and keep it in a .bss word" MOVES the ~31-byte ladder rather than sharing it (`test dl,imm8` is 3 bytes against `test al,imm8`'s 2, and the store and reload add ~8 and a .bss word), and if taken must keep DX = image+bss. Either shape covers all three ld_check_hdr callers (962 peek, 1094 re-check, 1886 image arm - the one The Wire feeds).
- **Bytes:** **+32..+36 .cold res on kern_big and kern_small** in place (+14 loader-1, +12..+16 loader-3, +6 loader-2 - the per-finding verifiers' standalone figures agree), 0 .bss; the fold +35..+45 .cold and +2 .bss. The earlier ~+24 assumed the fold shared bytes it does not (cost lens). kern_mega: ld_check_hdr is .cold at 0x17B09, above 1MB, so 0 MEGA_RESIDENT (+0..+2 .bss only in the fold). A dozen instructions per launch. ESTIMATED. **ABI:** none (the header layout is unchanged). **Confidence:** confirmed.
- **Rows:** lzfence, lzload, lzload-lz4, lzfmt-all, lzbig (soak). None feeds a hostile header; one should.

### BR-8 wm_ptr2idx divides an unvalidated window pointer

- **Sources:** wm-1 (hazard, high, corrected), wm-2 (size, the alternative fix), wm-13 (consistency, medium), histsys-11 (weakness, low), gfce-5 (hazard, low, confirmed: DESK.DRV's .make has the same unbounded divide). Lenses: truth amend, cost amend, fit amend; phase1 WP8 prices the same work. Feeds BR-9; BR-29 is the loader's road into it.
- **Kernels:** all; the .make half is DESK.DRV (kern_big).
- **Rank:** S high, L low (a wild BX from a package, or a stale one from a worker), B high (INT 0: the IBM 5150/5160 ROM's stub masks the 8259 and IRETs - every interrupt gone; a 286+ re-executes the div for ever).
- **Mechanism:** `sub ax,wm_wins / mov cl,WIN_SIZE / div cl` (wm.inc:7259-7263; WIN_SIZE 74 on kern_big, 65 on kern_small, wm.inc:128-133) overflows AL for AX of 256*WIN_SIZE or more (18,944 bytes on kern_big, 16,640 on kern_small), a range that includes any BX below wm_wins; the clamp comment's "wrong and bounded" holds only inside the table. Unvalidated doors: OSAPI_WM_SHOW/HIDE/FRONT go through wm_lk_go to their bodies with no check (wm.inc:3298-3330, 3345-3362, 3675, 4368), OSAPI_WM_DESTROY likewise (3048), OSAPI_WM_CLIP_SET reaches wm_zabove through wm_clip_occw (6023, 6362-6368), and OSAPI_WM_WAKE writes [bx+W_WKQ] with "BX is trusted" (os88api.inc:1733). The inline setter cells write `[cs:bx+W_*]` (wm.inc:1530-1536). DESK.DRV's .make tests W_FLAGS and [ld_base] == [bx+W_SEG], then divides bx - wm_wins with no range test and indexes wm_owner with the quotient (deskmod.inc:236-266).
- **Fix:** the truth lens: a bound in front of the div ALONE swaps a dead machine for silent corruption - a wild BX then maps to slot MAX_WIN-1, which can be a live window, while the bodies still write through the wild BX (wm_show_b's `or word [bx+W_FLAGS],2`; wm_destroy zeroes [bx+W_FLAGS] and [bx+W_WKQ] and then drops the clamped slot's save-under and z-order entry, unlinking another package's live window). So: (1) bound before the div and delete the post-div clamp it makes dead (`cmp al,MAX_WIN / jb .ok / mov al,MAX_WIN-1`, wm.inc:7275-7277; its `mov` becomes the jae target) - the compare is wm_isrec's (wm.inc:11655-11661); (2) wm_isrec refusals with CF at the SHOW, HIDE, FRONT, DESTROY and CLIP_SET doors (wm-13's fix, now part of this item); (3) the wake fence at wm_wake's entry and MEMC_POST - which reverses a written contract, so SPEC first and abilock --update with no bump; wm_isrec is a range test with no alignment check, safe at ISR time but still accepting a misaligned in-table BX, which the text should say; (4) DESK.DRV's .make takes wm_isrec's bound first. Write the BX-trust rule into SPEC 11 either way, and fix inst_wchk's same off-by-one comment. **If the owner declines (2)-(4) under the packages-are-trusted ruling, prefer leaving the div to BR-9's panel over taking (1) alone: a named stop beats a write into another package's window.** **Decided (owner, 2026-11-08): packages are trusted, so (1)-(4) are not taken as retail code.** A wild BX from a package is the package's bug; the div stays as it is (a dead machine on the IBM ROM, a named stop in BR-9's knob build), and (2)-(4) at most go into the APICHK knob (phase1 WP19). What stays: the BX-trust rule written into SPEC 11, and inst_wchk's comment (0 bytes). wm-2 (store W_IDX, make wm_ptr2idx a bounded load) is the alternative the fit lens advises against: the load also reads through the wild BX and needs the same doors; the W_IDX fill cannot ride wm_create's zero run and costs +10..+15 in .boot2/.ovl, both full on kern_big; WIN_SIZE 74 -> 75 leaves every other record's words odd-aligned on the 16-bit-bus machines kern_big and kern_mega also run on (pad to 76: +12 .bss) (truth and fit, phase1 WP8); and tools/os88geom.py:142 pins 74/65 for t_mirror while btndown.py, chromedown.py, dragcost.py, deskclip.py and os88dosdbg.py stride the table by it.
- **Bytes:** (1) **+1..+2 .text res** (truth +1, fit ~+2, cost +1..+5; wm_ptr2idx is 19 bytes, 0x51AE..0x51C1 in build/kernel.map and 0x3DE7..0x3DFA in smallk, MEASURED); (2) ~+4..+6 a door plus one shared CF exit, ~+22..+32; (3) +6..+12 .text; (4) +5 DESK.DRV. **Total +29..+46 .text res**, all kernels; ~+8 clocks of execution (~20 at the fetch floor) against an 80-90 clock div. **Not taken (owner, 2026-11-08): 0 retail, and out of every batch sum below**; the figures stand for an APICHK build. wm-2 instead of (1): +4 kern_big / -2 kern_small resident PLUS +10..+15 .boot2/.ovl, +12 .bss if padded, ~-75 clocks a call. phase1 WP8 prices this item and BR-9 again (kern_big +34 +12 .ovl, kern_small +28, with wm-2): count it once - the batches below use this item's figures. **ABI:** (3) is SDK text. **Confidence:** corrected (the overflow boundary; the fix by the truth lens).
- **Rows:** int0sweep (soak) is the detector; it traps the INT instruction with a debugger breakpoint, so BR-9's handler does not blind it. No row passes a wild window pointer.

### BR-9 No divide-error or invalid-opcode handler on the real-mode kernels

- **Sources:** histsys-2 (hazard, medium, corrected), isr-8 (weakness, medium, confirmed), gfce-1 (hazard, HIGH, confirmed: the same proposal, scoped and priced), gfce-2 (rethink: close the package, not the machine), gfce-3 (weakness: make the divide fence a ratchet). BR-8 (with gfce-5), BR-23 and BR-29 are the reachable divides this turns into a report; gfce-4 (FSX_SPK N=0) was refuted and is not one of them. phase1 WP8 prices it too (see BR-8).
- **Kernels:** big, small (and mega before its switch; its IDT handles INT 0 after).
- **Rank:** raised to S high (gfce-1), L medium (package code dividing hostile data reaches it; SPEC records SKIES' ADI and a ZMODEM size field doing so, SPEC.md:165882-165890, 127741-127743; and ctrl.inc:5966-5975 records a SHIPPED Control Panel gutter click that overflowed a div), B high (dead machine on the IBM ROM; silent hang on a 286+; garbage on GLaBIOS).
- **Mechanism:** nothing writes IVT 0 (or 1, 3-7) on kern_big or kern_small outside BOOT_MARK's mark_hook (kernel.asm:7721-7752); the comments at instance.inc:1984, ctrl.inc:5980-5982 and wm.inc:8264-8275 all say no handler is installed. On a 286+ #DE pushes the IP OF the div, so the BIOS dummy's IRET re-runs it; the 5150 masking rests on the tree's own note (wm.inc:8268-8275). It contradicts the kernel's own "a halted machine is a bug report" (sched.inc:2601-2612).
- **Decided (owner, 2026-11-08): a knob, off in shipped kernels.** On kern_mega, where the kernel can afford not to crash, the faulting package is closed (mega_fkill already does this; gfce-2 answered). In real mode a divide fault deserves at least a stack-overflow-style message where it cannot be recovered from, but it has not been met in production, so the panel's resident bytes - the stubs, the install and the generalised panel, the ~174-byte answer included - wait behind a build KNOB until a field report says it is being hit. The fix below is the knob build's. gfce-3's divide ratchet is build-time (0 bytes) and stays. The same weighing applies to every diagnostic panel for a failure nobody has met (BR-68, BR-80, BR-82, BR-173's optional panel).
- **Fix (the knob build):** as gfce-1 scopes it: an ~11-byte resident stub (cli, pop IP and CS, DS = CS, SI = the message, jmp into the panel); sch_diepanel generalised to take SI = message and AX/DX = the evidence and print "DIVIDE ERROR TASK nn AT cccc:iiii NAME" where the overflow case prints T_SP; installed from the boot overlay at sched_init - vector 0 always, vector 6 only when cpu_tier >= 286, never 5 (PrtSc on a PC). Reuse BOOT_MARK's mark_hook shape, not its vector list, which includes 5 (fit, phase1 WP8). sched-9's INST_MAX bound (BR-84) rides with it. Policy cost: on non-IBM 8088 ROMs a divide overflow that today continues, often harmlessly, will stop the machine - the kernel's written doctrine (sched.inc:2580-2583). Later, an owner decision (gfce-2): close the faulting package rather than stop the machine, kern_mega's mega_fkill shape (+45..+65 on top). And gfce-3: a 0-byte fast-tier ratchet registering every div/idiv with its proof (5 of ~90 kernel divide sites are unfenced, and all 5 trust a value that crossed a boundary).
- **Bytes:** **0 shipped (owner, 2026-11-08); out of every batch sum below.** In the knob build: **~+55 res kern_big (with the #UD stub), ~+40 kern_small, +12 .ovl and +8 more for vector 6** (gfce-1, ESTIMATED; the first register's ~+30 priced the stubs without the generalised panel). The panel body the stubs join is 174 bytes on kern_big (sch_stkdie 53AEh to sch_stkbase 545Ch, strings included) and 173 on kern_small (MEASURED from the maps). ROOM: kern_big's boot overlay is at 5,119 of 5,120 bytes, so the install needs a body moved out of it first (*Cross-cutting constraints*). **ABI:** none. **Confidence:** corrected / confirmed.
- **Rows:** int0sweep, stkpanel (soak). New (gfce-1): a tests/ package dividing by zero in a callback, on MartyPC GLaBIOS and as a box- row on an 86Box 286; it must time out when the install is removed.

### BR-10 Esc on the Resume face discards the hibernated session

- **Sources:** hiber-7 (weakness, medium, confirmed).
- **Kernels:** big (HIBER.DRV).
- **Rank:** S medium, L high (Esc is the natural "not now" on a question at boot), B high (both files deleted with no confirmation; the session is gone).
- **Mechanism:** hbm_onkey maps Esc to button 2 in every mode (hiber.inc:1366-1382); in HB_M_RESUME button 2 is .discard -> hbm_forget (1411-1414). The close box already does the non-destructive thing (hbf_closed, 738-744). SPEC 87.5 knows Esc is Discard and only drains key repeats (SPEC.md:158622).
- **Fix:** in RESUME mode Esc closes the window and keeps the files; Discard needs the click or a letter key.
- **Bytes:** +0..+4 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernate, hibernatedrv, hibernatem (soak).

### BR-11 The purge floor is one word, stamped with a task that names nobody, and lifted by any teardown

- **Sources:** mem-4 (bug, medium, confirmed), files-4 (hazard, low, confirmed), hm-13 (weakness, low), loader-8 (weakness, low). ctx-1 adds that mem_free_rec_x's unconditional floor store runs on the dying worker today (memory.inc:4847-4848); BR-4 moves it to the UI task.
- **Kernels:** big, small (the paste is FILECP.DRV on kern_small).
- **Rank:** S medium, L medium (closing any window while a multi-file paste asks an overwrite question, under memory pressure), B high (the paste silently stops part-way and reports success).
- **Mechanism:** OSAPI_MEM_FLOOR stamps [sch_cur] (memory.inc:5848-5853), which is 0 for every package callback, the DOS and the kernel's paste alike. mem_free_rec_x writes MEM_LVL_TOP on every instance teardown, a Disk window's included (4826); fcp_lend does the same (filecp.inc:2348); fcp_paste's multi-file arm overwrites another task's floor with MED (457-459). The next entry's copy buffer then sheds MEM_P_FLIST and fcp_lnext reads [fcp_lseg] = 0 as DONE (532-547). ld_unreserve, a drifted twin of mem_free_rec_x, misses the floor lift, so an aborting entry proc leaves its floor standing (loader.inc:216-262).
- **Fix:** stamp the floor with inst_caller's instance (DH; 0xFF for the kernel) as well as the task; lift it in mem_free_rec_x and fcp_lend only when the stamp names the dying instance; fcp_paste does not set a floor over another's. For loader-8, write I_KIND and I_SPTR at step 5 (the record is unpublished), so ld_unreserve becomes desk drop + cw_xm_release_rec + mem_free_rec_x.
- **Bytes:** ~+8..+18 .cold res; loader-8 ~-3 .cold big, ~-20 .cold mega (ESTIMATED; owned by phase1 WP1, which prices it too, and not counted in batch D). **ABI:** none. **Confidence:** confirmed.
- **Rows:** fcpcopy, fcpapi, fcproom, memp (soak). No row closes a window mid-paste.

### BR-12 mem_regrow's path-3 move copies the whole block with interrupts off

- **Sources:** hm-1 (bug, medium, corrected), mem-2 (bug, medium, confirmed); ctx-4 (BR-178, the IF=0 bound this item is the worst case of).
- **Kernels:** big, small.
- **Rank:** S medium, L high (a grow that must move: Paint, Fractal, the clipboard, on an XT with a serial mouse), B medium (86 ms per 32KB, ~170 ms per 64KB at IF=0 - PREDICTED, arithmetic from memory.inc:2046's 16K words x ~25 clocks, not measured (ul-11): every tick past the first lost, a 1200-baud mouse overruns within ~17 ms, a 2400-baud link loses bytes, an SB IRQ is late).
- **Mechanism:** mem_regrow is one pushf/cli window from entry (memory.inc:4559-4560) to its popf (4664) around mem_hifit (4616) and mem_bcopy (4640). The compactor moves the same blocks at IF=1 under [sch_lock] and cli's only for MEM_K_DRV (2973-2977). apps/fractal/fractal.asm:846-848 already plans around the window.
- **Fix:** find the destination and publish under cli, raise [sch_lock], restore IF for the copy, cli again for the record store - EXCEPT a MEM_K_DRV record, or any block an ISR writes into, which copy at IF=0 as mem_cp_walk does. Document that a holder whose ISR writes into the block quiesces it before a grow (SPEC 66.4.2's movable-claim rule).
- **Bytes:** +10..+16 .cold res; ~+16 with the kernel-tag refinement (keep IF=0 for kernel-tag owners, whose ISRs may write the block; IF=0 does nothing against DMA - ctx-4). **ABI:** none. **Confidence:** corrected / confirmed.
- **Rows:** regrowshed, clipgrow, heapcheck, heapmap (soak). New (ul-10): ifoff --regrow, regrowshed's scenario with a moving grow and breakpoints at memory.inc 4559 and 4664; break it by reverting the fix.

### BR-13 kern_mega: a reflection on the LENT stack reads .lowbss through SS

- **Sources:** mc-1 (bug, high, corrected).
- **Kernels:** mega.
- **Rank:** S high, L medium (WIN16's Mozart int 10h AX=1A00h goes this way on every limit-checking CPU), B high (#SS in .cold code, which mega_fkill treats as the kernel's fault: the machine halts).
- **Mechanism:** mega_svc_lend loads SS = [mega_rsseg], the 1KB claim's selector with limit 3FFh (mega.inc:3463-3488, 14062-14073), and continues at mega_svc.low. mega_s2segc's first instruction is `cmp byte [ss:mega_pm],0` with mega_pm at .lowbss 1347h (5457-5458); mega_p2d reads mem_tab through SS (5425; mem_tab at .lowbss 13CCh); mega_hibnc lands the bounce at SS:dsk_secbuf (5825); mega_rseg's .nu arm reaches mega_ldt_alloc's `cmp di,[ss:mega_ldte]` (9858). QEMU checks no limit and reads a heap byte instead.
- **Fix:** read through ES = LOW_SEG by name, as mega_codesel does, on the reflection path and its .cold siblings (mega_slidec, mega_kaimf, mega_oddsegc, mega_bkaimf, mega_backf); mega_hibnc `mov ax, LOW_SEG`. Consider a fast gate listing non-bp `[ss:]` operands reachable from mega_svc.
- **Bytes:** +16 (the reflection path) to ~+40 (with the siblings), .cold/.mega above 1MB; 0 conventional. **ABI:** none. **Confidence:** corrected (a fourth read added).
- **Rows:** no row. The win16mega* rows run on QEMU, which cannot see it; add a Bochs probe that lends a 1KB stack and issues int 10h 1A00h and 1130h (docs/BOCHS.md).

### BR-14 kern_small's fsx_page enables Hercules page 1 on a machine that also has a CGA

- **Sources:** video-1 (bug, high, corrected); gkm-2 (weakness, medium, confirmed: one of kern_small's separately written guards, BR-62).
- **Kernels:** kern_small (kern_big's guard is replaced by the same shared test).
- **Rank:** S high, L low-medium (a 5150 with Hercules + CGA running the 128KB kernel and a page-flipping app such as WireBall), B medium (both cards decode B8000 for the bracket; page-1 writes land in the CGA's RAM).
- **Mechanism:** the dual-card refusal in fsx_page's Hercules arm is `%ifdef KERN_BIG` (fsx.inc:1027-1061, guard at 1031-1038), and vid_dual_ok lives inside vidsel.inc's KERN_BIG block (1356-2299). kern_small does build vid_avail with both HERC and CGA bits (vidsel.inc:110-128) and offers FSXM_HERC with 2 pages (fsx.inc:172, 1175-1177); BX = 030Ah writes 3BFh = 3 on every call, page 0 included. CLAUDE.md promises the API refuses here, and WireBall relies on it (apps/wireball/wbvideo.inc:82-87).
- **Fix:** on both kernels, `test byte [vid_avail], VID_A_VGA|VID_A_CGA / jz .single` (in the .herc arm vid_kind is VID_HERC, so the colour bit is the whole question) - the guard tests the hardware capability it protects, not the build (gkm-2). Land it with BR-6, so [vid_avail]'s CGA bit means one thing on both kernels (fit lens on BR-6).
- **Bytes:** +13 res kern_small, +2 res kern_big. **ABI:** none. **Confidence:** corrected (byte count).
- **Rows:** wireballvideo, wireballmono, dispfsxherc (soak).

### BR-15 The system clock gains about 31 seconds a day

- **Sources:** sysmisc-1 (bug, medium, confirmed).
- **Kernels:** all; and the Timer package.
- **Rank:** S medium, L high (every machine, every day: nothing re-reads an RTC after boot, and a 5150 has none), B low (3.6 minutes a week).
- **Mechanism:** clk_tick computes seconds as delta*10/182 (clock.inc:1686-1692) while [ticks] runs at 1193182/65536 = 18.20648 Hz; each counted second is 0.99964 s. apps/timer/timer.asm:208 has the same constant. kerndos re-derives the time from 40:6C and does not accumulate it.
- **Fix:** `mov dx,625 / mov cx,11379` (11379/625 = 18.20640, about 0.4 s a day); the same encodings; overflow checked (65535*625+11378 fits, quotient at most 3600; hibernation's 32768-tick passes stay in range). Update clk_acc's comment, SPEC 37 Advancing, and the Timer.
- **Bytes:** 0. **ABI:** none. **Confidence:** confirmed (predicted, not measured on a 5150).
- **Rows:** deskclock, arclock (soak). None measures the rate.

### BR-16 Media-changed status 06h is retried as a generic error and thrown away

- **Sources:** dr-4 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (an AT-class BIOS and a fast swap inside a quiet switch whose motor test passed, or inside a batch), B high (the write goes to the new disk with the old disk's FAT).
- **Mechanism:** dsk_xfer's failure arm special-cases only 03h (disk.inc:2020-2056), so 06h is reset and retried, the retry succeeds, and the only hardware swap signal is lost; the media test is motor-only (8342-8392). The change line was declined as a REPLACEMENT for the motor test (FIELD-NOTES 63-67); this is additive and arrives on a read already being made.
- **Fix:** on 06h with [dsk_mntok] = 1, clear [dsk_mntok] (the next operation remounts and the write gate shuts) and fail with ioerr 06h, no retry; during a mount keep today's retry.
- **Bytes:** +16 .cold res. **ABI:** none. **Confidence:** confirmed (whether a given AT ROM returns 06h with CF=1 to the caller is the open question).
- **Rows:** fdswap, wseq, dosmedia, curdisk (soak).

### BR-17 A failed write on a multi-window FAT16 volume leaks its half-built chain - on Disk full, all free space

- **Sources:** dw-2 (bug, medium, confirmed).
- **Kernels:** big, small (FAT12 floppies, whole window, are immune).
- **Rank:** S medium, L medium (a hard disk filling up), B high (the volume stays "full" until a System Check repair, even after the user deletes the file they were saving).
- **Mechanism:** the plain rollback arms only call dskw_refat, an invalidate with no I/O (diskw.inc:1600-1602, 4089-4092), but the FAT16 scan and links moved the window and dsk_fat_window flushed before evicting, so the chain built so far is already on the disk (5008-5012). The HELD arm frees its own sub-chain first and names exactly this leak (4069-4087).
- **Fix:** in both plain arms `mov ax,[dskw_first] / call dskw_free_chain / call dskw_flush_x`, and refat only if that flush fails.
- **Bytes:** +12 .cold res big, +9 small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** dosfull, fsckhd, fsckdisk (soak).

### BR-18 A 16550 whose FIFO is already on is treated as an 8250

- **Sources:** input-1 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (after any DOS program, warm-booted driver or earlier OS that enabled the FIFO; 16550s are common on AT and 386 boards), B medium (at trigger 14 the pointer lags ~117 ms; at trigger 1 two queued bytes leave INTR high with no new edge and the mouse is silent for the session).
- **Mechanism:** mouse_init never writes FCR (mouse.inc:767-800, 888-896); the ISR reads one RX byte per port per interrupt (3720-3753); the SPEC 9.13 re-arm rewrites MCR and IER only (1585-1590).
- **Fix:** FCR = 0 beside the LCR writes (`mov ax,0x0200 / call mou_ov_pall`; an 8250 ignores it) and in osapi_mouse's re-arm; optionally re-read the row's LSR after mou_byte so no byte is left behind.
- **Bytes:** +6 .ovl (big) / +6 .boot2 (small), 0 res; +6 .text big for the re-arm; optional +2 .text both. **ABI:** none. **Confidence:** confirmed (not measured).
- **Rows:** serlease, dosmouse, doswmouse, kdmouse (soak).

### BR-19 A tick can park a ROM int 09h with IRQ1 still in service

- **Sources:** input-2 (hazard, medium, confirmed).
- **Kernels:** big on an AT ROM advertising int 15h 4Fh (KBD_I15), mega (its jmp far); the generic fix reaches all.
- **Rank:** S medium, L medium (AMI/Phoenix/IBM AT ROMs sti at int 09h's entry and EOI at its end), B medium-high (IRQ2-7 blocked until the parked task runs: serial mouse overruns, PS/2 stalls; if the running task is inside int 13h under sch_lock the parked task cannot run, IRQ6 is never delivered and the call returns a disk error after ~2 s).
- **Mechanism:** the I15 path is not inside the SPEC 9.8.3 IMR bracket (mouse.inc:3016-3019, 3034-3039); kbm_i15 wakes the UI task (3079-3099); sch_isr switches every tick without asking whether it interrupted an interrupt handler (sched.inc:2380-2392); disk.inc:1462 holds sch_lock across int 13h. PREDICTED, not measured: SeaBIOS does not sti in its IRQ handlers, so QEMU and Bochs cannot show it.
- **Fix:** the generic rule - sch_isr refuses a switch while any IRQ other than IRQ0 is in service: OCW3 0Bh, `test al,0FEh`, restore 0Ah (khb_* already uses the sequence, sched.inc:1947-1953). **Follow-up (2026-11-08):** a NESTED IRQ0 already refuses the switch through `[sch_chbusy]` (sched.inc:2163, 2201, 2285, 2397; SPEC 8.5.1), and the OCW3 read is the heartbeat's `khb_pisr` (sched.inc:1961-1968), so the only new part is the test for an IRQ other than IRQ0, beside `[sch_chbusy]`. This also covers kern_mega's jump and any driver ISR that sti's. Verify on 86Box with an AMI or Phoenix AT BIOS.
- **Bytes:** +~14..+16 .text res, all kernels; ~40 cycles on the switch path only (ESTIMATED). **ABI:** none. **Confidence:** confirmed (a prediction).
- **Rows:** kbd4f, kbdpreempt, kbdburst (soak; on SeaBIOS they cannot fail for this). The box- twins on 86Box are where it can be seen.

### BR-20 After the contest the mouse ISR still drains retired and foreign UARTs

- **Sources:** input-3 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a DOS program under os8088's DOS, or a package, driving COM2 while the mouse is on COM1, without the serial lease), B medium (its bytes are read and discarded before its own IRQ3 handler sees them; at 9600 baud about one per cursor move).
- **Mechanism:** mou_isr_body scans every row with a nonzero base on every interrupt (mouse.inc:3720-3753), because mou_lockon keeps the bases for mouse_unhook (1215-1260); mou_byte drops a non-[mou_port] byte AFTER the RX read (3793-3798). LSR data-ready does not depend on IER.
- **Fix:** once [mou_seen] is set, scan only [mou_port]'s row and [ser_row] (`cmp byte [mou_seen],0 / je .take / cmp bl,[mou_port] / jne .next`). Cross-wired machines are unaffected; two-port machines save an IN per row per IRQ.
- **Bytes:** +~13 .text res, both. **ABI:** none. **Confidence:** confirmed.
- **Rows:** serlease, dosmouse (soak).

### BR-21 The cursor is drawn at IF=0 inside the serial ISR, which caps SERIAL.DRV's usable rate on an 8250

- **Sources:** isr-7 (hazard, medium, corrected); cursor/mc-3 (BR-175) for kern_big's VGA figure; systemic T2's truth lens for the rethink's stack.
- **Kernels:** big, small.
- **Rank:** S medium, L medium (the lease defaults to 9600 baud; the mouse moving while bytes arrive), B medium (an 8250 overruns once a second byte completes behind an unread one, in 1-2 byte times, against cur_move's 2.4 ms on CGA and 4.5 ms on kern_big's VGA - 3.2 ms only with KHP_VGASAVE, which kern_big does not define).
- **Mechanism:** mou_byte -> mou_apply -> cur_move runs inside mou_isr's port scan before the EOI, IF=0 throughout (mouse.inc:3718-3762, 4212-4300); PERFORMANCE.md:1356 measures cur_move at 11,500 cycles CGA and 15,100 VGA - but the VGA figure is with KHP_VGASAVE, a kern_mega-only knob (kernel.asm:265-294); kern_big's own is 21,450 cycles, MEASURED in LAST-DROP-PERF.md:590 (cursor/mc-3). The baud ceilings derived from these are PREDICTED; the spans are measured (ul-11). SPEC 115.2's "keeps mouse movement from stealing serial data" is true of the routing, not of the timing.
- **Fix:** measure first: tests/serlease.py at divisor 12 with the mouse moving, on os8088_xt_vga and a CGA twin, counting LSR overrun bits. Then either default the lease to 2400 on an 8250 and document the ceiling, or the rethink: EOI, `inc sch_lock`, a busy byte, sti, the redraw loop, cli. The rethink must also deal with the stack: mou_isr runs on MOUPRIV, a 128-byte private stack sized on the premise that "nothing can interrupt this stack" (mouse.inc:357-361, 375, 392-396); once the redraw runs at IF=1, IRQ0's frame, a ROM int 09h and the floppy completion nest on it. Either leave MOUPRIV before the sti, or resize MOU_PSTK for the nested depth and re-measure it with stkdiag (truth lens, systemic T2).
- **Bytes:** 0 to measure; +30..+40 .text res for the rethink, plus +~6 to leave MOUPRIV before the sti or +64..+128 .lowbss to grow MOU_PSTK. **ABI:** none. **Confidence:** corrected (the cycle figures; kern_big's VGA span by cursor/mc-3).
- **Rows:** serlease, wirefleetlink (soak). New (ul-10): ifoff --mouse, mou_isr_body to the EOI with os88mouserel motion on os8088_xt_vga and the 5150 CGA twin - serlease does not count overruns.

### BR-22 The UI-task-only rule for file cells (and the clipboard's) is prose

- **Sources:** dw-4 (hazard, medium, confirmed), abi-8 (weakness, medium), ui-8 (hazard, medium, confirmed), xm-12 (weakness, low, confirmed: the XMEM cells' rule), ctx-7 (weakness, medium, confirmed: the systemic fix); loader-5's worker half is BR-30. The systemic fit lens (T1) on dw-4's refusal.
- **Kernels:** big, small.
- **Rank:** S medium, L low (a package worker calling a file cell while the UI task is in one), B high (the bodies share dskw_raw, dskw_n83, dsk_secbuf, the dirty range and api_name/api_name2: a silently cross-linked FAT).
- **Mechanism:** SPEC 18.4 says "The kernel cannot enforce this" (SPEC.md:34500-34501; diskw.inc:27-31; shared staging kernel.asm:5213-5214), yet the kernel tests "am I the UI task" with `cmp byte [sch_cur],0` in several places (memory.inc:955, instance.inc:2203, toast.inc:407). The clipboard is the reverse case: the SDK and SPEC 55.4 tell authors a worker MAY call it (os88api.inc:5669-5670; SPEC.md:110111) while clip.inc:36-56 says it must not, for good reason (mem_claim, 32KB bursts between movable segments). The XMEM cells are a third family: SPEC 41.8 makes ALLOC, FREE and COPY UI-task only because tier 1 is a BIOS call that borrows the caller's stack, but no cell checks and os88api.inc:2960-2983 names no context, so a worker copy on tier 1 runs AH=87h on a stack of about 256 bytes (xm-12).
- **Fix:** `cmp byte [sch_cur],0 / jne refuse` (FERR_NODISK) once in the file cells' N-stub, or in dskw_mounted/dskw_gate. First show that DOS.O88's worker (dvw_start, apps/dos/dos.asm:2010), kerndos, HIBER and the drivers never reach the bodies off task 0. Correct the two clipboard texts, and name xm-12's rule in the SDK (fingerprinted: abilock --update, no bump; SPEC 41.8's half is in the review's doc fixes). The systemic fit lens finds the retail refusal in conflict with the owner's ruling that packages are trusted (docs/plans/LAST-DROP-BYTES.md:1135-1140): either APICHK=1 only (gfce-9; the same check at 0 shipped bytes), or put dw-4 to the owner as a named exception - a cross-linked FAT survives the reboot that ends every other package fault. Do not count it in both (*Cross-cutting constraints*). Longer term, ctx-7: context rules as machine-readable tags on the cells and a call-graph gate, not prose lists.
- **Not taken: packages are trusted (owner, 2026-11-08).** The retail refusal is one of the six package-argument fences the owner declined; the check lives in the APICHK knob (gfce-9, phase1 WP19) at 0 shipped bytes, and its bytes leave batch E. The clipboard text half is already done (follow-up, 2026-11-08: `apps/os88api.inc:5684-5689` and SPEC 55.4 have said "never from a worker" since the review's own doc pass, `14428309`); xm-12's SDK sentence and the ctx-7 tags stay.
- **Bytes:** 0 retail (owner). In the knob, or as a named exception: +9 .text res (N-stub) or +14 .cold (two gates). **ABI:** SDK text (abilock --update, no bump); the refused call was already forbidden. **Confidence:** confirmed.
- **Rows:** no row.

### BR-23 A hostile FileSize faults two unguarded divides

- **Sources:** dw-1 (bug, medium, corrected); BR-9 is what the fault does.
- **Kernels:** big, small (both divides are outside any KERN_SMALL arm).
- **Rank:** S medium, L low (a corrupt or crafted directory entry claiming 32MB or more on a floppy, then OSAPI_FILE_APPEND, or a DOS program lseeking past 32MB in it), B high (INT 0).
- **Mechanism:** dskw_wabody's .div divides the offset by the cluster size with no high-word check (diskw.inc:3930-3932), and on APPEND the offset is the entry's size; dskw_read_at_x divides the caller's offset, bounded only by that size (3128-3134, 3168-3170). The truncate arm has the guard (3850-3851) and SPEC 18.4.7.5 states why.
- **Fix:** `cmp dx, <bytes per cluster> / jae <FERR_IO arm>` before each div (.jcor is in reach in wabody; READ_AT needs stc before .rdend); fix the "offset is under 33MB" comments.
- **Bytes:** ~+4 and +6..+8 .cold res (~+10..+12 in all) on BOTH kernels - kern_small is ~+11, not the +4 an earlier split gave it (ul-11). **ABI:** none. **Confidence:** corrected (both kernels need both guards).
- **Rows:** no row (int0sweep detects the fault).

### BR-24 An unreadable FAT window reads as "Disk full", after a per-cluster retry storm

- **Sources:** dw-3 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a bad sector in both FAT copies, or a failing drive), B medium (the wrong error after minutes of resets).
- **Mechanism:** dskw_alloc's .one treats a CF from dsk_next_clus_x as "used" (diskw.inc:4703-4713); the rover keeps candidates in range, so that CF can only be I/O. The FAT16 .scan falls back to .one on the same failure (4749-4750). Every caller maps AX=0 CF=1 to FERR_FULL - the misreport the routine's header says it exists to prevent (4680-4684).
- **Fix:** in .one, `jc` to `pop ax / stc / jmp .iofail`.
- **Bytes:** +4..+5 .cold res, both. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-25 A hard-disk boot fetches its own BPB with the AH=08h geometry boothd stopped trusting

- **Sources:** boot-5 (hazard, medium, confirmed), dr-1 (hazard, medium, confirmed), dr-13 (bug, low), boot-6 (weakness, low, corrected; supplies the room).
- **Kernels:** big (hard-disk boot).
- **Rank:** S medium, L medium (FIELD-NOTES 33's shape: a volume written under a geometry the drive's ROM does not answer, at a nonzero base), B medium (boothd boots and C: never mounts: no SYSTEM.CFG, no HDD.DRV, on a desktop that looks as if it works).
- **Mechanism:** dsk_boot_from_x asks int 13h AH=08h into dsk_bootspt/dsk_boothds (disk.inc:2786-2808), and the one read that fetches the BPB converts bpb_hidd to CHS with them (4074-4081) before dsk_bpb_check switches to the BPB's (5327-5330). boothd deliberately uses the BPB's spt and heads (boot/boothd.asm:16-36, 174-183) and hands over only DL and BX:CX (286-288). Hibernate and dsk_hcyl_probe read the same pair (hiber.inc:1669-1672, 2782-2785; disk.inc:2949-2970). dr-13: the 255-head guard is dead, `xor ah,ah / inc ax` never gives 0 (2803-2806). The AH=08h comment still says it is "the same question boothd asks".
- **Fix:** boothd hands over SI = spt and DI = heads with a marker in AX; dsk_boot_from_x uses them when the marker matches and they are sane (spt 1..63, heads 1..255), else falls back to AH=08h so an old VBR chaining a new KERNEL.SYS still boots; drop the fallback once every boothd carries the marker. boothd has 7 bytes free, so take boot-6 first: compare MEMNEED against the top of RAM before the subtract, as stage 1 does (-9 boot sector). Write the hand-off into SPEC 52.10.3. Verify on FN33's dumped images in 86Box first.
- **Bytes:** boothd +8 and -9 (boot sector); +6 .ovl now, -28 .ovl once AH=08h goes; dr-13 -2 .ovl; 0 res. **ABI:** none (a private boot hand-off). **Confidence:** confirmed (predicted from reading, not run).
- **Rows:** hdboot, hddgeom (soak), and their box- twins on 86Box.

### BR-26 The user's clock setting never reaches 40:6C, so os8088's DOS disagrees with the menu bar

- **Sources:** sysmisc-3 (bug, medium, corrected), sysmisc-14 (rethink R1: one clock).
- **Kernels:** big, small, and kern_dos.
- **Rank:** S medium, L high (a 5150 without a clock card: DOS TIME reads time since power-on and DATE 4 July 2026; on an AT, POST time plus uptime), B low.
- **Mechanism:** clock.inc promises "one date + time of day for the whole system" (4-5); kerndos takes the time from 40:6C and the date from int 1Ah AH=04h (apps/dos/dos.asm:13878-13906, 14098-14135); cp_flush_close_x writes only the RTC (ctrl.inc:5485-5496); nothing in kernel/ writes 46Ch outside mega.inc's PM tick. WIN16 is unaffected (it asks OSAPI_CLK_GET).
- **Fix:** in cp_flush_close_x, when [clk_dirty], set the BIOS count with int 1Ah AH=01h (every PC BIOS, the 5150's included) from h:m:s and clear 40:70; kerndos asks OSAPI_CLK_GET for the date, as WIN16 does. R1 (derive the time of day from 40:6C, sysmisc-14) fixes it by construction.
- **Bytes:** +~40..+60 CTRL.DRV, 0 res; R1 is -10..+10 res (ESTIMATED). **ABI:** none. **Confidence:** corrected (the byte figure).
- **Rows:** no row asserts DOS TIME/DATE after a panel Set; kdreturnset (soak) is the closest.

### BR-27 kern_big: fm_reloc does not patch the window being launched

- **Sources:** files-2 (bug, medium, corrected).
- **Kernels:** big.
- **Rank:** S medium, L low (a compaction inside a Disk window's or the chooser's first mount), B high (the listing is copied into a freed or foreign segment; FS_VSEG stays stale for the window's life; DSK_BIGDIR regrows the stale segment).
- **Mechanism:** app_launch runs KD_INIT before publishing I_STATE = 1 (instance.inc:1600-1610); fm_kinit_x claims the cache MOVABLE and mounts (files.inc:1297-1319, 681-683), whose claims reach the compactor; fm_reloc patches only I_STATE = 1 KIND_FILES records (9898-9922), while [fm_vseg] and [dsk_dseg] move; fmv_store then sees FS_VSEG != [dsk_dseg] and copies (1216).
- **Fix:** walk the FM_NSLOT fm_pool blocks and patch any FS_VSEG equal to the moved base, whoever owns the block (fm_kinit_x zeroes FS_VSEG on reuse).
- **Bytes:** ~-12 .text res kern_big. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** fmcommit, fdlgchoose, pkgmove-* (soak).

### BR-28 Content-click, right-click and W_ONDRAG read the handler before gfx_lock and never re-test liveness

- **Sources:** ui-2 (hazard, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L low-medium (a click landing while the window's worker reaches inst_task_die), B high (a far call through a freed W_SEG; a #GP on kern_mega).
- **Mechanism:** SPEC 13.9.2 fixed the timer pass by re-testing W_FLAGS under the lock (ui.inc:823-831). .content_front reads W_ONCLICK before gfx_lock (579, 592); ui_rdown reads W_ONRC before its lock (1569, 1578); ui_arm_trk tests and reads W_ONDRAG before it (1343-1356). inst_task_die locks, unwinds, unlocks, then frees the region (instance.inc:1921-1946); wm_pkgcall has no liveness check (wm.inc:396-460).
- **Fix:** `test byte [si+W_FLAGS],1 / jz .out` at ui_bill's head (every caller holds the lock; wm_destroy writes W_FLAGS = 0); drop .tp_locked's identical test and keep its W_ONTIMER re-read. Add a clickrace row on timerrace's model. ctx-1 names this race a consequence of teardown on the worker (BR-4); keep the re-test after BR-4 lands, because SPEC 20.6 rule 7 is not enforced. If flag-day R14 lands, the re-test belongs in its class-dispatch helper (the flagday fit lens).
- **Bytes:** net 0 res kern_big, +5 kern_small. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** timerrace (soak) is the model; no click row.

### BR-29 The entry proc's returned BX is not validated

- **Sources:** loader-4 (bug, medium, confirmed); the divide it reaches is BR-8.
- **Kernels:** all.
- **Rank:** S medium, L low (an entry proc returning CF=0 with a bad BX), B high (BX = 0 sends wm_ptr2idx's div to INT 0: dead machine on IBM ROMs, a hang on a 286 running kern_big; on GLaBIOS the record steals the last window's wm_owner).
- **Mechanism:** on CF=0 step 9 passes BX straight to the WF_DESK test, inst_bind_win and wm_show (loader.inc:1460, 1468-1469, 1495). SPEC 21 step 9's "sanity-checked exactly as before" (SPEC.md:50592) describes the retired abort path; inst_wchk needs an owner, which an unpublished record lacks.
- **Fix:** at .reg check BX - wm_wins < MAX_WIN*WIN_SIZE and [bx+W_SEG] == [ld_base] (the stamp .sweep already trusts), otherwise go to .abort. Check the W_SEG compare against kern_mega's selector stamps and the re-home arm's new [ld_base] first.
- **Not taken: packages are trusted (owner, 2026-11-08)** - one of the six package-argument fences; an APICHK-style knob at most, and its bytes leave batch D. SPEC 21 step 9's stale "sanity-checked exactly as before" is still corrected (0 bytes).
- **Bytes:** 0 retail (owner). In a knob: ~+8 .cold (W_SEG only) to ~+18 (with the range test), ESTIMATED. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-30 OSAPI_PKG_START's "no nested start" and "UI task only" are prose

- **Sources:** loader-5 (hazard, medium, confirmed).
- **Kernels:** all.
- **Rank:** S medium, L low, B medium-high (a nested start from an entry proc overwrites [ld_rec]/[ld_base]/[ld_need]/[ld_lname]; the outer launch then publishes I_SPTR = 0 on an already-published record and its region is owned by an unpublished slot; a worker caller runs the loader concurrently with the UI task).
- **Mechanism:** the image arm and the no-document by-name arm re-enter the pipeline (loader.inc:1851, 1990-1991); only the document arm tests [ld_base] and posts (2048-2049); the nested step 9 zeroes [ld_base] (1497); api_rn checks no [sch_cur] (kernel.asm:4747-4765). SPEC 21.5.4 forbids it.
- **Fix:** `cmp byte [sch_cur],0 / jne .bad` at the slot's head; `cmp word [ld_base],0 / jne .bad` on the image and plain by-name arms; the document arm keeps its post.
- **Not taken: packages are trusted (owner, 2026-11-08)** - one of the six package-argument fences; SPEC 21.5.4's prose rule stands, an APICHK-style knob at most, and its bytes leave batch D.
- **Bytes:** 0 retail (owner). In a knob: ~+15..+21 .cold res (ESTIMATED). **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-31 A driver can be unloaded while another task is inside it

- **Sources:** drivers-2 (hazard, medium, confirmed).
- **Kernels:** big (OS88_DRIVERS).
- **Rank:** S medium, L low (a worker pre-empted inside a socket or stream verb while the user unticks the driver), B high (DETACH and the free run under the worker's CS; a far call through segment 0).
- **Mechanism:** drv_pkg_call_x is documented worker-safe and takes no lock (driver.inc:6619-6631); stream verbs are any-task (os88api.inc:567-579); drv_svc_call_x tests [drv_fseg] and then loads it with no IF=0 window between (2537-2543). The nest frames already count other tasks' in-calls in wm_tcnt (wm.inc:11737-11771), but drv_unload_x looks only at drv_wcnt (driver.inc:4154-4192).
- **Fix:** before DETACH wait, bounded, for [wm_tcnt] == 0 and refuse with DRVE_BUSY if it does not drain. A per-image entry count is the exact form (~15 bytes a site per HEAP-UNPIN-PLAN).
- **Bytes:** +~12 .cold res (coarse form). **ABI:** none. **Confidence:** confirmed (a race, not reproduced).
- **Rows:** no row.

### BR-32 OSAPI_DRV_CLASSK reports a driver in the HMA as not loaded

- **Sources:** drivers-3 (bug, medium, confirmed); gkm-1 (hazard, medium, corrected: a fix made on kern_mega's path left the shipped kernel with the defect).
- **Kernels:** big, emu (kern_mega already fixed this).
- **Rank:** S medium, L medium (the ROM_COLD default plus COLDHMA.SYS on any 286+ moves SOUND/HDD/NET into the HMA), B medium (INSTALL's in_todrive reads CTRL.DRV off the floppy for a driver already up; DOS's Memory page greys Sound or Hard-drive).
- **Mechanism:** %1_hma zeroes DRVR_PARA (driver.inc:3941); the non-mega slot derives "loaded" from a KB figure, `cmp ax,1` (2216), and an HMA driver with no heap claim sums to 0KB; kern_mega counts rows (2206-2214, KERN-MEGA-PLAN 14.88).
- **Fix:** the row count (`inc cx / cmp cx,1`) on every OS88_DRIVERS build; then correct SPEC 51.12.3's predicate sentences and the slot header. Add gkm-1's rule to KERN-MEGA-PLAN's checklist: a fix under %ifdef KERN_MEGA says why its cause is not in shared code, or it lands unconditionally in the same commit (neither this nor BR-38 is in MEGA-RELEASE-PLAN 4.2's list).
- **Bytes:** +5 .cold res (push/xor/inc/pop cx and `cmp cx,1` for `cmp ax,1`, gkm-1's verifier; kern_big and kern_emu). **ABI:** none (the answer becomes what the contract already says). **Confidence:** confirmed.
- **Rows:** hmadrv, hma286, hmapool (soak).

### BR-33 snd_release_inst calls DSV_RELINST at IF=0 into code that makes kernel calls

- **Sources:** drivers-4 (hazard, medium, confirmed).
- **Kernels:** big (SOUND.DRV).
- **Rank:** S medium, L medium (any teardown that held a stream; fsx's bracket entry calls it per live instance), B medium (OSAPI_MEM_MOVABLE and OSAPI_MEM_FREE at IF=0; up to several ms on a slow high-speed DSP reset, past a serial mouse byte).
- **Mechanism:** snd.inc:478-497 opens pushf/cli and calls drv_svc_call(DSV_RELINST) inside; SOUND.DRV's chain reaches sbl_unpin and sbl_pool_put, whose own comment says "Outside every cli window: both are kernel calls" (drivers/sound/sb.inc:1897-1955, 1966, 2597-2622).
- **Fix:** move the OS88_DRIVERS block above the pushf/cli (drv_svc_call already refuses when nothing is published); only the speaker half needs the window.
- **Bytes:** 0 (reorder). **ABI:** none. **Confidence:** confirmed.
- **Rows:** sndkeep, sndonce, sndmove (soak).

### BR-34 Nothing owns the IRQ vectors or the IMR

- **Sources:** drivers-6 (hazard, medium, confirmed), isr-6 (hazard, low, corrected), isr-17 (consistency, low), isr-13 (rethink: one owner for IRQ lines, IVT and IMR).
- **Kernels:** big (drivers), both (IMR sites).
- **Rank:** S medium, L low, B high (a driver image freed with a vector still naming it: the next claim in that memory runs as an ISR; a task switch inside an IMR read-modify-write writes back a stale mask).
- **Mechanism:** drv_release frees the image with no check of IRQ vectors 08h-0Fh/70h-77h (driver.inc:4232-4351); the attach/detach contract is trusted (drivers/os88drv.inc:44-53); drv_hi_ivt is the 16-vector scan's existing shape (4410-4455). IMR RMWs at IF=1 on task context in mou_lockon, mou_p2_off, mouse_unhook and SOUND.DRV's sbl_hook/sbl_cell (mouse.inc:1253-1255, 2194-2196, 1417-1419; sb.inc:3211-3213, 3235-3237); DOS.O88 exchanges vectors but not the IMR. SOUND.DRV leaves the master PIC reading its ISR (sb.inc:937-942, 3294-3298), which a DOS program reading port 20h for the IRR then misreads.
- **Fix now:** after DETACH or a refused ATTACH scan the 16 vectors for the image segment - **walking `mem_rr_ivt`'s existing list** (memory.inc:3487-3507: IRQ0-7, IRQ8-15 and int 33h, which the region-reloc walk already tests against a block) rather than a second hand-written list, which probably costs less than the +~25 below (follow-up, 2026-11-08); if any matches keep the image claimed (a leak, not a crash) and bank a code. Wrap the four IMR RMWs in pushf/cli ... popf. Re-select OCW3 0Ah at both SB sites. **Flag day:** an OSAPI_DRV_IRQ set/unset cell with an IMR shadow, so the kernel owns the entries, refuses a line already held and force-unhooks at release (isr-13; ABI-BREAK-LIST L3).
- **Bytes:** +~25 .cold; +6..+9 .text res (IMR, both kernels); +6 and +8 SOUND.DRV; the cell ~+60..+100 res at a flag day (isr-13 estimate). **ABI:** none now; flag day for the cell. **Confidence:** confirmed / corrected.
- **Rows:** sndkeep (soak); no row tests a vector after release.

### BR-35 MEM_OWNER_MAX caps the kernel's own shared tags

- **Sources:** mem-1 (bug, medium, confirmed), hm-4 (hazard, low, corrected). BR-209 (xm-6) is the same shared-0xFF-owner shape in XMEM.DRV's block table.
- **Kernels:** big (kern_mega's cap is 12).
- **Rank:** S medium, L low (eight conventional driver images at once: XMEM.DRV plus seven, or no HMA), B medium (the ninth is refused as "no heap" over a free heap).
- **Mechanism:** mem_claim_1 counts every live record whose MC_OWN equals BX and refuses at 8 whatever BX is (memory.inc:57-74, 1093-1112); every driver image and every DRVC_OVL image is claimed under MEM_K_DRV (driver.inc:3513 and five more sites); DRV_MAX = 9 + DRV_VX + DRV_PX (80). Modules are MEM_K_MOD, a separate eight.
- **Fix:** skip the cap when BH = 0xFF. Add a gate that loads every driver on an AT profile, or an assembly-time check that the kernel tags' maxima fit MEM_MAX.
- **Bytes:** +5 .cold res. **ABI:** none. **Confidence:** confirmed / corrected.
- **Rows:** drvmem (fast), heapdrv (soak).

### BR-36 osapi_mouse_feed is reached from an IRQ handler but swaps SP and not SS

- **Sources:** isr-2 (hazard, medium, corrected); the family ctx-5 generalises (BR-85).
- **Kernels:** big (OS88_DRIVERS: USBMOUSE.DRV's PicoMEM path).
- **Rank:** S medium, L medium (the card's IRQ landing on a ROM service's, an option ROM's or a DOS program's stack), B high (pushes at foreignSS:mou_pstack; evq_push and the cursor row table write through SS into that segment: a lost event, a garbage arrow, corrupted foreign memory).
- **Mechanism:** the feed swaps SP onto mou_pstack only, inside its cli (mouse.inc:4016-4041), while its caller um_pm33 runs inside the card's IRQ handler (drivers/usbmouse/usbmouse.asm:417-426); the body reaches .lowbss through SS (395-422) and the ring is addressed through SS (events.inc:100-128). The header's "not an ISR" and "MOUPRIV_ENTER tests SS first" are stale.
- **Fix:** bank and load SS as MOUPRIV_ENTER does (`mov [mou_pss],ss / mov ss,[cs:mou_lowss]`, restored on exit) inside the existing cli; rewrite the header.
- **Bytes:** ~+13 .text res. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** usbmouse (soak).

### BR-37 The batch bracket's "UI is frozen" premise is not enforced at the door packages can call

- **Sources:** dr-2 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L low (no package in the tree calls it), B high (a floppy's banked BPB, signature, FAT window and cache are trusted with no LBA 0 read; a package-opened batch lasts until any gfx_unlock, so a swapped floppy is written with the old FAT).
- **Mechanism:** dsk_batch_begin_x sets the batch unconditionally (disk.inc:940-953; its argument 913-919). It is published as cell 02B0h with a C thunk (kernel.asm:3217-3225; apps/cc/os88thunk.asm:2054); package file I/O holds no mutex (fprog.inc:633-646); vga12.inc:5191 is the one ending store.
- **Fix:** begin does nothing unless [gfx_lock_flag] is set and [gfx_lock_own] == [sch_cur] (fpg_arm's test, fprog.inc:616-620); a refused begin is only slower. Check drivers/hdd/inst.inc's two begins still pass.
- **Not taken: packages are trusted (owner, 2026-11-08)** - one of the six package-argument fences (no package in the tree calls the cell); an APICHK-style knob at most, and its bytes leave batch E.
- **Bytes:** 0 retail (owner). In a knob: ~+16 .cold res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-38 kern_big's DOS-handoff refusals leak the extent claim and strand the user in DOS.O88's folder

- **Sources:** hiber-2 (bug, medium, confirmed); gkm-1 (hazard, medium, corrected).
- **Kernels:** big, emu (HIBER.DRV).
- **Rank:** S medium, L medium (.b4: DOS.O88 on a volume int 13h cannot reach), B medium (a MEM_K_HIB claim of up to 8KB leaked; the user left in another folder).
- **Mechanism:** HBM_BADV is .badv only on kern_mega; elsewhere .b2-.b6 go to .bad, which neither frees [hb_xseg] nor calls hbm_vbk (hiber.inc:3395-3399, 3454-3469, 2876, 2923-2928). gkm-1's verifier: .badv and its siblings sit inside the %ifdef KERN_MEGA opened at .b6 (hiber.inc:3424-3466), not inside OS88_IMG, so the fix assembles .badv - its hb_xseg free and hbm_vbk call - on kern_big, not only re-points HBM_BADV.
- **Fix:** assemble .badv on every kernel (nothing in it is mega-specific) and make HBM_BADV .badv unconditionally.
- **Bytes:** +20 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernate, hibernatem (soak).

### BR-39 One static volume bank, and it nests

- **Sources:** hiber-3 (bug, medium, confirmed), drivers-7 (weakness, low).
- **Kernels:** big, mega.
- **Rank:** S medium, L medium (a refused hibernate - serial leased, write error, HMA copy refused - after any hardware driver was swept), B low-medium (the user's folder becomes the root of [hb_vol]).
- **Mechanism:** hbm_hibfail calls hbm_reload and then hbm_vbk (hiber.inc:1982-1994; kern_mega's .b8s 3445-3449); hbm_reload's drv_load_at banks into the same single pair (driver.inc:3446; drvvol.inc:100-113, 135-182), which deskmod.inc:79-100 already saves by hand around exactly this nesting.
- **Fix:** hbm_vbk before hbm_reload in both places (0 bytes). Structurally, the bank pushes the pair onto the caller's stack and the back pops it (the kentc idiom), and deskmod's workaround goes.
- **Bytes:** 0; structural ~0 res and ~-14 in deskmod (unpriced). **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernatedrv (soak).

### BR-40 EMS pages held across a hibernate are lost and nothing refuses

- **Sources:** hiber-4 (bug, medium, confirmed).
- **Kernels:** big (HIBER.DRV).
- **Rank:** S medium, L medium (the browser, Video, VBattle and Redline hold EMS pages), B medium-high (the resumed session's pages name nothing).
- **Mechanism:** hbm_sweep keeps DRVC_EMS (right for a suspend; hiber.inc:2233-2236), SPEC 107.6 already says a holder across a hibernate is unsupported, but hb_ok greys only on XMS blocks (605-609) and hbm_arm checks room and transport only (1712-1782): SPEC 87.7's "refused rather than guessed at" is broken.
- **Fix:** in hbm_arm ask DRVC_EMS's EMSV_CAPS and refuse with a toast ("EMS pages in use") when any page is allocated.
- **Bytes:** +30 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** ems, hibernate (soak).

### BR-41 The Save box commits names dskw_name83 refuses

- **Sources:** files-3 (bug, medium, confirmed).
- **Kernels:** big (.cold), small (FDLG.DRV).
- **Rank:** S medium, L high (anyone typing a nine-letter name), B low-medium (the application's write fails FERR_NAME after the dialog has gone).
- **Mechanism:** fm_editkey bounds the buffer at 12 and filters characters but does not enforce the 8-character stem or 3-character extension and allows a trailing dot (files.inc:6686-6729); fdlg_save checks only FS_MOK and a non-empty box (fdlg.inc:968-980, 1008-1010). SPEC 38.5 claims the box accepts exactly what dskw_name83 stores (SPEC.md:75569).
- **Fix:** in fdlg_save after the empty test, `mov si, fdlg_name / FDX dskw_name83n / jc fdlg_beep`; optionally enforce 8/3 per keystroke in fm_editkey for every naming prompt (+~14).
- **Bytes:** +8 .cold kern_big; +8 FDLG.DRV (0 res) kern_small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** fdlgchoose, fdlgup, fdlgsmall (soak).

### BR-42 The Sticky App library can be freed under a widget that has bound it

- **Sources:** shell-1 (hazard, medium, confirmed).
- **Kernels:** big (DESK.DRV).
- **Rank:** S medium, L low (any MAKE refusal; every precondition holds for a correct package today), B high (the next `call far [stik_le+...]` jumps into a freed claim).
- **Mechanism:** DESK.DRV is dropped when no record carries IF_DESK (deskmod.inc:118-134), and step 9 calls desk_drop_x for any launch whose window is not WF_DESK (loader.inc:1458-1466). A STIK_LIB widget has already bound the library into stik_le (apps/os88stikc.inc:152-176), and under STIK_DESKONLY stik_made ignores MAKE's CF (apps/os88stik.inc:962-966). The drop rule uses "has a desk window" as a proxy for "holds pointers into the library"; the banner and SPEC 110.15 still say "never freed under a caller".
- **Fix:** cheapest, the library's stik_made refuses the launch when MAKE refuses. Right model: the LIB verb sets an IF_LIB bit (I_FLAGS bit 3 is free), desk_drop_x tests IF_DESK|IF_LIB, and only the record's free clears it (AmigaOS OpenLibrary/CloseLibrary).
- **Bytes:** (a) +4..+6 DESK.DRV; (b) 0 res (an immediate changes), +6 DESK.DRV. **ABI:** none. **Confidence:** confirmed.
- **Rows:** stik, stikcard (fast); stikmenu, stikboot, stikswap (soak).

### BR-43 OSAPI_RAND's low bits repeat with a short period, and six games read them

- **Sources:** abi-1 (bug, medium, confirmed).
- **Kernels:** all.
- **Rank:** S medium, L high (Tank, DotDel, Tamegram, WireBall and Pixelstein mask the low bits on every draw), B low (Tank's `test al,1` coin strictly alternates; Tamegram's spawns walk a period-8 pattern; one package's SRAND re-seeds every other package's stream).
- **Mechanism:** osapi_rand returns seed = seed*25173 + 13849 mod 2^16 whole (kernel.asm:6599-6607); 25173 is 1 mod 4 and the increment odd, so the low k bits have period 2^k (apps/tank/tkgame.inc:1271, 1275; apps/dotdel/ddgame.inc:1002, 1014, 1177; apps/tamegram/tamegram.asm:1200-1215). The seed is one system-wide word and the SDK says neither thing (os88api.inc:348-349).
- **Fix:** `xchg ah, al` after the store (AL carries bits 8-15; the stored seed is unchanged); update SPEC 20.4's "out AX = seed"; document the shared seed and that a reproducible stream carries its own ~14-byte LCG; re-baseline any test asserting a seeded sequence.
- **Bytes:** +2 .text res, all kernels. **ABI:** SDK text (abilock --update, no bump); outputs change value, not shape. **Confidence:** confirmed.
- **Rows:** no row (tankspawn, soak, may baseline a sequence).

### BR-44 A hostile SYSTEM.CFG 'CH' byte lets the Date/Time page step a .bss byte 254 past clk_sec

- **Sources:** sysmisc-2 (bug, medium, corrected); gfce-6 (weakness, medium, corrected: the general form).
- **Kernels:** big (kern_small reads no SYSTEM.CFG).
- **Rank:** S medium, L low (a crafted or damaged SYSTEM.CFG, then a click on the page), B high (kernel .bss corruption).
- **Mechanism:** the unpack copies mapped bytes raw (driver.inc:4964-4972, 906-907); the page uses clk_h12 as a COUNT (ctrl.inc:4859-4866, 4984-4987), draws and hit-tests an eighth field, and clk_fld_adj(7) steps [clk_sec+0FEh] (5195-5219). osapi_clk_get_x already masks bit 0 for this reason (clock.inc:2343-2344).
- **Fix:** `and byte [clk_h12],1` after the map loop (CFG_BOOT is instantiated for the overlay only). Or gfce-6's general form, which retires the class: a MAX byte per CFG_MAP row (15 rows on kern_big with DOCK_OPT, 8 otherwise) and an unpack that SKIPS the store of an out-of-domain byte, so the boot default stands as driver.inc:883-885 promises - storing 0, as gfce-6 first proposed, is wrong, since 0 is not every key's default (its verifier). sysmisc-2 becomes one row's MAX, and a future count-shaped key is covered the day it is added.
- **Bytes:** +5 .ovl, 0 res, 0 CTRL.DRV; gfce-6's form ~+22 .ovl and ~+22 CTRL.DRV, 0 res. Either form waits on overlay room on kern_big (*Cross-cutting constraints*). **ABI:** none. **Confidence:** corrected (one site, not two).
- **Rows:** cfgtrip (soak).

### BR-45 About on a VESA desktop indexes app_about_vid past its end and letters from a garbage pointer

- **Sources:** ui-1 (bug, medium, confirmed).
- **Kernels:** big, emu, mega (VID_EXT).
- **Rank:** S medium, L medium (any VESA.DRV desktop, then About), B low (garbage on the fourth line).
- **Mechanism:** app_about_vid has 3 rows, 4 with GFX_VGA (apps.inc:57-66), indexed by [vid_kind] with no bound (113-118); VID_VESA = 4 (viddet.inc:110, 118-119) reads 'VG' = 4756h from app_about_v0. SPEC 39.29.4 names this table (SPEC.md:82191); cp_vidnam and fsx_capstab were fixed, this was not.
- **Fix:** a %ifdef VID_EXT row with a geometry-free name ("VESA - 256 colors", since a VESA mode's geometry varies) and a build-time bound, `%if ($ - app_about_vid)/2 != VID_KIND_MAX + 1 / %error`. Or ui-10: compose the line.
- **Bytes:** +18 .text res on big/emu/mega, 0 small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** vesa, vesacp (soak).

### BR-46 fpg_bare banks [wm_clip_n] but not [gfx_hole]

- **Sources:** text-1 (hazard, medium, confirmed).
- **Kernels:** big (DOCK_OPT).
- **Rank:** S medium, L low (a handler that draws through a region after file I/O in the same hold while the hidden dock's strip is open), B medium (the caller's next primitive clips against the hole's table and can paint over windows above it).
- **Mechanism:** fpg_bare zeroes and restores the clip count only (fprog.inc:161-170); with the count at 0, CLIPQ -> gfx_clip_query tail-jumps to gfx_hole_arm, which re-seeds wm_clip_tab (vga12.inc:454-470; dockmod.inc:1542-1562). Every other bank site saves both words (menu.inc:1929-1935, 2038-2039; dockmod.inc:528-534); toast_now also goes through fpg_bare (toast.inc:412).
- **Fix:** under %ifdef DOCK_OPT, `push word [gfx_hole] / and word [gfx_hole],0` before the painter and `pop word [gfx_hole]` after, inside fpg_bare.
- **Bytes:** +13 .text res kern_big. **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** toastfpg, dockmodule (soak).

### BR-47 wm_paint_dmg's empty-rect arm leaks its one-shot arguments into the next pass

- **Sources:** wm-3 (bug, medium, corrected), wm-17 (consistency: one-shot globals with many lifetimes).
- **Kernels:** big, small.
- **Rank:** S medium, L low (all four arming callers pass on-screen rects; reachable after a display removal), B medium (a leaked npro suppresses the next real promotion; a leaked stwin excuses every overlapping window from the next unrelated pass: a missed repaint).
- **Mechanism:** the .chrome arm, taken when the clamped rect is empty, returns before .promote and before wm_dmg_wins' .wdone, the only consumers of wm_dmg_npro and wm_dmg_stwin (wm.inc:5285, 5307-5313, 5468-5481); callers arm them first (wm.inc:3735; ui.inc:2025; desk.inc:980-986; deskmod.inc:330).
- **Fix:** clear both on .chrome (`xor ax,ax / mov [wm_dmg_stwin],ax / mov [wm_dmg_npro],al`), or a flags argument cleared in one place (wm-17).
- **Bytes:** +8 .text res. **ABI:** none. **Confidence:** corrected (dk and su_son are not leaked).
- **Rows:** dmgcull, zonedmg (soak).

### BR-48 wm_ask_size trusts a W_ONSIZE negotiator to keep BX

- **Sources:** wm-4 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L low (the one shipped negotiator, Paint's pt_onsize, keeps BX by choice), B medium (callers write [bx+W_W] through whatever BX came back).
- **Mechanism:** wm_ask_size banks AX, SI and BP only (wm.inc:2465-2481) and its header promises every other register; wm_pkgcall restores only DS/ES; ui_grow and wm_zoom use BX straight after (ui.inc:2243-2245; wm.inc:2934, 2977). SPEC.md:26382 lets a callback clobber BX.
- **Fix:** push/pop bx in wm_ask_size; say in the SDK that a negotiator answers in CX/DX and may clobber what any callback may.
- **Bytes:** +2 .text res. **ABI:** SDK text (no bump). **Confidence:** confirmed.
- **Rows:** paintnogrow, paintnogrow-vga (soak).

### BR-49 wm_snap_ax moves a window at a left dock's band edge onto the strip

- **Sources:** wm-5 (bug, medium, confirmed).
- **Kernels:** big (DOCK_OPT).
- **Rank:** S medium, L medium (anyone with a left dock: wm_fit, ui_drag drops and wm_zoom all snap through it), B low (the border overlaps the live strip, so every later show, raise or dock update pays wm_dock_under's window repaint).
- **Mechanism:** dkx_band sets vid_band_x0 = 32 (dockmod.inc:409-433); wm_snap_ax rounds the CONTENT origin down, 33 -> 32, frame x = 31, the strip's rule column, and special-cases only a negative result (wm.inc:4102-4136). PLAUSIBLE: argued from geometry.
- **Fix:** in the DOCK_OPT arm compare the rounded x with vid_band_x0 instead of 0 and take the next aligned x up when below.
- **Bytes:** +6 .text res kern_big. **ABI:** none. **Confidence:** confirmed.
- **Rows:** dockpos, dockmark (soak).

### BR-50 wm_resize clamps against the union width and ignores the side-dock band

- **Sources:** wm-6 (consistency, medium, confirmed).
- **Kernels:** big.
- **Rank:** S medium, L low (OSAPI_WM_RESIZE from an app; none shown to do it), B low (a primary window straddling the seam of an extended desktop, or over a side strip).
- **Mechanism:** wm_resize_nb clamps width and x against [vid_w], the union on an extended desktop (wm.inc:2508-2512, 2531-2545; viddet.inc:299-307), and never reads vid_band_x0/xe, which wm_fit and wm_zoom use (583-589, 2898-2905).
- **Fix:** clamp with the band words on DOCK_OPT; decide whether the union is ever intended on two displays.
- **Bytes:** +10 .text res kern_big. **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** dockpos (soak).

### BR-51 task_spawn holds IF=0 across the whole 0xCC slice fill

- **Sources:** sched-1 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L high (every launch and every SB stream open spawns), B low (~676 us at IF=0 for a 384-byte slice: about four lost 5.5 kHz speaker pulses, two-thirds of a 9600-baud byte).
- **Mechanism:** the cli at the head (sched.inc:914-915) covers the rep stosw of the slice (983-989); the header and SPEC 8 still price the window as "~30 stores with no I/O" (882-883; SPEC.md:12446).
- **Fix:** reserve the slot under the cli with T_STATE = 3 (readers outside sched.inc compare with 0 or 2, so 3 reads as occupied and not sleeping), restore the caller's IF to fill and build the frame, cli again to raise sch_top and publish. Check the Task Manager's SS_TSTATE snapshot first. Fix the two "~30 stores" sentences regardless.
- **Bytes:** +8 .text res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row (stkclass, fast, checks sizes only).

### BR-52 Blocks an ISR uses move at IF=1 unless they are driver images

- **Sources:** mem-7 (hazard, medium, confirmed).
- **Kernels:** big (OS88_COMPACT).
- **Rank:** S medium, L low (held today by coincidence: DOS's int 08h hook appears to live only inside a pinning bracket), B high (a tick mid-copy runs the old copy, and its data writes are lost once the vector is patched).
- **Mechanism:** SPEC 66.3 bounds ISRs by an MC_DMA pin that no longer exists (memory.inc:1987-2000); only MEM_K_DRV moves at IF=0 (2974-2976); mem_region_reloc's IVT rows patch vectors naming ANY moved block (3474-3483); DOS hooks int 08h with its CS (apps/dos/dos.asm:11445-11446) and its region is movable (docs/HEAP-CLAIMS.md:329).
- **Fix:** move at IF=0 whenever the block's base appears in any of mem_rr_ivt's 17 segment words (subsumes the driver-image test); state an ISR author rule in SPEC 66.3.
- **Bytes:** +12 .cold res. **ABI:** none. **Confidence:** confirmed (latent).
- **Rows:** pkgmove-* (soak), dosmem (soak).

### BR-53 A doomed claim sheds every cache first, and a table-full refusal compacts before it sheds

- **Sources:** mem-3 (weakness, medium, confirmed), hm-8 (perf, low, confirmed).
- **Kernels:** big.
- **Rank:** S medium, L medium (compress.inc's cmz_wclaim probes largest-first and halves on refusal, so the first refusal has already shed everything), B medium (MEM_P_DIRW and the FAT windows thrown away - seconds of re-reads on a floppy; workers parked once per shed).
- **Mechanism:** mem_claim_x loops claim -> compact -> shed one -> retry until nothing is left to shed (memory.inc:952-989), with no total-size precheck; mem_compact clears [mem_parked] at its end so each round may park again (4095-4106, 4174). A full table refuses at .fail0 (1103-1105), and mem_compact, which can never create a record, runs before the shed that would free one.
- **Fix:** before the first compaction compare the request with mem_avail_lvl_p's exact TOTAL at the claimant's rank (never a false refusal); cmz_wclaim sizes its first try from mem_avail; one park per mem_claim_x; report "no record" distinctly (DX = 1) and go straight to mem_shed_one.
- **Bytes:** +14 .cold (precheck), +6 (one park), +6 (no-record path). **ABI:** none. **Confidence:** confirmed.
- **Rows:** regrowshed, msegnomem, lzcomp (soak).

### BR-54 O(MEM_MAX^2) planning walks run with interrupts off

- **Sources:** mem-8 (perf, medium, confirmed), hm-5 (perf, low, confirmed); ctx-4 (BR-178).
- **Kernels:** big (the plan is the compaction model).
- **Rank:** S medium, L medium (packages size themselves with OSAPI_MEM_AVAIL; the Task Manager polls it), B medium (predicted ~8-20 ms at IF=0 per call on a busy table, against a 7.5 ms serial byte).
- **Mechanism:** mem_avail_lvl_p runs the whole model inside pushf/cli (memory.inc:4927-5023), as does osapi_mem_compact_x's what-if (5753-5772); mem_cp_next is a full table scan per step (2680-2720) and mem_can_move far-calls per record (1975-2148). Nothing at interrupt level touches mem_tab. The figures are arithmetic, not measured.
- **Fix:** measure first (MartyPC cycles across one OSAPI_MEM_AVAIL on a 20-claim heap); if over ~5 ms, guard both walks with [sch_lock] instead of cli; keep cli for mem_claim_1's short scan-and-publish. mem-14 removes the square.
- **Bytes:** +0..+6 .cold res. **ABI:** none. **Confidence:** confirmed (predicted).
- **Rows:** memp, heapmap (soak).

### BR-55 PS/2 MCGA is classed VID_VGA

- **Sources:** video-3 (bug, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (PS/2 Models 25 and 30 are 8086 machines in the target class), B medium (no mode 12h, no GC or Sequencer: the planar renderer draws garbage).
- **Mechanism:** vid_detect tests only AL == 1Ah after int 10h AX=1A00h (viddet.inc:554-557); MCGA answers BL = 0Ah..0Ch, and the header and SPEC 39.1 even name it.
- **Fix:** after `cmp al,1Ah`, send BL 0Ah..0Ch to .equip (it comes up VID_CGA in mode 6, which MCGA emulates); every other BL stays on .vga as today. NOT the reviewer's wider diversion, which would send a VGA+Hercules machine (BL=01h) to VID_HERC.
- **Bytes:** +8..+12 .boot2, 0 res. **ABI:** none. **Confidence:** corrected (the diversion narrowed).
- **Rows:** no row.

### BR-56 A 64KB EGA is driven planar although int 10h AH=12h already says 64KB

- **Sources:** video-4 (weakness, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (base EGA cards shipped with 64KB), B medium (16 colours collapse toward black; mode 6 would be perfect).
- **Mechanism:** after `cmp bl,10h / je .equip` the code tests only BH (viddet.inc:558-562); BL = 0 means 64KB. SPEC 39.1's third scope cut accepts the collapse without a reason against the test (SPEC.md:76083).
- **Fix:** `or bl,bl / jz .equip`; delete the scope cut.
- **Bytes:** +4 .boot2, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row (the EGA game rows run 128KB+ cards).

### BR-57 fsx_page on Hercules writes the page bit and then waits, so the flip frame tears

- **Sources:** video-5 (bug, medium, confirmed; PLAUSIBLE).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (every WireBall flip on Hercules), B low (one torn frame per flip).
- **Mechanism:** Mode X's start address latches at frame start, so write-then-wait is right there; Hercules 3B8h bit 7 is the fetch's address MSB and acts immediately. fsx_page writes 3BFh/3B8h and then tail-jumps into the retrace wait (fsx.inc:1054-1061); the header, SPEC 53.10 and the SDK say it "is latched at the next vertical retrace".
- **Fix:** on the Hercules arm run the two retrace phases first, then the two outs; the "on glass when it returns" contract still holds. Verify with m.flicker() across a WireBall flip, or on an 86Box HGC, before changing the "latched" wording.
- **Bytes:** +8 res, both. **ABI:** none. **Confidence:** confirmed (the hardware half PLAUSIBLE).
- **Rows:** wireballvideo, dispfsxherc (soak).

### BR-58 The planar blank (AC 12h) is used for VID_VESA

- **Sources:** video-6 (hazard, medium, confirmed; PLAUSIBLE).
- **Kernels:** big (VID_EXT).
- **Rank:** S medium, L medium, B low (the idle blanker and vid_blank may do nothing at 8bpp on many SVGAs and at 16bpp behind a HiColor DAC; QEMU applies AR 12h only in its 4/2bpp line drawers).
- **Mechanism:** the dec/cmp map makes kind 4 planar to vid_bk (vidsel.inc:949-954, 1254-1279); SR01 bit 5 (Screen Off), which works on every VGA-class card, was retired as "an identical result" (1325-1331), true only in 16-colour modes; SPEC 120.5's row was never measured.
- **Fix:** SR01 bit 5 by read-modify-write for kinds VGA and VESA; AC 12h for the EGA alone (its SR01 is write-only with no Screen Off bit). Alternatively a VX_BLANK driver selector (VBE 4F10h). Test under QEMU at 8 and 16bpp first.
- **Bytes:** +12..+14 res kern_big (or +6 res plus driver bytes). **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** vesa, saver, savervga (soak).

### BR-59 A lazy part is re-fetched by 8.3 name with no check that it is still the file launched

- **Sources:** hm-7 (hazard, medium, confirmed). BR-191 is the same include's other defect (reads sized in clusters).
- **Kernels:** package side (apps/os88partsbody.inc), every kernel.
- **Rank:** S medium, L low (a same-named file of a different build: an update through The Wire, FTPD or a host copy while the package runs; most swaps give a not-found refusal instead), B high (a code part from the wrong build is far-called).
- **Mechanism:** op_fetch reads through op_read with op_name (os88partsbody.inc:789-794) and checks only that enough bytes arrived (829-831); the kernel loader guards the same case by re-checking the header (loader.inc:1091-1093).
- **Fix:** op_load banks the file's 32-bit size and first cluster; op_fetch compares them with one stat before trusting the read, refusing with a "Disk changed" toast.
- **Bytes:** +20 per parts package image, 0 kernel. **ABI:** none (shared source; every parts package rebuilds). **Confidence:** confirmed.
- **Rows:** no row.

### BR-60 Subdirectories never grow

- **Sources:** dw-12 (weakness, medium, corrected).
- **Kernels:** big (optional on small).
- **Rank:** S medium, L high (a kernel-made folder holds 14 entries on 512-byte clusters - 1.44MB, 1.2MB, the 360KB system disk - and 30 on 1KB clusters), B medium (FERR_DIRFULL; RunCPM's save failed on the glass, SPEC.md:133703-133709).
- **Mechanism:** when a subdirectory's chain ends with no free slot every create answers FERR_DIRFULL (diskw.inc:692-693, 1548-1551); SPEC 19.3.2 records the limit (SPEC.md:43108-43111). mkdir already allocates and zeroes a directory cluster (715-728), and both slot users go through dskw_find's no-slot exit.
- **Fix:** dskw_dirgrow at that exit for a subdirectory: allocate, zero (mkdir's loop factored out), flush, link the walker's last cluster, flush, publish its first sector as [dskw_fsec] with [dskw_fend] = 1. A crash after the link leaves a harmless empty cluster.
- **Bytes:** +90 .cold res kern_big. **ABI:** none. **Confidence:** corrected (cluster sizes). This is the largest single byte cost in the register: an owner's call on whether the defect is worth 90 resident bytes.
- **Rows:** no row.

### BR-61 A failing read-ahead fill runs the full retry ladder per good sector, and a timeout gets the single-sector fallback too

- **Sources:** dr-3 (weakness, medium, confirmed), dr-12 (weakness, low).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a marginal sector at a track's tail, or no disk in the drive), B low-medium (each ladder is seconds on a 5150; every later read on that track refills and pays again).
- **Mechanism:** a miss fills to the track's end through disk_read_x with three attempts and the single-sector fallback (disk.inc:7206-7208, 2025-2073), and after each sector succeeds the next pass recomputes a full run and fails three times again; a failed fill caches nothing, so the direct read and every later read repeat it (7023-7029). A timeout (80h) also takes the single-sector fallback (2057-2066), which cannot help.
- **Fix:** mark a fill distinctly (add/sub 2 on dsk_rah_busy; 4202-4204's LBA 0 swap read keeps its ladder) and fail it at its first error with no reset; for 80h, one attempt at the run and one at a single sector.
- **Bytes:** +10 .cold and +6 .cold res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-62 kern_small's refusal stubs break their register contracts, and the ABI gate never decodes any kernel but kern_big

- **Sources:** abi-2 (bug, medium, confirmed), dw-8 (bug, low, confirmed), abi-3 (hazard, medium, confirmed), gkm-2 (weakness, medium, confirmed: three of the four kern_small-only bugs are a stub or guard written apart from the full body), gi-6 (weakness, low, confirmed). BR-179 (gi-1) is a package-side consequence; BR-14 the guard case.
- **Kernels:** small (stubs); host gate for small/emu/mega.
- **Rank:** S medium, L low (no caller reads AX after FS_OP today; DOS.O88, WRITE_AT's one caller, is not on the small disks), B low (a refusal reads as FERR_OK).
- **Mechanism:** on kern_small cell 04EFh is OSAPI_XREFUSE (mov ax,1 / cmp al,2 / retf; kernel.asm:1303-1307, 4430-4439) while the SDK ends "Every register kept" (os88api.inc:2543-2565). dwf_dskw_write_at's label sits on the stc below `mov ax, FERR_NAME`, so CF=1 returns the caller's offset (diskw.inc:6805-6811). t_api_abi opens only build/kernel.bin (tests/unit/t_api_abi.py:458), its FIXED ledger has no rows for kern_small's two XREFUSE cells, and thirteen per-arm cells (kernel.asm:2820..4440) are held only by a total-length assert. gi-6: READ_SEQ and WRITE_SEQ answer FERR_NAME, but WRITE_AT and VOL_STAT are bare stc with the caller's AX (diskw.inc:6813-6825; kernel.asm:6653-6657), so shared fallbacks cannot tell "no door" from a refusal - od_write on kern_small tries WRITE_AT for every stamped save, is refused, and re-walks root, SYSTEM and APPDATA before the full write (apps/os88data.inc:313-336). OSAPI_XREFUSE takes no argument and serves four cells (04B3h, 04CBh, 04EFh, 04F5h) whose contracts differ (kernel.asm:2595-2600, gkm-2's verifier).
- **Fix:** SDK says "kern_small: CF=1, AX=1 (OSAPI_XREFUSE)", copying VOL_CHECK's wording; move dwf_dskw_write_at above `mov ax, FERR_NAME`; give t_api_abi a --kernel mode for smallk, emuk and megak with per-kernel FIXED rows and a cross-kernel comparison of cell start offsets from the four maps, registered where those kernels are built (full tier). Point osapi_vol_stat at a stub answering FERR_NODISK (gi-6; whether od_write may then skip its walk on FERR_NAME is gi-6's open witness question). Give OSAPI_XREFUSE an argument naming the cell's refusal shape (KEEP = stc/retf, or AX = FERR_x), so a stub cannot be written without naming the contract it answers (gkm-2). And gkm-3: kern_small has 21-28 soak rows against kern_big's ~1,490, and no MartyPC lane for its ported rows (tests/suite.py's _ports drops the marty need, 21775-21777) - a small- prefix that keeps it, seeded with the modules where kern_small has its own bodies (desk, diskw, disk, memory, files, wm, fsx, the FCP/FDLG seam), needs a parser change, not only a list.
- **Bytes:** 0; +4 kern_small for the VOL_STAT stub. **ABI:** SDK text (abilock --update, no bump). **Confidence:** confirmed.
- **Rows:** api-abi (fast), abicompat (soak); the small- lane (new).

### BR-63 A canary reload resets the splash's .text state while the blob thinks the splash is still up

- **Sources:** boot-3 (bug, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L low (only when the canary reloads - exactly the machines BR-1 is about), B low (the loading screen is gone for the rest of the boot; vid_init's mode set wipes the screen).
- **Mechanism:** .rerun reloads from .text sector 0 (boot2.asm:525-529), re-landing spl_live, spl_busy, spl_quiet (viddet.inc:1753-1796) and the vid_* block with image defaults, while [cs:spl_on] in the blob stays 1, so spl_tick skips its first-tick branch for good (splash.inc:241-244, 346-348, 1180-1190).
- **Fix:** test the kernel byte for the first-tick branch (`cmp byte [spl_live],0 / jne .running`), keeping `mov byte [cs:spl_on],1`.
- **Bytes:** -1 .boot2. **ABI:** none. **Confidence:** corrected (a secondary claim refuted and removed).
- **Rows:** splashbar, splashspin (soak).

### BR-64 kern_mega: the planar decoder's deliberate over-read can #GP

- **Sources:** gfx-2 (hazard, medium, confirmed; PLAUSIBLE).
- **Kernels:** mega.
- **Rank:** S medium, L medium (a canvas's last row, or a first row at offset < 3 with odd-phase x), B high (#GP inside the decoder).
- **Mechanism:** vga_blit_prow starts p>>1 bytes early and reads whole 4-byte groups, touching up to 3 bytes either side of a row (vga12.inc:2475-2477, 2574-2576, 2612-2632); SPEC 5.4.1.3 calls that safe because the bytes are "inside the caller's segment" (SPEC.md:4573-4574), which holds in real mode only; a claim's selector limit is the claim's own end (mega.inc:5966-5970; SPEC.md:99294-99296).
- **Fix:** under %ifdef KERN_MEGA one lsl of ES where the planar path is armed (vga12.inc:2046-2068), leaving [gfx_blit_pok] = 0 when SI-3 underflows or the last row's end+3 passes the limit; state the 3-byte slack in OSAPI_GFX_BLIT4's contract either way.
- **Bytes:** +~20 .text kern_mega only (conventional resident, i.e. MEGA_RESIDENT, unless placed in .mega); 0 big/small. **ABI:** SDK text. **Confidence:** confirmed (not run).
- **Rows:** no row (a Bochs row with a 3-byte-slack canvas).

### BR-65 kern_mega: an option-ROM IRQ on an unlent foreign stack is reflected with a selector as its real-mode SS

- **Sources:** mc-2 (hazard, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L low-medium (a package keeping its own stack without SEG_LEND; WIN16 escapes only because it re-lends before every run), B high (the ROM ISR pushes its frame at selector x 16, inside somebody's heap).
- **Mechanism:** mega_svc .none sends a ROM-owned IRQ arriving with SS neither LOW_SEG nor the lent stack to .nest/.low (mega.inc:3290-3296); [mega_rss] becomes the selector (3448-3456) and mega_svc_lo loads it as a segment (3575-3577). SPEC 124.31.2 item 3's "goes on as before" describes the bug SEG_LEND was invented for.
- **Fix:** (a) reflect such IRQs on a small fixed kernel stack in .lowbss; (b) mask the line and replay it at the next LOW_SEG reflection; or at least (c) make SEG_LEND mandatory for any package that keeps its own stack, in SPEC 50.6.11.
- **Bytes:** (a) +20 .mega, +128..+192 .lowbss conventional resident - the owner's call against MEGA_RESIDENT; (c) 0. **ABI:** (c) SDK text. **Confidence:** confirmed.
- **Rows:** no row.

### BR-66 kern_mega: linear LDT walks, and 64KB compaction pieces, with interrupts off

- **Sources:** mc-4 (hazard, medium, confirmed), mc-5 (hazard, medium, corrected; record), gms-10 (perf, low, confirmed: the same walks in the block walkers).
- **Kernels:** mega.
- **Rank:** S medium, L medium (a teardown of a program with N blocks on a 15MB machine; a pool compaction), B medium (PREDICTED ~5 ms per walk on an 8 MHz 286, N walks back to back at IF=0; ~16.4 ms per 64KB piece, two serial bytes at 1200 baud - DERIVED from the Victory 286's field-MEASURED 0.257 ms/KB in KERN-MEGA-PLAN 14.48.3, not bare arithmetic (ul-11's verifier); mx_alloc holds sch_lock across a full pack, ~0.5 s per 2MB, PREDICTED).
- **Mechanism:** mega_ldt_alloc/_alloc2 scan FREE0..mega_ldte under cli (mega.inc:9846-9930); mx_dfree walks the whole LDT per block freed, inside mx_free's and mx_relall's cli (9251-9335, 9413-9478); ldtn = FREE0 + 512 + extKB/16 (10903-10910). gms-10: mega_rgnsubfree, mega_cdfree, mega_mpmovef (under one cli for the whole pass) and both match scans also run to [mega_ldte], 512+ entries - ~2 ms of IF=0 per moved conventional claim on a 6 MHz 286, PREDICTED (mega.inc:14240-14275). mx_cpmove copies and rebases each 64KB piece in one cli step (11055-11190, 11556-11587); mx_alloc raises sch_lock around the pack (8957-9100).
- **Fix:** a high-water mark [mega_ldthi] ends every walk, gms-10's walkers included; mx_dfree out of the outer cli (mega_ldt_free brackets its own write); a rotating start hint; later a free list through free descriptors (the DPMI shape). The compaction half is a model change: a per-claim ISR-reachable flag so every other block moves with IF on and cuts at descriptor boundaries - a package contract, so a flag day.
- **Bytes:** +20..+30 .mega (above 1MB); +20 .mega for the compaction model at the flag day. **ABI:** none now; flag day for the ISR-reachable flag. **Confidence:** confirmed / corrected (the 16 ms is already in KERN-MEGA-PLAN 14.48.3; the sch_lock hold is new; all figures predicted).
- **Rows:** megahigh (full), megahibfull (soak).

### BR-67 kern_mega: global GDT scratch selectors are held by prose, and the compactor rebases SLD but not BACK

- **Sources:** mc-9 (hazard, medium, corrected). BR-187 (gms-4) is the same class from the allocating side.
- **Kernels:** mega.
- **Rank:** S medium, L low (another task's mx_alloc packs a block while an LZ decode is suspended mid-match), B high (lz_match reads through BACK's stale base).
- **Mechanism:** ODD, SLD, BACK, K0-K5, FD, VB, XA/XB and WIN are one descriptor each for the machine, re-aimed per use and correct by comment (mega.inc:4002-4006); mx_cpmove rebases [MEGA_SEL_SLD, SLD+8) only (11173-11179); BACK is aimed by mega_backf/mega_bkaimf from lz_match (4064-4093; lz.inc:519-534). The likelier collision of the same class - another task's decode re-aiming BACK, ODD or SLD - is not covered by widening the rebase; whether LZ decoding is serialized is open.
- **Fix:** rebase the whole K0..SLD range in mx_cpmove (`mov si,MEGA_SEL_K0 / mov cx,MEGA_SEL_SLD+8`; PSCR and CODE in between are based outside the pool). Rethink: one LDT selector per LZ or module stream, allocated with it.
- **Bytes:** 0 .mega now; rethink ~-60 .cold aim helpers against +40 alloc/free. **ABI:** none. **Confidence:** corrected.
- **Rows:** lzload (soak), megahigh (full).

### BR-68 kern_mega: the kernel's own writes through CS are completed silently

- **Sources:** mc-10 (weakness, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium (it hides a kernel bug), L medium, B low-medium (a missed CSDO in .text becomes a permanent #GP and trampoline per store, reported only under MEGATRAP=1).
- **Mechanism:** mega_fault sends every #GP to mega_gpfix/mega_gpw whatever the CS (mega.inc:2418-2427); mega_gpw takes the data segment from CS (4553-4558) and mega_cs2ds maps 0x0060 to KDATA (4818-4823). The trap exists for packages (ABI-BREAK-LIST L3).
- **Fix:** count fixups whose faulting CS is a GDT selector in [mega_kfix], read by the harness as it reads [mega_infault]; a Bochs row asserts it stays 0 across a boot and a standard workload. **Behind a knob (follow-up, 2026-11-08, under the owner's weighing):** the counter is an instrument for a kernel mistake no machine has met, so it belongs in a knob build rather than the shipped kernel - and MEGATRAP=1 already censuses every #GP to port E9 for `tools/megatraps.py` (Makefile:1556-1565), which is the natural home.
- **Bytes:** 0 shipped (knob); +8 .mega and +2 .bss in the knob build. Out of batch H's sums. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megatrap0 (soak).

### BR-69 kern_mega: the park stub's PCHK check can never run

- **Sources:** md-1 (bug, medium, confirmed).
- **Kernels:** mega (HIBER.DRV's park stub).
- **Rank:** S medium, L low (a damaged PKB cell after a warm reset), B high (the restore moves too little or runs past 640KB).
- **Mechanism:** `cmp ax, mega_dwake / je .cells` jumps every PARK past .cells0, which is reached only for mega_hbwake (megapark.inc:67-89); nothing passes mega_hbwake any more (megados.inc:1363-1364; megares.inc:579; hiber.inc:4469); W7 passes the header's own cells, so the first check compares the header with itself, and MPK_C_PKB at 3B8h is not summed (megados.inc:938-951, 2141-2149, 2229-2231).
- **Fix:** route mega_dwake to .cells0; delete the mega_hbwake compare and the NREG==0 arm (a park always has NREG > 0, and MPK_SAVE and the header cells come from the same values, so a normal trip still passes).
- **Bytes:** -12 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megadosp, megadosroom (soak).

### BR-70 kern_mega: the HMA kern_dos's mouse vector leaves its A20 trampoline at the first poll

- **Sources:** md-2 (bug, medium, corrected).
- **Kernels:** mega (KD_HMA, implied by KD_MEGA).
- **Rank:** S medium, L medium (a DOS program that shuts A20: a direct int 15h AH=87h on an IBM-AT ROM, or AX=2400h), B high (the next mouse IRQ goes into wrapped low memory; the re-arm also drains one RBR byte mid-packet).
- **Mechanism:** kd_mou_arm writes CS:kd_mou_isr with CS = FFFFh (kerndos/kdmouse.inc:149-156), kdh_tramp then rewrites it to a LOW_SEG slot (kerndos/kdhma.inc:47-64; kdentry.inc:327, 396), and kd_mou_rearm, on every INT 33h AX=3 and every key poll, compares the vector with CS:kd_mou_isr, fails, and re-arms without a trampoline (kdmouse.inc:347, 391-395).
- **Fix:** on KD_HMA, publish through kdh_vslot in kd_mou_arm and compare against the same answer in kd_mou_rearm (dos_hvec is DOS_MEGA only; call kdh_vslot directly under %ifdef KD_HMA).
- **Bytes:** +8 HMA kern_dos image only. **ABI:** none. **Confidence:** corrected (the dos_hvec route would not assemble everywhere).
- **Rows:** kdmouse, dosmouse (soak, on kern_mega's arm).

### BR-71 kern_mega: A20 openings test once (W7) or 512 times (unpark, streaming resume)

- **Sources:** md-3 (bug, medium, confirmed), md-10 (hazard, low, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L medium (the IBM-AT ROM's int 15h AH=87h leaves A20 shut, so the 8042 path is live on exactly the ATs W7 targets), B high (W7 has already cleared the anchor, so a miss loses the parked session with no message; kd_unpark prints the A20 sentence and int 19h follows).
- **Mechanism:** mega_wrprobe clears the anchor, writes port 92h and 8042 D1h/DFh, waits only for IBF to clear, tests A20 exactly once and on a miss boots normally (megados.inc:2116-2128, 2167-2182, 2235-2242). kdp_a20w and mr_a20 poll 512 times (kerndos/kdpark.inc:135-142; megares.inc:481), kdh_tchk 65,536 (kdhma.inc:203-206, 224).
- **Fix:** poll as kdh_tchk does (`xor cx,cx / loop`) at all three sites.
- **Bytes:** +8 .boot2; -1 HMA kern_dos; -1 HIBER.DRV. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megadosp (soak).

### BR-72 The disk cloner cannot see 8-sector or single-sided disks

- **Sources:** hiber-12 (weakness, medium, confirmed).
- **Kernels:** big (DISKOPS.DRV).
- **Rank:** S medium, L medium (160KB/180KB/320KB DOS 1.x disks without a BPB are the commonest 5150-era disks, the case the module header says the cloner exists for), B low (CERR_IO, or "Not a disk image").
- **Mechanism:** the probe ladder tries 18, 15 and 9 sectors per track with two heads (diskops.inc:601-614, 629-657); Write Img has four double-sided sizes and CL_HDS = 2 (1702, 1730-1734).
- **Fix:** add 8 after 9 in the ladder; on a failed 2-head extent read retry with one head before dropping to 40 cylinders; give clo_imgtab a heads column and rows 320/8/1, 360/9/1, 640/8/2.
- **Bytes:** +30 DISKOPS.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** diskclone, imgcases (soak).

### BR-73 Packages write the window record's geometry, which the SDK says they may not

- **Sources:** abi-7 (weakness, medium, confirmed).
- **Kernels:** package side (Paint, the Sticky library); every kernel.
- **Rank:** S medium, L high (Paint's pt_wfix and the Sticky library do it on every use), B low-medium (derived state W_NATR, W_ZOOMR, W_SUEXT is bypassed).
- **Mechanism:** os88api.inc:3165-3176 says the fields are "readable by contract, not writable"; apps/paint/paint.asm:12831-12870 writes W_W/W_H/W_X/W_Y from a size-changed callback; apps/os88stik.inc:1023, 1712, 2376-2407 hides, writes and shows. The writes exist because OSAPI_WM_RESIZE always repaints and cannot be used from that callback.
- **Fix:** a no-repaint/hidden mode for OSAPI_WM_RESIZE (top bit of CX), or legalise hide-write-show for HIDDEN windows only; a host lint for stores to `[es:..+W_*]` under apps/ with an allow-list.
- **Bytes:** ~+10 .text res for the mode; 0 for the lint. **ABI:** additive (a flag bit, no bump). **Confidence:** confirmed.
- **Rows:** paintnogrow (soak); stik (fast).

### BR-74 checkdocs fails the build on committed review reports

- **Sources:** engsys-1 (bug, high, confirmed).
- **Kernels:** none (the build gate).
- **Rank:** S high, L high on this branch, B medium (`make` stops before building a floppy).
- **Mechanism:** checkdocs is `all`'s first prerequisite (Makefile:2425); six problems sat in this review's own area reports, several of them quoting a bad citation as evidence. docs/README.md says reports and history are not maintained, yet checkdocs fails on them; SITES_HIST exists to work around exactly that contradiction (tools/checkdocs.py:124-152).
- **Fix:** the six lines are fixed as part of assembling the review. Systemic half: problems in docs/reports/ and docs/history/ (and possibly docs/plans/completed/) print as `warn:` lines and fail nowhere else.
- **Bytes:** 0 shipped. **ABI:** none. **Confidence:** confirmed.
- **Rows:** the `make` gate itself; no suite row.

### BR-75 A citation that resolves to the WRONG section passes

- **Sources:** engsys-2 (hazard, high, confirmed).
- **Kernels:** none (documentation; hard rule 8 binds packages).
- **Rank:** S high, L high (already live), B low-medium (six cites of SPEC 46, ArtfulType, mean SPEC 47, the greying standard).
- **Mechanism:** SPEC.md:161, 26453, 26463, 26555, 55757, 76640 cite 46 where 47 is meant (SPEC.md:96478 heads 47, 95185 is in 46); the preamble refuses renumbering because checkdocs cannot catch this class (SPEC.md:20-23), and the class is live without one.
- **Fix:** correct the six; add a heading-drift check comparing SPEC.md's number-to-title map at the merge-base with HEAD, failing a changed pair and listing every citation of the old number.
- **Bytes:** 0. **ABI:** none. **Confidence:** confirmed.
- **Rows:** the checkdocs gate.

### BR-76 Basename collisions silently disable citation checks

- **Sources:** engsys-8 (hazard, medium, corrected).
- **Kernels:** none (tools).
- **Rank:** S medium, L high (29 shared basenames: README.md x27, BRIEF.md x11, SKILL.md x10, LESSONS.md x8), B low (a qualified citation is resolved against the wrong file and skipped as uncheckable).
- **Mechanism:** checkdocs' stems dict keeps the last path (tools/checkdocs.py:204); docs/README.md:74's "no shared basename" rule is ungated; `LESSONS.md 3` in two skills resolves to vga-face's, which numbers no headings.
- **Fix:** resolve a qualifier in the citing file's own directory first, by basename only when unique; fail an ambiguous qualifier naming the candidates.
- **Bytes:** 0. **ABI:** none. **Confidence:** corrected (count).
- **Rows:** the checkdocs gate.

### BR-173 A worker that dies holding the gfx lock deadlocks the desktop

- **Sources:** ctx-2 (hazard, medium, corrected). Lands in the same diff as BR-4 (its step 1 in ctx-1's recipe).
- **Kernels:** big, small (kern_mega already releases the lock in mega_fkill .worker).
- **Rank:** S high, L low (a package breaking SPEC 20.6 rule 4: OSAPI_TASK_ALIVE called while holding the lock), B high (scheduling continues and nothing ever draws again: the session is lost).
- **Mechanism:** inst_task_die calls gfx_lock (instance.inc:1926), and gfx_lock's .retry/.block never compares the owner (vga12.inc:5082-5104), so it spins on its own hold for ever. gfx_lock_own has existed since SPEC 59.4 (vga12.inc:310-318), and mega_fkill .worker already reads it and calls gfx_unlock before inst_task_die (mega.inc:2709-2717). The verifier corrected the kind: SPEC 20.6 rule 4 documents the deadlock as a package rule, so this is a missing kernel defence against a documented package error, and the SPEC text has since been amended to say gfx_lock records its owner but does not consult it.
- **Fix:** at the top of inst_task_die, `mov al,[sch_cur] / cmp al,[gfx_lock_own] / jne .nolk / call gfx_unlock` (0xFF never matches a slot, so no flag test is needed), then delete kern_mega's duplicate. Optionally gfx_lock's held arm compares the owner with sch_cur and branches to a named panel (BR-9's). Under the packages-are-trusted ruling this is the owner's call (*Cross-cutting constraints*). **Decided (owner, 2026-11-08): not taken as retail code** - one of the six package-argument fences; kern_mega keeps its own release in mega_fkill .worker, and the real-mode kernels at most carry the check in an APICHK-style knob. The optional self-take panel is a diagnostic for a failure nobody has met, so it goes behind BR-9's knob (follow-up, under the owner's weighing). Its bytes leave batch C. The SPEC 20.6 rule 4 text stands.
- **Bytes:** 0 retail (owner). In a knob: +12 .text res on kern_big and kern_small (3+4+2+3, the verifier's count; the reviewer's +9 is superseded); on kern_mega +12 .text conventional and about -24 .mega (the 24-byte block it replaces); the optional self-take panel ~+10 .text. ESTIMATED. **ABI:** none. **Confidence:** corrected.
- **Rows:** timerrace, stkpanel (soak). No row calls TASK_ALIVE under the lock; one should, and must hang without the fix.

### BR-174 OSAPI_GFX_LOCK no longer hides the cursor, the SDK says it does, and a raw framebuffer writer cannot make it

- **Sources:** cursor/mc-5 (hazard, medium, confirmed), cursor/mc-9 (weakness, medium, confirmed: the missing instrument).
- **Kernels:** all.
- **Rank:** S medium, L low (a package that writes the framebuffer itself inside the lock, outside FSX; no known live case), B low-medium (its writes land under a lit arrow and the next restore smears them).
- **Mechanism:** gfx_lock .take ends with `mov byte [cur_lazy],1` and no hide (vga12.inc:5083, 5139, 5146, 5262-5268); kernel primitives spend the promise. os88api.inc:220-221, the vga12.inc headers and SPEC 7's first bullet (SPEC.md:10682-10686) still describe a hide. cw_cur_unlazy and cw_cur_lazyrect are kernel-internal far shims, not cells (kernel.asm:7843-7847). SPEC 7.1.4's safety argument is a debug-build snapshot that SPEC 7.1.1 calls "instrumentation, never committed" (SPEC.md:10841-10847); GFXAUDIT counts unlocked draws and collisions but not a cell written under a kept promise, and nothing in tools/ or tests/ reads CURLOG (cur_logput, mouse.inc:4376-4400), though CURLOG_N's comment names readers. The class has recurred (SPEC 7.1.5, the gfx_xor_rect and one-plane arms), each found on the glass.
- **Fix:** now, correct the texts: vga12.inc and SPEC are in the review's doc fixes; os88api.inc's OSAPI_GFX_LOCK/UNLOCK comment is fingerprinted, so it lands with abilock --update and no bump. Record: an appended OSAPI_CUR_EXCLUDE(rect) cell that is cw_cur_lazyrect (INT 33h fn 10h, QuickDraw's ShieldCursor). mc-9: under GFXAUDIT only, recompute (save|white)&~black for each banked byte before a restore and compare it with VRAM, logging a mismatch to CURLOG with the tick and the interrupted CS:IP, on 1bpp; and a soak row that fails on a non-zero count.
- **Bytes:** 0 now; +5..+8 for the appended cell (record); ~+40 in the GFXAUDIT knob build only. **ABI:** SDK text now; additive for the cell. **Confidence:** confirmed.
- **Rows:** no row; a GFXAUDIT soak row (mc-9).

### BR-175 kern_big moves a VGA pointer through the generic rect path at IF=0, erase-then-draw

- **Sources:** cursor/mc-3 (weakness, medium, confirmed); already queued as LAST-DROP-PERF 7.1 row 3 (KHP_VGASAVE). Shares its span with BR-21.
- **Kernels:** kern_big and kern_emu, on VGA/EGA.
- **Rank:** S medium, L high (every pointer packet on a VGA kern_big), B low-medium (about 4.5 ms at IF=0 per packet, before the EOI; overlap bytes show background between the restore and the draw, PREDICTED and never measured with m.flicker; about 20% of the CPU during a sweep, PREDICTED, which SPEC 7.1.2's measured 21.5% supports).
- **Mechanism:** KHP_VGASAVE is defined only under KERN_MEGA (kernel.asm:265-294); on kern_big cur_restoreu/cur_saveu are cur_rect plus vga_restore_vram/vga_save_vram, called from the serial ISR, and cur_move_1's planar arm is restore, then save, then draw (mouse.inc:5618-5641). LAST-DROP-PERF.md:590 MEASURED 21,450 cycles a packet against 15,100 with the knob; PERFORMANCE.md:1355 quotes the 15,100 without naming the kernel.
- **Fix:** take KHP_VGASAVE on kern_big, and one m.flicker() pointer-sweep reading on os8088_xt_vga. Structurally, cursor/mc-R2 (a planar save-under in off-screen VRAM, moved by write-mode-1 latch copies).
- **Bytes:** +122 .vga - resident only on VGA/EGA machines, but billed by KERN_BUDGET, which is a VGA reading (rt-5); -6,350 cycles a packet (MEASURED difference). **ABI:** none. **Confidence:** confirmed.
- **Rows:** the disp* rows on os8088_xt_vga; BR-21's ifoff --mouse leg reads the same span.

### BR-176 os88ui's standard button (and its scroll track) writes caption cells twice

- **Sources:** os88ui/F1 (weakness, medium, confirmed), os88ui/F8 (perf, low, confirmed; record).
- **Kernels:** kern_big and kern_mega (.cold), kern_small (its module copies), and every package copy not built OS88UI_BIMG.
- **Rank:** S low-medium (rule 2: a visible interval between two writes of the same cells), L high (every kernel button draw), B low.
- **Mechanism:** OS88UI_BTN_BODY runs UI_INK, UI_FILL of the interior, UI_PEN, UI_FRAME, a second UI_FRAME for the default ring, UI_WIDTH, then UI_RUN (apps/os88ui.inc:806-911), so every caption cell is written twice with one or two gfx_frame calls (and a pen call) between - against SPEC 13.14.6 rule 1 and the file's own DRAWING RULE 1 (66-72); os88ui_sbthdraw was reordered for this same interval (SPEC 13.10.5.9) and the button never was. SPEC 20.5.1.3.1 makes the fill unconditional but says nothing about its order, so it is not a choice. F8: a full scroll-bar paint greys the track under the thumb before drawing the thumb.
- **Fix:** pen, frame and ring first; measure and centre the label; then the fill and the caption run back to back (the kernel arm banks [gfx_color]'s caption ink before the fill's ink store, and CX/DX across the fill). The interval drops to zero calls, though the cells are still written twice (fill, then an opaque run). Prove it with a fill/run call recorder or m.flicker() on a Disk window header press. F8 is record.
- **Bytes:** +8..+12 per copy - .cold res on kern_big and kern_mega (above 1MB on mega), 0 res on kern_small (its CTRL.DRV and FDLG.DRV copies grow), and each package image; 0 extra calls. **ABI:** none (shared source; every carrier rebuilds). **Confidence:** confirmed.
- **Rows:** btnall, disp* (soak).

### BR-177 os88ovlchk cannot see the KBTNIMG/KGLYPH split it is cited as guarding

- **Sources:** os88ui/F2 (hazard, medium, corrected).
- **Kernels:** the build gate; kern_small is where a miss becomes a wild call.
- **Rank:** S medium, L low (a future edit), B medium (on kern_small a resident near call to os88ui_kbtn would pass make).
- **Mechanism:** (a) OS88UI_BTN_BODY, KBTN_BODY and BHIT_BODY define their routine as a bare `%1:` (apps/os88ui.inc:644, 1281, 6620), while LABEL needs a letter first and TLABEL needs `%1 %+ suffix:` (tools/os88ovlchk.py:164, 189), so os88ui_btn/kbtn/bhit and the fdb_* routines never enter the label map and calls to them go untested. (b) SECT requires `section .\w+` (152), so `section OS88UI_KGLYPH` (os88ui.inc:1330) is filed under EXTRA's .cold (116), and CPXN (kernel/ctrl.inc:267-273) exists only to hide the panel's legal call. (c) UI_HLINE's near `call ct_cw_gfx_hline` has no OS88UI_KFAR arm (os88ui.inc:177-179); the verifier struck UI_GRAY, which is already an absolute far call.
- **Fix:** tooling only: TLABEL accepts a bare `%1:`; resolve `section <macro>` through the includer's %define (or a marker) and retire CPXN; give UI_HLINE a KFAR arm or %error it under OS88UI_KFAR. Break it on purpose: plant a resident `call os88ui_kbtn` under KERN_SMALL and watch make go red.
- **Bytes:** 0 shipped. **ABI:** none. **Confidence:** corrected.
- **Rows:** the make gate (os88ovlchk); no suite row.

### BR-178 IF=0 has no stated bound and no instrument

- **Sources:** ctx-4 (hazard, medium, confirmed); ul-10 (consistency: the rows that would measure it), ul-11 (which figures are predicted). The local cases are BR-12, BR-21, BR-54, BR-66, BR-166, BR-175 and BR-210.
- **Kernels:** all.
- **Rank:** S medium, L high (nothing bounds the next IF=0 window either), B medium (lost serial bytes and lost ticks).
- **Mechanism:** the long IF=0 windows are mem_regrow's path 3 (86 ms per 32KB, PREDICTED from memory.inc:2046's 16K words x ~25 clocks), a driver image move (17-50 ms, deliberate), mem_avail_lvl_p's walk (~10-20 ms, PREDICTED), a kern_mega pool piece (~16.4 ms, derived from a measured rate), the arrow inside mou_isr (2.4 ms CGA, 4.5 ms kern_big VGA, MEASURED in cycles), mega_leds (up to 2 x 65,536 polls) and sch_switch (~0.46 ms). The 8250's one-byte holding register sets the bound: ~8.3 ms at 1,200 baud (the mouse), ~4.2 ms at 2,400 (the WireBall link), ~1 ms at 9,600 (SERIAL.DRV's lease default, SPEC 115); IRQ0 ticks are lost past 55 ms. No suite row measures IF=0 duration: uilat measures lock latency and serlease a lease's line state (tests/suite.py).
- **Fix:** per-class bounds in SPEC 7: XT 8 ms, with SERIAL.DRV at 9,600 on an 8250 documented as sharing the CPU with an ISR-drawn arrow - a 1 ms bound is not reachable while BR-21 stands. An IFLAT=1 knob (private tree) in which sch_isr records max(period - latched count) at entry, with the sub-tick at divisor 2048. A soak row iflat on os8088_5150_cga_gla and os8088_xt_vga - a 32KB regrow, an OSAPI_MEM_AVAIL over 16 claims, continuous mouse motion - asserting the maximum against the bound, paired with serlease at divisor 12; break it by reverting BR-12. ul-10's other unregistered measurements: stkdie (BR-4), xferfreeze (histsys-5), ifoff --regrow and --mouse (BR-12, BR-21), uilat --workers (histsys-4).
- **Bytes:** 0 shipped (knob only). **ABI:** none. **Confidence:** confirmed (the bounds are arithmetic, not measurements).
- **Rows:** none today; iflat and ifoff (new, soak).

### BR-179 os88wseq's documented kern_small path cannot run

- **Sources:** gi-1 (bug, medium, confirmed); BR-62 is the stub it trips on.
- **Kernels:** kern_small (package side: apps/os88wseq.inc and its one user, REDLINE).
- **Rank:** S medium, L low (REDLINE.O88 ships on util360.img and is not in SMALLOMIT, so a kern_small machine can open it; whether REDLINE otherwise runs there is unchecked), B low (REDLINE's Inspect report answers "Report not saved" and no file is made).
- **Mechanism:** os88_wseq_open calls OSAPI_VOL_STAT first (apps/os88wseq.inc:66-104), and on kern_small that is osapi_snd_fm's bare stc/ret (OS88_SNDCARD is KERN_BIG only, kernel.asm:1008-1010; snd.inc:714-717), so AX is still the 0 from the xor at :74, WS_ERR banks 0 and the open answers CF=1 AX=0, which is no FERR code; the header promises the APPEND path there (37-40; apps/redline/inspect.inc:801-806).
- **Fix:** ask OSAPI_FILE_DFREE (BX = sectors per cluster, on both kernels, the same cost) instead, and bank FERR_NODISK when it refuses; later BR-191's grain cell. BR-62's FERR_NODISK stub (gi-6) also gives this open a real code.
- **Bytes:** +2 package bytes per carrier, 0 kernel. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row; a kern_small REDLINE Inspect save would be one (gkm-3's small- lane, BR-62).

### BR-180 kern_big does not own int 15h: a DOS program can AH=87h over the pool and the HMA, or shut A20

- **Sources:** xm-1 (hazard, medium, confirmed).
- **Kernels:** kern_big on a 286+ (DOS.O88); kern_mega's kern_dos already filters.
- **Rank:** S high, L low-medium (a CORRECT pre-XMS program under DOS.O88 is the trigger, which is exactly why HIMEM answers AH=88h with 0), B high (every instance's pool blocks, the RAM disk's store, and .cold and SOUND.DRV in the HMA are writable; on an IBM AT the BIOS's AH=87h also shuts A20 on exit, so the next IRQ0 reaches snd_tick in the HMA with the gate shut - the lock-up xm_bios exists to prevent).
- **Mechanism:** dos_ivtab hooks 20h-2Fh and 33h, not 15h (apps/dos/dos.asm:3256-3300). The only int 15h filter in the tree is kerndos/kdxms.inc's kdx_i15, included under KD_MEGA only; megados.inc:1742 is kern_mega's IVT restore. drivers/xmem/xmem.asm:1037-1050 records the AT's AH=87h closing A20. HMA-PLAN.md:113-119 records only the A20 half, as "the same class as writing kernel memory".
- **Fix:** DOS.O88 hooks int 15h for its programs: AH=88h answers AX=0 CF=0; AH=87h with either base at or above 100000h answers CF=1 AH=03h; E801h, E820h, C7h, 8Ah and AH=24h answer CF=1 AH=86h; everything else chains to the BIOS - the table SPEC 96.15.4 gives kern_mega's kern_dos. Run tests/dostrap/xmsprobe.asm under DOS.O88 on a QEMU 386.
- **Bytes:** +45..+60 DOS.O88 (package), 0 kernel. ESTIMATED. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row; xmsprobe under DOS.O88 on QEMU (a 286+ is on QEMU's list).

### BR-181 XMEM.DRV's tier-2 copy never re-proves A20

- **Sources:** xm-2 (hazard, medium, confirmed).
- **Kernels:** kern_big on a 386+ (XMEM.DRV).
- **Rank:** S high, L low (a gate shut by a third party - BR-180's DOS program, or kern_dos's kd_a20shut), B high (a pool block at 110000h is written at 010000h, inside the conventional heap: silent corruption where COLDHMA.SYS is absent or .cold is in ROM; with .cold in the HMA the machine dies anyway).
- **Mechanism:** xm_copy probes A20 only on the LOADALL arm; `cmp byte [xm_tier],CPU_386 / je .unreal` goes straight to xm_ucopy, and ub_set puts the base on the bus as given (drivers/xmem/xmem.asm:807-849). apps/os88ub.inc:44-47's premise - "the BIOS's block move ... is the only thing in the tree that might close it" - fails once DOS.O88 runs foreign code.
- **Fix:** hoist `call xm_a20_reopen / jc .e1` above the tier branch and delete the LOADALL arm's probe; xm_ucopy reads only [xm_src]/[xm_dst] and BP, and xm_a20_reopen is resident (listing 6A0h, before xm_once at 718h) and clobbers only AX-DX. The LOADALL arm changes from "shut gate -> BIOS path" to "re-open, then LOADALL"; both are correct. Optionally osapi_xmem_walk's HMA door goes through the same reopen.
- **Bytes:** ~0 XMEM.DRV image (net +2 by the verifier, not measured; the image has 7 bytes of slack), +3 if the walk gets it too; ~40 clocks a copy on a 386. **ABI:** none. **Confidence:** confirmed.
- **Rows:** tests/xmcheck.py and tests/xmla.py cover the driver; none shuts A20 under it.

### BR-182 HDV_ARM redirects every rung-1 read, the kernel's own metadata included, and an arm outlives its owner

- **Sources:** hdk-1 (hazard, medium, corrected).
- **Kernels:** kern_big with HDD.DRV on an IDE (rung 1) disk.
- **Rank:** S high, L low (a package arming with a wrong DX, or one that faults between ARM and DISARM; the one caller, the Video Player's vp_harm, passes the right DX), B high (a FAT-window load lands in XMS while dsk_fatw0 names the new range, and the next dskw_flush writes the stale window back as that range's FAT on the hard disk).
- **Mechanism:** hd_xa_arm validates the CPU, CX and A20 but never DX (drivers/hdd/hdxms.inc:72-106); hd_xfer_ide asks hd_xa_read about every rung-1 read (drivers/hdd/hdd.asm:482, 489-492), which includes the FAT window, directory sectors, dsk_secbuf and the cache (kernel/diskw.inc:4955-4982); hd_xa_on is cleared only by ARM and DISARM, with no teardown path. The door's stated safety, "writes are never redirected" (hdxms.inc:34-36), misses that a redirected metadata read becomes a wrong write.
- **Fix:** NOT a `cmp dx,es` fence - the verifier showed it unsound: the buffer is a separate heap claim that may sit below the package (apps/video/video.asm:6501, 7344). For the wrong-DX half, record for the flag day: the kernel tags its user-data DSV_BLK reads with a spare AL bit and the driver redirects only those (a DSV_BLK contract change). For the stale arm, now: the kernel's instance teardown sends the driver a disarm, or an arm expires when the READ_SEQ it brackets returns.
- **Bytes:** a few bytes of kernel plus a DSV verb (unpriced); the tag is a contract change. **ABI:** flag day (DSV_BLK) for the tag; none for the teardown disarm. **Confidence:** corrected (the proposed fence struck).
- **Rows:** no row.

### BR-183 HDD.DRV's rung 0 fails a read the controller ECC-corrected (int 13h status 11h)

- **Sources:** hdk-3 (weakness, medium, confirmed; PLAUSIBLE on hardware).
- **Kernels:** kern_big with HDD.DRV on rung 0 (BIOS); the kernel's own BIOS loop for an un-taken boot partition has the same gap.
- **Rank:** S medium, L medium (a weak MFM sector behind an ST-11M or WD1002 - the owner's 5150 class), B medium (a sector the controller corrects every time is unreadable through HDD.DRV).
- **Mechanism:** rung 0's .attempt loop retries every CF=1 three times with an AH=00h reset between, then fails (drivers/hdd/hdd.asm:336-364); AH=11h is never examined, and kernel/disk.inc has no 11h handling at all (grep finds only int 11h equipment-word hits). DOS-era fixed-disk code accepts 11h.
- **Fix:** on AH=11h accept a one-sector run as done; for a longer run re-issue one sector at a time so the corrected sector is accepted on its own. Raise the same rule for the kernel's BIOS loop with diskread (batch E). Whether an XT controller stops at the corrected sector on a multi-sector command is a field-machine question (docs/FIELD-MACHINES.md).
- **Bytes:** +12 HDD.DRV resident image (+5 for the one-sector-only form); the kernel half unpriced. **ABI:** none. **Confidence:** confirmed (controller behaviour PLAUSIBLE).
- **Rows:** hdboot, hddgeom (soak); none injects 11h.

### BR-184 kern_mega: fsx_rmvck compares MC_OWN against the slot, so it never sees a package's own claims

- **Sources:** gms-1 (bug, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L low (shipped callers pin their listed claims, as os88real.inc:39-43 and SPEC 53.2.3's preceding paragraph require, so the guard is defence in depth and today covers the region alone), B high (a door claim compacts a listed claim and os88real_swap installs a stale real segment inside the bracket).
- **Mechanism:** fsx_rmvck compares MC_OWN with BX = the slot (kernel/fsx.inc:1589-1601), while mem_own records a package's own claims under its calling segment - on kern_mega in PM, the I_SPTR selector (memory.inc:5286, 5917-5960). mega_bown had exactly this defect and was fixed ("matched against the slot alone, a package's own claim was nobody's", mega.inc:13508-13580). SPEC 53.2.3 promises the guard snapshots "every movable claim the CALLER'S INSTANCE owns". [fsx_rseg] for a conventional region is stored (fsx.inc:1543) before fsx_rsvcf/fsx_rdrvf/fsx_rextf claim, so the region moving becomes a needless FSXR_ROOM fallback.
- **Fix:** match MC_OWN against [inst_tab + slot*32 + I_SPTR] as well as the slot, mega_bown's pattern (BP is free under pusha); compute [fsx_rseg] after fsx_rextf, which none of the three reads.
- **Bytes:** +10 .mega, 0 conventional. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-185 kern_mega: mega_rgnsubfree frees unbound windows based in an exiting region above 1MB

- **Sources:** gms-2 (bug, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S high, L low (another instance's SEG_WIN/SEG_WINL window over a high region; SEG_WINL takes any 32-bit linear base, SPEC.md:100792; reach in shipped software not shown - the C64 case of KERN-MEGA-PLAN 14.80 was conventional), B high (the holder's next load becomes mega_gpfix's NULL; in 14.80 freeing such a window jammed the 6510).
- **Mechanism:** mega_rgnsubfree frees every present allocator descriptor based in the region's pool block except the region pair, with its own 24-bit compare and no mega_iswin test (mega.inc:12487-12534); every other block walker has the test (mega_cdin for mega_cdfree and mega_mpmovef, 14604-14640; mx_dfree's `call mega_iswin / jnc .din`, 9455-9460). mega_iswin's rule is that a window is its owner's wherever it is based (14650-14700). Callers loader.inc:254 and memory.inc:4875 reach it through mega_pkghifree.
- **Fix:** walk with mega_cdin (DL:AX = the block base, CX = KB*64 paragraphs), keep the region-pair exemption, delete the private compare.
- **Bytes:** -15..-20 .mega. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megahigh (full) covers the area; no row exits a high package under another's window.

### BR-186 kern_mega: OSAPI_MEM_SUBSEG's private match can hand out a window or a handle descriptor as a SUB

- **Sources:** gms-3 (bug, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L low, B high (when the window's owner re-aims or frees it, the package's SUB moves or dies under it - KERN-MEGA-PLAN 14.128's defect, on another door).
- **Mechanism:** mega_subsegf's "one over that base already" scan compares access byte, base and limit only (mega.inc:12610-12624); mega_wnew allocates windows with access 92h, and WIN16 handle descriptors are 92h with MEGA_HAVL, so both match a SUB's question. OSAPI_SEG's SUB arm uses mega_dfind, which refuses both (13975-14012, the MEGA_HAVL test at 13994 and mega_wslot at 13999).
- **Fix:** replace the scan with `call mega_dfind` (DX:AX, CX and BL are already in place); better, gms-R2: make the cell an adapter over SEG_SUB/SEG_SUBC.
- **Bytes:** -25 .mega. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-187 kern_mega: SEG_SUB/SUBC/TILE/HND/BWIN read a block base and later write a descriptor with no sch_lock

- **Sources:** gms-4 (bug, medium, confirmed). BR-67 is the same class seen from the compactor.
- **Kernels:** mega.
- **Rank:** S high, L low (the caller pre-empted between the read and the write by a task whose allocation compacts), B high (the new descriptor lands at the OLD base and is never rebased, because it did not exist when mx_cpmove or mega_mpmovef ran).
- **Mechanism:** mega_subsegf holds sch_lock so that "no task compacts the pool between reading the block's base and aiming a descriptor at it (mx_alloc's rule)" (mega.inc:12559), but mega_segf's .sub1 does mega_selbase, mega_bclip, mega_dfind and mega_ldt_alloc at IF=1 with no lock (12911-12945), api_rxc takes none (kernel.asm:4611), mega_segtile reads and clips before its cli (13650-13676), and mega_segh and mega_segbw read, bown and wbind unlocked. mega_ldt_alloc's own cli covers only the entry scan.
- **Fix:** inc/dec byte [sch_lock] round the read-to-write window in mega_segf .sub1 (shared by SUB and SUBC), mega_segtile, mega_segh and mega_segbw. Leave SEG_AIM alone: it is rate-hook-legal and writes under its own cli.
- **Bytes:** +24 .mega. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

---

## Register - low

Each row carries the same fields as above in short. "rec" in the bytes column
means the reviewer and verifier agreed on RECORD-only (write it down, do not
spend a byte now). ABI is `none` unless stated.

| BR | defect (sources, verdict) | kernels | mechanism (evidence) | fix | bytes | rows |
|---|---|---|---|---|---|---|
| BR-77 | [gfx_color] after OSAPI_GFX_BLIT4 and OSAPI_FONT_RUN depends on which path drew (gfx-5 confirmed, text-3 confirmed) | all | the run paths store the pen, the decoders and the fast font passes never do (vga12.inc:2343-2344, 3245-3250, 6938 vs softgfx.inc:983-1088, vga12.inc:2540-2692; font.inc:1951-1961, 2228-2236); headers promise "the last colour drawn" (vga12.inc:1742-1743; font.inc:1090-1093) | declare the pen UNSPECIFIED after both in SPEC 5.4, 6.1 and the SDK; or bank AL and restore | 0 (doc, SDK text) or +8 .text each | dispblit, toastbar (soak) |
| BR-78 | Sleep and timer ranges are sign compares: OSAPI_TASK_SLEEP with AX 8001h..FFFFh wakes at the next tick, OSAPI_WM_TIMER with AX >= 32768 fires at once, and a UI-task sleep ends at the next input (histsys-15 corrected, sched-5 corrected, wm-8 confirmed, sched-4 confirmed) | all (WM_TIMER body is KERN_BIG) | task_sleep stores ticks+AX, the wake scan tests a sign (sched.inc:1155-1159, 2362-2370); wm_timer the same (wm.inc:2061-2079; ui.inc:805-812); sch_wake_ui sets T_STATE=1 unconditionally (sched.inc:722); toast_show already clamps (toast.inc:216-224) | state 1..32768 for SLEEP, 0..32767 for TIMER and the UI-task behaviour on the cells and in SPEC 8 and 13.9; or clamp | 0 (SDK text, abilock no bump) or +5 / +6 .text | timerrace, timerbounce (soak) |
| BR-79 | sch_rhook returns IF=1 on its skip path under a sample ISR (sched-2 corrected) | big | sti before the three skip tests, re-cli only on the taken path (sched.inc:595-609, 624-627); the rest of the tick then runs interruptible on sch_chstack | move the `spk_seg` test and sti to after the `add word [sch_rbusy],0x0101`; NOT the reviewer's lower-the-lock-first reorder, which opens a nested-switch window | 0 | trkspkrate, vidspk22, vidspkfast (soak) |
| BR-80 | sch_lock has become a general Forbid: yield self-resumes, task_sleep returns at once marked sleeping, gfx_lock spins, and a task exiting with it raised kills pre-emption for the session (sched-3 confirmed, sched-18 confirmed, ctx-8 corrected) | all | SPEC.md:10687 names two raisers, the tree has a dozen (disk, diskw, memory, xmem, fsx, hiber, mega, megados, snd); sched.inc:1151-1164, 1881-1882, 1189-1211; vga12.inc:5091-5104; every site that must wait drops the lock by hand (memory.inc:4101-4103; disk.inc:2030-2035), and on kern_mega a package fault under it is fatal (mega.inc:2564-2565) | rewrite SPEC 7's paragraph as a nestable pre-emption-off bracket with its holder list and the "nothing inside may yield, sleep or take gfx_lock, or exit" rule - SPEC 7's sch_lock bullet now states the no-wait half (ctx-8's verifier), but unnumbered and unenforced: number it, and add a tripwire in gfx_lock's .block arm (`cmp byte [sch_lock],0 / jne` a panel, off the fast path) or gate it statically (ctx-7); optional task_sleep guard. **Follow-up (2026-11-08):** the Forbid sentence itself is already SPEC 7's ("a nestable count ... nothing that holds it may wait on another task", from `14428309`), so what is missing is the rule NUMBER and the lock ORDER (`gfx_lock`, `sch_lock`, `mem_cp_busy`, the module lock); the two tripwires are diagnostics for a failure no machine has met, so they go behind a knob or become ctx-7's static gate (under the owner's weighing) | 0 (doc); the +7 task_sleep guard and +7 gfx_lock tripwire in a knob only, out of batch F's sums | kbdpreempt (soak) |
| BR-81 | evq_push wakes the UI task before the record is in the ring (sched-6 confirmed) | all | sch_wake_ui before pushf/cli (events.inc:84, 92-93): a task-context caller at IF=1 pre-empted between them loses the wake until the tick | move the call to just after the cli | 0 | evqfull (soak) |
| BR-82 | The two private ISR stacks have no canary and sit back to back; SCH_CHSTK's sizing text has the ROM's EOI order backwards (sched-7 corrected, isr-9 confirmed) | big, small | mou_pstack LOW_SEG:06D6..0756 then sch_chstack at 0756 (build/kernel.map; sched.inc:80-89, 2937; mouse.inc:6624-6635); SPEC.md:13125-13130 and sched.inc:2187-2188 say EOI-then-sti, the IBM listing and SeaBIOS run int 1Ch BEFORE the EOI | SCH_MAGIC at sch_chstack's base checked once per TICK after the chain's `mov sp,bx`, with its own panel; seed in a resident init (the .ovl is 5,119 of 5,120); correct both texts; a STKDIAG field reading before shrinking SCH_CHSTK. **Follow-up (2026-11-08):** the canary and its panel are a diagnostic for a failure no machine has met, so under the owner's weighing they go behind a knob (STKDIAG's own is the natural one); the text corrections and the STKDIAG reading stay | 0 shipped (the canary +9..+12 .text in the knob build only, out of batch F's sums); 0 for the texts | stk0water, stkpanel (soak) |
| BR-83 | STKDIAG's private-stack high waters scan from the wrong end (sched-8 confirmed) | knob only | sd_cwater/sd_mwater scan down for the first 0xE5 (stkdiag.inc:303-389) while sd_water scans up (230-273); the result sizes SCH_CHSTK and MOU_PSTK | scan bottom-up; re-take any field number that came from the panel | 0 shipped | no row |
| BR-84 | The death panel trusts the table it calls suspect (sched-9 confirmed) | big, small | sch_diepanel indexes inst_ptr with no bound and prints I_NAME to a NUL (sched.inc:2692-2721; instance.inc:786-797); draws into an fsx bracket's foreign mode | `cmp al,INST_MAX / jae .name` if touched (take it with BR-9's panel rework, which is now a knob: owner, 2026-11-08; on its own it is +4 in the retail stack-overflow panel, still record) | rec; +4 .text | stkpanel (soak) |
| BR-85 | Every ISR-callable door assumes SS = LOW_SEG, which the kernel itself says is false; OSAPI_WM_WAKE's "ISR-safe" is the visible case (isr-3 confirmed; ctx-5 confirmed, which raises it to MEDIUM and moves it to batch F's core) | all | evq_push writes the ring through SS, `[ss:di+EV_TYPE]` and push ss/pop es (events.inc:80-128; wm.inc:2343-2369); sch_switch's SS test records SeaBIOS hopping stacks for int 16h, which ui_task polls with sch_lock clear, so an IRQ there lands on the ROM's SS (sched.inc:2408-2437); DOS.O88's dvw_irq0 moves to a LOW_SEG slice first (apps/dos/dosw.inc:267-276); latent unless a package IRQ handler calls WAKE, and none was traced | the SDK cell already reads "ISR-safe given SS = LOW_SEG" after the review's doc pass (os88api.inc:6619-6622, ctx-5's verifier); fix the kernel comment at events.inc:109-111 and load ES from a constant in evq_push (`mov ax,LOW_SEG / mov es,ax` for push ss/pop es, es: for ss:; AX is already banked), checking kern_mega's spelling; state the rule that an ISR-callable door may assume DS and nothing else. **Follow-up (2026-11-08):** the code half is held under the owner's trust ruling (it defends against a package IRQ handler calling WAKE on a foreign SS, and none was traced; the SDK cell already states SS = LOW_SEG); the events.inc comment fix stays | 0 counted (+3 .text res if taken) | dosmouse, evqfull (soak) |
| BR-86 | DF is not cleared at the IRQ0 callback boundary, and clip_put/clip_get trust it (isr-4 confirmed, ui-7 corrected) | all | sch_isr has no cld but calls the splash rep movsb, DSV_TICK and the FSXF_RATE hook (sched.inc:1892-2400, 2349-2353; splash.inc:1024-1029); fsx_rtick likewise (fsx.inc:1364-1446); a std package's clip_put writes clip_seg:0000 then wraps and walks down from FFFF, up to 64KB past the claim (clip.inc:150-151, 196-198) while toast_show defends (toast.inc:204-209) | one cld in sch_isr after KDS and in fsx_rtick; cld in clip_put and clip_get; write "DF is the interrupted code's" into the DSV_TICK and hook contracts | +1 .text res (+1 mega fsx_rtick); +1..+2 clip | clipkeep, clipgrow (soak) |
| BR-87 | The serial lease trusts its caller to pin the region the ISR writes into (input-4 corrected) | big | srl_open validates I_SPTR and bounds but not MC_RLOC (serial.inc:226-243, 272, 423-470); SPEC.md:225404-225405 makes the pin the caller's duty and WireFleet complies (apps/wirefleet/wflink.inc:118-121) | SERIAL.DRV refuses Open with AX=5 when the region's MC_RLOC != 0; or move the ring into the driver's own image | +~20 SERIAL.DRV, 0 res | serlease, wirefleetlink, ringmove-wirefleet (soak) |
| BR-88 | Shift+PrtSc runs the ROM's Print Screen inside kbm_isr's IRQ mask (input-5 confirmed) | big, small | the ROM is called with every IRQ but IRQ1 masked (mouse.inc:3021-3031) and nothing hooks int 05h: seconds with no printer, tens of seconds printing, ticks lost | set the print-screen status byte at 0500h to 1 in mouse_init and after a hibernate resume (also disables PrtSc for DOS programs); or hook int 05h to an iret (~+12) | +5 .ovl/.boot2, 0 res | kbdburst (soak) |
| BR-89 | The 'MO' block publishes the doctored 8042 command byte as the BIOS's (input-6 confirmed) | big | mou_p2cmd0 is BIOS & 0EFh or 20h; the raw byte is outside the published span (mouse.inc:1901-1912, 6459-6469; SPEC.md:14127-14130) | swap the two declarations and their names; fix the comment with the code | 0 | ps2mouse (full) |
| BR-90 | srl_open's "settled" test lags the ISR by one UI pass (input-7 confirmed) | big | tests [mou_hpst]==2, set on the pass after the ISR settled (serial.inc:265-268, 323-334; mouse.inc:1371-1379, 3911-3916): a named Open can take the winning mouse port | test [mou_seen] instead | 0 (SERIAL.DRV) | serlease (soak) |
| BR-91 | KBDDIAG=1 drops SPEC 9.8.3's IMR bracket (input-8 confirmed) | knob only | the diag arm calls the ROM with every IRQ open (mouse.inc:2978-2996 vs 3021-3031); kbddiag.inc:24-25's header is stale | wrap it in the same bracket; fix the header | +~8 knob only | no row |
| BR-92 | mou_p2_off writes the 8042 on the UI task with nothing excluding int 09h (input-9 confirmed) | big | 0A7h then 60h + value with IF=1 and IRQ1 live (mouse.inc:2189-2203, 1753-1765); an AT int 09h's ADh/AEh between the halves sends the value to the keyboard; the probe masks IRQ1 for this race (1848-1863) | bank and mask IRQ1 at the master around the two writes (not pushf/cli: the wait counts ticks) | +~12 .text res | ps2mouse (full), p2hiber (soak) |
| BR-93 | The hot-plug poller drops DTR on a modem port every 3 s for the session (input-11 confirmed) | big, small | while [mou_seen] is 0 mou_hotplug power-cycles every unleased live port (mouse.inc:1335-1370); a modem at &D2 hangs up; SPEC 115.1.1 rule 4 already treats DSR/DCD as not-a-mouse (serial.inc:342-352) | skip the drop on a port whose MSR shows DSR or DCD | +~14 .text res | wirefleetnomouse, serlease (soak) |
| BR-94 | A settled serial mouse is never offered a reset edge again (input-14 confirmed) | big, small | [mou_hpst]=2 is terminal (mouse.inc:1298-1299): a mouse wedged by a cable bump stays wedged | a Control Panel verb or key running one drop-hold-raise on [mou_port] | rec; +~20 CTRL.DRV | no row |
| BR-95 | Key and mouse order is lost across a UI pass (histsys-6 confirmed) | big, small | ui_task takes one int 16h key per pass, then drains the mouse ring (ui.inc:120-245, 224-233): under starvation a click on B then typing sends the first key to A | kbm_isr pushes a payload-less EVT_KEY marker when the BIOS tail moves; the drain fetches the key at the marker | +~25 .text res (optional) | kbdburst (soak) |
| BR-96 | The wake re-arms the LOSING serial port (hiber-1 corrected) | big (HIBER.DRV) | mou_pall writes both UART rows (hiber.inc:4801-4807; mouse.inc:474-481); on the reboot route, a resume reached before the fresh contest settled brings the loser home unmasked with IER=1 and [mou_seen]=1, so mou_lockon never runs again | after the IER write, when [mou_seen], call the resident mou_lockon through hbm_kd before .kbunm | +10 HIBER.DRV | hibernate, p2hiber (soak) |
| BR-97 | Nothing fences compaction or shedding during the progressive image write (hiber-5 corrected) | big (HIBER.DRV) | the image is written live through the file layer (hiber.inc:1886-1918), which can claim (disk.inc:6194-6220, 7540-7600); safe today because the FAT window already exists | raise [mem_cp_busy] across the write (it also makes mem_free and mem_movable refuse: the write path must do neither); a test leg with the FAT window dropped | +10 HIBER.DRV | hibernate (soak) |
| BR-98 | The resume stub's int 13h calls run through kbm_i15 in the image being overwritten (hiber-6 confirmed) | big on KBD_I15 machines | hbm_res never calls mouse_unhook (hiber.inc:2586-2651, 3626-3683), so an AT ROM's int 13h calls int 15h AH=90h/91h into kbm_i15 (mouse.inc:3079-3084); survives because the bytes are identical | mouse_unhook via hbm_kd after hbm_down | +6 HIBER.DRV | hibernate (soak) |
| BR-99 | A failed no-question resume pins HIBER.DRV for the session (hiber-8 confirmed) | big | HBP_DOS opens no window; on refusal hb_mode=HB_M_RESUME and hbf_dropck drops only GONE (hiber.inc:2430-2435, 2664-2671, 721-729); same after a failed cw_app_launch | set HB_M_GONE when hbm_win reports no window | +8 HIBER.DRV | hibernate (soak) |
| BR-100 | A pointer this kernel can never read is kept and HIBER.DRV reloaded every boot (hiber-9 confirmed) | big | any dskw_read failure goes to .gone with no forget (hiber.inc:2368-2369); SPEC 87.9's FERR_BIG for a kern_mega pointer | FERR_BIG to .stale ("Hibernation: other build", forget) | +6 HIBER.DRV | hibernatem, megahiber (soak) |
| BR-101 | Write Img lays sector 0 over a short disk and says "Wrote", and never checks the target can hold the image (hiber-10 confirmed, hiber-11 confirmed) | big (DISKOPS.DRV) | a zero chunk ends the loop and clo_fin runs (diskops.inc:1902-1926); clo_wimg calls no clo_fits, and clo_fits skips same-drive (1612-1654, 409-415) | refuse CERR_NOTIMG unless CL_NEXT == CL_TOT; gate clo_same on CL_END != 2 and call clo_fits | +8 and +10 DISKOPS.DRV | wimgtrip, diskclone (soak) |
| BR-102 | Two non-atomic BDA tick reads (hiber-16 confirmed) | big (HIBER.DRV) | 046Ch then 046Eh with interrupts on (hiber.inc:3146-3149, 4647-4648): a carry between them is an hour of error | pushf/cli around each pair | +6 HIBER.DRV | hibernate (soak) |
| BR-103 | hbm_hsave trusts ROMF_HTAB's size (hiber-18 confirmed) | big | `sub cx,6 / ja` walks 64KB on CX=0; HBP_PAD keeps only the low byte (hiber.inc:2072-2096, 2385-2392) | jcxz guard; %error in COLDHMA pinning the table under 256 bytes | rec; +2 HIBER.DRV | hmahiber (soak) |
| BR-104 | OSAPI_MEM_FREE and _REGROW act on the caller's own live region, and the damage lands on the next claimant (hm-2 corrected, mem-6 corrected) | big, small | mem_find_own's self arm (memory.inc:4415-4422) fences mem_free_x (4459) and mem_regrowp (4576); REGROW path 3 calls no relocation; DELIBERATE per loader.inc:1690-1695 and SPEC 66.6.1.1 | owner's call: refuse a region record in the package doors mmf_osapi_mem_free and osapi_mem_regrow_x (NOT in mem_find_own, which mem_owner_of_x needs); allow the shrink. **Follow-up (2026-11-08):** the same class as the six fences the owner declined (it defends the kernel from a trusted caller, and SPEC 66.6.1.1 accepts it as self-harm), so it is not counted; the checked build (phase1 WP19) or a named exception | 0 counted; +3..+8 .cold res if named; a contract change | memp (soak) |
| BR-105 | KB-to-paragraph shifts trust CF, which holds only the last bit out (hm-6 corrected, mem-5 confirmed, md-16 confirmed) | big, small; kern_dos | mem_claim_dma_x's head: CX=2048 gives SI=0 and a claim with no page constraint (memory.inc:860-868; package door 5254-5266); kerndos mem_claim_x the same (kerndos/kdshim.inc:409-412); mem_regrow's sibling already fixed (4562-4565); md-16 is kern_dos's private mem_claim_x, reached from kerndos/kdgate.inc:97, not a package door (flagday fit lens on C1) | `cmp cx,64 / ja <refuse>` before the shift; `cmp ax,0x400 / jae` in kdshim | 0..+3 .cold; +1..+4 kern_dos | memp (soak) |
| BR-106 | kern_mega osapi_mem_regrow_x's move copies the OLD size into the NEW claim (mem-19 corrected) | mega | the .move arm copies old KB << 9 (memory.inc:5440-5468) and mega_claimregrowf sends EVERY huge-block regrow there, shrinks included (mega.inc:12052-12056, 12084): an overrun, a 0-length copy at 128KB+, or an offset past 64KB | copy min(old, new) and refuse (or segment-step) past 64KB | +4 .cold (above 1MB) | megahigh (full) |
| BR-107 | OSAPI_MEM_P treats an unknown verb as MEMP_AVAIL and hands back the caller's CF (hm-14 confirmed) | big, small | `cmp bl,MEMP_AVAIL / jae .avail` (memory.inc:5582-5583) and pushf/popf (4927, 5023) | `cmp bl,MEMP_KPARTS / ja <stc/retf>`, or document. **Follow-up (2026-11-08):** the register already called it the same kind as the six package fences, and the fit lens argues it as forward compatibility (an old kernel answering a newer verb), which is the API level's question (the owner: prove its worth first, then, in the second round, take it if it is cheap - phase1 WP9). So document (0 bytes), and take the refusal with the first new `OSAPI_MEM_P` verb, if one is ever added (the purgeable verb is parked, `../followup-purgeable.md`) | 0 (document); +4 .cold with a new verb | memp (soak) |
| BR-108 | wm_su_drop ignores mem_free's refusal and loses the claim (wm-9 corrected) | big, small | zeroes the slot then calls mem_free, which refuses under mem_cp_busy (wm.inc:9866-9888; memory.inc:4446-4453); needs a relocation proc to break SPEC 66.3 rule 3 | clear the slot only on CF=0 | +3 .text | no row |
| BR-109 | OSAPI_WM_FULLSCREEN draws a HIDDEN window whole, and the destroy/fullscreen lock texts are stale (wm-7 corrected) | big, small | no visible test before wm_raise (wm.inc:4316-4345); both require the caller to hold the lock (kernel.asm:2724, 3228; os88api.inc:304, 892-894) but the SDK's "as OSAPI_WM_HIDE wants it" is no longer true; wm_lk_go's `or al,al` would clobber CF | refuse or defer the raise for a hidden window; fix the SDK text; doors optional (wm_lk_go needs pushf/popf to carry CF). **Follow-up (2026-11-08):** the doors are the package-fence class the owner declined, so only the raise fix is counted | +~5 .text; the doors (+6..+19 in all) not counted | dispfsx (soak) |
| BR-110 | ui_dispatch decides "is this Locator" by an unlocked read of AM_ONCMD = 0, and .oncmd reads through BX = 0 (ui-3 confirmed) | big, small | ui.inc:2512-2536, 2586-2600; menu_win_set takes no lock (menu.inc:448-457); SPEC 12.2 reserves 0 for Locator but the SDK's AM_ONCMD line does not (os88api.inc:4072) | `cmp word [menu_win],0 / jne .app`; `or bx,bx / jz .app_done` in .oncmd; reserve 0 in the SDK | -19 .text res, both (owned by phase1 WP1, which prices it too; not counted in batch C) | no row |
| BR-111 | ui_cmd's jump table has no length assert against CMD_LAST (ui-4 confirmed) | big, small | ui.inc:3023-3045; menu.inc:75-88; the comment's count is already wrong | .tab_end and `%if (.tab_end-.tab)/2 != CMD_LAST / %error` | 0 | no row |
| BR-112 | A typed plain Enter matches the Alt+Enter latch (ui-5 confirmed) | big, small | the latch holds scancode 1Ch and the dedupe compares AH alone (ui.inc:195-207; mouse.inc:3198-3214): an enhanced ROM gets two fullscreen toggles and no Enter, an XT ROM loses the Enter | `test al,0x0F / jnz .notae` before the compare | +4 .text res | no row |
| BR-113 | A toast raised under a fullscreen window or saver expires unseen (ui-6 corrected) | big, small | toast_arm stamps [toast_die] before menu_draw_bar's wm_fs_vis guard, in both toast_pass and toast_now (toast.inc:434-447, 632-639; menu.inc:825) | guard both arming sites, or toast_arm itself; or fix the comment and SPEC 59.3 | +5..+10 .text res, or 0 | toastbar, toast (fast) |
| BR-114 | Under TOAST_Q one raiser's retire takes down another's message (ui-16 corrected) | big | toast_show's .kill calls toast_kill on whatever is up (toast.inc:224-229, 291-301); SPEC 59.11 documents it, against its own ownership model | retire only when DH = [toast_inst] or the caller is the kernel | +6..+10 .text res | toast (fast) |
| BR-115 | W_ONMOUSEUP is not delivered when the ring dropped the release (ui-9 confirmed) | big, small | .arm_done clears a region-0 arm when the button is up and the ring empty (ui.inc:773-783) while .mup says "the kernel guarantees delivery" (325-327); evq_mup drops on full (events.inc:223-245) | dispatch W_ONMOUSEUP at [mouse_x]/[mouse_y] through .mup_lk when [ui_armr]=0 at the drop; or weaken the contract | +12 .text res, or 0 | mouseup, evqfull (soak) |
| BR-116 | ui_sys_open indexes fm_stattab with no bound (ui-17 corrected) | big, small | ui.inc:2941-2959; toast_say exists to carry LD_NSTAT's bound (toast.inc:324-327; files.inc:2375) | `cmp al,LD_NSTAT / jae .back` | +4..+6 .text res (ul-11) | no row |
| BR-117 | The Remove Shortcut item is poked by raw offset from five sites (shell-6 confirmed) | big | menu_items_file+2 at desk.inc:1406, 1452 and desksc.inc:517, 541, 1492; the table comment names the wrong writer (menu.inc:2576) | MENU_FILE_REM equ with an %if MENU_FILE_N guard | 0 | desksc (soak) |
| BR-118 | Basic dock_geom works only at boot or with CTRL.DRV loaded (shell-11 confirmed) | big | dkb_geom far-calls CPE_DOCK_GEOM outside the splash (dock.inc:213-220); a future caller gets mod_gone's CF and no geometry | state the invariant in the banner, or fall back to dkb_band | 0, or +3 .text | dockpos (soak) |
| BR-119 | STIKMENU.DRV writes the kernel's service-item record, and a driver's record is half-sanitised (shell-7 confirmed, shell-12 corrected) | big | stm_a_more copies into desk_svc_file/desk_svc_name (stikmenu.inc:1844-1864); osapi_desk_item_x terminates only the caption (desk.inc:2236-2251) | pass ui_svc_open the strings as inputs; optionally terminate file and name | 0..+4; 0..+10 .cold | stikmenu, deskitem (soak) |
| BR-120 | [ld_said] leaks out of loads nobody reports, silencing the next failure (loader-7 confirmed) | all | set at .abort/.rehtoast; ldf_ld_pkg_start and assoc_run_x's hand-on arm never consume it (loader.inc:548-557, 1552-1573, 2085-2103; assoc.inc:1315-1316; ui.inc:2944) | clear the adjacent word at the three ld_painted clears and on the Task Manager route; or return the flags in AH | +8 .cold, or ~0 | assocopen (soak) |
| BR-121 | DESK.DRV stays loaded when a widget launch meets a full instance table (loader-9 confirmed) | big | desk_need before the record scan; `jc .nomem` skips .nodrv's drop, and desk_drop_x there would clear a bit one past the table (loader.inc:1199-1215, 1275-1282; deskmod.inc:121) | refuse before desk_need when the top-down scan finds no free record | ~+5 .cold | stikboot (soak) |
| BR-122 | OSAPI_PKG_START's caller image must be pinned across the call; the contract does not say so (loader-10 confirmed) | all | [ld_msrc] is banked before ld_alloc, which may compact (loader.inc:1872-1873); The Wire pins (apps/thewire/thewire.asm:3234-3236) | one sentence in SPEC 21.5 | 0 (doc) | thewire (soak) |
| BR-123 | fmv_owner's "cannot happen" fallback tags the cache as the menu save-under (files-6 confirmed) | big | files.inc:1376-1382 owns it as MEM_P_MSAVE, whose shed row names menu_sseg (memory.inc:1308); unreachable today | a non-purgeable kernel tag, or stc | 0 | no row |
| BR-124 | The Standard File commit's volume put-back ignores a failed mount (files-7 confirmed) | big, small | fdlg_reap_x calls dsk_chdir_x and never reads CF (fdlg.inc:597-606); kernel requesters with no instance then act at the root with the write gate shut | on CF skip the callback with a beep | +2..+5 .cold | fmcommit (soak) |
| BR-125 | Opening a chooser ends other windows' prompts without repainting them (files-8 confirmed) | big, small | .endp clears modes 1-2 in every pool block, a byte store and nothing else (fdlg.inc:336-351) | mark each cleared block FS_DIRTY=FSD_PIXELS or call fm_owed | ~+4 .cold | fdlgchoose (soak) |
| BR-126 | Modality filters presses, not drops: a file dragged out of a Save chooser moves into the window behind (files-9 confirmed) | big | fdlg_grab sees MDOWN/RDOWN only (ui.inc:457-470); fm_dgdrop resolves with wm_hit (files.inc:3730-3760, 7136-7156, 7221-7229) | in fm_dgdrop with [fdlg_win] set, refuse any target but the chooser | ~+10 .cold | fdlgdrop (soak) |
| BR-127 | kern_small keeps a closed window's 2KB cache claimed (files-10 confirmed) | small | MEM_P_VIEW+slot is reaped by nothing; freed only when the pool block's next tenant runs fm_kinit_x (files.inc:555-565, 1432-1433) | free the chooser's cache in fdlg_reap_x and a KIND_FILES cache on close | ~+10 .cold | fdlgsmall (soak) |
| BR-128 | A folder copy silently skips hidden and system files, and a cross-volume Cut then stops at the first one (files-12 confirmed) | big, small | fcp_scan asks dsk_dscan with DL=0 (filecp.inc:1222-1225, 1297-1298); dskw_rmtree refuses FERR_PROT (diskw.inc:1006-1010) | state it in SPEC 22.3's limits; optionally toast a skip count before the delete phase | rec; ~+15 if toasted | fcpcopy (soak) |
| BR-129 | ASSOC.DAT can exhaust the association slots for the session, and an emptied volume keeps a stale cache (files-13 confirmed, text-11 confirmed) | big; FORMAT.DRV | nothing frees a slot; assoc_ext_take accepts any byte above 20h (assoc.inc:734-761, 2077-2100); asw_close returns CF=1 with nothing to write and the flush takes .same (ascw.inc:153-154, 186-188, 983-990) | filter the three bytes through dskw_char_x and stop absorbing near the cap; write an empty header over an old file | ~+10 .cold; +~10 FORMAT.DRV | assocstale, assocflush (soak) |
| BR-130 | The inst_tab I_SPTR relocation row also matches near pointers (files-14 corrected) | big | mem_rr_tab patches every I_SPTR equal to a moved base; a built-in kind's I_SPTR is a near pointer into fm_pool at 0AAFBh, safe only because that is above every heap segment (memory.inc:3351; instance.inc:201) | an %error bounding fm_pool's offset, or filter the row by I_KIND | rec; 0 | pkgmove-* (soak) |
| BR-131 | OSAPI_FONT_GLYPHS can return the seam cut's borrowed table (text-4 confirmed) | big (SEAMCUT) | font_ch_seam rewrites [font_seg]:[font_base] for one font_char under the lock (font.inc:448-458); osapi_font_glyphs reads it lock-free (kernel.asm:6627-6632) | a canonical pair for the API written only by font_init, hbm_wake 4a and the mega pool move; or document | +4 .bss, +~6 per writer | no row |
| BR-132 | Menu layout measures package strings with an unbounded font_width_x (text-5 confirmed) | big, small | 8 x len mod 64K (font.inc:2542-2557) in menu_widest before the clamp (menu.inc:2427-2460): an 8,192-byte item wraps to a narrow save-under while menu_trunc letters 24 cells | bound the measure at MENU_MAXCH+1; take the name width from the built menu_abstr | +4..+8 .text res | no row |
| BR-133 | A failed FAT flush keeps its dirt, which dsk_fatw_park may write onto another disk (dw-5 confirmed) | big, small | .ioerr skips .reset (diskw.inc:4989-4992); park flushes outgoing dirt before it knows a floppy changed (disk.inc:5794-5799) | .ioerr: dskw_ioerr then dskw_refat; it lands before phase1 WP12's lazy FAT flush, which depends on it | +3 .cold | fdswap (soak) |
| BR-134 | WRITE_AT's INSIDE arm writes up to 511 bytes past the caller's buffer onto disk slack (dw-6 confirmed) | big | dskw_size32 rounds [dskw_rem] up and the arm transfers whole sectors from ES:BX (diskw.inc:2727-2747, 3954-3966); SPEC 18.4's "No memory ever leaks past EOF" | route the final partial sector through dskw_wone; or amend SPEC 18.4 to name the exception | +25 .cold res | dosfile (soak) |
| BR-135 | DSKW_RT_MAX bounds the size of a tree, not the depth of a cycle, and its trip leaves the volume deep in the tree (dw-7 corrected) | big | the guard decrements per entry (diskw.inc:1142-1144): a 4,096-entry tree stops part-deleted with FERR_IO; `jz .corrupt` skips dskw_rt_go_home | re-arm after each removal through one shared label; trip through .corrupt_home | +6..+18 .cold | no row |
| BR-136 | Short names with bytes 80h-FFh, or a leading 05h, are listed but cannot be opened, deleted or renamed (dw-11 confirmed) | big, small | dskw_char_x accepts only A-Z, 0-9 and dskw_syms (diskw.inc:5217-5248; 5150-5160) | accept 80h-FFh (no upcase); map a leading E5h to 05h in dskw_name83 | +8 .cold | dosdir (soak) |
| BR-137 | fsck's FILEnnnn.CHK number uses an 8-bit multiply and wraps at 10,000 (dw-18 confirmed) | big (FSCHECK.DRV) | `mul dh` (fsck.inc:2151-2163); four digits written (2298-2307) | 16-bit multiply; saturate at 9,999 and treat it as a full root | +6 FSCHECK.DRV | fsckdisk (soak) |
| BR-138 | A run past the buffer's segment wraps to ES:0000 silently (dr-7 confirmed) | big, small | .notched adds the run to BX with no carry test and the next cap at BX=0 is 128 sectors (disk.inc:1238-1242, 2128-2135, 2627-2631) | fail when `add bx,ax` carries while SI != 0 | +6 .cold | no row |
| BR-139 | dsk_rah_fill computes the track phase on the volume-relative LBA (dr-8 corrected) | big (BIOS hard-disk rows) | divides DI by spt while the transfer converts DI+[dsk_base] (disk.inc:7095-7099, 1750-1760); mis-sized fill on a partition not track-aligned; data stays correct | divide by spt*heads first and take the remainder mod spt, or bank base mod spt once per transfer (a full 32-bit dividend can #DE) | ~+14 .cold | hddgeom (soak) |
| BR-140 | dsk_find returns a redirected volume's name unterminated when the driver's is 13 bytes or more (dr-9 corrected) | big | the FSV_ENUM arm copies 13 bytes with no NUL forced; +13 is the attribute byte (disk.inc:3807, 8110-8119, 8193-8199) | `mov byte [es:di-1],0` after the rep movsb | +5 .cold | cdromsim (soak) |
| BR-141 | dsk_fdd_probe clears unit 1's calibrated bit whatever unit it probed (dr-14 corrected) | big | .out ANDs 0040:003E with 7Dh (disk.inc:10962) though the probe runs for units 1..3 (10772; desk.inc:349-352) | derive the mask from [fdd_unit]; keep clearing the probed unit's bit | ~+10 .ovl | deskfdd (soak) |
| BR-142 | Two unstated reads before write and a zero-capacity link in the mount (dr-15, dr-17 confirmed) | big, small | dsk_rah_fill reads [dsk_totsec] before the mount sets it (disk.inc:4220-4223, 7115-7122); dsk_up_open writes index 0 without [dsk_nmax] (5533-5560) | comment both | rec; 0..+5 | no row |
| BR-143 | Taking the terminator in a sector's last slot trusts the next sector to be zero (dw-19 confirmed) | big, small | diskw.inc:4648-4654; fsck.inc:2285-2291 | record only | rec | no row |
| BR-144 | kern_mega's drv_fs_hi wraps its buffer offset at 64KB (drivers-8 confirmed) | mega | `add [dfh_buf],cx` with a 32-bit capacity (driver.inc:2687-2817, 2786); depends on a caller passing >64KB | clamp to 10000h - BX, or pin the caller rule in SPEC 62.9.2.4 | rec; +~8 | no row |
| BR-145 | OSAPI_DRV_CALL promises DI back, which neither side keeps, and "CF=1 AX=0 = no driver" is ambiguous (abi-5 confirmed) | big | drv_pkg_call_x pops the caller's DI before the far call (driver.inc:6614-6662); os88api.inc:3456-3460; drivers/os88drv.inc:377 | driver contract says "preserve DI; BP is spent" | 0 (SDK text); +2 if enforced | ethcfg (soak) |
| BR-146 | An N-cell body reads the caller's DS out of api_rn's frame at a literal [bp+10] (abi-4 corrected) | all | diskw.inc:3397-3414; kernel.asm:4747-4767; ABI-BREAK-LIST T1 reshapes the frame | API_RN_DSOFF equ beside api_rn; name the reader in T1's row | 0 | api-abi (fast) |
| BR-147 | vid_apply arms the row table on VID_VESA, where the mouse stack may overwrite it (video-8 confirmed) | big | vid_rowtab lies under mou_pstack and a packed display's arrow may run into it (mouse.inc:371-388) while VID_APPLY builds it and sets vid_rowmax=348 (viddet.inc:1022-1042) | skip the build and leave vid_rowmax 0 when [vid_planes] > 4 (also ~4.6 ms per switch) | +7 per VID_APPLY expansion (+7 .boot2, +14 modules) | vesa (soak) |
| BR-148 | A same-mode bracket on the secondary paces FSXW_VSYNC to the primary's retrace port (video-9 confirmed) | big | fsx_wait picks 3BAh/3DAh from [vid_kind] (fsx.inc:953-958) | `mov al,[fsx_wdisp] / call vid_kind_of` | +3..+8 .text res | fsxdisp, dispfsx (soak) |
| BR-149 | vid_reboot leaves a mono SECONDARY showing a frozen desktop (video-10 corrected) | big | handles only a Hercules primary (vidsel.inc:499-522), against SPEC 39.20.1 | program the mono card to text by hand and restore [vid_kind] (HIBER.DRV's hbm_stageseg reads it after vid_reboot); dark a CGA secondary with vid_blank_kind | +18..+24 .text res | dispreboot (soak) |
| BR-150 | A mode-setting bracket's collapse leaves vid_ctx record 0 describing the primary (video-11 confirmed) | big (EXTD.DRV) | vid_fsx_enter republishes with ndisp=1, cur=0 and captures nothing (extmod.inc:437-466) | state it as binding in SPEC 39.18.3, or capture and restore record 0 | rec; 0 or +10 EXTD.DRV | fsxdisp (soak) |
| BR-151 | FSI_PAGES advertises pages fsx_page refuses (video-12 confirmed) | big, small | VGA0D says 8, a dual-card Hercules 2 (fsx.inc:1161-1189, 1005-1012) | FSIF_FLIP (FSI_FLAGS bit 3) set when fsx_page will flip | +8 res big, +4 small; additive (no bump) | wireballvideo (soak) |
| BR-152 | The planar bodies hard-code an 80-byte stride (gfx-7 confirmed) | big | ROW_BYTES at thirteen planar sites (vga12.inc:3398..4782) while gfx_blitp and gfx_blit1 read the live stride | refuse a 4-plane mode with stride != ROW_BYTES in vid_apply | +~8 .ovl (placement unchecked) | vgadirty (soak) |
| BR-153 | The SPL_RESIDENT guard stops four bytes short, and the rung is four sectors too big (boot-4 corrected) | big, small | SPL_RES_SIZE measured at spw_resident_end, before spl_live/spl_busy/spl_quiet (kernel.asm:8653-8656; viddet.inc:1743-1796); 9 sectors against a need of 5 | move the label; derive SPL_RESIDENT from the size; correct the %error text | 0 res; ~-400 bytes of KERNEL.SYS on kern_big | splashbar (soak), kzboot (soak) |
| BR-154 | Guard 5 omits BLOB_LIFT on every shipped kernel (boot-18 corrected) | big, small | the sum leaves out 1,536 bytes on ROM_COLD, the default (kernel.asm:9683-9686, 1121-1123, 1913-1933); stage 1 refuses correctly at run time | use BLOB_SEG_AT in the sum | 0 | small128 (full) |
| BR-155 | sch_isr draws a 28.7 ms spinner frame with IF=0 during kmain_o (boot-11 confirmed) | big, small | sched.inc:2318-2353; splash.inc:1150-1163: IRQ6 and IRQ4 blocked per phase change | sch_isr sets a "frame due" byte; spl_step draws it on the main line; measure with os88boot.py first | ~-10 .text, +6 .boot2 | splashspin (soak) |
| BR-156 | A floppy image dd'd to a USB stick boots drive A; Shut Down leaves hard-disk heads over data (boot-12, boot-15 confirmed) | big, small | boot.asm:276-298 forces DL > 3 to 0; shutdown.inc:24, 54, 328 | docs/LIVE-MEDIA.md troubleshooting line; a park verb in HDD.DRV if ever taken | rec; ~+20 module | shutdown (soak) |
| BR-157 | The MM58167 New-Year shadow is refreshed only on a Set or a roll (sysmisc-4 confirmed) | big | clk_ns_read stamps only when [clk_nsrol] (clock.inc:1090-1105, 1203-1218): off from one March to the next April reads a year low | stamp whenever the decoded shadow differs from [clk_tb_mo] | +~8 .ovl | no row |
| BR-158 | An RP5C01 in 12-hour mode is claimed in the morning and refused in the afternoon (sysmisc-5 confirmed) | big | the probe joins the 10-hour digit with its PM bit (clock.inc:1305-1315, 1463-1465) | also compare the 12-hour decode when the raw hour is above 23 | +~16 .ovl | no row |
| BR-159 | The Screen Saver settings window outlives the Control Panel, and its save waits for the next panel close (sysmisc-6 confirmed) | big | THMPAL's window is reaped at panel close (driver.inc:3083-3097), SAVER's only via ss_post_x, which sets [cp_wdirty] with nobody to spend it (blank.inc:844-861) | close it from the panel's teardown with a new SSV_ verb (DRVC_OVL is kernel-private); or flush when no panel is open | +~12 .cold res, +~15 SAVER.DRV | saver (soak) |
| BR-160 | Unloading VESA.DRV redraws the Drivers pane at a stale origin after vid_switch (sysmisc-7 confirmed) | big | cp_drv_click's VID_EXT arm switches and then runs cp_list/cp_drv_box1/cp_drv_line1 at the pre-switch DI/BP (ctrl.inc:7131-7179), against the rule at 4390-4396 | record the unload, cp_wdset, jmp .done past the redraw | -1..+2 CTRL.DRV | vesacp (soak) |
| BR-161 | SEG_LEND writes mega_rspar before validating, and checks no owner (mc-3 confirmed) | mega | mega.inc:12779-12784: a refused lend leaves rsseg on the old claim and rspar on the refused paragraph | store after the `jc .lno`; optionally compare MC_OWN | 0..+12 .mega | no row |
| BR-162 | Spurious IRQ7/IRQ15 get a non-specific EOI (mc-6 confirmed) | mega | mega_eoi1/eoi2 send OUT 20h,20h with no ISR read (mega.inc:840-852, 2136-2150) | stubs for 27h and 77h that read OCW3 0Bh first | +16 .mega | no row |
| BR-163 | mega_rfclk miscounts a reflection across midnight (mc-7 confirmed) | mega | low-word subtraction of 40:6C (mega.inc:3892-3934) | add 00B0h when the difference is 8000h or more | +6 .mega | no row |
| BR-164 | mega_k09: keypad -/+ give no ASCII with NumLock off; 40:96's right Ctrl/Alt bits are not kept (mc-8 confirmed) | mega | .pad covers 47h-53h (mega.inc:1837-1858); AH=12h returns 40:18 alone (2069-2073) | exclude 4Ah/4Eh; track 40:96 bits 2-3 | +20 .mega | no row |
| BR-165 | mega_s2segc truncates an off-grid base; mega_ustab rows are never freed; MX_FWIN is never cleared (mc-12, mc-13, mc-14 confirmed) | mega | shr ax,4 with no nibble test (mega.inc:5470-5486); mega_useg has no free path (14299-14385); MX_FWIN or'd at 13480, tested at 11243 | return FFFFh for an off-grid base; limit useg to A000h+; clear MX_FWIN with the last window row | +4, +6, +10 .mega | no row |
| BR-166 | mega_leds spins up to 2x65,536 polls inside IRQ1 with IF off (mc-15 corrected) | mega | mega.inc:1311-1345, 1756-1759 | bound the wait near 2,000 polls | +4 .mega | no row |
| BR-167 | OSAPI_XMEM_COPY writes anywhere below 1MB; the software-vector dispatch enters a handler with the dispatcher's flags (mc-17, mc-18 confirmed) | mega | mega.inc:9773-9776; 891-1022 | record (HIMEM parity; handlers read the frame) | rec | no row |
| BR-168 | The snapshot's fallback can build a prefix park below MPK_MIN_KB (md-4 corrected) | mega | .fallb uses KB-in-use + 16 with no floor (megados.inc:345-357, 795-798) against .ptry's floor (362-364) | clamp the runs claim to MPK_MIN_KB | +8 .mega | megadosroom (soak) |
| BR-169 | mega_d8042get can bank a keystroke as the 8042 command byte; the park stub's key wait accepts a PS/2 mouse byte (md-9, md-11 confirmed) | mega | megados.inc:1944-1984 tests bit 2 only; megapark.inc:242-254 tests no AUX bit | ADh before 20h and AEh after (or require bit 0); `test al,0x20` as mega_dkobf does | +5 .mega; +4 HIBER.DRV | megap2hiber (soak) |
| BR-170 | mega_dreg_add drops a region on overflow; the stub's restore loop is wrong below PKB 33; the trip's own time is uncounted and x87 state not reset (md-12, md-17 corrected, md-18 UNCHECKED) | mega | megados.inc:480-497, 73-76; megapark.inc:172-198; megados.inc:735-737, 1342-1348 | an %if for the region worst case; an assert PKB >= 33 where the floor is set; FNINIT if a package ever keeps x87 state across a yield | rec; 0 | megadosp (soak) |
| BR-171 | Section switch-back is gated only for nobits sections (engsys-12 confirmed) | build | NASM refuses only an open nobits section; shutdown.inc ends in .modu by design; the sentinel block omits .vga, .cfx, .modx, .mods, .modfs (kernel.asm:9786-9886) | a host check that each .inc's last section equals the one active at its %include, with an exemption list | 0 | no row (a fast unit row) |
| BR-172 | The whole-soak permission refusal is bypassed by `-k '*'` (engsys-15 confirmed) | tools | os88test.py:1318 and os88soak.py:1050 test only `not a.k` | also refuse a selection covering more than ~90% of the tier without --user-asked | 0 | no row |
| BR-188 | On kern_small any task's claim may shed, the case kern_big's gate exists to forbid (ctx-6 corrected) | small | both "ONLY THE UI TASK SHEDS" tests, `cmp byte [sch_cur],0 / jne`, are under %ifdef OS88_COMPACT (memory.inc:967, 4726; kern_big only, kernel.asm:1089-1090); mem_regrow's own reason ("a shed from its stack could free a cache the pre-empted UI task holds in a register") applies on kern_small, which has purgeable claims and 7 task slots; the FAT walk defends itself with sch_lock for exactly this (disk.inc:6405-6420); SPEC 66.3.1 excludes kern_small on purpose, but its stated reason covers compaction, not the shed; PLAUSIBLE | take the two tests out of the guard, or state kern_small's rule in SPEC 66.3.1 with the holders that defend themselves; fix disk.inc:6426's comment | +14 .text res kern_small, 0 big | fcpsmall (soak); small128 (full) |
| BR-189 | cmz_jsayx tells a code from a string at 16; fm_errtab has 11 entries (gfce-7 confirmed) | all | an AX of 11..15 reads osapi_ferr_text's code bytes as string pointers, toast_say's documented failure mode (compress.inc:3416-3429; files.inc:2396-2401, FM_NERR = 11; toast.inc:325-327); both callers pass kernel FERR codes today: latent | `cmp ax, FM_NERR` in place of `cmp ax, 16` (the same imm8 encoding) | 0 | no row |
| BR-190 | The READ_SEQ cursor names no file and stays trusted across a quiet folder change (gi-4 confirmed) | big | the hot path compares only RSQ_GEN with [dsk_mgen] and RSQ_VOL with [disk_drive] (diskw.inc:3104-3115); dsk_mgen moves only on committed writes and on mounts that read a boot sector (disk.inc:4199-4204), so a fixed-disk or batch-trusted navigation keeps a cursor for A\X.DAT hot for B\Y.DAT at the same offset: the wrong clusters with CF=0; it is a documented caller contract (os88rseq.inc:21-24, SPEC 18.4.8.1), but the kernel's own banner says the opposite (diskw.inc:3249-3253) and os88api.inc says "a stale cursor is never an error" | stamp an 8-bit name sum and the folder's low cluster byte into the cursor's kernel-private bytes 3-5 in dwf_dskw_read_seq; a mismatch reads cold; fix the os88api.inc sentence (SDK text) and the banner | ~+20 .cold res kern_big (unchecked), 0 small | no row |
| BR-191 | The parts standard and op_self size reads in CLUSTERS on kern_big, bringing back SPEC 18.4.4.2's cost, and the cluster size is learnable only through a full free count (gi-3, gi-2 confirmed) | big (package side: every lazy-parts and pself carrier) | op_lazyp, op_claim and op_self round head and length to clusters (apps/os88partsbody.inc:1834-1870; apps/os88pself.inc:41-45, 62-80) while READ_AT takes sectors on kern_big (diskw.inc:3143-3148): a 4KB lazy part on a 16KB-cluster partition claims and reads up to ~32KB, ~64KB at 32KB clusters - negligible on floppies, which is why it has not shown; DFREE and VOL_STAT both run dsk_free_clus_x (~105 ms on a 20MB disk MEASURED, ~55 ms on 1.44MB modelled) for a [dsk_spc] the kernel already holds | append OSAPI_FILE_GRAIN behind inst_vol_enter (AX = bytes per cluster, CX = READ_AT's granule; no count, no I/O); op_clbget banks CX in [op_clb] and op_self masks with it | +14..+16 res per kernel (the cell; additive, no bump); ~0 package bytes | no row |
| BR-192 | pit_now is wrong inside an armed FSXF_FASTTICK/FSXF_RATE bracket, and only its users say so (gi-7 confirmed) | all | sch_fast_on reloads channel 0 at 65536/N or the caller's divisor while 0040:006C stays at 18.2 Hz (sched.inc:509-534); pit_now assumes 65,536 counts a tick (apps/os88pit.inc:57-83), so its low half laps N times a tick and goes backwards mid-tick; isoTron and RealDMG work round it; SPEC 53.2.1's "nothing outside sched.inc can tell" is wrong for a count reader | document it in os88pit.inc, SPEC 53.2.1 and SPEC 72.15.1; record an OSAPI_PIT_NOW that knows [sch_fast]/[sch_racc]. **Follow-up (2026-11-08):** os88pit.inc:39-43 and SPEC 53.2.1 already say it (the review's doc pass, `10912c6e`); only SPEC 72.15.1's sentence is owed | 0 (docs); ~+20 res if the cell is ever taken | no row |
| BR-193 | crt0's plain .OVL arm ships in no package and keeps the folder bug the lazy arm fixed (gi-9 confirmed) | C SDK (only tests/cfsx uses it) | without CC_OVL_PART, cc_ovneed reads <NAME>.OVL with OSAPI_FILE_READ where the instance stands (apps/cc/crt0.asm:1256-1261), which the file dialog moves (SPEC 19.2.1); apple2, c64, cword, lemmings, loom, runcpm and weave all use CC_OVL_PART | retire the arm (source only), or give it the lazy arm's go-home bracket; at least correct the comments | 0 shipped | no row |
| BR-194 | kbm_move's draw gate lacks FPG_ARMED (cursor/mc-2 confirmed) | big, small | mou_apply refuses to draw while the progress widget is armed (FIELD-NOTES 34), but kbm_move.paint tests only [gfx_lock_flag] and [cur_level] (mouse.inc:2689-2709) and cur_move's working set is in gfx_u; fprog.inc:771-790 lets the bar compose with the mutex unheld; the window needs a mouseless machine, the keypad pointer and a lock-free fpg arm pre-empted mid-fill | add FPG_ARMED to kbm_move.paint, or one gate shared with mou_apply | +7 .text res | no row |
| BR-195 | The volume popup moves the pointer as cursor_hide + cursor_show, the pre-7.1.2 double write (cursor/mc-6 confirmed) | big, small | ui.inc:3359-3373: two walks, so the overlap bytes are written as background and then as arrow on every adapter - FIELD-NOTES 6's wash-out - for the popup's life | one fused walk: pushf/cli and cur_move, as kbm_move does | ~0 | no row |
| BR-196 | The scroll-bar drag identity (x1,y1) assumes bars cannot share a corner; Disk windows can (os88ui/F5 confirmed) | big | os88ui_sbmine keys the drag on (x1,y1) (apps/os88ui.inc:2868-2904) and fm_sbset fills one block for every Disk window (files.inc:3412-3452); a damage repaint of a hidden window mid-drag (3356-3361) draws its thumb at the dragged position; SPEC 13.10.5.10's "cannot share a top-left corner" holds only for visible bars | bank [fm_vp] at the grab and call os88ui_sbfix in fm_sbset only when it matches, or add y2 to the identity | ~+8 .cold res kern_big; 0 small (no drag) | no row |
| BR-197 | The package door reaches a partition erase, and five places still say it reaches no sector (hdk-2 corrected) | big (HDD.DRV, HDDTOOL) | HDV_TOOL forwards to HDT_PKG, whose HDI_GO with CH bit 0 erases and reformats a partition through hd_raw (drivers/hdd/hdxms.inc:53-65); SPEC 117.4's design is deliberate (the fence is hd_iact_ok and the wizard's Confirm page), so this is stale text: hdxms.inc:34-36, hdd.asm:1569-1572, os88api.inc:3490 and 3550, SPEC 52.1.4 | rewrite the five (os88api.inc:3490 is fingerprinted: abilock --update, no bump); the owner decides whether a package may erase without a tool-owned confirm; the cheap strengthening is an HDI_GO erase that requires a token from HDI_SLOTS | 0 res; ~+10 HDDTOOL | no row |
| BR-198 | HDD.DRV's rung 1 has no retry and no reset (hdk-4 confirmed) | big (HDD.DRV) | every hd_xfer_ide failure goes to hd_xfer_fail4 or the 0Ah exit on the first attempt (drivers/hdd/hdd.asm:449-527) where rung 0 gets three and the floppy ladder more; hd_buf_step leaves hd_lba/hd_bofs on the failing sector, so `.run` is a valid re-entry | one re-issue from the failing sector with a counter; optionally SRST through base+206h and hd_ide_setparams between attempts | +10 HDD.DRV resident image (+12 more with SRST) | hddgeom (soak) |
| BR-199 | The partition table is trusted by type and length alone, and HSV_MOUNT does not refuse a mounted partition (hdk-5, hdk-7 confirmed) | big (HDD.DRV) | hd_part_load accepts any LBA 0 carrying 55AA as HMB_OK and never reads the status byte (drivers/hdd/hdcom.inc:188-223, 204-207), so a superfloppy's boot code becomes four entries a user can Delete or Format over a live filesystem; hd_vol_find keys on (device, slot), so two slots with one base mount twice (mount.inc:44-91, 359-376); hd_svc_mount calls hd_mount_one with neither of hd_mount's guards (hdtool.inc:597-625) - harmless today only because the installer unmounts first | an entry whose status byte is not 00h or 80h is HMB_BAD in hd_part_load (hdcom.inc, both images); hd_mount_one refuses a base equal to a live row's HDV_BASE on the device, which subsumes hdk-7; then delete hd_mount's own hd_vol_find test | +14 (status, both images) + ~+20 resident image (base check; hdk-7's ~+10 net inside it) | hdboot (soak) |
| BR-200 | hd_chs bounds the cylinder at 1,024, not at the drive's own count (hdk-6 confirmed) | big (HDD.DRV) | `cmp ax,1024 / jae .bad` (drivers/hdd/hdcom.inc:142) never reads HDD_CYL, though the header says "past the drive"; an overstated partition is refused nowhere until a write near its end fails; the kernel's BIOS path uses the same ceiling (dsk_cylmax) | `cmp ax,[di+HDD_CYL]` (the same 3 bytes); either set HDD_CYL from IDENTIFY in hd_twins on success or document the BIOS row's lost last cylinder | 0 | hddgeom (soak) |
| BR-201 | hd_mount_one's CF is read as a verdict but is undocumented and partly accidental (hdk-8 confirmed) | big (HDD.DRV) | the .badvol exit leaves OSAPI_VOL_DEL's CF (drivers/hdd/mount.inc:110-113, 175-177) and hd_svc_mount branches on it (hdtool.inc:619-620); hd_vol_of answers 0FFh/CF itself | delete the `jc hd_svc_out` at hdtool.inc:620; correct the header | -2 | no row |
| BR-202 | hd_raw's bounce copies CL x 256 words, silently dropping a count's high byte (hdk-13 confirmed) | big (HDD.DRV) | `mov ch,cl / mov cl,al` (drivers/hdd/mount.inc:525-526) copies sectors x 256 words mod 256 sectors while the claim is sized from the full CX; HSV_RAW passes HRQ_CNT unchecked (hdtool.inc:533-551); only HMA or selector buffers bounce, and callers use 1-3 sectors | refuse CX > 127 in the bounce path (`cmp cx,127 / ja` with CF), or state the bound in HSV_RAW's ABI line | +4 HDD.DRV resident image | no row |
| BR-203 | hd_buf_step walks DX on an offset wrap, while BLKCAP_SEL promises it walks only BX (hdk-14 confirmed) | big, mega (HDD.DRV) | `add [hd_bseg],0x1000` on a carry (drivers/hdd/hdd.asm:659-666) is meaningless on a selector; unreachable, since a selector's run cannot wrap before its last sector (SPEC.md:102787-102790) | a one-line note on hd_buf_step and in SPEC 51.8.1 | rec; 0 | no row |
| BR-204 | kern_mega's SEG_TILE proves only that the first and last tile lie in some block (gms-5 confirmed) | mega | two mega_bclip calls (mega.inc:13662-13670); bclip leaves CX unchanged on CF=1 and .put ignores CF (13690-13693), so a middle tile in a gap gets limit FFFFh and a run ending in a neighbouring block passes, against the header's promise | mega_blkof once on the first base; refuse with AX=3 when (n-1)*64KB reaches the block's end; clip only the last tile; drop the per-tile table walks under cli | -10 .mega | no row |
| BR-205 | kern_mega's mega_mpmovef holds a conventional move as a 16-bit signed paragraph count (gms-6 confirmed) | mega | `sub bx,ax` then `sar bp,12` (mega.inc:14250-14252): a move of 8000h paragraphs (512KB) or more rebases every descriptor in the block the wrong way; the 640KB heap is ~35,800 paragraphs, so a near-empty heap compacting one claim from the top to the floor can reach it; rare and silent | take the 17th bit from the sub's borrow (`sbb bp,bp`) when forming the high part | +4..+8 .mega (the verifier: a few bytes more than +4) | megahigh (full) |
| BR-206 | XMEM.DRV has eight block-table entries for the whole machine (xm-3 corrected) | big (XMEM.DRV) | XM_MAX_BLKS 8 is shared by the RAM disk, the cloner and ~10 packages while DOS.O88 alone exposes XMS_NH = 8 handles (drivers/xmem/xmem.asm:160; apps/dos/dos.asm:14487); HIMEM defaults to 32; the stride is 8 for a 6-byte entry (xmem.asm:178) | XM_BLKSZ 6 and XM_MAX_BLKS 13 in both files (t_mirror holds them); update tests/xmcheck.py:55 and tests/xmla.py:92; fsx.inc:3567 (kern_mega's no-store answer) reads the constant too | 0 image (the scratch goes 208 -> 222, under its 225-byte region); but if xm-8 frees that region first, +87 image - price the two together | tests/xmcheck.py, tests/xmla.py |
| BR-207 | DOS.O88's XMS 08h reports the total as the largest free block (xm-4 confirmed) | big (DOS.O88) | xm_caps answers the pool less every block, a total (drivers/xmem/xmem.asm:488-505), and DOS.O88's .query returns it as the largest ("the same number", apps/dos/dos.asm:14623-14630); once the pool fragments, a program allocating the largest block (the common XMS idiom) gets A0h; only kern_mega's kdx_largest answers right | compute the largest gap in DOS.O88 from OSAPI_XMEM_WALK, which kern_big answers (package only); a CAPS register answer would be a flag-day change | +~50 DOS.O88, 0 kernel | no row |
| BR-208 | On kern_big DOS.O88's XMS move does not check the offset against the handle (xm-5 confirmed) | big (DOS.O88) | dos_xms_fits and both its calls sit inside %ifdef DOS_MEGA (apps/dos/dos.asm:14874-14915, 14963-14990); the kernel's xm_chk passes any range wholly inside some block the DOS instance owns (drivers/xmem/xmem.asm:741-791), so a bad offset on handle A that lands inside handle B silently writes B, where HIMEM answers A4h/A6h/A7h | take dos_xms_fits and its two non-bothx calls out of the %ifdef | +~55 DOS.O88 | no row |
| BR-209 | XMS owner 0xFF is shared by the kernel and every driver, and nothing sweeps it (xm-6 confirmed) | big | the RAM disk and the cloner stamp their blocks 0xFF (drivers/ramdisk/rdstore.inc:131-134); xm_free and xm_chk compare BL only, so any 0xFF caller can free or copy into another's block; XMV_RELINST is reached only from instance teardown (kernel/xmem.inc:709-735), so a forgotten 0xFF block is lost until reboot; the shared tag has cost a bug before (SPEC 62.9.10.2); kern_mega tags drivers 0C0h\|row and FDh; BR-35 is the same shape in the heap | stamp a driver row in drv_call/drv_cp_call that xm_ownbl maps to 0C0h\|row, and have drv_release dispatch XMV_RELINST with it; the slot users of snd_inst must not see the tag | rec; +~15 .cold, +~6 .text res kern_big | no row |
| BR-210 | XMEM.DRV's tier-1 masked windows probably exceed one 1200-baud mouse byte on a 6 MHz AT (xm-7 confirmed; PREDICTED) | big on a 286 (XMEM.DRV) | XM_MAX_COPY = 32KB with the PIC masked across the BIOS call (drivers/xmem/xmem.asm:147-152, 1036-1061): at least ~11 ms at 6 MHz, nearer 16 ms with wait states; a 16KB LOADALL piece is ~2x the Victory 286's 5 ms (1083-1090); a 7N1 byte at 1200 baud is 7.5 ms and an 8250 holds one; no emulator here times a 286 | one field run on a 286-6 with a moving mouse; if overruns are confirmed, an 8KB LOADALL piece (within 6% of 16KB per the Victory report) and 8KB internal AH=87h chunks, priced against the per-call reset | rec; 0 (constants) to +~10 | a field run (docs/FIELD-MACHINES.md) |
| BR-211 | XMEM.DRV leaves the conventional end unchecked: ES=FFFFh reaches past 10FFEFh into the pool's first 32KB (xm-9 confirmed) | big (XMEM.DRV) | the kernel folds ES*16+SI with no bound and leaves the length to the image (kernel/xmem.inc:604-606, 638-659), and the image's xm_chk tests only the extended end (drivers/xmem/xmem.asm:817): FFFF:FFF0 with CX = 32768 addresses 10FFE0h..117FDFh, another owner's pool block; HMA drivers legitimately pass FFFF:xxxx | in xm_copy refuse with AX=2 when the conventional end plus the length passes 10FFF0h | +~12 XMEM.DRV image (7 bytes of slack: needs xm-8's ~-20 first) | tests/xmcheck.py |
| BR-212 | A clone of a running instance is not a pristine image - a hazard of loader-14's PROPOSAL, not of the tree (ul-6 confirmed) | all (a proposal) | ld_start zeroes only [ld_bss] bytes at [ld_img] and enters (loader.inc:1300-1308), so a clone of a running region carries that instance's modified initialised data; the second launch re-reads the disk in full anyway (a streaming read, SPEC 18.95.1) | keep loader-14 as record; if a multi-instance package ever makes it worth doing, hm-15's code-only shared PART, not a region clone | rec; ~+25 .cold for the clone, or not viable | no row |

---

## Cross-cutting constraints

Three facts bind several batches at once. Each was visible in one item's notes
and is stated here once, because a batch planned without it does not assemble.

### Two transient windows are full on kern_big

Both are MEASURED from the maps at this tree.

- **The blob (`.boot2`).** kern_big: 0x19FC of 0x1A00 (BOOT2_SECS 13,
  build/kernel.map:414 and 3127), **4 bytes free**. kern_small: 0x1342 of 0x1600,
  702 free. kern_mega: 0x1AE3 of 0x1C00, 285 free. The register asks kern_big's
  blob for BR-1 +19..+23, BR-6 +60..+110, BR-55 +8..+12, BR-56 +4, BR-63 -1,
  BR-147 +7 and (if taken) BR-155 +6, against boot-7's -41 (phase1 WP6, which
  owns it): +56..+120 net. **A 14th blob sector is certain on kern_big if BR-6
  lands in the blob at all**, and that is the owner's call: +512 bytes of guard
  5's boot footprint and ~+22 ms of load on a 360KB disk (PREDICTED). Without
  BR-6 the rest fits after boot-7. **Answered (owner, 2026-11-08): no 14th
  sector** - it delays the progress bar by 0.4-0.8 s. BOOT3-PLAN's wave 1 moves
  507 bytes of boot-only bodies into a third boot home at KERNEL.SYS's tail and
  frees about **508** blob bytes (~549 after boot-7), 0 resident, about +24 ms
  after the bar on 360KB (docs/plans/BOOT3-PLAN.md sections 8-11). With BR-1's
  canary move now 0 bytes (follow-up), the ask is **+45..+109** net of boot-7,
  which fits there with BR-6 in its alias-test form.
- **The boot overlay's window half (`.ovl`).** kern_big: OVL_SIZE 13FFh =
  5,119 bytes against the 5,120 the FAT window plus dsk_secbuf allow
  (kernel.asm:8939's %error), **1 byte free**; kern_mega 13FEh, 2 free;
  kern_small 05C2h, ample. (The cost lens quoted 5,116; the map says 5,119.)
  The register asks kern_big's overlay for BR-9 +12 (+8 for vector 6), BR-18
  +6, BR-25 +6 and -2, BR-44 +5 (+22 in gfce-6's form), BR-88 +5, BR-141 +10,
  BR-152 +8, BR-157 +8 and BR-158 +16: **+82..+99**. None of those lands until
  a body leaves the overlay. **Now (2026-11-08):** BR-9's +12 and +8 go behind
  the knob (owner), so the ask is **+62..+79**; WP7's -45 still lands, and
  BOOT3-PLAN's wave 3 moves 176 bytes (`vid_ctx_init`, `sch_idle_start`,
  `mem_init_x`) into `.boot3`, leaving `.ovl` 177 free before WP7. The %error names the remedies - move a body to the
  blob (also full) or out of the boot path - and one item supplies room: BR-6's
  cheaper shape moves vid_memchk from `.ovl` into `.boot2`, freeing 55 bytes of
  overlay at the cost of the blob sector above (no longer needed: BOOT3-PLAN
  expands vid_memchk in both homes). The first register noted the full overlay
  against BR-9 only.

### The owner's "packages are trusted" ruling

**A recorded principle now (owner, 2026-11-08, decision 3 in
`../owner-decisions.md`).** Real mode cannot defend against a hostile
package (and on kern_mega an FSX real-mode bracket hands it real mode anyway),
so the only thing worth defending against is our own mistakes, and most of
the time those are better found and guarded at BUILD time than with resident
bytes. Resident guards pay where the failure is catastrophic and
user-visible and the printout gives the owner useful information (the
stack-overflow panel). The earlier wording, from
docs/plans/LAST-DROP-BYTES.md:1135-1140, said the same: "packages are trusted
- this is real mode, and a package that misbehaves takes the machine down
whatever the kernel refuses - so a check that exists only to defend the kernel
from its caller is not a reason to keep a byte." The systemic fit lens had
asked for one choice per fence - APICHK=1 only (gfce-9, 0 shipped bytes), or a
named exception. **The owner chose: the six package-argument fences are not
taken as retail code**; an APICHK-style knob at most:

| item | the check | core bytes (big / small), no longer counted |
|---|---|---|
| BR-8 | door refusals, the wake fence, DESK.DRV's .make bound | +36 / +36 (+5 DESK.DRV) |
| BR-22 | file cells refuse a worker caller | +12 / +12 |
| BR-29 | the entry proc's returned BX | +13 / +13 |
| BR-30 | no nested OSAPI_PKG_START, UI task only | +18 / +18 |
| BR-37 | a package-opened batch bracket | +16 / +16 |
| BR-173 | a worker dying under its own gfx lock | +12 / +12 |
| total | **not taken (owner, 2026-11-08)** | **+107 / +107** |

BR-8's bound (+2) goes with its doors: taken alone it swaps a dead machine for
a write into another package's window, so it is not taken either. BR-104,
BR-107 and BR-109's optional doors are the same kind in the low rows and leave
the "all" sums as follow-up applications of the principle (each stays open as
a named exception), and BR-85's +3 code half leaves batch F's core the same
way (follow-up, 2026-11-08). NOT in this class: the fixes against hostile FILES (BR-7,
BR-23, BR-44, BR-59), which SPEC 19's threat model covers whatever the caller.
BR-9 is not in it either - it reports every fault, the kernel's own included -
but it goes behind a knob for the owner's other reason (decision 1: a panel
for a failure nobody has met waits for a field report), and so do the
diagnostic-only guards BR-68, BR-80, BR-82 and BR-173's optional panel
(follow-up, the same weighing).

### Phase 1 and this register price some of the same work

Count each once. phase1 WP6 owns boot-7's -41 (used here as room, not as a
saving). phase1 WP8 prices BR-8 and BR-9 with wm-2 (kern_big +34 +12 .ovl,
kern_small +28); the batches here use this register's figures - and after the
owner's answers both are 0 retail (BR-8 not taken, BR-9 a knob), in WP8 and
here alike (owner, 2026-11-08). phase1 WP1 owns
BR-110's -19 and BR-11's loader-8 (-3 kern_big, -20 kern_mega .cold); they are
NOT counted in the batches below.

---

## Fix batches

Nine batches, each a module boundary an implementation session can take whole:
few files, one review thread, one final pass. Batch order is a suggestion (A and
D carry the most dangerous items; A also settles the blob question the others
wait on - now answered by BOOT3-PLAN, owner, 2026-11-08). Every fix is phase **now** with no ABI break except four, which belong
to the ABI-BREAK-LIST batch instead: BR-34's IRQ ownership cell, BR-66's
ISR-reachable claim flag, BR-104's policy reversal if the owner takes it, and
BR-182's DSV_BLK tag. Items marked "SDK text" change a fingerprinted contract
comment and need `abilock --update` without a PKG_FMT bump; that is not a break.

Byte sums add each item's figure, the midpoint where a range is given; a range
in brackets sums the ends. "core" is the items a batch should not ship without,
"all" adds every low row in the batch. As the banner in CLAUDE.md says, these
are BYTES, not rungs, and a batch whose sum crosses no rung has still spent them.
**Recomputed after the owner's answers (2026-11-08):** the items marked "not
taken" (packages are trusted) and "knob" (a diagnostic for a failure nobody
has met) are out of every sum below, and each table says which.
kern_emu takes kern_big's figures unless stated. kern_mega columns count only
CONVENTIONAL bytes (MEGA_RESIDENT): `.text`, `.bss`, `.lowbss`, `.vga`; its
`.cold` and `.mega` are noted separately as "above 1MB", and the placement is
inferred from kern_big's sections.

### Batch A - boot, detection and the build's own gates

- **Files:** boot/boot2.asm, boot/boothd.asm, kernel/splash.inc, kernel/viddet.inc (vid_detect), kernel/vidsel.inc (vid_memchk if it moves), kernel/disk.inc (dsk_boot_from_x, .ovl only), kernel/kernel.asm (guard 5, the SPL_RESIDENT guard), Makefile (KSIG_OFF per arm, KZ_HEAD), tests/unit/t_canary.py, tests/unit/t_api_abi.py, tools/checkdocs.py, tools/os88test.py, tools/os88soak.py, tools/os88ovlchk.py.
- **Items:** phase1 WP6's boot-7 first (the room), and BOOT3-PLAN wave 1 for kern_big's blob (owner, 2026-11-08: no 14th sector). Core BR-1 (its canary move and gate first, needing no room), BR-6 (with BR-14 from batch B), BR-63, BR-25, BR-55, BR-56. Low: BR-153, BR-154, BR-155 (measure first), BR-156 (doc). Host gates: BR-62's t_api_abi half, BR-74, BR-75, BR-76, BR-171, BR-172, BR-177.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident | 0 (BR-155 would be ~-10 .text) | 0 (same) | 0 |
| .boot2, core, on its own (follow-up, 2026-11-08: BR-1's canary move is 0 big/mega, -11 small) | **+110** (+83..+137): BR-1 +12 (part 2 in its safe form; +8 with a rerun guard), BR-6 +85, BR-55 +10, BR-56 +4, BR-63 -1 | **+59** (+37..+81): BR-1 +1 (part 1 -11, part 2 +12), BR-6 +45, the rest the same | +110 (+83..+137) |
| .boot2, core, after WP6's -41 | **+69** (+42..+96) against 4 free: paid from BOOT3-PLAN wave 1's ~508 (owner: no 14th sector) | **+18** (-4..+40), fits in 702 | +69, fits in 285 |
| .boot2, all | +75, plus BR-147's +7 from batch B | +24 | +75 |
| boot sector | boothd **-1** (+8 -9) | - | - |
| .ovl (transient) | **+4** (BR-25 +6, dr-13 -2) against 1 free; -28 once AH=08h goes; BR-6's vid_memchk move would free 55 | 0 | as kern_big, against 2 free |
| disk and boot time | KERNEL.SYS ~-400 (BR-153); no blob sector (owner); BOOT3-PLAN wave 1's ~+450 of KERNEL.SYS and ~+24 ms after the bar on 360KB are that plan's, not this batch's; BR-1 +1 int 13h per 8088 boot on the 1.2MB and 1.44MB disks (PREDICTED) | BR-1 +1 int 13h per 8088 boot on both disks (PREDICTED) | - |

- **Rows (final pass):** canary, api-abi (fast); blobruns, blobsum, splashbar, splashspin, smallboot, kzboot, hdboot, hddgeom, pravetzboot (soak); bootsmoke, small128 (full); box- twins of hdboot on 86Box for FN33's images; the make gate itself for BR-177. New: an MDA-primary boot, an MCGA and a 64KB EGA profile if MartyPC or 86Box carries one (none in the suite today).

### Batch B - display, FSX and drawing

- **Files:** kernel/fsx.inc, kernel/vga12.inc, kernel/vidsel.inc, kernel/apps.inc, kernel/fprog.inc, kernel/font.inc, kernel/menu.inc (menu_widest), kernel/viddet.inc (VID_APPLY), SPEC 5.4/6.1/7/53.10 and the SDK for the pen, page and cursor texts.
- **Items:** core BR-3, BR-14 (land with BR-6), BR-45, BR-46, BR-57, BR-174 (texts). Low: BR-58, BR-77 (doc), BR-131, BR-132, BR-147, BR-148, BR-149, BR-151, BR-152. (kern_mega's BR-64 is in batch H.)

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | **+41** (BR-14 +2, BR-45 +18, BR-46 +13, BR-57 +8; BR-3 and BR-174 0), +2 .cold if gfx_blit1 is taken | **+21** (BR-14 +13, BR-57 +8), +2 | **+41** conventional; blit1's +2 above 1MB |
| resident, all | **+106**: adds BR-58 +13, BR-148 +5, BR-149 +21, BR-151 +8, BR-131 +10, BR-132 +6 | **+33**: adds BR-151 +4, BR-132 +6 | ~+104 |
| transient | .ovl +8 (BR-152) against 1 free; .boot2 +7 (BR-147) against 4 | 0 (correction: BR-147 is VID_VESA, kern_big only) | as kern_big |
| modules | +14 across VID_APPLY's module expansions (BR-147); GFXAUDIT knob ~+40 (BR-174) | - | - |

- **Rows:** dispblit, blitcut, blitpair, blitplane, atblit, atblitvga, wireballvideo, wireballmono, dispfsxherc, fsxdisp, dispfsx, vesa, vesacp, saver, savervga, toastfpg, dockmodule, dispreboot, vgadirty (soak). New: a negative-width BLIT4 call (must go red on `jz`); a GFXAUDIT row (BR-174).

### Batch C - window manager, UI and instance teardown

- **Files:** kernel/wm.inc, kernel/ui.inc, kernel/instance.inc (inst_task_die, app_close_win's tail), kernel/toast.inc, kernel/deskmod.inc and DESK.DRV, kernel/desk.inc/desksc.inc (one equ), apps/os88ui.inc (BR-176, BR-196), apps/os88api.inc texts, SPEC 11, 20.6 and 11.103.10 first.
- **Items:** core BR-4, BR-28, BR-47, BR-48, BR-49, BR-50, BR-42, BR-176; BR-173 and BR-8 are not taken (packages are trusted, owner, 2026-11-08). Low: BR-73 (the resize mode and its lint), BR-108, BR-109, BR-111..BR-119, BR-195, BR-196; BR-110 is phase1 WP1's. (BR-85 moves to batch F with the rest of the ISR doors.)

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core (owner, 2026-11-08: BR-8 and BR-173 not taken) | **+58** (+28..+88): BR-4 +22 (-6..+50), BR-47 +8, BR-48 +2, BR-49 +6, BR-50 +10, BR-176 +10; +1 .bss (was +108) | **+42** (+14..+70): BR-4 +27, BR-28 +5, BR-47 +8, BR-48 +2; +1 .bss (was +92) | **~+48** conventional, +1 .bss (was ~+98); above 1MB +10 (BR-176) |
| resident, all | **+128**: adds BR-73 +10, BR-109 +5 (its doors not counted), BR-108 +3, BR-112 +4, BR-113 +8, BR-114 +8, BR-115 +12, BR-116 +5, BR-119 +2 .text and +5 .cold, BR-196 +8 (was +185) | **+89**: adds BR-73 +10, BR-109 +5, BR-108 +3, BR-112 +4, BR-113 +8, BR-115 +12, BR-116 +5 (was +146) | ~+105 (was ~+162) |
| wm-2 for BR-8's bound | +2 more, +10..+15 .boot2/.ovl (neither has room), +12 .bss if padded to 76 | -4, +10..+15 transient | |
| modules and packages | DESK.DRV +6 (BR-42; BR-8's .make +5 not taken); every os88ui copy +8..+12 (BR-176) | CTRL.DRV and FDLG.DRV os88ui copies +8..+12 each | |
| not taken (owner, 2026-11-08) | BR-8 +38 (bound and doors) and BR-173 +12, out of the core above; DESK.DRV's +5 with BR-8 | the same | BR-173's -24 .mega not taken either |

- **Rows:** stkclass, stkapps, stik, stikcard, toast (fast); stk0water, stkpanel, timerrace, deskbench, dmgcull, zonedmg, paintnogrow, paintnogrow-vga, dockpos, dockmark, stikmenu, stikboot, stikswap, mouseup, evqfull, desksc, deskitem, toastbar, dispfsx, btnall (soak). New: stkdie (BR-4), a clickrace row on timerrace's model (BR-28); the TASK_ALIVE-under-the-lock row (BR-173) and the wild-pointer WM call (BR-8) belong to the APICHK knob's row now (owner, 2026-11-08).

### Batch D - loader and heap

- **Files:** kernel/loader.inc, kernel/memory.inc, kernel/filecp.inc (floor sites; FILECP.DRV on kern_small), kernel/compress.inc (cmz_wclaim's first try, cmz_jsayx), apps/os88partsbody.inc (BR-59), apps/cc/crt0.asm (BR-193), kerndos/kdshim.inc (BR-105's kern_dos half), SPEC 20.13.8.1 first.
- **Items:** core BR-2, BR-7, BR-11, BR-12; BR-29 and BR-30 are not taken (packages are trusted, owner, 2026-11-08). Then BR-35, BR-52, BR-53, BR-54 (measure first), BR-59. Low: BR-104 (owner's call), BR-105, BR-106, BR-107, BR-120, BR-121, BR-122 (doc), BR-188, BR-189, BR-193.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core (owner, 2026-11-08: BR-29 and BR-30 not taken) | **+63** .cold (+50..+70): BR-7 +34, BR-11 +13, BR-12 +16 (was +94) | **+73** (+59..+80): the same plus BR-2 +10 (was +104) | **0** conventional (+0..+2 .bss only in BR-7's fold); the core .cold above 1MB |
| resident, all | **+124**: adds BR-35 +5, BR-52 +12, BR-53 +26, BR-54 +3, BR-105 +2, BR-120 +8, BR-121 +5; BR-104 and BR-107 not counted (follow-up) (was +164) | **+97**: adds BR-105 +2, BR-120 +8, BR-188 +14 (.text) (was +137) | 0; +4 above 1MB (BR-106) |
| not counted here | loader-8 (BR-11's fix): ~-3 .cold big, ~-20 mega - phase1 WP1's | | |
| modules / packages | FILECP.DRV a few bytes (kern_small, BR-11); +20 in EVERY parts package's image (BR-59); kern_dos +1..+4 (BR-105) | | |
| not taken (owner, 2026-11-08) | BR-29 +13 and BR-30 +18, out of the core; BR-104 +5 and BR-107 +4 out of "all" (follow-up) | the same | |

- **Rows:** heapgran, drvmem (fast); lzfence, lzload, lzload-lz4, lzfmt-all, lzbig, lzcomp, fcpcopy, fcpapi, fcproom, fcpsmall, fdlgsmall, memp, regrowshed, clipgrow, heapcheck, heapmap, heapdrv, msegnomem, pkgmove-*, dosmem, assocopen, stikboot, thewire (soak); small128, megahigh (full). New: a hostile-header package, a shed-during-mount row on the 128KB machine, a close-a-window-mid-paste row, ifoff --regrow (BR-12). (Nested-start and bad-entry-BX rows go to the APICHK knob's row.)

### Batch E - disk and file layer

- **Files:** kernel/disk.inc, kernel/diskw.inc, kernel/files.inc, kernel/fdlg.inc (FDLG.DRV on kern_small), kernel/fsck.inc (FSCHECK.DRV), kernel/assoc.inc, kernel/ascw.inc (FORMAT.DRV), kernel/filecp.inc (BR-128's doc only), kernel/kernel.asm (kern_small's refusal stubs), apps/os88wseq.inc (BR-179), apps/os88partsbody.inc and apps/os88pself.inc (BR-191), apps/os88rseq.inc and os88api.inc texts.
- **Items:** core BR-16, BR-17, BR-23, BR-24, BR-27, BR-41 (BR-22's and BR-37's fences are not taken, packages are trusted, owner, 2026-11-08; BR-22's SDK texts stay); BR-62's stub halves (dw-8's label move, gi-6's VOL_STAT stub, abi-2's SDK text) with BR-179. Then BR-61, and BR-60 only on the owner's word (90 bytes). Low: BR-123..BR-130, BR-133..BR-143, BR-190, BR-191. BR-183's kernel half (int 13h status 11h in the BIOS loop) lands here too, unpriced.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core (owner, 2026-11-08: BR-22 and BR-37 not taken) | **+40** (+38..+41): BR-16 +16, BR-17 +12, BR-23 +11, BR-24 +5, BR-27 -12, BR-41 +8 (was +68) | **+45** (+43..+46): BR-16 +16, BR-17 +9, BR-23 +11, BR-24 +5, BR-62's VOL_STAT stub +4 (was +73) | **0** conventional (was 0..+9, BR-22's N-stub); the rest above 1MB |
| resident, all (without BR-60) | **+192** (was +220): adds BR-61 +16, BR-134 +25, BR-135 +12, BR-139 +14, BR-126 +10, BR-129 +10, BR-136 +8, BR-138 +6, BR-140 +5, BR-125 +4, BR-124 +4, BR-133 +3, BR-190 +20, BR-191's cell +15 | **+111** (was +139): adds BR-61 +16, BR-127 +10, BR-136 +8, BR-138 +6, BR-125 +4, BR-124 +4, BR-133 +3, BR-191's cell +15 | |
| BR-60 subdirectory growth | **+90** more | optional | |
| transient / modules / packages | .ovl +10 (BR-141) against 1 free; FSCHECK.DRV +6; FORMAT.DRV +10; os88wseq carriers +2 (BR-179) | FDLG.DRV +8 | |
| not taken (owner, 2026-11-08) | BR-22 +12 and BR-37 +16, out of the core | the same | |

- **Rows:** fdswap, wseq, dosmedia, curdisk, dosfull, fsckhd, fsckdisk, fmcommit, fdlgchoose, fdlgup, fdlgdrop, fdlgsmall, pkgmove-*, dosfile, dosdir, assocstale, assocflush, cdromsim, deskfdd, hddgeom (soak); api-abi (fast). New: a hostile FileSize entry (the worker-calls-a-file-cell row is the APICHK knob's now, owner, 2026-11-08), an unreadable FAT, a 15th file in a kernel-made folder, a kern_small REDLINE save.

### Batch F - input, ISRs and the scheduler

- **Files:** kernel/mouse.inc, kernel/sched.inc (the INT 0/INT 6 stubs and the system-error panel), kernel/events.inc, kernel/serial.inc (SERIAL.DRV), kernel/clip.inc (one cld), kernel/fsx.inc (fsx_rtick's cld only), kernel/ui.inc (BR-195's popup, if not taken in C), kernel/kbddiag.inc and kernel/stkdiag.inc (knobs), apps/os88pit.inc (BR-192's text), SPEC 7 for BR-178's bounds.
- **Items:** core BR-18, BR-19, BR-20, BR-36, BR-51, BR-85 (its comment only: the +3 code half is held, follow-up, 2026-11-08), BR-86 (the cld), BR-178; BR-9 is a knob build's (owner, 2026-11-08), as are BR-80's tripwires and BR-82's canary (follow-up). BR-21 is MEASURE first. Low: BR-78 (doc), BR-79, BR-80, BR-81, BR-82, BR-83, BR-84 (rides with BR-9), BR-87..BR-95, BR-175, BR-192, BR-194; BR-34's four IMR read-modify-writes (its vector scan is batch G).

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core (owner, 2026-11-08: BR-9 a knob; follow-up: BR-85's +3 held) | **+56**: BR-18 +6, BR-19 +15, BR-20 +13, BR-36 +13, BR-51 +8, BR-86 +1; BR-178 0 (was +114) | **+37**: BR-19 +15, BR-20 +13, BR-51 +8, BR-86 +1 (was +80) | **~+57** conventional (BR-18..BR-51's .text, +1 for fsx_rtick); BR-9 is a knob, and kern_mega's IDT handles INT 0 after the switch |
| resident, all | **+99**: adds BR-92 +12, BR-93 +14, BR-34 IMR +8, BR-86 clip +2, BR-194 +7 (was +175; BR-82 +11 and BR-80 +7 to the knob, follow-up) | **+68**: adds BR-93 +14, BR-34 IMR +8, BR-86 clip +2, BR-194 +7 (was +129) | ~+100 (was ~+121) |
| optional on top | BR-95 +25; BR-21's rethink +30..+40, plus +6 to leave MOUPRIV before the sti or +64..+128 .lowbss for a bigger MOU_PSTK (the owner: more stack work is not needed immediately - the mouse and idle already have stacks, and the known case is a sound interrupt on a near-full package stack, owner, 2026-11-08); BR-175 +122 .vga (VGA/EGA machines); BR-80's second guard +7 in the knob only | the same, less BR-175 | |
| knob build only (owner, 2026-11-08; follow-up) | BR-9 ~+55 res and .ovl +20 (+12, vector 6 +8); BR-82 +11; BR-80 +7..+14 | BR-9 ~+40 res and .ovl +12; BR-82 +11; BR-80 +7..+14 | |
| transient | .ovl **+11** (BR-18 +6, BR-88 +5) against 1 free (was +31 with BR-9) | .boot2 +11 (BR-18 +6, BR-88 +5) | as kern_big, against 2 free |
| modules | SERIAL.DRV +20 (BR-87), 0 (BR-90); the IFLAT knob (BR-178) | - | - |

- **Rows:** serlease, dosmouse, doswmouse, kdmouse, usbmouse, kbd4f, kbdpreempt, kbdburst, int0sweep, stkpanel, stk0water, timerrace, timerbounce, evqfull, wirefleetlink, wirefleetnomouse, ringmove-wirefleet, clipkeep, clipgrow, trkspkrate, vidspk22, vidspkfast (soak); ps2mouse (full); 86Box AT-BIOS twins for BR-19. New: iflat and ifoff --mouse (BR-178, BR-21); the divide-by-zero package (BR-9) runs on the knob build. The measurement BR-21 needs is serlease at divisor 12 with the mouse moving, taken once by whoever decides.

### Batch G - drivers, hibernate and system services

- **Files:** kernel/driver.inc, kernel/snd.inc, kernel/hiber.inc and kernel/diskops.inc (HIBER.DRV, DISKOPS.DRV), kernel/drvvol.inc, kernel/clock.inc, kernel/ctrl.inc (CTRL.DRV), kernel/blank.inc (SAVER.DRV), kernel/kernel.asm (osapi_rand), drivers/sound/sb.inc (SOUND.DRV), apps/timer/timer.asm, apps/dos (the kern_dos date half of BR-26), docs/plans/NO-PINNING-PLAN.md (K1b's row) and KERN-MEGA-PLAN's checklist (gkm-1's rule).
- **Items:** core BR-5 (as K1b), BR-10, BR-15, BR-31, BR-32, BR-33, BR-38, BR-39, BR-40, BR-43, BR-44; BR-34's vector scan and SB fixes. Then BR-26, BR-72. Low: BR-96..BR-103, BR-157..BR-160, BR-145 (doc).

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | **+83** (+75..+90): BR-5 +28 .cold and +11 .bss, BR-31 +12, BR-32 +5, BR-34 scan +25, BR-43 +2 | **+2** (BR-43; BR-15 is 0) | **+13** conventional (.bss +11, .text +2); ~+70 .cold above 1MB |
| kern_emu | +84 (DRV_MAX gives .bss +12) | | |
| resident, all | **+95** (adds BR-159 +12) | +2 | +13 |
| .ovl (transient) | **+29** (BR-44 +5, BR-157 +8, BR-158 +16), +46 with gfce-6's form, against 1 free | 0 | as kern_big |
| HIBER.DRV | **~+100** (BR-10 +2, BR-38 +20, BR-40 +30, BR-96 +10, BR-97 +10, BR-98 +6, BR-99 +8, BR-100 +6, BR-102 +6, BR-103 +2) | - | batch H adds -9 |
| other modules | DISKOPS.DRV +48 (BR-72 +30, BR-101 +18); CTRL.DRV +40..+60 (BR-26), ~0 (BR-160), +22 (gfce-6's form of BR-44); SAVER.DRV +15; SOUND.DRV +14 (IMR +6, OCW3 +8) | - | |

- **Rows:** drvmem (fast); usbmouse, heapdrv, hmadrv, hma286, hmapool, sndkeep, sndonce, sndmove, hibernate, hibernatedrv, hibernatem, hmahiber, p2hiber, ems, diskclone, imgcases, wimgtrip, cfgtrip, deskclock, arclock, kdreturnset, saver, vesacp (soak). New: untick a second driver while USBMOUSE's worker is up (BR-5), Esc on the Resume face keeps the files (BR-10), a DOS TIME read after a panel Set (BR-26).

### Batch H - kern_mega

- **Files:** kernel/mega.inc, kernel/megados.inc, kernel/megapark.inc, kernel/megares.inc, kerndos/kdmouse.inc, kerndos/kdhma.inc, kerndos/kdpark.inc, kernel/fsx.inc's real-mode door (BR-184), kernel/vga12.inc's KERN_MEGA arm (BR-64), kernel/memory.inc's mega regrow arm (BR-106 is in batch D's file but may travel here).
- **Items:** core BR-13, BR-64, BR-67 (0 bytes), BR-69, BR-70, BR-71, BR-184, BR-185, BR-186, BR-187. Then BR-65 (form (c) now, (a) on the owner's word), BR-66's LDT half (with gms-10's walkers); BR-68's counter goes to a knob (follow-up, 2026-11-08). Low: BR-144, BR-161..BR-170, BR-204, BR-205. BR-66's compaction model is flag day. kern_mega also carries its conventional share of batches B, C, F and G (the side-by-side table).

| | conventional (MEGA_RESIDENT) | above 1MB (.cold/.mega) | modules / other |
|---|---|---|---|
| core | **+20** .text (BR-64, unless placed in .mega) | **+19**: BR-13 +28, BR-67 0, BR-184 +10, BR-185 -18, BR-186 -25, BR-187 +24 | HIBER.DRV -13 (BR-69 -12, BR-71 -1); HMA kern_dos +7 (BR-70 +8, BR-71 -1); .boot2 +8 (BR-71), which fits after batch A's +69 in the 285 free |
| all | +20; **+128..+192 .lowbss** only if BR-65's form (a) is chosen | **~+133** (was ~+141; BR-68's +8 to a knob): adds BR-66 +25, BR-161 +6, BR-162 +16, BR-163 +6, BR-164 +20, BR-165 +20, BR-166 +4, BR-168 +8, BR-169 +5, BR-144 +8, BR-204 -10, BR-205 +6; BR-65(a) +20 more if chosen | HIBER.DRV -9 (adds BR-169 +4) |

- **Rows:** megagrid, megapkg (fast); megahigh (full); megatrap0, megadosp, megadosroom, megahibfull, megap2hiber, megahiber, kdmouse, dosmouse, lzload, win16mega* (soak). These run on QEMU, which checks no segment limit and cannot fail BR-13, BR-64 or BR-65: those rows belong on Bochs (docs/BOCHS.md) - a lent-stack int 10h probe, a 3-byte-slack canvas, a foreign-stack option-ROM IRQ. BR-184..BR-187 are logic and descriptor races that QEMU can run once a row drives them.

### Batch I - HDD.DRV, XMEM.DRV and DOS.O88's extended-memory face

New in this revision: the gap reviews' driver-side defects, none of which costs a kernel byte in its core.

- **Files:** drivers/hdd/hdd.asm, hdcom.inc, hdxms.inc, mount.inc, hdtool.inc (HDD.DRV and HDDTOOL); drivers/xmem/xmem.asm and apps/os88ub.inc's premise text; apps/dos/dos.asm (the int 15h hook, XMS 08h and the move check); kernel/xmem.inc and kernel/driver.inc only for BR-209 (record) and BR-182's teardown disarm; SPEC 41, 51.8.1, 52, 96 and 117 texts.
- **Items:** core BR-180, BR-181, BR-183; BR-182's teardown disarm (its DSV_BLK tag is flag day). Low: BR-197..BR-203, BR-206..BR-211.

| | kernel resident | DOS.O88 | XMEM.DRV image | HDD.DRV |
|---|---|---|---|---|
| core | 0 (BR-182's disarm: a few bytes plus a DSV verb, unpriced) | **+52** (+45..+60, BR-180) | **~+2** (BR-181) | **+12** resident image (BR-183) |
| all | +21 on kern_big only if BR-209 is taken (record) | **+157** (adds BR-207 +50, BR-208 +55) | **+14** (adds BR-211 +12, which needs xm-8's ~-20 first: 7 bytes of slack); BR-206 0, or +87 priced with xm-8 | **+58** resident image (adds BR-198 +10, BR-199 +34, BR-201 -2, BR-202 +4; BR-200 0); HDDTOOL +10 (BR-197) |

- **Rows:** hdboot, hddgeom (soak) and their box- twins; tests/xmcheck.py and tests/xmla.py; xmsprobe under DOS.O88 on QEMU (a 286+ is on QEMU's list). BR-183 and BR-210 want field runs (docs/FIELD-MACHINES.md).

### The batches side by side (core / all)

| batch | kern_big resident | kern_small resident | kern_mega conventional | transient | modules and packages |
|---|---|---|---|---|---|
| A boot and detection | 0 / 0 | 0 / 0 | 0 / 0 | .boot2 +69 big and mega, +18 small (after WP6; big paid from BOOT3 wave 1); .ovl +4; boothd -1 | KERNEL.SYS ~-400; no blob sector (owner) |
| B display | +41 / +106 | +21 / +33 | +41 / ~+104 | .ovl +8, .boot2 +7 (big) | +14; GFXAUDIT knob |
| C WM and UI | +58 / +128 | +42 / +89 | ~+48 / ~+105 | - | DESK.DRV +6; os88ui copies +8..+12 each |
| D loader and heap | +63 / +124 | +73 / +97 | 0 / 0 | - | parts packages +20 each |
| E disk and files | +40 / +192 (+90 BR-60) | +45 / +111 | 0 / 0 | .ovl +10 | FDLG/FSCHECK/FORMAT +8/+6/+10 |
| F input, ISR, sched | +56 / +99 | +37 / +68 | ~+57 / ~+100 | .ovl +11 big; .boot2 +11 small | SERIAL.DRV +20 |
| G drivers, hiber, services | +83 / +95 | +2 / +2 | +13 / +13 | .ovl +29 | HIBER ~+100, DISKOPS +48, CTRL ~+50, SAVER +15, SOUND +14 |
| H kern_mega | - | - | +20 / +20 (+128..+192 if BR-65a) | .boot2 +8 | HIBER -13 / -9, HMA kern_dos +7; above 1MB +19 / ~+133 |
| I drivers' side | 0 / 0 (+21 record) | - | - | - | DOS.O88 +52 / +157, XMEM.DRV +2 / +14, HDD.DRV +12 / +58 |
| **total, core** (owner, 2026-11-08) | **~+341** (was ~+508) | **~+220** (was ~+372) | **~+179** (was ~+232..+241) | kern_big .ovl +62 against 1 free (177 after BOOT3 wave 3) | |
| **total, all** | **~+744** (+90 with BR-60; was ~+945) | **~+400** (was ~+586) | **~+342** (was ~+420..+429) | | |
| not counted: knob builds | BR-9 ~+55, BR-82 +11, BR-80 +7..+14 | BR-9 ~+40, BR-82 +11, BR-80 +7..+14 | BR-68 +8 .mega, +2 .bss | BR-9 .ovl +20 big, +12 small | |
| not counted: packages are trusted | BR-8 +38, BR-22 +12, BR-29 +13, BR-30 +18, BR-37 +16, BR-173 +12 = **+109**; BR-104 +5, BR-107 +4, BR-109's doors +7; BR-85's code half +3 (follow-up) | the same **+109**, and BR-104/107 and BR-85's +3 | BR-8 +38, BR-173 +12 conventional; BR-85's +3 | - | DESK.DRV +5 |

Read the totals against the owner's brief ("not huge byte growths") and the
budgets, not against a rung. **After the owner's answers (2026-11-08), about
341 resident bytes on kern_big and 220 on kern_small buy every high and medium
defect the register still takes** (they were ~+508 and ~+372; the first
register said ~+420 and ~+300). The fall is the owner's two principles, not a
re-pricing: the six package-argument fences and BR-8's bound are not taken
(packages are trusted: -109 on each kernel), and BR-9's panel goes behind a
knob (a diagnostic for a failure nobody has met: -55 kern_big, -40
kern_small), with BR-80's tripwires, BR-82's canary and BR-68's counter
following it out of the "all" sums, and BR-85's +3 code half held under the
same trust ruling (follow-up). What the core still carries is real
defects with a mechanism: BR-7's in-place guards, BR-12's kernel-tag
refinement, BR-176 and kern_small's VOL_STAT stub among them.
kern_small's figure is still the one to argue about, since its budget is 64KB
resident; batches D and E now carry most of it, and they are defects that
corrupt data on exactly that machine (BR-2, BR-7, BR-17). Several items
reclaim bytes and can pay inside their batch: BR-27 (-12), BR-201 (-2), BR-185
and BR-186 (-43 .mega), BR-204 (-10 .mega), BR-69 (-12 module), and route (i)
of BR-4 at its low end; boot-7, BR-110 and loader-8 reclaim too, but are
phase1's to count. The figure that decided scheduling is no longer a wall:
kern_big's blob (4 bytes free) and boot overlay (1 free) are asked for
+45..+109 and +62..+79, and BOOT3-PLAN's waves 1 and 3 supply about 508 and 176
without a resident byte or a 14th sector (owner, 2026-11-08).

## Source finding index

Every non-refuted bug and hazard finding, and every other-kind finding used as
a defect, with the item it is merged into. Area reviews first, then the gap
reviews.

| source | BR | source | BR | source | BR | source | BR |
|---|---|---|---|---|---|---|---|
| abi-1 | 43 | dw-1 | 23 | input-1 | 18 | mem-1 | 35 |
| abi-2 | 62 | dw-2 | 17 | input-2 | 19 | mem-2 | 12 |
| abi-3 | 62 | dw-3 | 24 | input-3 | 20 | mem-3 | 53 |
| abi-4 | 146 | dw-4 | 22 | input-4 | 87 | mem-4 | 11 |
| abi-5 | 145 | dw-5 | 133 | input-5 | 88 | mem-5 | 105 |
| abi-7 | 73 | dw-6 | 134 | input-6 | 89 | mem-6 | 104 |
| abi-8 | 22 | dw-7 | 135 | input-7 | 90 | mem-7 | 52 |
| boot-1 | 1 | dw-8 | 62 | input-8 | 91 | mem-8 | 54 |
| boot-2 | 1 | dw-11 | 136 | input-9 | 92 | mem-10 | 4 |
| boot-3 | 63 | dw-12 | 60 | input-11 | 93 | mem-19 | 106 |
| boot-4 | 153 | dw-18 | 137 | input-14 | 94 | sched-1 | 51 |
| boot-5 | 25 | dw-19 | 143 | isr-2 | 36 | sched-2 | 79 |
| boot-6 | 25 | engsys-1 | 74 | isr-3 | 85 | sched-3 | 80 |
| boot-11 | 155 | engsys-2 | 75 | isr-4 | 86 | sched-4 | 78 |
| boot-12 | 156 | engsys-8 | 76 | isr-6 | 34 | sched-5 | 78 |
| boot-15 | 156 | engsys-12 | 171 | isr-7 | 21 | sched-6 | 81 |
| boot-18 | 154 | engsys-15 | 172 | isr-8 | 9 | sched-7 | 82 |
| dr-1 | 25 | files-1 | 2 | isr-9 | 82 | sched-8 | 83 |
| dr-2 | 37 | files-2 | 27 | isr-17 | 34 | sched-9 | 84 |
| dr-3 | 61 | files-3 | 41 | loader-1 | 7 | sched-18 | 80 |
| dr-4 | 16 | files-4 | 11 | loader-2 | 7 | shell-1 | 42 |
| dr-7 | 138 | files-6 | 123 | loader-3 | 7 | shell-6 | 117 |
| dr-8 | 139 | files-7 | 124 | loader-4 | 29 | shell-7 | 119 |
| dr-9 | 140 | files-8 | 125 | loader-5 | 30 | shell-11 | 118 |
| dr-12 | 61 | files-9 | 126 | loader-7 | 120 | shell-12 | 119 |
| dr-13 | 25 | files-10 | 127 | loader-8 | 11 | sysmisc-1 | 15 |
| dr-14 | 141 | files-12 | 128 | loader-9 | 121 | sysmisc-2 | 44 |
| dr-15 | 142 | files-13 | 129 | loader-10 | 122 | sysmisc-3 | 26 |
| dr-17 | 142 | files-14 | 130 | mc-1 | 13 | sysmisc-4 | 157 |
| drivers-1 | 5 | gfx-1 | 3 | mc-2 | 65 | sysmisc-5 | 158 |
| drivers-2 | 31 | gfx-2 | 64 | mc-3 | 161 | sysmisc-6 | 159 |
| drivers-3 | 32 | gfx-5 | 77 | mc-4 | 66 | sysmisc-7 | 160 |
| drivers-4 | 33 | gfx-7 | 152 | mc-5 | 66 | text-1 | 46 |
| drivers-6 | 34 | hiber-1 | 96 | mc-6 | 162 | text-3 | 77 |
| drivers-7 | 39 | hiber-2 | 38 | mc-7 | 163 | text-4 | 131 |
| drivers-8 | 144 | hiber-3 | 39 | mc-8 | 164 | text-5 | 132 |
| histsys-1 | 4 | hiber-4 | 40 | mc-9 | 67 | text-11 | 129 |
| histsys-2 | 9 | hiber-5 | 97 | mc-10 | 68 | ui-1 | 45 |
| histsys-6 | 95 | hiber-6 | 98 | mc-12 | 165 | ui-2 | 28 |
| histsys-11 | 8 | hiber-7 | 10 | mc-13 | 165 | ui-3 | 110 |
| histsys-15 | 78 | hiber-8 | 99 | mc-14 | 165 | ui-4 | 111 |
| hm-1 | 12 | hiber-9 | 100 | mc-15 | 166 | ui-5 | 112 |
| hm-2 | 104 | hiber-10 | 101 | mc-17 | 167 | ui-6 | 113 |
| hm-3 | 4 | hiber-11 | 101 | mc-18 | 167 | ui-7 | 86 |
| hm-4 | 35 | hiber-12 | 72 | md-1 | 69 | ui-8 | 22 |
| hm-5 | 54 | hiber-16 | 102 | md-2 | 70 | ui-9 | 115 |
| hm-6 | 105 | hiber-18 | 103 | md-3 | 71 | ui-16 | 114 |
| hm-7 | 59 | hm-8 | 53 | md-4 | 168 | ui-17 | 116 |
| hm-13 | 11 | hm-14 | 107 | md-9 | 169 | video-1 | 14 |
| md-10 | 71 | md-11 | 169 | md-12 | 170 | video-2 | 6 |
| md-16 | 105 | md-17 | 170 | md-18 | 170 | video-3 | 55 |
| video-4 | 56 | video-5 | 57 | video-6 | 58 | video-8 | 147 |
| video-9 | 148 | video-10 | 149 | video-11 | 150 | video-12 | 151 |
| wm-1 | 8 | wm-3 | 47 | wm-4 | 48 | wm-5 | 49 |
| wm-6 | 50 | wm-7 | 109 | wm-8 | 78 | wm-9 | 108 |
| wm-13 | 8 | | | | | | |

The `mc-N` above are megacore's. The gap reviews' (`cursor/` and `os88ui/` as
explained under *How to read it*):

| source | BR | source | BR | source | BR | source | BR |
|---|---|---|---|---|---|---|---|
| ctx-1 | 4 | gfce-6 | 44 | hdk-6 | 200 | xm-2 | 181 |
| ctx-2 | 173 | gfce-7 | 189 | hdk-7 | 199 | xm-3 | 206 |
| ctx-4 | 178 | gi-1 | 179 | hdk-8 | 201 | xm-4 | 207 |
| ctx-5 | 85 | gi-2 | 191 | hdk-13 | 202 | xm-5 | 208 |
| ctx-6 | 188 | gi-3 | 191 | hdk-14 | 203 | xm-6 | 209 |
| ctx-7 | 22 | gi-4 | 190 | cursor/mc-2 | 194 | xm-7 | 210 |
| ctx-8 | 80 | gi-6 | 62 | cursor/mc-3 | 175 | xm-9 | 211 |
| gfce-1 | 9 | gi-7 | 192 | cursor/mc-5 | 174 | xm-12 | 22 |
| gfce-2 | 9 | gi-9 | 193 | cursor/mc-6 | 195 | ul-6 | 212 |
| gfce-3 | 9 | gkm-1 | 32, 38 | cursor/mc-9 | 174 | ul-10 | 178 |
| gfce-5 | 8 | gkm-2 | 62, 14 | os88ui/F1 | 176 | gms-1 | 184 |
| hdk-1 | 182 | hdk-3 | 183 | os88ui/F2 | 177 | gms-2 | 185 |
| hdk-2 | 197 | hdk-4 | 198 | os88ui/F5 | 196 | gms-3 | 186 |
| hdk-5 | 199 | xm-1 | 180 | os88ui/F8 | 176 | gms-4 | 187 |
| gms-5 | 204 | gms-6 | 205 | gms-10 | 66 | | |

Context, not defects, cited above: boot-7 (the room BR-1 and BR-25 need;
phase1 WP6's), boot-10 (BR-1's rethink), wm-2 (BR-8's alternative), wm-17
(BR-47's rethink), isr-13 (BR-34's flag-day cell), sysmisc-14 (BR-26's rethink
R1), mem-14 (BR-54's rethink), gfce-9 (APICHK, the ruling's alternative),
gkm-3 (the small- port lane, in BR-62), ul-11 (the predicted-figure list,
applied throughout), cursor/mc-R2 (BR-175's structural fix), gms-R2 (BR-186's
better fix), xm-8 (the room BR-206 and BR-211 compete for), rt-5 (KERN_BUDGET is
a VGA reading, in BR-175).

Read and not carried as defects (size, rethink, prior-art, flag-day
reconciliation or tooling, each the concern of another synthesis file): ctx-3,
ctx-9; gfce-8, gfce-10, gfce-11; gfr-1..gfr-10; gi-5, gi-8 (its sb_now part
refuted), gi-10, gi-11; gkm-4..gkm-11; gms-8, gms-9, gms-R1, gms-R3;
cursor/mc-1, mc-4, mc-7, mc-8, mc-R1, mc-R3; os88ui/F3, F4, F6, F7, R1;
hdk-9..hdk-12; rt-1..rt-4, rt-6..rt-9; ul-1..ul-5, ul-7..ul-9; xm-10, xm-11.

Refuted and dropped: isr-5, wm-20 (area reviews); gfce-4, gms-7 (gap reviews).
