# A window's own pointer

**Research document written alongside a working prototype.** SPEC.md §7.2 is
the binding contract for what landed; this is the study behind it — what the
defect actually was, the three designs, why the one that shipped is the one
that shipped, what it costs measured rather than estimated, and the six traps
it sprang on the way. Read §7.2/§7.2.1/§7.2.2 for the contract and this for
the reasoning.

The ask, in the requester's words:

- Missile Command, **in windowed mode**, draws a second non-system cursor
  inside its window.
- Investigate **allowing an app to change the cursor**.
- Investigate that cursor change being **only over the window in question**.

---

## 0. The verdict, up front

**Both are affordable, they are not the same size, and the expensive half is
not the one the question is about.**

> **Changing which picture is drawn is three instructions.** The cursor
> renderer reads its bitmap in exactly three places — `cur_put_mono`,
> `cur_move_mono`'s pass 2 and `cur_draw` — and all three say
> `mov si, cur_and`. Making that `mov si, [cur_tabp]` is the whole of it.
> **`cur_get_mono` did not change at all**, which is the property that makes
> this cheap rather than merely small: the erase replays banked geometry and
> drains a save buffer, so it is a statement about the *cell*, not about the
> picture inside it. Shapes that share a cell share an erase.
>
> **Confining it to one window is one byte and one call.** The shape rides in
> `W_FLAGS`' spare high byte — no record grew, no template changed, and
> `wm_create`/`wm_destroy` already zero it, which is the arrow — and
> `cur_shape_pass` in `ui_task`'s `.yield` tail asks `wm_hit` which window the
> pointer is over. One compare per pass when nothing moved.
>
> **The expensive half is the HOT SPOT.** A crosshair has to be centred on the
> pointer, and the arrow's hot spot is (0,0) — which every walk in `mouse.inc`
> has assumed for years, because `mouse_x`/`mouse_y` are clamped to the screen
> so the cell's top-left could never be negative. A centred hot spot makes it
> negative within `hx` of the left edge and `hy` of the top, and a negative
> byte column addresses the row above the framebuffer. That is real work in
> the most correctness-fragile code in the kernel.

Measured cost of the whole thing, both kernels, `tools/kernsize.py`:

| | `.text` | `.bss` | `.cold` | footprint | rung |
|---|---|---|---|---|---|
| kern_big | **+323** | +4 | 0 | 99,840 → 100,352 | one crossed, spare 2,560 → **2,048** (4 steps) |
| kern_small | **+316** | +4 | 0 | 94,208 → 94,720 | one crossed, spare 2,048 → **1,536** (3 steps) |

Missile Command grew **24 bytes** and that is heap, not budget.

**The rung was going to be crossed by anything.** kern_big's image rung had
**115 bytes** left before this and kern_small's 187, so trimming the feature
could not have avoided the 512 — the same situation SPEC.md §9.6's keyboard
mouse was in when it cost a whole step for 114 bytes of code. That is the
guard working, not the feature being dear.

---

## 1. What the defect actually is

`MC_CHARM` is 8 and its comment says why:

> arm length. Big enough to read AROUND the system arrow, which the kernel
> keeps drawing at the same point (SPEC.md 11.2: even fullscreen, the cursor
> stays live) — a short crosshair simply hides under it

So the game already knew. `mc_cross_on`/`off`/`moved`/`need`/`xor` is an XOR
overlay the game draws, erases and re-draws itself, standing on top of the
kernel arrow. Two pointers, one mouse.

**"In windowed mode" is precise, and the precision matters.** Three cases:

| how Missile is on screen | kernel pointer | Missile's crosshair |
|---|---|---|
| ordinary window | drawn | drawn — **two pointers** |
| §11.2 fullscreen *window* | drawn (§11.2 keeps it live) | drawn — **two pointers** |
| §53 fsx bracket | off (the held lock keeps it off) | drawn — one pointer |

SPEC.md §48.13 already names the third row from the other side: "the cheapest
confirmation the bracket is live is that the DOUBLE CURSOR goes." So the
bracket is where this is already right, and the bracket is also where the
kernel cannot help — it holds the gfx lock for its whole life by construction.

**Which caps the prize.** Missile keeps `mc_cross_*` for the bracket and
skips it windowed; the ~130 lines do not go away. What it gets back windowed
is `crs`, measured at **4.4 ms of a ~55 ms frame** (PERFORMANCE.md Part 9
Set 12) — about 8%, against a median worst frame of 52.4 ms. Worth having,
and not the headline. **The headline is that there is one pointer.**

