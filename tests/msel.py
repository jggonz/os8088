#!/usr/bin/env python3
"""SPEC.md 22.27: MULTI-SELECT in a Disk window, and the operations on a set.

Driven on MartyPC in B:\\APPS and B:\\ (kern_big, the 360KB pair). Every
selection check reads the window's OWN state block - FS_MSEL, the bitmap, and
FS_SEL, the focus - and then the GLASS: which visible rows are inverted, so a
band left behind or drawn twice is caught as well as a wrong bit.

  1. a click selects one row; Ctrl+click adds a second, Ctrl+click on the first
     takes it out again - one bit, one band, each time;
  2. Shift+click takes the run from the anchor (the last Ctrl+click) to the
     row clicked;
  3. Shift+Down extends the run, Shift+Up takes it back;
  4. a plain click collapses to one row; Ctrl+A selects everything;
  5. a DRAG SELECT from right of the name column selects the run of rows from
     where it started to where the pointer is, down or up - and nothing else;
     a press there that does not move selects that row alone;
  6. Copy of a two-entry set, Up, Paste: both land in B:\\, and the list is
     CLEARED on paste (the clipboard empty, its claim freed);
  7. dragging one member of a set onto a folder MOVES the whole set, and a
     Ctrl+drag onto '..' COPIES - the source is still there afterwards;
  8. Delete on a set asks about "2 items", and the second Delete removes both.

VERIFIED TO FAIL: built `make MSELOFF=1` the first Ctrl+click is an ordinary
click and leg 1 is red; with fm_ondrag's FM_BBAND dispatch taken out, the
four drag-select checks of leg 5 are red and nothing else is - and a drag
select sampled before fm_mto entered the bitmap left the bits right and the
glass wrong (the anchor row's band XORed off), which only the band checks
saw.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom as geom                                     # noqa: E402
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402

fails = []

# kern_big's FS_MSEL; an MSELOFF kernel has none, and then the red run reads
# whatever sits there - which is what makes it red
FS_MSEL = os88sym.equates().get("FS_MSEL", 61)
BUILD = os.environ.get("OS88_BUILD", "build")
NAMEX = 120


def check(name, cond, note=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, note))
    if not cond:
        fails.append(name)


def word(ui, blk, off):
    return int.from_bytes(ui.m.read(blk + off, 2), "little")


def bits(ui, blk):
    b = ui.m.read(blk + FS_MSEL, 8)
    return sorted(i for i in range(64) if b[i >> 3] & (1 << (i & 7)))


def bands(ui, w, fit):
    """Which visible rows are drawn INVERTED (fmarrows.py's reading)."""
    x0 = w.x + 1 + 2
    x1 = w.x + w.w - 2 - 18
    y0 = w.y + geom.TITLE_H + 1 + geom.FM_ROW_Y0
    W, H, px = ui.m.fbuf()
    out = []
    for r in range(fit):
        dark = tot = 0
        for y in range(y0 + r * geom.FM_ROW_H + 2,
                       y0 + (r + 1) * geom.FM_ROW_H - 2):
            for x in range(x0, x1, 2):
                dark += px[(y * W + x) * 3] < 0x40
                tot += 1
        if dark * 2 > tot:
            out.append(r)
    return out


def held(ui, key, fn):
    """`fn()` with `key` held down, as a hand on the keyboard holds it."""
    ui.m.key(key, down=True, up=False)
    os88marty.pace(ui.m, 0.05)
    try:
        fn()
    finally:
        ui.m.key(key, down=False, up=True)
        os88marty.pace(ui.m, 0.15)


def click(ui, w, row, x=None):
    cx, cy = ui.row_xy(w, row)
    if x is not None:
        cx = w.x + 1 + x
    ui.mo.click(cx, cy, settle=0)
    ui.settle()


def state(ui, blk, w, fit, want, what):
    sel = bits(ui, blk)
    check(what + ": the set is %r" % (want,), sel == want, "(bits %r)" % sel)
    scr = ui.scroll(w)
    vis = [i - scr for i in want if 0 <= i - scr < fit]
    if not want:
        vis = [] if word(ui, blk, geom.FS_SEL) == 0xFFFF else \
            [word(ui, blk, geom.FS_SEL) - scr]
    got = bands(ui, w, fit)
    check(what + ": ...and exactly those bands on the glass", got == vis,
          "(inverted rows %r, want %r)" % (got, vis))


def names(ui, w):
    return [n for n, _t in ui.listing(w)]


with os88ui.boot(os.path.join(BUILD, "os8088-360.img"),
                 apps=os.path.join(BUILD, "apps360.img")) as ui:
    m = ui.m
    S = ui._S
    w = ui.path("B:/APPS")
    ui.settle()
    blk = ui._fsblk(w)
    n = word(ui, blk, geom.FS_N)
    fit = ui._word("fm_fit")
    print("== SPEC.md 22.27: multi-select (B:\\APPS, %d entries, %d rows "
          "fit) ==" % (n, fit))

    # --- 1: click, Ctrl+click, Ctrl+click off --------------------------------
    click(ui, w, 0)
    check("a click selects row 0 alone", word(ui, blk, geom.FS_SEL) == 0
          and bits(ui, blk) == [])
    held(ui, "ControlLeft", lambda: click(ui, w, 2))
    state(ui, blk, w, fit, [0, 2], "Ctrl+click row 2")
    held(ui, "ControlLeft", lambda: click(ui, w, 0))
    state(ui, blk, w, fit, [2], "Ctrl+click row 0 again")

    # --- 2: Shift+click: the run from the anchor ------------------------------
    held(ui, "ShiftLeft", lambda: click(ui, w, 4))
    state(ui, blk, w, fit, [0, 1, 2, 3, 4], "Shift+click row 4 (anchor 0)")

    # --- 3: Shift+Down, Shift+Up ---------------------------------------------
    def sdown():
        m.key("ArrowDown")
        ui.settle()
    held(ui, "ShiftLeft", sdown)
    state(ui, blk, w, fit, [0, 1, 2, 3, 4, 5], "Shift+Down")
    def sup2():
        m.key("ArrowUp")
        m.key("ArrowUp")
        ui.settle()
    held(ui, "ShiftLeft", sup2)
    state(ui, blk, w, fit, [0, 1, 2, 3], "Shift+Up twice")
    ui.scroll_to(0, win=w)              # back to the top: rows ARE entries
    ui.settle()

    # --- 4: a plain click collapses; Ctrl+A takes everything ------------------
    click(ui, w, 1)
    check("a plain click: one row again", bits(ui, blk) == []
          and word(ui, blk, geom.FS_SEL) == 1)
    check("...and one band", bands(ui, w, fit) == [1],
          "(%r)" % bands(ui, w, fit))
    held(ui, "ControlLeft", lambda: (m.key("KeyA"), ui.settle()))
    state(ui, blk, w, fit, list(range(n)), "Ctrl+A")
    click(ui, w, 0)

    # --- 5: the drag select, from right of the name column -------------------
    x0, y0 = ui.row_xy(w, 1)
    x1, y1 = ui.row_xy(w, 3)
    ui.mo.drag(w.x + 1 + 200, y0, w.x + 1 + 230, y1, settle=0)
    ui.settle()
    state(ui, blk, w, fit, [1, 2, 3], "a drag select down rows 1..3")
    ui.mo.drag(w.x + 1 + 200, y1, w.x + 1 + 200, y0 - geom.FM_ROW_H,
               settle=0)
    ui.settle()
    state(ui, blk, w, fit, [0, 1, 2, 3], "a drag select UP rows 3..0")
    click(ui, w, 2, x=200)
    check("a press right of the names that does not move selects that row",
          bits(ui, blk) == [] and word(ui, blk, geom.FS_SEL) == 2
          and bands(ui, w, fit) == [2],
          "(bits %r, sel %04X, bands %r)" % (bits(ui, blk),
                                             word(ui, blk, geom.FS_SEL),
                                             bands(ui, w, fit)))
    click(ui, w, 0)

    # --- 6: Copy a set, Up, Paste ---------------------------------------------
    vseg = word(ui, blk, geom.FS_VSEG)
    rows = ui.listing(w)
    sizes = []
    for i, (nm, ty) in enumerate(rows[:fit]):
        if ty in (0, 1):
            sz = int.from_bytes(m.read((vseg << 4) + i * geom.DSK_DE_STRIDE
                                       + 20, 4), "little")
            sizes.append((sz, i, nm))
    sizes.sort()
    (_s1, i1, n1), (_s2, i2, n2) = sizes[0], sizes[1]
    print("  copying %s (%d) and %s (%d)" % (n1, _s1, n2, _s2))
    click(ui, w, i1)
    held(ui, "ControlLeft", lambda: click(ui, w, i2))
    held(ui, "ControlLeft", lambda: (m.key("KeyC"), ui.settle()))
    check("Copy put a LIST of two on the clipboard",
          ui._word("fcp_lcnt") == 2 * 16 and ui._word("fcp_lseg") != 0
          and ui._byte("fcp_cbop") == 1,
          "(lcnt %d, lseg %04X, op %d)" % (ui._word("fcp_lcnt"),
                                          ui._word("fcp_lseg"),
                                          ui._byte("fcp_cbop")))
    m.key("Backspace")
    ui._wait(lambda: "APPS" in names(ui, w), "Up to B:\\", 30.0)
    held(ui, "ControlLeft", lambda: m.key("KeyV"))
    try:
        ui._wait(lambda: n1 in names(ui, w) and n2 in names(ui, w),
                 "both copies in B:\\", 120.0)
    except os88ui.UIError:
        print("  DIAG: root %r toast %r err %d busy %d lcnt %d "
              "op %d floor %02X" % (names(ui, w), ui.toast(),
                                    ui._byte("fcp_err"), ui._byte("fcp_busy"),
                                    ui._word("fcp_lcnt"),
                                    ui._byte("fcp_cbop"),
                                    ui._byte("mem_pg_floor")))
        raise
    ui.settle()
    check("Paste copied BOTH into B:\\", True, "(%r)" % names(ui, w))
    check("...and the list is CLEARED on paste",
          ui._word("fcp_lcnt") == 0 and ui._word("fcp_lseg") == 0
          and ui._byte("fcp_cbop") == 0)
    check("...and the purge floor came back down",
          ui._byte("mem_pg_floor") == 0xFF)

    # --- 7: drag one member of the set onto a folder: the whole set moves -----
    root = ui.listing(w)
    j1 = [i for i, (nm, _t) in enumerate(root) if nm == n1][0]
    j2 = [i for i, (nm, _t) in enumerate(root) if nm == n2][0]
    g = [i for i, (nm, _t) in enumerate(root) if nm == "GAMES"][0]
    click(ui, w, j1)
    held(ui, "ControlLeft", lambda: click(ui, w, j2))
    sx, sy = ui.row_xy(w, j2)
    gx, gy = ui.row_xy(w, g)
    ui.mo.drag(sx, sy, gx, gy, settle=0)
    ui._wait(lambda: n1 not in names(ui, w) and n2 not in names(ui, w),
             "both gone from B:\\", 120.0)
    check("a drag of one member moved BOTH out of B:\\", True)
    w = ui.path("B:/GAMES")
    ui.settle()
    blk = ui._fsblk(w)
    got = names(ui, w)
    check("...into GAMES", n1 in got and n2 in got, "(%r)" % got)

    # --- 7b: Ctrl+drag is a COPY: the source stays ---------------------------
    # B:\APPS's CALC.O88 Ctrl+dragged onto its '..' row: a copy into B:\
    w = ui.path("B:/APPS")
    ui.settle()
    rows = ui.listing(w)
    up = [i for i, (_n, t) in enumerate(rows) if t == 3]
    src = [i for i, (nm, _t) in enumerate(rows) if nm == n1]
    if up and src and max(up[0], src[0]) < fit:
        ax_, ay_ = ui.row_xy(w, src[0])
        ux_, uy_ = ui.row_xy(w, up[0])
        held(ui, "ControlLeft", lambda: ui.mo.drag(ax_, ay_, ux_, uy_,
                                                    settle=0))
        ui.settle()
        check("Ctrl+drag left the source where it was", n1 in names(ui, w),
              "(%r)" % names(ui, w))
        w = ui.path("B:/")
        ui.settle()
        check("...and put a COPY in B:\\", n1 in names(ui, w),
              "(%r)" % names(ui, w))
    else:
        check("'..' and %s both on screen in B:\\APPS" % n1, False,
              "(%r)" % rows)
    w = ui.path("B:/GAMES")
    ui.settle()
    blk = ui._fsblk(w)
    got = names(ui, w)

    # --- 8: Delete the set ----------------------------------------------------
    k1 = got.index(n1)
    k2 = got.index(n2)
    s0 = ui.scroll(w)
    check("both on screen in GAMES", s0 <= min(k1, k2)
          and max(k1, k2) < s0 + fit, "(rows %d, %d, scroll %d)" % (k1, k2, s0))
    click(ui, w, k1 - s0)
    held(ui, "ControlLeft", lambda: click(ui, w, k2 - s0))
    check("the set is the two copies", bits(ui, blk) == sorted([k1, k2]),
          "(%r)" % bits(ui, blk))
    m.key("Delete")
    ui.settle()
    onam = bytes(m.read(S("fm_onam"), 13)).split(b"\0")[0].decode()
    check("Delete asks about '2 items'", onam == "2 items", "(%r)" % onam)
    os88marty.pace(m, 0.6)              # SPEC.md 22: a typematic repeat is
    m.key("Delete")                     # not an answer
    try:
        ui._wait(lambda: n1 not in names(ui, w) and n2 not in names(ui, w),
                 "both deleted", 120.0)
    except os88ui.UIError:
        print("  DIAG: %r toast %r" % (names(ui, w), ui.toast()))
        raise
    after = names(ui, w)
    check("...and the second Delete removed both, and only them",
          after == [x for x in got if x not in (n1, n2)], "(%r)" % after)

print()
if fails:
    print("FAILURES:")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("all pass")
