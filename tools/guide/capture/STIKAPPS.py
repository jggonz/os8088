"""The STIKAPPS folder page's screenshot, for tools/guide/manuals/STIKAPPS.md.

    python3 tools/guide/capture/run.py STIKAPPS

The bare desktop's Sticky Apps menu on a fresh boot, with nothing open yet,
so every Sticky App's row shows its Add button (DESKCAL_O88.py takes the
same menu with the calendar ticked On).
"""
import os88marty


def capture(ui, shot):
    m = ui.m
    os88marty.guest_sleep(m, 2.0)
    cells = ui.menus()
    _, x0, x1, _items = [c for c in cells if c[0] == "Sticky Apps"][0]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(m, 8.0)
    shot("stikapps-menu", rect=(130, 0, 400, 340), settle=False)
    ui.mo.to(200, 440, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(m, 2.0)
