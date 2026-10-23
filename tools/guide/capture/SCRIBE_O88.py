"""Scribe's screenshots, for tools/guide/manuals/SCRIBE_O88.md.

Run with: python3 tools/guide/capture/run.py SCRIBE_O88 --apps build/scribe360.img
"""

TITLE = "Scribe"


def esc(ui):
    ui.m.key("Escape")
    ui.settle()


def capture(ui, shot):
    ui.path("B:/SCRIBE.O88")
    ui.settle()
    ui.m.alt("KeyF")
    ui.m.key("KeyO")                    # File > Open...
    ui.chooser_open("SCWELCOM.RTF")
    ui.settle()
    shot("scribe-window", window=TITLE, pad=4)

    ui.m.alt("KeyF")
    shot("scribe-file-menu", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyI")
    shot("scribe-insert-menu", window=TITLE, pad=4)
    ui.m.key("KeyP")                    # Insert > Picture...
    ui.chooser()
    shot("scribe-picture-dialog")
    ui.chooser_cancel()
    ui.settle()

    ui.m.key("F1")                      # the About box
    shot("scribe-about", window=TITLE, pad=4)
    esc(ui)
