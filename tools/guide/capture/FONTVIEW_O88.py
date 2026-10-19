"""Font Viewer's screenshots, for tools/guide/manuals/FONTVIEW_O88.md.

Font Viewer lives in APPS/ on the system disk, and a typeface in
SYSTEM/FONTS/ opens it, so this needs nothing in B:.

    python3 tools/guide/capture/run.py FONTVIEW_O88

Keys go in one at a time with time to repaint between them: a burst of
seventy queued Backspaces once ended in STACK OVERFLOW on the UI task.
"""


def capture(ui, shot):
    m = ui.m

    def key(name, frames=20):
        m.key(name)
        m.advance(frames=frames)
        m.run()

    w = ui.path("A:/SYSTEM/FONTS")
    ui.mo.to(632, 440)
    x, y, ww, hh = ui._rect(w.i)
    shot("fontview-folder", rect=(x - 8, y - 8, ww + 18, hh + 18))
    ui.open("CHARTER.F88")                 # the association opens Font Viewer
    ui.mo.to(632, 440)
    ui.settle()
    shot("fontview-window", window="Font Viewer", pad=8)
    key("ArrowUp", 70 * 4)                 # Archivo...
    key("ArrowUp", 70 * 4)                 # ...and round to Times, the last
    ui.settle()
    for _ in range(66):                    # empty the specimen, one at a time
        key("Backspace")
    for ch in "Typed in os8088, 1983 style!":
        m.type_text(ch)
        m.advance(frames=20)
        m.run()
    ui.settle()
    shot("fontview-typed", window="Font Viewer", pad=8)
