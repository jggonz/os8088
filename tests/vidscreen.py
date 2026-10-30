#!/usr/bin/env python3
"""VIDEO.O88 plays 16 colours on a SCREEN OF ITS OWN, flipped, in its own
palette - SPEC.md 98.1.3.2.1, 98.3.8.2 - and in its own palette on the
desktop's 640 x 480 too, full screen and unflipped (98.1.3.2.2).

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

The NARROW two (320x200n, 320x240n: rows 40 plane bytes apart, 98.1.3.2.5)
flip as 1 and 2 do (98.1.3.2.5.1) and are asked for by name; the default is
the five above. --lcopy sets vp_flcw 0, so every back page is brought up by
the latch copy off the glass (98.3.8.1) rather than by decoding the last
record again - the only leg that sees vp_lcopy's row: with it back on 80
bytes, 320x200n FAILS from frame 3, and without --lcopy it passes.

640 x 480 (screen 0, mode 12h, the desktop's) has one page, so its clip is
not flipped, and it is played WITHOUT forcing the full screen: a file in its
own colours must refuse the window by itself (vp_canwin), and a play in the
window leaves the glass the desktop's, which fails every hold.

--build (with --own implied) holds frames 17-19 toward frame 20 - the
clip's scene change, a new flat ground - and gives frame 21's record
REC_UP, the copy off the glass onto the page the flip left: a scene built
ahead (--flip-build, 98.2.5.2.1). Its holds straddle it - the glass on
frame 16 through the held run, frame 20 whole, frame 21 drawn on the
copied page - so a player that took REC_UP only after a key FAILS there.

Broken on purpose: with vp_scrset's palette left out the glass is in the
EGA's colours and every hold FAILS; with vp_show's OUTs skipped the glass
stays on page 0 and the holds a frame apart differ from it; and on 640 x
480, with vp_canwin's palette test taken out the play goes into the window
and every hold fails.
"""
import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

import numpy as np

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


OWN = False                             # --own: VIDEO-SEEN-PLAN 3b's records
BUILD = False                           # --build: 98.2.5.2.1's scene built
BUILD_HELD = (17, 18, 19)               # ahead - these held toward frame 20,
BUILD_UP = 21                           # the next scene's, and this the copy
STOPS_BUILD = (2, 3, 18, 19, 20, 21, 22, 23, NF)    # after its flip
LCOPY = False                           # --lcopy: every bring-up the copy


def clip(tmp, scr):
    rnd = random.Random(1613)
    g = vid.Geom(vid.LAY_LIN80, W // 8, H, bitplanes=True,
                 stride=vid.screen_stride(scr))
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
    if BUILD:                           # (the frames held draw the scene's
        for f in BUILD_HELD:            # first picture, on the page the
            cvs[f] = cvs[BUILD_HELD[-1] + 1]    # glass is not showing)
    pal = bytes(v for c in PAL for v in c) + bytes(vid.PAL_BYTES - 48)
    out = os.path.join(tmp, "SCR%d.V88" % scr)
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_VGA4, pal,
                        "vidscreen clip", keysecs=2.0, poster=1,
                        flip=scr != 0, screen=scr, own=OWN and scr != 0,
                        held=BUILD_HELD if BUILD and scr else (),
                        ups=(BUILD_UP,) if BUILD and scr else ())
    vid.verify_v88(out)
    return out


