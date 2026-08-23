#!/usr/bin/env python3
"""os88linecost - what a line pixel costs, and what a cheaper one would cost.

    python3 tools/os88linecost.py --model                     # no emulator
    python3 tools/os88linecost.py gfx_line   [--machine ...]
    python3 tools/os88linecost.py candidate  [--machine ...]
    python3 tools/os88linecost.py pieces     [--machine ...]

CYCLE-EXACT, ON A RUNNING os8088, WITH NO CODE ON THE FLOPPY. MartyPC's debug
server reports `cycles` and `instructions` and its `park` command points the
CPU at an address with the prefetch queue flushed (docs/MARTYPC-DEBUG.md), so
this boots a desktop, writes a stub into `gfx_pairtab0` - 256 idle .bss bytes
inside KERNEL_SEG - parks on it and reads the counter either side.  `park`
resets the CPU, so IF is 0 for the whole measurement: no tick, no mouse ISR,
nothing in the number but the routine.

WHY NOT `tests/gfxbench`. gfxbench has the four `GFX_LINE` rows and they are
the right instrument for the SHIPPED cost of the shipped call - but a row
there is a package on a floppy, so trying an alternative rasteriser means a
disk, a boot and a GUI to drive.  Here a candidate is a text file the host
assembles per run, which is what makes a LADDER of them affordable.  The two
agree: PERFORMANCE.md Set 11 measured `GFX_LINE steep thin` at 21,184 us on
the field 5150 and this reports 19,775 for the same geometry without
gfxbench's far call and wrapper.

THE PIXELS ARE DIFFED, NOT ASSUMED. A candidate is only interesting if it is
the same line, so `candidate` captures the framebuffer after gfx_line, puts
the screen back, runs the candidate and compares byte for byte.  That is not
decoration: the Hercules-only `js` wrap shortcut lays a DIFFERENT line on CGA
and this is what says so.

`--model` is the same question one level up and needs no emulator: it walks
gfx_line_mono's rasterisation and the candidate's, in Python, over every
endpoint pair in a 121x80 box, and reports how many disagree.  Run it first -
a candidate that fails there is not worth booting anything for.
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STUBSRC = os.path.join(HERE, "os88linecost.asm")
KERNEL_SEG = 0x0060
HZ = 4772727.0                      # the 5150's 14.31818MHz / 3
NITER = 500                         # iterations per `pieces` row

# Two transposed geometries at three lengths. The pair checks itself: the same
# line either way round is the same number of pixels, so a difference between
# them is the walk's own asymmetry (PERFORMANCE.md Set 11 found 10%).
CASES = [
    (100, 40, 108,  71, "steep    8x31   (32 px)"),
    (100, 40, 116, 103, "steep   16x63   (64 px)"),
    (100, 40, 132, 167, "steep   32x127 (128 px)"),
    (100, 40, 131,  48, "shallow 31x8    (32 px)"),
    (100, 40, 163,  56, "shallow 63x16   (64 px)"),
    (100, 40, 227,  72, "shallow 127x32 (128 px)"),
]

VARIANTS = [
    ("reg", dict(SIGNWRAP=1),
     "registers, inline row step, `js` wrap (HERCULES ONLY)"),
    ("reg-gen", dict(SIGNWRAP=0),
     "...with the portable `test di,[vid_wrapbit]` row step"),
    ("nodraw", dict(SIGNWRAP=1, NODRAW=1),
     "`reg` with the framebuffer store removed - the store's own share"),
    ("accum", dict(SIGNWRAP=1, ACCUM=1),
     "x-major only: one framebuffer RMW per BYTE instead of per pixel"),
]

PIECES = [
    (1, "call gfx_nextrow, as the walk makes it"),
    (2, "...the same three instructions INLINE"),
    (3, "the five per-pixel guard compares (clip box + gfx_ln_wide)"),
    (4, "the e2 block: e2=2*err through .bss, both Bresenham tests"),
    (5, "...the same decision in registers"),
    (7, "an indirect `jmp word [mem]` - the four-steep-loops collapse"),
]


def us(c):
    return c * 1e6 / HZ


# --- the rasterisations, in Python ------------------------------------------

def walk_kernel(x1, y1, x2, y2):
    """gfx_line_mono, transcribed. The endpoints are already normalised y1<=y2."""
    dy, sx, dx = y2 - y1, 1, x2 - x1
    if dx < 0:
        dx, sx = -dx, -1
    err, x, y, out = dx - dy, x1, y1, []
    while True:
        out.append((x, y))
        if x == x2 and y == y2:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x += sx
        if e2 < dx:
            err += dx
            y += 1
    return out


def walk_candidate(x1, y1, x2, y2):
    """The octant-split form the stub runs: no err memory, no per-pixel tests."""
    dy, sx, dx = y2 - y1, 1, x2 - x1
    if dx < 0:
        dx, sx = -dx, -1
    x, y, out = x1, y1, []
    if dy > dx:                                     # y-major
        d, dx2, dy2 = 2 * dx - dy, 2 * dx, 2 * dy
        for _ in range(dy + 1):
            out.append((x, y))
            if d > 0:
                x += sx
                d -= dy2
            d += dx2
            y += 1
    else:                                           # x-major
        d, dx2, dy2 = 2 * dy - dx, 2 * dx, 2 * dy
        for _ in range(dx + 1):
            out.append((x, y))
            if d > 0:
                y += 1
                d -= dx2
            d += dy2
            x += sx
    return out


def cmd_model(_args):
    bad = n = 0
    for dx in range(-60, 61):
        for dy in range(0, 80):
            if dx == 0 or dy == 0:                  # gfx_line tail-calls the
                continue                            # area primitives for these
            n += 1
            a = walk_kernel(100, 40, 100 + dx, 40 + dy)
            b = walk_candidate(100, 40, 100 + dx, 40 + dy)
            if a != b:
                bad += 1
                if bad <= 5:
                    print("  MISMATCH dx=%d dy=%d" % (dx, dy))
    print("%d endpoint pairs, %d mismatches" % (n, bad))
    return 1 if bad else 0


# --- the emulator side -------------------------------------------------------

def assemble(sym, stub_off, **defs):
    """Assemble the stub for one configuration; return (bytes, {label: off})."""
    d = {
        "STUB": stub_off, "VID_RSEG": sym["vid_rseg"],
        "GFX_ROWBASE": sym["gfx_rowbase"], "GFX_NEXTROW": sym["gfx_nextrow"],
        "ROWADD": sym["vid_rowadd"], "WRAPBIT": sym["vid_wrapbit"],
        "WRAPFIX": sym["vid_wrapfix"],
        "LN_CX1": sym["gfx_ln_cx1"], "LN_CX2": sym["gfx_ln_cx2"],
        "LN_CY1": sym["gfx_ln_cy1"], "LN_CY2": sym["gfx_ln_cy2"],
        "LN_WIDE": sym["gfx_ln_wide"], "LN_ERR": sym["gfx_ln_err"],
        "LN_E2": sym["gfx_ln_e2"], "LN_DX": sym["gfx_ln_dx"],
        "LN_DY": sym["gfx_ln_dy"], "LN_SX": sym["gfx_ln_sx"],
        "X1": 0, "Y1": 0, "D0": 0, "SUB2": 0, "ADD2": 0, "COUNT": 0,
        "STEEP": 0, "SIGNWRAP": 0, "NODRAW": 0, "ACCUM": 0, "N": NITER,
        "PIECE": 0,
    }
    d.update(defs)
    with tempfile.TemporaryDirectory() as td:
        out, mp, src = (os.path.join(td, n)
                        for n in ("s.bin", "s.map", "s.asm"))
        with open(src, "w") as f:                   # [map] answers where the
            f.write("[map all %s]\n" % mp)          # labels landed; nasm -l's
            f.write(open(STUBSRC).read())           # columns do not (os88sym)
        cmd = ["nasm", "-f", "bin", "-w+error", "-o", out]
        cmd += ["-D%s=%d" % (k, v) for k, v in d.items()]
        subprocess.run(cmd + [src], check=True, cwd=td)
        blob = open(out, "rb").read()
        lab = {}
        for line in open(mp):
            p = line.split()
            if len(p) == 3 and p[-1] in ("start", "loop_start", "done"):
                lab[p[-1]] = int(p[1], 16)
    return blob, lab


def line_defs(x1, y1, x2, y2, **kw):
    dy, dx = y2 - y1, x2 - x1
    assert dx >= 0 and dy >= 0, "the stub covers the +x/+y octant pair"
    if dy > dx:
        d0, count, sub2, add2 = 2 * dx - dy, dy + 1, 2 * dy, 2 * dx
        steep = 1
    else:
        d0, count, sub2, add2 = 2 * dy - dx, dx + 1, 2 * dx, 2 * dy
        steep = 0
    out = dict(X1=x1, Y1=y1, D0=d0 & 0xFFFF, SUB2=sub2, ADD2=add2,
               COUNT=count, STEEP=steep)
    out.update(kw)
    return out, count


class Rig(object):
    """A booted, paused os8088 the stub can be parked on repeatedly."""

    def __init__(self, m, sym, stub):
        self.m, self.sym, self.stub = m, sym, stub
        r = m.regs()
        self.ss, self.sp = r["ss"], r["sp"]
        self.fbseg = self.word("vid_rseg")
        # vid_wrapbit is nbanks * 0x2000, which is the framebuffer's length
        self.fblen = self.word("vid_wrapbit") or 0x8000
        m.write(os88sym.linear("wm_clip_n"), b"\x00\x00")   # SPEC.md 5.6.3
        m.write(os88sym.linear("gfx_color"), bytes([15]))   # -> solid white

    def word(self, name):
        return int.from_bytes(self.m.read(os88sym.linear(name), 2), "little")

    def fb(self):
        return self.m.read(self.fbseg << 4, self.fblen)

    def restore(self, data):
        self.m.write(self.fbseg << 4, data)

    def park(self, blob, **regs):
        m = self.m
        m.write((KERNEL_SEG << 4) + self.stub, blob)
        m.cmd(cmd="park", cs=KERNEL_SEG, ip=self.stub)      # ...and IF := 0
        for reg, val in (("ds", KERNEL_SEG), ("es", KERNEL_SEG),
                         ("ss", self.ss), ("sp", self.sp)):
            m.setreg(reg, val)
        for reg, val in regs.items():
            m.setreg(reg, val)
        m.write(os88sym.linear("cur_lazy"), b"\x00")        # SPEC.md 7.1.4

    def marks(self, offs):
        """Cycle/instruction marks: one now, one at each offset in turn."""
        m, out = self.m, [(self.m.status()["cycles"],
                           self.m.status()["instructions"])]
        for off in offs:
            m.bp_exec((KERNEL_SEG << 4) + off)
            m.run()
            if m.wait_stop(120) is None or m.status()["ip"] != off:
                raise SystemExit("the stub never reached %04x (ip=%04x)"
                                 % (off, m.status()["ip"]))
            out.append((m.status()["cycles"], m.status()["instructions"]))
        return out

    def gfx_line(self, x1, y1, x2, y2, fat=0):
        """The kernel's own, through a three-byte `call` stub."""
        rel = (self.sym["gfx_line"] - (self.stub + 3)) & 0xFFFF
        self.park(bytes([0xE8, rel & 0xFF, rel >> 8, 0xEB, 0xFE]),
                  ax=x1 & 0xFFFF, bx=y1 & 0xFFFF, cx=x2 & 0xFFFF,
                  dx=y2 & 0xFFFF, si=fat)   # setreg is unsigned; a coordinate
                  # off the top or left of the screen is a legitimate argument
        a, b = self.marks([self.stub + 3])
        return b[0] - a[0], b[1] - a[1]


