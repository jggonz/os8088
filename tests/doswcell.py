#!/usr/bin/env python3
"""doswcell - what ONE CELL costs a live DOS window's band (SPEC.md 96.33.12).

A live program's text lands in the console buffer and the UI task spends the
dirty marks through `con_rows_owed` -> per row `con_compose` + `con_emit` ->
OSAPI_GFX_BLIT1 (SPEC.md 70.8.2). The bitmap names a ROW and since 96.33.12
it carries a column SPAN as well, and this row asks whether the WINDOWED
renderer honours it: a clock that rewrites one digit must not compose and
blit eighty cells.

The probe (tests/dostrap/dwcell.asm, assembled onto a scratch B: here) runs
in its window and changes one cell at a time, two BIOS ticks apart:

  1  int 10h AH=09h at a parked cursor - a clock's shape
  2  the teletype printing a digit and a BACKSPACE - the caret walks a cell
     right and back, so the cell it leaves must lose its underline

Every `con_rows_owed` pass in each phase is traced on MartyPC (logged
breakpoints, guest cycles) and priced: cycles from entry to the end of the
row walk, rows composed, and OSAPI_GFX_BLIT1 runs. A pass that composed one
row is the update; its MEDIAN must be under DC_MAXCYC guest cycles. The whole
row measured 81,013 on Hercules and 85,596 on VGA (47,027 of it composing
eighty cells); the span measures 11,305 and 12,429 (SPEC.md 96.33.12).

  3  ...and THE INCREMENTAL GLASS IS THE FULL REPAINT'S: with the probe
     waiting on its key, the band is captured, every row is marked whole
     from the host and repainted, and the two captures must not differ by a
     pixel inside the band - a span that missed a cell (the caret's old one
     above all) is a stale cell the full repaint puts right. The rect is
     CALIBRATED by a solid cell poked into the buffer with no mark, because
     fbuf is the raster with its border (-16,2 on Hercules). And the caret's
     two scanlines must be lit, which equality alone cannot say.

**Break it on purpose** (docs/WRITING-TESTS.md 1), all three done: with
con_rows_owed painting the whole row instead of the dirty span (the old
con_row; since the size pass, con_rowspan given columns 0..255), leg 1
reads 67,869 cycles and fails the budget; with con_rows_owed's con_curmove
removed, leg 3 finds 32 stale pixels under the caret's cells; with con_compose's
old `mov al, ch` before the AND put back, the caret is 0 of 8 lit.

    python3 tests/doswcell.py [--machine os8088_xt_vga] [--report]
"""

import os
import shutil
import statistics
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwcell.asm")
DC_N = 24
DC_MAXCYC = 20000           # a one-cell pass; the whole row is ~4x this
MACHINES = ("os8088_5150_herc_gla", "os8088_xt_vga")


def fail(msg):
    print("doswcell: FAIL: %s" % msg)
    sys.exit(1)


class Dos(object):
    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    def base(self):
        return dosmap.lin(self.m, dosmap.instance(self.m))

    def addr(self, name):
        return self.base() + self.dm[name]

    def b(self, name):
        return dosmap.cellread(self.m, self.dm, self.base(), name, 1)[0]

    def psp(self, off):
        psp = int.from_bytes(self.m.read(self.addr("dos_ldpsp"), 2), "little")
        return int.from_bytes(self.m.read(psp * 16 + off, 2), "little")

    def phase(self):
        return self.psp(0x105)

    def count(self):
        return self.psp(0x103)


def until(m, cond, what, limit):
    try:
        os88marty.until(m, lambda _m: cond(), what, poll=0.25, limit=limit)
        return True
    except os88marty.MartyError:
        return False


