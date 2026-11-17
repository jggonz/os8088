#!/usr/bin/env python3
"""PageDesk's Notes & edits list model (PAGEDESK-SPEC 17.3, 13.4, 20.9).

    python3 tests/unit/t_pdnotes.py

Compiles apps/pagedesk/hosttest/pdu_nt.c with the host's `cc` - it #includes
apps/pagedesk/pdunotes.c, the SAME file the package compiles - against a fake
edit store and runs it: the three filters' membership and order, seek/step/find
agreeing for every item, a source annotation a record stands in for listed
once, review state (the newest reply wins; a PageDesk reply to a SOURCE note),
a 256-record store, the source /Subtype table and the change words. ~0.1 s.
SKIPs (never passes) without a host C compiler.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "pagedesk", "hosttest", "pdu_nt.c")
INC = os.path.join(ROOT, "apps", "pagedesk")


def main():
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        print("SKIP: no host C compiler (cc/gcc/clang) on PATH")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "pdu_nt")
        r = subprocess.run([cc, "-O1", "-Wall", "-Wno-unused-function",
                            "-DPD_HOST", "-I", INC, "-o", exe, SRC],
                           capture_output=True, text=True)
        check(r.returncode == 0, "pdu_nt.c compiles on the host: "
              + (r.stderr.strip()[:400] or "ok"))
        if r.returncode != 0:
            return done('t_pdnotes')
        r = subprocess.run([exe], capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        tail = out[-1] if out else "(no output)"
        check(r.returncode == 0 and tail.endswith(" 0 failed"),
              "every list-model check holds - " + tail
              + ("" if r.returncode == 0 else "\n" + "\n".join(out[:20])))
    return done('t_pdnotes')


if __name__ == "__main__":
    sys.exit(main())
