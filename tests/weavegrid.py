#!/usr/bin/env python3
"""The grid, against the model and against itself (WEAVE-SPEC 12.3, 12.4).

    make weavedisk && python3 tests/weavegrid.py
    python3 tests/weavegrid.py --machine os8088_5150_herc_gla
    python3 tests/weavegrid.py --png shots/     # ...and LOOK at it

WHAT IT IS FOR. `weavevm` proves both interpreters in a boot sector and can
reach nothing else - it has no cell store in a running app, no band on a
screen and no slice against a real tick. `weavesession` drives a FORM and
never touches a grid. This row is the `<grid>`'s own, and WEAVE-SPEC 13.1
names it as wave 4's gate: **recalc vs the model, and the tpdraw identity.**

THREE ASSERTIONS, and each one fails differently:

  1. THE PICTURE IS THE MODEL'S. Every visible band is read off the glass and
     compared, character by character, with `weavesim`'s own band() - which is
     WEAVE-SPEC 6.9.1's pinned layout: the 4-cell gutter, the fixed 8-cell
     column, the header letters, a label left and a number right. A defect in
     the FX VM, in the display conversion (5.2.1), in the store, in the
     justification or in the scroll origin lands here.

  2. THE DAMAGE IS THE MODEL'S. The set of bands whose PIXELS changed across
     an edit must equal the set whose model band() text changed. This is
     WEAVE-SPEC 5.5.1's per-row damage said as a fact about the glass: a
     runtime that repaints the whole grid on every edit passes assertion 1 and
     fails this one, and that is exactly the regression PERFORMANCE.md Part 5
     exists to stop (a 20-row page is 291 ms against one row's 14.5).

  3. THE INCREMENTAL PICTURE IS THE FULL ONE. tests/tpdraw.py's identity, for
     the grid: the pixels after an incremental edit must be IDENTICAL to the
     pixels after a full re-compose of the same state. The re-compose is
     forced with the arrow keys - a selection move that scrolls repaints every
     band (6.9.1) - so the state is reached twice by two different paths and
     the pictures are diffed. WEAVE-SPEC 6.9.1's XOR fast path is the thing
     this catches: XOR-ing an inverted cell restores it and a plain one
     inverts it, which is true right up until the band composer and the XOR
     disagree about which cells the selection covers.

HOW TEXT IS READ, WHICH IS BY CONSISTENCY AND NEVER BY FAITH (12.3.2). The
8x8 face comes from the machine's BIOS at boot (kernel/font.inc) and is not in
this tree, so there is no font to compare against: glyphs are LEARNED from
rows whose text is already known - the menu bar, the card's static label, the
two button labels, the grid's own column headers and its row numbers, none of
which comes out of the cell store - and a character the learning rows never
contained reads back as '?' and is skipped. The assertion is 12.3.2's: every
LEARNED glyph must match the model's string, and no learned glyph may appear
where the model says another one does. That catches `22.75` against `22.05` on
the 7 alone and never fails for a font this tree cannot see.

WHAT IS DRIVEN. Two edits, one per path, because the two reach the store
through completely different code:

  - `Cider +1` runs SHEET's own `bump()`, which is `g.setCell(4,2,
    g.cell(4,2)+1)` - the SCRIPT path, through the ring, a slice, CALLM,
    5.6's store and 5.5's sliced recalculation;
  - then a formula is TYPED into an empty cell - the FORMULA BAR path, through
    os88line, 6.9.3's classification and 6.9.2's compiler into a 5.6 kind-6
    pool slot.

NAVIGATION IS weavesmoke's, IMPORTED AND NOT COPIED, and what flakes is its
documented case: a double-click whose two presses straddle the kernel's
9-tick window is seen as two first clicks, and on a loaded host that happens.
Retry the navigation, never the assertions.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                           # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import weavesmoke                                           # noqa: E402
from rczex_ocr import Screen                                # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISK = weavesmoke.DISK
BUNDLE = "SHEET.WAB"
SRC = "apps/weave/demos/sheet.wml"

MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]

# WEAVE-SPEC 6.9.1's geometry, mirrored - and the mirror is checked, because
# the model prints the same numbers and assertion 1 compares whole bands.
WG_GUT, WG_COLW, WG_BAR, WG_HDR = 4, 8, 2, 1
WG_CHROME = WG_BAR + WG_HDR

# The cell the typed formula goes in, 1-based, and what is typed into it. C3
# is EMPTY in SHEET.WFX, so the bar loads blank and the keystrokes append
# cleanly - clicking into a cell that already has a source would need a
# backspace count, which is a fact about os88line rather than about the grid.
TYPE_R, TYPE_C, TYPE_SRC = 3, 3, "=B3*2"


# ---------------------------------------------------------------------------
# THE ORACLE
# ---------------------------------------------------------------------------

def _sim(adapter, events):
    """`weavesim --run` over an event script; answers its stdout."""
    ev = os.path.join(ROOT, "build", "weavegrid.events")
    open(ev, "w").write(events)
    r = subprocess.run(["python3", "tools/weavesim.py", "--run",
                        "build/" + BUNDLE, "--events", ev, "--src", SRC,
                        "--adapter", adapter],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("weavesim --run: %s"
                           % (r.stderr or r.stdout)[-400:])
    return r.stdout


def _cardrows(adapter, events=""):
    """The CARD as the model draws it when `events` are spent - one string per
    8-px row, borders stripped.

    THE MODEL ANSWERS AND THIS FILE WRITES DOWN NOTHING. A test carrying its
    own expected strings has to be edited when the demo changes; one that asks
    the oracle fails loudly when the demo and the runtime stop agreeing, which
    is the only thing worth failing for."""
    ev = os.path.join(ROOT, "build", "weavegrid.events")
    open(ev, "w").write(events)
    r = subprocess.run(["python3", "tools/weavesim.py", "--run",
                        "build/" + BUNDLE, "--events", ev, "--src", SRC,
                        "--adapter", adapter, "--render-after"],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("weavesim --render-after: %s"
                           % (r.stderr or r.stdout)[-400:])
    return [ln[1:-1] for ln in r.stdout.splitlines()
            if len(ln) > 2 and ln[0] in "|:" and ln[-1] in "|:"]


def _grid_geom(rows):
    """(the card row the formula bar sits on, the visible band count), out of
    the model's own picture - never a constant, because the walk is what puts
    the grid there and 7.1.1 forbids hard-coding a grid.

    The bar is the only row that starts with `[` (6.9.1 draws the field that
    way), and a data band is a row carrying a right-justified row number in
    the gutter."""
    for i, ln in enumerate(rows):
        if ln.startswith("["):
            vr = 0
            while i + WG_CHROME + vr < len(rows) \
                    and re.match(r"^ {0,2}\d{1,3} ",
                                 rows[i + WG_CHROME + vr]):
                vr += 1
            return i, vr
    raise RuntimeError("weavesim --render: no formula bar on the card")


# ---------------------------------------------------------------------------
# READING THE GLASS
# ---------------------------------------------------------------------------

class Glass(Screen):
    """rczex_ocr's reader over MartyPC's one-byte-per-pixel frame.

    `vram` hands back 1 = LIT, and a window's content is the kernel's white
    fill, so INK is 0 - the inverse of the screendump reader's assumption.
    Overriding one method is the whole adaptation; the learning, the reverse-
    video fallback and the '?' are that file's, which is the point of using
    it (SPEC.md 74's own instrument, WEAVE-SPEC 12.3.2)."""

    def __init__(self, rows, w, h, x0, y0):
        Screen.__init__(self, w, h, None, x0, y0)
        self.rows = rows

    def dark(self, x, y):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return 0
        return 0 if self.rows[y][x] else 1


def _content(m, S, slot, vidw):
    """The window's content box in pixels - weavesmoke's own derivation."""
    x, y, w, h, seg, flags = weavesmoke._win(m, S, slot)
    if weavesmoke._flush(x, w, flags, vidw):
        cx, cw = x, w
    else:
        cx, cw = x + 1, w - 2
    cy = y + os88geom.TITLE_H
    return cx, cy, cw, y + h - 1 - cy


def _park(m, mo, vidw, vidh):
    """Move the POINTER out of the grid before a capture.

    The cursor is drawn into the framebuffer, so a pointer resting on a band
    makes that band's pixels differ from the same band with the pointer
    elsewhere - and assertions 2 and 3 are pixel comparisons of bands. It cost
    this row its first run: the damage set came back one band too big, and the
    extra band was the one the mouse had stopped over. Parking is cheaper and
    far clearer than stripping the cursor out of every read."""
    mo.to(vidw - 4, vidh - 4)
    os88marty.settle(m)


def _band_pixels(rows, cx, cy, row, cells):
    """One band's 8 pixel rows, as a tuple - the unit assertions 2 and 3
    compare."""
    y0 = cy + row * 8
    return tuple(tuple(rows[y0 + k][cx:cx + cells * 8]) for k in range(8))


def _learn(g, model, gr0, vr):
    """Harvest glyphs from rows whose text this file already knows.

    NONE OF THEM COMES OUT OF THE CELL STORE, which is what keeps the reading
    honest: the card's static label and the two button labels are painted from
    ATOMS before a line of bytecode has run, and the grid's column letters and
    row numbers are the runtime's own arithmetic over `cols` and `rows`. A
    character the learning rows never contained reads '?' and is skipped,
    which is 12.3.2's rule and not a weakening of it."""
    g.learn(0, "Picnic Budget")                 # the card's static label
    g.learn(gr0 + WG_BAR, model[gr0 + WG_BAR])  # the column headers
    for k in range(vr):                         # ...and every gutter
        g.learn(gr0 + WG_CHROME + k,
                model[gr0 + WG_CHROME + k][:WG_GUT])


def _read_bands(g, gr0, vr, ncells):
    return [g.read(gr0 + WG_BAR, ncells)] + \
           [g.read(gr0 + WG_CHROME + k, ncells) for k in range(vr)]


def _model_bands(model, gr0, vr):
    return [model[gr0 + WG_BAR]] + \
           [model[gr0 + WG_CHROME + k] for k in range(vr)]


def _agree(got, want):
    """WEAVE-SPEC 12.3.2's rule, as a predicate: every LEARNED glyph must
    match the model's string, and no learned glyph may appear where the model
    says another one does.  Answers (ok, the first disagreement)."""
    for i, ch in enumerate(got):
        if ch == "?":
            continue                    # a glyph the learning rows never had
        w = want[i] if i < len(want) else " "
        if ch != w:
            return False, "col %d: read %r, model %r" % (i, ch, w)
    return True, ""


# ---------------------------------------------------------------------------
# THE SESSION
# ---------------------------------------------------------------------------

def session(machine, card, vidw, vidh, png_dir=None):
    S = os88sym.linear
    weavesmoke.BUNDLE = BUNDLE
    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=machine) as m:
        try:
            _drive(machine, card, vidw, S, m, png_dir)
        except os88marty.MartyError as e:
            check(False, "%s: the scripted session broke off" % machine,
                  "the machine booted, so this is the harness losing its grip "
                  "on it rather than the tree being wrong",
                  got=str(e)[:200], want="a driven session")


