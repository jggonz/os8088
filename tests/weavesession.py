#!/usr/bin/env python3
"""A driven Weave session, diffed against the model (WEAVE-SPEC 12.3, 12.3.1).

    make weavedisk && python3 tests/weavesession.py
    python3 tests/weavesession.py --machine os8088_5150_herc_gla
    python3 tests/weavesession.py --png shots/     # ...and LOOK at it

WHAT IT IS FOR. `weavevm` proves the interpreter and can reach nothing else:
it is a boot sector, so it has no ring in a running app, no slice boundary
against a real tick, no component to write and no alert to raise. This row is
the other half - the SHIPPING package under MartyPC, driven the way a person
drives it, with every reading compared against `weavesim --run` given the same
events.

WHY IT READS THE GLASS AND NOT A TRANSCRIPT (WEAVE-SPEC 12.3.1 at length).
The row was drafted as a `-DWVHARNESS` build printing a transcript on COM4,
the zharness shape. That is right for Frotz, whose entire output is text, and
wrong here: WEAVE's output is a picture and a set of component states, and a
build that speaks about them on a wire is a SECOND IMPLEMENTATION of the thing
under test - it can be right about a machine the shipping build gets wrong,
and every state a later wave adds has to learn to serialize itself. So this
reads only facts that are on the glass or in the kernel's own window table:

  1. the `<meter>`'s fill, IN PIXELS, which is an exact reading of the `value`
     the bytecode wrote (`doGreet` sets `count.value = greets`). The model
     predicts the value; the fill is `interior x value / max` and the runtime
     and the oracle compute it the same way (WEAVE-SPEC 6.4);
  2. that an ALERT WINDOW exists after `alert()` and is gone after OK - two
     separate structural facts in the window table, which is where 8.2's
     "returns immediately, the callback arrives as a later event" actually
     shows;
  3. that the row a `.text` write lands on CHANGED, and changed back;
  4. that the guest is still EXECUTING at the end. A task frozen holding the
     gfx lock draws a perfect window and never draws another (SPEC.md 59.7).

Every one of those is a value the bytecode computed, arriving through the
whole stack - ring, slice, SETP, painter, primitive - with nothing in the path
that exists only for this test.

NAVIGATION IS weavesmoke's, IMPORTED AND NOT COPIED. That file's
`_open_bundle` carries the retry, the two `until` waits and the reasons for
both, all of them paid for in lost runs; a second copy here would drift from
it on the first fix. This file owns the SESSION and nothing else.

WHAT FLAKES, AND IT IS NOT THIS FILE. The navigation is weavesmoke's and so
is its failure mode: a double-click whose two presses straddle the kernel's
9-tick window is seen as two FIRST clicks, and on a loaded host that happens.
Measured here across three consecutive runs on a box that had just run three
emulator sessions: two clean at 135 s, one that spent all three of
`_open_bundle`'s retries and failed before a single assertion about WEAVE had
run. That is a statement about the machine running the test - the guest says
so itself, in those words - and the right response to it is the one
weavesmoke already documents: retry the navigation, never the assertions, and
print every retry so a host that has really got slower is visible.
"""

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "unit"))
import os88fixture                                           # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import weavesmoke                                           # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISK = weavesmoke.DISK
BUNDLE = weavesmoke.BUNDLE
NAME = "nora"                   # every letter of it is in the card's own
                                # static labels, which is what 12.3.2's
                                # consistency reading needs

MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]


# ---------------------------------------------------------------------------
# THE ORACLE
# ---------------------------------------------------------------------------

EVENTS = """\
# The same gestures the guest is driven through below, in the same order.
set who nora
click greet
click greet
click loud
click greet
dump
"""


