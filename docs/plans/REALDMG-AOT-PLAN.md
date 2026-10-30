# RealDMG: the full static recompiler - the plan and its stories

**The owner's goals, in order** (2026-10-25, docs/reports/REALDMG-FULLSTATIC-2026-10-25.md
section 10):
1. **No stalls, without first having to play the whole game** with Profile
   for Static on.
2. **Fit a 256 KB cartridge**: on a 286 with XMS first, on a 640 KB machine
   with no XMS if it can be had.
3. **Speed** as a bonus, from counters the game itself keeps.

Running beats stopping, so a fallback ships. One `.o88` per game and one
`REALDMG.O88` with a data file are both acceptable.

The contract is REALDMG-SPEC section 20, and it says what is built. This
file is the stories and their evidence. The study it comes from is
docs/reports/REALDMG-FULLSTATIC-2026-10-25.md, whose section 11 is the
design.

## Where it stands

Waves 0 to 4 are built, the speed rounds after them are 1 to 10, and the
correctness items they turned up are closed. What is still open:
- **The pins revisit** - after this release, at the owner's word.
- **A JR across 4000h on the 8088's plain interpreter** lands with the wrong
  DS (REALDMG-SPEC 3.1). Priced and LEFT: 3-6% of every game there for
  Looney Tunes' two JRs, which every machine with the recompiler runs right.
