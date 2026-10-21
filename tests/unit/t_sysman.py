#!/usr/bin/env python3
"""t_sysman: System Check's manifest IS the system disk (SPEC.md 118.2.1).

    python3 tests/unit/t_sysman.py

tools/os88sysman.py writes build/syscheck_man.inc from the system disk's
INPUTS, because the image depends on SYSCHECK.O88 and a manifest read off it
would be a cycle. That is only right while tools/os88disk.py copies a file
exactly as it is given - so this reads every manifest file back off each
built kern_big system disk, with a walker of its own, and holds its size and
its sum to the manifest's. A packer that moved into the image step, a disk
that dropped a driver, or a manifest from a stale build all fail here, and
each would otherwise surface as System Check calling a fresh install broken.

It also holds the two things SPEC.md 118.2 says about SYSCHECK.O88 itself:
every system disk carries it in SYSTEM\\, and the manifest does not name it.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88sysman            # noqa: E402
from os88fat import Fat12    # noqa: E402
from harness import check, done  # noqa: E402

BUILD = os.path.join(ROOT, "build")
IMAGES = ["os8088.img", "os8088-720.img", "os8088-120.img", "os8088-360.img"]
FOLDERS = {0: None, 1: "SYSTEM", 2: "STIKAPPS"}


def entries(fat, first):
    """A folder's 32-byte entries: the root when `first` is None."""
    if first is None:
        for _i, _o, e in fat.entries():
            yield e
        return
    csz = fat.spc * fat.bps
    for c in fat.chain(first):
        o = fat.cluster_off(c)
        for k in range(0, csz, 32):
            e = bytes(fat.img[o + k:o + k + 32])
            if e[0] == 0:
                return
            if e[0] != 0xE5:
                yield e


def read(fat, e):
    size = struct.unpack_from("<I", e, 28)[0]
    first = struct.unpack_from("<H", e, 26)[0]
    if not first:
        return b""
    csz = fat.spc * fat.bps
    return b"".join(bytes(fat.img[fat.cluster_off(c):fat.cluster_off(c) + csz])
                    for c in fat.chain(first))[:size]


def folder(fat, name):
    for e in entries(fat, None):
        if e[11] & 0x10 and Fat12.pretty(e[:11]) == name:
            return struct.unpack_from("<H", e, 26)[0]
    return None


def main():
    inc = os.path.join(BUILD, "syscheck_man.inc")
    if not os.path.exists(inc):
        print("t_sysman: no %s - run make" % inc)
        return 1
    rows = os88sysman.read_inc(inc)
    check(len(rows) > 20, "the manifest names the system disk's files",
          got=len(rows))
    check(all(r[2] != "SYSCHECK.O88" for r in rows),
          "SYSCHECK.O88 is not in its own manifest (SPEC.md 118.2)")
    for img in IMAGES:
        path = os.path.join(BUILD, img)
        if not os.path.exists(path):
            check(False, "%s is built" % img, "`make` builds all four")
            continue
        fat = Fat12(path)
        where = {0: None}
        for k, name in FOLDERS.items():
            if name:
                where[k] = folder(fat, name)
        for kind, f, name, size, sm in rows:
            first = where.get(f)
            if f and first is None:
                check(False, "%s carries %s\\" % (img, FOLDERS[f]))
                continue
            hit = [e for e in entries(fat, first)
                   if Fat12.pretty(e[:11]) == name and not e[11] & 0x18]
            if not hit:
                check(False, "%s carries %s" % (img, name),
                      "the manifest names it, so System Check would call a "
                      "fresh install broken")
                continue
            data = read(fat, hit[0])
            check(len(data) == size and os88sysman.sums(data) == sm,
                  "%s's %s is the manifest's, byte for byte" % (img, name),
                  "os88disk must copy a file as it is given (SPEC.md 118.2.1)",
                  got=(len(data), os88sysman.sums(data)), want=(size, sm))
        sysf = where.get(1)
        check(sysf is not None and any(
              Fat12.pretty(e[:11]) == "SYSCHECK.O88"
              for e in entries(fat, sysf)),
              "%s carries SYSTEM\\SYSCHECK.O88 (SPEC.md 118.10)" % img)
    done("t_sysman: %d files against %d system disks" % (len(rows), len(IMAGES)))


if __name__ == "__main__":
    sys.exit(main())
