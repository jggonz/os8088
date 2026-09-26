#!/usr/bin/env python3
"""A toast of the MAXIMUM width reaches the bar whole, and touches no menu.

    python3 tests/toastbar.py [machine]

`tests/unit/t_toast.py` checks every fixed message against `TOAST_MAX`.
Nothing checked `TOAST_MAX` itself.  It is derived from a geometry - the
clock's field is 25 cells and SPEC.md 59.9.2's gap takes one - and a constant
derived from a geometry by arithmetic is a constant that can be wrong by one
with every message in the tree quietly a cell short, and the static gate green
for ever.  So this one asks the MACHINE.

THREE THINGS, and each has bitten before:

  1. **THE WIDEST LEGAL MESSAGE IS DRAWN, AND IT IS ALL THERE.**  A strip is
     the message plus its bed; at `TOAST_MAX` characters it must change about
     `TOAST_MAX` cells' worth of the bar and no fewer.  A cap one too big
     would clip; one too small would leave the field short.
  2. **IT TOUCHES NO MENU** (SPEC.md 59.8).  The strip used to borrow the
     MENUS segment, where `menu_bput`'s clamp dropped whatever it covered - so
     a long enough message took the front application's own menu titles off
     the bar, which reads as the application breaking.  Asserted on PIXELS,
     not on cell arithmetic, because the arithmetic is what would be wrong.
  3. **THE BAR COMES BACK** (SPEC.md 59.9.2).  A strip that got shorter or
     moved left dragged `[menu_blast]` back over the padding that had just
     erased its tail: those cells were recorded blank, never drawn, and
     `menu_bcell` is the record of the GLASS - so no later pass could repair
     them.  Measured before that fix: two cells inverted on the bar
     PERMANENTLY, through the next toast and for the rest of the session.

SPEC.md 59.7 says of the test that found the livelock *"the test that found it
is the one worth keeping"* - and it is in no registry, which is why this file
exists.  §59.7's own failure needed a strip wide enough to reach back over a
menu title's pen and **every earlier test passed because the strip never
reached a menu**: so the message here is `TOAST_MAX` wide and the front window
is a Disk window, whose File/Folder/View/Special is the widest menu set the
kernel draws.

THE MESSAGE IS POKED IN, not triggered.  `toast_show` stages into `toast_buf`
and `toast_pass` draws; writing the buffer, the TTL and `[toast_want]` from
the host is that path from its second instruction, and it is the only way to
choose the width instead of hunting for an application whose message happens
to be exactly the cap.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                               # noqa: E402
import os88sym                                                 # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
MBAR_H = 20                     # kernel.asm; the bar is rows 0..MBAR_H-1
CELL = 8                        # a bar cell is one 8px glyph column
TTL = 0x0400                    # ticks: ~57s, long enough that nothing expires
                                # mid-measurement and short enough to wait out


def fail(msg):
    print("toastbar: FAIL: %s" % msg)
    sys.exit(1)


def cap():
    """TOAST_MAX, out of kernel/toast.inc - never a copy (t_mirror.py's rule)."""
    p = os.path.join(os.path.dirname(__file__), "..", "kernel", "toast.inc")
    for line in open(p, encoding="utf-8"):
        if line.startswith("TOAST_MAX"):
            return int(line.split("equ")[1].split(";")[0].strip())
    fail("kernel/toast.inc has no `TOAST_MAX equ <n>`")


def bar(m):
    """The menu bar's rendered pixels as (w, rows) - rgb24, one row per list."""
    w, h, px = m.fbuf()
    return w, [px[y * w * 3:(y + 1) * w * 3] for y in range(MBAR_H)]


def cells_differing(w, a, b):
    """Which 8px cell columns differ between two bar captures."""
    out = set()
    for y in range(len(a)):
        ra, rb = a[y], b[y]
        for x in range(w):
            i = x * 3
            if ra[i:i + 3] != rb[i:i + 3]:
                out.add(x // CELL)
    return out


def ink(w, rows, c):
    """What fraction of cell column `c` is dark.

    **THE THREE CAPTURES CANNOT BE COMPARED BYTE FOR BYTE, BECAUSE THE CLOCK
    IS LIVE.**  The strip sits in the clock's field (SPEC.md 59.8), so the
    cells under it are the ones whose glyphs change on their own between two
    captures seconds apart - and the first draft of this row compared raw
    pixels, found the clock's last digit had gone from one shape to another,
    and reported it as SPEC.md 59.9.2's permanently-inverted cell.  It is a
    measurement of the right thing against the wrong baseline.

    Ink per cell separates them, because the failure has a SHAPE: a cell still
    carrying the bed reads the bed's own uniform value (~0.33 here, against
    0.05 for blank bar and 0.10..0.28 for a glyph), and it reads it because it
    is STILL THE BED.  So the test is not a threshold - it is that a cell
    which changed when the strip arrived must change again when it leaves.
    """
    n = tot = 0
    for r in rows:
        for x in range(c * CELL, (c + 1) * CELL):
            i = x * 3
            tot += 1
            if sum(r[i:i + 3]) < 200:
                n += 1
    return n / float(tot)


def main():
    MAX = cap()
    msg = ("T%d." % MAX).ljust(MAX, "x")[:MAX]      # exactly MAX characters,
    assert len(msg) == MAX                          # and self-describing on a
                                                    # photograph of a failure
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing" % p)

    with os88ui.boot(SYS, apps=APPS, machine=MACHINE) as ui:
        m = ui.m
        sym = ui.sym
        # A DISK WINDOW IN FRONT: File/Folder/View/Special is the widest menu
        # set the kernel draws, which is what SPEC.md 59.7's own repro needed.
        ui.open_drive("B")
        os88marty.settle(m)

        w, before = bar(m)
        menus_end = None

        # --- put the widest legal message up --------------------------------
        m.write(sym("toast_buf"), msg.encode("latin-1") + b"\0")
        m.write(sym("toast_ttl"), TTL.to_bytes(2, "little"))
        m.write(sym("toast_want"), b"\x01")
        try:
            os88marty.until(m, lambda _: m.read(sym("toast_on"), 1)[0] == 1,
                            "[toast_on] = 1", poll=0.1, limit=30)
        except os88marty.MartyError:
            fail("[toast_on] never went up - toast_pass did not arm a message "
                 "written straight into toast_buf, so this row is not driving "
                 "the path it thinks it is")
        os88marty.settle(m)
        w2, during = bar(m)
        if w2 != w:
            fail("the screen changed width mid-run (%d -> %d)" % (w, w2))

        moved = cells_differing(w, before, during)
        if not moved:
            fail("a %d-character toast is up ([toast_on] = 1) and NOT ONE "
                 "PIXEL of the bar changed" % MAX)

        # --- 1. it is all there ---------------------------------------------
        span = max(moved) - min(moved) + 1
        if span < MAX:
            fail("the strip covers %d cells and the message is %d characters "
                 "(cells %d..%d). TOAST_MAX is bigger than the field can "
                 "actually draw, so EVERY message in the tree is short by "
                 "%d and tests/unit/t_toast.py cannot see it (SPEC.md 59.10)"
                 % (span, MAX, min(moved), max(moved), MAX - span))

        # --- 2. it touched no menu ------------------------------------------
        # The menus are the LEFT of the bar; whatever the strip's home, no cell
        # it changed may be one a menu title had drawn in.
        menus_end = int.from_bytes(bytes(m.read(sym("menu_bn"), 2)), "little")
        intruded = sorted(c for c in moved if c < menus_end)
        if intruded:
            fail("the strip changed cell(s) %s, inside the MENUS segment, "
                 "which ends at cell %d. SPEC.md 59.8 moved the toast out of "
                 "that segment precisely so no message can take a menu title "
                 "off the bar" % (intruded[:8], menus_end))
        print("toastbar: %r is up: %d cells (%d..%d), menus end at %d - clear "
              "by %d" % (msg, span, min(moved), max(moved), menus_end,
                         min(moved) - menus_end))

        # --- 3. ...and the bar comes back ------------------------------------
        # **IT EXPIRES, IT IS NOT RETIRED**, and the difference is the whole
        # assertion: `toast_pass`'s `.chk` arm - the modular compare against
        # `[toast_die]` - is how every real toast ends, and SPEC.md 59.9.2's
        # permanently-inverted cells were an EXPIRY leaving its tail behind.
        # Poking an empty buffer and re-arming instead would go through
        # `toast_arm` and measure a different path (this row did, first draft,
        # and reported one cell of its own making as a product defect).
        # Winding `[toast_die]` back is the only way to reach the real one
        # without waiting out a whole TTL.
        now = int.from_bytes(bytes(m.read(sym("ticks"), 2)), "little")
        m.write(sym("toast_die"), ((now - 2) & 0xFFFF).to_bytes(2, "little"))
        try:
            os88marty.until(m, lambda _: m.read(sym("toast_on"), 1)[0] == 0,
                            "[toast_on] = 0", poll=0.1, limit=30)
        except os88marty.MartyError:
            fail("the toast never came down")
        os88marty.settle(m)
        _, after = bar(m)

        # The MENUS are not under the strip and nothing there is live, so they
        # are held to the byte across all three captures - the strongest form
        # of assertion 2, and free.
        for tag, shot in (("while it was up", during), ("after it expired", after)):
            moved2 = [c for c in cells_differing(w, before, shot) if c < menus_end]
            if moved2:
                fail("%s, menu cell(s) %s changed. The menus segment ends at "
                     "cell %d and no toast may reach it (SPEC.md 59.8)"
                     % (tag, moved2[:8], menus_end))

        # ...and no cell that the strip lit is still lit by it.
        stuck = []
        for c in sorted(moved):
            b, d, a = (ink(w, before, c), ink(w, during, c), ink(w, after, c))
            if abs(a - d) < 0.01 and abs(b - d) >= 0.01:
                stuck.append((c, round(a, 2)))
        if stuck:
            fail("the toast has expired and cell(s) %s still carry the "
                 "STRIP's own ink. SPEC.md 59.9.2 - a strip that got shorter "
                 "or moved left dragged [menu_blast] back over the padding "
                 "that erased its tail, and menu_bcell is the record of the "
                 "GLASS, so nothing repairs it for the rest of the session"
                 % (stuck[:8],))

    print("toastbar: it came down and left no cell of its bed behind")
    print("toastbar: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
