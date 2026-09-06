#!/usr/bin/env python3
"""`os88disk.py --verify` over every image the default build produces.

    python3 tests/unit/t_diskverify.py

The tool has had a structural fsck since SPEC.md 18.4 and `make` runs it on
the C, Word and RunCPM disks - and on NONE of the seven the default build
ships.  That is the whole of this file: the check exists, it is good, and it
was pointed at the on-demand images and not at the ones every user boots.

It is the SECOND reader of these volumes.  `tests/unit/t_image.py` walks them
from the format with an independent implementation and checks the os8088
rules - contiguity, the BPB, SPEC.md 19.6's attributes; this one runs the
tree's own fsck, which knows things a re-implementation should not have to
(the free-count, the FAT copies agreeing).  Neither subsumes the other and
they disagree in different directions, which is the point of having two.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

IMAGES = ["os8088.img", "os8088-120.img", "os8088-720.img", "os8088-360.img",
          "apps.img", "apps120.img", "apps720.img", "apps360.img",
          "media360.img"]


def main():
    seen = 0
    for img in IMAGES:
        p = os.path.join(ROOT, "build", img)
        if not check(os.path.exists(p), "%s exists" % img, "run `make` first"):
            continue
        r = subprocess.run([sys.executable, "tools/os88disk.py", "--verify", p],
                           cwd=ROOT, capture_output=True, text=True)
        check(r.returncode == 0, "%s passes the structural fsck" % img,
              "a volume os8088 is happy with and a volume that is COHERENT are "
              "two different claims (docs/FIELD-NOTES.md 4)",
              got=(r.stdout + r.stderr).strip().splitlines()[-1:] or "no output",
              want="verify OK")
        seen += 1
    print("t_diskverify: %d images" % seen)
    done("t_diskverify")


if __name__ == "__main__":
    main()
