#!/usr/bin/env python3
"""PageDesk's 360KB program disk still fits, with room (PAGEDESK-SPEC 20.22.2.8).

    python3 tests/unit/t_pd360.py

The 360KB set is two floppies, and the first one must hold PAGEDESK.O88
whole: one file cannot span two disks. `make pagedeskdisk` is on demand, so
its image is no gate. It went stale at 08:52 while the fix round grew the
program, and t_image / t_diskverify read the stale image and passed. That
left the disk at 359 clusters of 354 with nothing failing. This row builds
the disk again from the CURRENT build/pagedesk.o88. The file list is the
Makefile's own PD360DISK1, the one list the recipe uses too. PDFONTS.TXT is
packed here the way the recipe packs it (lz4, the shipped PKGZ). The row
fails with fewer than SPARE clusters free, so the program's growth is caught
while it can still be paid for. ~0.3 s.
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")
SPARE = 4                       # the lead's margin (the fix round, 2026-11-10)
CLUSTERS = 354                  # a 360KB disk's, 1,024 bytes each


def main():
    mk = open(os.path.join(ROOT, "Makefile")).read()
    m = re.search(r"^PD360DISK1 = (.*)$", mk, re.M)
    check(m is not None, "the Makefile names the 360KB program disk (PD360DISK1)")
    if m is None:
        return done("t_pd360")
    pkg = os.path.join(BUILD, "pagedesk.o88")
    lic = os.path.join(BUILD, "PDFONTS.TXT")
    for p in (pkg, lic):
        check(os.path.exists(p), "%s is built (`make` builds it)" % os.path.relpath(p, ROOT))
        if not os.path.exists(p):
            return done("t_pd360")
    with tempfile.TemporaryDirectory() as tmp:
        pdz = os.path.join(tmp, "pdz")
        os.mkdir(pdz)
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88lz.py"),
                            "--wrap", os.path.join(pdz, "PDFONTS.TXT"), "--fmt", "lz4", lic],
                           capture_output=True, text=True, cwd=ROOT)
        check(r.returncode == 0, "PDFONTS.TXT packs: " + (r.stderr.strip()[:200] or "ok"))
        args = []
        for tok in m.group(1).split():
            tok = tok.replace("$(BUILD)", BUILD).replace("$(PDZ)", pdz)
            check("$" not in tok, "PD360DISK1's %s names only $(BUILD) and $(PDZ)" % tok)
            args.append(tok)
        img = os.path.join(tmp, "pd360.img")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                            "-o", img, "--size", "360"] + args,
                           capture_output=True, text=True, cwd=ROOT)
        out = (r.stdout + r.stderr).strip()
        check(r.returncode == 0, "the 360KB program disk builds: " + out[-300:])
        if r.returncode != 0:
            return done("t_pd360")
        u = re.search(r"(\d+)/%d clusters" % CLUSTERS, out)
        check(u is not None, "os88disk reports the clusters it used: " + out[-200:])
        if u:
            used = int(u.group(1))
            print("pagedesk360.img: %d of %d clusters, %d free (PAGEDESK.O88 %d bytes)"
                  % (used, CLUSTERS, CLUSTERS - used, os.path.getsize(pkg)))
            check(CLUSTERS - used >= SPARE,
                  "the 360KB program disk keeps %d clusters free: %d used of %d. "
                  "Pay for it before it stops fitting (PAGEDESK-SPEC 20.22.2.8)"
                  % (SPARE, used, CLUSTERS))
    return done("t_pd360")


if __name__ == "__main__":
    sys.exit(main())
