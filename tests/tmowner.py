#!/usr/bin/env python3
"""A raise cache is listed under PURGEABLE, whoever caused it (SPEC.md 28.4.10)

    make && python3 tests/tmowner.py [--machine os8088_xt_vga]

MEM_P_WSAVE + slot is a KERNEL tag, so the heap page's System arm once
swallowed every raise cache: tens of KB of a 640KB machine appeared on the one
page that reports memory as a `WinSave` row belonging to the kernel. SPEC.md
28.4.5 then filed it under the package that owns the window. Since the memory
and heap pages merged (KERN-MEGA-PLAN 14.70.2) a purgeable claim is its OWN
group, `Purgeable`: it is in nobody's total (SPEC.md 28.4.1), so filing it
under an owner made that owner's rows add to more than its heading.

  KERNEL   cover the Disk window - a window the KERNEL made, with no instance
           behind it - and its cache must be listed under Purgeable, not
           System
  PACKAGE  cover the Calculator and its cache must be listed under Purgeable
           too, not under the CALCULATOR

The groups open on a click and start closed, so the page is opened with
`+ALL` once - the expansion is the package's state and survives the page
cycling that the capture below retries with.

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
TM_PEN, TMM_HDR_Y = 8, 66               # apps/taskmgr/taskmgr.asm
fails = []


def tick(mm, card=None):
    mm.advance(frames=110)
    mm.run()


class Rows(dispcells.Pump):
    """Every line the package composes, as it draws it.

    The memory page letters each row in TM_CHUNK-character pieces left to
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
        if not (self.stub <= ret < self.stub + 12):
            return                              # the kernel's own drawing
        t = self.m.read(((r["es"] & 0xFFFF) << 4) + (r["si"] & 0xFFFF), 48)
        n = t.find(b"\0")
        self.rows.append((r["cx"] & 0xFFFF, r["dx"] & 0xFFFF,
                          t[:48 if n < 0 else n].decode("latin-1")))

    def raw_frames(self):
        out, cur, last = [], [], -1
        for x, y, t in self.rows:
            if y < last and cur:
                out.append(cur)
                cur = []
            cur.append((x, y, t))
            last = y
        if cur:
            out.append(cur)
        return out

    def frames(self):
        """...only the frames whose every row kept its FIRST chunk.

        The pump loses stops (see the header). A row missing its leftmost one
        has lost TMH_IND with it and reads as a heading, which is the one loss
        that changes this gate's answer - so it is the one tested for, and a
        frame carrying it is dropped rather than reasoned about.
        """
        return [self.join(f) for f in self.raw_frames() if self.usable(f)]

    @staticmethod
    def usable(frame):
        by = {}
        for x, y, _ in frame:
            by.setdefault(y, []).append(x)
        if not by:
            return False
        pen = min(x for v in by.values() for x in v)
        return all(pen in v for v in by.values())

    @staticmethod
    def join(frame):
        by = {}
        for x, y, t in frame:
            by.setdefault(y, []).append((x, t))
        return [(y, "".join(t for _, t in sorted(by[y])).rstrip())
                for y in sorted(by)]


