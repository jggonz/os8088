# ANSWERED — a posted compaction ran at the WRONG RANK, and the hole it could not open was opened later by a pass that could not move the region

> **SPEC.md §66.4.3.3 is the contract and the fix; SPEC.md §20.12.10.8.1 carries
> the measurements. This file is the design record — how it was chased, and
> why the framing below had to be discarded before it could be.**

**The answer, in three sentences.** `mem_cpq_run_x` sets `[mem_pg_rank]` to the
rank the poster named and then calls `mem_compact` with `AX = 0`, whose `.rank`
block stored a zero over it on *both* arms — so every posted pass ever run
dissolved nothing and stopped at the first purgeable cache as a **barrier**,
parking its fill point past every movable claim above it. That cache (drive
B:'s FAT window, `MEM_P_FATW|1`, **5,120 bytes** — the whole of the missing
figure) was then dropped moments later by the package's *own*
`OSAPI_MEM_CLAIM_LVL`, which compacts at the claimant's real rank but runs
**inside the package's callback**, where `wm_pkgd` is 1 and `mem_in_nest` pins
the asking region. Two windows: the one that could move the region could not
open the hole, and the one that opened the hole could not move the region.

**−2 bytes in `mem_compact`, +5 in `mem_unblob_x` — +3 bytes of `.cold`,
which is RESIDENT** (docs/KERNEL-MEMORY.md: the name is the cold PATH, not a
cold lifetime — `.ovl`/`.ovlw` are the sections that go away). `.text`, `.bss`
and `.lowbss` are unmoved and `KERN_SIZE` reads 113,152 either way. With the
rank honoured the bottom-up carve reads **arena 455,680 and a hole of 0**,
which is the top-down carve's figure to the byte.

## 0. THE PREMISE BELOW IS WRONG, AND THAT IS THE LESSON

Everything from §1 on is the handoff as it was written. It is kept because
four of its five sections are sound and because the one that is not is
instructive: **"THE REGION MOVED UP BY 5,120 BYTES" never happened.**

§1's two middle rows are two different *launches*. Opening `A:/APPS/DOS.O88` by
hand leaves the heap floor clear, so the bottom-up carve lands at 0x2540;
reaching the box through `DOSHELLO.COM`'s association means **drive B: has been
opened first**, and its 5,120-byte FAT window is already standing at 0x2480, so
the carve is claimed above it at 0x2680. The equality of the two 5,120s is not
a coincidence — it is the *same cache*, counted once as the gap between the two
launches and once as the hole it left — and reading it as a distance a region
travelled is what sent §4 looking for a wrong-way move.

There was never a wrong-way move to find. **Candidate 2** was checkable at the
source and
is **refused**: `mem_cp_mine` is three instructions, a `door lo` claim is the
ascending pass's alone, and that pass's fill point is never above the claim it
is looking at, so neither pass can move a bottom-up claim up. **Candidate 1** (a
transient
under the region) was the right *instinct* — something 5,120 bytes long really
was standing under it — but it is not transient and it does not go away by
itself: it is a purgeable cache, and the question is which pass is allowed to
dissolve it. **Candidates 3 and 4** are both innocent.

**The method that did answer it is §4's own first sentence, taken literally:**
*dump `mem_tab` from inside `mem_cpq_run_x`*. Every reading in §1 was taken
from outside the pass, and *why did this claim not move* cannot be answered
from outside one. Three breakpoints did it — `mem_cpq_run_x` for the heap at
the service point, `mem_cp_dest`/`mem_cp_drop`/`mem_cp_move` for the walk's
fill point claim by claim, and `mem_pg_cheap` for the rank in force — and the
walk trace named it immediately: the region reached `mem_cp_drop` and
`mem_cp_far` (a **barrier**) where the fill point was already correct at
0x2540, in a pass whose `[mem_pg_rank]` read **0x00** twenty-four instructions
after `mem_cpq_run_x` had set it to **0xFE**.

**What would have caught it as a test**, and is the row to add if this is ever
revisited: nothing asserts that the poster's rank reaches `mem_cp_drop`.
`heapcheck` cannot see it — its region packs against a **pinned** neighbour, so
its hole closes at any rank. That is also why `soak -k 'heap*' -k 'reg*'` was
honestly 10/10 while the mechanism's one documented argument was being
discarded.

---

---

## 1. The measurement

`build/kdos360.img`, `os8088_5150_cga_gla`, `DOSHELLO.COM` off B:, the carve
forced to `OSAPI_MEM_CLAIM` at `apps/os88partsbody.inc:656`:

> **THE DISK NAME IS STALE AND THE DISK IS NOT.** `kdos360.img` was the
> shipped 360KB system disk with the parted `DOS.O88` in APPS/, built by `make
> kdostest`; SPEC.md 96.40.3 has since made the shipped disk itself carry that
> package, so the gate disk built byte-identical and was deleted. Every
> `build/kdos360.img` below is `build/os8088-360.img` today, and the snippet
> in §5 runs unchanged with that one substitution.

