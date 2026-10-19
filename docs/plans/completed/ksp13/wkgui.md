# ksp13 agent "wkgui" - the window system and drawing, whole-kernel

Kernel size pass 13 (the brief was written as pass 14 and renumbered; it is
`docs/plans/completed/ksp14/BRIEF.md` on this branch). A WHOLE-KERNEL agent:
area `wm.inc`, `ui.inc`, `menu.inc`, `font.inc`, `vga12.inc`, `softgfx.inc`,
`band.inc`, `blank.inc`, `toast.inc`, `dock.inc`, `desk.inc`, `mouse.inc`,
`events.inc`, `vidsel.inc`, `viddet.inc`, `splash.inc`, `spinner.inc`,
`clockw.inc`, bar the hunks `git diff 3bb038f3 aedadd25` gives the concept
agents (desk, theme, misc). Branch `ksp13-wkgui`, cut at `aedadd25` (the brief
on `4833dfef`). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`;
modules are the UNPACKED image (`os88drv.image_unwrap`).

**One more file was touched outside the list, and it belongs to no agent:
`kernel/extmod.inc`** (`EXTD.DRV`, the extended desktop's on-demand module).
It holds window-system code (`wm_fit_box`, `wm_disp_span`, `ui_drag_size`,
`wm_disp_now`) and the two cuts and the landing latch below moved INTO it. No
concept hunk is in it (`git diff 3bb038f3 aedadd25 -- kernel/extmod.inc` is
empty). `kernel.asm`, wkcore's, was NOT edited: the two new entry numbers are
defined beside their callers (`font.inc`, `vga12.inc`) and asserted in the
module's header.

## BASE AND TIP

| | base `aedadd25` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,757 | **92,351** | **-406** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,756 / 5,223 / 38,084 / 5,358 / 336 | 43,342 / 5,231 / 38,084 / 5,358 / 336 | -414 / +8 / 0 / 0 / 0 |
| kern_big `.boot2` / `.ovl` | 5,129 / 5,106 | 5,129 / 5,106 | 0 / 0 |
| kern_big `KERN_SIZE` | 98,816 | **98,304** | **-512** (the image rung, uncrossed: 96 -> 95 steps) |
| **kern_small resident** | 60,260 | **59,832** | **-428** |
| kern_small `.text` / `.bss` / `.cold` / `.lowbss` | 31,847 / 3,063 / 22,482 / 2,868 | 31,445 / 3,059 / 22,460 / 2,868 | -402 / -4 / -22 / 0 |
| kern_small `.boot2` / `.ovl` | 4,364 / 1,480 | 4,364 / 1,480 | 0 / 0 |
| kern_small `KERN_SIZE` | 62,464 | **61,952** | **-512** |
| **kern_emu resident** | 93,016 | **92,610** | **-406** |
| kern_emu `KERN_SIZE` | 99,328 | 98,816 | -512 |
| `EXTD.DRV` image | 1,031 | 1,406 | +375, **inside the 2 KB claim it already took** (1,031 is 7 bytes over the KB - see DEFECTS) |
| every other module and driver | | | 0 |

The rungs are reported because the table asks for them, not because they are
the result: the result is **406 bytes on kern_big and kern_emu and 428 on
kern_small**, every one of them a byte nothing reached or a byte that moved to
where its only reader already was. No functionality changed and no ABI moved.

Per commit, resident, measured by assembling each commit (section lengths off
the NASM map; `kernsize --json` at the tip agrees):

| commit | kern_big / kern_emu | kern_small |
|---|---:|---:|
| `e9d5fbb8` dead and constant arms | -76 | -389 |
| `a1b30271` kern_small's VGA row, MDA table, depth test, the 8s, the zoom byte | 0 | -43 |
| `bef5c874` the two seam cuts are EXTD.DRV's | -330 | 0 |
| `8ebc917a` ss_pub, the landing latch, parity pads | 0 | +4 |
| **total** | **-406** | **-428** |

## TAKEN

### 1. `wm_clip_rect` off the shortcut kernels - kern_big/emu -68, kern_small 0

Its one caller, `desk_zone_redraw`, is `%ifndef OS88_SHORTCUTS`, and
`OS88_SHORTCUTS` is defined on kern_big (and so kern_emu). The routine is now
under the same condition as its caller; kern_small keeps it because it keeps
the caller. SPEC.md's row for it says so. Not on a hot path.

### 2. `font_str` is BOOTPROF's alone - -8 on all three

`bootprof.inc`'s three far calls were its last callers; every shipped kernel
draws a kernel string through `font_str_x` or `font_run`, and the API's
`OSAPI_FONT_STR` door is `apic_font_str`. `%ifdef BOOT_PROFILE`. `make
BOOTPROF=1` still assembles it (the buildmatrix row covers it; not run here,
see ROWS RUN).

### 3. kern_small's PLANAR RESIDUE - kern_small -352, kern_big 0

kern_small has no VGA (SPEC.md 39.27): `[vid_kind]` is a CGA or a Hercules for
the life of the machine, so `[vid_mono]` is 1 and the plane count is 1 before
the first pixel. Pass 9 took the VGA driver off that build; what was left was
the planar arms of routines that are shared, each behind a `[vid_mono]` test
that could not come out the other way. Now `%ifdef GFX_VGA`, with the shared
label falling into the mono body (a label, never an `equ` - SPEC.md 39.27.5):

- the cursor: `cur_put_1`, `cur_get_1` and `cur_move_1`'s planar dispatch, and
  the four planar bodies only they reached (`cur_restoreu`, `cur_saveu`,
  `cur_rect`, `cur_draw`);
- `gfx_1bpp_on`/`gfx_1bpp_off`, `vga_pn`/`vga_pm` and `gfx_sr_tail`'s call;
- `vid_planes_w` and its six readers, all multiplying by one: the save-under's
  `wm_su_bytes`, `wm_su_scrset`, `wm_su_ck`'s depth compare, `wm_su_take`'s
  depth word, `wm_su_edge`'s row/plane extent and `wm_su_merge`'s plane loop
  (whose `.bss` pair `wm_su_nrow`/`wm_su_pext` is kern_big's now, `wm_su_nrow`
  an `equ` of `wm_su_srow` on small), and `menu_drop`'s `mul`;
- `font_ink`'s mono-or-window test and `font_run_x`'s `cmp`/`je .slq`, which
  was never taken;
- `vid_apply`'s planar rseg arm and `vid_depth_set`'s planes writer;
- with them `ss_mins2idle` (-23, `.cold`: kern_small reads no SYSTEM.CFG,
  SPEC.md 51.5, so `[ss_idle]` keeps its assembled `BLK_IDLE` for ever) and
  `ui_tm_open`'s `ui_desc` store (-6: small has no description page).

**Cycles, all faster on kern_small and untouched on kern_big** (8088, the
fetch floor counted): every cursor put, get and move in the mouse ISR loses a
`cmp byte [mem], 0` and a TAKEN `jne`, ~22 + 16 = **~38 cycles a call**;
`font_ink` loses `mov al,[m]` / `or al,[m]` / `jz` not taken, **~37 cycles a
call**; `font_run_x` loses a compare and an untaken `je`, **~26 cycles a run**;
the save-under's six multiplies by one are 118-133 cycles each where they ran.
kern_big's arms are textually identical.

### 4. More of the same on kern_small (`a1b30271`) - kern_small -43

`vid_tab`'s VGA row (18; `vid_tab` an `equ $ - VID_REC` so the walk's
arithmetic is unchanged), `vid_6845t`'s MDA text-mode table (12; reached only
from the VGA's MDA-companion path), `vid_depth_set`'s 4bpp test and
`vid_apply`'s `mov al,[vid_kind]` before it (~10), `vga_m8l`/`vga_m8r` (2) and
`wm_animon` (1; nothing on small sets it). Boot-time and setup paths only.

### 5. The two SEAM CUTS are EXTD.DRV's - kern_big/emu -330 (`.text` -338, `.bss` +8)

`font_ch_cut` (39.14.11, the one glyph cell a display seam crosses) and
`gfx_blit4`'s `.cut` (39.14.7.2, the straddling blit cut at the seam) are now
`ext_fcut` and `ext_bcut`, EXTD.DRV entries 12 and 13 (`EXT_NENT` 14). Both
can only run while a second display is up, and a second display exists only
while EXTD.DRV is mounted (SPEC.md 39.19.6's invariant: `[vid_ndisp]` > 1 and
`[gfx_dnest]` = 1 only then) - so the resident code was the kind 39.19.6 was
written to remove. What stays resident is a stub each: `font_ch_spill` tests
`[gfx_dnest]` first and `EXTCALL`s, the blit's `.cut` is three instructions.
Each knob's arm is kept: `NOSEAMCUT=1` and `NOBLITCUT=1` assemble the entry as
`ext_refuse` (`stc`/`retf`), and the header asserts the numbers with `%error`.
`kernel.asm` was not touched (the entry numbers are defined beside their
callers). SPEC.md 39.19.6's table and paragraph name the two new rows.

- **Heap: nothing.** EXTD.DRV was already 1,031 bytes and took a 2 KB claim
  (`mem_bytes_kb_x` rounds up); 1,406 is inside the same 2 KB, so a machine
  that Extends pays no more memory and a machine that never Extends pays 330
  bytes less for ever.
- **Cycles, one card (every machine that never Extends): FASTER.** A glyph
  hanging off a display's edge used to enter `font_ch_cut` and refuse inside
  it; it now refuses on `font_ch_spill`'s first `cmp byte [gfx_dnest], 1` /
  `jne`, ~83 -> ~38 cycles a refused cell. The blit stub is reached only by a
  block that straddles, which one card cannot have.
- **Cycles, extended desktop: SLOWER, and only on the seam.** A seam cell pays
  the far call into the module, two `EXK` hops back into the kernel
  (`font_ch_seam`, `vid_ctx_act`) and the return: **~+900 cycles per seam
  cell**, of which there is at most ONE per text run (the seam is a multiple
  of 8, 39.14.11). A straddling blit pays the far call plus two or three `EXK`
  hops once per BLIT, against a body measured in thousands. Nested draws on
  that path run ~10 bytes deeper on the task stack (far return, `exk_go`'s
  frame) - well inside the 64-byte design floor's margin, and on the
  extended-desktop path only.
- `stkbalance` is cleaner by one finding (`font_ch_cut`'s tail `jmp kret_bp`
  at depth +7 is gone with it); `extmod.inc` walks with 0.

### 6. `ss_pub` and the landing latch (`8ebc917a`) - kern_big 0, kern_small +4

- `ss_pub`'s `ss_modes`/`ss_secs`/`ss_mins` are kern_big's now (kern_small:
  -3; nothing there read or wrote them once `ss_mins2idle` went).
- `wm_lf_win`/`wm_lf_disp` (SPEC.md 39.16.4.2) were resident `.text` read and
  written ONLY by EXTD.DRV's `ui_drag_size` and `wm_disp_now`. They live in the
  module's image now, `[cs:]` on each access (kern_big -3). A fresh load is
  exactly when "nothing armed" (0) is right, so the initialiser is still true.
- **Both savings went back as PARITY PADS**, and the commit says so: kern_big
  +3 (`wm_lf_pad`, a `nop` after `.hkdonej`, and one jump the move relaxed),
  kern_small +7 (`db 0` in `ss_pub`'s place, a `nop` after `ss_mins2idle`'s
  arm, pads in `font_ink`, before `font_run_cell`, after `vid_apply`'s
  `jmp vid_pop7`, in `cur_draw`'s arm and before `gfx_sub_st`). Never
  executed. With them **no hot routine is at an odd shift against the base on
  either kernel** (`gfx_blit4`, `gfx_blit1_x`, `font_run_x`, `font_char`,
  `font_run_cell`, `mou_isr`, `mou_apply`, `cur_move*`, `cur_put_mono`,
  `cur_get_mono`, `kbm_isr`, `sch_isr`, `sch_switch`, `ui_task`,
  `sw_blit_row`, `vga_blit_span`, `gfx_fill_raw`, `sw_fill`, `dsk_xfer`,
  `api_far`, `gfx_points`, `wm_clip_rows`, `dsk_next_clus_x`, ...). The batch
  is kept rather than dropped because without it batches 1-3 left kern_small's
  cursor and FAT walk odd.

## REFUSED

- **The defensive `cld`s.** About twenty non-ISR routines in the area `cld`
  before a string op although every documented entry already has DF = 0.
  ~-20 bytes, and the robustness they buy is against a PACKAGE that returns
  with DF set - which is a functionality question, so it is in WANTS THE OWNER
  and not taken.
- **`cur_move_multi` and `gfx_disp_run` into EXTD.DRV.** Both are
  extended-desktop code, but one runs in the mouse ISR and the other on every
  primitive on a straddled window; a far call and the `EXK` hops back on those
  paths is the cost 39.19.6 kept them resident to avoid.
- **The `vid_switch` family into CTRL.DRV and the blob.** Priced: the shims
  each entry would need eat nearly all of it (~net 0).
- **`kbm_move` / `mou_apply` sharing a tail.** Needs a `call` inside the ISR;
  a byte or two against cycles and stack on the hottest path in the kernel.
- **Factoring the `GFXCLIP` prologue** and **sharing the font cell clip
  prologue.** Per-primitive and per-cell work; a call each is the cost.
- `vid_equip` (-1), `wm_an_n`'s `idiv` by a constant (-1): one byte each, on
  paths whose cycles would have to be re-proved; not worth the review.
- Shrinking `WSU_IMG` on kern_small: a heap claim, not resident.
- Every relaxed jump left in the area is on pass 11's deliberate list.

## WANTS THE OWNER

- **`rect_get`/`rect_put` S2 + S3** (docs/plans LAST-DROP-BYTES 7.10, already
  HELD there): **-138 bytes on both kernels**, for cycles on every save-under.
  Still held; nothing here changes the trade.
- **The defensive `cld`s** (above): **~-20 bytes on all three kernels**, for
  giving up DF = 0 when a package's callback returns with DF set.

## WANTS AN ABI BREAK

None.

## CROSS-FILE

For **wkfs** (`files.inc`, `icons.inc`), kern_small `.cold` reached by nothing
or only from `%ifndef KERN_SMALL` callers:
`fm_bar_gate_x` 64 (its only caller is in `ui.inc` under `%ifndef
KERN_SMALL`), `fm_c_uncto` 50, `fmv_icostale_but` 29, `fm_ncpye` 13,
`dwf_dskw_write_sys` / `dwf_dskw_append_sys` 5 + 5; `dsk_fatw_drop` 51 looked
the same and wants checking against its far-call references before it is
taken. `fm_s_root`'s `'Disk',0` is the tail of `drv_t_ram`'s `'Ram
Disk',0` (-5 by aliasing). `ico_col` (`icons.inc`) is unreferenced `.bss` on
kern_small.