def group_in(allframes, addr):
    """The group a claim row sits under: rows are indented, a group's row is
    not - it starts with its '+' or '-' (TMH_IND). Searched across every repaint captured, because which one
    is the full list depends on where the pump happened to start - and a
    PARTIAL repaint is refused outright, since its first row can be a
    fragment with no indent, which reads as a heading and files everything
    under it."""
    for rows in sorted(allframes, key=len, reverse=True):
        if not any("Purgeable" in t for _, t in rows):
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
    stub = m.sym("api_x") - (KSEG << 4)      # SPEC.md 20.3's shared X body

    def tmw(name, n=2):
        b = m.read((seg << 4) + dispapps.img_size("taskmgr")
                   + dispapps.bss_off("taskmgr", name), n)
        return int.from_bytes(b, "little")

    def all_open():
        return tmw("tm_exp", 4) == 0xFFFFFFFF

    def plus_all():
        """The header's `+ALL`, off the page's own origin and rise."""
        return (tmw("tm_cx") + TM_PEN + 22 * 8 + 4,
                tmw("tm_cy") + TMM_HDR_Y - tmw("tm_xoff") + 4)

    def to_heap(cap, want):
        """...onto the memory page, capturing until the answer is READABLE.

        Not until a fixed number of clicks: the pump drops stops, so arriving
        at the page is not the same as having captured it whole. Each click
        cycles the view, so every second one is a fresh full repaint of this
        page to try again with - which is the RETRY dispcells.Pump.serve asks
        for, spelled as a condition rather than a hope.
        """
        for _ in range(12):
            ww = W(slot)
            cap.click(ww.x + 20, ww.y + TITLE_H + 40)
            cap.pump(1200, 4)
            if view() != 1:
                continue
            if not all_open():          # once: +ALL, at the header's right
                cap.click(*plus_all())  # end, and the next cycle round is
                cap.pump(1200, 4)       # a full repaint with every group open
                continue
            if not want:                # nothing was banked, so there is no
                return len(cap.rows) > 40   # row to wait for and the leg
            if group_in(                    # below says that for itself
                    [f for f in cap.frames() if len(f) > 3], want)[0]:
                return True
        return False

    # --- KERNEL ------------------------------------------------------------
    drag(c.x + 30, c.y + 9, d.x + 40 + 30, d.y + 20 + 9, settle=False)
    ds = su(disk)
    kp = Rows(m, mo, stub)
    ok = to_heap(kp, ds)
    kp.close()
    mo.to(*dispcorner.PARK)
    for _ in range(4):
        tick(m)
    dh, drow, _ = group_in([f for f in kp.frames() if len(f) > 3], ds)
    print("KERNEL  : view %d, Disk cache %04X -> %r" % (view(), ds, dh))
    if view() != 1:
        fails.append("KERNEL: never reached the memory page, so this read the "
                     "wrong list")
    elif not ds:
        fails.append("KERNEL: the Disk window banked nothing when the "
                     "Calculator landed on it, so there is no row to file")
    elif not ok:
        fails.append("KERNEL: 12 cycles of the page and not one capture came "
                     "back with every row's first chunk AND a row naming "
                     "%04X. That is the HARNESS (dispcells.Pump.serve drops "
                     "stops) unless the page has genuinely stopped listing "
                     "the cache - the frames are printed above" % ds)
    elif drow is None:
        fails.append("KERNEL: no row names its cache %04X - the list was "
                     "never composed in full under capture" % ds)
    elif "Purgeable" not in dh:
        fails.append("KERNEL: a raise cache is purgeable, so it is listed "
                     "under Purgeable whoever caused it (SPEC.md 28.4.10) - "
                     "the Disk window's is under %r" % dh)

    # --- PACKAGE -----------------------------------------------------------
    d, c = W(disk), W(calc)
    drag(d.x + 30, d.y + 9, c.x + 40 + 30, c.y + 20 + 9, settle=False)
    cs, cseg = su(calc), segs.get(calc)
    rp = Rows(m, mo, stub)                      # ...and cycle the page round
    okp = to_heap(rp, cs)                       # to compose the list IN FULL:
    rp.close()                                  # SPEC.md 28.10.2 means the
    mo.to(*dispcorner.PARK)                     # drag's own repaint draws only
    for _ in range(4):                          # the chunks that changed, so
        tick(m)                                 # there is no whole list in it
    if view() != 1:
        fails.append("PACKAGE: never got back to the memory page")
    elif not cs:
        fails.append("PACKAGE: the Calculator banked nothing when the Disk "
                     "window landed on it, so there is no row to file")
    elif not okp:
        fails.append("PACKAGE: 12 cycles and no capture came back with every "
                     "row's first chunk AND a row naming %04X - the harness, "
                     "unless the page has stopped listing it" % cs)
    ch, crow, cf = group_in([f for f in rp.frames() if len(f) > 3], cs)
    print("PACKAGE : Calc cache %04X -> %r (its segment %04X)"
          % (cs, ch, cseg or 0))
    if not cs:
        fails.append("PACKAGE: the Calculator banked nothing when the Disk "
                     "window landed on it, so there is no row to file")
    elif crow is None:
        fails.append("PACKAGE: no row names its cache %04X - the list was "
                     "never composed in full under capture" % cs)
    elif "Purgeable" not in ch:
        fails.append("PACKAGE: a raise cache is purgeable and in nobody's "
                     "total, so it is listed under Purgeable and not under "
                     "its window's package (SPEC.md 28.4.10) - the "
                     "Calculator's is under %r" % ch)

    print()
    for f in fails:
        print("FAIL  " + f)
    if not fails:
        print("PASS  both raise caches are Purgeable's, the kernel window's "
              "and the package's")
    sys.exit(1 if fails else 0)