| moment | the box's region | interior hole | the arena |
|---|---|---:|---:|
| `A:/APPS/DOS.O88` open, **nothing run** | 0x2540 | **0** | — |
| a `.COM` has been run | 0x2680 | **5,120** | 450,560 |
| …and with the carve top-down (shipped) | 0x9480 | 0 | 455,680 |

`tests/kdhand.py`'s windowed figure reads **431 KB** in the middle row and
**436** in the last.

**THE REGION MOVED UP BY 5,120 BYTES**, which is the part to keep hold of. It
is not that a hole was left behind and ignored; a `door lo`, `MC_RLOC`-movable
region that started packed against the caches at 0x2540 ended up 320
paragraphs higher with nothing under it. An ascending pass should have packed
it *down*, and at worst should have left it where it was.

## 2. The posted compaction DOES run

Do not re-derive this: it cost a wrong section in SPEC.md.
docs/plans/REGION-SELF-COMPACT-PLAN.md §5 is **BUILT**, whatever CLAUDE.md's
summary of it says —

* `OSAPI_MEM_COMPACT_WAKE` is slot **0x0598** (`apps/os88api.inc`);
* `ui_task` step 0 spends it through `mem_cpq_run_x`
  (`kernel/ui.inc` `.keys`, `kernel/memory.inc`), with nothing held and
  `[wm_pkgd]` 0, which is the whole point of the service point being there;
* `apps/dos/dos.asm`'s `.post` has used it since SPEC.md §96.35;
* and `[dos_cpw]` reads **1** on the machine, so the post was accepted and
  the wake arrived.

`OS88_COMPACT` is defined on `kern_big` (`kernel/kernel.asm:382`) and
`kdos360.img` is a `kern_big` disk, so `mem_cpq_run_x`'s
`xor ax, ax / call mem_compact` — *compact regardless, BOTH passes* — really
executes.

## 3. What is ruled out, and on what evidence

`mem_frameless` (`kernel/memory.inc`) is the predicate that decides whether a
region may move. Its refusals, each checked:

| refusal | evidence it does not apply |
|---|---|
| `I_TASK != 0xFF` without a restart declaration | the box spawns no worker at all — `apps/dos/dos.asm` has no `OSAPI_TASK_SPAWN`, no `OSAPI_DRV_TASK`, no `inst_restart` |
| `cmp bx, [ld_base]` — the loader is standing in it | `[ld_base]` reads **0x0000** after the launch, read off the machine |
| `mem_in_nest` — a callback frame is in the region | the pass runs at `ui_task` step 0, where the asker's callback has returned; this is exactly what §5's design rests on |
| the feature compiled out | `OS88_COMPACT` is defined (§2 above) |
| the region not declared movable | `MC_RLOC` reads **0x00A7** and `MC_DMA` bit 15 is clear — *movable, door lo* |

So none of the obvious pins fires, and the claim is movable in the direction
that would have helped.

## 4. Where to look, ranked

1. **A transient claim under the region at the moment of the pass.** This is
   the hypothesis that explains the shape best and it is cheap to test: if
   something 5,120 bytes long occupied 0x2540–0x2680 *while* `mem_compact`
   ran, the pass packed the region above it legitimately — and whatever it was
   went away afterwards with nothing left to compact again. `dos_drv_take`
   (`OSAPI_DRV_SUSPEND`) runs a few instructions before `.post` and a driver
   image is a heap claim; neither dump in §1 shows a `MEM_K_DRV` (0xFF03)
   record, but **both dumps are outside the window**. Dump `mem_tab` from
   inside `mem_cpq_run_x` — a breakpoint there is the direct answer, and
   MartyPC's debugger costs the guest nothing.
2. **The ascending pass's placement.** If there was no transient, then
   `mem_cp_*` moved a `door lo` claim UP, which it should never do. Walk
   `mem_compact`'s two passes with the same table and see which one touches
   this record — docs/plans/REGION-SELF-COMPACT-PLAN.md §5.1.2's own finding
   was that two sweeps in the wrong ORDER wall each other in, so the order is
   the first thing to check.
3. **The arena claim's retry path.** `mem_claim`'s refusal is compact → shed →
   retry (SPEC.md §66.4), and the box claims with `DOS_PG_FLOOR` =
   `MEM_PG_HIGH`. The `MEM_P_WSAVE` cache (6,144, rank `MEM_PG_TRIV`) sits
   directly above the region before the run and is gone after, so a shed
   happened. 6,144 and 5,120 are not the same number, but a failed first
   attempt that shed and retried is a place a gap can be born.
