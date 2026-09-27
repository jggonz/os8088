#!/usr/bin/env python3
"""VIDEO.O88 plays IN ITS WINDOW - SPEC.md 98.3.7, VIDEO-PLAN wave 7.

    make && python3 tests/vidwin.py [--layout herc|cga] [--machine NAME]

tests/vidplay.py's clip in the Hercules layout, on the Hercules 5150, where
a 640 x 200 picture is shown at its own size - so Play plays it in the
window: a same-mode bracket, the decoder writing the desktop's own
framebuffer at the picture's place in the box. --layout cga plays the CGA
clip there instead, through the SHADOW and its copy, the file's layout not
being the desktop's. Five questions:

1. IS EVERY FRAME RIGHT, THERE? The player holds before chosen frames
   (vp_stopat), and at each hold the desktop's framebuffer, read at the
   rows and byte the window's origin names, must equal the host's decode.
2. IS IT ON TIME? A whole play in the window: every frame, no stall, no
   late period, 91 ticks within 2 (4 through the shadow).
3. A CLICK PAUSES IT BACK TO THE DESKTOP: the session waits - no bracket,
   the card and the ring kept - with the frame it stopped on in the box
   (the picture's bytes against the host's decode), and Play shows Play
   again; Space goes back into the window and it plays to the end.
4. F SWAPS WHILE PLAYING: into the full screen still playing, and F again
   back into the window still playing.
5. ESC STOPS IT, and Play starts next at the keyframe at or before the
   frame it got to.

And round the picture, before a play and after the window is DRAGGED by 5
rows (on no bank): the frame hugs the picture and the box's spare rows are
the window's white - on the owner's 5150 they were black bars at rest, and
after a drag the play landed on a bank the dragged poster was not on and
left its top rows showing. Broken on purpose (the repaint before an
in-window play skipped) it FAILS naming the rows.

And at every hold the scrub bar holds ONE black 8-pixel block, at the
offset the player last drew it at, on white - the single-pass move's masks
(98.3.7) - and it has moved along the play.

And before any of it: the poster's row is on a Hercules bank. A box placed
before the first play once rounded it to CGA's two (the vp_dinfo call
missing from vp_boxxy): it FAILS with row 46.

Broken on purpose - the canvas not read back as a bracket ends (vp_kget
skipped) - the frame in the box after the click is not the frame played.
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
import vidplay                                                # noqa: E402

DESK = {"herc": (0xB0000, vid.LAYOUT_BY_NAME["herc"], 348)}


class Stop(Exception):
    """a wait that never came true: reported as a FAIL, not a traceback"""


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


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
    nf = vidplay.NF
    want_t = nf / vidplay.FPS * 1193182 / 65536
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = vidplay.clip(tmp, a.layout)
        r = vid.Reader(v88)
        g = r.g
        keys = [e[0] for e in r.keys]
        disk = os.path.join(tmp, "vidwin.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=a.machine) as ui:
            m = ui.m
            w = ui.path("B:/CLIP.V88")
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
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def screen(n, what):
                """the desktop at the window's origin against frame n"""
                sb, lay, rows = DESK["herc"]
                dg = vid.Geom(lay, g.wb, rows)
                ty0, tx0 = rw("vp_ty0"), rw("vp_tx0")
                seg = bytes(m.read(sb, 65536))
                got = b"".join(seg[dg.base[ty0 + y] + tx0:
                                   dg.base[ty0 + y] + tx0 + g.wb]
                               for y in range(g.h))
                want = vid.decode_at(r, n)
                d = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: the window holds frame %d, %d bytes of %d "
                      "differ" % (what, n, d, len(got)))
                if d:
                    bad.append("%s: the window differs from frame %d in %d "
                               "bytes" % (what, n, d))

            def border(what):
                """The picture's frame HUGS it and the box's spare rows are
                ground (98.3.7): the row above the picture and the row below
                it are the frame's, dark across it, and the two beyond each
                are the window's white - not black bars, and not the rows of
                a poster a drag left behind"""
                w_, h_, rows = m.vram(None)
                y0, px0 = rw("vp_py"), rw("vp_px")
                y1 = y0 + g.h
                row = lambda y: rows[y][px0:px0 + g.wb * 8]
                got = [sum(row(y0 - 3)), sum(row(y0 - 2)), sum(row(y0 - 1)),
                       sum(row(y1)), sum(row(y1 + 1)), sum(row(y1 + 2))]
                full = g.wb * 8
                want = [full, full, 0, 0, full, full]
                print("   %s: lit pixels in the rows round the picture %s "
                      "(want %s)" % (what, got, want))
                if got != want:
                    bad.append("%s: the rows round the picture read %s, not "
                               "%s" % (what, got, want))

            def thumb(what):
                """The bar's inside rows hold ONE black block, 8 wide, at
                the offset the player last drew it at, and white everywhere
                else (98.3.7). The move is one store a byte of its final
                value; the two-pass one it replaced left the block right at
                a hold too, so this is the masks' check - the flicker is the
                design's, and `vp_wmove` says why"""
                w_, h_, rows = m.vram(None)
                y0 = rw("vp_cy0") + rw("vp_lbary") + 1
                x1, x2, tx = rw("vp_tx1"), rw("vp_tx2"), rw("vp_wtx")
                want = [0 if tx <= x - x1 < tx + 8 else 1
                        for x in range(x1, x2 + 1)]
                off = sum(1 for y in range(y0, y0 + 8)
                          for x, v in zip(range(x1, x2 + 1), want)
                          if rows[y][x] != v)
                print("   %s: the thumb at %d, %d bar pixels wrong"
                      % (what, tx, off))
                if off:
                    for y in range(y0 - 2, y0 + 10):
                        print("      %d %s" % (y, "".join(
                            str(rows[y][x]) for x in range(x1 - 2, x1 + 40))))
                if off:
                    bad.append("%s: %d pixels of the bar are not the thumb "
                               "at %d on white" % (what, off, tx))
                return tx

            def play_end(what):
                wait(lambda mm: rb("vp_played") == 1, what, 120.0)
                return (rw("vp_done"), rw("vp_stall"), rw("vp_late"),
                        rw("vp_dt"), rb("vp_err"))

            def check_end(what, res, tol):
                done, stall, late, dt, err = res
                print("   %s: drew %d, stalls %d, late %d, %d ticks (want "
                      "%.1f)" % (what, done, stall, late, dt, want_t))
                if done != nf or stall or late or err:
                    bad.append("%s drew %d, stalled %d, late %d, error %d"
                               % (what, done, stall, late, err))
                if abs(dt - want_t) > tol:
                    bad.append("%s: %d ticks for %.1f" % (what, dt, want_t))

            tol = 4 if shadow else 2
            try:
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                if rw("vp_ps") != 1 or rb("vp_ok") != 1:
                    sys.exit("vidwin: the picture is not at its own size "
                             "here (scale %d)" % rw("vp_ps"))
                ui.mo.to(700, 12)           # the pointer off the window
                border("the poster")
                if rw("vp_py") % 4:
                    bad.append("the poster sits at row %d, on no Hercules "
                               "bank: the play will draw it elsewhere"
                               % rw("vp_py"))
                # A DRAG moves the window's pixels by any number of rows and
                # the play puts the picture on a bank: 5 is on no bank
                ui.drag_window(w, 8, 5)
                ui.mo.to(700, 12)
                os88marty.pace(m, 1.0)
                # --- 1: every frame right, in the window
                stops = (1, 23, 64, 111, nf)
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1, "the play to start")
                if rb("vp_winm") != 1 or rb("vp_shadow") != int(shadow):
                    bad.append("the play went %s, %s" % (
                        "into the window" if rb("vp_winm") else "FULL SCREEN",
                        "through the shadow" if rb("vp_shadow")
                        else "in place"))
                if rw("vp_blabels") and u16(m.read(
                        base + syms["vp_blabels"] + 4, 2)) != \
                        syms["vp_i_pause"]:
                    bad.append("playing in the window, Play is not Pause")
                txs = []
                for n in stops:
                    wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                         and (not shadow or rw("vp_dy1") == 0),
                         "the hold before frame %d" % n)
                    screen(n - 1, "hold")
                    txs.append(thumb("hold before frame %d" % n))
                    if n == stops[0]:
                        border("playing, after a drag of 5 rows")
                    i = stops.index(n)
                    ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                if len(set(txs)) < 3:
                    bad.append("the thumb did not move along the play: %s"
                               % txs)
                check_end("the held play", play_end("the held play's end"),
                          1000)
                # --- 2: on time
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                check_end("a play in the window",
                          play_end("the play's end"), tol)
                # --- 3: a click pauses it back to the desktop
                m.write(base + syms["vp_played"], b"\0")
                ww("vp_stopat", 60)         # held there, so the click is
                m.write(base + syms["vp_held"], b"\0")    # not beaten by
                m.type_text("p")                            # the clip's end
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 60,
                     "the play to frame 60")
                ui.mo.click(700, 12)
                wait(lambda mm: rb("vp_ready") == 0 and rb("vp_sess") == 1
                     and rw("vp_dkey") == 0xFFFE, "the click to pause it")
                os88marty.pace(m, 1.0)
                at = rw("vp_done")
                img, bw, px, rows = vid.poster(vid.decode_at(r, at - 1),
                                               g.wb, g.h, 1)
                got = bytes(m.read(rw("vp_pseg") * 16, bw * rows))
                d = sum(1 for x, y in zip(img, got) if x != y)
                lab = u16(m.read(base + syms["vp_blabels"] + 4, 2))
                print("   clicked at frame %d: the box holds it, %d bytes "
                      "differ; Play is %s" % (at, d, "Play" if lab ==
                                              syms["vp_i_play"] else "Pause"))
                if d:
                    bad.append("paused at frame %d, the box differs from it "
                               "in %d bytes" % (at, d))
                if lab != syms["vp_i_play"]:
                    bad.append("paused on the desktop, Play still shows Pause")
                if rb("vp_played") or rb("vp_upause") != 1:
                    bad.append("the click ended the play instead of pausing")
                ww("vp_stopat", 0xFFFF)
                m.type_text(" ")
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_winm") == 1,
                     "Space to resume in the window")
                # (its time is not checked: the row's own hold at frame 60
                # is play time. vidsndpause measures a desktop pause's)
                check_end("a play paused by a click",
                          play_end("the resumed play's end"), 1000)
                # --- 4: F swaps while playing, both ways
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1 and rw("vp_done") >= 30,
                     "the play to frame 30")
                m.type_text("f")
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_winm") == 0
                     and rw("vp_done") >= 45, "F to go on in the full screen")
                playing_fs = rb("vp_upause") == 0
                # step 5's hold is ARMED HERE, not after the window's frame
                # 60 is seen: a hold fires at exactly its frame, and a host
                # polling late under load let the play run past 100 before
                # the write landed - the hold then never came (the soak's
                # intermittent, VIDEO-PLAN 14.1)
                ww("vp_stopat", 100)
                m.write(base + syms["vp_held"], b"\0")
                m.type_text("f")
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_winm") == 1
                     and rw("vp_done") >= 60, "F to come back to the window")
                playing_w = rb("vp_upause") == 0
                print("   F while playing: full screen %s, the window %s"
                      % ("playing" if playing_fs else "PAUSED",
                         "playing" if playing_w else "PAUSED"))
                if not (playing_fs and playing_w):
                    bad.append("F while playing did not keep it playing")
                # --- 5: Esc stops, where it got to kept: held at frame 100
                # first (armed before the second F), so the clip's end
                # cannot beat the key there
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 100,
                     "the hold at frame 100")
                m.key("Escape")
                wait(lambda mm: rb("vp_played") == 1, "Esc to stop it")
                ww("vp_stopat", 0xFFFF)
                last = rw("vp_done") - 1
                want_k = max(i for i, k in enumerate(keys) if k <= last)
                print("   Esc at frame %d: Play starts at key %d (want %d)"
                      % (last, rw("vp_sel"), want_k))
                if rw("vp_sel") != want_k or rb("vp_sess"):
                    bad.append("Esc at frame %d left Play at key %d, the "
                               "session %s" % (last, rw("vp_sel"),
                                               "alive" if rb("vp_sess")
                                               else "gone"))
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