---

## 2. Three designs, and why the middle one

### Option 0 — let a window HIDE the system pointer over itself

Cheapest by a mile: `cursor_hide`/`cursor_show` is already a refcount, so an
extra hide while the pointer is inside a window's content just works
(`cur_level` = −2 means hidden and staying hidden; only the 0 → −1 and −1 → 0
edges draw). No bitmaps, no hot spot, no clipping. Missile keeps its crosshair
exactly as it is — already optimised (§48.11) and verified — and simply stops
having the arrow on top of it.

**Rejected as the primary answer, kept as the fallback.** It generalises the
bracket's behaviour to the windowed case, which is elegant, but it makes every
app that wants a different pointer *draw one*, and the whole complaint is that
drawing one is what Missile is doing. It also leaves the pointer invisible over
any part of the window the app is not tracking, and the app must be told when
it stops being frontmost to put it back (§44.8's two questions). It remains the
right answer for an app whose pointer is genuinely its own — a paint program's
brush preview, say — and nothing here forecloses it.

### Option 1 — a small set of built-in shapes, chosen by index ← **this one**

The kernel ships N pictures; a window names one. `OSAPI_WM_CURSOR` takes
`BX` = window, `AL` = index. 64 bytes of `.text` per shape.

This is the Macintosh System 1 model (arrow, I-beam, crosshair, watch), and
three things fall out of it rather than having to be built:

- **No lifetime problem.** The shape is a byte in a record the kernel owns and
  zeroes on destroy. A package that dies holding a crosshair cannot leave one
  behind.
- **No staging.** `cur_put_mono` reads its table through DS with DS =
  KERNEL_SEG; a pointer into a package's segment would render the package's
  own image, which is §31.9's Control Panel page-name trap exactly.
- **No run-time validation.** §7.1's three assertions — the widest bit set,
  the last row that sets one, and *every black bit is also a white bit* —
  stay at assembly time, per shape, through the same `CUR_ROW` macro. That
  third one is not cosmetic: `cur_draw` fuses both colours into one masked
  store, so a black bit outside its white row is simply not drawn on VGA.

### Option 2 — the app supplies its own bitmap

Everything Option 1 avoids, plus per-window storage (48 bytes of table and 2
of hot spot, ×12 windows = 600 bytes of `.bss` against an image rung with 115
left) or a single shared slot that two apps then fight over.

**Not refused on principle — refused on order.** The published slot takes an
index today; a variant that takes a bitmap is a *new* number appended later
(§20.8 rule 4 prefers appending to re-contracting), and by then the run-time
validator will have one caller to justify it rather than none.

---

## 3. Why the cell may not grow, and what that costs the crosshair

`CUR_GW` (8), `CUR_GH` (12) and `CUR_SPAN` (2) bound **every** walk in
`mouse.inc`, and SPEC.md §7.1 is the record of what shrinking them from 16x16
bought: a quarter of the rows, a third of the bytes per row, the cell's third
framebuffer byte retired outright, and the pair measured **17.82 → 5.41** PIT
counts on Hercules.

A 16x16 cell would give most of that back — three bytes a row instead of two,
sixteen rows instead of twelve, the retired branch reinstated, and
`CUR_SAVE_SZ` 96 → 192 with its twin 24 → 48 (+120 bytes of `.bss`, another
rung). **And it would be paid on every gfx lock hold in the machine, by every
application**, for a crosshair over one window. That is the wrong shape of
trade and it is the single most important constraint here.

So the crosshair is 8x11 inside the arrow's own cell — a 1px cross dilated by
a pixel, which is the same outline-and-body construction the arrow uses and
the only one that reads on both a white desktop and a black sky:

```
white (outline)        black (the body)
 ..***...               ...#....
 ..***...               ...#....
 ..***...               ...#....
 ..***...               ...#....
 ********               ........
 ********               ###.####     <- the hot spot's own pixel is clear,
 ********               ........        so the point being aimed at is not
 ..***...               ...#....        covered by the thing aiming at it
 ..***...               ...#....
 ..***...               ...#....
 ..***...               ...#....
```

Hot spot (3,5). It is smaller than Missile's own 17px overlay; that is the
price of not charging the whole machine for it.

**The gap and the vertical stroke must be the same column, and nothing can
assert that.** It shipped a column apart first — the vertical stroke on 2 and
the gap on 3 — and every test above passed, because the tests ask where the
crosshair is worn and whether anything is left behind, and both were right.
It reads as a broken cross, which is a fact about what the picture *means*
rather than about the bits, and the only instrument for it is looking at a
zoomed capture. The `CUR_ROW` assertions catch a shape that is too wide, too
tall or has a black bit outside its white row; there is no assertion for
"this shape is aiming at the wrong pixel."

---

## 4. The hot spot: clamped, not clipped

`cur_geom` and `cur_rect` are, in the module's own words, "the ONLY two places
a position becomes an ADDRESS". They now subtract the hot spot and **clamp the
result at the display origin** — four instructions each, and it can never
address outside the framebuffer.

**What the correct fix would be, so that nobody re-derives it:** left and top
*clipping* — a `cur_b0ok` beside `cur_b1ok`, a table start offset for the rows
skipped off the top, `sar` where the column shift is `shr`, and both threaded
through `cur_mvcols`/`cur_mvrow`/`cur_mvbg`, the four per-column answers
§7.1.2 built to write every byte exactly once and whose cases double when
either cell can lose its first column.

**What the clamp costs instead:** the picture stops moving in the outermost
`hx` columns and `hy` rows. The crosshair's centre drifts up to 3 pixels from
the pointer against the left edge of the screen and 5 against the top. On the
two mono adapters it is unreachable through Missile at all, because `WF_SNAP`
keeps its content origin on a multiple of 8 — measured, the window's content
starts at x=7 there however hard it is dragged left. On VGA it is reachable
and was driven: content at x=0, the pointer walked over x=0..6 repeatedly,
**0 differing pixels**.

Three consequences, each of which broke first:

- **`[cur_cellx]`/`[cur_celly]` are banked by `cur_geom`**, because the cell's
  top-left is no longer the pointer's position and `cur_move_mono` asks about
  it three times. §7.1's rule is unchanged: two derivations of a save-under's
  geometry are two chances to disagree by a byte, and a byte is a smear.
- **`cur_lazyck` subtracts the hot spot and deliberately does not clamp.**
  Unclamped is a *superset* of where the cell is, so the error can only be a
  redundant hide; forgetting it runs the other way and §7.1.4's smear is
  permanent. It is the one site here that fails silently and weeks later.
- **The shape changes only while the cursor is off the glass.**
  `cur_shape_set` calls `cur_unlazy` and adds no hide/show pair of its own —
  `gfx_lock` promised the hide and `gfx_unlock` already owes the show. That
  hands `cur_move_mono` its precondition: within one move the hot spot is
  fixed, so pass 1 and pass 2 may subtract cell tops from each other.

---

## 5. Where the question is asked

`cur_shape_pass`, from `ui_task`'s `.yield` tail, beside `toast_pass`. Two
reasons, and the second is binding.

1. `wm_hit` is a z-order walk with an 8-bit `mul` per candidate. The mouse ISR
   runs per packet; putting it there gives back a share of what §7.1 spent
   three optimisations winning.
2. **The UI task is the only mutator of `wm_zord` and the window rects**, so
   `wm_hit` answers from it with no gfx lock and no race — which is exactly
   what the ISR could not do. A frame of latency on a cosmetic change is
   invisible; a torn z-order walk is not.

It is gated on the pointer having moved (`[cur_shx]`/`[cur_shy]`) **or**
`[cur_shchk]`, which covers the case a position compare cannot see: the
pointer standing still while the window under it is raised, hidden, closed,
dragged or grown away. Two stores set that flag — `wm_raise` and
`wm_paint_dmg` — because every one of those five events pays a repaint
through one of them.

**And the lock is taken only when the shape actually CHANGES**, which is a
compare in `cur_shape_pass` and not merely the one inside `cur_shape_set`. §7
below is how that was found.

Chrome stays the arrow: `wm_hit` answers `AL != 0` for the title bar and its
boxes, and those are the kernel's to draw and to click.

---

## 6. What was verified, and how

MartyPC, a cycle-accurate 4.77MHz 8088, on **CGA, Hercules and VGA mode 12h**.
Two suites.

**The feature.** The crosshair region is read out of the window record and
probed one pixel either side of all four edges — not eyeballed off a
screenshot — against the kernel's own `[cur_shape]` byte:

| | CGA | Hercules | VGA |
|---|---|---|---|
| content middle / top-left / bottom-right | crosshair | crosshair | crosshair |
| 1px left / right / above / below, menu bar | arrow | arrow | arrow |
| 19 boundary crossings, then home | **0 px** | **0 px** | **0 px** |
| left-edge clamp walk | n/a (`WF_SNAP`) | n/a (`WF_SNAP`) | **0 px** |

**The no-regression control**, which is the claim that a window that never
asks pays nothing: the same 18-position scripted session driven through a
**reference kernel built from `HEAD`** and through this one, framebuffers
compared pixel for pixel — every byte phase, the right screen edge where the
cell's second byte is clipped away, the bottom edge where it is cut short,
both left corners:

> **0 differing pixels across 18 positions, on all three adapters.**

Both suites subtract what moves on its own. The menu bar clock is excluded by
name; the game's own live content is excluded by a **self-calibrating mask** —
five frames captured with the pointer parked, and any pixel that moved between
them does not count. On VGA that mask is 402 subpixels of banner text, and
without it the walk "failed" with 518.

---

## 7. Six traps, and what they say

Three were already written down in this tree and it re-sprang all three:

- **`wm_cursor` must preserve FLAGS.** A package's entry proc returns CF to
  the loader, and `cmp al, CUR_NSHAPE` leaves CF *set* for every legal shape.
  Asking for a crosshair aborted the launch — the file row just sat there
  selected, with no error anywhere. This is `wm_snap`'s bug, verbatim, whose
  own comment says "without this, asking to be snapped aborted the launch".
- **A step inserted in `ui_task`'s deferred ladder is a step the jump above it
  has to be pointed at.** `.chk_zones`' `je .chk_toast` skipped straight past
  the new `.chk_curs`, and `[desk_zdirty]` is almost always 0, so
  `cur_shape_pass` ran essentially never. The feature *half-worked*: the shape
  was recorded, the pointer never changed. CLAUDE.md states this rule and
  §22.8 states it again.
- **Taking the gfx lock is the expensive thing, not the poll.** The first
  `cur_shape_pass` locked whenever the pointer *moved*, not whenever the shape
  *changed* — a `gfx_lock`/`gfx_unlock` pair per mouse move, which is
  PERFORMANCE.md Part 9 Set 4's 21.8%, charged to every machine. §7.1.3 makes
  exactly this point about `fm_drag`'s `.wait`.

The other three were in the **apparatus**, which is this tree's most reliable
finding about itself:

- `settle()` returns during a floppy load, because a 13KB package read at
  MartyPC's real 300 RPM leaves the screen perfectly still — which is what
  `settle` is looking for. It read as "the double-click did not launch".
- `m.sym()` resolves against the **current tree's** kernel. Reading `vid_w`
  out of a *reference* image with those symbols returns garbage, so the two
  sides of the A/B visited **different points** — and every reported
  difference was at a coordinate derived that way, none at a literal one.
  That asymmetry is what identified it.
- A framebuffer read of a *running* machine can land inside a lock hold, with
  the pointer legitimately off the glass. It shows as the whole cursor cell
  differing, which looks exactly like a smear.

Reference-versus-reference — the apparatus against itself — is what settled
each of the last two, and it is worth running before believing any A/B here.

---

## 8. What is deliberately not done

- **No second bitmap from a package** (§2 above). Append a slot when there is
  a caller.
- **No I-beam or watch.** Both are 64 bytes of `.text` and no code at all, and
  both want a consumer first: an I-beam wants Note Pad's and ArtfulType's text
  areas, which is a *sub-window* question this cannot answer — `wm_hit`'s
  granularity is the window, and a text app wants the beam over its text and
  the arrow over its scroll bar. That is the next real design question here.
- **Nothing inside an fsx bracket** (§53). The held lock keeps the pointer off
  by construction, and an exclusive app draws its own — as Missile still does.
- **The clamp is not clipping** (§4). Written down rather than hidden.
- **`docs/KERNEL-MEMORY.md` is not re-blessed.** The tree already carried
  unblessed drift at `HEAD` (+38 `.text`, +52 `.bss`, +110 `.cold`), and
  blessing now would absorb somebody else's numbers into this change's.
