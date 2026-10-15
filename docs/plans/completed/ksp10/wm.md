# ksp10 agent "wm" - kernel/wm.inc

Notes appended as decided. Figures are `kernsize` section deltas against the
blessed baseline at `833f13e4`.

## TAKEN

### Batch 1 - the damage re-seed, the empty box, one occlusion walk
kern_big `.text` -82, kern_small `.text` -66.

* **`wm_dmg_rebands`** (the #230 review fix's hunk, `e890eb7`, passed without
  losing it): `wm_dmg_gray`'s `.frames` and `.whole` re-seeded the bands by
  popping the damage off the entry frame and pushing it straight back (10
  bytes each) before `call wm_dmg_bands`. `wm_dmg_bands` already banks the
  damage in `wm_db_r` and reads AX..DX nowhere after, and nothing else writes
  `wm_db_r`, so a second entry after its four stores re-seeds with no
  register at all. -20 big (two sites), -10 small (one). The fix itself is
  unchanged: `.frames` still rebuilds the frames-only region and `.whole`
  still re-owes the zones and re-seeds the bands, not the box.
* **`wm_rect_empty`**: three boxes (`wm_db_b`, `wm_dmg_zb` at `.wdone`,
  `wm_cov_x1..y2`) each spelled six stores to seed the empty union box. One
  17-byte helper. The far-corner sentinel is now -1 for all three (it was
  8000h for the first two); equivalent because every rect folded into those
  two is on the glass (x2/y2 >= 0) and an unfolded box is refused by x1 =
  7FFFh alone in gfx_rect_hit. wm_cov_rect's was -1 already, and it is the
  one a package sees through wm_damage. -16 big, ~-16 small. SPEC.md 30.x's
  dock span sentence that cited wm_dmg_bands' idiom updated.
* **One occlusion walk**: `wm_clip_occl_r`'s loop and `wm_dmg_occl`'s were
  the same loop. Every entry now names its subtract in DI and its start in
  SI/CX and joins at `wm_occl_go`; `wm_clip_subx` and the `wm_clip_ofr` flag
  byte it read per window are gone (DI is the choice). wm_zabove leaves CX = 0
  on CF = 1, so "nothing above" needs no test. -46 big, ~-40 small. Faster per
  window too: `call di` replaces a `cmp byte [mem],0` + branch + jmp.

### Batch 2 - the title bar, the flay clamp, small ones
As committed (146f7878) kern_big `.text` -207 and kern_small -203, of which
138 / 138 were the rect_get/rect_put conversions batch 3 backs out: net
-69 big, ~-65 small.

* **rect_get / rect_put at fourteen sites** - TAKEN IN THIS COMMIT AND BACKED
  OUT IN THE NEXT, see REFUSED.
* **wm_draw_title**: four `mov dx, [di+W_Y]` / `add dx, n` pairs become
  `lea dx, [bx+k]` off the BX the same block just loaded (gfx_fill, the pens
  and thm_tink preserve BX). -12. Same primitive calls, fewer instructions.
* **wm_su_flay's WSU_CLAMP2** macro (two copies, 22 bytes each) is a local
  `.clamp2`. -15. Costs two call/ret pairs (~70 cycles) per wm_su_flay, i.e.
  per window cache op; the macro's comment argued against exactly this, on
  speed. A fiftieth of one gfx primitive call - taken.
* small: wm_min_floor (CX needs no bank; the cap reloaded straight into AX)
  -4; wm_resize_nb's push/pop CX DX round wm_min_floor whose values were
  overwritten next line -4; wm_rz_swap lea + lodsw -4; wm_dmg_stale `x2 <=
  x+w-1` as `x2 < x+w`, and AX needs no bank there -6; wm_su_srect's second
  `cmp [wm_su_son]` (`.named` is only reached with it set) -7; wm_su_bget's
  `or al, al` after a `pop` (ZF already set) -2; wm_damage's four
  pop/pop/ret exits one tail -5; wm_su_drop_all walks records not indices
  -4; wm_an_lerp addresses wm_an_c as [si+16] (asserted) -6; wm_su_piece,
  wm_clip_split -1 each.

### Batch 3 - wm_kent_bp, and the rect_get/rect_put sites backed out
kern_big `.text` -166 cumulative, kern_small `.text` -140 cumulative
(1db2fbb7 had kent at 16 sites; the next commit took the seven save-under
ones back out).

