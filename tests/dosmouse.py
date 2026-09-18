#!/usr/bin/env python3
"""The DOS wave-2 mouse gate (SPEC.md 96.10, docs/plans/DOS-EXEC-PLAN.md 9.1).

INT 33h here is a TRANSLATION over numbers os8088's own ISR is already
keeping (SPEC.md 96.10), so this gate is a comparison and not a smoke test:
it moves the kernel's pointer to a place it chooses, reads `mouse_x`/`mouse_y`
back out of the guest, scales them the way the package is supposed to, and
asserts the DOS program printed exactly that.

WHAT IT WOULD CATCH, and three of these were seen failing before it existed:

  - `mul` eating DX, which is the y this answers    -> y tracks x, or is a
    (the multiply and the divide BOTH land there)      divide remainder
  - the scale taken against the wrong geometry      -> right on VGA, wrong on
    (a constant 640x480 rather than OSAPI_VIDEO)       both 1bpp adapters
  - functions 5 and 6 answering a flat 0            -> "PRESS n=0" after a
    (no edge accumulation, SPEC.md 96.10.1)            click that happened
  - an unsupported function falling through         -> "FN1F ANSWERED"
    into a handler instead of answering AX=0           printed by the program

It runs on MartyPC and must: the pointer is moved by driving a real serial
mouse into a real 8088, which is the whole of what is being translated.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
MOU = "build/dosmou360.img"

# INT 33h's virtual screen, whatever the card is (SPEC.md 96.10).
VW = 640
VH = 200

# BOTH ARMS, and the second one is the row. A CGA desktop is 640x200, which is
# INT 33h's virtual screen EXACTLY - so on that machine the scale is the
# identity and every wrong answer the arithmetic can give is hidden behind it.
# Hercules is 720x348 and nothing cancels: x is scaled by 8/9 and y by 50/87,
# the multiply's product does not fit 16 bits, and the divide leaves a
# remainder in DX. It is the arm that decides whether this is a translation or
# a coincidence.
MACHINES = ("os8088_5150_cga_gla", "os8088_5150_herc_gla")


def fail(msg):
    print("dosmouse: FAIL: %s" % msg)
    sys.exit(1)


def wait_line(m, prefix, limit=40.0):
    """The text screen's first row starting with `prefix`, stripped."""
    end = time.time() + limit
    rows = []
    while time.time() < end:
        rows = m.screen() or []
        for r in rows:
            if r.lstrip().startswith(prefix):
                return r.strip()
        time.sleep(0.2)
    fail("no %r line appeared; the last text screen was %r"
         % (prefix, [r.rstrip() for r in rows if r.strip()][:10]))


def fields(line):
    """'POS1 x=12 y=34 b=0' -> {'x': 12, 'y': 34, 'b': 0}."""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            out[k] = v
    return out


def expect(ui, name, line, tol=2):
    """Assert a POS line against what the kernel's own pointer says."""
    f = fields(line)
    try:
        gx, gy = int(f["x"]), int(f["y"])
    except (KeyError, ValueError):
        fail("could not read a position out of %r" % line)

    mx = ui._word("mouse_x")
    my = ui._word("mouse_y")
    w = ui._word("vid_w")
    h = ui._word("vid_h")
    want_x = mx * VW // w
    want_y = my * VH // h
    print("dosmouse: %s: pointer (%d,%d) on %dx%d -> want (%d,%d), "
          "the program read (%d,%d)" % (name, mx, my, w, h,
                                        want_x, want_y, gx, gy))
    if abs(gx - want_x) > tol or abs(gy - want_y) > tol:
        fail("%s: INT 33h answered (%d,%d) where the pointer at (%d,%d) on a "
             "%dx%d desktop scales to (%d,%d) (SPEC.md 96.10)"
             % (name, gx, gy, mx, my, w, h, want_x, want_y))
    return gx, gy


def run(machine):
    print("dosmouse: --- %s ---" % machine)
    with os88ui.boot(SYS, apps=MOU, machine=machine) as ui:
        m = ui.m
        win = ui.path("B:/DOSMOUSE.COM")
        if not win:
            fail("double-clicking DOSMOUSE.COM opened no window")

        # --- 1. reset -------------------------------------------------------
        line = wait_line(m, "RESET")
        f = fields(line)
        if f.get("ax", "").upper() != "FFFF":
            fail("INT 33h AX=0 answered ax=%s, not FFFF - a mouse IS "
                 "installed (SPEC.md 96.10)" % f.get("ax"))
        if f.get("bx") != "2":
            fail("INT 33h AX=0 answered bx=%s, not 2 buttons" % f.get("bx"))
        print("dosmouse: reset -> %s" % line)

        # --- 2. two level reads, the pointer moved in between ---------------
        w = ui._word("vid_w")
        h = ui._word("vid_h")
        # Two points well inside the screen and well apart on BOTH axes, so a
        # y that is tracking x - the shape the DX clobber gives - cannot pass
        # by accident.
        for step, (px, py), label in ((0, (w // 4, h * 3 // 4), "POS1"),
                                      (1, (w * 3 // 4, h // 4), "POS2")):
            ui.mo.to(px, py)
            m.type_text("x")
            expect(ui, label, wait_line(m, label))

        # --- 3. the edges, which happen while the program is BLOCKED --------
        cx, cy = w // 2, h // 2
        ui.mo.to(cx, cy)
        ui.mo.click(cx, cy)
        m.type_text("x")

        line = wait_line(m, "PRESS")
        f = fields(line)
        if f.get("n") != "1":
            fail("INT 33h AX=5 answered n=%s after exactly one click: the "
                 "counts are accumulated on every state read precisely so a "
                 "press that happened while the program was blocked is still "
                 "there (SPEC.md 96.10.1)" % f.get("n"))
        expect(ui, "PRESS", line)
        print("dosmouse: the press edge survived the block -> %s" % line)

        line = wait_line(m, "REL")
        if fields(line).get("n") != "1":
            fail("INT 33h AX=6 answered %r after exactly one click" % line)

        line = wait_line(m, "FN1F")
        if "FAILED" in line:
            fail("INT 33h AX=1Fh answered instead of returning AX=0 "
                 "(SPEC.md 96.10.2)")

        rows = m.screen() or []
        print("dosmouse: the bracket's text screen:")
        for r in rows[:16]:
            if r.strip():
                print("   | %s" % r.rstrip())

        # --- 4. out, and the desktop back -----------------------------------
        wait_line(m, "READY")
        m.type_text("x")
        os88marty.settle(m)

        titles = ui.titles()
        if "Disk" not in titles:
            fail("the desktop did not come back after the bracket: %r"
                 % (titles,))
        print("dosmouse: back on the desktop, windows %r" % (titles,))

        wd, ht, data = m.fbuf()
        png = "build/dosmouse-%s.png" % machine.rsplit("_", 2)[-2]
        os88marty.write_png_rgb(png, wd, ht, data)
        print("dosmouse: %s written" % png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", action="append",
                    help="run one machine instead of both arms")
    args = ap.parse_args()

    for p in (SYS, MOU):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    for machine in (args.machine or MACHINES):
        run(machine)

    print("dosmouse: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
