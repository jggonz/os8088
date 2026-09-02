# Every claim in the tree, and whether the compactor can move it

The inventory behind SPEC.md §65. It exists because "can this move?" has five
different answers with different reasons, and most of them are permanent — so
a table is worth more than re-deriving it per claim.

**Adoption is finished.** Read §66.3 first for the mechanism, §66.9 for the
shape of what was left pinned, and "Where adoption stands" below for the
per-claim register of it. This is the map.

---

## The four verdicts

| | meaning |
|---|---|
| **MOVABLE** | declares a relocation proc and the kernel accepts it today |
| **DECLARABLE** | nothing structural stops it; it simply has no proc yet |
| **PINNED (rule)** | refused by `mem_can_move` for a reason a declaration cannot overturn |
| **PINNED (forever)** | its base *is* a CS, or a bus master is looking at it |
| **nothing to declare** | it makes no heap claim at all — measured, not inferred |

**Three things can never move, and no API will change that.** A package
**region**, a **driver image** and an **on-demand kernel module** are each
addressed by a CS, so relocating one invalidates every far pointer, every
`MB_SEG`, every claim owner word and every saved CS on every stack (§66.6). A
claim carrying **`MC_DMA`** is the fourth: the 64KB page rule is a property of
the address, and the chip may be mid-transfer.

**Purgeable caches are refused too, and that one is a design choice rather
than a hazard.** They place themselves top-down under the region ceiling
(§50.6.1), so sliding one *down* puts it in the arena it is kept out of — and
they need no relocation, because the shed already gives their room back.

---

## Kernel-owned claims

| claim | size / lifetime | verdict | why, and what it would take |
|---|---|---|---|
| `MEM_K_SAVE` menu save-under | 20KB, one menu | **MOVABLE** | `menu_reloc`. 20KB mid-arena at exactly the moment a *menu command* claims |
| `MEM_K_ASC` ASSOC.DAT cache | one per volume, long-lived | **MOVABLE** | `asc_reloc`. Claimed on a **volume switch**, so it lands wherever the arena had room — the measured barrier, see below |
| `MEM_K_CLIP` clipboard | sized to contents, long-lived | **MOVABLE** | `clip_reloc`. Outlives the app that filled it (§55), so long-lived by design |
| Disk window view cache | 3KB per open window | **MOVABLE** | `fm_reloc` — `FS_VSEG` **and** the `[fm_vseg]` mirror, the pair that killed the word-poke design (§66.1) |
| `MEM_K_FATW` FAT window | 4.5KB per mounted volume, long-lived | **DECLARABLE**, deliberately not yet | `[dsk_fatseg]` **plus** the per-volume `dsk_fatw0` array, and it is read by `dsk_next_clus` inside chain walks that themselves call `disk_read`, which claims. Claimed at MOUNT time, so it tends to sit low |
| `MEM_K_COPY` copy buffer | one Cut/Copy/Paste | **PINNED (forever)** | claimed through `mem_claim_dma` with the whole buffer as the page-safe head (§22.5.1) |
| `MEM_K_DRV` driver image | per loaded driver | **PINNED (forever)** | its base is the driver's CS |
| `MEM_K_MOD` on-demand module | per loaded module | **PINNED (forever)** | its base is the module's CS |
| `MEM_P_WSAVE` window raise cache | one per window | **PINNED (rule)** | purgeable |
| `MEM_P_DIRW` directory read-ahead | 63KB | **PINNED (rule)** | purgeable |

## Package-owned claims

| claim | verdict | note |
|---|---|---|
| **Paint** canvas, undo, clipboard, scratch | **MOVABLE** | §66.5.1. `pt_reloc` shifts a 110-entry row table and recomputes `[pt_undelta]` |
| **Paint** GIF/LZW staging buffer | **PINNED (rule)** | `[pt_gbase]` is a paragraph derived off `[pt_gseg]` at four sites, and it is the `OSAPI_FILE_READ` target as well; it lives for one file's encode or decode |
| **Tracker** module (up to 116KB) | **MOVABLE** | §66.5.2. `trk_reloc` fixes 36 words: `[mp_blobseg]`, 31 sample bases, 4 channel segments |
| **Frotz** story (up to 508KB — the largest claim this OS hands out), Z-stack, undo snapshot | **MOVABLE** | §66.5.9. `zf_reloc`. The story costs **three** words — the base, the live **program counter** (shifted, never set), and `[zf_sdelta]`, the running total `zi_yield` differences to repair an `ES` pushed before the move. It **cannot** declare `OSAPI_MEM_PARKSAFE` and does not need to (§66.5.9.1) |
| **Frotz** save staging, transcript, picture buffer, `.mg1` probe | **PINNED (rule)** | file-operation targets, or transients freed inside the call that made them |
| **Note Pad** document + undo arena | **MOVABLE** | §66.5.7. `np_reloc`, **two** words, one proc — `BX` picks between them |
| **Note Pad** CR/LF staging buffer | **PINNED (rule)** | transient, and the `OSAPI_FILE_WRITE` target throughout — pinned by having no declaration, the cheapest correct answer |
| **ArtfulType** document + undo/redo/clip arena | **MOVABLE** | §66.5.7. `at_reloc`, **two** words. The arena is up to 64KB and is claimed on the first fullscreen entry, so it is a late claim |
| **Fractal** run cache | **MOVABLE** | §66.5.7. `fr_reloc`, **one** word — every cursor into it is an offset |
| **ModPlug** module (up to 116KB) | **MOVABLE** | §66.5.8. `mpp_reloc`, **36** words — and §56.1's bill: the replayer is an independent copy of Tracker's at *different strides* (`MPS_SZ` 12, `MPM_CHSZ` 40), so a renamed `trk_reloc` walks the tables wrong and yields plausible garbage |
| **Task Manager**, Solitaire, Arkanoid, Missile, Tamegram, Minesweeper, Piano, Recorder, Hello | **nothing to declare** | §66.5.11. Measured, not assumed: **not one of the nine makes a heap claim.** The Task Manager's "~7.3KB of heap while open" (§28) is its *region* — image plus bss — and a region's base is its CS |
| every package **region** | **PINNED (forever)** | base is CS |

