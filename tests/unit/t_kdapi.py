#!/usr/bin/env python3
"""No `OSAPI_*` far call may survive into a `kern_dos` image.

    python3 tests/unit/t_kdapi.py

`KERNEL_SEG` is kern_dos's OWN segment (`kerndos/kdlayout.inc`), so every
`call OSAPI_X` that reaches that image is a far call to `KD_SEG:0xNNNN` - a
jump into the middle of the disk layer, with the caller's registers, and no
way to tell afterwards where it went.  `apps/dos/dos.asm` is included whole
and has ~96 of them; wave 2 measured the LOAD path at 21 procs reaching none,
and the RUN path then reached one - `dos_getkey` polls `dos_mou_read`, which
is every DOS program that waits for a keystroke.  It presented as a machine
spinning in the ROM with a key already in the BIOS ring, and it took a day.

**THIS USED TO CHECK A WALL AND NOW IT CHECKS THE CALLS** (SPEC.md 96.44.6).
The wall was 179 `stc`/`retf` cells at every published offset, so a survivor
refused instead of jumping - 1,432 bytes of the image, and it is what held
`CORE_ORG` at 0x0600 in BOTH hosts.  Scanning the assembled images found the
wall was catching **six sites in four cells**: `dos_keeph`'s two, a window
routine mis-marked core, and `dosh.inc`'s four, which reach a heap only the
windowed host has.  The first moved into the window half and the other four
became `DHK_CLAIM`/`DHK_FREE`, so there is nothing left to catch.

What replaces it is strictly better.  A wall turns a wild jump into a wrong
ANSWER at runtime, on a machine that has no operating system left to report
it; this fails the BUILD, at the wave that writes the call, naming the cell.

HOW.  Disassembly is not needed and would not be safe: `9A` is also a data
byte.  What makes the scan sound in the other direction is that it only ever
reports MORE than the truth - a false hit is a wrong refusal somebody reads
and dismisses, where a missed one is the failure this exists for.  So every
`9A ll hh ss ss` whose segment word is `KD_SEG` and whose offset is a
published cell counts, and the message names the slot.

IT NEEDS THE BUILT IMAGES (`make kdostest`), because the question is about
what nasm emitted and not about what the source says.  A tree without them
SKIPS rather than passes - `tests/suite.py` carries that in `wants=`.
"""
import collections
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88geom import KD_SEG                                # noqa: E402

SDK = os.path.join(ROOT, "apps", "os88api.inc")
IMAGES = ("build/kerndos.bin", "build/doscore.bin")


def fail(msg):
    print("t_kdapi: FAIL: %s" % msg)
    sys.exit(1)


def cells():
    """{offset: name} for every published API cell."""
    out = {}
    for ln in open(SDK):
        m = re.match(r"\s*%define\s+(OSAPI_\w+)\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)",
                     ln)
        if m:
            out.setdefault(int(m.group(2), 16), m.group(1))
    return out


def scan(path, api):
    """Every `call far KD_SEG:<published cell>` in the image."""
    data = open(path, "rb").read()
    hits = collections.Counter()
    for i in range(len(data) - 4):
        if data[i] != 0x9A:                 # call far ptr16:16
            continue
        off, seg = struct.unpack_from("<HH", data, i + 1)
        if seg == KD_SEG and off in api:
            hits[off] += 1
    return hits, len(data)


def main():
    api = cells()
    if len(api) < 100:
        fail("only %d OSAPI_* cells found in apps/os88api.inc - the regex no "
             "longer matches how they are spelled, so this test is checking "
             "nothing" % len(api))

    missing = [p for p in IMAGES
               if not os.path.exists(os.path.join(ROOT, p))]
    if missing:
        print("t_kdapi: SKIP - %s not built (`make kdostest`)"
              % ", ".join(missing))
        return

    total = 0
    for rel in IMAGES:
        hits, size = scan(os.path.join(ROOT, rel), api)
        if hits:
            lines = ["   0x%04X  x%-3d %s" % (o, n, api[o])
                     for o, n in sorted(hits.items())]
            fail("%s makes %d far call(s) to KERNEL_SEG, which under this "
                 "root is kern_dos's own segment:\n%s\n"
                 "That is a jump into kern_dos's code with the caller's "
                 "registers. Either the caller belongs in the WINDOW half "
                 "(`%%ifndef KD_BACKEND`), or the thing it wants is a HOST "
                 "HOOK - `dos_hkv`, SPEC.md 96.44.3 - which is what "
                 "`dos_keeph` and `dosh.inc`'s heap pair each turned out to "
                 "be (96.44.6)."
                 % (rel, sum(hits.values()), "\n".join(lines)))
        total += size

    print("t_kdapi: ok - %d published cells, 0 far calls in %d bytes of image"
          % (len(api), total))


if __name__ == "__main__":
    main()
