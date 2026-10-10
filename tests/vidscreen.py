#!/usr/bin/env python3
"""VIDEO.O88 plays 16 colours on a SCREEN OF ITS OWN, flipped, in its own
palette - SPEC.md 98.1.3.2.1, 98.3.8.2.

    make && python3 tests/vidscreen.py [--screen 640x400 ...] [--machine NAME]

A VGA4 file may name the mode it plays in - 320 x 200 (0Dh), 320 x 240 (0Dh
on 480 lines), 640 x 350 or 640 x 400 (12h on 350 or 400) - with its rows 80
plane bytes apart on every one, so two pages fit a plane and it plays
flipped; and it may carry its own sixteen colours. The clip is made here,
224 x 144 and 40 frames of boxes of every value that move and bursts of
noise, page flipped, in a palette nothing like the EGA's - so a DAC that
was never loaded, or an Attribute Controller left on the EGA's mapping,
shows. For each screen, booted on MartyPC's VGA XT:

1. IS THE FILE TAKEN AS ITS SCREEN? Format VGA4, the screen, two pages of
   its rows, flipped, the mode it is retimed from (0Dh or 12h), no shadow.
2. IS THE POSTER IN THE DESKTOP'S COLOURS? The Preview's claim holds
   vga4_pack of the poster key through vga4_xlat - each of the file's
   colours as the nearest of the desktop's sixteen - byte for byte.
3. IS EVERY FRAME RIGHT? Full screen, held before odd and even frames:
   every mode pixel the glass shows against the decode through the file's
   palette, which is only right if the mode, the retime, the Offset, the
   palette, the draw into the back page AND the flip all work. MartyPC does
   not model a 480-line retime (it scans 240 rows into 400, as for Mode X:
   tests/vidvga8.py), so 320 x 240 is checked on the 200 rows it shows -
   the canvas's 144 are inside them.

Broken on purpose: with vp_scrset's palette left out the glass is in the
EGA's colours and every hold FAILS; with vp_show's OUTs skipped the glass
stays on page 0 and the holds a frame apart differ from it.
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

W, H, NF, FPS = 224, 144, 40, 15.0
STOPS = (2, 3, 11, 12, 26, 27, NF)
# a palette nothing like the EGA's, black at 0
PAL = [(0, 0, 0)] + [((i * 4) % 64, 63 - i * 4, (i * 13) % 64)
                     for i in range(1, 16)]
FSXM_VGA0D, FSXM_VGA12 = 5, 7


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, scr):
    rnd = random.Random(1613)
    g = vid.Geom(vid.LAY_LIN80, W // 8, H, bitplanes=True)
    cvs, cv = [], bytearray(W * H)
    for f in range(NF):
        k = f % 20
        if k == 0:
            cv = bytearray([(f // 20) * 5 % 16]) * (W * H)
        elif k in (6, 7):
            for _ in range(200):
                a = rnd.randrange(W * H - 12)
                cv[a:a + 12] = bytes(rnd.randrange(16) for _ in range(12))
        else:
            x, y = (f * 9) % (W - 40), (f * 5) % (H - 30)
            for r in range(30):
                cv[(y + r) * W + x:(y + r) * W + x + 40] = \
                    bytes([(f + r // 4) % 16]) * 40
        cvs.append(bytes(cv))
    pal = bytes(v for c in PAL for v in c) + bytes(vid.PAL_BYTES - 48)
    out = os.path.join(tmp, "SCR%d.V88" % scr)
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_VGA4, pal,
                        "vidscreen clip", keysecs=2.0, poster=1, flip=True,
                        screen=scr)
    vid.verify_v88(out)
    return out


def one(scr, machine, syms, pkg, tmp, bad):
    name, SW, SH, _ = vid.SCREENS[scr]
    v88 = clip(tmp, scr)
    r = vid.Reader(v88)
    g = r.g
    pal8 = [tuple((v * 255 + 31) // 63 for v in c) for c in PAL]
    disk = os.path.join(tmp, "vidscr%d.img" % scr)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                    "--size", "360", pkg, v88], check=True,
                   capture_output=True)
    print("  %s:" % name)
    with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                     machine=machine) as ui:
        m = ui.m
        w = ui.path("B:/" + os.path.basename(v88))
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rw(n):
            return u16(m.read(base + syms[n], 2))

        def rb(n):
            return m.read(base + syms[n], 1)[0]

        def ww(n, v):
            m.write(base + syms[n], struct.pack("<H", v))

        def until(cond, what, guest=90.0):
            os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                            guest=guest)

        until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
        until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
        # --- 1
        want1 = (vid.PF_VGA4, scr, 1, SH * 80,
                 FSXM_VGA0D if SW == 320 else FSXM_VGA12, 0, 1)
        got1 = (rb("vp_pixfmt") + 1, rb("vp_screen"), rb("vp_flip"),
                rw("vp_page"), rb("vp_mode"), rb("vp_shadow"), rb("vp_ok"))
        print("   format %d, screen %d, flip %d, page %d, mode %d, shadow "
              "%d, ok %d" % got1)
        if got1 != want1:
            bad.append("%s: the player read it as %r, not %r"
                       % (name, got1, want1))
        # --- 2: the poster, key 1, in the desktop's nearest
        k = r.key(1)
        surf = g.surface()
        r.apply(surf, k[1], key=True)
        ps = rw("vp_ps")
        want, obw, ow, oh = vid.vga4_pack(g.canvas(surf), W, H, ps,
                                          vid.vga4_xlat(r.palette))
        got = bytes(m.read(rw("vp_pseg") << 4, len(want)))
        d = sum(1 for x, y in zip(got, want) if x != y)
        print("   the poster: scale %d, %d x %d, %d of %d bytes differ from "
              "vga4_pack through vga4_xlat" % (ps, ow, oh, d, len(want)))
        if d:
            bad.append("%s: the poster differs from the packed key in %d "
                       "bytes" % (name, d))
        # --- 3: every hold, full screen
        m.write(base + syms["vp_nowin"], b"\1")
        ww("vp_stopat", STOPS[0])
        m.write(base + syms["vp_played"], b"\0")
        m.type_text("p")
        ty0, tx0 = (SH - H) // 2, ((SW - W) // 16) * 8
        for si, n in enumerate(STOPS):
            until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                  "hold before frame %d" % n, 240.0)
            ref = vid.decode_at(r, n - 1)
            fw, fh, rgb = m.fbuf(0)
            sx, sy = fw / SW, fh / SH
            if sy < sx * 0.99:      # (a 480-line retime, scanned into 400)
                sy = sx
            shown = min(SH, int(fh / sy))
            wrong = 0
            for y in range(shown):
                ry = int((y + 0.5) * sy)
                for x in range(SW):
                    cy, cx = y - ty0, x - tx0
                    v = ref[cy * W + cx] if 0 <= cy < H and 0 <= cx < W \
                        else 0
                    o = 3 * (ry * fw + int((x + 0.5) * sx))
                    c = pal8[v]
                    if max(abs(rgb[o + j] - c[j]) for j in range(3)) > 6:
                        wrong += 1
            print("   hold before frame %2d: %d of %d mode pixels wrong "
                  "(%dx%d rendered, %d rows shown)"
                  % (n, wrong, SW * shown, fw, fh, shown))
            if ty0 + H > shown:
                bad.append("%s: the canvas runs past the %d rows the glass "
                           "shows" % (name, shown))
            if wrong:
                bad.append("%s: the glass before frame %d is wrong in %d "
                           "pixels" % (name, n, wrong))
            ww("vp_stopat", STOPS[si + 1] if si + 1 < len(STOPS)
               else 0xFFFF)
            m.write(base + syms["vp_held"], b"\0")
        until(lambda mm: rb("vp_played") == 1, "the play's end", 60.0)
        if rb("vp_err"):
            bad.append("%s: the play ended in an error" % name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--screen", action="append",
                    choices=[v[0] for k, v in vid.SCREENS.items() if k])
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    names = a.screen or [v[0] for k, v in vid.SCREENS.items() if k]
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        for nm in names:
            one(vid.SCREEN_BY_NAME[nm], a.machine, syms, pkg, tmp, bad)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
