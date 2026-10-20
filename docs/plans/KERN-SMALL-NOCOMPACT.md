# Gating heap compaction out of kern_small — costed, measured, and REFUSED

**Status: BUILT, both halves, and SPEC.md 50.6.5 and SPEC.md 66.0 are the
contract. This file is the design record behind them — how the refusal in 5
became the route in 9 and then the gate in 10.**

The ask: kern_small's heap is small and it can load no driver, so would the
128KB machine rather have the compactor's bytes back than the compactor?

The bill is **1,725 resident bytes**, worth **1.5 KB of heap** on the tree this
was taken on. What it costs is the ability to launch a 32KB package once two
Disk windows are open — measured as an A/B on the floor machine, same disk,
same gestures:

| | `PAINT.O88` after A: and B: are open |
|---|---|
| kern_small as it ships | **loads** |
| kern_small `HEAPCOMPACT=0` | **"Out of memory"** |

and 1.5 KB of extra floor does not close that gap, because the gap is 9 KB.

**That was the answer for one afternoon.** 9 is the route round it — make the
view cache PURGEABLE, +12 bytes — and 10 is what the gate then measured once
that had landed: **1,659 resident bytes and +1,536 of heap**, free heap on the
floor machine **48.5 → 50.0 KB**, with the same session still loading Paint.

**9 is the way through, and it is measured too**: make the view caches
PURGEABLE on kern_small and the same session reaches **48.5 KB in one run** —
more than compaction's 44.5, because a shed cache gives its bytes back where a
moved one keeps them — and Paint loads with no compactor at all. The
compactor's bytes are then buyable. What that costs, and the two wrinkles in
the purgeable contract, are in 9.

**The premise about drivers is right and does not reach the answer.** No driver
image, no `SOUND.DRV` ring, no donated listing exists on kern_small — SPEC.md
51.0 gates the whole mechanism out — and none of those is what compaction is
doing there. What it is doing is moving **two 2 KB Disk-window view caches**
that sit either side of the directory read-ahead, and those 2 KB claims strand
**21.5 KB** of a 48.5 KB heap.

Every byte below is nasm's own listing of `build/smallk/kernel.bin`; every heap
figure is read out of `mem_tab` on `os8088_5150_cga_128k` under MartyPC.
Taken at `d3ffb10`, 2026-09-08.

---

## 1. What gating it out would save

Not the `HEAPCOMPACT=0` knob — see 2. This is the whole feature: the compactor,
the one predicate that decides what may move, the relocation dispatch, the
worker park (SPEC.md 66.5) and the four relocation procs in the kernel. The
`OSAPI_MEM_MOVABLE`, `OSAPI_MEM_PARKSAFE` and `OSAPI_TASK_RESTARTABLE` slots
stay and become refusing stubs, `gfx_blit1`'s precedent on this kernel — a
small-built package calls the same table at the same offsets.

| | `.cold` | `.text` |
|---|---:|---:|
| `memory.inc` — `mem_compact`, `mem_cp_end`, the seventeen `mem_cp_*`, `mem_can_move` and its five helpers (`mem_is_region`, `mem_frameless`, `mem_busy_seg`, `mem_in_nest`, `mem_in_xfer`), `mem_reloc_call`, `mem_movable_x`, `mmf_mem_movable`, the call sites | 962 | — |
| `memory.inc` — `mem_rr_walk`, `mem_region_reloc`, `mem_rr_tab` | — | 91 |
| `instance.inc` — the worker park: `inst_park_hold/wait/lk/unlk/mk/me`, `inst_of_seg`, `inst_park_req/end/all`, `inst_seg_parked`, `inst_svc_parked`, `inst_task_park`, the two setters, the `ALIVE` hook | — | 401 |
| `sched.inc` — `sch_wk_restart` | — | 66 |
| `menu.inc`, `clip.inc`, `files.inc` — `menu_reloc`, `clip_reloc`, `fm_reloc`, `fmv_movable` and their declarations | — | 107 |
| `disk.inc`, `hiber.inc`, `vga12.inc` — `[mem_pinseg]`, the two `gfx_lock` park calls | — | 27 |
| **code** | **962** | **692** |

plus 53 bytes of `.bss` (`mem_cp_busy`, `mem_cp_msk`, `mem_wpin`, `mem_parked`,
`mem_pinseg`, `mem_cp_key`, `inst_parksafe`, `inst_restart`, `inst_parkreq`,
`sch_parked`) and 4 of `.lowbss` (`mem_fptr`). **1,725 bytes.**

