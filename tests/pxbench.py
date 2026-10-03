#!/usr/bin/env python3
"""WHAT PIXEL COSTS A 4.77 MHz 8088 (SPEC.md 106.15).

    python3 tests/pxbench.py [--machine os8088_5150_cga_gla | os8088_xt_vga]
                             [--no-ceiling]

MartyPC's cycle counter, on the 5150 with a CGA and the XT with a VGA, both
at 4.77 MHz - the machine this project is calibrated against (CLAUDE.md).
Every figure is GUEST time and none of it is a host clock, so a loaded box
measures the same numbers as an idle one:

  open   MOUNTAIN.BMP (256x192, 24-bit: the cube's ordered dither), CITY.PCX
         (320x240, 8-bit RLE: on a colour display, the palette's 256 plans),
         and BIG.BMP (640x480, 8-bit, made here: 308 KB, the disk's share) -
         from the key that opens to the end of the decode (the instruction
         that counts an open ended, [px_ndone], by a memory breakpoint), and
         to the FIRST ROWS on the glass (px_rimg's first entry)
  render a zoom step with the canvas covered: px_zoomto, which is the whole
         canvas composed and blitted, the Navigator's frame and the zoom
         field; and Fit (the key 0) from there
  pan    an arrow key: one OSAPI_GFX_SCROLL and the exposed strip (px_panby)

A routine is timed from its entry to its return - the return address is read
off the guest's stack at the entry stop - so a figure is the exact cycles
between two instructions, and the stops themselves cost the guest nothing.

Each figure is held to a CEILING: what SPEC.md 106.15 records plus a margin.
A change that makes PiXEL slower on the target fails here with both numbers
on the line; `--no-ceiling` prints them and judges nothing.
"""
import os, sys, struct, argparse, functools, time
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88build
from pxsyms import pkg_syms, instance, u16

HZ = 4772727
FAIL = []
# SPEC.md 106.15's figures, seconds, plus about 15% - and 30% on a FIRST
# ROWS figure, which moves with where in its tick and its disk revolution the
# key happened to land: the bench's CEILINGS. (open, first rows) per picture;
# then the zoom step, Fit and pan figures
CEIL = {
    "os8088_5150_cga_gla": {
        "MOUNTAIN.BMP": (21.0, 4.0), "CITY.PCX": (14.7, 3.5),
        "BIG.BMP": (40.8, 2.6),
        "zoom": 1.0, "fit": 0.55, "pan": 0.35},
    "os8088_xt_vga": {
        "MOUNTAIN.BMP": (23.0, 4.1), "CITY.PCX": (19.3, 6.0),
        "BIG.BMP": (47.0, 5.9),
        "zoom": 4.3, "fit": 2.25, "pan": 0.57},
}


def big_bmp(path):
    """BIG.BMP: 640x480, 8 bits, a 256-colour palette - a smooth field with
    structure in it, deterministic, uncompressed (308 KB)."""
    w, h = 640, 480
    pal = b"".join(bytes(((i * 7) & 255, (i * 3 + 60) & 255, (255 - i) & 255, 0))
                   for i in range(256))
    rows = []
    for y in range(h - 1, -1, -1):                  # bottom-up
        rows.append(bytes(((x // 3 + y // 2 + ((x * y) >> 9)) & 255)
                          for x in range(w)))
    data = b"".join(rows)
    off = 14 + 40 + 1024
    hdr = b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(data),
                       2835, 2835, 256, 0)
    blob = hdr + pal + data
    if not os.path.exists(path) or open(path, "rb").read() != blob:
        open(path, "wb").write(blob)
    return path


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
ap.add_argument("--no-ceiling", action="store_true")
a = ap.parse_args()
ceil = CEIL.get(a.machine, {})

syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run "
             "`make build/pixel.o88`")
os.makedirs(os88build.at("build/pxbench"), exist_ok=True)
BIG = big_bmp(os88build.at("build/pxbench/BIG.BMP"))
SMALL = os88build.at("build/pxbench/C8.PCX")    # what the launch opens beside
sys.path.insert(0, "tools")                     # BIG.BMP, by its association
import pixcorpus                                # (a PCX is PiXEL's; a BMP is
_c8 = dict((n, d) for n, d, w in pixcorpus.corpus())["C8.PCX"]  # Paint's)
if not os.path.exists(SMALL) or open(SMALL, "rb").read() != _c8:
    open(SMALL, "wb").write(_c8)


def check(name, secs, cap):
    ok = a.no_ceiling or cap is None or secs <= cap
    print("   %-46s %7.2f s%s" % (name, secs,
          "" if cap is None else "   (ceiling %.2f%s)" % (cap, "" if ok else " FAIL")))
    if not ok:
        FAIL.append(name)


def session(disk, pics, work):
    """Boot with `pics` on B:, open the first by its association, then hand
    the machine to `work`."""
    with os88ui.boot("build/os8088-360.img", apps=disk,
                     machine=a.machine) as ui:
        m = ui.m
        S = ui._S
        ui.path("B:/PICS/" + pics[0])
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        seg = instance(m, S, image)
        px = Guest(m, seg)
        M.until(m, lambda _: px.W("px_ndone") and px.idle(), "the first open",
                poll=0.3, limit=1800)
        ui.settle()
        ui.mo.to(2, 2)                  # the pointer off the canvas
        ui.settle()
        work(m, px)


