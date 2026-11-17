#!/usr/bin/env python3
"""PageDesk's edit store and undo (PAGEDESK-SPEC 13.1.1, 13.6, 20.8).

    python3 tests/unit/t_pded.py

Compiles apps/pagedesk/hosttest/pdu_ed_test.c with the host's `cc` - it
#includes apps/pagedesk/pduedit.c and pduundo.c, the SAME files the package
compiles, over pdmath.c's real-mode arena - and runs it: every annotation kind
in 13.1.1's layout, the limits (64 records - O9 -, 1,024 bytes of text, the heaps),
undo and redo of create/set/move/delete/text/points and the typing session
against snapshots, coalescing, the 64-command history and its announced
truncation, the save point, source annotations, page-operation deltas, and 40
seeded random walks of commands undone and redone in random order with every
state compared with the one taken when it was made (a third of the commands
make several calls aimed at one record, the way a caller may mix a move, a
resize and a points change in one bracket), and pdu_ed_gen (D19). ~0.5 s. SKIPs (never
passes) without a host C compiler.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "pagedesk", "hosttest", "pdu_ed_test.c")
INC = os.path.join(ROOT, "apps", "pagedesk")


def main():
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        print("SKIP: no host C compiler (cc/gcc/clang) on PATH")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "pdu_ed")
        r = subprocess.run([cc, "-O1", "-Wall", "-Wno-unused-function",
                            "-Wno-unused-variable", "-DPD_HOST", "-I", INC,
                            "-o", exe, SRC, os.path.join(INC, "pdmath.c"),
                            os.path.join(INC, "hosttest", "pdhost.c")],
                           capture_output=True, text=True)
        check(r.returncode == 0, "pdu_ed_test.c compiles on the host: "
              + (r.stderr.strip()[:400] or "ok"))
        if r.returncode != 0:
            return done('t_pded')
        r = subprocess.run([exe], capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        tail = out[-1] if out else "(no output)"
        check(r.returncode == 0 and tail.endswith(" 0 failed"),
              "every edit-store and undo check holds - " + tail
              + ("" if r.returncode == 0 else "\n" + "\n".join(out[:20])))
    return done('t_pded')


if __name__ == "__main__":
    sys.exit(main())
