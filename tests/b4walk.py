#!/usr/bin/env python3
"""A COVERED gfx_blit4: every fragment at the fast path's price (SPEC.md 5.4.1.5)

    make build/b4walk.o88 && python3 tests/b4walk.py [--machine M]

tests/b4walk/b4walk.asm is one window whose W_PAINT is ONE OSAPI_GFX_BLIT4 of
a 200 x 112 block, every pixel a colour at random out of eight - about a run
a pixel, which is the shape that paid a whole gfx_fill a run when the block
crossed a fragment edge (13.4 s a heal on a 4.77 MHz 8088, measured with the
Video Player's poster, docs/plans/completed/BLIT4-WALK-PLAN.md 1).

The session: the window opens in front and its first paint is the UNCOVERED
reference - its cycles and its pixels. Then a Disk window is moved over part
of the block and moved again, twice, so each heal's region is the old rect
less the new one: an L, two fragments, which the block crosses. Every outer
gfx_blit4 the heals make is read four ways, each one a thing the walk exists
to get right:

  1. its COST in guest cycles - at most the uncovered blit's, every heal here
     drawing a part of the block. The kernel before the walk reads 23.6x, and
     one before SPEC.md 5.4.1.6 reads 1.67x on the second heal, whose 46-pixel
     piece paid the run writer a pixel;
  2. its GFX_FILL calls - none for a piece that starts on a HIGH nibble, and at
     most one a row for one that starts on a LOW nibble (that column is a
     vline per run of one colour). A cap stops the count on the old path;
  3. every PIECE (a nested gfx_blit4) and every gfx_fill INSIDE one fragment
     of the region the outer call met, and the pieces' area EQUAL to the
     block's intersection with the region - so nothing is drawn over the
     window on top and nothing visible is left undrawn;
  4. the PIXELS: the block on the rendered glass, everywhere in the region,
     equal to the uncovered reference. The fixture CLEARS the block before
     each blit, so a pixel the walk misses shows the clear's colour rather
     than the right one left over from an earlier paint.

Both piece phases have to have been met, or the row fails rather than passes
on half of the routine: the odd column is reached only when a fragment edge
falls an odd number of pixels into the block, and that is the window
manager's geometry and not this script's choice.

Run on os8088_xt_vga (5.4.1.3's planar decoder and the run writer) and on a
Hercules (5.4.1.1's pair decoder) by default; --machine picks one.
"""
import argparse
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                             # noqa: E402
import os88marty                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88ui                                                # noqa: E402

BW, BH = 200, 112           # tests/b4walk/b4walk.asm's BW_W x BW_H
FILL_CAP = 600              # gfx_fill hits counted before the count gives up:
                            # the walk's worst here is two odd columns of 112
                            # rows, and the old path is a run a pixel
T_HEAL = 90.0               # a move's wait: the walk heals in well under a
                            # second, and the kernel before it in ~13 on VGA -
                            # which has to be MEASURED here, not timed out on
MACHINES = ("os8088_xt_vga", "os8088_5150_herc_gla")


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class Heal(object):
    """Every outer gfx_blit4 of the block made while it is armed."""

    def __init__(self, m, S, eq):
        self.S = S
        self.kbase = os88sym.KERNEL_SEG << 4
        self.b4 = S("gfx_blit4")
        self.fill = S("gfx_fill")
        self.tab = S("wm_clip_tab")
        self.ncl = S("wm_clip_n")
        self.wsz = eq["WCR_SZ"]
        self.calls = []
        self.cur = None

    def arm(self, mm, extra=()):
        bps = [{"type": "exec", "addr": self.b4}]
        bps += [{"type": "exec", "addr": a} for a in extra]
        mm.breakpoints(bps)

    def hit(self, mm, rec):
        r = rec["regs"]
        sp = r["sp"]
        flat = rec["addr"]
        if flat == self.b4:
            geom = (s16(r["ax"]), s16(r["bx"]), r["cx"], r["dx"])
            if self.cur is None:
                if geom[2:] != (BW, BH):
                    return None             # some other blit: not ours
                n = u16(mm.read(self.ncl, 2))
                raw = mm.read(self.tab, n * self.wsz)
                frags = [tuple(s16(u16(raw, i * self.wsz + k))
                               for k in (0, 2, 4, 6)) for i in range(n)]
                ret = u16(mm.read((r["ss"] << 4) + sp, 2))
                self.cur = {"geom": geom, "sp0": sp, "c0": rec["cycles"],
                            "frags": frags, "pieces": [], "fills": [],
                            "nfill": 0, "ret": self.kbase + ret}
                self.arm(mm, (self.fill, self.cur["ret"]))
            elif sp < self.cur["sp0"]:
                self.cur["pieces"].append(geom)
            return None
        if self.cur is None:
            return None
        if flat == self.fill:
            c = self.cur
            c["nfill"] += 1
            c["fills"].append((s16(r["ax"]), s16(r["bx"]),
                               s16(r["cx"]), s16(r["dx"])))
            if c["nfill"] >= FILL_CAP:
                self.arm(mm, (c["ret"],))   # stop counting: the old path
            return None
        if flat == self.cur["ret"] and sp == self.cur["sp0"] + 2:
            self.cur["cyc"] = rec["cycles"] - self.cur["c0"]
            self.calls.append(self.cur)
            self.cur = None
            self.arm(mm)
        return None


