"""Paint's pictures and the TeXPad guide, for SAMPLE_BMP.md, OS8088_GIF.md
and GUIDE_TEX.md. The logo is on the system disk's MEDIA folder, the Paint
sample and the guide on the Office disk's:

    python3 tools/guide/capture/run.py SAMPLE_BMP --apps build/office360.img
"""

import os88marty


def _open(ui, spec, title):
    ui.path(spec)
    w = ui.wait_window(title)
    ui.mo.to(630, 470)
    os88marty.guest_sleep(ui.m, 6.0)
    ui.settle(limit=200.0)
    print(spec, "->", ui.titles())
    return ui.window(title)


def capture(ui, shot):
    w = _open(ui, "A:/MEDIA/OS8088.GIF", "Paint")
    shot("os8088-gif", window=w.title)
    ui.close(w)
    os88marty.guest_sleep(ui.m, 3.0)

    w = _open(ui, "B:/MEDIA/SAMPLE.BMP", "Paint")
    shot("sample-bmp", window=w.title)
    ui.close(w)
    os88marty.guest_sleep(ui.m, 3.0)

    w = _open(ui, "B:/MEDIA/GUIDE.TEX", "TeXPad")
    shot("guide-tex", window=w.title, pad=8)
