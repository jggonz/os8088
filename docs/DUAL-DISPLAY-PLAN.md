# os8088 dual-display plan — Hercules *and* CGA, one extended desktop

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what it would take to drive **both** cards
of `docs/FIELD-MACHINES.md`'s 5150 at once, with one desktop spanning the two
monitors, and of whether that can be a loadable driver. Nothing here is
implemented. Every interface named would land in SPEC.md *before* its code.

The ask, in the requester's words:

- Support **two displays at once**, both Hercules and CGA, in a single system.
- **The desktop would extend to both.**
- Investigate making this a **loadable driver to minimize kernel impact**.
- ABI compatibility is **not** required — it may be rewritten completely.

---

## 0. The verdict, up front

**The hardware half is free, the renderer half is nearly free, and the driver
idea does not pay.**

Three findings, in the order they matter:

1. **The machine can already do it and the kernel already knows.** Both cards
   are simultaneously addressable by construction — `vid_setmode` deliberately
   leaves Hercules 3BFh bit 1 clear *so that* a CGA can live at B8000
   (SPEC.md §39.6) — and `vid_probe_avail` already answers "this machine has
   both" on the field 5150 (SPEC.md §39.11.1). Nothing has to be discovered.
   What is missing is only that the kernel drives *one* of them at a time.

2. **The renderer is already parameterized, so a second display is a table
   swap and not a second driver.** SPEC.md §39.3's whole argument is that
   Hercules and CGA differ by four numbers in `vid_tab`. Two displays differ
   by the same four numbers plus an origin. `vid_switch` (SPEC.md §39.11.2)
   already proves the swap works at run time; it just does it once per user
   click instead of once per primitive.

3. **A loadable driver cannot take the work, because the work is *inside* the
   primitives.** Two independent reasons, either of which is decisive:

   - **The splash draws before a file exists.** `vid_detect`/`vid_apply`/
     `vid_setmode` run on the boot splash's first tick, far-ticked by the boot
     sector per sector read, with the rest of the kernel still coming off the
     floppy (SPEC.md §15.3/§39.1). `drv_boot` runs ~70 lines *later* in
     `kmain`. The kernel must therefore keep a complete renderer whatever a
     driver does.
   - **The dispatch cost lands in the wrong place.** A far API cell is
     **46.7 us** and a drawing call's fixed floor is **756 us**
     (PERFORMANCE.md Part 2), so a driver boundary at the *primitive* is 6.2%
     — affordable. But `gfx_rowbase`/`gfx_nextrow` are the addressing, they
     run **per scan line**, and SPEC.md §39.3 has them *inlined* precisely
     because an 11 us near call/ret doubles them. Behind a far cell a 348-row
     fill pays 348 x 46.7 us = 16 ms it does not pay now, and an 8-row glyph
     cell pays 374 us on top of 901 — **+41% on every character in the
     system**. A machine where a full text page is already 2.50 s cannot buy
     that.

   Measured against the change surface below, a driver removes **~300 bytes of
   ~1,600** and adds a class, a service-table extension and a boot-ordering
   hazard. **~19% of the impact, for a new failure mode.** Recommendation: do
   not.

**Budget.** Measured on this build (`make`, `tools/kernsize.py`):

```
kernsize: rungs      image 55,808 +0 (190 left)   cold 22,016 (173 left)
kernsize: footprint  KERN_SIZE 91,648 of KERN_BUDGET 94,208 -> 2,560 spare (5 steps)
kernsize: segment    .text+.bss 55,618 of KERN_CODE_MAX 65,536 -> 9,918 left
```

`.text` + `.bss` may grow by **190 bytes of rung slack plus five 512-byte
steps = ~2,750 bytes** before `KERN_BUDGET` is exceeded. The estimate below is
**1,400–1,900 bytes**. It fits without a raise, and it spends three or four of
the five remaining steps — which is a conversation to have with whoever wants
the feature, not a build fix (`docs/KERNEL-MEMORY.md`).

**`.cold` does not help.** The binding guard here is `KERN_BUDGET`, and cold
code still counts against it (SPEC.md §2.6); `KERN_CODE_MAX` has 9,918 bytes
free and is not the constraint.

---

## 1. What the tree already has

More than half of this feature is in the tree already, built for a different
reason. `vidsel.inc` was written so the user could *switch* between the two
cards on the 5150; almost every part of it is reusable unchanged.

| already exists | what it does | reuse |
|---|---|---|
| `vid_probe_avail` | fills `[vid_avail]`, a bitmap of the cards this machine has | **as is** — the dual case is `VID_A_HERC \| VID_A_CGA` |
| `vid_memchk` | is there display RAM at this segment (and rejects a text-only MDA for free) | as is |
| `vid_cga_alias` | B800 answering is not a CGA — it can be a Hercules page 1 | as is; this is *the* test that says two cards are two cards |
| `vid_tab` | 18-byte geometry record per adapter | becomes the per-display context's initialiser |
| `vid_apply` | `rep movsw`s a record over the live block, derives the rest | splits: the renderer half becomes the context swap |
| `vid_setmode` | programs either card, blank-first on Hercules | called **twice** at boot instead of once |
| `vid_blank` | darks a card, two `out`s, sync preserved | as is — it is how a display is turned off |
| `vid_equip` | keeps 40:10 pointing at the card `CMD_REBOOT` must return to | as is, now meaning "the primary" |
| `vid_switch` | the whole ordered dance (buffer, cursor, geometry, refit, chrome) | becomes one case of "reconfigure the displays" |
| `vid_avail_test` | refuses a card the machine does not have | as is — a settings disk from a dual machine must not brick a single one |
| Control Panel **Display** page (SPEC.md §31.10) | lists adapters, Activate Mode | gains a mode selector |
| `SYSTEM.CFG` `VM` key (SPEC.md §39.11.3) | remembers the adapter | widens to remember the layout |

**The `x & (x-1)` test in `vid_probe_avail` is the tell that this was
anticipated**: the Display page is suppressed when exactly one bit is set,
which means the two-bit case has been a real, tested state on the field
machine since §39.11 landed.

---

## 2. Can the machine actually do it?

Yes, and this is the least risky part of the whole investigation.

**Addresses do not collide.** Hercules page 0 is B0000–B7FFF; a CGA is
B8000–BBFFF. They overlap only if the Hercules is told to decode its second
32KB page, which is 3BFh bit 1 — and `vid_setmode` already leaves that bit
clear, with a comment saying it does so *because this 5150 has both cards in
it* (SPEC.md §39.6).

**Ports do not collide.** Hercules is 3B0–3BF, CGA is 3D0–3DF.

**Both can be in graphics mode at once.** They are two cards with two CRTCs
driving two monitors; nothing is shared. This is the classic period
configuration — it is how dual-monitor debugging worked on these machines, and
it is why the field 5150 is wired the way it is.

**Nothing costs conventional RAM.** Both framebuffers are card memory, so a
second display is **free in RAM terms**. On a 128KB machine that is the difference between
possible and not.

Three things are *not* free, and each is a real ordering trap of exactly the
kind SPEC.md §39.11.2 already documents:

- **The CGA's mode set goes through the BIOS and the BIOS is
  equipment-driven.** On a mono-primary 5150 the equipment word says 11b, and
  an XT BIOS then forces mode 7 on the MDA whatever mode was asked for —
  `vid_cga_equip` exists for exactly this and its header records the field
  report. So bringing the CGA up as a *secondary* means flipping 40:10 to
  colour, calling `int 10h AX=0006h`, and **flipping it back**, because
  `CMD_REBOOT`'s `vid_text` must land on the card the user has been looking
  at. Today `vid_cga_equip` is a one-way door and `vid_equip` is its only
  counterweight; dual needs the flip scoped to the mode set.
- **Order: the BIOS card first, then the direct one.** Program the CGA through
  `int 10h` while the Hercules is still in its power-on text state, then
  program the 6845 directly. The reverse ought to work — a colour mode set
  touches 3D4/3D8/3D9 only — but "ought to" about an XT ROM is what §39.6 is a
  monument to.
- **`vid_setmode` clears the framebuffer it programmes**, so bringing up the
  secondary mid-boot wipes nothing of the splash (different card) but *does*
  need `spl_reset`'s treatment if the primary is ever re-programmed.

---

## 3. The coordinate model

**A virtual desktop is the union of display rects placed in a virtual plane.**
Per display: an origin `(vx, vy)` in virtual space plus its existing geometry.

Herc primary on the left, CGA to its right:

```
        x=0                     x=720                  x=1359
  y=0   +-----------------------+----------------------+
        |                       |                      |
        |   Hercules 720x348    |    CGA 640x200       |
        |   display 0           |    display 1         |
  y=199 |                       +----------------------+
        |                       |                      |
        |                       |     DEAD ZONE        |
  y=347 +-----------------------+----------------------+
```

