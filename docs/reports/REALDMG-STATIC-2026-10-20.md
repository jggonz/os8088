# A static recompiler for RealDMG: feasible, and not where the time is

**A measurement and an argument.** 2026-10-20, a four-core cloud container,
branch `gameboy-static` cut from `gameboy` at 70ab57c, the tree this file was
committed in.
- **The 286 figures are 86Box's `vm/286-victory`**: the Victory 286's twin,
  16 MHz, through `tests/rdgcore/rdgbench.py --box 286-victory --mhz 16` and
  `tests/rdgcore/rdgprof.py --box 286-victory`. 86Box times a 286 by its own
  model and is not cycle-accurate (docs/86BOX.md). It is not the owner's iron.
- **The host figures are `tools/gbstatic.py`**, on Peanut-GB, with gbprof's
  platformer input script, frames 800 to 7,200.
- **Every figure is marked MEASURED or PREDICTED.** The predictions are
  priced at the rate section 4 measures.

The question was whether translating a cartridge **ahead of time** is
feasible beside RealDMG's dynamic recompiler (REALDMG-SPEC section 16), and
whether it would gain speed towards 30 frames drawn a second on Super Mario
Land **while it scrolls with enemies on screen**. The games are the owner's
and are not in this repository.

## 1. The answer

- **Feasible: yes.** A recursive-descent disassembler finds **99.88%** of
  Super Mario Land's busy instructions without running the game, and 100%
  of Kirby's Dream Land's and Dr. Mario's (section 2). It needs two things
  beyond a plain walk: reading the jump table after `RST 28h`, and carrying a
  constant `A` into the bank write.
- **The gain specific to translating ahead of time is small: about 0.3 ms
  of a 6.55 ms frame (PREDICTED, section 5).** Each extra thing the static
  view proves was measured against what a translator working one block at a
  time can already see:
  - **flags**: whole-program liveness proves 38.8% of flag writes dead; a
    lookahead of 16 instructions proves 35.4%;
  - **pointer regions**: whole functions prove 10% of accesses, one block
    at a time 9%;
  - **what is left**: the block lookups and the RET cache's misses, 6.9% of
    the machine.
- **The cost is large.** Code for the whole ROM is **~170 KB** for Super
  Mario Land and **~250 KB** for Kirby's Dream Land (PREDICTED, section 4).
  Today's cache holds Super Mario Land's hot code in **35.6 KB** (MEASURED).
  It also needs a translator, a derived file beside every cartridge, and the
  whole dynamic machinery kept as the fallback.
- **The time is in three short loops, not in a missing analysis.** On the
  Victory twin they are **17%** of Super Mario Land's CPU time over 12
  seconds of play, and **26%** over 3 seconds with enemies on screen
  (MEASURED):
  - 3EF9, a background-map row multiply that runs as the screen scrolls;
  - 2CF0 and 2D02, a 13-byte copy of an enemy's slot into HRAM and back.

  **Replacing one of them, 3EF9, took the CPU from 6.55 to 6.22 ms a frame**
  (MEASURED, section 3). A dynamic recompiler can recognise all three loops,
  because each one is a single block that jumps to itself.

**Recommendation: do not build a static recompiler.** Harvest two of its
analyses into the dynamic one instead, in this order:
1. **Loop idioms** (section 3).
2. **Flag liveness by bounded lookahead** at translate time (section 5.1).

## 2. Coverage: what a disassembler finds

`tools/gbstatic.py` walks each ROM from 0100h, the RST vectors and the
interrupt vectors. It follows every direct JR, JP, CALL and RST. When an RST
handler pops its own return address, it reads the bytes after that RST as a
jump table. It also tracks the ROM bank a call into 4000-7FFF lands in: a
constant `A` written to 2000-3FFF on the way there.
`tools/gbprof/gbcover.c` records every instruction that RAN, by bank.
"Busy" leaves out exactly what RealDMG skips (REALDMG-SPEC section 8); see
section 6 for why that differs from gbprof's own count.

**Coverage, MEASURED** (busy executions found by the walk; frames 800-7,200):

