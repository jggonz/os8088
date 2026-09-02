#!/usr/bin/env python3
"""LOOM's Preview pane against the model's picture (WEAVE-SPEC 1.7.1, 12.3).

    make loomdisk && python3 tests/weaveprev.py
    python3 tests/weaveprev.py --machine os8088_5150_herc_gla
    python3 tests/weaveprev.py --png shots/

WHY IT EXISTS. Wave 7's Preview draws the card with WEAVE's own flow walk and
WEAVE's own component painter, compiled a second time into LOOM.WPV - a
SECOND RESIDENT SEGMENT (WEAVE-SPEC 1.2.4). Nothing else in this family
exercises that module at all: `weavegfx` reads the RUNTIME's window, and every
assertion it makes would pass with the pane blank.

WHAT A FAILURE HERE MEANS, which is the reason the row is worth its minutes:
the pictures cannot drift, because there is only one description of them -
apps/weave/wflow.c and apps/weave/wpaint.c are #included into
apps/loom/lmpvmod.c rather than reimplemented, which is WEAVE-SPEC 1.2's
"never a second copy" applied to code. So a wrong picture here is the SEAM or
the SEGMENT and never the painter: the pane rect arriving wrong, the module's
.bss not zeroed, the caller's DS not banked, the section table read at the
wrong offsets, a stale module believed. Those are exactly the failures a
second segment adds and an overlay does not, and they are silent in every
other row.

THE THREE ASSERTIONS ARE tests/weavegfx.py's, aimed at the pane instead of at
the window, and that file's header is the argument for each. In short:

  1. EVERY ROW READS AS THE MODEL DRAWS IT, by 12.3.2's consistency rule -
     glyphs are LEARNED from a row whose text is already known and an
     unlearned character reads '?' and is skipped, so this never fails for a
     BIOS font this tree cannot see.
  2. A ROW THE MODEL DRAWS ON HAS INK, AND A ROW WELL CLEAR OF ANYTHING IS
     BLANK - the half assertion 1 cannot make, because a pane that drew
     nothing at all passes a text comparison made entirely of question marks.
     This is the assertion that fails when the module does not load.
  3. NOTHING IS DRAWN PAST THE LAST WHOLE PANE ROW. The walk clips against
     the box it was given (7.4) and every primitive draws in ABSOLUTE screen
     coordinates, so a pane rect that was too tall would draw over LOOM's own
     status row and then through the window border.

THE ORACLE IS `weavesim --render --preview`, and the `--preview` half is not a
convenience: WEAVE-SPEC 1.7.1 says a Preview draws a <grid> and a <canvas> as
their FRAME and nothing inside, because a grid's body is the band composer
over a cell store and a canvas's is the compositor inside WEAVE.WSM, and a
preview builds neither. The model was taught that one rule in one flag rather
than the test being taught to ignore two components - which is the difference
between a scope decision and a divergence.

`--cells` IS MEASURED OFF THE GLASS AND NOT ASSUMED. The pane is a child area
inside LOOM's window (1.7): narrower than the content box by the sidebar and
the scroll bar, shorter by the status row. This file derives it from the
window record the kernel is holding plus apps/loom/loom.h's four constants,
which is a second opinion about apps/loom/loom.c's lm_layout() - and a wrong
one fails LOUDLY on the first row rather than subtly, because a card laid out
in the wrong width wraps somewhere else entirely.

ALL THREE DEMO PROJECTS, because they are three different shapes of card and
the third is the one the flag is about: FORM is the widget zoo, SHEET has a
<grid> in it and PONG a <canvas>, so between them they cover both halves of
1.7.1's rule.

BOTH 1bpp ADAPTERS, because they are the target class and grey rounds to black
on them (SPEC.md 39.4) - and because the GLaBIOS twins are the only pair that
boots in a tree with no IBM ROM (tools/martypc/build.sh).

IT DRIVES THE LOOM DISK, and the sources are in its LOOM/ folder (WEAVE-SPEC
11.2: the IDE beside what it edits, the runtime beside what it runs), so the
navigation is weavesmoke's two double-clicks with FOLDER pointed at LOOM
rather than WEAVE. `make weavedisk` carries the same two folders and would do
as well; the pane under test is the same pane.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import weavesmoke                                           # noqa: E402
import weavegrid                                            # noqa: E402
import weavegfx                                             # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]

# The 360KB geometry, because the 5150 machines have 360KB drives - and it is
# the tightest of the three, which is the one worth booting.
DISK = "build/loom360.img"

# (the .WML to open, the .WAB the model renders, a row of text this file
# already knows). The known row is 12.3.2's learning row: it is the card's own
# first static label, painted from ATOMS before a line of bytecode has run -
# and no bytecode runs in a Preview at all, which makes it the only kind of
# string the pane can contain.
CARDS = [("FORM",  "build/FORM.WAB",  "Weave Greeter"),
         ("SHEET", "build/SHEET.WAB", "Picnic Budget"),
         ("PONG",  "build/PONG.WAB",  "Weave Pong")]

# apps/loom/loom.h's and apps/loom/loom.c's, and they are named rather than
# inlined so that a mismatch is one grep. LM_SIDE_CELLS is the sidebar's width
# in cells, LM_SBW the scroll bar's in pixels, and the shadow's two bounds cap
# the pane the way lm_layout() caps it.
LM_SIDE_CELLS = 12
LM_SBW = 14
LM_SH_STRIDE = 88
LM_SH_ROWS = 52

# The menu bar, dispcalc.py's own constants. The View menu is set index 1, and
# an app's own menus start at bar cell 1 because cell 0 is the chip - so View
# is cell 2, and Preview is its first item.
MBAR_H, MENU_ITEM_H, MB_ENTSZ, MB_XL = 20, 16, 14, 6
VIEW_CELL, PREVIEW_ITEM = 2, 0


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def _menu_pick(m, mo, S, cell, item):
    """Drop menu-bar cell `cell` and pick item `item` - dispcalc.py's, and its
    reason: the x comes out of the kernel's own `menu_bar` table because the
    bar is REBUILT whenever the owner changes (SPEC.md 12.2), so a computed x
    is a second opinion about a table that is right there."""
    t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
    x = _u16(t, MB_XL) + 6
    mo.menu(x, 8, x, MBAR_H + 1 + item * MENU_ITEM_H + 8)
    os88marty.settle(m)


def _pane(cx, cy, cw, ch):
    """LOOM's editor pane, in pixels and cells, out of the window's content
    box - apps/loom/loom.c's lm_layout(), replicated.

    IT IS A SECOND OPINION AND IT IS SUPPOSED TO BE. The alternative is
    reading lm_ex/lm_ecols out of LOOM's own segment, and nothing in this tree
    resolves a symbol inside a package (tools/os88sym.py re-assembles the
    KERNEL). A second opinion about arithmetic this short is cheap and it
    fails loudly: a card laid out one cell too wide wraps somewhere else and
    every row after the first disagrees.
    """
    ox = (cx + 7) & ~7
    lcw = (cx + cw - ox) // 8
    lch = ch // 8
    sidew = LM_SIDE_CELLS if lcw - LM_SIDE_CELLS >= 20 else 0
    sbx = cx + cw - 1 - (LM_SBW - 1)
    ex = ox + sidew * 8
    ecols = min((sbx - ex) // 8, LM_SH_STRIDE)
    erows = min(lch - 1, LM_SH_ROWS)     # the status row is the last one
    return ex, cy, ecols, erows


def _render(wab, adapter, ecols, erows):
    r = subprocess.run(["python3", "tools/weavesim.py", "--render", wab,
                        "--adapter", adapter, "--preview",
                        "--cells", "%dx%d" % (ecols, erows)],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("weavesim --render --preview: %s"
                           % (r.stderr or r.stdout)[-400:])
    return [ln[1:-1] for ln in r.stdout.splitlines()
            if len(ln) > 2 and ln[0] in "|:" and ln[-1] in "|:"]


def _open(m, mo, S, machine, stem):
    """Double-click <stem>.WML in the B: window and answer LOOM's window slot.

    IT IS weavesmoke._open_bundle(), UNCHANGED AND NOT A COPY - open drive B,
    double-click `weavesmoke.BUNDLE`, retry the NAVIGATION up to three times
    and nothing else. That file's docstring is the argument for every part of
    it, and the only difference here is which file is double-clicked: a `.WML`
    rather than a `.WAB`, so the association that answers is LOOM's rather
    than WEAVE's (WEAVE-SPEC 1.5 step 2).
    """
    before, after = weavesmoke._open_bundle(m, mo, S, machine)
    new = sorted(after - before)
    if not check(len(new) == 1, "%s/%s: opened one window" % (machine, stem),
                 "nothing below would mean what it says",
                 got=new, want="exactly one slot"):
        return None
    return new[0]


def _drive(machine, card, vidw, vidh, S, m, stem, wab, known, png_dir):
    mo = os88mouse.Mouse(marty=m)
    win = _open(m, mo, S, machine, stem)
    if win is None:
        return

    _menu_pick(m, mo, S, VIEW_CELL, PREVIEW_ITEM)   # View > Preview
    os88marty.settle(m)
    os88marty.settle(m)                 # the pack is a claim and a compile;
                                        # the module read is a disk revolution
    weavegrid._park(m, mo, vidw, vidh)
    vw, vh, rows = m.vram(card)

    cx, cy, cw, ch = weavegrid._content(m, S, win, vidw)
    ex, ey, ecols, erows = _pane(cx, cy, cw, ch)
    if not check(ecols >= 24 and erows >= 4,
                 "%s/%s: the pane is big enough to lay a card out in"
                 % (machine, stem),
                 "below os88_wm_minsize()'s floor there is nothing to "
                 "compare, and WPVE_PANE is what the module answers",
                 got="%dx%d cells" % (ecols, erows), want=">= 24x4"):
        return

    model = _render(wab, card, ecols, erows)
    cells = len(model[0])
    g = weavegrid.Glass(rows, vw, vh, ex, ey)
    g.learn(0, known)

    for r in range(min(len(model), erows)):
        want = model[r].rstrip()
        got = g.read(r, cells)
        ok, why = weavegrid._agree(got, want)
        check(ok, "%s/%s: pane row %d reads as the model draws it"
              % (machine, stem, r),
              "12.3.2's consistency rule over the SAME painter the runtime "
              "uses (WEAVE-SPEC 1.2.4) - so a mismatch here is the seam, the "
              "pane rect or the module's own state, and never the picture",
              got=repr(got), want=repr(want) + ("  (%s)" % why if why else ""))

        ink = weavegfx._ink(rows, ex, ey, r, cells)
        prev = model[r - 1].rstrip() if r > 0 else ""
        if not want.strip():
            if prev.strip():
                continue                # the row under a two-row control -
                                        # the sketch draws one (weavegfx)
            check(ink == 0, "%s/%s: pane row %d is blank, as the model has it"
                  % (machine, stem, r),
                  "the half the text check cannot make: an unlearned glyph "
                  "reads '?' and is skipped, so a pane that drew NOTHING - a "
                  "module that did not load, a .bss that was not zeroed - "
                  "would pass a comparison made entirely of question marks",
                  got="%d dark pixels" % ink, want="0")
        else:
            check(ink > 0, "%s/%s: pane row %d has ink, as the model has it"
                  % (machine, stem, r),
                  "this is the assertion that fails when LOOM.WPV is absent "
                  "or stale: the pane then holds one sentence and nothing "
                  "else (WEAVE-SPEC 10.3)",
                  got="0 dark pixels", want="> 0")

    # --- 3: nothing past the last whole pane row ---------------------------
    # The pane ends one cell row above the window's content box, because
    # lm_layout() keeps the last row for the status line - and THAT ROW HAS
    # INK IN IT, LOOM's own label. So this counts the remainder BELOW the
    # status row, which is the band a card drawn one row too tall would reach
    # into first.
    below = 0
    for y in range(ey + (erows + 1) * 8, min(cy + ch, vh)):
        for x in range(ex, min(ex + cells * 8, vw)):
            if not rows[y][x]:
                below += 1
    check(below == 0,
          "%s/%s: no ink past the pane and its status row" % (machine, stem),
          "7.4's clip, which w_paint_card arms and which the module inherits "
          "unchanged: every primitive draws in ABSOLUTE screen coordinates, "
          "so a pane rect that was too tall draws through LOOM's own window "
          "border and across the dock",
          got="%d dark pixels" % below, want="0")

    if png_dir:
        weavesmoke._shot(png_dir, "%s-preview-%s" % (machine, stem),
                         vw, vh, rows)


def session(machine, card, vidw, vidh, png_dir=None, only=None):
    S = os88sym.linear
    for stem, wab, known in CARDS:
        if only and stem != only:
            continue
        weavesmoke.BUNDLE = "%s.WML" % stem
        weavesmoke.FOLDER = "LOOM"          # the sources' folder (11.2)
        with os88marty.launch("build/os8088-360.img", apps=DISK,
                              machine=machine) as m:
            try:
                _drive(machine, card, vidw, vidh, S, m, stem, wab, known,
                       png_dir)
            except os88marty.MartyError as e:
                check(False, "%s/%s: the session broke off" % (machine, stem),
                      "the machine booted, so this is the harness losing its "
                      "grip on it rather than the tree being wrong",
                      got=str(e)[:200], want="a driven session")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine")
    ap.add_argument("--project")
    ap.add_argument("--png")
    ap.add_argument("--no-make", action="store_true")
    a = ap.parse_args()
    if not a.no_make:
        r = subprocess.run(["make", DISK], cwd=ROOT, capture_output=True,
                           text=True)
        if r.returncode:
            print("weaveprev: `make %s` failed:\n%s"
                  % (DISK, (r.stderr or r.stdout)[-800:]))
            return 1
    for machine, cardk, w, h in [t for t in MACHINES
                                 if not a.machine or t[0] == a.machine]:
        session(machine, cardk, w, h, a.png, a.project)
    return done("weaveprev")


if __name__ == "__main__":
    sys.exit(main())