**The dead zone costs nothing in the renderer, and that is the single best
property of this design.** The split is "for each display, intersect the
primitive's rect with that display and draw the survivor" — a fragment that
lands in no display is drawn by nobody, with no test for it anywhere. It is
`wm_clip_set`'s arithmetic (SPEC.md §11.3) with a geometry attached to each
rect instead of just a rect.

The dead zone needs explicit handling in exactly two places, both outside the
renderer:

- **the cursor**, which must not be able to walk into it and vanish; and
- **`ui_drag`'s clamp**, which must not let a title bar go somewhere the user
  cannot grab it back from.

**Layout is a user choice, not a constant.** Left/right and right/left at
minimum; top-align vs bottom-align is a second byte. Both belong in the `VM`
key beside the primary's kind.

### 3.1 The chrome stays on the primary

The menu bar and the dock are full-width strips whose geometry is derived from
`[vid_w]` — `[vid_clk_hx]`, `[vid_dock_y0]`, `[vid_desk_zx]`. Spanning them
across 1360 virtual pixels would put the clock and half the dock on the other
monitor, split across two cards, and would touch `menu.inc`, `dock.inc` and
`desk.inc` deeply (SPEC.md §12.9/§30.3 both keep a record of what is on the
glass).

**So the chrome lives on the primary display only, and the secondary is pure
desktop and windows.** This is not a compromise — it is what a second monitor
is *for* — and it keeps three modules almost untouched. The one consequence is
that `wm_fit`'s y-clamp becomes per-display: `dock_y0` on the primary, the
display's full height on the secondary.

---

## 4. Where the split goes

The surface is much smaller than it looks, because the primitives already
funnel. Traced through the source:

**The rect family collapses to four entry points.** `gfx_pixel`, `gfx_hline`
and `gfx_vline` are each a `call gfx_fill` with the arguments rearranged;
`gfx_frame` is four fills; `gfx_xor_rect` decomposes into four `gfx_xor_fill`
strips; and **`gfx_blit4` emits one `gfx_hline` per run** (SPEC.md §5.4), so
the one primitive that takes an image is already made of fills. That leaves
the four sites that already carry a `GFXCLIP` macro:

```
gfx_fill        GFXCLIP gfx_fill_raw
gfx_fill_gray   GFXCLIP gfx_fill_gray_raw
gfx_fill_pat    GFXCLIP gfx_fill_pat_raw
gfx_xor_fill    GFXCLIP gfx_xor_fill_raw
```

A `GFXDISP` macro above `GFXCLIP` at those four sites covers roughly ten public
entries and the whole of blitting.

**The rest of the surface, enumerated:**

| path | what it needs | why it is not covered by the four |
|---|---|---|
| `font_char` / `font_run` | whole-cell display selection | clips per cell via `wm_clip_test`, not per pixel |
| `ico_core` | whole-shape display selection | same, and it reads `[vid_stride]`/`[vid_strm1]` directly |
| `gfx_scroll` | per-display; refuse a straddling blit | deliberately off the clip list (SPEC.md §11.3) — a blit cannot take a sub-rect |
| `gfx_save` / `gfx_restore` | display of the rect | the cursor's save-under, from inside IRQ4 |
| `gfx_line_mono` / `gfx_lstep*` | display of each endpoint | walks pixels, not rects |
| `cur_*` | see §4.1 | its own fused single-pass path (SPEC.md §7.1) |

(`gfx_flush` used to need a line here; SPEC.md §32's back buffer is gone and
so is the routine.)

### 4.1 The cursor does not straddle — it jumps

`cur_move` writes every byte exactly once and replays banked `cur_geom`
answers, because a save and a restore that disagree by a byte smear the arrow
permanently (SPEC.md §7.1.2). A cursor straddling the seam would span two
cards with two strides and two segments, and would need that fused loop
rewritten.

**So it does not straddle.** When the hot spot crosses a display boundary, the
arrow is erased whole from the old display and drawn whole on the new. The
visible cost is that the pointer's 8x12 cell does not bleed across the seam —
which on two physically separate monitors with a bezel between them is not a
defect anybody can see. The fused mono cursor and both save buffers survive
untouched.

### 4.2 The choke point for clipping

`vga_rect_setup` clips to `[vid_h]`/`[vid_hm1]`/`[vid_w]`/`[vid_wm1]` and then
calls `gfx_rowbase` — one routine that re-bases every rect primitive in both
renderers. **It is the single place a display's clip and origin have to be
right**, and everything above it is argument shuffling.

---

## 5. The context swap: copy, do not indirect

The renderer reads its parameters as **absolute** operands — `[cs:vid_rowadd]`,
`[cs:vid_wrapbit]` — and `gfx_nextrow`'s contract is *DI and flags and nothing
else*, because several callers are inner loops with no spare register and one
is inside IRQ4 (SPEC.md §39.3). There is no register to index a per-display
block through. **So the active display's numbers are copied into the live
words, exactly as `vid_apply` already does, and the pointer idea is wrong for
the same reason `vid_apply` is a `rep movsw`.**

**What moves (per display, ~18 words = 36 bytes):**

```
vid_seg  vid_stride  vid_bmask  vid_bshift  vid_rowadd  vid_wrapbit
vid_wrapfix  vid_bankmask  vid_strm1  vid_rseg  vid_rpara  vid_rend
+ the display's own clip extent (see below)
```

**What does NOT move — and this split is the crux of the whole design:**

```
vid_dock_y0  vid_dock_ty0  vid_clk_hx  vid_ymax  vid_popmax
vid_desk_zx  vid_desk_zl  vid_desk_zr
```

Those are **desktop layout**, not display geometry. They belong to the virtual
desktop and to the primary's chrome, and they must not change when a primitive
happens to be drawing on the other monitor.

### 5.1 `vid_w`/`vid_h` stay VIRTUAL, and the renderer gets new clip words

This is the one naming decision that decides how big the change is, and the
obvious answer is the expensive one.

`[vid_w]`/`[vid_h]` are read by **both** the renderer (as a clip) and the UI
(as the screen extent). Measured over the tree, the UI side is large and
diffuse — 40 sites in `wm.inc` alone, plus `ui.inc`, `menu.inc`, `desk.inc`,
`dock.inc`, `ctrl.inc`, `fsx.inc` — and the renderer side is small and
enumerable:

| module | renderer-side reads |
|---|---|
| `vga12.inc` | `vga_rect_setup` — 4 |
| `icons.inc` | ~9 (`vid_h`, `vid_hm1`, `vid_stride`, `vid_strm1`) |
| `mouse.inc` | ~5 in `cur_draw`/`cur_geom` |
| `softgfx.inc` | 2 in `gfx_scroll` |
| `font.inc` | 2 (`vid_wm8`/`vid_hm8`, the whole-cell clip) |
| `wm.inc` | 2 in `wm_su_edge` |

**~24 sites against ~60+.** So `[vid_w]`/`[vid_h]` keep meaning *the desktop*
— which on a single-display machine is unchanged, so every UI site is
untouched and cannot regress — and the renderer gets `[vid_cw]`/`[vid_ch]` and
their derived forms in the swap block. Changing the small side is the whole
saving, and picking the other way is how this feature turns into a 60-site
rename whose failures are invisible on the machine you have.

### 5.2 The cost of a swap, and why it is nearly free

`rep movsw` of 18 words is 36 bytes at PERFORMANCE.md's **1.76 us/byte** for
RAM = **~63 us**, plus the decision. Against a **756 us** primitive floor that
is **8% — paid only when the display actually changes.**

And it changes rarely, because **drawing is spatially coherent**: a window's
`W_PAINT` is a burst of tens to hundreds of calls in one place, a `font_run`
row is one call, a desktop dither is one call per display. The steady state is
"the rect is entirely on the active display", which costs **two compares**
against 756 us — under half a percent.

**On a single-display machine the whole mechanism is one compare on
`[vid_ndisp]` and a taken branch**, which is how `[vid_mono]` and `[wm_clip_n]`
already pay for themselves. That property is not negotiable: every machine
that is not the 5150 must measure exactly as it does today.

---

## 6. What changes outside the renderer

| area | change | note |
|---|---|---|
| `mouse.inc` | clamp to the **union**, not a bounding box | 8 compares for 2 displays, on the ISR path — must stay cheap |
| `wm_fit` | clamp into the display the origin is in; `dock_y0` on the primary only | one subtraction site today (SPEC.md §39.7), now per display |
| `ui_drag` / `ui_grow` | keep the title bar inside the union | the dead-zone rule |
| `wm_paint_all` | dither each display's desktop band | the marginal cost of the feature — see §7 |
| `wm_disp_of` | which display is a window primarily on — centre, then origin, then the primary | new; §6.5.1. Used by BOTH fullscreens, so the two cannot drift |
| `fsx_run` / `wm_fullscreen` | take one display; fsx blanks the others and §11.2 does not | §6.5 — the whole of it follows from whether the kernel is running |
| `vid_unblank` | the inverse of `vid_blank`, two `out`s | new; §6.5.2. NOT `vid_setmode`, which would clear a framebuffer nobody asked to lose |
| `wm_refit` | re-fit every window when the layout changes | already exists, called by `vid_switch` |
| `desk_rowcalc` | zones per column on the **primary** | already resident for `vid_switch`'s sake (SPEC.md §39.11.2) |
| `menu.inc` / `dock.inc` | nothing, given §3.1 | both already keep a record of the glass and diff against it |
| `ctrl.inc` | Display page gains Single / Extend and a layout choice | the largest single new lump — ~250 B |
| `drv_cfg_*` | `VM` widens: primary kind, mode, layout | SPEC.md §39.11.3 |
| `OSAPI_VIDEO` | still answers the **desktop**, so no package changes | a new cell after 0x0378 can enumerate displays for anything that cares |

