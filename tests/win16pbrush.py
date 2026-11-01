#!/usr/bin/env python3
"""win16pbrush - Paintbrush 3.0 draws, picks its tools, fills and saves
(SPEC.md 124.8, 124.9, 124.25, 96.11.6.4, docs/plans/WIN16-ANY-PLAN.md S19b).

    python3 tests/win16pbrush.py            (row win16pbrush)
    python3 tests/win16pbrush.py --keep     leave the QEMU for a look

*As a user, I can paint in Windows 3.0's Paintbrush: a stroke, a box, and
the roller filling inside and around it - and save it.*

PBRUSH.EXE and PBRUSH.DLL are Microsoft's, in build/win16/ only where the
win16-reference skill's `samples` put them; elsewhere the row SKIPS. On VGA
under QEMU, with the pointer driven the way a hand drives it:

  1. a brush stroke across the canvas. The canvas takes the capture on the
     press and lets it go only on a WM_MOUSEMOVE off the canvas with no
     button down (124.8's hover moves), so the box tool clicked next must
     be the box tool - not another dab of the brush;
  2. a box drawn with it, red picked from the palette, the roller picked and
     clicked inside the box: ExtFloodFill on the window's DC (124.25.6);
  3. green picked and the roller clicked outside the box: a fill across the
     whole canvas, whose read-back mask and Paintbrush's own strips (kept
     GMEM_DISCARDABLE | GMEM_NOTIFY, never discarded - 124.9) must both fit.

The canvas's pixels are then read: inside the box red, the box's edge black,
outside it green. Last, the first sixteen interrupt vectors must be as they
were before the first stroke: a discarded strip that Paintbrush locks
unchecked is written through NULL, over them, which is how the first cut of
124.9 failed.

  4. File > Save As, `NEW.BMP`: Paintbrush writes the body, seeks back to 0
     and writes the header over its placeholder (96.11.6.4), and keeps its
     picture in GMEM_NOTIFY bands that the heap asks it to page out (124.9.1).
     The file is read off B: on the host: a whole BMP, red inside the box,
     green outside it, where the screen had them.
"""
import argparse
import os
import struct
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import b                                      # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16sol import (SDrive, glass, window_of,               # noqa: E402
                      W16S_RUNNING)
from os88fat import Fat12                                     # noqa: E402

NAMES = ("PBRUSH.EXE", "PBRUSH.DLL")
# from the window's frame corner (VGA, the window as WIN16 places it)
BRUSH0, BRUSH1 = (140, 96), (220, 126)
BOXTOOL = (12, 183)
ROLLER = (12, 131)
RED, GREEN = (170, 324), (220, 324)
BOX0, BOX1 = (170, 146), (230, 186)
INSIDE, OUTSIDE = (200, 166), (320, 226)
FILE, SAVEAS = (20, 26), (20, 99)       # the menu bar's File, its Save As
CANVAS = (51, 40)                       # the picture's (0, 0), a new one,
#                                         where a press there lands in it


def red(c):
    return c is not None and c[0] > 200 and c[1] < 150 and c[2] < 150


def green(c):
    return c is not None and c[1] > 200 and c[0] < 150 and c[2] < 150


def disk_file(img, name):
    """NAME's bytes from the root of the FAT12 image IMG, or None"""
    fat = Fat12(img)
    for _, _, e in fat.entries():
        if Fat12.pretty(e[:11]) == name:
            size = struct.unpack_from("<I", e, 28)[0]
            first = struct.unpack_from("<H", e, 26)[0]
            out = bytearray()
            for c in fat.chain(first) if first else ():
                o = fat.cluster_off(c)
                out += fat.img[o:o + fat.spc * fat.bps]
            return bytes(out[:size])
    return None


