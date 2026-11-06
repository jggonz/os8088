"""Mouse Check's screenshots, for tools/guide/manuals/MOUSECHK_O88.md.

Run with --apps build/util360.img: the program is at that disk's root.
"""


def capture(ui, shot):
    ui.path("B:/MOUSECHK.O88")
    ui.m.advance(frames=90)
    shot("mousechk-window", window="Mouse Check", pad=10, settle=False)
    ui.m.type_text("n")
    ui.m.advance(frames=90)
    shot("mousechk-page2", window="Mouse Check", pad=10, settle=False)
    ui.m.type_text("p")
    ui.m.advance(frames=90)
    shot("mousechk-probe", window="Mouse Check", pad=24, settle=False)
