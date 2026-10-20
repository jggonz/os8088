# GFX-EMBEDDABLE-PLAN.md — the graphics library a package embeds

**Status: BUILT, all eight waves. SPEC.md §5.12 is the contract and this file is
now the design record behind it.** `apps/os88gfx.inc` ships and **the whole
`gfx_line` family is out of both kernels** (§5.12.7): `kern_big` `.text`
50,688 → **49,016** with `.bss` −47, `kern_small` −659 and −36, and the image
rung uncrosses **four steps of 512 — 2,048 bytes of every machine's RAM**. What
is left in this file that is not built is §8.8, and that one says in its own
title that it is not this plan's.

**§8.2 to §8.11 are what each wave actually found, and they are the part worth
reading**: five of the eight landed somewhere other than where this document
pointed, and every headline figure below the wave table is now guest cycles off
a breakpoint bracket rather than arithmetic (PERFORMANCE.md Set 141). Where a
figure is still an estimate — §4.1's size table for the capabilities nothing
built, and §8.8's — §9 says so, and §9 item 6 says why that table is wrong on
the high side.

**Two of the eight waves were won by not writing the code.** Wave 2's checkmark
went to a shape `apps/skies` had already written for a different reason (§8.6.2)
and wave 8's last two callers were deleted rather than ported (§8.11.1) — so the
plan's own best moves were both *finding* something, and §9.1 is the rule that
came out of missing one of them for six waves.

`gfx_embeddable` is an app-side graphics library in the shape of
`apps/os88ui.inc` — a `%include` a package opts into capability by capability.
**Line is its first customer and is not meant to be its only one**, so the
layering in §4 is the part to get right; §8's waves are only the order the line
family goes in.

---

## 0. The premise, and the two things that make it more than a size argument

The owner's framing, which this document is an answer to:

> duplicate code only used by apps that take up most of the system resources,
> and thus are unlikely to run many at a time, instead of permanently spending
> kernel RAM on them.

Two facts turn that from a size trade into a straight win:

1. **An app-side rasteriser is measured FASTER.** PERFORMANCE.md priced a
   candidate 1bpp mask rasteriser at **24.6 µs a pixel** against `gfx_line`'s
   **31.6** with the arrival removed — **1.29×** — because what it drops is
   *"everything `gfx_line` does that a caller compositing its own figure does
   not need: clipping, the ink, the dither table, the per-row `gfx_rowbase`."*
   The library is not a worse rasteriser. It is a better one.

2. **`kern_small` has never had `gfx_line_fast`, and a library gives it back.**
   SPEC.md 5.6.4.1's interval-folded walk is **4.9×** on a 1bpp adapter and is
   `%ifdef KERN_BIG` — *"NOT ON kern_small (5.6.4.4): it is 686 bytes of `.text`
   and that machine is short of the thing it would be spent from"*. Measured
   here at **647 bytes**. In a library that is 647 bytes of **Paint's own
   package**, and the floor machine gets a 4.9× stroke it has never had.

So the question the owner posed — *the apps would have to choose between
keeping those optimisations or gating them out* — has a third answer for at
least one app: **take an optimisation the kernel never gave it.**

---

## 1. The prize, measured

Every figure from `[map all]` on this tree, per-symbol sizes reconciled against
the section lengths (§10).

| | `.text` | `.bss` | total |
|---|---:|---:|---:|
| `kern_small` | **1,377** | 69 | **1,446** |
| `kern_big` | **2,520** | 80 | **2,600** |

3.6% of `kern_small`'s `.text` and 4.9% of `kern_big`'s. The contiguous block
`gfx_linit` → `gfx_blit4` is 1,368 / 2,511; the remaining 9 bytes are
`gfx_lmtab` (8) and `gfx_lmsk` (1), which sit elsewhere in `.text`.

### 1.1 Where the difference between the builds is — the owner's prediction, confirmed

**`kern_big` carries 1,143 bytes of `.text` that `kern_small` does not**, and
every byte of it is a speed optimisation:

| symbol | small | big | what it is |
|---|---:|---:|---|
| `gfx_line_fast` | 0 | **647** | SPEC.md 5.6.4.1, the 4.9× interval walk — **1bpp**, and absent from the 1bpp-only kernel |
| `gfx_line_runs` | 0 | 157 | 4bpp/VGA major-axis run coalescing |
| `gfx_lf_wide3` | 0 | 70 | 5.6.6's three-column dilated walk |
| `gfx_line_flush` | 0 | 66 | `gfx_line_runs`' rect emitter |
| `gfx_lstep_slow` | 0 | 30 | the walk's planar per-pixel arm |
| `gfx_ls_one` | 52 | 126 | +74: display translation round the walk |
| `gfx_line` | 316 | 365 | +49: the planar dispatch |
| `gfx_line_raw` | 2 | 26 | +24 |
| `gfx_ls_box` | 173 | 183 | +10 |
| `gfx_ls_lx` / `gfx_ls_ly` | 1 / 1 | 9 / 9 | +16 |

The rest — `gfx_line_mono` 286, `gfx_lstep_mono` 212, `gfx_ls_box` 173,
`gfx_ls_adv` 71, `gfx_linit` 64, `gfx_lstepv` 59, `gfx_lm_pre` 55,
`gfx_lstep` 37, `gfx_ls_addr` 29, `gfx_ls_ink` 10, `gfx_lmtab` 8 — is
identical on both.

### 1.2 The finding nobody was looking for: the kernel carries TWO 1bpp walkers

`gfx_line_mono` (**286**) and `gfx_lstep_mono` (**212**) are the same
capability written twice — *"draw the next pixel of a Bresenham on a 1bpp
adapter"* — because the line and the resumable walk were built at different
times and share only `gfx_ls_addr` (29). **498 bytes for one capability.**

That is exactly the shape §4's rule fixes, and it is the reason the library's
total should come out *below* the 1,377 it replaces rather than equal to it.
It is also why the owner's rule is the right one and not merely tidy: a lattice
where `WALK` is built **on** `LINE` cannot express this duplication.

---

## 2. What it was blocked on, and why it no longer is

### 2.1 A windowed package has no framebuffer, and never has had

The only published framebuffer address on the machine is `FSI_SEG` in the fsx
info block, and SPEC.md 53.7 makes every kernel drawing slot illegal the moment
`fsx_mode` returns — the two are **mutually exclusive by design**.
`apps/tank/tkraster.inc` is what that permission looks like when it is granted,
and Tank pays for it by owning the whole screen. Nothing in this plan asks for
that to change.

So an app-side rasteriser must hand its result back through a slot, and the
plan lives or dies on which slot.

### 2.2 `gfx_spans` is not the one

`stc`/`ret` with no body on `kern_small`, and on `kern_big` it refuses outright
whenever a clip region is armed or a second display exists — *"both re-cut the
run, and the caller's `gfx_fill` loop does it right"*. **Every windowed package
arms a clip region before it draws.** Rule it out and stop re-deriving it.

### 2.3 `gfx_blit1` IS the one, and it is already legal

Read `gfx_blit1_x` (`kernel/vga12.inc:5972`) rather than assuming from
`gfx_spans`. It does all three things a windowed commit must:

- **`cur_unlazy`** — *"this may write any pixel on the screen, so the deferred
  hide is owed (SPEC.md 7.1.4)"*, taken above everything it could stale;
- **the display**, resolved *"BEFORE anything meets a screen extent (39.14.8)"*,
  with `.percol` for a band that straddles;
- **the clip region** — `wm_clip_rows` for the row range, `.percol` when
  `wm_clip_rows` refuses a part-width row.

Its only argument refusal is an **x off the byte grid**, *"a caller error, not a
shape to clip"*; a width off the grid has been honoured since 5.4.2.5.

### 2.4 …and `kern_small` is getting it anyway

`OSAPI_GFX_BLIT1` has **14 callers**: `arkanoid artful c64 cc os88type paccman
pacman paint skies telnet thewire weave wire word`. Filtering by `SMALLOMIT`,
**nine of them ship on the small disks** — arkanoid, artful, cc, os88type,
pacman, paint, weave, word, and any C package through the thunk — and on that
build every one takes a fallback, because `gfx_blit1` there is

```
gfx_blit1:            stc               ; kern_small carries the SLOT and not
                      ret               ; the body ... The body was measured on
                                        ; this build and refused (SPEC.md 5.4.2.5)
```

**So the +419 bytes that §12.4 of the cut plan charged against this row is not
this row's cost.** It is a decision already taken on its own merits, and this
plan should be priced with `gfx_blit1` present on both builds.

---

## 3. The commit is a FOUR-WAY choice, and all four are published and measured

The first draft of this section said *"the band commit is opaque, and that is
the sharp edge of the plan."* **Both halves of that were wrong.** There is a
transparent commit; and for the figures these programs draw it is the *worst*
of the four options, not the escape hatch. What follows is the whole published
surface with a measured price on each row.

| mode | slot | transparent? | priced per | measured, 5150 |
|---|---|:-:|---|---|
| **opaque band** | `OSAPI_GFX_BLIT1` | no | band **AREA** | **0.40–0.77 µs/px** — `BLIT1 632×8` 2.02 ms, `BLIT1 224×8 1bpp` 1.21 ms, `GFX_BLIT1 128×128` 12.59 ms |
| **masked band** | `OSAPI_ICON_DRAW` | **YES** | band **AREA** | **~39–47 µs/px** — a 12×12 is **6.7 ms** and a 16×16 **~10 ms**, both Hercules |
| **whole line** | `OSAPI_GFX_LINE` | yes | **INK** pixel | **37.1 µs/px**, of which ~714 µs is fixed |
| **one pixel** | `OSAPI_GFX_PIXEL` | yes | **INK** pixel | **640.87 µs** |

**The two units are the point.** A band is priced by its AREA whatever it
carries; a line and a pixel by the INK they lay down. So the mode is chosen by
the figure's DENSITY, and that is a per-app fact rather than a per-library one.

Worked, on the 127×32 line PERFORMANCE.md already benches — 127 ink pixels in a
4,064-pixel band:

| | |
|---|---:|
| opaque band (`GFX_BLIT1`) | **~3.1 ms** |
| direct (`GFX_LINE`) | **4.7 ms** |
| masked band (`ICON_DRAW`) | **~158 ms** |

**For a sparse figure the transparent band is 34× worse than the thing it was
supposed to rescue.** It is not a general-purpose escape; it is what it was
built for — a *small dense* glyph, a 12×12 control.

### 3.1 …and the published masked band is capped, by a BUFFER rather than by the renderer

Worth writing down because the code and the SDK comment disagree, and the
reason matters if anyone wants to lift it:

- **`icon_draw`, the kernel-internal entry, has no cap at all.** It reads
  `wwords` and `rows` as plain bytes into `[ico_ww]`/`[ico_h]`, and every
  stride, clip and row advance is computed from them. The 1bpp pass is a real
  masked read-modify-write — `ico_bbop` selects `or` (white) or `and-not`
  (black) per byte.
- **`icon_draw_x`, the API entry, refuses everything but 16×16.**
  `cmp al, ICO_STAGE_WW / jne .refuse` and `cmp ah, ICO_STAGE_H / ja .refuse`,
  because the record is copied into a **66-byte staging buffer**
  (`ICO_STAGE_SZ = 2 + 16 × 4`) — the package's record lives in the package's
  segment and ES is the kernel's.

Lifting it is therefore not a renderer change. Two shapes, neither costed:
widen the stage (a 64×64 one is **1,026 bytes of `.bss`**, on the build with
least to give), or render straight out of the caller's segment with no stage
at all — which is what `gfx_blit1` already does. **Neither is worth doing for
this plan**, because §3's table says a general masked band is the wrong answer
for the figures in question anyway.

