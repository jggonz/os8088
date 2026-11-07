"""Video Battle's screenshots, for tools/guide/manuals/VBATTLE_O88.md.

    python3 tools/guide/capture/run.py VBATTLE_O88 --machine os8088_xt_vga_144 \
        --image build/os8088.img --apps build/vbattlev.img

The VGA demo disk (BATTLEV.VBT beside VBATTLE.O88) in B: of the 1.44MB VGA
XT. One play: the window, the title loop, the menu under the idle loop, Ari's
Bolt, Bram's turn, then Esc back to the window and its File menu. The battle
streams off the floppy here, so the waits are generous; the pictures are
the point, not the timing.
"""

import os88marty

T = "Video Battle"


def _wait(ui, secs):
    os88marty.guest_sleep(ui.m, secs)


def _tap(ui, name, secs=0.4):
    ui.m.key(name)
    _wait(ui, secs)


def _menu_shot(ui, shot, title, sid):
    """Drop a menu, take the picture, and let it go over nothing."""
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    _wait(ui, 1.5)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(470, 5, l=True)
    ui.mo._edge(False)
    _wait(ui, 1.5)


def _park(ui):
    w = ui.window(T)
    ui.mo.to(min(630, w.x + w.w + 10), 460)


def capture(ui, shot):
    ui.path("B:/VBATTLE.O88")
    _wait(ui, 3.0)
    _park(ui)
    ui.settle(limit=300.0)
    shot("vbattle-window", window=T, pad=10)

    # Enter: the battle is read, then the full screen and its title loop
    _tap(ui, "Enter", 1.0)
    _wait(ui, 40.0)
    shot("vbattle-title", settle=False)

    # Enter again: the title fades, the opening plays, the enemy appears
    _tap(ui, "Enter", 1.0)
    _wait(ui, 30.0)
    shot("vbattle-menu", settle=False)

    # Ari's Bolt: Down to it, Enter; a moment into its clip
    _tap(ui, "ArrowDown", 1.0)
    _tap(ui, "Enter", 0.2)
    _wait(ui, 2.0)
    shot("vbattle-clip", settle=False)

    # the clip over and its damage counted: Bram's turn
    _wait(ui, 20.0)
    shot("vbattle-turn", settle=False)

    # Esc at a top menu leaves the battle for its window
    _tap(ui, "Escape", 1.0)
    _wait(ui, 10.0)
    _park(ui)
    ui.settle(limit=300.0)
    shot("vbattle-again", window=T, pad=10)
    _menu_shot(ui, shot, "File", "vbattle-file-menu")
