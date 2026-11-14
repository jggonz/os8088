#!/usr/bin/env python3
"""WireArena's weapons and pickups (docs/plans/WIREARENA-WEAPONS-PLAN.md).

One boot into the Heights arena at 40 columns, then each piece checked off
the game's own variables (its symbols from wirearena.asm assembled with a
map, refused unless byte-identical to the build). The two WEAPON SPAWNS pick
their weapon at random, so the row writes the pick it wants into the game's
kind byte before it walks onto one - and checks the dice separately: every
choice of each spawn comes up, nothing else does, and the renderer's copy
of the kind follows the game's.

  - the start: the pistol, 6 loaded, 100 health, four pickups;
  - the pistol: three shots kill, by the fall; R reloads at any time;
  - the shotgun: taken, held, 3 and 3; one close shot kills, red as long as
    it lies, by COLLAPSE; and at MEDIUM range nothing of the collapse is
    drawn above the figure (the narrow path's unsigned multiply once drew
    two lines into the sky: WEAPONS-PLAN 3.5); R tops a part-used magazine
    up from the reserve;
  - the rocket launcher (key 5): a rocket flies, kills the shielded dummy 0
    outright into bits, and it is gone after; a rocket into a wall close by
    hurts the player;
  - the chaingun (key 3): fires while the trigger is held, kills by the
    fall;
  - the grenade launcher (key 4): a grenade off a wall comes back, bounces
    off the floor and goes off on its fuse; one that meets a dummy kills it
    outright, into bits;
  - the railgun (key 6): one shot kills two dummies standing in line - it
    pierces - and its beam shows for a few ticks;
  - the health and shield packs; a taken pickup comes back after its time;
  - F1-F5 set the detail level, RAMP2 on Very Low and Low alone; Y is no
    longer its key.

Each is a guest-memory assertion. `--shots DIR` writes a picture at each
step.
"""
import argparse
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
from os88fixture import need                                   # noqa: E402
from walift import Game, start, amap, SYS, APPS, ITEM  # noqa: E402

FS = 56                         # F_SIZE (apps/wirearena/waconst.inc)


def anims():
    """The animations' first keyframes, as tools/arenagen.py made them for
    this build (build/watab.inc's WA_A_*)"""
    import re
    import os88build
    t = open(os.path.join(os.path.dirname(__file__), "..",
                          os88build.at("build/"), "watab.inc")).read()
    return {k: int(v) for k, v in re.findall(r"WA_A_(\w+) equ (\d+)", t)}


