#!/usr/bin/env python3
"""The card's PIXELS against the model's picture, no goldens (WEAVE-SPEC 12.3).

    make weavedisk && python3 tests/weavegfx.py
    python3 tests/weavegfx.py --machine os8088_5150_herc_gla
    python3 tests/weavegfx.py --png shots/

WHY IT EXISTS, WHICH IS zgfx's REASON SAID ABOUT A WIDGET LIBRARY. Every other
row in this family reads a NUMBER or a STRUCTURE: weavevm diffs two
interpreters' end states, weavesession reads a meter's fill and a window's
existence, weavesmoke asserts a frame's edges. None of them can see a
component drawn at the wrong row, a label painted over its neighbour, a
control that draws nothing at all, or a card whose ink runs outside the
content box - and those are precisely a component library's failure modes
(12.4's closing paragraph). A transcript is structurally blind to a picture.

AND IT IS NOT A GOLDEN SCREENSHOT, for tests/bootsmoke.py's reason: a golden
fails on every legitimate pixel change - a font tweak, a one-pixel inset, a
component that gains a border - and a gate that cries wolf gets turned off.
What it compares is the machine's picture against the MODEL's, and the model
is `weavesim --render`, which WEAVE-SPEC 12.1 makes the oracle every
differential in this family diffs against.

THREE ASSERTIONS PER CARD:

  1. EVERY ROW READS AS THE MODEL DRAWS IT, by 12.3.2's consistency rule -
     glyphs are LEARNED from rows whose text is already known and a character
     the learning rows never contained reads '?' and is skipped, so this never
     fails for a BIOS font this tree cannot see. It catches text at the wrong
     column, which is a layout defect wearing a drawing defect's clothes.

  2. A ROW THE MODEL DRAWS ON HAS INK, AND A ROW WELL CLEAR OF ANYTHING IS
     BLANK. This is the half assertion 1 cannot make: an unlearned glyph
     reads '?' and is skipped, so a component that drew nothing at all could
     pass a text comparison made entirely of question marks. Ink presence
     needs no font.

     "Well clear" is the SECOND blank row of a run and not the first, and the
     reason is the model's picture rather than the machine's: `--render` is a
     CELL SKETCH, one character per cell, and it draws a two-row control on
     one row - an `<input>` as `[text___]`, a `<check>` as `X label` - while
     the real control's frame and its 12x12 glyph reach into the row below
     (7.3 gives all three a natural height of 2). So the row after a drawn
     row is not asserted, and every row after that is. It still catches a
     component drawn at the wrong row, which is the defect this exists for,
     and it does not ask the sketch to be a pixel map.

  3. NOTHING IS DRAWN OUTSIDE THE CONTENT BOX. The walk clips against it
     (WEAVE-SPEC 7.4) and every primitive draws in ABSOLUTE screen
     coordinates, so a component whose rect ran past the bottom would draw
     through the window border and across the dock - which is exactly what
     the clip in w_paint_card exists to stop, and nothing else here would
     notice it being removed.

BOTH 1bpp ADAPTERS, because they are the target class and grey rounds to black
on them (SPEC.md 39.4) - and because a drawing change is not done until it has
been looked at on one (CLAUDE.md).
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                           # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import weavesmoke                                           # noqa: E402
import weavegrid                                            # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISK = weavesmoke.DISK

MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]

# One bundle per SHAPE of card. FORM is the widget zoo - label, input, button,
# check, meter, list - and SHEET is the band composer, which draws through a
# completely different primitive (GFX_BLIT1 rather than FONT_RUN).
CARDS = [("FORM.WAB", "apps/weave/demos/form.wml", "Weave Greeter"),
         ("SHEET.WAB", "apps/weave/demos/sheet.wml", "Picnic Budget")]


def _render(bundle, src, adapter):
    r = subprocess.run(["python3", "tools/weavesim.py", "--render",
                        "build/" + bundle, "--adapter", adapter],
                       cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("weavesim --render: %s"
                           % (r.stderr or r.stdout)[-400:])
    return [ln[1:-1] for ln in r.stdout.splitlines()
            if len(ln) > 2 and ln[0] in "|:" and ln[-1] in "|:"]


def _ink(rows, cx, cy, row, cells):
    """Unlit pixels in one 8-px card row.  The kernel white-fills a window's
    content before W_PAINT, so INK is 0 and the count is of dark pixels."""
    n = 0
    for k in range(8):
        y = cy + row * 8 + k
        if y >= len(rows):
            break
        r = rows[y]
        for x in range(cx, min(cx + cells * 8, len(r))):
            if not r[x]:
                n += 1
    return n


def session(machine, card, vidw, vidh, png_dir=None):
    S = os88sym.linear
    for bundle, src, known in CARDS:
        weavesmoke.BUNDLE = bundle
        with os88marty.launch("build/os8088-360.img", apps=DISK,
                              machine=machine) as m:
            try:
                _drive(machine, card, vidw, S, m, bundle, src, known, png_dir)
            except os88marty.MartyError as e:
                check(False, "%s/%s: the session broke off" % (machine,
                                                               bundle),
                      "the machine booted, so this is the harness losing its "
                      "grip on it rather than the tree being wrong",
                      got=str(e)[:200], want="a driven session")


def _drive(machine, card, vidw, S, m, bundle, src, known, png_dir):
    mo = os88mouse.Mouse(marty=m)
    before, after = weavesmoke._open_bundle(m, mo, S, machine)
    new = sorted(after - before)
    if not check(len(new) == 1, "%s/%s: opened one window" % (machine,
                                                              bundle),
                 "nothing below would mean what it says",
                 got=new, want="exactly one slot"):
        return
    cx, cy, cw, ch = weavegrid._content(m, S, new[0], vidw)
    vw, vh, rows = m.vram(card)
    weavegrid._park(m, mo, vw, vh)
    vw, vh, rows = m.vram(card)

    model = _render(bundle, src, card)
    cells = len(model[0])
    g = weavegrid.Glass(rows, vw, vh, cx, cy)
    # 12.3.2: learn from a row whose text is ALREADY KNOWN - the card's own
    # first static label, painted from ATOMS before a line of bytecode ran.
    g.learn(0, known)

    visible = ch // 8
    for r in range(min(len(model), visible)):
        want = model[r].rstrip()
        got = g.read(r, cells)
        ok, why = weavegrid._agree(got, want)
        check(ok, "%s/%s: card row %d reads as the model draws it"
              % (machine, bundle, r),
              "12.3.2's consistency rule - every LEARNED glyph must match the "
              "model's string, and no learned glyph may appear where the "
              "model says another one does. An unlearned character reads '?' "
              "and is skipped, so this never fails for a BIOS font this tree "
              "cannot see",
              got=repr(got), want=repr(want) + ("  (%s)" % why if why else ""))

        ink = _ink(rows, cx, cy, r, cells)
        prev = model[r - 1].rstrip() if r > 0 else ""
        if not want.strip():
            if prev.strip():
                continue                # the row under a two-row control -
                                        # see the head: the sketch draws one
            check(ink == 0, "%s/%s: card row %d is blank, as the model has it"
                  % (machine, bundle, r),
                  "the half assertion 1 cannot make: an unlearned glyph reads "
                  "'?' and is skipped, so a component drawn on the wrong row "
                  "would otherwise pass a comparison made of question marks",
                  got="%d dark pixels" % ink, want="0")
        else:
            check(ink > 0, "%s/%s: card row %d has ink, as the model has it"
                  % (machine, bundle, r),
                  "a control that draws NOTHING passes every text check "
                  "there is, because nothing is what '?' skips",
                  got="0 dark pixels", want="> 0")

    # --- 3: nothing outside the content box --------------------------------
    # ...AND THE GROW BOX IS NOT OURS. The kernel draws it AFTER W_PAINT
    # returns, inside our lock hold and outside our content (wpaint.c says so
    # where it arms the clip), and on Hercules the content height has a 4-px
    # remainder below the last whole cell row for its top edge to land in - 13
    # dark pixels, on both bundles, on that adapter alone. So the rightmost
    # GROW cells of the remainder band are the kernel's and are not counted.
    GROW = 20
    below = 0
    for y in range(cy + visible * 8, min(cy + ch, vh)):
        for x in range(cx, min(cx + cells * 8, vw) - GROW):
            if not rows[y][x]:
                below += 1
    check(below == 0,
          "%s/%s: no ink past the last whole content row" % (machine, bundle),
          "7.4's clip, which w_paint_card arms and nothing else in this "
          "family would notice being removed: every primitive draws in "
          "ABSOLUTE screen coordinates, so a component whose rect ran past "
          "the bottom draws through the window border and across the dock",
          got="%d dark pixels" % below, want="0")

    if png_dir:
        weavesmoke._shot(png_dir, "%s-%s" % (machine, bundle.split(".")[0]),
                         vw, vh, rows)


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
    return done("weavegfx")


if __name__ == "__main__":
    sys.exit(main())
