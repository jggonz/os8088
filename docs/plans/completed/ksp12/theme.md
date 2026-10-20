# ksp12 agent "theme" - the looks that landed unpassed

Kernel size pass 12. Concepts, all on `main` (so their ABI is FROZEN this
pass): the Windows theme, `WIN95.DRV` and the fourth theme (SPEC.md 76.14,
`c4b43d0` `62b7505`, +232 resident on kern_big); Force Color (SPEC.md 39.28,
`c461c5b`, +29 resident, blob +15); `OSAPI_ICON_DRAW` taking `ICON_SOLID`
records (SPEC.md 25.6.2, `ac7b5b1`, +15). Owned: the resident kernel code and
`WIN95.DRV` code those commits added, and the blob part of Force Color; the
`CTRL.DRV` (`kernel/ctrl.inc`) halves are agent ctrl's. Branch `ksp12-theme`,
cut from `kernel-size-12` at `df8603a1`. Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes, resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`; the module is the UNPACKED image
(`os88drv.image_unwrap`).

## BASE AND TIP

Base is `94616a0a` (`df8603a1` plus the brief's checkdocs fix, no code).

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big resident | 93,574 | 93,491 | **-83** |
| kern_big `.text` / `.bss` / `.cold` | 43,738 / 5,212 / 38,690 | 43,691 / 5,208 / 38,658 | -47 / -4 / -32 |
| kern_big `.boot2` (the blob) | 4,884 | 4,891 | +7 (Force Color's test, moved out of `.cold`) |
| kern_big `KERN_SIZE` | 99,328 | 99,328 | no rung moved (accrued: image 310 -> 259, cold 290 -> 258 of 512) |
| kern_big ROM window (`.cold` of 40,944) | 2,254 spare | 2,286 spare | +32 for the ROM arm's tail (SPEC.md 2.10) |
| kern_emu resident | 93,833 | 93,750 | **-83** |
| kern_emu `.boot2` | 4,984 | 4,991 | +7 |
| kern_small resident | 60,413 | 60,413 | 0 - **byte-identical** (assembled both trees with the same flags and `cmp`'d) |
| `WIN95.DRV` image | 1,565 | 1,382 | **-183** (still a 2 KB claim; the file 1,320 -> 1,288) |
| `.ovl`, every other module and driver | | | 0 |

**Against the concepts' unpassed 276 resident bytes, -83 is 30.1%.** By
concept: the Windows theme 232 -> 163 (-69, 29.7%), Force Color 29 -> 15
(-14, 48%), `ICON_SOLID` 15 -> 15 (0, see REFUSED). kern_small was 0 in all three
and stays so: the theme and Force Color are kern_big's, and `ICON_SOLID`'s
kern_small arm is the refusing one.

## TAKEN

### The doors (SPEC.md 76.14.2) - -15

`thv`'s five far pointers stay; what changed is the DOOR in front of each,
which is now the cheapest shape its callers allow.

* **A door with ONE caller is the far call AT that caller.** wm_hit's title
  row, menu_drop's release and ui_lit_go's held-box rect each had `call door`
  (3) + a four-byte `jmp far [thv + i]` door; each is now `call far [thv +
  i]` (4). The resting bodies end `retf` (wm_hit_ttl_b's three `ret`s;
  menu_drop's "nothing to do" is the existing `cw_kretf`; `wmz_crect_b` was
  far already, so its `.cold` door simply went). -3, -3, -5 (the CRECT site
  was itself a five-byte `call COLD_SEG:` and is now four).
* **The grow box's glyph is a TAIL** (-4): wm_grow_paint `jmp far`s, the
  resting body is the very next instruction (`wm_grow_paint.glyph`, now a
  LOCAL so `.out`/`.box` stay in scope), and both bodies leave through
  wm_grow_paint's own epilogue - no call, no return, and the KERN_BIG and
  KERN_SMALL arms of the routine are ONE body again (the `%else` copy is
  gone from the source).
* The title bar's door keeps its shape: six callers.

Cycles, 8088: a title-row hit and a menu release pay ~20 clocks more (`call
far [m]` + `retf` against `call` + `jmp far [m]` + `ret`), the held-box rect
~7 fewer, a grow-box paint ~43 fewer. None is on a listed hot path.

### `thm_set` and `thm_tab` (SPEC.md 76.12.1, 76.14) - -31

* **Windows on a colour primary IS Color's row** - the two rows were the
  same eight bytes - and its 1bpp row moves to index 3, Windows' own kind,
  which no other kind can reach there (Color is refused on 1bpp). One row a
  kind; `THM_WINMONO` is gone. -8 of table.
* **The refusal is `thm_set` asking itself for Color** (-15 with the row
  logic): a WIN95.DRV that cannot be mounted adopts what thm_set makes of
  Color anyway, so the fallback is `mov al, THM_COLOR / call thm_set / stc`
  where it was a second copy of the adapter test, a second row choice and a
  `pushf`/`popf` around the copy.
* **The future-kind clamp is ON kern_big's ladder** (-2): a kind past
  `THM_NKIND` lands on the 1bpp Color refusal's own `xor ax, ax`.
* **Seven-byte rows** (-5): the pad byte made the index a shift, but it is a
  byte `mul` and the copy a `rep movsb` either way, so it was a byte a row
  and one in the live block (`thm_pad`, never read).
* **No `cld`** (-1): DF = 0 is SPEC.md 1 rule 3, which icon_draw_x's copy and
  api_x already rest on; every caller is the kernel's own.

### `thf_apply` and the module's slot - -8

* **`WN_NENT` 2 -> 1** (.bss -4): hook and unhook are one entry, `wnx_set`,
  and the bit between them is the CARRY thf_apply already has in hand - 0
  from `mod_need`'s success, 1 from the `cmp al, THM_WIN` of every other kind
  (0..2 after the clamp). Module-internal (the kernel and its own module, the
  stamp check guaranteeing both halves are one build): BRIEF 1's
  "module entry tables that only the kernel and its own modules share".
* **`wnx_set` hands the carry BACK** (`pushf`/`popf` where it had `clc`), and
  `mod_gone` - the empty slot - answers 1 too, so thf_apply branches on it
  AFTER the call: one `call far [WNFP]` for both directions and no `jmp`.

### `ui_task` (SPEC.md 76.14.4, 76.14.5) - -5

* **The chrome release's regions in one compare** (-3): `cmp ah, 3 / je
  minimize / jb close`, and 5 (maximize) falls through - there being no
  armed 1 or 4 (`.chrome` arms 2, 3, 5).
* **ui_lit_go takes the lock and clips in place** (-2): `gfx_lock`, the CRECT
  door, `wm_chrome_clip`, `gfx_xor_fill`, `gfx_unlock` - where `ui_lit_xor`
  was a routine of its own behind `ui_lcall`. The clip stays on the Mac path
  too (see REFUSED).

### `gfx_blit1`'s planar tail (SPEC.md 5.4.2.9) - -10, and FASTER

The theme commit's 51-byte fix, on a primitive's row loop, so every change
here is cycle-neutral or better:

* **The planar flag rides in DL** (-5). DL is the complemented-loop flag only
  until the dispatch, and neither emit loop touches it after (DH is the split
  pass's row count). So the dispatch is `cmp byte [gfx_b1inv], 0` before the
  segment loads (whose `mov sreg`s and `cld` write no flag), DL is loaded
  with `[vid_mono]` beside it, and the per-row test is `or dl, dl` where it
  was `cmp byte [bp+6], 0` off the SS frame: -2 bytes and ~16 clocks (+2
  bytes of fetch) a TAIL ROW, twice; the band's setup is 30 clocks against
  31.
* **`xchg [es:di], cl`** for the latch load and the masked write
  (vga_solid_rect's idiom, -3) - one instruction where `mov ah, [es:di]` and
  `mov [es:di], al` were two, ~7 clocks fewer.
* **CL holds the byte across the port write** (0 bytes, ~25 clocks a planar
  tail row fewer): CX is ZERO at the tail in both loops (`rep movsw`, `loop
  .iword`) and reloaded at the next row, so `mov cl, al` replaces `push ax` /
  `pop ax`.
* **`.vmask` sits behind `.row`'s own `jmp short .emitted`** (-2): it cost a
  jump of its own where it stood.

Net per planar tail row ~-48 clocks, per 1bpp tail row ~-16; `thm95`'s
pixel legs (the navy ground beside each band, where 5.4.2.9's defect showed)
and `deskpen`/`disptitle` are green.

### Force Color (SPEC.md 39.28) - -14 resident, +7 blob

* **The box's test is at the call site** (-7 `.cold`, +7 blob): `drv_boot_x`
  is in the boot BLOB now (it was the full overlay when this landed), which
  has 229 bytes left on kern_big and 129 on kern_emu after it, and the blob
  is given back at `mem_unblob`.
* **vgcf_boot banks nothing** (-2): AX is reloaded on the call's next line in
  drv_boot_x.
* **CPE_SC's SC_OP_VGC keeps AL** (-2): modc_e_sc counts AH down and
  cp_vgc_force banks AX, so `mod_drop` needs no second `mov al, MOD_SETS`.
* **One `mov ax`** for AL = MOD_SETS and AH = SC_OP_VGC (-1).
* **No refusal branch** (-2): a core that cannot be read leaves `SETFP` on
  `mod_gone` (`stc / retf`) and its row empty, which `mod_drop` passes over,
  so the call and the drop are both no-ops. vgcf_boot is 14 bytes where it
  was 28.

### WIN95.DRV - -183 of image

* **No landing pads** (-32 + the bodies' exits): each body is entered the way
  its door enters the kernel's - wn_title by thv's jump (home through the
  kernel's `kret_es` ladder, -6: its pushes are put in the ladder's order and
  the fullscreen test moved under them, so there is ONE exit), wn_hit and
  wn_stick by a far call (wn_stick home through `kretfc_di`, -2), wn_grip as
  wm_grow_paint's tail (it banks only SI and ES now, -8).
* **One entry** (`wnx_set`, header -2).
* **The scratch is a module BSS** (-125): `.modwb`, nobits, in the claim's KB
  rounding and asserted so (kernel.asm, `.modhb`'s shape and terms - every
  cell is written before it is read: wn_title's pens and caption, wn_blit's
  band, wn_stick's last pointer at `.stick` before the first `wn_track`).
* **Each button's left lit edge is drawn with its band** (-13): wn_band
  draws the column the band leaves out right after it, so the three `mov ax`
  / `call wn_vl` pairs at the end of wn_buttons go. No pixel is written twice
  either way round, which is what makes the order free.

### Tools

* `tools/stkbalance.py` read `jmp far [m]`'s target as `far`, and did not
  resolve `jmp KERNEL_SEG:label` although its docstring says it walks one.
  Both fixed: the first is a computed jump like `jmp word [m]`; the second
  strips the segment. Re-run over the tree with the HEAD tool against the
  fixed one, the only new finding is wn_grip (a deliberate tail, marked
  `STKBALANCE-OK` with its reason).

## REFUSED

* **Storing `[thm_kind]` before `thf_apply`** so that thf_apply need not
  bank AL (-2). TAKEN, and `thm95`'s leg C went red on it: Windows read as
  the kind while `mod_need` was still reading WIN95.DRV, and the byte has
  always meant what is LIVE. Put back; paid for by vgcf_boot's branch.
* **`ICON_SOLID` (all of it, 0 of 15).** The big arm is 37 bytes against the
  small arm's 22. A byte-loop arm with the solid fill folded in is 44; an
  `xchg ax, cx` header variant is longer; the DS/ES swap pair (8 bytes) is
  the floor for a `rep movsw` copy, every alternative to it (save ES and
  `push cs / pop es`, or `es lodsw` loops) being the same or more; and an
  implied mask cannot ride as a flag without a test in ico_pass, which is
  per row. Its cost is the stage copy's shape, and it stays.
* **Dropping the held box's clip on the Mac path (-12).** ui_lit_xor's
  `wm_chrome_clip` is only NEEDED for Windows (a Mac box is on the front
  window), and the CRECT door could carry it - but a window raised over the
  front window between the press and the release (a package's wake opening
  one) is then inverted, which is the pre-theme behaviour the theme's clip
  incidentally fixed. A behaviour change; the -2 taken keeps the clip.
* **A shared CPU-merge subroutine for both of gfx_blit1's loops** (~-10):
  +43 clocks a tail row on a primitive's row loop.
* **`.vmask` without `push dx`**: DH is the split pass's row count.
* **Deriving the 1bpp Windows row from Bright's at run time**: ~16 bytes of
  code for 8 of table (moot after the row merge left it the only extra row).
* **wm_hit's door as a tail** (-2) where the far call is -3, and menu_drop's
  as a tail (the same resident, a worse module).
* **A near-pointer door table with resident trampolines**: larger.
* **A marker register for the refusal instead of the recursion**: 9 bytes to
  8.
* **The parity flag for region 5** (`jpo` after the `cmp ah, 2`, -3): found
  first, superseded by the three-way compare, which needs no trick.
* **RLE for WIN95.DRV's art**: three bytes a run against two a row, net
  larger on four of the six pictures.
* **vgcf_boot wholly into the blob**: it would need a far door to
  `mod_need`, which does not exist and would be resident.
* **`sc_modcall` for vgcf_boot**: its refusal is a toast, and there is no
  desktop at boot.
* **A 1KB claim for WIN95.DRV**: image + bss would have to be <= 1,024; it is
  1,382 + 125.

## WANTS AN ABI BREAK

* **The `'VC'` key folded into a spare bit** of an existing record (say
  `'FD'`'s read byte, whose 2 is reserved): -1 `.bss` (`CFG_NB`), -5 of
  blob (the key record) on kern_big and kern_emu, ESTIMATED. Breaks
  SYSTEM.CFG: a file `main` wrote carries `'VC'`, which this kernel would
  drop (an unknown key is dropped, SPEC.md 51.5 rule 1), so a user's Force
  Color would be lost once. Callers: driver.inc's key table, CTRL.DRV's
  writer and cp_vid_click (agent ctrl's), vgcf_boot.
* **A shorter module name** (`WIN.DRV` for `WIN95.DRV`): -2 resident
  (`mod_f_win`), ESTIMATED. Not on BRIEF 1's frozen list, but it is a file
  on every shipped system disk, named in the Theme page's toast, SPEC.md
  76.14 and tests/thm95.py; a user-visible name change rather than a size
  pass.

## CROSS-FILE

* **`dock.inc`'s `dkf_apply`, `extmod.inc`'s `exf_sync` and `thf_apply` are
  one shape** - `mod_need` / `call far [FP]` / `mod_drop` - and a mod.inc
  helper taking the module id, the slot and the direction in the carry could
  serve all three: ~8-12 resident bytes, ESTIMATED. Not mine to write
  (dock.inc and extmod.inc are no concept of this pass's).
* **`desksc.inc`'s `modc_e_sc` (CTRL.DRV's settings core, agent ctrl's)**:
  vgcf_boot now relies on CPE_SC's SC_OP_VGC leaving AL as it came (it does:
  the dispatcher counts AH down and cp_vgc_force banks AX). Whoever reshapes
  that dispatcher keeps AL, or restores vgcf_boot's `mov al, MOD_SETS` (+2).
* **`tools/stkbalance.py`**: the two resolution fixes above walk every far
  tail jump to a label from now on - any agent's new one is checked.
* **docs/INDEX.md** is regenerated by `tools/os88index.py` in two commits
  here (the brief, and this note) - every agent's note changes it, so the
  merge wants one regeneration at the end rather than a hand resolution.
* **docs/plans/completed/WINDOWS-THEME-PLAN.md** still describes the landing
  pads and `THM_WINMONO`; it is the design record and was left as it was.
  SPEC.md 76.14, 76.14.1, 76.14.2, 76.14.6, 76.14.11 and 39.28 are updated.

## ROWS RUN

All on MartyPC, `--marty-jobs 1`, with the tree not being edited:

| row | result | what it reaches |
|---|---|---|
| fast tier (every batch, inside `make`) | 43/43 | stkbalance (kernel 0 unbalanced, base and tip), ovlchk, mirror, the doc gates |
| `thm95` | ok (163 s), after one red | all five legs: the buttons pixel by pixel on VGA and Hercules, a title bar under 40 ms, the click-open menu, maximize/restore/minimize, a background X, the clipped light, the Theme page's mount and drop, the refusal with and without a saved Windows. RED once on the `[thm_kind]` store (REFUSED above), green after |
| `vgacolor` | ok (34 s) | Force Color's boot path through vgcf_boot (leg 3) and the panel |
| `dispthm` | ok | the theme on the extended desktop |
| `deskpen`, `disptitle` | ok | gfx_blit1's pen and the title bar on both displays |
| `wmchrome`, `clipgrow` | ok | chrome hit-testing and the grow box under a clip |

**One title bar, Windows on VGA** (thm95 leg A's own trace, median of 8, a
4.77 MHz 8088): base **29.6 ms**, tip **28.9 ms** - gfx_blit1's tail rows
and the doors, cycle-counted rather than predicted.

## EQUIVALENCE

**The pixel diff against the base is the equivalence**, and it is exact.
`build/th/pxdiff.py` (scratch) boots the BASE tree's images and then the
TIP's, each on `os8088_xt_vga` and on `os8088_5150_herc_gla`, each as the
shipped kernel (Bright) and as `THEMEDARK=3` (Windows booted), and saves the
whole rendered frame at eight steps: Disk B: frontmost (grow box / size grip),
Disk A: over it (an unfocused bar), a menu title pressed and held, released
(the Mac closes it, Windows leaves it up), Escape, the front window's close
box held (the lit inversion), slid off still held, released. **All 32 frames
are identical to the pixel**, the menu-bar clock included. The steps are not
null: within a run they differ by 31 to 23,308 pixels (the held light 134 on
the Mac, 214-251 under Windows; the sticky release 35-68, the arrow alone).

What no frame reaches is argued instead: the refusal paths are thm95's legs D
and E; Force Color's unreadable-core path is `mod_gone` and an empty row
(read in `mod.inc`, above); `wnx_set`'s carry hand-back is leg C (hook) and
the Bright pick after it (unhook); kern_small was assembled from both trees
with the same flags and `cmp`'d byte-identical.

## DEFECTS FOUND

* **The brief failed `make`** at `df8603a1`: two bare section-sign 1.2
  references (checkdocs reads a bare section sign as SPEC.md, which has no
  1.2) and a docs/INDEX.md that did not list `ksp12/BRIEF.md`. Fixed in this branch's first commit, which carries
  no code; every other agent's tree has the same failure, so the coordinator
  may prefer one fix on `kernel-size-12`.
* **`tools/stkbalance.py`** did not walk the far tail jumps its own docstring
  says it walks (above). It hid nothing in today's tree.
