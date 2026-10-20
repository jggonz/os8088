#!/usr/bin/env python3
"""THE DESKTOP COMES BACK WHOLE FROM AN fsx BRACKET ENTERED FROM A CLICK
HANDLER (SPEC.md 53.1.1), on MartyPC.

    python3 tests/fsxclip.py [--machine os8088_5150_herc_gla]

A clip region dies at the arming task's next gfx_unlock, and a bracket runs
INSIDE that hold - so a package that armed one to draw in its click handler
and then entered a bracket used to hand the kernel its own content rect as
the clip for fsx_restore's repaint of the DESKTOP. CLEAR SKIES' Fly button
is exactly that shape, and the desktop came back with no background, no menu
bar, no dock and no drive icons while every window drew correctly (wm_paint
arms a region per window; nothing else in wm_paint_all does).

So the row drives the BUTTON and not the `f` key - the key and the menu arm
no region, which is why this looked like a fault in the launcher for as long
as it did. The assertion is the desktop's own pixels: three bands that no
window covers - the menu bar, a strip of desktop background beside the
windows, and the dock's row - captured before the flight and again after it,
and held to each other. The clock is left out of the bar's band: it repaints
itself on its own schedule and a minute may pass in between.

--clobber-clip is the red run (docs/WRITING-TESTS.md 1): it patches
fsx_run's `call wm_clip_clear` to three NOPs, which is the kernel before the
fix, and the row must then fail on all three bands.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLOCK_W = 200                       # the bar's right end, left out of its band
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def band(fb, w, y0, y1, x0, x1):
    """The pixels of rows y0..y1, columns x0..x1, out of a packed rgb24 grab."""
    out = bytearray()
    for y in range(y0, y1):
        out += fb[(y * w + x0) * 3:(y * w + x1) * 3]
    return bytes(out)


def lit(b):
    return sum(1 for i in range(0, len(b), 3) if b[i:i + 3] != b"\0\0\0")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-clip", action="store_true",
                    help="NOP fsx_run's wm_clip_clear: the row must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        m.advance(frames=40)
        m.run()

        if a.clobber_clip:
            lo = os88sym.linear("fsx_run")
            code = m.read(lo, 0x200)
            want = os88sym.linear("wm_clip_clear")
            site = None
            for i in range(len(code) - 3):
                if code[i] == 0xE8:
                    rel = int.from_bytes(code[i + 1:i + 3], "little")
                    rel = rel - 0x10000 if rel >= 0x8000 else rel
                    if lo + i + 3 + rel == want:
                        site = lo + i
                        break
            if site is None:
                sys.exit("fsxclip: fsx_run does not call wm_clip_clear - there "
                         "is nothing to put back")
            m.pause()
            m.write(site, b"\x90\x90\x90")
            m.run()
            print("  (fsx_run's wm_clip_clear NOPed: this run must fail)")

        # --- the three bands, from the guest's own geometry -------------------
        S = os88sym.linear
        vw = int.from_bytes(m.read(S("vid_w"), 2), "little")
        vh = int.from_bytes(m.read(S("vid_h"), 2), "little")
        dock = int.from_bytes(m.read(S("vid_dock_y0"), 2), "little")
        print("  screen %dx%d, the dock's first row %d" % (vw, vh, dock))
        m.pause()
        fw, fh, before = m.fbuf(0)
        m.run()
        BANDS = (("the menu bar", 0, 18, 0, vw - CLOCK_W),
                 ("the desktop background", 30, dock - 10, 0, 90),
                 ("the dock's row", dock, min(vh, fh), 0, vw))
        was = {n: band(before, fw, y0, y1, x0, x1) for n, y0, y1, x0, x1 in BANDS}
        for n, y0, y1, x0, x1 in BANDS:
            check(lit(was[n]) > 0, "%s has something on it before the flight "
                                   "(%d lit)" % (n, lit(was[n])))

        # --- into the bracket THROUGH THE BUTTON, and out again ---------------
        r = [int.from_bytes(m.readseg(seg, mp["cs_flyrect"] + 2 * i, 2), "little")
             for i in range(4)]
        check(r[2] > r[0] and r[3] > r[1], "the painter wrote the Fly button's rect %s" % r)
        ui.mo.click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        m.advance(frames=150)
        m.run()
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")
        back = m.readseg(seg, base + dispapps.bss_off("skies", "cs_back"), 1)[0]
        check(back != 0, "the Fly button entered the bracket (cs_back %d)" % back)
        m.type_text("f")
        m.advance(frames=250)
        m.run()
        m.pause()
        fw, fh, after = m.fbuf(0)
        m.run()

        for n, y0, y1, x0, x1 in BANDS:
            now = band(after, fw, y0, y1, x0, x1)
            same = sum(1 for i in range(0, len(now), 3)
                       if now[i:i + 3] == was[n][i:i + 3])
            tot = len(now) // 3
            check(same == tot,
                  "%s came back exactly as it went (%d of %d pixels, %d lit "
                  "against %d)" % (n, same, tot, lit(now), lit(was[n])))

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
