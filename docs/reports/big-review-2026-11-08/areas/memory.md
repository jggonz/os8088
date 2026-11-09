# The claim heap, XMS and on-demand modules

Reviewer area key: `memory`. Tree: branch `the-big-review` (cut from `elendilon`), 2026-11-08.
Phase tags: **now** = no ABI break; **flagday** = batch with docs/plans/ABI-BREAK-LIST.md; **record** = worth writing down, not recommended.
Byte figures are estimates unless marked measured, and say which section pays: `.cold` is RESIDENT (CLAUDE.md), a module image is not.

## Scope read

| file | lines | read |
|---|---|---|
| `kernel/memory.inc` | 6,617 | every proc, all arms (kern_big, kern_small `%ifndef OS88_COMPACT`, kern_mega `%ifdef KERN_MEGA`, `MEM_ALIGN_CLASS`, `NOCOMPACT`, `HMA_DRV`, `COLD_SLIDE`/`ROM_COLD`) |
| `kernel/xmem.inc` | 847 | whole file, both the kern_big arm and kern_small's `%else` |
| `kernel/mod.inc` | 1,006 | whole file incl. `mod_cfix` (COLD_SLIDE), `mod_hi`/`mod_hiboot` (kern_mega) |
| `kernel/instance.inc` | ~150 | `inst_caller`, `inst_seg_parked`, `inst_park_req` (callees whose contracts memory.inc leans on) |
| `kernel/filecp.inc`, `kernel/compress.inc`, `kernel/deskmod.inc`, `kernel/driver.inc`, `apps/dos/dos.asm`, `apps/os88api.inc` | spot reads | the callers each finding depends on (cited at the finding) |
| SPEC.md | §2.8-2.8.4, §50.2, §50.3.1, §50.3.4, §50.4, §66.3 | read in full; other §50/§66/§41 subsections by heading |
| docs/HEAP-CLAIMS.md | 371 | kernel- and driver-owned rows, "What limits compaction" |
| docs/KERNEL-MEMORY.md | 1,271 | headings and the memory-table references only |
| docs/plans/ABI-BREAK-LIST.md, KERNEL-BYTE-QUEUE.md, docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md | grep for this area | to avoid re-proposing listed rows (C1, R2, R2-6, P2-1 are listed; CODE-REVIEW #6 and #19 are fixed in this tree) |

## Verdict

The heap is the most carefully argued subsystem in the kernel, and its central model is good: an out-of-band record table that *is* the allocator, a two-ended placement policy, purgeability encoded in the owner word at no record cost, and relocation by callback through the holder's own dispatcher rather than by handles. That last choice is the right one for an 8088. It avoids the double indirection that Mac handles and GEOS put on every access. Plan and run share one walk body, and `mem_avail` is defined as "the question `mem_claim` answers". Both close whole classes of "promised room that never arrives" bugs.

The weak points are at the edges, not in the core:
- **Interrupt latency.** `mem_regrow` copies at IF=0, and `mem_avail` walks O(n²) at IF=0.
- **Policy gaps at failure.** A claim that cannot succeed still sheds every cache and can park workers again on each retry.
- **A per-owner cap that also binds the kernel's shared tags.** `MEM_K_DRV` caps the machine at eight driver images.
- **A purge floor** that any teardown lifts.
- **Two places where a fixed bug class survives:** the KB-to-paragraph shift, and the region-ownership widening.

The documentation has drifted in a specific way. Every "size pass N inlined this" leaves a detached header behind, and most of the code/doc mismatches below are in those orphans or in SPEC §66.3, which still describes the pre-park, MC_DMA-pins design. Modules (`mod.inc`) are solid. Hostile-image checks are thorough, and the build plus layout stamps are a model to copy. The real open risk there is drop-while-executing from a worker. `xmem.inc` is a clean overlay shim with three stale sentences.

## Strengths

1. **The record IS the allocator, out of band** (`memory.inc:1064-1258`, SPEC §50.2). Freeing is one word store (`and word [ss:si+MC_SEG],0`), so no free list can disagree with the map. The map is in `.lowbss`, behind `ss:`, and no package overrun can corrupt it. DOS keeps its MCBs in-band, ahead of every block, and a one-byte overrun gives "Memory allocation error, system halted". os8088 cannot fail that way. Keep it.
2. **Purgeability costs zero bytes per record** (`memory.inc:305-351`). The 0xFBxx-0xFExx owner range is the class *and* the priority, and the contract "one kernel word names it, and 0 there already means the slow path" makes the shed's notification a single store (`mem_pg_forget`, `:1456`). Shed order is cheapest-first and rank-bounded (`mem_rank_bh`, `:1379`), so a cache can never evict a dearer one.
3. **Relocation by callback, not handles** (`mem_reloc_call`, `:3839`; SPEC §66.2). The holder's normal access is a plain segment load. Only a move pays, and it pays once, through the same `PKG_DISP` dispatcher every callback uses, so nobody writes a `retf`. The default is PINNED (`MC_RLOC`=0, written fresh on every publish, `:1224`), and that is what made adoption one claimant at a time safe.
4. **Plan and run are one body** (`mem_cp_walk`, `:2917`, BP = 0/1). The plan cannot promise a run the moves do not deliver, by construction. The direction is a *mask* (`mem_cp_msk`), which folds both passes into one scan (`mem_cp_next`'s complemented key, `:2688`). That is genuinely clever and well explained.
5. **The kernel's half is a table** (`mem_rr_tab`, `:3349`; `mem_rr_walk`, `:3143`): one 4-byte row per holder, one loop over four address spaces. It also keeps the hard-won lesson that **the IVT is not a table of pointers**, so only the 16 IRQ vectors plus int 33h are rewritten (`:3123-3141`, `tests/hdmove.py`). That lesson should be quoted wherever anything else proposes a sweep.
6. **`mem_avail` is defined by `mem_claim`** (`:4874-4904`). The answer is modelled on the compaction plus shed that a claim would actually do, and the write-up says why an under-report is the invisible error. kern_small's arm (`mem_bigrun`, `:4329`) answers the same question for a shed-only heap.
7. **The DMA page rule is a bump inside the scan** (`:1153-1171`, `mem_cp_dest` `:2784`), not claim-test-retry. It is monotonic in both directions, and it is honoured again by regrow (`mem_hifit`) and by compaction. A moved claim keeps the property it was granted.
8. **Modules: two stamps and bounded entries** (`mod.inc:105-141`, `:537-616`). BUILD_NUM catches another commit and the section-size sum catches another build of the same commit. Every entry offset is bounded by the header's end and the bytes that arrived, unloaded slots point at a refusing `retf` (`mod_gone`), and `t_moddrop` gates "every module has a drop". `mod_cfix` (`:681`) patches far-call segments from the bytes alone, and `tools/os88rom.py` proves the predicate exact at build time. That is a scan made safe by a build-time proof, which is the right way round.
9. **XMS as an overlay with one gate** (`xmem.inc:40-55`, `:343`). `[xm_kb]` is written last and means both "there is a store" and "the image is attached". The tier-0 refusals are byte-identical to an 8088's, so every package is already exercised against them on the target machine.

## Findings

Ranked by value. "Evidence" cites file:line in this tree.

### mem-1 - `MEM_OWNER_MAX` also caps the kernel's shared tags: the ninth driver image is refused with the heap free (bug, medium, now)
`mem_claim_1`'s owner walk (`memory.inc:1095-1112`) counts every live record whose `MC_OWN` equals BX and refuses at 8 (`:60`), whatever BX is. Every driver image is claimed under the single tag `MEM_K_DRV` (`driver.inc:3513-3515`), and so are the DRVC_OVL images loaded through `drv_load_at`: XMEM.DRV (`xmem.inc:283-285`), SAVER.DRV (`blank.inc:683`) and THMPAL.DRV. kern_big has `DRV_MAX = 9 + DRV_VX + DRV_PX` rows (`driver.inc:80`). The rows are SOUND, HDD, NET/ETHER, RAMDISK, VESA, EMS, CDROM, HTTPFS, USBMOUSE and PCIETHER (`driver.inc:1179-1226`), plus the overlays. On a well-equipped AT (or one where `HMA_DRV` declines), the ninth `MEM_K_DRV` claim fails exactly like "no heap". `MEM_K_MOD` (every loaded module) and `MEM_K_IMG` (every mounted image) share one owner each in the same way. The cap's stated purpose (`:62-74`) is to charge the *app* that floods the table. It has no meaning for kernel tags, whose counts the kernel bounds itself (DRV_MAX, MOD_MAX).
**Recommendation:** skip the cap for `BH = 0xFF` (kernel tags). The purgeable ranges are already separate owners per ordinal. Add a fast-tier assertion that `DRV_MAX + overlays <= MEM_MAX`-share, or a row that loads every driver on an AT profile. **+5 `.cold` (resident).**

### mem-2 - `mem_regrow` runs path 3's copy at IF=0 (bug/hazard, medium, now)
`mem_regrow` opens with `pushf/cli` (`:4559-4560`) and closes it at `:4664`. The window covers `mem_hifit` (O(MEM_MAX²), `:1740`) and `mem_bcopy` of the whole old block (`:4640`). On a 4.77 MHz 8088, `rep movsw` costs about 25 clocks a word, so a 64 KB canvas grow (Paint's path 3, the case the routine exists for) is about 170 ms with interrupts off. A 1200-baud mouse byte arrives every ~7.5 ms into a one-byte 8250 buffer, so that copy overruns roughly twenty bytes. Every SB half-buffer IRQ in that span is also late. The PIC holds only one pending IRQ0, so at 170 ms two ticks are lost from the BIOS clock. The compactor solved the same problem with `[sch_lock]` and IF=1 (`:4040`), and the regrow is the outlier.
**Recommendation:** decide and publish the new base under `cli`, raise `[sch_lock]`, then `popf` and copy at IF=1. The destination is free, nobody else can claim while switching is paused, and ISRs do not claim. Re-enter `cli` only for the record store. With the lock held, `mem_dsk`/`mem_dma` stay this window's. **~+10 `.cold`.**

### mem-3 - A claim that cannot succeed destroys every cache and may park workers once per shed (weakness, medium, now)
On kern_big the retry loop (`:952-989`) runs compact, then shed one, then retry, until nothing is left to shed. `mem_compact` re-arms its park on every call, because `[mem_parked]` is cleared at `.end` (`:4174`). A doomed claim therefore pays up to MEM_MAX sheds, each followed by up to three plans and possibly a park of `INST_PARKW` = 4 ticks (`instance.inc:576`), before CF=1. The caches it destroys include `MEM_P_DIRW`, priced at "seconds" (`:429-436`), and the FAT windows, whose loss is 18.8.1's eviction thrash. A real caller probes exactly this way: `cmz_wclaim` (`compress.inc:3536-3553`) tries the largest window first and halves on refusal. So the first refusal has already shed every cache before a smaller window that fits without shedding is tried.
**Recommendation:** two parts.
(a) Before the first compaction, compare the request against the exact TOTAL that `mem_avail_lvl_p` returns in BX at the claimant's rank. The total has no modelling error (§66.4.3.1's mismatch is about the *run*), so this refusal can never be wrong. **~+14 `.cold`.**
(b) `cmz_wclaim` should size its first try from `mem_avail` (`.cold` caller, a few bytes).
Optionally keep `[mem_parked]` standing for the length of one `mem_claim_x` call, not one `mem_compact` call: one park per claim (~+6).

### mem-4 - The purge floor is lifted by ANY instance's teardown, and its task stamp distinguishes nobody (bug, low-medium, now)
`mmf_osapi_mem_floor` stamps the floor with `[sch_cur]` (`:5850`). Every package callback, the DOS shell and the kernel's own paste run on task 0, so the stamp names "the UI task" and not a claimant. `mem_free_rec_x` then writes `MEM_LVL_TOP` unconditionally (`:4826`) on every instance teardown, including a Disk window closing, and `fcp_lend` does the same (`filecp.inc:2348`). Concrete failure: a multi-file paste raises the floor to MED so that its copy buffer cannot shed `MEM_P_FLIST` (`filecp.inc:452-459`). The user closes any window while an overwrite question is up, which lifts the floor. The next entry's copy-buffer claim, under pressure, sheds the list. `fcp_lnext` finds `[fcp_lseg]` = 0 and ends with DONE (`filecp.inc:532-547`): the paste **silently stops part-way and reports success**. The DOS's documented "set, size, post, return, claim on the wake" (`memory.inc:5840-5845`) can lose its floor the same way.
**Recommendation:** stamp the floor with `inst_caller`'s instance (DH, 0xFF for the kernel) as well as the task, and lift it in `mem_free_rec_x` only when the stamp is the dying instance. Alternatively make the paste's protection a property of the copy-buffer claim (claim it with a rank owner) and not of a machine-wide byte. **~+8 `.cold`.**

### mem-5 - `mem_claim_dma_x`'s head KB-to-paragraph shift has the bit-10 carry bug the other two doors were fixed for (bug, low, now)
`:860-866`: `mov si,cx / mov cl,6 / shl si,cl / pop cx / jnc .set`. On the 8086 CF is only the last bit out, which is bit 10. A head of 1024-2047 KB sets it and is refused (0xFFFF), but 2048 KB shifts to 0 (no constraint), and 2049 KB to 0x40 (a 1 KB head), both granted. The claim KB (`:927`) and `mem_regrow` (`:4562`, CODE-REVIEW-TWO-CYCLES #6) carry `cmp ax, 1024` guards for exactly this. The package door `mmf_osapi_mem_claim_dma` passes CX through raw (`:5254-5266`). The impact is a nonsense request granted with a different constraint, not corruption.
**Recommendation:** `cmp cx, 65 / jae -> si = 0xFFFF` ahead of the shift (a head over 64 KB is refused by `mem_claim_1:1090` anyway). **~+3 `.cold`.** This is also evidence for C1 (mem-18).

### mem-6 - `mem_find_own`'s self-region widening also authorises FREE and REGROW of the caller's running region (hazard, low-medium, now)
`:4415-4422` accepts `DX == BX` ("a record whose base equals the caller's own segment can be nothing but that caller's region") so that `OS88_REGION_MOVABLE` can name the slot-owned region. The same fence serves `mem_free_x` (`:4459`) and `mem_regrow` (`:4576`). So `OSAPI_MEM_FREE DX=CS` frees the region the package is executing in, and `OSAPI_MEM_REGROW DX=CS` with a grow that hits path 3 copies the region elsewhere and returns. CS is still the old base, and the old paragraphs are free heap. Both are "one wrong word" accidents, which is what the fence was written to stop (`:4435-4436`).
**Recommendation:** confine the widening to the movable door. One option is a byte `[mem_selfok]` set by `osapi_mem_movable_x`. Another is to do the slot substitution in `osapi_mem_movable_x` (`mem_owner_of_x` already returns the slot) and delete it from `mem_find_own`. **~+3 net `.cold`.**

### mem-7 - Nothing bounds the INTERRUPT context any more: a block an ISR uses moves at IF=1 unless it is a driver image (hazard, medium, now)
SPEC §66.3 context 2 says ISRs are bounded by "the `MC_DMA` pin". That pin is gone (`:1987-2000`, §66.4.2). The only IF=0 move is `MC_OWN == MEM_K_DRV` (`:2974-2976`). Yet `mem_rr_ivt` rewrites IRQ vectors that name *any* moved block, package regions included (`:3474-3483`, via `mem_region_reloc`, which runs for every move). That rewrite only makes sense if packages may hook IRQs, and they do: DOS chains int 08h with `mov [es:0x08*4+2], cs` (`apps/dos/dos.asm:11445-11446`), and its region is MOVABLE (HEAP-CLAIMS.md:329). Today DOS's hook appears to live only inside its bracket, where nest and worker pins hold. But the rule is held by the coincidence that no movable region hooks a vector at a compaction point. A tick taken mid-copy runs the old copy, and its data writes are lost after the vector is patched. The same holds for any driver ISR touching a movable *data* claim of its driver; the SB pool is safe only because `sbl` re-reads the segment under `cli` per chunk (HEAP-CLAIMS.md:227).
**Recommendation:** move at IF=0 whenever the block's base appears in any of the 17 `mem_rr_ivt` vector segment words. That is one 17-compare scan per move, replacing the owner test, and it subsumes the driver case. Also write the author rule into §66.3: "an ISR may not write a movable claim except through a segment it re-reads with interrupts off". **~+12 `.cold`.**

### mem-8 - O(MEM_MAX²) walks with interrupts off: `mem_avail`, `mem_hifit`, `mem_claim_1` (perf, medium, now - measure first)
`mem_avail_lvl_p` (`:4927-5023`) runs the whole compaction model inside `pushf/cli`: `mem_cp_next` is a full table scan per step (`:2680-2720`), and per live claim it calls `mem_can_move`, which far-calls `cw_inst_ptr` and `inst_seg_parked`, and that one walks `inst_tab` (`inst_of_seg`). A 32-record table is about 1,024 inner iterations plus 32 far-call chains. By arithmetic that is ~8-15 ms at IF=0 on a busy heap, against a 7.5 ms serial byte. Every package sizes itself with this call, and the Task Manager polls it. `mem_hifit` (`:1740`: 33 candidates × `mem_runfree`'s 32) runs under regrow's `cli`. `mem_claim_1`'s bump-restart is bounded by live claims × table size, also under `cli`.
**Recommendation:** measure first. Count MartyPC cycles across one `OSAPI_MEM_AVAIL` on a 20-claim heap, a host-side number for `tests/heapcheck.py`. If it is over ~5 ms, guard `mem_avail` with `[sch_lock]` and not `cli`: its globals (`mem_pg_rank`, `mem_cp_pl`, `mem_cp_key`) are only shared with other *tasks*, and ISRs never read them. **~+5 `.cold`.** See also mem-14, which removes the n².

### mem-9 - Regrow path 3 puts every grower at the TOP, whatever door it came in by (weakness, medium, now)
`mem_hifit` is always highest-fit (`:1712-1724`, `:4599-4616`). A bottom-up data claim that grows past its neighbour therefore lands among the ceiling's regions and driver images. That is the "one long-lived data claim landing mid-heap permanently splits the space a package can be loaded into" which §50.2 and §50.3.2 were written against. Because `MC_DMA_HI` stays clear, the descending pass will not pack it and the ascending pass treats ceiling claims as barriers, so if the grower is pinned it is now a wall above the regions. The "the next grow extends in place" argument (`:1717-1720`) is about the grower, not the heap.
**Recommendation:** honour the door. Take lowest-fit for a bottom-up claim (or the fit that leaves the largest remaining run) and highest-fit only for `MC_DMA_HI`. This is one XOR with the mask trick `mem_cp_next` already uses. **~+8 `.cold`.**

### mem-10 - `mod_drop` can free a module the UI task is executing in, when the drop runs on a worker; and modules are pinned for life for the same missing fact (hazard, low-medium, now / rethink)
`mod_disarm` (`mod.inc:880-886`) documents that a drop can run on a worker (`inst_task_die` -> `desk_drop_x`) "while the UI task far-calls a slot with no lock held (cur_shape_pass's DKD_HOVER)". The `cli` there prevents a torn *slot*, not a freed *image*. `desk_drop_x` keeps the image while any record has `IF_DESK` (`deskmod.inc:118-133`). But the last widget's own worker dying while the UI task is pre-empted inside `dkd_hover` for that widget clears the last `IF_DESK` and frees the code under the UI task's IP. That does not fault (§2.8.3). The same missing fact, "is a frame standing in this image", is why `MEM_K_MOD` is pinned forever (`memory.inc:2049-2050`). Yet a module is named only by kernel words (`mod_fp` slots, `MODR_SEG`), exactly as a driver image is.
**Recommendation (now):** never free a module from a non-UI task. Post the drop (a byte, serviced at `ui_task` step 0, beside `mem_cpq`). **~+12 `.cold`.**
**Rethink (record):** bracket the module thunks with `wm_nest_pushd/pop`, as `xm_dsp` does for the XMS image (`xmem.inc:434-438`). Then `mem_in_nest` answers for modules, `mod_drop` can refuse while one is executing, and a module can move like a driver image with `mod_fp`/`MODR_SEG` rows in `mem_rr_tab`. That would take CTRL.DRV/DOCK.DRV/DESK.DRV off the wall list (HEAP-CLAIMS.md:127). It costs a few bytes per thunk site: price it before choosing.

### mem-11 - The driver-image IF=0 move's latency note is wrong in both directions (weakness, low, now)
`:3080-3088` says ~50 ms is "three ticks" and costs "at most one mouse PACKET". 50 ms is under one 55 ms tick, and the PIC latches one IRQ0, so no tick is lost below ~110 ms. The mouse, though, receives ~6-7 bytes at 1200 baud into a one-byte 8250 during 50 ms, which is about two packets. The SB half-buffer refill and the NIC's receive also stall. The design choice (IF=0 rather than masking lines) is still defensible; the stated cost is not.
**Recommendation:** correct the text (see mismatches). If it matters on the field machine, have the *descending* pass prefer leaving driver images where they are unless the plan needs them to move (the plan knows whether a mover is required). **0 bytes** for the text.

### mem-12 - Orphaned headers of inlined routines are where the documentation rots (consistency, medium, now, 0 bytes)
Ten routines no longer exist as bodies, and their headers stay as free-standing commentary: `mem_frameless` (`:2279-2402`), `mem_busy_seg` (`:2405-2436`), `mem_cp_mine` (`:2151-2170`), `mem_cp_both` (`:2173-2238`), `mem_cp_drop` (`:2595-2633`), `mem_cp_move` (`:3067-3093`), `mem_cp_kill` (`:3095-3101`), `mem_own_drv` (`:6009-6030`), `mem_dmaok` (`:1727-1738`) and `mem_total_kb` (`:5148-5174`). That is roughly 450 lines describing code that lives somewhere else. Six of the mismatches below are in them, because nobody edits a header with no code under it.
**Recommendation:** for each, keep three lines at the call site (what is tested and why, with a § pointer) and move the essay to SPEC §66, where the design record belongs. Make `checkdocs.py` refuse a `; (X is INLINE since size pass N)` header whose name no longer appears as a label *or* in SPEC (cheap and host-only).

### mem-13 - The owner word means four different things, and the region/data split across slot and segment costs code in five places (rethink, low, flagday)
`MC_OWN` can be an instance slot (a region, or a built-in's data claim), a segment (a package's or driver's data, a driver's second image), 0xFBxx-0xFExx (a kernel cache) or 0xFFxx (a kernel tag). The region is owned by the *slot*, the package's data by its *segment* (`mem_own` returns `ES`, `:5955`). That split is why:
- `mem_find_own` needs the self widening (mem-6);
- `mem_rr_tab` carries a `mem_tab + MC_OWN` row (`:3459`);
- `mem_reloc_call` classifies three ways (`:3880-3906`);
- `mem_free_rec_x` walks the table twice (`:4836-4841`);
- `MEM_OWNER_MAX` counts a package's region apart from its data (see mismatch O).

Owning everything a package holds by its slot would delete four of those five (drivers keep segment ownership). The cost lands on `CLS_OWN` readers: the Task Manager bills rows by matching owner to `I_SPTR`.
**Recommendation:** record, and take it with C1 at the flag day if the Task Manager is touched anyway. **est. -20 to -40 `.cold`, unmeasured.**

### mem-14 - Keep `mem_tab` sorted by base (rethink, medium, now on kern_big/kern_small; blocked on kern_mega)
Every walk in the file is O(n²) only because the table is unordered: `mem_cp_next`'s nearest-key scan, `mem_claim_1`'s restart-on-overlap, `mem_hifit`/`mem_runat` candidate × table, and `mem_bigrun`. Compaction preserves order (a claim slides only onto paragraphs the walk has passed, and both passes keep relative order), so a sorted table stays sorted. Insert-on-claim and remove-on-free are a `rep movsw` of at most 320 bytes, on operations that are already rare. With order, first fit is one linear pass, best fit (§50.3.1's "cure 2", never built) becomes nearly free, and the plan is O(n).
**Blockers:**
- kern_mega pairs a selector with each record *index* (`mem_val`, `:4376-4396`, `MEGA_MSELN`), so records cannot shift there without re-keying.
- `mem_skip` and every `SI` held across a call are record pointers.
- The snapshot's row order changes (the Task Manager should not care; verify).

**est. -30 to +20 `.cold`, unmeasured; the win is cycles and the IF=0 time in mem-8.**

### mem-15 - Only the KERNEL can own a cache: there is no purgeable class for a package (rethink / prior-art, medium, now)
The purgeable range is kernel tags only (`:310-319`). A package's own rebuildable caches cannot be offered to the machine, so each is either a wall or freed by hand: a decoder's lookup tables, Sheet's 32 KB staging, an editor's undo, ModPlug's mix buffers. That is what Windows' `GMEM_DISCARDABLE`, the Mac's purgeable handles and GrowZone proc, GEOS's discardable blocks and AmigaOS 3's low-memory handlers all gave applications.
**Proposal:** `MC_DMA` bit 13 is free (a head is at most 0x1000, so bits 0-12; 14 and 15 are used). Let a package mark a *declared-movable* claim purgeable at a rank through an `OSAPI_MEM_P` verb. The shed calls the claim's existing relocation proc with `DX = 0` ("you were purged") through `PKG_DISP` before it frees the block, which is the same protocol, one new value. `mem_pg_cheap`/`mem_shed_one` read the bit and take the rank from the verb (a byte array or the high byte of a spare word).
This is additive: a new verb, so `abilock --update` with no bump. **~+30 to +40 `.cold`.**

### mem-16 - No recency inside a rank: `mem_shed_one` takes the first equal-rank record in TABLE order (weakness / prior-art, low, record)
`:1422-1423` (`jae .scan`) keeps the first-seen candidate among equals. All window raise caches are TRIV, so the cache of the window the user is about to raise is as likely to go as one buried under five others. Windows 3.x discards by LRU, and the Mac purges what the request needs, in address order. An exact LRU costs a byte per record. A cheap approximation for `MEM_P_WSAVE` is that z-order *is* recency: shed the deepest window's first.
**Recommendation:** record. It only matters on small heaps, where it does matter. **+6 to +40, depending on approach.**

### mem-17 - Movable regions could be declared by a header bit instead of 72 call sites (rethink, low, now)
Every package carries `OS88_REGION_MOVABLE` (72 declarations across 67 package sources), 10-19 bytes each in the image, plus a far call at start-up, because "declared and named nothing" must be told apart from "forgot" (`apps/os88api.inc:4829-4836`). A package-header flag bit read by the loader, with `MC_RLOC` set to a kernel sentinel meaning "kernel half only, no owner proc", would make the common case free. `mem_reloc_call` already runs the kernel's half unconditionally (`:3869-3871`). Old packages keep their call, and new packages built with the bit run pinned on an older kernel (the safe direction).
**est. kernel +8 `.cold`; -10 to -19 per package image, ~-1 KB of disk tree-wide; the `t_movable` ratchet then checks the bit.**

### mem-18 - The KB doors keep regrowing the same bug (size, low, flagday) - listed as ABI-BREAK-LIST **C1**
New evidence for C1: the KB-to-paragraph conversion has carried the bit-10 carry defect at three doors (claim AX, regrow AX, and still the DMA head CX, mem-5). Each fix was found separately. The paragraph door has no such shift. Nothing new to propose beyond C1 itself.

### mem-19 - kern_mega `osapi_mem_regrow_x`'s move-down copies the OLD size whatever the new one is (hazard, low, now - kern_mega only)
`:5457-5468` sizes the copy from the old KB (`shl si, 9`) into a conventional claim of the *new* KB. If a shrink ever reaches `.move` (`mega_claimregrowf` refusing for any reason), the copy overruns the new claim. An old size of 128 KB or more also wraps `shl si,9` to 0. Today a pool shrink probably never refuses, but nothing in this routine says so.
**Recommendation:** copy `min(old, new)`. **+4 `.cold` (kern_mega).**

### mem-20 - `xm_release_rec` owns the EMS and NET teardown (consistency, low, record)
`xmem.inc:713-729` issues `EMSV_GONE` and `NETV_GONE` driver calls before the XMS release. Those are per-instance teardown for two unrelated drivers, inside the XMS file, because this routine has the fourth caller (`ld_unreserve`). A future per-instance driver resource will go here too, or be forgotten. The cleaner shape is one "instance gone" broadcast to every published class that has a GONE verb (`drv_pkg_call_x` in a loop over classes). **~±0.**

### mem-21 - XMS blocks are named by linear address and the pool cannot be defragmented (prior-art, low, record)
`XM_MAX_BLKS` = 8 for the machine (`kernel.asm:1948`), and a block is its 32-bit base (`xmem.inc:525-531`). HIMEM's handles plus lock counts let it move unlocked EMBs. Here a pool of 8 blocks with first fit fragments for good. That is harmless at today's two consumers (the cloner, packages' occasional stash). Record it so nobody grows the table without thinking about it.

## Rethinks (summary)

| id | change | phase | est. bytes |
|---|---|---|---|
| mem-14 | sorted `mem_tab`: O(n) walks, best fit for free | now (big/small), blocked on mega's index-keyed pairs | -30..+20 `.cold`, unmeasured |
| mem-15 | package-purgeable claims through the existing reloc proc (DX=0 = purged) | now (additive verb) | +30..40 `.cold` |
| mem-13 | owner = instance slot for everything a package holds | flagday (Task Manager reads `CLS_OWN`) | -20..-40 `.cold` |
| mem-17 | header bit for "region movable, no fix-up" | now | +8 kernel, ~-1 KB of disk tree-wide |
| mem-10 | nest-bracket module thunks: safe drop, and movable modules | record | price per thunk site first |

## Prior art

- **DOS (MCB arena).** In-band headers, owner = PSP segment (os8088's segment-owner is the same idea), allocation strategies first/best/last fit (`AH=58h`), no compaction, no notification. *DOS was better at*: a selectable fit strategy, and an unbounded block count. *os8088 is better at*: an out-of-band map an overrun cannot corrupt, owner reaping at teardown, page-safe DMA placement inside the allocator. *Cheap to adopt*: best fit as a door bit, if mem-14 lands.
- **Windows 3.0/3.1 (real and standard mode).** Fixed, moveable and discardable segments with lock counts, LRU discard, `GMEM_NOTIFY` discard callbacks, fixed low and discardable code high. In real mode it moved *code* even with return addresses on the stack, by walking BP chains, with odd-BP marking far frames. *Better*: applications could own discardable memory (mem-15), and LRU (mem-16). *os8088's refusal of stack walking is right for it*: packages follow no frame convention, and the INC BP prologue costs bytes in every far proc. *Worth stealing*: discardable-code-with-reload-thunk is exactly `mod_fp` + `mod_gone` + `mod_need`. Windows added automatic discard by knowing which segments were on the stack, which is mem-10's rethink.
- **OS/2 1.x.** Protected mode only, so segment motion is transparent behind descriptors, with `DosReallocSeg` and huge segments. kern_mega's selector model (`mem_val`, §50.6.12) *is* this model, and its conventional-memory compactor stops needing holder procs there (`:2995-2999`). Nothing further to adopt cheaply on an 8088.
- **Mac System 5-7 Memory Manager.** Handles and master pointers (double indirection), HLock/HPurge, MoveHHi (move to the top before locking, the same "pinned goes high" instinct as §50.3.2), GrowZone (an application proc called when a request fails) and purge-warning procs. Its segment loader's UnloadSeg was *application-explicit*, exactly like `mod_drop` (§2.8.3). *Better*: GrowZone and application purgeables (mem-15), and MaxMem/CompactMem as explicit queries (os8088 has them: `OSAPI_MEM_AVAIL`, `OSAPI_MEM_COMPACT`). *os8088 is better*: no double indirection on every access, which on an 8088 is a real per-access tax.
- **GEOS (the 8088 peer).** Handle-based movable, discardable and swappable blocks locked around use, LMem local heaps inside blocks, VM files swapping blocks to disk or EMS. GEOS paid the lock/unlock discipline on every access path. os8088's callback model is cheaper per access but cannot *swap*. Its "store above 1MB" and EMS are manual. *Worth noting, not adopting*: swap-to-EMS of a purgeable class (mem-15) would be GEOS's discardable-block model with EMS as the backing store, and it is too large for now.
- **AmigaOS.** `AllocMem` with attribute flags (MEMF_CHIP for DMA-reachable is the analogue of the DMA door), no owner tracking, so a crashed task leaks; low-memory handlers from 3.0. *os8088 is better*: owner reaping. *Amiga was better*: memory attributes as flags on one call, which is what C1 plus `OSAPI_MEM_P`'s verb bits already approximate.

## Doc/code mismatches

Each row: file:line, exact current text, what the code does, proposed text. (dated docs excluded.)

- **A.** `kernel/memory.inc:5` - "handed out in KB-granular, segment-aligned blocks". The granule is `MEM_GRAN` = 32 paragraphs (512 bytes) (`:458-460`), and the paragraph doors ask for less than a KB. Proposed: "handed out in granule-aligned blocks (MEM_GRAN, 512 bytes - SPEC.md 50.2.1)".
- **B.** `kernel/memory.inc:76` and `:487` - "size in paragraphs (KB << 6)". The size is any whole number of granules (`:933-937`). Proposed: "size in paragraphs, a whole number of MEM_GRAN granules (SPEC.md 50.2.1)".
- **C.** `kernel/memory.inc:77-78` - "owner - 0..INST_MAX-1 is an instance". The code also has segment owners (`mem_own` returns ES, `:5955`; `mem_reloc_call .pkg`, `:3891-3892`) and the purgeable range. Proposed: "owner - 0..INST_MAX-1 an instance slot (a package's REGION, a built-in's data); a SEGMENT (a package's or driver's data claim, SPEC.md 50.2); 0xFBxx..0xFExx a purgeable kernel cache (50.6); 0xFFxx a kernel tag".
- **D.** `kernel/memory.inc:784-787` - "a package region can NEVER move, because its base IS its CS". Regions move when frameless (`mem_can_move .frameless`, `:2065-2083`; `mem_region_reloc`). Proposed: "a package region moves only when no frame stands in it and its worker is restartable (SPEC.md 66.6.1/66.6.2), and a move costs its holder's proc and mem_region_reloc's walk - so the CS-based claims still go high, to keep them out of the busiest traffic (SPEC.md 50.2's note)".
- **E.** `kernel/memory.inc:1947-1948` - "MC_DMA != 0 is pinned outright". `mem_can_move` no longer tests MC_DMA (`:1987-2000`). Proposed: "INTERRUPTS - not bounded by sch_lock; only a driver image moves at IF = 0 (mem_cp_walk), and an ISR that writes a movable claim must re-read its segment with interrupts off (SPEC.md 66.3)".
- **F.** `kernel/memory.inc:1943-1945` - "pins a claim whose owner has a live worker until SPEC.md". The park handshake exists (`inst_park_req`, `inst_seg_parked`). Proposed: "...reach a task already suspended mid-access; such a claim moves only once its worker has PARKED (SPEC.md 66.5, inst_seg_parked)".
- **G.** `kernel/memory.inc:1970` - "nothing declares a region or a DMA buffer movable". 72 region declarations exist (`OS88_REGION_MOVABLE`), and the SB pool declares itself movable (HEAP-CLAIMS.md:227). Proposed: "Some of the tests below guard declarations a correctly-behaved holder never makes".
- **H.** `kernel/memory.inc:2110-2111` - "a driver's image - answers CF = 1". `inst_seg_parked .drv` answers CF = 0 when every TF_SERVICE task is parked and no package worker is inside a driver verb (`instance.inc:700-718`). Proposed: "A segment matching no instance is a driver's: it answers CF = 0 only when every TF_SERVICE task has parked and no package worker is inside a driver verb (SPEC.md 66.5.5, K9)".
- **I.** `kernel/memory.inc:2312-2313` - "parked at OSAPI_TASK_ALIVE\" and raises", and `:2380-2381` "and it raises". `inst_seg_parked` never writes `[mem_wpin]` (grep: only memory.inc names it); `mem_can_move .busy` does (`:2112`). `:2926`'s "mem_busy_seg raises" names a routine that no longer exists. Proposed: "...and mem_can_move's .busy arm raises [mem_wpin] when it refuses".
- **J.** `kernel/memory.inc:2417` - "A DRIVER's segment is pinned unconditionally", and `:2432` "Both of these lift when SPEC.md 66.5's park handshake lands". Both lifted (see H). Proposed: delete the paragraph (mem-12), or "A driver's segment moves only while all TF_SERVICE tasks are parked (66.5.5)".
- **K.** `kernel/memory.inc:3207` - "SPEC.md 66.6 lists twelve holders", and `:3218` "THREE ADDRESS SPACES, which is why there are three loops." It is one loop (`mem_rr_walk`) over four address spaces (`:3115`, `:3236-3253`), and `mem_rr_tab` has more than twelve rows. Proposed: "FOUR ADDRESS SPACES, ONE LOOP: mem_rr_tab's groups for .bss (DS), .lowbss (SS) and the IRQ vectors (segment 0), walked by mem_rr_walk".
- **L.** `kernel/memory.inc:3083-3085` - "~50 ms - three ticks" ... "and at most one mouse PACKET". 50 ms is under one 55 ms tick and the PIC latches one IRQ0, while a 1200-baud mouse delivers ~6 bytes into a one-byte 8250 buffer. Proposed: "~50 ms for ETHER's 18KB - under one tick, which the PIC holds pending, so the clock loses nothing; the serial mouse loses about two packets (mou_isr resyncs on the framing bit), and a sound refill IRQ is ~50 ms late".
- **M.** `kernel/memory.inc:6285` - "first heap paragraph = HEAP_SEG". It is `mem_floor_ax`'s answer, raised past stage 2's blob until `mem_unblob` (`:611-621`, `:672-690`). Proposed: "the arena's first paragraph - the floor (HEAP_SEG, or a rung under it, mem_floor_ax), and past stage 2's blob until mem_unblob".
- **N.** `kernel/memory.inc:6417` - "The claim heap's unit is the kilobyte". The unit is the paragraph and the granule 512 bytes. Proposed: "The KB doors' unit is the kilobyte (the heap's own is the paragraph, in MEM_GRAN granules)".
- **O.** `kernel/memory.inc:70-74`, and SPEC.md §50.2 "plus its region and one spare" - "Eight is Paint's measured peak ... with its region and one spare". The region is owned by the *slot* and data by the *segment*, so `mem_claim_1`'s count (`:1106-1108`) never counts the region with the data. Proposed: "Eight DATA claims per package - Paint's measured peak of six plus two spare; its REGION is owned by its instance slot and is counted apart".
- **P.** `kernel/memory.inc:211-212` - "because its base IS its CS and so it can". Driver images are CS-based and move (§66.6.3). Proposed: "claimed top-down like a driver's, and PINNED (mem_can_move) because nothing brackets a call into a module, so no test can say no frame stands in it".
- **Q.** `kernel/mod.inc:152-153` - "is THREE, so the pad was wasting six." `MOD_MAX` is 10 or 11 (kern_big) and 6 or 7 (kern_small) (`mod.inc:41-73`). Proposed: "...the live part had been the first four bytes for a while, and the pad was two bytes a row".
- **R.** `kernel/mod.inc:986` - "Every MODR_SEG must read mod_gone before the first thunk runs". MODR_SEG is a segment that rests at 0 meaning "not loaded" (`:407`); it is the `mod_fp` *slots* that must point at `mod_gone`. Proposed: "Every mod_fp slot must point at mod_gone before the first thunk runs".
- **S.** `kernel/mod.inc:470` - "including the compaction (memory.inc:520)". `mem_claim_x` is at memory.inc ~880-996. Proposed: "including the compaction (memory.inc, mem_claim_x)".
- **T.** `kernel/xmem.inc:19` - "DRV_MAX is still 4 and DRVC_MAX is still 5." kern_big DRV_MAX is `9 + DRV_VX + DRV_PX` (`driver.inc:80`) and DRVC_MAX is 10 (`driver.inc:226`). Proposed: "It takes no drv_tab row and no class slot, so DRV_MAX and DRVC_MAX do not count it".
- **U.** `kernel/xmem.inc:704-708` - "ONE TRAP, and it is not this routine's: a block claimed in a package's ENTRY PROC is stamped 0xFF". The loader stamps the entry proc's dispatch (`instance.inc:1013-1020`, `inst_caller`'s `[snd_inst]`), so an entry-proc block carries the instance. Proposed: delete the paragraph, or "An entry-proc block is stamped with its instance since loader_run step 8 brackets the entry (SPEC.md 41.5.1), so it is released here like any other".
- **V.** `kernel/xmem.inc:688` - "(loader.inc:157)". `ld_unreserve`'s call is at `loader.inc:236`. Proposed: "(loader.inc, ld_unreserve)".
- **W.** SPEC.md §50.2 (line 98903) - "`MEM_MAX` (**32**) records × **8** bytes". `MC_SIZE` is 10 (`memory.inc:125`), with MC_RLOC at 8, and kern_small has 16 records (`:34`). The layout block also omits MC_RLOC and MC_DMA's bits 14/15, and says "(KB << 6)". Proposed: "`MEM_MAX` (**32**; **16** on kern_small) records × **10** bytes" with rows for `MC_RLOC 8 word relocation proc, 0 = pinned (66.2)` and `MC_DMA bits 0-13 head, bit 14 DISK class, bit 15 top-down door`.
- **X.** SPEC.md §50.2 (lines 98974, 98980) - "`MEM_K_SAVE` | `MENU_SAVE_KB` = 20" and "a Disk window reads the global mount snapshot instead of a cache". `MEM_K_SAVE` does not exist; the save-under is `MEM_P_MSAVE`, purgeable TRIV (`memory.inc:174-179`), and there is no mount snapshot (`memory.inc:267-271`). Proposed: replace the row with `MEM_P_MSAVE` (purgeable, movable on kern_big) and the fallback sentence with "a Disk window re-reads the volume into a fresh claim (22.6)".
- **Y.** SPEC.md §50.2 (line 98983) - "Every operation runs inside one". Compaction runs under `[sch_lock]` with IF=1 (`memory.inc:4040`), and only driver-image moves are at IF=0. Proposed: "Every map operation runs inside one pushf/cli...popf window; the compactor holds [sch_lock] across its plan and moves with interrupts on, and moves a driver image at IF = 0 (66.6.3.1)".
- **Z.** SPEC.md §66.3 (lines 118225-118231, 118250-118258) - "**`MC_DMA != 0`** — a bus master may be mid-transfer", "**a purgeable cache** — it places itself top-down", "until §66.5's park handshake exists", "A **driver's** segment is pinned unconditionally", "which is what the `MC_DMA` pin above is for". All are contradicted by `mem_can_move` (`:1984-2148`): MC_DMA is not tested; a cache with a proc moves (50.6.7); regions and driver images move (66.6.1, 66.6.3); parks exist. Proposed: rewrite the pinned list as `mem_can_move` has it (no MC_RLOC; a disk transfer's target, `mem_in_xfer`; a module image; a region with a worker and no restart point, a frame in it, or the loader standing in it; a driver image with a frame in it or any driver worker; a package or driver claim whose worker may be running), and state context 2's rule as in E.
- **AA.** SPEC.md §2.8.1 (line 1766) - "`MOD_MAX` is 10 on `kern_big` (nine images and". `MOD_SER` (SERIAL.DRV, both builds) makes ten images and `MOD_SETS` = 10 on kern_big (`mod.inc:51-52`, `:68-70`). kern_small has six images (`:42-43`), and FSCHECK.DRV takes the id after them. Proposed: "`MOD_MAX` is 11 on kern_big (ten images, `MOD_SER` 9, and `MOD_SETS` 10) and 6 on kern_small (7 with OS88_SHORTCUTS)".
- **AB.** docs/HEAP-CLAIMS.md:118 - "`MEM_K_SAVE` menu save-under". It is `MEM_P_MSAVE`, purgeable at TRIV and movable on kern_big (`memory.inc:174-179`). Proposed: "`MEM_P_MSAVE` menu save-under | ... | **PURGEABLE (TRIV) and MOVABLE on kern_big**".
- **AC.** docs/HEAP-CLAIMS.md:125 and :317 - "the sixteen hardware IRQ vectors" / "a driver may hook its own IRQ vector and nothing else". `mem_rr_ivt` also rewrites int 33h (`memory.inc:3476-3483`) for USBMOUSE.DRV. Proposed: "the sixteen hardware IRQ vectors and int 33h (§66.6.3.1)" / "a driver may hook its own IRQ vector, and the pointing class int 33h, and nothing else".
- **AD.** docs/HEAP-CLAIMS.md:141 - "(`mem_claim_dma_x`: `MC_DMA` pins it, and a shed is the way out)". MC_DMA no longer pins (`memory.inc:1987-2000`); the landing is pinned because it declares no proc. Proposed: "(claimed page-safe through `mem_claim_dma_x` and never declared movable, so it is pinned; a shed is the way out)".

## Open questions

1. mem-1: on which profile does a full driver set exceed eight `MEM_K_DRV` claims with HMA_DRV in force? (`vm/386-*` with every Drivers tick and Sound in RAM.) It is the cheapest fix in this list once confirmed.
2. mem-8: the actual MartyPC cycle count of `OSAPI_MEM_AVAIL` on a 20-claim heap. It decides whether `cli` becomes `[sch_lock]`.
3. mem-14: does anything outside `memory.inc` rely on a record's *index* staying put (other than kern_mega's `mem_sel` pairs)? `tools/heapmap.py` and the Task Manager's hash read the snapshot in table order.
4. mem-7: does DOS (or any movable region) ever hold an IRQ vector outside a bracket where `mem_in_nest` pins it? If never, the cheap fix is the SPEC rule and a gate, not code.
5. mem-10: is `desk_drop_x` from `inst_task_die` reachable while the UI task is inside `dkd_hover` for the dying widget, or does something serialise them that this review did not find?

## Verification

Adversarial pass, 2026-11-08: every bug/hazard and every high/medium finding re-read against the code (all build arms); low-severity non-bugs spot-checked. No finding is already fixed, and none is in docs/plans/ABI-BREAK-LIST.md or KERNEL-BYTE-QUEUE.md except mem-18 (C1). Machine-readable verdicts and the applied-ready doc fixes are in `data/memory.json`.

- **mem-1** CONFIRMED - the owner walk has no kernel-tag exception; HMA_DRV is ROM_COLD-only, so every image is a MEM_K_DRV heap claim. Latent until XMEM.DRV plus eight drivers are loaded at once.
- **mem-2** CONFIRMED - mem_regrow's single pushf/cli covers mem_hifit and the whole mem_bcopy.
- **mem-3** CONFIRMED - compact/shed/retry has no total-size precheck, and [mem_parked] is cleared at every mem_compact exit.
- **mem-4** CONFIRMED - the floor is one machine-wide word stamped with [sch_cur], and both mem_free_rec_x and fcp_lend lift it unconditionally.
- **mem-5** CONFIRMED (low) - CX = 2048 gives head 0, 2049 a 1KB head.
- **mem-6** CORRECTED, severity medium -> low - the arm is a documented choice (loader.inc:1690-1695), and mem_owner_of_x depends on the DX = BX match, so the proposed deletion from mem_find_own would break it. A fix belongs in the package free/regrow doors.
- **mem-7** CONFIRMED (latent) - no MC_DMA pin, IF=0 only for MEM_K_DRV, and the IVT rows patch any block; whether DOS's int 08h hook ever stands outside a pinning bracket is unproven.
- **mem-8** CONFIRMED - the cli window is real; the 8-15 ms figure is arithmetic, not measured.
- **mem-9** CORRECTED, severity medium -> low - mem_hifit picks the BOTTOM of the highest free run that fits, which is usually directly above the data stack; a grower lands among ceiling claims only when a hole there fits it.
- **mem-10** CONFIRMED (low).
- **mem-11** CONFIRMED - 50 ms is under one tick; the mouse loses about two packets. Doc fix included.
- **mem-12** CONFIRMED - none of the ten names is a label; their headers remain.
- **mem-13** CONFIRMED (spot-check).
- **mem-14** CONFIRMED (spot-check).
- **mem-15** CONFIRMED (spot-check) - MC_DMA bit 13 is free.
- **mem-16** CONFIRMED (spot-check).
- **mem-17** CORRECTED - 69 OS88_REGION_MOVABLE sites in 67 files, not 72.
- **mem-18** unchecked (C1 evidence).
- **mem-19** CORRECTED - wider than stated: every regrow of an FH32/FH16/bridge huge block reaches .move (.hnol -> .nol), shrinks included; 128KB+ copies nothing, and 65-127KB runs a 16-bit offset past 64KB. kern_mega only.
- **mem-20** CONFIRMED (spot-check).
- **mem-21** unchecked (record).

Doc mismatches - 40 fixes confirmed (exact text checked unique, code comments change comment text only):

- **A-N, P-V, W, Y, AB, AC, AD** CONFIRMED, replacement text refined. Two were split or widened: D (MC_OWN) also covers CLS_PARA's twin of B, and E/F (the OTHER TASKS / INTERRUPTS lines) became one fix. G now reads "Some of the tests below can never fire... nothing declares a module image movable, for one". H's two lines and J's last paragraph ("inst_seg_parked pins it outright", "Both of these lift when ... lands") are fixed too. The memory.inc:781 lead-in "only one of them can ever move" goes with the region sentence.
- **K** CORRECTED - it is three address spaces (DS, SS, segment 0), not four. The fix says "one loop, called once per address space" and names drv_tab and int 33h.
- **O** CONFIRMED, and SPEC.md 50.2's "plus its region and one spare" is fixed with it.
- **X** CORRECTED - the MEM_K_SAVE row is fixed: MEM_P_MSAVE, sized to the menu, at most MENU_SAVE_KB. The "global mount snapshot" fallback sentence is NOT rewritten, because the replacement behaviour of a refused listing claim was not confirmed.
- **Z** CORRECTED - SPEC 66.3's pinned list, context 1's driver paragraph and context 2 are rewritten as mem_can_move has them. The proposed new ISR author rule is not added: it is a design decision (mem-7), not a doc fix.
- **AA** CORRECTED - kern_big defines OS88_SHORTCUTS unconditionally (kernel.asm:961-963) and kern_small never does, so the fix says 11 / 6 with MOD_SER 9 / 5 and drops "(7 with OS88_SHORTCUTS)".
- **Verifier-found:** the memory.inc MC_DMA comment's "Bits 0-14 are the ... HEAD" is wrong (MC_DMA_HEAD = 0x3FFF, bit 14 = MC_DISK), and HEAP-CLAIMS.md:317's "sixteen hardware IRQ vectors" and "a driver may hook its own IRQ vector and nothing else" miss int 33h. All three are fixed.
- Not taken up: memory.inc:3207 "twelve holders" and mem_rr_walk's own "ONE LOOP FOR FOUR ADDRESS SPACES" (the second header lists three groups, and the count could not be settled without guessing the author's fourth). instance.inc's inst_seg_parked header ("A segment that matches no instance answers CF = 1") is just as stale, but it is outside this area's files.

No finding was REFUTED outright.