| game | busy instructions a frame | plain walk | with RST jump tables | plus targets seen at JP HL and RET |
|---|---|---|---|---|
| **Super Mario Land** (64 KB, MBC1) | **2,039** | 31.3% | **99.88%** | 100% |
| Kirby's Dream Land (256 KB, MBC1) | 2,266 | 100% | 100% | 100% |
| Looney Tunes (1 MB, MBC5) | 2,551 | 97.8% | 97.8% | 99.7% |
| Dr. Mario (32 KB) | 294 | 94.9% | ~100% | ~100% |

- **The jump table is the whole difference for Super Mario Land.** Its
  `RST 28h` is the classic dispatcher (`ADD A,A / POP HL / ... / JP HL`), and
  the game's state machine runs through it.
- **The bank needs `A` tracked across instructions.** Super Mario Land
  selects a bank as `LD A,3 / LDH (FD),A / LD (2000),A / CALL 47F2`. A
  pattern of exactly `LD A,n / LD (nn),A` finds 76%; tracking A finds
  99.88%.
- **What is never found.** Super Mario Land's misses are its sound driver's
  `JP HL` tables, 0.12%. Looney Tunes' are the same kind, 2.2%. Super Mario
  Land runs 3 busy instructions a frame from RAM (the OAM DMA routine), and
  Looney Tunes 45 (REALDMG-SPEC section 16.3's HRAM thunks).

**What follows from coverage:**
- Every one of these games still needs the interpreter or the dynamic
  recompiler for what the walk misses.
- A bank the walk guessed wrong still needs the run-time bank check that
  today's exits already carry (REALDMG-SPEC section 16.4).

So a static translator is an addition to the dynamic machinery, not a
replacement for it. Feasibility was never the obstacle: gb-recompiled
statically translates Game Boy ROMs to C, using a control-flow graph to get
past the indirect jumps ([source](https://github.com/arcanite24/gb-recompiled),
[a review](https://korben.info/en/gb-recompiled-game-boy-roms-native-c.html)).

## 3. Where Super Mario Land's time goes on the Victory twin

**The CPU alone, MEASURED** (`rdgbench --rom`, 1,200 frames in play, no
picture):

| | ms a Game Boy frame | share of real time |
|---|---|---|
| recompiler (Auto's choice) | **6.55** | 39.1% |
| interpreter | 10.76 | 64.2% |
| **recompiler, the 3EF9 loop replaced in a scratch copy of the ROM** | **6.22** | 37.2% |

**The profile, MEASURED** (`rdgprof --box 286-victory --samples 12000 --lead
30 --blocks`, 12 emulated seconds):

| | share |
|---|---|
| translated code | **65.8%** |
| rdgsys (events, scheduling, register writes, the raster log) | 13.5% |
| the register and MBC tails (`io_lcd` 5.4% - STAT and LY - `io_rd`, `rd_s1`, `mbc_set`) | 10.9% |
| block lookup, chaining and the RET cache (`tr_link` 1.6%, `tr_rpatch` 1.1%, `tr_ekey` 1.9%, `tr_enter` 0.8%) | 6.9% |
| the interpreter's handlers | 0.7% |

Translated code by block (share of ALL samples):

| block | Game Boy code | 12 s of play | the 3 s with most enemies |
|---|---|---|---|
| **3EF9** | `ADD HL,DE / DEC B / JR NZ,-4`, B = 32: HL = 9800h + 32 x row, the row from SCY | **8.2%** | 6.9% |
| **2CF0** | `LD A,(HL+) / LD (DE),A / INC DE / DEC B / JR NZ`, B = 13: an enemy's slot, D100h + 16n, into HRAM at FFC0h | **4.9%** | **13.4%** |
| **2D02** | the same copy back, FFC0h to the slot | 2.0% | 5.8% |
| 3EE6, 3EFD (3EF9's two neighbours) | | 1.6% | 1.5% |
| everything else | ~370 blocks, none over 2.2% | ~49% | ~47% |

**The copy loop's translation explains the cost.** One pass of 2CF0's five
Game Boy instructions is **about 33 x86 instructions**: four compares for
the read, four for the write, the guard on the store and three SAHF/LAHF.
That counts the 155-byte block in `--dump`, without its out-of-line slow
paths. With the source page and the count known, the same pass is six:

    mov al,[ss:bx] / inc bx / mov [ss:di],al / inc di / dec ch / jnz

Better still, check once that the whole range is RAM and copy it with
`rep movsb`, charging 10 M-cycles a byte.

**The A/B is one loop of the three.** Replacing 3EF9 in the scratch copy
took away 0.33 ms a frame over the whole run. The replacement is 12 bytes in
place of 13: `ADD A,C0h / LD L,A / LD H,4 / ADD HL,HL` x5 `/ LD B,0` and a
NOP. It leaves HL and B as the loop did, and the flags are overwritten two
instructions later. The patched game drew the same three screenshots as the
original (gbprof `--shots`). Run again, the original read the same 143
ticks.

- **The two copy loops predict another 0.3-0.4 ms on average** (PREDICTED,
  from their 6.9% share less the cost of a block copy).
- **They predict ~1 ms in a frame with enemies on screen**, where they are
  19% of the machine. That is the frame the target is set on.
- **A dynamic recompiler can take all three.** Each is one block that jumps
  to its own start, which is the shape `rt_jrt` already examines for a
  spin-wait. A closed form needs only what is in the block:
  - for 3EF9, HL += DE x B and B = 0, charged 6B - 1 M-cycles;
  - for the copy, a range check on HL and DE against B, then the copy,
    charged 10B - 1;
  - both only when BP covers the whole loop, as `cnt_ff` requires.

## 4. Code size: what translating ahead of time costs in memory

**MEASURED:** the cache and block table dumped out of 86Box after 50
seconds of play hold 376 blocks. That is 35,569 bytes of code for 1,757
Game Boy instructions: **20.2 bytes a Game Boy instruction**, exits
included.

**PREDICTED:**

| | Game Boy instructions | code at 20.2 bytes each |
|---|---|---|
| Super Mario Land, everything the walk finds | 8,532 | **~172 KB** |
| Super Mario Land, only what ran in 107 s of play | 4,929 | ~100 KB |
| Kirby's Dream Land, everything found | 12,469 | **~252 KB** |
| today's cache, either game | (what was entered 64 times) | **<58 KB, one segment** |

- **The field Victory had 179 KB of heap** for Kirby's Dream Land
  (REALDMG-SPEC section 10.3).
- **The code does not fit one 64 KB segment**, so it needs far exits
  between segments. Translating only the code that ran makes it a
  precomputed copy of what the dynamic cache already selects for itself.

## 5. What only the ahead-of-time view would buy

### 5.1 Flags: the lookahead gets nearly all of it

**MEASURED, Super Mario Land:** 643 flag-writing instructions a busy frame.
Each column is the share of those writes whose flags are needed for:

| | nothing | Z only | C only | Z and C | N or H (or unknown) |
|---|---|---|---|---|---|
| what happened (gbcover, dynamically) | **42.5%** | 51.1% | 6.0% | 0.2% | 0.3% |
| proved by whole-program liveness | **38.8%** | 45.2% | 5.2% | 0.9% | 9.9% |
| proved by looking 16 instructions past the write | **35.4%** | 35.2% | 5.8% | 1.6% | 22.1% |
| proved by looking 4 instructions past it | 29.3% | 25.3% | 7.3% | 3.1% | 35.0% |

No proof claimed a flag dead that the game then read. Kirby's Dream Land:
50.8% dead in fact, 49.9% by whole-program liveness, 45.5% by a lookahead
of 16 instructions.

**What it is worth (PREDICTED).** A dead flag write saves about 3-5 bytes:
- a `LAHF`;
- an `OR AH,20h` or an `AND AH,40h`;
- an INC or DEC's `SAHF` before its `LAHF`.

A Z-only write followed by its branch can take the branch on the host's own
flags, which saves the `LAHF` and the branch's `SAHF`. At the rate measured
in section 3 (translated code is ~34 clocks a Game Boy instruction for ~20
bytes, so ~1.7 clocks a byte), **all of it is 0.15-0.2 ms a frame**. The
lookahead takes ~80% of that, which leaves the whole-program view
**~0.04 ms**.

### 5.2 Pointer regions: no help

A template's `(HL)`, `(DE)` or `(BC)` access carries four compares unless
the pointer's region is known. **MEASURED**, the share of those accesses
whose region constant propagation proves:

| game | whole functions | one block at a time |
|---|---|---|
| Super Mario Land | 10% | 9% |
| Kirby's Dream Land | 30% | 29% |
| Looney Tunes | 6% | 6% |

A pointer comes from memory or walks through a loop, so analysing the whole
program adds nothing to what a block knows.

### 5.3 Lookups and returns: the static share

- **What the 6.9% is.** Block lookup, chaining and the RET cache's misses,
  which a static translator could mostly replace:
  - a return dispatched per function over its known callers;
  - a resume after an event that jumps straight back into the code.
- **What only whole-program knowledge brings is the callers.** A
  dynamically chosen superblock could lay its fall-throughs out just as
  well: 34% of Super Mario Land's 364 conditionals a frame fall through.
- **Its worth is ~0.25-0.3 ms a frame** (PREDICTED: about two thirds of the
  6.9%).

**Total for what is specific to ahead of time: ~0.3 ms of 6.55 (PREDICTED).**
- That is ~18 ms a second at 60 game frames a second.
- It is **about one drawn frame a second** at a 30-frames-a-second target,
  where a scrolling draw is ~18 ms (the bare harness's MODE 7, git log
  464eab9).

## 6. A correction to the workload report's busy count

- **The two counts.** gbprof's classifier
  (docs/reports/GAMEBOY-WORKLOAD-2026-10-19.md) counts Super Mario Land at
  **1,747** busy instructions a frame; RealDMG runs **2,039** (+17%).
- **Why.** gbprof treats any short backward loop that writes nothing as
  free. RealDMG skips only REALDMG-SPEC section 8's spin-waits and its
  `DEC A/B/C / JR NZ,-3` countdowns. So 3EF9, which writes no memory, is
  skipped by one and run by the other.
- **Which count to use.** gbcover uses RealDMG's rules, and a figure that
  prices RealDMG should quote 2,039.

## 7. Addendum: the recompile stall, and what fits in real mode

The owner reports a stall in the dynamic recompiler: the game slows to a
crawl for about 3 seconds in every 10 while it translates. A static
translation would remove that.
- **Why.** Nothing is translated during play, the cache is never flushed,
  and Auto has nothing to re-time (REALDMG-SPEC section 16.7).
- **What it does not remove.** Code the translation did not cover still
  falls to the interpreter: slow, but steady.

So if the stall cannot be fixed on the dynamic side, a static translation
**for a few known games** answers it. The speed argument of section 5 does
not.

**Memory, per bank, PREDICTED at 20.2 bytes a Game Boy instruction:**

| game | everything the walk finds | what ran in 107 s of host play | what ran at least once every 20 frames |
|---|---|---|---|
| Super Mario Land | 8,532 instructions, ~168 KB (bank 0 6,260; bank 2 522; bank 3 1,750) | 4,755, **~93 KB** | 1,928, ~38 KB |
| Kirby's Dream Land | 12,469, ~245 KB | 1,715, ~33 KB | 1,280, ~25 KB |
| Looney Tunes | 13,825, ~272 KB | 6,529, ~128 KB | 2,810, ~55 KB |

**What fits where:**
- **Every instruction the walk finds would want protected mode** (or code
  paged from XMS beside its ROM bank), as the owner expects. That count is
  an upper bound: Dr. Mario's 6,822 "instructions" in a 16 KB bank 0 are
  partly data that the walk read as code.
- **A set guided by a profile of play stays in real mode.** For Super Mario
  Land it is ~93 KB, against the 80 KB the dynamic recompiler claims today.

A profile covers only what the host run played, so the profile would have to
walk every level, and anything it missed would run interpreted.

## 8. Reproduce

```sh
export OS88_GB_ROMS=<dir>                                   # SML.GB etc., the user's
python3 tools/gbstatic.py $OS88_GB_ROMS/SML.GB              # coverage, flags, pointers
python3 tests/rdgcore/rdgbench.py --box 286-victory --mhz 16 --rom $OS88_GB_ROMS/SML.GB [--nojit]
python3 tests/rdgcore/rdgprof.py $OS88_GB_ROMS/SML.GB --box 286-victory --samples 12000 \
        --lead 30 --blocks [--dump 2CF0,3EF9,2D02]
```

The 3EF9 A/B patches a scratch copy, never the user's file:
13 bytes at 3EF0h, `11 00 00 5F 21 00 98 06 20 19 05 20 FC` becomes
`C6 C0 6F 26 04 29 29 29 29 29 06 00 00`.
