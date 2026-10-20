#!/usr/bin/env python3
"""THE PLAYER WINDOW'S GREY GROUND ON A SIXTEEN-COLOUR DESKTOP - SPEC.md 98.4.8.

    make && python3 tests/vidgrey.py [--machine NAME]

On a VGA (or EGA) desktop the Video Player's window is SOLID grey where no
element is - Tracker's body, CLGRAY (the owner: "Tracker uses a solid grey
background for VGA only", where the first build used the dither) - and it
paints every pixel of its content itself (WF_OWNBG), so the kernel's white
fill in front of W_PAINT is not a flash of white under the grey. The
elements are the picture's box, the scrub bar, the buttons and the info
card, which stays white between its lines.

1. THE WINDOW OWNS ITS GROUND: [vp_grey] set and WF_OWNBG in its W_FLAGS.
2. EVERY GROUND PIXEL IS THE GREY: each content pixel no element covers is
   CLGRAY on the glass - the card shut, and again with it out.
3. THE CARD IS WHITE BETWEEN ITS LINES: the rows between its text runs and
   its margins are white.
4. A ONE-BIT DESKTOP IS AS IT WAS: on the Hercules 5150 no grey, and no
   WF_OWNBG - the kernel's white fill is the ground there.

Broken on purpose - vp_ground's call taken out of vp_paint - 2 FAILS: the
content under an OWNBG window is whatever was there before.
"""
import argparse
import os
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

