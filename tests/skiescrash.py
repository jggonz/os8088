#!/usr/bin/env python3
"""THE WINDSHIELD IS DRAWN WHOLE (SPEC.md 88.7.11.1).

    python3 tests/skiescrash.py [--machine os8088_5150_herc_gla]

A crash freezes the picture and draws eight cracks over it for CS_CRASHT
ticks. Reported from the air, on Hercules: the UPPER part of the view is wrong
after a crash.

It is 88.13.3.1's rule, applied one caller short. cs_seg reads three words to
decide what a segment owes the glass, and cs_crackle runs AFTER cs_scene, so
all three hold the LAST OBJECT DRAWN's values: cs_pinview would skip the clip,
cs_pwhole would skip the marking outright, and cs_markacc would accumulate
into an object box cs_drawobj flushed a moment ago and resets for its first
object next frame. Either way the marks are lost, and an unmarked run never
reaches the glass (88.3.1). The horizon and the panel (cs_pclip) both take all
three stores; cs_crackle took only cs_pinview.

WHAT THAT LOOKS LIKE is a windshield with a hole in it: a crack appears
wherever SOMETHING ELSE marked the row - the ground, a building, the horizon
segment - and nowhere else. Over open sky, which on Hercules is the top of the
view and is black, nothing marks a row and the cracks up there were never
drawn at all. The star and the ring stop dead at the horizon.

The row pins 300 m over Paris with the nose down, so the view has real sky
above a real city below, crashes the aeroplane where it stands, and counts
what the crash ADDS to each half:

  1. the crash reaches the glass at all - so a row that poked a state byte
     and photographed two identical frames would not pass;
  2. and it reaches THE SKY, above the horizon the guest itself reports in
     cs_hzy0 - 129 lit pixels against 0;
  3. and EVERY ROW IT WROTE IS RECORDED: the shadow and the frame's span set
     are read on either side of cs_crackle, and a row whose bytes changed
     must have a span that covers them. The shipped routine changes 111 rows
     and leaves 75 of them outside their own span, most with no span at all;
--clobber-crash is the red run (docs/WRITING-TESTS.md 1): it NOPs the two
stores the fix added and the one that puts the borrow back, which is
cs_crackle exactly as it shipped, and CHECKS 2 AND 3 must go red.

AND IT IS ALSO WHY A CRACK OUTLIVES THE CRASH, which is how it was reported:
cs_blit copies each row over cur UNION prv, and the next frame's sky/ground
pass refills only prv. A crack run that was never marked itself but fell
inside the PREVIOUS frame's span reaches the glass once - and once the view
moves on, no span covers it again, so nothing ever erases it. That needs a
MOVING view to happen, which is why it appears on every real crash and on
none of the pinned poses this row can hold still. Check 3 asks the question
without needing the leftover to survive: it snapshots the shadow and the
frame's span set on either side of cs_crackle and holds the routine to
88.3.1's own rule.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CS_ST_CRASH = 2
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-crash", action="store_true",
                    help="cs_crackle marks nothing again: check 2 goes red")
    ap.add_argument("--shot", help="write the three pictures here, as a stem")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def poke(n, d):
            m.write(lin + base + off(n), d)

        def word(n):
            v = int.from_bytes(m.read(lin + base + off(n), 2), "little")
            return v - 65536 if v >= 32768 else v

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        m.advance(frames=40)
        m.run()
        # ...AND WAIT FOR cs_reset, which is what puts the aeroplane on the
        # runway: a pin poked before it is overwritten. This row gave the
        # bracket a fixed 100 frames, which was a guess that held until the
        # image grew - then a soak lane pinned first, the horizon came out at
        # row 55 where the pose puts it at 30, and the crash drew nothing
        m.pause()
        poke("cs_inited", b"\x00")
        m.run()
        m.type_text("f")
        for _ in range(60):
            m.advance(frames=20)
            m.run()
            if byte("cs_back") and byte("cs_inited"):
                break
        else:
            sys.exit("skiescrash: the flight never initialised")
        m.advance(frames=20)
        m.run()

        def guest_frames(n, cap=40):
            """Advance until the GUEST has drawn n more frames. m.advance
            counts the emulator's, and a Clear Skies frame on a 4.77 MHz 8088
            is 150-280 ms of them, so a fixed count is a guess that reads
            differently on every scene."""
            want = word("cs_frames") + n
            for _ in range(cap):
                m.advance(frames=30)
                m.run()
                if word("cs_frames") >= want:
                    return True
            return False

        if a.clobber_crash:
            # cs_crackle's `mov byte [cs_pwhole],0` and `mov byte
            # [cs_ownmk],-1`, and the `mov byte [cs_ownmk],0` that puts the
            # borrow back: fifteen bytes of NOP is the routine as it shipped.
            lo, hi = mp["cs_crackle"], mp["cs_crx"]
            code = m.read(lin + lo, hi - lo)
            pats = [b"\xC6\x06" + (base + off(n)).to_bytes(2, "little") + v
                    for n, v in (("cs_pwhole", b"\x00"), ("cs_ownmk", b"\xFF"),
                                 ("cs_ownmk", b"\x00"))]
            m.pause()
            for p in pats:
                i = code.find(p)
                if i < 0:
                    sys.exit("skiescrash: cs_crackle does not take the three "
                             "stores the way this patch expects - re-read it "
                             "before trusting the red run")
                m.write(lin + lo + i, b"\x90" * 5)
            m.run()
            print("  (cs_crackle marking nothing again: check 2 must fail)")

        def shot():
            m.pause()
            w, h, data = m.fbuf(0)
            m.run()
            return w, h, data

        def rows(w, h, data):
            """(the view's first screen row, its height) - found off the
            PANEL's own full-width rule, because where the view sits on the
            glass is the backend's business and not a number to mirror."""
            lit = [sum(1 for x in range(w) if data[(y * w + x) * 3] > 128)
                   for y in range(h)]
            top = min(y for y in range(h) if lit[y] > w * 0.5)
            return top - word("cs_wh"), word("cs_wh")

        def count(w, h, data, y0, y1):
            return sum(1 for y in range(y0, y1) for x in range(w)
                       if data[(y * w + x) * 3] > 128)

        # --- 300 m over Paris, nose down: real sky over a real city --------
        m.pause()
        for n, v in (("cs_px", 150), ("cs_py", 300), ("cs_pz", -900)):
            poke(n, ((v * 256) & 0xFFFFFFFF).to_bytes(4, "little"))
        for n, v in (("cs_hdg", 30), ("cs_pitch", -5), ("cs_roll", 0)):
            poke(n, ((v * 65536 // 360) & 0xFFFF).to_bytes(2, "little"))
        poke("cs_state", b"\x01")
        poke("cs_pause", b"\x01")
        port = word("cs_airport") & 0xFFFF   # a poke is a TELEPORT (88.5.2)
        objs = int.from_bytes(m.read(lin + port + 18, 2), "little")
        nobj = int.from_bytes(m.read(lin + port + 20, 2), "little")
        for o in range(objs, objs + nobj * 20, 20):
            m.write(lin + o + 18, b"\x00\x00")
        m.run()
        guest_frames(6)
        w, h, before = shot()
        v0, vh = rows(w, h, before)
        sky = word("cs_hzy0")               # the horizon's own top row
        if not 4 <= sky <= vh - 4:
            sys.exit("skiescrash: the pinned pose has no sky over a horizon "
                     "(cs_hzy0 = %d of %d rows)" % (sky, vh))
        b_all = count(w, h, before, v0, v0 + vh)
        b_sky = count(w, h, before, v0, v0 + sky)

        # --- crash it where it stands. The crash state freezes the world -
        # cs_step does nothing but count the picture down - so nothing moves
        # while it runs, and cs_pause has to come off for it to run at all.
        m.pause()
        poke("cs_state", bytes([CS_ST_CRASH]))
        poke("cs_crasht", (400).to_bytes(2, "little"))   # hold the picture
        poke("cs_pause", b"\x00")
        m.run()

        # --- 3: EVERY ROW IT WROTE IS RECORDED (88.3.1), asked on the FIRST
        # crackle of the crash. On the second and every later one the crack
        # is already in the shadow wherever nothing refilled it, so drawing
        # it again changes only the rows something else marked - and a check
        # taken there reads 51 rows, 0 loose, on the broken build as well.
        m.bp_exec(lin + mp["cs_crackle"])
        m.run()
        if m.wait_stop(60) is None:
            sys.exit("skiescrash: cs_crackle never ran")
        r = m.regs()
        ret = int.from_bytes(m.read((r["ss"] << 4) + r["sp"], 2), "little")
        vh, shseg = word("cs_wh"), word("cs_shseg") & 0xFFFF
        spcur = word("cs_spcur") & 0xFFFF
        shadow0 = m.read(shseg << 4, 80 * vh)
        m.bp_exec(lin + ret)
        m.run()
        if m.wait_stop(60) is None:
            sys.exit("skiescrash: cs_crackle never returned")
        shadow1 = m.read(shseg << 4, 80 * vh)
        spans = m.read(lin + spcur, 2 * vh)
        m.bp_exec()
        m.run()
        drew, loose, first = 0, 0, None
        for y in range(vh):
            lo_b, hi_b = spans[2 * y], spans[2 * y + 1]
            ch = [b for b in range(80) if shadow0[80 * y + b] != shadow1[80 * y + b]]
            if not ch:
                continue
            drew += 1
            out = [b for b in ch if not lo_b <= b <= hi_b]
            if out:
                loose += 1
                if first is None:
                    first = (y, min(out), max(out), lo_b, hi_b)
        check(drew >= 20 and loose == 0,
              "and EVERY ROW IT WROTE IS RECORDED: it changed %d shadow rows "
              "and %d of them have bytes outside the row's own span%s (want "
              "20 rows or more, and none loose - an unmarked run neither "
              "reaches the glass nor can be erased off it)"
              % (drew, loose, "" if first is None else
                 ", first row %d bytes %d..%d against a span of %d..%d%s"
                 % (first[0], first[1], first[2], first[3], first[4],
                    " (EMPTY)" if first[3] > first[4] else "")))


        guest_frames(2)
        w2, h2, during = shot()
        d_all = count(w2, h2, during, v0, v0 + vh)
        d_sky = count(w2, h2, during, v0, v0 + sky)

        check(d_all - b_all >= 200,
              "the crash REACHES THE GLASS: %d lit pixels in the view against "
              "%d before it (want 200 more at least)" % (d_all, b_all))
        check(d_sky - b_sky >= 40,
              "...and it reaches THE SKY, above the horizon the guest puts at "
              "row %d: %d lit pixels there against %d before the crash (want "
              "40 more at least - the cracks up there were the half that was "
              "never drawn)" % (sky, d_sky, b_sky))

        # --- and let it end, so the row leaves a machine that is flying
        # rather than one frozen in a crash
        m.pause()
        poke("cs_crasht", (4).to_bytes(2, "little"))
        m.run()
        for _ in range(40):
            m.advance(frames=30)
            m.run()
            if byte("cs_state") != CS_ST_CRASH:
                break
        if byte("cs_state") == CS_ST_CRASH:
            sys.exit("skiescrash: the crash never ended (%d ticks left)"
                     % word("cs_crasht"))
        guest_frames(4)
        if a.shot:
            import os88marty
            w3, h3, after = shot()
            for nm, (ww, hh, d) in (("before", (w, h, before)),
                                    ("during", (w2, h2, during)),
                                    ("after", (w3, h3, after))):
                os88marty.write_png_rgb("%s-%s.png" % (a.shot, nm), ww, hh, d)

    print("skiescrash: %s" % ("ok" if not bad else "FAILED %d" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
