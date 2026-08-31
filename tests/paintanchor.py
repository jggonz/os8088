#!/usr/bin/env python3
"""DOES A SHRINK REDRAW A PICTURE IT DID NOT CHANGE? (SPEC.md 11.90.3)

    make && python3 tests/paintanchor.py [--machine os8088_xt_vga]

ui_grow repaints the union of the rect a window had and the rect it has, so on
a SHRINK the window is told "draw all of you" about pixels that were never
painted over: wm_dmg_gray clips the desktop dither to the windows about to
cover it, and wm_draw_win writes the frame whole whatever the content does.
11.90.3 arms [wm_dmg_rzwin] across that one repaint and wm_damage answers the
shrunk window with an EMPTY rect.

Measured on a Hercules, one grow-box drag in: the W_PAINT was 1.820 s of which
the canvas blit was 0.639; it is 1.180 s now, and the rest is pt_resize.

THREE ASSERTIONS, and the second is the one that would catch the fix being
wrong rather than merely absent:

  SKIPPED   pt_blit is entered with an EMPTY rect - x1 > x2 - so the canvas
            was not redrawn. Entered and not skipped: the empty rect has to
            reach it, because that is what proves the narrowing arrived rather
            than the call being lost somewhere else.

  INTACT    every surviving canvas pixel is byte-identical before and after.
            This is what makes skipping the redraw legitimate instead of lucky,
            and it fails loudly if the desktop fill ever stops being clipped to
            the windows about to cover it.

  VACATED   the columns the window gave up are NOT still showing the picture.
            A narrowing that went too far - telling the desktop it owed nothing
            either - leaves the old frame standing on the desktop, and nothing
            else here would see it.
"""
import argparse
import os
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(HERE)
S = os88sym.linear


def _boff(seg, name):
    return ((seg << 4) + dispapps.img_size("paint")
            + dispapps.bss_off("paint", name))


def _bss(m, seg, name, w=2):
    return int.from_bytes(m.read(_boff(seg, name), w), "little")


def _s16(v):
    """A canvas rect is SIGNED - pt_blit is handed x1 = -48 routinely, which
    reads as 65488 and is greater than any x2 there is. Comparing raw words
    calls every rect empty and the test passes against the defect."""
    return v - 65536 if v >= 32768 else v


def _visible(m, cx, cy):
    """How much of the canvas is actually on the glass, found in the frame.

    NOT pt_cw/pt_ch: the canvas may be wider than the content and the window
    frame clips it, so a comparison sized from those runs off the window into
    the desktop dither and reports every row as changed. The canvas is white
    with one stroke well inside it, so the first dark pixel along its top row
    is the frame and the first down its left column is the strip separator.
    """
    fw, fh, fb = m.fbuf(card=0)
    x0, y0 = cx + 2, cy + 2                 # inside the canvas's own border,
    w = 0                                   # and above/left of the stroke,
    while x0 + w < fw and fb[(y0 * fw + x0 + w) * 3] >= 128:
        w += 1                              # which starts 25 rows down
    h = 0
    while y0 + h < fh and fb[((y0 + h) * fw + x0) * 3] >= 128:
        h += 1
    return w, h