That is already net of ~70 bytes credited back, because **`mem_avail` IS
`mem_cp_plan`** (SPEC.md 66.10.3): the number every package sizes itself down
from is the run compaction would leave, so gating the compactor out means
writing a largest-free-run walk to replace it. The one it replaced was
`O(MEM_MAX^2)` and is not in the tree to restore.

Dropping `MC_RLOC` from the claim record as well is a further 40 bytes of
`.lowbss` on `MEM_MAX` = 20, and it crosses no rung, so it is not worth the
churn through every `MC_*` offset.

### 1.1 What that is in heap

`kernsize` for kern_small at this commit: `.text` 39,458, `.bss` 4,871,
`.cold` 27,372, `.lowbss` 6,064, `KERN_SIZE` 79,872, HEAP at 0x13E0.

| rung | now | after | |
|---|---:|---:|---:|
| cold | 27,648 | 26,624 | **−1,024** |
| image (`.text`+`.bss`) | 44,544 | 44,032 | **−512** |
| low | 6,656 | 6,656 | 0 |

**HEAP_SEG falls 1,536 bytes**, and free heap on the 128KB machine goes
**48.5 KB → 50.0 KB**. Quote the 1,725, not the 1,536: which rung a byte lands
in decides where the 512 comes from and never whether the change was free.

## 2. `HEAPCOMPACT=0` is not a preview of this

The knob stubs the bodies of `mem_compact` and `mem_can_move` and leaves every
`mem_cp_*` routine assembled, because `mem_avail` still plans through them. On
kern_small it is worth **158 bytes of `.cold` and nothing else** — no rung, no
`.text`, no `.bss`. It is a behaviour A/B and is used as one in 5 below; it is
not a size measurement and reading it as one understates the feature by 11x.

## 3. The heap kern_small actually has

**A bare kern_small desktop has no claims at all.** `tests/small128.py` asserts
it and it passes: `mem_tab` is empty, 48.5 KB free in one run. There is nothing
for a compactor to do until the user does something.

What ordinary use puts in it, read off the machine:

```
--- A: and B: Disk windows open ---
    arena 13E0..2000 = 48.5 KB
    13E0    2.0K inst0        movable   bottom-up   fm_reloc
    1460   23.0K pg:FE02      purgeable bottom-up   the read-ahead
    1A20    2.0K inst1        movable   bottom-up   fm_reloc
    1AA0    1.0K pg:FATW1     purgeable bottom-up
    1AE0    6.0K pg:WSAVE0    purgeable bottom-up
```

Shed every cache and the largest run is **23.0 KB** — the read-ahead's own
hole, walled above by a 2 KB view cache. Move those two 2 KB claims down and it
is **44.5 KB**. The 21.5 KB difference is the whole of what compaction is worth
here, and it is bought by moving 4 KB.

## 4. The claims on kern_small that are neither purgeable nor a module image

Packages aside. `MEM_K_ASC` (SPEC.md 54.0), `MEM_K_DRV` (51.0) and `MEM_K_BAND`
are compiled out of this build, so the list is short and the movable part of it
is three rows:

