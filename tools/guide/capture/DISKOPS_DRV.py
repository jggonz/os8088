"""Screenshots for the floppy side of the system files: DISKOPS_DRV.md,
FORMAT_DRV.md, KERNEL_SYS.md and ASSOC_DAT.md.

    python3 tools/guide/capture/run.py DISKOPS_DRV

Nothing is formatted or cloned: each prompt is shown and answered with Esc.
The one change made is a Compress of README.TXT on the system disk, which is
the instance's private copy of the image.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from HDDTOOL_DRV import _hold_menu, _wait  # noqa: E402


def capture(ui, shot):
    ui.open_drive("A")
    ui.mo.to(600, 420)
    shot("kernel-sys-hidden", window="Disk", pad=8)

    # --- the Disk menu --------------------------------------------------------
    _hold_menu(ui, shot, "Disk", "Format Disk...", "format-drv-menu",
               (200, 0, 200, 110))
    _hold_menu(ui, shot, "Disk", "Clone Disk...", "diskops-drv-menu",
               (200, 0, 200, 110))

    # --- Format Disk..., answered No ----------------------------------------
    ui.menu_pick("Disk", "Format Disk...")
    _wait(ui, 8)
    ui.mo.to(600, 420)
    shot("format-drv-prompt", window="Disk", pad=8)
    ui.m.key("Escape")
    _wait(ui, 4)

    # --- Clone Disk..., to the other drive and then to a file, answered No --
    ui.menu_pick("Disk", "Clone Disk...")
    _wait(ui, 8)
    ui.mo.to(600, 420)
    shot("diskops-drv-clone", window="Disk", pad=8)
    ui.m.key("Space")
    _wait(ui, 3)
    shot("diskops-drv-image", window="Disk", pad=8)
    ui.m.key("Escape")
    _wait(ui, 4)

    # --- Compress README.TXT -------------------------------------------------
    w = ui.window("Disk")
    names = [n for n, _ in ui.listing(w)]
    x, y = ui.row_xy(w, names.index("README.TXT"))
    ui.mo.click(x, y)
    _wait(ui, 2)
    ui.menu_pick("File", "Compress")
    ui.mo.to(600, 420)
    ui.wait_toast(says="Compressed", limit=180)
    shot("diskops-drv-compress", rect=(96, 0, 544, 280), settle=False)

    # --- documents in B:\MEDIA wear their programs' marks --------------------
    ui.path("B:/MEDIA")
    m = ui.window("MEDIA")
    ui.mo.click(m.x + 219, m.y + 26)        # the Icons button
    _wait(ui, 4)
    ui.mo.to(600, 420)
    shot("assoc-dat-icons", window="MEDIA", pad=8)