- **A save state with more than one bank of cartridge RAM as a heat
  source** (one bank is taken, round 10), and **a 286 speed figure with the
  100% cap lifted** (W4's two not built).
- **A cartridge whose code runs from WRAM** is interpreted (W3's GBRMW
  finding). No real game measured does; HRAM's routines are precompiled.
- **Speed**: round 10 took Link's Awakening's two (the counters' blind
  spot, megamorphic RETs); left and priced are the memory flushes and the
  event path (~2% each) and bank switches (~8% of Link's Awakening).
- **Coverage is only as good as the replays the heat came from.** Code
  reached only through a jump table is invisible to the walk, so an image
  interprets it until a heat run has entered it. Link's Awakening's
  push-the-spiked-beetle stretch, from the owner's replay (2026-10-28), was
  60 such ROM PCs entered ~3 times a frame (banks 15h and 00h). That was
  ~20% of the 86Box 286 in `rdg_aotmiss`, `tr_ejf`, `tr_ekey` and `op_*`,
  and it held the game at 82-95%. With that replay added to the heat it
  is 1 PC: 100% and 15 -> 22-23 pictures a second, `check` identical over
  its 460 frames. **On kern_mega this is answered** (REALDMG-SPEC 20.16):
  the package carries the recompiler and translates its own misses, which
  lands that replay at 99-102% with no replay in the counters. Still open
  for the machines without it - the 8088's and kern_big's packages - is
  the walk seeing these tables (a pass over `JP (HL)` sites that reads the
  table they index); only 19 of Link's Awakening's 60 PCs appear as a word
  anywhere in the ROM, so it can narrow the gap but not close it.
  **The walk's half is built (2026-10-28)**: a dispatcher reached by a JP
  out of its RST vector or by a CALL is recognised (`gbstatic.dispatcher`
  - Link's Awakening's RST 00 is `JP 2872h`, so none of its tables was
  read), a table's 0000h/FFFFh are holes rather than its end, a PARALLEL
  bank table (`LD HL,nn / ADD HL,DE / LD A,(HL) / CALL switch` then `LD
  A,E / RST`) gives each entry its bank, a table's targets are block starts
  (they never were, in any game), and the images fill after the counted
  blocks with what is ONE control transfer from them (`near_heat`, depth 3
  measured worse). Link's Awakening's walk went 1,232 -> ~40,000 block
  starts, and that is the finding: the images (10 on the host, ~7,700
  blocks) are what binds, not the walk. On the beetle replay, unrecorded,
  34,671 -> 33,235 x86 instructions a frame (the interpreter 9,318 ->
  6,544) against 25,450 recorded; the recorded replay unchanged. Of its 60
  PCs 19 are precompiled now, 22 found and dropped for room, 19 not found.

Moved off this plan: **the kern_mega arm** (20.14) is built on the
kern_mega memory branch by the session doing that rework, which keeps
merging this work in; it is not in this release.

## The shape that was built, and why it is not the study's

The study proposed a new code generator: emitted assembly, direct jumps and
a static dispatcher. What W1 built instead is RealDMG's OWN translator run on
the host under an 8086 emulator (Unicorn), over a complete list of block
starts that the walk makes. It has three consequences, and they are why the
shape changed:
- **A block is byte for byte the machine's**, so every gate on the
  recompiler (`gbcpu`, `rdgstatic`, the harness's event counts) is a gate on
  the precompiled code. A second generator would have needed its own.
- **The runtime is Static's** (REALDMG-SPEC 16.8): a fixed table,
  `tr_enter`, `tr_link`'s lazy chaining, the RET caches. It is already in
  the package and already measured, and nothing new is resident.
- **The memory is the recompiler's segment layout**: 64 KB segments with the
  tails copied in, and the 64 KB table. That is what W2 has to shrink.

## Stories

### W0 - the contract and the instrument

- [x] REALDMG-SPEC section 20, written before the package work.
- [x] `tools/gbaot.py cover`: the walk, the `JP HL` table rule and the block
  starts, with no play.
  - Measured: Super Mario Land 99.88% of busy execution found, Kirby's Dream
    Land 100%, Looney Tunes 2.17% missed -> 0.30% with the table rule.
- [x] The RAM idioms: HRAM's routines translated ahead of time behind a
  guard (REALDMG-SPEC 20.5) - built after W4, below; what was taken for the
  OAM DMA routine was Looney Tunes' `LD (DE),A / RET` thunk.

### W1 - the host translator and its proof

- [x] `tools/gbaot/aothost.asm` + `tools/gbaot.py build`: the whole
  cartridge translated in rounds and bank groups (REALDMG-SPEC 20.4).
- [x] The cut where another block begins, with no event check
  (REALDMG-SPEC 20.3).
- [x] `tools/gbaot.py check` and `tests/gbaot.py`: equivalence against the
  interpreter, two negative controls (REALDMG-SPEC 20.6).
- [x] Evidence on the owner's games (not committed). The precompiled code
  matches:

  | game | blocks | code | segments | result |
  |---|---|---|---|---|
  | Super Mario Land | 1,874 (25 dropped) | 178 KB, 20.9 B/instr | core + 3 | identical to the interpreter, 3,000 frames |
  | Kirby's Dream Land | 2,900 (251 dropped) | 227 KB, 18.2 | core + 4 | identical to the interpreter, 3,000 frames |
  | Looney Tunes (.gb, 128 KB) | 3,757 (304) | 358 KB, 19.5 | core + 6 | identical to the dynamic recompiler, 3,000 frames; to the interpreter until frame 1178, the interpreter's own seam defect - fixed since (REALDMG-SPEC 3.1), and identical to it now |
  | Looney Tunes (.gbc, 1 MB) | 4,186 (463) | 387 KB, 19.1 | core + 8 | the same |
- [ ] Speed with the CPU alone. MOVED to W3, where it is measured in the
  package on 86Box's `286-victory`: the bare harness has no room for a
  256 KB cartridge's segments.

### W2 - memory

The full form is ~20 bytes a Game Boy instruction, which is the study's
prediction confirmed. What decides the fit is the SEGMENTS: on top of the
code, each holds 7.4 KB of tails and the gaps rounding leaves.

- [x] The table: 64 KB -> 16 KB plus a sorted list the miss path searches,
  and every exit linked on the host (REALDMG-SPEC 20.9). No block is
  dropped; the four games are still identical for 3,000 frames.
- NOT TAKEN: the cut's exit carried on into the next block with no exit,
  and the resume through a CONTINUATION (study 4.3). Measured: the cut's
  exits are ~256 (SML) to ~398 (Kirby) blocks, 5-8 KB of code - worth less
  than the measurement.
- [x] Decide: does Looney Tunes fit at full speed? Decided in W3 on the
  Victory: 2 of 8 images beside 10 slots, 100%, and the hot images hold
  all but 1-10% of the play (round 6) - so no compact grade.

### W3 - `GAME.O88`

- [x] `realdmg.asm`'s `RDG_AOT` arm and `gbaot.py package` (REALDMG-SPEC
  20.10): the parts, the header check, the ROM opened from beside the
  package, the images claimed before the ROM and given back to fit, and what
  does not fit interpreted. `tests/gbaotpkg.py` on MartyPC's 5150, verified
  red with the attach taken out.
  - Measured, 640 KB 8088 (MartyPC, no input, the title screen): Super
    Mario Land's package holds 2 of its 3 images and runs 11.9 game frames a
    guest second against the stock recompiler's 12.9; the picture is half
    the machine there (49% of the samples in the package's own code), the
    precompiled images 26%.
  - Found: a cartridge's code in WRAM is interpreted (GBRMW.GB's loop is at
    C8xx: 9 frames a second against the recompiler's 29). Real games keep
    only HRAM's OAM routine there; W0's RAM idiom is the answer if one
    does not.
- [x] The miss log: built in W4, beside the counters it is one of.
- [x] Measured on `286-victory` (86Box, holding Right in play, 30 s) and a
  640 KB 8088 (MartyPC):

  | | images kept | speed | worst second |
  |---|---|---|---|
  | SML, Victory, REALDMG.O88 | - | 100% | - |
  | SML, Victory, precompiled | 3 of 3 | 100% | - |
  | Kirby, Victory, REALDMG.O88 | - | 100% | 89% (warming up) |
  | Kirby, Victory, precompiled, 8 slots first | 1 of 4 | 17% | 10% |
  | Kirby, Victory, precompiled, images first + filter | 2 of 4 | 100% | 95% |
  | SML, 640 KB 8088, precompiled (title, no counters) | 2 of 3 | 11.9 fps | - |
  | Looney Tunes (.gb), Victory, REALDMG.O88 | - | 98.8% | 75% (translating) |
  | Looney Tunes (.gb), Victory, precompiled + counters | some of 7 | 100% | 99% |

  The 17% was two defects, both fixed (REALDMG-SPEC 20.9, 20.10): the plan
  traded images for slots, and every interpreted jump searched the list.
  On the Victory both packages hold 100%; the precompiled one translates
  nothing, and its worst second is 95% where the recompiler's is 89%.
- [x] `.o88`s of the owner's games for the owner to test (not committed).

### W4 - counters and the bundle

- [x] Per-block counters and promotion in the host build (REALDMG-SPEC
  20.11): counted EXACTLY in the harness, over its script or a replay the
  owner recorded, so nothing in the package counts and no NAME.RDH is
  needed - the replay RealDMG already writes is the input. SML's hot code
  went from four images to one.
- [x] The miss log: the same run's ROM PCs that went to the interpreter,
  added as block starts.
- [x] `make gbaotbundle` (REALDMG-SPEC 20.12): the window, the tools and
  the prebuilt binaries; needs Python and Unicorn only.
- [ ] Not built: a state with cartridge RAM as a heat source, and a 286
  speed figure with the 100% cap lifted (both packages already hold the
  Game Boy's own speed on the Victory). The RAM idiom is built (W0).

### After W4 - the field and the merge

- [x] The owner's 286 (Packard Bell Victory, 4MB, `.cold` in the HMA):
  Kirby's Dream Land `P1/4` and Super Mario Land `P3/3`, "the best it has
  ever played". Two field defects found on the way and fixed (REALDMG-SPEC
  20.10): image reads refused on a hard disk's clusters, and four images
  starving the bank cache (13,705 banks fetched in 100 s).
- [x] The ROM inside the package (REALDMG-SPEC 20.13), under 1MB (32MB since 20.17).
- [x] Merged with the kern_mega RealDMG work: the shipped REALDMG.O88 and its
  kern_mega arm are byte-identical to elendilon's; the precompiled gates
  pass on the merged core.
- [x] kern_mega's arm (REALDMG-SPEC 20.15, built on `mega-dmg` once the
  memory rework's MC7 landed): `gbaot.py --mega` packages realdmg.asm as
  RDG_AOT + PKG_MEGA, every image in one pool block and the banks in place;
  the plain package still refuses there (20.14). `tests/rdgaotmega.py`.
- [x] Speed, round 1 (86Box `286-victory`, 580K heap, Looney Tunes (GBC)
  holding Right through the stretch at 20-38 s that saturates both arms;
  `tests/rdgcore/rdgwin.py --profile` now charges samples to the
  precompiled images and their blocks):

  | | images | 25 s | 40 s |
  |---|---|---|---|
  | REALDMG.O88 | - | 89.3% (23% at worst) | - |
  | precompiled, as shipped | 1 of 8 | 97.2% | - |
  | ...images claimed first, from the ceiling, the load's hold let go | 2 of 8 | 95.5% | 91.5% |
  | ...the misses walked, their heat their blocks', counted on the stretch | 2 of 8 | - | 92.5% |

  The seconds inside the stretch move by +-15 points between two runs of
  one package, so the image fix is NOT a measured speed-up here: this
  scene's hot code was in image 0 all along. What it buys is the cold
  code a different scene runs, which the stretch does not exercise.

  Found and fixed on the way (REALDMG-SPEC 20.3, 20.10, 20.11): the plan
  gave every KB to bank slots before a cold image; two of our own claims
  held mid-heap split the slots' run; a block at FF:3FFE across the bank
  boundary that a miss made a start HUNG the promoted build after frame
  3,000 (the packages sent before it carry that block); a miss's entries
  never reached its block, and a miss the first walk had not reached was
  dropped. Kirby on the owner's replay keeps 99.5% (10 slots, 4 fetched).
- [x] Speed, round 2 - the translator, shared with REALDMG.O88. The stretch
  is the game's object loop at FF:3E44/3E52 (32 bytes to HRAM and back with
  `LD (FF00+C)`, eight objects a pass): 17% of the machine on BOTH arms,
  ~22 x86 instructions a pass of 5 Game Boy ones. What carries it: the
  `LD A,(HL+)` region compares, the HRAM store's guard, and the flags'
  `sahf / dec / lahf / or ah,20h / sahf` round trip on every DEC whose Z
  the branch reads. Kept in x86 form through a loop and made AH only on its
  exit, that last is ~5 instructions a pass. Built in round 4 below, for
  one of the two loops: the other stores before anything writes F.
- [x] HRAM's routines precompiled, guarded (REALDMG-SPEC 20.5). FF:FFE9 was
  not the DMA routine - whose wait `cnt_ff` already collapses (8.1) - but
  Looney Tunes' `LD (DE),A / RET` thunk inside the object loop. The owner's
  replay (the first half of level 1), Victory, 60 s: REALDMG.O88 71.3%,
  precompiled counted on it 85.2%, with HRAM 88.3%.
- Measured for round 2, the harness's exact block counts over three games
  (Looney Tunes on the owner's replay, Kirby on the owner's, SML on the
  script): conditional transfers are 14.9 / 15.3 / 15.8% of the
  instructions run, and 84 / 63 / 92% of them come straight after an ALU,
  INC or DEC (`CP n` first, then `AND A`, `DEC B`) - generic. What the
  translator could take out of such a pair under the rule that F is exact
  wherever an interrupt can land (16.1.2) is the branch's `sahf`, one
  instruction of ~10, for 3-6 bytes of code a pair: about 1%. The bigger
  saving - no `lahf`/fix-up at all when both successors overwrite F - is
  refused by that same rule, since an interrupt is taken at the branch and
  its handler may PUSH AF. Copy loops are NOT generic: 16.5% of Looney
  Tunes' instructions, 2.6% of SML's, 0.3% of Kirby's.
- [x] Speed, round 3 - two more loop idioms, generic, in BOTH arms
  (REALDMG-SPEC 16.6). Fourteen more cartridges were surveyed for loop
  shapes before anything was written, and the two that recur are a byte
  FILL (`LD (HL+),A / DEC r / JR NZ`) and a 16-bit COUNTDOWN (`DEC BC` or
  `DEC DE`, then `LD A,B / OR C / JR NZ`) whose body is a delay, a copy
  or a fill. `fill_ff` and `w16_ff` take each in one step, `w16_ff` in
  PIECES - k passes to the next taken branch, charged exactly - so a
  65,536-pass delay is never a stall. x86 instructions a frame, the
  harness's exact count: Metroid II -23%, SML2 -25%, SML3 -6% (fill);
  Zelda LA -13%, Tetris -9%, DKL -3.5%, Pokemon Red -2% (w16). Both live
  in the core's tail pages, so neither costs a precompiled image a byte.
  GBRMW grew a case for each, and its verdict is now checked against the
  interpreter separately from the frames, which a NOP delay legitimately
  moves (SPEC 20.6). Measured and NOT built: reordering the memory
  checks (~1.5%) and denser image code (~60% of block bytes are outside
  templates, ~4% to win). The survey precompiled 21 of 23 cartridges;
  on the way it found the DYNAMIC recompiler diverging from the
  interpreter on Pokemon Gold (frame 2) and Yellow (frame 4) - the NOP
  delay's timing, CLOSED in round 5.
- [x] Speed, round 4 - round 2's flag round trip and round 3's three
  measured leftovers, all in the translator and so in both arms
  (REALDMG-SPEC 16.1.3, 16.1.4). A countdown loop closing its own block
  writes its DEC bare and makes F only at its exits and its event; every
  other conditional transfer after the ALU or INC/DEC r branches on the
  8086's flags with no SAHF; a read's region check is one TEST shorter;
  and an exit's event way is a near jump to one shared `jmp far`. 8086
  instructions a frame -0.7% to -6.1% over twelve cartridges, the
  precompiled code 2.3-3.7% smaller, and every one of them, both arms,
  1,500 frames identical state for state to the tree before. Looney Tunes
  on the owner's replay -3.7%. `gbcpu`'s new leg holds the loops to the
  interpreter under a timer interrupt landing at their JR. Two things the
  rounds found that are NOT this work's: both Looney Tunes, Donkey Kong
  Land and Pokemon Red part from the interpreter at frames 1178, 1178, 5
  and 2 at the tree before this round as well, so the cross-check here is
  against the tree before rather than the interpreter. Since read: Looney
  Tunes' is the seam (fixed, REALDMG-SPEC 3.1), the other two the NOP
  delay's timing (20.6).
- [x] Speed, round 5 - the copy through (C), `cpc_ff` (REALDMG-SPEC
  16.6): `LD A,(HL+) / LD (C),A / INC C / DEC B` and its reverse, RAM to
  HRAM and back, in one step when the whole copy fits before the next
  event. It is Looney Tunes' object copy and every cartridge's boot copy
  of its DMA routine. Looney Tunes on the owner's replay 29,256 -> 28,288
  x86 instructions a frame (-3.3%), identical to the interpreter over
  1,500 frames in both arms with the NOP check out; twelve other
  cartridges state-identical to the tree before in both arms, their
  title-screen counts unmoved. `gbcpu`'s "copies through (C)" leg is the
  gate, `RT_CPCBUG` its control. Two things found on the way:
  `tools/gbaot.py` had been loading its driver 14 bytes into the core's
  first page (REALDMG-SPEC 20.1, now refused), and the round-3 survey's
  Pokemon Gold and Yellow divergences are the dynamic arm's NOP-delay
  timing, as Donkey Kong Land's and Pokemon Red's are - with the NOP
  check out both run 1,500 frames identical to the interpreter, and their
  precompiled arm is identical with it in. That item is CLOSED.
- [x] Speed, round 6 - the cold code (REALDMG-SPEC 20.9, 20.10): an
  interpreted jump under a precompiled machine asks the list's filter
  first, which now names the table's blocks too, and a PC with no block
  is dispatched in place instead of after the table, its neighbour and
  the list. With no image in memory, Kirby's replay 109,217 -> 83,139
  x86 instructions a frame (-24%) and Looney Tunes' 94,389 -> 73,436
  (-22%); with every image in, the cartridges the walk does not wholly
  find gain too - Super Mario Land 3 -19%, Kirby's Dream Land 2 -18%,
  Link's Awakening -8%, Donkey Kong Land -3% - and the rest pay +0.04%
  to +0.54%. Twelve cartridges state-identical in both arms to the tree
  before; Super Mario Land and Kirby identical to the interpreter at 0
  and 1 images, the Looney replay at 0 and all. The gate cannot see a
  filter that turns a block away (it is interpreted the same), so
  `tests/gbaot.py` counts blocks entered instead, and its control
  (`RDG_NOTABFILT`, the table left out of the filter) enters 22% fewer
  on GBBANK. Measured and NOT the case: with the images ordered by the
  owner's replays the cold code is 1-10% of the play at one image and
  under 1.3% at two, so on a heap that holds an image what is left is
  the interpreting itself, not the lookup.
- [x] Speed, round 7 - the list's search cached (REALDMG-SPEC 20.9): 256
  entries after the filter hold each slot's last answer, a block or none,
  so a dynamic dispatch the table cannot hold costs ~15 instructions
  where the binary search cost ~280. Walk-ordered images: Super Mario Land
  2 -30%, Pokemon Red -24%, Metroid II -11%, Looney Tunes -7% to -10%,
  Kirby -9%, nothing worse; replay-ordered, Looney Tunes and Kirby -1.8%
  and -2.0% - which is the case for it: the hot table only knows the play
  it was ordered by, and this does not. 2KB beside the filter, after the table in the table's part (it was the
  meta part's first, and that broke the packaging of three big cartridges,
  fixed). Twelve
  cartridges state-identical in both arms. Measured and NOT built first:
  item 5 of the open list, the loop idioms in pieces - real cartridges
  decline at most ~7 passes a frame (Wario Land's fills, Donkey Kong
  Land's compare countdowns), under 1% anywhere; the gate ROM's 17,000
  declines were its own timer's.
- [x] Correctness: Prehistorik Man and the JRs across the bank seam
  (REALDMG-SPEC 3.1, 20.2). Prehistorik Man was the HARNESS's: its frame
  ran until a VBlank, and the game keeps the LCD off waiting for every
  button to be let go, which the script's held Right never does - a frame's
  time is a frame with the LCD off now, and it is identical to the
  interpreter in both arms (so is the bad Super Mario Bros. Deluxe dump,
  with the NOP delay's check out). The seam: a JR at 3FFF is the trap and
  `seam_jr` runs it whole; a JR across 4000h from either side is put right
  by `tr_int` on the dynamic recompiler and by `tr_ejf` itself on a
  precompiled one, which round 6 had got wrong. Left on the 8088's plain
  interpreter, priced: 3-6% of every game for Looney Tunes' two JRs.
- [x] Speed, round 8 - a fresh profile of the owner's Looney Tunes replay,
  replay-ordered, every image in (21,233 x86 instructions a frame): blocks
  68%, the core's helpers 16%, rdgsys 15%, and nothing above 3.5% in one
  place. In the blocks, conditional jumps are 18%, flag handling 12% and
  the time charges 9%; in rdgsys, I/O writes ~1,400 a frame (14 writes at
  ~100) and events ~1,300 (6.2 at ~210). Taken: the sound registers rdgsys
  only stores, written through (C) or (HL), are stored by the core
  (REALDMG-SPEC 17) - Looney Tunes' replay 21,233 -> 20,676 (-2.6%), the
  script -3.6% (.gbc) and -4.5% (.gb), Tetris -1.5%, the rest within
  +-0.4%; twelve cartridges state-identical in both arms, `gbfm` red with
  the table wrong. Priced and left for later rounds: a memory access's
  own time charge (~650 a frame not beside an event check, ~3%), the event
  path's ~210 instructions (~2%), RET's misses past its inline cache.
- [x] Speed, round 9 - the small items round 8 priced, each measured before
  any was built. RET past its inline cache: 2 repatches and 3 `tr_link`s a
  frame, ~150 instructions - CLOSED, nothing to win. What the dispatch
  count was really made of is calls into HRAM: ~20 a frame through
  `tr_enter` and the table, which the host now links straight to the
  guarded HRAM block (REALDMG-SPEC 20.5) - Looney Tunes' replay 20,676 ->
  20,057 (-3.0%), identical to the interpreter. NOT TAKEN, priced: a memory
  access's flush of the pending charge, ~550 a frame (2.6%), because only
  its slow path reads the time and saying so costs every memory template
  a `sub` and an `add` in that slow path (bigger images on a heap that
  holds two) or a contract across every slow helper; and the event path,
  ~210 instructions an event over a dozen small routines, ~1,300 a frame
  with no single waste.
- [x] Speed, round 10 - the owner's replay of Link's Awakening's first room
  (1,128 frames from ZELDA.SS1, its one bank of cartridge RAM now taken by
  the harness), 32,133 x86 instructions a frame: blocks only 42%, and the
  rest two things. **The counters were blind past a frontier**
  (REALDMG-SPEC 20.11): a block dropped from the image they ran on was
  interpreted, and the code it reached after it was interpreted with no
  miss to count, so a hot routine reached only from cold code stayed out of
  the next build too - 37 interpreter entries a frame, and `rdg_aotmiss`,
  `tr_link`'s failed lookups and the interpreter ~25% between them. The
  heat run now counts every instruction the interpreter dispatches: 24,180
  (-24.7%). **A RET returning to more than two places repatched its site
  at every return** (REALDMG-SPEC 16.4.1): the bank switcher at 07B9, 20.5
  of a frame's 28.7 repatches at ~110 instructions each. A site patched six
  times while it holds a slot of `S_RMEG` becomes a lookup: 22,202 (-8.2%,
  -30.9% for the round), blocks 64% of the frame. Both identical to the
  interpreter over the replay and 3,000 script frames; the RET change across
  thirteen cartridges and both recompilers state-identical, +1 instruction
  to -4.8%. `gbaot.py check` takes `--replay`/`--state`. Left, priced: bank
  switches ~8% of the room (07B9's `ld (2100),a` through `wr_slow`, `mbc_set`
  and rdgsys, ~70 instructions each) and the megamorphic lookups themselves
  (~35 a return, 26 a frame).
- [x] Speed, round 11 - the owner's log of the same room on the Victory 286
  (`ZELDA00.LOG`: one code image, 9 slots over XMS, 45-69% of real time)
  said what instruction counts could not: **210-255 banks fetched a
  second**, the Game Boy at 850-950 ms of every second. The room switches
  banks 68 times a frame across 13 banks, so 9 slots fetched 8.7 a frame
  whatever the eviction (random 9.6, true LRU 10.1) - but it READS 6-7 of
  them, and bank 19 is switched to 7,896 times and never read. **A bank is
  switched to lazily now** (REALDMG-SPEC 10.3.2): translated code's switch
  leaves `S_ROMX` 0 and every reader fetches first. Over both replays at 9
  slots, every frame the interpreter's: **9,816 fetches to 247** and **11,935
  to 979** (0.2-0.4 a frame); at 5 slots, 35,051 to 8,055 - the slots still
  matter, and a second code image (64KB, four slots) bought ~1% of the
  instructions here. Found on the way, in this work's own first build: a
  fetch let `rdg_setbank` point the interpreter's DS at the bank from a
  stale `S_PC`, which the harness hid because its slots sat over bank 0's
  copy; both are fixed, and GBBANKRD's two controls are the gate - and a
  third that moved there: round 6's `RDG_NOTABFILT` control measured the
  interpreter's jumps into table code on GBBANK, whose interpreter ran only
  through the eager switch this took away, so it counts GBBANKRD's
  interpreted jumps to a table block now (841 entries against 1). The
  package's own move (`rw_reloc`) names the new vector, `S_RXVEC`, as it
  names `S_MBCVEC`: `rdgclose`'s move leg found it unnamed. Thirteen
  cartridges and Link's Awakening are each identical to themselves with
  every bank in and with 6-9 slots, under both recompilers.
- [x] Round 12 - the side features on the heap round 11 made fast. On the
  owner's Victory with Link's Awakening at 80-100% of real time, Record
  Replay and Log both refused, *not enough RAM*: the plan gives the bank
  cache every KB past `RW_KEEPKB` and the code images take theirs first.
  Keeping room for them would cost a slot for the whole session, so a side
  buffer the heap cannot hold now lives in BANK SLOTS LENT for it while it
  is held (REALDMG-SPEC 10.3.3) - cut from the cache's top slots, never
  below four, every slot the cache's again with the last buffer. The first
  build resized the slots' claim through `OSAPI_MEM_REGROW` instead, and the
  86Box run found why that is wrong: a refused claim compacts the heap, the
  cartridge's RAM moved up against the slots, and they could never grow
  back - four slots for the rest of the session. Measured on `286-victory`,
  heap full to the byte with the Task Manager open: replay and log on one
  lent slot, all three files written, the replay identical to the
  interpreter on playback, 9 slots again after. `tests/rdglend.py` is the
  row; `-DRW_NOLEND` its control.
