"""Fractal's screenshots, for tools/guide/manuals/FRACTAL_O88.md."""

import os88geom as geom
import os88marty


def _types(ui):
    """The bar has two menus called Fractal; the types are in the one that
    holds Mandelbrot."""
    for i, c in enumerate(ui.menus()):
        if any(t == "Mandelbrot" for t, _ in c[3]):
            return i
    raise RuntimeError("no Fractal types menu on the bar: %r" % ui.menus())


def _menu_shot(ui, shot, cell_no, sid):
    """Drop menu cell `cell_no`, take the picture, and let it go over
    nothing."""
    _, x0, x1, items = ui.menus()[cell_no]
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 400, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    ui.path("B:/APPS/FRACTAL.O88")
    w = ui.window("Fractal")
    ui.mo.to(620, 400)

    # The first pass paints the picture in coarse bands, then fills it in.
    os88marty.guest_sleep(ui.m, 12.0)
    shot("fractal-progress", window="Fractal", pad=10, settle=False)
    os88marty.guest_sleep(ui.m, 100.0)
    shot("fractal-window", window="Fractal", pad=10, settle=False)

    _menu_shot(ui, shot, _types(ui), "fractal-menu")

    # Julia Rabbit in the Ice palette.
    ui.menu_pick(_types(ui), "Julia Rabbit")
    ui.menu_pick("Colour", "Ice")
    ui.mo.to(620, 400)
    os88marty.guest_sleep(ui.m, 100.0)
    shot("fractal-julia", window="Fractal", pad=10, settle=False)

    # Back to the Mandelbrot in Spectrum, then click where the small bulb at
    # the top meets the body: the view recentres there, one level in.
    ui.menu_pick(_types(ui), "Mandelbrot")
    ui.menu_pick("Colour", "Spectrum")
    w = ui.window("Fractal")
    cx, cy = w.x + 1, w.y + geom.TITLE_H + 10    # the canvas, under the strip
    ui.mo.click(cx + 194, cy + 26)
    ui.mo.to(620, 400)
    os88marty.guest_sleep(ui.m, 100.0)
    shot("fractal-zoom", window="Fractal", pad=10, settle=False)
