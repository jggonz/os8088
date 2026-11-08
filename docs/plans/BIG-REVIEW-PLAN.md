# BIG-REVIEW-PLAN - the two phases of the 2026-11-08 kernel review, as sessions

**Status: A PROPOSAL. Nothing in it is taken, nothing is scheduled, and no
session below has started.** Written on 2026-11-08 on the branch
`the-big-review`, from the review's final synthesis. This is the plan for
the work. The review itself is the report under
`docs/reports/big-review-2026-11-08/`. A finding's evidence, mechanism and
corrected figure live there and are not copied here. This file says who does
what, in which order, on which branch, and what "done" means. Tick section 6
as work lands. When the last row is ticked, move the file to
`docs/plans/completed/` in the same commit (docs/README.md).

## 1. Sources, and how the review was run

The owner asked for a comprehensive, blue-sky review of the kernel: what is
good, what could use work, what would be better done another way, and what DOS,
Windows 3.0/3.1, OS/2 and System 5+ did better at a systemic level. The
recommendations were to stay clear of large byte growth, and ABI breaks were
allowed. The work was to come in two phases. Phase 1 is what can be done now
without a break. Phase 2 is a flag day that adds the breaks to the size
optimisations already listed in `docs/plans/ABI-BREAK-LIST.md`. As a second
goal, comments, documents and SPEC text that disagree with the code were to be
corrected.

**How it was run.**

- 25 deep reviewers, each followed by an adversarial verifier: 467 findings, 2
  refuted.
- 2 completeness critics, who named 12 gaps. Each gap was reviewed and
  verified: 132 more findings, of which two were refuted, gfce-4 and gms-7.
  `bugs.md` drops them, with the first round's two (isr-5, wm-20).
- 5 cross-area synthesists.
- A 3-lens challenge panel (truth, cost and fit) over the top 40
  recommendations. None was refuted outright, and most were amended.
- 4 revision writers.

Nothing was assembled into `build/` for any finding. Nothing was booted or
soaked. The review was read-only. Host builds were used only to prove that the
doc pass changed no shipped byte.

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

**Inputs.** Each plays a part in this plan. Paths are relative to
`docs/reports/big-review-2026-11-08/`.

| file | what it is | what this plan takes from it |
|---|---|---|
| `synth/bugs.md` | the final defect register: 212 items (BR-1..BR-212), in nine fix batches A-I | each session's bug batch, its byte figures, its rows, and the *Cross-cutting constraints* |
| `synth/phase1.md` | the final no-break plan, WP1..WP34, with an owner for each finding and a session order | each session's work packages, their files, rows and dependencies |
| `synth/flagday.md` | the final phase-2 plan: rows, bundle order, net bytes | section 4 |
| `synth/abilist-section.md` | the text to be appended to `docs/plans/ABI-BREAK-LIST.md` as its section 13 | the flag-day rows and their status words |
| `synth/systemic.md` | 13 ranked systemic themes, the period-OS comparison, and what not to copy | the *why* in each goal; the ordering across themes |
| `synth/keep.md` | what is good: the ten strongest patterns (K1..K10) and the 25-area scorecard | what no session may "fix" (`keep.md:873`) |
| `synth/challenge-{truth,cost,fit}.json` | three lenses over the top 40 | already folded into the figures quoted here |
| `synth/doc-owner-questions.md` | the doc pass's seven concerns that were not a text fix: each a code defect already in `bugs.md`, or a rule to decide | nothing new; the `.bss` rule (item 3) is S02's, the canary (item 2) S03's |
| `areas/*.md`, `data/*.json` | 25 area reviews and 12 gap reviews, verified | evidence behind each finding id |
| `docs/reports/BIG-REVIEW-2026-11-08.md` | the review's report to the owner, written from the synth files | the answers to the owner's questions this plan does not carry (below) |

**Where each of the owner's questions is answered.** This plan answers the
last four. The report answers the first four, from `keep.md` and
`systemic.md`.

| the owner asked | answered in |
|---|---|
| What is good? | the report's section 3; `keep.md` (K1..K10, the scorecard, and Part 4's "do not fix" list) |
| What could use work? | the report's section 4; `bugs.md`; `systemic.md`'s ranked themes |
| What would be better approached an entirely different way? | the report's section 5; `systemic.md` themes 1-13 |
| What did DOS, Windows 3.0/3.1, OS/2, System 5+ (and GEOS) do better, systemically? | the report's section 6; `systemic.md`'s per-theme comparison and *What we would NOT copy* |
| No huge byte growths | every handoff's byte table, signed by section and kernel; the session table (2.2); the phase totals |
| Two phases: now with no break, then a break | phase 1 is sections 2, 3 and 6; phase 2 is section 4 |
| The break batched with the existing ABI-break list, where its rows still apply | section 4. `docs/plans/ABI-BREAK-LIST.md` section 13 is already appended (identical to `synth/abilist-section.md`); its 13.1 says which existing rows still apply, are superseded or are refused |
| Multi-session, handoffs, an automated cloud setup | the handoffs are section 3; the cloud setup, an orchestrator session and the one-session alternative are section 5 |
| Comments, docs and SPEC that disagree with the code | the review's doc pass (its figures are above); what is left is S26-S29 |

**Reading the figures.**

- Every byte figure is signed (minus is a saving) and placed in a section and
  a kernel. A figure is ESTIMATED from instruction encodings unless it says
  MEASURED. MEASURED means read off `build/kernel.map`, `build/smallk/kernel.map`
  or `build/megak/kernel.map` at the review's tree.
- Every timing is PREDICTED (8088 tables and the fetch floor) unless it says
  MEASURED.
- *Resident* is `.text`, `.bss`, `.cold`, and `.vga` on VGA/EGA. **`.cold` is
  resident on kern_big and kern_small**, billed by `KERN_BUDGET` exactly like
  `.text`.
- *Transient* is `.boot2` (the blob), `.ovl`, the boot sector and boothd.
- kern_mega figures are *conv* (conventional, `MEGA_RESIDENT`) / *high*
  (`.cold`/`.mega` above 1MB, not budgeted).
- kern_emu takes kern_big's figure unless stated.
- Where `phase1.md` and `bugs.md` priced the same item differently, the later
  lens-corrected figure is the one quoted, and the item says which. The main
  case is BR-6: +60..+110 `.boot2`, not phase1's ledger figure of +25..+35
  (BR-6 (`bugs.md:198`)).

## 2. Sessions and their order

### 2.1 The shape

Phase 1 is **29 sessions in 7 waves**, the seventh taken in two steps (7,
then 7b). Each session is one of:

- a bug batch from `bugs.md`, or half of one, plus the WPs `phase1.md` says
  travel with it;
- a group of WPs that share files.

Sessions in the same wave can run **in parallel**: their files are disjoint,
or their shared files are split by routine (the *Neighbours* line in each
handoff). Waves are **serial**. A session is cut from the integration branch
after the waves it depends on have merged. Within a wave, merges follow the
order given.

The order is `phase1.md`'s *Recommended order of work* (`phase1.md:2869`), with three
changes:

- Sessions that touch disjoint files share a wave.
- The bug batches are sized to sessions: G and I are split in two, and batch B
  lends BR-14 to the boot session, as `bugs.md` asks.
- Each item has one owner. Five items move from where `phase1.md` placed them:
  - WP13 goes with batch F, not batch D. Its "Travels with" line says so, and
    its IFLAT knob is in `sched.inc`.
  - WP8's door half (BR-8) goes to batch C, as its own text allows.
  - WP22's IMR shadow goes to batch F, so that batch F's four IMR
    read-modify-writes use it: "the shadow first" (WP22 (`phase1.md:1738`)).
  - BR-195 goes from batch F to batch C.
  - The `kernsize` tally line for the two transient homes goes from WP31 to
    S01, so that later sessions print the ledger instead of keeping it by hand.

### 2.2 The session table

"res" is resident. Figures are the session's core, ESTIMATED; each handoff
gives the full table.