def _drive(machine, card, vidw, S, m, png_dir):
    mo = os88mouse.Mouse(marty=m)        # ONE connection, shared - a second
                                         # client to the debug server HANGS
    before, after = weavesmoke._open_bundle(m, mo, S, machine)
    new = sorted(after - before)
    if not check(len(new) == 1, "%s: %s opened one window" % (machine, BUNDLE),
                 "nothing below would mean what it says",
                 got=sorted(new), want="exactly one slot"):
        return
    slot = new[0]
    cx, cy, cw, ch = _content(m, S, slot, vidw)

    m0 = _cardrows(card)
    gr0, vr = _grid_geom(m0)
    # The grid spans the row (7.3), so its width IS the content grid's CW -
    # which the model's picture is exactly as wide as.
    ncells = len(m0[0])

    # --- 1: the opening picture is the model's ------------------------------
    vw, vh, rows = m.vram(card)
    _park(m, mo, vw, vh)
    vw, vh, rows = m.vram(card)
    g = Glass(rows, vw, vh, cx, cy)
    _learn(g, m0, gr0, vr)
    for k, (a, b) in enumerate(zip(_read_bands(g, gr0, vr, ncells),
                                   _model_bands(m0, gr0, vr))):
        ok, why = _agree(a, b)
        check(ok, "%s: band %d reads what the model drew" % (machine, k),
              "6.9.1's layout and 5.2.1's display, on the glass. The bands "
              "were composed by wband.inc out of the cell store the CELLS "
              "section loaded, and the values in them came out of wfx.inc's "
              "first recalculation - so a disagreement here is any one of "
              "those, and weavevm has already said it is not the arithmetic",
              got=repr(a), want=repr(b) + ("  (%s)" % why if why else ""))

    base = [_band_pixels(rows, cx, cy, gr0 + WG_CHROME + k, ncells)
            for k in range(vr)]

    # --- 2: the SCRIPT path, and the damage it does -------------------------
    # `Cider +1` is SHEET's own bump(): g.setCell(4,2, g.cell(4,2)+1), through
    # the ring, a slice, CALLM, the store and 5.5's sliced recalculation.
    _press(m, mo, card, cx, cy, "Cider +1")
    os88marty.until(m, lambda mm: _changed(mm, card, cx, cy, gr0, vr,
                                           ncells, base),
                    "the recalculation to reach the glass", limit=20.0)
    _park(m, mo, vidw, vh)
    vw, vh, rows = m.vram(card)
    m1 = _cardrows(card, "click btnBump\n")
    moved = set(k for k in range(vr)
                if m1[gr0 + WG_CHROME + k] != m0[gr0 + WG_CHROME + k])
    drawn = set(k for k in range(vr)
                if _band_pixels(rows, cx, cy, gr0 + WG_CHROME + k,
                                ncells) != base[k])
    check(drawn == moved,
          "%s: the edit repainted exactly the rows that changed" % machine,
          "5.5.1's per-row damage, said as a fact about the glass. A runtime "
          "that repaints the whole grid on an edit passes every value check "
          "and fails this one - and a 20-row page is 291 ms against one "
          "row's 14.5 (WEAVE-SPEC 14)",
          got=sorted(drawn), want=sorted(moved))

    g = Glass(rows, vw, vh, cx, cy)
    _learn(g, m1, gr0, vr)
    for k, (a, b) in enumerate(zip(_read_bands(g, gr0, vr, ncells),
                                   _model_bands(m1, gr0, vr))):
        ok, why = _agree(a, b)
        check(ok, "%s: band %d after setCell() reads the model's" % (machine,
                                                                    k),
              "the whole stack - ring, slice, CALLM, 5.6's store, 5.5's two "
              "passes, 5.2.1's display, the band composer - with nothing in "
              "the path that exists only for this test",
              got=repr(a), want=repr(b) + ("  (%s)" % why if why else ""))

    # --- 3: the FORMULA BAR path -------------------------------------------
    # A cell, then its bar, then 6.9.2's compiler and a 5.6 kind-6 pool slot.
    _click_cell(mo, cx, cy, gr0, TYPE_R, TYPE_C)
    os88marty.settle(m)
    mo.click(cx + 8, cy + (gr0 * 8) + 4)        # the bar, to arm it
    os88marty.settle(m)
    m.type_text(TYPE_SRC)
    os88marty.settle(m)
    m.key("Enter")
    os88marty.settle(m)
    _park(m, mo, vidw, vh)
    vw, vh, rows = m.vram(card)
    m2 = _cardrows(card, "click btnBump\nselect g %d %d\nedit g %d %d %s\n"
                   % (TYPE_R, TYPE_C, TYPE_R, TYPE_C, TYPE_SRC))
    g = Glass(rows, vw, vh, cx, cy)
    _learn(g, m2, gr0, vr)
    for k, (a, b) in enumerate(zip(_read_bands(g, gr0, vr, ncells),
                                   _model_bands(m2, gr0, vr))):
        ok, why = _agree(a, b)
        check(ok, "%s: band %d after a TYPED formula reads the model's"
              % (machine, k),
              "6.9.3's classification chose `formula`, 6.9.2's resident "
              "compiler emitted 5.3's RPN into a 5.6 kind-6 pool slot, and "
              "5.5's passes evaluated it out of the GRID claim rather than "
              "the bundle's - which is the one thing a bundle formula never "
              "exercises",
              got=repr(a), want=repr(b) + ("  (%s)" % why if why else ""))

    inc = [_band_pixels(rows, cx, cy, gr0 + WG_CHROME + k, ncells)
           for k in range(vr)]

    # --- 4: the tpdraw identity --------------------------------------------
    # Scroll the selection off the bottom and back to exactly where it was:
    # a move that scrolls re-composes EVERY band (6.9.1), so the same state is
    # reached a second time by a completely different path.
    for _ in range(vr):
        m.key("ArrowDown")
    for _ in range(vr + 4):
        m.key("ArrowUp")
    for _ in range(TYPE_R - 1):
        m.key("ArrowDown")
    os88marty.settle(m)
    _park(m, mo, vidw, vh)
    vw, vh, rows = m.vram(card)
    full = [_band_pixels(rows, cx, cy, gr0 + WG_CHROME + k, ncells)
            for k in range(vr)]
    bad = [k for k in range(vr) if inc[k] != full[k]]
    check(not bad,
          "%s: the incremental picture IS the full one" % machine,
          "tests/tpdraw.py's identity, for the grid. Every rule it enforces "
          "was a shipped defect first, and 6.9.1's own is the XOR fast path: "
          "XOR-ing an inverted cell restores it and a plain one inverts it, "
          "which is true right up until the composer and the XOR disagree "
          "about which cells the selection covers",
          got="bands %s differ" % bad, want="every band identical")

    # --- 5: and the guest is still EXECUTING --------------------------------
    t0 = m.read(0x46C, 4)
    os88marty.settle(m, quiet=0.6, stable=1)
    check(m.read(0x46C, 4) != t0, "%s: the guest is still running" % machine,
          "a task frozen holding the gfx lock draws a perfect window and "
          "never draws another (SPEC.md 59.7)",
          got="the BIOS tick did not move", want="a moving tick")

    if png_dir:
        weavesmoke._shot(png_dir, machine + "-grid", vw, vh, rows)


