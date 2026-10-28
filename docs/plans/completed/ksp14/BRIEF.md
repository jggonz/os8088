# Kernel size pass 14: the brief every agent reads first

Branch `kernel-size-14`, cut from `elendilon` at `26a63034`. The pass before
this one was **pass 13** (its record is
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md`, named for the pass it hands
to). Each agent works in a git worktree of its own, created by the coordinator
at `/home/user/wt/<name>` on a branch `ksp14-<name>` cut from the commit that
adds this file, and writes its record to
`docs/plans/completed/ksp14/<name>.md`. The coordinator merges the branches;
**agents do not push**.

Read before writing any code, in this order:

1. this file;
2. `docs/plans/completed/HANDOFF-KERNEL-SIZE.md` sections 3-6 - pass 1's
   method and the mistakes that cost it time (the authority on method);
3. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P14.md` - pass 13's record,
   especially 1.1 (what paid, and how), 3 (what the owner decided - the
   DECLINED list is binding), 6 (what is left) and 8 (method lessons);
4. **the REFUSED lists in `docs/plans/completed/ksp13/*.md` and
   `docs/plans/completed/ksp12/*.md`** before proposing anything in a file
   those agents touched - a refused item is not re-proposed unless the reason
   it was refused no longer holds, and then the note says what changed;
5. CLAUDE.md's Hard rules (8086 only, near model, SS != DS, register
   discipline, section discipline, label hygiene) and its banner: **bytes,
   not rungs**.

## 0. The brief