def inside(r, frags):
    """Is the inclusive rect r inside ONE fragment?"""
    x1, y1, x2, y2 = r
    return any(f[0] <= x1 and x2 <= f[2] and f[1] <= y1 and y2 <= f[3]
               for f in frags)


def expected(g, frags):
    """The block cut to each fragment: the pieces a walk owes, as x, y, w, h."""
    x, y, w, h = g
    out = []
    for f in frags:
        x1, y1 = max(x, f[0]), max(y, f[1])
        x2, y2 = min(x + w - 1, f[2]), min(y + h - 1, f[3])
        if x2 >= x1 and y2 >= y1:
            out.append((x1, y1, x2 - x1 + 1, y2 - y1 + 1))
    return out


def screen(m):
    """The screen as pixel(x, y), in SCREEN coordinates.

    A 1bpp card is read out of its framebuffer memory (`vram`), because the
    RENDERED Hercules raster carries the overscan and is about fourteen pixels
    to the right of the screen's own x - a comparison there checks pixels the
    block never had. VGA has no flat framebuffer, and its 640 x 480 raster is
    the screen one for one.
    """
    if m.cmd(cmd="video")["type"] in ("cga", "mda", "hercules"):
        rows = m.vram()[2]
        return lambda x, y: rows[y][x]
    w, h, buf = m.fbuf(card=0)
    if (w, h) != (640, 480):
        sys.exit("b4walk: a %dx%d VGA raster is not the screen one for one"
                 % (w, h))
    return lambda x, y: buf[(y * w + x) * 3:(y * w + x) * 3 + 3]


def block_diff(ref, now, g, frags):
    """Pixels of the block INSIDE the region that differ from the reference.

    The region and not the Disk window's record: a frame's shadow is a pixel
    past W_W, and a margin generous enough to be safe swallowed exactly the
    odd column this row exists to check (found by breaking it on purpose).
    """
    x, y, bw, bh = g
    n = 0
    for row in range(y, y + bh):
        for col in range(x, x + bw):
            if inside((col, row, col, row), frags) and \
                    ref(col, row) != now(col, row):
                n += 1
    return n


