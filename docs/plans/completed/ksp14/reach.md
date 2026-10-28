# ksp14 agent "reach" - reachability: which population of callers can reach each resident routine

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`), the first
general (whole-kernel) agent. Concept: for every resident routine (`.text`,
`.cold`) and resident datum, which POPULATION of callers can reach it - and if
only a module (`CTRL.DRV`, `EXTD.DRV`, `STIKMENU.DRV`, ...) or only the boot
(the blob, `.ovl`) can, it can live there, moved or written once as a macro
and expanded where its callers are. Branch `ksp14-reach`, base `e1e7cebf`
(the merged concept agents). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`. Ownership this pass: `disk.inc`, `diskw.inc`,
`files.inc`, `filecp.inc`, `desk.inc`, `desksc.inc`, `assoc.inc` are the
files agent's and `driver.inc`, `xmem.inc`, `memory.inc`, `hiber.inc` and the
API table the api agent's - nothing in them was edited here; what reaches
into them is under CROSS-FILE.

## THE INSTRUMENT

A call graph built the way pass 13 said it must be: every kernel is assembled
with `nasm -Le -Lf -l` and a `[map all]` map, `tools/os88rdiff.py`'s listing
parser files each EMITTED line under the top-level routine and the section it
really assembled into (so a dead `%if` arm names nothing), and every
identifier on that line that is a global label - or an `equ` whose right-hand
side names one, `cw_x equ apic_x` and `X equ mod_fp + n` alike - is an edge.
`call far COLD_SEG:label`, `dw label` tables, `mov bp, label` doors and
`call cp_kgo / dw label` hops are all edges by that rule. A routine whose
previous routine does not end in `jmp`/`ret`/`retf` is given a fall-through
edge. Then a fixpoint: a resident symbol whose every caller is non-resident -
or a resident symbol already in the set - is reachable only from the boot or
a module. Every candidate it named was then READ before anything moved (the
graph's sizes are label spans, so `equ`s inside code and labels sharing an
address read wrong - `mod_fp`'s "44" is `CPFP`'s block, `cw_wm_snap`'s "110"
is the API table under an alias; both false).

**What the graph says about the kernel**: pass 13's two structural moves
and its dispsw work have mined most of it. On kern_big the whole set the
fixpoint returned, with the false positives taken out, was ~560 bytes, and
~230 of those are in the other two agents' files.

## BASE AND TIP