**Packages need no rebuild.** `OSAPI_VIDEO` answers width/height/dock-row, and
on an extended desktop those are the virtual desktop's — which is exactly what
a package laying out a window wants. `wm_fit` still clamps a 640x480 template
onto whichever display it lands on. This is the one place the "ABI may be
rewritten" licence turns out not to be needed.

---

## 6.5 Fullscreen: both kinds, and they answer differently

Two fullscreens exist and they are not variants of each other. **The rule that
separates them on one monitor is the rule that separates them on two.**

- **SPEC.md §53's exclusive bracket (`fsx`) borrows the machine.** `fsx_run`
  does not return until the app's exclusive main does; the scheduler passes
  only that task, its kept worker and `TF_SERVICE` tasks; the gfx lock is held
  throughout. **The kernel does not run.**
- **SPEC.md §11.2's fullscreen surface is a big window.** It is in the
  z-order, it is pre-empted, it takes the lock once a frame and gives it back.
  **The kernel does run.**

Everything below follows from that one difference.

### 6.5.1 Both pick ONE display, by the same test

`wm_disp_of(window)` answers which display a window is *primarily* on, and
both paths use it — one rule, not two that can drift.

**The test is the window's CENTRE, not the largest overlap.** Overlap area is
the intuitive answer and it needs a 32-bit multiply: 720 × 348 = 250,560
overflows a word, so "which display does more of this window touch" costs a
`mul` pair per display on a machine whose whole design is about not paying for
arithmetic. The centre is one point-in-rect test per display and lands on the
same answer in every case anybody will construct. The ladder, because the
virtual desktop is not a rectangle (§3):

```
the display containing the window's CENTRE
  ...else the display containing its ORIGIN     (centre fell in the dead zone)
  ...else the PRIMARY                           (both did)
```

### 6.5.2 Inside an fsx bracket the machine is single-display again

That is the whole design, and it is what makes this nearly free: `fsx_run`
makes the chosen display the live context with its origin at (0,0), and every
§53 contract is then exactly what it is today. `fsx_mode`, the FSI block,
`fsx_wait`, `FSXF_KEEPWORKER`, `FSXF_FASTTICK` — none of them changes, and **no
fsx app needs a line**.

`fsx_caps` answers for **the chosen display**, not for `[vid_kind]`. On a
Herc+CGA machine that is `0x011` or `0x00F` depending which one the window was
on, so an app asking for Mode X is refused on both — which is what happens
today.

**The other display blanks**, which is what the requester asked for and is
SPEC.md §39.11.4's argument unchanged: nothing can maintain it (the kernel is
not running), so what it would show is a frozen desktop, and stopping the video
signal costs two `out`s with the card's sync preserved.

**Two orderings are binding, and both are §39.11.2's species:**

- **Unblank AFTER the desktop is repainted, not before.** The blanked card's
  framebuffer still holds the pre-bracket desktop, so unblanking first shows a
  stale frame — minutes stale, on a monitor the user has not been looking at —
  before `wm_paint_all` covers it. Blanking gates the video signal and not
  memory, so the repaint may happen behind it. Restore order is therefore:
  geometry back → `wm_paint_all` → unblank.
- **Unblanking is not `vid_setmode`.** `vid_blank` writes 3B8h = 0 *and*
  3BFh = 0 on a Hercules, so its inverse is 3BFh = 1 then 3B8h = 0Ah; a CGA
  wants 3D8h back. That is a `vid_unblank` — two `out`s, no 32KB clear and no
  6845 reprogram. Re-running `vid_setmode` on a card whose mode never changed
  would buy a framebuffer clear nobody needs and a sync transient somebody
  might see.

**The trap: a SAME-MODE bracket now changes the geometry.** SPEC.md §53.7 says
"exclusive but same mode… costs nothing to allow", and Paint (§42.7) is that
consumer — it sets no mode at all. On two displays it still moves to a
single-display context, so its content origin goes from virtual (720, 0) to
(0, 0) even though nothing about the *mode* moved. So: **entering a bracket may
change the geometry even when it does not change the mode**, and the bracket's
display must be published — through the FSI block, and by having
`OSAPI_VIDEO` answer the bracket's display while one is live, so an app that
reads the screen size the ordinary way keeps working.

### 6.5.3 §11.2 fullscreen takes one display and blanks nothing

Same display choice, opposite answer on the other monitor: **the kernel is
running, so the other display is a live desktop with its own windows on it.**
Blanking it would be throwing away half the machine to show a window on the
other half.

Mechanically `wm_fullscreen` sets `W_X`/`W_Y`/`W_W`/`W_H` to the chosen
display's rect **in virtual coordinates** and sets `WF_FULL`. Nothing else
changes: `wm_fit`, `wm_paint_all` and §11.91's damage machinery already work on
virtual rects.

**Why one display rather than the whole virtual desktop**, which is the obvious
alternative and is wrong four ways:

1. **The dead zone (§3).** 1360x348 has a 640x148 hole in it. A window covering
   the bounding box lays content into a region with no pixels, and every app
   that centres anything would centre it into the gap.
2. **Every app lays out from `W_W`/`W_H`.** A 1360-wide content box split by
   two bezels and a resolution change is not a better Minesweeper.
3. **It is the CHEAPEST case for the renderer, not the dearest.** A window
   aligned exactly to a display boundary means every primitive it issues lands
   wholly on one display, so §4's split test always answers "one display,
   already active" — the two-compare fast path, zero swaps. A window spanning
   the seam is the most expensive thing the renderer can be asked to draw.
4. **Consistency with fsx**, which matters because Missile Command reaches the
   bracket *from* a §11.2 surface: the window already covers one display, so
   the centre test lands on the same display, and the two never disagree.

**And one thing falls out for free.** §3.1 puts the chrome on the primary, so a
window fullscreened on the *secondary* leaves the menu bar and dock reachable —
better than the single-display behaviour, where fullscreen covers them. An app
that draws its own bar (ArtfulType, §46) then has its bar on one monitor and
the system's on the other, which is correct: the system's bar still belongs to
the machine.

**Considered and declined: a span-both opt-in.** Nothing asks for it, the dead
zone makes it a poor default, and a `WF_FULL` variant that means two different
things is a flag every paint proc has to test. If a real consumer appears it is
a second flag, not a change to this one.

## 7. What it costs the machine

The marginal cost of the second display is **its own desktop background**, not
a doubling. Windows are not duplicated — they are spread — so `wm_paint_all`
draws the same windows either way.

Priced with PERFORMANCE.md Part 2's measured constants (177 us per `gfx_fill`
scan line on Hercules / 182 on CGA, 0.28 / 0.33 us per pixel):

| | rows | scan lines | pixels | total |
|---|---|---|---|---|
| Hercules desktop band | ~324 | 57.3 ms | 65.3 ms | **~123 ms** |
| CGA desktop band | ~176 | 32.0 ms | 37.2 ms | **~69 ms** |

So a full-screen repaint grows by **~69 ms, about +56%** on the background —
against a baseline where PERFORMANCE.md already treats a full repaint as the
defect to avoid. That is affordable *because* SPEC.md §11.90/§11.91 mean the
system almost never does one: coming to the front costs one window, going away
costs a rectangle, and both are per-display for free (a damage rect on the
primary never touches the secondary).

**The thing to watch is not the steady state but the events that force a full
pass** — a layout change, `[cp_dirty]`, a resolution switch. Those are already
`wm_paint_all` and simply get bigger.

---

## 8. The driver question, answered

The ask was "a loadable driver to minimize kernel impact". Taking it
seriously:

**What a video driver could plausibly own:** the second card's detection, its
mode set, its blank/unblank, its geometry row, and its Control Panel page.

**What it cannot own:** the split at the primitives, the context swap, the
cursor's cross-display handling, the mouse clamp, `wm_fit`, and the virtual
desktop itself. All of that is in the middle of routines that run per scan
line and per glyph cell.

**The arithmetic:**

