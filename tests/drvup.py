#!/usr/bin/env python3
"""SPEC.md 13.8.4: a DRIVER's Control Panel page acts on the RELEASE.

    python3 tests/drvup.py [machine]

13.8.3 converted the panel's five STATIC pages and silently missed the driver
ones, because cp_pgprobe sets a kernel byte a driver's ladder has never heard
of - so a driver page told to "identify" ACTED, and a press could still mount
a disk while every static page had stopped. This is the gate for the fix, and
it drives the HARD DISK page because that page has all five species on it:
three framed buttons, a +/- pair, device ROWS and editor FIELDS.

The signal for the firing cases is the MOUNT STATE, read out of the kernel's
own volume table, because that is memory rather than a picture.

  A  press the mount button, slide OFF the pane, release -> UNCHANGED
  B  ...and the button came back UP while held (the tracking edge)
  C  press AND release on it                             -> CHANGED
  D  a press draws it DOWN
  E  a device ROW still selects on the PRESS   -> the safe prefix action
     (13.6) keeps it, and mode 2 is what tells the two species apart

**C is a CHANGE and not a mount**, and that is SPEC.md 52.6.1 rather than
vagueness: ticking the driver in mounts every drive it found, so by the time
this page is on screen the button says `Unmount` and firing it drops the
volumes. Which direction it goes is the disk's business; that the RELEASE and
only the release moves it is this test's.

C is the one that says the conversion did not eat the feature; E is the one
that says mode 2 works, and a build that had made the whole ladder
release-fired would pass A-D and fail it.

REQUIRES A DISK: os8088_xt_hdd, and the Hard Drive driver has to be ticked on
first (SPEC.md 51.3 - no row is wanted by default), which is tests/hddcp.py's
own opening sequence.

THE NET PAGE IS THE SECOND MACHINE, and it is worth its own run rather than
being taken on trust from this one: os88net's Connect is the other converted
page, and driving it caught a bug the hard disk's could not - net_draw_btn had
no OS88UI_FILL (correctly, until a release path started redrawing it over a
PRESSED button), so the upright redraw left the black interior and lettered a
black caption onto it. 147 lit pixels held, 0 after. It needs
os8088_5150_cga_lpt, the one machine here with a port at 0x378 - the driver
refuses to attach without one, which is the right answer and not the one you
want to be testing against.

    python3 tests/drvup.py net
"""
import sys
import time
import struct

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M
import dispcp                                          # noqa: E402
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

ARG = sys.argv[1] if len(sys.argv) > 1 else "os8088_xt_hdd"
NET = ARG == "net"
MACHINE = "os8088_5150_cga_lpt" if NET else ARG
IMG = "build/os8088-360.img"
NET_ROW = 4                        # drv_tab: os88net, last since the page
                                   # was reordered (SPEC.md 31.1)
NP_BX, NP_BY, NP_BW, NP_BH = 2, 66, 92, 16      # netui.inc: Connect

CP_I0Y, CP_IROWH = 6, 14           # ctrl.inc: the item list
CP_RX = 96                         # ...and the pane's left edge
CP_DBY1, CP_DROWH = 20, 26         # ...the Drivers page's hit bands
CP_IDRV = 2
TITLE_H = 18
HDP_BY, HDP_BH = 82, 16            # page.inc: the button row
HDP_B1X, HDP_BW1 = 146, 72         # ...Mount / Unmount, LAST on the row
                                   #    (SPEC.md 52.4)
HDP_R0Y, HDP_ROWH = 17, 11         # ...the device rows
HDP_EY = 63                        # ...and the C/H/S editor's row
DRVR_SZ, DRVR_SEG = 16, 2
HDD_ROW = 1

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def panel(m):
    want = m.sym("cp_ttl") - (M.KERNEL_SEG << 4)
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    for i in range(MAX_WIN):
        r = b[i * WIN_SIZE:(i + 1) * WIN_SIZE]
        if _u16(r, 0) & 2 and _u16(r, 10) == want:
            return tuple(_u16(r, o) for o in (2, 4, 6, 8))
    return None


def hdd_seg(m):
    return int.from_bytes(
        m.read(m.sym("drv_tab") + HDD_ROW * DRVR_SZ + DRVR_SEG, 2), "little")


DVOL_MAX, DV_SIZE, DV_KIND, DVK_FREE = 8, 16, 0, 0xFF


