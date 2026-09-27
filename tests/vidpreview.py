#!/usr/bin/env python3
"""VIDEO.O88's window is a Preview, and a play can start at a keyframe and
be paused - SPEC.md 98.4, 98.3.4 and 98.3.5, VIDEO-PLAN wave 6.

    make && python3 tests/vidpreview.py [--layout cga|herc] [--screen ...]

tests/vidplay.py's clip (150 frames at 30 fps, keyframes at 0, 60 and 120,
the poster the second of them because frame 0 is black) on a scratch floppy,
opened by double-clicking it. Four questions:

1. THE POSTER. The window's box must hold the header's poster keyframe,
   halved 2x2 into 1 with the ordered dither: the bytes in the player's
   claim against tools/os88vid.py's poster(), and the SCREEN under the box
   against the same bits - the pointer parked off it first.
2. PICKING A KEY. Right arrow, a click on the scrub bar and the Prev button
   each move the key Play starts from, and the box follows every one. At the
   last key Next is grey, and at the first Prev is.
3. PLAYING FROM IT. Play from the second key: the hook HOLDS before frame
   k+1, so the screen is the keyframe alone and must equal the host's decode
   of frame k; then at later holds the frames streamed after it, and the play
   must end on the file's last frame. The first frame it draws is k+1: the
   records before it in its super-packet were stepped over.
4. SPACE PAUSES. A play from the start is paused with Space for 1.5 guest
   seconds: not one frame is drawn while it is, and Space again finishes it
   with no stall and no late period, in the clip's own time once the pause
   is taken out.

--screen plays the clip on another layout's machine, through the SHADOW
(98.3.2): the keyframe is decoded into it and copied before the stream
starts, and each hold reads the screen where the copy put the rows.

5. F AND ALT+ENTER go into full screen PAUSED - on the first frame, or on
   the picked key's - and back out, PAUSED in the window (98.3.7); Esc then
   stops it, and Play starts next at the keyframe at or before the last
   frame drawn; a play that reached the end starts at the start again, with
   the poster back (98.3.6). Every play here is pinned to the full screen
   (vp_nowin): the window's is tests/vidwin.py's.
6. THE THUMB DRAGS: on an 8088 the picture loads once, on the release; on
   a 286 (the tier poked) it loads mid-drag too (98.4.2).

Broken on purpose: the half-scaler's thresholds swapped (the poster bytes
differ), vp_base left at 0 (the play from the key holds at the wrong frame),
and the hook's pause test removed (frames are drawn while paused).
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

# the DESKTOP's framebuffer on each adapter: base, banks, stride
DESK = {"cga": (0xB8000, 2, 80), "herc": (0xB0000, 4, 90)}
VP_BOXX, VP_OS88UI_DIS = 8, 1


class Stop(Exception):
    """a wait that never came true: reported as a FAIL, not a traceback"""


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def key_canvas(r, i):
    k, rec, *_ = r.key(i)
    surf = bytearray(65536)
    r.apply(surf, rec, key=True)
    return k, r.g.canvas(surf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--layout", choices=sorted(DESK), default="cga")
    ap.add_argument("--screen", choices=sorted(DESK),
                    help="play on this layout's machine: the shadow path")
    ap.add_argument("--machine")
    a = ap.parse_args()
    screen = a.screen or a.layout
    shadow = screen != a.layout
    machine = a.machine or vidplay.MACHINE[screen]
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    if not os.path.exists(pkg):
        sys.exit("vidpreview: no build/video.o88 - run `make`")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = vidplay.clip(tmp, a.layout)
        r = vid.Reader(v88)
        g = r.g
        keys = [e[0] for e in r.keys]
        print("   keyframes after frames %s, poster %d" % (keys, r.poster))
        if len(keys) < 3 or r.poster != 1:
            sys.exit("vidpreview: the clip's keyframes are not the ones this "
                     "row was written against")
        disk = os.path.join(tmp, "vidprev.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88], check=True,
                       capture_output=True)
        vseg, vsize = vidplay.VSEG[screen], vidplay.VSIZE[screen]
        tg = vid.Geom(vid.LAYOUT_BY_NAME[screen], g.wb, vidplay.ROWS[screen])
        ty0 = (vidplay.ROWS[screen] - g.h) // 2 // tg.banks * tg.banks
        tx0 = (tg.stride - g.wb) // 2
        rows_at = [tg.base[y + ty0] + tx0 for y in range(g.h)]
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            w = ui.path("B:/CLIP.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rw(n, off=0):
                return u16(m.read(base + syms[n] + off, 2))

            def rb(n):
                return m.read(base + syms[n], 1)[0]

            def ww(n, v):
                m.write(base + syms[n], struct.pack("<H", v))

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.3, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)" % (
                        what, str(e).split(".")[0]))

            def poster_ok(ki, what):
                """the claim's bytes, and the screen under the box"""
                _, cv = key_canvas(r, ki)
                img, bw, px, rows = vid.poster(cv, g.wb, g.h, rw("vp_ps"))
                got = bytes(m.read(rw("vp_pseg") * 16 + rw("vp_pskip"),
                                   bw * rows))
                d = sum(1 for x, y in zip(img, got) if x != y)
                if (rw("vp_pbw"), rw("vp_ppx"), rw("vp_prows")) != \
                        (bw, px, rows):
                    bad.append("%s: the poster is %s, not %s" % (
                        what, (rw("vp_pbw"), rw("vp_ppx"), rw("vp_prows")),
                        (bw, px, rows)))
                    return
                # the screen: the pointer off the box first, and only the
                # columns the box shows (a window off the byte grid loses up
                # to seven at the right, SPEC.md 98.4)
                ui.mo.to(630, 12)
                os88marty.pace(m, 0.5)
                sb, banks, stride = DESK[screen]
                x0, y0, dw = rw("vp_px"), rw("vp_py"), rw("vp_pdw")
                full, part = dw // 8, dw % 8
                sd = 0
                for y in range(rw("vp_pdh")):
                    sy = y0 + y
                    at = sb + (sy % banks) * 8192 + (sy // banks) * stride \
                        + x0 // 8
                    scr = m.read(at, full + (1 if part else 0))
                    want = img[y * bw:y * bw + full + (1 if part else 0)]
                    for j in range(full):
                        sd += bin(scr[j] ^ want[j]).count("1")
                    if part:
                        mask = (0xFF00 >> part) & 0xFF
                        sd += bin((scr[full] ^ want[full]) & mask).count("1")
                print("   %s: poster of key %d, %d bytes of %d differ in "
                      "memory, %d pixels on the screen" % (
                          what, ki, d, len(img), sd))
                if d or sd:
                    bad.append("%s: the poster differs (%d bytes, %d pixels)"
                               % (what, d, sd))

            done = stall = late = dt = ptk = d1 = d2 = 0
            try:
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                m.write(base + syms["vp_nowin"], b"\1")   # FULL SCREEN, the
                                            # plays this row reads (vidwin is
                                            # the window's)
                if rb("vp_ok") != 1:
                    sys.exit("vidpreview: the player will not play the clip "
                             "here")
                if rb("vp_shadow") != int(shadow):
                    bad.append("the player chose %s on the %s screen" % (
                        "the shadow" if rb("vp_shadow") else "native",
                        screen))
                # --- 1: the poster
                if rw("vp_kload") != r.poster or rw("vp_sel") != 0:
                    bad.append("opened: key %d loaded and %d picked, not the "
                               "poster %d and the start" % (
                                   rw("vp_kload"), rw("vp_sel"), r.poster))
                poster_ok(r.poster, "opened")
                if not rw("vp_bflags", 2) & VP_OS88UI_DIS:
                    bad.append("at the start, Prev is not grey")
                # --- 2: picking a key - the keyboard, the bar, a button
                n0 = rw("vp_ploads")
                m.key("ArrowRight")
                wait(lambda mm: rw("vp_ploads") > n0, "Right to load a key")
                if rw("vp_sel") != 1:
                    bad.append("Right picked key %d, not 1" % rw("vp_sel"))
                poster_ok(1, "Right")
                n0 = rw("vp_ploads")
                cx0, cy0 = rw("vp_cx0"), rw("vp_cy0")
                ui.mo.click(cx0 + VP_BOXX + rw("vp_lbw") * 5 // 6,
                        cy0 + rw("vp_lbary") + 4)
                wait(lambda mm: rw("vp_ploads") > n0, "the bar to load a key")
                if rw("vp_sel") != 2:
                    bad.append("the bar's last third picked key %d, not 2"
                               % rw("vp_sel"))
                poster_ok(2, "the bar")
                if not rw("vp_bflags", 6) & VP_OS88UI_DIS:
                    bad.append("at the last key, Next is not grey")
                n0 = rw("vp_ploads")
                rc = struct.unpack("<4H", m.read(base + syms["vp_brects"] + 8,
                                                 8))
                ui.mo.click((rc[0] + rc[2]) // 2, (rc[1] + rc[3]) // 2)
                wait(lambda mm: rw("vp_ploads") > n0, "Prev to load a key")
                if rw("vp_sel") != 1:
                    bad.append("Prev picked key %d, not 1" % rw("vp_sel"))
                # --- 3: play from key 1: the key alone, then what streams after
                k1 = keys[1]
                stops = (k1 + 1, k1 + 9, k1 + 40, vidplay.NF)
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                for n in stops:
                    wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                         and (not shadow or rw("vp_dy1") == 0),
                         "the hold before frame %d" % n, 120.0)
                    seg = bytes(m.read(vseg, vsize))
                    got = b"".join(seg[b:b + g.wb] for b in rows_at)
                    want = vid.decode_at(r, n - 1)
                    diff = sum(1 for i in range(len(got)) if got[i] != want[i])
                    print("   from key 1 (frame %d): hold before frame %3d, %d "
                          "bytes of %d differ" % (k1, n, diff, len(got)))
                    if diff:
                        bad.append("from key 1, the screen before frame %d "
                                   "differs in %d bytes" % (n, diff))
                    i = stops.index(n)
                    ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                wait(lambda mm: rb("vp_played") == 1, "the play to end")
                if rw("vp_done") != vidplay.NF or rb("vp_err") or \
                        rw("vp_base") != k1 + 1:
                    bad.append("the play from key 1 ended at %d, error %d, "
                               "based at %d" % (rw("vp_done"), rb("vp_err"),
                                                rw("vp_base")))
                # ...and a play that reached the end starts next from the
                # START, the poster back in the box (98.3.6)
                if rw("vp_sel") != 0 or rw("vp_dkey") != r.poster:
                    bad.append("after a play to the end, Play starts at key "
                               "%d and the box is key %d, not the start and "
                               "the poster" % (rw("vp_sel"), rw("vp_dkey")))
                # --- 4: Space pauses a play from the start
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_ready") == 1 and rw("vp_done") >= 40,
                     "the play to reach frame 40")
                m.type_text(" ")
                wait(lambda mm: rb("vp_upause") == 1, "Space to pause")
                d1 = rw("vp_done")
                os88marty.pace(m, 1.5)
                d2 = rw("vp_done")
                m.type_text(" ")
                wait(lambda mm: rb("vp_upause") == 0, "Space to resume")
                wait(lambda mm: rb("vp_played") == 1, "the paused play to end")
                done, stall, late, dt, ptk = (rw("vp_done"), rw("vp_stall"),
                                              rw("vp_late"), rw("vp_dt"),
                                              rw("vp_ptk"))
                # --- 5: F goes in PAUSED on the first frame; Space plays; F
                # comes out, and Play starts next at the key at or before
                # the last frame drawn (98.3.6)
                def screen_is(n, what):
                    seg = bytes(m.read(vseg, vsize))
                    got = b"".join(seg[b:b + g.wb] for b in rows_at)
                    diff = sum(1 for a_, b_ in zip(got, vid.decode_at(r, n))
                               if a_ != b_)
                    print("   %s: the screen is frame %d, %d bytes differ"
                          % (what, n, diff))
                    if diff:
                        bad.append("%s: the screen differs from frame %d in "
                                   "%d bytes" % (what, n, diff))
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("f")
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_upause") == 1
                     and (not shadow or rw("vp_dy1") == 0),
                     "F to go in paused")
                os88marty.pace(m, 1.0)
                if rw("vp_done") != 1:
                    bad.append("F went in having drawn %d frames, not the "
                               "first" % rw("vp_done"))
                screen_is(0, "F, paused")
                ww("vp_stopat", 40)         # held BEFORE key 1: the stop
                m.write(base + syms["vp_held"], b"\0")   # rounds back to
                m.type_text(" ")                          # key 0, which the
                wait(lambda mm: rb("vp_held") == 1 and    # box must then show
                     rw("vp_done") == 40, "Space to play to frame 40")
                m.type_text("f")            # out of the full screen, PAUSED
                wait(lambda mm: rb("vp_sess") == 1 and rb("vp_ready") == 0
                     and rw("vp_dkey") == 0xFFFE,     # in the window (98.3.7)
                     "F to come out, paused")
                # ...and Esc stops the session. SENT AGAIN if it did not:
                # the F above is seen done while the bracket is still
                # putting the desktop back, and a key pressed in that window
                # goes with the bracket's input - one run in fifty under a
                # loaded soak. What is asserted is that Esc stops it, not
                # that the harness won the race
                for tries in range(3):
                    m.key("Escape")
                    try:
                        os88marty.until(m, lambda mm: rb("vp_played") == 1,
                                        "Esc to stop it", poll=0.2,
                                        limit=120.0, guest=5.0)
                        break
                    except os88marty.MartyError:
                        pass
                wait(lambda mm: rb("vp_played") == 1, "Esc to stop it")
                if tries:
                    print("   (Esc sent %d times: the first went with the "
                          "bracket's input)" % (tries + 1))
                ww("vp_stopat", 0xFFFF)
                last = rw("vp_done") - 1
                want_k = max(i for i, k in enumerate(keys) if k <= last)
                print("   F out at frame %d: Play now starts at key %d "
                      "(want %d), the box key %d" % (
                          last, rw("vp_sel"), want_k, rw("vp_dkey")))
                if (rw("vp_sel"), rw("vp_dkey")) != (want_k, want_k):
                    bad.append("out at frame %d, Play starts at key %d and "
                               "the box is key %d, not %d" % (
                                   last, rw("vp_sel"), rw("vp_dkey"), want_k))
                # ...and the BOX SHOWS IT, on the screen and not only in the
                # claim: the owner's field report was a box that kept the old
                # picture until something else repainted it
                poster_ok(want_k, "after the stop")
                # --- 6: Alt+Enter, both ways: in paused on that key's frame
                m.write(base + syms["vp_played"], b"\0")
                m.alt("Enter", hold=0.3)
                wait(lambda mm: rb("vp_ready") == 1 and rb("vp_upause") == 1
                     and (not shadow or rw("vp_dy1") == 0),
                     "Alt+Enter to go in paused")
                os88marty.pace(m, 1.0)
                screen_is(keys[want_k], "Alt+Enter, paused")
                m.alt("Enter", hold=0.3)
                wait(lambda mm: rb("vp_sess") == 1 and rb("vp_ready") == 0
                     and rw("vp_dkey") == 0xFFFE, "Alt+Enter to come out")
                m.key("Escape")
                wait(lambda mm: rb("vp_played") == 1, "Esc to stop it")
                if rw("vp_sel") != want_k:
                    bad.append("a paused visit moved Play's key to %d"
                               % rw("vp_sel"))
                # --- 7: THE THUMB, DRAGGED. On an 8088 the picture loads
                # once, on the release (98.4.2)...
                cx0, cy0 = rw("vp_cx0"), rw("vp_cy0")
                bw, by = rw("vp_lbw"), cy0 + rw("vp_lbary") + 4
                xat = lambda f: cx0 + VP_BOXX + int(bw * f)
                n0 = rw("vp_ploads")
                ui.mo.drag(xat(0.2), by, xat(0.9), by)
                wait(lambda mm: rw("vp_ploads") > n0 and rb("vp_drag") == 0,
                     "the drag's release to load a key")
                os88marty.pace(m, 1.0)
                print("   a drag on an 8088: %d load(s), Play at key %d"
                      % (rw("vp_ploads") - n0, rw("vp_sel")))
                if rw("vp_ploads") - n0 != 1 or rw("vp_sel") != 2:
                    bad.append("an 8088's drag loaded %d times and picked "
                               "key %d, not once and 2"
                               % (rw("vp_ploads") - n0, rw("vp_sel")))
                # ...and on a 286 it follows the pointer: the tier poked, the
                # thumb held over key 1 past VP_DRAGT and nudged
                m.write(base + syms["vp_tier"], b"\1")
                mo = ui.mo
                n0 = rw("vp_ploads")
                mo.to(xat(0.9), by)
                mo._sep()
                mo._edge(True)
                mo.to(xat(0.5), by, l=True)
                os88marty.pace(m, 1.5)
                mo.to(xat(0.5) + 4, by, l=True)
                wait(lambda mm: rw("vp_ploads") > n0,
                     "a 286's drag to load key 1 before the release")
                mid = rw("vp_sel")
                mo.to(xat(0.05), by, l=True)
                mo._edge(False)
                wait(lambda mm: rb("vp_drag") == 0 and rw("vp_sel") == 0,
                     "the 286's release to pick key 0")
                print("   a drag on a 286: key %d loaded mid-drag, Play at "
                      "key %d after it" % (mid, rw("vp_sel")))
                if mid != 1:
                    bad.append("a 286's drag loaded key %d mid-drag, not 1"
                               % mid)
            except Stop as e:
                bad.append(str(e))
    want_t = vidplay.NF / vidplay.FPS * 1193182 / 65536
    print("   paused at frame %d, still %d after 1.5 guest s; drew %d, "
          "stalls %d, late %d, %d ticks played (want %.1f) and %d paused"
          % (d1, d2, done, stall, late, dt, want_t, ptk))
    if d2 != d1:
        bad.append("%d frames were drawn while paused" % (d2 - d1))
    if done != vidplay.NF or stall or late:
        bad.append("the paused play drew %d, stalled %d, late %d"
                   % (done, stall, late))
    # a shadow play runs to 95 ticks unpaused (SPEC.md 98.3.2); and the
    # paused span is taken in whole ticks at BOTH ends, which is up to a
    # tick each - two more, not one (97 was measured, before and after
    # VIDEO-PLAN wave 10, and failed the old allowance one run in three)
    if abs(dt - want_t) > (4 if shadow else 2) + 2:
        bad.append("%d ticks played for %.1f s of video" % (dt, want_t / 18.2))
    if ptk < 20:
        bad.append("only %d ticks counted as paused" % ptk)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
