#!/usr/bin/env python3
"""PiXEL's full screen (SPEC.md 106.23), on MartyPC's VGA XT, CGA 5150 and
Hercules 5150.

    python3 tests/pxfsx.py [--machine M ...] [--shots DIR]

A folder of three pictures in B:/PICS - VACATION.JPG (the cube), BALLOONS.PNG
and CAT.GIF (their own palettes) - and PiXEL opened on BALLOONS.PNG by its
association. Then, on each machine, for EVERY MODE THE DISPLAY OFFERS (VGA:
Mode X, 13h, 640x480 adaptive; CGA: C160, 320x200x4, 640x200; Hercules:
720x348):

  ENTER  View > Screen's choice poked, F pressed: the bracket entered with
         its worker kept; the part's tables are pixelsim's (FsPic: the median
         cut, every used entry's plan, the CGA's chosen set, ground and ink);
         the DAC read back through 3C7h/3C9h is the palette or the sixteen;
         and once the captions are down EVERY PIXEL of the mode is pixelsim's
         frame at Fit (FsView, FsPic.frame) - the CGA's and the Hercules'
         memory read straight, the VGA's out of MartyPC's whole field (its
         apertures stop at 400 lines, and Mode X is 480) through the DAC's
         widening, v x 255 div 63
  LEAVE  Esc (Alt+Enter once, on the CGA): the desktop's mode back, the DAC
         as it was before (the 12h palette, VGA), the desktop's pixels as
         they were (the window repainted - the menu bar's clock row aside),
         and PiXEL's claims as they were, sizes and all

and on the VGA, then:

  NEXT   N in full screen: the next picture decodes HIDDEN with the worker
         kept, is committed, and is pixelsim's frame in the new picture's
         colours; leaving shows it in the window (its tables the worker's)
         with no decode's claim left behind
  SLIDES S: two slides go by in full screen; a key stops it; Esc leaves
"""
import argparse
import functools
import os
import sys
import time

print = functools.partial(print, flush=True)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
sys.path.insert(0, "tests")
sys.path.insert(0, "tools")
import os88marty as M           # noqa: E402
import os88ui                   # noqa: E402
import heapmap                  # noqa: E402
import os88build                # noqa: E402
import pixelsim as P            # noqa: E402
from pxsyms import pkg_syms, part_syms, instance, u16      # noqa: E402

MACHINES = {"os8088_xt_vga": (0, 1, 2), "os8088_5150_cga_gla": (4, 5, 6),
            "os8088_5150_herc_gla": (7,)}
ap = argparse.ArgumentParser()
ap.add_argument("--machine", action="append")
ap.add_argument("--shots")
a = ap.parse_args()
machines = a.machine or list(MACHINES)
syms, image = pkg_syms()
fs = part_syms("pxfull.asm")
o88 = open(os88build.at("build/pixel.o88"), "rb").read()
if o88[:syms["op_table"]] != image[:syms["op_table"]]:
    sys.exit("build/pixel.o88 is not this tree's pixel.asm - run make")
PART_FULL = 5
HZ = 4772727                    # the 8088's clock, both machines (MartyPC)
BAR = 20                        # the menu bar's rows, with the clock in them
FAIL = []
TIMES = []


def check(what, ok, detail=""):
    print("%s %s%s" % ("PASS" if ok else "FAIL", what,
                       "" if ok or not detail else "  -- " + detail))
    if not ok:
        FAIL.append(what)


PICS = ["VACATION.JPG", "BALLOONS.PNG", "CAT.GIF"]
DISK = M.scratch_disk("build/pxfsx.img", "build/pixel.o88", "build/PIXEL.GFX",
                      *["PICS:apps/pixel/samples/" + n for n in PICS],
                      "--folder", "SYSTEM/APPDATA", size=720)


def wpng(name, w, h, rgb):
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
        M.write_png_rgb(os.path.join(a.shots, name), w, h, rgb)


# --- reading a mode back ---------------------------------------------------------
def conv(v6):
    return (v6 * 255) // 63      # MartyPC's DAC widening


