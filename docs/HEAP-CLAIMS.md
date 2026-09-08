# Every claim in the tree, and whether the compactor can move it

The per-claim inventory behind SPEC.md §66. §66.3 is the mechanism
(`mem_can_move` is the one predicate and every pinning rule is in it), §66.9
is the shape of what stays pinned and why; this file is the register, one row
per claim, so "can this move?" is looked up rather than re-derived.

**A verdict here is a declaration, not an observation.** `MC_RLOC` in the claim
record (§66.2: 0 = pinned, else the holder's relocation proc) is the fact, and
`tools/heapmap.py` / `tests/heapmap.py` read it out of `mem_tab` on a running
machine. §66.5.6.2 is the case for checking: a declaration whose owner fence
refused it, silently, left this table saying MOVABLE for a cache that was
pinned. When a row here matters, boot and read the map.

**A package that owns a WORKER is a third question** (§66.6.2). Its region is
pinned however it declares `OSAPI_MEM_MOVABLE`, because `task_spawn` wrote the
segment into the worker's frame before its first instruction —
`OSAPI_TASK_RESTARTABLE` is the way past it, and the kernel acts on it only
while the worker is **parked**. So a row here that says MOVABLE for a
worker-owning package is describing what the package asked for and not what the
compactor will do; `inst_restart[slot]` beside `MC_RLOC` is the pair that
decides.

**Movable is only half of it — the other half is WHICH WAY** (§66.4.1). A claim
goes back through the door it came in by, so the ascending pass moves only the
bottom-up claims and the descending pass only the top-down ones, and each
treats the other half as a wall. `MC_HI` in the record is that fact and the map
prints it beside the verdict:

    92400..95C00    14.0K  seg 95C0       movable  top-down
    9C400..9E400     8.0K  seg 9E40       PINNED   top-down  dma-head 512 para
    1B800..1C400     3.0K  kern:ASC       movable  bottom-up

so "it is movable and it did not move" has an answer to look up rather than
derive. On a stock 640KB boot the whole ceiling stack is top-down and only
`kern:ASC` is not, which is why an ascending pass alone was a plausible model
of the arena for as long as it was.

**Adding a claim is not touching the compactor.** The fifteen-row heap family
is ~2,500 declared seconds of emulator, and docs/WRITING-TESTS.md 13 row 31
asks for all of it after a change to `mem_can_move`, the `mem_cp_*` placement
and packing routines, `mem_region_reloc`, the claim table's shape, or a
relocation proc — the kernel code every one of those rows is about. A PACKAGE
that claims heap reaches none of it: an undeclared claim is born pinned
(§66.2) and is a wall like any other, which is the case the family already
covers a dozen times over. Add a row here, let the package's own gates and
`heapcheck` run, and leave the family to whoever changed the machinery.
Declaring one MOVABLE is a bigger step and wants `heapmap` read back off a
running machine (above), but it is still that package's question and not the
compactor's.

---

## The verdicts

| | meaning |
|---|---|
| **MOVABLE** | declares a relocation proc and `mem_can_move` accepts it |
| **UNDECLARED** | nothing structural stops it; it has no proc, so it is pinned by default (§66.2) |
| **PINNED (rule)** | refused by `mem_can_move` for a reason a declaration cannot overturn |
| **PINNED (forever)** | its base *is* a CS, or a bus master is looking at it |
| **PURGEABLE** | never moved and never a barrier: the compactor **drops** it under pressure (§66.10) |
| **nothing to declare** | the package makes no heap claim at all (its region is a claim, and a region's base is its CS) |

**Four things could never move, and three of them now do.** A package
**region**, a **driver image** and an **on-demand kernel module** are each
addressed by a CS, so relocating one invalidates every far pointer, every
`MB_SEG`, every claim owner word and every saved CS on every stack (§66.6) —
which is why the answer for the first two is not an API but a **kernel table
walk**: `mem_region_reloc` rewrites every kernel word that names the block,
including (§66.6.3.1) the interrupt vector table, and a worker standing on the
old segment is re-entered on the new one (§66.6.2). The fourth,
**`MC_DMA`**, stopped being a blanket pin at §66.4.2 — the page rule says where
a block may *land*, and `mem_cp_dest` honours it — and is now refused only
while a chip could be armed on one, which `[drv_wcnt]` answers exactly
(§66.6.4).

**The on-demand kernel module is the one that stays pinned**, and by decision
rather than by mechanism: every module in the tree is a *temporary action* —
the Control Panel, Format, Clone, Hibernate, a Cut/Copy/Paste, a file dialog —
so one can fragment the heap only for as long as the user is inside it, and
`mem_claim_1`'s `.hi` arm puts it back at the ceiling when it is dropped and
re-taken. **A persistent module would reopen the question**, and only for that
module.

**Purgeable caches are refused by `mem_can_move` too**, and it costs nothing:
a cache that is in the way is dissolved by `mem_cp_drop` when a claim is
waiting and outranks it (§66.10.1), which is the same room at none of the copy.

---

## Kernel-owned claims

| claim | size / lifetime | verdict | why, and what it would take |
|---|---|---|---|
| `MEM_K_SAVE` menu save-under | `MENU_SAVE_KB` = 20KB, one menu | **MOVABLE** | `menu_reloc`. 20KB mid-arena at exactly the moment a *menu command* claims |
| `MEM_K_ASC` ASSOC.DAT cache | `ASC_KB` = 3KB, one volume at a time, long-lived | **MOVABLE** | `asc_reloc`. Claimed on a **volume switch**, so on a used machine it lands mid-arena — measured holding 40KB out of reach before it was declared (§66.5.6) |
| `MEM_K_CLIP` clipboard | sized to contents, long-lived | **MOVABLE** | `clip_reloc`. Outlives the app that filled it (§55). `clip_put` pins its *source* through `[mem_pinseg]` across its own claim (§66.5.6) |
| Disk window view cache (owner = the window's instance slot) | `VIEW_KB` = 3KB per open window (2KB on kern_small) | **MOVABLE** | `fm_reloc` — `FS_VSEG` **and** the `[fm_vseg]` mirror, the pair that killed the word-poke design (§66.1). Declared from the claim site with the owner in hand (§66.5.6.2) |
| `MEM_K_COPY` copy buffer | one Cut/Copy/Paste | **PINNED (forever)** | claimed through `mem_claim_dma` with the whole buffer as the page-safe head (§22.5.1) |
| `MEM_K_DRV` driver image | per loaded driver | **MOVABLE** | §66.6.3. Its base is the driver's CS, and a CS moves when nothing anywhere refers to it — which for a driver image means the kernel's own tables (`mem_region_reloc` walks all 66 words in 9 of them), the sixteen hardware IRQ vectors (§66.6.3.1) and nothing else, because no driver declares anything and none can forget to. Refused only while a frame is standing in it or `[drv_wcnt]` is non-zero. `tests/drvmove.py` and `tests/sndmove.py` are the gates |
| `MEM_K_MOD` on-demand module | per loaded module | **PINNED (forever)** | its base is the module's CS. Claimed top-down |
| `MEM_K_CLONE` disk cloner buffer | up to 640KB, one clone | **UNDECLARED** | transient, and the `int 13h` target throughout — cannot be a barrier longer than the clone |
| `MEM_K_CMPR` Compress working block | twice the file plus up to 40KB, one compress | **UNDECLARED** | transient; three segments are paragraph arithmetic off one base |
| `MEM_K_HIB` hibernate extent list | `hbm_xcap`, the resume stub's lifetime | **UNDECLARED** | claimed on the way into the stub; the machine it is in is about to be overwritten |
| `MEM_K_BAND` band composer buffer | `BAND_KB`, boot to shutdown | **UNDECLARED** | `BAND=1` builds only. Taken once at boot, so it sits on the floor and is not in anybody's way |
| `MEM_P_WSAVE` window raise cache | one per window | **PURGEABLE** | |
| `MEM_P_FATW` FAT window | ~5KB per volume that did not get the `FAT_SEG` pin (the boot volume takes the pin, §18.8.3) | **PURGEABLE** | §18.8.4. It was `MEM_K_FATW`, a long-lived pinned claim and the first bottom-up claim of the boot, with a relocation proc; both were deleted when it became a cache, and `dsk_fatw_demote` now carries the second naming word (`[dsk_fatseg]`) that proc existed for |
| `MEM_P_DIRW` directory read-ahead | 63KB, one 64KB page | **PURGEABLE** | It carries `MC_DMA` over the WHOLE block (`mem_claim_dma_x`, §50.3) so `dsk_runcap` never splits a fill and a fill is exactly one `int 13h`. That is a PLACEMENT constraint and not a chip armed on it — nothing is mid-transfer between two claims — so it is the row to read beside §66.9 reason 2, which states the two as one thing. Since §66.4.2 the tag no longer PINS anything: `mem_cp_dest` honours the page constraint when it places a block, and what keeps an armed claim still is that nobody declared it |

## Package-owned claims

### Declared

| claim | verdict | note |
|---|---|---|
| **Paint** canvas, undo, clipboard, scratch | **MOVABLE** | §66.5.1. `pt_reloc` shifts the per-row segment table (`pt_rowseg`, `PT_CH_MAX` entries) and recomputes `[pt_undelta]`; the BMP save pins the canvas for the write (`pt_pin`/`pt_unpin`) |
| **Paint** GIF/LZW staging buffer | **PINNED (rule)** | `[pt_gbase]` is a paragraph derived off `[pt_gseg]` at four sites, it is the `OSAPI_FILE_READ` target, and it lives for one file |
| **Tracker** module (up to 116KB) | **MOVABLE** | §66.5.2. `trk_reloc` fixes 36 words: `[mp_blobseg]`, 31 `MS_SEG` sample bases, 4 `MP_SEG` channel segments. Declared only after `mp_load` succeeds |
| **Frotz** story (up to 508KB, the largest claim this OS hands out), Z-stack, undo snapshot | **MOVABLE** | §66.5.9. `zf_reloc`. The story costs **three** words — `[zf_sseg]`, the live program counter `[zf_pcseg]` (shifted, never set) and `[zf_sdelta]`, the running total `zi_yield` differences to repair an `ES` pushed before the move. It **cannot** declare `OSAPI_MEM_PARKSAFE` and does not need to (§66.5.9.1) |
| **Frotz** save staging, transcript (`ZI_SCRKB`), picture buffer, `.mg1` probe | **PINNED (rule)** | file-operation targets, or transients freed inside the call that made them |
| **Note Pad** document + undo arena | **MOVABLE** | §66.5.7. `np_reloc`, two words, one proc — `BX` picks between them. A load pins the note for the read (`np_dmov`, §66.5.7.1) |
| **Note Pad** CR/LF staging buffer | **PINNED (rule)** | transient and the `OSAPI_FILE_WRITE` target throughout — pinned by having no declaration, the cheapest correct answer |
| **ArtfulType** document + undo/redo/clip arena | **MOVABLE** | §66.5.7. `at_reloc`, two words; `at_dmov` pins across both file operations |
| **Fractal** run cache | **MOVABLE** | §66.5.7. `fr_reloc`, one word — every cursor into it is an offset |
| **ModPlug** module (up to 116KB) | **MOVABLE** | §66.5.8. `mpp_reloc`, 36 words. §56.1's bill: the replayer is an independent copy of Tracker's at *different strides* (`MPS_SZ` 12, `MPM_CHSZ` 40), so a renamed `trk_reloc` walks the tables wrong and yields plausible garbage |
| every package **region** | **PINNED (forever)** | base is CS |

### Undeclared

Every package below claims heap and declares none of it movable, so each is
pinned by default and is a barrier for as long as it is held. None has been
audited for §66.3's author rules; a row is "nobody has looked", not "it cannot
be done". Sizes are the `equ`s at the claim sites.

| package | claims |
|---|---|
| **Word** | document and CHP arena (grown in lockstep from `WD_KB0`), PAP dictionary 1KB, undo arena, italic glyph table + staging `WD_ITKB` 9KB, `WORD.OVL` image `WD_OVKB` 8KB (a CS, so that one is forever), `WD_LSTGKB` 62KB load staging (transient), a `2*WD_SCHALF` scratch |
| **the typeface cache** (`apps/os88type.inc`, so Word, TeXpad and every other includer) | `TY_FACE_KB` per open face, and it is the one claim in the tree that carries `MC_DMA` for **alignment alone**: it asks `OSAPI_MEM_CLAIM_DMA` for a whole-block head so the base is 512-byte aligned for its file read. It was `TY_FACE_KB + 1` with a hand round-up underneath, and the round-up could never fire — guard 6b makes every claim base `HEAP_SEG + n*MEM_PARA_KB` paragraphs and asserts `MEM_PARA_KB` is a multiple of 32 — so that was 1KB of heap per open face, up to three per Word or CWORD instance, for nothing. The package asserts the alignment now and refuses the face if it ever fails. No chip is armed on it and none ever will be, and since §66.4.2 that no longer pins it: the blanket `MC_DMA` refusal is gone and `mem_cp_dest` places a page-constrained block page-safely. It is still **UNDECLARED** — declaring it wants `TF_CLAIM`/`TF_SEG` per slot plus `[ty_curseg]` and `[ty_psseg]`, which is an audit rather than a proc |
| **Sheet** | staging 32KB ONLY — it is the `ES:BX` of all seven of the package's `OSAPI_FILE_READ`/`WRITE` calls (§66.9 reason 4), so it stays pinned; §66.5.7.1's pin/unpin pair is what it would take. The other five — cells 32KB, text 8KB, borders 4KB, notes 4KB, chart 19KB — are **MOVABLE** now (see below) |
| **Chart** | chart 19KB, staging 32KB |
| **Browser** | link table `BR_LINKKB` 6KB, document up to `BR_DOCMAX` 63KB, line table 8KB, fetch buffer `BR_MAXKB` 32KB |
| **FTPD** | staging `FD_STGKB` 8KB |
| **TeXpad** | source `TP_SRC_KB` 8KB; export `TP_EXP_KB` 24KB (transient) |
| **Audio** | `AP_LA_SZ` 32KB look-ahead ring |
| **Tank Attack** | `TK_SHKB` 16KB CGA/Hercules shadow |
| **Frotz** | scrollback `ZW_SBKB` 24KB (8KB fallback) — `zwin.inc`'s claim, outside `zf_reloc` |
| **Clear Skies** | `CS_SHKB` 16KB 1bpp shadow, `CS_ART_KB` 11KB launcher artwork — both for the life of the instance, and both **pinned by not declaring**, which is the right answer for the shadow rather than an audit nobody has done. `[cs_shseg]` is copied into `[cs_tseg]` at every `fsx` entry and **ES is loaded from `[cs_tseg]` at thirteen sites and kept across the span**, so a move inside a frame is a live segment in a register that no relocation proc can reach — the shape §66.5.9.1's `OSAPI_MEM_PARKSAFE` exists for, and the fsx bracket has no park point. The artwork is a different case and would only cost one word (`[cs_artseg]`, read by three blits and 0 when the claim failed), but it is 11KB claimed at launch and freed with the instance |
| **every C package** (C64, RunCPM, Weave, Loom, CWORD) | **`os88_mem_movable()` EXISTS NOW** (§66.4.1, `%define CC_HAS_ONMOVE` + `void os88_onmove(unsigned was, unsigned now)`), so this row is "nobody has done the audit" rather than "the SDK cannot" — which is a different row and a much cheaper one. Nothing here declares yet. C64's 64KB RAM, RunCPM's 64KB Z80 space, Weave's bundle/VM/canvas/grid, Loom's 29/50/62KB project buffers, and every `apps/os88parts.inc` scratch part are all pinned. **The C OVERLAY is the exception and it is MOVABLE** (§66.4.1, `apps/cc/crt0.asm`): it is claimed top-down because its base is a CS, and `cc_ovreloc` is four bytes falling through into `cc_ovbind` — safe because `cc_ovthunk` DISCARDS the module's CS and rebuilds the `retf` from `[cc_ovseg]` after the call, so no stack ever holds it. `tests/ovlhigh.py` is the gate |

Word is the one worth an afternoon: session-lived, tens of KB,
claimed after the user has been working — exactly the profile that stranded
Tracker's module in the field (docs/FIELD-NOTES.md 2).

### Nothing to declare

Task Manager, Solitaire, Arkanoid, Missile, Tamegram, Minesweeper, Piano,
Recorder, Hello, Calculator, Cyclone, Telnet: no `OSAPI_MEM_CLAIM` in any of
them. The Task Manager's "~7.3KB of heap while open" (§28) is its *region*,
image plus bss, whose base is its CS. (`tests/heapfrag` claims and declares,
and does not ship.)

## Driver-owned claims

| claim | verdict | note |
|---|---|---|
| **SB staging pool** (`SBL_POOLKB` 20KB, stepping down) | **MOVABLE** | §66.5.5. `sbl_reloc`, one word, because a grant is an *offset* and the staging copy is the v3 boundary; every copy into or out of it goes in `SBL_DCHUNK` chunks under `cli`, re-reading `[sbl_poolseg]` per chunk |
| **SB DMA double-buffer** | **MOVABLE, while the chip is idle** | §66.6.4. `MC_DMA` and claimed top-down, and it was PINNED (forever) until the `[drv_wcnt]` gate: a stream lives only while its refill or drain task does (§34.5) and the 8237 is armed only while a stream is open, so zero means nothing is armed and `mem_can_move` refuses any `MC_DMA` claim while it is non-zero. `sbl_ring_reloc` is three words and they were already written — it stores the base and falls through into `sbl_dma_derive`, the factored tail of `sbl_dma_map`. **It is inert on its own**: the ring is claimed straight after the image and sits immediately below it, so it moves when the image does and not before. `tests/sndmove.py` is the gate |
| **HDD** install buffer (`hd_ibufsz` ladder) | **PINNED (rule)** | §66.5.10. An `OSAPI_FILE_READ`/`WRITE` target at all four uses, claimed for one install and freed at its end |
| **HDD** per-partition listing (`HDD_LISTKB` 6KB) | **MOVABLE** | §66.5.10.2. Donated to the kernel by `osapi_vol_add`, so three words name it: `mem_reloc_call` walks the kernel's own holders for **every** move first (`dsk_vtab`'s `DV_SEG` and `[dsk_dseg]`, two rows of `mem_rr_tab`), then the owner's `hd_lst_reloc`, one word. Stays claimed LOW (§50.3.2.1): sent high it cost 9KB |
| **HDD** second image (`HDDTOOL.DRV`) | **PINNED (forever)** | base is CS (§52.11.7). Claimed top-down |
| **RAM disk** second image (`RAMPAGE.DRV`) | **PINNED (forever)** | base is CS (§62.9.9) — `[rd_pfar]` is `PKG_DISP:segment`. Claimed top-down |
| **Sheet** REGION | **MOVABLE** | §66.6.1, and the first region in the tree that is. It hires no worker, stores its own segment nowhere and is `org 0` with no relocation, so every word that names it is the KERNEL's and `mem_region_reloc` is what puts those right. `tests/regmove.py` is the gate |
| **Sheet** cells, text, borders, notes, chart | **MOVABLE** | ~67KB of the ~99, and Sheet hires NO WORKER so `mem_can_move` passes them on `I_TASK = 0xFF` alone — no park is involved at all. `sh_reloc` is a TABLE and not a ladder of compares: it patches **every** word that names the old base, because `os88chart.inc`'s `ch_srcseg` / `ch_stgseg` / `ch_srcseg2` are borrowed second copies and §66.1 is the record of a word-poke design that failed on exactly that. `tests/sheetmove.py` is the gate |
| **RAM disk** store | **MOVABLE** | §66.5.10. `rd_reloc`, one word — nothing outside `ramdisk.asm` sees the arena, and every handle into it is an offset |
| **ETHER.DRV** socket pool | **MOVABLE** | §66.4.1, and it was `UNDECLARED` rather than unmovable for a release: the comment at the claim said *"the card's own descriptors point into these rings and it DMAs into them"* and both clauses are false for an NE2000 — the 8390's two DMA engines are internal to the card, the host side of remote DMA is `in al, dx` / `stosb`, and this driver hooks no vector at all. `sk_reloc` is **one word**, `sbl_reloc`'s shape, because `sk_ring` re-derives ES from `[sk_seg]` on every access and `[sk_base]` is an offset *inside* the block. Still claimed top-down (`OSAPI_MEM_CLAIM_HI`) — placement is a second axis — so it packs UP with the ceiling rather than down into the arena |

---

## Placement is a second axis this table does not have

Every verdict above answers *can the compactor move this block*. Which END of
the heap it was claimed from is a separate question (§50.3.2, and
`OSAPI_MEM_CLAIM_HI` is the top-down door), and PINNED settles the first while
saying nothing about the second.

**The rule is not "pinned belongs high"**, measured rather than assumed. A
pinned claim's cost is decided by **what is above it**: below every movable
claim it costs nothing, because `mem_cp_plan` resumes its fill point above
each barrier and the movables pack straight past; above them it cuts the
arena in two for as long as it is held. The HDD's listing claim is the case
that separated the two while it was still pinned — under the 63KB read-ahead
it was free where it was, and sent high it measured 9KB worse (§50.3.2.1).
`tests/heaphi.py` asserts the compaction number rather than a predicate over
the map, because no predicate over the map tells the two apart. And where a
first-fit claim lands is a property of the session, not of the code: the same
6KB first-fits *above* the view cache on a desktop that has been used
(§66.5.6.2), which is why the listing was made movable after all.

## Regions — the block a package RUNS in (§66.6.1, §66.6.2)

A region is a claim like any other and the same two columns decide it, but the
holder is unusual: **every word that names it is the KERNEL's**, so its
relocation proc is very nearly always a `ret` and `OS88_REGION_MOVABLE` emits
one. What actually decides a region is the *worker*.

| package | region | verdict | note |
|---|---:|---|---|
| **SHEET** | 48.5KB | **MOVABLE** | §66.6.1's first customer, and worker-less, so it moves on `I_TASK == 0xFF` alone |
| **Word** | 56.8KB | **MOVABLE + RESTARTABLE** | the largest region in the tree that hires a worker. `wd_worker` polls four statics and sleeps; a restart costs one poll |
| **Tank Attack** | 30.9KB | **MOVABLE + RESTARTABLE** | a game loop, every value a static; a restart costs one frame. No move row can cover it — it redraws for ever, so `settle` never returns |
| **ftpd** | 28.2KB | **MOVABLE + RESTARTABLE** | `fd_step` is a state machine in statics and the restart lands at the loop top, above it, so a transfer resumes at the step it had reached |
| **Audio** | 30.2KB | **MOVABLE + RESTARTABLE** | hires only when playback starts; until then it is movable on `I_TASK` alone |
| **Browser** | 19.8KB | **MOVABLE + RESTARTABLE** | `br_nstep` banks a generation and every store is guarded on it, so a fetch a restart abandons writes nothing — `br_abort`'s own design |
| **kernel module images** | 2–8KB | **PINNED, and measured harmless** | `CTRL.DRV`, `FORMAT.DRV`, `CLONE.DRV`, `HIBER.DRV` (+ `FILECP`/`FDLG` on kern_small). Claimed top-down, and `mem_claim_1`'s `.hi` arm puts one back at the ceiling when it is dropped and re-taken — so it strands nothing, and the descending pass closes any hole that opens beneath it. Measured: the Control Panel open costs **8.0KB, exactly its own size**, not a trapped run. The one exception used to be a module sitting under an attached `SOUND.DRV`; since §66.6.3.1 that image moves too |
| **driver images** | 48KB | **MOVABLE** | §66.6.3. SOUND 6KB, HDD 8KB, ETHER 18KB, RAMDISK 9KB, NET 6KB, VMMOUSE 1KB (`drv_memk`). Every one of them, the sound driver's included: §66.6.3.1 patches the **sixteen hardware IRQ vectors** instead of refusing on the table, and the copy runs at IF=0 so no ISR can be taken between it and the rewrite. Sixteen and not 256 is load-bearing — unused vectors are SCRATCH, and a full sweep rewrote an XT-IDE option ROM's word and left the machine with no hard disk. The other side of it is a contract: a driver may hook its own IRQ vector and nothing else. `tests/drvmove.py` and `tests/sndmove.py` are the gates |
| **every C package** | — | **MOVABLE** | `crt0.asm` declares it at entry; `cc_regreloc` is `cc_ovbind` where there is an overlay, because its `.res` loop writes the live `CS` into the return vectors and inside a relocation proc that CS is the new base |
| **CWORD**, **RUNCPM** | 60.8 / 56.6KB | **+ RESTARTABLE** | `os88_task_restartable(1)` after the spawn takes; both workers are `for(;;)` polls over statics |
| ArtfulType, Fractal, Frotz, ModPlug, Note Pad, Tracker | | **MOVABLE, NOT restartable** | the six that declare `OSAPI_MEM_PARKSAFE`. That lets the kernel stop the worker while it is blocked in `gfx_lock` — anywhere in its loop, including halfway through a frame or a mixed buffer — so the honest declaration for them is a *window* around `OSAPI_TASK_ALIVE`, not a blanket. Not taken yet |
| WEAVE, C64, LOOM | | **MOVABLE** | C64 and LOOM hire no worker at all and say so in their own source; WEAVE's worker trampolines into `WEAVE.WSM`, so its restartability is that module's |

`tests/regapp.py` reads `MC_RLOC` and `inst_restart` back out of the kernel for
every row above that ships, because a declaration the owner fence refused is
silent from inside the package.

## What limits compaction today

Not the mechanism. Two things, in the order they cost:

**1. A worker that draws and NEVER SLEEPS must declare `OSAPI_MEM_PARKSAFE`**
or its claims are unreachable whenever the triggering claim comes from a
callback holding the gfx lock (§66.5.3/§66.5.4). A worker that sleeps between
passes reaches `OSAPI_TASK_ALIVE` inside `INST_PARKW` (4 ticks) on its own, so
Note Pad, Fractal, ArtfulType and ModPlug move with the gfx-lock park removed
and Tracker does not (§66.5.7.2); all five declare it as a widening. Frotz is
the one app that **must not** (§66.5.9.1): `zx_lock` pushes the program
counter's segment across `OSAPI_GFX_LOCK` by design, and its claims move at
the ordinary `ALIVE` park regardless.

**1a. A driver IMAGE moves now** (§66.6.3) and is refused only by two facts: a
frame standing in it, or `[drv_wcnt]` non-zero — any driver worker alive at
all, because a service task runs *in* the image and its stack carries the
image's CS whether it is parked or not. An interrupt vector used to be a third
and is not any more: §66.6.3.1 rewrites the table rather than refusing on it,
which is what lets `SOUND.DRV` — the only driver in the tree that hooks one,
and it hooks five — move at all. No driver declares anything and none can
forget to: the kernel asks. `drv_load` declares the claim with
`mem_region_reloc`, whose table walk is every kernel word that names an
image.

**2. A driver's claims move only while every `TF_SERVICE` task is parked**,
all-or-nothing, because `TF_SERVICE` is the only handle the kernel has on "a
task running inside a driver" and it does not say *which* one (§66.5.5). One
Sound Blaster stream mid-refill pins the RAM disk's store and the HDD listing
too, neither of which owns a worker. A finer handle is a task-table change,
not a claim-side one.

And behind both: **every undeclared row above.** A pinned block is a barrier
whatever its size — the measured case was a 3KB cache holding 40KB out of
reach, and unpinning it moved a 114KB module and the sound driver's pool on
the very next run with nothing else changed (§66.5.6).