| | base `e1e7cebf` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,598 | **92,268** | **-330** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,503 / 5,388 / 38,013 / 5,358 / 336 | 43,175 / 5,396 / 38,003 / 5,358 / 336 | -328 / +8 / -10 / 0 / 0 |
| kern_big `KERN_SIZE` | 98,816 | **98,304** | -512 (the image rung) |
| kern_big blob (the real one, `kernel.map`'s `.boot2`) | 6,307 of 6,656 | 6,595 of 6,656 | +288 (13 sectors, unchanged) |
| kern_big `.ovl` | 5,118 of 5,120 | 5,114 | -4 |
| **kern_small resident** | 59,009 | **58,951** | **-58** (`.text` -58) |
| kern_small `KERN_SIZE` / blob | 61,440 / 4,592 | 61,440 / 4,592 | 0 |
| **kern_emu resident** | 92,867 | **92,539** | **-328** |
| kern_emu `KERN_SIZE` | 99,328 | 98,816 | -512 |
| kern_emu blob | 13 sectors | **14 sectors** (6,695 of 7,168) | TAKEN 7 |
| **kern_mega resident** | 102,901 (`.text` 47,181) | **102,569** (`.text` 46,851) | **-332** (`.mega` 14,698 unchanged) |

| image | base | tip | claim |
|---|---:|---:|---|
| `CTRL.DRV` kern_big (`.modc` + `.modu`, + 164 bss) | 14,346 | 14,624 | 15 KB, unchanged (14,788 of 15,360) |
| `CTRL.DRV` kern_small | 4,946 | 5,004 | 5 KB, unchanged (116 spare) |
| `EXTD.DRV` | 1,518 | 1,837 | 2 KB, unchanged |
| `STIKMENU.DRV` | 14,778 | 14,780 | unchanged |

## TAKEN

Hot paths: none touched. `hotpar` is green at every commit on both kernels
(one parity `nop`, item 1). `tools/stkbalance.py` over every file touched,
base against tip: the same six findings, none new.

### 1. `vid_apply` is the boot's, the panel's and EXTD.DRV's (`f9ee15a3`) - kern_big .text -206

Nothing resident calls `vid_apply` on kern_big. Its callers, each read: the
splash's first tick (`spl_chrome`, through `spw_near`), `vid_init` (blob),
the blob's `bvs_switch`/`bvs_disp_init` (`VSW_SWITCH`/`VSW_DISP`, expanded in
`driver.inc`'s blob block), `CTRL.DRV`'s `vid_switch`/`vid_disp_init`, and
`EXTD.DRV`'s `ext_kapply` (the fullscreen restore of an extended desktop).
Nothing falls into it, nothing jumps into its interior, no table holds it.

So it is `viddet.inc`'s `VID_APPLY %1, %2` macro, the `DRV_ATTACH`/dispsw
shape, expanded three times on kern_big: `bvs_apply` (blob, beside
`vid_init`), `vid_apply` (CTRL.DRV, beside `VSW_SWITCH` in `vidsel.inc`) and
`ext_apply` (EXTD.DRV). `VSW_SWITCH`/`VSW_DISP` call `%1_apply`, so the
blob's copies in `driver.inc` reach `bvs_apply` with no edit to that file.
kern_small keeps the one resident copy, `VID_APPLY vid, call` followed by
its `jmp vid_pop7`, and assembles **byte-identical** (`cmp` of
`build/smallk/kernel.bin`).

An image's copy differs from the resident body in exactly three places:
`push ds / pop es` for `KES` (DS = KERNEL_SEG at every caller - the data
alias on kern_mega, which is what `KES`'s `[cs:k_dsel]` loads), `gfx_nextrow`
written out (its operands are `[cs:]`, the kernel's CS - three instructions,
FASTER than the call), and its own seven pops (`vid_pop7` is `.text`'s).
`vid_depth_set` stays resident (`vid_ctx_act`'s too, and its own header's
"one writer") and is reached through the image's hop: `KSWK` in CTRL.DRV,
`EXK` in EXTD.DRV, and in the blob `BSPW` - `spw_near`, because the
splash's first tick runs before the end of `.text` (where `cw_kretf`, every
`KGO_PAIR`'s return, lives) is aboard, which `spw_near` is by construction.

Costs: blob +215, CTRL.DRV +220, EXTD.DRV +222; no claim moved. `.text`
-207 left an odd hole ahead of the hot run, so one `nop` in its place
(kern_big only, never executed). `tests/ovlrefs.txt` registers `bvs_apply`
(the splash's reference into the blob's half). SPEC.md 39.3.1 has the
paragraph.

### 2. `mod_init` written out in kern_big's blob (`33901dfb`) - kern_big .cold -10

wkcore REFUSED this in pass 13 for one reason, "5 bytes of blob left"; the
blob had 349 after the vesa agent's thirteenth sector, so the reason no
longer held. kern_small's loop in `kmain_o`, now on both kernels; `mod_init_x`
is gone. Blob +15.

### 3. `wm_apply_size` and `wm_sz_notify` written out at their callers (`33901dfb`) - kern_big and kern_small .text -50

`wm_apply_size`'s one caller is `WM_REFIT` (CTRL.DRV, and the blob's
`bvw_refit`); `wm_sz_notify`'s are `WM_REFIT` and EXTD.DRV's `ui_drag_size`.
No resident caller on any kernel. The replay is written into `WM_REFIT` (its
one caller, so no call/ret at all) and the notice is `WM_SZNOTE %1`, a macro
expanded in place at its three sites, reaching `wm_nat_take_wh`,
`wm_pref_now`, `wm_fit`, `wm_geom` and `wm_pkgcall` through each image's
hop. FLAGS are no longer preserved across either: every caller was read and
none reads a flag after them (`WM_REFIT`'s `add`/`loop`, `ui_drag_size`'s
`xor ax, ax`). Blob +58, CTRL.DRV +58 (kern_small's too: 5,004 of 5 KB),
EXTD.DRV +28. `wm_apply_pf`, a label nothing jumped to any more, went (it
would have been walked by `stkbalance` as an entry). SPEC.md 11.100.9 and
39.11.2 say where the bodies are.

### 4. `ui_post_cmd` written at its one caller; `cw_dock_band`'s three callers take their own hop (`0c72ed26`) - kern_big -12, kern_small -8

* `ui_post_cmd` (8): its one caller is STIKMENU.DRV's `stm_pick` (posting
  `CMD_STIK`). Its two stores are written there, DS = KERNEL_SEG as the far
  call already required. STIKMENU.DRV +2.
* `cw_dock_band` (4): `DOCK_GEOM`'s blob and CTRL.DRV copies and EXTD.DRV's
  `ext_e_fsx`. `DOCK_GEOM` takes the hop as a parameter: `KSWK` in the
  panel, `BDKGO` in the blob (a `bvs_kgo` hop defined in `dock.inc`'s
  `.boot2` block, because `driver.inc`'s `BSWK` comes later in the source;
  `ovl_dock_geom` runs from `kmain_o` through `spl_gate`, long after the first
  tick), `EXK` in EXTD.DRV. Five bytes a site either way.

### 5. `ui_drag_dead` is EXTD.DRV's `EXT_DEAD`, and `ui_drag_phase`'s second-display arm `EXT_PHASE` (`5c351998`, `b70309aa`) - kern_big .text -56, .bss +8

The population here is "an extended desktop", which exists only while
EXTD.DRV is mounted (SPEC.md 39.19.6's invariant). A dead zone needs two
displays; so does a phased drop landing on no card. Both run ONCE, at a
drag's release, after `ui_track` returns - not in the tracking loop.

**This revisits a recorded refusal, and says why.** SPEC.md 39.19.6's second
wave refused "`ui_drag_dead` and `ui_drag_phase`, on the drag" as a path
question. On one display `ui_drag_dead`'s `EXTCALL` into `mod_gone`
(`call far [m]` 43 + `stc`/`retf` 36 cycles on the 8088) costs what the
resident near call, its `[vid_ndisp]` compare, branch and `ret` cost (~81):
cycle-neutral. The phase arm's `EXTCALL` is ~37 cycles more than its
compare and branch, once per drop. An extended desktop pays a far call and
one or two `EXK` hops (~400-700 cycles) once per drop, against the repaint
the drop starts. `ext_e_dead`/`ext_e_phase` ask `ext_one` themselves (the
image stays mounted over a collapsed desktop). `ui_drag_phase` itself, and
the other kept arms (`wm_disp_of`/`wm_disp_xw`, `wm_fs_vis`, `fsx_surf`) stay
resident: those are per-draw or per-paint. `EXT_NENT` 14 -> 16 (+8 `.bss`,
the two slots). EXTD.DRV +69. SPEC.md 39.19.6's table has rows 14 and 15.

### 6. `cw_vid_ctx_capture` goes (`b1f940b7`) - kern_big .text -4

Its callers were EXTD.DRV's Extend (two sites) and the boot overlay's
`vid_ctx_init`. EXTD.DRV takes `EXK` (five bytes either way); `vid_ctx_init`
reaches `vid_ctx_capture` through `spw_near`, as it already reached
`vid_ctx_ptr` - three bytes more, paid for inside it: ES from DS rather than
from `KERNEL_SEG` through CX, no CX bank (nothing in it touches CX), and the
`xor al, al` before the capture (AX is already 0). `.ovl` -4 net.
`tests/cswrites.txt`'s `vidsel.inc` line is lowered by the ratchet's own
rule (2 -> 1 pairs).

### 7. kern_emu takes its extra blob sector back (`aa7c3699`) - kern_emu 13 -> 14 sectors

Items 1-3 leave kern_big's thirteen sectors 61 bytes spare; kern_emu's
`vmm_boot_x` is ~100 more, so kern_emu's blob no longer fit. It is
`BOOT2_KNOBSEC`'s arm for `KERN_EMU` again - exactly what it was until the
vesa agent's thirteenth sector this pass took it away - and only an emulator
boots kern_emu (SPEC.md 9.11.7). kern_big, kern_small and the shipped images
are untouched; SPEC.md 9.11.2.1 says so.

### 8. kern_small: `vid_apply` and the button leave resident - TAKEN AT THE OWNER'S WORD (`0e2eb5cf`, `4a0baaeb`) - kern_small -494

WANTS THE OWNER 1, approved after the merge in the owner's words: *"500b
resident for 1kb control panel is well worth paying."* Measured on the merged
tree (`5fc79571`, kern_big's and files' work together), all assembled:

| | merged `5fc79571` | tip | delta |
|---|---:|---:|---:|
| **kern_small resident** | 58,810 | **58,316** | **-494** (`.text` -196, `.cold` -298) |
| kern_small `KERN_SIZE` | 61,440 | **60,928** | -512 |
| kern_small blob | 4,592 of 5,632 | 4,800 | +208 (11 sectors, unchanged) |
| kern_small **`CTRL.DRV`** (unpacked image, no bss on this kernel) | 4,946 + 58 = 5,004, a 5 KB claim | **5,496**, a **6 KB claim** | +492; **648 bytes of the 6 KB left** |
| kern_small `FDLG.DRV` | 1,284 + 16 bss | 1,586 + 16 bss | +302, still 2 KB (446 left) |
| kern_big, kern_emu, kern_mega | | | **byte-identical** (`cmp` of `build/kernel.bin` at one commit; the changes are `KERN_SMALL`/`OS88UI_KBTNIMG`-gated or a macro expanding to the same bytes) |
| every package and driver image (`build/*.o88`, `*.drv`, `*.bin`) | | | **byte-identical** (md5 over all of them, before and after) |

* **`vid_apply` (-196, and its `jmp vid_pop7` and parity nop, an even
  200)**: kern_small expands `VID_APPLY` as `bvs_apply` (blob: the splash's
  first tick and `vid_init`) and `vid_apply` (CTRL.DRV: Set Primary), exactly
  as kern_big. No kernel has a resident copy now, so the macro's resident arm
  (`KES`, `call gfx_nextrow`, `jmp vid_pop7`) and the `vid_pop7` label went.
* **The button (-298 `.cold`)**: `os88ui_btn` (222), `os88ui_kbtn` (49),
  `os88ui_btn_f` (4) and `os88ui_bhit` (23) had no resident caller on
  kern_small - the callers are CTRL.DRV's `cp_bemit` and `sd_button` and
  FDLG.DRV's `fdlg_drawbtn` and hit test. **Pass 13's refusal was "the
  include cannot be assembled twice into one image pair"**, and the answer is
  that it still is not: `apps/os88ui.inc` writes the three bodies ONCE as
  macros (`OS88UI_BTN_BODY`, `OS88UI_KBTN_BODY`, `OS88UI_BHIT_BODY`) and
  expands them in place for every package and for kern_big - byte for byte
  what they were - while kern_small (`OS88UI_KBTNIMG`, set by `fdlg.inc`)
  expands them after the include into CTRL.DRV (`os88ui_btn`, `os88ui_kbtn`)
  and FDLG.DRV (`fdb_btn`, `fdb_kbtn`, `fdb_bhit`) under `OS88UI_KFAR`.
  `UI_RUN` and `UI_WIDTH` gained the far arms `UI_FRAME`/`UI_FILL`/`UI_PEN`
  already had (`cw_font_run`, `font_width`). `os88ui_krect` and the staging
  record stay in `.bss`: the body reads them through DS, KERNEL_SEG in both
  images. `tests/unit/t_btnrules.py` no longer counts a `*_BODY` line as a
  call, and `tests/btnsites.txt` says ctrl.inc and shutdown.inc carry one
  site per kernel. SPEC.md 38.0 and 39.3.1 say where the bodies are.
* `make rom-small` builds: the ROM window holds the bigger CTRL.DRV and
  FDLG.DRV with **4,245 bytes** spare.

**Rows** (one pass, `--marty-jobs 1`): `small128`, `smallboot`,
`fdlgsmall`, `fdlgdrop`, `dispclose-small`, `romsmall` (kern_small);
`btncp`, `btnall`, `fdlgup`, `fdlggrey`, `cpup`, `shutdown` (kern_big's
buttons, unchanged bytes) - **12/12 ok**. `tests/shutdown.py --small` (the
Control Panel's Shut Down card on the 128 KB CGA: CTRL.DRV's `sd_button`
through its own `os88ui_kbtn`): **ok**. **Set Primary on kern_small** has no
row (xfile's pass-13 reason), so a scratch harness drove it on
`os8088_5150_both_gla` - CGA -> Hercules -> CGA through CTRL.DRV's
`vid_apply` and its buttons: `[vid_kind]` 2 -> 1 -> 2, the geometry 640x200
-> 720x348 -> 640x200, the pointer homed each time: **PASS**. **Broken on
purpose**: FDLG.DRV's `fdb_bhit` made to answer "outside" always -
**`fdlgdrop` went red**; restored, green.

## REFUSED

* **`vid_ctx_capture` (25) out of resident**: it would need a copy in the
  blob (~49 bytes - more than the 61 left once a cross-file item takes any),
  CTRL.DRV and EXTD.DRV, and the boot overlay's `vid_ctx_init` would need one
  too, with `.ovl` at 5,114 of 5,120. Blocked by room, not by reach: with
  `ovc_buf` as nobits (CROSS-FILE) the blob half becomes possible.
* **`vid_tab`'s four constant rows (72) into the three `vid_apply` copies**:
  the VESA row must stay DS-addressable (VESA.DRV is handed its address,
  `VXB_TABROW`, and writes it), and the other four would cost ~80 a copy -
  the blob has 61. A `cs rep movsw` would be shorter and is refused outright
  (the 8086/8088 loses a segment override on a REP string op after an
  interrupt).
* **`wm_disp_of`, `wm_disp_xw`'s arm, `wm_fs_vis`'s arm, `fsx_surf`'s arm,
  `cur_move_multi`, `gfx_disp_run`** into EXTD.DRV: priced (`wm_disp_of` -41,
  the arms ~-10 each) and refused for SPEC.md 39.19.6's reason, which holds
  for these: `wm_bord` reaches `wm_disp_of` under every `OSAPI_WM_CONTENT`,
  `wm_fs_vis` is under every bar and dock paint, `fsx_surf` per frame.
* **`cw_vid_disp_of` through EXTD.DRV's `EXK`** (-4): xfile's pass-13
  refusal stands (`OSAPI_WM_DISPLAY`'s cycles).
* **CTRL.DRV's pure scratch bytes into its image** (`cp_probe`, `cp_ctlid`,
  `cp_lfull`, `cp_only`, `cp_skip`, `cp_tfull`, `cp_snd_err`, `cp_dmpar`: 8
  resident bytes): fifty accesses each gain a `cs:` prefix, and every WRITE
  on kern_mega becomes `CSDO`'s seven bytes, for 8. The panel's PERSISTENT
  state (`cp_sel`, `cp_tsel`, `cp_vsel`, `cp_ltop`/`cp_lsel`, `cp_dtop`,
  `cp_dview`) is behaviour - it survives a close by design - and is not
  movable at all.
* **A 14th blob sector on kern_big** to make room: `t_blobruns --sectors 14`
  prices it at one more `int 13h` for the blob AND one more for the kernel
  read on both 360 KB and 720 KB disks (~0.4-0.8 s of a 5150 boot). Not
  taken; it is in WANTS THE OWNER with what it would buy.
* **The boot's `bvw_refit` as a bare `ret`** (it walks `wm_wins` before any
  window exists - drv_boot and vx_boot_x run ahead of `as_boot`): true by
  reading, but a claim about boot order nothing checks, for ~150 blob bytes.
* The rest of the fixpoint's output, read and dismissed: the ISRs `.ovl`
  installs (`kbm_isr`, `mou_p2_isr`, `sch_idle_body`); the published debug
  spans (`clk_dbg_*`, `mou_idn`/`mou_idb0`/`mou_idlast` inside SPEC.md
  9.4.2's span, `fdd_dbg_*`, `vid_hprobe`); `clk_*` probe scratch (already
  overlaid on `clk_str`); `cp_dm*` (already `cp_sbuf`'s tail); `THM_CROW`
  (an `equ` into `thm_tab`); `vesa_row`/`dock_*` (state the panel, the dock
  and the boot share); `drv_cfgname`/`drv_sysname`/`hb_s_ptr`/`cp_s_dsdsk`
  (DS-passed names and messages, MODULE-SELFCONTAIN-PLAN 5.4's trap); the
  `ovw_*` shims (knob- or emu-only already).

## WANTS THE OWNER

1. **TAKEN at the owner's word after the merge - TAKEN 8: -494 measured.**
   **kern_small: `CTRL.DRV`'s claim 5 KB -> 6 KB for ~-500 resident bytes**
   (ESTIMATED from symbol spans). On kern_small two bodies are reached only
   from the boot and the on-demand images: `vid_apply` (196: the blob and
   CTRL.DRV's Set Primary) and the kernel's button `os88ui_btn` with
   `os88ui_kbtn`, `os88ui_btn_f` and their `.bss` (~300: CTRL.DRV and
   FDLG.DRV only). CTRL.DRV small has 116 bytes of its 5 KB, so either one
   tips it to 6 KB. With 6 KB both fit (~5,540 of 6,144), the blob takes
   `vid_apply`'s boot copy (~1,250 spare there) and FDLG.DRV's 2 KB takes
   its button copy. The trade is pass 13's HIBER.DRV/CTRL.DRV kind: the panel
   needs 1 KB more free heap to open, on the 128 KB machine. xfile's pass-13
   refusal of the button ("the include cannot be assembled twice into one
   image pair") is the work this needs, and it is why this is an estimate.
2. **DECLINED by the owner (boot time); api takes the free `ovc_buf`
   alternative.** **kern_big: a 14th blob sector, ~-200 to -270 resident** (ESTIMATED),
   at one more `int 13h` for the blob and one more for the kernel read on
   360 KB and 720 KB floppies (`t_blobruns --sectors 14`; ~0.4-0.8 s of a
   5150 boot). It would hold `desk_rowcalc_x` (-100) and `vgcf_boot` (-21)
   beside `exf_sync` (CROSS-FILE), `vid_ctx_capture` (-29) and `vid_tab`'s
   constant rows (-72). The free alternative for most of it is `ovc_buf` as
   nobits (CROSS-FILE, api).

## WANTS AN ABI BREAK

None. Nothing frozen moved: no cell, no module or driver header, no
SYSTEM.CFG key. `EXT_NENT` and EXTD.DRV's entry table are a kernel <->
module interface `os88mod.py` checks on both halves.

## CROSS-FILE

In priority order, with what each needs and costs. The blob has **61 bytes**
spare on kern_big after this branch.

* **api - `exf_sync` into the blob and CTRL.DRV, -46 kern_big**: `exf_sync`
  is `extmod.inc`'s (reach could write the macro), and its callers are
  CTRL.DRV's `cp_vid_click` (two sites) and `driver.inc:5607`, `drv_boot_x`'s
  `call COLD_SEG:exf_sync` in the blob. Written once as a macro, expanded as
  a CTRL.DRV copy and a blob copy (`bxf_sync` in an `extmod.inc` `.boot2`
  block), with that one `driver.inc` line becoming a near call. Blob +~46
  (fits the 61), CTRL.DRV +~46 (630 spare). Not taken here because the blob
  call site is the api agent's line.
* **api - `ovc_buf` as nobits** (`driver.inc`, 164 zero bytes of blob): with
  `mem_init`'s reserve covering it (xfile's pass-13 WANTS 3), it is room for
  `vgcf_boot` and `vid_ctx_capture` without a 14th sector.
* **files + api - `vgcf_boot` into the blob, -21 kern_big**: `desksc.inc`'s
  21-byte `.cold` body has one caller, `driver.inc:5574` in `drv_boot_x`
  (blob). Blob +~17.
* **files - `desk_rowcalc_x`, -100 kern_big** (`desk.inc`, `.cold`): callers
  the blob (`kmain_o`, `bvs_switch`, `bdk_reflow`), CTRL.DRV (`cp_vid_click`,
  `vid_switch`, `dock_reflow`) and the boot overlay's `desk_init`. Needs a
  blob copy (no room), a CTRL.DRV copy and an answer for `.ovl` (full). On
  kern_small the same plus `desk_reflow` (53) and `desk_live` (8): ~-123
  there, and kern_small's blob has the room - but CTRL.DRV small has 116.
* **api/files - the `drvf_*`/`dskf_*`/`dkf_*` far shims with no resident
  caller**, 4 bytes each: `drvf_drv_call`, `_load_at`, `_mounted`,
  `_release`, `_row`, `_unload`, `_vol_back`, `_vol_bank`;
  `dskf_disk_read`, `_disk_write`, `_dsk_vol_row`; `dkf_dsk_vol_fixed`
  (48 kern_big). Callers are the blob (`BSWK`, 5 bytes = the far call's 5),
  CTRL.DRV (`KSWK`/`KSWC`, the same), and HIBER.DRV, DISKOPS.DRV,
  FORMAT.DRV, STIKMENU.DRV and the `.modc` core, which have no hop: each
  needs a `KGO_PAIR` (~54 image bytes) first. `drvf_drv_call` takes BP as an
  input (xfile pass 13), so its hop must not spend BP - `KGO_PAIR` restores it.
  kern_big only: kern_small's DISKOPS.DRV is exactly 7,168.
* **api - HIBER.DRV-only resident `.bss`**: `hb_base`, `hb_unit`, `hb_spt`,
  `hb_heads`, `hb_nx`, `hb_down`, `hb_drvmask`, `hb_susp`, `hb_sp` (~19
  bytes) are named only by HIBER.DRV. Which of them must outlive a
  `mod_drop` - a DOS suspend's two halves are two loads, and `hb_sp`
  crosses a hibernate's own restore - is the question; only those that do
  not could be `.modhb`'s.

## ROWS RUN

`python3 tools/os88test.py soak --marty-jobs 1` over 32 rows, ONCE, after the
last code commit: **29 ok, 1 FAILED, 2 SKIPPED** (643 s).

| rows | what they reach |
|---|---|
| `dispfit`, `dispherc1`, `dispthm`, `dispreboot`, `dispmode`, `dispcheck`, `dualcheck`, `dispsave`, `cpup` | Set Primary and the arrangement through CTRL.DRV's `vid_apply` and `wm_refit` (items 1, 3); the reboot through the blob's `bvs_apply` |
| `dispfsx`, `dispfsxherc`, `dispfsxcga`, `dispmcfs`, `fsxdisp`, `dispstrad`, `dispseam` | fsx brackets on an extended desktop: EXTD.DRV's `ext_apply` (item 1) |
| `dispdrag`, `dragui`, `deskdrag` | `ui_drag`'s release: `EXT_DEAD`, `EXT_PHASE` (item 5) |
| `dockpos`, `dockmodule` | `DOCK_GEOM`'s hops (item 4) |
| `vesa8`, `vesacp` (QEMU) | VESA.DRV's attach through the blob's `bvs_switch` -> `bvs_apply`, and the panel's VESA row |
| `vmmouse` (QEMU) | kern_emu's 14-sector blob booting (item 7) |
| `bootsmoke`, `kzboot`, `blobsum`, `small128`, `stk0water` | the blob copies at boot (items 1-3), kern_small on its floor machine |
| `extdmod` | **FAILED, and fails identically at the base** - DEFECTS. Fixed (the row's fixture), then **ok** |
| `stikmenu` | SKIPPED for want of `mtools`; installed and run: ok, ok (4/4 in `os88bisect classify`), and **once** red in six runs (below) |
| `megarehome` | SKIPPED: needs Bochs, which this container has not got. kern_mega builds (`make mega`) at every batch |

* **Broken on purpose**: CTRL.DRV's `vid_apply` copy made to `ret` at once -
  **`dispfit` went red** (*"the CGA clamped nothing"*: the panel's switch
  published no CGA geometry); restored, green.
* **`stikmenu` failed ONE run of six** (*"STIKMENU.DRV dropped did not happen
  in 120 GUEST seconds"*), the first, run beside `stik` and `stikswap`; then
  ok alone, and `tools/os88bisect.py classify stikmenu -n 4` read HEAD **GOOD
  4/4** and did not build the base. Item 4's change on that path is the same
  two stores `ui_post_cmd` made. Recorded as an intermittent, not classified
  further.
* Every batch: `make -j2` (the fast tier, 61/61), `make -j2 small`; `make
  -j2 emu` and `make -j2 mega` at the batches that could reach them and at
  the tip. `cmp` of kern_small's `kernel.bin` after item 1: identical.

## DEFECTS FOUND

* **`tests/extdmod.py` leg C has failed on every tree since SPEC.md
  120.2.3** (VESA, #295): its fixture wrote a ver-2 'VM' record, kern_big's
  `VID_EXT` build reads only ver 3 (a fourth byte, the packed display's mode
  class), so the record stopped matching, the defaults stood, and the "boot
  with Extend saved" came up Single - which the row reports as a kernel
  defect. Confirmed at the base with `os88bisect sample extdmod --at
  e1e7cebf`: **BAD 2/2, leg C**. FIXED (`0c997045`): the fixture writes ver 3
  when the kernel has `CFG_VE`; leg D (no EXTD.DRV) now exercises the real
  refusal instead of an ignored record. Green here.
* **`tools/os88ladder.py`** names `mod_init_x` as a boot phase; on kern_big
  the phase is now inline in `kmain_o` (as it already was on kern_small), so
  the ladder's "heap" step will show `mem_init_x` alone. An on-demand tool
  (nothing in `make` runs it); not edited.
