#!/usr/bin/env python3
"""How much of the figure is on the glass, frame by frame (SPEC.md 78.5).

    make wiredisk && python3 tests/wireflick.py [--machine os8088_5150_cga_gla]

WIREFRAME DOES NOT SHIP (SPEC.md 78.9), so `make wiredisk` is what puts
WIRE.O88 on a floppy for this to open. `make` alone still builds the
package - it just no longer lands on the apps disk.

`m.flicker()` is the wrong instrument here and says so: it needs the screen to
SETTLE, and the whole point of this window is that it never does again. So
this measures the thing a person actually reacts to instead - **the figure
going away** - by sampling the object area once per displayed frame and
counting its ink.

A complete cube is some number of lit pixels. A frame caught between the erase
and the draw has fewer, and how many fewer, how often, is the flicker. The
numbers below are that distribution per draw order: the FLOOR is the emptiest
frame a viewer sees and `blank` is the fraction of frames under half full.

It is a MEASUREMENT and not a gate. 78.5's three orders are a trade with no
free corner, so there is no threshold to assert - the point is to put numbers
on a choice the reader makes by looking.

**ON THE GLaBIOS TWIN**, `os8088_5150_herc_gla`, because `os8088_5150_herc`
wants the IBM ROM this repo cannot ship. Checked with that ROM dropped in
beside GLaBIOS: the frame rates come out the SAME on both - 18.2, 18.2, 13.1,
18.2 down the four draw orders - and the ink percentages move by a few points
either way, which is this row sampling a free-running animation and not the
BIOS.
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
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WR_OX0, WR_OY0, WR_OW, WR_OH = 16, 18, 20, 22       # apps/wire's own bss
MENU_DRAW = (0, 0)                                  # menu 2's three items
ORDERS = ["Whole figure", "Edge at a time", "Edge, then repair",
          "Composed"]                          # SPEC.md 78.8


def ink(m, rig_seg, geom, fbseg, stride, banks):
    """Lit pixels inside the object area, from one framebuffer sample."""
    x0, y0, w, h = geom
    fb = m.read(fbseg << 4, banks * 0x2000)
    n = 0
    b0, b1 = x0 >> 3, (x0 + w + 7) >> 3
    bm = banks - 1
    sh = {2: 1, 4: 2}[banks]
    for y in range(y0, y0 + h):
        base = (y & bm) * 0x2000 + (y >> sh) * stride
        for b in range(b0, b1):
            n += 8 - bin(fb[base + b]).count("1")        # ink is BLACK: a 0 bit
    return n


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/wire360.img")
    ap.add_argument("--frames", type=int, default=48)
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("WIRE.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=200)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("wireflick: WIRE.O88 did not open")
        seg = got[1]
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")
        stride = int.from_bytes(m.read(S("vid_stride"), 2), "little")
        fbseg = int.from_bytes(m.read(S("vid_rseg"), 2), "little")
        banks = (int.from_bytes(m.read(S("vid_wrapbit"), 2), "little")
                 or 0x8000) // 0x2000
        geom = tuple(int.from_bytes(m.readseg(seg, base + o, 2), "little")
                     for o in (WR_OX0, WR_OY0, WR_OW, WR_OH))
        print("  object area %r, framebuffer %04x, %d banks of %d"
              % (geom, fbseg, banks, stride))

        out = []
        for mode, name in enumerate(ORDERS):
            m.pause()
            m.write((seg << 4) + base + 39, bytes([mode]))   # wr_mode
            m.run()
            m.advance(frames=110)               # let the order take effect,
            m.run()                             # and wr_fps re-settle
            samples = []
            for _ in range(a.frames):
                m.advance(frames=1)
                m.pause()
                samples.append(ink(m, seg, geom, fbseg, stride, banks))
                m.run()
            full = max(samples)
            floor = min(samples)
            mean = sum(samples) / float(len(samples))
            blank = sum(1 for s in samples if s < full * 0.5) / float(len(samples))
            m.pause()
            fps = int.from_bytes(m.readseg(seg, base + 14, 2), "little") / 10.0
            m.run()
            out.append((name, full, floor, mean, blank, fps))
            print("  %-20s full %4d  floor %4d (%3.0f%%)  mean %5.1f (%3.0f%%)"
                  "  under half %3.0f%%   %4.1f fps"
                  % (name, full, floor, 100.0 * floor / full, mean,
                     100.0 * mean / full, 100.0 * blank, fps))

    print()
    print("| draw order | emptiest frame | mean ink | under half full | fps |")
    print("|---|---:|---:|---:|---:|")
    for name, full, floor, mean, blank, fps in out:
        print("| %s | %.0f%% | %.0f%% | %.0f%% | %.1f |"
              % (name, 100.0 * floor / full, 100.0 * mean / full,
                 100.0 * blank, fps))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
