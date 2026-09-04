#!/usr/bin/env python3
"""Does the 1bpp PATTERNED fill lay the tile down where it says it does?
(SPEC.md 5, 32/39.5 - `sw_fill_pat` and `sw_patcol`)

    make && python3 tests/fillpat.py

`gfx_fill_pat` on a 1bpp adapter is `sw_fill_pat`: two masked edge COLUMNS
through `sw_patcol` and a whole-byte interior through `rep stosw`, with the
tile row picked by `(y & 7)` and stepped down the rect. It is the Task
Manager's memory map, files.inc's chevron band under a Disk listing that
overflows, and the `OSAPI_GFX_FILL_PAT` slot.

**NOTHING IN AN ORDINARY SESSION REACHES IT.** A Disk window whose listing
fits draws no chevrons; the Task Manager has to be open. So a boot-and-look
harness is a null test that reads exactly like a pass, and this one calls the
primitive through the debugger instead - tests/icoclip.py's shape, for the
same reason - over rows it has zeroed itself, at four rect shapes chosen so
that every arm of the body runs:

    a rect on whole byte columns          - the interior alone
    a rect with BOTH edges masked         - both sw_patcol calls
    a rect one byte wide                  - the degenerate case, where
                                            gfx_rect_setup folds the two
                                            masks together and there is no
                                            interior at all
    a full-width rect                     - the widest `rep stosw` and the
                                            odd-tail carry

THE ASSERTION IS THE TILE'S OWN ARITHMETIC, checked against the kernel's own
staged copy rather than against a golden image: over a zeroed band the result
is `pattern & mask`, so an interior byte of row `i` must be
`gfx_patbuf[(y1 + i) & 7]` exactly, an edge column must be that byte masked
by `[gfx_lmsk]` / `[gfx_rmsk]`, and every byte outside the rect must still be
zero. `gfx_patbuf`, the two masks and the row span are all read back out of
the guest after the call, so the only thing this file supplies is the
inequality.

BOTH 1bpp ADAPTERS: CGA is 80 bytes a row and Hercules 90, and the row walk
is `gfx_nextrow`, whose bank wrap differs between them.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

KERNEL_SEG = 0x60
KB = KERNEL_SEG << 4

MACHINES = (("cga", "os8088_5150_cga", 0xB800),
            ("herc", "os8088_5150_herc", 0xB000))

Y0 = 64                         # a blank band of desktop, clear of the menu
NROW = 20                       # bar and of the volume zone's icons


class Caller(object):
    """Call a near kernel routine on a PAUSED machine - icoclip.py's, and the
    reasoning about `park` versus `setreg("ip")` is written out there."""

    def __init__(self, m, trap="icon_draw_x"):
        self.m = m
        self.trap = os88sym.linear(trap) - KB
        r = m.regs()
        self.ss, self.sp = r["ss"], (r["sp"] - 64) & 0xFFFF
        m.bp_exec(os88sym.linear(trap))

    def call(self, name, **regs):
        m = self.m
        m.cmd(cmd="park", cs=KERNEL_SEG, ip=os88sym.linear(name) - KB)
        sp = (self.sp - 2) & 0xFFFF
        m.write((self.ss << 4) + sp, bytes((self.trap & 0xFF, self.trap >> 8)))
        m.setreg("ss", self.ss)
        m.setreg("sp", sp)
        m.setreg("ds", KERNEL_SEG)
        m.setreg("es", KERNEL_SEG)
        for r, v in regs.items():
            m.setreg(r, v & 0xFFFF)
        m.run()
        if m.wait_stop(30.0) is None:
            raise SystemExit("fillpat: %s never returned" % name)
        return m.regs()


def u16(m, name):
    b = m.read(os88sym.linear(name), 2)
    return b[0] | (b[1] << 8)


def run(tag, machine, fbseg, verbose):
    bad = []
    with os88marty.launch(os.path.join(ROOT, "build", "os8088-360.img"),
                          apps=os.path.join(ROOT, "build", "apps360.img"),
                          machine=machine, label="fillpat") as m:
        m.pause()
        if not m.read(os88sym.linear("vid_mono"), 1)[0]:
            raise SystemExit("fillpat: %s came up NOT mono" % tag)
        stride = u16(m, "vid_stride")
        c = Caller(m)
        rows = [c.call("gfx_rowbase", ax=y)["ax"] for y in range(Y0, Y0 + NROW)]
        fb = fbseg << 4

        # files.inc's own chevron tile: eight real bytes with both a set and a
        # clear column, so a mask that is wrong in either direction shows.
        tile = os88sym.linear("fm_chevtab") - KB
        m.write(os88sym.linear("gfx_pat"), bytes((tile & 0xFF, tile >> 8)))

        last = 8 * (stride - 1) + 7
        for x1, y1, x2, y2 in ((16, Y0, 79, Y0 + 15),
                               (13, Y0, 74, Y0 + 15),
                               (17, Y0 + 2, 22, Y0 + 9),
                               (0, Y0, last, Y0 + 11)):
            for rb in rows:
                m.write(fb + rb, bytes(stride))
            c.call("gfx_fill_pat", ax=x1, bx=y1, cx=x2, dx=y2)
            got = [m.read(fb + rb, stride) for rb in rows]

            pat = m.read(os88sym.linear("gfx_patbuf"), 8)
            lm = m.read(os88sym.linear("gfx_lmsk"), 1)[0]
            rm = m.read(os88sym.linear("gfx_rmsk"), 1)[0]
            span = u16(m, "gfx_rspan")
            lcol, rcol = x1 >> 3, x2 >> 3
            ink = 0
            for i in range(NROW):
                y = Y0 + i
                for col in range(stride):
                    want = 0
                    if y1 <= y <= y2 and lcol <= col <= rcol:
                        b = pat[(y1 + (y - y1)) & 7]
                        if col == lcol:
                            want = b & lm
                        elif col == rcol and span:
                            want = b & rm
                        else:
                            want = b
                    ink += 1 if want else 0
                    if got[i][col] != want:
                        bad.append(
                            "%s rect (%d,%d)-(%d,%d): row %d column %d is "
                            "0x%02X, the tile says 0x%02X"
                            % (tag, x1, y1, x2, y2, y, col, got[i][col], want))
                        break
                if len(bad) > 20:
                    break
            # The positive control: a rect that lands NOTHING passes every
            # assertion above, and so would a harness that never called. Eight
            # is the ONE-BYTE-WIDE rect's own figure - one column, eight rows -
            # and it is the smallest of the four on purpose.
            if ink < 8:
                bad.append("%s rect (%d,%d)-(%d,%d): only %d bytes are "
                           "expected to be inked - this rect proves nothing"
                           % (tag, x1, y1, x2, y2, ink))
            if verbose:
                print("  %s (%d,%d)-(%d,%d)  cols %d..%d  lm %02X rm %02X  "
                      "span %d  %d inked  ok" % (tag, x1, y1, x2, y2, lcol,
                                                 rcol, lm, rm, span, ink))
        print("%s: stride %d, 4 rects, %d findings" % (tag, stride, len(bad)))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--machine", help="one adapter only (cga | herc)")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    os88sym.default_defines()
    bad = []
    for tag, machine, seg in MACHINES:
        if a.machine and a.machine != tag:
            continue
        bad += run(tag, machine, seg, a.verbose)
    if bad:
        print("\nfillpat: FAIL")
        for b in bad[:30]:
            print("  " + b)
        return 1
    print("\nfillpat: ok - the tile lands where (y & 7) says, masked at both "
          "edges, on both strides")
    return 0


if __name__ == "__main__":
    sys.exit(main())
