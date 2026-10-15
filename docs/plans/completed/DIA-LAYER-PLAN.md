> **Status: landed** (DIA v2, PR #251). The design record for SPEC.md 110 v2 -
> the desk layer, the widget treatment and the Desk Calendar as the owner's
> second mockup. SPEC.md 110 cites its section 7 as "the v2 design's open
> decision N"; the owner had answered none of them when it was built, so
> every default below is what shipped, and SPEC.md 110.9 records each place
> the code proved this wrong. (The DIA section was numbered 107 when this was
> written; its citations here are renumbered to 110.)

# DIA v2: the desk layer, the widget treatment, and the Desk Calendar as mockup2

Judge and synthesis. Read-only against `/Users/jggonz/Repos/os8088-dia` at
8311137f (feature/dia, PR #251). I re-checked every claim below that the
design depends on. I ran `tools/kernsize.py` on kern_big (it printed
`unchanged`):

| kernel | image rung left | cold rung left | low rung left | source |
|---|---:|---:|---:|---|
| kern_big | **183** | 351 | 34 | measured here |
| kern_emu | 430 | **227** | 34 | map-dia, same tree |
| kern_small | 315 | **65** | 204 | map-dia; SPEC §110.6 |

The binding limits are **kern_big's image rung (183 B, for .text plus .bss)**
and **kern_emu's cold rung (227 B)**.

The mockup (`mockup2.png`, divided by 2.2625) gives these native VGA
measurements:
- **Cards.** The column is x 373..548, 176 px wide. Card 1 is y 42..219
  (178 rows), card 2 is 224..315 (91), card 3 is 321..377 (57), and the tag
  is 381..393.
- **Card 1.** The big "24" is two 24x24 digits at rows 7..31 of the card.
  The nav line is at rows 44..52, the weekday header at 60..68, and the
  weeks start at row 77 with a 16-row pitch and a 23 px column pitch. Today
  is an inverse box about 20x16.
- **Card 2.** The agenda is at rows 22 and 36, the rule at row 50, and the
  11x11 boxes at rows 57 and 73.
- **Card 3.** The bell is 16x17 at x+9, "21:00" is at x+35, and the links
  are at rows 41..48.

---

## 1. Scores

| criterion | kernel-chrome | include-chrome | risk-first |
|---|---:|---:|---:|
| Fidelity to the asks and the mockup | 6 | 7 | 8 |
| Interaction correctness against the code | 5 | 6 | 8 |
| Resident cost against headroom | 5 | 8 | 7 |
| 8088 cost of hover and the redraw | 8 | 4 | 9 |
| Testability | 6 | 7 | 9 |
| Build risk | 4 | 6 | 6 |
| **Total (of 60)** | **34** | **38** | **47** |

**Winner: risk-first.** Its core is kept:
- a kernel z-band;
- show by the damage pass;
- a posted drag;
- a kernel hover edge that wakes the package;
- package-drawn chrome, with the frame equal to the face plus one ground
  column;
- no SAVEU;
- the `t_chromesites` ratchet.

Five ideas are grafted in from the other two designs:
1. The **slab composer** from kernel-chrome: a 704-byte band reused per
   32-row slab, as a new `GFXE_TEXT` capability in `apps/os88gfx.inc`. It
   replaces risk-first's 8 KB whole-face bss band.
2. The **key target goes into the cold `fdlg_top_x` stub** (kernel-chrome).
   That costs 0 resident bytes, where risk-first spent 5 B .text and 22 B
   .cold.
3. **Focus is polled only while the widget owns the bar** (include-chrome's
   poll, bounded to the time the widget is in use). It replaces
   `menu_dwake_x`, saving 5 B .text and 28 B .cold.
4. **Layout by screen height, so EGA takes the Hercules layout** (all three
   designs found the PREFER bug; kernel-chrome stated it most precisely).
5. **Keep the Calendar and Remind menus while the widget is active**
   (kernel-chrome). The menus already exist, so this adds no code, and they
   carry what has no in-widget control.

### 1.1 Claims that are wrong or need correcting

- **kernel-chrome: the culled show is unsafe.** It draws a desk window on
  show by `wm_draw_win` with `[wm_dmg_cullq]=1`, outside `wm_dmg_wins`.
  - `wm_dw_cull` sets `[wm_dmg_cull]=1`, and `wm_clip_test` then **rounds
    OUTWARD** (wm.inc, "THE CULL ROUNDS OUTWARD": containment with the
    corners exchanged).
  - So any `font_char`/`font_run` cell or icon body that straddles a
    covering window is drawn whole, over that window. Inside `wm_dmg_wins`
    this is repaired, because the frame grows the damage for the windows
    above. Outside it, nothing repairs it.
  - It would be correct only if a package never drew a glyph or icon in
    W_PAINT. That is not an invariant anyone can hold.
- **kernel-chrome: the section number is taken.** "§11.102 (new)" already
  exists (SPEC.md:19600, the sizing constants), so the next free number is
  §11.103.
- **kernel-chrome: line reference.** `W_ZOOMR` is at wm.inc:120, not :99.
- **kernel-chrome: aliasing the FACE into `W_ZOOMR`** hangs a correctness
  invariant on "a desk window is never zoomed". It works, but it is the kind
  of coupling §76.11.1 regretted. Rejected along with the module.
- **kernel-chrome: an invisible grab band.** Its resting top band is 18
  opaque rows of ground above card 1. A press on what looks like bare
  desktop grabs the widget and activates it. The mockup has nothing there.
- **include-chrome: far more than nine sites.** It says "nine WF_FULL sites"
  need widening. `grep WF_FULL kernel/wm.inc kernel/instance.inc` finds
  **35 references**, among them 1139, 1154, 3531, 3729, 3751, 3769, 3905-3937,
  4289, 4320, 4626, 4662, 4711, 4792, 5563, 5568, 5640, 5973, 6410, 7557,
  7996, 8087, 8401, 8942, 8947, 9953, 10649, 11012, 11027, 11044 and
  instance.inc:1727. Some must widen and some must stay FULL-only. A ratchet
  that classifies each one is the only safe way to do this.
- **include-chrome: the poll cost is real and permanent.** "0.25-0.7% per
  widget forever" is honest, and it multiplies the DIA's timer fires by 9.
  That is the wrong standing cost on a 4.77 MHz target when a 13-byte
  resident edge gives zero idle cost.
- **map-zorder: CF cannot survive `ui_bill` cheaply.** It says "W_ONCLICK
  CF=1 = drag, plus a pushf/popf in `ui_bill`, 2 B". In `ui_bill`
  (ui.inc:912-941), `wm_pkgcall` is followed by `or di,di`, `inst_charge`
  and `add sp,6`, and all three write flags. A `pushf` after the call
  displaces the banked words that the pops expect. A posted request is the
  correct mechanism; risk-first and map-chrome both say so.
- **map-zorder: the bar should move.** Its "skip `menu_activate` for desk
  windows at ui.inc:438" contradicts the user's "not owning the menu bar
  unless it is the active thing". A press on the widget should make it
  active, and the existing line already does that.
- **risk-first: bss and cold budget.**
  - Its 8 KB fixed face band is too much bss for one widget, because the
    same image runs on kern_small's 128 KB floor machine.
  - Its ~215 B of .cold against kern_emu's 227 leaves 12 B, which is no
    margin. The grafts cut it to about 165 B.
- **risk-first: confirmed claims.**
  - BLIT1 is exact against any clip region: SPEC.md:17983 says "gfx_blit1
    bands, exact against any region".
  - Show through `wm_paint_dmg` marks the window, because `wm_dmg_stale`
    takes `.mark` when `[wm_dmg_stwin]==0` (wm.inc:4831).
  - `wm_snap_ax` puts frame x at 7 mod 8, so content x = W_X+1 is
    8-aligned (wm.inc `wm_snap_ax`: `inc / and 0xFFF8 / dec`).

  So the extra ground column costs no snap code.

---

## 2. The final design

### 2.1 Kernel: the desk layer (kern_big and kern_emu; all under `%ifdef KERN_BIG`)

**Flags** (wm.inc:188-291; the free high-byte bits are 10-12):
- **`WF_DESK equ 0x0400`.** Added to `WF_HIBITS` (wm.inc:286), so the shape
  masks in `wm_cursor`/`cur_shape_pass` and the `CUR_NWSHAPE` `%error`
  follow by construction.
- **`WF_HOVER equ 0x0800`.** Also added to `WF_HIBITS`. Set and cleared only
  by the hover edge.
- **`IF_DESK equ 4`** (I_FLAGS bit 2, instance.inc:26; bits 2-7 are free).
- **Mirrors.** The SDK gets them in `apps/os88api.inc` as read-only bits of
  `[es:bx+W_FLAGS]`; the "never by hand" rule still binds writes. Also
  mirrored in `tools/os88geom.py` and held equal by `t_mirror`.

**Invariant:** every WF_DESK window occupies `wm_zord[0..k-1]`
(wm.inc:11499, [0] is the backmost). There is no count byte. The writers
keep it as follows:
- `wm_create` appends at the top, unchanged. The window is born hidden and
  ordinary.
- **MAKE** (the cell below) moves the entry to [0] once, so the newest
  widget is backmost.
- **`wm_lift`** (wm.inc:4452) gains `test byte [bx+W_FLAGS+1],WF_DESK>>8 /
  jnz .out` before `wm_zcut`: **+6 B .text**. It is the only reordering
  writer.
- `wm_destroy`'s `wm_zcut` compaction keeps the order.
- A comment at `wm_zord` names the invariant and its four writers. The
  `deskz` row asserts it after every gesture.

**Changes, each priced:**

| touch point | change | .text | .cold |
|---|---|---:|---:|
| `wm_top` wm.inc:4560 | the loop's visible test becomes visible-and-not-desk. Every caller then means "the front ORDINARY window": the key router, Close Window and `ui_loc_gate` (ui.inc:2170/2785), `menu_check` (menu.inc:495), `wm_raise`'s outgoing title, `wm_dmg_wins`' pinstripes, `dock_act`, the precover and grow-box tests, `OSAPI_WM_TOP` | 6 | |
| `wm_lift` wm.inc:4452 | no-op for desk | 6 | |
| `wm_show_b` wm.inc:3341 | desk arm before `wm_top`/precover: `or` visible, `wm_win_rect`, `wm_dmg_union`, `wm_paint_dmg` with `[wm_dmg_stwin]=0`, so the widget is marked, and windows above it only where it overlaps them. No precover, no zoom, no `wm_raise`, no `menu_activate`. This is the drag-release pattern (ui.inc:1653-1655), and it runs under the lock `wm_show` already holds | 22 | |
| `wm_front_b` wm.inc:4394 | a visible desk window takes the existing already-frontmost arm (`menu_activate`, `menu_draw_bar`, `wm_dock_under`): it activates and never moves. This covers the keyboard mouse (mouse.inc:3232), `ui_rdown`'s raise, `app_launch` at the cap and a package's `OSAPI_WM_FRONT` | 8 | |
| chromeless sites | the "no chrome" `WF_FULL` byte tests become `test word ...,WF_FULL\|WF_DESK` (+1 B each; about 12 sites: wm_hit 4626, wm_win_rect 4711, wm_dmg_shadowed 4792, wm_content 5568, wm_geom 5640, the obscured seed 5973, wm_seed_frame 6410, wm_draw_title 7557, title-set 8087, su-occl 8942, plus whatever the ratchet classifies). The fullscreen-LATCH sites stay FULL-only: 1139, 3531, 3910 (keep the snap), 8401, 9953, 11012/11044, instance.inc:1727. **`tests/unit/t_chromesites.py`** classifies every reference | 12 | |
| `wm_draw_win` wm.inc:10649 | desk skips outline, shadow, title, `wm_su_try` and the white fill, and goes straight to `.paint`. OWNBG is forced by MAKE, and W_PAINT runs under `wm_dw_cull` as usual | 7 | |
| ui.inc:457 content press | after `mov si,bx`: `test byte [si+W_FLAGS+1],WF_DESK>>8 / jnz .content_front`. `menu_activate` at ui.inc:438 has already made the widget active. W_ONCLICK runs under the lock, `[ui_armw]` is set, and W_ONMOUSEUP arrives | 6 | |
| keys | in the cold `fdlg_top_x` stub (fdlg.inc:1223), the `.no` arm under KERN_BIG: if `[menu_win]` is nonzero and its W_FLAGS has both 2 and WF_DESK, BX = `[menu_win]`. A modal chooser still wins (it is tested first). `desk_key_x` is already gated on `menu_win == 0` (desksc.inc:308) | 0 | ~16 |
| `ui_drag` (ui.inc ~1580, before `ui_drag_phase`/`wm_dc_take`) | a desk window takes `.nocache`. This removes the covered-bank ghost (`wm_dc_take` banks from the glass) and the odd-dy dither seam (the phase snaps x only) | 6 | |
| posted drag pick-up, `.content_front` after `gfx_unlock` | `cmp si,[ui_dragq] / jne .yj1 / call COLD_SEG:ui_dragq_x`. The cold body zeroes `[ui_dragq]` and `[ui_armw]` (the drag eats the release), loads CX/DX from `[ui_dragx]/[ui_dragy]` (seeded at ui.inc:479), and far-calls `cw_ui_drag`, a new 4 B shim. The lock is free here, as ui_drag needs | 11 + 4 | ~22 |
| hover edge, mouse.inc:4853 after `call wm_hit` | `cmp bx,[cur_hovw] / je .h / call COLD_SEG:cur_hov_x / .h:`. The body XCHGs `[cur_hovw]`; on the old window, if WF_DESK, it clears WF_HOVER and calls `wm_wake`; on the new one, if WF_DESK and `[fdlg_win]==0`, it sets WF_HOVER and calls `wm_wake`. It preserves AX/BX. It only ever sets or clears, never toggles, so a destroyed or reused record cannot desynchronise | 11 | ~40 |
| dock dock.inc:837, ~1082 | `db_key` returns 0 and `db_hit` reports no tile for IF_DESK | 12 | |
| DOCK.DRV dockmod.inc:886, 1112, 1303 | `dkx_key`, `dkx_hit` and `dkx_map` (`.count`/`.walk`) skip IF_DESK | module ~16 | |
| instance.inc:985 | `inst_alloc_di:` a 3 B entry into `inst_alloc`'s `.hit` tail | 3 | |
| loader.inc step 5 (~:1041) | header flag bit 6 `OS88_F_DESK`: scan `inst_tab` from INST_MAX-1 down, then `inst_alloc_di`. The widget takes the last slot, so its empty tile position is at the strip's far end and the user's first app takes tile 0, as in the mockup | | ~16 |
| `OSAPI_WM_DESK` 0x046D | `OSAPI_RCSLOT osapi_wm_desk_x`. Verbs: AL=0 QUERY (CF=0 the layer exists); AL=1 MAKE BX=win; AL=2 DRAG BX=win (`[ui_dragq]`=BX; legal only inside W_ONCLICK); AL=3 RELEASE (if `[menu_win]==BX`: `cw_menu_activate(wm_top)` + `cw_menu_draw_bar`, existing thunks at kernel.asm:7298/7312). MAKE refuses with CF=1 a visible, WF_FULL, WF_SIZABLE or not-yours window (`inst_win_owner` against the caller); otherwise it ORs in WF_DESK\|WF_NOANIM\|WF_OWNBG, clears WF_SAVEU\|WF_1BPP, sets IF_DESK on the owner record and moves the z entry to [0] (`wm_zord` is DS data, so no thunk is needed) | 6 | ~70 |
| `OSAPI_GFX_DESK` 0x0473 | `OSAPI_RSLOT thm_desk` (vga12.inc:4478): AX..DX rect, lock held, the theme's desktop ground. Bright is `gfx_fill_gray`'s screen-phased dither, Dark is black, Color is teal, and it is per display through GFXDISP | 6 | |
| table assertion kernel.asm:~4565 | 99 rare cells become 101; `osapi_table_end` becomes 0x0479 | 0 | |
| bss | `cur_hovw dw`, `ui_dragq dw` | +4 bss | |

**Totals:**

| kernel | .text + .bss | .cold | DOCK.DRV |
|---|---|---|---|
| kern_big | **~126 B .text + 4 B .bss**, about 130 against 183 left, so ~50 B of margin (±25% per line). If the ratchet widens more sites, about +5 | **~164 B** against 351 | ~16 B |
| kern_emu | the same against 430 | 164 against **227** (63 left) | the same |
| kern_small | **12 B .text** (the two cells) against 315. `osapi_wm_desk_x equ mod_gone` (the loader.inc:1898 precedent: stc/retf), and `OSAPI_GFX_DESK` is `thm_desk`, which is `gfx_fill_gray` on that build. **0 cold bytes**, and nothing else compiled | 0 | 0 |

**If W1 measures a crossing**, this is the cut order. The decision goes to
the owner (CLAUDE.md: a rung is not a build fix).
1. Move `wm_show_b`'s desk arm behind a 5 B far call into .cold (−17
   .text, +22 .cold).
2. Fold the hover edge's inline compare into the far call (−6 .text, +13 µs
   per mouse packet).
