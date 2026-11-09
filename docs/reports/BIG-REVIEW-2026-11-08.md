# The big kernel review, 2026-11-08

**Commit reviewed:** `origin/elendilon` at `5e4740bb` ("Merge branch
'mega-release' into elendilon"). The review's reports and syntheses live on the
branch `the-big-review`, cut from that commit.

This is a dated measurement of one tree, not a maintained document. SPEC.md and
the code stay the authority. Every line number and byte figure below can move.

**After the review (owner and follow-up, 2026-11-08).** The owner has answered
section 10 in two rounds on the same day. The answers, in the owner's words
where it matters, are `big-review-2026-11-08/owner-decisions.md`, and they
bind; where the second round revisits a first-round answer (the API level),
the second round is the one in force. Five
follow-ups re-checked what the answers turned on, all under
`docs/reports/big-review-2026-11-08/`:

- `followup-canary.md`: BR-1 corrected (inert since 2026-09-05, part 1 of the
  fix costs 0 bytes, the harm is crossing READS);
- `followup-purgeable.md`: package-purgeable claims are not needed now;
- `followup-decisions-6-8.md`: rt-7, text-7 and rt-5 explained and re-priced;
- `followup-already-exists.md`: six recommendations withdrawn or reworded, five
  that missed an existing piece;
- `areas/gap-bss-zeroing.md`: `.bss` is zero on every shipped path since PR #61.

The design that replaces the refused 14th blob sector is
`docs/plans/completed/BOOT3-PLAN.md`. Every passage below that one of these changed is
marked "(owner, 2026-11-08)" or "(follow-up, 2026-11-08)"; section 10 is now
split into what is decided and what is still open. The plan,
`docs/plans/BIG-REVIEW-PLAN.md`, carries the same changes as sessions.

## 1. Verdict

The kernel's mechanisms are, area by area, better than the period systems they
are measured against, and better for this machine rather than in the abstract.
The examples are direct far calls into a fixed table with no fixups, an
out-of-band claim map, relocation by callback with a pinned default, a UI task
that really halts with the lost wakeup closed by construction, a FAT commit
order that survives a power cut (DOS did not), and a drawing core priced
against the 8088's fetch floor.

The defects have one shape. A problem is **solved once, very well, at the site
of the field report that found it, and the rule then reaches the neighbouring
site only through prose and care.** Most of the 212 registered defects are the
kernel's own best pattern missing one routine away:
- the truncate arm has the divide guard and append/READ_AT do not;
- `gfx_blitp` has the sign fix and `gfx_blit4` does not;
- kern_mega releases a dying task's lock and kern_big does not.

So the cheapest high-value work in this tree is not new design. It is copying
the kernel's own patterns to the sites they have not reached, and turning the
rules that are held by care into gates. The owner's first reading agreed:
"most of the agents seemed to like our current structure, overall" (owner,
2026-11-08).

The live risks that matter most:
- the boot canary protects one of the six shipped system disks (BR-1). On
  the 360KB and 720KB disks it has been inert since the packed kernel became
  the default on 2026-09-05, and the half of the fix that re-arms it costs 0
  bytes (follow-up, 2026-11-08);
- a kern_small shed during a mount writes the directory listing into the FAT
  window (BR-2);
- `gfx_blit4` overwrites 64KB of heap on a negative width, and the fix is 0
  bytes (BR-3);
- a dying worker tears down and repaints on its own 128-byte slice (BR-4);
- nothing handles INT 0, so one divide overflow kills the machine on the 1981
  IBM ROM, re-executes for ever on a 286+, and continues with garbage on the
  10/27/82 ROM and GLaBIOS (BR-9). No machine has met it yet, so the
  panel that answers it waits behind a build knob, off in shipped kernels
  (owner, 2026-11-08).

Two facts constrain the plans more than any byte total:
- **kern_big's two transient homes are full.** The blob has 4 bytes free and
  `.ovl` has 1 (MEASURED). The owner refused a 14th blob sector; the route is
  `docs/plans/completed/BOOT3-PLAN.md`, which moves boot-only code into a third section
  at the tail of KERNEL.SYS and frees about 508 blob bytes (its wave 1) and 176
  `.ovl` bytes (its wave 3) for 0 resident (owner, 2026-11-08).
- **Phase 1 is a net resident saving on kern_big only if the owner takes
  WP2's FILECP/FDLG trade.** The owner refused that trade (owner,
  2026-11-08). With it gone, the package fences and the fault panel out of
  the shipped kernels, and the keyboard mouse and text-7 taken (the owner's
  second round), Phase 1 with its bug batches comes to about
  **-0.13..+0.25KB on kern_big** without the KHP round and +0.16..+0.54KB
  with its six recommended rows (ESTIMATED, section 7). Correctness costs
  bytes, and the review's biggest saving is no longer on offer.

The flag day saves about **-1,170 resident on kern_big, -515 on kern_small and
-770 conventional on kern_mega** (ESTIMATED). It moves `PKG_FMT` 10 -> 13 and
`DRV_VER` 12 -> 14.

## 2. How it was done, and how to read the rest

**Method.**

1. **25 deep reviews**, each followed by an adversarial verifier. There were
   19 areas: boot, memory, sched, ui, wm, gfx, video, text, input, diskread,
   diskwrite, loader, drivers, files, hiber, shell, sysmisc, megacore and
   megados. There were 6 lenses: abi, histmem, histsys, isr, bytes and
   engsys. They produced **469 findings; the verifiers refuted 2 (isr-5,
   wm-20), leaving 467.** Of those, 54 were left unchecked: the verifier did
   not re-derive them. Each is marked where it is cited.
2. **2 completeness critics** named 12 gaps. Each gap got a review and a
   verifier. That added 132 findings; 2 were refuted (gfce-4, gms-7).
3. **5 cross-area synthesists** wrote the bug register, the Phase 1 plan, the
   flag-day plan, the systemic view and the strengths register.
4. **A 3-lens challenge panel** (truth, cost, fit) read the top 40
   recommendations. None was refuted outright. Truth amended 30 and let 10
   stand; cost amended 33 and let 7 stand; fit amended 37 and let 3 stand.
5. **4 revision writers** folded the gap reviews and the lens verdicts into the
   final syntheses.

**What was not done.** Nothing was assembled into `build/` for any finding, and
nothing was booted or soaked. This was a read-only review. Host builds were used
only to prove that the documentation pass changed no shipped byte. One gap
reviewer (`gap-unchecked-loadbearing`) assembled `kernel/kernel.asm` into a
scratch directory, to read a listing.

**Conventions.**
- **Bytes** are signed and name their section: `.text`, `.bss` and `.cold` are
  RESIDENT (`.cold` is a cold path, not a cold lifetime); `.boot2` (the blob)
  and `.ovl` are transient; *module* is a `.DRV` image; *conv* is kern_mega's
  conventional memory, whose `.cold` and `.mega` run above 1MB.
- **MEASURED** means a span read off `build/kernel.map`,
  `build/smallk/kernel.map` or `build/megak/kernel.map`, or a figure a row or
  the field produced. Every other byte figure is ESTIMATED from encodings, and
  every time or cycle figure is PREDICTED unless it says MEASURED.
- **Corrections win.** Where a verifier or a lens corrected a figure, only the
  corrected figure appears here.
- **Ids.**
  - Findings are `area-N` (`dr-` diskread, `dw-` diskwrite, `hm-` histmem,
    `mc-` megacore, `md-` megados, `mem-` memory).
  - Gap ids: `ctx-` context/lock model, `rt-` residency tiers, `gkm-` kernel
    matrix, `gfce-` fault containment, `gi-` API shape and includes, `gfr-`
    flag-day reconciliation, `ul-` unchecked load-bearing, `xm-` XMEM, `hdk-`
    hard-disk face, `gms-` mega unowned spans, `mcur-` mouse cursor, `ouk-`
    os88ui kernel copy.
  - Plan ids: `BR-n` is a bug-register item, `WPn` a Phase 1 work package,
    `Kn` a strength.
  - Flag-day rows keep ABI-BREAK-LIST's letters (T5, R14, L1, ...).
  - Themes are numbered 1-13.

**The file map** (`docs/reports/big-review-2026-11-08/`):

| path | what it is | read it for |
|---|---|---|
| `synth/bugs.md` | the final bug and hazard register: 212 items (BR-1..BR-212), 9 fix batches A-I, per-kernel batch sums, cross-cutting constraints | any defect, its mechanism, fix, bytes and rows |
| `synth/phase1.md` | the final Phase 1 plan: WP1-WP34, who owns each finding, the transient-home ledger, the order of work | what to do now, with no break |
| `synth/flagday.md` | the final Phase 2 plan: existing rows re-judged, new rows, the bundle and its net | the flag day |
| `synth/abilist-section.md` | the text to append to `docs/plans/ABI-BREAK-LIST.md` as its section 13 | the list's new rows |
| `synth/systemic.md` | 13 themes ranked by value, with the period-OS comparison per theme and what not to copy | "would this be better done differently" |
| `synth/keep.md` | the strengths register (K1-K23), the 25-area scorecard, and what is already solved on one kernel | what to protect and copy |
| `synth/challenge-{truth,cost,fit}.json` | the three lenses' verdicts and notes over the top 40 | why a figure was corrected |
| `synth/doc-owner-questions.md` | the seven concerns the doc pass could not settle as a text fix | which comments record a live defect or a rule to decide |
| `areas/*.md` | the 25 first-round reports and the 12 gap reports | evidence, code citations, rethinks |
| `data/*.json` | the verified findings, strengths and doc fixes behind each report | the verdict on any one finding |
| `owner-decisions.md` | the owner's two rounds of answers, 2026-11-08 (binding) | what is decided (section 10) |
| `followup-*.md` | the follow-ups the answers asked for: canary, purgeable, decisions 6-8, already-exists | the corrected figures marked "(follow-up, 2026-11-08)" |

## 3. Where we stand

### 3.1 The scorecard

*Code* and *docs* are each reviewer's verdict as verified. *Biggest risk* is
the highest-severity finding that survived. Every one of the 25 reports found
comments and SPEC text describing retired designs as current.

| area | code | docs | biggest risk |
|---|---|---|---|
| abi | the call surface's mechanics are the best part of the kernel | contracts kept in up to four places, which disagree (abi-10) | no runtime API level: a new package on an old kernel calls past the table (abi-6; WP9). The Wire already fences the packages it serves, so the gap is a hand-copied package (follow-up, 2026-11-08); the owner: take the level if it is cheap |
| boot | one of the best-reasoned parts; boot-only code costs no resident byte | 40+ comments state history as current values | canary inert on four of the six system disks since 2026-09-05, and the splash's first tick runs code ahead of it on three (BR-1) |
| bytes | close to the floor of routine-by-routine work | SPEC.md 2.6 and KERNEL-MEMORY.md tables stale | no live defect; the remaining levers are structural (theme 7, T5) |
| diskread | one transfer routine, divide safety by arithmetic, a measured cache | the worst drift found: headers of deleted routines, SPEC 18's mount sequence (dr-16) | the boot partition is mounted with AH=08h geometry but addressed with the BPB's; FIELD-NOTES 33 is still live (dr-1, BR-25) |
| diskwrite | the best-argued subsystem: one commit order, shadow replace | SPEC 18.4 describes a kernel several passes old | hostile FileSize divides (BR-23); the multi-window FAT16 chain leak (BR-17) |
| drivers | one of the most carefully built parts; very little resident | snd.inc's banner describes a speaker-only kernel | one global worker count stalls every unload (BR-5) |
| engsys | 636 `%error`s, budgets derived from a stated rule, the ABI read from the binary | SPEC.md's front matter, read first, is the most wrong | a citation that resolves to the wrong section passes (BR-75); review reports failed `make` (BR-74) |
| files | the most mature subsystem | SPEC 22 and 54 describe the retired global listing | a kern_small shed inside a mount (BR-2) |
| gfx | inner loops priced for the 8088; refusal with fallbacks | comments describe two dozen things that no longer exist | `gfx_blit4`'s negative width (BR-3, 0 bytes to fix) |
| hiber | a tiny resident part; a crash-consistent commit | SPEC 87.4-87.7 and 51.11.4 drift | the wake repairs devices one by one instead of restoring masks; Esc discards the session (BR-10) |
| histmem | the memory model is sound, in places better than every peer | retired names still cited | disciplines stop at a neighbour: the IF=0 regrow copy (BR-12), a module dropped on a worker (BR-4) |
| histsys | several pieces beat every peer | SPEC 7's "exactly two" `sch_lock` raisers | mechanisms run on the wrong context (BR-4); no INT 0 handler (BR-9) |
| input | one of the strongest; every rule traces to a named field machine | about 30 places: folded names, moved constants | a tick can park a ROM int 09h with IRQ1 in service (BR-19) |
| isr | careful; the typical defect is no longer a missing `cli` | SPEC names the wrong longest IF=0 window | the driver and package doors miss preconditions the ISRs meet (BR-36); no latency budget (BR-178) |
| loader | one of the best-engineered corners | SPEC 21 and 29 describe the v2 pool | an image shorter than its prefix writes ~64KB past the region, reachable from network bytes (BR-7) |
| megacore | the model is right; a better fault model than Windows 3.0's UAE | the header and BOCHS.md describe the retired static GDT | a reflection on the lent stack reads `.lowbss` through SS (BR-13) |
| megados | the park is among the best code in the tree | SPEC 87.9's retired resume route; `t_mirror` misses `kerndos/` | rarely run recovery edges (BR-69, BR-71); ~1KB of dead resume per image (md-6) |
| memory | the best-argued subsystem by its own reviewer's measure | orphaned headers of inlined routines | interrupt latency (BR-12, BR-54); `MEM_OWNER_MAX` caps the kernel's tags (BR-35) |
| sched | invariants checked rather than trusted | the SDK's "worker DS = KERNEL_SEG" | small defects (BR-51); systemically, nothing can wait except on time (sched-15) |
| shell | one reconciler; every pixel written once | a long tail of comments older than size pass 14 | the Sticky App library can be freed under a widget (BR-42); a module load per gesture (shell-2) |
| sysmisc | structurally sound; almost entirely non-resident | SPEC 31/37/64/76/79 describe retired designs | the clock gains ~31 s a day, 0 bytes to fix (BR-15); a hostile 'CH' byte (BR-44) |
| text | the strongest 8088 engineering in its part of the kernel | ~45% of the files is comment, much of it history | latent only (BR-46, text-5) |
| ui | the model is sound | ~2 comment lines per code line; SPEC 13's loop is several redesigns old | About indexes past its table on a VESA desktop (BR-45); SPEC 13.9.2's race is open on three paths (BR-28) |
| video | the geometry core is the area's strongest code | ~40 stale statements in SPEC 39 and 53 | kern_small enables Hercules page 1 beside a CGA (BR-14); an MDA gets Hercules timings (BR-6) |
| wm | the most carefully measured subsystem | 52% comment, much of it per-pass byte history | `wm_ptr2idx` raises INT 0 on a wild pointer (BR-8) |

**Across it:** the code is rated sound to excellent in all 25 areas, and the
documentation is the weak column in all 25. Section 9 covers that.

### 3.2 What is genuinely good, and must be protected

These are the ten strongest patterns from `synth/keep.md`, ranked by how much of
the kernel they hold up. *Gate* says what stops a change from breaking the
pattern today. "Prose only" means nothing but review does.

