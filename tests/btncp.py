#!/usr/bin/env python3
"""THE CONTROL PANEL'S BUTTONS, DRIVEN (SPEC.md 2.6, 13.8.4, 20.5.1.3).

The panel is an ON-DEMAND MODULE with a CS of its own, so it reaches the
shared button by `call COLD_SEG:os88ui_btn_f` - a FAR call, invisible to a
grep for `call os88ui_btn`.  That is how every page of it was missed when the
control started taking a record: the far entry went on pointing at the
record-based routine, which read a live count and a rect pointer out of a
RECTANGLE's coordinates, and the Date/Time page filled the whole screen white
while Theme lost its buttons.

tests/btnall.py drives the packages; this drives the far side.  It asserts on
PIXELS because the record here is the kernel's shared one and says nothing
about which page drew what - and because the glass is what the report was.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT + "/tools")
sys.path.insert(0, ROOT + "/tests")
import os88marty                                           # noqa: E402
import os88mouse                                           # noqa: E402
import os88sym                                             # noqa: E402
import dispcp                                              # noqa: E402

PAGES = [(0, "Scheduler"), (1, "Date/Time"), (3, "Sound"), (5, "Theme")]


def run():
    S = os88sym.linear
    fails = []
    with os88marty.launch(ROOT + "/build/os8088-360.img",
                          apps=ROOT + "/build/apps360.img",
                          machine="os8088_5150_herc_gla", boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        os88marty.settle(m)
        wx, wy = dispcp._cp_win(m, S)
        hide = m.read(S("cp_hide"), 1)[0]

        def lit():
            """non-black pixels in the panel's PANE - the page's own area."""
            fw, fh, d = m.fbuf(0)
            n = 0
            for y in range(wy + 20, min(fh, wy + 150)):
                for x in range(wx + 90, min(fw, wx + 320)):
                    i = (y * fw + x) * 3
                    if d[i:i + 3] != b"\x00\x00\x00":
                        n += 1
            return n

        def screen_lit():
            fw, fh, d = m.fbuf(0)
            return sum(1 for i in range(0, len(d), 3)
                       if d[i:i + 3] != b"\x00\x00\x00")

        total = fw_total = None
        for rec, name in PAGES:
            if hide & (1 << rec):
                print("  %-11s hidden on this machine" % name)
                continue
            row = sum(1 for r in range(rec) if not (hide & (1 << r)))
            mo.click(wx + 1 + dispcp.CP_IX + 30,
                     wy + dispcp.TITLE_H + 1 + dispcp.CP_I0Y
                     + row * dispcp.CP_IROWH + dispcp.CP_IROWH // 2, settle=0)
            m.advance(frames=90)
            m.run()
            pane, scr = lit(), screen_lit()
            fw, fh, _ = m.fbuf(0)
            # A page that drew a garbage rectangle whites the SCREEN, not the
            # pane: that is the shape the report had, and it is what a rect
            # read as a record produces.
            if scr > fw * fh * 3 // 4:
                fails.append("%s: %d of %d pixels on the WHOLE SCREEN are lit "
                             "- the page drew a garbage rectangle, which is "
                             "what a RECT reaching the record-based entry "
                             "looks like (SPEC.md 2.6)" % (name, scr, fw * fh))
            # ...and a page that drew nothing has lost its controls.
            if pane < 40:
                fails.append("%s: the pane has %d lit pixels - the page drew "
                             "nothing, so its buttons are missing" % (name, pane))
            print("  %-11s pane=%-6d screen=%d" % (name, pane, scr))

    for f in fails:
        print("FAIL " + f)
    print("btncp: %d page(s), %d failure(s)" % (len(PAGES), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(run())
