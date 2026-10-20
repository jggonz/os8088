# ksp13 agent "dispsw" - kern_big's display switch, and its glyph, into CTRL.DRV

Kernel size pass 13, a follow-on agent run on the merged pass. It took two
items with the owner's approval: xfile's WANTS THE OWNER 1, the display
switch on kern_big, and xfile's WANTS THE OWNER 2, the check-box and radio
glyph on kern_big. The owner's words for the first were *"Display switch:
not fsx switch, but setting a new primary? If so that only happens from
control panel so yes do it."* So the constraint is functional. Nothing that
runs outside the Control Panel and the boot may come to need `CTRL.DRV`,
which means the system disk.

The branch is `ksp13-dispsw`, cut at `9210f7e4` (the merged pass). The pass
base is `aedadd25`. Figures are ASSEMBLED bytes: `tools/kernsize.py --json`
absolutes, with resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.
A module figure is its UNPACKED image. The real blob is `boot2_end` in a KZIP
assembly. kernsize's `.boot2` is the NOKZIP figure.

## BASE AND TIP

| | `9210f7e4` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 91,542 | **90,936** | **-606** |
| kern_big `.text` / `.bss` / `.cold` | 42,984 / 5,218 / 37,646 | 42,630 / 5,218 / 37,394 | -354 / 0 / -252 |
| kern_big `KERN_SIZE` | 97,792 | **97,280** | **-512** (image rung uncrossed) |
| kern_big `.boot2` (kernsize, NOKZIP) | 5,407 | 5,896 | +489 |
| kern_big REAL blob (KZIP) | 5,621 of 5,632 | **6,110 of 6,144** | +489 |
| kern_big `BOOT2_SECS` / `KSIG_OFF` | 11 / 5120 | **12 / 4608** | file sector 21 both |
| **kern_emu resident** | 91,799 | **91,193** | **-606** |
| kern_emu `KERN_SIZE` | 97,792 | 97,280 | -512 |
| kern_emu blob (KZIP) / sectors | 5,721 of 6,144 / 12 | 6,199 of 6,656 / 13 | +478 / +1 |
| **kern_small resident** | 58,912 | **58,912** | **0** - byte-identical, see below |
| kern_small `BOOT2_SECS` / `KSIG_OFF` | 11 / 5120 | 11 / 5120 | 0 |
| `CTRL.DRV` kern_big (+ 163 bss) | 13,128 | **13,793** | +665, claim **13 KB -> 14 KB**, 380 spare |
| `CTRL.DRV` kern_emu | (not built at the base) | 13,796 | kern_big's + 3; 14 KB |
| `EXTD.DRV` | 1,406 | 1,511 | +105, 2 KB claim unchanged, 537 spare |
| `CTRL.DRV` kern_small | 4,878 | 4,878 | 0 |

**kern_small is byte-identical.** `tools/os88rdiff.py kernel 9210f7e4 . -D
KERN_SMALL --all` reports 2,535 routines `same` and one `changed`. The changed
one is `cp_cgo`, whose `jmp short` target is now a macro-local label name. Its
bytes are the same. `kernel.bin` and every small module differ from the base
only by the build number byte or bytes.

**Every package is byte-identical** except `scribe.o88`. Its `SC_OVSTAMP` is a
`cksum` of its source text, and `apps/os88ui.inc` changed in comments.

## TAKEN

Four commits. Each was gated by `make -j2` (fast tier 47/47), `make -j2
small` and `make -j2 emu`. `make rom` builds at the tip: both ROM arms still
build, and the window holds EXTD.DRV with 539 bytes spare. `tools/stkbalance.py`
over every touched file gives the same three ladder lines at base and tip.
`tools/checkdocs.py` is clean.

### 1. The adapter switch - kern_big -299, kern_emu -299 (`0efc7158`)

The walk out of `vid_switch` on kern_big ends at its callers. Each one was
read rather than taken from a reference walk:

* **Set Primary**: the Display page's click (`cp_vid_click`).
* **the arrangement**: `cp_vid_click`'s desktop row, which calls
  `vid_disp_init` and `wm_refit`.
