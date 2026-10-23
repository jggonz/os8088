# ksp13 agent "theme" - the EGA Color themes, WIN95 following the style, Customize

Kernel size pass 13 (the brief on this branch is still filed as
`docs/plans/completed/ksp14/BRIEF.md`; the coordinator's tree moved it). Concepts:
the EGA Color themes with persistent title stripes (#249, `3987ad05` - ten-byte
theme rows, the inactive pair and the menu XOR mask, `thm_set` far in `.cold`,
`api_thm_set_x`, `thmv_reap_x`), WIN95 following the title style (#261,
`bf1e4a05`, `kernel/winmod.inc`) and the Theme page's Customize (`2bd968a4`,
the `834c0a6b` merge: `thmv_row`, `cp_thm_*`, `cp_thmv_open`, THMPAL.DRV). All
of it is on `main`, so `OSAPI_THM_SET` (its address and its 8-byte length) and
the `'TP'`/`'TH'` SYSTEM.CFG records are FROZEN; the kernel <-> CTRL.DRV and
kernel <-> THMPAL.DRV interfaces are not. Branch `ksp13-theme`, base
`aedadd25` (`4833dfef` plus the brief, no code). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`; module and driver figures are the UNPACKED image.

## BASE AND TIP

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big resident | 92,757 | 92,680 | **-77** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,756 / 5,223 / 38,084 / 5,358 / 336 | 43,718 / 5,223 / 38,045 / 5,358 / 336 | -38 / 0 / -39 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 5,129 / 5,106 / 98,816 | 5,129 / 5,106 / 98,816 | 0 |
| kern_emu resident | 93,016 | 92,939 | **-77** (`.text` 44,015 -> 43,977, `.cold` -39) |
| kern_emu `.boot2` / `KERN_SIZE` | 5,229 / 99,328 | 5,229 / 99,328 | 0 |
| kern_small resident | 60,260 | 60,260 | 0 - `os88rdiff -D KERN_SMALL`: 2,582 routines same, none shifted |
| `CTRL.DRV` image (kern_big) | 12,781 | 12,774 | **-7** (13 KB claim either way, 163 bytes of module bss) |
| `CTRL.DRV` image (kern_small) | 4,383 | 4,383 | 0 |
| `WIN95.DRV` image | 1,443 | 1,430 | **-13** (2 KB claim either way) |
| `THMPAL.DRV` image | 2,496 | 2,496 | 0 |

Against the concepts' ~+210 unpassed resident bytes, -77 is 37%. The concept's
own routines are -80: `.cold` carries +3 that is not mine, `fm_dgdrop`'s
`jne .out` relaxed by NASM on this layout (CROSS-FILE 2). kern_small has no
theme (`OS88_THEME` is kern_big's), and none of what this pass touched is
assembled there.

## TAKEN

### `thm_set` asks the adapter once (SPEC.md 76.14.14.1) - `.cold` 163 -> 129

* **BH holds the adapter's depth** (`sbb bh, bh` after the one `dec`/`cmp`:
  FFh on a 1bpp primary, 0 on colour). The routine asked it three times - for
  the kind, again for the refusal's fallback and again for the row - and every
  later answer was the first one.
* **BL is the table row from the same ladder that settles the kind**, so
  nothing after `thf_apply` re-derives it: on colour Color and Windows both
  wear row 2, on 1bpp Windows keeps row 3 (`THM_WINMONO` = `THM_WIN`, asserted)
  and Color falls to Bright. The refusal is `mov al, bh / not al / and al,
  THM_COLOR / mov bl, al`. The base computed BL before `thf_apply` and threw it
  away after.
* **`kentc_bp` / `kretfc_es`** in place of six pushes and the seven-instruction
  inline far exit, and `xchg ax, bx` for the row and the kind together.
* The kern_small arms inside the routine were never assembled anywhere
  (`OS88_THEME` implies `KERN_BIG`) and are deleted from the source.

### `OSAPI_THM_SET` jumps to its cold body (SPEC.md 20.3, 76.14.14.1) - `.text` -6, `.cold` +1

The cell's ADDRESS and LENGTH are ABI and both stay; its body is not. 0x046D
is a new 8-byte shape, `OSAPI_FJCELL`: `push ds / push cs / pop ds / jmp
COLD_SEG:api_thm_set_x`, and the body ends `pop ds / retf`. The 6-byte
`api_thm_set` thunk the SLOT near-called is gone, and the cell is one call and
one return faster. `OSAPI_IENDJ` is `OSAPI_IEND`'s two length assertions
without the `retf`, because kern_small's arm of the same cell is still seven
NOPs and a `retf`. `api_thm_set_x` no longer banks AX, which nothing in it
writes (-2). `tests/unit/t_api_abi.py` decodes the shape as `FJSLOT` and
requires its segment word to be `COLD_SEG`; the jump is written inside the
macro so `os88ovlchk`'s far-tail rule, which judges the enclosing label, does
not take the table for a near body (an FCELL's `call COLD_SEG:%1` is exempt
the same way).

### No shims back into `.text` - `.text` -8, `.cold` +4

`cw_dock_force` and `cw_wm_su_drop_all` (4 bytes each, one caller each) are
gone: `thm_set` (BP banked) and `api_thm_set_x` (BP pushed) reach `dock_force`
and `wm_su_drop_all` through `ct_cw_mem_disp`, `cw_mem_disp`'s `call bp /
retf`.

### `thmv_reap_x` (SPEC.md 76.14.14) - `.cold` 28 -> 18

No "is it loaded?" test: `drv_call` refuses a row whose `DRVR_SEG` is 0 (CF = 1,
AX = 0, SPEC.md 51.2) before it reads anything else of it, and `drv_release`
returns at once for one, so the never-loaded close does nothing either way.
`drv_call_ent` reads BP off the row. `cp_thmv_open`'s loaded-then-refused path
far-calls it instead of carrying its own copy of the two calls (CTRL.DRV -10).

### THMPAL.DRV's file name moves into CTRL.DRV - `.text` -11

`drv_load_at` reads a row's `DRVR_FILE` through DS, so the name has to be in the
kernel segment - but only while it loads, and `cp_thmv_open` is the one code
that loads `thmv_row`. It stages `cp_thmv_fname` into `cp_sbuf` and stores the
pointer before each load (CTRL.DRV +17). Nothing else reads the row's FILE or
TITLE: it is past `DRV_MAX`, so no walk meets it, and `thmv_row` ships `dw 0,
0` there. `drv_mounted` and `drv_find` stage nothing into `cp_sbuf` before the
name is read (read in `kernel/drvvol.inc`).

### The title pens (SPEC.md 76.12) - `.text` -14, and fewer clocks on every arm

`wm_tpen_bg` / `wm_tpen_ink` move from `kernel/wm.inc` (kern_small keeps its
two `equ` aliases there) to beside `thm_pen` in `kernel/vga12.inc`, where the
focus test reaches the ACTIVE door (`thm_tbg` / `thm_tink`) with a short `je`
and the inactive pen is written inline. `thm_ibg` and `thm_iink`, the two
inactive doors, had no other caller and are gone. 8088 clocks to the shared
`thm_pen` tail (EU timings, fetch aside):

| | base | tip |
|---|---:|---:|
| ground, front window | 66 | 63 |
| ground, other window | 78 | 51 |
| ink, front window | 66 | 63 |
| ink, other window | 78 | 36 |

### One byte of padding, for parity - `.text` +1

The pass's `.text` total came out odd. A never-executed `db 0x90` after
`thm_desk` (where `api_thm_set` and `thmv_fname` used to be) puts every `.text`
routine after it at an even shift: `os88rdiff kernel aedadd25 . --hot-file`
over `gfx_blit4`, `gfx_blit1`/`_x`, `font_run`, `font_char`, `mou_isr`,
`mou_p2_isr`, `kbm_isr`, `sch_isr`, `sch_switch`, `dsk_xfer`,
`dsk_next_clus_x`, `dsk_runcap`, `ui_task`, `gfx_fill`/`_raw`,
`vga_solid_rect`, `gfx_xor_fill_raw`, `vga_xor_fill_vram`, `cur_move`,
`api_x`/`rs`/`rsc`, `api_far`, `sw_fill`, `sw_blit_row`, `sw_col`, `gfx_hline`
reads **all 28 identical, at an even shift**, on kern_big and kern_emu, and
every `.text` run is even. The one odd run left is `.cold`'s `fm_paste_res ..
cold_kretf`, from the `fm_dgdrop` relaxation, with nothing hot in it.

### WIN95.DRV - image -13

`wn_buttons` picks both of its pens on ONE test of `[thm_cstrp]` bit 1: AX =
the title pair and DL = its ink for the new style, the chrome face over black
and white for Classic, then one store each. It tested the bit twice and wrote
each pen in two places. DX is in the routine's documented clobber list.

### CTRL.DRV - image -7 net

The Theme page's "WIN95.DRV could not be read" toast is `sc_m_say` again
(-10: `#249` had inlined the ES = CS staging around `cw_toast_show`), and
`cp_thmv_open` banks through `kentm_bp` / `kretm_bp` (-8); with the refusal
path's -10, against the staged name's +17.

