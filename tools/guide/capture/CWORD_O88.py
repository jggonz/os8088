"""The C edition of Word's screenshots, for tools/guide/manuals/CWORD_O88.md.

Run with: python3 tools/guide/capture/run.py CWORD_O88 --apps build/cword360.img
"""

TITLE = "Microsoft Word"


def esc(ui):
    ui.m.key("Escape")
    ui.settle()


def capture(ui, shot):
    # A fresh boot does not yet know that .RTF belongs to this program (it
    # says so itself when it starts), so open the program and then the file.
    ui.path("B:/CWORD.O88")
    ui.settle()
    ui.m.alt("KeyF")
    ui.m.key("KeyO")                    # File > Open...
    ui.chooser_open("WELCOME.RTF")
    ui.settle()
    shot("cword-window", window=TITLE, pad=4)

    ui.m.alt("KeyT")
    ui.m.key("KeyC")
    shot("cword-character", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyT")
    ui.m.key("KeyP")
    shot("cword-paragraph", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyE")
    shot("cword-edit-menu", window=TITLE, pad=4)
    esc(ui)
