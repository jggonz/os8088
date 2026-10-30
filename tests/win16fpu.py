#!/usr/bin/env python3
"""win16fpu - floating point on WIN16, with a coprocessor and without one
(SPEC.md 124.5, docs/plans/WIN16-PLAN.md S9).

    python3 tests/win16fpu.py              the QEMU leg (row win16fpu)
    python3 tests/win16fpu.py --marty      the XT leg (row win16fpuxt)
    python3 tests/win16fpu.py --keep       leave QEMU and its disks for a look

QEMU HAS a coprocessor, so its leg is the NATIVE path: the sites stay x87
code, `__FPMATH` is WIN16's, and an unmasked exception travels IRQ 13 ->
the BIOS's int 75h -> WIN16's int 2 -> the program's next int 3Dh. MartyPC's
stock 5150 has NO coprocessor, so its leg is the EMULATOR's: every site an
`int 34h`-`3Dh` and part W16P_FPU fetched. Which path a leg took is
read off WIN16 (`w16_fpm`), not assumed.

Each program prints its answers into a MessageBox, and the text is read off
the program's own stack while it waits there (MessageBox's second argument),
against what Windows 3.0 showed for the same file (win16ref/ref/
w30-fix-fpu.png, w30-fix-fpx.png):

  FPU.EXE   2.01-2, 2.01*2, 2.01/2 to six places (tests/win16/fpu.c);
  FPX.EXE   an unmasked 1/0 reaching the program's signal(SIGFPE) handler,
            the same masked, 1/3 and sqrt(2) to fifteen (tests/win16/fpx.c).

Calculator is S10's (its `win16calc` row keys 2.01-2= and must read 0.00,
KB Q72540 reproduced): S9 takes it past __FPMATH and the C runtime's
floating-point start-up into its message loop, and its window is S10's.
The fixtures are `make win16fixtures`'s; none built is a SKIP.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, MDrive, b, u16                 # noqa: E402


def pkg_syms():
    """win16mines.pkg_syms, with W16_DEFINES' test defines (W16_X87_EMU:
    the emulator on a machine with a coprocessor) in the assembly too, so a
    knob build's symbols are the knob build's"""
    defs = os.environ.get("W16_DEFINES", "").split()
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "w.asm"), os.path.join(d, "w.map")
        open(cp, "w").write('%%include "win16/win16.asm"\n'
                            "[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/win16/", "-o", os.path.join(d, "w.bin")]
                       + ["-D" + x for x in defs] + [cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out

# A stock 5150 - an 8088 and no coprocessor: the emulator's path, and the
# in-guest census (SPEC.md 124.4.1) sweeping each program first. S9 ran this
# leg on a V20 because that census walked a modrm+immediate instruction short
# and called FPU.EXE "80386" off the imm8 of `sub sp, 60h` at seg 1:0013;
# S11 fixed it, and tests/unit/t_w16cens.py holds it to os88ne.py's walk
XT = "os8088_5150_cga_gla"
W16S_REFUSED = 2
W16S_ENDED = 4
W16F_ARGS = 12
fpm_seen = [None]           # w16_fpm while the last program ran
FPM = {0: "none", 1: "the emulator", 2: "the coprocessor"}

WANT = {
    "FPU.EXE": "2.01-2 = 0.010000\n2.01*2 = 4.020000\n2.01/2 = 1.005000",
    "FPX.EXE": "1/0 unmasked: SIGFPE\n1/0 masked: infinity\n"
               "1/3 = 0.333333333333333\nsqrt(2) = 1.414213562373095",
}
# With no coprocessor FPX's runtime finds no 8087 (a bare fninit/fnstcw, which
# nothing traps on an 8086), so its _control87 cannot mask and WIN87EM's 1332h
# stands: the "masked" 1/0 is a second SIGFPE (tests/win16/fpx.c says why).
# It is the MACHINE's answer, not the path's: under -DW16_X87_EMU on QEMU the
# probe meets QEMU's real FPU and the mask takes, so only the XT leg expects it
WANT_EMU = dict(WANT)
WANT_EMU["FPX.EXE"] = WANT["FPX.EXE"].replace(
    "1/0 masked: infinity", "1/0 masked: SIGFPE (no 8087 to mask)")


def msgrow():
    """MessageBox's export row: the W16EXPORT lines' order is the table's"""
    n = 0
    for line in open(os.path.join(ROOT, "apps", "win16", "w16api.inc")):
        if line.startswith("W16EXPORT"):
            f = [x.strip() for x in line.split(";")[0][len("W16EXPORT"):].split(",")]
            if f[0] == "W16M_USER" and f[1] == "1":
                return n
            n += 1
    raise RuntimeError("no MessageBox row in w16api.inc")


