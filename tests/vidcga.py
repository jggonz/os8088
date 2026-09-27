#!/usr/bin/env python3
"""VIDEO.O88 plays CGA IN COLOUR - 320 x 200 x 4 in mode 4, and 160 x 100 x
16 in the text hack - SPEC.md 98.1.3.3, 98.3.12, 98.4.6.

    make && python3 tests/vidcga.py --fmt cga4|c160 [--screen cga|vga]
                                    [--pal HEX]

ONE CLIP, made here: coloured boxes that move over a coloured ground, and
bursts of noise in every value the format has - 40 frames, a keyframe a
second. CGA4 carries the palette byte `--pal` (default 51h: mode 5's cyan,
red and white, bright, on blue - the one set the BIOS has no call for). On
the CGA 5150 or MartyPC's VGA XT:

1. IS THE FILE TAKEN AS IT IS? Format, layout, mode (4 / text), and C160
   through the shadow while CGA4 is native.
2. IS THE POSTER THE KEY'S GREY? The Preview's claim holds cga4_mono /
   c160_mono of the poster key, byte for byte - the luma of each colour
   against the 4 x 4 Bayer cell.
3. NO WINDOW: Play goes to the full screen, which is these formats' only
   place (98.3.12).
4. EVERY HELD FRAME, IN MEMORY AND ON THE GLASS: held before four frames,
   the screen's memory against the decode - CGA4 the banked mode 4 image,
   C160 each ATTRIBUTE at its odd address with 0DEh in every character -
   and every pixel's RENDERED colour against the colour the file means,
   which is what proves the palette byte and the CRTC retime.
5. IT ENDS: the play finishes with every frame drawn and no error.

Broken on purpose - vp_cgaset's palette calls skipped, or the C160 retime
- the colours on the glass are wrong and it FAILS 4.
"""
import argparse
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

