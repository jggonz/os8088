# Kernel size pass 16: the brief every agent reads first

Branch `kernel-size-16`, cut from `elendilon` at `23791571`. The pass before
this one was **pass 15**; its record is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P16.md` (named for the pass it hands
to) and its brief and agent notes are `docs/plans/completed/ksp15/`.

Each agent works in a git worktree of its own, created by the coordinator at
`/home/user/wt/<name>` on a branch `ksp16-<name>` cut from the commit that
adds this file, and writes its record to `docs/plans/completed/ksp16/<name>.md`.
The coordinator merges the branches; **agents do not push**.

Read before writing any code, in this order:

1. this file;
2. **`docs/plans/completed/ksp14/BRIEF.md` sections 0.2, 1, 2, 3 and 4** -
   the rules, the method, the environment and the note's shape all carry over
   UNCHANGED except where this file says otherwise. Substitute `ksp16` for
   `ksp14` and `(kernel size pass 16)` for the commit suffix everywhere;
3. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 (pass 1's
   method and the mistakes that cost it time);
4. **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P16.md` in full** - pass 15's
   record: section 3's DECLINED list is BINDING (the SOUND.DRV run reorder,
   dropping LZB, `kent_di` on the damage-repaint path), section 6 is what is
   left and section 8 its method lessons;
5. **the REFUSED and WANTS THE OWNER lists in `docs/plans/completed/ksp15/*.md`
   and `ksp14/*.md`** for any file you touch - a refused item is not
   re-proposed unless the reason no longer holds, and then the note says what
   changed;
6. CLAUDE.md's Hard rules and its banner: **bytes, not rungs**.

## 0. The brief

The owner's words: *"The goal of this size pass is to cover the kernel work
since the last size pass, and then, with that and/or general work, recover
(with some spare for bugfixes) the gained kern-big size, ending neutral or
better than main."* Five agents, all at once.

**Where kern_big stands.** `main` (`origin/main` at `7b4b441d`, which carries
pass 15 squashed) against the base, kern_big, assembled:

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| main | 41,134 | 5,398 | 37,690 | 5,358 | 336 | 2,032 | **91,948** | 6,447 | 5,110 | 97,280 |
| base | 41,289 | 5,418 | 37,703 | 5,358 | 336 | 2,032 | **92,136** | 6,456 | 5,119 | 97,792 |

So the base is **188 bytes over main**, and that crossed one `KERN_SIZE`
rung.

**The goal: kern_big resident at least 512 bytes under the base** (`.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`, `tools/kernsize.py
--json`) - main's figure plus the "spare for bugfixes" the owner asked for.
**The floor is neutral with main** (188 off the base); anything less is a
miss and the note says so. kern_small, kern_emu and kern_mega count too and
are reported; none of them may grow. kern_mega has had two passes of its own
(`docs/plans/completed/msp1/`, `msp2/`) - do not undo either, and a
`%ifdef KERN_MEGA` arm that a change of yours touches is measured on
`make mega` before and after.

Pass 13's standing rules carry over unchanged: keep hot paths fast; **if it
changes functionality, it goes to the owner first**.

### 0.1 What has not had a pass

Pass 15 landed in `39b74780`. `tools/os88rdiff.py kernel origin/main HEAD`
at the base, kern_big, by the concept that added it (the routines named are
the largest movers, not all of them; `git log -p 39b74780..HEAD -- <file>`
says whose a hunk is):

| concept | commits | the routines |
|---|---|---|
| **the heap granule and `OSAPI_MEM_P`** (SPEC.md 50.3.7) | `3d349548`, `327c7492`, `9d03e89b`, `aafe18bc`, `46878357`, `f4e195ee`, `11e3ec29`, `ae6764ce`, `587c0cb9`, `8c58f9af` | `mem_regrowp` 151 (new; `mem_regrow` 141 -> 3), `mem_avail_lvl_p` 123 (new; `mem_avail_lvl_x` 129 -> 12), `osapi_mem_p` 81, `mem_owned_kba` 26, `mem_claim_x` +23, `mem_rr_tab` -16, `mem_bytes_kb` -15 / `mem_rr_lone_end`, `dsk_fatw_scan` 18 (`dsk_swapent`, `dsk_fatw_pinned` gone), `drv_release` +26, `drv_svc_clear` gone |
| **queued toasts, one progress scale per operation, `OSAPI_FS_OP`** (SPEC.md 12.8.8-12.8.10, 59.11) | `1cee9991`, `cfcc380d`, `dde803bc`, `e828aa61`, `27c8ae52`, `c6f086c9`, `d89d9a2f`, `ce8ff9d2`, `45c668d7`, `41f31be6` | `toast_show` +51, `toast_promote` 54, `toast_owner_gone` +22, `fpg_begin` +41, `fpg_opbegin` 37, `fpg_finish` +23, `dsk_fpgdata` 12, `osapi_fs_op` 66, the counting in `dsk_xfer` +38, `fcp_xfer` +25, `ld_start` +23, `dskw_read_at_x` +12, `ui_grow`, `cmz_*` (CTRL.DRV) |
| **the review fixes and the real-mode fsx bracket** | `8c58f9af`, `1dea2435`, `75442582`, `c6f805ea` | `fsx.inc`, `memory.inc`'s refusals, `mem_regrow`'s 1024 KB check |

