# Every claim in the tree, and whether the compactor can move it

The per-claim inventory behind SPEC.md §66. §66.3 is the mechanism
(`mem_can_move` is the one predicate and every pinning rule is in it), §66.9
is the shape of what stays pinned and why; this file is the register, one row
per claim, so "can this move?" is looked up rather than re-derived.

**The pins are RATCHETED** (docs/plans/NO-PINNING-PLAN.md 4.1). Every claim
site in `apps/`, `drivers/` and `kernel/` either declares itself movable or has
a line in **`tests/pins.txt`** with its bucket - M a module, D a device armed on
it, C in use right now, plus B, S, X and N (no valid reason yet) - and a reason.
`tests/unit/t_pins.py` is the host scan and `tests/pinsrt.py` reads the same
buckets back out of `mem_tab` on MartyPC; both are the full tier. A pinned row
below and its `pins.txt` line say the same thing, and a claim that starts to
declare fails the gate until its line goes.

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
module. **Two are persistent now**: `DOCK.DRV` for as long as an advanced Dock
setting stands (§30.5) and `EXTD.DRV` for as long as the desktop is extended
(§39.19.6) - 3KB and 2KB claims, taken top-down, so each is a small wall at the
ceiling for the session rather than one in the middle of the arena, and neither
has been given a relocation proc.

**On `kern_small` NOTHING in this file moves** (§66.0). The compactor is
`kern_big`'s: every claim there is born pinned and stays pinned, `mem_can_move`
and the whole `mem_cp_*` walk are compiled out, and `OSAPI_MEM_MOVABLE` answers
CF = 1. Read every **MOVABLE** verdict below as "on `kern_big`" unless the row
says otherwise. What that machine keeps is **purging**, which reaches every
`MEM_P_*` row unchanged — and the Disk window's listing cache moved from the
movable column into the purgeable one to make the trade worth taking
(§50.6.5).

**Purgeable caches are refused by `mem_can_move` too**, and it costs nothing:
a cache that is in the way is dissolved by `mem_cp_drop` when a claim is
waiting and outranks it (§66.10.1), which is the same room at none of the copy.

---

## Kernel-owned claims

