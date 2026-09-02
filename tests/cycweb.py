#!/usr/bin/env python3
"""Does the claw eat the web it slides over? (SPEC.md 67.5.3.1)

    make && python3 tests/cycweb.py [--machine os8088_5150_herc_gla]

67.1's whole scheme is that a mover's erase can never reach the static
playfield, and 67.5.3 says the claw is safe because it sits at CY_TOPD - a
depth OUTSIDE the rim. That is a claim about a RADIAL SCALE, and a scale
separates nothing where the rim edge runs radially: on the flat ribbon the lip
and the rim are the same line, so the claw is drawn ON the web at every window
size, and on the star, the plus, the vee and the triangle the clearance runs
out at ordinary ones.

THE MEASUREMENT IS 67.5.2'S: sweep the claw over every lane and back, then
force a full repaint and diff. Pixels the repaint has and the incremental
screen does not are web the claw ate. Three things make it about the CLAW:

  - the wave is stopped by stubbing `cy_spawn_tick` (not by emptying it - an
    empty wave is a level CLEARED, and the measurement then runs against a
    warp-out with no web on the glass);
  - 67.19's repair is stubbed too, so what is counted is what was eaten
    rather than how fast the repair paints it back;
  - the POINTER is parked at (2,2), because the kernel's arrow is 8x12 of
    somebody else's pixels wherever the last click left it.

Every shape is measured, not just the circle everybody plays first: [cy_shape]
is written and the layout forced, which is what cy_startlevel does.

--climb runs the same sweep with an ENEMY parked at CY_TOPD instead - a fully
ascended climber is on the lip with the player and had the identical problem -
and --depth puts that enemy inside the tube instead.

Measured on a cycle-accurate 5150/CGA: before 67.5.3.1, 221 pixels for the
claw and 125 for the climber; after, 0 and 0, on CGA and on Hercules.
"""
import sys, os, time, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

CYS_TITLE = 0
CYS_PLAY = 2
CY_MAXENEM = 10
SHAPES = ["cy_sh_circle", "cy_sh_square", "cy_sh_plus", "cy_sh_tri",
          "cy_sh_star", "cy_sh_vee", "cy_sh_flat", "cy_sh_uu"]


def pkg_syms(src="apps/cyclone/cyclone.asm", incs=("apps/",), defines=()):
    """The package's symbols AND its image, by re-assembling it.

    paintmove.py's trick, plus the half that had to be added here: the image
    comes back too, so `Pkg` can check it against the one actually RUNNING.
    An edited source and a `make` nobody ran is a set of offsets that read
    plausible rubbish out of the middle of the wrong variable - measured as
    "the warp never finished", ninety seconds after the run started, with the
    game on screen and perfectly fine.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


class Pkg:
    """Read and write the running package's own variables."""
    def __init__(self, m, seg, syms):
        self.m, self.seg, self.s = m, seg, syms

    def addr(self, name):
        return self.seg * 16 + self.s[name]

    def rw(self, name):
        return u16(self.m.read(self.addr(name), 2))

    def rb(self, name):
        return self.m.read(self.addr(name), 1)[0]

    def ww(self, name, v):
        self.m.write(self.addr(name), bytes([v & 0xFF, (v >> 8) & 0xFF]))

    def wb(self, name, v):
        self.m.write(self.addr(name), bytes([v & 0xFF]))


def shot(m, card=None):
    """The screen as one lit/unlit byte per pixel, on whichever adapter."""
    if m.cards()[0]["type"] in ("cga", "mda"):
        w, h, rows = m.vram()
        return w, h, bytes(b for r in rows for b in r)
    w, h, px = m.fbuf(card=card)
    return w, h, bytes(1 if px[i] or px[i + 1] or px[i + 2] else 0
                       for i in range(0, len(px), 3))


def diff(a, b, box):
    """(missing, extra) between incremental `a` and repaint `b`, inside `box`.

    THE BOX IS THE APP'S CONTENT and not the screen: the menu bar carries a
    CLOCK, and a minute turning over between the two captures is four pixels
    of "missing web" 500px from the window.
    """
    w, _, ap = a
    _, _, bp = b
    x0, y0, x1, y1 = box
    miss, extra = [], []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            i = y * w + x
            if bp[i] and not ap[i]:
                miss.append((x, y))
            elif ap[i] and not bp[i]:
                extra.append((x, y))
    return miss, extra


def bbox(pts):
    if not pts:
        return "-"
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return "x %d..%d y %d..%d" % (min(xs), max(xs), min(ys), max(ys))