class Arena(Game):
    def byte(self, n, off=0):
        return self.p.read(self.sym[n] + off, 1)[0]

    def fig(self, i):
        d = self.p.read(self.sym["wa_figs"] + i * FS, FS)
        w = lambda o: struct.unpack_from('<h', d, o)[0]
        return dict(x=w(0), y=w(2), sh=d[10], anim=d[11], nfr=d[12],
                    flash=d[16], dead=d[17], z=w(18), head=w(20) & 1023,
                    hp=d[42], kill=d[43])

    def arms(self):
        return dict(wpn=self.byte("wa_wpn"), own=self.byte("wa_own"),
                    mag=list(self.p.read(self.sym["wa_mag"], 6)),
                    res=list(self.p.read(self.sym["wa_res"], 6)),
                    rld=self.byte("wa_rld"), hp=self.word("wa_hp"),
                    sh=self.word("wa_sh"), pkon=self.byte("wa_pkon"),
                    level=self.byte("wa_level"), beam=self.byte("wa_beam"),
                    look=self.byte("wa_look"), mtr=self.byte("wa_mtr"))

    def kind(self, i):
        """Pickup i's kind: the game's, and the renderer's copy"""
        g = self.byte("wa_pks", i * PKSZ + 6)
        rds = self.word("wa_rds", signed=False)
        off = self.sym["wa_pks"] - self.sym["wa_fb_cfg"] + WR_FB
        return g, self.m.readseg(rds, off + i * PKSZ + 6, 1)[0]

    def respawn(self, i, kind=None):
        """Pickup i back now (its timer at 1: the next tick rolls it), and
        then the kind wanted written over the roll"""
        self.put("wa_pkt", 1, 2 * i)
        for _ in range(12):         # (a tick runs at the next frame's start:
            self.tick(1)            # until the timer has run out and rolled)
            if struct.unpack('<H', self.p.read(
                    self.sym["wa_pkt"] + 2 * i, 2))[0] == 0:
                break
        self.tick(1)
        if kind is not None:
            self.p.write(self.sym["wa_pks"] + i * PKSZ + 6, bytes([kind]))

    def projs(self):
        """Each projectile slot: x, y, z, state, type, and its x, z speed"""
        out = []
        for k in range(4):
            x, y, z, st = struct.unpack('<3hH', self.p.read(
                self.sym["wa_rks"] + 8 * k, 8))
            vx, _, vz = struct.unpack('<3h', self.p.read(
                self.sym["wa_rkg"] + RKGSZ * k + 6, 6))
            out.append(dict(x=x, y=y, z=z, st=st & 255, typ=st >> 8,
                            vx=vx, vz=vz))
        return out

    def at(self, x, y, feet, ang=None):
        """The player standing at (x, y) on `feet`, every key up."""
        self.m.pause()
        for n, v in (("wa_px", x), ("wa_py", y), ("wa_pxf", 0), ("wa_pyf", 0),
                     ("wa_pitch", 0), ("wa_jz", 0), ("wa_jv", 0),
                     ("wa_feet", feet), ("wa_fpx", -1)):
            self.put(n, v)
        if ang is not None:
            self.put("wa_ang", ang << 6)
        self.tick(2)

    def ahead(self, i, d):
        """d units ahead of dummy i on its patrol, facing it: it walks into
        the crosshair, so no shot depends on the frame it lands in."""
        f = self.fig(i)
        a = f["head"] * 2 * math.pi / 1024
        self.at(int(f["x"] + d * math.cos(a)), int(f["y"] + d * math.sin(a)),
                f["z"], ang=(f["head"] + 512) & 1023)

    def face(self, i, d):
        """Dummy i stood still at (1100, 300) in the open south floor, the
        player d units west of it facing it: a shot that misses is the
        weapon's fault, not a corner the dummy turned"""
        self.freeze(i, 1100, 300)
        self.at(1100 - d, 300, 0, ang=0)

    def alive(self, *figs):
        for _ in range(150):
            if not any(self.fig(i)["dead"] for i in figs):
                return
            self.tick(2)

    def freeze(self, i, x, y):
        """Dummy i put at (x, y), flinching - it stands for ~12 ticks"""
        base = self.sym["wa_figs"] + i * FS
        self.p.write(base, struct.pack('<hh', x, y))
        self.p.write(base + 18, struct.pack('<h', 0))
        self.p.write(base + 11, bytes([A_["FLINCH"], 3, 0]))
        self.p.write(base + 14, struct.pack('<h', 0))

    def press(self, key, ticks=4):
        """A key held long enough for a frame to poll it at any frame rate
        here (the trigger and the keys are read once a frame)."""
        self.m.key(key, up=False)
        self.tick(ticks)
        self.m.key(key, down=False)
        self.tick(1)


