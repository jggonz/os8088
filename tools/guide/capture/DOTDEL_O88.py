"""Dot Delirium's screenshots, for tools/guide/manuals/DOTDEL_O88.md.

    python3 tools/guide/capture/run.py DOTDEL_O88 --apps build/games360.img

The pointer is parked at the top-left under the menu bar, outside the
window, so its arrow is not drawn over the maze.
"""
import os88marty

PARK = (4, 24)


def _steer(m, plan):
    for key, secs in plan:
        m.key(key, up=False)
        os88marty.guest_sleep(m, secs)
        m.key(key, down=False)


def capture(ui, shot):
    ui.path("B:/DOTDEL.O88")
    for o in ui.windows():
        if o.visible and o.title != "Dot Delirium":
            ui.close(o)
    ui.mo.to(*PARK)
    os88marty.guest_sleep(ui.m, 6.0)
    shot("dotdel-title", window="Dot Delirium", pad=8, settle=False)

    ui.menu_pick("Dot Delirium", "About Dot Delirium")
    ui.mo.to(*PARK)
    os88marty.guest_sleep(ui.m, 1.0)
    shot("dotdel-about", window="Dot Delirium", pad=8, settle=False)
    # A click puts the card away. (A key does not, though the game's own
    # notes say it should: the card stays up and Enter starts a game under it.)
    w = ui.window("Dot Delirium")
    ui.mo.click(w.x + w.w // 2, w.y + w.h // 2)
    ui.mo.to(*PARK)
    os88marty.guest_sleep(ui.m, 1.0)

    ui.m.key("Enter")
    os88marty.guest_sleep(ui.m, 3.0)    # READY!
    _steer(ui.m, [("ArrowLeft", 1.6), ("ArrowUp", 1.2), ("ArrowRight", 1.4),
                  ("ArrowUp", 1.0), ("ArrowLeft", 1.4)])
    shot("dotdel-play", window="Dot Delirium", pad=8, settle=False)

    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 4.0)
    _steer(ui.m, [("ArrowDown", 1.0), ("ArrowRight", 1.6), ("ArrowDown", 1.0),
                  ("ArrowLeft", 1.2)])
    shot("dotdel-fullscreen", settle=False)
    ui.m.key("KeyF")
    os88marty.guest_sleep(ui.m, 3.0)