For **wkcore** (`kernel.asm`, `clock.inc`, `instance.inc`):

- the boot-only `cw_*` shims `cw_menu_relayout`, `cw_vid_ctx_ptr`,
  `cw_vid_disp_init`, `ovw_dock_geom`, `ovw_font_run_x` through the `spw_near`
  door: ~-4 resident each, ~-20, for ~+5 blob bytes each;
- `osapi_video`'s bpp on kern_small is always 1: `mov dh, 1` (-8 small);
- unreferenced kern_small `.bss`: `clk_rb`, `clk_rbin`, `clk_rpep`,
  `clk_rp24` (`clock.inc`), `inst_parkreq` (`instance.inc`);
- `ui_svc_open` and `cw_ui_svc_open` fold into one (-2).

For **theme**: `vga_xorm` is a byte of `.bss` nothing reads on kern_small; its
line is a theme hunk, so it is not touched here.

## ROWS RUN

Once, after the last code commit (`8ebc917a`), at `--marty-jobs 1`:
`os88test.py soak -k smallboot -k smalllaunch -k tmsmall -k paint1small -k
dispseam -k blitcut -k dispsave -k curshape -k curbusy -k saver -k deskclip
-k dispnp`, then `os88test.py full -k small128`.

| row | covers | result |
|---|---|---|
| `smallboot` | kern_small on every 1bpp adapter, the planar residue gone | ok 174.2s |
| `small128` | kern_small on its 128KB floor | ok 8.2s |
| `smalllaunch` | packages launching on kern_small | **FAIL, then ok 128.7s** (below) |
| `paint1small` | `gfx_blit1` and the cursor over Paint on kern_small | **FAIL, then ok 15.2s** (below) |
| `tmsmall` | Task Manager small arm, `ui_tm_open` | ok 46.3s |
| `dispseam` | `ext_fcut`: the seam cell, and `NOSEAMCUT=1`'s A/B | ok 305.2s |
| `blitcut` | `ext_bcut`: the straddling blit, and `NOBLITCUT=1`'s A/B | ok 430.0s |
| `dispnp` | the extended desktop, the landing latch | ok 40.9s |
| `dispsave` | the save-under, `vid_planes_w` | ok 32.5s |
| `curshape` / `curbusy` | the cursor arms | ok 52.7s / 19.2s |
| `saver` | `ss_pub`, `ss_mins2idle` on kern_big | ok 25.2s |
| `deskclip` | the desk zone redraw beside `wm_clip_rect` | ok 155.4s |

