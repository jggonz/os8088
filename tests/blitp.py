#!/usr/bin/env python3
"""DOES SPEC.md 5.4.3's gfx_blitp PUT THE BYTES WHERE THEY WERE GIVEN?

    make && make bench && python3 tests/blitp.py

gfxbench's GFX_BLITP rows hand the primitive 2,048 bytes of known pattern -
four planes of alternating AA/55 - so every plane of every row on screen has
one right answer and this compares against it. The block is caught at the
primitive's RETURN, by a breakpoint on the address it was called from, so
nothing has drawn over it yet.

**IT READS THE PLANES AND NOT `fbuf`, AND THAT IS THE WHOLE METHOD.** The
card's RENDERED frame is only as current as the raster: stop the machine
mid-frame and everything below the beam is last frame's, which reads exactly
like a blit that stopped halfway - one good row, then rows of the colours
that were there before, at a different row every run. That cost most of a
session. The planes are memory and are always current, so this drives Read
Map Select (GC4) itself and reads A000: through the debugger.

WHAT IT WOULD HAVE CAUGHT, all three found this way: a row loop whose
registers the emitter clobbers (470 rows of stripes instead of 64), a `pop cx`
destroying the shift count a `mov cl, 3` had just set (every row one byte too
wide, reading into the next), and no `cld` in a routine built out of `lodsb`,
`stosb` and `rep movsb`.
"""
import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty, os88mouse, os88sym, dispcp                 # noqa: E402

S = os88sym.linear
KBASE = os88sym.KERNEL_SEG << 4
VGA_BASE = 0xA0000


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="/tmp/blitp.img")
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()

    if a.apps == "/tmp/blitp.img" and not os.path.exists(a.apps):
        subprocess.check_call(
            [sys.executable, "tools/os88disk.py", "-o", a.apps, "--size",
             "360", "APPS:build/gfxbench.o88"])

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, bx, by, "APPS")
        os88marty.settle(m)
        bx, by = dispcp.win_rect(m, S, disk)[:2]
        rx, ry = dispcp.row_xy(bx, by,
                               dispcp.scroll_to(m, mo, S, os88marty.settle,
                                                bx, by,
                                                dispcp.row_of(m, S,
                                                              "GFXBENCH.O88")))
        mo.dblclick(rx, ry)
        t0 = time.time()
        while time.time() - t0 < 120:
            if [w for w in dispcp.win_list(m, S) if w != disk]:
                break
            time.sleep(0.3)
        os88marty.settle(m)

        m.bp_exec("gfx_blitp")
        m.key("KeyR")
        if not m.wait_stop(limit=300.0):
            sys.exit("blitp: gfxbench never called gfx_blitp")
        r = m.regs()
        x, y, w, h = r["ax"], r["bx"], r["cx"], r["dx"]
        pstep, rstride = r["di"], r["bp"]
        print("   blit x=%d y=%d w=%d h=%d planestep=%d rowstride=%d"
              % (x, y, w, h, pstep, rstride))
        if x % 8:
            sys.exit("blitp: the bench handed it an unaligned x - it will "
                     "have REFUSED, and this row would prove nothing")
        src = m.read((r["es"] << 4) + r["si"], pstep * 4)
        ret = u16(m.read((r["ss"] << 4) + r["sp"], 2))
        m.bp_exec(KBASE + ret)
        m.run()
        if not m.wait_stop(limit=600.0):
            sys.exit("blitp: gfx_blitp never returned")
        if m.regs()["flags"] & 1:
            sys.exit("blitp: REFUSED (CF=1) - nothing was drawn, so there is "
                     "nothing here to check")
        stride = u16(m.read(S("vid_stride"), 2))
        m.bp_exec()

        # ...and the comparison stays INSIDE the machine's lifetime: the
        # planes are read through the debugger, so there is nothing to check
        # once it is gone.
        nb = w // 8
        bad = []
        for pl in range(4):
            m.outb(0x3CE, 4)                # GC4: Read Map Select = this plane
            m.outb(0x3CF, pl)
            for row in range(h):
                o = pl * pstep + row * rstride
                want = bytes(src[o:o + nb])
                got = m.read(VGA_BASE + (y + row) * stride + (x >> 3), nb)
                if got != want:
                    bad.append((pl, row, want.hex(), got.hex()))
        m.outb(0x3CE, 4)                    # ...and the card as we found it
        m.outb(0x3CF, 0)
    if bad:
        print("   %d plane-rows differ of %d" % (len(bad), 4 * h))
        for pl, row, wnt, got in bad[:6]:
            print("      plane %d row %3d: want %s got %s" % (pl, row, wnt, got))
        sys.exit("blitp: FAILED")
    print("blitp: %d plane-rows, %d bytes, all as given"
          % (4 * h, 4 * h * nb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
