#!/usr/bin/env python3
"""PageDesk's three layouts hold on every content box (PAGEDESK-SPEC 17.1, 20.6.4).

    python3 tests/unit/t_pdlay.py

Compiles apps/pagedesk/hosttest/pdu_lay.c with the host's `cc` - it
#includes apps/pagedesk/pdulay.c, the SAME layout file the package compiles -
and runs it: every content box from 320x100 to 720x460 in every mode, with
and without a document, and the five displays by name (VGA, EGA, Hercules at
348 AND 350 rows, CGA), checking that every region and control lies inside
the box and its own bar or panel, nothing overlaps, the canvas is on the
8-pixel grid gfx_scroll needs, the column tiles the height, the kind follows
the thresholds, and each mode's tools are on the glass at each display.
~0.4 s. SKIPs (never passes) without a host C compiler.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "apps", "pagedesk", "hosttest", "pdu_lay.c")
INC = os.path.join(ROOT, "apps", "pagedesk")


def main():
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        print("SKIP: no host C compiler (cc/gcc/clang) on PATH")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "pdu_lay")
        r = subprocess.run([cc, "-O1", "-Wall", "-Wno-unused-function",
                            "-DPD_HOST", "-I", INC, "-o", exe, SRC],
                           capture_output=True, text=True)
        check(r.returncode == 0, "pdu_lay.c compiles on the host: "
              + (r.stderr.strip()[:400] or "ok"))
        if r.returncode != 0:
            return done('t_pdlay')
        r = subprocess.run([exe], capture_output=True, text=True)
        out = r.stdout.strip().splitlines()
        tail = out[-1] if out else "(no output)"
        check(r.returncode == 0 and tail.endswith(" 0 failed"),
              "every layout check holds - " + tail
              + ("" if r.returncode == 0 else "\n" + "\n".join(out[:20])))
        for line in out:
            if line.startswith(("VGA", "EGA", "Hercules", "CGA")):
                print("  " + line)
    return done('t_pdlay')


if __name__ == "__main__":
    sys.exit(main())