### Tools

* `tools/os88rdiff.py` could not assemble a COMMIT's kernel at all: it exported
  `kernel/`, `boot/` and `apps/` and `kernel/vga12.inc` includes
  `drivers/thmpal/thmrows.inc`. It exports `drivers/` for a kernel side now.
* `tests/unit/t_api_abi.py` decodes `OSAPI_FJCELL` (above).

## REFUSED

* **The XOR head storing the mask on both arms** (-1: `cmp` / `mov [vga_xorm],
  al` / `pop ax` / `jne sw_xor_fill`). Built, measured, reverted: the VGA arm
  is the same four instructions, but `jne sw_xor_fill` relaxes, so the 1bpp
  arm is 46 clocks where it was 43, on a near-hot path. Sending both 1bpp tests
  to one shared `jmp sw_xor_fill` (-6) is +12 clocks a 1bpp XOR fill. Storing
  the mask BEFORE `cur_unlazy` (-3) is refused because the arrow's restore may
  itself fill. `vga_xor_fill_vram` is byte-identical to the base.
* **A rare cell (`OSAPI_RCSLOT`) plus two pad bytes at 0x046D** (-6 against
  the far-jump cell's -5): the table walk decodes a cell by its shape, a
  padded rare cell is two shapes, and `api_rsc` + `api_far` is slower.
* **Merging `api_thm_set_x` into `thm_set`** behind an entry flag (-3, keeping
  the `.text` thunk): smaller than nothing but larger than the far-jump cell,
  and the two do not combine - the merged body would need a `pop ds` under the
  far ladder's pops.
* **The refusal re-entering the adapter ladder** with a flag in DL (-2): it
  calls `thf_apply` a second time (a no-op `mod_drop`, read in `mod.inc`), so
  the equivalence would be an argument rather than a trace.
* **`thmv_reap_x` as a CTRL.DRV entry**: the resident thunk and its 4-byte
  slot are no smaller than the 18-byte body.
* **The reap inside `cpf_cp_flush_close`**: `hbm_down` calls that for a live
  resume and a DOS handoff, so the Customize window would close across one.
* **`thmv_row` in `.bss`, or shorter**: `drv_call`'s one range test needs a
  whole `DRVR_` row right after `drv_tab`, in `.text`, with `DRVR_DISP` and
  `DRVR_CLASS` set.
* **Deriving the inactive pair and the XOR mask** (thm_tab's three newest
  columns are Bright/Dark/1bpp-Windows' chrome pair and 0Fh): 9 table bytes
  against at least 11 of code and the Color row's exception.
* **Color's stripes through the table** (row byte 6 = `[thm_cstrp]` bit 0):
  Windows wears the same row on a colour adapter and must stay solid.
* **`vga_sr_xor` taking the mask through AL**: the same size.
* Pass 12's REFUSED list: nothing re-proposed.

## WANTS THE OWNER

* **`[menu_xorm]`, the menu's private copy of the XOR mask** - -7 kern_big
  (`menu.inc`'s two-instruction snapshot, 6 bytes, and its `.bss` byte), with
  `vga_xor_fill_vram` reading `[thm_xorm]` itself. The snapshot exists so a
  theme change while a menu is down cannot make the highlight's OFF XOR
  disagree with its ON. `menu_drop`'s poll loop runs on the UI task and
  `OSAPI_THM_SET` is UI-task-only (SPEC.md 76.14.14), so only a package worker
  breaking that contract could reach it - and the release restores the menu
  rect from its save-under anyway, so a mismatch would be visible only while
  the menu stays down. Behaviour differs only for a contract-violating caller.
* **kern_small carrying `'TP'`** - -7 kern_small `.bss` (`CFG_THMP` ..
  `CFG_THMC`) and a key-table row in the boot blob and CTRL.DRV's core. That
  kernel has no Color and never reads it; it carries the record so a preset
  survives booting a small kernel and back (SPEC.md 76.13, 76.14.14). Dropping
  it loses the user's preset at the next small-kernel save.

## WANTS AN ABI BREAK

None. The cell's shape changed inside its frozen address and length.

## CROSS-FILE

1. **`kernel/driver.inc` `drv_cp_closed_x` (wkcore)**: append the reap at its
   end, before `jmp kretfc_bp` - every register is already banked there -
   `mov bx, thmv_row / mov al, THMV_CLOSE / call drv_call_ent / call
   drv_release` under `%ifdef KERN_BIG`, then delete `thmv_reap_x` and
   `instance.inc`'s `call COLD_SEG:thmv_reap_x`, and give `cp_thmv_open`'s
   refusal its own two far calls back. ESTIMATED **-12 resident** (+10
   CTRL.DRV). The order is today's: the drivers are told first, then the
   Customize window goes, then the settings write.
2. **`kernel/files.inc` `fm_dgdrop` (wkfs / misc)**: its `jne .out` is 126 bytes
   from `.out`, and on this branch's layout NASM grew it to 5 bytes and never
   shrank it back (the code between is identical, so a multi-pass convergence
   artefact). `jne short .out` pins it: **-3 `.cold`** here, and it is the cause
   of the one odd `.cold` run. Worth a listing scan for others like it after
   the merge.
3. **`kernel/kernel.asm`'s `ct_cw_mem_disp` site-count comment** ("8 / 6 sites")
   is two callers short now (`thm_set`, `api_thm_set_x`). Comment only.
4. **docs/INDEX.md** is regenerated by `tools/os88index.py` in two commits here
   (the base's stale index, which failed `make` at `aedadd25`, and this note);
   the merge wants one regeneration.

## ROWS RUN

MartyPC on the GLaBIOS twins (nothing needed the IBM ROM, nothing skipped),
`--marty-jobs 1`, the tree not being edited. The fast tier ran inside every
`make`: 47/47 each batch.

| row | at | result |
|---|---|---|
| `thm95` | `dcba9c1e` | ok (271 s): Windows' buttons pixel by pixel on VGA and Hercules, the refusal paths, the Theme page's mount and drop |
| `colorpresets` (QEMU) | `dcba9c1e` | ok (241 s): every preset, both title pairs, the menu XOR on and off, Customize open/Cancel/Apply, the reap on panel close, the private ABI refusal, SYSTEM.CFG across a reboot |
| `disptitle`, `dispthm` | `dcba9c1e` and the tip | ok, ok |
| `wmchrome`, `deskpen`, `clipgrow`, `xorrect` | `dcba9c1e` | ok each |

The tip's last code commit (`66992583`) only moved the pens and put the XOR
head back to the base's bytes, so `disptitle` and `dispthm` were run again
there and nothing else.

## EQUIVALENCE

A unicorn harness (scratch) loads each kernel's `.text` and `.cold` at their
segments from the base's and the tip's own assembly and runs the real code,
stubbing only what it calls out to:

* **`thm_set`**: 2,160 cases - AL in 0..5, 7Fh, FFh; `[thm_cstrp]` in nine
  values; `[vid_kind]` 0..4; WIN95.DRV loading or not; the prior kind 0, 2 or
  3. Every register, DS/ES/SP, CF, `[thm_kind]`, `[thm_cstrp]`, the ten-byte
  live block, `[menu_bdirty]`, the sequence of `thf_apply` calls with their AL
  and DS, and the `dock_force` call: **identical**. A negative control (the
  load failure flipped on one side) differs.
* **Through the cell at 0x046D**: the same 2,160 with a caller DS that is not
  the kernel's, plus `[cp_dirty]`, `[ui_post]` and the `wm_su_drop_all` call:
  identical, DS handed back.
* **`thmv_reap_x`**, image loaded and not: the far calls (AL, BP, segment, BX =
  the row), `drv_release`'s row and every register: identical.
* **`wm_tpen_bg` / `wm_tpen_ink`**, front window and not: every register,
  EFLAGS and `[gfx_color]`: identical.

`wn_buttons` and the CTRL.DRV changes are argued by reading (each is the same
stores from the same sources) and covered by `thm95` and `colorpresets`.

## DEFECTS FOUND

* **The base did not build**: docs/INDEX.md was stale against the brief
  (`os88index` failed `make`). Regenerated in this branch's first commit.
* **`tools/os88rdiff.py` could not assemble a commit's kernel** (TAKEN, Tools).
* **No row sees a window's INACTIVE caption.** Broken on purpose -
  `wm_tpen_ink`'s inactive arm reading the inactive GROUND - `disptitle`,
  `dispthm` and `colorpresets` all stayed green, while a CGA capture of two
  Disk windows shows the back window's "Disk" gone (89 pixels differ from the
  fixed build). `colorpresets` checks the inactive pair as bytes, not as
  pixels. Not fixed here - writing a row is not this pass's - but the
  kernel's half of SPEC.md 76.12's "focus readable across a screen" has no
  pixel gate on any adapter.
