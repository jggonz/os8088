"""TeXPad's screenshots, for tools/guide/manuals/TEXPAD_O88.md."""

import os88marty


def capture(ui, shot):
    # Double-clicking a .TEX file opens TeXPad on it, already typeset.
    ui.path("B:/MEDIA/PAPER.TEX")
    ui.wait_window("TeXPad")
    ui.settle()
    shot("texpad-window", window="TeXPad", pad=8)

    # Esc typesets and puts the focus on the preview; Page Down turns a page.
    ui.m.key("Escape")
    ui.m.key("PageDown")
    shot("texpad-page2", window="TeXPad", pad=8)

    # File > New's starting document; type a paragraph into the source.
    # The bar says "(edit)" until F5 typesets it again.
    ui.m.ctrl("KeyN")
    ui.settle()
    w = ui.window("TeXPad")
    ui.mo.click(w.x + 8, w.y + 48)      # the source pane's first row
    ui.m.key("PageDown")                # the caret to the last row...
    ui.m.key("ArrowUp")                 # ...and up onto \end{document}
    for ch in "My first paragraph, set by TeXPad.\n\n":
        ui.m.type_text(ch)              # at typing speed: a burst outruns
        os88marty.pace(ui.m, 0.15)      # the keyboard buffer
    shot("texpad-edit", window="TeXPad", pad=8)
    ui.m.key("F5")
    shot("texpad-typeset", window="TeXPad", pad=8)

    # Export PDF asks for a file name in the Standard File dialog.
    ui.menu_pick("File", "Export PDF...")
    ui.chooser()
    ui.settle()
    shot("texpad-export")
    ui.chooser_cancel()