### 3.2 The fourth option — and it may retire the walker outright

This is the answer to *"is keeping the walker inside the app on the table?"*
**Yes, and it does not need a band at all.**

`gfx_lstep` exists because of SPEC.md 5.6.7: a trail is drawn a couple of
pixels a frame and erased as one long line, and *"Bresenham over the whole line
does not visit the union of the per-frame segments"*. **That argument is about
the KERNEL holding the state.** Once the app holds it — which is the whole
proposal — the app knows *this frame's* segment endpoints, and

```
    OSAPI_GFX_LINE(p_prev, p_now)      ; this frame's segment, in ink
    ...later...
    OSAPI_GFX_LINE(p_prev, p_now)      ; the identical segment, in paper
```

is **exact**: `gfx_line`'s pixel set is a pure function of the endpoint pair
(SPEC.md 5.6.2), so an erase replaying the same segments replays the same
pixels. No band, no shadow, no opacity question, and the union that 5.6.7 says
whole-line Bresenham misses is drawn segment by segment, which is precisely
what it is.

Two costs to name: adjacent segments share an endpoint, so **one pixel a
segment is written twice** (PERFORMANCE.md rule 2, in miniature — idempotent
in both directions, but say it out loud); and the arithmetic below is
ARITHMETIC.

#### MEASURED — PERFORMANCE.md Set 139, Hercules 5150

This was arithmetic when it was written and it has since been benched
(`tests/gfxbench`, six rows `kwalk n=N x8` / `aline n=N x8`). **The prediction
was directionally right and its crossover was wrong:**

| pixels a block a frame | kernel `gfx_lstepv` | app walker + `GFX_LINE` a segment | |
|---:|---:|---:|---|
| 1 | **5,243.9 µs** | 7,884.8 | kernel **1.50×** |
| 3 | **7,703.8** | 10,496.9 | kernel **1.36×** |
| 10 | 16,271.5 | **9,179.6** | app **1.77×** |

**The two sides have different shapes, and that is the finding.** The kernel
walk is LINEAR and agrees with SPEC.md 5.6.8 to within 3% — a fitted intercept
of 4,013 µs for eight blocks against 5.6.8's 480×8, and 154 µs a pixel a block
against its ~175. The `gfx_line` side is **FLAT**: 986 µs a call at two pixels
and 1,147 at eleven, because a short line is its own fixed part and little
else.

So the crossover is **about FOUR pixels a block a frame** — (9,180 − 4,013) /
1,230 = 4.2 — and not the two this section predicted. The error was the
`gfx_line` fixed part: 714 µs, taken from the intercept of a 127-pixel row,
where a SHORT line on this geometry costs **~1,150**.

**What it decides, and it is not what was hoped:**

- **Missile's drain wins app-side, decisively.** `MC_DRNBUD` is 64 pixels a
  frame over ~4 blocks — sixteen a block — modelling at **23.7 ms** for the
  kernel walk against a measured **9.2 ms**. **2.6×.**
- **An ordinary trail loses.** One to three pixels a block a frame is what
  Cyclone's warp and Missile's own missiles do, and the kernel walk is
  **1.36–1.50×** ahead there.

**And `gfx_line` is not the only thing an app-side walker can plot through.**
The same run reads **`GFX_PIXEL` at 539.52 µs**, and a walker that owns its
state knows every pixel's coordinates:

| pixels a block a frame | `gfx_lstepv` x8 | 8 × `GFX_PIXEL` | 8 × `GFX_LINE` | best |
|---:|---:|---:|---:|---|
| 1 | 5,243.9 | **4,316** | 7,884.8 | **PIXEL**, 1.21× |
| 3 | **7,703.8** | 12,948 | 10,496.9 | **walk** |
| 10 | 16,271.5 | 43,162 | **9,179.6** | **LINE**, 1.77× |

> **`gfx_lstep` is the best of the three only between about 1.3 and 4.2 pixels
> a block a frame.** Below that its per-block setup costs more than a whole
> `gfx_pixel`; above it, its 154 µs marginal pixel costs more than amortising
> one `gfx_line` across the segment.

That is what the slot's 537/641 bytes actually buy — a **window**, not a
category. Whether Cyclone's warp and Missile's missiles sit inside it is a
reading nobody has taken, and it is now the cheapest thing left to measure
(this plan's 9.1).

### 3.2.1 Why an app must call a slot at all — and the slot that is MISSING

It is not only adapter genericity. Six things stand between a package and the
framebuffer, and two of them are the ones that bind:

| | |
|---|---|
| the adapter | VGA planar against two 1bpp cards, three strides, three framebuffer segments (SPEC.md 39) — the one everybody thinks of, and the easiest to abstract |
| **the CLIP REGION** | a window's content is partly covered by other windows; `wm_clip_tab` is a LIST of rects and every primitive re-runs itself per rect (SPEC.md 11.3) |
| **the CURSOR** | the arrow is IN the framebuffer with a save-under, so anything that writes pixels must first make the kernel take it down, or `gfx_unlock` paints the stale save-under back over the ink (SPEC.md 7.1.4) |
| the display | an extended desktop resolves virtual coordinates to a display and cuts a shape at the seam (SPEC.md 39.14) |
| the gfx lock | pre-emptive multitasking: another task may be inside a primitive (SPEC.md 7) |
| the address | `FSI_SEG` is the only published framebuffer and it is fsx-only (§2.1) |

The clip region and the cursor are **dynamic** — they change as the user drags
a window or moves the mouse — which is why "just tell the app the stride" was
never the answer.

**But none of that requires the kernel to hold the WALK.** Those six are
per-CALL concerns; the Bresenham is per-LINE. The slot that separates them
cleanly does not exist:

> **`OSAPI_GFX_POINTS` — `ES:SI` = CX coordinate pairs, plot every one in
> `[gfx_color]`.** The app computes the points; the kernel does the six things
> above, once for the call and once per point.

`GFX_SPANS` is its sibling and not its substitute: spans are CONSECUTIVE rows
with one x-interval each, which is what a polygon rasteriser emits. A walk, a
particle, a scatter plot emit arbitrary points.

#### What it would cost, in time

The inner loop already exists — `gfx_lstep_mono`'s body IS this, with the
Bresenham advance where the "read the next pair" would go: `gfx_ls_box` once,
then per point the four-compare box test, the `.miss` re-resolve, `gfx_ls_addr`
and the read-modify-write. So the marginal is the walk's own **154 µs**
(Set 139's fit; SPEC.md 5.6.8's 160–195) plus a full `gfx_ls_addr` where the
walk sometimes steps its address incrementally — **call it ~170 µs a point,
estimated against a measured comparable**.

Eight blocks stepping n pixels, against Set 139's measured rows:

| pixels a block a frame | `gfx_lstepv` x8 | 8 × `GFX_PIXEL` | 8 × `GFX_LINE` | **`GFX_POINTS`** |
|---:|---:|---:|---:|---:|
| 1 | 5,243.9 | 4,316 | 7,884.8 | **~1,502** |
| 3 | 7,703.8 | 12,948 | 10,496.9 | **~4,222** |
| 10 | 16,271.5 | 43,162 | **9,179.6** | ~13,742 |

**It is the best route below about 6.6 pixels a block a frame, `gfx_line` above
it — and `gfx_lstep` is then never the best at any n.** What it removes is the
walk's whole **~480 µs a block**, because that figure is *staging the caller's
Bresenham state in and back out* and a points list has no state to stage.

It also makes the erase exact for nothing: the app replays the same coordinate
list in the paper colour. That is stronger than `gfx_line`'s endpoint-pair
guarantee (SPEC.md 5.6.2) — it is literally the same points.

#### What it would cost, in bytes

Estimates against measured comparables, `kern_small`:

| | bytes |
|---|---:|
| the 1bpp body — prologue, the ES:SI cursor and count, the box test and `.miss` re-resolve, the RMW draw, the loop | **~115–140** |
| the X-stub entry | ~10 |
| a `kern_big` planar arm — `call gfx_pixel` a point when `[vid_planes] != 1`, which costs a planar caller nothing it does not pay today | ~15 |
| **reused, not written**: `gfx_ls_box` (173), `gfx_ls_addr` (29), `gfx_rowbase` | 0 |
| the API cell | 0 — the table is offset-addressed and a cell exists either way |
| **the walker it retires** | **−537 / −641** |
| **net** | **~−385 `kern_small`, ~−470 `kern_big`** |

`gfx_ls_box` is the walker's alone (`kernel/vga12.inc:1354`, `:1366` are its
only callers) so it goes with it; `gfx_ls_addr` is shared with `gfx_line` and
stays whatever happens.

**This is the best row in the document if it survives**, and what it is not yet
is built or measured. It would also be **the first batch pixel primitive
`kern_small` has ever had** — `gfx_spans` and `gfx_blit1` are both `stc`/`ret`
there today (§2.2, §2.4).

### 3.3 So the per-program answer, restated

| program | figure | mode |
|---|---|---|
| **Paint** | dense, own bitmap | opaque band, or `GFX_LINE` direct as today |
| **Sheet** | grid on fresh ground | opaque band |
| **Cyclone** | sparse, accumulating | **app walker + `GFX_LINE` a segment** — §3.2, no band, no shadow |
| **Missile** | sparse, over terrain | **app walker + `GFX_LINE` a segment** — and it is the one the arithmetic most favours |
| **Mines, Word, Weave** | small dense marks | opaque band, or `ICON_DRAW` where it fits 16×16 |

**No program in the tree needs a full-window shadow**, which is what the first
draft of this section thought Cyclone and Missile would have to carry.

---

## 4. The capability lattice

The rule, as set:

> wanting one capability should not drag in unused others. So walk drags in
> "how to draw a line", but "how to draw a line" does not drag in the walk.

The idiom is `apps/os88ui.inc`'s, unchanged: `%define GFXE_<CAP>` before the
`%include`, `%ifdef` blocks inside, and an implication written as a `%define`
at the top the way `OS88UI_BARONLY` already defines `OS88UI_NOBTN`.

```
    %define GFXE_WALK          ; implies GFXE_LINE
    %define GFXE_LINE_FAST     ; implies GFXE_LINE
    %include "os88gfx.inc"
```

### 4.1 The layers

