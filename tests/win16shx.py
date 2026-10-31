#!/usr/bin/env python3
"""win16shx - ShellExecute and FindExecutable (SPEC.md 124.24.10,
docs/plans/WIN16-ANY-PLAN.md S19).

    python3 tests/win16shx.py            (row win16shx)
    python3 tests/win16shx.py --keep     leave the QEMU for a look

*As a user, a document a Windows 3.1 program opens for me opens in the
os8088 program that opens it when I double-click it.*

SHX.EXE (tests/win16/shx.c, `make win16fixtures`) runs with ROOT.TXT beside
it and SHX.TXT in a folder, DOCS. It asks ShellExecute for a verb other than
"open", a file and a folder that are not there, and FindExecutable for a
program and a document; then ShellExecute opens SHX.TXT by its folder, and
a second call before that launch has started is turned away. Its
MessageBox, read off the program's stack, must be "SHX OK". The launch is
posted to WIN16's wake (124.24.10), so by then os8088's association has
opened the document: a Note Pad window must be on the screen, and only the
folder's SHX.TXT could have opened. It is closed, and SHX ends.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)

NAME = "SHX.EXE"
EDITOR = "Note Pad"         # os8088's .TXT program, on the system disk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    from win16mines import QDrive, b
    import win16fpu as F
    from win16sol import W16S_REFUSED, W16S_ENDED
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16shx: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88geom as geom
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16x-")     # short: a QMP socket path
    try:
        root = os.path.join(work, "ROOT.TXT")
        open(root, "wb").write(b"ROOT.TXT, beside the program.\r\n")
        doc = os.path.join(work, "SHX.TXT")
        open(doc, "wb").write(b"SHX.TXT, in DOCS.\r\n")
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME], [root])
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        subprocess.run(["mmd", "-i", bimg, "::DOCS"], check=True, env=env)
        subprocess.run(["mcopy", "-i", bimg, doc, "::DOCS/"], check=True, env=env)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        before = set(w.i for w in ui.windows() if w.visible)
        p = F.Prog(d, ui, xmcheck.sym, S, NAME)
        p.launch(dw)
        row = F.msgrow()
        d.wait(lambda: p.waiting_msgbox(row), 600, "SHX's MessageBox")
        d.idle(4.0)
        st = p.pb("w16_state")
        if st in (W16S_REFUSED, W16S_ENDED):
            bad.append("WIN16 %s it: %s / %s" % (
                "refused" if st == W16S_REFUSED else "ended",
                p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            print("SHX: " + got)
            if got != "SHX OK":
                bad.append("the MessageBox says %r, not 'SHX OK'" % got)
            d.wait(lambda: any(w.visible and w.title == EDITOR and w.i not in before
                               for w in ui.windows()), 60, "a %s window" % EDITOR)
            ed = [w for w in ui.windows()
                  if w.visible and w.title == EDITOR and w.i not in before]
            print("ShellExecute opened DOCS\\SHX.TXT in %s" % EDITOR)
            w = ed[0]
            d.click(*geom.close_xy(w.x, w.y, w.w, m=d, sym=d.x.sym))
            d.wait(lambda: all(x.i != w.i or not x.visible for x in ui.windows()),
                   60, "%s to close" % EDITOR)
            d.idle(2.0)
            d.key("Enter")
            d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
                   "SHX to end after its MessageBox")
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16shx: SHX's checks passed, and the document opened in %s" % EDITOR)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
