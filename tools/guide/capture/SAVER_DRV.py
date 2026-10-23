"""The screen saver's and the RAM disk's screenshots, for SAVER_DRV.md and
RAMDISK_DRV.md.

    python3 tools/guide/capture/run.py SAVER_DRV

One boot on the VGA XT. The Theme page's Screen Saver button opens
SAVER.DRV's settings window; its Test button starts a session at once, so
each picture is shown by ticking it alone. Then the RAM disk is ticked on the
Drivers page, mounted from its own page, and given a file.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tests"))

# kernel/ctrl.inc's geometry, content-relative (tests/dispcp.py, tests/rdcz.py)
CP_IX, CP_I0Y, CP_IROWH, CP_RX = 6, 6, 14, 96
CP_ITHM = 5                             # the Theme page's record in cp_items
CPH_BTX, CPH_BTY = 112, 110             # the Theme page's Screen Saver button
RP_MNTX, RP_MNTW, RP_B0Y, RP_BH = 2, 64, 52, 16     # the Ram Disk page's Mount
# drivers/saver/svcfg.inc, content-relative
SC_GX, SC_R0Y, SC_ROWH, SC_BY, SC_TESTX, SC_OKX = 4, 2, 16, 112, 4, 176


def _cp(ui):
    cp = ui.window("Control Panel")
    return cp.x + 1, cp.y + 19


def _away(ui):
    ui.mo.to(630, 300)
    os88marty.guest_sleep(ui.m, 1)


def _box(ui, cx, cy, i):
    ui.mo.click(cx + SC_GX + 6, cy + SC_R0Y + i * SC_ROWH + 6)
    os88marty.guest_sleep(ui.m, 1)


def _session(ui, shot, cx, cy, sid, wait=10):
    m = ui.m
    ui.mo.click(cx + SC_TESTX + 30, cy + SC_BY + 8)     # Test
    os88marty.guest_sleep(m, wait)
    shot(sid, settle=False)
    ui.mo.to(300, 300)                                  # the mouse ends it
    os88marty.guest_sleep(m, 2)
    ui.mo.to(320, 320)
    os88marty.guest_sleep(m, 6)
    ui.settle()


def capture(ui, shot):
    import dispcp
    m, mo = ui.m, ui.mo

    # The harness turned the saver off at boot; put back the shipped five
    # minutes so the window shows what a fresh machine has.
    m.write(ui.sym("ss_mins"), b"\x05")
    dispcp.open_panel(m, mo, ui._S, ui.settle, page=CP_ITHM)
    x0, y0 = _cp(ui)
    _away(ui)
    shot("saver-drv-theme", window="Control Panel", pad=8)
    mo.click(x0 + CP_RX + CPH_BTX + 52, y0 + CPH_BTY + 8)
    os88marty.guest_sleep(m, 8)
    ui.settle()
    sw = ui.window("Screen Saver")
    cx, cy = sw.x + 1, sw.y + 19

    _away(ui)
    shot("saver-drv-settings", window="Screen Saver", pad=8)

    # Each picture alone, with Test: cube, shapes, sea life, starfield.
    for i in (1, 2, 3):
        _box(ui, cx, cy, i)
    _session(ui, shot, cx, cy, "saver-drv-cube")
    _box(ui, cx, cy, 0)
    _box(ui, cx, cy, 2)
    _session(ui, shot, cx, cy, "saver-drv-shapes", wait=14)
    _box(ui, cx, cy, 2)
    _box(ui, cx, cy, 3)
    _session(ui, shot, cx, cy, "saver-drv-fish", wait=14)
    _box(ui, cx, cy, 3)
    _box(ui, cx, cy, 1)
    _session(ui, shot, cx, cy, "saver-drv-stars")
    for i in (0, 2, 3):                                 # all four again
        _box(ui, cx, cy, i)
    os88marty.no_saver(m)
    mo.click(cx + SC_OKX + 30, cy + SC_BY + 8)          # Ok
    os88marty.guest_sleep(m, 3)
    ui.settle()
    os88marty.no_saver(m)

    # The RAM disk: tick its row (the fourth) on the Drivers page, then its
    # own page - the first driver page on a machine with no hard disk.
    dispcp.open_panel(m, mo, ui._S, ui.settle, page=2)
    mo.click(x0 + CP_RX + 8, dispcp.drv_row_y(y0, 3))
    os88marty.guest_sleep(m, 10)
    ui.settle()
    nst = m.read(ui.sym("cp_nst"), 1)[0]
    mo.click(x0 + CP_IX + 30, y0 + CP_I0Y + nst * CP_IROWH + CP_IROWH // 2)
    os88marty.guest_sleep(m, 8)
    ui.settle()
    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    os88marty.guest_sleep(m, 8)
    ui.settle()
    ui.close(ui.window("Control Panel"))
    os88marty.no_saver(m)

    # Copy README.TXT from the system disk onto it.
    src = ui.open_drive("A")
    i, _ = ui.entry("README.TXT", src)
    row = ui.scroll_to(i, win=src)
    mo.click(*ui.row_xy(src, row))
    ui.settle()
    ui.menu_pick("Edit", "Copy")
    dst = None
    for letter in ("D", "C"):
        try:
            dst = ui.open_drive(letter)
            break
        except Exception as e:
            print("drive", letter, e)
    ui.menu_pick("Edit", "Paste")
    os88marty.guest_sleep(m, 10)
    ui.settle()
    _away(ui)
    # Every Disk window is titled "Disk", so crop to this one by its rect.
    x, y, ww, hh = ui._rect(dst.i)
    shot("ramdisk-drv-window", rect=(x - 8, y - 8, ww + 18, hh + 18))
    shot("ramdisk-drv-desktop")
