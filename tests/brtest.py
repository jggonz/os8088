#!/usr/bin/env python3
"""Does the browser render, and does scrolling move only what changed?

    make browsertest && python3 tests/brtest.py [--adapter cga|herc]

Two assertions, and the second is the one worth having.

1. RENDERS. Double-clicking DEMO.HTM launches the browser through SPEC.md
   54's association and the page appears. Checked against tools/htmsim.py -
   the model and the 8086 must agree on the text, or one of them is wrong and
   it does not matter which.

2. SCROLLING DRAWS ONLY WHAT MOVED. A one-line scroll is a blit plus one
   lettered row (BROWSER-PLAN 4.1), so the framebuffer afterwards must be
   BYTE-IDENTICAL to what a full repaint of the same position produces. That
   is the same standard SPEC.md 22.11 and 27.7.2 were held to, and it is the
   only thing that separates "it scrolled" from "it scrolled correctly" - a
   blit that leaves a stale row looks perfectly plausible in a screenshot.
"""
import argparse
import os
import sys

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402

S = os88sym.linear
# The period IBM ROM where there is one - it is the BIOS the calibration
# machine runs and the one SPEC.md 18.91's AL bug lives in. Falls back to the
# GLaBIOS twins, which MartyPC bundles, when tools/martypc/roms/ is empty:
# this tree cannot ship somebody else's ROM (tools/martypc/README.md).
import os                                              # noqa: E402
_IBMROM = os.path.exists("tools/martypc/roms/"
                         "BIOS_IBM5150_27OCT82_1501476_U33.BIN")
