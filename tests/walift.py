#!/usr/bin/env python3
"""WireArena's lift carries whoever stands on it, however they got there
(docs/plans/completed/WIREARENA-HEIGHTS-PLAN.md 3.2, 7.4.4, 9.10).

The owner, on the pc5150 in the Heights arena: "The lift just 'jitters' me in
place when I stand on it, never lifting me up to the tower." The cause was in
`wa_hjump`'s cached floor read: on a still tick with the floor's box the lift,
it branched to `.read` with the LIFT'S RECORD in AX where `.read` takes x - so
it read the floor at x = 40, the ground, every other tick. The feet went
16, 0, 16, 0 (the jitter), `wa_ltick` saw a rider on the ground every other
tick and reset its count, and the lift never started. It worked only while
the rider's x or y changed every tick - a moving rider reads the floor
through the other branch, with AX right.

So every ride here ends with the player STILL on the lift - the case that
broke - and each checks, tick by tick off the game's own variables:

  - the lift reached its raised height (wa_ltop) and the feet with it;
  - the feet never went DOWN during the ride (the jitter was exactly that);
  - the rider walked off south onto the tower and stood there at its height;
  - with nobody on it, the lift came back to its rest height (wa_lrest).

The rides: backed on and every key released; walked on with the mouse
turning the view during the rise; W kept held into the tower's face (it
walks on by itself once the lift is within a step); arrived by a jump;
standing on its west edge (square half on the ground) and on its north
edge. Three more: a rider who walks off mid-rise falls to the ground and the
lift goes home; a rider who steps back onto it from the tower holds it up,
still; and one who drops onto it from the tower as it comes down stops it
and is carried back up (it never carries anyone DOWN: 3.2, it holds while
occupied).

`--frames DIR` also writes each tick's picture (30 a second) of the first
ride as PNGs, for a video of it.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
from os88fixture import need                                   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYS = "build/os8088-360.img"
APPS = "build/warena360.img"
F = 4772727                     # the 5150's clock: guest cycles a second
TICK = F // 30                  # the sim's tick (wa_tick runs at 30 Hz)
MENU, ITEM = "Link", "Automatic"   # (the menu bar's one menu: its first
                                   # item there says the game is up)


def start_keys(m, arena=2, width=40, cpu8088=True):
    """a match ALONE from the setup by its KEYS, no symbol map (the tools
    that boot a tree they do not assemble): Arena from its default, the
    heights (2), Right a step at a time in the list's order - wrapping
    past its last - then Down to Columns, Right once if the width
    is not the CPU's default (40 on an 8088), Enter"""
    import arenagen
    F = 4772727
    tags = [t[0] for t in arenagen.arenas()]   # (the list's order: arenagen's
    item = [tags.index(t) for t in arenagen.ARENA_ORDER].index  # ARENA_ORDER)
    for k in ["ArrowRight"] * ((item(arena) - item(2)) % len(tags)) + \
            ["ArrowDown"] + (["ArrowRight"] if (width == 40) != cpu8088
                             else []) + ["Enter"]:
        m.key(k)
        m.advance(cycles=F // 8)


def start(ui, w, sym, arena=2, w40=True, bots=None):
    """a match ALONE from the setup (NET-PLAN 2; the Game menu's Play items
    went, the owner 2026-11-14): the arena and the width poked into the
    setup's own settings, then Enter - the Play button's key"""
    m = ui.m
    os88marty.until(m, lambda _: any(c[0] == MENU and any(
        i[0] == ITEM for i in c[3]) for c in ui.menus()),
        "WireArena's Link menu on the bar", guest=10)
    p = os88marty.Pkg(m, w)
    p.write(sym["wa_lkarena"], bytes([arena]))
    p.write(sym["wa_want"], bytes([1 if w40 else 0]))
    if bots is not None:
        p.write(sym["wa_lkbots"], bytes([bots]))
    m.key("Enter")
    return p

# The arena's lift and tower (tools/arenagen.py HEIGHTS_BOXES): the lift is
# x 1920..2048, y 1152..1280, the tower's face it rises to is y = 1152.
LX0, LX1, LY0, LY1 = 1920, 2048, 1152, 1280
LSPD = 5                        # WA_LSPD: the feet follow the lift a tick
                                # late, by the step rule (7.4.4)
SOUTH = 768                     # wa_ang >> 6 facing -y (1024 to the turn)
NORTH = 256

VARS = ("wa_px", "wa_py", "wa_feet", "wa_jz", "wa_jv", "wa_flrb", "wa_lon",
        "wa_loff")


def bench_build():
    """THE BENCHES' BUILD: B and P are a test build's since NET-PLAN 9.9
    (`make WABENCH=1`), so a row or tool that presses them builds a private
    tree with them (or reuses it) and points this process at it - every
    `build/...` it then names, os88build.at's. -> the tree's directory; read
    the game's symbols with amap(bench=True)."""
    t = os88build.tree("WABENCH=1",
                       targets=("os8088-360.img", "warena360.img")).apply()
    return t.dir


def amap(bench=False):
    """The game's own symbol offsets: wirearena.asm assembled with a map, and
    refused unless it is byte-identical to the build's (a stale map reads the
    wrong words and reports a plausible wrong ride). bench: the build is a
    WABENCH=1 one (bench_build)."""
    bdir = os.path.join(ROOT, os88build.at("build/"))
    d = tempfile.mkdtemp()
    mp, bn = os.path.join(d, "m"), os.path.join(d, "b")
    subprocess.check_call(
        ["nasm", "-f", "bin"] + (["-DWA_BENCH"] if bench else []) +
        ["-I", os.path.join(ROOT, "apps/"),
         "-I", os.path.join(ROOT, "apps/wirearena/"),
         "-I", os.path.join(ROOT, "drivers/net/"), "-I", bdir + "/",
         "--before", "[map all %s]" % mp, "-o", bn,
         os.path.join(ROOT, "apps/wirearena/wirearena.asm")], cwd=ROOT)
    mine = open(bn, "rb").read()
    built = open(os.path.join(bdir, "wirearena.bin"), "rb").read()
    if mine != built:
        sys.exit("walift: build/wirearena.bin is not apps/wirearena/"
                 "wirearena.asm as it stands - rebuild (make wirearenadisk)")
    o = {}
    for line in open(mp):
        f = line.split()
        if (len(f) == 3 and re.match(r'^[0-9A-F]+$', f[0])
                and re.match(r'^[0-9A-F]+$', f[1])):
            o[f[2]] = int(f[0], 16)
    return o


class Game(object):
    def __init__(self, ui, w, sym, frames=None):
        self.m, self.sym, self.frames = ui.m, sym, frames
        self.p = os88marty.Pkg(ui.m, w)
        self.nframe = 0
        self.ticks = 0
        self.lift = self.word("wa_lift")
        self.lrest = self.word("wa_lrest")
        self.ltop = self.word("wa_ltop")

    def word(self, n, signed=True):
        v = self.p.word(self.sym[n])
        return v - 0x10000 if signed and v >= 0x8000 else v

    def put(self, n, v, off=0):
        self.p.write(self.sym[n] + off, struct.pack('<H', v & 0xFFFF))

    def state(self):
        spans = [(self.sym[n], 2) for n in VARS]
        spans.append((self.sym["wa_bxh"] + self.lift, 2))
        r = [struct.unpack('<h', b)[0] for b in self.p.reads(spans)]
        s = dict(zip(VARS, r))
        s["lh"] = r[-1]
        return s

    def tick(self, n=1):
        """`n` sim ticks of guest time; the state after them."""
        self.m.advance(cycles=TICK * n)
        self.ticks += n
        if self.frames:
            w, h, d = self.m.fbuf(0)
            os88marty.write_png_rgb(os.path.join(
                self.frames, "f%05d.png" % self.nframe), w, h, d)
            self.nframe += 1
        return self.state()

    def pose(self, x, y, ang=SOUTH):
        """The player on the ground at (x, y), every key up, the lift at rest
        and nobody's count running - each ride starts from the same place."""
        m = self.m
        m.pause()
        for n, v in (("wa_px", x), ("wa_py", y), ("wa_pxf", 0),
                     ("wa_pyf", 0), ("wa_ang", ang << 6), ("wa_pitch", 0),
                     ("wa_jz", 0), ("wa_jv", 0), ("wa_feet", 0),
                     ("wa_fpx", -1), ("wa_lon", 0), ("wa_loff", 0)):
            self.put(n, v)
        self.put("wa_bxh", self.lrest, self.lift)
        self.tick(2)

    def hold_until(self, keys, cond, limit=90):
        """Hold `keys` until cond(state) or `limit` ticks; release them.
        Answers the state, or None if it never came true."""
        for k in keys:
            self.m.key(k, up=False)
        s = None
        for _ in range(limit):
            s = self.tick()
            if cond(s):
                break
        else:
            s = None
        for k in keys:
            self.m.key(k, down=False)
        return s

    def on_lift(self, s):
        return (s["wa_flrb"] == self.lift and s["wa_jz"] == 0
                and s["wa_jv"] == 0)


def ride(g, fails, name, x, y, arrive, during=None, walk_off=True,
         ang=SOUTH):
    """Pose at (x, y), `arrive(g)` onto the lift, then stand still on it
    (calling during(g, tick) each tick) until it is up; walk off south."""
    g.pose(x, y, ang)
    if not arrive(g):
        fails.append("%s: never got onto the lift" % name)
        return
    trace = []
    up = None
    for t in range(150):            # 5 s: 1/3 s wait and ~1 s of rise
        if during:
            during(g, t)
        s = g.tick()
        trace.append((s["wa_feet"], s["lh"], s["wa_flrb"]))
        if s["lh"] == g.ltop and s["wa_feet"] == g.ltop and g.on_lift(s):
            up = t
            break
    feet = [f for f, _, _ in trace]
    drops = [i for i in range(1, len(feet)) if feet[i] < feet[i - 1]]
    if drops:
        i = drops[0]
        fails.append("%s: the feet went DOWN on the lift %d time(s), first "
                     "%d -> %d at tick %d (lift %d): the jitter"
                     % (name, len(drops), feet[i - 1], feet[i], i,
                        trace[i][1]))
    if up is None:
        s = trace[-1]
        fails.append("%s: after %d ticks standing on it the lift is at %d "
                     "(raised %d) and the feet at %d - it never carried them "
                     "up" % (name, len(trace), s[1], g.ltop, s[0]))
        return
    print("  %-22s up in %3d ticks (%.2f s), feet %d, no drop"
          % (name, up + 1, (up + 1) / 30.0, g.ltop), flush=True)
    if not walk_off:
        return
    s = g.hold_until(["KeyW" if ang == SOUTH else "KeyS"],
                     lambda s: s["wa_py"] < LY0 - 30, 40)
    if s is None:
        fails.append("%s: could not walk off the raised lift south onto the "
                     "tower" % name)
        return
    s = g.tick(3)
    if s["wa_feet"] != g.ltop or s["wa_jz"] or s["wa_jv"]:
        fails.append("%s: walked south off the lift but stands at %d "
                     "(jz %d), not on the tower at %d"
                     % (name, s["wa_feet"], s["wa_jz"], g.ltop))
        return
    for _ in range(90):             # 45 ticks of hold, then ~1 s down
        s = g.tick()
        if s["lh"] == g.lrest:
            break
    if s["lh"] != g.lrest or s["wa_feet"] != g.ltop:
        fails.append("%s: 3 s after the rider left, the lift is at %d (rest "
                     "%d) and the rider at %d (the tower %d)"
                     % (name, s["lh"], g.lrest, s["wa_feet"], g.ltop))


def walk_on(g, stop, key="KeyW"):
    """W (S backing) until the player's y is at or below `stop` - on the
    lift."""
    return g.hold_until([key], lambda s: s["wa_py"] <= stop
                        and g.on_lift(s), 60) is not None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", help="write the first ride's frames here")
    a = ap.parse_args()
    need(APPS)
    sym = amap()
    fails = []
    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WARENA.O88")
        # the window is up before its entry proc has set its menus
        p = start(ui, w, sym)
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0
                        and p.word(sym["wa_lift"]) < 0x8000,
                        "the Heights arena's first frames", guest=20)
        m.pause()
        # (the bots on their home loops and holding their fire, as in the
        # benches: this row is about the lift, and a bot's shot kills the
        # rider - docs/plans/completed/WIREARENA-AI-PLAN.md)
        p.write(sym["wa_bdet"], b"\x01")
        g = Game(ui, w, sym, a.frames)
        if g.ltop <= g.lrest or g.lift < 0:
            sys.exit("walift: no lift in this arena (lift %d, %d -> %d)"
                     % (g.lift, g.lrest, g.ltop))
        cx = (LX0 + LX1) // 2

        # 1. backed on from the north, every key released, looking north
        # over the arena (the video's ride: facing the tower, its face
        # fills the view and a rise shows nothing)
        ride(g, fails, "back on, keys up", cx, 1360,
             lambda g: walk_on(g, 1230, "KeyS"), ang=NORTH)
        g.frames = None
        # 2. walked on facing the tower, the mouse turning the view during
        # the rise
        ride(g, fails, "walk on, mouse turns", cx, 1360,
             lambda g: walk_on(g, 1230),
             during=lambda g, t: g.m.mouse(dx=(4 if t % 8 < 4 else -4)))
        # 3. W kept held: into the tower's face, then on by itself
        g.pose(cx, 1360)
        t0 = g.ticks
        g.m.key("KeyW", up=False)
        s = None
        for _ in range(240):
            s = g.tick()
            if s["wa_py"] < LY0 - 30 and s["wa_feet"] == g.ltop:
                break
        g.m.key("KeyW", down=False)
        if s["wa_py"] >= LY0 - 30 or s["wa_feet"] != g.ltop:
            fails.append("W held: after 8 s the player is at y %d, feet %d, "
                         "lift %d - not carried up onto the tower"
                         % (s["wa_py"], s["wa_feet"], s["lh"]))
        else:
            print("  %-22s on the tower in %d ticks" % ("W held",
                                                        g.ticks - t0),
                  flush=True)

        # 4. arrived by a jump from the north
        def jump_on(g):
            g.m.key("KeyW", up=False)
            g.tick(2)
            g.m.key("Space")
            s = g.hold_until([], lambda s: s["wa_py"] <= 1240, 40)
            g.m.key("KeyW", down=False)
            if s is None:
                return False
            return g.hold_until([], g.on_lift, 60) is not None
        ride(g, fails, "jumped on", cx, 1400, jump_on)
        # 5. on its west edge, the square half on the ground
        ride(g, fails, "west edge", LX0 - 12, 1360,
             lambda g: walk_on(g, 1230))
        # 6. on its north edge, the square half on the ground
        ride(g, fails, "north edge", cx, 1360,
             lambda g: walk_on(g, LY1 + 12))

        # 7. walked off west mid-rise: a fall to the ground, the lift home
        g.pose(cx, 1360)
        walk_on(g, 1230)
        s = None
        for _ in range(90):
            s = g.tick()
            if s["lh"] >= 80:
                break
        if s["lh"] < 80 or s["wa_feet"] < s["lh"] - LSPD:
            fails.append("walk off mid-rise: the lift at %d and the feet at "
                         "%d before the walk off - never mid-rise"
                         % (s["lh"], s["wa_feet"]))
        s = g.hold_until(["KeyD"], lambda s: s["wa_px"] < LX0 - 30, 40)
        if s is None:
            fails.append("walk off mid-rise: could not strafe off west")
        else:
            for _ in range(120):
                s = g.tick()
                if s["lh"] == g.lrest:
                    break
            if s["wa_feet"] != 0 or s["lh"] != g.lrest:
                fails.append("walk off mid-rise: feet %d, lift %d - wanted "
                             "the ground and the lift back at %d"
                             % (s["wa_feet"], s["lh"], g.lrest))
            else:
                print("  %-22s fell to 0, lift home" % "walk off mid-rise",
                      flush=True)

        # 8. back onto the raised lift from the tower: it holds, still
        g.pose(cx, LY0 - 40)            # on the tower: the feet step up
        g.m.pause()                     # to it (pose's two ticks), and the
        g.put("wa_bxh", g.ltop, g.lift)  # lift as a ride left it, raised
        g.put("wa_loff", 0)
        s = g.hold_until(["KeyS"], lambda s: s["wa_py"] >= 1230, 40)
        if s is None:
            fails.append("back onto the raised lift: could not walk back")
        else:
            bad = 0
            for _ in range(90):
                s = g.tick()
                if (s["lh"] != g.ltop or s["wa_feet"] != g.ltop
                        or not g.on_lift(s)):
                    bad += 1
            if bad:
                fails.append("back onto the raised lift: %d of 90 still ticks "
                             "off it or it not held up (last: lift %d, feet "
                             "%d)" % (bad, s["lh"], s["wa_feet"]))
            else:
                print("  %-22s held up 3 s, feet %d" % ("back on from tower",
                                                        g.ltop), flush=True)

        # 9. onto it from the tower as it comes DOWN: it stops under the
        # rider and, a third of a second later, carries them back up. It
        # never carries anyone down (3.2: it holds while occupied).
        g.pose(cx, LY0 - 40)
        g.m.pause()
        g.put("wa_bxh", 150, g.lift)    # on its way down, past its hold
        g.put("wa_loff", 60)
        s = g.hold_until(["KeyS"], lambda s: s["wa_py"] >= LY0 + 24, 40)
        s = s and g.hold_until([], g.on_lift, 40)   # (a drop: a fall)
        if s is None:
            fails.append("onto it coming down: never stood on it")
        elif s["lh"] <= g.lrest:
            fails.append("onto it coming down: it was home (%d) before the "
                         "rider was on it - nothing tested" % s["lh"])
        else:
            low, feet, up = s["lh"], [s["wa_feet"]], None
            for t in range(120):
                s = g.tick()
                feet.append(s["wa_feet"])
                if s["lh"] == g.ltop and s["wa_feet"] == g.ltop:
                    up = t
                    break
            drop = [i for i in range(1, len(feet)) if feet[i] < feet[i - 1]]
            if up is None or drop:
                fails.append("onto it coming down: landed at %d, then %d "
                             "drop(s) and %s" % (low, len(drop),
                                                 "up" if up is not None else
                                                 "never back up (lift %d)"
                                                 % s["lh"]))
            else:
                print("  %-22s landed on it at %d, back up in %d ticks"
                      % ("onto it coming down", low, up + 1), flush=True)

    if fails:
        print("walift: FAIL - %d of the lift's rides went wrong:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("walift: ok - every ride up, onto the tower, the lift home")


if __name__ == "__main__":
    main()