def with_rig(args, body):
    sym = os88sym.syms()
    with os88marty.launch(args.image, machine=args.machine) as m:
        os88marty.settle(m)
        m.pause()
        rig = Rig(m, sym, sym["gfx_pairtab0"])
        print("%s: framebuffer %04x:0000+%04x\n"
              % (args.machine, rig.fbseg, rig.fblen))
        return body(rig, sym)


def cmd_gfx_line(args):
    def body(rig, sym):
        print("  %-26s %8s %10s %8s" % ("", "cycles", "us", "ins"))
        rows = []
        for (x1, y1, x2, y2, label) in [
                (100, 40, 100, 40, "degenerate (-> gfx_hline)"),
                (100, 40, 227, 40, "gfx_hline 128px")] + CASES:
            for fat, tag in ((0, ""), (1, " FAT")):
                if fat and "px)" not in label:
                    continue
                cyc, ins = rig.gfx_line(x1, y1, x2, y2, fat)
                rows.append((label + tag, cyc, ins))
                print("  %-26s %8d %10.1f %8d"
                      % (label + tag, cyc, us(cyc), ins))
        print()
        for kind in ("steep", "shallow"):
            pts = [(int(l.split("(")[1].split()[0]), c)
                   for l, c, _ in rows if l.startswith(kind) and "FAT" not in l]
            if len(pts) >= 2:
                (n0, c0), (n1, c1) = pts[0], pts[-1]
                slope = (c1 - c0) / float(n1 - n0)
                print("  %-8s fit: arrival %.0f cyc (%.0f us) + %.1f cyc/px "
                      "(%.1f us/px)"
                      % (kind, c0 - n0 * slope, us(c0 - n0 * slope),
                         slope, us(slope)))
        return 0
    return with_rig(args, body)