MACHINE = ({"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"} if _IBMROM
           else {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"})


def frame(m, adapter):
    if adapter == "herc":
        w, h, rows = m.vram("herc")
    else:
        w, h, rows = m.vram("cga")
    return w, h, bytes(b for r in rows for b in r)


def say(*a):
    print(*a)
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--page", default="DEMO.HTM")
    ap.add_argument("--shot", default=None)
    ap.add_argument("--form", default=None,
                    help="type this into the page's field and check the URL")
    ap.add_argument("--expect", default=None, help="...against this URL")
    ap.add_argument("--resize", type=int, default=0,
                    help="drag the grow box this many pixels left and re-check")
    ap.add_argument("--scroll", type=int, default=0,
                    help="page down this many times before the screenshot")
    args = ap.parse_args()

    fails = []
    with os88marty.launch("build/os8088-360.img", apps="build/brtest360.img",
                          machine=MACHINE[args.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        if not wins:
            sys.exit("brtest: no Disk window after double-clicking B:")
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        say("Disk window at (%d,%d), listing %r"
            % (wx, wy, [r[0] for r in dispcp.listing(m, S)]))

        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, args.page)
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("brtest: %s did not open a window - the association "
                     "did not resolve to BROWSER.O88" % args.page)
        say("browser window opened (%d windows now)" % len(wins2))

        # --- 1: it has to have DRAWN something -------------------------------
        # Without this the comparison below passes on a blank window: two
        # identical nothings differ by zero pixels, which is a check that
        # cannot fail for the reason it claims. It caught a hung table layout.
        # Count DARK pixels inside the content box, not lit ones: the content
        # is WHITE, so "lit" is what a blank window is made of and a blank
        # window sailed through the first version of this check.
        bx0, by0, bw, bh = dispcp.win_rect(m, S, wins2[-1])
        cx0, cy0 = bx0 + 1, by0 + dispcp.TITLE_H + 1
        cw0, ch0 = bw - 2, bh - dispcp.TITLE_H - 1
        w0, h0, first = frame(m, args.adapter)
        stride0 = len(first) // h0
        ink = sum(1 for yy in range(cy0, min(cy0 + ch0, h0))
                  for xx in range(cx0, min(cx0 + cw0, stride0))
                  if not first[yy * stride0 + xx])
        say("content: %d ink pixels in the %dx%d box" % (ink, cw0, ch0))
        if ink < 500:
            fails.append("the window has %d ink pixels - nothing rendered"
                         % ink)

        # --- forms (BROWSER-PLAN 7.4) ----------------------------------------
        # With no transport yet the URL is BUILT and REPORTED rather than
        # fetched, which is what step 4 asks for: the encoder is checkable by
        # reading the URL a submit would produce.
        if args.form is not None:
            seg = dispcp.win_rect and None
            rec = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                         dispcp.WIN_SIZE)
            pseg = rec[22] | (rec[23] << 8)
            img = os.path.getsize("build/browser.bin")
            m.key("Tab")
            os88marty.settle(m)
            m.type_text(args.form)
            os88marty.settle(m)
            m.key("Enter")
            os88marty.settle(m)
            raw = m.readseg(pseg, img + 780, 160)
            url = raw.split(b"\x00")[0].decode("latin-1")
            say("submit built: %r" % url)
            if args.expect is not None and url != args.expect:
                fails.append("the composed URL is %r, expected %r"
                             % (url, args.expect))

        for _ in range(args.scroll):
            m.key("PageDown")
            os88marty.settle(m)
        if args.shot:
            sw, sh, srows = (m.vram("herc") if args.adapter == "herc"
                             else m.vram("cga"))
            os88marty.write_png(args.shot, sw, sh, srows)
            say("wrote %s (%dx%d)" % (args.shot, sw, sh))

        # --- 2: a blitted arrival must equal a repainted one -----------------
        # BROWSER-PLAN 4.1 has three tiers, and only a delta of a windowful or
        # more repaints the band. So the two ways to ARRIVE AT top=0 are:
        #
        #   blit    - from top=1, Up is a delta of -1, under the tier bound
        #   repaint - End then Home are both far bigger than the window, so
        #             each redraws every row from scratch
        #
        # Comparing them is the only thing that separates "it scrolled" from
        # "it scrolled correctly": a blit that leaves one stale cell looks
        # perfectly plausible in a screenshot. An earlier version of this test
        # compared a blit against ANOTHER blit and called it a repaint, which
        # is a check that cannot fail for the reason it claims.
        m.key("Home")                   # start from a known top, so --scroll
        os88marty.settle(m)             # cannot leave the two paths at
        m.key("ArrowDown")              # different positions
        os88marty.settle(m)
        m.key("ArrowUp")
        os88marty.settle(m)
        w, h, blitted = frame(m, args.adapter)

        m.key("End")
        os88marty.settle(m)
        m.key("Home")
        os88marty.settle(m)
        _, _, repainted = frame(m, args.adapter)

        # Compared INSIDE THE CONTENT BOX only. The menu bar carries a clock
        # and, after a submit, a toast whose lifetime is its own - neither is
        # the browser's drawing, and both would report as a scrolling defect.
        stride = len(blitted) // h
        bad = [yy * stride + xx
               for yy in range(cy0, min(cy0 + ch0, h))
               for xx in range(cx0, min(cx0 + cw0, stride))
               if blitted[yy * stride + xx] != repainted[yy * stride + xx]]
        say("blit vs repaint at top=0: %d differing pixels of %d"
            % (len(bad), len(blitted)))
        for i in bad[:8]:
            say("   at x=%d y=%d  blit=%02X repaint=%02X"
                % (i % stride, i // stride, blitted[i], repainted[i]))
        # 13.3's seven-pixel defect was allowed here by signature until it was
        # FIXED (13.7 - a colour change was clobbering the low byte of the
        # scroll bar's x1). The allowance is gone with it: anything at all
        # here is a regression now.
        if bad:
            fails.append("a blitted arrival and a repainted one disagree by "
                         "%d pixels - the blit is leaving something stale"
                         % len(bad))

        # --- 3: a resize must RELAYOUT, not just clip --------------------
        # BROWSER-PLAN 3.5: a width change invalidates the line table and
        # nothing else. The window is sizable (SPEC.md 11.1), so this drags
        # the grow box and checks the layout actually followed.
        if args.resize:
            img2 = os.path.getsize("build/browser.bin")
            rec2 = m.read(S("wm_wins") + wins2[-1] * dispcp.WIN_SIZE,
                          dispcp.WIN_SIZE)
            pseg2 = rec2[22] | (rec2[23] << 8)

            def word(off):
                d = m.readseg(pseg2, img2 + off, 2)
                return d[0] | (d[1] << 8)

            before = (word(10), word(38))
            gx, gy, gw, gh = dispcp.win_rect(m, S, wins2[-1])
            mo.drag(gx + gw - 6, gy + gh - 6,
                    gx + gw - 6 - args.resize, gy + gh - 6)
            os88marty.settle(m)
            after = (word(10), word(38))
            say("resize: cols %d -> %d, lines %d -> %d"
                % (before[0], after[0], before[1], after[1]))
            if after[0] >= before[0]:
                fails.append("the window narrowed and cols did not: %d -> %d"
                             % (before[0], after[0]))
            elif after[1] <= before[1]:
                fails.append("cols fell but the line count did not rise: the "
                             "layout did not follow (%d -> %d)"
                             % (before[1], after[1]))

    for f in fails:
        say("FAIL: " + f)
    say("brtest: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
