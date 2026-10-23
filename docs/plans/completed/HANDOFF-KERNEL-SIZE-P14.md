# Kernel size pass 13: the record, and what is left for a fourteenth

**PASS 13 HAS LANDED** on `kernel-size-13`, cut from `elendilon` at
`4833dfef`. The file is named `-P14` for the reason the records before it are
named one ahead: each pass's record is named for the pass it hands to. Its
companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P13.md`**: pass 12's record.
* **`docs/plans/completed/ksp13/`**: the brief every agent read (`BRIEF.md`)
  and the nine agents' own notes, one file each: every item TAKEN with its
  bytes on each kernel, every item REFUSED with the bytes it would have been
  and why, what each one put to the owner, and the cross-file lists. **Read
  the refused lists before re-proposing anything.**
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand, blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The owner's brief: *"This will be a whole kernel pass, anything is a valid
target; but there is a set of recent unpassed work, and each concept should
get an agent to work through it. Most of the 'that looks big' pieces of the
kernel have had multiple passes already; innovative approaches, combinations,
refactors may be needed. Keep hot paths fast, and if it is going to change
functionality run it by me first. .boot2 can grow a sector or two if it needs
to."* **Today's goal: 1 KB off resident.**

The unpassed work, measured routine by routine against pass 12's close (the
tree `3bb038f3`; the history was re-cut at the #264 squash, so it is not an
ancestor): about **+1,260 resident bytes on kern_big** across the ASSOC.DAT
writer, the EGA Color themes and Customize, the desk layer and Sticky Apps,
`*.LNK` links and autostart, and a tail of smaller work - offset by the 1,323
that Timer and Bounce took with them when they became packages. kern_small
had moved by +45. kern_big's cold rung had been crossed again.

**The outcome**, resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`, `tools/kernsize.py --json`, assembled:

