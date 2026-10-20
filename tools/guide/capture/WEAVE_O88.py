"""Weave's screenshots, for tools/guide/manuals/WEAVE_O88.md.

    python3 tools/guide/capture/run.py WEAVE_O88 --apps build/weave360.img

Every click inside a card is aimed at the cell where tools/weavesim.py's
--render draws the same text, the way tests/weavesession.py aims its own.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty  # noqa: E402
import os88geom as geom  # noqa: E402

TITLE_H = 18


def card(wab):
    """The rendered card's rows, row k being card row k."""
    out = subprocess.run(["python3", "tools/weavesim.py", "--render",
                          "build/%s" % wab, "--adapter", "vga"],
                         cwd=ROOT, capture_output=True, text=True, check=True)
    return [ln[1:-1] for ln in out.stdout.splitlines()
            if ln.startswith("|") and ln.endswith("|")]


def named(ui, title):
    """The window titled exactly `title`: the Disk window showing the WEAVE
    folder is called WEAVE, which ui.window() cannot tell from Weave."""
    return [w for w in ui.windows() if w.title == title][0]


def wrect(ui, title, pad=4):
    x, y, w, h = ui._rect(named(ui, title).i)
    return (x - pad, y - pad, w + pad + 2, h + pad + 2)


def origin(ui, title="Weave"):
    w = named(ui, title)
    cx = 0 if w.x == 0 else ((w.x + 1 + 7) & ~7)
    return cx, w.y + TITLE_H


def find(rows, text):
    pat = re.compile(r"(?<![A-Za-z])" + re.escape(text) + r"(?![A-Za-z])")
    for r, ln in enumerate(rows):
        mm = pat.search(ln)
        if mm:
            return mm.start(), r
    raise RuntimeError("no %r on the card" % text)


def click_cell(ui, col, row, title="Weave"):
    cx, cy = origin(ui, title)
    ui.mo.click(cx + col * 8 + 4, cy + row * 8 + 4)
    os88marty.settle(ui.m)


def click_text(ui, rows, text):
    c, r = find(rows, text)
    click_cell(ui, c + len(text) // 2, r)


def hold(ui, menu, item):
    """Drop `menu` and park on `item` with the button still down."""
    cells = ui.menus()
    cell = [i for i, c in enumerate(cells) if c[0].upper() == menu.upper()][0]
    title, x0, x1, items = cells[cell]
    k = [i for i, (t, _) in enumerate(items) if t.upper().startswith(item.upper())][0]
    ui.mo.to((x0 + x1) // 2, geom.MBAR_H // 2)
    ui.mo._edge(True)
    ui._wait(lambda: ui._word("menu_y1") and ui._byte("menu_dropd"),
             "the menu to drop", 20.0)
    y1 = ui._word("menu_y1")
    ui.mo.to(ui._word("menu_x1") + 8,
             y1 + 1 + k * geom.MENU_ITEM_H + geom.MENU_ITEM_H // 2, l=True)
    ui._wait(lambda: ui._word("menu_sel") == k, "the item to highlight", 20.0)
    os88marty.settle(ui.m)


def release(ui):
    ui.mo._edge(False)
    os88marty.settle(ui.m)
    ui.settle()


def capture(ui, shot):
    m = ui.m
    # --- FORM: type a name, greet, shout, greet again
    rows = card("FORM.WAB")
    ui.path("B:/WEAVE/FORM.WAB")
    ui.settle()
    click_cell(ui, 60, 20)                 # an empty spot: focus the card
    m.key("Tab")                           # Tab arms the name field
    m.type_text("Ada")
    os88marty.settle(m)
    click_text(ui, rows, "Greet")
    click_text(ui, rows, "Greet")
    click_text(ui, rows, "Shout it")
    click_text(ui, rows, "Greet")
    ui.mo.to(400, 300)
    shot("weave-form", rect=(0, 0, 360, 160))

    # the Bundle menu, held open on Reload
    hold(ui, "Bundle", "Reload")
    shot("weave-menu", rect=(0, 0, 260, 110), settle=False)
    release(ui)
    ui.close(named(ui, "Weave"))

    # --- SHEET: change Bread's cost in the formula bar, then Show total
    rows = card("SHEET.WAB")
    ui.open("SHEET.WAB")
    ui.settle()
    c, r = find(rows, "Bread")
    click_cell(ui, 15, r)                  # cell B2
    click_cell(ui, 20, 1)                  # the formula bar: arm it
    m.key("End")
    for _ in range(6):
        m.key("Backspace")
    m.type_text("4.25")
    m.key("Enter")
    os88marty.settle(m)
    ui.settle()
    ui.mo.to(400, 300)
    shot("weave-sheet", rect=(0, 0, 360, 170))

    before = {w.i for w in ui.windows()}
    click_text(ui, rows, "Show total")
    ui._wait(lambda: {w.i for w in ui.windows()} - before, "the alert", 20.0)
    ui.settle()
    new = [w for w in ui.windows() if w.i not in before][0]
    x, y, ww, hh = ui._rect(new.i)
    shot("weave-alert", rect=(x - 8, y - 8, ww + 18, hh + 18))
    ui.mo.click(x + ww // 2, y + hh - 20)  # OK
    ui._wait(lambda: not ({w.i for w in ui.windows()} - before), "OK", 20.0)
    ui.close(named(ui, "Weave"))

    # --- PONG: serve and let the rally run a little
    rows = card("PONG.WAB")
    ui.open("PONG.WAB")
    ui.settle()
    c, r = find(rows, "Serve")
    cx, cy = origin(ui)
    ui.mo.click(cx + (c + 2) * 8 + 4, cy + r * 8 + 4, settle=0)  # the game
    m.advance(frames=45)                   # never goes still, so no settle
    m.run()
    shot("weave-pong", rect=(0, 0, 320, 200), settle=False)
    ui.close(named(ui, "Weave"))

    # --- WEAVE.O88 on its own: no bundle open
    ui.open("WEAVE.O88")
    ui.settle()
    shot("weave-empty", rect=(0, 0, 560, 90))