| | pattern | where it is exemplary | gate today | where it has not spread (finding) |
|---|---|---|---|---|
| K1 | hostile input bounded where it is read; every risky `div` preceded by the compare that proves it safe | the BPB's 17-rule table with per-call quotient guards (`kernel/disk.inc:5091-5217`, SPEC.md 18.2); the loader's carry fence; `toast.inc`'s bounded scan | **prose only**; proposed: gfce-3's divide registry, `t_divsites` (WP8) | dw-1, loader-1/-3, gfx-1, text-5, wm-1: in all but wm-1 the fix already exists one routine away |
| K2 | facts checked in the artifact that runs: `%error` beside the declaration, the ABI decoded from the binary | 636 `%error` sites; `t_api_abi`; `abilock` (258 recorded decisions, 5 bumps); `t_mirror`; `os88sym`'s byte-identical rule | the assembler, `t_api_abi`, `t_abilock`, `t_mirror`, `t_kdfar`, `t_kdapi` | `t_api_abi` decodes one kernel of four (abi-3, gkm-2); `t_mirror` misses `kerndos/` and `os88.h` (md-5, engsys-11) |
| K3 | one source of truth, expanded wherever it is needed | `SCH_PARTITION` (`sched.inc:198-269`); `CFG_MAP`/`CFG_DATA` emitted into the boot overlay and CTRL.DRV; `vid_tab`; `hbm_ptrfix`; `kernel/mouproto.inc` shared with kern_dos | partly (`t_mirror`, the INDEX generator); copies of code are prose only | the dock painter copied into DOCK.DRV and diverged (shell-4); `ld_unreserve` a drifted twin (loader-8); A20 code written six times (md-21, unchecked) |
| K4 | one door owns the dispatch, the transfer, or the end of a transaction | `wm_pkgcall`, `ui_bill`, `dsk_xfer`, `gfx_unlock` (EndPaint without an explicit release), `mou_apply`, `drv_call` | prose only (`t_btnrules` and `t_chromesites` are the models) | three callback paths read the handler before the lock (ui-2, BR-28); door-by-door window-pointer checks (wm-13) |
| K5 | post, do not do: work moves to the context that owns it, and a forgotten post is late, never lost | `[ui_post]`'s xchg with 26 setters, re-set by the tick; the deferred close; `fdlg_reap`; kern_mega's `mega_fkq` | prose only (`t_wakedrain`, `t_drvclaim` are partial) | teardown is the one kind of work not posted (BR-4, ctx-1); a module dropped on a worker (hm-3) |
| K6 | publication by one store; the lost wakeup closed inside one IF=0 window | `T_STATE` stored last at spawn; the UI task's `[sch_uiwake]` test and `task_sleep` in one `cli` span (96.9% halted at idle, MEASURED); `gfx_lock`'s flag and owner in one word, with the fairness debt that took press-to-menu from 1.4-14.7 s to 37-70 ms | prose for the ordering; `%error` adjacency guards hold the word pairs | `evq_push` wakes before the record is in the ring (sched-6, 0 bytes); four IMR read-modify-writes at IF=1 (isr-6) |
| K7 | commit order: the deciding record written last and removed first | shadow replace (`diskw.inc:1576-1597`), one order through mkdir, delete, rmtree, truncate, fsck and defrag; hibernate's pointer written last; the cloner's LBA 0 last | prose only (soak rows exercise it; no host check reads the order) | the multi-window FAT16 leak (BR-17); Write Img over a short disk (hiber-10/11); unknown SYSTEM.CFG records dropped (drivers-17) |
| K8 | teardown by attribution: the kernel believes nothing the package says | the loader's `.abort`/`.sweep`; `mem_own` taking the owner from the stub's ES; windows freed before claims; `mod_gone` = `stc / retf` | partly (`t_pins`, `t_movable`, `t_moddrop`) | the driver layer: one global `drv_wcnt` (BR-5), nothing counts callers inside a driver (BR-31), IRQ vectors not checked at free (drivers-6) |
| K9 | one predicate decides both the grey and the refusal; refusal is a contract | `hb_pick`, `fsx_capstab`, CF=1 stubs for cells kern_small lacks, `gfx_blitp`'s probe bit | prose only (SPEC.md 47) | no API level (abi-6); grey and refusal disagree in video-12, video-13, files-3 |
| K10 | pay only while used: on-demand modules behind a one-compare resident gate; boot-only code at the heap floor | the blob rejoining the free run (better than DOS's SYSINIT); `cp_tick_due_x`; `fdlg_hook`'s compare, which replaced a far call measured at 1.12% of the machine; the `.vga` slide | **held**: `t_moddrop`, `tests/vgarefs.txt`, `t_coldsym`, `t_noovl`, `t_bootfloor`, `t_resident`, `KERN_BUDGET` | the review's largest byte levers (theme 7) |

Other patterns that must not be lost:
- private ISR stacks that set SS as well as SP (K11);
- instruments that ship with the design: the DD block, the 0xCC fill and
  canary, the KFZ heartbeat (K12);
- probes that verify the transfer rather than the table (K13);
- tables versioned by their own length, and header bytes that must already be
  zero (K14);
- plan-before-copy compaction (K15);
- the heap's out-of-band record table with relocation by callback (K16);
- every pixel written once (K17);
- fsck-style stateless bounded steps (K18);
- the same-build rule (K19);
- build-matrix hygiene (K20);
- cell shape following heat: the inline cell at 52-57 cycles against 159-162
  for a SLOT, MEASURED (K21);
- state that can only be stale in the safe direction (K22);
- the engineering system's own habits: derived budgets, a checked index, a
  harness that prints want/got (K23).

**Already solved on one kernel: copy it home.** kern_mega was built later and
more defensively. These are the cheapest copies in the tree, because the design
is already proven (`synth/keep.md` Part 3):

| kern_mega already does | kern_big/kern_small still | finding | bytes |
|---|---|---|---|
| releases a lock the dying task held (`mega_fkill`) | deadlock when a worker dies holding the gfx lock | BR-173 | +12 `.text`, -24 `.mega`; not taken as retail code: a package fence (owner, 2026-11-08) |
| posts callback-fault teardown to `ui_task` | tear down on the dying worker's slice | BR-4 | -6..+50 by route |
| closes the faulting package on a CPU fault | hand INT 0 to the BIOS dummy handler | BR-9, gfce-2 | ~+55 big / ~+40 small for the halting panel (BR-9); closing the package is gfce-2's +45..+65 more. Decided: kern_mega keeps closing the package; the real-mode panel waits behind a build knob, off in shipped kernels (owner, 2026-11-08) |
| restores the image's own 8259 masks at wake | wake with the fresh boot's masks | hiber-1, hiber-14 | +10..+14 HIBER.DRV |
| unwinds the DOS-handoff refusals | leak the extent claim | BR-38 | +20 HIBER.DRV |
| ships the XT redraw knobs RELIT/DMGSTALE/CAPTION/RAISEOL | run without them on the XT they were measured for | WP11 | owner's round |
| guards the dual-card Hercules page | kern_small enables page 1 beside a CGA | BR-14 | +13 kern_small |

The rule gkm-1 proposes is right: **a fix in shared code lands on every kernel
that carries the code, in the same commit, unless that kernel is the reason
for the fix.**

**Deliberate designs to leave alone.** The reviewers checked these and
endorsed them:
- `cpu_detect`'s no-override `[bp+0]` x87 encoding;
- `spl_ifp`;
- explicit lazy parts rather than automatic reload;
- `mem_find_own`'s self-region widening (tighten FREE and REGROW only);
- no run-time unification of the kernels (+5,440 to +14,700 bytes, gkm-5);
- no flattening of kern_small to one code segment;
- the two-segment tax of ~700 bytes on kern_big and ~540 on kern_small;
- no single handle model;
- Pause stopping the BIOS tick on an XT;
- the parity pads.

## 4. What could use work

### 4.1 The defects that matter

These are the top of `synth/bugs.md`, ranked by severity × likelihood on real
machines × blast radius. Ties go to the shipped kernels. Rows 1-15 are the
register's ranked fifteen; rows 16-23 are its "close behind" list, which it
does not rank among themselves. The bytes are the register's figures as
corrected.

| # | BR | defect | kernels | fix bytes | batch |
|---|---|---|---|---|---|
| 1 | BR-1 | The boot canary is inert on kern_big's 360/720KB and kern_small's 360/720KB disks, and the splash's first tick runs `.text` ahead of it on kern_small 360/720 and kern_big 1.2MB. Only kern_big 1.44MB is protected. `t_canary` passed because it modelled the runs from the wrong origin. **Corrected (follow-up, 2026-11-08):** inert on the 9-sector disks since **2026-09-05** (`175926d5`, KZIP the default, which read the plain head in runs of its own), not since the 12 -> 13 blob change, which re-armed 1.2MB and 1.44MB. The harm is a crossing **read** that returns the other head's sectors (cylinder runs are reads only, SPEC.md 18.91.3), not a write. The Pravetz record is the int 15h freeze (#343), not a cylinder-read report | big, small | 0 resident. **Part 1** (KSIG_OFF 22528, the segment arm dropped): **0** `.boot2` big and mega, **-11** small. **Part 2** (the head at the track bound): +12 `.boot2` on each kernel, big in BOOT3's room; +1 int 13h per 8088 boot on small360/720, 1.2MB and 1.44MB, none on kern_big 360/720 (PREDICTED). Part 1 and the `t_canary` rewrite are urgent | A |
| 2 | BR-2 | kern_small: a shed during a Disk-window mount hands the listing's destination to the read-ahead or FAT window, and the next write puts it on the disk | small | +9..+10 `.cold` | D |
| 3 | BR-3 | `gfx_blit4` trusts the sign of its width: 64KB from the 336-byte `vga_pbuf` into the heap on VGA, ~8KB a row on 1bpp. `gfx_blitp` already has the fix | all | **0** (`jz` -> `jle`, same encoding) | B |
| 4 | BR-4 | A dying worker runs `gfx_lock`, `wm_destroy` and its repaint chain, and `desk_drop_x`, on its own 128/192-byte slice; DESK.DRV can be freed under the UI task. A posted close as first written does not work (`app_close_win` re-hides a state-2 record) | all | -6..+50 by route (+22 big / +27 small in the batch) | C |
| 5 | BR-9 | No divide-error or invalid-opcode handler. The 1981 IBM ROM masks the 8259 (a dead machine); the 10/27/82 ROM and GLaBIOS continue with garbage in AX; a 286+ re-executes the `div` for ever | big, small | ~+55 big / ~+40 small resident, +12..+20 `.ovl` (no room today). **Decided: behind a build knob, off in shipped kernels, 0 shipped bytes**, until a field report says it is being hit (owner, 2026-11-08) | F (owned by WP8) |
| 6 | BR-6 | A plain MDA primary is programmed with Hercules graphics CRTC timings: ~33.4 kHz horizontal against the 5151's 18.4 kHz. An MDA+CGA machine leaves the usable CGA idle. **Decided: os8088 does not run on an MDA; an MDA alone prints a message and halts, and an MDA beside a usable CGA switches to the CGA** (owner, 2026-11-08: "this can damage the monitor") | big, small, mega | +60..+110 `.boot2` big/mega, +25..+65 small. On kern_big it is paid from BOOT3-PLAN's wave 1 room, in `vid_detect`'s first-tick arm; no 14th sector (owner, 2026-11-08) | A |
| 7 | BR-7 | The loader trusts a package header against its own region. An image shorter than its clear prefix wraps the decoder's bound and writes ~64KB past the region; The Wire feeds this path network bytes | all | +32..+36 `.cold` (0 conv on mega). Kept: a fence on a damaged FILE, which `synth/bugs.md` puts outside the packages-are-trusted class | D |
| 8 | BR-8 | `wm_ptr2idx` divides an unvalidated window pointer (INT 0). A bound alone trades the dead machine for a write into another package's window, so the door refusals go with it | all | +1..+2 the bound; +29..+46 with the doors. **Not taken as retail code**: the doors are a package fence, and the bound goes only with them (owner, 2026-11-08) | C (owned by WP8) |
| 9 | BR-10 | Esc on the Resume face deletes the hibernated session without asking | big | +0..+4 HIBER.DRV | G |
| 10 | BR-11 | The purge floor is one word, stamped with a task that names nobody and lifted by any teardown: a paste can stop part-way and report success | big, small | +8..+18 `.cold` | D |
| 11 | BR-12 | `mem_regrow`'s path-3 move copies the whole block at IF=0, where the compactor copies the same bytes at IF=1 (~86 ms per 32KB, ~170 ms per 64KB, PREDICTED) | big, small | +10..+16 `.cold` | D |
| 12 | BR-180 | kern_big does not own int 15h: a correct pre-XMS DOS program can `AH=87h` over the XMS pool and the HMA, or shut A20 | big (286+) | +45..+60 DOS.O88, 0 kernel | I |
| 13 | BR-13 | kern_mega: a reflection on the lent stack reads `.lowbss` through SS, a #SS halt on any limit-checking CPU (Bochs, not QEMU, shows it) | mega | +16..+40 above 1MB | H |
| 14 | BR-14 | kern_small's `fsx_page` enables Hercules page 1 on a machine that also has a CGA, because the dual-card guard is `%ifdef KERN_BIG` | small | +13 small, +2 big | B |
| 15 | BR-15 | The system clock gains about 31 s a day | all | 0 | G |
| 16 | BR-5 | One global `drv_wcnt`: with USBMOUSE's worker running (the Book8088/CH375 arm), every other driver's unload stalls ~5 s and leaves it detached but still published. This is NO-PINNING K1b, whose stated condition is now met | big, mega | +20..+35 `.cold`, +11 `.bss` | G |
| 17 | BR-173 | A worker that dies holding the gfx lock deadlocks the desktop (`gfx_lock`'s retry never compares the owner) | big, small | +12 `.text`; not taken as retail code (owner, 2026-11-08) | C |
| 18 | BR-181 | XMEM.DRV's tier-2 copy never re-proves A20 | big (386) | ~+2 XMEM.DRV | I |
| 19 | BR-182 | `HDV_ARM` redirects every rung-1 read, the kernel's own FAT reads included, and an arm outlives its owner | big | disarm unpriced; its DSV_BLK tag is flag day | I |
| 20 | BR-16 | Media-changed status 06h, the AT's free disk-swap signal, is retried as a generic error and thrown away | all | +16 `.cold` | E |
| 21 | BR-17 | A failed write on a multi-window FAT16 volume leaks its half-built chain; on Disk full, that is all free space | big, small | +12 big / +9 small `.cold` | E |
| 22 | BR-18 | A 16550 whose FIFO is already on is treated as an 8250, which silences the mouse | big | +6 `.ovl` | F |
| 23 | BR-19 | A tick can park a ROM int 09h with IRQ1 still in service (the int 15h and kern_mega paths SPEC.md 9.8.3 left unmasked) | big, mega | +14..+16 `.text` | F |

**The batches** (`synth/bugs.md`, *Fix batches*), core / all, resident:

| batch | kern_big | kern_small | kern_mega conv | transient |
|---|---|---|---|---|
| A boot, detection, the build's gates | 0 / 0 | 0 / 0 | 0 / 0 | `.boot2` +78 big and mega, +27 small (after WP6's -41); +69 and +18 with BR-1 re-priced (follow-up, 2026-11-08); `.ovl` +4 |
| B display, FSX, drawing | +41 / +106 | +21 / +33 | +41 / ~+104 | `.ovl` +8, `.boot2` +7 (big) |
| C WM, UI, instance teardown | +108 / +185 | +92 / +146 | ~+98 / ~+162 | - |
| D loader and heap | +94 / +164 | +104 / +137 | 0 / 0 | - |
| E disk and file layer | +68 / +220 (+90 with BR-60) | +73 / +139 | 0..+9 | `.ovl` +10 |
| F input, ISRs, scheduler | +114 / +175 | +80 / +129 | ~+60 / ~+121 | `.ovl` +31 big |
| G drivers, hibernate, services | +83 / +95 | +2 / +2 | +13 / +13 | `.ovl` +29 |
| H kern_mega | - | - | +20 / +20 | above 1MB +19 / ~+141 |
| I HDD.DRV, XMEM.DRV, DOS.O88's XMS | 0 / 0 | - | - | DOS.O88 +52 / +157; HDD.DRV +12 / +58 |
| **total** | **~+508 / ~+945** | **~+372 / ~+586** | **~+232..+241 / ~+420..+429** | `.ovl` +82 big against 1 byte free |
| **total after the owner's answers** (owner and follow-up, 2026-11-08) | **~+341 / ~+744** | **~+220 / ~+400** | **~+179 / ~+342** | `.ovl` +62 big; `.boot2` A +69 big and mega, +18 small |

Notes on these totals:
- **The owner's "packages are trusted" ruling is applied** (owner,
  2026-11-08). The six package fences (BR-8's doors +36, BR-22 +12, BR-29
  +13, BR-30 +18, BR-37 +16, BR-173 +12: 107 core bytes on each kernel) are
  not taken as retail code; at most they go into an `APICHK`-style knob.
  BR-8's +2 bound goes with its doors (`synth/bugs.md`'s own rule).
