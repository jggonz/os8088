#!/usr/bin/env python3
"""THE LOGO VIDEO plays LIVE on every screen - VIDEO-PLAN 14.3, SPEC.md
98.3.10. The committed apps/video/os8088.v88 (made by tools/os88logovid.py,
by hand: it needs numpy and the build does not), on the screen named:

    make && python3 tests/vidlogo.py [--screen herc|cga|vga]

1. THE FILE IS WHAT THE GENERATOR SAYS: resident, live, three renditions
   each naming its screen, keys at 0 and LOOP with the second the poster,
   the seam back to LOOP, Repeat on, and under the
   owner's budget (120 KB, VIDEO-PLAN 14.7).
2. THE SCREEN'S OWN RENDITION PLAYS LIVE: at its own size, so the box shows
   it whole (vp_ps 1) - the one thing a file can be refused Live for.
   And the status line says READY: a rendition made for this screen is
   not "Made for VGA: plays via a copy" because its layout is lin80's.
3. EVERY HELD FRAME, IN THE BOX: before the burn, mid-burn, the last frame
   and the first after the seam, over two laps - the desktop's pixels
   against the rendition's host decode.
4. BACK TO THE START BY THE THUMB, mid-loop: the thumb dragged to the
   bar's left end stops the looping play with the start picked, and Play
   plays frame 4 again. The file has a key at 0 for it (the generator once
   left only the loop's, and the bar had one place); and the release must
   pick even the key the session STARTED from.

Broken on purpose - the release's session test taken out, it FAILS 4 (the
loop never stops). And the CGA and Hercules renditions' targets swapped - on
CGA it FAILS 1, takes the Hercules picture at box scale 2, and then FAILS 2:
no live session, because a box that shows the picture scaled is not Live.
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

LOGO = "apps/video/os8088.v88"
NF, L, BUDGET = 105, 57, 120 * 1024     # tools/os88logovid.py's NFRAMES, LOOP
ORDER = ("vga", "herc", "cga")          # the file's renditions
SCREEN = {"herc": ("os8088_5150_herc_gla", 0xB0000, "herc", 348, 90),
          "cga": ("os8088_5150_cga_gla", 0xB8000, "cga", 200, 80),
          "vga": ("os8088_xt_vga", 0xA0000, "lin80", 480, 80)}


class Stop(Exception):
    pass


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screen", choices=sorted(SCREEN), default="herc")
    a = ap.parse_args()
    machine, vseg, dlay, rows, stride = SCREEN[a.screen]
    want_r = ORDER.index(a.screen)
    os.chdir(ROOT)
    bad = []
    # --- 1: the file
    size = os.path.getsize(LOGO)
    r = vid.Reader(LOGO, want_r)
    tg = [vid.Reader(LOGO, i).target for i in range(r.nrend)]
    print("   %s: %d bytes, %d renditions (targets %s), %d frames, loop "
          "from %s, live %d, resident %d" % (LOGO, size, r.nrend, tg,
                                             r.frames, r.loop[0], r.live,
                                             r.resident))
    ks = [e[0] for e in r.keys]
    print("   keyframes at %s, poster %d" % (ks, r.poster))
    if size > BUDGET or not (r.live and r.resident) or r.nrend != 3 or \
            r.frames != NF or r.loop[0] != L or ks != [0, L] or \
            r.poster != 1 or tg != [vid.TARGETS[n] for n in ORDER]:
        bad.append("the file is not the generator's")
    vid.verify_v88(LOGO)
    WB, H = r.g.wb, r.g.h
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        disk = os.path.join(tmp, "logo.img")
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "360", pkg, LOGO],
                       check=True, capture_output=True)
        with os88ui.boot(os88build.at("build/os8088-360.img"), apps=disk,
                         machine=machine) as ui:
            m = ui.m
            w = ui.path("B:/OS8088.V88")
            rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE,
                         geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4
            rw = lambda n: u16(m.read(base + syms[n], 2))
            rb = lambda n: m.read(base + syms[n], 1)[0]
            ww = lambda n, v: m.write(base + syms[n], struct.pack("<H", v))
            dg = vid.Geom(vid.LAYOUT_BY_NAME[dlay], stride, rows)

            def wait(cond, what, guest=60.0):
                try:
                    os88marty.until(m, cond, what, poll=0.1, limit=600.0,
                                    guest=guest)
                except os88marty.MartyError as e:
                    raise Stop("%s never happened (%s)"
                               % (what, str(e).split(".")[0]))

            def box(n, what):
                px, py = rw("vp_px"), rw("vp_py")
                seg = bytes(m.read(vseg, 65536))
                got = b"".join(seg[dg.base[py + y] + px // 8:
                                   dg.base[py + y] + px // 8 + WB]
                               for y in range(H))
                want = vid.decode_at(r, n)
                d = sum(1 for x, y in zip(got, want) if x != y)
                print("   %s: the box holds frame %d, %d bytes of %d differ"
                      % (what, n, d, len(got)))
                if d:
                    bad.append("%s: frame %d differs in %d bytes"
                               % (what, n, d))
            try:
                wait(lambda mm: rw("vp_ploads") >= 1, "the poster")
                got1 = (rb("vp_flive"), rb("vp_rend"), rb("vp_target"),
                        rw("vp_ps"))
                print("   live %d, rendition %d (want %d), target %d, box "
                      "scale %d" % (got1[0], got1[1], want_r, got1[2],
                                    got1[3]))
                if (got1[0], got1[1], got1[3]) != (1, want_r, 1):
                    bad.append("the player took %s" % (got1,))
                msg = rw("vp_msg")
                name = next((k for k, v in syms.items() if v == msg and
                             k.startswith("vp_s_")), hex(msg))
                print("   the status line: %s" % name)
                if msg != syms["vp_s_ready"]:
                    bad.append("the status line says %s, not ready: the "
                               "rendition is MADE for this screen" % name)
                ui.mo.to(8, rows - 8 if rows < 400 else 470)
                stops = (10, 30, NF, L + 1, 80, NF, L + 1)
                ww("vp_stopat", stops[0])
                m.write(base + syms["vp_played"], b"\0")
                m.type_text("p")
                wait(lambda mm: rb("vp_lsess") == 1, "a live session", 20.0)
                if (rb("vp_hired"), rb("vp_winm")) != (1, 0):
                    bad.append("Play started no live worker")
                for i, n in enumerate(stops):
                    wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == n
                         and rw("vp_dy1") == 0, "hold %d (frame %d)" % (i, n))
                    os88marty.pace(m, 0.3)
                    box(n - 1, "hold %d" % i)
                    ww("vp_stopat", stops[i + 1] if i + 1 < len(stops)
                       else 0xFFFF)
                    m.write(base + syms["vp_held"], b"\0")
                # --- 4: BACK TO THE START, by the thumb, mid-loop: the key
                # a looping play started from is the one the release picks,
                # and it has to be news while the session is elsewhere
                os88marty.pace(m, 0.5)
                cx0, cy0 = rw("vp_cx0"), rw("vp_cy0")
                bw, by = rw("vp_lbw"), cy0 + rw("vp_lbary") + 4
                xat = lambda f: cx0 + 8 + int(bw * f)          # VP_BOXX
                was = rw("vp_done")
                ui.mo.drag(xat(0.7), by, xat(0.02), by)
                wait(lambda mm: rb("vp_drag") == 0 and rb("vp_sess") == 0,
                     "the thumb's release to stop the loop", 20.0)
                print("   the thumb dragged to the start at frame %d: "
                      "session %d, Play at key %d"
                      % (was, rb("vp_sess"), rw("vp_sel")))
                if rw("vp_sel") != 0:
                    bad.append("the thumb at the start picked key %d"
                               % rw("vp_sel"))
                ww("vp_stopat", 5)
                m.write(base + syms["vp_held"], b"\0")
                m.write(base + syms["vp_played"], b"\0")
                ui.mo.to(8, rows - 8 if rows < 400 else 470)
                m.type_text("p")
                wait(lambda mm: rb("vp_held") == 1 and rw("vp_done") == 5
                     and rw("vp_dy1") == 0, "Play from the start to frame 5")
                os88marty.pace(m, 0.3)
                box(4, "played again from the start")
                ww("vp_stopat", 0xFFFF)
                m.write(base + syms["vp_held"], b"\0")
                m.key("Escape")
                wait(lambda mm: rb("vp_lsess") == 0, "Esc to stop it", 10.0)
            except Stop as e:
                bad.append(str(e))
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
