#!/usr/bin/env python3
"""The heap's granule and its paragraph door, on the machine (SPEC.md 50.2.1,
50.3.7).

    make build/memp360.img && python3 tests/memp.py

Launches tests/memp (MEMP.O88, the only file on its own 360KB disk) and reads
its eleven verdicts out of its bss: OSAPI_MEM_P's four verbs, the granule it
reports, alignment and the DMA head, the KB door at 4KB and 16KB, and the heap
the same size again once everything is given back. The package says what each
check is; this prints what it granted alongside.

Break it on purpose: put `and al, -MEM_GRAN` back in kernel/memory.inc's
mem_claim_x.gran (the defect this branch shipped for one commit) and checks 8
and 9 go red - a claim of 4KB is 256 paragraphs, whose low byte is zero. Seen
red that way: build the broken kernel into a tree of its own and point the
row at it, with the broken source still in place (os88sym re-assembles it):

    make BUILD=/tmp/red /tmp/red/os8088-360.img
    OS88_BUILD=/tmp/red MEMP_SYS=/tmp/red/os8088-360.img python3 tests/memp.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
os.chdir(ROOT)
import os88geom                                              # noqa: E402
import os88marty as M                                        # noqa: E402
import os88ui                                                # noqa: E402
from os88fixture import need                                 # noqa: E402

LABELS = {
    1: "MEMP_AVAIL reports the granule (CX = 32) and a run",
    2: "one paragraph is granted a whole granule, on a granule",
    3: "33 paragraphs are granted two granules",
    4: "MEMP_CLAIM_HI: 300 -> 320, aligned",
    5: "MEMP_REGROW to 100 answers 128 granted",
    6: "a claim of nothing is refused",
    7: "a 288-paragraph DMA head stays in one 64KB page",
    8: "the KB door claims 4KB (256 paragraphs, low byte 0)",
    9: "the KB door claims 16KB",
    10: "MEMP_CPU is accepted, granted to the granule",
    11: "everything given back, the largest run is what it was",
}


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def main():
    need("build/memp360.img")
    fails = 0
    with os88ui.boot(os.environ.get("MEMP_SYS", "build/os8088-360.img"), apps="build/memp360.img") as ui:
        m = ui.m
        ui.path("B:/MEMP.O88")
        S = m.sym

        def bss():
            for w in os88geom.windows(m, S):
                if (w.title or "") == "MEMP":
                    seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                     + os88geom.W_SEG, 2))
                    lin = (m.lin if getattr(m, "bochs", False)   # a selector
                           else lambda sg, o: sg * 16 + o)      # on kern_mega
                    img = u16(m.read(lin(seg, 8), 2))
                    return bytes(m.read(lin(seg, img), 40))
            return None
        got = {}

        def ready(_):
            b = bss()
            if b and u16(b) == 0x504D:                 # 'MP'
                got["b"] = b
                return True
            return False
        M.until(m, ready, "MEMP's suite to finish", limit=60.0, guest=5.0)
        b = got["b"]
        print("granule %d paragraphs; largest run %d -> %d paragraphs; "
              "granted 1->%d, 33->%d, hi 300->%d at %04X, regrow 100->%d, "
              "cpu 40->%d"
              % (u16(b, 16), u16(b, 18), u16(b, 20), u16(b, 22), u16(b, 24),
                 u16(b, 26), u16(b, 34), u16(b, 28), u16(b, 30)))
        for n in sorted(LABELS):
            v = b[2 + n]
            print("%s %2d  %s" % ({1: "ok  ", 2: "FAIL"}.get(v, "NOT RUN"),
                                  n, LABELS[n]))
            fails += v != 1
    print("memp: %s" % ("ok" if not fails else "%d FAILED" % fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