- **The fault panel waits behind a build knob** (owner, 2026-11-08): BR-9's
  +55 kern_big / +40 kern_small and its `.ovl` install (+12, +8 for vector 6)
  leave the shipped totals. kern_mega keeps closing the faulting package, as
  it does today.
- **The same weighing, applied by the follow-up** to the other guards whose
  failure no machine has met. Diagnostics, behind knobs with the panel (the
  owner's decision 1): BR-84 (+4, the panel's own table bound); BR-80's
  `task_sleep` guard and `gfx_lock` tripwire (+7 each, or ctx-7's static
  gate); BR-82's ISR-stack canary and its panel (+9..+12, STKDIAG's knob);
  BR-68's `[mega_kfix]` counter (+8 `.mega`, +2 `.bss`); BR-173's optional
  self-take panel. Package fences, held under decision 3: BR-85's code half
  (+3: "latent unless a package IRQ handler calls WAKE, and none was traced",
  and the SDK already states the precondition); BR-107 (+4) and BR-109's optional
  doors, which `synth/bugs.md` calls "the same kind" as the six, take their
  documented form; BR-104 (+5) is uncounted while it stays the owner's open
  item 11. All of them are out
  of the totals above. The fences against hostile FILES (BR-7, BR-23, BR-44,
  BR-59) are NOT in this class and stay.
- **BR-1 is re-priced** (follow-up, 2026-11-08): batch A's `.boot2` falls by
  9 on every kernel (BR-1's midpoint was +21 big and +10 small; it is now +12
  big and mega, part 1 at 0 and part 2 +12, and +1 small, part 1 -11 and
  part 2 +12).
- kern_mega also loses BR-173's -24 `.mega`: that saving came from sharing
  the fix, and without the retail fix kern_mega keeps its own release.
- Several items reclaim bytes inside their batch: BR-27 -12, BR-185/BR-186
  -43 `.mega`, BR-204 -10 `.mega`, BR-69 -12 module.

### 4.2 The structural weaknesses, ranked

These are model-level gaps, not defects. Each produces defects. The ranking
follows `synth/systemic.md`'s value judgement, with the scheduling constraint
from `synth/phase1.md` inserted where it binds.