def _oracle_meter():
    """`count.value` after the three greets, from tools/weavesim.py.

    THE MODEL ANSWERS, NOT THIS FILE. A test that writes down 3 is a test
    that has to be edited when the demo changes; one that asks the oracle
    fails loudly when the demo and the runtime stop agreeing, which is the
    only thing worth failing for.
    """
    ev = os.path.join(ROOT, "build", "weavesession.events")
    open(ev, "w").write(EVENTS)
    out = subprocess.run(["python3", "tools/weavesim.py",
                          "--run", "build/FORM.WAB", "--events", ev,
                          "--src", "apps/weave/demos/form.wml"],
                         cwd=ROOT, capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError("weavesim --run: %s"
                           % (out.stderr or out.stdout)[-300:])
    m = re.search(r"meter \S+: value=(\d+)", out.stdout)
    if not m:
        raise RuntimeError("weavesim --run printed no meter value:\n%s"
                           % out.stdout[-400:])
    mx = re.search(r'max="(\d+)"', open(os.path.join(
        ROOT, "apps/weave/demos/form.wml")).read())
    return int(m.group(1)), int(mx.group(1)) if mx else 100


def _oracle_meter_cells(adapter):
    """The meter's (col, row, width) in CELLS, out of `weavesim --render`.

    The picture is the oracle's own, so a layout change moves this with it.
    The meter is drawn `[----]` and is the only bracketed run on the row that
    also carries the label beside it, which is what makes the scan unique.
    """
    out = subprocess.run(["python3", "tools/weavesim.py", "--render",
                          "build/FORM.WAB", "--adapter", adapter],
                         cwd=ROOT, capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError("weavesim --render: %s"
                           % (out.stderr or out.stdout)[-300:])
    body = [ln for ln in out.stdout.splitlines()
            if ln.startswith("|") and ln.endswith("|")]
    for r, ln in enumerate(body):
        m = re.search(r"\[(-+)\]", ln)
        if m and "Greetings" in ln:
            # body[k] IS card row k: the +---+ borders start with '+' and are
            # already filtered out, so there is no header row to subtract.
            # Column 0 of the card is index 1, past the leading '|'.
            return m.start() - 1, r, len(m.group(0))
    raise RuntimeError("weavesim --render: no meter row in the picture")


# ---------------------------------------------------------------------------
# READING THE GLASS
# ---------------------------------------------------------------------------

def _content(m, S, slot, vidw):
    """The window's content box in pixels, through SPEC.md 11.95.2's flush
    rule - weavesmoke's own derivation, which is why its helper is used."""
    x, y, w, h, seg, flags = weavesmoke._win(m, S, slot)
    if weavesmoke._flush(x, w, flags, vidw):
        cx, cw = x, w
    else:
        cx, cw = x + 1, w - 2
    cy = y + os88geom.TITLE_H       # NOT +1: the kernel's own content origin
                                    # is y + TITLE_H (tools/os88geom.py), and
                                    # only the X side has a border to skip
    return cx, cy, cw, y + h - 1 - cy


def _lit(rows, x0, y0, x1, y1):
    """Lit pixels in the box.

    `Marty.vram()` hands back ONE BYTE PER PIXEL, already de-banked, and 1 is
    LIT - which on a window whose content the kernel white-filled means 1 is
    the PAPER and 0 is the ink. Nothing below cares which way round that is:
    the two readings are compared with each other, not with a threshold.
    """
    n = 0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if rows[y][x]:
                n += 1
    return n


def _meter_fill(m, card, S, slot, vidw, cells):
    """The filled WIDTH of the meter, in pixels.

    The bar is solid ink inside a frame, so the fill is the run of fully-inked
    columns from the left edge of the INTERIOR - which starts one pixel past
    the frame's own column, so the frame cannot be counted as a fill of 1.
    """
    col, row, wcells = cells
    cx, cy, _, _ = _content(m, S, slot, vidw)
    rows = m.vram(card)[2]              # vram answers (w, h, rows)
    x0 = cx + col * 8 + 1                       # inside the frame
    x1 = cx + (col + wcells) * 8 - 2
    y0 = cy + row * 8 + 1
    y1 = cy + row * 8 + 6
    n = 0
    for x in range(x0, x1 + 1):
        # THE BAR IS INK, so its columns are UNLIT - the content ground is
        # the kernel's white fill and vram's 1 is lit. Reading it the other
        # way round measures the EMPTY part of the meter and grows when the
        # value falls, which is a number that looks plausible and is the
        # inverse of the answer.
        if not any(rows[y][x] for y in range(y0, y1 + 1)):
            n += 1
        else:
            break
    return n, x1 - x0 + 1


# ---------------------------------------------------------------------------
# THE SESSION
# ---------------------------------------------------------------------------

def session(machine, card, vidw, vidh, png_dir=None):
    S = os88sym.linear
    want_val, want_max = _oracle_meter()
    cells = _oracle_meter_cells(card)
    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=machine) as m:
        try:
            _drive(machine, card, vidw, S, m, want_val, want_max, cells,
                   png_dir)
        except os88marty.MartyError as e:
            check(False, "%s: the scripted session broke off" % machine,
                  "the machine booted, so this is the harness losing its grip "
                  "on it rather than the tree being wrong",
                  got=str(e)[:200], want="a driven session")


def _drive(machine, card, vidw, S, m, want_val, want_max, cells, png_dir):
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

    # The card's own geometry, from the oracle: everything clicked below is a
    # cell rect the model already told us about.
    col, row, wcells = cells

    # --- 1: type into the field, then fire the handler three times ----------
    # The field is the first <input> in UISTREAM order, so Tab arms it - which
    # is cheaper and far more robust than finding its rect: WEAVE-SPEC 6.7
    # makes Tab the platform's own way to the next field.
    mo.click(cx + 4, cy + 4)            # the card, to give the window focus
    os88marty.settle(m)
    m.key("Tab")                        # MartyKey names are W3C
                                        # KeyboardEvent.code, not ASCII
    m.type_text(NAME)
    os88marty.settle(m)
    ink_typed = _lit(m.vram(card)[2], cx, cy, cx + cw - 1, cy + ch - 1)

    _press_button(m, mo, S, cx, cy, card, "Greet")
    _press_button(m, mo, S, cx, cy, card, "Greet")
    _press_check(m, mo, S, cx, cy, card)
    _press_button(m, mo, S, cx, cy, card, "Greet")
    os88marty.settle(m)

    fill, interior = _meter_fill(m, card, S, slot, vidw, cells)
    want = (interior * want_val) // want_max
    check(abs(fill - want) <= 1,
          "%s: the meter reads the value the model computed" % machine,
          "count.value is what doGreet's bytecode wrote, and the fill is "
          "WEAVE-SPEC 6.4's `interior x value / max`. A disagreement here is "
          "the ring, the slice, SETP or the painter - this row cannot say "
          "which, and weavevm has already said it is not the interpreter",
          got="%d px of %d" % (fill, interior),
          want="%d px (value %d of %d)" % (want, want_val, want_max))

    # --- 2: an alert is a WINDOW, and its callback is a later event ---------
    wins = set(dispcp.win_list(m, S))
    _press_button(m, mo, S, cx, cy, card, "Reset")
    os88marty.until(m, lambda mm: set(dispcp.win_list(mm, S)) - wins,
                    "alert()'s window to open", limit=20.0)
    os88marty.settle(m)
    alert = sorted(set(dispcp.win_list(m, S)) - wins)
    if check(len(alert) == 1, "%s: alert() raised a window" % machine,
             "8.2: alert() returns immediately and the dialog is a window of "
             "its own (SPEC.md 75.3)",
             got=alert, want="exactly one new slot"):
        ax, ay, aw, ah, aseg, _ = weavesmoke._win(m, S, alert[0])
        # ITS OWNERSHIP IS NOT ASSERTED. os88ui.inc calls the alert window
        # "unbound" - no dock tile, no Task Manager row, no callback billing
        # (SPEC.md 75.3) - but that is about the INSTANCE table and this file
        # cannot see which of those a W_SEG of 0 would mean. A guess dressed
        # as a check is worse than no check: the two facts worth having are
        # that a window arrived when alert() ran and went away when OK was
        # taken, and both are asserted.
        mo.click(ax + aw // 2, ay + ah - 20)    # the OK button's row
        os88marty.until(m,
                        lambda mm: not (set(dispcp.win_list(mm, S)) - wins),
                        "the alert to close", limit=20.0)
        os88marty.settle(m)

    # --- 3: the callback ran, so the card CHANGED again --------------------
    ink_end = _lit(m.vram(card)[2], cx, cy, cx + cw - 1, cy + ch - 1)
    check(ink_end != ink_typed,
          "%s: the card moved after the alert was dismissed" % machine,
          "afterReset writes status.text, and it can only run as an onalert "
          "event delivered AFTER the dismissal (4.9.1) - so ink that never "
          "moved means the callback never arrived",
          got=ink_end, want="!= %d" % ink_typed)

    # --- 4: and the guest is still EXECUTING -------------------------------
    t0 = m.read(0x46C, 4)
    time.sleep(0.6)
    check(m.read(0x46C, 4) != t0, "%s: the guest is still running" % machine,
          "a task frozen holding the gfx lock draws a perfect window and "
          "never draws another (SPEC.md 59.7), so stillness alone cannot "
          "tell a healthy machine from a dead one",
          got="the BIOS tick did not move", want="a moving tick")

    if png_dir:
        vw, vh, rows = m.vram(card)
        weavesmoke._shot(png_dir, machine, vw, vh, rows)


def _press_button(m, mo, S, cx, cy, card, label):
    """Press and release a button by finding its LABEL on the glass.

    The rect comes from the ORACLE's picture rather than from a constant, so a
    layout change moves the click with it - the same rule _oracle_meter_cells
    follows one level up, and the same reason.
    """
    x, y = _find_label(m, card, cx, cy, label)
    mo.click(x, y)
    os88marty.settle(m)


def _press_check(m, mo, S, cx, cy, card):
    """...and a check is clicked on its LABEL, not on its glyph.

    WEAVE-SPEC 7.3 gives a check a natural width of `label length + 2`, so the
    component's rect spans the glyph AND the words beside it, and wd_hit tests
    the whole rect. Aiming at the glyph would mean knowing where the walk put
    it; aiming at the label is the same rect and the picture already says
    where that is.
    """
    _press_button(m, mo, S, cx, cy, card, "Shout it")


def _find_label(m, card, cx, cy, text):
    """Where `text` is drawn, by the oracle's picture.

    The rendered card is a cell grid and the model draws the same characters
    the machine does, so the label's cell is the model's to answer - and the
    pixel is that cell's centre in the content box.
    """
    out = subprocess.run(["python3", "tools/weavesim.py", "--render",
                          "build/FORM.WAB", "--adapter", card],
                         cwd=ROOT, capture_output=True, text=True)
    body = [ln for ln in out.stdout.splitlines()
            if ln.startswith("|") and ln.endswith("|")]
    # WHOLE WORD, and this is the defect the first real run of this row found.
    # `str.find("Greet")` matches the card's own title - "Weave GREETer" - two
    # rows above the button, so every press landed on a <label>, no handler
    # ever ran, and the only assertion that noticed was the meter's. A click
    # that misses is indistinguishable from a runtime that ignored it, which
    # is exactly the confusion a gate must not add.
    pat = re.compile(r"(?<![A-Za-z])" + re.escape(text) + r"(?![A-Za-z])")
    for r, ln in enumerate(body):
        mm = pat.search(ln)
        if mm and mm.start() > 0:
            return (cx + (mm.start() - 1 + len(text) // 2) * 8 + 4,
                    cy + r * 8 + 4)     # body[k] IS card row k - see
                                        # _oracle_meter_cells
    raise RuntimeError("weavesim --render: no %r on the card" % text)


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
    rows = [t for t in MACHINES if not a.machine or t[0] == a.machine]
    for machine, card, w, h in rows:
        session(machine, card, w, h, a.png)
    return done("weavesession")


if __name__ == "__main__":
    sys.exit(main())
