"""The extended desktop's screenshot, for tools/guide/manuals/EXTD_DRV.md.

    python3 tools/guide/capture/run.py EXTD_DRV --machine os8088_xt_vga_herc

A VGA XT with a Hercules card beside it. The Control Panel's Display page
turns Extend on (Right), a Disk window is dragged onto the second monitor,
and both screens are written side by side into one picture.
"""
import os
import sys

import os88marty

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "tests"))


def _field(ui, card):
    """(w, h, rgb) of a card's picture: aperture 0 cut out of the whole
    field, the way run.py's shot() does it for a VGA."""
    v = ui.m.video(card)
    aps = v.get("apertures") or []
    if not aps:
        return ui.m.fbuf(card=card)
    full = max(range(len(aps)), key=lambda i: aps[i]["w"] * aps[i]["h"])
    w, h, data = ui.m.fbuf(full, card=card)
    a0 = aps[0]
    x0, y0 = a0["x"] - aps[full]["x"], a0["y"] - aps[full]["y"]
    cw = min(a0["w"], w - x0)
    ch = min(480 if v.get("type") == "vga" else a0["h"], h - y0)
    rows = [data[((y0 + y) * w + x0) * 3:((y0 + y) * w + x0 + cw) * 3]
            for y in range(ch)]
    return cw, ch, b"".join(rows)


def _both(ui, sid):
    """Both monitors side by side, as they stand on the desk: the VGA on the
    left, the Hercules to its right."""
    import __main__
    cards = ui.m.cards()
    pri = [c["idx"] for c in cards if c["type"] == "vga"][0]
    sec = [c["idx"] for c in cards if c["type"] == "mda"][0]
    w0, h0, d0 = _field(ui, pri)
    w1, h1, d1 = _field(ui, sec)
    gap = 16
    W, H = w0 + gap + w1, max(h0, h1)
    grey = b"\x30\x30\x30"
    rows = []
    for y in range(H):
        a = d0[y * w0 * 3:(y + 1) * w0 * 3] if y < h0 else grey * w0
        b = d1[y * w1 * 3:(y + 1) * w1 * 3] if y < h1 else grey * w1
        rows.append(a + grey * gap + b)
    out = os.path.join(__main__.OUT, sid + ".png")
    __main__.png(out, W, H, b"".join(rows))
    print("shot %s %dx%d" % (sid, W, H))


def capture(ui, shot):
    import dispcp
    m = ui.m
    ui.open_drive("B")
    os88marty.guest_sleep(m, 2.0)
    dispcp.open_panel(m, ui.mo, ui._S, ui.settle, page=dispcp.CP_IVID)
    dispcp.set_mode(m, ui.mo, ui._S, os88marty.settle, "right")
    os88marty.guest_sleep(m, 3.0)
    try:
        ui.move_window(ui.disk_window(), 760, 60)
    except Exception as e:                  # the picture is still worth having
        print("move:", e)
    os88marty.guest_sleep(m, 3.0)
    ui.mo.to(400, 300)
    os88marty.guest_sleep(m, 2.0)
    ui.settle()
    _both(ui, "extd-drv-desktop")