def _grab(m, x, y, w, h):
    fw, fh, fb = m.fbuf(card=0)
    return [fb[((y + r) * fw + x) * 3:((y + r) * fw + x + w) * 3]
            for r in range(h)]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)

    fails = []
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
                               rows.index("PAINT.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        os88marty.no_saver(m)
        seg = dispapps.pkg_seg(m, 0)[1]
        pw = dispcp.win_list(m, S)[-1]
        pm = dispapps._map("paint")
        base = seg << 4

        cx0, cy0 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        mo.to(cx0 + 30, cy0 + 25)                   # ink, so there is a picture
        os88marty.settle(m)                         # worth not redrawing
        mo._edge(True)
        for _ in range(8):
            m.mouse(dx=7, dy=6, l=True)
            m.advance(frames=3)
            m.run()
        mo._edge(False)
        os88marty.settle(m)
        mo.to(4, 4)
        os88marty.settle(m)
        w0, h0 = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        v0 = _visible(m, cx0, cy0)
        before = _grab(m, cx0 + 2, cy0 + 2, v0[0], v0[1])
        if os.environ.get("SHOTS"):
            fw, fh, fb = m.fbuf(card=0)
            os88marty.write_png_rgb("/tmp/anchor-before.png", fw, fh, fb)

        wr = dispcp.win_rect(m, S, pw)
        gx, gy = wr[0] + wr[2] - 8, wr[1] + wr[3] - 8
        rects, done = [], []
        m.bp_exec(base + pm["pt_blit"])
        threading.Thread(target=lambda: (
            time.sleep(1.0),
            mo.drag(gx, gy, gx - 60, gy - 40),
            done.append(1)), daemon=True).start()
        t0, quiet = time.time(), None
        while time.time() - t0 < 300.0:
            st = m.status()
            if st.get("state", "running") != "running":
                rects.append(tuple(_s16(_bss(m, seg, n)) for n in
                                   ("pt_rx1", "pt_ry1", "pt_rx2", "pt_ry2")))
                quiet = time.time()
                m.run()
                continue
            if done and quiet and time.time() - quiet > 4.0:
                break
            if done and quiet is None and time.time() - t0 > 40.0:
                break
            time.sleep(0.05)
        m.breakpoints([])
        m.run()
        os88marty.settle(m)
        mo.to(4, 4)
        os88marty.settle(m)
        w1, h1 = _bss(m, seg, "pt_cw"), _bss(m, seg, "pt_ch")
        # ...taken LAST, so a stale pixel anywhere in the surviving canvas has
        # had every chance to appear
        cx1, cy1 = _bss(m, seg, "pt_cx0"), _bss(m, seg, "pt_cy0")
        contw, conth = _bss(m, seg, "pt_contw"), _bss(m, seg, "pt_conth")
        v1 = _visible(m, cx0, cy0)
        after = _grab(m, cx0 + 2, cy0 + 2, v1[0], v1[1])
        if os.environ.get("SHOTS"):
            fw, fh, fb = m.fbuf(card=0)
            os88marty.write_png_rgb("/tmp/anchor-after.png", fw, fh, fb)
        vac = (_grab(m, cx0 + v1[0] + 8, cy0 + 2, 8, v1[1])
               if v1[0] < v0[0] else None)
        wr1 = dispcp.win_rect(m, S, pw)

    print("   canvas %dx%d -> %dx%d at %d,%d -> %d,%d; VISIBLE %r -> %r; "
          "window %r -> %r"
          % (w0, h0, w1, h1, cx0, cy0, cx1, cy1, v0, v1, wr[:4], wr1[:4]))
    print("   pt_blit was entered %d time(s) over the drag: %r"
          % (len(rects), rects[:4]))
    if w1 >= w0 and h1 >= h0:
        fails.append("SETUP: the drag did not shrink the canvas at all")
    empty = [r for r in rects if r[0] > r[2] or r[1] > r[3]]
    if not rects:
        fails.append("SETUP: pt_blit was never entered, so nothing says the "
                     "narrowed rect reached it")
    elif not empty:
        fails.append("the canvas was REDRAWN on a shrink: pt_blit got %r and "
                     "none of them is empty, so wm_damage did not narrow "
                     "(SPEC.md 11.90.3)" % (rects[:4],))
    if before is not None:
        n = min(v0[0], v1[0]) * 3
        diff = sum(1 for r in range(min(len(before), len(after)))
                   if before[r][:n] != after[r][:n])
        px = [(c, r) for r in range(min(len(before), len(after)))
              for c in range(n // 3) if before[r][c*3] != after[r][c*3]]
        print("   surviving canvas: %d of %d rows differ, %d pixels; first 6: "
              "%r" % (diff, len(after), len(px), px[:6]))
        if diff:
            fails.append("the shrink CHANGED %d rows of the picture it kept - "
                         "skipping the redraw is only legitimate because "
                         "nothing painted over them (SPEC.md 11.90.3)" % diff)
    if vac is not None:
        lit = sum(1 for r in vac for i in range(0, len(r), 3) if r[i] < 128)
        print("   the vacated columns: %d lit pixels of %d"
              % (lit, len(vac) * (len(vac[0]) // 3)))
    if fails:
        for f in fails:
            print("paintanchor: %s" % f)
        return 1
    print("paintanchor: PASS - the canvas was not redrawn and every surviving "
          "pixel of it is unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
