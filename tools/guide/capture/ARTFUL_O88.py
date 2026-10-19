"""ArtfulType's screenshots, for tools/guide/manuals/ARTFUL_O88.md.

Run with --apps build/office360.img: WRITING.MD rides that disk's MEDIA/.
"""

# The app draws its own bar: File, Edit, Style, View, Help from x = 8,
# each title cell 16 px wider than its name; pull-down rows are 13 px from y 21.
VIEW_X = 8 + 48 + 48 + 56 + 20
ROW = lambda i: 21 + 13 * i + 6     # noqa: E731


def capture(ui, shot):
    # Double-clicking a .MD file opens it in ArtfulType, full screen.
    ui.path("B:/MEDIA/WRITING.MD")
    ui.settle()

    # The View menu, held open, then Markdown picked from it.
    def aim(mo):
        mo.to(VIEW_X, ROW(0), l=True)
        shot("artful-view-menu", rect=(0, 0, 320, 140))
        return VIEW_X, ROW(0)
    ui.mo.menu(VIEW_X, 10, VIEW_X, ROW(0), aim=aim)
    ui.settle()
    shot("artful-markdown")

    # ...and back to Writer, the styled view.
    ui.mo.menu(VIEW_X, 10, VIEW_X, ROW(1))
    ui.mo.to(632, 300)        # the pointer out of the text
    ui.settle()
    shot("artful-writer")

    # Esc hands the screen back: the window is the start card.
    ui.m.key("Escape")
    ui.settle()
    shot("artful-splash", window="ArtfulType", pad=8)
