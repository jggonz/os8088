"""OS88CZ.COM run in the DOS box, for OS88CZ_COM.md. It is a program for a PC
with MS-DOS, and os8088's DOS window runs it too, which is the easiest way
to show it. It rides SYSTEM/DOS on the 720KB and larger software disks:

    python3 tools/guide/capture/run.py OS88CZ_COM --machine os8088_xt_vga_720b \\
        --apps build/apps720.img
"""

import os88marty

T = "DOS"


def _type(ui, line, wait=3.0):
    ui.m.type_text(line + "\n")
    os88marty.guest_sleep(ui.m, wait)
    ui.settle(limit=300.0)


def capture(ui, shot):
    ui.path("A:/APPS/DOS.O88")
    ui.settle()
    w = ui.window(T)
    x, y, ww, hh = ui._rect(w.i)
    ui.mo.click(x + ww // 2, y + hh - 40)
    ui.settle()
    _type(ui, "b:")
    _type(ui, "cd system")
    _type(ui, "cd dos")
    _type(ui, "os88cz", wait=6.0)
    shot("os88cz-com-usage", window=T, pad=6)