class Guest:
    def __init__(self, m, seg):
        self.m, self.seg, self.base = m, seg, seg * 16

    def a(self, n):
        return self.base + syms[n]

    def B(self, n):
        return self.m.read(self.a(n), 1)[0]

    def W(self, n):
        return u16(self.m.read(self.a(n), 2))

    def idle(self):
        return self.B("px_busy") == 0 and self.B("px_job") == 0

    def cycles(self):
        return int(self.m.status()["cycles"])

    def pump(self, bps, on_stop, done, trigger, limit=1800.0):
        """Arm `bps`, THEN `trigger()` (a key: a trigger before the arming
        is a measurement of nothing),
        calling on_stop(flat, cycles) at each stop until done() - then clear
        them and leave the guest running."""
        m = self.m
        m.pause()                       # ...and PAUSED while both land: a
        m.breakpoints(bps)              # running guest took the key before
        trigger()                       # the set did, and a zoom's entry
        m.run()                         # went by unstopped
        t0 = time.time()
        seen = None
        try:
            while True:
                st = m.status()
                if st.get("state") == "breakpoint":
                    # ONE STOP CAN REPORT TWICE: the `run` after it is a
                    # second round trip and may not have landed by the next
                    # poll (os88marty.BpTrace dedupes on the same counter)
                    if st.get("stops") != seen:
                        seen = st.get("stops")
                        flat = ((st["cs"] << 4) + st["ip"]) & 0xFFFFF
                        on_stop(flat, int(st["cycles"]))
                    m.run()
                    continue
                if done():
                    return
                if time.time() - t0 > limit:
                    raise SystemExit("pxbench: no end to the measurement in "
                                     "%d host seconds" % limit)
                time.sleep(0.01)
        finally:
            m.breakpoints([])
            m.run()

    def timed(self, name, trigger):
        """Guest cycles inside routine `name`, entry to return, over what
        `trigger` sets off (outermost calls only)."""
        m = self.m
        ent = self.a(name) & 0xFFFFF
        rets, open_, tot = {}, [], [0, 0]
        bps = [{"type": "exec", "addr": ent}]

        def stop(flat, cyc):
            if flat == ent:
                r = m.regs()
                ret = (self.base + u16(m.read(r["ss"] * 16 + r["sp"], 2))) & 0xFFFFF
                open_.append((cyc, ret))
                if ret not in rets:
                    rets[ret] = 1
                    m.breakpoints(bps + [{"type": "exec", "addr": x} for x in rets])
            elif open_ and flat == open_[-1][1]:
                c0, _ = open_.pop()
                if not open_:
                    tot[0] += cyc - c0
                    tot[1] += 1
        self.pump(bps, stop, lambda: tot[1] and not open_ and M.ui_idle(m),
                  trigger)
        M.ui_done(m, name)
        return tot[0] / HZ

    def open(self, name):
        """(open, first rows) seconds: the key to [px_ndone]'s increment, and
        to px_rimg's first entry."""
        m = self.m
        n0 = self.W("px_ndone")
        m.write(self.a("px_cur"), name.encode().ljust(13, b"\0"))
        rimg = self.a("px_rimg") & 0xFFFFF
        got = {}

        def stop(flat, cyc):
            if flat == rimg:
                got.setdefault("first", cyc)
                m.breakpoints([{"type": "mem", "addr": self.a("px_ndone")}])
            else:                       # the one instruction that touches
                got.setdefault("end", cyc)  # [px_ndone]: the open has ended
        c0 = []

        def trigger():
            c0.append(self.cycles())
            m.ctrl("KeyR")
        self.pump([{"type": "exec", "addr": rimg},
                   {"type": "mem", "addr": self.a("px_ndone")}], stop,
                  lambda: "end" in got, trigger)
        c0 = c0[0]
        M.until(m, lambda _: self.idle(), "the open to settle", poll=0.3,
                limit=600)
        M.ui_done(m, "the open")
        end = got["end"]
        return (end - c0) / HZ, (got.get("first", end) - c0) / HZ


print("== PiXEL on the target: %s (SPEC.md 106.15) ==" % a.machine)
DISK = M.scratch_disk("build/pxbench.img", "build/pixel.o88",
                      "PICS:apps/pixel/samples/CITY.PCX",
                      "PICS:apps/pixel/samples/MOUNTAIN.BMP")


def work1(m, px):
    for pic in ("MOUNTAIN.BMP", "CITY.PCX"):
        t, first = px.open(pic)
        cap = ceil.get(pic, (None, None))
        check("open %s" % pic, t, cap[0])
        check("   ...first rows on the glass", first, cap[1])
    # CITY is shown at Fit: two zoom steps cover the canvas, the third is the
    # full render the figure is about
    for k in ("Equal", "Equal"):
        m.key(k)
        M.ui_done(m, k)
    check("zoom step, the canvas covered (px_zoomto)",
          px.timed("px_zoomto", lambda: m.key("Equal")), ceil.get("zoom"))
    check("pan step, an arrow (px_panby)",
          px.timed("px_panby", lambda: m.key("ArrowDown")), ceil.get("pan"))
    check("Fit (px_zoomto)", px.timed("px_zoomto", lambda: m.key("Digit0")),
          ceil.get("fit"))


session(DISK, ["CITY.PCX", "MOUNTAIN.BMP"], work1)
DISK2 = M.scratch_disk("build/pxbench2.img", "build/pixel.o88",
                       "PICS:" + SMALL, "PICS:" + BIG)


def work2(m, px):
    t, first = px.open("BIG.BMP")
    cap = ceil.get("BIG.BMP", (None, None))
    check("open BIG.BMP (640x480 8-bit, 308 KB)", t, cap[0])
    check("   ...first rows on the glass", first, cap[1])


session(DISK2, ["C8.PCX"], work2)
print()
if FAIL:
    print("pxbench: FAILED: " + ", ".join(FAIL))
    sys.exit(1)
print("pxbench: ok")
