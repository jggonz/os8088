#!/usr/bin/env python3
"""win16cdlg - COMMDLG's dialogs (SPEC.md 124.24, docs/plans/WIN16-ANY-PLAN.md
S19).

    python3 tests/win16cdlg.py            (row win16cdlg)
    python3 tests/win16cdlg.py --keep     leave the QEMU for a look

*As a user, File > Open in a Windows 3.1 program shows os8088's Standard
File dialog* - and its font dialog is WIN16's.

One boot on VGA, three legs:

  1. CDLG.EXE (tests/win16/cdlg.c, `make win16fixtures`): ChooseFont waits
     with a window titled Font up; Enter is its OK. ChooseColor waits with
     Color up; Enter. The MessageBox, read off the program's stack, must say
     each answered TRUE, the face MS Sans Serif at 8 points (its "Helv"
     mapped, 124.24.4), red's COLORREF, and that the font made from it,
     selected, is MS Sans Serif 13 rows high - the DC's font picking the
     face of GetTextFace and GetTextMetrics;
  2. Notepad 3.1, where the win16-reference skill put it: Alt+F O opens a
     window that is not Notepad's - the chooser (124.24.2) - and Esc closes
     it; Notepad is left running, no longer waiting in GetOpenFileName,
     and CommDlgExtendedError's word is 0 (Cancel is seen, not told);
  3. Marquee 3.1's setup dialog (SSMARQUE.EXE): Format Text opens Font,
     Enter closes it, and the program carries on.

Microsoft's files are never in this tree; legs 2 and 3 are skipped without
them. Without CDLG.EXE the row is a SKIP.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16sol import (window_of, W16S_REFUSED, W16S_RUNNING,  # noqa: E402
                      W16S_ENDED)

WANT = ("font 1 MS Sans Serif 80; colour 1 0000FF; "
        "face MS Sans Serif 13")


def exprow(mod, ordinal):
    """the export row of MOD.ORDINAL: the W16EXPORT lines' order is the
    table's (win16fpu.msgrow's reading)"""
    n = 0
    for line in open(os.path.join(ROOT, "apps", "win16", "w16api.inc")):
        if line.startswith("W16EXPORT"):
            f = [x.strip() for x in line.split(";")[0][len("W16EXPORT"):].split(",")]
            if f[0] == mod and f[1] == str(ordinal):
                return n
            n += 1
    raise RuntimeError("no %s.%d row in w16api.inc" % (mod, ordinal))


def waiting_in(p, row):
    st = p.pb("w16_state")
    return st in (W16S_REFUSED, W16S_ENDED) or \
        (p.pb("w16_inprog") == 0 and p.serves(row))


def titled(ui, word):
    return any(w.visible and w.title == word for w in ui.windows())


def ended(p, bad, tag):
    st = p.pb("w16_state")
    if st in (W16S_REFUSED, W16S_ENDED):
        bad.append("%s: WIN16 %s it: %s / %s" % (tag, "refused" if st == W16S_REFUSED
                   else "ended", p.text("w16_l1"), p.text("w16_l2")))
        return True
    return False


def leg_cdlg(d, ui, S, dw, bad):
    from win16cpu import raise_
    raise_(d, ui, dw)
    p = F.Prog(d, ui, d.x.sym, S, "CDLG.EXE")
    p.launch(dw)
    for mod, ordinal, word in (("W16M_COMMDLG", 15, "Font"),
                               ("W16M_COMMDLG", 5, "Color")):
        row = exprow(mod, ordinal)
        d.wait(lambda: waiting_in(p, row), 300, "CDLG to wait in %s" % word)
        d.wait(lambda: titled(ui, word), 60, "a window titled %s" % word)
        if ended(p, bad, "CDLG"):
            return
        print("CDLG: waiting in %s, its dialog up" % word)
        d.idle(2.0)
        d.key("Enter")
    row = F.msgrow()
    d.wait(lambda: waiting_in(p, row), 120, "CDLG's MessageBox")
    if ended(p, bad, "CDLG"):
        return
    got = p.msgtext()
    print("CDLG: " + got)
    if got != WANT:
        bad.append("CDLG: the MessageBox says %r, not %r" % (got, WANT))
    d.key("Enter")
    d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
           "CDLG to end")
    d.idle(4.0)


def leg_notepad(d, ui, S, dw, bad):
    from win16cpu import raise_
    raise_(d, ui, dw)
    p = F.Prog(d, ui, d.x.sym, S, "NOTEPAD.EXE")
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or p.text("w16_l2") == "Running", 300, "Notepad to run")
    d.idle(6.0)
    if ended(p, bad, "Notepad"):
        return
    before = set(w.i for w in ui.windows() if w.visible)
    d.hmp("sendkey alt-f")
    d.idle(1.5)
    d.hmp("sendkey o")
    row = exprow("W16M_COMMDLG", 1)
    d.wait(lambda: waiting_in(p, row), 60, "Notepad to wait in GetOpenFileName")
    d.wait(lambda: any(w.visible and w.i not in before for w in ui.windows()),
           60, "the chooser's window")
    print("Notepad: waiting in GetOpenFileName, the chooser up")
    d.idle(2.0)
    d.hmp("sendkey esc")
    d.wait(lambda: p.pw("w16_ofnwin") == 0, 30, "the Cancel to be seen")
    d.idle(4.0)
    if ended(p, bad, "Notepad"):
        return
    if p.pb("w16_state") != W16S_RUNNING:
        bad.append("Notepad: WIN16 is in state %d after the Cancel, not running"
                   % p.pb("w16_state"))
    if p.pb("w16_inprog") == 0 and p.serves(row):
        bad.append("Notepad: still waiting in GetOpenFileName after the Cancel")
    if p.pw("w16_cderr") != 0:
        bad.append("Notepad: CommDlgExtendedError is %04X after a Cancel, not 0"
                   % p.pw("w16_cderr"))
    print("Notepad: the Cancel answered, Notepad running")
    w = window_of(ui, p)
    if w is not None:
        import os88geom as geom
        d.click(*geom.close_xy(w.x, w.y, w.w, m=d, sym=d.x.sym))
        d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
               "Notepad to end")
        d.idle(4.0)


def leg_marquee(d, ui, S, dw, bad):
    from win16cpu import raise_
    raise_(d, ui, dw)
    p = F.Prog(d, ui, d.x.sym, S, "SSMARQUE.EXE")
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or p.text("w16_l2") == "Running", 300, "Marquee's setup to run")
    d.idle(6.0)
    if ended(p, bad, "Marquee"):
        return
    w = window_of(ui, p)
    if w is None:
        bad.append("Marquee: no setup window")
        return
    # Format Text..., the right-hand column's third button on VGA: 75 left of
    # the frame's right edge, 99 below its top (Windows 3.1's template in the
    # dialog face's units, 124.22.17)
    d.click(w.x + w.w - 75, w.y + 99)
    row = exprow("W16M_COMMDLG", 15)
    d.wait(lambda: waiting_in(p, row), 60, "Marquee to wait in ChooseFont")
    d.wait(lambda: titled(ui, "Font"), 60, "a window titled Font")
    print("Marquee: Format Text opened Font")
    d.idle(2.0)
    d.key("Enter")
    d.wait(lambda: not titled(ui, "Font"), 60, "Font to close")
    d.idle(3.0)
    if ended(p, bad, "Marquee"):
        return
    if p.pb("w16_inprog") == 0 and p.serves(row):
        bad.append("Marquee: still waiting in ChooseFont after its OK")
    print("Marquee: Font closed, the setup dialog carries on")
    d.key("esc")
    d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
           "Marquee to end")
    d.idle(4.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", "CDLG.EXE"))):
        print("win16cdlg: SKIP - no build/win16/CDLG.EXE (`make win16fixtures`)")
        return 77
    names = ["CDLG.EXE"]
    for n in ("NOTEPAD.EXE", "SSMARQUE.EXE"):
        if os.path.exists(b(os.path.join("win16", n))):
            names.append(n)
        else:
            print("  (no build/win16/%s: its leg skipped)" % n)
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16c-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, names)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        leg_cdlg(d, ui, S, dw, bad)
        if "NOTEPAD.EXE" in names:
            leg_notepad(d, ui, S, dw, bad)
        if "SSMARQUE.EXE" in names:
            leg_marquee(d, ui, S, dw, bad)
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
        print("win16cdlg: %s - every check passed" % ", ".join(names))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