* **the Dock page**: `cp_dock_click`, which calls `dkf_apply` and through it
  `dock_reflow`, which calls `wm_refit`.
* **the BOOT**:
  * `drv_boot_x` applies SYSTEM.CFG's adapter through `vid_switch`, its
    `.vnosw` arm calls `vid_disp_init`, and `CFG_BOOT` calls `dkf_apply`.
  * `kmain_o` calls `vid_disp_init` before `mem_init`, so no module could
    serve it.

These are all the callers. `dkf_apply` has two callers, ctrl.inc and the
boot's settings reader, and `dock_reflow` has one, `dkf_apply`. Nothing falls
through into any of these routines, no table holds their addresses, and no
code jumps into their interiors.

So these bodies are now `.modu`, CTRL.DRV's:

| routine | bytes it took out of resident |
|---|---:|
| `vid_switch` | 62 |
| `vid_blank` | 25 |
| `vid_avail_test` | 20 |
| `vid_disp_init` | 68 |
| `vid_disp_other` | 21 |
| `wm_refit` | 37 |
| `dock_reflow` | 16 |
| `dkf_apply` (`.cold`, now `dock_apply`) | 45 |
| `desk_rowcalc`, the `.text` thunk (deleted; both its callers far-call `desk_rowcalc_x`) | 6 |

Each body is written ONCE as a macro, `driver.inc`'s `DRV_ATTACH` shape:
`VSW_AVAIL`, `VSW_SWITCH`, `VSW_DISP`, `WM_REFIT` and `DOCK_APPLY`. Each is
expanded twice:

* **into CTRL.DRV**, with the natural names, reaching the kernel through
  `KSWK`/`KSWC`, which is the panel's `cp_kgo`/`cp_cgo` hop;
* **into the boot blob**, as `bvs_*`, `bvw_refit` and `bdk_*`, reaching the
  kernel through `BSWK`/`BSWC`.

The hop pair itself became a macro, `KGO_PAIR`, in kernel.asm. It is expanded
as `cp` in ctrl.inc, byte-identical to the old hand-written pair, and as `bvs`
in the blob. The blob's copies are given back at `mem_unblob` with the rest of
it.

Three call sites changed shape:

* `kmain_o` and `.vnosw` near-call `bvs_disp_init`, 3 bytes against the 8 of
  `mov bp / call spw_near`.
* `drv_boot_x` and `CFG_BOOT` use `BLOBCALL bvs_switch` and `BLOBCALL
  bdk_apply`.

What stays resident is what something outside the panel and the boot
reaches:

* `wm_su_drop_all`, from `OSAPI_THM_SET`;
* `vid_dual_ok`, from `fsx_page`;
* `vid_ctx_capture`, from `cw_vid_ctx_capture` (EXTD.DRV and `.ovl`);
* `wm_pref_take`, `wm_pref_now` and `wm_apply_size`, which every window
  that is sized goes through;
* `vid_equip` and `vid_desk_union` stayed until TAKEN 3.

**The blob is TWELVE sectors** on kern_big, up from eleven: the real blob is
6,005 of 6,144 bytes at this commit. `t_blobruns` counts int 13h calls per
geometry, 360 KB / 720 KB / 1.2 MB / 1.44 MB. It reads stage 1's blob read as
2/2/2/2 calls and stage 2's kernel read as 11/11/8/7 calls, **unchanged from
eleven sectors on every geometry**. The 720 KB call that wkcore measured for a
twelfth sector is not paid on this kernel, because the kernel's own length
moved where the cylinder boundary falls. The cost is one more in-run sector,
about 24 ms by PERFORMANCE.md's per-sector figure, and no extra call.

`KSIG_OFF` moved 5120 -> 4608, so the canary is still on file sector 21, and
`t_canary` is green. kern_small has no boot copy and keeps eleven sectors and
5120: `BOOT2_SMALLSEC` is in kernel.asm and boot2.asm, and the Makefile's
KERN_SMALL block takes the sector back. The shipped literal on the
`BOOT2_SECS` line stays kern_big's, which is the one that `t_canary`,
`t_blobruns`, `os88layout` and `kzboot` read. kern_emu keeps its knob sector,
so it is now 13 sectors, 6,199 of 6,656 bytes. Only an emulator boots it.

