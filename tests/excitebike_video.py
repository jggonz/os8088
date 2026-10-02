#!/usr/bin/env python3
"""EXCITEBIKE wave 2: the scroll engine against the reference renderer (SPEC.md 102.6).

    make excitebikedisk && python3 tests/excitebike_video.py --adapter vga|cga|herc [--quick]

The oracle is tools/exbsim.py: the plan's world function W(x, l) and the pose art
rendered from the SOURCE text, with none of the guest's tricks (no sheared
memory, no pages, no latches, no skip lists, no ring).  The guest is asked to
render a state and the card's own output is compared with it PIXEL FOR PIXEL:

  1  the 20 random positions: a random course column, 1-3 bikes at random poses
     and (even) x, y; the guest jumps there (a full-window write on every page)
     and then scrolls a few frames at a random speed (the incremental path with
     its skip lists, the erase of the previous footprints, the third page).
  2  the long scroll: the whole course - and a synthetic 1,637-column course
     poked into the column array, the largest race the compiler allows - at a
     speed that puts 1 to 3 columns a frame through the engine, compared every
     ~90 frames.  On CGA this passes the ring wrap of SPEC.md 102.1.
  3  VGA: the line compare, the start address and the palette, from the card's
     own rasterisation (MartyPC here; `--qemu` runs the same picture on QEMU's
     VGA, the emulator that implements the real one: gate G1).

The capture is `m.fbuf` (the card's rendering: line-doubled 0Dh on VGA, the CGA
composite).  Nothing here reads a ROM or any external file.
"""
import argparse
import os
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M          # noqa: E402
import os88ui                  # noqa: E402
import os88geom as G           # noqa: E402
import os88build               # noqa: E402
import exbsim                  # noqa: E402

X = exbsim.X
MACHINE = {"vga": "os8088_xt_vga", "cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}
WPX = {"vga": 320, "cga": 320, "herc": 360}         # the picture's game pixels across
COLS = {"vga": 40, "cga": 40, "herc": 45}          # ...and its tile columns


def cgaish(tag):
    """The CGA back end and the Hercules' card side of it are one engine (SPEC.md 102.7)."""
    return tag in ("cga", "herc")


def expframe(ref, tag, cids, S, bikes, hud):
    """The reference renderer's picture for this adapter: palette slots (VGA), 2-bit inks (CGA) or
    2-bit card-pixel PAIRS (Hercules: 0 = 00, 2 = 10, 3 = 11, and 1 = 01 in a tile's checkerboard)."""
    return ref.frame(cids, S, bikes, hud.strip("\xff"), cga=tag == "cga", herc=tag == "herc")
OUT = os.path.join(ROOT, "build", "excitebike-proof")
FILES = ("excitebike.asm", "front.inc", "video.inc", "world.inc", "game.inc", "vga.inc",
         "cga.inc", "herc.inc", "sprite.inc", "sim.inc", "input.inc", "hud.inc", "ai.inc", "flow.inc", "audio.inc")


def at(p):
    return os88build.at(p)


def symbols():
    """Every x?_ label and VAR of the package, by assembling it with a table of
    `dw label` on the end (the guest's own addresses, no listing parsing)."""
    d = os.path.join(ROOT, "apps", "excitebike")
    src = open(os.path.join(d, "excitebike.asm")).read()
    names = re.findall(r"^VAR (x[a-z]_\w+),", src, re.M)
    for f in FILES:
        names += re.findall(r"^(x[a-z]_\w+)(?::| equ \$)", open(os.path.join(d, f)).read(), re.M)
    tab = open(at("build/excitebike-art/exbtables.inc")).read()
    names += re.findall(r"^(exb_skip_\w+|exb_full_\w+):", tab, re.M)
    names = sorted(set(names))
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, "probe.asm")
        b = os.path.join(td, "probe.bin")
        open(p, "w").write(src + "\n" + "\n".join("dw " + n for n in names))
        subprocess.run(["nasm", "-f", "bin", "-I", os.path.join(ROOT, "apps") + "/",
                        "-I", d + "/", "-I", at("build/excitebike-art") + "/",
                        "-o", b, p], cwd=ROOT, check=True)
        raw = open(b, "rb").read()
        return dict(zip(names, struct.unpack("<%dH" % len(names), raw[-2 * len(names):])))


def nearest(palette):
    cache = {}

    def f(rgb):
        if rgb not in cache:
            cache[rgb] = min(range(len(palette)), key=lambda i: sum(
                (a - b) ** 2 for a, b in zip(palette[i], rgb)))
        return cache[rgb]
    return f


