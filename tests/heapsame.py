#!/usr/bin/env python3
"""Compaction must change NOTHING that ships (SPEC.md 66.2).

    make HEAPCOMPACT=0 && python3 tests/heapsame.py build/hs-off.bin --off
    make               && python3 tests/heapsame.py build/hs-on.bin
    cmp build/hs-off.bin build/hs-on.bin

The claim this proves is the one the whole staged design rests on: the default
is PINNED, nothing shipped declares a relocation proc, so `mem_can_move`
refuses every record, `mem_cp_plan` counts no movers and `mem_compact` returns
without raising [sch_lock] or copying a byte. If that is true, a kernel WITH
the compactor and a kernel built HEAPCOMPACT=0 must put identical pixels on
the glass through an identical session - and if it is not true, the difference
shows up as somebody else's window drawn from memory that moved underneath it.

A screenshot at the end would not do: the interesting divergence is transient
(a window drawn from a stale base, repainted correctly a moment later), so
this captures the rendered framebuffer at EVERY step and concatenates them.
It captures the claim map at every step too, for the same reason - the map is
where a spurious move would show first, and two identical screens over two
different maps is exactly the state a later boot turns into a wrong pixel.
"""
import sys, time, argparse, hashlib
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

MC_SIZE, MEM_MAX = 10, 32


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--off", action="store_true",
                    help="the kernel was built HEAPCOMPACT=0")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    defines = ("NOCOMPACT",) if a.off else ()

    def S(name):
        return os88sym.linear(name, defines)

    blob = bytearray()

    def step(m, what):
        w, h, rows = m.vram("cga")
        # THE MENU BAR IS EXCLUDED, and it has to be: it carries a CLOCK
        # (SPEC.md 12.9) that advances with wall time, so two runs minutes
        # apart differ on rows 6..11 for reasons that have nothing to do with
        # the kernel under test. Measured, that is the ONLY thing that ever
        # differed here - every pixel below the bar and every byte of the claim
        # map agreed - which is exactly the sort of near-miss that turns a
        # byte-identity claim into a shrug if it is not named.
        px = bytes(b for r in rows[os88geom.MBAR_H:] for b in r)
        # the map WITHOUT MC_RLOC: that word is the one thing the two kernels
        # may legitimately differ in, since only one of them has a compactor
        # to read it. Everything else about a claim must match.
        raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
        mp = b"".join(raw[i * MC_SIZE:i * MC_SIZE + 8] for i in range(MEM_MAX))
        blob.extend(px)
        blob.extend(mp)
        print("  %-22s %dx%d  fb %s  map %s" %
              (what, w, h, hashlib.md5(px).hexdigest()[:12],
               hashlib.md5(mp).hexdigest()[:12]))

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        step(m, "desktop")

        dispcp.open_drive(m, mo := os88mouse.Mouse(marty=m), S,
                          os88marty.settle, "B")
        step(m, "disk window")

        w = dispcp.win_list(m, S)
        wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 0)   # APPS
        step(m, "APPS folder")

        w = dispcp.win_list(m, S)
        wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 1)
        time.sleep(3)
        os88marty.settle(m)
        step(m, "a package running")

        # ...and a menu, which is the kernel's own 20KB save-under claim
        # taken and released inside one operation (SPEC.md 50.2)
        mo.menu(12, 8, 40, 45)
        os88marty.settle(m)
        step(m, "after a menu")

    open(a.out, "wb").write(bytes(blob))
    print("%s: %d bytes, md5 %s"
          % (a.out, len(blob), hashlib.md5(bytes(blob)).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
