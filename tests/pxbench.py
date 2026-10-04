#!/usr/bin/env python3
"""WHAT PIXEL COSTS A 4.77 MHz 8088 (SPEC.md 106.17).

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
  wave 3 CAT.GIF (320x240, 256 colours), BALLOONS.PNG (320x240 palette) and
         HOUSE.PNG (320x240 RGB, 160 KB): the decoder PARTS (SPEC.md 106.18),
         opened as above; and INFLATE's own cost on HOUSE.PNG: the cycles
         inside the part's DECODE less those in its scanline extraction
         (unfilter, pixels, emit), in K_NEXT (the worker waiting on the
         disk) and in the UI task's wakes that land while inflate runs (its
         reads of the next slot), over the 230,640 bytes inflate writes -
         with the canvas's progressive painting held off for it. (A CZ-
         wrapped PNG would read no disk at all, but a PNG does not pack.)
  wave 4 VACATION.JPG (640x480 4:2:0) at 1/8, 1/2 and 1/1 - each asked for
         through the re-decode's floor, [px_zreq] - and as FAST OPEN picks;
         ROOM.JPG (640x480 progressive, 1/4); where VACATION's decode goes
         at 1/8 and 1/2 (the entropy decode with its dequantising, the IDCT,
         colour, rows out, the ring's waits); and LAKE.JPG's (320x240 grey)
         entropy + dequantise and IDCT in cycles a pixel (SPEC.md 106.19,
         106.22). BIG.BMP's disk is 720 KB since wave 4's parts
         made PIXEL.O88 68 clusters, so its session runs the same machine
         with an 80-cylinder 720 KB B: (os8088_5150_cga_720b_gla,
         os8088_xt_vga_720b), whose tracks read as the 360 KB drive's do

A routine is timed from its entry to its return - the return address is read
off the guest's stack at the entry stop - so a figure is the exact cycles
between two instructions, and the stops themselves cost the guest nothing.

Each figure is held to a CEILING: what SPEC.md 106.17 records plus a margin.
A change that makes PiXEL slower on the target fails here with both numbers
on the line; `--no-ceiling` prints them and judges nothing.
"""
import os, sys, struct, argparse, functools, time, subprocess, tempfile
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88build
from pxsyms import pkg_syms, instance, u16