Three tools were adjusted:

* `tools/os88ovlchk.py` now honours `ovlchk: builds its own far frame` on a
  macro TEMPLATE label too. That is `KGO_PAIR`'s `%1 %+ _kgo:`, and without
  it the return-kind rule called `cp_kgo` a near-called RETF.
* `tests/unit/t_chromesites.py` reads `WM_REFIT`'s pasted label (`%1_refit`).
* `tools/os88boot.py` names the blob's `bvs_disp_init` call for the
  `vid_disp_init` phase (`ec2f6e48`). That cleared that name from
  `os88ladder --selfcheck`, though not the rest of the selfcheck: see
  DEFECTS.

Cycles: the panel's click and the boot each pay about 300 cycles per hop, at
roughly 21 hops on a Set Primary. That is about 1.3 ms against a mode set and
a full repaint. No hot path is touched.

### 2. The check-box and radio glyph - kern_big -207, kern_emu -207 (`75ff7e76`)

This is xfile's TAKEN 3 on kern_big. `OS88UI_KGLYPH .modu` is now set on both
kernels in `fdlg.inc`, and ctrl.inc's `cp_gemit` near-calls the glyph with
`CPXN` on both.

On kern_big the glyph's only kernel caller is the panel:

* `os88ui_btn` and its records do not draw a glyph.
* `OS88UI_CHK` and `OS88UI_RAD` are not defined in the kernel.
* WIN95.DRV, `icons.inc` and `files.inc` do not call it.
* The drivers that draw a glyph on a panel page of their own (RAMDISK, SAVER,
  ETHER) include `os88ui.inc` themselves and carry their own copy.

`.cold` went from 37,601 to 37,394. CTRL.DRV went from 13,459 to 13,688.

### 3. `vid_equip` and `vid_desk_union` - kern_big -100, kern_emu -100 (`cb4caaaa`)

These two had a caller outside the panel: EXTD.DRV's restore when a
fullscreen program leaves an extended desktop. That is `ext_e_fsx`'s
`EXTF_LEAVE`, through `ext_kapply_eq` and `ext_two`, and it runs with nothing
but EXTD.DRV mounted. So they became macros too, `VSW_EQUIP` and `VSW_UNION`,
with a THIRD expansion `ext_` in EXTD.DRV. There they are near calls that
replace two `EXK` hops. Their callers are now:

* `vid_equip`: `vid_switch` and EXTD's own copy;
* `vid_desk_union`: `vid_disp_init` and EXTD's own copy.

As copies, `vid_desk_union` banks its six registers by hand. The resident body
used the `kent_di`/`kret_di` ladder, which an image cannot reach. The pushes
and pops are in the same order. kern_small's `vid_equip` is the same macro,
expanded where it was, and is byte-identical.

The cost of the three copies:

| image | change |
|---|---|
| CTRL.DRV | +105, to 13,793 + 163 bss = 13,956 of 14,336 |
| blob | +105, to 6,110 of 6,144 |
| EXTD.DRV | +105, to 1,511 of its 2 KB claim |

This item is what uncrosses kern_emu's image rung.

### 4. A parity pad - kern_big +1, kern_emu +1 (in `0efc7158`)

`wm_refit`'s 37 bytes left an odd hole ahead of `wm_paint_all` and `ui_task`.
A `nop` after `wm_nat_take_wh`'s `ret` (KERN_BIG, never run) puts the run back
on an even shift. The `.text` total did not move: `dsk_img16`'s jump
re-converged short and gave the byte back.

## PARITY

`tools/os88rdiff.py kernel <base> . --hot-file` over 31 regexes:

* `gfx_blit4`
* `gfx_blit1.*`
* `font_run.*`
* `mou_isr`
* `mou_p2_isr`
* `kbm_isr`
* `sch_isr`
* `sch_switch`
* `sch_resume`
* `sch_idle_body`
* the `api_*` stubs
* `dsk_xfer`
* `dsk_runcap`
* `dsk_next_clus_x`
* `dsk_copy_seg_x`
* `ui_task`
* `wm_paint_all`
* `cur_move.*`