PKSZ = 8                        # WA_PKSZ (wafb.inc)
RKGSZ = 16                      # WA_RKGSZ (wirearena.asm)
WR_FB = 32                      # the frame block's offset in the part
A_ = {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", help="write a picture at each step here")
    a = ap.parse_args()
    need(APPS)
    sym = amap()
    A = anims()
    A_.update(A)
    fails = []

    def check(ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
        if not ok:
            fails.append(what)

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WARENA.O88")
        p = start(ui, w, sym)
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                        "the Heights arena's first frames", guest=20)
        m.pause()
        g = Arena(ui, w, sym)

        def shot(name):
            if a.shots:
                os.makedirs(a.shots, exist_ok=True)
                wd, ht, d = m.fbuf(0)
                os88marty.write_png_rgb(os.path.join(a.shots, name + ".png"),
                                        wd, ht, d)

        g.tick(4)
        # (the bots on their home loops, as the benches walk them - this row
        # is about the weapons, tests/waroutes.py about where bots go - and
        # each back at its loop's start: dead a tick, then respawned)
        g.p.write(sym["wa_bdet"], bytes([1]))
        for i in range(3):
            g.p.write(sym["wa_figs"] + i * FS + 17, bytes([1]))
        g.tick(4)
        s = g.arms()
        check(s["wpn"] == 0 and s["mag"][0] == 6 and s["hp"] == 100
              and s["sh"] == 0 and s["pkon"] == 15,
              "the start: the pistol, 6 loaded, 100 health, four pickups "
              "(%r)" % s)
        shot("00-start")

        # the dice: every choice of each spawn, nothing else, both copies
        seen2, seen3, agree = set(), set(), True
        for _ in range(30):
            g.respawn(2)
            g.respawn(3)
            k2, k3 = g.kind(2), g.kind(3)
            seen2.add(k2[0])
            seen3.add(k3[0])
            agree = agree and k2[0] == k2[1] and k3[0] == k3[1]
        check(seen2 == {0, 1, 2} and seen3 == {3, 4} and agree,
              "the weapon spawns' dice: the platform %r (shotgun, chaingun, "
              "grenades), the tower %r (rockets, railgun), the renderer's "
              "copy %s" % (sorted(seen2), sorted(seen3),
                           "the same" if agree else "DIFFERENT"))

        n = 0
        while n < 4 and not g.fig(1)["dead"]:
            g.face(1, 300)
            g.press("ControlLeft")
            n += 1
        f = g.fig(1)
        check(f["dead"] and n == 3 and f["kill"] == 0
              and f["anim"] == A["DEATH"],
              "the pistol: three shots kill, by the fall (%d shots, %r)"
              % (n, f))
        check(g.arms()["mag"][0] == 3, "the pistol: three spent of six")
        g.press("KeyR")
        r = g.arms()["rld"]
        g.tick(16)
        check(r > 0 and g.arms()["mag"][0] == 6,
              "R: the pistol reloads with three still in it (to 6)")
        shot("01-pistol-kill")

        g.respawn(2, 0)
        g.at(1248, 1000, 80, ang=256)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["own"] & 2 and s["wpn"] == 1 and s["mag"][1] == 3
              and s["res"][1] == 3 and not s["pkon"] & 4,
              "the shotgun on the ramp platform: taken, held, 3 and 3, gone "
              "(%r)" % s)
        g.press("Digit1")
        g.press("Digit2")
        check(g.arms()["wpn"] == 1, "keys 1 then 2: the shotgun again")
        shot("02-shotgun")

        g.tick(10)
        g.alive(2)
        g.face(2, 110)
        g.press("ControlLeft")
        f = g.fig(2)
        check(f["dead"] and f["kill"] == 1 and f["anim"] == A["COLLAPSE"]
              and f["flash"] > 0 and abs(f["flash"] + 150 - f["dead"]) <= 1,
              # (its body lies 90 ticks of the 240 to its respawn: red for
              # all 90 - AI-PLAN 8.5)
              "the shotgun: one close shot kills, red as long as it lies "
              "(%r)" % f)
        g.tick(6)
        shot("03-shotgun-kill")

        # ...and at MEDIUM range, where the joints take the renderer's
        # narrow path (WA_FJN): its height multiply is unsigned, and a
        # COLLAPSE that sank two joints below the floor drew them far above
        # it - two red lines into the sky while it fell (WEAPONS-PLAN 3.5).
        # Dummy 1, softened by the pistol, then one shotgun shot at 260: no
        # red cell in the view's top third while it collapses.
        g.alive(1)
        g.press("Digit1")
        for _ in range(2):
            g.face(1, 300)
            g.press("ControlLeft")
            g.tick(4)
        g.press("Digit2")
        g.tick(12)
        g.face(1, 260)
        g.press("ControlLeft", ticks=2)
        f = g.fig(1)
        sky = 0
        for _ in range(26):
            d = m.read(0xB8000, 40 * 8 * 2)
            sky += sum(1 for c in range(40 * 8) if d[c * 2 + 1] == 0x0C)
            g.tick(1)
        check(f["dead"] and f["kill"] == 1 and sky == 0,
              "the shotgun at medium range: a collapse, and nothing of it "
              "above the view's top third (%d red cells there; %r)"
              % (sky, f))
        shot("03b-shotgun-medium")
        s0 = g.arms()
        g.press("KeyR")
        g.tick(40)
        s = g.arms()
        check(s0["mag"][1] == 1 and s0["res"][1] == 3 and s["mag"][1] == 3
              and s["res"][1] == 1,
              "R tops the shotgun up: 1 and 3 -> 3 and 1 (%r -> %r)"
              % ((s0["mag"][1], s0["res"][1]), (s["mag"][1], s["res"][1])))

        g.respawn(3, 3)
        g.at(1952, 1000, 160, ang=512)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["own"] & 16 and s["wpn"] == 4 and s["mag"][4] == 3
              and not s["pkon"] & 8,
              "the rocket launcher on the tower: taken and held (%r)" % s)
        g.press("Digit1")
        g.press("Digit5")
        check(g.arms()["wpn"] == 4, "keys 1 then 5: the launcher again")

        g.alive(0)
        f = g.fig(0)
        check(f["sh"] == 100, "dummy 0 lives with a shield (%r)" % f)
        g.tick(30)
        g.face(0, 420)
        shot("04-shielded")
        g.press("ControlLeft")
        flew = 0
        for _ in range(40):
            if any(r["st"] == 1 and r["typ"] == 0 for r in g.projs()):
                flew += 1
            if g.fig(0)["dead"]:
                break
            g.freeze(0, 1100, 300)      # (held there for the flight)
            g.tick(1)
        shot("05-explosion")
        f = g.fig(0)
        check(f["dead"] and f["kill"] == 2 and f["anim"] == A["GIB"]
              and flew >= 2,
              "the rocket: it flies (%d ticks seen in flight) and a direct "
              "hit kills through the shield, into bits (%r)" % (flew, f))
        g.tick(6)
        shot("06-bits")
        g.tick(30)
        check(g.fig(0)["nfr"] == 0, "the rocket's kill: gone once its bits "
              "have flown")

        hp0 = g.arms()["hp"]
        g.tick(80)                      # (the launcher reloads: 2.5 s)
        g.at(1790, 1024, 0, ang=0)      # the tower's face 66 units ahead
        g.press("ControlLeft")
        g.tick(10)
        hp1 = g.arms()["hp"]
        check(hp1 < hp0, "a rocket into a wall close by: its splash hurts "
              "the player (%d -> %d)" % (hp0, hp1))

        # THE CHAINGUN: held, it keeps firing; nine rounds kill
        g.respawn(2, 1)
        g.at(1248, 1000, 80, ang=256)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["own"] & 4 and s["wpn"] == 2 and s["mag"][2] == 30
              and s["res"][2] == 30,
              "the chaingun: taken, held, 30 and 30 (%r)" % s)
        g.alive(1)
        g.face(1, 250)
        m.key("ControlLeft", up=False)
        for _ in range(8):
            g.tick(6)
            if g.fig(1)["dead"]:
                break
            g.face(1, 250)
        m.key("ControlLeft", down=False)
        g.tick(1)
        f = g.fig(1)
        spent = 30 - g.arms()["mag"][2]
        check(f["dead"] and f["kill"] == 0 and spent >= 9,
              "the chaingun: held, %d rounds, a kill by the fall (%r)"
              % (spent, f))
        shot("07-chaingun")

        # THE GRENADE LAUNCHER: off the tower's face, back off the floor,
        # off on its fuse; and into a dummy, a kill
        g.respawn(2, 2)
        g.at(1248, 1000, 80, ang=256)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["own"] & 8 and s["wpn"] == 3 and s["mag"][3] == 4
              and s["res"][3] == 4,
              "the grenade launcher: taken, held, 4 and 4 (%r)" % s)
        g.tick(16)
        g.at(1700, 1024, 0, ang=0)
        g.press("ControlLeft", ticks=2)
        vxs, vzs, boom = [], [], False
        for _ in range(70):
            gr = [r for r in g.projs() if r["typ"] == 1 and r["st"]]
            if gr and gr[0]["st"] == 1:
                vxs.append(gr[0]["vx"])
                vzs.append(gr[0]["vz"])
            elif gr:
                boom = True
                break
            g.tick(1)
        back = any(v > 0 for v in vxs) and any(v < 0 for v in vxs)
        bounce = any(a_ < 0 < b_ for a_, b_ in zip(vzs, vzs[1:]))
        check(back and bounce and boom and len(vxs) >= 40,
              "a grenade: back off the wall (%s), up off the floor (%s), "
              "off on its fuse after %d ticks (%s)"
              % (back, bounce, len(vxs), boom))
        shot("08-grenade")
        g.tick(16)
        g.alive(2)
        g.face(2, 110)
        g.press("ControlLeft", ticks=2)
        for _ in range(30):
            if g.fig(2)["dead"]:
                break
            g.tick(1)
        f = g.fig(2)
        check(f["dead"] and f["kill"] == 2,
              "a grenade into a dummy: a kill, into bits (%r)" % f)

        # THE RAILGUN: two dummies in line, one shot, both dead; its beam
        g.respawn(3, 4)
        g.at(1952, 1000, 160, ang=512)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["own"] & 32 and s["wpn"] == 5 and s["mag"][5] == 1
              and s["res"][5] == 4,
              "the railgun on the tower: taken, held, 1 and 4 (%r)" % s)
        g.tick(10)
        g.alive(1, 2)
        g.at(900, 300, 0, ang=0)
        g.freeze(1, 1100, 300)
        g.freeze(2, 1300, 300)
        g.tick(2)
        m.key("ControlLeft", up=False)
        beam = cells = 0
        for _ in range(8):
            g.tick(1)
            b_ = g.arms()["beam"]
            beam = max(beam, b_)
            if b_:
                d = m.read(0xB8000, 40 * 25 * 2)
                cells = max(cells, sum(1 for r in range(13, 19)
                                       for c in range(40)
                                       if d[(r * 40 + c) * 2] == 0x5C))
        m.key("ControlLeft", down=False)
        f1, f2 = g.fig(1), g.fig(2)
        check(f1["dead"] and f2["dead"],
              "the railgun pierces: one shot, both dummies in line dead "
              "(%r, %r)" % (f1, f2))
        check(beam > 0 and cells >= 4, "the railgun's beam: %d cells "
              "showing, %d ticks at most" % (cells, beam))
        shot("09-railgun")
        g.tick(8)
        check(g.arms()["beam"] == 0, "the beam gone after its ticks")

        g.at(64, 1050, 0, ang=768)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["hp"] == 100 and not s["pkon"] & 1,
              "the health pack in the back tunnel: health 100 (%r)" % s)
        g.at(480, 1050, 48, ang=768)
        g.tick(6)                   # (taken at a frame's first tick)
        s = g.arms()
        check(s["sh"] == 100 and not s["pkon"] & 2,
              "the shield pack on the jump platform: shield 100 (%r)" % s)
        shot("10-shield")

        g.tick(600)
        check(g.arms()["pkon"] & 1, "the health pack back after its 20 s")

        ramp2, wide = {}, {}
        for k, lv in (("F1", 0), ("F2", 1), ("F3", 2), ("F4", 3), ("F5", 4)):
            g.press(k)
            s = g.arms()
            ramp2[lv] = (s["level"], bool(s["look"] & 32))
            wide[lv] = bool(s["look"] & 128)
        check(ramp2 == {0: (0, True), 1: (1, True), 2: (2, False),
                        3: (3, False), 4: (4, False)},
              "F1-F5: the detail levels, RAMP2 on Very Low and Low alone "
              "(%r)" % ramp2)
        check(wide == {0: True, 1: True, 2: False, 3: False, 4: False},
              "the wider level edges (LK_COARSE2) on Very Low and Low alone "
              "(MULTIPLAYER-FPS-PLAN 16.10: %r)" % wide)
        mtr = [g.arms()["mtr"]]             # F10: the meter - off as shipped,
        for _ in range(3):                  # the frame rate, every number, off
            g.press("F10")
            mtr.append(g.arms()["mtr"])
        check(mtr == [0, 1, 2, 0], "F10 cycles the meter off, the frame rate, "
              "every number, off - and it starts off (%r)" % mtr)
        g.press("KeyY")
        check(not g.arms()["look"] & 32, "Y: no key of its own any more")

    if fails:
        print("waweapons: FAIL - %d of the round's checks:" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("waweapons: ok - six weapons, two weapon spawns, four pickups")


if __name__ == "__main__":
    main()