| claim | size / lifetime | verdict | why, and what it would take |
|---|---|---|---|
| `MEM_K_SAVE` menu save-under | `MENU_SAVE_KB` = 20KB clamp; **measured 3.0KB** for a real menu | **MOVABLE on kern_big** | `menu_reloc`. Mid-arena at exactly the moment a *menu command* claims. **PINNED on kern_small** — §66.0 compiles the compactor out there, so the proc and its declaration go with it |
| `MEM_K_ASC` ASSOC.DAT buffer | `ASC_KB` = 5KB, **transient — one call** | **MOVABLE** | `asc_reloc`. **It is a FILE BUFFER now** (§54.7.4): `asc_use` reads the file into it, absorbs the bodies into §25.9's store, and `asc_drop` frees it before it returns, so it never outlives one mount. It used to be claimed on a volume switch and kept for the session, landing mid-arena on a used machine and measured holding 40KB out of reach; the declaration stays because `dsk_read_chain` reaches the sector cache's own claims and those can compact (§18.95), so the block can still move between the claim and the free |
| `MEM_K_ASC` the ASSOC.DAT flush's work buffer (`kernel/ascw.inc`) | `ASW_KB` = 12KB, **transient — one call** | PINNED | the old file, the new one, one directory's package list and the root's folders, claimed by `FORMAT.DRV`'s `ascw_flush_x` and freed before it returns (§54.7.6.4). Not declared because nothing outside the call ever sees it and every offset into it is live across the whole call, `dsk_read_chain` and the write included; it is a BARRIER for the seconds a flush lasts and never longer |
| `MEM_K_CLIP` clipboard | sized to contents, long-lived | **MOVABLE on kern_big** | `clip_reloc`. Outlives the app that filled it (§55). `clip_put` pins its *source* through `[mem_pinseg]` across its own claim (§66.5.6) — and that word is the compactor's, so on **kern_small** the pin, the proc and the declaration are all compiled out (§66.0) and the claim is PINNED |
| Disk window view cache, **kern_big** (owner = the window's instance slot) | `VIEW_KB` = 3KB per open window | **MOVABLE** | `fm_reloc` — `FS_VSEG` **and** the `[fm_vseg]` mirror, the pair that killed the word-poke design (§66.1). Declared from the claim site with the owner in hand (§66.5.6.2) |
| Disk window view cache, **kern_small** (owner = `MEM_P_VIEW` + the window's `fm_pool` slot) | 2KB per open window, up to four | **PURGEABLE** | §50.6.5. A cache that can be SHED does not need to be MOVED, and on a 48.5KB heap two of these stranded **21.5KB** between them as movable claims. Rank `MEM_PG_LOW` because it self-heals — `fmv_fit`'s only caller is `fmv_store`. `fmv_demote` is the second naming word, `dsk_fatw_demote`'s twin; `fm_kinit` frees the previous tenant's, because a tag is nobody's instance and `mem_free_rec` cannot reap it |
| `MEM_K_COPY` copy buffer | one Cut/Copy/Paste | **PINNED**, and `N` in `tests/pins.txt` | claimed through `mem_claim_dma` with the whole buffer as the page-safe head (§22.5.1). The transfer is pinned by `mem_in_xfer` anyway; what is not momentary is that it is held across a suspended overwrite QUESTION, a user wait - NO-PINNING-PLAN K4b, a one-word proc (~17 bytes), waiting for a size pass |
| `MEM_K_DRV` driver image | per loaded driver | **MOVABLE** | §66.6.3. Its base is the driver's CS, and a CS moves when nothing anywhere refers to it — which for a driver image means the kernel's own tables (`mem_region_reloc` walks all 66 words in 9 of them), the sixteen hardware IRQ vectors (§66.6.3.1) and nothing else, because no driver declares anything and none can forget to. Refused only while a frame is standing in it or `[drv_wcnt]` is non-zero. `tests/drvmove.py` and `tests/sndmove.py` are the gates |
| `MEM_K_MOD` on-demand module | per loaded module | **PINNED (forever)** | its base is the module's CS. Claimed top-down |
| `MEM_K_CLONE` disk cloner buffer | up to 640KB, one clone | **UNDECLARED** | transient, and the `int 13h` target throughout — cannot be a barrier longer than the clone |
| `MEM_K_CMPR` Compress working block | twice the file plus up to 40KB, one compress | **UNDECLARED** | transient; three segments are paragraph arithmetic off one base |
| `MEM_K_HIB` hibernate extent list | `hbm_xcap`, the resume stub's lifetime | **UNDECLARED** | claimed on the way into the stub; the machine it is in is about to be overwritten |
| `MEM_K_FONT` the 8x8 glyph table, copied | 1KB (760 used), boot to shutdown - **only on a machine whose planar ROM does not carry the adapter BIOS's glyphs**, or a `FONTSLOW=1` build | **UNDECLARED** | §6.0.1. Claimed TOP-DOWN at boot, before any driver, module or region exists, so it takes the arena's ceiling and everything later stacks under it: never a barrier to either compaction pass, and a proc for it would be resident bytes buying nothing. `[font_seg]` is the one word naming it. `hbm_wake` refills it in place on a resume |
| `MEM_K_SC` the desktop shortcuts' table | 1KB for 1-8 shortcuts, 2KB for 9-16 - only while one exists | **MOVABLE**, `sc_reloc` | §26.8.1. Made at boot by the `.ovl` reader when SYSTEM.CFG carries shortcuts, or at the first drop, so a boot claim would otherwise be a wall low in the arena for the session. `[sc_seg]` is the one word naming it, and the proc is that one store |
| `MEM_K_SC` the SYSTEM.CFG trailer's compose buffer | up to 3KB, for the length of one write | **PINNED**, on purpose | §26.8.6. `CTRL.DRV`'s `sc_m_ser` builds settings + rows + footer in it and `sc_m_serx` frees it after `dskw_write_sys` returns. The write is READING it, so a compaction the write triggers must not move it |
| `MEM_K_IMG` a mounted image's run table | 1KB a mount, for as long as it is a drive | **MOVABLE** once mounted (NO-PINNING-PLAN K2) | §18.101. `DISKOPS.DRV`'s `clo_mount` claims it top-down and the image's row names it in `IMG_DVSEG`; `img_reloc` re-points that word, declared at `.marked`. It was pinned for the mount's life on the ground that `dsk_xfer`'s `.img` arm holds its segment in DX across a nested transfer that can claim - K2 re-reads `IMG_DVSEG` after the nested transfer instead, and a mid-session mount stopped being a wall. Pinned only by `[mem_pinimg]` for the mount itself |
| `MEM_K_MTBL` kern_mega's descriptor tables in conventional memory | 82KB, from `mega_enter` to shutdown, **only on a 286 whose extended memory cannot hold them** | **PINNED**, and `N` in `tests/pins.txt` | claimed top-down and pinned (`mega_tlowf`). It looked like a ceiling claim and is not: `mega_enter` runs after `drv_boot`, so the boot drivers' images sit above it and unloading one opens a hole over the wall (NO-PINNING-PLAN 5 #6). K3 - claim it before `drv_boot`, or a proc that reloads GDTR/IDTR/the LDT - is deferred as niche |
| `MEM_K_MOD` the Sticky Apps panel's second save-under (`[stm_sseg2]`) | the panel's rect, while it is open | **PINNED**, and `N` in `tests/pins.txt` | `stm_open` takes two save-unders: the first is `menu_drop`'s own (`[menu_sseg]`, `MEM_P_MSAVE`, purgeable and moved by `menu_reloc`), the second has no proc. Held across a user wait, so not bucket C - and tagged `MEM_K_MOD` though it is no module, which a reader of `mem_tab` will take for one |
| `MEM_K_BAND` band composer buffer | `BAND_KB`, boot to shutdown | **UNDECLARED** | `BAND=1` builds only. Taken once at boot, so it sits on the floor and is not in anybody's way |
| `MEM_P_WSAVE` window raise cache | one per window | **PURGEABLE** | |
| `MEM_P_FATW` FAT window | ~5KB per volume that did not get the `FAT_SEG` pin (the boot volume takes the pin, §18.8.3) | **PURGEABLE** | §18.8.4. It was `MEM_K_FATW`, a long-lived pinned claim and the first bottom-up claim of the boot, with a relocation proc; both were deleted when it became a cache, and `dsk_fatw_demote` now carries the second naming word (`[dsk_fatseg]`) that proc existed for |
| `MEM_P_FLIST` the Disk window's multi-file clipboard (kern_big) | exactly 1KB (64 records of 16 bytes), from a Cut or Copy of a SET until its paste - **cleared on paste** | **PURGEABLE at MED**, not movable | §22.27. One word names it (`fcp_lseg`, `mem_pg_own`'s row) and its zero already means an empty clipboard, which Paste handles. While a paste walks it the UI task's purge floor is raised to MED (§50.6.6), so the copy buffer each entry claims cannot shed it; a list shed BEFORE its paste is simply an empty clipboard. Not declared movable: a cache that is in the way is dropped rather than moved (§66.10.1), and 1KB is not worth a proc |
| `MEM_P_DIRW` directory read-ahead | up to 32KB, no page head | **PURGEABLE, and MOVABLE** — `dsk_rah_reloc` is its proc | **This row described the claim as it was TWO cycles ago and every clause of it had been retired.** It carried `MC_DMA` over the whole block (`mem_claim_dma_x`, §50.3) so that `dsk_runcap` could never split a fill; §18.95.7 withdrew that, on the finding that whole-block placement was an OPTIMISATION and not a rule — `dsk_runcap` caps a run at the page boundary and `dsk_xfer` carries on from there, so a straddling fill is two `int 13h` and the right data either way, and what the constraint really cost was the PLACEMENT. It is claimed with a plain `mem_claim_x` now. It then declares itself movable: the claim is taken at a MOUNT, so after a shed it used to come back wherever the arena had room and stand there pinned for the session. **One word names it** (`mem_pg_own` says so), the slot table holds LBAs rather than addresses, and a fill in flight is already pinned by `mem_in_xfer` — which is the whole of why the proc is cheap. The 63KB figure was the old one-page block; the cap is 32KB and the solve is a ladder (§18.95.8). Read §66.9 reason 2 knowing that this row is no longer its example |
| `MEM_P_RAHB` read-ahead landing (kern_mega only) | 5KB, page-safe head over all of it | **PURGEABLE, PINNED** (`mem_claim_dma_x`: `MC_DMA` pins it, and a shed is the way out) | Taken only in protected mode, where the read-ahead STORE itself is above 1MB behind an LDT selector and is not a heap claim at all (docs/plans/KERN-MEGA-PLAN.md 10.3 item 32). Every fill reads one chunk into this and copies it up, because a reflected `int 13h` reaches only a real-mode ES:BX. Page-safe so a fill is one transfer; pinned is therefore the price, and it is 5KB against the 32KB `MEM_P_DIRW` claim it replaces there. `[dsk_rah_bnc]` is its one naming word (`mem_pg_own`), and a shed costs the FILLS until the next mount re-takes it - the store keeps serving what it holds |

## Package-owned claims

### Declared

| claim | verdict | note |
|---|---|---|
| **Paint** canvas, undo, clipboard, scratch | **MOVABLE** | §66.5.1. `pt_reloc` shifts the per-row segment table (`pt_rowseg`, `PT_CH_MAX` entries) and recomputes `[pt_undelta]`; the BMP save pins the canvas for the write (`pt_pin`/`pt_unpin`) |
| **Paint** GIF/LZW staging buffer | **PINNED (rule)** | `[pt_gbase]` is a paragraph derived off `[pt_gseg]` at four sites, it is the `OSAPI_FILE_READ` target, and it lives for one file |
| **Tracker** module (up to 116KB) | **MOVABLE** | §66.5.2. `trk_reloc` fixes 36 words: `[mp_blobseg]`, 31 `MS_SEG` sample bases, 4 `MP_SEG` channel segments. Declared only after `mp_load` succeeds |
| **Frotz** story (up to 508KB, the largest claim this OS hands out), Z-stack, undo snapshot | **MOVABLE** | §66.5.9. `zf_reloc`. The story costs **three** words — `[zf_sseg]`, the live program counter `[zf_pcseg]` (shifted, never set) and `[zf_sdelta]`, the running total `zi_yield` differences to repair an `ES` pushed before the move. It **cannot** declare `OSAPI_MEM_PARKSAFE` and does not need to (§66.5.9.1) |
| **Frotz** scrollback (`ZW_SBKB` 24KB, 8KB fallback), save staging, transcript (`ZI_SCRKB` 16KB) | **MOVABLE** since NO-PINNING A3, and PINNED only across the UI task's file call into or out of the staging (`zi_savpin`) or the transcript (`zi_scrpin`), bucket C | §66.5.9.2. One word each, in `zf_reloc`'s table (`zf_rltab`). The scrollback alone bought nothing: it sat directly on the other two, both session-lived and pinned as "file-operation targets". The worker loads each from its word and gives ES back before either park point (`zx_worker`'s ALIVE, `zi_yield`). `editmove-frotz` is the gate |
| **Frotz** picture buffer, `.mg1` probe | **PINNED (rule)** | transients freed inside the call that made them |
| **Browser** source, document, line table, link arena | sizes since Browser 1.6 (SPEC.md 71.18): the network source starts at 1KB and grows by 4KB under the 20% conventional reserve to `BR_MAXKB` 96KB (two 64KB aliases of one claim), the document from 1KB to `BR_DOCMAX` 63KB, the line table to 16KB and the href arena to 12KB, claimed only when a real link is parsed (`br_linkensure`). **MOVABLE** since NO-PINNING A3; the source PINNED across its own `FILE_READ`/`FILE_WRITE` (`br_srcpin`, bucket C) | SPEC.md 71.14.1. `br_reloc`, one word each. No socket buffer is one of them - every `NETV_*` verb is handed `br_rxb`/`br_reqb` in the package's segment - and the worker loads the words each pass and parks only at `OSAPI_TASK_ALIVE` with `br_nstep` done. `pkgmove-browser` is the gate |
| **FTPD** STOR/RETR stage (`FD_BIGSZ` 32KB) | **MOVABLE** since NO-PINNING A3; PINNED across the UI task's file call into or out of it (`fd_stgpin`, bucket C) | SPEC.md 77.51.1. `fd_reloc`, one word. `ETHER.DRV`'s send and receive copy and keep no pointer; a running worker pins it and the worker parks only at `OSAPI_TASK_ALIVE` with `fd_step` done. `pkgmove-ftpd` is the gate (MartyPC has no NIC, so it moves the idle server's stage) |
| **Chart** canvas (19KB) | **MOVABLE** since NO-PINNING A3; PINNED across the export's `FILE_WRITE` (`ct_pin`, bucket C) | SPEC.md 82.14.1. `ct_reloc`: `[ct_chartseg]` and `ch_prep`'s borrowed `[ch_stgseg]`. The read's staging stays undeclared - one callback, a file read's target throughout. `pkgmove-chart` is the gate |
| **Note Pad** document + undo arena | **MOVABLE** | §66.5.7. `np_reloc`, two words, one proc — `BX` picks between them. A load pins the note for the read (`np_dmov`, §66.5.7.1) |
| **Note Pad** CR/LF staging buffer | **PINNED (rule)** | transient and the `OSAPI_FILE_WRITE` target throughout — pinned by having no declaration, the cheapest correct answer |
| **Telnet** Zmodem staging (`TZ_STGSZ` 8KB) | **MOVABLE** since NO-PINNING A3; PINNED across `tz_commit`'s file write (`tz_spin`, bucket C) | §70.11.3. Held for a transfer only: claimed in `tz_dlgopen` before the Save dialog, reused across a batch, freed by `tz_sfree`. `tz_sreloc`, one word. It was pinned on the reasoning that the worker's `tz_store` loads ES from the word and is not `OSAPI_MEM_PARKSAFE` - which is the case the kernel excludes: a RUNNING worker pins its package's claims (`inst_seg_parked`), and this one parks only at `OSAPI_TASK_ALIVE` with nothing held. No move gate: the claim exists only during a Zmodem transfer, which needs a NIC (QEMU) - argued by code reading |
| **Dot Delirium** board picture (4-27KB, the instance's life) | **MOVABLE** since NO-PINNING A3 | SPEC.md 93.3.4.6. `dd_bdreloc`, one word; a regrow keeps the declaration. The worker draws through it and parks only at `OSAPI_TASK_ALIVE`; no file touches it. `pkgmove-dotdel` is the gate (the window hidden for the pass: the worker drawing flat out was measured never reaching ALIVE inside `INST_PARKW` while it shows) |
| **Desk Clock** face cache (sized to the face, while open) | **MOVABLE** since NO-PINNING A3 | SPEC.md 110.14.10. `dk_creloc`: `[dk_cseg]` and the composer's borrowed `[ck_bseg]`. The settings window's tall band (`[dk_sbseg]`) stays undeclared, claimed and freed inside one paint (bucket C). `pkgmove-deskclock` is the gate |
| **Recorder** speaker play buffer (4KB), **Stik Note** checkbox flip (the note, up to 16KB), **DOS** `dos_hk_claim` (the shell's COPY buffer, and DIR's sorted listing) | **PINNED (rule)**, bucket C | NO-PINNING A3 audited them and left them: each is claimed and freed inside one callback or one shell command, and is a file transfer's (or the PCM_EXCL play's) buffer throughout. DIR's listing is the longest-lived - held across a `--More--` page and freed by the next listing - and is filled by a directory walk through file calls, so it is a pin-and-proc audit of `dsh_*` and not a one-word proc |
| **ArtfulType** document + undo/redo arena | **MOVABLE** | §66.5.7. `at_reloc`, two words; `at_dmov` pins across both file operations. The clip slice left the arena at §46.6.1 - the clipboard is the kernel's claim now |
| **Fractal** run cache | **MOVABLE** | §66.5.7. `fr_reloc`, one word — every cursor into it is an offset |
| **ModPlug** module (up to 116KB) | **MOVABLE** | §66.5.8. `mpp_reloc`, 36 words. §56.1's bill: the replayer is an independent copy of Tracker's at *different strides* (`MPS_SZ` 12, `MPM_CHSZ` 40), so a renamed `trk_reloc` walks the tables wrong and yields plausible garbage |
| **Video Player** poster, palettes a scene, VGA8's palette; the session's ring, keeper, page copy and the text's second save | **MOVABLE** | SPEC.md 98.3.19.2. One proc, `vp_smove`, over `vp_smtab`: every word naming one of these claims holds its BASE, so the word equal to the old base is the one moved (the ring may start inside its claim, THE GAP, and moves by what the claim does). The poster (`vp_pmov`) and **VGA8's 768-byte palette** (`[vp_p8seg]`, 1KB from the top, held while the file is open: SPEC.md 98.1.9.13's data pass took it out of the bss) are declared once made - nothing reads them at interrupt time. **The full screen's text's second save** (`[vo_s1seg]`, 2KB from the top, SPEC.md 98.3.13.2) is a session claim like the page copy: only a play on two pages or more takes it, and the hook writes it, so it is pinned with the rest for every bracket. The SESSION's and the palettes a scene (`vp_smov`) are pinned for every bracket, whose hook reads them, and movable between brackets; a LIVE session keeps them pinned, its worker decoding out of them. **The block decoder** (SPEC.md 98.3.23), part 0 fetched by `op_fetch` on every machine (part 1, its protected-mode assembly, 4.9 KB, under `CPU_F_PM`: 98.3.23.2) - 4.7 KB, one bit's 2 KB codebook inside it (98.3.23.1) - is the same shape: `[vp_bseg]` is in `vp_smtab` and the claim in `vp_smov`, so it moves on the desktop and is pinned for a bracket, the hook calling into it; `tests/vidv2.py` reads its `MC_RLOC` both ways. **A6's remainder joined it (2026-10-24)**: a RESIDENT play's KEY RECORD (now in `[vp_rclm]` beside `[vp_ring]`, which also ends its leak - `vp_sfree` gives back `[vp_rclm]`), THE LAYER's own claim in place (`[vp_lyseg]`, asked first in `vp_smove` because a flip's is derived inside the ring's claim) and VPDIAG's profile (`[vp_prseg]`, pinned by `vp_prend` for its file write); and a pause that BANKS pins the session for each chunk's read (`vp_pstep`). `tests/vidclmove.py`'s three arms move each and play on. A RESIDENT block is declared under `[vp_rblk]`/`[vp_rablk]` (`vp_rmov`, 98.1.7.4) once `vp_rload` has read and walked it |
| **Word** document, CHP twin, PAP dictionary 1KB, italic table `WD_ITKB` 9KB, undo arena + its CHP twin | **MOVABLE** | SPEC.md 68.3.2. `wd_reloc`, one proc over `wd_rltab`: each claim is named by ONE word and every cursor into one is an offset, so a move is one store. `wd_itinit` is the one claim made with a segment in a register (`wd_walk` holds ES = the text), so it pins the rest across that claim (`wd_dmov`) and declares them all again after. `tests/wdheapmove.py --app word` |
| **Scribe** — the same six, plus every **picture** (up to `SC_PICMAX` 8) | **MOVABLE** | SPEC.md 95.14. `sc_reloc` is `wd_reloc` plus the eight `sc_pictab` segment words (a picture is named by its slot alone; the CHP byte is the index). `sc_picmov` declares a picture once it is registered, at all three readers. `tests/wdheapmove.py --app scribe` |
| **LEMMINGS** a level's claims: mask 32, bank 96, LEMMAIN 60, record/object map 16, and T 125 (VGA) or world 63/32 + shadow 16 (CGA/Hercules); 329 / 283 / 252 KB, held while a level is KEPT on the desktop | **MOVABLE** on the desktop, **PINNED (rule)** across the launch | SPEC.md 116.6.2 (NO-PINNING C4). `os88_onwake` pins them around `ovl_launch` (read, bracket, play: locals hold claim segments across file reads there, bucket C) and declares them again after. `os88_onmove` in `lemblit.inc` RANGE-translates eighteen words: the seven claims, `lem_anseg`/`lem_mkseg`/`lem_ptseg` (segments INSIDE LEMMAIN's claim), `lem_far_seg`, and the raster's seven (`lb_world`, `lb_shadow`, `lb_t`, `lb_wp0`, `lb_wp3`, `lm_seg`, `lo_seg`), sized off `lem_cl_kb[]`. Moving the set is a ~0.85-1.07 s pass on a 4.77 MHz 8088 (848 ms for 252 KB, 1,066 ms for 329 KB). `tests/lemmove.py` is the gate on both claim sets |
| **HTTPFS.DRV**'s index (16 KB, the life of a mount) | **MOVABLE** | NO-PINNING R2. `hf_reloc` writes ONE word, `[hf_iseg]`; declared once the fetch is validated, the claim having been the network's write target until then. Inside a verb it is loaded into ES and re-read after `hf_list`'s `OSAPI_FS_ENT`, the one call there that can claim. A package worker inside the verb pins every driver claim (SPEC.md 66.6.3.2) |
| **1942** canvas 64KB (the splash's band cache on the desktop) | **MOVABLE** on the desktop, **PINNED (rule)** across the bracket | SPEC.md 101.2.1 (NO-PINNING A4). `n_canvasmv`, one word; `n_launch` pins it around `OSAPI_FSX_RUN`, where ES holds it while the scenes, banks and ring are claimed. The banks, scenes, effects and the 13KB PCM ring are claimed and freed inside the bracket: bucket C, and D while the card is armed. `pkgmove-1942` |
| **isoTron** canvas 63KB, work claim 30KB | **MOVABLE** on the desktop, **PINNED (rule)** across the bracket | SPEC.md 108.8.1 (NO-PINNING A4). `it_claimmv` stores whichever of `[it_cseg]`/`[it_seg]` moved; the row bases are offsets, the restartable worker loads both at each wake and is parked for a move. `it_fs_go` pins both around `OSAPI_FSX_RUN`. `pkgmove-isotron` |
| **WireFleet** stamp bank (lazy part 0, 14KB) and its planar copy 9KB (colour windows) | **MOVABLE** | SPEC.md 114.11, 20.12.12 (NO-PINNING A8). The parts standard's first opted-in lazy part: `op_movable`, `op_lazymv` stores the row and `wf_partsmv` (the `OP_ONMOVE` hook) re-reads `[wf_fxseg]`; `wf_fxpmv` stores `[wf_fxpseg]`. The samples part is fetched and dropped inside `wf_pcm_open` (bucket C). `pkgmove-wireflet` |
| **PIXELSTEIN** shadow 9/16KB | **MOVABLE** on the desktop, **PINNED (rule)** across the bracket | SPEC.md 97.9.2 (NO-PINNING A4). `px_shmv`: `[px_shseg]` and `[px_dseg]`; it moves with the worker parked. On every layout measured it sits directly above the loader's pinned claims below, so it does not move yet. **Its levels 10KB, art masters 37KB and sprite set 60KB stay PINNED, blocked by the fence**: the loader claims them, the re-home re-owns them to the slot, and `mem_find_own` refuses the program's declaration (MC_OWN = slot, not its segment) - a kernel fence accepting the caller's own slot is what unblocks them. The loader's reserve is bucket C (around `op_load`). `pxsmove` leg 8 |
| **The Wire** the catalog (`[wr_catseg]`, held while the window is open), the transfer claim (`[wr_fseg]`, one file or one archive's largest entry, for a chain) | **MOVABLE** (NO-PINNING, SPEC.md 92.5.1) | `wr_reloc`, one proc for the three words that name them - `[wr_dseg]` is the worker's copy of whichever is being filled. Every reader loads its segment at each use and the worker parks only at the top of its loop, so a move lands between two passes. The transfer claim is PINNED (bucket C) across `OSAPI_FILE_WRITE` (`wr_write`), `OSAPI_PKG_START` (`wr_pkgrun`) and the archive's `OSAPI_FILE_APPEND`/`_WRITE` (`wr_arcwrite`) by `wr_fpin`/`wr_fmov`. Gated on QEMU, the claims existing only with a NIC: `tests/thewire.py` assertions 17-18 post the compaction through QEMU's GDB stub (`os88qemu.poke`) |
| **PiXEL** the code parts 0-8 (`op_fetch`, `px_pfetch`), an animation's claim (`[px_anseg]`); the flat-file expansion (`[px_flatraw]`), the decode's work claim (`pxmaster` `[px_wbase]`), the GIF part's work claim (`pxgif` `[px_wbase]`), an operation's work claim and Save As's scratch (`[px_ewseg]`) | parts and `px_anseg` **MOVABLE**; the five work claims **PINNED (C)** | SPEC.md 106.5.1. The parts through the standard's `OP_ONMOVE px_mreloc`, pinned exactly while the UI task or the worker stands in one (`px_pin`/`px_unpin`, a mask a task): the worker parks at `OSAPI_TASK_ALIVE` INSIDE a decoder. `px_anseg` is pinned for one animation pass (`av_start` to `av_wake`). The work claims each live exactly as long as the job that uses them, whose worker holds segments derived from them across its parks. The master, the view, the undo master and the face already declared - the face's pictures claimed through the CEILING door since SPEC.md 106.16.1 (`OP_WANT_ASSETHI`), so the descending pass is the one that packs them. `pxpump`'s ring is a DMA target (D). Rows `pkgmove-pixel`, `-pixelgif`, `-pixelanim` |
| **Excitebike** sprite poses (compiled, far-called IN the claim), CGA dictionary, art pack, harness and custom-course claims | **PINNED (rule)**, bucket C | NO-PINNING A5, checked 2026-10-23: every one is claimed after `xb_fullscreen` starts and freed at `xb_fsdone` (or `xb_race_close`, or inside `xb_custom_load`), on every exit of the bracket, so the poses' CS is never held on the desktop. The splash art (`xb_artseg`) is the one desktop claim and was already MOVABLE (`xb_artreloc`) |
| **the parts standard** (`apps/os88parts.inc`): the carve, a fetched lazy part | **MOVABLE when the package opts in** | SPEC.md 20.12.12 (NO-PINNING A8). `%define OP_ONMOVE <hook>` assembles `op_movable`/`op_pin` and the procs `op_carvemv` (stores `[op_base]`) and `op_lazymv` (stores the row's banked segment), each jumping to the hook; byte-identical without it. The section's table is the audit of every user |
| **RealDMG** frames 17KB, damage memory, cartridge (one claim up to 256KB, or the bank cache's slots), cartridge RAM up to 33KB, Static's profile 9KB, the recompiler's 80-145KB and its second 64KB, the log's records, a replay's buttons | **MOVABLE** | REALDMG-SPEC 14.4 (docs/plans/NO-PINNING-PLAN.md A1). `rw_mreloc`, one proc, ALL the work at move time - the state block's copies, the bank table and the recompiler's far doors - so no routine a frame runs gains an instruction (measured: 1,414.3 switches a guest second against 1,414.4). A move of a cache segment FLUSHES the recompiler, its chains being `jmp far` with the segment in their bytes and no list of them. Pinned around every file call (`rw_fgo`/`rw_fback`) and for a load. **The parts carve (core + view, 128KB) moves too** (A1b): `OP_ONMOVE rw_partsmv` puts right the view's and the core's every copy and flushes the recompiler. Still pinned: the log's text, one call's. A bank-cache game that is FETCHING keeps its worker mid-frame, so its claims wait (bucket C). `tests/rdgmove.py`, `tests/rdgclose.py` (86Box) |
| **DOS's** packet-driver buffers (`PKB_CARDKB` 3KB, `PKB_XLKB` on the cable) | **MOVABLE** | §96.23.7.2. `dos_pkt_reloc` writes ONE word — every reader loads `[dos_pkt_bseg]` fresh and the offsets inside it (`PKB_RX`, `PKB_STK`, the `dn_*` rows) do not move with the base. **It was in neither table, and that is how it came to be a wall**: it is claimed BEFORE the floor, before the unmount and before `dos_run`'s posted compaction, so it lands on whatever purgeable caches are at the heap's floor at that instant and the pass purges them out from under it — measured on a 530KB heap with an NE2000, the Memory page promised **442KB and the program got 400**, of which 3 is this claim and **39.5 is stranded floor** the arena cannot reach because an arena is ONE run. Invisible without a card: `dos_pkt_bufs` claims nothing at all when `net_find` fails, so the same build promised 441 and delivered 441 on a machine with a hard disk and no NIC, which is why `tests/dosnetarena.py` is QEMU's and its own row. **It is PINNED AGAIN for the handle's lifetime** (`dos_pkt_rloc`, bracketed on `[dos_pkt_raw]`): the packet driver's private STACK is in this claim and `dos_pkt_tick` switches `SS:SP` into it from the `int 08h` chain, so a compaction reaching it mid-frame is the one case a relocation proc cannot repair |
| **Audio** look-ahead ring (`AP_LA_SZ` 32KB, `[ap_la_seg]`) and speaker ring (`[aps_rseg]`), each claimed once for the instance | **MOVABLE** between plays, **PINNED (rule)** for one - bucket C (the look-ahead is every refill's `ES:BX`) and D (os88spk's ISR reads the speaker ring) | SPEC.md 86.5.3 (NO-PINNING A6). `ap_ensure_bufs` pins both before the parse's first read (`ap_rpin`); `ap_rdecl` declares both again with one proc, `ap_rmove`, from `ap_stream_close`'s tail, `aps_close` and a refused open. A pause is a play: both stay pinned. `os88spk_rseg`, the library's copy, is live only inside a play (`os88spk_init` writes it at each). The idle worker's sleep came down 6 -> 3 ticks so it answers `INST_PARKW`'s park. `ringmove-audio`, `ringmove-audiospk` |
| **Tracker** speaker ring (`[tsp_rseg]`, `trkspk.inc`, claimed at the first speaker Play and held for the session) | **MOVABLE** between plays, **PINNED (rule)** for one, bucket D (the ISR reads it) | SPEC.md 45.25.6 (NO-PINNING A6). Declared at the claim (`tsp_rmove`, one store), pinned by `tsp_start` before `[tsp_run]` goes up (`tsp_rpin`), declared by `tsp_halt` when it comes down - a stop, a pause, the song's end. The door, the brackets the play crosses and a rung down all happen pinned. The text bracket's shadow is bucket C (claimed before the bracket, freed after). `ringmove-tracker` |
| **WireFleet** clip ring (13KB, `[wf_pcmseg]`, claimed at the window's open on a Sound Blaster route) | **MOVABLE** between clips, **PINNED (rule)** for each, bucket D (the card is armed on it from verb 0 to verb 2) | SPEC.md 114.11.1 (NO-PINNING A6). Declared once the clips are copied in (`wf_pcmmv`, one store), pinned by `wf_pcm_play` before the stream opens, declared by `wf_pcm_halt` after the close. A TOP-DOWN claim, so it moves only in the descending pass. +24 bytes out of the alignment pad (now 0), image + bss still 65,024; verb 0's CX, never loaded before, is loaded now. `ringmove-wirefleet` |
| **MIDIRack** song (`[mr_fseg]`), wavetable bank (`[mwt_bseg]`) and mix claim (`[mwt_xseg]`), each held until the window closes | **MOVABLE**; the song PINNED only across its own read (bucket C), the bank and the mix claim PINNED for a wavetable play (bucket C: the voices hold segments derived from the bank across the worker's park) | SPEC.md 105.10.1 (NO-PINNING A6). `mr_fmove` (one store) is declared at every exit of `mrl_load`; `mwt_pin`/`mwt_decl` (proc `mwt_move`) bracket a wavetable play in `mro_play` and `mro_close`. The two rings are bucket D, claimed per Play and freed at its end (SPEC.md 105.8.3, 105.8.4). `ringmove-midirack` |
| **DOS's** trace part (35KB, `DOSTRACE` builds only - `tools/os88dosdbg.py build`) | **MOVABLE** at the prompt; PINNED from `dos_run`'s `.ready` to the end of `dos_trace_dump` (`op_pin`, bucket C) | §96.29.1.2 (NO-PINNING 2). The parts standard's `OP_ONMOVE` opt-in, its hook `dos_trmove` re-asking `op_seg` for the two banked words (`[dos_trseg]`, `[dos_m33seg]`). The pin covers the stretch a DOS PROGRAM runs - its INT 21h calls trace into the part and claim on the UI task - and the dump's `OSAPI_FILE_WRITE` out of it. **The shipped `DOS.O88` makes no such claim at all**: its only `op_load` is this `%ifdef DOSTRACE` one, the loader's carve becomes the REGION at the re-home (declared by `OS88_REGION_MOVABLE`) and the core part is fetched, copied and dropped inside one call. All four shipped arms assemble byte-identical. `dostrmove` is the gate |
| every package **region** | **see *Regions* below** | a region's base is its CS, and this row said `PINNED (forever)` until §66.6.1 opened that door. It is not a data claim and the two columns here do not decide it — the *worker* does. **42 of the tree's 44 packages declare today** and `tests/unit/t_movable.py` is what keeps it that way (§66.6.1.1): the other three are registered in `tests/movable.txt` with a reason each |

### Undeclared

Every package below claims heap and declares none of it movable, so each is
pinned by default and is a barrier for as long as it is held. None has been
audited for §66.3's author rules; a row is "nobody has looked", not "it cannot
be done". Sizes are the `equ`s at the claim sites.

| package | claims |
|---|---|
| **Word** | `WD_LSTGKB` 62KB load staging and the save's, and the `2*WD_SCHALF` Utilities scratch — each claimed and freed inside one command and a transfer's or a splice's buffer throughout, so **pinned by not declaring** (bucket C). Everything session-lived is **MOVABLE** now (SPEC.md 68.3.2, see below) |
| **Scribe** | the same two transients as Word, plus the 40KB picture decode scratch (`[sc_picseg]`, shrunk in place into the picture or freed before Insert ▸ Picture returns) and the RTF picture still being collected (`[sc_rpseg]`, until `sc_rpfin` registers it) — all bucket C. The rest is **MOVABLE** (SPEC.md 95.14, see below) |
| **the typeface cache** (`apps/os88type.inc`, so Word, TeXpad and every other includer) | `TY_FACE_KB` per open face, and it is the one claim in the tree that carries `MC_DMA` for **alignment alone**: it asks `OSAPI_MEM_CLAIM_DMA` for a whole-block head so the base is 512-byte aligned for its file read. It was `TY_FACE_KB + 1` with a hand round-up underneath, and the round-up could never fire — guard 6b makes every claim base `HEAP_SEG + n*MEM_PARA_KB` paragraphs and asserts `MEM_PARA_KB` is a multiple of 32 — so that was 1KB of heap per open face, up to three per Word or CWORD instance, for nothing. The package asserts the alignment now and refuses the face if it ever fails. No chip is armed on it and none ever will be, and since §66.4.2 that no longer pins it: the blanket `MC_DMA` refusal is gone and `mem_cp_dest` places a page-constrained block page-safely. **MOVABLE since NO-PINNING A7**, the face once it is read and checked and the pre-shifted table once it is built: `ty_reloc` updates `TF_CLAIM`/`TF_SEG`, `[ty_curseg]` and `[ty_psseg]`, which are the only words that name them (every render loads one into ES and claims nothing while holding it). `tests/tymove.py` is the gate. |
| **TeXpad** | source: sized to the document, `TP_SRC_KB0` 1KB up to 8KB - the zero-capacity `FILE_READ` probe sizes a load, `MEM_REGROW` trims it back after a load or New, and typing grows it a KB at a time. **MOVABLE since NO-PINNING A3** (`tp_srcreloc`, one word), and PINNED for the length of its own `FILE_READ`/`FILE_WRITE`, whose ES:BX it is (bucket C, `tp_srcpin`); `pkgmove-texpad` is the gate. Export: grown in 4KB steps as the file is composed, up to 24KB, and FREED once the file is written - it used to be held for the rest of the session after the first export (apps size pass 1, set 6; SPEC.md 69) |
| **Video Player** | nothing long-lived any more (NO-PINNING A6, SPEC.md 98.3.19.2): its transients (the header cluster, a key's shadow, the poster scratch, the record buffer, a RESIDENT block while `vp_rload` reads it into place) are bucket C, and the card's ring bucket D |
| **Tank Attack** | `TK_SHKB` 32KB — the CGA/Hercules shadow and its HUD template, one claim (SPEC.md 85.3.5). **Bucket C since NO-PINNING A4** (SPEC.md 85.3.5.2): claimed inside the fsx bracket and FREED on its way out (`tk_shfree`, the template's own claim too on kern_mega), so the desktop holds none of it; `tests/tank.py`'s last leg is the gate. **The `APP_SMALL` build claims off a LADDER instead** — 18KB, else 17, else 16 (85.3.5.1) — and keeps it for the instance, the 128KB machine having no compactor. Same package, same ABI, two claims; it is what put this package on the 128KB machine's disk |
| **Clear Skies** | **Bucket C, nothing to declare (NO-PINNING A4, checked 2026-10-23)**: the shadow is claimed in `cs_r_setup` inside the bracket and freed at `cs_fsx_main`'s `.out` on every exit, and `cs_wldget`'s buffer inside one call. **Since the apps size pass (set 6, SPEC.md 88) there is ONE claim besides the region**: the `CS_SHKB` 16KB 1bpp shadow (8KB in C160, none on Mode X), claimed on the way INTO each flight and freed on the way out, and pinned by not declaring. The 11KB artwork claim described below is GONE - the art is one stream of the `csw.z` part, expanded into a bss union it shares with the flight's projection tables. **And since SPEC.md 88.10.7.1 it is not re-read on the way out of a flight**: the six bands are packed one by one and the whole stream (4,901 bytes) is KEPT, packed, in the region for the instance's life (in the IMAGE since SPEC.md 88.10.7.2, so nothing reads it), each band expanded from there the first time it is drawn after a flight - so the region carries the art twice on the title page (packed and expanded) and once in flight, which is why it is 55KB and not 50, and the way out reads no disk. `cs_wldget`'s transient buffer is then a world's read alone, never the art's. What follows is the record of the earlier shape. What follows is the record of the earlier shape. Then: `CS_SHKB` 16KB 1bpp shadow, `CS_ART_KB` 11KB launcher artwork — both for the life of the instance, and both **pinned by not declaring**, which is the right answer for the shadow rather than an audit nobody has done. `[cs_shseg]` is copied into `[cs_tseg]` at every `fsx` entry and **ES is loaded from `[cs_tseg]` at thirteen sites and kept across the span**, so a move inside a frame is a live segment in a register that no relocation proc can reach — the shape §66.5.9.1's `OSAPI_MEM_PARKSAFE` exists for, and the fsx bracket has no park point. The artwork is a different case and would only cost one word (`[cs_artseg]`, read by three blits and 0 when the claim failed), but it is claimed at launch and freed with the instance. **SPEC.md 88.10.3 moved both out of the image** — the artwork is an `OP_COMP | OP_LAZY` part the loader `op_fetch`es, which expands it into a claim of its own (11KB, `op_lazykb`'s R plus the read; SPEC.md 88.10.4.1, which retired the loader's own packed stream, decode and second claim of `CS_ART_KB`) — and **88.10.5 added a third, which is TRANSIENT**: `cs_wldget` claims a cluster-rounded buffer for one packed world stream, expands it into the bss overlay and frees it before it returns, so it is never live across a call the user could reach |
| **every C package** (C64, RunCPM, Weave, Loom, CWORD) | **`os88_mem_movable()` EXISTS NOW** (§66.4.1, `%define CC_HAS_ONMOVE` + `void os88_onmove(unsigned was, unsigned now)`), so this row is "nobody has done the audit" rather than "the SDK cannot" — which is a different row and a much cheaper one. Declared: **LEMMINGS' kept level** (the *Declared* row, SPEC.md 116.6.2); **CWORD's Undo snapshot** (9KB, SPEC.md 73.12.4), whose `os88_onmove` re-points the one word that names it; **RunCPM's 64KB Z80 space and every open file's buffer** (SPEC.md 74.3.2: `rc_zseg` + `rc_z.seg`, `o->seg`; pinned only around a file call into them); **C64's 64KB RAM and Paste's queue** (C64-SPEC 3.1.1: `c64_m.ramseg`, `c64_chr_seg`, `c64_paste_seg`); **the Apple II's 49KB RAM and Paste's queue** (APPLE2-SPEC 3.1.1: `a2_m.ramseg`, `a2_paste_seg`). Each emulator's core reloads its segment at the top of every run and runs on the UI task with no claim inside it, so the proc does all the work at move time and the hot path is instruction-identical. Pinned, and bucket C (claimed and freed inside one call): C64's `.PRG` and Copy staging, the Apple II's Load/Save Program claims and its 53KB fullscreen shadow (held for the length of the `os88_fsx_run` call). Declared too: **WEAVE's bundle, VM, grid and canvas claims and its canvas core (part 0); LOOM's source claim, Preview's two pack claims and its preview module (part 0)** (WEAVE-SPEC 1.2.6: one `os88_onmove` each, which rewrites every COPY of a base as well as the word - the WJS VM's `wvm_vmseg`/`wvm_bseg`, the FX VM's `wfx_gseg`, the canvas core's `WSS_CVSEG`/`WSS_BSEG` in ITS state block, a part's row `zkb` and far vector - and WEAVE's region proc tells it too, `CC_ONMOVE_REGION`, for `wvm_ourds`; `soak -k weavemove -k loommove`). Pinned, bucket C: WEAVE's canvas core once its worker is hired (the worker parks INSIDE it), the 8KB `saveState`/`loadState` claims (one native each), LOOM's Pack claims (one command) and its source claim across each save's write. Still pinned and undeclared: every `apps/os88parts.inc` scratch part. **The C OVERLAY is the exception and it is MOVABLE** (§66.4.1, `apps/cc/crt0.asm`): it is claimed top-down because its base is a CS, and `cc_ovreloc` is four bytes falling through into `cc_ovbind` — safe because `cc_ovthunk` DISCARDS the module's CS and rebuilds the `retf` from `[cc_ovseg]` after the call, so no stack ever holds it. **As a PART (SPEC.md 73.14.2, CWORD and COVL) the EAGER arm is MOVABLE when the carve holds the module alone** — `cc_ovcarve`. LEMMINGS' wave 4 pinned it for a cycle on finding that a module function calling another was a `call far [cc_ovm_*]` that left the MODULE's CS on the stack for a plain `retf`; that call goes through `cc_ovmthunk` now (the stash `cc_ovthunk` already kept), so no stack holds the module's CS and the pin came off (SPEC.md 73.14.2.1, docs/plans/NO-PINNING-PLAN.md). A carve with more rows - where the program may have banked a part's segment - moves when the package says **`CC_ONMOVE_PARTS`** (SPEC.md 73.14.2.1): crt0 declares it after `os88_main` and its proc calls `os88_onmove` with the carve's bases, the C re-reading `os88_part_seg()`. **C64's ROM + module carve and the Apple II's ROM carve both do**, and `tests/emumove.py` moves each on MartyPC (`soak -k c64move`, `-k a2move`). The LAZY one is `op_fetch`'s claim, bottom-up, declared with `cc_ovreloc` storing into the part row's `zkb`, which IS `[cc_ovseg]` there — closed by the same thunk. `tests/ovlhigh.py` is the gate (CWORD, eager) and asserts `MC_RLOC` ≠ 0 |

Word was the one worth an afternoon: session-lived, tens of KB, claimed
after the user has been working — exactly the profile that stranded Tracker's
module in the field (docs/FIELD-NOTES.md 2). It had it (NO-PINNING-PLAN A2),
and so did SCRIBE: their session-lived claims are in the movable table below.

### Nothing to declare

Task Manager, Solitaire, Arkanoid, Missile, Tamegram, Minesweeper, Piano,
Hello, Calculator, Cyclone: no `OSAPI_MEM_CLAIM` in any of them. (Recorder
was on this list and does claim: its speaker play buffer, held for one play -
the bucket-C row above.) (Telnet left this list at the apps size pass: its Zmodem staging is a
transient claim now, above.) The Task Manager's "~7.3KB of heap while open" (§28) is its *region*,
image plus bss, whose base is its CS. (`tests/heapfrag` claims and declares,
and does not ship.)

## Driver-owned claims

| claim | verdict | note |
|---|---|---|
| **SB staging pool** (sized to the first grant, up to `SBL_POOLKB` 20KB — §34.6.3) | **MOVABLE, except while the card plays out of it** | §66.5.5. `sbl_reloc`, one word, because a grant is an *offset* and the staging copy is the v3 boundary; every copy into or out of it goes in `SBL_DCHUNK` chunks under `cli`, re-reading `[sbl_poolseg]` per chunk. Claimed through the DMA door now (`OSAPI_MEM_CLAIM_DMA_HI`, head = the whole pool; from the top, and from the BOTTOM on kern_mega, KERN-MEGA-PLAN 14.55) so a ring in it can be PLAYED IN PLACE (§34.5.2) — and for exactly that stream verb 0 PINS it (`OSAPI_MEM_MOVABLE` AX = 0), because a direct stream has no task and `[drv_wcnt]` is then 0 while the 8237 reads it; `sbl_unpin` declares it again once the channel is masked. `tests/trackmove.py` check 8 carries the assertion and nothing reaches it yet (its registered machine has no card) |
| **SB DMA double-buffer** | **PINNED, and only held for one stream** | §34.5.2. `MC_DMA`, claimed top-down (bottom-up on kern_mega, KERN-MEGA-PLAN 14.55) at the open of a stream that is NOT direct (a linear stream, a single-cycle DSP, a pool that is not page-safe, every record stream) and freed at its close by `sbl_unpin`, so it is never declared and never needs to be: `[drv_wcnt]` is non-zero for all of its life anyway. It was claimed at attach and MOVABLE while idle (§66.6.4, `sbl_ring_reloc`) — both are gone, because an idle card now holds nothing but its image, which `tests/sndmove.py` asserts |
| **HDD** install buffer (`hd_ibufsz` ladder) | **PINNED (rule)** | §66.5.10. An `OSAPI_FILE_READ`/`WRITE` target at all four uses, claimed for one install and freed at its end |
| **HDD** per-partition listing (`HDD_LISTKB` 6KB) | **RETIRED — there is no claim** | §22.6. It was the only block in the tree with three holders and the worked example of §66.5.10.2's kernel-side walk; §25.9 took the icon bodies out of it and §22.6.2 raised the floor to the same cap, after which it funded a listing `.lowbss` already gave for free. A mounted partition now costs the driver a `hd_vols` row and **no heap at all** — 6KB back per partition, 24KB on a four-partition machine. `tests/hdnoclaim.py` is the gate, and it replaced `hdmove` |
| **HDD** second image (`HDDTOOL.DRV`) | **PINNED (forever)** | base is CS (§52.11.7). Claimed top-down. **It carries the Control Panel page now** (§52.13), so it is claimed while the panel's Drives page is open and not only while the disk tool is — freed at `DSV_CPCLOSE` by `hd_tool_reap` either way |
| **RAM disk** second image (`RAMPAGE.DRV`) | **PINNED (forever)** | base is CS (§62.9.9) — `[rd_pfar]` is `PKG_DISP:segment`. Claimed top-down |
| **Sheet** REGION | **MOVABLE** | §66.6.1, and it was the first region in the tree to move. It was pinned again for a while because the binds stamp CS into the two module vector tables (`ch_v_*`, `sh_v_*`); those tables are in SHEET's own data, so `sh_rgnreloc` re-runs both binds after a move and that is the whole fix (NO-PINNING P1). The second stated reason - the modules' far returns across their file calls - never bound the REGION, which `mem_in_nest` pins while a callback stands; it bound the module claims, below. `tests/sheetmove.py` 3c is the gate |
| **Sheet** chart module 52KB, macro module 28KB (parts 0 and 1 of `SHEET.O88`, §82.16.12; `CHART.OVL` and `MACRO.OVL` before it) | **MOVABLE** | §82.16.11 (NO-PINNING P1b). Code modules, so the hazard was the MODULE's CS on the stack: every module-to-resident call (`SHOUT`, `CHFP`), the macro module's calls into the chart module (`CHOUT`) and every claim-capable kernel slot a module calls (`SHK`) now go through `ch_xthunk`, which swaps the stacked CS for a tag and returns through the module's segment as it is NOW. The macro module's text channels read into its own claim, so those two file calls PIN it for the call (`sh_m2pin`/`sh_m2unpin`, `op_pin`/`op_movable`). Declared with `op_movable` after `op_fetch`; the standard's `op_lazymv` puts the row right and jumps to `sh_partmv`, which is `ch_ovreloc`'s two stores and one more for `sh_m2far`. `tests/sheetovlmove.py` moves the chart module under a live `WM_SHOW` and watches the return land in the new home |
| **Sheet** cells, text, borders, notes, chart, and the 32KB staging | **MOVABLE**; the staging PINNED only across each `OSAPI_FILE_READ`/`WRITE`, bucket C | All ~99KB, and Sheet hires NO WORKER so `mem_can_move` passes them on `I_TASK = 0xFF` alone — no park is involved at all. `sh_reloc` is a TABLE and not a ladder of compares: it patches **every** word that names the old base, because `os88chart.inc`'s `ch_srcseg` / `ch_stgseg` / `ch_srcseg2` are borrowed second copies and §66.1 is the record of a word-poke design that failed on exactly that. `tests/sheetmove.py` is the gate. **The staging joined them in NO-PINNING 1**: it is the `ES:BX` of every file call the package makes (§66.9 reason 4), and every one of those goes through the two resident stubs `sh_k_file_read`/`sh_k_file_write`, so `sh_stgpin`/`sh_stgunpin` there pin it for exactly the call (§66.5.7.1) - one place for thirteen call sites. `sheetmove` 3b (moved and followed) and `sheetmove-rt` (the pin seen INSIDE the save and the load, and a save + reload after the move) are the gates |
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

## kern_mega is a third axis: which claims stay BELOW 1MB

On kern_mega a package whose class byte says `OS88_HIGHMEM` gets its plain
claims of 64KB or less as selectors ABOVE 1MB (docs/plans/KERN-MEGA-PLAN.md
13.6, item 81), so most of the rows above never touch the conventional heap
there. Every package declares the bit or is registered in
`tests/megahigh.txt` with a reason (KERN-MEGA-PLAN 14.52, `t_megahigh.py`,
the full tier); one is, PiXEL. What still lands in conventional memory is
the DMA door (`OSAPI_MEM_CLAIM_DMA`/`_HI`, the parts library's carve and
lazy fetch included), a claim over 64KB, and PiXEL's. **KERN-MEGA-PLAN
14.52.4 is the register of those**, one row a claim with the owner's three
tests - held only during active use, claimed bottom-up, movable while idle
- and its verdicts: the open ones are the Apple II's and C64's ROM carves
(movable while idle since `CC_ONMOVE_PARTS`, SPEC.md 73.14.2.1, but still
claimed through the DMA door) and `os88type.inc`'s face cache (undeclared). Two were closed by
KERN-MEGA-PLAN 14.55: **MIDIRack's external ring** (17KB, `mrb_seg`) is
claimed at every Play and freed at every stop on every kernel (SPEC.md
105.8.4), and **a DMA ring is claimed BOTTOM-UP on kern_mega** - any
`OSAPI_MEM_CLAIM_DMA_HI` with a head, so 1942's PCM rings, the Video
Player's card ring, MIDIRack's and `SOUND.DRV`'s pool and double buffer
(SPEC.md 50.3.2, 34.5.3), which kern_big and kern_small still take from the
top. All of them are held only while they play. KERN-MEGA-PLAN 14.59 took
the two that were left in MIDIRack: a refused external ring is no longer a
refused Play (the grant ring out of `SOUND.DRV`'s pool plays instead), and
**its speaker/Covox ring** (5KB, `mrk_seg`, a plain claim - so above 1MB on
kern_mega, and conventional heap on the other two) is claimed only by a
speaker or Covox Play on the ISR's road and freed at every end of a session
but a pause (SPEC.md 105.8.3), where it was held from the first speaker play
to the window's close.
Clear Skies' flight shadow, below, is one of the claims 14.52 moved: pinned
by not declaring on kern_big, a pool selector on kern_mega.

## Regions — the block a package RUNS in (§66.6.1, §66.6.2)

A region is a claim like any other and the same two columns decide it, but the
holder is unusual: **every word that names it is the KERNEL's**, so its
relocation proc is very nearly always a `ret` and `OS88_REGION_MOVABLE` emits
one. What actually decides a region is the *worker*.

**43 of 46 packages declare, and the three that do not are a list rather than a
shrug** (and since NO-PINNING P2 no package that hires a worker is exempt from
restarting it: Frotz's `worker` line was the last) — §66.6.1.1's ratchet, `tests/unit/t_movable.py`, `tests/movable.txt`.
It was 42 of 44 when the ratchet landed; the two added are the four-piece
`DOS.O88`'s halves (§96.44.5), and the third exemption is its loader.
The table below names the ones with something to say; everything not in it
declares the bare form and moves on `I_TASK == 0xFF`.

| package | region | verdict | note |
|---|---:|---|---|
| **SHEET** | 48.5KB | **MOVABLE** | §66.6.1's first customer, and worker-less, so it moves on `I_TASK == 0xFF` alone |
| **Word** | 58.0KB, and part 1's 2.7KB in the same claim (SPEC.md 68.10) | **MOVABLE + RESTARTABLE** | the largest region in the tree that hires a worker. `wd_worker` polls four statics and sleeps; a restart costs one poll |
| **Tank Attack** | 30.9KB | **MOVABLE + RESTARTABLE** | a game loop, every value a static; a restart costs one frame. No move row can cover it — it redraws for ever, so `settle` never returns |
| **ftpd** | 28.2KB | **MOVABLE + RESTARTABLE** | `fd_step` is a state machine in statics and the restart lands at the loop top, above it, so a transfer resumes at the step it had reached |
| **Audio** | 30.2KB | **MOVABLE + RESTARTABLE** | hires only when playback starts; until then it is movable on `I_TASK` alone |
| **Browser** | 19.8KB | **MOVABLE + RESTARTABLE** | `br_nstep` banks a generation and every store is guarded on it, so a fetch a restart abandons writes nothing — `br_abort`'s own design |
| **kernel module images** | 2–8KB | **PINNED, and measured harmless** | `CTRL.DRV`, `FORMAT.DRV`, `DISKOPS.DRV`, `HIBER.DRV` (+ `FILECP`/`FDLG` on kern_small). Claimed top-down, and `mem_claim_1`'s `.hi` arm puts one back at the ceiling when it is dropped and re-taken — so it strands nothing, and the descending pass closes any hole that opens beneath it. Measured: the Control Panel open costs **8.0KB, exactly its own size**, not a trapped run. The one exception used to be a module sitting under an attached `SOUND.DRV`; since §66.6.3.1 that image moves too |
| **driver images** | 48KB | **MOVABLE** | §66.6.3. SOUND 6KB, HDD 8KB, ETHER 18KB, RAMDISK 9KB, NET 6KB, VMMOUSE 1KB (`drv_memk`). Every one of them, the sound driver's included: §66.6.3.1 patches the **sixteen hardware IRQ vectors** instead of refusing on the table, and the copy runs at IF=0 so no ISR can be taken between it and the rewrite. Sixteen and not 256 is load-bearing — unused vectors are SCRATCH, and a full sweep rewrote an XT-IDE option ROM's word and left the machine with no hard disk. The other side of it is a contract: a driver may hook its own IRQ vector and nothing else. `tests/drvmove.py` and `tests/sndmove.py` are the gates |
| **every C package** | — | **MOVABLE** | `crt0.asm` declares it at entry; `cc_regreloc` is `cc_ovbind` where there is an overlay, because its `.res` loop writes the live `CS` into the return vectors and inside a relocation proc that CS is the new base |
| **CWORD**, **RUNCPM** | 60.8 / 56.6KB | **+ RESTARTABLE** | `os88_task_restartable(1)` after the spawn takes; both workers are `for(;;)` polls over statics |
| ArtfulType, Fractal, ModPlug, Note Pad | | **MOVABLE, NOT restartable** | the four that declare `OSAPI_MEM_PARKSAFE` and still hire a worker (Tracker declares both — it is park-safe *and* restartable, and §66.4.3.3 is what that cost). Park-safe lets the kernel stop the worker while it is blocked in `gfx_lock` — anywhere in its loop, including halfway through a frame or a mixed buffer — so the honest declaration is a *window* around `OSAPI_TASK_ALIVE`, not a blanket. Registered `worker` in `tests/movable.txt` |
| **Frotz** | 23.4KB | **MOVABLE + RESTARTABLE** | since NO-PINNING P2 (§66.5.9.3). It is NOT park-safe, so it parks only inside `OSAPI_TASK_ALIVE` - the burst loop's top and seven waits - and re-executing the opcode in flight there is LOSSY, so `zx_restart` RESUMES THE WAIT instead: `[zx_rsm]` names it, and each wait's resume entry rebuilds the frame its routine pops and takes `[zi_linen]`, `[zf_req]`/`[zf_reqres]` and the rest as they stand in bss. `[MORE]` withdraws the declaration while it is up (the decoder's place is on the stack), so the region is pinned for that wait alone. `zrestart` is the gate, every wait with the region moved against a boot that moved nothing |
| Arkanoid, Cyclone, Dot Delirium, Missile, Pac-Man, TameGram, Task Manager, Telnet, The Wire, WIREFRAME | | **MOVABLE + RESTARTABLE** | the ten converted in one pass, and they are one argument: none declares `OSAPI_MEM_PARKSAFE`, so the only park point is `OSAPI_TASK_ALIVE`, each has exactly one and it is at the top of the outer loop, and every byte that outlives a pass is a static. Six of them open `GET_TICKS; mov [x_due], ax` above that loop, so the restart *re-seeds* the frame deadline |
| **PACCMAN** | | **MOVABLE + RESTARTABLE** | the C one of that set, and the one a grep missed: its worker is hired from `os88_paint`. The single automatic crossing its park (`due`) is re-seeded by the restarted entry, which is `cc_worker` and not `os88_worker` — the SDK names the offset so a C author cannot pick the wrong one |
| Calculator, Chart, Font View, Hello, Mines, Paint, Piano, Recorder, Solitaire, TexPad, and the rest | | **MOVABLE** | no worker, nothing of their own to fix: the bare form, moving on `I_TASK == 0xFF` alone |
| **WEAVE** | | **MOVABLE, restartable** | `w_chire` declares `os88_task_restartable(1)` at the spawn: the worker's body is in `WEAVE.WSM` (part 0), which keeps its state in its OWN claim, and a restart re-enters it through `wcv_run`. Part 0 itself is pinned from that spawn (the worker parks inside it), and `CC_ONMOVE_REGION` tells `os88_onmove` of the region's moves for the WJS VM's banked DS (WEAVE-SPEC 1.2.6) |
| C64, LOOM, APPLE2, CWORD, RUNCPM | | **MOVABLE** | C packages that hire no worker; `crt0.asm` declares for them |
| **SCRIBE** | 50.0KB, and part 1's 9.0KB in the same claim (SPEC.md 95.8.8) | **MOVABLE + RESTARTABLE** | Word's shape and Word's worker, one fork along. It was the one PINNED region in the tree: a hand-rolled `SCRIBE.OVL` with a CS of its own was told the package's segment by stamps meant to last the session. That module is part 1 of `SCRIBE.O88` now, assembled at `SC_P1ORG` in the program's own segment, so nothing is stamped and the proc is a bare `ret`. `sc_worker` polls four statics and sleeps; a restart costs one poll |
| **SKIES** | 55KB | **MOVABLE** | one of the tree's two re-homed programs (§20.12.10). It declared with a `cs_reloc` for a cycle, on the belief that the title art was inside its carve and moved with the region: it is NOT — `csl_art`'s `op_fetch` expands the bands into a claim of their **own**, outside the carve (SPEC.md 88.10.4.1; slot-owned after the re-home, pinned, never moved), so a proc adding the region's delta to `[cs_artseg]` would have pointed the title page at 11KB of whatever the compactor packed there on the first move. Bare `ret` now (§66.6.1.1), and since set 6 of the apps size pass the art lives INSIDE the region's bss, read by DS-relative offset, so it moves with the region and still needs no proc - its packed copy too (`cs_artz`, SPEC.md 88.10.7.1), which `OSAPI_DECOMP` reads at `DS:cs_artz` with DS whatever the region is at that moment; `[cs_shseg]`/`[cs_wgseg]`/`[cs_gseg]` are two claims of their own and the kernel's glyph table, so nothing here names the region |
| **DOS** | ~15KB | **MOVABLE** | the second re-homed program, and the one §66.6.1.2 was built for: it is **part 0** of a four-piece `DOS.O88` (§96.44.5), so its region is the loader's CARVE, which the re-home TRIMS to the program (§20.12.10.5) — it ran 512 bytes up it for a cycle, `0x8FC0` against `I_SPTR`'s `0x8FE0` on the shipped package, and four places in the compactor read the claim's base where they meant that segment, so the declaration was refused and the box was a wall: measured at **426KB against 440** for a DOS program on a Sound Blaster machine, exactly `SOUND.DRV`'s image plus its ring in a hole above it. The proc is a bare `ret` — unlike SKIES it banks no segment of its own — and `tests/dosarena.py` reads `MC_RLOC` back beside the arena figure, because a refused declaration and a compaction that cannot reach the hole are the same number and different bugs |
| **CSLOAD**, **DOSLOAD**, **SCLOAD** | | **nothing to declare** | the launch images: it reads the parts, hands off through `OSAPI_PKG_REHOME` and its region is freed inside that call — and until then it *is* `[ld_base]`, which `mem_frameless` pins anyway. What each leaves behind is the carve, declared by the part that inherits it |

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
