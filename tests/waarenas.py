#!/usr/bin/env python3
"""WireArena's HEDGE and TWIN, and what the engine had to stop assuming to
carry them (docs/plans/completed/WIREARENA-ARENAS-PLAN.md 4, WIREARENA-HEDGES-PLAN.md).

The two arenas were the owner's stress test ("to see how well the system
holds up before we do ctf mode"), and each needed the engine to drop a limit
the first arenas had set rather than one anything required. This row checks
each of those on MartyPC, off the game's own variables:

  TWIN (arena 4)
  - BOTH LIFTS carry a rider still on them to the tower's top (240), and
    each goes home with nobody on it - the sim moved one lift (4.1); with
    the tables a second lift's rider stands on a lift that never moves;
  - a TALL box a lift climbs onto is a FLOOR (4.3): off the raised lift onto
    the tower, the feet stay at 240 - without kind bit 6 they fall to 0;
  - a FLOOR LINE stops the feet (4.5): walked at the west square's straight
    edge and at the path's sloping one, the player stops at the line;
  - the bots ride BOTH lifts (a lift leg's kind byte names its lift, 4.1):
    with the player out of the way, each lift leaves its rest at least once
    in 150 guest seconds;
  - the P bench starts at the arena's OWN first view (4.2): (640, 1024),
    marked A - it read the heights arena's table, and a wrong offset into
    the new one put a stray byte where the mark is. (Last: the bench makes
    the bots dummies.)
  HEDGE (arena 3; the maze it replaced was the first arena here)
  - the red stack's top is a floor at 160, and its MIDDLE tier one at 104 -
    a box STANDING on a box (HEDGES-PLAN 8): its floor is the tallest box
    under the feet, so a tier read as anything else is red;
  - its lift, in the outer wall's north gap, carries a rider to the wall's
    top (128) and off onto it;
  - the P bench's first view is the court's, (768, 832).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
from os88fixture import need                                   # noqa: E402
from walift import start, amap, bench_build, SYS, APPS         # noqa: E402

F = 4772727
TICK = F // 30


class G(object):
    def __init__(self, ui, w, sym):
        self.m, self.sym = ui.m, sym
        self.p = os88marty.Pkg(ui.m, w)

    def word(self, n, off=0):
        v = self.p.word(self.sym[n] + off)
        return v - 0x10000 if v >= 0x8000 else v

    def put(self, n, v, off=0):
        self.p.write(self.sym[n] + off, struct.pack('<H', v & 0xFFFF))

    def lift(self, i):
        return self.word("wa_bxh", self.word("wa_lift", 2 * i))

    def place(self, x, y, feet, ang=256):
        for n, v in (("wa_px", x), ("wa_py", y), ("wa_pxf", 0), ("wa_pyf", 0),
                     ("wa_ang", ang << 6), ("wa_pitch", 0), ("wa_jz", 0),
                     ("wa_jv", 0), ("wa_feet", feet), ("wa_fpx", -1)):
            self.put(n, v)

    def ride(self, i, x, y, dx, dy, fails, name):
        """stand still on lift i at (x, y) until it is up, then step (dx,
        dy) onto what it climbs to: the feet there"""
        top = self.word("wa_ltop", 2 * i)
        self.place(x, y, self.word("wa_lrest", 2 * i))
        for t in range(150):
            self.m.advance(cycles=TICK)
            if self.lift(i) == top and self.word("wa_feet") == top:
                break
        else:
            fails.append("%s: lift %d at %d, feet %d - never up to %d"
                         % (name, i, self.lift(i), self.word("wa_feet"), top))
            return
        self.put("wa_px", x + dx)
        self.put("wa_py", y + dy)
        self.put("wa_fpx", -1)
        self.m.advance(cycles=TICK * 15)
        f = self.word("wa_feet")
        if f != top:
            fails.append("%s: stepped off the raised lift, the feet at %d, "
                         "not %d - not a floor%s" % (name, f, top,
                                                    " (DEAD)" if self.p.byte(
                                                        self.sym["wa_pdead"])
                                                    else ""))
            return
        print("  %-26s up in %d ticks, off at %d" % (name, t + 1, f),
              flush=True)

    def home(self, i, fails, name):
        self.place(192, 768, 0)                 # (the west square's corner)
        rest = self.word("wa_lrest", 2 * i)
        for t in range(200):
            self.m.advance(cycles=TICK)
            if self.lift(i) == rest:
                return
        fails.append("%s: lift %d at %d with nobody on it, not home (%d)"
                     % (name, i, self.lift(i), rest))

    def bench(self, want, fails, name):
        self.m.key("KeyP")
        self.m.advance(cycles=TICK * 3)
        got = (self.word("wa_px"), self.word("wa_py"), self.word("wa_pbm")
               & 0xFF)
        if got != want:
            fails.append("%s: P's first view %r, not %r" % (name, got, want))
        else:
            print("  %-26s %r" % (name, got), flush=True)


def boot():
    return os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga", quick=True)


def match(ui, arena, bots=None, mode=None):
    sym = amap(bench=True)
    w = ui.path("B:/WARENA.O88")
    if mode is not None:                # (the setup's Mode, poked: CTF on
        os88marty.Pkg(ui.m, w).write(sym["wa_lkgmode"], bytes([mode]))
    p = start(ui, w, sym, arena=arena, bots=bots)
    f0 = p.byte(sym["wa_frame"])
    os88marty.until(ui.m, lambda _: p.byte(sym["wa_frame"]) != f0,
                    "the match's first frames", guest=20)
    return G(ui, w, sym)


def main():
    bench_build()                       # (P, the play bench: a test build's -
    need(APPS)                          # its own tree, NET-PLAN 9.9)
    fails = []
    with boot() as ui:
        g = match(ui, 4, bots=3)
        g.p.write(g.sym["wa_bdet"], b"\x01")    # (the rides: dummies, who
                                                # neither ride nor shoot)
        if g.word("wa_nlift") != 2:
            fails.append("twin: %d lifts, not 2" % g.word("wa_nlift"))
        g.ride(0, 384, 1024, 128, 0, fails, "twin: west lift, tower")
        g.home(0, fails, "twin: west lift")
        g.ride(1, 1664, 1024, -128, 0, fails, "twin: east lift, tower")
        g.home(1, fails, "twin: east lift")
        for (x, y, a, lo, hi, axis, what) in (
                (192, 768, 768, 700, 720, 1, "the west square's south edge "
                 "(y 704)"),
                (704, 1024, 0, 770, 800, 0, "the path's sloping north edge "
                 "(x 789 at y 1024)")):
            g.place(x, y, 0, a)                 # (FLOOR LINES, 4.5: walked
            g.m.advance(cycles=TICK * 4)        # at, the player stops at the
            g.m.key("KeyW", up=False)           # line)
            g.m.advance(cycles=F * 2)
            g.m.key("KeyW", down=False)
            vs = []                             # (the most common of five
            for _ in range(5):                  # reads: one can land inside
                vs.append(g.word("wa_py" if axis else "wa_px"))  # a tick,
                g.m.advance(cycles=TICK // 3)   # its step tried and not yet
            v = max(set(vs), key=vs.count)      # put back)
            if not lo <= v <= hi:
                fails.append("twin: walked at %s, now at %d - the line did "
                             "not stop the feet" % (what, v))
            else:
                print("  %-26s stopped at %d" % ("twin: a floor line", v),
                      flush=True)
        g.place(192, 768, 0)
        g.p.write(g.sym["wa_bdet"], b"\x00")    # (the bots: their own minds)
        rose = [False, False]
        for s in range(150):
            g.m.advance(cycles=F)
            for i in (0, 1):
                rose[i] = rose[i] or g.lift(i) > g.word("wa_lrest", 2 * i)
            if all(rose):
                break
        if not all(rose):
            fails.append("twin: in 150 s the bots rode %s"
                         % ("the west lift alone" if rose[0] else
                            "the east lift alone" if rose[1] else
                            "neither lift"))
        else:
            print("  %-26s both, within %d s" % ("twin: the bots' rides",
                                                  s + 1), flush=True)
        g.bench((640, 1024, ord("A")), fails, "twin: bench view A")
    with boot() as ui:
        g = match(ui, 3)
        g.p.write(g.sym["wa_bdet"], b"\x01")
        for (x, y, want, what) in ((1024, 1152, 160, "the stack's top"),
                                   (920, 1150, 104, "its middle tier")):
            g.place(x, y, want)
            g.m.advance(cycles=TICK * 10)
            f = g.word("wa_feet")
            if f != want:
                fails.append("hedge: on %s, the feet at %d, not %d"
                             % (what, f, want))
            else:
                print("  %-26s feet %d" % ("hedge: " + what, f), flush=True)
        g.ride(0, 1024, 2016, 112, 0, fails, "hedge: the lift, the wall")
        g.bench((768, 832, ord("A")), fails, "hedge: bench view A")
    with boot() as ui:                  # CROSSING (THREE-ARENAS-PLAN 1)
        g = match(ui, 5)
        if g.p.byte(g.sym["wa_aflat"]) != 1:
            fails.append("crossing: not a flat arena (wa_aflat %d)"
                         % g.p.byte(g.sym["wa_aflat"]))
        else:
            print("  %-26s flat" % "crossing:", flush=True)
        g.bench((1024, 256, ord("A")), fails, "crossing: bench view A")
    with boot() as ui:                  # THE ZIGGURAT (THREE-ARENAS-PLAN 3)
        g = match(ui, 6)
        g.p.write(g.sym["wa_bdet"], b"\x01")
        g.place(1024, 448, 0, ang=256)  # (the south ramp's foot, north)
        g.m.advance(cycles=TICK * 4)
        g.m.key("KeyW", up=False)       # (up the ramp: 6 m at a walk's
        for t in range(150):            # speed)
            g.m.advance(cycles=TICK * 2)
            if g.word("wa_feet") == 288:
                break
        g.m.key("KeyW", down=False)
        if g.word("wa_feet") != 288:
            fails.append("ziggurat: up the south ramp, the feet at %d, not "
                         "the tower's top (288)" % g.word("wa_feet"))
        else:
            print("  %-26s up in %d ticks" % ("ziggurat: the south ramp",
                                              2 * (t + 1)), flush=True)
        g.bench((1024, 256, ord("A")), fails, "ziggurat: bench view A")
    with boot() as ui:                  # THE PIT (THREE-ARENAS-PLAN 2, 5)
        g = match(ui, 7, mode=1)        # (CTF: its flags on the ring)
        z = [g.word("wa_cfl", 10 * k + 6) for k in (0, 1)]
        if z != [240, 240] or g.p.byte(g.sym["wa_gmode"]) != 1:
            fails.append("pit: CTF's flags at their bases %r high (the "
                         "ring's 240), mode %d" % (z, g.p.byte(
                             g.sym["wa_gmode"])))
        else:
            print("  %-26s 240 up" % "pit: CTF's flags, the ring",
                  flush=True)
        g.p.write(g.sym["wa_bdet"], b"\x01")
        g.m.advance(cycles=TICK * 4)
        f = g.word("wa_feet")           # (the start: on the west layer)
        if f != 192:
            fails.append("pit: the start's feet at %d, not the layer's 192"
                         % f)
        else:
            print("  %-26s feet %d" % ("pit: the start, the layer", f),
                  flush=True)
        g.place(512, 1024, 0)           # (down in the pit)
        g.m.advance(cycles=TICK * 4)
        g.ride(0, 416, 1024, -112, 0, fails, "pit: the west lift out")
        g.place(192, 1024, 192, ang=512)    # A HOP onto the ring, west
        g.m.advance(cycles=TICK * 4)
        g.m.key("KeyW", up=False)
        g.m.advance(cycles=TICK * 2)
        g.m.key("Space", up=False)      # (held: a frame's poll sees it)
        g.m.advance(cycles=TICK * 5)
        g.m.key("Space", down=False)
        for t in range(40):
            g.m.advance(cycles=TICK)
            if g.word("wa_feet") == 240 and g.word("wa_jz") == 0:
                break
        g.m.key("KeyW", down=False)
        if g.word("wa_feet") != 240:
            fails.append("pit: a hop west off the layer, the feet at %d, not "
                         "the ring's 240" % g.word("wa_feet"))
        else:
            print("  %-26s feet 240" % "pit: a hop onto the ring",
                  flush=True)
        g.bench((256, 1024, ord("A")), fails, "pit: bench view A")
    with boot() as ui:                  # ...ITS BOTS: the lifts out of the
        g = match(ui, 7)                # pit, the hop to the ring
        g.place(256, 1024, 192)
        g.p.write(g.sym["wa_bdet"], b"\x00")
        rode, ring = False, False
        for s_ in range(240):
            g.put("wa_hp", 100)
            g.m.advance(cycles=F // 2)
            rode = rode or any(g.lift(i) > g.word("wa_lrest", 2 * i)
                               for i in (0, 1))
            ring = ring or any(g.word("wa_figs", 56 * i + 18) == 240
                               for i in range(3))
            if rode and ring:
                break
        if not (rode and ring):
            fails.append("pit: in 120 s the bots %s"
                         % ("rode no lift" if not rode else
                            "hopped onto no ring"))
        else:
            print("  %-26s within %d s" % ("pit: the bots ride, hop",
                                            (s_ + 1) // 2), flush=True)
    if fails:
        print("waarenas: FAIL - %d:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("waarenas: ok - both lifts, the towers, the floor lines, the bots' rides, "
          "the stack's tiers, the hedge's lift, the crossing flat, the "
          "ziggurat's ramp, the pit's lift, hop and bots, each arena's bench")


if __name__ == "__main__":
    main()