NF, FPS = 40, 15.0
STOPS = (1, 12, 27, NF)
MACHINE = {"cga": "os8088_5150_cga_gla", "vga": "os8088_xt_vga"}
RGB16 = [tuple((v * 255 + 31) // 63 for v in vid.STD16[3 * i:3 * i + 3])
         for i in range(16)]


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, fmt, pal):
    """(path, pixel width, rows): the canvases as colour indexes, packed"""
    rnd = random.Random(8088)
    if fmt == "cga4":
        W, H, nv, ppb = 160, 100, 4, 4
        g = vid.Geom(vid.LAY_CGA, W // 4, H)
    else:
        W, H, nv, ppb = 120, 80, 16, 2
        g = vid.Geom(vid.LAY_C160, W // 2, H)
    bits = 8 // ppb
    cvs = []
    px = [0] * (W * H)
    for f in range(NF):
        k = f % 13
        if k == 0:
            px = [(f // 13 + 1) % nv] * (W * H)
        elif k in (5, 6):
            for _ in range(150):
                a = rnd.randrange(W * H - 8)
                for i in range(8):
                    px[a + i] = rnd.randrange(nv)
        else:
            x0, y0 = (f * 7) % (W - 32), (f * 3) % (H - 24)
            for y in range(24):
                for x in range(32):
                    px[(y0 + y) * W + x0 + x] = (f + y // 3) % nv
        cv = bytearray(g.wb * H)
        for y in range(H):
            for x in range(W):
                cv[y * g.wb + x // ppb] |= px[y * W + x] << \
                    (8 - bits * (x % ppb + 1))
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "COLOUR.V88")
    vid.encode_canvases(cvs, g, out, FPS,
                        vid.PF_CGA4 if fmt == "cga4" else vid.PF_C160,
                        title="vidcga clip", keysecs=1.0, poster=1,
                        cgapal=pal if fmt == "cga4" else None)
    vid.verify_v88(out)
    return out, W, H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fmt", choices=("cga4", "c160"), default="cga4")
    ap.add_argument("--screen", choices=sorted(MACHINE), default="cga")
    ap.add_argument("--pal", default="51")
    a = ap.parse_args()
    pal = int(a.pal, 16)
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    cols = vid.cga4_colours(pal) if a.fmt == "cga4" else list(range(16))
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88, W, H = clip(tmp, a.fmt, pal)
        r = vid.Reader(v88)
        g = r.g
        disk = os.path.join(tmp, "vidcga.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=MACHINE[a.screen]) as ui:
            m = ui.m
            w = ui.path("B:/COLOUR.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def until(cond, what, guest=90.0):
                os88marty.until(m, cond, what, poll=0.2, limit=600.0,
                                guest=guest)

            def value(cv, x, y):
                if a.fmt == "cga4":
                    return (cv[y * g.wb + x // 4] >> (6 - 2 * (x & 3))) & 3
                b = cv[y * g.wb + x // 2]
                return b >> 4 if not x & 1 else b & 15

            def held(n, what):
                want = vid.decode_at(r, n)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                vram = bytes(m.read(0xB8000, 16384))
                if a.fmt == "cga4":
                    row = [((ty0 + y) % 2) * 8192 + (ty0 + y) // 2 * 80 +
                           tx0 for y in range(H)]
                    got = b"".join(vram[b:b + g.wb] for b in row)
                    chars = 0
                else:
                    got = bytes(vram[(ty0 + y) * 160 + (tx0 + x) * 2 + 1]
                                for y in range(H) for x in range(g.wb))
                    chars = sum(1 for i in range(0, 16000, 2)
                                if vram[i] != 0xDE) \
                        if a.screen == "cga" else 0
                # (the memory only on a CGA: a VGA keeps B800h's bytes in
                # its planes, odd/even, where the debugger's read of the
                # address does not reach them - the glass is its proof)
                dm = sum(1 for x, y in zip(got, want) if x != y) \
                    if a.screen == "cga" else 0
                # the glass: every pixel's colour at its centre. (MartyPC's
                # VGA draws attribute 6 in text mode as red, 170,0,0, where a
                # VGA's is brown and its own CGA's is brown - the same file
                # - so on that screen alone either is taken for colour 6)
                vgatext = a.fmt == "c160" and a.screen == "vga"
                fw, fh, rgb = m.fbuf(0)
                wrong = 0
                for y in range(H):
                    for x in range(W):
                        if a.fmt == "cga4":
                            sx, sy = (tx0 * 4 + x) * 2 + 1, ty0 + y
                            fx, fy = sx * fw // 640, sy * fh // 200
                        elif a.screen == "cga":
                            fx, fy = (tx0 * 2 + x) * 4 + 2, (ty0 + y) * 2
                        else:               # 9-dot cells, rows of four
                            cell = tx0 + x // 2
                            fx = cell * 9 + (1 if not x & 1 else 6)
                            fy = (ty0 + y) * 4 + 1
                        o = 3 * (fy * fw + fx)
                        ci = cols[value(want, x, y)]
                        got = rgb[o:o + 3]
                        if max(abs(got[j] - RGB16[ci][j])
                               for j in range(3)) > 8 and not (
                                   ci == 6 and vgatext and
                                   tuple(got) == (170, 0, 0)):
                            wrong += 1
                print("   %s, frame %d: %d of %d bytes in memory differ, %d "
                      "characters not 0DEh, %d of %d pixels on the glass "
                      "not their colour" % (what, n, dm, len(want), chars,
                                            wrong, W * H))
                if dm or chars or wrong:
                    bad.append("%s: frame %d wrong (%d bytes, %d chars, %d "
                               "pixels)" % (what, n, dm, chars, wrong))
            try:
                until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
                until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
                # --- 1
                got1 = (rb("vp_pixfmt") + 1, rb("vp_layout") + 1,
                        rb("vp_mode"), rb("vp_shadow"), rb("vp_ok"))
                want1 = (vid.PF_CGA4, vid.LAY_CGA, 2, 0, 1) \
                    if a.fmt == "cga4" else (vid.PF_C160, vid.LAY_C160, 0,
                                             1, 1)
                print("   format %d, layout %d, mode %d, shadow %d, ok %d"
                      % got1)
                if got1 != want1:
                    bad.append("the player read the file as %r, not %r"
                               % (got1, want1))
                # --- 2: the poster, key 1
                k = r.key(1)
                surf = g.surface()
                r.apply(surf, k[1], key=True)
                cv = g.canvas(surf)
                wantp = vid.cga4_mono(cv, g.wb, H, pal) \
                    if a.fmt == "cga4" else vid.c160_mono(cv, g.wb, H)
                ps = rw("vp_ps")
                gotp = bytes(m.read(rw("vp_pseg") << 4, len(wantp)))
                dp = sum(1 for x, y in zip(gotp, wantp) if x != y)
                print("   the poster: scale %d, %d of %d bytes differ from "
                      "the host's grey" % (ps, dp, len(wantp)))
                if ps != 1 or dp:
                    bad.append("the poster (scale %d) differs in %d bytes"
                               % (ps, dp))
                # --- 3, 4
                ui.mo.to(8, 190)
                ww("vp_stopat", STOPS[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                until(lambda mm: rb("vp_ready") == 1, "the play to start")
                if rb("vp_winm"):
                    bad.append("the play went into the window")
                for i, n in enumerate(STOPS):
                    until(lambda mm: rb("vp_held") == 1 and
                          rw("vp_done") == n, "the hold before frame %d" % n)
                    os88marty.pace(m, 0.2)
                    held(n - 1, "hold %d" % i)
                    ww("vp_stopat", STOPS[i + 1] if i + 1 < len(STOPS)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                # --- 5
                until(lambda mm: rb("vp_played") == 1, "the play to end")
                res = (rw("vp_done"), rb("vp_err"), rw("vp_stall"))
                print("   the play: drew %d, error %d, stalls %d" % res)
                if res != (NF, 0, 0):
                    bad.append("the play ended as %r" % (res,))
            except os88marty.MartyError as e:
                bad.append(str(e).split(".")[0])
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