def trace_phase(m, dos, ph):
    """Every con_rows_owed pass while phase `ph` runs, priced."""
    names = ("con_rows_owed", "con_rows_owed.done", "con_rows_owed.next", "con_compose",
             "con_emit", "con_blitrun")
    tg = [dos.addr(n) for n in names]
    by = dict(zip(tg, names))
    if not until(m, lambda: dos.phase() == ph and dos.count() >= 2,
                 "phase %d under way" % ph, 60):
        fail("the probe never reached phase %d (phase %d, count %d)"
             % (ph, dos.phase(), dos.count()))
    with os88marty.bp_trace(m, *tg, log=True) as tr:
        tr.until(lambda: dos.phase() != ph or dos.count() >= DC_N - 2,
                 "phase %d's updates" % ph, 120)
        hits = list(tr.hits)
    passes, cur = [], None
    for h in hits:
        n = by.get(h["addr"])
        if n == "con_rows_owed":
            cur = {"t0": h["cycles"], "rows": 0, "blits": 0,
                   "tc": 0, "te": 0}
        elif cur is None:
            continue
        elif n == "con_rows_owed.next":
            if cur["te"] and not cur.get("emit"):
                cur["emit"] = h["cycles"] - cur["te"]
        elif n == "con_rows_owed.done":
            cur["cyc"] = h["cycles"] - cur["t0"]
            passes.append(cur)
            cur = None
        elif n == "con_compose":
            cur["rows"] += 1
            cur["tc"] = h["cycles"]
        elif n == "con_emit":
            cur["te"] = h["cycles"]
            cur["comp"] = h["cycles"] - cur["tc"]
        elif n == "con_blitrun":
            cur["blits"] += 1
    one = [p for p in passes if p["rows"] == 1]
    if len(one) < 6:
        fail("phase %d: only %d one-row passes in %d traced (%r)"
             % (ph, len(one), len(passes), passes[:4]))
    return one


def repaint(m, dos):
    """The glass as it stands, then EVERY ROW MARKED WHOLE from the host and
    repainted: returns the capture taken BEFORE the forced repaint and leaves
    the machine with it done (so the next call's capture is the full one)."""
    drb = lambda: m.read(dos.addr("con_drb"), 4)
    os88marty.pace(m, 0.5)
    shot = m.fbuf()
    m.pause()
    m.write(dos.addr("con_dc0"), bytes(25))
    m.write(dos.addr("con_dc1"), bytes([79] * 25))
    m.write(dos.addr("con_drb"), b"\xff\xff\xff\x01")
    m.write(dos.addr("dvw_dirty"), b"\x01")
    m.run()
    if not until(m, lambda: drb() == b"\0\0\0\0", "the full repaint", 10):
        fail("the forced full repaint never ran")
    return shot


