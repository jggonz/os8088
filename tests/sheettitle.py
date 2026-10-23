#!/usr/bin/env python3
"""SHEET's window says which document it is (SPEC.md 81.103).

    make && python3 tests/sheettitle.py

The caption was always "Sheet" - the open file's name appeared nowhere on the
screen. It is "Sheet - <NAME>" now, Word's "Microsoft Word - <NAME>"
convention, composed into a buffer the window template's title points at.

  1. a document DOUBLE-CLICKED in the Disk window: the caption names it from
     the first frame - the name is known at entry (sh_note_arg copies it,
     touching no disk) and composed before the window is created, because
     SPEC.md 11.92 forbids retitling from inside a paint, which is where that
     document is actually read
  2. File > New: the caption becomes "Sheet - SHEET1.SLK" - an event path, so
     sh_repaint's sync tells the kernel, which redraws the caption strip
  3. ...and the strip's pixels DID change: the record alone could be right
     while the glass still said the old name

The title is read the way the kernel reads it: the record's W_TITLE, in the
window's own segment (os88geom.windows).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "unit"))
import os88marty as M                                       # noqa: E402
import os88sheetfmt as F                                     # noqa: E402
from os88mouse import Mouse                                  # noqa: E402
import dispcp                                                # noqa: E402
import os88geom                                             # noqa: E402
import os88sym                                              # noqa: E402
from harness import check, done                              # noqa: E402
import sheetfmt as SF                                        # noqa: E402

WORK = "build/sheettitle"
DISK = "build/sheettitle.img"
NAME = "LEDGER.SLK"
TITLE_H = 18


def main():
    os.chdir(os.path.join(HERE, ".."))
    os.makedirs(WORK, exist_ok=True)
    src = os.path.join(WORK, NAME)
    open(src, "wb").write(F.write_sylk({(0, 0): 1.0}))
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", DISK, "--size",
                    "360", "build/sheet.o88", "build/CHART.OVL",
                    "build/MACRO.OVL", src], check=True,
                   stdout=subprocess.DEVNULL)
    S = os88sym.linear
    got = {}
    with M.launch(SF.SYS, apps=DISK, machine="os8088_xt_vga") as m:
        M.settle(m)
        M.no_saver(m)
        mo = Mouse(marty=m)
        dispcp.open_drive(m, mo, S, M.settle, letter="B")
        M.settle(m)
        ds = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, ds)
        dispcp.open_named(m, mo, S, M.settle, wx, wy, name=NAME)
        M.settle(m, limit=240)
        sl = dispcp.win_list(m, S, check=False)[-1]
        x0, y0, w0, h0 = dispcp.win_rect(m, S, sl)

        def title():
            for w in os88geom.windows(m):
                if w.i == sl:
                    return w.title
            return None

        def strip():
            w, h, rgb = m.fbuf()
            return b"".join(rgb[(y * w + x0) * 3:(y * w + x0 + w0) * 3]
                            for y in range(y0, y0 + TITLE_H))

        got["launch"] = title()
        before = strip()
        M.write_png_rgb(os.path.join(WORK, "1-launch.png"), *m.fbuf())
        # 2: File > New, Worksheet (the default radio), OK
        bar = y0 + 18 + 7
        mo.menu(x0 + 1 + (8 * 4 + 16) // 2, bar,
                x0 + 1 + 24 + 6, y0 + 18 + 14 + 2 + 6)
        M.settle(m, limit=120)
        dlg = dispcp.win_list(m, S, check=False)[-1]
        if dlg != sl:
            mo.click(*SF.fdlg_okxy(m))   # the live window's rect, not a literal
            M.settle(m, limit=120)
        mo.to(x0 + w0 - 30, y0 + h0 // 2)
        M.settle(m)
        got["new"] = title()
        after = strip()
        M.write_png_rgb(os.path.join(WORK, "2-new.png"), *m.fbuf())
        got["strip"] = before != after

    print("   titles %r -> %r" % (got["launch"], got["new"]))
    check(got["launch"] == "Sheet - " + NAME,
          "a double-clicked document is named in the caption from the start",
          "composed at entry from sh_note_arg's copy, before the window",
          got=got["launch"], want="Sheet - " + NAME)
    check(got["new"] == "Sheet - SHEET1.SLK",
          "File > New renames the caption to the default document",
          "sh_repaint's sync, an event and not a paint (11.92)",
          got=got["new"], want="Sheet - SHEET1.SLK")
    check(got["strip"], "the caption strip was REDRAWN, not only the record",
          "OSAPI_WM_TITLE with AX = 0: 'the bytes W_TITLE names changed'")
    done("sheettitle")


if __name__ == "__main__":
    main()