def run(machine, image, apps):
    os88build.plain().apply()
    S = os88sym.linear
    eq = os88sym.equates()
    bad = []
    with os88ui.boot(image, apps=apps, machine=machine) as ui:
        m = ui.m
        disk = ui.open_drive("B")
        heal = Heal(m, S, eq)
        with os88marty.bp_trace(m, heal.b4, regs=True,
                                on_hit=heal.hit) as tr:
            ui.path("B:/APPS/B4WALK.O88")
            tr.until(lambda: heal.calls, "the first paint's gfx_blit4",
                     limit=60.0)
        ref = heal.calls[0]
        g = ref["geom"]
        bx, by = g[0], g[1]
        print("   %s: block at (%d,%d), uncovered blit %d cycles, %d "
              "piece(s), %d gfx_fill" % (machine, bx, by, ref["cyc"],
                                          len(ref["pieces"]), ref["nfill"]))
        park = (bx + 20, 40)        # the desktop above both windows
        ui.mo.to(*park)
        ui.settle()
        fref = screen(m)

        di = disk.i                 # by SLOT: its title is the folder's now
        byslot = lambda: [w for w in ui.windows() if w.i == di][0]
        disk = byslot()
        phases = set()
        # A, then B, then C, the Disk window to the block's LEFT and its right
        # edge inside it: the region is the block's window less the Disk
        # window, two fragments, one starting at the window's own left (the
        # block's x, the even phase) and one a pixel past the Disk window's
        # right edge, which a frame's geometry puts an odd number of pixels
        # into the block (the odd phase)
        dw = disk.w
        stops = [(bx + 72 - dw, by + 20), (bx + 112 - dw, by + 44),
                 (bx + 152 - dw, by + 60)]
        ui.move_window(disk, *stops[0], limit=T_HEAL)
        for dest in stops[1:]:
            heal.calls = []
            with os88marty.bp_trace(m, heal.b4, regs=True,
                                    on_hit=heal.hit) as tr:
                ui.move_window(byslot(), *dest, limit=T_HEAL)
                tr.until(lambda: heal.calls and heal.cur is None,
                         "the heal's gfx_blit4", limit=60.0)
                ui.settle()
            ui.mo.to(*park)
            ui.settle()
            for c in heal.calls:
                want = expected(c["geom"], c["frags"])
                ph = sorted(set((p[0] - c["geom"][0]) & 1 for p in want))
                phases.update(ph)
                odd_rows = sum(p[3] for p in want
                               if (p[0] - c["geom"][0]) & 1)
                area = sum(p[2] * p[3] for p in c["pieces"]) + \
                    sum((f[2] - f[0] + 1) * (f[3] - f[1] + 1)
                        for f in c["fills"])
                print("   heal to %r: %d cycles (%.2fx uncovered), %d "
                      "fragment(s), %d piece(s) at phase %s, %d gfx_fill%s"
                      % (dest, c["cyc"], c["cyc"] / float(ref["cyc"]),
                         len(c["frags"]), len(want), ph, c["nfill"],
                         "+" if c["nfill"] >= FILL_CAP else ""))
                if c["cyc"] > ref["cyc"]:
                    bad.append("a heal took %d cycles, more than the whole "
                               "uncovered blit's %d" % (c["cyc"], ref["cyc"]))
                if c["nfill"] > odd_rows:
                    bad.append("%d gfx_fill calls where the odd columns allow "
                               "%d" % (c["nfill"], odd_rows))
                for p in c["pieces"]:
                    r = (p[0], p[1], p[0] + p[2] - 1, p[1] + p[3] - 1)
                    if p[2] and not inside(r, c["frags"]):
                        bad.append("piece %r is outside every fragment %r"
                                   % (p, c["frags"]))
                for f in c["fills"]:
                    if f[0] != f[2] or not inside(f, c["frags"]):
                        bad.append("gfx_fill %r is not one column inside a "
                                   "fragment" % (f,))
                        break
                full = sum(p[2] * p[3] for p in want)
                if c["nfill"] < FILL_CAP and area != full:
                    bad.append("the pieces and columns cover %d pixels of the "
                               "%d the region shows" % (area, full))
            n = block_diff(fref, screen(m), g, heal.calls[-1]["frags"])
            print("   heal to %r: %d differing pixels in the region"
                  % (dest, n))
            if n:
                bad.append("%d pixels of the block differ from the uncovered "
                           "picture after the heal to %r" % (n, dest))
        if phases != {0, 1}:
            bad.append("the heals met piece phase(s) %s only - both nibbles "
                       "must be exercised" % sorted(phases))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=os.path.join(tempfile.gettempdir(),
                                                   "b4walk.img"))
    ap.add_argument("--machine", action="append")
    a = ap.parse_args()
    os88marty.scratch_disk(a.apps, "APPS:build/b4walk.o88")
    bad = []
    for mach in a.machine or MACHINES:
        bad += ["%s: %s" % (mach, b) for b in run(mach, a.image, a.apps)]
    for b in bad:
        print("b4walk: " + b)
    if bad:
        sys.exit("b4walk: FAILED")
    print("b4walk: every heal walked, under the uncovered blit, "
          "pixels identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
