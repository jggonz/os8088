#!/usr/bin/env python3
"""The SUB-RECT RESTORE gate (SPEC.md 5.8 / 11.96.6) - does putting back only
part of a window's cache land the same pixels as putting back all of it?

    python3 tools/subcheck.py capture DIR [machine]   # drive one kernel
    python3 tools/subcheck.py diff DIR-A DIR-B        # ...and compare two

The claim this exists to test is "the picture is the same, only less of it was
drawn", and a screenshot of ONE build cannot check that. So `make REDRAWFULL=1`
builds the reference - it puts wm_dmg_wins back on whole-window restores - and
the SAME scripted session is driven through both kernels and compared frame by
frame. 0 differing pixels is the standard, on VGA mode 12h, CGA and Hercules.

    rm -f build/apps360.img && make REDRAWFULL=1          # the reference kernel
    python3 tools/subcheck.py capture /tmp/ref <machine>
    rm -f build/apps360.img && make                       # ...and this one
    python3 tools/subcheck.py capture /tmp/new <machine>
    python3 tools/subcheck.py diff /tmp/ref /tmp/new

`capture` always boots `build/os8088-360.img`, so the two runs are two builds of
that path rather than two paths. REBUILD THE REFERENCE after any change to a
path the session touches, or the diff reports yesterday's kernel against
today's. And clear the apps floppy first: the guest MOUNTS IT WRITABLE, so a
previous run's scratch files reach the second run's Disk window listings and
read as a redraw defect.

Four things cost a run each, and all four are REDRAW-SPEC Part 3's:

  NO ANIMATING WINDOW. A Timer's digits and a Fractal's render differ between
    two passes and read as a redraw defect. Every window here is a DISK window,
    which exercises the same paths, promises WF_SAVEU (SPEC.md 22.14) and
    stands perfectly still.

  MASK THE MENU BAR. The clock changes between two runs a minute apart, and
    SPEC.md 59's toast lives in the same strip. Rows above MASK_Y are dropped -
    and the bar can never be covered (windows clamp to y >= MBAR_H), so nothing
    this feature touches is up there.

  THE MOUSE ARROW IS MASKED, and the reasoning that said otherwise was wrong.
    It ran: both runs drive the pointer to the same derived coordinates, so the
    arrow is in the same place in both. True of its POSITION and silent about
    whether it is DRAWN - the cursor is erased under the gfx lock and put back at
    the unlock (SPEC.md 7.1.4), so whether a capture catches it depends on where
    `settle` lands relative to the last one. Measured: the SAME BUILD captured
    twice differed by 45 arrow-shaped pixels over a dock tile. A gate that fails
    at random is worse than no gate, so a 16x16 box at the published cursor
    position is excluded - and none of the features these gates cover involves
    the cursor, which has verification of its own.

  EVERY COORDINATE IS DERIVED from wm_wins/wm_zord read out of the guest, never
    written down: half the harness time in this round went on clicks that
    missed. It also means the two kernels are asked for the same session even
    if a window lands somewhere new.

  ONE GLYPH IN A DISK WINDOW'S STATUS LINE IS A FACT ABOUT THE KERNEL. The
    drive-A window shows `Size ... Free ...` for the SYSTEM disk, which carries
    KERNEL.SYS - so a build that crosses a 1KB cluster reports one kilobyte less
    free, and the gate sees a 7x7 box of differing pixels that is not a redraw
    defect at all. Measured: 25 pixels on all three adapters, at the drive-A
    window's status line and only in the steps where that line is ON SCREEN.
    Confirm it in one command rather than guessing - `make` prints
    `os88disk: ... N/354 clusters` for os8088-360.img, and if N differs between
    the two builds that is the whole of it. The same shape reaches callfront.py
    through a package's SIZE in a folder listing.
"""
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse
import sucheck as su
from os88geom import CUR_XHX, CUR_XHY

MASK_Y = 18                     # the menu bar: clock + toast (SPEC.md 12.9/59)
TITLE_H = 18


def curbox(mo):
    """The cell the mouse pointer may or may not be occupying, at its published
    position - CUR_GW/CUR_GH are 8x12 (SPEC.md 7.1) and 16x16 is generous on
    purpose.

    IT REACHES UP AND LEFT AS WELL, and that is not slack: a shape carries a
    HOT SPOT and its cell starts at (pointer - hot), so the crosshair's cell
    begins 3px left and 5px above mouse_x/mouse_y (SPEC.md 7.2.2). Anchored at
    the published position this mask fits the ARROW alone, whose hot spot is
    (0,0) - and over any window that names another shape it leaves a sliver of
    pointer in the capture. Measured: 14 pixels of crosshair over a Disk
    window's file row, reported as a redraw defect in a step where the two
    kernels drew identically."""
    x, y, _ = mo.where()
    return (x - CUR_XHX, y - CUR_XHY, x + 15, y + 15)


