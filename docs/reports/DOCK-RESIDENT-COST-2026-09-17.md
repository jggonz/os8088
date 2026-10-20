# What SPEC.md 30.5's Dock placement costs `kern_big`, symbol by symbol

**A measurement, not a description.** Taken 2026-09-17 on a four-core cloud
container, `nasm` 2.16.01, at commit `3f0f0cff` on `size-pass-3`'s merge base
`6bfcffb7`, on a tree where `KERN_SIZE` is 112,128 of `KERN_BUDGET` 129,536
and the sections are `.text` 49,977, `.bss` 6,097, `.cold` 41,262, `.ovl`
1,511. It is true of that commit and of no other tree; a later measurement is
a new file.

Reproduce the totals by making `DOCK_OPT` suppressible — `%ifndef DOCK_AB`
around `%define DOCK_OPT 1` in `kernel/kernel.asm`, plus an `%elifdef
KERN_BIG` arm on `MODMAP_START` — and reading `nasm -DKERNSIZE`'s own `ks:`
warning with and without `-DDOCK_AB`. The per-module column is
`tools/kernsize.py --modules` in the same two arms. The per-symbol column is
a listing walk; the numbers below reconcile to the per-module totals **to the
byte**, which is the only reason to trust them.

## The question

> The mechanism is 112 bytes and the feature is 486. What are the other 374
> bytes, and what do they do?

## The answer in one line

**They are not one thing.** **150** bytes are the window manager, the UI task
and the menu bar learning that the desktop band has an x fence and that the
strip has a live rect — spread over **26 routines in six files at 1 to 19
bytes each**, and eleven of them are given BACK by a shared clip query the
feature introduced. **60** are the live-rect state those sites read. **58**
are the basic bodies the module replaces. **90** are the module's own
plumbing outside `dkv` — the `mod_tab` row, the `cw_` shims, `dock_apply`,
`dock_reflow` and `.cold`'s 45-byte `dkf_apply`. **16** are the Control
Panel's Dock page and `[dock_cfg]`. **The largest single item in the feature
is `wm.inc`'s 78 bytes, and it is charged one byte at a time.**

---

## 1. The totals

`kern_big`, `DOCK_OPT` on against off, `ks:` line:

| section | on | off | delta |
|---|---:|---:|---:|
| `.text` | 49,977 | 49,561 | **+416** |
| `.cold` | 41,262 | 41,217 | **+45** |
| `.bss` | 6,097 | 6,072 | **+25** |
| **resident** | | | **486** |
| `.ovl` | 1,511 | 1,422 | +89 |
| `.ovlw` | 5,084 | 5,084 | 0 |
| `KERN_SIZE` | 112,128 | 111,616 | +512 |

`kern_small` pays **0** in every section (`kernsize[small]: unchanged`):
`DOCK_OPT` is inside `%ifdef KERN_BIG`.

Per module (`kernsize.py --modules`, same two arms):

| module | `.text` | `.cold` | `.bss` | sum |
|---|---:|---:|---:|---:|
| `dock.inc` | 245 | 45 | 24 | **314** |
| `wm.inc` | 78 | — | — | **78** |
| `ui.inc` | 30 | — | — | **30** |
| `menu.inc` | 22 | — | — | **22** |
| `ctrl.inc` | 15 | — | — | **15** |
| `mod.inc` | 13 | — | — | **13** |
| `kernel.asm` | 12 | — | — | **12** |
| `vidsel.inc` | 9 | — | — | **9** |
| `fsx.inc` | 3 | — | — | **3** |
| `driver.inc` | — | — | 1 | **1** |
| `font.inc` | −8 | — | — | **−8** |
| `icons.inc` | −2 | — | — | **−2** |
| `viddet.inc` | −1 | — | — | **−1** |
| `vga12.inc` | 0 | — | — | **0** |
| **total** | **416** | **45** | **25** | **486** |