def progs():
    out = []
    for p in ("FPU.EXE", "FPX.EXE"):
        if os.path.exists(b(os.path.join("win16", p))):
            out.append(p)
        else:
            print("  (no build/win16/%s: skipped)" % p)
    return out


def bdisk(out, size, names):
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                    "--size", str(size), b("win16.o88"), b("win16/DOS.O88")]
                   + [b(os.path.join("win16", n)) for n in names]
                   + ["--folder", "SYSTEM/APPDATA", "--folder",
                      "SYSTEM/APPDATA/WIN16"],
                   check=True, capture_output=True)


class Prog(object):
    def __init__(self, d, ui, sym, S, name):
        self.d, self.ui, self.sym, self.S, self.name = d, ui, sym, S, name
        self.w, self.seg = None, 0

    def pw(self, n, off=0):
        return u16(self.d.read((self.seg << 4) + self.S[n] + off, 2))

    def pb(self, n, off=0):
        return self.d.read((self.seg << 4) + self.S[n] + off, 1)[0]

    def text(self, n):
        raw = self.d.read((self.seg << 4) + self.S[n], 48)
        return raw.split(b"\0")[0].decode("latin-1")

    def launch(self, dw):
        ui, d = self.ui, self.d
        i, _ = ui.entry(self.name, dw)
        before = set(w.i for w in ui.windows())
        d.dblclick(*ui.row_xy(dw, i - ui.scroll(dw)))
        box = {}

        def opened():
            # WIN16's window, not DOS.O88's: the association opens DOS, which
            # hands the NE on, so the instance is the one holding its name
            for w in ui.windows():
                if w.i not in before and w.visible:
                    seg = u16(d.read(geom.winptr(d, w, self.sym) + geom.W_SEG, 2))
                    if seg and d.read((seg << 4) + self.S["w16_name"], 13) \
                            .split(b"\0")[0].decode("latin-1") == self.name:
                        box["p"] = (w, seg)
                        return True
            return False
        d.wait(opened, 120, "a window for %s" % self.name)
        self.w, self.seg = box["p"]

    def waiting_msgbox(self, row):
        st = self.pb("w16_state")
        return st in (W16S_REFUSED, W16S_ENDED) or \
            (self.pb("w16_inprog") == 0 and self.pw("w16_idx") == row)

    def msgtext(self):
        """MessageBox(hwnd, text, caption, type), pascal: the last argument
        pushed is at +0, so the text's far pointer is at +6"""
        ss, sp = self.pw("w16_pss"), self.pw("w16_psp_")
        a = (ss << 4) + sp + W16F_ARGS
        off, seg = struct.unpack("<HH", self.d.read(a + 6, 4))
        raw = self.d.read((seg << 4) + off, 200)
        return raw.split(b"\0")[0].decode("latin-1")