3. Accept the rung: KERN_SIZE +512, 59 to 58 steps.

**What is deliberately NOT in the kernel:**
- no new module;
- no FACE record;
- no kernel-drawn widget chrome;
- no hover timer;
- no menu focus wake;
- no SAVEU bank for widgets.

### 2.2 Interaction against the actual code

- **Press on a card.**
  1. `wm_hit` answers region 0 everywhere on a desk window, because of the
     widened 4626 test.
  2. ui.inc:438's `menu_activate` hands the widget the bar. Its CF=1 path
     repaints the bar.
  3. The ui.inc:457 arm goes to `.content_front`.
  4. W_ONCLICK runs under the lock, and the release is armed.

  The front app window keeps its z position and its pinstripes. This is the
  same split as a bare-desktop click today (bar = Locator, stripes on
  `wm_top`).
- **Press on the tag row or a gap.** The include's prefilter posts DRAG and
  returns. `.content_front` sees `[ui_dragq]==SI` after unlock and runs the
  kernel's `ui_drag` unchanged: XOR outline, clamps, `wm_snap_ax`,
  `wm_dock_snap`, and `wm_paint_dmg` over the vacated and new rects, with no
  drag cache.
  - The drop costs one W_PAINT of the widget (~18 calls, below).
  - A press that does not move repaints nothing (ui_drag's rule).
  - `ui_tdbl`'s zoom and `.tb_raise` are never reached.
- **Close.** A press on the revealed × arms it in the package, which lights
  it with one BLIT1. A release over it runs `dia_quit`:
  1. `dia_close` (os88dia.inc:613): `DIA_ONCLOSE`, remove the row by name,
     save.
  2. On CF=0, `OSAPI_WM_CLOSE` (0x0368), then return without drawing.

  Sliding off cancels (the §13.5 gesture), and diaswap's declined-once case
  is unchanged. **0 kernel bytes.** While the widget owns the bar, the
  app-name cell's Close (§12.7) is the keyboard route through the same
  negotiator.
- **Keys.** They go to the widget only while `[menu_win]` is that widget,
  that is, after a press on it or a keyboard-mouse front. They return on any
  press elsewhere (ui.inc:438/503), on Esc (RELEASE), or on any raise
  (`wm_raise`'s `menu_activate`).
- **Right press.** `ui_rdown`'s `wm_front` activates the widget and stops.
  W_ONRC is not delivered (open decision 11).
- **Modal questions.** `os88ui_ask` (os88ui.inc:2694) is an unbound ordinary
  window appended on top of the z-order, so it lands above every app and
  over the widget and takes the bar. When it closes, `menu_check` gives the
  bar to `wm_top`. The Standard File chooser keeps `fdlg_grab_x` (presses)
  and `fdlg_top_x` (keys) over widgets.
- **Close Window and cycling.** Close Window closes `wm_top`, which is never
  a widget, and it greys when only widgets are up. There is no cycling key in
  the tree; §11.103 records that a future one skips WF_DESK.
- **Hover over a covered widget.** `wm_hit` answers the topmost window, so a
  pointer over the covering window is a leave. A raise or open over a
  standing pointer sets `[cur_shchk]` (wm.inc:3477, 5053), so the edge still
  comes.
- **Fullscreen and saver.** `wm_paint_all` starts at `wm_fs_zpos`, so a
  widget under a WF_FULL window is not drawn and the full window wins
  `wm_hit`. An fsx bracket freezes UI passes.
- **Minimize.** A widget has no tile, so a minimized widget could not come
  back. It is a written rule that a DIA never calls `OSAPI_WM_MINIMIZE` or
  `OSAPI_FULLSCREEN` on its widget, and os88dia never does (open decision
  12).

### 2.3 The widget treatment: package-owned, shared as source in `apps/os88dia.inc`

It is opt-in by `%define DIA_WIDGET`, so `deskcal` stays v1 until W4.

**Geometry:**
- The FACE is the column of cards plus the tag row, 22 cells (176 px) wide.
- The desk frame is **177 x H**: one column of desktop ground at W_X, then
  the face at W_X+1.
- `wm_snap_ax` already lands W_X at 7 mod 8, so the face is 8-aligned for
  free.
- In the titled fallback (kern_small, or QUERY refused) the frame is
  178 x (H − tag − last gap + TITLE_H + 1), and the face is again at W_X+1.
  So **x is the same in both forms**.

**Pixel ownership, every pixel written once per paint:**

| pixels | drawn with | owner |
|---|---|---|
| left column W_X (full height); each gap (face width); ground either side of the plate | `OSAPI_GFX_DESK` | include |
| the plate (rest), inverse plate (focus) or handle (hover): one composed slab | one `OSAPI_GFX_BLIT1` | include |
| each card's interior, white included, composed per slab | `OSAPI_GFX_BLIT1` | package |

- The pen is the default CWHITE ink on CBLACK paper, and the slab is
  composed with **1 = white**. That means the same on all three adapters:
  BLIT1's pen is unread on 1bpp, where a set bit is lit.
- Cards are content and are never themed (§76). Under Dark the gaps are
  black; under Color they are teal.
- There is no SAVEU: the widget holds no themed pixel in any cache, so a
  theme change needs no cache drop.

**The composer** goes in `apps/os88gfx.inc`, next to `GFXE_BAND` (INDEX
:306), as two new capabilities:
- **`%define GFXE_TEXT`** gives `gfxe_glyph` (an 8x8 from
  `OSAPI_FONT_GLYPHS` at any pixel x, normal or inverse, ×1/×2/×3,
  optionally bold as the glyph ORed with itself shifted 1 px) and
  `gfxe_text`.
- **`%define GFXE_BITS`** gives `gfxe_bits`, which ANDs or ORs a 16-wide bit
  record: tick, bell, ×, grip, dot.

The band is the package's own: **22 x 32 = 704 B of bss**. A card is
composed and blitted one 32-row slab at a time. On a 4.77 MHz 8088, a slab
costs about 1 ms to clear (rep stosw) plus about 70 µs per byte-aligned
glyph, so ~3-4 ms to compose, plus ~4.4 ms to blit on VGA (scaled from
PERFORMANCE.md:8008's 12.6 ms for 128x128) and less on 1bpp.

**Include API:**
- **`DIA_TAG`.** `'Desk Calendar'`, which equals W_TITLE. `DIA_CAP` stays
  the downgraded row's caption.
- **`DIA_LAYOUTS`.** Per layout: the card count, the card heights, the gaps
  and the tag height. Each table carries a `%error` sum check against its
  band.
- **`DIA_PAINTCARD`.** AL = card, BX..DX = the damaged rows in card-local
  coordinates. The package recomposes and blits only the slabs that
  intersect.
- **`DIA_ONCARD` / `DIA_ONCARDUP`.** Card-relative press and release.
- **`DIA_ONFOCUS`.** AL = 1 when the widget gained the bar, 0 when it lost
  it. The calendar uses 0 to close an open entry line, as Esc does.
- **`dia_paint` (W_PAINT).** Calls `OSAPI_WM_DAMAGE`; fills the ground
  pieces that intersect; draws the plate, inverse plate or handle by state;
  calls `DIA_PAINTCARD` for each card that intersects; and ends with
  `dia_painted`.
- **`dia_click` prefilter.**

  | press on | result |
  |---|---|
  | tag row, outside a revealed × | DRAG |
  | gap or left column | DRAG |
  | revealed × | arm the × |
  | a card | `DIA_ONCARD` |

- **`dia_mup`.** The × release goes to `dia_quit`; otherwise
  `DIA_ONCARDUP`.
- **`dia_wake` (W_ONWAKE, free on kern_big).** Level-triggered. It reads
  WF_HOVER and `OSAPI_MENU_OWNER` and reconciles the `dia_hv` state (HIDDEN,
  DWELL, SHOWN, LINGER, plus a FOCUS bit), arming `OSAPI_WM_TIMER` for the
  next deadline.
- **`dia_tick`.** Multiplexes the dwell, linger and focus checks with the
  36-tick `dia_beat`. A beat that comes early is harmless, because
  `dia_beat` is a poll.
- **`dia_load` / `dia_made`.** Unchanged in shape, plus QUERY, MAKE,
  `OSAPI_WM_ONWAKE dia_wake` and `ONCLICK`/`ONMOUSEUP` set to the
  prefilters.
- **Titled fallback.** No tag and no hover. Gaps are white with a centred
  1 px `gfx_hline` (so each gap is still one fill plus one line, never
  ground then rule). SAVEU ON\|1BPP, as v1.

**Positions:**
- The header +4/+6 now hold the **face origin**, flagged by `DIA_HF_CONT` =
  header +3 bit 1 (bit 0 is `DIA_HF_POS`).
  - Desk: face = (W_X+1, W_Y).
  - Titled: face = (W_X+1, W_Y+TITLE_H).
- A v1 file (bit 1 clear) holds a titled frame (x, y), so face =
  (x+1, y+18).
- `DIA_HDR_VER` stays 1, so no file reads as damaged.
- `dia_anchor` measures with the face size. The give-back over the dock
  (os88dia.inc:356-381) uses the shown height.
- D rows carry no geometry (§110.2): **no migration**.

### 2.4 Auto-hide: what appears, when, and what it costs

**What appears.** Only the tag row changes. Nothing overlays a card, so
revealing never touches card pixels:

```
REST   ::::[ Desk Calendar ]::::      white plate, 112 px (13 cells + 4 px
                                      pad each side), ground either side
FOCUS  ::::[#Desk Calendar#]::::      the same plate inverse (the widget owns
                                      the bar), like a selected icon label (§76.8)
HOVER  #[x]#### Desk Calendar ##|||#  a full face-width black bar: a white
                                      10x10 close box with a black x at
                                      face x+3..12, the name in white centred,
                                      a three-line dotted grip at x+158..170
```

**When:**
- **Reveal** after the pointer has been over any visible part of the frame
  (cards, gaps, tag) for `DIA_HOVER_T` = 5 ticks (275 ms, the dock's
  `DOCK_HOVER_T`, dock.inc:85).
- **Hide** `DIA_LEAVE_T` = 14 ticks (770 ms, `DOCK_LEAVE_T`) after the
  pointer leaves. Crossing a gap is not a leave.
- **A press anywhere on the widget reveals at once.**
- **Never revealed:**
  - with a button held (`OSAPI_MOUSE` AL is checked when the dwell expires);
  - under a Standard File chooser (`cur_hov_x` treats `[fdlg_win]` as "no
    window", which is the `dkx_pass` precedent, dockmod.inc:1430-1440);
  - over a covering window;
  - under a fullscreen window;
  - under a saver;
  - during a menu drop or another window's drag, because no passes run then.
    One correct edge arrives afterwards.
- **Focus.** While the widget owns the bar, `dia_tick` runs at 4 ticks and
  checks `OSAPI_MENU_OWNER` (an inline cell). Focus loss is therefore seen
  within 220 ms, and the plate un-inverts. When the widget does not own the
  bar there is no focus poll at all.

**Cost on a 4.77 MHz 8088:**

| state | cost |
|---|---|
| idle | 0 calls, 0 extra timer fires |
| per mouse packet | one 6-byte compare (~4 µs) |
| per window-change under the pointer | one cold far call plus `wm_wake` (~15 µs) and one ONWAKE dispatch |
| reveal | 1 BLIT1 under `OSAPI_WM_CLIP_SET`/`CLEAR`: ~3-8 ms including the one cursor hide/show that CLIP_SET spends because the pointer is inside the frame |
| hide | 2 `OSAPI_GFX_DESK` + 1 BLIT1 |
| while focused | one ~1 ms timer fire every 220 ms (~0.5%), and only then |

**Keyboard alternatives:**
- the keyboard mouse (§9.6) hovers, presses and drags exactly like the mouse;
- while active, the app-name cell offers About and Close;
- Esc gives the bar back.

### 2.5 The look per adapter

The layout is chosen from **OSAPI_VIDEO's height**, not the adapter kind:
- 400 rows or more: TALL (VGA);
- 300 to 399: MID (Hercules 348, and EGA 350, which today wrongly takes
  VGA's PREFER pair at deskcal.asm:228-232);
- otherwise: COMPACT (CGA).

`OS88_PREFER` keeps three derived pairs. The usable band is h − `MBAR_H`
(20) − `DOCK_H` (24).

**TALL, VGA 640x480** (band 436). The face is 176 x 354 at the default
**(376, 42)**, which puts the frame at (375, 42) and its right edge at 551,
clear of the drive icons at ~588. The default is right-anchored at
screen_w − 264. It matches the mockup:

```
 :::::::::::::::::::::::::::::     (: = the desktop's own ground)
 :+------------------------+       card 1, 178 rows
 :| ###  ###  MONDAY       |       rows 8-31: day number x3 (48x24) at x+8;
 :|   # #  #  AUG 2026     |       MONDAY bold at x+64 rows 9-16, AUG 2026 rows 22-29
 :|                        |
 :|<      August 2026     >|       rows 44-51: bare '<' cell 1, '>' cell 20
 :|  M  T  W  T  F  S  S   |       rows 60-67, 24 px fields from x+4
 :|                 1  2   |       weeks from row 78, pitch 16, digits
 :|  3  4  5  6  7  8  9   |       right-aligned in each field
 :|           .            |       2x2 dot at r+10 = has appointments
 :|[24]25 26 27 28 29 30   |       today: inverse 20x16 box (r-4..r+11)
 :| 31                     |       selected != today: 1 px outline, same box
 :+------------------------+
 ::::::::::::::::::::::::::::      gap 6
 :| Today                + |       card 2, 91 rows: heading rows 6-13 (bold)
 :| 09:00  Team sync       |       agenda rows 22, 36: HH:MM x+8, text x+56
 :| 14:30  Dentist         |
 :| ---------------------- |       rule row 50, x+8..167
 :| [v] Read mail          |       11x11 box x+8 rows 57, 73; 2 px tick
 :| [ ] Fix mouse driver   |       text x+32
 ::::::::::::::::::::::::::::      gap 6
 :| (b) 21:00              |       card 3, 57 rows: bell 16x16 x+8 rows 5-20,
 :|     Back up files      |       21:00 bold x+32 rows 8-15, text rows 25-32,
 :|     Snooze . Dismiss   |       links rows 41-48 (Snooze x+32, dot x+88,
 ::::::::::::::::::::::::::::      Dismiss x+104); gap 5
 ::::[ Desk Calendar ]:::::::      tag row 12 (plate 2+8+2)
```

The total is 178+6+91+6+57+5+12 = **355 rows**.

**MID, Hercules 720x348 and EGA 640x350** (band 304/306). The face is
176 x 242; the default is (456, 24) on Hercules and (376, 24) on EGA. The
same three cards and tag are drawn tighter:
- card 1, 122 rows: day x2 (32x16) rows 4-19; MONDAY rows 4-11 and AUG 2026
  rows 13-20 at x+48; nav 26-33; weekdays 38-45; weeks from 50 at a 12-row
  pitch, dot 2x1 at r+9, today box r-2..r+9;
- card 2, 67 rows: heading 4-11; agenda 16 and 27; rule 39; 9x9 boxes at 42
  and 54;
- card 3, 30 rows: bell 16x12; "21:00 Back up files" rows 4-11; links 18-25;
- gaps 4/4/4, tag 11.

**COMPACT, CGA 640x200** (band 156). The face is 176 x 146 at (376, 22):

```
 :| ## ## MONDAY           |       card 1, 85 rows: day x2 rows 2-17;
 :|  #  # < Aug 2026 >     |       nav shares the header (rows 10-17)
 :|  M  T  W  T  F  S  S   |       rows 20-27
 :|  ...6 weeks, pitch 9 ..|       weeks from 30; 1-row dot at r+8; box r-1..r+8
 ::::::::::::::::::::::::::::      gap 3
 :| Today >              + |       ONE 44-row slot: heading 2-9 pages
 :| 09:00  Team sync       |       Today > / Tasks > / Reminder >,
 :| 14:30  Dentist         |       3 lines at pitch 10; a ringing reminder
 ::::::::::::::::::::::::::::      takes the slot (bell 8x8); gap 3
 ::::[ Desk Calendar ]:::::::      tag row 11 (plate 1+8+1)
```

The total is 85+3+44+3+11 = **146 rows**. In the titled fallback the face
loses the last gap and the tag, so the frame is 132 + 19 = 151 rows, and it
still fits 156.

On every adapter the drawing is ink and paper only. Grey is never used, so
nothing rounds to black on 1bpp (§39.4).

**How a widget differs from an app window:**
- no frame, shadow, title, pinstripes or boxes;
- no zoom-open animation;
- no dock tile;
- never raised, always below every app window;
- never the target of Close Window;
- takes the bar only while it is the last thing pressed;
- its handle exists only while hovered, and its name shows as a label.

### 2.6 The Desk Calendar to the mockup

**Rendering.** Every card pixel goes through `GFXE_TEXT`/`GFXE_BITS` into
the 704 B slab and then one BLIT1. Text uses no `font_run`, so nothing is
drawn twice.

**Calls per gesture:**

| gesture | VGA calls | about, on a 5150 |
|---|---|---|
| full paint | 11 card BLIT1s (card 1: 6 slabs, card 2: 3, card 3: 2) + 1 left column + 3 gaps + 2 tag-side fills + 1 plate = **~18** | ~90-110 ms, against v1's 65 calls and 177 ms (PERFORMANCE.md:1353) |
| day select | the 1-2 week slabs touched + card 2's 3 | ≤ 5 calls |
| month | card 1's slabs 2-6 + card 2 | ≤ 8 calls |
| tick toggle | **1** (a sub-rect BLIT1 of the 11x11 box) | |
| reminder fires | card 3's 2 slabs | + the toast and chirp |
| hover show / hide | 1 / 3 | |
| idle | 0 | |

An uncover recomposes only the slabs that intersect `OSAPI_WM_DAMAGE`.

**Controls** (W_ONCLICK and W_ONMOUSEUP, card-relative; the press lights
the target with one `OSAPI_GFX_XOR_FILL`, and a release inside it acts):
- **Card 1.**
  - `<` / `>`: previous / next month, clamped to 1980-01..2099-12.
  - The month label or the day-number block: Go to Today.
  - A day: select it; card 2 follows it.
- **Card 2.**
  - `+`: opens the os88line entry in the heading row. "HH:MM text" makes an
    appointment on the selected day; plain text makes a task. Enter
    commits, Esc cancels, and losing focus cancels too.
  - A line's text: select it. Enter or a second click edits it; Del asks
    the existing `os88ui_ask` Yes/No.
  - A check box: toggle done.
  - When a section has more items than lines, its last line reads
    `+N more`, and a click on it pages that section.
  - Heading: "Today", or "Tue 25 Aug" for another selected day.
- **Card 3.**
  - RINGING: `Snooze` (+`DC_SNOOZE` = 5 min) and `Dismiss`.
  - NEXT: the next armed reminder today, shown with no links.
  - NONE: "No reminders today".
- **CGA.** The slot heading pages the slot, as Tab does.
- **Keys while active.** The v1 table (arrows, PgUp/PgDn, T, N, K, Enter,
  Del, Tab), plus S (snooze), D (dismiss) and Esc (RELEASE).
- **Menus.** The Calendar and Remind menus (deskcal.asm:2011-2066) stay,
  shown only while the widget owns the bar. They carry Edit, Delete, Clear
  Done, the reminder leads, Go to Today and About. "Show Tasks/Day"
  survives on CGA only.

**Data** (no version bump: `DIA_HDR_VER` = `DCP_VER` = 1):
- **Monday-first is display only.** `dc_build`'s offset (dcdate.inc:101)
  becomes `(dow+6)%7`.
- **New appointment flags.** `DCA_SNOOZED` = 4 and `DCA_DUE` = 8 join
  `DCA_FLAGS`, beside `DCA_REMIND` = 1 and `DCA_FIRED` = 2.
- **The snooze slot uses the spare payload words.** +4 holds the snooze
  minute of the day (0FFFFh = none) and +6 its FAT date. One snooze is held
  at a time; snoozing another puts the first back to DUE.
- **Firing.** It sets FIRED|DUE; card 3 rings, and the toast (§59) and the
  chirp stay. **The modal alert goes** (`dc_fire`'s `os88ui_ask`,
  dcrem.inc:191-196).
- **An old file** has bits 2 and 3 clear, so nothing is due and nothing is
  snoozed, and an old FIRED reminder reads as already dismissed.
- **`tools/os88dia.py`** learns the bits and the slot.

**Dropped:**
- `dc_head`'s inverse strip;
- the 4-cell field and the `*` mark;
- the `GFX_FRAME` selection, and its 78 double-written pixels;
- the `!`/`-` agenda mark cell;
- the 232-wide titled window;
- the alert-on-fire;
- os88ui's check glyph (`%define OS88UI_NOGLYPH`).

**Size.** The image goes from 13,168 B to about 14.0-14.5 KB, and bss grows
by ~0.8 KB (the slab and bitmaps). lz4 goes from 11,211 to ~12 KB: at most 1
more cluster on the 360 KB system disk (§110.6 quotes 321-326 of 354). The
include grows by ~+500 B of image per DIA, under `DIA_WIDGET`.

### 2.7 kern_small

kern_small gets the two cells and nothing else (12 B .text, 0 .cold).
- QUERY refuses, so `dia_load` sizes a titled frame.
- The widget is v1's window with v2's face: title bar, shadow, a dock tile,
  raised on click, and the kernel's close box and title-bar drag.
- Gaps are white with a 1 px rule, and there is no tag.
- The worker arm (os88dia.inc:444-459) still beats, and a reminder rings
  card 3 or the slot with a single note.
- BLIT1 needs x on the byte grid on small. Face x = W_X+1 is aligned, and
  the slab composes glyphs at any x itself.
- The same DESKCAL.O88 runs on both kernels, and the face origin is the
  same in both forms.

---

## 3. SPEC.md amendments (W0, before any code)

- **§11.103 (new) "The desk layer (WF_DESK)".**
  - 11.103.1 the z invariant and its four writers;
  - 11.103.2 "front means ordinary" (`wm_top`, and the OSAPI_WM_TOP banner);
  - 11.103.3 show by the damage pass, and why not the cull (outward
    rounding);
  - 11.103.4 activation without a raise: press, keys, RELEASE;
  - 11.103.5 the chromeless sites and the `t_chromesites` ratchet;
  - 11.103.6 the posted drag, and no drag cache;
  - 11.103.7 the hover edge (WF_HOVER, `cur_hovw`, the refusals);
  - 11.103.8 what a DIA must not call (MINIMIZE, FULLSCREEN), and that a
    future window cycler skips WF_DESK.
- **§7.2.1** gets one paragraph: the hover edge in `cur_shape_pass`.
- **§12.6** gets an amendment: keys follow a desk window that owns the bar.
- **§13.7** gets one sentence: a press on a desk window is delivered without
  a raise, and DRAG is legal only inside W_ONCLICK.
- **§13.16.2.1** gets a note: a composed 2 px tick (≥ 2 px strokes) is the
  widget's, and it reads on 1bpp where a 1 px `gfx_line` did not. os88ui's
  solid square stands.
- **§20.2** flags table: bit 6 `OS88_F_DESK`.
- **§20.3**: cells 0x046D `OSAPI_WM_DESK` (verbs) and 0x0473
  `OSAPI_GFX_DESK`.
- **§30**: no tile for IF_DESK, and the top-down slot. **§30.6**: the
  widget's hover reuses the strip's constants by name.
- **§110.1**: strike "a DIA is a normal window".
- **§110.3**: rewrite to cards and controls, per adapter.
- **§110.5**: header +3 bit 1 and the include's new routines.
  **§110.5.2**: the give-back measured with the face.
- **§110.6**: the size and gesture rows.
- **§110.7**: the titled fallback with v2's face.
- **§110.8**: strike the desk-layer and chrome bullets, and add the drag
  repaint, the shortcut-drop and the "only beside its own kernel" notes.
- **§110.10**: the waves and gates.
- **§110.11 (new) "The widget treatment"**: geometry, ownership, the tag,
  hover and focus states, and the include API.
- **PERFORMANCE.md Part 5** rows for the Desk Calendar (:1352-1356):
  placeholders with the budgets in §2.6, filled in W5.
- **docs/KERNEL-MEMORY.md**: the planned bytes.
- `docs/INDEX.md` is regenerated.

**ABI:**
- `python3 tools/abilock.py --update "OSAPI_WM_DESK 0x046D + OSAPI_GFX_DESK 0x0473 appended; header flag bit 6; WF_DESK/WF_HOVER mirrors"`.
- **No `PKG_FMT` bump** (cells added at the end and a reserved bit given a
  meaning, §20.2.0.1), and no `DRV_VER` bump.
- `tools/os88pkg.py:173`'s mask goes from 0xD8 to 0x98.
- DIAs stay off the Wire (§110.8, already true because of 0x0467).

---

## 4. Waves

Each wave builds and is gated on its own.

**W0 — SPEC first.** §3 above. Record the kernsize baseline for big, emu and
small. No code.

**W1 — the kernel desk layer** (no hover, no drag):
- Code: the flags; `wm_top`, `wm_lift`, and the `wm_show_b`/`wm_front_b`
  arms; the chromeless sites and the `wm_draw_win` arm; the ui.inc:457
  press; the `fdlg_top_x` key arm; `OSAPI_WM_DESK` QUERY/MAKE/RELEASE and
  `OSAPI_GFX_DESK`; the header bit with top-down allocation; dock and
  DOCK.DRV.
- `tests/unit/t_chromesites.py` (fast tier).
- `tests/diaprobe` opts in and paints two white cards plus gaps through
  `OSAPI_GFX_DESK`. `deskcal` is untouched and stays a v1 titled window.
- Rows: `deskz`, `deskdock`, `deskkeys`, `desksmall`.
- Gates: kernsize on all three kernels (**stop for the owner on any rung
  crossing**), `t_api_abi`, abilock, `os88sym --all` on both kernels,
  `os88ladder --selfcheck`, buildmatrix, and test-full run alone.

**W2 — kernel interaction:**
- Code: the DRAG verb, `ui_dragq_x` and `cw_ui_drag`; the desk `.nocache`;
  `cur_hov_x`, WF_HOVER and `cur_hovw`.
- The probe draws a crude tag and handle.
- Rows: `deskdrag`, `deskhover`, `deskclose`.
- kernsize again.

**W3 — the treatment:**
- `apps/os88gfx.inc` gets `GFXE_TEXT`/`GFXE_BITS`.
- `apps/os88dia.inc` gets `DIA_WIDGET`: `dia_paint`, the prefilters,
  `dia_wake`/`dia_tick`, `dia_quit`, focus, the face-origin header with v1
  conversion, and the titled fallback.
- `tools/os88dia.py` and `t_dia` follow.
- The probe moves onto the include.
- Rows: `deskground` (Bright, Dark and Color gap parity), `deskfit` (EGA
  takes MID).

**W4 — the Desk Calendar:**
- the header flag;
- the three layout tables and slab painters;
- the Monday-first grid with dots, today box and outline;
- card 2 and card 3;
- the CGA slot pager;
- the controls;
- the snooze and due data;
- the alert-on-fire removed;
- the menus kept.

Every existing `dia*`/`deskcal*` row is updated (§5), and `deskcalcards`,
`deskcalsnooze` and `deskmodal` are added.

**W5 — look, measure, publish:**
- Look at the result on 1bpp: CGA and Hercules on MartyPC, EGA, Dark and
  Color.
- One look on 86Box `xt-multimon` (a seam straddle) and on `xt-hercules`.
- `deskcalperf` on the MartyPC 5150 (VGA, CGA, Hercules): paint, reveal,
  hide, select, toggle, reminder and drop. Fill in PERFORMANCE.md Part 5.
- Run `kernsize --bless`.
- Retake all 12 `docs/dia/` images plus the hover set: `vga-rest`,
  `vga-hover`, `vga-focus` (bar = Desk Calendar, Disk window striped),
  `vga-reminder`, `vga-dark`, `vga-color`, `vga-covered` (an app window over
  half the tag, with the handle clipped), `cga-rest`/`cga-hover`/`cga-page1..3`,
  `herc-rest`/`herc-hover`, `ega-rest`, and `smallk-cga` (the titled
  fallback).
- Each rest/hover pair also gets a zoomed crop of the tag row
  (`shot.py --crop ... --zoom 4`). The rest crop must equal the computed
  desktop ground outside the plate.
- Update `prshots.py`, README.md and readme.txt.
- Run test-full alone, then soak `dia*` and `deskcal*` with
  `--marty-jobs 1`, then the functional-check skill on the glass.
- Update PR #251's body and screenshots.

---

## 5. Tests

Every assertion is one of:
- a symbol read through `os88sym`: `wm_zord`, `wm_zn`, `menu_win`,
  `cur_hovw`, `ui_dragq`, `wm_dc_win`, and each record's W_FLAGS and
  I_FLAGS;
- a package bss read through I_SPTR: `dia_hv`, `dc_sel`, `dc_month`;
- a breakpoint count;
- framebuffer pixels;
- a host read of SYSTEM.CFG or DESKCAL.DAT.

Timing waits on symbols and BIOS ticks (0x46C), never on sleeps, and any
double-click is driven from a single process.

**Fast tier:**
- `t_chromesites` (new): every `WF_FULL` reference is registered FULL-ONLY
  or CHROMELESS with a reason; the CHROMELESS sites assemble as word tests
  of 0x0408; an unregistered site fails `make`.
- `t_dia`: `OS88_F_DESK` set in deskcal's header, `DIA_HF_CONT`,
  `DCA_SNOOZED`/`DCA_DUE`, and payload +4/+6 round-tripped through
  `os88dia.py`.
- `t_api_abi`: the two cells. `t_mirror`: WF_DESK, WF_HOVER and IF_DESK.
- abilock; the os88index check; `os88pkg` bit 6.
- `t_appsmall` and buildmatrix: kern_small byte-identical except the table
  tail, and NODRAGCACHE and BAND=1 still assemble.
- No new image gate is needed, because there is no new module. That is a
  deliberate build-risk saving over kernel-chrome.

**New soak rows:**
- **`deskz`** (VGA XT). A Disk window overlapping the widget; click `>` on
  the visible part of card 1. Assert:
  - desk entries first in `wm_zord`;
  - `dc_month` moved;
  - no breakpoint on `wm_raise` or on `wm_lift`'s store;
  - the Disk window's overlap pixels byte-identical;
  - `wm_top` is still the Disk window;
  - `[menu_win]` is the widget, and the bar reads "Desk Calendar";
  - a click on the Disk window gives `[menu_win]` back, and the plate
    un-inverts within 6 ticks;
  - closing the Disk window gives the bar to Locator;
  - with only the widget up, Close Window is greyed.
- **`deskkeys`.** Notepad front, click the widget, send `t` and the arrows:
  the calendar state moves and Notepad's buffer does not. Esc returns the
  keys to Notepad, and a click on Notepad also returns them. A Standard File
  chooser takes the keys over a focused widget.
- **`deskdock`.** The widget's instance is INST_MAX-1; no tile is drawn
  there (vram); a click at that strip position fronts nothing; the first app
  is drawn at tile 0.
- **`deskhover`** (VGA, plus `--leg cga|herc`):
  - pointer on a gap: WF_HOVER set within one pass, `dia_hv` SHOWN by 5+2
    ticks, the handle crop equals the golden;
  - pointer away: hidden by 14+2 ticks;
  - never revealed when the pointer is over a covering window, with a
    button held, or under a Standard File chooser;
  - calls per edge: 1 to show, 3 to hide;
  - shots of the hidden and revealed states.
- **`deskdrag`.** Press on the tag, then on a gap, then on the left column;
  move by (+43, +33), an odd dy; release. Assert:
  - W_X ≡ 7 mod 8 and W_Y moved by 33;
  - `wm_dc_take` breakpoint did not fire;
  - W_PAINT ran once;
  - every gap pixel equals the independently computed `(x+y)&1` ground at
    its screen coordinate (Bright);
  - DESKCAL.DAT carries `DIA_HF_CONT` and the face origin;
  - a drag out from under a Disk window leaves no ghost (crop compare);
  - a press without movement repaints 0 calls;
  - `ui_dragq` is 0 afterwards;
  - a DRAG posted from a timer does not turn the next press into a drag.
- **`deskclose`.** Press the ×, slide off, release: still open. Press and
  release on the ×: the instance is gone and the D row removed (mtools). A
  press on the × cell while it is not revealed drags. diaswap's no-disk
  decline is unchanged.
- **`deskground`.** Under Bright, Dark and Color, gap pixels equal desktop
  pixels just outside the frame. Cover and uncover under Color restores
  teal.
- **`deskfit`.** `VIDEO=ega` picks MID, and the frame fits the 306-row band.
- **`desksmall`** (extends `diasmall`, MartyPC 128 KB 5150 CGA): QUERY
  refused, a titled frame of 178 x 151, a dock tile, the v2 face, the
  title-bar drag saved as the face origin, and a reminder on the slot with
  no alert.
- **`deskcalcards`.** Per adapter, pixel probes of the card corners, the
  today box, a dot, the tick, the bell and the gaps.
- **`deskcalsnooze`.** With the clock poked: the reminder fires, card 3
  rings, no alert window (`wm_zn` unchanged); Snooze sets `DCA_SNOOZED` and
  the slot, and it re-fires 5 minutes later; Dismiss clears `DCA_DUE`; a v1
  DESKCAL.DAT loads with nothing due.
- **`deskmodal`.** With the Disk window front, Del on a selected item: the
  alert is the top of `wm_zord` and owns the bar, the widget stays at [0],
  and answering it gives the bar to `wm_top`.

**Existing rows that change** (tests/suite.py:8838-9014, and the fast `dia`
row :382):
- **`diaboot`.** Relaunched at `wm_zord[0]` with no tile, at INST_MAX-1.
- **`diaswap`, `diaregister`.** Close through `ui.widget_close` instead of
  `ui.close`/`geom.close_xy` (os88geom.py:813). The face-origin header.
- **`diamove` (and its CGA and anchor legs).**
  - drag through `ui.widget_drag` instead of `ui.drag_window`
    (os88ui.py:671);
  - positions become face origins;
  - the KEEPH arithmetic uses 355/242/146;
  - the anchor fixture is v1-format, which exercises the conversion.
- **`diasmall`.** Becomes the `desksmall` above.
- **`deskcal1bpp`.** The 177x146 and 177x242 frames, the today box, the dot,
  the tick, the 1 px selection outline, the gaps as ground. The greyed-menu
  leg stays, but on the focused bar.
- **`deskcaltask`.** Box geometry from the layout table, the tick pixels, a
  toggle is exactly 1 BLIT1, Clear Done still by menu.
- **`deskcalgrid`.** Monday-first: tests/deskcalgrid.py:59 changes
  `firstweekday` from 6 to 0. Plus 24 px fields.
- **`deskcaledit`.**
  - focus by a press;
  - `+` opens the field in card 2's heading;
  - keys reach the widget while the Disk window is front, which keeps its
    stripes;
  - Esc releases;
  - the alert menu-bar regression stays, for the Delete question.
- **`deskcalremind`.** Card 3's text, no alert, the toast and chirp kept,
  one ringing at a time.
- **`deskcalperf`.** Budgets:

  | gesture | calls (VGA unless stated) |
  |---|---|
  | full paint | ≤ 20 (COMPACT ≤ 14) |
  | nav | ≤ 8 |
  | select | ≤ 5 |
  | tick | 1 |
  | hover | 1 / 3 |
  | reminder | 2 + toast |
  | idle across three beats and a minute boundary | 0 |

  About is still picked from the menu.
- **No change:** `diahiber`, `diamiss` and `diashort` read only titles, and
  W_TITLE is still the tag.
- **`tools/os88ui.py`** gains `widget_hover(win, on)`,
  `widget_drag(win, dx, dy)` (press at the tag row's centre),
  `widget_close(win)` (hover, wait for `dia_hv` SHOWN, press and release the
  × at face x+7, tag row +6) and `widget_activate(win)` (a press on a card
  margin).
- **`tools/prshots.py`**: new constants.

---

## 6. Risks

1. **Resident budget.** About 130 B against 183, with each line ±25%. W1
   measures before anything else lands; the cut list is in §2.1, and the
   owner decides.
2. **kern_emu's cold rung.** About 164 against 227. The verbs are the bulk;
   moving MAKE's z-shift into the `dia_made` package side is impossible (the
   kernel owns `wm_zord`), so if it is short, merge RELEASE into the
   key-target stub's tail.
3. **Bottom-always repaints.** Any window moved off a widget repaints the
   uncovered part through `DIA_PAINTCARD`, with no cache. The slab design
   makes that ≤ a few BLIT1s, and `deskcalperf` measures an app window
   dragged across it.
4. **Showing a widget under open windows** (a hand launch) redraws the
   windows that overlap it whole, through `.mbelow`. This is rare (boot has
   no app windows) and safe.
5. **Two focus cues can disagree.** The front app keeps its stripes while
   the widget types. The inverse plate and the bar name are the cues, as
   after a bare-desktop click.
6. **Desktop shortcuts under a widget** are invisible and unclickable
   (`desk_free`/`desk_reflow` know no windows). The right-anchored default
   avoids the drive column.
7. **A missed chrome site** would give a phantom border or a wrong content
   rect. The `t_chromesites` ratchet, plus the probe's frame-filling face
   with its edge pixels asserted, guard it.
8. **The 1-row CGA tables.** 146 of 156 rows. The `%error` sums guard it.
9. **The tick departs from §13.16.2.1.** It must be looked at on CGA and
   Hercules.
10. **A widget wholly covered when its reminder fires.** The toast and the
    chirp carry it, and card 3 stays ringing until dismissed.

---

## 7. Open decisions

Work proceeds on the defaults.

1. **A rung crossing in W1/W2.** Default: apply the §2.1 cut list first;
   accept +512 B only on your word.
2. **Card 2 capacity on VGA.** Default: **2 agenda + 2 tasks, exactly the
   mockup's 91-row card**, with a `+N more` pager. The alternative is 3+3
   (+29 rows, still fits the 436-row band).
3. **Menus while the widget is active.** Default: **keep Calendar and
   Remind** (0 new code; they carry Delete, Clear Done, the leads, About).
   The alternative drops them (−0.9 KB) and gives those actions in-widget
   controls the mockup does not show.
4. **The real tick in the check box.** Default: **yes**, as in the mockup,
   with a §13.16.2.1 note.
5. **Week start.** Default: **Monday, fixed**. The alternative is a "Week
   Starts Sunday" Calendar item in DCP_FLAGS bit 1.
6. **What drags.** Default: **the tag row, the gaps and the left column**
   (anything that is not a card). The alternative is the tag row only, with
   gaps inert.
7. **Snooze length.** Default: **5 minutes**. The alternatives are 10, or
   the Remind menu's lead list.
8. **Hover timings.** Default: **the dock's 5 / 14 ticks**. The alternative
   is a snappier 3 / 9.
9. **kern_small.** Default: **the titled fallback with v2's face** (12 B).
   Porting the layer is ruled out by its 65 B cold rung and its lack of a
   WM timer.
10. **Focus model.** Default: **any press on a widget activates it** (bar
    and keys), the ui.inc:438 behaviour. The alternative is that the widget
    takes focus only when its entry opens (+6 B), which makes its keys
    nearly unreachable.
11. **Right press on a widget.** Default: **activate only**. The
    alternative is W_ONRC with a Close/About popup (+6 B .text).
12. **MINIMIZE and FULLSCREEN on a desk window.** Default: **a written
    rule**. The alternative is a kernel refusal (+6 B each).
13. **The dock's top slot through header bit 6.** Default: **yes** (~3 B
    .text, ~16 B .cold). The alternative is an empty tile 0 while a widget
    holds record 0.
14. **Shortcut drops under a widget.** Default: **accept, and document**.
    The alternative is a CTRL.DRV refusal of a cell under a WF_DESK rect
    (~30 module bytes).