| capability | implies | what it is | ~bytes |
|---|---|---|---:|
| `GFXE_BAND` | — | compose into your own 1bpp band; commit with `OSAPI_GFX_BLIT1`. **The pixel primitive lives here** — a bit-set, not a slot | ~80 |
| `GFXE_LINE` | `GFXE_BAND`¹ | Bresenham + the 1bpp per-pixel walk (`gfx_line_mono`'s body) | ~350 |
| `GFXE_LINE_FAST` | `GFXE_LINE` | SPEC.md 5.6.4.1's interval walk, **4.9×**. **Not one choice — §4.1.1** | **647**, or ~350 |
| `GFXE_WIDE` | `GFXE_LINE` | 5.6.5/5.6.6 dilation — the three-pass and the three-column mask walk | ~130 |
| `GFXE_RUNS` | `GFXE_LINE` | major-axis run coalescing, for a **4bpp** target | ~223 |
| `GFXE_WALK` | `GFXE_LINE` | resumable state: `linit`, `lstep`, replay-to-erase | ~230 |
| `GFXE_WALK_BATCH` | `GFXE_WALK` | `lstepv`'s many-walks-one-arrival | ~60 |

### 4.1.1 `GFXE_LINE_FAST` is not one choice — the menu is already measured

**[docs/plans/completed/LINE-PERF-PLAN.md](completed/LINE-PERF-PLAN.md) is
`gfx_line_fast`'s design record and its LINE-PERF-PLAN §5.2 is a gift to this plan**: it
prices three ways of making the fast walk *smaller* against exactly what each
costs in speed, on the 5150, with `tools/os88linecost.py pieces` as the
instrument. In the kernel those were rejected — a kernel cannot ask the caller
which trade it wants. **A library can**, and that is what turns the owner's
*"keep the optimisation or gate it out"* into a dial:

| sub-capability | bytes | what dropping it costs |
|---|---:|---|
| the eight octant loop bodies | 324 | dropping all of them is **no fast walk at all** — back to `GFXE_LINE` |
| four steep bodies behind one indirect jump | ~38 | **+6%** on a 32×127 line, **+24%** on a 45° one; the steep/shallow spread goes 1.18× → 1.47× |
| ink specialisation (an ink-independent plot: two RMWs a pixel) | ~148 | **+25% steep, +30% shallow, on every line** |
| the black loops | ~148 | **every erase back to 723 cyc/px — 4.8×**. Free for a package that never erases |
| `gfx_lf_wide3` (5.6.6.1's dilated steep) | 71 | `GFXE_WIDE`'s fast arm; a thin-only caller never wanted it |
| the eligibility setup | 161 | **not droppable** — LINE-PERF-PLAN §5.1 explains why the two octant blocks are mirror images that cannot share code on an 8086 |

So **Sheet, which draws grid lines in one ink and never erases**, plausibly
takes `GFXE_LINE_FAST` at ~350 bytes rather than 647. **Cyclone, whose whole
warp is a draw/erase pair**, must keep the black loops. This is the per-package
conversation the owner asked for, and it is already priced.

LINE-PERF-PLAN §4.5 also records the one thing measured and **refused**, so
nobody costs it again: *accumulating a framebuffer byte* — one RMW per byte
rather than per pixel — is worth **10%**, not the 8× the store count suggests,
because at 127×32 the row changes every fourth pixel and the byte must be spent
then anyway.

¹ `GFXE_LINE` implies `GFXE_BAND` only in compose mode. A caller that wants a
line drawn **directly** takes `GFXE_LINE_DIRECT` instead, which is a thin
adapter onto `OSAPI_GFX_LINE` and costs ~20 bytes — the point of naming it is
that a program keeping the kernel's line must not silently pull in a
rasteriser.

### 4.2 What the lattice buys over today's kernel

`GFXE_WALK` implying `GFXE_LINE` is what deletes §1.2's duplication: the
resumable walk becomes *"`GFXE_LINE`'s recurrence, with the state in the
caller's block instead of in `.bss`"*, so `gfx_lstep_mono`'s 212 bytes are the
`GFXE_LINE` body it already had. **A package taking `GFXE_WALK` should come out
around 580 bytes where the kernel spends 498 on the two plotters alone.**

This is the one claim in this document that is a *design* claim rather than a
measurement, and §9 owes evidence for it.

---

## 5. Who ships on the small disks, and what each would choose

The small apps disk is `APPS_TOOLS` less `SMALLOMIT` plus `APPS_GAMES` less
`SMALLOMIT_GAMES` — so **artful, calc, chart, fontview, fractal, hello,
notepad, paint, piano, sheet, texpad** and **arkanoid, cyclone, mines, missile,
pacman, solitair, tamegram**, plus the `SYSAPPS` and the core copies on the
system disk.

| package | `LINE` | `WALK` | `PIXEL` | `BLIT1` | on small? | likely choice |
|---|:-:|:-:|:-:|:-:|:-:|---|
| **Paint** | ✓ | | | ✓ + `SPANS` | yes | `LINE` + **`LINE_FAST`** + `WIDE` — §42.8's stroke is the reason, and 4.9× is new to this build |
| **Sheet** | ✓ | | | | yes | `LINE` only; grid on fresh ground, so compose mode fits |
| **Cyclone** | ✓ | ✓ (`LINIT`,`LSTEPV`) | | | yes | `WALK` + `LINE_DIRECT` — §3.2, no band |
| **Missile** | ✓ | ✓ (all three) | ✓ | | yes | `WALK` + `LINE_DIRECT`; the case §3.2's arithmetic most favours |
| **Mines** | | | ✓ (two 10px diagonals) | | yes | `BAND` alone — the X becomes 20 bit-sets and one blit |
| **Word** | | | ✓ (one pixel) | ✓ | yes | `BAND`; it already blits |
| **Weave** | | | ✓ (44–64 calls, **35–50 ms**) | ✓ | yes | `BAND` — the biggest single win in this column |
| **Artful, Arkanoid, PacMan, os88type** | | | | ✓ | yes | nothing; they are already band callers |
| **Tank** | ✓ | ✓ | | | **no** (`SMALLOMIT_GAMES`) | `WALK` for `tkattr.inc`; `tkraster.inc` is unaffected |
| **Skies, Telnet, The Wire** | | | | ✓ | **no** (`SMALLOMIT`) | — |
| **`SAVER.DRV`** | | ✓ | ✓ | | **no** (`SMALLDRIVERS = $(KMODS)`) | `WALK_BATCH` on `kern_big` |
| **`os88ui.inc`** | ✓ (checkmark) | | (macro, **0 users**) | | 25 packages | **neither** — see §6.2 |
| **`apps/cc`** | ✓ | ✓ | ✓ | ✓ | yes | the C SDK is its own question (§7) |

---

## 6. `gfx_pixel` — DEFERRED, and priced here so the deferral is informed

**This is a separate piece of work and comes AFTER the line waves**, by the
owner's decision. It is recorded here rather than started because the pricing
below is what makes the sequencing obviously right: `GFXE_BAND` is what gives
the callers somewhere better to go, so retiring the slot before the library
exists would be a caller sweep with no destination.

The proposal is to retire `OSAPI_GFX_PIXEL` outright as a benchmarking relic
that confuses readers. **Three corrections, and the conclusion still mostly
stands.**

1. **It is 12 bytes.** Not a primitive with a body — a wrapper:

   ```
   gfx_pixel:
       ...
       call gfx_fill               ; a pixel is a 1x1 solid rect
   ```

   Retiring it returns **12 bytes of `.text`**; the API cell stays either way,
   pointing at the shared refusing stub. `gfx_hline` (13 bytes) is the same
   shape and the same argument.

2. **It has shipping callers, not benchmark ones.** `word` (one pixel, the
   decimal tab's point), `mines` (two 10px diagonals), `weave`
   (44–64 calls, *"35–50 ms field-measured"*), `saver/svstars.inc`,
   `gfxbench`, and — the one that binds — **`os88_gfx_pixel()` published in
   `apps/cc/os88.h:475`**, so retiring it is a C SDK break.

3. **The confusion is real but it is about COST, not existence.** The slot reads
   like a cheap per-pixel primitive and is **640.87 µs**, because it is a whole
   `gfx_fill` arrival. That is a documentation defect, and SPEC.md 5.6's entry
   for it should say so in one line whatever else is decided.

### 6.1 …and `GFX_POINTS` is where it goes — which deletes the fallback too

The owner's framing, and it is the one that makes the retirement free rather
than merely cheap: **a collection of pixels is a pixel, with one of them in
it.** `gfx_pixel` is `OSAPI_GFX_POINTS` with `CX = 1` and a two-word array, so
the 12-byte wrapper and its slot have a destination and not just an exit.

It also reaches back into `gfx_points` itself. That routine's `kern_big` arm
calls `gfx_pixel` for a planar adapter or a second display (SPEC.md 5.6.9.2),
and PERFORMANCE.md Set 140 measured what that arm and the three gates in front
of it cost: `kern_big` came out **56 bytes over the estimate** and `kern_small`
only 17, and the difference is exactly them. Retiring `gfx_pixel` means that
fallback cannot call it — so the arm becomes `gfx_fill` on a 1×1 rect
directly, which is all `gfx_pixel` ever was, and the wrapper's bytes are
absorbed rather than moved.

**The gates themselves stay**, and it is worth being exact about why: they are
the ADAPTER dispatch, not a `gfx_pixel` artefact. A planar point is genuinely
different work, and a slot that took one body for both would be `gfx_fill` a
point at 539.52 µs — which is the whole win given away. On `kern_small` they
already compile out (§5.6.9.2), so that build carries neither the gates nor
the arm today.

**The library's answer is better than either keeping or retiring it**: under
`GFXE_BAND` a pixel is a bit-set in the app's own band and the commit is one
blit. Weave's 44–64 calls at 35–50 ms become **one** blit; Mines' X becomes 20
bit-sets. So the sequence is *give the callers somewhere better to go, then
retire the slot* — and the 12 bytes are the least of what that is worth.

### 6.2 `os88ui.inc` is not an obstacle — and it is ONE package, not 25

**CORRECTED, and the correction shrinks wave 2 to almost nothing.** Every
earlier revision of this document said the checkmark reached the 25 packages
that include `os88ui.inc`. It does not: both `OSAPI_GFX_LINE` call sites sit
inside `%ifdef OS88UI_MENU` (`apps/os88ui.inc:3469`–`4584`), the in-window menu
element is **opt-in**, and `apps/word/word.asm:20802` is the only file in the
tree that defines it.

So `OSAPI_GFX_LINE`'s shipped caller list is **six programs**, not
twenty-five-plus-four: **Paint, Sheet, Missile, Cyclone, Word** (through the
menu element) and **cword** (the same checkmark again, in C), with **Tank** on
`kern_big` and `wire` not shipping at all.

**And that moves wave 2 off the critical path.** Removing the checkmark's two
calls no longer unblocks a crowd; it removes one caller of six, and `gfx_line`
cannot leave until Paint, Sheet, Missile and Cyclone have gone too. Wave 2 is
therefore **deferred to after wave 6**, where it is a tidy-up rather than an
enabler — and its implementation should be reconsidered there rather than
taken from §8.1.3, because two things it assumed are false:

- **The speed is a WASH, not 2×.** The mark is seven distinct pixels, so
  `GFX_POINTS` draws it in 335 + 7×166 = **~1,497 µs** against the two lines'
  **~1,680** — and a `blit1` band's ~785 is a saving on something drawn once
  per menu open.
- **`blit1` cannot express the DISABLED mark on a 1bpp adapter.** The pen there
  is `OSAPI_GFX_PEN` CF=1 — `CDGRAY` *and* `[gfx_dis]` — and §39.4 makes that a
  checkerboard, which is §47's whole point. `gfx_blit1` ignores the pen on
  1bpp, so the dither would have to be composed into the band from the mark's
  screen-absolute `(x+y)` parity. `GFX_POINTS` has no such problem: it runs the
  same ink path as `gfx_pixel`, dither test included, which
  `tests/gfxpoints.py` case 2 is the proof of.

So when wave 2 is taken, **`GFX_POINTS` is the primitive** — same pixels, same
pen, no new failure mode on the build where greying is hardest.

37 files across **25 packages** include `os88ui.inc`, which is where the
"everyone would embed a rasteriser" objection comes from. It does not hold:

- the include's only line use is the **menu checkmark**, two *fixed* ±45°
  strokes 4 and 5 pixels long (`apps/os88ui.inc:3986`, `:3993`). A glyph, or an
  `OSAPI_ICON_DRAW` record, replaces it — **no library**;
- `UI_PIXEL` is a macro with **zero call sites** anywhere in `apps/` or
  `drivers/`. It is not a caller of `OSAPI_GFX_PIXEL`; it is a definition
  nobody expanded.

**The checkmark change can land on its own, before any of this**, and it should:
it is the only thing standing between `OSAPI_GFX_LINE` and a caller list of
four programs.

---

## 7. The C SDK converts WITH the rework, and it is the small half

An earlier revision of this section had `apps/cc/` as an outside constraint —
*"the kernel keeps a `gfx_line` body for as long as any C package wants one"*.
**That is wrong and the correction is the owner's: `apps/cc/` is ours exactly
as `apps/` is.** `os88.h`, `os88thunk.asm` and every C package in the tree are
in this rework, not around it.

And the conversion is **smaller than the assembly side**, because the whole C
line surface has one real caller:

| thunk | in-tree callers | what happens |
|---|---|---|
| `os88_gfx_line` | **one**: `cword`'s menu checkmark, `cwdrop.c:195`–`:196` — the same two strokes as `os88ui.inc` | moves to `os88_gfx_blit1`, **already published** at `os88.h:513`; the thunk is then withdrawn |
| `os88_gfx_linit` / `_lstep` / `_lstepv` | **none** — the header declares them and no `.c` in the tree calls one (`cwuitest.c` is a host-side stub) | withdrawn with the walker, free |
| `os88_gfx_pixel` | two, both `cword` chrome — `cwchrome.c:94`, `:468` | §6's deferred question, and `GFX_POINTS` is where it lands |

**So there is no C blocker and there never was one to weigh.** What a C
package cannot do is `%include` the NASM library — so if a C program ever wants
to *rasterise* rather than call a slot, it needs a C `gfx_embeddable`. Nothing
in the tree wants that today: `cword` draws a checkmark, and a checkmark is a
band.

Two new slots reach C for free — `os88_gfx_spans` and `os88_gfx_points` are
`os88thunk.asm` entries of the same shape as `os88_gfx_blit1`, a few bytes
each — and they are what a future C rasteriser would plot through anyway.

---

## 8. Waves, in the order the evidence ranks them

Each is independently landable and each is a separate PR.

| wave | what | prize | risk / blocker |
|---|---|---|---|
| **1 ✅ DONE** | **`gfx_blit1` on `kern_small`** (SPEC.md 5.4.2.5.1) — the pen, the second display, the VGA ports, the split pass and the port teardown each `%ifdef`'d out | **+472** measured; `kern_big` BYTE-IDENTICAL; nine shipped small-disk packages stop taking a fallback and Paint's one-bit canvas stops being 24× | none — `tests/paint1small.py` asks the RUNNING machine, because a thunk pointing at a body is not the claim |
| **1a ✅ DONE** | **`OSAPI_GFX_POINTS`** (§3.2.1, SPEC.md 5.6.9) — the slot that separates the six per-CALL concerns from the per-LINE Bresenham | **+167 / +221** measured; **165.96 µs a point** (Set 140), best route below 6.66 px a block a frame and better than the walk out to 37.7 | none — `tests/gfxpoints.py`, three cases, two breakages proven |
| **2 ⏸ HELD, and now PRICED** | the menu checkmark → anything but `gfx_line` | nothing on its own | **§8.6**: all four routes are worse or riskier than the two lines it would replace, and the only consumer is wave 8. Decide it THERE, with the tick measured in situ |
| **3 ✅ DONE** | **`apps/os88gfx.inc`** — `GFXE_BAND` + `GFXE_LINE` (the Bresenham), **WIREFRAME** the first customer, and it is a **LIFT** rather than a new implementation (§8.2) | proves the lattice; **+18 bytes on the customer, ZERO kernel bytes** | none — the kernel is untouched, and `wirefps`/`wireflick` are the A/B that was already in the suite |
| **4 ✅ DONE, RE-SCOPED** | **Paint's stroke stops calling `OSAPI_GFX_LINE`** (SPEC.md 42.23.8) — its screen half is a band out of the 1bpp canvas it already owns. The wave as WRITTEN could not be built and §8.3 says why | **13% off the screen half** (10,646 → 9,243 cycles, measured), +84 bytes of Paint, no kernel byte — and Paint off the `gfx_line` caller list, which is wave 8's first blocker | none — ten Paint rows pass, `tests/paintstroke.py` is the number |
| **5 ✅ DONE** | `GFXE_WALK` + `GFX_POINTS` on **Cyclone, Missile, Tank and `SAVER.DRV`** — the plan named two and there are FOUR (§8.1.4.2) | **Missile −33.5%, Cyclone −9.2%** median, measured; ~1,117 bytes across four package images and 1,024 of their bss, no kernel byte | none — nine rows pass, and `tests/gfxewalk.py` is the one thing no picture can show |
| **6 ✅ DONE** | gate `gfx_linit/lstep/lstepv` out of both kernels (SPEC.md 5.12.6) | **−513 / −617** measured, and `kern_big` **UNCROSSES AN IMAGE RUNG** — 512 bytes of every machine's RAM back | none — every walker moved in wave 5 first, which is the only thing that makes a refusing stub safe. (It landed behind `GFXWALK=1`; wave 8 deleted the knob) |
| **7 ✅ DONE, RE-SCOPED** | the PIXEL LOOPS move to `GFX_POINTS`, and `gfx_pixel` STAYS (SPEC.md 5.13) | Mines' wrong-flag X 20 calls → 1; `os88_gfx_points()` published to C. **No kernel bytes, and §8.7 says why there were never any to get** | none — `minexflag` is the row and it is exactly this path |
| **8 ✅ DONE** | **the whole `gfx_line` family out of both kernels** (SPEC.md 5.12.7), and `GFXWALK` with it | **−659 / −1,672 `.text`**, −36 / −47 `.bss`, and `kern_big` **UNCROSSES FOUR IMAGE RUNGS** — **2,048 bytes of every machine's RAM** | none — every caller was converted first (§8.11) |

**Waves 1–5 take nothing out of either kernel** (wave 1 ADDS to `kern_small` and wave 1a to both). That is deliberate: every one
of them is reversible and none of them can break a shipped program, so the
whole risky half of the plan is waves 4–6 and each of those is gated on a
program actually being on the library first.

---

## 8.1 THE END STATE, and the four things that move it off the obvious version

The obvious reading of §8 is: add `blit1` to `kern_small`, add `gfx_spans` to
both, delete the `gfx_line` family, put the checkmark on `blit1`, build the
library, and let apps plot through spans. **That is the right shape and four
things move the arithmetic.**

### 8.1.1 `GFX_POINTS` needs the clip region — and `gfx_ls_box` is where it comes from

A points slot is not exempt from §3.2.1's six: the clip region and the cursor
bind it exactly as they bind `gfx_pixel`. **The cheap answer is the walker's
own, and it is a piece the removal was about to take with it.**

`gfx_ls_box` (**173** small / **183** big) resolves *the clip rect containing a
point*, hands back an EMPTY box when none does — so the caller skips that pixel
and re-resolves at the next — and already scans in virtual space so a second
display works (SPEC.md 39.14.9). `gfx_ls_addr` (**29**) turns a point into a
byte and a bit mask through `gfx_rowbase`. Between them that is the whole of
what a points loop does per point, and `gfx_lstep_mono` is the worked example.

> **So `gfx_ls_box` and `gfx_ls_addr` STAY, and the rest of the family goes.**
> They are inside the 1,377 / 2,520 the line family measures, which is why the
> removal is **1,175 / 2,308** and not the headline figure.

`gfx_ls_addr` was staying regardless — `gfx_line` shares it — but `gfx_ls_box`
has only the walker's two call sites today (`kernel/vga12.inc:1354`, `:1366`),
so without this it would have left with it.

### 8.1.2 `gfx_spans` is NOT in this — and that is a simplification

The sibling slot stays exactly where it is: a body on `kern_big`, `stc`/`ret`
on `kern_small`, refusing under an armed clip on both (§2.2). Nothing here
needs it. **Spans is consecutive rows with one x-interval each** — what a
polygon rasteriser emits — and it would have had to be given clip handling and
a `kern_small` body to serve as a plot primitive. Points needs neither: it is
one new body, and its clip comes from a routine that already exists.

**What a figure-shaped app does instead is COMPOSE**, which is the second half
of the owner's own sentence and is the better answer where it applies:

- **Paint** already keeps its canvas as a 1bpp bitmap (SPEC.md 42.23). Its
  stroke rasterises into RAM it owns — no slot at all — and commits the damaged
  band with one `gfx_blit1` at 0.40–0.77 µs a band pixel. A 45° chord's band is
  a few thousand pixels, so it is **~3 ms against today's 13.03** (estimated
  from Set 139's blit1 rows), and the rasterising in between is the 24.6 µs a
  pixel §0 measured.
- **A walk** has no band worth committing — two or three pixels scattered over
  eight places — so it plots, and that is `GFX_POINTS`.

### 8.1.3 `cword` reimplemented the SAME checkmark in C

`apps/cword/cwdrop.c:195` is `os88_gfx_line(cw_m_x1+2, y+5, cw_m_x1+3, y+7, 0)`
and `:196` the up-stroke — the identical geometry to `apps/os88ui.inc:3986`. A
C package cannot `%include` a NASM library, but it does not need to here:
**`os88_gfx_blit1` is already published** (`apps/cc/os88.h:513`). So wave 0 is
two packages rather than one, and landing both **retires the C
`os88_gfx_line` thunk**, which then has no in-tree caller at all.

(`os88_gfx_pixel` still has two — `cwchrome.c:94`, `:468` — and that is §6's
deferred question, not this one's. §7 is the whole C surface, and it converts
with everything else rather than constraining it.)

### 8.1.4 The arithmetic, corrected

Measured where the table says measured; the rest estimated against a measured
comparable.

| | `kern_small` | `kern_big` |
|---|---:|---:|
| `gfx_blit1` body — **BUILT**, SPEC.md 5.4.2.5.1 | **+472** | 0, it is there |
| `GFX_POINTS` (§3.2.1) — body, X stub, planar arm; clip and addressing reused | +150 | +165 |
| the line family — **measured** at 1,377 / 2,520 `.text`… | | |
| …less `gfx_ls_box` and `gfx_ls_addr`, which both stay (§8.1.1) | **−1,175** | **−2,308** |
| `.bss` freed with it — **measured** | ~−61 | ~−72 |
| **net, `blit1` charged here** | **~−667** | **~−2,215** |
| **net, `blit1` charged to its own case (§2.4)** | **~−1,086** | **~−2,215** |

`blit1` belongs in the second row: nine shipped small-disk packages already
call the slot and take a fallback, so it is a decision standing on its own.

**That is ~1 KB off the floor machine and 2.2 KB off `kern_big`**, where
`KERN_CODE_MAX` — the guard that cannot be raised — is what binds.

### 8.1.4.1 Which route each program takes — RECUT ON THE CENSUS

The first version of this table was written from the plan's assumptions and
**two of its rows were wrong**. Wave 3 walked every call site in the tree
instead; §8.1.4.2 is the census and this is what it says.

| program | what it draws | route |
|---|---|---|
| **Paint** | a stroke into a 1bpp canvas it owns | **compose + `GFX_BLIT1`** — no plot slot at all, and ~4× faster than today |
| **`wire`** | one call an edge, a whole figure a frame | **`GFXE_BAND` + `GFXE_LINE`** — BUILT, wave 3. It is the figure caller the plan thought Sheet was |
| **Tank `tkattr.inc`**, **`SAVER.DRV`'s cube** | edges, `kern_big` only | `GFXE_BAND` + `GFXE_LINE`, the same shape as `wire`'s |
| **Missile** | trails and the drain, 1–16 px a block a frame | **`GFX_POINTS`** — one call covers both effects, which is what §3.2's per-effect problem dissolves into |
| **Cyclone**, **`SAVER.DRV`'s web** | accumulating warp walks, a few px a frame | **`GFX_POINTS`** — and NEITHER is a `gfx_line` caller (§8.1.4.2), so neither blocks wave 8 |
| **`os88ui.inc`, Sheet, `cword`** | a checkmark, three separate copies of it | **`GFX_BLIT1`**, one band (§8.1.3) |

**`GFX_POINTS` is the plot primitive and `GFX_BLIT1` the commit primitive**,
and no program in the tree needs a third.

### 8.1.4.2 The census — every `gfx_line` and `gfx_lstep` caller in the tree

Nobody had written this down, and the plan had guessed twice.

| caller | what it draws | kind |
|---|---|---|
| `apps/os88ui.inc:3986`, `:3993` | a menu checkmark, two strokes of ≤8 px | **tick** |
| `apps/sheet/sheet.asm:4481`, `:4491` | the SAME checkmark, its own copy | **tick** |
| `apps/cword/cwdrop.c:195`, `:196` | the same checkmark again, in C (§8.1.3) | **tick** |
| `apps/paint/paint.asm:7520` | one stroke segment (SPEC.md 42.8) | **figure** |
| `apps/missile/missile.asm:6904` | a trail, and the drain's erase | **figure** |
| `apps/wire/wire.asm:533` | one an edge — modes 0–2 only since wave 3 | **figure** |
| `apps/tank/tkattr.inc:536`, `:946` | `kern_big`, inside an fsx bracket | **figure** |
| `drivers/saver/svcube.inc:584` | the cube saver's edges | **figure** |
| `apps/cc/os88thunk.asm:225` | `os88_gfx_line()` — every C caller | **SDK** |
| `apps/missile/missile.asm:4421`, `:4462` | `LSTEP` / `LSTEPV` | **walk** |
| `apps/cyclone/cyclone.asm:2328` | `LSTEPV`, eight warps in one call | **walk** |
| `apps/tank/tkattr.inc:370` | `LSTEP` | **walk** |
| `drivers/saver/svshape.inc:352` | `LSTEPV`, the web arriving at once | **walk** |

Three corrections fall out of it:

1. **Sheet is a TICK caller, not a grid caller.** Both its `gfx_line` calls are
   `sh_menu`'s checkmark and it draws its grid with `gfx_fill` — which is right,
   a horizontal or vertical rule is a rect. The plan had it composing a grid.
2. **Cyclone does not call `gfx_line` at all**, only `LSTEPV`. It is on the WALK
   list and it does not block wave 8.
3. **`SAVER.DRV` is on BOTH lists** — `svcube.inc` draws edges with `gfx_line`
   and `svshape.inc` walks with `LSTEPV`. The plan had it under points alone.

And the count worth noticing: **the tick is six call sites in three packages**,
the single largest group, and every one of them is the identical two-stroke
mark. It does not consolidate — each package carries its own copy — but it does
mean wave 2 is three edits rather than the one §6.2 left it as.

### 8.1.5 What has to convert before `gfx_line` can go — RECUT

The first version of this list named Paint, Sheet and Cyclone. **Paint is done
(wave 4), Cyclone never called `gfx_line` at all, and Sheet is a tick and not a
figure** (§8.1.4.2). What is actually left:

| | what it draws | |
|---|---|---|
| `apps/os88ui.inc`, Sheet, `cword` | the same tick, three copies | **wave 2**, held (§8.6) |
| `apps/missile/missile.asm` `mc_line` | trails, and the drain's erase | a **figure**, and the speed gate below |
| `apps/wire/wire.asm` modes 0–2 | one call an edge | see below — this one is special |
| `apps/tank/tkattr.inc` ×2, `drivers/saver/svcube.inc` | edges, `kern_big` only | figures |
| `apps/cc/os88thunk.asm` | `os88_gfx_line()` | retires when its callers do |

**THE "MEASURE PAINT" GATE IS DISCHARGED — and asking WHO INHERITS IT is where
two documents turn out to be stale, this one included.**

Paint's screen half is a `gfx_blit1` band now and its canvas half never went
through the kernel, so a default `PAINT.O88` contains no `OSAPI_GFX_LINE` call
at all (`pt_lndraw` is behind `PT_LNLINE`, and `pt_lndraw` is absent from the
default build's map). Half B's Paint gate is simply spent.

The obvious next move is to ask `gfx_line_fast`'s own design record who else
cares, and **[LINE-PERF-PLAN](completed/LINE-PERF-PLAN.md) answers with `wire`
at 18.2 fps against 8.1 and with Missile Command's trails changing pace — and
does not mention Paint at all.** That is not because Paint was not a consumer.
It is because that document was written before §42.8 existed, and a design
record does not get updated when a *new* program starts using the thing it
describes. Reading it as a list of consumers is exactly the error this section
was correcting one level up.

**The code settles it in nine lines.** `gfx_line_fast` is reached on 1bpp
`kern_big` (`vid_mono`, `vid_planes == 1`) and refuses only three shapes: a
wide line (`[gfx_ln_wide]`), the **dither** ink class (§39.4 makes that a
per-pixel decision), and a line outside the clip box. Everything else takes it.
So the consumer set is not a list anybody wrote down — it is *every thin,
solid, in-box `gfx_line` call on a 1bpp machine*, which was Paint's stroke,
is Missile's thin trail draws, `wire`'s edges, Tank's, `SAVER.DRV`'s and all
three ticks.

**Which makes wave 4 worth more to wave 8 than it looks**: it did not only
discharge a gate, it **removed the busiest consumer** of the bytes half A
proposes to take. What wave 8 owes now is a measurement of the ones that are
left, and `wire` is the one with a published before-and-after to check against.

**And WIREFRAME is not just a caller — it is the SUBJECT.** §78's opening is
*"it exists to be the thing SPEC.md 5.6.4.1 was built for, and to say out loud
whether it worked."* Gating `gfx_line_fast` out of `kern_big` deletes the
comparison its status strip reports. That is not a reason to refuse wave 8, but
it is a cost the wave pays and `GFXWALK=1`'s shape is the answer: the knob has
to compile the fast walk back, or the instrument loses its subject.

### 8.1.5.1 Wave 8 is TWO changes and only one of them needs a conversion

This was never stated and it changes the order:

- **Half A — gate `gfx_line_fast`, `gfx_line_runs`, `gfx_lf_wide3` out of
  `kern_big` (−874).** No caller converts; `gfx_line` stays and simply gets
  **4.9× slower** for everyone still on it. So half A is *worse* the more
  callers remain, which is the opposite of half B.
- **Half B — gate `gfx_line` itself.** Needs every caller in the table above
  moved first, on wave 6's own evidence: a refusing stub is free only when the
  caller tests CF, and none of these do.

Half A is therefore the one to measure and half B the one to sequence, and
doing A first — the obvious order, since it needs no app changes — is the wrong
one: it charges the remaining callers the full 4.9× for as long as the
conversions take.

## 8.2 Wave 3, as built — and the first customer was already carrying the library

**Sheet was the wrong first customer and WIREFRAME was the right one**, for a
reason the plan could not have guessed and should have checked: `apps/wire/`
**already had `GFXE_BAND` and `GFXE_LINE`, hand-rolled** (SPEC.md 78.8). Its
"Composed" draw mode folds the two figures' vertex arrays into a union bound,
snaps the band onto the byte grid, papers it, ANDs the twelve edges out of it
with its own Bresenham and commits it with one `OSAPI_GFX_BLIT1`.

So wave 3 is a **lift**, and that is the strongest form the proof could have
taken: the first customer's code did not have to be invented, so the library's
shape was decided by working code rather than by this document. §1.2's finding —
*the kernel carries the same capability twice because the two halves were built
at different times* — turns out to be true one level out as well.

**What moved and what did not.** The cut is the one §5.12.4 names:

| stayed in `apps/wire/` | moved to `apps/os88gfx.inc` |
|---|---|
| `wr_mpt` — vertex to band coordinates | the running bounds (`wr_bspan` → `gfxe_bfold`) |
| the object-area refusal — the status strip lives under those rows | the margin, the byte grid, the two capacity refusals |
| `[wr_gb*]` — which band is ON THE GLASS (78.8.2) | the paper, the plot, the Bresenham (`wr_mline` → `gfxe_line`) |
| modes 0–2, which still call `OSAPI_GFX_LINE` | the commit (`OSAPI_GFX_BLIT1`, the stride, the four numbers) |

**Measured**: image **2,750 → 2,802**, bss **2,232 → 2,198**. The customer pays
**+18 bytes** — thirty of the fifty-two are the library's fifteen state words
moving out of bss and into the image, which is a wash and not a cost, and the
rest is the seams (`gfxe_bfold` takes a count where `wr_bspan` read `[wr_nv]`
itself, and `gfxe_line` takes its endpoints in registers where `wr_mline` read
four words). **No kernel byte moves and WIREFRAME does not ship** (SPEC.md
78.9), so the risk of wave 3 is exactly zero and the two rows that answer it
(`wirefps`, `wireflick`) were already in the suite.

### 8.2.1 Three things the lattice made the library decide

1. **The row step is an assembly-time choice, not a runtime one.** A
   power-of-two `GFXE_BAND_ST` gets a shift and anything else gets a `mul`,
   picked by a `%if` on the constant. It is once a line, not once a pixel — so
   the `%if` is not a speed argument, it is the argument that a caller who sizes
   a band conveniently should not pay for one who did not.
2. **The band buffer is the CALLER's and the state is the LIBRARY's.** Only the
   caller knows how big a band it can afford and only the caller can put one in
   bss; but the fifteen words the library needs go in the **image**, where a
   package's writable copy is per-instance exactly as bss is, so a customer does
   not have to find offsets for them.
3. **The opposite ink polarity is DOCUMENTED AND ABSENT** (SPEC.md 5.12.2). It is
   six lines under a `%if` and it would have been free to write; an untested arm
   in a shared library is worse than an absent one, so what it needs is written
   down instead. `gfxe_bclear` already takes the paper word in AX, so the half
   that a black ground actually needs is there.

## 8.3 Wave 4, as built — the wave as WRITTEN could not be built

Wave 4 was *"`GFXE_LINE_FAST` — Paint on `kern_small` takes the 4.9× it has
never had"*, and it does not typecheck. **An app-side walker cannot reach the
screen**: §2.1 is that a windowed package has no framebuffer, so a rasteriser in
Paint's image can only write RAM Paint owns and something still has to commit
it. Paint's line is on the *screen*, so `GFXE_LINE_FAST` has nothing to write
into there.

Looking properly at what Paint does dissolves it. **Paint rasterises a stroke
TWICE**: `pt_lineseg` walks it into the 1bpp canvas and the undo image — an
app-side Bresenham already, which is the very thing this plan proposes to give
other programs — and then `pt_lndraw` draws the same segment through
`OSAPI_GFX_LINE`. The canvas half needed nothing from this plan and the screen
half needed no walker at all: **the canvas already holds the answer**, so the
screen half is a COPY of the rect that changed.

### 8.3.1 The obvious spelling is 34% WORSE than the line

This is the part worth keeping, because §8.1.2 assumed it and it is wrong.
*"Commit the damaged band with one `gfx_blit1`"* reads as *"call `pt_blit`,
which Paint already has"*, and `pt_blit` costs **14,268 guest cycles** for a
stroke segment against `OSAPI_GFX_LINE`'s **10,646**. `pt_blit` is the path for
everything that **cannot know what it changed** — W_PAINT, undo, paste, a file
load — so it pays a clip, an inked-table band walk and a decode setup before it
reaches a blit. A stroke segment knows exactly what it changed.

Computing the band from the segment's own two endpoints and going straight to
`OSAPI_GFX_BLIT1` is **9,243** — 13% better than the line, and the route that
ships. All three numbers are guest cycles off `tests/paintstroke.py`, an exec
breakpoint on each side of the one call and nothing else in the window;
PAINT-STROKE-PLAN §7 is the standing warning that produced the row rather than
an estimate.

### 8.3.2 Wave 1 is what makes it work on the floor machine

Before `gfx_blit1` had a body on `kern_small` (SPEC.md 5.4.2.5.1, wave 1) this
change would have answered CF = 1 on every segment and fallen to `pt_blit` —
**slower than what it replaced**, on exactly the machine SPEC.md 42.8 exists
for. The order those two waves landed in was load-bearing, and it was not
planned that way: wave 1 was taken for nine other packages.

### 8.3.3 What is still owed to `kern_small`'s Paint

The 4.9× the wave was named for is **not delivered and is still available**. It
is `pt_lineseg`'s, not the screen half's: Paint's canvas walk is a per-pixel
read-modify-write and SPEC.md 5.6.4.1's interval walk emits RUNS, which in a
1bpp canvas is a byte fill. That is pure app-side work over RAM Paint owns, it
needs no slot and no kernel byte, and it is the one place in this plan where
`GFXE_LINE_FAST` would earn its 647 bytes. It is not in wave 4 because wave 4's
blocker was wave 8's, and this is not.

## 8.4 Wave 5, as built — and §9 item 4 is ANSWERED

**The measurement §9 asked for, first**, because it is what decides the wave
and nothing had taken it. Pixels a block a frame, read off the descriptor
arrays `gfx_lstepv` was actually handed:

| | live blocks a call | pixels a call | **pixels a BLOCK** |
|---|---:|---:|---:|
| Cyclone | 10.43 | 38.68 | **3.71** |
| Missile | 3.64 | 10.08 | **2.77** |

Both are inside `GFX_POINTS`'s best-route window (≤ 6.66, Set 140), and
Missile — fewer pixels per block — is further in. **That ordering predicts the
result and the result confirms it**: Missile's batch step is 33.5% cheaper and
Cyclone's 9.2%. A program stepping thirty pixels a block would have gone the
other way, which is exactly why the reading had to be taken rather than assumed.

### 8.4.1 The plan named two programs and there are FOUR

§8.1.4.2's census is what caught it: `apps/tank/tkattr.inc` and
`drivers/saver/svshape.inc` step the walk too, both `kern_big`-only, and **wave
6 cannot gate anything out of the kernel until all four have moved**. They are
converted here.

Tank's is the one where the change is not mainly about size: its letter cursors
called `OSAPI_GFX_LSTEP` **once per segment of a glyph**, so a wake spent an
arrival a segment and now spends one for the wake.

`SAVER.DRV` is the one place a point list can be sized by ARITHMETIC —
`SV_HACT` descriptors of at most `SV_HPX` pixels is an exact bound, so
`SV_PTMAX` is their product and can never be forced to commit early.

### 8.4.2 `gfx_linit` had to move too, and it is thirty bytes

Wave 6 gates `linit`, `lstep` and `lstepv`, so `GFXE_WALK` needs the block
setup as well. It is `OSAPI_GFX_LINIT`'s arithmetic verbatim — it has to be, or
a block set up one way and stepped the other walks a different line — and there
was never anything in it that needed the kernel.

### 8.4.3 The walk is a fifth of what §4.1 costed, and here is why

§4.1 put `GFXE_WALK` at ~230 bytes and §4.2 predicted ~580 for a package taking
it. The real thing is far smaller, and the reason is the one §3.2.1 was written
about from the other side: **the kernel's walk carries a clip rect, a
framebuffer byte and bit mask, the second display's translation and the
cursor's save-under, and `OSAPI_GFX_POINTS` does every one of them at the
commit.** So what moves into the app is only the recurrence — and it gets
*cheaper* in the move, because an app-side walk steps x with
`add si, [di + GLS_SX]` where the kernel needs a branch (its `sx` also has to
move a bit mask and a byte pointer).

That is the lattice's own claim (§4.2) coming out true for a reason the
document did not have: not that `GFXE_WALK` is `GFXE_LINE`'s recurrence — it is
not, it plots to a different sink — but that a slot which does the six per-call
concerns lets the app-side half be arithmetic and nothing else.

### 8.4.4 A full point list commits itself

The list is a batching window rather than a bound, so a buffer sized too small
costs an arrival and never a pixel. That makes the size a tuning choice, and it
also makes it **invisible**: break Cyclone's list down to four points and the
picture is identical — 5,207 points either way — while the arrivals go
133 → 1,317. `[gfxe_pflush]` counts them and `tests/gfxewalk.py` asserts 0,
which is the one thing about this wave that no screenshot can show.

## 8.5 Wave 6, as built — and the estimate was good to 4%

`.text` **−493 / −597** and `.bss` **−20** either side, against the plan's
−537 / −641 — the shortfall being that `gfx_ls_ink`, `gfx_ls_box` and
`gfx_ls_addr` stay, which §8.1.1 already said they would. `kern_big`
**uncrosses an image rung**, so the change is worth a further 512 bytes of
every machine's RAM on top of the sum.

**The order was the whole safety argument.** KERN-SMALL-CUT-PLAN §10 is the
standing finding that a refusing stub is only free when the caller tests CF,
and on the small floppies it mostly does not — a package that walked and
ignored CF would get **no pixels rather than wrong ones**, which is exactly the
class of defect an emulator cannot show. So wave 5 converted all four walkers
and landed; only then did the bodies go.

`GFXWALK=1` compiles them back, which is `BAND=1`'s shape and for `BAND=1`'s
reason: it is the only thing keeping the kernel path assembling, and it is the
A/B PERFORMANCE.md Set 139 came off. Two instruments need it — `tests/gfxbench`'s
`kwalk` rows and `tests/linetest`'s walk fans — and both say so at the top of
the file, because a bench row that times a `stc`/`ret` reports a number rather
than an error.

`apps/cc/os88.h`'s not-wrapped list carried these three under *"a state block
explicitly not yours to read"*; the reason has changed and so has the entry.

## 8.6 Wave 2 — the checkmark, and the answer was already in the file

Six call sites, three packages (§8.1.4.2), every one the identical two-stroke
tick of about eight pixels. **The owner's decisions collapse it to ONE.**

| | |
|---|---|
| `apps/sheet` | **deferred** — Sheet needs a whole pass to catch up with the tree and that is its own work; when it lands it becomes a consumer of the shared menu, the way Word is, and its private tick disappears rather than being converted |
| `apps/cword` | **deferred, likely permanently** — it was the C half of an A/B against the assembly port of Word and assembly won it decisively |
| `apps/os88ui.inc` | the one that converts, and §6.2 already established it reaches **Word alone** |

### 8.6.1 The pricing was WRONG, and the error was mine rather than a document's

The first version of this section put two `gfx_line` calls at **~554 µs**, built
from `GFX_LINE`'s 37.1 µs a pixel plus a 128.7 µs arrival. **That is the
MARGINAL cost and the arrival, not the call.** PERFORMANCE.md Set 139 measured
the thing itself on this exact geometry and says so in as many words:

> `gfx_line` … is **near enough FLAT**: **986 µs a call at two pixels** and
> 1,147 at eleven, *because a short line is its own fixed part and almost
> nothing else.*

So the tick costs **~1,972 µs**, not 554 — and every alternative was being
measured against a bar 3.6× too low. Both numbers are in PERFORMANCE.md, one
section apart. CLAUDE.md's own performance table carries the warning this
walked into: *never quote 756 as a floor a design must beat.*

### 8.6.2 …and there is a FIFTH route, already in this file, already shared

`os88ui_chk` (§13.15) — the check box `apps/skies` opts into with
`%define OS88UI_CHK` — does not draw a tick at all. It draws **a solid square
inside the frame**, and its own comment gives the reason:

> `; ...and the mark: a solid square inside it,`
> `; which reads on one bit as a tick does not`

That is **one `gfx_fill`**. Against the five routes, priced on measured figures
rather than marginal ones:

| route | cost | |
|---|---:|---|
| 2 × `GFX_LINE` — what ships | **~1,972 µs** | measured, Set 139 |
| **1 × `GFX_FILL` — the solid square** | **~756–900** | **the winner: no table, no buffer, no alignment constraint, and it already exists** |
| `GFX_BLIT1`, one band | ~250 + a second far call for the pen | cheapest on paper, but see §8.6.3 |
| `GFX_POINTS`, ~10 points | ~1,660 | a line is priced by its ink |
| `GFX_SPRITE1` | never measured | and now does not need to be |

**The solid square is the answer**, and it is better on four axes at once:
roughly half the cost, one drawing call, zero bytes of table, no geometry
constraint — and it is a LOOK improvement the owner asked for, on a mark that
one-bit adapters render badly today. It also makes the two shared controls agree
with each other, which they do not now.

### 8.6.3 Why `GFX_BLIT1` loses even though it is cheapest

The alignment hazard is worse than §8.6's first version knew, and the fix the
owner offered — *require the label to be drawn after the ground* — solves the
wrong edge.

`MRECT.x` is `MN_RECT.x + 8n + 4` (`apps/os88ui.inc:3639`, *"the panel starts
4px left of the title"*), and §11.94 snaps a window's content origin to 8 — so
**the panel's left edge sits at 4 mod 8** and the tick at 6. Snapping the band
down to the byte grid starts it **4 pixels left of the panel**, over the frame
and the desktop beneath it; snapping up starts it *after* the tick's first two
columns and loses them. The label-order rule fixes the RIGHT edge, which was
never the binding one. The Help row's own arm (`:3705`) puts `MRECT.x` at an
arbitrary value besides.

A fill has no such edge, which is most of why it wins.
## 8.7 Wave 7, as built — and there were never any kernel bytes in it

The wave was *"retire `gfx_pixel` → `GFX_POINTS` with `CX = 1`… 12 bytes + the
fallback arm's"*. Two things about that turn out to be wrong once the code is
in front of you.

**`gfx_pixel` is nine instructions** — *a pixel is a 1×1 solid rect*, and
`gfx_fill` clips and dispatches itself. A shim that staged one record and
entered `gfx_points` would be **longer than the body it replaced**, and slower
for the single-pixel case that is the only reason the slot exists. So the slot
stays, and §6's twelve bytes were an accurate price for something not worth
buying.

**And `gfx_points`'s general arm is not a fallback that can be deleted.** The
fast path is 1bpp / one display / one plane; a VGA or an extended desktop needs
the general route, and dropping the gates would cost every 1bpp machine the
3.9× the fast path buys. What the arm calls is `gfx_pixel`, which is already
the shortest spelling of the three instructions it needs.

**What the wave really is, and it is worth having: the LOOPS.** `GFX_PIXEL` is
640.87 µs and `GFX_POINTS` 165.96 a point, so a loop that plots more than two
or three pixels is paying an arrival each. There are five such sites and only
one converts:

- **`apps/mines`'s wrong-flag X** — twenty far calls, **12.8 ms per wrongly
  flagged cell of a lost board**, and a board can carry several. One arrival
  now, for +13 bytes of image and 80 of bss.
- **the C SDK** gains `os88_gfx_points()`, so a C package has the plot
  primitive at all — §7's content, and useful past this wave.
- **`os88ui.inc`'s `.gpix`** is the biggest loop of the five, 44–64 calls for
  one 12×12 glyph, and it is **REFUSED**: the file is included by ~25 packages,
  so a 12×12 point buffer is 576 bytes in every one of them, for a path that
  runs only when a control straddles a clip boundary. A shared include is the
  one place a per-caller buffer is the wrong shape.
- **`apps/word`'s decimal-tab point** is one pixel; one call either way.
- **`apps/cword`'s ruler fallback** is ~75 pixel calls and **is now
  unreachable**: wave 1 gave `kern_small` a `gfx_blit1` body, so the composed
  band no longer refuses on any shipped kernel. It was converted, found to push
  cword one byte over its 61,440 budget, and reverted once that was noticed —
  the right outcome twice over.

## 8.8 FOLLOW-ON, not this plan's: the two shared controls do not agree

Wave 2 turned up something that belongs to whoever next touches
`apps/os88ui.inc` rather than to the graphics library, and it is recorded here
because this is where the measurement was taken.

**`os88ui.inc` carries TWO check boxes that look nothing like each other.**

| | what it draws | how |
|---|---|---|
| `os88ui_chk` (§13.15) — `apps/skies` opts in | a frame, and the mark is **a solid square** | three `gfx_fill`/`gfx_frame` calls, no data at all |
| `os88ui_glyph` (§13.x) — the Control Panel's | a 12×12 box with an **X** in it | four bitmaps, a masked-sprite pass, and a 44–64-call per-pixel fallback |

The owner's reading is that the first is much better on the glass, and
`os88ui_chk`'s own comment already carries the argument — *"a solid square …
which reads on one bit as a tick does not"*. §39.4 is why: grey rounds to black
and a thin diagonal reads as noise on both 1bpp adapters, which are the machines
this OS is for.

### 8.8.1 What it costs today, measured — AND WHAT OF IT WOULD ACTUALLY GO

Per copy, from the kernel's own listing (`.cold`, `kern_big`), re-measured
after wave 8 — **two figures in the first version of this table were wrong and
are corrected here**:

| | bytes | fate under a conversion |
|---|---:|---|
| `os88ui_glyph` | 244 | **most of it goes** — see below |
| `os88ui_grec` + `os88ui_grec_d` (the sprite record and its staging) | 50 | **goes** |
| four 12×12 bitmaps (`_roff`, `_ron`, `_coff`, `_con`) | 96 | **goes** |
| `os88ui_gdn` | 20 | **STAYS** — a fill-drawn glyph still has to ask "is it pressed?" |
| `os88ui_glyph_f` (the far entry) | **4** | **STAYS** — it is `call` + `retf`, and this table first said 27 |
| **block today** | **414** | of which **146 is an outright delete** before a line of the body is touched |

**The 244 is bitmap machinery almost end to end**, which is what makes this a
removal rather than a swap: the per-row dither compose (`.gdith`/`.gdata`), the
record staging, the `UI_ICON` call and the `.gpix` per-pixel fallback all exist
to put a *bitmap* on the screen. A mark drawn with `UI_FILL` needs none of the
four.

**And one of them gets strictly better rather than merely smaller.** The dither
compose is there because, in `os88ui_glyph`'s own words, *"on a 1bpp adapter a
grey is a 50% STIPPLE that a mask pass has nowhere to put, so the caller lays it
into the data rows"* — it hand-composes §47's disabled grey, row by row, with a
screen-absolute parity term. A fill takes its grey from the **pen**, so the whole
of that becomes `stc` / `UI_PEN`, which is what `os88ui_chk` already does.

**The kernel is one copy of TWENTY-THREE** (this first said twenty-seven): the
block is inside `%ifndef OS88UI_NOBTN`, and of the packages and drivers that
include `os88ui.inc`, **22 carry it and 10 opt out**.

### 8.8.2 What a conversion might return — ESTIMATE, and the shape of the doubt

**If only the CHECK boxes convert**: the two check bitmaps go (−48) and a
frame-and-fill arm arrives, while the sprite path, the record and the per-pixel
fallback all stay for the radio. **Net ≈ zero.**

**And it is worse than that, on the evidence the first version of this section
did not go and get.** The Control Panel's glyph call sites are **ten RADIO rows
against three CHECK rows** — Display's three, Sound's two, Scheduling's two,
the adapter row, the desktop row and `cp_dngly`'s shared arm are all radios; the
checks are the 12-hour clock, the seconds in the menu bar, and a driver row. So
converting the check boxes alone touches **under a quarter** of the panel and
leaves the other three quarters drawn the old way: one page with two mark styles
on it, which is a worse answer than doing nothing. **Check-only is not a small
version of this change, it is a different and bad one.**

**If the RADIO converts too**: all four bitmaps (−96), the record and its
staging (−50), and most of `os88ui_glyph` — the sprite pass and the `.gpix`
per-pixel loop both exist only to put a *bitmap* on the screen. Call it
**−200 to −300 of `.cold` per copy**, which is `KERN_SIZE` and the cold rung
rather than `KERN_BUDGET`, plus the same again in twenty-six package images.

**The doubt is the radio, and it is a look question rather than a size one.** A
radio is a circle with a dot, and §39.4 says a ring is dotted on 1bpp — so it is
already the glyph that renders worst. What it becomes in a fill-drawn scheme (a
diamond? a smaller square? a frame with a different inset?) is the owner's call,
and the byte figure above is worth exactly as much as that decision.

**And it would retire wave 7's one refusal.** §8.7 declined to convert
`os88ui_glyph`'s `.gpix` loop — the biggest pixel loop in the tree, 44–64 far
calls for one glyph — because a 12×12 point buffer is 576 bytes in each of ~25
packages. A mark drawn with fills needs no buffer, so the same change deletes
the loop instead of feeding it.

**`os88ui_chk` IS NOT A DROP-IN, and the sentence above is about a look rather
than a call.** It draws its own ground, its frame, its mark **and its label**,
off a record at BX; `os88ui_glyph` takes CX/DX/AL/AH, draws a bare 12×12 and
nothing else, and every Control Panel page letters its own labels beside it
(§8.8's `cp_dngly` carries the id in BH for exactly that reason). So the work is
a **new fill-drawn body in `os88ui_glyph`'s own signature** — the shape borrowed
from `os88ui_chk`, not the code — and the arrival above is priced against the
wrong routine.

**This is an estimate against a measured base, not a measurement.** The honest
figure comes from building it; §10's method (per-symbol map, reconciled against
the section lengths) is how, and the 414 above came off it.

## 8.9 Wave 8's caller list is much shorter than it looks — THREE apps had already built it

The owner's instinct was that WIREFRAME is the place to start and that whatever
is done there ports cleanly into `SAVER.DRV`'s cube, *"wireframe except
fullscreen and bouncing around"*. Both halves are right and both are **already
done** — by the programs themselves, before this plan existed.

| | its composite | what its `gfx_line` calls are |
|---|---|---|
| `apps/wire` | mode 3, SPEC.md 78.8 — **this plan's library was lifted out of it** (§8.2) | modes 0–2, the **control arm**: they exist to be compared against mode 3 and against the kernel's own walk |
| `drivers/saver` `svcube.inc` | SPEC.md 79.5.6 — a 128×128 mask, `or`-plotted, blitted whole; it is the **default** | `sv_cube_edge1`, reached only when `OSAPI_GFX_BLIT1` answers CF = 1, under a comment saying *"which on a kern_big machine with these arguments it cannot — so this is insurance and not a path"* |
| `apps/paint` | SPEC.md 42.23's 1bpp canvas | converted in wave 4 |

**That is the same shape three times, and it is the plan's central claim
arriving from the other direction.** Nobody was told to write a band composer;
three separate programs did, because composing and committing once is what a
figure wants on this machine. The library's job was never to invent the
capability — it was to stop it being written a fourth time.

### 8.9.1 …and wave 1 made `SAVER.DRV`'s fallback unreachable

`sv_cube_edge1`'s comment says the refusal *"cannot"* happen on a `kern_big`
machine. Since SPEC.md 5.4.2.5.1 gave `kern_small` a `gfx_blit1` body it cannot
happen there either, so the arm is dead on **every shipped kernel** — the third
time wave 1 has done that to a `gfx_blit1` refusal path (`apps/cword`'s ruler
was the second, §8.7).

### 8.9.2 What is actually left

| caller | |
|---|---|
| `apps/os88ui.inc` | ✅ **converted** — the tick is a solid square (SPEC.md 13.16.2.1) |
| `apps/paint` | ✅ converted, wave 4 |
| `apps/missile` `mc_line` | **a real conversion, and the only speed-critical one** — trails and the drain's erase |
| `apps/tank` `tkattr.inc` ×2 | **a real conversion** — the attract logo's letter segments, `kern_big` only |
| `apps/wire` modes 0–2 | a control arm; gating `gfx_line` deletes what the instrument measures |
| `drivers/saver` `sv_cube_edge1` | dead insurance (§8.9.1) |
| `apps/sheet`, `apps/cword` | deferred by the owner (§8.6) |
| `apps/cc/os88thunk.asm` | retires when `cword` does |

**So wave 8's conversion work is Missile and Tank**, and everything else is a
decision rather than a port. That is a far smaller wave than §8.1.5 describes,
and the reason it looked big is that a caller census counts call sites and
cannot see that two of them are an A/B and a third is insurance — §9.1's rule
again, one level along: a census is the right instrument for *what would break*
and the wrong one for *what is work*.

## 8.10 Missile, mapped — and its `gfx_line` arm exists for a reason wave 5 removed

§3.3 named Missile *"the one the arithmetic most favours"* and §3.2's fourth
option — an app walker committing `OSAPI_GFX_LINE(p_prev, p_now)` a segment — is
what it proposed. **Missile already does exactly that**, and wave 5 has since
made the premise underneath it false.

**The structure.** A trail takes one of two arms, decided by `mc_tr_lay`:

- **the walk** (`mc_iarm` = 1) — `gfxe_winit` + `gfxe_wstep` + `gfxe_pput` since
  wave 5, and measured 33.5% cheaper than the kernel's;
- **the segment path** (`mc_iarm` = 2, `.iseg`) — `mc_line(p_prev, p_now)` once
  a frame, which IS §3.2's fourth option, hand-rolled;

and its erase is `mc_line(start, current)` with `[mc_lfat]` set, one long
**dilated** line, because §5.6.5: *"we DRAW in per-frame segments and erase in
one long line, and those two Bresenhams disagree by a pixel."*

**`mc_tr_lay` refuses — sending a trail to the segment path — for exactly two
reasons**: the Mode X surface (§53.7, no kernel slot is legal), and *either
endpoint off the content*. The second carries its own explanation:

> *…not for a line with an end outside our content, where the kernel would clip
> to the SCREEN and paint over the desktop.*

**That is a statement about the KERNEL's line, and since wave 5 the walk is not
the kernel's.** `gfx_points` resolves every point against `wm_clip_tab`
(§5.6.9.1) and simply drops one no rect holds — so an off-content trail can take
the walk arm now, and the whole segment path with it. This is §3.2's own
argument one turn further along: it said the walker could move into the app
because 5.6.7's reasoning was about the kernel holding the state; the same is
true of this refusal, and for the same reason.

### 8.10.1 …which matters because the DILATED erase has no good conversion

The thin arm converts trivially: a per-frame segment is one to three pixels, and
`GFX_POINTS` at 165.96 µs a point beats `gfx_line`'s flat ~986 µs a call by
about 1.6×, on machinery Missile already carries.

**The fat arm does not.** §5.6.5's dilation is the line plus one pixel either
side of the minor axis — three times the points — so a 60-pixel trail erase is
~30 ms through `GFX_POINTS` against ~9.5 ms as one dilated `gfx_line`. **3×
worse**, and building `GFXE_WIDE` would not change that: the cost is the point
count, not the code.

So converting `mc_line` piecemeal leaves Missile on `gfx_line` for the erase and
unblocks nothing. **Retiring the segment path retires both arms at once**, and
the dilation with them — an erase that replays the walk is exact by
construction, which is what §5.6.5 is a workaround for.

### 8.10.2 What is owed before it is built

**A measurement that could not be taken here.** `mc_line`'s traffic was
instrumented over 30 guest seconds of scripted clicking and read **zero calls** —
the attract state routes every trail through the batch — so how often the
segment path runs in real play, and how long its lines are, is unmeasured. That
decides whether this is worth doing at all, and the row wants a way to drive
Missile into a live game rather than its attract loop.

**And one behavioural risk to name.** `mc_clamp` is what keeps an off-content
line off the desktop today. Moving those trails onto the walk arm makes
`gfx_points`'s clip the thing that does it instead, which is correct in
principle and is a real change to a game's core rendering, on the path
docs/FIELD-NOTES.md has already had one trail defect on. It wants the
measurement above first, not after.

## 8.11 Wave 8, as built — and the last two callers were not conversions

`gfx_line`, `gfx_line_raw`, `gfx_line_mono`, `gfx_line_fast`, `gfx_line_runs`,
`gfx_lf_wide3`, `gfx_lm_pre` and `gfx_line_flush` are out of both kernels, with
§5.6.7's walk and the `GFXWALK` knob §8.5 had kept it behind — **a knob that
compiles a comparison nothing can make any more is dead code with a switch on
it**, which is the owner's call and the right one.

| | `.text` | `.bss` | |
|---|---:|---:|---|
| `kern_small` | **−659** | −36 | the fast walk and the runs path were `%ifdef KERN_BIG` |
| `kern_big` | **−1,672** | −47 | and **four image rungs**: 2,048 bytes of every machine's RAM |

Of `gfx_line`'s whole working set exactly **one byte** survives — `[gfx_ln_ink]`
— and `gfx_points` is its only reader. `gfx_ls_ink`, `gfx_ls_box`, `gfx_ls_addr`
and `gfx_ls_lx`/`ly` stay for the same reason (§5.12.7).

### 8.11.1 The last four callers, and only two were code to convert

**Sheet and `cword`** were deferred (§8.6), but Sheet *ships* — so its private
tick had to go before the slot could, and it is the same one-`gfx_fill` solid
square (§81.30, §13.16.2.1). `cword`'s went with it, which retired the C
`os88_gfx_line` thunk, its header declaration and the host-test's model of it.
Neither needed the big Sheet pass; both are ten lines.

**`apps/wire`'s modes 0–2 and `SAVER.DRV`'s `sv_cube_edge1` were DELETED, not
ported** (§78.5.1, §79.5.6.1). Wire's three orders were an A/B against its own
composite, and the composite already won both of §78.5's counts; saver's arm was
insurance against a `gfx_blit1` refusal that cannot happen — `gfx_blit1_x`
refuses four things and that call makes none of them, and wave 1 removed the
last machine where it could. −380 bytes of wire, −186 of saver.

### 8.11.2 Three instruments lost their subject and went with it

`tests/linetest` (§5.6.6's dilated walk), `tests/wirefps` (what §5.6.4.1 was
worth) and `tests/linefast` (that the fast walk drew the same pixels) all
measured something that no longer exists, as did `gfxbench`'s line and walk
rows and Paint's `PT_LNLINE` arm. **A bench row that times a `stc`/`ret` reports
a number rather than an error**, which is worse than no row.

**`OSAPI_GFX_POINTS` lost its `pts` bench rows with the walk** — they only ever
existed to be read *against* it, and *"cheaper than the walk"* is not a
measurement once there is no walk. It is the slot every app-side walker in the
tree commits through (§5.12.5), so leaving it unmeasured would have made the
library's own commit the one cost nobody could quote. **Two rows replace them**
(8 points and 24): two unknowns, two readings, `arrival + N × marginal`
determined, every longer commit off the fit. The dead scaffolding went with the
rows it fed — `gb_lsinit`, `gb_b_lstep8`, `gb_b_lstepv8`, `gb_b_line8n`,
`gb_b_lsteep`, `gb_b_lshal`, twelve row labels and four `.bss` reservations.

## 9. What is NOT settled — evidence still owed

1. **§4.2's layering claim is a design claim, not a measurement.** *"`GFXE_WALK`
   is `GFXE_LINE`'s recurrence with the state in the caller's block"* has to be
   written and assembled before the ~580 is quotable. If the two walkers turn
   out not to unify, wave 3's prize is ~790 and wave 4 is still worth taking.
2. **The commit's real cost is per BAND AREA, not per pixel.** A 127×32 line's
   band is 508 bytes whether it carries 127 pixels or 4,064. **Measure a real
   figure** — Sheet's grid, Paint's stroke — never a single line.
3. **Clip and ink come back.** The 24.6 µs is a rasteriser with no clipping, no
   ink and no dither. A caller that needs them pays them, and `gfx_blit1` still
   refuses an x off the byte grid.
4. ~~§3.2 is arithmetic and is the single most valuable thing to bench.~~
   ~~**DONE — PERFORMANCE.md Set 139.** …instrument Cyclone and Missile for
   the pixels-a-block-a-frame they actually step.~~ **ANSWERED — §8.4.**
   Cyclone steps **3.71** pixels a block a frame and Missile **2.77**, read off
   the descriptor arrays `gfx_lstepv` was actually handed rather than off a
   counter in either program. Both are inside the ≤ 6.66 window, and the
   ordering predicted the result.
5. ~~**Nothing here has been measured on the glass.** …No wave has been
   built.~~ **Waves 1, 1a, 3, 4 and 5 are built and every headline number in
   them is guest cycles off a breakpoint bracket on `os8088_5150_herc_gla`, not
   arithmetic.** What is still quoted rather than measured is the *unbuilt*
   half: §4.1's byte table for `GFXE_LINE_FAST`, `GFXE_WIDE` and `GFXE_RUNS`,
   and waves 6–8's kernel savings.

6. **§4.1's size table is now known to be wrong on the high side, for one
   reason that generalises.** `GFXE_WALK` came out a fifth of its ~230 estimate
   because `OSAPI_GFX_POINTS` keeps the clip, the cursor, the adapter and the
   display in the kernel (§8.4.3). Every other row of that table was costed the
   same way — as *"the kernel's routine, moved"* — so `GFXE_WIDE` and
   `GFXE_RUNS` should be re-costed as *"the arithmetic, with the six per-call
   concerns left behind"* before either is quoted again.

---

## 9.1 A design record names what it MEASURED, not what uses the thing

Worth stating on its own, because it caught this plan twice in one session and
the second time it was this plan doing the catching.

§8.1.5 said *"measure Paint before gating `gfx_line`"*. Wave 4 discharged it,
and the natural next question — *who inherits that gate?* — was answered by
opening `gfx_line_fast`'s own design record, which names `apps/wire` and
Missile Command **and does not mention Paint**. Taken at face value that reads
as *"Paint was never a consumer"*, which is false: LINE-PERF-PLAN predates
SPEC.md §42.8, and **a design record is written once, at the landing, and is
never revised when a later program starts using what it describes.**
docs/README.md already says a `completed/` plan is *how something got there and
never what it does* — this is the sharp edge of that.

**The check that settled it was nine lines of `kernel/vga12.inc`**: the
eligibility gate. A primitive's consumer set is a property of its refusal
conditions, not of any prose — `gfx_line_fast` refuses a wide line, the dither
ink class and a line outside the clip box, and takes everything else on a 1bpp
`kern_big`. That is a question the code answers exactly and that no document
can answer at all, because the answer changes every time a caller is added.

The rule that falls out, for anything in this tree:

> **When a plan asks "who uses X", read X's REFUSALS, not X's plan.** The plan
> tells you who was measured on the day; the refusals tell you who qualifies
> today. Where the two disagree, the plan is the stale one — always, and
> silently, because nothing fails when it goes wrong.

The same shape is why §8.1.4.2's census exists at all: §8.1.4.1's routing table
had Sheet composing a grid and Cyclone calling `gfx_line`, and both survived
several revisions of this document because a table of programs is exactly the
kind of thing that reads as checked.

### 9.1.1 The same rule for NUMBERS, and this one was self-inflicted

§8.6 priced the menu tick at 554 µs from `GFX_LINE`'s **marginal** 37.1 µs a
pixel plus its arrival, while PERFORMANCE.md Set 139 — one part of the same
file — had measured the call itself at **986 µs at two pixels** and said
explicitly that *a short line is its own fixed part and almost nothing else.*
Every alternative was then judged against a bar 3.6× too low, and the section
concluded "nowhere better to go" when the answer was twice as good.

> **A marginal rate is not a call cost, and a summary table is not a
> measurement.** Before pricing anything against a primitive, find the SET that
> measured that primitive at the size you are about to use it, not the per-unit
> figure a briefing quotes.

CLAUDE.md's performance table already carries the warning in the general form —
*never quote 756 as a floor a design must beat* — and this is what walking into
it looks like from the inside: the arithmetic was tidy, internally consistent,
and wrong.

### 9.1.2 …and the fifth option was in the file being edited

`os88ui_chk` draws its mark as **a solid square** and carries the reason in a
comment — *"which reads on one bit as a tick does not"* — eight hundred lines
above the tick this plan was trying to find a home for, in the same file.
Neither the census nor the four-route table found it, because both were built
by asking *"who calls `gfx_line`"* rather than *"what else in this file draws a
mark"*. A caller census is the right instrument for a REMOVAL and the wrong one
for a REPLACEMENT, and §8.6 was quietly doing the second.

## 10. Method, and how to re-derive any of it

Sizes are per-symbol from `nasm [map all]` on a whole-kernel re-assembly,
summed per section and **reconciled against the section lengths from the
summary block** — that equality is what makes a per-feature figure quotable
rather than indicative (KERN-SMALL-CUT-PLAN §9.1's method, and the same
`symmap.py`). A symbol's size is the distance to the next symbol in the same
section, with NASM's anonymous macro locals attributed to the preceding
top-level label.

Reconciliation for the family total: the contiguous block `gfx_linit` →
`gfx_blit4` is **1,368** (small) / **2,511** (big); the per-symbol sum is
**1,377** / **2,520**; the difference is `gfx_lmtab` (8) and `gfx_lmsk` (1),
which sit elsewhere in `.text`. Both numbers are right and they answer
different questions — quote the contiguous block for *"what a gate returns"*
and the per-symbol sum for *"what the capability weighs"*.

Timings are PERFORMANCE.md's, all from the field 5150:
`GFX_PIXEL` 640.87 µs, `GFX_LINE` 37.1 µs a pixel (31.6 less the arrival), the
candidate mask rasteriser 24.6, the walk's marginal pixel ~175, its block setup
~480, an arrival 128.7.

---

## 11. Sequencing, as decided

1. **The line waves first** (§8 waves 0–6). `gfx_pixel` (§6) is a different
   piece of work and follows them.
2. **Keeping the walker inside the app is fully in scope** and is `GFXE_WALK`.
   §3.2 is what it plots through, and the answer is likely to be
   `OSAPI_GFX_LINE` a segment rather than a band.
3. **`gfx_blit1` on `kern_small` is not this plan's to justify** — it is
   already wanted for its own reasons (§2.4), and this plan assumes it.
