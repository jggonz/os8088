#!/usr/bin/env python3
"""System Check's disk tasks on kern_mega (SPEC.md 118.14.8) -
python3 tests/fsckmega.py

kern_mega runs FSCHECK.DRV from a claim in PROTECTED MODE: through the
claim's CODE selector, with nothing written through CS and `.cold` reached
by its identity selector. Bochs is kern_mega's instrument (docs/BOCHS.md):
it faults where a real 286 faults, where QEMU lets code write through CS.

A: is `make mega`'s mega.img - SYSCHECK.O88 and kern_mega's own FSCHECK.DRV
in SYSTEM\\ - with an AUTOSTRT link that opens SYSTEM\\SYSCHECK.O88 and names
the drive and the task (SPEC.md 118.14.9), so the run needs no pointer. B:
is a 1.44MB disk, held afterwards to tests/fsckdisk.py's independent FAT
reader:

  LEG 1  `B: REPAIR REPORT` on fsckdisk's seven planted faults: the disk
         comes back clean but for the cross-link, three FILEnnnn.CHK made
  LEG 2  `B: DEFRAG REPORT` on a disk with files in pieces: none left in
         pieces, every file's bytes unchanged

...and on both, kern_mega reports no fault and closed no package.

    make mega && python3 tests/fsckmega.py [--leg 1|2]
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import fsckdisk as F                                        # noqa: E402


def base144(work):
    """A 1.44MB data disk with SYSTEM\\DOS on it, for the fixtures."""
    out = os.path.join(work, "base.img")
    subprocess.check_call(
        ["python3", "tools/os88disk.py", "-o", out, "--size", "1440",
         F.b("redline.o88"), "SYSTEM/DOS:" + F.b("os88net.com"),
         "--folder", "MEDIA"], cwd=ROOT, stdout=subprocess.DEVNULL)
    return out


def system_disk(work, arg):
    """mega.img with \\AUTOSTRT\\FSCK.LNK -> SYSTEM\\SYSCHECK.O88 `arg`."""
    import os88lnk
    v = F.Vol(open(F.b("mega.img"), "rb").read())
    o, e = v.find("\\AUTOSTRT")
    if e is None:
        raise SystemExit("fsckmega: mega.img has no AUTOSTRT folder")
    for need in ("\\SYSTEM\\SYSCHECK.O88", "\\SYSTEM\\FSCHECK.DRV"):
        if v.find(need)[1] is None:
            raise SystemExit("fsckmega: mega.img has no %s - `make mega`"
                             % need)
    v.add("FSCK.LNK", os88lnk.make("SYSTEM\\SYSCHECK.O88", arg=arg),
          parent=struct.unpack_from("<H", e, 26)[0])
    out = os.path.join(work, "a.img")
    open(out, "wb").write(bytes(v.img))
    return out


def run(work, arg, bimg, fails, secs=900.0):
    """Boot, wait for the report's toast, stop. -> B:'s bytes afterwards."""
    import os88bochs as B
    out = os.path.join(work, "bochs")
    os.makedirs(out, exist_ok=True)
    box = B.Box(out, [], aimg=system_disk(work, arg), bimg=bimg,
                btype="1_44", noc=True)
    try:
        box.up()

        def said():
            t = bytes(box.read("toast_buf", 30)).split(b"\0")[0]
            return b"SYSCHECK.TXT" in t
        try:
            box.until(said, "the report's toast", secs=secs, poll=1.0)
        except Exception as ex:             # noqa: BLE001
            fails.append("%s: the report never came: %s" % (arg, ex))
        flt = box.faults()
        if flt:
            fails.append("%s: kern_mega faulted: %r" % (arg, flt))
    finally:
        box.stop()
    return open(os.path.join(out, "b.img"), "rb").read()


def leg1(work, fails):
    img = os.path.join(work, "broken.img")
    open(img, "wb").write(F.broken_fixture(base144(work)))
    v = F.Vol(run(work, "B: REPAIR REPORT", img, fails))
    try:
        text = v.read("\\SYSCHECK.TXT").decode("latin1")
        print(text)
    except Exception:                       # noqa: BLE001
        text = ""
    F.need(fails, text, ["Task: Check and repair the disk", "(fixed)",
                         "8 repairs made", "Result: 1 broken, 0 warnings"],
           "repair on kern_mega")
    r = F.check(v)
    for k in ("lost", "long", "short", "broken", "dot"):
        if r[k]:
            fails.append("kern_mega repair: the reader still finds %s: %r"
                         % (k, r[k]))
    if r["fatdiff"]:
        fails.append("kern_mega repair: the FAT copies still differ")
    if r["xlink"] != ["\\XB.TXT"]:
        fails.append("kern_mega repair: cross-links %r" % r["xlink"])


def leg2(work, fails):
    img = os.path.join(work, "frag.img")
    open(img, "wb").write(F.frag_fixture(base144(work)))
    v0 = F.Vol(open(img, "rb").read())
    sums = {p: hashlib.sha1(v0.read(p)).hexdigest()
            for p, _, e, _ in v0.walk() if not e[11] & 0x10}
    v = F.Vol(run(work, "B: DEFRAG REPORT", img, fails))
    try:
        text = v.read("\\SYSCHECK.TXT").decode("latin1")
        print(text)
    except Exception:                       # noqa: BLE001
        text = ""
    F.need(fails, text, ["Task: Defragment the disk", "0 left in pieces"],
           "defragment on kern_mega")
    r = F.check(v)
    for k in ("lost", "xlink", "long", "short", "broken", "dot"):
        if r[k]:
            fails.append("kern_mega defrag: the reader finds %s: %r"
                         % (k, r[k]))
    left = [p for p in r["frag"] if not p.endswith("SYSCHECK.TXT")]
    if left:                            # (the report is written AFTER)
        fails.append("kern_mega defrag: still in pieces %r" % left)
    for p, s in sums.items():
        if hashlib.sha1(v.read(p)).hexdigest() != s:
            fails.append("kern_mega defrag: %s's bytes changed" % p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", type=int, choices=(1, 2))
    a = ap.parse_args()
    fails = []
    work = tempfile.mkdtemp(prefix="fsckmega-")
    try:
        if a.leg in (None, 1):
            leg1(os.path.join(work, "1"), fails) if os.makedirs(
                os.path.join(work, "1")) is None else None
        if a.leg in (None, 2):
            leg2(os.path.join(work, "2"), fails) if os.makedirs(
                os.path.join(work, "2")) is None else None
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("fsckmega: repaired and defragmented under kern_mega's protected "
          "mode, no fault, every result held to an independent FAT reader")
    return 0


if __name__ == "__main__":
    sys.exit(main())