WF_OWNBG = 64
GREY = tuple((v * 255 + 31) // 63 for v in vid.STD16[3 * 7:3 * 7 + 3])
NBTN = 7                        # VP_NBTN: the four, Repeat, Mute (SPEC.md
                                # 98.3.17) and the info card's
VP_BOXX, VP_BOXY, VP_BARH = 8, 6, 10
VP_TXTY, VP_LPITCH, VP_LINES, VP_CARDW = 6, 11, 8, 280
VP_LINESB = 8                   # ...with the buttons in the card (VPDIAG=1
                                # makes VP_LINES 12: SPEC.md 98.3)
VP_CARDH = VP_TXTY + VP_LINES * VP_LPITCH + 4
VP_CARDHB = 96 + 20 + 4


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def s16(b, i=0):
    return struct.unpack_from("<h", b, i)[0]


def clip(tmp):
    g = vid.Geom(vid.LAY_CGA, 40, 100)
    cvs = [bytes(((x * 7 + y * 3 + f) & 0xFF) for y in range(100)
                 for x in range(40)) for f in range(8)]
    out = os.path.join(tmp, "GREY.V88")
    vid.encode_canvases(cvs, g, out, 15.0, title="grey", keysecs=1.0)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()
    vga = "vga" in a.machine or "ega" in a.machine
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        disk = os.path.join(tmp, "grey.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", os88build.at("build/video.o88"), v88],
                       check=True, capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/GREY.V88")
            wrec = ui._S("wm_wins") + w.i * geom.WIN_SIZE
            base = u16(m.read(wrec, geom.WIN_SIZE), geom.W_SEG) << 4
            rw = lambda n: u16(m.read(base + syms[n], 2))
            sw = lambda n: s16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            os88marty.until(m, lambda mm: rw("vp_ploads") >= 1, "the poster",
                            poll=0.3, limit=600.0, guest=60.0)
            os88marty.pace(m, 1.0)
            flags = u16(m.read(wrec, geom.WIN_SIZE), geom.W_FLAGS)
            print("   1: grey %d, W_FLAGS %04x (WF_OWNBG %s)"
                  % (rb("vp_grey"), flags,
                     "set" if flags & WF_OWNBG else "clear"))
            if (rb("vp_grey"), bool(flags & WF_OWNBG)) != (int(vga), vga):
                bad.append("1: grey %d and WF_OWNBG %s on a%s desktop"
                           % (rb("vp_grey"), bool(flags & WF_OWNBG),
                              " sixteen-colour" if vga else " one-bit"))

            def holes():
                """every element's rect, on the screen, as the player's own
                numbers put it"""
                cx0, cy0 = sw("vp_cx0"), sw("vp_cy0")
                lbw, lbary = rw("vp_lbw"), rw("vp_lbary")
                out = [(sw("vp_bx1") - 1, sw("vp_by1") - 1, sw("vp_bx2") + 1,
                        sw("vp_by2") + 1),
                       (cx0 + VP_BOXX - 1, cy0 + lbary, cx0 + VP_BOXX + lbw,
                        cy0 + lbary + VP_BARH - 1)]
                br = m.read(base + syms["vp_brects"], NBTN * 8)
                out += [struct.unpack_from("<4h", br, 8 * i)
                        for i in range(NBTN)]
                card = None
                if rb("vp_lcard"):
                    x = cx0 + rw("vp_lcardx")
                    card = (x - 4, cy0 + VP_TXTY - 3, x + VP_CARDW + 3,
                            cy0 + (VP_CARDHB if rb("vp_lbin") else
                                   VP_CARDH) - 1)
                    out.append(card)
                return out, card

            def ground(what):
                # the pixels: the VGA's as rendered (its planes are not
                # memory), a one-bit screen's own bits (os88marty.vram) -
                # MartyPC renders Hercules' 348 rows into 350 lines
                if vga:
                    fw, fh, img = m.fbuf(0)
                    sx, sy = fw / 640.0, fh / 480.0

                    def rgb(x, y):
                        o = 3 * (int((y + 0.5) * sy) * fw +
                                 int((x + 0.5) * sx))
                        return tuple(img[o:o + 3])
                    black = lambda x, y: sum(rgb(x, y)) < 384
                else:
                    _, _, bits = m.vram(None)
                    black = lambda x, y: not bits[y][x]
                hs, card = holes()
                sw_, sh_ = (640, 480) if vga else (720, 348)
                on = lambda x, y: 0 <= x < sw_ and 0 <= y < sh_  # (a window
                # may hang off the screen's edge: that part is not glass)
                cx0, cy0 = sw("vp_cx0"), sw("vp_cy0")
                cw, ch = rw("vp_lcw"), rw("vp_lch")
                wrong = n = 0
                for y in range(cy0, cy0 + ch):
                    for x in range(cx0, cx0 + cw):
                        if any(h[0] <= x <= h[2] and h[1] <= y <= h[3]
                               for h in hs) or not on(x, y):
                            continue
                        n += 1
                        if vga:
                            if max(abs(a - b) for a, b in
                                   zip(rgb(x, y), GREY)) > 6:
                                wrong += 1
                        elif black(x, y):
                            wrong += 1

                cwrong = cn = 0
                if card:
                    lines = [(cx0 + rw("vp_lcardx"), cy0 + VP_TXTY + i *
                              VP_LPITCH) for i in range(
                                  VP_LINESB if rb("vp_lbin") else VP_LINES)]
                    for y in range(card[1], card[3] + 1):
                        for x in range(card[0], card[2] + 1):
                            if any(lx <= x < lx + VP_CARDW and ly <= y <
                                   ly + 8 for lx, ly in lines) or \
                                    any(h[0] <= x <= h[2] and h[1] <= y <=
                                        h[3] for h in hs[2:8]) or \
                                    not on(x, y):
                                continue
                            cn += 1
                            if black(x, y):
                                cwrong += 1
                print("   %s: %d of %d ground pixels not %s; %d of %d of the "
                      "card's gaps not white" % (
                          what, wrong, n, "CLGRAY" if vga else "white",
                          cwrong, cn))
                if wrong or cwrong:
                    bad.append("%s: %d ground and %d card pixels wrong"
                               % (what, wrong, cwrong))
            ground("2: the card shut")
            m.type_text("i")
            os88marty.until(m, lambda mm: rb("vp_lcard") == 1, "the card",
                            poll=0.3, limit=600.0, guest=30.0)
            os88marty.pace(m, 3.0)
            ground("2, 3: the card out")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