def nvol(m):
    """How many rows of dsk_vtab are live - a mount adds one (SPEC.md 18.7)."""
    b = m.read(m.sym("dsk_vtab"), DVOL_MAX * DV_SIZE)
    return sum(1 for i in range(DVOL_MAX)
               if b[i * DV_SIZE + DV_KIND] != DVK_FREE)


def quiet(m, s=12.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def band(m, mono, rect):
    """The rect's pixels, so two samples can be COMPARED rather than counted.

    A lit count cannot see the editor's highlight move: the frame is the same
    size on every field, so taking one off and putting another on is a net
    change of about zero. With the pointer parked, any differing pixel is the
    thing under test.
    """
    if mono:
        w, h, rows = m.vram()
        return bytes(rows[y][x] for y in range(rect[1], rect[3] + 1)
                     for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    return bytes(px[(y * w + x) * 3] for y in range(rect[1], rect[3] + 1)
                 for x in range(rect[0], rect[2] + 1))


def ndiff(a, b):
    return sum(1 for x, y in zip(a, b) if x != y)


def lit(m, mono, rect):
    if mono:
        w, h, rows = m.vram()
        return sum(rows[y][x] for y in range(rect[1], rect[3] + 1)
                   for x in range(rect[0], rect[2] + 1))
    w, h, px = m.fbuf()
    n = 0
    for y in range(rect[1], rect[3] + 1):
        for x in range(rect[0], rect[2] + 1):
            i = (y * w + x) * 3
            if px[i] or px[i + 1] or px[i + 2]:
                n += 1
    return n



def run_net(m, mo, mono):
    """os88net's Connect - the other converted page (SPEC.md 13.8.4)."""
    mo.menu(8, 8, 8, 40)
    M.settle(m)
    w = panel(m)
    check("the Control Panel opened", w is not None)
    if not w:
        sys.exit("no panel")
    cx, cy = w[0] + 1, w[1] + TITLE_H
    mo.click(cx + 8, cy + CP_I0Y + CP_IDRV * CP_IROWH + 6)
    quiet(m)
    # ROW 4 IS BELOW THE FOLD (SPEC.md 31.1.1) - scroll, then click.
    vis = dispcp.drv_show(mo, cx, cy, NET_ROW, lambda: quiet(m))
    mo.click(cx + CP_RX + 8, dispcp.drv_row_y(cy, vis))
    quiet(m, 25.0)
    seg = int.from_bytes(
        m.read(m.sym("drv_tab") + NET_ROW * DRVR_SZ + DRVR_SEG, 2), "little")
    check("the os88net driver loaded", seg != 0, f"(drv_tab seg {seg:#06x})")
    if not seg:
        sys.exit("no driver - is this a machine with a port at 0x378?")
    cp_nst = m.read(m.sym("cp_nst"), 1)[0]
    mo.click(cx + 8, cy + CP_I0Y + cp_nst * CP_IROWH + 6)
    quiet(m)

    r = (cx + CP_RX + NP_BX + 1, cy + NP_BY + 1,
         cx + CP_RX + NP_BX + NP_BW - 2, cy + NP_BY + NP_BH - 2)
    mid = ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
    mo.to(mid[0], r[1] - 30)
    time.sleep(0.8)
    up = lit(m, mono, r)
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.9)
    down = lit(m, mono, r)
    check("a press draws Connect DOWN", down * 2 < up,
          f"({up} lit upright, {down} held)")
    mo.to(w[0] + 20, mid[1], l=True)
    time.sleep(1.2)
    off = lit(m, mono, r)
    # THE CASE THAT FOUND THE BUG: without OS88UI_FILL this reads 0, not the
    # held value - the interior stays black and the caption is lettered black
    # onto it, so the button goes INVISIBLE rather than staying pressed.
    check("...and back UP when the pointer slides off it", off == up,
          f"({off} lit, upright is {up}, held was {down})")
    mo._edge(False)
    quiet(m)


with M.launch(IMG, apps="build/apps360.img", machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : a driver's page fires on the release (13.8.4) ==")
    if NET:
        run_net(m, mo, mono)
        print()
        if fails:
            print(f"{len(fails)} FAILED: {', '.join(fails)}")
            sys.exit(1)
        print("all pass")
        sys.exit(0)

    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    M.settle(m)
    w = panel(m)
    check("the Control Panel opened", w is not None)
    if not w:
        sys.exit("no panel")
    cx, cy = w[0] + 1, w[1] + TITLE_H

    # --- tick the Hard Drive row on the Drivers page ----------------------
    mo.click(cx + 8, cy + CP_I0Y + CP_IDRV * CP_IROWH + 6)
    quiet(m)
    mo.click(cx + CP_RX + 8, cy + CP_DBY1 + HDD_ROW * CP_DROWH + 8)
    quiet(m, 25.0)
    check("the Hard Drive driver loaded", hdd_seg(m) != 0,
          f"(drv_tab seg {hdd_seg(m):#06x})")
    if not hdd_seg(m):
        sys.exit("no driver")

    # its own page is the row after the static ones
    cp_nst = m.read(m.sym("cp_nst"), 1)[0]
    mo.click(cx + 8, cy + CP_I0Y + cp_nst * CP_IROWH + 6)
    quiet(m)

    mount = (cx + CP_RX + HDP_B1X, cy + HDP_BY,
             cx + CP_RX + HDP_B1X + HDP_BW1 - 1, cy + HDP_BY + HDP_BH - 1)
    mid = ((mount[0] + mount[2]) // 2, (mount[1] + mount[3]) // 2)
    was = nvol(m)                       # the tick above ALREADY mounted the
                                        # disk (SPEC.md 52.6.1), so this is
                                        # the count the release has to move
    print(f"  volumes mounted: {was}")

    # --- D: the press draws it DOWN ---------------------------------------
    inner = (mount[0] + 1, mount[1] + 1, mount[2] - 1, mount[3] - 1)
    mo.to(mid[0], mount[1] - 40)
    time.sleep(0.8)
    up = lit(m, mono, inner)
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.9)
    down = lit(m, mono, inner)
    check("a press draws the mount button DOWN", down * 2 < up,
          f"({up} lit upright, {down} held - the interior, so a black fill "
          f"more than halves it)")

    # --- B: back UP while still held, off the button ----------------------
    mo.to(w[0] + 20, mid[1], l=True)            # into the item list, HELD
    time.sleep(1.2)
    off = lit(m, mono, inner)
    check("...and back UP when the pointer slides off it", off == up,
          f"({off} lit, upright is {up})")

    # --- A: the release lands off it: NOTHING fires -----------------------
    mo._edge(False)
    quiet(m, 20.0)
    check("a slide-off release does NOT fire", nvol(m) == was,
          f"(dsk_nvol = {nvol(m)})")

    # --- C: press AND release on it: it mounts ----------------------------
    mo.to(*mid)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    quiet(m, 30.0)
    check("press-and-release ON it DOES fire", nvol(m) != was,
          f"(dsk_nvol = {nvol(m)}, was {was})")

    # --- E: a device ROW still selects on the PRESS -----------------------
    # Mode 2's whole reason: a row is a safe prefix action and keeps the press
    # (13.6), while a button on the same ladder only reports its id.
    # hd_sel and hd_field live in the DRIVER's segment, so the screen is the
    # instrument. An EDITOR FIELD rather than a device row, because this
    # machine has ONE disk and row 1 is not there to be selected - what is
    # being tested is that a SELECT still happens on the press, not which
    # kind. The highlight frame moves while the button is still down.
    # THE POINTER IS PARKED FIRST and only then sampled, so the arrow is in
    # the band on BOTH reads and cannot be mistaken for the highlight moving.
    # It is ~15 pixels and the first draft of this case measured exactly 16 -
    # the same trap tests/fmbtn.py sprang, and the reason that one now
    # measures a halving rather than a difference.
    ed = (cx + CP_RX, cy + HDP_EY - 2, cx + CP_RX + 200, cy + HDP_EY + 12)
    mo.to(cx + CP_RX + 100, cy + HDP_EY + 4)     # the second/third field
    time.sleep(1.0)
    before = band(m, mono, ed)
    mo._edge(True)
    time.sleep(1.0)
    during = band(m, mono, ed)
    check("a SELECT still happens on the PRESS (mode 2)",
          ndiff(before, during) > 40,
          f"({ndiff(before, during)} pixels of the editor row differ with the "
          f"press still down - a 13x11 highlight frame is ~44)")
    mo._edge(False)
    quiet(m)

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
