#!/usr/bin/env python3
"""win16thunk - discardable code reached by far calls only (SPEC.md 124.26.14,
WIN16-NEXT c3, the real-mode arm).

    python3 tests/win16thunk.py            (row win16thunk)
    python3 tests/win16thunk.py --keep     leave the QEMU for a look

*As a user, a program whose code Microsoft's linker laid out as far calls
between discardable segments runs on a machine that cannot hold all of it.*

THKCALL.EXE (tests/win16/thkcall.asm, `make win16fixtures`) has two 3,500-byte
discardable segments, A and B, that the entry point reaches only by
`CALL FAR seg:off` with a selector relocation on the segment word - the shape
InstallShield's _INST16.EXE has 6,955 times - and a third, C, that the entry
point calls the same way but that A also names with `MOV AX, seg C`.
WIN16 is built into a private tree with `W16DEFS=-DW16_CODEKB=5`
(tools/os88build.py), so A and B do not fit together: calling B discards A,
calling A discards B. Under QEMU (kern_big, VGA) the MessageBox it ends with
must say `thkcall: abacxba` (A, B, A, A+10h reading C:0, C+10h, B, A), and
WIN16's two counters must read what the calls make them:

    5 segments loaded through a thunk (A, B, A, B, A) and
    4 discarded (A, B, A, B) while C, kept when A first loaded, stays.

A fixed A or B (the thunk skipped, 124.26.1's old rule applied to a call) loads
all of them at launch, and the counters read 0 and 0; a call left on a segment
number runs the wrong bytes; a C that was discarded leaves A+10h reading a
stale paragraph. The fixture not built is a SKIP.
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
import os88build                                              # noqa: E402
from win16mines import b                                      # noqa: E402
import win16fpu as F                                          # noqa: E402
from win16sol import SDrive                                   # noqa: E402

NAME = "THKCALL.EXE"
CODEKB = 5
WANT = "thkcall: abacxba"
LOADS, DROPS = 5, 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(b(os.path.join("win16", NAME))):
        print("win16thunk: SKIP - no build/win16/%s (make win16fixtures)" % NAME)
        return 77
    tree = os88build.tree("W16DEFS=-DW16_CODEKB=%d" % CODEKB,
                          targets=("win16.o88",))
    os.environ["W16_DEFINES"] = "W16_CODEKB=%d" % CODEKB
    import os88ui
    import xmcheck
    from win16cpu import raise_
    bad = []
    work = tempfile.mkdtemp(prefix="w16t-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", bimg,
                        "--size", "1440", tree.img("win16.o88"),
                        b("win16/DOS.O88"), b(os.path.join("win16", NAME)),
                        "--folder", "SYSTEM/APPDATA",
                        "--folder", "SYSTEM/APPDATA/WIN16"],
                       check=True, capture_output=True)
        d = SDrive(work, b("os8088.img"), bimg, a.keep)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        S = F.pkg_syms()
        dw = F.open_b(d, ui, xmcheck.sym)
        raise_(d, ui, dw)
        p = F.Prog(d, ui, xmcheck.sym, S, NAME)
        p.launch(dw)
        row = F.msgrow()
        d.wait(lambda: p.waiting_msgbox(row), 300, "%s's MessageBox" % NAME)
        d.idle(4.0)
        st = p.pb("w16_state")
        if st in (F.W16S_REFUSED, F.W16S_ENDED):
            bad.append("WIN16 %s it: %s / %s"
                       % ("refused" if st == F.W16S_REFUSED else "ended",
                          p.text("w16_l1"), p.text("w16_l2")))
        else:
            got = p.msgtext()
            load, drop = p.pw("w16_cnload"), p.pw("w16_cndrop")
            print("%s: %r; %d segments loaded through a thunk, %d discarded, "
                  "under a %d KB code budget" % (NAME, got, load, drop, CODEKB))
            if got != WANT:
                bad.append("the MessageBox says %r, not %r" % (got, WANT))
            if (load, drop) != (LOADS, DROPS):
                bad.append("%d loaded and %d discarded, not %d and %d: "
                           "the segments were not discardable by thunk"
                           % (load, drop, LOADS, DROPS))
        if not a.keep:
            d.p.close()
    except StopIteration:
        pass
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16thunk: far calls between discardable segments went "
              "through thunks, and the segment a `mov` names was kept")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
