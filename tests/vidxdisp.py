#!/usr/bin/env python3
"""THE VIDEO PLAYER ON AN EXTENDED DESKTOP plays where it CAN (SPEC.md 98.3.21,
53.1.2).

    make && python3 tests/vidxdisp.py

Reported off the owner's 5150 - a Hercules primary and a CGA beside it,
Control Panel -> Display -> Right: a CGA 4-colour video opens on the
Hercules, because every new window does, and is refused there ("CGA colour:
needs a CGA or VGA"); dragged onto the CGA it was refused still, because the
player asked once, at open, about the display the window was on then. The
only way to play it was to make the CGA the primary.

The machine is that one, `os8088_5150_both_gla_mono`, extended to the right,
and the clip is tests/vidcga.py's CGA4 clip on a 360 KB disk with the player:

  A. OPENED ON THE HERCULES IT IS PLAYABLE, ON THE CGA: [vp_ok] 1 and
     [vp_fsdisp] 2 (display 1, OSAPI_FSX_RUN's CH), and the card says so -
     "Plays full screen on the CGA".
  B. PLAY OPENS THE BRACKET ON THE CGA: the kernel's [fsx_wdisp] is 1 while
     it plays, the CGA's own card is in 320 x 200 colour - mode 5, the clip's
     palette 51h being mode 5's set (MartyPC's register-level answer, card 1), the play is full screen, and it plays to its last frame
     with no error. The window never moved.
  C. DRAGGED ONTO THE CGA, Play asks again (vp_recheck): its own display now
     plays it, [vp_fsdisp] 0, and the bracket is on the CGA again.
  D. DRAGGED BACK, the walk finds the CGA again: [vp_fsdisp] 2.
  E. A ONE-BIT CGA FILE, which the Hercules CAN play - but only through the
     shadow, the slow copy (98.3.21.1) - goes to the CGA's own mode instead:
     [vp_fsdisp] 2, [vp_shadow] 0, and the bracket there in 640 x 200.

Broken on purpose - vp_canplay's walk taken out, the player asks only the
window's display, as it shipped - A FAILS on [vp_ok] 0 and the refusal;
with the native walk out (a shadow play kept) E FAILS on [vp_shadow] 1; with
fsx_run's CH ignored (the bracket on the window's display) B FAILS, the
Hercules refusing mode 4: "The screen could not be taken".
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)

import os88marty, os88ui, os88build, os88sym, os88geom as geom  # noqa: E402
import os88vid as vid                                         # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
import dispcp                                                 # noqa: E402
import vidcga                                                 # noqa: E402

MACHINE = "os8088_5150_both_gla_mono"   # a Hercules primary, a CGA beside it
CGA = 1                                 # the CGA's card and display index
HERC_W = 720                            # Right: the CGA's virtual x


def u16(b, o=0):
    return struct.unpack_from("<H", bytes(b), o)[0]


def mono_clip(tmp):
    """A one-bit clip in the CGA's own layout - 320 x 100 of its 640 x 200,
    a box moving over noise: the Hercules plays it only through the shadow,
    a CGA in its own mode 6"""
    import random
    rnd = random.Random(5150)
    g = vid.Geom(vid.LAY_CGA, 40, 100)
    cvs = []
    for f in range(30):
        cv = bytearray(rnd.getrandbits(8) & 0x11 for _ in range(40 * 100))
        for y in range(20):
            for x in range(4):
                cv[(10 + y + f) * 40 + 4 + f % 30 + x] = 0xFF
        cvs.append(bytes(cv))
    out = os.path.join(tmp, "MONO.V88")
    vid.encode_canvases(cvs, g, out, 15, title="vidxdisp mono",
                        keysecs=1.0, poster=1)
    vid.verify_v88(out)
    return out


def main():
    bad = []
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88, W, H = vidcga.clip(tmp, "cga4", 0x51)
        mono = mono_clip(tmp)
        disk = os.path.join(tmp, "vidxdisp.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, v88, mono], check=True,
                       capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=MACHINE) as ui:
            m = ui.m
            S = lambda n: os88sym.linear(n)                  # noqa: E731
            settle = os88marty.settle
            cards = m.cards()
            if len(cards) != 2 or cards[CGA]["type"] != "cga":
                sys.exit("vidxdisp: %s is not a Hercules with a CGA beside "
                         "it: %r" % (MACHINE, cards))
            dispcp.open_panel(m, ui.mo, S, settle)
            dispcp.set_mode(m, ui.mo, S, settle, "right")
            dispcp.close_panel(m, ui.mo, S, settle)
            if m.read(S("vid_ndisp"), 1)[0] != 2:
                sys.exit("vidxdisp: the Control Panel did not extend the "
                         "desktop")
            cur = {}

            def attach(spec):
                """Open a file in a player of its own, and make it the one
                the helpers below read"""
                w = ui.path(spec)
                rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                             geom.WIN_SIZE)
                cur.update(w=w, base=u16(rec, geom.W_SEG) << 4)
                until(lambda mm: rb("vp_loaded") == 1, "the header", 30.0)
                until(lambda mm: rw("vp_ploads") >= 1, "the poster", 60.0)
                os88marty.settle(m)  # (the window re-fit to the file: a drag
                return w             # before it grabs where the bar WAS)

            def rw(n):
                return u16(m.read(cur["base"] + syms[n], 2))

            def rb(n):
                return m.read(cur["base"] + syms[n], 1)[0]

            def until(cond, what, guest=90.0):
                os88marty.until(m, cond, what, poll=0.2, limit=600.0,
                                guest=guest)

            def drag_to(x):
                """The window by its title bar's middle to x, as
                tests/dispdrag.py moves one across the seam"""
                w = cur["w"]
                wx, wy, ww_, wh = dispcp.win_rect(m, S, w.i)
                gx, gy = wx + ww_ // 2, wy + 9
                ui.mo.drag(gx, gy, x + ww_ // 2, gy)
                os88marty.settle(m)
                return dispcp.win_rect(m, S, w.i)

            def msg():
                return bytes(m.read(cur["base"] + rw("vp_msg"), 40)).split(
                    b"\0")[0].decode("latin-1")

            def play(leg, want_fs, want_mode="LowRes"):
                """Play, and watch the bracket's display and the CGA's mode
                while it plays; the play to its end"""
                m.write(cur["base"] + syms["vp_played"], b"\0")
                ui.mo.to(8, 190)
                m.type_text("p")
                try:
                    until(lambda mm: rb("vp_ready") == 1,
                          "%s: the play to start" % leg)
                except os88marty.MartyError:
                    bad.append("%s: the play never started - the bracket on "
                               "display %d, the card saying %r"
                               % (leg, m.read(S("fsx_wdisp"), 1)[0], msg()))
                    return
                wd = m.read(S("fsx_wdisp"), 1)[0]
                mode = str(m.video(card=CGA).get("mode"))
                print("   %s: [vp_fsdisp] %d, the bracket on display %d, "
                      "the CGA in %s, in the window %d"
                      % (leg, rb("vp_fsdisp"), wd, mode, rb("vp_winm")))
                if rb("vp_fsdisp") != want_fs:
                    bad.append("%s: [vp_fsdisp] %d, not %d"
                               % (leg, rb("vp_fsdisp"), want_fs))
                if wd != CGA:
                    bad.append("%s: the bracket is on display %d, not the "
                               "CGA" % (leg, wd))
                if want_mode not in mode:  # (mode 4, or 5: the clip's 51h
                    bad.append("%s: the CGA is in %s, not its %s mode"
                               % (leg, mode, want_mode))  # is mode 5's set)
                if rb("vp_winm"):
                    bad.append("%s: it played in the window" % leg)
                until(lambda mm: rb("vp_played") == 1,
                      "%s: the play to end" % leg, guest=120.0)
                res = (rw("vp_done"), rb("vp_err"))
                if res[1] or res[0] < 2:
                    bad.append("%s: the play ended at frame %d, error %d"
                               % ((leg,) + res))
                # THE DESKTOP BACK is vid_fsx_unblank's one-shot and not
                # [fsx_cur] - that clears before the repaint, and a press
                # in between is lost (tests/dispfsxcga.py's wait_restored)
                os88marty.until(m, lambda mm: m.read(
                    S("fsx_vndisp"), 1)[0] == 1, "%s: the desktop back" % leg,
                    poll=0.2, limit=120.0)
                os88marty.settle(m)

            w = attach("B:/COLOUR.V88")
            # --- A
            print("   A: opened at x %d on the Hercules: ok %d, display %d, "
                  "%r" % (w.x, rb("vp_ok"), rb("vp_fsdisp"), msg()))
            if (rb("vp_ok"), rb("vp_fsdisp")) != (1, CGA + 1):
                bad.append("A: a CGA4 file on the Hercules is ok %d, display "
                           "%d - not the CGA's: %r"
                           % (rb("vp_ok"), rb("vp_fsdisp"), msg()))
            elif rw("vp_msg") != syms["vp_s_oncga"]:
                bad.append("A: the card says %r, not that it plays on the "
                           "CGA" % msg())
            if not bad:
                play("B", CGA + 1)
            # --- C: onto the CGA
            if not bad:
                print("   C: dragged to %r" % (drag_to(HERC_W + 16),))
                play("C", 0)
                if rb("vp_fsdisp") != 0:
                    bad.append("C: on the CGA it still plays as another "
                               "display's (%d)" % rb("vp_fsdisp"))
            # --- D: back onto the Hercules
            if not bad:
                print("   D: dragged to %r" % (drag_to(16),))
                play("D", CGA + 1)
                if rb("vp_fsdisp") != CGA + 1:
                    bad.append("D: back on the Hercules [vp_fsdisp] is %d"
                               % rb("vp_fsdisp"))
            # --- E: a one-bit CGA file, which the Hercules plays only
            # THROUGH THE SHADOW, goes to the CGA's own mode (98.3.21.1)
            if not bad:
                attach("B:/MONO.V88")
                print("   E: one bit, CGA layout, on the Hercules: ok %d, "
                      "display %d, shadow %d, %r" % (
                          rb("vp_ok"), rb("vp_fsdisp"), rb("vp_shadow"),
                          msg()))
                if (rb("vp_ok"), rb("vp_fsdisp"), rb("vp_shadow")) != \
                        (1, CGA + 1, 0):
                    bad.append("E: a one-bit CGA file on the Hercules is ok "
                               "%d, display %d, shadow %d - not the CGA's "
                               "own mode" % (rb("vp_ok"), rb("vp_fsdisp"),
                                             rb("vp_shadow")))
                else:
                    play("E", CGA + 1, "HiRes")
    print("\nvidxdisp: %s" % ("FAIL" if bad else "ok"))
    for x in bad:
        print("  - " + x)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
