#!/usr/bin/env python3
"""VIDEO.O88 REPEATS - SPEC.md 98.3.9, VIDEO-PLAN 14.2 item 1.

    make && python3 tests/vidrepeat.py [--layout herc|cga] [--machine NAME]

Two clips are made here, 320 x 100 and 40 frames of bars that move a
different way every frame, so a frame out of place is a different picture.
LOOP.V88 carries a SEAM from its last frame back to frame 12 and asks for
Repeat on; PLAIN.V88 is the same pictures with neither. Both play in the
window of the Hercules 5150 (--layout cga plays CGA-layout clips there,
through the SHADOW). The player holds before chosen frames (vp_stopat), and
a hold shows the frame before [vp_done] - which after a seam is the frame
the seam shows, [vp_done] being set to the one after it.

1. THE FILE'S FLAG: LOOP.V88 opens with Repeat on and its button latched,
   PLAIN.V88 with it off.
2. LAPS JOIN AT THE SEAM: LOOP.V88 is held before its last frame, before
   the frame after the seam (the screen then is frame 12), and so on round
   twice - every hold's picture against the host's decode, and the frames
   counted every lap ([vp_vseq]) climbing past the file's length.
3. R TURNS IT OFF MID-PLAY: the play then ends at the file's end.
4. A FILE WITHOUT A SEAM REPEATS TOO: R on PLAIN.V88, and the lap joins
   through keyframe 0 over a cleared canvas - the hold after it is frame 0.
5. A CLICK ON REPEAT WHILE PLAYING IN THE WINDOW turns it over and the play
   goes on - no pause, no bracket left - and the button on the glass, turned
   by the player's XOR, is the button the repaint after the play draws.

Broken on purpose: the seam decoded as a plain frame with [vp_done] counted
on (vp_frame's .seam taking the .dec path) holds at the wrong frame and
FAILS question 2; vp_warm never arming leaves the play stalled at the end
and FAILS it by timing out.
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

WB, H, NF, FPS, L = 40, 100, 40, 15.0, 12
DESK = (0xB0000, vid.LAYOUT_BY_NAME["herc"], 348)


class Stop(Exception):
    """a wait that never came true: reported as a FAIL, not a traceback"""


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def canvases():
    out = []
    for f in range(NF):
        cv = bytearray(WB * H)
        for y in range(H):
            # a bar whose place and width both depend on the frame
            x0 = (f * 3 + y // 10) % WB
            for x in range(x0, min(WB, x0 + 1 + f % 5)):
                cv[y * WB + x] = 0xFF if (y + f) % 7 else 0x81
        out.append(bytes(cv))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--layout", choices=("herc", "cga"), default="herc")
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    shadow = a.layout != "herc"
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        g = vid.Geom(vid.LAYOUT_BY_NAME[a.layout], WB, H)
        cvs = canvases()
        lp = os.path.join(tmp, "LOOP.V88")
        pl = os.path.join(tmp, "PLAIN.V88")
        vid.encode_canvases(cvs, g, lp, FPS, title="loop", loop=L,
                            repeat=True)
        vid.encode_canvases(cvs, g, pl, FPS, title="plain")
        for p in (lp, pl):
            vid.verify_v88(p)
        rl, rp = vid.Reader(lp), vid.Reader(pl)
        if not rl.loop or rl.loop[0] != L or not rl.repeat or rp.loop:
            sys.exit("vidrepeat: the clips are not what the row needs")
        disk = os.path.join(tmp, "vidrep.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, lp, pl], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m

            def opened(name):
                w = ui.path("B:/" + name)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                return w, u16(rec, geom.W_SEG) << 4

            def mk(base):
                rw = lambda n: u16(m.read(base + syms[n], 2))
                rb = lambda n: m.read(base + syms[n], 1)[0]
                ww = lambda n, v: m.write(base + syms[n],
                                          struct.pack("<H", v))
                return rw, rb, ww

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def screen(rw, r, n, what):
                sb, lay, rows = DESK
                dg = vid.Geom(lay, 90, rows)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                seg = bytes(m.read(sb, 65536))
                got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                   dg.base[ty0 + y] + tx0 + WB]
                               for y in range(H))
                want = vid.decode_at(r, n)
                d = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: the window holds frame %d, %d bytes of %d "
                      "differ" % (what, n, d, len(got)))
                if d:
                    bad.append("%s: the window differs from frame %d in %d "
                               "bytes" % (what, n, d))

            def hold(rw, rb, ww, base, n, nxt):
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                     and (not shadow or rw("vp_dy1") == 0),
                     "the hold before frame %d" % n)
                ww("vp_stopat", nxt)
                return

            def release(base):
                m.write(base + syms["vp_held"], b"\0")

            def btn(rw, base):
                """the Repeat button's rect on the glass, as rows of pixels"""
                x1, y1, x2, y2 = struct.unpack(
                    "<4H", m.read(base + syms["vp_brects"] + 32, 8))
                _, _, rows = m.vram(None)
                return (x1, y1, x2, y2), [bytes(rows[y][x1:x2 + 1])
                                          for y in range(y1, y2 + 1)]

            try:
                # --- 1, 2, 3: LOOP.V88
                w, base = opened("LOOP.V88")
                rw, rb, ww = mk(base)
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                bfl = lambda: u16(m.read(base + syms["vp_bflags"] + 8, 2))
                try:                        # the poster is made before the
                    wait(lambda mm: bfl() & 32, "the latched button", 10.0)
                except Stop:                # paint that draws the buttons
                    pass
                flags = bfl()
                print("   LOOP.V88: Repeat %d, its button's flags %04x, the seam "
                      "kind %d" % (rb("vp_rep"), flags, rb("vp_lkind")))
                if rb("vp_rep") != 1 or not flags & 32 or rb("vp_lkind") != 1:
                    bad.append("LOOP.V88 opened with Repeat %d, button %04x, "
                               "kind %d" % (rb("vp_rep"), flags, rb("vp_lkind")))
                ui.mo.to(700, 12)
                # the holds: in lap 1, before the last frame, straight after the
                # seam, and round again
                stops = (5, NF, L + 1, 25, NF, L + 1, 20)
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play to start")
                if rb("vp_winm") != 1 or rb("vp_shadow") != int(shadow):
                    bad.append("the play is not in the window as the row wants")
                seqs = []
                for i, n in enumerate(stops):
                    nxt = stops[i + 1] if i + 1 < len(stops) else 0xFFFF
                    wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                         and (not shadow or rw("vp_dy1") == 0),
                         "hold %d, before frame %d" % (i, n))
                    seqs.append(rw("vp_vseq"))
                    screen(rw, rl, n - 1, "LOOP.V88 hold %d" % i)
                    ww("vp_stopat", nxt)
                    release(base)
                print("   frames counted at the holds, every lap: %s" % seqs)
                want = [5, NF, NF + 1, NF + 1 + 25 - (L + 1), 2 * NF - L,
                        2 * NF - L + 1, 2 * NF - L + 1 + 20 - (L + 1)]
                if seqs != want:
                    bad.append("the frames counted at the holds are %s, not %s"
                               % (seqs, want))
                # --- 3: R off, mid-play: it ends at the file's end
                m.type_text("r")
                wait(lambda mm: rb("vp_rep") == 0, "R to turn Repeat off", 10.0)
                wait(lambda mm: rb("vp_played") == 1, "the play to end")
                print("   R mid-play: the play ended at frame %d, error %d"
                      % (rw("vp_done"), rb("vp_err")))
                if rw("vp_done") != NF or rb("vp_err"):
                    bad.append("with Repeat off the play ended at %d (error %d)"
                               % (rw("vp_done"), rb("vp_err")))
                # --- 4, 5: PLAIN.V88
                w2, base2 = opened("PLAIN.V88")
                rw, rb, ww = mk(base2)
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                print("   PLAIN.V88: Repeat %d, the seam kind %d"
                      % (rb("vp_rep"), rb("vp_lkind")))
                if rb("vp_rep") != 0 or rb("vp_lkind") != 2:
                    bad.append("PLAIN.V88 opened with Repeat %d, kind %d"
                               % (rb("vp_rep"), rb("vp_lkind")))
                ui.mo.to(700, 12)
                m.type_text("r")
                wait(lambda mm: rb("vp_rep") == 1, "R to turn Repeat on", 10.0)
                stops = (8, NF, 1, 9)
                ww("vp_stopat", stops[0])
                m.write(base2 + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play to start")
                for i, n in enumerate(stops):
                    nxt = stops[i + 1] if i + 1 < len(stops) else 0xFFFF
                    wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                         and (not shadow or rw("vp_dy1") == 0),
                         "PLAIN hold %d, before frame %d" % (i, n))
                    screen(rw, rp, n - 1, "PLAIN.V88 hold %d" % i)
                    if i == 0:
                        # --- 5: a click on Repeat, the play held and in the
                        # window: it turns off, the play stays
                        rect, g0 = btn(rw, base2)
                        ui.mo.click((rect[0] + rect[2]) // 2,
                                    (rect[1] + rect[3]) // 2, settle=0)
                        wait(lambda mm: rb("vp_rep") == 0, "the click to turn "
                             "Repeat off", 10.0)
                        os88marty.pace(m, 0.5)
                        rect, g1 = btn(rw, base2)
                        left = (rb("vp_winm"), rb("vp_ready"), rb("vp_upause"))
                        print("   a click on Repeat mid-play: Repeat %d, the play "
                              "(window, ready, paused) %s" % (rb("vp_rep"), left))
                        if left != (1, 1, 0):
                            bad.append("a click on Repeat left the play as %s"
                                       % (left,))
                        inner = sum(1 for a_, b_ in zip(g0[1:-1], g1[1:-1])
                                    for x, y in zip(a_[1:-1], b_[1:-1]) if x == y)
                        edge = sum(1 for x, y in zip(g0[0] + g0[-1],
                                                     g1[0] + g1[-1]) if x != y)
                        print("   the button turned over: %d interior pixels "
                              "unchanged, %d frame pixels changed" % (inner, edge))
                        if inner or edge:
                            bad.append("the button did not turn over (%d inside "
                                       "unchanged, %d of the frame changed)"
                                       % (inner, edge))
                        ui.mo.to(700, 12)
                        m.type_text("r")        # ...and back on, for the laps
                        wait(lambda mm: rb("vp_rep") == 1, "R again", 10.0)
                    ww("vp_stopat", nxt)
                    release(base2)
                # the play ends where Esc stops it; the repaint draws the button
                # from [vp_rep], which must be what the XORs left
                os88marty.pace(m, 1.0)
                rect, g2 = btn(rw, base2)
                m.key("Escape")
                wait(lambda mm: rb("vp_played") == 1, "Esc to stop it")
                os88marty.pace(m, 1.5)
                rect, g3 = btn(rw, base2)
                d = sum(1 for a_, b_ in zip(g2, g3) for x, y in zip(a_, b_)
                        if x != y)
                print("   the Repeat button after the play's repaint: %d pixels "
                      "differ from the glass the XORs left" % d)
                if d:
                    bad.append("the repaint drew the Repeat button %d pixels "
                               "different from what the play left" % d)
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
