#!/usr/bin/env python3
"""VIDEO.O88's DESKTOP POSTERS (SPEC.md 98.1.3.11, 98.4.5) on MartyPC, each
held to the host's reference BYTE FOR BYTE in the poster's claim, with the
set the player chose ([vp_p4]) and the scale it laid out at ([vp_ps]):

On a VGA's desktop (os8088_xt_vga):
 1. VGA4 of its own colours, 640 wide, posters at half and a CUSTOM one:
    at open the box is the custom one; Right, and it is key 1's.
 2. The same at its own size, shown at half: the stored one halved.
 3. None stored: vga4_pack's decimation, which turns with the output's place
    so a checkerboard stays one.
 4. VGA8: the colour poster at its own size, packed for BLIT4.
 5. CGA4 with both sets: the COLOUR one, on this desktop.
 6. TH_SIZE: a VGA4 file that fits, its posters at half - the box at half.
 7. RESIDENT, a custom poster alone (slot bytes 56..61).
 8. VGA4 with black and white posters only - the owner's T69 Collapse -
    held to its bits ON THE SCREEN too: the one-bit blit, never the
    planar one a VGA4 file's Live shadow takes.
On a CGA's (os8088_5150_cga_gla):
 9. CGA4 with both sets: the BLACK AND WHITE one, OSAPI_GFX_BLIT1's rows.
10. VGA4 312 x 350, colour at its own size, shown at a quarter: halved
    twice by vp_dphalf, the second in place (a colour picture is one
    segment, 98.1.3.11, so a quarter of one at its own size is a CGA's
    desktop and a narrow clip).
11. MONO1 624 x 350, black and white at its own size, at a quarter: halved
    twice by vp_half.

Broken on purpose: the posters ignored ([vp_thn] never set) and 1, 2, 4,
5, 7 and 9 FAIL; the layout tried from its own size whatever TH_SIZE says,
with the colour set taken without asking the desktop, and 6 and 9 FAIL;
vp_dphalf's turning taken out and 2 and 10 FAIL; the first of two black
and white halvings skipped and 11 FAILS; vp_v4pack's turning taken out and
3 FAILS; the poster drawn through vp_blitb again (a VGA4 file's planes)
and 8 FAILS on the screen alone.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

FPS = 6.0
# sixteen greys: none is the desktop's but black and white
PAL = bytes(v for i in range(16) for v in (i * 4 + 3,) * 3)
PAL = bytes(3) + PAL[3:45] + bytes((63, 63, 63))
PAL8 = bytes((i * 7) % 64 if c == 0 else (i * 3) % 64 if c == 1 else
             (63 - i % 64) for i in range(256) for c in range(3))
CGASEL = 0x01                   # blue ground, palette 0


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def thumbs(pf, g, ts, fl, pal=None, cgapal=None, custom=None):
    """Writer.thumbs as the encoder's desk_posters makes them"""
    pw, ph = vid.thumb_dims(pf, g)

    def pic(rgb, kind):
        return (vid.desktop_poster_rgb if kind == "c" else
                vid.desktop_mono_rgb)(rgb, pw, ph, ts)

    def make(cv, kind):
        return pic(vid.canvas_rgb(pf, cv, g, pal, cgapal), kind)
    cust = None
    if custom:
        rgb = [[(min(63, x * 63 // pw), min(63, y * 63 // ph), 32)
                for x in range(pw)] for y in range(ph)]
        import numpy as np
        rgb = np.asarray(rgb, np.float64)
        cust = {k: pic(rgb, k) for k in vid.thumb_kinds(fl)}
    return dict(scale=ts, flags=fl, make=make, custom=cust)


def frames(pf, g, n, seed, flat=False):
    """n canvases: two-value checkerboards under a moving bar - or, `flat`,
    a value a row (a big canvas's key small enough for the floppy)"""
    rnd = random.Random(seed)
    out = []
    if flat:                            # (each row one value, a few apart)
        for f in range(n):
            v = rnd.randrange(16)
            cv = bytearray()
            for y in range(g.h):
                if pf == vid.PF_VGA4:
                    cv += bytes([(v + y // 3) & 15]) * g.w
                else:
                    cv += bytes([(0x00, 0xFF, 0xAA, 0x0F)[(v + y) & 3]]) * \
                        g.wb
            out.append(bytes(cv))
        return out
    if pf in (vid.PF_VGA4, vid.PF_VGA8):
        top = 16 if pf == vid.PF_VGA4 else 256
        w = g.w
        for f in range(n):
            a, b = rnd.randrange(top), rnd.randrange(top)
            row0 = bytes(a if x & 1 else b for x in range(w))
            row1 = bytes(b if x & 1 else a for x in range(w))
            cv = bytearray((row0 + row1) * (g.h // 2) + row0 * (g.h & 1))
            x0 = (f * 17) % max(1, w - 64)
            for y in range(g.h // 4, g.h // 2):
                cv[y * w + x0:y * w + x0 + 64] = bytes([top - 1]) * 64
            out.append(bytes(cv))
        return out
    for f in range(n):                  # a byte a row of packed pixels
        a, b = rnd.randrange(256), rnd.randrange(256)
        cv = bytearray()
        for y in range(g.h):
            cv += bytes((a if (x + y) & 1 else b) for x in range(g.wb))
        x0 = (f * 3) % max(1, g.wb - 8)
        for y in range(g.h // 4, g.h // 2):
            cv[y * g.wb + x0:y * g.wb + x0 + 8] = b"\xff" * 8
        out.append(bytes(cv))
    return out


def clip(tmp, name, pf, g, th, pal=None, cgapal=None, n=12, resident=False,
         flat=False):
    out = os.path.join(tmp, name)
    if pal and len(pal) < vid.PAL_BYTES:    # (VGA4's sixteen, the rest 0)
        pal = pal + bytes(vid.PAL_BYTES - len(pal))
    vid.encode_canvases(frames(pf, g, n, len(name), flat), g, out, FPS, pf,
                        pal, "vidposter", keysecs=1.0, poster=0,
                        cgapal=cgapal, thumbs=th, resident=resident)
    vid.verify_v88(out)
    return out


def halved(pic, kind, w, h, times):
    """A stored poster halved as the player halves it"""
    bw = (w + 7) // 8
    for _ in range(times):
        if kind == "c":
            pic, w, h = vid.packed_half(pic, w, h)
        else:
            pic, bw, h = vid.thumb_half(pic, bw, h)
    return pic


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    T = vid
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        g64 = vid.Geom(vid.LAY_LIN80, 80, 120, bitplanes=True)
        g32 = vid.Geom(vid.LAY_LIN80, 40, 96, bitplanes=True)
        g8 = vid.Geom(vid.LAY_LIN320, 320, 96)
        gc = vid.Geom(vid.LAY_CGA, 80, 96)
        gq4 = vid.Geom(vid.LAY_LIN80, 39, 350, bitplanes=True)
        gq1 = vid.Geom(vid.LAY_LIN80, 78, 350)
        P4, P8, PC, P1 = T.PF_VGA4, T.PF_VGA8, T.PF_CGA4, T.PF_MONO1
        K, C, M, CU, SZ = (T.TH_KEYS, T.TH_COLOUR, T.TH_MONO, T.TH_CUSTOM,
                           T.TH_SIZE)
        vga = [
            # (name, file, arm, the picture wanted: (pic index, kind), the
            # scale wanted, BLIT4)
            ("STORED", clip(tmp, "STORED.V88", P4, g64, thumbs(
                P4, g64, 2, K | C | CU, PAL, custom=True), PAL),
             "custom then key 1", 2, 1),
            ("OWNSIZE", clip(tmp, "OWNSIZE.V88", P4, g64, thumbs(
                P4, g64, 1, K | C, PAL), PAL), "halved", 2, 1),
            ("NONE", clip(tmp, "NONE.V88", P4, g64, None, PAL),
             "vga4_pack", 2, 1),
            ("VGA8", clip(tmp, "VGA8.V88", P8, g8, thumbs(  # (three
                P8, g8, 1, K | C, PAL8), PAL8, n=3), "key", 1, 1),  # frames:
            # key 0 alone either way, and the floppy holds the package's
            # block decoder part beside the rest, SPEC.md 98.3.23.1)
            ("CGA4V", clip(tmp, "CGA4V.V88", PC, gc, thumbs(
                PC, gc, 1, K | C | M, cgapal=CGASEL), cgapal=CGASEL, n=4),
             "key", 1, 1),
            ("SIZE", clip(tmp, "SIZE.V88", P4, g32, thumbs(
                P4, g32, 2, K | C | SZ)), "key", 2, 1),
            ("RESID", clip(tmp, "RESID.V88", P4, g32, thumbs(
                P4, g32, 1, C | CU, custom=True), resident=True),
             "custom", 1, 1),
            # (the owner's T69 Collapse: a sixteen-colour file of black and
            # white posters ONLY, on a VGA's desktop - drawn through the
            # one-bit blit, so the box on the SCREEN is the poster's bits)
            ("VGA4M", clip(tmp, "VGA4M.V88", P4, g32, thumbs(
                P4, g32, 1, K | M)), "key, on screen", 1, 0),
        ]
        cga = [
            ("CGA4C", clip(tmp, "CGA4C.V88", PC, gc, thumbs(
                PC, gc, 1, K | C | M, cgapal=CGASEL), cgapal=CGASEL, n=4),
             "key", None, 0),
            ("QUART4", clip(tmp, "QUART4.V88", P4, gq4, thumbs(
                P4, gq4, 1, K | C), n=4, flat=True), "key", 4, 1),
            ("QUART1", clip(tmp, "QUART1.V88", P1, gq1, thumbs(
                P1, gq1, 1, K | M), n=4, flat=True), "key", 4, 0),
        ]
        arm = 0
        for machine, files in (("os8088_xt_vga", vga),
                               ("os8088_5150_cga_gla", cga)):
            disk = os.path.join(tmp, "vidpost.img")
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                            "--size", "360", pkg] +
                           [f[1] for f in files], check=True,
                           capture_output=True)
            with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                             machine=machine) as ui:
                m = ui.m
                for name, v88, how, wps, w4 in files:
                    arm += 1
                    r = vid.Reader(v88)
                    w = ui.path("B:/" + os.path.basename(v88))
                    rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                                 geom.WIN_SIZE)
                    base = u16(rec, geom.W_SEG) << 4

                    def rw(n, _b=base):
                        return u16(m.read(_b + syms[n], 2))

                    def rb(n, _b=base):
                        return m.read(_b + syms[n], 1)[0]

                    def until(cond, what):
                        os88marty.until(m, cond, what, poll=0.3,
                                        limit=600.0, guest=90.0)

                    until(lambda mm: rw("vp_ploads") >= 1, "the poster")
                    ps, p4 = rw("vp_ps"), rb("vp_p4")

                    def poster(n):
                        return bytes(m.read(rw("vp_pseg") << 4, n))

                    def diff(a, b):
                        return sum(1 for x, y in zip(a, b) if x != y) + \
                            abs(len(a) - len(b))

                    def stored(pic, kind):
                        """the player's poster: the stored one halved"""
                        tat, tn, tw, th_, ts, tf = r.thumb
                        i = r.thumb_at(pic, kind)
                        hv = {ts: 0, 2 * ts: 1, 4 * ts: 2}.get(ps)
                        if i is None or hv is None:     # (a box the stored
                            return b"\xa5"             # one cannot be)
                        return halved(r.thumb_pic(i), kind, tw, th_, hv)

                    kind = "c" if w4 else "m"
                    if how == "vga4_pack":
                        cv = vid.decode_at(r, r.key(r.poster)[0])
                        want = vid.vga4_pack(cv, g64.w, g64.h, ps,
                                             vid.vga4_xlat(r.palette))[0]
                    elif how.startswith("custom"):
                        want = stored(r.nkeys, kind)
                    elif how == "halved":
                        want = stored(r.poster, kind)
                    else:
                        want = stored(r.poster, kind)
                    d = diff(poster(len(want)), want)
                    note = ""
                    if how == "custom then key 1":
                        loads = rw("vp_ploads")
                        m.key("ArrowRight")
                        until(lambda mm: rw("vp_ploads") > loads, "key 1's")
                        w1 = stored(1, kind)
                        d1 = diff(poster(len(w1)), w1)
                        note = ", key 1's %d" % d1
                        d += d1
                    print("   %2d. %-8s %-17s scale %d, %s: %d of %d bytes "
                          "differ%s" % (arm, name, how, ps,
                                        "BLIT4" if p4 else "BLIT1", d,
                                        len(want), note))
                    if how.endswith("on screen"):
                        # THE GLASS: what the card rasterised (fbuf - mode
                        # 12h's plane 0 alone is the poster's bits even when
                        # the other three are garbage, which is how the
                        # owner's T69 Collapse showed noise): every pixel of
                        # the box white where its bit is lit, black where not
                        os88marty.pace(m, 1.0)
                        px, py = rw("vp_px"), rw("vp_py")
                        pw_, ph_ = rw("vp_pdw"), rw("vp_pdh")
                        pbw = rw("vp_pbw")
                        fw, fh, rgb = m.fbuf()
                        sd = 0
                        for y in range(ph_):
                            for x in range(pw_):
                                o = ((py + y) * fw + px + x) * 3
                                pix = rgb[o:o + 3]
                                lit = want[y * pbw + x // 8] >> (7 - x % 8) & 1
                                if pix != (b"\xff\xff\xff" if lit else
                                           b"\0\0\0") and not (
                                        lit and min(pix) > 0xA0 and
                                        max(pix) - min(pix) < 8):
                                    sd += 1
                        print("       on the screen: %d of %d pixels of the "
                              "box are not the poster's black and white"
                              % (sd, ph_ * pw_))
                        d += sd
                    if d or p4 != w4 or (wps is not None and ps != wps):
                        bad.append("%s: scale %d (want %s), %s, %d bytes "
                                   "differ" % (name, ps, wps, "BLIT4" if p4
                                               else "BLIT1", d))
                    ui.menu_pick("Video Player", "Close")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