def bmp_pixel(data, x, y):
    """(r, g, b) at X, Y of a BMP whose header is whole, or None"""
    if len(data) < 54 or data[:2] != b"BM" \
            or struct.unpack_from("<I", data, 2)[0] != len(data):
        return None
    off = struct.unpack_from("<I", data, 10)[0]
    hsz = struct.unpack_from("<I", data, 14)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    if bits not in (1, 4, 8, 24) or not (0 <= x < w and 0 <= y < abs(h)):
        return None
    stride = (w * bits + 31) // 32 * 4
    row = off + stride * ((abs(h) - 1 - y) if h > 0 else y)
    if bits == 24:
        bb, gg, rr = data[row + 3 * x:row + 3 * x + 3]
        return rr, gg, bb
    v = data[row + x * bits // 8]
    v = (v >> (8 - bits - (x * bits % 8))) & ((1 << bits) - 1)
    bb, gg, rr = data[14 + hsz + 4 * v:14 + hsz + 4 * v + 3]
    return rr, gg, bb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for n in NAMES:
        if not os.path.exists(b(os.path.join("win16", n))):
            print("win16pbrush: SKIP - no build/win16/%s (the win16-reference "
                  "skill's `samples`)" % n)
            return 77
    import os88ui
    import xmcheck
    from win16cpu import raise_
    bad = []
    work = tempfile.mkdtemp(prefix="w16p-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, list(NAMES))
        d = SDrive(work, b("os8088.img"), bimg, a.keep)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        raise_(d, ui, dw)
        p = F.Prog(d, ui, xmcheck.sym, S, "PBRUSH.EXE")
        p.launch(dw)
        d.wait(lambda: p.text("w16_l2") == "Running", 300, "Paintbrush to run")
        d.idle(10.0)
        w = window_of(ui, p)
        if p.pb("w16_state") != W16S_RUNNING or w is None \
                or "paintbrush" not in w.title.lower():
            bad.append("Paintbrush is not running in a window of its own: %s / %s"
                       % (p.text("w16_l1"), p.text("w16_l2")))
            raise StopIteration
        fx, fy = w.x, w.y
        print("Paintbrush: %r at %d, %d" % (w.title, fx, fy))
        ivt0 = d.read(0, 0x40)          # the first sixteen vectors, now

        def m(*args):
            subprocess.run([sys.executable, "tools/mouse.py", d.sock]
                           + [str(v) for v in args], check=True,
                           capture_output=True)

        def at(pt):                     # the pointer there, a tick to see it
            m("to", fx + pt[0], fy + pt[1])
            time.sleep(2.0)

        def click(pt):
            at(pt)
            m("down")
            time.sleep(0.3)
            m("up")
            time.sleep(2.0)

        def drag(p0, p1):
            at(p0)
            m("down")
            time.sleep(0.4)
            for k in (1, 2):
                m("move", (p1[0] - p0[0]) // 2, (p1[1] - p0[1]) // 2)
                time.sleep(0.4)
            m("up")
            time.sleep(3.0)

        drag(BRUSH0, BRUSH1)            # 1: the brush, then the box tool
        click(BOXTOOL)
        drag(BOX0, BOX1)                # 2: the box, red, the roller inside
        click(RED)
        click(ROLLER)
        click(INSIDE)
        d.idle(10.0)
        click(GREEN)                    # 3: green, the roller outside
        click(OUTSIDE)
        d.idle(15.0)
        m("to", fx + 400, fy + 360)     # off the canvas, out of the reading
        time.sleep(2.0)

        vw, vh, rows = glass(d, "vga", work)

        def px(pt):
            return rows[fy + pt[1]][fx + pt[0]]
        inside, outside = px(INSIDE), px(OUTSIDE)
        edge = px((BOX0[0], (BOX0[1] + BOX1[1]) // 2))
        print("inside %r, the box's edge %r, outside %r" % (inside, edge, outside))
        if not red(inside):
            bad.append("inside the box is %r, not red: the box tool or the "
                       "fill inside it did not happen" % (inside,))
        if max(edge) > 64:
            bad.append("the box's edge is %r, not black" % (edge,))
        if not green(outside):
            bad.append("outside the box is %r, not green: the fill across the "
                       "canvas did not happen" % (outside,))
        ivt = d.read(0, 0x40)
        if ivt != ivt0:
            bad.append("the interrupt table has changed under Paintbrush: %s, "
                       "was %s" % (ivt[:16].hex(), ivt0[:16].hex()))
        if p.pb("w16_state") != W16S_RUNNING:
            bad.append("Paintbrush is not running after the fills: %s / %s"
                       % (p.text("w16_l1"), p.text("w16_l2")))
            raise StopIteration

        click(FILE)                     # 4: File > Save As, NEW.BMP
        click(SAVEAS)
        time.sleep(1.0)
        for k in ("n", "e", "w", "dot", "b", "m", "p", "ret"):
            subprocess.run([sys.executable, "tools/qmp.py", d.sock,
                            "sendkey " + k], check=True, capture_output=True)
            time.sleep(0.25)
        saved, t0 = None, time.time()
        while time.time() - t0 < 240:   # the header is written last
            time.sleep(5.0)
            data = disk_file(bimg, "NEW.BMP")
            if data and bmp_pixel(data, 0, 0) is not None:
                saved = data
                break
        if saved is None:
            bad.append("File > Save As left no whole NEW.BMP on B: (%s): %s"
                       % ("none" if data is None else "%d bytes" % len(data),
                          p.msgtext() if p.waiting_msgbox(F.msgrow())
                          else p.text("w16_l2")))
        else:
            fin = bmp_pixel(saved, INSIDE[0] - CANVAS[0], INSIDE[1] - CANVAS[1])
            fout = bmp_pixel(saved, OUTSIDE[0] - CANVAS[0],
                             OUTSIDE[1] - CANVAS[1])
            print("NEW.BMP: %d bytes, inside %r, outside %r"
                  % (len(saved), fin, fout))
            if not (red(fin) and green(fout)):
                bad.append("NEW.BMP has %r inside the box and %r outside it, "
                           "not red and green as the screen has them"
                           % (fin, fout))
        if p.pb("w16_state") != W16S_RUNNING:
            bad.append("Paintbrush is not running after the save: %s / %s"
                       % (p.text("w16_l1"), p.text("w16_l2")))
        if not a.keep:
            d.p.close()
    except StopIteration:
        pass
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16pbrush: a stroke, the box tool after it, a box, two "
              "fills and NEW.BMP saved, and the interrupt table whole")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
