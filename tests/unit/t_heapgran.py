#!/usr/bin/env python3
"""The heap's granule, and the two places that have to agree with it (SPEC.md
50.2.1, 51.1.2).

    python3 tests/unit/t_heapgran.py

The heap claims every image - a driver's, a module's - to a 512-byte rung,
from the size the DIRECTORY reports, before it has read the header that says
how much bss the image needs. That is only safe because the image's FILE
already covers its bss to that rung: tools/os88drv.py strips a driver's
trailing zeros only as far as the rung, and a module carries its bss as zeros
in its file. So three numbers have to be the same number, and they live in
three files edited by different people:

  * kernel/memory.inc's mem_bytes_gp_x - the rung an image is claimed to;
  * tools/os88drv.py's DRV_RUNG - the rung a driver is stripped to;
  * every BUILT driver: image + bss <= its file rounded up to that rung.

The third is the one that matters and the only one a person cannot see by
reading: drv_bss refuses a driver whose bss runs past its claim (.nofit), so a
strip rung coarser than the claim's is a driver that will not load - and it
would not load on the machine, not in the build.

Break it on purpose: set DRV_RUNG back to 1024 and rebuild; every driver whose
stripped file ends in the first half of a KB fails the third check here.

The granule itself is guarded where it is defined: guard 6b in kernel.asm
refuses a granule finer than 512 bytes without the DISK class.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, eq, done                        # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88drv                                            # noqa: E402

BUILD = os.path.join(ROOT, "build")


def main():
    mem = open(os.path.join(ROOT, "kernel/memory.inc"), errors="replace").read()
    m = re.search(r"^mem_bytes_gp_x:\s*\n\s*add ax, (\d+)\s*\n\s*and ax, -(\d+)",
                  mem, re.M)
    rung = int(m.group(2)) if m else None
    eq(m and int(m.group(1)) + 1 == rung, True,
       "mem_bytes_gp_x rounds UP to one rung (add rung-1, and -rung)",
       "the image claim's rounding is two constants that must agree - an "
       "`add` one short of the mask rounds a size already on the rung up a "
       "whole rung, and one long of it rounds some sizes down")
    eq(os88drv.DRV_RUNG, rung, "os88drv.py strips to the rung the kernel claims to",
       "tools/os88drv.py's DRV_RUNG and kernel/memory.inc's mem_bytes_gp_x are "
       "the same number written twice: a coarser strip rung leaves a driver's "
       "claim short of its bss, and drv_bss refuses to load it (.nofit)")

    seen = 0
    for name in sorted(os.listdir(BUILD)):
        if not name.endswith(".drv"):
            continue
        blob = os88drv.image_unwrap(open(os.path.join(BUILD, name), "rb").read())
        if blob[2] != os88drv.DRV_VER:      # a kernel MODULE (SPEC.md 2.8) is
            continue                        # a .DRV with its own header and
                                            # its bss as zeros in the file
        img = struct.unpack_from("<H", blob, 8)[0]
        need = img + blob[31] * 16          # image + bss, what drv_bss checks
        claim = -(-len(blob) // rung) * rung if rung else 0
        seen += 1
        check(need <= claim,
              "%s: image + bss fits its claim (%d <= %d)" % (name, need, claim),
              "the kernel claims the file rounded up to %d bytes and only then "
              "reads the header's bss; this driver's bss runs past that, so "
              "drv_bss refuses it at load (.nofit) - the strip took it below "
              "its own rung (SPEC.md 51.1.2)" % (rung or 0),
              got=need, want="<= %d" % claim)
    check(seen > 0, "the drivers are built",
          "run `make` first - this row reads build/*.drv, and with none there "
          "it would pass by checking nothing")
    print("t_heapgran: rung %s, %d drivers" % (rung, seen))
    done("t_heapgran")


if __name__ == "__main__":
    main()