**The two failures were the HARNESS and not the kernel**: `os88marty.no_saver`
wrote `[ss_mins]` by name on every kernel, and kern_small no longer has the
symbol (TAKEN 6). Nothing on kern_small ever read that byte - `ss_mins2idle`
is kern_big's, and it was unreferenced on small at the base too - so the
write was already a no-op there and `[ss_idle]` = 0 is the whole setting. The
harness now writes `[ss_mins]` only where the symbol exists (`tools/os88marty.py`,
the one tool this pass touched), and both rows pass
on the same kernel. Not run: `buildmatrix`; the arms it would build that
this pass touched - `BOOT_PROFILE` on both kernels (now `font_str`'s home)
and `NOSEAMCUT` + `NOBLITCUT` together - were assembled by hand at the tip,
and `dispseam`/`blitcut` built and booted each knob alone. Host-side, the fast tier and `make -j2`/`small`/`emu` ran at
every commit.

## DEFECTS FOUND

- **EXTD.DRV was 1,031 bytes at the base**, 7 over the KB, so it has been
  taking a 2 KB claim while SPEC.md 39.19.6 still quoted the second wave's
  1,023. The section states the real size now (1,406, still 2 KB): the claim
  did not change in this pass, it was already 2 KB.
- Two stale comments said kern_small carried `ss_*` and `wm_animon` across a
  SYSTEM.CFG it never reads. Corrected where the code moved.
- **`tools/os88modcost.py`'s cross-check fails** on this base (`.ovl`: 3,437 of
  5,106 bytes attributed), so the tool stops before printing. Not fixed here;
  measured around it with a local copy that skips that one check.
- **`tools/os88rdiff.py kernel <base>` fails on kern_big** for this base: its
  `git archive` tree lacks `drivers/thmpal/thmrows.inc`, which `kernel.asm`
  now includes. Measured with `--lst` against listings from a private
  worktree instead.
- **`os88marty.no_saver` wrote a kernel symbol whose write did nothing** on
  kern_small (ROWS RUN). Fixed in the harness, where a missing symbol now
  means "nothing to put the period back".