| | base `4833dfef` | close | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,757 | **90,936** | **-1,821** |
| kern_big `.text` / `.bss` / `.cold` | 43,756 / 5,223 / 38,084 | 42,630 / 5,218 / 37,394 | -1,126 / -5 / -690 |
| kern_big `KERN_SIZE` | 98,816 | **97,280** | **-1,536** (three rungs: the cold one the unpassed work crossed, and two image ones) |
| kern_big `.boot2` (kernsize's figure) / real blob | 5,129 / - | 5,896 / 6,110 of 6,144 | `BOOT2_SECS` 11 -> **12** (dispsw; no extra `int 13h` on any geometry) |
| **kern_small resident** | 60,260 | **58,912** | **-1,348** |
| kern_small `.text` / `.bss` / `.cold` | 31,847 / 3,063 / 22,482 | 31,124 / 2,978 / 21,942 | -723 / -85 / -540 |
| kern_small `KERN_SIZE` | 62,464 | **60,928** | **-1,536** (three rungs) |
| **kern_emu resident** | 93,016 | **91,193** | **-1,823** |
| kern_emu `KERN_SIZE` | 99,328 | 97,280 | -2,048 (`BOOT2_SECS` 13: it carries its extra knob sector) |

The goal was met on all three kernels - by the nine agents (-1,215 on
kern_big) before the owner approved the last two moves, which took it to
-1,821. Every merge reproduced the sum of the
branches to within four bytes on either side (branch relaxation re-converging
on the merged layout).

## 1. WHO TOOK WHAT

Five concept agents and three whole-kernel agents, each in a worktree of its
own at `aedadd25` (the brief); then `xfile` on the merged tree (`a7f9ac2e`)
for the cross-file lists; then the coordinator's one owner-approved item;
then `dispsw` on the merged tree (`9210f7e4`) for the two moves the owner
approved after xfile priced them.

| agent | subject | big | small | images |
|---|---|---:|---:|---|
| **dispsw** | Set Primary and the check-box/radio glyph into `CTRL.DRV` (the owner's call) | **-606** | 0 | **`CTRL.DRV` claim 13 -> 14 KB** (13,793 + 163 of 14,336), `EXTD.DRV` 1,406 -> 1,511 (2 KB), blob 11 -> 12 sectors |
| **wkgui** | the window system and drawing | **-406** | **-428** | `EXTD.DRV` 1,031 -> 1,406 inside its 2 KB claim |
| **wkcore** | the core | **-338** | -49 | `HIBER.DRV` claim 6 -> 7 KB (the owner's call, 4) |
| **xfile** | the cross-file lists, after the merge | -160 | **-715** | kern_small `CTRL.DRV` 4,395 -> 4,878 (5 KB), `FDLG.DRV` +16 bss (2 KB) |
| **wkfs** | the file system and the Disk window | -89 | -132 | kern_small `FILECP.DRV` 1,936 inside 2 KB |
| **theme** | EGA Color themes, Customize, WIN95 | -77 | 0 | `WIN95.DRV` -13, `CTRL.DRV` -7 |
| **misc** | parted packed images, Timer/Bounce's leftovers, #252's fixes | -55 | -29 | |
| **assoc** | the ASSOC.DAT writer | -32 | 0 | **`FORMAT.DRV` 4 KB -> 3 KB claim** (-96) |
| **links** | `*.LNK` links and autostart | -30 | 0 | `CTRL.DRV` -38, blob -25 |
| **desk** | the desk layer, DIAs/Sticky Apps | -16 | -2 | `DOCK.DRV` -10 |
| coordinator | `wm_su_rect`'s chromeless arm (the owner's call) | -10 | +3 | |
| merges | | ±4 | ±4 | |
| **total** | | **-1,821** | **-1,348** | |

### 1.1 What paid, and how

**The concept agents found 11-37% of what their concepts added** (assoc 32 of
~300, links 30 of ~164, desk 16 of ~150, theme 77 of ~210, misc 55 of ~350):
fresh code that had been written carefully, and most of whose remaining size
is the feature. **The whole-kernel agents found 833 between them**, and two
STRUCTURAL moves were most of it:

* **Code only one population of callers can reach, written where they are**
  (wkgui, wkcore, xfile). The two seam cuts (`font_ch_cut`, `gfx_blit4`'s
  cut) run only while the desktop is extended, which is exactly while
  `EXTD.DRV` is mounted: -330 resident, and a one-card machine's off-edge
  glyph refusal got faster (~83 -> ~38 cycles). The ATTACH half of a class
  driver load (`drv_hma_try`, `drv_attach`, `drv_tier`, `drv_publish`,
  `drv_load_x`) is reached only from the blob, `CTRL.DRV` and `HIBER.DRV`:
  written once as the `DRV_ATTACH` macro and expanded in each, -266, with
  `drv_load_at` far-calling its caller's copy through the DI:SI it already
  saved. kern_small's adapter switch (`vid_switch` and the five routines only
  it reaches) is `CTRL.DRV`'s now, and its check-box and radio glyph too.
* **kern_small carried planar code it can never run** (wkgui, ~-395): the
  cursor's planar dispatch and bodies, `vid_planes_w` and its six
  multiply-by-one readers, `font_ink`'s and `font_run_x`'s mono tests, the VGA
  row of `vid_tab`. Taking it out made the mouse ISR's cursor calls ~38
  cycles faster each and every text run ~26.

And the shapes pass 12 named recurred: single-caller routines written at the
call (`dsk_free_slow`, `fcp_lrec`, `dsk_img_host`); a `cw_*` shim replaced by
the API cell that already publishes the routine (eight in wkcore, more in
misc and theme); module scratch moved into the module's own nobits section
(FILECP's operation state - and wkfs found why MODULE-SELFCONTAIN-PLAN 6.4.1's
first attempt had to be reverted: a RESIDENT reader of that state that no
check can see, because its operand is `[x]` on both builds); a constant table
replaced by a compare (`assoc_dfold`); two near-identical bodies folded
(`dskw_fdir`, the `sc_m_*` path writers); a register held across a call so a
store goes (`ascw.inc`'s BP-based scratch, which took `FORMAT.DRV` under 3 KB).

## 2. HOT PATHS

None got slower; several got faster. `ui_task`'s idle pass is 8 clocks
faster (misc: the deferred launch is the Disk window's alone, a byte); on
kern_small the cursor calls under the mouse ISR, `font_ink` and `font_run_x`
lost their dead planar tests; the title pens are fewer clocks on every arm
(theme: 66->63, 78->51, 78->36); a one-card machine's refused off-edge glyph
is faster. Paid on purpose, all once per operation: eight `cw_*` shims now go
through API cells (~50-90 clocks a call, the Disk window's layout and the
panel's operations); a module's resident call in `ascw.inc` goes through a
5-byte helper; an EXTENDED desktop pays a far call and two hops on its one
seam cell per text row (~+900 cycles against the cell's ~4,300).

**Parity**: xfile put every hot routine - `gfx_blit4`, `gfx_blit1`,
`font_run`, both mouse ISRs, `cur_move`, `kbm_isr`, `sch_isr`, `sch_switch`,
`ui_task`, `dsk_xfer`, `dsk_runcap`, `dsk_next_clus_x`, `wm_paint_all` - back
at an EVEN shift against the base on both kernels. Some read `changed` in
`os88rdiff --hot` at the close, and each is a content change that was
measured: `gfx_blit4` (its seam cut left for `EXTD.DRV`), `ui_task` (faster),
and `wm_paint_all`/`gfx_blit1` read `equ` on a moved segment constant.

## 3. WHAT THE OWNER DECIDED

Put to the owner during the pass, with the bytes each agent measured or
estimated:

* **TAKEN**:
  * `HIBER.DRV`'s claim 6 KB -> 7 KB as the price of wkcore's -266: a
    hibernate, a wake and a DOS suspend now need 1 KB more free heap at that
    moment.
  * `wm_su_rect` answers a CHROMELESS window's frame (desk's item 2): -10 on
    kern_big, +3 on kern_small, and it FIXES a defect - `OSAPI_WM_DAMAGE` on
    a fullscreen window answered CF = 1 ("draw the whole content") with
    AX..DX inset by a title bar and borders the window does not have, so a
    package that trusted the rect skipped its top 18 rows (SPEC.md 11.103.5).
    Applied on both kernels by the coordinator.
* **DECLINED** - every move that makes a feature need the system disk where
  it did not: Cut/Copy/Paste and the Standard File dialog as modules on
  kern_big (~3.2 KB, estimated), fullscreen mode-setting as a module (~400),
  `.LNK` links through `CTRL.DRV` (~65) and desktop shortcuts with them
  (~220). And: Sticky Apps relaunched from the blob (-63, an earlier launch),
  the menu's private XOR mask (-7), dropping the ASSOC capture (~-54, a
  slower flush), `rect_get`/`rect_put` S2+S3 (-138, save-under cycles), the
  defensive `cld`s (~-20), five-byte rare cells (~-105, an ABI break on
  `main`), the slot tables at 12/24 (-112, reopens a silent drop), kern_small's
  `TP` preset (-7, loses a user's preset), `app_launch`'s at-cap refusal (-6).
* **TAKEN, put after xfile priced them** (`dispsw`, -606 together):
  * **Set Primary into `CTRL.DRV`**, at the owner's word: *"setting a new
    primary ... only happens from control panel so yes do it"*. `vid_switch`,
    `vid_disp_init`, `wm_refit` and the Dock page's apply are written once as
    macros and expanded in `CTRL.DRV` and in a twelfth blob sector for the
    boot (a `SYSTEM.CFG` naming a non-default primary). `vid_equip` and
    `vid_desk_union` are expanded a third time in `EXTD.DRV`, because a
    fullscreen program leaving an extended desktop is their one caller
    outside the panel - so nothing outside the Control Panel and the boot
    needs `CTRL.DRV` (tested with it unloaded, and broken on purpose once).
    What stayed resident and why is in `ksp13/dispsw.md`.
  * **The check-box and radio glyph into `CTRL.DRV`** (-207): the panel is
    its only kernel caller on kern_big too. Packages carry their own copy
    and are byte-identical.
  * Together they take `CTRL.DRV`'s claim 13 KB -> 14 KB while the panel is
    open, the same kind of trade as `HIBER.DRV`'s.

## 4. WANTS AN ABI BREAK

Only wkcore's five-byte rare cells (~-105 `.text` on both kernels, estimated):
it renumbers the API table, so `PKG_FMT` moves and every package is rebuilt.
Declined this pass. **Pass 12's ABI wish 1 is REFUSED, not deferred**:
`OSAPI_MEM_HMA` as an arm of `OSAPI_MEM_CLAIM_DMA_HI` would put `SOUND.DRV`'s
movable pool in the HMA (it calls DMA_HI with CX = AX at 4-20 KB), and the
shape that works - both forms on `OSAPI_MEM_FREE` - measured kern_big **+6**.

## 5. DEFECTS FOUND

* **`OSAPI_WM_DAMAGE`'s fullscreen rect** (desk, FIXED with the owner's
  word): above.
* **`tests/hibernate.py` mirrored `KIND_HIBER = 5`** where the kernel's is 3
  since #264 renumbered the kinds, so `hibernate` and `hibernatem` failed on
  every tree since (xfile found, coordinator FIXED).
* **`tests/desksc.py` leg G waited for a Disk window on cluster 4**; `B:\MEDIA`
  is cluster 5 since `ASSOC.DAT` grew to three clusters. It reads the
  cluster off the disk now (links/desk found, coordinator FIXED).
* **`tools/os88rdiff.py` could not assemble a commit's kernel**: `vga12.inc`
  includes `drivers/thmpal/thmrows.inc` by a root-relative path (four agents
  hit it; FIXED twice over - a "." in the include search and `drivers/` in
  the export).
* **`tests/kdreturn.py --compact module` walled 3+3 KB round the DOS box**
  for a 6 KB `HIBER.DRV`: with the 7 KB image (wkcore, the owner's call) it
  could not fit even after the move, and the handoff refused honestly - the
  row read it as SPEC.md 96.40.8's defect. The holes now add up to the image
  (4 under, 3 over: the ceiling leaves exactly 4 KB above the box). FIXED.
* **`tests/kdbigexe.py`'s breakpoint is a flat address that is live KERNEL
  code until the handoff replaces it**; this pass moved an instruction the
  handoff executes onto it, so the first recorded "width" was 2135. A hit
  counts now only while the bytes under it are kern_dos's own. FIXED.
* **`MSELOFF=1` did not assemble** (`soak -k buildmatrix`): `fm_mods` sat
  inside `%ifdef FM_MSEL` while the `*.LNK` Alt-drop calls it too. It failed
  at the pass's base as well. FIXED with no byte of the shipped kernel moved.
* **`tools/os88marty.py`'s `no_saver` wrote `[ss_mins]` by name**, which
  kern_small no longer has (wkgui, FIXED: it writes it where the symbol
  exists).
* **`EXTD.DRV` was 1,031 bytes at the base**, so it took a 2 KB claim while
  SPEC.md quoted 1,023 (wkgui, SPEC.md 39.19.6 corrected).
* **The base's `docs/INDEX.md` was stale against the brief**, so `make` failed
  at `checkdocs` in every worktree until each agent regenerated it.
* **Not fixed**: no row checks a BACKGROUND window's caption (theme broke
  `wm_tpen_ink` on purpose so the inactive caption was drawn in its ground
  colour; `disptitle`, `dispthm` and `colorpresets` stayed green while a CGA
  capture showed 89 pixels of caption gone); `kernsize`'s `.boot2` figure is
  the NOKZIP blob, ~213 bytes under what ships (links); `tools/os88modcost.py`'s
  `.ovl` cross-check fails on this base (wkgui); a module load reached from
  inside a desktop-shortcut launch overwrites the outer folder bracket's saved
  position (desk, by reading; not reproduced); `stiknodesk` and `deskmodal`
  need `mtools`, which `make deps` does not install, so they skip silently.

## 6. WHAT IS LEFT

* **The boot blob is full again**: 6,110 of 6,144 bytes on kern_big, 34
  spare, at 12 sectors. The next thing that grows it takes a thirteenth
  (`--sectors 13` prices that at no extra `int 13h` either), or first makes
  `ovc_buf`'s 163 zero bytes nobits with a `mem_init` reserve that covers
  them (xfile's WANTS 3, ESTIMATED).
* **kern_small's `DISKOPS.DRV` is exactly 7,168 bytes**, as at pass 12's
  close: one byte more and its claim is 8 KB. kern_big's `CTRL.DRV` has 380
  bytes of its 14 KB.
* **`.ovl` is 5,118 of 5,120** on kern_big.
* Carried, untouched: `HDD.DRV` at its floor, the 2A3h PicoMEM ramp in four
  places, the relaxed-jump remainder (pass 11's table says which are
  deliberate).

## 7. WHAT WAS RUN

* Every agent: `make`, `make small` and the fast tier after every batch,
  `make emu` (and `make rom` where it mattered) at its tip, `checkdocs`, and
  `tools/stkbalance.py` over the files it touched, base against tip - no new
  unbalanced path anywhere. Equivalence where no row reaches, in unicorn
  against the base kernel's own bytes: `thm_set` (2,160 cases, and again
  through the cell with a foreign DS), `ld_check_hdr` (75,600 headers),
  `ld_hdr_ok`, `dsk_vol_rmv`/`dsk_img_orphan` (~39,000), `fsx_page` (810
  states). Each agent broke something it changed on purpose and watched a
  row go red.
* About 180 per-agent soak rows, green, listed in each note.
* **The integration soak** and **the full tier**: section 7.1.

### 7.1 The close

* **The integration soak**, on the merged nine (`9210f7e4`): **278 rows**,
  the union of every agent's rows widened to every subject family the pass
  touched (`assoc*`, `ico*`, `fmt*`, `stik*`, `desk*`, `dock*`, `fdlg*`,
  `fcp*`, `wseq*`, `fm*`, `thm*`, `disp*`, `cur*`, `small*`, `kd*`,
  `hibernate*`, `drv*`, `lz*`, `img*`, `cp*`, `hd*`, `snd*`, `heap*`, `reg*`,
  `rehome*`, `rom*`, `vidpage*`, `wire*`, `int0sweep`...). **276 ok first
  time; `kdreturncm` and `kdbigexe` failed** - both green at the base and red
  at the merge, both the ROW (section 5), both green after the fix. `blitcut`
  was cut off by a container restart and passed on re-run. The whole soak
  tier was not run: it is the owner's to ask for.
* `dispsw`'s 30 rows on its own tip (the Display page, the extended desktop,
  the fsx bracket, the boot, `small128`): 30/30.
* **The full tier: 55/55** on the close.
* **`soak -k buildmatrix`: ok** (after the `MSELOFF=1` fix).
* `make`, `make small`, `make emu`, `make rom` build; `docs/KERNEL-MEMORY.md`
  re-blessed on all three kernels.
* **A new fast row, `hotpar`** (`tests/unit/t_hotpar.py`, 0.1 s, at the
  owner's request): every hot routine starts at the address PARITY it was
  measured at, on kern_big and - when `build/smallk` is current - kern_small.
  The kernel build now writes `kernel.map` beside `kernel-full.bin` (no byte
  of the image), so the row assembles nothing. Broken on purpose with one
  `nop` in front of the hot `.text` run on each kernel: red, and green on
  restore. It is the gate for the step section 8 says both passes had to do
  by hand.

## 8. METHOD LESSONS

* **`git stash` is ONE STACK for every worktree of a repository.** Two agents
  used it for base comparisons before anyone noticed, and one popped the
  other's stash; nothing was lost, by luck and a patch file. The brief must
  say so. `git show <base>:path`, `os88rdiff`, or a private
  `git worktree add` of the base are the safe ways.
* **The scratchpad is shared by every agent too**: helper scripts and a
  soak log were overwritten. Brief each agent a private subdirectory.
* **An agent that reports per row floods the coordinator.** One handed back
  eleven times while its rows trickled in; ask for ONE final message carrying
  the row results.
* **Concept agents on fresh code find little; whole-kernel agents with a
  STRUCTURAL brief found most of the goal** - not by re-peepholing the big
  files, but by asking which population of callers can reach a routine, and
  writing it there. The two biggest single moves (-330, -266) were each one
  reachability argument. A pass briefed for a fixed byte target should weight
  its agents that way.
* **Parity is a gate now** (`hotpar`, above). Both passes found the hot list
  at an odd shift part-way through and padded it back by hand; the row pins
  the parities, so the next pass is told at the first `make` that moves one.
* **A reference walk misses fall-through entries** (wkcore nearly moved the
  screen saver's path out of resident): verify every moved entry by READING.
* **A stale row mirror reads as a kernel defect**: two rows had failed since
  #264 for constants the tests carried themselves. Two agents classified the
  failure against the base before blaming anything, which is the step that
  saved the time.
