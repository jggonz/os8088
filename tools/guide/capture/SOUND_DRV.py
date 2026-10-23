"""The hardware drivers' screenshots, for tools/guide/manuals/*_DRV.md.

    python3 tools/guide/capture/run.py SOUND_DRV --machine os8088_xt_vga_hdd_sb

One boot on the VGA XT with a hard disk and a Sound Blaster, so the first
boot's own hardware check has already loaded SOUND.DRV and HDD.DRV. It takes
the Control Panel's Drivers page, its Ports view, the rows of drivers whose
hardware this machine lacks, and the Sound, Hard Drive and Ram Disk pages.
SAVER_DRV.py takes the screen saver and the RAM disk's Disk window.
MartyPC has no network card, so ETHER.DRV's own page cannot be reached here.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tests"))

# kernel/ctrl.inc's geometry, content-relative (tests/dispcp.py, tests/rdcz.py)
CP_IX, CP_I0Y, CP_IROWH, CP_RX = 6, 6, 14, 96
CP_ISND = 4                 # records in cp_items
CP_LARX, CP_LARY = 52, 118              # the item list's down arrow
CPD_PBX1, CPD_PBY1 = 148, 2             # the Drivers page's Ports button
RP_MNTX, RP_MNTW, RP_B0Y, RP_BH = 2, 64, 52, 16     # the Ram Disk page's Mount


def _cp(ui):
    cp = ui.window("Control Panel")
    return cp.x + 1, cp.y + 19


def _away(ui):
    ui.mo.to(630, 300)
    os88marty.guest_sleep(ui.m, 1)


def _page(ui, ordinal):
    """Select the item list's ORDINAL, scrolling the list to reach it."""
    x0, y0 = _cp(ui)
    for _ in range(3):
        top = ui.m.read(ui.sym("cp_ltop"), 1)[0]
        if ordinal - top < 8:
            break
        ui.mo.click(x0 + CP_LARX + 7, y0 + CP_LARY + 7)
        os88marty.guest_sleep(ui.m, 2)
    top = ui.m.read(ui.sym("cp_ltop"), 1)[0]
    row = ordinal - top
    ui.mo.click(x0 + CP_IX + 30, y0 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    os88marty.guest_sleep(ui.m, 4)
    ui.settle()


def _sound_state(ui):
    m = ui.m
    svc = ui.sym("drv_svc")
    rd = lambda o: int.from_bytes(m.read(svc + o, 2), "little")
    print("snd_route", m.read(ui.sym("snd_route"), 1)[0],
          "CAPS %04x STREAM %04x TONE %04x TIERS %04x"
          % (rd(0), rd(4), rd(12), rd(14)))


def capture(ui, shot):
    import dispcp
    m, mo = ui.m, ui.mo
    _sound_state(ui)
    dispcp.open_panel(m, mo, ui._S, ui.settle, page=2)
    x0, y0 = _cp(ui)
    _away(ui)
    shot("sound-drv-drivers", window="Control Panel", pad=8)

    # The Ports view: where the network card and the MIDI port are looked for.
    mo.click(x0 + CP_RX + CPD_PBX1 + 22, y0 + CPD_PBY1 + 8)
    _away(ui)
    shot("ether-drv-ports", window="Control Panel", pad=8)
    mo.click(x0 + CP_RX + CPD_PBX1 + 22, y0 + CPD_PBY1 + 8)
    _away(ui)

    # Tick Ethernet (row 2) and Ram Disk (row 3).
    mo.click(x0 + CP_RX + 8, dispcp.drv_row_y(y0, 2))
    os88marty.guest_sleep(m, 14)
    mo.click(x0 + CP_RX + 8, dispcp.drv_row_y(y0, 3))
    os88marty.guest_sleep(m, 10)
    _away(ui)
    shot("ether-drv-row", window="Control Panel", pad=8)

    # The last three rows: os88net, USB Mouse, EMS - none of their hardware
    # is in this machine.
    top = 3
    dispcp.drv_show(mo, x0, y0, 6, settle=ui.settle)
    for row in (4, 5, 6):
        mo.click(x0 + CP_RX + 8, dispcp.drv_row_y(y0, row - top))
        os88marty.guest_sleep(m, 10)
    _away(ui)
    shot("usbmouse-drv-rows", window="Control Panel", pad=8)

    # The Sound page.
    dispcp.open_panel(m, mo, ui._S, ui.settle, page=CP_ISND)
    _away(ui)
    _sound_state(ui)
    shot("sound-drv-page", window="Control Panel", pad=8)

    # The driver pages follow the static ones: Hard Drive, then Ram Disk.
    nst = m.read(ui.sym("cp_nst"), 1)[0]
    _page(ui, nst)
    _away(ui)
    shot("hdd-drv-page", window="Control Panel", pad=8)
    _page(ui, nst + 1)
    os88marty.guest_sleep(m, 6)
    _away(ui)
    shot("ramdisk-drv-page", window="Control Panel", pad=8)
    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    os88marty.guest_sleep(m, 8)
    _away(ui)
    shot("ramdisk-drv-mounted", window="Control Panel", pad=8)
