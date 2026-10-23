"""Frotz's screenshots, for tools/guide/manuals/FROTZ_O88.md.

FROTZ.O88 is on the everything set and the live media, not on the 360KB
apps disk, and no story file is committed to the tree:

    python3 tools/guide/capture/run.py FROTZ_O88 --machine os8088_xt_vga_144 \
        --image build/os8088.img --apps build/apps-all-1.img
"""

import os88marty


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cell = [c for c in ui.menus() if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6))
    ui.mo.to(620, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    ui.path("B:/APPS/FROTZ.O88")
    ui.mo.to(620, 460)
    ui.settle()
    shot("frotz-window", window="Frotz", pad=8)

    _menu_shot(ui, shot, "File", "frotz-file-menu")

    # File > Open Story... is the Standard File dialog.
    ui.menu_pick("File", "Open Story...")
    cw = ui.chooser()
    x, y, ww, hh = ui._rect(cw.i)
    shot("frotz-open", rect=(x - 8, y - 8, ww + 18, hh + 18))

    # A file that is not a story is refused, with the reason in the window.
    ui.chooser_open("../MEDIA/GUIDE.TEX")
    ui.mo.to(620, 460)
    ui.settle()
    w = ui.window("Frotz")
    x, y, ww, hh = ui._rect(w.i)
    shot("frotz-notstory", rect=(x - 8, y - 8, ww + 18, 60))

    ui.menu_pick("Frotz", "About Frotz")
    ui.mo.to(620, 460)
    ui.settle()
    shot("frotz-about", rect=(x - 8, y - 8, ww + 18, 140))
