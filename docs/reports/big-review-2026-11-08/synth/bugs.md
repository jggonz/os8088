# Bug and hazard register - kernel review of 2026-11-08

This is the register of DEFECTS the 25 area reviews found: every finding of kind
`bug` or `hazard` that survived verification, plus the findings filed under
another kind (`weakness`, `consistency`, `perf`) that describe something that is
wrong rather than something that could be better. The same defect often arrived
from two or three angles; each such group is merged into ONE item here, and every
source finding id is listed against it so the area reports
(`docs/reports/big-review-2026-11-08/areas/*.md`) and the verified data
(`.../data/*.json`) can be followed back.

It is a dated measurement of one tree (the branch `the-big-review` at the time of
the review), not a maintained document.

## How to read it

**Inputs.** 170 findings of kind bug or hazard (66 bug, 104 hazard) across the 25
data files, of which none is refuted, plus about 60 findings of other kinds that
are defects. Two findings were refuted and are DROPPED: isr-5 (WireBall's queue
tail) and wm-20 (wm_anim yields holding the gfx lock). Verdicts are carried
through: **confirmed** (the verifier re-derived it), **corrected** (the verifier
changed a detail, a byte count or the fix; the corrected form is what is written
here), **unchecked** (not re-derived; lower confidence, said so where it occurs).
A finding the verifier marked PLAUSIBLE (argued from code, not run) says so in
its mechanism.

**Ranking.** Severity x likelihood ON REAL MACHINES x blast radius, each scored
H/M/L. Likelihood is about the field: "every machine, every day" is H; "a common
configuration or an ordinary action" is M; "a hostile file, a package breaking a
stated rule, or a narrow race" is L. Blast: H is data or memory corruption, a
dead machine or a lost session; M is a feature broken or wrong for the session;
L is cosmetic or a cost. Ties go to the SHIPPED kernels (kern_big, kern_small)
over kern_mega, which is budgeted but unreleased.

**Kernels.** `small` = kern_small, `big` = kern_big, `emu` = kern_emu,
`mega` = kern_mega. kern_emu and kern_mega both define KERN_BIG, so anything
marked `big` reaches them too unless the item says otherwise. "modules" means an
on-demand `.DRV` (named).

**Bytes** are signed and are the reviewers' figures, as corrected by the
verifiers. `res` is RESIDENT, and `.cold` IS resident (it is billed by
KERN_BUDGET exactly as `.text`; only its CS differs). `.boot2`, `.ovl` and the
boot sector are transient. Module bytes are named by module and cost no resident
byte. On kern_mega, `.cold` and `.mega` go above 1MB after the switch, so they
do not count against MEGA_RESIDENT's 68KB of conventional memory; `.text` and
`.lowbss` do.

**ABI.** `none`; `SDK text` (an `apps/os88api.inc` contract comment changes, so
`python3 tools/abilock.py --update "why"` WITHOUT a PKG_FMT bump - a contract
clarified); `additive` (a new cell or flag at the end, no bump); `flag day` (a
break, to be batched with docs/plans/ABI-BREAK-LIST.md). Almost everything here
is `none`: these are defects, and the fixes are inside the kernel.

**Rows** are `tests/suite.py` rows (tier in brackets) that cover the AREA - the
rows an implementation session names and runs once in its final pass. "no row"
means nothing in the suite reaches the defect; most items say what row would.
A row that covers the area has not, by being green today, shown the defect is
absent: almost none of these defects is exercised by any row, which is why they
survived.

## The fifteen that matter most