## 2. `dock.inc` — 245 `.text`, 45 `.cold`, 24 `.bss`

### 2.1 `.text`

| symbol | bytes | what it is for |
|---|---:|---|
| `dkv` | 56 | the fourteen-slot far-pointer table: `KERNEL_SEG:<basic body>` at rest, the module's segment and body while one is mounted |
| the fourteen entry points | 56 | `jmp far [dkv + 4i]`, 4 bytes each — `dock_geom`, `dock_band`, `dock_force`, `dock_paint`, `dock_px_hit`, `dock_click`, `dock_rclick`, `dock_slot_rect`, `dock_force_r`, `dock_drop`, `gfx_hole_arm`, `dock_pass`, `dock_hole_win`, `dock_hole_sub` |
| `dkb_band` | 29 | the BASIC band: `vid_band_x0/xe`, `vid_desk_zx`, `vid_dock_y0` for a bottom strip |
| `dkb_geom` | 23 | sends basic geometry setup to the boot overlay before `spl_finish` and to `CTRL.DRV` after |
| `dock_reflow` | 16 | `dock_geom` + `desk_rowcalc` + `wm_refit` + `wm_su_drop_all` + `dock_force`, as one near entry `dkf_apply` can far-call |
| `db_hit` (grew) | 15 | the basic hit tester's extended-desktop rejections — **see §5.1, this is §39.19's question and not the Dock's** |
| `dock_px_rect` + `dock_swap_side` | 26 | the span `dock_paint` PAINTED, as a rect, with the pair swapped on a side strip. `wm_dock_under`'s damage |
| `dock_live_hit` | 9 | does a rect overlap the LIVE strip? Three callers |
| `dock_apply` | 6 | the `.text` half of the setting change: `call COLD_SEG:dkf_apply` |
| `dkb_force_r` | 6 | basic `dock_force_r`: overlap, then `db_force_x` |
| `dkb_clc` + `dkb_ret` | 2 | the two rest bodies four slots point at |
| `dock_cfg` | 1 | **the setting itself** — `.text` data, not `.bss`, because `SYSTEM.CFG`'s `DK` record is stored straight into it and its default has to survive a warm boot |
| | **245** | |

### 2.2 `.cold` — 45

`dkf_apply`: test the setting, `mod_need` or `mod_drop`, call the module's
`dkx_hook`/`dkx_unhook`, then `dock_reflow` with the refusal flag banked.

### 2.3 `.bss` — 24

Exactly what a KERNEL routine reads. Everything else the advanced Dock owns is
in `DOCK.DRV`'s own image.

| symbol | bytes | read by |
|---|---:|---|
| `dock_lx1` `dock_ly1` `dock_lx2` `dock_ly2` | 8 | `dock_live_hit`, `wm_su_owed`, `wm_dock_clear` |
| `dock_rc` `dock_fc1` `dock_fc2` | 6 | the live rule and field crosses — `dkx_live_set` writes them as one `rep movsw` with the rect above |
| `dock_lc1` `dock_lc2` | 4 | `dock_px_rect` |
| `dock_c0` | 2 | `db_rect_ax` — every tile's across coordinate |
| `dock_thk` | 2 | `wm_dock_snap`'s half-a-strip nudge |
| `dock_side` | 1 | `dock_px_rect`, `wm.inc`, `ctrl.inc` |
| `dock_up` | 1 | `ui.inc` — an open strip answers a press first |
| | **24** | |

`driver.inc` adds **1** more: `drv_cfg`'s row for the `DK` key.

## 3. The other 171 `.text` bytes, outside `dock.inc`

### 3.1 `wm.inc` — 78, over nineteen routines

This is the single largest item in the feature and there is no fat symbol in
it. Every one is either *the desktop band has an x fence now* or *the strip
has a live rect now*.

