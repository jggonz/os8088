# Batch A — the straddle rows: placement changed, the tests never caught up

**Read `docs/HANDOFF-TESTS.md` first** — environment, the single-emulator rule, the
commit trap, and the validation protocol are all there and are not repeated here.

**Your rows:** `dispmode` `dispmodex` `dispbrow` `dispstrad` `dispnp`

**The hypothesis you are testing:** how a window snaps and is placed across a
display seam was modified, and these rows were not brought along. Some of them are
therefore *stale tests*; at least two look like *real defects*. Your job is to say
which is which, row by row, with evidence.

All five need the two-card machine. `xt-multimon` (86Box) is the two-card XT — a CGA
and a Hercules, a monitor each — and `os8088_xt_vga_mda` is MartyPage's two-card XT
**with a VGA in it**, the only machine here where an extended desktop has a COLOUR
display. `tests/dualcheck.py` is the gate that says the second card came up at all —
**run it first**; if it fails, nothing else in this batch means anything.

---

## A1. `dispmode` + `dispmodex` — PROVEN test-side. Start here; no emulator needed.

`dispmode` measures: Single vs Extend, where the second display sits, and whether that
survives a reboot.
`dispmodex` measures: which display Missile Command asks about for Mode X (SPEC.md
§39.18.1).

**This one is already solved and only needs typing.** The record layout moved and the
two test files did not:

```
kernel/vidsel.inc:1202   VID_CTX_W    equ 19
kernel/vidsel.inc:1205   VID_CTX_VX   equ VID_CTX_W*2      = 38
kernel/vidsel.inc:1206   VID_CTX_VY   equ VID_CTX_W*2+2    = 40
kernel/vidsel.inc:1207   VID_CTX_KIND equ VID_CTX_W*2+4    = 42

tests/dispmode.py:103    "kind": r[40], "vx": u16(r, 36), "vy": u16(r, 38)
tests/dispmodex.py:130   ctx[40]
```

Both files are still on the `VID_CTX_W = 18` layout (36/38/40). That is why the failure
prints a **segment** where an x-coordinate belongs:

```
dispmode: FAIL: 'right' put display 1 at (45056,640), wanted (640, 20)
dispmode: FAIL: after the swap display 0 is kind 0 at (45056,0)
dispmode: FAIL: 'right' survived the swap as x=47104, wanted 720
```

`45056` is `0xB000`. `47104` is `0xB800`. Those are the **MDA and CGA framebuffer
segments** — the test is reading the adapter's base address out of the field where it
expects a coordinate, because it is two words short.

**Do not hard-code 38/40/42 either.** `tools/os88geom.py` exists to mirror kernel
record layouts host-side; these two files are the readers a nine-script sweep missed.
Make them read the offsets from there, so the next `VID_CTX_W` change moves one number.
While you are in `os88geom.py`, note that `WF_HIBITS` at line ~211 is hand-copied and
sits **outside** `_MIRROR` — the same class of bug, not yet fired.

**The kernel is innocent for A1.** Fix the tests.

---

## A2. `dispstrad` + `dispbrow` — very likely ONE real defect

`dispstrad` measures: a window dragged across the seam gives back the rows on only ONE
display.
`dispbrow` measures: the field's browser report — a drag that does not move it, and a
width cut on a card wide enough to hold it.

```
dispstrad: FAIL: the width moved 322 -> 320: only the axis the displays
                 OVERLAP in may be narrowed (SPEC.md 39.16.3)
dispbrow:  FAIL: the window came out 496 wide on a card that is 640 wide,
                 from 498 - nothing may cut a width that FITS
                 (the field's "chopped thin in the X direction")
```

**Both are a 2-pixel cut on an axis where the window fits.** 322→320 and 498→496. That
is the same violation stated twice, and §39.16.3 is the invariant: *only the axis the
displays overlap in may be narrowed*. Expect one fix to clear both, and be suspicious
if you find two.

`dispbrow`'s parenthetical — "the field's *chopped thin in the X direction*" — means
this was **reported off real hardware**, so treat it as a live user-visible defect and
not a test artefact. `docs/FIELD-NOTES.md` is where field reports live.

Start by finding what narrows a width by exactly 2. A border inset, a frame allowance
counted twice, or a snap that rounds to a cell and then subtracts one on each side, are
the usual shapes. `[vid_w]` on an extended desktop is the **SUM** of both displays, not
one screen — a test written against "the screen" is wrong on two cards, and SPEC.md
§11.95.2.1 and §39.16.2 are the sections that already had to learn this.

---

## A3. `dispnp` — no diagnostic, needs a run

Measures: does a WIDE straddling Note Pad letter its whole row? (SPEC.md §27.2.1)

It failed at 65.8 s with **no assertion message**, so nothing can be said from the log.
Run it directly and capture the output before theorising. If it is the same width-cut
family as A2, fix A2 first and re-run this one — it may go green for free.

---

## Standing constraints for this batch

* **Three adapters, one binary.** `SCREEN_W`/`SCREEN_H`/`ROW_BYTES` are VGA *reference*
  values, **not the truth**. The live screen is `[vid_w]`/`[vid_h]`/`[vid_stride]`, and
  on an extended desktop `[vid_w]` is the union. Anything that clips, centres or anchors
  to a screen edge must read those.
* **Look at any drawing or greying change on a 1bpp adapter before calling it done**
  (§39.4, §47) — grey rounds to black there, so a disabled glyph is a checkerboard.
* A **visible redraw** and a **double-draw flash** are invisible in an emulator. If your
  fix changes what is drawn rather than only where, say how you checked.
* `SPEC.md` is the binding contract and is updated **before** the change, not after.
