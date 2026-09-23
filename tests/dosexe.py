#!/usr/bin/env python3
"""The DOS wave-2 gate: a real MZ .EXE (SPEC.md 96.8).

tests/doscom.py proves the bracket and the .COM path. This proves the parts
only an .EXE has, and each line it checks is one of them:

  Relocated pointer reads: RELOC-OK
        the loader applied the relocation table. Left alone that word is a
        raw paragraph offset, so the program's DS lands in the interrupt
        vector table and the eight bytes read back are whatever is at
        0000:0000 - which is loud rather than subtle, and is the point.

  Header SS:SP was 0040:0400
        the stack came from the HEADER and not from the PSP, which is the
        whole difference between an .EXE entry and a .COM one.

  AH=4Ah shrink ... AH=48h ... granted and writable
        the MCB chain is a real allocator (SPEC.md 96.9). Every compiled
        program does exactly this pair at startup, and a stub that always
        refuses leaves malloc returning NULL for the life of the program.

  Largest free block: N KB
        ...and the refusal path answers a TRUTHFUL figure, which is what a
        BX=FFFFh probe is asking for.

VERIFIED TO FAIL. dos_movedown built its paragraphs-to-words shift with
`mov cl, 3 / shl cx, cl` - loading CL destroys the low byte of the count
being shifted, so 64 paragraphs became 3 and 48 bytes of a 1KB image moved.
The screen then shows neither an error nor the program: just the machine's
own memory rendered as text. This row goes red on it at the first line.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
EXE = "build/dosexe360.img"


def fail(msg):
    print("dosexe: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, EXE):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)

    with os88ui.boot(SYS, apps=EXE) as ui:
        m = ui.m
        if not ui.path("B:/DOSHELLO.EXE"):
            fail("double-clicking DOSHELLO.EXE opened no window")

        rows = []
        for _ in range(80):
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.2)
        else:
            fail("the .EXE never reached its READY prompt; the last text "
                 "screen was %r" % ([r.rstrip() for r in rows if r.strip()][:6],))

        text = "\n".join(r.rstrip() for r in rows)
        print("dosexe: the bracket's text screen:")
        for r in rows[:12]:
            if r.strip():
                print("   | %s" % r.rstrip())

        if "RELOC-OK" not in text:
            fail("the relocated far pointer did not read back RELOC-OK - the "
                 "relocation table was not applied (SPEC.md 96.8)")
        if "0040:0400" not in text:
            fail("SS:SP is not the header's 0040:0400 - an .EXE's stack comes "
                 "from its header, not from the PSP")
        if "REFUSED - the gate has FAILED" in text:
            fail("AH=4Ah or AH=48h refused: the MCB chain is not allocating "
                 "(SPEC.md 96.9)")
        if "granted and writable" not in text:
            fail("AH=48h never reported a usable block")

        kb = None
        for r in rows:
            if "Largest free block:" in r:
                try:
                    kb = int(r.split(":")[1].strip().split()[0])
                except (IndexError, ValueError):
                    fail("could not read the free-block figure out of %r" % r.rstrip())
        if kb is None or kb < 64:
            fail("the BX=FFFFh probe answered %r KB, which is not a truthful "
                 "largest-free-block on a 640KB machine" % (kb,))
        print("dosexe: the BX=FFFFh probe answers %d KB" % kb)

        m.type_text("x")
        os88marty.settle(m)
        titles = ui.titles()
        if "DOS" not in titles or "Disk" not in titles:
            fail("the desktop did not come back cleanly: %r" % titles)
        print("dosexe: back on the desktop, windows %r" % (titles,))

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/dosexe.png", wd, ht, data)

    print("dosexe: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