def cmd_candidate(args):
    def body(rig, sym):
        bad = 0
        for (x1, y1, x2, y2, label) in CASES:
            shallow = (x2 - x1) >= (y2 - y1)
            clean = rig.fb()
            old, _ = rig.gfx_line(x1, y1, x2, y2)
            gold = rig.fb()
            print("  %-24s gfx_line %7d cyc %9.1f us" % (label, old, us(old)))
            for vname, kw, _note in VARIANTS:
                if kw.get("ACCUM") and not shallow:
                    continue
                rig.restore(clean)
                defs, count = line_defs(x1, y1, x2, y2, **kw)
                blob, lab = assemble(sym, rig.stub, **defs)
                rig.park(blob)
                a, b, c = rig.marks([lab["loop_start"], lab["done"]])
                setup, walk = b[0] - a[0], c[0] - b[0]
                diff = sum(1 for p, q in zip(gold, rig.fb()) if p != q)
                bad += 1 if (diff and vname != "nodraw") else 0
                print("      %-8s %7d cyc %9.1f us = setup %3d + walk %5d "
                      " (%5.1f cyc/px, %4.1f ins/px)  %5.2fx   %s"
                      % (vname, setup + walk, us(setup + walk), setup, walk,
                         walk / float(count),
                         (c[1] - b[1]) / float(count),
                         old / float(setup + walk),
                         "identical" if diff == 0
                         else "%d BYTES DIFFER" % diff))
            rig.restore(clean)
            print()
        return 0
    return with_rig(args, body)