| | est. |
|---|---|
| Kernel work that cannot move (split, swap, cursor, mouse, wm, desk) | ~1,200 B |
| Second-card bring-up: mode set ordering, blank, geometry | ~120 B |
| Control Panel page | ~250 B |
| `SYSTEM.CFG` key | ~40 B |
| **Total in the kernel** | **~1,600 B** |
| Movable to a driver | ~410 B |
| Kernel cost of a `DRVC_VIDEO` class (publication slot exists; new `DSV_` fields at 2 B x `DRVC_MAX`, dispatch, row) | **~+100 B** |
| **Net saving** | **~310 B, or 19%** |

Against that saving, a driver buys three new problems:

- **The desktop cannot extend until `drv_boot` runs**, which is after
  `mem_init`, `font_init` and every splash notch. So the first
  `wm_paint_all` is single-display and everything must re-fit afterwards — a
  second `wm_refit` + full repaint on every boot of a dual machine.
- **A driver that fails to attach must leave nothing behind** (SPEC.md §51.2's
  all-or-nothing rule) — and what this one would leave behind is a *card in
  graphics mode* that the kernel is now addressing.
- **The service table is shared across classes.** `DSV_SIZE` is 24 bytes and
  every new field costs `2 x DRVC_MAX` bytes of `.bss` for all classes,
  whether or not they use it.

**Recommendation: build it resident.** The driver framing is right for
hardware the kernel can live without — a sound card, a hard disk, a serial
monitor — and this is not that: it is the display, and the kernel cannot boot
without one.

### 8.1 The variant where a driver *does* pay

There is one, and it is worth naming because it is cheap and genuinely useful
— it is just **not an extended desktop**:

**The secondary as a driver-owned surface.** The driver owns the second card
outright: detection, mode, and a small published surface. The window manager
never composites there, the cursor never goes there, nothing drags between.
Content is pushed to it — a Task Manager readout, Tracker's pattern grid, a
`DEBUG.DRV`-style console, a fullscreen picture.

- Kernel impact: a class plus two or three cells — **~300 B**, against ~1,600.
- The renderer needs no split at all: the driver calls the existing
  parameterized renderer with its own context, once, under the gfx lock.
- It answers a real need on the field machine (watch the debugger on the mono
  monitor while the OS runs on the colour one) without touching a single
  primitive.

If the goal is "use the second monitor for something", this is the 20% that
buys 80%. If the goal is "one desktop across two monitors" — which is what was
asked — it is not a substitute, and the resident build is the answer.

---

## 9. Testing: the instrument does not exist yet

**This is the biggest practical risk in the whole plan, and it is not in the
kernel.**

> **STATUS: BUILT AND PASSING.** Everything below the table was written before
> the instrument existed; it is kept because the three questions it names are
> the right ones, and §9.1 records what they turned out to be. `make marty` now
> carries it and `python3 tests/dualcheck.py` is the gate.

| tool | can it do dual? |
|---|---|
| **QEMU** | **No.** VGA-class devices only; no CGA, no Hercules, no second card. Out of scope entirely. |
| **MartyPC** | **Yes — verified.** Two `[[machine.video]]` entries produce two cards; both rasterise; both keep their own frame clock. It needed two patches and a machine, all in the tree now (§9.1). |
| **86Box** | Yes, interactively. `vm/xt-cga` and `vm/xt-hercules` exist; a dual profile is a config edit. No automation socket, so no scripted verification. |
| **The 5150** | Yes, and it is the final word — it carries both cards permanently. But it is a field run: `make field`, seven steps, somebody's afternoon. |

**Step 0 of any implementation is a MartyPC patch**, and it is small: give
`video`/`vram`/`fbuf`/`shot`/`flicker` an optional card index and route it
through `for_each_videocard`. What must be verified before writing a line of
kernel code:

1. Does MartyPC's config loader actually instantiate **two** `[[machine.video]]`
   entries, or does it take the first and drop the rest?
2. Do both cards rasterise, or only the primary?
3. Does its MDA/Hercules still decode the whole 64KB whatever 3BFh says?
   `vid_cga_alias`'s header says it does — which on a dual machine means
   **MartyPC will alias the CGA's framebuffer into the Hercules'**, and every
   dual test would be measuring one card twice. This is the single most likely
   way to get a convincing false pass.

Without that patch there is **no way to verify a dual-display change except by
posting a floppy to the field machine**, and PERFORMANCE.md's whole argument is
that a redraw defect is something you watch rather than count.

### 9.1 What the three questions turned out to be

**1. Two entries produce two cards — yes, unconditionally.** `BusInterface`'s
builder is `for (i, card) in machine_config.video.iter().enumerate()`, keyed by
`VideoCardId { idx, vtype }`, and `videocard_ids` keeps them in config order.
`primary_videocard()` is `videocard_ids[0]`, which is why every capture went to
the first entry.

**2. Both rasterise, independently.** Measured: 30 CGA frames against **25**
Hercules frames over the same interval — 60Hz and 50Hz, the two real field
rates. A card that has not been *programmed* sits at 0 frames for ever, which
is not the same as a card that is not there, and cost some time before the
distinction was clear.

**3. The aliasing was real, and it is now fixed at the root.** Upstream maps a
Hercules-subtype MDA at B0000 **and** B8000 unconditionally — the mapping is
built in the constructor, before any guest has written 3BFh — while a CGA maps
B8000. `Bus::register_map` resolves the overlap by **last writer wins**: it
stamps `mmio_map_fast` and never reads the `priority` field the descriptor
carries. So one card silently vanished into the other, and *which* one depended
only on the order of the `[[machine.video]]` blocks.

That is worse than it sounds, because **the obvious test passes on the broken
machine**. Write to B0000, write to B8000, read both back: they differ, because
they are 32KB apart inside one card's 64KB. Measured passing on an aliased
configuration. The test that discriminates is the one that asks the *rasters*:
a write to one card's memory must change **that card's** rendered output and
not the other's.

Rather than work around it with config ordering — which cannot be made to work,
since the order that keeps the CGA reachable is the order the 1982 IBM ROM
refuses to POST in — `tools/martypc/patches/02-hercules-page1-decode.patch`
narrows the Hercules to page 0. That is what a real card with 3BFh bit 1 clear
decodes, and `vid_setmode` leaves that bit clear *precisely so a CGA can answer
at B8000* (SPEC.md §39.6). Order stops deciding anything.

### 9.2 What was built

| piece | what it is |
|---|---|
| `patches/02-hercules-page1-decode.patch` | the Hercules decodes page 0 only, as the hardware does |
| `cards` command | every card, in config order: `idx`, `type`, `primary`, `mode`, `field_w/h`, `frames` |
| `card=` on `video`/`screen`/`fbuf`/`flicker`/`pace`/`advance` | absent = the primary, so nothing that worked changes; by index or by type name, and an ambiguous type is **refused** rather than guessed |
| every capture reports `card` | a capture that fell back to the primary says so in its own output |
| `park` command | point the CPU at an address with the queue flushed |
| `os8088_5150_both_gla` | the GLaBIOS twin of the two-card 5150, so this is testable without the IBM ROM |
| `os8088_5150_herc_gla` | single-card Hercules control |
| `card=` on `settle`/`launch`/`_sample`/`_bar_up` | which card the BOOT GATE watches — os8088 need not be driving MartyPC's primary (§11.3) |
| `tests/dualcheck.py` | the gate on the EMULATOR: two cards that are genuinely two |
| `tests/dispcheck.py` | the gate on the KERNEL: did it bring the second one up? (§11.3) |
| `tests/dispband.py` | can a window use the second display's top rows? (SPEC.md §39.16.2) |
| `tests/dispblit.py` | does a straddled canvas survive its own repaint? (SPEC.md §39.14.7) |
| `tests/dispsave.py` | is the raise cache taken, and right, on the second display? (SPEC.md §39.14.8) |
| `tests/dispdrag.py` | does a window DRAGGED across the seam arrive on both displays? (SPEC.md §11.96.14.1) — `--layout right\|below`, because the two seams are separate arms |
| `tests/dispvy.py` | can a STRADDLING window reach the second monitor's first row? (SPEC.md §39.19.3) |
| `tests/dispnp.py` | does a WIDE straddling window letter its whole row? (SPEC.md §27.2.2) |

**`park` is there because `setreg ip` cannot be made to work.** `Register16::PC`
is settable, and setting it is not enough: `pc` is the *fetch* pointer and the
core derives `ip() = pc - queue.len()`, so a bare write leaves whatever the 8088
had already prefetched from the old address in front of the new one, and those
bytes execute first — parking at 0x0500 landed at 0xD4CC. The flush is not
reachable through `CpuDispatch`, so `park` goes through the CPU's reset vector
instead, and clears every register as a documented consequence.

**The gate parks the CPU and boots no operating system**, which is the point: a
booted os8088 programs both cards and clears both framebuffers, which is exactly
the state a two-card test is trying to control. It is verified to **fail**, not
just to pass — reverting patch 02 and rebuilding gives exit 1 and names the
check.

### 9.3 …and it immediately found a kernel bug

The instrument's first result was not about the instrument. On a machine that
provably has two live cards, **os8088 running on the Hercules reports only the
Hercules**:

