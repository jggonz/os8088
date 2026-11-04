#!/usr/bin/env python3
"""win16mscdex - MSCDEX's int 2Fh 15xxh under WIN16 (SPEC.md 124.33.3,
docs/plans/WIN16-NEXT-PLAN.md Q3).

    make cdromtest && make win16fixtures && python3 tests/win16mscdex.py
    python3 tests/win16mscdex.py --keep     leave the QEMU for a look

*As a user, a Windows 3.1 setup that asks DOS whether a CD-ROM drive is there,
and which letter it is, is told the truth.*

MSCDX.EXE (tests/win16/mscdx.c) asks 1500h, 150Bh for the CD's letter and the
next, 150Ch, 150Dh and 1510h, and GetDriveType of the letter. Leg 1 boots the
CD-ROM gate's system disk (SYSTEM.CFG wants CDROM.DRV) with build/cdfix.iso as
the secondary master, so a drive is attached: the MessageBox must be
"MSCDX OK n" with n the CD's letter: 3, D:, because C: is kept for a hard
disk (SPEC.md 18.7.4) and a CD driver's volume is the first free row after
it. Leg 2 boots the same disk with an EMPTY drive: still a drive, so
OK again, which is MSCDEX's own rule (the answers describe the drive, not the
disc). Leg 3, the shipped system disk with no CD at all, must say
"MSCDX NONE". The MessageBox is read off the program's stack, as win16shx does.
"""
import argparse
import os
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)

NAME = "MSCDX.EXE"


def leg(tag, sysimg, iso, want, keep, bad):
    from win16mines import b
    import win16fpu as F
    from win16sol import W16S_REFUSED, W16S_ENDED
    import os88qemu
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16c-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        extra = []
        if iso is not None:
            extra = ["-drive", ("file=%s,format=raw," % iso if iso else "")
                     + "if=ide,index=2,media=cdrom"]
        from win16mines import QDrive
        # QDrive boots build/os8088.img; this leg's A: is its own
        class CDrive(QDrive):
            def __init__(self, work, bimg, keep):
                self.q, self.x = os88qemu, xmcheck
                self.p = os88qemu.Private(work, sysimg, bimg, extra=extra)
                self.sock = self.p.sock
                if keep:
                    import atexit
                    atexit.unregister(self.p.close)
                else:
                    os88qemu.own(os.path.join(work, "qemu.pid"), self.sock)
                xmcheck.wait_desktop(self.sock, "win16mscdex")
        d = CDrive(work, bimg, keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        p = F.Prog(d, ui, xmcheck.sym, S, NAME)
        p.launch(dw)
        row = F.msgrow()
        d.wait(lambda: p.waiting_msgbox(row), 600, "MSCDX's MessageBox")
        d.idle(2.0)
        st = p.pb("w16_state")
        if st in (W16S_REFUSED, W16S_ENDED):
            bad.append("%s: WIN16 %s it: %s / %s" % (
                tag, "refused" if st == W16S_REFUSED else "ended",
                p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            print("%s: %s" % (tag, got))
            if got != want:
                bad.append("%s: the MessageBox says %r, not %r" % (tag, got, want))
            d.key("Enter")
            d.wait(lambda: p.pb("w16_state") == W16S_ENDED or F.gone(ui, p), 60,
                   "MSCDX to end")
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s" % work)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    from win16mines import b
    import os88build
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16mscdex: SKIP - no build/win16/%s (`make win16fixtures`)" % NAME)
        return 77
    cdsys = os88build.at("build/cdromtest.img")
    iso = os88build.at("build/cdfix.iso")
    if not (os.path.exists(cdsys) and os.path.exists(iso)):
        print("win16mscdex: SKIP - no build/cdromtest.img (`make cdromtest`)")
        return 77
    bad = []
    leg("with a disc", cdsys, iso, "MSCDX OK 3", a.keep, bad)
    leg("empty drive", cdsys, "", "MSCDX OK 3", a.keep, bad)
    leg("no CD drive", b("os8088.img"), None, "MSCDX NONE", a.keep, bad)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16mscdex: MSCDEX answered with a disc, with an empty drive "
              "and (as absent) with none")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
