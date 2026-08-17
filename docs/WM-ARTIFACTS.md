# Two window-manager artifacts, reproduced

> **Re-measured after this report was written, on the branch that carries it
> plus SPEC.md §11.96.13.1.** Both were confirmed still valid — §0's warnings
> are exactly why an earlier attempt on another branch had reported neither.
>
> **§1 is FIXED (SPEC.md §11.97.2), and the report's own diagnosis is what
> found it.** "Look at how a repaint composes a lower window's shadow under a
> window drawn after it, not at `wm_show`" is right: `wm_chrome_clip`
> subtracted each occluder by its **occupied box**, which holds the two corners
> a drop shadow's L never paints, so the lower window's shadow lost a pixel
> that nothing above would ever write. It subtracts by the FRAME now —
> `wm_clip_occlf`, the entry point §11.96.15 added for this exact asymmetry one
> subtraction site later. **0 bytes**, and part 1 reads 0 px with every control
> still 0.
>
> **§2 is FIXED too (SPEC.md §39.16.2.1)**, and its numbers had moved first
> because the GEOMETRY moved.
> `ui_drag_phase` now snaps `dy` to a multiple of 8 (SPEC.md §11.96.13.1), so
> the drags land on different rows than when this was written: the window that
> went to `(687,51)` goes to `(687,44)`, and the residue tracks it. Drag back
> now leaves **3,844 px** where it left 5,058, in a band of **24 rows** where
> it was 31 — and 155 rows of window at y = 44 ends at 199 against 206 at
> y = 51, which is those 7 rows exactly. The drag *out* read **0** even before
> the fix, for the reason §2.1 gives. It is 0 on both cards both ways round
> now, with both controls still 0.
>
> **Everything below this box is the report as it was written**, and it is
> left that way: it is the evidence, and the diagnosis in §1 is what found the
> first fix. §2.1 is the only part added after the fact.

**Reproduced, diagnosed, and both now fixed** — the box above carries the
outcome and the two SPEC sections. Both were found while testing SPEC.md §65's
Calculator and neither is the Calculator's — one reproduces with `hello`, the
other with a Disk window, and there is no package in that one at all.
`tests/wmartifact.py` is the reproducer and every number below comes out of it:

```
make marty && make
python3 tests/wmartifact.py                              # both parts
python3 tests/wmartifact.py --part shadow --machine os8088_5150_herc_gla
python3 tests/wmartifact.py --part seam                  # the dual machine
```

## 0. Why a reproduction fails — read this before concluding "cannot repro"

**Neither artifact is visible in a screenshot.** The glass is *stale*, not
corrupt: what is on screen is a coherent picture that simply is not the
picture the window manager would draw if asked again. So the only question
that finds either of them is

> is what is on the glass **right now** what a full repaint would draw?

and the answer is a pixel count against a forced `wm_paint_all`. Four things
have to be right for that count to mean anything, and each one of them cost
this session real time:

1. **Force the repaint from the GUEST.** Poke `[cp_dirty] = 1`; `ui_task`'s
   step 3 drains it with `gfx_lock` / `wm_paint_all` / `gfx_unlock` on its own
   stack. Do **not** park the CPU on a stub and call those from outside —
   `park` is a CPU reset and the frame it borrows belongs to whichever task
   the pause caught, so catching the UI task inside a lock hold corrupts the
   guest *three assertions later* (measured: a window rect reading 2056x2056
   and a keyboard that had stopped arriving). docs/MARTYPC-DEBUG.md.
2. **Park the mouse pointer** somewhere the artifact cannot reach. The arrow
   is drawn into the framebuffer (SPEC.md §7.1), so a forced repaint takes it
   off and puts it back, and its 8x12 cell legitimately differs.
3. **Exclude the clock.** The forced repaint takes real wall time, so a run
   can straddle the menu bar's once-a-minute change: ~42 pixels at the top
   right, nothing to do with any window.
4. **Settle first.** A diff of a screen still being drawn measures the
   drawing.

And for part 1 there is a fifth, which is the likeliest single reason a
reproduction comes back clean: **the artifact needs two windows whose frames
were clamped to the same shadow row, with the upper one's left edge strictly
inside the lower one's shadow span.** Open the wrong package and there is
nothing to see — measured below, and it is not a subtle difference once the
rects are printed.

