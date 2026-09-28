#!/usr/bin/env python3
"""VIDEO.O88 plays 16 colours in mode 12h, IN THE WINDOW - SPEC.md 98.1.3.2,
VIDEO-PLAN wave 12.

    make && python3 tests/vidvga4.py [--machine NAME]

A VGA desktop IS mode 12h, so a file of its own sixteen colours plays in
the window: its records are sub-records under a Map Mask, the planes that
want one byte stored together. The clip is made here, 224 x 144 and 60
frames: boxes of every colour that move, and bursts of noise. On MartyPC's
VGA XT:

1. IS THE FILE TAKEN AS VGA4? Format, layout, bit-planes, mode 12h, and
   no shadow.
2. IS THE POSTER IN COLOUR? The Preview's claim holds vga4_pack of the
   poster key, byte for byte - what OSAPI_GFX_BLIT4 draws in the box.
3. IS EVERY FRAME RIGHT IN THE WINDOW? Mode 12h is four planes behind the
   Graphics Controller, so at each hold the RENDERED frame is read, the
   picture's pixels at the window's origin against the decode's colours.
4. FULL SCREEN too, and ON TIME.
5. DOES THE THUMB STAY BLACK AND WHITE? The decoder moves the Map Mask
   per sub-record, and the player's own thumb (vp_wbox) stores through the
   Bit Mask assuming all four planes: at each window hold the bar's inside
   rows on the glass are the thumb's black and the bar's white, and
   nothing else. Broken on purpose (the Map Mask left where the last
   sub-record put it) the thumb comes out in a colour and it FAILS. The
   menu bar after the plays is compared as well, though the kernel sets
   the Map Mask for its own drawing and so does not depend on it.
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

W, H, NF, FPS = 224, 144, 60, 15.0
STOPS = (1, 11, 26, 41, NF)
PAL8 = [tuple((v * 255 + 31) // 63 for v in vid.STD16[3 * i:3 * i + 3])
        for i in range(16)]


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp):
    rnd = random.Random(1612)
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
    out = os.path.join(tmp, "COLOR16.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_VGA4, None,
                        "vidvga4 clip", keysecs=2.0, poster=1)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        r = vid.Reader(v88)
        g = r.g
        disk = os.path.join(tmp, "vidvga4.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/COLOR16.V88")
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
                os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                guest=guest)

            def glass():
                fw, fh, rgb = m.fbuf(0)
                return fw, fh, rgb

            def picture(n, what, x0, y0):
                """the rendered picture at screen (x0, y0) against frame n"""
                want = vid.decode_at(r, n)
                fw, fh, rgb = glass()
                sx, sy = fw / 640, fh / 480
                wrong = 0
                for y in range(H):
                    ry = int((y0 + y + 0.5) * sy)
                    for x in range(W):
                        o = 3 * (ry * fw + int((x0 + x + 0.5) * sx))
                        c = PAL8[want[y * W + x]]
                        if max(abs(rgb[o + j] - c[j]) for j in range(3)) > 6:
                            wrong += 1
                print("   %s, frame %d: %d of %d pixels on the glass are "
                      "not the decode's colour" % (what, n, wrong, W * H))
                if wrong:
                    bad.append("%s: frame %d is wrong in %d pixels"
                               % (what, n, wrong))

            def thumb(n):
                """98.3.7: the bar's inside rows, black at the thumb and
                white elsewhere, on the glass"""
                fw, fh, rgb = glass()
                sx, sy = fw / 640, fh / 480
                y0 = rw("vp_cy0") + rw("vp_lbary") + 1
                x1, x2 = rw("vp_tx1"), rw("vp_tx2")
                tx = x1 + rw("vp_wtx")
                wrong = 0
                for y in range(y0, y0 + 8):
                    ry = int((y + 0.5) * sy)
                    for x in range(x1, x2 + 1):
                        o = 3 * (ry * fw + int((x + 0.5) * sx))
                        c = (0, 0, 0) if tx <= x < tx + 8 else \
                            (255, 255, 255)
                        if max(abs(rgb[o + j] - c[j]) for j in range(3)) > 6:
                            wrong += 1
                print("   the thumb before frame %d: %d pixels of the bar "
                      "not black-and-white as they should be" % (n, wrong))
                if wrong:
                    bad.append("the bar before frame %d is wrong in %d "
                               "pixels" % (n, wrong))

            def menubar():
                fw, fh, rgb = glass()
                return bytes(rgb[:3 * fw * int(16 * fh / 480)])

            until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
            until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
            ui.mo.to(8, 470)
            os88marty.pace(m, 1.0)
            bar0 = menubar()
            # --- 1
            got1 = (rb("vp_pixfmt") + 1, rb("vp_layout") + 1,
                    rb("vp_planar"), rb("vp_mode"), rb("vp_shadow"),
                    rb("vp_ok"))
            print("   format %d, layout %d, planar %d, mode %d, shadow %d, "
                  "ok %d" % got1)
            if got1 != (vid.PF_VGA4, vid.LAY_LIN80, 2, 7, 0, 1):
                bad.append("the player read the file as %r" % (got1,))
            # --- 2: the poster, key 1
            k = r.key(1)
            surf = g.surface()
            r.apply(surf, k[1], key=True)
            ps = rw("vp_ps")
            want, obw, ow, oh = vid.vga4_pack(g.canvas(surf), W, H, ps)
            got = bytes(m.read(rw("vp_pseg") << 4, len(want)))
            d = sum(1 for x, y in zip(got, want) if x != y)
            print("   the poster: scale %d, %d x %d, %d of %d bytes differ "
                  "from vga4_pack" % (ps, ow, oh, d, len(want)))
            if d or ps != 1 or (rw("vp_pbw"), rw("vp_prows")) != (obw, oh):
                bad.append("the poster is not the packed key (scale %d, %d "
                           "bytes differ)" % (ps, d))
            # --- 3: in the window
            ww("vp_stopat", STOPS[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            until(lambda mm: rb("vp_ready") == 1, "the play to start")
            if rb("vp_winm") != 1:
                bad.append("the play went full screen, not into the window")
            # THE RING GETS WHAT THE SESSION LEAVES: on this 640 KB machine
            # it is all eight slots - a keeper claimed four times its size
            # (the KB taken as 16 paragraphs) left two, and a real clip then
            # paused whole every half second on the owner's 286
            until(lambda mm: rb("vp_ready") == 1 or rb("vp_played") == 1,
                  "the play to start")
            print("   the ring: %d slots" % rw("vp_k"))
            if rw("vp_k") != 8:
                bad.append("the ring has %d slots, not 8: something the "
                           "session claims is too big" % rw("vp_k"))
            for n in STOPS:
                until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                      "the hold before frame %d" % n)
                picture(n - 1, "in the window", rw("vp_tx0") * 8,
                        rw("vp_ty0"))
                if n > 1:
                    try:                    # the foreground's next poll
                        until(lambda mm: rw("vp_wtk") == n, "the thumb",
                              10.0)
                    except os88marty.MartyError:
                        pass
                    os88marty.pace(m, 0.3)
                    thumb(n)
                i = STOPS.index(n)
                ww("vp_stopat", STOPS[i + 1] if i + 1 < len(STOPS)
                   else 0xFFFF)
                m.write(base + syms["vp_held"], b"\0")
            until(lambda mm: rb("vp_played") == 1, "the window play to end")
            # --- 4: full screen, held once, then on time
            m.write(base + syms["vp_nowin"], b"\1")
            ww("vp_stopat", 30)
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 30,
                  "the full-screen hold")
            picture(29, "full screen", (640 - W) // 2, (480 - H) // 2)
            ww("vp_stopat", 0xFFFF)
            m.write(base + syms["vp_held"], b"\0")
            until(lambda mm: rb("vp_played") == 1, "the full-screen play")
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            until(lambda mm: rb("vp_played") == 1, "the timed play")
            done, stall, late, dt, err = (rw("vp_done"), rw("vp_stall"),
                                          rw("vp_late"), rw("vp_dt"),
                                          rb("vp_err"))
            # --- 5: the desktop after
            ui.mo.to(8, 470)
            os88marty.pace(m, 1.5)
            bar1 = menubar()
    want_t = NF / FPS * 1193182 / 65536
    print("   a full-screen play: drew %d, stalls %d, late %d, %d ticks "
          "(want %.1f)" % (done, stall, late, dt, want_t))
    if done != NF or stall or late or err or abs(dt - want_t) > 2:
        bad.append("the timed play: drew %d, %d stalls, %d late, %d ticks, "
                   "error %d" % (done, stall, late, dt, err))
    dbar = sum(1 for x, y in zip(bar0, bar1) if x != y)
    print("   the menu bar after the plays: %d of %d bytes differ from "
          "before" % (dbar, len(bar0)))
    if dbar > len(bar0) // 50:      # the clock may tick; a colour cannot
        bad.append("the menu bar changed in %d bytes after the plays"
                   % dbar)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
