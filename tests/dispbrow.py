#!/usr/bin/env python3
"""apps/browser walked across the seam, with the kernel's own bookkeeping
printed beside the rect at every step.

    make && python3 tests/dispbrow.py

Reported from the field in three parts, on the field machine's own pair of
cards: a drag that moves the window NOT AT ALL, then one that moves it and
does not resize it, then one clear across that lands it against the edge
"chopped thin in the X direction".

Browser is the only window in the tree that is all four things at once -
WF_SIZABLE, a published per-adapter preference, WF_KEEPH over the dock
(SPEC.md 11.93), and an 11.98 handler that calls back INTO the kernel's own
sizing sequence (br_onresize -> br_keeph -> OSAPI_WM_KEEPH -> wm_apply_size)
from inside the notification wm_land_fit sends. So this prints what each of
them is holding rather than guessing which one moved: the rect, the natural
bank a drag re-derives its size from (SPEC.md 11.100.3), the user-sized bit
that outranks a preference (11.100.5), the adapter it was last sized for
(11.100.4) and W_FLAGS.

It PRINTS and asserts only the things the report is unambiguous about, since
two of the three symptoms are about what the user saw rather than a number.
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
import dispapps                                             # noqa: E402
import os88geom                                             # noqa: E402
import dispsize                                             # noqa: E402

TITLE_H = 18
S = os88sym.linear
u16 = dispsize.u16


def state(m, slot):
    x, y, w, h = dispcp.win_rect(m, S, slot)
    nx, ny, nw, nh = dispsize.bank(m, slot)
    flags = u16(m.read(S("wm_wins") + slot * os88geom.WIN_SIZE +
                       os88geom.W_FLAGS, 2))
    usr = m.read(S("wm_usrsz") + slot, 1)[0]
    kind = m.read(S("wm_pkind") + slot, 1)[0]
    return dict(x=x, y=y, w=w, h=h, nw=nw, nh=nh, usr=usr, kind=kind,
                flags=flags)


def show(tag, s):
    print("  %-32s (%4d,%3d) %3dx%-3d   bank %3dx%-3d  usr=%d pkind=%d "
          "flags=%04X" % (tag, s["x"], s["y"], s["w"], s["h"], s["nw"],
                          s["nh"], s["usr"], s["kind"], s["flags"]))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)

    fail = []
    settle = os88marty.settle
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispsize.extend(m, mo, settle)

        c0, c1 = dispsize.ctx(m, 0), dispsize.ctx(m, 1)
        seam = u16(c1, dispsize.VID_CTX_VX)
        cga_w = u16(c1, dispsize.VID_CTX_CW)
        print("  display 0 %dx%d, display 1 %dx%d at (%d,%d)\n"
              % (u16(c0, dispsize.VID_CTX_CW), u16(c0, dispsize.VID_CTX_CH),
                 cga_w, u16(c1, dispsize.VID_CTX_CH), seam,
                 u16(c1, dispsize.VID_CTX_VY)))

        dispcp.open_drive(m, mo, S, settle, "B", card=0)
        dslot = dispcp.win_list(m, S)[-1]   # open_drive answers the ZONE's
                                            # (x, y) and not a slot
        dx, dy, dw, dh = dispcp.win_rect(m, S, dslot)
        for step in ("APPS", "BROWSER.O88"):
            dispcp.open_named(m, mo, S, settle, dx, dy, step, card=0)
            settle(m, card=0)
            dx, dy, dw, dh = dispcp.win_rect(m, S, dslot)

        bslot = bseg = None
        for n in range(os88geom.MAX_WIN):
            got = dispapps.pkg_seg(m, n)
            if got is None:
                break
            if m.read((got[1] << 4) + 16, 16).split(b"\0")[0] == b"BROWSER":
                bslot, bseg = got
                break
        if bslot is None:
            sys.exit("dispbrow: the Browser package window is not open")

        s0 = state(m, bslot)
        show("opened on the Hercules", s0)

        # 1. A DRAG THAT STAYS ON THE HERCULES. Nothing may resize (SPEC.md
        #    11.100.3's "moved within A"), and it MUST move.
        mo.drag(s0["x"] + s0["w"] // 2, s0["y"] + TITLE_H // 2,
                60 + s0["w"] // 2, 60)
        settle(m, card=0)
        s1 = state(m, bslot)
        show("...dragged, same card", s1)
        if (s1["x"], s1["y"]) == (s0["x"], s0["y"]):
            fail.append("a drag INSIDE one display moved the window not at "
                        "all - it is still at (%d,%d)" % (s1["x"], s1["y"]))
        if (s1["w"], s1["h"]) != (s0["w"], s0["h"]):
            fail.append("a drag inside ONE display resized the window "
                        "%dx%d -> %dx%d; only a change of adapter may "
                        "(SPEC.md 11.100.3)"
                        % (s0["w"], s0["h"], s1["w"], s1["h"]))

        # 2. ONTO THE SEAM: the restrictive card, so the CGA's height.
        mo.drag(s1["x"] + s1["w"] // 2, s1["y"] + TITLE_H // 2,
                seam - s1["w"] // 3, s1["y"] + TITLE_H // 2)
        settle(m, card=1)
        s2 = state(m, bslot)
        show("...pushed onto the SEAM", s2)

        # 3. AND CLEAR ACROSS. The width is what the report is unambiguous
        #    about: br_pref publishes 496 on every adapter and the CGA is 640
        #    wide, so nothing here has any business cutting it.
        mo.drag(s2["x"] + s2["w"] // 2, s2["y"] + TITLE_H // 2,
                seam + 40 + s2["w"] // 2, s2["y"] + TITLE_H // 2)
        settle(m, card=1)
        s3 = state(m, bslot)
        show("...dragged fully onto the CGA", s3)
        if s3["w"] < s0["w"] and s3["w"] < cga_w:
            fail.append("the window came out %d wide on a card that is %d "
                        "wide, from %d - nothing may cut a width that FITS "
                        "(the field's 'chopped thin in the X direction')"
                        % (s3["w"], cga_w, s0["w"]))
        if s3["x"] + s3["w"] > seam + cga_w:
            fail.append("it ends at %d, past display 1's right edge %d"
                        % (s3["x"] + s3["w"], seam + cga_w))

        # 4. ...AND HOME, which is what says the bank still holds the big one.
        mo.drag(s3["x"] + s3["w"] // 2, s3["y"] + TITLE_H // 2,
                80 + s3["w"] // 2, 60)
        settle(m, card=0)
        s4 = state(m, bslot)
        show("...and back to the Hercules", s4)
        if (s4["w"], s4["h"]) != (s0["w"], s0["h"]):
            fail.append("it came home %dx%d and left at %dx%d - the natural "
                        "bank is what puts a clamp back (SPEC.md 39.11.2.1)"
                        % (s4["w"], s4["h"], s0["w"], s0["h"]))

    print()
    for f in fail:
        print("dispbrow: FAIL: " + f)
    if fail:
        return 1
    print("dispbrow: browser moves when dragged, keeps a width that fits, and "
          "comes home the size it left - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
