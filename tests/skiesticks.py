#!/usr/bin/env python3
"""Does Clear Skies draw its world after 30 minutes of uptime? (SPEC.md 88.5.2.4)

    make && python3 tests/skiesticks.py

**IT EXISTS BECAUSE THE WORLD WENT MISSING IN FRONT OF AN AUDIENCE.** Reported
off the 5150, every time the machine was being shown off and never when it
was being tested: *"it fails to load any terrain/map items, but otherwise
continues to display and run. The screen shows a horizon, a cockpit and
instruments, but no runway, no buildings."* The reporter's two suspects were
the extended desktop and the hard disk, because both were always on by the
time Clear Skies was launched. Neither was it. What a demo always has and a
test never does is **half an hour of uptime**.

`cs_consider`'s first cull asked whether an object was still inside its skip
window with `CSO_SKIP - [cs_last]`, signed, `jg .out` - and `CSO_SKIP` = 0 is
what every world ships with, what `cs_skipclr` writes and what the runway's own
record holds from its zeroed bss. `[cs_last]` is `[ticks]`, which counts IRQ0
from boot and wraps at 65,536. So `0 - [cs_last]` is POSITIVE for every tick
count from 0x8001 to 0xFFFF: from 30 minutes of uptime to 60, from 90 to 120,
and so on, every object in the world AND the runway read as skipped, and the
view was the horizon and the panel and nothing else. A skip that EXPIRED had
the same defect later: an object that came back into range kept its old tick,
which comes round positive again 32,768 ticks after it passed.

**THREE LEGS, and each is a way the machine reaches the state:**

  A. the control - a fresh boot's tick count, which is what every other skies
     row flies at and why none of them could see this
  B. the demo - `[ticks]` put at 0x9000 before Fly, the reporter's session
  C. the crossing - flying ACROSS 0x8000, so the objects already filed with
     `CSO_SKIP` = 0 must survive the tick count changing under them

**THE ASSERTION IS `[cs_nvisn]`**, the count of objects the cull filed on the
last frame - the runway is one of them, so on the spawn at Paris it is never
zero on a working build. It is read rather than a screen compared: what is
drawn depends on the adapter and the settings, and the defect is that the
cull filed NOTHING, which is one word and has one answer.

VERIFIED TO FAIL against the package before the fix: A passes, and B and C
report `[cs_nvisn]` 0.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
from skies import open_game, Bss                            # noqa: E402

MACHINE = "os8088_5150_herc_gla"


def ticks(m):
    return int.from_bytes(m.read(os88sym.linear("ticks"), 2), "little")


def set_ticks(m, v):
    m.write(os88sym.linear("ticks"), (v & 0xFFFF).to_bytes(2, "little"))


def fly(m, r):
    """Into the bracket, confirmed by state (SPEC.md 88.10.5.5)."""
    r.poke("cs_back", b"\x00")
    r.poke("cs_quit", b"\x00")
    m.run()
    m.type_text("f")
    for _ in range(25):
        m.advance(frames=20)
        m.run()
        if r.byte("cs_back"):
            return True
    return False


def land(m, r):
    """...and back out to the launcher."""
    m.type_text("f")
    for _ in range(40):
        m.advance(frames=20)
        m.run()
        if r.byte("cs_quit"):
            break
    m.advance(frames=60)
    m.run()


def filed(m, r, frames=120):
    """[cs_nvisn] after the cull has had a few frames to run."""
    m.advance(frames=frames)
    m.run()
    return r.word("cs_nvisn")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default=MACHINE)
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--shots", help="a directory: the rendered frame of each "
                    "leg goes there as a PNG, to LOOK at what the count says")
    a = ap.parse_args()
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)

    bad = []
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        slot, seg, base = open_game(m)
        r = Bss(m, seg, base)
        if r.byte("cs_want") == 0:      # CSB_NONE - the Fly button is greyed
            sys.exit("skiesticks: this display offers no mode to fly in")

        def leg(name, start, cross=None):
            m.pause()
            r.poke("cs_inited", b"\x00")        # on the runway, every leg
            if start is not None:
                set_ticks(m, start)
            if not fly(m, r):
                bad.append(name)
                print("  FAIL %s: Fly did nothing" % name)
                return
            if cross is not None:               # wait for the tick count to
                for _ in range(200):            # pass the boundary in flight
                    m.advance(frames=20)
                    m.run()
                    if ticks(m) >= cross:
                        break
            n = filed(m, r)
            t = ticks(m)
            if a.shots:
                w, h, data = m.fbuf()
                png = os.path.join(a.shots, "%s.png" % name.split()[0])
                os88marty.write_png_rgb(png, w, h, data)
            if n == 0:
                bad.append(name)
                print("  FAIL %s: [ticks] %04X and the cull filed NOTHING - "
                      "no runway, no world (SPEC.md 88.5.2.4)" % (name, t))
            else:
                print("  ok   %s: [ticks] %04X, %d objects filed"
                      % (name, t, n))
            land(m, r)

        leg("A control", None)
        leg("B uptime", 0x9000)
        leg("C crossing", 0x7F00, cross=0x8040)

    if bad:
        print("skiesticks: FAILED - %s" % ", ".join(bad))
        return 1
    print("skiesticks: ok - the world is drawn at every tick count")
    return 0


if __name__ == "__main__":
    sys.exit(main())