The equ-ok pattern is `--equ-ok '.*_SEG|COLD_HOME'`.

**Against `9210f7e4`**, all 31 are identical at an even shift on kern_big.
kern_small is byte-identical, as above.

**Against `aedadd25`**:

* kern_big and kern_emu fail only on `gfx_blit4` and `ui_task`. Those two are
  `changed` by the merge, not by this branch. They sit at -320 and -852, both
  even.
* kern_small lists `gfx_blit1` moved -717, which is odd. It also lists
  `font_run_x`, `cur_move_1` and `ui_task` as changed. All four are the same
  at `9210f7e4`, because kern_small is byte-identical across this branch. The
  odd shift is therefore the merged tree's, after xfile's close, and not this
  branch's. See DEFECTS.

## REFUSED

* **`ovc_buf` as nobits, to stay at eleven sectors** (xfile's WANTS 3). The
  blob needed +489, not 163. Even with the reserve extension, eleven sectors
  could not hold the boot copies, and a twelfth sector costs no int 13h here.
  Not taken.
* **Loading CTRL.DRV at boot to serve SYSTEM.CFG's adapter.** It is refused
  for two reasons.
  * `kmain_o` calls `vid_disp_init` before `mem_init`, so it needs a resident
    or blob copy anyway.
  * A boot whose CFG names another adapter would read about 14 KB of module
    on every start, and would boot on the probe's adapter if CTRL.DRV were
    missing. That second part is a functionality change.
* **kern_small taking the twelfth sector too**, as SPEC.md 26.10.5 did for
  autostart. It would buy that build nothing. `BOOT2_SMALLSEC` keeps it at
  eleven and byte-identical.
* **kern_emu back to twelve sectors** (6,199 against 6,144: it no longer
  fits). Its knob sector stays, and only an emulator boots it.
* **`vid_ctx_capture`, `vid_dual_ok` and `wm_su_drop_all` into images.** Each
  has a resident or non-panel caller, listed under TAKEN 1.

## WANTS THE OWNER

Nothing new. The claim growth was approved for this item:

* CTRL.DRV's claim is **13 KB -> 14 KB on kern_big and kern_emu**. The Control
  Panel, a Dock or Display change, and a desktop gesture that loads the panel
  now each need 1 KB more free heap while the panel is loaded. CTRL.DRV is
  13,793 + 163 = 13,956 bytes with both items, which leaves 380 spare.
* The core-only load (`MOD_SETS`, `.modc`) is unchanged.

## WANTS AN ABI BREAK

None. Every change is kernel-internal: module images, the blob, and `.text`
and `.cold` bodies. The `apps/os88ui.inc` change is comments only.

## CROSS-FILE

* **`vidsel.inc`, `wm.inc`, `dock.inc`, `driver.inc`, `extmod.inc`,
  `ctrl.inc`, `kernel.asm`**: the macro bodies and their expansions. A NEW
  runtime caller of `vid_switch`, `vid_disp_init`, `wm_refit`, `vid_equip`,
  `vid_desk_union` or `dock_apply` from resident code must not be added.
  Those labels are `.modu` now, and `os88ovlchk` refuses a resident near
  call to them, because the CTRL expansions sit under a literal `section
  .modu`.
  * A caller outside the panel needs a copy, as EXTD.DRV's `ext_` ones do.
  * A caller in the blob uses `bvs_`/`bdk_`.
* **The blob has 34 bytes left** in twelve sectors on kern_big. The next blob
  growth takes a thirteenth sector. `t_blobruns --sectors 13` reads 720 KB's
  stage 2 at 10 calls and stage 1 unchanged, so on this kernel it is not a
  call either.

## ROWS RUN

**Scratch harnesses**, not rows, kept in this agent's scratch directory:

* `swh.py` drives Set Primary on kern_big both ways, checks BDA 40:10 and
  CTRL.DRV's load state, writes SYSTEM.CFG and reboots. It ran on
  `os8088_5150_both_gla` (CGA <-> Hercules), `os8088_xt_vga_herc` and
  `os8088_xt_vga_mda` (VGA <-> Hercules). **PASS on all three**:
  * `[vid_kind]` moves both ways.
  * BDA 40:10 bits 5:4 read 0x30 on Hercules and 0x20 on colour.
  * The Disk window's rect comes back exactly, which is SPEC.md 39.11.2.1's
    identity.
  * CTRL.DRV is unloaded once the panel closes.
  * **The reboot comes up on the saved non-default primary**: the blob's
    `bvs_switch`, with CTRL.DRV unloaded.
  * The base tree gives the same values. The screenshots of every leg are
    **0 pixels different** from the base on `both_gla` and on `vga_herc`.
* `fsxh.py` runs Extend Right, closes the panel, takes the DOS box full screen
  and back. It ran on `os8088_5150_both_gla` (CGA primary) and `_mono`
  (Hercules primary). **PASS**:
  * CTRL.DRV is unloaded throughout.
  * `[vid_w]`/`[vid_h]` are back to the union (1360x368 and 1360x348).
  * `[vid_ndisp]` is 2, and 40:10 is the primary's kind.
  * **Broken on purpose**, with `ext_two` skipping the union unless
    CTRL.DRV is loaded, it goes **red**: `w is 640 after the bracket, 1360
    before`.
* `cph.py` crops every Control Panel page on base and tip. On
  `os8088_xt_vga` all eight pages are identical bar the Date/Time page's
  running seconds digit. On `os8088_5150_herc_gla` the six pages shown are
  identical. That covers the radios, the check boxes and the Force Color box.

**Rows**, run once after the last code commit with `os88test.py soak ...
--marty-jobs 2`: see the results below.

**30 passed, 0 failed, 0 skipped** (794 s, two lanes):

| rows | what they reach |
|---|---|
| `dispfit`, `dispherc1`, `dispthm`, `dispreboot` | Set Primary through CTRL.DRV's `vid_switch` and `wm_refit`; the theme re-assert; `vid_reboot` |
| `dispmode`, `dispcheck`, `extdmod`, `dualcheck` | the arrangement through the panel's `vid_disp_init`; the reboot through the blob's `bvs_disp_init` |
| `dispfsx`, `dispfsxherc`, `dispfsxcga`, `dispmcfs`, `fsxdisp`, `dispsave`, `dispstrad` | fsx brackets on an extended desktop, through EXTD.DRV's `ext_equip` and `ext_desk_union` |
| `dockpos`, `dockmodule` | the Dock page's `dock_apply`; the boot's `bdk_apply`, including the saved-setting fallback |
| `cpup`, `cpnames`, `cplistrow`, `vgacolor`, `glyphcost`, `colorpresets`, `thm95` | the panel's controls and its glyph |
| `kzboot`, `bootsmoke`, `smallboot`, `small128`, `lzdrv`, `stk0water` | the twelve-sector blob, kern_small's eleven sectors, and task 0's stack high water |

`thm95` took 155.0 s against its declared 75 s. That is a timing note, not a
failure.

## DEFECTS FOUND

* **`dispfsxcga` cannot see a lost union.** With the deliberate break above,
  where the restore leaves the desktop 640 wide on an extended machine, the
  row still reports `ok`. It asserts the second card's mode and framebuffer
  bytes, and neither moves. `fsxh.py`'s leg is the check that does see it.
  Whoever next touches that row could add a `[vid_w]`/`[vid_ndisp]`
  assertion after `wait_restored`. Not done here: it is not this branch's
  file.
* **`os88ladder --selfcheck` is stale at `9210f7e4`.** kmain names `as_boot`,
  `desk_rowcalc_x`, `far [10]` and `ui_lcall`, which its STAGES table does
  not know. That was so before this branch. `bvs_disp_init`, this branch's
  addition, is mapped to `vid_disp_init`.
* **kern_small's `gfx_blit1` is at an odd shift (-717) against `aedadd25`**
  on the merged tree. This branch leaves kern_small byte-identical, so it is
  the merge's, after xfile's parity pass. It is for the coordinator.