1. **Work runs on whatever context caused it** (theme 1). Teardown is the one
   kind of work the UI task does not drain from a post. The UI task already
   drains six kinds of posted work (`inst_launch`, `wm_clreq`, `mega_fkq`, the
   posted compaction, `ui_rebootq`, `wm_tarm`). `sch_lock` is a big kernel
   lock with six meanings and about forty takers. The context rules are two
   prose lists (SPEC.md 20.6 rule 7 and the SDK's) that disagree, and nothing
   enforces either (ctx-7). This one gap is the root of BR-4, BR-173, hm-3,
   mem-10's worker arm and the worker reset of `[mem_pg_floor]`. **The owner
   (2026-11-08):** it has caused one or two problems in the field, none of
   them at shutdown: a sound interrupt arriving on a package already near its
   stack's edge drives it over. The mouse and idle already have stacks of
   their own, and more work here is not needed immediately.
2. **There is no fatal path, and refusal has no common meaning** (theme 4).
   One panel exists, for one cause (stack overflow: 174 bytes on kern_big, 173
   on kern_small, MEASURED). CF is the only shared refusal signal. AX means
   whatever each cell decided; kern_small's stubs leave it undefined in three
   places (gi-6, abi-2, dw-8). **The owner (2026-11-08):** a resident guard
   pays where the failure is catastrophic, user-visible, and its printout
   tells the owner something, as the stack-overflow panel does. A divide
   fault has not been met in production, so its panel waits behind a build
   knob.
3. **The hostile-input line is drawn site by site** (theme 5). The BPB is
   validated once by the reader that owns its domain. The loader, the
   directory-entry stage, the settings unpack and the partition table are not.
4. **Nobody owns the IVT, the IMR or int 15h, and IF=0 has no bound**
   (theme 2). Six parties write the PIC mask and the vectors in their own way.
   The longest IF=0 windows are a grow copy (~170 ms per 64KB, PREDICTED) and
   the cursor drawn inside the serial ISR, measured at:
   - 21,450 cycles (~4.5 ms) a packet on kern_big's VGA, which has no
     `KHP_VGASAVE`, MEASURED (cursor/mc-3);
   - 15,100 (~3.2 ms) on VGA with `KHP_VGASAVE`, a kern_mega-only knob,
     MEASURED;
   - 11,500 on CGA, MEASURED.

   None of these is the window SPEC names.
5. **Packages cannot ask the kernel's API level** (theme 9). `PKG_FMT` is an
   equality test with no minor number. A newer apps disk in an older machine
   is the ordinary case. **Narrowed (follow-up, 2026-11-08):** The Wire
   already computes exactly the level the review proposed (`WIRE_LEVEL` and
   `WIRE_CELL_LEVELS` in `tools/os88wire.py`, SPEC.md 92.2) and greys a
   too-new package, so the gap left is a package copied by hand. The owner,
   first round: a run-time level must prove its worth before it spends bytes;
   second round: **take it if it is cheap**, because the loaders gate on
   `<=` PKG_FMT, so a package newer than the kernel can still call a cell the
   kernel lacks.
6. **The kernels drift apart through one-arm fixes, and test coverage runs
   inverse to importance** (theme 11). 32 of the 64 code bugs live on one
   kernel or kernel group, but only 5 are true divergence. The 128KB floor
   has 21-28 soak rows and kern_mega 390-430. `test-full` never boots
   kern_mega. `t_api_abi` decodes kern_big only.
7. **Residency is shaped by build, not by machine, and there are only two
   lifetimes** (theme 7):
   - the mono renderer stays resident on VGA-only machines (~1,108 bytes,
     MEASURED span);
   - the PS/2 runtime (~265) and the keyboard mouse (`kbm_*` 599 `.text`,
     MEASURED) stay resident on every machine; the keyboard mouse's pointer
     half is taken out, kept only on a machine with no mouse (owner,
     2026-11-08);
   - every module use is a disk read;
   - `KERN_BUDGET` reads a VGA machine, so a win on the mono 8088 target never
     shows on the gauge (rt-5). The mono figure is already computed inside
     the assembly: `MONO_FLOOR`, 94,208 bytes against the gauge's 96,768,
     MEASURED (follow-up, 2026-11-08). The owner: print it in `kernsize`,
     because mono gains are pure gains for mono (owner, 2026-11-08).
8. **Lifetimes are kept by hope, not by count** (themes 7 and 10). One global
   driver worker count stands for all drivers (BR-5); nothing counts the
   callers inside a driver (BR-31). The Sticky App library's lifetime is keyed
   on a proxy instead of on who bound it (BR-42).
9. **kern_big's transient homes are full.** This binds the order of work more
   than any design does:
   - The blob: 0x19FC of 0x1A00, 4 bytes free (MEASURED). Its wants are
     +56..+120 net.
   - `.ovl`: 5,119 of 5,120, 1 byte free (MEASURED). Its wants are +82..+99.

   WP6 (-41 blob) and WP7 (-45 `.ovl`) come first. Even after them, kern_big
   needs a 14th blob sector or boot-17's table, and `.ovl` bodies have to move
   out. **The owner refused the 14th sector** (it delays the progress bar by
   0.4-0.8 s); `docs/plans/completed/BOOT3-PLAN.md` is the route: boot-only code moves
   into a `.boot3` section at the tail of KERNEL.SYS, read while the splash is
   already up and dropped before the `.vga` drop. Its wave 1 frees ~504 blob
   bytes and its wave 3 frees 176 of `.ovl`, for 0 resident (owner,
   2026-11-08). The owner's second round makes it generic across the kernels
   (kern_small unless it truly does not need it) and takes its wave 4.
10. **A task can block only on time, and every int 13h freezes every task for
    ~400 ms** (theme 3). `dsk_xfer` raises `sch_lock` across the BIOS call
    (`kernel/disk.inc:1462`). `gfx_lock`'s waiters spin through yield.
11. **The documentation is a hand copy of the code, and it narrates history**
    (theme 12). See section 9.
12. **Register discipline is a hard rule nothing checks.** Resident push/pop
    is 4,633-4,791 bytes; the entry callee-saves are 2,459 bytes in 612
    routines. Only 79 saves (127 bytes) are provably dead at every caller, and
    ~93 bytes are removable (ul-1). Changing to an "AX is scratch" convention
    is refuted on evidence: 247 of the 512 bytes of AX saves are in routines
    whose callers read AX afterwards. WP14 makes this a lint, not a size pass.
13. **No row produces any of the measurements the recommendations defer**
    (ul-10). See section 11.

## 5. What would be better approached an entirely different way

The thirteen themes from `synth/systemic.md`, in its value order. *Now* means
no ABI break: an appended cell or a contract clarification is an
`abilock --update` with no bump. Bytes are kern_big resident unless stated.

| rank | theme | the change of approach | now | flag day |
|---|---|---|---|---|
| 1 | 1. contexts and locks | teardown runs on the UI task; a dying owner releases its lock; the context rules become `; @ctx` tags plus a call-graph gate | **+11..+48** (teardown -6..+50 by route, ~+43 for the cheapest working shape; ui-3 -19); about **-1..+36** with BR-173 out under decision 3. Not needed immediately (owner, 2026-11-08) | none (deferred damage recorded at +150..+200) |
| 2 | 4. faults and errors | every CPU fault reaches one named stop; CF is the refusal and AX means something only inside a named family; errors carry DOS-3-style classes | **0 shipped** (owner, 2026-11-08): the panel and stubs (+45..+60 `.text` big, +35..+55 small, +20..+30 transient) are built only behind a knob | R9's universal value (open: AX=0 or the doc half only) |
| 3 | 7. residency and lifetime | a third lifetime (a dropped module becomes a purgeable cache); init-lifetime code held from boot and dropped at settle; class code in owner-loaded overlays, not more rungs | FILECP/FDLG (-2,380..-2,450) **refused** (owner, 2026-11-08); rt-1's drop-to-cache **+60..+85** `.cold`; **-300..-320** (keyboard mouse, TAKEN as "instead of": owner, 2026-11-08; re-priced by the follow-up), **-220** (PS/2 overlay); text-7 TAKEN (+16 gauge, about -177 mono) | - |
| 4 | 9. versioning | a runtime API LEVEL at `KERNEL_SEG:0006`, padding that is free today | **0** for the word, +7..+12 `.cold` for the load check. **Take it if it is cheap** (owner, 2026-11-08, second round; the first round said it must prove its worth). The Wire already computes the level, so the load check covers only hand-copied packages (follow-up, 2026-11-08) | dissolves abi-6's flag-day half |
| 5 | 2. interrupts | one owner for the IVT, the IMR and int 15h; a written IF=0 budget with a ratchet and rows | **+74..+88** (+77..+91 less ctx-5's +3, held: follow-up, 2026-11-08; the IVT scan probably costs less, walking `mem_rr_ivt`) (+100..+150 more for the full `OSAPI_IRQ` cell, OWNER) | the IRQ cell mandatory for drivers (D8) |
| 6 | 5. validation | hostile FILES are fenced by the reader that owns the domain; package POINTERS are checked only in an `APICHK=1` build | **+33..+47** (+41..+55 less wm-2's +4 and hm-14's +4, which are package fences: owner and follow-up, 2026-11-08; about half `.cold`) | - |
| 7 | 11. the kernel matrix | kernels fork, packages must not; arm honesty enforced by gates | **+5** `.cold`, +~20 HIBER.DRV; gates 0 shipped | L3, staged |
| 8 | 6. memory | keep claims and the relocation proc; add package-purgeable claims through that proc, a shared sub-allocator in source, and parts init-drop | **+29..+39** `.cold` with package-purgeable claims parked (+64..+119 with them; follow-up, 2026-11-08); hm-11 saves 0.8-1.5KB per running parts instance at 0 kernel bytes | C1 (-45..-69 kernel) |
| 9 | 3. waiting and I/O | block on events, not on time; a disk wait that freezes nothing (after four preconditions); a file layer that uses what it already knows | **+40..+55** `.text` +6 cell (`task_wake`) | a caller-held write cursor (C9, recorded) |
| 10 | 12. the documentation system | one source per fact, the rest generated; a committed heading lock | **0** | 0 |
| 11 | 8. dispatch | rare cells behind one index door; the handler table in a window class | -40..-60 (the `cw_` rehearsal, once the vector is chosen) | T5 **-325..-385**, R14 **~-320** |
| 12 | 10. the kernel-driver contract | read service tables in place; a request block only where the registers ran out | **-90..-140** big, 0 small | D6 (recorded) |
| 13 | 13. API shape and the includes | a stateless kernel whose efficiency is won back by caller-held tokens; fix the leaks in that shape, do not build a general library (STIKLIB, the one that pays, stays) | **+34..+36** (granule cell, cursor name stamp) | a `READ_AT`-shaped stream cell; `os88_rseq` withdrawn |

Theme 8's flag-day figures are `synth/flagday.md`'s, which re-derived them
after `synth/systemic.md` was written (systemic's own table still says T5
-325..-390 and R14 ~-300). Theme 2's kern_small range spans systemic's
+35..+45 and WP8's +40..+55.

What each theme means concretely:

**1. Contexts and locks.**
- **The worker keeps only the shallow, lock-free steps**: `inst_rel_rec` and
  `toast_owner_gone`. It releases a lock it owns. Then, inside one IF=0
  window, it marks the record task-less, posts it, and `task_exit`s with
  BX = 0.
- **The UI task runs the deep half on its own stack** (task 0's 512 bytes,
  `STK0_SIZE` on all three maps, against the dying worker's 128/192-byte
  slice; `synth/systemic.md`'s "1KB" is wrong): `inst_unwin`,
  `mem_free_rec`, `desk_drop_x`. It re-tests `I_STATE` and `I_TASK` first, so
  a stale post cannot hit a reused slot.
- **SPEC first.** SPEC.md 20.6's "the only code that can free its instance
  record is the worker's own `task_exit`" and SPEC.md 11.103.10's worker-arm
  close are rewritten before the code.
- **The same diff also takes:**
  - liveness tested once at `ui_bill`'s head (BR-28);
  - Locator identity from `[menu_win]` instead of a package byte (-19);
  - number SPEC.md 7's existing `sch_lock` rule and write the lock order
    (`gfx_lock`, `sch_lock`, `mem_cp_busy`, the module lock). SPEC.md 7
    already says `sch_lock` is a nestable count under which nothing may wait
    on another task (follow-up, 2026-11-08: the review's doc pass wrote that
    sentence, so only the number and the order are missing);
  - the `@ctx` tags with a host gate over `stkdepth`'s call graph (WP19,
    0 shipped).
- **The owner's weighing (2026-11-08).** Not needed immediately. The field
  problems the owner knows of were not at shutdown: a sound interrupt arriving
  on a package already near its stack's edge drives it over. The mouse and
  idle already have stacks of their own. BR-173, the dying owner's lock
  release, is one of decision 3's six package fences and is not taken as
  retail code. BR-4 stays in the register, its urgency waiting on `stkdie`
  (section 11).

**4. Faults and errors.**
- **The panel.** Generalise `sch_diepanel` into a system-error panel that
  prints "DIVIDE ERROR TASK nn AT cccc:iiii NAME".
- **The stubs.** Install int 0 always and int 6 on a 286+ (never int 5,
  which is PrtSc). Each stub loads DS = `KERNEL_SEG` and SS = `LOW_SEG`
  before it enters the panel.
- **The stated behaviour change.** On an 8088 a package divide that today
  continues with garbage would halt the machine. So the stubs ship with the
  package-close half (gfce-2, owner) or with a kernel-CS test.
- **Decided (owner, 2026-11-08).** On kern_mega, where the kernel can afford
  not to crash, closing the faulting package beats killing the OS, and it
  already does. In real mode a divide fault deserves at least a
  stack-overflow-style message where it cannot be recovered from, because a
  picture of it lets the owner trace who divided. But no machine has met it,
  so the panel, the stubs and the ~174-byte package answer wait behind a
  build KNOB, off in shipped kernels, until a field report says it is being
  hit: "that is a balancing act". The same weighing applies to every
  diagnostic panel for a failure nobody has met. The divide ratchet (0
  shipped bytes, a build-time gate) is unaffected, and so are the two DOS-side
  changes below, which are DOS.O88's.
- **Two DOS-side changes ride with it**:
  - DOS gives its world its own int 0 ("Divide overflow" ends the program);
  - windowed DOS banks vector 06h (+6 DOS.O88).
- **The divide ratchet** registers every `div` with its proof: 95 `div`,
  1 `idiv` and 7 `xlatb` in the kernel and 48 in drivers; 5 kernel sites are
  unfenced today.
- **The DOS 3 class and action columns** go into SPEC.md 18.4 beside the
  FERR list (0 bytes).
- **Not adopted**: one renumbered error space. No consumer mixes families.

**7. Residency and lifetime.**
- **`mod_drop` re-tags the claim purgeable; `mod_need`'s hit path re-tags it
  pinned** (rt-1, +60..+85 `.cold`). The need/drop bracket is then the pin.
  The hit path re-zeroes the module's bss from a new `MOD_H_BSS` header word
  and skips `mod_cfix`.
- **It overturns SPEC.md 2.8.3's recorded "the heap never does".** The reason
  given there survives, because only a *dropped* image is purged.
- **Purge ranks per module**: FILECP and FDLG at the highest rank, the
  system-disk modules LOW.
- **Then the owner's two residency trades:**
  - FILECP/FDLG cached at boot on kern_big. **REFUSED** (owner, 2026-11-08),
    as it has been "20+ times now": on kern_big the user has swapped in a DOS,
    office or utility disk and cannot be made to insert the system disk and
    wait every time a file is opened or copied, and a boot-time cache is not
    a saving either. Now recorded in CLAUDE.md, AGENTS.md and SPEC.md 38 so
    it is not proposed again. kern_small, a one-disk machine, keeps them as
    modules; the owner finds rt-6's "keep FDLG as purgeable" there
    interesting (section 10).
  - the keyboard mouse dropped at the first `[mou_ptr]` store. Re-priced
    (follow-up, 2026-11-08): about **-300..-320** resident (`kbm_poll0` must
    stay); on a machine that never settles a pointer the module holds a whole
    512-byte heap granule, so **+130..+220** for the session, not +85..+165
    (about +190..+210 against the corrected saving); and its "load at boot"
    rode on WP2b's boot cache, which is not built. **TAKEN, in an
    "instead of" form** (owner, 2026-11-08, second round): the keyboard mouse
    IS resident on every kernel today (`kbm_tab` .. `kbm_ev`, `.text`), and one
    cluster on disk for a decent amount of resident RAM is a good trade, but
    no boot may get longer. So it is loaded ONLY when no mouse is found, and
    the boot-cache dependency goes. **The shape the owner then proposed, and
    prefers over a module** (owner, 2026-11-08): the POINTER half moves into
    `.boot3` (BOOT3-PLAN) and, before `.boot3` is dropped, is copied into a
    heap claim only when no mouse has been found. So there is no disk read,
    no cluster on any disk and no system-disk dependency, and the claim can
    still be dropped later when a mouse that speaks only after boot
    (USBMOUSE.DRV, a late serial mouse) appears. Only the pointer half can go:
    `kbm_isr` is the keyboard ISR on every machine, while `kbm_key`,
    `kbm_move`, `kbm_btn`, `kbm_rclick` and `kbm_p5spend` are reached only in
    task context, so the claim needs no ISR pin and its callers need the
    one-compare gate a module's callers already have. A mouseless machine pays
    the heap claim; nobody pays a read.
- **text-7, `font_run_x`'s VGA prologue into `.vga`: TAKEN** (owner,
  2026-11-08, second round), at +16 on the VGA gauge and about -177 on a mono
  kern_big, and 0.048% of a full Note Pad repaint (0.78% in the absolute worst
  case, one run per cell), PREDICTED (follow-up, 2026-11-08).
- **Do not generalise the rung** (rt-4). A rung can be given back only as a
  contiguous suffix under `.cold`, and mono and VGA are complementary. The
  working split is:
  - one rung for planar hot code;
  - `DRVC_OVL` overlays for class code;
  - modules for features.

**9. Versioning.**
- **The level.** `dw OS88_API_LEVEL` at `KERNEL_SEG:0006`, never written as a
  literal 0060, because L1 moves the segment. 0006-0007 is padding today:
  0 resident.
- **The package side.** `os88pkg.py` writes the needed level into header
  byte 31. Byte 31 is the NUL of a legal 15-character name, so names are
  capped at 14; the longest in the tree is 13.
- **The load check.** `ld_check_hdr` (not `ld_hdr_ok`, which drivers share,
  and in whose header byte 31 is the bss paragraph count) refuses with
  `LD_EVER`.
- **One table** with The Wire's `WIRE_CELL_LEVELS`. `t_abilock` fails an
  appended cell that does not raise the level.
- **Old kernels.** Every `PKG_FMT`-10 kernel reads 0 at 0006 and refuses
  `OS88_NEED` as designed. The mouse-instrument word left that address before
  `PKG_FMT` 10 arrived.
- **What already exists (follow-up, 2026-11-08).** The level computation
  proposed here is The Wire's existing code: `WIRE_LEVEL` = 7 and
  `WIRE_CELL_LEVELS` in `tools/os88wire.py`, checked against each package's
  calls, with The Wire greying a too-new package (SPEC.md 92.2, 20.2.0.1) and
  `abicompat` testing the old-kernel case. So the kernel half (the word at
  0006 and the load check, ~8 `.cold`) covers only a package copied by hand.
  That gap is real: SPEC.md 20.2.0.1 says a new package calling a cell an old
  kernel lacks loads at the same `PKG_FMT` and far-calls past the table.
- **The owner (2026-11-08), first round.** Liked in general, but packages
  and drivers already carry versions checked before anything runs, so a
  run-time level would catch only a mistake in that versioning, and the same
  mistake could break the new number. It must prove its worth before it
  spends bytes.
- **The owner, second round: TAKE IT IF IT IS CHEAP.** The loaders gate on
  `<=` PKG_FMT rather than `==`, so a package newer than the kernel can still
  call a cell the kernel lacks. In the owner's words, "an `==` check on an API
  level keeps the `<=` PKG_FMT gate (rather than a list of supported formats
  per kernel) while refusing to crash on an old apps disk in a new kernel, or
  the reverse". The design above refuses only a package whose needed level is
  above the kernel's; whether the owner's `==` means that or an exact match
  is still to confirm (section 10).

**2. Interrupts.**
- **Write the budget and gate it** (WP13): `tests/ifwin.txt` registers every
  long `pushf/cli` window with its reason.
- **Long copies at IF=1 under `sch_lock`** (BR-12, BR-54).
- **One generic in-service rule**: `sch_isr` refuses a switch while any IRQ
  other than IRQ0 is in service (OCW3 0Bh). This is the rule AmigaOS, OS/2
  and well-behaved TSRs obeyed (BR-19, +14..+16, 55-65 cycles PREDICTED). It
  reuses the OCW3 in-service read the heartbeat's stop reading already makes
  (`khb_pisr`, `kernel/sched.inc`), and sits beside `[sch_chbusy]`, which
  already refuses the switch for a nested IRQ0; only the test for an IRQ other
  than IRQ0 is new (follow-up, 2026-11-08).
- **The ISR-door preconditions at the door** (ctx-5, BR-36). ctx-5's
  `evq_push` half (BR-85, +3) is held: the SDK already states SS = LOW_SEG,
  and no package IRQ handler that calls WAKE was traced (follow-up,
  2026-11-08, applying decision 3's weighing).
- **The IMR shadow** (+~25) and the IVT scan at driver release (+25 `.cold`).
  The scan walks `mem_rr_ivt` (`kernel/memory.inc`), which already lists every
  IVT row a driver can own for the region-reloc walk, rather than a second
  hand-written list, and probably costs less than +25 (follow-up,
  2026-11-08).
- **DOS.O88 is an IMR owner too**, so the shadow re-syncs from port 21h after
  every DOS bracket.
- **Measure first**: moving the cursor after the EOI. It breaks `mou_isr`'s
  written "never stis" invariant, and its 128-byte private stack is sized on
  that invariant.

**5. Validation.**
- **The loader** computes the clear prefix and refuses unless both the image
  and the file exceed it (BR-7, +32..+36 `.cold`).
- **The directory stage** clamps FileSize once at `dsk_get_dir`, with dw-1's
  two guards as the belt.
- **The settings unpack** gets a domain column in `CFG_MAP`. An
  out-of-domain byte skips the store (gfce-6: ~+22 `.ovl`, ~+22 CTRL.DRV,
  0 resident).
- **The partition table** gets a status, duplicate and overlap check (in
  HDD.DRV).
- **Package-pointer checks** (`wm_isrec` at the ISR doors, the entry proc's
  BX, hm-2's exact fence, dw-4's worker refusal) go into an `APICHK=1` knob
  build with one soak row. Windows 3.1's debug KERNEL, without 3.1's retail
  cost.
- **Decided (owner, 2026-11-08): packages are trusted.** Real mode cannot
  defend against a hostile package (and on kern_mega an FSX real-mode bracket
  hands it real mode anyway), so the only thing worth defending against is our
  own mistakes, and those are better found and guarded at BUILD time than with
  resident bytes. BR-8's doors and bound, BR-22, BR-29, BR-30, BR-37 and
  BR-173 are not taken as retail code; at most they are `APICHK=1` arms. The
  follow-up applies the same weighing to BR-107 and BR-109's optional doors,
  which `synth/bugs.md` names as the same kind. Now recorded in CLAUDE.md and
  AGENTS.md.
- **Not adopted**: window handles in place of record pointers. The breakage
  would be ~121 field reads in 33 asm files (ul-8), and the checked build gets
  the validation without a break.

**11. The kernel matrix.**
- **The two one-arm fixes**: BR-32 (+5 `.cold`) and BR-38 (+20 HIBER.DRV).
- **A four-kernel `t_api_abi`** that also decodes the refusal stubs.
- **A kern_small MartyPC port list**; `t_hotpar` over kern_mega.
- **A ratchet on raw build names in shared code**: 638 `KERN_BIG`, 126
  `KERN_SMALL`, 406 `KERN_MEGA` and 21 `KERN_EMU` lines (MEASURED census).
- **The near model** no longer limits capacity on kern_small (32,565 bytes
  free in the window) or on kern_mega (14,056). It binds kern_big at the VGA
  rung, with **6,946 bytes left** (MEASURED, gkm-11), not the 19,842 that
  `KERN_CODE_MAX`'s line implies.
- **Retire kern_emu**: a USBMOUSE-shaped VMMOUSE.DRV (+~40 kern_big, -385
  emu-only). Unproven, and the owner's call.

**6. Memory.**
- **Purgeable claims for packages** (`MEMP_PURGEABLE`, +35..+80 `.cold`). The
  proc is called with DX = 0 at IF=1 under `sch_lock`, as `mem_reloc_call`
  does. `MC_DMA_HEAD` narrows to 0x1FFF to free bit 13. kern_small refuses
  the verb (+4..+6). **Parked (follow-up, 2026-11-08), after the owner asked
  for the real use cases.** It is not needed now. Of the two consumers cited
  here, Skies' title art is already solved (SPEC.md 88.10.7.1), and Tracker's
  9.5KB shadow is real but small. The one large case is Sheet's two modules,
  ~80KB held from launch to exit, whose recovery code already exists. The
  kernel already keeps the largest package-content cache purgeable on the
  package's behalf (`MEM_P_WSAVE`). If it is ever built, it is justified by
  Sheet and by keeping lazy parts until the heap needs them, with the rank in
  AL; measure first on a 256KB profile with Sheet behind a second
  application. The zero-kernel-byte alternative is Sheet dropping its modules
  after each use, at a ~1-2 s re-read each time.
- **A system sub-allocator as an include**: WIN16's arena lifted into
  `apps/os88lmem.inc`. That is GEOS LMem, Win16 `LocalAlloc` and OS/2
  `DosSubAlloc` as source; 0 kernel bytes.
- **Parts init-drop** (`OP_INITDROP`).
- **An explicit kind byte per LDT entry on kern_mega** (gms-R1), in place of
  ownership inferred from geometry.
- **The KB doors' carry bug** is kernel-internal: its shifts live in helpers
  that ~20 internal callers keep. So withdrawing the cells (C1) fixes nothing;
  converting the internal callers in Phase 1 does.

**3. Waiting and I/O.**
- **`task_wake(AL=slot)`** with a sticky wake-pending bit in `T_FLAGS` bit 1
  (free today): Amiga's signal bit with the slot as SysV's channel
  (+40..+55 `.text`, a 6-byte cell).
- **Releasing `sch_lock` across the UI task's floppy int 13h** needs four
  preconditions first:
  - a task-scoped `cur_inxfer`;
  - theme 1's teardown;
  - theme 2's in-service rule;
  - windowed DOS's IVT exchange excluding 0Eh/15h or keeping the lock.

  It pays only on ROMs that keep the caller's stack (the IBM/XT), and
  `xferfreeze` must be measured first.
- **The file layer** (WP12, WP16):
  - cache the free count, as DOS's DPB did;
  - `FILE_FIND_SEQ` over the existing resume, DOS's DTA model;
  - contiguous allocation from the known size (Mac `PBAllocContig`);
  - only droppable frees may wait to be flushed.

**12. Documentation.** See section 9.

**8. Dispatch.**
- **The flag-day half** is in section 8: T5's door (owner) and R14's class
  table.
- **Now**, only after the door's vector is chosen: rehearse the door on the
  kernel's own 45 `cw_` shim bodies (P4).
- **Refused on evidence**: a Pascal-style scratch-register convention.

**10. Drivers.**
- **Keep only class 1's service-table copy** (the IRQ0 reader). Read every
  other class's table in place through `drv_cls_svc_x`.
- **The bytes**: -90..-140 resident kern_big, 0 kern_small. `drv_svc` is 252
  bytes MEASURED on kern_big and absent from `build/smallk/kernel.map`.
- **D4**: write the service-table floor down. No shipped driver pads, so this
  needs no bump.
- **Refused**: a request packet for every verb. It is dearer than the
  near-proc table on an 8088.

**13. API shape.**
- **`OSAPI_FILE_GRAIN`**: bytes per cluster and `READ_AT`'s granule, with no
  FAT count (+14..+16). Today a caller learns the cluster size only by
  counting the whole FAT: ~105 ms on a 20MB disk, MEASURED.
- **The `READ_SEQ` cursor names its file**: an 8-bit name sum plus a folder
  byte (+20 `.cold`).
- **`pit_now`'s bracket limitation** is already stated in `apps/os88pit.inc`
  and SPEC.md 53.2.1 (the review's own doc pass wrote both); only SPEC.md
  72.15.1's sentence is still owed (follow-up, 2026-11-08).
- **The kernel button draws each pixel once** (BR-176).
- **Not adopted**: a GENERAL shared-library tier. Shared include code is
  real, at least 60.6KB duplicated across 35 measured listings (MEASURED,
  rt-8). But RAM pays only for packages that run together, ~3-5KB in a
  typical trio. The one shape that already pays is in the tree: the Sticky
  App library, `apps/stiklib.asm` (SPEC.md 110.15), assembled once, carried
  in DESK.DRV's image and far-called by every `STIK_LIB` widget, a library
  riding a module that is resident anyway (follow-up, 2026-11-08).

**What we would NOT copy**, recorded so nobody re-derives it as an improvement
(`synth/systemic.md`, *What we would NOT copy*). Each entry refuses a MODEL;
where os8088 already has the feature in another shape, the entry names it
(follow-up, 2026-11-08):
- **Automatic demand loading and LRU discard of package code.** On one floppy,
  every reload can be a swap prompt mid-gesture.
- **Master pointers on every access.** A per-access tax on an 8088, and `HLock`
  bugs were the Mac's chronic disease.
- **Real-mode Windows' stack-walking code motion.**
- **One window procedure in place of per-event vectors.**
- **A Pascal-style scratch-register convention.**
- **One renumbered error space.**
- **A request packet for every driver verb.**
- **Trap patching.**
- **A general shared-library tier.** The one library that already pays,
  STIKLIB carried by DESK.DRV (SPEC.md 110.15), stays.
- **A general rung mechanism.**
- **Run-time kernel unification.**
- **A full priority field in the scheduler.**
- **Per-thread message queues.**
- **Proportional fonts.**
- **Write-behind on removable media.**
- **Delayed clipboard rendering.**
- **A desktop that is a folder of `.LNK` files** (Windows 95), read back by a
  directory walk at every boot. os8088 already has both shortcuts (SPEC.md
  26.8: SYSTEM.CFG records with their picture cached, made by dragging
  anything runnable onto the desk) and `.LNK` links (SPEC.md 26.10). Making
  the desktop a folder was already looked into, and it is not a bad idea at
  its core; it is only too expensive in disk reads and resident bytes to
  manage on a machine without a 95-era system's resources (owner and
  follow-up, 2026-11-08).
- **Settings in CMOS.**
- **GEOS-style state files instead of hibernate.**
- **OS/2's no-copy DOS box.**
- **A unified DOS-style buffer pool.**

## 6. What the period systems did better, and what os8088 does better than all of them

The comparison is at the level of the model: how a system is organised, not
which features it shipped.

| question | MS-DOS 2-6 | Windows 3.0/3.1 | OS/2 1.x | Mac System 5-7 | PC/GEOS | os8088 today |
|---|---|---|---|---|---|---|
| a CPU fault | its own int 0 handler prints "Divide overflow" and ends the *program* | 3.0's UAE kills the application, often leaving the system unstable; 3.1's GP-fault dialog offers Close | two tiers: a fault in user code kills the process, a fault in system code is a trap screen | `SysError` draws the bomb box with an ID | - | no handler on kern_big/kern_small (BR-9); kern_mega closes the faulting package with per-task catch frames |
| an error | int 24h Abort/Retry/Ignore (Fail from 3.3); AH=59h (3.0) returns CLASS, ACTION and LOCUS; error 1 has never been renumbered | per-API returns; the 3.1 debug KERNEL RIPs and continues | per-API return codes | one `OSErr` number space for every manager | - | CF=1 is the only common sign; four code spaces reuse small numbers; no class column |
| who owns the IRQs | nobody: TSRs chain vectors and "unhook only if still yours" is folklore | VPICD virtualises the PIC per VM (enhanced mode); a short documented list of calls is legal at interrupt time; anything an ISR touches must be FIXED or page-locked | the kernel: `DevHlp_SetIRQ` (refused if held, unless both say shared), `UnSetIRQ`, `DevHlp_EOI`; `ProcRun` hands work to a task | the cursor is drawn from the VBL task at level 1, beneath the SCC at level 2; the Deferred Task Manager (Inside Macintosh V) | - | six owners write the IMR and IVT their own way; no IF=0 budget (theme 2) |
| waiting | single task; the AT BIOS added int 15h AH=90h/91h so a multitasker could block on the floppy | cooperative: a task gives way inside `GetMessage`; idle spins, except that 386 enhanced mode offers int 2Fh 1680h to give the slice back | `DosSemWait`, owner-died semaphores, priority classes with foreground and I/O boosts | cooperative under MultiFinder; every File Manager call takes a ParamBlock with `ioCompletion`, which is how the Mac got async I/O; System 6's event loop (`GetNextEvent`/`SystemTask`) spun | pre-emptive threads with priorities | the UI task really halts (96.9% at idle, MEASURED); everything else can wait only on time; every int 13h freezes all tasks (theme 3) |
| memory that moves | an in-band MCB chain with owner = PSP; nothing moves | handles over a moveable/discardable global heap, lock counts, LRU discard, `GMEM_NOTIFY`; real mode walks BP chains to move code | LDT selectors; `DosAllocSeg`/`DosReallocSeg`/`DosSubAlloc`; motion is transparent, and every descriptor is owned by an explicit record | master pointers; `HLock`, `HPurge`, `MoveHHi`; a `GrowZone` proc; purgeable resources | a handle table for everything; movable, discardable and swappable blocks; LMem chunk heaps; VM files; swap drivers | an out-of-band claim table; relocation by the holder's own callback; a pinned default; purge ranks owned only by the kernel (theme 6) |
| system code residency | COMMAND.COM's transient portion, reloaded from COMSPEC with a checksum; TSRs keep only their resident paragraphs | discardable code segments reloaded through thunks; one DISPLAY/KEYBOARD/MOUSE.DRV per machine, every primitive a far call | init code discarded after boot | the toolbox in ROM; purgeable CODE resources; `LoadSeg`/`UnloadSeg` | a small fixed kernel, everything else in movable, discardable resources; one video driver resident per adapter | on-demand modules behind one-compare gates; a boot blob at the heap floor; the `.vga` slide; only two lifetimes (theme 7) |
| versioning | int 21h AH=30h | the NE header's expected-Windows version; ordinal imports fail the LOAD; the NE `PROTMODE` flag lets a converted binary declare itself | - | `Gestalt` (System 6.0.4) | each geode carries a protocol number per imported library: the major must match, the minor must be at least the one built against | a major number at load, an equality test; plus The Wire's per-cell level for every package it serves, which greys a too-new one (SPEC.md 92.2; corrected, follow-up, 2026-11-08) (theme 9) |
| validation at the API | trusts the MZ header and every BPB field | 3.0 validated almost nothing; 3.1 added retail parameter validation at the USER/GDI door (`IsWindow` on every HWND) | full, behind the protection boundary | none: a bad handle is a bomb | - | hostile files mostly fenced (the BPB better than DOS); package pointers trusted by ruling (theme 5) |
| paint and teardown | - | a per-window update region; `WM_PAINT` synthesised only when the queue is empty, on the owner's queue | PM defers `WM_PAINT` by default; a window class opts into synchronous painting with `CS_SYNCPAINT` | `updateRgn` accumulated; `updateEvt` delivered after every other event | - | repaint is synchronous and nested; teardown runs on the dying worker (theme 1) |
| drivers | the `DEVICE=` chain, strategy/interrupt entries, a request header at ES:BX, `INIT` returning the break address | installable drivers (`DriverProc`) with open counts; MMSYSTEM device IDs (many devices per type); `SYSTEM.INI` keeps unknown keys | request packets through a strategy routine; `DevHlp` services | the Device Manager unit table: Open/Prime/Control/Status/Close; cdev Control Panels | drivers are geodes with reference counts; a refusable `DR_SUSPEND` with a reason | loaded and unloaded at run time, typed classes, `DRV_H_KEEP` (DOS's break address); lifetimes kept by hope (BR-5) |
| several kernels | one binary from 8088 to 486, adopting HIGH/HIMEM at run time | 3.0: KERNEL.EXE, KRNL286.EXE and KRNL386.EXE from shared source, with one binary for every application; 3.1 dropped real mode | one kernel (286 protected mode) | raised the RAM floor per release instead of forking | - | four build-time kernels, one package binary; arm honesty kept by culture (theme 11) |
| the contract text | the Programmer's Reference gives every function the same Entry/Return/Notes shape, with a version gate ("2.0+") | H2INC generated the MASM include from the C header | - | Inside Macintosh: a chapter per manager with a Summary, cited by chapter NAME; an appendix of "routines that may move or purge memory" | routine headers read PASS / RETURN / DESTROYED | 636 build-time assertions and an ABI lock, but the prose is hand-copied (theme 12) |

A dash means the review found nothing at the model level worth comparing. It is
not a claim that the system lacked the mechanism.

**What the period systems did better, in one line each.**
- **DOS.** Turned a fault into the end of the *program* rather than the
  machine. Gave every error a class and an action that a program can act on.
  Cached the free-cluster count in the DPB. Kept search state in the caller's
  DTA.
- **Windows 3.1.** Validated at one door, the USER/GDI boundary. Shipped a
  debug kernel that reports and continues. Painted only on the owner's
  queue. Failed the *load* when an import was missing, instead of failing the
  call.
- **OS/2 1.x.** The kernel owned every IRQ line and told semaphore waiters
  when the holder had died. Every descriptor had an explicit owner record.
- **Mac System 5-7.** Published the *list*: which routines may move memory
  and which may be called at interrupt time. Drew the cursor beneath serial
  in priority. Gave applications a purgeable tier and a `GrowZone`. Made I/O
  asynchronous through one ParamBlock shape.
- **GEOS**, the 8088 peer. A protocol number per imported library, checked at
  load. A sub-allocator in every block. Reference-counted drivers.
- **Unix System V and AmigaOS**, which the owner's "etc." admits.
  - SysV: `sleep(chan)`/`wakeup(chan)` on any address; SVR3/SVR4 kernels
    configured from `master`/`system` files selecting whole objects at link
    time, so there was no `#ifdef` forest in shared files.
  - Amiga: 32 signal bits per task; `Forbid` against `Disable` with a written
    rule.

**The pattern under all of these.** A period system that aged well turned a
rule into a **mechanism at a single choke point**. os8088 already has the
choke points:
- `ui_bill`;
- the file cells' N-stub;
- `wm_pkgcall`;
- `ld_check_hdr`;
- `sch_diepanel`;
- `mem_reloc_call`;
- `mod_need`/`mod_drop`.

It also has the tooling that turns prose into gates. So most of section 5 is
**"move the rule to the funnel, then gate it."**

**What os8088 does better than all of them.**
- **The call surface.** Direct far calls into a fixed table: no relocation, no
  loader fixups, and no thunk per callback (Win16 needed `MakeProcInstance`).
  The per-call cost is about 5× below DOS's int 21h. A package pays nothing
  for an event it never asked for.
- **Memory.**
  - The claim map is out of band, so a stray write cannot corrupt the
    allocator the way an MCB header could.
  - The owner word comes from the stub's ES and cannot be forged.
  - Relocation by callback means no double indirection on every access, a
    real tax on an 8088 that GEOS and the Mac paid.
  - A five-level purge rank is priced in user wait.
  - Compaction plans before it copies.
  - DMA heads are kept inside one 64KB page by the allocator itself.
- **Idle.** The UI task blocks with the lost wakeup closed in one `cli` span,
  and the machine is ~97% halted at idle. Windows 3.x and System 6 spun.
- **The disk.**
  - Shadow replace and one commit order: a power cut never costs the user's
    bytes, where DOS truncated in place.
  - Stateless, name-based file calls leak nothing when a program dies; DOS
    handles did.
  - Cylinder-run batching made 140 int 13h calls against DOS's 325 on the
    same workload.
- **Refusal.** Refusal is a normal path: a probe, CF=1 and a greyed control
  ("grey a fact, never a guess", SPEC.md 47). No period system had a
  discipline against a feature that silently costs seconds.
- **Fault containment.** kern_mega's beats Windows 3.0's UAE: a close
  attributed to one instance, loop protection, and a kernel fault kept fatal.
  The death panel is safer to draw than the Mac's bomb box.
- **The engineering system.** No period project gated its own facts:
  - 636 assembly-time assertions;
  - an ABI lock with a written reason for every fingerprint move;
  - the API table decoded from the binary;
  - a suite registry;
  - a generated "does this already exist" index.
- **Boot.** The boot blob rejoins the heap at its floor; DOS's SYSINIT was
  lost memory in practice.
- **Fairness.** `gfx_lock` is an OS/2 RAM semaphore with a fairness rule OS/2
  lacked.
- **One codebase, four machines.** Segment macros make ~400 sites
  protected-mode clean at assembly time and byte-identical off kern_mega,
  where Windows relied on written rules. A real 128KB floor sits beside a
  286+ protected-mode kernel, from one source.

## 7. Phase 1: now, with no ABI break

The plan is `synth/phase1.md`: 34 work packages in 29 implementation sessions,
with the bug batches interleaved. **No session runs a soak row or `test-full`.**
Each verifies on the host and names its rows. The deduplicated union of those
rows runs once, after the last session, with the A/B leg against the starting
commit, and then `test-full` runs once.

**The binding constraint first.** kern_big's blob (4 bytes free) and `.ovl`
(1 byte free: `OVL_SIZE` 13FFh = 5,119 of 5,120 in `build/kernel.map`, where
`synth/phase1.md`'s ledger still quotes the cost lens's 5,116) are full,
MEASURED. WP6 (blob -41) and WP7 (`.ovl` -45) land
early. Even then:
- **the blob** needs a 14th sector (an OWNER decision) or boot-17's table
  (-30..-60, measure first);
- **`.ovl`'s** wants exceed its room by tens of bytes, so bodies move out of it
  (clk_probe's RTC rungs first).

**Decided (owner, 2026-11-08): no 14th sector.** The route is
`docs/plans/completed/BOOT3-PLAN.md` (a proposal, nothing built): a `.boot3` section at
the tail of KERNEL.SYS, read while the splash is already up, dropped before the
`.vga` drop and the heap compaction. Its wave 0 is the mechanism (and takes
boot-18, BR-154); wave 1 moves three closed clusters and frees ~504 blob bytes;
wave 2 moves the SYSTEM.CFG reader and the boot driver loader (~1,640 more);
wave 3 frees 176 of `.ovl`; wave 4, the owner's call, takes `.boot2` back to
"draw the splash and load KERNEL.SYS". Every wave is 0 resident (wave 4 -8).
Its section 11 places each want: BR-6, BR-1 and BR-147 in `.boot2` once wave 1
has landed; shell-8 in `.boot3` from wave 2; the `.ovl` wants with about 120 to
spare after wave 3. phase1.md's "14th sector or boot-17" choice is gone, and
boot-17 rides wave 4 for its diagnostics alone. The owner's second round
answers seven of its ten open questions: `.boot3` is GENERIC across the
kernels (kern_small unless it truly does not need it), so kern_mega's 2-byte
`.ovl` gets the same relief; the name is whatever confuses a new session
least; wave 4 is YES; no kern_big 128KB boot row; `blobruns` into the fast
tier if it is really quick (or full); the earlier bar without wave 4 only if
it is less complex than wave 4. BOOT3-PLAN's per-kernel figures were priced
for kern_big alone and are re-priced there. The plan carries waves 0-1 in
session 3 and waves 2-4 as sessions of their own.

kern_small's installs go in `.boot2`, which has 702 bytes free.

**The work packages, in session order.** Bytes are resident, signed and
ESTIMATED. kern_big is the VGA gauge. kern_mega is conv / above 1MB. OWNER
marks a WP whose bytes wait on a decision.

| session | WP (and batch) | what | WP bytes: kern_small | kern_big | kern_mega conv / high |
|---|---|---|---:|---:|---|
| 1 | WP10 | gates that catch wrong facts: four-kernel `t_api_abi`, `t_mirror` over `kerndos/` and `os88.h`, a heading lock, `t_sections`, the four measurement rows | 0 | 0 | 0 |
| 2 | WP1 + WP7 | free bytes (dead code, dead data, redundant tests); `.bss` arrives zeroed: settled, zero on every shipped path since PR #61, the comments corrected, and `t_bsszero` proposed as the gate (owner and follow-up, 2026-11-08) | -165..-208 | -144..-166 (`.ovl` -45, `.boot2` -4) | -87..-121 / -49 |
| 3 | WP6 + BOOT3 waves 0-1 + batch A | boot: KSIG_OFF and SPL_RESIDENT derived by the build; blob room through BOOT3-PLAN, not a 14th sector (owner, 2026-11-08); then BR-1, BR-6, BR-25, BR-55, BR-56, BR-63 | 0 (`.boot2` +18) | 0 (blob -41 and BOOT3 wave 1's -504 first, then +69; KERNEL.SYS ~-403, then ~+450 for `.boot3`) | 0 |
| 4 | WP14 + WP19 | a routine header grammar and a liveness lint; `@ctx` tags, the call-graph gate, the `APICHK=1` checked build | 0 | 0 | 0 |
| 5 | WP9 + WP26 | the API level (take it if it is cheap: owner, 2026-11-08, second round); every contract sentence in one `abilock --update` | 0..+5 | 0..+5 | -4..-8 / +8..+9 |
| 6 | WP8 + batch F | the fault panel, int 0/6 stubs (behind a build knob, owner, 2026-11-08), divide ratchet, window doors (not retail, owner), CFG_MAP domain; then input, ISR and scheduler fixes | 0 | 0 (`.ovl` +22, gfce-6 only) | 0 |
| 7 | batch C + WP21 | BR-4 teardown on the UI task with BR-173, BR-28, BR-42, BR-47..BR-50, BR-176; the damage-pass convention | -10 | -10 | -10 |
| 8 | batch D + WP17 + WP13 | BR-2, BR-7, BR-11, BR-12, BR-53 (WP2's prerequisite); heap placement; the IF=0 rule, gate and instrument | 0 | +8 | 0 / +8 |
| 9 | WP2a | rt-1 drop-to-cache, rt-9 ranks, shell-2's two users (MOD_SETS 3,180 bytes MEASURED, STIKMENU 7,876) | 0 | +60..+85 `.cold` | 0 / +60..+85 |
| 10 | WP2b | FILECP and FDLG on kern_big as modules cached at boot. **REFUSED** (owner, 2026-11-08), in any form; the session is withdrawn | 0 | 0 (was -2,468..-2,508) | 0 |
| 11 | WP3 (KBMOUSE and text-7 TAKEN: owner, 2026-11-08, second round) | the keyboard mouse's pointer half in `.boot3`, copied into a heap claim only when no mouse is found (the owner's shape, over a module); the PS/2 runtime as an owner-loaded overlay; `font_run_x`'s VGA prologue into `.vga` | -300..-320 (KBMOUSE re-priced by the follow-up; was -290..-380) | -504..-539 (mono XT about -697..-732) | -300..-320 / 0 |
| 12 | WP4 + WP22 + batch G (driver half) + WP31 | driver tables read in place; contract text; the IMR shadow; gkm-1's one-arm fixes; matrix gates | +15..+19 | -62..-122 | -156..-166 / +40..+100 |
| 13 | WP5 + batch H | kern_dos images (-560 kern_big's, -1,585 each kern_mega's) and kern_mega's own dead weight | 0 | 0 | -40 / -145..-165 |
| 14 | batch E + WP16 | the disk hazards; a linear directory walk, `FILE_GRAIN`, F3's dual-mark reader | +22..+27 | +77..+88 | +6..+12 / +70..+80 |
| 15 | WP12 | FAT write path: a cached free count, contiguous runs (dw-14, OWNER for its SPEC rewrite), a bracketed flush | 0..+3 | +160..+255 | 0 / +160..+255 |
| 16 | WP18 | scheduler: a measure-first UI boost, `task_wake`, numbered lock rules; histsys-5's disk release only after its preconditions | +50..+77 | +50..+77 | +50..+77 / 0 |
| 17 | batch B + WP20 + WP23 | display fixes; graphics hot paths; the button drawing order; video facts | +32..+44 | +120..+146 (mono +40..+56) | +24..+36 / +16..+20 |
| 18 | WP11 (per row: owner, 2026-11-08) | the KHP round, revived row by row, each on whether it earns its space: RELIT, DMGSTALE, CAPTION, VGASAVE, VGAFILL, VGAEDGE recommended | 0 | 0..+289 by row (the six: +286..+289, +104..+107 every machine, +182 `.vga`) | 0 |
| 19 | WP15 | SDK-side work in packages | +24..+32 | +42..+60 | +30..+40 / +12..+20 |
| 20 | WP27 | desk, dialogs and menus: the additions, behind `OS88_NEED` | -21..+9 | -15..-25 | -21..-31 / +6 |
| 21 | WP29 + WP30 + batch G (hiber/clock half) | DISKOPS and HIBER module work (+54..+74 modules); one clock (prototype) | -10..+10 | -10..+10 | -10..+10 |
| 22 | WP33 (+ batch I, HDD half) | HDD.DRV's kernel face; hdk-11 first, because the image has 2 spare bytes | 0 | 0 (HDD.DRV ~-45 net) | 0 |
| 23 | WP34 (+ batch I, XMEM/DOS half) | XMEM.DRV, its cells, and DOS.O88's int 15h | 0 | -54 | 0 |
| 24 | WP28 | the Control Panel engine prototype; shared module source; `os88ovlchk` sees macros | 0 | 0 (CTRL.DRV -1,500..-3,000, unprototyped) | 0 |
| 25 | WP32 | flag-day pre-positions: the `OSCALL` mechanism (cmp-proved) and the `cw_` door, once the vector is chosen | 0 | -40..-60 | 0 |
| 26-29 | WP24, WP25 | documentation: one copy of each fact; the comment discipline sweep | 0 | 0 | 0 |

The byte columns count the session's WPs only, summed where a session takes
several. A batch's bytes are in section 4.1, and a parenthesis notes transient
bytes. Each WP's own figure is in `synth/phase1.md`'s summary table.

**Phase totals** (`synth/phase1.md`, the WPs alone, bug batches excluded,
ESTIMATED). **Recomputed after the owner's answers (owner and follow-up,
2026-11-08):** WP2b is refused, so it is in neither column; WP2a's
drop-to-cache (+60..+85 `.cold` kern_big) is in both; WP8's panel, stubs and
door refusals (+75..+106 big, +70..+101 small, +30..+46 mega conv) are behind
knobs, so 0 shipped; WP9 is in both, as "take it if it is cheap"; WP3's
KBMOUSE (re-priced -300..-320) and text-7 (+16 gauge, about -177 mono) are
taken. The one owner item left to split on is WP11, now per row:

| kernel | without WP11 | with WP11's six recommended rows | context |
|---|---:|---:|---|
| kern_small | **-249..-426** | -249..-426 | KERN_SIZE 59,392 of 64,000 |
| kern_big (VGA gauge) | **-469..-95** | **-183..+194** | KERN_SIZE 96,768 of 100,864 |
| kern_big on a mono 8088 | about -752..-368 | about -648..-261 | `MONO_FLOOR` 94,208, MEASURED |
| kern_mega conventional | **-443..-596** | -443..-596 | budget 68,096 |
| kern_mega above 1MB | +166..+389 | +166..+389 | not budgeted |

The first review's figures, for the record: every WP with the owner items
taken, kern_small -138..-416, kern_big -2,158..-2,676 (mono about
-2,600..-3,150), kern_mega -387..-626 conv and +166..+389 high; without WP2,
WP3's KBMOUSE and text-7 and WP11, -36..+152, -150..+210 (mono about
-240..+130), -97..-246 and +106..+304. rt-6 (kern_small keeping FDLG
purgeable, section 10) adds +60..+85 to kern_small if it is taken.

**With the bug batches.** This is this document's arithmetic, not a
synthesis figure. After the owner's answers the core batches (section 4.1's
recomputed total) add about **+341 kern_big, +220 kern_small and ~+179
kern_mega conv**; nothing is double-counted with WP8 any more, because both
BR-8 and BR-9 are out of the shipped kernels. The whole of Phase 1 is
therefore about:

- **kern_big:** -0.13..+0.25KB without the KHP round; +0.16..+0.54KB with its
  six recommended rows; on a mono 8088 about -0.41..-0.03KB and
  -0.31..+0.08KB.
- **kern_small:** -0.21..-0.03KB.
- **kern_mega conv:** about -0.26..-0.42KB.

The first review's figures were kern_big -1.7..-2.3KB with the owner items
and +0.27..+0.63KB without, kern_small -0.12..+0.16KB and +0.26..+0.45KB,
and kern_mega about -0.19..-0.43KB. All of it is ESTIMATED.

Outside the resident totals:
- **Modules:** no KBMOUSE.DRV in the owner's preferred shape: its ~+400 bytes
  ride `.boot3` inside KERNEL.SYS instead, and become a heap claim only on a
  machine where no mouse was found (owner, 2026-11-08); a PS/2 overlay ~+265; CTRL.DRV
  -1,500..-3,000 (WP28, unprototyped); HDD.DRV ~-45; XMEM.DRV ~-17;
  HIBER.DRV/DISKOPS.DRV +54..+74.
- **Packages:** mem-17 -10..-19 per image; DOS.O88 +150..+165 (WP34).
- **Boot:** `docs/plans/completed/BOOT3-PLAN.md` costs 0 resident through its wave 3
  and -8 at wave 4 (PREDICTED, not counted above). KERNEL.SYS grows about
  +450 at its wave 1 and +1,950 by its wave 2 (+1 and +4 sectors), read after
  the bar: about +24 ms for wave 1 and +80 ms more for wave 2 on 360KB
  (PREDICTED). Those figures are
  kern_big's; the second round makes `.boot3` generic, so they are re-priced
  per kernel there.
- **Disk:** WP2b's +4 clusters per kern_big system disk are gone with it.

**Where the phase grows** is mostly correctness and speed:
- WP12 +160..+255;
- WP20 +120..+146;
- WP16 +77..+88;
- WP18 +50..+77.

WP8's +75..+106 left the list with the knob and the ruling (owner,
2026-11-08).

**If it must shrink, cut in this order:**
1. dw-14;
2. WP20's `.vga` items;
3. WP18's boost;
4. WP27's optional cells.

WP8's door refusals, the old third item, are already out.

**Lock discipline.** No WP moves `PKG_FMT` or `DRV_VER`, but several move a
fingerprint, so each needs an `abilock --update` with no bump:
- WP9's level, and the header byte-31 cap;
- WP19's `@ctx` tags, one update for the whole tagging;
- WP26's contract text, one update;
- each cell-adding WP (WP16, WP18, WP22's optional `OSAPI_IRQ`, WP27; WP2's
  `MEMP_PURGEABLE` is parked, follow-up, 2026-11-08).

**WP9 lands before any WP that adds a cell**, so the first caller of a new cell
refuses on an old kernel. That holds if WP9 is taken, which the owner's second
round makes "if it is cheap" (owner, 2026-11-08). If session 5 finds it is not,
a cell-adding WP falls back on what the tree already does: The Wire's
`WIRE_CELL_LEVELS` row and `WIRE_LEVEL` raised by the release that first
publishes the cell (SPEC.md 92.2), with hand-copied packages unguarded.

**The PM-clean bit goes in `LD_H_CLASS`, not in the flags byte.** The
syntheses disagree on where L7's bit and mem-17's movable bit go:
- systemic proposes a free `LD_H_FLAGS` bit;
- the flags byte at header +3 is fully assigned (checked for this document):
  - bit 0 ICON, bit 1 ASSOC, bit 2 PARTS;
  - bits 3-4 compression;
  - bit 5 GLYPH, bit 6 DESK, bit 7 STIK.

So both go in `LD_H_CLASS` (+15), as WP9's header-bit ledger and flagday's L7
say. New kernels then mask the class field. `os88pkg.py:211` and `sched.inc`'s
`%error` move in the same diff.

## 8. Phase 2: the flag day

The plan is `synth/flagday.md`. The text for `docs/plans/ABI-BREAK-LIST.md`'s
new section 13 is `synth/abilist-section.md`. **The bump is `PKG_FMT` 10 -> 13
and `DRV_VER` 12 -> 14** (the list's section 0 item 2: 11 and 12 are the
drivers', and 13 is free for a package). It lands on one branch, with the C SDK
in the same commit as the SDK, and the table is renumbered once.

### 8.1 The existing rows the review touches

| verdict | rows |
|---|---|
| **strengthened** | **K17-4**: -78 under T5 rather than -26, but each demoted cell then pays +150..+300 clocks; it becomes part of V7. **R1**: both forms are still there, with one KEEP caller. **N1**: take it now; only the FSV fallback is resident, 7 bytes. **D3**: the header's name already carries the title. **L1 on kern_big**: -384 bytes of pad, MEASURED paragraph-exact. **L3**: a staged road (L7, D8, mc-10's counter), and figures restated, MEASURED: -1,271 `.mega` (`mega_gpfix` 602, `mega_gpw` 526, `mega_null0` 74, `mega_db` 69), -1,627 with `mega_strcal*`/`mega_strfix`, plus -17..-60 conv; the list's "MEASURED" label on its old figure was wrong. **P2**: fingerprint `VX_ABI_VER`, `XM_ABI_VER` and `HD_ABI_VER`. **V1**: a new package-facing entry beside the preserve-everything body. **V2**: free inside T5's renumber |
| **weakened** | **L1 on kern_small**: +924..+3,280 as written, because a shared `KERNEL_SEG` leaves ten sectors below an image whose FAT window needs two; it needs a design first. **L1 on kern_mega**: 0 conv as written (its rungs stay 512-rounded); up to -128 only if its ladder is packed. **R2-6** (handles): kern_mega's forks are above 1MB, and packages would pay +2..+4 at 367 FREE/MOVABLE sites; it stays recorded, and hm-10 folds in |
| **superseded** | **T1** and **K17-1**: one row, superseded by T5 and kept as its fallback. **K17-3**: superseded by R14 (its ten setters are INLINE cells, ~55 bytes, decoded). **T2-2**: six of kern_mega's ten ICW cells are handler setters, which R14 deletes. **D1** and drivers-9: moot once histsys-7 and D4 land in Phase 1. **F2-1**: BUILT (SPEC.md 20.13.9, `LZ_ESC`). **Section 11**: bullets 1 and 3 done |
| **refused or refused harder** | **T3**: unchanged; T5 recovers -28 of it inside rule 4. **T4**: refused harder. **C3**: refused by arithmetic, -6 kernel against +~300 across 154 sites. **T2-2's remaining four**: XT cycles against ~32 conv bytes |
| **refreshed** | **C1**: now a bytes-and-one-unit row, not a correctness row (-69 under T5 MEASURED, -12 conv kern_mega; packages +0..+150 with a shared helper). **C4**: `APPEND_SYS` alone (zero callers; -17 kern_big MEASURED). **K17-2**: reduced to one runtime `OSAPI_WM_FLAG`. **K17-5** and **C7**: one row, recorded. **R2-1**: `mega_hibnc` is 235 `.mega` today, MEASURED. **R6**: shelved, with wm-R3 refused beside it |

### 8.2 The new rows

| id | row | bump | bytes (signed, ESTIMATED unless marked) | status |
|---|---|---|---|---|
| **T5** | the rare cells behind one software-interrupt door: `int OS88_RARE / db index`, 3 bytes against 5, and 2-byte table words. Its vector lives in 81h-FFh and is virtualised in DOS's AH=25h/35h, windowed and full-screen. IF comes back by `popf`; kern_mega uses a trap gate. An index past this kernel's end answers CF=1, AX=1 (rare cells only) | both | **-325..-385 kern_big and kern_small** (112 rare cells on each, MEASURED decode), **-345..-410 conv kern_mega** (119); packages -2 a rare site, ~2,640-2,770 sites, **-5.3..-5.5KB unpacked**; **+150..+300 clocks (31-63 us) a rare call, PREDICTED** | proposed, OWNER (re-proposes the declined K17-1 with three new arguments) |
| **T6** | one call spelling that can carry T5's flip. Evaluate first the option of overloading `call` once in `os88api.inc`, which touches no package source. The renames (CLIP, FS, DESK, SUBSYS_VERB outliers) come with one cycle of aliases | none | 0 (byte-identical, cmp-proved); every source gate that parses `call OSAPI_X` must learn the spelling and be broken on purpose | mechanism now (WP32), renames at the flag day |
| **R14** | the window class lives in the package: one `W_CLASS` pointer replaces 13 handler words on kern_big and kern_mega (10 on kern_small). Creation flags become the style word. kern_small's refusal signal survives (`OSAPI_WM_CREATE` answers how many class slots it dispatches) | both | **~-320 kern_big (-260..-360)**, **-95..-120 kern_small**, **~-350 conv kern_mega**; packages ~-1.5KB; **+40..+90 clocks a callback, PREDICTED** | proposed, replaces K17-3 |
| **R9** | one refusal sign, documented: CF=1 universal, AX only inside a named family, and a class/action table in SPEC.md 18.4. A universal AX value is refused as written, because AX=1 collides with `EMSE_ROOM`, `DRVE_DISK`, `LD_EDISK` and `FERR_NODISK` | none | 0 | doc half now; universal value open (OWNER) |
| **R10** | deferred damage, opt-in by a class style bit | none / PKG_FMT | +80..+100 | recorded |
| **R11** | `OSAPI_GET_TICKS32`, appended | none | +2 `.bss`, +~6 and a cell | recorded until asked |
| **R12** | one display-index register (CH) on FSX_CAPS/RUN/SURF and VIDEO | PKG_FMT | -6 kern_big | fold-in |
| **R13** | a claim's owner word is the instance slot | PKG_FMT | -20..-40 `.cold` | rides C1 only if the Task Manager is touched |
| **R15** | a format byte on the clipboard | PKG_FMT | +8; packages up to +78 | fold-in |
| **R16** | an extension filter on the Open form | PKG_FMT | +~30 `.cold` kern_big | optional feature |
| **C9** | WRITE_SEQ takes READ_SEQ's caller cursor | both | 0..-30 | recorded; needs a census of concurrent writers |
| **C10** | `OSAPI_DESK_ITEM` remove by record only | both | -6 `.cold` | fold-in |
| **D4** | the driver service-table floor written down | none | 0 resident | Phase 1 |
| **D5** | typed driver slots | DRV_VER | 0..-40 | recorded |
| **D6** | a request block for `DSV_BLK` and `FSV_*` only | DRV_VER | -60..-120 | recorded |
| **D7** | driver discovery from the `.DRV` headers | DRV_VER | **-100..-180 resident** (ul-3: the titles are already not resident, so the first -200..-350 does not hold), overlapping WP4's drivers-10 (-95), so never add the two; +~120 `.boot2`; ~5 s to open the Drivers page on an XT, PREDICTED | recorded, with that price |
| **D8** | the kernel owns driver IRQ lines. The shadow and the scan come now; the `OSAPI_IRQ` cell is an OWNER call now (+100..+150) or mandatory at the flag day | none / DRV_VER | flag day ~0 big/small | Phase 1 half now |
| **F3** | the compression hint moves off the NTRes case bits, which make every packed file list lower-case on NT, Linux and macOS | none | +6 now, -6 at the flag day | reader now |
| **L6** | interrupts-on compaction with an ISR-reachable flag per claim (`GlobalFix`/`HLock`) | none / PKG_FMT | +20 `.mega`, +10..+16 `.cold` | addition first |
| **L7** | a "protected-mode clean" bit in `LD_H_CLASS` (Windows 3.0's `PROTMODE`). Packages only; drivers need a `DRV_VER` bit | none | +12 `.mega` (+8 `.mega` +2 `.bss` for mc-10's counter) | Phase 1 for packages |
| **P3** | the DOS launch block reorganised at the next `KDL_VER` | KDL_VER | 0 | recorded |
| **P4** | T5's door rehearsed on the kernel's own `cw_` shims | none | -45..-90 `.text` kern_big | before T5 |
| **P5** | one 16-byte boot handoff record | none | ~0 | recorded |
| **A4** | the runtime API level (WP9). The level computation is already The Wire's (`WIRE_CELL_LEVELS`, SPEC.md 92.2); the kernel half covers only a hand-copied package (follow-up, 2026-11-08) | none | +~8 `.cold`, 0 word | take it first, if it is cheap (owner, 2026-11-08, second round; the first round said it must prove its worth) |
| **A5** | the appended cells the flag day makes cheaper, recorded so none waits for it | none | - | recorded |
| **V7** | T5's renumber sorts by MEASURED call frequency (`OSAPI_SEG`, 108 sites, and `OSAPI_FS_OP`, 45, are promotion candidates) | inside T5 | +6 a promotion | prerequisite of T5 |

### 8.3 The bundle and its net

**Before it, with no bump:**
- `t_api_abi` on four kernels, which must also decode the index table under
  T5;
- the C SDK inside `PKG_FMT`'s fingerprint;
- the private ABI versions fingerprinted;
- `@ctx` tags;
- `API_RN_DSOFF`;
- A4, if it is cheap (owner, 2026-11-08);
- T6's mechanism;
- V7's frequency reading;
- P4;
- N1, D4 and histsys-7;
- C1's correctness half (BR-105, and the ~20 internal KB callers);
- L7 and mc-10;
- D8's shadow and scan;
- F3's reader;
- R9's doc half;
- an `APICHK=1` kernel for the flag day's soak (it now also carries the six
  package fences decision 3 keeps out of retail, if they are built at all);
