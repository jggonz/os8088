"""XTMP3's screenshots, for tools/guide/manuals/XTMP3_O88.md.

Run with the 360KB music disk, which carries XTMP3 at its root and AFTER
HOURS in MEDIA\\, on the 640KB VGA XT - no sound card and no hard disk, so
what is shown is what a 4.77 MHz XT does: it checks the track, finds that
nothing keeps up, and asks for a hard disk to decode to:

    python3 tools/guide/capture/run.py XTMP3_O88 --apps build/media360.img
"""
import os88marty

TITLE = "XTMP3"


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    # A double-click on the song opens XTMP3 with it. On this machine the
    # check finds no live quality, and Decode First comes up by itself.
    ui.path("B:/MEDIA/AFTERHRS.MP3")
    ui.wait_window(TITLE)
    ui.wait_window("Decode First", limit=240)
    os88marty.guest_sleep(ui.m, 2.0)
    shot("xtmp3-decode", window="Decode First", pad=4)

    ui.m.key("Escape")                  # the song stays loaded, READY
    os88marty.guest_sleep(ui.m, 3.0)
    ui.mo.to(636, 470)
    os88marty.guest_sleep(ui.m, 1.0)
    shot("xtmp3-window", window=TITLE, pad=4)

    _menu_shot(ui, shot, "Options", "xtmp3-options")