The owner's words: *"This kernel size pass will attempt to recover 1200+
bytes. ... There should be about 2kb of kernel code not size passed (bit more
or less, main's added items are in scope). That will be the first target of
this pass. Other opportunities seen while doing those items ARE in scope.
Following that we will do a more general size pass to reach the goal."* And:
*one concept per agent*, two agents at a time.

**The goal is 1,200+ bytes off RESIDENT on kern_big.** Resident = `.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf`, read with `tools/kernsize.py
--json`. kern_small, kern_emu and kern_mega count too and are reported.

Pass 13's standing rules carry over unchanged: keep hot paths fast; **if it
changes functionality, it goes to the owner first** (section 1).

**Base** (`26a63034`, assembled):

| | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 44,005 | 5,375 | 38,140 | 5,486 | 336 | **93,342** | 5,926 | 5,118 | 99,328 |
| kern_small | 31,269 | 3,016 | 22,054 | 2,868 | 0 | **59,207** | 4,381 | 1,486 | 61,440 |
| kern_emu | 44,274 | 5,375 | 38,140 | 5,486 | 336 | **93,611** | 6,026 | 5,118 | 99,328 |
| kern_mega | 47,413 | 5,411 | 42,748 | 7,257 | 336 | **103,165** (+ `.mega` 14,698) | 6,282 | 5,117 | - |

### 0.1 What has not had a pass

Pass 13 closed at the tree that `571befce` squashed onto `main` (#268), and
`571befce` IS an ancestor of the base. Since then kern_big resident has grown
**+2,205** (`.text` +1,289, `.cold` +639, `.bss` +149, `.lowbss` +128);
kern_small +280 (`.text` +137, `.cold` +105, `.bss` +38). Measured routine by
routine with `tools/os88rdiff.py kernel 571befce .`, by concept, kern_big:

| agent | concept | commits | kern_big resident (net) | the routines |
|---|---|---|---:|---|
| **vesa** | the VESA display's resident half: `kernel/vidext.inc`'s `VIDEXT` hook and every hook site it put in the drawing layer (SPEC.md 120, docs/plans/VESA-PLAN.md) | `322dda62`, re-applied in `553f2a21` (its message prices it: *"+600 .text, +146 .cold, +128 .lowbss; built NOVIDEXT=1 it is .text +12, .cold +9"*) | **~+870** | `vx_*` (`vx_boot_x` 120, `vx_blk` 54, `vx_call` 36, `vx_go` 29, `vx_callf1` 25, `vx_goc` 20, ...), `gfx_b4piece` 74, `gfx_blit4` +36, `gfx_spans` +24, `gfx_blitd` 17 and `OSAPI_GFX_BLITD`, `gfx_blit1_x` +16, `gfx_scroll` +14, `gfx_b4walk`, `gfx_walk`, the `+12`s on `vga_solid_rect`/`gfx_fill_*_raw`/`vga_save_vram`/`vga_restore_vram`/`cur_put_1`/`cur_get_1`/`cur_move_1`, `font_run_x` +28, `font_char` +14, `vid_depth_set` +31, `vid_tab` +18, `vid_setmode` +12, `vid_disp_planes` +10, `wm_display` +10, `ico_core` +12, `fsx_surf`/`fsx_caps` +22, `vesa_row`/`drv_f_vesa`/`drv_t_vesa` 38, **`mou_pstack` 128 -> 256 (`.lowbss` +128)** |
| **menubar** | the menu bar's speaker and volume, the file-progress rework and toast placement | `553f2a21`'s rework (*"speaker right of the clock, a toast that misses the gap covers it, fpg_hide"*), `cd38ec7d` (#296), `35cac28e` (#300) | **~+430** (gross much larger) | `menu_bt_go` 298, `menu_sound` 161, `menu_ckend` 29, `menu_draw_clock` -270, `menu_bar_text` -218, `menu_bcell` `.bss` +23, `ui_volume_service` 107, `ui_task` +58, `fpg_hide` 57, `fpg_show` 44, `fpg_bare` 21, `fpg_gbare` 20, `fpg_vis` 12, `fpg_busy` +14, `toast_up` 18, `toast_place` -37 |
| **serial** | the serial lease, WireFleet's link, and the mouse contest | `2edd2c29` (#278), `901516f2` (#282), `60c46f08`, `f9ed8cc4`, `61b4054d` | **~+260** | `serial.inc` +126 (`ser_api_x` 58, `osapi_serial` 31, `ser_release_inst` 23, `ser_drop`), `mod_f_serial`, `SERFP` `.bss` 12, `mou_claim` +18, `mou_isr_body` +28, `mou_byte` +13, `mou_p2_byte` +16, `mou_apply` +12, `mou_errt`, the other `mou_*` +6s - **the mouse.inc hunks that are NOT vesa's** |
| **files** | the file side: the Disk window's mouse wheel and folder scan/goto, image mounts, CD-ROM drive icons on the desk, links' argument text | `eea2d24a` (#277), `cc4db7cf` (#297), `9cbbb93b`'s `dskw_imgro`/`dsk_img_next`, `553f2a21`'s `lnk_open`/`osapi_arg_text_x` | **~+250** net | `dsk_dscan` 151, `dsk_goto` 84 (with `fcp_scan` -124, `fcp_rdnext` -109, `fcp_goto` -84 gone, `dsk_find_x` -94), `dsk_img_next` 33, `dsk_img_orphan`, `dskw_imgro` 36, `fm_onwheel_x` 32, `mou_whl_add`, `fm_fmt_reach` +13, `desk_drvico` 41, `desk_caprect` +32, `assoc_run_x` +66, `lnk_open` +33, `osapi_arg_text_x` 12, `drv_f_cd`/`drv_t_cd` |
| **api** | the API table and driver-table additions | `8a509992` (#287, `OSAPI_DRV_USE`), `9cbbb93b`'s kern_big cells, `553f2a21`'s `OSAPI_IMG_NAME` | **~+400** | the API table (~36 new cells: `apic_thm_desk` reads +74 because the table follows it), `osapi_img_name` 88, `osapi_drv_use_x` 82, `drv_cp_closed` 49 (`drv_cp_closed_x` -45), `apic_drv_call_at` 25, `api_gfx_blit4` 20, `osapi_xmem_walk` 24, `osapi_xmem_sel`, `osapi_mem_codesel`, `mmf_osapi_mem_huge`, `drv_f_http`/`drv_t_http` 22, `usbm_row` +32, `drv_svc` `.bss` +72, `wm_wins` `.bss` +24, `mod_fp` `.bss` +4, `hb_ok_x` +10 |

Agents run **two at a time**: vesa and menubar first, then serial and files,
then api; then the general pass, briefed when the concept agents are in.
**A concept agent owns the hunks its commits added** (`git diff 571befce HEAD
-- <file>` shows every hunk; `git log -p <commit> -- <file>` says whose)
**plus anything it finds inside the routines those hunks touched.** The table
is a measurement and the attribution is the coordinator's reading of it - if a
routine is in the wrong row, say so in your note and work it if nobody else is
in that file now.

**"Other opportunities seen while doing those items ARE in scope."** Code you
read on the way - a neighbour of your routine, an older routine your hunk
calls, a second copy of what you are folding - is yours to take if the agent
running beside you does not own it. Record each such item under TAKEN with a
line saying it is outside the concept. Anything you see and do not take goes
in *CROSS-FILE* for the general pass, with its bytes.

### 0.2 Where the bytes have come from before

Pass 13's concept agents found 11-37% of what their concepts added; its
whole-kernel agents found most of the goal, with **structural** moves. Look
for structure in your concept too:

* **Where code lives, not just how long it is.** A byte moved out of
  resident is a byte saved. The levers, in order of how often they have
  paid: (1) code only ever called from a MODULE (`CTRL.DRV`, `DISKOPS.DRV`,
  `FORMAT.DRV`, `HIBER.DRV`, `DOCK.DRV`, `EXTD.DRV`, `DESK.DRV`, `WIN95.DRV`,
  `STIKMENU.DRV`, the `.mods` module, and on kern_small `FILECP.DRV`/
  `FDLG.DRV`) can move INTO that module (SPEC.md 2.8;
  `tools/os88ovlchk.py` says what may name what) - or, as pass 13 did with
  `DRV_ATTACH`, be written once as a macro and expanded where its only
  callers are; (2) code only run during boot can move into `.ovl` or the blob
  (`.boot2`), which are given back (`.cold` is RESIDENT - CLAUDE.md says
  why); (3) data only a module reads can move into the module. **VESA is the
  sharpest case**: everything a VESA desktop needs that a machine WITHOUT
  `VESA.DRV` never runs is a candidate to live in the driver and be reached
  through the hook - measure what the hook costs a non-VESA machine.
  **A move that makes a feature need the system disk where it did not before
  is a FUNCTIONALITY change**: record it as a proposal (*WANTS THE OWNER*) -
  do not take it.
* **Two routines that are one** across files; a family of near-identical
  routines that wants a table (or a table that wants a compare, on
  kern_small); a shared epilogue reached by a near `jmp`; a register
  convention that makes every caller set up the same thing.
* **Data**: tables a computation could replace, strings that share a tail,
  buffers whose lifetimes never overlap (prove it), a resident copy of
  something a module or the blob could rebuild. **A `.lowbss`/`.bss` size
  was set for a reason: find the reason before you shrink it** (`mou_pstack`
  doubled for VESA - measure the water, `tools/stkwater.py`, before touching
  it, and a stack is the last thing to trim on an estimate).
* **The shapes that paid last time** (P14 handoff 1.1): single-caller routines
  written at the call; a `cw_*` shim replaced by the API cell that already
  publishes the routine; module scratch in the module's own nobits section;
  near-identical bodies folded; a register held across a call so a store
  goes; code assembled on a kernel arm where its guard can never pass;
  relaxed jumps.

## 1. The rules this pass

* **Functionality**: an item that changes what the user sees or can do - a
  message reworded or dropped, a feature that needs the system disk where it
  did not, a limit lowered, a behaviour in an edge case - is NOT taken: it
  goes in the note's *WANTS THE OWNER* with its bytes, and the coordinator
  puts it to the owner. Byte-identical behaviour is the bar, including on the
  refusal paths. **Pass 13's DECLINED list (P14 handoff section 3) is not
  re-proposed.**
* **ABI**: `main` is `origin/main` at `e631e0ab`. An interface is FROZEN if it
  is in `main`'s `apps/os88api.inc`, `drivers/os88drv.inc`, `apps/cc/os88.h`,
  a cell offset, a module's or driver's header, a file format or a
  `SYSTEM.CFG` key. `git diff origin/main HEAD -- apps/os88api.inc
  drivers/os88drv.inc apps/cc/os88.h` is the list of what is NOT frozen
  (`OSAPI_IMG_NAME` at 0x04D7 and `OSAPI_ARG_TEXT` at 0x04DD are among it).
  Those may change freely; keep the SDK, the drivers and the packages in step;
  `tools/abilock.py` records it (SPEC.md 20.2.0.1). A frozen one that would
  pay to break goes in *WANTS AN ABI BREAK*, with bytes and callers.
* Kernel-internal interfaces (`_x` routines, `.bss` layout, module entry
  tables only the kernel and its modules share) are not ABI - but a
  module/kernel interface is checked by `os88mod.py`, so change both halves.
  **A driver's resident hook (VIDEXT, the serial lease) is kernel <-> driver:
  `drivers/vesa/` and `drivers/os88drv.inc` change with it, and the driver's
  `DRV_VER` rule applies** (CLAUDE.md, *An ABI change is a written decision*).
* **Hot paths are off limits for anything that costs cycles**: per-pixel
  loops, the mouse ISRs, IRQ0/`sch_isr`, `sch_switch`, the API cell,
  `font_run`, `gfx_blit4`/`gfx_blit1`'s per-run loops, `dsk_xfer`'s
  per-sector path, `ui_task`'s idle pass. A change there must be
  cycle-neutral or faster, with the count in the note. **Several VESA hooks
  sit at the head of hot routines** (`gfx_blit4`, `font_char`, `font_run_x`,
  `cur_*_1`): a non-VESA machine's cost through the hook is the number that
  matters there, and it must not grow. **Parity is a GATE now**: the fast row
  `hotpar` (`tests/unit/t_hotpar.py`) fails `make` when a hot routine starts
  at a different address parity than it was measured at - pad it back (pass
  12/13 did it for +2 bytes) and say so.
* **`.boot2`** may grow by a sector if a move into it pays; the blob is near
  full (P14 handoff 6), so say so in the note with `BOOT2_SECS` before and
  after, and re-check `KSIG_OFF`'s band (`tests/unit/t_canary.py` is in the
  fast tier).
* **kern_mega must still build** (`make mega`): code under `%ifdef KERN_MEGA`
  is not this pass's target, but a kern_big change it shares must not break
  it. Say in the note what kern_mega's resident did.
* **Ownership**: two agents run at once, in different concepts. Do not edit a
  hunk the other agent's concept added (the table above). Anything outside
  your ownership goes in your note's *CROSS-FILE* list with its bytes.

## 2. Method (passes 11-13, which worked)

* **First command**: `cd /home/user/wt/<name> && git status` - you are on
  `ksp14-<name>`; if you are not, stop and say so. **Never `cd` into
  `/home/user/os8088` to build or edit**: that is the coordinator's tree.
* **Scratch**: your OWN directory, `<scratchpad>/<name>/` - the scratchpad is
  shared by every agent and pass 13 lost files to it. **Never `git stash`**:
  it is ONE stack for every worktree of the repository. `git show <base>:path`,
  `os88rdiff`, or a private `git worktree add` of the base are the safe ways
  to compare.
* **Measure base and tip yourself**, absolute figures from
  `tools/kernsize.py --json` on all four kernels: kern_big (`make`),
  kern_small (`make small`, then `--build build/smallk --ico build/smallk
  -DKERN_SMALL`), kern_emu (`make emu`, `--build build/emuk --ico build/emuk
  -DKERN_BIG -DKERN_EMU`), kern_mega (`make mega` prints its own
  `kernsize[mega]` lines). Quote **assembled** bytes, never estimates.
  `kernsize`'s printed deltas are against the blessed baseline, not your
  base - use the absolutes. Module and driver images: the UNPACKED image
  (`os88drv.image_unwrap`), and say whether a saving fell into a pad or moved
  a claim's KB.
* `tools/os88rdiff.py kernel <base> .` (add `-D KERN_SMALL` for kern_small)
  is how you see every routine you changed, what relaxed, and what shifted.
  `tools/os88rdiff.py kernel 571befce <base>` is how you see your concept.
* **Commit in small batches**, each gated by `make -j2`, `make -j2 small` and
  the fast tier (`python3 tools/os88test.py fast`, which `make` already runs),
  plus `make emu` and `make mega` at your tip. `tools/stkbalance.py` over every
  file you touched, base against tip: no new unbalanced path. Run
  `python3 tools/checkdocs.py` AFTER `git add`.
* **Equivalence**: where no row reaches a path, prove it another way - a
  scratch unicorn harness (pass 13 ran several against the base kernel's own
  bytes), a model, a reading recorded in the note. "It assembled" is not
  equivalence. **A reference walk misses fall-through entries**: verify every
  moved entry by READING.
* **Rows**: name the `tests/suite.py` rows covering what you changed (grep
  it). You may run THE ROWS FOR YOUR OWN SUBJECTS, scoped with `-k`, ONCE,
  after your last code commit (`python3 tools/os88test.py soak -k '<row>' -k
  ... --marty-jobs 1`) - one pass, not one per batch, and a row is a row
  however it is started (CLAUDE.md's Testing section). **NEVER the whole soak
  tier**, never `test-full` (the coordinator runs it once at the close). Do
  not run a row while you are editing kernel sources in the same tree. Break
  the thing you changed on purpose once where a row is new to you, and watch
  it go red. A row that fails: re-run it alone on the BASE before blaming
  yourself (`python3 tools/os88bisect.py classify <row>`) - pass 13 lost a day
  to two rows that carried stale mirrors of kernel constants.
* **VESA** has no MartyPC machine: `docs/plans/VESA-PLAN.md` and the `vesa*`
  rows in `tests/suite.py` say which emulator its rows use.

## 3. The environment

* 4 cores, 15 GB, shared by two agents. Use `make -j2`. At most ONE MartyPC
  instance of your own at a time (`--marty-jobs 1`).
* `make deps` has run. MartyPC is built ONCE in the main tree: in your
  worktree run `mkdir -p build && ln -sfn /home/user/os8088/build/martypc
  build/martypc` before your first emulator row. **There is no IBM ROM in
  this container**: rows go through the GLaBIOS twins (`os88ui.boot()`
  substitutes them), and a row that needs the IBM ROM itself SKIPs - say so.
  `mtools` is not installed by `make deps`; `apt-get install -y mtools` if a
  row you need (`stiknodesk`, `deskmodal`) would otherwise skip silently.
* A fresh worktree has no `build/`: the first `make` builds everything
  (a few minutes - start it first).

## 4. The note

`docs/plans/completed/ksp14/<name>.md`, pass 13's shape
(`docs/plans/completed/ksp13/wkgui.md` and `theme.md` are good models): BASE
AND TIP (a table, all four kernels and any image), TAKEN (each item with its
bytes per kernel and its cycles if on a hot path; mark items outside the
concept), REFUSED (with the bytes it would have been and why), WANTS THE
OWNER (functionality changes, with bytes), WANTS AN ABI BREAK, CROSS-FILE,
ROWS RUN (with results), DEFECTS FOUND (fixed or not, and how found). After
adding the note run `python3 tools/os88index.py` (docs/INDEX.md is generated)
and commit both. Commit messages end in `(kernel size pass 14)` and carry the
attribution trailer the session gives you. Never a model name in a commit or
a file.

**Your final message to the coordinator is ONE message** (pass 13's agents
flooded theirs row by row): the branch tip, the resident delta on each
kernel, the WANTS THE OWNER items, the rows you ran with results, and
anything that blocked you.