def _changed(m, card, cx, cy, gr0, vr, ncells, base):
    rows = m.vram(card)[2]
    return any(_band_pixels(rows, cx, cy, gr0 + WG_CHROME + k, ncells)
               != base[k] for k in range(vr))


def _click_cell(mo, cx, cy, gr0, r1, c1):
    """A data cell's centre, from 6.9.1's geometry - the grid spans the row,
    so its own x is the content origin."""
    x = cx + (WG_GUT + (c1 - 1) * WG_COLW) * 8 + (WG_COLW * 8) // 2
    y = cy + (gr0 + WG_CHROME + (r1 - 1)) * 8 + 4
    mo.click(x, y)


def _press(m, mo, card, cx, cy, label):
    """Press a button by finding its LABEL in the ORACLE's picture, so a
    layout change moves the click with it (weavesession's own rule)."""
    r = subprocess.run(["python3", "tools/weavesim.py", "--render",
                        "build/" + BUNDLE, "--adapter", card],
                       cwd=ROOT, capture_output=True, text=True)
    body = [ln[1:-1] for ln in r.stdout.splitlines()
            if len(ln) > 2 and ln[0] in "|:" and ln[-1] in "|:"]
    pat = re.compile(r"(?<![A-Za-z])" + re.escape(label)
                     + r"(?![A-Za-z])")
    for row, ln in enumerate(body):
        mm = pat.search(ln)
        if mm:
            mo.click(cx + (mm.start() + len(label) // 2) * 8 + 4,
                     cy + row * 8 + 4)
            os88marty.settle(m)
            return
    raise RuntimeError("weavesim --render: no %r on the card" % label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine")
    ap.add_argument("--png")
    ap.add_argument("--no-make", action="store_true")
    a = ap.parse_args()
    if not a.no_make:
        # THE SAME `make`, AND IT DOES NOTHING once the runner has built the
        # artefact: Row(wants=...) declares it and os88test's prebuild builds
        # it before any row starts. That is what lets this row drop
        # builds=True and share the emulator lane.
        os88fixture.need(DISK)
    for machine, cardk, w, h in [t for t in MACHINES
                                 if not a.machine or t[0] == a.machine]:
        session(machine, cardk, w, h, a.png)
    return done("weavegrid")


if __name__ == "__main__":
    sys.exit(main())