| S | session | items | wave | parallel with | depends on (merged) | kern_big res | kern_small res | kern_mega conv |
|---|---|---|---|---|---|---:|---:|---:|
| S01 | gates | WP10, batch A's host gates, kernsize home tally | 1 | S02, S04 | - | 0 | 0 | 0 |
| S02 | free bytes, `.bss` rule | WP1, WP7 | 1 | S01, S04 | - | -144..-166 | -165..-208 | -87..-121 |
| S04 | contracts, context tags, APICHK | WP14, WP19 | 1 | S01, S02 | - | 0 | 0 | 0 |
| S03 | boot | WP6, batch A core, BR-14 | 2 | S05 | S01, S02 | +2 (BR-14) | +13 (BR-14) | 0 |
| S05 | API level, contract text | WP9, WP26 (+ R9's doc half) | 2 | S03 | S04 | +0..+5 | +0..+5 | -4..-8 |
| S06 | faults, ISRs, input | WP8 panel, WP13, WP22 shadow, batch F | 3 | S07, S08, S13, S14 | wave 2 | +129..+144 | +105..+120 | ~+99 |
| S07 | WM, UI, teardown | batch C, WP21, WP8 doors | 3 | S06, S08, S13, S14 | wave 2 | +98 (+59..+136) | +82 (+45..+118) | ~+88 |
| S08 | loader and heap | batch D, WP17, C1's callers | 3 | S06, S07, S13, S14 | wave 2 | +99 | +104 | 0 |
| S13 | kern_mega | WP5, batch H | 3 | S06, S07, S08, S14 | wave 2 | 0 | 0 | -18 (+ high) |
| S14 | disk and files | batch E, WP16 | 3 | S06, S07, S08, S13 | wave 2 | +145..+156 | +95..+100 | +6..+21 |
| S09 | module drop-to-cache | WP2a (rt-1, rt-9, shell-2) | 4 | S12, S15, S17 | S07, S08 | +60..+85 | 0 (O5: +60..+85) | 0 |
| S12 | drivers, matrix gates | WP4, WP22 driver half, batch G driver half, WP31 | 4 | S09, S15, S17 | S06 | -6..-66 | -10 | -184..-194 |
| S15 | FAT write path | WP12 | 4 | S09, S12, S17 | S14 | +160..+255 | 0..+3 | 0 |
| S17 | display and hot paths | batch B, WP20, WP23 | 4 | S09, S12, S15 | S03, S07 | +159..+185 | +40..+52 | +63..+75 |
| S10 | FILECP/FDLG cached on kern_big (**O4**) | WP2b (rt-2) | 5 | S16, S19, S20, S21 | S09 | **-2,468..-2,508** | 0 | 0 |
| S16 | scheduler | WP18 | 5 | S10, S19, S20, S21 | S06, S12 | +50..+77 | +50..+77 | +50..+77 |
| S19 | SDK-side work in packages | WP15 | 5 | S10, S16, S20, S21 | S08, S14 | +42..+60 | +24..+32 | +30..+40 |
| S20 | desk, dialogs, menus | WP27 | 5 | S10, S16, S19, S21 | S07, S14 | -15..-25 | -21..+9 | -21..-31 |
| S21 | hibernate, clock, services | WP29, WP30, batch G services half | 5 | S10, S16, S19, S20 | S12, S13 | -8..+12 | -8..+12 | -8..+12 |
| S11 | keyboard mouse, PS/2 overlay (**O6, O7**) | WP3, input-10 | 6 | S18, S22, S23, S24 | S09, S12, S16 | -494..-599 | -290..-380 | -290..-380 |
| S18 | the KHP round (**O8**) | WP11 | 6 | S11, S22, S23, S24 | S17 | +286..+289 | 0 | 0 |
| S22 | HDD.DRV | WP33, batch I's HDD half | 6 | S11, S18, S23, S24 | S13, S14 | 0 | 0 | 0 |
| S23 | XMEM.DRV and DOS.O88's XMS | WP34, batch I's XMEM half | 6 | S11, S18, S22, S24 | S13, S21 | -54 | 0 | 0 |
| S24 | Control Panel engine | WP28 | 6 | S11, S18, S22, S23 | S17, S21 | -10 | 0 | 0 |
| S25 | the OSCALL rewrite, the `cw_` door | WP32 (T6, P4, V7's counter) | 7 | S26 only | waves 1-6 | -40..-60 | 0 | 0 |
| S26 | docs: front matter, generated copies, the comment convention | WP24a | 7 | S25 | S01, S04 | 0 | 0 | 0 |
| S27 | docs: SPEC's package sections split out | WP24b (engsys-18) | 7b | S28, S29 | S25, S26 | 0 | 0 | 0 |
| S28 | docs: the Makefile split | WP24c (engsys-21) | 7b | S27, S29 | S25, S26 | 0 | 0 | 0 |
| S29 | the comment discipline sweep | WP25 (split by file inside the session) | 7b | S27, S28 | S25, S26 | 0 | 0 | 0 |

How some cells above were derived:

- **S02** adds WP1's figure to WP7's 0..-20.
- **S03**'s resident cost is BR-14's alone. Its real spend is `.boot2` (2.5).
- **S06** is batch F's core without BR-9 (+59 / +40), plus WP8's panel and
  stubs (+45..+60 / +40..+55), plus WP22's shadow (+25 / +25; kern_mega +39).
- **S07** is batch C's core (+108 / +92 / ~+98) plus WP21's -10.
- **S08** is batch D's core, plus WP17's +8 and loader-8's -3 on kern_big. On
  kern_mega, loader-8's -20 is high. `bugs.md` and `phase1.md` each disclaim
  loader-8, and `phase1.md`'s ownership table gives it to batch D, so it is
  counted here.
- **S12** is WP4 plus batch G's driver half (BR-5, BR-31, BR-32, BR-34's
  scan: +81 on kern_big; .bss +11 on kern_mega).
- **S14** is batch E's core plus WP16.
- **S17** is batch B's core without BR-14, plus WP20's gauge figure. A mono
  kern_big machine pays about 80..90 less of it (WP20's mono figure is
  +40..+56).

The phase totals are `phase1.md`'s (`phase1.md:2827`) plus the bug batches'
(`bugs.md:1412`). The two are not additive where an item is owned twice; the
handoffs count each item once.

### 2.3 Waves at a glance

```
wave 1  S01 gates ........ S02 free bytes ........ S04 contracts/tags
            \___________________|______________________/
wave 2  S03 boot (needs S02's blob/.ovl room) .... S05 API level + contract text (needs S04)
            \_____________________________________/
wave 3  S07 WM/teardown   S08 loader/heap   S06 faults/ISR   S14 disk/files   S13 kern_mega
            merge order:  S07 -> S08 -> S06 -> S14 -> S13
wave 4  S12 drivers   S09 drop-to-cache   S15 FAT write   S17 display
            merge order:  S12 -> S09 -> S15 -> S17
wave 5  S21 hiber/clock   S16 scheduler   S19 SDK includes   S20 desk/dialogs   [S10, owner-gated]
            merge order:  S21 -> S16 -> S19 -> S20 -> S10
wave 6  S22 HDD.DRV   S23 XMEM/DOS XMS   S24 CP engine   S11 KBMOUSE/PS2   [S18, owner-gated]
            merge order:  S22 -> S23 -> S24 -> S11 -> S18
wave 7  S25 OSCALL + cw_ door (ALONE among code sessions)   S26 docs front matter
wave 7b S27 SPEC split   S28 Makefile split   S29 comment sweep
then    the phase-1 final pass, once (section 5.5); then phase 2 (section 4)
```

The widest wave is five sessions. Why the merge orders are what they are:

- **Wave 3.** S07's BR-4 and S08's BR-53 unblock S09 earliest. S06 then takes
  its `.ovl` bytes before S14 takes BR-141's. S13 is kern_mega-only and the
  least likely to conflict.
- **Wave 5.** S21 provides the `.ovl` relief (2.5) before anyone else's
  overlay want lands.

### 2.4 Hotspot files: who touches them and how to sequence it

| file | sessions | rule |
|---|---|---|
| `SPEC.md` | every session | Edit only the sections your handoff names. A new numbered heading goes only under a parent your session owns. A new top-level number is assigned by the orchestrator in `DECISIONS.md` (5.3), never "the next free". S27 moves the package sections, so no other session edits SPEC.md while S27 runs (S28 and S29 do not touch it) |
| `apps/os88api.inc` + `tools/abi.lock.json` | S01 (re-records inputs), S04, S05, S06, S07, S12 (via `drivers/os88drv.inc`), S14, S16, S19, S20, S25 | **Never hand-merge `abi.lock.json`.** On a conflict, take either side, then run `python3 tools/abilock.py --update "<both reasons>"` on the merged tree. Cells are **appended in merge order**: a session never numbers a cell before it merges, and a cell-adding session rebases on the latest cell-adding merge first |
| `kernel/kernel.asm` | S02, S03, S04, S05, S06, S10, S11, S12, S14, S16, S19, S20, S21, S23, S25 | Different regions (guards, cells, install, stubs), so a conflict is textual. The API table's end is the one serial point: see the row above |
| `kernel/wm.inc` | S02, S06, S07, S18 | S06 edits only the INT 0 stub (wm.inc:8268-8275 per WP8 (`phase1.md:782`)). S07 edits `wm_ptr2idx`, the doors, the damage pass and teardown. S02's dead stores land first (wave 1) |
| `kernel/sched.inc` | S02, S06, S16, S19, S21 | S06 (the panel, stubs, IFLAT, the shadow's sched half) merges before S16 is cut. S19 touches only the `%error` at sched.inc:2891-2893 (the class mask), and S21 only the skip count; both merge in wave 5's order around S16 |
| `kernel/mouse.inc` | S02, S06, S11, S12, S16, S17, S18 | S06 (IMR, ISR doors) before S12 and S16. S17 takes the cursor drawing routines only. S11 extracts the keyboard mouse, so it is cut after S16 merges. S18 touches only its VGASAVE arms in `.vga` |
| `kernel/memory.inc` | S08, S09, S13 | S08 owns it in wave 3. BR-106 (kern_mega's regrow arm) stays in S08. S13 does not edit memory.inc. S09 is cut after S08 merges |
| `kernel/disk.inc`, `diskw.inc`, `files.inc` | S02, S03 (`.ovl` `dsk_boot_from_x` only), S14, S15, S16 (histsys-5, optional) | S14 owns them in wave 3. S15 is cut after S14 merges. S16's histsys-5 goes last, behind its preconditions |
| `kernel/fsx.inc` | S02, S03 (BR-14), S06 (`fsx_rtick`'s `cld`), S13 (BR-184's real-mode door), S16, S17, S18 | Each session touches a named routine. S17 owns `fsx_page` after S03 |
| `kernel/ctrl.inc` (CTRL.DRV) | S02, S06 (CFG_MAP, gfce-6 if taken there), S12, S20 (shell-3, ui-11's Mouse page), S21, S24 | S21 and S20 share wave 5 and merge in that order. S24's engine rewrite is last (wave 6), on top of every earlier ctrl.inc change |
| `kernel/fdlg.inc` (FDLG.DRV on kern_small) | S02, S14, S10, S20, S24 | S14 owns it in wave 3. In wave 5, S20 (files-17's callback) merges before S10 (the kern_big module arm). S24 is last |
| `kernel/driver.inc` | S02, S06 (CFG_MAP), S12 | S12 owns it |
| `kernel/hiber.inc` (HIBER.DRV) | S02, S12, S13, S21 | S21 is cut after S13 merges (BR-69, BR-71, md-19) |
| `boot/boot2.asm` | S03, S10, S12 | S03 owns the blob in wave 2. S12's `%1_call` and S10's `mem_unblob` copy come after it |
| `apps/os88ui.inc` | S02, S07, S17, S24 | S07 (BR-176, BR-196) before S17's button order (os88ui:F1) |
| `apps/os88partsbody.inc` | S08 (BR-59), S14 (BR-191) | Same wave, so the routines are split: S08 takes the lazy part re-fetch, S14 the size read. Merge S08 first |
| `Makefile` | S03, S10, S11, S12 (if O10), S28 | S28 (the split) is last |
| `tests/suite.py` | every session that adds a row | Add rows in your own block. Conflicts are textual |
| `docs/INDEX.md` | generated | Re-run `python3 tools/os88index.py` after every merge. Never hand-merge it |

### 2.5 The two transient homes on kern_big: the cross-session ledger

Both homes are full today. Figures are MEASURED from the maps
(`bugs.md:1192`); the wants are ESTIMATED.

**The blob (`.boot2`): 0x19FC of 0x1A00, 4 bytes free.** kern_small has 702
free and kern_mega 285, so this is kern_big's problem alone.

| session | frees | wants | running free |
|---|---|---|---:|
| start | | | 4 |
| S02 | drivers-12 -4, hiber-17 -9 | | 17 |
| S03 | boot-7 -41 | BR-1 +19..+23, BR-6 **+60..+110**, BR-55 +8..+12, BR-56 +4, BR-63 -1, BR-155 +6 (optional) | **-32..-96**: does not fit |
| S12 | | drivers-5 +2..+4 | |
| S17 | | BR-147 +7 | |
| S20 | | shell-8 +10 | |

**BR-6 cannot land in the blob without a 14th blob sector (owner decision
O1).** That sector costs +512 bytes of guard 5's boot footprint and about
+22 ms of load on a 360KB disk (PREDICTED), and no resident byte. `phase1.md`'s
alternative, boot-17's `kmain_o` table (-30..-60, measure first), was sized
against BR-6's superseded +25..+35 and does not cover the corrected figure.
Without BR-6, `bugs.md`'s own blob items fit after boot-7 (`bugs.md:1192`).
`phase1.md`'s two (drivers-5 +2..+4, shell-8 +10) are not in that sum. With
them, the 58 bytes of room against every other want in this table run from
9 free down to 7 short (1 short without the optional BR-155). That is why O1
also blocks S17's BR-147 and S20's shell-8: the last two to land read the
tally first.

**The boot overlay's window half (`.ovl`): OVL_SIZE 5,119 of 5,120, 1 byte
free.** The `%error` is at kernel.asm:8939. kern_mega has 2 free. `phase1.md`
quotes 5,116; the map says 5,119.

| session | frees | wants | running free |
|---|---|---|---:|
| start | | | 1 |
| S02 | WP7 -45 (sched-12 -30, sysmisc-10 -15) | | 46 |
| S03 | dr-13 -2 | BR-25 +6 | 42 |
| S06 | | WP8 install +20..+30, BR-18 +6, BR-88 +5 | 1..11 |
| S14 | | BR-141 +10 | -9..+1 |
| S21 | **relief: move `clk_probe`'s RTC rungs out of `.ovl`** (the first candidate `phase1.md` names; SPEC.md 2.5.3.2 is the rule) | BR-44 +5 (or gfce-6's +22), BR-157 +8, BR-158 +16 | needs about 30..55 of relief |

Rules that follow from the ledger:

- S06 and S14 land their overlay bytes in that order.
- S21 lands its relief in its own first commit, before its own wants.
- If the owner takes O1, S03 may instead take BR-6's cheaper shape: move
  `vid_memchk` from `.ovl` to `.boot2`. That frees 55 bytes of `.ovl` and
  costs about +85 of blob, which the new sector covers. The S21 relief then
  shrinks or goes.
- Every session that touches either home quotes the `kernsize` tally line
  that S01 adds.

### 2.6 Owner decisions: blocked until decided

A blocked item does not stop its session. The session builds everything else
and leaves the item open in the checklist. A decision taken goes into
`DECISIONS.md` (5.3) and into this plan's checklist.

| id | the question | bytes | blocks |
|---|---|---|---|
| **O1** | A 14th blob sector on kern_big? | +512 boot footprint (guard 5), ~+22 ms on a 360KB disk PREDICTED, 0 resident | S03's BR-6; S17's BR-147 and S20's shell-8 (blob room) |
| **O2** | gfce-2: does the fault panel (a) halt as a bug report, (b) close the faulting package and keep the desk, as `mega_fkill` does, or (c) halt only for a kernel CS and IRET otherwise on an 8088? (a) alone halts on package divides that continue today on later 8088 ROMs, so theme 4 ships the stubs with (b) or (c) (`systemic.md:62`) | (b) is about +45..+65 more, plus a skip decoder on the 286+ | S06's panel (WP8 (`phase1.md:782`)) |
| **O3** | The "packages are trusted" ruling (docs/plans/LAST-DROP-BYTES.md:1135-1140) applied to six fixes: ship them, put them behind APICHK=1 only, or make them named exceptions? | BR-8 +36, BR-22 +12, BR-29 +13, BR-30 +18, BR-37 +16, BR-173 +12: **+107 / +107** big / small (`bugs.md:1192`) | parts of S07, S08, S14 |
| **O4** | WP2b: FILECP and FDLG on kern_big as modules cached at boot. Put with the three quotes in WP2 (`phase1.md:254`) (ONDEMAND-PLAN 1.2, KERN-SMALL-MODULE-SPLIT.md:13-15, SPEC.md 38.0), **not as settled** | **-2,468..-2,508** kern_big res (-2,568 movable MEASURED, +60..+100 glue); +4 clusters per kern_big system disk; a reload is 1-2 int 13h (0.4-0.8 s PREDICTED) | S10 |
| **O5** | rt-6: kern_small's own module caching | +60..+85 kern_small | S09's kern_small arm |
| **O6** | rt-7: the keyboard mouse as an init-lifetime module (KBMOUSE.DRV) | -290..-380 static res every kernel; KBMOUSE.DRV +400..+450, one cluster on every system disk (small360 included); a machine with NO mouse keeps the image claimed all session, **+85..+165 of RAM in use for ever** on exactly the mouseless 128KB 5150 (cost lens, WP3 (`phase1.md:419`)) | S11 |
| **O7** | text-7: `font_run_x`'s VGA prologue into `.vga` | +16 gauge; -165..-185 on a mono machine; ~30-40 clocks a run on VGA PREDICTED | S11 |
| **O8** | WP11: take the KHP round (LAST-DROP-PERF 7.1's measured knobs) on kern_big | +286..+289 gauge (+104..+107 every machine, +182 `.vga`) | S18 |
| **O9** | dw-14: rewrite SPEC 18.4's "no attempt to defragment while writing" as a dated decision, and allocate contiguous runs | +100..+160 `.cold` kern_big | S15's dw-14 |
| **O10** | gkm-6: retire kern_emu (VMMOUSE.DRV as a DRVC_POINT worker driver); prototype first | about +40 kern_big, -385 kern_emu-only, one kernel build retired | S12's gkm-6 |
| **O11** | hdk-2 (BR-197): may a package erase a partition without a tool-owned confirm? | ~+10 HDDTOOL for the token form | S22's BR-197 policy |
| **O12** | ctx-6 (BR-188): kern_small lets any task's claim shed | +14 `.text` kern_small | S08's BR-188 |
| **O13** | BR-60: subdirectories grow | +90 kern_big | S14's BR-60 |
| **O14** | BR-104: the policy reversal on OSAPI_MEM_FREE/_REGROW of the caller's own live region (if reversed, it is a flag-day row) | +5 (the no-break form) | S08's BR-104 |
| **O15** | BR-65: form (c) now; form (a) on the owner's word | (a) +128..+192 kern_mega `.lowbss`, +20 more high | S13's BR-65(a) |
| **O16** | D8: the appended `OSAPI_IRQ` cell now, or only at the flag day? | +100..+150 kern_big; SOUND.DRV -60..-100 | S12's cell |
| **O17** | Fork rule 3 in a multi-session run: does each child send the thirteen disks at every commit, or once at its last commit, with the orchestrator sending after each merge? | - | the cadence of every session (5.5). Until answered, the rule as written stands |
| **O18** | The smaller calls the report also lists: engsys-15's threshold for a scoped soak that counts as the whole tier (~90% proposed; S01's BR-172); rt-5, gate the mono residency figure or only print it (S12); hm-12a, `MEM_OWNER_MAX` 8 -> 10 on kern_big against SPEC.md 50.2's reason for 8 (S19); wm-19, retire `band.inc`'s `BAND=1` knob (S18); hiber-19, keep `HIBERNAT.IMG` allocated (S21, unchecked) | 0 each, except as the session prices it | the named session's item only |

Phase 2's own questions are in 4.4.

## 3. The handoffs

### 3.1 The common brief: every handoff includes this

A session told to "take S<n>" reads this section, then its handoff, then the
synth sections the handoff points at. It needs nothing else from the session
that wrote this plan.

**Start.**

1. `make deps` first: fork rule 0 in CLAUDE.md.
2. If `git rev-parse --is-shallow-repository` says `true`, run
   `git fetch --unshallow`. That is fork rule 1, and docs/UPSTREAM.md's
   Rule 0.
3. Run `git ls-remote --heads origin`.
4. Cut your branch `big-review/s<NN>-<slug>` from `origin/big-review/phase1`.
   - If `big-review/phase1` does not exist yet, the orchestrator has not
     started. Cut it yourself from `origin/elendilon` if `the-big-review` has
     merged there, otherwise from `origin/the-big-review`, and say so in your
     report.
5. Run `make` once on the untouched base, and keep its `KERN_BUDGET big <n>,
   small <n>` line and its `tools/kernsize.py` output (kern_big; `make small`
   for kern_small; `make mega` when your items reach kern_mega) as your
   baseline. Keep that baseline in a private tree (`make BUILD=<dir>`), never
   in `build/` beside the real build.
6. Read CLAUDE.md in full, `docs/INDEX.md` (is there already a way to do
   it?), and the synth sections your handoff cites.

**Order of work.**

- **SPEC first.** The sections your handoff names are rewritten before the
  code that makes them true (CLAUDE.md, the SPEC.md row of its map).
- Items in the order the handoff lists.
- One commit per item, or per tight group of items. Stage by name, never
  `git add -A`/`-u`. Never `git stash`. End every commit with the attribution
  lines from the session's system reminder.

**Budget rule (bytes, not rungs).**

- Quote every change as signed BYTES, by section and by kernel: the routine's
  span, or `kernsize`'s sum and its accrued line. Never as a rung count, and
  never off a padded artefact. "It crosses no rung" and "it uncrosses no
  rung" are both refused arguments (CLAUDE.md, the banner).
- `KERN_BUDGET` is derived from the two residency rules: kern_big 100KB at the
  desktop, kern_small 64KB. `MEGA_RESIDENT` (68,096 by `phase1.md`'s reading;
  re-read it) binds kern_mega conventional. Branch work may run up to
  `KERN_GRACE` (2,048) over, and `make budget-check` is where the grace ends:
  at an upstream merge or a release, on all four budgeted kernels. Run
  `make budget-check` at the end of your session and **report** its result;
  a session does not have to meet it exactly.
- `phase1.md`'s reading of the blessed sizes: kern_big 96,768 of 100,864,
  kern_small 59,392 of 64,000. Re-read both at your start.
- Your handoff's table is the estimate. Report the measured figure beside it.
  If a measured figure runs more than half again over the estimate, say why
  in the report.
- A session that grows a transient home on kern_big quotes the `kernsize`
  tally line (S01) and follows 2.5's ledger.
- Before spending a resident byte, ask whether the feature is an on-demand
  module (SPEC.md 2.8).

**Testing (CLAUDE.md, *Testing*; the owner's rule).**

- **Host checks only during the session:** `make` (which runs the fast tier),
  `make KERN_SMALL=1`, `tools/kernsize.py` on both kernels, `make mega` when
  kern_mega is reached, pure host checks and `python3 tools/checkdocs.py`.
  One boot to **look** is allowed: screenshots cropped and zoomed, and a 1bpp
  adapter for any drawing or greying change (SPEC.md 39.4).
- **Name the rows; do not run them.** No soak row, no `test-full`, and no hand
  run of `python3 tests/<row>.py`, which is a soak run under another name. The
  rows your handoff lists go into your report, deduplicated, with any you
  added.
- **The one exception is a NEW row you write.** Writing it per
  docs/WRITING-TESTS.md means running it: once red with the defect present (or
  restored on purpose), once green. Its `secs` comes from that run. Write each
  run down in the report. Running a row someone else wrote is not part of
  this.
- A number that DECIDES a design is taken once by the session deciding, and
  written in the report. Nobody re-takes it. The handoffs mark these "measure
  first".
- The deduplicated soak union and `test-full` run **once, after the last
  phase-1 session** (5.5). The whole soak tier runs only when the owner asks
  for it in as many words.

**ABI lock.**

- An `apps/os88api.inc` or `drivers/os88drv.inc` change that only clarifies a
  contract or appends a cell runs `python3 tools/abilock.py --update "why"`
  **without** a bump.
- No phase-1 session bumps `PKG_FMT` or `DRV_VER`. If an item seems to need a
  bump, it belongs to phase 2: stop and write it in the report.
- Keep the lock to one update per session where you can: batch your text
  changes.
- A cell-adding session needs WP9's level first (S05). New cells are guarded
  by `OS88_NEED` in the packages that call them.

**Fork rules that bind every session** (CLAUDE.md, *Working in this fork*):

- rule 0: `make deps` first;
- rule 1: unshallow;
- rule 2: "elendilon" is the branch;
- rule 3: send the disks. See O17 for the cadence;
- rule 5: do not boot an image after building it to send it;
- rule 8: "Try again" means the session was cut off, so pick up where the work
  stood.

**Report.** The report is the body of your **last commit message**, under a
line `Report:`. Repeat it as your final message. It gives:

- the items landed, each with its measured bytes beside the estimate;
- the items left open, and why;
- the owner decisions you hit;
- the rows named;
- the new rows, and their red/green runs;
- `make budget-check`'s result;
- the checklist lines (section 6) the orchestrator should tick.

Do not add a report file to the repository: it would make every merge
regenerate `docs/INDEX.md`. Push only your own branch. Do not merge, and do
not open a PR. The orchestrator integrates.

**Definition of done, for every session:**

- every item landed or explicitly left open with a reason;
- SPEC updated first;
- `make`, `make KERN_SMALL=1` and checkdocs green;
- bytes measured and reported;
- rows named;
- new rows proven red-then-green;
- abilock updated when the SDK moved;
- the branch pushed with its report.

### 3.2 S01 - Gates that catch wrong facts (WP10, batch A's host gates)

- **Branch:** `big-review/s01-gates`. **Wave 1**, in parallel with S02 and
  S04. Merges first.
- **Goal:** host-only checks that fail `make` on errors the tree already
  makes. Every later session leans on them: the four-kernel ABI decoder, the
  section check, `t_mirror` over kerndos (WP10 (`phase1.md:962`)). Theme 12
  (theme 12 (`systemic.md:1633`)): one source per fact, the rest generated or gated.
- **Items:**
  - **WP10, all of it.** abi-3 / gkm-2: `t_api_abi` over all four kernels,
    with a per-kernel mode in full (BR-62's gate half). abi-4 (BR-146,
    `API_RN_DSOFF`). engsys-2, -8, -12, -15 (BR-75, BR-76, BR-171, BR-172).
    gfx-7 (BR-152, now an assembly-time check, 0 `.ovl`). md-12 (BR-170).
    shell-6 (BR-117). ui-4 (BR-111). md-5 (`t_mirror` over kerndos).
    engsys-11 (`os88.h` and `os88thunk.asm` into `PKG_FMT`'s fingerprint and
    `t_mirror`). drivers-16 (the VX/XM/HD versions fingerprinted).
  - **Batch A's host gates:** BR-74 (`bugs.md:878`), BR-75 (`bugs.md:888`), BR-76 (`bugs.md:898`), BR-171 (`bugs.md:1162`),
    BR-172 (`bugs.md:1163`), BR-177 (`bugs.md:948`).
    - **BR-74 matters to this plan in particular:** checkdocs must not fail
      the build on the committed review reports this plan cites.
    - BR-177 (`os88ovlchk` cannot see the KBTNIMG/KGLYPH split) is the start
      of WP28's "a scanner that sees macros". S24 builds on it.
  - **Moved in from WP31:** a `tools/kernsize.py` tally line for kern_big's
    blob and `.ovl` (free bytes, from the map), so 2.5's ledger is printed
    rather than kept by hand (`phase1.md:139`).
- **Files:**
  - tools: `checkdocs.py`, `abilock.py`, `os88test.py`, `os88soak.py`,
    `os88ovlchk.py`, `kernsize.py`;
  - tests/unit: `t_mirror.py`, `t_api_abi.py`, `t_registry.py`,
    `t_kernbudget.py`, a new `t_sections.py`;
  - `tests/suite.py`, CLAUDE.md and AGENTS.md (both, per their own rule);
  - the `%if`-level touches WP10 names: `kernel/ui.inc`, `desk.inc`,
    `desksc.inc`, `fprog.inc`, `viddet.inc`, `megados.inc`;
    `kerndos/kdpark.inc`; `apps/os88pit.inc`.
- **Neighbours:**
  - S02 edits `ui.inc` (ui-3) and `viddet.inc` (text-2). Your touches there
    are guards and `%if`s only.
  - S04 owns `os88index.py`, `stkdepth.py` and `t_apictx.py`.
- **SPEC first:** none of substance. SPEC.md 20.2.0.1's list of fingerprinted
  files gains the C SDK and the private versions.
- **Bytes:** 0 shipped on every kernel. Prove it with `cmp` over every image
  at one commit.
- **ABI / lock:** engsys-11 and drivers-16 re-record the lock's inputs (no
  bump). After this, WP34's XM_ABI_VER 3 and WP33's HD_ABI_VER 8 are checked.
- **Rows to name:** the gates are rows: `checkdocs`, `docindex`, `mirror`,
  `api-abi`, `abilock`, `registry`, `asmrules`, `kernbudget`, `canary` (fast);
  the new `sections` (fast); api-abi's per-kernel mode (full).
- **Done when:** each new or changed gate is shown red on a deliberately
  broken input, then green (docs/WRITING-TESTS.md section 1), and every
  shipped image is byte-identical to the base at one commit.

### 3.3 S02 - Free bytes and the `.bss` rule (WP1, WP7)

- **Branch:** `big-review/s02-freebytes`. **Wave 1**, in parallel with S01
  and S04. Merges second.
- **Goal:** take every saving that needs no design (WP1 (`phase1.md:198`)), and settle
  whether `.bss` arrives zeroed: one rule, a gate, the dead stores
  (WP7 (`phase1.md:746`)). This is also the `.ovl` room (-45) that S03, S06, S14 and S21
  spend (2.5).
- **Items:**
  - **WP1:** text-2, ui-3 (BR-110 (`bugs.md:1101`), which is also its hazard fix),
    wm-10, text-8, sched-14, gfx-3, files-11, dw-9, mc-11, video-7 (the
    truth lens's fsx_bmodes amendment), drivers-12 (blob -4), cursor:mc-1,
    cursor:mc-4, cursor:mc-7, os88ui:F3, F4, F6 (hoist the clear above
    `call cpf_vidok` at ctrl.inc:4302), and the dead `%else` arms (drivers-11,
    wm-14, text-13).
  - **WP7:** the `.bss` rule; the gate (`t_bsssentinel`, promoted); the dead
    stores; sched-12 (-30 `.ovl`), sysmisc-10 (-15 `.ovl`), hiber-17 (-9
    blob).
  - loader-8 is NOT here: it is S08's (BR-11).
- **Files:**
  - kernel/: `font.inc`, `ui.inc`, `wm.inc`, `viddet.inc`, `menu.inc`,
    `sched.inc`, `vga12.inc`, `files.inc`, `diskw.inc`, `mega.inc`,
    `fsx.inc`, `driver.inc`, `mouse.inc`, `ctrl.inc`, `fdlg.inc`,
    `hiber.inc`, `shutdown.inc`, `kernel.asm`, `clock.inc`, `clockw.inc`,
    `events.inc`, `cpudet.inc`, `bootprof.inc`, `mod.inc`, `splash.inc`;
  - `apps/os88ui.inc`;
  - `tests/unit/t_bsssentinel.py`.
- **Neighbours:** this is wide and shallow, and it is the reason it runs in
  wave 1, before every code session. Keep each edit to the named routine.
  S01's touches in `ui.inc` and `viddet.inc` are guards.
- **SPEC first:** SPEC.md 2.5, 2.6 and 8 (the `.bss` rule).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP1 | -165..-188 | -144..-146 | -87..-101 conv, -49 high |
  | WP7 | 0..-20 | 0..-20 | 0..-20 conv |
  | transient | - | `.ovl` -45, `.boot2` -13 (-4 drivers-12, -9 hiber-17) | measure: WP7's figures are kern_big's |
  | modules | CTRL.DRV and FDLG.DRV -24 each (os88ui:F3) | CTRL.DRV and HIBER.DRV about -7 | |

- **Risk:**
  - text-2, text-13 and mc-1 touch hot-path parity padding: re-balance
    `t_hotpar`.
  - ui-3: check the Locator with no app menu, and with a package menu.
  - F6: the CF site.
- **Rows to name:**
  - fast: `hotpar`, `kernbudget`, and `bsssentinel` (a soak row today;
    WP7 promotes it to fast);
  - full: `kernresident`, `small128`, `bootsmoke`, `megahigh`;
  - soak: `disp*`, `fmarrows`, `navcalls`, `vidfskeys*`, `xorrect`,
    `dispblit`, `btnall`, `btncp`, `curshape`, `curbusy`.
- **Done when:**
  - WP1 and WP7 have landed;
  - `kernsize` shows the savings on all three kernels;
  - the tally shows `.ovl` at 46 free and the blob at 17 free on kern_big;
  - `t_hotpar` is green.

### 3.4 S03 - Boot: derived constants, the room, and the boot defects (WP6, batch A core, BR-14)

- **Branch:** `big-review/s03-boot`. **Wave 2**, in parallel with S05.
  Cut after S01 and S02 have merged: it needs S02's blob and `.ovl` room and
  S01's tally.
- **Goal:** make three hand-typed boot constants derived, free blob and
  boothd bytes (WP6 (`phase1.md:674`)), then fix the boot and detection defects in that room
  (batch A (`bugs.md:1279`)). Theme 11's divergence and theme 4's "one fatal path" begin
  here (theme 11 (`systemic.md:1533`)).
- **Items, in this order:**
  1. WP6's boot-4 audit first (BR-153 (`bugs.md:1144`): the SPL_RESIDENT guard).
  2. boot-18 (BR-154 (`bugs.md:1145`): guard 5 and BLOB_LIFT).
  3. boot-7 (blob -41).
  4. boot-6 (boothd -9, BR-25's boothd half).
  5. boot-17 (optional; measure first).
  6. Then batch A core: BR-1 (`bugs.md:148`), BR-6 (`bugs.md:198`) (**blocked on O1**), BR-14 (`bugs.md:278`)
     (batch B's, landing with BR-6 so that `[vid_avail]`'s CGA bit means one
     thing on both kernels), BR-63 (`bugs.md:768`), BR-25 (`bugs.md:388`), BR-55 (`bugs.md:688`), BR-56 (`bugs.md:698`).
  7. Low: BR-155 (`bugs.md:1146`) (measure first), BR-156 (`bugs.md:1147`) (doc).

  Notes on the core items:
  - **BR-1 is the review's rank-1 defect.** The boot canary protects one of the
    six shipped system disks. The fix moves KSIG_OFF per arm. `t_canary`'s
    `crosses()` must model the KZ head split. The fix costs +1 int 13h per
    8088 boot on four of the six system disks (PREDICTED).
  - **BR-6.** The MDA primary is driven with Hercules CRTC timings. Write 3BFh
    = 1 first, then discriminate with Wilton's 3BAh toggle or the 0x1000
    alias test. An MDA alone halts with a message.
- **Files:**
  - `boot/boot2.asm`, `boothd.asm`, `boot.asm`;
  - kernel/: `splash.inc`, `viddet.inc` (`vid_detect`), `vidsel.inc`
    (`vid_memchk` if it moves), `disk.inc` (`dsk_boot_from_x`, `.ovl` only),
    `fsx.inc` (BR-14's kern_small `fsx_page`), `kernel.asm` (guard 5; the SPL
    guard at 8650-8653);
  - Makefile (SPL_RESIDENT, KSIGDEF2, KSIG_OFF per arm, KZ_HEAD);
  - `tests/unit/t_canary.py`, a new `tools/os88ksig.py`, `docs/history/`.
- **Neighbours:** S05 edits `kernel.asm` at KERNEL_SEG:0006 and the loader.
  You touch only the guards and the boot path.
- **SPEC first:** SPEC.md 39.1's MDA cut, and SPEC.md 39.11.1's closing
  sentence (BR-6); SPEC.md 18.93.1 (the canary).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | resident | +13 (BR-14) | +2 (BR-14) | 0 |
  | `.boot2`, WP6 | -41 | -41 | -41 |
  | `.boot2`, batch A core | +68 (+44..+92) | +119 (+90..+148) | +119 |
  | `.boot2`, net | +27, fits in 702 | **+78 against 17 free: needs O1** | +78, fits in 285 |
  | `.ovl` | 0 | +4 (BR-25 +6, dr-13 -2), fits in 46 | as kern_big |
  | boothd | - | -1 (+8, then -9) | - |
  | KERNEL.SYS | -383 | -403 | -383 |
  | boot time | +1 int 13h per 8088 boot | +1 int 13h per 8088 boot on the 1.2MB and 1.44MB disks; ~+22 ms with a 14th sector (PREDICTED) | - |

- **Owner:** **O1** blocks BR-6. Land everything else, and leave BR-6 open
  with its two shapes priced. If O1 is yes, prefer the shape that moves
  `vid_memchk` into `.boot2`: `.ovl` -55 (2.5).
- **Rows to name:**
  - fast: `canary`, `api-abi`, `image`, `diskverify`;
  - soak: `blobruns`, `blobsum`, `splashbar`, `splashspin`, `smallboot`,
    `kzboot`, `hdboot`, `hddgeom`, `bootfloor`, `bootfloor-ab`,
    `hdtakeboot`, `pravetzboot`, `v20boot`;
  - full: `bootsmoke`, `small128`;
  - 86Box: the `box-` twins of `hdboot` for FN33's images;
  - the `make` gate itself for BR-177;
  - new: an MDA-primary boot, an MCGA profile and a 64KB EGA profile, if
    MartyPC or 86Box carries one. None is in the suite today; say which
    exist.
- **Done when:**
  - `t_canary` passes on all six system disks, with `crosses()` modelling the
    head split;
  - KSIG_OFF, SPL_RESIDENT and KZ_HEAD are derived;
  - BR-6 has landed or is open on O1.

### 3.5 S04 - Routine contracts, context tags and the checked build (WP14, WP19)

- **Branch:** `big-review/s04-contracts`. **Wave 1**, in parallel with S01
  and S02. Merges third.
- **Goal:** make register discipline checkable: a header grammar and a
  liveness lint (WP14 (`phase1.md:1264`)). Turn "UI task only", "lock held", "worker safe"
  and "ISR safe" into tags a tool reads, plus an `APICHK=1` checked build
  (WP19 (`phase1.md:1574`)). This is theme 1's model (theme 1 (`systemic.md:134`)). The fences that refuse a
  violation are the batches': BR-22 and BR-37 (S14), BR-30 and BR-107 (S08),
  BR-28 and BR-73 (S07), BR-86 (S06). This session is the model they fence.
- **Items:**
  - WP14: `tools/os88contract.py` (the grammar), `tools/os88live.py` (the
    lint), the generated reference in `docs/`, and the header fixes the lint
    reports.
  - WP19: abi-8 / ctx-7 `; @ctx` tags on every contract in
    `apps/os88api.inc`; `tools/stkdepth.py`; a new `tests/unit/t_apictx.py`;
    `tools/os88index.py`.
  - abi-15 / engsys-20 / gfce-9: the `APICHK=1` knob (cell macros in
    `kernel/kernel.asm`).
  - The tags also answer phase 2's IF question (4.4).
- **Files:** as above.
- **Neighbours:** S05 is cut after you merge. It needs your tags, so that
  WP26's contract sentences are written once, beside them.
- **SPEC first:** SPEC.md 20.3 and 20.6.
- **Bytes:** 0 shipped. The `APICHK` knob is bounded by guard 2 only, as any
  knob is. WP14's later size pass saves about -93..-127 on kern_big, one
  time; that pass is not in this plan.
- **ABI / lock:** one `abilock --update`, no bump, for the whole tagging:
  contract lines are fingerprinted.
- **Rows to name:**
  - fast: `stkbalance`, `stkwalker`, `api-abi`, `abilock`; the new `apictx`
    (fast once clean);
  - soak: a new report-only `contracts` row (promoted to a fast ratchet once
    clean), a new `apichk` row, `buildmatrix`.
- **Done when:**
  - every contract carries a tag;
  - the lint and `t_apictx` run in report mode;
  - `make APICHK=1 BUILD=<dir>` builds.

### 3.6 S05 - The API level and the contract text (WP9 as A4, WP26, R9's doc half)

- **Branch:** `big-review/s05-apilevel`. **Wave 2**, in parallel with S03.
  Cut after S04 has merged.
- **Goal:** close the gap SPEC.md 20.2.0.1 names under "What it does not
  cover". A package built against newer cells far-calls past an older table's
  end (WP9 (`phase1.md:903`)). Then make the SDK and SPEC say what the code does, in as few
  lock updates as possible (WP26 (`phase1.md:1885`)). Theme 9 (theme 9 (`systemic.md:1336`)) is the cheapest item
  in the review, and the precondition for every appended cell.
- **Items:**
  - **WP9, in the form flag-day row A4 requires**
    (`abilist-section.md:50`, `abilist-section.md` row A4):
    - `dw OS88_API_LEVEL` at KERNEL_SEG:0006 (never a literal 0060, which
      flag-day row L1 moves);
    - `os88pkg.py` writes the needed level into header byte 31, and names are
      capped at 14 when it is set;
    - `ld_check_hdr` refuses with `LD_EVER`;
    - `OS88_NEED level`;
    - one level table shared with The Wire's `WIRE_CELL_LEVELS`;
    - `t_abilock` fails an appended cell that does not raise the level.
  - **WP26's contract sentences** (WP26 (`phase1.md:1885`)), plus the text halves of BR-22
    (ui-8), BR-77 (`bugs.md:1068`), BR-78 (`bugs.md:1069`), BR-85 (isr-3: OSAPI_WM_WAKE from an ISR
    requires SS = LOW_SEG), BR-104 (`bugs.md:1095`), BR-115 (`bugs.md:1106`) (ui-9's "unless the ring
    overflowed" weakens a promise: say so in the lock reason), BR-122 (`bugs.md:1113`),
    BR-145 (`bugs.md:1136`) (abi-5; its `os88drv.inc` half is S12's), cursor:mc-5's GFX_LOCK
    wording, xm-12's context rule, hdk-2's os88api.inc:3490, and gfce-8's
    class sentence.
  - **histsys-17** (-4..-8 code, `vga12.inc`).
  - **R9's doc half** (flag-day 3.1): CF=1 is the only universal refusal
    sign; AX means something only inside a named family; SPEC 18.4 gets a
    class/action table.
- **Watch-list traps (`phase1.md:171`):**
  - Byte 31 is the NUL of a legal 15-character name. Relax
    `os88pkg.py:122-125`, `os88disk.py:289-293`, `os88pkgsize.py:41`,
    `os88rate.py:150`, `os88face.py` and `os88vbt.py`.
  - An OLD kernel's Sticky caption (desksc.inc:770-781) measures such a name
    as 16 and shows its stem. That is harmless, but it is a visible
    difference: write it in SPEC.
- **Files:**
  - kernel/: `kernel.asm` (KERNEL_SEG:0006), `loader.inc` (`ld_check_hdr`),
    `instance.inc`, `vga12.inc`;
  - `apps/os88api.inc`;
  - tools/: `os88pkg.py`, `os88wire.py`, `os88disk.py`, `os88pkgsize.py`,
    `os88rate.py`, `os88face.py`, `os88vbt.py`;
  - `tests/unit/t_pkg.py` and the `t_abilock` addition.
- **Neighbours:** S03 is in `kernel.asm`'s guards and the boot path. You are
  at 0006 and in the loader.
- **SPEC first:**
  - SPEC.md 20.2 and 20.2.0.1 (the level);
  - the sentences WP26 lists, in SPEC.md 7, 8, 13.9, 18.4, 20.3, 41.8, 53.2.1,
    55.4 and 66, and in SPEC 21.5 and 72.15.1.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP9 | +8..+9 `.cold` | +8..+9 `.cold` | +8..+9 high, 0 conv |
  | histsys-17 | -4..-8 | -4..-8 | -4..-8 conv |
  | packages | about +9..+10 per opting package | | |

- **ABI / lock:** two updates at most, both without a bump: the level, then
  the whole text pass. One combined update is better.
- **Rows to name:**
  - fast: `api-abi`, `abilock`, `pkg`, `installabi`, `checkdocs`, `stik`;
  - soak: `abicompat`, `thewire`, `wire`, `mouseup`;
  - new host cases in `t_pkg`: a too-new package is refused with `LD_EVER`;
    a 14-character name with byte 31 set round-trips through `os88disk.py`.
- **Done when:**
  - the level is readable at 0006 on all four kernels;
  - a too-new package is refused at load;
  - every WP26 sentence has landed with one lock update;
  - every cell-adding session can now start.

### 3.7 S06 - Faults, ISRs and input (WP8 panel, WP13, WP22's shadow, batch F)

- **Branch:** `big-review/s06-faults`. **Wave 3**, in parallel with S07, S08,
  S13 and S14. Third in the merge order.
- **Goal:** one fatal path. Today one divide overflow is a dead machine on an
  IBM 5150/5160 ROM, the DIV re-runs for ever on a 286+, and GLaBIOS continues
  with garbage (WP8 (`phase1.md:782`), BR-9 (`bugs.md:228`)). Also: a written IF=0 budget with a ratchet
  and an instrument (WP13 (`phase1.md:1202`), BR-178 (`bugs.md:958`)); one kernel-side IMR shadow
  (WP22 (`phase1.md:1738`)); and the input and ISR defects (batch F (`bugs.md:1355`)). Themes 4 and 2
  (theme 4 (`systemic.md:560`), theme 2 (`systemic.md:301`)). **Implement with Opus from the start:** these are fault
  paths and re-entry (the story-fanout skill's rule).
- **Items:**
  - **WP8's panel half:**
    - gfce-1: the generalised panel and the INT 0 / INT 6 stubs, installed in
      `sched_init`. The panel must not trust the scheduler tables
      (BR-84 (`bugs.md:1075`)).
    - gfce-3: the divide ratchet, `tests/unit/t_divsites.py` plus
      `tests/divsites.txt`. Register every div/idiv with a reason; "trusted by
      ruling" is one reason.
    - DOS.O88's vector exchange must round-trip.
    - **O2 decides the panel's behaviour before it ships.**
  - **WP13:** the IF=0 rule in SPEC, `tests/unit/t_ifwin.py` plus
    `tests/ifwin.txt`, the IFLAT knob in `sched.inc`, and the `ifoff` row.
    BR-12 (`bugs.md:258`) is its first catch, and S08 fixes it. S08 merges
    before S06 (2.3), so register BR-12's window in `ifwin.txt` only to watch
    the gate go red on your base, then drop the entry when you rebase onto
    S08's merge.
  - **WP22's shadow:** the IMR shadow (re-synced from port 21h after each DOS
    bracket). Then BR-34 (`bugs.md:478`)'s four IMR read-modify-writes through it. The
    vector scan and SOUND.DRV's sites are S12's. input-10 (PS/2 sync) is
    S11's.
  - **Batch F core:** BR-18 (`bugs.md:318`), BR-19 (`bugs.md:328`), BR-20 (`bugs.md:338`), BR-36 (`bugs.md:498`), BR-51 (`bugs.md:648`),
    BR-85 (`bugs.md:1076`) (its code: the ES load and the SS-independent `evq_push`),
    BR-86 (`bugs.md:1077`) (the `cld`s), BR-178 (`bugs.md:958`).
    - BR-21 (`bugs.md:348`) is measure first: serlease at divisor 12 with the mouse
      moving, taken once by whoever decides.
  - **Batch F low:** BR-79 (`bugs.md:1070`), BR-80 (`bugs.md:1071`) (the code; WP18's lock text rides
    S16), BR-81 (`bugs.md:1072`), BR-82 (`bugs.md:1073`), BR-83 (`bugs.md:1074`), BR-87 (`bugs.md:1078`), BR-88 (`bugs.md:1079`), BR-89 (`bugs.md:1080`),
    BR-90 (`bugs.md:1081`), BR-91 (`bugs.md:1082`), BR-92 (`bugs.md:1083`), BR-93 (`bugs.md:1084`), BR-94 (`bugs.md:1085`), BR-175 (`bugs.md:928`)
    (optional: +122 `.vga`), BR-192 (`bugs.md:1168`) (text), BR-194 (`bugs.md:1170`).
    - BR-95 (`bugs.md:1086`) is optional (+25): measure how often it happens first.
  - Moved out: BR-78 to S05 (text), BR-195 to S07.
- **Files:**
  - kernel/: `sched.inc` (panel, stubs, IFLAT), `mouse.inc`, `events.inc`,
    `serial.inc` (SERIAL.DRV), `clip.inc` (one `cld`), `fsx.inc`
    (`fsx_rtick`'s `cld` only), `kbddiag.inc`, `stkdiag.inc`, `kernel.asm`
    (the install), `wm.inc` (**the INT 0 stub at 8268-8275 only**),
    `instance.inc` (`inst_wchk`'s comment), `deskmod.inc`, `driver.inc`
    (CFG_MAP), `ctrl.inc`, `compress.inc` (divide sites);
  - `apps/os88pit.inc`;
  - tests: `t_divsites.py`, `divsites.txt`, `t_ifwin.py`, `ifwin.txt`;
  - docs/TESTING.md: `int0sweep` changes meaning, from "detector" to "the
    panel appears";
  - PERFORMANCE.md (the IF=0 rule).
- **Neighbours:**
  - S07 owns the rest of `wm.inc`: the doors, `wm_ptr2idx` (BR-8), the damage
    pass and teardown.
  - S08 owns `compress.inc`'s `cmz_wclaim` and `cmz_jsayx`. You touch only
    divide sites.
  - S14 owns `kernel.asm`'s kern_small refusal stubs. You touch the install
    in `sched_init`.
- **SPEC first:** SPEC.md 1, 7 and 8; SPEC.md 66.3 and the other IF=0 bounds
  WP13 names; SPEC 9 and 10 for the shadow.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP8 panel and stubs | +40..+55 | +45..+60 | 0 (the IDT handles it) |
  | batch F core without BR-9 | +40 | +59 | ~+60 conv |
  | WP22 shadow | +25 | +25 | +39 conv |
  | batch F low (if all taken; includes BR-34's IMR +8) | +49 | +61 | ~+61 |
  | transient | `.boot2` +31..+41 (install +20..+30, BR-18 +6, BR-88 +5), fits in 702 | **`.ovl` +31..+41 against 46** (the same three) | `.ovl` as kern_big, against 2 free plus whatever S02's WP7 frees there |
  | modules | | SERIAL.DRV +20 (BR-87); DESK.DRV +5; CTRL.DRV +22 if gfce-6 lands here | |

- **Owner:** **O2** (the panel), **O3** does not apply here: BR-9 reports
  every fault, the kernel's own included (`bugs.md:1192`).
- **Rows to name:**
  - fast: `mirror`, the new `divsites`, the new `ifwin`;
  - soak: `serlease`, `dosmouse`, `doswmouse`, `kdmouse`, `usbmouse`,
    `kbd4f`, `kbdpreempt`, `kbdburst`, `int0sweep`, `mega-int0sweep`,
    `stkpanel`, `stk0water`, `timerrace`, `timerbounce`, `evqfull`,
    `wirefleetlink`, `wirefleetnomouse`, `ringmove-wirefleet`, `clipkeep`,
    `clipgrow`, `trkspkrate`, `vidspk22`, `vidspkfast`, `dispclose`,
    `dispclose-small`, `zonedmg`, `dmgcull`, `kdos`, `cfgtrip`, `uilat`,
    `schacct`, `irqgrab`, `dosirq`, the new `ifoff`;
  - full: `ps2mouse`;
  - 86Box AT-BIOS twins for BR-19.
  - New: a `tests/` package that divides by zero in a callback gets the
    panel on a 5150 `_gla` profile, and as a `box-<row>` on 86Box's 286.
    Break the install and watch it time out. Also `iflat` and
    `ifoff --mouse`.
- **Done when:**
  - INT 0 and INT 6 reach the panel on kern_big and kern_small;
  - every div is registered;
  - the IF=0 rule is in SPEC with its ratchet;
  - the shadow is in place;
  - batch F's core has landed.

### 3.8 S07 - Window manager, UI and instance teardown (batch C, WP21, WP8's door half)

- **Branch:** `big-review/s07-teardown`. **Wave 3.** First in the merge
  order.
- **Goal:** teardown runs on the UI task, a dying owner releases its lock,
  and window pointers from packages are refused at the door. Theme 1's one
  move (theme 1 (`systemic.md:134`)) removes the root of five findings. Batch C (batch C (`bugs.md:1310`));
  the damage pass's calling convention (WP21 (`phase1.md:1702`)). **Implement with Opus from
  the start** (re-entry, "the wrong package closed").
- **Items:**
  - **Core:**
    - BR-4 (`bugs.md:178`) with BR-173 (`bugs.md:908`). There are two working routes, -6..+50. The
      posted-close form as first written does not work: `app_close_win`
      re-hides a dying record.
    - BR-8 (`bugs.md:218`): the bound before the div, plus WP8's door refusals and the
      wake fence, +29..+46. A bound alone trades a dead machine for a write
      into another package's window.
    - BR-28 (`bugs.md:418`), BR-47 (`bugs.md:608`) (with WP21's wm-17 in the same commit), BR-48 (`bugs.md:618`),
      BR-49 (`bugs.md:628`), BR-50 (`bugs.md:638`), BR-42 (`bugs.md:558`), BR-176 (`bugs.md:938`).
  - **Low:** BR-73 (`bugs.md:868`) (the resize mode and its lint), BR-108 (`bugs.md:1099`),
    BR-109 (`bugs.md:1100`), BR-112 (`bugs.md:1103`), BR-113 (`bugs.md:1104`), BR-114 (`bugs.md:1105`), BR-115 (`bugs.md:1106`) (code),
    BR-116 (`bugs.md:1107`), BR-118 (`bugs.md:1109`), BR-119 (`bugs.md:1110`), BR-195 (`bugs.md:1171`) (moved from batch F),
    BR-196 (`bugs.md:1172`).
  - **WP21:** wm-17, the damage-pass tidy, and gfx-15's region sort (S17's
    WP20 needs it). Measure before prototyping wm-12/R1. PERFORMANCE.md Part
    5's widget-drop row is the bar.
  - Not here: BR-110 is S02's; BR-111 and BR-117 are S01's.
- **Files:**
  - kernel/: `wm.inc`, `ui.inc`, `instance.inc` (`inst_task_die`,
    `app_close_win`'s tail), `toast.inc`, `deskmod.inc` and DESK.DRV,
    `desk.inc`/`desksc.inc` (one equ);
  - `apps/os88ui.inc` (BR-176, BR-196);
  - `apps/os88api.inc` (the door texts; wm-13 reverses "BX is trusted, as
    OSAPI_WM_WAKE", os88api.inc:1733).
- **Neighbours:**
  - S06 edits only `wm.inc`'s INT 0 stub, and only a comment in
    `instance.inc`.
  - S08 stays in `loader.inc` and `memory.inc`. `instance.inc`'s teardown is
    yours.
- **SPEC first:** SPEC.md 11, 20.6 and 11.103.10 (teardown on the UI task);
  SPEC.md 20.8 (the doors).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | batch C core | +92 (+55..+128), +1 `.bss` | +108 (+69..+146), +1 `.bss` | ~+98 conv, +1 `.bss`; high +10 (BR-176) -24 (BR-173) |
  | WP21 | -10 | -10 | -10 conv |
  | batch C low (if all taken) | +54 | +77 | ~+64 |
  | under O3 | BR-8 +36 and BR-173 +12 of the core | the same | |
  | modules | CTRL.DRV and FDLG.DRV os88ui copies +8..+12 each | DESK.DRV +11; every os88ui copy +8..+12 | |

  wm-2 for BR-8's bound is not taken: it needs +10..+15 of blob or `.ovl`,
  and neither has room.
- **Owner:** **O3** for BR-8's door refusals and BR-173. If O3 applies the
  ruling, take BR-8's bound only with its doors. Otherwise leave the div to
  S06's panel (`bugs.md:1192`).
- **Rows to name:**
  - fast: `stkclass`, `stkapps`, `stik`, `stikcard`, `toast`, `hotpar`,
    `chromesites`;
  - soak: `stk0water`, `stkpanel`, `timerrace`, `deskbench`, `dmgcull`,
    `zonedmg`, `paintnogrow`, `paintnogrow-vga`, `dockpos`, `dockmark`,
    `stikmenu`, `stikboot`, `stikswap`, `mouseup`, `evqfull`, `desksc`,
    `deskitem`, `toastbar`, `dispfsx`, `btnall`, `dispclose`, `dispzoom`,
    `dispband`, `sizesnap`, `deskzoom`, `zoomsave`, `dispapps`, and the
    `mega-` twins;
  - new: `stkdie` (BR-4); a clickrace row on `timerrace`'s model (BR-28); a
    TASK_ALIVE-under-the-lock row (BR-173); a wild-pointer WM call under
    `int0sweep`, refused (BR-8).
- **Done when:**
  - no window teardown or DESK.DRV drop runs on a worker's slice;
  - a dying lock holder releases;
  - the doors refuse a bad BX (or the bound alone, per O3).

### 3.9 S08 - Loader and heap (batch D, WP17, C1's internal callers)

- **Branch:** `big-review/s08-heap`. **Wave 3.** Second in the merge order.
- **Goal:** the loader and the heap defects, among them kern_small's rank-2
  shed-into-the-FAT-window and the IF=0 regrow copy (batch D (`bugs.md:1325`)). Then place
  growth by SPEC.md 50.2's two-ends rule and prototype the sorted table
  (WP17 (`phase1.md:1456`)). Also C1's correctness half, which flag-day row C1 waits on
  (`flagday.md:546`). Theme 6 (theme 6 (`systemic.md:825`)). **Implement with Opus from the start** (the
  heap is everyone's).
- **Items:**
  - **Core:** BR-2 (`bugs.md:158`), BR-7 (`bugs.md:208`) (+32..+36 `.cold`), BR-11 (`bugs.md:248`) (with
    loader-8, counted here), BR-12 (`bugs.md:258`) (S06 merges after you and drops
    its `tests/ifwin.txt` entry; remove it here only if S06 merged first), BR-29 (`bugs.md:428`), BR-30 (`bugs.md:438`).
  - **Then:** BR-35 (`bugs.md:488`), BR-52 (`bugs.md:658`), BR-53 (`bugs.md:668`) (**S09's prerequisite**),
    BR-54 (`bugs.md:678`) (measure first), BR-59 (`bugs.md:728`).
  - **Low:** BR-104 (`bugs.md:1095`) (**O14**), BR-105 (`bugs.md:1096`) (the DMA-head guard and
    kern_dos's twin: C1's correctness half), BR-106 (`bugs.md:1097`), BR-107 (`bugs.md:1098`) (it must
    land before S09's optional MEMP_PURGEABLE), BR-120 (`bugs.md:1111`), BR-121 (`bugs.md:1112`),
    BR-188 (`bugs.md:1164`) (**O12**), BR-189 (`bugs.md:1165`), BR-193 (`bugs.md:1169`). BR-122's text is S05's.
  - **WP17:** heap placement by the two-ends rule; the sorted-table prototype
    (mem-14, 0 shipped); the ~20 internal KB callers moved to the paragraph
    entries (C1's correctness half, about neutral, unmeasured).
- **Files:**
  - kernel/: `loader.inc`, `memory.inc`, `filecp.inc` (floor sites;
    FILECP.DRV on kern_small), `compress.inc` (`cmz_wclaim`'s first try,
    `cmz_jsayx`);
  - `apps/os88partsbody.inc` (BR-59: the lazy part re-fetch only);
  - `apps/cc/crt0.asm` (BR-193);
  - `kerndos/kdshim.inc` (BR-105's kern_dos half);
  - the C1 caller files.
- **Neighbours:**
  - S14 edits `os88partsbody.inc`'s size read (BR-191). Merge before it.
  - S06 edits `compress.inc`'s divide sites only.
  - S13 does not edit `memory.inc`: BR-106 is yours.
- **SPEC first:** SPEC.md 20.13.8.1, 50 and 66.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | batch D core | +104 (+83..+119) | +94 `.cold` (+73..+109) | 0 conv (+0..+2 `.bss` in BR-7's fold); core above 1MB |
  | batch D rest (all) | +33 | +70 | 0; +4 high (BR-106) |
  | WP17 | 0 | +8 | +8 high |
  | loader-8 | | -3 `.cold` | -20 high |
  | under O3 | BR-29 +13, BR-30 +18 | the same | |
  | modules / packages | FILECP.DRV a few bytes (BR-11) | +20 in EVERY parts package's image (BR-59); kern_dos +1..+4 (BR-105) | |

- **Owner:** O3 (BR-29, BR-30), O12 (BR-188), O14 (BR-104).
- **Rows to name:**
  - fast: `heapgran`, `drvmem`;
  - soak: `lzfence`, `lzload`, `lzload-lz4`, `lzfmt-all`, `lzbig`, `lzcomp`,
    `fcpcopy`, `fcpapi`, `fcproom`, `fcpsmall`, `fdlgsmall`, `memp`,
    `regrowshed`, `clipgrow`, `heapcheck`, `heapmap`, `heapdrv`,
    `msegnomem`, `pkgmove-*`, `dosmem`, `assocopen`, `stikboot`, `thewire`,
    `megashed`, `cmemmove`, `heaphi`, `wdheapmove`, `mega-heaphi`;
  - full: `small128`, `megahigh`;
  - `tools/heapmap.py` before and after;
  - new: a hostile-header package (BR-7); a shed during a mount on the 128KB
    machine (BR-2); closing a window mid-paste (BR-11); `ifoff --regrow`
    (BR-12).
- **Done when:**
  - batch D core has landed;
  - BR-53 has landed, so S09 can start;
  - BR-105 and the internal KB callers are converted.

### 3.10 S09 - Module lifetimes: drop to a cache (WP2a)

- **Branch:** `big-review/s09-modcache`. **Wave 4.** Cut after S07 (BR-4) and
  S08 (BR-53) have merged.
- **Goal:** give kernel modules the lifetime ONDEMAND-PLAN 7.1/7.2 designed
  and never built: dropped to a purgeable cache, re-adopted on the next need,
  reloaded only if shed. The pin is the need/drop bracket `t_moddrop` already
  guarantees (WP2 (`phase1.md:254`)). Theme 7's third lifetime (theme 7 (`systemic.md:984`)).
- **Items:**
  - rt-1 drop-to-cache;
  - rt-9's ranks;
  - shell-2's two users;
  - optionally hm-9 `MEMP_PURGEABLE`:
    - it needs BR-107 (`bugs.md:1098`) (S08) first, and `OS88_NEED` in callers;
    - every shipped kernel answers an unknown `OSAPI_MEM_P` verb as
      `MEMP_AVAIL` with CF clear;
    - the proc call runs at IF=1 under `sch_lock` (WP2 (`phase1.md:254`)).
  - rt-2 (FILECP and FDLG) is NOT here: it is S10's, on O4.
- **Files:** kernel/`mod.inc`, `memory.inc` (`mem_pg_own`, `mem_shed_one`),
  `desksc.inc`, `stikmenu.inc`; `tests/moddrop.txt`,
  `tests/unit/t_moddrop.py` (it must recognise the cache as a drop).
- **SPEC first:** SPEC.md 2.8.3 (the lifetime) and SPEC.md 38.0.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | rt-1 + rt-9 | 0 (**O5**: +60..+85) | +60..+85 `.cold` | 0 conv, +60..+85 high (conventional-heap modules only) |
  | optional hm-9 | +4..+6 (the refusal) | +50..+80 `.cold` | +50..+80 high |

- **Owner:** O5 for the kern_small arm.
- **Rows to name:**
  - fast: `moddrop`, `stik`;
  - full: `kernresident`, `small128`, `bootsmoke`;
  - soak: `stikmenu`, `stikshort`, `stiknodesk`, `desksc`,
    `deskitem`, `shedrelist`, `regrowshed`, and the `mega-` twins (kern_mega
    must stay unchanged);
  - new: Sticky Apps pressed twice gives one disk load.
- **Done when:**
  - a dropped module is re-adopted without a disk read while its claim is
    unshed;
  - `t_moddrop` accepts the cache as a drop.

### 3.11 S10 - FILECP and FDLG cached at boot on kern_big (WP2b, **owner decision O4**)

- **Branch:** `big-review/s10-fcpcache`. **Wave 5.** Cut after S09 has
  merged, and **only on the owner's word**.
- **Goal:** rt-2 with bytes-3 (WP2 (`phase1.md:254`)). kern_big's two largest resident
  bodies become modules cached at boot. The boot read does not grow, because
  the bytes are already in the cylinder run. The refusal narrows to a purged
  image under pressure, and the gap review proposes the existing swap prompt
  for that case, COMMAND.COM's answer, instead of a refusal.
- **Items:**
  - the FILECP/FDLG arm on kern_big and kern_emu only;
  - the ROM/HMA skip, because the cache saves nothing where `.cold` is
    ROM- or COLDHMA-adopted;
  - the swap prompt;
  - **do not apply on kern_mega**: its `.cold` is already above 1MB.
- **Files:** kernel/`filecp.inc`, `fdlg.inc`, `kernel.asm`
  (FCP_MOD/FDLG_MOD on kern_big); `boot/boot2.asm` (the copy at
  `mem_unblob`); `tools/os88mod.py`; Makefile (the four kern_big system
  disks).
- **SPEC first:** SPEC.md 38.0 ("On kern_big the glue is NOT a module, on
  purpose" is the sentence being reversed) and SPEC.md 2.8.3.
- **Bytes:**
  - kern_big **-2,468..-2,508 resident**: -2,568 movable, MEASURED spans
    (FILECP about 1,657, FDLG about 911), plus +60..+100 glue and prompt.
  - 1,089 bytes of the kernel's `os88ui` copy stay resident.
  - With S09's rt-1 the WP2 core is -2,383..-2,448.
  - Disk: about +4 clusters on each kern_big system disk.
  - Runtime: a reload is 1-2 int 13h (0.4-0.8 s PREDICTED) on a path that
    today never touches the disk.
- **Rows to name:**
  - soak: `fcpapi`, `fcpcopy`, `fcpsmall`, `fcproom`, `fdlggrey`,
    `fdlgsmall`, `fdlgdrop`, `fdlgup`, `fdlgchoose`;
  - full: `kernresident`, `bootsmoke`;
  - new: shed FILECP under pressure, then Save As shows the prompt; the same
    on a COLDHMA machine, with no preload claim.
- **Done when:** the trade was put to the owner with the three quotes, and
  either it landed with its rows named, or the decision "no" is written in
  `DECISIONS.md` and the checklist.

### 3.12 S11 - Residency by hardware: keyboard mouse, PS/2 overlay, text-7 (WP3, input-10)

- **Branch:** `big-review/s11-residency`. **Wave 6.** Cut after S09 (the boot
  cache), S12 and S16 have merged, because `mouse.inc` is busy until then.
- **Goal:** code for hardware this machine lacks, or code needed only until
  something happens, should not stay resident (WP3 (`phase1.md:419`)). Rungs serve nested
  machine classes only (rt-4), so class code goes in owner-loaded overlays,
  not in more rungs (theme 7 (`systemic.md:984`)).
- **Items:**
  - rt-7: KBMOUSE as an init-lifetime module, held from boot and dropped at
    settle (**O6**).
  - rt-3: the PS/2 runtime as an owner-loaded overlay, with its door registry
    on `tests/vgarefs.txt`'s shape (not owner-gated).
  - text-7 (**O7**).
  - WP22's input-10: PS/2 sync, which now lives in the overlay.
  - Refused: bytes-2's AT rung.
- **Files:**
  - kernel/: `mouse.inc`, `vmmouse.inc`, `font.inc` (the parity pad at
    font.inc:1839 and `t_hotpar` move with text-7), `mod.inc` (mod_tab rows),
    `kernel.asm` (the slide decision; the boot-cache copy);
  - a new `drivers/ps2mouse/` (or a DRVC_OVL row beside XMEM's);
  - `tests/vgarefs.txt`, `tests/moddrop.txt` (an "init lifetime" reason);
  - Makefile (KBMOUSE.DRV on every system disk).
- **SPEC first:** SPEC.md 9.6.1.1, 9.12 and 2.10.9 (rt-4's nesting rule).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | rt-7 KBMOUSE (O6) | -290..-380 (gross of the boot copy) | -290..-380 | -290..-380 conv |
  | rt-3 PS/2 overlay | n/a | **-220..-235 on every machine** (+14 inside the overlay for input-10) | 0 (kept resident) |
  | text-7 (O7) | n/a | +16 gauge; -165..-185 mono | 0 |
  | total | -290..-380 | -494..-599 gauge; -675..-800 mono XT | -290..-380 conv |
  | modules | KBMOUSE.DRV +400..+450 | KBMOUSE.DRV +400..+450; PS/2 overlay ~+265 | KBMOUSE.DRV +400..+450 |

- **Risk:** the drop sites must cover every `[mou_ptr]` store on every
  kernel.
- **Rows to name:**
  - fast: `ovlchk`, `moddrop`;
  - full: `ps2mouse` (QEMU: the PS/2 mouse is on the closed list),
    `kernresident`;
  - soak: `wheelps2`, `mega-ps2mouse`, `vmmouse`, `vidresidentcga`,
    `vidresidentvga`, `disptext`, `glyphcost`, `hibernate`,
    `box-ps2mouse`;
  - new: a pointerless desktop driven by arrow keys on a CGA `_gla` machine
    (no `kbm*` row exists), and the same after a hibernate resume.
- **Done when:** rt-3 has landed; O6 and O7 are answered and landed or
  declined; `t_moddrop` knows the init lifetime.

### 3.13 S12 - Drivers, the IRQ scan and the matrix gates (WP4, WP22's driver half, batch G's driver half, WP31)

- **Branch:** `big-review/s12-drivers`. **Wave 4.** First in the merge order.
  Cut after S06 has merged, so that the shadow exists.
- **Goal:**
  - Shrink what kern_big keeps per driver class, and make
    `drivers/os88drv.inc` say what the kernel does, in a single DRV_VER
    update (WP4 (`phase1.md:517`)). This is theme 10's sure saving: read the service tables
    in place (theme 10 (`systemic.md:1433`)).
  - Make the kernel own its IRQ vectors at release (BR-34 (`bugs.md:478`)'s scan).
  - Fix the driver defects (batch G (`bugs.md:1370`)).
  - Make each kernel's separate code honest by gate (WP31 (`phase1.md:2089`), theme 11 (`systemic.md:1533`)).
- **Items:**
  - **WP4:**
    - histsys-7: read in place, -90..-140;
    - N1 (-11 resident);
    - D4: the service-table floor written down, plus an `%error` in
      `OS88_DRIVER`. Not a flag-day row any more (`flagday.md:102`);
    - drivers-5 (keep "preserve BX" as a portability sentence; truth lens);
    - drivers-14;
    - bytes-8;
    - isr-11;
    - BR-145 (`bugs.md:1136`)'s `os88drv.inc` half.
  - **WP22's driver half:**
    - drivers-6: the release-time IVT scan (BR-34's vector scan, +25
      `.cold`);
    - SOUND.DRV's IMR (+6) and OCW3 (+8) through the shadow;
    - the optional `OSAPI_IRQ` cell (**O16**).
  - **Batch G's driver half:**
    - BR-5 (`bugs.md:188`) as K1b (docs/plans/NO-PINNING-PLAN.md's row);
    - BR-31 (`bugs.md:448`), BR-32 (`bugs.md:458`), BR-33 (`bugs.md:468`).
  - **WP31:**
    - the build-name ratchet with `tests/buildnames.txt`;
    - a `small-` port list for `tests/suite.py`'s `_ports`, in a new
      `tests/smallport.txt` (beside `megaport.txt` and `box86port.txt`);
    - `OSAPI_XREFUSE`;
    - the `diskw.inc` and `snd.inc` stubs;
    - `tests/kernresident.py`'s mono row;
    - gkm-1's two fixes and its rule in docs/plans/KERN-MEGA-PLAN.md's
      checklist;
    - gkm-6 (**O10**, prototype against `vmmouse` first).
    - The kernsize tally line already landed in S01.
- **Files:**
  - kernel/: `driver.inc`, `mod.inc`, `kernel.asm`, `ctrl.inc`, `hiber.inc`
    (WP4's reads only), `snd.inc`, `diskw.inc` (the stub);
  - `boot/boot2.asm` (`%1_call`);
  - drivers/: `os88drv.inc`, `sound/sb.inc`;
  - tests/: `unit/t_hotpar.py`, `unit/t_api_abi.py`, `buildnames.txt`,
    `suite.py`, `smallport.txt` (new), `kernresident.py`;
  - `tools/kernsize.py`;
  - docs/plans/: `KERN-MEGA-PLAN.md`, `NO-PINNING-PLAN.md`.
- **Neighbours:**
  - S09 (same wave) is in `mod.inc`'s lifetime code. You are in its slot
    tables. Merge first.
  - S21 (wave 5) owns the rest of batch G.
- **SPEC first:** SPEC 51 (the driver contract) and SPEC.md 51.8.1; SPEC 10
  (IRQ ownership).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP4 | -10 | -87..-147 | -195..-205 conv, +40..+100 high |
  | batch G driver core | 0 | +81 (BR-5 +28 `.cold` and +11 `.bss`, BR-31 +12, BR-32 +5, BR-34 scan +25) | +11 `.bss` conv, ~+70 high |
  | O16 `OSAPI_IRQ` | | +100..+150 | |
  | O10 gkm-6 | | about +40 (DRV_MAX 6 -> 7) | |
  | kern_emu | | DRV_MAX gives `.bss` +12 | |
  | modules | | blob, CTRL.DRV and HIBER.DRV +2..+4 each (drivers-5); SOUND.DRV +14 (-60..-100 with O16) | |

- **ABI / lock:** one DRV_VER-side `abilock --update` for the contract text,
  no bump. N1 and D4 need no bump (flag-day 3.1).
- **Rows to name:**
  - fast: `drvovl`, `drvmem`, `mirror`, `abilock`, `hotpar`, `api-abi`;
  - full: `kernresident` (plus its new mono row);
  - soak: `drvcall`, `drvmove`, `drvclaim`, `hmadrv`, `lzdrv`, `heapdrv`,
    `vidsound`, `arcsound`, `midiracksb`, `usbmouse`, `hibernatedrv`,
    `cpnames`, `extdmod`, `dockmodule`, `hddcp`, `irqgrab`, `dosirq`,
    `sbirq5`, `sndkeep`, `sndonce`, `sndmove`, `vmmouse`, `buildmatrix`, the
    `mega-` twins, `box-ps2mouse`;
  - new: the seeded `small-` rows from `tests/smallport.txt`;
  - new: untick a second driver while USBMOUSE's worker is up (BR-5).
- **Done when:**
  - no driver release leaves a vector behind;
  - every driver unload waits only on its own workers;
  - the build-name ratchet runs.

### 3.14 S13 - kern_mega (WP5, batch H)

- **Branch:** `big-review/s13-mega`. **Wave 3.** Last in the merge order.
- **Goal:** kern_mega's and kern_dos's free bytes (WP5 (`phase1.md:600`)) and kern_mega's
  defects (batch H (`bugs.md:1386`)). Also L7, the protected-mode-clean bit, which is
  flag-day row L3's staged road and needs no bump for packages. **Implement
  with Opus from the start** (descriptor races, selectors).
- **Items:**
  - **WP5:** md-8, md-19, md-20, md-21; gms-5, gms-8, gms-9; gms-R2 (after
    gms-4); L7 (mc-19's bit in LD_H_CLASS bit 5); mc-10's `[mega_kfix]`; the
    shared A20 include (S22 and S23 depend on it); the kern_dos image savings.
    md-15's park-header sum is optional: KDL_VER 6 -> 7, kern_mega arm only.
  - **Batch H core:** BR-13 (`bugs.md:268`), BR-64 (`bugs.md:778`), BR-67 (`bugs.md:808`) (0 bytes), BR-69 (`bugs.md:828`),
    BR-70 (`bugs.md:838`), BR-71 (`bugs.md:848`), BR-184 (`bugs.md:1018`), BR-185 (`bugs.md:1028`), BR-186 (`bugs.md:1038`), BR-187 (`bugs.md:1048`).
  - **Then:** BR-65 (`bugs.md:788`) (form (c) now; form (a) is **O15**), BR-66's LDT half
    (with gms-10's walkers), BR-68 (`bugs.md:818`).
  - **Low:** BR-144 (`bugs.md:1135`), BR-161 (`bugs.md:1152`) through BR-170 (`bugs.md:1161`), BR-204 (`bugs.md:1180`),
    BR-205 (`bugs.md:1181`). BR-66's compaction model is a flag-day row.
- **Files:**
  - kernel/: `mega.inc`, `megados.inc`, `megapark.inc`, `megares.inc`,
    `hiber.inc` (md-19), `loader.inc`/`instance.inc` (the class mask: after
    S05's header-bit ledger), `fsx.inc` (BR-184's real-mode door only),
    `vga12.inc` (BR-64's KERN_MEGA arm only);
  - kerndos/: `kdmouse.inc`, `kdhma.inc`, `kdpark.inc`, `kdresume.inc`,
    `kdentry.inc`, `kdosgate.inc`, `kdback.inc`, `kdshim.inc` (after S08's
    BR-105 half), `kdlayout.inc`;
  - the new shared A20 include, used by `drivers/xmem/xmem.asm` and
    `drivers/hdd/hdxms.inc`;
  - `tools/os88pkg.py` (`:211` masks 0x07 like the kernel; sched.inc's
    `%error` too);
  - `apps/dos/dos.asm` (md-20).
- **Neighbours:**
  - BR-106 (`memory.inc`) is S08's.
  - S08 edits `kdshim.inc` for BR-105. Take its merged version.
- **SPEC first:** the kern_mega sections each item names; SPEC 20.2's header
  byte +15 (L7).
- **Bytes:**

  | | kern_mega conv | kern_mega high | other |
  |---|---:|---:|---|
  | WP5 | -40 | -145..-165 | each kern_mega kern_dos image -1,585..-1,588; kern_big's kern_dos image -560; HIBER.DRV +~40 |
  | batch H core | +20 `.text` (BR-64, unless placed in `.mega`) | +19 | HIBER.DRV -13; HMA kern_dos +7; `.boot2` +8 (fits in 285 after S03) |
  | batch H all | +20 (+128..+192 `.lowbss` only with O15's form (a)) | ~+141 | HIBER.DRV -9 |
  | L7, mc-10 | +2 `.bss` | +12, +8 `.mega` | |

- **Rows to name:**
  - fast: `mirror` (once S01's md-5 covers kerndos), `megagrid`, `megapkg`;
  - full: `megahigh`;
  - soak: `kerndos`, `kdos`, `kdarena`, `kdbigexe`, `kdhand`, `kdcylrun`,
    `kdpart`, `kdreturn*`, `kdkbd`, `kdmouse`, `dosbss`, `dosxms`,
    `megahiber`, `megadosp`, `megadosroom`, `megatrap0`, `megahibfull`,
    `megap2hiber`, `win16megatrap`, `win16mega*`, `xmla`, `xmcheck`,
    `hdboot`, `dosmouse`, `lzload`.
  - **QEMU checks no segment limit and cannot fail BR-13, BR-64 or BR-65**:
    those want Bochs (docs/BOCHS.md). Name three probes: a lent-stack int 10h,
    a 3-byte-slack canvas, and a foreign-stack option-ROM IRQ.
- **Done when:**
  - batch H core has landed;
  - L7 is readable by the class mask on every kernel;
  - the A20 include exists for S22 and S23.

### 3.15 S14 - Disk and file layer (batch E, WP16)

- **Branch:** `big-review/s14-disk`. **Wave 3.** Fourth in the merge order.
- **Goal:** the disk and file defects (batch E (`bugs.md:1340`)), among them the FAT16
  write that leaks every free cluster on Disk full (BR-17 (`bugs.md:308`)) and the media
  change thrown away (BR-16 (`bugs.md:298`)). Then the read path, directory and listing
  (WP16 (`phase1.md:1394`)). Theme 3's file half and theme 5's hostile files (theme 3 (`systemic.md:439`),
  theme 5 (`systemic.md:702`)).
- **Items:**
  - **Core:** BR-16 (`bugs.md:298`) (plus BR-183's kernel half, int 13h status 11h in the
    BIOS loop, unpriced), BR-17 (`bugs.md:308`), BR-22 (`bugs.md:358`) (**O3**: retail, or APICHK=1
    only), BR-23 (`bugs.md:368`), BR-24 (`bugs.md:378`), BR-27 (`bugs.md:408`) (-12), BR-37 (`bugs.md:508`) (**O3**),
    BR-41 (`bugs.md:548`).
  - **BR-62's stub halves:** dw-8's label move, gi-6's VOL_STAT stub (+4
    kern_small, also flag-day 3.1's kern_small VOL_STAT tail), abi-2's SDK
    text. Land them with BR-179 (`bugs.md:968`).
  - **Then:** BR-61 (`bugs.md:748`); BR-60 (`bugs.md:738`) only on **O13**.
  - **Low:** BR-123 (`bugs.md:1114`) through BR-130 (`bugs.md:1121`), BR-133 (`bugs.md:1124`) (**it lands first**:
    S15 needs it), BR-134 (`bugs.md:1125`) through BR-143 (`bugs.md:1134`), BR-190 (`bugs.md:1166`), BR-191 (`bugs.md:1167`) (the
    parts size read only).
  - **WP16:**
    - dr-5: an appended `OSAPI_FILE_FIND_SEQ`;
    - gi-2: an appended `OSAPI_FILE_GRAIN`;
    - dr-11, files-5, dw-21, dr-10;
    - **F3 phase 1**: a dual-mark reader, and `os88disk.py` writes a mark with
      bits 3 and 4 clear. This is flag-day row F3's no-break half.
- **Files:**
  - kernel/: `disk.inc`, `diskw.inc`, `files.inc`, `fdlg.inc` (FDLG.DRV on
    kern_small), `fsck.inc` (FSCHECK.DRV), `assoc.inc`, `ascw.inc`
    (FORMAT.DRV), `filecp.inc` (BR-128's doc only), `kernel.asm`
    (kern_small's refusal stubs; the two cells);
  - apps/: `os88wseq.inc` (BR-179), `os88partsbody.inc` and `os88pself.inc`
    (BR-191), `os88rseq.inc`, `os88api.inc`, `apps/cc` (the FIND_SEQ and
    GRAIN thunks);
  - `tools/os88disk.py`.
- **Neighbours:**
  - S08 edits `os88partsbody.inc`'s lazy re-fetch. Merge after it.
  - S03 edits only `dsk_boot_from_x` in `disk.inc`.
  - S06 edits only `kernel.asm`'s install.
- **SPEC first:** SPEC.md 18 and 19; SPEC.md 18.4 for kern_small's late
  FERR_FULL.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | batch E core | +73 (+68..+76) | +68 (+63..+71) | 0..+9 conv; the rest high |
  | WP16 | +22..+27 | +77..+88 | +6..+12 conv, +70..+80 high |
  | batch E low (if all taken) | +66 | +152 | |
  | O13 BR-60 | optional | +90 | |
  | `.ovl` | | +10 (BR-141), after S06 in the ledger | |
  | modules | FDLG.DRV +8 | FSCHECK.DRV +6 +10; FORMAT.DRV +10; os88wseq carriers +2 | |
  | under O3 | BR-22 +12, BR-37 +16 | the same | |

- **ABI / lock:** two appended cells and the SDK texts: one
  `abilock --update`, no bump, after S05's level. Callers use `OS88_NEED`.
- **Risk:** F3 touches every shipped image's directory entries. Verify with
  `os88disk.py --verify` and a host mount.
- **Rows to name:**
  - fast: `api-abi`, `diskverify`, `volsig`;
  - soak: `fdswap`, `wseq`, `dosmedia`, `curdisk`, `dosfull`, `fsckhd`,
    `fsckdisk`, `fmcommit`, `fdlgchoose`, `fdlgup`, `fdlgdrop`,
    `fdlgsmall`, `pkgmove-*`, `dosfile`, `dosdir`, `assocstale`,
    `assocflush`, `cdromsim`, `deskfdd`, `hddgeom`, `assocfind`, `rdmount`,
    `imgmount`, `fmarrows`, `navcalls`, `dirwsize`, `shedrelist`,
    `fddpage`, `bigvol`, `lzload`;
  - new: a worker calling a file cell (it must refuse, under O3's answer); a
    hostile FileSize entry; an unreadable FAT; a 15th file in a kernel-made
    folder; a kern_small REDLINE save; a packed file listing in upper case on
    a Linux vfat mount (a host check).
- **Done when:**
  - batch E core and BR-133 have landed;
  - the two cells are appended behind the level;
  - F3's reader ships, and every image round-trips `--verify`.

### 3.16 S15 - FAT write path: free count, contiguous runs, a bracketed flush (WP12)

- **Branch:** `big-review/s15-fatwrite`. **Wave 4.** Cut after S14 has merged
  (BR-133, BR-17).
- **Goal:** cost disk work in calls (WP12 (`phase1.md:1123`)). Today free space is
  recomputed by walking the whole FAT, allocation fragments one cluster at a
  time, and a multi-file delete pays two FAT writes per file. Theme 3 (theme 3 (`systemic.md:439`)).
  **Implement with Opus from the start**: the data-integrity path.
- **Items:**
  - histsys-3: a kept free count;
  - dw-15;
  - dw-13: refuse FERR_FULL before the first data sector;
  - dw-16: reset the rover only on the non-keepcwd mount arm;
  - dw-17;
  - dw-20 (do it first);
  - **dw-14 on O9 only**: contiguous runs, SPEC 18.4's non-goal rewritten as a
    dated decision first;
  - the optional FAT12 scan (+40);
  - kern_small gets none of histsys-3, dw-13 or dw-14. Say so in SPEC 18.4.
- **Files:** kernel/`diskw.inc`, `disk.inc`, `fsck.inc` (`fsk_findrun`'s
  idea).
- **SPEC first:** SPEC.md 18.4 and 18.4.5; SPEC 18.4.10.
- **Bytes:**
  - kern_small 0..+3 (dw-16).
  - kern_big **+160..+255 `.cold`**: histsys-3 +35..+50, dw-15 +15..+25,
    dw-14 +100..+160 (O9), dw-13 +10..+15, dw-16 0..+3; plus +40 for the
    optional FAT12 scan.
  - kern_mega 0 conv, +160..+255 high.
  - Without O9: kern_big +60..+93.
- **Rows to name:**
  - fast: `cswrite`;
  - full: `sysfree`;
  - soak: `wseq`, `wseqheld`, `wsequnclosed`, `wseqinter`, `wseqdeleted`,
    `wseqcut`, `wseqioerr`, `wseqfull`, `dtwrite`, `fsckdisk`, `fsckhd`,
    `bigvol`, `fatwpin`, `ftpd`, `fcpcopy`, `diskclone`, `mega-wseqfull`,
    `mega-dtwrite`;
  - new: a 50-file rmtree's int 13h count before and after (a counter, not a
    timer); a swap inside a batch; a pulled floppy during a deferred flush
    (one attempt, then quiet).
  - The A/B leg against the starting commit runs in the final pass.
- **Done when:**
  - free space no longer walks the FAT;
  - a delete batch flushes once;
  - every image passes `os88disk.py --verify` and a host mount after a
    write-heavy session.

### 3.17 S16 - Scheduler: UI boost, a wait primitive, lock honesty (WP18)

- **Branch:** `big-review/s16-sched`. **Wave 5.** Cut after S06 and S12 have
  merged (`sched.inc`, `mouse.inc`, `sb.inc`).
- **Goal:** input should not wait behind every ready worker; a task should be
  able to block on something other than time; `sch_lock`'s rules should be
  numbered (WP18 (`phase1.md:1507`)). Theme 3's wait primitive is "the one change of model"
  the scheduler review names (theme 3 (`systemic.md:439`)).
- **Items:**
  - the UI boost, **measure first** (the boost's +10..+22 is the first
    candidate to cut);
  - sched-15: `OSAPI_TASK_WAKE`, an appended 6-byte cell;
  - the `sch_lock` rules numbered (BR-80's lock text);
  - SOUND.DRV refill on wake;
  - histsys-5 optional, and only after its three preconditions: input-2,
    ctx-3's task-scoped `cur_inxfer`, and the windowed-DOS IVT precondition.
    The new `xferfreeze` row measures it.
- **Files:** kernel/`sched.inc`, `events.inc`, `ui.inc`, `fsx.inc`;
  `disk.inc` and `mouse.inc` (histsys-5 only); `drivers/sound/sb.inc`;
  `apps/os88api.inc`.
- **SPEC first:** SPEC.md 7 and 8.
- **Bytes:**
  - kern_small, kern_big and kern_mega conv: **+50..+77 `.text`** each, plus
    the 6-byte cell.
  - The optional histsys-5 adds +33..+40, and +~25 with the AT hook.
  - The boost and the door figures are PREDICTED cycles; the A/B leg takes
    them in the final pass.
- **ABI / lock:** one appended cell, `abilock --update`, no bump.
- **Rows to name:**
  - fast: `hotpar`, `stkclass`;
  - soak: `kbdpreempt`, `kbdburst`, `timerrace`, `timerbounce`, `stk0water`,
    `stkpanel`, `splashspin`, `vidsound`, `midiracksb`, `midiracksbrate`,
    `skiessound`, `wakedrain`, `uilat`, and the `mega-` twins;
  - new: `xferfreeze` (histsys-5).
- **Done when:**
  - `task_wake` is appended behind the level;
  - the boost has been measured once and either landed or been dropped with
    its number;
  - the lock rules are in SPEC.md 7.

### 3.18 S17 - Display, FSX and the drawing hot paths (batch B, WP20, WP23)

- **Branch:** `big-review/s17-display`. **Wave 4.** Last in the merge order.
  Cut after S03 (BR-14) and S07 (WP21's region sort) have merged.
- **Goal:**
  - the display defects (batch B (`bugs.md:1296`)), among them rank-3 BR-3 (`bugs.md:168`):
    `gfx_blit4` trusts the sign of its width, and 64KB of heap is
    overwritten;
  - the per-adapter holes in the primitives packages call most, and the one
    shared control that letters after it fills (WP20 (`phase1.md:1629`));
  - the two zero-byte video facts (WP23 (`phase1.md:1785`)).
- **Items:**
  - **Batch B core:** BR-3 (`bugs.md:168`), BR-45 (`bugs.md:588`), BR-46 (`bugs.md:598`), BR-57 (`bugs.md:708`), BR-174 (`bugs.md:918`)
    (texts, and the GFXAUDIT knob).
  - **Low:** BR-58 (`bugs.md:718`), BR-131 (`bugs.md:1122`), BR-132 (`bugs.md:1123`), BR-147 (`bugs.md:1138`) (blob +7: **after
    O1**), BR-148 (`bugs.md:1139`), BR-149 (`bugs.md:1140`), BR-151 (`bugs.md:1142`) (FSIF_FLIP: CLAUDE.md and
    AGENTS.md's WireBall section change with it).
  - Already done elsewhere: BR-14 (S03), BR-77 (S05), BR-152 (S01).
  - **WP20:** gfx-9, gfx-8, gfx-15 (on S07's region sort), gfx-4, shell-9, os88ui:F1
    (the button's drawing order, a new `kernel/theme.inc`), and cursor:mc-9's
    `curaudit`. Measure each first on its own `gfxbench` row.
- **Files:**
  - kernel/: `fsx.inc`, `vga12.inc`, `vidsel.inc`, `apps.inc`, `fprog.inc`,
    `font.inc`, `menu.inc` (`menu_widest`), `viddet.inc` (VID_APPLY),
    `softgfx.inc`, `icons.inc`, `desk.inc`, `winmod.inc`, `mouse.inc` (the
    cursor drawing only), `ui.inc`, the new `theme.inc`;
  - `apps/os88ui.inc`.
- **SPEC first:** SPEC.md 5.4, 6.1, 7, 53.10 and 13.14.6; SPEC.md 4 and 5
  (WP20).
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | batch B core without BR-14 | +8 | +39 (+2 `.cold` if gfx_blit1 is taken) | +39 conv |
  | batch B low | +12 | +65 | ~+63 |
  | WP20 | +32..+44 | +120..+146 gauge (+24..+36 `.text`, +8 `.cold`, os88ui:F1 +8..+12 `.cold`, +80..+90 `.vga`); a mono machine +40..+56 | +24..+36 conv, +16..+20 high |
  | transient | | `.boot2` +7 (BR-147, after O1) | |
  | modules | CTRL.DRV and FDLG.DRV +8..+12 each | WIN95.DRV +6; VID_APPLY's expansions +14; GFXAUDIT knob ~+40 | |

  The `.vga` part counts against kern_big's 6,946-byte VGA window.
- **Rows to name:**
  - fast: `hotpar`;
  - soak: `dispblit`, `blitcut`, `blitpair`, `blitplane`, `atblit`,
    `atblitvga`, `wireballvideo`, `wireballmono`, `dispfsxherc`, `fsxdisp`,
    `dispfsx`, `vesa`, `vesacp`, `saver`, `savervga`, `toastfpg`,
    `dockmodule`, `dispreboot`, `vgadirty`, `vidpage*`, `gfxpoints`,
    `gfxptsmall`, `xorrect`, `dispblitp`, `blitp`, `gfxlk`, `glyphcost`,
    `deskbench`, `dispapps`, `rdicon`, `zonedmg`, `btnall`, `btncp`,
    `btngesture`, and the games' rows for gfx-4;
  - `m.flicker()` on a Disk-window header press (os88ui:F1);
  - new: a negative-width BLIT4 call, which must go red on `jz`; a GFXAUDIT
    row; `curaudit`.
  - Look at a 1bpp adapter before calling a drawing change done (SPEC.md
    39.4).
- **Done when:**
  - BR-3 cannot be reached with a negative extent;
  - each WP20 item has its `gfxbench` number before and after (taken once);
  - os88ui:F1 letters without a fill-then-letter.

### 3.19 S18 - The KHP round (WP11, **owner decision O8**)

- **Branch:** `big-review/s18-khp`. **Wave 6.** Cut after S17, and only on
  the owner's word.
- **Goal:** no indefinite shelf (WP11 (`phase1.md:1058`)). LAST-DROP-PERF 7.1 measured these
  knobs on the 4.77 MHz XT. kern_mega carries rows 1-11; kern_big, the XT's
  build, carries none but KHP_LZ. This is a request to the owner for a bigger
  round, not the review's default. The owner's bar for khp1 was "around 100b"
  (docs/plans/khp1/LOG.md).
- **Items:**
  - take each knob, or move it to `docs/plans/khp1/patches/`;
  - date each decision in `docs/plans/LAST-DROP-PERF.md`;
  - keep the kern_small arms under KERN_SMALL;
  - wm-19 (band.inc's BAND=1 knob) retires under the same rule, if the owner
    agrees.
- **Files:** `kernel/kernel.asm:252-307` (the KHP block), `wm.inc`,
  `softgfx.inc`, `vga12.inc`, `mouse.inc` (the VGASAVE arms in `.vga`),
  `blank.inc:540-582`, `fsx.inc:500`; SPEC.md 5.7.
- **Bytes:** kern_big **+104..+107 on every machine and +182 `.vga`, so
  +286..+289 on the gauge**; kern_small 0; kern_mega 0.
- **Rows to name:**
  - full: `khpknobs`, `knobself`, `kernresident`;
  - soak: `buildmatrix`, `glyphcost`, `dispsaver`, `dispfsx`, `dispfsxcga`,
    `dispfsxherc`, `zonedmg`, `dmgcull`, `dispzoom`, `xorrect`, `vgadrop`,
    `vgadirty`;
  - a `m.flicker()` pointer sweep on `os8088_xt_vga` for VGASAVE;
  - the A/B leg in the final pass.
- **Done when:** every knob is taken or patched, with a date.

### 3.20 S19 - SDK-side work in packages (WP15)

- **Branch:** `big-review/s19-sdk`. **Wave 5.** Cut after S08 (`loader.inc`)
  and S14 (gi-2) have merged.
- **Goal:** bytes and correctness in the shared package includes, and two
  small SDK additions that retire a standing rule violation (WP15 (`phase1.md:1305`)).
  Theme 6's package-owned cache and theme 13's includes (theme 6 (`systemic.md:825`), theme 13 (`systemic.md:1730`)).
- **Items:**
  - **mem-17: the movable bit in LD_H_CLASS (+15) bit 3.**
    - New kernels mask `and bx,0x07`.
    - `os88pkg.py:211` must mask the same, and so must the `%error` at
      sched.inc:2891-2893.
    - An old kernel gives the largest stack class, which is safe (`phase1.md:171`).
  - text-15: a bold run cell.
  - text-16: `MENU_SEP` and `MENU_CHK`. An older kernel draws MENU_SEP as a
    selectable blank, and so does a kern_small without text-16's +12.
  - hm-11: a shared sub-allocator in source, a new `apps/os88lmem.inc`.
    Price it on xtmp3 first: 0.8-1.5KB saved per running parts instance at
    0 kernel bytes.
  - gi-3 (on S14's gi-2).
  - The parts init-drop.
- **Files:**
  - apps/: `os88partsbody.inc`, `os88parts.inc`, `os88pself.inc`,
    `os88wseq.inc`, `os88pit.inc`, `cc/crt0.asm`, `os88api.inc`;
  - kernel/: `loader.inc`, `memory.inc`, `font.inc`, `menu.inc`, the
    `%error` in `sched.inc`;
  - `tools/os88pkg.py`;
  - tests/: `unit/t_movable.py`, `textsites.txt`;
  - the new `apps/os88lmem.inc`.
- **SPEC first:** SPEC 20.2's header (+15), SPEC.md 66.6.1.1, and the text
  sections for the cells.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP15 | +24..+32 (mem-17 +12..+20, text-16 +12) | +42..+60 (mem-17 +12..+20, text-15 +35..+45, text-16 -5) | +30..+40 conv, +12..+20 high |
  | packages | mem-17 -10..-19 per image unpacked; hm-11 unpriced until the facility | | |

- **ABI / lock:** appended cells and the class-byte text: one update, no
  bump.
- **Risk:** about 21 packages build these shared includes. The final pass
  rebuilds and boots them.
- **Rows to name:**
  - fast: `movable`, `textrules`, `pkg`, `abilock`;
  - soak: `wdparts`, `pxparts`, `rehome`, `rehomemove`, `rehomeabort`,
    `megarehome`, `sheetmsep`, `assocopen`, and the parts packages' own
    rows.
- **Done when:**
  - the class byte is masked identically by every reader;
  - the new cells are behind `OS88_NEED`.

### 3.21 S20 - Desk, dialogs and menus: additions (WP27)

- **Branch:** `big-review/s20-desk`. **Wave 5.** Cut after S07 and S14 have
  merged.
- **Goal:** the user-visible additions, and the one size saving left in this
  area once batches C, E and G own its fixes (WP27 (`phase1.md:1929`)).
- **Items:**
  - ui-10 (-30..-40);
  - files-17 (AH/BX);
  - ui-11 (one double-click interval, +9 in the core figure below);
  - ui-12 and ui-19, as optional cells;
  - **shell-3**: `OSAPI_DESK_ITEM`'s `AH = 0FEh`, the additive half of
    flag-day row C10. Check what an older kernel does with AH=0FEh before
    shipping a caller;
  - shell-8 (blob +10: **after O1**, or in a form that leaves the blob);
  - the WIN16 shortcut row.
- **Files:** kernel/`apps.inc`, `fdlg.inc`, `desk.inc`, `desksc.inc`,
  `ctrl.inc`, `ui.inc`, `clip.inc`; `apps/os88api.inc`;
  `apps/win16/w16progm.inc`.
- **SPEC first:** the sections each cell documents; SPEC.md 11.103.10 for the
  desk item.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | WP27 | -21..+9 | -15..-25 (ui-10 -30..-40, files-17 +6, ui-11 +9) | -21..-31 conv, +6 high |
  | optional cells | +34..+44 | +34..+44 | |
  | modules / transient | | CTRL.DRV +12; `.boot2` +10 (shell-8) | |

- **ABI / lock:** one update, no bump; callers use `OS88_NEED`.
- **Rows to name:**
  - fast: `toast`;
  - soak: `fdlgchoose`, `fdlgsmall`, `deskitem`, `desksc`, `stikmenu`,
    `clipkeep`, `mouseup`, and the WIN16 shortcut row.
- **Done when:** shell-3 has landed (phase 2 needs it), and each optional cell
  has landed or been declined.

### 3.22 S21 - Hibernate, the clock and system services (WP29, WP30, batch G's services half)

- **Branch:** `big-review/s21-services`. **Wave 5.** First in the merge
  order. Cut after S12 (`driver.inc`) and S13 (HIBER.DRV's kern_mega changes)
  have merged.
- **Goal:**
  - the rest of batch G (batch G (`bugs.md:1370`)), among them rank-9 BR-10 (`bugs.md:238`) (Esc on
    the Resume face deletes the hibernated session) and rank-15 BR-15 (`bugs.md:288`)
    (the clock gains about 31 seconds a day);
  - the module-side hibernate work (WP29 (`phase1.md:2018`));
  - the one-clock prototype (WP30 (`phase1.md:2052`));
  - **the `.ovl` relief kern_big needs** (2.5).
- **Items, in this order:**
  1. **The `.ovl` relief**: move `clk_probe`'s RTC rungs out of `.ovl` (SPEC.md
     2.5.3.2). Do it first, in its own commit, and quote the tally line.
  2. Batch G services core: BR-10 (`bugs.md:238`), BR-15 (`bugs.md:288`), BR-38 (`bugs.md:518`), BR-39 (`bugs.md:528`),
     BR-40 (`bugs.md:538`), BR-43 (`bugs.md:568`), BR-44 (`bugs.md:578`) (in its +5 form, or gfce-6's +22 form,
     which replaces it).
  3. Then BR-26 (`bugs.md:398`) (CTRL.DRV and kern_dos's date half) and BR-72 (`bugs.md:858`).
  4. Low: BR-96 (`bugs.md:1087`) (**before WP29**), BR-97 (`bugs.md:1088`) through BR-103 (`bugs.md:1094`),
     BR-157 (`bugs.md:1148`), BR-158 (`bugs.md:1149`), BR-159 (`bugs.md:1150`), BR-160 (`bugs.md:1151`).
  5. WP29: DISKOPS and HIBER module work.
  6. WP30: one clock, a prototype after BR-15 and BR-26.
- **Files:**
  - kernel/: `hiber.inc`, `hbstub.inc`, `diskops.inc`, `compress.inc`,
    `drvvol.inc`, `clock.inc`, `clockw.inc`, `ctrl.inc`, `blank.inc`
    (SAVER.DRV), `kernel.asm` (`osapi_rand`), `mega.inc:1362-1369` (WP30),
    `sched.inc`/`fsx.inc` (the skip count only);
  - `apps/timer/timer.asm`;
  - `apps/dos` (BR-26's kern_dos date half).
- **SPEC first:** SPEC.md 2.5.3.2 (what leaves the overlay); the clock and
  hibernate sections each item names.
- **Bytes:**

  | | kern_small | kern_big | kern_mega |
  |---|---:|---:|---:|
  | batch G services core | +2 (BR-43; BR-15 is 0) | +2 | +2 conv |
  | BR-159 | | +12 | |
  | WP30 | -10..+10 | -10..+10 | -10..+10 |
  | `.ovl` | 0 | **relief first**, then +29 (BR-44 +5, BR-157 +8, BR-158 +16), or +46 with gfce-6's form | as kern_big |
  | HIBER.DRV | | ~+100 (batch G) +14 (WP29) | +20 more (WP29) |
  | other modules | | DISKOPS.DRV +48 (BR-72 +30, BR-101 +18) +40 (WP29); CTRL.DRV +40..+60 (BR-26), +22 (gfce-6's form); SAVER.DRV +15 | |

- **Rows to name:**
  - fast: `drvmem`;
  - soak: `hibernate`, `hibernatedrv`, `hibernatem`, `hmahiber`, `p2hiber`,
    `megahiber`, `megap2hiber`, `stikhiber`, `ems`, `diskclone`,
    `mega-diskclone`, `imgcases`, `wimgtrip`, `cfgtrip`, `deskclock`,
    `deskclockherc`, `mega-deskclock`, `pkgmove-deskclock`, `tickzero`,
    `arclock`, `kdreturnset`, `kdos`, `saver`, `vesacp`, `lzcomp`,
    `mega-lzcomp`;
  - docs/TESTING.md list item 6's QEMU reboot assertion;
  - new: Esc on the Resume face keeps the files (BR-10); a DOS TIME read after
    a panel Set (BR-26).
- **Done when:**
  - the `.ovl` relief leaves room for the rest of the ledger;
  - batch G's services core has landed;
  - the one-clock prototype is measured, then landed or recorded.

### 3.23 S22 - HDD.DRV's kernel face (WP33, batch I's HDD half)

- **Branch:** `big-review/s22-hdd`. **Wave 6.** Cut after S13 (the A20
  include) and S14 (BR-16's 11h rule) have merged.
- **Goal:** the trust edges and transport retries of HDD.DRV (WP33 (`phase1.md:2217`),
  batch I (`bugs.md:1398`)). **The image is 4,096 bytes with 2 spare**, so hdk-11 lands
  first and pays for the rest.
- **Items:**
  - hdk-11 first;
  - BR-183 (`bugs.md:1008`): rung 0 fails a read the controller ECC-corrected, status 11h;
  - BR-182 (`bugs.md:998`)'s teardown disarm (its DSV_BLK tag is a flag-day row);
  - BR-197 (`bugs.md:1173`) (**O11** for the policy; the text is S05's);
  - BR-198 (`bugs.md:1174`) through BR-203 (`bugs.md:1179`);
  - WP33's remaining items.
- **Files:**
  - drivers/hdd/: `hdd.asm`, `hdcom.inc`, `mount.inc`, `hdtool.inc`,
    `hdsvc.inc`, `hdxms.inc` (on S13's A20 include), `inst.inc`,
    `cppage.inc`;
  - the HDDTOOL package;
  - `drivers/os88drv.inc` (a note);
  - `kernel/driver.inc` (BR-182's disarm only).
- **SPEC first:** SPEC.md 51.8.1, 52.1.4 and 117.4; SPEC 52.11.
- **Bytes:**
  - 0 kernel.
  - HDD.DRV about **-45 net** with hdk-11 (about +55 without it, which does
    not fit). Batch I's core +12 and all +58 are inside that.
  - HDDTOOL +~50 (+10 for BR-197's token form).
- **ABI / lock:** HD_ABI_VER 8 is now fingerprinted (S01). A change to the
  private HD ABI bumps that version, not PKG_FMT.
- **Rows to name:**
  - soak: `hdboot`, `hddgeom`, `hdauto`, `hdmap`, `hdsize`, `hdtake`,
    `hdtakeboot`, `hddcp`, `hdnoclaim`, `hdbounce`, and their `box-` twins
    on 86Box (the instrument; docs/86BOX.md).
  - BR-183 wants a field run (docs/FIELD-MACHINES.md): whether an XT
    controller stops at the corrected sector.
- **Done when:** HDD.DRV still fits in 4,096 bytes, with batch I's HDD core in
  it.

### 3.24 S23 - XMEM.DRV, its cells and DOS.O88's XMS (WP34, batch I's XMEM half)

- **Branch:** `big-review/s23-xmem`. **Wave 6.** Cut after S13 (the A20
  include) and S21 (batch G) have merged.
- **Goal:** ownership (WP34 (`phase1.md:2261`)). On kern_big nothing filters int 15h for DOS
  programs: rank-12 BR-180 (`bugs.md:978`), a correct pre-XMS DOS program can write the
  pool and the HMA, or shut A20. Also, the tier-2 copy never re-proves A20
  (BR-181 (`bugs.md:988`)). **XMEM.DRV is 2,041 of 2,048 bytes** (7 slack), so xm-3 and
  xm-8 compete for the same region. Price them together.
- **Items:**
  - BR-180: DOS.O88 owns int 15h, through `dos_ivtab`;
  - BR-181;
  - BR-206 (`bugs.md:1182`) (priced with xm-8);
  - BR-207 (`bugs.md:1183`), BR-208 (`bugs.md:1184`);
  - BR-211 (`bugs.md:1187`) (needs xm-8's ~-20 first);
  - WP34's -54 kern_big item;
  - BR-209 (`bugs.md:1185`) is recorded only;
  - BR-210 (`bugs.md:1186`) is a field-run prediction, not a row.
- **Files:**
  - `drivers/xmem/xmem.asm` (on S13's A20 include);
  - `kernel/xmem.inc`, `kernel/kernel.asm` (the XM mirror);
  - `apps/dos/dos.asm` (`dos_ivtab`, the 08h query, `dos_xms_fits`);
  - tests/: `vidxms.py`, `xmla.py:92`, `xmcheck.py:55`;
  - `apps/os88ub.inc` (its premise text).
- **SPEC first:** SPEC.md 41.8; SPEC.md 41.4 (the FS/GS rule goes to
  history); SPEC 96 and 117 texts.
- **Bytes:**
  - kern_big **-54**; kern_small 0 (no XMS); kern_mega 0.
  - XMEM.DRV about -17 net.
  - DOS.O88: WP34 prices it at +150..+165, and batch I at +157 (BR-180 +52,
    BR-207 +50, BR-208 +55). This plan reads the two as the same work, so
    count it once.
- **ABI / lock:** XM_ABI_VER 3 is fingerprinted (S01).
- **Rows to name:**
  - soak: `xmla`, `xmcheck`, `xmhwalk`, `dosxms`, `msegxms`, `vidxms*`,
    `ems`, `hma286`, `hmapool`, `kdos`;
  - a QEMU 386 run of xmsprobe under DOS.O88 (a 286+ is on QEMU's list).
- **Done when:**
  - an AH=87h from a DOS program cannot reach the pool or the HMA;
  - XMEM.DRV still fits in 2,048 bytes.

### 3.25 S24 - The Control Panel engine, shared module source, a scanner that sees macros (WP28)

- **Branch:** `big-review/s24-cpengine`. **Wave 6.** Cut after S17 and S21
  have merged: every earlier `ctrl.inc` and `os88ui.inc` change is in.
- **Goal:** the largest module saving on offer, plus modules sharing their
  boilerplate as source, and tools that see it (WP28 (`phase1.md:1973`)). sysmisc-8's
  Control Panel invariants become structural.
- **Items:**
  - sysmisc-15: the engine, prototyped. Risk is medium: it touches every page.
  - The shared `modhop.inc`.
  - `os88ovlchk.py` and `stkbalance.py` see macros (on S01's BR-177).
  - files-16.
- **Files:** kernel/`ctrl.inc`, `dock.inc`, `dockmod.inc`, `extmod.inc`,
  `winmod.inc`, `stikmenu.inc`, `fdlg.inc`, `filecp.inc`, the new
  `modhop.inc`, `compress.inc`; `apps/os88ui.inc`;
  `tools/os88ovlchk.py`, `stkbalance.py`.
- **Bytes:** 0 resident (-10 `.cold` kern_big after files-16). CTRL.DRV
  -1,500..-3,000 (unprototyped).
- **Rows to name:**
  - fast: `ovlchk`, `stkbalance`;
  - soak: `cppromise`, `cpup`, `cpcost`, `cpnames`, `cplistrow`,
    `cpnameshdd`, `dockmodule`, `dockmark`, `dockpos`, `extdmod`, `fdlg*`,
    `fcp*`, `btncp`, and the `mega-` twins.
- **Done when:** the prototype's CTRL.DRV figure is measured, and every page
  passes its rows' names in the report.

### 3.26 S25 - The OSCALL rewrite, the `cw_` door, V7's counter (WP32)

- **Branch:** `big-review/s25-oscall`. **Wave 7. It runs ALONE among code
  sessions**, because the rewrite touches every package and driver source.
  Cut after waves 1-6 have merged. S26 (documentation) may run beside it.
- **Goal:** take now, at no bump, the halves of flag-day rows that need no
  break, so that the flag day is a macro flip rather than a rewrite
  (WP32 (`phase1.md:2158`), `flagday.md:546`). Theme 8 (theme 8 (`systemic.md:1174`)).
- **Items:**
  - **T6's mechanism.** Evaluate first whether `os88api.inc` can overload the
    mnemonic once (`%imacro call 1` on a per-cell kind `%define`), touching
    no package source. Check that NASM does not warn under `-w+error`.
    Otherwise every `call OSAPI_X` becomes `OSCALL OSAPI_X`.
    - It must be byte-identical: `cmp` every image at one commit.
    - The source gates that parse `call OSAPI_X` (`t_textrules`, `t_movable`,
      `t_stkclass`, `t_megahigh`, `t_pins`, `t_drvclaim`, `t_toast`,
      `t_kdapi`) learn the spelling in the same commit and are broken on
      purpose.
    - A new gate refuses any surviving far call to a rare offset
      (macro-wrapped sites in `os88ui.inc`, `w16pm.inc` and `stiklib.asm`).
  - **P4: the door on the kernel's own `cw_` shims** (45 bodies; the 23
    zero-byte aliases stay far calls), with its own `retf 2` tail.
    **It needs the vector chosen first** (4.4 question 1: in 81h-FFh,
    virtualised in DOS's AH=25h/35h).
  - **V7's reading:** a counter in the rare path (one rebuild) that draws the
    hot/rare line for T5's renumber. This number decides a design, so this
    session takes it once and writes it down. Candidates: `OSAPI_SEG` (108
    sites) and `OSAPI_FS_OP` (45).
- **Files:**
  - `apps/os88api.inc` (OSCALL);
  - every package and driver source (mechanical, rewrite form only);
  - the `apps/cc` thunks;
  - `kernel/kernel.asm` (the door, the `cw_` table) and the `cw_` call sites
    in `kernel/` and `drivers/`;
  - `tests/unit/t_api_abi.py` and the gates named above.
- **SPEC first:** SPEC.md 20.3 (the call spelling; the door, as a kernel
  private).
- **Bytes:**
  - kern_big **-40..-60 `.text`** (P4; flag-day 3.1 puts the door at -45..-90
    `.text`), plus the per-site `.cold` share, unmeasured until the census.
  - kern_small and kern_mega 0.
  - Packages 0 (T6 is byte-identical).
  - +~170 clocks a call on the cold module paths P4 converts, and +150..+300
    clocks (31-63 us) a rare call once T5 shares the door (`flagday.md`'s
    corrected range; `phase1.md`'s WP32 still quotes gfr-1's +150..+215):
    both PREDICTED. A door cycle count is taken on MartyPC in the final
    pass.
- **ABI / lock:** one update for OSCALL's text, no bump. Prove the binaries
  identical by `cmp`.
- **Rows to name:**
  - fast: `api-abi`, `abilock`, plus every source gate named above;
  - the `cmp` of every image;
  - soak: `kdos`, `dosirq`, `doswmouse`;
  - the door cycle count (MartyPC, final pass).
- **Done when:**
  - every image is byte-identical to its parent for T6;
  - the `cw_` door works, from inside windowed DOS too;
  - V7's counts are written in the report.

### 3.27 S26 - Documentation: front matter, generated copies, the comment convention (WP24a)

- **Branch:** `big-review/s26-docs`. **Wave 7**, beside S25. Cut after S01
  (heading drift) and S04 (tags).
- **Goal:** every fact has one authority, and every copy is generated or
  deleted (WP24 (`phase1.md:1804`), theme 12 (`systemic.md:1633`)).
- **Items:**
  - engsys-5 with abi-10: delete SPEC 20.3's 37-entry block and 53.8's;
  - engsys-4: SPEC's front matter rewritten as current, with SPEC 3 and 15.1
    generated between markers and a gate for SPEC 4's table;
  - engsys-10 with bytes-10: AGENTS.md generated from CLAUDE.md;
  - bytes-9: KERNEL-MEMORY.md's tables emitted by `--bless`;
  - isr-16: the "callable at interrupt time" table generated from S04's
    tags;
  - the records: gkm-5's refused kernel merge (+5,440 to +14,700) into
    KERNEL-MEMORY.md; rt-4's nesting rule into SPEC.md 2.10.9 and
    MOVABLE-COLD-PLAN; gkm-8; bytes-4 into SPEC.md 2.6;
  - **WP25's convention, once**, into CLAUDE.md, AGENTS.md and SPEC.md 1: a
    header states the current in/out/clobbers/invariants, and history goes to
    the commit, the SPEC section or the report.
- **Files:** SPEC.md, AGENTS.md, CLAUDE.md, `tools/checkdocs.py`,
  `os88index.py`, `kernsize.py`; `docs/INDEX.md`, KERNEL-MEMORY.md,
  `docs/history/`.
- **Bytes:** 0. A documentation-only commit owes checkdocs alone.
- **Rows to name:** fast: `checkdocs`, `docindex`, `checkreadme`, `abilock`,
  `guidelock`; full: `guidelock-full`.
- **Done when:**
  - AGENTS.md is generated and checked;
  - every deleted copy has a pointer;
  - checkdocs is green;
  - `make help` is unchanged.

### 3.28 S27 - Documentation: SPEC's package sections split out (WP24b, engsys-18)

- **Branch:** `big-review/s27-specsplit`. **Wave 7b**, beside S28 and S29.
  Cut after S25 and S26 have merged. **No other session edits SPEC.md while
  this one runs.**
- **Goal:** SPEC.md's package sections move to `<PKG>-SPEC.md`, keeping their
  numbers, with a generated TOC (the WEAVE, C64 and ARCADE precedent). The
  heading-drift check (S01) must resolve citations into the new files.
- **Files:** SPEC.md, the new `<PKG>-SPEC.md` files, `tools/checkdocs.py`,
  `os88index.py`, `docs/INDEX.md`.
- **Bytes:** 0.
- **Rows to name:** fast: `checkdocs`, `docindex`, `checkreadme`.
- **Done when:**
  - every citation that resolved before still resolves;
  - two documents never share a basename (docs/README.md).

### 3.29 S28 - Documentation: the Makefile split (WP24c, engsys-21)

- **Branch:** `big-review/s28-mksplit`. **Wave 7b.** Cut after S25 and S26.
- **Goal:** the Makefile split by subject into `mk/*.mk`.
- **Files:** Makefile, `mk/*.mk`, `tools/os88help.py`'s inputs.
- **Bytes:** 0. Prove it by `cmp` of every image built before and after, at
  one commit, and by an unchanged `make help`.
- **Rows to name:** fast: `checkdocs`; `make help` and `os88help.py --check`.
- **Done when:** every image is byte-identical and every `#>` marker
  survives.

### 3.30 S29 - The comment discipline sweep (WP25)

- **Branch:** `big-review/s29-comments`. **Wave 7b.** Cut after S25 and S26
  (the convention).
- **Goal:** seven reviewers independently found comments used as
  archaeology: "it was X until size pass N", stale contract lines, retired
  designs narrated (WP25 (`phase1.md:1852`)).
- **Items:**
  - gfx-12, wm-15, ui-15, sched-10, text-14, video-15 (the per-area sweeps,
    each with its `doc_fixes` in `data/*.json`);
  - mem-12: about 450 lines describe ten routines that no longer exist;
  - dr-16 (it absorbs dr-6 and ul-9);
  - input-16: 22 references to the folded `mou_claim`;
  - sysmisc-12: WF_VIS for the literal 2 at ~30 places;
  - the gate: checkdocs fails a backticked kernel-comment identifier that is
    neither in the map nor on an allow-list.
- **Inside the session:** split the work by file across sub-agents (the
  story-fanout skill). Each file is one agent's. First check the doc fixes the
  review's own doc pass already applied (section 1 gives its figures), so that
  nothing is done twice.
- **Files:** every `kernel/*.inc` the sweeps name, `tools/checkdocs.py`.
- **Bytes:** 0. **Prove it by `cmp`:** every shipped image is byte-identical
  at one commit.
- **Rows to name:** fast: `checkdocs`. No emulator row can see a comment.
- **Done when:** the gate holds, and `cmp` passes over every image.

## 4. Phase 2: the flag day

### 4.1 What must be true before it starts

The flag day is one bump, `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14
(ABI-BREAK-LIST.md section 0, item 2: 11 and 12 are drivers', 13 is free for a
package, so drivers go straight to 14). It lands on one branch, with the C SDK
in the same commit and the table renumbered **once** (`flagday.md:571`). These phase-1
pieces must have merged first (`flagday.md:546`):

| prerequisite (flag-day 3.1) | why the flag day needs it | lands in |
|---|---|---|
| `t_api_abi` over all four kernels (abi-3 / gkm-2), decoding the index table and `CD nn idx` | a renumber verified on one kernel of four is not verified | S01 (the decoder); its T5 arm in P1 |
| the C SDK in `PKG_FMT`'s fingerprint (engsys-11) | section 0 item 4 says the C SDK moves in the same commit; no gate fails today when it does not | S01 |
| the VX/XM/HD private versions fingerprinted (drivers-16) | a flag day that moves a private ABI then notices | S01 |
| `API_RN_DSOFF` (abi-4) | T5 moves the frame by FLAGS | S01 |
| `@ctx` tags (abi-8) and the `APICHK=1` knob (abi-15) | the rebuilt tree is checked; the tags answer T5's IF question; the flag day's soak can run a checked kernel once | S04 |
| the API level (A4) | the only guard for hot cells after T5 | S05 |
| R9's doc half | the additive half of R9 | S05 |
| T6's mechanism, byte-identical, with the gates; V7's reading; P4's door on `cw_` | T5 becomes a flip; the renumber's sort; the door proved on kernel-private traffic | S25 |
| N1, D4 (the floor written down), histsys-7 | make D1 and drivers-9 moot without a renumber | S12 |
| C1's correctness half: BR-105 and the ~20 internal KB callers moved to paragraphs | C1's KB shifts can then go with the cells | S08 |
| L7 (the clean bit, with `os88pkg.py` and the `%error`), mc-10 | L3's staged road | S13 |
| D8 phase 1: the IMR shadow and drivers-6's scan (and the cell if O16 is yes) | lets the flag day make the cell mandatory | S06, S12 |
| F3 phase 1, the dual-mark reader | lets the flag day drop the old mark | S14 |
| shell-3 (`AH = 0FEh`), and kern_small's `VOL_STAT` tail | the additive half of C10; R9's stub | S20, S14 |
| L1's kern_small design, on paper | L1 cannot ride on kern_small without it | P0 |

Also: **the phase-1 final pass has run** (5.5), and **phase 1 has merged into
`elendilon`**. A flag day changes the meaning of every package at once.
Starting it on a tree whose own last pass is unknown leaves no baseline to
diff its soak against.

### 4.2 The sessions

**P0 - flag-day preparation.**

- **Branch:** `big-review/flagday-prep`, from `elendilon` after phase 1. It is
  a decision-and-paper session and changes no shipped byte.
- **Items:**
  - put 4.4's questions to the owner, with the numbers;
  - write L1's kern_small design: which fixed-size kern_small content fills
    the region below a shared `KERNEL_SEG`, or record that L1 stays a kern_big
    and kern_mega row;
  - re-read V7's counts from S25 (do not re-take them);
  - build the `APICHK=1` kernel into a private tree, ready for the soak;
  - confirm that ABI-BREAK-LIST.md section 13 is still present and still
    identical to `synth/abilist-section.md` (the review's doc pass appended it
    on `the-big-review`), and fold in any row phase 1 changed;
  - mark each row's status from the owner's answers.
- **Done when:** every row in 4.3 is marked taken, deferred or refused, with
  the owner's words.

**P1 - the flag day.**

- **Branch:** `big-review/flagday`, cut from `elendilon` after P0. One branch,
  one bump.
- **Follow ABI-BREAK-LIST.md section 0's nine items, in full:**
  1. the written decision (`abilock.py --update` with both bumps);
  2. the numbers that keep fencing the kinds (13 and 14; `kernel/loader.inc`,
     `apps/os88api.inc` and `tools/os88pkg.py` held by `t_mirror`);
  3. every package and driver rebuilt;
  4. the C SDK in the same commit (`os88.h`, the 63 `CC_T_*` thunks in
     `os88thunk.asm`, `crt0.asm`, the host-test stubs, `cc8086.py` if a rule
     changes), then every C package (`make cword runcpm c64 apple2 paccman
     lemmings weave loom`);
  5. The Wire's cell fence reset (SPEC.md 92.2: `WIRE_CELL_LEVELS`,
     `WIRE_TABLE_END`, `wcat.inc`'s level), with the catalogue republished on
     os8088.com with packages of the new format **in the same release**;
  6. everything that names an address by hand (`t_api_abi`, `os88sym.py`,
     `docs/INDEX.md`, SPEC.md 20.3's running count, `kernel.asm`'s length
     guard);
  7. files on disk (`SYSTEM.CFG` for F1, the dirent mark for F3, hibernate
     images, the ROMs, `COLDHMA.SYS`);
  8. every kernel priced, and `make budget-check` on all four;
  9. the soak, which is the owner's to ask for.
- **Inside the branch the order is fixed** (4.3), because the table renumbers
  once. The package-side rewrites at each step are mechanical, so a lead can
  fan them out by package with the story-fanout skill, all onto this one
  branch. The kernel-side steps stay serial.
- **Bytes** (net resident, ESTIMATED; L1's pad MEASURED today, 0-511 by
  build; `flagday.md:605`):

  | | kern_big | kern_small | kern_mega conv |
  |---|---:|---:|---:|
  | T5 (+ V7's two promotions) | -340 (-325..-385) | -340 (-325..-385) | -365 (-345..-410) |
  | L1 | -384 MEASURED pad | not taken (+924..+3,280 as written) | 0 (up to -128 if packed) |
  | R14 + K17-2 | -320 (-260..-360) | -110 (-95..-120) | -350 |
  | C1, C2, C4', R1 | -113 | -72 | -32 |
  | R2, R3, R12, R15, C10, F1, F3, V1, R2-4/5 | -14 | +6 | -24 |
  | **net** | **~-1,170 (-1,100..-1,260)** | **~-515 (-490..-570)** | **~-770 (-700..-870)** |

  Notes on the table:
  - kern_mega also drops -235 `.mega` (R2-1). L3 (-1,271..-1,627 `.mega`
    MEASURED) is out of this flag day.
  - About a third of kern_big's figure is L1's pad. With the pad at its ~256
    average, the net is about -1,040.
  - Packages: -5.3 to -5.5KB unpacked at ~2,640-2,770 rare sites (T5); about
    -1.5KB from class tables (R14); +0 to +150 for C1 with a shared helper
    (+0.9 to +1.7KB if inlined); up to +78 for R15.
  - Cost: **+150 to +300 clocks a rare call (31-63 us), PREDICTED**, and +40
    to +90 clocks a callback for R14, PREDICTED.
- **Rows to name:** `api-abi` on four kernels, `abilock`, `mirror`, `pkg`,
  `installabi`, `abicompat`, `thewire`, `wire`, and every package's rows:
  the soak, on the owner's word, ideally on the `APICHK` kernel.
- **Done when:**
  - the bump is in one commit with the C SDK;
  - every image is rebuilt;
  - `t_api_abi` passes on four kernels;
  - `make budget-check` passes exactly (the grace ends here; this is a
    release-shaped merge);
  - The Wire's generation is reset;
  - the owner has the soak's result, or has said it is not wanted.

### 4.3 The bundle, in order

1. **Withdrawals**, so that nothing renumbers twice: C1 (the six KB memory
   doors; packages convert through a shared helper), C2 (APPEND), C4'
   (APPEND_SYS alone: -17 kern_big MEASURED), R1's bytes form, R14's setters,
   and any K17-4 demotion V7's reading supports.
2. **R14**: the class table, the `WT_*` template reshape, K17-2's one
   `OSAPI_WM_FLAG`, and kern_small's dispatch-count answer (`OSAPI_WM_CREATE`
   answers how many class slots this kernel dispatches). This deletes six of
   T2-2's kern_mega ICW stubs.
3. **Re-contracts**: R2, R3, R12, R15, C10, R2-4, R2-5, V1's new rect entries,
   and L7's driver header bit. Optional: R16, and D8 made mandatory for
   drivers.
4. **T5**: sort the rare set by kind and by V7's frequency; renumber once;
   flip the call spelling's rare arm to the door; name the vector and
   virtualise it in DOS. V2 rides it. T6's renames land here, with one cycle
   of `%define` aliases.
5. **L1** on kern_big and kern_mega (`KERNEL_SEG` 0x0060 -> 0x01A0, the FAT
   window and `dsk_secbuf` below the image), after T5, so that T5's
   `t_api_abi` diff is not confounded by every address moving. kern_small
   takes it only with P0's design. kern_mega's figure counts only if its
   ladder is packed. A4's word moves with it to `KERNEL_SEG:0006`.
6. **Files on disk**: F1 (`'VC'` folded; Force Color lost once) and F3 (the
   old dirent mark dropped). kern_mega only: R2-1 (`mega_hibnc`, -235 `.mega`)
   and R2-3 (`MEGA_WTN` 32 -> 64, ~+128 of pool above 1MB).
7. **The decision and the gates**: `abilock.py --update` with both bumps;
   The Wire's generation reset at 13 and the catalogue republished;
   `t_api_abi` on four kernels; `make budget-check` on all four; then the
   soak, which is the owner's to ask for.

**Out of it** (`flagday.md:659`):

- L3, until The Wire serves kern_mega builds;
- T1/K17-1, the fallback for T5;
- K17-3, superseded by R14;
- K17-5/C7;
- T2-2's remaining four;
- C3;
- D1/drivers-9;
- D4, N1 and L7, which need no bump and are phase 1;
- R9's universal value;
- R6 with wm-R3;
- R2-6/hm-10;
- R10, R11 and L6, which have additive forms;
- R13;
- C9, D5, D6, D7, P3, P5;
- V3-V5, A1;
- F2;
- P1, P2-1.

### 4.4 Phase 2's owner questions

Questions 1-6 come from flag-day 3.5 (`flagday.md:682`); question 7 is the
one `systemic.md` leaves open that `flagday.md` does not ask. P0 puts them;
none is answered here.

1. **T5's door.** A software interrupt for the rare half, at +150 to +300
   clocks (31-63 us) a rare call (PREDICTED). Its vector is owned against
   windowed and full-screen DOS, in 81h-FFh, virtualised in AH=25h/35h, and a
   DOS program that writes the IVT directly is a residual risk. If not T5,
   then T1 is the table row. S25's P4 needs the vector answer before it
   starts.
2. **The IF rule.** Is any rare cell legitimately called with IF=0? S04's
   tags answer it. "IF=1 callers only" drops the FLAGS restore.
3. **R14 and the C SDK.** Should the C SDK emit a class table from the setter
   calls it sees, or should C packages declare one?
4. **L1 on kern_small**, and whether kern_mega packs its ladder (P0's paper).
5. **D8.** The `OSAPI_IRQ` cell now (O16), or only at the flag day?
6. **L3 as product.** gkm-9 asks whether a one-disk 8088/286 set is a goal.
   That runs against the standing rule that no release artefact carries
   kern_mega's disks.
7. **R9's "not on this kernel" value.** `flagday.md` 3.4 refuses AX=1, which
   is already a real refusal in four families. `systemic.md` theme 4
   (`systemic.md:560`) leaves AX=0 open: unused as a refusal in every family,
   about +4..+10 resident on kern_big and kern_small, but it moves XREFUSE
   and needs a census of XREFUSE's AX=1 readers. Until it is decided, only
   R9's doc half (S05) stands.

## 5. Running it as several cloud sessions

**The honest trade first.** Several sessions buy wall-clock: seven waves
instead of 29 serial sessions. They pay for it in tokens and coordination:

- Every session re-reads CLAUDE.md, this plan and its synth sections.
- Every container runs `make deps` and a full `make` of its own.
- Every merge in a hotspot file is a conflict somebody resolves.
- The owner decisions block parallel and serial runs alike.

If the owner would rather spend fewer tokens and wait longer, take the
sessions in 2.2's order in one place. 5.4 is the middle road.

### 5.1 One environment for every session

Create one personal cloud environment for the repository, named `os8088`. Do
it in claude.ai's environment settings, or ask any session to propose it with
its `create_environment` tool; the approval prompt shows the script verbatim.

- **Network:** limited, with package managers allowed. That covers `make
  deps`'s apt packages. Add a host only when a session reports a request
  blocked by the proxy.
- **Setup script.** It runs as root on every fresh container, in the directory
  that holds the checkouts, after the clone and before Claude starts. A
  failing script stops the session from starting, so every step is guarded:

  ```sh
  if [ -d os8088 ]; then
    (cd os8088 && make deps) || echo "setup: make deps failed; the session's first step repeats it"
  fi
  ```

- **Optional, only for the session that runs the final pass:** add `make
  marty`. Its prerequisite is the 360KB system image, which it builds first;
  then it compiles MartyPC, which is minutes on every fresh container. It also
  needs MartyPC's repository host (`tools/martypc/UPSTREAM`'s `repo=` line)
  and crates.io to be reachable.

  ```sh
  if [ -d os8088 ]; then
    (cd os8088 && make deps && make && make marty) || echo "setup: marty not built; build it in the session"
  fi
  ```

  Phase-1 sessions run no emulator rows, so they do not need it.
- **No secrets** in the script or in its variables.

### 5.2 Starting sessions by hand (claude.ai/code or the Claude app)

For each session in the current wave (2.3), start a new cloud session:

- the repository `Elendilon/os8088`;
- the base branch `big-review/phase1` (the orchestrator creates it; before
  that, `elendilon` if `the-big-review` has merged there, otherwise
  `the-big-review`, since this plan must be on the branch the session reads);
- the `os8088` environment;
- this first message, with the number filled in:

  > Read docs/plans/BIG-REVIEW-PLAN.md and take session S<NN>. Follow section
  > 3.1, the common brief, and then the S<NN> handoff exactly. Your branch is
  > `big-review/s<NN>-<slug>`, cut from `origin/big-review/phase1`. Push only
  > that branch. Do not merge and do not open a PR. Put your report in your
  > last commit message under `Report:` and repeat it as your final message.

Start a wave's sessions together. Start the next wave only after the
orchestrator (you, or 5.3's session) has merged the previous wave in its
order.

### 5.3 An orchestrator session that drives the others

One cloud session can run the whole phase with its remote-session tools.
Start it with:

> Read docs/plans/BIG-REVIEW-PLAN.md section 5.3 and orchestrate phase 1.

The orchestrator:

1. **Prepares.**
   - Runs `make deps` and unshallows.
   - Creates `big-review/phase1` from `elendilon` if `the-big-review` has
     merged there, otherwise from `the-big-review`, and pushes it.
   - Opens one **draft PR** `big-review/phase1` -> `elendilon`. This is the
     review thread (CLAUDE.md's waves rule 4). Its description carries
     section 6's checklist.
   - Keeps a `DECISIONS.md` outside the repository (in its scratchpad). Owner
     answers and section-number reservations go there, and are restated in
     each child's prompt, because a message to a running session can arrive
     late (the story-fanout skill's LESSONS.md).
2. **Starts a wave** with `create_session`, one call per session:
   - `source_url` = the repository;
   - `source_revision` = `big-review/phase1`;
   - `outcome_branch` = `big-review/s<NN>-<slug>`;
   - `environment_id` = the `os8088` environment (from `list_environments`);
   - `title` = `S<NN> <name>`, `tags` = `["big-review", "wave-<n>"]`;
   - `prompt` = 5.2's message plus any `DECISIONS.md` lines that bear on it;
   - `permission_mode` no more permissive than its own, and **never `plan`**:
     a child waiting for plan approval stalls for good.
3. **Watches.**
   - `get_session` per child: `status_bucket` reads `working`, `blocked`,
     `review_ready`, `completed` or `failed`.
   - `list_events` with `kinds: ["assistant", "result"]` reads what a child
     said.
   - **A child that finishes cleanly does not report back**, and a cloud
     session cannot message its parent. Its answer is in its own transcript
     and its last commit. Where enabled, a child whose turn FAILS, or whose
     worker restarts, arrives as a child-session event; a clean finish never
     does, so `status_bucket` is the check.
   - Schedule a one-shot check-in with `send_later` (about 20-30 minutes)
     rather than polling.
   - `send_message` steers a child. `interrupt_session` stops one that has
     gone off track.
   - `archive_session` once its branch has merged and the owner has seen its
     report.
4. **Merges**, in the wave's order (2.3), by 5.5's protocol. It ticks section
   6 in the PR description, and starts the next wave's children only once the
   wave has merged.
5. **Escalates.** An owner decision reported by a child goes to the owner as
   one concrete question with the child's numbers. The answer goes to
   `DECISIONS.md`.

**Limits:**

- Each container has its own CPU budget (four cores on this one), its own
  `build/`, and its own clone. Sessions cannot share a worktree. That is why
  every session has its own branch and only the orchestrator merges.
- A cold container spends `make deps` plus a full `make` before it does any
  work.
- Sessions run on the account's own limits. This plan's widest wave is five.

### 5.4 The alternative inside ONE session: the story-fanout skill

One session can run a wave as sub-agents in worktrees under the story-fanout
skill (`.claude/skills/story-fanout/SKILL.md`).

- **Mapping:**
  - the keys are the session ids (`s06`, `s07`, ...);
  - the worktrees are `~/Repos/os8088-br-<key>` on branches
    `big-review/<key>-<slug>` from `big-review/phase1`;
  - COMMON.md is section 3.1 of this plan;
  - each agent's prompt is its handoff;
  - the reserved section-number table goes in COMMON.md;
  - the log, the Monitor and the stall watchdog are the skill's.
- **Models:** the skill's rule is that cheaper models build and Opus
  verifies. This plan marks the handoffs where Opus should implement from the
  start: S06, S07, S08, S13 and S15 (fault paths, re-entry, the heap,
  descriptors, the FAT write path).
- **What it saves:** cold containers. Worktrees share one clone and one
  `make deps`.
- **What it costs:** one container's four cores for every agent's `make`.
  Hold a wave to three or four agents, or the builds contend.
- **The fan-out bins:** the skill's three bins are already drawn here.
  - Fan out: every session not marked owner-gated.
  - Owner decision: 2.6's table.
  - Defer: the waves after the current one.

### 5.5 The merge protocol, and the one final pass

**After each session's branch** (the orchestrator, or the skill's lead), in
the wave's order:

1. Merge the branch into `big-review/phase1` in a worktree with a warm
   `build/`.
2. Resolve conflicts by reading both sides, never wholesale. For
   `tools/abi.lock.json`, re-run `abilock.py --update` on the merged tree. For
   `docs/INDEX.md`, re-run `os88index.py`.
3. Run `make` (the fast tier), `make KERN_SMALL=1` and `make mega` if the
   branch reached kern_mega. Run `tools/kernsize.py` on each kernel and quote
   the transient tally on kern_big. Run `make budget-check` and record its
   result: grace is allowed until the upstream merge or release.
4. Run `python3 tools/checkdocs.py`.
5. Tick the session's lines in section 6. Push.

Two branches can each pass alone and fail together. That is why the merge is
built, and why the order is fixed.

**Onto `elendilon`** (fork rule 6): when `big-review/phase1` merges into
`elendilon` (at the end of the phase, or at a wave boundary if the owner
wants the work there sooner), the merge rebuilds and **boots once** first,
because it could change a shipped byte. `make test-full` runs once, the first
time this major work reaches the integration branch, and again only if a
later large round lands.

**The phase-1 final pass, once, after S29**:

1. Collect every **Rows to name** line from the session reports. Deduplicate
   them, and add the rows the sessions wrote.
2. Run them in ONE `python3 tools/os88soak.py start -k ... -k ...` call,
   scoped and never unscoped. Include the A/B leg against phase 1's starting
   commit; that is where WP11's, WP12's, WP16's, WP18's and WP20's
   measurements are taken. Include Bochs rows for S13, and the MartyPC door
   cycle count for S25.
3. Hold a waiting task for the whole run.
4. **Report every ~15 minutes** (fork rule 4): elapsed, reported N/M,
   ok/FAIL/SKIP, the failure names, and which of them were already failing at
   the base.
5. Fix the failures and re-run only those rows.
6. Run `make test-full` once, alone. Then `make budget-check`.
7. The whole soak tier runs only if the owner asks for it in as many words.

**The disks (fork rule 3).** After every commit that could change a shipped
disk's content, send all thirteen:

- the eleven 360KB images of `$(SHIPIMGS)`;
- `build/os8088.img` and `build/apps.img`;
- any on-demand disk whose package changed.

A child session's `build/` is in a container the owner cannot reach, so
"send" means attaching the files. O17 asks whether each child sends at every
commit or only at its last, with the orchestrator sending after each merge.
Until the owner answers, the rule as written binds every session.

## 6. Checklist

Tick a line when its work has merged into `big-review/phase1` (phase 1) or
`big-review/flagday` (phase 2), with the commit. An owner-gated line is
ticked only when it has landed, or when the owner's "no" is recorded beside
it.

**Phase 1, wave 1**

- [ ] S01 gates: WP10; BR-74, BR-75, BR-76, BR-171, BR-172, BR-177; the kernsize home tally
- [ ] S02 free bytes and `.bss`: WP1 (with BR-110), WP7; `.ovl` at 46 free, blob at 17
- [ ] S04 contracts and tags: WP14, WP19, the APICHK knob; one lock update

**Phase 1, wave 2**

- [ ] S03 boot: WP6 (BR-153, BR-154 first), BR-1, BR-14, BR-25, BR-55, BR-56, BR-63; BR-155, BR-156
- [ ] S03 BR-6 (**O1**: the 14th blob sector)
- [ ] S05 the API level (A4) and WP26's contract text; R9's doc half; histsys-17

**Phase 1, wave 3**

- [ ] S07 batch C core: BR-4, BR-173, BR-8, BR-28, BR-42, BR-47, BR-48, BR-49, BR-50, BR-176; WP21
- [ ] S07 batch C low: BR-73, BR-108, BR-109, BR-112..BR-116, BR-118, BR-119, BR-195, BR-196
- [ ] S07 BR-8's doors and BR-173 under **O3**
- [ ] S08 batch D core: BR-2, BR-7, BR-11 (with loader-8), BR-12, BR-29, BR-30
- [ ] S08 batch D rest: BR-35, BR-52, BR-53, BR-54, BR-59, BR-105, BR-106, BR-107, BR-120, BR-121, BR-189, BR-193; WP17 and C1's callers
- [ ] S08 owner items: BR-104 (**O14**), BR-188 (**O12**), BR-29/BR-30 under **O3**
- [ ] S06 the fault panel and stubs (BR-9, BR-84) (**O2**); the divide ratchet
- [ ] S06 WP13: the IF=0 rule, `t_ifwin`, IFLAT, `ifoff`
- [ ] S06 WP22's IMR shadow; BR-34's IMR read-modify-writes
- [ ] S06 batch F core: BR-18, BR-19, BR-20, BR-21 (measured), BR-36, BR-51, BR-85, BR-86, BR-178
- [ ] S06 batch F low: BR-79..BR-83, BR-87..BR-95, BR-175, BR-192, BR-194
- [ ] S14 batch E core: BR-16, BR-17, BR-22, BR-23, BR-24, BR-27, BR-37, BR-41; BR-62's stubs with BR-179; BR-183's kernel half
- [ ] S14 batch E rest: BR-61, BR-123..BR-130, BR-133..BR-143, BR-190, BR-191
- [ ] S14 BR-60 (**O13**)
- [ ] S14 WP16: FIND_SEQ, GRAIN, F3's dual-mark reader, dr-10, dr-11, dw-21, files-5
- [ ] S13 WP5 (with L7, mc-10, the A20 include)
- [ ] S13 batch H core: BR-13, BR-64, BR-67, BR-69, BR-70, BR-71, BR-184..BR-187
- [ ] S13 batch H rest: BR-65(c), BR-66's LDT half, BR-68, BR-144, BR-161..BR-170, BR-204, BR-205
- [ ] S13 BR-65 form (a) (**O15**)

**Phase 1, wave 4**

- [ ] S12 WP4 (histsys-7, N1, D4, drivers-5, drivers-14, bytes-8, isr-11, BR-145's driver half)
- [ ] S12 BR-34's vector scan; SOUND.DRV's IMR and OCW3; BR-5, BR-31, BR-32, BR-33
- [ ] S12 WP31's gates; gkm-1's two fixes
- [ ] S12 `OSAPI_IRQ` (**O16**); gkm-6 (**O10**)
- [ ] S09 drop-to-cache: rt-1, rt-9, shell-2; hm-9 (optional); kern_small arm (**O5**)
- [ ] S15 WP12: histsys-3, dw-13, dw-15, dw-16, dw-17, dw-20
- [ ] S15 dw-14 (**O9**)
- [ ] S17 batch B core: BR-3, BR-45, BR-46, BR-57, BR-174
- [ ] S17 batch B low: BR-58, BR-131, BR-132, BR-148, BR-149, BR-151; BR-147 (after **O1**)
- [ ] S17 WP20 (gfx-4, gfx-8, gfx-9, gfx-15, shell-9, os88ui:F1, cursor:mc-9) and WP23

**Phase 1, wave 5**

- [ ] S21 the `.ovl` relief (`clk_probe`'s RTC rungs out of `.ovl`)
- [ ] S21 batch G services: BR-10, BR-15, BR-38, BR-39, BR-40, BR-43, BR-44, BR-26, BR-72
- [ ] S21 batch G low: BR-96..BR-103, BR-157..BR-160
- [ ] S21 WP29 and WP30
- [ ] S16 WP18: the boost (measured), `OSAPI_TASK_WAKE`, the lock rules; histsys-5 (optional)
- [ ] S19 WP15: mem-17, text-15, text-16, hm-11, gi-3, the parts init-drop
- [ ] S20 WP27: ui-10, files-17, ui-11, shell-3, shell-8 (after **O1**); ui-12, ui-19 (optional)
- [ ] S10 FILECP/FDLG cached on kern_big (**O4**)

**Phase 1, wave 6**

- [ ] S22 WP33 with hdk-11; BR-182's disarm, BR-183, BR-198..BR-203
- [ ] S22 BR-197 (**O11**)
- [ ] S23 WP34: BR-180, BR-181, BR-206, BR-207, BR-208, BR-211
- [ ] S24 WP28: the engine prototype, `modhop.inc`, the scanner that sees macros
- [ ] S11 rt-3, the PS/2 overlay, with input-10
- [ ] S11 KBMOUSE (**O6**); text-7 (**O7**)
- [ ] S18 the KHP round (**O8**)

**Phase 1, wave 7**

- [ ] S25 T6's mechanism (`cmp`-proved), the source gates, the surviving-far-call gate
- [ ] S25 P4's `cw_` door (after 4.4 question 1)
- [ ] S25 V7's counts written down
- [ ] S26 WP24a: front matter, generated AGENTS.md, slot-contract copies deleted, KERNEL-MEMORY markers, isr-16's table, the records, the comment convention
- [ ] S27 WP24b: SPEC's package sections split out
- [ ] S28 WP24c: the Makefile split
- [ ] S29 WP25: the comment sweep and its gate

**Phase 1, close**

- [ ] every owner decision O1..O18 answered and recorded
- [ ] the deduplicated soak union, once, with the A/B leg (15-minute reports)
- [ ] failures fixed; only those rows re-run
- [ ] `make test-full` once; `make budget-check` recorded
- [ ] `big-review/phase1` merged into `elendilon` (rebuild and boot first); the thirteen disks sent

**Phase 2**

- [ ] P0: 4.4's seven questions answered; L1's kern_small design; ABI-BREAK-LIST.md section 13 confirmed and updated for phase 1; the APICHK kernel built
- [ ] P1.1 withdrawals: C1, C2, C4', R1's bytes form, R14's setters, K17-4 demotions (per V7)
- [ ] P1.2 R14, the class table, with K17-2's `OSAPI_WM_FLAG` and kern_small's dispatch count
- [ ] P1.3 re-contracts: R2, R3, R12, R15, C10, R2-4, R2-5, V1, L7's driver bit
- [ ] P1.3 optional: R16; D8 mandatory for drivers
- [ ] P1.4 T5: sorted, renumbered once, the door, the vector named and virtualised; V2; T6's renames with aliases
- [ ] P1.5 L1 on kern_big and kern_mega (kern_small only with P0's design); A4's word moved
- [ ] P1.6 F1 and F3; kern_mega R2-1 and R2-3
- [ ] P1.7 `abilock.py --update` with `PKG_FMT` 13 and `DRV_VER` 14; the C SDK in the same commit; every C package rebuilt
- [ ] P1.7 The Wire's generation reset at 13; the catalogue republished in the same release
- [ ] P1.7 `t_api_abi` on four kernels; `make budget-check` exact on all four
- [ ] P1.7 the soak, on the owner's word, on an APICHK kernel
- [ ] this plan moved to `docs/plans/completed/` with its status line updated