## 1. One pixel at a window's `(W_X, W_Y + W_H)`

### What it is

Open `HELLO.O88` from a Disk window on B:\APPS on a CGA machine and the glass
disagrees with a forced repaint at exactly one pixel:

```
   CONTROL two Disk windows, no package      0 px
   HELLO.O88 (row 4) opened at (199,85) 240x90
      slot 0 rect (103,20) 320x155   shadow row 175, columns 104..423
      slot 1 rect (199,85) 240x90    shadow row 175, columns 200..439
   a package window is open                  1 px   x 199..199 y 175..175
      (199,175) 0->1
      (199,175) is on the shadow of window(s) [0]: the glass has black there
                and the repaint white
```

### What the rects say

Both windows are clamped by `wm_fit` to end one row above the dock, so **they
share shadow row 175**. `wm_draw_shadow` draws the bottom edge from `x+1`
(SPEC.md §11.95.2 — the corner pixel belongs to nobody unless the window is
flush against the screen's left edge), so:

* the Disk window's shadow covers columns **104..423** of row 175;
* `hello`'s covers **200..439**;
* column **199** is therefore the *last* pixel of the lower window's shadow
  that the upper window's own shadow does not overwrite.

The incremental path leaves it **black** — the Disk window's shadow, running
unbroken under `hello`. `wm_paint_all` leaves it **white**, i.e. it draws that
shadow with a one-pixel gap immediately left of the upper window's shadow.

**So the incremental path may be the one that is right**, and the reference is
the suspect. That is worth saying plainly because it decides where to look:
not at `wm_show`, but at how `wm_paint_all` composes a lower window's shadow
under a window drawn after it. SPEC.md §11.91.1's dither "subtracts each
window's FRAME and not its occupied box", and `wm_draw_shadow`'s own header
says the shadow is "dithered over and put back by the window's own redraw" —
that pair is where a single column can go missing.

### What it is NOT

* **Not the package's.** `hello` draws one greeting; the pixel is outside
  every content box, and the `gfx_*` primitives take content coordinates.
* **Not present without a package** — two Disk windows measure 0 px — but
  that is about the geometry, not about packages: what those two Disk windows
  lack is an upper frame whose x falls strictly inside a lower shadow span.
* **Not persistent across a drag.** Drag `hello` and the pixel does not come
  back (measured 0 px at the new position), because `wm_paint_dmg` agrees
  with `wm_paint_all`. Closing the window leaves 0 px.
* **Not reproduced by every package.** `PIANO.O88` opens at **(103,20)
  320x155** — the Disk window's own rect — so its shadow span is *identical*
  to the one underneath and there is no leftover column to disagree about.
  It measures 0 px. This is the discriminator, and it is why "I opened a
  package and saw nothing" is a result about which package.

### Adapter

Measured on `os8088_5150_cga_gla`. The same run is worth taking on
`os8088_5150_herc_gla` and `os8088_xt_vga` (`--machine`); the geometry that
produces it is the dock clamp, which every adapter has at its own row.

## 2. A drag across an extended desktop's seam leaves the secondary dirty

### What it is

On `os8088_5150_both_gla`, extend the desktop (Control Panel ▸ Display ▸
extend right) and drag a **Disk window** — the kernel's own, no package
anywhere — across the seam:

```
   the seam is at x = 640; the virtual desktop is 1360 wide;
   the PRIMARY is cga and the secondary is herc

   CONTROL extended, nothing dragged (herc)    0 px
   CONTROL extended, nothing dragged (cga)     0 px

   dragging the Disk window at (103,20) 320x155 across the seam
   it is now at (687,51) 320x155 - origin past the seam
   after the drag (herc)                    1111 px   x 0..364 y 82..179
   after the drag (cga)                        0 px

   dragged back to (359,51)
   after dragging back (herc)               5058 px   x 40..367 y 156..186
   after dragging back (cga)                   0 px
```

### The one thing to get right

**The residue is on the SECONDARY display, both ways round.** Here the primary
is the CGA, so it lands on the Hercules; in `tests/dispcalcx.py`, which sets
the Hercules primary first, the same operation leaves it on the CGA (220 px
for a Calculator window, **566 px for a Disk window** doing the same thing).
A run that diffs only the card it expects — or only the card the window ended
up on — sees a clean screen and reports no reproduction. `wmartifact.py`
prints which card is primary for exactly this reason.

The counts are large (thousands of pixels, whole bands), so this is not a
rounding question: rows the drag vacated are not being repaired on the display
that is not the primary. It reproduces on the drag *out* and again on the drag
*back*, and the control — extended, nothing dragged — is 0 px on both cards,
so it is the drag and not the extension.

### What it is NOT

* **Not the package's**, and this one needs no argument: the window dragged
  here is a Disk window. Measured comparatively as well — over the same seam a
  Calculator window left 220 px where a Disk window left 566.
* **Not `wm_strad_fit` refusing to re-fit.** The frame's own geometry is
  correct at every step (`tests/dispcalcx.py` asserts it: the window ends on a
  row the display it is on actually has, and the package re-derives its layout
  through `OSAPI_WM_ONRESIZE`). What is wrong is the *repair of the ground it
  left*.

### 2.1 What the re-measurement points at: one band, one display's

The residue is the **bottom 24 rows of the rect the window vacated on the
secondary**, and the arithmetic closes to four pixels:

```
   dragged back from (687,44) to (359,44); the window is 320x155
   its old rect on the Hercules is virtual x 687..1006, y 44..198
   the CGA primary's [vid_dock_y0] is 176
   so virtual rows 176..198 are the part above nothing repaired
   measured band  x 47..367  y 156..179 local = virtual 687..1007, 176..199
   320 columns x 24 rows = 7,680, half of them lit in a 50% dither = 3,840
   measured                                                          3,844
```

`wm_dmg_band` (§39.16.2) is per display and correct: display 0 gets
`MBAR_H .. [vid_dock_y0]-1` and any other display gets its own full height.
**But `wm_paint_dmg` asks it about ONE POINT** — the damage rect's top-left —
and then applies that answer to the whole rect. A drag across the seam is
precisely the case where the damage spans two displays, and here `x1 = 359` is
on the CGA, so the **primary's** ceiling of 175 is applied to the Hercules
part of the damage as well. Every vacated row below it on the secondary is
never dithered.

That also explains the asymmetry the re-measurement found. On the way **out**
the window arrives on the secondary and covers those rows itself, and what is
left uncovered had not changed, so nothing is owed — 0 px. On the way **back**
it leaves, and the rows it vacates hold its own pixels with nothing to repair
them.

**FIXED** — SPEC.md §39.16.2.1. `wm_dmg_bands` replaces `wm_dmg_band` and
**takes a rect**, emitting one clip seed per display (each display's own band
intersected with the damage) and answering their bounding box; `wm_dmg_gray`
builds its region on those seeds. The split it needed already existed —
`gfx_disp_run` intersects and translates per display, and a fragment on no
display is drawn by nobody — so this is still **one region build and one
`gfx_fill_gray`**, not one of each per display.

`.text` **+199 bytes**, no rung crossed. Both legs of part 2 read **0 px** on
both cards with both controls still 0, and part 1 is unmoved.

The contract is the part meant to outlast the bug: there is no point-shaped
entry point left, so a future site cannot get a per-display property wrong by
asking about a corner. **A one-display machine can never show this class of
defect**, which is why the shape has to carry it rather than a test.

## 3. Where to start

Both are about the same subsystem — who repaints the ground a window is no
longer on — and both have the same shape: the incremental path and
`wm_paint_all` disagree, and the disagreement is invisible until something
forces the second one. SPEC.md §11.91 (`wm_paint_dmg`, the damage rect, the
marking pass), §11.91.1's dither subtraction and §11.95.2's flush-left shadow
rule are the three pieces of prose that cover the first; §39.16's virtual
desktop and the per-display drawing context (§39.12/§39.14) cover the second.

`tests/wmartifact.py` is written to keep answering while a fix is developed:
every step prints its own pixel count, so a change can be judged by whether
those counts go to 0 without any of the controls moving off 0.