- a paper design for L1 on kern_small.

**The order:**
1. **Withdrawals**: C1, C2, C4 (`APPEND_SYS`), R1's bytes form, R14's setters,
   and the demotions V7 supports.
2. **R14** with K17-2's single flag door.
3. **Re-contracts**: R2, R3, R12, R15, C10, R2-4, R2-5, V1's entries, and L7's
   driver bit. Optionally R16, and D8 made mandatory.
4. **T5**, with V2 and T6's renames.
5. **L1** on kern_big, and on kern_mega only if its ladder is packed. A4's word
   moves with `KERNEL_SEG`.
6. **Files on disk**: F1 and F3. kern_mega's R2-1 and R2-3.
7. **The decision and the gates**: `abilock --update` with both bumps; The
   Wire's level reset at 13; `t_api_abi` on four kernels;
   `make budget-check`; the soak when the owner asks for it.

| row | kern_big | kern_small | kern_mega conv |
|---|---:|---:|---:|
| T5 (+ V7's two promotions) | -340 (-325..-385) | -340 (-325..-385) | -365 (-345..-410) |
| L1 | -384 MEASURED pad (0-511 by build) | not taken | 0 (up to -128 if packed) |
| R14 + K17-2 | -320 (-260..-360) | -110 (-95..-120) | -350 |
| C1, C2, C4, R1 | -113 | -72 | -32 |
| R2, R3, R12, R15, C10, F1, F3, V1, R2-4/5 | -14 | +6 | -24 |
| **net** | **~-1,170 (-1,100..-1,260)** | **~-515 (-490..-570)** | **~-770 (-700..-870)** |

How to read the net:
- **kern_big:** about a third is L1's pad. With the pad at its ~256 average,
  the net is about -1,040.
- **kern_mega:** also drops -235 `.mega` (R2-1). L3, when taken, is another
  -1,271..-1,627 `.mega`.
- **Packages:**
  - T5: -5.3..-5.5KB unpacked;
  - R14: ~-1.5KB;
  - C1: +0..+150 with a shared helper;
  - R15: up to +78.

**Out of the bundle:**
- L3, until The Wire serves kern_mega builds (the owner's ranking);
- T1/K17-1, the fallback;
- K17-3;
- K17-5/C7;
- T2-2's remaining four;
- C3;
- D1/drivers-9;
- R9's universal value;
- R6 with wm-R3;
- R2-6;
- R10, R11 and L6, which all have additive forms;
- R13;
- C9, D5, D6, D7, P3 and P5.

## 9. Documentation corrected, and why it drifts

The doc pass, the secondary goal, landed in two commits on `the-big-review`,
both comment and documentation only:

- **Round 1** (`14428309`): 946 fixes - every doc/code mismatch the 25 area
  reviews found and their verifiers confirmed (937 applied mechanically as
  exact, unique quote replacements; 9 overlapping ones merged by hand; 10
  more turned out to be covered by an earlier area's fix).
- **Round 2** (`10912c6e`): 162 fixes from the 12 gap reviews; 212 coherence
  fixes after every one of the 893 edited hunks was re-read in place
  (102 broken markup, 37 contradictions with neighbouring text, 36 garbled
  splices, 22 inaccuracies, 9 duplicates, 4 bad references, 2 voice); 81
  edits settling 46 of the readers' 84 concerns about nearby text (31 left
  as correct or history, 7 recorded in `synth/doc-owner-questions.md`); and
  `docs/plans/ABI-BREAK-LIST.md` section 13.
- **Reach**: 112 files, +3,713/-2,785 lines - SPEC.md +1,326/-1,021,
  CLAUDE.md and AGENTS.md (AGENTS.md given back the byte rules it had lost),
  `apps/os88api.inc` and `drivers/os88drv.inc` contract prose (the ABI lock
  re-recorded twice, no bump), and the comments of 68 files under `kernel/`,
  `boot/` and `kerndos/`, plus drivers and shared includes.
- **Proof that no shipped byte moved**: for each round, a full `make` and
  `make mega` of the edited tree and of its parent at the same commit count
  gave 569 byte-identical artefacts - all 33 images, every package and
  driver, `kern_small` and `kern_mega` - and the fast tier passed (73 rows).

**The systemic causes.** The review found the same shapes of drift in all 25
areas and in all 12 gaps. The two doc passes (commits `14428309` and
`10912c6e`) fixed the instances they found. Seven concerns could not be settled
as text and are in `synth/doc-owner-questions.md`: five are code defects
already in the register (BR-1, BR-23, BR-32, BR-45, BR-196), one is a rule to
decide (whether code may rely on `.bss` arriving zeroed on every load path,
WP7; settled since: zero at `kmain` on every shipped path since PR #61, by the
file layout, `areas/gap-bss-zeroing.md`, owner and follow-up, 2026-11-08), and
one is a stale `SPL_RESIDENT` inside an `%error` string that needs a
code-string edit (WP6). The shapes recur, because nothing catches them:

1. **Hand copies of code.**
   - SPEC.md 3 gave `KERNEL_SEG` 0x0800 against the code's 0x0060, and
     `STK0_SIZE` 1024 against 512 (engsys-3; fixed).
   - A slot contract lives in up to four places that disagree in both
     directions (abi-10, engsys-5).
   - The gap reviews added 13 mismatches around the includes (gi), 22 around
     XMEM (xm), 26 around the hard-disk driver (hdk), and twenty stale places
     about the kernel's `os88ui` copy (ouk).
2. **Running counts in contract prose.**
   - The fast tier's "37" was 76 rows.
   - "Exactly two" `sch_lock` raisers were about forty (ctx-3).
   - "Two users" of `os88pit.inc` were eight shipped carriers (gi).
3. **Citations that resolve to the wrong section.** Six citations of the
   greying standard said 46 and meant 47, and passed `checkdocs` (engsys-2,
   BR-75). Basename collisions silently disable citation checks (engsys-8,
   BR-76). `.c`, `.h`, `.txt` and `.sh` files are not scanned (engsys-9).
4. **History narrated as the current state.**
   - Comments say "it was X until size pass N".
   - SPEC.md's front matter (sections 1-4 and 15-17) describes retired
     designs (engsys-4).
   - The text and wm files run 45-52% comment, much of it per-pass byte
     history (text-14, wm-15).
   - 56% of SPEC.md is package design notes (engsys-18).
5. **Two documents that must agree, kept by hand.** AGENTS.md still differs
   from CLAUDE.md by about 675-705 diff lines (engsys-10).
6. **Rules nothing parses.** 1,476 `; out:` and 804 `; clobbers:` header lines
   (engsys-19). Two prose lists of context rules that disagree (ctx-7).

**The gates that would catch them.** All are host tooling, 0 shipped bytes,
and mostly WP10 and WP24:
- **A heading-drift check against a committed `{number: title}` lock.** It is
  updated deliberately with a reason, as `tools/abi.lock.json` is. It is not
  checked against `git merge-base`, which is confidently wrong on a shallow
  clone. It lands before the per-package SPEC split, and resolves citations
  into the new files.
- **A check of SPEC.md's ~178 fenced `NAME equ N` lines against the source.**
  SPEC.md 3 and 15.1 are generated or deleted inside WP24's front-matter
  rewrite.
- **A COUNTS table fed by `suite.rows()` and the maps.** The rule: a prose
  count either carries its moment or is not written. This is the
  `t_stackprose` shape.
- **`checkdocs` resolves basenames in the citing file's directory first**, and
  scans `.c`, `.h`, `.txt` and `.sh`.
- **`t_mirror` reads `kerndos/`, `os88.h` and the includes' copied
  conventions**; the C SDK goes into `PKG_FMT`'s fingerprint.
- **AGENTS.md generated from CLAUDE.md**, as `docs/INDEX.md` is.
- **A routine reference generated from a fixed header grammar**
  (in/out/clobbers/flags/lock, the GEOS PASS/RETURN/DESTROYED shape), with
  `tools/os88live.py`'s liveness pass as a correctness lint over it (WP14).
- **History moves to `docs/history/` once.** Comments state the INVARIANT and
  the guard that enforces it (boot-9's pattern; `KERN-BUDGET-LEDGER.md`
  already does this for the budgets).
- **`checkdocs` warns, rather than fails, in `docs/reports/` and
  `docs/history/`.** This review's own in-progress reports once failed `make`
  (engsys-1, BR-74).

## 10. Decisions for the owner

Every item the syntheses mark owner or needs-owner, with the options. The
owner answered in two rounds on 2026-11-08
(`big-review-2026-11-08/owner-decisions.md`, which is the record and binds).
So the section is split: what is **decided**, with the answer, and what is
**still open**, grouped by when it blocks work. Each item keeps its number from
the first version of this list, because the plan and the follow-ups cite them.

### Decided

- **1. The fatal panel's purpose** (gfce-2). The options were (a) halt as a
  bug report; (b) close the faulting package and keep the desk, as
  `mega_fkill` does (+45..+65 more, plus a skip decoder on a 286+); (c) halt
  only for a kernel CS, and IRET otherwise on an 8088. **The answer (owner,
  2026-11-08):** on kern_mega, where the kernel can afford not to crash,
  closing the faulting package beats killing the OS, and it already does. In
  real mode a divide fault deserves at least a stack-overflow-style message
  where it cannot be recovered from, because a picture of it lets the owner
  trace who divided. But it has not been met in production, so the panel's
  resident bytes, the ~174-byte package answer included, wait behind a build
  KNOB, off in shipped kernels, until a field report says it is being hit:
  "that is a balancing act". **The same weighing applies to every diagnostic
  panel for a failure nobody has met** (recorded in CLAUDE.md and AGENTS.md).
  Shipped bytes: 0 (was ~+55 big / ~+40 small resident, +12..+20 `.ovl`).
  The follow-up applies that weighing to the review's other diagnostics for
  failures no machine has met - BR-84's table bound, BR-80's tripwires,
  BR-82's ISR-stack canary, BR-68's `[mega_kfix]` counter and BR-173's
  optional panel - which go behind knobs and out of the totals (section
  4.1).
- **2. A 14th blob sector on kern_big: NO** (owner, 2026-11-08). It delays the
  progress bar by 0.4-0.8 s, one or two more int 13h. Options (b) boot-17's
  table and (c) `vid_memchk` into `.boot2` fall with it. The route is
  `docs/plans/completed/BOOT3-PLAN.md`: a third boot-time section at the tail of
  KERNEL.SYS, read while the splash is already up and dropped before the
  `.vga` drop and the heap compaction, so that `.boot2` goes back to "draw the
  splash and load kernel.sys". **Its own questions, second round:** generic
  across the kernels, kern_small included unless it truly does not need it;
  the name is whatever confuses a new session reading the boot least; wave 4
  YES; no kern_big 128KB boot row (there was one, and it was taken out);
  `blobruns` into the fast tier if it is really quick, or full; the earlier
  bar without wave 4 only if it is less complex than wave 4.
- **3. Packages are trusted** (owner, 2026-11-08). Real mode cannot defend
  against a hostile package (and on kern_mega an FSX real-mode bracket hands
  it real mode anyway), so the only thing worth defending against is our own
  mistakes, and most of the time those are better found and guarded at BUILD
  time than with resident bytes. Resident guards pay where the failure is
  catastrophic and user-visible and the printout tells the owner something,
  as the stack-overflow panel does. **The six package-argument fences (BR-8's
  doors +36, BR-22 +12, BR-29 +13, BR-30 +18, BR-37 +16, BR-173 +12) are not
  taken as retail code**: at most an `APICHK`-style knob (option (b)). BR-8's
  bound goes only with its doors, by this item's own rule, so the `div` stays
  as it is in retail. The follow-up applies the same weighing to BR-85's code
  half, BR-107 and BR-109's optional doors, and leaves BR-104 out of the
  sums while it stays open (item 11; section 4.1). The fences against hostile FILES (BR-7, BR-23, BR-44, BR-59) are not
  in this class (`synth/bugs.md`). Recorded in CLAUDE.md and AGENTS.md.
- **4. WP2b, FILECP and FDLG on kern_big as modules cached at boot:
  REFUSED** (owner, 2026-11-08), in any form, as it has been "20+ times now
  (almost once per size pass)". kern_small can afford them as modules: a
  one-disk distribution with no hard disk, drivers or DOS, where running a
  program at all beats managing and opening files faster. kern_big cannot: the
  user has swapped in a DOS, office or utility disk and cannot be made to
  insert the system disk and wait every time a file is opened or copied, and
  "open and copy file are not 'system management' actions". A boot-time cache
  is not a saving either. Recorded in CLAUDE.md, AGENTS.md and SPEC.md 38 so
  that it is not proposed again.
- **6. rt-7, the keyboard mouse: TAKEN, as "instead of", not "always, then
  dropped"** (owner, 2026-11-08, second round). The keyboard mouse is
  resident on every kernel today; one cluster on disk for a decent amount of
  resident RAM is a good trade, but no boot may get longer, so it is loaded
  ONLY when no mouse is found. **The preferred shape** (owner, 2026-11-08,
  over a `KBMOUSE.DRV` module): the pointer half rides `.boot3` and is copied
  into a heap claim before `.boot3` is dropped, only on a machine where no
  mouse was found, and dropped later if a late mouse speaks. No disk read, no
  cluster, no system-disk dependency; `kbm_isr` stays resident. Figures
  (follow-up, 2026-11-08, priced for the module shape): about **-300..-320**
  static resident (not -290..-380: `kbm_poll0` stays); a mouseless machine
  holds the claim in whole 512-byte heap granules, about **+130..+220** for
  the session (not +85..+165), PREDICTED. The module's +400..+450 on disk
  becomes about the same bytes inside KERNEL.SYS's `.boot3`, read after the
  bar; the shape's own glue (the copy, the gate) is for S11 to measure.
- **7. text-7, `font_run_x`'s VGA prologue into `.vga`: TAKEN** (owner,
  2026-11-08, second round). About -177 on a mono kern_big, +16 on the VGA
  gauge; Note Pad's real aligned runs gain ~+5..+15 clocks, and a full
  maximised repaint gains 0.048% at the review's 40 clocks a run (0.78% in
  the absolute worst case of one run per cell), PREDICTED (follow-up,
  2026-11-08).