```
vid_kind  = 1 (HERC)      vid_avail = 0x02 -> ['HERC']          # wrong
vid_kind  = 2 (CGA)       vid_avail = 0x06 -> ['HERC', 'CGA']   # right
```

`vid_cga_alias` (SPEC.md §39.11.1) is meant to tell a real CGA at B8000 from a
Hercules' page 1 wearing the same address. It writes a byte at `B000:8000` and
looks for it at `B800:0000` — **and those are the same linear address**,
`0xB8000`, whichever segment reaches it. The comparison is between an address
and itself, so it answers "same" on every machine where anything at all decodes
B8000, and `vid_probe_avail` then drops the CGA. It is reached only when the
Hercules is the *primary*, which is why nothing had caught it: the direction
that was verifiable on the old emulator was CGA→Hercules, and this is the other
one.

**FIXED** (SPEC.md §39.11.1). The routine now writes a *different* value
through each segment at offset `0x7F00` and reads the first back — one memory
answering at both apertures is one card wearing two addresses, which is
`vid_memchk`'s own idiom moved one aperture apart instead of one offset apart.
0x7F00 is inside page 0 and past the last displayed byte, so the probe never
flickers the screen it is running on.

Measured across the four cases that matter: two cards CGA-primary `0x06`, two
cards **Hercules-primary `0x06`** (was `0x02`), a Hercules alone `0x02`, a CGA
alone `0x04`. And the backstop still backstops — reverting patch 02 recreates
an aliasing card, and a lone Hercules on it still reports `0x02` rather than
inventing a CGA. Cost: **8 bytes of `.text`**, no rung crossed.

**The `REDRAWFULL=1` precedent applies.** SPEC.md §12.9/§30.3 verified their
incremental drawing by **byte identity** against a reference kernel, not by
looking. The same discipline is the only honest gate here: a dual kernel
driving one display must be **pixel-identical** to today's kernel on all three
adapters. That A/B is buildable from day one and catches the entire class of
"the split broke the single-display path".

---

## 10. What is unverified

Stated plainly, because several of these can only be settled on iron:

1. **Whether an XT BIOS's `int 10h AX=0006h` leaves the Hercules alone** once
   40:10 is flipped to colour. It should touch 3D4/3D8/3D9 only. SPEC.md §39.6
   exists because a ROM did something nobody predicted.
2. ~~Whether MartyPC can hold two cards at all~~ — **answered: yes** (§9.1),
   and the gate that says so is `tests/dualcheck.py`.
3. **The 63 us swap figure** is `rep movsw` priced from PERFORMANCE.md's RAM
   throughput, not measured. `gfxbench` would settle it in one run.
4. **The ~1,600 byte estimate** is a sum of per-site guesses, in a tree where
   the image rung currently has **190 bytes of slack**. The first thing that
   crosses a rung will make the number real.
5. **Whether the dead zone is tolerable in use.** It is 640x148 of nothing on
   the right-hand monitor and no amount of design removes it — the two cards
   are different shapes. Nobody has looked at it.
6. **The equipment-word flip-back** is reasoned from `vid_cga_equip`'s and
   `vid_equip`'s headers, not tested. Getting it wrong is a machine that
   reboots to a dead monitor — §39.11.2's exact recorded failure.

---

## 11. Recommendation and staging

**Build it resident, not as a driver, and stage it so every step is testable
on its own.**

| # | step | gate |
|---|---|---|
| 0 | ~~MartyPC dual-card patch; answer §9's three questions~~ **DONE** (§9.1/§9.2) | `tests/dualcheck.py` passes, and fails without patch 02 |
| 0b | ~~Fix `vid_cga_alias` (§9.3)~~ **DONE** — SPEC.md §39.11.1 | `vid_avail = 0x06` both ways round, `0x02` for a Hercules alone, and still `0x02` for an aliasing one |
| 1 | Per-display context block + swap; `[vid_ndisp]` = 1 everywhere | **byte-identical** to today on VGA, Hercules and CGA |
| 2 | ~~`vid_cw`/`vid_ch` split at the ~24 renderer sites~~ **DONE** — SPEC.md §39.2.1 | 22 sites; **0 differing pixels** on CGA, Hercules and VGA, twice |
| 3 | ~~Bring the second card up at boot; no desktop on it yet~~ **DONE** — SPEC.md §39.13 | both cards scanning our raster, primary framebuffer **byte-identical**, both directions |
| 4 | ~~`GFXDISP` at the four rect entries + font/icon/scroll/line~~ **DONE** — SPEC.md §39.14 | secondary shows a desktop dither; **0 differing pixels** on all three single-card adapters. `gfx_save`/`gfx_restore` deferred to step 5, §11.4 |
| 5 | ~~Cursor crossing; mouse clamp to the union~~ **DONE** — SPEC.md §39.15 | the pointer crosses, and a round trip leaves **0 differing pixels** on both cards |
| 6 | ~~`wm_fit`, `ui_drag`, `wm_paint_all` per display~~ **DONE** — SPEC.md §39.16 | a window drags across the seam, both primaries; **0 differing pixels** on all three single-card adapters |
| 6b | ~~`wm_disp_of`, then §11.2 fullscreen on one display~~ **DONE** — SPEC.md §39.17 | fullscreen on the secondary leaves the menu bar **0 differing pixels** on both primaries; the control differs by 779/304 |
| 6c | ~~`fsx` on one display, others blanked; `vid_unblank`~~ **DONE** — SPEC.md §39.18 | the other card goes to **0 frames/s** inside the bracket and comes back; `tests/fsxdisp.py` |
| 7 | ~~Control Panel mode + layout; `VM` key~~ **DONE** — SPEC.md §39.19/§31.10.2 | Single is the **default**; Right/Below place display 1; survives a reboot; `tests/dispmode.py` |
| 8 | Field run on the 5150 | the three things QEMU cannot show |

### 11.9 Step 7, as built

`[vid_dmode]` / `[vid_dlay]`, the Display page's desktop row and a `VM` key
three bytes wide (SPEC.md §39.19, §31.10.2, §39.11.3). **Cost: `.text` +180,
`.bss` +4, `.cold` +211 for `kern_big` — 395 bytes, but one image rung *and*
one cold rung, so 2,048 spare becomes 1,024 — and `kern_small` +30 with no
rung crossed, so +0 to the machine.**

**The design decision worth arguing with is the default, and it is Single.**
Everything §11.3–§11.8 built now ships switched off. The reasoning is in
§39.19.1 and it is short: the kernel can detect a second *card* and nothing it
can do detects a second *monitor*. A Hercules card with nothing plugged into
it is an ordinary thing to find in a 5150, and extending onto it puts half the
desktop — and every pointer position in that half — on glass that does not
exist, with the pointer being how the user would reach the control that turns
it off. That is §39.11.3's own rule about a settings disk carried between
machines (*a machine must not boot to a dead monitor*), applied where the
kernel has no fact to refuse on.

**And the layout is two placements, not four**, because the primary is always
at the virtual origin: which monitor is on the left is answered by which card
is *primary*, the choice sitting directly above it on the same page. All four
physical arrangements are reachable; what is given up is the menu bar being on
the right-hand monitor, and buying that back would put the primary's virtual
origin at every chrome site in the machine (§39.19.2).

**`vid_disp_init` stopped being a boot-time one-shot and became the arbiter.**
It is the only routine that writes a `vid_ctx` origin or `[vid_ndisp]`, so the
Control Panel changing its mind, `vid_switch` moving the primary and
`drv_boot` applying `SYSTEM.CFG` are one call. Single is not "do nothing"
there: the other card is *darked*, the desktop shrinks back and the pointer
comes home. `vid_disp_relayout` wraps it with `desk_rowcalc` + `wm_refit` —
`vid_switch` already did those two as its steps 4 and 5; what needed a routine
was the other caller, because turning Extend off strands every window that was
on the display that went.

**The page's line is not drawn on a machine that cannot extend**, which is
§31.10.1's own answer one level down — that page stopped being drawn on a
single-adapter machine rather than carrying a caption apologising for itself.
The alternative here was worse than an apology: a permanently greyed control
whose §47 rule 6 say-why-not would have to displace the Activate button's
caption, on the one page where that button is the main control.

| | measured, CGA primary |
|---|---|
| at boot | `ndisp 1 dmode 0`, desktop 640x200 = chrome, second card **stopped scanning** |
| `Right` | `ndisp 2 dlay 0`, display 1 at **(640,0)**, desktop **1360x348**, chrome still 640x200 |
| `Below` | `dlay 1`, display 1 at **(0,200)**, desktop **720x548**, chrome still 640x200 |
| `Single` | back to `ndisp 1`, desktop = chrome, second card dark, the panel re-fitted to (160,35) |
| swap the primary while extended | `kind 1`, d0 the Hercules at (0,0) 720x348, d1 the CGA at **(720,0)**, chrome **720x348** — §39.19.2's other two arrangements |
| reboot | comes up on the saved arrangement without anybody clicking |

