# ksp13 agent "wkcore" - the core, whole-kernel

Kernel size pass 13 (the brief on this branch still sits at
`docs/plans/completed/ksp14/BRIEF.md`; the pass was renumbered mid-round).
Area: `kernel.asm` (the API table, the cells, the `cw_*` shims, the ladder),
`memory.inc`, `driver.inc`, `loader.inc`, `mod.inc`, `instance.inc`,
`sched.inc`, `clock.inc`, `hiber.inc`, `shutdown.inc`, `fsx.inc`, `xmem.inc`,
`snd.inc`, `cpudet.inc`, `boot/boot2.asm` and the `.ovl` overlay, bar the
concept agents' hunks. Branch `ksp13-wkcore` (was `ksp14-wkcore`), cut at
`aedadd25` (`4833dfef` plus the brief). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes, resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`; a module is its UNPACKED image (`os88drv.image_unwrap`).

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,757 | **92,419** | **-338** |
| kern_big `.text` / `.bss` / `.cold` | 43,756 / 5,223 / 38,084 | 43,690 / 5,223 / 37,812 | -66 / 0 / -272 |
| kern_big `KERN_SIZE` | 98,816 | **98,304** | **-512** (the cold rung the unpassed work crossed, uncrossed) |
| kern_big `.boot2` (kernsize) / the blob's end (`BOOT2_SIZE`) | 5,129 / 5,342 | 5,412 / 5,627 | +283 / +285 - **5 bytes of the 11-sector blob left** (see CROSS-FILE) |
| **kern_small resident** | 60,260 | **60,211** | **-49** |
| kern_small `.text` / `.cold` | 31,847 / 22,482 | 31,811 / 22,469 | -36 / -13 |
| kern_small `.boot2` | 4,364 | 4,379 | +15 |
| kern_small `KERN_SIZE` | 62,464 | 62,464 | 0 |
| **kern_emu resident** | 93,016 | **92,676** | **-340** |
| kern_emu `KERN_SIZE` | 99,328 | 98,816 | -512 |
| `BOOT2_SECS` | 11 | 11 | 0 (12 was tried, see TAKEN 1; `KSIG_OFF` unmoved) |
| `.ovl` | 5,106 / 1,480 | 5,106 / 1,480 | 0 |
| `CTRL.DRV` kern_big | 12,781 | 13,094 | +313, **13 KB claim unchanged** |
| `CTRL.DRV` kern_small | 4,383 | 4,395 | +12, 5 KB claim unchanged |
| `HIBER.DRV` | 5,532 | 6,145 (5,819 + the dead-zone pad) | +613, **6 KB -> 7 KB claim** (WANTS THE OWNER 1) |
| every other module and driver | | | 0 |
| 360 KB system disk, free clusters | 11 | 10 | -1 |
| ROM window (`make rom`) | | big holds `EXTD.DRV` with 565 spare; small holds FDLG, FILECP, CTRL, FORMAT and the Task Manager with 5,199 spare | |

## TAKEN

### 1. The attach half of a driver load leaves resident `.cold` - kern_big/emu -266 (`ae46a3d1`)

The finding is a call graph, not a peephole. Every caller that brings up a
CLASS driver is outside resident memory - `drv_boot` in the blob, the
Drivers and Sound pages in `CTRL.DRV`, `hbm_reload_m` in
`HIBER.DRV` (a wake's reload and `OSAPI_DRV_SUSPEND`'s resume) - and the one
RESIDENT caller of the loader, the screen saver's `ss_open`, loads
`SAVER.DRV`, an OVERLAY (`DRVC_OVL`), which stops after `drv_bss` exactly as
`XMEM.DRV` and `THMPAL.DRV` do. So the half only a class row reaches - the HMA
placement (`drv_hma_try`), `drv_attach`, `drv_tier`, `drv_publish`, the
row-index entry `drv_load_x` and its far entries `drvf_drv_load` /
`drvf_drv_tier` - is `driver.inc`'s `DRV_ATTACH` macro, expanded in the three
images (`bdl_`, `cdl_`, `hdl_`), and `drv_load_at` stays the one resident
loader. Where the attach used to be, `drv_load_at` far-calls the CALLER's half
through the DI:SI its `kentc_bp` bank already holds (`mov bp, sp / call far
[bp+4]`), so the attach runs at the same point, inside the same
`drv_vol_bank`/`drv_vol_back` bracket, with the same `DRVE_OK` stamp before it
and the same refusal map after it. SPEC.md 51.5.3's trade, by name: copies in
images that come and go against one in a segment every machine pays for.

* Each copy reaches the kernel only through far entries that already existed
  (`drvf_drv_row`, `drvf_drv_load_at`, `drvf_drv_call`, `drvf_drv_cls_svc`,
  `mmf_mem_free`); `drv_hma_door`, `drv_cls_idx` and `drv_cls_fp_x` are
  written out (the class indexing without its range test - every row that
  reaches `_publish` has a class in 1..DRVC_MAX, a `drv_tab` row whose
  header `drv_check` held to it or the `[drv_owner]` row `_publish` wrote).
  **No new resident far entry.**
* `drv_attach` is merged into its one caller (`_att`): its four pushes and its
  write of a refusal's AL into the banked AX were both for a caller whose own
  KENT bank is what gets restored. `drv_load_x`'s index entry went: every
  caller has the row in BX (`drv_boot_want`, `cp_drvrow`; `hbm_reload_m` asks
  `drvf_drv_row`). That is what fitted the blob copy into the 11 sectors -
  the first version needed a twelfth, which costs a 720 KB boot one more
  `int 13h` (`t_blobruns` red, 11 -> 12 calls), and was taken back.
* Stack: the attach's depth is unchanged to the word (+4 hook far call, +2
  `_call`, +4/+2 the far entry, against the four pushes and two near calls
  that went).
* `tests/lzdrv.py` stopped at `drv_attach`; it stops at the new local
  `drv_load_at.attach` - the same instant, image read, checked and zeroed,
  nothing run.
* SPEC.md 51's "One loader, not two" paragraph carries the new shape.

Cost: blob +277 (of the 290 it had spare), `CTRL.DRV` +277, `HIBER.DRV` +287,
which tips HIBER's bss out of its claim's KB rounding: a CTRL-style dead-zone
pad (`kernel.asm`, `MODH_UNPAD`) takes the claim to 7 KB.

### 2. Three bodies out to their images - big -40, small -23 (`67bb7358`)

* **`sched_mode_set`** (20, `.text`, both kernels): its callers are the blob's
  settings reader and the Scheduling radio. The reader writes the two stores
  out (`pushf/cli/.../popf`, 13 bytes for the 5 of a far call - its own `ja`
  has already normalised AL to 0/1); the radio's copy is `cp_schset`, CTRL.DRV
  `.modu`, the same body with `ret`. No row drives the radio: equivalence by
  reading, the body is byte-for-byte the old one bar `retf` -> `ret`.
* **`drv_memk`** (14, `.text`, kern_big): read only by the Drivers page; it is
  `CTRL.DRV`'s table now, read `cs:` (two sites, +1 byte each in the image).
* **`drv_cp_count_x`** (6 big / 3 small, `.cold`): `cp_nitems` asks
  `drvf_drv_cp_class` for the 0xFF ordinal itself (what the far entry did);
  kern_small's stub, which answered 0, is `cp_nitems` reading `[cp_nst]`.

### 3. Eight `cw_*` shims are the API cell that already publishes the routine - big -32, small -16 (`67bb7358`, `6cb3eb6c`)

`cw_gfx_lock`, `cw_gfx_unlock`, `cw_task_yield`, `cw_wm_content` (both
kernels), `cw_thm_desk`, `cw_drv_pkg_call`, `cw_wm_ondrag`, `cw_wm_timer`
(kern_big) are `equ` the cell (`apic_*` labels, which emit nothing). Size pass
9's rule (MODULE-SELFCONTAIN-PLAN 5): the cell is a door that exists, the shim
a second one. Semantics: an SLOT cell is the shim plus a DS bank whose value
is KERNEL_SEG for every kernel caller; the X cell (`drv_pkg_call`) sets ES to
the caller's DS, which `desk.inc`'s one caller has just made ES itself; the
`wm_ondrag_c`/`wm_timer_c` cells answer CF = 0, and neither `fm_kinit_x` nor
`fm_ondrag_x` reads a flag after the call. `cw_wm_content`'s note said it was
"NOT the cell" for the Bounce worker's 128-byte slice; Bounce is a package
(SPEC.md 14.8) and every caller left is on the UI task. Cycles, 8088: a lock,
an unlock, a yield, a `wm_content`: ~50 clocks more a call (`push ds / push cs
/ pop ds / pop ds`); `thm_desk` and `wm_ondrag` through `api_rs` ~90; the X
cell ~80 - each once per operation or per damaged zone, against a 756 us draw.
`t_stkclass` (worst static chains) green.

### 4. kern_small: `mod_init` written out in the blob - small -10 (`47fd4037`)

kern_small's blob has ~1,250 bytes spare, so kmain_o's far call to
`mod_init_x` is the slot loop itself (20 bytes of blob, `mov word [di+2],
COLD_SEG` a blob word `tools/os88rom.py` re-points, where `mod_disarm` in
`.cold` takes CS). kern_big keeps the far call: its blob has 5 bytes.

## HOT PATHS

None touched. `tools/os88rdiff.py kernel aedadd25 --hot ...` over `sch_isr`,
`sch_switch`, `sch_resume`, `kbm_isr`, `mou_isr`, `mou_p2_isr`, `gfx_blit4`,
`gfx_blit1(_x)`, `font_run(_x)`, `dsk_xfer`, `dsk_runcap`, `dsk_next_clus_x`,
`ui_task` and the API stubs (`api_x`, `api_rx`, `api_rs`, `api_rsc`,
`api_rxc`, `api_rn`, `api_far`): **all `same`, all at an even shift**, both
kernels. `osapi_table` reads "changed" because the `apic_*` labels split it,
which is the label and not a byte. Cells: untouched (offsets are ABI).

## REFUSED

* **Pass 12's ABI wish 1, `OSAPI_MEM_HMA` as an arm of `OSAPI_MEM_CLAIM_DMA_HI`
  / `OSAPI_MEM_FREE`** - handed to this agent, and refused on two findings.
  (a) The DMA_HI arm cannot be keyed the way the wish says: `SOUND.DRV`'s
  POOL (`sbl_pool_get`) also calls DMA_HI with CX = AX, at 4..20 KB, so an
  arm that tries the HMA for a driver's ask would put the pool there -
  movable-declared, addressed from offset 0, which an HMA block (offsets from
  `HB_ORG`, unmovable) is not. A discriminator (a flag in CX's bit 15, a value
  DMA_HI refuses today) makes the arm ~+14 bytes against the 18-byte body and
  6-byte cell it deletes, and SOUND.DRV keeps its two-step claim. (b) The
  cheapest shape that works - both forms on `OSAPI_MEM_FREE` (DX = 0, AX =
  bytes claims; DX >= F000 gives back) - was BUILT and measured: **kern_big
  +6** (`.text` -6, `.cold` +12), kern_small -6, SOUND.DRV ~-7. The primary
  kernel gets bigger; not taken, and the cell stays where it is.
* **`cw_gfx_fill`, `cw_gfx_frame`, `cw_font_run` through their cells** (-12
  both kernels): these are per-ELEMENT calls from cold painters (the file
  manager's rows, a dialog's controls), and `font_run` is on the hot list -
  ~50 clocks a draw call is a different trade from ~50 a lock.
* **`api_rsc` tailing into `api_rs`** (-1): ~15 clocks on every cold rare cell.
* **`mod_init_x` into kern_big's blob** (-10): 5 bytes of blob left.
* **`drv_sysname`, `drv_cfgname`, `hb_s_ptr`, `clk_fbuf` into the images that
  read them**: each is handed to a kernel routine through DS (a file name to
  `dskw_*`, a field to `font_run`), MODULE-SELFCONTAIN-PLAN 5.4's trap.
* **HIBER.DRV back in a 6 KB claim**: needs 267 bytes off its image; none on
  offer in an image three passes have read.
* **`inst_parksafe_set` / `inst_restart_set` sharing a prologue routine**: 44
  bytes either way. **`str_len` reshaped**: every form is 14 or more.
* **kmain's post-blob tail (paint, boot ticks, `cursor_show`,
  `drv_notice_x`) moved into the blob ahead of `mem_unblob`**: it reorders the
  first paint and the Control Panel's notice launch against the blob's
  compaction, and `boot_ticks` would stop counting the unblob - a behaviour
  change for ~60 bytes.
* **`xm_release_rec`'s kern_small call** (9): ksp11 xfile left it for the
  teardown-site rule, which still holds.

## WANTS THE OWNER

1. **HIBER.DRV's claim 6 KB -> 7 KB, for TAKEN 1's -266 resident.** A
   hibernate, a wake and `OSAPI_DRV_SUSPEND`'s two halves (the DOS box) load
   HIBER.DRV for the call and drop it after, so nothing is resident - but each
   of those now needs 1 KB more free heap at that moment, and on a heap with
   between 6 and 7 KB free it refuses where it did not. The image is 5,819
   unpadded against a 6,144 claim and 592 bytes of its own bss (561 of them
   the DOS handoff's record copy), so the other way to keep 6 KB is to move
   that record out of the image's bss - a change to the handoff, not taken.
   If declined, `ae46a3d1` is self-contained and reverts whole (-266 back).
2. **FSX's mode set as an on-demand module** (`fsx_mode` + `fsx_mtab` +
   `fsx_setbios` + `fsx_xcrtc` + `fsx_bmodes`, ~400 resident bytes on both
   kernels, ESTIMATED): it runs once per fullscreen bracket, but a module needs
   the system disk, and a game on a games disk on a one-drive machine would
   then ask for it at mode set.

## WANTS AN ABI BREAK

* **A five-byte rare cell** (`call api_rX / dw target`, the stub banking BP
  itself): -105 `.text` on both kernels (105 cells), ESTIMATED, and a few
  clocks more per rare call. It renumbers the whole table after the first
  rare cell: `PKG_FMT`, every package rebuilt.

## CROSS-FILE

* **wkgui - the display-switch cluster, the largest structural item found**:
  `vid_switch`, `vid_disp_init`, `vid_desk_union`, `wm_pref_take`,
  `vid_equip`, `wm_refit`, `wm_sz_notify`, `vid_ctx_capture`, `vid_blank`,
  `wm_apply_size`, `vid_disp_other`, `vid_avail_test`, `dock_reflow`,
  `wm_pref_now`, `wm_apply_pf`, `dock_geom`, kernel.asm's `desk_rowcalc`
  thunk and `desk_rowcalc_x` (`.cold`) - **~580 bytes on kern_big and ~300 on
  kern_small** that a reference walk finds reached only from the blob,
  `CTRL.DRV`, `EXTD.DRV` and `.ovl`. TAKEN 1's shape applies; the walk does not
  see fall-through entry or code pointers in data, so each needs the reading
  TAKEN 1 got (it is how `ss_open` was found), and kern_big's blob has 5
  bytes, so its boot half needs the room below or a 12th sector.
* **theme - `ovc_buf: times CFG_FBUF db 0`** (driver.inc, `3987ad05`'s hunk):
  163 zero bytes carried in the blob. Anywhere nobits would give the blob back
  163 bytes (it has 5).
* **wkgui - `desk.inc`'s `push ds / pop es` before `cw_drv_pkg_call`** (-2
  big): the X cell does it now.
* **wkfs/wkgui - `ct_` trampolines at two sites**: `ct_cw_wm_clip_test` on
  kern_big, `ct_cw_snd_beep` and `ct_cw_task_yield` on kern_small (-2 each
  there): the trampolines are kernel.asm's, the sites other files'.
* **wkfs - strings sharing a tail**: `sch_s_in` (`'  ', 0`) is
  `fm_s_colsp + 1` (-3 both kernels); `fm_s_root` (`'Disk', 0`) is
  `drv_t_ram + 4` on kern_big (-5).
* **wkgui - `icon_draw_ix`** (150 bytes `.text`) is reached only through
  `cw_icon_draw_ix` from `.cold`; in `.cold` the shim (4) goes.
* **ctrl.inc (no owner this pass) - lines this agent changed**: `cp_thmv_open`
  is untouched; `cp_nitems`, the Sound page's `cdl_tier`, the Drivers page's
  `cdl_load`, the Scheduling radio's `cp_schset`, and `drv_memk`'s two `cs:`
  sites.
* **misc/assoc - `tools/kernsize.py`** prints "no theme for ascw.inc".

## ROWS RUN

Once, after the last code commit (`47fd4037`), with no source edits in
flight: `os88test.py soak -k lzdrv -k drvcall -k drvup -k drvmove -k sndmove
-k sndplay -k hmadrv -k hibernate -k hibernatedrv -k kdreturn -k kdreturnm -k
saver -k xmcheck -k covoxdrv -k covoxcp -k cpup -k cpnames -k rdup -k rdmount
-k fsclass -k netcoexist -k ethernet -k colorpresets -k thm95 -k fmthumb -k
wirezone -k smallboot -k small128 -k heaphi -k deskcalcards --marty-jobs 1`:
**27 ok, 3 FAIL**.

| row | covers | result |
|---|---|---|
| `sndplay`, `covoxdrv`, `ethernet`, `netcoexist` | class drivers loaded at BOOT through the blob's `bdl_` copy, the sound tier at attach | ok |
| `lzdrv`, `drvup`, `drvcall`, `cpup`, `cpnames`, `rdup`, `rdmount`, `fsclass` | class drivers loaded from the Drivers page (`cdl_load`), the page's figures (`drv_memk`, `cp_nitems`) | ok (`lzdrv` stops at `drv_load_at.attach` now) |
| `covoxcp` | the Sound page's tier (`cdl_tier`) | ok |
| `hmadrv` | a driver placed in the HMA by the attach half, the copies' `_hma` | FAIL once (P13 4's known race in its "two halves" check), **ok** alone on re-run |
| `drvmove`, `sndmove`, `heaphi` | driver images moving, claims from the top | ok |
| `kdreturn`, `kdreturnm` | `OSAPI_DRV_SUSPEND`'s two halves through HIBER.DRV (`hdl_`) | ok |
| `saver`, `xmcheck`, `colorpresets`, `thm95` | the OVERLAY loads that stay resident (`ss_open`, `xm_boot`, THMPAL) | ok |
| `fmthumb`, `wirezone`, `deskcalcards` | `cw_wm_timer`, `cw_wm_ondrag`, `cw_drv_pkg_call`, `cw_thm_desk` through their cells | ok |
| `smallboot`, `small128` | kern_small, `mod_init` in the blob | ok |
| `hibernate`, `hibernatedrv` | HIBER.DRV's reload after a wake | **FAIL, PRE-EXISTING**: `os88bisect classify --base aedadd25 hibernate` - BAD at the tip AND at the base, same leg (the Hibernate window is never found: `button1`'s instance list is empty) |

So HIBER.DRV's own expansion is reached by `kdreturn*` and by reading: it
is the same macro text as the two that the rows above drive, and
`hbm_reload_m`'s one change is `call COLD_SEG:drvf_drv_row` before
`hdl_load` (`drv_row_x` keeps AX; CX, the loop's counter, untouched; BX
banked around the loop, as `drv_load_x` used to keep it).

Host side at every commit: `make -j2` (with the fast tier, 47/47),
`make -j2 small`; `make emu` and `make rom` (both ROM arms build: see BASE AND
TIP) at the tip; `tools/stkbalance.py` over every touched file, base against
tip: the same ten (ladder-rung) lines, nothing new; `tests/unit/t_blobruns.py`
(2/2/2/2 calls for the blob, and the 720 KB kernel read back at 11).

## DEFECTS FOUND

* **`tests/hibernate.py` cannot open the Hibernate window at the base** (rows
  `hibernate`, `hibernatedrv`; classified PRE-EXISTING above). Not this
  agent's: found by the row, not fixed. Likely the System menu's layout
  against the row's fixed `HIBER_Y`.
* Nothing in the kernel. One trap for the next reader of a call graph: a
reference walk does not see FALL-THROUGH, and `drv_load_row` is reached from
the resident `drv_load_at` that way - the first reading of this pass had the
whole loader module-only and would have broken the screen saver.
