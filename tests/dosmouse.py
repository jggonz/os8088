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
  - an unsupported function falling through         -> "FN90 ANSWERED"
    into a handler instead of leaving AX alone         printed by the program

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
    """The text screen's first row starting with `prefix`, FINISHED.

    **A LINE THAT IS ON THE SCREEN IS NOT A LINE THAT HAS BEEN WRITTEN**, and
    this used to return the first sighting. Observed directly, polling as fast
    as the debug socket allows while DOSMOUSE.COM ran, the screen carries

        |RESET ax=|
        |RESET ax=FFFF bx=2|

    in that order - the label is one INT 21h and the value is the next - so
    `fields` on the first of them gives `ax` = '' and the row fails with
    `INT 33h AX=0 answered ax=, not FFFF`, which is a sentence about the
    kernel for a sampling accident. It is a TIMING race and therefore load
    sensitive in the direction that makes it look like contention: a poll a
    fixed number of HOST milliseconds apart covers less of the GUEST's work on
    a busy box, so the half-written state is sampled more often there. This
    row failed in a four-lane soak and passes solo, which is the shape every
    wrong diagnosis in this suite has worn.

    So the line has to have STOPPED CHANGING, and on the guest's own clock:
    `quiesce` wants the same text twice a fixed number of GUEST seconds apart,
    which a loaded box cannot shorten. It costs a few screen reads and it is
    exact - the program writes each line in one burst of INT 21h calls and
    then either blocks on a key or starts the next label, so a line that is
    the same half a guest second later is a line that is done.
    """
    def row():
        for r in (m.screen() or []):
            if r.lstrip().startswith(prefix):
                return r.strip()
        return None

    try:
        os88marty.until(m, lambda _: row() is not None,
                        "the %s line to appear" % prefix,
                        guest=limit, poll=0.2)
    except os88marty.MartyError as e:
        fail("no %r line appeared: %s  The last text screen was %r"
             % (prefix, str(e).split("\n")[0][:200],
                [r.rstrip() for r in (m.screen() or []) if r.strip()][:10]))
    try:
        return os88marty.quiesce(m, row, budget=limit,
                                 what="the %s line to finish printing"
                                      % prefix)
    except os88marty.MartyError as e:
        fail("the %r line never stopped changing: %s  It last read %r"
             % (prefix, str(e).split("\n")[0][:200], row()))


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

        line = wait_line(m, "FN90")
        if "FAILED" in line:
            fail("INT 33h AX=0090h CHANGED AX - a function with no "
                 "documented return value must leave it alone (SPEC.md "
                 "96.10.6; 96.10.2's bullet used to say the opposite and "
                 "this row asserted that). It asked 1Fh until "
                 "docs/FIELD-NOTES.md 56 - which DOES document a return "
                 "value, and which CuteMouse answers")

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