| routine | bytes | what changed |
|---|---:|---|
| `wm_dmg_bands` | +19 | the dither seeds clamp to `[vid_band_x0]`/`[vid_band_xe]` as well as to the dock row |
| `wm_dock_snap` | +12 | the nudge is the BOTTOM strip's alone, and against `[dock_thk]`/2 rather than `DOCK_H`/2 — so a hidden strip nudges nothing |
| `wm_su_owed` | +9 | a save-under owes the whole live rect, as a widening and never a replacement |
| `wm_su_take` | +8 | ...and the same question on the way in |
| `wm_fit_box` | +7 | the primary arm fits inside the fence |
| `wm_obscured` | +6 | " |
| `wm_clip_rect` | +5 | " |
| `wm_su_try` | +5 | " |
| `wm_disp_rest` | +4 | " |
| `wm_zoom` | +3 | " |
| `wm_paint_all` | +2 | the dither stops at the fence |
| `wm_clip_occl_r` | +2 | |
| `wm_dmg_occl` | +2 | |
| `wm_fit` | +1 | |
| `wm_land_snap` | +1 | |
| `wm_dock_clear` | **−3** | the rewrite is SMALLER than the `y+h >= dock_y0` test it replaced |
| `wm_clip_test` | −2 | |
| `wm_clip_rows` | −2 | |
| `wm_paint_dmg` | −1 | |
| | **+78** | |

**Eleven of the nineteen are two-instruction substitutions inside a
routine** — `mov ax,[vid_band_x0]` / `mov cx,[vid_band_xe]` where the
pre-feature code had `xor ax,ax` / `mov cx,[vid_pw]`, at **+1 to +7 bytes**.
Four more are the `CLIPQ` conversion paying bytes BACK. That matters for §6.2:
a site like that cannot be vectored, because it is not a routine.

### 3.2 The rest

