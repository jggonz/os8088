#!/usr/bin/env python3
"""SPEC.md 66: TeXPad's parser writes ITS OWN variables through DS, never ES.

    make && python3 tests/tpstore.py

The parser keeps ES pointed at the SOURCE segment ([tp_srcseg], a heap claim)
while it walks a document, and two routines stored into the package's own
variables with `stos` under it - so the bytes went to the SOURCE segment at the
variable's offset instead. Past the end of a small source claim that is
whatever heap block comes next. Both legs are what the defect looked like from
outside, on the shipped 360KB disks:

  A  `tp_tab_body` cleared tp_colw (12 bytes) on every \\begin{tabular}. Open
     B:\\MEDIA in a Disk window, double-click GUIDE.TEX, close TeXPad: the
     12 bytes had landed in the Disk window's RAISE CACHE (SPEC.md 11.96), and
     the close put it back as a cyan line through a row - one plane cleared.
     The screen must carry no colour at all afterwards.
  B  `tp_c_bslash` copied a \\textbackslash command's NAME the same way, so
     the preview printed `\\` and whatever tp_wbuf last held: "Type
     \\textbackslash section" rendered `Type \\ype`. Two one-line documents
     that differ only in the word before it - `Xa` and `Xb` - must render
     identical previews but for that one character cell. The defect makes the
     command print `\\a` and `\\b`, a second differing cell.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): put either `stos` back
under ES = [tp_srcseg] and its leg fails. Both were run red against the
TeXPad that shipped before the fix: A with 73 coloured pixels, B with 12
differing columns spanning 31 px (two cells) where the fix gives 6 in 7.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui      # noqa: E402

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def coloured(m):
    """Pixels whose R, G and B disagree: the desktop and every Disk window
    are grey, black and white, so any of these is damage."""
    w, _h, d = m.fbuf()
    return sum(1 for i in range(0, len(d), 3)
               if d[i] != d[i + 1] or d[i + 1] != d[i + 2])


def preview(ui, name):
    """Open B:\\name in TeXPad, and the preview pane's pixels; then close."""
    ui.path("B:/" + name)
    ui.settle()
    w, h, d = ui.m.fbuf()
    tp = [x for x in ui.windows() if x.visible and x.title.startswith("TeXPad")]
    win = tp[0]
    # the preview pane is the window's right two thirds (SPEC.md 66), and a
    # short line is CENTRED in it
    x0, x1 = win.x + win.w // 3, win.x + win.w - 24
    y0, y1 = win.y + 60, win.y + 120
    px = {(x, y): d[(y * w + x) * 3] for y in range(y0, y1) for x in range(x0, x1)}
    ui.close(win)
    return px


def main():
    bdir = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
    tmp = tempfile.mkdtemp(prefix="tpstore-")
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(bdir, "os8088-360.img"), sysimg)
    appimg = os.path.join(tmp, "apps360.img")
    shutil.copy(os.path.join(bdir, "apps360.img"), appimg)

    print("A: GUIDE.TEX over a Disk window, then closed")
    with os88ui.boot(sysimg, apps=appimg, machine="os8088_xt_vga") as ui:
        ui.path("B:/MEDIA")
        mw = [w for w in ui.windows() if w.visible and w.title == "MEDIA"][0]
        idx, _ = ui.entry("GUIDE.TEX", mw)
        px, py = ui.row_xy(mw, ui.scroll_to(idx, win=mw))
        ui.mo.dblclick(px, py)
        ui._wait(lambda: any(t.startswith("TeXPad") for t in ui.titles()),
                 "TeXPad on GUIDE.TEX", 90, snapshot=lambda: ui.titles())
        ui.settle()
        for w in list(ui.windows()):
            if w.visible and w.title.startswith("TeXPad"):
                ui.close(w)
        ui.settle()
        n = coloured(ui.m)
        check(n == 0, "the Disk window came back uncorrupted (%d coloured "
              "pixels)" % n)

    print("B: \\textbackslash after `Xa` and after `Xb`")
    docs = []
    for tag in ("A", "B"):
        p = os.path.join(tmp, "BS%s.TEX" % tag)
        with open(p, "w") as f:
            f.write("\\documentclass{article}\n\\begin{document}\n"
                    "X%s \\textbackslash section here.\n\\end{document}\n"
                    % tag.lower())
        docs.append(p)
    bsimg = os.path.join(tmp, "bs360.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools",
                                                        "os88disk.py"),
                           "-o", bsimg, "--size", "360",
                           os.path.join(bdir, "texpad.o88")] + docs,
                          stdout=subprocess.DEVNULL)
    shutil.copy(os.path.join(bdir, "os8088-360.img"), sysimg)
    with os88ui.boot(sysimg, apps=bsimg, machine="os8088_xt_vga") as ui:
        a = preview(ui, "BSA.TEX")
        b = preview(ui, "BSB.TEX")
    cols = sorted({x for (x, y), v in a.items() if b.get((x, y)) != v})
    span = (cols[-1] - cols[0] + 1) if cols else 0
    check(cols and span <= 8,
          "the previews differ in ONE character cell, the `a`/`b` (%d "
          "differing columns, spanning %d px)" % (len(cols), span))

    shutil.rmtree(tmp, ignore_errors=True)
    print("tpstore: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