def band_diff(a, b, x0, y0, vc, vr):
    """(pixels, first cell, last cell, first row, last row) differing inside
    the band between two fbuf captures, or None."""
    w, h, pa = a
    pb = b[2]
    xs, ys = [], []
    for y in range(max(0, y0), min(h, y0 + vr * 8)):
        row = y * w * 3
        lo, hi = row + max(0, x0) * 3, row + min(w, x0 + vc * 8) * 3
        if pa[lo:hi] == pb[lo:hi]:
            continue
        for x in range(max(0, x0), min(w, x0 + vc * 8)):
            i = row + x * 3
            if pa[i:i + 3] != pb[i:i + 3]:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return (len(xs), (min(xs) - x0) // 8, (max(xs) - x0) // 8,
            (min(ys) - y0) // 8, (max(ys) - y0) // 8, min(xs), min(ys))


def run(disk, machine, report):
    with os88ui.boot(SYS, apps=disk, machine=machine) as ui:
        m = ui.m
        if not ui.path("B:/DWCELL.COM"):
            fail("could not launch DWCELL.COM")
        dos = Dos(m)
        # the double-click runs it in the BRACKET first (doswlive leg 1)
        if not until(m, lambda: any("PRESS A KEY" in r
                                    for r in (m.screen() or [])),
                     "the bracketed run's prompt", 90):
            fail("DWCELL.COM never asked for its key inside the bracket")
        m.type_text("x")
        if not until(m, lambda: not dos.b("dos_inbr"), "the bracket down", 48):
            fail("the bracket never came down")
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        if not until(m, lambda: dos.b("dvw_run") == 1, "the worker's run", 20):
            fail("Run in Window did not put the program on the worker")

        res = {}
        for ph, what in ((1, "AH=09h in place"), (2, "teletype + BS")):
            one = trace_phase(m, dos, ph)
            cyc = sorted(p["cyc"] for p in one)
            med = statistics.median(cyc)
            blits = statistics.median(p["blits"] for p in one)
            comp = statistics.median(p.get("comp", 0) for p in one)
            emit = statistics.median(p.get("emit", 0) for p in one)
            res[ph] = med
            print("doswcell: %s %d %s: %d one-row passes, median %d cycles "
                  "(%.2f ms), min %d; compose %d, emit %d, blit runs %g"
                  % (machine, ph, what, len(one), med,
                     1000.0 * med / os88marty.GUEST_HZ, cyc[0], comp, emit,
                     blits))
            if not report and med > DC_MAXCYC:
                fail("phase %d (%s): a one-cell update's pass is a median %d "
                     "guest cycles against a budget of %d - the band is "
                     "composing and blitting the whole row, not the span"
                     % (ph, what, med, DC_MAXCYC))

        # --- 3: the incremental glass equals the full repaint ---------------
        if not until(m, lambda: dos.phase() == 3, "the key prompt", 60):
            fail("the probe never reached its key prompt")
        drb = lambda: m.read(dos.addr("con_drb"), 4)
        if not until(m, lambda: drb() == b"\0\0\0\0", "the band painted", 10):
            fail("the band never caught up with the console (%s)"
                 % drb().hex())
        os88marty.pace(m, 0.5)
        x0 = int.from_bytes(m.read(dos.addr("con_px"), 2), "little")
        y0 = (int.from_bytes(m.read(dos.addr("con_oy"), 2), "little")
              + int.from_bytes(m.read(dos.addr("con_topy"), 2), "little"))
        vc = int.from_bytes(m.read(dos.addr("con_vcols"), 2), "little")
        vr = int.from_bytes(m.read(dos.addr("con_vrows"), 2), "little")
        a = repaint(m, dos)
        w, h = a[0], a[1]
        b = repaint(m, dos)
        # THE INSTRUMENT IS CALIBRATED BY A CELL IT CAN SEE: fbuf is the
        # card's raster with its border, so screen (x, y) is not fbuf (x, y)
        # (on Hercules it is 720x350 against 720x348). A solid block poked
        # into the buffer with no mark is invisible until a forced repaint
        # draws it, and where it lands is the offset - a comparison cut to
        # the wrong rect would pass whatever the renderer did.
        cell = (20 * 80 + 70) * 2
        m.write(dos.addr("con_scr") + cell, b"\xdb\x07")
        repaint(m, dos)
        os88marty.pace(m, 0.5)
        c = m.fbuf()
        # below the menu bar only: its clock ticks on its own, and a minute
        # turning between the two captures is 31 pixels that are not the cell
        pk = band_diff(b, c, 0, y0, w // 8, (h - y0) // 8 + 1)
        if not pk or pk[0] != 64:
            fail("a solid cell poked at row 20 column 70 came back from the "
                 "forced repaint as %r, not 64 pixels in one cell" % (pk,))
        ox = pk[5] - (x0 + 70 * 8)
        oy = pk[6] - (y0 + 20 * 8)
        box = band_diff(a, b, x0 + ox, y0 + oy, vc, vr)
        if box:
            fail("%d pixels of the band changed when every row was repainted "
                 "whole, in cells %d..%d of rows %d..%d: the span renderer "
                 "left stale cells on the glass" % box[:5])
        # ...AND THE CARET IS ON IT: two lit scanlines under the cursor's
        # cell (SPEC.md 70.8.2). Both captures agreed for the life of the
        # renderer while it drew the caret on NO cell (con_compose cleared
        # what it had just lit), so equality alone cannot say it is there
        cx, cy = dos.b("con_cx"), dos.b("con_cy")
        ink = lambda px: px != b"\0\0\0"
        for r in (6, 7):
            yy = y0 + oy + cy * 8 + r
            row = [b[2][((yy * w) + x) * 3:((yy * w) + x) * 3 + 3]
                   for x in range(x0 + ox + cx * 8, x0 + ox + cx * 8 + 8)]
            if not all(ink(px) for px in row):
                fail("the caret at row %d column %d is not drawn: scanline "
                     "%d of its cell has %d of 8 lit pixels"
                     % (cy, cx, r, sum(ink(px) for px in row)))
        print("doswcell: %s 3 the band after %d incremental updates is the "
              "full repaint's, to the pixel (%dx%d cells; fbuf offset %d,%d)"
              % (machine, 2 * DC_N, vc, vr, ox, oy))
        m.type_text("q")
        return res


def main():
    args = sys.argv[1:]
    report = "--report" in args
    machines = MACHINES
    if "--machine" in args:
        machines = (args[args.index("--machine") + 1],)
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswcell")
    try:
        com = os.path.join(tmp, "DWCELL.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwcell360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        for mc in machines:
            run(disk, mc, report)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswcell: ok")


if __name__ == "__main__":
    main()