def run_prog(d, ui, sym, S, dw, name, bad, tag, shot=None, want=WANT):
    p = Prog(d, ui, sym, S, name)
    t0 = ticks(d)
    p.launch(dw)
    row = msgrow()
    d.wait(lambda: p.waiting_msgbox(row), 600, "%s's MessageBox" % name)
    t = ticks(d) - t0
    d.idle(4.0)
    st = p.pb("w16_state")
    fpm = fpm_seen[0] = p.pb("w16_fpm")
    print("%s %s: state %d, x87 through %s; double-click to its MessageBox "
          "%d ticks (%.1f s)" % (tag, name, st, FPM.get(fpm, fpm), t, t * 0.054925))
    if st in (W16S_REFUSED, W16S_ENDED):
        print("   %s / %s" % (p.text("w16_l1"), p.text("w16_l2")))
    if st in (W16S_REFUSED, W16S_ENDED):
        bad.append("%s %s: WIN16 %s it: %s / %s" % (tag, name, "refused" if st == W16S_REFUSED
                   else "ended", p.text("w16_l1"), p.text("w16_l2")))
        return p
    if shot:
        shot(name)
    got = p.msgtext()
    want = want[name]
    print("   " + got.replace("\n", "\n   "))
    if got != want:
        bad.append("%s %s: the MessageBox says %r, not %r" % (tag, name, got, want))
    d.key("Enter")
    d.wait(lambda: p.pb("w16_state") == W16S_ENDED or gone(ui, p), 60,
           "%s to end after its MessageBox" % name)
    d.idle(4.0)
    return p


def ticks(d):
    return struct.unpack("<I", d.read(0x46C, 4))[0]


def title(ui, p):
    for w in ui.windows():
        if w.i == p.w.i:
            p.w = w
            return w.title
    return ""


def gone(ui, p):
    return all(w.i != p.w.i or not w.visible for w in ui.windows())


def open_b(d, ui, sym):
    before = set(w.i for w in ui.windows())
    d.dblclick(*geom.drive_pt(d, "B", sym))
    box = {}

    def disk():
        for w in ui.windows():
            if w.i not in before and w.visible:
                box["w"] = w
                return True
        return False
    d.wait(disk, 60, "B:'s window")
    d.idle(4.0)
    return box["w"]


def qemu_leg(bad, keep, names):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16f-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        bdisk(bimg, 1440, names)
        d = QDrive(work, bimg, keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = pkg_syms()
        dw = open_b(d, ui, xmcheck.sym)

        def shot(name):
            out = b("win16fpu-%s.png" % name.split(".")[0].lower())
            subprocess.run([sys.executable, "tools/shot.py", d.sock, out],
                           capture_output=True)
        for n in names:
            p = run_prog(d, ui, xmcheck.sym, S, dw, n, bad, "QEMU", shot)
            if fpm_seen[0] != (1 if "W16_X87_EMU" in os.environ.get("W16_DEFINES", "") else 2):
                bad.append("QEMU %s: a machine WITH a coprocessor took %s"
                           % (n, FPM.get(fpm_seen[0], fpm_seen[0])))
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))


def marty_leg(bad, names):
    import os88ui
    work = tempfile.mkdtemp(prefix="w16fx-", dir=os.path.join(ROOT, "build"))
    try:
        bimg = os.path.join(work, "b360.img")
        bdisk(bimg, 360, names)
        with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=XT,
                         verbose=False) as ui:
            d = MDrive(ui)
            d.key = lambda n: ui.m.key({"dot": ".", "minus": "-", "equal": "="}.get(n, n))
            S = pkg_syms()
            dw = open_b(d, ui, ui.sym)
            for n in names:
                p = run_prog(d, ui, ui.sym, S, dw, n, bad, "XT", want=WANT_EMU)
                if fpm_seen[0] != 1:
                    bad.append("XT %s: an 8088 with no coprocessor took %s, not "
                               "the emulator" % (n, FPM.get(fpm_seen[0], fpm_seen[0])))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--marty", action="store_true")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    names = progs()
    if os.environ.get("W16FPU_ONLY"):
        names = [x for x in names if x == os.environ["W16FPU_ONLY"]]
    if not names:
        print("win16fpu: SKIP - neither FPU.EXE nor FPX.EXE is built "
              "(make win16fixtures)")
        return 77
    bad = []
    if a.marty:
        marty_leg(bad, names)
    else:
        qemu_leg(bad, a.keep, names)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        emu = a.marty or "W16_X87_EMU" in os.environ.get("W16_DEFINES", "")
        print("win16fpu: %s" % ("the emulator gives the answers expected"
                                if emu else "the coprocessor gives Windows 3.0's answers"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
