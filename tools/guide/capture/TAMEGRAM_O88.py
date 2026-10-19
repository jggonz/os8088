"""TameGram's screenshots, for tools/guide/manuals/TAMEGRAM_O88.md."""
import random

import os88marty

KEYS = ["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "KeyA", "KeyD",
        "Space"]


def capture(ui, shot):
    ui.path("B:/GAMES/TAMEGRAM.O88")
    w = ui.window("TameGram")
    ui.mo.to(w.x + w.w + 40, w.y + 40)
    rnd = random.Random(8088)

    def tick(n=1):
        os88marty.guest_sleep(ui.m, 0.12 * n)

    # Play: each unit gets a couple of moves, then a hard drop. The worker
    # spends one key a tick, so the keys are paced a tick apart.
    for _ in range(26):
        tick(3)
        for _ in range(rnd.randint(1, 4)):
            ui.m.key(rnd.choice(KEYS))
            tick()
        ui.m.key("Enter")
        tick(4)
    tick(6)
    shot("tamegram-window", window="TameGram", pad=10, settle=False)

    # Halt: the unit freezes and the banner goes up.
    ui.m.type_text("p")
    shot("tamegram-halt", window="TameGram", pad=10)

    # Help > How to Play.
    ui.menu_pick("Help", "How to Play")
    shot("tamegram-howto", window="TameGram", pad=10)
    ui.m.key("Escape")
    tick(3)

    # Colour-blind, and the vector locked: the HUD shows both.
    ui.m.type_text("c")
    tick()
    ui.m.type_text("f")
    tick()
    ui.m.type_text("p")
    for _ in range(4):
        tick(3)
        ui.m.key("Enter")
        tick(4)
    tick(3)
    shot("tamegram-colourblind", window="TameGram", pad=10, settle=False)
