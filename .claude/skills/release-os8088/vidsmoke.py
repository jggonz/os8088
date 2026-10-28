#!/usr/bin/env python3
"""Boot every machine in the release's videos zip, as a reader would get it.

    python3 .claude/skills/release-os8088/vidsmoke.py --version "$VERSION" \
        [--shots build/smoke-vid]

os8088-<version>-videos.zip is three 86Box machine folders -- a config, its
CMOS in nvr/ and a hard disk -- that a reader drops into 86Box's machines
directory and starts. So this unpacks THE ZIP mkzip.py wrote, not build/,
and boots each folder the way 86Box would: the config as it is, the CMOS it
carries and nothing else. A config whose disk name does not resolve, a CMOS
that stops the AT in BIOS setup, or a disk whose kernel is not this build's
all fail here, where a reader would meet them.

It passes a machine when the kernel on its disk reaches the desktop - the
same test os88box.py's `up` makes, on kernel state rather than on a picture -
inside three emulated minutes, and writes a screenshot of each desktop to
look at. Needs 86Box built (`python3 tools/os88box.py build`, docs/86BOX.md).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = subprocess.check_output(["git", "rev-parse", "--show-toplevel"],
                               text=True).strip()
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88box                                              # noqa: E402

GUEST_MS = 180000


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build"))
    ap.add_argument("--shots", default=os.path.join(ROOT, "build", "smoke-vid"))
    a = ap.parse_args()

    z = os.path.join(a.build_dir, "os8088-%s-videos.zip" % a.version)
    if not os.path.isfile(z):
        sys.exit("vidsmoke: no %s - run mkzip.py first" % z)
    os.makedirs(a.shots, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="vidsmoke-")
    failed = []
    try:
        with zipfile.ZipFile(z) as zf:
            zf.extractall(tmp)
        top = os.path.join(tmp, "os8088-%s-videos" % a.version)
        machines = sorted(m for m in os.listdir(top)
                          if os.path.isfile(os.path.join(top, m, "86box.cfg")))
        if not machines:
            sys.exit("vidsmoke: %s holds no machine folder" % z)
        for m in machines:
            prof = os.path.join(top, m)
            name = "vidsmoke-" + m
            # os88box runs a machine out of a run directory of its own, and
            # 86Box reads the CMOS from nvr/ beside the config: seed it with
            # exactly what the folder ships, as a reader's copy would hold.
            run = os.path.join(os88box.RUNS, name)
            shutil.rmtree(run, ignore_errors=True)
            os.makedirs(os.path.join(run, "nvr"))
            seed = os.path.join(prof, "nvr")
            for n in os.listdir(seed) if os.path.isdir(seed) else ():
                shutil.copy(os.path.join(seed, n), os.path.join(run, "nvr", n))
            b = os88box.launch(prof, name=name, hold=True, temp=True)
            try:
                ms = b.kernel_up(guest_ms=GUEST_MS)
                shot = os.path.join(a.shots, m + ".png")
                b.shot(shot)
                print("vidsmoke: %-14s desktop in %.1f emulated s  %s"
                      % (m, ms / 1000.0, shot))
            except SystemExit as e:
                shot = os.path.join(a.shots, m + "-FAILED.png")
                try:
                    b.shot(shot)
                except SystemExit:
                    shot = "(no picture)"
                print("vidsmoke: %-14s FAILED: %s  %s" % (m, e, shot))
                failed.append(m)
            finally:
                b.quit(remove=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("FAIL: %s" % ", ".join(failed) if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
