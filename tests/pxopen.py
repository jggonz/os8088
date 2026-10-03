#!/usr/bin/env python3
"""WHAT PIXEL PUTS ON THE GLASS IS WHAT THE REFERENCE RENDERS (SPEC.md 106.13).

    python3 tests/pxopen.py [--machine os8088_xt_vga | os8088_5150_cga_gla]

A picture is opened by its ASSOCIATION (a double-click on CITY.PCX, SPEC.md
54.10's two-phase launch), decoded, and drawn. Then the canvas is read back
off the card - VGA's rasterised frame, or the CGA's video memory - and every
pixel of the picture's part of it is held to tools/pixelsim.py's render() of
the same master, the same palette and the same view:

  leg A  the view itself: the zoom is pixelsim's fit_z for this canvas and
         this display's pixel aspect, and the steps and the picture's screen
         size are pixelsim's View - the arithmetic, not just its result
  leg B  the picture's pixels at Fit, every one, and the ground round them
  leg C  ZOOM IN (+): a new view, the canvas rendered again - compared again
  leg D  a pan DOWN (the arrow key): OSAPI_GFX_SCROLL moved the rows and only
         the exposed strip was rendered - the seam between them must be
         invisible, which is the image-anchored dither phase (106.11)
  leg E  a pan ACROSS: the columns moved by OSAPI_GFX_SAVE/REST band by band
         and the exposed strip rendered - compared again
  leg F  a BOTTOM-UP truecolour file (MOUNTAIN.BMP, through File > Revert
         with its name poked in): the rows painted as they came, from the
         bottom, and the last ones at the end - with no canvas render after
         - are pixelsim's, which is the progressive painter's whole proof

A composer that is off by one row, a DDA that drifts, a table built for the
wrong depth, a scroll whose strip is rendered at the wrong phase - each of
them is a picture that LOOKS right and is not, and each fails here.
"""
import os, sys, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tests"); sys.path.insert(0, "tools")
import os88marty as M
import os88ui, os88build
import pixelsim as P
from pxsyms import pkg_syms, instance, u16

FAIL = []


def check(name, ok, detail=""):
    print("   %-64s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_xt_vga")
ap.add_argument("--picture", default="apps/pixel/samples/CITY.PCX")
ap.add_argument("--second", default="apps/pixel/samples/MOUNTAIN.BMP")
a = ap.parse_args()

syms, image = pkg_syms()
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run "
             "`make build/pixel.o88`")
pic = os.path.basename(a.picture).upper()
data = open(a.picture, "rb").read()
DISK = "build/pxopen.img"
M.scratch_disk(DISK, "build/pixel.o88", "PICS:" + a.picture,
               "PICS:" + a.second)
EGA = {c: i for i, c in enumerate(P.EGA16)}