def shot(m, tag, out, log, mo=None):
    """The screen, in guest coordinates, with the bar dropped.

    The window rects go into a .txt beside it, because a pixel diff cannot tell
    "the same session drew different pixels" from "the two runs put the windows
    in different places" - and the second is a harness failure that reads
    exactly like the first (thousands of differing pixels in a window-shaped
    region). `diff` compares these FIRST for that reason.
    """
    w, bpp, data = su.fb(m)
    body = bytearray(data[MASK_Y * w * bpp:])
    if mo is not None:                  # blank the arrow's cell in BOTH runs, so
        x1, y1, x2, y2 = curbox(mo)     # "drawn or not" cannot be a difference
        rows = len(body) // (w * bpp)
        for y in range(max(y1, MASK_Y), min(y2, MASK_Y + rows - 1) + 1):
            r = (y - MASK_Y) * w * bpp
            for x in range(max(x1, 0), min(x2, w - 1) + 1):
                for k in range(bpp):
                    body[r + x * bpp + k] = 0
    body = bytes(body)
    stem = os.path.join(out, "%02d-%s" % (len(log), tag))
    with open(stem + ".raw", "wb") as f:
        f.write(struct.pack("<HH", w, bpp) + body)
    geom = " ".join("%d:%d,%d,%d,%d" % (x.i, x.x, x.y, x.w, x.h)
                    for x in wins(m)) + " z=" + repr(zorder(m))
    with open(stem + ".txt", "w") as f:
        f.write(geom + "\n")
    log.append((tag, len(body)))
    print("  %-22s %d x %s  %s" % (tag, w, bpp, geom))


def wins(m):
    return [x for x in su.windows(m) if x.visible]


def pclick(mo, x, y, settle=2.5):
    """A click whose BOTH EDGES are proven, which os88mouse's own `click` does
    not do - and a dropped edge here is silent and cumulative.

    `click`/`drag` send their button packets with `_pk`, which does not check
    that the guest decoded them; the 1200-baud UART drops a packet clocked in
    while the previous one is still in flight. A dropped RELEASE is the
    expensive one: the button stays down, so the next press is no edge at all
    and every step after it quietly does nothing. Measured, that is exactly what
    happened - a session whose last seven steps left the window table completely
    unchanged while every click "succeeded". `_edge` is the primitive that waits
    for the published mouse_btn to agree.
    """
    mo.to(x, y)
    if mo.where()[2] & 1:
        mo._edge(False)
    mo._edge(True)
    mo._edge(False)
    time.sleep(settle)


def pdrag(mo, x0, y0, x1, y1, settle=2.5):
    """...and the same for a drag: press proven, move proven by `to`'s own
    read-back, release proven."""
    mo.to(x0, y0)
    if mo.where()[2] & 1:
        mo._edge(False)
    mo._edge(True)
    mo.to(x1, y1, l=True)
    mo._edge(False)
    time.sleep(settle)


def zorder(m):
    """wm_zord, bottom first - which is what decides who covers whom."""
    n = m.read(m.sym("wm_zn"), 1)[0]
    return list(m.read(m.sym("wm_zord"), max(n, 1))[:n])


def titlebar(m, win):
    """A point on `win`'s title bar that nothing ABOVE it covers, or None.

    The z-order is the whole question and a geometric test is not it: two
    cascaded Disk windows overlap almost completely, so "does any other window
    contain this point" says the FRONT one has no clickable title bar - which is
    exactly backwards, since nothing is on top of it.
    """
    z = zorder(m)
    above = z[z.index(win.i) + 1:] if win.i in z else []
    others = [o for o in wins(m) if o.i in above]
    y = win.y + TITLE_H // 2
    for x in range(win.x + 26, win.x + win.w - 26):
        if not any(o.covers(x, y) for o in others):
            return (x, y)
    return None