- **8. rt-5, the mono residency figure: PRINT IT** (owner, 2026-11-08, second
  round), option (a): a `classes` line in `tools/kernsize.py` (VGA, EGA, mono,
  mono with `.cold` in a ROM or the HMA), 0 shipped bytes. The figure already
  exists as `MONO_FLOOR`: mono kern_big resides in **94,208** against the
  gauge's 96,768, MEASURED (follow-up, 2026-11-08). No `KERN_MONO_BUDGET`
  gate.
- **9. WP11, the KHP round: per row** (owner, 2026-11-08), option (b). It is
  waiting to be revived, and each row is reviewed on whether it earns its
  space. Its separate half (kern_small's losing arms) is still open below.
- **The `.bss` rule** (WP7; `synth/doc-owner-questions.md`). "iirc we didn't
  used to, and now do": confirmed from the code. Kernel `.bss` is zero at
  `kmain` on every shipped path since PR #61, by the file layout rather than
  by an instruction, and the comments are corrected
  (`areas/gap-bss-zeroing.md`). The proposed gate is `tests/unit/t_bsszero.py`
  (fast, 0 shipped bytes).
- **BR-6, a plain MDA driven with Hercules timings** (owner, 2026-11-08). It
  can damage the monitor. os8088 does not run on an MDA: print an MDA message
  and halt, or switch to the usable CGA when one is present.
- **The run-time API level (A4, abi-6, gfr-5): TAKE IT IF IT IS CHEAP**
  (owner, 2026-11-08, second round). The first round said it must prove its
  worth, because packages and drivers already carry versions checked before
  anything runs. The second round: the loaders gate on `<=` PKG_FMT rather
  than `==`, so a package newer than the kernel can still call a cell the
  kernel lacks, and a level check refuses that rather than crash. The level
  computation already exists as The Wire's (follow-up, 2026-11-08), so the
  cost is the word at `KERNEL_SEG:0006` (0 bytes) and ~8 `.cold` for the
  check.
- **`.LNK` and a desktop that is a folder.** Shortcuts and `.LNK` links exist
  (SPEC.md 26.8, 26.10). The refusal of a desktop that is a folder stands:
  it was looked into, it is not a bad idea at its core, and it is too
  expensive in disk reads and resident bytes for a machine without a 95-era
  system's resources (owner, 2026-11-08; section 5's list is reworded).