print("== PiXEL on the glass: %s on %s (SPEC.md 106.11) ==" % (pic, a.machine))
with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine) as ui:
    m = ui.m
    S = ui._S
    ui.path("B:/PICS/" + pic)
    M.until(m, lambda _: instance(m, S, image), "PiXEL's instance", poll=0.3,
            limit=90)
    seg = instance(m, S, image)
    base = seg * 16
    B = lambda n: m.read(base + syms[n], 1)[0]
    W = lambda n: u16(m.read(base + syms[n], 2))
    D = lambda n: W(n) | (u16(m.read(base + syms[n] + 2, 2)) << 16)
    sW = lambda n: (lambda v: v - 65536 if v >= 32768 else v)(W(n))

    def idle():
        return B("px_busy") == 0 and B("px_job") == 0
    M.until(m, lambda _: W("px_ndone") and idle(), "the decode", poll=0.3,
            limit=900)
    ui.settle()
    vt = m.cmd(cmd="video")["type"]
    depth = B("px_bpp")
    kind = B("px_rkind")
    print("   PiXEL at %04X, %s, display depth %d" % (seg, vt, depth))
    r = m.read(base + syms["px_cur"], 48)
    mw, mh, scl, mseg = u16(r, 32), u16(r, 34), r[36], u16(r, 38)
    master = m.read(mseg * 16, mw * mh)
    p = P.decode(data, pic.rsplit(".", 1)[1])
    sm, smw, smh, mode, pal = P.emit(p, scl)
    shown = {}
    check("the master is pixelsim's", bytes(sm) == master, "")
    aspect = ((1, 1), (29, 45), (5, 12), (35, 48))[kind]

    def view():
        v = syms["px_vcan"]
        rd = lambda o: u16(m.read(base + v + o, 2))
        rdd = lambda o: rd(o) | (rd(o + 2) << 16)
        s16 = lambda x: x - 65536 if x >= 32768 else x
        return P.GuestView(s16(rd(0)), s16(rd(2)), rdd(4), rdd(8), rd(12),
                           rd(14))

    def canvas():
        return (sW("px_cvx1"), sW("px_midy1"), sW("px_cvx2"), sW("px_midy2"))

    def frame():
        if vt == "vga":
            w, h, rgb = m.fbuf()
            return lambda x, y: EGA.get(tuple(rgb[3 * (y * w + x):
                                                  3 * (y * w + x) + 3]), -1)
        w, h, rows = m.vram()
        return lambda x, y: rows[y][x]

    def compare(leg, ground=True):
        ui.mo.to(2, 2)              # the pointer off the canvas: it is drawn
        ui.settle()                 # into the frame the card rasterises
        r = m.read(base + syms["px_cur"], 48)
        mw, mh, mseg = u16(r, 32), u16(r, 34), u16(r, 38)
        master = m.read(mseg * 16, mw * mh)
        pal = shown["pal"]
        v = view()
        cx1, cy1, cx2, cy2 = canvas()
        get = frame()
        x1, y1 = max(cx1, v.ix), max(cy1, v.iy)
        x2 = min(cx2, v.ix + v.dw - 1)
        y2 = min(cy2, v.iy + v.dh - 1)
        want = P.render(master, mw, mh, pal, depth, v, (x1, y1, x2, y2),
                        None)
        bad = [(x, y) for (x, y), c in want.items() if get(x, y) != c]
        if bad and os.environ.get("PXDBG"):
            rows = sorted(set(y for x, y in bad))
            print("      rows %s, x %d..%d" % (rows[:12], min(x for x, y in bad),
                                               max(x for x, y in bad)))
            for yy in rows[:2]:
                print("      glass %s" % "".join(str(get(x, yy)) for x in range(88, 160)))
                print("      want  %s" % "".join(str(want[(x, yy)]) for x in range(88, 160)))
        check("%s: the picture's %d pixels are pixelsim's" %
              (leg, len(want)), not bad,
              "%d differ, first %s: pixelsim %s, glass %s" %
              (len(bad), bad[:1], want[bad[0]], get(*bad[0])) if bad else "")
        if ground and depth == 4 and v.ix > cx1:
            g = [get(x, cy1 + 5) for x in range(cx1, v.ix)]
            check("%s: the ground beside it is dark grey" % leg,
                  set(g) == {8}, "colours %s" % sorted(set(g)))

    shown["pal"] = pal
    # --- A: the view's arithmetic ------------------------------------------------
    cx1, cy1, cx2, cy2 = canvas()
    cw, ch = cx2 - cx1 + 1, cy2 - cy1 + 1
    z = D("px_zcur")
    check("A: Fit is pixelsim's fit_z for %dx%d at %d/%d" % ((cw, ch) + aspect),
          z == P.fit_z(mw, mh, scl, cw, ch, aspect),
          "guest %d, pixelsim %d" % (z, P.fit_z(mw, mh, scl, cw, ch, aspect)))
    sv = P.View(mw, mh, scl, z, aspect, (cx1, cy1, cx2, cy2))
    gv = view()
    check("A: the steps and size are pixelsim's View's",
          (gv.hstep, gv.vstep, gv.dw, gv.dh) ==
          (sv.hstep, sv.vstep, sv.dw, sv.dh),
          "guest %r, pixelsim %r" % ((gv.hstep, gv.vstep, gv.dw, gv.dh),
                                     (sv.hstep, sv.vstep, sv.dw, sv.dh)))
    check("A: ...and its place", (gv.ix, gv.iy) == (sv.ix, sv.iy),
          "guest %r, pixelsim %r" % ((gv.ix, gv.iy), (sv.ix, sv.iy)))
    compare("B")
    for leg, key in (("C", "Equal"), ("C", "Equal")):   # '=' is zoom in
        m.key(key)
        ui.settle()
    compare("C (zoomed in twice)", ground=False)
    oy0 = sW("px_oy")
    m.key("ArrowDown")              # the picture moves UP: content scrolls
    ui.settle()
    check("D: the pan moved the picture", sW("px_oy") != oy0, "")
    compare("D (scrolled)", ground=False)
    ox0 = sW("px_ox")
    m.key("ArrowRight")
    ui.settle()
    check("E: the pan moved it across", sW("px_ox") != ox0,
          "the picture may be no wider than the canvas")
    compare("E (shifted)", ground=False)
    # --- F: a bottom-up file, painted as it came ----------------------------
    sec = os.path.basename(a.second).upper()
    n0 = W("px_ndone")
    m.write(base + syms["px_cur"], sec.encode().ljust(13, b"\0"))
    m.ctrl("KeyR")
    M.until(m, lambda _: W("px_ndone") != n0 and idle(), "the second open",
            poll=0.3, limit=900)
    r = m.read(base + syms["px_cur"], 48)
    p2 = P.decode(open(a.second, "rb").read(), sec.rsplit(".", 1)[1])
    sm2, _, _, _, pal2 = P.emit(p2, r[36])
    check("F: %s's master is pixelsim's" % sec,
          bytes(sm2) == m.read(u16(r, 38) * 16, u16(r, 32) * u16(r, 34)), "")
    shown["pal"] = pal2
    compare("F (%s, painted as it came)" % sec)

print("pxopen: %s" % ("ok" if not FAIL else "%d FAILED" % len(FAIL)))
sys.exit(1 if FAIL else 0)