def capture(out, machine, defines=()):
    os.makedirs(out, exist_ok=True)
    log = []
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine) as m:
        # THE REFERENCE KERNEL IS A DIFFERENT BINARY, so every symbol has to be
        # looked up with the knob that built it: os88sym asserts its map against
        # build/kernel.bin and refuses rather than answering with a plausible
        # wrong address. sucheck's helpers call m.sym themselves, so the define
        # is bound here rather than passed down through all of them.
        if defines:
            plain = m.sym
            m.sym = lambda n, d=tuple(defines): plain(n, d)
        mo = Mouse(marty=m)
        print("machine %s -> %s" % (machine, out))
        shot(m, "desktop", out, log, mo)

        # Two Disk windows, cascaded 16px apart by wm_create.
        mo.dblclick(*su.zone(m, 1)); time.sleep(4)
        shot(m, "disk-b", out, log, mo)
        mo.dblclick(*su.zone(m, 0)); time.sleep(4)
        shot(m, "disk-a", out, log, mo)

        w = wins(m)
        if len(w) < 2:
            raise SystemExit("subcheck: wanted two windows, got %r" % (w,))
        z = [i for i in zorder(m) if i in [x.i for x in w]]
        by = {x.i: x for x in w}
        back, front = by[z[0]], by[z[-1]]   # z-order, not slot order
        print("  back %r / front %r" % (back, front))

        # Move the front one clear, so the back one has a title bar to click
        # and the two overlap the way the field report describes.
        pt = titlebar(m, front)
        if pt is None:
            raise SystemExit("subcheck: no title strip on %r" % (front,))
        pdrag(mo, pt[0], pt[1], pt[0] + 90, pt[1] + 60)
        shot(m, "drag-clear", out, log, mo)

        # THE FIELD BUG: drag the front window ACROSS the back one. Left, then
        # back right - the two directions uncover different Ls.
        f = [x for x in wins(m) if x.i == front.i][0]
        pt = titlebar(m, f)
        pdrag(mo, pt[0], pt[1], pt[0] - 70, pt[1])
        shot(m, "drag-across-l", out, log, mo)
        f = [x for x in wins(m) if x.i == front.i][0]
        pt = titlebar(m, f)
        pdrag(mo, pt[0], pt[1], pt[0] + 40, pt[1] - 25)
        shot(m, "drag-across-r", out, log, mo)

        # A raise of a covered window, both ways round: this is the restore the
        # whole cache exists for.
        pclick(mo, *su.tile(m, back))
        shot(m, "raise-back", out, log, mo)
        pclick(mo, *su.tile(m, front))
        shot(m, "raise-front", out, log, mo)

        # Minimize banks (SPEC.md 11.96.5) and restore-from-dock is a blit.
        # Both boxes are wm_hit_test's own geometry: rows W_Y+4..W_Y+14, close
        # at W_X+8..W_X+18 and minimize mirrored at W_X+W_W-19..W_X+W_W-9.
        f = [x for x in wins(m) if x.i == front.i][0]
        pclick(mo, f.x + f.w - 14, f.y + 9)
        shot(m, "minimized", out, log, mo)
        pclick(mo, *su.tile(m, front))
        shot(m, "restored", out, log, mo)

        # ...and closing, which damages the rect the window vacated.
        f = [x for x in wins(m) if x.i == front.i][0]
        pclick(mo, f.x + 13, f.y + 9)
        shot(m, "closed", out, log, mo)
        m.quit()
    print("captured %d step(s)" % len(log))


def diff(a, b):
    fa = sorted(x for x in os.listdir(a) if x.endswith(".raw"))
    fb_ = sorted(x for x in os.listdir(b) if x.endswith(".raw"))
    if fa != fb_:
        raise SystemExit("subcheck: the two runs took different steps\n  %r\n  %r"
                         % (fa, fb_))
    # GEOMETRY FIRST: two runs that put the windows in different places are not
    # comparable at all, and the pixel diff would blame the kernel for it.
    for name in fa:
        ga = open(os.path.join(a, name[:-4] + ".txt")).read().strip()
        gb = open(os.path.join(b, name[:-4] + ".txt")).read().strip()
        if ga != gb:
            raise SystemExit(
                "subcheck: the two runs DIVERGED at %s - the session is not the\n"
                "same one, so no pixel comparison means anything:\n"
                "  A: %s\n  B: %s\n"
                "A dropped drag packet is the usual cause: the 1200-baud UART\n"
                "drops one sent while the previous is in flight, and the slower\n"
                "of two kernels drops more of them." % (name, ga, gb))
    bad, total = [], 0
    print("%-24s %12s %10s" % ("step", "pixels", "differing"))
    for name in fa:
        da = open(os.path.join(a, name), "rb").read()
        db = open(os.path.join(b, name), "rb").read()
        wa, bpa = struct.unpack_from("<HH", da)
        wb, bpb = struct.unpack_from("<HH", db)
        if (wa, bpa) != (wb, bpb):
            raise SystemExit("subcheck: %s geometry differs: %r vs %r"
                             % (name, (wa, bpa), (wb, bpb)))
        pa, pb = da[4:], db[4:]
        n = min(len(pa), len(pb)) // bpa
        d = sum(1 for i in range(n)
                if pa[i * bpa:(i + 1) * bpa] != pb[i * bpa:(i + 1) * bpa])
        total += d
        print("%-24s %12d %10d%s" % (name, n, d, "   <-- DIFFERS" if d else ""))
        if d:
            bad.append("%s: %d pixels" % (name, d))
    print()
    if bad:
        raise SystemExit("FAIL: %d differing pixels in %d step(s)\n  %s"
                         % (total, len(bad), "\n  ".join(bad)))
    print("PASS: 0 differing pixels in %d steps" % len(fa))


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "capture":
        capture(sys.argv[2],
                sys.argv[3] if len(sys.argv) > 3 else "os8088_5150_cga_gla",
                sys.argv[4:])         # any -D the kernel was built with
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        diff(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
