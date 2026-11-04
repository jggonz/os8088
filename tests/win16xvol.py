#!/usr/bin/env python3
"""win16xvol - a setup's copy from its floppy to WIN16's volume (SPEC.md
96.11.11, 96.27.1, 124.28.5, docs/plans/WIN16-ANY-PLAN.md S24).

    python3 tests/win16xvol.py            (row win16xvol)
    python3 tests/win16xvol.py --keep     leave the QEMU for a look

*As a user, a Windows program's setup run from one disk copies its files to
the disk WIN16 is on.*

XVOL.EXE (tests/win16/xvol.c, `make win16fixtures`) is on A:, a copy of the
shipped 1.44 MB system disk, and WIN16 with its DOS is on B:, a 720 KB disk
whose 1 KB clusters are not A:'s 512 bytes - the difference 96.11.11 is
about, and what lets AH=36h's two answers be told apart. The Windows
directory is on another volume than the program, as it is when a setup runs
from its floppy and WIN16 lives on the hard disk. XVOL asks AH=36h about the
Windows directory's drive, then copies itself a kilobyte at a time into
WINDIR\\XVOLDIR and WINDIR\\~XVOL.T and compares each copy with itself. Its
MessageBox must say "XVOL OK".

Then B: is read back with the QEMU gone: both COPY.BIN files must be
XVOL.EXE byte for byte. Before 96.11.11 the DOS core's window used A:'s
512-byte cluster for B:'s files too, and in-place writes were refused.
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
from win16mines import QDrive, b                              # noqa: E402
import win16fpu as F                                          # noqa: E402
import os88geom as geom                                       # noqa: E402

NAME = "XVOL.EXE"
WANT = "XVOL OK"
WINDIR = "SYSTEM/APPDATA/WIN16"
ENV = dict(os.environ, MTOOLS_SKIP_CHECK="1")


class XDrive(QDrive):
    """QDrive with A: given: the system disk with XVOL.EXE added"""

    def __init__(self, work, aimg, bimg, keep):
        import os88qemu
        import xmcheck
        self.q, self.x = os88qemu, xmcheck
        self.p = os88qemu.Private(work, aimg, bimg)
        self.sock = self.p.sock
        if keep:
            import atexit
            atexit.unregister(self.p.close)
        else:
            os88qemu.own(os.path.join(work, "qemu.pid"), self.sock)
        xmcheck.wait_desktop(self.sock, "win16xvol")


def open_a(d, ui, sym):
    before = set(w.i for w in ui.windows())
    d.dblclick(*geom.drive_pt(d, "A", sym))
    box = {}

    def disk():
        for w in ui.windows():
            if w.i not in before and w.visible:
                box["w"] = w
                return True
        return False
    d.wait(disk, 60, "A:'s window")
    d.idle(4.0)
    return box["w"]


def mread(img, path):
    """the bytes of PATH on IMG, or None"""
    r = subprocess.run(["mtype", "-i", img, "::" + path], env=ENV,
                       capture_output=True)
    return r.stdout if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    exe = b(os.path.join("win16", NAME))
    if not os.path.exists(exe):
        print("win16xvol: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    sym = xmcheck.sym
    bad = []
    work = tempfile.mkdtemp(prefix="w16x-")     # short: a QMP socket path
    try:
        aimg = os.path.join(work, "sys.img")
        shutil.copy(b("os8088.img"), aimg)
        subprocess.run(["mcopy", "-i", aimg, exe, "::" + NAME], env=ENV,
                       check=True, capture_output=True)
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 720, [])          # 1 KB clusters against A:'s 512
        d = XDrive(work, aimg, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=sym, verbose=False)
        S = F.pkg_syms()
        dw = open_a(d, ui, sym)
        p = F.Prog(d, ui, sym, S, NAME)
        p.launch(dw)
        d.wait(lambda: p.waiting_msgbox(F.msgrow()), 600, "XVOL's MessageBox")
        d.idle(2.0)
        st = p.pb("w16_state")
        if st in (F.W16S_REFUSED, F.W16S_ENDED):
            bad.append("WIN16 %s it: %s / %s" % (
                "refused" if st == F.W16S_REFUSED else "ended",
                p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            print("1. " + got)
            if got != WANT:
                bad.append("the MessageBox says %r, not %r" % (got, WANT))
            d.key("Enter")
            d.wait(lambda: p.pb("w16_state") == F.W16S_ENDED or F.gone(ui, p),
                   60, "the instance to end after its MessageBox")
            d.idle(3.0)
        if a.keep:
            print("kept: %s (QMP %s)" % (work, d.sock))
            return 1 if bad else 0
        d.p.close()
        want = open(exe, "rb").read()
        for n, sub in enumerate(("XVOLDIR", "~XVOL.T"), 2):
            got = mread(d.p.b, "%s/%s/COPY.BIN" % (WINDIR, sub))
            print("%d. B:%s/%s/COPY.BIN: %s" % (
                n, WINDIR, sub, "missing" if got is None else
                "%d bytes, %s" % (len(got), "the same" if got == want
                                  else "DIFFERENT")))
            if got != want:
                bad.append("B:%s/%s/COPY.BIN is not XVOL.EXE" % (WINDIR, sub))
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16xvol: a program on A: copies itself to WIN16's volume, B:")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
