#!/usr/bin/env python3
"""Is changing adapter and changing back the IDENTITY on every window rect?
(SPEC.md 39.11.2.1)

    make && python3 tests/dispfit.py

`wm_fit` is a clamp, so it takes `min` on every axis, and running it a second
time against a bigger screen cannot undo what it did against a smaller one.
That made `vid_switch` one-way: VGA -> CGA shrank every window and CGA -> VGA
did nothing at all, so the Task Manager came back 155 rows tall on a 480-row
screen - **and drew the 284 rows' worth its own layout still believed in
straight through the bottom of its frame**, the `gfx_*` primitives clipping to
the screen rather than to the window (SPEC.md 11.3). The natural bank is the
fix: the rect a window ASKED for is kept beside it and replayed here.

THIS IS A SINGLE-CARD TEST and that is deliberate - it is the machine the
fault was reported on. A VGA answers for the CGA as well (SPEC.md 39.11.1: a
VGA/EGA does mode 6 too), so `os8088_xt_vga` offers both adapter rows on the
Display page with one card in the box, and no dual-display config is needed.

THREE WINDOWS, because the rect is decided in three places and each banks at a
different site: a window left at its TEMPLATE (wm_create's bank), one DRAGGED
(ui_drag's), and the same one GROWN (ui_grow's). A gate that only opened a
window would pass with two of the three sites missing.

...and it compares the rects with THEMSELVES either side of the round trip
rather than against predicted numbers. What is being asserted is that the
switch is reversible; what any particular clamp produces on the way is
`wm_fit`'s business and would only bake this file into it.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

TITLE_H = 18
VID_VGA, VID_CGA = 0, 2
S = os88sym.linear


def rects(m, slots):
    return dict((s, dispcp.win_rect(m, S, s)) for s in slots)


def kind(m):
    return m.read(S("vid_kind"), 1)[0]


def switch_to(m, mo, settle, slot, want, tries=4):
    """dispcp.set_primary, retried until [vid_kind] agrees.

    A packet sent while the previous one is still in flight through the
    1200-baud UART is DROPPED - SPEC.md 9.4.3's warning seen from the other
    side - and os88mouse raises rather than clicking into empty desktop. It is
    transient, and a gate that does not absorb it reports a working kernel as
    broken about one run in four.

    The retry is safe because both clicks are idempotent: clicking the radio
    already selected draws nothing (cp_vid_click's own `je .done`), and
    Activate on the adapter already running is greyed and refused by
    cpf_vidok. A forced release first, because the dropped packet is the one
    that would have lifted the button.
    """
    for _ in range(tries):
        try:
            dispcp.set_primary(m, mo, S, settle, slot)
        except os88marty.MartyError:
            m.mouse(l=False)
            settle(m)
        if kind(m) == want:
            return
    sys.exit("dispfit: the switch to kind %d never happened (kind %d) - see "
             "the Display page, not this gate" % (want, kind(m)))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        mo = os88mouse.Mouse(marty=m)
        if kind(m) != VID_VGA:
            sys.exit("dispfit: %s did not come up on the VGA" % a.machine)

        # --- a window at its TEMPLATE, and one the user has moved and sized ---
        dispcp.open_drive(m, mo, S, settle, "A")
        dispcp.open_drive(m, mo, S, settle, "B")
        w = dispcp.win_list(m, S)
        if len(w) != 2:
            sys.exit("dispfit: wanted two Disk windows, got %r" % (w,))
        keep, moved = w[0], w[1]

        x, y, ww, wh = dispcp.win_rect(m, S, moved)
        mo.drag(x + ww // 2, y + TITLE_H // 2, x + ww // 2 + 70,
                y + TITLE_H // 2 + 60)
        settle(m)
        x, y, ww, wh = dispcp.win_rect(m, S, moved)
        mo.drag(x + ww - 8, y + wh - 8, x + ww - 8 + 60, y + wh - 8 + 40)
        settle(m)
        say("template %r, dragged+grown %r"
            % (dispcp.win_rect(m, S, keep), dispcp.win_rect(m, S, moved)))

        # --- VGA -> CGA -> VGA -----------------------------------------------
        dispcp.open_panel(m, mo, S, settle)
        slots = dispcp.win_list(m, S)
        before = rects(m, slots)
        say("VGA    %r" % (before,))

        switch_to(m, mo, settle, 1, VID_CGA)                # the Cga row
        during = rects(m, slots)
        say("CGA    %r" % (during,))
        if during == before:
            sys.exit("dispfit: the CGA clamped nothing, so a round trip that "
                     "comes back unchanged proves nothing - this machine's "
                     "windows all fit 200 rows already")

        switch_to(m, mo, settle, 0, VID_VGA)                # ...and the Vga
        after = rects(m, slots)
        say("VGA    %r" % (after,))

        for s in sorted(before):
            if after[s] != before[s]:
                fail.append("window %d came back %r, was %r - the clamp is "
                            "still one-way (SPEC.md 39.11.2.1)"
                            % (s, after[s], before[s]))

    print()
    for f in fail:
        print("dispfit: FAIL: %s" % f)
    if fail:
        return 1
    print("dispfit: a template, a drag and a grow all survive "
          "VGA -> CGA -> VGA unchanged - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
