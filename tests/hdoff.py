#!/usr/bin/env python3
"""A system disk on which the USER has answered the hard disk OFF.

    import hdoff
    with os88marty.launch(hdoff.image(), ...) as m:

Not a row: a fixture for rows that were written against a machine whose hard
disk driver was NOT loaded at the desktop and that tick it in themselves -
the tick is what they test (hddcp's "the tick is the mount", drvup's mount
count, heaphi's and drvmove's order of driver images on the heap). SPEC.md
51.3.2 loads HDD.DRV by itself on any machine whose BIOS knows a fixed disk,
so on os8088_xt_hdd that starting state is now the one a user reaches by
UNTICKING the row - which is a 'DA' answer (SPEC.md 51.5.4). This writes that
answer into a scratch copy of the system disk, so those rows start exactly
where they always did and their own tick still means what it meant.

The copy is private to the process (a temporary directory, removed at exit),
so nothing under build/ is written and two rows never share one.
"""
import atexit
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
BIT_HDD = 1                             # drv_cfgbit, kern_big

_tmp = []


def cfg_bytes(want=0, answered=1 << BIT_HDD):
    """A minimal SYSTEM.CFG: 'DW' and 'DA' and nothing else (SPEC.md 51.5)."""
    return (b"O88CFG\0\0" + struct.pack("<H", 3)
            + b"DW" + bytes([1, 2]) + struct.pack("<H", want)
            + b"DA" + bytes([1, 2]) + struct.pack("<H", answered)
            + b"\0\0")


def image(src="build/os8088-360.img"):
    if not _tmp:
        _tmp.append(tempfile.mkdtemp(prefix="hdoff-"))
        atexit.register(shutil.rmtree, _tmp[0], True)
    src = src if os.path.isabs(src) else os.path.join(ROOT, src)
    out = os.path.join(_tmp[0], os.path.basename(src))
    shutil.copyfile(src, out)
    cfg = os.path.join(_tmp[0], "SYSTEM.CFG")
    with open(cfg, "wb") as f:
        f.write(cfg_bytes())
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88fat.py"),
                    "add", out, cfg, "SYSTEM.CFG"], check=True,
                   capture_output=True)
    return out


if __name__ == "__main__":
    print(image(*sys.argv[1:]))
