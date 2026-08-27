#!/usr/bin/env python3
"""A short read of stage 2's blob HALTS, instead of executing what landed
(SPEC.md 2.9.7).

    make && python3 tests/blobsum.py

SPEC.md 18.93.1's canary verifies the KERNEL's load. The BLOB's load had
nothing, and SPEC.md 2.9.6 took it from four sectors and one `int 13h` to
thirteen and two. What that leaves is not a disk error: stage 2 is in the first
sectors so it runs, the loading screen is in the first sectors so it draws, and
the machine then executes whatever landed in the sectors that did not arrive -
hundreds of instructions later, in a routine with no connection to the disk,
as a fault that differs from boot to boot because uninitialised RAM does.

So this blanks ONE sector in the middle of the blob - sector 5, where
`clk_init` happens to cross - and requires the machine to stop dead rather than
boot. "Stop dead" is read off the BIOS tick at 0040:006C: stage 1's halt is
`cli` / `hlt`, so the tick stops with it, and a machine that is merely slow or
wandering keeps counting.

The positive half is every other row in this suite: they all boot this image.
"""
import os
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

IMG = os.path.join(ROOT, "build", "os8088-360.img")
APPS = os.path.join(ROOT, "build", "apps360.img")
BAD = os.path.join(ROOT, "build", "blobsum-bad.img")
HURT = 5                # the blob sector to lose


def wound():
    """A copy of the boot floppy with one blob sector blanked."""
    shutil.copyfile(IMG, BAD)
    with open(BAD, "rb") as f:
        d = bytearray(f.read())
    bps = struct.unpack_from("<H", d, 11)[0]
    rsvd = struct.unpack_from("<H", d, 14)[0]
    nfat, fatsz = d[16], struct.unpack_from("<H", d, 22)[0]
    rootent = struct.unpack_from("<H", d, 17)[0]
    data = rsvd + nfat * fatsz + (rootent * 32 + bps - 1) // bps
    off = (data + HURT) * bps
    assert any(d[off:off + bps]), "blob sector %d is already blank" % HURT
    d[off:off + bps] = b"\0" * bps
    with open(BAD, "wb") as f:
        f.write(bytes(d))
    return data + HURT


def main():
    lba = wound()
    print("  blanked blob sector %d (LBA %d)" % (HURT, lba))
    lin_entry = os88sym.linear("cold_entry")
    fail = []
    with os88marty.launch(BAD, apps=APPS, machine="os8088_5150_cga_gla",
                          boot=False) as m:
        m.run()
        booted = False
        for _ in range(45):
            time.sleep(1.0)
            if (m.read(lin_entry, 1)[0] == 0xE9
                    and os88marty._Screen(m).field > 0.9):
                booted = True
                break
        a = int.from_bytes(m.read(0x46C, 2), "little")
        time.sleep(2.0)
        b = int.from_bytes(m.read(0x46C, 2), "little")

    print("  desktop=%s ; BIOS ticks %d -> %d" % (booted, a, b))
    if booted:
        fail.append("the machine BOOTED with a blob sector missing: stage 1 "
                    "jumped into it and stage 2 got away with running, which "
                    "is the whole failure SPEC.md 2.9.7 exists to stop")
    if b != a:
        fail.append("the BIOS tick is still advancing (%d -> %d): the machine "
                    "did not reach stage 1's `cli`/`hlt`, so whatever stopped "
                    "it was not the checksum" % (a, b))

    for f in fail:
        print("FAIL: %s" % f)
    print("blobsum: %s" % ("FAILED" if fail else
                           "a blob sector that never arrived halts the boot"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