def sweep(m, p, frames=3, climb=False):
    """Walk the claw (and optionally a climber) over every lane and back."""
    n = p.rw("cy_nlane")
    for lane in list(range(n)) + list(range(n - 1, -1, -1)):
        if climb:                       # the CLIMBER walks; the claw sits
            m.write(p.addr("cy_e_lane"), bytes([lane]))
        else:
            p.ww("cy_plane", lane)
        m.advance(frames=frames)
    return n


def quiet(m, p):
    """Stop the wave: nothing but the claw moves.

    `cy_spawn_tick` is stubbed to a `ret` in the package's own image rather
    than the wave being emptied, because an empty wave is a level CLEARED -
    cy_wave_ck wants cy_wleft AND cy_left at zero - and the measurement then
    runs against a warp-out with no web on the glass at all. It cost the
    first run of this script: 21 "missing" pixels of a screen that was
    already two levels further on.
    """
    m.write(p.addr("cy_spawn_tick"), bytes([0xC3]))
    m.write(p.addr("cy_e_kind"), bytes([0xFF] * CY_MAXENEM))
    p.ww("cy_wleft", 40)                       # ...so the wave never clears
    p.ww("cy_left", 40)


def park_climber(m, p, depth=17):
    """One flipper, fully ascended, parked on the lip beside the claw.

    `cy_enemies_update` is stubbed with it: a live flipper on the rim walks,
    flips and kills, and what that measures is a death sweep and a banner
    rather than the pixels under an arrival. The DRAWING path is untouched -
    this enemy is drawn by cy_play_render exactly as any other, at the lane
    the sweep writes.
    """
    m.write(p.addr("cy_enemies_update"), bytes([0xC3]))
    m.write(p.addr("cy_e_kind"), bytes([0]))            # flipper
    m.write(p.addr("cy_e_lane"), bytes([0]))
    m.write(p.addr("cy_e_dp"), bytes([0, depth]))       # the depth, hi byte


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--shapes", default="all")
    ap.add_argument("--climb", action="store_true")
    ap.add_argument("--depth", type=int, default=17,
                    help="the depth to park that enemy at; 17 is CY_TOPD, the "
                         "lip, and anything less is inside the tube")
    ap.add_argument("--repair", action="store_true",
                    help="leave 67.19's repair running (it HIDES damage: "
                         "one lane every fourth frame, so a sweep measures "
                         "how fast the repair is, not what the claw ate)")
    ap.add_argument("--png", default="")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--src", default="apps/cyclone/cyclone.asm",
                    help="the source the package on --apps was built from; "
                         "point both at an older build to measure BEFORE")
    ap.add_argument("--fps", type=int, default=0,
                    help="instead of measuring pixels, sweep the claw for N "
                         "seconds and report FRAMES PER GUEST TICK - the A/B "
                         "for 'does this cost anything', run against an apps "
                         "disk of each build")
    ap.add_argument("--dump", action="store_true",
                    help="print the live geometry and lip table per shape")
    a = ap.parse_args()
    S = os88sym.linear
    want = SHAPES if a.shapes == "all" else a.shapes.split(",")

    with os88marty.launch("build/os8088-360.img", apps=a.apps,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        # NOT open_named: it settles, and the attract screen never does.
        entry = dispcp.row_of(m, S, "CYCLONE.O88")
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        time.sleep(6)

        seg = None
        for w in os88geom.windows(m, S):
            if w.title.startswith("Cyclone"):
                seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                 + os88geom.W_SEG, 2))
        if not seg:
            raise RuntimeError("no Cyclone window - it did not launch")
        mo.to(2, 2)                     # THE POINTER OFF THE PLAYFIELD: the
                                        # kernel's arrow is 8x12 of somebody
                                        # else's pixels sitting wherever the
                                        # last click left it, and a capture
                                        # catches it drawn or hidden depending
                                        # on where the gfx lock was
        syms, image = pkg_syms(a.src)
        p = Pkg(m, seg, syms)
        # A RANGE OF CODE, not the whole image: the header is stamped by
        # os88pkg.py on the way to the .o88, `cy_tpl` is a window template
        # both the app and the kernel write into, and `cy_ekcol` is a table
        # cy_pal patches for the display it is on. Code is the part that
        # cannot legitimately differ - and a stale package here does not fail,
        # it reads plausible rubbish out of the middle of the wrong variable.
        lo, n = syms["cy_entry"], 2048
        live = bytes(m.read(seg * 16 + lo, n))
        if live != image[lo:lo + n]:
            bad = next(i for i in range(n) if live[i] != image[lo + i])
            raise RuntimeError("the CYCLONE.O88 running here is not this "
                               "source (first difference at +%d) - run make"
                               % (lo + bad))
        print("cyclone at segment %04X, state %d" % (seg, p.rb("cy_state")))

        # ENTER, UNTIL IT TAKES. The attract screen animates through its own
        # phases and one keystroke into the wrong one is simply swallowed;
        # this used to be a single key and the run then failed sixty seconds
        # later, waiting for a warp nobody had asked for.
        for _ in range(10):
            if p.rb("cy_state") != CYS_TITLE:
                break
            m.key("Enter")
            time.sleep(2)
        os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_PLAY,
                        "the warp to finish", poll=0.5, limit=90)
        if a.fps:
            # FRAMES PER GUEST TICK, and neither of the two obvious things.
            # Host seconds measure this container - a headless MartyPC is not
            # throttled to 4.77MHz and runs the guest several times over.
            # Guest CYCLES per frame measure the PACING, because cy_worker
            # sleeps to one frame a tick and a frame that fits its tick costs
            # the same 262,000 cycles whatever it drew. What a cost shows up
            # in is frames that MISS their tick, so the ratio is what to
            # compare: 1.00 while the work fits, less when it does not.
            #
            # The LOAD is the claw sweeping, with the wave stopped: identical
            # in both builds, where real play is a different game each time
            # and ends at the title screen with the counter stopped.
            quiet(m, p)
            ticks = lambda: u16(m.read(0x46C, 2))
            n = p.rw("cy_nlane")
            f0, k0, t0 = p.rw("cy_frame"), ticks(), time.time()
            lane = 0
            while time.time() - t0 < a.fps:
                lane = (lane + 1) % n
                p.ww("cy_plane", lane)
            f1, k1, t1 = p.rw("cy_frame"), ticks(), time.time()
            df, dk = (f1 - f0) & 0xFFFF, (k1 - k0) & 0xFFFF
            print("FRAMES: %d in %d guest ticks = %.3f frames/tick "
                  "(%.2f fps of the guest's own 18.2Hz clock; %.1fs of host "
                  "time; level %d, state %d)"
                  % (df, dk, df / dk, 18.2065 * df / dk, t1 - t0,
                     p.rw("cy_level"), p.rb("cy_state")))
            return 0
        quiet(m, p)
        if not a.repair:
            m.write(p.addr("cy_web_repair"), bytes([0xC3]))
        print("web repair: %s" % ("live" if a.repair else "STUBBED"))
        bad = 0
        for name in want:
            p.ww("cy_shape", syms[name])
            p.wb("cy_needlay", 1)
            p.wb("cy_full", 1)
            m.advance(frames=40)
            quiet(m, p)
            if a.climb:
                park_climber(m, p, a.depth)
            p.wb("cy_full", 1)
            m.advance(frames=40)
            if a.dump:
                n = p.rw("cy_nlane")
                lip = [(u16(m.read(p.addr("cy_lipx") + i * 2, 2)),
                        u16(m.read(p.addr("cy_lipy") + i * 2, 2)))
                       for i in range(n)]
                print("     org=(%d,%d) rad=%d cx=%d cy=%d cwid=%d pfh=%d"
                      % (p.rw("cy_ox"), p.rw("cy_oy"), p.rw("cy_rad"),
                         p.rw("cy_cx"), p.rw("cy_cy"), p.rw("cy_cwid"),
                         p.rw("cy_pfh")))
                print("     lip=%s" % (lip,))
            n = sweep(m, p, climb=a.climb)
            m.advance(frames=6)
            inc = shot(m)
            p.wb("cy_full", 1)
            m.advance(frames=40)
            full = shot(m)
            ox, oy = p.rw("cy_ox"), p.rw("cy_oy")
            miss, extra = diff(inc, full,
                               (ox, oy, ox + p.rw("cy_cwid") - 1,
                                oy + p.rw("cy_chgt") - 1))
            bad += len(miss)
            print("  %-12s lanes=%2d  missing=%4d [%s]  extra=%4d [%s] %s"
                  % (name[6:], n, len(miss), bbox(miss), len(extra),
                     bbox(extra), "" if not miss else "<-- EATS WEB"))
            if a.dump and miss:
                print("     missing, in CONTENT coordinates: %s"
                      % ([(x - ox, y - oy) for x, y in miss][:20],))
            if a.png:
                w, h, _ = inc
                for tag, sh in (("inc", inc), ("full", full)):
                    os88marty.write_png("%s-%s-%s.png" % (a.png, name[6:], tag),
                                        w, h,
                                        [sh[2][y * w:(y + 1) * w]
                                         for y in range(h)])
        print("TOTAL missing: %d" % bad)
        return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
