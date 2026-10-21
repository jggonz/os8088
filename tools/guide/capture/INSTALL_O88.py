"""Install's screenshots, for tools/guide/manuals/INSTALL_O88.md.

Needs a machine with a hard disk:

    python3 tools/guide/capture/run.py INSTALL_O88 --machine os8088_xt_vga_hdd

The disk is MartyPC's bundled 31MB XT-IDE image (a DOS partition), copied
into the instance's own directory. The script walks the wizard to Step 3 and
cancels: nothing is installed and nothing is written.
"""
import os88geom as geom
import os88marty


def _next(ui, w, n=1):
    """Click button `n` of the wizard (0 Back, 1 Next, 2 Cancel)."""
    x0, y0 = w.x + 1, w.y + geom.TITLE_H
    ui.mo.click(x0 + 104 + n * 80 + 36, y0 + 114 + 8)
    os88marty.guest_sleep(ui.m, 4)


def capture(ui, shot):
    w = ui.path("A:/INSTALL.O88")
    shot("install-welcome", window="Install os8088", pad=8)
    _next(ui, w)                        # the hard disk
    os88marty.guest_sleep(ui.m, 8)
    _next(ui, w)                        # where to put it
    os88marty.guest_sleep(ui.m, 4)
    shot("install-slot", window="Install os8088", pad=8)
    _next(ui, w)                        # keep or erase
    shot("install-mode", window="Install os8088", pad=8)
    _next(ui, w, 2)                     # Cancel
