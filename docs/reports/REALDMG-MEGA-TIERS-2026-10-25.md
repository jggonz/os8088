# RealDMG on kern_mega: more recompiler tiers in the heap the cartridge left (2026-10-25)

A MEASUREMENT, true of the trees named below and of no other (docs/README.md).
Base: `mega-dmg` at `c8df6b4c` (docs/reports/REALDMG-MEGA-2026-10-25.md's
tree). After: the commit that adds this file. Box: a four-core cloud
container; Bochs 2.7 (Ubuntu's) and 86Box at `tools/86box/UPSTREAM`'s pin
with this tree's patches. The cartridges are the owner's and blargg's
`cpu_instrs` (`tools/getgbtest.py`); none is committed. The contract is
docs/REALDMG-SPEC.md section 16.2.5. No kernel byte changed.

## 1. What the arm holds in conventional memory

kern_mega's arm keeps the cartridge above 1MB (REALDMG-SPEC 10.3.1), so the
heap it used to give the bank cache is free once a game is loaded. The base
spent none of it on the recompiler beyond its two cache segments:

| | base | after |
|---|---|---|
| block table (64KB) | in the cache's claim | **an extended-pool block**, `OSAPI_XMEM_RUN`'s selector |
| cache segments, 64KB each | 2 (cache A, cache B) | **4** (A, B and two extra tiers) |
| tiers in the round robin (16.2.3) | 3 (A, the core's holes, B) | **5** |

Measured by reading `[S_JITXN]` out of the view: **2 extra tiers on both
Bochs (`make megatrap`, 640KB conventional) and 86Box `286-mega` (the
owner's mr286, 640KB conventional, 4MB)**, with `RW_XRESKB` = 32KB left free
beside them. Before the table moved, the same rule found room for ONE
(both machines).

## 2. Correctness (Bochs, `tests/rdgmega.py`)

| leg | build | cpu_instrs | extra tiers | tiers recycled | traps in the core or a cache |
|---|---|---|---:|---:|---:|
| arm | `make mega`'s | passes all eleven | 2 | 0 | 0 |
| squeezed | the arm with `RT_SQUEEZE`, every cache segment ~0.5KB | passes all eleven | 2 | 6 | 0 |
| squeezed, `tr_lpx` broken (`mov es,dx` first) | links into an extra tier written through its code selector | passes - by luck | 2 | 6 | **24, the row RED** |

The squeezed leg's first turn fills all five tiers fresh, so six recycles
is every tier - both extra ones - written, recycled with its links undone,
and written again. The broken build's 24 are completed #GPs at CS `0FEC`,
an extra tier's code alias: each pushed a frame onto the Game Boy's stack,
which cpu_instrs happened not to read back, so the trap census is what goes
red and not the serial line.

## 3. Speed

Not measured as a change: no scripted play here reaches the thrash. 86Box
`286-mega`, Kirby's Dream Land:

| run | base | after |
|---|---|---|
| 60 s walking right in the first level | 361-414 blocks, 0 recycles, 100.5% | 0 recycles, 100.6% |
| 240 s of the attract, no input | 460 blocks, 0 recycles, 100.1% | 444 blocks, 0 recycles, 100.1% |

Both are inside cache A. The thrash the owner reports comes from playing
further into the game than either run gets. On the base the arm's capacity
was cache A + cache B + the core's holes (~140KB of code); after, it is
~270KB. Whether that holds all of Kirby's Dream Land's code is for a field
play to say: the F1 overlay's flush count, and `[S_JITRCY]` in the view,
are the readings.

### 3.1 The owner's replay: the first level and its miniboss

The owner recorded a flyover of the first level and the first miniboss
(slot 1's state and `KIRBY.RP1`, 4,047 frames) and reported it thrashing "a
couple of times on big, some of it bank thrashing". Played back on 86Box
`286-mega` with `tests/rdgcore/rdgwin.py --replay SS1,RP1 --press w3000,f7
--secs 70`, the three builds side by side:

| build | blocks translated | bytes of code | tier switches | recycles | banks fetched |
|---|---:|---:|---:|---:|---:|
| shipped, Cache: 128K, ON KERN_MEGA (the bank cache, 8 slots) | 991 | ~137KB | 2 | 0 | 7 |
| arm, base | 979 | ~134KB | 2 | 0 | 0 |
| arm, after | 991 | ~137KB | 2 | 0 | 0 |

The play fills cache A, the core's holes and about half of cache B. The
three tiers hold 162,036 bytes between them (57,584 a cache segment, 46,868
in the core's holes, off the templates' free ranges), so the base arm is at
84% at the miniboss and recycles a little further on; after, the same play
uses 49% of 277,204. Every second after the state's load runs at the Game
Boy's rate on all three. (The after run's first second reads 3,583%: the
state's load set the frame counter inside that second, so its 148% overall is
not a speed.) The bank fetches, 7 in the span, are all the shipped build's -
the arm reads no bank at all.

(The owner's "big" was the shipped build on KERN_BIG, not this row's
kern_mega: 3.2 is that run.)

### 3.2 The same replay, logged, on the owner's two kernels

The owner reported, on the same mr286: the shipped build on **kern_big** with
Cache: 128K *"would slow down to ~30% speed for ~2 seconds, twice"*; the arm
on **kern_mega** *"mostly 100%, but speeding up, then slowing down"*, 80-115%
in fall-behind-catch-up bursts, 5-7 fps on the F1 line in the long flying
section. Taken here with Machine > Log on before F7, read with
`tools/rdglog.py KBIG.LOG KMEGA.LOG`, which lines the two up at PLAY+:

| from PLAY+ | kern_big, shipped, 128K | kern_mega, the arm |
|---|---|---|
| mean speed, seconds under 70% / under 90% / over 105% | 89%; 6 / 34 / 7 | 97%; 2 / 9 / 11 |
| the long flying section (seconds 13-49) | 76-93%, owed pinned at 30, 3-4 fps drawn | 88-111%, owed 12-30 and catching up, 3-7 fps drawn |
| slowdowns | second 11 (47%) and 57-59 (47-55%) | second 11 (27%) |
| what 57-59 is | a tier recycled (`fl` 1) and 48, 71 and 198 blocks translated again | - |
| banks fetched in the replay | 0 | 0 |
| the replay's 4,047 frames took | 76 s | 69 s |

Both of the owner's readings are reproduced: kern_big's second slowdown is
recompiler thrash (its three tiers are 162KB, and the play needs more by
then), and kern_mega's bursts are the flying section at the machine's
limit - the Game Boy's milliseconds are ~650-740 a second there and the
drawing's ~170-280, so a second that draws a few more lines falls behind
and the next catches up. Second 11 is the same moment on both and is not a
cache event (no flush, no fetch): a scene's change the machine cannot do
at full rate either way. The kern_big log's Game Boy milliseconds run
~40-80 higher a second through the flying section, where its bank cache
looks a bank up on every switch and the arm's banks are in place - the
likely reason it never catches up there, not yet measured as such.

### 3.3 A defect the logged run found, fixed

With the log on, the arm's F7 said **"Replay: no room" with 91KB free**:
`MEM_OWNER_MAX` is 8 claims a package, and the first build took one claim
per extra tier - seven after the load, the log's the eighth, the replay's
refused (compacting and shedding the read-ahead on the way, as a refused
claim does). The extra tiers are now cache B's own claim grown with
`OSAPI_MEM_REGROW` (REALDMG-SPEC 16.2.5), so the arm holds the records the
base did; the same steps play the replay, and `tests/rdgmega.py`'s legs pass
on it.

## 4. Cost

| artefact | base | after |
|---|---:|---:|
| shipped `REALDMG.O88` | 48,894 | 48,895 (the core's `tr_lpx`, in a spare half-slot) |
| shipped image | 36,000 | 36,000, byte-identical |
| arm `REALDMG.O88` (kern_mega's disks only) | 48,894 | 49,508 |
| arm image / its recompiler part, uncompressed | 36,220 / 21,037 | 36,566 / 21,160 |
| the bare harness (`tests/rdgcore/rdghost.asm`) | byte-identical | |
