#!/usr/bin/env python3
"""The hard-disk driver's Control Panel page, and the two windows behind it.

SPEC.md 51.3: no driver row is wanted by default, so the Hard Drive row has
to be TICKED once before its page exists at all (SPEC.md 31.9 - the item list
is the static rows plus one per loaded driver that publishes DSV_CPNAME).

Every coordinate here is the module's OWN constant rather than an estimate.
A first pass guessed the Drivers page's row pitch, clicked the same row
twice, and reported that the driver would not load.

  python3 hddcp.py <system.img> <out.bin>
"""
import sys, struct
sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse

IMG = sys.argv[1]
OUT = sys.argv[2]
MACHINE = "os8088_xt_hdd"
from os88geom import WIN_SIZE, MAX_WIN   # NOT a local copy: this one
                                        # moved 28 -> 30 with SPEC.md
                                        # 13.8.2's W_ONDRAG, and a stale
                                        # stride decodes window 1 as
                                        # garbage and reads it as unused

CP_I0Y, CP_IROWH = 6, 14           # ctrl.inc: the item list
CP_DIVX, CP_RX = 88, 96            # ...the divider, and the pane's left edge
CP_DBY1, CP_DROWH = 20, 26         # ...the Drivers page's hit bands
CP_IDRV = 2                        # SPEC.md 31.3
HDP_BY, HDP_BH = 82, 16            # page.inc: the Disks page's button row
HDP_B0X, HDP_BW0 = 2, 64           # ...Format, which OPENS the partition tool
HDP_B2X, HDP_BW2 = 154, 64         # ...Install, which opens the installer
DRVR_SZ, DRVR_SEG = 16, 2          # driver.inc: 0 = not loaded
HDD_ROW = 1                        # drv_tab row 1 is the hard disk


def wins(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [(i,) + tuple(struct.unpack_from("<H", b[i*WIN_SIZE:], o)[0]
                         for o in (2, 4, 6, 8))
            for i in range(MAX_WIN)
            if struct.unpack_from("<H", b[i*WIN_SIZE:], 0)[0] & 2]


def quiet(m, s=12.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


def hdd_seg(m):
    return int.from_bytes(
        m.read(m.sym("drv_tab") + HDD_ROW * DRVR_SZ + DRVR_SEG, 2), "little")


shots = []
with M.launch(IMG, apps="build/apps360.img", machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)

    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    quiet(m)
    cp = [w for w in wins(m) if w[3] >= 280 and w[4] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    cp = cp[0]
    x0, y0 = cp[1] + 1, cp[2] + 18
    print(f"  panel={cp}  hdd seg before = {hdd_seg(m):#06x}")

    def item(n):
        mo.click(x0 + 40, y0 + CP_I0Y + n * CP_IROWH + 7)
        quiet(m)

    def grab(w, tag):
        m.pause()
        W, H, px = m.fbuf()
        buf = bytes(px[(y * W + x) * 3]
                    for y in range(w[2], min(w[2] + w[4], H))
                    for x in range(w[1], min(w[1] + w[3], W)))
        shots.append((tag, w[3], w[4], buf))
        m.run()
        print(f"  {tag}: {w[3]}x{w[4]}")

    item(CP_IDRV)                               # the Drivers page
    grab(cp, "drivers")
    # ...and the hard-disk row's band. The bands start at CP_DBY1 and are one
    # CP_DROWH each; cp_drv_click divides rather than laddering, so the middle
    # of the band is the safe aim.
    mo.click(x0 + CP_RX + 40, y0 + CP_DBY1 + HDD_ROW * CP_DROWH + CP_DROWH // 2)
    quiet(m)
    seg = hdd_seg(m)
    print(f"  hdd seg after tick = {seg:#06x}")
    grab(cp, "drivers_ticked")
    if not seg:
        print("  !! the driver did not load - the Disks page will not exist")

    # The driver's page is appended after the static rows (SPEC.md 31.9), so
    # it is the row past the last one. cp_nst is the static count.
    nst = m.read(m.sym("cp_nst"), 1)[0]
    print(f"  cp_nst={nst}, so the driver's page is item row {nst}")
    item(nst)
    grab(cp, "disks")

    # --- and the two WINDOWS behind it -------------------------------------
    # Format and Install both just OPEN a window (hd_tool_open loads the tool
    # as a second image off the system volume); neither writes to the disk
    # until a button inside is clicked, and none is.
    def pane_click(px, py):
        mo.click(x0 + CP_RX + px, y0 + py)
        quiet(m, 20.0)                          # the tool is a floppy read

    def newwin(known):
        return next((w for w in wins(m) if w[0] not in known), None)

    for tag, bx, bw in (("tool", HDP_B0X, HDP_BW0),
                        ("installer", HDP_B2X, HDP_BW2)):
        known = {w[0] for w in wins(m)}
        pane_click(bx + bw // 2, HDP_BY + HDP_BH // 2)
        w = newwin(known)
        if w is None:
            print(f"  !! {tag} did not open")
            continue
        print(f"  {tag} window = {w}")
        grab(w, tag)
        mo.click(w[1] + 8, w[2] + 9)            # its close box
        quiet(m)
        # the Control Panel is behind now; one click RAISES it and is not
        # dispatched (SPEC.md 13: W_ONCLICK fires on the frontmost alone)
        mo.click(x0 + 40, y0 + 4)
        quiet(m)

with open(OUT, "wb") as f:
    f.write(struct.pack("<H", len(shots)))
    for tag, W, H, px in shots:
        t = tag.encode()
        f.write(struct.pack("<BHHI", len(t), W, H, len(px)) + t + px)
print(f"wrote {OUT}: {len(shots)} captures")
