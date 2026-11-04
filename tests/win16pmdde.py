#!/usr/bin/env python3
"""win16pmdde - Program Manager's DDE server (SPEC.md 124.33.2,
docs/plans/WIN16-ANY-PLAN.md S24).

    python3 tests/win16pmdde.py            (row win16pmdde)
    python3 tests/win16pmdde.py --keep     leave the QEMU for a look

*As a user, a setup program that finishes by asking Program Manager for a
group leaves a folder of links on my desktop.*

PMDDE.EXE (tests/win16/pmdde.c, `make win16fixtures`) talks to PROGMAN over
DDE as a setup program does: WM_DDE_INITIATE to HWND_BROADCAST, answered by
a window of class Progman inside the send; four blocks of commands, each
acknowledged (CreateGroup, ShowGroup and two AddItems; an unknown command,
refused; DeleteItem; a group made and deleted); a WM_DDE_REQUEST refused;
WM_DDE_TERMINATE both ways. Its MessageBox must say "PMDDE OK".

Then the disks are read back with the QEMU gone. On B:, WIN16's volume:
SYSTEM/APPDATA/WIN16/GROUPS/PMDDETES/PMTEST.LNK is a link to B:\\PMDDE.EXE,
README.LNK is not there (DeleteItem took it), and neither is the deleted
group's PMDDEGON. On A:, SYSTEM.CFG's shortcut trailer names the group's
folder and not the deleted one.
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

NAMES = ["PMDDE.EXE"]
WANT = "PMDDE OK"
GRP = "SYSTEM/APPDATA/WIN16/GROUPS/PMDDETES"
SC_PATH = b"SYSTEM\\APPDATA\\WIN16\\GROUPS\\PMDDETES"
SC_GONE = b"SYSTEM\\APPDATA\\WIN16\\GROUPS\\PMDDEGON"
ENV = dict(os.environ, MTOOLS_SKIP_CHECK="1")


def mread(img, path):
    """the bytes of PATH on IMG, or None"""
    r = subprocess.run(["mtype", "-i", img, "::" + path], env=ENV,
                       capture_output=True)
    return r.stdout if r.returncode == 0 else None


def mlist(img, path):
    """the names in folder PATH on IMG, upper case, or None"""
    r = subprocess.run(["mdir", "-b", "-i", img, "::" + path], env=ENV,
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return [os.path.basename(x.rstrip("/")).upper() for x in r.stdout.split()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    for n in NAMES:
        if not os.path.exists(b(os.path.join("win16", n))):
            print("win16pmdde: SKIP - no build/win16/%s (`make win16fixtures`)" % n)
            return 77
    import os88ui
    import xmcheck
    sym = xmcheck.sym
    bad = []
    work = tempfile.mkdtemp(prefix="w16p-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, NAMES)
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, sym)
        p = F.Prog(d, ui, sym, S, NAMES[0])
        p.launch(dw)
        d.wait(lambda: p.waiting_msgbox(F.msgrow()), 300, "PMDDE's MessageBox")
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
        aimg, bimg = d.p.a, d.p.b
        names = mlist(bimg, GRP)
        print("2. B:%s holds %s" % (GRP, names))
        if names is None:
            bad.append("no folder %s on B:" % GRP)
        else:
            if "PMTEST.LNK" not in names:
                bad.append("no PMTEST.LNK in the group's folder")
            if "README.LNK" in names:
                bad.append("README.LNK is still there after DeleteItem")
        lnk = mread(bimg, GRP + "/PMTEST.LNK")
        if lnk is not None:
            want = b"LK\x01\x01\x00PMDDE.EXE\x00"
            print("3. PMTEST.LNK: %r" % lnk[:16])
            if len(lnk) != 50 or not lnk.startswith(want):
                bad.append("PMTEST.LNK is %r, not a link to B:\\PMDDE.EXE"
                           % lnk[:20])
        gone = mlist(bimg, "SYSTEM/APPDATA/WIN16/GROUPS")
        if gone is not None and "PMDDEGON" in gone:
            bad.append("DeleteGroup left PMDDEGON")
        cfg = mread(aimg, "SYSTEM.CFG") or b""
        print("4. SYSTEM.CFG: the group's shortcut %s, the deleted one's %s"
              % ("there" if SC_PATH in cfg else "missing",
                 "there" if SC_GONE in cfg else "gone"))
        if SC_PATH not in cfg:
            bad.append("no desktop shortcut to the group in SYSTEM.CFG")
        if SC_GONE in cfg:
            bad.append("the deleted group's shortcut is still in SYSTEM.CFG")
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16pmdde: Program Manager's groups and items made over DDE")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
