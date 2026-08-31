#!/usr/bin/env python3
"""A raise cache is listed under the PACKAGE that owns it (SPEC.md 28.4.5)

    make && python3 tests/tmowner.py [--machine os8088_xt_vga]

MEM_P_WSAVE + slot is a KERNEL tag, so tm_hmatch's System arm swallowed every
raise cache: tens of KB of a 640KB machine appeared on the one page that
reports memory as an anonymous `WinSave` row belonging to nobody. The slot in
that tag names a WINDOW, and OSAPI_WM_OWNSEG (11.96.3.1) turns it into the
segment that owns it - which is what tm_ispt already holds per instance.

  KERNEL   cover the Disk window - a window the KERNEL made, with no instance
           behind it - and its cache must be listed under System, which is
           where it was all along and where it belongs
  PACKAGE  cover the Calculator and its cache must be listed under the
           CALCULATOR, in the same group as its own region claim

READ OFF THE COMPOSED ROWS, not the pixels. The page letters each row in
TM_CHUNK-character pieces left to right, one row per y, top down, and every
line goes through the package's own font_run - so grouping the calls on y and
sorting on x reassembles the list exactly as drawn, headings and indents
included. Nothing else here can answer WHICH GROUP a row is in.

The two halves are in this order and cannot be swapped: the drag that makes
the Calculator hold a cache raises the Disk window, and its cache goes with
the redraw.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp, dispcorner, dispapps
import dispcells

MACHINE = "os8088_xt_vga"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
KSEG = os88geom.KERNEL_SEG
TITLE_H = 18
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


class Rows(dispcells.Pump):
    """Every line the package composes, as it draws it.

    The heap page letters each row in TM_CHUNK-character pieces left to
    right, one row per y, top down - so grouping on y and sorting on x
    reassembles the row, and a y that goes BACKWARDS starts a new repaint.
    Reading the page this way rather than off the glass is what makes an
    assertion about WHICH GROUP a row is in possible at all.
    """

    def __init__(self, mm, mmo, stub):
        self.stub, self.rows = stub, []
        dispcells.Pump.__init__(self, mm, mmo, ["font_run_x"])

    def on_stop(self, r, sym):
        b = self.m.read(((r["ss"] & 0xFFFF) << 4) + (r["sp"] & 0xFFFF), 2)
        ret = b[0] | (b[1] << 8)
        if not (self.stub <= ret < self.stub + 16):
            return                              # the kernel's own drawing
        t = self.m.read(((r["es"] & 0xFFFF) << 4) + (r["si"] & 0xFFFF), 48)
        n = t.find(b"\0")
        self.rows.append((r["cx"] & 0xFFFF, r["dx"] & 0xFFFF,
                          t[:48 if n < 0 else n].decode("latin-1")))

    def frames(self):
        out, cur, last = [], [], -1
        for x, y, t in self.rows:
            if y < last and cur:
                out.append(cur)
                cur = []
            cur.append((x, y, t))
            last = y
        if cur:
            out.append(cur)
        return [self.join(f) for f in out]

    @staticmethod
    def join(frame):
        by = {}
        for x, y, t in frame:
            by.setdefault(y, []).append((x, t))
        return [(y, "".join(t for _, t in sorted(by[y])).rstrip())
                for y in sorted(by)]


def group_in(allframes, addr):
    """The heading a claim row sits under: rows are indented, headings are
    not (TMH_IND). Searched across every repaint captured, because which one
    is the full list depends on where the pump happened to start - and a
    PARTIAL repaint is refused outright, since its first row can be a
    fragment with no indent, which reads as a heading and files everything
    under it."""
    for rows in sorted(allframes, key=len, reverse=True):
        if not any(t.startswith("System") for _, t in rows):
            continue
        head = None
        for _, t in rows:
            if not t:
                continue
            if t[0] != " ":
                head = t
            elif addr and ("%04X" % addr) in t:
                return head, t, rows
    return None, None, None


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    os88marty.no_saver(m)

    def su(i):
        b = m.read(S("wm_su_segs") + i * 2, 2)
        return b[0] | (b[1] << 8)

    def W(i):
        return [x for x in os88geom.windows(m) if x.i == i][0]

    def drag(x0, y0, x1, y1, settle=True):
        mo.to(x0, y0); mo._edge(True); mo.to(x1, y1, l=True); mo._edge(False)
        mo.to(*dispcorner.PARK)
        if settle:
            os88marty.settle(m, limit=120)
        else:
            for _ in range(6):
                tick(m)

    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    dx, dy, _, _ = dispcp.win_rect(m, S, disk)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "APPS")
    dispcp.open_named(m, mo, S, tick, dx, dy, "CALC.O88")
    for _ in range(4):
        tick(m)
    calc = [x.i for x in os88geom.windows(m) if x.i != disk][-1]
    c = W(calc)
    drag(c.x + 30, c.y + 9, 380 + 30, 300 + 9)      # ...off the Disk list, or
    mo.click(W(disk).x + 30, W(disk).y + 9)         # the clicks below land on
    mo.to(*dispcorner.PARK)                         # IT
    os88marty.settle(m, limit=120)
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, dx, dy, "SYSTEM")
    dispcp.open_named(m, mo, S, tick, dx, dy, "TASKMGR.O88")
    for _ in range(4):
        tick(m)
    got, i, segs = None, 0, {}
    while dispapps.pkg_seg(m, i) is not None:
        got = dispapps.pkg_seg(m, i)
        segs[got[0]] = got[1]
        i += 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % os88geom.windows(m))
    slot, seg = got

    def view():
        return m.read((seg << 4) + dispapps.img_size("taskmgr")
                      + dispapps.bss_off("taskmgr", "tm_view"), 1)[0]

    # Park all three clear: the page top right, Disk bottom left, Calc middle.
    w = W(slot); drag(w.x + 30, w.y + 9, 396 + 30, 26 + 9, settle=False)
    d = W(disk); drag(d.x + 30, d.y + 9, 8 + 30, 264 + 9, settle=False)
    c = W(calc); drag(c.x + 30, c.y + 9, 8 + 30, 40 + 9, settle=False)
    w, d, c = W(slot), W(disk), W(calc)
    print("SETUP   : page %r, Disk %r, Calc %r"
          % ((w.x, w.y, w.w, w.h), (d.x, d.y, d.w, d.h), (c.x, c.y, c.w, c.h)))
    stub = m.sym("api_font_run") - (KSEG << 4)

    def to_heap(cap):
        """...onto the heap page, capturing every repaint on the way."""
        for _ in range(8):
            ww = W(slot)
            cap.click(ww.x + 20, ww.y + TITLE_H + 40)
            cap.pump(1200, 4)
            if view() == 2 and len(cap.rows) > 40:
                return True
        return view() == 2

    # --- KERNEL ------------------------------------------------------------
    drag(c.x + 30, c.y + 9, d.x + 40 + 30, d.y + 20 + 9, settle=False)
    ds = su(disk)
    kp = Rows(m, mo, stub)
    ok = to_heap(kp)
    kp.close()
    mo.to(*dispcorner.PARK)
    for _ in range(4):
        tick(m)
    dh, drow, _ = group_in([f for f in kp.frames() if len(f) > 3], ds)
    print("KERNEL  : view %d, Disk cache %04X -> %r" % (view(), ds, dh))
    if not ok or view() != 2:
        fails.append("KERNEL: never reached the heap page, so this read the "
                     "wrong list")
    elif not ds:
        fails.append("KERNEL: the Disk window banked nothing when the "
                     "Calculator landed on it, so there is no row to file")
    elif drow is None:
        fails.append("KERNEL: no row names its cache %04X - the list was "
                     "never composed in full under capture" % ds)
    elif not dh.startswith("System"):
        fails.append("KERNEL: the Disk window is the kernel's own, with no "
                     "instance behind it, so its cache belongs under System - "
                     "it is under %r" % dh)

    # --- PACKAGE -----------------------------------------------------------
    d, c = W(disk), W(calc)
    drag(d.x + 30, d.y + 9, c.x + 40 + 30, c.y + 20 + 9, settle=False)
    cs, cseg = su(calc), segs.get(calc)
    rp = Rows(m, mo, stub)                      # ...and cycle the page round
    okp = to_heap(rp)                           # to compose the list IN FULL:
    rp.close()                                  # SPEC.md 28.10.2 means the
    mo.to(*dispcorner.PARK)                     # drag's own repaint draws only
    for _ in range(4):                          # the chunks that changed, so
        tick(m)                                 # there is no whole list in it
    if not okp or view() != 2:
        fails.append("PACKAGE: never got back to the heap page")
    ch, crow, cf = group_in([f for f in rp.frames() if len(f) > 3], cs)
    print("PACKAGE : Calc cache %04X -> %r (its segment %04X)"
          % (cs, ch, cseg or 0))
    if not cs:
        fails.append("PACKAGE: the Calculator banked nothing when the Disk "
                     "window landed on it, so there is no row to file")
    elif crow is None:
        fails.append("PACKAGE: no row names its cache %04X - the list was "
                     "never composed in full under capture" % cs)
    elif ch.startswith("System"):
        fails.append("PACKAGE: its cache is still under System - SPEC.md "
                     "28.4.5 files it under the package that owns the window")
    elif cseg and not any(("%04X" % cseg) in t for _, t in cf
                          if t and t[0] == " "):
        fails.append("PACKAGE: it landed under %r, but nothing in that list "
                     "names the Calculator's own segment %04X - so the group "
                     "is not demonstrably the right one" % (ch, cseg))

    print()
    for f in fails:
        print("FAIL  " + f)
    if not fails:
        print("PASS  the kernel's cache is System's and a package's is the "
              "package's")
    sys.exit(1 if fails else 0)
