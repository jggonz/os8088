"""PiXEL's sample pictures, one shot each, for the picture pages
(VACATION_JPG.md, FLOWER_JPG.md, ROOM_JPG.md, LAKE_JPG.md, CAT_GIF.md,
BALLOONS_PNG.md, HOUSE_PNG.md, MOUNTAIN_BMP.md, CITY_PCX.md, BOUNCE_GIF.md).

The 1.44MB PiXEL disk carries all ten, so this boots the 1.44MB pair:

    python3 tools/guide/capture/run.py VACATION_JPG --machine os8088_xt_vga_144 \\
        --image build/os8088.img --apps build/pixel.img

It double-clicks CAT.GIF first, to see which program a GIF on this disk
opens in, then steps through the folder with PiXEL's Next (Space).
"""

import os88marty


def _pix(ui):
    for w in ui.windows():
        if w.title.upper().startswith("PIXEL"):
            return w
    return None


def _name(w):
    t = w.title
    return t.split("-", 1)[1].strip() if "-" in t else ""


def _wait_title(ui, old, limit=300.0):
    spent = 0.0
    while spent < limit:
        os88marty.guest_sleep(ui.m, 2.0)
        spent += 2.0
        w = _pix(ui)
        if w is not None and _name(w) and _name(w) != old:
            return w
    return _pix(ui)


def _decoded(ui, w):
    """True once the Navigator's thumbnail holds a picture: a progressive
    JPEG shows nothing at all for a long while, so a quiet screen is not
    yet a finished one."""
    x, y, ww, hh = ui._rect(w.i)
    fw, fh, data = ui.m.fbuf()
    seen = set()
    for py in range(y + 80, y + 150, 7):
        for px in range(x + ww - 175, x + ww - 50, 9):
            o = (py * fw + px) * 3
            seen.add(bytes(data[o:o + 3]))
    return len(seen) > 4


def _take(ui, shot, w):
    ui.mo.to(630, 470)
    os88marty.guest_sleep(ui.m, 15.0)
    for _ in range(60):
        if _decoded(ui, _pix(ui)):
            break
        os88marty.guest_sleep(ui.m, 10.0)
    ui.settle(limit=400.0)
    w = _pix(ui)
    name = _name(w)
    sid = name.lower().replace(".", "-")
    print("shot", sid, "title", w.title)
    shot(sid, window=w.title)
    return name


def capture(ui, shot):
    w = ui.path("B:/PICTURES/CAT.GIF")
    os88marty.guest_sleep(ui.m, 20.0)
    print("CAT.GIF double-click opened:", ui.titles())
    if _pix(ui) is None:
        ui.mo.to(630, 470)
        ui.settle(limit=200.0)
        shot("cat-gif-dblclick")
        ui.close(ui.front())
        os88marty.guest_sleep(ui.m, 3.0)
        ui.path("B:/PICTURES/BALLOONS.PNG")
        os88marty.guest_sleep(ui.m, 20.0)
    seen = []
    name = _take(ui, shot, _pix(ui))
    seen.append(name)
    for _ in range(12):
        ui.raise_window(_pix(ui))
        ui.mo.to(630, 470)
        ui.m.key("Space")
        w = _wait_title(ui, name)
        if w is None or _name(w) in seen:
            break
        name = _take(ui, shot, w)
        seen.append(name)
    print("pictures shot:", seen)