| rank | id | defect | kernels | fix bytes |
|---|---|---|---|---|
| 1 | BR-1 | The boot canary protects one of the six shipped system disks | big, small | 0 res; +8 .boot2 (pays from boot-7's -41) |
| 2 | BR-2 | kern_small: a shed during a mount writes the listing into the FAT window | small | +10 .cold |
| 3 | BR-3 | gfx_blit4 trusts the sign of its width: 64KB of heap overwritten | all | 0 |
| 4 | BR-4 | A dying worker repaints and drops DESK.DRV on its own 128-byte slice | all | +15..+30 res |
| 5 | BR-5 | One global worker count: every driver unload waits 5 s on USBMOUSE's worker | big | +25 .cold, +11 .bss |
| 6 | BR-6 | A plain MDA is driven with Hercules graphics CRTC timings | big, small | +25..+35 .boot2 |
| 7 | BR-7 | The loader trusts a package header against its own region | all | ~+24 .cold |
| 8 | BR-8 | wm_ptr2idx divides an unvalidated window pointer (INT 0) | all | +5 res, or wm-2 net +4 big / -2 small |
| 9 | BR-9 | No divide-error or invalid-opcode handler: one INT 0 is a dead machine | big, small | ~+30 res, +12 .ovl |
| 10 | BR-10 | Esc on the Resume face deletes the hibernated session | big | +0..+4 HIBER.DRV |
| 11 | BR-11 | The purge floor is lifted by any teardown: a paste stops part-way and reports success | big, small | ~+8..+18 .cold |
| 12 | BR-12 | mem_regrow copies a whole canvas with interrupts off (~170 ms per 64KB) | big, small | +10..+16 .cold |
| 13 | BR-13 | kern_mega: a reflection on the lent stack reads .lowbss through SS (#SS, halt) | mega | +16..+40 above 1MB |
| 14 | BR-14 | kern_small's fsx_page turns Hercules page 1 on beside a CGA | small (big guard reshaped) | +13 small, +2 big |
| 15 | BR-15 | The clock gains about 31 seconds a day | all | 0 |

Close behind, and the first to take if a batch has room: BR-16 (media-change
status discarded), BR-17 (FAT16 write leaks every free cluster on Disk full),
BR-18 (16550 FIFO left on silences the mouse), BR-19 (a tick parks the AT ROM's
int 09h with IRQ1 in service).

---

## Register - high and medium

### BR-1 The boot canary protects one of the six shipped system disks

- **Sources:** boot-1 (bug, high, confirmed), boot-2 (hazard, high, corrected), boot-10 (rethink), boot-7 (size; supplies the room).
- **Kernels:** kern_big 360KB/720KB (canary inert), kern_small 360KB/720KB and kern_big 1.2MB (canary defeated by the splash). Only kern_big 1.44MB is protected today.
- **Rank:** S high, L medium (an EOT-8 clone BIOS, or a 286 with Cylinder ticked), B high (every multi-sector write of the session crosses heads on that BIOS).
- **Mechanism:** KSIG_OFF 4096 is file sector 21 = LBA 35 on kern_big's 9/2 disks, inside run 1 (LBA 27..35), which lies on head 1 alone and crosses no head (boot/boot2.asm:318-322, 713-723; Makefile:1021). Kernel size pass 14 (BOOT2_SECS 12 -> 13) moved it; `t_canary`'s `crosses()` walks from the data area instead of data+BOOT2_SECS and so still passes (tests/unit/t_canary.py:99-117). Separately, the splash's first tick far-calls vid_setmode and gfx_rowbase in .text sectors 3-4 as soon as SPL_RESIDENT sectors land - BEFORE the canary runs at the end of the load (boot2.asm:350-377, 419-473). On kern_small's 360/720 (.text at head 0 sector 6) and kern_big's 1.2MB the first crossing run lies inside those sectors, so the bad delivery executes before the shorten-and-reload can rescue it. A missed canary publishes dsk_cylrun = 1 (disk.inc:5343-5346).
- **Fix:** KSIG_OFF := 22528 (file sector 57; the legal band over every shipped system image and blob length is {57, 106..110}) and its equ in boot2.asm: 0 bytes. Fix `crosses()` to start at data + boot2_secs and break it on purpose (KSIG_OFF back to 4096 must turn os8088-360.img red). Keep b2_runmax at the TRACK bound until SPL_RESIDENT sectors are aboard: +8 .boot2, which needs boot-7's deletion of stage 2's dead DPT copy (-41 .boot2, every kernel). Later, boot-10: the build computes KSIG_OFF from the shipped geometries instead of checking a typed value.
- **Bytes:** 0 res; .boot2 +8 and -41. **ABI:** none. **Confidence:** confirmed / corrected.
- **Rows:** canary (fast) - must go red on the old value; blobruns, splashbar, splashspin, smallboot, kzboot (soak); bootsmoke, small128 (full). No row models a BIOS that flips heads at the wrong sector.

### BR-2 kern_small: a shed during a Disk-window mount hands the listing's destination to the read-ahead or FAT window

- **Sources:** files-1 (bug, high, confirmed).
- **Kernels:** kern_small.
- **Rank:** S high, L medium (the 128KB machine is under memory pressure by construction), B high (FAT-window bytes overwritten in RAM are written back to the disk on the next write).
- **Mechanism:** fmv_load aims dsk_dest at the window's purgeable FS_VSEG claim, then mounts (files.inc:1693-1715). At mount step 2, before step 3 writes the listing, dsk_rah_want and dsk_fatw_claim claim at their own rank (disk.inc:4252, 4278, 5991, 7505-7570), and mem_shed_one takes the cheapest record below them (memory.inc:1403-1438) - once the TRIV caches are gone, this window's own cache. fmv_demote zeroes FS_VSEG and [fm_vseg] but not [dsk_dseg] (files.inc:1408-1437), so the retrying claim can receive those paragraphs and step 3 writes entries through `mov es,[dsk_dseg]` into it. SPEC 22.6.3's "the one claimer in the whole mount is asc_use" is false (SPEC.md:53470); ico_need/asc_use have the same exposure.
- **Fix:** in mem_shed_one, under %ifdef KERN_SMALL, skip a record whose MC_SEG equals [dsk_dseg], beside the dirty-FAT-window skip.
- **Bytes:** +~10 .cold res kern_small; 0 kern_big. **ABI:** none. **Confidence:** confirmed.
- **Rows:** fcpsmall, fdlgsmall, fdlgchsmall (soak); small128 (full). No row: add one that fills a 128KB heap until VIEW caches are the cheapest record, navigates a B: window and compares the FAT window with the disk; it must go red without the skip.

### BR-3 gfx_blit4 trusts the sign of its width and height

- **Sources:** gfx-1 (bug, high, corrected).
- **Kernels:** all (planar half big/emu/mega; 1bpp half both; a #GP on mega).
- **Rank:** S high, L medium (an empty rect's x2-x1+1 from a package; PaccMan hit exactly this in gfx_blitp), B high (VGA: 64KB of VGABUF_SEG from vga_pbuf, the bottom of the heap above the kernel; 1bpp: ~8KB of framebuffer per row, into the CGA aperture on a dual-card Hercules).
- **Mechanism:** gfx_blit4 refuses only zero (vga12.inc:1886-1889). Both decoders prove the block on screen with an unsigned add that wraps (2011-2013, 2050-2054), and `cmp cx,16 / jb` passes any negative. With x=20, w=-10 vga_blit_prow's group count is 0, so `dec bp / jnz .g` runs 65,536 groups storing at [di], [di+84], [di+168], [di+252] (2551-2661). gfx_blitp already carries the jle fix (2856-2859).
- **Fix:** `jz vga_b4out` -> `jle vga_b4out` at both sites (same 2-byte encoding). Optionally the same for gfx_blit1_x's jcxz.
- **Bytes:** 0 .text; +2 .cold if gfx_blit1 is taken too. **ABI:** none. **Confidence:** corrected (group count, not size of the damage).
- **Rows:** dispblit, blitcut, blitpair, blitplane, atblit, atblitvga (soak). None passes a negative width; the row should.

### BR-4 A dying worker tears down windows, and drops DESK.DRV, on its own stack slice

- **Sources:** histsys-1 (hazard, high, confirmed), hm-3 (hazard, medium, confirmed), mem-10 (hazard, low, confirmed).
- **Kernels:** all (the DESK.DRV half is kern_big).
- **Rank:** S high, L medium (closing Bounce, or any 192-class package with a worker, over another window or the dock), B high (STACK OVERFLOW panel at close; on kern_big the UI task resumes in a freed claim; on kern_mega a #GP through a freed selector).
- **Mechanism:** inst_pkg_alive's .die runs inst_task_die on the WORKER (instance.inc:2196-2210, 1921-1947): gfx_lock, inst_unwin -> wm_destroy -> wm_paint_dmg, which can reach another package's W_PAINT, wm_dock_under and menu_draw_bar's font_run chain, then wm_destroy_seg and desk_drop_x. Bounce's slice is 128 bytes (apps/bounce/bounce.asm:32) less a 64-byte interrupt floor; STACK-SLOTS-PLAN 12.4 sized CWORD/RUNCPM at 256 for this exact path and `t_stkclass` checks only the worker's own depth. desk_drop_x -> mod_drop on the worker can free DESK.DRV while the UI task is pre-empted inside dkd_hover, which it calls with no lock (ui.inc:1006); mod_disarm's cli guards only the slot write (mod.inc:880-886, 947-960). PLAUSIBLE, unmeasured.
- **Fix:** inst_task_die sets I_TASK = 0xFF, posts the window's wm_clreq bit (or a reap byte for a windowless record) and task_exits; the UI task's .cl_slot then runs app_close_win's task-less arm on the 1KB stack. This also moves the module drop onto the UI task, which is hm-3's fix. Add a stkwater reading of Bounce closed over a second window.
- **Bytes:** +15..+30 .text/.cold res. **ABI:** none. **Confidence:** confirmed (overflow plausible, not measured).
- **Rows:** stkclass, stkapps (fast); stk0water, stkpanel, timerrace, deskbench (soak).

### BR-5 One global drv_wcnt makes every driver's unload wait on every driver's workers

- **Sources:** drivers-1 (bug, high, confirmed).
- **Kernels:** big (+emu, mega); OS88_DRIVERS.
- **Rank:** S high, L medium (any machine with USBMOUSE.DRV up, i.e. every PicoMEM USB-mouse machine), B medium (5 s stall per untick; a detached SOUND.DRV stays published and snd_tick far-calls its DSV_TICK; a second DETACH restores stale vectors; 5 s per earlier row at reboot).
- **Mechanism:** drv_wcnt is one byte (driver.inc:6139). drv_unload_x waits 91 ticks for it and on timeout `jmp short .out` skips drv_release, leaving the row loaded and its class published (4171-4189). USBMOUSE.DRV's worker lives as long as the driver (drivers/usbmouse/usbmouse.asm:139-149). ctrl.inc:7140-7146 clears WANT/ERR whatever happened; drv_shutdown walks rows in order (driver.inc:4208-4222); memory.inc:2127 pins every driver image on any non-zero count. NO-PINNING-PLAN K1b deferred the per-row count "until a second driver spawns", which one now does.
- **Fix:** a DRV_MAX-byte per-row table, indexed at spawn (drv_pub_seg -> drv_row_ix_of) and at exit (drv_owns_seg_x); unload and the image pin test the row's byte; on timeout also clear the class's publication.
- **Bytes:** +~25 .cold, +11 .bss res (DRV_MAX on kern_big). **ABI:** none. **Confidence:** confirmed.
- **Rows:** usbmouse, heapdrv (soak); drvmem (fast). No row unticks a second driver while USBMOUSE is up.

### BR-6 A plain MDA primary is programmed with Hercules graphics CRTC timings

- **Sources:** video-2 (hazard, high, confirmed).
- **Kernels:** big, small.
- **Rank:** S high, L medium (MDA is the period's commonest mono card; MDA+CGA with switches at mono is the commonest dual-monitor machine), B high (dead screen at best; period documentation warns about out-of-spec drive of a fixed-frequency 5151; the usable CGA sits idle).
- **Mechanism:** vid_detect makes every mono-flagged machine VID_HERC (viddet.inc:588-594; SPEC 39.1's documented scope cut), and vid_setmode writes vid_6845's 720x348 table, R0 = 35h (397-398, 1391-1413): about 33 kHz horizontal against the 5151's 18.4 kHz. vidsel.inc's alias and 3BFh retry test already discriminates MDA from Hercules, for the SECONDARY only (145-152, 231-253).
- **Fix:** in the mono arm write 3BFh = 1 and run a 0x1000 alias test (vid_memchk's idiom) or Wilton's 3BAh bit-7 toggle. An MDA with a CGA present probes B800 and becomes VID_CGA (vid_cga_equip already reaches it); an MDA alone halts with a message. Remove the scope cut. On kern_big vid_memchk is in .ovl, so the blob needs its own copy.
- **Bytes:** +25..+35 .boot2 (transient; may cost a blob SECTOR of disk on kern_big), 0 res. **ABI:** none. **Confidence:** confirmed (the monitor-damage risk is lore, not verified; the dead screen and idle CGA stand on the code).
- **Rows:** no row. MartyPC's `_vga_mda` profile carries the MDA as a secondary; an MDA-primary profile is wanted.

### BR-7 The loader trusts a package header against its own region

- **Sources:** loader-1 (bug, high, confirmed), loader-3 (bug, medium, confirmed), loader-2 (bug, medium, confirmed).
- **Kernels:** all.
- **Rank:** S high, L low-medium (a corrupt or foreign file; The Wire feeds the image arm network bytes), B high (memory above the region overwritten, with top-down placement the last-launched package's code, instead of "Bad package").
- **Mechanism:** ld_check_hdr bounds the image only to >= 32 and against the file (loader.inc:445-449). Step 6a computes a clear prefix of up to 320 bytes (1018-1035) and does `sub cx,bx / sub dx,bx` with no carry test (1066-1067): image 64 with prefix 320 gives DX = 0xFF00, and one literal plus an offset-1 match floods past a 512-byte region. ld_where's two round-ups can exceed roundup512(image+bss) by 512 on the read and 511 on the image-arm copy (858-889, 983-991, 1923-1933). The disk-swap re-check rewrites [ld_img]/[ld_bss]/[ld_ent] from the in-region header without re-bounding against [ld_need] (1091-1093, 1167-1175), so step 7's rep stosb zeroes past the region (1303-1307). The module's cmz_uncomp/cmz_hdr and os88pkg.py check what the kernel does not; SPEC 20.13.8.1 wrongly says the output bound keeps writes inside the region (SPEC.md:49072-49075).
- **Fix:** compute the prefix once in ld_check_hdr (it already holds the flags in DL), keep it in a .bss word, and test image > prefix, file > prefix and R + file <= roundup512(image+bss) there, where a refusal owes no unreserve; step 6a reads the word. After the swap re-check, `cmp dx,[ld_need] / ja` to an LD_EBAD exit.
- **Bytes:** ~+18 .cold (loader-1 with loader-3) + 6 .cold (loader-2) = ~+24 .cold res, ESTIMATED. **ABI:** none. **Confidence:** confirmed.
- **Rows:** lzfence, lzload, lzload-lz4, lzfmt-all, lzbig (soak). None feeds a hostile header; one should.

### BR-8 wm_ptr2idx divides an unvalidated window pointer

- **Sources:** wm-1 (hazard, high, corrected), wm-2 (size, the alternative fix), wm-13 (consistency, medium), histsys-11 (weakness, low); feeds BR-9; BR-29 is the loader's road into it.
- **Kernels:** all.
- **Rank:** S high, L low (a wild BX from a package, or a stale one from a worker), B high (INT 0: the IBM 5150/5160 ROM's stub masks the 8259 and IRETs - every interrupt gone; a 286+ re-executes the div for ever).
- **Mechanism:** `sub ax,wm_wins / div cl` (wm.inc:7278-7301) overflows AL for any BX below wm_wins or 256*WIN_SIZE or more above it (18,944 bytes on kern_big, 16,640 on kern_small); the clamp comment's "wrong and bounded" holds only inside the table. Unvalidated roads in: OSAPI_WM_SHOW/HIDE/FRONT through wm_su_precover and wm_zabove (7317-7335), OSAPI_WM_DESTROY twice, OSAPI_WM_CLIP_SET from a worker (6380-6388). Validation across the doors is inconsistent: wm_isrec on three, inst_wchk on two, none on the rest, and the inline setter cells write `[cs:bx+W_*]` (wm.inc:1530-1536). OSAPI_WM_WAKE writes `[bx+W_WKQ]` from ISR and worker context with "BX is trusted" (wm.inc:2343-2369; os88api.inc:1726).
- **Fix:** bound before dividing, `cmp ax, MAX_WIN*WIN_SIZE / jae clamp` (+5 .text). Or take wm-2: store the slot index in the record (W_IDX; WIN_SIZE 74 -> 75, 65 -> 66 - WIN_SIZE is not in the SDK) and make wm_ptr2idx a bounded load. wm_isrec at wm_wake's entry and MEMC_POST (+6..+12 .text). Write the BX-trust rule into SPEC 11 either way, and fix inst_wchk's same off-by-one comment.
- **Bytes:** +5 .text res; or wm-2 at -8 .text +12 .bss = +4 net on kern_big, -2 net on kern_small, ~-80 clocks a call; plus +6..+12 for the wake fence. **ABI:** none. **Confidence:** corrected (the overflow boundary).
- **Rows:** int0sweep (soak) is the detector. No row passes a wild window pointer.

### BR-9 No divide-error or invalid-opcode handler on the real-mode kernels

- **Sources:** histsys-2 (hazard, medium, corrected), isr-8 (weakness, medium, confirmed). BR-8, BR-23 and BR-29 are the reachable divides this would turn into a report.
- **Kernels:** big, small (and mega before its switch).
- **Rank:** S medium, L medium (package code dividing hostile data reaches it; SPEC records SKIES' ADI and a ZMODEM size field doing so, SPEC.md:165882-165890, 127741-127743), B high (dead machine on the IBM ROM; silent hang on a 286+; garbage on GLaBIOS).
- **Mechanism:** nothing writes IVT 0 or 6 outside the BOOT_MARK knob (kernel.asm:7727-7758) and kern_mega's IDT; instance.inc:1976 and ctrl.inc:5975 both say so. It contradicts the kernel's own "a halted machine is a bug report" (sched.inc:2601-2612).
- **Fix:** generalise sch_diepanel into a system-error panel that takes a message pointer; two 4-byte stubs - int 0 always, int 6 when cpu_tier >= 286, never int 5 (PrtSc on a PC) - that set a code and enter it with the faulting CS:IP; install from .ovl at sched_init (mind sched-7: that .ovl is 5,119 of 5,120 bytes). Later: kill the instance when CS is a package segment on a worker, kern_mega's mega_fkill shape.
- **Bytes:** ~+30 .text/.cold res, +12 .ovl. **ABI:** none. **Confidence:** corrected / confirmed.
- **Rows:** int0sweep, stkpanel (soak).

### BR-10 Esc on the Resume face discards the hibernated session

- **Sources:** hiber-7 (weakness, medium, confirmed).
- **Kernels:** big (HIBER.DRV).
- **Rank:** S medium, L high (Esc is the natural "not now" on a question at boot), B high (both files deleted with no confirmation; the session is gone).
- **Mechanism:** hbm_onkey maps Esc to button 2 in every mode (hiber.inc:1366-1382); in HB_M_RESUME button 2 is .discard -> hbm_forget (1411-1414). The close box already does the non-destructive thing (hbf_closed, 738-744). SPEC 87.5 knows Esc is Discard and only drains key repeats (SPEC.md:158622).
- **Fix:** in RESUME mode Esc closes the window and keeps the files; Discard needs the click or a letter key.
- **Bytes:** +0..+4 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernate, hibernatedrv, hibernatem (soak).

### BR-11 The purge floor is one word, stamped with a task that names nobody, and lifted by any teardown

- **Sources:** mem-4 (bug, medium, confirmed), files-4 (hazard, low, confirmed), hm-13 (weakness, low), loader-8 (weakness, low).
- **Kernels:** big, small (the paste is FILECP.DRV on kern_small).
- **Rank:** S medium, L medium (closing any window while a multi-file paste asks an overwrite question, under memory pressure), B high (the paste silently stops part-way and reports success).
- **Mechanism:** OSAPI_MEM_FLOOR stamps [sch_cur] (memory.inc:5848-5853), which is 0 for every package callback, the DOS and the kernel's paste alike. mem_free_rec_x writes MEM_LVL_TOP on every instance teardown, a Disk window's included (4826); fcp_lend does the same (filecp.inc:2348); fcp_paste's multi-file arm overwrites another task's floor with MED (457-459). The next entry's copy buffer then sheds MEM_P_FLIST and fcp_lnext reads [fcp_lseg] = 0 as DONE (532-547). ld_unreserve, a drifted twin of mem_free_rec_x, misses the floor lift, so an aborting entry proc leaves its floor standing (loader.inc:216-262).
- **Fix:** stamp the floor with inst_caller's instance (DH; 0xFF for the kernel) as well as the task; lift it in mem_free_rec_x and fcp_lend only when the stamp names the dying instance; fcp_paste does not set a floor over another's. For loader-8, write I_KIND and I_SPTR at step 5 (the record is unpublished), so ld_unreserve becomes desk drop + cw_xm_release_rec + mem_free_rec_x.
- **Bytes:** ~+8..+18 .cold res; loader-8 ~-3 .cold big, ~-20 .cold mega (ESTIMATED). **ABI:** none. **Confidence:** confirmed.
- **Rows:** fcpcopy, fcpapi, fcproom, memp (soak). No row closes a window mid-paste.

### BR-12 mem_regrow's path-3 move copies the whole block with interrupts off

- **Sources:** hm-1 (bug, medium, corrected), mem-2 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L high (a grow that must move: Paint, Fractal, the clipboard, on an XT with a serial mouse), B medium (86 ms per 32KB, ~170 ms per 64KB at IF=0: every tick past the first lost, a 1200-baud mouse overruns within ~17 ms, a 2400-baud link loses bytes, an SB IRQ is late).
- **Mechanism:** mem_regrow is one pushf/cli window from entry (memory.inc:4559-4560) to its popf (4664) around mem_hifit (4616) and mem_bcopy (4640). The compactor moves the same blocks at IF=1 under [sch_lock] and cli's only for MEM_K_DRV (2973-2977). apps/fractal/fractal.asm:846-848 already plans around the window.
- **Fix:** find the destination and publish under cli, raise [sch_lock], restore IF for the copy, cli again for the record store - EXCEPT a MEM_K_DRV record, or any block an ISR writes into, which copy at IF=0 as mem_cp_walk does. Document that a holder whose ISR writes into the block quiesces it before a grow (SPEC 66.4.2's movable-claim rule).
- **Bytes:** +10..+16 .cold res. **ABI:** none. **Confidence:** corrected / confirmed.
- **Rows:** regrowshed, clipgrow, heapcheck, heapmap (soak).

### BR-13 kern_mega: a reflection on the LENT stack reads .lowbss through SS

- **Sources:** mc-1 (bug, high, corrected).
- **Kernels:** mega.
- **Rank:** S high, L medium (WIN16's Mozart int 10h AX=1A00h goes this way on every limit-checking CPU), B high (#SS in .cold code, which mega_fkill treats as the kernel's fault: the machine halts).
- **Mechanism:** mega_svc_lend loads SS = [mega_rsseg], the 1KB claim's selector with limit 3FFh (mega.inc:3463-3488, 14062-14073), and continues at mega_svc.low. mega_s2segc's first instruction is `cmp byte [ss:mega_pm],0` with mega_pm at .lowbss 1347h (5457-5458); mega_p2d reads mem_tab through SS (5425; mem_tab at .lowbss 13CCh); mega_hibnc lands the bounce at SS:dsk_secbuf (5825); mega_rseg's .nu arm reaches mega_ldt_alloc's `cmp di,[ss:mega_ldte]` (9858). QEMU checks no limit and reads a heap byte instead.
- **Fix:** read through ES = LOW_SEG by name, as mega_codesel does, on the reflection path and its .cold siblings (mega_slidec, mega_kaimf, mega_oddsegc, mega_bkaimf, mega_backf); mega_hibnc `mov ax, LOW_SEG`. Consider a fast gate listing non-bp `[ss:]` operands reachable from mega_svc.
- **Bytes:** +16 (the reflection path) to ~+40 (with the siblings), .cold/.mega above 1MB; 0 conventional. **ABI:** none. **Confidence:** corrected (a fourth read added).
- **Rows:** no row. The win16mega* rows run on QEMU, which cannot see it; add a Bochs probe that lends a 1KB stack and issues int 10h 1A00h and 1130h (docs/BOCHS.md).

### BR-14 kern_small's fsx_page enables Hercules page 1 on a machine that also has a CGA

- **Sources:** video-1 (bug, high, corrected).
- **Kernels:** kern_small (kern_big's guard is replaced by the same shared test).
- **Rank:** S high, L low-medium (a 5150 with Hercules + CGA running the 128KB kernel and a page-flipping app such as WireBall), B medium (both cards decode B8000 for the bracket; page-1 writes land in the CGA's RAM).
- **Mechanism:** the dual-card refusal in fsx_page's Hercules arm is `%ifdef KERN_BIG` (fsx.inc:1027-1061, guard at 1031-1038), and vid_dual_ok lives inside vidsel.inc's KERN_BIG block (1356-2299). kern_small does build vid_avail with both HERC and CGA bits (vidsel.inc:110-128) and offers FSXM_HERC with 2 pages (fsx.inc:172, 1175-1177); BX = 030Ah writes 3BFh = 3 on every call, page 0 included. CLAUDE.md promises the API refuses here, and WireBall relies on it (apps/wireball/wbvideo.inc:82-87).
- **Fix:** on both kernels, `test byte [vid_avail], VID_A_VGA|VID_A_CGA / jz .single` (in the .herc arm vid_kind is VID_HERC, so the colour bit is the whole question).
- **Bytes:** +13 res kern_small, +2 res kern_big. **ABI:** none. **Confidence:** corrected (byte count).
- **Rows:** wireballvideo, wireballmono, dispfsxherc (soak).

### BR-15 The system clock gains about 31 seconds a day

- **Sources:** sysmisc-1 (bug, medium, confirmed).
- **Kernels:** all; and the Timer package.
- **Rank:** S medium, L high (every machine, every day: nothing re-reads an RTC after boot, and a 5150 has none), B low (3.6 minutes a week).
- **Mechanism:** clk_tick computes seconds as delta*10/182 (clock.inc:1686-1692) while [ticks] runs at 1193182/65536 = 18.20648 Hz; each counted second is 0.99964 s. apps/timer/timer.asm:208 has the same constant. kerndos re-derives the time from 40:6C and does not accumulate it.
- **Fix:** `mov dx,625 / mov cx,11379` (11379/625 = 18.20640, about 0.4 s a day); the same encodings; overflow checked (65535*625+11378 fits, quotient at most 3600; hibernation's 32768-tick passes stay in range). Update clk_acc's comment, SPEC 37 Advancing, and the Timer.
- **Bytes:** 0. **ABI:** none. **Confidence:** confirmed (predicted, not measured on a 5150).
- **Rows:** deskclock, arclock (soak). None measures the rate.

### BR-16 Media-changed status 06h is retried as a generic error and thrown away

- **Sources:** dr-4 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (an AT-class BIOS and a fast swap inside a quiet switch whose motor test passed, or inside a batch), B high (the write goes to the new disk with the old disk's FAT).
- **Mechanism:** dsk_xfer's failure arm special-cases only 03h (disk.inc:2020-2056), so 06h is reset and retried, the retry succeeds, and the only hardware swap signal is lost; the media test is motor-only (8342-8392). The change line was declined as a REPLACEMENT for the motor test (FIELD-NOTES 63-67); this is additive and arrives on a read already being made.
- **Fix:** on 06h with [dsk_mntok] = 1, clear [dsk_mntok] (the next operation remounts and the write gate shuts) and fail with ioerr 06h, no retry; during a mount keep today's retry.
- **Bytes:** +16 .cold res. **ABI:** none. **Confidence:** confirmed (whether a given AT ROM returns 06h with CF=1 to the caller is the open question).
- **Rows:** fdswap, wseq, dosmedia, curdisk (soak).

### BR-17 A failed write on a multi-window FAT16 volume leaks its half-built chain - on Disk full, all free space

- **Sources:** dw-2 (bug, medium, confirmed).
- **Kernels:** big, small (FAT12 floppies, whole window, are immune).
- **Rank:** S medium, L medium (a hard disk filling up), B high (the volume stays "full" until a System Check repair, even after the user deletes the file they were saving).
- **Mechanism:** the plain rollback arms only call dskw_refat, an invalidate with no I/O (diskw.inc:1600-1602, 4089-4092), but the FAT16 scan and links moved the window and dsk_fat_window flushed before evicting, so the chain built so far is already on the disk (5008-5012). The HELD arm frees its own sub-chain first and names exactly this leak (4069-4087).
- **Fix:** in both plain arms `mov ax,[dskw_first] / call dskw_free_chain / call dskw_flush_x`, and refat only if that flush fails.
- **Bytes:** +12 .cold res big, +9 small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** dosfull, fsckhd, fsckdisk (soak).

### BR-18 A 16550 whose FIFO is already on is treated as an 8250

- **Sources:** input-1 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (after any DOS program, warm-booted driver or earlier OS that enabled the FIFO; 16550s are common on AT and 386 boards), B medium (at trigger 14 the pointer lags ~117 ms; at trigger 1 two queued bytes leave INTR high with no new edge and the mouse is silent for the session).
- **Mechanism:** mouse_init never writes FCR (mouse.inc:767-800, 888-896); the ISR reads one RX byte per port per interrupt (3720-3753); the SPEC 9.13 re-arm rewrites MCR and IER only (1585-1590).
- **Fix:** FCR = 0 beside the LCR writes (`mov ax,0x0200 / call mou_ov_pall`; an 8250 ignores it) and in osapi_mouse's re-arm; optionally re-read the row's LSR after mou_byte so no byte is left behind.
- **Bytes:** +6 .ovl (big) / +6 .boot2 (small), 0 res; +6 .text big for the re-arm; optional +2 .text both. **ABI:** none. **Confidence:** confirmed (not measured).
- **Rows:** serlease, dosmouse, doswmouse, kdmouse (soak).

### BR-19 A tick can park a ROM int 09h with IRQ1 still in service

- **Sources:** input-2 (hazard, medium, confirmed).
- **Kernels:** big on an AT ROM advertising int 15h 4Fh (KBD_I15), mega (its jmp far); the generic fix reaches all.
- **Rank:** S medium, L medium (AMI/Phoenix/IBM AT ROMs sti at int 09h's entry and EOI at its end), B medium-high (IRQ2-7 blocked until the parked task runs: serial mouse overruns, PS/2 stalls; if the running task is inside int 13h under sch_lock the parked task cannot run, IRQ6 is never delivered and the call returns a disk error after ~2 s).
- **Mechanism:** the I15 path is not inside the SPEC 9.8.3 IMR bracket (mouse.inc:3016-3019, 3034-3039); kbm_i15 wakes the UI task (3079-3099); sch_isr switches every tick without asking whether it interrupted an interrupt handler (sched.inc:2380-2392); disk.inc:1462 holds sch_lock across int 13h. PREDICTED, not measured: SeaBIOS does not sti in its IRQ handlers, so QEMU and Bochs cannot show it.
- **Fix:** the generic rule - sch_isr refuses a switch while any IRQ other than IRQ0 is in service: OCW3 0Bh, `test al,0FEh`, restore 0Ah (khb_* already uses the sequence, sched.inc:1947-1953). This also covers kern_mega's jump and any driver ISR that sti's. Verify on 86Box with an AMI or Phoenix AT BIOS.
- **Bytes:** +~14..+16 .text res, all kernels; ~40 cycles on the switch path only (ESTIMATED). **ABI:** none. **Confidence:** confirmed (a prediction).
- **Rows:** kbd4f, kbdpreempt, kbdburst (soak; on SeaBIOS they cannot fail for this). The box- twins on 86Box are where it can be seen.

### BR-20 After the contest the mouse ISR still drains retired and foreign UARTs

- **Sources:** input-3 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a DOS program under os8088's DOS, or a package, driving COM2 while the mouse is on COM1, without the serial lease), B medium (its bytes are read and discarded before its own IRQ3 handler sees them; at 9600 baud about one per cursor move).
- **Mechanism:** mou_isr_body scans every row with a nonzero base on every interrupt (mouse.inc:3720-3753), because mou_lockon keeps the bases for mouse_unhook (1215-1260); mou_byte drops a non-[mou_port] byte AFTER the RX read (3793-3798). LSR data-ready does not depend on IER.
- **Fix:** once [mou_seen] is set, scan only [mou_port]'s row and [ser_row] (`cmp byte [mou_seen],0 / je .take / cmp bl,[mou_port] / jne .next`). Cross-wired machines are unaffected; two-port machines save an IN per row per IRQ.
- **Bytes:** +~13 .text res, both. **ABI:** none. **Confidence:** confirmed.
- **Rows:** serlease, dosmouse (soak).

### BR-21 The cursor is drawn at IF=0 inside the serial ISR, which caps SERIAL.DRV's usable rate on an 8250

- **Sources:** isr-7 (hazard, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (the lease defaults to 9600 baud; the mouse moving while bytes arrive), B medium (an 8250 overruns once a second byte completes behind an unread one, in 1-2 byte times, against cur_move's 2.4 ms on CGA and 3.2 ms on VGA).
- **Mechanism:** mou_byte -> mou_apply -> cur_move runs inside mou_isr's port scan before the EOI, IF=0 throughout (mouse.inc:3718-3762, 4212-4300); PERFORMANCE.md:1356 measures cur_move at 11,500 cycles CGA and 15,100 VGA. SPEC 115.2's "keeps mouse movement from stealing serial data" is true of the routing, not of the timing.
- **Fix:** measure first: tests/serlease.py at divisor 12 with the mouse moving, on os8088_xt_vga and a CGA twin, counting LSR overrun bits. Then either default the lease to 2400 on an 8250 and document the ceiling, or the rethink: EOI, `inc sch_lock`, a busy byte, sti, the redraw loop, cli.
- **Bytes:** 0 to measure; +30..+40 .text res for the rethink. **ABI:** none. **Confidence:** corrected (the cycle figures).
- **Rows:** serlease, wirefleetlink (soak).

### BR-22 The UI-task-only rule for file cells (and the clipboard's) is prose

- **Sources:** dw-4 (hazard, medium, confirmed), abi-8 (weakness, medium), ui-8 (hazard, medium, confirmed); loader-5's worker half is BR-30.
- **Kernels:** big, small.
- **Rank:** S medium, L low (a package worker calling a file cell while the UI task is in one), B high (the bodies share dskw_raw, dskw_n83, dsk_secbuf, the dirty range and api_name/api_name2: a silently cross-linked FAT).
- **Mechanism:** SPEC 18.4 says "The kernel cannot enforce this" (SPEC.md:34500-34501; diskw.inc:27-31; shared staging kernel.asm:5213-5214), yet the kernel tests "am I the UI task" with `cmp byte [sch_cur],0` in several places (memory.inc:955, instance.inc:2203, toast.inc:407). The clipboard is the reverse case: the SDK and SPEC 55.4 tell authors a worker MAY call it (os88api.inc:5669-5670; SPEC.md:110111) while clip.inc:36-56 says it must not, for good reason (mem_claim, 32KB bursts between movable segments).
- **Fix:** `cmp byte [sch_cur],0 / jne refuse` (FERR_NODISK) once in the file cells' N-stub, or in dskw_mounted/dskw_gate. First show that DOS.O88's worker (dvw_start, apps/dos/dos.asm:2010), kerndos, HIBER and the drivers never reach the bodies off task 0. Correct the two clipboard texts.
- **Bytes:** +9 .text res (N-stub) or +14 .cold (two gates). **ABI:** SDK text (abilock --update, no bump); the refused call was already forbidden. **Confidence:** confirmed.
- **Rows:** no row.

### BR-23 A hostile FileSize faults two unguarded divides

- **Sources:** dw-1 (bug, medium, corrected); BR-9 is what the fault does.
- **Kernels:** big, small (both divides are outside any KERN_SMALL arm).
- **Rank:** S medium, L low (a corrupt or crafted directory entry claiming 32MB or more on a floppy, then OSAPI_FILE_APPEND, or a DOS program lseeking past 32MB in it), B high (INT 0).
- **Mechanism:** dskw_wabody's .div divides the offset by the cluster size with no high-word check (diskw.inc:3930-3932), and on APPEND the offset is the entry's size; dskw_read_at_x divides the caller's offset, bounded only by that size (3128-3134, 3168-3170). The truncate arm has the guard (3850-3851) and SPEC 18.4.7.5 states why.
- **Fix:** `cmp dx, <bytes per cluster> / jae <FERR_IO arm>` before each div (.jcor is in reach in wabody; READ_AT needs stc before .rdend); fix the "offset is under 33MB" comments.
- **Bytes:** ~+4 and +6..+8 .cold res, both kernels. **ABI:** none. **Confidence:** corrected (both kernels need both guards).
- **Rows:** no row (int0sweep detects the fault).

### BR-24 An unreadable FAT window reads as "Disk full", after a per-cluster retry storm

- **Sources:** dw-3 (bug, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a bad sector in both FAT copies, or a failing drive), B medium (the wrong error after minutes of resets).
- **Mechanism:** dskw_alloc's .one treats a CF from dsk_next_clus_x as "used" (diskw.inc:4703-4713); the rover keeps candidates in range, so that CF can only be I/O. The FAT16 .scan falls back to .one on the same failure (4749-4750). Every caller maps AX=0 CF=1 to FERR_FULL - the misreport the routine's header says it exists to prevent (4680-4684).
- **Fix:** in .one, `jc` to `pop ax / stc / jmp .iofail`.
- **Bytes:** +4..+5 .cold res, both. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-25 A hard-disk boot fetches its own BPB with the AH=08h geometry boothd stopped trusting

- **Sources:** boot-5 (hazard, medium, confirmed), dr-1 (hazard, medium, confirmed), dr-13 (bug, low), boot-6 (weakness, low, corrected; supplies the room).
- **Kernels:** big (hard-disk boot).
- **Rank:** S medium, L medium (FIELD-NOTES 33's shape: a volume written under a geometry the drive's ROM does not answer, at a nonzero base), B medium (boothd boots and C: never mounts: no SYSTEM.CFG, no HDD.DRV, on a desktop that looks as if it works).
- **Mechanism:** dsk_boot_from_x asks int 13h AH=08h into dsk_bootspt/dsk_boothds (disk.inc:2786-2808), and the one read that fetches the BPB converts bpb_hidd to CHS with them (4074-4081) before dsk_bpb_check switches to the BPB's (5327-5330). boothd deliberately uses the BPB's spt and heads (boot/boothd.asm:16-36, 174-183) and hands over only DL and BX:CX (286-288). Hibernate and dsk_hcyl_probe read the same pair (hiber.inc:1669-1672, 2782-2785; disk.inc:2949-2970). dr-13: the 255-head guard is dead, `xor ah,ah / inc ax` never gives 0 (2803-2806). The AH=08h comment still says it is "the same question boothd asks".
- **Fix:** boothd hands over SI = spt and DI = heads with a marker in AX; dsk_boot_from_x uses them when the marker matches and they are sane (spt 1..63, heads 1..255), else falls back to AH=08h so an old VBR chaining a new KERNEL.SYS still boots; drop the fallback once every boothd carries the marker. boothd has 7 bytes free, so take boot-6 first: compare MEMNEED against the top of RAM before the subtract, as stage 1 does (-9 boot sector). Write the hand-off into SPEC 52.10.3. Verify on FN33's dumped images in 86Box first.
- **Bytes:** boothd +8 and -9 (boot sector); +6 .ovl now, -28 .ovl once AH=08h goes; dr-13 -2 .ovl; 0 res. **ABI:** none (a private boot hand-off). **Confidence:** confirmed (predicted from reading, not run).
- **Rows:** hdboot, hddgeom (soak), and their box- twins on 86Box.

### BR-26 The user's clock setting never reaches 40:6C, so os8088's DOS disagrees with the menu bar

- **Sources:** sysmisc-3 (bug, medium, corrected), sysmisc-14 (rethink R1: one clock).
- **Kernels:** big, small, and kern_dos.
- **Rank:** S medium, L high (a 5150 without a clock card: DOS TIME reads time since power-on and DATE 4 July 2026; on an AT, POST time plus uptime), B low.
- **Mechanism:** clock.inc promises "one date + time of day for the whole system" (4-5); kerndos takes the time from 40:6C and the date from int 1Ah AH=04h (apps/dos/dos.asm:13878-13906, 14098-14135); cp_flush_close_x writes only the RTC (ctrl.inc:5485-5496); nothing in kernel/ writes 46Ch outside mega.inc's PM tick. WIN16 is unaffected (it asks OSAPI_CLK_GET).
- **Fix:** in cp_flush_close_x, when [clk_dirty], set the BIOS count with int 1Ah AH=01h (every PC BIOS, the 5150's included) from h:m:s and clear 40:70; kerndos asks OSAPI_CLK_GET for the date, as WIN16 does. R1 (derive the time of day from 40:6C, sysmisc-14) fixes it by construction.
- **Bytes:** +~40..+60 CTRL.DRV, 0 res; R1 is -10..+10 res (ESTIMATED). **ABI:** none. **Confidence:** corrected (the byte figure).
- **Rows:** no row asserts DOS TIME/DATE after a panel Set; kdreturnset (soak) is the closest.

### BR-27 kern_big: fm_reloc does not patch the window being launched

- **Sources:** files-2 (bug, medium, corrected).
- **Kernels:** big.
- **Rank:** S medium, L low (a compaction inside a Disk window's or the chooser's first mount), B high (the listing is copied into a freed or foreign segment; FS_VSEG stays stale for the window's life; DSK_BIGDIR regrows the stale segment).
- **Mechanism:** app_launch runs KD_INIT before publishing I_STATE = 1 (instance.inc:1600-1610); fm_kinit_x claims the cache MOVABLE and mounts (files.inc:1297-1319, 681-683), whose claims reach the compactor; fm_reloc patches only I_STATE = 1 KIND_FILES records (9898-9922), while [fm_vseg] and [dsk_dseg] move; fmv_store then sees FS_VSEG != [dsk_dseg] and copies (1216).
- **Fix:** walk the FM_NSLOT fm_pool blocks and patch any FS_VSEG equal to the moved base, whoever owns the block (fm_kinit_x zeroes FS_VSEG on reuse).
- **Bytes:** ~-12 .text res kern_big. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** fmcommit, fdlgchoose, pkgmove-* (soak).

### BR-28 Content-click, right-click and W_ONDRAG read the handler before gfx_lock and never re-test liveness

- **Sources:** ui-2 (hazard, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L low-medium (a click landing while the window's worker reaches inst_task_die), B high (a far call through a freed W_SEG; a #GP on kern_mega).
- **Mechanism:** SPEC 13.9.2 fixed the timer pass by re-testing W_FLAGS under the lock (ui.inc:823-831). .content_front reads W_ONCLICK before gfx_lock (579, 592); ui_rdown reads W_ONRC before its lock (1569, 1578); ui_arm_trk tests and reads W_ONDRAG before it (1343-1356). inst_task_die locks, unwinds, unlocks, then frees the region (instance.inc:1921-1946); wm_pkgcall has no liveness check (wm.inc:396-460).
- **Fix:** `test byte [si+W_FLAGS],1 / jz .out` at ui_bill's head (every caller holds the lock; wm_destroy writes W_FLAGS = 0); drop .tp_locked's identical test and keep its W_ONTIMER re-read. Add a clickrace row on timerrace's model.
- **Bytes:** net 0 res kern_big, +5 kern_small. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** timerrace (soak) is the model; no click row.

### BR-29 The entry proc's returned BX is not validated

- **Sources:** loader-4 (bug, medium, confirmed); the divide it reaches is BR-8.
- **Kernels:** all.
- **Rank:** S medium, L low (an entry proc returning CF=0 with a bad BX), B high (BX = 0 sends wm_ptr2idx's div to INT 0: dead machine on IBM ROMs, a hang on a 286 running kern_big; on GLaBIOS the record steals the last window's wm_owner).
- **Mechanism:** on CF=0 step 9 passes BX straight to the WF_DESK test, inst_bind_win and wm_show (loader.inc:1460, 1468-1469, 1495). SPEC 21 step 9's "sanity-checked exactly as before" (SPEC.md:50592) describes the retired abort path; inst_wchk needs an owner, which an unpublished record lacks.
- **Fix:** at .reg check BX - wm_wins < MAX_WIN*WIN_SIZE and [bx+W_SEG] == [ld_base] (the stamp .sweep already trusts), otherwise go to .abort. Check the W_SEG compare against kern_mega's selector stamps and the re-home arm's new [ld_base] first.
- **Bytes:** ~+8 .cold (W_SEG only) to ~+18 (with the range test), ESTIMATED. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-30 OSAPI_PKG_START's "no nested start" and "UI task only" are prose

- **Sources:** loader-5 (hazard, medium, confirmed).
- **Kernels:** all.
- **Rank:** S medium, L low, B medium-high (a nested start from an entry proc overwrites [ld_rec]/[ld_base]/[ld_need]/[ld_lname]; the outer launch then publishes I_SPTR = 0 on an already-published record and its region is owned by an unpublished slot; a worker caller runs the loader concurrently with the UI task).
- **Mechanism:** the image arm and the no-document by-name arm re-enter the pipeline (loader.inc:1851, 1990-1991); only the document arm tests [ld_base] and posts (2048-2049); the nested step 9 zeroes [ld_base] (1497); api_rn checks no [sch_cur] (kernel.asm:4747-4765). SPEC 21.5.4 forbids it.
- **Fix:** `cmp byte [sch_cur],0 / jne .bad` at the slot's head; `cmp word [ld_base],0 / jne .bad` on the image and plain by-name arms; the document arm keeps its post.
- **Bytes:** ~+15..+21 .cold res (ESTIMATED). **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-31 A driver can be unloaded while another task is inside it

- **Sources:** drivers-2 (hazard, medium, confirmed).
- **Kernels:** big (OS88_DRIVERS).
- **Rank:** S medium, L low (a worker pre-empted inside a socket or stream verb while the user unticks the driver), B high (DETACH and the free run under the worker's CS; a far call through segment 0).
- **Mechanism:** drv_pkg_call_x is documented worker-safe and takes no lock (driver.inc:6619-6631); stream verbs are any-task (os88api.inc:567-579); drv_svc_call_x tests [drv_fseg] and then loads it with no IF=0 window between (2537-2543). The nest frames already count other tasks' in-calls in wm_tcnt (wm.inc:11737-11771), but drv_unload_x looks only at drv_wcnt (driver.inc:4154-4192).
- **Fix:** before DETACH wait, bounded, for [wm_tcnt] == 0 and refuse with DRVE_BUSY if it does not drain. A per-image entry count is the exact form (~15 bytes a site per HEAP-UNPIN-PLAN).
- **Bytes:** +~12 .cold res (coarse form). **ABI:** none. **Confidence:** confirmed (a race, not reproduced).
- **Rows:** no row.

### BR-32 OSAPI_DRV_CLASSK reports a driver in the HMA as not loaded

- **Sources:** drivers-3 (bug, medium, confirmed).
- **Kernels:** big, emu (kern_mega already fixed this).
- **Rank:** S medium, L medium (the ROM_COLD default plus COLDHMA.SYS on any 286+ moves SOUND/HDD/NET into the HMA), B medium (INSTALL's in_todrive reads CTRL.DRV off the floppy for a driver already up; DOS's Memory page greys Sound or Hard-drive).
- **Mechanism:** %1_hma zeroes DRVR_PARA (driver.inc:3941); the non-mega slot derives "loaded" from a KB figure, `cmp ax,1` (2216), and an HMA driver with no heap claim sums to 0KB; kern_mega counts rows (2206-2214, KERN-MEGA-PLAN 14.88).
- **Fix:** the row count (`inc cx / cmp cx,1`) on every OS88_DRIVERS build; then correct SPEC 51.12.3's predicate sentences and the slot header.
- **Bytes:** +5 .cold res. **ABI:** none (the answer becomes what the contract already says). **Confidence:** confirmed.
- **Rows:** hmadrv, hma286, hmapool (soak).

### BR-33 snd_release_inst calls DSV_RELINST at IF=0 into code that makes kernel calls

- **Sources:** drivers-4 (hazard, medium, confirmed).
- **Kernels:** big (SOUND.DRV).
- **Rank:** S medium, L medium (any teardown that held a stream; fsx's bracket entry calls it per live instance), B medium (OSAPI_MEM_MOVABLE and OSAPI_MEM_FREE at IF=0; up to several ms on a slow high-speed DSP reset, past a serial mouse byte).
- **Mechanism:** snd.inc:478-497 opens pushf/cli and calls drv_svc_call(DSV_RELINST) inside; SOUND.DRV's chain reaches sbl_unpin and sbl_pool_put, whose own comment says "Outside every cli window: both are kernel calls" (drivers/sound/sb.inc:1897-1955, 1966, 2597-2622).
- **Fix:** move the OS88_DRIVERS block above the pushf/cli (drv_svc_call already refuses when nothing is published); only the speaker half needs the window.
- **Bytes:** 0 (reorder). **ABI:** none. **Confidence:** confirmed.
- **Rows:** sndkeep, sndonce, sndmove (soak).

### BR-34 Nothing owns the IRQ vectors or the IMR

- **Sources:** drivers-6 (hazard, medium, confirmed), isr-6 (hazard, low, corrected), isr-17 (consistency, low), isr-13 (rethink: one owner for IRQ lines, IVT and IMR).
- **Kernels:** big (drivers), both (IMR sites).
- **Rank:** S medium, L low, B high (a driver image freed with a vector still naming it: the next claim in that memory runs as an ISR; a task switch inside an IMR read-modify-write writes back a stale mask).
- **Mechanism:** drv_release frees the image with no check of IRQ vectors 08h-0Fh/70h-77h (driver.inc:4232-4351); the attach/detach contract is trusted (drivers/os88drv.inc:44-53); drv_hi_ivt is the 16-vector scan's existing shape (4410-4455). IMR RMWs at IF=1 on task context in mou_lockon, mou_p2_off, mouse_unhook and SOUND.DRV's sbl_hook/sbl_cell (mouse.inc:1253-1255, 2194-2196, 1417-1419; sb.inc:3211-3213, 3235-3237); DOS.O88 exchanges vectors but not the IMR. SOUND.DRV leaves the master PIC reading its ISR (sb.inc:937-942, 3294-3298), which a DOS program reading port 20h for the IRR then misreads.
- **Fix now:** after DETACH or a refused ATTACH scan the 16 vectors for the image segment; if any matches keep the image claimed (a leak, not a crash) and bank a code. Wrap the four IMR RMWs in pushf/cli ... popf. Re-select OCW3 0Ah at both SB sites. **Flag day:** an OSAPI_DRV_IRQ set/unset cell with an IMR shadow, so the kernel owns the entries, refuses a line already held and force-unhooks at release (isr-13; ABI-BREAK-LIST L3).
- **Bytes:** +~25 .cold; +6..+9 .text res (IMR, both kernels); +6 and +8 SOUND.DRV; the cell ~+60..+100 res at a flag day (isr-13 estimate). **ABI:** none now; flag day for the cell. **Confidence:** confirmed / corrected.
- **Rows:** sndkeep (soak); no row tests a vector after release.

### BR-35 MEM_OWNER_MAX caps the kernel's own shared tags

- **Sources:** mem-1 (bug, medium, confirmed), hm-4 (hazard, low, corrected).
- **Kernels:** big (kern_mega's cap is 12).
- **Rank:** S medium, L low (eight conventional driver images at once: XMEM.DRV plus seven, or no HMA), B medium (the ninth is refused as "no heap" over a free heap).
- **Mechanism:** mem_claim_1 counts every live record whose MC_OWN equals BX and refuses at 8 whatever BX is (memory.inc:57-74, 1093-1112); every driver image and every DRVC_OVL image is claimed under MEM_K_DRV (driver.inc:3513 and five more sites); DRV_MAX = 9 + DRV_VX + DRV_PX (80). Modules are MEM_K_MOD, a separate eight.
- **Fix:** skip the cap when BH = 0xFF. Add a gate that loads every driver on an AT profile, or an assembly-time check that the kernel tags' maxima fit MEM_MAX.
- **Bytes:** +5 .cold res. **ABI:** none. **Confidence:** confirmed / corrected.
- **Rows:** drvmem (fast), heapdrv (soak).

### BR-36 osapi_mouse_feed is reached from an IRQ handler but swaps SP and not SS

- **Sources:** isr-2 (hazard, medium, corrected).
- **Kernels:** big (OS88_DRIVERS: USBMOUSE.DRV's PicoMEM path).
- **Rank:** S medium, L medium (the card's IRQ landing on a ROM service's, an option ROM's or a DOS program's stack), B high (pushes at foreignSS:mou_pstack; evq_push and the cursor row table write through SS into that segment: a lost event, a garbage arrow, corrupted foreign memory).
- **Mechanism:** the feed swaps SP onto mou_pstack only, inside its cli (mouse.inc:4016-4041), while its caller um_pm33 runs inside the card's IRQ handler (drivers/usbmouse/usbmouse.asm:417-426); the body reaches .lowbss through SS (395-422) and the ring is addressed through SS (events.inc:100-128). The header's "not an ISR" and "MOUPRIV_ENTER tests SS first" are stale.
- **Fix:** bank and load SS as MOUPRIV_ENTER does (`mov [mou_pss],ss / mov ss,[cs:mou_lowss]`, restored on exit) inside the existing cli; rewrite the header.
- **Bytes:** ~+13 .text res. **ABI:** none. **Confidence:** corrected (bytes).
- **Rows:** usbmouse (soak).

### BR-37 The batch bracket's "UI is frozen" premise is not enforced at the door packages can call

- **Sources:** dr-2 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L low (no package in the tree calls it), B high (a floppy's banked BPB, signature, FAT window and cache are trusted with no LBA 0 read; a package-opened batch lasts until any gfx_unlock, so a swapped floppy is written with the old FAT).
- **Mechanism:** dsk_batch_begin_x sets the batch unconditionally (disk.inc:940-953; its argument 913-919). It is published as cell 02B0h with a C thunk (kernel.asm:3217-3225; apps/cc/os88thunk.asm:2054); package file I/O holds no mutex (fprog.inc:633-646); vga12.inc:5191 is the one ending store.
- **Fix:** begin does nothing unless [gfx_lock_flag] is set and [gfx_lock_own] == [sch_cur] (fpg_arm's test, fprog.inc:616-620); a refused begin is only slower. Check drivers/hdd/inst.inc's two begins still pass.
- **Bytes:** ~+16 .cold res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-38 kern_big's DOS-handoff refusals leak the extent claim and strand the user in DOS.O88's folder

- **Sources:** hiber-2 (bug, medium, confirmed).
- **Kernels:** big, emu (HIBER.DRV).
- **Rank:** S medium, L medium (.b4: DOS.O88 on a volume int 13h cannot reach), B medium (a MEM_K_HIB claim of up to 8KB leaked; the user left in another folder).
- **Mechanism:** HBM_BADV is .badv only on kern_mega; elsewhere .b2-.b6 go to .bad, which neither frees [hb_xseg] nor calls hbm_vbk (hiber.inc:3395-3399, 3454-3469, 2876, 2923-2928).
- **Fix:** assemble .badv on every kernel (nothing in it is mega-specific) and make HBM_BADV .badv unconditionally.
- **Bytes:** +20 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernate, hibernatem (soak).

### BR-39 One static volume bank, and it nests

- **Sources:** hiber-3 (bug, medium, confirmed), drivers-7 (weakness, low).
- **Kernels:** big, mega.
- **Rank:** S medium, L medium (a refused hibernate - serial leased, write error, HMA copy refused - after any hardware driver was swept), B low-medium (the user's folder becomes the root of [hb_vol]).
- **Mechanism:** hbm_hibfail calls hbm_reload and then hbm_vbk (hiber.inc:1982-1994; kern_mega's .b8s 3445-3449); hbm_reload's drv_load_at banks into the same single pair (driver.inc:3446; drvvol.inc:100-113, 135-182), which deskmod.inc:79-100 already saves by hand around exactly this nesting.
- **Fix:** hbm_vbk before hbm_reload in both places (0 bytes). Structurally, the bank pushes the pair onto the caller's stack and the back pops it (the kentc idiom), and deskmod's workaround goes.
- **Bytes:** 0; structural ~0 res and ~-14 in deskmod (unpriced). **ABI:** none. **Confidence:** confirmed.
- **Rows:** hibernatedrv (soak).

### BR-40 EMS pages held across a hibernate are lost and nothing refuses

- **Sources:** hiber-4 (bug, medium, confirmed).
- **Kernels:** big (HIBER.DRV).
- **Rank:** S medium, L medium (the browser, Video, VBattle and Redline hold EMS pages), B medium-high (the resumed session's pages name nothing).
- **Mechanism:** hbm_sweep keeps DRVC_EMS (right for a suspend; hiber.inc:2233-2236), SPEC 107.6 already says a holder across a hibernate is unsupported, but hb_ok greys only on XMS blocks (605-609) and hbm_arm checks room and transport only (1712-1782): SPEC 87.7's "refused rather than guessed at" is broken.
- **Fix:** in hbm_arm ask DRVC_EMS's EMSV_CAPS and refuse with a toast ("EMS pages in use") when any page is allocated.
- **Bytes:** +30 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** ems, hibernate (soak).

### BR-41 The Save box commits names dskw_name83 refuses

- **Sources:** files-3 (bug, medium, confirmed).
- **Kernels:** big (.cold), small (FDLG.DRV).
- **Rank:** S medium, L high (anyone typing a nine-letter name), B low-medium (the application's write fails FERR_NAME after the dialog has gone).
- **Mechanism:** fm_editkey bounds the buffer at 12 and filters characters but does not enforce the 8-character stem or 3-character extension and allows a trailing dot (files.inc:6686-6729); fdlg_save checks only FS_MOK and a non-empty box (fdlg.inc:968-980, 1008-1010). SPEC 38.5 claims the box accepts exactly what dskw_name83 stores (SPEC.md:75569).
- **Fix:** in fdlg_save after the empty test, `mov si, fdlg_name / FDX dskw_name83n / jc fdlg_beep`; optionally enforce 8/3 per keystroke in fm_editkey for every naming prompt (+~14).
- **Bytes:** +8 .cold kern_big; +8 FDLG.DRV (0 res) kern_small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** fdlgchoose, fdlgup, fdlgsmall (soak).

### BR-42 The Sticky App library can be freed under a widget that has bound it

- **Sources:** shell-1 (hazard, medium, confirmed).
- **Kernels:** big (DESK.DRV).
- **Rank:** S medium, L low (any MAKE refusal; every precondition holds for a correct package today), B high (the next `call far [stik_le+...]` jumps into a freed claim).
- **Mechanism:** DESK.DRV is dropped when no record carries IF_DESK (deskmod.inc:118-134), and step 9 calls desk_drop_x for any launch whose window is not WF_DESK (loader.inc:1458-1466). A STIK_LIB widget has already bound the library into stik_le (apps/os88stikc.inc:152-176), and under STIK_DESKONLY stik_made ignores MAKE's CF (apps/os88stik.inc:962-966). The drop rule uses "has a desk window" as a proxy for "holds pointers into the library"; the banner and SPEC 110.15 still say "never freed under a caller".
- **Fix:** cheapest, the library's stik_made refuses the launch when MAKE refuses. Right model: the LIB verb sets an IF_LIB bit (I_FLAGS bit 3 is free), desk_drop_x tests IF_DESK|IF_LIB, and only the record's free clears it (AmigaOS OpenLibrary/CloseLibrary).
- **Bytes:** (a) +4..+6 DESK.DRV; (b) 0 res (an immediate changes), +6 DESK.DRV. **ABI:** none. **Confidence:** confirmed.
- **Rows:** stik, stikcard (fast); stikmenu, stikboot, stikswap (soak).

### BR-43 OSAPI_RAND's low bits repeat with a short period, and six games read them

- **Sources:** abi-1 (bug, medium, confirmed).
- **Kernels:** all.
- **Rank:** S medium, L high (Tank, DotDel, Tamegram, WireBall and Pixelstein mask the low bits on every draw), B low (Tank's `test al,1` coin strictly alternates; Tamegram's spawns walk a period-8 pattern; one package's SRAND re-seeds every other package's stream).
- **Mechanism:** osapi_rand returns seed = seed*25173 + 13849 mod 2^16 whole (kernel.asm:6599-6607); 25173 is 1 mod 4 and the increment odd, so the low k bits have period 2^k (apps/tank/tkgame.inc:1271, 1275; apps/dotdel/ddgame.inc:1002, 1014, 1177; apps/tamegram/tamegram.asm:1200-1215). The seed is one system-wide word and the SDK says neither thing (os88api.inc:348-349).
- **Fix:** `xchg ah, al` after the store (AL carries bits 8-15; the stored seed is unchanged); update SPEC 20.4's "out AX = seed"; document the shared seed and that a reproducible stream carries its own ~14-byte LCG; re-baseline any test asserting a seeded sequence.
- **Bytes:** +2 .text res, all kernels. **ABI:** SDK text (abilock --update, no bump); outputs change value, not shape. **Confidence:** confirmed.
- **Rows:** no row (tankspawn, soak, may baseline a sequence).

### BR-44 A hostile SYSTEM.CFG 'CH' byte lets the Date/Time page step a .bss byte 254 past clk_sec

- **Sources:** sysmisc-2 (bug, medium, corrected).
- **Kernels:** big (kern_small reads no SYSTEM.CFG).
- **Rank:** S medium, L low (a crafted or damaged SYSTEM.CFG, then a click on the page), B high (kernel .bss corruption).
- **Mechanism:** the unpack copies mapped bytes raw (driver.inc:4964-4972, 906-907); the page uses clk_h12 as a COUNT (ctrl.inc:4859-4866, 4984-4987), draws and hit-tests an eighth field, and clk_fld_adj(7) steps [clk_sec+0FEh] (5195-5219). osapi_clk_get_x already masks bit 0 for this reason (clock.inc:2343-2344).
- **Fix:** `and byte [clk_h12],1` after the map loop (CFG_BOOT is instantiated for the overlay only); optionally a per-key mask in CFG_MAP for booleans.
- **Bytes:** +5 .ovl, 0 res, 0 CTRL.DRV. **ABI:** none. **Confidence:** corrected (one site, not two).
- **Rows:** cfgtrip (soak).

### BR-45 About on a VESA desktop indexes app_about_vid past its end and letters from a garbage pointer

- **Sources:** ui-1 (bug, medium, confirmed).
- **Kernels:** big, emu, mega (VID_EXT).
- **Rank:** S medium, L medium (any VESA.DRV desktop, then About), B low (garbage on the fourth line).
- **Mechanism:** app_about_vid has 3 rows, 4 with GFX_VGA (apps.inc:57-66), indexed by [vid_kind] with no bound (113-118); VID_VESA = 4 (viddet.inc:110, 118-119) reads 'VG' = 4756h from app_about_v0. SPEC 39.29.4 names this table (SPEC.md:82191); cp_vidnam and fsx_capstab were fixed, this was not.
- **Fix:** a %ifdef VID_EXT row with a geometry-free name ("VESA - 256 colors", since a VESA mode's geometry varies) and a build-time bound, `%if ($ - app_about_vid)/2 != VID_KIND_MAX + 1 / %error`. Or ui-10: compose the line.
- **Bytes:** +18 .text res on big/emu/mega, 0 small. **ABI:** none. **Confidence:** confirmed.
- **Rows:** vesa, vesacp (soak).

### BR-46 fpg_bare banks [wm_clip_n] but not [gfx_hole]

- **Sources:** text-1 (hazard, medium, confirmed).
- **Kernels:** big (DOCK_OPT).
- **Rank:** S medium, L low (a handler that draws through a region after file I/O in the same hold while the hidden dock's strip is open), B medium (the caller's next primitive clips against the hole's table and can paint over windows above it).
- **Mechanism:** fpg_bare zeroes and restores the clip count only (fprog.inc:161-170); with the count at 0, CLIPQ -> gfx_clip_query tail-jumps to gfx_hole_arm, which re-seeds wm_clip_tab (vga12.inc:454-470; dockmod.inc:1542-1562). Every other bank site saves both words (menu.inc:1929-1935, 2038-2039; dockmod.inc:528-534); toast_now also goes through fpg_bare (toast.inc:412).
- **Fix:** under %ifdef DOCK_OPT, `push word [gfx_hole] / and word [gfx_hole],0` before the painter and `pop word [gfx_hole]` after, inside fpg_bare.
- **Bytes:** +13 .text res kern_big. **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** toastfpg, dockmodule (soak).

### BR-47 wm_paint_dmg's empty-rect arm leaks its one-shot arguments into the next pass

- **Sources:** wm-3 (bug, medium, corrected), wm-17 (consistency: one-shot globals with many lifetimes).
- **Kernels:** big, small.
- **Rank:** S medium, L low (all four arming callers pass on-screen rects; reachable after a display removal), B medium (a leaked npro suppresses the next real promotion; a leaked stwin excuses every overlapping window from the next unrelated pass: a missed repaint).
- **Mechanism:** the .chrome arm, taken when the clamped rect is empty, returns before .promote and before wm_dmg_wins' .wdone, the only consumers of wm_dmg_npro and wm_dmg_stwin (wm.inc:5285, 5307-5313, 5468-5481); callers arm them first (wm.inc:3735; ui.inc:2025; desk.inc:980-986; deskmod.inc:330).
- **Fix:** clear both on .chrome (`xor ax,ax / mov [wm_dmg_stwin],ax / mov [wm_dmg_npro],al`), or a flags argument cleared in one place (wm-17).
- **Bytes:** +8 .text res. **ABI:** none. **Confidence:** corrected (dk and su_son are not leaked).
- **Rows:** dmgcull, zonedmg (soak).

### BR-48 wm_ask_size trusts a W_ONSIZE negotiator to keep BX

- **Sources:** wm-4 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L low (the one shipped negotiator, Paint's pt_onsize, keeps BX by choice), B medium (callers write [bx+W_W] through whatever BX came back).
- **Mechanism:** wm_ask_size banks AX, SI and BP only (wm.inc:2465-2481) and its header promises every other register; wm_pkgcall restores only DS/ES; ui_grow and wm_zoom use BX straight after (ui.inc:2243-2245; wm.inc:2934, 2977). SPEC.md:26382 lets a callback clobber BX.
- **Fix:** push/pop bx in wm_ask_size; say in the SDK that a negotiator answers in CX/DX and may clobber what any callback may.
- **Bytes:** +2 .text res. **ABI:** SDK text (no bump). **Confidence:** confirmed.
- **Rows:** paintnogrow, paintnogrow-vga (soak).

### BR-49 wm_snap_ax moves a window at a left dock's band edge onto the strip

- **Sources:** wm-5 (bug, medium, confirmed).
- **Kernels:** big (DOCK_OPT).
- **Rank:** S medium, L medium (anyone with a left dock: wm_fit, ui_drag drops and wm_zoom all snap through it), B low (the border overlaps the live strip, so every later show, raise or dock update pays wm_dock_under's window repaint).
- **Mechanism:** dkx_band sets vid_band_x0 = 32 (dockmod.inc:409-433); wm_snap_ax rounds the CONTENT origin down, 33 -> 32, frame x = 31, the strip's rule column, and special-cases only a negative result (wm.inc:4102-4136). PLAUSIBLE: argued from geometry.
- **Fix:** in the DOCK_OPT arm compare the rounded x with vid_band_x0 instead of 0 and take the next aligned x up when below.
- **Bytes:** +6 .text res kern_big. **ABI:** none. **Confidence:** confirmed.
- **Rows:** dockpos, dockmark (soak).

### BR-50 wm_resize clamps against the union width and ignores the side-dock band

- **Sources:** wm-6 (consistency, medium, confirmed).
- **Kernels:** big.
- **Rank:** S medium, L low (OSAPI_WM_RESIZE from an app; none shown to do it), B low (a primary window straddling the seam of an extended desktop, or over a side strip).
- **Mechanism:** wm_resize_nb clamps width and x against [vid_w], the union on an extended desktop (wm.inc:2508-2512, 2531-2545; viddet.inc:299-307), and never reads vid_band_x0/xe, which wm_fit and wm_zoom use (583-589, 2898-2905).
- **Fix:** clamp with the band words on DOCK_OPT; decide whether the union is ever intended on two displays.
- **Bytes:** +10 .text res kern_big. **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** dockpos (soak).

### BR-51 task_spawn holds IF=0 across the whole 0xCC slice fill

- **Sources:** sched-1 (hazard, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L high (every launch and every SB stream open spawns), B low (~676 us at IF=0 for a 384-byte slice: about four lost 5.5 kHz speaker pulses, two-thirds of a 9600-baud byte).
- **Mechanism:** the cli at the head (sched.inc:914-915) covers the rep stosw of the slice (983-989); the header and SPEC 8 still price the window as "~30 stores with no I/O" (882-883; SPEC.md:12446).
- **Fix:** reserve the slot under the cli with T_STATE = 3 (readers outside sched.inc compare with 0 or 2, so 3 reads as occupied and not sleeping), restore the caller's IF to fill and build the frame, cli again to raise sch_top and publish. Check the Task Manager's SS_TSTATE snapshot first. Fix the two "~30 stores" sentences regardless.
- **Bytes:** +8 .text res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row (stkclass, fast, checks sizes only).

### BR-52 Blocks an ISR uses move at IF=1 unless they are driver images

- **Sources:** mem-7 (hazard, medium, confirmed).
- **Kernels:** big (OS88_COMPACT).
- **Rank:** S medium, L low (held today by coincidence: DOS's int 08h hook appears to live only inside a pinning bracket), B high (a tick mid-copy runs the old copy, and its data writes are lost once the vector is patched).
- **Mechanism:** SPEC 66.3 bounds ISRs by an MC_DMA pin that no longer exists (memory.inc:1987-2000); only MEM_K_DRV moves at IF=0 (2974-2976); mem_region_reloc's IVT rows patch vectors naming ANY moved block (3474-3483); DOS hooks int 08h with its CS (apps/dos/dos.asm:11445-11446) and its region is movable (docs/HEAP-CLAIMS.md:329).
- **Fix:** move at IF=0 whenever the block's base appears in any of mem_rr_ivt's 17 segment words (subsumes the driver-image test); state an ISR author rule in SPEC 66.3.
- **Bytes:** +12 .cold res. **ABI:** none. **Confidence:** confirmed (latent).
- **Rows:** pkgmove-* (soak), dosmem (soak).

### BR-53 A doomed claim sheds every cache first, and a table-full refusal compacts before it sheds

- **Sources:** mem-3 (weakness, medium, confirmed), hm-8 (perf, low, confirmed).
- **Kernels:** big.
- **Rank:** S medium, L medium (compress.inc's cmz_wclaim probes largest-first and halves on refusal, so the first refusal has already shed everything), B medium (MEM_P_DIRW and the FAT windows thrown away - seconds of re-reads on a floppy; workers parked once per shed).
- **Mechanism:** mem_claim_x loops claim -> compact -> shed one -> retry until nothing is left to shed (memory.inc:952-989), with no total-size precheck; mem_compact clears [mem_parked] at its end so each round may park again (4095-4106, 4174). A full table refuses at .fail0 (1103-1105), and mem_compact, which can never create a record, runs before the shed that would free one.
- **Fix:** before the first compaction compare the request with mem_avail_lvl_p's exact TOTAL at the claimant's rank (never a false refusal); cmz_wclaim sizes its first try from mem_avail; one park per mem_claim_x; report "no record" distinctly (DX = 1) and go straight to mem_shed_one.
- **Bytes:** +14 .cold (precheck), +6 (one park), +6 (no-record path). **ABI:** none. **Confidence:** confirmed.
- **Rows:** regrowshed, msegnomem, lzcomp (soak).

### BR-54 O(MEM_MAX^2) planning walks run with interrupts off

- **Sources:** mem-8 (perf, medium, confirmed), hm-5 (perf, low, confirmed).
- **Kernels:** big (the plan is the compaction model).
- **Rank:** S medium, L medium (packages size themselves with OSAPI_MEM_AVAIL; the Task Manager polls it), B medium (predicted ~8-20 ms at IF=0 per call on a busy table, against a 7.5 ms serial byte).
- **Mechanism:** mem_avail_lvl_p runs the whole model inside pushf/cli (memory.inc:4927-5023), as does osapi_mem_compact_x's what-if (5753-5772); mem_cp_next is a full table scan per step (2680-2720) and mem_can_move far-calls per record (1975-2148). Nothing at interrupt level touches mem_tab. The figures are arithmetic, not measured.
- **Fix:** measure first (MartyPC cycles across one OSAPI_MEM_AVAIL on a 20-claim heap); if over ~5 ms, guard both walks with [sch_lock] instead of cli; keep cli for mem_claim_1's short scan-and-publish. mem-14 removes the square.
- **Bytes:** +0..+6 .cold res. **ABI:** none. **Confidence:** confirmed (predicted).
- **Rows:** memp, heapmap (soak).

### BR-55 PS/2 MCGA is classed VID_VGA

- **Sources:** video-3 (bug, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (PS/2 Models 25 and 30 are 8086 machines in the target class), B medium (no mode 12h, no GC or Sequencer: the planar renderer draws garbage).
- **Mechanism:** vid_detect tests only AL == 1Ah after int 10h AX=1A00h (viddet.inc:554-557); MCGA answers BL = 0Ah..0Ch, and the header and SPEC 39.1 even name it.
- **Fix:** after `cmp al,1Ah`, send BL 0Ah..0Ch to .equip (it comes up VID_CGA in mode 6, which MCGA emulates); every other BL stays on .vga as today. NOT the reviewer's wider diversion, which would send a VGA+Hercules machine (BL=01h) to VID_HERC.
- **Bytes:** +8..+12 .boot2, 0 res. **ABI:** none. **Confidence:** corrected (the diversion narrowed).
- **Rows:** no row.

### BR-56 A 64KB EGA is driven planar although int 10h AH=12h already says 64KB

- **Sources:** video-4 (weakness, medium, confirmed).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (base EGA cards shipped with 64KB), B medium (16 colours collapse toward black; mode 6 would be perfect).
- **Mechanism:** after `cmp bl,10h / je .equip` the code tests only BH (viddet.inc:558-562); BL = 0 means 64KB. SPEC 39.1's third scope cut accepts the collapse without a reason against the test (SPEC.md:76083).
- **Fix:** `or bl,bl / jz .equip`; delete the scope cut.
- **Bytes:** +4 .boot2, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row (the EGA game rows run 128KB+ cards).

### BR-57 fsx_page on Hercules writes the page bit and then waits, so the flip frame tears

- **Sources:** video-5 (bug, medium, confirmed; PLAUSIBLE).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (every WireBall flip on Hercules), B low (one torn frame per flip).
- **Mechanism:** Mode X's start address latches at frame start, so write-then-wait is right there; Hercules 3B8h bit 7 is the fetch's address MSB and acts immediately. fsx_page writes 3BFh/3B8h and then tail-jumps into the retrace wait (fsx.inc:1054-1061); the header, SPEC 53.10 and the SDK say it "is latched at the next vertical retrace".
- **Fix:** on the Hercules arm run the two retrace phases first, then the two outs; the "on glass when it returns" contract still holds. Verify with m.flicker() across a WireBall flip, or on an 86Box HGC, before changing the "latched" wording.
- **Bytes:** +8 res, both. **ABI:** none. **Confidence:** confirmed (the hardware half PLAUSIBLE).
- **Rows:** wireballvideo, dispfsxherc (soak).

### BR-58 The planar blank (AC 12h) is used for VID_VESA

- **Sources:** video-6 (hazard, medium, confirmed; PLAUSIBLE).
- **Kernels:** big (VID_EXT).
- **Rank:** S medium, L medium, B low (the idle blanker and vid_blank may do nothing at 8bpp on many SVGAs and at 16bpp behind a HiColor DAC; QEMU applies AR 12h only in its 4/2bpp line drawers).
- **Mechanism:** the dec/cmp map makes kind 4 planar to vid_bk (vidsel.inc:949-954, 1254-1279); SR01 bit 5 (Screen Off), which works on every VGA-class card, was retired as "an identical result" (1325-1331), true only in 16-colour modes; SPEC 120.5's row was never measured.
- **Fix:** SR01 bit 5 by read-modify-write for kinds VGA and VESA; AC 12h for the EGA alone (its SR01 is write-only with no Screen Off bit). Alternatively a VX_BLANK driver selector (VBE 4F10h). Test under QEMU at 8 and 16bpp first.
- **Bytes:** +12..+14 res kern_big (or +6 res plus driver bytes). **ABI:** none. **Confidence:** confirmed (PLAUSIBLE).
- **Rows:** vesa, saver, savervga (soak).

### BR-59 A lazy part is re-fetched by 8.3 name with no check that it is still the file launched

- **Sources:** hm-7 (hazard, medium, confirmed).
- **Kernels:** package side (apps/os88partsbody.inc), every kernel.
- **Rank:** S medium, L low (a same-named file of a different build: an update through The Wire, FTPD or a host copy while the package runs; most swaps give a not-found refusal instead), B high (a code part from the wrong build is far-called).
- **Mechanism:** op_fetch reads through op_read with op_name (os88partsbody.inc:789-794) and checks only that enough bytes arrived (829-831); the kernel loader guards the same case by re-checking the header (loader.inc:1091-1093).
- **Fix:** op_load banks the file's 32-bit size and first cluster; op_fetch compares them with one stat before trusting the read, refusing with a "Disk changed" toast.
- **Bytes:** +20 per parts package image, 0 kernel. **ABI:** none (shared source; every parts package rebuilds). **Confidence:** confirmed.
- **Rows:** no row.

### BR-60 Subdirectories never grow

- **Sources:** dw-12 (weakness, medium, corrected).
- **Kernels:** big (optional on small).
- **Rank:** S medium, L high (a kernel-made folder holds 14 entries on 512-byte clusters - 1.44MB, 1.2MB, the 360KB system disk - and 30 on 1KB clusters), B medium (FERR_DIRFULL; RunCPM's save failed on the glass, SPEC.md:133703-133709).
- **Mechanism:** when a subdirectory's chain ends with no free slot every create answers FERR_DIRFULL (diskw.inc:692-693, 1548-1551); SPEC 19.3.2 records the limit (SPEC.md:43108-43111). mkdir already allocates and zeroes a directory cluster (715-728), and both slot users go through dskw_find's no-slot exit.
- **Fix:** dskw_dirgrow at that exit for a subdirectory: allocate, zero (mkdir's loop factored out), flush, link the walker's last cluster, flush, publish its first sector as [dskw_fsec] with [dskw_fend] = 1. A crash after the link leaves a harmless empty cluster.
- **Bytes:** +90 .cold res kern_big. **ABI:** none. **Confidence:** corrected (cluster sizes). This is the largest single byte cost in the register: an owner's call on whether the defect is worth 90 resident bytes.
- **Rows:** no row.

### BR-61 A failing read-ahead fill runs the full retry ladder per good sector, and a timeout gets the single-sector fallback too

- **Sources:** dr-3 (weakness, medium, confirmed), dr-12 (weakness, low).
- **Kernels:** big, small.
- **Rank:** S medium, L medium (a marginal sector at a track's tail, or no disk in the drive), B low-medium (each ladder is seconds on a 5150; every later read on that track refills and pays again).
- **Mechanism:** a miss fills to the track's end through disk_read_x with three attempts and the single-sector fallback (disk.inc:7206-7208, 2025-2073), and after each sector succeeds the next pass recomputes a full run and fails three times again; a failed fill caches nothing, so the direct read and every later read repeat it (7023-7029). A timeout (80h) also takes the single-sector fallback (2057-2066), which cannot help.
- **Fix:** mark a fill distinctly (add/sub 2 on dsk_rah_busy; 4202-4204's LBA 0 swap read keeps its ladder) and fail it at its first error with no reset; for 80h, one attempt at the run and one at a single sector.
- **Bytes:** +10 .cold and +6 .cold res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** no row.

### BR-62 kern_small's refusal stubs break their register contracts, and the ABI gate never decodes any kernel but kern_big

- **Sources:** abi-2 (bug, medium, confirmed), dw-8 (bug, low, confirmed), abi-3 (hazard, medium, confirmed).
- **Kernels:** small (stubs); host gate for small/emu/mega.
- **Rank:** S medium, L low (no caller reads AX after FS_OP today; DOS.O88, WRITE_AT's one caller, is not on the small disks), B low (a refusal reads as FERR_OK).
- **Mechanism:** on kern_small cell 04EFh is OSAPI_XREFUSE (mov ax,1 / cmp al,2 / retf; kernel.asm:1303-1307, 4430-4439) while the SDK ends "Every register kept" (os88api.inc:2543-2565). dwf_dskw_write_at's label sits on the stc below `mov ax, FERR_NAME`, so CF=1 returns the caller's offset (diskw.inc:6805-6811). t_api_abi opens only build/kernel.bin (tests/unit/t_api_abi.py:458), its FIXED ledger has no rows for kern_small's two XREFUSE cells, and thirteen per-arm cells (kernel.asm:2820..4440) are held only by a total-length assert.
- **Fix:** SDK says "kern_small: CF=1, AX=1 (OSAPI_XREFUSE)", copying VOL_CHECK's wording; move dwf_dskw_write_at above `mov ax, FERR_NAME`; give t_api_abi a --kernel mode for smallk, emuk and megak with per-kernel FIXED rows and a cross-kernel comparison of cell start offsets from the four maps, registered where those kernels are built (full tier).
- **Bytes:** 0. **ABI:** SDK text (abilock --update, no bump). **Confidence:** confirmed.
- **Rows:** api-abi (fast), abicompat (soak).

### BR-63 A canary reload resets the splash's .text state while the blob thinks the splash is still up

- **Sources:** boot-3 (bug, medium, corrected).
- **Kernels:** big, small.
- **Rank:** S medium, L low (only when the canary reloads - exactly the machines BR-1 is about), B low (the loading screen is gone for the rest of the boot; vid_init's mode set wipes the screen).
- **Mechanism:** .rerun reloads from .text sector 0 (boot2.asm:525-529), re-landing spl_live, spl_busy, spl_quiet (viddet.inc:1753-1796) and the vid_* block with image defaults, while [cs:spl_on] in the blob stays 1, so spl_tick skips its first-tick branch for good (splash.inc:241-244, 346-348, 1180-1190).
- **Fix:** test the kernel byte for the first-tick branch (`cmp byte [spl_live],0 / jne .running`), keeping `mov byte [cs:spl_on],1`.
- **Bytes:** -1 .boot2. **ABI:** none. **Confidence:** corrected (a secondary claim refuted and removed).
- **Rows:** splashbar, splashspin (soak).

### BR-64 kern_mega: the planar decoder's deliberate over-read can #GP

- **Sources:** gfx-2 (hazard, medium, confirmed; PLAUSIBLE).
- **Kernels:** mega.
- **Rank:** S medium, L medium (a canvas's last row, or a first row at offset < 3 with odd-phase x), B high (#GP inside the decoder).
- **Mechanism:** vga_blit_prow starts p>>1 bytes early and reads whole 4-byte groups, touching up to 3 bytes either side of a row (vga12.inc:2475-2477, 2574-2576, 2612-2632); SPEC 5.4.1.3 calls that safe because the bytes are "inside the caller's segment" (SPEC.md:4573-4574), which holds in real mode only; a claim's selector limit is the claim's own end (mega.inc:5966-5970; SPEC.md:99294-99296).
- **Fix:** under %ifdef KERN_MEGA one lsl of ES where the planar path is armed (vga12.inc:2046-2068), leaving [gfx_blit_pok] = 0 when SI-3 underflows or the last row's end+3 passes the limit; state the 3-byte slack in OSAPI_GFX_BLIT4's contract either way.
- **Bytes:** +~20 .text kern_mega only (conventional resident, i.e. MEGA_RESIDENT, unless placed in .mega); 0 big/small. **ABI:** SDK text. **Confidence:** confirmed (not run).
- **Rows:** no row (a Bochs row with a 3-byte-slack canvas).

### BR-65 kern_mega: an option-ROM IRQ on an unlent foreign stack is reflected with a selector as its real-mode SS

- **Sources:** mc-2 (hazard, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L low-medium (a package keeping its own stack without SEG_LEND; WIN16 escapes only because it re-lends before every run), B high (the ROM ISR pushes its frame at selector x 16, inside somebody's heap).
- **Mechanism:** mega_svc .none sends a ROM-owned IRQ arriving with SS neither LOW_SEG nor the lent stack to .nest/.low (mega.inc:3290-3296); [mega_rss] becomes the selector (3448-3456) and mega_svc_lo loads it as a segment (3575-3577). SPEC 124.31.2 item 3's "goes on as before" describes the bug SEG_LEND was invented for.
- **Fix:** (a) reflect such IRQs on a small fixed kernel stack in .lowbss; (b) mask the line and replay it at the next LOW_SEG reflection; or at least (c) make SEG_LEND mandatory for any package that keeps its own stack, in SPEC 50.6.11.
- **Bytes:** (a) +20 .mega, +128..+192 .lowbss conventional resident - the owner's call against MEGA_RESIDENT; (c) 0. **ABI:** (c) SDK text. **Confidence:** confirmed.
- **Rows:** no row.

### BR-66 kern_mega: linear LDT walks, and 64KB compaction pieces, with interrupts off

- **Sources:** mc-4 (hazard, medium, confirmed), mc-5 (hazard, medium, corrected; record).
- **Kernels:** mega.
- **Rank:** S medium, L medium (a teardown of a program with N blocks on a 15MB machine; a pool compaction), B medium (predicted ~5 ms per walk on an 8 MHz 286, N walks back to back at IF=0; ~16.4 ms per 64KB piece, two serial bytes at 1200 baud; mx_alloc holds sch_lock across a full pack, ~0.5 s per 2MB).
- **Mechanism:** mega_ldt_alloc/_alloc2 scan FREE0..mega_ldte under cli (mega.inc:9846-9930); mx_dfree walks the whole LDT per block freed, inside mx_free's and mx_relall's cli (9251-9335, 9413-9478); ldtn = FREE0 + 512 + extKB/16 (10903-10910). mx_cpmove copies and rebases each 64KB piece in one cli step (11055-11190, 11556-11587); mx_alloc raises sch_lock around the pack (8957-9100).
- **Fix:** a high-water mark [mega_ldthi] ends every walk; mx_dfree out of the outer cli (mega_ldt_free brackets its own write); a rotating start hint; later a free list through free descriptors (the DPMI shape). The compaction half is a model change: a per-claim ISR-reachable flag so every other block moves with IF on and cuts at descriptor boundaries - a package contract, so a flag day.
- **Bytes:** +20..+30 .mega (above 1MB); +20 .mega for the compaction model at the flag day. **ABI:** none now; flag day for the ISR-reachable flag. **Confidence:** confirmed / corrected (the 16 ms is already in KERN-MEGA-PLAN 14.48.3; the sch_lock hold is new; all figures predicted).
- **Rows:** megahigh (full), megahibfull (soak).

### BR-67 kern_mega: global GDT scratch selectors are held by prose, and the compactor rebases SLD but not BACK

- **Sources:** mc-9 (hazard, medium, corrected).
- **Kernels:** mega.
- **Rank:** S medium, L low (another task's mx_alloc packs a block while an LZ decode is suspended mid-match), B high (lz_match reads through BACK's stale base).
- **Mechanism:** ODD, SLD, BACK, K0-K5, FD, VB, XA/XB and WIN are one descriptor each for the machine, re-aimed per use and correct by comment (mega.inc:4002-4006); mx_cpmove rebases [MEGA_SEL_SLD, SLD+8) only (11173-11179); BACK is aimed by mega_backf/mega_bkaimf from lz_match (4064-4093; lz.inc:519-534). The likelier collision of the same class - another task's decode re-aiming BACK, ODD or SLD - is not covered by widening the rebase; whether LZ decoding is serialized is open.
- **Fix:** rebase the whole K0..SLD range in mx_cpmove (`mov si,MEGA_SEL_K0 / mov cx,MEGA_SEL_SLD+8`; PSCR and CODE in between are based outside the pool). Rethink: one LDT selector per LZ or module stream, allocated with it.
- **Bytes:** 0 .mega now; rethink ~-60 .cold aim helpers against +40 alloc/free. **ABI:** none. **Confidence:** corrected.
- **Rows:** lzload (soak), megahigh (full).

### BR-68 kern_mega: the kernel's own writes through CS are completed silently

- **Sources:** mc-10 (weakness, medium, confirmed).
- **Kernels:** mega.
- **Rank:** S medium (it hides a kernel bug), L medium, B low-medium (a missed CSDO in .text becomes a permanent #GP and trampoline per store, reported only under MEGATRAP=1).
- **Mechanism:** mega_fault sends every #GP to mega_gpfix/mega_gpw whatever the CS (mega.inc:2418-2427); mega_gpw takes the data segment from CS (4553-4558) and mega_cs2ds maps 0x0060 to KDATA (4818-4823). The trap exists for packages (ABI-BREAK-LIST L3).
- **Fix:** count fixups whose faulting CS is a GDT selector in [mega_kfix], read by the harness as it reads [mega_infault]; a Bochs row asserts it stays 0 across a boot and a standard workload.
- **Bytes:** +8 .mega. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megatrap0 (soak).

### BR-69 kern_mega: the park stub's PCHK check can never run

- **Sources:** md-1 (bug, medium, confirmed).
- **Kernels:** mega (HIBER.DRV's park stub).
- **Rank:** S medium, L low (a damaged PKB cell after a warm reset), B high (the restore moves too little or runs past 640KB).
- **Mechanism:** `cmp ax, mega_dwake / je .cells` jumps every PARK past .cells0, which is reached only for mega_hbwake (megapark.inc:67-89); nothing passes mega_hbwake any more (megados.inc:1363-1364; megares.inc:579; hiber.inc:4469); W7 passes the header's own cells, so the first check compares the header with itself, and MPK_C_PKB at 3B8h is not summed (megados.inc:938-951, 2141-2149, 2229-2231).
- **Fix:** route mega_dwake to .cells0; delete the mega_hbwake compare and the NREG==0 arm (a park always has NREG > 0, and MPK_SAVE and the header cells come from the same values, so a normal trip still passes).
- **Bytes:** -12 HIBER.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megadosp, megadosroom (soak).

### BR-70 kern_mega: the HMA kern_dos's mouse vector leaves its A20 trampoline at the first poll

- **Sources:** md-2 (bug, medium, corrected).
- **Kernels:** mega (KD_HMA, implied by KD_MEGA).
- **Rank:** S medium, L medium (a DOS program that shuts A20: a direct int 15h AH=87h on an IBM-AT ROM, or AX=2400h), B high (the next mouse IRQ goes into wrapped low memory; the re-arm also drains one RBR byte mid-packet).
- **Mechanism:** kd_mou_arm writes CS:kd_mou_isr with CS = FFFFh (kerndos/kdmouse.inc:149-156), kdh_tramp then rewrites it to a LOW_SEG slot (kerndos/kdhma.inc:47-64; kdentry.inc:327, 396), and kd_mou_rearm, on every INT 33h AX=3 and every key poll, compares the vector with CS:kd_mou_isr, fails, and re-arms without a trampoline (kdmouse.inc:347, 391-395).
- **Fix:** on KD_HMA, publish through kdh_vslot in kd_mou_arm and compare against the same answer in kd_mou_rearm (dos_hvec is DOS_MEGA only; call kdh_vslot directly under %ifdef KD_HMA).
- **Bytes:** +8 HMA kern_dos image only. **ABI:** none. **Confidence:** corrected (the dos_hvec route would not assemble everywhere).
- **Rows:** kdmouse, dosmouse (soak, on kern_mega's arm).

### BR-71 kern_mega: A20 openings test once (W7) or 512 times (unpark, streaming resume)

- **Sources:** md-3 (bug, medium, confirmed), md-10 (hazard, low, confirmed).
- **Kernels:** mega.
- **Rank:** S medium, L medium (the IBM-AT ROM's int 15h AH=87h leaves A20 shut, so the 8042 path is live on exactly the ATs W7 targets), B high (W7 has already cleared the anchor, so a miss loses the parked session with no message; kd_unpark prints the A20 sentence and int 19h follows).
- **Mechanism:** mega_wrprobe clears the anchor, writes port 92h and 8042 D1h/DFh, waits only for IBF to clear, tests A20 exactly once and on a miss boots normally (megados.inc:2116-2128, 2167-2182, 2235-2242). kdp_a20w and mr_a20 poll 512 times (kerndos/kdpark.inc:135-142; megares.inc:481), kdh_tchk 65,536 (kdhma.inc:203-206, 224).
- **Fix:** poll as kdh_tchk does (`xor cx,cx / loop`) at all three sites.
- **Bytes:** +8 .boot2; -1 HMA kern_dos; -1 HIBER.DRV. **ABI:** none. **Confidence:** confirmed.
- **Rows:** megadosp (soak).

### BR-72 The disk cloner cannot see 8-sector or single-sided disks

- **Sources:** hiber-12 (weakness, medium, confirmed).
- **Kernels:** big (DISKOPS.DRV).
- **Rank:** S medium, L medium (160KB/180KB/320KB DOS 1.x disks without a BPB are the commonest 5150-era disks, the case the module header says the cloner exists for), B low (CERR_IO, or "Not a disk image").
- **Mechanism:** the probe ladder tries 18, 15 and 9 sectors per track with two heads (diskops.inc:601-614, 629-657); Write Img has four double-sided sizes and CL_HDS = 2 (1702, 1730-1734).
- **Fix:** add 8 after 9 in the ladder; on a failed 2-head extent read retry with one head before dropping to 40 cylinders; give clo_imgtab a heads column and rows 320/8/1, 360/9/1, 640/8/2.
- **Bytes:** +30 DISKOPS.DRV, 0 res. **ABI:** none. **Confidence:** confirmed.
- **Rows:** diskclone, imgcases (soak).

### BR-73 Packages write the window record's geometry, which the SDK says they may not

- **Sources:** abi-7 (weakness, medium, confirmed).
- **Kernels:** package side (Paint, the Sticky library); every kernel.
- **Rank:** S medium, L high (Paint's pt_wfix and the Sticky library do it on every use), B low-medium (derived state W_NATR, W_ZOOMR, W_SUEXT is bypassed).
- **Mechanism:** os88api.inc:3165-3176 says the fields are "readable by contract, not writable"; apps/paint/paint.asm:12831-12870 writes W_W/W_H/W_X/W_Y from a size-changed callback; apps/os88stik.inc:1023, 1712, 2376-2407 hides, writes and shows. The writes exist because OSAPI_WM_RESIZE always repaints and cannot be used from that callback.
- **Fix:** a no-repaint/hidden mode for OSAPI_WM_RESIZE (top bit of CX), or legalise hide-write-show for HIDDEN windows only; a host lint for stores to `[es:..+W_*]` under apps/ with an allow-list.
- **Bytes:** ~+10 .text res for the mode; 0 for the lint. **ABI:** additive (a flag bit, no bump). **Confidence:** confirmed.
- **Rows:** paintnogrow (soak); stik (fast).

### BR-74 checkdocs fails the build on committed review reports

- **Sources:** engsys-1 (bug, high, confirmed).
- **Kernels:** none (the build gate).
- **Rank:** S high, L high on this branch, B medium (`make` stops before building a floppy).
- **Mechanism:** checkdocs is `all`'s first prerequisite (Makefile:2425); six problems sat in this review's own area reports, several of them quoting a bad citation as evidence. docs/README.md says reports and history are not maintained, yet checkdocs fails on them; SITES_HIST exists to work around exactly that contradiction (tools/checkdocs.py:124-152).
- **Fix:** the six lines are fixed as part of assembling the review. Systemic half: problems in docs/reports/ and docs/history/ (and possibly docs/plans/completed/) print as `warn:` lines and fail nowhere else.
- **Bytes:** 0 shipped. **ABI:** none. **Confidence:** confirmed.
- **Rows:** the `make` gate itself; no suite row.

### BR-75 A citation that resolves to the WRONG section passes

- **Sources:** engsys-2 (hazard, high, confirmed).
- **Kernels:** none (documentation; hard rule 8 binds packages).
- **Rank:** S high, L high (already live), B low-medium (six cites of SPEC 46, ArtfulType, mean SPEC 47, the greying standard).
- **Mechanism:** SPEC.md:161, 26453, 26463, 26555, 55757, 76640 cite 46 where 47 is meant (SPEC.md:96478 heads 47, 95185 is in 46); the preamble refuses renumbering because checkdocs cannot catch this class (SPEC.md:20-23), and the class is live without one.
- **Fix:** correct the six; add a heading-drift check comparing SPEC.md's number-to-title map at the merge-base with HEAD, failing a changed pair and listing every citation of the old number.
- **Bytes:** 0. **ABI:** none. **Confidence:** confirmed.
- **Rows:** the checkdocs gate.

### BR-76 Basename collisions silently disable citation checks

- **Sources:** engsys-8 (hazard, medium, corrected).
- **Kernels:** none (tools).
- **Rank:** S medium, L high (29 shared basenames: README.md x27, BRIEF.md x11, SKILL.md x10, LESSONS.md x8), B low (a qualified citation is resolved against the wrong file and skipped as uncheckable).
- **Mechanism:** checkdocs' stems dict keeps the last path (tools/checkdocs.py:204); docs/README.md:74's "no shared basename" rule is ungated; `LESSONS.md 3` in two skills resolves to vga-face's, which numbers no headings.
- **Fix:** resolve a qualifier in the citing file's own directory first, by basename only when unique; fail an ambiguous qualifier naming the candidates.
- **Bytes:** 0. **ABI:** none. **Confidence:** corrected (count).
- **Rows:** the checkdocs gate.

---

## Register - low

Each row carries the same fields as above in short. "rec" in the bytes column
means the reviewer and verifier agreed on RECORD-only (write it down, do not
spend a byte now). ABI is `none` unless stated.

| BR | defect (sources, verdict) | kernels | mechanism (evidence) | fix | bytes | rows |
|---|---|---|---|---|---|---|
| BR-77 | [gfx_color] after OSAPI_GFX_BLIT4 and OSAPI_FONT_RUN depends on which path drew (gfx-5 confirmed, text-3 confirmed) | all | the run paths store the pen, the decoders and the fast font passes never do (vga12.inc:2343-2344, 3245-3250, 6938 vs softgfx.inc:983-1088, vga12.inc:2540-2692; font.inc:1951-1961, 2228-2236); headers promise "the last colour drawn" (vga12.inc:1742-1743; font.inc:1090-1093) | declare the pen UNSPECIFIED after both in SPEC 5.4, 6.1 and the SDK; or bank AL and restore | 0 (doc, SDK text) or +8 .text each | dispblit, toastbar (soak) |
| BR-78 | Sleep and timer ranges are sign compares: OSAPI_TASK_SLEEP with AX 8001h..FFFFh wakes at the next tick, OSAPI_WM_TIMER with AX >= 32768 fires at once, and a UI-task sleep ends at the next input (histsys-15 corrected, sched-5 corrected, wm-8 confirmed, sched-4 confirmed) | all (WM_TIMER body is KERN_BIG) | task_sleep stores ticks+AX, the wake scan tests a sign (sched.inc:1155-1159, 2362-2370); wm_timer the same (wm.inc:2061-2079; ui.inc:805-812); sch_wake_ui sets T_STATE=1 unconditionally (sched.inc:722); toast_show already clamps (toast.inc:216-224) | state 1..32768 for SLEEP, 0..32767 for TIMER and the UI-task behaviour on the cells and in SPEC 8 and 13.9; or clamp | 0 (SDK text, abilock no bump) or +5 / +6 .text | timerrace, timerbounce (soak) |
| BR-79 | sch_rhook returns IF=1 on its skip path under a sample ISR (sched-2 corrected) | big | sti before the three skip tests, re-cli only on the taken path (sched.inc:595-609, 624-627); the rest of the tick then runs interruptible on sch_chstack | move the `spk_seg` test and sti to after the `add word [sch_rbusy],0x0101`; NOT the reviewer's lower-the-lock-first reorder, which opens a nested-switch window | 0 | trkspkrate, vidspk22, vidspkfast (soak) |
| BR-80 | sch_lock has become a general Forbid: yield self-resumes, task_sleep returns at once marked sleeping, gfx_lock spins, and a task exiting with it raised kills pre-emption for the session (sched-3 confirmed, sched-18 confirmed) | all | SPEC.md:10687 names two raisers, the tree has a dozen (disk, diskw, memory, xmem, fsx, hiber, mega, megados, snd); sched.inc:1151-1164, 1881-1882, 1189-1211; vga12.inc:5091-5104 | rewrite SPEC 7's paragraph as a nestable pre-emption-off bracket with its holder list and the "nothing inside may yield, sleep or take gfx_lock, or exit" rule; optional task_sleep guard | 0 (doc); +7 .text optional | kbdpreempt (soak) |
| BR-81 | evq_push wakes the UI task before the record is in the ring (sched-6 confirmed) | all | sch_wake_ui before pushf/cli (events.inc:84, 92-93): a task-context caller at IF=1 pre-empted between them loses the wake until the tick | move the call to just after the cli | 0 | evqfull (soak) |
| BR-82 | The two private ISR stacks have no canary and sit back to back; SCH_CHSTK's sizing text has the ROM's EOI order backwards (sched-7 corrected, isr-9 confirmed) | big, small | mou_pstack LOW_SEG:06D6..0756 then sch_chstack at 0756 (build/kernel.map; sched.inc:80-89, 2937; mouse.inc:6624-6635); SPEC.md:13125-13130 and sched.inc:2187-2188 say EOI-then-sti, the IBM listing and SeaBIOS run int 1Ch BEFORE the EOI | SCH_MAGIC at sch_chstack's base checked once per TICK after the chain's `mov sp,bx`, with its own panel; seed in a resident init (the .ovl is 5,119 of 5,120); correct both texts; a STKDIAG field reading before shrinking SCH_CHSTK | +9..+12 .text res | stk0water, stkpanel (soak) |
| BR-83 | STKDIAG's private-stack high waters scan from the wrong end (sched-8 confirmed) | knob only | sd_cwater/sd_mwater scan down for the first 0xE5 (stkdiag.inc:303-389) while sd_water scans up (230-273); the result sizes SCH_CHSTK and MOU_PSTK | scan bottom-up; re-take any field number that came from the panel | 0 shipped | no row |
| BR-84 | The death panel trusts the table it calls suspect (sched-9 confirmed) | big, small | sch_diepanel indexes inst_ptr with no bound and prints I_NAME to a NUL (sched.inc:2692-2721; instance.inc:786-797); draws into an fsx bracket's foreign mode | `cmp al,INST_MAX / jae .name` if touched (take it with BR-9's panel rework) | rec; +4 .text | stkpanel (soak) |
| BR-85 | OSAPI_WM_WAKE's "ISR-safe" has an unstated SS = LOW_SEG precondition (isr-3 confirmed) | all | evq_push writes the ring through SS (events.inc:80-128; wm.inc:2343-2360); DOS.O88's dvw_irq0 moves to a LOW_SEG slice first (apps/dos/dosw.inc:267-276) | state it in the SDK and evq_push's header; optionally load ES from mou_lowss and write es: | 0 (SDK text) or +3 .text | dosmouse (soak) |
| BR-86 | DF is not cleared at the IRQ0 callback boundary, and clip_put/clip_get trust it (isr-4 confirmed, ui-7 corrected) | all | sch_isr has no cld but calls the splash rep movsb, DSV_TICK and the FSXF_RATE hook (sched.inc:1892-2400, 2349-2353; splash.inc:1024-1029); fsx_rtick likewise (fsx.inc:1364-1446); a std package's clip_put writes clip_seg:0000 then wraps and walks down from FFFF, up to 64KB past the claim (clip.inc:150-151, 196-198) while toast_show defends (toast.inc:204-209) | one cld in sch_isr after KDS and in fsx_rtick; cld in clip_put and clip_get; write "DF is the interrupted code's" into the DSV_TICK and hook contracts | +1 .text res (+1 mega fsx_rtick); +1..+2 clip | clipkeep, clipgrow (soak) |
| BR-87 | The serial lease trusts its caller to pin the region the ISR writes into (input-4 corrected) | big | srl_open validates I_SPTR and bounds but not MC_RLOC (serial.inc:226-243, 272, 423-470); SPEC.md:225404-225405 makes the pin the caller's duty and WireFleet complies (apps/wirefleet/wflink.inc:118-121) | SERIAL.DRV refuses Open with AX=5 when the region's MC_RLOC != 0; or move the ring into the driver's own image | +~20 SERIAL.DRV, 0 res | serlease, wirefleetlink, ringmove-wirefleet (soak) |
| BR-88 | Shift+PrtSc runs the ROM's Print Screen inside kbm_isr's IRQ mask (input-5 confirmed) | big, small | the ROM is called with every IRQ but IRQ1 masked (mouse.inc:3021-3031) and nothing hooks int 05h: seconds with no printer, tens of seconds printing, ticks lost | set the print-screen status byte at 0500h to 1 in mouse_init and after a hibernate resume (also disables PrtSc for DOS programs); or hook int 05h to an iret (~+12) | +5 .ovl/.boot2, 0 res | kbdburst (soak) |
| BR-89 | The 'MO' block publishes the doctored 8042 command byte as the BIOS's (input-6 confirmed) | big | mou_p2cmd0 is BIOS & 0EFh or 20h; the raw byte is outside the published span (mouse.inc:1901-1912, 6459-6469; SPEC.md:14127-14130) | swap the two declarations and their names; fix the comment with the code | 0 | ps2mouse (full) |
| BR-90 | srl_open's "settled" test lags the ISR by one UI pass (input-7 confirmed) | big | tests [mou_hpst]==2, set on the pass after the ISR settled (serial.inc:265-268, 323-334; mouse.inc:1371-1379, 3911-3916): a named Open can take the winning mouse port | test [mou_seen] instead | 0 (SERIAL.DRV) | serlease (soak) |
| BR-91 | KBDDIAG=1 drops SPEC 9.8.3's IMR bracket (input-8 confirmed) | knob only | the diag arm calls the ROM with every IRQ open (mouse.inc:2978-2996 vs 3021-3031); kbddiag.inc:24-25's header is stale | wrap it in the same bracket; fix the header | +~8 knob only | no row |
| BR-92 | mou_p2_off writes the 8042 on the UI task with nothing excluding int 09h (input-9 confirmed) | big | 0A7h then 60h + value with IF=1 and IRQ1 live (mouse.inc:2189-2203, 1753-1765); an AT int 09h's ADh/AEh between the halves sends the value to the keyboard; the probe masks IRQ1 for this race (1848-1863) | bank and mask IRQ1 at the master around the two writes (not pushf/cli: the wait counts ticks) | +~12 .text res | ps2mouse (full), p2hiber (soak) |
| BR-93 | The hot-plug poller drops DTR on a modem port every 3 s for the session (input-11 confirmed) | big, small | while [mou_seen] is 0 mou_hotplug power-cycles every unleased live port (mouse.inc:1335-1370); a modem at &D2 hangs up; SPEC 115.1.1 rule 4 already treats DSR/DCD as not-a-mouse (serial.inc:342-352) | skip the drop on a port whose MSR shows DSR or DCD | +~14 .text res | wirefleetnomouse, serlease (soak) |
| BR-94 | A settled serial mouse is never offered a reset edge again (input-14 confirmed) | big, small | [mou_hpst]=2 is terminal (mouse.inc:1298-1299): a mouse wedged by a cable bump stays wedged | a Control Panel verb or key running one drop-hold-raise on [mou_port] | rec; +~20 CTRL.DRV | no row |
| BR-95 | Key and mouse order is lost across a UI pass (histsys-6 confirmed) | big, small | ui_task takes one int 16h key per pass, then drains the mouse ring (ui.inc:120-245, 224-233): under starvation a click on B then typing sends the first key to A | kbm_isr pushes a payload-less EVT_KEY marker when the BIOS tail moves; the drain fetches the key at the marker | +~25 .text res (optional) | kbdburst (soak) |
| BR-96 | The wake re-arms the LOSING serial port (hiber-1 corrected) | big (HIBER.DRV) | mou_pall writes both UART rows (hiber.inc:4801-4807; mouse.inc:474-481); on the reboot route, a resume reached before the fresh contest settled brings the loser home unmasked with IER=1 and [mou_seen]=1, so mou_lockon never runs again | after the IER write, when [mou_seen], call the resident mou_lockon through hbm_kd before .kbunm | +10 HIBER.DRV | hibernate, p2hiber (soak) |
| BR-97 | Nothing fences compaction or shedding during the progressive image write (hiber-5 corrected) | big (HIBER.DRV) | the image is written live through the file layer (hiber.inc:1886-1918), which can claim (disk.inc:6194-6220, 7540-7600); safe today because the FAT window already exists | raise [mem_cp_busy] across the write (it also makes mem_free and mem_movable refuse: the write path must do neither); a test leg with the FAT window dropped | +10 HIBER.DRV | hibernate (soak) |
| BR-98 | The resume stub's int 13h calls run through kbm_i15 in the image being overwritten (hiber-6 confirmed) | big on KBD_I15 machines | hbm_res never calls mouse_unhook (hiber.inc:2586-2651, 3626-3683), so an AT ROM's int 13h calls int 15h AH=90h/91h into kbm_i15 (mouse.inc:3079-3084); survives because the bytes are identical | mouse_unhook via hbm_kd after hbm_down | +6 HIBER.DRV | hibernate (soak) |
| BR-99 | A failed no-question resume pins HIBER.DRV for the session (hiber-8 confirmed) | big | HBP_DOS opens no window; on refusal hb_mode=HB_M_RESUME and hbf_dropck drops only GONE (hiber.inc:2430-2435, 2664-2671, 721-729); same after a failed cw_app_launch | set HB_M_GONE when hbm_win reports no window | +8 HIBER.DRV | hibernate (soak) |
| BR-100 | A pointer this kernel can never read is kept and HIBER.DRV reloaded every boot (hiber-9 confirmed) | big | any dskw_read failure goes to .gone with no forget (hiber.inc:2368-2369); SPEC 87.9's FERR_BIG for a kern_mega pointer | FERR_BIG to .stale ("Hibernation: other build", forget) | +6 HIBER.DRV | hibernatem, megahiber (soak) |
| BR-101 | Write Img lays sector 0 over a short disk and says "Wrote", and never checks the target can hold the image (hiber-10 confirmed, hiber-11 confirmed) | big (DISKOPS.DRV) | a zero chunk ends the loop and clo_fin runs (diskops.inc:1902-1926); clo_wimg calls no clo_fits, and clo_fits skips same-drive (1612-1654, 409-415) | refuse CERR_NOTIMG unless CL_NEXT == CL_TOT; gate clo_same on CL_END != 2 and call clo_fits | +8 and +10 DISKOPS.DRV | wimgtrip, diskclone (soak) |
| BR-102 | Two non-atomic BDA tick reads (hiber-16 confirmed) | big (HIBER.DRV) | 046Ch then 046Eh with interrupts on (hiber.inc:3146-3149, 4647-4648): a carry between them is an hour of error | pushf/cli around each pair | +6 HIBER.DRV | hibernate (soak) |
| BR-103 | hbm_hsave trusts ROMF_HTAB's size (hiber-18 confirmed) | big | `sub cx,6 / ja` walks 64KB on CX=0; HBP_PAD keeps only the low byte (hiber.inc:2072-2096, 2385-2392) | jcxz guard; %error in COLDHMA pinning the table under 256 bytes | rec; +2 HIBER.DRV | hmahiber (soak) |
| BR-104 | OSAPI_MEM_FREE and _REGROW act on the caller's own live region, and the damage lands on the next claimant (hm-2 corrected, mem-6 corrected) | big, small | mem_find_own's self arm (memory.inc:4415-4422) fences mem_free_x (4459) and mem_regrowp (4576); REGROW path 3 calls no relocation; DELIBERATE per loader.inc:1690-1695 and SPEC 66.6.1.1 | owner's call: refuse a region record in the package doors mmf_osapi_mem_free and osapi_mem_regrow_x (NOT in mem_find_own, which mem_owner_of_x needs); allow the shrink | +3..+8 .cold res; a contract change | memp (soak) |
| BR-105 | KB-to-paragraph shifts trust CF, which holds only the last bit out (hm-6 corrected, mem-5 confirmed, md-16 confirmed) | big, small; kern_dos | mem_claim_dma_x's head: CX=2048 gives SI=0 and a claim with no page constraint (memory.inc:860-868; package door 5254-5266); kerndos mem_claim_x the same (kerndos/kdshim.inc:409-412); mem_regrow's sibling already fixed (4562-4565) | `cmp cx,64 / ja <refuse>` before the shift; `cmp ax,0x400 / jae` in kdshim | 0..+3 .cold; +1..+4 kern_dos | memp (soak) |
| BR-106 | kern_mega osapi_mem_regrow_x's move copies the OLD size into the NEW claim (mem-19 corrected) | mega | the .move arm copies old KB << 9 (memory.inc:5440-5468) and mega_claimregrowf sends EVERY huge-block regrow there, shrinks included (mega.inc:12052-12056, 12084): an overrun, a 0-length copy at 128KB+, or an offset past 64KB | copy min(old, new) and refuse (or segment-step) past 64KB | +4 .cold (above 1MB) | megahigh (full) |
| BR-107 | OSAPI_MEM_P treats an unknown verb as MEMP_AVAIL and hands back the caller's CF (hm-14 confirmed) | big, small | `cmp bl,MEMP_AVAIL / jae .avail` (memory.inc:5582-5583) and pushf/popf (4927, 5023) | `cmp bl,MEMP_KPARTS / ja <stc/retf>`, or document | +4 .cold | memp (soak) |
| BR-108 | wm_su_drop ignores mem_free's refusal and loses the claim (wm-9 corrected) | big, small | zeroes the slot then calls mem_free, which refuses under mem_cp_busy (wm.inc:9866-9888; memory.inc:4446-4453); needs a relocation proc to break SPEC 66.3 rule 3 | clear the slot only on CF=0 | +3 .text | no row |
| BR-109 | OSAPI_WM_FULLSCREEN draws a HIDDEN window whole, and the destroy/fullscreen lock texts are stale (wm-7 corrected) | big, small | no visible test before wm_raise (wm.inc:4316-4345); both require the caller to hold the lock (kernel.asm:2724, 3228; os88api.inc:304, 892-894) but the SDK's "as OSAPI_WM_HIDE wants it" is no longer true; wm_lk_go's `or al,al` would clobber CF | refuse or defer the raise for a hidden window; fix the SDK text; doors optional (wm_lk_go needs pushf/popf to carry CF) | +~5 .text; +6..+19 with doors | dispfsx (soak) |
| BR-110 | ui_dispatch decides "is this Locator" by an unlocked read of AM_ONCMD = 0, and .oncmd reads through BX = 0 (ui-3 confirmed) | big, small | ui.inc:2512-2536, 2586-2600; menu_win_set takes no lock (menu.inc:448-457); SPEC 12.2 reserves 0 for Locator but the SDK's AM_ONCMD line does not (os88api.inc:4072) | `cmp word [menu_win],0 / jne .app`; `or bx,bx / jz .app_done` in .oncmd; reserve 0 in the SDK | -19 .text res, both | no row |
| BR-111 | ui_cmd's jump table has no length assert against CMD_LAST (ui-4 confirmed) | big, small | ui.inc:3023-3045; menu.inc:75-88; the comment's count is already wrong | .tab_end and `%if (.tab_end-.tab)/2 != CMD_LAST / %error` | 0 | no row |
| BR-112 | A typed plain Enter matches the Alt+Enter latch (ui-5 confirmed) | big, small | the latch holds scancode 1Ch and the dedupe compares AH alone (ui.inc:195-207; mouse.inc:3198-3214): an enhanced ROM gets two fullscreen toggles and no Enter, an XT ROM loses the Enter | `test al,0x0F / jnz .notae` before the compare | +4 .text res | no row |
| BR-113 | A toast raised under a fullscreen window or saver expires unseen (ui-6 corrected) | big, small | toast_arm stamps [toast_die] before menu_draw_bar's wm_fs_vis guard, in both toast_pass and toast_now (toast.inc:434-447, 632-639; menu.inc:825) | guard both arming sites, or toast_arm itself; or fix the comment and SPEC 59.3 | +5..+10 .text res, or 0 | toastbar, toast (fast) |
| BR-114 | Under TOAST_Q one raiser's retire takes down another's message (ui-16 corrected) | big | toast_show's .kill calls toast_kill on whatever is up (toast.inc:224-229, 291-301); SPEC 59.11 documents it, against its own ownership model | retire only when DH = [toast_inst] or the caller is the kernel | +6..+10 .text res | toast (fast) |
| BR-115 | W_ONMOUSEUP is not delivered when the ring dropped the release (ui-9 confirmed) | big, small | .arm_done clears a region-0 arm when the button is up and the ring empty (ui.inc:773-783) while .mup says "the kernel guarantees delivery" (325-327); evq_mup drops on full (events.inc:223-245) | dispatch W_ONMOUSEUP at [mouse_x]/[mouse_y] through .mup_lk when [ui_armr]=0 at the drop; or weaken the contract | +12 .text res, or 0 | mouseup, evqfull (soak) |
| BR-116 | ui_sys_open indexes fm_stattab with no bound (ui-17 corrected) | big, small | ui.inc:2941-2959; toast_say exists to carry LD_NSTAT's bound (toast.inc:324-327; files.inc:2375) | `cmp al,LD_NSTAT / jae .back` | +4 .text res | no row |
| BR-117 | The Remove Shortcut item is poked by raw offset from five sites (shell-6 confirmed) | big | menu_items_file+2 at desk.inc:1406, 1452 and desksc.inc:517, 541, 1492; the table comment names the wrong writer (menu.inc:2576) | MENU_FILE_REM equ with an %if MENU_FILE_N guard | 0 | desksc (soak) |
| BR-118 | Basic dock_geom works only at boot or with CTRL.DRV loaded (shell-11 confirmed) | big | dkb_geom far-calls CPE_DOCK_GEOM outside the splash (dock.inc:213-220); a future caller gets mod_gone's CF and no geometry | state the invariant in the banner, or fall back to dkb_band | 0, or +3 .text | dockpos (soak) |
| BR-119 | STIKMENU.DRV writes the kernel's service-item record, and a driver's record is half-sanitised (shell-7 confirmed, shell-12 corrected) | big | stm_a_more copies into desk_svc_file/desk_svc_name (stikmenu.inc:1844-1864); osapi_desk_item_x terminates only the caption (desk.inc:2236-2251) | pass ui_svc_open the strings as inputs; optionally terminate file and name | 0..+4; 0..+10 .cold | stikmenu, deskitem (soak) |
| BR-120 | [ld_said] leaks out of loads nobody reports, silencing the next failure (loader-7 confirmed) | all | set at .abort/.rehtoast; ldf_ld_pkg_start and assoc_run_x's hand-on arm never consume it (loader.inc:548-557, 1552-1573, 2085-2103; assoc.inc:1315-1316; ui.inc:2944) | clear the adjacent word at the three ld_painted clears and on the Task Manager route; or return the flags in AH | +8 .cold, or ~0 | assocopen (soak) |
| BR-121 | DESK.DRV stays loaded when a widget launch meets a full instance table (loader-9 confirmed) | big | desk_need before the record scan; `jc .nomem` skips .nodrv's drop, and desk_drop_x there would clear a bit one past the table (loader.inc:1199-1215, 1275-1282; deskmod.inc:121) | refuse before desk_need when the top-down scan finds no free record | ~+5 .cold | stikboot (soak) |
| BR-122 | OSAPI_PKG_START's caller image must be pinned across the call; the contract does not say so (loader-10 confirmed) | all | [ld_msrc] is banked before ld_alloc, which may compact (loader.inc:1872-1873); The Wire pins (apps/thewire/thewire.asm:3234-3236) | one sentence in SPEC 21.5 | 0 (doc) | thewire (soak) |
| BR-123 | fmv_owner's "cannot happen" fallback tags the cache as the menu save-under (files-6 confirmed) | big | files.inc:1376-1382 owns it as MEM_P_MSAVE, whose shed row names menu_sseg (memory.inc:1308); unreachable today | a non-purgeable kernel tag, or stc | 0 | no row |
| BR-124 | The Standard File commit's volume put-back ignores a failed mount (files-7 confirmed) | big, small | fdlg_reap_x calls dsk_chdir_x and never reads CF (fdlg.inc:597-606); kernel requesters with no instance then act at the root with the write gate shut | on CF skip the callback with a beep | +2..+5 .cold | fmcommit (soak) |
| BR-125 | Opening a chooser ends other windows' prompts without repainting them (files-8 confirmed) | big, small | .endp clears modes 1-2 in every pool block, a byte store and nothing else (fdlg.inc:336-351) | mark each cleared block FS_DIRTY=FSD_PIXELS or call fm_owed | ~+4 .cold | fdlgchoose (soak) |
| BR-126 | Modality filters presses, not drops: a file dragged out of a Save chooser moves into the window behind (files-9 confirmed) | big | fdlg_grab sees MDOWN/RDOWN only (ui.inc:457-470); fm_dgdrop resolves with wm_hit (files.inc:3730-3760, 7136-7156, 7221-7229) | in fm_dgdrop with [fdlg_win] set, refuse any target but the chooser | ~+10 .cold | fdlgdrop (soak) |
| BR-127 | kern_small keeps a closed window's 2KB cache claimed (files-10 confirmed) | small | MEM_P_VIEW+slot is reaped by nothing; freed only when the pool block's next tenant runs fm_kinit_x (files.inc:555-565, 1432-1433) | free the chooser's cache in fdlg_reap_x and a KIND_FILES cache on close | ~+10 .cold | fdlgsmall (soak) |
| BR-128 | A folder copy silently skips hidden and system files, and a cross-volume Cut then stops at the first one (files-12 confirmed) | big, small | fcp_scan asks dsk_dscan with DL=0 (filecp.inc:1222-1225, 1297-1298); dskw_rmtree refuses FERR_PROT (diskw.inc:1006-1010) | state it in SPEC 22.3's limits; optionally toast a skip count before the delete phase | rec; ~+15 if toasted | fcpcopy (soak) |
| BR-129 | ASSOC.DAT can exhaust the association slots for the session, and an emptied volume keeps a stale cache (files-13 confirmed, text-11 confirmed) | big; FORMAT.DRV | nothing frees a slot; assoc_ext_take accepts any byte above 20h (assoc.inc:734-761, 2077-2100); asw_close returns CF=1 with nothing to write and the flush takes .same (ascw.inc:153-154, 186-188, 983-990) | filter the three bytes through dskw_char_x and stop absorbing near the cap; write an empty header over an old file | ~+10 .cold; +~10 FORMAT.DRV | assocstale, assocflush (soak) |
| BR-130 | The inst_tab I_SPTR relocation row also matches near pointers (files-14 corrected) | big | mem_rr_tab patches every I_SPTR equal to a moved base; a built-in kind's I_SPTR is a near pointer into fm_pool at 0AAFBh, safe only because that is above every heap segment (memory.inc:3351; instance.inc:201) | an %error bounding fm_pool's offset, or filter the row by I_KIND | rec; 0 | pkgmove-* (soak) |
| BR-131 | OSAPI_FONT_GLYPHS can return the seam cut's borrowed table (text-4 confirmed) | big (SEAMCUT) | font_ch_seam rewrites [font_seg]:[font_base] for one font_char under the lock (font.inc:448-458); osapi_font_glyphs reads it lock-free (kernel.asm:6627-6632) | a canonical pair for the API written only by font_init, hbm_wake 4a and the mega pool move; or document | +4 .bss, +~6 per writer | no row |
| BR-132 | Menu layout measures package strings with an unbounded font_width_x (text-5 confirmed) | big, small | 8 x len mod 64K (font.inc:2542-2557) in menu_widest before the clamp (menu.inc:2427-2460): an 8,192-byte item wraps to a narrow save-under while menu_trunc letters 24 cells | bound the measure at MENU_MAXCH+1; take the name width from the built menu_abstr | +4..+8 .text res | no row |
| BR-133 | A failed FAT flush keeps its dirt, which dsk_fatw_park may write onto another disk (dw-5 confirmed) | big, small | .ioerr skips .reset (diskw.inc:4989-4992); park flushes outgoing dirt before it knows a floppy changed (disk.inc:5794-5799) | .ioerr: dskw_ioerr then dskw_refat | +3 .cold | fdswap (soak) |
| BR-134 | WRITE_AT's INSIDE arm writes up to 511 bytes past the caller's buffer onto disk slack (dw-6 confirmed) | big | dskw_size32 rounds [dskw_rem] up and the arm transfers whole sectors from ES:BX (diskw.inc:2727-2747, 3954-3966); SPEC 18.4's "No memory ever leaks past EOF" | route the final partial sector through dskw_wone; or amend SPEC 18.4 to name the exception | +25 .cold res | dosfile (soak) |
| BR-135 | DSKW_RT_MAX bounds the size of a tree, not the depth of a cycle, and its trip leaves the volume deep in the tree (dw-7 corrected) | big | the guard decrements per entry (diskw.inc:1142-1144): a 4,096-entry tree stops part-deleted with FERR_IO; `jz .corrupt` skips dskw_rt_go_home | re-arm after each removal through one shared label; trip through .corrupt_home | +6..+18 .cold | no row |
| BR-136 | Short names with bytes 80h-FFh, or a leading 05h, are listed but cannot be opened, deleted or renamed (dw-11 confirmed) | big, small | dskw_char_x accepts only A-Z, 0-9 and dskw_syms (diskw.inc:5217-5248; 5150-5160) | accept 80h-FFh (no upcase); map a leading E5h to 05h in dskw_name83 | +8 .cold | dosdir (soak) |
| BR-137 | fsck's FILEnnnn.CHK number uses an 8-bit multiply and wraps at 10,000 (dw-18 confirmed) | big (FSCHECK.DRV) | `mul dh` (fsck.inc:2151-2163); four digits written (2298-2307) | 16-bit multiply; saturate at 9,999 and treat it as a full root | +6 FSCHECK.DRV | fsckdisk (soak) |
| BR-138 | A run past the buffer's segment wraps to ES:0000 silently (dr-7 confirmed) | big, small | .notched adds the run to BX with no carry test and the next cap at BX=0 is 128 sectors (disk.inc:1238-1242, 2128-2135, 2627-2631) | fail when `add bx,ax` carries while SI != 0 | +6 .cold | no row |
| BR-139 | dsk_rah_fill computes the track phase on the volume-relative LBA (dr-8 corrected) | big (BIOS hard-disk rows) | divides DI by spt while the transfer converts DI+[dsk_base] (disk.inc:7095-7099, 1750-1760); mis-sized fill on a partition not track-aligned; data stays correct | divide by spt*heads first and take the remainder mod spt, or bank base mod spt once per transfer (a full 32-bit dividend can #DE) | ~+14 .cold | hddgeom (soak) |
| BR-140 | dsk_find returns a redirected volume's name unterminated when the driver's is 13 bytes or more (dr-9 corrected) | big | the FSV_ENUM arm copies 13 bytes with no NUL forced; +13 is the attribute byte (disk.inc:3807, 8110-8119, 8193-8199) | `mov byte [es:di-1],0` after the rep movsb | +5 .cold | cdromsim (soak) |
| BR-141 | dsk_fdd_probe clears unit 1's calibrated bit whatever unit it probed (dr-14 corrected) | big | .out ANDs 0040:003E with 7Dh (disk.inc:10962) though the probe runs for units 1..3 (10772; desk.inc:349-352) | derive the mask from [fdd_unit]; keep clearing the probed unit's bit | ~+10 .ovl | deskfdd (soak) |
| BR-142 | Two unstated reads before write and a zero-capacity link in the mount (dr-15, dr-17 confirmed) | big, small | dsk_rah_fill reads [dsk_totsec] before the mount sets it (disk.inc:4220-4223, 7115-7122); dsk_up_open writes index 0 without [dsk_nmax] (5533-5560) | comment both | rec; 0..+5 | no row |
| BR-143 | Taking the terminator in a sector's last slot trusts the next sector to be zero (dw-19 confirmed) | big, small | diskw.inc:4648-4654; fsck.inc:2285-2291 | record only | rec | no row |
| BR-144 | kern_mega's drv_fs_hi wraps its buffer offset at 64KB (drivers-8 confirmed) | mega | `add [dfh_buf],cx` with a 32-bit capacity (driver.inc:2687-2817, 2786); depends on a caller passing >64KB | clamp to 10000h - BX, or pin the caller rule in SPEC 62.9.2.4 | rec; +~8 | no row |
| BR-145 | OSAPI_DRV_CALL promises DI back, which neither side keeps, and "CF=1 AX=0 = no driver" is ambiguous (abi-5 confirmed) | big | drv_pkg_call_x pops the caller's DI before the far call (driver.inc:6614-6662); os88api.inc:3456-3460; drivers/os88drv.inc:377 | driver contract says "preserve DI; BP is spent" | 0 (SDK text); +2 if enforced | ethcfg (soak) |
| BR-146 | An N-cell body reads the caller's DS out of api_rn's frame at a literal [bp+10] (abi-4 corrected) | all | diskw.inc:3397-3414; kernel.asm:4747-4767; ABI-BREAK-LIST T1 reshapes the frame | API_RN_DSOFF equ beside api_rn; name the reader in T1's row | 0 | api-abi (fast) |
| BR-147 | vid_apply arms the row table on VID_VESA, where the mouse stack may overwrite it (video-8 confirmed) | big | vid_rowtab lies under mou_pstack and a packed display's arrow may run into it (mouse.inc:371-388) while VID_APPLY builds it and sets vid_rowmax=348 (viddet.inc:1022-1042) | skip the build and leave vid_rowmax 0 when [vid_planes] > 4 (also ~4.6 ms per switch) | +7 per VID_APPLY expansion (+7 .boot2, +14 modules) | vesa (soak) |
| BR-148 | A same-mode bracket on the secondary paces FSXW_VSYNC to the primary's retrace port (video-9 confirmed) | big | fsx_wait picks 3BAh/3DAh from [vid_kind] (fsx.inc:953-958) | `mov al,[fsx_wdisp] / call vid_kind_of` | +3..+8 .text res | fsxdisp, dispfsx (soak) |
| BR-149 | vid_reboot leaves a mono SECONDARY showing a frozen desktop (video-10 corrected) | big | handles only a Hercules primary (vidsel.inc:499-522), against SPEC 39.20.1 | program the mono card to text by hand and restore [vid_kind] (HIBER.DRV's hbm_stageseg reads it after vid_reboot); dark a CGA secondary with vid_blank_kind | +18..+24 .text res | dispreboot (soak) |
| BR-150 | A mode-setting bracket's collapse leaves vid_ctx record 0 describing the primary (video-11 confirmed) | big (EXTD.DRV) | vid_fsx_enter republishes with ndisp=1, cur=0 and captures nothing (extmod.inc:437-466) | state it as binding in SPEC 39.18.3, or capture and restore record 0 | rec; 0 or +10 EXTD.DRV | fsxdisp (soak) |
| BR-151 | FSI_PAGES advertises pages fsx_page refuses (video-12 confirmed) | big, small | VGA0D says 8, a dual-card Hercules 2 (fsx.inc:1161-1189, 1005-1012) | FSIF_FLIP (FSI_FLAGS bit 3) set when fsx_page will flip | +8 res big, +4 small; additive (no bump) | wireballvideo (soak) |
| BR-152 | The planar bodies hard-code an 80-byte stride (gfx-7 confirmed) | big | ROW_BYTES at thirteen planar sites (vga12.inc:3398..4782) while gfx_blitp and gfx_blit1 read the live stride | refuse a 4-plane mode with stride != ROW_BYTES in vid_apply | +~8 .ovl (placement unchecked) | vgadirty (soak) |
| BR-153 | The SPL_RESIDENT guard stops four bytes short, and the rung is four sectors too big (boot-4 corrected) | big, small | SPL_RES_SIZE measured at spw_resident_end, before spl_live/spl_busy/spl_quiet (kernel.asm:8653-8656; viddet.inc:1743-1796); 9 sectors against a need of 5 | move the label; derive SPL_RESIDENT from the size; correct the %error text | 0 res; ~-400 bytes of KERNEL.SYS on kern_big | splashbar (soak), kzboot (soak) |
| BR-154 | Guard 5 omits BLOB_LIFT on every shipped kernel (boot-18 corrected) | big, small | the sum leaves out 1,536 bytes on ROM_COLD, the default (kernel.asm:9683-9686, 1121-1123, 1913-1933); stage 1 refuses correctly at run time | use BLOB_SEG_AT in the sum | 0 | small128 (full) |
| BR-155 | sch_isr draws a 28.7 ms spinner frame with IF=0 during kmain_o (boot-11 confirmed) | big, small | sched.inc:2318-2353; splash.inc:1150-1163: IRQ6 and IRQ4 blocked per phase change | sch_isr sets a "frame due" byte; spl_step draws it on the main line; measure with os88boot.py first | ~-10 .text, +6 .boot2 | splashspin (soak) |
| BR-156 | A floppy image dd'd to a USB stick boots drive A; Shut Down leaves hard-disk heads over data (boot-12, boot-15 confirmed) | big, small | boot.asm:276-298 forces DL > 3 to 0; shutdown.inc:24, 54, 328 | docs/LIVE-MEDIA.md troubleshooting line; a park verb in HDD.DRV if ever taken | rec; ~+20 module | shutdown (soak) |
| BR-157 | The MM58167 New-Year shadow is refreshed only on a Set or a roll (sysmisc-4 confirmed) | big | clk_ns_read stamps only when [clk_nsrol] (clock.inc:1090-1105, 1203-1218): off from one March to the next April reads a year low | stamp whenever the decoded shadow differs from [clk_tb_mo] | +~8 .ovl | no row |
| BR-158 | An RP5C01 in 12-hour mode is claimed in the morning and refused in the afternoon (sysmisc-5 confirmed) | big | the probe joins the 10-hour digit with its PM bit (clock.inc:1305-1315, 1463-1465) | also compare the 12-hour decode when the raw hour is above 23 | +~16 .ovl | no row |
| BR-159 | The Screen Saver settings window outlives the Control Panel, and its save waits for the next panel close (sysmisc-6 confirmed) | big | THMPAL's window is reaped at panel close (driver.inc:3083-3097), SAVER's only via ss_post_x, which sets [cp_wdirty] with nobody to spend it (blank.inc:844-861) | close it from the panel's teardown with a new SSV_ verb (DRVC_OVL is kernel-private); or flush when no panel is open | +~12 .cold res, +~15 SAVER.DRV | saver (soak) |
| BR-160 | Unloading VESA.DRV redraws the Drivers pane at a stale origin after vid_switch (sysmisc-7 confirmed) | big | cp_drv_click's VID_EXT arm switches and then runs cp_list/cp_drv_box1/cp_drv_line1 at the pre-switch DI/BP (ctrl.inc:7131-7179), against the rule at 4390-4396 | record the unload, cp_wdset, jmp .done past the redraw | -1..+2 CTRL.DRV | vesacp (soak) |
| BR-161 | SEG_LEND writes mega_rspar before validating, and checks no owner (mc-3 confirmed) | mega | mega.inc:12779-12784: a refused lend leaves rsseg on the old claim and rspar on the refused paragraph | store after the `jc .lno`; optionally compare MC_OWN | 0..+12 .mega | no row |
| BR-162 | Spurious IRQ7/IRQ15 get a non-specific EOI (mc-6 confirmed) | mega | mega_eoi1/eoi2 send OUT 20h,20h with no ISR read (mega.inc:840-852, 2136-2150) | stubs for 27h and 77h that read OCW3 0Bh first | +16 .mega | no row |
| BR-163 | mega_rfclk miscounts a reflection across midnight (mc-7 confirmed) | mega | low-word subtraction of 40:6C (mega.inc:3892-3934) | add 00B0h when the difference is 8000h or more | +6 .mega | no row |
| BR-164 | mega_k09: keypad -/+ give no ASCII with NumLock off; 40:96's right Ctrl/Alt bits are not kept (mc-8 confirmed) | mega | .pad covers 47h-53h (mega.inc:1837-1858); AH=12h returns 40:18 alone (2069-2073) | exclude 4Ah/4Eh; track 40:96 bits 2-3 | +20 .mega | no row |
| BR-165 | mega_s2segc truncates an off-grid base; mega_ustab rows are never freed; MX_FWIN is never cleared (mc-12, mc-13, mc-14 confirmed) | mega | shr ax,4 with no nibble test (mega.inc:5470-5486); mega_useg has no free path (14299-14385); MX_FWIN or'd at 13480, tested at 11243 | return FFFFh for an off-grid base; limit useg to A000h+; clear MX_FWIN with the last window row | +4, +6, +10 .mega | no row |
| BR-166 | mega_leds spins up to 2x65,536 polls inside IRQ1 with IF off (mc-15 corrected) | mega | mega.inc:1311-1345, 1756-1759 | bound the wait near 2,000 polls | +4 .mega | no row |
| BR-167 | OSAPI_XMEM_COPY writes anywhere below 1MB; the software-vector dispatch enters a handler with the dispatcher's flags (mc-17, mc-18 confirmed) | mega | mega.inc:9773-9776; 891-1022 | record (HIMEM parity; handlers read the frame) | rec | no row |
| BR-168 | The snapshot's fallback can build a prefix park below MPK_MIN_KB (md-4 corrected) | mega | .fallb uses KB-in-use + 16 with no floor (megados.inc:345-357, 795-798) against .ptry's floor (362-364) | clamp the runs claim to MPK_MIN_KB | +8 .mega | megadosroom (soak) |
| BR-169 | mega_d8042get can bank a keystroke as the 8042 command byte; the park stub's key wait accepts a PS/2 mouse byte (md-9, md-11 confirmed) | mega | megados.inc:1944-1984 tests bit 2 only; megapark.inc:242-254 tests no AUX bit | ADh before 20h and AEh after (or require bit 0); `test al,0x20` as mega_dkobf does | +5 .mega; +4 HIBER.DRV | megap2hiber (soak) |
| BR-170 | mega_dreg_add drops a region on overflow; the stub's restore loop is wrong below PKB 33; the trip's own time is uncounted and x87 state not reset (md-12, md-17 corrected, md-18 UNCHECKED) | mega | megados.inc:480-497, 73-76; megapark.inc:172-198; megados.inc:735-737, 1342-1348 | an %if for the region worst case; an assert PKB >= 33 where the floor is set; FNINIT if a package ever keeps x87 state across a yield | rec; 0 | megadosp (soak) |
| BR-171 | Section switch-back is gated only for nobits sections (engsys-12 confirmed) | build | NASM refuses only an open nobits section; shutdown.inc ends in .modu by design; the sentinel block omits .vga, .cfx, .modx, .mods, .modfs (kernel.asm:9786-9886) | a host check that each .inc's last section equals the one active at its %include, with an exemption list | 0 | no row (a fast unit row) |
| BR-172 | The whole-soak permission refusal is bypassed by `-k '*'` (engsys-15 confirmed) | tools | os88test.py:1318 and os88soak.py:1050 test only `not a.k` | also refuse a selection covering more than ~90% of the tier without --user-asked | 0 | no row |

---

## Fix batches

Eight batches, each a module boundary an implementation session can take whole:
few files, one review thread, one final pass. Batch order is a suggestion
(A and D carry the most dangerous items). Every fix in every batch is
phase **now** with no ABI break except three, which belong to the
ABI-BREAK-LIST batch instead: BR-34's IRQ ownership cell, BR-66's
ISR-reachable claim flag, and BR-104's policy reversal if the owner takes it.
Items marked "SDK text" change a fingerprinted contract comment and need
`abilock --update` without a PKG_FMT bump; that is not a break.

Byte sums are the reviewers' estimates added up, midpoints where a range was
given, rounded; "core" is the items a batch should not ship without, "all"
includes every low row in the batch. As the banner in CLAUDE.md says, these are
BYTES, not rungs, and a batch whose sum crosses no rung has still spent them.
kern_emu takes kern_big's figures. On kern_mega, `.cold` lands above 1MB, so a
batch's `.cold` figure is NOT billed to MEGA_RESIDENT; its `.text`/`.bss`
figure is.

### Batch A - boot, detection and the build's own gates

- **Files:** boot/boot2.asm, boot/boothd.asm, kernel/splash.inc, kernel/viddet.inc (vid_detect), kernel/disk.inc (dsk_boot_from_x, .ovl only), kernel/kernel.asm (guard 5, the SPL_RESIDENT guard), Makefile (KSIG_OFF, KZ_HEAD), tests/unit/t_canary.py, tests/unit/t_api_abi.py, tools/checkdocs.py, tools/os88test.py, tools/os88soak.py.
- **Items:** core BR-1, BR-6, BR-63, BR-25, BR-55, BR-56; with boot-7's dead DPT copy as the room. Low: BR-153, BR-154, BR-155 (measure first), BR-156 (doc). Host gates: BR-62's t_api_abi half, BR-74, BR-75, BR-76, BR-171, BR-172.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident | 0 (BR-155 would be ~-10 .text) | 0 (same) | 0 |
| .boot2 (transient) | +8 -41 -1 +30 +10 +4 = **~+10** (range +3..+17) | **~+10** (same items) | as kern_big |
| boot sector | boothd **-1** (+8 -9) | - | - |
| .ovl (transient) | **+4** (BR-25 +6, dr-13 -2); -28 later | 0 | as kern_big |
| disk | KERNEL.SYS ~-400 (BR-153); BR-6 may add one blob sector | - | - |

- **Rows (final pass):** canary, api-abi (fast); blobruns, blobsum, splashbar, splashspin, smallboot, kzboot, hdboot, hddgeom (soak); bootsmoke, small128 (full); box- twins of hdboot on 86Box for FN33's images. New: an MDA-primary boot, an MCGA and a 64KB EGA profile if MartyPC or 86Box carries one (none in the suite today).

### Batch B - display, FSX and drawing

- **Files:** kernel/fsx.inc, kernel/vga12.inc, kernel/vidsel.inc, kernel/apps.inc, kernel/fprog.inc, kernel/font.inc, kernel/menu.inc (menu_widest), kernel/viddet.inc (VID_APPLY), SPEC 5.4/6.1/53.10 and the SDK for the pen and page texts.
- **Items:** core BR-3, BR-14, BR-45, BR-46, BR-57. Low: BR-58, BR-77 (doc), BR-131, BR-132, BR-147, BR-148, BR-149, BR-151, BR-152. (kern_mega's BR-64 is in batch H.)

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | 0 +2 +18 +13 +8 (+2 blit1) = **~+43** | 0 +13 +8 (+2) = **~+23** | as kern_big |
| resident, all | **~+105** (adds BR-58 +13, BR-148 +5, BR-149 +21, BR-151 +8, BR-131 +10, BR-132 +6) | **~+33** (adds BR-151 +4, BR-132 +6) | as kern_big |
| transient | .ovl +8 (BR-152); .boot2 +7 (BR-147) | .boot2 +7 | as kern_big |
| modules | +14 across VID_APPLY's module expansions (BR-147) | - | - |

- **Rows:** dispblit, blitcut, blitpair, blitplane, atblit, atblitvga, wireballvideo, wireballmono, dispfsxherc, fsxdisp, dispfsx, vesa, vesacp, saver, savervga, toastfpg, dockmodule, dispreboot, vgadirty (soak). New: a negative-width BLIT4 call (must go red on `jz`).

### Batch C - window manager, UI and instance teardown

- **Files:** kernel/wm.inc, kernel/ui.inc, kernel/instance.inc (inst_task_die), kernel/toast.inc, kernel/deskmod.inc and DESK.DRV, kernel/desk.inc/desksc.inc (one equ), kernel/events.inc (BR-85 comment), apps/os88api.inc texts.
- **Items:** core BR-4, BR-8, BR-28, BR-47, BR-48, BR-49, BR-50, BR-42. Low: BR-73 (the resize mode and its lint), BR-85, BR-108, BR-109, BR-110, BR-111, BR-112, BR-113, BR-114, BR-115, BR-116, BR-117, BR-118, BR-119.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | +22 +5 +9 +0 +8 +2 +6 +10 = **~+62** (range +50..+75) | +22 +5 +9 +5 +8 +2 = **~+51** | as kern_big |
| resident, all | **~+105**: adds BR-73 +10, BR-109 +12, BR-108 +3, BR-110 **-19**, BR-112 +4, BR-113 +8, BR-114 +8, BR-115 +12, BR-116 +4 | **~+80** (same, less the kern_big-only rows) | as kern_big |
| with wm-2 in place of BR-8's compare | -1 against the core figure (net +4 instead of +5) | -7 (net -2 instead of +5) | |
| modules | DESK.DRV +6 (BR-42) | - | - |

- **Rows:** stkclass, stkapps, stik, stikcard, toast (fast); stk0water, stkpanel, timerrace, deskbench, dmgcull, zonedmg, paintnogrow, paintnogrow-vga, dockpos, dockmark, stikmenu, stikboot, stikswap, mouseup, evqfull, desksc, deskitem, toastbar, dispfsx (soak). New: a clickrace row on timerrace's model; a stkwater reading of Bounce closed over a second window; a wild-pointer WM call under int0sweep.

### Batch D - loader and heap

- **Files:** kernel/loader.inc, kernel/memory.inc, kernel/filecp.inc (floor sites; FILECP.DRV on kern_small), kernel/compress.inc (cmz_wclaim's first try), kernel/mod.inc only if BR-4 lands elsewhere, apps/os88partsbody.inc (BR-59), kerndos/kdshim.inc (BR-105's kern_dos half).
- **Items:** core BR-2, BR-7, BR-11, BR-12, BR-29, BR-30. Then BR-35, BR-52, BR-53, BR-54 (measure first), BR-59. Low: BR-104 (owner's call), BR-105, BR-106, BR-107, BR-120, BR-121, BR-122 (doc).

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | 0 +24 +13 +13 +13 +18 = **~+81** .cold | +10 +24 +8 +13 +13 +18 = **~+86** .cold | core .cold above 1MB; loader-8 ~-20 |
| resident, all | **~+150** .cold: adds BR-35 +5, BR-52 +12, BR-53 +26, BR-54 +8, BR-104 +5, BR-105 +2, BR-107 +4, BR-120 +8, BR-121 +5 | **~+110** .cold | +4 more for BR-106, above 1MB |
| modules / packages | FILECP.DRV a few bytes (kern_small); +20 in EVERY parts package's image (BR-59); kern_dos +1..+4 (BR-105) | | |

- **Rows:** heapgran, drvmem (fast); lzfence, lzload, lzload-lz4, lzfmt-all, lzbig, lzcomp, fcpcopy, fcpapi, fcproom, fcpsmall, fdlgsmall, memp, regrowshed, clipgrow, heapcheck, heapmap, heapdrv, msegnomem, pkgmove-*, dosmem, assocopen, stikboot, thewire (soak); small128, megahigh (full). New: a hostile-header package, a shed-during-mount row on the 128KB machine, a close-a-window-mid-paste row.

### Batch E - disk and file layer

- **Files:** kernel/disk.inc, kernel/diskw.inc, kernel/files.inc, kernel/fdlg.inc (FDLG.DRV on kern_small), kernel/fsck.inc (FSCHECK.DRV), kernel/assoc.inc, kernel/ascw.inc (FORMAT.DRV), kernel/filecp.inc (BR-128's doc only).
- **Items:** core BR-16, BR-17, BR-22, BR-23, BR-24, BR-27, BR-37, BR-41. Then BR-61, and BR-60 only on the owner's word (90 bytes). Low: BR-123..BR-130, BR-133..BR-143.

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | +16 +12 +12 +11 +5 -12 +16 +8 = **~+67** | +16 +9 +12 +11 +5 +16 = **~+68** | as kern_big (.cold above 1MB) |
| resident, all (without BR-60) | **~+185**: adds BR-61 +16 and the low rows (BR-134 +25, BR-135 +12, BR-139 +14, BR-126 +10, BR-129 +10, BR-136 +8, BR-138 +6, BR-140 +5, BR-125 +4, BR-124 +4, BR-133 +3) | **~+120** (adds BR-61 +16, BR-127 +10, BR-136 +8, BR-138 +6, BR-125 +4, BR-124 +4, BR-133 +3) | |
| BR-60 subdirectory growth | **+90** more | optional | |
| transient / modules | .ovl +10 (BR-141); FSCHECK.DRV +6; FORMAT.DRV +10 | FDLG.DRV +8 | |

- **Rows:** fdswap, wseq, dosmedia, curdisk, dosfull, fsckhd, fsckdisk, fmcommit, fdlgchoose, fdlgup, fdlgdrop, fdlgsmall, pkgmove-*, dosfile, dosdir, assocstale, assocflush, cdromsim, deskfdd, hddgeom (soak). New: a worker calling a file cell (must refuse), a hostile FileSize entry, an unreadable FAT, a 15th file in a kernel-made folder.

### Batch F - input, ISRs and the scheduler

- **Files:** kernel/mouse.inc, kernel/sched.inc (including the INT 0/INT 6 stubs and the system-error panel), kernel/events.inc, kernel/serial.inc (SERIAL.DRV), kernel/clip.inc (one cld), kernel/fsx.inc (fsx_rtick's cld only), kernel/kbddiag.inc (knob), kernel/stkdiag.inc (knob).
- **Items:** core BR-9, BR-18, BR-19, BR-20, BR-36, BR-51, BR-86 (the cld). BR-21 is MEASURE first. Low: BR-78 (doc), BR-79, BR-80 (doc), BR-81, BR-82, BR-83, BR-84, BR-87, BR-88, BR-89, BR-90, BR-91, BR-92, BR-93, BR-94 (record), BR-95 (optional); BR-34's four IMR read-modify-writes (the scan of vectors is batch G).

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | +30 +6 +15 +13 +13 +8 +1 = **~+86** | +30 +15 +13 +8 +1 = **~+67** | as kern_big (+1 fsx_rtick); its own IDT already handles INT 0 after the switch |
| resident, all | **~+140**: adds BR-82 +11, BR-92 +12, BR-93 +14, BR-34 IMR +8, BR-86 clip +2, BR-85 +3, BR-80 +7 (BR-95 +25 and BR-21's rethink +35 on top if taken) | **~+110** | as kern_big |
| transient | .ovl +12 (BR-9), +6 (BR-18), +5 (BR-88) | .ovl +12, .boot2 +6, +5 | as kern_big |
| modules | SERIAL.DRV +20 (BR-87), 0 (BR-90) | - | - |

- **Rows:** serlease, dosmouse, doswmouse, kdmouse, usbmouse, kbd4f, kbdpreempt, kbdburst, int0sweep, stkpanel, stk0water, timerrace, timerbounce, evqfull, wirefleetlink, wirefleetnomouse, ringmove-wirefleet, clipkeep, clipgrow, trkspkrate, vidspk22, vidspkfast (soak); ps2mouse (full); 86Box AT-BIOS twins for BR-19. The measurement BR-21 needs is serlease at divisor 12 with the mouse moving, taken once by whoever decides.

### Batch G - drivers, hibernate and system services

- **Files:** kernel/driver.inc, kernel/snd.inc, kernel/hiber.inc and kernel/diskops.inc (HIBER.DRV, DISKOPS.DRV), kernel/drvvol.inc, kernel/clock.inc, kernel/ctrl.inc (CTRL.DRV), kernel/blank.inc (SAVER.DRV), kernel/kernel.asm (osapi_rand), drivers/sound/sb.inc (SOUND.DRV), apps/timer/timer.asm, apps/dos (the kern_dos date half of BR-26).
- **Items:** core BR-5, BR-10, BR-15, BR-31, BR-32, BR-33, BR-38, BR-39, BR-40, BR-43, BR-44; BR-34's vector scan and SB fixes. Then BR-26, BR-72. Low: BR-96..BR-103, BR-157..BR-160, BR-145 (doc).

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| resident, core | +25 +11(.bss) +12 +5 +0 +25 +2 = **~+80** | **+2** (BR-43; BR-15 is 0) | the .bss +11 and +2 are conventional; the .cold above 1MB |
| resident, all | **~+92** (adds BR-159 +12) | +2 | |
| .ovl (transient) | +29 (BR-44 +5, BR-157 +8, BR-158 +16) | 0 | as kern_big |
| HIBER.DRV | **~+100** (BR-10 +2, BR-38 +20, BR-40 +30, BR-96 +10, BR-97 +10, BR-98 +6, BR-99 +8, BR-100 +6, BR-102 +6, BR-103 +2) | - | batch H adds -9 |
| other modules | DISKOPS.DRV +48 (BR-72 +30, BR-101 +18); CTRL.DRV +40..+60 (BR-26), ~0 (BR-160); SAVER.DRV +15; SOUND.DRV +14 (IMR +6, OCW3 +8) | - | |

- **Rows:** drvmem (fast); usbmouse, heapdrv, hmadrv, hma286, hmapool, sndkeep, sndonce, sndmove, hibernate, hibernatedrv, hibernatem, hmahiber, p2hiber, ems, diskclone, imgcases, wimgtrip, cfgtrip, deskclock, arclock, kdreturnset, saver, vesacp (soak). New: untick a second driver with USBMOUSE up (BR-5), Esc on the Resume face keeps the files (BR-10), a DOS TIME read after a panel Set (BR-26).

### Batch H - kern_mega

- **Files:** kernel/mega.inc, kernel/megados.inc, kernel/megapark.inc, kernel/megares.inc, kerndos/kdmouse.inc, kerndos/kdhma.inc, kerndos/kdpark.inc, kernel/vga12.inc's KERN_MEGA arm (BR-64), kernel/memory.inc's mega regrow arm (BR-106 is in batch D's file but may travel here).
- **Items:** core BR-13, BR-64, BR-67 (0 bytes), BR-69, BR-70, BR-71. Then BR-65 (form (c) now, (a) on the owner's word), BR-66's LDT half, BR-68. Low: BR-144, BR-161..BR-170. BR-66's compaction model is flag day.

| | conventional (MEGA_RESIDENT) | above 1MB (.cold/.mega) | modules / other |
|---|---|---|---|
| core | **+20** .text (BR-64, unless placed in .mega) | +28 (BR-13) +0 (BR-67) = **~+28** | HIBER.DRV -12 (BR-69) -1 (BR-71); HMA kern_dos +8 -1 (BR-70, BR-71); .boot2 +8 (BR-71) |
| all | +20; **+128..+192 .lowbss** only if BR-65's form (a) is chosen | **~+150** (adds BR-66 +25, BR-68 +8, BR-161 +6, BR-162 +16, BR-163 +6, BR-164 +20, BR-165 +20, BR-166 +4, BR-168 +8, BR-169 +5, BR-144 +8) | HIBER.DRV +4 (BR-169) |

- **Rows:** megagrid, megapkg (fast); megahigh (full); megatrap0, megadosp, megadosroom, megahibfull, megap2hiber, megahiber, kdmouse, dosmouse, lzload, win16mega* (soak). These run on QEMU, which checks no segment limit and cannot fail BR-13, BR-64 or BR-65: the new rows belong on Bochs (docs/BOCHS.md) - a lent-stack int 10h probe, a 3-byte-slack canvas, a foreign-stack option-ROM IRQ.

### The batches side by side (core, then all)

| batch | kern_big resident | kern_small resident | kern_mega conventional | transient | modules |
|---|---|---|---|---|---|
| A boot and detection | 0 / 0 | 0 / 0 | 0 / 0 | ~+10 .boot2 each kernel; -1 boothd; +4 .ovl | KERNEL.SYS ~-400 disk |
| B display | ~+43 / ~+105 | ~+23 / ~+33 | as big / as big | .ovl +8, .boot2 +7 | +14 |
| C WM and UI | ~+62 / ~+105 | ~+51 / ~+80 | as big / as big | - | DESK.DRV +6 |
| D loader and heap | ~+81 / ~+150 | ~+86 / ~+110 | .text 0; .cold above 1MB | - | parts packages +20 each |
| E disk and files | ~+67 / ~+185 (+90 BR-60) | ~+68 / ~+120 | .cold above 1MB | .ovl +10 | FDLG/FSCHECK/FORMAT +8/+6/+10 |
| F input, ISR, sched | ~+86 / ~+140 | ~+67 / ~+110 | as big's .text share | .ovl +23, .boot2 +6 (small) | SERIAL.DRV +20 |
| G drivers, hiber, services | ~+80 / ~+92 | +2 / +2 | +13 (.bss and .text) | .ovl +29 | HIBER ~+100, DISKOPS +48, CTRL ~+50, SAVER +15, SOUND +14 |
| H kern_mega | - | - | +20 / +20 (+128..192 if BR-65a) | .boot2 +8 | HIBER -9, HMA kern_dos +7; ~+150 above 1MB |
| **total, core** | **~+420** | **~+300** | | | |
| **total, all** | **~+780** (+90 with BR-60) | **~+455** | | | |

Read the totals against the owner's brief ("not huge byte growths") and the
budgets, not against a rung: about 420 resident bytes on kern_big and about 300
on kern_small buy every high and medium defect in this register. kern_small's
300 is the figure to argue about, since its budget is 64KB resident; batches D
and E are where most of it goes, and both are defects that corrupt data on
exactly that machine (BR-2, BR-7, BR-17). Several items reclaim bytes and can
pay for others inside the same batch: BR-110 (-19), BR-27 (-12), boot-7 (-41
transient), BR-69 (-12 module), and wm-2's form of BR-8.

## Source finding index

Every non-refuted bug and hazard finding, and every other-kind finding used as
a defect, with the item it is merged into.

| source | BR | source | BR | source | BR | source | BR |
|---|---|---|---|---|---|---|---|
| abi-1 | 43 | dw-1 | 23 | input-1 | 18 | mem-1 | 35 |
| abi-2 | 62 | dw-2 | 17 | input-2 | 19 | mem-2 | 12 |
| abi-3 | 62 | dw-3 | 24 | input-3 | 20 | mem-3 | 53 |
| abi-4 | 146 | dw-4 | 22 | input-4 | 87 | mem-4 | 11 |
| abi-5 | 145 | dw-5 | 133 | input-5 | 88 | mem-5 | 105 |
| abi-7 | 73 | dw-6 | 134 | input-6 | 89 | mem-6 | 104 |
| abi-8 | 22 | dw-7 | 135 | input-7 | 90 | mem-7 | 52 |
| boot-1 | 1 | dw-8 | 62 | input-8 | 91 | mem-8 | 54 |
| boot-2 | 1 | dw-11 | 136 | input-9 | 92 | mem-10 | 4 |
| boot-3 | 63 | dw-12 | 60 | input-11 | 93 | mem-19 | 106 |
| boot-4 | 153 | dw-18 | 137 | input-14 | 94 | sched-1 | 51 |
| boot-5 | 25 | dw-19 | 143 | isr-2 | 36 | sched-2 | 79 |
| boot-6 | 25 | engsys-1 | 74 | isr-3 | 85 | sched-3 | 80 |
| boot-11 | 155 | engsys-2 | 75 | isr-4 | 86 | sched-4 | 78 |
| boot-12 | 156 | engsys-8 | 76 | isr-6 | 34 | sched-5 | 78 |
| boot-15 | 156 | engsys-12 | 171 | isr-7 | 21 | sched-6 | 81 |
| boot-18 | 154 | engsys-15 | 172 | isr-8 | 9 | sched-7 | 82 |
| dr-1 | 25 | files-1 | 2 | isr-9 | 82 | sched-8 | 83 |
| dr-2 | 37 | files-2 | 27 | isr-17 | 34 | sched-9 | 84 |
| dr-3 | 61 | files-3 | 41 | loader-1 | 7 | sched-18 | 80 |
| dr-4 | 16 | files-4 | 11 | loader-2 | 7 | shell-1 | 42 |
| dr-7 | 138 | files-6 | 123 | loader-3 | 7 | shell-6 | 117 |
| dr-8 | 139 | files-7 | 124 | loader-4 | 29 | shell-7 | 119 |
| dr-9 | 140 | files-8 | 125 | loader-5 | 30 | shell-11 | 118 |
| dr-12 | 61 | files-9 | 126 | loader-7 | 120 | shell-12 | 119 |
| dr-13 | 25 | files-10 | 127 | loader-8 | 11 | sysmisc-1 | 15 |
| dr-14 | 141 | files-12 | 128 | loader-9 | 121 | sysmisc-2 | 44 |
| dr-15 | 142 | files-13 | 129 | loader-10 | 122 | sysmisc-3 | 26 |
| dr-17 | 142 | files-14 | 130 | mc-1 | 13 | sysmisc-4 | 157 |
| drivers-1 | 5 | gfx-1 | 3 | mc-2 | 65 | sysmisc-5 | 158 |
| drivers-2 | 31 | gfx-2 | 64 | mc-3 | 161 | sysmisc-6 | 159 |
| drivers-3 | 32 | gfx-5 | 77 | mc-4 | 66 | sysmisc-7 | 160 |
| drivers-4 | 33 | gfx-7 | 152 | mc-5 | 66 | text-1 | 46 |
| drivers-6 | 34 | hiber-1 | 96 | mc-6 | 162 | text-3 | 77 |
| drivers-7 | 39 | hiber-2 | 38 | mc-7 | 163 | text-4 | 131 |
| drivers-8 | 144 | hiber-3 | 39 | mc-8 | 164 | text-5 | 132 |
| histsys-1 | 4 | hiber-4 | 40 | mc-9 | 67 | text-11 | 129 |
| histsys-2 | 9 | hiber-5 | 97 | mc-10 | 68 | ui-1 | 45 |
| histsys-6 | 95 | hiber-6 | 98 | mc-12 | 165 | ui-2 | 28 |
| histsys-11 | 8 | hiber-7 | 10 | mc-13 | 165 | ui-3 | 110 |
| histsys-15 | 78 | hiber-8 | 99 | mc-14 | 165 | ui-4 | 111 |
| hm-1 | 12 | hiber-9 | 100 | mc-15 | 166 | ui-5 | 112 |
| hm-2 | 104 | hiber-10 | 101 | mc-17 | 167 | ui-6 | 113 |
| hm-3 | 4 | hiber-11 | 101 | mc-18 | 167 | ui-7 | 86 |
| hm-4 | 35 | hiber-12 | 72 | md-1 | 69 | ui-8 | 22 |
| hm-5 | 54 | hiber-16 | 102 | md-2 | 70 | ui-9 | 115 |
| hm-6 | 105 | hiber-18 | 103 | md-3 | 71 | ui-16 | 114 |
| hm-7 | 59 | hm-8 | 53 | md-4 | 168 | ui-17 | 116 |
| hm-13 | 11 | hm-14 | 107 | md-9 | 169 | video-1 | 14 |
| md-10 | 71 | md-11 | 169 | md-12 | 170 | video-2 | 6 |
| md-16 | 105 | md-17 | 170 | md-18 | 170 | video-3 | 55 |
| video-4 | 56 | video-5 | 57 | video-6 | 58 | video-8 | 147 |
| video-9 | 148 | video-10 | 149 | video-11 | 150 | video-12 | 151 |
| wm-1 | 8 | wm-3 | 47 | wm-4 | 48 | wm-5 | 49 |
| wm-6 | 50 | wm-7 | 109 | wm-8 | 78 | wm-9 | 108 |
| wm-13 | 8 | | | | | | |

Context, not defects, cited above: boot-7 (the room BR-1 and BR-25 need),
boot-10 (BR-1's rethink), wm-2 (BR-8's alternative), wm-17 (BR-47's rethink),
isr-13 (BR-34's flag-day cell), sysmisc-14 (BR-26's rethink R1), mem-14
(BR-54's rethink). Refuted and dropped: isr-5, wm-20.
