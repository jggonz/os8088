"""Note Pad's screenshots, for tools/guide/manuals/NOTEPAD_O88.md."""

TEXT = ("Shopping list for the weekend\n"
        "\n"
        "Bread, milk and the good coffee. Floppy disks for the "
        "new machine, and a ribbon for the printer.\n"
        "\n"
        "Call the shop about the monitor. Ask whether the green "
        "screen or the amber one is easier on the eyes.\n")


def capture(ui, shot):
    ui.path("B:/APPS/NOTEPAD.O88")
    ui.m.type_text(TEXT)
    ui.settle()
    shot("notepad-window", window="Note Pad", pad=10)

    # Find: the panel docks at the top, Enter finds the next match.
    ui.m.ctrl("KeyF")
    ui.m.type_text("the")
    ui.m.key("Enter")
    ui.m.key("Enter")
    shot("notepad-find", window="Note Pad", pad=10)

    # Replace: Ctrl-R switches the panel, Tab moves to the Repl box.
    ui.m.ctrl("KeyR")
    ui.m.key("Tab")
    ui.m.type_text("a")
    shot("notepad-replace", window="Note Pad", pad=10)
    ui.m.key("Escape")
    ui.settle()

    # Closing with unsaved work asks first.
    w = ui.window("Note Pad")
    x, y, ww, hh = ui._rect(w.i)
    geom = __import__("os88geom")
    cx, cy = geom.close_xy(w.x, w.y, w.w, ui.m, ui.sym)
    ui.mo.click(cx, cy)
    ui.settle()
    shot("notepad-close", rect=(x - 10, y - 10, ww + 22, hh + 22))
