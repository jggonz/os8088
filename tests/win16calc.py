#!/usr/bin/env python3
"""win16calc - the original Windows Calculator on WIN16 (SPEC.md 124.16,
docs/plans/WIN16-PLAN.md S10).

    python3 tests/win16calc.py           the QEMU leg (row win16calc)
    python3 tests/win16calc.py --keep    leave QEMU and its disks for a look

Calculator 3.00 (CALC.EXE, Windows 3.0's) and 3.10 (W31CALC.EXE, Windows
3.1's) are Microsoft's: the win16-reference skill copies them into
build/win16/, and none there is a SKIP. Each is keyed through its menus and
its own keys, and its DISPLAY is read the way a user reads it into another
program - Edit > Copy, then the kernel's clipboard (`clip_seg`/`clip_bytes`,
SPEC.md 55) - so every reading is CALC's own text, put there through the
hidden EDIT and WM_COPY that SPEC.md 124.16.2 describes:

  Note Pad   types `12+3=`, ^A ^C; Calculator's Edit > Paste keys it in
             and the display must read 15 (a number pasted in from Note Pad);
  Standard   `2.01-2=` answers 0.00 on both - KB Q72540's subtraction
             defect, reproduced and pinned: Windows 3.1's Calculator shows
             0.00 on the reference machine too (win16ref/ref/w31-calc-std.png);
  Scientific `2 y 10 =` is 1024;
  Statistics ^S opens the Statistics Box in a window of its own, RET hands
             the keyboard back, 5 Ins 7 Ins enters the data and Ave (^A) is 6.

The answers are what the same files gave on the reference machines
(win16ref/, `win16ref.py`): 3.0 under Windows 3.0, 3.1 under Windows 3.1.
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
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, b, u16                         # noqa: E402
from win16fpu import Prog, open_b, pkg_syms                   # noqa: E402

W16S_REFUSED = 2
W16S_ENDED = 4

# what each Calculator shows, read through its own Copy
# - and the leading SPACE is CALC's: its display string keeps a sign column
# (" 6." at DS:008Ah after Ave), and Copy (WM_COMMAND 300) lstrcpy's it,
# drops a trailing decimal point, SetWindowText's it into the hidden EDIT,
# selects 0..21h and sends WM_CUT. Nothing on that path can lose the space,
# and Windows 3.1 keeps it: Copy pasted into its Notepad between an x and a
# y reads `x 0.00y` (win16ref/ref/w31-calc-copy2.png)
WANT = {
    "CALC.EXE": {"paste": " 15", "std": " 0.00", "sci": " 1024", "ave": " 6"},
    "W31CALC.EXE": {"paste": " 15", "std": " 0.00", "sci": " 1024", "ave": " 6"},
}

# QEMU sendkey names for what is typed
KEYS = {".": "dot", "-": "minus", "=": "equal", "+": "shift-equal", " ": "spc"}


def progs():
    out = []
    for p in ("CALC.EXE", "W31CALC.EXE"):
        if os.path.exists(b(os.path.join("win16", p))):
            out.append(p)
        else:
            print("  (no build/win16/%s: skipped)" % p)
    return out


def bdisk(out, names):
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                    "--size", "1440", b("win16.o88"), b("win16/DOS.O88"),
                    b("notepad.o88")]
                   + [b(os.path.join("win16", n)) for n in names]
                   + ["--folder", "SYSTEM/APPDATA", "--folder",
                      "SYSTEM/APPDATA/WIN16"],
                   check=True, capture_output=True)


class Calc(object):
    def __init__(self, d, ui, sym, bad, tag):
        self.d, self.ui, self.sym, self.bad, self.tag = d, ui, sym, bad, tag
        self.cseg, self.cbytes = sym("clip_seg"), sym("clip_bytes")

    def keys(self, *names):
        for n in names:
            self.d.hmp("sendkey %s" % n)
            self.d.idle(0.4)

    def type(self, text):
        self.keys(*[KEYS.get(c, c) for c in text])

    def clip(self):
        seg = u16(self.d.read(self.cseg, 2))
        n = u16(self.d.read(self.cbytes, 2))
        if not seg:
            return None
        return self.d.read(seg << 4, n).decode("latin-1")

    def wait_clip(self, before, what):
        box = {}

        def changed():
            c = self.clip()
            if c is not None and c != before:
                box["c"] = c
                return True
            return False
        self.d.wait(changed, 30, what)
        return box["c"]

    def raise_(self, w):
        top = self.ui.front()
        if top is not None and top.i == w.i:
            return
        pt, _ = self.ui._raise_point(w)
        self.d.click(*pt)
        self.d.wait(lambda: (lambda f: f is not None and f.i == w.i)(self.ui.front()),
                    20, "window %r to come to the front" % w.title)
        self.d.idle(1.0)

    def display(self, label):
        """Edit > Copy, and what it put on the os8088 clipboard"""
        before = self.clip()
        self.keys("alt-e", "c")
        return self.wait_clip(before, "%s's Copy (%s) to reach the clipboard"
                              % (self.tag, label))

    def check(self, label, got, want):
        print("   %-10s %r" % (label, got))
        if got != want:
            self.bad.append("%s %s: the display reads %r, not %r"
                            % (self.tag, label, got, want))


def notepad(d, ui, sym, dw, c):
    """Note Pad types `12+3=` and copies it: the clipboard holds it"""
    c.raise_(dw)
    i, _ = ui.entry("NOTEPAD.O88", dw)
    before = set(w.i for w in ui.windows())
    d.dblclick(*ui.row_xy(dw, i - ui.scroll(dw)))
    box = {}

    def opened():
        for w in ui.windows():
            if w.i not in before and w.visible:
                box["w"] = w
                return True
        return False
    d.wait(opened, 60, "Note Pad's window")
    d.idle(3.0)
    old = c.clip()
    c.type("12+3=")
    c.keys("ctrl-a", "ctrl-c")
    got = c.wait_clip(old, "Note Pad's ^C to reach the clipboard")
    print("   Note Pad   %r on the clipboard" % got)
    if got != "12+3=":
        c.bad.append("Note Pad copied %r, not '12+3='" % got)
    return box["w"]


def run_calc(d, ui, sym, S, dw, name, bad, keep, shot):
    c = Calc(d, ui, sym, bad, name)
    want = WANT[name]
    np = notepad(d, ui, sym, dw, c)
    c.raise_(dw)
    p = Prog(d, ui, sym, S, name)
    p.launch(dw)
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED)
           or p.pb("w16_inprog") == 0, 120, "%s to reach its message loop" % name)
    d.idle(4.0)
    st = p.pb("w16_state")
    print("%s: state %d, %s / %s" % (name, st, p.text("w16_l1"), p.text("w16_l2")))
    if st in (W16S_REFUSED, W16S_ENDED):
        bad.append("%s: WIN16 %s it: %s / %s" % (name, "refused" if st == W16S_REFUSED
                   else "ended", p.text("w16_l1"), p.text("w16_l2")))
        return
    c.raise_(p.w)

    # a number pasted in from Note Pad: Edit > Paste keys `12+3=`
    c.keys("esc", "alt-e", "p")
    d.idle(2.0)
    c.check("paste", c.display("paste"), want["paste"])

    # Standard: View > Standard, then KB Q72540's subtraction
    c.keys("alt-v", "t")
    d.idle(2.0)
    c.keys("esc")
    c.type("2.01-2=")
    c.check("standard", c.display("standard"), want["std"])
    if shot:
        shot("%s-std" % name.split(".")[0].lower())

    # Scientific: View > Scientific, x^y
    c.keys("alt-v", "s")
    d.idle(2.0)
    c.keys("esc")
    c.type("2y10=")
    c.check("scientific", c.display("scientific"), want["sci"])

    # Statistics: the box in a window of its own, RET hands the keyboard back
    before = set(w.i for w in ui.windows())
    c.keys("ctrl-s")
    box = {}

    def sbox():
        for w in ui.windows():
            if w.i not in before and w.visible:
                box["w"] = w
                return True
        return False
    d.wait(sbox, 30, "%s's Statistics Box" % name)
    d.idle(2.0)
    c.keys("ret")
    d.wait(lambda: (lambda f: f is not None and f.i == p.w.i)(ui.front()), 20,
           "RET to hand the keyboard back to %s" % name)
    c.keys("esc")
    c.type("5")
    c.keys("insert")
    c.type("7")
    c.keys("insert", "ctrl-a")
    d.idle(1.0)
    c.check("statistics", c.display("statistics"), want["ave"])
    if shot:
        shot("%s-sci" % name.split(".")[0].lower())
    return np


def qemu_leg(bad, keep, names):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16c-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        bdisk(bimg, names)
        d = QDrive(work, bimg, keep)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = pkg_syms()
        dw = open_b(d, ui, xmcheck.sym)

        def shot(name):
            out = b("win16calc-%s.png" % name)
            subprocess.run([sys.executable, "tools/shot.py", d.sock, out],
                           capture_output=True)
        for n in names:
            run_calc(d, ui, xmcheck.sym, S, dw, n, bad, keep, shot)
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    names = progs()
    if os.environ.get("W16CALC_ONLY"):
        names = [x for x in names if x == os.environ["W16CALC_ONLY"]]
    if not names:
        print("win16calc: SKIP - no Calculator in build/win16/ (the "
              "win16-reference skill copies Microsoft's there)")
        return 77
    bad = []
    qemu_leg(bad, a.keep, names)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16calc: Calculator gives the answers Windows gave")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