## Driver-owned claims

| claim | verdict | note |
|---|---|---|
| **SB staging pool** (20KB) | **MOVABLE** | §66.5.5. One word, because a grant is an *offset* and the staging copy is the v3 boundary |
| **SB DMA double-buffer** | **PINNED (forever)** | `MC_DMA` |
| **HDD** install buffer | **PINNED (rule)** | §66.5.10. An `OSAPI_FILE_READ`/`WRITE` target at all four uses, and claimed for one install and freed at the end of it — Note Pad's staging buffer again |
| **HDD** per-partition listing (6KB) | **MOVABLE** | §66.5.10.2, and it was **PINNED (forever, structurally)** until then. **Donated** to the kernel by `osapi_vol_add`, so three words name it and the callback — which dispatches to the *owner*, the driver — reaches one; the kernel now fixes its own two (`dsk_dseg_reloc`) for every move, before the owner's proc runs. `hd_lst_reloc` is the driver's half and is **one word**. Stays claimed LOW (§50.3.2.1): sent high it cost 9KB |
| **HDD** second image (`HDDTOOL.DRV`) | **PINNED (forever)** | base is CS (§52.11.7). **Claimed top-down** since §50.3.2.1 |
| **RAM disk** second image (`RAMPAGE.DRV`) | **PINNED (forever)** | base is CS (§62.9.9) — `[rd_pfar]` is `PKG_DISP:[rd_pseg]`. **Claimed top-down** since §50.3.2.1, where claiming it low was measured at **64.5KB** of what a claimant can have |
| **RAM disk** store | **MOVABLE** | §66.5.10. `rd_reloc`, **one** word — nothing outside `ramdisk.asm` sees the arena, and every handle into it is an offset |

---

## Where adoption stands

**Every claim in the tree has been looked at, and every one that can move
does.** SPEC.md §66.9 is the shape of what is left; the table below is the
per-claim register, and it exists because *"we did not get to it"* and *"it
cannot be done"* look identical from the outside a year later.

| pinned because | which claims | permanent? | what revisiting would take |
|---|---|---|---|
| its base **is a CS** | every package region, every driver image, the on-demand modules, `HDDTOOL.DRV` | **yes** — §66.6 | the whole of §66.6: `I_SPTR`, every `W_SEG` and `MB_SEG`, every claim owner word, `drv_fseg`, **and every saved CS on every stack**. Not a follow-on |
| a **bus master** may be looking at it | SB double-buffer, the copy buffer (`MC_DMA`) | **yes** | nothing. The 64KB page rule is a property of the address and the chip may be mid-transfer |
| **purgeable** | window raise cache, directory read-ahead | by design | not a proc — a decision to move the caches out from under the region ceiling (§50.6.1). The shed already gives their room back |
| a **file target**, and **transient** | Note Pad CR/LF staging, Paint GIF staging, Frotz save/transcript/picture/probe, HDD install buffer | no, but cheap for a reason | §66.5.7.1's pin/unpin pair, two far calls per file operation. **A transient cannot be a barrier for longer than it exists**, so the gain is bounded by the operation's own length |
| ~~it was **given away**~~ | ~~HDD per-partition listing, 6KB~~ | **DONE** | the kernel change was taken (§66.5.10.2) and it is not what that row predicted: `mem_reloc_call` recognises nothing, it calls `dsk_dseg_reloc` for **every** move, so the next driver to donate a claim is movable the day it declares a proc. Struck through rather than deleted for the reason the row above it is: the shape of what closing one takes is worth more than a blank |
| ~~nobody has done the **audit**~~ | ~~`MEM_K_FATW` FAT window~~ | **DONE** | the audit was taken and the window MOVES (§66.5.6.1): `dsk_fatw_reloc`, two words — `dsk_fatwc[vol]` where the claim is recorded and `[dsk_fatseg]` where the FAT is read from — scanned by value, because a compaction can fire while a different volume is current. This row is kept struck through rather than deleted: it is the only DECLARABLE this table ever carried, and the shape of what closing one takes is worth more than a blank |