Most of the gross movement is `.cold` and `.text` reshuffling between
existing routines; the net is the 188 above. `.cfx` grew 353 -> 494 and
the modules (`.modh` +504, `.modu` +323, `.modw` +126, `.modl` +96) are not
resident and are not this pass's target, but a module that a change of yours
fills past its claim is a regression (`tools/os88modcost.py`).

## 1. The five agents and what each owns

| agent | owns | the work |
|---|---|---|
| **heap** | `kernel/memory.inc` (all of it, its `%ifdef KERN_MEGA` arms included), `kernel/mod.inc`, the heap-claim lines of `kernel/driver.inc` (`drv_release`, `drv_svc_*`, the paragraph claims of `587c0cb9`), `dsk_fatw_*` in `disk.inc` | First the heap row of 0.1: `mem_regrowp`/`mem_regrow`, `mem_avail_lvl_p`/`mem_avail_lvl_x`, `osapi_mem_p` and the KB doors - the KB and paragraph doors are two front ends on one body now and the seams between them are where the bytes are. Then a general finder over `memory.inc` and `mod.inc`. `tests/unit/t_pins.py`, `heap*`, `reg*`, `drvmove`, `sndmove`, `pgrank`, `small128` and the `mem*` rows are this agent's. |
| **ops** | `kernel/toast.inc` (or wherever `toast_*` lives), `fprog` (`fpg_*`), `osapi_fs_op`, and **the counting hunks** the toast/progress commits put into other files (`dsk_xfer`, `fcp_xfer`, `ld_start`, `dskw_read_at_x`, `ui_grow`, CTRL.DRV's `cmz_*`), plus `kernel/fsx.inc` | The toast/progress row of 0.1, then the fsx bracket's resident half, then a finder over those files. The counting hunks are yours; the routines around them are fsys's - coordinate in your note rather than both editing one routine. |
| **gui** | a general finder over the window/drawing side: `wm.inc`, `ui.inc` (not `ui_grow`'s counting hunk), `menu.inc`, `mouse.inc`, `cursor.inc`, `font.inc`, `vga12.inc`, `gfx*.inc`, `theme`/`win95` resident halves, `sched.inc`, `instance.inc`, `clip.inc` | Pass 15 gui.md's shapes, file by file, largest routines first, on what pass 15 did NOT read. Hot paths are cycle-neutral or faster with the count in the note (`hotpar` gates parity); the damage-repaint path keeps its speed (DECLINED 3). |
| **fsys** | a general finder over the file side: `disk.inc` (not `dsk_fatw_*` or the counting hunk), `diskw.inc`, `files.inc`, `fdlg.inc`, `filecp.inc`, `loader.inc`, `driver.inc` (not heap's or place's lines), `lz.inc`, `compress.inc`, `hiber.inc`'s resident half, `serial.inc`, `kbd*.inc`, `clock`/`rtc` code, `assoc.inc`, `icons.inc` | The same over these files, on what pass 15 fsys.md did NOT read. |
| **place** | where code lives: `boot/boot2.asm`'s kernel blob blocks, the `.cfx` section, `.ovl`/`.ovlw`, `extmod.inc`, `desksc.inc`, `desk.inc`, `ctrl.inc`, `drv_boot_x`/`ovc_*` of `driver.inc` | **Pass 15's section 6 says room is gone in three places** - the blob, the boot overlay and CTRL.DRV - so this agent MAKES ROOM FIRST (a size finder over the blob's, `.ovl`'s and CTRL.DRV's own bodies, which are not resident but are the room a resident move needs), then moves resident code whose only callers are the boot, the overlay or a module out of resident (`tools/os88ovlchk.py`, the reference walk, `.cfx` for boot-only bodies - and READ for fall-through entries). `BOOT2_SECS` before and after, `KSIG_OFF`'s band (`t_canary`), `t_blobruns` and `DSK_OVLPAD` in the note. |

A file not in any row is free to whoever reaches it first: SAY SO in your
note's TAKEN line, and do not edit a routine another agent's row names.
**The parity-pad pairs are the coordinator's, at the close** - do not take
them; DO list any new pad your change adds.

## 2. What is different from pass 15's rules

* **Five agents at once on 4 cores and 15 GB**, as pass 15 (which worked):
  `make -j1`, never two builds in your own tree at once, at most ONE MartyPC
  instance of your own. Run your rows ONCE, after your last code commit; a
  row that fails on a TIMEOUT is re-run alone before it is believed.
* **ABI**: `main` is `origin/main` at `7b4b441d`. `git diff origin/main
  HEAD -- apps/os88api.inc drivers/os88drv.inc apps/cc/os88.h` is the list of
  what is NOT frozen; everything else in those files is. `OSAPI_MEM_P` and
  `OSAPI_FS_OP` are in that diff, so their CONTRACTS may still be tightened -
  but say so in the note as a WANTS THE OWNER item rather than doing it.
* Commit messages end in `(kernel size pass 16)`. Each agent's note goes in
  `docs/plans/completed/ksp16/<name>.md`; run `python3 tools/os88index.py`
  after adding it.
* MartyPC: `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` in your worktree before your first emulator row. If
  `/home/user/os8088/build/martypc` is not built, ask the coordinator rather
  than building a second copy.
* `disk.inc`'s `align 2` after `dsk_img16` swallows any odd `.text` delta
  above it (pass 15 section 6): measure with `kernsize --json` AND the
  routine span, and say which.
