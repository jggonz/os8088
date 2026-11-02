#!/usr/bin/env python3
"""win16reg - the registration database and OLE's "not available" (SPEC.md
124.24.6, 124.24.7, docs/plans/WIN16-ANY-PLAN.md S19).

    python3 tests/win16reg.py            (row win16reg)
    python3 tests/win16reg.py --keep     leave the QEMU for a look

*As a user, a Windows 3.1 program that registers its documents keeps them
registered, and one that offers OLE runs with OLE greyed.*

REGDB.EXE (tests/win16/regdb.c, `make win16fixtures`) registers a document
class as an installer would - an extension, a class name and its open
command - and reads it back by every route SHELL has: RegQueryValue by full
name, through an open key and cut short; RegEnumKey in order and one past
the last; RegDeleteKey and the key gone after it. Then OleRegisterClientDoc
answers OLE_OK and OleQueryCreateFromClip OLE_ERROR_FORMAT. Its MessageBox,
read off the program's stack, is "REGDB OK" or names each check that failed.

When it has ended, REG.DAT is read off the floppy image on the host, in
SYSTEM\\APPDATA\\WIN16: the [HKEY_CLASSES_ROOT] section must hold exactly
what the delete left - .w16 and w16file, with their values.
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

NAME = "REGDB.EXE"
WANT_KEYS = [(".w16", "w16file"), ("w16file", "W16 Document")]


def regdat(img):
    """REG.DAT off the image, or None"""
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    r = subprocess.run(["mtype", "-i", img, "::SYSTEM/APPDATA/WIN16/REG.DAT"],
                       capture_output=True, env=env)
    return r.stdout if r.returncode == 0 else None


def keys(text):
    """[HKEY_CLASSES_ROOT]'s key lines, as (key, value) in order"""
    out, inside = [], False
    for line in text.decode("latin-1").splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        if line.startswith("["):
            inside = line.upper() == "[HKEY_CLASSES_ROOT]"
            continue
        if inside:
            k, _, v = line.partition("=")
            out.append((k.strip(), v.strip()))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16reg: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    import os88ui
    import xmcheck
    bad = []
    work = tempfile.mkdtemp(prefix="w16r-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "REGDB OK"})
        d.idle(4.0)                     # its profiles, written as it ended
        d.hmp("stop")                   # the image, as the guest left it
        text = regdat(d.p.b)
        if text is None:
            bad.append("REG.DAT is not in B:\\SYSTEM\\APPDATA\\WIN16")
        else:
            got = keys(text)
            print("REG.DAT [HKEY_CLASSES_ROOT]: %r" % got)
            if got != WANT_KEYS:
                bad.append("REG.DAT holds %r, not %r" % (got, WANT_KEYS))
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
        print("win16reg: REGDB's checks passed, and REG.DAT holds what it left")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
