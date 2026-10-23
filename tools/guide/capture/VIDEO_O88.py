"""Video Player's screenshots, for tools/guide/manuals/VIDEO_O88.md.

    python3 tools/guide/capture/run.py VIDEO_O88 --machine os8088_xt_vga_144 \
        --image build/os8088.img --apps build/apps.img

OS8088.V88 rides the 1.44MB apps disk's MEDIA folder, not the 360KB one's.
"""

import os88marty

T = "Video Player"


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def _park(ui):
    w = ui.window(T)
    ui.mo.to(min(630, w.x + w.w + 10), 460)


def capture(ui, shot):
    ui.path("B:/MEDIA/OS8088.V88")
    os88marty.guest_sleep(ui.m, 3.0)
    _park(ui)
    ui.settle(limit=300.0)            # the poster is drawn

    # Space: the logo is a Live file, so it plays on the desktop
    # (the play first reads the whole video into memory: seconds off a floppy)
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 15.0)
    shot("video-window", window=T, pad=6, settle=False)

    # pause, then the info card
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 2.0)
    ui.m.type_text("i")
    os88marty.guest_sleep(ui.m, 4.0)
    _park(ui)
    ui.settle(limit=300.0)
    shot("video-info", window=T, pad=6)

    _menu_shot(ui, shot, "File", "video-file-menu")

    # F: the full screen, paused; Space plays it there
    ui.m.type_text("f")
    os88marty.guest_sleep(ui.m, 4.0)
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 3.0)
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 1.5)
    shot("video-fullscreen-paused", settle=False)
    ui.m.type_text("f")
    os88marty.guest_sleep(ui.m, 4.0)