- **Theme 1's further context work: not needed immediately** (owner,
  2026-11-08). The mouse and idle already have stacks of their own. The field
  problem the owner knows of is a sound interrupt arriving on a package
  already near its stack's edge; it was not at shutdown.
- **Package-purgeable claims (hm-9): parked** (follow-up, 2026-11-08, on the
  owner's request for real use cases). Not needed now; if built, justified by
  Sheet's ~80KB of modules and by keeping lazy parts until the heap needs
  them, not by the two consumers the review cited; and the zero-kernel-byte
  alternative (Sheet dropping its modules after use) is the comparison to put
  first (`followup-purgeable.md`).

### Still open

**Before Phase 1's sessions that need them:**

- **5. rt-6: cache modules on kern_small too.** Options: (a) +60..+85,
  letting the 128KB machine open Save As on a data floppy until heap pressure;
  (b) leave it as today. The owner finds it interesting: "keep FDLG as
  purgeable" might be worth ~100 bytes (owner, 2026-11-08). Not yet a yes.
- **9, its second half: kern_small's losing KHP arms.** Delete them (+74
  there), or keep them under `%else`.
- **The API level's comparison.** The owner's second round speaks of "an `==`
  check on an API level ... refusing to crash on an old apps disk in a new
  kernel, or the reverse". The review's design refuses only a package whose
  needed level is ABOVE the kernel's, because cells are only appended and an
  older package runs on a newer kernel. Confirm which is meant before session
  5 builds it.
