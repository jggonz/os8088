#!/usr/bin/env python3
"""The scrub bar's thumb follows an IN-WINDOW play - SPEC.md 98.3.7.

    make && python3 tests/vidthumb.py [--machine os8088_xt_vga|HERC]

A play in the window is a same-mode bracket, where the kernel's drawing is
not the player's to use, so the thumb is written into the desktop's own
framebuffer by the player (vp_wbox): the old 8 x 8 block white, the new one
black, only when its offset moves. It used to be a whole-bar repaint once a
second through the kernel, and on a VGA desktop not at all - the owner's
report, on the 86Box 286.

A 320 x 120 one-bit clip of 60 frames, played in the window and held
before chosen frames. At each hold the bar's eight inside rows are read off
the desktop's framebuffer - plane 0 of mode 12h on VGA (a one-bit desktop
writes all four the same), the Hercules page through vram - and the thumb
must be black exactly at (n - 1) x (bar - 8) / frames, every other inside
pixel white: a block left behind or a bar never updated both fail, and so
does a store that reached past the block.

Broken on purpose (vp_wthumb returning at once) the thumb stays at 0 and it
FAILS from the second hold.
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

W, H, NF, FPS = 320, 120, 60, 15.0
STOPS = (1, 16, 31, 45, NF)
VP_BARH, VP_BOXX, VP_THW = 10, None, 8


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp, layout):
    g = vid.Geom(layout, W // 8, H)
    cvs = []
    for f in range(NF):
        cv = bytearray(g.wb * H)
        for y in range(H):
            cv[y * g.wb + (f % g.wb)] = 0xFF
            cv[y * g.wb:(y + 1) * g.wb] = bytes(
                0xAA if (y + f) & 1 else 0x55 for _ in range(g.wb)) \
                if y % 30 == f % 30 else cv[y * g.wb:(y + 1) * g.wb]
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "THUMB.V88")
    vid.encode_canvases(cvs, g, out, FPS, vid.PF_MONO1, None, "thumb clip")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    a = ap.parse_args()
    vga = "vga" in a.machine
    layout = vid.LAY_LIN80 if vga else vid.LAY_HERC
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, layout)
        disk = os.path.join(tmp, "vidthumb.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/THUMB.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n):
                return u16(m.read(base + syms[n], 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def wait(cond, what, guest=90.0):
                os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                guest=guest)

            def bar():
                """the bar's inside rows as lists of pixels, 1 = white"""
                y0 = rw("vp_cy0") + rw("vp_lbary") + 1
                x1, x2 = rw("vp_tx1"), rw("vp_tx2")
                if vga:
                    out = []
                    for y in range(y0, y0 + VP_BARH - 2):
                        row = m.read(0xA0000 + y * 80, 80)
                        out.append([row[x >> 3] >> (7 - (x & 7)) & 1
                                    for x in range(x1, x2 + 1)])
                    return out
                _, _, rows = m.vram(None)
                return [list(rows[y][x1:x2 + 1])
                        for y in range(y0, y0 + VP_BARH - 2)]

            wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
            if rw("vp_ps") != 1 or rb("vp_ok") != 1:
                sys.exit("vidthumb: the picture is not at its own size here")
            ui.mo.to(8, 460 if vga else 340)
            lbw = rw("vp_lbw")
            ww("vp_stopat", STOPS[0])
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            wait(lambda mm: rb("vp_ready") == 1, "the play to start")
            if rb("vp_winm") != 1:
                bad.append("the play went full screen, not into the window")
            for n in STOPS:
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n,
                     "the hold before frame %d" % n)
                want = (n - 1) * (lbw - VP_THW) // NF
                try:                        # the foreground's next poll
                    wait(lambda mm: rw("vp_wtx") == want, "the thumb at %d"
                         % want, 10.0)
                except os88marty.MartyError:
                    pass                    # ...and the bar says how wrong
                os88marty.pace(m, 0.3)
                rows = bar()
                wrong = 0
                for r_ in rows:
                    for i, px in enumerate(r_):
                        if px != (0 if want <= i < want + VP_THW else 1):
                            wrong += 1
                print("   hold before frame %2d: the thumb at %d (want %d), "
                      "%d of %d inside pixels wrong"
                      % (n, rw("vp_wtx"), want, wrong,
                         len(rows) * len(rows[0])))
                if wrong:
                    bad.append("before frame %d the bar is wrong in %d "
                               "pixels (the thumb wants %d)"
                               % (n, wrong, want))
                i = STOPS.index(n)
                ww("vp_stopat", STOPS[i + 1] if i + 1 < len(STOPS)
                   else 0xFFFF)
                m.write(base + syms["vp_held"], b"\0")
            wait(lambda mm: rb("vp_played") == 1, "the play to end")
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
