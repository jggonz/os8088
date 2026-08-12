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

...and the dragged one is driven TWICE, the second time with WF_NOSNAP poked
into its record from outside the guest. That is SPEC.md 11.96.13's own
counterfactual (see the commit that measured it), and without it this file
cannot see the ordering it exists to protect: `ui_drag_phase` runs after
ui_drag's clamps and moves W_X by up to 7px, so ui_drag's bank has to sit
BELOW it - but alignment is the default now, `wm_snap_ax` has already put W_X
on the right phase, and for an ordinary window the snap moves nothing. So the
bank in the wrong place passes every test that only drags ordinary windows,
which is exactly what happened when these two arrived from different branches
and were merged. An opted-out window is where the phase is a real coin toss.

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
import os88geom                                             # noqa: E402

TITLE_H = 18
VID_VGA, VID_CGA = 0, 2
KSEG_BASE = 0x600            # KERNEL_SEG << 4: os88sym answers LINEAR,
                             # and W_ONSIZE-family handlers are NEAR offsets
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

        # ...and dragged again with the phase snap ARMED (see the docstring):
        # WF_NOSNAP makes wm_snap_ax decline, so the drop lands on an arbitrary
        # x and ui_drag_phase really moves it - which is the only way this file
        # can tell ui_drag's bank being below that call from being above it.
        rec = S("wm_wins") + moved * dispcp.WIN_SIZE
        fl = int.from_bytes(m.read(rec + dispcp.W_FLAGS, 2), "little")
        m.write(rec + dispcp.W_FLAGS,
                (fl | os88geom.WF_NOSNAP).to_bytes(2, "little"))
        x, y, ww, wh = dispcp.win_rect(m, S, moved)
        mo.drag(x + ww // 2, y + TITLE_H // 2, x + ww // 2 + 13,
                y + TITLE_H // 2 + 11)
        settle(m)
        say("...and re-dragged un-snapped to %r"
            % (dispcp.win_rect(m, S, moved),))

        # --- and SPEC.md 11.98: is the window TOLD? -------------------------
        # There is no shipped app with a size-changed handler, so the handler
        # is written INTO the guest as nine bytes of machine code and pointed
        # at from wm_onsz. That drives the real path - the real slot table, the
        # real compare, wm_pkgcall's `call bp` for a kernel window - where a
        # stub inside the kernel would be a different route with the same name.
        #
        # It lives in the wm_natr entries of two window slots that are not in
        # use, and records into a wm_zoomr entry of a third. Nothing reads
        # those: wm_refit visits used records only, wm_create banks into the
        # one slot it fills, wm_destroy clears the one it frees. Do not open
        # more windows after this point.
        code_at = S("wm_natr") + 10 * 8 - KSEG_BASE     # a NEAR offset: the
        rec_at = S("wm_zoomr") + 10 * 8 - KSEG_BASE     # handler is called in
        handler = (b"\x89\x0e" + rec_at.to_bytes(2, "little") +      # mov [],cx
                   b"\x89\x16" + (rec_at + 2).to_bytes(2, "little")  # mov [],dx
                   + b"\xc3")                                        # ret
        m.write(S("wm_natr") + 10 * 8, handler)
        m.write(S("wm_zoomr") + 10 * 8, b"\0\0\0\0")
        m.write(S("wm_onsz") + keep * 2, code_at.to_bytes(2, "little"))
        say("handler for window %d poked at %04x, recording to %04x"
            % (keep, code_at, rec_at))

        # --- VGA -> CGA -> VGA -----------------------------------------------
        dispcp.open_panel(m, mo, S, settle)
        slots = dispcp.win_list(m, S)
        before = rects(m, slots)
        say("VGA    %r" % (before,))

        switch_to(m, mo, settle, 1, VID_CGA)                # the Cga row
        during = rects(m, slots)
        say("CGA    %r" % (during,))

        got = m.read(S("wm_zoomr") + 10 * 8, 4)
        gw = int.from_bytes(got[0:2], "little")
        gh = int.from_bytes(got[2:4], "little")
        want = (during[keep][2] - 2, during[keep][3] - TITLE_H - 1)
        say("handler was called with content %dx%d (the window is %dx%d)"
            % (gw, gh, want[0], want[1]))
        if (gw, gh) == (0, 0):
            fail.append("the size-changed handler never ran, and window %d's "
                        "content box moved %r -> %r (SPEC.md 11.98)"
                        % (keep, before[keep][2:], during[keep][2:]))
        elif (gw, gh) != want:
            fail.append("the handler got %dx%d, but the window's content box "
                        "is %dx%d (SPEC.md 11.98)" % (gw, gh, want[0], want[1]))
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