- **BOOT3-PLAN's remaining questions** (`docs/plans/completed/BOOT3-PLAN.md` section
  15, not copied here). The second round answered questions 1, 3, 4 (kern_small
  opts in unless it truly does not need it, which is a finding the work has to
  make), 5, 6, 7 and 10. Open: 2 (wave 2's boot-time RAM), 8 (the clock ladder
  whole or two rungs) and 9 (`B2CALL`'s constant segment).
- **The Pravetz report.** Which disk did that machine boot? No record says.
  `followup-canary.md` has the one 86Box run that tells an erroring ROM from a
  lying one; then ask the reporter.
- **10. ctx-6: kern_small's shed gate.** Options: (a) +14 `.text`; (b) keep
  SPEC.md 66.3.1's exclusion and write the shed case into it.
- **11. BR-104 / hm-2: close `mem_find_own`'s self-region arm for FREE and
  REGROW.** Options: (a) +6..+8, reversing an explicit SPEC.md 66.6.1.1
  decision; (b) record the self-harm acceptance; (c) put it in `APICHK` only.
  Decision 3's weighing reads on it (a package harming its own region), but
  the owner has not answered it.
- **12. BR-60 / dw-12: subdirectories grow.** Options: (a) +90 `.cold`, the
  first item to cut; (b) keep SPEC's "the kernel does not grow a directory".
- **13. dw-14: contiguous allocation from the known size** (session 15).
  Options: (a) +70 `.cold`, with SPEC 18.4's non-goal rewritten first;
  (b) drop it.
- **14. The `OSAPI_IRQ` cell (D8).** Options: (a) now, +100..+150 resident
  kern_big, SOUND.DRV -60..-100; (b) only at the flag day; (c) never
  (shadow and scan only).
- **15. gkm-6: retire kern_emu** for a USBMOUSE-shaped VMMOUSE.DRV. Options:
  (a) prototype it (+~40 kern_big, -385 emu-only, unproven drag
  responsiveness); (b) keep kern_emu.
- **16. hdk-2: may a package erase a hard-disk partition without a tool-owned
  confirm?** Options: (a) as today; (b) require an HDDTOOL token.
- **17. hm-12a: `MEM_OWNER_MAX` 8 -> 10 on kern_big.** 0 bytes; a policy
  against SPEC.md 50.2's justification of 8.
- **18. BR-65 (kern_mega): an option-ROM IRQ on an unlent foreign stack.**
  Options: (a) reflect it on a small fixed kernel stack, +20 `.mega` and
  +128..+192 `.lowbss` conventional; (b) mask the line and replay it at the
  next LOW_SEG reflection; (c) make `SEG_LEND` mandatory for a package that
  keeps its own stack, 0 bytes (SDK text). Batch H takes (c) now and (a)
  only on the owner's word.
- **19. engsys-15: the threshold** at which a scoped soak selection counts as
  the whole tier and needs `--user-asked` (proposed ~90%).
- **20. wm-19: retire `band.inc`'s `BAND=1` knob** under WP11's "no
  indefinite shelf".
- **21. hiber-19: keep `HIBERNAT.IMG` allocated** between hibernates
  (unchecked).

**For the flag day:**

- **22. T5 or T1.** Options:
  - (a) the software-interrupt door at +150..+300 clocks (31-63 us) a rare
    call, with a vector in 81h-FFh that DOS virtualises, and a residual risk
    from a DOS program that writes the IVT directly;
  - (b) T1's five-byte cells;
  - (c) neither.

  K17-1, T1's cousin, was declined before.
- **23. The IF rule.** Is any rare cell legitimately called with IF=0? "IF=1
  callers only" drops the FLAGS restore from the door. The `@ctx` tags
  answer it.
- **24. R14 and the C SDK.** Options: (a) the C SDK emits a class table from
  the setter calls it sees; (b) C packages declare one.
- **25. L1 on kern_small.** Options: (a) design fixed-size kern_small content
  for the region below a shared `KERNEL_SEG`; (b) keep L1 a kern_big row
  and let kern_small pay the hole. Also: does kern_mega pack its ladder, so
  L1 is worth up to -128 there?
- **26. R9's "not on this kernel" value.** Options: (a) AX=0, which is unused
  as a refusal in every family but moves XREFUSE and needs a census of its
  AX=1 readers; (b) the doc half only. AX=1 is refused.
- **27. gkm-9: is a one-disk 8088/286 product a goal?** It bears on L3, and
  runs against the standing rule that no release carries kern_mega's disks.
- **28. L3's timing.** It stays blocked on The Wire serving kern_mega builds,
  by the owner's ranking. Confirm, or reorder.

## 11. What was NOT covered

**No emulator ran.** No finding was booted, soaked or measured for this review.
Every "MEASURED" figure above is either:
- a span or count read off the three `kernel.map` files, the `kernel-full.bin`
  decodes or a listing; or
- a number the tree already carried from an earlier row or field run (the
  cursor's 15,100/11,500/21,450 cycles, DFREE's ~105 ms, the 96.9% idle).

The 86 ms per 32KB regrow that `kernel/memory.inc` quotes is arithmetic
(16K words at ~25 clocks), not a measurement (ul-11), and `synth/phase1.md`'s
list of measured figures is wrong to include it.

No defect in the register was reproduced. Almost none is exercised by any row,
which is why they survived. **BR-4's stack overflow at close is PLAUSIBLE, not
measured.**

**The measurements the recommendations wait on.** No row produces any of them
today. Register them and run their union once, in the final pass:

| measurement | row to register | decides |
|---|---|---|
| a dying 128-byte worker's stack depth (Bounce closed over a second window) | `stkdie` | BR-4's urgency |
| a worker's slices during a 16KB floppy read, and the UI task's own wall time for it | `xferfreeze` on `os8088_5150_cga_gla` | histsys-5's disk-freeze release |
| IF=0 spans of a regrow and of the mouse ISR | `ifoff --regrow`, `ifoff --mouse` | BR-12, BR-21, the budget in WP13 |
| UI latency with N workers | `uilat --workers N` | WP18's one-shot boost |
| serial lease at divisor 12 with the mouse moving | `serlease` leg | BR-21's ceiling (2,400 baud on an 8250) |

**New rows the defects need.** None exists today:
- a hostile-header package (BR-7);
- a negative-width BLIT4 call that goes red on `jz` (BR-3);
- a package dividing by zero in a callback, on MartyPC GLaBIOS and as a `box-`
  row on 86Box's 286 (BR-9), now against the knob build only (owner,
  2026-11-08);
- a wild-pointer WM call (BR-8), now an `APICHK=1` row only (owner,
  2026-11-08);
- a shed during a mount on the 128KB machine (BR-2);
- unticking a second driver while USBMOUSE's worker runs (BR-5);
- Esc on the Resume face (BR-10);
- a pointerless desktop driven by the arrow keys on CGA, and after a resume
  (WP3). Corrected (follow-up, 2026-11-08): the keypad half exists as
  `wirefleetnomouse` (soak), which puts the probe back to "nobody has spoken"
  and drives every gesture with the keypad and Space, and `mouerrq` covers
  the keyboard mouse retiring. What is missing is a row named for the
  keyboard mouse, a CGA leg, the after-resume leg, and now a boot with a
  mouse that proves no claim is made, plus a late mouse (USBMOUSE.DRV) that
  drops the claim;
- shed FILECP, then Save As (WP2b): withdrawn with WP2b (owner,
  2026-11-08);
- Sticky Apps pressed twice gives one disk load (WP2a);
- an MDA-primary, an MCGA and a 64KB-EGA machine profile (BR-6, BR-55,
  BR-56): none exists in MartyPC or 86Box today;
- `KSIG_OFF` back at 4096 turning all four 9-sector images red,
  `os8088-360.img`, `os8088-720.img`, `small360.img` and `small720.img`,
  once `t_canary`'s `crosses()` models what stage 2 really does: start at
  KERNEL.SYS + `BOOT2_SECS`, read `KZ_HEADSEC` in runs of its own, restart
  the tail there, apply the DMA-page bound (BR-1; follow-up, 2026-11-08);
- `rdiag360.img`, FIELD-NOTES 31's survey instrument, no longer tests
  cylinder runs at all, because stage 1's flat arm is now track-bounded: give
  it the cylinder bound behind a define, or retire it (follow-up,
  2026-11-08).

**Predicted numbers that need a measurement before they are quoted as fact:**
- the regrow copy at IF=0 (~86-170 ms);
- isr-7's baud ceilings;
- every cycle figure for T5's door (+150..+300), R14's class lookup (+40..+90)
  and input-2's in-service test (55-65);
- the boot cost of BR-1's track-bound head (~170-400 ms on four disks; the
  14th blob sector it was set beside is refused);
- BOOT3-PLAN's load after the bar (about +24 ms at its wave 1, +80 ms at
  wave 2 on 360KB);
- D7's ~5 s Drivers page;
- kern_mega's ~16 ms compaction piece, which is derived from the Victory
  286's MEASURED 0.257 ms/KB.

Every byte figure not marked MEASURED is an estimate from encodings, and
nobody assembled any of them.

**Covered thinly.**
- **Unchecked findings.** 54 of the 467 surviving first-round findings were not
  re-derived by their verifier. The share is highest in megados (6 of 25), ui
  (5 of 21), diskread, files and text (4 each). `gap-unchecked-loadbearing`
  re-checked the ones recommendations rest on, and found the mechanisms mostly
  hold and the magnitudes mostly shrink.
- **kern_mega's real-CPU behaviour.** Limit-checking faults (BR-13, BR-64,
  BR-65) are visible only on Bochs or 86Box, and QEMU cannot fail them. The
  286 reset path runs only on 86Box. None of these was run.
- **Real hardware.** BR-6's monitor risk is lore, not verified by this
  review; the owner treats it as real ("this can damage the monitor") and
  decided the MDA rule on it (owner, 2026-11-08). BR-183
  (HDD.DRV's ECC-corrected read) and BR-210 want field runs
  (docs/FIELD-MACHINES.md).
- **Outside the kernel proper.** Packages and the C toolchain were reviewed
  only where they meet the kernel: `crt0`, the includes, DOS.O88's XMS and IMR
  faces. ETHER.DRV's TCP/IP stack, SOUND.DRV beyond its kernel seam, and the
  Weave and ARCADE families were not reviewed. kern_emu was reviewed only as
  a matrix question (gkm-6).
- **The line references** are against the review branch at the time each
  report was written (several HEADs between `5e4740bb` and the final
  syntheses). Re-read a line before editing it.
