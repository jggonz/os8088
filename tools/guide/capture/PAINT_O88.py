"""Paint's screenshots, for tools/guide/manuals/PAINT_O88.md."""

import os88marty


def _geo(ui):
    """Paint's content origin, the canvas size and the strip's top."""
    w = ui.window("Paint")
    x, y, ww, hh = ui._rect(w.i)
    ox, oy = x + 1, y + 18
    contw, conth = ww - 2, hh - 19
    stripy = conth - 22
    return ox, oy, contw, conth, stripy


def _tool(ui, k):
    ox, oy, _, _, _ = _geo(ui)
    tx = 1 if k % 2 == 0 else 22
    ty = 1 + 21 * (k // 2)
    ui.mo.click(ox + tx + 10, oy + ty + 10)


def _colour(ui, c):
    ox, oy, contw, _, stripy = _geo(ui)
    pitch = (contw - 15 - 16 - 18 - 120) // 16
    pitch = max(11, min(21, pitch))
    ui.mo.click(ox + 116 + c * pitch + (pitch - 1) // 2, oy + stripy + 11)


def _width(ui, k):
    ox, oy, _, _, stripy = _geo(ui)
    ui.mo.click(ox + 2 + 21 * k + 10, oy + stripy + 11)


def _filled(ui):
    ox, oy, contw, _, stripy = _geo(ui)
    filx = contw - 15 - 16 - 18
    ui.mo.click(ox + filx + 8, oy + stripy + 11)


def _cv(ui, cx, cy):
    ox, oy, _, _, _ = _geo(ui)
    return ox + 48 + cx, oy + cy


def _drag(ui, a, b):
    x0, y0 = _cv(ui, *a)
    x1, y1 = _cv(ui, *b)
    ui.mo.drag(x0, y0, x1, y1)


def _stroke(ui, pts):
    mo = ui.mo
    x, y = _cv(ui, *pts[0])
    mo.to(x, y)
    mo._sep()
    mo._edge(True)
    for p in pts[1:]:
        x, y = _cv(ui, *p)
        mo.to(x, y, l=True)
    mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.0)


def _away(ui):
    """Park the pointer on the desktop, clear of the picture."""
    w = ui.window("Paint")
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.to(min(630, x + ww + 20), 300)


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
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6))
    ui.mo.to(600, 300, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    ui.path("B:/APPS/PAINT.O88")
    ui.settle()

    # a filled light-blue box
    _tool(ui, 3)
    _filled(ui)
    _colour(ui, 9)
    _drag(ui, (30, 30), (150, 110))
    # a red oval outline, three pixels thick
    _filled(ui)
    _tool(ui, 4)
    _colour(ui, 4)
    _width(ui, 2)
    _drag(ui, (180, 30), (330, 140))
    # fill the oval yellow
    _tool(ui, 6)
    _colour(ui, 14)
    _cv_c = (255, 85)
    x, y = _cv(ui, *_cv_c)
    ui.mo.click(x, y)
    # a green freehand curve
    _tool(ui, 0)
    _colour(ui, 2)
    _width(ui, 2)
    _stroke(ui, [(30, 200), (60, 170), (95, 160), (130, 185), (160, 215),
                 (195, 225), (230, 205), (260, 175), (300, 165), (340, 185),
                 (370, 210)])
    # some words
    _tool(ui, 7)
    _colour(ui, 0)
    x, y = _cv(ui, 40, 125)
    ui.mo.click(x, y)
    ui.m.type_text("Hello from Paint")
    ui.m.key("Escape")
    _away(ui)
    ui.settle()
    shot("paint-window", window="Paint")

    # a selection round the box
    _tool(ui, 5)
    _drag(ui, (20, 20), (160, 118))
    _away(ui)
    ui.settle()
    shot("paint-select", window="Paint")
    ui.m.key("Escape")

    _menu_shot(ui, shot, "Draw", "paint-menu-draw")

    # the picture full screen, then back
    ui.menu_pick("View", "Full Screen")
    os88marty.guest_sleep(ui.m, 3.0)
    shot("paint-full")
    ui.m.type_text("f")
    os88marty.guest_sleep(ui.m, 3.0)

    # save the drawing as a GIF
    ui.menu_pick("File", "Save as Gif")
    ch = ui.chooser()
    ui.settle()
    cx, cy, cw, chh = ui._rect(ch.i)
    shot("paint-save", rect=(cx - 6, cy - 6, cw + 14, chh + 14))
    ui.chooser_save("DRAWING.GIF")
    os88marty.guest_sleep(ui.m, 3.0)
