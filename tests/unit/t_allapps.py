#!/usr/bin/env python3
"""SPEC.md 19.10 - the everything set grows a disk when the payload needs one.

`make allapps` needs the C toolchain and the RunCPM fetch, so no fast row can
build the real set. This drives tools/os88allapps.py over a SYNTHETIC payload
shaped to need three 1.44MB disks, and reads every image back with t_image's
FAT reader - not os88disk.py, whose arithmetic the tool shares - to hold the
rules a set has to keep:

  * every input lands on exactly one disk, in its own folder;
  * a folder that fits on one disk is never split (it is a whole program);
  * a COLLECTION bigger than a disk is split, and never inside a stem group;
  * placement is first fit in payload order;
  * every disk carries DOCS, SYSTEM/APPDATA and CONTENTS.TXT;
  * a disk the set stops needing is deleted, not left looking current;
  * a non-collection folder bigger than a floppy is refused by name.
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import check, done                                    # noqa: E402
from t_image import Vol, read                                      # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOL = os.path.join(ROOT, "tools", "os88allapps.py")
KB = 1024


def make(td, folder, name, kb):
    d = os.path.join(td, "src", folder)
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, name)
    with open(p, "wb") as f:
        f.write(bytes([len(name)]) * (kb * KB))
    return "%s:%s" % (folder, p)


def run(td, entries, prefix="set"):
    lst = os.path.join(td, prefix + ".list")
    r = subprocess.run([sys.executable, TOOL, "--size", "1440",
                        "--prefix", os.path.join(td, prefix), "--list", lst,
                        "--folder", "DOCS", "--folder", "SYSTEM/APPDATA",
                        "--collection", "APPS"] + entries,
                       capture_output=True, text=True)
    imgs = []
    if r.returncode == 0:
        imgs = [l.strip() for l in open(lst) if l.strip()]
    return r, imgs


def listing(img):
    """{'FOLDER/NAME.EXT', ...} for files, and the set of folder paths."""
    v = Vol(read(img), img)
    files, dirs = set(), set()
    for path, name11, attr, _, _ in v.walk():
        nm = name11.decode("ascii")
        full = nm[:8].strip() + ("." + nm[8:].strip() if nm[8:].strip() else "")
        if attr & 0x10:
            dirs.add(path + full)
        else:
            files.add(path + full)
    return files, dirs


def main():
    with tempfile.TemporaryDirectory() as td:
        # APPS is a collection of twenty 100KB programs - 2MB, more than a
        # 1.44MB disk - with PIC.DAT/PIC.TXT a same-stem pair inside it.
        # WHOLE and BIG are programs: 700KB and 900KB, each must stay whole.
        ents = [make(td, "APPS", "P%02d.DAT" % i, 100) for i in range(18)]
        ents += [make(td, "APPS", "PIC.DAT", 100), make(td, "APPS", "PIC.TXT", 100)]
        ents += [make(td, "WHOLE", "W1.DAT", 350), make(td, "WHOLE", "W2.DAT", 350)]
        ents += [make(td, "BIG", "B1.DAT", 450), make(td, "BIG", "B2.DAT", 450)]
        r, imgs = run(td, ents)
        check(r.returncode == 0, "os88allapps.py failed on a 3.6MB payload",
              got=(r.stderr or r.stdout).strip()[-400:])
        check(len(imgs) == 3, "a 3.6MB payload did not take three 1.44MB disks",
              got=len(imgs), want=3)
        where = {}
        for i, img in enumerate(imgs):
            files, dirs = listing(img)
            for need in ("DOCS", "SYSTEM", "SYSTEM/APPDATA"):
                check(need in dirs, "%s has no %s folder" % (os.path.basename(img), need),
                      why="SPEC.md 19.9: every disk of the set is a launch volume")
            check("CONTENTS.TXT" in files,
                  "%s has no CONTENTS.TXT" % os.path.basename(img))
            for f in files:
                if f not in ("CONTENTS.TXT",):
                    where.setdefault(f, []).append(i)
        for e in ents:
            folder, path = e.split(":", 1)
            key = "%s/%s" % (folder, os.path.basename(path))
            check(len(where.get(key, [])) == 1,
                  "%s is on %d disks" % (key, len(where.get(key, []))), want=1)
        for folder in ("WHOLE", "BIG"):
            on = {i for k, v in where.items() if k.startswith(folder + "/") for i in v}
            check(len(on) == 1, "folder %s is split across disks %s" % (folder, sorted(on)),
                  why="a folder that fits on one disk is a whole program")
        on = {i for k, v in where.items() if k.startswith("APPS/") for i in v}
        check(len(on) > 1, "the 2MB APPS collection was not split")
        check(where.get("APPS/PIC.DAT") == where.get("APPS/PIC.TXT"),
              "a same-stem group was split", got=(where.get("APPS/PIC.DAT"),
                                                  where.get("APPS/PIC.TXT")))
        check(where.get("APPS/P00.DAT") == [0],
              "first fit: the first entry is not on disk 1", got=where.get("APPS/P00.DAT"))
        text = read(imgs[0])
        check(b"(this disk)" in text and os.path.basename(imgs[2]).encode() in text,
              "CONTENTS.TXT does not map the whole set")

        # The payload shrinks: the third disk must go, not linger as current
        r, imgs2 = run(td, ents[:12])
        check(r.returncode == 0 and len(imgs2) == 1,
              "a 1.2MB payload did not fit one disk", got=len(imgs2))
        check(not os.path.exists(imgs[2]) and not os.path.exists(imgs[1]),
              "a disk the set no longer needs was left in place")

        # A folder that is ONE program and bigger than a floppy: refused
        big = [make(td, "HUGE", "H%d.DAT" % i, 500) for i in range(3)]
        r, _ = run(td, big, prefix="huge")
        check(r.returncode != 0 and "HUGE" in r.stderr,
              "a 1.5MB non-collection folder was not refused by name",
              got=r.stderr.strip()[-300:])
    done("t_allapps")


if __name__ == "__main__":
    main()
