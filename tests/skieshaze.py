#!/usr/bin/env python3
"""THE HAZE - the horizon fades, on its own toggle (SPEC.md 88.4.1.1), on MartyPC.

    python3 tests/skieshaze.py [--machine os8088_5150_herc_gla] [--clobber-ends]

cs_hazerows lays the rows the horizon's bands reach in a pass of its own, and
the part of it that can go wrong without anybody seeing it in a screenshot is
the SPAN: a split haze row tells the blit to carry only its crossings' bytes,
on the argument that everything beyond them is what it was last frame. When
that argument is false the shadow holds the new picture and the glass keeps
the old one, and nothing ever repaints it. So the row's question is the
invariant itself, asked at the top of every frame - after the last frame's
blit and before this one draws:

  1. the shadow's view rows and the card's memory under them are THE SAME
     BYTES, frame after frame, through a steady roll past the steep form both
     ways with the nose moving, and eight jumps that carry a band across the
     view's edge - with the Haze box on, on a shadow backend (Hercules by
     default; CGA with --machine). The world is PAUSED and the attitude poked
     a frame at a time: a render runs while paused, and an aeroplane left to
     fly with no airspeed stalls and points the horizon out of the view,
     which is a test of nothing;
  2. ...and the haze was really there: rows of the haze inks' KINDS in level
     flight (a row no line crosses is one ink, kept as its kind), THIN ones -
     two sky bands and one ground band of S rows each, S a 32nd of the view
     (88.4.1.1.1) - and cs_hzfill reached while banked;
  3. with the Haze box off there is none - the plain pass, untouched - and
     F6 turns it back on.

--clobber-ends is the red run (docs/WRITING-TESTS.md 1): it NOPs the jump
that gives a row whose end inks changed its whole span, and check 1 goes red
on the first roll that carries a band across the view's edge.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402
import os88build                                            # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSB_MODEX, CSB_C160 = 1, 4
CSFL_ALL, CSFL_HAZE = 3, 4
HAZE_KINDS = (18, 19, 20)           # CSI_HZS2, CSI_HZS1, CSI_HZG1
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def ends_site():
    """The `jne .full` after cs_hzfill's end-ink compare, out of a listing."""
    fd, lst = tempfile.mkstemp(prefix="skieshaze_", suffix=".lst")
    os.close(fd)
    cswidx = os.path.join(ROOT, os88build.at("build")) + os.sep
    r = subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/skies/", "-I", cswidx, "-o", lst + ".bin",
                        "-l", lst, "apps/skies/skies.asm"],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("skieshaze: the tree does not assemble:\n" + r.stderr[:400])
    rx = re.compile(r"\s*\d+\s+([0-9A-F]{8})\s+([0-9A-F]+)\s+(?:<\d+>\s*)?(.*)$")
    lines = open(lst).read().split("\n")
    os.unlink(lst)
    os.unlink(lst + ".bin")
    for i, L in enumerate(lines):
        if "cmp dl, [cs_hzrend + bx]" in L:
            for M in lines[i + 1:i + 4]:
                m = rx.match(M)
                if m and m.group(3).split(";")[0].strip().startswith("jne"):
                    return int(m.group(1), 16), bytes.fromhex(m.group(2))
    sys.exit("skieshaze: no end-ink compare in cs_hzfill to clobber")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-ends", action="store_true",
                    help="a row's changed END INKS no longer widen its span")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")
    site = ends_site() if a.clobber_ends else None

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def poke(n, d):
            m.write(lin + base + off(n), d)

        m.advance(frames=30)
        m.run()
        if site:
            m.pause()
            m.write(lin + site[0], b"\x90" * len(site[1]))
            m.run()
            print("  (a changed end ink no longer widens the span: this run "
                  "must fail)")
        m.pause()
        poke("cs_setfill", bytes([CSFL_ALL | CSFL_HAZE]))
        m.run()
        m.type_text("f")
        m.advance(frames=150)
        m.run()
        back = byte("cs_back")
        check(back not in (0, CSB_MODEX, CSB_C160),
              "a SHADOW backend: the blit is what is under test (%d)" % back)
        if back in (0, CSB_MODEX, CSB_C160):
            return 1
        # in the air and level, so the horizon is in the view
        m.pause()
        poke("cs_py", (600 * 256).to_bytes(4, "little"))
        poke("cs_state", b"\x01")
        poke("cs_pitch", b"\x00\x00")
        poke("cs_roll", b"\x00\x00")
        m.run()
        m.advance(frames=60)
        m.run()

        wh, wb0, wbn = w("cs_wh"), w("cs_wb0"), w("cs_wbn")
        shseg, fseg = w("cs_shseg"), w("cs_fsi")
        dv = m.readseg(seg, base + off("cs_devoff"), 2 * wh)
        devoff = [int.from_bytes(dv[2 * r:2 * r + 2], "little")
                  for r in range(wh)]

        def stale():
            """Bytes of the view where the glass is not the shadow, at the top
            of a frame: what the last blit failed to carry"""
            n = 0
            sh = m.readseg(shseg, 0, 80 * wh)
            for r in range(wh):
                g = m.readseg(fseg, devoff[r] + wb0, wbn)
                s = sh[80 * r + wb0:80 * r + wb0 + wbn]
                n += sum(1 for x, y in zip(g, s) if x != y)
            return n

        def kinds():
            return set(m.readseg(seg, base + off("cs_rowkind"), wh))

        DEG = 65536.0 / 360.0
        seen = {"fill": 0}

        def pose(roll, pitch):
            """One frame at this attitude, stopped at the top of the next one:
            the stale count. The world is PAUSED, which a render does not
            mind - so the attitude is the test's and not the stall's"""
            m.pause()
            poke("cs_roll", (int(roll * DEG) & 0xFFFF).to_bytes(2, "little"))
            poke("cs_pitch", (int(pitch * DEG) & 0xFFFF).to_bytes(2, "little"))
            m.bp_exec(lin + mp["cs_render"], lin + mp["cs_hzfill"])
            n = 0
            while True:
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skieshaze: cs_render never ran")
                ip = m.status()["ip"]
                if ip == mp["cs_hzfill"]:
                    seen["fill"] += 1
                    continue
                n += 1
                if n == 3:                  # a frame drawn at the pose, and
                    break                   # the top of the one after it
            out = stale()
            m.bp_exec()
            m.run()
            return out

        m.pause()
        poke("cs_pause", b"\x01")
        m.run()
        level = [pose(0, 0) for _ in range(2)]
        kk = list(m.readseg(seg, base + off("cs_rowkind"), wh))
        hz = sorted(set(k for k in kk if k in HAZE_KINDS))
        cnt = dict((k, kk.count(k)) for k in HAZE_KINDS)
        sband = (wh >> 5) + 1
        check(hz == list(HAZE_KINDS),
              "level, the rows either side of the horizon are HAZE rows, all "
              "three bands (kinds %s)" % hz)
        check(all(sband - 1 <= cnt[k] <= sband + 1 for k in (18, 19, 20)),
              "...and THIN: two sky bands and one ground band, each S = %d "
              "rows (rows %s)" % (sband, cnt))
        # a steady roll right through the steep form, back through level to
        # the left, with the nose moving the horizon up and down as well...
        sweep = list(range(0, 75, 5)) + list(range(70, -75, -5)) + \
            list(range(-70, 5, 5))
        roll = [pose(r, (r // 10) % 7 - 3) for r in sweep]
        # ...and JUMPS, where a band crosses the view's edge between two
        # frames and every row's end inks can change at once
        jumps = [pose(r, p) for r, p in ((0, 0), (20, 0), (60, -5), (-30, 8),
                                         (5, 2), (45, 0), (-60, -6), (0, 0))]
        print("      stale bytes a frame: level %s, sweep %s, jumps %s"
              % (level, roll, jumps))
        allf = level + roll + jumps
        check(seen["fill"] > 0, "banked, the split rows are laid by cs_hzfill "
              "(%d rows)" % seen["fill"])
        check(not any(allf),
              "the glass is the shadow at the top of EVERY frame - a steady "
              "roll through the steep form both ways and eight jumps (%d "
              "frames, worst %d bytes)" % (len(allf), max(allf)))

        def frames(n):
            out = []
            m.bp_exec(lin + mp["cs_render"])
            for _ in range(n):
                m.run()
                if m.wait_stop(30) is None:
                    sys.exit("skieshaze: cs_render never ran")
                out.append(stale())
            m.bp_exec()
            m.run()
            return out

        m.pause()                                   # ...and with the box
        poke("cs_setfill", bytes([CSFL_ALL]))       # off, nothing of it
        poke("cs_roll", b"\x00\x00")
        m.run()
        mod = frames(4)
        hz = sorted(k for k in kinds() if k in HAZE_KINDS)
        check(not hz and not any(mod),
              "with the Haze box off there is no haze row and nothing stale "
              "(%s, %s)" % (hz, mod))
        m.key("F6")                                 # ...and F6 is the box
        m.advance(frames=20)
        m.run()
        f6 = byte("cs_setfill")
        frames(2)
        hz = sorted(k for k in kinds() if k in HAZE_KINDS)
        check(f6 & CSFL_HAZE and hz,
              "F6 turns the haze back on (fill %#x, kinds %s)" % (f6, hz))
        m.type_text("f")
        m.advance(frames=40)
        m.run()

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