def one(ui, scr, v88, syms, bad):
    """One screen's clip, opened off B: on the machine `main` booted ONCE for
    every screen (a boot each was ~70% of the row); the window is closed after
    the play, so each screen's player is a fresh instance as it was."""
    name, SW, SH, _ = vid.SCREENS[scr]
    r = vid.Reader(v88)
    g = r.g
    pal8 = [tuple((v * 255 + 31) // 63 for v in c) for c in PAL]
    print("  %s:" % name)
    if True:
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
            os88marty.until(m, cond, what, poll=0.05, limit=600.0,
                            guest=guest)

        until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
        until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
        # --- 1
        want1 = (vid.PF_VGA4, scr, 1 if scr else 0,
                 SH * vid.screen_stride(scr) if scr else 0,
                 FSXM_VGA0D if SW == 320 else FSXM_VGA12, 0, 1)
        got1 = (rb("vp_pixfmt") + 1, rb("vp_screen"), rb("vp_flip"),
                rw("vp_page") if scr else 0, rb("vp_mode"), rb("vp_shadow"),
                rb("vp_ok"))
        print("   format %d, screen %d, flip %d, page %d, mode %d, shadow "
              "%d, ok %d" % got1)
        if rb("vp_own") != int(OWN and scr != 0):
            bad.append("%s: the player's own-page flag is %d"
                       % (name, rb("vp_own")))
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
        # --- 3: every hold, full screen - which 640 x 480 must take by
        # itself, its colours not being the desktop's (98.1.3.2.2)
        if scr:
            m.write(base + syms["vp_nowin"], b"\1")
        if LCOPY:                       # (98.3.8.1: the copy off the glass
            ww("vp_flcw", 0)            # on every frame, never the decode)
        stops = STOPS_BUILD if BUILD and scr else STOPS
        ww("vp_stopat", stops[0])
        m.write(base + syms["vp_played"], b"\0")
        m.type_text("p")
        ty0, tx0 = (SH - H) // 2, ((SW - W) // 16) * 8
        for si, n in enumerate(stops):
            until(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                  "hold before frame %d" % n, 240.0)
            ref = vid.decode_at(r, n - 1)
            fw, fh, rgb = m.fbuf(0)
            sx, sy = fw / SW, fh / SH
            if sy < sx * 0.99:      # (a 480-line retime, scanned into 400)
                sy = sx
            shown = min(SH, int(fh / sy))
            # every mode pixel against the decode, as arrays (the per-pixel
            # Python loop this replaces was most of the row's host CPU)
            ry = ((np.arange(shown) + 0.5) * sy).astype(int)
            rx = ((np.arange(SW) + 0.5) * sx).astype(int)
            gl = np.frombuffer(bytes(rgb), np.uint8)[:fh * fw * 3].reshape(
                fh, fw, 3)[ry][:, rx].astype(int)
            v = np.zeros((shown, SW), int)
            refa = np.frombuffer(bytes(ref), np.uint8).reshape(H, W)
            y0, y1 = max(0, ty0), min(shown, ty0 + H)
            if y1 > y0:
                v[y0:y1, tx0:tx0 + W] = refa[y0 - ty0:y1 - ty0]
            want = np.array(pal8, int)[v]
            wrong = int((np.abs(gl - want).max(axis=2) > 6).sum())
            print("   hold before frame %2d: %d of %d mode pixels wrong "
                  "(%dx%d rendered, %d rows shown)"
                  % (n, wrong, SW * shown, fw, fh, shown))
            if ty0 + H > shown:
                bad.append("%s: the canvas runs past the %d rows the glass "
                           "shows" % (name, shown))
            if wrong:
                bad.append("%s: the glass before frame %d is wrong in %d "
                           "pixels" % (name, n, wrong))
            ww("vp_stopat", stops[si + 1] if si + 1 < len(stops)
               else 0xFFFF)
            m.write(base + syms["vp_held"], b"\0")
        until(lambda mm: rb("vp_played") == 1, "the play's end", 60.0)
        if rb("vp_err"):
            bad.append("%s: the play ended in an error" % name)
        ui.close(w)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    # (the NARROW two, 98.1.3.2.5, flip as the others do, on rows 40 bytes
    # apart - by name, vidscreenn's; the default is the wide five)
    wide = [v[0] for k, v in vid.SCREENS.items() if k not in vid.NARROW]
    ap.add_argument("--screen", action="append",
                    choices=[v[0] for v in vid.SCREENS.values()])
    ap.add_argument("--lcopy", action="store_true",
                    help="every back page brought up by the latch copy off "
                    "the glass (vp_flcw 0, 98.3.8.1)")
    ap.add_argument("--own", action="store_true",
                    help="the flipped screens' clips AGAINST THEIR OWN PAGE "
                    "(byte 37 7, VIDEO-SEEN-PLAN 3b)")
    ap.add_argument("--build", action="store_true",
                    help="with --own: frames 17-19 HELD toward frame 20, a "
                    "scene change, and frame 21's record bringing its page "
                    "up by the copy off the glass - a scene built ahead "
                    "(--flip-build, 98.2.5.2.1)")
    a = ap.parse_args()
    global OWN, LCOPY, BUILD
    OWN, LCOPY, BUILD = a.own or a.build, a.lcopy, a.build
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    names = a.screen or wide
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        scrs = [vid.SCREEN_BY_NAME[nm] for nm in names]
        clips = [clip(tmp, scr) for scr in scrs]
        disk = os.path.join(tmp, "vidscr.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg] + clips, check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            for scr, v88 in zip(scrs, clips):
                one(ui, scr, v88, syms, bad)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
