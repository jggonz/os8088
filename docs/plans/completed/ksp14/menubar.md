# ksp14 agent "menubar" - the menu bar's speaker and volume, the file-progress rework, toast placement

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`). Concept: the menu
bar's speaker and the volume popup's kernel half (#296 `cd38ec7d`, #300
`35cac28e`), the file-progress widget's rework and toast placement (`553f2a21`'s
re-applied rework: *speaker right of the clock, a toast that misses the gap
covers it, fpg_hide*) - `menu.inc`'s composition (`menu_bt_go`, `menu_sound`,
`menu_ckend`, `menu_bar_clock`), `ui.inc`'s `ui_volume_service` and `ui_task`'s
clock cell, `fprog.inc` (`fpg_hide`/`fpg_show`/`fpg_bare`/`fpg_gbare`/`fpg_vis`
and the changed `fpg_*`), `toast.inc` (`toast_up`, `toast_place`). Branch
`ksp14-menubar`, base `4be03502` (`26a63034` plus the brief, no code). Figures
are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident = `.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf`. No module or driver image changed
(SOUND.DRV's source changed by one comment: the image is byte-identical).

## BASE AND TIP

| | base `4be03502` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 93,342 | **93,135** | **-207** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 44,005 / 5,375 / 38,140 / 5,486 / 336 | 43,785 / 5,388 / 38,140 / 5,486 / 336 | -220 / +13 / 0 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 5,926 / 5,118 / 99,328 | 5,926 / 5,118 / 99,328 | 0 |
| **kern_small resident** | 59,207 | **59,050** | **-157** |
| kern_small `.text` / `.bss` | 31,269 / 3,016 | 31,137 / 2,991 | -132 / -25 |
| kern_small `.boot2` / `.ovl` / `KERN_SIZE` | 4,381 / 1,486 / 61,440 | 4,381 / 1,486 / 61,440 | 0 |
| **kern_emu resident** | 93,611 | **93,406** | **-205** (`.text` 44,274 -> 44,056, `.bss` 5,375 -> 5,388) |
| kern_emu `.boot2` / `KERN_SIZE` | 6,026 / 99,328 | 6,026 / 99,328 | 0 |
| **kern_mega resident** | 103,165 | **102,952** | **-213** (`.text` 47,413 -> 47,225, `.bss` 5,411 -> 5,386; `.mega` 14,698 unchanged) |

The kern_big and kern_emu figures include a DEFECT FIX that costs +38 of
`.bss` and gives back 24 of `.text` (`MENU_BCMAX` for a 1024-wide VESA bar,
below); without it they would be -221 and -219, but without it the base's
overrun corrupts the About cell once `menu_tbuf` is shared, so the two go
together. kern_small and kern_mega have no `VID_EXT` and are untouched by it.

Against the concept's ~+430 net kern_big, -221 is 51% before the defect fix's
+14. Everything is `.text` and `.bss`; `.cold`, `.lowbss`, `.boot2`, `.ovl` and every module are
untouched, so no rung moved on any kernel (KERN_SIZE is the same step on all
four - the bytes are banked below the next one, which is the point).

## TAKEN

Per-routine, `tools/os88rdiff.py kernel 4be03502 HEAD` (`-D KERN_SMALL` for
small); every item below is in the concept's own routines unless marked
*(outside the concept)*. Bytes are kern_big / kern_small.

### The composition (`menu.inc`) - one choice in CF, one place that ends in the speaker

* **`menu_furniture` written in `menu_draw_bar`**, its one caller, where
  `kent_di` has already banked everything it pushed: -12 / -12. And both of the
  bar's flags are taken and cleared with one `shr byte [m], 1` (`[menu_bovr]`
  and `[menu_bdirty]` are 0 or 1 at every writer - all eight read), CF being
  the bit: `[menu_bovr]`'s is -6 / -6, `[menu_bdirty]`'s is in the next item.
* **The composition's four doors carry the choice in CF** and every one but
  `menu_sound`'s own ends in `menu_sound` *once*: `menu_bar_clock` (CF = 0, the
  UI task's tick), `menu_bt_ent` (CF as given - `menu_draw_bar`'s `shr`),
  `menu_bar_text` (CF = 1, `menu_sound`'s re-composition, which must not come
  back round) and `menu_bt_raw`. `menu_draw_clock` and both `call menu_sound`s
  went; `menu_draw_bar`'s text pass is `shr` + one `call`: -20 / -22. Then
  `menu_sound` is written straight after `menu_bt_ent`'s call, so the jump into
  it goes: -3 / -. `ui_task`'s clock step calls `menu_bar_clock`, the same
  three bytes it was.
* **`menu_x2cell`**: `sub ax, MENU_TXT_X0 / mov cl, 3 / shr ax, cl` was written
  in `menu_bpad`, `menu_inval`, `menu_ckend` and the composition's prologue.
  `menu_ckend` falls into it; the clamp (an x left of the segment is cell 0) is
  `menu_bpad`'s and `menu_inval`'s, and the other two never pass such an x
  (`[vid_clk_hx]` >= 434, the margin >= 600): -18 / -18.
* **`menu_bemit` takes "inverted" in CF** and banks it across `font_run` with
  `pushf`/`popf` (the entry's IF and DF are what come back, which this path
  never changes); `[menu_binv]`, its store and two of its three tests went, the
  gap fill's test merged into the restore's: -17 / -17. The caller makes CF with
  `add al, al` (AL is 0 or 80h) and `thm_rpair`/`thm_pair` are a `mov` and an
  `xchg`, which write no flag. *(`menu_binv` is SPEC.md 76.3's, outside the
  concept but inside `menu_bemit`, which the concept changed.)* And the run's x
  is `lea ax, [bx + MENU_TXT_X0/8]` shifted once (`%if` holds the segment to a
  cell), the run's SI one `lea`: -4 / -4.
* **The prologue asks `FPG_SHOWN` first**: `fpg_vis` only runs when the widget
  has pixels to reserve, and `mov dx, FPG_CELLS` sits between `fpg_vis` and its
  `jc`, so the `and dx, FPG_CELLS` went: -4 / -4. Faster on the common path
  (widget not shown: one test where it was a call).
* **Smaller, all in the composition**: `[menu_bend]`'s min built in BX with AX
  left as the pad bound (-2); the clock step's `xchg ax, dx / mov ax, dx`
  round `clk_fmt`, which banks AX (-3); the toast strip's gap cell enters the
  loop past its `lodsb` (-4); `[menu_bfirst]`'s seed `or word [m], -1` (-1); the
  emit's none-test `inc / jz / dec` (-1); `menu_bent` reads the title last,
  into SI, so its AX bank goes (-2, *outside the concept*); the bare label's
  `push es`/`pop es` - the entry's covers it, and what ES held there before was
  the caller's, never ours (-2). -15 / -15.
* **`menu_inval`'s walk** *(outside the concept, beside `menu_x2cell`)*: the
  first-cell bound `cmp ax, MENU_BCMAX` is the clamped last cell's compare
  (`ax >= BCMAX > dx`), and the forget loop indexes `menu_bcell` by BX:
  -10 / -10.
* **A dropped cell advances DI** (SPEC.md 59.7.2, new): `menu_bput` dropped a
  cell at or past `[menu_blim]` without advancing, and SPEC.md 59.7 and 59.7.1
  are two hangs that fact caused and two guards against it in `menu_bpadc`.
  With the drop advancing, `menu_bpadc` ends at its own target whatever the
  bound does, so the clamp and its per-cell re-read went: -7 / -7. Every other
  caller's use of DI was read: `menu_bstr` stops at the bound by its own
  compare, the menus' end is min(DI+1, `[menu_bn]`) and every cell below the
  bound is composed before the first drop, and the clock, the strip and the
  blanking reset or count their cells. `menu_bcell`, and so every pixel, is
  what it was.
* **`menu_tbuf` is `clk_str`** (`.bss` -25 / -25, *outside the concept*:
  SPEC.md 12.2's truncation buffer, which `553f2a21`'s rework had left with a
  25-byte buffer of its own). `clk_str` is written by `clk_fmt` alone and read
  straight back by the composition, under the gfx lock on the UI task;
  `menu_tbuf` is written by `menu_trunc` and read by the `font_run_x` three
  instructions on in `menu_drop`'s item loop, which holds the same lock on the
  same task - no composition can run between. `CLK_STR_LEN` names the 25 that
  both `%if`s (the boot probe's and this) check.

### The speaker (`menu_sound`) - 161 -> 131 on kern_big

`kent_di` + BP + ES is `kret_es`'s run exactly (-9); the capability as `mov dx,
imm / and dx, [m]` (-1); the speaker's erase is `fpg_ring` (below, -16 here);
`[menu_mixcap]`'s high byte tested (0, 0200h or 0FFFFh: -1); the icon verb's
`push ds / pop es` (the verb takes no buffer and `mix_icon` sets its own ES:
-2); `[toast_scov]` compared with DL, which is 0 there (`snd.inc`'s `%if` holds
`SND_CAP_MIXER`'s low byte to it: -1). `fpg_ring` erases rows 0..MBAR_H-2 where
the picture is 2..17: the four more rows are the bar's own ground under no other
painter at those columns (the widget ends at `[vid_clk_hx]` - 1, the strip is
text-band only), written with the colour they hold. `menu_ckend`'s capability
test is a byte (-1).

### The progress widget (`fprog.inc`)

* **`fpg_ring`**: `fpg_hide`'s two fills above and below the text band, shared
  with `menu_sound` (kern_big only; kern_small keeps `fpg_hide` as it was).
* **`fpg_hide`'s VESA wide-bar band is gone** (-24, kern_big): it whitened the
  text band over the widget's part past `MENU_BCMAX` cells, which with
  `MENU_BCMAX` sized for 1024 (DEFECTS FOUND) is no part - the composition's
  own run erases it, as on every other bar. `fpg_hide` 57 -> 14 with
  `fpg_ring` (18) beside it.
* **`fpg_show` answers in ZF** (`FPG_SHOWN` tested at its exit; `fpg_bare`'s
  `pop`/`ret` write no flag), so `fpg_busy` and `fpg_gbare` drop their
  `test`: -5 / -5. Cycle-neutral: the shown path returns on the `jnz` that
  already decided it.
* `fpg_gbare`'s tail `jnz fpg_bare` is back in short reach (-3, NASM).

### Toasts (`toast.inc`)

* **`toast_place`**: `clk_fmt` and `str_len` both keep AX, so asking the clock
  before the strip's width needs no banks: -4 / -4.
* **`toast_up`**: `test byte [toast_on], 1 / jz` (CF = 0) for `cmp / cmc / jnc`:
  -1 / -1.
* **`toast_now` through `fpg_bare`** *(outside the concept)*: the bank, clear and
  restore of `[wm_clip_n]` round `toast_arm` + `menu_draw_bar` is exactly
  `fpg_bare`'s, with a two-instruction painter `toast_ad`; `fpg_bare` spends
  BX, which is banked: -6 / -6.

### The volume bridge and the clock cell (`ui.inc`, kern_big only)

* **`ui_volume_service`** 107 -> 82: it banked ES, BX, SI, DI and BP for a
  caller (`volume_popup`, `drivers/sound/volume.inc`) that holds nothing in any
  of them across a bridge call - read call by call, and `mixui_draw` sets its
  own ES - while every routine it calls preserves them (bar verb 3's DI). The
  contract is now written at both ends: DS preserved, AL/CX/DX verb 2's (-10).
  The `times 2 db 0` PARITY pad was even and so moved no parity (-2). Verb 2's
  three reads under `cli` are `osapi_mouse.live` - `OSAPI_MOUSE`'s own tail,
  the same three loads - inside the same `pushf/cli/popf` (-8). One compare
  splits the four verbs, and the show and the exit are shared (-5).
* **The clock cell's press is out of `ui_task`** (`ui_clkcell`, called; kern_big
  only, kern_small's arm is unchanged): its 43 bytes inside `ui_task` had put
  `fdlg_grab_x`'s `jc .yj1` out of short reach, a five-byte inverted pair. And
  `[menu_mixcap]`'s high byte (-1). -2 net.

### Parity (t_hotpar)

Every hot routine is at its measured parity on both kernels at the tip, with
no pad added: kern_small's `fprog.inc` PARITY pad (`db 0` after `fpg_finish`)
is DELETED (-1), its -103 at that point having been odd the other way, and two
of the items above were taken in the order that kept kern_big even. `ui_task`'s
own bytes changed (the clock cell left it): the cut is 46 bytes, even, so the
idle pass after it keeps its internal parity; the 3 the `jc` lost are before
`.clock_cell`, in the press path. NOT on the hot list and at an ODD shift
against the base on kern_big: the run `fpg_arm .. toast_now` (-181), which
holds `fpg_step` - dsk_xfer's per-sector caller, a compare and a return when
the widget is unarmed - and `dsk_img16`, whose jump relaxed for a byte there
(pass 13 saw the same). A pad byte in front of `fpg_arm` would even it for +1
if the coordinator wants `fpg_step` pinned; `t_hotpar` does not ask.

### Taken after the merge, at the owner's word: the speaker's hit test is SOUND.DRV's

WANTS THE OWNER item 1, approved: *"the window between SOUND.DRV unloading and
the bar recomposing is so short no human could do it, so we can take the -10
resident from that. It's not that bad even if it happens (the clock is going
to be in that spot as soon as the draw finishes anyway)."* Item 2 was NOT
approved and is not taken. Measured on `kernel-size-14` after the vesa and
serial merges (`ea108fa8`, fast-forwarded to):

* `ui_clkcell` (44 -> 35) hands SNDV_MIXPOP the press (CX) AND the speaker's
  x (DX, `[menu_mixx]`), and opens the Date/Time page when the driver answers
  CF = 1 - which `drv_pkg_call_x` also answers with no SOUND.DRV published:
  that is the owner's accepted edge. The speaker's x goes with every press
  rather than being the driver's own `[volume_iconx]` because a SOUND.DRV
  unticked and re-ticked inside a minute is mounted before any composition
  runs `menu_sound` again, and a fresh image's `[volume_iconx]` is 0 - the
  driver would answer "not mine" to its own speaker until the bar next moved.
  That costs 4 of the estimated 13.
* `volume_popup` does the hit test first: `CX - DX < 24`, else CF = 1 and a
  `ret` before anything is pushed or claimed (+13). SPEC.md 34.15's verb
  table, `sndpkg.inc` and `volume.inc` say so.

| | before | after | delta |
|---|---:|---:|---:|
| kern_big `.text` / resident | 43,507 / 92,602 | 43,499 / 92,594 | **-8** |
| kern_emu `.text` | 43,778 | 43,768 | **-10** (`ui_clkcell` -9, `dsk_img16`'s jump relaxes a byte) |
| kern_mega `.text` | 47,191 | 47,181 | **-10** (the same two) |
| kern_small | - | - | 0 (`OS88_SNDCARD` is kern_big's; byte-identical) |
| `SOUND.DRV` image (header +8) | 11,134 | 11,147 | +13 |
| `SOUND.DRV` stored stream (`os88drv.image_unwrap`, trailing bss cut) | 10,254 | 10,251 | -3 |
| `SOUND.DRV` claim | 11 KB | 11 KB | none (117 bytes left in it) |

Rows at the tip: `mixercga`, `mixerherc`, `mixervga` (MartyPC), `mixersb16`,
`mixervesa` (QEMU) - every one presses the speaker and drives the popup - and
`sndmove`, `sndplay` for SOUND.DRV itself: 7/7 ok. No row presses the clock
field BESIDE a showing speaker, so that arm was checked by hand on MartyPC
(`os8088_5150_sb_gla`, `mixer360.img`): a press on the clock's text (x 436)
and one in the margin right of the speaker (x 634) each opened the Control
Panel on Date/Time (`[cp_sel]` = 1), and a press on the speaker opened no
window. `make`, `make small`, `make emu`, `make mega`, the fast tier and
`hotpar` on both kernels: green.

## REFUSED

* **`fpg_busy` through `fpg_gbare`** with the no-scale/no-hatch tests moved
  into a painter in front of `fpg_hatch` (-12 both kernels): +~100 clocks per
  transfer while the widget is armed - one more `fpg_bare` on a path dsk_xfer
  takes once per `int 13h` *and once per cache hit*, where a 1-sector hit is
  ~7,600 clocks. In WANTS THE OWNER.
* **The bridge as an OSAPI path**: it needs `fpg_finish`, the busy shape,
  `cur_unlazy`, `cursor_hide/show` and `evq_drain(ui_ev)`; no cell publishes
  them, and a cell is 6 bytes where the bridge's whole dispatch is 10.
* **The bridge's far pointer in ES:DI instead of `ui_volume_bridge`'s 4-byte
  table** (-4 kernel, -3 SOUND.DRV): the driver would build the far pointer with
  `KERNEL_SEG` as the segment, which is right on every kernel - but the verb is
  on `main` (#296/#300) and an older SOUND.DRV beside a newer kernel would
  far-call through whatever DI points at. That is a DRV_VER bump for 4 bytes.
* **A shared "drop the busy shape" helper** for `ui_volume_service`,
  `stm_thaw` and `gfx_unlock`: `gfx_unlock`'s copy is inline on every unlock
  and a call there is cycles on every drawing burst; `stm_thaw`'s is in a
  module (SMK). Two copies would not pay for the helper.
* **`KES` out of `ui_volume_service`** (-2): `evq_pop` sets its own ES and the
  cursor routines bank theirs, so it is probably dead - but not every callee
  under `fpg_finish` was read end to end, and a wrong ES there is a silent
  write. Left for whoever reads them.
* **`[menu_mixcap]` as a byte** (-1 of `.text` data): `tests/mixer.py` and
  `tests/megacdrom.py` read it as a word against `SND_CAP_MIXER`.

## WANTS THE OWNER

1. **TAKEN at the owner's word after the merge (section above).** **The speaker's hit test into SOUND.DRV** - about -10 resident kernel bytes
   (`ui_clkcell`'s `mov ax, cx / sub ax, [menu_mixx] / cmp ax, 24 / jae / mov
   cx, [menu_mixx]` become a `jc .date` after the driver call), ~+12 in
   `SOUND.DRV`, which already holds the icon's x (`[volume_iconx]`). The
   behaviour that changes: a press on the speaker's place in the window between
   SOUND.DRV going away and the next composition opens the Date/Time page where
   it does nothing today. ESTIMATED.
2. **`fpg_busy` through `fpg_gbare`** (REFUSED above): -12 on both kernels for
   ~100 clocks a transfer while a file operation has the widget armed. A cycles
   trade on the path every `int 13h` and every cache hit takes once.

## WANTS AN ABI BREAK

None. Nothing frozen moved: no cell, no header, no SYSTEM.CFG key; the
kernel <-> SOUND.DRV bridge (SNDV_MIXPOP) keeps its verbs and its far pointer,
and only the registers the bridge preserves narrowed - to the one its one
caller relies on (DS), written down in `ui.inc` and `volume.inc`.

## CROSS-FILE

* **`osapi_mouse.live` (`mouse.inc`) has a second caller** -
  `ui_volume_service`'s verb 2, inside its own `pushf/cli/popf`. Whoever edits
  `osapi_mouse` (the serial agent's mouse hunks) must keep `.live` the three
  loads and a `ret`.
* `clk_str` is now `menu_tbuf` too: `tests/mixer.py` reads it as the clock's
  last line (as it was), which stays true unless a menu item longer than 24
  glyphs was dropped since the last composition - no row does that.
* **A row for the strip's glyphs and the trough's pixels** (the two breaks
  above that nothing saw): `tests/toastbar.py` could compare the strip's cells
  against `font_run` of the message, and `tests/fpgnotch.py` the trough's lit
  width against `[fpg_px]`.
* `kernel/viddet.inc`'s comment at `vid_apply` still says `MENU_BCMAX = 62`
  (it has been 85, and is 123 on kern_big now) - a comment, the vesa agent's
  file this pass.

## ROWS RUN

One pass of the concept's rows at `a6666f56` (`os88test.py soak -k ...
--marty-jobs 1`), the two VESA rows again after the fix at `f3be393d`, and a
confidence pass of five bar rows and the kern_small rows at the tip:

| row | where | result |
|---|---|---|
| toastbar, toastfpg, fpgnotch, fpgcold, curdisk | MartyPC | ok (toastbar, toastfpg, curdisk again at the tip: ok) |
| mixercga, mixerherc, mixervga | MartyPC | ok (mixercga again at the tip: ok) |
| mixersb16 | QEMU | ok |
| **mixervesa** | QEMU | **FAIL at `a6666f56`** - the About cell's name read `  10:54`: the base's overrun (DEFECTS FOUND) landing on `menu_abstr` once `menu_tbuf` left .bss. **ok at `f3be393d`** |
| **vesa** | QEMU | **FAIL at `a6666f56` AND at the base `4be03502`** (`os88bisect.py classify vesa --base 4be03502 -n 2`: 2/2 and 2/2, same leg, pre-existing) - *the disk widget's chrome is still on the bar*. **ok at `f3be393d`** |
| disptitle, dispthm, thm95, wmchrome, dispcheck, disptext | MartyPC | ok (dispcheck again at the tip: ok) |
| colorpresets | QEMU | ok |
| czto, msel, wdmenusu, uilayer, bptrace, deskclock | MartyPC | ok |
| small128 (`full -k`), smallboot, stiksmall | MartyPC, kern_small | ok, at the tip |
| mixerhost | - | SKIP (needs unicorn) |
| stikmenu | - | SKIP (needs mtools) |
| the fast tier | host | 61/61 at every commit (`make`), `hotpar` included on both kernels |

**Broken on purpose**: `menu_x2cell`'s shift as 2 instead of 3 - `toastbar`
and `mixercga` went red (*a 24-character toast is up and NOT ONE PIXEL of the
bar changed*), green on restore. Two breaks that went UNSEEN, worth knowing:
`menu_bemit` skipping the bit-7 mask on the inverted run (font_char indexes
past its glyphs) left `toastbar`/`toastfpg` green, and `fpg_show` answering
"not shown" always (so the trough never fills and the hatch never draws) left
`toastfpg`, `fpgnotch`, `fpgcold` and `curdisk` green: **no row looks at the
toast strip's glyphs or the widget's trough pixels** (CROSS-FILE).

## DEFECTS FOUND

* **A 1024-wide VESA bar overran `menu_bcell`** (FIXED, `f3be393d`, +38
  `.bss` / -24 `.text` on kern_big and kern_emu). `MENU_BCMAX` was Hercules'
  85 cells, and on a VESA primary the clock field starts at cell 98 (`[vid_clk_hx]`
  = 816), so every composition wrote the clock, its padding and the toast strip
  up to 34 cells past the buffer - over `[menu_bn]`..`[menu_blast]` in the middle
  of the pass (whose bytes the emit then lettered where the disk widget had
  been: `tests/vesa.py`'s *chrome still on the bar*, red at the pass's base) and
  into the `.bss` after them, `menu_sseg` and the scratch `menu_tbuf`. FOUND
  when this pass shared `menu_tbuf` with `clk_str`: the About cell's name sat
  where the clock's last seven glyphs land and `tests/mixer.py --adapter vesa`
  read it as `  10:54` - the arithmetic is exact (`menu_abstr + 6` = cell 113,
  the clock's glyphs 11..17). `menu_inval`'s clamp at 84 also meant an
  overdraw never forgot the clock's cells on that bar. `MENU_BCMAX` is
  `(1024 - 8 - MENU_TXT_X0) / 8` = 123 under `VID_EXT` now, and `fpg_hide`'s
  band erase for the part past 85 (VESA's, `553f2a21`) is the composition's own
  run. SPEC.md 120.5 has the row. **This is a VESA defect fixed in this
  concept's file**: the coordinator may want the vesa agent to know, and the
  +38 is resident on every kern_big machine, VESA or not. Dropping it means
  dropping the `menu_tbuf` share with it (+25), or the About cell breaks.
* SPEC.md's `clk_str` sentence named two callers that no longer exist
  (`menu_draw_clock`, `toast_gapend`), and `menu_draw_clock` was named in five
  more places after it became a call and a jump; corrected.
* Not fixed (CROSS-FILE): no row checks the toast strip's glyphs or the
  progress widget's trough.
