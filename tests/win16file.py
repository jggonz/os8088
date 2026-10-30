#!/usr/bin/env python3
"""win16file - files, through WIN16's DOS (SPEC.md 124.21,
docs/plans/WIN16-ANY-PLAN.md S16).

    python3 tests/win16file.py              the QEMU leg (row win16file)
    python3 tests/win16file.py --keep       leave QEMU and its disks for a look

FILES.EXE (tests/win16/files.c, `make win16fixtures`) reaches files through
both doors a Windows program has: KERNEL's _lopen family, OpenFile and
GetTempFileName, and the C runtime's own int 21h - mkdir, chdir, getcwd,
rename, remove, _dos_findfirst and stdio. Every one of them is answered by
DOS.O88's core, which WIN16 hosts (124.21.1), so a check that fails is named
in its MessageBox and the box must say none did.

Then it writes W16FILE.TXT - 250 numbered lines of 40 bytes, more than one
8 KB window of the DOS - and reads it back. The row reads that file off the
floppy image on the host after the program has ended and checks every byte:
the DOS's window flushed, the FAT and the directory entry written, which is
what no message box can say. The fixture not built is a SKIP.
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

NAME = "FILES.EXE"


def lines():
    """files.c's line(): exactly 40 bytes each, CR LF included"""
    return b"".join(("W16FILE line %03d of 250, %04X ........\r\n"
                     % (i, (i * 97) & 0xFFFF)).encode() for i in range(1, 251))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16file: SKIP - FILES.EXE is not built (make win16fixtures)")
        return 77
    import os88ui
    import xmcheck
    from os88fat import Fat12
    bad = []
    work = tempfile.mkdtemp(prefix="w16f-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        F.bdisk(bimg, 1440, [NAME])
        d = QDrive(work, bimg, a.keep)
        d.key = lambda n: d.hmp("sendkey %s" % {"Enter": "ret"}.get(n, n))
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        F.run_prog(d, ui, xmcheck.sym, S, dw, NAME, bad, "QEMU",
                   want={NAME: "files: 0 failed"})
        d.hmp("stop")                   # the image, as the guest left it
        fat = Fat12(d.p.b)
        names = [Fat12.pretty(e[:11]) for _, _, e in fat.entries()]
        try:
            got = fat.read("W16FILE.TXT")
        except Exception as e:          # noqa: BLE001 - absent is the finding
            got = None
            bad.append("W16FILE.TXT is not on B: (%s)" % e)
        if got is not None:
            want = lines()
            if got != want:
                at = next((i for i in range(min(len(got), len(want)))
                           if got[i] != want[i]), min(len(got), len(want)))
                bad.append("W16FILE.TXT is %d bytes, %d wanted, first "
                           "difference at %d" % (len(got), len(want), at))
            else:
                print("B:\\W16FILE.TXT: %d bytes, every one as written" % len(got))
        if "W16DIR" in names:
            bad.append("W16DIR is still on B: - rmdir did not reach the disk")
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
        print("win16file: both doors to the DOS, and 10,000 bytes on the disk")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
