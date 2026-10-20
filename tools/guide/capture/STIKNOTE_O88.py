"""Sticky Note's screenshots, for tools/guide/manuals/STIKNOTE_O88.md.

    python3 tools/guide/capture/run.py STIKNOTE_O88

The pad off the system disk's STIKAPPS folder, reading the welcome note the
disk ships with. `+` makes a second note and opens it in Note Pad, where a
checklist is typed and saved; the pad shows it on a pink sheet once Note Pad
closes. Every press on the pad is placed from tools/os88stik.py's
STIKNOTE_GEO, the pad's own geometry table.
"""
import struct

import os88marty
import os88stik as S

TITLE = "Sticky Note"
LIST = ("Weekend\n"
        "[ ] Bread and milk\n"
        "[ ] Floppy disks\n"
        "[x] Printer ribbon\n"
        "[ ] Call the shop\n")


def _pad(ui):
    return ui.window(TITLE)


def _geo(ui):
    return S.STIKNOTE_GEO[ui._wfield(_pad(ui), "stik_li")]


def _at(ui, x, y):
    w = _pad(ui)
    return w.x + 1 + x, w.y + y


def _press(ui, pt, wait=2.0):
    ui.mo.click(pt[0], pt[1], settle=0)
    os88marty.guest_sleep(ui.m, wait)


def _box(ui, line):
    g = _geo(ui)
    return _at(ui, S.STIKNOTE_BX + 4, g["B0"] + g["P"] * line + 3)


def _rest(ui):
    ui.mo.to(60, 300)
    os88marty.guest_sleep(ui.m, 1.5)


def _editor(ui, before):
    new = [o for o in ui.windows() if o.i not in before and o.visible]
    return new[0] if new else None


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    _, x0, x1, _items = [c for c in cells if c[0].upper() == title.upper()][0]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(200, 300, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    m = ui.m
    m.write(ui._S("clk_day"), bytes([14, 10]))         # Wed 14 Oct 2026,
    m.write(ui._S("clk_year"), struct.pack("<H", 2026))
    m.write(ui._S("clk_sec"), bytes([0, 20, 9]))       # 09:20:00
    w = ui.path("A:/STIKAPPS/STIKNOTE.O88")
    for o in list(ui.windows()):
        if o.i != w.i and o.visible:
            ui.close(o)
    os88marty.guest_sleep(m, 3.0)
    _rest(ui)
    shot("stiknote-welcome", window=TITLE, pad=8)

    # `+`: a new note, opened in Note Pad. The checklist typed over its
    # title line, saved, and Note Pad closed: the pad reads it again.
    g = _geo(ui)
    before = set(o.i for o in ui.windows())
    _press(ui, _at(ui, S.STIKNOTE_PLUSX + 2, g["HY"] + 3), 1.0)
    ui._wait(lambda: _editor(ui, before) is not None, "Note Pad to open",
             60, snapshot=ui.titles)
    os88marty.guest_sleep(m, 2.0)
    m.ctrl("KeyA")
    os88marty.pace(m, 0.5)
    m.key("Backspace")
    os88marty.pace(m, 0.5)
    m.type_text(LIST)
    os88marty.guest_sleep(m, 1.0)
    m.ctrl("KeyS")
    ui.wait_toast(says="Saved")
    os88marty.guest_sleep(m, 1.0)
    ui.close(_editor(ui, before))
    os88marty.guest_sleep(m, 6.0)

    # A pink paper, and the first box ticked on the pad itself.
    ui.widget_activate(_pad(ui))
    ui.menu_pick("Color", "  Pink")
    os88marty.guest_sleep(m, 2.0)
    _press(ui, _box(ui, 0), 3.0)
    _rest(ui)
    shot("stiknote-checklist", window=TITLE, pad=8)

    # The Note and Color menus, while the pad owns the bar.
    ui.widget_activate(_pad(ui))
    _menu_shot(ui, shot, "Note", "stiknote-menu")
    ui.widget_activate(_pad(ui))
    _menu_shot(ui, shot, "Color", "stiknote-color")

    # The open arrow: the note in Note Pad, beside the pad - the pad dragged
    # to the left edge first, so the editor can stand clear of it.
    ui.widget_drag(_pad(ui), -160, 0)
    os88marty.guest_sleep(m, 2.0)
    g = _geo(ui)
    before = set(o.i for o in ui.windows())
    _press(ui, _at(ui, S.STIKNOTE_OPENX + 6, g["OY"] + 4), 1.0)
    ui._wait(lambda: _editor(ui, before) is not None, "Note Pad to open",
             60, snapshot=ui.titles)
    os88marty.guest_sleep(m, 3.0)
    ed = _editor(ui, before)
    pw = _pad(ui)
    try:                                # as far right as the display allows
        ui.move_window(ed, pw.x + pw.w + 8, 40)
    except Exception:
        pass
    os88marty.guest_sleep(m, 2.0)
    ui.mo.to(620, 300)
    os88marty.guest_sleep(m, 1.5)
    shot("stiknote-desktop")
    ui.close(_editor(ui, before))
    os88marty.guest_sleep(m, 4.0)

    # Back to the welcome note.
    ui.widget_activate(_pad(ui))
    m.key("ArrowLeft")
    os88marty.guest_sleep(m, 2.0)

    # The name tag turns into a handle under the pointer.
    w = ui.widget_hover(_pad(ui), True)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.to(x + 100, y + hh - 6)
    os88marty.guest_sleep(m, 1.0)
    shot("stiknote-handle", rect=(x - 8, y + hh - 70, ww + 18, 80))
    _rest(ui)