**The harness trap this one sprang is the sharpest yet, and it is about
persistence tests generally.** The reboot half failed first, reporting the
defaults — and the kernel was right: MartyPC mounts a floppy by reading it
into RAM, and `os88marty.launch` copies the image into its run directory
before that, so the guest's `SYSTEM.CFG` write reached neither the build tree
nor anything a *second* launch would mount. Two launches of the same path are
two pristine floppies. The reboot is `m.reset()` — the same machine, the same
in-RAM media, the POST run again — which is what a reboot actually is.
`tools/os88flush.py` is the other route and is the one to reach for when the
host has to *read* what the guest wrote.

**`dispcheck` and `fsxdisp` had to grow a Control Panel round trip**, since
what they test is no longer the default. Driving the real control rather than
poking `[vid_dmode]` is deliberate: it is the only route a user has, so those
two now exercise the default → Extend path on every run, at ~30 s against a
ten-minute test. `tests/dispcp.py` is that drive, shared by all three, and its
one rule is that **every click re-reads the window's rect** — a click that
takes posts `[cp_dirty]` and runs `wm_refit`, so a click aimed at where the
panel used to be lands on the desktop, switches the menu bar to Locator, and
looks exactly like a control that does not work.

**And sysbench grew a display section** (SPEC.md §57.4's `VD` block): the
adapter, the availability bitmap, the arrangement, one group per display with
its origin, size, stride, banks and framebuffer segment, the desktop against
the chrome extent, and the dead zone. It is unconditional, for the mouse and
clock blocks' reason — two cards on two monitors is the field machine's own
arrangement, and whether a monitor is plugged into the second one is the one
question in §39 no emulator can be asked.

### 11.8 Step 6c, as built

`vid_fsx_enter` / `vid_fsx_leave` / `vid_fsx_unblank` (SPEC.md §39.18).
**Cost: `.text` +320 for `kern_big`, no rung crossed, and `kern_small` +0.**

**§6.5.2's design went in as written and no fsx app needed a line.** The
bracket makes its display the *only* display at the virtual origin, and every
§53 contract is then exactly what it is today. The one thing §6.5.2 did not
say is what carries it: **`[vid_kind]` moves to that display's adapter**, and
`vid_apply` then republishes the live block, the desktop words and §39.16's
chrome extent out of `vid_tab` in a single call. That is why the context
record grew an adapter-kind byte — outside the eighteen-word run, because the
run is what the *renderer* reads.

**The two orderings §6.5.2 called binding both are**, and the second one
turned out to matter more than it reads: `vid_unblank_kind` restores the CGA
from **the BIOS's shadow of 3D8h at 40:65h**, because that register is
write-only on the card and the ROM's record is the only honest source for
what its own mode set left there. The Hercules' pair is the two writes
`vid_setmode` itself makes.

**The gate's instrument took two goes, and the second is the interesting
one.** A blanked Hercules does not render black — it *stops rendering*, and
MartyPC's `fbuf` hands back the last frame it rasterised, so through it a dark
Hercules reads as a perfectly ordinary desktop. Measured by hand and then
built into the test: **163 frames/s → 0 → 160** across a 3B8h video bit
cleared and set again, where counting lit pixels would have passed a kernel
that never wrote the port at all.

**And then the Hercules-primary run failed, on a test that was now wrong in
the other direction.** With the CGA as the secondary the gate reported 196
frames/s inside the bracket and called it a live card. It was not: writing
3D8h from the host on that same machine takes the CGA to **0 lit and holds
~187 frames/s**, because that adapter gates its video output and keeps its
CRTC running, which is the card and not the model. So *dark* is **stopped
scanning or scanning nothing** — SPEC.md §39.18.1 now says so — and the CGA
half owes one thing more, because "0 lit" could also be a picture that had
been erased: its **VRAM is read out of the guest** while the card is dark and
must still hold the desktop, which is what makes this a test of the *signal*
gate §39.18.1 specifies. Measured, Hercules primary: 0 lit on the card,
**64,000 lit (50.0%) still in memory**.

The lesson is the one this tree keeps relearning: *an instrument calibrated on
one adapter is a claim about that adapter*. The kernel was right both times.

`tests/fsxdisp.py` is `dispcheck`'s twin and separate because it needs a
different apps floppy — §53.9's `fsxtest` is the only package in the tree with
a fullscreen item reachable in two double-clicks, and it ships on a scratch
image of its own. Its `x` key is the **same-mode** bracket, which is what a
Hercules or a CGA can take and is also §39.18's own trap: entering changes the
*geometry* even though it changes no mode.

| | CGA primary (secondary = Hercules) | Hercules primary (secondary = CGA) |
|---|---|---|
| secondary before the bracket | 161 frames/s, 48.6% lit | 193 frames/s, 50.0% lit |
| **inside it** | **0 frames/s** | **0 lit**, 187 frames/s, VRAM still 50.0% |
| after | 158 frames/s, the same 48.6% | 190 frames/s, the same 50.0% |
| the primary, before vs after | 185 px of 128,000 | 188 px of 252,000 |

— the last row being §53.9's restore repaint erasing what the bracket drew —
plus byte identity on the three single-card adapters and every `dispcheck`
assertion still passing on both.

**Two traps the change sprang in the harness rather than the kernel.**
`VID_CTX_SZ` grew from 40 to 42 for the kind byte, and `dispcheck` had the 40
written into it twice; it failed loudly — a display origin of 16,769 — which
is the good kind of failure, but the size is a named constant with a comment
there now. And `fsxdisp` watched MartyPC's card 0 for the boot gate, which on
a `VIDEO=herc` build is the *secondary* — so it timed out saying the machine
never booted, about a machine that had. What the secondary carries is the
extended desktop's dither with no menu bar in it, which is exactly what the
gate is looking for the absence of. It takes `--primary` now, the way
`dispcheck` already did.

### 11.7 Step 6b, as built

`wm_disp_of` and what asks it (SPEC.md §39.17). **Cost: `.text` +259 for
`kern_big` — one image rung, and it carries §39.14.4's owed clip translation
too — and `kern_small` +24.**

**§6.5.1's centre-then-origin-then-primary ladder went in unchanged**, and the
32-bit-multiply argument against overlap area held: 720 × 348 is 250,560 and
does not fit a word, so "which display does more of this window touch" is a
`mul` pair per display on a machine whose whole design is about not paying for
arithmetic.

**What the step turned out to be about is `wm_fs_vis`, not `wm_fullscreen`.**
Sizing the window is four stores; what decides whether a *second monitor is
usable at all* while an app is fullscreen is the predicate four painters and
three input sites ask. It answers "is a visible fullscreen window covering
**the chrome**" now — the chrome being the primary's — so a fullscreen window
anywhere else answers no and the bar, the dock and the drive column are drawn
and clickable exactly as with no fullscreen window at all. `wm_paint_all`'s
"start the walk AT the fullscreen window" follows for free: it is gated on the
same answer and is only sound when the cover is total.

**`ui.inc`'s three sites were testing the LATCH** (`[wm_fs] != 0`) where they
meant the predicate. That is the correction that would otherwise have made a
menu bar sitting plainly on screen unclickable.

**Two things went with it that §6.5 did not list.** The damage a fullscreen
window leaves is its **own rect**, read out of the record — a constant screen
rect would repaint the primary because a window on the *other* display went
away. And `wm_zoom`'s "standard state" is the display the window is on, or
zooming a window on the second monitor would move it to the first: `wm_zoom`
introduced that defect at step 6 and this is where it is paid for.

**And §39.14.4's owed item is closed.** `wm_clip_tab` is virtual; a
whole-shape hook that has translated and then calls a rect primitive arrives
in display coordinates. Both meeting points — `wm_clip_test` and
`gfx_clip_run` — translate the **one** rect rather than the sixteen region
entries, gated on `[gfx_dnest]`. `wm_clip_test` has to `pushf` around undoing
it, because `font_char` takes its `CX`/`DX` back from `AX`/`BX` after the call
and `sub` sets the flags the routine answers in.

Measured on `os8088_5150_both_gla`, both primaries, in the same run as every
earlier step's assertions:

| | CGA primary | Hercules primary |
|---|---|---|
| fullscreen latched on the **second** display | menu bar differs by **0 px** | **0 px** |
| the control — same latch, window on the primary | **779 px** | **304 px** |

plus byte identity on the three single-card adapters.

**What that gate does not cover, stated plainly:** `wm_fs_setrect`'s own
arithmetic. No shipped *kernel* window has a fullscreen item, so driving it
for real means loading a package — three double-clicks and two mounts — and
the poke drives the painters instead. `wm_disp_of` itself *is* exercised,
because the flip between 0 and 779 is that routine answering.

### 11.6 Step 6, as built

`[vid_w]`/`[vid_h]` are the union (SPEC.md §39.16). **Cost: `.text` +202 for
`kern_big`, no rung crossed — 48 bytes left in it — and `kern_small` +39.**

**§5.1's naming decision is what made this ~15 sites instead of ~60.** Keeping
`[vid_w]`/`[vid_h]` meaning *the desktop* means every UI site that reads them
is asking the question it is still being answered; what had to move is the
smaller set that reads them and means **the chrome**. Those go to
`[vid_pw]`/`[vid_ph]`/`[vid_pwm1]`/`[vid_phm1]`, the primary's extent —
`menu.inc` ×4, `dock.inc` ×3, `wm.inc` ×8 (the strips, the desktop band, and
§11.2's fullscreen surface).

**And every *derived* chrome word came free.** `[vid_dock_y0]`,
`[vid_clk_hx]`, `[vid_ymax]`, `[vid_popmax]`, `[vid_desk_z*]` are computed by
`vid_apply` from whatever display it is applying — and `vid_disp_init` applies
the primary **last**. So `vid_desk_union` grows four words afterwards and
touches nothing else; there is no special case anywhere and no second opinion
about where the dock is.

**`wm_fit` fits a frame into a DISPLAY.** Against a union its old bounds were
the wrong shape twice — they would allow a window in the dead zone, and they
would apply the primary's dock row to a display that has no dock. The box is
four registers now (`x0`, `xe`, `y0`, `ye`) rather than three loads, filled
from the display the window's **origin** is on. Origin and not centre
deliberately: `wm_disp_of`'s centre test decides which monitor an app *owns*
(§6.5.1), and this decides which edges a frame may not pass, which is a
question about the corner the clamps are written from.

**`ui_drag` needed one thing `wm_fit` could not give it.** Its clamps are
per-axis and run before the record is written, and two per-axis clamps against
a union can agree on a point no display covers — a title bar dropped there is
one nobody can grab back. `ui_drag_dead` is the correction, and it reuses step
5's two routines exactly: `vid_disp_find`, then `vid_pt_clamp` into
`[cur_disp]` — **the pointer's** display, because during a drag the title bar
is under the hand, and §39.15.4 already guarantees the pointer itself is
inside a display.

**A tall window dragged onto a short display is left hanging off, not
shrunk**, and that falls out of the model rather than being decided: a window
is a rect on the virtual plane, and §39.14's split draws the part each display
covers. Nothing in the window manager is aware of it.

Measured on `os8088_5150_both_gla`, driven end to end — the chip menu opened,
the About box's title bar pressed, the pointer walked onto the second display,
released:

| | CGA primary | Hercules primary |
|---|---|---|
| desktop / chrome | 1360x348 / 640x200 | 1360x348 / 720x348 |
| About opened at | (170, 55) | (170, 140) |
| dragged to | **(850, 52)** — display 1 starts at 640 | **(890, 52)** — display 1 starts at 720 |
| its 300px frame on the secondary | longest lit run **298 px** | **298 px** |

plus byte identity on the three single-card adapters, and every step-3/4/5
assertion still passing in the same run.

**Still owed, and now the only thing in §39.14.4's caveat left standing:**
`wm_clip_tab` is virtual and is not translated. `gfx_clip_run` sees virtual
coordinates on the path that matters (GFXDISP is *below* GFXCLIP), so the leak
is narrow — a whole-shape hook that has already translated calling a rect
primitive *while a clip region is armed*, which is the background-painter case
on a window that has been dragged to the second display. `vid_switch` no
longer owes anything: it re-runs `vid_disp_init`, which was step 3's debt and
became load-bearing here.

### 11.5 Step 5, as built

The pointer crosses (SPEC.md §39.15). **Cost: `.text` +286 for `kern_big`, one
image rung crossed, and `kern_small` +6** — the first non-zero small delta in
this work, and it is the clamp refactor being *shared* rather than an `%ifdef`
being missed: two inline per-axis clamps became one `mou_clamp`, which both
variants call. No rung crossed there; 440 bytes still free.

**§5.1's decision paid off again here, in a place §4 did not list.** Keeping
`[cur_drawn_x]`/`[cur_drawn_y]` in the same coordinate space as
`[mouse_x]`/`[mouse_y]` — virtual — meant the translation landed in exactly
**two routines**, `cur_rect` and `cur_geom`, which are the only two that turn
a position into an address. Everything else in `mouse.inc` compares two
virtual positions or works in *differences* (`cur_move_mono`'s `d = newcol -
oldcol`, its `mv_oy - cur_drawn_y`), and an origin cancels out of a
difference. §7.1's fused 1bpp cursor and §7.1.2's write-every-byte-once pass
did not change by a line.

**The clamp is where the design had to change shape**, and §6's one-line entry
("clamp to the union, not a bounding box") understates it: the union is not a
rectangle, so there is no per-axis clamp that is right. The rule that works is
*"inside some display, take it; otherwise clamp into the display the arrow is
on"*, and it produces every seam behaviour without naming any of them. It also
forced the one structural change to `mou_isr`: the clamp is 2-D, so x has to
be staged rather than stored before y is decoded.

**The gate found a real bug, which is the whole reason it was written to push
the pointer at walls rather than just move it.** `vid_disp_of`'s loop passed
the display index to `vid_ctx_ptr` in `AL` — and `AL` is the low byte of the
x being tested. `mov al, cl` zeroed it, so a point at 1300 was tested as 1280.
That lands on the right display *nearly always*, which is why step 4's gates
all passed with it in place; what it broke was a pointer stepping across the
seam, which stopped dead at display 0's corner. The loop walks the array with
a pointer now and needs no register at all.

Measured on `os8088_5150_both_gla`, both primaries:

| | CGA primary | Hercules primary |
|---|---|---|
| crossing | (160,100) → (1000,174), `cur_disp` 0 → 1 | (180,174) → (1040,100), 0 → 1 |
| **round trip** | **0 differing pixels**, both cards | **0**, both cards |
| pushed at the dead zone | left along a row only display 1 has → stops at **640** | right along a row only display 0 has → stops at **719** |
| pushed at the outer corner | (1359, 347) | (1359, 199) |

and byte identity on the three single-card adapters, as before.

The dead-zone row is mirrored on purpose: the gap is the rows the **taller**
display has and the shorter one does not, and the Hercules is the taller one
either way — so which display you stand on and which way you push depends on
the machine, and a test that hard-codes one direction passes on one primary
and cannot even reach the wall on the other.

**One thing step 5 makes available and does not take.** §39.14.3 had every
rect hook restore the display it entered from *because* `cur_unlazy` runs
above them and erases the arrow through whatever display is live. The cursor
brackets itself now, so that reason is gone and the cheaper arrangement —
leave the last-drawn display current, restore once at `gfx_unlock` — is safe.
It is deliberately not taken in the same change that makes it safe.

### 11.4 Step 4, as built

`GFXDISP` under `GFXCLIP` at the four rect entries, `GFXDENTER`/`GFXDLEAVE` at
the whole-shape ones, and `wm_paint_all` dithering every display (SPEC.md
§39.14). **Cost: `.text` +633 for `kern_big`, one image rung crossed, and
`kern_small` +0.**

**§4's "a `GFXDISP` macro ABOVE `GFXCLIP`" was wrong and it goes below.**
`wm_clip_tab` holds *virtual* rects, so the clip has to be applied before
anything is translated; above it, `gfx_clip_run` would compare display-local
coordinates against virtual fragments — right on display 0, where the two are
the same numbers, and silently wrong everywhere else. That is a plan written
before the clip's coordinate space had been thought about, not a change of
mind.

**Three things this discovered that §4 and §5 did not anticipate.**

The whole-shape sites needed **two** spellings of the hook, because
`font_char` and `font_run_x` carry the character in `AL` and their coordinates
in `CX`/`DX` — hence `GFXDENTERCD`. And the sites that hold a *rect* rather
than a point need the far corner moved as well, which is `GFXDORG`.

The hooks **compose only because of a nesting count**. `font_run`'s fallback
path is `gfx_fill` + `font_str`, and `gfx_line`'s axis-aligned cases are
`gfx_hline`/`gfx_vline`: a translated caller reaching a hooked callee would
translate twice. A flag would not do — `GFXDLEAVE` has to know which call owes
the restore.

And each hook **restores the display it entered from**, which is deliberately
not the cheap arrangement §5.2 described. Leaving the last-drawn display
current would pay one swap per burst instead of one per off-display primitive
— but `cur_unlazy` runs at the top of `GFXCLIP`, above all of this, and erases
the arrow through whatever display is live. Step 5 makes the cursor
display-aware and the optimisation is available then.

**`gfx_save`/`gfx_restore` are deferred to step 5 and that is a scope call,
not an oversight.** `gfx_save` tail-*jumps* into `sw_save`, so bracketing it
means restructuring a path the cursor is on; and its only caller today is the
menu save-under, which hangs off the menu bar and so is on the primary by
§3.1. The cursor's save-under calls `vga_save_vram` directly, below any hook.
Both questions land in the same place at step 5.

**The gate, and what it can and cannot say.** Byte identity first: a settled
desktop on `os8088_5150_cga_gla`, `os8088_5150_herc_gla` and
**`os8088_xt_vga`** is **0 differing pixels** against the build immediately
before — better coverage than step 2 got, since mode 12h works on MartyPC
after all. Then `tests/dispcheck.py` grew two assertions, on both primaries:

- the secondary is **48.6% lit with a longest horizontal run of 1 pixel**,
  which is what a 50% dither *is* and what nothing else is; and
- moving `[vid_desk_zx]` — the drive column's x, one word — into the second
  display's half of the virtual desktop and posting `[cp_dirty]` takes that
  longest run to **53 pixels**, with the lit fraction barely moving. Volume
  icons and their labels reached the second card, so `icon_draw16` and
  `font_char` are exercised and not merely written.

What that does **not** cover is `gfx_scroll` and `gfx_line`, which have no
off-display-0 caller until a window can live there. They are byte-identical
and unexercised, and step 6 is where they get a caller.

### 11.3 Step 3, as built

`vid_disp_init` in `vidsel.inc`, called from `kmain` after `vid_probe_avail`,
`KERN_BIG` only (SPEC.md §39.13). **Cost: `.text` +83, no rung crossed** — 298
bytes left in the image rung, against 381 before — and `kern_small` measures
**+0**, which is what the split is for.

It cost 83 bytes because it *reuses* the boot path rather than adding one. The
secondary is brought up by making it momentarily the live geometry, so
`vid_apply` derives its eighteen words and `vid_setmode` programmes the right
card — both by construction, neither told which — and the whole routine is a
kind swap, two `vid_apply`s, one `vid_setmode`, two captures and an origin.

**The gate is stronger than "both monitors lit", because black is not
evidence.** `vid_setmode` clears the framebuffer, so a correctly programmed
secondary and a card nobody touched both render an empty screen.
`tests/dispcheck.py` is that gate — `dualcheck`'s twin, one about the emulator
and one about the kernel:

```
python3 tests/dispcheck.py --sha 1aff5c6d56aaf779
make VIDEO=herc && python3 tests/dispcheck.py --primary herc --sha af83a91224fd5555
```

Three measurements, on `os8088_5150_both_gla`, in **both directions**:

| | CGA primary (the machine's own answer) | Hercules primary (`make VIDEO=herc`) |
|---|---|---|
| primary framebuffer, pre-step-3 vs post | `1aff5c6d…` → `1aff5c6d…`, 76,798 lit | `af83a912…` → `af83a912…`, 136,617 lit |
| the secondary went | MDA `frames` **0 → 442** — never scanned, now scanning | CGA `Mode3TextCo80` → **`Mode6HiResGraphics`** |
| host writes to the secondary's aperture | 4 banks → 4 consecutive rows at x +0/+80/+160/+240 | 2 banks → 2 consecutive rows at x +0/+80 |

The last row is the one that matters and it is why the test writes from the
*host*. A card in mode 6 and a card in mode 3 both have memory at B8000; what
tells them apart is what the raster does with a byte, and 0xFF at bank *b*
appearing as an 8-pixel run on row *b* is SPEC.md §39.3's banked layout and
nothing else. In text mode the same byte is a character cell. The check is
written to be independent of *where* row 0 lands, for the reason below.

**Two things about the instrument had to be fixed first, and both were silent.**
`os88marty.settle`/`launch` asked `video` with no card, so on a two-card
machine they always watched MartyPC's **primary** — and a `VIDEO=herc` kernel
draws on the card the config lists second. The boot gate then timed out on a
machine that had booted perfectly, with the message "never finished booting".
`card=` now threads through `_sample`, `_bar_up`, `settle` and `launch`; the
wrapper's `advance` also gained the `card=` the server already accepted.

And **MartyPC's MDA aperture in Hercules graphics is offset**: guest (x, y)
renders at `fbuf` (x−16, y+2). Measured on `os8088_5150_herc_gla` by scanning
`fbuf` against `vram` over the same desktop — 2,280 of 2,280 sampled pixels
agree at exactly that offset and nowhere else. docs/MARTYPC-DEBUG.md already
warned that `fbuf` is "16 columns short"; the vertical half was not written
down, and step 4's pixel gate is where it would have cost a day.

### 11.2 Step 2, as built

`vid_cw`/`vid_ch` and their four derived limits, split from the desktop's
`vid_w`/`vid_h` (SPEC.md §39.2.1). **22 renderer sites moved; the ~60 window
system ones did not.** Cost: **`.text` +44** — the extra derivation in
`vid_apply` — no rung crossed, and `kern_small` pays the same 44 because this
is a *shared* refactor rather than an `%ifdef`. That is deliberate: the
alternative is twenty-two `%ifdef`s inside the renderer's hot paths, which is
§2's own "hook left outside its `%ifdef`" hazard with more places to get it
wrong. 44 bytes and no rung is the cheaper mistake.

`vid_tab`'s columns moved with it — width and height are last now — so the
nine columns land on nine words a display owns and SPEC.md §39.12's run stays
contiguous.

**What the gate covers, and what it does not.** A settled desktop compared
byte-for-byte on CGA, Hercules and VGA is **0 differing pixels, twice**, and
that exercises `vga_rect_setup`, `font_char`/`font_run`, `ico_core` and the
cursor across a full `wm_paint_all`. It does **not** exercise
`vga_rect_setup`'s two *clamps*, because nothing at rest is asked to draw past
a screen edge — and a driven comparison could not be made to cover them
either: the cursor is erased and redrawn asynchronously by the mouse ISR and
`gfx_unlock`'s deferred hide, so ~31 bytes at any pointer position differ from
run to run whatever the kernel does. **A gate that cannot tell its own noise
from a finding is worse than none**, and the first negative control here was
exactly that — it reported 43 differing bytes for an injected transposition
that turned out to be noise, at coordinates a symbol-lookup bug had put at
432x182 on a 640x200 screen. (That bug is worth its own line: the script
booted two different kernels and resolved symbols against `build/kernel.bin`,
so one of the two runs was reading the wrong addresses.)

So the clamps are covered by argument instead, and the argument is narrow
enough to state. A transposition in a *compare* — `cmp cx, [vid_ch]` where
`vid_cw` belongs — is caught instantly by the pixel gate, because 640 and 200
are nothing like each other and every fill on the screen would be cut. A
transposition in a *clamp value* fires only past an edge. There are four such
pairs — `vga_rect_setup`'s two, `gfx_ls_box`'s and `gfx_line`'s — and all four
were read back and confirmed axis-consistent, along with the whole-cell tests
in `font.inc`, `ico_core`'s vertical clip, `gfx_scroll`'s bounds,
`wm_su_edge`'s guard and the splash's centring.

Steps 1 and 2 are the ones worth doing even if the feature is abandoned: they
are pure refactoring toward SPEC.md §39.3's own argument, they are provable by
byte identity, and they leave the tree strictly better factored than they found
it.

~~**The decision to take with the requester before step 3** is the budget~~ —
**taken**: `kern_big`'s `KERN_BUDGET` was raised 2KB to 96,256 (the fifteenth
move, and the first that is big's alone), leaving 2,560 spare after step 1.
`kern_small` stays at 94,208 with 1,024, which is the direction the two should
drift from here.

### 11.1 Step 1, as built

`vid_ctx` in `vidsel.inc`, `KERN_BIG` only (SPEC.md §39.12). **Cost: `.text`
+138, `.bss` +80, one image rung.**

Two things made it cheaper than this plan assumed. **The eighteen words a
display owns were almost contiguous already** — `vid_tab` was the only thing
between the live block and the derived renderer words, and it is a static
table, so moving it above them makes the whole run one `rep movsw` instead of
two. And **the boundary lands exactly where §5 wanted it**: the run ends at
`vid_rend`, and `vid_dock_y0` — the first word the *desktop* owns — is the very
next one, so the split between per-display and per-desktop geometry needed no
rearranging at all, only a name.

**The gate was the screen, not the binary.** `kern_small` is the pre-step-1
kernel by construction (everything here is behind `%ifdef KERN_BIG`), so
booting both and comparing framebuffers is the natural check — but the
`vid_tab` move is *shared*, so it changes `kern_small`'s layout too. Measured
both ways: **82,022 bytes before and after** (identical size, identical rung,
177 bytes of pure relocation), and **0 differing pixels on CGA, Hercules and
VGA** against a build of the tree immediately before. The context was then read
back out of the guest to prove it had actually *run* rather than merely not
broken anything — all eighteen words matching the live block, origin (0, 0),
`[vid_ndisp]` = 1, `[vid_cur]` = 0.

**Honest caveat: `vid_ctx_act`'s LOAD half is unexercised.** Nothing on a
drawing path calls it, which is exactly what makes step 1's gate "the screen
did not change" — so the copy *out* of a record is verified and the copy *back*
is not. It lands the first time step 3 activates a second display. A step whose
gate is "nothing changed" cannot also prove that something works, and claiming
otherwise would be the more expensive mistake.