class Game:
    """One guest, booted, with the package open and (after enter()) inside its
    bracket.  Every read is of a PAUSED machine; a run is `go()`."""

    def __init__(self, ui, tag, sym, ref):
        self.ui, self.m, self.tag, self.sym, self.ref = ui, ui.m, tag, sym, ref
        w = ui.window("8BitBike")
        raw = self.m.read(self.m.sym("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
        self.base = struct.unpack_from("<H", raw, w.i * G.WIN_SIZE + G.W_SEG)[0] << 4

    # ---- guest memory ----------------------------------------------------------
    def a(self, n):
        return self.base + self.sym[n]

    def rd(self, n, size=1):
        return self.m.read(self.a(n), size)

    def b(self, n):
        return self.rd(n)[0]

    def w(self, n):
        return struct.unpack("<H", self.rd(n, 2))[0]

    def put(self, n, v, size=None):
        if isinstance(v, int):
            v = bytes([v]) if size in (None, 1) else struct.pack("<H", v)
        self.m.write(self.a(n), bytes(v))

    # ---- driving ---------------------------------------------------------------
    def enter(self, sim=False, flow=False, refuse_big_claim=False):
        """Into the race loop.  The wave-2 video and scroll gates drive the scripted
        scroll test (`xb_simon` = 0: the rider simulation is off and they own the bike
        records, the scroll and the clock); the wave-3 gates pass sim=True.  Both use
        the RAW loop (`xb_noflow` = 1: no title, no countdown, no results); the wave-4
        flow gate passes flow=True and gets the game's own front end."""
        m = self.m
        if not flow:
            self.put("xb_noflow", 1)
        if refuse_big_claim:
            # the machine that refuses the sprite loader's FIRST (28KB) claim: stop right after that call and
            # make its answer CF=1, so the 17KB fallback is taken for real (SPEC.md 102.8.3; it was reported as
            # a failed load until wave 7, and nothing had ever run it)
            m.pause()
            m.bp_exec(self.a("xs_claim1"))
        m.key("Enter", up=False)
        M.pace(m, .04)
        m.key("Enter", down=False)
        if refuse_big_claim:
            m.run()
            assert m.wait_stop(120) == "breakpoint", "the sprite loader's first claim was never reached"
            m.setreg("flags", m.regs()["flags"] | 1)
            m.bp_exec()
            m.run()
        if flow:
            M.until(m, lambda _: self.b("xb_fs") == 1 and self.b("xb_state") == 0 and self.w("xb_idle") > 2,
                    "the title screen is up", guest=90)
            assert self.b("xb_error") == 0, "the bracket reported an error"
            return
        M.until(m, lambda _: self.b("xb_fs") == 1 and self.w("xb_frames") > 3,
                "the race loop is running", guest=90)
        assert self.b("xb_error") == 0, "the bracket reported an error"
        if not sim:
            self.put("xb_simon", 0)
            # the wave-2 state exactly: one stationary bike (the sim's own rider was
            # drawn for the frames before the switch, and its box sits on other rows)
            self.set_bikes([{"pose": 0, "x": 88, "y": 100}])

    def go(self, frames, limit=60):
        """Run exactly `frames` rendered frames, then hold (tpause), let the card
        rasterise a few whole frames of the final state, pause the machine."""
        m = self.m
        self.put("xb_tframes", frames, 2)
        self.put("xb_tpause", 0)
        m.run()
        M.until(m, lambda _: self.b("xb_tpause") == 1, "%d frames rendered" % frames,
                guest=limit)
        M.guest_sleep(m, .12)
        m.pause()

    def jump(self, col):
        self.put("xb_tcol", col, 2)
        self.put("xb_tset", 1)

    def set_bikes(self, bikes):
        raw = bytearray(6 * 8)
        for i, b in enumerate(bikes):
            raw[i * 8:i * 8 + 8] = struct.pack("<BBhHBB", 1, b["pose"], b["x"], b["y"],
                                               b.get("rider", 10), 0)
        self.put("xb_bikes", raw)

    def state(self):
        S = self.w("xb_shownS")
        hud = bytes(self.rd("xb_hudcur", 20)).decode("latin1")
        raw = self.rd("xb_bikes", 48)
        bikes = []
        for i in range(6):
            act, pose, x, y, rider, _ = struct.unpack_from("<BBhHBB", raw, i * 8)
            if act:
                bikes.append({"pose": pose, "x": x, "y": y, "rider": rider})
        return S, hud, bikes

    # ---- pictures --------------------------------------------------------------
    def theme(self):
        return self.b("xb_theme")

    def screen_slots(self):
        """The card's rendering as 200 rows x 320 palette slots (VGA)."""
        w, h, px = self.m.fbuf(0)
        assert (w, h) == (640, 400), ("the 0Dh framebuffer is line-doubled", w, h)
        pal = X.slot_rgb(self.ref.art, self.theme())
        assert len(set(pal)) == 16, "a theme with two slots of one colour cannot be told apart"
        f = nearest(pal)
        return [[f(tuple(px[((2 * y) * w + 2 * x) * 3:((2 * y) * w + 2 * x) * 3 + 3]))
                 for x in range(320)] for y in range(200)]

    def screen(self):
        return self.screen_herc() if self.tag == "herc" else (
            self.screen_cga() if self.tag == "cga" else self.screen_slots())

    def screen_herc(self):
        """The Hercules memory decoded through the ring the card scans: 200 rows of 360 game
        pixels, each the 2-bit PAIR of card pixels it is made of, at the CRTC's start address."""
        raw = self.m.read(0xB0000, 0x8000)
        S = self.w("xb_shownS")
        img = []
        for y in range(200):
            base = (y & 3) * 0x2000
            row = []
            for c in range(90):
                b = raw[base + ((y >> 2) * 90 + 2 * S + c) % 8192]
                row += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
            img.append(row)
        return img

    def screen_cga(self):
        """The CGA memory decoded through the ring the card scans: 200 rows of
        320 inks at the CRTC's start address."""
        raw = self.m.read(0xB8000, 0x4000)
        S = self.w("xb_shownS")
        img = []
        for y in range(200):
            base = (y & 1) * 0x2000
            row = []
            for c in range(80):
                b = raw[base + ((y >> 1) * 80 + 2 * S + c) % 8192]
                row += [(b >> 6) & 3, (b >> 4) & 3, (b >> 2) & 3, b & 3]
            img.append(row)
        return img


def diff_report(got, exp, what, keep=None):
    W = len(exp[0])
    bad = [(x, y) for y in range(200) for x in range(W) if got[y][x] != exp[y][x]]
    if bad and keep:
        os.makedirs(OUT, exist_ok=True)
        rgb = lambda img: [c for row in img for v in row for c in (v * 16, v * 16, v * 16)]
        M.write_png_rgb(os.path.join(OUT, keep + "-got.png"), W, 200, bytes(rgb(got)))
        M.write_png_rgb(os.path.join(OUT, keep + "-exp.png"), W, 200, bytes(rgb(exp)))
    if bad:
        xs = [x for x, y in bad]
        ys = [y for x, y in bad]
        x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
        print("difference box x %d..%d y %d..%d" % (x0, x1, y0, y1))
        for y in range(y0, min(y1, y0 + 30) + 1):
            print("%3d %s | %s" % (y, "".join("%x" % got[y][x] for x in range(x0, min(x1, x0 + 60) + 1)),
                                  "".join("%x" % exp[y][x] for x in range(x0, min(x1, x0 + 60) + 1))))
    assert not bad, "%s: %d pixels differ from the reference renderer, first %s (got %s, want %s)" % (
        what, len(bad), bad[:6], [got[y][x] for x, y in bad[:6]], [exp[y][x] for x, y in bad[:6]])


def compare(g, what, keep=None):
    if cgaish(g.tag):
        # a frame whose retrace never came committed nothing (SPEC.md 102.1): run on
        # until the last one landed, so the glass and the state are the same frame
        for _ in range(8):
            if g.b("xc_to") == 0:
                break
            g.go(1)
    S, hud, bikes = g.state()
    ref = g.ref
    exp = expframe(ref, g.tag, g.cids, S, bikes, hud)
    got = g.screen()
    if g.tag == "vga":
        # the card shows the HUD row from address 0 by line compare
        pass
    diff_report(got, exp, "%s (S=%d, %d bikes)" % (what, S, len(bikes)), keep or "%s-fail" % g.tag)
    return S


def herc_glass(g, what):
    """WAVE 6: the Hercules's picture as the CARD renders it (the framebuffer, not the memory the other
    checks decode): the field is 200 lines and nothing else is lit (`xh_crtc` shows 50 rows of the 87
    the desktop mode does, SPEC.md 102.7.1), and it sits inside the tube, not against its top edge.  ->
    the span of lit lines, first and last."""
    w, h, px = g.m.fbuf(0)
    lit = [y for y in range(h) if any(px[(y * w + x) * 3] for x in range(w))]
    assert len(lit) > 100, ("%s: the card shows next to nothing" % what, len(lit))
    return lit[0], lit[-1]


def herc_glass_control(g):
    """The check above must be able to fail: put the desktop mode's R6 and R7 back (87 rows and the sync
    after them) through the 6845's ports and the rows below the field - the sheared ring's other rows -
    appear; then put ours back."""
    m = g.m
    top, bot = herc_glass(g, "before the control")
    assert bot - top + 1 <= 200 and top > 20, ("the field is not 200 lines mid-tube", top, bot)
    m.pause()
    for reg, val in ((6, 87), (7, 87)):
        m.outb(0x3b4, reg)
        m.outb(0x3b5, val)
    m.run()
    M.guest_sleep(m, .3)
    m.pause()
    t2, b2 = herc_glass(g, "the control")
    assert b2 - t2 + 1 > 200, ("the control did not widen the picture: the check cannot fail", t2, b2)
    for reg, val in ((6, 50), (7, 68)):
        m.outb(0x3b4, reg)
        m.outb(0x3b5, val)
    m.run()
    M.guest_sleep(m, .3)
    m.pause()
    t3, b3 = herc_glass(g, "after the control")
    assert (t3, b3) == (top, bot), ("R6/R7 put back did not restore the picture", (top, bot), (t3, b3))
    print("herc", "the card's own picture: the field is lines %d-%d (%d lines) and nothing else is lit; with the "
          "desktop mode's R6/R7 the picture is lines %d-%d, so the check can fail" % (top, bot, bot - top + 1, t2, b2),
          flush=True)
    m.run()


def random_bikes(rnd, tag):
    out = []
    for _ in range(rnd.choice((1, 1, 2, 3))):
        pose = rnd.randrange(28)             # the 24 poses and the four effects (shadow, dust)
        x = rnd.randrange(0, 320 - 32, 2) if tag == "vga" else rnd.randrange(8, WPX[tag] - 40, 2)
        if rnd.randrange(4) == 0:
            x = rnd.randrange(-22, WPX[tag] - 1, 2)      # ...hanging off either edge (SPEC.md 102.7.5)
        out.append({"pose": pose, "x": x, "y": rnd.randrange(48, 160),
                    "rider": rnd.choice((10, 12, 13))})
    return out


def overlap(g, ref, tag, quick):
    """WAVE 4: bikes that OVERLAP.  The opponents pass the rider and each other, and the CGA composes a
    box from the world alone, so the back end draws into every box every bike whose footprint meets it,
    in record order (SPEC.md 102.4): a fresh window with two and three bikes on top of each other, and
    then bikes moving ACROSS each other frame by frame (the erase of one over the other's pixels, the
    box of one carrying the other) - each compared with the reference renderer, which draws in record
    order.  The VGA draws sprites over the page in record order and must equal it too."""
    rnd = random.Random(0x4F1)
    smax = g.w("xb_Smax")
    lo, hi = (8, WPX[tag] - 40) if cgaish(tag) else (0, 320 - 32)
    g.put("xb_tspeed", 0, 2)
    scenes = 6 if quick else 16
    for i in range(scenes):
        x0 = rnd.randrange(lo + 16, hi - 16, 2)
        y0 = rnd.randrange(56, 130)
        bikes = []
        for k in range(rnd.choice((2, 3, 3))):
            bikes.append({"pose": rnd.randrange(28), "x": max(lo, min(hi, x0 + rnd.randrange(-14, 15, 2))),
                          "y": y0 + rnd.randrange(-12, 13), "rider": rnd.choice((10, 12, 13))})
        g.set_bikes(bikes)
        g.jump(rnd.randrange(0, smax + 1))
        g.go(4)
        compare(g, "overlap scene %d (%d bikes) on a fresh window" % (i, len(bikes)))
        # ...and a bike sliding across the others, a frame at a time, the window scrolling a little
        g.put("xb_tspeed", rnd.choice((0, 0x0400)), 2)
        for f in range(4 if quick else 8):
            b = bikes[f % len(bikes)]
            b["x"] = max(lo, min(hi, b["x"] + rnd.randrange(-6, 7, 2)))
            b["y"] = max(48, min(150, b["y"] + rnd.randrange(-5, 6)))
            g.set_bikes(bikes)
            g.go(1)
        g.put("xb_tspeed", 0, 2)
        g.go(3)
        compare(g, "overlap scene %d after the bikes crossed" % i)
    print(tag, "overlapping bikes: %d scenes fresh and moving across each other: pixel-identical to the "
          "reference renderer (record order)" % scenes, flush=True)


def run(tag, quick, small=False):
    ref = exbsim.Ref()
    sym = symbols()
    system = at("build/os8088-360.img")
    disk = at("build/excitebike360.img")
    with os88ui.boot(system, apps=disk, machine=MACHINE[tag]) as ui:
        m = ui.m
        ui.open_drive("B")
        ui.settle()
        ui.open("8BITBIKE.O88")
        ui.settle()
        g = Game(ui, tag, sym, ref)
        g.cids = ref.course_cids(0)
        M.until(m, lambda _: g.w("xb_reveal") >= g.w("xb_frontheight") > 0,
                "the splash reveal finishes", guest=30)
        ui.settle()
        g.enter(refuse_big_claim=small)
        print(tag, "race loop running, n =", g.w("xb_n"), flush=True)
        if small:
            assert g.w("xb_sprkb") == 17 and g.b("xb_nspr") == 28 and g.b("xb_error") == 0, (
                "the small claim was not the one taken", g.w("xb_sprkb"), g.b("xb_nspr"), g.b("xb_error"))
            m.pause()
            assert sum(struct.unpack("<56H", g.rd("xs_ctab", 112))) == 0, "poses were compiled into a claim without the room"
            m.run()
            print(tag, "the sprite loader's 28KB claim refused: the 17KB fallback loaded 28 poses, none compiled", flush=True)
        check_blobs(g, ref, tag)
        rnd = random.Random(0x88)
        smax = g.w("xb_Smax")
        if tag == "herc":
            g.set_bikes([{"pose": 0, "x": 88, "y": 100}])
            g.jump(300)
            g.go(6)
            herc_glass_control(g)
        # ---- 1: twenty random positions
        for i in range(20):
            col = rnd.randrange(0, smax + 1)
            bikes = random_bikes(rnd, tag)
            g.put("xb_tspeed", 0, 2)
            g.set_bikes(bikes)
            g.jump(col)
            g.go(4)
            compare(g, "position %d full window at %d" % (i, col))
            speed = rnd.choice((0x0400, 0x0800, 0x0C00, 0x1000))
            g.put("xb_tspeed", speed, 2)
            g.set_bikes(random_bikes(rnd, tag))
            g.go(rnd.randrange(3, 9))
            # a CGA frame whose retrace never came commits nothing and catches up on
            # the next (SPEC.md 102.1): let the window stop and land before the read
            g.put("xb_tspeed", 0, 2)
            g.go(4)
            compare(g, "position %d after scrolling at %#x" % (i, speed))
            if quick and i >= 3:
                break
        print(tag, "20 random positions (full window + incremental, 1-3 bikes): pixel-identical", flush=True)
        # ---- 2: the erase invariant's negative control: forget a page's footprints,
        # move the bike, and the residue MUST show (a gate that cannot fail proves nothing)
        negative_control(g, ref, tag)
        if tag == "vga":
            skip_control(g, ref)
        # ---- 2a (wave 4): bikes on top of each other
        overlap(g, ref, tag, quick)
        edges(g, ref, tag, quick)
        if not small:                # (the small claim has no compiled code to break)
            compiled_control(g, ref, tag)
        # ---- 2b (wave 3): the rider moving over both laps, before the synthetic course
        # overwrites the column array
        race(g, ref, tag, quick)
        # ---- 3: the whole course, then the largest race the compiler allows
        long_scroll(g, rnd, tag, "course 1", quick)
        synthetic(g, ref, rnd, tag, quick)


def edges(g, ref, tag, quick):
    """WAVE 6: bikes hanging off the picture's edges (SPEC.md 102.7.5).  The clipping blitters (the CGA
    engine's xc_drawc into a box that stops at the edge, the VGA's record-skipping loop) draw a bike whose bytes lie partly off either edge into a box that stops at the edge: a fresh
    window with one, two and three bikes on the left, the right and both edges (pose, phase and row
    chosen at random), and then a bike SLIDING in from beyond each edge across the whole 24 pixels
    that show, a frame at a time, the window scrolling, each compared with the reference renderer
    (which clips at the pixel: the edges are byte boundaries, so the two must agree exactly)."""
    rnd = random.Random(0xED6E)
    W = WPX[tag]
    smax = g.w("xb_Smax")
    g.put("xb_tspeed", 0, 2)
    scenes = 6 if quick else 24
    for i in range(scenes):
        bikes = []
        for k in range(rnd.choice((1, 2, 3))):
            x = rnd.choice((rnd.randrange(-22, 6, 2), rnd.randrange(W - 30, W - 1, 2)))
            bikes.append({"pose": rnd.randrange(28), "x": x, "y": rnd.randrange(48, 160),
                          "rider": 10})
        g.set_bikes(bikes)
        g.jump(rnd.randrange(0, smax + 1))
        g.go(4)
        compare(g, "edge scene %d (%s) on a fresh window" % (i, [b["x"] for b in bikes]))
        if i < 4:
            # the scene is not vacuous: the reference picture WITH the bikes differs from the one without
            S, hud, live = g.state()
            with_ = expframe(ref, tag, g.cids, S, live, hud)
            without = expframe(ref, tag, g.cids, S, [], hud)
            shown = sum(1 for y in range(200) for x in range(W) if with_[y][x] != without[y][x])
            assert shown > 15, ("an edge scene with nothing of the bikes to see", shown)
    for side in (0, 1):
        pose = rnd.choice((0, 1, 12))
        y = rnd.randrange(60, 130)
        xs = list(range(-24, 14, 2)) if side == 0 else list(range(W - 14, W + 2, 2))
        if side == 0:
            xs = xs
        else:
            xs = xs[::-1] if False else xs
        g.put("xb_tspeed", 0x0400, 2)
        for x in xs:
            g.set_bikes([{"pose": pose, "x": x, "y": y, "rider": 10},
                         {"pose": 3, "x": W // 2, "y": 140, "rider": 10}])
            g.go(1)
            if len(xs) > 12 and (xs.index(x) % 4 == 0 or quick):
                g.put("xb_tspeed", 0, 2)
                g.go(3)
                compare(g, "a bike sliding past the %s edge at x=%d" % ("left" if side == 0 else "right", x))
                g.put("xb_tspeed", 0x0400, 2)
        g.put("xb_tspeed", 0, 2)
        g.go(3)
        compare(g, "after sliding past the %s edge" % ("left" if side == 0 else "right"))
    print(tag, "edge riders: %d fresh scenes and a bike sliding across each edge: pixel-identical to the "
          "reference renderer" % scenes, flush=True)


def compiled_control(g, ref, tag):
    """WAVE 6: the compiled poses are what draws pose 0 at phase 0 (a bike at x = 88), so a wrong immediate in
    ITS code must show in the picture - and the picture is right again when it is put back.  A gate that draws
    through the compiled code and compares nothing that could differ proves nothing."""
    ink = ref.herc_ink if tag == "herc" else None
    ctab, code = (exbsim.compiled_poses(ref.art, ink) if cgaish(tag) else exbsim.vga_compiled(ref.art))
    base = len(exbsim.cga_blob_bytes(ref.art, ink) if cgaish(tag) else exbsim.vga_blob_bytes(ref.art))
    start = ctab[0] - base
    # the first instruction's last byte is an immediate (mov byte/word [es:di+d], imm  or  mov al, imm)
    if cgaish(tag):
        i = start
        while not (code[i] == 0x26 and code[i + 1] in (0xc6, 0xc7)):
            i += 1
        at_ = i + (4 if code[i + 2] == 0x45 else 3)          # the first immediate byte
    else:
        i = start
        while not (code[i] == 0xb0 and code[i + 2] == 0xee):
            i += 1
        at_ = i + 1                                           # the first mask
    seg = g.w("xb_sprseg") << 4
    addr = seg + ctab[0] + (at_ - start)
    was = g.m.read(addr, 1)
    g.put("xb_tspeed", 0, 2)
    g.set_bikes([{"pose": 0, "x": 88, "y": 100}])
    g.jump(120)
    g.m.pause()
    g.m.write(addr, bytes([was[0] ^ 0xFF]))
    g.go(6)
    S, hud, bikes = g.state()
    exp = expframe(ref, tag, g.cids, S, bikes, hud)
    got = g.screen()
    bad = sum(1 for y in range(200) for x in range(WPX[tag]) if got[y][x] != exp[y][x])
    g.m.pause()
    g.m.write(addr, was)
    assert bad > 0, "a wrong immediate in the compiled pose changed nothing: the gate cannot see the compiled code"
    g.jump(122)                                   # (an unchanged bike under a window that did not move is skipped whole)
    g.go(6)
    compare(g, "the compiled pose put back")
    print(tag, "negative control: one wrong byte in the compiled code of pose 0 leaves %d wrong pixels, "
          "and none once it is put back" % bad, flush=True)


def check_blobs(g, ref, tag):
    """The sprite claim the guest's loader built is the one the model computes from
    the pose text: every byte of the table and the blobs."""
    m = g.m
    m.pause()
    seg = g.w("xb_sprseg")
    used = g.w("xb_sprused")
    ink = ref.herc_ink if tag == "herc" else None
    exp = (exbsim.cga_blob_bytes(ref.art, ink) if cgaish(tag) else exbsim.vga_blob_bytes(ref.art))
    code = b""
    ctab = None
    # the compiled hot poses ride after the blobs (SPEC.md 102.7.4) when the claim has the room
    if cgaish(tag):
        ctab, code = exbsim.compiled_poses(ref.art, ink)
        if g.w("xb_sprkb") < 28:
            ctab, code = [0] * len(ctab), b""
    else:
        ctab, code = exbsim.vga_compiled(ref.art)
    assert used == len(exp) + len(code), ("the loader's blob size differs from the model's", used, len(exp), len(code))
    got = m.read(seg << 4, used)
    want = exp + code
    if got != want:
        i = next(i for i in range(used) if got[i] != want[i])
        raise AssertionError("sprite claim differs at byte %d (%s): %s vs %s" % (
            i, "blob" if i < len(exp) else "compiled code", got[i:i + 12].hex(), want[i:i + 12].hex()))
    if ctab is not None:
        have = list(struct.unpack("<%dH" % len(ctab), g.rd("xs_ctab", 2 * len(ctab))))
        assert have == ctab, ("the compiled-pose table differs from the model's", have, ctab)
    kb = g.w("xb_sprkb")
    print(tag, "sprite claim: %d bytes of %d KB (%d of blobs, %d of compiled code for %d pose-phases), "
          "identical to the model" % (used, kb, len(exp), len(code),
                                      sum(1 for v in (ctab or []) if v)), flush=True)
    m.run()


def race(g, ref, tag, quick):
    """WAVE 3: the rider moving.  The reference rider drives course 1 through the guest's
    own simulation (the harness feeds the steps, 102.6) and at chosen steps - a flat run,
    the middle of a jump, and one every ~250 steps along both laps - the picture on the
    card is compared, pixel for pixel, with the reference renderer drawing the MODEL's
    bike records and HUD; a second script rides straight into the hurdles for a CRASH.
    That is the ERASE INVARIANT with sprites that move every frame (a bike, its shadow,
    the dust puff over ramps and landings, the gauge): the incrementally maintained
    window must equal a fresh redraw.  Three frames of each state are compared (the VGA
    has three pages and each must hold the same picture), and the guest's bike records
    and HUD string must equal the model's.  The three named states are saved as PNGs
    (build/excitebike-proof/<adapter>-{flat,ramp,crash}.png)."""
    import excitebike_ref as R
    art = ref.art
    lap = R.bot_script(art, 0)
    # a rider that never steers: flat out into whatever is in its lane, a crash
    # (flat out from the line: lap 2's high hurdle in lane 26 is the first thing it hits, at
    # step ~2227, after the engine has overheated and stalled once)
    straight = [exbsim.INP_A | exbsim.INP_B] * 2400
    total = 0
    for name, script, want in (("reference rider", lap, ("flat", "ramp")),
                               ("straight into the hurdles", straight, ("crash",))):
        total += race_script(g, ref, tag, quick, name, script, want)
    g.put("xb_tmode", 0)
    g.put("xb_simon", 0)


def race_script(g, ref, tag, quick, name, script, want):
    import excitebike_ref as R
    art = ref.art
    sim = exbsim.Sim(art, 0, cols=COLS[tag])
    probe = exbsim.Sim(art, 0, cols=COLS[tag])
    picks = {}
    for i, inp in enumerate(script):
        probe.step(inp)
        n = i + 1
        if "flat" in want and "flat" not in picks and n > 200 and probe.mode == 0 \
                and probe.speed >= 0x300 and probe.pitch == 0 and probe.sc == 0 and probe.th == 0 \
                and probe.scr is None:
            picks["flat"] = n
        if "ramp" in want and "ramp" not in picks and probe.mode == 1 and probe.A >= 0x0800 \
                and abs(probe.vy) < 0x40:
            picks["ramp"] = n
        if "crash" in want and "crash" not in picks and probe.mode == 2 \
                and probe.mtimer == exbsim.PHYS["CRASH_STEPS"] - 12:
            picks["crash"] = n
    assert set(picks) == set(want), "the %s script never reached %s" % (name, set(want) - set(picks))
    every = 500 if quick else 250
    marks = sorted(set(list(picks.values()) + list(range(every, len(script), every)) + [len(script)]))
    names = {v: k for k, v in picks.items()}
    g.put("xb_simon", 1)
    g.put("xb_tmode", 1)
    g.put("xb_tsn", 0, 2)
    g.put("xb_tsi", 0, 2)
    g.put("xb_treset", 1)
    g.put("xb_tpause", 0)
    g.m.run()
    M.until(g.m, lambda _: g.b("xb_tpause") == 1 and g.b("xb_treset") == 0, "a fresh rider",
            guest=30)
    g.m.pause()
    done = 0
    checked = 0
    os.makedirs(OUT, exist_ok=True)
    for mark in marks:
        while done < mark:
            chunk = script[done:min(mark, done + 256)]
            g.put("xb_tscript", bytes(chunk) + bytes(256 - len(chunk)))
            g.put("xb_tsn", len(chunk), 2)
            g.put("xb_tsi", 0, 2)
            g.put("xb_tpause", 0)
            g.m.run()
            M.until(g.m, lambda _: g.b("xb_tpause") == 1, "%d steps" % len(chunk), guest=120)
            g.m.pause()
            for inp in chunk:
                sim.step(inp)
            done += len(chunk)
        # the state is on the glass; look at it three times (three VGA pages)
        for look in range(3):
            if look:
                g.go(1)
            S, hud, bikes = g.state()
            assert hud.strip("\xff") == sim.hud(), "HUD at step %d: %r vs model %r" % (
                mark, hud, sim.hud())
            exp = [b for b in sim.bikes(cga=cgaish(tag)) if b]
            have = [{"pose": b["pose"], "x": b["x"], "y": b["y"]} for b in bikes]
            assert have == exp, "bike records at step %d: guest %r, model %r" % (mark, have, exp)
            if look == 0 and mark in names:
                compare_keep(g, "race step %d (%s)" % (mark, names[mark]),
                             "%s-%s" % (tag, names[mark]))
            else:
                compare(g, "race step %d, look %d" % (mark, look))
            checked += 1
    print(tag, "race (%s): %d comparisons over %d steps, erase invariant holds with the rider, "
          "its shadow and dust moving every frame (%s)" % (
              name, checked, len(script), ", ".join("%s at step %d" % kv for kv in picks.items())),
          flush=True)
    return checked


def compare_keep(g, what, keep):
    """compare() and keep the card's picture as evidence (a PNG of what was on the glass)."""
    compare(g, what)
    got = g.screen()
    ref = g.ref
    if g.tag == "herc":
        rgb = [(0, 0, 0), (128, 128, 128), (128, 128, 128), (255, 255, 255)]
    else:
        rgb = X.CGA_RGB_HIGH if g.tag == "cga" else X.slot_rgb(ref.art, g.theme())
    rows = bytes(c for row in got for v in row for c in rgb[v])
    M.write_png_rgb(os.path.join(OUT, keep + ".png"), WPX[g.tag], 200, rows)


def negative_control(g, ref, tag):
    g.put("xb_tspeed", 0, 2)
    g.set_bikes([{"pose": 3, "x": 60, "y": 90}])
    g.jump(200)
    g.go(6)
    compare(g, "before the control")
    fp = "xc_fp" if cgaish(tag) else "xv_fpn"
    n = 32 if cgaish(tag) else 3
    g.put(fp, bytes(n))                              # every page forgets what it drew
    g.set_bikes([{"pose": 3, "x": 200, "y": 120}])
    g.go(6)
    S, hud, bikes = g.state()
    exp = expframe(ref, tag, g.cids, S, bikes, hud)
    got = g.screen()
    bad = sum(1 for y in range(200) for x in range(WPX[tag]) if got[y][x] != exp[y][x])
    assert bad > 50, "the negative control found no residue (%d): the erase check cannot fail" % bad
    print(tag, "negative control: forgotten footprints leave %d wrong pixels, as they must" % bad, flush=True)
    g.set_bikes([{"pose": 0, "x": 88, "y": 100}])
    g.jump(0)
    g.go(6)                                          # the full-window path cleans the residue up
    compare(g, "after the control")


def skip_control(g, ref):
    """VGA: the skip lists are what lets an entering plain column write ~1/4 of its
    band, so the gate must SEE a wrong one.  Cut every band list to nothing (a run
    list that ends at once) and scroll over plain ground: the band lines the
    lists were meant to write are never written and the picture must differ."""
    tab = g.rd("exb_skip_band", 18)
    ptrs = struct.unpack("<9H", tab)
    saved = {p_: g.m.read(g.base + p_, 2) for p_ in ptrs}    # (equal lists share one label)
    for p_ in saved:
        g.m.write(g.base + p_, b"\xff\xff")
    g.put("xb_tspeed", 0, 2)
    g.set_bikes([])
    g.jump(0)
    g.go(6)                                          # full window: no skip involved
    g.put("xb_tspeed", 0x0800, 2)
    g.go(9)                                          # ~10 plain columns entering
    S, hud, bikes = g.state()
    exp = ref.frame(g.cids, S, bikes, hud.strip("\xff"))
    got = g.screen_slots()
    bad = sum(1 for y in range(200) for x in range(320) if got[y][x] != exp[y][x])
    for p_, b in saved.items():
        g.m.write(g.base + p_, b)
    assert bad > 500, "cutting the skip lists left the picture right (%d): they are not exercised" % bad
    print("vga", "negative control: emptied skip lists leave %d wrong pixels, as they must" % bad, flush=True)
    g.put("xb_tspeed", 0, 2)
    g.set_bikes([{"pose": 0, "x": 88, "y": 100}])
    g.jump(0)
    g.go(6)
    compare(g, "after the skip control")


def long_scroll(g, rnd, tag, what, quick):
    smax = g.w("xb_Smax")
    g.put("xb_tspeed", 0, 2)
    g.set_bikes([{"pose": 1, "x": 88, "y": 100}])
    g.jump(0)
    g.go(4)
    compare(g, "%s: the start" % what)
    speeds = (0x0400, 0x0800, 0x1000, 0x1800, 0x0800)
    k = 0
    samples = 0
    while g.w("xb_S") < smax:
        g.put("xb_tspeed", speeds[k % len(speeds)], 2)
        k += 1
        g.go(rnd.choice((83, 89, 97, 101)) if not quick else 40, limit=120)
        compare(g, "%s: after %d chunks" % (what, k))
        samples += 1
        if quick and samples >= 6:
            break
    if not quick:
        g.put("xb_tspeed", 0x1800, 2)
        g.go(200, limit=240)
        compare(g, "%s: held at the end" % what)
        assert g.w("xb_S") == smax
    print(tag, "%s: %d samples along the whole course, identical (S reached %d of %d)" % (
        what, samples, g.w("xb_S"), smax), flush=True)


def synthetic(g, ref, rnd, tag, quick):
    """1,637 columns of every dictionary column in random order: the largest
    race the compiler accepts (43 + 2 x 797), which no shipped course is."""
    ncols = 1637
    ids = [rnd.randrange(len(ref.art.col_order)) for _ in range(ncols)]
    for i in range(0, ncols, 64):
        ids[i:i + 6] = [0, 1, 2, 0, 1, 2][:len(ids[i:i + 6])]   # plain runs among them
    g.m.write(g.a("xb_cid"), bytes(ids))
    g.put("xb_ncols", ncols, 2)
    g.put("xb_Smax", ncols - COLS[tag], 2)
    g.cids = ids
    long_scroll(g, rnd, tag, "synthetic 1,637 columns", quick)


def read_ppm(path):
    raw = open(path, "rb").read()
    parts = raw.split(b"\n", 3)
    assert parts[0] == b"P6", parts[0]
    w, h = (int(v) for v in parts[1].split())
    assert parts[2] == b"255"
    return w, h, parts[3]


def qemu_g1():
    """GATE G1 on QEMU's VGA, the emulator that implements the real one: the
    line compare (the HUD row starts on scan line 384 and nowhere else), the
    start-address flip and the palette, compared PIXEL FOR PIXEL with the reference
    renderer.  The game is frozen with its own `p` key, so the display is static and
    the guest's state is read back with pmemsave."""
    import ethernet as E
    import os88qemu
    import os88qemu as Q
    import os88sym
    import dispcp
    os88qemu.own()
    subprocess.run(["make", "test", "TESTIMG=build/os8088-360.img",
                    "TESTAPPS=build/excitebike360.img"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    m = E.Qemu()
    mo = E.Mouse()
    S = os88sym.linear
    ref = exbsim.Ref()
    sym = symbols()
    try:
        Q.pace(m, 8)
        E.settle(m)
        dispcp.open_drive(m, mo, S, E.settle, "B")
        win = next(w for w in G.windows(m, S) if w.visible and w.title == "Disk")
        dispcp.open_named(m, mo, S, E.settle, win.x, win.y, "8BITBIKE.O88")
        win = next(w for w in G.windows(m, S) if w.visible and w.title == "8BitBike")
        raw = m.read(S("wm_wins"), G.MAX_WIN * G.WIN_SIZE)
        base = struct.unpack_from("<H", raw, win.i * G.WIN_SIZE + G.W_SEG)[0] << 4
        E.settle(m)
        m.hmp("sendkey ret")
        Q.pace(m, 4)
        assert m.read(base + sym["xb_fs"], 1) == b"\x01", "QEMU did not enter the bracket"
        assert m.read(base + sym["xb_error"], 1) == b"\x00"
        # the game's own front end: Enter on the title, on the mode, on the course; then
        # the race's countdown, and `p` pauses it (xb_paused) so the display is static
        for _ in range(3):
            m.hmp("sendkey ret")
            Q.pace(m, 2)
        assert m.read(base + sym["xb_state"], 1) == b"\x03", "not in the race"
        m.hmp("sendkey p")
        Q.pace(m, 1.2)
        assert m.read(base + sym["xb_paused"], 1) == b"\x01"
        path = os.path.join(ROOT, "build", "excitebike-proof", "vga-qemu.ppm")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        m.hmp('screendump "%s"' % path)
        w, h, px = read_ppm(path)
        Sx = struct.unpack("<H", m.read(base + sym["xb_shownS"], 2))[0]
        hud = m.read(base + sym["xb_hudcur"], 20).decode("latin1")
        theme = m.read(base + sym["xb_theme"], 1)[0]
        braw = m.read(base + sym["xb_bikes"], 48)
        bikes = []
        for i in range(6):
            act, pose, x, y, rider, _ = struct.unpack_from("<BBHHBB", braw, i * 8)
            if act:
                bikes.append({"pose": pose, "x": x, "y": y, "rider": rider})
        print("QEMU VGA frame %dx%d, S=%d, HUD %r, theme %d" % (w, h, Sx, hud, theme))
        scale = w // 320
        assert w in (320, 640) and h == 200 * scale, ("the 0Dh picture's size", w, h)
        pal = X.slot_rgb(ref.art, theme)
        f = nearest(pal)
        got = [[f(tuple(px[((y * scale) * w + x * scale) * 3:((y * scale) * w + x * scale) * 3 + 3]))
                for x in range(320)] for y in range(200)]
        cids = ref.course_cids(0)
        exp = ref.frame(cids, Sx, bikes, hud, cga=False)
        diff_report(got, exp, "QEMU VGA at S=%d" % Sx, "vga-qemu-fail")
        print("QEMU VGA: line compare, start address, palette and sprite: pixel-identical (G1 passes)")
    finally:
        Q.kill()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", choices=("vga", "cga", "herc"))
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--smallclaim", action="store_true",
                    help="refuse the sprite loader's 28KB claim so the 17KB fallback runs (CGA / Hercules), then the pixel gate")
    ap.add_argument("--qemu", action="store_true", help="gate G1 on QEMU's VGA")
    a = ap.parse_args()
    if a.qemu:
        qemu_g1()
        print("excitebike_video (qemu): PASS")
        return
    if not a.adapter:
        ap.error("--adapter or --qemu")
    if a.smallclaim and a.adapter == "vga":
        ap.error("the VGA has no smaller claim")
    run(a.adapter, a.quick, a.smallclaim)
    print("excitebike_video (%s): PASS" % a.adapter)


if __name__ == "__main__":
    main()