**Nine packages have nothing to declare at all** (§66.5.11): Task Manager,
Solitaire, Arkanoid, Missile, Tamegram, Minesweeper, Piano, Recorder, Hello.
Measured, after this document claimed otherwise.

## …and PLACEMENT is a second axis this table does not have

Every verdict above answers *can the compactor move this block*. Since §50.3.2
there is a second question with a different answer — *which END of the heap
was it claimed from* — and a row here reading **PINNED (forever)** settles the
first and says nothing about the second. `OSAPI_MEM_CLAIM_HI` is the top-down
door; §50.3.2.1 is the two claims that should have gone through it and did
not, both of them a driver's second image.

**A verdict here is a declaration, not an observation — check `MC_RLOC` on a
running machine.** §66.5.6.2 is why: `fm_kinit_x` said `; ...and it is MOVABLE`
on the line that failed to make it so, because `fmv_movable` derived the owner
from `[fm_vinst]` sixty lines before `fm_vp_set` published it and
`mem_movable`'s fence refused a claim it did not recognise — silently, into a
`popf`. Every Disk window on a floppy volume carried a **PINNED** 3KB cache in
the arena's floor, and this table said MOVABLE the whole time. `tests/heaphi.py`
and `tools/heapmap.py` both read `MC_RLOC` out of `mem_tab` for that reason.

**The rule is not "pinned belongs high", and that was measured rather than
assumed.** A pinned claim's cost is decided by **what is above it**: below
every movable claim it costs nothing, because `mem_cp_plan` resumes its fill
point above each barrier and the movables pack straight past; above them it
cuts the arena in two for as long as it is held. The HDD's per-partition
listing claim is the case that separates the two — pinned for ever, sitting
directly under the 63KB read-ahead, and **free where it is**; sent high it
measured 9KB worse and was put back. `tests/heaphi.py` is the gate, and it
asserts the compaction number rather than a predicate over the map, because no
predicate over the map tells the two apart.

**The standing limit is none of the six.** It is §66.5.5's all-or-nothing
driver park: `TF_SERVICE` cannot say *which* service task is running, so one
Sound Blaster stream mid-refill pins every driver-owned claim on the machine
— the RAM disk's store included, which owns no worker at all. A finer handle
is a task-table change, not a claim-side one.

## What actually limits compaction today

Not the mechanism. Two things now, in the order they cost:

**Late kernel claims were the biggest barrier, and are fixed (§66.5.6).** This
is the finding that contradicted the plan document's §1, which reasoned that
kernel claims are small and settle at the bottom. They settle at the bottom
*when they are claimed early*. The **ASSOC cache is claimed on a volume
switch**, so on a used machine it sits wherever the arena had room — measured
on a sound machine mid-session, `MEM_K_ASC` sat at `0x2fc0` with a **40KB hole
beneath it** and every movable block above it packed hard against it. **A 3KB
undeclared claim was holding 40KB out of reach.**

Declaring it changed the whole arena on the identical run: the cache moved
`0x2fc0 → 0x25a0`, Tracker's 114KB module followed `0x5900 → 0x4ee0`, the
sound driver's pool followed `0x7580 → 0x6b60`, and the trapped 40KB became a
**52KB contiguous run**. Nothing else was touched. *A pinned block is a
barrier whatever its size.*

**1. A worker that draws and NEVER SLEEPS must declare `OSAPI_MEM_PARKSAFE`**
or its claims are unreachable whenever the triggering claim comes from a
callback holding the gfx lock (§66.5.3/§66.5.4). That qualifier is measured
rather than assumed: a worker that sleeps between passes reaches
`OSAPI_TASK_ALIVE` inside `INST_PARKW` on its own, so Note Pad, Fractal and
ArtfulType move with the gfx-lock park removed and Tracker does not
(§66.5.7.2), and so does ModPlug, whose worker sleeps a tick a pass. All five
declare it as a widening. **Frotz is the one app that MUST NOT** (§66.5.9.1):
`zx_lock` pushes the program counter's segment across `OSAPI_GFX_LOCK` by
design, so the assertion is false there — and its claims move at the ordinary
`ALIVE` park regardless. Tracker remains the only case where it is the
enabler.

**2. A driver's claims move only while every `TF_SERVICE` task is parked**,
all-or-nothing, because `TF_SERVICE` is the only handle the kernel has on "a
task running inside a driver" and it does not say *which* one (§66.5.5).

---

## The gap that closed itself

This document previously recorded that **the SB pool had never been observed
moving** — declared, base a live claim, driver parking, but never seen to
shift, because the pool exists only while a stream does and the arena beneath
it was packed in every run.

It was packed *because the ASSOC cache was pinned underneath it*. Unpinning
one 3KB kernel claim moved the pool on the very next run, with nothing else
changed: `0x7580 → 0x6b60`, on `os8088_xt_vga_sb` with the module playing.
`tests/trackmove.py` check 7 passes.

Worth keeping as a shape: **the thing that could not be demonstrated was not
the thing that was broken.** Three separate blocks looked immovable and one
barrier was holding all three.