def dac(m, n):
    m.outb(0x3C7, 0)
    return [tuple(m.inb(0x3C9) for _ in range(3)) for _ in range(n)]


def mem_codes(m, mode, W, H, seg):
    """the CGA's and the Hercules' modes: their memory, as colour codes"""
    rows = []
    if mode == P.FSM_C160:
        b = m.read(0xB8000, 16000)
        for y in range(H):
            r = []
            for c in range(80):
                at = b[y * 160 + 2 * c + 1]
                r += [at >> 4, at & 15]
            rows.append(r)
    elif mode == P.FSM_CGA320:
        b = m.read(0xB8000, 0x4000)
        for y in range(H):
            o = (y & 1) * 0x2000 + (y >> 1) * 80
            rows.append([(b[o + (x >> 2)] >> (6 - 2 * (x & 3))) & 3
                         for x in range(W)])
    else:
        base, banks, stride = (0xB8000, 2, 80) if mode == P.FSM_CGA640 \
            else (seg * 16, 4, 90)
        b = m.read(base, banks * 0x2000)
        for y in range(H):
            o = (y % banks) * 0x2000 + (y // banks) * stride
            rows.append([(b[o + (x >> 3)] >> (7 - (x & 7))) & 1
                         for x in range(W)])
    return rows


def vga_rgb(m, W, H, scan, ref):
    """the VGA's picture, out of the WHOLE FIELD (aperture 3) - placed by
    where the reference fits it, which MartyPC decides per mode (104, 34 for
    the 320-wide modes, 96, 32 for 12h)"""
    fw, fh, rgb = m.fbuf(aperture=3)
    sx = 640 // W
    best = None
    for oy in range(24, 44):
        for ox in range(88, 120):
            n = 0
            for y in range(0, min(H, 64), 9):
                for x in range(0, W, 7):
                    o = 3 * ((oy + y * scan) * fw + ox + x * sx)
                    n += tuple(rgb[o:o + 3]) != ref[y][x]
            if best is None or n < best[0]:
                best = (n, ox, oy)
    _, ox, oy = best
    return [[tuple(rgb[3 * ((oy + y * scan) * fw + ox + x * sx):
                       3 * ((oy + y * scan) * fw + ox + x * sx) + 3])
             for x in range(W)] for y in range(H)], (fw, fh, rgb)


for mach in machines:
    modes = MACHINES[mach]
    print("== PiXEL's full screen on %s: modes %s (SPEC.md 106.23) =="
          % (mach, [P.FSM_NAMES[k] for k in modes]))
    with os88ui.boot("build/os8088-360.img", apps=DISK, machine=mach) as ui:
        m = ui.m
        S = ui._S
        vga = 0 in modes
        hsym = {n: S(n) for n in ("mem_base", "mem_top", "spl_live",
                                  "mem_tab")}
        ui.path("B:/PICS/BALLOONS.PNG")
        M.until(m, lambda _: instance(m, S, image), "PiXEL's instance",
                poll=0.3, limit=120)
        base = instance(m, S, image) * 16
        B = lambda n: m.read(base + syms[n], 1)[0]             # noqa: E731
        W2 = lambda n: u16(m.read(base + syms[n], 2))          # noqa: E731

        def still(what, limit=900):
            M.until(m, lambda _: W2("px_ndone") and B("px_busy") == 0
                    and B("px_job") == 0 and B("px_hmode") == 0
                    and B("px_fsin") == 0, what, poll=0.5, limit=limit)
            M.ui_done(m, what)

        def pseg():
            r = m.read(base + syms["op_table"] + 10 + 8 * PART_FULL, 8)
            return u16(r, 6) if r[1] & 32 else 0

        def pb(n, k=1):
            return m.read(pseg() * 16 + fs[n], k)

        def pw(n):
            return u16(pb(n, 2))

        def claims():
            hm = heapmap.Map(m, hsym)
            seg = base >> 4
            return sorted(c.para for c in hm.claims
                          if c.own == seg and c.seg != seg)

        bar = BAR + (32 if vga else 0)

        def desktop(like=None):
            """the desktop's pixels - on the VGA out of the WHOLE FIELD
            (aperture 3, its size fixed), where a usual aperture can still
            answer a 13h frame's 400 lines after the mode set back (the
            desktop at 96, 32 in it); and once the raster is the desktop's"""
            for _ in range(20):
                m.pause()
                w, h, rgb = m.fbuf(3 if vga else 0)
                m.run()
                if like is None or (w, h) == like[:2]:
                    break
                M.guest_sleep(m, 0.1)
            return w, h, rgb

        def master():
            r = m.read(base + syms["px_cur"], 50)
            mw, mh, mseg = u16(r, 32), u16(r, 34), u16(r, 38)
            pb_ = m.read(base + syms["px_pal"], 768)
            pal = [tuple(pb_[3 * i:3 * i + 3]) for i in range(256)]
            name = r[:13].split(b"\0")[0].decode()
            return name, m.read(mseg * 16, mw * mh), mw, mh, pal

        def rendered(n0):
            """a render owed and finished, the captions down"""
            M.until(m, lambda _: pseg() and pw("pxf_nren") > n0
                    and pb("pxf_jon")[0] == 0 and pb("pxf_bup")[0] == 0
                    and pb("pxf_bdue")[0] == 0, "the full screen drawn",
                    poll=0.5, limit=900)

        def timed_enter(mode):
            """F, timed by the guest's cycle counter: the bracket's entry
            (px_fsmain) to the part's whole render starting (pxf_jfull: the
            mode set and its colours - the cut, the plans, the chooser) and
            on to its end (pxf_nren's increment). Seconds of a 4.77 MHz 8088"""
            fsmain = (base + syms["px_fsmain"]) & 0xFFFFF
            got = {}
            m.pause()
            m.breakpoints([{"type": "exec", "addr": fsmain}])
            m.key("KeyF")
            m.run()
            seen = None
            while "end" not in got:
                st = m.status()
                if st.get("state") == "breakpoint" and st.get("stops") != seen:
                    seen = st.get("stops")
                    flat = ((st["cs"] << 4) + st["ip"]) & 0xFFFFF
                    cyc = int(st["cycles"])
                    if flat == fsmain and "in" not in got:
                        got["in"] = cyc
                        sg = pseg()
                        got["jf"] = (sg * 16 + fs["pxf_jfull"]) & 0xFFFFF
                        m.breakpoints([
                            {"type": "exec", "addr": got["jf"]},
                            {"type": "mem",
                             "addr": (sg * 16 + fs["pxf_nren"]) & 0xFFFFF}])
                    elif flat == got.get("jf"):
                        got.setdefault("render", cyc)
                    elif "render" in got:
                        got["end"] = cyc
                    m.run()
                    continue
                time.sleep(0.01)
            m.breakpoints([])
            m.run()
            return ((got["render"] - got["in"]) / HZ,
                    (got["end"] - got["render"]) / HZ)

        def frame_check(mode, label, geom=None, view=None):
            name, mst, mw, mh, pal = master()
            fp = P.FsPic(mst, mw, mh, pal, mode,
                         ordered=(B("px_dither") == 0))
            Wd, Hd = pw("pxf_W"), pw("pxf_H")
            z = pw("pxf_z") | (u16(pb("pxf_z", 4), 2) << 16)
            gm = geom or P.FS_GEOM[mode]
            if view:                # (z, ox, oy): a zoom or a pan's
                v = P.FsView(mw, mh, mode, view[0], view[1], view[2], geom=gm)
            else:
                v = P.FsView(mw, mh, mode, P.fs_fit(mw, mh, *gm), geom=gm)
            check("%s: %s's view is pixelsim's (%dx%d at %d, %d)"
                  % (label, name, v.dw, v.dh, v.ox, v.oy),
                  (z, pw("pxf_ox"), pw("pxf_oy"), Wd, Hd)
                  == (v.z, v.ox & 0xFFFF, v.oy & 0xFFFF) + gm[:2],
                  "z %d ox %d oy %d" % (z, pw("pxf_ox"), pw("pxf_oy")))
            if fp.kind == "plan":
                n = pw("pxf_ncol")
                g6 = pb("pxf_c6", 3 * n)
                check("%s: the colours shown (%d) are pixelsim's" % (label, n),
                      [tuple(g6[3 * i:3 * i + 3]) for i in range(n)]
                      == fp.cols6)
                cnt = P.hist_counts(mst)
                c1, c2, t = pb("pxf_c1", 256), pb("pxf_c2", 256), \
                    pb("pxf_t", 256)
                bad = [i for i in range(256) if cnt[i] and
                       (c1[i], c2[i], t[i]) != (fp.c1[i], fp.c2[i], fp.t[i])]
                if bad:                         # (which input moved?)
                    gp = pb("pxf_pal", 768)
                    gcn = pb("pxf_cnt", 1024)
                    print("   ...the part's palette %s px_pal; its counts %s "
                          "the master's" % (
                              "is" if list(gp) == [v for c in pal for v in c]
                              else "is NOT",
                              "are" if [u16(gcn, 4 * i) | u16(gcn, 4 * i + 2)
                                        << 16 for i in range(256)] == cnt
                              else "are NOT"))
                check("%s: every used entry's plan is plan6's (%d used)"
                      % (label, sum(1 for c in cnt if c)), not bad,
                      "%d differ, first %r" % (len(bad), [
                          (i, (c1[i], c2[i], t[i]), (fp.c1[i], fp.c2[i],
                                                     fp.t[i]))
                          for i in bad[:3]]))
            if fp.cga:
                got = (pb("pxf_cset")[0], pb("pxf_chi")[0], pb("pxf_cbg")[0])
                want = ({1: 0, 0: 1, 5: 2}[fp.cga[0]], fp.cga[1], fp.cga[2])
                check("%s: the set chosen is fs_cgapick's (%s %s, ground %d)"
                      % (label, {0: "palette 1", 1: "palette 0", 2: "mode 5"}
                         [want[0]], "high" if want[1] else "low", want[2]),
                      got == want, "guest %r" % (got,))
            check("%s: ground and ink are the darkest and the lightest"
                  % label, (pb("pxf_gnd")[0], pb("pxf_light")[0])
                  == (fp.ground, fp.light))
            ref, _ = fp.frame(v)
            m.pause()
            if mode in (P.FSM_MODEX, P.FSM_VGA13, P.FSM_VGA12):
                d = dac(m, 256 if mode != P.FSM_VGA12 else 16)
                want = [tuple(c >> 2 for c in pal[i]) for i in range(256)] \
                    if mode != P.FSM_VGA12 else \
                    fp.cols6 + [(0, 0, 0)] * (16 - len(fp.cols6))
                check("%s: the DAC read back is %s" % (label, "the palette"
                      if mode != P.FSM_VGA12 else "the sixteen"), d == want)
                rr = [[tuple(conv(c) for c in d[k]) for k in r] for r in ref]
                scan = 1 if mode == P.FSM_VGA12 else 2
                got, raw = vga_rgb(m, Wd, Hd, scan, rr)
                ref = rr
            elif mode == P.FSM_DESK:            # the desktop's own 640 wide,
                rr = [[tuple(conv(c) for c in P.CGA6[k]) for k in r]
                      for r in ref]             # out of the whole field too:
                got, raw = vga_rgb(m, Wd, Hd, 1, rr)    # aperture 0 keeps a
                ref = rr                        # 13h's 400 lines after one
            else:
                got = mem_codes(m, mode, Wd, Hd, pw("pxf_seg"))
                raw = None
            if a.shots:
                w, h, rgb = m.fbuf(0)
                wpng("pxfsx-%s-%d.png" % (mach, mode), w, h, rgb)
            m.run()
            bad = [(x, y) for y in range(Hd) for x in range(Wd)
                   if got[y][x] != ref[y][x]]
            check("%s: every pixel of %s is pixelsim's (%dx%d)"
                  % (label, P.FSM_NAMES[mode], Wd, Hd), not bad,
                  "%d differ, first %r" % (len(bad), bad[:4]))

        still("BALLOONS.PNG open")
        c0 = claims()
        # THE PICTURE EVERY EXIT IS HELD TO, taken when it is STILL: the
        # status bar's "Memory:" is a look every PX_MEMT (5 s), and a d0
        # taken inside the open's last look holds the decode's figure while
        # every exit's repaint draws the next - the wave-8 soak's one
        # failure and wave 9's one in four runs, all three CGA modes alike
        # ("59 pixels differ"). Two pictures a look apart must agree
        d0 = desktop()
        for _ in range(3):
            M.guest_sleep(m, 5.5)
            d0b = desktop()
            if d0b == d0:
                break
            if d0b[:2] == d0[:2]:
                mv = [k // 3 for k in range(0, len(d0[2]), 3)
                      if d0[2][k:k + 3] != d0b[2][k:k + 3]]
                print("      (the desktop moved under the baseline: %d pixels "
                      "in (%d, %d, %d, %d))" % (
                          len(mv), min(k % d0[0] for k in mv),
                          min(k // d0[0] for k in mv),
                          max(k % d0[0] for k in mv),
                          max(k // d0[0] for k in mv)))
            d0 = d0b
        m.pause()
        v0 = m.video()
        dac0 = dac(m, 256) if vga else None
        m.run()
        for i, mode in enumerate(modes):
            label = P.FSM_NAMES[mode]
            m.write(base + syms["px_fsm"], bytes([mode]))
            t_set, t_ren = timed_enter(mode)
            check("%s: F entered full screen in it (PXM %d); its colours %.2f "
                  "s, the picture whole %.2f s on a 4.77 MHz 8088"
                  % (label, B("px_fsmode"), t_set, t_ren),
                  B("px_fsmode") == mode)
            TIMES.append((mach, label, t_set, t_ren))
            rendered(0)
            frame_check(mode, label)
            if mach == "os8088_5150_cga_gla" and i == 0:
                m.alt("Enter", hold=0.15)      # the other door, once
                how = "Alt+Enter"
            else:
                m.key("Escape")
                how = "Esc"
            still("back from %s" % label)
            check("%s: %s left it, the part dropped" % (label, how),
                  B("px_fsin") == 0 and not pseg())
            m.pause()
            v1 = m.video()
            dac1 = dac(m, 256) if vga else None
            m.run()
            check("%s: the desktop's mode back (%s)" % (label, v1.get("mode")),
                  (v1.get("mode"), v1.get("field_w"), v1.get("field_h"))
                  == (v0.get("mode"), v0.get("field_w"), v0.get("field_h")))
            if vga:
                check("%s: the DAC as it was before (the 12h palette)" % label,
                      dac1 == dac0)
            def ddif(d1):
                return [k // 3 for k in range(bar * d1[0] * 3, len(d1[2]), 3)
                        if d1[2][k:k + 3] != d0[2][k:k + 3]] \
                    if d1[:2] == d0[:2] else [-1]
            d1 = desktop(d0)
            dif = ddif(d1)
            if dif:     # the status bar's "Memory:" is a sample every PX_MEMT
                M.guest_sleep(m, 5.5)   # (5 s), and the exit's repaint can
                d1 = desktop(d0)        # draw the one taken with the view
                dif = ddif(d1)          # claim and the part still freed
            box = (min(k % d1[0] for k in dif), min(k // d1[0] for k in dif),
                   max(k % d1[0] for k in dif), max(k // d1[0] for k in dif)) \
                if dif and dif != [-1] else ()
            if dif:                     # (the two pictures, under --shots)
                wpng("pxfsx-%s-%d-before.png" % (mach, mode), *d0)
                wpng("pxfsx-%s-%d-after.png" % (mach, mode), *d1)
            check("%s: the desktop repainted as it was (%d pixels differ "
                  "under the menu bar)" % (label, len(dif)), not dif,
                  "within %r" % (box,))
            c1 = claims()
            check("%s: PiXEL's claims as they were" % label, c1 == c0,
                  "%r -> %r" % (c0, c1))

        # ------------------------------------------------------------- ORDERED
        # View > Dither's other rule, on the machine's first dithered mode
        dmode = [k for k in modes if k not in P.FS_256][0]
        m.write(base + syms["px_dither"], b"\x00")
        m.write(base + syms["px_fsm"], bytes([dmode]))
        n_in = W2("px_fsn")
        m.key("KeyF")
        M.until(m, lambda _: W2("px_fsn") > n_in and B("px_fsin"),
                "the bracket", poll=0.3, limit=120)
        rendered(0)
        frame_check(dmode, "ORDERED " + P.FSM_NAMES[dmode])
        m.key("Escape")
        still("back from ORDERED")
        m.write(base + syms["px_dither"], b"\x01")
        if not vga:
            continue
        # ------------------------------------------------------- ZOOM AND PAN
        # in the two 256-colour modes a pan MOVES the screen (Mode X through
        # the latches, 13h a byte at a time) and renders the strip it left;
        # the picture after it must be pixelsim's at the panned view
        for zm in (P.FSM_MODEX, P.FSM_VGA13):
            lab = "PAN " + P.FSM_NAMES[zm]
            m.write(base + syms["px_fsm"], bytes([zm]))
            n_in = W2("px_fsn")
            m.key("KeyF")
            M.until(m, lambda _: W2("px_fsn") > n_in and B("px_fsin"),
                    "the bracket", poll=0.3, limit=120)
            rendered(0)
            _, _, mw, mh, _ = master()
            Wz, Hz = P.FS_GEOM[zm][:2]
            z = P.fs_fit(mw, mh, *P.FS_GEOM[zm])
            z = P.fs_zstep(z, True, mw, mh, zm)
            z = P.fs_zstep(z, True, mw, mh, zm)
            vv = P.FsView(mw, mh, zm, z)
            for k in ("Equal", "Equal"):
                n0 = pw("pxf_nren")
                m.key(k)
                rendered(n0)
            frame_check(zm, lab + ", two steps in", view=(z, None, None))
            for k, dx, dy in (("ArrowRight", -(Wz // 8), 0),
                              ("ArrowDown", 0, -(Hz // 8)),
                              ("ArrowLeft", Wz // 8, 0)):
                n0 = pw("pxf_nren")
                m.key(k)
                rendered(n0)
                vv = P.FsView(mw, mh, zm, z, vv.ox + dx, vv.oy + dy)
                frame_check(zm, "%s, %s" % (lab, k), view=(z, vv.ox, vv.oy))
            m.key("Escape")
            still("back from " + lab)
        # ---------------------------------------------------------------- DESK
        # The EGA's mode is a SAME-MODE bracket drawn with OSAPI_GFX_BLITP,
        # and no MartyPC machine has an EGA: so it is driven here on the
        # VGA's own 12h desktop - the same sixteen, 640x480 at 1/1 - by
        # poking [px_fsmode] at the bracket's entry, where the mode the
        # display offers has been chosen and not yet entered
        fsmain = (base + syms["px_fsmain"]) & 0xFFFFF
        m.pause()
        m.breakpoints([{"type": "exec", "addr": fsmain}])
        m.key("KeyF")
        m.run()
        M.until(m, lambda _: m.status().get("state") == "breakpoint",
                "the bracket's entry", poll=0.1, limit=120)
        m.write(base + syms["px_fsmode"], bytes([P.FSM_DESK]))
        m.breakpoints([])
        m.run()
        rendered(0)
        check("DESK: the same-mode bracket's rect is the desktop's (%dx%d at "
              "%d, %d), BLITP %s" % (pw("pxf_W"), pw("pxf_H"), pw("pxf_sx"),
                                     pw("pxf_sy"), "refused" if
                                     pb("pxf_p4")[0] else "taken"),
              (pw("pxf_W"), pw("pxf_H"), pb("pxf_p4")[0]) == (640, 480, 0))
        frame_check(P.FSM_DESK, "DESK", geom=(640, 480, 1, 1))
        m.key("Escape")
        still("back from DESK")
        d1 = desktop(d0)
        if ddif(d1):                    # (the "Memory:" sample, as above)
            M.guest_sleep(m, 5.5)
            d1 = desktop(d0)
        nd = len(ddif(d1))
        check("DESK: the desktop repainted as it was (%d pixels differ)" % nd,
              nd == 0)
        check("DESK: PiXEL's claims as they were", claims() == c0)
        # ---------------------------------------------------------------- NEXT
        m.write(base + syms["px_fsm"], bytes([P.FSM_VGA12]))
        n_in = W2("px_fsn")
        m.key("KeyF")
        M.until(m, lambda _: W2("px_fsn") > n_in and B("px_fsin"),
                "the bracket", poll=0.3, limit=120)
        rendered(0)
        was = master()[0]
        # the COMMIT is the moment, caught by a breakpoint: two reads of a
        # running guest can see [px_hmode] 0 before the key and the hidden
        # record's name after it, and the progress line's band jobs count
        # in pxf_nren too - so a render "finished" mid-decode
        commit = (base + syms["px_fscommit"]) & 0xFFFFF
        m.pause()
        m.breakpoints([{"type": "exec", "addr": commit}])
        m.key("KeyN")
        m.run()
        M.until(m, lambda _: m.status().get("state") == "breakpoint",
                "the next picture's commit", poll=0.2, limit=900)
        n0 = pw("pxf_nren")
        m.breakpoints([])
        m.run()
        rendered(n0)
        now = master()[0]
        check("NEXT: N in full screen showed %s after %s, its master the "
              "hidden decode's" % (now, was),
              now == sorted(PICS)[(sorted(PICS).index(was) + 1) % 3])
        frame_check(P.FSM_VGA12, "NEXT")
        m.key("Escape")
        still("back from NEXT")
        r = master()
        check("NEXT: the window shows %s, its tables made (px_tabok %d)"
              % (r[0], B("px_tabok")), r[0] == now and B("px_tabok") == 1)
        c2 = claims()
        check("NEXT: no decode's claim left behind (%d claims, as before)"
              % len(c2), len(c2) == len(c0), "%r -> %r" % (c0, c2))
        if a.shots:
            w, h, rgb = desktop()
            wpng("pxfsx-%s-after-next.png" % mach, w, h, rgb)
        # -------------------------------------------------------------- SLIDES
        n_in = W2("px_fsn")
        m.key("KeyF")
        M.until(m, lambda _: W2("px_fsn") > n_in and B("px_fsin"),
                "the bracket", poll=0.3, limit=120)
        rendered(0)
        seen = [master()[0]]
        m.key("KeyS")
        M.until(m, lambda _: B("px_slon") == 1, "the slideshow", poll=0.3,
                limit=60)
        for k in range(2):
            M.until(m, lambda _: master()[0] != seen[-1],
                    "slide %d" % (k + 2), poll=0.5, limit=900)
            seen.append(master()[0])
        m.key("KeyQ")
        M.until(m, lambda _: B("px_slon") == 0, "the slideshow to stop",
                poll=0.3, limit=120)
        check("SLIDES: S showed %s, a key stopped it" % " -> ".join(seen),
              len(seen) == 3 and B("px_fsin") == 1)
        m.key("Escape")
        still("back from SLIDES")
        c3 = claims()
        check("SLIDES: no decode's claim left behind (%d claims)" % len(c3),
              len(c3) == len(c0), "%r -> %r" % (c0, c3))

for t in TIMES:
    print("   time %-22s %-14s colours %6.2f s  render %6.2f s" % t)
print("%s pxfsx" % ("FAIL" if FAIL else "PASS"))
sys.exit(1 if FAIL else 0)