def cmd_pieces(args):
    def body(rig, sym):
        m = rig.m
        for name, val in (("gfx_ln_cx1", 0), ("gfx_ln_cy1", 0),
                          ("gfx_ln_cx2", 719), ("gfx_ln_cy2", 347),
                          ("gfx_ln_err", 5), ("gfx_ln_dx", 32),
                          ("gfx_ln_dy", 127), ("gfx_ln_sx", 1)):
            m.write(os88sym.linear(name), val.to_bytes(2, "little"))
        m.write(os88sym.linear("gfx_ln_wide"), b"\x00")
        print("  per iteration over %d, net of the bare `loop`:" % NITER)
        base = 0
        for piece, label in [(6, "(the bare `loop`)")] + PIECES:
            blob, lab = assemble(sym, rig.stub, PIECE=piece)
            rig.park(blob)
            a, b, c = rig.marks([lab["loop_start"], lab["done"]])
            per = (c[0] - b[0]) / float(NITER)
            ins = (c[1] - b[1]) / float(NITER)
            if piece == 6:
                base = per
                print("    %-52s %6.1f cyc %5.1f ins" % (label, per, ins))
            else:
                print("    %-52s %6.1f cyc %5.1f ins"
                      % (label, per - base, ins - 1))
        return 0
    return with_rig(args, body)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("mode", nargs="?", default="model",
                    choices=("model", "gfx_line", "candidate", "pieces"))
    ap.add_argument("--model", action="store_true",
                    help="the pixel-set comparison, with no emulator")
    ap.add_argument("--machine", default="os8088_5150_herc",
                    help="a MartyPC machine (…_herc, …_cga, …_herc_gla)")
    ap.add_argument("--image", default="build/os8088-360.img")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.model:
        a.mode = "model"
    return {"model": cmd_model, "gfx_line": cmd_gfx_line,
            "candidate": cmd_candidate, "pieces": cmd_pieces}[a.mode](a)


if __name__ == "__main__":
    sys.exit(main())
