#!/usr/bin/env python3
"""THE THROTTLE IS A USER INPUT, SO IT NEVER WAITS (SPEC.md 88.9.1.1).

    python3 tests/skiesthr.py [--clobber-now]

The instruments read the aeroplane every `CS_PRATE` = 6 ticks (88.9.1),
which is right for a speed or an altitude - they move every tick in a climb
and nine opaque glyphs a change at the frame rate is a tenth of the frame.
It is wrong for the THROTTLE, which moves only while the pilot is holding a
key: a frame of lag there reads as the machine not listening.

`cs_pitem` exempted the state and the message from the gate by name and
nothing else, so the throttle and its bar waited with the instruments -
`cs_thr` moving on 16 frames and the panel redrawing on 8, the number on the
glass being the one before on every other frame. `cs_pnow` is the table that
says which items never wait, and the throttle and its bar are in it.

The row reads `cs_pkeys`, which is what is ON THE GLASS and not what the
aeroplane holds, at a `cs_blit` breakpoint - the one instruction at which a
frame is whole (docs/WRITING-TESTS.md 13 entry 44):

  1. with W held from idle to full, every frame `cs_thr` moves the panel
     shows the NEW value - not one frame's lag, not half the frames;
  2. and with the throttle steady at full the panel does not redraw at all,
     which is the whole of "it only costs when they are changing it".

--clobber-now is the red run (docs/WRITING-TESTS.md 1): it puts the two
throttle entries of `cs_pnow` back to 0, which is the tree as the field had
it, and check 1 goes red at about half the frames.
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
CS_PI_THR, CS_PI_BAR = 4, 6
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
    ap.add_argument("--clobber-now", action="store_true",
                    help="the throttle waits for the gate again: red")
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

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        m.advance(frames=30)
        m.run()
        if a.clobber_now:
            m.pause()
            for i in (CS_PI_THR, CS_PI_BAR):
                m.write(lin + mp["cs_pnow"] + i, b"\x00")
            m.run()
            print("  (the throttle waits for the gate again: this run must "
                  "fail)")

        m.type_text("f")
        m.advance(frames=150)
        m.run()
        check(w("cs_back") != 0, "the bracket took a mode")

        # cs_pkeys is WHAT IS ON THE GLASS, a word an item, one set a page
        pk = mp["cs_pkeys"]

        def drawn(i):
            return int.from_bytes(m.readseg(seg, pk + 2 * i, 2), "little")

        m.bp_exec(lin + mp["cs_blit"])
        m.run()
        if m.wait_stop(60) is None:
            sys.exit("skiesthr: cs_blit never ran")
        rows = []
        m.key("KeyW", down=True, up=False)       # full throttle, held
        for _ in range(30):
            m.run()
            if m.wait_stop(60) is None:
                sys.exit("skiesthr: cs_blit never ran")
            rows.append((w("cs_thr"), drawn(CS_PI_THR), drawn(CS_PI_BAR)))
        m.key("KeyW", down=False, up=True)
        m.bp_exec()
        m.run()

        print("      cs_thr   %s" % [r[0] for r in rows[:16]])
        print("      on glass %s" % [r[1] for r in rows[:16]])
        moved = [i for i in range(1, len(rows)) if rows[i][0] != rows[i - 1][0]]
        stale = [i for i in moved if rows[i][1] != rows[i][0]
                 or rows[i][2] != rows[i][0]]
        check(bool(moved),
              "the throttle actually ran up (%d frames of movement)"
              % len(moved))
        check(not stale,
              "...and EVERY frame it moved, the panel shows the new value "
              "(%s)" % ("stale on frames %s of %d" % (stale[:6], len(moved))
                        if stale else "%d of %d" % (len(moved), len(moved))))

        # ...and steady it costs nothing: no redraw at all once it is pinned
        held = [i for i in range(1, len(rows))
                if rows[i][0] == rows[i - 1][0] == 100]
        redrew = [i for i in held if rows[i][1] != rows[i - 1][1]
                  or rows[i][2] != rows[i - 1][2]]
        check(len(held) >= 5,
              "the throttle sat at full long enough to say (%d frames)"
              % len(held))
        check(not redrew,
              "...and a STEADY throttle redraws nothing (%s)"
              % (redrew[:6] if redrew else "0 of %d frames" % len(held)))

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
