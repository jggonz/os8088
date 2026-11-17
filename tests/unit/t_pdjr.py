#!/usr/bin/env python3
"""PageDesk's recovery journal (PAGEDESK-SPEC 14.7, 14.3, 20.17).

    python3 tests/unit/t_pdjr.py

Compiles apps/pagedesk/hosttest/pdu_jr_test.c with the host's `cc` - it
#includes pduedit.c, pduundo.c, pdurecov.c and pdovlrecov.c, the SAME files the
package compiles - over an in-memory FAT, and runs it: what is written is what
a new session recovers (24 seeded random walks at two cluster sizes), the
half-second debounce, a stale fingerprint not offered, a corrupt or torn
record stopping replay at the last good one, a read-only disk turning the
journal off, the 8 KiB limit, pages the journal cannot rebuild, the
interrupted-save note and offer, retirement on Save, and the /NM seed. SKIPs
(never passes) without a host C compiler.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "pagedesk", "hosttest", "pdu_jr_test.c")
INC = os.path.join(ROOT, "apps", "pagedesk")


def main():
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        print("SKIP: no host C compiler (cc/gcc/clang) on PATH")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "pdu_jr")
        r = subprocess.run([cc, "-O1", "-Wall", "-Wno-unused-function",
                            "-Wno-unused-variable", "-DPD_HOST", "-I", INC,
                            "-o", exe, SRC, os.path.join(INC, "pdmath.c"),
                            os.path.join(INC, "hosttest", "pdhost.c")],
                           capture_output=True, text=True)
        check(r.returncode == 0, "pdu_jr_test.c compiles on the host: "
              + (r.stderr.strip()[:400] or "ok"))
        if r.returncode != 0:
            return done('t_pdjr')
        r = subprocess.run([exe], capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        tail = out[-1] if out else "(no output)"
        check(r.returncode == 0 and tail.endswith(" 0 failed"),
              "every edit-store and undo check holds - " + tail
              + ("" if r.returncode == 0 else "\n" + "\n".join(out[:20])))
    return done('t_pdjr')


if __name__ == "__main__":
    sys.exit(main())