HZ = 4772727
FAIL = []
# SPEC.md 106.17's figures, seconds, plus about 15% - and 30% on a FIRST
# ROWS figure, which moves with where in its tick and its disk revolution the
# key happened to land: the bench's CEILINGS. (open, first rows) per picture;
# then the zoom step, Fit and pan figures
# The GIF, PNG and JPEG rows, the inflate figure and the decode breakdown's
# two are the decoder speed pass's (SPEC.md 106.22), with the same margins
# (a progressive picture's FIRST ROWS are the whole store's decode, so that
# one carries the open's margin; a first-rows ceiling is the larger of a
# picture's measured first rows across its opens at one scale, + 30%).
# BIG.BMP's first rows are 106.17's again (1.34 s on the 5150): the wake's
# deferral had been reading 32 KB in front of the first paint (106.22). The
# JPEG `huffman` figure is the block's entropy decode WITH its dequantising
# now (pj_blk less its IDCT), `idct` the IDCT alone (pj_id8)
CEIL = {
    "os8088_5150_cga_gla": {
        "MOUNTAIN.BMP": (21.0, 4.0), "CITY.PCX": (14.7, 3.5),
        "BIG.BMP": (40.8, 1.8),
        "CAT.GIF": (29.0, 4.0), "BALLOONS.PNG": (18.3, 5.6),
        "HOUSE.PNG": (52.6, 3.0), "inflate": 372,
        "zoom": 1.0, "fit": 0.55, "pan": 0.35,
        "VACATION.JPG 1/8": (27.4, 6.0), "VACATION.JPG 1/2": (53.0, 4.8),
        "VACATION.JPG 1/1": (133.5, 8.0), "VACATION.JPG fast": (54.5, 4.8),
        "ROOM.JPG": (71.8, 64.1), "huffman": 345, "idct": 613},
    "os8088_xt_vga": {
        "MOUNTAIN.BMP": (23.0, 4.1), "CITY.PCX": (19.3, 6.0),
        "BIG.BMP": (47.0, 3.9),
        "CAT.GIF": (34.1, 6.2), "BALLOONS.PNG": (23.7, 8.4),
        "HOUSE.PNG": (55.6, 2.8), "inflate": 463,
        "zoom": 4.3, "fit": 2.25, "pan": 0.57,
        "VACATION.JPG 1/8": (31.4, 5.8), "VACATION.JPG 1/2": (57.5, 5.1),
        "VACATION.JPG 1/1": (138.0, 8.2), "VACATION.JPG fast": (57.5, 5.1),
        "ROOM.JPG": (76.0, 64.3), "huffman": 360, "idct": 635},
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
ap.add_argument("--sessions", default="12345",
                help="which of the four sessions to run (for a quick look)")
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


def session(disk, pics, work, machine=None):
    """Boot with `pics` on B:, open the first by its association, then hand
    the machine to `work`."""
    with os88ui.boot("build/os8088-360.img", apps=disk,
                     machine=machine or a.machine) as ui:
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


print("== PiXEL on the target: %s (SPEC.md 106.17) ==" % a.machine)
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


if "1" in a.sessions:
    session(DISK, ["CITY.PCX", "MOUNTAIN.BMP"], work1)
# BIG.BMP (302 clusters) and PIXEL.O88 (68 since wave 4's parts) are more
# than a 360 KB volume holds, so this one disk is 720 KB and goes in an
# 80-cylinder 720 KB B: - the same machine with that drive, whose 9 sectors a
# track at 250 kbps read as the 360 KB drive's do. A is the 360 KB system disk
DISK2 = M.scratch_disk("build/pxbench2.img", "build/pixel.o88",
                       "PICS:" + SMALL, "PICS:" + BIG, size=720)
MACH720 = {"os8088_5150_cga_gla": "os8088_5150_cga_720b_gla",
           "os8088_xt_vga": "os8088_xt_vga_720b"}


def work2(m, px):
    t, first = px.open("BIG.BMP")
    cap = ceil.get("BIG.BMP", (None, None))
    check("open BIG.BMP (640x480 8-bit, 308 KB)", t, cap[0])
    check("   ...first rows on the glass", first, cap[1])


if "2" in a.sessions:
    session(DISK2, ["C8.PCX"], work2, MACH720.get(a.machine))

# --- wave 3: the decoder parts (SPEC.md 106.18) --------------------------------
def part_syms(src):
    """{name: offset} of a part, assembled with a map from this tree."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/pixel/", "-I", os88build.at("build") + "/",
                        "-o", os.path.join(d, "p.bin"),
                        cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)
        return out


PNGSYMS = part_syms("apps/pixel/pxpng.asm")
OP_T_ROWS = 10                          # apps/os88parts.inc


def inflate_bench(m, px, name, outbytes):
    """Guest cycles a byte inflate writes: DECODE's (px_dpart, entry to
    return) less pz_extract's and K_NEXT's, over `outbytes`."""
    ent = px.a("px_dpart") & 0xFFFFF
    nxt = px.a("px_ks_next") & 0xFFFFF
    wake = px.a("px_onwake") & 0xFFFFF
    st = {"t0": None, "t1": None, "ext": 0, "next": 0, "ui": 0, "open": [],
          "rets": {}, "segbp": None, "ow": None, "owret": None}
    base_bps = [{"type": "exec", "addr": ent}, {"type": "exec", "addr": nxt},
                {"type": "exec", "addr": wake}]

    def arm():
        extra = [a for a in (st.get("ext_at"), st.get("dret"), st.get("owret"))
                 if a is not None] + list(st["rets"])
        m.breakpoints(base_bps + [{"type": "exec", "addr": a} for a in extra])

    def stop(flat, cyc):
        if st["t0"] is None:
            if flat != ent:
                return
            st["t0"] = cyc
            r = m.regs()
            st["dret"] = (px.base + u16(m.read(r["ss"] * 16 + r["sp"], 2))) & 0xFFFFF
            row = m.read(px.a("op_table") + OP_T_ROWS + 8 * 2, 8)
            pseg = u16(row, 6)
            st["ext_at"] = (pseg * 16 + PNGSYMS["pz_extract"]) & 0xFFFFF
            st["pseg"] = pseg
            m.write(px.a("px_abon"), b"\1")  # the progressive paint held off
            arm()
            return
        if flat == st["dret"]:
            st["t1"] = cyc
            for c0, kind in st["open"]:
                if c0 is not None:
                    st[kind] += cyc - c0
            st["open"] = []
            m.write(px.a("px_abon"), b"\0")
            m.breakpoints([])
            return
        if flat == wake:
            # THE UI TASK, waking to read the next slot (or to paint the
            # progress): its cycles are taken off inflate's when it lands
            # while inflate runs and nothing of the worker's interrupts it
            r = m.regs()
            st["ow"] = [cyc, not st["open"]]
            st["owret"] = (px.base + u16(m.read(r["ss"] * 16 + r["sp"], 2))) & 0xFFFFF
            arm()
            return
        if flat == st.get("owret") and st["ow"] is not None:
            c0, clean = st["ow"]
            if clean:
                st["ui"] += cyc - c0
            st["ow"] = None
            return
        if st["ow"] is not None:
            st["ow"][1] = False         # the worker ran inside the wake
        if flat == st["ext_at"] or flat == nxt:
            r = m.regs()
            sp = r["ss"] * 16 + r["sp"]
            if flat == nxt:                     # far: IP then CS
                ret = (u16(m.read(sp + 2, 2)) * 16 + u16(m.read(sp, 2))) & 0xFFFFF
                kind = "next"
            else:
                ret = (st["pseg"] * 16 + u16(m.read(sp, 2))) & 0xFFFFF
                kind = "ext"
            st["open"].append((cyc if not st["open"] else None, kind))
            if ret not in st["rets"]:
                st["rets"][ret] = 1
                arm()
            return
        if flat in st["rets"] and st["open"]:
            c0, kind = st["open"].pop()
            if c0 is not None:
                st[kind] += cyc - c0

    def trigger():
        m.write(px.a("px_cur"), name.encode().ljust(13, b"\0"))
        m.ctrl("KeyR")
    px.pump(base_bps, stop, lambda: st["t1"] is not None, trigger)
    M.until(m, lambda _: px.idle(), "the open to settle", poll=0.3, limit=600)
    M.ui_done(m, "the open")
    tot = st["t1"] - st["t0"]
    infl = tot - st["ext"] - st["next"] - st["ui"]
    return tot, infl, st["ext"], st["next"], st["ui"]


DISK3 = M.scratch_disk("build/pxbench3.img", "build/pixel.o88",
                       "PICS:" + SMALL, "PICS:apps/pixel/samples/CAT.GIF",
                       "PICS:apps/pixel/samples/BALLOONS.PNG",
                       "PICS:apps/pixel/samples/HOUSE.PNG")


def work3(m, px):
    for pic in ("CAT.GIF", "BALLOONS.PNG", "HOUSE.PNG"):
        t, first = px.open(pic)
        cap = ceil.get(pic, (None, None))
        check("open %s" % pic, t, cap[0])
        check("   ...first rows on the glass", first, cap[1])


if "3" in a.sessions:
    session(DISK3, ["C8.PCX"], work3)
DISK4 = M.scratch_disk("build/pxbench4.img", "build/pixel.o88",
                       "PICS:" + SMALL, "PICS:apps/pixel/samples/HOUSE.PNG")


def work4(m, px):
    tot, infl, ext, nxt, ui = inflate_bench(m, px, "HOUSE.PNG", 240 * 961)
    cpb = infl / (240 * 961)
    print("   HOUSE.PNG decode %.2f s = inflate %.2f s + rows %.2f s + "
          "K_NEXT %.2f s + the UI's wakes %.2f s"
          % (tot / HZ, infl / HZ, ext / HZ, nxt / HZ, ui / HZ))
    print("   inflate: %.1f cycles a byte written (230,640 bytes)" % cpb)
    cap = ceil.get("inflate")
    if cap is not None and not a.no_ceiling and cpb > cap:
        print("   ...over its ceiling of %.0f FAIL" % cap)
        FAIL.append("inflate")


if "4" in a.sessions:
    session(DISK4, ["C8.PCX"], work4)

# --- wave 4: JPEG (SPEC.md 106.19) ----------------------------------------------
JPGSYMS = part_syms("apps/pixel/pxjpeg.asm")


def stage_bench(m, px, name, routines):
    """Guest cycles inside each of `routines` (entry to return, outermost
    calls only) over one open of `name`: where a decode's time goes, stage
    by stage. A `pj_` name is the JPEG part's, near-called inside it; a
    `px_ks_` name is a resident SERVICE, far-called from the part."""
    ent = px.a("px_dpart") & 0xFFFFF
    st = {"pseg": None, "rets": {}, "open": [], "tot": {r: 0 for r in routines},
          "n": {r: 0 for r in routines}, "t0": None, "t1": None, "dret": None}
    base_bps = [{"type": "exec", "addr": ent}]

    def arm():
        at = [st["ents"][r] for r in routines] + list(st["rets"])
        if st["dret"] is not None:
            at.append(st["dret"])
        m.breakpoints(base_bps + [{"type": "exec", "addr": x} for x in at])

    def stop(flat, cyc):
        if st["t0"] is None:
            if flat != ent:
                return
            st["t0"] = cyc
            r = m.regs()
            st["dret"] = (px.base + u16(m.read(r["ss"] * 16 + r["sp"], 2))) & 0xFFFFF
            row = m.read(px.a("op_table") + OP_T_ROWS + 8 * 3, 8)
            st["pseg"] = u16(row, 6)
            st["ents"] = {r: ((st["pseg"] * 16 + JPGSYMS[r]) if r in JPGSYMS
                              else px.a(r)) & 0xFFFFF for r in routines}
            st["byent"] = {v: k for k, v in st["ents"].items()}
            arm()
            return
        if flat == st["dret"] and not st["open"]:
            st["t1"] = cyc
            m.breakpoints([])
            return
        if flat in st.get("byent", {}):
            r = m.regs()
            sk = m.read(r["ss"] * 16 + r["sp"], 4)
            if st["byent"][flat] in JPGSYMS:
                ret = (st["pseg"] * 16 + u16(sk)) & 0xFFFFF
            else:                       # a far call's return: IP, then CS
                ret = (u16(sk, 2) * 16 + u16(sk)) & 0xFFFFF
            st["open"].append((cyc, st["byent"][flat], ret))
            if ret not in st["rets"]:
                st["rets"][ret] = 1
                arm()
            return
        if st["open"] and flat == st["open"][-1][2]:
            c0, rt, _ = st["open"].pop()
            st["tot"][rt] += cyc - c0
            st["n"][rt] += 1

    def trigger():
        m.write(px.a("px_cur"), name.encode().ljust(13, b"\0"))
        m.ctrl("KeyR")
    px.pump(base_bps, stop, lambda: st["t1"] is not None, trigger)
    M.until(m, lambda _: px.idle(), "the open to settle", poll=0.3, limit=900)
    M.ui_done(m, "the open")
    return st["t1"] - st["t0"], st["tot"], st["n"]


DISK5 = M.scratch_disk("build/pxbench5.img", "build/pixel.o88",
                       "PICS:" + SMALL, "PICS:apps/pixel/samples/VACATION.JPG",
                       "PICS:apps/pixel/samples/ROOM.JPG",
                       "PICS:apps/pixel/samples/LAKE.JPG")


def work5(m, px):
    # VACATION.JPG, 640x480 4:2:0: at 1/8, at 1/2, at 1/1 - each scale asked
    # for through the re-decode's floor ([px_zreq], SPEC.md 106.19) - and as
    # FAST OPEN chooses it in this machine's canvas
    for sc, label in ((3, "1/8"), (1, "1/2"), (0, "1/1")):
        px.m.write(px.a("px_zreq"), bytes((sc,)))
        t, first = px.open("VACATION.JPG")
        cap = ceil.get("VACATION.JPG " + label, (None, None))
        got = m.read(px.a("px_cur") + 36, 1)[0]
        check("open VACATION.JPG at %s (got 1/%d)" % (label, 1 << got), t, cap[0])
        check("   ...first rows on the glass", first, cap[1])
    px.m.write(px.a("px_zreq"), b"\xFF")
    t, first = px.open("VACATION.JPG")
    got = m.read(px.a("px_cur") + 36, 1)[0]
    cap = ceil.get("VACATION.JPG fast", (None, None))
    check("open VACATION.JPG by FAST OPEN (1/%d here)" % (1 << got), t, cap[0])
    check("   ...first rows on the glass", first, cap[1])
    t, first = px.open("ROOM.JPG")
    got = m.read(px.a("px_cur") + 36, 1)[0]
    cap = ceil.get("ROOM.JPG", (None, None))
    check("open ROOM.JPG, progressive (1/%d)" % (1 << got), t, cap[0])
    check("   ...first rows on the glass", first, cap[1])
    # where a decode's time goes: LAKE.JPG (320x240 grey, 1,200 blocks) at
    # 1/1 - the block (pj_blk: its entropy decode, the dequantising as each
    # coefficient is read, and the IDCT it calls), the IDCT alone (pj_id8),
    # rows out (SPEC.md 106.22)
    px.m.write(px.a("px_zreq"), b"\0")
    tot, stg, n = stage_bench(m, px, "LAKE.JPG", ("pj_blk", "pj_id8",
                                                 "pj_band"))
    px.m.write(px.a("px_zreq"), b"\xFF")
    # ...and VACATION.JPG's own, at 1/8 and at 1/2: the entropy decode (the
    # block less its IDCT), the IDCT, the colour conversion, the rows out
    # (K_EMIT, the master and its progress) and the waits for the disk
    # (K_NEXT, the ring)
    for sc, idct in ((3, "pj_id1"), (1, "pj_id4")):
        px.m.write(px.a("px_zreq"), bytes((sc,)))
        vt, vs, vn = stage_bench(m, px, "VACATION.JPG", (
            "pj_blk", idct, "pj_conv", "px_ks_emit", "px_ks_next"))
        print("   VACATION.JPG at 1/%d: decode %.2f s - entropy + dequantise "
              "%.2f, IDCT %.2f, colour %.2f, rows out %.2f, the ring's waits "
              "%.2f" % (1 << sc, vt / HZ, (vs["pj_blk"] - vs[idct]) / HZ,
                        vs[idct] / HZ, vs["pj_conv"] / HZ,
                        vs["px_ks_emit"] / HZ, vs["px_ks_next"] / HZ))
    px.m.write(px.a("px_zreq"), b"\xFF")
    pixels = 320 * 240
    huff = stg["pj_blk"] - stg["pj_id8"]
    print("   LAKE.JPG at 1/1: decode %.2f s - entropy + dequantise %.2f s (%d "
          "blocks, %.0f cycles a pixel), IDCT %.2f s (%.0f a pixel), rows out "
          "%.2f s" % (tot / HZ, huff / HZ, n["pj_blk"], huff / pixels,
                      stg["pj_id8"] / HZ, stg["pj_id8"] / pixels,
                      stg["pj_band"] / HZ))
    cap = ceil.get("idct")
    if cap is not None and not a.no_ceiling and stg["pj_id8"] / pixels > cap:
        print("   ...IDCT over its ceiling of %.0f FAIL" % cap)
        FAIL.append("idct")
    cap = ceil.get("huffman")
    if cap is not None and not a.no_ceiling and huff / pixels > cap:
        print("   ...entropy + dequantise over its ceiling of %.0f FAIL" % cap)
        FAIL.append("huffman")

if "5" in a.sessions:
    session(DISK5, ["C8.PCX"], work5)
print()
if FAIL:
    print("pxbench: FAILED: " + ", ".join(FAIL))
    sys.exit(1)
print("pxbench: ok")
