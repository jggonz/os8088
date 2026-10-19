"""The ten typeface pages' screenshots, and the FONTS folder's, for
tools/guide/manuals/FONTS.md and tools/guide/manuals/<FACE>_F88.md.

    python3 tools/guide/capture/run.py FONTS

One boot: open SYSTEM/FONTS, double-click the first face (Font Viewer opens
on it), type one sample, then press Down for each of the other nine. The
sample stays put when the face changes, so all ten show the same words.
Keys go in one at a time with time to repaint between them (see
FONTVIEW_O88.py).
"""

FACES = ["archivo", "charter", "courier", "helv", "inconsol",
         "jetbrain", "noto", "robomono", "tallx", "times"]

SAMPLE = "Sphinx of black quartz, judge my vow! 0123456789 ($4.50 & 27%)"


def capture(ui, shot):
    m = ui.m

    def key(name, frames=20):
        m.key(name)
        m.advance(frames=frames)
        m.run()

    w = ui.path("A:/SYSTEM/FONTS")
    ui.mo.to(632, 440)
    x, y, ww, hh = ui._rect(w.i)
    shot("fonts-folder", rect=(x - 8, y - 8, ww + 18, hh + 18))
    ui.open("ARCHIVO.F88")                 # the association opens Font Viewer
    ui.mo.to(632, 440)
    ui.settle()
    for _ in range(66):                    # empty the specimen, one at a time
        key("Backspace")
    for ch in SAMPLE:
        m.type_text(ch)
        m.advance(frames=20)
        m.run()
    ui.settle()
    for i, face in enumerate(FACES):
        if i:
            key("ArrowDown", 70 * 4)       # the next face, and its load
            ui.settle()
        shot(face + "-f88", window="Font Viewer", pad=8)