* **wm_kent_bp** - `call wm_kent_bp` IS push ax..bp (kentc_bp's twin, in
  `.text`; that one is in `.cold` and no `.text` caller can reach it near).
  18 bytes, then 4 a site at six seven-push prologues (wm_sz_notify,
  wm_ask_close, wm_paint_dmg, wm_dmg_wins, wm_paint_all, wm_title_set) and 3
  at three six-push routines that end on `kret_di` and now push BP too and
  end on `kret_bp` (wm_dock_clear [kern_big], wm_cov_rect, wm_anim). -15 big
  net. ~95 cycles a call, each entered once per operation. Not given it:
  **wm_clip_rows** (per glyph cell under a clip - hot), **wm_draw_win and
  wm_destroy** (a BP push there deepens the stack under a package's W_PAINT,
  the deepest in the machine), and **the save-under routines** (see REFUSED).
  stkbalance over kernel/kernel.asm kernel/*.inc: 0 unbalanced, base and tip.

### Batch 4 - three shared walk/raise/box idioms
kern_big `.text` -206 cumulative (this batch -40), kern_small `.text` -182
cumulative (-42).

* **wm_znext** (`lodsb / call wm_idx2ptr / test byte [bx+W_FLAGS], 2`):
  wm_paint_all, the occlusion walk, wm_cov_rect and kern_small's
  wm_dock_clear spelled it out; kern_big's wm_dock_clear walked DI instead
  and now walks SI through it too. `mov al, [si] / inc si` -> `lodsb` at the
  other two walks (wm_obscured, wm_su_precover). Faster where it is inline
  (lodsb), one call/ret where it is shared.
* **wm_top_dbp / wm_top_bpd** (`mov di, bx / call wm_top / mov bp, bx /
  mov bx, di`): wm_show_b, wm_fullscreen, wm_front_b, wm_title_set. -10.
* **wm_grow_paint's** fill-and-frame pair is a local `.box` (twice), and
  three `mov dx, bx / add dx, n` are `lea dx, [bx+n]` (with wm_grow_rect).
  Same primitive calls. -12.

### Batch 5 - dead clears and a dead bank
kern_big `.text` -226 cumulative (this batch -20), kern_small `.text` -202
cumulative (-20).

* **wm_clip_set** banked BX round wm_su_drop and reloaded the window from
  [wm_clipwin] for it and for cur_lazyck, on a comment that BX "does not
  survive the occlusion walk" - it does (the walk, the seed and both border
  asks all preserve it, and did before this pass too). The store stays:
  mouse.inc reads [wm_clipwin]. -8.
* **wm_title_set's `.clear`** cleared a list it reached only when
  [wm_clip_n] was already 0. -5.
* **wm_covered's** opening wm_clip_clear: wm_seed_frame sets [wm_clip_n]
  itself and nothing between reads the list. -3.
* **wm_zoom's `.go`** banked SI round a `lea si` only to copy it to DI: `lea
  di` straight. -4.

### Batch 6 - shared epilogues
kern_big `.text` -242 cumulative (this batch -16), kern_small `.text` -218
cumulative (-16).

* Six routines whose pop run and `ret` are identical to another's jump to
  it: wm_show_b -> wm_front_b.go (its raise as well, AL = 1, same four words
  banked; -5), wm_obscured -> wm_fit.out, wm_pref_take -> wm_create.out,
  wm_covered -> wm_zoom.out, wm_top -> wm_lift.out, wm_resize_nb ->
  wm_dock_snap.out. A `jmp` writes no flag, so every CF answer rides through.

## REFUSED

* **rect_get / rect_put at the damage-repaint and save-under sites - 138
  bytes on BOTH kernels, HELD BY THE OWNER, NOT MINE TO TAKE.** I converted
  fourteen sites (wm_dmg_bands' store and load in both arms, wm_paint_dmg,
  wm_dmg_gray's `.fill`, wm_su_owed, wm_su_sub, wm_su_vset, wm_su_srect,
  wm_su_flay, wm_su_try x2 and wm_su_try's two four-word copies sx<->bx as
  get+put pairs at -14 each) before reading rect_get's own header, which says
  the damage-repaint and save-under sites stay inline on purpose, and
  docs/plans/LAST-DROP-BYTES.md 7.10, which has them measured (S2 +1,088
  cycles a damage repaint, S3 ~0.37% of a close) and HELD by the owner.
  Backed out in batch 3. Measured here: kern_big `.text` -138, kern_small
  -138. If the owner releases S2/S3 the sites are exactly the
  `call rect_put/get` lines in commit 146f7878; S3's note prefers GET sites
  (a put is 1.75x a get). The su_try copies are a new shape the 7.10 table
  does not list (8-mov copies, -14 each as a get+put pair, or -13 each via
  wm_cpy4 at ~+120 cycles).

* **wm_rz_swap with `scasw`** (one more byte) and **wm_an_lerp with
  `cmpsw`**: both read ES:DI, and ES belongs to nobody - a stray read of
  A000 loads the VGA latches.
* **A shared `wm_flagw` for the on/off flag setters** (wm_ownbg,
  wm_sizable...): needs a mask register or an inline-word helper that
  discards its own return address under a pushf; the saving was ~10 bytes for
  a helper nobody could read.
* **wm_kent_bp at the seven save-under routines - 25 bytes, kern_big and
  kern_small alike, left for the owner with 7.10.** wm_su_flay, wm_su_edge
  (4 each), wm_su_take, wm_su_try (4 each, `call; push es` + `jmp kret_es`),
  wm_su_vset, wm_su_bytes, wm_su_scrset (3 each, BP pushed, `kret_bp`). It is
  not rect_get, but it is the same trade in the same place: ~95 cycles a call
  and ~12 calls a cached restore is ~1,100 cycles (~0.24 ms) a raise, which is
  the size of the S3 cost 7.10 holds. Built and measured (in 1db2fbb7), then
  backed out.
* **wm_clip_subl folded into wm_clip_subg - ~10 bytes kern_big.** subl is
  "subg with s = 0, then x1 and y1 one more", or "with an origin offset o";
  every spelling needs either a second register on the shared hot path
  (push/pop CX, `add ax, cx` twice, per window per clip build) or the stores
  made a callable body (call/ret on the shared path, +6 bytes there), and
  kern_small, which has no subl, would pay that unless both arms are
  %ifdef'd. Not worth the two arms for ten bytes.


## CROSS-FILE (for the coordinator)

* **wm_kent_bp is a kernel-wide `.text` helper now** (it lives in wm.inc).
  27 seven-push `push ax..bp` prologues sit in `.text` outside wm.inc - vga12
  7, icons 3, font 2, softgfx 2, viddet 2, and one each in clip, fsx,
  instance, kbddiag, moudiag, mouse, sched, stkdiag, ui, vmmouse and
  kernel.asm. Each is -4 bytes as `call wm_kent_bp`, at ~95 cycles a call, so
  only the ones entered per operation qualify: NOT vga12/softgfx/font/mouse/
  sched's (primitives, glyphs, ISRs, the switch), and check viddet/stkdiag/
  kbddiag/moudiag are not `.ovl`-bound or knob-only first. Plausibly instance,
  ui, fsx, clip, icons: ~-20 if all five hold up. A rename to a neutral
  prefix (it is not WM-specific) would be the coordinator's call.
* **tests/deskwhole.py**'s docstring says "`.whole`'s re-seed (`call
  wm_dmg_bands` after its pops)": it is `call wm_dmg_rebands` now, with no
  pops - the break-it-on-purpose line still describes the right instruction
  to take out, under its new name. Comment only.
* **docs/INDEX.md** is regenerated in this branch for
  docs/plans/ksp10/wm.md; every agent's branch will conflict there - take
  any side and re-run `tools/os88index.py`.
* **SPEC.md** (the dock span paragraph near "Empty is `0x7FFF`..`0x8000`")
  edited: it cited wm_dmg_bands' empty-box idiom, which is wm_rect_empty's
  `7FFFh..-1` now. dock.inc's own span keeps 8000h and needs nothing.

## WHAT WAS RUN

* `make` and `make small` after every batch, `make emu` at the tip
  (kernsize[emu] text -242, assembles); the fast tier after every batch,
  61/61 at the tip.
* `tools/stkbalance.py kernel/kernel.asm kernel/*.inc`: 0 unbalanced at the
  base (`833f13e4`) and at every batch from 3 on.
* Soak, one at a time: `deskwhole` after batch 2 (exercises the re-seed both
  ways - `.frames` and `.whole`) and again at the tip; `wmchrome` after batch
  4; `zonedmg` at the tip. All green.
