"""Word's screenshots, for tools/guide/manuals/WORD_O88.md.

Run with: python3 tools/guide/capture/run.py WORD_O88 --apps build/word360.img
"""

TITLE = "Microsoft Word"


def esc(ui):
    ui.m.key("Escape")
    ui.settle()


def capture(ui, shot):
    ui.path("B:/WELCOME.DOC")
    ui.settle()
    shot("word-window", window=TITLE, pad=4)

    ui.m.alt("KeyF")
    shot("word-file-menu", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyT")
    ui.m.key("KeyC")
    shot("word-character", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyT")
    ui.m.key("KeyP")
    shot("word-paragraph", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyE")
    ui.m.key("KeyE")
    shot("word-replace", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyU")
    shot("word-utilities", window=TITLE, pad=4)
    esc(ui)

    ui.m.alt("KeyV")
    ui.m.key("KeyP")
    shot("word-page", window=TITLE, pad=4)
