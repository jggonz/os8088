#!/usr/bin/env python3
"""VIDEO.O88 plays LIVE on the desktop - SPEC.md 98.3.10, VIDEO-PLAN W9.

    make && python3 tests/vidlive.py [--screen herc|cga|vga]

ONE LIVE FILE, made here: RESIDENT, three renditions of a 160 x 60 one-bit
canvas in the LIN80 layout - the shadow's, the band OSAPI_GFX_BLIT1 takes -
each drawn its own way and naming the screen it is for (CGA, Hercules,
VGA), with a seam back to frame 10 and Repeat on. On the screen named:

1. THE SCREEN'S OWN RENDITION, and LIVE: Play starts a live session - no
   bracket, the worker hired - with the desktop still the desktop.
2. EVERY FRAME, IN THE BOX: held before chosen frames over two laps (the
   worker blits the frame before the hold and then nothing), the box's
   pixels on the desktop's framebuffer against that rendition's decode.
3. THE WINDOW MOVES AND THE PICTURE GOES WITH IT: dragged mid-play, the
   next hold is in the box where the window now is.
4. ON TIME: Repeat off and unheld, the rest of the file plays at its rate -
   the guest's own clock against the frames, within 10%.
4b. A WINDOW OVER IT IS NOT DRAWN ON: a Disk window dragged across part of
   the box mid-play, its pixels inside the box unchanged over a second and a
   half of frames (SPEC.md 5.4.2.7 - gfx_blit1 walks every fragment of the
   region); then the video window raised again.
5. SPACE PAUSES IT, AND IT STAYS; Space again goes on.
6. F HANDS IT TO THE FULL SCREEN, still playing, and F again back to the
   desktop, live and playing; ESC STOPS IT.

Broken on purpose - vp_lblit's blit skipped - every hold FAILS; vp_canlive
refusing always, the play is a bracket and FAILS 1; gfx_blit1's fragment
walk taken out, the band draws over the Disk window and 4b FAILS.
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

WB, H, NF, FPS, L = 20, 60, 40, 15.0, 10
TARGETS = ("cga", "herc", "vga")
SCREEN = {"herc": ("os8088_5150_herc_gla", 0xB0000, "herc", 348, 90),
          "cga": ("os8088_5150_cga_gla", 0xB8000, "cga", 200, 80),
          "vga": ("os8088_xt_vga", 0xA0000, "lin80", 480, 80)}
HZ = 4772727.0


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def canvases(ri):
    out = []
    for f in range(NF):
        cv = bytearray(WB * H)
        for y in range(H):
            x0 = (f * (ri + 2) + y // 6) % WB
            for x in range(x0, min(WB, x0 + 3 + ri)):
                cv[y * WB + x] = (0xF0, 0x3C, 0x5A)[ri] if (y + f) % 3 \
                    else 0xFF
        out.append(bytes(cv))
    return out


def writer(ri):
    g = vid.Geom(vid.LAY_LIN80, WB, H)
    w = vid.Writer(g, int(FPS * 100), 100, vid.AUD_NONE, 0, vid.PF_MONO1,
                   title="live", keysecs=100.0, loop=L)
    surf = g.surface()
    for cv in canvases(ri):
        ch = []
        for y, b in enumerate(g.base):
            for x in range(WB):
                if surf[b + x] != cv[y * WB + x]:
                    ch.append(b + x)
                    surf[b + x] = cv[y * WB + x]
        w.frame(vid.spans(ch, surf, g), surf)
    return w


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screen", choices=sorted(SCREEN), default="herc")
    a = ap.parse_args()
    machine, vseg, dlay, rows, stride = SCREEN[a.screen]
    want_r = TARGETS.index(a.screen)
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = os.path.join(tmp, "LIVE.V88")
        vid.write_resident(v88, [writer(i) for i in range(3)], title="live",
                           repeat=True,
                           live=[vid.TARGETS[t] for t in TARGETS])
        vid.verify_v88(v88)
        r = vid.Reader(v88, want_r)
        disk = os.path.join(tmp, "live.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            w = ui.path("B:/LIVE.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n: u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))
            dg = vid.Geom(vid.LAYOUT_BY_NAME[dlay], stride, rows)

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def box(n, what):
                """the box's pixels on the desktop against frame n"""
                px, py = rw("vp_px"), rw("vp_py")
                seg = bytes(m.read(vseg, 65536))
                got = b"".join(seg[dg.base[py + y] + px // 8:
                                   dg.base[py + y] + px // 8 + WB]
                               for y in range(H))
                want = vid.decode_at(r, n)
                d = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: the box holds frame %d, %d bytes of %d differ "
                      "(at %d,%d)" % (what, n, d, len(got), px, py))
                if d:
                    bad.append("%s: frame %d differs in %d bytes"
                               % (what, n, d))

            def hold(n, what, drag=None):
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                     and rw("vp_dy1") == 0, "%s (frame %d)" % (what, n))
                os88marty.pace(m, 0.3)      # the worker's next pass, idle
                box(n - 1, what)
            try:
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                got1 = (rb("vp_flive"), rb("vp_rend"), rb("vp_target"))
                print("   live %d, rendition %d (want %d), target %d"
                      % (got1[0], got1[1], want_r, got1[2]))
                if got1[:2] != (1, want_r):
                    bad.append("the player took %s" % (got1,))
                ui.mo.to(8, rows - 8 if rows < 400 else 470)
                ww("vp_stopat", 5)
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_lsess") == 1, "a live session", 20.0)
                st = (rb("vp_lsess"), rb("vp_hired"), rb("vp_ready"),
                      rb("vp_winm"))
                print("   Play: live %d, worker %d, ready %d, bracket in "
                      "the window %d" % st)
                if st != (1, 1, 1, 0):
                    bad.append("Play started %s, not a live session" % (st,))
                # --- 2, 3: holds over two laps, a drag between two
                stops = (5, NF, L + 1, 25, NF, L + 1)
                for i, n in enumerate(stops):
                    hold(n, "hold %d" % i)
                    nxt = stops[i + 1] if i + 1 < len(stops) else 0xFFFF
                    if i == 2:              # a DRAG, the live session held
                        # (the window re-read: the file's relayout made it
                        # another size than it opened at). The window
                        # manager moves the pixels; the frames after it go
                        # where the window now is
                        ui.drag_window(ui.window("Video Player"), 24, 16)
                        ui.mo.to(8, rows - 8 if rows < 400 else 470)
                    # (the next stop AFTER the drag: a hold stops only at the
                    # frame it names, so written before, the play would run
                    # on through the drag)
                    ww("vp_stopat", nxt)
                    m.write(base + syms["vp_held"], b"\0")
                # --- 4: the rest unheld, Repeat off, on time
                m.write(base + syms["vp_rep"], b"\0")
                c0 = int(m.status().get("cycles", 0))
                d0 = rw("vp_done")
                # (polled finely: a coarse poll overshoots the end by as
                # much as a guest second, which is the whole tolerance)
                os88marty.until(m, lambda mm: rb("vp_lsess") == 0,
                                "the play's end", poll=0.02, limit=600.0,
                                guest=60.0)
                c1 = int(m.status().get("cycles", 0))
                secs = (c1 - c0) / HZ
                want = (NF - d0) / FPS
                print("   frames %d to %d unheld: %.2f guest s (want %.2f)"
                      % (d0, NF, secs, want))
                if abs(secs - want) > want * 0.1 + 0.2:
                    bad.append("the live play took %.2f s for %.2f"
                               % (secs, want))
                # --- 5: Space pauses; it stays; Space again; Esc
                m.write(base + syms["vp_rep"], b"\1")
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_lrun") == 1, "live again", 20.0)
                # --- 4b: a window over part of the box is not drawn on
                vw = ui.window("Video Player")
                px, py = rw("vp_px"), rw("vp_py")
                dk = ui.open_drive("A")
                ui.move_window(dk, px + WB * 4, py + H // 3)
                dk = ui._as_win(dk.i)
                x0, x1 = (dk.x + 2) // 8 + 1, min(px // 8 + WB, (dk.x + dk.w)
                                                    // 8) - 1
                y0, y1 = dk.y + 12, min(py + H, dk.y + dk.h) - 2

                def under():
                    seg = bytes(m.read(vseg, 65536))
                    return b"".join(seg[dg.base[y] + x0:dg.base[y] + x1]
                                    for y in range(y0, y1))
                os88marty.pace(m, 0.3)
                u0, v0 = under(), rw("vp_vseq")
                os88marty.pace(m, 1.5)
                u1, v1 = under(), rw("vp_vseq")
                dch = sum(1 for p, q in zip(u0, u1) if p != q)
                print("   a Disk window over the box: %d frames played, %d of "
                      "%d bytes of it inside the box changed" % (v1 - v0, dch,
                                                                  len(u0)))
                if dch or v1 - v0 < 5 or not u0:
                    bad.append("the Disk window over the box: %d bytes drawn "
                               "on in %d frames" % (dch, v1 - v0))
                ui.raise_window(vw)
                os88marty.pace(m, 0.5)
                m.type_text(" ")
                wait(lambda mm: rb("vp_upause") == 1, "Space to pause", 10.0)
                d1 = rw("vp_done")
                os88marty.pace(m, 1.5)
                d2 = rw("vp_done")
                m.type_text(" ")
                s2 = rw("vp_vseq")
                wait(lambda mm: rb("vp_lrun") == 1 and rw("vp_vseq") > s2 + 3,
                     "Space to go on", 10.0)
                # --- 6: F hands it to the full screen, still playing, and F
                # again hands it back to the desktop, live and playing
                os88marty.pace(m, 0.3)
                m.type_text("f")
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_lrun") == 0
                     and rb("vp_upause") == 0 and rb("vp_winm") == 0,
                     "F to the full screen, playing", 20.0)
                # (progress in vp_vseq, which counts every lap: vp_done
                # wraps at the seam, and a wait on it from frame 37 of 40
                # never comes true - the row's own intermittent)
                df = rw("vp_done")
                sf = rw("vp_vseq")
                wait(lambda mm: rw("vp_vseq") > sf + 3, "the full screen's "
                     "frames", 10.0)
                m.type_text("f")
                wait(lambda mm: rb("vp_lsess") == 1 and rb("vp_lrun") == 1,
                     "F back to live", 20.0)
                db = rw("vp_done")
                sb = rw("vp_vseq")
                wait(lambda mm: rw("vp_vseq") > sb + 3, "live frames after "
                     "it", 10.0)
                print("   F: the full screen went on from %d, and F again "
                      "went on live from %d" % (df, db))
                m.key("Escape")
                wait(lambda mm: rb("vp_lsess") == 0 and rb("vp_sess") == 0,
                     "Esc to stop it", 10.0)
                print("   paused at %d, still %d 1.5 guest s later; Space "
                      "went on and Esc stopped it" % (d1, d2))
                if d2 != d1:
                    bad.append("%d frames drawn while paused" % (d2 - d1))
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