4. **`mem_reown_x` and the door bit.** The region here is a re-owned parts
   carve (SPEC.md §20.12.10), not an `ld_alloc` region, and it is the only
   region in the tree with `door lo`. `heapcheck`'s region is an ordinary
   top-down one. If the compactor's two passes were only ever exercised
   against top-down regions, a `door lo` region is new ground.

## 5. The repro, and the probe

```sh
sed -i 's/OSAPI_MEM_CLAIM_HI     ; out DX/OSAPI_MEM_CLAIM        ; out DX/' \
    apps/os88partsbody.inc          # put the carve back bottom-up
make kdostest
```

Then this, which is the whole instrument (`tools/heapmap.py` is **QMP-only**
and the default emulator here is MartyPC, which is why it is not used):

```python
import os, sys, time
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty, os88ui, os88sym
MC_SIZE = 10                         # MC_SEG 0, MC_PARA 2, MC_OWN 4,
                                     # MC_DMA 6 (bit 15 = top-down), MC_RLOC 8
def heap(m, base, tag):
    rows = []
    for i in range(32):              # MEM_MAX
        d = bytes(m.read(base + i * MC_SIZE, MC_SIZE))
        if int.from_bytes(d[0:2], "little"):
            rows.append(tuple(int.from_bytes(d[j:j+2], "little")
                              for j in (0, 2, 4, 6, 8)))
    rows.sort(); print(tag); prev = None
    for seg, para, own, dma, rloc in rows:
        if prev is not None and seg > prev:
            print("   ....... %7d FREE" % ((seg - prev) * 16))
        print("   %04X %7d own %04X %s %s"
              % (seg, para * 16, own, "HI" if dma & 0x8000 else "lo",
                 "MOVABLE" if rloc else "PINNED"))
        prev = seg + para

with os88ui.boot("build/kdos360.img", apps="build/doscom360.img",
                 machine="os8088_5150_cga_gla") as ui:
    m = ui.m; base = os88sym.linear("mem_tab")
    ui.open_drive("A"); ui.open("APPS"); ui.open("DOS.O88")
    os88marty.settle(m); heap(m, base, "before any run")
    # ...then drive the Run path and dump again
```

`[dos_cpw]`, `[dos_akb]`, `[dos_memkb]` and `[dos_state]` are read with
`tests/dosmap.py` — `dosmap.package("DOSKPART")` for the offsets and
`dosmap.instance(m)` for the segment.

## 6. Traps

* **Opening a `.COM` RUNS IT.** `ui.open("DOSHELLO.COM")` on B: launches the
  box *and* the program through the association, so a dump taken after it is
  already past the Run path. To see the *before* state, open
  `A:/APPS/DOS.O88` directly — that is the one difference between the first
  and second rows of §1's table and it cost an hour of reading the same
  numbers twice.
* **Do not trust a plan header over the code.** CLAUDE.md's summary of
  docs/plans/REGION-SELF-COMPACT-PLAN.md says *"5's posted request is
  DESIGN"*; it is built and shipping and `apps/dos/dos.asm` calls it. This
  repository has form here — docs/plans/completed/GFX-REWORK-PLAN.md's own
  header said *"DESIGN NOT STARTED"* long after §3's boxes said otherwise.
* **Two soak rows are contention-flaky at lane 4 with extra emulators
  running** — `pkgrun` and `skiesvga` both reported red in a run and both pass
  standalone. Re-run a failure alone before believing it.
* **`MC_OWN` decodes three ways**: `0..INST_MAX-1` is an instance slot,
  `0xFF__` is a kernel tag (`MEM_K_*`), and `0xFB__`/`0xFE__` are PURGEABLE
  caches whose high byte is the rank (`MEM_PG_TRIV`…`MEM_PG_HIGH`). `FE02` is
  the directory read-ahead window and `FB10` is `MEM_P_WSAVE`, a window's
  raise cache — neither is a leak.

## 7. What a fix would be worth

**5,120 bytes of a DOS program's arena on this path, and nothing else
today** — the shipped carve is top-down, so no user is losing the 5 KB. The
value is in the answer: if **candidate 2** is what is happening, the compactor
moves a `door lo` claim the wrong way and every region that is ever claimed
bottom-up inherits it. If **candidate 1** is what is happening, the lesson is
that a package which frees something and then posts a compaction has the order
backwards, which is a rule worth writing into SPEC.md §66.4.3 rather than a
defect to fix.

Either way the outcome belongs in SPEC.md §20.12.10.8.1, which is where the
measurements in §1 already live.

> **It was neither, and the value guessed at here was guessed LOW.** It is not
> 5,120 bytes on one unshipped path: `OSAPI_MEM_COMPACT_WAKE`'s rank argument
> had never done anything on any path, so the one mechanism a package has for
> *"pack the heap properly, my own region included, and then wake me"* could
> not dissolve a cache and stopped at the first one as a barrier. The fix is
> SPEC.md §66.4.3.3 and it landed in the kernel; the outcome is written up
> there and in §20.12.10.8.1 as this section asks.