| claim | size on kern_small | verdict |
|---|---|---|
| `MEM_K_SAVE` menu save-under (SPEC.md 12.4) | measured **3.0 KB** live while the File menu is down; `MENU_SAVE_KB` 20 is the clamp | **MOVABLE** — `menu_reloc`. Live at exactly the moment a menu COMMAND claims |
| Disk window view cache (owner = the window's instance slot) | `VIEW_KB` = **2 KB**, up to four | **MOVABLE** — `fm_reloc`. **The one that bites**: two of them strand 21.5 KB in 3 |
| `MEM_K_CLIP` clipboard (SPEC.md 55) | sized to contents | **MOVABLE** — `clip_reloc`. Long-lived by design; outlives the app that filled it |
| `MEM_K_COPY` Cut/Copy/Paste buffer | one PASTE | PINNED for ever — `mem_claim_dma` with the whole block as the page-safe head |
| `MEM_K_CLONE` disk cloner (SPEC.md 18.99), `MEM_K_CMPR` Compress/Uncompress (22.22/22.23) | one operation | UNDECLARED, so pinned; both are `DISKOPS.DRV`'s |

`MEM_K_HIB` is **not a row on this kernel at all**: hibernate is `BIGMODS`, so
kern_small has no `mod_tab` row, no module and no claim (SPEC.md 87).

Three lifetime facts worth having, because they are what decides that only one
row above matters:

* **The copy buffer is claimed by the PASTE, not by the Cut or the Copy.**
  Cut/Copy write a path into `.bss`; `fcp_bufget` runs inside `fcp_start`,
  after the self-check and the cluster-span probe. It is *not* bounded by the
  transfer, though — `fcp_stop` exists because the claim is held across a
  suspended **overwrite question**, so it can stand for as long as the user
  takes to answer a modal dialog.
* **The cloner** copies LBA 0..N−1 raw between two floppies (or two disks in
  one drive) and never looks at a FAT. Its claim is the job block plus the
  sectors in flight, for one clone.
* **Compress** is the file manager's two verbs, riding inside `DISKOPS.DRV`. Its
  claim is the source, the output and the matcher's `head[]`/`prev[]` in one
  block — the largest transient claim in the kernel, and freed with the verb.

The three purgeable families (`MEM_P_WSAVE`, `MEM_P_FATW`, `MEM_P_DIRW`) are
out of scope by the question and stay whatever is decided — the shed is a
separate mechanism and this study proposes nothing about it.

**The shape of the answer is why gating hurts.** Every claim that is *big* on
kern_small is either purgeable (sheds) or a region (top-down, and the ceiling
already refills). Everything that is *pinned mid-arena* is transient. What is
left is three small, long-lived, movable claims — too small to shed, big enough
to wall, and landing wherever the session put them. That is the population
compaction exists for, and removing the move leaves no other answer for it.

## 5. The A/B that refuses it

`os8088_5150_cga_128k`, `small360.img` + `apps360.img`, identical gestures
through `tools/os88ui.py`: open A:, open B:, open `B:/APPS/PAINT.O88`. Paint's
region is ~32 KB, which sits between the 23.0 KB the arena has and the 44.5 KB
compaction reaches.

```
kern_small (shipped)          after A: and B: -> run 23.0 KB   PAINT LOADED
kern_small HEAPCOMPACT=0      after A: and B: -> run 23.0 KB   toast: "Out of memory"
```

Both arms read the same 23.0 KB, which is the point: the run a claimant can
have is not the run lying about now, and the two kernels differ only in whether
the allocator can go and make one.

## 6. The read-ahead is the proximate fragmenter, and capping it is not the way out

`HEAPCOMPACT=0 DIRW1=1` — no compactor and no sector cache (SPEC.md 18.95) —
leaves 44.5 KB after both windows are open and loads Paint. So in *this*
scenario the 23 KB read-ahead is what splits the arena.

It does not follow that the read-ahead should be capped instead, for three
reasons:

1. `DIRW1=1` is a claim about REVOLUTIONS and no emulator here models one. The
   Makefile says so at the knob. Trading a memory feature for an I/O feature on
   the slowest machine in the tree wants the 5150, not this box.
2. The cache is **purgeable**: it is not heap the machine loses, it is heap the
   machine lends. What costs is its PLACEMENT, and moving it to the ceiling was
   tried and reverted — `mem_claim_x`'s own header records that a top-down
   cache is one the shed cannot give back.
3. It is not the only splitter. Taking it out only moves the failure one app
   along: with `DIRW1=1` and no compactor, Calc and Note Pad load and Paint
   still refuses at an 11.0 KB run — and the compacting arm of the same
   sequence refuses too, at 13.5 KB, because by then the heap is genuinely
   full. The discriminating case is the one in 5, and it is the common one.

## 7. What is worth taking anyway

Nothing large, and none of it is this study's subject:

* **`inst_svc_parked` (49 bytes) and `inst_task_park`'s body (15)** are the
  park's DRIVER half. `TF_SERVICE` is set only by `OSAPI_DRV_TASK`, so on a
  kernel that can load no driver the scan can never find one. `%ifdef
  OS88_DRIVERS` with a `clc`/`ret` fallback is ~56 bytes of `.text`, gated on
  the drivers and not on the compactor — `mem_rr_tab` already does exactly this
  for its five driver rows.
* The 1,725 bytes stay on the table for a **192KB-class** kernel that does not
  exist. Nothing here argues the feature is cheap; it argues it is earned.

## 8. How to re-take any of it

```
make small                                            # build/smallk
python3 tools/kernsize.py --build build --ico build -DKERN_SMALL
python3 tools/kernsize.py --modules --build build --ico build -DKERN_SMALL
python3 tests/small128.py                             # the bare-desktop audit
```

The A/B trees are `tools/os88build.py`'s, never `build/`:

```
python3 -c "import sys; sys.path.insert(0,'tools'); import os88build; \
            print(os88build.tree('HEAPCOMPACT=0', targets=('small','apps360.img')).dir)"
```

and every script driving one needs `OS88_BUILD=<tree>/smallk`,
`OS88_TREE=<tree>` and `OS88_DEFINES="KERN_SMALL NOCOMPACT"` — the knob's make
variable is not its nasm define, and without the define `os88sym` refuses with
"the map describes a DIFFERENT kernel", which reads like a broken kernel and is
not one.

---

## 9. The route: make the view cache PURGEABLE on kern_small

The owner's reading of 4 — the save-under and the copy buffer are temporal to a
user action, hibernate is not on this kernel, the cloner and Compress are
one-shot verbs, the clipboard is a candidate for gating out entirely — leaves
**one** row that is neither temporal nor removable: the Disk window's listing
cache. It is 2 KB, it is long-lived, and 3 measured it stranding 21.5 KB.

**A cache that can be SHED does not need to be MOVED.** That is the whole idea,
and it beats compaction on its own ground: the shed gives the bytes back, the
move only rearranges them.

### 9.1 Measured

Same session as 5, on a kern_small built with `HEAPCOMPACT=0` and the view
cache refused at both claim sites — the worst case of purgeable, a cache that
is always gone:

```
A: and B: open   -> 3 claims, largest run 48.5 KB   (compaction reaches 44.5)
APPS open        -> 3 claims, largest run 48.5 KB
PAINT.O88        -> LOADED
```

against the 23.0 KB and the *Out of memory* of 5. The whole arena becomes
reclaimable, because every claim left in it is purgeable.

**And the window still works.** The cacheless Disk window paints its rows,
icons, sizes and scrollbar off the global snapshot — photographed on the floor
machine, `Drive B: 17 files` with the listing on the glass. files.inc:916 calls
that the documented fallback and it holds.

### 9.2 The contract, and the two wrinkles

SPEC.md 50.6 asks for exactly ONE kernel word naming the block, and a zero in
it already meaning "no buffer, do it the slow way".

* **The zero path already exists and is already the documented one.** `fmv_fit`
  and the `KD_INIT` claim both fall to `.nocache` on a refusal today.
* **It has TWO naming words, and there is precedent.** `FS_VSEG` in the
  window's `KD_POOL` block, and the `[fm_vseg]` mirror — which is *derived*,
  republished by `fm_vp_set` from `FS_VSEG` on every acting-window change, so a
  demote proc is `fm_reloc`'s two compares writing 0 instead of DX.
  `mem_pg_forget` already carries an arm exactly like it for the FAT window
  (`dsk_fatw_demote`), for exactly this reason.
* **The owner has to become a kernel tag** (`MEM_P_VIEW` + the pool ordinal,
  `MEM_P_WSAVE`'s shape at `FM_MAXWIN` = 4), because in SPEC.md 50.6 the tag IS
  the request. **That is the wrinkle**: the claim is reaped today by
  `mem_free_owner` on the instance's teardown — files.inc:395 says so in as
  many words — and a kernel tag is not reaped that way. The Disk kind has no
  teardown hook, so the free has to go somewhere. The cheap answer is
  `KD_INIT`: it already worries about "whatever the last tenant of this
  `KD_POOL` block left" (SPEC.md 22.6.1), so freeing the previous tenant's
  claim there closes the one hazard that matters — a stale purgeable claim
  whose naming word now belongs to a different window. Between a close and the
  next open the block is merely held, and held purgeably, which is safe.
* **Rank `MEM_PG_LOW`.** Losing one costs that window's repaints a directory
  re-read from the global snapshot — "a little I/O, or a visible pause" — and
  it is **self-healing**: `fmv_fit`'s only caller is `fmv_store`, so the cache
  is re-claimed the next time a listing is stored into that window. That is
  strictly cheaper than `MEM_P_FATW`'s MED, whose loss persists until the
  volume is remounted.

### 9.3 What it does not fix

Compaction still reaches things purging cannot, and gating it out gives those
up on kern_small too:

* **The apps declare movable claims and most of them ship on the small disk** —
  Paint's canvas, undo, clipboard and scratch; Note Pad's document and undo;
  ArtfulType's and Fractal's. SPEC.md 24.5 omits nine packages and none of
  those is among them. On a 48.5 KB heap the realistic case is one app at a
  time and its claims die with it, but that is an argument from the size of the
  machine rather than from the mechanism.
* **The menu save-under** is still movable and still live at exactly the moment
  a menu command claims (SPEC.md 12.4) — measured at 3.0 KB, not the 20 KB
  clamp, so it is a small wall rather than a large one.

### 9.4 The order to take it in

`MEM_P_VIEW` is worth taking **on its own merits and before anything is
gated**: it is a few dozen bytes, it makes the arena fully reclaimable, and 9.1
shows it beating the compactor in the case that actually bites. Whether the
compactor's 1,725 bytes then come out is a second decision, measurable against
the same rows once the first has landed — and it should be re-measured rather
than inferred, because 9.3 is what changes hands.

---

## 10. Built: `OS88_COMPACT`, and what the second measurement said

9 landed first, on its own merits (SPEC.md 50.6.5). With the arena fully
reclaimable the case in 5 was gone, so the gate was re-measured against the new
baseline rather than inferred from the old one — which is what 9.4 asked for.

**SPEC.md 66.0 is the contract.** `OS88_COMPACT` is defined for `KERN_BIG` only,
beside `OS88_ASSOC` / `OS88_DRIVERS` / `OS88_RTC` above every `%include`.

### 10.1 What it cost, measured

| | `.text` | `.bss` | `.cold` | `.lowbss` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|
| gating compaction out | **−667** | **−52** | **−936** | **−4** | **−1,536** |

**1,659 resident bytes**, against 1,725 predicted in 1 — the difference being
`fm_reloc` and `fmv_movable`, which 9 had already taken. Free heap on
`os8088_5150_cga_128k` goes **48.5 → 50.0 KB** and `kern_big` assembles
**byte-identical**.

### 10.2 `mem_avail` was the whole of the work

Everything else is `%ifdef`. §66.10.3 made the largest free run `mem_cp_plan`'s
*because the refusal path compacts*; with the compactor gone the refusal path
sheds, so the answer is the biggest hole the shed leaves and `mem_bigrun` had
to be written for it — ~100 bytes, `O(MEM_MAX²)`, exactly what it replaced.

**It shipped wrong first and a test caught it.** `mem_pg_cheap` answers CF = 0
for *takeable*, and the first cut read the carry the other way round — so every
cache was a wall and every wall was room. It answered **48.0 KB where the heap
had 50.0**: an under-report, which SPEC.md 50.3 names as the one direction in
which the error is invisible. Nothing in the driven session showed it. What
showed it was reading the routine's own `AX` at its `ret` and comparing it with
the same walk done on the host, over four real heap states.

### 10.3 The two traps in measuring it

Both are worth writing down because both produced a **green result that had
measured nothing**:

1. **A breakpoint on a routine nothing is calling.** The first probe armed
   `mem_bigrun` on an idle desktop, waited, timed out, and reported PASS
   because the failure path returned before the compare. `dsk_fatw_want` sizes
   the FAT window off `mem_avail` at **every mount**, so a mount is the
   provocation — and a probe that does not fire has to be a FAILURE, not a
   silent skip (docs/WRITING-TESTS.md 1).
2. **`run` is asynchronous.** A `wait_stop` issued straight after it observes
   the breakpoint the guest is still standing on and answers immediately, which
   reads the registers of the call *before* the one being measured. Polling
   `stopped()` does not fix it either — the guest is stopped at the entry when
   you ask and stopped at the return a microsecond later, and only the **address**
   tells the two apart. And driving the UI layer from a second thread while
   breakpoints are armed does not work at all: every `os88ui` verb raises on a
   stopped guest, correctly.

The read-ahead's size is a third, cheaper reading of the same number and needs
no debugger: `dsk_rah_want` claims `(n*9+1)>>1` KB for `n = avail/9`, so
`MEM_P_DIRW`'s size in `mem_tab` *is* `mem_avail`'s answer with known arithmetic
on top. It confirmed 50.0 KB exactly on an empty heap.

### 10.4 What the machine gives up, stated plainly

The owner's decision, in their words: *"the user can manage their app space"* —
launch the Task Manager, launch the file manager, close the Task Manager, then
launch Paint, in exchange for the KB. What is actually given up:

* the **menu save-under** (3.0 KB, alive while a menu is down) and the
  **clipboard** are barriers now rather than movable;
* every claim a package declares movable is pinned — Paint's canvas, Note Pad's
  document, ArtfulType's and Fractal's, all of which ship on the small apps
  disk (SPEC.md 24.5);
* `OSAPI_MEM_MOVABLE`, `OSAPI_MEM_PARKSAFE` and `OSAPI_TASK_RESTARTABLE` refuse.
  The slots stay, so a small-built package still runs on `kern_big` unchanged.

And what it keeps is the half that was doing the work: **purging**, which after
9 reaches every claim in the arena that a `kern_small` desktop actually makes.