| module | bytes | what |
|---|---:|---|
| `ui.inc` | +30 | `ui_task` +18 (the `dock_pass` tick call and the press paths), `ui_rdown` +12 — an OPEN strip answers a left or right press before `wm_hit` |
| `menu.inc` | +22 | `menu_drop` banks and restores `[gfx_hole]` and `[wm_clip_n]` around a pull-down: two `xchg`/`push` pairs and two `pop [mem]` |
| `ctrl.inc` | +15 | `cp_items` +8 (the Dock page's row), `cp_s_dock` 5 (`'Dock',0`), `cp_ltop` + `cp_lsel` 2 (the list scrolls now) |
| `mod.inc` | +13 | `mod_f_dock` 9 (`'DOCK.DRV',0`) + `mod_r_dock` 4 (the table row) |
| `kernel.asm` | +12 | `cw_dock_band` 4, `cw_gfx_clip_query` 4, `cw_dock_apply` 3, `cw_kretf` 1 |
| `vidsel.inc` | +9 | three calls: `dock_geom` at a mode switch and a display relayout, `dock_band` on fullscreen return |
| `fsx.inc` | +3 | one `dock_drop` — an open strip's hole would clip the app out |
| `font.inc` | **−8** | |
| `icons.inc` | **−2** | |
| `viddet.inc` | **−1** | |
| `vga12.inc` | **0** | `gfx_clip_query` +18 against nine `gfx_*` entries at −2 each: the feature turned an inline `cmp word [wm_clip_n],0` at ten sites into one shared helper, and it came out **exactly even** |

The three negative rows and `vga12.inc`'s wash are the same change: SPEC.md
30.6.1's `CLIPQ`/`CLIPQF` macro. **The feature made the clip query smaller at
every site that was not about the Dock at all**, which is 11 bytes back.

## 4. `.ovl` — 89, and none of it resident

~74 is `ovl_dock_geom` + the `DOCK_GEOM` instantiation `ovd_geom` (counted
from the encodings), and the remaining ~15 is `SYSTEM.CFG`'s `DK` key inside
the boot settings parser. `DOCK_GEOM` is instantiated **twice** — once here
and once in `CTRL.DRV`'s `.modc` — and neither copy is resident, which is why
moving work INTO it is the cheapest move this feature has.

The blob is not tight: `.boot2` is 2,250 of `OVL_AT` 2,624 and `.ovl` 1,511 of
1,984, so **847 bytes of blob slack**, one pool.

## 5. Three things the itemisation found that the totals hide

### 5.1 15 of the 486 are not the Dock's question

`db_hit`'s `+15` is three compares and a `.miss` arm that reject a press below
the primary's last row or right of its last column — *"another display never
owns a Dock tile"*. That is SPEC.md 39.19's extended-desktop question and it
is true of the bottom strip too; it is inside `%ifdef DOCK_OPT` because it
arrived with this feature, not because the feature needs it. `kern_small` has
one display and cannot fail the test.

### 5.2 `dkb_band` looks redundant and is not

`vid_ctx_init` (`viddet.inc`) already sets `[vid_dock_y0]` to the bottom
strip's band **in both arms**, and `[vid_desk_zx]` to `[vid_w] - 56`. So
`dkb_band` looks like 29 bytes re-doing `vid_apply`'s work. It is not, for two
reasons and both are about a machine `kern_small` cannot be:

- `[vid_band_x0]`/`[vid_band_xe]` exist only under `DOCK_OPT` and **nothing in
  `viddet.inc` writes them**, so on fullscreen return they would keep a side
  strip's fence across a mode change.
- `vid_ctx_init` anchors the drive column to `[vid_w] - 56` — the whole
  desktop — where SPEC.md 30.5 anchors it to `[vid_band_xe] - 56`, the
  PRIMARY minus a right-hand strip. On an extended desktop those differ.

And `dock_reflow` reaches `dkb_geom` with no `vid_apply` in front of it at all
(a Control Panel change from Left back to Bottom), where `[vid_dock_y0]` still
holds the side strip's `ph`. **Refused: 29 bytes, and it would be wrong on
two of the three machines the kernel supports.**

### 5.3 The span tool will lie to you about `dock_swap_side`

`/tmp/claude-0/span.py` reports `dock_px_rect 15` and `dock_swap_side 11`.
`dock_swap_side` is a **fall-through label with no callers**, not a routine;
folding it away saves **0 bytes**, measured. Adjacent-symbol deltas are spans,
not routine sizes, and a second entry point into one body reads as a second
body.

---

## 6. "Resident = BOTTOM ONLY" — priced

The proposal: leave the kernel's own Dock bodies doing what `kern_small`'s do
and nothing more, and move every trace of left/right/hidden/auto-hide into
`DOCK.DRV`.

### 6.1 What is actually left/right/hidden, and what is not

| group | bytes | moves? |
|---|---:|---|
| **M** the module mechanism — `dkv` 56, the fourteen entries 56, `dkb_clc`/`dkb_ret` 2, `mod.inc`'s row 13, the three `cw_` shims 8, `dock_apply` 6, `dock_reflow` 16, `dkf_apply` 45 | **202** | **no** — it is how a module is reached at all, and it is what SPEC.md 30.5 already calls the advanced half's price |
| **C** the Control Panel's Dock page, resident part 15, plus `[dock_cfg]` 1 | **16** | **no** — the page's row and its string are the feature's front door |
| **G** the basic bodies the module replaces — `dkb_geom` 23, `dkb_band` 29, `dkb_force_r` 6 | **58** | **no** — these ARE the bottom-only bodies the proposal wants to keep |
| **S** the live-rect state and its two readers — `.bss` 25, `dock_live_hit` 9, `dock_px_rect` 26 | **60** | **only by routing**, see 6.2 |
| **W** the awareness — `wm.inc` 78, `ui.inc` 30, `menu.inc` 22, `vidsel.inc` 9, `db_hit` 15, `cw_gfx_clip_query` 4, `fsx.inc` 3, less `font.inc`/`icons.inc`/`viddet.inc`'s **−11** | **150** | **only by routing**, see 6.2 |
| | **486** | |

### 6.2 Routing the awareness costs more than the awareness

Groups **S** and **W** are **210 bytes over 26 routines in six files**. A
routed operation in this kernel is **8 resident bytes** — a four-byte entry
and a four-byte `dkv` slot — so asking the module those 26 questions is
**208 bytes before a single module-side byte**, to remove 210. On paper that
is a **2-byte win**, which is to say none.

It is not a win in fact, because **eleven of the nineteen `wm.inc` sites are
not routines.** They are `mov ax,[vid_band_x0]` / `mov cx,[vid_band_xe]` in
the middle of `wm_fit`, `wm_zoom`, `wm_obscured`, `wm_clip_rect` and the rest,
one byte dearer than the `xor ax,ax` they replaced. A vector cannot replace
two instructions inside a routine; each would become a `call` returning a pair
in registers, which is **3 bytes at the site plus a body plus a slot** against
the **1 byte** it costs today.

**So group W does not move, and it is the larger half.** And the 26 is
already generous to the proposal: `vid_switch`, `vid_fsx_leave`,
`vid_disp_init` and `fsx_run` are bare `call dock_geom` / `call dock_band` /
`call dock_drop` sites that go through `dkv` **today**, so they are not
awareness the kernel could stop having — they are the mechanism, counted
here because the A/B charges them to the feature.

### 6.3 What a module-absent machine does today — and it is already right

Two behaviours, both already built and both already asserted by
`tests/dockmodule.py` and `tests/dockpos.py`:

- **`[mod_r_dock]` is 0 and the setting says Left.** `dkf_apply` calls
  `mod_need`, it refuses, and the `.fell` arm writes `[dock_cfg] =
  DOCK_P_BOTTOM` — *"live controls describe the fallback"* — then runs
  `dock_reflow` with `mod_need`'s CF=1 still banked, so the Control Panel
  reports the refusal and its radio moves to Bottom. **The machine falls back
  to Bottom, visibly, and does not refuse the setting.** `SYSTEM.CFG` is left
  untouched (SPEC.md 30.5), so the next boot with the disk present gets Left
  back.
- **The system disk is out of the drive when the strip needs a repaint.**
  Nothing happens, because **the module is pinned for as long as a non-default
  setting is live.** `mod_need` runs once — at the settings reader after the
  system volume is mounted, or at the Control Panel click — and `mod_drop`
  runs only on the way back to Bottom. A repaint never reads a disk.
  `tests/dockpos.py` asserts both ends of that: *"advanced Dock has exactly
  its rounded module claim"* while Left and Right are set, and *"disabling the
  Dock frees its old claim"* at Bottom.

**So the disk is not what keeps a resident renderer.** The reason is 6.2's
arithmetic and one thing under it: every site in group W runs **under the gfx
lock on the repaint path**, and a kernel that cannot answer *where is the
strip* without a far call has moved the question, not removed it.

### 6.4 The honest number

**`kern_big`'s Dock placement cannot reach 400 resident bytes by moving code.**
The dispatch is at its floor at 112 (`docs/plans/LAST-DROP-BYTES.md` §7.8 has
the three cheaper schemes priced, and
`docs/plans/LAST-DROP-BYTES.md` §7.8.1 the one that is not refused), the
basic bodies and the Control Panel plumbing are the thing being kept, and the
210 bytes of awareness and state cost 208 to route and cannot be routed
anyway. Taking `docs/plans/LAST-DROP-BYTES.md` §7.8.1's 42 gives **444**.

**400 needs SCOPE, and the scope question is one sentence: is a side or hidden
strip worth 150 bytes of window manager and 60 of live-rect state on every
`kern_big` machine, for ever?** That is a decision for whoever asked for
SPEC.md 30.5, not a build fix — CLAUDE.md is explicit that a feature which
does not fit is a discussion and never a quiet cut.
